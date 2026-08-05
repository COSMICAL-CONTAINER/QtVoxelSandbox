#include "mobmodel.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32

#include <algorithm> // std::min/max
#include <cmath>     // std::sin, std::cos
#include <vector>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节。每面铺整张贴图 [0,1]×[0,1]（全脸 UV，同 CrackBox）。
namespace {
struct MobVtx {
    float x, y, z;
    float u, v;
};

// 6 面角点（与 blockcube.cpp kFaceCorners 同序：+X -X +Y -Y +Z -Z）。每面 4 角从外侧看 CCW（叉积 = 外法线，
// 默认 backface 剔除下可见）。角点用符号三元组 (sx,sy,sz) ∈ {-1,+1} 表达 → 由 addBox 据盒心 + 半长缩放。
// (u,v) ∈ {0,1}² 全脸铺贴图（与 CrackBox 同 UV 方案；与 blockcube cu/cv 同映射，但 u0=0/u1=1 整张非子区）。
// 推导自 blockcube kFaceCorners（kH→+1、-kH→-1），保绕序 / 法线一致 → 与 Main.qml center 摆位自洽。
struct Sgn { int sx, sy, sz; float u, v; };
const Sgn kFace[6][4] = {
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
    // -Z（前；头朝此向）
    {{+1, -1, -1, 1, 0}, {-1, -1, -1, 0, 0}, {-1, +1, -1, 0, 1}, {+1, +1, -1, 1, 1}},
};

// t241 腿摆动幅度（弧度，约 29°）。机制等价 MC 四足 mob 腿摆幅（非精确数值复刻）。
constexpr float kLegSwingAmp = 0.5f;

// 追加一个轴对齐盒（心 cx,cy,cz；半长 hx,hy,hz）到 verts/idx，全脸 UV。每面 4 角 + 2 三角，24 顶点 / 36 索引。
// base = 当前进度顶点数；索引以 base 为偏移写入。（与 hoe.cpp / pickaxe.cpp addBox 同实现思路，仅多 uv 通道。）
// 同时累计 bMin/bMax（局部 AABB，供 setBounds 给视锥剔除盒）。
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<MobVtx> &verts, std::vector<quint32> &idx,
            QVector3D &bMin, QVector3D &bMax)
{
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            MobVtx vt;
            vt.x = cx + float(s.sx) * hx;
            vt.y = cy + float(s.sy) * hy;
            vt.z = cz + float(s.sz) * hz;
            vt.u = s.u;
            vt.v = s.v;
            verts.push_back(vt);
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
    // 累计局部 AABB（盒 8 角的最小 / 最大）。
    bMin.setX(std::min(bMin.x(), cx - hx));
    bMin.setY(std::min(bMin.y(), cy - hy));
    bMin.setZ(std::min(bMin.z(), cz - hz));
    bMax.setX(std::max(bMax.x(), cx + hx));
    bMax.setY(std::max(bMax.y(), cy + hy));
    bMax.setZ(std::max(bMax.z(), cz + hz));
}

// t241 追加一个**绕过 (pivY, pivZ) 的 X 轴旋转**的盒（腿摆动 / 头部俯仰用）。X 轴旋转 → x 分量不变
//   （故无 pivX 参数）；仅 y/z 两轴在 Y-Z 平面内绕 pivot 旋转。每角顶点旋转后写入；bounds 用旋转后顶点的
//   实际范围累计（旋转改变 y/z 区间，addBox 的解析 AABB 不再适用）。angle=0 时几何等价 addBox（cos=1/sin=0），
//   但仍走 cos/sin 路径——调用方对恒 0 角度（猪/牛头）应走 addBox 快路径（见 addHeadRot）。
void addBoxRot(float cx, float cy, float cz, float hx, float hy, float hz,
               float pivY, float pivZ, float angle,
               std::vector<MobVtx> &verts, std::vector<quint32> &idx,
               QVector3D &bMin, QVector3D &bMax)
{
    const float ca = std::cos(angle), sa = std::sin(angle);
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            MobVtx vt;
            vt.x = cx + float(s.sx) * hx;                       // X 轴旋转 → x 不变
            const float ly = cy + float(s.sy) * hy;
            const float lz = cz + float(s.sz) * hz;
            const float dy = ly - pivY;
            const float dz = lz - pivZ;
            vt.y = pivY + dy * ca - dz * sa;                     // X 轴旋转（右手）：y' = y·cos − z·sin
            vt.z = pivZ + dy * sa + dz * ca;                     //             z' = y·sin + z·cos
            vt.u = s.u;
            vt.v = s.v;
            verts.push_back(vt);
            // 旋转后实际范围（逐顶点；解析 AABB 对旋转盒不再适用）。
            bMin.setX(std::min(bMin.x(), vt.x)); bMax.setX(std::max(bMax.x(), vt.x));
            bMin.setY(std::min(bMin.y(), vt.y)); bMax.setY(std::max(bMax.y(), vt.y));
            bMin.setZ(std::min(bMin.z(), vt.z)); bMax.setZ(std::max(bMax.z(), vt.z));
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}

