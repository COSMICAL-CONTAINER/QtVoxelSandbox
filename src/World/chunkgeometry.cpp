#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "partialblockgeometry.h" // t133：Vtx（chunk 顶点格式）+ PartialBlockGeometry 异形分支
#include "voxellight.h"           // t257：PCF 软影 + 光场顶点色单点实现（mesher 与 BlockCube 共用）
#include "world.h"

#include <QByteArray>
#include <QElapsedTimer> // t155f：buildMesh 计时（诊断编辑卡顿）
#include <QVector3D>

#include <algorithm> // std::clamp / std::max（t151 真光场顶点色钳制；PCF 软影的 sqrt/floor 已迁 voxellight.h）
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

// t153 PCF 软影调参与 t166 顶点色钳制（kSunMin/kSunFade/kMaxShadow/kVcMin/kVcMax）已迁至
//   voxellight.h（VoxelLight 命名空间）—— mesher 与 BlockCube（t257 掉落沙顶点色）共用同一实现，
//   杜绝「两处各持魔数、调参漂移」。见 VoxelLight::sunShadow / VoxelLight::vertexLight。

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

// t326：cutout 段开关变 → 重建（cutout 段只发 cross 顶点、地形段不再发 cross → 两段选块不同，需重网格化）。
//   值未变则早退。用 Dirty reason（同编辑即时重建路径，绕过 sun-step 节流，跨 chunk 边界 cross 随邻居破/放刷新）。
void ChunkGeometry::setCutoutOnly(bool on)
{
    if (m_cutoutOnly == on) return;
    m_cutoutOnly = on;
    emit cutoutOnlyChanged();
    buildMesh(RebuildReason::Dirty);
}

// t343：岩浆段开关变 → 重建（岩浆段只画 Lava、地形段跳 Lava → 两段选块不同，需重网格化）。值未变则早退。
//   用 Dirty reason（同编辑即时重建路径，绕过 sun-step 节流）。岩浆段复用 culled/greedy 立方面路径（满格立方 +
//   自剔 nb==Lava + 邻实体剔），不效仿水的变高水面（岩浆浓稠近不透、满格即可；流岩浆 state 仅驱动蔓延逻辑）。
void ChunkGeometry::setLavaOnly(bool on)
{
    if (m_lavaOnly == on) return;
    m_lavaOnly = on;
    emit lavaOnlyChanged();
    buildMesh(RebuildReason::Dirty);
}

