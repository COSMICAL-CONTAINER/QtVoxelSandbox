#include "mapstore.h"

// map 地图数据存储实现（t1114 初建；t1132 扩展域 + 持久化首片——设计裁定与存档口径见 mapstore.h
// 头注，本文件只做像素簿记 + 数据集出入库）。
MapStore *MapStore::s_active = nullptr;

MapStore::MapStore(QObject *parent)
    : QObject(parent)
{
    s_active = this; // 图像 provider 拉取面注册（析构对称注销，防悬垂）
}

MapStore::~MapStore()
{
    if (s_active == this)
        s_active = nullptr;
}

void MapStore::initialize(int width, int depth)
{
    if (width <= 0 || depth <= 0)
        return; // 非法定版 no-op（调用侧传 World 尺寸，恒正；防御负例）
    m_worldWidth = width;
    m_worldDepth = depth;
    // 数据集 = 世界核心域 + 四侧探索前沿带（t1132 MAP-02——era 128 画布封顶的有界对应面，禁无限
    //   位图）。全图未探索底色（quint32 逐格填充——QByteArray 单字节 fill 无法表达 4 字节色；resize
    //   后逐格覆写，初值无关紧要）。
    m_width = width + 2 * kDomainMargin;
    m_depth = depth + 2 * kDomainMargin;
    m_pixels.resize(m_width * m_depth * 4);
    auto *px = reinterpret_cast<quint32 *>(m_pixels.data());
    const int cells = m_width * m_depth;
    for (int i = 0; i < cells; ++i)
        px[i] = kUnexploredColor;
    ++m_revision;
    emit mapChanged();
}

void MapStore::writeColumn(int x, int z, quint32 argb)
{
    if (!hasMap())
        return; // 未建库 no-op（探索盒过界列由 caller 跳过，本侧双保险）
    // 世界坐标 → 数据集下标（+kDomainMargin 平移；带外列 no-op——负向越带 / 正向越带同拒）。
    const int cx = x + kDomainMargin;
    const int cz = z + kDomainMargin;
    if (cx < 0 || cz < 0 || cx >= m_width || cz >= m_depth)
        return;
    reinterpret_cast<quint32 *>(m_pixels.data())[size_t(cz) * size_t(m_width) + size_t(cx)] = argb;
}

void MapStore::commitColumns()
{
    if (!hasMap())
        return; // 未建库 no-op（无数据可提交）
    ++m_revision;
    emit mapChanged();
}

quint32 MapStore::columnColor(int x, int z) const
{
    if (!hasMap())
        return kUnexploredColor;
    const int cx = x + kDomainMargin;
    const int cz = z + kDomainMargin;
    if (cx < 0 || cz < 0 || cx >= m_width || cz >= m_depth)
        return kUnexploredColor; // 带外读兜底（未探索底色语义一致）
    return reinterpret_cast<const quint32 *>(m_pixels.constData())[size_t(cz) * size_t(m_width) + size_t(cx)];
}

QImage MapStore::renderImage() const
{
    if (!hasMap())
        return QImage();
    // 常量数据包装（共享不拷）→ copy() 出独立副本（provider 侧生命周期解耦）。Format_ARGB32 与
    // m_pixels 的 quint32 ARGB 逐格同 layout，零转换。
    const QImage wrapped(reinterpret_cast<const uchar *>(m_pixels.constData()),
                         m_width, m_depth, qsizetype(m_width) * 4, QImage::Format_ARGB32);
    return wrapped.copy();
}

void MapStore::clearAll()
{
    if (!hasMap())
        return; // 未建库 no-op（不无故发信号，SignStore::clearAll 同门口径）
    m_worldWidth = 0;
    m_worldDepth = 0;
    m_width = 0;
    m_depth = 0;
    m_pixels.clear();
    ++m_revision;
    emit mapChanged();
}

// t1132 MAP-03 导出（形状 = loadVariant 入参 / map_dataset 表行；世界核心域尺寸 + revision +
//   全幅像素原样——表行无 id 无序号 = 共享口径单份，era per-map 偏离登记见 mapstore.h 头注）。
QVariantMap MapStore::exportVariant() const
{
    if (!hasMap())
        return {}; // 未建库 → 空 map（caller 表清空语义；旧 caller 零改动兼容面）
    QVariantMap v;
    v.insert(QStringLiteral("present"), true);
    v.insert(QStringLiteral("width"), m_worldWidth);
    v.insert(QStringLiteral("depth"), m_worldDepth);
    v.insert(QStringLiteral("revision"), m_revision);
    v.insert(QStringLiteral("pixels"), m_pixels);
    return v;
}

// t1132 MAP-03 装载（整体替换内存——signStore.loadAll 同门口径）：空 map（旧档无行 / 无表）→
//   clearAll 降级（会话口径重探索面）；有行 → initialize 后像素 BLOB 回填 + revision 复原。
//   尺寸账不平（pixels 字节数 ≠ 数据集字节数）= 存档病 → 诚实降级 clearAll（不载半截数据）。
void MapStore::loadVariant(const QVariantMap &data)
{
    if (data.isEmpty() || !data.value(QStringLiteral("present")).toBool()) {
        clearAll(); // 旧档降级（未建库 no-op——不无故发信号）
        return;
    }
    bool okW = false, okD = false, okR = false;
    const int w = data.value(QStringLiteral("width")).toInt(&okW);
    const int d = data.value(QStringLiteral("depth")).toInt(&okD);
    const int rev = data.value(QStringLiteral("revision")).toInt(&okR);
    const QByteArray pixels = data.value(QStringLiteral("pixels")).toByteArray();
    if (!okW || !okD || !okR || w <= 0 || d <= 0)
        return; // 尺寸/版本键病 → no-op（保守不重建）
    initialize(w, d); // 全图未探索底色 + revision 自增 + 单信号（尺寸账随后校验）
    const qsizetype want = qsizetype(m_width) * qsizetype(m_depth) * 4;
    if (pixels.size() != want) {
        clearAll(); // 账不平 = 存档病（世界尺寸换代 / 截断）→ 诚实降级，不载半截
        return;
    }
    m_pixels = pixels;
    m_revision = rev; // 复原存档版本号（下次 commitColumns 自增仍单调）
    emit mapChanged();
}
