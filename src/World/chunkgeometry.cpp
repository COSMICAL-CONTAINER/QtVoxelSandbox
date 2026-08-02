#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "partialblockgeometry.h" // t133：Vtx（chunk 顶点格式）+ PartialBlockGeometry 异形分支
#include "world.h"

#include <QByteArray>
#include <QElapsedTimer> // t155f：buildMesh 计时（诊断编辑卡顿）
#include <QVector3D>

#include <algorithm> // std::clamp / std::max（t151 真光场顶点色钳制）
#include <cmath>     // std::sqrt / std::floor（t153 PCF 软影 sunShadowAt）
#include <cstddef>
#include <cstring>
#include <vector>    // t178 greedy meshing mask 缓冲（std::vector）

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

// t153 PCF 软影调参（spec「kMaxShadow 短 / kSunMin 高」；方案③：t151 顶点光基底 + heightmap 正交深度图）。
//   见 ChunkGeometry::sunShadowAt。文件作用域供 buildMesh 与 sunShadowAt 共用。
//   - kSunMin：太阳高度门（sunDir.y 下限）。太阳低于此（黎明/黄昏/夜间）不投影 → 避免低角度极长影扫出
//     世界；门偏高 → 仅近正午投影（用户嫌 t135「影一大坨」：高门 + 短步把影收紧到日中、贴近障碍）。
//   - kSunFade：门附近淡入淡出带宽。sunDir.y 量化跨步到门两侧时影因子平滑过 0，无突变跳变。
//   - kMaxShadow：投影步进上限（格）。短 → 影紧凑、计算省（每顶点 kMaxShadow×4 次 heightmap 查询），
//     且低角度时影被截断不无限延伸。
constexpr float kSunMin    = 0.30f; // ≈17.5° 仰角门（max 仰角 50°→sin=0.766；日中窗 [17.5°,50°]）
constexpr float kSunFade   = 0.10f; // 门附近 ±0.10 band 平滑淡入
constexpr int   kMaxShadow = 4;     // 步进上限 4 格（每顶点 4×4=16 次 heightmap 查询；影短促紧凑）

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
//   体素未变、仅光照变 → 直接 buildMesh(Sun)（绕过 chunk dirty：dirty 是「体素改动」标记，与此无关）。
//   值未变（WorldClock 跨步但量化值恰好相同 / QML 重绑）则早退，避免无谓重建。
//   t155：编辑活跃期 WorldClock 节流跳过 sunChanged（见 worldclock.cpp onTick）→ 本 setter 在编辑密集
//   段不被太阳跨步触发，编辑即时重建（onWorldChanged）独占主线程，无抢帧。
void ChunkGeometry::setSunDir(const QVector3D &dir)
{
    if (m_sunDir == dir) return;
    m_sunDir = dir;
    emit sunInputChanged();
    buildMesh(RebuildReason::Sun);
}

// t148：水段开关变 → 重建（水段 / 地形段选块不同，需重网格化）。值未变则早退。
void ChunkGeometry::setWaterOnly(bool on)
{
    if (m_waterOnly == on) return;
    m_waterOnly = on;
    emit waterOnlyChanged();
    buildMesh(RebuildReason::Water);
}

// t166b 阴影开关变 → 重网格化（顶点光 PCF 软影随开关重算；语义同光照变 → 用 Sun reason）。值未变早退。
void ChunkGeometry::setShadowsEnabled(bool on)
{
    if (m_shadowsEnabled == on) return;
    m_shadowsEnabled = on;
    emit shadowsEnabledChanged();
    buildMesh(RebuildReason::Sun);
}

