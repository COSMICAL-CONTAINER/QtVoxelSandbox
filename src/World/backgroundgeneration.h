#ifndef BACKGROUNDGENERATION_H
#define BACKGROUNDGENERATION_H

// R20.12 后台 GenerationJob（refactor-plan §29.3 R20.12「迁移一个最小地形生成路径」——四验收
// 即硬线：①GUI 不被整 Chunk 生成阻塞；②seed、ChunkKey、generator version 决定输出；③world
// epoch 可以丢弃旧任务；④退出时不会被任务卡死）。本头是 R20.11 GenerationWorker 缝的**第三个
// 实现**（前两个 = r2011c 的 TraceWorker/AltTraceWorker 可替换性实证）——后台 worker 经
// isAsynchronous()/submitAsync/takeCompletedAsync 三点接入 scheduler 异步协议，调用者面
// （submit/cancel/pump/takeOutcome）零改动（「换 worker 不改调用者」的结构性主张）。
//
// ── 线程原语选型（头注释立此存照）──────────────────────────────────────────────────
//   std::thread + std::mutex + std::condition_variable，**不用 Qt 线程设施**。依据：R20.12
//   红线（audit #8 已立）= worker 侧禁 QObject/QML/QQuick3D——worker 全程不持任何 QObject
//   （输入 GenerationRequest / 输出 GeneratedChunkData 都是纯值；执行体 TerrainGen 纯函数），
//   Qt 线程设施（QObject 派生的线程载体 + 事件循环投递）会把 QObject 语义重新引入 worker 侧，
//   背离红线；标准线程原语 + 锁内值移交即完整覆盖需求，且 t1023c 事实钉的同变更修订恰好把
//   本文件登记为 src/ 树首个受认可的线程原语落点（r2020/D6 起白名单再修订为双文件：本文件 +
//   World/meshworker.h——MeshBuilder 线程化执行器，生产零接线；仍仅 std::thread，纠偏留痕非
//   放宽）。
//
// ── 线程安全根据（无共享可变态）────────────────────────────────────────────────────
//   执行体 TerrainGen 构造后只读（置换表构造期填定，纯函数采样）；任务输入按值拷入；输出写
//   进局部自持缓冲后整体移交。跨线程可变态只有锁护卫的三条 FIFO（待执行/完成/数据）+ stop
//   标志——全部单锁串行化。worker 输入 = (seed, ChunkKey, generator version)（经 TerrainGen
//   一体化），输出自持数据缓冲（QObjectFree 编译期钉）——**纯函数**：同输入同输出，不把任何
//   全局 RNG / 运行期随机源带进 worker（worldgen 无运行期 RNG 是既有证据，PLAN §2-K）。
//
// ── 容量策略（延续 R20.06 分化登记口径）────────────────────────────────────────────
//   待执行队列上界 kMaxQueuedTasks=64（与 scheduler kMaxPendingJobs 同量级）：满载 kErrQueueFull
//   可见拒绝 → scheduler 交接相按背压处理（job 留队下轮 pump 再试）——不覆盖、不静默挤出。
//   完成/数据两队列刻意不设上界：每条完成记录必然对应一条已受理任务（在途 ≤ 交接数 ≤ 上界），
//   容量被上游传递性封顶；主线程不及时收割只是账面积压（诊断面 resultDataCount 可见），无
//   生产者/消费者不对称的丢失风险（与命令/事件队列「满载拒绝」、快照队列「覆盖最老」的两域
//   分化并列为第三形态：**传递性有界**）。
//
// ── 退出语义（验收④）──────────────────────────────────────────────────────────────
//   析构 = 停止接受（stop 置位）+ 唤醒（notify_all）+ join。worker 循环见 stop 即退出——
//   **队内未开工任务整体丢弃**（与「世界已亡，旧任务产物无投递价值」的 epoch 丢弃同门：调度
//   侧 epoch 过滤本就不投递过期结果，早停只是把同一语义前移到执行侧，省掉无用功）；至多一个
//   在途任务跑完（纯函数单 chunk 生成，毫秒级）→ join 有界返回。矩阵 r2012d 以「提交后立即
//   析构，腿体在 deadline 内走完」实证无挂死。
//
// ── 结果应用（主线程）──────────────────────────────────────────────────────────────
//   worker 只算不自写世界：执行线程只产缓冲；收割/投递全部发生在调用者线程 pump/takeOutcome
//   侧。数据经 applyGeneratedChunkData（ChunkManager::setBlock 5 参守卫入口——越界拒 / 标脏 /
//   边界邻接标脏，与 worldgen 直写 chunk 的静默约定一致）落格；生命周期边①（交接时）边②
//   （收割时）由 scheduler pumpAsync 经 setLifecycle 驱动（与 R20.11 同款 best-effort，非法
//   转移被守卫拒即忽略 = 固定世界零变化）；失败恢复边⑨（Loading→Absent，R20.10b）同由
//   scheduler 在失败 outcome **实际投递**时驱动（交接后才过期/取消的丢弃面不转移）。本
//   worker 自身生成恒成功面（纯函数无失败路径），边⑨经异步协议对任意 isAsynchronous()
//   worker 生效（矩阵 r2010bb 脚本化异步 worker 实证）。真实生产接线（Absent-only 策略层 /
//   World 挂点）仍登记后续——本单 app 面零变化。
//
// ── 分层 / QML 面 ──────────────────────────────────────────────────────────────────
//   World 层 header-only，非 QObject 无 AUTOMOC，无 Q_INVOKABLE/Q_PROPERTY（生命周期与调度
//   决策不进 QML，R20 主线不变量）；向下依赖 Core 叶子 + World 层头（generationjob /
//   terraingen / chunkmanager），不依赖 Renderer/Game/Entities/QML。

