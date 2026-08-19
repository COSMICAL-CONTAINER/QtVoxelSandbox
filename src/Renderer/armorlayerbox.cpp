#include "armorlayerbox.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <array>

// t718 盔甲 layer 薄壳盒（±0.5 居中，同 UnitCube 基准 → Main.qml 既有护甲 Model 的 position/scale 沿用）。
//
// 六面 UV（MC armor layer box-UV，与 mobmodel.cpp R19 C3 同公式同坐标系换算，两处已验证约定）：
//   - MC ModelRenderer.addBox 自动 UV 6 面像素矩形（面序 0=+X右 1=-X左 2=+Y顶 3=-Y底 4=+Z前 5=-Z后；
//     从 textureOffset (u0,v0) 起：顶/底 w×d 在上、侧带 d|w|d|w 在 v0+d 起 h 高、背在最右）。
//   - 本工程左手系（+X 右 / +Y 上 / -Z 前）与 MC 右手系（+Z 前）水平差 180° → 面 remap +X↔-X、+Z↔-Z
//     （kMcFace），使本工程 -Z 前（脸）采 MC +Z Front 区（MC 贴图脸在 +Z Front，mobmodel.cpp 像素实测）。
//   - MC v 向下增 vs Qt 图像顶↔v=1 → v 翻（mcToQtV），umin/vmin 交换大小关系。
// base 64×32（HD pack layer 是 base 整数倍 → UV 分数不变，无需感知贴图实际尺寸）。
namespace {

// MC 像素 → Qt UV（同 mobmodel.cpp mcToQtU/mcToQtV；base 64×32 钉死）。
constexpr float kTexW = 64.0f, kTexH = 32.0f;
inline float mcU(float px) { return px / kTexW; }
inline float mcV(float py) { return 1.0f - py / kTexH; }

// 6 面角点（同 mobmodel.cpp kFace：每面 4 角从外侧看 CCW；(u,v)∈{0,1}² 是面内角点归一化坐标，
//   UV 子区按它插值）。pos 由 ±0.5 符号缩放。
struct Sgn { int sx, sy, sz; float u, v; };
constexpr Sgn kFace[6][4] = {
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
    // -Z（前；脸朝此向）
    {{+1, -1, -1, 1, 0}, {-1, -1, -1, 0, 0}, {-1, +1, -1, 0, 1}, {+1, +1, -1, 1, 1}},
};

// 本工程 kFace 面 → MC 面（水平 180° remap：+X↔-X、+Z↔-Z；同 mobmodel.cpp kMcFace）。
constexpr int kMcFace[6] = { 1, 0, 2, 3, 5, 4 };

// t718 盔甲部位盒区表（index = piece；MC textureOffset + size，见 armorlayerbox.h 注释出处）。
//   行：{ u0, v0, w, h, d }。
struct BoxTex { float u0, v0, w, h, d; };
constexpr std::array<BoxTex, 6> kPieces = {{
    {  0.0f,  0.0f, 8.0f,  8.0f, 8.0f }, // 0 Helmet（layer_1）
    { 16.0f, 16.0f, 8.0f, 12.0f, 4.0f }, // 1 Chest（layer_1）
    { 40.0f, 16.0f, 4.0f, 12.0f, 4.0f }, // 2 Sleeve（layer_1；左右臂共用）
    {  0.0f, 16.0f, 4.0f, 12.0f, 4.0f }, // 3 Leg（layer_1；左右腿共用）
    {  0.0f, 16.0f, 4.0f, 12.0f, 4.0f }, // 4 BootR（layer_2）
    { 16.0f, 16.0f, 4.0f, 12.0f, 4.0f }, // 5 BootL（layer_2）
}};

// 某面 MC box-UV 像素矩形 → Qt UV 子区（同 mobmodel.cpp mobFaceQtUV）。
void faceQtUV(int face, const BoxTex &t, float &umin, float &vmin, float &umax, float &vmax)
{
    const int mf = kMcFace[face];
    const float sd = t.v0 + t.d;  // 侧面行 v 起点（MC 像素）
    const float sh = t.v0 + t.d + t.h; // 侧面行 v 终点
    float mu0, mv0, mu1, mv1;
    switch (mf) {
    case 0: mu0 = t.u0 + t.d + t.w; mv0 = sd; mu1 = t.u0 + 2 * t.d + t.w; mv1 = sh; break; // MC +X 右（d×h）
    case 1: mu0 = t.u0;             mv0 = sd; mu1 = t.u0 + t.d;             mv1 = sh; break; // MC -X 左（d×h）
    case 2: mu0 = t.u0 + t.d;       mv0 = t.v0; mu1 = t.u0 + t.d + t.w;     mv1 = t.v0 + t.d; break; // +Y 顶（w×d）
    case 3: mu0 = t.u0 + t.d + t.w; mv0 = t.v0; mu1 = t.u0 + t.d + 2 * t.w; mv1 = t.v0 + t.d; break; // -Y 底（w×d）
    case 4: mu0 = t.u0 + t.d;       mv0 = sd; mu1 = t.u0 + t.d + t.w;       mv1 = sh; break; // MC +Z 前（w×h）= 脸
    case 5: mu0 = t.u0 + 2 * t.d + t.w; mv0 = sd; mu1 = t.u0 + 2 * t.d + 2 * t.w; mv1 = sh; break; // -Z 后（w×h）
    default: mu0 = 0; mv0 = 0; mu1 = t.w; mv1 = t.h; break;
    }
    umin = mcU(mu0);
    umax = mcU(mu1);
    vmin = mcV(mv1); // MC v 终点（图像更下）→ Qt v 更小
    vmax = mcV(mv0); // MC v 起点（图像更上）→ Qt v 更大
}

} // namespace

