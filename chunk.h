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

    // 脏标记：体素被改 → 置脏；mesher(t03) 只重建脏 chunk。新建即脏（首帧需 mesh）。
    bool dirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

private:
    int m_originX;                       // 世界 X 起点（= cx*kSize）
    int m_originZ;                       // 世界 Z 起点（= cz*kSize）
    int m_height;                        // Y 向高度（= 世界高度；chunk 跨满高）
    std::vector<quint8> m_voxels;        // kSize × kSize × height，索引 lx + kSize*(lz + kSize*ly)
    std::atomic<bool> m_dirty{true};     // 新建即脏（首帧需 mesh）。atomic：未来 mesh-worker 读脏标记无 TSan 数据竞争（voxel 数组仍需 §2-C per-chunk 锁，线程化时补）
};

#endif // CHUNK_H
