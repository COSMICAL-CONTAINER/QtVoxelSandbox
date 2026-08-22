#ifndef PLAYERSKINBOX_H
#define PLAYERSKINBOX_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// t731 玩家皮肤盒几何（Renderer 层；Main.qml playerModel 第三人称 + CharacterPreview3D 背包预览共用）。
//
// 用途：玩家身体各部件（头/躯干/臂/腿）从共享皮肤贴图（程序皮肤 entity_skin_default/alex.png 128×64
// （t747 高清重绘，布局 = 64×32 基准 ×2 → UV 分数不变），或 pack 命中后 Core 侧裁切重排的 64×32 族）
// 按 MC 标准 box-UV 采样对应部位纹素——与 ArmorLayerBox
// （t718）/ MobModel（R19 C3）同公式同坐标系换算：本工程左手系（+X 右 / +Y 上 / -Z 前）vs MC 右手系
// （+Z 前）水平差 180° → 面 remap +X↔-X、+Z↔-Z；MC v 向下增 vs Qt 图像顶↔v=1 → v 翻。本工程 -Z 前
// 采 MC +Z Front 区 = 皮肤脸区（程序/pack 皮肤脸都画在 Front，build_entities_pack.py draw_skin 实测）。
//   t747：皮肤盒六面是绕盒连续展开条带，+X/+Y/-Z 三面旧 u 方向把「贴脸区边界的前缘（鬓角/耳发）」接反
//   （头部耳朵前后反的根因），已按环形连续性修正——推导见 playerskinbox.cpp kFace 注释。
// 几何是 ±0.5 居中单位盒（同 UnitCube/ArmorLayerBox 基准 → Main.qml 既有 position/scale 直接沿用）。
//
// piece 部位（MC 64×32 皮肤布局 textureOffset + size；与 build_entities_pack.py draw_skin 的
//   paint_box 调用同源，与 ArmorLayerBox kPieces 的 Head/Chest/Sleeve/Leg 区一致——皮肤与盔甲 layer
//   共用同一套人体盒区坐标）：
//   0 Head (0,0)   8×8×8（前脸 = 皮肤脸区 → 皮肤化后 Main.qml/预览的旧「眼子 Model」移除）
//   1 Body (16,16) 8×12×4
//   2 Arm  (40,16) 4×12×4（左右臂共用——64×32 布局无独立左臂区，MC 左臂镜像右臂；贴图近对称视觉无差，
//                   同 ArmorLayerBox Sleeve 左右共用先例）
//   3 Leg  (0,16)  4×12×4（左右腿共用，同上）
//
// subV0/subV1（0..1 盒高分数，缺省 0/1 = 整段）：**腿分段采样**。本工程腿分大腿+小腿两段绕膝盖弯折
//   （蹲/坐姿），MC 是整腿单盒 → 大腿段采腿盒上半 [0,0.5]、小腿段采下半 [0.5,1]（鞋在皮肤最底 2 行，
//   落在小腿段底 = 位置正确）。只裁含盒高 h 的面（侧 d×h / 前后 w×h）的 v 行区间；顶/底面（w×d）不含
//   h 不受影响（段端关节面被邻段/护甲遮挡，视觉无害）。臂不分段（袖+手两 Model 合并为单整臂盒）。
//
// 顶点格式：pos(3)+uv(2)=5 float；24 顶点 / 36 索引；CCW 朝外单面（默认 backface 剔除）。
//
// 分层（PLAN §2）：属 Renderer，与 armorlayerbox / unitcube 同层同风格；不读 Game/Core（皮肤名 /
// 贴图选择在 QML 呈现层，pack 裁切在 Core ResourcePackManager），依赖只向下。
class PlayerSkinBox : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PlayerSkinBox)
    // 皮肤部位（0 Head / 1 Body / 2 Arm / 3 Leg；见类注释盒区表）。setter 触发 rebuild 重算六面 UV 子区。
    Q_PROPERTY(int piece READ piece WRITE setPiece NOTIFY pieceChanged)
    // 盒高采样分数区间 [subV0,subV1]（腿分段用；0/1 = 整段）。含 h 的面 v 行区间按它裁。
    Q_PROPERTY(qreal subV0 READ subV0 WRITE setSubV0 NOTIFY subV0Changed)
    Q_PROPERTY(qreal subV1 READ subV1 WRITE setSubV1 NOTIFY subV1Changed)

public:
    explicit PlayerSkinBox(QQuick3DObject *parent = nullptr);

    int piece() const { return m_piece; }
    void setPiece(int piece);

    qreal subV0() const { return m_subV0; }
    void setSubV0(qreal v);

    qreal subV1() const { return m_subV1; }
    void setSubV1(qreal v);

signals:
    void pieceChanged();
    void subV0Changed();
    void subV1Changed();

private:
    void rebuild(); // 按 m_piece 选 MC box-UV 盒区 + m_subV0/1 裁侧/前后面的 v 行区间，建 ±0.5 单位盒。

    int m_piece = 0;       // 默认 Head（合法非空，防未设 piece 时空几何）
    qreal m_subV0 = 0.0;   // 盒高采样起点分数（0 = 盒顶）
    qreal m_subV1 = 1.0;   // 盒高采样终点分数（1 = 盒底）
};

#endif // PLAYERSKINBOX_H