// t302 Z 轴旋转盒（蜘蛛腿步态用；镜像 addBoxRot 的 X 轴旋转，但绕 Z 轴 → z 分量不变，x/y 在 X-Y 平面旋转）。
//   用途：蜘蛛腿是横向盒（半长在 X = 向躯干外侧延伸），addBoxRot 的 X 轴旋转对它几乎无效（y/z 分量小，
//   旋转主要在 Y-Z 平面里晃，腿看不出动）→ 需 Z 轴旋转才能让 outer 端上下抬起（步态）。pivot (pivX, pivY)
//   = 腿内端（髋 = 腿与躯干侧面相接处），angle > 0 把 +X 端向上转、−X 端向下转（右手定则）。
//   与 addBoxRot 同实现骨架（6 面 × 4 角 + 累计 bounds 用旋转后顶点实际范围），仅旋转轴不同。
void addBoxRotZ(float cx, float cy, float cz, float hx, float hy, float hz,
                float pivX, float pivY, float angle,
                std::vector<MobVtx> &verts, std::vector<quint32> &idx,
                QVector3D &bMin, QVector3D &bMax)
{
    const float ca = std::cos(angle), sa = std::sin(angle);
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            MobVtx vt;
            const float lx = cx + float(s.sx) * hx;
            const float ly = cy + float(s.sy) * hy;
            vt.z = cz + float(s.sz) * hz;                       // Z 轴旋转 → z 不变
            const float dx = lx - pivX;
            const float dy = ly - pivY;
            vt.x = pivX + dx * ca - dy * sa;                     // Z 轴旋转（右手）：x' = x·cos − y·sin
            vt.y = pivY + dx * sa + dy * ca;                     //             y' = x·sin + y·cos
            vt.u = s.u;
            vt.v = s.v;
            verts.push_back(vt);
            // 旋转后实际范围（逐顶点；解析 AABB 对旋转盒不再适用）。
            bMin.setX(std::min(bMin.x(), vt.x)); bMax.setX(std::max(bMax.x(), vt.x));
            bMin.setY(std::min(bMin.y(), vt.y)); bMax.setY(std::max(bMax.y(), vt.y));
            bMin.setZ(std::min(bMin.z(), vt.z)); bMax.setZ(std::max(bMax.z(), vt.z));
        }
        const quint32 b = base + quint32(f * 4);
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}

// t241 头部盒（+ 牛角等头附肢）：pitch==0 走 addBox 轴对齐快路径（猪 / 牛 / 非吃草态羊，每帧不进旋转），
//   pitch!=0 走 addBoxRot 绕颈附着点（头后侧面心 = (cx, cy, cz+hz)，头与躯干相接处）旋转。
//   pitch<0 = 低头（吃草）。负值使头前下部下沉（muzzle 朝地），机制等价 MC 羊吃草低头姿态。
void addHeadRot(float cx, float cy, float cz, float hx, float hy, float hz, float pitch,
                std::vector<MobVtx> &verts, std::vector<quint32> &idx,
                QVector3D &bMin, QVector3D &bMax)
{
    if (pitch == 0.0f) { // 恒 0（猪/牛）→ 轴对齐快路径，免 cos/sin
        addBox(cx, cy, cz, hx, hy, hz, verts, idx, bMin, bMax);
        return;
    }
    // 颈附着点 = 头盒后侧面心（+Z 侧 = 朝躯干侧）：(cx, cy, cz+hz)。
    addBoxRot(cx, cy, cz, hx, hy, hz, cy, cz + hz, pitch, verts, idx, bMin, bMax);
}

