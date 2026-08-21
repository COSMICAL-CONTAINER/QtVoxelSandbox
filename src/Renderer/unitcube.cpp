#include "unitcube.h"

#include <QByteArray>
#include <QVector3D>

// t757 贴图坐标补全：顶点布局从 pos(3) 扩为 pos(3)+uv(2)（stride 5 float）。**顶点位置与旧版逐字节一致**
// （72 顶点 = 12 三角形 ×2 正反绕序双面渲染，基准 ±0.5），既有纯色 Model 用户（材质无贴图）不读 UV →
// 行为不变；UnitCube+baseColorMap 用户（暗渊之眼 delegate / 夜行者眼层）从此每面铺整张贴图 [0,1]²。
// 根因（t757 白贴图排查）：旧版 UnitCube 只写 PositionSemantic，无任何 UV 属性 —— 贴图材质在无 UV 几何上
// 采样未定义（本工程里表现为材质回退纯白/丢贴图），全工程唯二 UnitCube+贴图用户（t727 夜行者眼层被
// baseColor 兜底色遮住、t729 暗渊之眼直接白块）均中招。修复 = 本类补 UV（引擎级根治，两层共用）。
//
// 每面 4 角 A,B,C,D 与旧版三角剖分一致（正绕序 (A,B,C)+(A,C,D) / 反绕序 (A,C,B)+(A,D,C)）；UV 取
// A(0,0) B(0,1) C(1,1) D(1,0)（面内全幅 [0,1]²，立方面取向对称、贴图方向不作要求）。
// 写入顺序（lessons-learned）：clear → setVertexData → setStride → setBounds
// → setPrimitiveType(Triangles) → addAttribute(Position/TexCoord0) → update()。漏 update() 后端不上传 GPU。
UnitCube::UnitCube(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr float h = 0.5f;
    // 6 面 × 4 角（A,B,C,D）位置：与旧版字面量逐项相同（+X/-X/+Y/-Y/+Z/-Z；CCW 朝外）。
    static const float kFaces[6][4][3] = {
        { { h, -h, -h }, { h, h, -h }, { h, h, h }, { h, -h, h } },    // +X
        { { -h, -h, h }, { -h, h, h }, { -h, h, -h }, { -h, -h, -h } }, // -X
        { { -h, h, -h }, { -h, h, h }, { h, h, h }, { h, h, -h } },    // +Y
        { { -h, -h, h }, { h, -h, h }, { h, -h, -h }, { -h, -h, -h } }, // -Y
        { { -h, -h, h }, { h, -h, h }, { h, h, h }, { -h, h, h } },    // +Z
        { { h, -h, -h }, { -h, -h, -h }, { -h, h, -h }, { h, h, -h } }, // -Z
    };
    // 面内 4 角 UV（A(0,0) B(0,1) C(1,1) D(1,0)）。
    static const float kCornerUV[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
    // 每面 2 三角 × 2 绕序 = 4 角序：正 (A,B,C,D) 取 (0,1,2)+(0,2,3)；反 (2,1,0)+(3,2,0)（CCW↔CW 互换）。
    static const int kTriCorners[4][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 2, 1, 0 }, { 3, 2, 0 } };

    float v[6 * 4 * 3 * 5]; // 72 顶点 × 5 float（pos3+uv2）
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        for (const auto &tri : kTriCorners) {
            for (int c : tri) {
                v[vi++] = kFaces[f][c][0];
                v[vi++] = kFaces[f][c][1];
                v[vi++] = kFaces[f][c][2];
                v[vi++] = kCornerUV[c][0];
                v[vi++] = kCornerUV[c][1];
            }
        }
    }

    clear();
    // QByteArray(const char*, int) 深拷贝；勿用 fromRawData（栈数组会悬空）。
    setVertexData(QByteArray(reinterpret_cast<const char *>(v), int(sizeof(v))));
    setStride(5 * int(sizeof(float)));
    setBounds(QVector3D(-h, -h, -h), QVector3D(h, h, h));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 3 * int(sizeof(float)), QQuick3DGeometry::Attribute::F32Type);
    update();
}
