#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include <QtGlobal> // quint8

#include <memory>
#include <vector>

#include "chunk.h"

// ChunkManager：持有一片连续的 chunk 列网格（width×depth 平面铺满，每 chunk 16×16 列），
// 负责「世界坐标 ↔ chunk/局部坐标」路由、跨 chunk blockAt/setBlock、越界判定、
// 与边界格的邻接 chunk 脏标记（为 t03 跨边界剔除准备）。
//
// World 层：**不**依赖 Renderer/Physics/QtQuick3D（PLAN §2 分层铁律 + 不变量 J）。
// 越界（世界坐标超出 [0,width)×[0,height)×[0,depth)）blockAt 视作空气、setBlock 拒绝。
class ChunkManager
{
public:
    // 默认构造 → 空 (0,0,0) 网格；World 持为成员后在 generate() 里 recreate 到实际尺寸。
    ChunkManager() : ChunkManager(0, 0, 0) {}
    ChunkManager(int width, int depth, int height);

    int width() const { return m_width; }
    int depth() const { return m_depth; }
    int height() const { return m_height; }
    int chunksX() const { return m_chunksX; }
    int chunksZ() const { return m_chunksZ; }
    int chunkCount() const { return int(m_chunks.size()); }

    // 世界坐标查询/写入（含跨 chunk 路由）。越界 blockAt 返回 0；setBlock 越界返回 false。
    // setBlock 成功写入后：标目标 chunk 脏；该格贴 chunk 边沿 → 同标邻接 chunk 脏（t03 准备）。
    quint8 blockAt(int x, int y, int z) const;
    bool setBlock(int x, int y, int z, quint8 id);
    // t133 不完整方块 state（朝向/开合）：世界坐标读 / 写（跨 chunk 路由，同 blockAt/setBlock）。
    //   setBlock 默认 state=0（兼容）；4 参数 setBlock 委托 5 参数 (id, 0)（新方块重置 state，防 stale）。
    quint8 stateAt(int x, int y, int z) const;
    bool setBlock(int x, int y, int z, quint8 id, quint8 state);
    // t121：世界坐标列的「自顶向下首个非空气」y（越界 / 空列 → -1）。mesher 据此判顶点见天（PLAN §2-H）。
    int heightmapAt(int x, int z) const;
    // t151 光场路由（世界坐标 ↔ chunk 局部）：sky/block 读 + 写 + 全清。越界读返回 0、写忽略。
    //   flood-fill（World）与 mesher 经此访问 per-voxel 光场（跨 chunk 自动路由）。OOB 语义统一交 caller。
    quint8 skyLightAt(int x, int y, int z) const;
    quint8 blockLightAt(int x, int y, int z) const;
    void setLight(int x, int y, int z, quint8 sky, quint8 block);
    void clearAllLight();

    // 取 chunk（网格坐标 cx,cz；越界返回 nullptr）。
    Chunk *chunk(int cx, int cz) const;
    // 世界坐标 (x,z) 所在 chunk（越界 nullptr）。
    Chunk *chunkAtWorld(int x, int z) const;

    // 尺寸变化时重建网格（清空旧 chunk，新建零填充 chunk，全部脏）。
    void recreate(int width, int depth, int height);

private:
    static constexpr int kSize = Chunk::kSize;

    int m_width = 0, m_depth = 0, m_height = 0;
    int m_chunksX = 0, m_chunksZ = 0;                  // = ceil(width/kSize), ceil(depth/kSize)
    std::vector<std::unique_ptr<Chunk>> m_chunks;      // 索引 [cx + m_chunksX * cz]
};

#endif // CHUNKMANAGER_H
