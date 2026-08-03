#include "sword.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <vector>

// 顶点：pos(3 float) = 12 字节（pos-only，同 UnitCube / PickaxeGeometry / HoeGeometry / AxeGeometry / ShovelGeometry；
// 颜色由 QML 的 PrincipledMaterial.baseColor 给，不进顶点 —— 复用「自定义几何 + NoLighting + baseColor」已验证可见路径）。
namespace {
struct SwordVtx {
    float x, y, z;
};

// 轴对齐盒的 6 面 × 4 角，每角 = 符号三元组 (sx,sy,sz)（相对盒心的角点偏移方向）。
// CCW 朝外（与 PickaxeGeometry / HoeGeometry / AxeGeometry / ShovelGeometry / CrackBox / BlockCube 同角点序；与 Main.qml center 摆位自洽）。
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
// base = 当前进度顶点数；索引以 base 为偏移写入。（与 pickaxe / hoe / axe / shovel addBox 同实现，复制保同层同风格。）
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<SwordVtx> &verts, std::vector<quint32> &idx)
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

SwordGeometry::SwordGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 剑轮廓（局部坐标，纵向对称；刃在上半、柄在下半）：
    //   - 剑刃（纵向长刃，tier 金属色；刃尖朝 +Y）：心 (0, 0.10, 0)，半长 0.03×0.34×0.025 → y∈[-0.24,0.44]；
    //   - 刃尖（顶端收窄小方块，构成尖锋视觉）：心 (0, 0.42, 0)，半长 0.02×0.04×0.02 → y∈[0.38,0.46]；
    //   - 护手（横向短梁，刃与柄之间）：心 (0, -0.26, 0)，半长 0.10×0.02×0.03 → y∈[-0.28,-0.24]、x∈[-0.10,0.10]；
    //   - 剑柄（下半短柄）：心 (0, -0.34, 0)，半长 0.025×0.06×0.025 → y∈[-0.40,-0.28]；
    //   - 柄首（柄底防滑圆头小方块）：心 (0, -0.42, 0)，半长 0.035×0.03×0.035 → y∈[-0.45,-0.39]。
    // 总包围盒 x∈[-0.10,0.10] y∈[-0.45,0.46] z∈[-0.03,0.03]。消费点用 Model.scale/position/eulerRotation
    // 摆位（刃尖 y≈+0.46 朝上 / 前指、柄底 y≈-0.45 为握把）。
    std::vector<SwordVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(5 * 24);
    idx.reserve(5 * 36);
    addBox(0.00f,  0.10f, 0.00f, 0.030f, 0.34f, 0.025f, verts, idx); // 剑刃（纵向长刃）
    addBox(0.00f,  0.42f, 0.00f, 0.020f, 0.04f, 0.020f, verts, idx); // 刃尖（顶端收窄）
    addBox(0.00f, -0.26f, 0.00f, 0.100f, 0.02f, 0.030f, verts, idx); // 护手（横向短梁）
    addBox(0.00f, -0.34f, 0.00f, 0.025f, 0.06f, 0.025f, verts, idx); // 剑柄（下半短柄）
    addBox(0.00f, -0.42f, 0.00f, 0.035f, 0.03f, 0.035f, verts, idx); // 柄首（柄底圆头）

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(SwordVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32)))); // 180 索引独立上传
    setStride(int(sizeof(SwordVtx)));
    setBounds(QVector3D(-0.10f, -0.45f, -0.03f), QVector3D(0.10f, 0.46f, 0.03f)); // 局部 AABB（配合 Model 变换给视锥剔除盒）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(SwordVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
