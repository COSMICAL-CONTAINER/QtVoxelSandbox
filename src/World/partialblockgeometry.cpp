#include "partialblockgeometry.h"

#include <algorithm> // std::min（WheatCrop stage clamp）
#include <cmath> // std::sqrt（pushCrossQuad 法线归一化）

// t134 不完整方块异形几何：为 6 类木制半方块（WoodSlab/WoodStairs/WoodFence/WoodPressurePlate/
// WoodDoor/WoodTrapdoor）生成异形顶点，**合批进同一 chunk mesh**（复用 chunkgeometry 顶点色光照管线 +
// 单 draw call，lessons-learned t03「大网格不要用 QML Repeater」）。
//
// 设计（spec t133/t134）：
//   - 复用 chunkgeometry 的「kFaces 法线 + uv 规则」：每面外法线 + 4 角（从外看 CCW）+ per-face UV 映射
//     （±X 面 cu,cv=(z,y)；±Z 面 (x,y)；±Y 面 (x,z)），三角形按 (0,1,2),(0,2,3)。本文件本地定义同款
//     Face 表（chunkgeometry 的 kFaces 是其 .cpp 内 static，无法 import；此处镜像，注释钉死同源）。
//   - t151/t360 真光场：盒体各面顶点色 = 该面外法线邻格的 flood 光 max(sky,block)/15（经 PartialLightCtx.face
//     传入；t360 改采邻格取代本格，修合活版门/下半砖顶面自影），cross 植物用本格光场（PartialLightCtx.light）。
//   - 各 shape 生成器把异形拆成 1~2 个轴对齐子盒（pushBox 推 6 面），不剔内面 —— 异形小体的内/底面被
//     自身遮挡（overdraw 可忽），且 partial 方块 solid=false 不参与整立方邻居剔除，需自画全部面。
//
// state 编码（与 BlockRegistry::Id 注释 + playercontroller placeBlock 一致；机制等价 MC (id,metadata)）：
//   slab        bit0      = 上半(1)/下半(0)
//   stairs      bit[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z（楼梯朝该向开 / 背墙在对侧） bit2=上下倒置（整步在上、背墙在下）
//   fence       —         （中心立柱 1.5 高 + 四向横档连邻居；state=0；连接判定读 PartialNeighborCtx，t209）
//   pressure_plate —      （贴地薄板；state=0）
//   door        bit[1:0]=朝向(0=+X 1=-X 2=+Z 3=-Z) bit2=开(1)/合(0) bit3=上格(1)/下格(0)
//   trapdoor    bit0=开(1)/合(0) bit[2:1]=开时朝向(0=+X 1=-X 2=+Z 3=-Z)
//
// 文件位置（分层 PLAN §2）：与唯一调用者 chunkgeometry 同放 src/World/（mesher 子系统同层），见 .h 注释。