// t241 四条腿 walk cycle：绕各腿髋部（腿盒顶 = 盒心 y + 半高 legHy）做 X 轴摆动。
//   对角配对（机制等价四足行走）：前左(−X,−Z) + 后右(+X,+Z) 同相 +sw；前右(+X,−Z) + 后左(−X,+Z) 反相 −sw。
//   sw = kLegSwingAmp·sin(walkPhase)。walkPhase 由 EntityManager 据移动速度推进（moveSpeed>0 时）。
//   腿盒 pivot.x = 腿盒中心 x（X 轴旋转不依赖 pivot.x，故 addBoxRot 签名无 pivX；这里隐式 = 各腿 cx）。
void addLegs(float legY, float legHy, float legOffX, float legOffZ, float legW,
             float walkPhase,
             std::vector<MobVtx> &verts, std::vector<quint32> &idx,
             QVector3D &bMin, QVector3D &bMax)
{
    const float hipTopY = legY + legHy; // 髋部 = 腿盒顶面 y（摆动轴位置）
    const float sw = kLegSwingAmp * std::sin(walkPhase);
    addBoxRot(-legOffX, legY, -legOffZ, legW, legHy, legW, hipTopY, -legOffZ, +sw, verts, idx, bMin, bMax); // 前左
    addBoxRot( legOffX, legY, -legOffZ, legW, legHy, legW, hipTopY, -legOffZ, -sw, verts, idx, bMin, bMax); // 前右
    addBoxRot(-legOffX, legY,  legOffZ, legW, legHy, legW, hipTopY,  legOffZ, -sw, verts, idx, bMin, bMax); // 后左
    addBoxRot( legOffX, legY,  legOffZ, legW, legHy, legW, hipTopY,  legOffZ, +sw, verts, idx, bMin, bMax); // 后右
}

// t302 Spider 八腿（机制等价 MC 1.0 蜘蛛 8 腿；4 对沿躯干长度 Z 分布，每对 = 左(−X) + 右(+X)）。
//   每条腿 = 从躯干侧面伸出的横向盒（半长在 X = 向外延伸），绕躯干侧面髋枢（腿内端 = bodyHalfX 处）
//   做 Z 轴旋转（addBoxRotZ）→ outer 端上下抬起 = 步态。baseDown 静态下倾角让腿略向外下伸（蜘蛛典型姿态，
//   非水平直伸），walkPhase 驱动四对腿的 tetropod 交替步态（前后对同相、中两对反相；左 + 右 镜像同步）。
//   t285 原 Spider 简化 4 腿（addLegs 垂直短桩，藏在躯干下不可见 → 用户观感「无腿像蟑螂」）；t302 升级为
//   8 条明显外伸的腿 + 步态。机制对齐 MC 1.0 蜘蛛 8 腿爬行；标识符 / 几何全原创（§9 区隔）。
//   legReachPivotY = 躯干侧面髋枢 Y（= 躯干中心 Y）；腿底最远点 ~ −0.30 贴 collision 底面（halfH=0.30）。
void addSpiderLegs(float bodyHalfX, float pivotY, float walkPhase,
                   std::vector<MobVtx> &verts, std::vector<quint32> &idx,
                   QVector3D &bMin, QVector3D &bMax)
{
    constexpr float kBaseDown         = 0.60f; // 腿静态外端下倾角（弧度，约 34°；蜘蛛腿外伸 + 下伸姿态）
    constexpr float kSpiderLegSwing   = 0.25f; // 步态摆幅（弧度，约 14°；小于四足 kLegSwingAmp 因 8 腿密度高、防互撞）
    constexpr float kLegHalfX         = 0.18f; // 腿半长（X 向躯干外延伸量）
    constexpr float kLegHalfY         = 0.045f;// 腿粗细（Y）
    constexpr float kLegHalfZ         = 0.05f; // 腿粗细（Z）
    constexpr float kLegCenterOffX    = 0.18f; // 腿心相对躯干侧面 bodyHalfX 的外延中点（= kLegHalfX → 内端贴 bodyHalfX）
    // 四对腿沿躯干 Z 分布（前→后）；相邻对步态反相（tetrapod gait：前+后对同相、中两对反相；左 + 右 镜像同相，
    //   即同对两腿同步抬起 / 落下，对间交替 → 八腿整体呈「波浪式」步态，机制等价 MC 蜘蛛爬行）。
    struct LegPair { float z; float pairSign; };
    const LegPair pairs[4] = {
        {-0.20f, +1.0f},
        {-0.07f, -1.0f},
        {+0.07f, -1.0f},
        {+0.20f, +1.0f},
    };
    for (const LegPair &p : pairs) {
        const float sw = kSpiderLegSwing * std::sin(walkPhase) * p.pairSign;
        // +X（右）腿：base 角 −kBaseDown（外端下倾）；+ sw 抬起（角趋 0 = 外端升高）。髋枢 = 躯干右侧 (+bodyHalfX, pivotY)。
        addBoxRotZ( bodyHalfX + kLegCenterOffX, pivotY, p.z, kLegHalfX, kLegHalfY, kLegHalfZ,
                    bodyHalfX, pivotY, -kBaseDown + sw, verts, idx, bMin, bMax);
        // −X（左）腿：镜像（base 角 +kBaseDown，sw 反号 → 与右腿同对同步抬起 / 落下，左右对称）。
        addBoxRotZ(-(bodyHalfX + kLegCenterOffX), pivotY, p.z, kLegHalfX, kLegHalfY, kLegHalfZ,
                   -bodyHalfX, pivotY, kBaseDown - sw, verts, idx, bMin, bMax);
    }
}
} // namespace

