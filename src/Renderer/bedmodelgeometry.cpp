#include "bedmodelgeometry.h"

#include <QByteArray>
#include <QVector3D>

#include "partialblockgeometry.h" // BedHalfBox / bedHalfBoxes —— 床盒布局单一权威（World 层；Renderer→World 向下依赖合规，同 blockcube.cpp include world.h）

// t784 床 3D 预览几何：bedHalfBoxes 双半盒列表 → 顶点缓冲。顶点 pos(3)+uv(2)=5 float（同 MobModel；
//   材质 NoLighting 无顶点色需求）。每盒 6 面 × 4 角，角序 CCW 朝外（默认 backface 剔除下外表面可见）。
namespace {

struct BedVtx {
    float x, y, z;
    float u, v;
};

// 6 面角点模板（±0.5 居中 + per-corner (cu,cv)∈{0,1}）——镜像 blockcube.cpp kFaceCorners（其又是
//   chunkgeometry kFaces 的 ±0.5 重心化；本表再经「模板角 lerp 到子盒范围」推广到任意轴对齐子盒）。
//   UV 映射 ±X 面 (cu,cv)=(z,y)、±Z 面 (x,y)、±Y 面 (x,z)，cu,cv 恒单位值 → 整张瓦片贴图铺满该面
//   （同 partialblockgeometry pushBox「cu,cv 恒 {0,1}」约定，薄面贴图压缩与游戏内一致）。
constexpr float kH = 0.5f;
struct FaceCorner {
    float x, y, z;
    float cu, cv;
};
const FaceCorner kFaceCorners[6][4] = {
    // +X（常数轴 x=+h；面内 z,y）
    {{ kH, -kH, -kH, 0, 0}, { kH,  kH, -kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH, -kH,  kH, 1, 0}},
    // -X（常数轴 x=-h；面内 z,y）
    {{-kH, -kH,  kH, 1, 0}, {-kH,  kH,  kH, 1, 1}, {-kH,  kH, -kH, 0, 1}, {-kH, -kH, -kH, 0, 0}},
    // +Y（顶；面内 x,z）
    {{-kH,  kH,  kH, 0, 1}, { kH,  kH,  kH, 1, 1}, { kH,  kH, -kH, 1, 0}, {-kH,  kH, -kH, 0, 0}},
    // -Y（底；面内 x,z）
    {{-kH, -kH, -kH, 0, 0}, { kH, -kH, -kH, 1, 0}, { kH, -kH,  kH, 1, 1}, {-kH, -kH,  kH, 0, 1}},
    // +Z（面内 x,y）
    {{-kH, -kH,  kH, 0, 0}, { kH, -kH,  kH, 1, 0}, { kH,  kH,  kH, 1, 1}, {-kH,  kH,  kH, 0, 1}},
    // -Z（面内 x,y）
    {{ kH, -kH, -kH, 1, 0}, {-kH, -kH, -kH, 0, 0}, {-kH,  kH, -kH, 0, 1}, { kH,  kH, -kH, 1, 1}},
};

// 图集 UV 常量：读 BlockRegistry::AtlasTileCount / kAtlasTilePx 单一权威（同 blockcube.cpp——半纹素内缩
//   防渗色，与 chunkgeometry / BlockCube 三方同口径；独立持魔数必随图集扩容漂移，lessons t54/t148/t182）。
constexpr int kAtlasN = BlockRegistry::AtlasTileCount;
constexpr float kTileW = 1.0f / kAtlasN;
constexpr float kHx = 0.5f / (kAtlasN * BlockRegistry::kAtlasTilePx);
constexpr float kHy = 0.5f / BlockRegistry::kAtlasTilePx;

} // namespace

BedModelGeometry::BedModelGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    rebuild(); // 构造期用默认 BedRed 建几何；QML 设 blockId 时再 rebuild 到正确床色
}

