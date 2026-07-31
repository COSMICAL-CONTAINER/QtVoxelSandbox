#include "partialblockgeometry.h"

#include <algorithm> // std::clamp / std::max
#include <cmath>     // std::fabs（faceVc 复算用；与 chunkgeometry 同源公式）

// t134 不完整方块异形几何：为 6 类木制半方块（WoodSlab/WoodStairs/WoodFence/WoodPressurePlate/
// WoodDoor/WoodTrapdoor）生成异形顶点，**合批进同一 chunk mesh**（复用 chunkgeometry 顶点色光照管线 +
// 单 draw call，lessons-learned t03「大网格不要用 QML Repeater」）。
//
// 设计（spec t133/t134）：
//   - 复用 chunkgeometry 的「kFaces 法线 + uv 规则」：每面外法线 + 4 角（从外看 CCW）+ per-face UV 映射
//     （±X 面 cu,cv=(z,y)；±Z 面 (x,y)；±Y 面 (x,z)），三角形按 (0,1,2),(0,2,3)。本文件本地定义同款
//     Face 表（chunkgeometry 的 kFaces 是其 .cpp 内 static，无法 import；此处镜像，注释钉死同源）。
//   - 顶点色 vc 按各面外法线复算（与 chunkgeometry 立方面同公式：地下 0.2 / 见天 clamp(1+kSunRange·
//     sunIntensity·(lit·shade − sunLitAvg), kSunMin, kSunMax)），使异形方块与立方面光照一致、无缝混排。
//   - 各 shape 生成器把异形拆成 1~2 个轴对齐子盒（pushBox 推 6 面），不剔内面 —— 异形小体的内/底面被
//     自身遮挡（overdraw 可忽），且 partial 方块 solid=false 不参与整立方邻居剔除，需自画全部面。
//
// state 编码（与 BlockRegistry::Id 注释 + playercontroller placeBlock 一致；机制等价 MC (id,metadata)）：
//   slab        bit0      = 上半(1)/下半(0)
//   stairs      bit[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z（楼梯朝该向开 / 背墙在对侧） bit2=上下倒置（整步在上、背墙在下）
//   fence       —         （单格中心立柱；state=0，连接邻居留后续）
//   pressure_plate —      （贴地薄板；state=0）
//   door        bit[1:0]=朝向(0=+X 1=-X 2=+Z 3=-Z) bit2=开(1)/合(0) bit3=上格(1)/下格(0)
//   trapdoor    bit0=开(1)/合(0) bit[2:1]=开时朝向(0=+X 1=-X 2=+Z 3=-Z)
//
// 文件位置（分层 PLAN §2）：与唯一调用者 chunkgeometry 同放 src/World/（mesher 子系统同层），见 .h 注释。

namespace {
// 顶点光常量（镜像 chunkgeometry.cpp 同名常量；改一处须同步另一处 —— 二者须渲染一致）。
constexpr float kSunRange = 1.5f;
constexpr float kSunMin   = 0.15f;  // codereview H1: 与 chunkgeometry.cpp t135 的 kSunMin 镜像（0.3→0.15），否则部分方块阴影区亮 2 倍断层
constexpr float kSunMax   = 1.0f;

// 单面顶点色（同 chunkgeometry 立方面公式：地下恒暗 0.2；见天按 faceNormal·sunDir + 投影阴影调制）。
float faceVc(const float nrm[3], const PartialLightCtx &L)
{
    if (!L.surface) return 0.2f;
    const float lit = std::max(0.f, nrm[0] * L.sdx + nrm[1] * L.sdy + nrm[2] * L.sdz);
    const float contrast = lit * L.shade - L.sunLitAvg;
    return std::clamp(1.f + kSunRange * L.sunIntensity * contrast, kSunMin, kSunMax);
}

// 轴对齐盒体 6 面定义（normal + 4 角 + per-corner (cu,cv)）。角序与 chunkgeometry kFaces 完全一致
// （从外看 CCW；UV 映射 ±X=(z,y) ±Z=(x,y) ±Y=(x,z)），三角形按 (0,1,2),(0,2,3)。
// cu,cv 取单位值 {0,1}（不随盒体尺寸缩放）→ 整张瓦片贴图始终映射到该面（薄面则贴图被压缩，与 MC 一致）。
struct BoxFace { float n[3]; float c[4][3]; float uv[4][2]; };
const BoxFace kBoxFaces[6] = {
    /*+X*/ {{ 1, 0, 0}, {{0,0,0},{0,1,0},{0,1,1},{0,0,1}}, {{0,0},{0,1},{1,1},{1,0}}}, // cu=z cv=y（x 由调用方填 x1）
    /*-X*/ {{-1, 0, 0}, {{0,0,1},{0,1,1},{0,1,0},{0,0,0}}, {{1,0},{1,1},{0,1},{0,0}}},
    /*+Y*/ {{0,  1, 0}, {{0,0,1},{1,0,1},{1,0,0},{0,0,0}}, {{0,1},{1,1},{1,0},{0,0}}}, // cu=x cv=z（y 由调用方填 y1）
    /*-Y*/ {{0, -1, 0}, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}}, {{0,0},{1,0},{1,1},{0,1}}},
    /*+Z*/ {{0, 0,  1}, {{0,0,0},{1,0,0},{1,1,0},{0,1,0}}, {{0,0},{1,0},{1,1},{0,1}}}, // cu=x cv=y（z 由调用方填 z1）
    /*-Z*/ {{0, 0, -1}, {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}, {{1,0},{0,0},{0,1},{1,1}}},
};
// 注：上表角点的「常数轴」（面法线轴）写 0，调用前据 +X/-X 等填成 x1/x0（见 pushBox 内 patch）。
// 这样 kBoxFaces 是「单位盒面模板」，pushBox 按 (x0,x1,y0,y1,z0,z1) 现填常数轴 → 任意轴对齐子盒复用。

