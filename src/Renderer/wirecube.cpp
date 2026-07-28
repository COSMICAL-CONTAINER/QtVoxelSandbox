#include "wirecube.h"

#include <QByteArray>
#include <QVector3D>

// 12 条棱 × 2 端点 = 24 顶点（pos(3 float)）。用 Lines（独立线段，每 2 顶点一段）而非 LineStrip：
// LineStrip 把每对相邻顶点都连起来，无法表达「立方体棱的跳跃」（棱端点不连续连接）；
// Lines 则每 2 顶点独立成段，正好对应 12 条棱。
//
// 8 角顶点（±0.5 居中，与 UnitCube/BlockCube 同基准，Model 摆位用「中心 = pos」自洽）：
//   0:(-,-,-) 1:(+,-,-) 2:(+,+,-) 3:(-,+,-)
//   4:(-,-,+) 5:(+,-,+) 6:(+,+,+) 7:(-,+,+)
// 棱：z=- 面 4 条 + z=+ 面 4 条 + 4 条纵向连接 = 12。
//
// 写入顺序（lessons-learned）：clear → setVertexData → setStride → setBounds
// → setPrimitiveType(Lines) → addAttribute(PositionSemantic) → update()。漏 update() 后端不上传 GPU。
WireCube::WireCube(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr float h = 0.5f;
    // 8 角（按位标 (sx,sy,sz) ∈ {-h,+h} 各轴组合）。
    const float c[8 * 3] = {
        -h, -h, -h,  // 0
         h, -h, -h,  // 1
         h,  h, -h,  // 2
        -h,  h, -h,  // 3
        -h, -h,  h,  // 4
         h, -h,  h,  // 5
         h,  h,  h,  // 6
        -h,  h,  h,  // 7
    };
    // 12 棱端点对（顶点索引）。z=- 面：0-1,1-2,2-3,3-0；z=+ 面：4-5,5-6,6-7,7-4；纵向：0-4,1-5,2-6,3-7。
    const int e[12 * 2] = {
        0, 1,  1, 2,  2, 3,  3, 0,  // z=- 面
        4, 5,  5, 6,  6, 7,  7, 4,  // z=+ 面
        0, 4,  1, 5,  2, 6,  3, 7,  // 纵向连接
    };
    // 展开为 24 顶点的 pos 数组（每棱 2 端点各拷一次，端点共享棱会重复——Lines 模式下正常）。
    float verts[24 * 3];
    for (int i = 0; i < 24; ++i) {
        const int vi = e[i];
        verts[i * 3 + 0] = c[vi * 3 + 0];
        verts[i * 3 + 1] = c[vi * 3 + 1];
        verts[i * 3 + 2] = c[vi * 3 + 2];
    }

    clear();
    // QByteArray(const char*, int) 深拷贝；勿用 fromRawData（栈数组会悬空）。
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts), int(sizeof(verts))));
    setStride(3 * int(sizeof(float)));
    setBounds(QVector3D(-h, -h, -h), QVector3D(h, h, h));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    update();
}
