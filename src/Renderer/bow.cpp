#include "bow.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <vector>

// 顶点：pos(3 float) = 12 字节（pos-only，同 SwordGeometry / PickaxeGeometry / ...；
// 颜色由 QML 的 PrincipledMaterial.baseColor 给，不进顶点 —— 复用「自定义几何 + NoLighting + baseColor」已验证可见路径）。
namespace {
struct BowVtx {
    float x, y, z;
};

// 轴对齐盒的 6 面 × 4 角，每角 = 符号三元组 (sx,sy,sz)（相对盒心的角点偏移方向）。
// CCW 朝外（与 SwordGeometry / PickaxeGeometry / ... / CrackBox / BlockCube 同角点序；与 Main.qml center 摆位自洽）。
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
// base = 当前进度顶点数；索引以 base 为偏移写入。（与 sword / pickaxe / ... addBox 同实现，复制保同层同风格。）
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<BowVtx> &verts, std::vector<quint32> &idx)
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

BowGeometry::BowGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 弓轮廓（局部坐标，垂直对称；弓臂在 XY 平面、belly 朝 +Z 弧形、弦在 -Z 后侧）：
    //   - 握把（中央手握处）：心 (0, 0, 0.04)，半长 0.025×0.05×0.025 → y∈[-0.05,0.05]、z∈[0.015,0.065]；
    //   - 上臂下段：心 (0, 0.18, 0.06)，半长 0.025×0.13×0.025 → y∈[0.05,0.31]；
    //   - 上臂尖（弧形前弯）：心 (0, 0.38, 0.09)，半长 0.025×0.08×0.025 → y∈[0.30,0.46]、z 前移表「弓臂弧」；
    //   - 下臂下段：心 (0, -0.18, 0.06)，半长 0.025×0.13×0.025 → y∈[-0.31,-0.05]（镜像上臂）；
    //   - 下臂尖：心 (0, -0.38, 0.09)，半长 0.025×0.08×0.025 → y∈[-0.46,-0.30]；
    //   - 弓弦（后侧细纵线 -Z）：心 (0, 0, -0.04)，半长 0.008×0.42×0.008 → y∈[-0.42,0.42]、z∈[-0.048,-0.032]。
    // 总包围盒 x∈[-0.025,0.025] y∈[-0.46,0.46] z∈[-0.048,0.115]。消费点用 Model.scale/position/eulerRotation
    // 摆位（垂直握持、弦朝玩家侧 / belly 朝前；弓拉弓动画由 viewModelHand 据 bowDrawProgress 后拉整把弓 + 手臂）。
    std::vector<BowVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(6 * 24);
    idx.reserve(6 * 36);
    addBox(0.00f,  0.00f, 0.04f, 0.025f, 0.05f, 0.025f, verts, idx); // 握把（中央）
    addBox(0.00f,  0.18f, 0.06f, 0.025f, 0.13f, 0.025f, verts, idx); // 上臂下段
    addBox(0.00f,  0.38f, 0.09f, 0.025f, 0.08f, 0.025f, verts, idx); // 上臂尖（弧形前弯）
    addBox(0.00f, -0.18f, 0.06f, 0.025f, 0.13f, 0.025f, verts, idx); // 下臂下段
    addBox(0.00f, -0.38f, 0.09f, 0.025f, 0.08f, 0.025f, verts, idx); // 下臂尖
    addBox(0.00f,  0.00f, -0.04f, 0.008f, 0.42f, 0.008f, verts, idx); // 弓弦（后侧细纵线）

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(BowVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32)))); // 216 索引独立上传
    setStride(int(sizeof(BowVtx)));
    setBounds(QVector3D(-0.025f, -0.46f, -0.048f), QVector3D(0.025f, 0.46f, 0.115f)); // 局部 AABB
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(BowVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
