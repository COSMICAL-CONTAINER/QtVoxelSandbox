#include "skydome.h"

#include <QByteArray>
#include <QVector3D>
#include <cmath>
#include <vector>

// UV 球（半径 1）生成：rings 条纬线 × segments 条经线。
//   顶点 = (rings+1) × (segments+1)；三角连接相邻两环。
//   theta=i·π/rings（0=南极 -Y → π=北极 +Y）；phi=j·2π/segments（绕 Y 经度）。
//   位置 = (sinθ·cosφ, -cosθ, sinθ·sinφ)；UV = (j/segments, i/rings)。
//
// 内表面正面（关键）：相机在球心，须令内表面（朝心）= 正面才能经默认 backface 剔除可见。
//   做法 = 把每对相邻环的三角按「从球内看 CCW」的顺序生成（外法线指向球心）。
//   具体：环 i（下）与 i+1（上），对每个经度段 j 生成两三角，顶点顺序取 (下左, 下右, 上右) 与
//   (下左, 上右, 上左)——从球外看这是 CW（背面），从球内看是 CCW（正面）→ 内表面显。
//
// 顶点布局：pos(3) + uv(2) = 5 float = 20 字节（与 BillboardQuad 同布局，便于材质复用）。
namespace {
struct DomeVtx {
    float x, y, z;
    float u, v;
};
} // namespace

SkyDome::SkyDome(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    constexpr int rings = 16;       // 纬线数（分辨率：星点纹理 256px，16 环足够无肉眼可见多边形化）
    constexpr int segments = 24;    // 经线数（绕一圈 24 段；星点对纬度分辨率更敏感，经线可少）

    std::vector<DomeVtx> verts;
    verts.reserve((rings + 1) * (segments + 1));
    for (int i = 0; i <= rings; ++i) {
        const float theta = float(M_PI) * float(i) / float(rings);   // 0..π
        const float st = std::sin(theta);
        const float ct = std::cos(theta);
        const float y = -ct;                                          // 南极 i=0 → y=-1；北极 i=rings → y=+1
        const float v = float(i) / float(rings);                      // UV.v 0..1（南→北）
        for (int j = 0; j <= segments; ++j) {
            const float phi = 2.f * float(M_PI) * float(j) / float(segments);
            const float u = float(j) / float(segments);              // UV.u 0..1（绕一圈，j=segments=0 重复以保证接缝无缝）
            DomeVtx vtx;
            vtx.x = st * std::cos(phi);
            vtx.y = y;
            vtx.z = st * std::sin(phi);
            vtx.u = u;
            vtx.v = v;
            verts.push_back(vtx);
        }
    }

    // 索引：内表面正面顺序。每格 (i,j)：两三角覆盖环 i..i+1 / 段 j..j+1。
    // 顶点行优先索引 = i*(segments+1)+j。
    std::vector<quint32> idx;
    idx.reserve(rings * segments * 6);
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segments; ++j) {
            const quint32 bl = quint32(i * (segments + 1) + j);       // 下环、段 j（左）
            const quint32 br = bl + 1;                                // 下环、段 j+1（右）
            const quint32 tl = quint32((i + 1) * (segments + 1) + j); // 上环、段 j（左）
            const quint32 tr = tl + 1;                                // 上环、段 j+1（右）
            // 从球内看 CCW：bl → br → tr 与 bl → tr → tl（外法线指向球心 → 内表面显）。
            idx.push_back(bl); idx.push_back(br); idx.push_back(tr);
            idx.push_back(bl); idx.push_back(tr); idx.push_back(tl);
        }
    }

    // 写入顺序（lessons-learned）：clear → setVertexData → setIndexData → setStride
    // → setBounds → setPrimitiveType → addAttribute → update。
    clear();
    setVertexData(QByteArray(reinterpret_cast<const char *>(verts.data()),
                             int(verts.size() * sizeof(DomeVtx))));
    setIndexData(QByteArray(reinterpret_cast<const char *>(idx.data()),
                            int(idx.size() * sizeof(quint32))));
    setStride(int(sizeof(DomeVtx)));
    setBounds(QVector3D(-1.f, -1.f, -1.f), QVector3D(1.f, 1.f, 1.f));   // 局部 AABB（单位球 ±1）
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(DomeVtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(DomeVtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
}
