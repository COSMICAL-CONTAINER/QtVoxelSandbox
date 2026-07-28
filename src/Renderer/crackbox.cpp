#include "crackbox.h"

#include <QByteArray>
#include <QVector3D>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节。每面 4 角独立顶点（24 总），便于 per-face 全幅 UV。
// 6 面按 chunkgeometry.cpp kFaces 同序（+X -X +Y -Y +Z -Z），CCW 朝外。
namespace {
struct CrackVtx {
    float x, y, z;
    float u, v;
};
} // namespace

CrackBox::CrackBox(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr float h = 0.5f;
    // 严格 ±0.5 居中（与 UnitCube/WireSquare 同基准）——Model 摆位用「方块中心 = miningBlock+0.5」
    // 才能覆盖整格。每面 4 角从外侧看 CCW，附全幅 UV（0..1）使整张贴图铺满该面。
    // (u,v) 与该面两个轴向对齐：±X 面 uv=(z,y)；±Y 面 uv=(x,z)；±Z 面 uv=(x,y)。
    // 各对面在常数轴上符号相反（+面常数=+h、−面常数=−h），面内两轴取 ±h → 闭合 1×1×1 立方体。
    const CrackVtx verts[24] = {
        // +X 面（外法线 +X，常数轴 x=+h）
        { h, -h, -h,  0, 0}, { h,  h, -h,  0, 1}, { h,  h,  h,  1, 1}, { h, -h,  h,  1, 0},
        // -X 面（外法线 -X，常数轴 x=-h）
        {-h, -h,  h,  0, 0}, {-h,  h,  h,  0, 1}, {-h,  h, -h,  1, 1}, {-h, -h, -h,  1, 0},
        // +Y 面（顶，外法线 +Y，常数轴 y=+h）
        {-h,  h,  h,  0, 0}, { h,  h,  h,  0, 1}, { h,  h, -h,  1, 1}, {-h,  h, -h,  1, 0},
        // -Y 面（底，外法线 -Y，常数轴 y=-h）
        {-h, -h, -h,  0, 0}, { h, -h, -h,  0, 1}, { h, -h,  h,  1, 1}, {-h, -h,  h,  1, 0},
        // +Z 面（外法线 +Z，常数轴 z=+h）
        {-h, -h,  h,  0, 0}, { h, -h,  h,  0, 1}, { h,  h,  h,  1, 1}, {-h,  h,  h,  1, 0},
        // -Z 面（外法线 -Z，常数轴 z=-h）
        { h, -h, -h,  0, 0}, {-h, -h, -h,  0, 1}, {-h,  h, -h,  1, 1}, { h,  h, -h,  1, 0},
    };
    // 每面 2 三角形：(0,1,2)+(0,2,3)，6 面 = 12 三角形 = 36 索引。
    const quint32 idx[36] = {
         0, 1, 2,  0, 2, 3,   // +X
         4, 5, 6,  4, 6, 7,   // -X
         8, 9,10,  8,10,11,   // +Y
        12,13,14, 12,14,15,   // -Y
        16,17,18, 16,18,19,   // +Z
        20,21,22, 20,22,23,   // -Z
    };

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    // 漏 update() 后端不上传 GPU；漏 setIndexData 则 idx 数组未用（-Wunused 警告）且
    // 36 索引永不进 GPU —— IndexSemantic 只声明布局，数据须 setIndexData 单独上传（同 chunkgeometry）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts), int(sizeof(verts))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx), int(sizeof(idx)))); // 36 索引独立上传
    setStride(int(sizeof(CrackVtx)));
    setBounds(QVector3D(-h, -h, -h), QVector3D(h, h, h)); // 局部 AABB = ±0.5（与几何一致，配合 Model 摆位 + 微放大）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(CrackVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(CrackVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
