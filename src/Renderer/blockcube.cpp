#include "blockcube.h"

#include <QByteArray>
#include <QVector3D>

#include <cmath> // std::floor（t257：worldPos → 占格）

#include "voxellight.h" // t257：VoxelLight::vertexLight（光场 + PCF 软影顶点色，与 chunkgeometry 同源）
#include "world.h"      // t257：skyLightAt / blockLightAt（光场采样；Renderer→World 向下依赖合规）

// 顶点：pos(3) + uv(2) + color(4 rgba) = 9 float = 36 字节。color.rgb 承载 t257 光场 × PCF 软影顶点色
// （未接 world 时恒白 1.0），配合材质 vertexColorsEnabled:true → 最终色 = baseColor × vertexColor × 贴图
// （同 chunkgeometry）。每面 4 角独立顶点（24 总），便于 per-face UV + per-vertex 软影。
namespace {
struct CubeVtx {
    float x, y, z;
    float u, v;
    float r, g, b, a;
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

// 6 面外法线（与 kFaceCorners 同序；+X -X +Y -Y +Z -Z）。t257 光照采样用：
//   每面取其「外侧邻格」的 sky/block 光（面的可见光来自其前方），同 chunkgeometry 立方面约定。
const int kFaceNormal[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

// 图集瓦片数：读 BlockRegistry::AtlasTileCount（**单一权威**，与 chunkgeometry / build_atlas.py 同源）。
//   历史复发 bug 类（t54 / t148 / t182）：本处曾持一份独立魔数（10 / 20），加新瓦片后漏改 → 与
//   chunkgeometry 的 N 不一致 → BlockCube 的 u 区间按错误 1/kAtlasN 算偏宽偏窄，tile t 采到
//   [t/kAtlasN,(t+1)/kAtlasN] 而真实瓦片在 [t/AtlasTileCount,(t+1)/AtlasTileCount] → 泥土采到半块
//   石头、树叶采到木板（手持/掉落物贴图「杂交」= 不是实际方块）。改读单一权威常量后该类回归结构性根除。
// 瓦片序号由 BlockRegistry::tileIndex(blockId, face) 给出（单一权威）。
constexpr int kAtlasN = BlockRegistry::AtlasTileCount;
constexpr float kTileW = 1.0f / kAtlasN;
// t668 HD 图集：半纹素内缩 = 0.5px 折算归一化 UV（读 kAtlasTilePx **单一权威**；旧 16px 时 1/32 瓦片宽 →
//   64px 后 1/128，与 chunkgeometry hx/hy 同步 —— 三者任一漏改 → BlockCube 手持/掉落贴图与地形贴图渗色
//   口径不一致）。图集高 1 瓦片 → kHy = 0.5/kAtlasTilePx。
constexpr float kHx = 0.5f / (kAtlasN * BlockRegistry::kAtlasTilePx);
constexpr float kHy = 0.5f / BlockRegistry::kAtlasTilePx;
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

// t257 光照采样上下文 setter：值变 → rebuild（重烘顶点色；顶点位置 / UV 不变故仅 color 段刷新）。
//   world 设 null（item entity / 手持 / HUD）→ 顶点色退回恒白 1.0。
void BlockCube::setWorld(World *w)
{
    if (m_world == w) return;
    m_world = w;
    emit worldChanged();
    rebuild();
}

void BlockCube::setWorldPos(const QVector3D &p)
{
    if (m_worldPos == p) return;
    m_worldPos = p;
    emit worldPosChanged();
    rebuild();
}

void BlockCube::setSunDir(const QVector3D &dir)
{
    if (m_sunDir == dir) return;
    m_sunDir = dir;
    emit sunDirChanged();
    rebuild();
}

void BlockCube::setShadowsEnabled(bool on)
{
    if (m_shadowsEnabled == on) return;
    m_shadowsEnabled = on;
    emit shadowsEnabledChanged();
    rebuild();
}

// PLAN §2-H dayMul setter（昼夜天光乘子，仅乘天光分量）：值变 → rebuild 重烘顶点色（掉落沙数量少，
//   无须 chunkgeometry 那样的量化门，直接随 dayPhaseChanged 10Hz 重建掉落沙 BlockCube 即可）。
void BlockCube::setDayMul(float m)
{
    if (m < 0.0f) m = 0.0f; else if (m > 1.0f) m = 1.0f; // 钳到合法 [0,1]
    if (m_dayMul == m) return;
    m_dayMul = m;
    emit dayMulChanged();
    rebuild();
}

// 按 m_blockId 重算每面 UV（顶点位置恒定）+ 据 world/worldPos 烘顶点色。每面查图集瓦片序号 → 该瓦片
// [u0,u1]×[v0,v1] 子区，4 角按 cu/cv 插值铺面。BlockRegistry::Face 序与本类面序一致（0=+X…5=-Z）。
// t257 顶点色：每面取外侧邻格 sky/block 光 + 每顶点 PCF 软影（VoxelLight::vertexLight，与 chunkgeometry
//   立方面逐顶点同公式）→ 掉落沙与地形同亮度曲线；未接 world → 恒白 1.0。
void BlockCube::rebuild()
{
    CubeVtx verts[24];
    const float v0 = 0.0f + kHy, v1 = 1.0f - kHy;

    // t257：方块占格 = floor(worldPos)（worldPos 是方块中心 (x+0.5,y+0.5,z+0.5)）。未接 world → 跳过光照，
    //   全顶点色恒白（保既有全亮行为）。
    const bool lightOn = (m_world != nullptr);
    const int bx = int(std::floor(m_worldPos.x()));
    const int by = int(std::floor(m_worldPos.y()));
    const int bz = int(std::floor(m_worldPos.z()));

    for (int f = 0; f < 6; ++f) {
        const int tile = BlockRegistry::tileIndex(quint8(m_blockId), BlockRegistry::Face(f));
        const float u0 = float(tile) * kTileW + kHx;
        const float u1 = float(tile + 1) * kTileW - kHx;
        // t257：本面外侧邻格（面所朝方向）= 占格 + 外法线。光场基底取该邻格 sky/block（同 chunkgeometry
        //   立方面「面可见光来自其前方空气格」约定）。软影 per-vertex（每顶点各采样其世界位）→ 影边平滑。
        const int nx = bx + kFaceNormal[f][0];
        const int ny = by + kFaceNormal[f][1];
        const int nz = bz + kFaceNormal[f][2];
        for (int c = 0; c < 4; ++c) {
            const FaceCorner &fc = kFaceCorners[f][c];
            CubeVtx &v = verts[f * 4 + c];
            v.x = fc.x; v.y = fc.y; v.z = fc.z;
            v.u = u0 + fc.cu * (u1 - u0);
            v.v = v0 + fc.cv * (v1 - v0);
            if (lightOn) {
                // 顶点世界位 = 方块中心 + 本地角点（±0.5）；软影据此采样 heightmap 路径。
                // PLAN §2-H：dayMul 只乘天光分量（block 项保留时间不变）→ 掉落沙夜间不随昼夜变暗、与地形同曲线。
                const float vc = VoxelLight::vertexLight(
                    m_world, m_sunDir, m_shadowsEnabled, m_dayMul, nx, ny, nz,
                    m_worldPos.x() + fc.x, m_worldPos.y() + fc.y, m_worldPos.z() + fc.z);
                v.r = vc; v.g = vc; v.b = vc;
            } else {
                v.r = 1.0f; v.g = 1.0f; v.b = 1.0f; // 未接 world → 恒白（item entity / 手持 / HUD 既有全亮）
            }
            v.a = 1.0f;
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
    setStride(int(sizeof(CubeVtx))); // 36（pos3 + uv2 + color4 rgba）
    setBounds(QVector3D(-kH, -kH, -kH), QVector3D(kH, kH, kH)); // 局部 AABB = ±0.5（与几何一致）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(CubeVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(CubeVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    // t257 顶点色（vec4 rgba）：vertexColorsEnabled=true 时最终色 = baseColor × vertexColor × 贴图
    // （同 chunkgeometry ColorSemantic）。color 用 4 float（a=1.0）—— ColorSemantic 按 vec4 读，写 3 float
    // 会越界读到下一顶点（见 partialblockgeometry.h Vtx 注释）。
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic,
                 int(offsetof(CubeVtx, r)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
