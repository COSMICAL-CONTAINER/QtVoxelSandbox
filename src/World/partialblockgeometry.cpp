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
//   pressure_plate —      （贴地薄板；state bit0=踩下（t627）→ 板高压半 1/32；机制等价 MC 压力板被压下）
//   lever/button —        （贴地薄板（同压力板几何）；state bit0=激活态。t628：按钮按下→板高压半 1/32（同压力板
//                          踩下视觉）；拉杆扳开→板体顶点色高光提亮（激活反馈，贴图不变））
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
             float tileW, float hx, float hy, float v0, float v1,
             int topTile = -1)
{
    // topTile >= 0 时 +Y 顶面（fi==2）用 topTile、其余面用 tile（t408 耕地：顶=farmland_dry / 侧·底=dirt）；
    //   默认 -1 → 全 6 面用 tile（既有 slab/stairs/fence/... 调用不变）。
    for (int fi = 0; fi < 6; ++fi) {
        const BoxFace &f = kBoxFaces[fi];
        const int ftile = (topTile >= 0 && fi == 2) ? topTile : tile;
        const float u0 = ftile * tileW + hx, u1 = (ftile + 1) * tileW - hx;
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
    case BlockRegistry::WoodSlab:
    case BlockRegistry::CobbleSlab: // t412 圆石台阶（与 WoodSlab 同几何，tile 由 tileIndex 取本方块 sideTile=cobble）
    case BlockRegistry::SpruceSlab: // t466 云杉台阶（与 WoodSlab 同几何，tile=spruce_planks）
    case BlockRegistry::StoneBrickSlab: { // t487 石砖台阶（与 WoodSlab 同几何，tile=stone_brick）
        // 半高：state bit0=上半 → y[0.5,1]；下半 → y[0,0.5]。全 footprint。
        const bool upper = (state & 1) != 0;
        const float y0 = upper ? 0.5f : 0.0f, y1 = upper ? 1.0f : 0.5f;
        pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, y0, y1, 0.f, 1.f, tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WoodStairs:
    case BlockRegistry::CobbleStairs: // t412 圆石楼梯（与 WoodStairs 同几何）
    case BlockRegistry::StoneBrickStairs: { // t487 石砖楼梯（与 WoodStairs 同几何，tile=stone_brick）
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
    case BlockRegistry::WoodFence:
    case BlockRegistry::CobbleFence: // t412 圆石墙（与 WoodFence 同几何；机制等价 MC 圆石墙）
    case BlockRegistry::SpruceFence: { // t466 云杉栅栏（与 WoodFence 同几何，tile=spruce_planks）
        // t209 栅栏 = 中心立柱（0.4 见方，1.5 高）+ 四向横档（连接相邻栅栏 / 实体方块）。
        //   立柱 y[0, 1.5] 与 collisionAABBs(ShapeFence) 同高（{0.3,0,0.3,0.7,1.5,0.7}）→ 玩家跳不过
        //   （跳跃顶点 ~1.25 < 1.5；机制等价 MC 栅栏 1.5 高不可越）。立柱顶探入上格 0.5（栅栏上格必为空气，
        //   否则碰撞亦不可能 1.5 高 → 渲染安全）。
        //   横档分上下两道（MC 式），每道从立柱中心延伸到格边；仅在该向「有连接」时画。连接判定 = 邻格为
        //   任意栅栏（WoodFence/CobbleFence，t412 经 isFence 谓词）或 isSolid（实体整立方；不连空气/水/火把/
        //   不完整方块，同 MC 栅栏只连栅栏与实体）。横档纯视觉（不进碰撞 AABB，机制等价 MC 栅栏 VoxelShape
        //   仅立柱；玩家贴立柱碰撞即可挡）。
        pushBox(verts, idx, lx, ly, lz, 0.3f, 0.7f, 0.f, 1.5f, 0.3f, 0.7f, tile, light, tileW, hx, hy, v0, v1);
        const auto connects = [](quint8 blk) {
            return BlockRegistry::isFence(blk) || BlockRegistry::isSolid(blk);
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
    // t627 压力板家族五件（wood/cobble/stone/iron/gold 同 case）+ 踩下视觉：贴地薄板（1/16 厚 + 1/16 边距）；
    //   踩下态（state bit0 = PressurePlateStatePressedFlag，updatePressurePlates 踩下沿置位 / 离开沿清位）
    //   把板高压半到 1/32（机制等价 MC 1.0 压力板被压下变矮——「踩下去」的视觉反馈）。踩下不改水平边距/碰撞。
    case BlockRegistry::WoodPressurePlate:
    case BlockRegistry::CobblePressurePlate: // t412 圆石压力板（与 WoodPressurePlate 同几何）
    case BlockRegistry::StonePressurePlate:  // t627 石压力板（家族扩展）
    case BlockRegistry::IronPressurePlate:   // t627 铁压力板（重质）
    case BlockRegistry::GoldPressurePlate: { // t627 金压力板（轻质）
        const float plateTop = (state & BlockRegistry::PressurePlateStatePressedFlag)
                                   ? (1.0f / 32.0f)   // 踩下：板高压半（1/32）
                                   : (1.0f / 16.0f);  // 常态：1/16 薄板
        pushBox(verts, idx, lx, ly, lz, 0.0625f, 0.9375f, 0.f, plateTop, 0.0625f, 0.9375f,
                tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::Lever:
    case BlockRegistry::WoodButton:
    case BlockRegistry::StoneButton: { // t490 手动点火机关（与 WoodPressurePlate 同贴地薄板几何）
        // t628 激活视觉（此前 state bit0 无任何视觉反馈）：按钮按下（state bit0）→ 板高压半到 1/32（同压力板
        //   踩下视觉 t627，机制等价 MC 按钮按下凹陷；右键按下 ~1s 后 playercontroller 自动清位弹回 1/16）。
        //   拉杆扳开（bit0）→ 板体顶点色高光提亮 ×1.3（blockregistry.h 既有「激活态切亮色高光」承诺的兑现；
        //   拉杆无自动复位——扳开保持直到再右键，高光常亮即「拉开持续激活」的视觉承载）。
        const bool active = (state & 1) != 0;
        if (BlockRegistry::isLever(blockId) && active) {
            // 拉杆 ON：复制光照上下文整体提亮（pushBox 收 const 引用故本地副本；clamp ≤1 防过曝白块）。
            PartialLightCtx lit = light;
            lit.light = std::min(lit.light * 1.3f, 1.0f);
            for (int fi = 0; fi < 6; ++fi) lit.face[fi] = std::min(lit.face[fi] * 1.3f, 1.0f);
            pushBox(verts, idx, lx, ly, lz, 0.0625f, 0.9375f, 0.f, 0.0625f, 0.0625f, 0.9375f,
                    tile, lit, tileW, hx, hy, v0, v1);
            break;
        }
        const float top = (active && !BlockRegistry::isLever(blockId))
                              ? (1.0f / 32.0f)   // 按钮按下：压半（同压力板踩下）
                              : (1.0f / 16.0f);  // 常态 / 拉杆：1/16 薄板
        pushBox(verts, idx, lx, ly, lz, 0.0625f, 0.9375f, 0.f, top, 0.0625f, 0.9375f,
                tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::WoodDoor:
    case BlockRegistry::SpruceDoor: { // t466 云杉门（与 WoodDoor 同几何；t620 起两族都据 bit3 选上下半贴图）
        // 两格高门：每格各画满高薄板（下格画门下半 / 上格画门上半，几何同 —— 区别仅在 isUpper state，
        //   用于破坏联动 & 朝向同步）。朝向 state[1:0]（0=+X 1=-X 2=+Z 3=-Z）、开合 state bit2。
        //   t620 per-face：此前上下半共用 planks/spruce_planks 单贴图；现据 state bit3（上/下格）选
        //   upper/lower 专属贴图（机制等价 MC 1.0 门两格高模型——下格门板 / 上格带窗）。kDefs 的
        //   topTile=upper / bottomTile=lower 承载该选择（WoodDoor 143/144、SpruceDoor 145/146；
        //   tileIndex(PosX)=sideTile=lower 是手持 / 掉落物 BlockCube 的门贴图）。
        const BlockRegistry::BlockDef &doorDef = BlockRegistry::def(blockId);
        const int doorTile = (state & 8) ? doorDef.topTile : doorDef.bottomTile; // bit3=上格 → upper / 下格 → lower
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
        pushBox(verts, idx, lx, ly, lz, bx0, bx1, 0.f, 1.f, bz0, bz1, doorTile, light, tileW, hx, hy, v0, v1);
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
    case BlockRegistry::SweetBerryBush: {
        // t467 雪原浆果灌木丛 cross 模型：与 WheatCrop 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 sweet berry bush —— cross 模型上贴「当前生长阶段」对应的贴图：state = 阶段
        //   0..SweetBerryBushStageMax（0=无果嫩丛、1=小果、2=成熟），mesher 在此据 state 选 tile = 基底
        //   (sweet_berry_bush_0=103) + stage（3 视觉阶段，同小麦的基底 + state 全阶段贴图模式）。cross 几何满格高
        //   不变（同 MC：丛模型尺寸不变、贴图编码「果实多寡」）。stage 越界（state>max，不应出现）clamp 到
        //   SweetBerryBushStageMax 防读图集越界。不做邻居剔除（cross 透明 + 灌木，同 TallGrass / WheatCrop；
        //   SweetBerryBush solid=false）。材质 alphaCutoff:0.5 丢弃透明底（仅丛像素显）。
        const int stage = std::min(int(state), int(BlockRegistry::SweetBerryBushStageMax));
        const int bushTile = tile + stage; // tile = 基底 sweet_berry_bush_0(103)；bushTile = 103..105（阶段 0..2）
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      bushTile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      bushTile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::CarrotCrop:
    case BlockRegistry::PotatoCrop: {
        // t407 胡萝卜/马铃薯作物 cross 模型：与 WheatCrop 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 carrot/potato 作物 —— MC 仅 4 张阶段贴图覆盖 8 个年龄（age 0-1→tex0、2-3→tex1、
        //   4-5→tex2、6-7→tex3），故本处据 state 选 tile = 基底 + state/2（4 视觉阶段），区别于小麦的基底 + state
        //   （全 8 阶段贴图）。state（age）仍 0..7、age 7 成熟（WheatCropStageMax 复用），与小麦同生长机制；
        //   仅贴图张数对齐 MC（少画 4 张纹理、观感不减）。stage 越界（state>max，不应出现）clamp 到 max 防读图集越界。
        //   不做邻居剔除（cross 透明 + 作物，同 WheatCrop；CarrotCrop/PotatoCrop solid=false）。材质 alphaCutoff:0.5
        //   丢弃透明底。tile = 基底（CarrotCrop def 各面=69 / PotatoCrop def 各面=73）。
        const int stage = std::min(int(state), int(BlockRegistry::WheatCropStageMax));
        const int cropTile = tile + (stage / 2); // 4 阶段贴图：基底 + state/2（CarrotCrop 69..72 / PotatoCrop 73..76）
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      cropTile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      cropTile, light, tileW, hx, hy, v0, v1);
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
    case BlockRegistry::Mushroom: {
        // t396 蘑菇 cross 模型：与 Sapling / DeadBush 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 蘑菇（沼泽 / 阴暗草地小蘑菇）—— cross 模型上贴 mushroom(62) 瓦片（透明底 + 米色菌柄 +
        //   红底白斑菌盖，alphaCutoff cutout）。**无 state 派生贴图**（蘑菇单一贴图；纯装饰，无生长 / 变种）。
        //   tile 由 BlockRegistry::tileIndex(Mushroom, PosX) = sideTile = 62 给出。不做邻居剔除（cross 透明 + 装饰，
        //   同 TallGrass；Mushroom solid=false）。材质 alphaCutoff:0.5 丢弃透明底 → 仅蘑菇像素显。
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::FlowerRed: case BlockRegistry::FlowerYellow:
    case BlockRegistry::FlowerBlue:  case BlockRegistry::FlowerWhite: {
        // t397 花 cross 模型：与 Sapling / Mushroom 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 花（poppy / dandelion 等）—— cross 模型上贴 flower_<color> 瓦片（tile 63..66；透明底
        //   + 绿茎 + 彩色花头，alphaCutoff cutout）。**无 state 派生贴图**（每色单一贴图；纯装饰，无生长 / 变种）。
        //   4 色合用同一 case（switch fallthrough；tile 由 BlockRegistry::tileIndex(<color>, PosX) 各自 sideTile 给出，
        //   故每色仍按各自方块 id 取贴图，机制等价 MC「各色花各有贴图」）。spec「thin like tall grass」即满格 cross。
        //   不做邻居剔除（cross 透明 + 装饰，同 TallGrass；Flower solid=false）。材质 alphaCutoff:0.5 丢弃透明底 →
        //   仅花像素显。
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::Sugarcane: {
        // t397 甘蔗 cross 模型：与花 / 草丛同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 sugar cane（甘蔗 / 芦苇）—— cross 模型上贴 sugarcane(67) 瓦片（透明底 + 绿色节段细茎
        //   + 顶部尖叶，alphaCutoff cutout）。甘蔗细茎观感由贴图 alpha 表达（透明底丢弃 → 仅茎像素显），cross 几何
        //   满格不变（同花 / 草丛）。**可叠高 1..3 格**：worldgen placeSugarcane 在水边列逐格向上仅写空气格堆叠甘蔗
        //   方块，每格独立走本 case 各画满高 cross → 视觉如细茎柱（机制等价 MC 甘蔗 1..3 格柱）。
        //   **无 state 派生贴图**（甘蔗单一贴图；叠高靠堆方块非 state 阶段，区别于 WheatCrop）。
        //   tile 由 BlockRegistry::tileIndex(Sugarcane, PosX) = sideTile = 67 给出。不做邻居剔除（cross 透明 + 植物，
        //   同 TallGrass；Sugarcane solid=false）。材质 alphaCutoff:0.5 丢弃透明底 → 仅茎像素显。
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::LilyPad: {
        // t396 睡莲「横向浮叶」模型：**一片水平双面 quad 贴 cell 底部**（y≈1/16，刚好浮于水面）—— 与竖直 cross
        //   （草丛 / 蘑菇等两片对角 X）不同，睡莲是平铺水面的圆叶，故几何为水平 quad 非竖直 cross。机制等价
        //   MC 1.0 lily pad（沼泽浅水水面浮叶）。worldgen 把本方块置于水格上方一格（y = 水面 + 1），其 cell 底部
        //   quad（world y ≈ 水面 + 1/16）恰浮于水面之上 → 视觉如叶片贴水。
        //   复用 pushCrossQuad（它对任意 4 共面角点发正反双面三角形 → 水平 quad 同样双面可见，从水面上下均见叶）。
        //   四角取 cell 全 footprint（xz [0,1]）+ y=1/16（略高于 cell 底防 z-fight 水面）；UV 满铺整张瓦片（圆叶由
        //   贴图 alpha cutout 表达，几何仍为整张 quad）。不做邻居剔除（透明 + 浮叶，同 cross；LilyPad solid=false）。
        //   材质 alphaCutoff:0.5 丢弃透明底 → 仅圆叶像素显（V 形缺口由贴图 alpha 表达）。
        //   tile 由 BlockRegistry::tileIndex(LilyPad, PosX) = sideTile = 61 给出。
        constexpr float yp = 1.0f / 16.0f; // 浮叶高度（cell 底以上 1/16，贴水面防 z-fight）
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, yp, 0.f,  1.f, yp, 0.f,  1.f, yp, 1.f,  0.f, yp, 1.f, // 水平 quad：BL→BR→TR→TL（xz 全 footprint）
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::Ladder: {
        // t413/t501/t519 木梯「单片贴墙 quad」模型（机制等价 MC 1.0 ladder 贴实体方块面）：t501 把 t413 的两片
        //   对角 cross 改为**单片贴墙 quad** —— 据所贴墙面水平方向（state[1:0]，placeBlock 经 ladderFaceFromNormal
        //   写入）把 quad 摆到对应面、贴图朝外（朝玩家侧）。须贴**完整立方方块**的侧面（placeBlock placeState
        //   算好后经 isFullCube 守卫；草丛/门/活版门等不完整方块侧拒放）。
        //   state 编码：0=+X 1=-X 2=+Z 3=-Z（=「支撑墙所在的水平方向」）。
        //   quad 位置：贴所贴面（cell 边 - 1/16 留嵌墙余量，防 z-fight + 视觉如梯嵌墙），覆盖另两轴全 [0,1]。
        //   贴图朝外：法线背离墙面（朝玩家侧）。pushCrossQuad 发正反双面三角形 → 玩家从面侧见梯正面、绕到
        //   墙后（透视）亦见背面（机制对标 MC 木梯单面贴图双面可见）。Ladder solid=false（不挡邻居面剔除，
        //   同草丛）；材质 alphaCutoff:0.5 丢弃透明底 → 仅梯像素显（两根纵轨 + 横向梯级 cutout）。无 state 派生
        //   贴图（单一梯瓦片）。tile 由 BlockRegistry::tileIndex(Ladder, PosX) = sideTile = 78 给出。
        //   UV 映射对齐 kBoxFaces 轴面约定（±X 面 cu=z,cv=y；±Z 面 cu=x,cv=y）：角点序 BL→BR→TR→TL 使贴图
        //   纵向（两根纵轨，沿贴图 V/行）映射到 world Y（垂直）→ 梯轨竖立、横级水平（贴图正向不旋转）。
        //   t519「放下形状上下宽粗糙」修复：几何（单片贴墙 quad 铺满 face [0,1]）本身即 MC 1.0 ladder 正确做法
        //   （薄板贴墙 + cutout 梯级，单面贴图双面可见），根因在贴图比例 —— 旧贴图纵轨居中瓦片中央 8/16 宽
        //   （x=4/5,10/11）+ 两侧各 4/16 透明留白 → 整张贴图铺满 face 后梯子只显在格中心半宽、两侧大块透明 →
        //   观感「格中央小梯图标、粗糙」。t519 改贴图满格（tools/build_ladder.py 纵轨贴瓦片两侧 x=2/3,12/13 +
        //   横级满铺轨间）→ 整张贴图铺满 face 后梯子铺满整格宽，读作「贴墙的一把梯子」（机制等价 MC 1.0 ladder
        //   贴图：轨靠边 + rung 满轨间，整张无大块透明留白）。几何不变（已是 MC 1.0 正确），仅同步注释说明。
        //   t538（用户复核「放下形状仍显粗糙 / 上下太宽」）：核查确认 t519 贴图**已真进** atlas（tile 78 逐像素
        //   一致）+ icon_ladder.png + 本渲染路径（tile 78）。用户「感觉没什么变化」的根因 = 启用的 demo 资源包
        //   （settings.json resourcePackEnabled）把 tile 78 覆盖成包内 128px ladder.png 的平滑缩小版 → t519 默认贴图
        //   在用户会话不可见。仍按 spec 收窄薄板：水平方向从满格 0..1 收到 12/16（两侧各 2/16 内缩，见 kPlateMargin）
        //   —— 无论默认 / 包贴图，放下都读作「贴墙的窄薄板梯子」而非满格宽板（贴墙薄板非厚块）。高度保持满高。
        constexpr float kInset = 1.0f / 16.0f; // 贴墙内缩（cell 边以内 1/16，留嵌墙余量防 z-fight）
        constexpr float kPlateMargin = 2.0f / 16.0f; // t538 薄板水平内缩：两侧各 2/16 → 梯板 12/16 宽（高 16/16，
                                                     //   读作「贴墙的窄薄板梯子」而非满格宽厚的板）。梯子比例更窄。
        const int face = state & 3;
        if (face == 0 || face == 1) {
            // 支撑墙 ±X：quad 贴 x 面（face 0=+X 边 1-kInset；face 1=-X 边 kInset），
            //   y 满高 [0,1]、z 水平内缩 [kPlateMargin, 1-kPlateMargin]（U→z 横级水平、V→y 纵轨竖立）。
            const float xw = (face == 0) ? (1.0f - kInset) : kInset;
            const float z0 = kPlateMargin, z1 = 1.0f - kPlateMargin;
            pushCrossQuad(verts, idx, lx, ly, lz,
                          xw, 0.f, z0,  xw, 0.f, z1,  xw, 1.f, z1,  xw, 1.f, z0, // BL→BR→TR→TL（U=z,V=y）
                          tile, light, tileW, hx, hy, v0, v1);
        } else {
            // 支撑墙 ±Z：quad 贴 z 面（face 2=+Z 边 1-kInset；face 3=-Z 边 kInset），
            //   y 满高 [0,1]、x 水平内缩 [kPlateMargin, 1-kPlateMargin]（U→x 横级水平、V→y 纵轨竖立）。
            const float zw = (face == 2) ? (1.0f - kInset) : kInset;
            const float x0 = kPlateMargin, x1 = 1.0f - kPlateMargin;
            pushCrossQuad(verts, idx, lx, ly, lz,
                          x0, 0.f, zw,  x1, 0.f, zw,  x1, 1.f, zw,  x0, 1.f, zw, // BL→BR→TR→TL（U=x,V=y）
                          tile, light, tileW, hx, hy, v0, v1);
        }
        break;
    }
    case BlockRegistry::Cobweb: {
        // t484 蜘蛛网 cross 模型：与 TallGrass / Ladder 同款两片对角相交双面 quad（满格高 0..1，俯视成 X 形）。
        //   机制等价 MC 1.0 蛛网（cobweb）—— cross 模型上贴 cobweb(120) 瓦片（透明底 + 灰白蛛丝放射网纹，
        //   alphaCutoff cutout）。蛛网无碰撞（ShapeNone → 玩家穿过；本工程简化不做减速系统，纯装饰）。两片对角
        //   cross 使蛛网从四面均可见（玩家绕到背面仍见蛛丝）。**无 state 派生贴图**（单一蛛网瓦片）。tile 由
        //   BlockRegistry::tileIndex(Cobweb, PosX) = sideTile = 120 给出。不做邻居剔除（cross 透明 + 蛛网，同
        //   TallGrass；Cobweb solid=false）。材质 alphaCutoff:0.5 丢弃透明底 → 仅蛛丝像素显。
        pushCrossQuad(verts, idx, lx, ly, lz,
                      0.f, 0.f, 0.f,  1.f, 0.f, 1.f,  1.f, 1.f, 1.f,  0.f, 1.f, 0.f, // Plane A: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        pushCrossQuad(verts, idx, lx, ly, lz,
                      1.f, 0.f, 0.f,  0.f, 0.f, 1.f,  0.f, 1.f, 1.f,  1.f, 1.f, 0.f, // Plane B: BL→BR→TR→TL
                      tile, light, tileW, hx, hy, v0, v1);
        break;
    }
    case BlockRegistry::Rail: {
        // t484 铁轨「贴地薄板」模型：**一片水平双面 quad 贴 cell 底部**（y≈1/16，刚好浮于地面之上）—— 与睡莲
        //   横向浮叶（LilyPad）同源几何，区别仅贴图（rail 瓦片 = 透明底 + 棕色枕木 + 灰铁双轨）。
        //   t565 连接 / 转弯（机制等价 MC 1.0 rail 自动连接 + 拐角）：state 4 位 = 水平 4 向连接
        //   （RailConnPx/Nx/Pz/Nz；placeBlock / World::checkRailOnEdit 与邻轨互连时写入）。据连接位选形态：
        //     - NS（默认 / 仅 ±Z 连接 / 0-1 连接且唯一连接向 Z）：整片 quad + tile 121（直轨沿 Z；贴图内
        //       双轨为纵向线）→ 铺满 cell，UV 标准映射（u→x、v→z）。
        //     - EW（仅 ±X 连接 / 0-1 连接且唯一连接向 X）：同 tile 121，但角点绕序旋转 90°（u→z、v→x）→
        //       贴图旋转、双轨沿线 X 向（机制等价 MC rail NS/EW 两种直轨形态，一张贴图两用省瓦片）。
        //     - 拐角（邻向 2 连接，如 +X+Z）：tile 136（贴图语义 = 轨自南(+Z)进入向左(-X)弯出）。四向
        //       拐角经「角点绕序旋转 / 镜像」把该贴图映射到四个象限（一张贴图四用）。
        //     - 十字（3+ 连接）：tile 137（南北 + 东西双轨叠交 + 中央方枕木）。
        //   0 连接（孤轨）默认 NS（state=0 兜底，旧存档 / 防御）。不做邻居剔除（透明 + 薄板，同睡莲；
        //   Rail solid=false）。材质 alphaCutoff:0.5 丢弃透明底 → 仅轨像素显。
        constexpr float yr  = 1.0f / 16.0f;  // 铁轨厚度（cell 底以上 1/16，贴地板防 z-fight）
        const int railTile = BlockRegistry::tileIndex(BlockRegistry::Rail, BlockRegistry::PosX); // 121
        const quint8 con = state;
        const bool cpx = (con & BlockRegistry::RailConnPx) != 0;
        const bool cnx = (con & BlockRegistry::RailConnNx) != 0;
        const bool cpz = (con & BlockRegistry::RailConnPz) != 0;
        const bool cnz = (con & BlockRegistry::RailConnNz) != 0;
        const int nConn = int(cpx) + int(cnx) + int(cpz) + int(cnz);
        // 水平 quad 通用：BL→BR→TR→TL 四角（UV (0,0)(1,0)(1,1)(0,1)；pushCrossQuad 内部固定该映射）。
        //   NS（标准）：u→x、v→z —— (x,z) = (0,0)(1,0)(1,1)(0,1)。
        //   EW（旋转 90°）：u→z、v→x —— (x,z) = (0,0)(0,1)(1,1)(1,0)。
        //   拐角镜像：以「南进西出」贴图为基准（v=1 是贴图底行 = 南入口侧），四象限经轴翻转映射。
        const auto pushRail = [&](float y, int tileIdx,
                                  float ax0, float az0, float ax1, float az1,
                                  float ax2, float az2, float ax3, float az3) {
            pushCrossQuad(verts, idx, lx, ly, lz,
                          ax0, y, az0,  ax1, y, az1,  ax2, y, az2,  ax3, y, az3,
                          tileIdx, light, tileW, hx, hy, v0, v1);
        };
        if (nConn >= 3) {
            // 十字：tile 137 整片。
            pushRail(yr, 137, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f);
        } else if (nConn == 2 && ((cpx || cnx) && (cpz || cnz))) {
            // 拐角（一个 X 向 + 一个 Z 向连接）。贴图基准（tile 136 贴图坐标）：v=1（UV 底行）= 南（+Z）入口侧、
            //   u=0（UV 左列）= 西（-X）出口侧 → 贴图语义 = 轨自南进入向左（西）弯出 = 基准形态连接 {+Z,-X}。
            //   四象限经轴翻转映射（BL=(u0,v0) BR=(u1,v0) TR=(u1,v1) TL=(u0,v1)，世界角按翻转后 u/v 轴取）：
            //     {+Z,-X} 基准 NS 序（u→x、v→z）；{+Z,+X} 东西镜像（u 翻）；{-Z,-X} 南北镜像（v 翻）；
            //     {-Z,+X} 双翻（180°）。轨两端恰落两个连接向上（镜像不改拓扑，只改贴图朝向）。
            if (cpz && cnx)       pushRail(yr, 136, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f);
            else if (cpz && cpx)  pushRail(yr, 136, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f);
            else if (cnz && cnx)  pushRail(yr, 136, 0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f);
            else                  pushRail(yr, 136, 1.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f);
        } else if (nConn == 1 || (nConn == 2 && (cpx || cnx))) {
            // EW 直轨：仅 X 向连接（1 个或对向 2 个）→ 贴图旋转 90°（u→z、v→x）。
            pushRail(yr, railTile, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 0.f);
        } else {
            // NS 直轨（默认：0 连接 / 仅 Z 向连接 / 对向 ±Z）。
            pushRail(yr, railTile, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f);
        }
        break;
    }
    case BlockRegistry::Cactus: {
        // t445 仙人掌细柱：机制对标 MC 1.0 仙人掌 14/16（~0.875）宽的居中柱。本工程取 0.8（X/Z [0.1,0.9]）
        //   居中、Y 满高 [0,1]，整柱贴 cactus 顶 / 侧贴图。**非满格整立方** —— cactus solid=false（同 Farmland /
        //   glass），mesher 路由进 PASS 1（chunkgeometry），不进 PASS 2 立方面（否则满格立方覆盖细柱）。
        //   全 6 面发（pushBox 不剔面）：+Y 顶面 cactus_top(54)（def.topTile，露出柱顶绿截面环纹）、侧·底
        //   cactus_side(55)（tile = tileIndex(PosX)=sideTile；底贴侧贴图不可见 —— 柱底压在沙 / 下段仙人掌上）。
        //   仙人掌柱（worldgen 1-3 格 / 玩家叠放）每格独立走本 case 各画满高 0.8 柱 → 视觉如整根细柱（段间
        //   顶 / 底面相贴不可见，overdraw 可忽）。不做邻居剔除（异形小体约定，同 Farmland）。topTile 取 def.topTile(54)。
        constexpr float kCactusInset = 0.1f; // (1 - 0.8) / 2 = 0.1（X/Z 内缩，居中 0.8 见方）
        pushBox(verts, idx, lx, ly, lz,
                kCactusInset, 1.0f - kCactusInset, 0.f, 1.f, kCactusInset, 1.0f - kCactusInset,
                tile, light, tileW, hx, hy, v0, v1,
                BlockRegistry::def(blockId).topTile); // +Y 顶面 cactus_top(54)；侧·底用 tile(=cactus_side 55)
        break;
    }
    case BlockRegistry::SnowLayer: {
        // t505 积雪层薄板（机制等价 MC 1.0 snow layer 8 层）：贴地薄板，高度由 state 驱动（state 0..7 → 高度
        //   (state+1)/8，1/8..1.0 八级；snowLayerHeight 单一权威）。全 footprint xz[0,1]、y[0, height]：+Y 顶面
        //   snow(57)（def.topTile，玩家踩薄层顶所见）、侧·底 snow(57)（同 tile；侧面露出薄层高度带，底部不可见）。
        //   **非满格整立方** —— SnowLayer solid=false（同 Farmland / glass / Cactus），mesher 路由进 PASS 1
        //   （chunkgeometry），不进 PASS 2 立方面（否则满格立方覆盖薄板）。相邻整立方不剔面（solid=false）→
        //   画出满高侧壁填住薄层上方缺口（防透视 x-ray 洞，同 Farmland 模式）。全 6 面发（pushBox 不剔面；
        //   异形小体约定，内 / 底面被自身或下方实体遮挡，overdraw 可忽）。state 越界 clamp 由 snowLayerHeight 兜底。
        //   topTile 取 def.topTile(57) → +Y 顶面贴 snow 贴图；侧·底用 tile(=sideTile snow 57)。
        const float h = BlockRegistry::snowLayerHeight(state);
        pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, 0.f, h, 0.f, 1.f,
                tile, light, tileW, hx, hy, v0, v1,
                BlockRegistry::def(blockId).topTile); // +Y 顶面 snow(57)；侧·底用 tile(=sideTile snow 57)
        break;
    }
    case BlockRegistry::Farmland: {
        // t408 耕地矮盒：机制等价 MC 耕地比整立方矮 1 像素（15/16=0.9375）→ 顶面略陷，相邻整立方（草地等）上方
        //   露出 1/16 唇。全 footprint、y[0, 0.9375]：顶面 farmland_dry(26)（湿润暗化由 mesher 在 lctx.face[+Y] 预乘
        //   farmlandHydrBrightMul 体现，darker=wetter），侧·底 dirt(2)。耕地走 solid=false（见 BlockDef）→ 相邻整立方
        //   不剔面、画满高侧壁填住矮盒上方 1/16 缺口（防透视 x-ray 洞，同 glass 模式）。不做邻居剔除（异形小体约定；
        //   内/底面被自身或邻实体遮挡，overdraw 可忽）。topTile 取 def.topTile(26) → +Y 顶面贴耕地贴图。
        constexpr float kFarmlandTop = 0.9375f; // 15/16（与 collisionAABBs 耕地特例同高）
        pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, 0.f, kFarmlandTop, 0.f, 1.f,
                tile, light, tileW, hx, hy, v0, v1,
                BlockRegistry::def(blockId).topTile); // +Y 顶面 farmland_dry(26)，侧·底用 tile(=sideTile dirt 2)
        break;
    }
    case BlockRegistry::EnchantingTable: {
        // t620 附魔台 0.75 高矮盒（机制等价 MC 1.0 附魔台 12/16 高非整块；此前 t474 简化为整立方，pack 贴图
        //   接入时改半高）。全 footprint、y[0, 0.75]：顶面 enchanting_table_top(109)（def.topTile，钻石纹 + 立书
        //   轮廓；pack 侧=enchanting_table_top.png）、侧·底 enchanting_table_side(110)（tile=sideTile；pack 合成
        //   时已裁掉 enchanting_table_side.png 顶部 0.25 空白 → 有效 0.75 部分整张贴 0.75 高侧面无缝；非 pack
        //   程序贴图满 16px 无空白 → 整张竖压 0.75×，同 slab 侧面压扁的可接受降级）。**顶部立书装饰未做**
        //   （MC 附魔台顶上有一本摊开的书；本工程顶面贴图自带立书轮廓读感，独立小盒留后续——几何简单优先）。
        //   solid=false（见 BlockDef）→ 相邻整立方不剔面、画满高侧壁填住上方 0.25 缺口（防透视 x-ray 洞，
        //   同 Farmland / glass 模式）；碰撞矮盒 0.75（collisionAABBs 特例）；selection/raycast 仍整格（ShapeFull）。
        //   不做邻居剔除（异形小体约定，同 Farmland）。底面压在下方实体上不可见，overdraw 可忽。
        constexpr float kEnchantTop = 0.75f; // 12/16（与 collisionAABBs 附魔台特例同高）
        pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, 0.f, kEnchantTop, 0.f, 1.f,
                tile, light, tileW, hx, hy, v0, v1,
                BlockRegistry::def(blockId).topTile); // +Y 顶面 enchanting_table_top(109)；侧·底用 tile(=sideTile 110)
        break;
    }
    case BlockRegistry::BedRed: case BlockRegistry::BedOrange: case BlockRegistry::BedYellow:
    case BlockRegistry::BedGreen: case BlockRegistry::BedCyan: case BlockRegistry::BedBlue:
    case BlockRegistry::BedMagenta: case BlockRegistry::BedBlack:
    case BlockRegistry::BedWhite: case BlockRegistry::BedLightBlue: case BlockRegistry::BedLime:
    case BlockRegistry::BedPink: case BlockRegistry::BedGray: case BlockRegistry::BedLightGray:
    case BlockRegistry::BedPurple: case BlockRegistry::BedBrown: {
        // t457/t496 低 3D 床模型（cell-local [0,1]）：四角木柱腿 + 木板床架 + 彩色被面床垫 + 床头板（head 半）/ 床尾板
        //   （foot 半）+ 床头端白色枕头（head 半）。机制等价 MC 1.0 床模型（低矮床架 + 床垫 + 床头板 + 枕头，非整立方）。
        //   每半（foot/head）独立渲染本 case，两半并排组成完整床（玩家放置 foot/head 双格横置，见 playercontroller
        //   placeBlock）。腿 / 床架 / 床头板 / 床尾板贴 planks tile(8)；床垫贴床色 tile（tile）；枕头贴白色 wool tile(38)
        //   （机制等价 MC 床头白色枕头，与被面色区分）。
        //
        //   **t496 重设计根因**（旧版「丑」）：旧版床垫仅 1/16 厚（plankTop 4/16 → matTop 5/16）→ 视觉薄如平板；
        //   床贴图顶部带「枕垫亮带」区，床垫六面铺同图 → 枕垫区出现在床垫顶面（与真正的枕头盒重复）+ 床垫侧面
        //   把 5 行枕垫区压在 1/16 厚度上 → 拉伸糊化；无床头板 / 床尾板 → 看不出是床（像带凸起的平板）。t496 修：
        //   (a) 贴图改纯绗缝被面（build_bed.py，无枕垫区）→ 床垫顶 / 侧面都干净不糊；
        //   (b) 床头板（head 半床头端竖立木板，高于床垫）+ 床尾板（foot 半床尾端矮木板）→ 一眼辨「这是床」
        //       （机制等价 MC 床头板 / 床尾板）；
        //   (c) 枕头改白色 wool tile(38)（旧版同被色 → 与床垫糊成一片分不清）；
        //   (d) 腿改「外角 2 条腿」（head 半的 -front 端两角 / foot 半的 +front 端两角）→ 双格并排共 4 条腿
        //       （旧版每半 4 条腿 → 双格 8 条腿 + 内侧腿并贴成裙边，观感乱）。
        //   不做邻居剔除（异形小体约定，同 Farmland；内 / 底面被自身遮挡 overdraw 可忽）。床 solid=false
        //   （shapeBoxes 走 ShapeBed 低盒 y[0, kBedMattressTop]）→ 不参与邻居整立面剔除（无 x-ray 洞）。
        //   床头板 / 床尾板 / 枕头凸出碰撞盒顶（纯视觉），机制等价 MC 床低 hitbox + 视觉床头板凸出。
        const int planksTile = BlockRegistry::tileIndex(BlockRegistry::Planks, BlockRegistry::PosX); // 木板瓦片 8
        const int woolTile = BlockRegistry::tileIndex(BlockRegistry::Wool, BlockRegistry::PosX); // 白色羊毛瓦片 38（枕头）
        const float leg = BlockRegistry::kBedLegHalf;          // 腿半宽（角柱 2*leg 见方）
        const float legTop = BlockRegistry::kBedLegTop;        // 腿顶 = 床架底
        const float plankTop = BlockRegistry::kBedPlankTop;    // 床架顶 = 床垫底
        const float matTop = BlockRegistry::kBedMattressTop;   // 床垫顶（碰撞盒顶 ~0.31）
        const float ins = BlockRegistry::kBedInset;            // 床垫 footprint 内缩
        const bool isHead = (state & 8) != 0; // head 半（床头端）= true
        const int f = state & 3;              // head→foot 方向 0=+X 1=-X 2=+Z 3=-Z（head 落 foot 的 -front 邻格）

        // t496 外端低角（cell-local）沿床长轴。head 半的外端 = 远离 foot 的端（-front 方向端）；foot 半的外端 =
        //   远离 head 的端（+front 方向端）。双格并排时 head 外端 + foot 外端分属床的两端 → 共 4 条腿 + 两端各一块板
        //   （机制等价 MC 床 4 角腿 + 床头 / 床尾板）。长轴 = front 方向（f=0/1 → X / f=2/3 → Z）。front 正向
        //   （f=0/2）→ foot 外端在 + 轴（lowCorner=1-thick）/ head 外端在 - 轴（lowCorner=0）；front 负向（f=1/3）→ 反之。
        //   旧版用一个 sgn 符号映射两端，对 f=1/3 反向 → 腿 / 板落到格边界（床中间）而非两端（用户复盘「朝 -X 放床
        //   横在两格中间凸起」根因）；改显式 per-f 低角，腿 / 板稳落两端。
        //   outerLow(thick) = 本半外端沿长轴的低角坐标（0 或 1-thick）。
        const bool longAxisIsX = (f == 0 || f == 1);
        const bool frontPositive = (f == 0 || f == 2);          // front 指向 +轴 / -轴
        const bool footOuterPositive = frontPositive;           // foot 外端在 +front 方向
        const bool outerPositive = isHead ? !footOuterPositive : footOuterPositive; // 本半外端方向
        auto outerLow = [](bool positive, float thick) { return positive ? (1.f - thick) : 0.f; };

        // (a) 木柱腿：外端两角各一条腿（2*leg 见方角柱），y[0, legTop]，planks 贴图。宽轴取两端 ±2leg 角。
        if (longAxisIsX) {
            const float ex0 = outerLow(outerPositive, 2.f * leg);
            pushBox(verts, idx, lx, ly, lz, ex0, ex0 + 2 * leg, 0.f, legTop, 0.f, 2 * leg, planksTile, light, tileW, hx, hy, v0, v1);
            pushBox(verts, idx, lx, ly, lz, ex0, ex0 + 2 * leg, 0.f, legTop, 1.f - 2 * leg, 1.f, planksTile, light, tileW, hx, hy, v0, v1);
        } else {
            const float ez0 = outerLow(outerPositive, 2.f * leg);
            pushBox(verts, idx, lx, ly, lz, 0.f, 2 * leg, 0.f, legTop, ez0, ez0 + 2 * leg, planksTile, light, tileW, hx, hy, v0, v1);
            pushBox(verts, idx, lx, ly, lz, 1.f - 2 * leg, 1.f, 0.f, legTop, ez0, ez0 + 2 * leg, planksTile, light, tileW, hx, hy, v0, v1);
        }

        // (b) 木板床架：全 footprint 薄板，planks 贴图（承托床垫的木框）。沿长轴全 [0,1] → 两半并排时床架在
        //   格边界处对接成连续木框，无中间断口（与床垫 (c) 同样满长轴，保证床体中段不空）。
        pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, legTop, plankTop, 0.f, 1.f, planksTile, light, tileW, hx, hy, v0, v1);

        // (c) 彩色被面床垫（t496 填实中间空隙）：床色贴图（被面包裹的床垫）。**沿床长轴满 [0,1]**（两半在格边界
        //   处对接成一张连续床垫 → 中间无空隙，spec t496「床头床尾中间羊毛处空隙要填实」），仅沿宽轴内缩 [ins,1-ins]
        //   留出两侧床架边缘可见（被面侧边凹入床框，机制等价 MC 床垫嵌入床架）。y[plankTop, matTop]。
        //   旧版床垫四边都内缩 → 沿长轴两端各空 ins(2/16) → 两半对接时长轴边界处出现 4/16 宽空缝（用户复盘
        //   「中间空」）；改满长轴后床垫在格边界连续，空缝消失。
        if (f == 0 || f == 1) {
            // 长 X 轴满 [0,1]，宽 Z 轴内缩。
            pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, plankTop, matTop, ins, 1.f - ins, tile, light, tileW, hx, hy, v0, v1);
        } else {
            // 长 Z 轴满 [0,1]，宽 X 轴内缩。
            pushBox(verts, idx, lx, ly, lz, ins, 1.f - ins, plankTop, matTop, 0.f, 1.f, tile, light, tileW, hx, hy, v0, v1);
        }

        // (d) 床头板（head 半）/ 床尾板（foot 半）：外端竖立木板。床头板高（kBedHeadboardTop 9/16），
        //   床尾板矮（kBedFootboardTop 7/16），机制等价 MC 床头板高于床尾板。板厚 kBedBoardThick(2/16 = kBedInset)
        //   贴外端边缘，跨越全宽（含床架两侧 → 板与床垫宽轴内缩留出的侧条带共面填满，零共面 z-fight）。
        //   板 y[plankTop, boardTop] —— 坐在床架平台上（不覆盖腿区，腿区 y[0,legTop] 在板下方独立可见）。
        //   仅外端有板（head 外端 = 床头板 / foot 外端 = 床尾板）；内端（格边界侧）无板 → 两半对接处床垫连续过渡。
        //   外端低角走 (a) 同一 outerLow（per-f 显式），保证 f=1/3 板也落床端而非格边界。
        const float boardThick = BlockRegistry::kBedBoardThick;
        const float boardTop = isHead ? BlockRegistry::kBedHeadboardTop : BlockRegistry::kBedFootboardTop;
        if (longAxisIsX) {
            // 长轴 X → 板竖立在 X 外端（与腿同 X 端），跨越全 z 宽。
            const float bx0 = outerLow(outerPositive, boardThick);
            pushBox(verts, idx, lx, ly, lz, bx0, bx0 + boardThick, plankTop, boardTop, 0.f, 1.f, planksTile, light, tileW, hx, hy, v0, v1);
        } else {
            // 长轴 Z → 板竖立在 Z 外端，跨越全 x 宽。
            const float bz0 = outerLow(outerPositive, boardThick);
            pushBox(verts, idx, lx, ly, lz, 0.f, 1.f, plankTop, boardTop, bz0, bz0 + boardThick, planksTile, light, tileW, hx, hy, v0, v1);
        }

        // (e) 白色枕头（仅 head 半）：床头端（head 外端，与床头板同端）床垫上方的白色 wool 枕。机制等价 MC 床头枕头
        //   （区分头/脚端 + 白色与被面色对比）。枕头占床头端 pLen(3/8) 长 × 床垫宽轴宽（宽轴内缩 = 床垫内缩 ins，
        //   与床垫侧边平齐 → 被面侧边在枕外仍可见一条），y[matTop, kBedPillowTop]，wool tile(38) 白色。
        //   长轴贴 head 外端（outerPositive 决定起角 0 或 1-pLen），宽轴内缩 [ins,1-ins] —— 与 (a)/(d) 同 outerLow 约定。
        if (isHead) {
            const float pLen = 0.375f;  // 枕头沿床长方向占 6/16（床头端 3/8，留出大半被面）
            const float pEdge = ins;    // 枕头沿宽轴内缩 = 床垫内缩（与床垫侧边平齐）
            const float l0 = outerLow(outerPositive, pLen); // 长轴低角（0 或 1-pLen）
            if (longAxisIsX) {
                pushBox(verts, idx, lx, ly, lz, l0, l0 + pLen, matTop, BlockRegistry::kBedPillowTop, pEdge, 1.f - pEdge,
                        woolTile, light, tileW, hx, hy, v0, v1);
            } else {
                pushBox(verts, idx, lx, ly, lz, pEdge, 1.f - pEdge, matTop, BlockRegistry::kBedPillowTop, l0, l0 + pLen,
                        woolTile, light, tileW, hx, hy, v0, v1);
            }
        }
        break;
    }
    default:
        return 0; // 非异形方块 / 未实现 → 不追加（chunkgeometry 的 continue 跳过此格）
    }
    return int(verts.size()) - startVerts;
}
