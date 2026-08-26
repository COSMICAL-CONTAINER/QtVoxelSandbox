#include "itemshapegeometry.h"

#include <QByteArray>
#include <QVector3D>

#include <algorithm> // std::min/max
#include <vector>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节（同 MobModel；无顶点色——昼夜明暗走 QML baseColor × terrainLight）。
namespace {
struct ShapeVtx {
    float x, y, z;
    float u, v;
};

// 6 面角点模板（±0.5 单位盒；CCW 朝外，同 BlockCube kFaceCorners——含面内归一化 (cu,cv) 供 UV 插值）。
// 本类把模板按子盒 extent 缩放：pos = 盒心 + fc.axis · 盒半长（fc 的 ±0.5 × 半长）。
constexpr float kH = 0.5f;
struct FaceCorner {
    float x, y, z;
    float cu, cv;
};
const FaceCorner kFaceCorners[6][4] = {
    // +X（面内 z,y）
    {{ kH, -kH, -kH, 0, 0}, { kH,  kH, -kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH, -kH,  kH, 1, 0}},
    // -X（面内 z,y；u 反向）
    {{-kH, -kH,  kH, 1, 0}, {-kH,  kH,  kH, 1, 1}, {-kH,  kH, -kH, 0, 1}, {-kH, -kH, -kH, 0, 0}},
    // +Y（顶，面内 x,z）
    {{-kH,  kH,  kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH,  kH, -kH, 1, 0}, {-kH,  kH, -kH, 0, 0}},
    // -Y（底，面内 x,z）
    {{-kH, -kH, -kH, 0, 0}, { kH, -kH, -kH, 1, 0}, { kH, -kH,  kH, 1, 1}, {-kH, -kH,  kH, 0, 1}},
    // +Z（面内 x,y）
    {{-kH, -kH,  kH, 0, 0}, { kH, -kH,  kH, 1, 0}, { kH,  kH,  kH, 1, 1}, {-kH,  kH,  kH, 0, 1}},
    // -Z（面内 x,y；u 反向）
    {{ kH, -kH, -kH, 1, 0}, {-kH, -kH, -kH, 0, 0}, {-kH,  kH, -kH, 0, 1}, { kH,  kH, -kH, 1, 1}},
};

// 图集常量（同 BlockCube：AtlasTileCount / kAtlasTilePx 单一权威 + 半纹素内缩防渗色）。
constexpr int kAtlasN = BlockRegistry::AtlasTileCount;
constexpr float kTileW = 1.0f / kAtlasN;
constexpr float kHx = 0.5f / (kAtlasN * BlockRegistry::kAtlasTilePx);
constexpr float kHy = 0.5f / BlockRegistry::kAtlasTilePx;

// 追加一个轴对齐子盒（cell-local [0,1]³ 区间，**未居中**——yShift 把盒平移到形心居中坐标系）。
//   topTile/bottomTile/sideTile = 三段图集瓦片。可选 UV 窗口（wu0..wu1 / wv0..wv1 ∈ [0,1]，瓦片内子区
//   ——火把细柱采 torch 瓦片中央列带用；默认满窗）。bounds 累计实际顶点范围。
void addShapeBox(std::vector<ShapeVtx> &verts, std::vector<quint32> &idx,
                 float x0, float y0, float z0, float x1, float y1, float z1,
                 int topTile, int bottomTile, int sideTile, float yShift,
                 QVector3D &bMin, QVector3D &bMax,
                 float wu0 = 0.0f, float wu1 = 1.0f, float wv0 = 0.0f, float wv1 = 1.0f)
{
    // cell-local [0,1]³ → 原点居中系：X/Z 平移 −0.5（满格 footprint 即 ±0.5；BlockCube 同基准），Y 用
    //   yShift 形心居中（各形状自身高度中点，图标 y_mid 同口径）。
    const float mx = (x0 + x1) * 0.5f - 0.5f, my = (y0 + y1) * 0.5f + yShift, mz = (z0 + z1) * 0.5f - 0.5f;
    // 模板角点 ∈ ±0.5 → 乘盒**全长**（extent）落到位（乘半长会把盒缩成一半——t880 探针 bounds 红的根因）。
    const float ex = x1 - x0, ey = y1 - y0, ez = z1 - z0;
    const quint32 base = quint32(verts.size());
    for (int f = 0; f < 6; ++f) {
        const int tile = (f == 2) ? topTile : (f == 3) ? bottomTile : sideTile;
        const float tu0 = float(tile) * kTileW + kHx;
        const float tu1 = float(tile + 1) * kTileW - kHx;
        const float u0 = tu0 + wu0 * (tu1 - tu0);
        const float u1 = tu0 + wu1 * (tu1 - tu0);
        const float v0 = kHy + wv0 * (1.0f - 2.0f * kHy);
        const float v1 = kHy + wv1 * (1.0f - 2.0f * kHy);
        const quint32 b = base + quint32(f * 4);
        for (int c = 0; c < 4; ++c) {
            const FaceCorner &fc = kFaceCorners[f][c];
            ShapeVtx v;
            v.x = mx + fc.x * ex;
            v.y = my + fc.y * ey;
            v.z = mz + fc.z * ez;
            v.u = u0 + fc.cu * (u1 - u0);
            v.v = v0 + fc.cv * (v1 - v0);
            verts.push_back(v);
            bMin.setX(std::min(bMin.x(), v.x)); bMax.setX(std::max(bMax.x(), v.x));
            bMin.setY(std::min(bMin.y(), v.y)); bMax.setY(std::max(bMax.y(), v.y));
            bMin.setZ(std::min(bMin.z(), v.z)); bMax.setZ(std::max(bMax.z(), v.z));
        }
        idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
        idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
    }
}

// 追加一片双面 quad（cross 族：对角平面 + 反向绕序副本 → 双面可见，机制等价 World pushCrossQuad
//   的双面发）。四角 cell-local（(ax,az)-(bx,bz) 对角 + 高度 h0..h1），yShift 同上；瓦片整张贴面。
void addCrossQuad(std::vector<ShapeVtx> &verts, std::vector<quint32> &idx,
                  float ax, float az, float bx, float bz, float h0, float h1,
                  int tile, float yShift, QVector3D &bMin, QVector3D &bMax)
{
    const float u0 = float(tile) * kTileW + kHx;
    const float u1 = float(tile + 1) * kTileW - kHx;
    const float v0 = kHy, v1 = 1.0f - kHy;
    const float y0 = h0 + yShift, y1 = h1 + yShift;
    // cell-local [0,1] → 原点居中系（X/Z −0.5，同 addShapeBox）。
    const ShapeVtx quad[4] = {
        { ax - 0.5f, y0, az - 0.5f, u0, v0 }, { bx - 0.5f, y0, bz - 0.5f, u1, v0 },
        { bx - 0.5f, y1, bz - 0.5f, u1, v1 }, { ax - 0.5f, y1, az - 0.5f, u0, v1 },
    };
    for (int pass = 0; pass < 2; ++pass) { // pass 0 正面 / pass 1 反向绕序（双面）
        const quint32 b = quint32(verts.size());
        for (int c = 0; c < 4; ++c) {
            const ShapeVtx &v = quad[c];
            verts.push_back(v);
            bMin.setX(std::min(bMin.x(), v.x)); bMax.setX(std::max(bMax.x(), v.x));
            bMin.setY(std::min(bMin.y(), v.y)); bMax.setY(std::max(bMax.y(), v.y));
            bMin.setZ(std::min(bMin.z(), v.z)); bMax.setZ(std::max(bMax.z(), v.z));
        }
        if (pass == 0) {
            idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
            idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
        } else {
            idx.push_back(b + 2); idx.push_back(b + 1); idx.push_back(b + 0);
            idx.push_back(b + 3); idx.push_back(b + 2); idx.push_back(b + 0);
        }
    }
}
} // namespace

