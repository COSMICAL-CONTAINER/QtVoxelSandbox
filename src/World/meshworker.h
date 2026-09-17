#ifndef MESHWORKER_H
#define MESHWORKER_H

// D6 worker meshing（§29.4 流式激活设计草案 v1 R 节「meshing 瓶颈」风险条目的组件兑现——
// filter 词 r2020）：MeshBuilder 的线程化执行器。形态逐项对照 R20.12 先例
// src/World/backgroundgeneration.h（std::thread + mutex + cv、析构 stop+discard+join、
// 队列 64 满载 kErrQueueFull 可见拒绝、worker 侧零 QObject）——r2012 同门。
//
// ── 组件先行 → §29.5-W4 生产接线（r2026；本段登记随接线同变更如实化）──────────────────────
//   r2020 交付态 = 组件先行零生产接线（ChunkGeometry 同步构建路径零变化，「本组件」记号全
//   src 树只出现在本文件——矩阵 r2020d 全树反探）。**§29.5-W4 起（r2026）生产接线落地**：
//   接线宿主 = GameSession 流式会话（unique_ptr 成员 + World 桥提交缝——「执行器」记号的全
//   src 树白名单随接线同变更修订为 {本文件, gamesession.h}，r2020d 探针纠偏留痕非放宽；
//   World/ChunkGeometry/Main.qml 仍零记号——几何经 World 桥的 std::function 缝可达执行器，
//   执行器类型零泄漏进 Renderer/世界头）。bake 链 = ChunkGeometry 主线程定格快照 → World
//   桥提交（requestId=(cx,cz,段,代次) 派生）→ GameSession pumpStreamingTick 收割拍 takeBuilt
//   → World 注册表路由回几何灌注；同步内联回退 = 满载/已停拒绝时几何走旧路径（拒绝面计数
//   可见）。仅 sparse 流式世界接线（fixed 世界连执行器构造都不发生——D2 零活动墙延续）；
//   QML 零迁移不变量不动。
//
// ── 确定性承重（单一权威不回流的透传形态）──────────────────────────────────────────
//   worker 执行体 = **MeshBuilder::build(snapshot) 唯一调用**（R20.13 网格算法单一权威，
//   r2013d 反探钉「网格本体不回流」继续有效）——本组件禁复制任何网格逻辑、禁对输出做任何
//   加工 / 重排 / 缓存：同快照 → 逐位同输出由 MeshBuilder 纯函数性保证（meshbuilder.h 头注
//   「零可变全局、同输入同输出」），本组件只负责把快照值搬进 worker 线程、把 owning 输出搬
//   回来（矩阵 r2020a 承重墙：worker 产物与主线程同步直调逐位恒等，顶点/索引/统计三面）。
//   执行原因恒 Reason::Dirty（内容驱动重建桶；Reason 只影响 FrameProfiler 计数分桶、与顶点
//   输出无关——meshbuilder.cpp 计数面实读核验）。
//
// ── 快照值越线程安全论证（头注释立证）──────────────────────────────────────────────
//   ChunkMeshSnapshot / ChunkMeshData 均为 QObjectFree owning 值（R20.13 static_assert 编译期
//   钉 + 本头对 MeshBuiltItem 复钉）：稠密域 std::vector / QVector 自持缓冲，零指针零引用零
//   QObject 子对象 → 跨线程按值传递（拷贝 / move）安全，所有权随值单调转移、无共享可变态。
//   **World 读面不越线程**：快照采集（captureChunkMeshSnapshot，~15 万次 Facade 查询）恒留在
//   调用者线程执行（生产接线期契约 = 主线程采集；World/ChunkManager 非线程安全）——本组件
//   只收快照值，绝不满世界查询。执行体 MeshBuilder::build 内部触 FrameProfiler 计数（meshN
//   族）：QMutex 锁保护写路径 + 函数局部 static 单例（C++11 magic static 线程安全初始化），
//   跨线程调用已是该探针的既设计面（frameprofiler.h perf-t520 起写路径统一锁保护）——如实
//   登记，非本组件新增的线程风险。本类自身代码体零 Qt 调用（std::thread/mutex/cv/deque +
//   值类型；Qt 类型只经 MeshBuilder 值接口间接触碰）。
//
// ── 语义铁律：每个被接受的 request 恰产出一条 built（事件可见性同门）──────────────────
//   submit 被接受（返回 ok）→ 恰一条 MeshBuiltItem 进入收割队列（按提交序；requestId 由调用
//   方给定，组件不解释、不查重、不合并）。唯一例外 = 析构丢弃：stop 时队内未开工请求整体
//   丢弃且**逐条计入 droppedCount**（禁静默消失——「丢弃必须被看见」与命令/事件队列同门）；
//   析构时刻已执行完但未收割的 built 属消费方自弃（收割面随对象亡，产出面恰一仍成立）。
//   teardownDroppedSink 测试缝（构造入参，默认 nullptr = 生产零挂载；SaveFaultHook /
//   ageLifetimeClock 测试缝先例）：析构把最终 droppedCount 写入外部哨，使「丢弃计数」越过
//   对象生命周期可见（矩阵 r2020c 析构卫生承重）。
//
// ── 容量策略（照 r2012 口径 + 测试缝压小）──────────────────────────────────────────
//   请求队列上界 kMaxQueuedTasks = 64（与 BackgroundGenerationWorker / scheduler kMaxPendingJobs
//   同量级）：满载 kErrQueueFull 可见拒绝（不覆盖、不静默挤出——请求方重试决策留上游）；收割
//   队列刻意不设上界（**传递性有界**第三形态：每条 built 必然对应一条已受理请求，在途 ≤ 交接
//   数 ≤ 上界，不及时收割只是账面积压、builtCount 可见）。构造入参 maxQueuedTasks 允许测试压
//   小容量（确定性满载面；生产零参用默认 64；负值钳 0 = 拒绝一切的退化上界）。
//
// ── 退出语义（r2012 同门）──────────────────────────────────────────────────────────
//   析构 = stop 置位（锁内顺带完成丢弃计数与落账）+ notify_all + join。worker 循环见 stop 即
//   退出；至多一个在途快照构建跑完（纯函数单 chunk 网格化，毫秒级）→ join 有界返回、绝不挂
//   死。成员声明序 = 构造序：锁 / 队列 / 标志先于线程（threadLoop 在全量就绪后才可能触碰它们）。
//
// ── 登记非目标 ────────────────────────────────────────────────────────────────────
//   快照采集线程化（World 非线程安全——采集恒调用者线程）；多 worker 并发（单线程起步，
//   扩并发 = 后续单）；优先级调度（FIFO 提交序即序）；结果缓存 / 去重（requestId 语义全权
//   归调用方——W4 路由层覆盖语义见 gamesession.h/chunkgeometry 头注释）。
//
// ── 分层 / 线程原语白名单 ──────────────────────────────────────────────────────────
//   World 层 header-only，非 QObject 无 AUTOMOC，无信号槽 / Q_INVOKABLE / Q_PROPERTY（D6 组件
//   不进 QML，R20 主线不变量）；向下依赖 meshbuilder.h（R20.13 单一权威）+ result.h（Core 叶
//   子），不依赖 Renderer/Game/Entities/QML。std::thread 落点：t1023c 白名单同变更修订为双文
//   件——src/World/backgroundgeneration.h（R20.12）+ 本文件（r2020；纪律原文已预见「含 R20.13
//   mesher 线程化」的修订路径，本次为纠偏留痕非放宽：仍仅 std::thread，禁 Qt 线程设施）。