// t405：玻璃段开关变 → 重建（玻璃段只画 Glass、地形段跳 Glass → 两段选块不同，需重网格化）。值未变则早退。
//   用 Dirty reason（同编辑即时重建路径，绕过 sun-step 节流）。玻璃段走 culled/greedy 立方面路径（满格立方 +
//   自剔 nb==Glass + 邻实体剔 + 邻空气画半透面）。玻璃非流体（无 state 液面），不走水的变高水面路径。
void ChunkGeometry::setGlassOnly(bool on)
{
    if (m_glassOnly == on) return;
    m_glassOnly = on;
    emit glassOnlyChanged();
    buildMesh(RebuildReason::Dirty);
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

// t223 水贴图动画 phase 变（flipbook 换帧）。仅水段（waterOnly=true）受影响：地形段不引用水 tile，
//   phase 变化对其顶点 UV 无影响 → 早退不重建（避免地形段无谓 buildMesh）。水段重网格化用 Water reason
//   （同 waterOnly 切换语义；绕过 sun-step 节流）。phase 钳到 0/1（两帧 flipbook；超界兜底取 0）。
void ChunkGeometry::setWaterAnimPhase(int phase)
{
    if (phase < 0 || phase > 1) phase = 0; // 钳到合法两帧范围
    if (m_waterAnimPhase == phase) return;
    m_waterAnimPhase = phase;
    emit waterAnimPhaseChanged();
    if (!m_waterOnly) return; // 地形段不引用水 tile → 无视觉影响，跳过重建
    buildMesh(RebuildReason::Water);
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
//   t225 箱子特例：前面（锁面 chest_front）所朝面由 state 决定（放置时朝玩家），其余三侧面 chest_side、
//   顶/底 chest_top。t234 耕地特例：顶面（+Y）恒 farmland_dry(26) 贴图，湿润等级（state 低 2 位）由顶点色
//   暗化体现（darker=wetter，见 buildMesh 内 farmlandHydrBrightMul）；其余面 = dirt（侧/底）。其余方块 state
//   inert（tileFor 退化为 stateless BlockRegistry::tileIndex）。
int ChunkGeometry::tileFor(quint8 block, int face, quint8 state) const
{
    // face: 0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z（须与 BlockRegistry::Face 一致）
    if (block == BlockRegistry::Chest) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（锁面）所朝面
        if (face == frontFace) return d.frontTile;                       // chest_front（锁面）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = chest_top
        return d.sideTile;                                               // 其余三侧面 = chest_side
    }
    // t234/t406 耕地：顶面（+Y）恒 farmland_dry(26)（topTile）；湿润等级 0..3（state 低 2 位）由顶点色暗化
    //   体现（darker=wetter，见 buildMesh 内 farmlandHydrBrightMul），不再切换 dry/wet 两贴图（4 级靠顶点色
    //   连续暗化实现，无需扩图集 + 2 贴图）。侧/底 = dirt(2)。
    if (block == BlockRegistry::Farmland) {
        if (face == int(BlockRegistry::Top))
            return BlockRegistry::def(block).topTile;     // farmland_dry(26) —— 湿润由顶点色暗化体现
        return BlockRegistry::def(block).sideTile;        // 侧/底 = dirt(2)
    }
    return BlockRegistry::tileIndex(block, BlockRegistry::Face(face));
}

// t406 耕地湿润度 → +Y 顶面顶点色暗化系数（darker=wetter；机制等价 MC 耕地越湿顶面越深）。
//   level 0（干）×1.00 不暗化 → 浅色 farmland_dry 贴图本色；level 3（最湿，邻水 dist 1）×0.46 → 明显深暗。
//   仅作用于顶点色（vc 已含天光/方光/软影）→ 既保光照信息又叠加湿润暗化。湿等级进 greedy 合并键
//   （MaskEntry.hydr）→ 不同湿润度的耕地顶面不共面误并（各保留各自暗化）。
float ChunkGeometry::farmlandHydrBrightMul(quint8 hydr) const
{
    static constexpr float kTbl[4] = { 1.00f, 0.82f, 0.64f, 0.46f };
    return (hydr <= BlockRegistry::FarmlandHydrationMax) ? kTbl[hydr] : kTbl[0];
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
//   t257：实现迁至 voxellight.h 的 VoxelLight::sunShadow（mesher 与 BlockCube 掉落沙共用）；本方法
//   仅注入本几何的 world/sunDir/shadowsEnabled 后委托，行为与历史逐字等价（机械抽取，零语义变化）。
float ChunkGeometry::sunShadowAt(float wx, float wy, float wz) const
{
    return VoxelLight::sunShadow(m_world, m_sunDir, m_shadowsEnabled, wx, wy, wz);
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
    // 半纹素内缩防渗色（线性采样跨瓦片）。N 读 BlockRegistry::AtlasTileCount（单一权威，
    //   与 BlockCube / build_atlas.py 同源——消除「两处各持魔数、加瓦片漏改一份」回归类，见 t182）。
    constexpr int N = BlockRegistry::AtlasTileCount;
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
    //   kVcMin / kVcMax 取自 voxellight.h（VoxelLight::kVcMin/kVcMax）—— mesher 与 BlockCube（t257 掉落沙）
    //   共用同一顶点色钳制曲线，保证「掉落沙与地形同亮度」（修暗处挖底沙变亮根因）。
    constexpr float kVcMin = VoxelLight::kVcMin; // 暗部地板最低亮度（洞穴/阴影最低，仍远低于火把光池 0.93 保持对比）
    constexpr float kVcMax = VoxelLight::kVcMax;

    if (c && m_world) {
        // ---- PASS 1：不完整方块（异形）合批进同一 chunk mesh（t133 PartialBlockGeometry）----
        //   **terrain 段独有**（水段无 partial；waterOnly 守卫防水段 ChunkGeometry 重复渲染异形方块）。
        //   每 cell 仅一次 append。独立于 PASS 2 的面 mask——否则 6 面 mask 各扫一次会 6× 重复 append。
        //   torch / 整立方 / 水不进此 pass。光照上下文（cellLight）按本格光场 + 本格中心 PCF 软影算
        //   （同 t151/t153 异形约定），打包进 PartialLightCtx 传入。
        if (!m_waterOnly && !m_lavaOnly && !m_glassOnly) for (int ly = 0; ly < H; ++ly) { // t343/t405：岩浆/玻璃段只画流体/玻璃立方面，跳过 partial/cross（PASS 1）
            for (int lz = 0; lz < S; ++lz) {
                for (int lx = 0; lx < S; ++lx) {
                    const int wx = originX + lx, wz = originZ + lz;
                    const quint8 b = blockAtWorld(wx, ly, wz);
                    if (b == 0) continue;
                    if (b == BlockRegistry::Water) continue;       // 水走 PASS 2 立方面（水段）
                    if (b == BlockRegistry::Lava) continue;        // t343 岩浆走 PASS 2 立方面（岩浆段，独立材质）
                    if (b == BlockRegistry::Torch) continue;       // 火把走 torchHost（QML Model）
                    // t194：必须闭区间 [FirstPartial, LastPartial]。段后整立方（Chest=22）虽 id 更大但非异形
                    //   （ShapeFull，走 PASS 2 立方面）。旧单边 `b >= FirstPartial` 把 Chest 误路由进 PartialBlockGeometry
                    //   （switch 无 case → 0 顶点 → 放置后透明透视格子）。Water/Torch 在上方已显式 continue。
                    // t235：cross 广告牌方块段 [FirstCross, LastCross]（草丛）亦进此 pass（pushCross 生成对角双面
                    //   quad）。与 partial 盒体段并列、闭区间判定（同 t194 教训）。
                    // t305：cross 路由改用 isCrossBillboard 谓词（连续段 ∪ {Sapling}）—— Sapling(28) id 不在
                    //   [FirstCross,LastCross]=[24,25] 连续段内（DiamondOre/Wool 夹中间且非 cross），故并入谓词。
                    // t412：partial 路由改用 isPartialBlock 谓词（[FirstPartial,LastPartial] ∪ 段外圆石变体）——
                    //   CobbleSlab/Stairs/Fence/PressurePlate id(58..61) 不在连续段内（中间夹大量非异形方块），
                    //   故并入谓词（同 isCrossBillboard 段外 cross 模式）。
                    const bool isPartialX = BlockRegistry::isPartialBlock(b)
                                            || b == BlockRegistry::Farmland; // t408 耕地矮盒经 PartialBlockGeometry 渲染（露 1/16 唇）
                    const bool isCrossX   = BlockRegistry::isCrossBillboard(b);
                    // t326 cross cutout 分流：cross 方块（草丛/作物/树苗）贴图带 alpha 透明底 → 进独立 cutout 段
                    //   （半透材质 opacity:0.99 + alphaCutoff:0.5 cutout 透明间隙）；partial 盒体（slab/stairs/...）
                    //   贴图不透明 → 留地形段（不透明材质 opacity=1）。两段互斥：地形段若同时发 cross → opacity=1
                    //   下 alpha 被忽略、透明底当不透明显成实心板（用户「草丛挡视线」根因）。cutout 段只发 cross。
                    if (m_cutoutOnly) {
                        if (!isCrossX) continue;     // cutout 段：仅 cross（草丛/作物/树苗）
                    } else {
                        if (isCrossX) continue;      // 地形段：cross 走 cutout 段、不在此画（否则显实心板）
                        if (!isPartialX) continue;   // 地形段：仅 partial 盒体（立方面在 PASS 2）
                    }
                    const quint8 cSky = m_world->skyLightAt(wx, ly, wz);
                    const quint8 cBlock = m_world->blockLightAt(wx, ly, wz);
                    const float cShadow = sunShadowAt(float(wx) + 0.5f, float(ly) + 0.5f, float(wz) + 0.5f);
                    const float cellLight = std::clamp(
                        std::max((cSky / 15.0f) * (1.0f - cShadow), cBlock / 15.0f), kVcMin, kVcMax);
                    const quint8 st = stateAtWorld(wx, ly, wz);
                    // t360 异形方块光照：cross 用本格光场（cellLight）；pushBox 各面用「面所朝邻格」flood 光
                    //   （修合活版门/下半砖顶面自影：旧采被本格 lightOpacity 压暗的本格值）。6 向邻格 sky/block
                    //   + 各面代表点（面中心世界位）PCF → clamp(max(sky*(1-sh), block))。顺序同 kBoxFaces
                    //   [+X,-X,+Y,-Y,+Z,-Z]，与 partialblockgeometry pushBox 取 face[fi] 一一对应。
                    struct FaceSrc { int dx, dy, dz; float px, py, pz; };
                    const FaceSrc fs[6] = {
                        { 1, 0, 0, float(wx + 1),     float(ly) + 0.5f, float(wz) + 0.5f}, // +X
                        {-1, 0, 0, float(wx),         float(ly) + 0.5f, float(wz) + 0.5f}, // -X
                        { 0, 1, 0, float(wx) + 0.5f,  float(ly + 1),   float(wz) + 0.5f}, // +Y
                        { 0,-1, 0, float(wx) + 0.5f,  float(ly),       float(wz) + 0.5f}, // -Y
                        { 0, 0, 1, float(wx) + 0.5f,  float(ly) + 0.5f, float(wz + 1)},   // +Z
                        { 0, 0,-1, float(wx) + 0.5f,  float(ly) + 0.5f, float(wz)},       // -Z
                    };
                    PartialLightCtx lctx;
                    lctx.light = cellLight;
                    for (int i = 0; i < 6; ++i) {
                        const int nx = wx + fs[i].dx, ny = ly + fs[i].dy, nz = wz + fs[i].dz;
                        const float nbSkyF = m_world->skyLightAt(nx, ny, nz) / 15.0f;
                        const float nbBlockF = m_world->blockLightAt(nx, ny, nz) / 15.0f;
                        const float sh = sunShadowAt(fs[i].px, fs[i].py, fs[i].pz);
                        lctx.face[i] = std::clamp(std::max(nbSkyF * (1.0f - sh), nbBlockF), kVcMin, kVcMax);
                    }
                    // t408 耕地从 PASS 2 立方面迁到 PASS 1 矮盒（PartialBlockGeometry），原 PASS 2 顶面湿润暗化
                    //   （farmlandHydrBrightMul，darker=wetter）改在此预乘 lctx.face[+Y(=2)]，使矮盒顶面顶点色仍随
                    //   state 低 2 位湿润等级渐暗（机制不变，仅消费点迁移）。
                    if (b == BlockRegistry::Farmland)
                        lctx.face[2] *= farmlandHydrBrightMul(quint8(st & BlockRegistry::FarmlandHydrationMask));
                    // t209 栅栏连接：查 4 向水平邻居 id（跨 chunk 经 blockAtWorld 路由，边界邻居正确）。
                    //   仅 fence 读本上下文；其余异形方块忽略。边界格破/放已标邻 chunk 脏（ChunkManager::setBlock
                    //   在 lx/lz 贴边时标邻接脏）→ 跨 chunk 栅栏连接随邻居重网格化自动更新。
                    const PartialNeighborCtx nctx{
                        blockAtWorld(wx + 1, ly, wz),
                        blockAtWorld(wx - 1, ly, wz),
                        blockAtWorld(wx, ly, wz + 1),
                        blockAtWorld(wx, ly, wz - 1),
                    };
                    PartialBlockGeometry::append(verts, idx, lx, ly, lz, b, st,
                                                 lctx, nctx, tileW, hx, hy, v0, v1);
                }
            }
        }

        // ---- PASS 2：立方面网格化（terrain 段 culled/greedy；水段 t197 变高水面专用路径）----
        //   t326：cutout 段（cutoutOnly）只发 cross 顶点（PASS 1），无水 / 无整立方面 → 跳过 PASS 2。
        if (m_cutoutOnly) {
            // cutout 段：cross 仅在 PASS 1（pushCross 双面 quad）；无立方面、无水。空分支，跳过下方三路。
        } else if (m_waterOnly || m_lavaOnly) {
            // t197 水位视觉 / t351 岩浆分层视觉：流体段（水 / 岩浆）不再画满格立方，而是按 cell 的 state(level)
            //   降液面高度 + 流体用独立贴图，呈现 MC 式逐格衰减流动（修「所有流体格同高满液位 → 看着静止/全平」）。
            //   t351：岩浆段复用此变高路径（旧岩浆段走 culled/greedy 满格立方 → 岩浆面全平，无分层；现 lavaOnly
            //   进本分支按 state 降液面，机制等价水的分层流动）。参数差异由下方 fluidId/maxLevel/各 tile 区分。
            //
            //   水面高度：水源(state=0) 满高 1.0（用 water=19 静水贴图）；流水(state=1..7) 水面 = (8-level)/8
            //   逐级降（用 water_flow=23 流水贴图）。机制等价 MC 1.0 流水 8 级衰减（机制对齐，非精确数值复刻）。
            //
            //   面剔除（关键：水位感知，决定本格水面与邻居水面间的暴露带 / 瀑布阶梯）：
            //     · 顶面(+Y)：上方为空气 → 画在 myTop（水面）；上方为实体/水 → 剔除。
            //     · 底面(-Y)：下方为空气 → 画在 0；下方为实体/水 → 剔除（流水悬空下方可见底）。
            //     · 侧面(±X/±Z)：邻实体 → 剔除（地形挡）；邻空气 → 画本格满侧（自 0 至 myTop）；
            //       邻水 → 比较水面：邻居水面 >= 本格 → 整面剔除；邻居水面 < 本格 → 画本格水面到邻居水面
            //       之间的暴露垂直带（瀑布阶梯感 —— 两格水面落差处露出高格的侧壁）。
            //   顶点色光照沿用 t151 真光场 + t153 PCF 软影（同 terrain culled 约定：邻格 sky/block + per-vertex 软影）。
            //
            //   分层（PLAN §2）：本段属 Renderer（mesher），只读 World（blockAt/stateAt/skyLightAt/blockLightAt/
            //   heightmapAt）—— 水面高度是 mesher 据 state 的纯渲染层计算，不写栅格、不进 BlockDef（Water 方块
            //   def 仍各面=19 静水；流水贴图选择是呈现层决定，非方块属性）。
            // t351 流体参数化（水 / 岩浆共用此变高路径，差异仅 id / 贴图 / 最大 level）：
            //   fluidId：本段画哪种流体（水段=Water / 岩浆段=Lava）。
            //   stillTile/flowTile：水源/岩浆源用静贴图，流水/流岩浆用流贴图。水有 flipbook 两帧 + 独立 flow 贴图(23/25)；
            //     岩浆仅 tile 42（无独立 flow 贴图）→ 源 / 流均用 42（分层高度差异已足够表达衰减，机制等价 MC 岩浆分层）。
            //   maxLevel：液面衰减分级。水 8 级（state 1..7 → 液面 (8-s)/8）；岩浆 4 级（state 1..3 → 液面 (4-s)/4）。
            const quint8 fluidId  = m_lavaOnly ? BlockRegistry::Lava  : BlockRegistry::Water;
            const int maxLevel    = m_lavaOnly ? 4 : 8;                 // 源(0) + 流(1..maxLevel-1)
            const int stillTile   = m_lavaOnly ? 42 : (m_waterAnimPhase == 0 ? 19 : 24);
            const int flowTile    = m_lavaOnly ? 42 : (m_waterAnimPhase == 0 ? 23 : 25);
            // state → 液面高度（cell-local Y，0..1）。源 1.0；流 (maxLevel-level)/maxLevel（越远越矮）。
            //   state 越界 clamp 兜底防负值（极端坏数据 → 最低一级液面而非负值）。
            auto surfH = [maxLevel](quint8 state) -> float {
                if (state == 0) return 1.0f;
                const int s = (int(state) > maxLevel - 1) ? (maxLevel - 1) : int(state);
                return (float(maxLevel) - float(s)) / float(maxLevel);
            };
            // t350 renderTop：流体格的**实际渲染**顶高（含竖向柱连续性修正）。源(st==0)=1.0；
            //   流(st>0) 的 slab 高 = surfH(state)，但若**正上方为同种流体**（竖向柱 / 下落流的中段）→ 1.0（满块）。
            //   修「竖向堆叠流格间露出空气带」：流 slab 仅占 cell 下部，上方留空；两流格上下堆叠时，下格侧壁止于
            //   其 slab 顶、上格侧壁起于本 cell 底 → slab 顶与本 cell 底之间一段无侧壁 → 透视见空气带。被上方同种
            //   流体覆盖的流格属柱内 → 渲染满高，侧壁贯通相邻格 → 柱连续无缝。顶格（上方 air）保 slab 液面高。
            //   blockAtWorld 越界返 Air → 顶格不触发满高修正。t351：流体判定由硬编码 Water 改为 fluidId（水 / 岩浆通用）。
            auto renderTop = [&](quint8 state, int ax, int ay, int az) -> float {
                if (state == 0) return 1.0f;
                if (blockAtWorld(ax, ay + 1, az) == fluidId) return 1.0f; // 上方有同种流体 → 柱内满块
                return surfH(state);
            };
            for (int ly = 0; ly < H; ++ly) {
                for (int lz = 0; lz < S; ++lz) {
                    for (int lx = 0; lx < S; ++lx) {
                        const int wx = originX + lx, wz = originZ + lz;
                        if (blockAtWorld(wx, ly, wz) != fluidId) continue;
                        const quint8 st = stateAtWorld(wx, ly, wz);
                        const float myTop = renderTop(st, wx, ly, wz); // t350：上方有水 → 满高（柱连续无缝）
                        const int tile = (st == 0) ? stillTile : flowTile; // 水源静水 / 流水斜纹（t223 按 phase 选帧）
                        const float u0 = tile * tileW + hx, u1 = (tile + 1) * tileW - hx;
                        for (int f = 0; f < 6; ++f) {
                            const FaceDef &F = kFaces[f];
                            const int nwx = wx + F.dir[0], nwy = ly + F.dir[1], nwz = wz + F.dir[2];
                            const quint8 nb = blockAtWorld(nwx, nwy, nwz);
                            // 决定本面是否画 + 画的垂直区间 [yLo, yHi]（cell-local）。
                            //   水平面(±Y)：yLo=yHi（单层）；侧面(±X/±Z)：[yLo,yHi] 可能是部分带。
                            float yLo = 0.0f, yHi = myTop;
                            if (F.dir[1] != 0) {
                                // 顶/底面：邻(上/下)为实体或水 → 剔除；为空气 → 画在水面 / 底。
                                if (BlockRegistry::isSolid(nb)) continue;
                                if (nb == fluidId) continue;
                                yLo = yHi = (F.dir[1] > 0) ? myTop : 0.0f; // 顶在 myTop / 底在 0
                            } else {
                                // 侧面：邻实体剔除；邻空气画满侧 [0,myTop]；邻水按水面差画暴露带。
                                // t222：流水（state>0，降水面 myTop<1）邻实体方块时**不整面剔除**——画 [0,myTop]
                                //   满侧保持流水贴图可见。修「流水格被占（玩家放方块 / 自然地形邻接）→水面贴图
                                //   消失/透明、透视见底」：透明水材质（opacity 0.7）下侧壁封闭水体体积，从水面斜
                                //   透视不再穿透到背后的实体方块 / 水底（水体「满」而非「空壳」），机制等价 MC
                                //   流水贴着实体方块显侧壁。流水格被占（t198 setBlock 覆盖水→实体）后邻接流水 N
                                //   朝新实体面不再被 `isSolid→continue` 抹掉其 water_flow 侧壁贴图。
                                //   水源（state=0，满高 1.0）邻实体仍**剔除**：满格实体完全遮挡，画了只在实体面上
                                //   叠一层半透水色（z-fight / 渗色观感），无视觉收益且会把所有水-地形接缝染蓝。
                                if (BlockRegistry::isSolid(nb)) {
                                    if (st == 0) continue; // 水源满高：邻实体完全遮挡 → 剔除（原行为）
                                    // 流水降水面：画 [0,myTop] 满侧（yLo=0,yHi=myTop 已是默认）保持贴图可见
                                } else if (nb == fluidId) {
                                    const float nbrTop = renderTop(stateAtWorld(nwx, nwy, nwz), nwx, nwy, nwz);
                                    if (nbrTop >= myTop - 1e-4f) continue;      // 邻居水面 >= 本格 → 整面剔除
                                    yLo = nbrTop;                                // 邻居更低 → 画邻居水面到本格水面间暴露带
                                    yHi = myTop;
                                } // else 邻空气：yLo=0, yHi=myTop（满侧）
                            }
                            // 光照（同 terrain culled：面所朝邻格的天光/方光 + per-vertex PCF 软影）。
                            const float nbSkyF = m_world->skyLightAt(nwx, nwy, nwz) / 15.0f;
                            const float nbBlockF = m_world->blockLightAt(nwx, nwy, nwz) / 15.0f;
                            const quint32 base = quint32(verts.size());
                            for (int cc = 0; cc < 4; ++cc) {
                                const float dx = F.c[cc][0], dy = F.c[cc][1], dz = F.c[cc][2];
                                // 顶点 Y：水平面四角同高（yHi）；侧面按角点 dy(0/1) 映射到 [yLo,yHi]。
                                const float yy = (F.dir[1] != 0) ? yHi : (dy ? yHi : yLo);
                                const float shadow = sunShadowAt(float(wx) + dx, float(ly) + yy, float(wz) + dz);
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow), nbBlockF),
                                                            kVcMin, kVcMax);
                                // UV：同 terrain culled 规则（±X cu=dz,cv=dy；±Z cu=dx,cv=dy；±Y cu=dx,cv=dz）。
                                //   侧面 cv=dy(0/1) → 贴图垂直方向 0..1 映射到 [yLo,yHi] 区间（部分带/矮水面
                                //   会让贴图竖向压缩/拉伸，图集 CLAMP 无法 REPEAT —— 已知图集路径权衡，见
                                //   lessons-learned greedy meshing 条）。
                                float cu, cv;
                                if (f == 0 || f == 1) { cu = dz; cv = dy; }       // ±X
                                else if (f == 4 || f == 5) { cu = dx; cv = dy; }  // ±Z
                                else { cu = dx; cv = dz; }                        // ±Y
                                // t391 水面波动/透明度润色（spec「水面有波动质感、非死板」）：仅水段顶面（+Y，f==2）
                                //   叠加一层随 waterAnimPhase 翻转的**空间正弦涟漪**——每顶点据世界角点 (wx+dx, wz+dz)
                                //   算 sin（k=1.1，周期 ~5.7 格，对角涟漪）；相邻 cell 共享同一角点 → 涟漪跨格连续
                                //   （非逐格跳变）。phase 0/1 在 sin 自变量上 ±π 偏移（半周期）→ flipbook 切帧时亮/暗
                                //   波带整体互换 = 阳光在水面细碎反光的「闪烁」感，叠加 t223 贴图 flipbook → 水面微动、
                                //   非全平死板。仅水（fluidId==Water，!m_lavaOnly）；岩浆段不参与（浓稠近不透、无涟漪语义）。
                                //   亮度 ±12%（反光起伏）+ vertex.a [0.85,1.0]（材质 opacity 0.7 × vertex.a → 有效
                                //   alpha [0.595,0.7]，透射起伏）；侧面/底面 brightMul=alphaMul=1（原行为）。
                                float brightMul = 1.0f, alphaMul = 1.0f;
                                if (!m_lavaOnly && f == 2) { // +Y 顶面（水面）
                                    const float sarg = float(wx + dx + wz + dz) * 1.1f;
                                    const float wave = std::sin(sarg + (m_waterAnimPhase != 0 ? 3.14159265f : 0.0f));
                                    brightMul = 1.0f + 0.12f * wave;                 // [0.88, 1.12] 反光起伏
                                    alphaMul = 0.85f + 0.15f * (0.5f + 0.5f * wave);   // [0.85, 1.00] 透射起伏
                                }
                                Vtx v;
                                v.x = float(lx) + dx; v.y = float(ly) + yy; v.z = float(lz) + dz; // 局部坐标
                                v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                                v.u = u0 + cu * (u1 - u0);
                                v.v = v0 + cv * (v1 - v0);
                                // t151 光场 × t153 PCF 软影顶点色；t391 水面顶面（+Y）再乘涟漪亮/透射因子。
                                v.r = vc * brightMul; v.g = vc * brightMul; v.b = vc * brightMul;
                                v.a = alphaMul; // 侧面/底面 = 1.0；水面顶面 = [0.85,1.0] 涟漪透射
                                verts.append(v);
                            }
                            idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
                            idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
                        }
                    }
                }
            }
        } else if (m_greedyMeshing) {
            // t178 贪婪网格化（greedy meshing，PLAN §4 性能打磨）：按 6 面方向逐「层」建 2D mask，合并同
            //   (tile, 邻格天光, 邻格方光) 的共面连续格为单个矩形 → 顶点 / 三角 / 索引数大幅下降（平坦地面
            //   16×16=256 quad → 1 quad）。F3 叠层据此可观测 meshing 吞吐改善（PLAN §2-F）。
            //
            //   合并键含光照（tile + 邻格 sky + 邻格 block）→ 仅**均匀照明**区合并（保光照保真：合并 quad
            //   四角共享同一邻格光值，内部不被误暗；火把 / 墙边光照变化处不合并，贴图也保持逐格清晰）。
            //   PCF 软影仍 per-vertex（同 t153：合并 quad 四角各自 sunShadowAt → 影边光栅化平滑过渡）。
            //   贴图在合并 quad 上**拉伸**铺满（图集路径权衡：逐格平铺需纹理数组 = 自研 RHI 路径，已记录
            //   为推迟偏差 dev-plan 1/2 / PLAN §2-I）。greedyMeshing=false 可回退逐格 culled（清晰贴图）。
            struct MaskEntry { bool valid = false; int tile = 0; quint8 sky = 0; quint8 block = 0; quint8 hydr = 0; }; // t406 hydr = Farmland +Y 顶面湿润等级（进合并键防不同湿润度共面误并）
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
                            const bool isLava  = (blk == BlockRegistry::Lava);
                            const bool isGlass = (blk == BlockRegistry::Glass);
                            // t343/t405 段分流：岩浆段只画 Lava；水段只画 Water；玻璃段只画 Glass；地形段跳过流体+玻璃（各自独立段渲染）。
                            if (m_glassOnly) { if (!isGlass) continue; }
                            else if (m_lavaOnly) { if (!isLava) continue; }
                            else if (m_waterOnly) { if (!isWater) continue; }
                            else { if (isWater || isLava || isGlass) continue; }       // 地形段跳流体 + 玻璃
                            if (!isWater && !isLava && !isGlass && blk == BlockRegistry::Torch) continue;
                            if (!isWater && !isLava && !isGlass && BlockRegistry::isPartialBlock(blk)) continue; // t412 异形已在 PASS 1（含段外圆石变体）；段后整立方（Chest）正常进立方面
                            if (!isWater && !isLava && !isGlass && blk == BlockRegistry::Farmland) continue; // t408 耕地矮盒已在 PASS 1；不进整立方面（否则满格立方覆盖矮盒唇）
                            if (!isWater && !isLava && !isGlass && BlockRegistry::isCrossBillboard(blk)) continue; // t235/t305 cross（草丛/作物/树苗）已在 PASS 1；不进立方面
                            const quint8 nb = blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (BlockRegistry::isSolid(nb)) continue;       // 邻居实体 → 剔除（跨 chunk 路由正确）
                            if (isWater && nb == BlockRegistry::Water) continue; // 水-水面互剔
                            if (isLava && nb == BlockRegistry::Lava) continue;   // t343 岩浆-岩浆面互剔
                            // t405 玻璃-玻璃面互剔（Glass solid=false → isSolid(nb) 不剔除玻璃邻；显式剔除避免两玻璃共面重复绘制）。
                            if (isGlass && nb == BlockRegistry::Glass) continue;
                            const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                            e.valid = true;
                            // t225 箱子前面朝向由 state 决定（其余方块 state inert）→ mask tile 含 state，
                            //   不同朝向的相邻箱子在前侧面自然不合并（侧/顶/底面仍同 tile 可合并）。
                            const quint8 st = stateAtWorld(wx, ly, wz);
                            e.tile = tileFor(blk, f, st);
                            e.sky = m_world->skyLightAt(ax, ay, az);
                            e.block = m_world->blockLightAt(ax, ay, az);
                            // t406 耕地湿润等级进合并键：仅 Farmland +Y 顶面带等级（侧/底 + 非耕地 = 0），
                            //   → 不同湿润度的耕地顶面不共面误并（各保留各自顶点色暗化，darker=wetter 清晰可辨）。
                            e.hydr = (blk == BlockRegistry::Farmland && f == int(BlockRegistry::Top))
                                     ? quint8(st & BlockRegistry::FarmlandHydrationMask) : quint8(0);
                        }
                    }
                    // 贪婪合并 (a,b) 平面 → 矩形（先沿 axisA 扩宽 w，再沿 axisB 扩高 h）。
                    for (int a = 0; a < sizeA; ++a) {
                        for (int b = 0; b < sizeB; ++b) {
                            const MaskEntry cur = mask[a * sizeB + b]; // 值拷贝（合并键固定；后续清格不影响）
                            if (!cur.valid) continue;
                            const int curTile = cur.tile;
                            const quint8 curSky = cur.sky, curBlock = cur.block;
                            const quint8 curHydr = cur.hydr; // t406 耕地湿润等级（进合并键）
                            auto same = [&](int aa, int bb) {
                                const MaskEntry &o = mask[aa * sizeB + bb];
                                return o.valid && o.tile == curTile && o.sky == curSky
                                       && o.block == curBlock && o.hydr == curHydr;
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
                            // t406 耕地 +Y 顶面湿润暗化（curHydr>0 仅耕地顶面；其余面 / 非耕地 = 1.0 不影响）。
                            const float brightMul = farmlandHydrBrightMul(curHydr);
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
                                v.r = vc * brightMul; v.g = vc * brightMul; v.b = vc * brightMul; v.a = 1.0f; // t406 brightMul = 耕地湿润暗化（非耕地 = 1.0）
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
                        const bool isLava  = (b == BlockRegistry::Lava);
                        const bool isGlass = (b == BlockRegistry::Glass);
                        // t343/t405 段分流：岩浆段只画 Lava；水段只画 Water；玻璃段只画 Glass；地形段跳过流体+玻璃（各自独立段渲染）。
                        if (m_glassOnly) { if (!isGlass) continue; }
                        else if (m_lavaOnly) { if (!isLava) continue; }
                        else if (m_waterOnly) { if (!isWater) continue; }
                        else { if (isWater || isLava || isGlass) continue; }
                        if (!isWater && !isLava && !isGlass && b == BlockRegistry::Torch) continue;
                        if (!isWater && !isLava && !isGlass && BlockRegistry::isPartialBlock(b)) continue; // t412 异形已在 PASS 1（含段外圆石变体）；段后整立方（Chest）正常进立方面
                        if (!isWater && !isLava && !isGlass && b == BlockRegistry::Farmland) continue; // t408 耕地矮盒已在 PASS 1；不进整立方面
                        if (!isWater && !isLava && !isGlass && BlockRegistry::isCrossBillboard(b)) continue; // t235/t305 cross（草丛/作物/树苗）已在 PASS 1；不进立方面
                        for (int f = 0; f < 6; ++f) {
                            const FaceDef &F = kFaces[f];
                            const quint8 nb = blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (BlockRegistry::isSolid(nb)) continue;
                            if (isWater && nb == BlockRegistry::Water) continue;
                            if (isLava && nb == BlockRegistry::Lava) continue;
                            // t405 玻璃-玻璃面互剔（Glass solid=false → isSolid(nb) 不剔除玻璃邻；显式剔除避免两玻璃共面重复绘制）。
                            if (isGlass && nb == BlockRegistry::Glass) continue;
                            const quint8 st = stateAtWorld(wx, ly, wz); // t225/t406 箱子前面朝向 / 耕地湿润由 state 决定
                            const int t = tileFor(b, f, st); // t225 箱子前面朝向由 state 决定
                            const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;
                            const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                            const float nbSkyF = m_world->skyLightAt(ax, ay, az) / 15.0f;
                            const float nbBlockF = m_world->blockLightAt(ax, ay, az) / 15.0f;
                            // t406 耕地 +Y 顶面湿润暗化（darker=wetter；仅 Farmland 顶面带等级，其余 = 1.0）。
                            const float brightMul = (b == BlockRegistry::Farmland && f == int(BlockRegistry::Top))
                                ? farmlandHydrBrightMul(quint8(st & BlockRegistry::FarmlandHydrationMask)) : 1.0f;
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
                                v.r = vc * brightMul; v.g = vc * brightMul; v.b = vc * brightMul; v.a = 1.0f; // t151 光场 × t153 PCF 软影顶点色 × t406 耕地湿润暗化（非耕地 brightMul=1.0）
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
