#include "selectionwireboxes.h"

#include "blockregistry.h" // selectionAABBs（Core 数据，单一权威）

#include <QByteArray>
#include <QVector3D>
#include <QVector>

// t146 按 BlockRegistry::selectionAABBs(blockId,state) 画每个 sub-AABB 的 12 棱（Lines）。
//
// 几何约定（与 WireCube 同基准）：顶点以「cell 中心」为原点 —— cell-local AABB [min,max] 减 0.5 居中。
//   Model 摆到命中方块中心（hitBlock + 0.5）→ 完整方块 AABB {0,0,0,1,1,1} 顶点恰为 ±0.5（与 WireCube 观感
//   一致）；下半砖 AABB {0,0,0,1,0.5,1} 顶点 y ∈ [-0.5, 0]（只画下半盒 12 棱）。
//
// 每盒 8 角（按 (sx,sy,sz) ∈ {min,max}^3 组合，与 WireCube 同序）+ 12 棱（z=- 面 4 + z=+ 面 4 + 纵向 4）。
//   多盒（如 stairs = 下步 + 背墙）顺序追加 → 一份 Lines 几何含所有盒的棱。
//
// 写入顺序（lessons-learned）：clear → setVertexData → setStride → setBounds
// → setPrimitiveType(Lines) → addAttribute(PositionSemantic) → update()。漏 update() 后端不上传 GPU。
SelectionWireBoxes::SelectionWireBoxes(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 默认 (air,0) → selectionAABBs 空 → 空几何（Model.visible 由 QML 控制，无棱可画）
}

void SelectionWireBoxes::setBlockId(int id)
{
    if (id == m_blockId) return;
    m_blockId = id;
    emit blockIdChanged();
    rebuild();
}

void SelectionWireBoxes::setState(int s)
{
    if (s == m_state) return;
    m_state = s;
    emit stateChanged();
    rebuild();
}

void SelectionWireBoxes::rebuild()
{
    const std::vector<BlockRegistry::BlockAABB> boxes =
        BlockRegistry::selectionAABBs(quint8(m_blockId), quint8(m_state));

    if (boxes.empty()) {
        clear(); // air / torch / 未知 → 无棱（air 走 Main.qml hasHit 门控隐藏；torch 走 isTorch 分支）
        update();
        return;
    }

    // 12 棱端点对（顶点角索引）。角序与 WireCube 一致：
    //   0:(min,min,min) 1:(max,min,min) 2:(max,max,min) 3:(min,max,min)
    //   4:(min,min,max) 5:(max,min,max) 6:(max,max,max) 7:(min,max,max)
    static const int kEdges[12 * 2] = {
        0, 1,  1, 2,  2, 3,  3, 0, // z=min 面 4 棱
        4, 5,  5, 6,  6, 7,  7, 4, // z=max 面 4 棱
        0, 4,  1, 5,  2, 6,  3, 7, // 纵向 4 棱
    };

    // 角索引 → (axis=0/1/2) 的坐标：bit0=+x、bit1=+y、bit2=+z（与 kEdges 角序对齐）。减 0.5 居中。
    auto cornerCoord = [](const BlockRegistry::BlockAABB &a, int cornerIdx, int axis) -> float {
        const bool px = (cornerIdx & 1) != 0;
        const bool py = (cornerIdx & 2) != 0;
        const bool pz = (cornerIdx & 4) != 0;
        float v;
        if (axis == 0)      v = px ? a.maxX : a.minX;
        else if (axis == 1) v = py ? a.maxY : a.minY;
        else                v = pz ? a.maxZ : a.minZ;
        return v - 0.5f; // cell 中心居中（Model 摆 hitBlock + 0.5）
    };

    QVector<float> verts;
    verts.reserve(int(boxes.size()) * 24 * 3);
    QVector3D bMin( 1e9f,  1e9f,  1e9f);
    QVector3D bMax(-1e9f, -1e9f, -1e9f);
    for (const BlockRegistry::BlockAABB &a : boxes) {
        for (int i = 0; i < 24; ++i) { // 12 棱 × 2 端点
            const int ci = kEdges[i];
            const float vx = cornerCoord(a, ci, 0);
            const float vy = cornerCoord(a, ci, 1);
            const float vz = cornerCoord(a, ci, 2);
            verts.append(vx); verts.append(vy); verts.append(vz);
            if (vx < bMin.x()) bMin.setX(vx); if (vx > bMax.x()) bMax.setX(vx);
            if (vy < bMin.y()) bMin.setY(vy); if (vy > bMax.y()) bMax.setY(vy);
            if (vz < bMin.z()) bMin.setZ(vz); if (vz > bMax.z()) bMax.setZ(vz);
        }
    }

    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.constData()),
                             verts.size() * int(sizeof(float))));
    setStride(3 * int(sizeof(float)));
    setBounds(bMin, bMax); // 影响 QtQuick3D 视锥剔除（选中框极小，bounds 准确无害）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    update();
}
