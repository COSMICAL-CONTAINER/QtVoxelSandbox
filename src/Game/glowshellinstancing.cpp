#include "glowshellinstancing.h"

#include <cmath> // std::cos / std::fmod / M_PI（t858/t1027 同款解析式动画）
#include <QtGlobal> // qRound

// t1039（R19.23）掉落物 instancing 批 3（类头注释 = 设计权威）。实现要点：
// - 收纳（与 QML delegate 排除侧 hasShellAt 薄委托互为镜像，同 id 壳不双渲）：槽序扫活体，
//     前 kShellCap=128 个入池（manager 上限 200，溢出尾槽壳走 delegate 保底）。
// - 实例表条目（旧 delegate entShell 逐字对齐）：position = 槽 pos + (0, bob, 0)；scale 0.45
//     均匀（旧壳 Model scale 0.45）；eulerRotation (0, rotY, 0)（旧壳继承 entRoot 绕 Y 自转；
//     UnitCube 无贴图纯色旋转视觉不可辨，仍逐字承载保形）；color rgb = 附魔紫/灰 × k，
//     alpha = 0.35 静态 / 附魔呼吸 0.28↔0.45。
// - 动画解析式（批 1/2 公式源 + t696 呼吸）：
//     rotY  = fmod(ph / 3.0, 1.0) * 360            （旧 entRoot `NumberAnimation on rotY` 3s/圈）
//     bob   = 0.075 * (1 - cos(pi * s2)), s2 = fmod(ph, 2.0)   （旧 bobY 0↔0.15 两段 InOutSine 1s/leg）
//     alpha = 0.28 + 0.17 * 0.5 * (1 - cos(pi * s3)), s3 = fmod(ph / 0.8, 2.0)
//         （旧 t696 `NumberAnimation { from:0.28; to:0.45; duration:800; InOutSine }` + 反向腿；
//          InOutSine out-and-back 三角波推导同 t858；1.6s 全循环）
//     ph = t + slot * 0.37（批 1/2 的「旧相位 = 创建时刻」等价视觉错开口径；壳-体相位脱锁口径
//     已在类头登记为接受——独立半透 halo，非嵌入件，与 t1038 书-台不同级）。
// - 每帧重算全表（≤128 × 80B ≈ 10KB 上界）：getInstanceBuffer 内复用 m_buffer / m_liveSlots。
// - hasTransparency(true)：Qt 官方属性——表 alpha 参与渲染（材质本体不透明白；文档语义「opaque
//     模型 + hasTransparency ⇒ 表 alpha 生效」），等效旧材质 opacity < 1 的透明通道行为。
// - 颜色存储口径：calculateTableEntry 落表前经 Quick3D 内部 QSSGUtils::color::sRGBToLinear
//     （rgb*(rgb*(rgb*C1+C2)+C3)，alpha 透传）——实例表存**线性**色（渲染管线权威输入），渲染
//     输出端转回 sRGB ⇒ 视觉与旧 delegate 材质 baseColor 同色（材质路径同经 sRGB→linear）。探针
//     侧（P-t1039a）回读 getColor() 得线性值，按同一多项式镜像断言。

GlowShellInstancing::GlowShellInstancing(QQuick3DObject *parent)
    : QQuick3DInstancing(parent)
{
    // 实例表 alpha 参与渲染（呼吸 / 0.35 半透的唯一生效通道——见文件头注释）。
    setHasTransparency(true);
    // 动画驱动（t858 官方 dynamic instancing 示例模式）：16ms Precise QTimer → markDirty → 渲染 sync
    //   重取表。空转门（t1032 同款）：构造**不启钟**——无 manager / 无活体场景零周期唤醒；
    //   活跃沿 refreshTicker start + markDirty 兜底，活跃期行为面与旧恒走钟一致。
    m_ticker.setTimerType(Qt::PreciseTimer);
    m_ticker.setInterval(16);
    connect(&m_ticker, &QTimer::timeout, this, &GlowShellInstancing::tick);
    m_clock.start();
}

void GlowShellInstancing::setManager(ItemEntityManager *m)
{
    if (m_manager == m) return;
    if (m_manager) // 空转门活体集变更沿直连；换 manager 先断旧防双投递（blockdropinstancing 同款）
        disconnect(m_manager, &ItemEntityManager::entitiesChanged,
                   this, &GlowShellInstancing::onManagerEntitiesChanged);
    m_manager = m;
    if (m_manager)
        connect(m_manager, &ItemEntityManager::entitiesChanged,
                this, &GlowShellInstancing::onManagerEntitiesChanged);
    emit managerChanged();
    refreshTicker(); // 空转门重估（活跃沿 start）
    markDirty();     // 数据源切换 → 立即重取一次表
}

void GlowShellInstancing::setSkyLight(qreal v)
{
    if (m_skyLight == v) return;
    m_skyLight = v;
    emit skyLightChanged();
    markDirty(); // 色调面变化 → 立即重取一次表（无时钟语义，空转门不受影响）
}

void GlowShellInstancing::setMinLight(qreal v)
{
    if (m_minLight == v) return;
    m_minLight = v;
    emit minLightChanged();
    markDirty(); // 同 skyLight（tintBySkyLight floor 项）
}

void GlowShellInstancing::onManagerEntitiesChanged()
{
    refreshTicker(); // 活体集变更沿：0→n 启钟 / n→0 停钟（完备沿，见类头注释）
    markDirty();     // 兜底防丢帧（首个活体当帧可见，不等下一 tick）
}

