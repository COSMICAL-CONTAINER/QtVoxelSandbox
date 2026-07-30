#include "blockcube.h"

#include <QByteArray>
#include <QVector3D>

// 顶点：pos(3) + uv(2) = 5 float = 20 字节。每面 4 角独立顶点（24 总），便于 per-face UV。
namespace {
struct CubeVtx {
    float x, y, z;
    float u, v;
};

// 6 面角点（与 chunkgeometry.cpp kFaces 同序：+X -X +Y -Y +Z -Z）。
// 每面 4 角从外侧看 CCW（叉积 = 外法线，默认 backface 剔除下可见）；位置严格 ±0.5 居中
// （与 UnitCube / CrackBox 同基准，Model 摆位用「中心 = pos」自洽）。
// (cu, cv) = 面内两轴的归一化坐标（0 或 1），用于把瓦片 [u0,u1]×[v0,v1] 子区铺到该面 ——
// 与 chunkgeometry 的 UV 映射完全一致：±X 面 (cu,cv)=(z,y)、±Z 面 (x,y)、±Y 面 (x,z)。
// 推导自 chunkgeometry kFaces 各角 (dx,dy,dz) + 其 cu/cv 公式，recenter 到 ±0.5。
constexpr float kH = 0.5f;
struct FaceCorner {
    float x, y, z;
    float cu, cv;
};
const FaceCorner kFaceCorners[6][4] = {
    // +X（常数轴 x=+h；面内 z,y）—— chunkgeometry +X 角 (1,0,0)(1,1,0)(1,1,1)(1,0,1)
    {{ kH, -kH, -kH, 0, 0}, { kH,  kH, -kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH, -kH,  kH, 1, 0}},
    // -X（常数轴 x=-h；面内 z,y）—— chunkgeometry -X 角 (0,0,1)(0,1,1)(0,1,0)(0,0,0)
    {{-kH, -kH,  kH, 1, 0}, {-kH,  kH,  kH, 1, 1}, {-kH,  kH, -kH, 0, 1}, {-kH, -kH, -kH, 0, 0}},
    // +Y（顶，常数轴 y=+h；面内 x,z）—— chunkgeometry +Y 角 (0,1,1)(1,1,1)(1,1,0)(0,1,0)
    {{-kH,  kH,  kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH,  kH, -kH, 1, 0}, {-kH,  kH, -kH, 0, 0}},
    // -Y（底，常数轴 y=-h；面内 x,z）—— chunkgeometry -Y 角 (0,0,0)(1,0,0)(1,0,1)(0,0,1)
    {{-kH, -kH, -kH, 0, 0}, { kH, -kH, -kH, 1, 0}, { kH, -kH,  kH, 1, 1}, {-kH, -kH,  kH, 0, 1}},
    // +Z（常数轴 z=+h；面内 x,y）—— chunkgeometry +Z 角 (0,0,1)(1,0,1)(1,1,1)(0,1,1)
    {{-kH, -kH,  kH, 0, 0}, { kH, -kH,  kH, 1, 0}, { kH,  kH,  kH, 1, 1}, {-kH,  kH,  kH, 0, 1}},
    // -Z（常数轴 z=-h；面内 x,y）—— chunkgeometry -Z 角 (1,0,0)(0,0,0)(0,1,0)(1,1,0)
    {{ kH, -kH, -kH, 1, 0}, {-kH, -kH, -kH, 0, 0}, {-kH,  kH, -kH, 0, 1}, { kH,  kH, -kH, 1, 1}},
};

// 图集瓦片数：**必须**与 src/World/chunkgeometry.cpp 的 N、tools/build_atlas.py 的 TILES
// 长度三处严格一致（一个偏差即贴图错位 / 渗色）。当前 19 瓦片横排（t119 加 bedrock）：
//   0 grass_top / 1 grass_side / 2 dirt / 3 stone / 4 sand
//   5 cobble / 6 log_top / 7 log_side / 8 planks / 9 leaves
//   10 crafting_table_top / 11 crafting_table_side
//   12 furnace_top / 13 furnace_side / 14 furnace_front
//   15 coal_ore / 16 iron_ore / 17 torch / 18 bedrock
// 瓦片序号由 BlockRegistry::tileIndex(blockId, face) 给出（单一权威）。
// 历史 bug（t54）：本处曾停在 10，而 chunkgeometry 已升到 12 → BlockCube 的 u 区间按 1/10 算
// 偏宽偏右，泥土(2)采到半块石头、树叶(9)采到木板；与 chunkgeometry 对齐到 12 后修复。
constexpr int kAtlasN = 19;
constexpr float kTileW = 1.0f / kAtlasN;
constexpr float kHx = 0.5f / (kAtlasN * 16); // 半纹素内缩（线性采样防跨瓦片渗色）
constexpr float kHy = 0.5f / 16;
} // namespace

BlockCube::BlockCube(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 Stone 建 UV；QML 设 blockId 时再 rebuild 到正确方块
}

void BlockCube::setBlockId(int id)
{
    if (id <= 0 || id >= int(BlockRegistry::Count)) id = int(BlockRegistry::Stone); // 兜底合法方块
    if (id == m_blockId) return;
    m_blockId = id;
    emit blockIdChanged();
    rebuild();
}

// 按 m_blockId 重算每面 UV（顶点位置恒定）。每面查图集瓦片序号 → 该瓦片 [u0,u1]×[v0,v1]
// 子区，4 角按 cu/cv 插值铺面。BlockRegistry::Face 序与本类面序一致（0=+X…5=-Z）。
void BlockCube::rebuild()
{
    CubeVtx verts[24];
    const float v0 = 0.0f + kHy, v1 = 1.0f - kHy;
    for (int f = 0; f < 6; ++f) {
        const int tile = BlockRegistry::tileIndex(quint8(m_blockId), BlockRegistry::Face(f));
        const float u0 = float(tile) * kTileW + kHx;
        const float u1 = float(tile + 1) * kTileW - kHx;
        for (int c = 0; c < 4; ++c) {
            const FaceCorner &fc = kFaceCorners[f][c];
            CubeVtx &v = verts[f * 4 + c];
            v.x = fc.x; v.y = fc.y; v.z = fc.z;
            v.u = u0 + fc.cu * (u1 - u0);
            v.v = v0 + fc.cv * (v1 - v0);
        }
    }
    // 每面 2 三角形：(0,1,2)+(0,2,3)，6 面 = 12 三角形 = 36 索引（同 CrackBox）。
    const quint32 idx[36] = {
         0, 1, 2,  0, 2, 3,
         4, 5, 6,  4, 6, 7,
         8, 9,10,  8,10,11,
        12,13,14, 12,14,15,
        16,17,18, 16,18,19,
        20,21,22, 20,22,23,
    };

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType(Triangles) → addAttribute(...) → update()。
    // 漏 update() 后端不上传 GPU；漏 setIndexData 则 idx 数组未用（-Wunused 警告）且
    // 36 索引永不进 GPU —— IndexSemantic 只声明布局，数据须 setIndexData 单独上传（同 chunkgeometry）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts), int(sizeof(verts))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx), int(sizeof(idx)))); // 36 索引独立上传
    setStride(int(sizeof(CubeVtx)));
    setBounds(QVector3D(-kH, -kH, -kH), QVector3D(kH, kH, kH)); // 局部 AABB = ±0.5（与几何一致）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(CubeVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(CubeVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
