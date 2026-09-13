#include "tooldropinstancing.h"

#include "toolregistry.h"

#include <cmath>   // std::cos / std::fmod / M_PI（t858/t1027 同款解析式动画）
#include <QtGlobal> // qRound

// t1041（R19.23）掉落物 instancing 批 4 收官 ①（类头注释 = 设计权威）。实现要点：
// - 收纳过滤（单一权威）：aliveAt(i) && itemId == familyId && ItemEntityManager::isTool3DDrop(familyId)
//   （谓词只依赖 familyId——桶内全实例同 id，循环外单次判定；与 hasLiveMember 同参，review0910 #3 纪律）。
// - 实例表条目（旧工具 delegate 分支逐字对齐）：position = 槽 pos + (0, bob, 0)；scale 0.45 均匀（旧工具
//     Model scale 0.45）；eulerRotation (0, rotY, 0)（旧工具继承 entRoot 绕 Y 自转，无 billboard 抵消）；
//     color = tier 色 × k（材质本体白基色——实例色乘法 = 旧「材质 baseColor = tier×tintBySkyLight」等效）。
// - 动画解析式（批 1/2/3 公式源）：
//     rotY = fmod(ph / 3.0, 1.0) * 360          （旧 entRoot `NumberAnimation on rotY` 3s/圈）
//     bob  = 0.075 * (1 - cos(pi * s2)), s2 = fmod(ph, 2.0)   （旧 bobY 0↔0.15 两段 InOutSine 1s/leg）
//     ph = t + slot * 0.37（批 1/2 的「旧相位 = 创建时刻」等价视觉错开口径）。
// - tier 色映射（Main.qml 工具 delegate 五分支逐字对齐，t472/t557/t589 同源）：
//     5=金 (242,200,50) / 6=铜 (200,120,80) / 4=钻 (79,217,210) / 3=铁 (216,216,230) / 2=石 (154,154,154)
//     / 其余=木 (138,90,46)；k = minLight + (1-minLight)*skyLight（tintBySkyLight floor 公式；qRound 保
//     量化最近邻，glowshellinstancing.cpp 同款）。stringPass → 蜘蛛丝白 (245,245,245)×k（t330 弦不随 tier）。
// - 颜色存储口径：calculateTableEntry 落表前经 Quick3D 内部 sRGBToLinear（alpha 透传）——实例表存线性色
//   （渲染管线权威输入）；探针侧按同一多项式镜像断言（P-t1039a 口径沿用）。
// - 每帧重算全表（桶内活体 ≤ 200 × 80B）：getInstanceBuffer 内复用 m_buffer / m_liveSlots。
// - t1032 空转门：16ms QTimer 构造不启钟，refreshTicker 按「桶内有本族活体」启/停（沿源见类头注释）；
//   空转期 tick 不存在 → 零周期 markDirty / 零空表重取。

ToolDropInstancing::ToolDropInstancing(QQuick3DObject *parent)
    : QQuick3DInstancing(parent)
{
    // 动画驱动（t858 官方 dynamic instancing 示例模式）：16ms Precise QTimer → markDirty → 渲染 sync 重取表。
    //   空转门（t1032 同款）：构造**不启钟**——未指派桶 / 非本族 / 空桶场景零周期唤醒；活跃沿 refreshTicker
    //   start + markDirty 兜底，行为面（活跃期逐帧动画）与旧恒走钟一致。
    m_ticker.setTimerType(Qt::PreciseTimer);
    m_ticker.setInterval(16);
    connect(&m_ticker, &QTimer::timeout, this, &ToolDropInstancing::tick);
    m_clock.start();
}

