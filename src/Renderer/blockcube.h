#ifndef BLOCKCUBE_H
#define BLOCKCUBE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

#include "blockregistry.h" // tileIndex → per-face 图集 UV（Renderer 读 World 数据，同 chunkgeometry）

// 带贴图的单位立方体几何（1×1×1，居中 ±0.5，Triangles，per-face 图集 UV）。
//
// 用途（t35 方块掉落实体）：item entity 的小方块图标 —— 用图集贴图还原被破方块的外观
// （草顶 / 草侧 / 圆石各面 …）。复用 BlockRegistry::tileIndex 的 per-face 瓦片映射（与
// chunkgeometry 同一权威），故掉落实体看上去就是「缩小的方块」。
//
// 与既有几何的区别：
//   - UnitCube：仅 pos（玩家模型 / 手用纯色材，不采样贴图）；
//   - CrackBox：每面全幅 UV（0..1，铺整张裂纹贴图）；
//   - 本类 BlockCube：每面 UV 按方块 + 面查图集瓦片序号取子区（半纹素内缩防渗色，同
//     chunkgeometry），故 6 面可显示不同 tile（草顶 vs 草侧）。
// 顶点 24（每面 4 角独立，便于 per-face UV），索引 36（12 三角形）；CCW 朝外（默认 backface
// 剔除下，外法线面可见）。
//
// blockId 是 Q_PROPERTY：QML 据 entity 的 itemId 设它；setter 触发 rebuild（重算每面 UV 后
// 整几何重传 GPU）。顶点位置恒定（±0.5 立方体），只 UV 随 blockId 变。
//
// 为何不用内置 #Cube（spec 字面建议）：本工程实测**静态 source:"#Cube" Model 不渲染**
// （t31 诊断结论；粒子 instanced #Cube 可见、静态 #Cube 不可见），故带贴图的静态方块
// 走自定义 QQuick3DGeometry + NoLighting 这条已验证可见路径（同 UnitCube / CrackBox）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只读 BlockRegistry（World 数据），
// 不反向写。依赖只向下。
class BlockCube : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BlockCube)
    Q_PROPERTY(int blockId READ blockId WRITE setBlockId NOTIFY blockIdChanged)

public:
    explicit BlockCube(QQuick3DObject *parent = nullptr);

    int blockId() const { return m_blockId; }
    void setBlockId(int id);

signals:
    void blockIdChanged();

private:
    void rebuild(); // 顶点位置恒定；按 m_blockId 重算每面 UV 后整几何重传。

    int m_blockId = int(BlockRegistry::Stone); // 默认石头（合法非空，防未设 blockId 时空 UV）
};

#endif // BLOCKCUBE_H
