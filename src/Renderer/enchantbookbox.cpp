#include "enchantbookbox.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <array>

// t732 附魔台悬浮书贴图盒（±0.5 居中，同 UnitCube/PlayerSkinBox 基准 → bookDelegate 既有
//   position/scale / 翻页枢轴动画沿用）。页面阅读向（t764 修正）：+Y 上面 u∝+X（左右页 u 都自书脊
//   向外缘 = 阅读行向，符文行不镜像）、**v=1（图顶行）钉在 −Z 远端**（+Z 是书正面朝玩家侧，
//   读者视线自 +Z 来 → 行首/行序顶行在远端才是正读，t764 前 v=1 落 +Z 近端 = 行序上下颠倒）。
//   坐标换算与面角点同 minecartbox.cpp（MC v 向下增 vs Qt 图像顶↔v=1 → v 翻；base 64×32 钉死，
//   demo 包 8× 下 UV 分数不变）。
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
    // +Y（顶；页面阅读面）。t764：v 翻——v=1（图顶行）从 +Z 近端改钉 −Z 远端（+Z 朝玩家时行序正读）。
    {{-1, +1, +1, 0, 0}, {+1, +1, +1, 1, 0}, {+1, +1, -1, 1, 1}, {-1, +1, -1, 0, 1}},
    // -Y（底；与顶面同向翻转，底面观感与顶面行序一致）
    {{-1, -1, -1, 0, 1}, {+1, -1, -1, 1, 1}, {+1, -1, +1, 1, 0}, {-1, -1, +1, 0, 0}},
    // +Z
    {{-1, -1, +1, 0, 0}, {+1, -1, +1, 1, 0}, {+1, +1, +1, 1, 1}, {-1, +1, +1, 0, 1}},
    // -Z
    {{+1, -1, -1, 1, 0}, {-1, -1, -1, 0, 0}, {-1, +1, -1, 0, 1}, {+1, +1, -1, 1, 1}},
};

// 面 → 像素矩形（图像 y 向下；面序同 kFace）。
struct FaceRect { float u0, v0, u1, v1; };
using PartRects = std::array<FaceRect, 6>;

// t732 布局 0（qrc 程序 entity_enchant_book.png 64×32；分区实测：左半 x[0,32) 棕封——金边竖条
//   x[0,2) / x[30,32)、纹章框 y[12,20) x[10,22)；右半 x[32,64) 暖白纸页——符文行 y=5,8,11,14,17,20,23
//   （止于 y=24，y[24,32) 无字白纸）、书脊侧暗化纸缘 x[32,34)）。
constexpr std::array<PartRects, 4> kQrcParts = {{
    // 0 封面页（左页；+X = 书脊侧金边竖条，-X = 外沿金边竖条，±Y 大面铺满左半封面）。
    PartRects{{ { 0, 0, 2,32}, {30, 0,32,32}, { 0, 0,32,32}, { 0, 0,32,32}, { 0, 0,32, 2}, { 0, 0,32, 2} }},
    // 1 纸页（右页；-X = 书脊侧暗化纸缘，+X = 外沿，±Y 大面铺满右半纸页含符文行）。
    PartRects{{ {62, 0,64,32}, {32, 0,34,32}, {32, 0,64,32}, {32, 0,64,32}, {32, 0,64, 2}, {32, 0,64, 2} }},
    // 2 书脊（可见 ±X 窄面 = 金边竖条（2×32 纵横比与 0.03×0.46 窄面吻合）；端角小面 2×2）。
    PartRects{{ { 0, 0, 2,32}, { 0, 0, 2,32}, { 0, 0, 2, 2}, { 0, 0, 2, 2}, { 0, 0, 2, 2}, { 0, 0, 2, 2} }},
    // 3 翻页片（t764：±Y 大面改采符文行区 x[33,46) y[4,25)——旧版采无字白纸行 y[24,32)，白纸贴白纸页
    //   肉眼不可辨（t764 ④「翻页不可见」根因之一）；符文行让飞行中的页片自带字迹可辨，配合 Main.qml
    //   暖 tint 与静态纸页拉开明度差）。
    PartRects{{ {32,24,34,32}, {62,24,64,32}, {33, 4,46,25}, {33, 4,46,25}, {33, 4,46, 7}, {33, 4,46, 7} }},
}};

// t732 布局 1（demo 包 entity/enchanting_table_book.png 512×256 = base 8×；分区像素实测）：
//   封面带 y[0,10)：左封 x[0,11) / 书脊条 x[11,13)（含白宝石）/ 右封 x[13,24)；纸页叠 y[10,19)
//   x[1,23)（书脊侧带暗影缘）；翻页白页 x[24,34) y[10,19)；其余为不透明暗底（采样窗全落实测亮区）。
constexpr std::array<PartRects, 4> kPackParts = {{
    // 0 封面页（左页；+X 书脊侧 = 书脊条，-X 外沿 = 左封边条，+Y = 左封、-Y = 右封（书外底观感））。
    PartRects{{ {11, 0,13,10}, { 0, 0, 2,10}, { 0, 0,11,10}, {13, 0,24,10}, { 0, 0,11, 2}, { 0, 0,11, 2} }},
    // 1 纸页（右页；-X 书脊侧 / +X 外沿 = 纸页边条，±Y 大面 = 纸页叠区）。
    PartRects{{ {10,10,12,19}, { 1,10, 3,19}, { 1,10,12,19}, {13,10,23,19}, { 1,10,12,12}, { 1,10,12,12} }},
    // 2 书脊（可见 ±X 窄面 = 书脊条（白宝石纵列）；端角小面 = 条顶）。
    PartRects{{ {11, 0,13,10}, {11, 0,13,10}, {11, 0,13, 2}, {11, 0,13, 2}, {11, 0,13, 2}, {11, 0,13, 2} }},
    // 3 翻页片（±Y 大面 = 翻页白页区（白底 + 深字块））。
    PartRects{{ {24,10,26,19}, {24,10,26,19}, {24,10,34,19}, {24,10,34,19}, {24,10,34,12}, {24,10,34,12} }},
}};

} // namespace

EnchantBookBox::EnchantBookBox(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 piece/layout 建；QML 设属性时再 rebuild
}

void EnchantBookBox::setPiece(int piece)
{
    // 越界钳 0（封面页；保几何非空、UV 合法——防误设）。合法 piece：0-3（见 kQrcParts/kPackParts 表）。
    if (piece < 0 || piece > 3) piece = 0;
    if (piece == m_piece) return;
    m_piece = piece;
    emit pieceChanged();
    rebuild();
}

void EnchantBookBox::setLayout(int layout)
{
    if (layout < 0 || layout > 1) layout = 0;
    if (layout == m_layout) return;
    m_layout = layout;
    emit layoutChanged();
    rebuild();
}

void EnchantBookBox::rebuild()
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
