#include "frameprofiler.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

#include <cmath>
#include <vector>

namespace {
Q_LOGGING_CATEGORY(lcProf, "vo.prof") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤 → 落 log 可见
}

// 逐帧桶名表（report 时按序输出，÷ m_frameCount 得 ms/frame）。须与 PlayerController tickImpl
//   插桩用的 Scope 名严格一致（一处拼错 → 该桶 report 恒 0，无副作用但不报数）。
const char *const FrameProfiler::kFramePhases[9] = {
    "env", "item", "xp", "boat", "mob", "pickup", "phys", "ray", "input"
};

FrameProfiler::FrameProfiler() = default;

FrameProfiler *FrameProfiler::instance()
{
    static FrameProfiler inst;
    // QQmlEngine 对单例有所有权管理；本进程静态实例寿命长于任何 QML engine，安全。
    // 多次 create() 调用返回同一指针，QML 单例机制允许（jd 注册一次）。
    return &inst;
}

// 单调纳秒（进程启动来累）。RAII Scope 与手动子桶计时共用同一静态 QElapsedTimer → 时间基一致可交叉对照。
//   实现同原 Scope::nowNs（静态 QElapsedTimer 启动即 start），提到 FrameProfiler 级以便手动计时调用。
qint64 FrameProfiler::nowNs()
{
    static QElapsedTimer t;
    if (!t.isValid()) t.start();
    return t.nsecsElapsed();
}

void FrameProfiler::add(const char *name, qint64 ns)
{
    m_ns[name] += ns;
}

void FrameProfiler::count(const char *name)
{
    m_counts[name] += 1;
}

// t500 QML 侧样本入口（BlockParticles.qml 等）：name → ns 累加进 m_ns（同 add 路径，但 name 非 const char* 字面量
//   而是 QString → 转 std::string 做 unordered_map 键；QML 一周 ~50 调用，开销可忽略）。
void FrameProfiler::addSampleMs(const QString &name, double ms)
{
    if (ms <= 0.0) return; // 防 0 / 负值（Date.now 精度噪声）
    m_ns[name.toStdString()] += qint64(ms * 1e6);
}

qint64 FrameProfiler::bucket(const char *name) const
{
    auto it = m_ns.find(name);
    return it == m_ns.end() ? 0 : it->second;
}

