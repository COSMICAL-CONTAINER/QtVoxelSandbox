#include "mobmodel.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <algorithm> // std::min/max
#include <vector>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节。每面铺整张贴图 [0,1]×[0,1]（全脸 UV，同 CrackBox）。
namespace {
struct MobVtx {
    float x, y, z;
    float u, v;
};

// 6 面角点（与 blockcube.cpp kFaceCorners 同序：+X -X +Y -Y +Z -Z）。每面 4 角从外侧看 CCW（叉积 = 外法线，
// 默认 backface 剔除下可见）。角点用符号三元组 (sx,sy,sz) ∈ {-1,+1} 表达 → 由 addBox 据盒心 + 半长缩放。
// (u,v) ∈ {0,1}² 全脸铺贴图（与 CrackBox 同 UV 方案；与 blockcube cu/cv 同映射，但 u0=0/u1=1 整张非子区）。
// 推导自 blockcube kFaceCorners（kH→+1、-kH→-1），保绕序 / 法线一致 → 与 Main.qml center 摆位自洽。
struct Sgn { int sx, sy, sz; float u, v; };
const Sgn kFace[6][4] = {
    // +X（外法线 +X）
    {{+1, -1, -1, 0, 0}, {+1, +1, -1, 0, 1}, {+1, +1, +1, 1, 1}, {+1, -1, +1, 1, 0}},
    // -X
    {{-1, -1, +1, 1, 0}, {-1, +1, +1, 1, 1}, {-1, +1, -1, 0, 1}, {-1, -1, -1, 0, 0}},
    // +Y（顶）
    {{-1, +1, +1, 0, 1}, {+1, +1, +1, 1, 1}, {+1, +1, -1, 1, 0}, {-1, +1, -1, 0, 0}},
    // -Y（底）
    {{-1, -1, -1, 0, 0}, {+1, -1, -1, 1, 0}, {+1, -1, +1, 1, 1}, {-1, -1, +1, 0, 1}},
    // +Z
    {{-1, -1, +1, 0, 0}, {+1, -1, +1, 1, 0}, {+1, +1, +1, 1, 1}, {-1, +1, +1, 0, 1}},
    // -Z（前；头朝此向）
    {{+1, -1, -1, 1, 0}, {-1, -1, -1, 0, 0}, {-1, +1, -1, 0, 1}, {+1, +1, -1, 1, 1}},
};

// 追加一个轴对齐盒（心 cx,cy,cz；半长 hx,hy,hz）到 verts/idx，全脸 UV。每面 4 角 + 2 三角，24 顶点 / 36 索引。
// base = 当前进度顶点数；索引以 base 为偏移写入。（与 hoe.cpp / pickaxe.cpp addBox 同实现思路，仅多 uv 通道。）
// 同时累计 bMin/bMax（局部 AABB，供 setBounds 给视锥剔除盒）。
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<MobVtx> &verts, std::vector<quint32> &idx,
            QVector3D &bMin, QVector3D &bMax)
{
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            MobVtx vt;
            vt.x = cx + float(s.sx) * hx;
            vt.y = cy + float(s.sy) * hy;
            vt.z = cz + float(s.sz) * hz;
            vt.u = s.u;
            vt.v = s.v;
            verts.push_back(vt);
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
    // 累计局部 AABB（盒 8 角的最小 / 最大）。
    bMin.setX(std::min(bMin.x(), cx - hx));
    bMin.setY(std::min(bMin.y(), cy - hy));
    bMin.setZ(std::min(bMin.z(), cz - hz));
    bMax.setX(std::max(bMax.x(), cx + hx));
    bMax.setY(std::max(bMax.y(), cy + hy));
    bMax.setZ(std::max(bMax.z(), cz + hz));
}

// 四条腿对称放置（前 / 后两对）。legHy = 腿半高；legY = 腿心 y；legOffX/Z = 腿心相对躯干中心的水平偏移。
// 腿粗细 legW（半宽）。复用于三种 mob（仅参数不同）。
void addLegs(float legY, float legHy, float legOffX, float legOffZ, float legW,
             std::vector<MobVtx> &verts, std::vector<quint32> &idx,
             QVector3D &bMin, QVector3D &bMax)
{
    addBox(-legOffX, legY, -legOffZ, legW, legHy, legW, verts, idx, bMin, bMax); // 前左
    addBox( legOffX, legY, -legOffZ, legW, legHy, legW, verts, idx, bMin, bMax); // 前右
    addBox(-legOffX, legY,  legOffZ, legW, legHy, legW, verts, idx, bMin, bMax); // 后左
    addBox( legOffX, legY,  legOffZ, legW, legHy, legW, verts, idx, bMin, bMax); // 后右
}
} // namespace

