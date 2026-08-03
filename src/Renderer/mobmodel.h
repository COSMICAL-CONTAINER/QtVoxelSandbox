#ifndef MOBMODEL_H
#define MOBMODEL_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 四足生物方块化原创 3D 模型几何（t240；Renderer 层）。
//
// 用途：EntityManager 的 Mob 实体（猪 / 牛 / 羊）呈现层几何源——替代 t95 旧版「单个 UnitCube 纯色立方」，
// 给三种 passive mob 各自一个**四肢 + 躯干 + 头**的方块化原创 3D 模型（PLAN §9 区隔：不照搬 MC 美术 /
// UV 拆皮，纯体素盒组合 + 全脸贴图）。机制对齐 MC 1.0 passive mob 形态（四足、头在前），标识符 / 贴图
// 全原创。mobType 0（t239 通用测试生物）仍走 Main.qml 旧 UnitCube 路径，不进本类（保 t95 行为不变）。
//
// mobType（与 EntityManager::MobType 同值，QML 据 entityManager.mobTypeAt 选）：
//   1 = Pig（猪）：紧凑低矮、短腿、大头像在前（-Z）；
//   2 = Cow（牛）：高大长身、头顶一对小角盒；
//   3 = Sheep（羊）：圆胖躯干、小头、短腿。
// 其余值（含 0 / 越界）→ 兜底按 Pig 建（保几何非空、bounds 合法）。
//
// 顶点格式：pos(3) + uv(2) = 5 float。每盒 6 面 × 4 角 = 24 顶点 / 36 索引；多盒累加。
// UV：每面铺**整张贴图** [0,1]×[0,1]（同 CrackBox 全脸 UV，区别于 BlockCube 的图集子区）→ QML 给每类
//   mob 一张独占贴图（mob_pig / mob_cow / mob_sheep），躯干 / 头 / 腿各盒都铺同一张贴图。简单稳健，
//   配合各方块比例 + 配色让三种 mob 肉眼可辨（非 MC 式 UV 拆皮）。
//
// 局部坐标约定：原点 = 躯干中心；+Y 上、-Z 前（头朝 -Z，与 EntityManager yawAt 约定「模型本地 -Z 正对
//   行走方向」一致 → delegate Node eulerRotation.y = yawAt 后头朝行走方向）。bounds 据各盒实际范围算
//   （配合 Model 变换给视锥剔除盒）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。mobType / 名称 / 血量在 Entities 层
// （EntityManager）；贴图在呈现层（Main.qml Texture）。本类不读 EntityManager、不持贴图——mobType 由
// QML 据 entityManager.mobTypeAt 设。依赖只向下。与 hoe.h / pickaxe.h 同层同风格（多盒 addBox 模式，
// 仅多 uv 通道 + mobType 选择比例）。
class MobModel : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MobModel)
    Q_PROPERTY(int mobType READ mobType WRITE setMobType NOTIFY mobTypeChanged)

public:
    explicit MobModel(QQuick3DObject *parent = nullptr);

    int mobType() const { return m_mobType; }
    void setMobType(int type);

signals:
    void mobTypeChanged();

private:
    void rebuild(); // 按 m_mobType 选比例建多盒几何；顶点位置 / bounds 随 mobType 变。

    int m_mobType = 1; // 默认猪（合法非空，防未设 mobType 时空几何）
};

#endif // MOBMODEL_H
