#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "partialblockgeometry.h" // t133：Vtx（chunk 顶点格式）+ PartialBlockGeometry 异形分支
#include "world.h"

#include <QByteArray>
#include <QVector3D>

#include <algorithm> // std::clamp / std::max（t151 真光场顶点色钳制）
#include <cstddef>
#include <cstring>

// Vtx（chunk 顶点格式：pos3 + normal3 + uv2 + color4 rgba = 12 float = 48 字节）定义在
// partialblockgeometry.h —— 由本文件（1×1×1 立方面）与 PartialBlockGeometry::append（异形方块）
// 共用，二者合批进同一 chunk mesh 的同一顶点缓冲。color.rgb 承载 t121 天光遮蔽 + t123 方向太阳光调制。

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

// t123：太阳方向变（WorldClock 量化跨步 → sunChanged → QML 绑定重算 → 本 setter）。
//   体素未变、仅光照变 → 直接 buildMesh（绕过 chunk dirty：dirty 是「体素改动」标记，与此无关）。
//   值未变（WorldClock 跨步但量化值恰好相同 / QML 重绑）则早退，避免无谓重建。
void ChunkGeometry::setSunDir(const QVector3D &dir)
{
    if (m_sunDir == dir) return;
    m_sunDir = dir;
    emit sunInputChanged();
    buildMesh();
}

