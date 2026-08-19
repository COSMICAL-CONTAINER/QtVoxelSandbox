#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "frameprofiler.h"        // perf：buildMesh 计时进「mesh」桶（settle t470 重建频率假设）
#include "partialblockgeometry.h" // t133：Vtx（chunk 顶点格式）+ PartialBlockGeometry 异形分支
#include "voxellight.h"           // t257：PCF 软影 + 光场顶点色单点实现（mesher 与 BlockCube 共用）
#include "world.h"

#include <QByteArray>
#include <QElapsedTimer> // t155f：buildMesh 计时（诊断编辑卡顿）
#include <QVector3D>

#include <algorithm> // std::clamp / std::max（t151 真光场顶点色钳制；PCF 软影的 sqrt/floor 已迁 voxellight.h）
#include <cmath>     // tXXX sun 粗量化门：std::asin/std::acos/std::fabs（仰角/方位角累计变化阈值判定）
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
// t123：太阳方向变（WorldClock 量化跨步 → sunChanged → QML 绑定重算 → 本 setter）。
//   体素未变、仅光照变 → 直接 buildMesh(Sun)（绕过 chunk dirty：dirty 是「体素改动」标记，与此无关）。
//   值未变（WorldClock 跨步但量化值恰好相同 / QML 重绑）则早退，避免无谓重建。
//   t155：编辑活跃期 WorldClock 节流跳过 sunChanged（见 worldclock.cpp onTick）→ 本 setter 在编辑密集
//   段不被太阳跨步触发，编辑即时重建（onWorldChanged）独占主线程，无抢帧。
//   t472 视距门控：远端 chunk（!m_chunkInRange）**仍更新 m_sunDir**（保 catch-up 时值已最新）但**跳过
//   buildMesh**（远 chunk 不绘制 → 无谓全量重建顶点光是「600 段无视距全成本」真因）。回 true 时
//   setChunkInRange 主动 catch-up 一次。
//   perf：空段（m_vertexCount==0，无该段方块）无顶点可打光 → 太阳步进对其无视觉影响，跳过重建
//   （100 chunk × 6 段中大部分段为空——地形的 lava/glass/ice/cross/水段常空，只有非空段需随太阳重烘顶点色）。
//   tXXX 粗量化门：WorldClock 每 ~16.7s 跨一步太阳（72 步/1200s 天）→ 旧行为每步全量 buildMesh(Sun)
//   （每非空段重跑 mesher + 逐顶点 PCF 烘光 + GPU 重传）≈ 325 段/16.7s，是残余卡顿主因之一。但昼夜亮度
//   已由 QML baseColor=terrainLight 平滑 lerp —— sun 步进重建只为「方向软影」（PCF 沿 sunDir 步进
//   heightmap）；方向只动几度时影边位移 <0.2 格、PCF 软边又抹掉跳变，肉眼无感 → 不必每步重烘。
//   门（sunRebuildDue）收敛到三类事件才重烘：影淡入/淡出带穿越 | 仰角/方位角累计变超阈值 | 距上次重烘
//   超硬顶。重烘前方向变被跳过 → 只更新 m_sunDir（下次 buildMesh 用最新值），不重建（见 sunRebuildDue）。
void ChunkGeometry::setSunDir(const QVector3D &dir)
{
    if (m_sunDir == dir) return;
    m_sunDir = dir;
    emit sunInputChanged();
    if (!m_chunkInRange) return; // t472：远端 chunk 静默跟随值变，不重建
    if (m_vertexCount == 0) return; // 空段无顶点可打光 → 太阳步进零影响，跳过重建
    if (!sunRebuildDue(dir, m_dayMul)) return; // tXXX：方向变太小 / 未穿影带 / 未超硬顶 / dayMul 未累计超阈 → 只更新值不重建
    buildMesh(RebuildReason::Sun);
}

// PLAN §2-H dayMul setter（昼夜天光乘子，仅乘天光分量）：QML 绑 window.skyDayMul（terrainLight(skyLight)）→
//   dayPhaseChanged（10Hz）推本属性。10Hz 全量重建太贵（325 段/100ms），故走 sunRebuildDue 同款量化门 ——
//   dayMul 累计变超 kDayMulThresh=0.03 才重建（最陡段 ~每 12s 一步，与 sun-step 门合并）。值未变早退。
//   t472 视距门控：远端 chunk 静默更新值，回 true 时 catch-up。
void ChunkGeometry::setDayMul(float m)
{
    if (m < 0.0f) m = 0.0f; else if (m > 1.0f) m = 1.0f; // 钳到合法 [0,1]
    if (m_dayMul == m) return;
    m_dayMul = m;
    emit dayMulChanged();
    if (!m_chunkInRange) return; // t472：远端 chunk 静默跟随值变，不重建
    if (m_vertexCount == 0) return; // 空段无顶点可打光 → 跳过重建
    if (!sunRebuildDue(m_sunDir, m)) return; // dayMul 未累计超阈 / sun 方向未跨门 / 未超硬顶 → 只更新值不重建
    buildMesh(RebuildReason::Sun); // dayMul 变属于光照层变化，复用 Sun reason（绕 chunk dirty，与 sun-step 同语义）
}