#include <QtGlobal> // quint64

#include <condition_variable>
#include <cstddef>  // size_t
#include <deque>
#include <mutex>
#include <thread>
#include <type_traits> // static_assert 值纪律钉（不依赖 meshbuilder.h 传递包含）

#include "meshbuilder.h" // MeshBuilder::build / ChunkMeshSnapshot / ChunkMeshData（R20.13 单一权威）
#include "result.h"      // Result / Error / kErrQueueFull

// 错误码分域（result.h 百位段约定：2xx = 执行域，域内递增）：submit 对已停止 worker 的用法
// 错误防御面（r2012 kErrWorkerStopped=201 同域递增；正常生命周期内不可达——析构后对象已亡）。
constexpr int kErrMeshWorkerStopped = 202;

// ── MeshBuiltItem：收割面条目（纯值）────────────────────────────────────────────────
// requestId = 调用方给的请求键（组件不解释；接线期由调用方从 chunk key 派生）；mesh =
// MeshBuilder::build 原样 owning 输出（零加工零重排）。
struct MeshBuiltItem
{
    quint64 requestId = 0;
    ChunkMeshData mesh;
};

// 值纪律编译期钉（R20.13 快照/输出同款：owning 值 → 可拷贝而非 QObject）。
static_assert(QObjectFree<MeshBuiltItem>, "MeshBuiltItem must not carry QObject (D6 worker meshing)");
static_assert(std::is_copy_constructible_v<MeshBuiltItem> && std::is_move_constructible_v<MeshBuiltItem>,
              "MeshBuiltItem must be a value (copy/move constructible) for the locked channel");

// ── MeshWorker：MeshBuilder 的单 worker 线程执行器（非 QObject 编排类）────────────────
class MeshWorker
{
public:
    // 请求队列容量（见头注容量策略；构造入参可压小——测试缝，生产零参用本默认）。
    static constexpr int kMaxQueuedTasks = 64;

    // teardownDroppedSink：析构丢弃计数外哨（测试缝，生产零挂载——nullptr 即纯行为组件）；
    // maxQueuedTasks：请求队列上界（<0 钳 0）。两者须在构造（= 起线程）前定格，线程期只读。
    explicit MeshWorker(quint64 *teardownDroppedSink = nullptr, int maxQueuedTasks = kMaxQueuedTasks)
        : m_maxQueued(maxQueuedTasks < 0 ? 0 : maxQueuedTasks),
          m_teardownDroppedSink(teardownDroppedSink),
          m_thread(&MeshWorker::threadLoop, this)
    {
        m_workerThreadId = m_thread.get_id(); // 线程启动后立即定格（成员声明序保证锁/队列先于线程就绪）
    }

