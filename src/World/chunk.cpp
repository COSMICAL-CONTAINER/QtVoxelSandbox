#include "chunk.h"
#include "blockregistry.h" // t150b：heightmap 跳过 Torch 判定（BlockRegistry::Torch 单一权威 id）

#include <algorithm> // std::fill（clearLight 归零光场）

Chunk::Chunk(int originX, int originZ, int height)
    : m_originX(originX), m_originZ(originZ), m_height(height),
      // 声明顺序：m_height 先于 m_voxels / m_states 初始化，故此处可安全引用 m_height。
      m_voxels(size_t(kSize * kSize * m_height), 0),
      m_states(size_t(kSize * kSize * m_height), 0), // t133：并行 state 数组（全 0；常规方块无 state）
      m_lightField(size_t(kSize * kSize * m_height), 0) // t151：光场第三数组（全 0；flood-fill 写入）
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

// t133：4 参数版默认 state=0（兼容 worldgen / 旧调用 / 常规方块 —— 它们无 state 概念）。
//   委托 5 参数版避免逻辑重复；id 变更时重置 state=0 是正确语义（新方块新 state，防 stale 残留）。
void Chunk::setBlock(int lx, int ly, int lz, quint8 id)
{
    setBlock(lx, ly, lz, id, quint8(0));
}

// 写体素 id + state（并行数组同索引），并增量维护 heightmap。越界忽略（防御）。
void Chunk::setBlock(int lx, int ly, int lz, quint8 id, quint8 state)
{
    if (lx < 0 || ly < 0 || lz < 0 || lx >= kSize || lz >= kSize || ly >= m_height)
        return; // 局部越界：忽略（防御）
    const size_t i = size_t(lx + kSize * (lz + kSize * ly));
    m_voxels[i] = id;
    m_states[i] = state; // t133：并行写 state（常规方块=0；异形方块=朝向/开合）

    // t121：增量维护本列 heightmap（PLAN §2-H「per-column 天光」）。
    //   置非空气：若高于现顶则抬升（worldgen 自底向上逐格置实 → 每列被抬到其最高实体 y）。
    //   置空气：若破的是顶块（ly == hm）则自顶向下回扫找新顶（仅此情形 O(height)，其余 O(1)）。
    //   t150b：Torch 不计入 heightmap —— 它是细立柱（非整立方），既不该挡天光（让火把下方地块被误判
    //   地下变暗），也不该在投影阴影回扫里被当成整格遮阳体（让火把列投出整格黑影）。故 Torch 视同空气：
    //   放置 Torch 不抬升 hm；破块回扫跳过 Torch。两者均修「火把所在列的整格阴影 / 地块变暗」。
    int &hm = m_heightmap[lx + kSize * lz];
    if (id == 0) {
        if (ly == hm) recomputeColumnHeightmap(lx, lz); // 破顶块 → 重扫找新顶
    } else if (id != BlockRegistry::Torch) {
        if (ly > hm) hm = ly; // 放置非 Torch 实体 → 抬升
    }
    // 放置 Torch：不计入 heightmap（透明于天光 / 阴影），hm 不变
}

// t133：读取本格 state（朝向 / 开合）。越界 / 常规方块 → 0。mesher 据此为异形方块选朝向变体。
quint8 Chunk::stateAt(int lx, int ly, int lz) const
{
    if (lx < 0 || ly < 0 || lz < 0 || lx >= kSize || lz >= kSize || ly >= m_height)
        return 0; // 局部越界 = 无 state（防御）
    return m_states[size_t(lx + kSize * (lz + kSize * ly))];
}

int Chunk::heightmapAt(int lx, int lz) const
{
    if (lx < 0 || lz < 0 || lx >= kSize || lz >= kSize) return -1;
    return m_heightmap[lx + kSize * lz];
}

// t151 光场读（越界 / 常规 → 0）。sky 取高 4 位、block 取低 4 位。mesher 与 flood（经 ChunkManager）只读。
quint8 Chunk::skyLightAt(int lx, int ly, int lz) const
{
    if (lx < 0 || ly < 0 || lz < 0 || lx >= kSize || lz >= kSize || ly >= m_height)
        return 0;
    return quint8((m_lightField[size_t(lx + kSize * (lz + kSize * ly))] >> 4) & 0x0F);
}

quint8 Chunk::blockLightAt(int lx, int ly, int lz) const
{
    if (lx < 0 || ly < 0 || lz < 0 || lx >= kSize || lz >= kSize || ly >= m_height)
        return 0;
    return quint8(m_lightField[size_t(lx + kSize * (lz + kSize * ly))] & 0x0F);
}

// 写两通道：各夹到 0..15 后打包成一字节（sky<<4 | block）。flood-fill 增量更新用（取 max 后写入）。
void Chunk::setLight(int lx, int ly, int lz, quint8 sky, quint8 block)
{
    if (lx < 0 || ly < 0 || lz < 0 || lx >= kSize || lz >= kSize || ly >= m_height)
        return; // 局部越界：忽略（防御）
    if (sky > 15) sky = 15;
    if (block > 15) block = 15;
    m_lightField[size_t(lx + kSize * (lz + kSize * ly))] = quint8((sky << 4) | block);
}

// re-flood 前清场：全格光场归零（天光 / 方块光都从种子重新传播）。
void Chunk::clearLight()
{
    std::fill(m_lightField.begin(), m_lightField.end(), quint8(0));
}

// t176：整 chunk（16×16 列）自顶向下重扫 heightmap。存档加载回填 voxel 后调（heightmap 派生于
//   体素，存档只存原始数组故需重算）。逐列复用 recomputeColumnHeightmap 的「跳 Torch、列全空→-1」语义，
//   与增量维护一致 → 加载后的 heightmap 与 worldgen / 编辑增量维护结果一致。仅 WorldStore load 路径调。
void Chunk::recomputeAllHeightmaps()
{
    for (int lz = 0; lz < kSize; ++lz)
        for (int lx = 0; lx < kSize; ++lx)
            recomputeColumnHeightmap(lx, lz);
}

// 单列自顶向下重扫：找首个非空气作新顶（破顶块用），列全空 → -1。仅 setBlock 破顶块时调用。
//   t150b：跳过 Torch —— Torch 不计入 heightmap（见 setBlock 注释），重扫时亦跳过，使火把不再被
//   当作列顶（否则火把上方地块 / 邻列阴影会受其影响）。
void Chunk::recomputeColumnHeightmap(int lx, int lz)
{
    for (int y = m_height - 1; y >= 0; --y) {
        const quint8 v = m_voxels[size_t(lx + kSize * (lz + kSize * y))];
        if (v != 0 && v != BlockRegistry::Torch) {
            m_heightmap[lx + kSize * lz] = y;
            return;
        }
    }
    m_heightmap[lx + kSize * lz] = -1;
}
