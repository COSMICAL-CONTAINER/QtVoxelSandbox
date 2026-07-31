#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "partialblockgeometry.h" // t133：Vtx（chunk 顶点格式）+ PartialBlockGeometry 异形分支
#include "world.h"

#include <QByteArray>
#include <QVector3D>

#include <algorithm> // std::clamp / std::max（t123 顶点光方向调制）
#include <cstddef>
#include <cmath>     // std::sqrt / std::fabs / std::round（t123 投影阴影回扫）
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

    // 图集瓦片横排：19 瓦片（304×16）。BlockRegistry 为 14 方块定义 0..18 序号，
    // 与 tools/build_atlas.py 打包顺序严格一致（一个偏差即渗色/错贴）：
    //   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
    //   5=cobble 6=log_top 7=log_side 8=planks 9=leaves
    //   10=crafting_table_top 11=crafting_table_side（t50）
    //   12=furnace_top 13=furnace_side 14=furnace_front（t80）
    //   15=coal_ore 16=iron_ore（t84；矿石各面同贴图）
    //   17=torch（t88；6 面同贴图）
    //   18=bedrock（t119；6 面同贴图，深灰斑驳底岩）
    // 半纹素内缩防渗色（线性采样跨瓦片）。
    constexpr int N = 19;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * 16);
    constexpr float hy = 0.5f / 16;
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    // t123 动态太阳顶点光常量（方案②：顶点色承载方向调制，PLAN §2-H 非 lit/shadowmap）：
    //   kSunRange：方向对比强度。朝太阳面（lit - litAvg > 0）超过 1 → 夹到 kSunMax（最亮=贴图原色）；
    //     背阳 / 投影阴影面（< 0）低于 1，呈暗。
    //   kSunMin / kSunMax：vc 钳制范围。t135 下限 0.3→0.15 放宽对比（阴影面更暗、朝太阳面更亮，
    //     方向感与投影对比更明显；仍不黑死）；上限 1.0 不超过贴图原色（NoLighting 无法 overbright）。
    //   kMaxShadow：heightmap 列投影阴影回扫最大列距离（界性能；低空太阳 tanElev 小 → 自然长阴影，超距截断）。
    //     t135 10→32：低空 / 接近正午时仍能扫到挡光列，阴影更长更连续（影子跟随太阳移动更平滑）。
    constexpr float kSunRange   = 1.5f;
    constexpr float kSunMin     = 0.15f;
    constexpr float kSunMax     = 1.0f;
    constexpr int   kMaxShadow  = 32;
    // 太阳派生量（由 m_sunDir 算一次，供本 chunk 全部顶点光照复用）：
    //   sunIntensity：太阳高度归一（sin(kSunMaxElevDeg)=0.766 为满日照基准）。sunDir.y<0（地平下）→ 0
    //     → 白天方向调制生效、夜间归零（vc=1，与 t121 等亮，无夜间变暗回归）。
    //   sunHx/Hz：太阳水平单位方向（投影阴影回扫方向）；hlen≈0（天顶）时无水平阴影（短至零长）。
    //   sunTanElev：仰角正切 = sunDir.y/hlen。低空 → 小 → 同样高的列在更远处仍挡（长阴影）。
    //   sunLitAvg：6 轴面方向 lit 的均值（|sx|+|sy|+|sz|)/6。「均值归一」使全天平均亮度 ≈ 天光：
    //     direction 对比在均值附近对称展开，朝太阳 +、背阳 −，整体不净变暗。
    const float sdx = m_sunDir.x(), sdy = m_sunDir.y(), sdz = m_sunDir.z();
    const float sunIntensity = std::clamp(sdy / 0.766f, 0.f, 1.f);
    const float sunHlen = std::sqrt(sdx * sdx + sdz * sdz);
    const float sunHx = (sunHlen > 1e-3f) ? sdx / sunHlen : 0.f;
    const float sunHz = (sunHlen > 1e-3f) ? sdz / sunHlen : 0.f;
    const float sunTanElev = (sunHlen > 1e-3f) ? sdy / sunHlen : 0.f;
    const float sunLitAvg = (std::fabs(sdx) + std::fabs(sdy) + std::fabs(sdz)) / 6.f;

    if (c && m_world) {
        // 逐局部格：世界坐标取体素 + 邻居（跨 chunk 边界经 world.blockAt 路由 → 共边实体面
        // 剔除、一侧空气画出、世界越界=空气）。顶点写 chunk 局部坐标（Model 负责世界定位）。
        for (int ly = 0; ly < H; ++ly) {
            for (int lz = 0; lz < S; ++lz) {
                for (int lx = 0; lx < S; ++lx) {
                    const int wx = originX + lx, wz = originZ + lz;
                    const quint8 b = blockAtWorld(wx, ly, wz);
                    if (b == 0) continue; // 空气
                    // t114 异形特例：火把不画 1×1×1 立方面 —— 它是「木柄 + 火焰」小模型（在 torchHost
                    // 由 QML Model 渲染，朝向据邻居 solid 推断）。mesher 在此跳过立方面，否则会出现
                    // 「黑底 6 面大立方 + 上叠小模型」的重复畸形。其他异形方块（未来如花/草）按同模式加。
                    if (b == BlockRegistry::Torch) continue;

                    // t121 天光 heightmap（PLAN §2-H「per-column 天光」）：本列首个非空气方块的 y。
                    // ly >= hm → 见天（地表/天空间）；否则地下。t123 改：见天块叠方向太阳光 + 投影阴影，
                    //   地下块恒暗 0.2（天光 flood-fill 留后续轮次）。
                    const int hm = m_world->heightmapAt(wx, wz);
                    const bool surface = (ly >= hm);

                    // t123 heightmap 列投影阴影：本块见天且太阳在地平线上时，沿太阳水平方向（朝太阳）
                    //   回扫邻接列 heightmap；若有列足够高（挡住本块到太阳的视线：colHm >= ly + d·tanElev）
                    //   则本块被投影阴影（shade=0 → 朝太阳的面失去直射贡献、呈阴影）。跨 chunk 经
                    //   world.heightmapAt 路由 → 阴影无缝跨越边界。太阳低空时 tanElev 小 → 长阴影。
                    //   本量为「每块」算一次（块内 6 面共享），界性能（仅见天块 + 太阳在上时算）。
                    //   t135 门控放宽：原 `sunIntensity > 1e-3f` 在正午（sunDir 接近天顶，水平投影小）与黄昏
                    //     （sunIntensity 趋 0）都不投影 → 一大坨无阴影。改判 `sdy > 0.f`：太阳只要在地平线上就
                    //     算投影（含正午低水平角与黄昏），影子在全天白昼段都生效（更连续）。
                    float shade = 1.0f;
                    if (surface && sdy > 0.f && sunHlen > 0.05f) {
                        for (int d = 1; d <= kMaxShadow; ++d) {
                            const int qx = wx + int(std::round(sunHx * float(d)));
                            const int qz = wz + int(std::round(sunHz * float(d)));
                            const int chm = m_world->heightmapAt(qx, qz); // 越界 / 空列 → -1（不挡）
                            if (chm >= 0 && float(chm) >= float(ly) + float(d) * sunTanElev) {
                                shade = 0.0f;
                                break;
                            }
                        }
                    }

                    // t133 不完整方块异形分支：id >= FirstPartial 的方块不走 1×1×1 立方面路径，交由
                    //   PartialBlockGeometry::append 生成异形顶点并**合批进同一 chunk mesh**（复用本顶点
                    //   缓冲 + 顶点色光照管线 + 单 draw call，非另起 Model）。本块的光照上下文（surface /
                    //   shade / sun 量）已在上方算好，打包进 PartialLightCtx 传入；append 据各面外法线按
                    //   同款公式复算 vc。当前 FirstPartial=15=BlockRegistry::Count → 任何合法 id <
                    //   FirstPartial → 此分支永不进入（无任何异形方块定义）；t134 加 WoodSlab=15.. 后激活。
                    if (b >= BlockRegistry::FirstPartial) {
                        const quint8 st = stateAtWorld(wx, ly, wz);
                        const PartialLightCtx lctx{
                            sdx, sdy, sdz,
                            sunIntensity, sunLitAvg,
                            shade, surface
                        };
                        PartialBlockGeometry::append(verts, idx, lx, ly, lz, b, st,
                                                     lctx, tileW, hx, hy, v0, v1);
                        continue;
                    }

                    for (int f = 0; f < 6; ++f) {
                        const FaceDef &F = kFaces[f];
                        // 邻居实体 → 剔除（跨 chunk 边界同样正确）。走 BlockRegistry::isSolid 单一权威，
                        // 与 playercontroller/raycast 的 isSolid 判定同源（PLAN §2：世界数据单一）。
                        //   原 `!= 0` 把任意非空气方块当 solid，导致火把(solid=false) 误判为挡面 →
                        //   火把下方地板的顶面被错误剔除 → 地板透明（t130 修复根因）。
                        //   越界 / air / torch 等 solid=false 方块不挡邻居面（应画出）。
                        if (BlockRegistry::isSolid(blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2])))
                            continue;

                        const int t = tileFor(b, f);
                        const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;

                        // t123 顶点光方向调制（写进 color.rgb，PrincipledMaterial NoLighting×vertexColor）：
                        //   地下块：恒暗 0.2（无天光 / 太阳）。
                        //   见天块：vc = clamp(1 + kSunRange·sunIntensity·(lit·shade − sunLitAvg), kSunMin, kSunMax)。
                        //     lit = max(0, faceNormal·sunDir)（朝太阳的面 >0、背阳 =0）；shade 门控投影阴影。
                        //     「均值归一」（减 sunLitAvg）使全天平均 ≈ 天光：sunIntensity=0（夜）→ vc=1（与 t121
                        //     等亮、无夜间变暗）；白天朝太阳面 >1（夹 kSunMax）、背阳/阴影面 <1（暗）。
                        //   方向感 + 投影阴影由此显现，且随 sunDir 量化跨步演变（太阳「时间流逝移动」）。
                        float vc;
                        if (!surface) {
                            vc = 0.2f;
                        } else {
                            const float lit = std::max(0.f, F.nrm[0] * sdx + F.nrm[1] * sdy + F.nrm[2] * sdz);
                            const float contrast = lit * shade - sunLitAvg;
                            vc = std::clamp(1.f + kSunRange * sunIntensity * contrast, kSunMin, kSunMax);
                        }

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
                            v.r = vc; v.g = vc; v.b = vc; v.a = 1.0f; // t123 顶点光（方向太阳 + 天光遮蔽）
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
