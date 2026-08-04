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
} // namespace

MobModel::MobModel(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 mobType=Pig 建；QML 设 mobType / walkPhase / headPitch 时再 rebuild
}

void MobModel::setMobType(int type)
{
    // 0（测试生物）/ 越界 → 兜底 Pig（保几何非空、bounds 合法；Main.qml 对 mobType 0 仍走 UnitCube，
    //   不进本类，故此处兜底仅防误设）。t282：mobType 4 = Shambler（人形）合法。
    if (type != 1 && type != 2 && type != 3 && type != 4) type = 1;
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
    verts.reserve(8 * 24); // 至多躯干+头+4腿+2角 = 8 盒（Shambler 6 盒在此内）
    idx.reserve(8 * 36);
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
