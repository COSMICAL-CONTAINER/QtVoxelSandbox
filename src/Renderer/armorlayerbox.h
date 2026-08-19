#ifndef ARMORLAYERBOX_H
#define ARMORLAYERBOX_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// t718 盔甲 layer 薄壳盒几何（Renderer 层；玩家（Main.qml playerModel）+ 人形 mob（t719 Shambler/Bones
// delegate）共用的「通用 layer 渲染器」）。
//
// 用途：玩家 / mob 已穿护甲件按部位叠加薄壳盒（= 比本体盒略大的满盒，本体在壳内挡住内面；机制等价
// MC 1.0 armor layer 模型——layer_1 头/胸/腿袖 + layer_2 靴，expansion 外扩）。几何是 ±0.5 居中单位盒
// （同 UnitCube 基准 → Main.qml 既有护甲 Model 的 position/scale 取值直接沿用），区别在 **UV**：
// 每面按 MC 盔甲 layer 的 box-UV 子区采样（同 MobModel R19 C3 的 MC ModelRenderer.addBox 自动 UV 公式，
// 含本工程/MC 坐标系水平 180° 面 remap + MC v 向下 → Qt v 翻两处换算），从共享 layer 贴图
// （armor_<kind>_layer_<1,2>.png 程序层 / pack <prefix>_layer_<n>.png 运行期映射，Main.qml armorLayerUrl）
// 取对应部位纹素——程序层与 pack 层是同一套 UV 分数（HD 包是 base 整数倍，UV=MC 像素/base 不变）。
//
// piece 部位（QML 传 int；越界钳 0=Helmet）——盒区（MC textureOffset u0,v0 + size w,h,d；base 64×32）：
//   0 Helmet 头盔 (0,0)    8×8×8   layer_1（贴图 front 面自带开脸窗 alpha 孔）
//   1 Chest  胸甲 (16,16)  8×12×4  layer_1（躯干壳；MC 胸甲另含双袖 → 见 piece 2）
//   2 Sleeve 袖壳 (40,16)  4×12×4  layer_1（手臂壳；左右共用同区——MC 左臂镜像，同 MobModel 臂/腿共用
//                                   同区先例不镜像，贴图近对称视觉无差）
//   3 Leg    护腿 (0,16)   4×12×4  layer_1（腿壳；左右共用同区，同上）
//   4 BootR  右靴 (0,16)   4×12×4  layer_2（独立盒区）
//   5 BootL  左靴 (16,16)  4×12×4  layer_2（独立盒区，非镜像——MC layer_2 实测左右各一区）
// QML 侧绑对应 layer 贴图：piece 0-3 用 layer_1 Texture、piece 4/5 用 layer_2 Texture（几何不持贴图，
// 分层铁律：本类只产出几何 + UV，贴图在呈现层）。
//
// 顶点格式：pos(3)+uv(2)=5 float；24 顶点 / 36 索引；CCW 朝外单面（默认 backface 剔除——薄壳内面被
// 本体（不透明）遮住，透过贴图 alpha 孔（开脸窗 / 链甲网眼）看到的是本体表面，无需双面）。
//
// 分层（PLAN §2）：属 Renderer（依赖 QtQuick3D），与 unitcube / mobmodel 同层同风格；不读 Game/Entities/
// Core（护甲数据、tier→贴图选择都在 QML 呈现层），依赖只向下。
class ArmorLayerBox : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ArmorLayerBox)
    // 盔甲部位（0 Helmet / 1 Chest / 2 Sleeve / 3 Leg / 4 BootR / 5 BootL；见类注释盒区表）。
    // setter 触发 rebuild 重算六面 UV 子区。
    Q_PROPERTY(int piece READ piece WRITE setPiece NOTIFY pieceChanged)

public:
    explicit ArmorLayerBox(QQuick3DObject *parent = nullptr);

    int piece() const { return m_piece; }
    void setPiece(int piece);

signals:
    void pieceChanged();

private:
    void rebuild(); // 按 m_piece 选 MC box-UV 盒区，建 ±0.5 单位盒（六面 UV=盒区子区）。

    int m_piece = 0; // 默认 Helmet（合法非空，防未设 piece 时空几何）
};

#endif // ARMORLAYERBOX_H