#include <QtGlobal> // quint8 / quint32 / quint64

#include <condition_variable>
#include <cstddef>  // size_t
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "chunkmanager.h"    // ChunkManager（结果应用守卫入口）+ Chunk（尺寸互钉）
#include "generationjob.h"   // GenerationWorker / GenerationRequest / CompletedGeneration（R20.11 缝）
#include "result.h"          // Result / Error / kErrQueueFull
#include "terraingen.h"      // TerrainGen（R20.12 纯地形生成单一权威）

// 错误码分域（result.h 百位段约定：百位段 = 域，域内递增）：2xx = 执行域。
constexpr int kErrWorkerStopped = 201; // worker 已停止受理 / 同步缝对异步实现误用

// ── GeneratedChunkData：worker 的自持体素输出缓冲（QObjectFree 钉）────────────────────
// 携带输出三要素（seed / key / generatorVersion）+ 定长双数组（blocks/states，索引布局同
// Chunk：lx + kChunkSize*(lz + kChunkSize*ly)）。owning vector → 非 trivially copyable（跨
// 线程移交用 unique_ptr move；与 Request/Outcome 的 trivially-copyable 钉是两个面：消息 vs
// 载荷，登记分化）。contentHash/identical 供逐位恒等腿（r2012b 承重墙）与未来复用缓存比对。
struct GeneratedChunkData
{
    ChunkKey key{};
    quint32 seed = 0;
    quint32 generatorVersion = 0;
    int originX = 0; // chunk 原点世界坐标（cx * kChunkSize；负坐标同等合法）
    int originZ = 0;
    int height = 0; // Y 容量（= 世界高度；chunk 跨满高）
    std::vector<quint8> blocks; // id 逐体素
    std::vector<quint8> states; // state 逐体素（SnowLayer 等；常规方块恒 0）

    bool valid() const
    {
        const size_t want = size_t(TerrainGen::kChunkSize) * size_t(TerrainGen::kChunkSize)
            * size_t(height > 0 ? height : 0);
        return height > 0 && blocks.size() == want && states.size() == want;
    }