// 推一个轴对齐盒体的 6 面。tile = 图集瓦片序号（partial 方块各面同贴图，由 BlockRegistry::tileIndex 给）。
// 不做邻居剔除（异形小体内/底面被自身遮挡，overdraw 可忽；partial solid=false 亦不参与整立方邻居剔除）。
void pushBox(QVector<Vtx> &verts, QVector<quint32> &idx,
             int lx, int ly, int lz,
             float x0, float x1, float y0, float y1, float z0, float z1,
             int tile, const PartialLightCtx &L,
             float tileW, float hx, float hy, float v0, float v1)
{
    const float u0 = tile * tileW + hx, u1 = (tile + 1) * tileW - hx;
    for (const BoxFace &f : kBoxFaces) {
        // 单位盒面模板的常数轴（法线轴）填成实际盒体边值（+面=x1/y1/z1，-面=x0/y0/z0）；
        // 面内两轴取模板 0/1 → 映射到该面实际范围（整张瓦片贴图覆盖该面）。
        const float vX = (f.n[0] > 0) ? x1 : (f.n[0] < 0) ? x0 : 0;
        const float vY = (f.n[1] > 0) ? y1 : (f.n[1] < 0) ? y0 : 0;
        const float vZ = (f.n[2] > 0) ? z1 : (f.n[2] < 0) ? z0 : 0;
        const float vc = faceVc(f.n, L);
        const quint32 base = quint32(verts.size());
        for (int i = 0; i < 4; ++i) {
            const float tx = f.c[i][0], ty = f.c[i][1], tz = f.c[i][2]; // 模板角点（0/1）
            Vtx v;
            v.x = float(lx) + (f.n[0] != 0.f ? vX : x0 + tx * (x1 - x0));
            v.y = float(ly) + (f.n[1] != 0.f ? vY : y0 + ty * (y1 - y0));
            v.z = float(lz) + (f.n[2] != 0.f ? vZ : z0 + tz * (z1 - z0));
            v.nx = f.n[0]; v.ny = f.n[1]; v.nz = f.n[2];
            v.u = u0 + f.uv[i][0] * (u1 - u0);
            v.v = v0 + f.uv[i][1] * (v1 - v0);
            v.r = vc; v.g = vc; v.b = vc; v.a = 1.0f;
            verts.append(v);
        }
        idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
        idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
    }
}
} // namespace

