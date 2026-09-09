#include "blockdropinstancing.h"

#include "itementitymanager.h"

#include <cmath> // std::cos / std::fmod / M_PI（t858 同款解析式动画）

// t1027（R19.21）掉落物 instancing 治理首批（类头注释 = 设计权威）。实现要点：
// - 动画解析式与旧 QML delegate 逐字对齐（Main.qml itemHost entity delegate）：
//     rotY = fmod(ph / 3.0, 1.0) * 360          （旧 `NumberAnimation on rotY { to:360; duration:3000 }`）
//     bob  = 0.075 * (1 - cos(pi * s)),  s = fmod(ph, 2.0)   （旧 0↔0.15 两段 InOutSine 1s/leg；
//          InOutSine out-and-back = 0.5*(1-cos(pi*s))，推导同 t858 xporbinstancing.cpp）
//   per-slot 错峰相位 ph = t + slot * 0.37s（旧 delegate 相位 = 各自创建时刻的等价视觉错开）。
// - 实例表条目：position = 槽 pos + (0, bob, 0)（bob 只加 Y，XZ 精确 = 实体世界位）；scale 0.3 均匀
//   （旧图标 Model scale 0.3）；eulerRotation (0, rotY, 0)（旧 delegate 根 Node 绕 Y 自转）；color 白
//   （天光乘子 terrainLight(skyLight) 留在共享材质 baseColor——族内无 per-instance 色差，白 = 不调制）。
// - 收纳过滤（单一权威）：aliveAt(i) && ItemEntityManager::isPlainCubeDrop(itemIdAt(i)) && itemId ==
//   familyId。QML delegate 排除侧（Main.qml plain-cube Model visible 追加 hasBucket）与本侧逐位同判。
// - 每帧重算全表（桶内活体 ≤ 200 × 80B）：getInstanceBuffer 内复用 m_buffer / m_liveSlots。

BlockDropInstancing::BlockDropInstancing(QQuick3DObject *parent)
    : QQuick3DInstancing(parent)
{
    // 动画驱动（t858 官方 dynamic instancing 示例模式）：16ms Precise QTimer → markDirty → 渲染 sync 重取表。
    m_ticker.setTimerType(Qt::PreciseTimer);
    m_ticker.setInterval(16);
    connect(&m_ticker, &QTimer::timeout, this, &BlockDropInstancing::tick);
    m_ticker.start();
    m_clock.start();
}

void BlockDropInstancing::setManager(ItemEntityManager *m)
{
    if (m_manager == m) return;
    m_manager = m;
    emit managerChanged();
    markDirty(); // 数据源切换 → 立即重取一次表
}

void BlockDropInstancing::setFamilyId(int id)
{
    if (m_familyId == id) return;
    m_familyId = id;
    emit familyIdChanged();
    markDirty(); // 桶指派变化 → 立即重取一次表
}

void BlockDropInstancing::tick()
{
    markDirty(); // 动画相位前进 → 实例表内容变（下帧 sync 经 getInstanceBuffer 重算）
}

QByteArray BlockDropInstancing::getInstanceBuffer(int *instanceCount)
{
    if (instanceCount) *instanceCount = 0;
    if (!m_manager || m_familyId <= 0) return {};

    const double t = double(m_clock.elapsed()) / 1000.0; // 秒（相位时钟恒走，同 t858）

    m_liveSlots.clear();
    const int n = m_manager->count();
    for (int i = 0; i < n; ++i) {
        if (!m_manager->aliveAt(i)) continue;
        const int itemId = m_manager->itemIdAt(i);
        // t1027 收纳过滤（单一权威谓词 + 桶 id 精确匹配）：非整立方族 / 非本桶 id 一律不进表——
        // 它们仍由 QML delegate 旧分支渲染（3D 族 / billboard 族 / 工具 / 材料）或其它桶收纳。
        if (itemId != m_familyId) continue;
        if (!ItemEntityManager::isPlainCubeDrop(itemId)) continue;
        m_liveSlots.append(i);
    }

    const int cnt = int(m_liveSlots.size());
    m_buffer.resize(cnt * int(sizeof(QQuick3DInstancing::InstanceTableEntry)));
    auto *entries = reinterpret_cast<QQuick3DInstancing::InstanceTableEntry *>(m_buffer.data());
    for (int k = 0; k < cnt; ++k) {
        const int slot = m_liveSlots[k];
        const QVector3D pos = m_manager->posAt(slot);
        // 动画相位（秒）：slot 错峰 + 全局时钟（公式推导见文件头注释）。
        const double ph = t + slot * 0.37;
        const double s1 = std::fmod(ph / 3.0, 1.0);
        const double s2 = std::fmod(ph, 2.0);
        const float rotY = float(s1 * 360.0);
        const float bob = float(0.075 * (1.0 - std::cos(M_PI * s2)));
        entries[k] = calculateTableEntry(QVector3D(pos.x(), pos.y() + bob, pos.z()),
                                         QVector3D(0.3f, 0.3f, 0.3f),
                                         QVector3D(0, rotY, 0),
                                         QColor(255, 255, 255, 255));
    }
    if (instanceCount) *instanceCount = cnt;
    return m_buffer;
}

int BlockDropInstancing::probeInstanceCount()
{
    int cnt = 0;
    getInstanceBuffer(&cnt);
    return cnt;
}

bool BlockDropInstancing::probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale, QQuaternion *outRot)
{
    int cnt = 0;
    const QByteArray buf = getInstanceBuffer(&cnt);
    if (k < 0 || k >= cnt || buf.size() < (k + 1) * int(sizeof(QQuick3DInstancing::InstanceTableEntry)))
        return false;
    const auto *e = reinterpret_cast<const QQuick3DInstancing::InstanceTableEntry *>(buf.constData()) + k;
    if (outPos) *outPos = e->getPosition();
    if (outScale) *outScale = e->getScale();
    if (outRot) *outRot = e->getRotation();
    return true;
}
