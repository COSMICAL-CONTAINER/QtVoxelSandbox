#include "billboardquad.h"

#include <QByteArray>
#include <QVector3D>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节。4 角构成单四边形（XY 平面 z=0，跨度 ±0.5），
// 从 +Z 侧看 CCW（外法线 +Z）→ 默认 backface 剔除下从 +Z 侧可见（承载 Model 须令 +Z 指回相机）。
// UV 全 0..1：把 sourceItem（MaterialIcon Canvas）整张 2D 图完整铺到该面。
namespace {
struct QuadVtx {
    float x, y, z;
    float u, v;
};
} // namespace

BillboardQuad::BillboardQuad(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr float h = 0.5f;
    // 严格 ±0.5 居中（与 UnitCube/CrackBox/WireSquare 同基准）——Model 摆位用「方块中心」，
    // scale 同材料段 0.3。CCW 顺序 BL→BR→TR→TL（从 +Z 侧看），三角 (0,1,2)+(0,2,3) 外法线 +Z。
    const QuadVtx verts[4] = {
        {-h, -h, 0.0f, 0.0f, 0.0f}, // BL
        { h, -h, 0.0f, 1.0f, 0.0f}, // BR
        { h,  h, 0.0f, 1.0f, 1.0f}, // TR
        {-h,  h, 0.0f, 0.0f, 1.0f}, // TL
    };
    // 2 三角形：(0,1,2)+(0,2,3)。lessons-learned：addAttribute(IndexSemantic,…) 须配 setIndexData
    // 才真正上传索引（否则 idx 数组未用 → -Wunused 警告 + 退化非索引绘制）。同 CrackBox 契约。
    const quint32 idx[6] = {
        0, 1, 2,
        0, 2, 3,
    };

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts), int(sizeof(verts))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx), int(sizeof(idx)))); // 6 索引独立上传
    setStride(int(sizeof(QuadVtx)));
    setBounds(QVector3D(-h, -h, 0.0f), QVector3D(h, h, 0.0f)); // 局部 AABB（XY 平面薄板，z=0）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(QuadVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(QuadVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
