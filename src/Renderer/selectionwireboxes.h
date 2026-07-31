#ifndef SELECTIONWIREBOXES_H
#define SELECTIONWIREBOXES_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// t146 异形方块选中框线框（Renderer 视图）：按 BlockRegistry::selectionAABBs(blockId,state) 绘制每个
// sub-AABB 的 12 棱（Lines 模式），顶点以「cell 中心」为原点（aabb.min-0.5 .. aabb.max-0.5）。Model 摆到
// 命中方块中心（hitBlock + 0.5）→ 完整方块还原 WireCube ±0.5 观感；异形方块（slab/stairs/...）选中框
// 贴合实际形状（下半砖只画下半盒 12 棱），替代旧「不完整方块也画全格立方框」。
//
// 与 WireCube 的差异：WireCube 是静态 ±0.5 单立方（固定几何，每帧只改 Model 变换）；本类按 (blockId,
// state) **动态重建**几何（blockId/state Q_PROPERTY 改变 → rebuild()）。Main.qml 把 selectionBoxPartial
// Model 的 geometry 绑到此类型，blockId/state 绑 player 命中方块的 id/state → 命中方块变 / 编辑改 state
// 时自动重画选中框棱。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D + Core BlockRegistry），只读 BlockRegistry 数据产出
// 几何，不读栅格、不做选体逻辑（同 WireCube / UnitCube 模式）。向下依赖 Core，合规。
class SelectionWireBoxes : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SelectionWireBoxes)
    Q_PROPERTY(int blockId READ blockId WRITE setBlockId NOTIFY blockIdChanged)
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)

public:
    explicit SelectionWireBoxes(QQuick3DObject *parent = nullptr);

    int blockId() const { return m_blockId; }
    void setBlockId(int id);
    int state() const { return m_state; }
    void setState(int s);

signals:
    void blockIdChanged();
    void stateChanged();

private:
    void rebuild(); // 按 (m_blockId, m_state) 重画所有 sub-AABB 的 12 棱。

    int m_blockId = 0;
    int m_state = 0;
};

#endif // SELECTIONWIREBOXES_H
