#ifndef CHUNKGRIDLINES_H
#define CHUNKGRIDLINES_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 区块边界网格线（Renderer 视图）：按世界尺寸 + chunk 边长画 16×16 区块边界线框叠层（t277，
// 机制等价 MC F3+G chunk boundary display）。
//
// 输出 Lines 几何，每个 chunk 边界可见：
//   - 纵线：每个 (x 边界, z 边界) 交点 (x = chunkSize*n, z = chunkSize*m) 处一根纵线 y=0..height，
//     从任意角度可见（高出地表部分始终在视线中）。
//   - 顶 / 底面水平连线：沿每条 x 边界（固定 xb，z 走 0..depth）与每条 z 边界（固定 zb，x 走 0..width），
//     各在 y=0 与 y=height 画一条 —— 与纵线一起构成完整区块格线框。
// 顶点为世界坐标（Model 摆 position 0,0,0，无额外平移）；bounds = [0..worldWidth]×[0..worldHeight]×[0..worldDepth]。
//
// 动态几何：worldWidth/Depth/Height/chunkSize 任一 Q_PROPERTY 改变 → rebuild()。世界尺寸在 QML
// 构造期定稿（固定网格全驻留，t276），故重建极少（仅切世界 / 调网格尺寸时）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只按传入尺寸产出几何，不读栅格 / worldgen。
// 与 WireCube / SelectionWireBoxes 同层同模式（Lines 几何 + NoLighting 材质）。
class ChunkGridLines : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChunkGridLines)
    Q_PROPERTY(int worldWidth  READ worldWidth  WRITE setWorldWidth  NOTIFY worldWidthChanged)
    Q_PROPERTY(int worldDepth  READ worldDepth  WRITE setWorldDepth  NOTIFY worldDepthChanged)
    Q_PROPERTY(int worldHeight READ worldHeight WRITE setWorldHeight NOTIFY worldHeightChanged)
    Q_PROPERTY(int chunkSize   READ chunkSize   WRITE setChunkSize   NOTIFY chunkSizeChanged)

public:
    explicit ChunkGridLines(QQuick3DObject *parent = nullptr);

    int worldWidth() const { return m_worldWidth; }
    void setWorldWidth(int v);
    int worldDepth() const { return m_worldDepth; }
    void setWorldDepth(int v);
    int worldHeight() const { return m_worldHeight; }
    void setWorldHeight(int v);
    int chunkSize() const { return m_chunkSize; }
    void setChunkSize(int v);

signals:
    void worldWidthChanged();
    void worldDepthChanged();
    void worldHeightChanged();
    void chunkSizeChanged();

private:
    void rebuild(); // 按 (m_worldWidth, m_worldDepth, m_worldHeight, m_chunkSize) 重画区块边界网格。

    int m_worldWidth = 0;
    int m_worldDepth = 0;
    int m_worldHeight = 0;
    int m_chunkSize = 16;
};

#endif // CHUNKGRIDLINES_H