namespace {
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
//   t360 光照：各面顶点色取该面外法线方向的邻格 flood 光（PartialLightCtx.face[fi]，fi 与 kBoxFaces 同序
//   +X,-X,+Y,-Y,+Z,-Z）—— 修合活版门/下半砖顶面自影（旧采被本格遮光压暗的本格值）。内/底面虽不可见，
//   仍按邻格取值（统一、无特例）；其邻格常为下方实体格（sky=0）→ 暗，与遮挡观感一致。
void pushBox(QVector<Vtx> &verts, QVector<quint32> &idx,
             int lx, int ly, int lz,
             float x0, float x1, float y0, float y1, float z0, float z1,
             int tile, const PartialLightCtx &L,
             float tileW, float hx, float hy, float v0, float v1)
{
    const float u0 = tile * tileW + hx, u1 = (tile + 1) * tileW - hx;
    for (int fi = 0; fi < 6; ++fi) {
        const BoxFace &f = kBoxFaces[fi];
        // 单位盒面模板的常数轴（法线轴）填成实际盒体边值（+面=x1/y1/z1，-面=x0/y0/z0）；
        // 面内两轴取模板 0/1 → 映射到该面实际范围（整张瓦片贴图覆盖该面）。
        const float vX = (f.n[0] > 0) ? x1 : (f.n[0] < 0) ? x0 : 0;
        const float vY = (f.n[1] > 0) ? y1 : (f.n[1] < 0) ? y0 : 0;
        const float vZ = (f.n[2] > 0) ? z1 : (f.n[2] < 0) ? z0 : 0;
        const float vc = L.face[fi]; // t360：本面外法线方向的邻格光场（顺序同 kBoxFaces）
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

// t235 cross 形广告牌方块（草丛 / 花 / 作物）：两片对角十字相交的**双面** quad，每片贴整张瓦片贴图。
//   机制等价 MC 1.0 cross 模型（tall grass / 花生 / 小麦作物的两片对角相交平面）。
//
//   双面：默认 backface 剔除下单面 quad 只从一个方向可见；cross 须两面都见（玩家绕到背面仍见草叶）。
//   故每片 quad 发**正反两组三角形**（正 CCW + 反 CW，法线取反）—— 不依赖材质关 culling，局部几何自洽。
//
//   quad 四角 p0..p3 须共面、按「从某一侧看 CCW」序给出（UV: p0=(0,0) p1=(1,0) p2=(1,1) p3=(0,1)，
//   即 BL→BR→TR→TL，整张瓦片铺满该 quad）。法线由 (p1-p0)×(p3-p0) 算（NoLighting 下不影响着色，仅填格式）。
//   光照：cross 各面共用本格光场值（cross 立于开敞格、本格 flood 光即其光照；PartialLightCtx.light）。
void pushCrossQuad(QVector<Vtx> &verts, QVector<quint32> &idx,
                   int lx, int ly, int lz,
                   float p0x, float p0y, float p0z,
                   float p1x, float p1y, float p1z,
                   float p2x, float p2y, float p2z,
                   float p3x, float p3y, float p3z,
                   int tile, const PartialLightCtx &L,
                   float tileW, float hx, float hy, float v0, float v1)
{
    const float u0 = tile * tileW + hx, u1 = (tile + 1) * tileW - hx;
    const float vc = L.light; // cross 用本格光场（开敞格 flood 光）
    // 法线 = (p1-p0)×(p3-p0)（NoLighting 下不影响渲染；填格式 + 供未来 lit 路径）。
    const float ex = p1x - p0x, ey = p1y - p0y, ez = p1z - p0z;
    const float fx = p3x - p0x, fy = p3y - p0y, fz = p3z - p0z;
    float nx = ey * fz - ez * fy;
    float ny = ez * fx - ex * fz;
    float nz = ex * fy - ey * fx;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
    else { nx = 0.f; ny = 1.f; nz = 0.f; } // 退化（不应发生）兜底 +Y

    const float c[4][3] = {{p0x,p0y,p0z},{p1x,p1y,p1z},{p2x,p2y,p2z},{p3x,p3y,p3z}};
    const float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};

    // 正面（法线 +n，CCW p0→p1→p2→p3）。
    const quint32 baseF = quint32(verts.size());
    for (int i = 0; i < 4; ++i) {
        Vtx v;
        v.x = float(lx) + c[i][0]; v.y = float(ly) + c[i][1]; v.z = float(lz) + c[i][2];
        v.nx = nx; v.ny = ny; v.nz = nz;
        v.u = u0 + uv[i][0] * (u1 - u0);
        v.v = v0 + uv[i][1] * (v1 - v0);
        v.r = vc; v.g = vc; v.b = vc; v.a = 1.0f;
        verts.append(v);
    }
    idx.append(baseF + 0); idx.append(baseF + 1); idx.append(baseF + 2);
    idx.append(baseF + 0); idx.append(baseF + 2); idx.append(baseF + 3);
    // 背面（法线 -n，CW p0→p3→p2→p1 → 反向绕序三角形）。
    const quint32 baseB = quint32(verts.size());
    for (int i = 0; i < 4; ++i) {
        Vtx v;
        v.x = float(lx) + c[i][0]; v.y = float(ly) + c[i][1]; v.z = float(lz) + c[i][2];
        v.nx = -nx; v.ny = -ny; v.nz = -nz;
        v.u = u0 + uv[i][0] * (u1 - u0);
        v.v = v0 + uv[i][1] * (v1 - v0);
        v.r = vc; v.g = vc; v.b = vc; v.a = 1.0f;
        verts.append(v);
    }
    idx.append(baseB + 0); idx.append(baseB + 3); idx.append(baseB + 2);
    idx.append(baseB + 0); idx.append(baseB + 2); idx.append(baseB + 1);
}
} // namespace

int PartialBlockGeometry::append(
    QVector<Vtx> &verts, QVector<quint32> &idx,
    int lx, int ly, int lz,
    quint8 blockId, quint8 state,
    const PartialLightCtx &light,
    const PartialNeighborCtx &nb,
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
        // t209 栅栏 = 中心立柱（0.4 见方，1.5 高）+ 四向横档（连接相邻栅栏 / 实体方块）。
        //   立柱 y[0, 1.5] 与 collisionAABBs(ShapeFence) 同高（{0.3,0,0.3,0.7,1.5,0.7}）→ 玩家跳不过
        //   （跳跃顶点 ~1.25 < 1.5；机制等价 MC 栅栏 1.5 高不可越）。立柱顶探入上格 0.5（栅栏上格必为空气，
        //   否则碰撞亦不可能 1.5 高 → 渲染安全）。
        //   横档分上下两道（MC 式），每道从立柱中心延伸到格边；仅在该向「有连接」时画。连接判定 = 邻格为
        //   WoodFence 或 isSolid（实体整立方；不连空气/水/火把/不完整方块，同 MC 栅栏只连栅栏与实体）。
        //   横档纯视觉（不进碰撞 AABB，机制等价 MC 栅栏 VoxelShape 仅立柱；玩家贴立柱碰撞即可挡）。
        pushBox(verts, idx, lx, ly, lz, 0.3f, 0.7f, 0.f, 1.5f, 0.3f, 0.7f, tile, light, tileW, hx, hy, v0, v1);
        const auto connects = [](quint8 blk) {
            return blk == BlockRegistry::WoodFence || BlockRegistry::isSolid(blk);
        };
        const float yLo0 = 0.375f,  yLo1 = 0.5625f; // 下档（MC 6/16..9/16）
        const float yHi0 = 0.9375f, yHi1 = 1.125f;  // 上档（探入 1.5 高区间，呼应立柱顶高度）
        if (connects(nb.posX)) { // +X：x[中心, +X 边]
            pushBox(verts, idx, lx, ly, lz, 0.5f, 1.0f, yLo0, yLo1, 0.3f, 0.7f, tile, light, tileW, hx, hy, v0, v1);
            pushBox(verts, idx, lx, ly, lz, 0.5f, 1.0f, yHi0, yHi1, 0.3f, 0.7f, tile, light, tileW, hx, hy, v0, v1);
        }
        if (connects(nb.negX)) { // -X：x[-X 边, 中心]
            pushBox(verts, idx, lx, ly, lz, 0.0f, 0.5f, yLo0, yLo1, 0.3f, 0.7f, tile, light, tileW, hx, hy, v0, v1);
            pushBox(verts, idx, lx, ly, lz, 0.0f, 0.5f, yHi0, yHi1, 0.3f, 0.7f, tile, light, tileW, hx, hy, v0, v1);
        }
        if (connects(nb.posZ)) { // +Z：z[中心, +Z 边]
            pushBox(verts, idx, lx, ly, lz, 0.3f, 0.7f, yLo0, yLo1, 0.5f, 1.0f, tile, light, tileW, hx, hy, v0, v1);
            pushBox(verts, idx, lx, ly, lz, 0.3f, 0.7f, yHi0, yHi1, 0.5f, 1.0f, tile, light, tileW, hx, hy, v0, v1);
        }
        if (connects(nb.negZ)) { // -Z：z[-Z 边, 中心]
            pushBox(verts, idx, lx, ly, lz, 0.3f, 0.7f, yLo0, yLo1, 0.0f, 0.5f, tile, light, tileW, hx, hy, v0, v1);
            pushBox(verts, idx, lx, ly, lz, 0.3f, 0.7f, yHi0, yHi1, 0.0f, 0.5f, tile, light, tileW, hx, hy, v0, v1);
        }
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
    case BlockRegistry::TallGrass: {
        // t235/t310 草丛 cross 模型：两片对角相交的双面 quad（俯视 X 形，对角占满 cell footprint）。
        //   机制等价 MC 1.0 cross 模型（tall grass / 蕨类）。两片分别沿 (+X,+Z) 与 (+X,-Z) 对角，在格中心
        //   垂直线相交成 X 形。每片整张贴 tall_grass 瓦片、双面发（pushCrossQuad）。
        //   不做邻居剔除（cross 透明 + 装饰，不挡邻居；TallGrass solid=false 亦不参与邻居面剔除）。
        //   t310 草变种（矮/中/高）：cross 高度 h 据 state 选——矮草 0.5（半格）、中草 1.0（满格，旧版外观）、
        //   高草 2.0（两格，顶点延伸进上格；同栅栏 y=1.5 越格渲染，上格必为空气，worldgen placeTallGrass 已守）。
        //   垂直 UV 仍取整张瓦片（v0..v1）→ 高草贴图被拉高 2×（草叶显更高）、矮草压缩半高 → 贴图自然表达变种。
        //   state 越界 clamp 到 TallGrassVariantMax 防异常（不应出现，兜底）。
        const int variant = std::min(int(state), int(BlockRegistry::TallGrassVariantMax));
        const float h = (variant == BlockRegistry::TallGrassShort)  ? 0.5f
                      : (variant == BlockRegistry::TallGrassTall)   ? 2.0f
                                                                   : 1.0f;
        // Plane A：对角 (0,0,0)-(1,0,1)（-X-Z 角到 +X+Z 角）；Plane B：对角 (1,0,0)-(0,0,1)（+X-Z 角到 -X+Z 角）。
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, h, 1.f,  0.f, h, 0.f, // Plane A: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, h, 1.f,  1.f, h, 0.f, // Plane B: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WheatCrop: {
        // t236 小麦作物 cross 模型：与 TallGrass 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 小麦作物（wheat crop）—— cross 模型上贴「当前生长阶段」对应的贴图：
        //   state = 阶段 0..7（0=刚种嫩芽、7=成熟金黄麦穗），mesher 在此据 state 选 tile = 基底(wheat_stage_0=29) + stage。
        //   **每阶段不同贴图**（spec「每阶段不同贴图」）—— 阶段贴图本身编码生长（嫩芽→拔高→抽穗→金黄），cross 几何
        //   满格高不变（同 MC：作物模型尺寸不变、贴图的透明像素表达「未长到的部分」）。stage 越界（state>max，不应出现）
        //   clamp 到 WheatCropStageMax 防读图集越界。不做邻居剔除（cross 透明 + 作物，同 TallGrass；WheatCrop solid=false）。
        const int stage = std::min(int(state), int(BlockRegistry::WheatCropStageMax));
        const int wheatTile = tile + stage; // tile = 基底 wheat_stage_0(29)；wheatTile = 29..36（阶段 0..7）
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      wheatTile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      wheatTile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::Sapling: {
        // t305 树苗 cross 模型：与 TallGrass 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 橡树树苗（sapling）—— cross 模型上贴 sapling(39) 瓦片（棕色短树干 + 绿色嫩叶小球冠，
        //   alpha 透明底 cutout）。**无 state 派生贴图**（树苗单一贴图；生长是清除树苗 + 生成完整树，非贴图阶段切换，
        //   区别于 WheatCrop 的 state→阶段贴图）。tile 由 BlockRegistry::tileIndex(Sapling, PosX) = sideTile = 39 给出。
        //   不做邻居剔除（cross 透明 + 树苗，同 TallGrass；Sapling solid=false）。材质 alphaCutoff:0.5 丢弃透明底。
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::DeadBush: {
        // t394 枯死的灌木 cross 模型：与 Sapling / TallGrass 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 dead bush（沙漠干旱地表枯枝装饰）—— cross 模型上贴 dead_bush(56) 瓦片（透明底 +
        //   棕褐放射干枝，alphaCutoff cutout）。**无 state 派生贴图**（枯灌木单一贴图；纯装饰，无生长 / 变种）。
        //   tile 由 BlockRegistry::tileIndex(DeadBush, PosX) = sideTile = 56 给出。不做邻居剔除（cross 透明 + 装饰，
        //   同 TallGrass；DeadBush solid=false）。材质 alphaCutoff:0.5 丢弃透明底 → 仅枯枝像素显。
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    default:
        return 0; // 非异形方块 / 未实现 → 不追加（chunkgeometry 的 continue 跳过此格）
    }
    return int(verts.size()) - startVerts;
}