// t178 贪婪网格化开关变 → 重网格化（greedy vs 逐格 culled 顶点布局不同，须重建）。值未变早退。
//   用 Dirty reason（同编辑即时重建路径，绕过 sun-step 节流）。
void ChunkGeometry::setGreedyMeshing(bool on)
{
    if (m_greedyMeshing == on) return;
    m_greedyMeshing = on;
    emit greedyMeshingChanged();
    buildMesh(RebuildReason::Dirty);
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
// t155 编辑即时重建保证：setBlock → ChunkManager.markDirty → World::emit worldChanged（GUI 同线程
//   直连）→ 本槽**同步**执行 buildMesh(Dirty)，破 / 放后贴图当帧刷新（不延迟到太阳步进，<1 帧）。
//   clearDirty 由本（编辑）路径独占 —— 太阳刷新（setSunDir→buildMesh(Sun)）见到的 chunk 永远非脏
//   （编辑的 onWorldChanged 已同步清过），故清脏条件 `c->dirty()` 在太阳路径恒 false 不清、在编辑路径
//   恒 true 清除，二者语义解耦、对任何太阳时序 immediate rebuild 都稳健（见 buildMesh 末尾清脏）。
void ChunkGeometry::onWorldChanged()
{
    Chunk *c = myChunk();
    if (c && c->dirty()) buildMesh(RebuildReason::Dirty);
}

// 查表（单一权威：BlockRegistry）。行为与历史硬编码一致：草顶/草侧/草底、其余各面统一。
int ChunkGeometry::tileFor(quint8 block, int face) const
{
    // face: 0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z（须与 BlockRegistry::Face 一致）
    return BlockRegistry::tileIndex(block, BlockRegistry::Face(face));
}

// t153 PCF 软影（方案③：t151 顶点光基底 + heightmap 正交深度图 PCF 0..1 软过渡）。
//   给定世界空间顶点 (wx,wy,wz)，沿太阳「水平方向」步进 kMaxShadow 格，逐步采样路径所过列的 heightmap
//   （= 该列正交深度，列顶实体方块顶面 = heightmap+1）。若列顶面高于太阳光线在该列的高度 → 该列遮挡。
//   PCF：每步采样路径点周围 2×2 最近整数列（floor/ceil）取平均 → 半格列贡献 0.5 遮挡，影边 0..1 软过渡
//   （非硬二值）。返回 [0,1] 软影因子（0=全亮、1=全影）。
//
//   方向基底（不开 lit 红线）：顶点 vc 的天光分量由 t151 flood-fill 光场决定（开敞见天 / 洞穴暗），PCF 在此
//   基础上把「太阳被邻近高地遮挡」处再压暗；火把方光（blockLight）不受影（mesher 取 max(sky*(1-sh), block)
//   保留）。昼夜乘子仍由 QML baseColor（terrainLight）承担，故影因子本身时间不变 —— 仅随 sunDir（量化跨步）
//   变 → 绑 sunChanged 重建（每顶点重算）。
//
//   退化情形：太阳低于门 kSunMin（含夜间 sunDir.y<=0）→ 影因子 0（vc 全由光场基底照亮）；太阳近天顶
//   （水平分量≈0）→ 影无方向感、退化为不投影。门附近按 kSunFade 平滑淡入，防量化跨步时影突变。
//   分层（PLAN §2）：本属 Renderer（mesher），只读 World::heightmapAt（World 层）+ 接收裸 sunDir，不依赖
//   Game 层 WorldClock —— 保持依赖向下。
float ChunkGeometry::sunShadowAt(float wx, float wy, float wz) const
{
    if (!m_world) return 0.0f;
    if (!m_shadowsEnabled) return 0.0f; // t166b：阴影开关关 → 跳过 PCF（提速 meshing / 诊断卡顿）
    const QVector3D s = m_sunDir;
    if (s.y() <= kSunMin) return 0.0f;                       // 太阳低于门 → 不投影（黎明/黄昏/夜间）
    const float sh = std::sqrt(s.x() * s.x() + s.z() * s.z());
    if (sh < 1e-3f) return 0.0f;                              // 太阳近天顶 → 影无方向感，退化不投影
    const float invSh = 1.0f / sh;
    const float hxp = s.x() * invSh, hzp = s.z() * invSh;     // 水平面归一太阳方向
    const float vyp = s.y() * invSh;                          // 单位水平距离的垂直爬升（= tan(仰角)）
    // 门附近窄带平滑淡入（防量化跨步影突变）：sunDir.y∈[kSunMin, kSunMin+kSunFade] → 0..1。
    const float elevFade = std::clamp((s.y() - kSunMin) / kSunFade, 0.0f, 1.0f);
    if (elevFade <= 0.0f) return 0.0f;

    int occluded = 0, total = 0;
    for (int k = 1; k <= kMaxShadow; ++k) {
        // 步进 k 格水平距离：光线落点列 (ox,oz)、该列光线高度 rayY。
        const float ox = wx + float(k) * hxp;
        const float oz = wz + float(k) * hzp;
        const float rayY = wy + float(k) * vyp;
        // PCF：采样路径点周围 2×2 最近整数列（floor / +1）→ 影边半格列贡献 0.5，软过渡。
        const int x0 = int(std::floor(ox));
        const int z0 = int(std::floor(oz));
        for (int xi = 0; xi < 2; ++xi) {
            for (int zi = 0; zi < 2; ++zi) {
                const int hm = m_world->heightmapAt(x0 + xi, z0 + zi);
                // 列顶实体顶面（heightmap+1）高于光线 → 遮挡；hm<0（空列 / 越界）永不遮挡。
                if (hm >= 0 && float(hm) + 1.0f > rayY) ++occluded;
                ++total;
            }
        }
    }
    if (total == 0) return 0.0f;
    return (float(occluded) / float(total)) * elevFade;
}

void ChunkGeometry::buildMesh(RebuildReason reason)
{
    QElapsedTimer bt; bt.start(); // t155f：诊断编辑卡顿（每 chunk 重建耗时）
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
    //   20=chest_top / 21=chest_side / 22=chest_front（t173；箱子方块各面贴图）
    // 半纹素内缩防渗色（线性采样跨瓦片）。
    constexpr int N = 23;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * 16);
    constexpr float hy = 0.5f / 16;
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    // t151 真光场 + t153 PCF 软影顶点色（PLAN §2-H / §M，替代 t123 方向太阳 faceVc）：
    //   光场基底 = 邻格（面所朝向的空气格）的 max(sky, block)/15，由 World 的 BFS flood-fill 算出（存 chunk
    //   第三数组），mesher 只读采样。t153 在此基底上叠 PCF 软影：天光分量再乘 (1 - sunShadowAt)，把「太阳
    //   被邻近高地遮挡」处压暗（heightmap 正交深度图沿 sunDir 步进、2×2 PCF 软过渡）；火把方光（block）
    //   不受影（取 max 保留）。昼夜乘子仍由 QML baseColor 承担（terrainLight 平滑 lerp），故光场本身时间
    //   不变 —— 影因子仅随 sunDir（量化跨步）变 → 绑 sunChanged 重建（sunShadowAt 见上方）。
    //   kVcMin：未照明格（深洞无天光 / 无火把）的底亮度 —— 防纯黑撕裂、保留微弱可辨识（MC 为纯黑，
    //     此处取小底兼顾可玩性；火把光池 0.93 与之强对比，洞穴暗 / 火把亮一目了然）。
    //   kVcMax：满光封顶 1.0（NoLighting 无法 overbright，贴图原色即最亮）。
    constexpr float kVcMin = 0.08f; // t166：暗部地板 0.05→0.08（用户「黑的地方稍太黑」；洞穴/阴影最低亮度微抬，仍远低于火把光池 0.93 保持对比）
    constexpr float kVcMax = 1.0f;

    if (c && m_world) {
        // ---- PASS 1：不完整方块（异形）合批进同一 chunk mesh（t133 PartialBlockGeometry）----
        //   **terrain 段独有**（水段无 partial；waterOnly 守卫防水段 ChunkGeometry 重复渲染异形方块）。
        //   每 cell 仅一次 append。独立于 PASS 2 的面 mask——否则 6 面 mask 各扫一次会 6× 重复 append。
        //   torch / 整立方 / 水不进此 pass。光照上下文（cellLight）按本格光场 + 本格中心 PCF 软影算
        //   （同 t151/t153 异形约定），打包进 PartialLightCtx 传入。
        if (!m_waterOnly) for (int ly = 0; ly < H; ++ly) {
            for (int lz = 0; lz < S; ++lz) {
                for (int lx = 0; lx < S; ++lx) {
                    const int wx = originX + lx, wz = originZ + lz;
                    const quint8 b = blockAtWorld(wx, ly, wz);
                    if (b == 0) continue;
                    if (b == BlockRegistry::Water) continue;       // 水走 PASS 2 立方面（水段）
                    if (b == BlockRegistry::Torch) continue;       // 火把走 torchHost（QML Model）
                    if (b < BlockRegistry::FirstPartial) continue; // 仅异形方块进此 pass
                    const quint8 cSky = m_world->skyLightAt(wx, ly, wz);
                    const quint8 cBlock = m_world->blockLightAt(wx, ly, wz);
                    const float cShadow = sunShadowAt(float(wx) + 0.5f, float(ly) + 0.5f, float(wz) + 0.5f);
                    const float cellLight = std::clamp(
                        std::max((cSky / 15.0f) * (1.0f - cShadow), cBlock / 15.0f), kVcMin, kVcMax);
                    const quint8 st = stateAtWorld(wx, ly, wz);
                    const PartialLightCtx lctx{ cellLight };
                    PartialBlockGeometry::append(verts, idx, lx, ly, lz, b, st,
                                                 lctx, tileW, hx, hy, v0, v1);
                }
            }
        }

        // ---- PASS 2：标准 1×1×1 立方面（terrain 实心整立方 + 水）----
        if (m_greedyMeshing) {
            // t178 贪婪网格化（greedy meshing，PLAN §4 性能打磨）：按 6 面方向逐「层」建 2D mask，合并同
            //   (tile, 邻格天光, 邻格方光) 的共面连续格为单个矩形 → 顶点 / 三角 / 索引数大幅下降（平坦地面
            //   16×16=256 quad → 1 quad）。F3 叠层据此可观测 meshing 吞吐改善（PLAN §2-F）。
            //
            //   合并键含光照（tile + 邻格 sky + 邻格 block）→ 仅**均匀照明**区合并（保光照保真：合并 quad
            //   四角共享同一邻格光值，内部不被误暗；火把 / 墙边光照变化处不合并，贴图也保持逐格清晰）。
            //   PCF 软影仍 per-vertex（同 t153：合并 quad 四角各自 sunShadowAt → 影边光栅化平滑过渡）。
            //   贴图在合并 quad 上**拉伸**铺满（图集路径权衡：逐格平铺需纹理数组 = 自研 RHI 路径，已记录
            //   为推迟偏差 dev-plan 1/2 / PLAN §2-I）。greedyMeshing=false 可回退逐格 culled（清晰贴图）。
            struct MaskEntry { bool valid = false; int tile = 0; quint8 sky = 0; quint8 block = 0; };
            std::vector<MaskEntry> mask;
            // 各面 UV 轴映射（须与历史逐格 culled 的 cu/cv 规则一致：±X cu=Z,cv=Y；±Y cu=X,cv=Z；±Z cu=X,cv=Y）。
            static const int kUVAxes[6][2] = {
                {2, 1}, {2, 1}, // ±X
                {0, 2}, {0, 2}, // ±Y
                {0, 1}, {0, 1}, // ±Z
            };
            for (int f = 0; f < 6; ++f) {
                const FaceDef &F = kFaces[f];
                // 法线轴（normalAxis）+ 两 in-plane 轴（axisA 带 merge 宽 w、axisB 带 merge 高 h）。
                const int normalAxis = (F.dir[0] != 0) ? 0 : (F.dir[1] != 0) ? 1 : 2;
                const int axisA = (normalAxis + 1) % 3;
                const int axisB = (normalAxis + 2) % 3;
                const int sizeN = (normalAxis == 1) ? H : S; // 沿法线轴的层数
                const int sizeA = (axisA == 1) ? H : S;
                const int sizeB = (axisB == 1) ? H : S;
                if (sizeA <= 0 || sizeB <= 0 || sizeN <= 0) continue;
                mask.assign(sizeA * sizeB, MaskEntry{});

                for (int n = 0; n < sizeN; ++n) {
                    // 建 mask：逐 (a,b) 判本格（normalAxis 层 = n）在面 f 是否出可见面。
                    for (int a = 0; a < sizeA; ++a) {
                        for (int b = 0; b < sizeB; ++b) {
                            int lc[3] = {0, 0, 0};
                            lc[normalAxis] = n; lc[axisA] = a; lc[axisB] = b;
                            const int lx = lc[0], ly = lc[1], lz = lc[2];
                            const int wx = originX + lx, wz = originZ + lz;
                            MaskEntry &e = mask[a * sizeB + b];
                            e.valid = false;
                            const quint8 blk = blockAtWorld(wx, ly, wz);
                            if (blk == 0) continue;
                            const bool isWater = (blk == BlockRegistry::Water);
                            if (isWater != m_waterOnly) continue;          // 段分流（地形段跳水 / 水段跳非水）
                            if (!isWater && blk == BlockRegistry::Torch) continue;
                            if (!isWater && blk >= BlockRegistry::FirstPartial) continue; // 异形已在 PASS 1
                            const quint8 nb = blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (BlockRegistry::isSolid(nb)) continue;       // 邻居实体 → 剔除（跨 chunk 路由正确）
                            if (isWater && nb == BlockRegistry::Water) continue; // 水-水面互剔
                            const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                            e.valid = true;
                            e.tile = tileFor(blk, f);
                            e.sky = m_world->skyLightAt(ax, ay, az);
                            e.block = m_world->blockLightAt(ax, ay, az);
                        }
                    }
                    // 贪婪合并 (a,b) 平面 → 矩形（先沿 axisA 扩宽 w，再沿 axisB 扩高 h）。
                    for (int a = 0; a < sizeA; ++a) {
                        for (int b = 0; b < sizeB; ++b) {
                            const MaskEntry cur = mask[a * sizeB + b]; // 值拷贝（合并键固定；后续清格不影响）
                            if (!cur.valid) continue;
                            const int curTile = cur.tile;
                            const quint8 curSky = cur.sky, curBlock = cur.block;
                            auto same = [&](int aa, int bb) {
                                const MaskEntry &o = mask[aa * sizeB + bb];
                                return o.valid && o.tile == curTile && o.sky == curSky && o.block == curBlock;
                            };
                            int w = 1;
                            while (a + w < sizeA && same(a + w, b)) ++w;
                            int h = 1;
                            while (b + h < sizeB) {
                                bool ok = true;
                                for (int k = 0; k < w; ++k)
                                    if (!same(a + k, b + h)) { ok = false; break; }
                                if (!ok) break;
                                ++h;
                            }
                            // 发射矩形 [a,a+w) × [b,b+h) @ 层 n，面 f：4 顶点 + 2 三角。
                            const float u0 = curTile * tileW + hx, u1 = (curTile + 1) * tileW - hx;
                            const float nbSkyF = curSky / 15.0f;
                            const float nbBlockF = curBlock / 15.0f;
                            const int cuAxis = kUVAxes[f][0], cvAxis = kUVAxes[f][1];
                            const quint32 base = quint32(verts.size());
                            for (int cc = 0; cc < 4; ++cc) {
                                int cl[3];
                                cl[normalAxis] = n + int(F.c[cc][normalAxis]);             // 面贴 n 或 n+1 侧
                                cl[axisA] = a + (F.c[cc][axisA] ? w : 0);                  // in-plane A：起 or 起+宽
                                cl[axisB] = b + (F.c[cc][axisB] ? h : 0);                  // in-plane B：起 or 起+高
                                // per-vertex PCF 软影（世界位 = chunk 原点 + 局部角点；同 t153）。
                                const float shadow = sunShadowAt(float(originX + cl[0]),
                                                                 float(cl[1]),
                                                                 float(originZ + cl[2]));
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow), nbBlockF),
                                                            kVcMin, kVcMax);
                                // UV 在合并 quad 上拉伸铺满（cu/cv 取角点分量 0/1，纹理随几何 span 拉伸）。
                                const float cu = F.c[cc][cuAxis] ? 1.0f : 0.0f;
                                const float cv = F.c[cc][cvAxis] ? 1.0f : 0.0f;
                                Vtx v;
                                v.x = float(cl[0]); v.y = float(cl[1]); v.z = float(cl[2]); // chunk 局部坐标
                                v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                                v.u = u0 + cu * (u1 - u0);
                                v.v = v0 + cv * (v1 - v0);
                                v.r = vc; v.g = vc; v.b = vc; v.a = 1.0f;
                                verts.append(v);
                            }
                            idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
                            idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
                            // 清已合并格（防后续重叠发射）。
                            for (int da = 0; da < w; ++da)
                                for (int db = 0; db < h; ++db)
                                    mask[(a + da) * sizeB + (b + db)].valid = false;
                        }
                    }
                }
            }
        } else {
            // 逐格 culled meshing（fallback，greedyMeshing=false）：每可见面 4 顶点 + 6 索引（贴图逐格清晰）。
            for (int ly = 0; ly < H; ++ly) {
                for (int lz = 0; lz < S; ++lz) {
                    for (int lx = 0; lx < S; ++lx) {
                        const int wx = originX + lx, wz = originZ + lz;
                        const quint8 b = blockAtWorld(wx, ly, wz);
                        if (b == 0) continue;
                        const bool isWater = (b == BlockRegistry::Water);
                        if (isWater != m_waterOnly) continue;
                        if (!isWater && b == BlockRegistry::Torch) continue;
                        if (!isWater && b >= BlockRegistry::FirstPartial) continue; // 异形已在 PASS 1
                        for (int f = 0; f < 6; ++f) {
                            const FaceDef &F = kFaces[f];
                            const quint8 nb = blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (BlockRegistry::isSolid(nb)) continue;
                            if (isWater && nb == BlockRegistry::Water) continue;
                            const int t = tileFor(b, f);
                            const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;
                            const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                            const float nbSkyF = m_world->skyLightAt(ax, ay, az) / 15.0f;
                            const float nbBlockF = m_world->blockLightAt(ax, ay, az) / 15.0f;
                            const quint32 base = quint32(verts.size());
                            for (int cc = 0; cc < 4; ++cc) {
                                const float dx = F.c[cc][0], dy = F.c[cc][1], dz = F.c[cc][2];
                                const float shadow = sunShadowAt(float(wx) + dx, float(ly) + dy, float(wz) + dz);
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow), nbBlockF),
                                                            kVcMin, kVcMax);
                                float cu, cv;
                                if (f == 0 || f == 1) { cu = dz; cv = dy; }       // ±X
                                else if (f == 4 || f == 5) { cu = dx; cv = dy; }  // ±Z
                                else { cu = dx; cv = dz; }                        // ±Y
                                Vtx v;
                                v.x = float(lx) + dx; v.y = float(ly) + dy; v.z = float(lz) + dz; // 局部坐标
                                v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                                v.u = u0 + cu * (u1 - u0);
                                v.v = v0 + cv * (v1 - v0);
                                v.r = vc; v.g = vc; v.b = vc; v.a = 1.0f; // t151 光场 × t153 PCF 软影顶点色
                                verts.append(v);
                            }
                            idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
                            idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
                        }
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

    // t155g：不再在此清 dirty。旧版在此 clearDirty → 同 chunk 的 terrain/water 两段共享脏标记，
    //   先处理的段清掉后，后处理的段 onWorldChanged 见 dirty=false 跳过 → 那段 mesh 陈旧到下个 sun-step
    //   （= 用户「挖/放后贴图 2s 才刷新」根因）。现 dirty 由 World 在 emit worldChanged（两段都重建完）后
    //   经 ChunkManager::clearAllDirty() 统一清。buildMesh 只管重建，不清脏。

    // 可观测性（dev-spec t03 / t155 验收）：dirty = 编辑 / 初次加载即时重建（同步于 setBlock，破/放后当帧）；
    //   sun = 太阳跨步全量重建（绕 dirty，t155 编辑活跃期被 WorldClock 节流跳过）；water = 水段切换。
    //   读此日志可核对：破/放后立刻见 dirty 重建（无 3-4s 残留），编辑密集段无 sun 重建抢帧。
    static const char *const kReasonName[] = {"dirty", "sun", "water"};
    qInfo("vo.render: chunk(%d,%d) rebuilt [%s] - %lld verts / %lld idx (%lldus)",
          m_cx, m_cz, kReasonName[int(reason)], qint64(verts.size()), qint64(idx.size()), qint64(bt.nsecsElapsed() / 1000));

    // 通知 F3 叠层刷新（顶点 / 三角面数已更新；t10）。
    emit meshRebuilt();
}
