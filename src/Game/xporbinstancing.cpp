#include "xporbinstancing.h"

#include "xporbmanager.h"

// t858（R19.14）经验球 instancing 试点（类头注释 = 设计权威）。实现要点：
// - 动画解析式与旧 QML delegate 逐字对齐（Main.qml t402 段）：
//     bob   = 0.06 * (1 - cos(pi * s1)),  s1 = fmod(t / 0.7, 2)   （0↔0.12 三角波，周期 1.4s）
//     pulse = 1.0  - 0.15 * cos(pi * s2),  s2 = fmod(t / 0.5, 2)   （0.85↔1.15，周期 1.0s）
//   推导：InOutSine ease(p) = (1-cos(pi*p))/2；out-and-back 两段拼起来恰为 0.5*(1-cos(pi*s))（s∈[0,2]）。
// - 颜色：amount >= 5 亮黄绿 #b8e635，小额深绿 #7fd13b（同旧 QML baseColor 二值；instance color 承载，
//   材质 baseColor 白）。光照不参与（旧材质 NoLighting + 纯色，无天光调制）——不复制 terrainLight。
// - 每帧重算全表（活体槽 ≤ 64 × 80B）：getInstanceBuffer 内复用 m_buffer / m_liveSlots，零稳态分配。

XpOrbInstancing::XpOrbInstancing(QQuick3DObject *parent)
    : QQuick3DInstancing(parent)
{
    // 动画驱动（官方 dynamic instancing 示例模式）：16ms Precise QTimer → markDirty → 渲染 sync 重取表。
    // 球不存在时空表照常推进（相位连续，新球出生即融入当前相位；避免「无球时冻结、出生跳变」）。
    m_ticker.setTimerType(Qt::PreciseTimer);
    m_ticker.setInterval(16);
    connect(&m_ticker, &QTimer::timeout, this, &XpOrbInstancing::tick);
    m_ticker.start();
    m_clock.start();
}

void XpOrbInstancing::setManager(XpOrbManager *m)
{
    if (m_manager == m) return;
    m_manager = m;
    emit managerChanged();
    markDirty(); // 数据源切换 → 立即重取一次表
}

void XpOrbInstancing::tick()
{
    markDirty(); // 动画相位前进 → 实例表内容变（下帧 sync 经 getInstanceBuffer 重算）
}

QByteArray XpOrbInstancing::getInstanceBuffer(int *instanceCount)
{
    if (instanceCount) *instanceCount = 0;
    if (!m_manager) return {};

    // 动画时钟：构造起点的 QElapsedTimer（相位连续）；per-slot 错峰相位 = slot * 0.37s
    //（旧 delegate 相位 = 各自创建时刻的等价视觉错开，见类头注释）。
    const double t = double(m_clock.elapsed()) / 1000.0; // 秒

    m_liveSlots.clear();
    const int n = m_manager->count();
    for (int i = 0; i < n; ++i)
        if (m_manager->aliveAt(i)) m_liveSlots.append(i);

    const int cnt = int(m_liveSlots.size());
    m_buffer.resize(cnt * int(sizeof(QQuick3DInstancing::InstanceTableEntry)));
    auto *entries = reinterpret_cast<QQuick3DInstancing::InstanceTableEntry *>(m_buffer.data());
    for (int k = 0; k < cnt; ++k) {
        const int slot = m_liveSlots[k];
        const QVector3D pos = m_manager->posAt(slot);
        const int amount = m_manager->amountAt(slot);
        // 动画相位（秒）：slot 错峰 + 全局时钟。
        const double ph = t + slot * 0.37;
        const double s1 = std::fmod(ph / 0.7, 2.0);
        const double s2 = std::fmod(ph / 0.5, 2.0);
        const float bob = float(0.06 * (1.0 - std::cos(M_PI * s1)));
        const float pulse = float(1.0 - 0.15 * std::cos(M_PI * s2));
        const float scale = 0.18f * pulse;
        // 颜色二值（同旧 QML：大额球亮黄绿更显眼、小额偏深绿）。
        const QColor col = amount >= 5 ? QColor(0xb8, 0xe6, 0x35) : QColor(0x7f, 0xd1, 0x3b);
        entries[k] = calculateTableEntry(QVector3D(pos.x(), pos.y() + bob, pos.z()),
                                         QVector3D(scale, scale, scale),
                                         QVector3D(0, 0, 0), // 无旋转（旧球 Model 无 eulerRotation）
                                         col);
    }
    if (instanceCount) *instanceCount = cnt;
    return m_buffer;
}

bool XpOrbInstancing::probeFirstInstancePosition(QVector3D *outPos)
{
    int cnt = 0;
    const QByteArray buf = getInstanceBuffer(&cnt);
    if (cnt <= 0 || buf.size() < int(sizeof(QQuick3DInstancing::InstanceTableEntry)))
        return false;
    const auto *e = reinterpret_cast<const QQuick3DInstancing::InstanceTableEntry *>(buf.constData());
    if (outPos) *outPos = e->getPosition();
    return true;
}
