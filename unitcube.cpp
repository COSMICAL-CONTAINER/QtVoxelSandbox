#include "unitcube.h"

#include <QByteArray>
#include <QVector3D>

// 36 顶点（12 三角形），pos(3 float)，CCW 朝外（各面外法线朝外 → 默认 Backface 剔除下可见）。
// 跨度 ±0.5 = 1×1×1 居中原点（与 #Cube 同基准；Model.scale 复用原取值）。
// 写入顺序（lessons-learned）：clear → setVertexData → setStride → setBounds
// → setPrimitiveType(Triangles) → addAttribute(PositionSemantic) → update()。漏 update() 后端不上传 GPU。
UnitCube::UnitCube(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr float h = 0.5f;
    const float v[36 * 3] = {
        // +X 面（外法线 +X）
         h, -h, -h,   h,  h, -h,   h,  h,  h,
         h, -h, -h,   h,  h,  h,   h, -h,  h,
        // -X 面（外法线 -X）
        -h, -h,  h,  -h,  h,  h,  -h,  h, -h,
        -h, -h,  h,  -h,  h, -h,  -h, -h, -h,
        // +Y 面（顶，外法线 +Y）
        -h,  h, -h,  -h,  h,  h,   h,  h,  h,
        -h,  h, -h,   h,  h,  h,   h,  h, -h,
        // -Y 面（底，外法线 -Y）
        -h, -h,  h,   h, -h,  h,   h, -h, -h,
        -h, -h,  h,   h, -h, -h,  -h, -h, -h,
        // +Z 面（外法线 +Z）
        -h, -h,  h,   h, -h,  h,   h,  h,  h,
        -h, -h,  h,   h,  h,  h,  -h,  h,  h,
        // -Z 面（外法线 -Z）
         h, -h, -h,  -h, -h, -h,  -h,  h, -h,
         h, -h, -h,  -h,  h, -h,   h,  h, -h,
    };

    clear();
    // QByteArray(const char*, int) 深拷贝；勿用 fromRawData（栈数组会悬空）。
    setVertexData(QByteArray(reinterpret_cast<const char *>(v), int(sizeof(v))));
    setStride(3 * int(sizeof(float)));
    setBounds(QVector3D(-h, -h, -h), QVector3D(h, h, h));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    update();
}
