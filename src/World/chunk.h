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

    // t151 真光场（PLAN §2-H「方块光独立 flood-fill、时间不变」+ §M 真光场）：per-voxel 光场存本 chunk
    //   第三数组 m_lightField，一字节打包两通道（机制等价 MC 1.0 的 nibble 光照：高 4 位天光 sky、低 4 位
    //   方块光 block，各 0..15）。两通道由 World::recomputeLightField() 做 BFS flood-fill 写入：
    //   - 天光 sky：列首遮光方块（isSolid）以上的非遮光格种 sky=15，向邻接非遮光格传播衰减 1（洞穴入口
    //     让天光渗入、自顶向下照亮地表 / 天空间）。
    //   - 方块光 block：火把格种 block=14（radius14），向邻接非遮光格传播衰减 1（火把在地下 / 室内打出光池）。
    //   mesher 据本格 / 邻格光场值写顶点色（替代 t123 方向太阳 faceVc）；昼夜乘子仍由 QML baseColor 承担
    //   （平滑），故光场本身时间不变 —— 仅 setBlock / worldgen 改变栅格时重算。越界读返回 0。
    quint8 skyLightAt(int lx, int ly, int lz) const;
    quint8 blockLightAt(int lx, int ly, int lz) const;
    void setLight(int lx, int ly, int lz, quint8 sky, quint8 block); // 写两通道（各夹到 0..15 后打包）
    void clearLight();                                                // 全格光场归零（re-flood 前清场）

    // 脏标记：体素被改 → 置脏；mesher(t03) 只重建脏 chunk。新建即脏（首帧需 mesh）。
    bool dirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    // t188 perf：流体专用脏标记。语义：fluidOnlyDirty=true ⟺ 「自上次 clearAllDirty 以来该 chunk 收到的
    //   **全部** setBlock 写入均为流体类（oldId/newId 均属 Air/Water/Lava）」→ terrain/cross/glass/ice 段顶点
    //   不变（它们只画非流体方块），onWorldChanged 可跳过重建（水流风暴一 tick 数百段无谓重建的真因：
    //   水流/蒸发只动水面/流面几何 = water/lava 段事，terrain 段却被共享 dirty 拖着每 tick 重跑 culled/greedy）。
    //   water/lava 段不受影响（恒据 dirty 重建）。
    //   **累积规则（AND）**：默认值 true（中性「假定为流体专用」）。每次 setBlock：流体类写 → 不动本标
    //     （保留原值；若已被某次固体写清 false 则继续 false）；固体类写 → clearFluidOnlyDirty() 置 false。
    //     clearAllDirty 把它 reset 回 true（中性，开新一轮窗口）。如此「固体写」终态 false、「纯流体窗」终态 true、
    //     「流体+固体混窗」终态 false（固体 dominate → terrain 须重建）。chunk 新建即 true（首帧全段重建走 dirty 路径，
    //     fluidOnlyDirty=true 不影响 —— dirty=true && 首次 buildMesh 各段都跑）。
    //   判定：terrain/cross/glass/ice 段 rebuild iff `dirty() && !fluidOnlyDirty()`（非流体专用才重建）。
    bool fluidOnlyDirty() const { return m_fluidOnlyDirty; }
    void clearFluidOnlyDirty() { m_fluidOnlyDirty = false; } // 固体写 / 显式清（每次 clearAllDirty 后由 resetFluidOnlyDirty 复位 true）
    void resetFluidOnlyDirty() { m_fluidOnlyDirty = true; }  // clearAllDirty 末调：开新窗，假定流体专用待固体写否决

    // t176 存档（SQLite）原始字节访问：m_voxels / m_states / m_lightField 三块定长连续数组
    //   （同尺寸同索引 lx + kSize*(lz + kSize*ly)，长 = voxelCount()）。WorldStore（同 World 层）
    //   经这三组访问器把整 chunk 序列化为 BLOB（save）或从 BLOB memcpy 回填（load）——避免逐格
    //   Q_INVOKABLE 调用开销（16384 格 × 3 数组）。const 版供序列化、非 const 版供回填。
    size_t voxelCount() const { return m_voxels.size(); } // = kSize*kSize*height
    const quint8 *voxelData() const { return m_voxels.data(); }
    const quint8 *stateData() const { return m_states.data(); }
    const quint8 *lightData() const { return m_lightField.data(); }
    quint8 *voxelDataMut() { return m_voxels.data(); }
    quint8 *stateDataMut() { return m_states.data(); }
    quint8 *lightDataMut() { return m_lightField.data(); }
    // t176 load 后整 chunk 重算 heightmap（存档只存体素 / 光场，heightmap 派生于体素，需重算以保
    //   mesher 的 sunShadowAt 列投影正确 —— 同 recomputeColumnHeightmap 跳 Torch 的语义）。
    void recomputeAllHeightmaps();

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
    // t151：per-voxel 光场（sky<<4 | block，各 4 位 0..15）。recreate / clearLight 归零；flood-fill 写入。
    //   mesher 只读。与 m_voxels / m_states 同尺寸 / 同索引（lx + kSize*(lz + kSize*ly)）。
    std::vector<quint8> m_lightField;
    std::atomic<bool> m_dirty{true};     // 新建即脏（首帧需 mesh）。atomic：未来 mesh-worker 读脏标记无 TSan 数据竞争（voxel 数组仍需 §2-C per-chunk 锁，线程化时补）
    // t188 perf：流体专用脏标记（见 fluidOnlyDirty() 注释）。默认 true（中性「假定流体专用」；固体写清 false，
    //   clearAllDirty 复位 true）。atomic 同 m_dirty（ChunkGeometry 读 / World 写跨「逻辑线程」）。
    std::atomic<bool> m_fluidOnlyDirty{true};
};

#endif // CHUNK_H
