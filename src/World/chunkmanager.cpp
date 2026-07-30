#include "chunkmanager.h"

#include <algorithm>

ChunkManager::ChunkManager(int width, int depth, int height)
{
    recreate(width, depth, height);
}

void ChunkManager::recreate(int width, int depth, int height)
{
    m_width = std::max(0, width);
    m_depth = std::max(0, depth);
    m_height = std::max(0, height);
    m_chunksX = (m_width + kSize - 1) / kSize;  // ceil：48→3；非整除时末列覆盖到 kSize 但世界越界判空
    m_chunksZ = (m_depth + kSize - 1) / kSize;
    // unique_ptr 是 move-only：不能用 assign(count, value)（需拷贝）。先 resize（默认构造=null），
    // 再逐格 move 赋值新 chunk。
    m_chunks.clear();
    m_chunks.resize(size_t(m_chunksX * m_chunksZ));
    for (int cz = 0; cz < m_chunksZ; ++cz)
        for (int cx = 0; cx < m_chunksX; ++cx)
            m_chunks[size_t(cx + m_chunksX * cz)] =
                std::make_unique<Chunk>(cx * kSize, cz * kSize, m_height);
}

Chunk *ChunkManager::chunk(int cx, int cz) const
{
    if (cx < 0 || cz < 0 || cx >= m_chunksX || cz >= m_chunksZ)
        return nullptr;
    return m_chunks[size_t(cx + m_chunksX * cz)].get();
}

Chunk *ChunkManager::chunkAtWorld(int x, int z) const
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return nullptr;
    return chunk(x / kSize, z / kSize);
}

quint8 ChunkManager::blockAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0; // 世界越界 = 空气（面剔除画边界面；物理把界外当可走出/可坠落）
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->blockAt(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize) : quint8(0);
}

// t121：世界坐标 (x,z) 列的 heightmap（PLAN §2-H）。路由到所在 chunk 的局部列；越界 / 无 chunk → -1。
int ChunkManager::heightmapAt(int x, int z) const
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return -1; // 世界越界 = 无实体（mesher 对越界列本就无面可画）
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->heightmapAt(x - (x / kSize) * kSize, z - (z / kSize) * kSize) : -1;
}

bool ChunkManager::setBlock(int x, int y, int z, quint8 id)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 世界越界：拒绝
    const int cx = x / kSize, cz = z / kSize;
    const int lx = x - cx * kSize, lz = z - cz * kSize;
    Chunk *c = chunk(cx, cz);
    if (!c) return false;
    c->setBlock(lx, y, lz, id);
    c->markDirty();
    // 边界格（local x/z 贴 chunk 边沿）→ 该格面可见性可能影响邻接 chunk 的边界剔除，
    // 标邻接 chunk 脏（dev-spec t02 验收；为 t03「跨边界破放不破坏邻居 mesh」准备）。
    if (lx == kSize - 1) { if (Chunk *n = chunk(cx + 1, cz)) n->markDirty(); } // +X 邻
    if (lx == 0)         { if (Chunk *n = chunk(cx - 1, cz)) n->markDirty(); } // -X 邻
    if (lz == kSize - 1) { if (Chunk *n = chunk(cx, cz + 1)) n->markDirty(); } // +Z 邻
    if (lz == 0)         { if (Chunk *n = chunk(cx, cz - 1)) n->markDirty(); } // -Z 邻
    return true;
}