// 格式化窗口报告：逐帧桶报 ms/frame、窗口桶报总 ms、mesh 附 rebuild 次数、world 各 tick 拆分。
//   发完清窗口桶 / 帧数，保 m_report + m_lastFrames 给 F3 绑定读（直到下次 flush）。
void FrameProfiler::flush()
{
    const int frames = m_frameCount > 0 ? m_frameCount : 1; // 防 0 除（窗口内无 tick 帧 → 用 1 兜底）
    const double f = frames;

    // 逐帧桶 ms/frame：tickImpl 内各阶段平均每帧耗时。
    std::vector<double> fpMs;
    fpMs.reserve(9);
    double simMs = 0.0;
    for (const char *p : kFramePhases) {
        const double ms = double(bucket(p)) / 1e6 / f;
        fpMs.push_back(ms);
        simMs += ms;
    }

    // 窗口桶（事件 / 10Hz，与帧数无关）：mesh 总 ms + rebuild 次数；world tick 各总 ms + 汇总。
    const double meshMs = double(bucket("mesh")) / 1e6;
    const qint64 meshN = [this]() { auto it = m_counts.find("meshN"); return it == m_counts.end() ? 0 : it->second; }();
    // w 前缀桶：World 9 个 tick 函数（wWater/wLava/wCrop/wSug/wFarm/wSap/wIce/wLeaf/wWeath）。
    struct WEnt { const char *key; const char *label; };
    static const WEnt wEntries[] = {
        {"wWater", "water"}, {"wLava", "lava"}, {"wCrop", "crop"},
        {"wSug", "sug"}, {"wFarm", "farm"}, {"wSap", "sap"},
        {"wIce", "ice"}, {"wLeaf", "leaf"}, {"wWeath", "wthr"}
    };
    double worldMs = 0.0;
    std::vector<double> wMs;
    wMs.reserve(9);
    for (const WEnt &e : wEntries) {
        const double ms = double(bucket(e.key)) / 1e6;
        wMs.push_back(ms);
        worldMs += ms;
    }

    // 格式化（两行，monospace 友好；NaN/极大保护由各 ms 自身 clamp 隐式）。
    QString tickLine = QStringLiteral("tick ms/f: ")
        + "env " + QString::number(fpMs[0], 'f', 2)
        + "  item " + QString::number(fpMs[1], 'f', 2)
        + "  xp " + QString::number(fpMs[2], 'f', 2)
        + "  boat " + QString::number(fpMs[3], 'f', 2)
        + "  mob " + QString::number(fpMs[4], 'f', 2)
        + "  pick " + QString::number(fpMs[5], 'f', 2)
        + "  phys " + QString::number(fpMs[6], 'f', 2)
        + "  ray " + QString::number(fpMs[7], 'f', 2)
        + "  in " + QString::number(fpMs[8], 'f', 2);
    QString winLine = QStringLiteral("win ms: ")
        + "sim " + QString::number(simMs, 'f', 2)
        + "  mesh " + QString::number(meshMs, 'f', 2) + "(" + QString::number(meshN) + "reb)"
        + "  world " + QString::number(worldMs, 'f', 2)
        + "  bp " + QString::number(double(bucket("bp")) / 1e6, 'f', 2)
        + "  [wat " + QString::number(wMs[0], 'f', 1)
        + " lav " + QString::number(wMs[1], 'f', 1)
        + " crop " + QString::number(wMs[2], 'f', 1)
        + " sug " + QString::number(wMs[3], 'f', 1)
        + " farm " + QString::number(wMs[4], 'f', 1)
        + " sap " + QString::number(wMs[5], 'f', 1)
        + " ice " + QString::number(wMs[6], 'f', 1)
        + " leaf " + QString::number(wMs[7], 'f', 1)
        + " wthr " + QString::number(wMs[8], 'f', 1) + "]";

    // t500 perf mob 子分解（逐帧 ms/f，÷ frames）：mob 桶（PlayerController tickImpl 整段）拆成 mobLoop
    //   （EntityManager::tick）/ mobHostile（tickHostileLife）/ mobSpawn（tickSpawners）三函数，mobLoop 再拆
    //   mobAI（aiTick 节流段：火烧/仙人掌/AI 决策移动）vs mobPhys（= mobLoop − mobAI：每帧段 重力/resting/
    //   flow/knockback/音频/walkPhase）。mobAI 手动 nowNs 计时（跨 continue）；mobPhys 派生免再计时。
    //   诊断 mob 桶瓶颈：mob≈27ms 时看这行即可知「27ms 在 tick 还是 hostileLife、AI-pass 还是物理-pass」。
    auto mobSubMs = [this, f](const char *key) { return double(bucket(key)) / 1e6 / f; };
    const double mobLoopMs = mobSubMs("mobLoop");
    const double mobAiMs = mobSubMs("mobAI");
    QString mobLine = QStringLiteral("mob sub ms/f: ")
        + "ai " + QString::number(mobAiMs, 'f', 2)
        + "  phys " + QString::number(mobLoopMs - mobAiMs, 'f', 2)
        + "  hostile " + QString::number(mobSubMs("mobHostile"), 'f', 2)
        + "  spawn " + QString::number(mobSubMs("mobSpawn"), 'f', 2)
        + "  loop " + QString::number(mobLoopMs, 'f', 2);

    m_report = QStringLiteral("prof[1s] %1fr\n  %2\n  %3\n  %4").arg(frames).arg(tickLine, winLine, mobLine);
    m_lastFrames = frames;

    // 重置窗口。
    m_ns.clear();
    m_counts.clear();
    m_frameCount = 0;

    emit reportChanged();
    // 落日志：用户进游戏跑几秒即可在 logs/voxelsandbox.log grep "vo.prof" 看每秒一行分解。
    qCInfo(lcProf).noquote() << "frame breakdown\n" << m_report;
}
