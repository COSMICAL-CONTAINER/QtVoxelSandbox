#include "billboarddropinstancing.h"

#include <cmath> // std::cos / std::fmod / M_PI（t858/t1027 同款解析式动画）

// t1041（R19.23）掉落物 instancing 批 4 收官 ②③（类头注释 = 设计权威）。实现要点：
// - 收纳过滤（单一权威）：aliveAt(i) && itemId == familyId && 谓词（itemIconFamily 缺省 false =
//     ItemEntityManager::isBlockIconBillboardDrop 异形方块图标族 / true = isIconBillboardDrop
//     工具/材料图标族）。谓词只依赖 familyId（桶粒度常量）→ 循环外单次判定；与 hasLiveMember 同参。
// - 实例表条目（旧 billboard delegate 分支逐字对齐）：position = 槽 pos + (0, bob, 0)；scale 0.3 均匀
//     （旧 billboard Model scale 0.3）；rotation = (camPitch, camYaw, 0)——旧 delegate 本地
//     eulerRotation (camPitch, camYaw-rotY, 0) 作为 entRoot（rotY）子节点的世界合成
//     = Ry(camYaw)·Rx(camPitch)（roll=0 时 fromEulerAngles=Ry·Rx，Main.qml 材料段注释同款推导）；
//     color 白（天光乘子 terrainLight(skyLight) 留在共享材质 baseColor，批 1 同口径）。
// - 动画解析式（billboard 不自转，只剩 bob）：
//     bob = 0.075 * (1 - cos(pi * s2)), s2 = fmod(ph, 2.0)   （旧 bobY 0↔0.15 两段 InOutSine 1s/leg）
//     ph = t + slot * 0.37（批 1/2 的「旧相位 = 创建时刻」等价视觉错开口径）。
// - 每帧重算全表（桶内活体 ≤ 200 × 80B）：getInstanceBuffer 内复用 m_buffer / m_liveSlots。
// - t1032 空转门：16ms QTimer 构造不启钟，refreshTicker 按「桶内有本族活体」启/停；空转期 tick 不存在
//   → 零周期 markDirty / 零空表重取。camPitch/camYaw 沿只 markDirty（朝向重取，无时钟语义）。

BillboardDropInstancing::BillboardDropInstancing(QQuick3DObject *parent)
    : QQuick3DInstancing(parent)
{
    // 动画驱动（t858 官方 dynamic instancing 示例模式）：16ms Precise QTimer → markDirty → 渲染 sync
    //   重取表。空转门（t1032 同款）：构造**不启钟**；活跃沿 refreshTicker start + markDirty 兜底。
    m_ticker.setTimerType(Qt::PreciseTimer);
    m_ticker.setInterval(16);
    connect(&m_ticker, &QTimer::timeout, this, &BillboardDropInstancing::tick);
    m_clock.start();
}

void BillboardDropInstancing::setManager(ItemEntityManager *m)
{
    if (m_manager == m) return;
    if (m_manager) // 空转门活体集变更沿直连；换 manager 先断旧防双投递（blockdropinstancing 同款）
        disconnect(m_manager, &ItemEntityManager::entitiesChanged,
                   this, &BillboardDropInstancing::onManagerEntitiesChanged);
    m_manager = m;
    if (m_manager)
        connect(m_manager, &ItemEntityManager::entitiesChanged,
                this, &BillboardDropInstancing::onManagerEntitiesChanged);
    emit managerChanged();
    refreshTicker(); // 空转门重估（活跃沿 start）
    markDirty(); // 数据源切换 → 立即重取一次表
}

void BillboardDropInstancing::setFamilyId(int id)
{
    if (m_familyId == id) return;
    m_familyId = id;
    emit familyIdChanged();
    refreshTicker(); // 桶指派变化 = 活跃/空转沿（t1032：含 0→id 启钟、id→0 停钟）
    markDirty(); // 桶指派变化 → 立即重取一次表
}

void BillboardDropInstancing::setItemIconFamily(bool itemIconFamily)
{
    if (m_itemIconFamily == itemIconFamily) return;
    m_itemIconFamily = itemIconFamily;
    emit itemIconFamilyChanged();
    refreshTicker(); // 收纳谓词切换重估空转门（同 familyId 沿语义）
    markDirty();     // 过滤面变化 → 立即重取一次表
}

