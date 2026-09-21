#include "blobshadowgeometry.h"

#include <cmath>
#include <cstring>

// t1075 blob 软阴影贴片几何（Renderer 层）——选型/落选/分层/顶点布局契约见 .h 头注释。
// 本 .cpp 只做三件事：同值早退的指纹差分、八边形扇形顶点装配、文档序 GPU 重传。

namespace {

// 顶点：pos(3) + color(4 rgba)。ColorSemantic 按 vec4 读（blockcube.cpp t257 先例：ColorSemantic
// 以 4 float 为单元寻址，只写 3 float 会把下一顶点的 x 当 a 错位读）。
struct ShadowVertex
{
    float x, y, z;
    float r, g, b, a;
};

constexpr float kPi = 3.14159265358979f;

} // namespace

BlobShadowGeometry::BlobShadowGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
}

void BlobShadowGeometry::setQuads(const ShadowBlobQuad *quads, int count)
{
    if (count < 0)
        count = 0;

    // 指纹差分早退：全值逐位相同 → 零重装零重传零沿（消费侧节流的对偶半边——采样侧 15Hz 到拍，
    //   重传侧只在真变时付 GPU 上传与 NOTIFY 成本）。
    const bool same = count == m_quads.size()
        && (count == 0 || std::memcmp(m_quads.constData(), quads, sizeof(ShadowBlobQuad) * count) == 0);
    if (same)
        return;

    m_quads.clear();
    if (count > 0)
        m_quads = QVector<ShadowBlobQuad>(quads, quads + count);
    rebuild();
    emit quadsChanged();
}

void BlobShadowGeometry::rebuild()
{
    const int count = int(m_quads.size());

    if (count == 0) {
        // 空集（无活体投影 / 未进世界）：清缓冲按空几何重传（chunkgeometry 空段同门先例）。
        clear();
        setVertexData(QByteArray());
        setStride(int(sizeof(ShadowVertex)));
        setIndexData(QByteArray());
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
        addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                     QQuick3DGeometry::Attribute::F32Type);
        addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 12,
                     QQuick3DGeometry::Attribute::F32Type);
        addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                     QQuick3DGeometry::Attribute::U32Type);
        update();
        return;
    }

    // 顶点装配：每 quad 中心 1（暗核满 alpha）+ 八边形缘 8（alpha=0）——线性插值 = 径向软边。
    //   缘点序 = 从 +X 起逆时针（俯视 XZ 平面）；QML 材质配 CullNoCulling（绕序不承载正确性）。
    const int totalVerts = count * kVertsPerQuad;
    const int totalIndices = count * kIndicesPerQuad;

    QByteArray vb;
    vb.resize(totalVerts * int(sizeof(ShadowVertex)));
    auto *v = reinterpret_cast<ShadowVertex *>(vb.data());
    QByteArray ib;
    ib.resize(totalIndices * int(sizeof(quint32)));
    auto *idx = reinterpret_cast<quint32 *>(ib.data());

    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;

    for (int q = 0; q < count; ++q) {
        const ShadowBlobQuad &s = m_quads[q];
        const int base = q * kVertsPerQuad;

        // 中心顶点（贴片暗核；rgb=0 纯黑 + a = quad alpha）。
        v[base] = { s.x, s.y, s.z, 0.0f, 0.0f, 0.0f, s.alpha };

        // 缘顶点（透明缘圈；y 与中心同面 = 贴地水平 disc，抬升已由采样权威算进 s.y）。
        for (int k = 0; k < 8; ++k) {
            const float ang = kPi * 2.0f * float(k) / 8.0f;
            const float rx = s.x + s.radius * std::cos(ang);
            const float rz = s.z + s.radius * std::sin(ang);
            v[base + 1 + k] = { rx, s.y, rz, 0.0f, 0.0f, 0.0f, 0.0f };
        }

        // 扇形三角（中心 + 相邻缘对；24 索引/quad，U32）。
        for (int k = 0; k < 8; ++k) {
            quint32 *t = idx + q * kIndicesPerQuad + k * 3;
            t[0] = quint32(base);
            t[1] = quint32(base + 1 + k);
            t[2] = quint32(base + 1 + (k + 1) % 8);
        }

        // bounds 逐拍重算（Model 摆原点，几何自带世界坐标 bounds → 视锥剔除正确）。
        if (q == 0) {
            minX = maxX = s.x;
            minY = maxY = s.y;
            minZ = maxZ = s.z;
        }
        minX = std::min(minX, s.x - s.radius);
        maxX = std::max(maxX, s.x + s.radius);
        minY = std::min(minY, s.y - 0.1f);
        maxY = std::max(maxY, s.y + 0.1f);
        minZ = std::min(minZ, s.z - s.radius);
        maxZ = std::max(maxZ, s.z + s.radius);
    }

    // 上传序 = lessons t03/t35 文档序（clear → vertexData → stride → indexData → bounds →
    //   原语 → attributes → update；setIndexData 与 addAttribute(IndexSemantic) 成对契约）。
    clear();
    setVertexData(vb);
    setStride(int(sizeof(ShadowVertex)));
    setIndexData(ib);
    setBounds(QVector3D(minX, minY, minZ), QVector3D(maxX, maxY, maxZ));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 12,
                 QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                 QQuick3DGeometry::Attribute::U32Type);
    update();
}
