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
//   8 = Chicken（鸡；t398）：小型鸟——圆胖躯干 + 前伸小头 + 后翘尾 + **2 细腿**（biped walk cycle，机制等价
//     MC 1.0 鸡两足鸟形态）。喙 / 鸡冠由 Main.qml delegate 补（纯色子 Model）。声音 mobType=8 越界兜底
//     mob_idle（generic）。
//   9 = Squid（鱿鱼；t399）：水生软体——圆胖躯干（ mantle ）+ 顶端小尖 + **8 触腕**（环绕身体底沿分布，机制
//     等价 MC 1.0 squid 8 触腕）。触腕绕各自顶端枢轴做 X 轴摆动（前后波浪式起伏，相位错开 → 游动时触腕飘动），
//     walkPhase 驱动（squid 水中持续漂移 → moveSpeed 恒 >0 → 触腕常驻摆动）。眼由 Main.qml delegate 补（纯色子 Model）。
//   10 = Wolf（狼；t480）：中型犬科——细长躯干 + 前伸尖头 + 双立耳 + **4 腿**（四足 walk cycle，机制等价
//     MC 1.0 狼）。尾巴**不在本几何** —— 呈现层 QML 据血量旋转独立尾巴 Model（spec「尾巴角度示血量」；独立子
//     Model 才能绕尾根枢独立旋转）。坐姿由 Main.qml delegate 变换（压缩 + 后倾）驱动。眼由 Main.qml delegate 补。
//   11 = Ocelot/Cat（豹猫/猫；t481）：中型猫科——细长躯干 + 前伸圆头 + 双尖耳 + 长尾（几何内带尾，随身体
//     贴图同纹）+ **4 腿**（四足 walk cycle，机制等价 MC 1.0 豹猫）。未驯服 = 丛林豹猫（斑点橙棕贴图）、
//     驯服 = 家猫（3 色变体贴图），几何共用（机制等价 MC 1.0 豹猫/猫同模型异贴图；毛色变体由 Main.qml 据
//     ocelotVariantAt 切贴图，几何不变）。坐姿由 Main.qml delegate 变换（压缩 + 后倾）驱动。眼由 Main.qml delegate 补。
//   14 = Silverfish（银鱼；t487）：小型虫类敌对生物——分节躯干 + 前伸小头 + **3 对短腿**（机制等价 MC 1.0 银鱼
//     多足 + 多节体）。腿底本地 y=−0.15 贴 collision 底面（halfH=0.15 → offset=0）。walkPhase 驱动短腿摆动
//     （缩 0.6 幅度，虫类快步频）。hostile → EntityManager AI 默认 aiHostile 近战追击玩家。要塞银鱼刷怪笼
//     （Spawner state 带 SpawnerStateSilverfishFlag）周期刷出。眼由 Main.qml delegate 补（2 颗黑点）。
//     mob_silverfish 贴图：灰白甲壳 + 体节横纹（build_mob.py 程序生成原创像素图，§9a 区隔不照搬 MC）。
//   12 = SnowGolem（雪傀儡；feat）：防御造物——**柱身两雪块**上下堆叠（机制等价 MC 1.0 雪傀儡雪块身）。
//     几何仅含雪块身（底块心 y=−0.45 / 顶块心 y=+0.45，各半 0.45 → 腿底本地 y=−0.90 贴 collision 底面 halfH=0.90）。
//     **南瓜头（+ 刻面眼/嘴）不在本几何** —— 由 Main.qml delegate 补独立南瓜头 Model（t582 起走 BlockCube{blockId:100}
//     + 共享图集 pumpkin 瓦片 = 真南瓜贴图头，宽 0.50 比顶雪块 0.60 小一截；旧纯色橙 UnitCube 被用户读作
//     「没有南瓜的头」）。局部原点 = 碰撞中心（mobModelYOff=0，区别于猪牛羊
//     的「躯干中心」）。pack 命中 snow_golem.png → 6 面 T 字 UV 展开进雪块身；pack 关 → 全脸 UV + 纯色雪白
//     （无程序生成贴图）。shearSnowGolem 剪南瓜头后南瓜头 / 眼 / 嘴悬浮原位（无头 derpy 形态，几何不动）。
//   13 = IronGolem（铁傀儡；feat）：防御造物——**铁块人形**（双腿 + 宽躯干 + 双长臂，机制等价 MC 1.0 铁傀儡
//     T 形铁块身）。几何含 5 铁块盒：双腿心 y=−0.90 半 (0.18,0.30,0.18)（腿底本地 y=−1.20 贴 collision 底面
//     halfH=1.20）+ 躯干心 y=+0.05 半 (0.475,0.525,0.325) + 双臂心 y=+0.10 半 (0.14,0.39,0.225)。**南瓜头 +
//     刻面眼不在本几何**（同 SnowGolem，由 Main.qml delegate 补）。pack 命中 iron_golem.png → 铁纹（修 dev-plan C
//     「铁傀儡全白」：程序纯色铁灰在用户视角读作「白」，pack 铁纹才显铁质）；pack 关 → 全脸 UV + 纯色铁灰。
//     局部原点 = 碰撞中心（mobModelYOff=0）。注：原 t483 程序锈斑 Model（深铁灰 + 锈橙斑）在 pack 命中时由
//     pack 铁纹取代（锈纹已是 iron_golem.png 的一部分）；pack 关时回退纯色铁灰 #7d848c（无锈斑）。
// 其余值（含 0 / 越界）→ 兜底按 Pig 建（保几何非空、bounds 合法）。
//
// 顶点格式：pos(3) + uv(2) = 5 float。每盒 6 面 × 4 角 = 24 顶点 / 36 索引；多盒累加。
// UV（两态）：
//   - packTextured=false（默认 / pack 关）：每面铺**整张贴图** [0,1]×[0,1]（同 CrackBox 全脸 UV）→ QML 给每类
//     mob 一张程序生成独占贴图（mob_pig / mob_cow / mob_sheep / mob_shambler / ...）。零回归。
//   - packTextured=true（pack 启用且包内命中 entity PNG）：**R19 C3 实体贴图精确 box-UV** —— 每盒按其 MC 真实
//     textureOffset(u,v) + size(w,h,d)，用 MC ModelRenderer.addBox 自动 UV 的 6 面公式（右/左/顶/底/前/后 从
//     (u0,v0) 起在贴图固定位置铺，见 mobmodel.cpp 注释表）算 6 面 UV 子区，归一化用该 mob 贴图 base 尺寸
//     （64×32 / 64×64 / 128×128；HD 包是 base 整数倍但 UV 分数=MC 像素/base 不变），并把 MC v 向下增 转 Qt
//     V（图像顶↔v=1，见 lessons qml-uv-flip）。UV 对齐**只依赖 (u0,v0)+size，不依赖几何位置** → 本工程 mob 几何
//     为原创方块化、尺寸与 MC 比例不同，但 UV 仍按 MC 原 size 采样 → pack entity 贴图各部（头/身/四肢）对齐。
//     替换旧「T 字格子游标」粗略映射（旧版游标逐盒前进无视 box 真实 texOffs → pack 贴图采样全错位）。
//     各 mobType 的 MC texOffs/size 数据源：U1 调研报告（MC Java 1.8 ModelRenderer 实测，MinecraftConsoles TU19 移植）。
//     贴图文件由 QML 据 ResourcePackManager.mobTextureSource(mobType) 切换（运行期读本地 gitignored pack PNG，红线 §9）。
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
    // pack 是否用 pack entity 贴图（MC box-UV 精确采样，R19 C3）；pack 关 / 包内无贴图 → false（全脸 UV + 程序生成贴图）。
    Q_PROPERTY(bool packTextured READ packTextured WRITE setPackTextured NOTIFY packTexturedChanged)

public:
    explicit MobModel(QQuick3DObject *parent = nullptr);

    int mobType() const { return m_mobType; }
    void setMobType(int type);

    float walkPhase() const { return m_walkPhase; }
    void setWalkPhase(float phase);

    float headPitch() const { return m_headPitch; }
    void setHeadPitch(float pitch);

    bool packTextured() const { return m_packTextured; }
    void setPackTextured(bool on);

signals:
    void mobTypeChanged();
    void walkPhaseChanged();
    void headPitchChanged();
    void packTexturedChanged();

private:
    void rebuild(); // 按 m_mobType / m_walkPhase / m_headPhase 选比例 + 动画角度建多盒几何。

    int m_mobType = 1;    // 默认猪（合法非空，防未设 mobType 时空几何）
    float m_walkPhase = 0.0f; // 行走相位（弧度）；sin 驱动腿摆
    float m_headPitch = 0.0f; // 头部俯仰（弧度，负=低头）；0 → 头走轴对齐快路径
    bool m_packTextured = false; // pack entity 贴图（MC box-UV 精确采样，R19 C3）；false → 全脸 UV（程序生成贴图）
};

#endif // MOBMODEL_H
