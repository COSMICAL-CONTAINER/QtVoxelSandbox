#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "partialblockgeometry.h" // t133：Vtx（chunk 顶点格式）+ PartialBlockGeometry 异形分支
#include "world.h"

#include <QByteArray>
#include <QVector3D>

#include <algorithm> // std::clamp / std::max（t151 真光场顶点色钳制）
#include <cmath>     // std::sqrt / std::floor（t153 PCF 软影 sunShadowAt）
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

    // t151 真光场 + t153 PCF 软影顶点色（PLAN §2-H / §M，替代 t123 方向太阳 faceVc）：
    //   光场基底 = 邻格（面所朝向的空气格）的 max(sky, block)/15，由 World 的 BFS flood-fill 算出（存 chunk
    //   第三数组），mesher 只读采样。t153 在此基底上叠 PCF 软影：天光分量再乘 (1 - sunShadowAt)，把「太阳
    //   被邻近高地遮挡」处压暗（heightmap 正交深度图沿 sunDir 步进、2×2 PCF 软过渡）；火把方光（block）
    //   不受影（取 max 保留）。昼夜乘子仍由 QML baseColor 承担（terrainLight 平滑 lerp），故光场本身时间
    //   不变 —— 影因子仅随 sunDir（量化跨步）变 → 绑 sunChanged 重建（sunShadowAt 见上方）。
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
                    //   t153 PCF 软影：异形方块近似取本格中心采样（per-face 子格尺度歧义，统一一格一影），
                    //   shadow 折进 cellLight 一次性传 PartialBlockGeometry（天光分量被压暗、方光取 max 保留）。
                    const quint8 cSky = m_world->skyLightAt(wx, ly, wz);
                    const quint8 cBlock = m_world->blockLightAt(wx, ly, wz);
                    const float cShadow = sunShadowAt(float(wx) + 0.5f, float(ly) + 0.5f, float(wz) + 0.5f);
                    const float cellLight = std::clamp(
                        std::max((cSky / 15.0f) * (1.0f - cShadow), cBlock / 15.0f), kVcMin, kVcMax);

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

                        // t151 真光场 + t153 PCF 软影顶点色：本面照明 = 邻格（面所朝向空气格）光场
                        //   max(sky,block)/15；天光分量再被 PCF 软影（sunShadowAt）压暗 —— 太阳被邻近高地
                        //   遮挡处变暗、火把方光（block）取 max 保留（洞穴火把仍亮）。shadow 按各顶点世界位
                        //   现算 → 同一面四角可不同 → 光栅化插值得影边软过渡（叠加 PCF 2×2 邻列平均）。
                        //   越界 y>=height 视作开阔天光 15（顶面采样）。clamped [kVcMin, kVcMax]。
                        const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                        const float nbSkyF = m_world->skyLightAt(ax, ay, az) / 15.0f;
                        const float nbBlockF = m_world->blockLightAt(ax, ay, az) / 15.0f;

                        const quint32 base = quint32(verts.size());
                        for (int cc = 0; cc < 4; ++cc) {
                            const float dx = F.c[cc][0], dy = F.c[cc][1], dz = F.c[cc][2];
                            // t153：per-vertex PCF 软影（世界位 = chunk 世界原点 + 格 + 面角偏移）。
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

    // t155 清脏语义解耦：仅当本次重建响应了待清的编辑脏（c->dirty()==true）时才清。编辑路径
    //   （onWorldChanged，dirty-gated）恒命中 → 清除；太阳刷新（setSunDir→buildMesh(Sun)）见到的 chunk
    //   永远非脏（编辑的 onWorldChanged 已在 setBlock 栈内同步清过）→ 不命中 → 不清。这让「编辑脏标记
    //   只被真正响应编辑的重建清除」，immediate rebuild 对任何太阳时序稳健（破 / 放后不依赖下个太阳步进）。
    if (c && c->dirty()) c->clearDirty();

    // 可观测性（dev-spec t03 / t155 验收）：dirty = 编辑 / 初次加载即时重建（同步于 setBlock，破/放后当帧）；
    //   sun = 太阳跨步全量重建（绕 dirty，t155 编辑活跃期被 WorldClock 节流跳过）；water = 水段切换。
    //   读此日志可核对：破/放后立刻见 dirty 重建（无 3-4s 残留），编辑密集段无 sun 重建抢帧。
    static const char *const kReasonName[] = {"dirty", "sun", "water"};
    qInfo("vo.render: chunk(%d,%d) rebuilt [%s] - %lld verts / %lld idx",
          m_cx, m_cz, kReasonName[int(reason)], qint64(verts.size()), qint64(idx.size()));

    // 通知 F3 叠层刷新（顶点 / 三角面数已更新；t10）。
    emit meshRebuilt();
}
