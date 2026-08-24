#include "playerskinbox.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32, qBound

#include <array>

// t731 玩家皮肤盒（±0.5 居中，同 UnitCube/ArmorLayerBox 基准 → Main.qml playerModel /
// CharacterPreview3D 既有 position/scale 沿用）。
//
// 六面 UV（MC 皮肤 64×32 box-UV，与 armorlayerbox.cpp / mobmodel.cpp R19 C3 同公式同坐标系换算，
// 两处已验证约定）：MC ModelRenderer.addBox 自动 UV 6 面像素矩形（面序 0=+X右 1=-X左 2=+Y顶 3=-Y底
// 4=+Z前 5=-Z后；从 textureOffset (u0,v0) 起）。本工程左手系与 MC 右手系水平差 180° → 面 remap
// +X↔-X、+Z↔-Z（kMcFace），使本工程 -Z 前（脸）采 MC +Z Front 区（皮肤脸区）。MC v 向下增 vs
// Qt 图像顶↔v=1 → v 翻（mcV）。base 64×32 钉死（pack 皮肤经 Core 裁切重排成 64×32 族；HD 包是
// base 整数倍 → UV 分数不变）。
namespace {

// MC 像素 → Qt UV（同 armorlayerbox.cpp mcU/mcV；分母钉 base 64×32——UV 是分数，与贴图实际分辨率无关：
//   t747 程序皮肤升 128×64（2× 整数倍）后同分数自动适配；pack HD 包同理（Core 裁切保 w:2:1 族））。
constexpr float kTexW = 64.0f, kTexH = 32.0f;
inline float mcU(float px) { return px / kTexW; }
inline float mcV(float py) { return 1.0f - py / kTexH; }

// 6 面角点（同 mobmodel.cpp / armorlayerbox.cpp kFace 骨架：每面 4 角从外侧看 CCW；(u,v)∈{0,1}² 是面内
//   角点归一化坐标，UV 子区按它插值）。pos 由 ±0.5 符号缩放。
// t747 u 方向修正（头盒「耳朵前后反」根因）：皮肤盒区六面是绕盒连续展开的条带（右脸区[0,8]-脸区[8,16]-
//   左脸区[16,24]-后区[24,32] 环形相接，实测 docs/Default HD 128x 包 alex.png 的额发**跨界连续**：从脸区
//   跨 x=8 边界渗进右脸区大 u 端、跨 x=16 渗进左脸区小 u 端 → 两侧脸区的「前缘（鬓角/耳发）」贴脸区边界）。
//   本工程 +X=角色右侧（左手系 +X 屏幕右 / 模型 -Z 前，t573 预览左右反转修的实测符号链为证）→ +X 面采
//   MC -X 区（[0,8]，kMcFace[0]=1 本就正确），但该区前缘在**大 u 端**（u→8），旧表把 z=-1（前）角接
//   u=0（=后脑缘）→ 侧脸前后贴反（用户报「耳朵前后反」；-X 面前缘恰在小 u 端故左脸一直正确）。同因连带：
//   脸面（-Z）与顶面（+Y）把角色右侧接大 u 端 = 整面镜像（对称内容肉眼不可见，但破坏与侧脸的发际连续性）。
//   修法 = 仅对 0(+X)/2(+Y)/5(-Z) 三面交换角点 u 值（pos/winding 不动 → 剔除无回归）。修后全盒 UV 满足
//   环形连续：脸 x8↔右脸区 x8 / 脸 x16↔左脸区 x16 / 左脸区 x24↔后区 x24 / 后区 x32↔右脸区 x0（条带回绕）、
//   顶区前缘 y=d 行 x8=角色右——连续性即正确性证明（连续展开 = 从外看每面不镜像）。
struct Sgn { int sx, sy, sz; float u, v; };
constexpr Sgn kFace[6][4] = {
    // +X（外法线 +X；采 MC 右脸区 [0,8]，前缘在大 u 端 → z=-1 角取 u=1，t747 翻）
    {{+1, -1, -1, 1, 0}, {+1, +1, -1, 1, 1}, {+1, +1, +1, 0, 1}, {+1, -1, +1, 0, 0}},
    // -X（采 MC 左脸区 [16,24]，前缘在小 u 端 → z=-1 取 u=0 本就正确，不动）
    {{-1, -1, +1, 1, 0}, {-1, +1, +1, 1, 1}, {-1, +1, -1, 0, 1}, {-1, -1, -1, 0, 0}},
    // +Y（顶；顶区前缘=贴脸区发际的 y=v0+d 行，该行角色右端在 x=u0+d 小 u 端 → +X 角取 u=0，t747 翻）
    {{-1, +1, +1, 1, 1}, {+1, +1, +1, 0, 1}, {+1, +1, -1, 0, 0}, {-1, +1, -1, 1, 0}},
    // -Y（底；各盒底面内容均匀（颈/掌/腰/鞋底）无方向特征，保持原样）
    {{-1, -1, -1, 0, 0}, {+1, -1, -1, 1, 0}, {+1, -1, +1, 1, 1}, {-1, -1, +1, 0, 1}},
    // +Z（后；-X 端取 u=0 → 纹理 x=u0+2d+w 贴左脸区后缘，本就正确，不动）
    {{-1, -1, +1, 0, 0}, {+1, -1, +1, 1, 0}, {+1, +1, +1, 1, 1}, {-1, +1, +1, 0, 1}},
    // -Z（前；脸区小 u 端 = 角色右侧（模板实测）→ +X 角取 u=0，t747 翻）
    {{+1, -1, -1, 0, 0}, {-1, -1, -1, 1, 0}, {-1, +1, -1, 1, 1}, {+1, +1, -1, 0, 1}},
};

// 本工程 kFace 面 → MC 面（水平 180° remap：+X↔-X、+Z↔-Z；同 mobmodel.cpp kMcFace）。
// t747 注：MC 模型系 -X=角色右侧（右手臂在 -X，ModelBiped 先例）→ 皮肤模板「头右侧」区=[0,8] 正对
//   MC -X。故本工程 +X（角色右）→ MC -X（下表 case 1 区 [0,8]）= 右脸区，方向自洽。
constexpr int kMcFace[6] = { 1, 0, 2, 3, 5, 4 };

// t731 皮肤部位盒区表（index = piece；MC textureOffset + size，见 playerskinbox.h 注释出处——与
//   build_entities_pack.py draw_skin 的 paint_box 调用同源）。行：{ u0, v0, w, h, d }。
//   复审 #8 + Review 2026-08-23 #1 修正（PIL 实测坐实旧 slim 两行数值错）：MC 1.8 标准 slim 布局是
//   **臂 3 宽×12 高×4 深**（条带 2×(3+4)=14px 占 u40..54，与 classic 仅差宽 1px——demo 包 alex 实测
//   臂条带不透明列 u40..53、u54..55 全透明）、**腿保持 4×12×4 不变**（占满 u0..16，demo 包 alex 腿
//   条带 u0..15 全不透明）。旧值 Arm/Leg 都是 3×12×3（d=3）：臂深错 1px（六面全错位）+ 腿条带 16px
//   满宽被按 3px 采样（大面积采错列）。Core 侧 probeSlimSkinLayout 探测区 [54,56) 按本表条带右缘
//   派生（互指，见 resourcepackmanager.cpp 注释）。
struct BoxTex { float u0, v0, w, h, d; };
constexpr std::array<BoxTex, 4> kPiecesClassic = {{
    {  0.0f,  0.0f, 8.0f,  8.0f, 8.0f }, // 0 Head（脸在 Front (8,8)-(16,16)）
    { 16.0f, 16.0f, 8.0f, 12.0f, 4.0f }, // 1 Body
    { 40.0f, 16.0f, 4.0f, 12.0f, 4.0f }, // 2 Arm（左右共用）
    {  0.0f, 16.0f, 4.0f, 12.0f, 4.0f }, // 3 Leg（左右共用）
}};
constexpr std::array<BoxTex, 4> kPiecesSlim = {{
    {  0.0f,  0.0f, 8.0f,  8.0f, 8.0f }, // 0 Head（同 classic）
    { 16.0f, 16.0f, 8.0f, 12.0f, 4.0f }, // 1 Body（同 classic）
    { 40.0f, 16.0f, 3.0f, 12.0f, 4.0f }, // 2 Arm slim：3宽×12高×4深（条带14px，仅宽比 classic 差 1px）
    {  0.0f, 16.0f, 4.0f, 12.0f, 4.0f }, // 3 Leg slim：4宽×12高×4深（slim 腿不变，与 classic 同款）
}};
// Review 2026-08-23 #1 数值锁（同 t789 QML 契约 static_assert 先例）：slim 与 classic 的差异**只有
//   臂宽 4→3**——臂深 d=4、腿整行不变。任何再按「slim = 臂腿都缩 3px」错误假设的回归在此编译期拦下；
//   同时锁 classic 表防误改（其臂条带右缘 56 是 Core probeSlimSkinLayout 探测区坐标的派生源）。
static_assert(kPiecesSlim[2].w == 3.0f && kPiecesSlim[2].h == 12.0f && kPiecesSlim[2].d == 4.0f,
              "slim arm must be 3x12x4 (MC 1.8 slim: depth stays 4)");
static_assert(kPiecesSlim[3].w == 4.0f && kPiecesSlim[3].h == 12.0f && kPiecesSlim[3].d == 4.0f,
              "slim leg must equal classic 4x12x4 (MC 1.8 slim keeps legs)");
static_assert(kPiecesClassic[2].w == 4.0f && kPiecesClassic[2].h == 12.0f && kPiecesClassic[2].d == 4.0f,
              "classic arm must stay 4x12x4 (probeSlimSkinLayout region derives from this strip)");
static_assert(kPiecesSlim[0].w == kPiecesClassic[0].w && kPiecesSlim[1].w == kPiecesClassic[1].w,
              "slim layout only differs on arm/leg rows");

// 某面 MC box-UV 像素矩形 → Qt UV 子区（armorlayerbox.cpp faceQtUV + subV 行区间裁切）。
//   sub0/sub1 ∈ [0,1] 是盒高 h 的采样分数区间：含 h 的面（侧 d×h / 前后 w×h）v 行起终 =
//   v0+d+sub0*h .. v0+d+sub1*h；顶/底（w×d）不含 h 不裁（腿段端关节面被邻段遮挡）。
void faceQtUV(int face, const BoxTex &t, float sub0, float sub1,
              float &umin, float &vmin, float &umax, float &vmax)
{
    const int mf = kMcFace[face];
    const float sd = t.v0 + t.d + sub0 * t.h;    // 侧面行 v 起点（裁后）
    const float sh = t.v0 + t.d + sub1 * t.h;    // 侧面行 v 终点（裁后）
    float mu0, mv0, mu1, mv1;
    switch (mf) {
    case 0: mu0 = t.u0 + t.d + t.w; mv0 = sd; mu1 = t.u0 + 2 * t.d + t.w; mv1 = sh; break; // MC +X=角色左（d×h；区 [16,24]）
    case 1: mu0 = t.u0;             mv0 = sd; mu1 = t.u0 + t.d;             mv1 = sh; break; // MC -X=角色右（d×h；区 [0,8]，前缘在大 u 端）
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

PlayerSkinBox::PlayerSkinBox(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 piece=Head 建；QML 设 piece/subV0/subV1 时再 rebuild
}

void PlayerSkinBox::setPiece(int piece)
{
    // 越界钳 0（Head；保几何非空、UV 合法——防误设）。合法 piece：0-3（见 kPieces 表）。
    if (piece < 0 || piece > 3) piece = 0;
    if (piece == m_piece) return;
    m_piece = piece;
    emit pieceChanged();
    rebuild();
}

void PlayerSkinBox::setSubV0(qreal v)
{
    const qreal c = qBound(0.0, v, 1.0);
    if (c == m_subV0) return;
    m_subV0 = c;
    emit subV0Changed();
    rebuild();
}

void PlayerSkinBox::setSubV1(qreal v)
{
    const qreal c = qBound(0.0, v, 1.0);
    if (c == m_subV1) return;
    m_subV1 = c;
    emit subV1Changed();
    rebuild();
}

// 复审 #8：slim 布局切换（仅臂宽 4→3；腿不变——见 kPiecesSlim 注释 + static_assert 数值锁）。幂等（同值 no-op）。
void PlayerSkinBox::setSlim(bool s)
{
    if (s == m_slim) return;
    m_slim = s;
    emit slimChanged();
    rebuild();
}

void PlayerSkinBox::rebuild()
{
    // 复审 #8：盒区表按 slim 布局选（Head/Body 两表同区，选择无差）。
    const BoxTex &t = (m_slim ? kPiecesSlim : kPiecesClassic)[size_t(m_piece)];
    // 采样分数区间（防退化：sub1 <= sub0 时抬 sub1 到 sub0+ε，保面片非零高）。
    float sub0 = float(m_subV0);
    float sub1 = float(m_subV1);
    if (sub1 <= sub0)
        sub1 = qMin(1.0f, sub0 + 0.001f);
    // 顶点：pos(3)+uv(2)=5 float；每面 4 角 + 2 三角（24 顶点 / 36 索引）。
    float v[24 * 5];
    quint32 idx[36];
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        float umin, vmin, umax, vmax;
        faceQtUV(f, t, sub0, sub1, umin, vmin, umax, vmax);
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
