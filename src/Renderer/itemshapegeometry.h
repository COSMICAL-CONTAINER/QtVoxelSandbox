#ifndef ITEMSHAPEGEOMETRY_H
#define ITEMSHAPEGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

#include "blockregistry.h" // def / tileIndex → per-face 图集 UV（Renderer 读 World 层注册表，同 BlockCube）

// 方块**异形 / cross**的物品 3D 模型几何（t880；Renderer 层）。
//
// 用途（t880 资源查看器 + 掉落物 3D 化）：把「有 3D 模型的物品」从 BillboardQuad 平面图标升级为真实
// 3D 形状 —— 木/铁活板门（合态薄板）、火把（细立柱）、台阶族（半高盒）、木板楼梯（整步 + 背墙）、
// 雪层（1/8 薄板）、草丛（对角交叉双面片）、附魔台（0.75 矮盒）。块几何与 World 层
// partialblockgeometry 的形状语义同源（盒区一致；本类是**物品摆位**变体：各形状绕自身高度中点居中
// —— 同 build_cube_icons._partial_y_mid 的图标居中口径），贴图按 BlockDef per-face 图集瓦片 +
// 半纹素内缩（同 BlockCube）。
//
// 与 BlockCube 的分工：BlockCube 是满 1×1×1 立方（ShapeFull 方块段）；本类管非满格形状（slab/
// stairs/trapdoor/snow layer/enchant table/torch/cross）。掉落物 delegate 与 ResourceBrowser 预览按
// isItem3DFamily 谓词分流到本类。
//
// 分面贴图：每子盒三段瓦片 —— 顶(+Y)=topTile / 底(-Y)=bottomTile / 四侧=sideTile（全部从 BlockDef
// 单一权威取）；活板门族薄侧边 per-family 分流（铁=iron_block / 木=planks，与 World mesher t742/t879
// 及运行期图标 spec 同一语言）。cross 族（草丛）用 def.topTile 整瓦片双面平铺。火把侧窗取 torch 瓦片
// 中央列带（贴图火把本体的窄柱），顶/底小窗取焰心区（参数视觉钉死，待用户目视确认）。
//
// 坐标约定：形状以**自身高度中点**为原点（如活板门薄板 y∈[-3/32,+3/32]、台阶 y∈[-0.25,+0.25]、
// 附魔台 y∈[-0.375,+0.375]）—— 掉落物 bob/自转与查看器旋转绕形心摆，视觉稳定。水平 footprint 保持
// [−0.5,+0.5] 满格宽。顶点：pos(3)+uv(2)=5 float（无顶点色通道 —— 昼夜明暗由 QML baseColor ×
// terrainLight 承载，同掉落物 BlockCube 分支）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只读 BlockRegistry（World 层），
// 不反向写。依赖只向下（同 BlockCube / MobModel）。
class ItemShapeGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ItemShapeGeometry)
    Q_PROPERTY(int blockId READ blockId WRITE setBlockId NOTIFY blockIdChanged)

public:
    explicit ItemShapeGeometry(QQuick3DObject *parent = nullptr);

    int blockId() const { return m_blockId; }
    void setBlockId(int id);

signals:
    void blockIdChanged();

private:
    void rebuild(); // 按 m_blockId 的 def.shape（+ 特型覆盖）建多盒/交叉片几何后整几何重传 GPU。

    int m_blockId = int(BlockRegistry::WoodSlab); // 默认木板台阶（合法非空，防未设 blockId 时空几何）
};

#endif // ITEMSHAPEGEOMETRY_H
