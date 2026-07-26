#include "chunkgeometry.h"
#include "blockregistry.h"
#include "world.h"

#include <QByteArray>
#include <QVector3D>

#include <cmath>
#include <cstddef>
#include <cstring>

// 顶点：pos(3) + normal(3) + uv(2) = 8 float = 32 字节。
struct Vtx {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

// 6 个面：邻居偏移 dir、外法线 nrm、4 角偏移（从外侧看逆时针，叉积验证 = 外法线）。
// 三角形按 (0,1,2),(0,2,3) 画。UV 按角点位置单独计算（保证侧面「上=草」）。
struct FaceDef {
    int dir[3];
    float nrm[3];
    float c[4][3];
};
static const FaceDef kFaces[6] = {
    /*+X*/ {{ 1, 0, 0}, { 1, 0, 0}, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
    /*-X*/ {{-1, 0, 0}, {-1, 0, 0}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}},
    /*+Y*/ {{0,  1, 0}, {0,  1, 0}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},
    /*-Y*/ {{0, -1, 0}, {0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    /*+Z*/ {{0, 0,  1}, {0, 0,  1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    /*-Z*/ {{0, 0, -1}, {0, 0, -1}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},
};

ChunkGeometry::ChunkGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent) {}

void ChunkGeometry::setWorld(World *w)
{
    if (m_world == w) return;
    if (m_world) disconnect(m_world, &World::worldChanged, this, &ChunkGeometry::buildMesh);
    m_world = w;
    if (m_world) connect(m_world, &World::worldChanged, this, &ChunkGeometry::buildMesh);
    emit worldChanged();
    buildMesh();
}

// 查表（单一权威：BlockRegistry）。行为与历史硬编码一致：草顶/草侧/草底、
// 其余各面统一；新增 cobble/log/planks/leaves 的瓦片 5..9 暂不在图集中（t12 重建）。
int ChunkGeometry::tileFor(quint8 block, int face) const
{
    // face: 0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z（须与 BlockRegistry::Face 一致）
    return BlockRegistry::tileIndex(block, BlockRegistry::Face(face));
}

void ChunkGeometry::buildMesh()
{
    const int W = m_world ? m_world->width() : 0;
    const int H = m_world ? m_world->height() : 0;
    const int D = m_world ? m_world->depth() : 0;

    QVector<Vtx> verts;
    QVector<quint32> idx;
    if (m_world) {
        verts.reserve(4096);
        idx.reserve(8192);
    }

    // 图集瓦片横排：当前 5 瓦片（80×16）。BlockRegistry 已为 8 方块定义 0..9 序号，
    // 但新增 4 方块的瓦片（5..9）要等 t12 重建图集后才打包进来——届时 N 提到 10。
    // 半纹素内缩防溢出。
    constexpr int N = 5;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * 16);
    constexpr float hy = 0.5f / 16;
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    if (m_world) {
        for (int y = 0; y < H; ++y) {
            for (int z = 0; z < D; ++z) {
                for (int x = 0; x < W; ++x) {
                    const quint8 b = blockAt(x, y, z);
                    if (b == 0) continue; // 空气
                    for (int f = 0; f < 6; ++f) {
                        const FaceDef &F = kFaces[f];
                        if (blockAt(x + F.dir[0], y + F.dir[1], z + F.dir[2]) != 0)
                            continue; // 邻居实体 → 剔除

                        const int t = tileFor(b, f);
                        const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;

                        const quint32 base = quint32(verts.size());
                        for (int c = 0; c < 4; ++c) {
                            const float dx = F.c[c][0], dy = F.c[c][1], dz = F.c[c][2];
                            float cu, cv;
                            if (f == 0 || f == 1) { cu = dz; cv = dy; }       // ±X
                            else if (f == 4 || f == 5) { cu = dx; cv = dy; }  // ±Z
                            else { cu = dx; cv = dz; }                        // ±Y
                            Vtx v;
                            v.x = float(x) + dx; v.y = float(y) + dy; v.z = float(z) + dz;
                            v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                            v.u = u0 + cu * (u1 - u0);
                            v.v = v0 + cv * (v1 - v0);
                            verts.append(v);
                        }
                        idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
                        idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
                    }
                }
            }
        }
    }

    // 写入 QQuick3DGeometry（文档顺序：clear → 数据 → stride → bounds → 原语 → 属性）
    clear();

    QByteArray vb;
    vb.resize(int(verts.size() * sizeof(Vtx)));
    if (!vb.isEmpty())
        std::memcpy(vb.data(), verts.constData(), size_t(vb.size()));
    setVertexData(vb);
    setStride(int(sizeof(Vtx))); // 32

    QByteArray ib;
    ib.resize(int(idx.size() * sizeof(quint32)));
    if (!ib.isEmpty())
        std::memcpy(ib.data(), idx.constData(), size_t(ib.size()));
    setIndexData(ib);

    setBounds(QVector3D(0, 0, 0), QVector3D(W, H, D));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);

    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(Vtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic,
                 int(offsetof(Vtx, nx)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(Vtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);

    update(); // 通知后端重新上传到 GPU
}