void ToolDropInstancing::setManager(ItemEntityManager *m)
{
    if (m_manager == m) return;
    if (m_manager) // 空转门活体集变更沿直连；换 manager 先断旧防双投递（blockdropinstancing 同款）
        disconnect(m_manager, &ItemEntityManager::entitiesChanged,
                   this, &ToolDropInstancing::onManagerEntitiesChanged);
    m_manager = m;
    if (m_manager)
        connect(m_manager, &ItemEntityManager::entitiesChanged,
                this, &ToolDropInstancing::onManagerEntitiesChanged);
    emit managerChanged();
    refreshTicker(); // 空转门重估（活跃沿 start）
    markDirty(); // 数据源切换 → 立即重取一次表
}

void ToolDropInstancing::setFamilyId(int id)
{
    if (m_familyId == id) return;
    m_familyId = id;
    emit familyIdChanged();
    refreshTicker(); // 桶指派变化 = 活跃/空转沿（t1032：含 0→id 启钟、id→0 停钟）
    markDirty(); // 桶指派变化 → 立即重取一次表
}

void ToolDropInstancing::setSkyLight(qreal v)
{
    if (m_skyLight == v) return;
    m_skyLight = v;
    emit skyLightChanged();
    markDirty(); // 色调面变化 → 立即重取一次表（无时钟语义，空转门不受影响）
}

void ToolDropInstancing::setMinLight(qreal v)
{
    if (m_minLight == v) return;
    m_minLight = v;
    emit minLightChanged();
    markDirty(); // 同 skyLight（tintBySkyLight floor 项）
}

void ToolDropInstancing::setStringPass(bool stringPass)
{
    if (m_stringPass == stringPass) return;
    m_stringPass = stringPass;
    emit stringPassChanged();
    refreshTicker(); // 通道切换重估空转门（同 familyId 沿语义——stringPass 表只服务弓桶）
    markDirty();     // 色面变化 → 立即重取一次表
}

void ToolDropInstancing::onManagerEntitiesChanged()
{
    refreshTicker(); // 活体集变更沿：桶内 0→n 启钟 / n→0 停钟（完备沿，见头注释）
    markDirty();     // 兜底防丢帧（首个活体入桶当帧即可见，不等下一 tick）
}

void ToolDropInstancing::refreshTicker()
{
    if (hasLiveMember()) {
        if (!m_ticker.isActive())
            m_ticker.start(); // 活跃沿启钟（16ms 动画相位推进恢复）
    } else if (m_ticker.isActive()) {
        m_ticker.stop(); // 空转沿停钟（familyId<=0 / 非本族 / 桶内活体 0 不走钟，t1032 同款）
    }
}

bool ToolDropInstancing::hasLiveMember() const
{
    // 谓词只依赖 familyId（桶粒度常量），提到循环外；与 getInstanceBuffer 收纳过滤同源同参。
    if (!m_manager || m_familyId <= 0) return false;
    if (!ItemEntityManager::isTool3DDrop(m_familyId)) return false;
    // t1047 O-2 弓门（review0912 #2）：stringPass 通道只服务弓桶——非弓 id（剪刀/竿/五几何工具桶的
    //   弦表 feeder）即便桶内有活体也恒空转（旧口径 7 废钟 ~420 唤醒/秒清偿）。ToolRegistry::tool
    //   同 getInstanceBuffer 色面读口（toolregistry.h 已 include，BlockRegistry::Bow 随之可见）。
    if (m_stringPass) {
        const auto *td = ToolRegistry::tool(m_familyId);
        if (!td || td->type != BlockRegistry::Bow) return false;
    }
    const int n = m_manager->count();
    for (int i = 0; i < n; ++i) {
        if (m_manager->aliveAt(i) && m_manager->itemIdAt(i) == m_familyId) return true;
    }
    return false;
}

void ToolDropInstancing::tick()
{
    markDirty(); // 动画相位前进 → 实例表内容变（下帧 sync 经 getInstanceBuffer 重算）
}