void GlowShellInstancing::refreshTicker()
{
    if (hasLiveMember()) {
        if (!m_ticker.isActive())
            m_ticker.start(); // 活跃沿启钟（16ms 动画相位推进恢复）
    } else if (m_ticker.isActive()) {
        m_ticker.stop(); // 空转沿停钟（无 manager / 活体 0 不走钟，t1032 同款）
    }
}

bool GlowShellInstancing::hasLiveMember() const
{
    // 壳族全族横切：活跃判定 = 任一活体槽（≥1 活体 ⇒ ≥1 壳入池 ⇒ bob/rotY/呼吸须走钟）。
    if (!m_manager) return false;
    const int n = m_manager->count();
    for (int i = 0; i < n; ++i) {
        if (m_manager->aliveAt(i)) return true;
    }
    return false;
}

bool GlowShellInstancing::hasShellAt(int slot) const
{
    // 收纳谓词权威（QML delegate 排除侧 entShell visible 链薄委托本函数——同源最强形式）。
    // 语义与 getInstanceBuffer 收纳互为镜像：前 kShellCap 个活体槽入池。
    if (!m_manager) return false;
    const int n = m_manager->count();
    if (slot < 0 || slot >= n) return false;
    if (!m_manager->aliveAt(slot)) return false;
    int ahead = 0; // 槽序上先于 slot 的活体数
    for (int i = 0; i < slot; ++i) {
        if (m_manager->aliveAt(i)) ++ahead;
    }
    return ahead < kShellCap;
}

void GlowShellInstancing::tick()
{
    markDirty(); // 动画相位前进 → 实例表内容变（下帧 sync 经 getInstanceBuffer 重算）
}

QByteArray GlowShellInstancing::getInstanceBuffer(int *instanceCount)
{
    if (instanceCount) *instanceCount = 0;
    if (!m_manager) return {};

    const double t = double(m_clock.elapsed()) / 1000.0; // 秒（相位时钟恒走，同 t858/t1027）
    // 天光乘子：k = minLight + (1-minLight)*skyLight（Main.qml tintBySkyLight / terrainLight
    //   floor 公式逐字对齐；minLight=0.4 / skyLight=1 → k=1 全亮）。t144「外壳随夜同步变暗」契约。
    const double k = m_minLight + (1.0 - m_minLight) * m_skyLight;

    m_liveSlots.clear();
    const int n = m_manager->count();
    for (int i = 0; i < n && m_liveSlots.size() < kShellCap; ++i) {
        // 壳族横切收纳：活体即收（不看 itemId——本体族路由在批 1/2 桶 / delegate，与本池正交）。
        //   空 / 已释放槽跳过（不进表）。容量满即止（尾槽 = hasShellAt false = delegate 壳保底）。
        if (!m_manager->aliveAt(i)) continue;
        m_liveSlots.append(i);
    }

    const int cnt = int(m_liveSlots.size());
    m_buffer.resize(cnt * int(sizeof(QQuick3DInstancing::InstanceTableEntry)));
    auto *entries = reinterpret_cast<QQuick3DInstancing::InstanceTableEntry *>(m_buffer.data());
    for (int j = 0; j < cnt; ++j) {
        const int slot = m_liveSlots[j];
        const QVector3D pos = m_manager->posAt(slot);
        // 附魔判定：与 QML entHasEnch 同一数据源（enchantsAt）+ 同一守卫语义（任一 pack 值非 0；
        //   QML `(entEnch[k] || 0) !== 0` 逐位对应 value(k).toInt() != 0）。
        const QVariantList ench = m_manager->enchantsAt(slot);
        const bool hasEnch = ench.value(0).toInt() != 0 || ench.value(1).toInt() != 0
                          || ench.value(2).toInt() != 0 || ench.value(3).toInt() != 0;
        // 动画相位（秒）：slot 错峰 + 全局时钟（公式推导见文件头注释）。
        const double ph = t + slot * 0.37;
        const double s1 = std::fmod(ph / 3.0, 1.0);
        const double s2 = std::fmod(ph, 2.0);
        const double s3 = std::fmod(ph / 0.8, 2.0);
        const float rotY = float(s1 * 360.0);
        const float bob = float(0.075 * (1.0 - std::cos(M_PI * s2)));
        // alpha：普通灰静态 0.35（旧材质 opacity）/ 附魔呼吸 0.28↔0.45（t696 解析式）。
        const float alpha = hasEnch ? float(0.28 + 0.17 * 0.5 * (1.0 - std::cos(M_PI * s3)))
                                    : 0.35f;
        // rgb：附魔紫 (140,64,230) / 普通灰 (176,176,176) × k（旧 baseColor tintBySkyLight 同式；
        //   qRound 保量化最近邻）。alpha 进表（hasTransparency ⇒ 渲染生效）。
        const QColor col(hasEnch ? qRound(140.0 * k) : qRound(176.0 * k),
                         hasEnch ? qRound(64.0 * k) : qRound(176.0 * k),
                         hasEnch ? qRound(230.0 * k) : qRound(176.0 * k),
                         qRound(double(alpha) * 255.0));
        entries[j] = calculateTableEntry(QVector3D(pos.x(), pos.y() + bob, pos.z()),
                                         QVector3D(0.45f, 0.45f, 0.45f),
                                         QVector3D(0, rotY, 0),
                                         col);
    }
    if (instanceCount) *instanceCount = cnt;
    return m_buffer;
}

int GlowShellInstancing::probeInstanceCount()
{
    int cnt = 0;
    getInstanceBuffer(&cnt);
    return cnt;
}

bool GlowShellInstancing::probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale,
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