    quint8 blockAt(int lx, int ly, int lz) const
    {
        if (lx < 0 || ly < 0 || lz < 0 || lx >= TerrainGen::kChunkSize || lz >= TerrainGen::kChunkSize
            || ly >= height)
            return 0;
        return blocks[index(lx, ly, lz)];
    }
    quint8 stateAt(int lx, int ly, int lz) const
    {
        if (lx < 0 || ly < 0 || lz < 0 || lx >= TerrainGen::kChunkSize || lz >= TerrainGen::kChunkSize
            || ly >= height)
            return 0;
        return states[index(lx, ly, lz)];
    }

    // 逐位内容指纹（FNV-1a over meta + 双数组；确定性、无随机盐——腿比对 / 未来缓存键用）。
    quint64 contentHash() const
    {
        quint64 h = 0xcbf29ce484222325ull;
        auto step = [&h](quint64 v) {
            h ^= v;
            h *= 0x100000001b3ull;
        };
        step(quint64(key.packed()));
        step(seed);
        step(generatorVersion);
        step(quint64(originX));
        step(quint64(originZ));
        step(quint64(height));
        for (quint8 b : blocks) step(b);
        for (quint8 s : states) step(s);
        // FNV 末轮 avalanche 同 hashColumn 尾混（无随机盐 → 跨进程/平台稳定）。
        h ^= h >> 16;
        h *= 0x7feb352dull;
        h ^= h >> 15;
        return h;
    }

    // 逐位恒等：元信息（三要素 + 原点 + 容量）与双数组全等（r2012b 的 A≡B 断言面）。
    bool identical(const GeneratedChunkData &o) const
    {
        return key == o.key && seed == o.seed && generatorVersion == o.generatorVersion
            && originX == o.originX && originZ == o.originZ && height == o.height
            && blocks == o.blocks && states == o.states;
    }

private:
    size_t index(int lx, int ly, int lz) const
    {
        return size_t(lx) + size_t(TerrainGen::kChunkSize)
            * (size_t(lz) + size_t(TerrainGen::kChunkSize) * size_t(ly));
    }
};

// 值纪律编译期钉（R20.05/06 先例；owning vector → 非 trivially copyable，见头注分化登记）。
static_assert(QObjectFree<GeneratedChunkData>, "GeneratedChunkData must be QObject-free (plan §29.3 R20.12)");

// chunk 边长互钉：Core 叶子不能引用 Chunk::kSize（mathtypes.h 契约），权威等式在此编译期闭环。
static_assert(TerrainGen::kChunkSize == Chunk::kSize, "TerrainGen::kChunkSize must match Chunk::kSize");

// ── generateTerrainChunk：worker 与矩阵同步对照共用的单 chunk 生成（单一权威的 chunk 视图）──
// 逐列 fillTerrainColumn（terraingen.h）→ 缓冲 sink 写入；越界列（key 出世界网格时）按缓冲
// 边界裁写（确定性；合法提交由策略层保证在界——登记 R20.12+ 接线面）。每次调用独立构缓冲，
// 无共享可变态。
inline std::unique_ptr<GeneratedChunkData> generateTerrainChunk(const TerrainGen &terrain, const ChunkKey &key)
{
    auto d = std::make_unique<GeneratedChunkData>();
    d->key = key;
    d->seed = quint32(terrain.seed()); // int → quint32 补码映射（负 seed 同样确定性）
    d->generatorVersion = TerrainGen::kGeneratorVersion;
    d->originX = key.cx * TerrainGen::kChunkSize;
    d->originZ = key.cz * TerrainGen::kChunkSize;
    d->height = terrain.dims().height;
    const size_t count = size_t(TerrainGen::kChunkSize) * size_t(TerrainGen::kChunkSize)
        * size_t(d->height);
    d->blocks.assign(count, 0);
    d->states.assign(count, 0);
    struct BufferSink
    {
        GeneratedChunkData *d;
        void write(int x, int y, int z, quint8 id, quint8 state)
        {
            const int lx = x - d->originX, lz = z - d->originZ;
            if (lx < 0 || lz < 0 || lx >= TerrainGen::kChunkSize || lz >= TerrainGen::kChunkSize
                || y < 0 || y >= d->height)
                return; // 缓冲边界裁写（越界列丢弃——确定性）
            const size_t i = size_t(lx) + size_t(TerrainGen::kChunkSize)
                * (size_t(lz) + size_t(TerrainGen::kChunkSize) * size_t(y));
            d->blocks[i] = id;
            d->states[i] = state;
        }
    } sink{ d.get() };
    for (int lz = 0; lz < TerrainGen::kChunkSize; ++lz)
        for (int lx = 0; lx < TerrainGen::kChunkSize; ++lx)
            terrain.fillTerrainColumn(d->originX + lx, d->originZ + lz, sink);
    return d;
}

