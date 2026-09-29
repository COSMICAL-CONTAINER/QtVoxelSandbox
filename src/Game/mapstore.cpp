#include "mapstore.h"

// map 地图数据存储实现（t1114；设计裁定与存档口径见 mapstore.h 头注——本文件只做像素簿记）。
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
    m_width = width;
    m_depth = depth;
    // 全图未探索底色（quint32 逐格填充——QByteArray 单字节 fill 无法表达 4 字节色；resize 后
    //   逐格覆写，初值无关紧要）。
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
    if (!hasMap() || x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return; // 越界 / 未建库 no-op（探索盒过界列由 caller 跳过，本侧双保险）
    reinterpret_cast<quint32 *>(m_pixels.data())[size_t(z) * size_t(m_width) + size_t(x)] = argb;
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
    if (!hasMap() || x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return kUnexploredColor;
    return reinterpret_cast<const quint32 *>(m_pixels.constData())[size_t(z) * size_t(m_width) + size_t(x)];
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
    m_width = 0;
    m_depth = 0;
    m_pixels.clear();
    ++m_revision;
    emit mapChanged();
}