    // 退出语义（见头注）：stop + 丢弃计数（队内未开工请求逐条入账，禁静默消失）+ 唤醒 +
    // join（至多一个在途构建跑完；绝不挂死）。外哨在 join 前写定（此后 worker 不再触碰任何成员）。
    ~MeshWorker()
    {
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_stop = true;
            m_dropped += quint64(m_tasks.size());
            m_tasks.clear();
            if (m_teardownDroppedSink)
                *m_teardownDroppedSink = m_dropped;
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    MeshWorker(const MeshWorker &) = delete;
    MeshWorker &operator=(const MeshWorker &) = delete;

    // 提交一个快照构建请求（快照按值深拷贝入队——自持稠密域，拷贝后调用方本地态与 worker
    // 侧解耦）。满载 kErrQueueFull / 已停止 kErrMeshWorkerStopped 可见拒绝（拒绝项不入账、
    // 不产生任何 built）。requestId 由调用方给定，组件不解释。
    Result<void> submit(const ChunkMeshSnapshot &snapshot, quint64 requestId)
    {
        {
            std::lock_guard<std::mutex> lk(m_mu);
            if (m_stop)
                return Result<void>::fail(kErrMeshWorkerStopped, "mesh worker stopped");
            if (int(m_tasks.size()) >= m_maxQueued)
                return Result<void>::fail(kErrQueueFull, "mesh worker request queue full");
            m_tasks.push_back(Task{ snapshot, requestId });
            ++m_accepted;
        }
        m_cv.notify_one();
        return Result<void>::ok();
    }

    // 收割面：取一条已产出条目（FIFO = 提交序；每被接受请求恰一条——析构丢弃除外）。无则
    // false（调用方轮询）。MeshBuiltItem 纯值 move 移交（owning 缓冲不过锁外复制）。
    bool takeBuilt(MeshBuiltItem &out)
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_built.empty())
            return false;
        out = std::move(m_built.front());
        m_built.pop_front();
        return true;
    }

    // ── 只读统计（锁护卫；任务书草案 snapshotQueuedCount 的落位名 = pendingCount）────────
    int pendingCount() const // 排队未开工快照数（析构时即丢弃数上界）
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return int(m_tasks.size());
    }
    int builtCount() const // 已产出未收割条目数（传递性有界的账面积压可见面）
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return int(m_built.size());
    }
    quint64 droppedCount() const // 析构丢弃的已受理请求数（生命周期内恒 0——丢弃只发生在析构）
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_dropped;
    }
    quint64 acceptedCount() const // 累计被接受请求数（恰一产出铁律的对账锚）
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_accepted;
    }

    // 线程身份对账（r2012 同款诊断面）：任务执行线程 == 本工作线程（且非调用者线程）。
    std::thread::id workerThreadId() const { return m_workerThreadId; }
    std::thread::id observedExecThreadId() const
    {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_execThreadId;
    }

private:
    struct Task
    {
        ChunkMeshSnapshot snap; // 按值持有（owning 稠密域——跨线程安全论证见头注）
        quint64 requestId = 0;
    };

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
                    return; // 队内未开工任务已由析构计数入 m_dropped（可见丢弃）；在途构建完成后退出
                t = std::move(m_tasks.front());
                m_tasks.pop_front();
            }
            // 确定性执行体（锁外）：MeshBuilder::build 唯一调用——单一权威，零加工零重排
            // （同快照逐位同输出由其纯函数性保证；Reason 只影响计数分桶，见头注）。
            MeshBuiltItem item;
            item.requestId = t.requestId;
            item.mesh = MeshBuilder::build(t.snap, MeshBuilder::Reason::Dirty);
            {
                std::lock_guard<std::mutex> lk(m_mu);
                m_built.push_back(std::move(item));
            }
        }
    }

    // 成员声明序 = 构造序：容量 / 外哨 / 锁 / 队列 / 标志先于线程（threadLoop 在全量就绪后
    // 才可能触碰它们）。可变态只有锁护卫的两条 FIFO + 计数 + stop——全部单锁串行化。
    const int m_maxQueued;                 // 请求队列上界（构造定格；线程期只读）
    quint64 *const m_teardownDroppedSink;  // 析构丢弃计数外哨（测试缝；仅构造/析构在调用者线程触碰）
    mutable std::mutex m_mu;
    std::condition_variable m_cv;
    std::deque<Task> m_tasks;          // 待执行 FIFO（上界 m_maxQueued；stop 时整体丢弃并计数）
    std::deque<MeshBuiltItem> m_built; // 产出 FIFO（提交序；传递性有界，见头注容量策略）
    std::thread::id m_execThreadId;    // threadLoop 入口定格（线程身份对账）
    quint64 m_accepted = 0;            // 累计受理（恰一产出对账锚）
    quint64 m_dropped = 0;             // 析构丢弃累计（生命周期内恒 0）
    bool m_stop = false;               // 停止标志（析构置位；见头注退出语义）
    std::thread m_thread;              // 线程体最后声明（构造序收尾）
    std::thread::id m_workerThreadId;  // 线程启动后于构造体定格
};

#endif // MESHWORKER_H