// ── applyGeneratedChunkData：结果应用主线程入口（既有守卫入口写入）────────────────────
// 经 ChunkManager::setBlock 5 参守卫（越界拒 / chunk 路由 / 标脏含边界邻接 / heightmap 增量
// 维护）逐格落地；空气格零写（新网格本就全零，语义等价、免噪声）。键不在网格 / 容量不匹配
// → false（防御）。只应在调用者线程调（ChunkManager 非线程安全——worker 侧禁触世界）。
inline bool applyGeneratedChunkData(ChunkManager &mgr, const GeneratedChunkData &d)
{
    if (!d.valid())
        return false;
    Chunk *c = mgr.chunk(d.key.cx, d.key.cz);
    if (!c || c->height() != d.height)
        return false;
    bool all = true;
    for (int y = 0; y < d.height; ++y) {
        for (int lz = 0; lz < TerrainGen::kChunkSize; ++lz) {
            for (int lx = 0; lx < TerrainGen::kChunkSize; ++lx) {
                const quint8 id = d.blockAt(lx, y, lz);
                if (id == 0)
                    continue; // 空气格零写
                all = mgr.setBlock(d.originX + lx, y, d.originZ + lz, id, d.stateAt(lx, y, lz))
                    && all;
            }
        }
    }
    return all;
}

// ── BackgroundGenerationWorker：GenerationWorker 的第三个实现（真线程）────────────────
class BackgroundGenerationWorker : public GenerationWorker
{
public:
    // 线程期容量（见头注容量策略）：满载 kErrQueueFull 可见拒绝 → scheduler 背压留队重试。
    static constexpr int kMaxQueuedTasks = 64;

    BackgroundGenerationWorker(int seed, TerrainGen::Dims dims)
        : m_terrain(seed, dims), m_thread(&BackgroundGenerationWorker::threadLoop, this)
    {
        m_threadId = m_thread.get_id(); // 线程启动后立即定格（成员声明序保证锁/队列先于线程就绪）
    }

