#include "pickaxe.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <vector>

// 顶点：pos(3 float) = 12 字节（pos-only，同 UnitCube；颜色由 QML 的 PrincipledMaterial.baseColor 给，
// 不进顶点 —— 复用「自定义几何 + NoLighting + baseColor」已验证可见路径，规避 t75 要修的「贴图黑」根因）。
namespace {
struct PickVtx {
    float x, y, z;
};

// 轴对齐盒的 6 面 × 4 角，每角 = 符号三元组 (sx,sy,sz)（相对盒心的角点偏移方向）。
// CCW 朝外（与 CrackBox / BlockCube 同角点序，h=0.5 时即其常数表，此处泛化到符号以便任意半长）。
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
// base = 当前进度顶点数；索引以 base 为偏移写入。
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<PickVtx> &verts, std::vector<quint32> &idx)
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

PickaxeGeometry::PickaxeGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 木镐轮廓（局部坐标，几何中心 ≈ 原点；木柄竖直、镐头水平在顶 + 两端下勾）：
    //   - 木柄（竖直轴）：心 (0,-0.05,0)，半长 0.04×0.40×0.04 → y∈[-0.45,0.35]；
    //   - 镐头横梁：心 (0, 0.40,0)，半长 0.30×0.05×0.06 → y∈[0.35,0.45]，x∈[-0.30,0.30]；
    //   - 左下勾 / 右下勾：心 (±0.26, 0.31, 0)，半长 0.05×0.07×0.06 → 从横梁两端向外向下弯。
    // 总包围盒 x∈[-0.31,0.31] y∈[-0.45,0.45] z∈[-0.06,0.06]。消费点用 Model.scale/position/eulerRotation
    // 摆位（柄底 y≈-0.45 为握把，镐头 y≈+0.45 为顶）。
    std::vector<PickVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(4 * 24);
    idx.reserve(4 * 36);
    addBox(0.00f, -0.05f, 0.00f, 0.04f, 0.40f, 0.04f, verts, idx); // 木柄
    addBox(0.00f,  0.40f, 0.00f, 0.30f, 0.05f, 0.06f, verts, idx); // 镐头横梁
    addBox(-0.26f, 0.31f, 0.00f, 0.05f, 0.07f, 0.06f, verts, idx); // 左下勾
    addBox( 0.26f, 0.31f, 0.00f, 0.05f, 0.07f, 0.06f, verts, idx); // 右下勾

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    // 漏 update() 后端不上传 GPU；漏 setIndexData 则 idx 数组未用（-Wunused 警告）且
    // 索引永不进 GPU —— IndexSemantic 只声明布局，数据须 setIndexData 单独上传（同 BlockCube/CrackBox）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(PickVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32)))); // 144 索引独立上传
    setStride(int(sizeof(PickVtx)));
    setBounds(QVector3D(-0.31f, -0.45f, -0.06f), QVector3D(0.31f, 0.45f, 0.06f)); // 局部 AABB（配合 Model 变换给视锥剔除盒）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(PickVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