ItemShapeGeometry::ItemShapeGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 WoodSlab 建；QML 设 blockId 时再 rebuild
}

void ItemShapeGeometry::setBlockId(int id)
{
    if (id <= 0 || id >= int(BlockRegistry::Count)) id = int(BlockRegistry::WoodSlab); // 兜底合法方块
    if (id == m_blockId) return;
    m_blockId = id;
    emit blockIdChanged();
    rebuild();
}

// 按 def.shape 泛化 + 特型覆盖（火把 / 附魔台）建几何。各形状盒区与 World partialblockgeometry
//   同源；yShift = -(y0+y1)/2 形心居中（图标 y_mid 同口径）。
void ItemShapeGeometry::rebuild()
{
    std::vector<ShapeVtx> verts;
    std::vector<quint32> idx;
    QVector3D bMin(1e9f, 1e9f, 1e9f), bMax(-1e9f, -1e9f, -1e9f);
    const BlockRegistry::BlockDef &d = BlockRegistry::def(quint8(m_blockId));
    const int topT = d.topTile, botT = d.bottomTile, sideT = d.sideTile;
    // 活板门族薄侧边 per-family 分流（铁=iron_block / 木=planks；World mesher t742/t879 同一语言）。
    const int trapSide = (m_blockId == int(BlockRegistry::IronTrapdoor))
        ? BlockRegistry::tileIndex(BlockRegistry::IronBlock, BlockRegistry::PosX)
        : BlockRegistry::tileIndex(BlockRegistry::Planks, BlockRegistry::PosX);

    if (m_blockId == int(BlockRegistry::Torch)) {
        // 火把细立柱：2/16 见方 × 10/16 高（世界内火把模型量级）。贴图 torch 瓦片**中央列带**子窗
        //   （u [7/16,9/16] 火把本体柱、v 满高含焰头）侧/顶/底同窗（火把 def 各面 = torch(17)；整瓦片
        //   直铺会把 16px 宽透明底压进 2/16 窄面成碎条——子窗只采火把像素）。形心居中 yShift=-5/16。
        constexpr float kT0 = 7.0f / 16.0f, kT1 = 9.0f / 16.0f, kHh = 10.0f / 16.0f;
        constexpr float kWu0 = 7.0f / 16.0f, kWu1 = 9.0f / 16.0f;
        addShapeBox(verts, idx, kT0, 0.0f, kT0, kT1, kHh, kT1,
                    topT, botT, sideT, -kHh * 0.5f, bMin, bMax, kWu0, kWu1, 0.0f, 1.0f);
    } else if (m_blockId == int(BlockRegistry::EnchantingTable)) {
        // 附魔台 0.75 矮盒（def shape=ShapeFull 但世界内走 mesher 显式 0.75 盒——本类同款；侧瓦片图集
        //   已裁顶 0.25 空白 → 整张贴 0.75 高侧面）。形心居中 -0.375。台顶悬浮书由 QML 叠 EnchantBookBox。
        addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 0.75f, 1.0f,
                    topT, botT, sideT, -0.375f, bMin, bMax);
    } else {
        switch (d.shape) {
        case BlockRegistry::ShapeTrapdoor:
            // 合态薄板：全 footprint y[0,3/16]（同 mesher 合态 + 图标 trapdoor 盒）；形心居中 -3/32。
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 3.0f / 16.0f, 1.0f,
                        topT, botT, trapSide, -3.0f / 32.0f, bMin, bMax);
            break;
        case BlockRegistry::ShapeSlab:
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f,
                        topT, botT, sideT, -0.25f, bMin, bMax);
            break;
        case BlockRegistry::ShapeStairs:
            // 整步（低，全 footprint）+ 背墙（远端半高上半）——同 World mesher / 图标 stairs 盒。
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f,
                        topT, botT, sideT, -0.5f, bMin, bMax);
            addShapeBox(verts, idx, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f,
                        topT, botT, sideT, -0.5f, bMin, bMax);
            break;
        case BlockRegistry::ShapeSnowLayer:
            // state 0 基底观感：1/8 高薄板（同图标 snow_layer 盒）。
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f / 8.0f, 1.0f,
                        topT, botT, sideT, -1.0f / 16.0f, bMin, bMax);
            break;
        default:
            if (BlockRegistry::isCrossBillboard(quint8(m_blockId))) {
                // cross 族（草丛等）：两片对角交叉双面 quad（同 World pushCrossQuad X 形）。
                addCrossQuad(verts, idx, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, topT, -0.5f, bMin, bMax);
                addCrossQuad(verts, idx, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, topT, -0.5f, bMin, bMax);
                break;
            }
            // 兜底：满立方（ShapeFull / 未知形；调用方本不该路由到此，防御性非空几何）。
            addShapeBox(verts, idx, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                        topT, botT, sideT, -0.5f, bMin, bMax);
            break;
        }
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride → setBounds
    //   → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()), int(verts.size() * sizeof(ShapeVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()), int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(ShapeVtx)));
    setBounds(bMin, bMax); // 实际形状 AABB（形心居中坐标系；Model 变换给视锥剔除盒）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(ShapeVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(ShapeVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