    // 验收④：停止接受 + 唤醒 + join（队内未开工任务整体丢弃；join 上界 = 一个在途任务耗时）。
    ~BackgroundGenerationWorker() override
    {
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    BackgroundGenerationWorker(const BackgroundGenerationWorker &) = delete;
    BackgroundGenerationWorker &operator=(const BackgroundGenerationWorker &) = delete;

    // 同步 execute 缝：本实现 async-only，isAsynchronous()=true 时 scheduler 不再内联执行；
    // 直调 = 用法错误，防御性可见 fail（不吞错——与 r2011c 失败穿透同门）。
    Result<void> execute(const GenerationRequest &req) override
    {
        Q_UNUSED(req);
        return Result<void>::fail(kErrWorkerStopped,
                                  "BackgroundGenerationWorker is async-only (use submitAsync)");
    }

    bool isAsynchronous() const override { return true; }

    Result<void> submitAsync(const GenerationRequest &req, quint64 jobId) override
    {
        {
            std::lock_guard<std::mutex> lk(m_mu);
            if (m_stop)
                return Result<void>::fail(kErrWorkerStopped, "background generation worker stopped");
            if (int(m_tasks.size()) >= kMaxQueuedTasks)
                return Result<void>::fail(kErrQueueFull, "background generation task queue full");
            m_tasks.push_back(Task{ req, jobId });
        }
        m_cv.notify_one();
        return Result<void>::ok();
    }

    bool takeCompletedAsync(CompletedGeneration &out) override
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_done.empty())
            return false;
        out = m_done.front();
        m_done.pop_front();
        return true;
    }

    // 主线程数据面：与完成记录同序的自持缓冲（每完成 job 恰一条——scheduler 收割 outcome 与
    // 本接口取数据同源同序，按完成序配对）。unique_ptr move 移交（owning 值不过锁外复制）。
    bool takeResultData(std::unique_ptr<GeneratedChunkData> &out)
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_data.empty())
            return false;
        out = std::move(m_data.front());
        m_data.pop_front();
        return true;
    }
    int resultDataCount() const
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return int(m_data.size());
    }

    // 诊断（矩阵 r2012a 线程身份断言面）：任务执行线程 == 本工作线程（且非调用者线程）。
    std::thread::id workerThreadId() const { return m_threadId; }
    std::thread::id observedExecThreadId() const
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_execThreadId;
    }
    // 已执行任务数（诊断；锁护卫——worker 侧计，调用者线程读）。
    quint64 executedCount() const
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_executed;
    }

private:
    struct Task
    {
        GenerationRequest req{};
        quint64 jobId = 0;
    };
    // 完成记录直接复用缝侧 CompletedGeneration（jobId + Error；类型同一 = takeCompletedAsync
    // 直拷零换算）。

    void threadLoop()
    {
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_execThreadId = std::this_thread::get_id(); // 入口定格（先于任何任务，锁内可见）
        }
        for (;;) {
            Task t;
            {
                std::unique_lock<std::mutex> lk(m_mu);
                m_cv.wait(lk, [this] { return m_stop || !m_tasks.empty(); });
                if (m_stop)
                    return; // 停止 → 队内未开工任务整体丢弃（见头注退出语义）；在途任务完成后退出
                t = m_tasks.front();
                m_tasks.pop_front();
            }
            // 纯函数生成（锁外）：TerrainGen 只读 + 局部缓冲——无共享可变态（线程安全根据）。
            std::unique_ptr<GeneratedChunkData> data = generateTerrainChunk(m_terrain, t.req.key);
            {
                std::lock_guard<std::mutex> lk(m_mu);
                m_done.push_back(CompletedGeneration{ t.jobId, Error{} }); // 生成恒成功面（纯函数无失败路径）
                m_data.push_back(std::move(data));
                ++m_executed;
            }
        }
    }

    // 成员声明序 = 构造序：锁 / 队列 / 标志先于线程（threadLoop 在全量就绪后才可能触碰它们）。
    TerrainGen m_terrain; // (seed, dims, 置换表) 一体；构造后只读 → 跨线程共享读安全
    mutable std::mutex m_mu;
    std::condition_variable m_cv;
    std::deque<Task> m_tasks;  // 待执行（上界 kMaxQueuedTasks；stop 时整体丢弃）
    std::deque<CompletedGeneration> m_done; // 完成记录 FIFO（传递性有界，见头注容量策略）
    std::deque<std::unique_ptr<GeneratedChunkData>> m_data; // 与 m_done 同序的自持缓冲
    std::thread::id m_execThreadId; // threadLoop 入口定格（线程身份对账）
    quint64 m_executed = 0;     // 已执行任务计数（诊断）
    bool m_stop = false;        // 停止标志（析构置位；见头注退出语义）
    std::thread m_thread;       // 线程体最后声明（构造序收尾）
    std::thread::id m_threadId; // 线程启动后于构造体定格
};

#endif // BACKGROUNDGENERATION_H
