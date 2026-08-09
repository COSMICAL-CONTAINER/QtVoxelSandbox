#ifndef FRAMEPROFILER_H
#define FRAMEPROFILER_H

#include <QObject>
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
// 全 GUI 线程访问（PlayerController QTimer / QML 信号槽直连 / WorldClock QTimer 均在 GUI 线程）→
//   无锁。即便将来线程化，本探针是诊断工具、非数据通路，竞态最坏导致某次报告数字偏差一帧，可接受。
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
    // C++ 全局访问点（PlayerController / ChunkGeometry / World 经它推送计时）。
    static FrameProfiler *instance();

    // RAII 计时段：构造记 t0，析构把耗时累加进 name 桶。name 须为静态字面量（不拷贝、不释放）。
    //   用法：{ FrameProfiler::Scope s("mob"); ...work... }
    class Scope
    {
    public:
        explicit Scope(const char *name) : m_name(name), m_t0(nowNs()) {}
        ~Scope() { FrameProfiler::instance()->add(m_name, nowNs() - m_t0); }
        Q_DISABLE_COPY(Scope)
    private:
        static qint64 nowNs();
        const char *m_name;
        qint64 m_t0;
    };

    QString report() const { return m_report; }
    int frames() const { return m_lastFrames; }

    // 累加 name 桶 ns（Scope 析构调）。name 静态字面量。
    void add(const char *name, qint64 ns);
    // 累加 name 事件计数（用于统计 mesh rebuild 次数等）。name 静态字面量。
    void count(const char *name);
    // 调用方每 60Hz tick 调一次 → 累帧计数（逐帧桶 report 时除以它）。
    void tickFrame() { ++m_frameCount; }
    // 每 ~1s 调一次（PlayerController 既有 perf 窗口）：把当前窗口各桶 → 报告，重置窗口，emit + qInfo。
    void flush();

    // t500 perf：QML 侧（如 BlockParticles.qml 的 50Hz Timer）经它推一个样本到 name 桶（ms 单位）。
    //   QML Timer 跑在 GUI 线程、与 C++ 60Hz tick 同线程但异步节拍 → 样本累加进窗口桶、report 时按
    //   「窗口总 ms」展示（同 mesh/w 前缀桶），不除帧数。name 须为静态字面量（同 add 契约）。
    //   分层（PLAN §2）：Core 叶子，仅依赖 Qt Core/Qml；QML 经 QML_NAMED_ELEMENT 单例访问。
    Q_INVOKABLE void addSampleMs(const QString &name, double ms);

signals:
    void reportChanged();

private:
    FrameProfiler();
    // 逐帧桶（report 时 ÷ m_frameCount 得 ms/frame）。
    static const char *const kFramePhases[9];
    // 查桶 ns（不存在 → 0）。
    qint64 bucket(const char *name) const;

    std::unordered_map<std::string, qint64> m_ns;     // 当前窗口累加耗时（ns），键 = phase name
    std::unordered_map<std::string, qint64> m_counts; // 当前窗口累加事件计数，键 = name（如 "meshN"）
    int m_frameCount = 0;                              // 当前窗口 60Hz tick 帧数
    QString m_report;                                  // 上次 flush 的报告（F3 绑定读它）
    int m_lastFrames = 0;                              // 上次窗口帧数（Q_PROPERTY frames 读它）
};

#endif // FRAMEPROFILER_H
