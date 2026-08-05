#include "bow.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32
#include <cmath>

#include <vector>

// 顶点：pos(3 float) = 12 字节（pos-only，同 SwordGeometry / PickaxeGeometry / ...；
// 颜色由 QML 的 PrincipledMaterial.baseColor 给，不进顶点 —— 复用「自定义几何 + NoLighting + baseColor」已验证可见路径）。
namespace {
struct BowVtx {
    float x, y, z;
};

// 6 面 × 4 角，每角 = 符号三元组 (sx,sy,sz)（相对盒心的角点偏移方向）。
// CCW 朝外（与 SwordGeometry / PickaxeGeometry / ... / CrackBox / BlockCube 同角点序；自洽）。
// addBox：sx→X、sy→Y、sz→Z；addOrientedBox：sx→本地 u、sy→本地 v、sz→Z（u×v=+1 时与 X/Y/Z 同手性）。
struct Sgn { int sx, sy, sz; };
const Sgn kFace[6][4] = {
    {{+1, -1, -1}, {+1, +1, -1}, {+1, +1, +1}, {+1, -1, +1}}, // +X / +u（外法线 +X）
    {{-1, -1, +1}, {-1, +1, +1}, {-1, +1, -1}, {-1, -1, -1}}, // -X / -u
    {{-1, +1, +1}, {+1, +1, +1}, {+1, +1, -1}, {-1, +1, -1}}, // +Y / +v（顶）
    {{-1, -1, -1}, {+1, -1, -1}, {+1, -1, +1}, {-1, -1, +1}}, // -Y / -v（底）
    {{-1, -1, +1}, {+1, -1, +1}, {+1, +1, +1}, {-1, +1, +1}}, // +Z
    {{+1, -1, -1}, {-1, -1, -1}, {-1, +1, -1}, {+1, +1, -1}}, // -Z
};

// 轴对齐盒（弓弦用）：心 (cx,cy,cz)；半长 hx,hy,hz。每面 4 角 + 2 三角，24 顶点 / 36 索引。
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

// 朝向盒（弓臂弧段用）：心 (cx,cy,cz)；本地正交基 u/v（XY 平面内单位向量，需 u×v=+1 以保 CCW 朝外）；
// 半长 hu（沿 u 切向）/ hv（沿 v 径向）/ hz（Z 厚度）。角点 = 心 + sx*hu*u + sy*hv*v + sz*hz*ẑ。
// （u,v,Z 同手性 → kFace 缠绕与轴对齐盒一致朝外；弧段沿圆切向摆放 → 拼成平滑 C 弧。）
void addOrientedBox(float cx, float cy, float cz,
                    float ux, float uy, float vx, float vy,
                    float hu, float hv, float hz,
                    std::vector<BowVtx> &verts, std::vector<quint32> &idx)
{
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            const float du = float(s.sx) * hu; // 沿 u
            const float dv = float(s.sy) * hv; // 沿 v
            const float dz = float(s.sz) * hz; // 沿 Z
            verts.push_back({cx + du * ux + dv * vx,
                             cy + du * uy + dv * vy,
                             cz + dz});
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}
} // namespace

BowGeometry::BowGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 弓臂 C 形弧（XY 平面、凸侧朝 -X / 凹侧 +X，正面 ±Z 可见清晰 C 弯）：
    //   圆心 O=(Ox,0)、半径 R、参数 α∈[π-Θ, π+Θ]（α 从 +X 轴起算）。点 P(α)=(Ox+R·cosα, R·sinα)。
    //   α=π（握把）→ P=(-0.10, 0)（左凸顶点）；α=π±Θ（臂尖）→ P=(+0.06, ±0.40)。
    //   切向 u=(-sinα, cosα)（CCW）；径向 v=(-cosα, -sinα)（使 u×v=+1，缠绕朝外）。
    //   采样 N=9 段（含 α=π 中段），每段切向朝向盒（半长 0.07 邻段重叠、径向半宽 0.025、Z 半厚 0.02）。
    //   弦在凹侧 +X 连接两臂尖（见 BowStringGeometry）。机制等价 MC 1.0 弓 C 形 + 凹侧弦。
    constexpr float kOx = 0.4808f;
    constexpr float kR = 0.5808f;
    constexpr float kTheta = 0.7610f;      // ≈43.6°（半张角）
    constexpr int kN = 9;                  // 弧段数（奇数 → 含 α=π 握把中段）
    constexpr float kHalfLen = 0.07f;      // 段切向半长（>弧间距 → 邻段重叠无缝）
    constexpr float kHalfW = 0.025f;       // 臂径向半宽
    constexpr float kHalfD = 0.02f;        // Z 半厚（薄板：正面见清晰 C 弧，侧视仍有 0.04 厚不隐）
    constexpr float kPi = 3.14159265f;

    std::vector<BowVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(10 * 24);
    idx.reserve(10 * 36);

    const int half = (kN - 1) / 2;         // α=π + (k/half)·Θ，k∈[-half,+half]
    for (int k = -half; k <= half; ++k) {
        const float a = kPi + (float(k) / float(half)) * kTheta;
        const float ca = std::cos(a), sa = std::sin(a);
        const float px = kOx + kR * ca;
        const float py = kR * sa;
        const float ux = -sa, uy = ca;     // 切向（CCW）
        const float vx = -ca, vy = -sa;    // 径向（u×v = (-sa)(-sa)-(ca)(-ca)=sa²+ca²=+1）
        addOrientedBox(px, py, 0.0f, ux, uy, vx, vy, kHalfLen, kHalfW, kHalfD, verts, idx);
    }
    // 握把缠绳（α=π 处径向加粗短段，表「手握处」）：u=(0,-1) 竖直、v=(1,0)（u×v=+1）。
    addOrientedBox(-0.10f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
                   0.05f, 0.038f, kHalfD, verts, idx);

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(BowVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(BowVtx)));
    setBounds(QVector3D(-0.15f, -0.48f, -0.025f), QVector3D(0.14f, 0.48f, 0.025f)); // 局部 AABB（略放宽防误剔）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(BowVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}

BowStringGeometry::BowStringGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    // 弓弦：凹侧（+X）竖直细线，连接两臂尖。心 (0.06, 0, 0)（= 臂尖 X）、半长 0.008×0.42×0.015。
    //   Z 半厚 0.015 < 弓身 0.02 → 弦略嵌弓身薄板内、正面（±Z）观感弦贴弓面；中段（|y|<0.40）弦在弓身
    //   凹侧 +X 外侧无重叠 → 恒清晰可见为白色竖线（仅近臂尖处被弓身前面遮挡 = 弦「挂」在臂尖，符合直觉）。
    std::vector<BowVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(24);
    idx.reserve(36);
    addBox(0.06f, 0.0f, 0.0f, 0.008f, 0.42f, 0.015f, verts, idx);

    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(BowVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(BowVtx)));
    setBounds(QVector3D(0.04f, -0.44f, -0.02f), QVector3D(0.08f, 0.44f, 0.02f));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(BowVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
