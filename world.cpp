#include "world.h"

#include "blockregistry.h"

#include <algorithm>
#include <cmath>

World::World(QObject *parent) : QObject(parent)
{
    generate(); // 用默认参数生成（静默，不 emit）
}

void World::setWidth(int w)  { if (w == m_width)  return; m_width = w;  emit widthChanged();  generate(); emit worldChanged(); }
void World::setDepth(int d)  { if (d == m_depth)  return; m_depth = d;  emit depthChanged();  generate(); emit worldChanged(); }
void World::setHeight(int h) { if (h == m_height) return; m_height = h; emit heightChanged(); generate(); emit worldChanged(); }
void World::setSeed(int s)   { if (s == m_seed)   return; m_seed = s;   emit seedChanged();   generate(); emit worldChanged(); }

quint8 World::blockAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0; // 越界 = 空气（面剔除会把边界面画出来；物理把界外当可走出/可坠落）
    return m_voxels[size_t(x + m_width * (z + m_depth * y))];
}

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

double World::noise2(double x, double z) const
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

double World::fbm(double x, double z) const
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

int World::heightAt(int x, int z) const
{
    const double n = fbm((x + m_seed) * 0.09, (z + m_seed) * 0.09); // [-1,1]
    const int h = int(std::lround(7.0 + n * 4.0));                  // ~3..11
    return std::max(0, h);
}

void World::generate()
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

    // 填充体素栅格（与原 ChunkGeometry 完全一致，地形外观不变）
    m_voxels.fill(0, m_width * m_depth * m_height);
    constexpr int sandLevel = 3;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int h = std::min(heightAt(x, z), m_height - 1);
            for (int y = 0; y <= h; ++y) {
                quint8 b;
                if (h <= sandLevel)   b = BlockRegistry::Sand;   // sand
                else if (y == h)      b = BlockRegistry::Grass;  // grass
                else if (y >= h - 2)  b = BlockRegistry::Dirt;   // dirt
                else                  b = BlockRegistry::Stone;  // stone
                m_voxels[size_t(x + m_width * (z + m_depth * y))] = b;
            }
        }
    }
}