MobModel::MobModel(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 mobType=Pig 建；QML 设 mobType / walkPhase / headPitch 时再 rebuild
}

void MobModel::setMobType(int type)
{
    // 0（测试生物）/ 越界 → 兜底 Pig（保几何非空、bounds 合法；Main.qml 对 mobType 0 仍走 UnitCube，
    //   不进本类，故此处兜底仅防误设）。合法 mobType：1 猪 / 2 牛 / 3 羊 / 4 Shambler(僵尸) /
    //   5 Bones(骷髅) / 6 Stalker(苦力怕) / 7 Spider(蜘蛛)。修：原仅接 1-5 且误标 5=Stalker（实际 enum 5=Bones）。
    if (type != 1 && type != 2 && type != 3 && type != 4 && type != 5 && type != 6 && type != 7) type = 1;
    if (type == m_mobType) return;
    m_mobType = type;
    emit mobTypeChanged();
    rebuild();
}

// t241 行走相位 setter：值未变早退（idle 时 EntityManager 返回同一 float → 不触发 rebuild）；
//   变化则 rebuild 把腿摆到新角度。QML 绑定 `{revision; walkPhaseAt(i)}` 在 revision bump 时重算。
void MobModel::setWalkPhase(float phase)
{
    if (phase == m_walkPhase) return;
    m_walkPhase = phase;
    emit walkPhaseChanged();
    rebuild();
}

// t241 头部俯仰 setter：同上早退；仅羊吃草周期内非零（headPitchAt 据吃草进度返 sin(πp) 包络）。
void MobModel::setHeadPitch(float pitch)
{
    if (pitch == m_headPitch) return;
    m_headPitch = pitch;
    emit headPitchChanged();
    rebuild();
}

