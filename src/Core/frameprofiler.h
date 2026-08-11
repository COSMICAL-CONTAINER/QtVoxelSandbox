#ifndef FRAMEPROFILER_H
#define FRAMEPROFILER_H

#include <QObject>
#include <QMutex>
#include <QString>
#include <QtQml/qqml.h>

#include <QtGlobal> // qint64

#include <string>
#include <unordered_map>

// 帧时间分解探针（perf 诊断，PLAN §2-F / §4 帧时间切分）。
//
// 背景：用户报进游戏 <10 FPS（简单版曾 100 FPS），且「渲染段数 culling 把段数 600→154 完全没用、
//   把渲染距离拉到最低还 <10 FPS」→ 瓶颈不在「渲染多少段」，而在「每帧固定开销」（与段数无关的
//   CPU 热路径：实体 tick / mesh 重建 / 物理 / QML binding）。本探针把每帧 / 每窗口各阶段 CPU 耗时
//   量化暴露，让用户进游戏跑几秒看 F3 / 日志即可定位瓶颈花在哪一阶段，不再猜。
//
// 设计：进程全局单例。C++ 各热路径（PlayerController tickImpl / ChunkGeometry buildMesh / World
//   tick 函数）经 RAII Scope 把各阶段耗时累加进具名桶；PlayerController 每 ~1s（其既有 60-tick
//   perf 窗口）调 flush() → 格式化报告字符串 → QML F3 叠层只读消费 + qInfo 落 logs/voxelsandbox.log。
//
// 桶分类（report 时据名字前缀分组）：
//   - 逐帧桶（60Hz tickImpl 内）：env / item / xp / boat / mob / pickup / phys / ray / input
//     → report 为 ms/frame（桶累加 ns / 窗口帧数 / 1e6）。
//   - 窗口桶（10Hz / 事件驱动，与帧数无关）：mesh（buildMesh，含 rebuild 次数）+ w 前缀（World tick 函数）
//     → report 为窗口内总 ms。
//   sim（= 逐帧桶之和 / 帧数）≈ player.simMs（既有 1s tick CPU 平均），交叉核对两路计时一致。
//
// 帧分解桶（main_total / render_cpu / qmlSync，perf-t520 新增）—— 区分 GUI 主线程 vs 渲染线程瓶颈：
//   - main_total：frameSwapped → 下一 frameSwapped 的总耗时（GUI 线程帧周期；含 sim + QML binding /
//     scenegraph update / 同步等待）。在 threaded render loop 下 ≈ 整帧时间，是「帧率上限」。
//   - render_cpu：beforeRendering → afterRendering 的耗时（渲染线程 CPU 侧编码 + GPU 提交阻塞；
//     **非**真 GPU 时间 —— QtQuick3D 路径无公开 GPU 计时查询，render_cpu 含 GPU stall 但不等同纯 GPU
//     时间，已在报告字串中诚实标注）。两者均从 main.cpp 经 addSampleMs 推入，按 ms/frame 报告。
//   - t488 residual 残留桶：report 时按 main_total − (sim 逐帧和 + qmlSync) 派生（不单独计时），显式量化
//     main_total 内没被 sim/qmlSync 覆盖的部分。诊断：residual 大 + render_cpu 同量级大 → 主线程在等渲染
//     线程（frameSwapped 被渲染节奏拖晚，vsync / 渲染 bound）；residual 大 + render_cpu 小 → 主线程有
//     未插桩重活（QML 绑定扇出 / chunk mesh 重建 / 实体 delegate 高水位）。residual ≈ frame 周期 − 已知
//     桶之和，正常帧率下 ≈ vsync 等待（非浪费 CPU）。
//   诊断公式（threaded render loop）：frame = max(main_total, render_cpu) 近似。
//     - main_total >> render_cpu → 主线程 bound（QML binding / 物理 tick / scene-graph update）。
//     - render_cpu >> main_total → 渲染线程 bound（GPU 提交 / 渲染队列长 / draw-call 多）。
//   main.cpp hook：QQuickWindow::frameSwapped（GUI 线程）→ main_total；
//     QQuickWindow::beforeRendering / afterRendering（渲染线程，DirectConnection）→ render_cpu。
//
// 全 GUI 线程访问（PlayerController QTimer / QML 信号槽直连 / WorldClock QTimer 均在 GUI 线程）→
//   无锁。即便将来线程化，本探针是诊断工具、非数据通路，竞态最坏导致某次报告数字偏差一帧，可接受。
//   perf-t520 新增 main_total / render_cpu 走 addSampleMs（QtQuick3D beforeRendering / afterRendering
//   在渲染线程发射 → 跨线程写 m_ns → 加 QMutex 保护，免 hash race 导致崩溃 / 数字乱跳）。
//
// 分层（PLAN §2）：Core 叶子（只依赖 Qt Core/Qml），无向上依赖。所有上层 include 它推送数据；
//   QML F3 只读 report 字符串。属 Core → Game/World/Renderer 任意上层均可向下 include（铁律成立）。
class FrameProfiler : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FrameProfiler)
    QML_SINGLETON
    Q_PROPERTY(QString report READ report NOTIFY reportChanged)
    Q_PROPERTY(int frames READ frames NOTIFY reportChanged)
