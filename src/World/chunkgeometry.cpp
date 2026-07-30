#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "world.h"

#include <QByteArray>
#include <QVector3D>

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
    if (m_world) disconnect(m_world, &World::worldChanged, this, &ChunkGeometry::onWorldChanged);
    m_world = w;
    if (m_world) connect(m_world, &World::worldChanged, this, &ChunkGeometry::onWorldChanged);
    emit worldChanged();
    onWorldChanged();
}

void ChunkGeometry::setCx(int cx)
{
    if (m_cx == cx) return;
    m_cx = cx;
    emit cxChanged();
    onWorldChanged();
}

void ChunkGeometry::setCz(int cz)
{
    if (m_cz == cz) return;
    m_cz = cz;
    emit czChanged();
    onWorldChanged();
}

// 本几何负责的 chunk（cx/cz 越界或 world 未设 → nullptr）。每次现查（不在本类缓存指针），
// 故 world 重建（recreate 销毁旧 chunk）后不会悬空：拿到的是新 chunk。
Chunk *ChunkGeometry::myChunk() const
{
    if (!m_world) return nullptr;
    return m_world->chunks().chunk(m_cx, m_cz); // cx/cz 越界 → nullptr
}

// dirty 驱动（dev-spec t03 验收）：仅当本 chunk 脏才重建。worldChanged 每次编辑都发，
// 但 9 个 ChunkGeometry 各检各的 chunk 脏标记 → rebuild 次数 = dirty chunk 数（非脏跳过）。
void ChunkGeometry::onWorldChanged()
{
    Chunk *c = myChunk();
    if (c && c->dirty()) buildMesh();
}

// 查表（单一权威：BlockRegistry）。行为与历史硬编码一致：草顶/草侧/草底、其余各面统一。
int ChunkGeometry::tileFor(quint8 block, int face) const
{
    // face: 0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z（须与 BlockRegistry::Face 一致）
    return BlockRegistry::tileIndex(block, BlockRegistry::Face(face));
}

void ChunkGeometry::buildMesh()
{
    Chunk *c = myChunk();
    const int H = m_world ? m_world->height() : 0;
    constexpr int S = Chunk::kSize; // 16（X、Z chunk 边长）
    const int originX = m_cx * S, originZ = m_cz * S; // chunk 世界起点

    QVector<Vtx> verts;
    QVector<quint32> idx;
    if (c && m_world) {
        verts.reserve(4096);
        idx.reserve(8192);
    }

    // 图集瓦片横排：18 瓦片（288×16）。BlockRegistry 为 13 方块定义 0..17 序号，
    // 与 tools/build_atlas.py 打包顺序严格一致（一个偏差即渗色/错贴）：
    //   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
    //   5=cobble 6=log_top 7=log_side 8=planks 9=leaves
    //   10=crafting_table_top 11=crafting_table_side（t50）
    //   12=furnace_top 13=furnace_side 14=furnace_front（t80）
    //   15=coal_ore 16=iron_ore（t84；矿石各面同贴图）
    //   17=torch（t88；6 面同贴图）
    // 半纹素内缩防渗色（线性采样跨瓦片）。
    constexpr int N = 18;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * 16);
    constexpr float hy = 0.5f / 16;
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    if (c && m_world) {
        // 逐局部格：世界坐标取体素 + 邻居（跨 chunk 边界经 world.blockAt 路由 → 共边实体面
        // 剔除、一侧空气画出、世界越界=空气）。顶点写 chunk 局部坐标（Model 负责世界定位）。
        for (int ly = 0; ly < H; ++ly) {
            for (int lz = 0; lz < S; ++lz) {
                for (int lx = 0; lx < S; ++lx) {
                    const int wx = originX + lx, wz = originZ + lz;
                    const quint8 b = blockAtWorld(wx, ly, wz);
                    if (b == 0) continue; // 空气
                    for (int f = 0; f < 6; ++f) {
                        const FaceDef &F = kFaces[f];
                        if (blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]) != 0)
                            continue; // 邻居实体 → 剔除（跨 chunk 边界同样正确）

                        const int t = tileFor(b, f);
                        const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;

                        const quint32 base = quint32(verts.size());
                        for (int cc = 0; cc < 4; ++cc) {
                            const float dx = F.c[cc][0], dy = F.c[cc][1], dz = F.c[cc][2];
                            float cu, cv;
                            if (f == 0 || f == 1) { cu = dz; cv = dy; }       // ±X
                            else if (f == 4 || f == 5) { cu = dx; cv = dy; }  // ±Z
                            else { cu = dx; cv = dz; }                        // ±Y
                            Vtx v;
                            v.x = float(lx) + dx; v.y = float(ly) + dy; v.z = float(lz) + dz; // 局部坐标
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

    // 网格统计（t10 F3 叠层）：顶点 / 三角面数（idx/3）在数据 finalize 后、上传前记录。
    m_vertexCount = int(verts.size());
    m_triangleCount = int(idx.size() / 3);

    // 写入 QQuick3DGeometry（文档顺序：clear → 数据 → stride → bounds → 原语 → 属性 → update）
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

    setBounds(QVector3D(0, 0, 0), QVector3D(S, H, S)); // 局部 bounds（Model 摆位负责世界定位）
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

    if (c) c->clearDirty(); // mesh 已刷新 → chunk 不再脏（下次 worldChanged 跳过它）

    // 可观测性（dev-spec t03 验收「日志证明 rebuild 次数 = dirty chunk 数」）：仅脏 chunk 走到此，
    // 非脏 chunk 在 onWorldChanged 已 return。读此日志可知每次 worldChanged 后实际重建了哪些 chunk。
    qInfo("vo.render: chunk(%d,%d) rebuilt - %lld verts / %lld idx",
          m_cx, m_cz, qint64(verts.size()), qint64(idx.size()));

    // 通知 F3 叠层刷新（顶点 / 三角面数已更新；t10）。
    emit meshRebuilt();
}
