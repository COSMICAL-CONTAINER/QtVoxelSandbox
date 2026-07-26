#include "chunkgeometry.h"

#include <QByteArray>
#include <QVector3D>

#include <algorithm>
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

ChunkGeometry::ChunkGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent)
{
    regenerate();
}

void ChunkGeometry::setWidth(int w) { if (w == m_width) return; m_width = w; emit widthChanged(); regenerate(); }
void ChunkGeometry::setDepth(int d) { if (d == m_depth) return; m_depth = d; emit depthChanged(); regenerate(); }
void ChunkGeometry::setHeight(int h) { if (h == m_height) return; m_height = h; emit heightChanged(); regenerate(); }
void ChunkGeometry::setSeed(int s) { if (s == m_seed) return; m_seed = s; emit seedChanged(); regenerate(); }

// --- 改进版 Perlin（2D）---
static double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
static double lerp(double a, double b, double t) { return a + t * (b - a); }
static double grad2(int hash, double x, double z)
{
    int h = hash & 7;
    double u = h < 4 ? x : z;
    double v = h < 4 ? z : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
}

double ChunkGeometry::noise2(double x, double z) const
{
    const int X = int(std::floor(x)) & 255;
    const int Z = int(std::floor(z)) & 255;
    x -= std::floor(x);
    z -= std::floor(z);
    const double u = fade(x), v = fade(z);
    const int A = m_perm[X] + Z, B = m_perm[X + 1] + Z;
    return lerp(lerp(grad2(m_perm[A], x, z), grad2(m_perm[B], x - 1.0, z), u),
                lerp(grad2(m_perm[A + 1], x, z - 1.0), grad2(m_perm[B + 1], x - 1.0, z - 1.0), u), v);
}

double ChunkGeometry::fbm(double x, double z) const
{
    double total = 0, amp = 1, freq = 1, maxv = 0;
    for (int o = 0; o < 4; ++o) {
        total += noise2(x * freq, z * freq) * amp;
        maxv += amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return total / maxv; // ~[-1,1]
}

int ChunkGeometry::heightAt(int x, int z) const
{
    const double n = fbm((x + m_seed) * 0.09, (z + m_seed) * 0.09); // [-1,1]
    const int h = int(std::lround(7.0 + n * 4.0));                  // ~3..11
    return std::max(0, h);
}

quint8 ChunkGeometry::blockAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0; // 越界 = 空气 = 该面暴露
    return m_voxels[size_t(x + m_width * (z + m_depth * y))];
}

// 图集瓦片：草的顶=grass_top、底=dirt、四侧=grass_side；其它方块各面统一。
int ChunkGeometry::tileFor(quint8 block, int face) const
{
    // face: 0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z
    switch (block) {
    case 1: // grass
        if (face == 2) return 0; // grass_top
        if (face == 3) return 2; // dirt
        return 1;                // grass_side
    case 2: return 2;            // dirt
    case 3: return 3;            // stone
    case 4: return 4;            // sand
    }
    return 2;
}

void ChunkGeometry::regenerate()
{
    // 置换表（线性同余 RNG，可复现）
    m_perm.resize(512);
    int p[256];
    for (int i = 0; i < 256; ++i) p[i] = i;
    unsigned int state = unsigned(m_seed >= 0 ? m_seed : -m_seed) + 1u;
    for (int i = 255; i > 0; --i) {
        state = state * 1103515245u + 12345u;
        int j = int((state >> 16) % unsigned(i + 1));
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 512; ++i) m_perm[i] = p[i & 255];

    // 填充体素栅格
    m_voxels.fill(0, m_width * m_depth * m_height);
    constexpr int sandLevel = 3;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int h = std::min(heightAt(x, z), m_height - 1);
            for (int y = 0; y <= h; ++y) {
                quint8 b;
                if (h <= sandLevel)   b = 4;     // sand
                else if (y == h)      b = 1;     // grass
                else if (y >= h - 2)  b = 2;     // dirt
                else                  b = 3;     // stone
                m_voxels[size_t(x + m_width * (z + m_depth * y))] = b;
            }
        }
    }

    buildMesh();
}

void ChunkGeometry::buildMesh()
{
    QVector<Vtx> verts;
    QVector<quint32> idx;
    verts.reserve(4096);
    idx.reserve(8192);

    // 图集：5 瓦片横排（80×16）。半纹素内缩防溢出。
    constexpr int N = 5;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * 16); // = 0.5 / atlasWidthPx
    constexpr float hy = 0.5f / 16;       // = 0.5 / atlasHeightPx
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    for (int y = 0; y < m_height; ++y) {
        for (int z = 0; z < m_depth; ++z) {
            for (int x = 0; x < m_width; ++x) {
                const quint8 b = blockAt(x, y, z);
                if (b == 0) continue; // 空气
                for (int f = 0; f < 6; ++f) {
                    const FaceDef &F = kFaces[f];
                    // 邻居为实体 → 该面被遮挡，剔除
                    if (blockAt(x + F.dir[0], y + F.dir[1], z + F.dir[2]) != 0)
                        continue;

                    const int t = tileFor(b, f);
                    const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;

                    const quint32 base = quint32(verts.size());
                    for (int c = 0; c < 4; ++c) {
                        const float dx = F.c[c][0], dy = F.c[c][1], dz = F.c[c][2];
                        // 按角点位置算 UV：侧面的 v 取 y（草在上），上下面的 v 取 z。
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

    setBounds(QVector3D(0, 0, 0), QVector3D(m_width, m_height, m_depth));
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
