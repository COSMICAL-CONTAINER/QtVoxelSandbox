#include "wiresquare.h"

#include <QByteArray>
#include <QVector3D>

// 顶点：pos(3 float)。5 顶点闭合线带（XY 平面 z=0，跨度 ±0.5 = 恰好覆盖 1×1 方块面）。
WireSquare::WireSquare(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr float h = 0.5f;
    const float verts[5 * 3] = {
        -h, -h, 0.0f,
         h, -h, 0.0f,
         h,  h, 0.0f,
        -h,  h, 0.0f,
        -h, -h, 0.0f, // 闭合回起点
    };

    // 写入顺序（lessons-learned）：clear → setVertexData → setStride → setBounds
    // → setPrimitiveType → addAttribute → update()。漏 update() 后端不上传 GPU。
    clear();
    // QByteArray(const char*, int) 深拷贝；勿用 fromRawData（栈数组会悬空）。
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts), int(sizeof(verts))));
    setStride(3 * int(sizeof(float)));
    setBounds(QVector3D(-h, -h, 0.0f), QVector3D(h, h, 0.0f));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::LineStrip);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    update();
}
