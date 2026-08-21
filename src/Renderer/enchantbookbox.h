#ifndef ENCHANTBOOKBOX_H
#define ENCHANTBOOKBOX_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// t732 附魔台悬浮书贴图盒几何（Renderer 层；Main.qml bookDelegate（t679）消费）。
//
// 用途：悬浮书的左右页 / 书脊 / 翻页片从 UnitCube 纯色改为带 UV 的 ±0.5 居中单位盒（同
// UnitCube/PlayerSkinBox 基准 → bookDelegate 既有 position/scale / 翻页枢轴动画直接沿用，
// 机制零改动）。t679 时 pack 书贴图「整本书 UV 展开与两页盒映射不符」而留程序纯色——本类以
// 像素实测分区解决该映射：两页盒各取封面 / 纸页区，不强求与原书 1:1（贴图分区重解读）。
//
// 双布局（layout）：qrc 程序贴图与 demo 包书贴图分区实测不同（同 MinecartBox 双表先例）：
//   0 = qrc 程序 entity_enchant_book.png（64×32 实测：左半 x[0,32) 棕封 + 金边（x[0,2) 金边竖条）
//       + 封面纹章，右半 x[32,64) 暖白纸页 + 符文行（符文行止于 y=24，y[24,32) 为无字白纸））；
//   1 = demo 包 entity/enchanting_table_book.png（512×256 = base 8× 实测：封面带 y[0,10)——
//       左封 x[0,11) / 书脊条 x[11,13)（含白宝石）/ 右封 x[13,24)；纸页叠 y[10,19)；翻页白页
//       x[24,34) y[10,19)；其余为不透明暗底，采样窗只落在实测亮区）。
//
// piece 部件（面序同 kFace：0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z；书脊沿 Z，页面 +Y 朝上）：
//   0 封面页（左页：上面 = 封面 / 底面 = 封底（包）或同封面（qrc）；书脊侧 +X = 金边竖条）
//   1 纸页（右页：上下大面 = 纸页 + 符文行；书脊侧 -X = 暗化纸缘）
//   2 书脊（可见 ±X 窄面 = 金边竖条（qrc，2×32 与窄面纵横比吻合）/ 书脊条（包））
//   3 翻页片（上下大面 = 无字白纸行（qrc）/ 翻页白页（包）——与静态纸页区分，翻动可辨）
//
// 顶点格式：pos(3)+uv(2)=5 float；24 顶点 / 36 索引；CCW 朝外单面（默认 backface 剔除）。
//
// 分层（PLAN §2）：属 Renderer，与 minecartbox / playerskinbox 同层同风格；不读 Game/Core
// （贴图选择在 QML 呈现层，pack 探测在 Core entitySource("enchant_book")），依赖只向下。
class EnchantBookBox : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EnchantBookBox)
    // 部件（0 封面页 / 1 纸页 / 2 书脊 / 3 翻页片；见类注释分区表）。setter 触发 rebuild。
    Q_PROPERTY(int piece READ piece WRITE setPiece NOTIFY pieceChanged)
    // 贴图布局（0 = qrc 程序 64×32 / 1 = demo 包 8× 实测排版；见类注释双布局说明）。
    Q_PROPERTY(int layout READ layout WRITE setLayout NOTIFY layoutChanged)

public:
    explicit EnchantBookBox(QQuick3DObject *parent = nullptr);

    int piece() const { return m_piece; }
    void setPiece(int piece);

    int layout() const { return m_layout; }
    void setLayout(int layout);

signals:
    void pieceChanged();
    void layoutChanged();

private:
    void rebuild(); // 按 m_layout/m_piece 查「部件 × 六面」像素矩形表，建 ±0.5 单位盒。

    int m_piece = 0;   // 默认封面页（合法非空，防未设 piece 时空几何）
    int m_layout = 0;  // 默认 qrc 程序布局（无包时的常态）
};

#endif // ENCHANTBOOKBOX_H
