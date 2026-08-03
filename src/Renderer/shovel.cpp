#include "shovel.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <vector>

// 顶点：pos(3 float) = 12 字节（pos-only，同 UnitCube / PickaxeGeometry / HoeGeometry / AxeGeometry；
// 颜色由 QML 的 PrincipledMaterial.baseColor 给，不进顶点 —— 复用「自定义几何 + NoLighting + baseColor」已验证可见路径）。
namespace {
struct ShovelVtx {
    float x, y, z;
};

// 轴对齐盒的 6 面 × 4 角，每角 = 符号三元组 (sx,sy,sz)（相对盒心的角点偏移方向）。
// CCW 朝外（与 PickaxeGeometry / HoeGeometry / AxeGeometry / CrackBox / BlockCube 同角点序；与 Main.qml center 摆位自洽）。
struct Sgn { int sx, sy, sz; };
const Sgn kFace[6][4] = {
    {{+1, -1, -1}, {+1, +1, -1}, {+1, +1, +1}, {+1, -1, +1}}, // +X（外法线 +X）
    {{-1, -1, +1}, {-1, +1, +1}, {-1, +1, -1}, {-1, -1, -1}}, // -X
    {{-1, +1, +1}, {+1, +1, +1}, {+1, +1, -1}, {-1, +1, -1}}, // +Y（顶）
    {{-1, -1, -1}, {+1, -1, -1}, {+1, -1, +1}, {-1, -1, +1}}, // -Y（底）
    {{-1, -1, +1}, {+1, -1, +1}, {+1, +1, +1}, {-1, +1, +1}}, // +Z
    {{+1, -1, -1}, {-1, -1, -1}, {-1, +1, -1}, {+1, +1, -1}}, // -Z
};

// 追加一个轴对齐盒（心 cx,cy,cz；半长 hx,hy,hz）到 verts/idx。每面 4 角 + 2 三角，24 顶点 / 36 索引。
// base = 当前进度顶点数；索引以 base 为偏移写入。（与 pickaxe / hoe / axe addBox 同实现，复制保同层同风格。）
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<ShovelVtx> &verts, std::vector<quint32> &idx)
{
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            verts.push_back({cx + s.sx * hx, cy + s.sy * hy, cz + s.sz * hz});
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}
} // namespace

ShovelGeometry::ShovelGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 铲轮廓（局部坐标，几何中心 ≈ 原点；木柄竖直、铲斗在顶端）：
    //   - 木柄（竖直轴）：心 (0,-0.05,0)，半长 0.04×0.40×0.04 → y∈[-0.45,0.35]（同镐 / 锄 / 斧柄，握把在底）；
    //   - 颈节（柄顶→铲斗的连接小方块）：心 (0,0.36,0)，半长 0.05×0.04×0.05 → y∈[0.32,0.40]；
    //   - 铲斗（方形扁斗，顶端略宽底略窄的掘土容器）：心 (0,0.30,0)，半长 0.14×0.08×0.05 → y∈[0.22,0.38]、
    //     x∈[-0.14,0.14]（方形铲头，平直底刃口掘土，区别于斧的弧形单边刃 / 锄的尖勾）。
    // 总包围盒 x∈[-0.14,0.14] y∈[-0.45,0.40] z∈[-0.05,0.05]。消费点用 Model.scale/position/eulerRotation
    // 摆位（柄底 y≈-0.45 为握把，铲斗 y≈+0.30 在顶、方斗形）。
    std::vector<ShovelVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(3 * 24);
    idx.reserve(3 * 36);
    addBox(0.00f, -0.05f, 0.00f, 0.04f, 0.40f, 0.04f, verts, idx); // 木柄
    addBox(0.00f,  0.36f, 0.00f, 0.05f, 0.04f, 0.05f, verts, idx); // 颈节（柄→铲斗连接）
    addBox(0.00f,  0.30f, 0.00f, 0.14f, 0.08f, 0.05f, verts, idx); // 铲斗（方形扁斗）

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(ShovelVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32)))); // 108 索引独立上传
    setStride(int(sizeof(ShovelVtx)));
    setBounds(QVector3D(-0.14f, -0.45f, -0.05f), QVector3D(0.14f, 0.40f, 0.05f)); // 局部 AABB（配合 Model 变换给视锥剔除盒）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(ShovelVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