ArmorLayerBox::ArmorLayerBox(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 piece=Helmet 建；QML 设 piece 时再 rebuild
}

void ArmorLayerBox::setPiece(int piece)
{
    // 越界钳 0（Helmet；保几何非空、UV 合法——防误设）。合法 piece：0-5（见 kPieces 表）。
    if (piece < 0 || piece > 5) piece = 0;
    if (piece == m_piece) return;
    m_piece = piece;
    emit pieceChanged();
    rebuild();
}

void ArmorLayerBox::rebuild()
{
    const BoxTex &t = kPieces[size_t(m_piece)];
    // 顶点：pos(3)+uv(2)=5 float；每面 4 角 + 2 三角（24 顶点 / 36 索引）。
    float v[24 * 5];
    quint32 idx[36];
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        float umin, vmin, umax, vmax;
        faceQtUV(f, t, umin, vmin, umax, vmax);
        const quint32 b = quint32(f * 4);
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            v[vi++] = 0.5f * float(s.sx);            // ±0.5 居中（同 UnitCube 基准）
            v[vi++] = 0.5f * float(s.sy);
            v[vi++] = 0.5f * float(s.sz);
            v[vi++] = umin + s.u * (umax - umin);    // 面内归一化 → UV 子区插值
            v[vi++] = vmin + s.v * (vmax - vmin);
        }
        idx[f * 6 + 0] = b + 0; idx[f * 6 + 1] = b + 1; idx[f * 6 + 2] = b + 2;
        idx[f * 6 + 3] = b + 0; idx[f * 6 + 4] = b + 2; idx[f * 6 + 5] = b + 3;
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride → setBounds
    // → setPrimitiveType(Triangles) → addAttribute(Position/TexCoord0/Index) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(v), int(sizeof(v))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx), int(sizeof(idx))));
    setStride(5 * int(sizeof(float)));
    setBounds(QVector3D(-0.5f, -0.5f, -0.5f), QVector3D(0.5f, 0.5f, 0.5f));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 0, QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 3 * int(sizeof(float)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
