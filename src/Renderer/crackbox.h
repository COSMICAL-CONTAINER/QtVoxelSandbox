#ifndef CRACKBOX_H
#define CRACKBOX_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 裂纹叠层几何（t410：按方块形状，每 sub-AABB 一盒；Triangles，每面独立全幅 UV）。
//
// 用途（t34 挖掘系统 / t410 异形贴合）：在生存挖掘目标方块上叠一个 Model，按进度切 6 阶裂纹贴图。
//   t410 起：裂纹叠层不再恒为整立方，而是按 BlockRegistry::selectionAABBs(blockId,state) 拆出每个 sub-AABB
//   各画一盒（下半砖→半高叠层、栅栏→中心立柱叠层、楼梯→下步+背墙双盒、薄板→贴地薄片…），裂纹贴合方块
//   实际形状而非整立方（机制对齐 MC destroy_stage 按形状缩放；spec t410「partial-block 破坏叠层匹配形状」）。
//   ShapeNone（火把 / cross 植物，瞬破且 selectionAABBs 空）兜底全格立方 = 旧行为，零回归。
//
// 与 UnitCube 的区别：UnitCube 仅 pos（玩家模型 / 手用纯色材，不采样贴图）；本类带 TexCoord0 → baseColorMap
// （裂纹 PNG）能正确映射到 6 面。每面 UV 全 0..1，整张贴图铺满该面（薄面贴图被压缩，与 MC 一致）。每盒 24
// 顶点（每面 4 角独立顶点便于 per-face UV）+ 36 索引（12 三角形）；多盒顺序追加。CCW 朝外（默认 backface
// 剔除下外法线面可见 → 裂纹只显在玩家看得到的那几面，背向面被剔除）。
//
// 动态重建（同 SelectionWireBoxes 模式）：blockId/state Q_PROPERTY 改变 → rebuild()。Main.qml 把 blockId/state
//   绑 player.miningBlock 处的 blockAt/stateAt → 切换挖掘目标时自动重画裂纹盒贴合新形状。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D + Core BlockRegistry），只读 BlockRegistry 数据产出几何；
//   挖掘进度态在 PlayerController（Game 层），选哪张贴图由 QML 呈现层据 player.miningStage 切 baseColorMap
//   （同 hotbar/held 浮动图标切法）。
class CrackBox : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CrackBox)
    Q_PROPERTY(int blockId READ blockId WRITE setBlockId NOTIFY blockIdChanged)
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)

public:
    explicit CrackBox(QQuick3DObject *parent = nullptr);

    int blockId() const { return m_blockId; }
    void setBlockId(int id);
    int state() const { return m_state; }
    void setState(int s);

signals:
    void blockIdChanged();
    void stateChanged();

private:
    void rebuild(); // 按 (m_blockId, m_state) 据 selectionAABBs 重画每 sub-AABB 的 6 面裂纹盒。

    int m_blockId = 0;
    int m_state = 0;
};

#endif // CRACKBOX_H
