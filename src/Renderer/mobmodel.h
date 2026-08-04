#ifndef MOBMODEL_H
#define MOBMODEL_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 四足生物方块化原创 3D 模型几何（t240；Renderer 层）。
//
// 用途：EntityManager 的 Mob 实体（猪 / 牛 / 羊 / 蹒跩者）呈现层几何源——替代 t95 旧版「单个 UnitCube 纯色立方」，
// 给三种 passive mob 各自一个**四肢 + 躯干 + 头**的方块化原创 3D 模型（PLAN §9 区隔：不照搬 MC 美术 /
// UV 拆皮，纯体素盒组合 + 全脸贴图）。机制对齐 MC 1.0 passive mob 形态（四足、头在前），标识符 / 贴图
// 全原创。mobType 0（t239 通用测试生物）仍走 Main.qml 旧 UnitCube 路径，不进本类（保 t95 行为不变）。
//
// mobType（与 EntityManager::MobType 同值，QML 据 entityManager.mobTypeAt 选）：
//   1 = Pig（猪）：紧凑低矮、短腿、大头像在前（-Z）；
//   2 = Cow（牛）：高大长身、头顶一对小角盒；
//   3 = Sheep（羊）：圆胖躯干、小头、短腿。
//   4 = Shambler（蹒跚者；t282）：方块化**人形**（躯干 + 头 + 双臂前伸 + 双腿），机制等价 MC 1.0 僵尸；
//     名称 / 模型 / 贴图全原创（PLAN §9 区隔 Zombie→Shambler）。双臂前伸固定（僵尸经典攻击姿态），
//     双腿绕髋做 biped walk cycle（左右反相，非四足对角），walkPhase 驱动。
//   7 = Spider（蜘蛛；t285/t302）：宽矮躯干 + 前伸小头 + **8 腿**（4 对沿躯干 Z 分布，机制等价 MC 1.0 蜘蛛
//     8 腿爬行；t285 原「简化 4 腿」升级为 t302 八腿 + Z 轴步态）。腿绕躯干侧面髋枢做 Z 轴旋转（外端抬起 =
//     步态），walkPhase 驱动 tetrapod 交替步态（前后对同相、中两对反相）。眼由 Main.qml delegate 补（4 颗红眼
//     纯色子 Model）；声音走 AudioManager.playMobAmbient/playMobHurt(mobType=7) → mob_idle_spider。
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
// t241 动画（腿摆 walk cycle + 头部俯仰）：
//   - walkPhase（弧度相位）：>0 时驱动 4 腿绕各自髋部（腿盒顶）做 X 轴摆动，幅度 kLegSwingAmp·sin(phase)；
//     对角配对（前左+后右 / 前右+后左 反相），机制等价四足 walk cycle。QML 绑定
//     `walkPhase: entityManager.walkPhaseAt(i)`——moveSpeed>0（行走）时 EntityManager 每帧推进相位；
//     idle/吃草 → 冻结（腿停于上次位置）。rebuild 在 setWalkPhase 时触发（几何每 active 帧重算一次，
//     非活动态早退不重算 → 不增开销）。
//   - headPitch（弧度，负=低头）：头部盒（+ 牛角）绕「头后侧颈附着点」X 轴俯仰。QML 绑定
//     `headPitch: entityManager.headPitchAt(i)`——仅羊吃草周期内非零（sin(πp) 包络，中段最深、起末归零），
//     猪 / 牛恒 0（走 addBox 轴对齐快路径，不进旋转）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。mobType / 名称 / 血量 / 相位在 Entities
//   层（EntityManager）；贴图在呈现层（Main.qml Texture）。本类不读 EntityManager、不持贴图——动画相位由
//   QML 据 entityManager.walkPhaseAt / headPitchAt 设。依赖只向下。与 hoe.h / pickaxe.h 同层同风格
//   （多盒 addBox 模式，仅多 uv 通道 + mobType 选择比例 + 动画相位）。
class MobModel : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MobModel)
    Q_PROPERTY(int mobType READ mobType WRITE setMobType NOTIFY mobTypeChanged)
    // t241 行走动画相位（弧度）：setWalkPhase 触发 rebuild 把腿摆到新角度（moveSpeed>0 时每帧变）。
    Q_PROPERTY(float walkPhase READ walkPhase WRITE setWalkPhase NOTIFY walkPhaseChanged)
    // t241 头部俯仰（弧度，负=低头吃草）：仅羊绑非零；猪/牛恒 0 → addBox 轴对齐快路径。
    Q_PROPERTY(float headPitch READ headPitch WRITE setHeadPitch NOTIFY headPitchChanged)

public:
    explicit MobModel(QQuick3DObject *parent = nullptr);

    int mobType() const { return m_mobType; }
    void setMobType(int type);

    float walkPhase() const { return m_walkPhase; }
    void setWalkPhase(float phase);

    float headPitch() const { return m_headPitch; }
    void setHeadPitch(float pitch);

signals:
    void mobTypeChanged();
    void walkPhaseChanged();
    void headPitchChanged();

private:
    void rebuild(); // 按 m_mobType / m_walkPhase / m_headPhase 选比例 + 动画角度建多盒几何。

    int m_mobType = 1;    // 默认猪（合法非空，防未设 mobType 时空几何）
    float m_walkPhase = 0.0f; // 行走相位（弧度）；sin 驱动腿摆
    float m_headPitch = 0.0f; // 头部俯仰（弧度，负=低头）；0 → 头走轴对齐快路径
};

#endif // MOBMODEL_H