// tXXX sun-step 粗量化门（性能：mesh 重建风暴根治 #1——sun 步进）。WorldClock 量化 72 步/天（每 ~16.7s
//   跨一步）→ 旧行为每步 buildMesh(Sun)，325 段/16.7s 全量重跑 mesher。昼夜亮度已由 QML baseColor
//   （terrainLight）平滑 lerp，本门只负责「方向软影」该不该重烘。三类事件才返 true：
//   (a) 影淡入/淡出带穿越：sunDir.y 跨 kSunMin(=0.30) / kSunMax(=kSunMin+kSunFade=0.40) —— 只有跨带时
//       影因子才 0↔非0 切换，是唯一需要「立即」重烘的方向变化点（否则影在错误时刻出现/消失）。
//   (b) 仰角 / 方位角累计变化超阈值：日间影缓慢旋转 + 收缩，攒够 kSunElevThresh / kSunAzimThresh 才重烘
//       一次（PCF 软边掩跳变，粗更新视觉可接受；正常 1200s 天 ≈ 十几次，325 段/16.7s → 罕见）。
//   (c) 距上次重烘超 kSunMaxInterval：硬顶——影绝不冻结超过该时长（防低变化率时段方向阈值永不达）。
//   夜/低仰角（y<=kSunMin）恒返 false：无影，方位角 / 时间都无需重烘（夜间零 sun 重建；黎明跨 0.30 才醒）。
//   层：只读裸 sunDir + 单调时钟（FrameProfiler::nowNs），不依赖 Game 层 WorldClock（Renderer→向下）。
//   debugFast（30s 天）下每步 0.42s，阈值步数不变 → 重烘每 ~1.7s 一次，调试仍见影动；生产 1200s 天每
//   ~30-70s 一次。m_lastBakedSunDir/m_lastSunBakeNs 在 buildMesh 末尾更新（任何 reason 都烘顶点色）。
bool ChunkGeometry::sunRebuildDue(const QVector3D &dir, float dayMul) const
{
    static constexpr float kSunMinY  = VoxelLight::kSunMin;                        // 0.30：影起始门（与 sunShadow 同源）
    static constexpr float kSunMaxY  = VoxelLight::kSunMin + VoxelLight::kSunFade; // 0.40：满影门（淡入带顶）
    static constexpr double kSunElevThresh = 0.12;   // ~6.9° 仰角累计变（影长 / 淡入淡出速率感知）
    static constexpr double kSunAzimThresh  = 0.35;  // ~20° 方位角累计变（日中影缓慢旋转攒够才重烘）
    static constexpr qint64 kSunMaxIntervalNs = 120LL * 1000000000LL; // 120s 硬顶（影绝不冻结超 2 分钟）
    // PLAN §2-H：昼夜天光（dayMul）量化阈值。dayMul ∈ [minLight(0.2..0.6), 1]，0.03 阈 → 整个昼夜周期
    //   约 25-33 步重烘（20min 周期 ~每 40s 一步、最陡段 ~每 12s；30s debug ~每 1s）→ 视觉无突变、不全量重建。
    //   skyLight 在白天/黑夜中段变化最慢（cos 曲线平顶），在黎明/黄昏（phase~0.25/0.75）变化最快 → 量化步进
    //   自然集中在过渡带（用户视觉最敏感处步进密），日中/夜间稳态几乎不重建。
    static constexpr float kDayMulThresh = 0.03f;

    const QVector3D &last = m_lastBakedSunDir;
    // (a) 影带穿越（上一烘 vs 当前）：带边界任一方向穿越都重烘。
    const bool lastBelow = last.y() <= kSunMinY;
    const bool curBelow  = dir.y()  <= kSunMinY;
    const bool lastAbove = last.y() >= kSunMaxY;
    const bool curAbove  = dir.y()  >= kSunMaxY;
    if (lastBelow != curBelow || lastAbove != curAbove) return true;
    // (d) dayMul 累计变化超阈（昼夜天光重烘，PLAN §2-H）。放在影带穿越之后，与仰角/方位角同级。
    if (std::fabs(dayMul - m_lastBakedDayMul) >= kDayMulThresh) return true;
    if (dir.y() <= kSunMinY) return false; // 夜/低仰角：无影，方位角无需重烘（dayMul 变已由 (d) 处理）
    // (b) 方位角累计变化（XZ 投影夹角；太阳近天顶 / 水平分量≈0 时退化不判，同 sunShadow 退化语义）。
    const float lh = std::sqrt(last.x()*last.x() + last.z()*last.z());
    const float ch = std::sqrt(dir.x()*dir.x() + dir.z()*dir.z());
    if (lh > 1e-4f && ch > 1e-4f) {
        const float dot = (last.x()*dir.x() + last.z()*dir.z()) / (lh * ch);
        if (std::acos(std::clamp(dot, -1.0f, 1.0f)) > kSunAzimThresh) return true;
    }
    // (b2) 仰角累计变化（asin 差值；日出日落影长剧变区比 (a) 更细粒度地追影长）。
    const float lastElev = std::asin(std::clamp(last.y(), -1.0f, 1.0f));
    const float curElev  = std::asin(std::clamp(dir.y(),  -1.0f, 1.0f));
    if (std::fabs(lastElev - curElev) > kSunElevThresh) return true;
    // (c) 距上次重烘超硬顶。
    if (FrameProfiler::nowNs() - m_lastSunBakeNs > kSunMaxIntervalNs) return true;
    return false;
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

// t468：冰段开关变 → 重建（冰段只画冰族 isIce、地形段跳冰族 → 两段选块不同，需重网格化）。值未变则早退。
//   用 Dirty reason（同编辑即时重建路径，绕过 sun-step 节流）。冰段走 culled/greedy 立方面路径（满格立方 +
//   自剔 nb==冰族 + 邻实体剔 + 邻空气画半透面，同 glass 模式）。冰非流体（无 state 液面），不走水的变高水面路径。
void ChunkGeometry::setIceOnly(bool on)
{
    if (m_iceOnly == on) return;
    m_iceOnly = on;
    emit iceOnlyChanged();
    buildMesh(RebuildReason::Dirty);
}

// t166b 阴影开关变 → 重网格化（顶点光 PCF 软影随开关重算；语义同光照变 → 用 Sun reason）。值未变早退。
//   t472 视距门控：远端 chunk 静默更新值，回 true 时 catch-up。
void ChunkGeometry::setShadowsEnabled(bool on)
{
    if (m_shadowsEnabled == on) return;
    m_shadowsEnabled = on;
    emit shadowsEnabledChanged();
    if (!m_chunkInRange) return; // t472：远端跳过，回 true catch-up
    buildMesh(RebuildReason::Sun);
}

// t178 贪婪网格化开关变 → 重网格化（greedy vs 逐格 culled 顶点布局不同，须重建）。值未变早退。
//   用 Dirty reason（同编辑即时重建路径，绕过 sun-step 节流）。
//   t472 视距门控：远端 chunk 静默更新值，回 true 时 catch-up。
void ChunkGeometry::setGreedyMeshing(bool on)
{
    if (m_greedyMeshing == on) return;
    m_greedyMeshing = on;
    emit greedyMeshingChanged();
    if (!m_chunkInRange) return; // t472：远端跳过，回 true catch-up
    buildMesh(RebuildReason::Dirty);
}

// t223/tXXX 水贴图动画 phase（flipbook 换帧）——**tXXX 起不再触发重建**（水动画重建消除，静态水单帧）。
//   旧行为：2s 一次全量水段 buildMesh(Water) 换 2 帧 UV（Swamp 场景 261 段/次），是 mesh 重建风暴第二根因；
//   2 帧 UV 子区换帧不必重建整段（重跑 mesher + 逐顶点烘光 + GPU 重传）。现改静态水：本 setter 只记录值 +
//   emit（API 稳定 + 将来 material 级动画复用），mesh 恒用 phase 0 帧（buildMesh 内硬编码 tiles 19/23）。
//   非水段（地形）本就不引用水 tile → 更无重建。phase 钳到 0/1（两帧 flipbook；超界兜底取 0）。
void ChunkGeometry::setWaterAnimPhase(int phase)
{
    if (phase < 0 || phase > 1) phase = 0; // 钳到合法两帧范围
    if (m_waterAnimPhase == phase) return;
    m_waterAnimPhase = phase;
    emit waterAnimPhaseChanged();
    // tXXX：不 buildMesh —— 翻页换帧不再重网格化（静态水单帧；视觉损失可接受，见 tXXX 验收）。
}

// t472 视距门控 setter（修 t470 盲点 + 砍 mesh 重建风暴）：由所在段 Model.chunkInRange 绑定注入。
//   false→true 转变（玩家走近远 chunk 进视野）触发**一次** buildMesh(Sun) catch-up —— 把离开视野期间
//   错过的 sun 步进 / 水翻页 / shadow / greedy 变化一并应用（远 chunk 重进视野时贴图 / 光照非陈旧）。
//   true→false 不重建（远 chunk 不绘制，下次回 true 再 catch up）。值未变早退。
//   reason=Sun：catch-up 走 Sun 语义（绕 chunk dirty；远 chunk 的 dirty 早被 clearAllDirty 清，且 onWorldChanged
//   本身也门控跳过 → dirty 不可靠，故用无条件重建路径）。
void ChunkGeometry::setChunkInRange(bool inRange)
{
    if (m_chunkInRange == inRange) return;
    m_chunkInRange = inRange;
    emit chunkInRangeChanged();
    if (inRange) buildMesh(RebuildReason::Sun); // false→true：catch-up 错过的 sun/water/shadow/greedy 更新
    // true→false：不重建（远 chunk 不绘制）
    // 注：catch-up 不设 m_vertexCount==0 空段守卫 —— 出视距期间的编辑不重建（onWorldChanged 门控跳过），
    //   远 chunk 的 m_vertexCount 可能陈旧（空→非空未反映），回程必须无条件重建（t472「dirty 不可靠」）。
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
    // t472 视距门控：远端 chunk（!m_chunkInRange）跳过编辑即时重建 —— 它不绘制（visible=false），
    //   破块/放块无须当帧刷远端 mesh。dirty 标记会被 World::clearAllDirty 清掉，但远 chunk 重进视野
    //   时 setChunkInRange(false→true) 的 catch-up buildMesh(Sun) 无条件重建，覆盖此情况（mesh 不会陈旧）。
    //   首次构建期（启动）chunkInRange 默认 true，此门控不影响首次 mesh 生成。
    if (!m_chunkInRange) return;
    Chunk *c = myChunk();
    if (!c || !c->dirty()) return;
    // t188 perf：流体专用脏跳过 —— 当本 chunk 自上次 clearAllDirty 以来只收到流体类写（Air/Water/Lava，
    //   由 ChunkManager::setBlock 据 oldId/newId 分类累积于 chunk::fluidOnlyDirty），本段若为 terrain/cross/
    //   glass/ice（非 water/lava 段）则顶点不变（这些段只画非流体方块，流体写必产相同 mesh）→ 跳过重建。
    //   水流风暴时一 tick 数百段无谓重建的真因即此（共享 dirty 拖 terrain 段每 tick 重跑 culled/greedy）。
    //   water/lava 段（m_waterOnly / m_lavaOnly）恒重建（流体写确改变其水面/流面几何）。流体专用假定由
    //   clearAllDirty 复位 true、固体写清 false（固体 dominate → 必重建）；故「混窗」（流体+固体）终态 false →
    //   重建，无误跳。冰段（m_iceOnly）跳过同理：冰↔水经 setWaterSilent 写 Ice（实体）→ fluidOnly=false → 重建。
    if (c->fluidOnlyDirty() && !m_waterOnly && !m_lavaOnly) return;
    buildMesh(RebuildReason::Dirty);
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
    // t456 熔炉朝向：前面（炉口 furnace_front）所朝面由 state 决定（放置时朝玩家，同箱子 horizontalFacing 同源
    //   编码）；其余三侧面 furnace_side(13)、顶/底 furnace_top(12)。此前熔炉未在此特判 → 落 BlockRegistry::tileIndex
    //   兜底（前面恒 -Z 固定方向）。chestFrontFace 是「state 低 2 位 → 水平前面 Face」通用解码器（命名历史性，
    //   0=+X 1=-X 2=+Z 3=-Z），chest / furnace 共用（编码同源）；熔炉复用之，不改箱子行为。
    // t494 熔炉燃烧态：state 的 FurnaceStateLitFlag（bit2）= 1 表「冶炼进行中」→ 前面用 furnace_front_on(134)
    //   （拱洞内带火，机制等价 MC 1.0 熔炉燃烧时正面发光）；bit2=0 用 furnace_front(14)（灭）。由 FurnaceUI
    //   冶炼 tick 在点燃/熄火边界经 PlayerController::setFurnaceLit 翻转本位（5 参数 setBlock → 仅 worldChanged
    //   重建 mesh，朝向低 2 位保留）。侧面/顶底不受燃烧态影响（同 furnace_side / furnace_top）。
    if (block == BlockRegistry::Furnace) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（炉口）所朝面
        if (face == frontFace)
            return (state & BlockRegistry::FurnaceStateLitFlag) != 0 ? 134 : 14; // 134=front_on(带火) / 14=front(灭)
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = furnace_top
        return d.sideTile;                                               // 其余三侧面 = furnace_side
    }
    // t486 发射器朝向（同熔炉编码）：前面（排出口 dispenser_front）所朝面由 state bit[1:0] 决定（放置时朝玩家，
    //   同 chest / furnace horizontalFacing 同源编码）；其余三侧面 dispenser_side(126)、顶/底 dispenser_top(125)。
    //   复用 chestFrontFace 解码（0=+X 1=-X 2=+Z 3=-Z）；发射器不改箱子 / 熔炉行为（编码同源）。
    if (block == BlockRegistry::Dispenser) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（排出口）所朝面
        if (face == frontFace) return d.frontTile;                       // dispenser_front（排出口）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = dispenser_top
        return d.sideTile;                                               // 其余三侧面 = dispenser_side
    }
    // t609 投掷器朝向（同发射器 / 熔炉编码）：前面（排出口 dropper_front(139)）所朝面由 state bit[1:0] 决定
    //   （放置时朝玩家，同 chest / furnace / dispenser horizontalFacing 同源编码）；其余三侧面 furnace_side(13)、
    //   顶/底 furnace_top(12)（复用熔炉贴图——机关盒家族石质观感）。复用 chestFrontFace 解码（0=+X 1=-X 2=+Z
    //   3=-Z）；投掷器不改箱子 / 熔炉 / 发射器行为（编码同源）。
    if (block == BlockRegistry::Dropper) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（排出口）所朝面
        if (face == frontFace) return d.frontTile;                       // dropper_front（排出口）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = furnace_top（复用）
        return d.sideTile;                                               // 其余三侧面 = furnace_side（复用）
    }
    // t234/t406 耕地：顶面（+Y）恒 farmland_dry(26)（topTile）；湿润等级 0..3（state 低 2 位）由顶点色暗化
    //   体现（darker=wetter，见 buildMesh 内 farmlandHydrBrightMul），不再切换 dry/wet 两贴图（4 级靠顶点色
    //   连续暗化实现，无需扩图集 + 2 贴图）。侧/底 = dirt(2)。
    if (block == BlockRegistry::Farmland) {
        if (face == int(BlockRegistry::Top))
            return BlockRegistry::def(block).topTile;     // farmland_dry(26) —— 湿润由顶点色暗化体现
        return BlockRegistry::def(block).sideTile;        // 侧/底 = dirt(2)
    }
    // t487/t620 末地传送门「末影祭坛化」per-face + 激活态：本工程无独立祭坛框方块，传送门方块本体兼作
    //   末影祭坛（endframe 化）。侧·底 = endframe_side(140)（灰白细孔框身）恒定；顶面按 state bit0
    //   （EndPortalStateActiveFlag，玩家持末影之眼右键翻）切换 endframe_top(141)（未放之眼：框面 +
    //   中央暗绿凹槽）→ endframe_eye(142)（已放之眼：框面 + 中央之眼亮纹，放之眼后的可见反馈）。
    //   旧 t487 程序星空贴图（end_portal 129 / end_portal_active 130）仍在图集，但已无 BlockDef/tileFor
    //   引用（非 pack 程序回退改走 140..142 的 build_endframe.py 程序贴图）。
    if (block == BlockRegistry::EndPortal) {
        if (face == int(BlockRegistry::Top))
            return (state & BlockRegistry::EndPortalStateActiveFlag) != 0 ? 142 : 141;
        return 140; // 侧 / 底 = endframe_side（框身）
    }
    // t638 ② 南瓜朝向 per-face（同箱子 / 熔炉 / 发射器模式）：前面（刻面 pumpkin_face）所朝面由 state
    //   bit[1:0] 决定（放置时朝玩家，placeBlock 写 horizontalFacing^1；此前南瓜未写 state → 前面恒 -Z 固定
    //   方向）。复用 chestFrontFace 解码（0=+X 1=-X 2=+Z 3=-Z）；其余三侧面 pumpkin_side(117)、顶/底
    //   pumpkin_top(119)。造物（雪傀儡 / 铁傀儡）检测不读南瓜 state → 零影响。旧存档南瓜 state=0 → 前面
    //   +X 兜底（朝向变化可接受，南瓜仅玩家放置）。
    if (block == BlockRegistry::Pumpkin) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（刻面）所朝面
        if (face == frontFace) return d.frontTile;                       // pumpkin_face（刻面双眼+锯齿嘴）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = pumpkin_top
        return d.sideTile;                                               // 其余三侧面 = pumpkin_side
    }
    // t620 红石灯两态全六面换贴图（机制等价 MC 1.0 redstone lamp off/on 两张贴图）：state bit0
    //   （RedstoneLampStateOnFlag，玩家右键翻位）→ on 态全六面 redstone_lamp_on(153)（暖黄亮芯）、
    //   off 态全六面 redstone_lamp_off(152)（灰暗壳，def 默认）。与熔炉 / 传送门不同：红石灯无朝向 /
    //   per-face 语义（六面同图），仅按 state 二选一 → 不读 BlockDef（def 存 off 态 152 作 BlockCube
    //   手持 / 掉落物的无 state 兜底），此处直接二值返回。光照（光 15）由 lightEmission 状态感知版
    //   承担，与贴图切换解耦（同 t494 熔炉 / t569 红石矿的「贴图 + 光照各自读同一 state bit」模式）。
    if (block == BlockRegistry::RedstoneLamp)
        return (state & BlockRegistry::RedstoneLampStateOnFlag) != 0 ? 153 : 152;
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
//   基础上把「太阳被邻近高地遮挡」处再压暗；火把方光（blockLight）不受影（mesher 取 max(sky*(1-sh)*dayMul, block)
//   保留）。昼夜天光乘子（dayMul）现烘进顶点色**天空分量**（PLAN §2-H 修复：方块光时间不变），故影因子本身
//   时间不变 —— 仅随 sunDir（量化跨步）变 → 绑 sunChanged 重建（每顶点重算）。
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
    // perf「mesh」桶：本 chunk 重建耗时累加进窗口（flush 时报窗口内 mesh 总 ms + rebuild 次数）。
    //   settle t470 假设：静态读 dirty-gated，但若某路径每帧标脏会致全量重建 —— 此桶量化真值。
    FrameProfiler::Scope profMesh("mesh");
    FrameProfiler::instance()->count("meshN");
    // perf：按重建原因细分计数（meshNdirty/meshNsun/meshNwater）→ FrameProfiler::flush 报告拆出 rebuild 原因
    //   构成，定位「mesh 风暴」是 dirty（编辑 / 标脏泄漏）还是 sun（太阳步进）/ water（水翻页）驱动。
    //   读报告：`mesh Xms (Yreb)` 后附 `[Ndirty d Nsun s Nwater w]`，占比最大者即风暴主源。
    if (reason == RebuildReason::Dirty) FrameProfiler::instance()->count("meshNdirty");
    else if (reason == RebuildReason::Sun) FrameProfiler::instance()->count("meshNsun");
    else FrameProfiler::instance()->count("meshNwater");
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
    //   t668 HD 图集：半纹素 = 0.5px 折算成归一化 UV = 0.5/(N × kAtlasTilePx)；垂直维同理 0.5/kAtlasTilePx
    //   （图集高 = 1 瓦片）。旧 16px 时内缩 1/32 瓦片宽，64px 后缩到 1/128（瓦片更密 → 内缩更小才不裁掉
    //   有效像素；**必须与 kAtlasTilePx 同步**，改一漏一 → 采到跨瓦片渗色或瓦片边缘被裁）。
    constexpr int N = BlockRegistry::AtlasTileCount;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * BlockRegistry::kAtlasTilePx);
    constexpr float hy = 0.5f / BlockRegistry::kAtlasTilePx;
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    // t151 真光场 + t153 PCF 软影顶点色（PLAN §2-H / §M，替代 t123 方向太阳 faceVc）：
    //   光场基底 = 邻格（面所朝向的空气格）的 max(sky, block)/15，由 World 的 BFS flood-fill 算出（存 chunk
    //   第三数组），mesher 只读采样。t153 在此基底上叠 PCF 软影：天光分量再乘 (1 - sunShadowAt)，把「太阳
    //   被邻近高地遮挡」处压暗（heightmap 正交深度图沿 sunDir 步进、2×2 PCF 软过渡）；火把方光（block）
    //   不受影（取 max 保留）。
    //   kVcMin / kVcMax 取自 voxellight.h（VoxelLight::kVcMin/kVcMax）—— mesher 与 BlockCube（t257 掉落沙）
    //   共用同一顶点色钳制曲线，保证「掉落沙与地形同亮度」（修暗处挖底沙变亮根因）。
    //
    // PLAN §2-H 夜间火把发光修复（R19 B6）：昼夜天光乘子 dayMul **只乘天光分量**、**绝不乘方块光分量**。
    //   公式 vc = clamp(max(sky/15 × (1 - 软影) × dayMul, block/15), kVcMin, kVcMax)。机制：方块光（火把/熔炉
    //   flood-fill）时间不变，昼夜只调制天光；火把光池（block/15≈0.93）在任何 dayMul 下都全亮 → 夜间火把发光
    //   （修旧版「夜间火把被压到 0.37」根因：旧 dayMul 留在 QML baseColor，会同时压暗 block 通道）。地形材质
    //   baseColor 改白（不再承担昼夜）；dayMul 由 setDayMul 注入（量化门控重烘，非 10Hz 全量重建）。
    constexpr float kVcMin = VoxelLight::kVcMin; // 暗部地板最低亮度（洞穴/阴影最低，仍远低于火把光池 0.93 保持对比）
    constexpr float kVcMax = VoxelLight::kVcMax;
    const float dayMul = m_dayMul; // 本帧烘光的昼夜天光乘子（只乘 sky 项，保 block 项时间不变）

    if (c && m_world) {
        // ---- PASS 1：不完整方块（异形）合批进同一 chunk mesh（t133 PartialBlockGeometry）----
        //   **terrain 段独有**（水段无 partial；waterOnly 守卫防水段 ChunkGeometry 重复渲染异形方块）。
        //   每 cell 仅一次 append。独立于 PASS 2 的面 mask——否则 6 面 mask 各扫一次会 6× 重复 append。
        //   torch / 整立方 / 水不进此 pass。光照上下文（cellLight）按本格光场 + 本格中心 PCF 软影算
        //   （同 t151/t153 异形约定），打包进 PartialLightCtx 传入。
        if (!m_waterOnly && !m_lavaOnly && !m_glassOnly && !m_iceOnly) for (int ly = 0; ly < H; ++ly) { // t343/t405/t468：岩浆/玻璃/冰段只画对应立方面，跳过 partial/cross（PASS 1）
            for (int lz = 0; lz < S; ++lz) {
                for (int lx = 0; lx < S; ++lx) {
                    const int wx = originX + lx, wz = originZ + lz;
                    const quint8 b = blockAtWorld(wx, ly, wz);
                    if (b == 0) continue;
                    if (b == BlockRegistry::Water) continue;       // 水走 PASS 2 立方面（水段）
                    if (b == BlockRegistry::Lava) continue;        // t343 岩浆走 PASS 2 立方面（岩浆段，独立材质）
                    if (b == BlockRegistry::Torch) continue;       // 火把走 torchHost（QML Model）
                    if (b == BlockRegistry::Painting) continue;    // t720 画作走 paintingHost（QML delegate，贴图不进图集）——非 partial 非 cross，双 PASS 均跳过
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
                                            || b == BlockRegistry::Farmland // t408 耕地矮盒经 PartialBlockGeometry 渲染（露 1/16 唇）
                                            || b == BlockRegistry::Cactus   // t445 仙人掌 0.8 细柱经 PartialBlockGeometry 渲染（非满格）
                                            || b == BlockRegistry::SnowLayer // t505 积雪层薄板经 PartialBlockGeometry 渲染（state 高度 1/8..1.0；非满格）
                                            || b == BlockRegistry::EnchantingTable // t620 附魔台 0.75 矮盒经 PartialBlockGeometry 渲染（非满格）
                                            || BlockRegistry::isBed(b);     // t457 床低 3D 模型经 PartialBlockGeometry 渲染（非整立方）
                    const bool isCrossX   = BlockRegistry::isCrossBillboard(b);
                    // t638 ① 木门镂空窗：门上半格栅窗贴图带 alpha（pack door_wood_upper.png 窗格真透明 /
                    //   程序贴图 t638 改窗洞 alpha=0）→ 门须走 **cutout 段**（alphaMode:Mask 材质——alpha<0.5
                    //   像素 discard）才能透视窗后（terrain 段不透明材质会把透明窗画成黑 / 暗色板）。门盒体
                    //   pushBox 几何不变（非 cross），仅路由到 cutout 段渲染（isDoor 门族全格两半都走 cutout——
                    //   下半门板不透明贴图 cutout 无副作用（alpha 全 255 不 discard），保同一方块单段渲染）。
                    const bool isDoorX = BlockRegistry::isDoor(b);
                    // t326 cross cutout 分流：cross 方块（草丛/作物/树苗）贴图带 alpha 透明底 → 进独立 cutout 段
                    //   （半透材质 opacity:0.99 + alphaCutoff:0.5 cutout 透明间隙）；partial 盒体（slab/stairs/...）
                    //   贴图不透明 → 留地形段（不透明材质 opacity=1）。两段互斥：地形段若同时发 cross → opacity=1
                    //   下 alpha 被忽略、透明底当不透明显成实心板（用户「草丛挡视线」根因）。cutout 段只发 cross。
                    if (m_cutoutOnly) {
                        if (!isCrossX && !isDoorX) continue;  // cutout 段：仅 cross + 门（t638 门窗 alpha cutout）
                    } else {
                        if (isCrossX) continue;      // 地形段：cross 走 cutout 段、不在此画（否则显实心板）
                        if (isDoorX) continue;       // t638 门走 cutout 段（窗格 alpha 透视；terrain 段不画门）
                        if (!isPartialX) continue;   // 地形段：仅 partial 盒体（立方面在 PASS 2）
                    }
                    const quint8 cSky = m_world->skyLightAt(wx, ly, wz);
                    const quint8 cBlock = m_world->blockLightAt(wx, ly, wz);
                    const float cShadow = sunShadowAt(float(wx) + 0.5f, float(ly) + 0.5f, float(wz) + 0.5f);
                    // PLAN §2-H：dayMul 只乘天光分量（cross 本格光场），block 项保留 → 夜间火把附近 cross 仍亮。
                    const float cellLight = std::clamp(
                        std::max((cSky / 15.0f) * (1.0f - cShadow) * dayMul, cBlock / 15.0f), kVcMin, kVcMax);
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
                        // PLAN §2-H：dayMul 只乘天光分量（异形盒体各面光场），block 项保留。
                        lctx.face[i] = std::clamp(std::max(nbSkyF * (1.0f - sh) * dayMul, nbBlockF), kVcMin, kVcMax);
                    }
                    // t408 耕地从 PASS 2 立方面迁到 PASS 1 矮盒（PartialBlockGeometry），原 PASS 2 顶面湿润暗化
                    //   （farmlandHydrBrightMul，darker=wetter）改在此预乘 lctx.face[+Y(=2)]，使矮盒顶面顶点色仍随
                    //   state 低 2 位湿润等级渐暗（机制不变，仅消费点迁移）。
                    if (b == BlockRegistry::Farmland)
                        lctx.face[2] *= farmlandHydrBrightMul(quint8(st & BlockRegistry::FarmlandHydrationMask));
                    // t209 栅栏连接：查 4 向水平邻居 id（跨 chunk 经 blockAtWorld 路由，边界邻居正确）。
                    //   仅 fence 读本上下文；其余异形方块忽略。边界格破/放已标邻 chunk 脏（ChunkManager::setBlock
                    //   在 lx/lz 贴边时标邻接脏）→ 跨 chunk 栅栏连接随邻居重网格化自动更新。
                    //   t667 铁轨族（普通 / 动力 / 探测）：追加填 4 向三高（同 / 上 / 下）邻轨高度差
                    //   （railProbeDelta 单一权威，同 World::recomputeRailConnections 的探针形态）→
                    //   PartialBlockGeometry Rail case 直轨段据此把 quad 端边抬高画坡度（低端画坡高端平铺）。
                    PartialNeighborCtx nctx{
                        blockAtWorld(wx + 1, ly, wz),
                        blockAtWorld(wx - 1, ly, wz),
                        blockAtWorld(wx, ly, wz + 1),
                        blockAtWorld(wx, ly, wz - 1),
                    };
                    if (BlockRegistry::isRail(b)) {
                        const auto dprobe = [&](int dx, int dz) {
                            return BlockRegistry::railProbeDelta(
                                { blockAtWorld(wx + dx, ly, wz + dz),
                                  blockAtWorld(wx + dx, ly + 1, wz + dz),
                                  blockAtWorld(wx + dx, ly - 1, wz + dz) });
                        };
                        nctx.railDeltaPx = dprobe(1, 0);
                        nctx.railDeltaNx = dprobe(-1, 0);
                        nctx.railDeltaPz = dprobe(0, 1);
                        nctx.railDeltaNz = dprobe(0, -1);
                    }
                    // t702 红石粉爬墙探针：该向水平邻的上 / 下一格是粉 → +1 / -1（World 连接位 /
                    //   电力互通同判据，渲染据 >0 把该向线臂画成上坡斜段；同铁轨 dprobe 三高探针模式）。
                    if (b == BlockRegistry::RedstoneDust) {
                        const auto dclimb = [&](int dx, int dz) {
                            if (BlockRegistry::isRedstoneDust(blockAtWorld(wx + dx, ly + 1, wz + dz))) return 1;
                            if (BlockRegistry::isRedstoneDust(blockAtWorld(wx + dx, ly - 1, wz + dz))) return -1;
                            return 0;
                        };
                        nctx.dustClimbPx = dclimb(1, 0);
                        nctx.dustClimbNx = dclimb(-1, 0);
                        nctx.dustClimbPz = dclimb(0, 1);
                        nctx.dustClimbNz = dclimb(0, -1);
                    }
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
            //   maxLevel：液面衰减分级。水 8 级（state 1..7 → 液面 (8-s)/8）；岩浆 4 级（state 1..3 → 液面 (4-s)/4）。
            const quint8 fluidId  = m_lavaOnly ? BlockRegistry::Lava  : BlockRegistry::Water;
            const int maxLevel    = m_lavaOnly ? 4 : 8;                 // 源(0) + 流(1..maxLevel-1)
            // t489 材质级 flipbook：水/岩浆段不再采样共享图集（tile 19/23/42），改采样**独立条带纹理**
            //   （waterStrip: 2 列×32 帧 / lavaStrip: 1 列×16 帧）。面 UV 烘焙为「单帧区域」约定：
            //     - u：水双列——水源(st==0) 采左列（静水）、流水(st>0) 采右列（流水）；岩浆单列。
            //     - v ∈ [0, 1/N]：帧 0 区（v=0=条带底）。帧切换由材质 positionV 动画（QtQuick3D Texture 6.11 已把
            //       vOffset 更名 positionV；positive positionV 上移采样 → 帧 k 在 v∈[k/N,(k+1)/N]）驱动——
            //       **mesh 一次性构建、动画纯材质参数，零 buildMesh**（修 t222/t223「水 2s 一次全量重建水段 261 段/次」
            //       的 mesh 重建风暴回归；F3 [w]/[s] reb 不回升）。静水/流水同帧索引同步动画（机制等价 MC 1.0
            //       still/flow 同步 flipbook）。
            //   t391 水面顶面空间涟漪（brightMul/alphaMul）仍按 phase 0 烘死（材质动画接管时间维度的荡漾，
            //   空间维度的反光起伏保留在顶点色）。tiles 19/23/24/25/42（图集水/岩浆瓦片）保留供 BlockCube
            //   手持 / 掉落路径消费；水/岩浆**段** mesh 不再引用图集瓦片。
            //   帧数 N / 帧像素 16 与 blockregistry kWaterStripFrames/kLavaStripFrames/kFluidStripFramePx 同源单一权威
            //   （条带构建方 resourcepackmanager/build_fluid_strips.py 与此处 UV 烘焙 + Main.qml positionV 步长三方共用）。
            const int stripFrames = m_lavaOnly ? BlockRegistry::kLavaStripFrames : BlockRegistry::kWaterStripFrames;
            const float frameH = 1.0f / float(stripFrames);                       // 单帧高（帧 0 区 v∈[0,1/N]）
            // 半像素内缩防帧间 / 列间渗色（线性采样越过帧/列边界采到相邻帧/列）。
            //   **关键**：内缩量是「半纹素」，纹素的归一化 V 尺寸 = 1/条带总高 = 1/(N×帧高px)，不是 1/帧高px
            //   ——早期写成 0.5/帧高px 会恰好等于半帧高（N=16 时 0.5/16 = 0.03125 = (1/16)/2），把帧 0 区
            //   [0,1/N] 内缩成单点 → 面四顶点 v 全相同 → V 维坍缩（采单行纹素）。正确：hys = 0.5/(N×帧高px)。
            //   水条带高 32×16=512 → hys=0.5/512；岩浆条带高 16×16=256 → hys=0.5/256。
            //   hxs：水双列条带宽 32px → 0.5/32；岩浆单列 16px → 0.5/16。
            const float stripPxH = float(stripFrames * BlockRegistry::kFluidStripFramePx); // 条带总高（像素）
            const float hys = 0.5f / stripPxH;
            const float hxs = m_lavaOnly ? (0.5f / float(BlockRegistry::kFluidStripFramePx))
                                         : (0.5f / float(2 * BlockRegistry::kFluidStripFramePx));
            const float stripV0 = hys, stripV1 = frameH - hys;                    // v 子区（帧 0 区，内缩）
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
                        float myTop = renderTop(st, wx, ly, wz); // t350：上方有水 → 满高（柱连续无缝）
                        // t639⑤ 耕地邻面水面 cap：水源 / 流格水平 4 向邻格 == Farmland（耕地矮盒顶
                        //   15/16=0.9375）→ 本格液面 cap 到 15/16，消除「满高 1.0 水面邻耕地凸出 1/16」
                        //   观感破绽（水不漫过耕地顶，接缝齐平）。流水更低液面（<=7/8=0.875）min 后不变。
                        if (blockAtWorld(wx + 1, ly, wz) == BlockRegistry::Farmland
                            || blockAtWorld(wx - 1, ly, wz) == BlockRegistry::Farmland
                            || blockAtWorld(wx, ly, wz + 1) == BlockRegistry::Farmland
                            || blockAtWorld(wx, ly, wz - 1) == BlockRegistry::Farmland)
                            myTop = std::min(myTop, 15.0f / 16.0f);
                        // t489：条带 UV（替代图集 tile UV）。水双列：源(st==0)→左列静水、流(st>0)→右列流水；
                        //   岩浆单列。u ∈ 列宽 [colX, colX+0.5/1.0]，v ∈ [0,1/N]（帧 0 区，内缩）。详见上方 t489 注释。
                        const float colL = m_lavaOnly ? 0.0f : ((st == 0) ? 0.0f : 0.5f); // 左列(still) / 右列(flow)
                        const float colW = m_lavaOnly ? 1.0f : 0.5f;                     // 单列宽（岩浆整宽 / 水半宽）
                        const float u0 = colL + hxs, u1 = colL + colW - hxs;
                        for (int f = 0; f < 6; ++f) {
                            const FaceDef &F = kFaces[f];
                            const int nwx = wx + F.dir[0], nwy = ly + F.dir[1], nwz = wz + F.dir[2];
                            const quint8 nb = blockAtWorld(nwx, nwy, nwz);
                            // t563 ③：邻接**异种流体**（水↔岩浆）→ 剔本面。水/岩浆各占独立透明 mesh 段
                            //   （水 opacity 0.7 / 岩浆 0.95，均透明 pass），在分界面**同一平面**各画一张侧壁
                            //   （本段流体朝对方：isSolid(对方)=false 不剔除、≠fluidId 不剔除 → 落 else 画满侧；
                            //   对方段朝本段同理）→ 两张共面半透明面 → z-fighting 逐帧闪烁（用户「水岩浆混合
                            //   闪烁」）。剔本面（对方段也不画）→ 分界面无共面 → 不闪烁；两流体体积在分界处
                            //   相接（交互凝固 obsidian/stone/cobble 由 tick 处理，此处仅解决渲染闪烁）。
                            const bool nbOtherFluid = (nb == BlockRegistry::Lava || nb == BlockRegistry::Water)
                                                      && nb != fluidId;
                            // 决定本面是否画 + 画的垂直区间 [yLo, yHi]（cell-local）。
                            //   水平面(±Y)：yLo=yHi（单层）；侧面(±X/±Z)：[yLo,yHi] 可能是部分带。
                            float yLo = 0.0f, yHi = myTop;
                            if (F.dir[1] != 0) {
                                // 顶/底面：邻(上/下)为实体或水 → 剔除；为空气 → 画在水面 / 底。
                                if (BlockRegistry::isSolid(nb)) continue;
                                if (nb == fluidId) continue;
                                if (nbOtherFluid) continue; // t563 ③：异种流体上下邻 → 剔共面（防 z-fight 闪烁）
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
                                } else if (nbOtherFluid) {
                                    continue; // t563 ③：异种流体水平邻 → 剔共面（防 z-fight 闪烁）
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
                                // PLAN §2-H：dayMul 只乘天光分量（水/岩浆变高面），block 项保留 → 夜间火把照亮的水面仍可见。
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow) * dayMul, nbBlockF),
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
                                //   叠加一层**空间正弦涟漪**——每顶点据世界角点 (wx+dx, wz+dz) 算 sin（k=1.1，
                                //   周期 ~5.7 格，对角涟漪）；相邻 cell 共享同一角点 → 涟漪跨格连续（非逐格跳变）。
                                //   tXXX：涟漪相位**烘死为 phase 0**（静态水）——旧 flipbook 的 ±π 相位翻转
                                //   （切帧时亮/暗波带互换 = 「闪烁」感）需随 waterAnimPhase 重网格化，已随翻页
                                //   一并废弃；保留烘死的涟漪 → 水面仍有明暗波带质感、非全平死板，但不再随时间
                                //   翻转（静态水视觉损失可接受，见 tXXX 验收）。仅水（fluidId==Water，!m_lavaOnly）；
                                //   岩浆段不参与（浓稠近不透、无涟漪语义）。
                                //   亮度 ±12%（反光起伏）+ vertex.a [0.85,1.0]（材质 opacity 0.7 × vertex.a → 有效
                                //   alpha [0.595,0.7]，透射起伏）；侧面/底面 brightMul=alphaMul=1（原行为）。
                                float brightMul = 1.0f, alphaMul = 1.0f;
                                if (!m_lavaOnly && f == 2) { // +Y 顶面（水面）
                                    const float sarg = float(wx + dx + wz + dz) * 1.1f;
                                    const float wave = std::sin(sarg); // tXXX 静态水：相位 0（不再随 phase 翻转）
                                    brightMul = 1.0f + 0.12f * wave;                 // [0.88, 1.12] 反光起伏
                                    alphaMul = 0.85f + 0.15f * (0.5f + 0.5f * wave);   // [0.85, 1.00] 透射起伏
                                }
                                Vtx v;
                                v.x = float(lx) + dx; v.y = float(ly) + yy; v.z = float(lz) + dz; // 局部坐标
                                v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                                v.u = u0 + cu * (u1 - u0);
                                v.v = stripV0 + cv * (stripV1 - stripV0); // t489：条带帧 0 区（v∈[0,1/N]）；帧切换由材质 positionV 驱动
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
                            const bool isIceBlk = BlockRegistry::isIce(blk); // t468 冰族（Ice/PackIce/BlueIce）
                            // t343/t405/t468 段分流：岩浆段只画 Lava；水段只画 Water；玻璃段只画 Glass；冰段只画冰族；
                            //   地形段跳过流体+玻璃+冰族（各自独立段渲染）。
                            if (m_iceOnly)   { if (!isIceBlk) continue; }
                            else if (m_glassOnly) { if (!isGlass) continue; }
                            else if (m_lavaOnly) { if (!isLava) continue; }
                            else if (m_waterOnly) { if (!isWater) continue; }
                            else { if (isWater || isLava || isGlass || isIceBlk) continue; }       // 地形段跳流体 + 玻璃 + 冰族
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Torch) continue;
                            if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isPartialBlock(blk)) continue; // t412 异形已在 PASS 1（含段外圆石变体）；段后整立方（Chest）正常进立方面
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Farmland) continue; // t408 耕地矮盒已在 PASS 1；不进整立方面（否则满格立方覆盖矮盒唇）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isCrossBillboard(blk)) continue; // t235/t305 cross（草丛/作物/树苗）已在 PASS 1；不进立方面
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Cactus) continue; // t445 仙人掌 0.8 细柱已在 PASS 1；不进整立方面（否则满格立方覆盖细柱）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::SnowLayer) continue; // t505 积雪层薄板已在 PASS 1；不进整立方面（否则满格立方覆盖薄板）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isBed(blk)) continue; // t457 床低 3D 模型已在 PASS 1；不进整立方面（否则满格立方覆盖低床）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::EnchantingTable) continue; // t620 附魔台 0.75 矮盒已在 PASS 1；不进整立方面（否则满格立方覆盖矮盒）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Painting) continue; // t720 画作渲染走 paintingHost QML delegate（贴图不进图集）；立方面路径会把画格画成 tile 0 草顶立方
                            const quint8 nb = blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (BlockRegistry::isSolid(nb)) continue;       // 邻居实体 → 剔除（跨 chunk 路由正确）
                            if (isWater && nb == BlockRegistry::Water) continue; // 水-水面互剔
                            if (isLava && nb == BlockRegistry::Lava) continue;   // t343 岩浆-岩浆面互剔
                            // t405 玻璃-玻璃面互剔（Glass solid=false → isSolid(nb) 不剔除玻璃邻；显式剔除避免两玻璃共面重复绘制）。
                            if (isGlass && nb == BlockRegistry::Glass) continue;
                            // t468 冰-冰面互剔（冰族 solid=false → isSolid(nb) 不剔除冰邻；显式剔除避免两冰共面重复绘制，同 glass 模式）。
                            if (isIceBlk && BlockRegistry::isIce(nb)) continue;
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
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow) * dayMul, nbBlockF),
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
                        const bool isIceBlk = BlockRegistry::isIce(b); // t468 冰族（Ice/PackIce/BlueIce）
                        // t343/t405/t468 段分流：岩浆段只画 Lava；水段只画 Water；玻璃段只画 Glass；冰段只画冰族；
                        //   地形段跳过流体+玻璃+冰族（各自独立段渲染）。
                        if (m_iceOnly)   { if (!isIceBlk) continue; }
                        else if (m_glassOnly) { if (!isGlass) continue; }
                        else if (m_lavaOnly) { if (!isLava) continue; }
                        else if (m_waterOnly) { if (!isWater) continue; }
                        else { if (isWater || isLava || isGlass || isIceBlk) continue; }
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Torch) continue;
                        if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isPartialBlock(b)) continue; // t412 异形已在 PASS 1（含段外圆石变体）；段后整立方（Chest）正常进立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Farmland) continue; // t408 耕地矮盒已在 PASS 1；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isCrossBillboard(b)) continue; // t235/t305 cross（草丛/作物/树苗）已在 PASS 1；不进立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Cactus) continue; // t445 仙人掌 0.8 细柱已在 PASS 1；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::SnowLayer) continue; // t505/t510 二轮复盘：积雪层薄板已在 PASS 1；不进整立方面（否则满格立方覆盖薄板，雪层显完整方块）
                        if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isBed(b)) continue; // t457 床低 3D 模型已在 PASS 1；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::EnchantingTable) continue; // t620 附魔台 0.75 矮盒已在 PASS 1
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Painting) continue; // t720 画作渲染走 paintingHost QML delegate（贴图不进图集）；不进整立方面
                        for (int f = 0; f < 6; ++f) {
                            const FaceDef &F = kFaces[f];
                            const quint8 nb = blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (BlockRegistry::isSolid(nb)) continue;
                            if (isWater && nb == BlockRegistry::Water) continue;
                            if (isLava && nb == BlockRegistry::Lava) continue;
                            // t405 玻璃-玻璃面互剔（Glass solid=false → isSolid(nb) 不剔除玻璃邻；显式剔除避免两玻璃共面重复绘制）。
                            if (isGlass && nb == BlockRegistry::Glass) continue;
                            // t468 冰-冰面互剔（冰族 solid=false → isSolid(nb) 不剔除冰邻；显式剔除避免两冰共面重复绘制，同 glass 模式）。
                            if (isIceBlk && BlockRegistry::isIce(nb)) continue;
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
                                // PLAN §2-H：dayMul 只乘天光分量（立方面），block 项保留 → 夜间火把/熔炉光照亮的方块面仍全亮。
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow) * dayMul, nbBlockF),
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

    // tXXX sun-step 粗量化：记录「本次实际烘进顶点色的太阳方向」+ 时刻 —— 下次 setSunDir 据此判是否值得
    //   重烘（方向变太小 → 只更新 m_sunDir 不重建）。**任何 reason** 的 buildMesh 都烘顶点色（PCF 软影用
    //   m_sunDir）→ 一律更新，门从「最近一次实际烘光的太阳位」起算（编辑即时重建后，sun 门从编辑时的太阳位
    //   重新累积，不会把编辑前旧方向也计入）。
    m_lastBakedSunDir = m_sunDir;
    m_lastBakedDayMul = m_dayMul; // PLAN §2-H：记录「本次实际烘进顶点色的 dayMul」，下次 setDayMul 据此判量化门
    m_lastSunBakeNs = FrameProfiler::nowNs();

    // 可观测性（dev-spec t03 / t155 验收）：dirty = 编辑 / 初次加载即时重建（同步于 setBlock，破/放后当帧）；
    //   sun = 太阳跨步全量重建（绕 dirty，t155 编辑活跃期被 WorldClock 节流跳过）；water = 水段切换。
    //   读此日志可核对：破/放后立刻见 dirty 重建（无 3-4s 残留），编辑密集段无 sun 重建抢帧。
    static const char *const kReasonName[] = {"dirty", "sun", "water"};
    qInfo("vo.render: chunk(%d,%d) rebuilt [%s] - %lld verts / %lld idx (%lldus)",
          m_cx, m_cz, kReasonName[int(reason)], qint64(verts.size()), qint64(idx.size()), qint64(bt.nsecsElapsed() / 1000));

    // 通知 F3 叠层刷新（顶点 / 三角面数已更新；t10）。
    emit meshRebuilt();
}
