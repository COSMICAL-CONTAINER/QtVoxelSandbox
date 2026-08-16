#include "mobmodel.h"

#include <QByteArray>
#include <QVector3D>
#include <QtGlobal> // quint32
#include <QtMath>   // qDegreesToRadians（aimPitch 度 → 弧度）

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

// R19 C3：pack entity 贴图精确 box-UV（替换旧「T 字格子游标」粗略映射）。
//   MC ModelRenderer.addBox 自动 UV：对每个 box = (w,h,d)、贴图原点 (u0,v0)（该 ModelPart 的 textureOffset），
//   从原点起在贴图上按**固定顺序**铺 6 面（v 向下增，MC 贴图惯例；像素矩形来自 Cube.cpp 实测 6 个 _Polygon）：
//
//         ┌─────────────┐  v0
//         │   TOP w×d   │  ← (u0+d, v0) → (u0+d+w, v0+d)
//    ┌────┼─────────────┼────┐
//    │LEFT│   FRONT w×h │RIGHT│  ← 侧面 d×h，v 范围 v0+d..v0+d+h
//    │ d×h│             │ d×h │
//    └────┼─────────────┼────┘  v0+d+h
//         │  BOTTOM w×d │  ← (u0+d+w, v0+d) → (u0+d+2w, v0+d)   (底面 v 翻转)
//         ├─────────────┤
//         │   BACK w×h  │  ← (u0+d+w+d, v0+d) → (u0+d+w+d+w, v0+d+h)
//         └─────────────┘
//
//   6 面像素矩形（相对 box 原点 u0,v0；[u1,v1,u2,v2]，v 向下增）—— 面序对齐 kFace[6]：
//     [0] +X Right : [u0+d+w,   v0+d  ] → [u0+d+w+d,     v0+d+h]   (d×h)
//     [1] -X Left  : [u0,        v0+d ] → [u0+d,          v0+d+h]   (d×h)
//     [2] +Y Top   : [u0+d,      v0   ] → [u0+d+w,        v0+d  ]   (w×d)
//     [3] -Y Bot   : [u0+d+w,    v0   ] → [u0+d+w+w,      v0+d  ]   (w×d, v 翻转)
//     [4] +Z Front : [u0+d,      v0+d ] → [u0+d+w,        v0+d+h]   (w×h)
//     [5] -Z Back  : [u0+d+w+d,  v0+d ] → [u0+d+w+d+w,    v0+d+h]   (w×h)
//   本工程 MobModel：+Y 上 / -Z 前 / +X 右，与 MC +X 右 / +Y 上 / +Z 朝后 一致（MC 「头朝 -Z 前」= 本工程前 = MC Back 面 -Z）。
//   即本工程「脸」（头朝 -Z）采 MC 的 Back 面 UV（kFace[5]）—— MC 贴图 Back 面就是脸，故无需翻转轴。
//
// QtQuick3D V 方向（见 lessons qml-uv-flip + Qt 6.11 Texture 段）：默认 flipV=false 时 **图像顶 ↔ v=1**
//   （与 OpenGL 底纹素原点相反，上传时翻转）。MC 贴图约定 v 向下增（row 0 = 图像顶）。故 MC 像素 (ux,uy)
//   → Qt UV：u_qt = ux/texW；v_qt = 1 − uy/texH。本工程按此把 6 面 MC 像素矩形转 Qt UV 后插值。
//
// 全局 UV 上下文（GUI 线程内 MobModel 生命周期，rebuild 串行设置每盒原点；无并发）：
bool   g_packTextured = false;       // pack 关 → 全脸 [0,1]²（kFace 原 u,v）；pack 开 → MC box-UV
float  g_texW = 64.0f, g_texH = 32.0f; // 当前 mob 贴图 base 尺寸（HD 包是 base 整数倍，UV 分数 = MC 像素 / base）
// 当前盒的 MC textureOffset(u0,v0) + size(w,h,d)（MC 像素；rebuild 在每个 addBox 前调 setMobTex 设）。
float  g_boxU0 = 0, g_boxV0 = 0, g_boxW = 1, g_boxH = 1, g_boxD = 1;

// 设当前盒的 MC textureOffset + size（rebuild 在每个 addBox 前 push）。pack 关时 writeMobUV 不读这些。
inline void setMobTex(float u0, float v0, float w, float h, float d)
{
    g_boxU0 = u0; g_boxV0 = v0; g_boxW = w; g_boxH = h; g_boxD = d;
}

// MC 像素 → Qt UV：u 除以贴图宽、v 取「1 − 像素行/贴图高」（图像顶 ↔ Qt v=1）。
inline float mcToQtU(float px) { return px / g_texW; }
inline float mcToQtV(float py) { return 1.0f - py / g_texH; }

// 计算某面 MC box-UV 子区并转 Qt UV。面序同 kFace：0=+X 右、1=-X 左、2=+Y 顶、3=-Y 底、4=+Z、5=-Z（前）。
//   **轴向换算（R19 C3 关键修正，像素验证）**：本工程是左手系（+X 右 / +Y 上 / -Z 前），MC 是右手系
//   （+X 东 / +Y 上 / +Z 南=前）。两者在水平面（X、Z）等价于绕 Y 转 180° → 本工程「前 -Z」对应 MC「前 +Z」
//   （实体贴图的脸在 MC +Z 面，见 creeper head 实测：Front+Z 区(8,8)-(16,16) 有 1570 色=脸，Back-Z 区仅 62 色=后脑），
//   本工程「右 +X」对应 MC「左 -X」（面朝 +Z 前时，MC 右=−X、本工程右=+X）。故 kFace 面 f 须 remap 到 MC 面：
//     +X↔-X（0↔1）、+Z↔-Z（4↔5）、±Y 不变（2、3）。
//   注：U1 §坐标约定注释「本工程前 -Z = MC Back」与像素验证矛盾（MC 脸在 +Z=Front，非 Back），此处以实测为准。
void mobFaceQtUV(int face, float u0, float v0, float w, float h, float d,
                 float &umin, float &vmin, float &umax, float &vmax)
{
    // 本工程 kFace 面 → MC 面（水平 180° remap：+X↔-X、+Z↔-Z）。
    static const int kMcFace[6] = { 1, 0, 2, 3, 5, 4 };
    const int mf = kMcFace[face];
    const float sd  = v0 + d;           // 侧面行 v 起点（MC 像素）
    const float sh  = v0 + d + h;       // 侧面行 v 终点
    float mu0, mv0, mu1, mv1;           // MC 像素矩形（按 MC 面序：0=+X右 1=-X左 2=+Y顶 3=-Y底 4=+Z前 5=-Z后）
    switch (mf) {
    case 0: mu0 = u0 + d + w;   mv0 = sd;  mu1 = u0 + 2*d + w;   mv1 = sh;  break; // MC +X 右（d×h）
    case 1: mu0 = u0;            mv0 = sd;  mu1 = u0 + d;          mv1 = sh;  break; // MC -X 左（d×h）
    case 2: mu0 = u0 + d;        mv0 = v0;  mu1 = u0 + d + w;      mv1 = v0+d; break; // MC +Y 顶（w×d）
    case 3: mu0 = u0 + d + w;    mv0 = v0;  mu1 = u0 + d + 2*w;    mv1 = v0+d; break; // MC -Y 底（w×d）
    case 4: mu0 = u0 + d;        mv0 = sd;  mu1 = u0 + d + w;      mv1 = sh;  break; // MC +Z 前（w×h）= 脸
    case 5: mu0 = u0 + 2*d + w;  mv0 = sd;  mu1 = u0 + 2*d + 2*w;  mv1 = sh;  break; // MC -Z 后（w×h）
    default: mu0 = 0; mv0 = 0; mu1 = float(w); mv1 = float(h);     break;
    }
    // MC 像素 → Qt UV（v 翻：图像顶 ↔ Qt v=1）。注意 vmin/vmax 也随 v 翻转互换大小关系：MC v 大=图像下=Qt v 小。
    umin = mcToQtU(mu0);
    umax = mcToQtU(mu1);
    vmin = mcToQtV(mv1); // MC v 终点（图像更下）→ Qt v 更小
    vmax = mcToQtV(mv0); // MC v 起点（图像更上）→ Qt v 更大
}