// 按 m_mobType 选比例建「躯干 + 头（俯仰）+ 4 腿（摆动）（+ 牛角随头转）」多盒几何；t282 加 Shambler 人形分支。
// 局部原点 = 躯干中心；头朝 -Z（前）。比例经手调使每种 mob 在 ~1×1×1 碰撞立方（EntityManager radius=0.5）内可辨：
//   - 猪：紧凑低矮、短腿、大头；   - 牛：高大长身 + 头顶两小角盒；  - 羊：圆胖躯干、小头、短腿。
//   - Shambler（t282）：方块化人形（躯干 + 头 + 双臂前伸 + 双腿），碰撞 halfH=0.90（1.8 高）→ 腿底本地 y=−0.90
//     （mobModelYOff=0.90 − halfH=0 → 模型无 Y 偏移、腿底贴 collision 底面 = 脚位）。机制等价 MC 1.0 僵尸形态。
// 全脸 UV → 各盒铺同张贴图；QML 据 mobType 选 mob_pig / mob_cow / mob_sheep / mob_shambler。
// t241：腿走 addLegs（walkPhase 驱动对角摆动）；头走 addHeadRot（headPitch=0 → 快路径；非 0 → 绕颈俯仰）。
// t282：Shambler 腿走 addBoxRot 双腿绕髋左右反相摆动（biped walk cycle）；双臂 addBox 前伸固定（僵尸姿态）。
void MobModel::rebuild()
{
    std::vector<MobVtx> verts;
    std::vector<quint32> idx;
    verts.reserve(10 * 24); // 至多 Spider 躯干+头+8腿 = 10 盒；其余 mob ≤8 盒（猪/牛/羊/Shambler/Bones/Stalker）
    idx.reserve(10 * 36);
    QVector3D bMin(1e9f, 1e9f, 1e9f), bMax(-1e9f, -1e9f, -1e9f);

    if (m_mobType == 4) {
        // t282 Shambler（蹒跚者；机制等价 MC 1.0 僵尸，§9 区隔改名 + 原创模型/贴图）：
        // 方块化人形 —— 躯干 + 头 + 双臂前伸（僵尸经典攻击姿态）+ 双腿绕髋做 biped walk cycle（左右反相）。
        // 局部原点 = 躯干中心（同猪牛羊约定）；头朝 -Z（前 = AI 行走方向）；腿底本地 y=−0.90 贴 collision 底面。
        // 双臂前伸固定（不走 walkPhase —— 僵尸手臂僵直前举的标志性姿态；腿摆即可传达行走，机制等价 MC 僵尸）。
        // 腿绕髋（腿顶 y=−0.25 = 躯干底）X 轴摆动；左右反相（biped walk cycle，区别于四足 addLegs 的对角配对）。
        addBox( 0.00f,  0.05f,  0.00f, 0.22f, 0.30f, 0.12f, verts, idx, bMin, bMax); // 躯干（心略上移让腿更长）
        addBox( 0.00f,  0.57f,  0.00f, 0.22f, 0.22f, 0.22f, verts, idx, bMin, bMax); // 头（躯干顶上方）
        addBox(-0.33f,  0.23f, -0.37f, 0.10f, 0.10f, 0.25f, verts, idx, bMin, bMax); // 左臂前伸（-X、-Z 前）
        addBox( 0.33f,  0.23f, -0.37f, 0.10f, 0.10f, 0.25f, verts, idx, bMin, bMax); // 右臂前伸（+X、-Z 前）
        const float sw = kLegSwingAmp * std::sin(m_walkPhase);
        const float hipY = -0.25f; // 髋枢 = 腿顶（= 躯干底面 y）
        addBoxRot(-0.11f, -0.575f, 0.00f, 0.11f, 0.325f, 0.12f, hipY, 0.00f, +sw, verts, idx, bMin, bMax); // 左腿
        addBoxRot( 0.11f, -0.575f, 0.00f, 0.11f, 0.325f, 0.12f, hipY, 0.00f, -sw, verts, idx, bMin, bMax); // 右腿
    } else if (m_mobType == 5) {
        // t287/t301 Bones（骸骨；机制等价 MC 1.0 骷髅，§9 区隔改名）—— 瘦骨嶙峋人形（窄躯干 + 小头骨 + 细骨杆四肢）
        //   + 右手持弓（t301：原创弧形弓几何）。修：原 mobType 5 误标为 Stalker，且 Main.qml 把 Bones(5) 路由到
        //   UnitCube 致「白方块」—— t287 改走 MobModel 人形；t301 进一步把比例从 Shambler 充血人形细化为骷髅比例 +
        //   持弓视觉。远程射箭（t283 aiArcher）由 EntityManager 负责；几何仅形态 + 持弓。
        // t301 比例（vs Shambler 同位的 0.22 / 0.22³ / 0.10 / 0.11）：窄躯干（halfX 0.14 vs 0.22）+ 小头骨
        //   （0.16×0.18×0.16 略竖 vs 0.22³）+ 细骨杆四肢（臂 0.05 vs 0.10、腿 0.06 vs 0.11）→ 「皮包骨」骷髅观感，
        //   明显区别于 Shambler 厚实人形。眼窝由 Main.qml delegate 补（黑色空洞，区别 Shambler 赤红亡灵眼）。
        addBox( 0.00f,  0.05f,  0.00f, 0.14f, 0.30f, 0.10f, verts, idx, bMin, bMax); // 窄躯干（瘦骨）
        addBox( 0.00f,  0.57f,  0.00f, 0.16f, 0.18f, 0.16f, verts, idx, bMin, bMax); // 小头骨（略竖，比 Shambler 头小一圈）
        addBox(-0.20f,  0.23f, -0.37f, 0.05f, 0.05f, 0.25f, verts, idx, bMin, bMax); // 左臂（细骨杆前伸）
        addBox( 0.20f,  0.23f, -0.37f, 0.05f, 0.05f, 0.25f, verts, idx, bMin, bMax); // 右臂（细骨杆前伸，持弓）
        const float sw5 = kLegSwingAmp * std::sin(m_walkPhase);
        addBoxRot(-0.07f, -0.575f, 0.00f, 0.06f, 0.325f, 0.06f, -0.25f, 0.00f, +sw5, verts, idx, bMin, bMax); // 左腿（细骨杆）
        addBoxRot( 0.07f, -0.575f, 0.00f, 0.06f, 0.325f, 0.06f, -0.25f, 0.00f, -sw5, verts, idx, bMin, bMax); // 右腿（细骨杆）
        // t301 弓体（持于右手前，垂直弧形；机制等价 MC 1.0 骷髅持弓，§9 区隔原创几何）。弓平面 = Y-Z 平面（含箭
        //   飞行方向 -Z 与垂直 Y）。弓背中段（握把）垂直立于右手前，上 / 下肢绕握把端向 +Z（朝射手）回弯成典型
        //   C 形；弓弦贴弓背 +Z 侧（凹侧 = 射手侧）。弓 X = 右手位 → 弓随整体 yaw 转（QML eulerRotation.y）始终在
        //   右手前。与 BowGeometry（玩家手持弓）同设计语言（握把 + 上 / 下肢 + 弓弦），更简（4 盒 vs 6 盒）——
        //   mob 第三人称远观，曲线近似要求低于玩家手持第一人称；弓共用 MobModel 单材质 → 同灰白骨色（骨弓）。
        constexpr float kBowX  = 0.22f;    // 弓平面 X（右手位 + 0.02 偏移免与臂 z-fight）
        constexpr float kBowY  = 0.22f;    // 握把 Y（与右手齐平，避开头部）
        constexpr float kBowZ  = -0.50f;   // 握把 Z（前伸于右手前段）
        constexpr float kBowTh = 0.025f;   // 弓体半厚（X / Z，瘦薄显骨弓）
        constexpr float kBowA  = 0.50f;    // 弓肢回弯角（弧度 ≈ 29°；tip 朝射手偏移量）
        addBox(kBowX, kBowY, kBowZ, kBowTh, 0.06f, kBowTh, verts, idx, bMin, bMax);          // 握把（中段）
        addBoxRot(kBowX, kBowY + 0.10f, kBowZ, kBowTh, 0.09f, kBowTh,
                  kBowY + 0.06f, kBowZ, +kBowA, verts, idx, bMin, bMax);                     // 上肢（向 +Z 回弯）
        addBoxRot(kBowX, kBowY - 0.10f, kBowZ, kBowTh, 0.09f, kBowTh,
                  kBowY - 0.06f, kBowZ, -kBowA, verts, idx, bMin, bMax);                     // 下肢（向 +Z 回弯）
        addBox(kBowX, kBowY, kBowZ + 0.06f, 0.012f, 0.16f, 0.012f, verts, idx, bMin, bMax);  // 弓弦（射手侧 +Z）
    } else if (m_mobType == 6) {
        // t284 Stalker（潜行者；机制等价 MC 1.0 苦力怕，§9 区隔改名）—— 四短粗腿 + 高瘦躯干 + 小头。
        //   修：原误标 mobType 5（与 Bones 冲突），已正为 6（enum MobStalker=6）。腿底本地 y=−0.90 贴 collision 底面。
        //   蓄力膨胀由 QML delegate 据 inflateAt 对整个 Model 做 scale（1.0+inflate·0.5）驱动，几何本身不参与。
        addBox( 0.00f,  0.08f,  0.00f, 0.18f, 0.42f, 0.16f, verts, idx, bMin, bMax); // 高瘦躯干
        addBox( 0.00f,  0.66f,  0.00f, 0.15f, 0.15f, 0.15f, verts, idx, bMin, bMax); // 小头
        addLegs(-0.62f, 0.28f, 0.12f, 0.09f, 0.09f, m_walkPhase, verts, idx, bMin, bMax); // 四短粗腿
    } else if (m_mobType == 7) {
        // t285/t302 Spider（蜘蛛；机制等价 MC 1.0 蜘蛛，§9 区隔）—— 宽矮躯干 + 前伸小头 + **8 腿**（4 对沿躯干
        //   Z 分布，t302 升级自 t285 简化 4 腿）。腿底本地 y ≈ −0.30 贴 collision 底面（EntityManager halfH=0.30）。
        //   爬墙留后续（t285 spec 未含；本任务只做模型 + 步态动画）。8 腿绕躯干侧面髋枢做 Z 轴步态摆动
        //   （addSpiderLegs），baseDown 外端下倾 + walkPhase 驱动 tetrapod 交替步态。眼由 Main.qml delegate 补
        //   （4 颗红眼，同猪/牛/羊纯色子 Model 模式）。声音 / 受击音由 AudioManager.playMobAmbient/playMobHurt
        //   据 mobType=7 路由（t294 mob_idle_spider 已就绪，本任务复用）。
        addBox( 0.00f, -0.05f,  0.00f, 0.40f, 0.18f, 0.30f, verts, idx, bMin, bMax); // 宽矮躯干
        addBox( 0.00f, -0.02f, -0.32f, 0.18f, 0.14f, 0.18f, verts, idx, bMin, bMax); // 小头（前伸）
        addSpiderLegs(0.40f, -0.05f, m_walkPhase, verts, idx, bMin, bMax);           // 8 腿（4 对，Z 轴步态）
    } else if (m_mobType == 2) {
        // 牛：高大长身 + 头顶两小角盒（角随头俯仰；牛 headPitch 恒 0 → 实走快路径不动）。机制等价 MC 牛形态。
        addBox(0.00f, 0.05f, 0.00f, 0.32f, 0.28f, 0.55f, verts, idx, bMin, bMax); // 躯干（长）
        // 头 + 双角共享颈附着点（cy, cz+hz）→ headPitch 驱动时整组随头俯仰。
        addHeadRot(0.00f, 0.15f, -0.60f, 0.20f, 0.22f, 0.20f, m_headPitch, verts, idx, bMin, bMax); // 头（前伸）
        addHeadRot(-0.22f, 0.34f, -0.58f, 0.05f, 0.06f, 0.05f, m_headPitch, verts, idx, bMin, bMax); // 左角
        addHeadRot( 0.22f, 0.34f, -0.58f, 0.05f, 0.06f, 0.05f, m_headPitch, verts, idx, bMin, bMax); // 右角
        addLegs(-0.30f, 0.20f, 0.20f, 0.35f, 0.10f, m_walkPhase, verts, idx, bMin, bMax); // 4 长腿
    } else if (m_mobType == 3) {
        // 羊：圆胖躯干、小头、短腿。机制等价 MC 羊形态（非名词照搬）。
        addBox(0.00f, 0.05f, 0.00f, 0.30f, 0.28f, 0.42f, verts, idx, bMin, bMax); // 躯干（圆胖）
        addHeadRot(0.00f, 0.10f, -0.45f, 0.14f, 0.16f, 0.16f, m_headPitch, verts, idx, bMin, bMax); // 小头（吃草时俯仰）
        addLegs(-0.28f, 0.16f, 0.18f, 0.26f, 0.09f, m_walkPhase, verts, idx, bMin, bMax); // 4 短腿
    } else {
        // 猪（默认 / 兜底）：紧凑低矮、短腿、大头。机制等价 MC 猪形态（非名词照搬）。
        addBox(0.00f, 0.00f, 0.00f, 0.35f, 0.22f, 0.45f, verts, idx, bMin, bMax); // 躯干（低矮）
        addHeadRot(0.00f, 0.05f, -0.50f, 0.22f, 0.22f, 0.18f, m_headPitch, verts, idx, bMin, bMax); // 大头（前伸）
        addLegs(-0.30f, 0.18f, 0.22f, 0.28f, 0.10f, m_walkPhase, verts, idx, bMin, bMax); // 4 短腿
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    // 漏 update() 后端不上传 GPU；漏 setIndexData 则 idx 数组未用（-Wunused 警告）且
    // 索引永不进 GPU —— IndexSemantic 只声明布局，数据须 setIndexData 单独上传（同 BlockCube/Hoe/Pickaxe）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(MobVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(MobVtx)));
    setBounds(bMin, bMax); // 局部 AABB（配合 Model 变换给视锥剔除盒；含旋转后的腿 / 头实际范围）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(MobVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(MobVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
