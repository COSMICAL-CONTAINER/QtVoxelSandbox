#include "mobbow.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32
#include <cmath>
#include <vector>

// 顶点：pos(3 float) = 12 字节（pos-only；颜色由 QML PrincipledMaterial.baseColor 给，同 BowGeometry）。
namespace {
struct BowVtx {
    float x, y, z;
};

// 6 面 × 4 角，符号三元组 (sx,sy,sz)（相对盒心角点偏移方向）；CCW 朝外（同 BowGeometry / mobmodel kFace）。
struct Sgn { int sx, sy, sz; };
const Sgn kFace[6][4] = {
    {{+1, -1, -1}, {+1, +1, -1}, {+1, +1, +1}, {+1, -1, +1}}, // +X（外法线 +X）
    {{-1, -1, +1}, {-1, +1, +1}, {-1, +1, -1}, {-1, -1, -1}}, // -X
    {{-1, +1, +1}, {+1, +1, +1}, {+1, +1, -1}, {-1, +1, -1}}, // +Y（顶）
    {{-1, -1, -1}, {+1, -1, -1}, {+1, -1, +1}, {-1, -1, +1}}, // -Y（底）
    {{-1, -1, +1}, {+1, -1, +1}, {+1, +1, +1}, {-1, +1, +1}}, // +Z
    {{+1, -1, -1}, {-1, -1, -1}, {-1, +1, -1}, {+1, +1, -1}}, // -Z
};

// 轴对齐盒：心 (cx,cy,cz)、半长 hx,hy,hz。每面 4 角 + 2 三角，24 顶点 / 36 索引（同 BowGeometry::addBox）。
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<BowVtx> &verts, std::vector<quint32> &idx)
{
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            verts.push_back({cx + float(s.sx) * hx, cy + float(s.sy) * hy, cz + float(s.sz) * hz});
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}

// 绕 (pivY,pivZ) 的 X 轴旋转盒（弓肢回弯用；与 mobmodel::addBoxRot 同旋转公式，仅 pos-only 无 uv / 无 bounds 累计）：
//   x 分量不变，y/z 在 Y-Z 平面绕 pivot 旋转。X 轴旋转（右手）：y' = pivY + dy·cos − dz·sin；z' = pivZ + dy·sin + dz·cos。
void addBoxRotX(float cx, float cy, float cz, float hx, float hy, float hz,
                float pivY, float pivZ, float angle,
                std::vector<BowVtx> &verts, std::vector<quint32> &idx)
{
    const float ca = std::cos(angle), sa = std::sin(angle);
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            const float lx = cx + float(s.sx) * hx;            // X 轴旋转 → x 不变
            const float ly = cy + float(s.sy) * hy;
            const float lz = cz + float(s.sz) * hz;
            const float dy = ly - pivY;
            const float dz = lz - pivZ;
            verts.push_back({lx, pivY + dy * ca - dz * sa, pivZ + dy * sa + dz * ca});
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}
} // namespace

MobBowGeometry::MobBowGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 drawAmount=0（松弦）建；QML 设 drawAmount 时再 rebuild
}

// t331 拉弓 setter：钳 0..1；值未变早退（非瞄准态 EntityManager 返回 0 → 不触发 rebuild）；变化则 rebuild。
void MobBowGeometry::setDrawAmount(float amount)
{
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    if (amount == m_drawAmount) return;
    m_drawAmount = amount;
    emit drawAmountChanged();
    rebuild();
}

// 弓体（持于右手前，垂直弧形；机制等价 MC 1.0 骷髅持弓，§9 区隔原创几何）—— 握把垂直立于原点，上/下肢
//   绕握把端向 +Z（朝射手 = 凹侧）回弯成 C 形；弓弦贴 +Z 侧。弓平面 Y-Z（X=0）；握把中心 = 原点 →
//   消费点 Model position 摆到右手（Main.qml 肩枢 Node 子节点，与右臂刚体同转）。
// t331 拉弓：m_drawAmount 驱动弦朝 +Z（射手方向）后移 kPull + 弓肢回弯角略增 kBowDraw（蓄力前凸感）。
//   弦 z 从 0.06（松）到 0.16（满拉）；肢回弯角从 0.50 到 0.62。
void MobBowGeometry::rebuild()
{
    constexpr float kTh = 0.025f;      // 弓体半厚（X/Z，瘦薄显弓）
    constexpr float kBowA = 0.50f;     // 弓肢静态回弯角（弧度 ≈ 29°；tip 朝射手偏移量）
    constexpr float kBowDraw = 0.12f;  // 满拉时肢角增量（弧度；肢前凸蓄力感）
    constexpr float kStringZ = 0.06f;  // 弦静态 Z（凹侧 = 射手侧 +Z）
    constexpr float kPull = 0.10f;     // 满拉弦后移量（+Z 朝射手）

    const float limbA = kBowA + m_drawAmount * kBowDraw;
    const float stringZ = kStringZ + m_drawAmount * kPull;

    std::vector<BowVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(4 * 24);
    idx.reserve(4 * 36);

    addBox(0.0f, 0.00f, 0.0f, kTh, 0.06f, kTh, verts, idx);                          // 握把（中段，原点）
    addBoxRotX(0.0f, 0.10f, 0.0f, kTh, 0.09f, kTh, 0.06f, 0.0f, +limbA, verts, idx);  // 上肢（绕 +Y 端向 +Z 回弯）
    addBoxRotX(0.0f, -0.10f, 0.0f, kTh, 0.09f, kTh, -0.06f, 0.0f, -limbA, verts, idx); // 下肢（绕 -Y 端向 +Z 回弯）
    addBox(0.0f, 0.00f, stringZ, 0.012f, 0.16f, 0.012f, verts, idx);                  // 弓弦（射手侧 +Z；拉弓后移）

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(BowVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(BowVtx)));
    setBounds(QVector3D(-0.03f, -0.22f, -0.04f), QVector3D(0.03f, 0.22f, 0.21f)); // 局部 AABB（含满拉弦 + 回弯肢范围）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(BowVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