// 写入一角的 UV：pack 关 → kFace 全脸 u,v；pack 开 → MC box-UV 子区（本盒 textureOffset + size）插值。
//   s.u/s.v ∈ {0,1}² 是面内角点的归一化坐标（kFace 定义），pack 开时把子区 [umin,umax]×[vmin,vmax] 按 s 插值。
inline void writeMobUV(MobVtx &vt, const Sgn &s, int face, bool pack)
{
    if (!pack) { vt.u = s.u; vt.v = s.v; return; }
    float umin, vmin, umax, vmax;
    mobFaceQtUV(face, g_boxU0, g_boxV0, g_boxW, g_boxH, g_boxD, umin, vmin, umax, vmax);
    vt.u = umin + s.u * (umax - umin);
    vt.v = vmin + s.v * (vmax - vmin);
}

// 追加一个轴对齐盒（心 cx,cy,cz；半长 hx,hy,hz）到 verts/idx，全脸 UV。每面 4 角 + 2 三角，24 顶点 / 36 索引。
// base = 当前进度顶点数；索引以 base 为偏移写入。（与 hoe.cpp / pickaxe.cpp addBox 同实现思路，仅多 uv 通道。）
// 同时累计 bMin/bMax（局部 AABB，供 setBounds 给视锥剔除盒）。
void addBox(float cx, float cy, float cz, float hx, float hy, float hz,
            std::vector<MobVtx> &verts, std::vector<quint32> &idx,
            QVector3D &bMin, QVector3D &bMax)
{
    const quint32 base = quint32(verts.size());
    // R19 C3：本盒 MC textureOffset/size 由调用方在 addBox 前 setMobTex() 设（pack 关时 writeMobUV 不读）。
    for (int f = 0; f < 6; ++f) {
        for (int c = 0; c < 4; ++c) {
            const Sgn &s = kFace[f][c];
            MobVtx vt;
            vt.x = cx + float(s.sx) * hx;
            vt.y = cy + float(s.sy) * hy;
            vt.z = cz + float(s.sz) * hz;
            writeMobUV(vt, s, f, g_packTextured);
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
    // R19 C3：本盒 MC textureOffset/size 由调用方在 addBoxRot 前 setMobTex() 设（pack 关不读）。
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
            writeMobUV(vt, s, f, g_packTextured);
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
    // R19 C3：本盒 MC textureOffset/size 由调用方在 addBoxRotZ 前 setMobTex() 设（pack 关不读）。
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
            writeMobUV(vt, s, f, g_packTextured);
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
//   R19 C3：texU/texV/w/h/d = 腿 MC textureOffset + size（pack 开时 4 腿共用同一 texOffs，MC 四足同框四腿）。
void addLegs(float legY, float legHy, float legOffX, float legOffZ, float legW,
             float texU, float texV, float w, float h, float d,
             float walkPhase,
             std::vector<MobVtx> &verts, std::vector<quint32> &idx,
             QVector3D &bMin, QVector3D &bMax)
{
    const float hipTopY = legY + legHy; // 髋部 = 腿盒顶面 y（摆动轴位置）
    const float sw = kLegSwingAmp * std::sin(walkPhase);
    setMobTex(texU, texV, w, h, d);
    addBoxRot(-legOffX, legY, -legOffZ, legW, legHy, legW, hipTopY, -legOffZ, +sw, verts, idx, bMin, bMax); // 前左
    setMobTex(texU, texV, w, h, d);
    addBoxRot( legOffX, legY, -legOffZ, legW, legHy, legW, hipTopY, -legOffZ, -sw, verts, idx, bMin, bMax); // 前右
    setMobTex(texU, texV, w, h, d);
    addBoxRot(-legOffX, legY,  legOffZ, legW, legHy, legW, hipTopY,  legOffZ, -sw, verts, idx, bMin, bMax); // 后左
    setMobTex(texU, texV, w, h, d);
    addBoxRot( legOffX, legY,  legOffZ, legW, legHy, legW, hipTopY,  legOffZ, +sw, verts, idx, bMin, bMax); // 后右
}

// t302 Spider 八腿（机制等价 MC 1.0 蜘蛛 8 腿；4 对沿躯干长度 Z 分布，每对 = 左(−X) + 右(+X)）。
//   每条腿 = 从躯干侧面伸出的横向盒（半长在 X = 向外延伸），绕躯干侧面髋枢（腿内端 = bodyHalfX 处）
//   做 Z 轴旋转（addBoxRotZ）→ outer 端上下抬起 = 步态。baseDown 静态下倾角让腿略向外下伸（蜘蛛典型姿态，
//   非水平直伸），walkPhase 驱动四对腿的 tetropod 交替步态（前后对同相、中两对反相；左 + 右 镜像同步）。
//   t285 原 Spider 简化 4 腿（addLegs 垂直短桩，藏在躯干下不可见 → 用户观感「无腿像蟑螂」）；t302 升级为
//   8 条明显外伸的腿 + 步态。机制对齐 MC 1.0 蜘蛛 8 腿爬行；标识符 / 几何全原创（§9 区隔）。
//   legReachPivotY = 躯干侧面髋枢 Y（= 躯干中心 Y）；腿底最远点 ~ −0.30 贴 collision 底面（halfH=0.30）。
//   R19 C3：texU/texV/w/h/d = 腿 MC textureOffset(18,0) + size(16,2,2)（MC 蜘蛛腿，8 腿共用同框 texOffs）。
void addSpiderLegs(float bodyHalfX, float pivotY, float walkPhase,
                   float texU, float texV, float w, float h, float d,
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
        setMobTex(texU, texV, w, h, d);
        addBoxRotZ( bodyHalfX + kLegCenterOffX, pivotY, p.z, kLegHalfX, kLegHalfY, kLegHalfZ,
                    bodyHalfX, pivotY, -kBaseDown + sw, verts, idx, bMin, bMax);
        // −X（左）腿：镜像（base 角 +kBaseDown，sw 反号 → 与右腿同对同步抬起 / 落下，左右对称）。
        setMobTex(texU, texV, w, h, d);
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
    //   5 Bones(骷髅) / 6 Stalker(苦力怕) / 7 Spider(蜘蛛) / 8 Chicken(鸡) / 9 Squid(鱿鱼) / 10 Wolf(狼) /
    //   11 Ocelot(豹猫/猫；t481) / 12 SnowGolem(雪傀儡；feat) / 13 IronGolem(铁傀儡；feat) / 14 Silverfish(银鱼；t487)。
    //   修：原仅接 1-5 且误标 5=Stalker（实际 enum 5=Bones）。
    //   注：12 SnowGolem / 13 IronGolem 此前在 Main.qml 用 UnitCube 堆叠（不走 MobModel）；feat 接入资源包实体
    //   贴图（snow_golem.png / iron_golem.png）改走 MobModel（T 字 UV 展开进 pack entity 贴图），南瓜头 / 眼 / 嘴
    //   仍由 Main.qml delegate 补独立 Model（§9 区隔：南瓜头是单独的橙色南瓜模型，非贴图的一部分）。
    if (type != 1 && type != 2 && type != 3 && type != 4 && type != 5 && type != 6 && type != 7 && type != 8 && type != 9 && type != 10 && type != 11 && type != 12 && type != 13 && type != 14) type = 1;
    if (type == m_mobType) return;
    m_mobType = type;
    emit mobTypeChanged();
    rebuild();
}

// t241 行走相位 setter：值未变早退（idle 时 EntityManager 返回同一 float → 不触发 rebuild）；
//   变化则 rebuild 把腿摆到新角度。QML 绑定 `{revision; walkPhaseAt(i)}` 在 revision bump 时重算。
// perf：量化到每周期 12 个离散腿姿（2π/12 ≈ 0.52rad）才 rebuild。旧版每帧每个行走 mob 都 rebuild（全顶点重生成
//   + setVertexData/setIndexData/update GPU 重上传）= mob 卡顿主因（用户实测 mob 22ms 恒定 + ~65ms QML，与视距无关）。
//   腿只需 ~12 姿态在远处即读作平滑行走；量化使 rebuild 频率降 ~3-12×（视步频），无位置/朝向延迟、腿部仅微小阶跃。
void MobModel::setWalkPhase(float phase)
{
    constexpr float kStep = 6.2831853f / 12.0f; // 每周期 12 腿姿（量化粒度；越小越平滑越费，12 为距离可视的平衡）
    const float q = std::round(phase / kStep) * kStep;
    if (q == m_walkPhase) return; // 同一量化格 → 腿姿未变 → 不 rebuild（idle / 帧间微小相位推进）
    m_walkPhase = q;
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

// review M10 右臂瞄准抬起 setter（度）：同上早退；仅 Bones 拉弓瞄准期非零（QML 绑 drawAmount*75，
//   与弓肩枢 Node eulerRotation.x 同值）。值变 → rebuild 把右臂绕肩枢转到新角度（满拉 75° 前伸瞄准）。
void MobModel::setAimPitch(float deg)
{
    if (deg == m_aimPitch) return;
    m_aimPitch = deg;
    emit aimPitchChanged();
    rebuild();
}

// t635 铁傀儡攻击抬臂 setter（0..1，0=垂臂）：同上早退；仅 IronGolem 攻击蓄力期非零（QML 绑
//   golemAttackPoseAt(i)——EntityManager 攻击 windup 进度）。量化到 1/12 步进（同 walkPhase 量化模式，
//   防每帧微变触发 rebuild）；值变 → rebuild 把双臂绕肩枢前抬（−attackPose·120°）。
void MobModel::setAttackPose(float pose)
{
    constexpr float kStep = 1.0f / 12.0f;
    const float q = std::round(pose / kStep) * kStep;
    if (q == m_attackPose) return;
    m_attackPose = q;
    emit attackPoseChanged();
    rebuild();
}

// pack entity 贴图开关 setter（R19 C3）：值变 → rebuild 把每盒 UV 从全脸切到 MC box-UV 精确采样（pack 开）或反之。
//   QML 绑定 `packTextured: mobXxxPackTex.source.toString().length > 0`（pack 切换 / 包内命中与否均刷新）。
void MobModel::setPackTextured(bool on)
{
    if (on == m_packTextured) return;
    m_packTextured = on;
    emit packTexturedChanged();
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
    verts.reserve(16 * 24); // 至多 Bones 镂空骨架 = 14 盒（脊柱+胸骨+8 肋+头+左臂+2 腿）；Spider/Squid = 10；其余 ≤8
    idx.reserve(16 * 36);
    QVector3D bMin(1e9f, 1e9f, 1e9f), bMax(-1e9f, -1e9f, -1e9f);

    // R19 C3：设置 UV 模式（pack 关=全脸 / 开=MC box-UV 精确贴图）。g_texW/H 默认 64×32，各 mob 分支按其贴图
    //   base 尺寸覆写（zombie/snow_golem=64×64、iron_golem=128×128，其余四足/虫=64×32）。pack 关时 writeMobUV
    //   不读 g_texW/H 或 setMobTex 设的 box texOffs（全脸 [0,1]²，零回归）。
    g_packTextured = m_packTextured;
    g_texW = 64.0f;
    g_texH = 32.0f;

    if (m_mobType == 4) {
        // t282 Shambler（蹒跚者；机制等价 MC 1.0 僵尸，§9 区隔改名 + 原创模型/贴图）：
        // 方块化人形 —— 躯干 + 头 + 双臂前伸（僵尸经典攻击姿态）+ 双腿绕髋做 biped walk cycle（左右反相）。
        // 局部原点 = 躯干中心（同猪牛羊约定）；头朝 -Z（前 = AI 行走方向）；腿底本地 y=−0.90 贴 collision 底面。
        // 双臂前伸固定（不走 walkPhase —— 僵尸手臂僵直前举的标志性姿态；腿摆即可传达行走，机制等价 MC 僵尸）。
        // 腿绕髋（腿顶 y=−0.25 = 躯干底）X 轴摆动；左右反相（biped walk cycle，区别于四足 addLegs 的对角配对）。
        // R19 C3 UV（MC Humanoid base 64×64；U1 §4 zombie）：head(0,0)8×8×8 / body(16,16)8×12×4 / arm(40,16)4×12×4 /
        //   leg(0,16)4×12×4。几何盒尺寸保持本工程原创比例，UV 按 MC 原 size 采样 → pack 贴图各部对齐。
        g_texW = 64.0f; g_texH = 64.0f;
        setMobTex(16, 16, 8, 12, 4);
        addBox( 0.00f,  0.05f,  0.00f, 0.22f, 0.30f, 0.12f, verts, idx, bMin, bMax); // 躯干（心略上移让腿更长）
        setMobTex(0, 0, 8, 8, 8);
        addBox( 0.00f,  0.57f,  0.00f, 0.22f, 0.22f, 0.22f, verts, idx, bMin, bMax); // 头（躯干顶上方）
        setMobTex(40, 16, 4, 12, 4);
        addBox(-0.33f,  0.23f, -0.37f, 0.10f, 0.10f, 0.25f, verts, idx, bMin, bMax); // 左臂前伸（-X、-Z 前）
        setMobTex(40, 16, 4, 12, 4);
        addBox( 0.33f,  0.23f, -0.37f, 0.10f, 0.10f, 0.25f, verts, idx, bMin, bMax); // 右臂前伸（+X、-Z 前）
        const float sw = kLegSwingAmp * std::sin(m_walkPhase);
        const float hipY = -0.25f; // 髋枢 = 腿顶（= 躯干底面 y）
        setMobTex(0, 16, 4, 12, 4);
        addBoxRot(-0.11f, -0.575f, 0.00f, 0.11f, 0.325f, 0.12f, hipY, 0.00f, +sw, verts, idx, bMin, bMax); // 左腿
        setMobTex(0, 16, 4, 12, 4);
        addBoxRot( 0.11f, -0.575f, 0.00f, 0.11f, 0.325f, 0.12f, hipY, 0.00f, -sw, verts, idx, bMin, bMax); // 右腿
    } else if (m_mobType == 5) {
        // t287/t301 Bones（骸骨；机制等价 MC 1.0 骷髅，§9 区隔改名）—— 瘦骨嶙峋人形（窄躯干 + 小头骨 + 细骨杆四肢）
        //   + 右手持弓（t301：原创弧形弓几何）。修：原 mobType 5 误标为 Stalker，且 Main.qml 把 Bones(5) 路由到
        //   UnitCube 致「白方块」—— t287 改走 MobModel 人形；t301 进一步把比例从 Shambler 充血人形细化为骷髅比例 +
        //   持弓视觉。远程射箭（t283 aiArcher）由 EntityManager 负责。t331：右臂 + 弓移至 Main.qml（肩枢 Node 子节点）
        //   —— 弓需独立木褐色材质（区别骨白体色，修「骨弓」误色）+ 抬臂/拉弓动画（drawAmount 驱动）；本几何仅剩
        //   躯干 + 头 + 左臂 + 双腿（骨白单材质）。
        // t301 比例（vs Shambler 同位的 0.22 / 0.22³ / 0.10 / 0.11）：窄躯干（halfX 0.14 vs 0.22）+ 小头骨
        //   （0.16×0.18×0.16 略竖 vs 0.22³）+ 细骨杆四肢（臂 0.05 vs 0.10、腿 0.06 vs 0.11）→ 「皮包骨」骷髅观感，
        //   明显区别于 Shambler 厚实人形。眼窝由 Main.qml delegate 补（黑色空洞，区别 Shambler 赤红亡灵眼）。
        // t370 镂空骨架重构：原 t301「窄躯干实体盒」远观像白棍 → 改为「脊柱 + 胸骨 + 多对肋骨（带间隙）」，
        //   远观读作骷髅（肋缝 / 脊柱透出），非实体白块。四肢仍细骨杆、头骨比例 / 挂点不变（t301）→
        //   Main.qml 肩枢 (0.20,0.28,-0.12) 与眼窝仍对齐。躯干本地范围同原（y∈[-0.25,0.35]、x∈[-0.14,0.14]、
        //   z∈[-0.10,0.10]）。机制等价 MC 1.0 骷髅镂空骨架观感（§9 区隔）。
        // R19 C3 UV（MC Skeleton base 64×32；U1 §5 bones）：head(0,0)8×8×8 / body(16,16)8×12×4 / arm(40,16)2×12×2 /
        //   leg(0,16)2×12×2。脊柱=body、胸骨/肋骨=leg 细骨杆 texOffs（贴图全骨白，交叉采样视觉无差）。
        g_texW = 64.0f; g_texH = 32.0f;
        // t594 修（用户「脊柱是黑的」）：脊柱原用 body texOffs(16,16,8,12,4)——其 +Y Top 面区
        //   base(20,16)-(28,20) 全透明（实测 0% 不透明）、±X 侧 41.7%，细脊柱各面把透明区放大成
        //   「黑脊」。改采 leg texOffs(0,16,2,12,2)（胸骨/肋已用，6 面实测 100% 不透明骨白）→ 脊柱显骨色。
        setMobTex(0, 16, 2, 12, 2);
        addBox( 0.00f,  0.05f,  0.04f, 0.035f, 0.28f, 0.035f, verts, idx, bMin, bMax); // 脊柱（垂直主干，略靠背；肋缝透出）
        setMobTex(0, 16, 2, 12, 2);
        addBox( 0.00f,  0.12f, -0.10f, 0.030f, 0.16f, 0.020f, verts, idx, bMin, bMax); // 胸骨（前中竖骨；肋前端汇集）
        //   肋骨：4 对（左/右各 4 根）沿胸高分布，每根横置细骨杆（半 X 大、半 Y 薄），邻肋 y 间隙 ~0.04
        //     → 远观「栅栏状」肋笼（缝隙 = 镂空，区别实体盒；左右肋中央留缝让脊柱 / 胸骨透出）。
        for (float ribY : { 0.25f, 0.18f, 0.11f, 0.04f }) {
            setMobTex(0, 16, 2, 12, 2);
            addBox(-0.07f, ribY, -0.03f, 0.05f, 0.016f, 0.06f, verts, idx, bMin, bMax); // 左肋
            setMobTex(0, 16, 2, 12, 2);
            addBox( 0.07f, ribY, -0.03f, 0.05f, 0.016f, 0.06f, verts, idx, bMin, bMax); // 右肋
        }
        setMobTex(0, 0, 8, 8, 8);
        addBox( 0.00f,  0.57f,  0.00f, 0.16f, 0.18f, 0.16f, verts, idx, bMin, bMax); // 小头骨（略竖，比 Shambler 头小一圈）
        // t616 双臂自然下垂（用户「骷髅弓箭手这个手还是举着的，不太合适」）：旧双臂前伸（z=-0.37 横置
        //   骨杆，僵尸姿态）→ 改垂臂——肩 (±0.20, 0.28) 竖直细骨杆，半高 0.325（臂长 0.65 = 腿长同源，
        //   MC 臂 12px=腿 12px），手端 y=-0.37（略低于髋 -0.25，MC 比例）；z=-0.02 贴躯干侧（肋 ±0.12 之外）。
        //   持弓由 Main.qml 肩枢 Node 挂 MobBowGeometry（弓移到垂手位，瞄准时 drawAmount 抬起）；图鉴预览
        //   （ResourceBrowser）另补静态弓 Model（t598 头补法同族）。t594 双臂保留在本几何（贴图一致）。
        setMobTex(40, 16, 2, 12, 2);
        addBox(-0.20f, -0.045f, -0.02f, 0.05f, 0.325f, 0.05f, verts, idx, bMin, bMax); // 左臂（细骨杆垂下）
        // review M10 右臂瞄准抬起（t616 后弓绕肩枢 Node 转、右臂静态垂下 → 满拉时弓浮离手）：
        //   右臂绕肩枢（臂盒顶心 (0.20,0.28,-0.02)）X 轴旋转 aimPitch（度，QML eulerRotation 同单位同值 =
        //   drawAmount*75），与 Main.qml 弓肩枢 Node 同枢同角 → 刚体耦合（弓握把恒贴手端，t331 原设计恢复）。
        //   aimPitch=0（未瞄准 / 图鉴静态）→ addBox 轴对齐快路径（同 addHeadRot 模式）。
        setMobTex(40, 16, 2, 12, 2);
        if (m_aimPitch == 0.0f) {
            addBox( 0.20f, -0.045f, -0.02f, 0.05f, 0.325f, 0.05f, verts, idx, bMin, bMax); // 右臂（细骨杆垂下，持弓手）
        } else {
            const float aimRad = qDegreesToRadians(m_aimPitch); // 度 → 弧度（addBoxRot 三角入参）
            addBoxRot( 0.20f, -0.045f, -0.02f, 0.05f, 0.325f, 0.05f, 0.28f, -0.02f, aimRad, verts, idx, bMin, bMax); // 右臂（瞄准抬起，手端随弓前伸）
        }
        // t331 弓 + 抬弓动画在 Main.qml 肩枢 Node（MobBowGeometry 木色弓 + drawAmount 抬弓），双臂在本几何。
        const float sw5 = kLegSwingAmp * std::sin(m_walkPhase);
        setMobTex(0, 16, 2, 12, 2);
        addBoxRot(-0.07f, -0.575f, 0.00f, 0.06f, 0.325f, 0.06f, -0.25f, 0.00f, +sw5, verts, idx, bMin, bMax); // 左腿（细骨杆）
        setMobTex(0, 16, 2, 12, 2);
        addBoxRot( 0.07f, -0.575f, 0.00f, 0.06f, 0.325f, 0.06f, -0.25f, 0.00f, -sw5, verts, idx, bMin, bMax); // 右腿（细骨杆）
        // t331 弓移至 Main.qml（MobBowGeometry 木色弓，肩枢 Node 子节点随 drawAmount 抬起）；右臂已在上面本几何。
    } else if (m_mobType == 6) {
        // t284 Stalker（潜行者；机制等价 MC 1.0 苦力怕，§9 区隔改名）—— 四短粗腿 + 宽身 + 大头。
        //   修：原误标 mobType 5（与 Bones 冲突），已正为 6（enum MobStalker=6）。腿底本地 y=−0.90 贴 collision 底面。
        //   蓄力膨胀由 QML delegate 据 inflateAt 对整个 Model 做 scale（1.0+inflate·0.5）驱动，几何本身不参与。
        // t595 修（用户「腿太长缩小 + 头太小 + 身体也小了」）：改到 MC 1.0 creeper 比例（头 8×8×8 = 0.5 格 /
        //   身 4×12×8 = 0.25×0.75×0.5 / 腿 4×6×4 = 0.25×0.375×0.25），身宽略放（保「身体不小」观感）。
        //   布局（腿底贴 -0.90）：腿心 y=-0.715 半 0.185（腿 0.37 高，原 0.56 缩 34%）→ 腿顶 -0.53；
        //   躯干心 y=-0.17 半 (0.16,0.36,0.26)（0.32×0.72×0.52，原 0.36×0.84×0.32 增深/增体积）→ 躯干顶 0.19；
        //   头心 y=0.43 半 0.24（0.48³，原 0.30³ 增 ~4× 体积）→ 头顶 0.67。腿四角（±0.25,±0.25）MC 站位。
        // t616 ③ 加高（用户「苦力怕比较矮，应该有两个格子高」）：t595 后总高 ~1.57（-0.90..0.67）读作矮。
        //   按 MC creeper 比例**整体拉伸**（腿/身/头三段 y 各按 1.9/1.57 ≈ 1.21 缩放、保持 MC 比例观感）：
        //   躯干心 -0.192 半 (0.17,0.437,0.28)（身 0.87 高）→ 躯干顶 0.245 / 躯干底 -0.629；头心 0.545 半 0.26
        //   （0.52³）→ 头顶 0.805。总高 0.805-(-0.90)=1.705 ≈ 1.7 格（MC 1.7 格标准；x/z 微放 1.06 保不瘦削）。
        //   review M8：t616 原腿心 -0.858 半 0.224 → 腿底 -1.082 沉入地面 0.18（中心/半高同乘拉伸比的算术错，
        //   旧注释误称「腿底仍贴 -0.90」）→ 腿心上移到 -0.676（= -0.90 + 半高 0.224）使腿底精确贴 -0.90
        //   collision 底面；腿顶 -0.452 与躯干（底 -0.629）相交 0.18 没入体内（视觉无害）；总高不变。
        //   halfH=0.90 不动（纯视觉修正，AABB 不变），Main.qml offset=0 不变。
        // R19 C3 UV（MC Creeper base 64×32；U1 §6 stalker）：head(0,0)8×8×8 / body(16,16)8×12×4 / leg(0,16)4×6×4。
        //   t633 ④ 修「潜行者头歪 90°」：t595/t616 注释误读 MC 身体为 4×12×8（窄宽深长）→ 几何 x 半 0.17 < z 半
        //   0.28（体长轴 ⊥ 脸朝向 -Z）→ 整体轮廓读作「侧身」，头像贴在窄身侧观感即「脸在侧面」。贴图布局反证
        //   vanilla：若 d=8，身体 box-UV 的面 v 域 16+8+12=36 越出 32px 贴图高（不可能）；vanilla ModelCreeper
        //   body = 8 宽×12 高×4 深。按 t616 身高（半 0.437 = 12px）换算：宽 8px→0.583（半 0.29）/ 深 4px→0.291
        //   （半 0.1455）。头（0.52³）比身宽略窄——vanilla creeper 头 8px 与身宽同宽，本工程保 t616 头尺寸
        //   （用户认可的观感），仅正身宽/深轴。腿位 / 腿径不动（四角站位与 vanilla 同）。
        g_texW = 64.0f; g_texH = 32.0f;
        setMobTex(16, 16, 8, 12, 4);
        addBox( 0.00f, -0.192f,  0.00f, 0.29f, 0.437f, 0.1455f, verts, idx, bMin, bMax); // 宽板躯干（t633 修正宽>深，vanilla 8×12×4 轴向）
        setMobTex(0, 0, 8, 8, 8);
        addBox( 0.00f,  0.545f,  0.00f, 0.26f, 0.26f, 0.26f, verts, idx, bMin, bMax); // 大头（0.52³，t616 拉高版）
        addLegs(-0.676f, 0.224f, 0.265f, 0.265f, 0.1325f, 0, 16, 4, 6, 4, m_walkPhase, verts, idx, bMin, bMax); // 四短腿（t616 拉高版；review M8 腿底贴 -0.90）
    } else if (m_mobType == 7) {
        // t285/t302 Spider（蜘蛛；机制等价 MC 1.0 蜘蛛，§9 区隔）—— 宽矮躯干 + 前伸小头 + **8 腿**（4 对沿躯干
        //   Z 分布，t302 升级自 t285 简化 4 腿）。腿底本地 y ≈ −0.30 贴 collision 底面（EntityManager halfH=0.30）。
        //   爬墙留后续（t285 spec 未含；本任务只做模型 + 步态动画）。8 腿绕躯干侧面髋枢做 Z 轴步态摆动
        //   （addSpiderLegs），baseDown 外端下倾 + walkPhase 驱动 tetrapod 交替步态。眼由 Main.qml delegate 补
        //   （4 颗红眼，同猪/牛/羊纯色子 Model 模式）。声音 / 受击音由 AudioManager.playMobAmbient/playMobHurt
        //   据 mobType=7 路由（t294 mob_idle_spider 已就绪，本任务复用）。
        // R19 C3 UV（MC Spider base 64×32；U1 §7）：head(32,4)8×8×8 / body1(0,12)10×8×12（宽矮躯干=主腹节）/
        //   leg(18,0)16×2×2。本工程蜘蛛几何 = 1 宽躯干 + 1 前伸头 + 8 腿；躯干采 body1、头采 head、腿采 leg。
        g_texW = 64.0f; g_texH = 32.0f;
        setMobTex(0, 12, 10, 8, 12);
        addBox( 0.00f, -0.05f,  0.00f, 0.40f, 0.18f, 0.30f, verts, idx, bMin, bMax); // 宽矮躯干
        setMobTex(32, 4, 8, 8, 8);
        addBox( 0.00f, -0.02f, -0.32f, 0.18f, 0.14f, 0.18f, verts, idx, bMin, bMax); // 小头（前伸）
        addSpiderLegs(0.40f, -0.05f, m_walkPhase, 18, 0, 16, 2, 2, verts, idx, bMin, bMax); // 8 腿（4 对，Z 轴步态）
    } else if (m_mobType == 8) {
        // t398 Chicken（鸡；机制等价 MC 1.0 鸡，§9 原创模型 + 贴图）—— 小型鸟：圆胖身躯 + 前伸小头 + 后翘尾 +
        //   **2 细腿**（biped walk cycle，区别于猪/牛/羊的 4 腿；鸡是两足鸟）。腿底本地 y≈−0.40 贴 collision 底面
        //   （EntityManager halfH=0.40 → offset=0，腿底贴地）。喙 / 鸡冠 / 肉垂由 Main.qml delegate 补（纯色子 Model，
        //   同猪眼模式 —— 单材质无法同几何双色，故头饰独立子节点）。机制等价 MC 1.0 鸡形态（小体型 + 两足 + 后翘尾）。
        // R19 C3 UV（MC Chicken base 64×32；U1 §8）：head(0,0)4×6×3 / body(0,9)6×8×6。
        //   t598 修（用户「鸡腿贴图缺失」）：旧腿 texOffs (26,0)3×5×3 在 demo 包实测六面 0-78% 不透明
        //   （(26,0)-(40,9) 区是翅膀 bill/chin 稀疏像素，非腿）→ 腿面采样透明区读作黑。
        //   demo 包 chicken.png 全图实测：唯一全覆盖腿区 = body(0,9,6,8,6) 自身（六面 100% 不透明，
        //   含宽条 (0,19)-(24,23)），本包未按 vanilla (26,0) 布局画独立腿 → 两腿与躯干共用 body texOffs
        //   （全脸同区采样，视觉为身体色，不再有透明黑腿）。
        //   后翘尾无 MC 对应 box → 复用 body texOffs（贴图同区，视觉为身体色）。喙/肉垂由 Main.qml 补。
        // t616 修（用户「鸡的腿为啥是毛绒的，应该是细小的黄色腿」）：t598 让腿共用 body texOffs → 采到
        //   躯干毛绒贴图区（读作毛绒腿）。细黄腿需独立于身体贴图的纯色 → **腿从本几何移除**（单材质
        //   无法同几何双色），由 Main.qml delegate 补两条独立纯色细黄腿 Model（#e8c53a，粗 0.06，同
        //   喙/冠独立子节点模式，绕髋摆动由 walkPhase 驱动 eulerRotation.x）。ResourceBrowser 图鉴预览
        //   同补（t598 头补法）。腿摆动画（biped walk cycle）随子 Node 走（几何内无腿 → 无 walkPhase 依赖）。
        g_texW = 64.0f; g_texH = 32.0f;
        setMobTex(0, 9, 6, 8, 6);
        addBox( 0.00f,  0.05f,  0.00f, 0.20f, 0.16f, 0.22f, verts, idx, bMin, bMax); // 圆胖躯干（紧凑小型鸟身）
        setMobTex(0, 0, 4, 6, 3);
        addHeadRot( 0.00f,  0.26f, -0.18f, 0.11f, 0.12f, 0.11f, m_headPitch, verts, idx, bMin, bMax); // 小头（前伸顶位）
        setMobTex(0, 9, 6, 8, 6);
        addBox( 0.00f,  0.14f,  0.22f, 0.09f, 0.09f, 0.05f, verts, idx, bMin, bMax); // 后翘尾（+Z 后方上翘小撮）
        // t616：细黄腿移至 Main.qml / ResourceBrowser 独立纯色 Model（本几何不再含腿；腿位参考旧值
        //   —— 髋枢 hipY=−0.05（躯干底面）、腿盒心 y=−0.225 半高 0.175 → 腿底 −0.40 贴 collision 底面）。
    } else if (m_mobType == 9) {
        // t399 Squid（鱿鱼；机制等价 MC 1.0 squid，§9 原创模型 + 贴图）—— 水生软体：圆胖躯干（mantle）+ 顶端小尖 +
        //   **8 触腕**（环绕身体底沿八向分布，机制等价 MC 1.0 squid 8 触腕）。触腕绕各自顶端枢轴做 X 轴摆动（前后
        //   波浪式起伏，相位错开 → 游动时触腕飘动；walkPhase 驱动）。squid 水中持续漂移（moveSpeed 恒 >0）→ 触腕常驻
        //   摆动（区别于陆地 mob idle 时腿停）。mobModelYOff 见 Main.qml（触腕底本地 |y|≈0.46 贴 collision 底面）。
        //   局部原点 = 躯干中心；无「头朝 -Z」语义（squid 软体无固定前后，yawAt 仅驱动整体朝向 → 触腕环对称无所谓前）。
        addBox( 0.00f,  0.08f,  0.00f, 0.28f, 0.24f, 0.28f, verts, idx, bMin, bMax); // 圆胖躯干（mantle 主体）
        addBox( 0.00f,  0.45f,  0.00f, 0.15f, 0.13f, 0.15f, verts, idx, bMin, bMax); // 顶端小尖（mantle 尖顶）
        // 8 触腕（环绕躯干底沿八向分布，半径 0.20）：每条细垂直盒（half 0.045×0.15×0.045），顶端枢轴 y=-0.16（躯干底）。
        //   绕 X 轴摆动（前后波浪式起伏）：angle = 0.18·sin(walkPhase + i·π/4)，相位错开 → 触腕此起彼伏飘动（游动感）。
        //   摆幅 0.18 弧度（~10°）小于四足 kLegSwingAmp（触腕是飘动非大跨步）；pivZ = 各触腕 z（绕各自 z 线旋转）。
        constexpr float kSquidTentacleSwing = 0.18f; // 触腕摆幅（弧度，~10°；飘动非大跨步）
        constexpr float kSquidRingR = 0.20f;          // 触腕环半径（躯干底沿八向分布）
        constexpr float kSquidPivotY = -0.16f;        // 触腕顶端枢轴 y（= 躯干底面）
        constexpr float kSquidTentHy = 0.15f;         // 触腕半高（垂直悬挂长度）
        for (int i = 0; i < 8; ++i) {
            const float ang = float(i) * 0.7853982f;          // i·π/4（八向：0°,45°,...,315°）
            const float tx = kSquidRingR * std::cos(ang);      // 触腕心 x
            const float tz = kSquidRingR * std::sin(ang);      // 触腕心 z
            const float swing = kSquidTentacleSwing * std::sin(m_walkPhase + float(i) * 0.7853982f);
            // 触腕心 y = 枢轴 y − 半高（顶端贴枢轴、底端下垂）。绕 (pivY, pivZ=tz) 的 X 轴旋转。
            addBoxRot(tx, kSquidPivotY - kSquidTentHy, tz, 0.045f, kSquidTentHy, 0.045f,
                      kSquidPivotY, tz, swing, verts, idx, bMin, bMax);
        }
    } else if (m_mobType == 10) {
        // t480 Wolf（狼；机制等价 MC 1.0 狼，§9 原创模型 + 贴图）—— 中型犬科：细长躯干 + 前伸尖头 + 双立耳 + 4 腿。
        //   尾巴**不在本几何** —— 呈现层 QML 据血量旋转独立尾巴 Model（spec「尾巴角度示血量」；独立子 Model
        //   才能绕尾根枢独立旋转，嵌在几何里的尾巴无法单独动）。腿底本地 y=−0.42 贴 collision 底面（halfH=0.45
        //   → Main.qml mobModelYOff=0.42−0.45=−0.03）。walkPhase 驱动 4 腿对角摆动（addLegs，同猪/牛/羊四足
        //   walk cycle）；坐姿由 Main.qml delegate 变换（压缩 + 后倾）驱动，几何本身不参与。
        addBox( 0.00f,  0.02f,  0.00f, 0.18f, 0.15f, 0.40f, verts, idx, bMin, bMax); // 细长躯干（比猪窄瘦；狼体型特征）
        addHeadRot( 0.00f,  0.12f, -0.52f, 0.14f, 0.15f, 0.18f, m_headPitch, verts, idx, bMin, bMax); // 头（前伸略尖；鼻吻）
        addBox(-0.08f,  0.30f, -0.50f, 0.035f, 0.07f, 0.035f, verts, idx, bMin, bMax); // 左立耳（头顶小尖盒）
        addBox( 0.08f,  0.30f, -0.50f, 0.035f, 0.07f, 0.035f, verts, idx, bMin, bMax); // 右立耳
        // R19 C3：wolf 无 pack 贴图映射（mobEntityMap 不含 10）→ pack 关全脸 UV，texOffs 占位（pack 关不读）。
        addLegs(-0.26f,  0.16f,  0.18f,  0.24f, 0.08f, 0, 16, 4, 12, 4, m_walkPhase, verts, idx, bMin, bMax); // 4 腿（细长，比猪腿瘦）
    } else if (m_mobType == 11) {
        // t481 豹猫/猫（Ocelot/Cat；机制等价 MC 1.0 豹猫，§9 原创模型 + 贴图）—— 中型猫科：细长躯干 +
        //   前伸圆头 + 双尖耳 + 长尾（几何内带尾，随身体贴图同纹）+ 4 细腿。未驯服 = 丛林豹猫（斑点橙棕贴图）、
        //   驯服 = 家猫（3 色变体贴图），几何共用（机制等价 MC 1.0 豹猫/猫同模型异贴图；毛色变体由 Main.qml
        //   据 ocelotVariantAt 切贴图，几何不变）。腿底本地 y=−0.40 贴 collision 底面（halfH=0.35 →
        //   Main.qml mobModelYOff=0.40−0.35=0.05）。walkPhase 驱动 4 腿对角摆动（addLegs，同狼四足 walk cycle）；
        //   坐姿由 Main.qml delegate 变换（压缩 + 后倾）驱动，几何本身不参与。
        addBox( 0.00f,  0.02f,  0.00f, 0.15f, 0.13f, 0.36f, verts, idx, bMin, bMax); // 细长躯干（比狼更窄长；猫科体型）
        addHeadRot( 0.00f,  0.12f, -0.46f, 0.11f, 0.12f, 0.14f, m_headPitch, verts, idx, bMin, bMax); // 头（前伸圆润）
        addBox(-0.06f,  0.26f, -0.44f, 0.03f, 0.06f, 0.03f, verts, idx, bMin, bMax); // 左尖耳（头顶小三角）
        addBox( 0.06f,  0.26f, -0.44f, 0.03f, 0.06f, 0.03f, verts, idx, bMin, bMax); // 右尖耳
        addBox( 0.00f,  0.18f,  0.36f, 0.04f, 0.05f, 0.16f, verts, idx, bMin, bMax); // 长尾（身后 +Z 后伸上翘；随身体同纹）
        // R19 C3：ocelot 无 pack 贴图映射（mobEntityMap 不含 11）→ pack 关全脸 UV，texOffs 占位（pack 关不读）。
        addLegs(-0.24f,  0.16f,  0.16f,  0.20f, 0.06f, 0, 16, 4, 12, 4, m_walkPhase, verts, idx, bMin, bMax); // 4 细腿
    } else if (m_mobType == 12) {
        // feat SnowGolem（雪傀儡；机制等价 MC 1.0 雪傀儡，§9 区隔原创模型 + pack 贴图）—— **柱身两雪块**上下堆叠。
        //   局部原点 = 碰撞中心（mobModelYOff=0，区别于猪牛羊「躯干中心」）；腿底本地 y=−0.90 贴 collision 底面
        //   （halfH=0.90 → mobModelYOff=0.0−0.90... 实际 mobModelYOff=0 因原点已是碰撞中心）。底块心 y=−0.45 半 0.45
        //   → spans y[−0.90,0.00]；顶块心 y=+0.45 半 0.45 → spans y[0.00,0.90]。
        //   **t552 下大上小**（用户报「底下两个雪块一样大」）：底块宽 0.80（半 0.40，MC 12/16=0.75 同量级）、
        //   顶块宽 0.60（半 0.30，MC 10/16=0.625 同量级）→ 雪堆下宽上窄读作雪人柱身。
        //   **南瓜头 + 刻面眼/嘴不在本几何** —— 由 Main.qml delegate 补独立南瓜头 Model（t582 起走
        //   BlockCube{blockId:100} + 共享图集采 pumpkin_side/top/face 瓦片 = 真南瓜贴图头，宽 0.50 比顶雪块
        //   0.60 小一截 = MC 8×8×8 头比例）。pack 命中 snow_golem.png → 6 面 T 字 UV 展开进雪块身（注：
        //   snow_golem.png 头部区只是雪 + 深色 derpy 脸，MC 1.8+ 南瓜不是 entity 贴图的一部分 → 南瓜头走
        //   block 瓦片，不走本贴图）；pack 关 → 全脸
        //   UV + 纯色雪白（无程序生成贴图，回退 baseColor）。shearSnowGolem 剪南瓜头仅隐藏南瓜头 Model（几何不动）。
        // R19 C3 UV（MC SnowMan base 64×64；U1 §12）：底雪块=piece2(0,36)12×12×12（大块在下）、顶雪块=piece1(0,16)10×10×10。
        g_texW = 64.0f; g_texH = 64.0f;
        setMobTex(0, 36, 12, 12, 12);
        addBox( 0.00f, -0.45f, 0.00f, 0.40f, 0.45f, 0.40f, verts, idx, bMin, bMax); // 底雪块（雪傀儡身体下块；宽 0.80）
        setMobTex(0, 16, 10, 10, 10);
        addBox( 0.00f,  0.45f, 0.00f, 0.30f, 0.45f, 0.30f, verts, idx, bMin, bMax); // 顶雪块（雪傀儡身体上块；宽 0.60，t552 下大上小）
    } else if (m_mobType == 13) {
        // feat IronGolem（铁傀儡；机制等价 MC 1.0 铁傀儡，§9 区隔原创模型 + pack 贴图）—— **铁块人形**：
        //   双腿 + 宽躯干 + 双长臂（机制等价 MC 铁傀儡 T 形铁块身）。局部原点 = 碰撞中心（mobModelYOff=0）；
        //   腿底本地 y=−1.20 贴 collision 底面（halfH=1.20）。盒比例与原 Main.qml UnitCube 堆叠同（保南瓜头 / 眼
        //   overlay 对齐）：双腿心 y=−0.90 半 (0.18,0.30,0.18)、躯干心 y=+0.05 半 (0.475,0.525,0.325)、双臂心
        //   y=+0.10 半 (0.14,0.39,0.225)。**南瓜头 + 刻面眼不在本几何**（同 SnowGolem，由 Main.qml delegate 补）。
        //   pack 命中 iron_golem.png → 6 面 T 字 UV 展开显铁纹（修 dev-plan C「铁傀儡全白」：程序纯色铁灰在用户
        //   视角读作「白」，pack 铁纹才显铁质）；pack 关 → 全脸 UV + 纯色铁灰 #7d848c（原 t483 锈斑 Model 在 pack
        //   命中时由 pack 铁纹取代，pack 关时简化为纯色铁灰无锈斑）。
        // R19 C3 UV（MC IronGolem base 128×128）：t598 重算（用户「铁傀儡腿前黑 + 肩黑色」）——
        //   demo 包 iron_golem.png (1024×1024 = 8×) 全图枚举六面 100% 不透明 box-UV 候选，取各部件实际绘画区
        //   （本包绘画布局与 vanilla 1.8.2 ModelIronGolem texOffs 不符，按包实测而非源码值）：
        //   body(0,40)18×12×11 —— 旧 d=9 使 Top 面左移 2px 采到空边 x9..11（= 肩黑色根因）；d=11 六面 100% 不透明。
        //   arm(60,58)4×16×6 —— 旧 (40,40,4,16,4) 侧面仅 56-89% 不透明（= 臂暗根因）；(60,58) 六面 100%。
        //   leg(0,70)9×5×6 —— 旧 (0,30,4,12,4) 六面 0-50% 不透明（= 腿前黑根因）；(0,70) 六面 100%
        //   （Front=(6,76)-(15,81) 正对镜头的面不再采样空区）。
        //   t635 ① 真头（pack 路径）：head(0,0)8×10×8 —— 六面实测 100% 不透明，Front(8,8)-(16,18) 含
        //   刻面双眼 + 垂藤特征（demo 包像素取证：dark 行 33..63 = 眼 + 藤 + 鼻梁）。头盒进**本几何**（pack 开
        //   时才加，同猪鼻模式）——Main.qml / ResourceBrowser 的傀儡头 delegate 据 packTextured 切换（pack 开
        //   → 隐独立橙色头 Model；pack 关 → 显纯橙头 + 刻面眼，现状不变）。几何位与旧橙色 UnitCube 头对齐：
        //   心 (0,0.95,0) 半 (0.36,0.33,0.36)（0.72×0.66×0.72）。UV 分数分母用 base 128（HD 包整张放大，分数仍按 base）。
        g_texW = 128.0f; g_texH = 128.0f;
        setMobTex(0, 40, 18, 12, 11);
        addBox( 0.00f,  0.05f, 0.00f, 0.475f, 0.525f, 0.325f, verts, idx, bMin, bMax); // 宽躯干（铁块身）
        setMobTex(0, 70, 9, 5, 6);
        addBox(-0.22f, -0.90f, 0.00f, 0.18f,  0.30f,  0.18f,  verts, idx, bMin, bMax); // 左腿（铁块）
        setMobTex(0, 70, 9, 5, 6);
        addBox( 0.22f, -0.90f, 0.00f, 0.18f,  0.30f,  0.18f,  verts, idx, bMin, bMax); // 右腿（铁块）
        // t635 ② 攻击抬臂（attackPose 0..1 → 双臂绕肩枢前抬）：attackPose=0 走 addBox 轴对齐快路径
        //   （垂臂，同旧）；>0 绕肩枢（臂盒顶面心 y=0.10+0.39=0.49）X 轴旋转 −attackPose·120°（负角把垂臂
        //   向前 −Z 抬起；度 → 弧度）。肩枢在臂盒顶 → 抬臂时臂根贴肩不脱节（同 Bones aimPitch 模式）。
        const float golemArmLift = qDegreesToRadians(-m_attackPose * 120.0f); // 度 → 弧度（负 = 向前 −Z 抬）
        setMobTex(60, 58, 4, 16, 6);
        if (m_attackPose == 0.0f) {
            addBox(-0.62f,  0.10f, 0.00f, 0.14f,  0.39f, 0.225f, verts, idx, bMin, bMax); // 左长臂（垂；机制等价 MC 铁傀儡重拳长臂）
        } else {
            addBoxRot(-0.62f,  0.10f, 0.00f, 0.14f,  0.39f, 0.225f, 0.49f, 0.00f, golemArmLift, verts, idx, bMin, bMax); // 左长臂（攻击前抬）
        }
        setMobTex(60, 58, 4, 16, 6);
        if (m_attackPose == 0.0f) {
            addBox( 0.62f,  0.10f, 0.00f, 0.14f,  0.39f, 0.225f, verts, idx, bMin, bMax); // 右长臂（垂）
        } else {
            addBoxRot( 0.62f,  0.10f, 0.00f, 0.14f,  0.39f, 0.225f, 0.49f, 0.00f, golemArmLift, verts, idx, bMin, bMax); // 右长臂（攻击前抬）
        }
        // t635 ① 贴图头（pack 开才加；几何位 = Main.qml 旧橙色头 Model 同位 → 切换零跳变）。
        if (g_packTextured) {
            setMobTex(0, 0, 8, 10, 8);
            addBox( 0.00f,  0.95f, 0.00f, 0.36f, 0.33f, 0.36f, verts, idx, bMin, bMax); // 贴图头（pack iron_golem 头区）
        }
    } else if (m_mobType == 14) {
        // t487 Silverfish（银鱼；机制等价 MC 1.0 银鱼，§9 原创模型 + 贴图）—— 小型虫类：分节躯干 + 前伸小头 +
        //   多对短腿（机制等价 MC 1.0 银鱼多足 + 多节体）。腿底本地 y≈−0.15 贴 collision 底面（halfH=0.15 →
        //   Main.qml mobModelYOff=0.15−0.15=0）。walkPhase 驱动短腿摆动（缩小幅度，虫类快步频）。
        //   hostile → EntityManager AI 自动追击玩家（默认 aiHostile 近战追击）。眼由 Main.qml delegate 补（纯色
        //   子 Model）。mob_silverfish 贴图：灰白甲壳 + 体节横纹（build_mob.py 程序生成原创像素图）。
        addBox( 0.00f,  0.00f,  0.00f, 0.20f, 0.12f, 0.32f, verts, idx, bMin, bMax); // 分节躯干（细长，比蜘蛛更小）
        addBox( 0.00f,  0.00f, -0.24f, 0.14f, 0.11f, 0.10f, verts, idx, bMin, bMax); // 前伸小头（虫头部，略窄于躯干）
        // 多对短腿（3 对沿躯干 Z 分布；每对绕躯干侧面髋枢 X 轴摆动，前后对反相 → 快步频虫类交替步态）。
        const float sw = kLegSwingAmp * 0.6f * std::sin(m_walkPhase); // 虫腿摆幅缩 0.6（短腿快频小步）
        // 前对（z=−0.12，前段躯干）：左 +X 外、右 −X 外。
        addBoxRot(-0.20f, -0.075f, -0.12f, 0.06f, 0.075f, 0.04f, 0.00f, -0.12f, +sw, verts, idx, bMin, bMax);
        addBoxRot( 0.20f, -0.075f, -0.12f, 0.06f, 0.075f, 0.04f, 0.00f, -0.12f, -sw, verts, idx, bMin, bMax);
        // 中对（z=0.00，中段躯干）：反相于前对（交替步态）。
        addBoxRot(-0.20f, -0.075f,  0.00f, 0.06f, 0.075f, 0.04f, 0.00f,  0.00f, -sw, verts, idx, bMin, bMax);
        addBoxRot( 0.20f, -0.075f,  0.00f, 0.06f, 0.075f, 0.04f, 0.00f,  0.00f, +sw, verts, idx, bMin, bMax);
        // 后对（z=+0.12，后段躯干）：同相于前对。
        addBoxRot(-0.20f, -0.075f,  0.12f, 0.06f, 0.075f, 0.04f, 0.00f,  0.12f, +sw, verts, idx, bMin, bMax);
        addBoxRot( 0.20f, -0.075f,  0.12f, 0.06f, 0.075f, 0.04f, 0.00f,  0.12f, -sw, verts, idx, bMin, bMax);
    } else if (m_mobType == 2) {
        // 牛：高大长身 + 头顶两小角盒（角随头俯仰；牛 headPitch 恒 0 → 实走快路径不动）。机制等价 MC 牛形态。
        // R19 C3 UV（MC Cow base 64×32；U1 §2）：body(18,4)12×18×10 / head(0,0)8×8×6 / horn(22,0)1×3×1 / leg(0,16)4×12×4。
        g_texW = 64.0f; g_texH = 32.0f;
        setMobTex(18, 4, 12, 18, 10);
        addBox(0.00f, 0.05f, 0.00f, 0.32f, 0.28f, 0.55f, verts, idx, bMin, bMax); // 躯干（长）
        // 头 + 双角共享颈附着点（cy, cz+hz）→ headPitch 驱动时整组随头俯仰。
        setMobTex(0, 0, 8, 8, 6);
        addHeadRot(0.00f, 0.15f, -0.60f, 0.20f, 0.22f, 0.20f, m_headPitch, verts, idx, bMin, bMax); // 头（前伸）
        setMobTex(22, 0, 1, 3, 1);
        addHeadRot(-0.22f, 0.34f, -0.58f, 0.05f, 0.06f, 0.05f, m_headPitch, verts, idx, bMin, bMax); // 左角
        setMobTex(22, 0, 1, 3, 1);
        addHeadRot( 0.22f, 0.34f, -0.58f, 0.05f, 0.06f, 0.05f, m_headPitch, verts, idx, bMin, bMax); // 右角
        addLegs(-0.30f, 0.20f, 0.20f, 0.35f, 0.10f, 0, 16, 4, 12, 4, m_walkPhase, verts, idx, bMin, bMax); // 4 长腿
    } else if (m_mobType == 3) {
        // 羊：圆胖躯干、小头、短腿。机制等价 MC 羊形态（非名词照搬）。
        // R19 C3 UV（MC Sheep base 64×32；U1 §3）：body(28,8)8×16×6 / head(0,0)6×6×8 / leg(0,16)4×12×4。
        g_texW = 64.0f; g_texH = 32.0f;
        setMobTex(28, 8, 8, 16, 6);
        addBox(0.00f, 0.05f, 0.00f, 0.30f, 0.28f, 0.42f, verts, idx, bMin, bMax); // 躯干（圆胖）
        setMobTex(0, 0, 6, 6, 8);
        addHeadRot(0.00f, 0.10f, -0.45f, 0.14f, 0.16f, 0.16f, m_headPitch, verts, idx, bMin, bMax); // 小头（吃草时俯仰）
        addLegs(-0.28f, 0.16f, 0.18f, 0.26f, 0.09f, 0, 16, 4, 12, 4, m_walkPhase, verts, idx, bMin, bMax); // 4 短腿
    } else {
        // 猪（默认 / 兜底）：紧凑低矮、短腿、大头。机制等价 MC 猪形态（非名词照搬）。
        // R19 C3 UV（MC Pig base 64×32；U1 §1）：body(28,8)10×16×8 / head(0,0)8×8×8 / leg(0,16)4×6×4。
        //   t592 修（用户「猪腿后跟都是黑色的 + 没嘴巴」）：
        //   ① 腿 UV box 原 6×6×5 采样越界——box-UV 公式算出 6 面落在 base (0,16)-(22,27)，
        //      -Z Back 面 (16,21)-(22,27) 全透明（采样到贴图外）= 四条腿后跟黑（实测 0% 不透明）。
        //      改回 MC 1.8 pig 腿标准 4×6×4 → 6 面全落在 (0,16)-(16,26) 全不透明（实测 100%）。
        //   ② 嘴 = 猪鼻（snout）：pack 命中时补独立小盒采样 MC snout(16,16)4×3×1 区（鼻子贴图区，
        //      正面 (17,17)-(21,20) 有鼻孔黑点；6 面全 100% 不透明）。pack 关无（程序生成贴图全脸
        //      无鼻子区，Main.qml 眼 Model 已补五官；鼻盒若用全脸 UV 会每面铺整张猪图 = 难看）。
        g_texW = 64.0f; g_texH = 32.0f;
        setMobTex(28, 8, 10, 16, 8);
        addBox(0.00f, 0.00f, 0.00f, 0.35f, 0.22f, 0.45f, verts, idx, bMin, bMax); // 躯干（低矮）
        setMobTex(0, 0, 8, 8, 8);
        addHeadRot(0.00f, 0.05f, -0.50f, 0.22f, 0.22f, 0.18f, m_headPitch, verts, idx, bMin, bMax); // 大头（前伸）
        // 猪鼻盒（pack 开）：头心 (0,0.05,-0.50) 半 (0.22,0.22,0.18) → 头前面 z=-0.68；鼻心 z=-0.71
        //   （前 -0.75 凸出 0.07、后 -0.67 缩进头内 0.01 防共面 z-fight），y=-0.05（头面前下沿）。
        //   猪 headPitch 恒 0（addHeadRot 快路径）→ 鼻固定位置不随俯仰，无需挂头旋转 Node。
        if (g_packTextured) {
            setMobTex(16, 16, 4, 3, 1);
            addBox(0.00f, -0.05f, -0.71f, 0.10f, 0.08f, 0.04f, verts, idx, bMin, bMax); // 猪鼻（嘴巴）
        }
        addLegs(-0.30f, 0.18f, 0.22f, 0.28f, 0.10f, 0, 16, 4, 6, 4, m_walkPhase, verts, idx, bMin, bMax); // 4 短腿
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