public:
    // QML 单例工厂（QML_SINGLETON 要求）：返回全局唯一实例（C++ / QML 共用同一对象）。
    static FrameProfiler *create(QQmlEngine *, QJSEngine *) { return instance(); }
    // C++ 全局访问点（PlayerController / ChunkGeometry / World / main.cpp 经它推送计时）。
    static FrameProfiler *instance();

    // 单调纳秒（进程启动来累；RAII Scope 与手动子桶计时共用同一静态 QElapsedTimer，时间基一致可交叉对照）。
    //   手动计时用途：热路径内一段代码跨多条 continue / 分支无法用单个 RAII Scope 包裹时（如 EntityManager
    //   tick 的 mob 循环内 AI-pass vs 物理-pass 拆分），用 nowNs() 手动取 t0/t1 累加进具名桶，避免每实体
    //   构造 Scope 的开销（nowNs 仅 1× nsecsElapsed，比 Scope 构造+析构 2× nowNs + map add 更轻）。
    //   亦被 main.cpp 跨线程读取（render_cpu 计时取 before/afterRendering 时间戳）—— QElapsedTimer
    //   ::nsecsElapsed 本身是线程安全的（只读静态计时器），无需加锁。
    static qint64 nowNs();

    // RAII 计时段：构造记 t0，析构把耗时累加进 name 桶。name 须为静态字面量（不拷贝、不释放）。
    //   用法：{ FrameProfiler::Scope s("mob"); ...work... }
    class Scope
    {
    public:
        explicit Scope(const char *name) : m_name(name), m_t0(FrameProfiler::nowNs()) {}
        ~Scope() { FrameProfiler::instance()->add(m_name, FrameProfiler::nowNs() - m_t0); }
        Q_DISABLE_COPY_MOVE(Scope)
    private:
        const char *m_name;
        qint64 m_t0;
    };

    QString report() const { return m_report; }
    int frames() const { return m_lastFrames; }

    // 累加 name 桶 ns（Scope 析构调）。name 静态字面量。GUI 线程调用，加锁（perf-t520：addSampleMs
    //   跨线程后，所有写路径统一锁保护 m_ns）。
    void add(const char *name, qint64 ns);
    // 累加 name 事件计数（用于统计 mesh rebuild 次数等）。name 静态字面量。
    void count(const char *name);
    // 调用方每 60Hz tick 调一次 → 累帧计数（逐帧桶 report 时除以它）。
    void tickFrame() { ++m_frameCount; }
    // 每 ~1s 调一次（PlayerController 既有 perf 窗口）：把当前窗口各桶 → 报告，重置窗口，emit + qInfo。
    void flush();

    // t500 perf / perf-t520：调用方推一个 ms 样本到 name 桶。name 可为静态字面量（add）亦可为
    //   动态 QString（addSampleMs 内部转 std::string 做 unordered_map 键）。QML 侧（BlockParticles.qml
    //   的 50Hz Timer）与 C++ 侧（main.cpp 的 frameSwapped / beforeRendering / afterRendering）共用本入口。
    //   跨线程安全：内部加 QMutex（render_cpu 由渲染线程发射、main_total 由 GUI 线程发射 → 必须锁）。
    //   ms<=0 忽略（防 0 / 负值噪声）。分层（PLAN §2）：Core 叶子，仅依赖 Qt Core/Qml；QML 经
    //   QML_NAMED_ELEMENT 单例访问，C++ 经 instance() 访问。
    Q_INVOKABLE void addSampleMs(const QString &name, double ms);

signals:
    void reportChanged();

private:
    FrameProfiler();
    // 逐帧桶（report 时 ÷ m_frameCount 得 ms/frame）。
    static const char *const kFramePhases[9];
    // 查桶 ns（不存在 → 0）。锁内调用（持 m_mutex）。
    qint64 bucketLocked(const char *name) const;

    mutable QMutex m_mutex;                              // 保护 m_ns / m_counts / m_frameCount（跨线程读写）
    std::unordered_map<std::string, qint64> m_ns;        // 当前窗口累加耗时（ns），键 = phase name
    std::unordered_map<std::string, qint64> m_counts;    // 当前窗口累加事件计数，键 = name（如 "meshN"）
    int m_frameCount = 0;                                // 当前窗口 60Hz tick 帧数
    QString m_report;                                    // 上次 flush 的报告（F3 绑定读它；锁外只读 OK）
    int m_lastFrames = 0;                                // 上次窗口帧数（Q_PROPERTY frames 读它）
};

#endif // FRAMEPROFILER_H
