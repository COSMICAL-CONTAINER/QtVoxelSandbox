#include "minecartbox.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <array>

// t732 矿车斗贴图盒（±0.5 居中，同 UnitCube/PlayerSkinBox 基准 → Main.qml cartHost 既有
//   position/scale 沿用）。
//
// 六面 UV：本类不走 MC ModelRenderer box-UV 公式（矿车贴图是条带排版而非单盒展开），改「部件 × 面」
//   显式像素矩形表（FaceRect { u0,v0,u1,v1 }，图像坐标 y 向下；六面面序同 playerskinbox kFace：
//   0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z，每面 4 角从外侧看 CCW，(u,v)∈{0,1}² 面内归一化角点）。MC v 向下
//   增 vs Qt 图像顶↔v=1 → v 翻（mcV，同 armorlayerbox/playerskinbox 换算）；base 64×32 钉死（demo 包
//   是 base 整数倍 → UV 分数不变）。矩形顶行落在面 v=1 端（如侧帮亮卷边画在带顶 → 卷边朝车口）。
namespace {

// MC 像素 → Qt UV（同 playerskinbox mcU/mcV；base 64×32 钉死）。
constexpr float kTexW = 64.0f, kTexH = 32.0f;
inline float mcU(float px) { return px / kTexW; }
inline float mcV(float py) { return 1.0f - py / kTexH; }

// 6 面角点（同 playerskinbox kFace：每面 4 角从外侧看 CCW；pos 由 ±0.5 符号缩放）。
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
    // -Z（车头朝此向）
    {{+1, -1, -1, 1, 0}, {-1, -1, -1, 0, 0}, {-1, +1, -1, 0, 1}, {+1, +1, -1, 1, 1}},
};

// 面 → 像素矩形（图像 y 向下；面序同 kFace）。
struct FaceRect { float u0, v0, u1, v1; };
using PartRects = std::array<FaceRect, 6>;

// t732 布局 0（qrc 程序 entity_minecart.png 64×32；分区实测：侧帮带 (0,4)-(44,20)——左壁窗
//   x[0,20) / 右壁窗 x[24,44)，上 2px y[4,6) 亮卷边 + 铆钉行 y[9,11)；木底板带 (0,20)-(44,28)）。
//   程序贴图各区纹样均匀（撒点 + 铆钉列）→ 端帮大面复用壁窗（4px 端槽拉伸过狠不用）。
constexpr std::array<PartRects, 4> kQrcParts = {{
    // 0 底板：±Y 大面铺满木底板带；±X/±Z 薄边被车帮包住（兜底木条）。
    PartRects{{ {0,20, 8,28}, {0,20, 8,28}, {0,20,44,28}, {0,20,44,28}, {0,20, 8,28}, {0,20, 8,28} }},
    // 1 左纵帮：外(-X)/内(+X) 同左壁窗（程序贴图无独立内壁区，铆钉对位一致）；顶 = 亮卷边条。
    PartRects{{ {0, 4,20,20}, {0, 4,20,20}, {0, 4,20, 6}, {0,18,20,20}, {0, 6, 4,20}, {0, 6, 4,20} }},
    // 2 右纵帮：外(+X)/内(-X) 同右壁窗。
    PartRects{{ {24, 4,44,20}, {24, 4,44,20}, {24, 4,44, 6}, {24,18,44,20}, {20, 6,24,20}, {20, 6,24,20} }},
    // 3 端帮：大面(±Z) 铺左壁窗；顶 = 卷边条；±X 薄端 = 带内窄条。
    PartRects{{ {0, 6, 4,20}, {0, 6, 4,20}, {24, 4,44, 6}, {24,18,44,20}, {0, 4,20,20}, {0, 4,20,20} }},
}};

// t732 布局 1（demo 包 entity/minecart.png 512×256 = base 8×；分区像素实测）：
//   侧壁带 y[10,28)：左壁 x[2,22) / 右壁 x[24,44)（y[10,12) 亮卷边行）；框栏暗端面 (0,2)-(20,10)
//   （亮 W 边框 + 暗板 = 端面栏板）；浅纹内壁 (20,2)-(36,10)；木底 (46,12)-(62,24)。贴图其余区
//   透明，采样窗全部落在实测亮区内（防采到空白）。
constexpr std::array<PartRects, 4> kPackParts = {{
    // 0 底板：木底块 16×12。
    PartRects{{ {46,12,54,24}, {46,12,54,24}, {46,12,62,24}, {46,12,62,24}, {46,12,54,24}, {46,12,54,24} }},
    // 1 左纵帮：外(-X) = 左壁窗含亮卷边；内(+X) = 浅纹内壁区（车斗内受光弱一档的层次）。
    PartRects{{ {20, 2,36,10}, { 2,10,22,28}, { 2,10,22,12}, { 2,26,22,28}, { 2,20, 6,28}, { 2,20, 6,28} }},
    // 2 右纵帮：外(+X) = 右壁窗；内(-X) = 浅纹内壁区。
    PartRects{{ {24,10,44,28}, {20, 2,36,10}, {24,10,44,12}, {24,26,44,28}, {40,20,44,28}, {40,20,44,28} }},
    // 3 端帮：大面(±Z) = 框栏暗端面（亮框读作端部栏板）；顶 = 端面上沿；薄端 = 内壁左条。
    PartRects{{ {20, 2,24,10}, {20, 2,24,10}, {20, 2,36, 4}, {20, 8,36,10}, { 0, 2,20,10}, { 0, 2,20,10} }},
}};

} // namespace

MinecartBox::MinecartBox(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 piece/layout 建；QML 设属性时再 rebuild
}

void MinecartBox::setPiece(int piece)
{
    // 越界钳 0（底板；保几何非空、UV 合法——防误设）。合法 piece：0-3（见 kQrcParts/kPackParts 表）。
    if (piece < 0 || piece > 3) piece = 0;
    if (piece == m_piece) return;
    m_piece = piece;
    emit pieceChanged();
    rebuild();
}

void MinecartBox::setLayout(int layout)
{
    if (layout < 0 || layout > 1) layout = 0;
    if (layout == m_layout) return;
    m_layout = layout;
    emit layoutChanged();
    rebuild();
}

void MinecartBox::rebuild()
{
    const PartRects &part = (m_layout == 1) ? kPackParts[size_t(m_piece)] : kQrcParts[size_t(m_piece)];
    // 顶点：pos(3)+uv(2)=5 float；每面 4 角 + 2 三角（24 顶点 / 36 索引）。
    float v[24 * 5];
    quint32 idx[36];
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        const FaceRect &r = part[size_t(f)];
        const float umin = mcU(r.u0), umax = mcU(r.u1);
        const float vmin = mcV(r.v1), vmax = mcV(r.v0); // v 翻：矩形底行 → 面 v 小端
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