// t148：水段开关变 → 重建（水段 / 地形段选块不同，需重网格化）。值未变则早退。
void ChunkGeometry::setWaterOnly(bool on)
{
    if (m_waterOnly == on) return;
    m_waterOnly = on;
    emit waterOnlyChanged();
    buildMesh();
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

    // 图集瓦片横排：20 瓦片（320×16）。BlockRegistry 为各方块定义 0..19 序号，
    // 与 tools/build_atlas.py 打包顺序严格一致（一个偏差即渗色/错贴）：
    //   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
    //   5=cobble 6=log_top 7=log_side 8=planks 9=leaves
    //   10=crafting_table_top 11=crafting_table_side（t50）
    //   12=furnace_top 13=furnace_side 14=furnace_front（t80）
    //   15=coal_ore 16=iron_ore（t84；矿石各面同贴图）
    //   17=torch（t88；6 面同贴图）
    //   18=bedrock（t119；6 面同贴图，深灰斑驳底岩）
    //   19=water（t148；6 面同贴图，蓝；纹理不透明，半透由水材质 opacity=0.7 实现）
    // 半纹素内缩防渗色（线性采样跨瓦片）。
    constexpr int N = 20;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * 16);
    constexpr float hy = 0.5f / 16;
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    // t151 真光场顶点色常量（PLAN §2-H / §M，替代 t123 方向太阳 faceVc）：
    //   光场值 = 邻格（面所朝向的空气格）的 max(sky, block)/15，直接作顶点色。天光 / 火把方光由 World 的
    //   BFS flood-fill 算出（存 chunk 第三数组），mesher 只读采样。昼夜乘子仍由 QML baseColor 承担
    //   （terrainLight(skyLight) 平滑 lerp），故光场时间不变 —— 不随 sunDir 重建而变（sunDir 保留供 t153
    //   PCF 软影重用，方向阴影那套 faceNormal·sunDir + heightmap 列投影已由此真光场替代）。
    //   kVcMin：未照明格（深洞无天光 / 无火把）的底亮度 —— 防纯黑撕裂、保留微弱可辨识（MC 为纯黑，
    //     此处取小底兼顾可玩性；火把光池 0.93 与之强对比，洞穴暗 / 火把亮一目了然）。
    //   kVcMax：满光封顶 1.0（NoLighting 无法 overbright，贴图原色即最亮）。
    constexpr float kVcMin = 0.05f;
    constexpr float kVcMax = 1.0f;

    if (c && m_world) {
        // 逐局部格：世界坐标取体素 + 邻居（跨 chunk 边界经 world.blockAt 路由 → 共边实体面
        // 剔除、一侧空气画出、世界越界=空气）。顶点写 chunk 局部坐标（Model 负责世界定位）。
        for (int ly = 0; ly < H; ++ly) {
            for (int lz = 0; lz < S; ++lz) {
                for (int lx = 0; lx < S; ++lx) {
                    const int wx = originX + lx, wz = originZ + lz;
                    const quint8 b = blockAtWorld(wx, ly, wz);
                    if (b == 0) continue; // 空气
                    // t148 水渲染分流：一个 chunk 由两段 ChunkGeometry 网格化 —— 地形段（waterOnly=false）
                    //   只取非水方块；水段（waterOnly=true）只取 Water。这样水走独立透明材质（opacity=0.7）、
                    //   地形走不透明材质，二者不互相污染（水不会被当不透明地形误渲、地形不会因水材质变透）。
                    //   isWater != m_waterOnly 即「本段不要这块」→ 跳过（地形段跳水 / 水段跳非水）。
                    const bool isWater = (b == BlockRegistry::Water);
                    if (isWater != m_waterOnly) continue;
                    // t114 异形特例：火把不画 1×1×1 立方面 —— 它是「木柄 + 火焰」小模型（在 torchHost
                    // 由 QML Model 渲染，朝向据邻居 solid 推断）。mesher 在此跳过立方面，否则会出现
                    // 「黑底 6 面大立方 + 上叠小模型」的重复畸形。其他异形方块（未来如花/草）按同模式加。
                    //   水段不会到此（水 != Torch，已被 isWater != m_waterOnly 跳过），故仅地形段需此守卫。
                    if (!isWater && b == BlockRegistry::Torch) continue;

                    // t151 真光场：本格光场值（max(sky,block)/15）用于异形方块各面顶点色（近似：异形小体
                    //   各面共用以本格光场，因其面所朝向的「邻格」在子格尺度上歧义；异形格非遮光 → 光场
                    //   已 flood 反映周围天光 / 火把光）。采样本格（wx,ly,wz）；立方体面则采邻格（下方）。
                    const quint8 cSky = m_world->skyLightAt(wx, ly, wz);
                    const quint8 cBlock = m_world->blockLightAt(wx, ly, wz);
                    const float cellLight = std::clamp(std::max(cSky, cBlock) / 15.0f, kVcMin, kVcMax);

                    // t133 不完整方块异形分支：id >= FirstPartial 的方块不走 1×1×1 立方面路径，交由
                    //   PartialBlockGeometry::append 生成异形顶点并**合批进同一 chunk mesh**（复用本顶点
                    //   缓冲 + 顶点色光照管线 + 单 draw call，非另起 Model）。本块的光照上下文（surface /
                    //   shade / sun 量）已在上方算好，打包进 PartialLightCtx 传入；append 据各面外法线按
                    //   同款公式复算 vc。当前 FirstPartial=15=BlockRegistry::Count → 任何合法 id <
                    //   FirstPartial → 此分支永不进入（无任何异形方块定义）；t134 加 WoodSlab=15.. 后激活。
                    //   t148：Water id=21 也 >= FirstPartial，但水是整立方（走下方立方面路径），且水段已
                    //   由 isWater 守卫隔离——此处 `!isWater` 防 PartialBlockGeometry 收到 Water（其 switch
                    //   无 Water case → default 返 0 不画 → 水面丢失）。
                    if (!isWater && b >= BlockRegistry::FirstPartial) {
                        const quint8 st = stateAtWorld(wx, ly, wz);
                        // t151：异形方块各面共用本格光场值作顶点色（PartialLightCtx.light）。
                        const PartialLightCtx lctx{ cellLight };
                        PartialBlockGeometry::append(verts, idx, lx, ly, lz, b, st,
                                                     lctx, tileW, hx, hy, v0, v1);
                        continue;
                    }

                    for (int f = 0; f < 6; ++f) {
                        const FaceDef &F = kFaces[f];
                        const quint8 nb = blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                        // 邻居实体 → 剔除（跨 chunk 边界同样正确）。走 BlockRegistry::isSolid 单一权威，
                        // 与 playercontroller/raycast 的 isSolid 判定同源（PLAN §2：世界数据单一）。
                        //   原 `!= 0` 把任意非空气方块当 solid，导致火把(solid=false) 误判为挡面 →
                        //   火把下方地板的顶面被错误剔除 → 地板透明（t130 修复根因）。
                        //   越界 / air / torch 等 solid=false 方块不挡邻居面（应画出）。
                        if (BlockRegistry::isSolid(nb))
                            continue;
                        // t148 水-水面互剔（spec「邻居剔除 nb==Water」）：水段渲染水块时，若邻居也是水
                        //   则该面是水体内部面 → 剔除（仅留水-空气接触面 = 水面 / 暴露侧）。同 solid 判定
                        //   一样走「邻居实体性」语义：水对水不可见、水对空气可见。（solid 判定已覆盖水贴
                        //   地形那面——地形 solid=true → 水面被剔，避免与地形面重合 z-fight。）
                        if (isWater && nb == BlockRegistry::Water)
                            continue;

                        const int t = tileFor(b, f);
                        const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;

                        // t151 真光场顶点色（替代 t123 方向太阳 faceVc）：本面照明 = 邻格（面所朝向的空气格）
                        //   的光场值 max(sky,block)/15。邻格为非 solid（空气 / 火 / 水 / 异形）→ 已 flood 得天光 /
                        //   火把光。越界 y>=height 视作开阔天光 15（顶面采样）。clamped [kVcMin, kVcMax]。
                        //   方向阴影（faceNormal·sunDir + heightmap 列投影）已移除，留 t153 PCF 软影重做。
                        const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                        const quint8 nbSky = m_world->skyLightAt(ax, ay, az);
                        const quint8 nbBlock = m_world->blockLightAt(ax, ay, az);
                        const float vc = std::clamp(std::max(nbSky, nbBlock) / 15.0f, kVcMin, kVcMax);

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
                            v.r = vc; v.g = vc; v.b = vc; v.a = 1.0f; // t151 真光场顶点色（邻格 max(sky,block)/15）
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
    setStride(int(sizeof(Vtx))); // 48（pos3 + normal3 + uv2 + color4 rgba）

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
    // t121：顶点色（vec4 rgba）。PrincipledMaterial vertexColorsEnabled=true 时最终色 = baseColor × vertexColor × 贴图。
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic,
                 int(offsetof(Vtx, r)), QQuick3DGeometry::Attribute::F32Type);
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