MobModel::MobModel(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 mobType=Pig 建；QML 设 mobType 时再 rebuild 到正确类型
}

void MobModel::setMobType(int type)
{
    // 0（测试生物）/ 越界 → 兜底 Pig（保几何非空、bounds 合法；Main.qml 对 mobType 0 仍走 UnitCube，
    //   不进本类，故此处兜底仅防误设）。
    if (type != 1 && type != 2 && type != 3) type = 1;
    if (type == m_mobType) return;
    m_mobType = type;
    emit mobTypeChanged();
    rebuild();
}

// 按 m_mobType 选比例建「躯干 + 头 + 4 腿（+ 牛角）」多盒几何。局部原点 = 躯干中心；头朝 -Z（前）。
// 比例经手调使每种 mob 在 ~1×1×1 碰撞立方（EntityManager radius=0.5）内可辨：
//   - 猪：紧凑低矮、短腿、大头；   - 牛：高大长身 + 头顶两小角盒；  - 羊：圆胖躯干、小头、短腿。
// 全脸 UV → 各盒铺同张贴图；QML 据 mobType 选 mob_pig / mob_cow / mob_sheep。
void MobModel::rebuild()
{
    std::vector<MobVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(8 * 24); // 至多躯干+头+4腿+2角 = 8 盒
    idx.reserve(8 * 36);
    QVector3D bMin(1e9f, 1e9f, 1e9f), bMax(-1e9f, -1e9f, -1e9f);

    if (m_mobType == 2) {
        // 牛：高大长身 + 头顶两小角盒。机制等价 MC 牛形态（非名词照搬）。
        addBox(0.00f, 0.05f, 0.00f, 0.32f, 0.28f, 0.55f, verts, idx, bMin, bMax); // 躯干（长）
        addBox(0.00f, 0.15f, -0.60f, 0.20f, 0.22f, 0.20f, verts, idx, bMin, bMax); // 头（前伸）
        addBox(-0.22f, 0.34f, -0.58f, 0.05f, 0.06f, 0.05f, verts, idx, bMin, bMax); // 左角
        addBox( 0.22f, 0.34f, -0.58f, 0.05f, 0.06f, 0.05f, verts, idx, bMin, bMax); // 右角
        addLegs(-0.30f, 0.20f, 0.20f, 0.35f, 0.10f, verts, idx, bMin, bMax); // 4 长腿
    } else if (m_mobType == 3) {
        // 羊：圆胖躯干、小头、短腿。机制等价 MC 羊形态（非名词照搬）。
        addBox(0.00f, 0.05f, 0.00f, 0.30f, 0.28f, 0.42f, verts, idx, bMin, bMax); // 躯干（圆胖）
        addBox(0.00f, 0.10f, -0.45f, 0.14f, 0.16f, 0.16f, verts, idx, bMin, bMax); // 小头
        addLegs(-0.28f, 0.16f, 0.18f, 0.26f, 0.09f, verts, idx, bMin, bMax); // 4 短腿
    } else {
        // 猪（默认 / 兜底）：紧凑低矮、短腿、大头。机制等价 MC 猪形态（非名词照搬）。
        addBox(0.00f, 0.00f, 0.00f, 0.35f, 0.22f, 0.45f, verts, idx, bMin, bMax); // 躯干（低矮）
        addBox(0.00f, 0.05f, -0.50f, 0.22f, 0.22f, 0.18f, verts, idx, bMin, bMax); // 大头（前伸）
        addLegs(-0.30f, 0.18f, 0.22f, 0.28f, 0.10f, verts, idx, bMin, bMax); // 4 短腿
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    // 漏 update() 后端不上传 GPU；漏 setIndexData 则 idx 数组未用（-Wunused 警告）且
    // 索引永不进 GPU —— IndexSemantic 只声明布局，数据须 setIndexData 单独上传（同 BlockCube/Hoe/Pickaxe）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(MobVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(MobVtx)));
    setBounds(bMin, bMax); // 局部 AABB（配合 Model 变换给视锥剔除盒）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(MobVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(MobVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