void BedModelGeometry::setBlockId(int id)
{
    // 非床 id（越界 / 非床段）兜底红床（配方产物默认色）——保几何非空、被面瓦片合法。
    if (id < 0 || id >= int(BlockRegistry::Count) || !BlockRegistry::isBed(quint8(id)))
        id = int(BlockRegistry::BedRed);
    if (id == m_blockId) return;
    m_blockId = id;
    emit blockIdChanged();
    rebuild();
}

// 按 m_blockId 拼完整双格床并整几何重传。盒布局 = bedHalfBoxes 单一权威（游戏内床 case 同源）：
//   facing 固定 0（head→foot = +X）→ 床尾半（foot）占本格 cell-local [0,1]、床头半（head）在 foot 的
//   -front 邻格（x-1）——与 playercontroller 放置形态一致（bedPartnerOffset 编码，机制等价 MC 床
//   head+foot 双格横置）。双半盒平移到「床整体中心 = 原点」：x 双格 [-1,1] 天然对称；y 减床高半
//   （kBedHeadboardTop = 最高点床头板顶）；z 减 0.5（cell-local [0,1] → [-0.5,0.5]）。
void BedModelGeometry::rebuild()
{
    QVector<BedHalfBox> boxes;
    PartialBlockGeometry::bedHalfBoxes(quint8(m_blockId), /*isHead=*/false, /*facing=*/0, boxes); // 床尾半（本格）
    const int footCount = boxes.size();
    PartialBlockGeometry::bedHalfBoxes(quint8(m_blockId), /*isHead=*/true, /*facing=*/0, boxes);  // 床头半（-front 邻格）
    const float yHalf = BlockRegistry::kBedHeadboardTop * 0.5f; // 床高半（最高点 = 床头板顶 9/16）
    const float v0 = 0.0f + kHy, v1 = 1.0f - kHy;

    QVector<BedVtx> verts;
    QVector<quint32> idx;
    verts.reserve(boxes.size() * 24);
    idx.reserve(boxes.size() * 36);
    for (int h = 0; h < boxes.size(); ++h) {
        const BedHalfBox &b = boxes[h];
        // 床头半沿 -front（facing 0 → -X）平移一格；y/z 平移到床整体居中坐标系。
        const float ox = (h < footCount) ? 0.0f : -1.0f;
        const float x0 = b.x0 + ox, x1 = b.x1 + ox;
        const float y0 = b.y0 - yHalf, y1 = b.y1 - yHalf;
        const float z0 = b.z0 - kH, z1 = b.z1 - kH;
        const float u0 = float(b.tile) * kTileW + kHx;
        const float u1 = float(b.tile + 1) * kTileW - kHx;
        for (int f = 0; f < 6; ++f) {
            const quint32 base = quint32(verts.size());
            for (int c = 0; c < 4; ++c) {
                const FaceCorner &fc = kFaceCorners[f][c];
                BedVtx v;
                // 模板角 [-0.5,0.5] 线性映射到子盒范围 [min,max]（顺序保持 → CCW 绕序不变）。
                v.x = x0 + (fc.x + kH) * (x1 - x0);
                v.y = y0 + (fc.y + kH) * (y1 - y0);
                v.z = z0 + (fc.z + kH) * (z1 - z0);
                v.u = u0 + fc.cu * (u1 - u0);
                v.v = v0 + fc.cv * (v1 - v0);
                verts.append(v);
            }
            idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
            idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
        }
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride → setBounds →
    //   setPrimitiveType(Triangles) → addAttribute(...) → update()。IndexSemantic 与 setIndexData 成对
    //   （缺后者索引永不进 GPU，lessons t35）；TexCoord0Semantic 必在（缺则贴图采样未定义白块，lessons t757）。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.constData()),
                             int(verts.size() * sizeof(BedVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.constData()),
                            int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(BedVtx))); // 20（pos3 + uv2）
    setBounds(QVector3D(-1.0f, -yHalf, -kH), QVector3D(1.0f, yHalf, kH)); // 局部 AABB = 双格床整体（视锥剔除用）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(BedVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(BedVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
