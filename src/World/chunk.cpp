#include "chunk.h"

Chunk::Chunk(int originX, int originZ, int height)
    : m_originX(originX), m_originZ(originZ), m_height(height),
      // 声明顺序：m_height 先于 m_voxels 初始化，故此处可安全引用 m_height。
      m_voxels(size_t(kSize * kSize * m_height), 0)
{
    // 新建即全空气 → 每列无实体 → heightmap = -1（全空列无面可画，sky 判定无副作用）。
    // worldgen 自底向上逐格 setBlock 时每格都会维护 heightmap，generate 末即正确。
    for (int i = 0; i < kSize * kSize; ++i) m_heightmap[i] = -1;
}

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

    // t121：增量维护本列 heightmap（PLAN §2-H「per-column 天光」）。
    //   置非空气：若高于现顶则抬升（worldgen 自底向上逐格置实 → 每列被抬到其最高实体 y）。
    //   置空气：若破的是顶块（ly == hm）则自顶向下回扫找新顶（仅此情形 O(height)，其余 O(1)）。
    int &hm = m_heightmap[lx + kSize * lz];
    if (id != 0) {
        if (ly > hm) hm = ly;
    } else if (ly == hm) {
        recomputeColumnHeightmap(lx, lz);
    }
}

int Chunk::heightmapAt(int lx, int lz) const
{
    if (lx < 0 || lz < 0 || lx >= kSize || lz >= kSize) return -1;
    return m_heightmap[lx + kSize * lz];
}

// 单列自顶向下重扫：找首个非空气作新顶（破顶块用），列全空 → -1。仅 setBlock 破顶块时调用。
void Chunk::recomputeColumnHeightmap(int lx, int lz)
{
    for (int y = m_height - 1; y >= 0; --y) {
        if (m_voxels[size_t(lx + kSize * (lz + kSize * y))] != 0) {
            m_heightmap[lx + kSize * lz] = y;
            return;
        }
    }
    m_heightmap[lx + kSize * lz] = -1;
}