void BillboardDropInstancing::setCamPitch(qreal v)
{
    if (m_camPitch == v) return;
    m_camPitch = v;
    emit camPitchChanged();
    markDirty(); // 朝向面变化 → 立即重取一次表（无时钟语义，空转门不受影响）
}

void BillboardDropInstancing::setCamYaw(qreal v)
{
    if (m_camYaw == v) return;
    m_camYaw = v;
    emit camYawChanged();
    markDirty(); // 同 camPitch
}

void BillboardDropInstancing::onManagerEntitiesChanged()
{
    refreshTicker(); // 活体集变更沿：桶内 0→n 启钟 / n→0 停钟（完备沿，见头注释）
    markDirty();     // 兜底防丢帧（首个活体入桶当帧即可见，不等下一 tick）
}

void BillboardDropInstancing::refreshTicker()
{
    if (hasLiveMember()) {
        if (!m_ticker.isActive())
            m_ticker.start(); // 活跃沿启钟（16ms bob 相位推进恢复）
    } else if (m_ticker.isActive()) {
        m_ticker.stop(); // 空转沿停钟（familyId<=0 / 非本族 / 桶内活体 0 不走钟，t1032 同款）
    }
}

bool BillboardDropInstancing::hasLiveMember() const
{
    // 谓词只依赖 familyId（桶粒度常量），提到循环外；与 getInstanceBuffer 收纳过滤同源同参。
    if (!m_manager || m_familyId <= 0) return false;
    const bool famOK = m_itemIconFamily ? ItemEntityManager::isIconBillboardDrop(m_familyId)
                                        : ItemEntityManager::isBlockIconBillboardDrop(m_familyId);
    if (!famOK) return false;
    const int n = m_manager->count();
    for (int i = 0; i < n; ++i) {
        if (m_manager->aliveAt(i) && m_manager->itemIdAt(i) == m_familyId) return true;
    }
    return false;
}

void BillboardDropInstancing::tick()
{
    markDirty(); // bob 相位前进 → 实例表内容变（下帧 sync 经 getInstanceBuffer 重算）
}

QByteArray BillboardDropInstancing::getInstanceBuffer(int *instanceCount)
{
    if (instanceCount) *instanceCount = 0;
    if (!m_manager || m_familyId <= 0) return {};

    // t1041 收纳过滤（单一权威谓词，itemIconFamily 切换两池）：非本族 id 恒不进表——它们由 QML
    //   delegate 旧分支渲染或其它池收纳（跨池互斥，两侧谓词同源纪律与批 1/2 相同）。
    if (!(m_itemIconFamily ? ItemEntityManager::isIconBillboardDrop(m_familyId)
                           : ItemEntityManager::isBlockIconBillboardDrop(m_familyId)))
        return {};

    const double t = double(m_clock.elapsed()) / 1000.0; // 秒（相位时钟恒走，同 t858/t1027）

    m_liveSlots.clear();
    const int n = m_manager->count();
    for (int i = 0; i < n; ++i) {
        if (!m_manager->aliveAt(i)) continue;
        if (m_manager->itemIdAt(i) != m_familyId) continue; // 非本桶 id 一律不进表
        m_liveSlots.append(i);
    }

    const int cnt = int(m_liveSlots.size());
    m_buffer.resize(cnt * int(sizeof(QQuick3DInstancing::InstanceTableEntry)));
    auto *entries = reinterpret_cast<QQuick3DInstancing::InstanceTableEntry *>(m_buffer.data());
    for (int k = 0; k < cnt; ++k) {
        const int slot = m_liveSlots[k];
        const QVector3D pos = m_manager->posAt(slot);
        // bob 相位（秒）：slot 错峰 + 全局时钟（公式推导见文件头注释）。
        const double ph = t + slot * 0.37;
        const double s2 = std::fmod(ph, 2.0);
        const float bob = float(0.075 * (1.0 - std::cos(M_PI * s2)));
        entries[k] = calculateTableEntry(QVector3D(pos.x(), pos.y() + bob, pos.z()),
                                         QVector3D(0.3f, 0.3f, 0.3f),
                                         QVector3D(float(m_camPitch), float(m_camYaw), 0.0f),
                                         QColor(255, 255, 255, 255));
    }
    if (instanceCount) *instanceCount = cnt;
    return m_buffer;
}

int BillboardDropInstancing::probeInstanceCount()
{
    int cnt = 0;
    getInstanceBuffer(&cnt);
    return cnt;
}

bool BillboardDropInstancing::probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale,
                                              QQuaternion *outRot)
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