QByteArray ToolDropInstancing::getInstanceBuffer(int *instanceCount)
{
    if (instanceCount) *instanceCount = 0;
    if (!m_manager || m_familyId <= 0) return {};

    // t1041 收纳过滤（单一权威谓词）：familyId 非工具 3D 族（剪刀/打火石/钓鱼竿/材料段/方块段）恒空表
    //   ——它们由 QML delegate 旧分支渲染或其它池收纳。桶粒度 = itemId → 循环外单次判定。
    if (!ItemEntityManager::isTool3DDrop(m_familyId)) return {};

    const double t = double(m_clock.elapsed()) / 1000.0; // 秒（相位时钟恒走，同 t858/t1027）
    // 天光乘子：k = minLight + (1-minLight)*skyLight（Main.qml tintBySkyLight floor 公式逐字对齐；
    //   minLight=0.4 / skyLight=1 → k=1 全亮）。t144「工具 tier 色随夜同步变暗」契约。
    const double k = m_minLight + (1.0 - m_minLight) * m_skyLight;

    // 实例色（桶内全实例同 id 同 tier → 常量，循环外单次计算）：
    //   stringPass → 蜘蛛丝白 245（t330 弦）；否则 tier 色映射（旧 delegate 五分支逐字）。
    int cr = 138, cg = 90, cb = 46; // 兜底木（tier<=1）
    if (m_stringPass) {
        cr = cg = cb = 245;
    } else {
        const ToolRegistry::ToolDef *td = ToolRegistry::tool(m_familyId);
        const int tier = td ? td->tier : 0; // 非 → 0 兜底木（hotbarVM.toolTier 同款兜底）
        if (tier == 5)       { cr = 242; cg = 200; cb = 50;  } // t557 金工具金黄
        else if (tier == 6)  { cr = 200; cg = 120; cb = 80;  } // t557 铜工具铜橙
        else if (tier == 4)  { cr = 79;  cg = 217; cb = 210; } // t589 钻石青绿
        else if (tier == 3)  { cr = 216; cg = 216; cb = 230; } // 铁银白
        else if (tier == 2)  { cr = 154; cg = 154; cb = 154; } // 石灰
    }

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
    for (int k2 = 0; k2 < cnt; ++k2) {
        const int slot = m_liveSlots[k2];
        const QVector3D pos = m_manager->posAt(slot);
        // 动画相位（秒）：slot 错峰 + 全局时钟（公式推导见文件头注释）。
        const double ph = t + slot * 0.37;
        const double s1 = std::fmod(ph / 3.0, 1.0);
        const double s2 = std::fmod(ph, 2.0);
        const float rotY = float(s1 * 360.0);
        const float bob = float(0.075 * (1.0 - std::cos(M_PI * s2)));
        const QColor col(qRound(double(cr) * k), qRound(double(cg) * k),
                         qRound(double(cb) * k), 255);
        entries[k2] = calculateTableEntry(QVector3D(pos.x(), pos.y() + bob, pos.z()),
                                          QVector3D(0.45f, 0.45f, 0.45f),
                                          QVector3D(0, rotY, 0),
                                          col);
    }
    if (instanceCount) *instanceCount = cnt;
    return m_buffer;
}

int ToolDropInstancing::probeInstanceCount()
{
    int cnt = 0;
    getInstanceBuffer(&cnt);
    return cnt;
}

bool ToolDropInstancing::probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale,
                                         QQuaternion *outRot, QColor *outColor)
{
    int cnt = 0;
    const QByteArray buf = getInstanceBuffer(&cnt);
    if (k < 0 || k >= cnt || buf.size() < (k + 1) * int(sizeof(QQuick3DInstancing::InstanceTableEntry)))
        return false;
    const auto *e = reinterpret_cast<const QQuick3DInstancing::InstanceTableEntry *>(buf.constData()) + k;
    if (outPos) *outPos = e->getPosition();
    if (outScale) *outScale = e->getScale();
    if (outRot) *outRot = e->getRotation();
    if (outColor) *outColor = e->getColor();
    return true;
}
