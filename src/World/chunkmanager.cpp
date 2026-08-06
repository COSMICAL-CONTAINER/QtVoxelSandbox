#include "chunkmanager.h"

#include <algorithm>

#include "blockregistry.h" // t360 solidTopOffset（列顶实面高度；PCF 软影按方块真实模型高度判遮挡）

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

// t155g：清所有 chunk 的 dirty 标记（World::setBlock 在 emit worldChanged 后调）。
//   旧版 buildMesh 末尾 clearDirty 由「先处理的 segment」抢先清掉共享 chunk 脏标记 →
//   后处理的 segment（terrain/water 二者之一）见 dirty=false 跳过重建 → 那段 mesh 陈旧到下个 sun-step。
//   改：buildMesh 不再清脏，统一由 World 在两段都重建完（emit worldChanged 同步返回后）清。
void ChunkManager::clearAllDirty()
{
    for (auto &c : m_chunks)
        if (c) c->clearDirty();
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

// t360 列顶实面世界 y（见头注释）：单次 chunk 路由取 heightmap + 列顶方块 block/state → solidTopOffset。
//   空列 / 越界 / 无 chunk → -1（不遮挡）。PCF 软影（voxellight.h sunShadow）每采样调本。
float ChunkManager::columnTopSurfaceY(int x, int z) const
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return -1.0f;
    Chunk *c = chunk(x / kSize, z / kSize);
    if (!c) return -1.0f;
    const int lx = x - (x / kSize) * kSize;
    const int lz = z - (z / kSize) * kSize;
    const int hm = c->heightmapAt(lx, lz);
    if (hm < 0) return -1.0f;
    return float(hm) + BlockRegistry::solidTopOffset(c->blockAt(lx, hm, lz), c->stateAt(lx, hm, lz));
}

// t151 光场路由（世界坐标 → chunk 局部）。越界读返回 0（无光）。flood-fill 与 mesher 经此访问光场。
quint8 ChunkManager::skyLightAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0;
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->skyLightAt(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize) : quint8(0);
}

quint8 ChunkManager::blockLightAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0;
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->blockLightAt(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize) : quint8(0);
}

void ChunkManager::setLight(int x, int y, int z, quint8 sky, quint8 block)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return; // 越界忽略（flood 自行跳过 OOB 邻居）
    Chunk *c = chunk(x / kSize, z / kSize);
    if (c) c->setLight(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize, sky, block);
}

// 全部 chunk 光场归零（re-flood 前清场，World::recomputeLightField 调）。
void ChunkManager::clearAllLight()
{
    for (auto &cp : m_chunks)
        if (cp) cp->clearLight();
}

// t133：世界坐标 state 读（跨 chunk 路由，同 blockAt）。越界 / 无 chunk → 0（常规方块无 state）。
quint8 ChunkManager::stateAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0; // 世界越界 = 无 state
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->stateAt(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize) : quint8(0);
}

bool ChunkManager::setBlock(int x, int y, int z, quint8 id)
{
    return setBlock(x, y, z, id, quint8(0)); // t133：默认 state=0（兼容；新方块重置 state 防 stale）
}

// t133：写 id + state + 标脏（含边界邻接）。与 4 参数版同一路径，仅多写一字节 state。
bool ChunkManager::setBlock(int x, int y, int z, quint8 id, quint8 state)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 世界越界：拒绝
    const int cx = x / kSize, cz = z / kSize;
    const int lx = x - cx * kSize, lz = z - cz * kSize;
    Chunk *c = chunk(cx, cz);
    if (!c) return false;
    c->setBlock(lx, y, lz, id, state);
    c->markDirty();
    // 边界格（local x/z 贴 chunk 边沿）→ 该格面可见性可能影响邻接 chunk 的边界剔除，
    // 标邻接 chunk 脏（dev-spec t02 验收；为 t03「跨边界破放不破坏邻居 mesh」准备）。
    if (lx == kSize - 1) { if (Chunk *n = chunk(cx + 1, cz)) n->markDirty(); } // +X 邻
    if (lx == 0)         { if (Chunk *n = chunk(cx - 1, cz)) n->markDirty(); } // -X 邻
    if (lz == kSize - 1) { if (Chunk *n = chunk(cx, cz + 1)) n->markDirty(); } // +Z 邻
    if (lz == 0)         { if (Chunk *n = chunk(cx, cz - 1)) n->markDirty(); } // -Z 邻
    return true;
}
