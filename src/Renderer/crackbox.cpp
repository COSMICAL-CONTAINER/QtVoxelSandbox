#include "crackbox.h"

#include "blockregistry.h" // selectionAABBs（Core 数据，单一权威，与 SelectionWireBoxes 同源）

#include <QByteArray>
#include <QVector3D>
#include <QVector>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节。每盒每面 4 角独立顶点（24/盒），便于 per-face 全幅 UV。
// 6 面按 chunkgeometry.cpp kFaces 同序（+X -X +Y -Y +Z -Z），CCW 朝外。
namespace {
struct CrackVtx {
    float x, y, z;
    float u, v;
};

// t410 z-fight 外扩余量：每盒六面各外扩 kEps，使裂纹盒「略大于」被挖方块的 sub-AABB 表面（与方块面留缝）。
//   必须烘焙进几何而非靠 Model 统一 scale——异形 sub-AABB 不以方块中心对称（如下半砖盒顶恰在方块中线
//   y=0.5），统一 scale 对「贴近方块中心」的面几乎不移（scale×0≈0）→ 该面与方块面重合 z-fight 闪烁。
//   逐面外扩 kEps 则无论盒在格内何处，每面都恒定留 kEps 缝（机制同旧 1.005 scale 对整立方的 0.0025 余量）。
constexpr float kEps = 0.005f;

// 按 cell-local 盒体 [x0,x1]×[y0,y1]×[z0,z1] 推 6 面裂纹盒（先六面外扩 kEps，再 cell-local 减 0.5 居中 →
//   Model 摆 miningBlock + 0.5 = 方块中心时，完整方块盒 {0,0,0,1,1,1} 顶点恰 ±0.5 附近，与旧 CrackBox 同基准）。
//   每面 4 角从外看 CCW，附全幅 UV（0..1）使整张贴图铺满该面（薄面贴图被压缩，与 MC 一致）。
//   绕序 / UV 与旧 CrackBox 单立方逐字同源（t34 ±0.5 居中 / t35 setIndexData 契约一并保留）。
void pushCrackBox(QVector<CrackVtx> &verts, QVector<quint32> &idx,
                  float x0, float x1, float y0, float y1, float z0, float z1)
{
    // 六面外扩 kEps（z-fight 缝），再 cell-local 减 0.5 居中（Model 摆方块中心）。
    const float X0 = (x0 - kEps) - 0.5f, X1 = (x1 + kEps) - 0.5f;
    const float Y0 = (y0 - kEps) - 0.5f, Y1 = (y1 + kEps) - 0.5f;
    const float Z0 = (z0 - kEps) - 0.5f, Z1 = (z1 + kEps) - 0.5f;
    const quint32 base = quint32(verts.size());
    const CrackVtx v[24] = {
        // +X 面（外法线 +X，常数轴 x=X1；面内 y/z；uv=(z,y)→全幅）
        {X1, Y0, Z0, 0, 0}, {X1, Y1, Z0, 0, 1}, {X1, Y1, Z1, 1, 1}, {X1, Y0, Z1, 1, 0},
        // -X 面（外法线 -X，常数轴 x=X0）
        {X0, Y0, Z1, 0, 0}, {X0, Y1, Z1, 0, 1}, {X0, Y1, Z0, 1, 1}, {X0, Y0, Z0, 1, 0},
        // +Y 面（顶，外法线 +Y，常数轴 y=Y1；面内 x/z；uv=(x,z)）
        {X0, Y1, Z1, 0, 0}, {X1, Y1, Z1, 0, 1}, {X1, Y1, Z0, 1, 1}, {X0, Y1, Z0, 1, 0},
        // -Y 面（底，外法线 -Y，常数轴 y=Y0）
        {X0, Y0, Z0, 0, 0}, {X1, Y0, Z0, 0, 1}, {X1, Y0, Z1, 1, 1}, {X0, Y0, Z1, 1, 0},
        // +Z 面（外法线 +Z，常数轴 z=Z1；面内 x/y；uv=(x,y)）
        {X0, Y0, Z1, 0, 0}, {X1, Y0, Z1, 0, 1}, {X1, Y1, Z1, 1, 1}, {X0, Y1, Z1, 1, 0},
        // -Z 面（外法线 -Z，常数轴 z=Z0）
        {X1, Y0, Z0, 0, 0}, {X0, Y0, Z0, 0, 1}, {X0, Y1, Z0, 1, 1}, {X1, Y1, Z0, 1, 0},
    };
    for (int i = 0; i < 24; ++i) verts.append(v[i]);
    for (int f = 0; f < 6; ++f) { // 每面 2 三角形：(0,1,2)+(0,2,3)
        const quint32 b = base + quint32(f) * 4;
        idx.append(b + 0); idx.append(b + 1); idx.append(b + 2);
        idx.append(b + 0); idx.append(b + 2); idx.append(b + 3);
    }
}
} // namespace

CrackBox::CrackBox(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 默认 (air,0) → selectionAABBs 空 → 兜底全格立方（= 旧行为；QML 绑 blockId/state 后贴合形状）
}

void CrackBox::setBlockId(int id)
{
    if (id == m_blockId) return;
    m_blockId = id;
    emit blockIdChanged();
    rebuild();
}

void CrackBox::setState(int s)
{
    if (s == m_state) return;
    m_state = s;
    emit stateChanged();
    rebuild();
}

void CrackBox::rebuild()
{
    // 形状单一权威：selectionAABBs（与 SelectionWireBoxes 选中框同源数据）—— 完整→全格、不完整→贴合 sub-AABB。
    std::vector<BlockRegistry::BlockAABB> boxes =
        BlockRegistry::selectionAABBs(quint8(m_blockId), quint8(m_state));
    if (boxes.empty())
        boxes.push_back({0, 0, 0, 1, 1, 1}); // ShapeNone（火把 / cross 植物，瞬破且 selectionAABBs 空）兜底全格立方 = 旧行为

    QVector<CrackVtx> verts;
    QVector<quint32> idx;
    verts.reserve(int(boxes.size()) * 24);
    idx.reserve(int(boxes.size()) * 36);
    for (const BlockRegistry::BlockAABB &a : boxes)
        pushCrackBox(verts, idx, a.minX, a.maxX, a.minY, a.maxY, a.minZ, a.maxZ);

    // bounds：所有盒顶点的并（居中 + 外扩后）。影响 QtQuick3D 视锥剔除；裂纹随形状缩，bounds 准确无害。
    QVector3D bMin( 1e9f,  1e9f,  1e9f);
    QVector3D bMax(-1e9f, -1e9f, -1e9f);
    for (const CrackVtx &v : verts) {
        if (v.x < bMin.x()) bMin.setX(v.x);
        if (v.x > bMax.x()) bMax.setX(v.x);
        if (v.y < bMin.y()) bMin.setY(v.y);
        if (v.y > bMax.y()) bMax.setY(v.y);
        if (v.z < bMin.z()) bMin.setZ(v.z);
        if (v.z > bMax.z()) bMax.setZ(v.z);
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    // 漏 update() 后端不上传 GPU；漏 setIndexData 则 idx 数组未用（-Wunused 警告）且
    // 索引永不进 GPU —— IndexSemantic 只声明布局，数据须 setIndexData 单独上传（同 chunkgeometry）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.constData()),
                             verts.size() * int(sizeof(CrackVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.constData()),
                            idx.size() * int(sizeof(quint32)))); // 索引独立上传（每盒 36）
    setStride(int(sizeof(CrackVtx)));
    setBounds(bMin, bMax);
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(CrackVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(CrackVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
