#include "glyphlines.h"

#include <QByteArray>
#include <QVector3D>

#include <array>

// t697 符文字迹几何（头注释见 .h）。8 条扁盒（两列 × 各 4 行），每盒 12 三角（CCW 朝外，单面即可 ——
// 薄片贴页面上方、仅上方可见）。行位 / 长度错落写死在表内（程序生成的「手写排布」，非对称）。
namespace {

struct BoxDef { float cx, cz, lenX, wZ; };

// 基础平面 ±0.5 内的两列字行（cx/cz = 条心，lenX/wZ = X 长 / Z 宽；Y 厚恒 0.02）。
constexpr std::array<BoxDef, 8> kLines = {{
    // 左列（Z ≈ -0.18 一列）
    { -0.22f, -0.34f, 0.26f, 0.06f },
    {  0.06f, -0.26f, 0.18f, 0.06f },
    { -0.18f, -0.16f, 0.30f, 0.06f },
    {  0.14f, -0.06f, 0.20f, 0.06f },
    // 右列（Z ≈ +0.14 一列）
    { -0.10f,  0.08f, 0.24f, 0.06f },
    {  0.18f,  0.16f, 0.16f, 0.06f },
    { -0.20f,  0.26f, 0.28f, 0.06f },
    {  0.08f,  0.34f, 0.18f, 0.06f },
}};

} // namespace

GlyphLines::GlyphLines(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 每盒 36 顶点（12 三角，同 UnitCube 单面正绕序写法）× 8 盒。
    const int kVertsPerBox = 36;
    std::array<float, kLines.size() * kVertsPerBox * 3> v{};
    int o = 0;
    for (const BoxDef &b : kLines) {
        const float hx = b.lenX * 0.5f;
        const float hz = b.wZ * 0.5f;
        const float hy = 0.01f;                 // Y 厚 0.02（半厚 0.01）
        const float x0 = b.cx - hx, x1 = b.cx + hx;
        const float z0 = b.cz - hz, z1 = b.cz + hz;
        const float y0 = -hy, y1 = hy;
        const float box[36 * 3] = {
            // +X 面
            x1, y0, z0,   x1, y1, z0,   x1, y1, z1,
            x1, y0, z0,   x1, y1, z1,   x1, y0, z1,
            // -X 面
            x0, y0, z1,   x0, y1, z1,   x0, y1, z0,
            x0, y0, z1,   x0, y1, z0,   x0, y0, z0,
            // +Y 面（朝上 —— 页面上观察的主面）
            x0, y1, z0,   x0, y1, z1,   x1, y1, z1,
            x0, y1, z0,   x1, y1, z1,   x1, y1, z0,
            // -Y 面
            x0, y0, z1,   x1, y0, z1,   x1, y0, z0,
            x0, y0, z1,   x1, y0, z0,   x0, y0, z0,
            // +Z 面
            x0, y0, z1,   x1, y0, z1,   x1, y1, z1,
            x0, y0, z1,   x1, y1, z1,   x0, y1, z1,
            // -Z 面
            x1, y0, z0,   x0, y0, z0,   x0, y1, z0,
            x1, y0, z0,   x0, y1, z0,   x1, y1, z0,
        };
        for (float f : box) v[size_t(o++)] = f;
    }

    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(v.data()), int(o * int(sizeof(float)))));
    setStride(3 * int(sizeof(float)));
    setBounds(QVector3D(-0.5f, -0.02f, -0.5f), QVector3D(0.5f, 0.02f, 0.5f));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    update();
}
