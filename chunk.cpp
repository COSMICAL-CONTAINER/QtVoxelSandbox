#include "chunk.h"

Chunk::Chunk(int originX, int originZ, int height)
    : m_originX(originX), m_originZ(originZ), m_height(height),
      // 声明顺序：m_height 先于 m_voxels 初始化，故此处可安全引用 m_height。
      m_voxels(size_t(kSize * kSize * m_height), 0) {}

quint8 Chunk::blockAt(int lx, int ly, int lz) const
{
    if (lx < 0 || ly < 0 || lz < 0 || lx >= kSize || lz >= kSize || ly >= m_height)
        return 0; // 局部越界 = 空气（防御；正常路由不应到达）
    return m_voxels[size_t(lx + kSize * (lz + kSize * ly))];
}

void Chunk::setBlock(int lx, int ly, int lz, quint8 id)
{
    if (lx < 0 || ly < 0 || lz < 0 || lx >= kSize || lz >= kSize || ly >= m_height)
        return; // 局部越界：忽略（防御）
    m_voxels[size_t(lx + kSize * (lz + kSize * ly))] = id;
}