int PartialBlockGeometry::append(
    QVector<Vtx> &verts, QVector<quint32> &idx,
    int lx, int ly, int lz,
    quint8 blockId, quint8 state,
    const PartialLightCtx &light,
    float tileW, float hx, float hy, float v0, float v1)
{
    const int startVerts = verts.size();
    // partial 方块各面同贴图（planks 等）；走 BlockRegistry 单一权威（与立方面 tileFor 同源）。
    // 6 面 tile 相同 → 取任一面即可（用 PosX = sideTile = planks(8)）。
    const int tile = BlockRegistry::tileIndex(blockId, BlockRegistry::PosX);

    switch (blockId) {
    case BlockRegistry::WoodSlab: {
        // 半高：state bit0=上半 → y[0.5,1]；下半 → y[0,0.5]。全 footprint。
        const bool upper = (state & 1) != 0;
        const float y0 = upper ? 0.5f : 0.0f, y1 = upper ? 1.0f : 0.5f;
        pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, y0, y1, 0.f, 1.f, tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WoodStairs: {
        // 楼梯 = 整步（全 footprint 半高）+ 背墙（朝向对侧半 footprint 的另半高）。
        //   state[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z（楼梯朝该向开 → 背墙在对侧 -d 半）。
        //   t147 state bit2=上下倒置：正置整步在下层(y 0..0.5)+背墙在上层(y 0.5..1)；
        //     倒置则垂直镜像（整步在上层 + 背墙在下层），机制等价 MC 倒置楼梯（天花板挂装）。
        //     y 区间据 bit2 翻转（与 blockregistry ShapeStairs 同编码 —— 改一处须同步）。
        const bool inverted = (state & 4) != 0;
        const float stepY0 = inverted ? 0.5f : 0.0f, stepY1 = inverted ? 1.0f : 0.5f; // 整步 y 区间
        const float wallY0 = inverted ? 0.0f : 0.5f, wallY1 = inverted ? 0.5f : 1.0f; // 背墙 y 区间
        pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, stepY0, stepY1, 0.f, 1.f, tile, light, tileW, hx, hy, v0, v1);
        float bx0 = 0.f, bx1 = 1.f, bz0 = 0.f, bz1 = 1.f;
        switch (state & 3) {
        case 0: bx0 = 0.0f; bx1 = 0.5f; break; // 朝 +X 开 → 背墙 -X 侧 x[0,0.5]
        case 1: bx0 = 0.5f; bx1 = 1.0f; break; // 朝 -X 开 → 背墙 +X 侧 x[0.5,1]
        case 2: bz0 = 0.0f; bz1 = 0.5f; break; // 朝 +Z 开 → 背墙 -Z 侧 z[0,0.5]
        case 3: bz0 = 0.5f; bz1 = 1.0f; break; // 朝 -Z 开 → 背墙 +Z 侧 z[0.5,1]
        }
        pushBox(verts, idx, lx, ly, lz, bx0, bx1, wallY0, wallY1, bz0, bz1, tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WoodFence: {
        // 中心立柱（0.4 见方，全高）。栅栏连接邻居的横档留后续任务（需邻居感知，同 fence gate）。
        pushBox(verts, idx, lx, ly, lz, 0.3f, 0.7f, 0.f, 1.f, 0.3f, 0.7f, tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WoodPressurePlate: {
        // 贴地薄板（1/16 厚 + 1/16 边距）。
        pushBox(verts, idx, lx, ly, lz, 0.0625f, 0.9375f, 0.f, 0.0625f, 0.0625f, 0.9375f,
                tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WoodDoor: {
        // 两格高门：每格各画满高薄板（下格画门下半 / 上格画门上半，几何同 —— 区别仅在 isUpper state，
        //   用于破坏联动 & 朝向同步）。朝向 state[1:0]（0=+X 1=-X 2=+Z 3=-Z）、开合 state bit2。
        //   合：薄板贴在「朝向」边（朝向 +X → 板在 x[0.8125,1]）；开：板旋 90° 贴邻边。
        const int facing = state & 3;
        const bool open = (state & 4) != 0;
        float bx0 = 0.f, bx1 = 1.f, bz0 = 0.f, bz1 = 1.f;
        const float t0 = 0.8125f, t1 = 1.0f, s0 = 0.0f, s1 = 0.1875f; // 厚 3/16
        if (!open) {
            switch (facing) {
            case 0: bx0 = t0; bx1 = t1; break; // 朝 +X → 板贴 +X 边
            case 1: bx0 = s0; bx1 = s1; break; // 朝 -X → 板贴 -X 边
            case 2: bz0 = t0; bz1 = t1; break; // 朝 +Z → 板贴 +Z 边
            case 3: bz0 = s0; bz1 = s1; break; // 朝 -Z → 板贴 -Z 边
            }
        } else {
            switch (facing) {
            case 0: bz0 = t0; bz1 = t1; break; // 原 +X → 旋到 +Z 边
            case 1: bz0 = s0; bz1 = s1; break; // 原 -X → 旋到 -Z 边
            case 2: bx0 = t0; bx1 = t1; break; // 原 +Z → 旋到 +X 边
            case 3: bx0 = s0; bx1 = s1; break; // 原 -Z → 旋到 -X 边
            }
        }
        pushBox(verts, idx, lx, ly, lz, bx0, bx1, 0.f, 1.f, bz0, bz1, tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WoodTrapdoor: {
        // 合：水平薄板贴地（y[0,0.1875]，全 footprint）。开：竖直薄板贴边（朝向 state[2:1]，0=+X 1=-X 2=+Z 3=-Z）。
        const bool open = (state & 1) != 0;
        if (!open) {
            pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, 0.f, 0.1875f, 0.f, 1.f,
                    tile, light, tileW, hx, hy, v0, v1);
        } else {
            const int facing = (state >> 1) & 3;
            float bx0 = 0.f, bx1 = 1.f, bz0 = 0.f, bz1 = 1.f;
            const float t0 = 0.8125f, t1 = 1.0f, s0 = 0.0f, s1 = 0.1875f;
            switch (facing) {
            case 0: bx0 = t0; bx1 = t1; break; // +X 边
            case 1: bx0 = s0; bx1 = s1; break; // -X 边
            case 2: bz0 = t0; bz1 = t1; break; // +Z 边
            case 3: bz0 = s0; bz1 = s1; break; // -Z 边
            }
            pushBox(verts, idx, lx, ly, lz, bx0, bx1, 0.f, 1.f, bz0, bz1, tile, light, tileW, hx, hy, v0, v1);
        }
        break;
    }
    default:
        return 0; // 非异形方块 / 未实现 → 不追加（chunkgeometry 的 continue 跳过此格）
    }
    return int(verts.size()) - startVerts;
}
