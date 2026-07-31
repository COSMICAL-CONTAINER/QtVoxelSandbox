#ifndef CHUNK_H
#define CHUNK_H

#include <QtGlobal> // quint8

#include <atomic>
#include <vector>

// 体素 chunk 列（PLAN §2 不变量 J 的存储单元）：16(X) × 16(Z) × 全世界高度(Y)。
// 当前世界高度=16 → 单 chunk 即 16×16×16；t07 放大到 16×16×128 时本类无需改（chunk 跨满高）。
//
// World 层数据：只存体素 + 脏标记，**不**依赖 Renderer/Physics/QtQuick3D（PLAN §2 分层铁律）。
// 脏标记供 t03 per-chunk mesher 判定哪些 chunk 需重建；本任务(t02)单 mesh 重建不消费它，
// 但 setBlock 写边界格时仍标邻接 chunk 脏（为 t03 跨边界剔除留接口，dev-spec 验收要求）。
class Chunk
{
public:
    static constexpr int kSize = 16; // X、Z 方向 chunk 边长（世界单位 = 方块数）

    Chunk(int originX, int originZ, int height);

    int originX() const { return m_originX; }
    int originZ() const { return m_originZ; }
    int height() const { return m_height; }

    // 局部坐标查询/写入：lx,lz ∈ [0,kSize)，ly ∈ [0,height)。越界读返回 0(空气)，写忽略。
    quint8 blockAt(int lx, int ly, int lz) const;
    void setBlock(int lx, int ly, int lz, quint8 id);
    // t133 不完整方块 state（朝向 / 开合；PLAN §2「(id,metadata) 方块模型」精神）：并行于 m_voxels 的
    //   逐格状态字节。常规方块 state 恒 0；异形方块（id >= BlockRegistry::FirstPartial）用 state 编码
    //   朝向（stairs 4 向 / door 朝向 / slab 上下半）与开合（door/trapdoor 开关）。mesher 经
    //   PartialBlockGeometry::append(blockId, state) 据此生成异形顶点。setBlock 默认 state=0（兼容旧调用）。
    quint8 stateAt(int lx, int ly, int lz) const;
    void setBlock(int lx, int ly, int lz, quint8 id, quint8 state); // 写 id + state（4 参数版默认 state=0）

    // t121 天光 heightmap（PLAN §2-H「per-column 天光——自顶向下首个实体的 heightmap」）：
    // 每列「自顶向下首个非空气方块」的 y（列全空 → -1）。mesher 据此判定顶点是否见天：
    // ly >= heightmap → 见天（地表/天空间）→ 天光满（1.0）；否则地下 → 暗（0.2）。setBlock 增量
    // 维护：置非空气时若高于现顶则抬升、破坏顶块时自顶向下回扫找新顶；worldgen 自底向上逐格置实
    // → 每格 setBlock 都维护 → generate 末各列 heightmap 即正确，无需额外全扫。
    int heightmapAt(int lx, int lz) const;

    // 脏标记：体素被改 → 置脏；mesher(t03) 只重建脏 chunk。新建即脏（首帧需 mesh）。
    bool dirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

private:
    void recomputeColumnHeightmap(int lx, int lz); // 单列自顶向下重扫（破坏顶块后回扫找新顶）

    int m_originX;                       // 世界 X 起点（= cx*kSize）
    int m_originZ;                       // 世界 Z 起点（= cz*kSize）
    int m_height;                        // Y 向高度（= 世界高度；chunk 跨满高）
    std::vector<quint8> m_voxels;        // kSize × kSize × height，索引 lx + kSize*(lz + kSize*ly)
    // t133 不完整方块 state 并行数组（同 m_voxels 尺寸 / 索引）：常规方块恒 0；异形方块存朝向/开合。
    //   setBlock 写入时同步刷新；worldgen / 4 参数 setBlock 默认写 0（兼容）。mesher 经 stateAt 读。
    std::vector<quint8> m_states;
    // t121：每列「自顶向下首个非空气」的 y（-1=全空列）。setBlock 增量维护（见 heightmapAt 注释）。
    int m_heightmap[kSize * kSize];
    std::atomic<bool> m_dirty{true};     // 新建即脏（首帧需 mesh）。atomic：未来 mesh-worker 读脏标记无 TSan 数据竞争（voxel 数组仍需 §2-C per-chunk 锁，线程化时补）
};

#endif // CHUNK_H
