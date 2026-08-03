#include "axe.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <vector>

// 顶点：pos(3 float) = 12 字节（pos-only，同 UnitCube / PickaxeGeometry / HoeGeometry；颜色由 QML 的
// PrincipledMaterial.baseColor 给，不进顶点 —— 复用「自定义几何 + NoLighting + baseColor」已验证可见路径）。
namespace {
struct AxeVtx {
    float x, y, z;
};

// 轴对齐盒的 6 面 × 4 角，每角 = 符号三元组 (sx,sy,sz)（相对盒心的角点偏移方向）。
// CCW 朝外（与 PickaxeGeometry / HoeGeometry / CrackBox / BlockCube 同角点序；与 Main.qml center 摆位自洽）。
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
// base = 当前进度顶点数；索引以 base 为偏移写入。（与 pickaxe.cpp / hoe.cpp addBox 同实现，复制保同层同风格。）
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<AxeVtx> &verts, std::vector<quint32> &idx)
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

AxeGeometry::AxeGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 斧轮廓（局部坐标，几何中心 ≈ 原点；木柄竖直、斧刃在顶端单边向 +X 侧伸出）：
    //   - 木柄（竖直轴）：心 (0,-0.05,0)，半长 0.04×0.40×0.04 → y∈[-0.45,0.35]（同镐 / 锄柄，握把在底）；
    //   - 颈节（柄顶→刃的连接小方块）：心 (0.03,0.36,0)，半长 0.05×0.04×0.05 → y∈[0.32,0.40]；
    //   - 斧刃主块（厚重单边刃，向 +X 伸出）：心 (0.18,0.34,0)，半长 0.14×0.06×0.05 → y∈[0.28,0.40]、
    //     x∈[0.04,0.32]（单侧厚刃，区别于镐的双端对称下勾 / 锄的宽扁横刃）；
    //   - 刃口（右下弧形收窄）：心 (0.30,0.26,0)，半长 0.04×0.06×0.05 → y∈[0.20,0.32]、x∈[0.26,0.34]
    //     （砍切面朝下、远端略收尖）。
    // 总包围盒 x∈[-0.04,0.34] y∈[-0.45,0.40] z∈[-0.05,0.05]。消费点用 Model.scale/position/eulerRotation
    // 摆位（柄底 y≈-0.45 为握把，斧刃 y≈+0.34 在顶、向 +X 侧伸）。
    std::vector<AxeVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(4 * 24);
    idx.reserve(4 * 36);
    addBox(0.00f, -0.05f, 0.00f, 0.04f, 0.40f, 0.04f, verts, idx); // 木柄
    addBox(0.03f,  0.36f, 0.00f, 0.05f, 0.04f, 0.05f, verts, idx); // 颈节（柄→刃连接）
    addBox(0.18f,  0.34f, 0.00f, 0.14f, 0.06f, 0.05f, verts, idx); // 斧刃主块（单边厚刃）
    addBox(0.30f,  0.26f, 0.00f, 0.04f, 0.06f, 0.05f, verts, idx); // 刃口（右下收窄）

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    // 漏 update() 后端不上传 GPU；漏 setIndexData 则 idx 数组未用（-Wunused 警告）且
    // 索引永不进 GPU —— IndexSemantic 只声明布局，数据须 setIndexData 单独上传（同 BlockCube/CrackBox/Pickaxe/Hoe）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(AxeVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32)))); // 144 索引独立上传
    setStride(int(sizeof(AxeVtx)));
    setBounds(QVector3D(-0.04f, -0.45f, -0.05f), QVector3D(0.34f, 0.40f, 0.05f)); // 局部 AABB（配合 Model 变换给视锥剔除盒）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(AxeVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
