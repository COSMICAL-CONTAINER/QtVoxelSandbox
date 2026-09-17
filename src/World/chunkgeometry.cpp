#include "chunkgeometry.h"
#include "blockregistry.h"
#include "chunk.h"
#include "frameprofiler.h"        // perf：buildMesh 计时进「mesh」桶（settle t470 重建频率假设）
#include "mathtypes.h"            // §29.5-W4：hashMix64/ChunkKey（requestId (cx,cz) 基座派生——Core 叶子单一权威）
#include "meshbuilder.h"          // R20.13：网格算法单一权威（本文件的网格本体已整体迁入）
#include "partialblockgeometry.h" // t133：Vtx（chunk 顶点格式）——灌注布局（stride/属性偏移）消费
#include "voxellight.h"           // tXXX：sun-step 粗量化门常量（kSunMin/kSunFade，与软影同源）
#include "world.h"

#include <QByteArray>
#include <QElapsedTimer> // t155f：buildMesh 计时（诊断编辑卡顿）
#include <QPointer>      // §29.5-W4：异步交付回调的本对象存活守卫（W3 驱逐/池重建竞态防悬垂）
#include <QVector3D>

#include <algorithm> // std::clamp / std::max（tXXX sun 粗量化门：仰角/方位角夹取）
#include <cmath>     // tXXX：std::asin/std::acos/std::fabs
#include <cstring>

// R20.13 迁移登记（抽取 = 搬移不改写；矩阵 r2013d 反探钉「网格本体不回流」）：本文件原持有的
// 网格算法件已整体迁往 meshbuilder.cpp——FaceDef/kFaces 六面表、occludesNeighborFace（t746 叶
// 特例）、tileFor / farmlandHydrBrightMul（贴图/湿润查表）、PASS1 异形合批、PASS2 流体变高面/
// greedy mask/逐格 culled 三路、AtlasTileCount UV 常量与 AO 三探针。本文件只剩「重建调度
// （dirty/sun/water 门控）+ 快照采集 + MeshBuilder 委托 + QQuick3D 灌注 + 观测（FrameProfiler
// 「mesh」时间桶 / vo.render 日志 / tXXX 量化门账本）」。
//
// Vtx（chunk 顶点格式：pos3 + normal3 + uv2 + color4 rgba = 12 float = 48 字节）定义仍出自
// partialblockgeometry.h —— 由 meshbuilder.cpp（网格本体）与 PartialBlockGeometry::append（异形
// 方块）共用；本文件只按其布局灌注 QQuick3D 属性偏移。color.rgb 承载 t121 天光遮蔽 + t123 方向
// 太阳光调制。

ChunkGeometry::ChunkGeometry(QQuick3DObject *parent) : QQuick3DGeometry(parent) {}

void ChunkGeometry::setWorld(World *w)
{
    if (m_world == w) return;
    // §29.5-W4：换世界 = 旧世界的在途网格作业一并作废（收割拍若稍后交付旧世界网格，
    //   会把上一世界的地形灌进已换绑的本段——显式注销，交付 miss 可见丢弃收口）。
    if (m_world && m_pendingRequestId != 0) {
        m_world->cancelChunkMeshJob(m_pendingRequestId);
        m_pendingRequestId = 0;
    }
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
    if (!m_chunkInRange) { // t472：窗外 chunk 静默跟随值变，不重建
        // t972：方向/昼夜已跨重烘门 → 光照欠账（呈现层渐进同步排空），防夜晚远处仍显上烘亮度
        if (sunRebuildDue(dir, m_dayMul)) m_lightStale = true;
        return;
    }
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
    if (!m_chunkInRange) { // t472：窗外 chunk 静默跟随值变，不重建
        // t972：昼夜乘子累计变已跨重烘门 → 光照欠账（呈现层渐进同步排空；t972 起窗外可见，
        //   不排空则夜晚远处地形仍显上烘正午亮度）
        if (sunRebuildDue(m_sunDir, m)) m_lightStale = true;
        return;
    }
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

// review28 #4：cutout 折叠开关变 → 重建。两态 PASS 1 选块不同——false 恢复 cross/门/活板门跳过清单
//   （让位并行存在的独立 cutout 段，互斥闭合防双重发射），true 重新全收。值未变则早退。Dirty reason
//   同 setCutoutOnly（绕过 sun-step 节流，跨 chunk 边界 cross 随开关翻转即时刷新）。
void ChunkGeometry::setCutoutFolded(bool on)
{
    if (m_cutoutFolded == on) return;
    m_cutoutFolded = on;
    emit cutoutFoldedChanged();
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

// t1023 AO 环境光遮蔽开关变 → 地形段逐格 culled 路径重网格化（角点接触阴影重烘）。值未变早退。
//   Dirty reason（同 setGreedyMeshing：内容驱动重建，绕过 sun-step 节流）。t472 视距门控：
//   远端 chunk 静默更新值，回 true 时 catch-up。范围钉死：仅逐格 culled 路径采样（greedy 合并键
//   不含 AO / 流体水面观感未验 / 异形 cross 段走 PartialLightCtx——均不采样，t1023 报告登记后续）。
void ChunkGeometry::setAoEnabled(bool on)
{
    if (m_aoEnabled == on) return;
    m_aoEnabled = on;
    emit aoEnabledChanged();
    if (!m_chunkInRange) return; // t472：远端跳过，回 true catch-up
    buildMesh(RebuildReason::Dirty);
}

// t223/tXXX 水贴图动画 phase（flipbook 换帧）——**tXXX 起不再触发重建**（水动画重建消除，静态水单帧）。
//   旧行为：2s 一次全量水段 buildMesh(Water) 换 2 帧 UV（Swamp 场景 261 段/次），是 mesh 重建风暴第二根因；
//   2 帧 UV 子区换帧不必重建整段（重跑 mesher + 逐顶点烘光 + GPU 重传）。现改静态水：本 setter 只记录值 +
//   emit（API 稳定 + 将来 material 级动画复用），mesh 恒用 phase 0 帧（meshbuilder 恒烘 tiles 19/23 条带帧 0）。
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
    // true→false：不重建（重建窗口外；错过部分记欠账由呈现层渐进同步排空，见 t972 onWorldChanged/setSunDir）
    // 注：catch-up 不设 m_vertexCount==0 空段守卫 —— 出窗期间的编辑不重建（onWorldChanged 门控跳过），
    //   窗外 chunk 的 m_vertexCount 可能陈旧（空→非空未反映），回程必须无条件重建（t472「dirty 不可靠」）。
}

// t972 载入世界空白区修复（三入口实现；契约见 .h 注释）。世界内容换代（beginLoad/regenerate 完成、
//   enterWorld 内呈现层调）后对窗外段调用：旧世界 / 上局 mesh 立即作废（vertexCount→0 → QML
//   `visible: vertexCount > 0` 绑定自动隐），防有限世界全幅可见后「窗外显上一世界地形」的陈旧错景。
//   同步清两项欠账（欠账语义针对当前世界；换代后旧世界的错过不再有意义，重建由呈现层入队重新覆盖）。
//   文档序（lessons t03/t35）：clear → setVertexData 空 → setStride → setIndexData 空 → bounds →
//   原语 → addAttribute → update。不置脏（无世界事件，纯呈现层动作）。
void ChunkGeometry::clearMesh()
{
    // §29.5-W4：世界内容换代 = 在途网格作业作废（陈旧 mesh 若在换代后交付，会把上一内容
    //   灌进已清空的段——显式注销，交付 miss 可见丢弃收口；与 t972 欠账双清同面）。
    if (m_world && m_pendingRequestId != 0) {
        m_world->cancelChunkMeshJob(m_pendingRequestId);
        m_pendingRequestId = 0;
    }
    m_vertexCount = 0;
    m_triangleCount = 0;
    m_deferredRebuild = false;
    m_lightStale = false;
    clear();
    setVertexData(QByteArray());
    setStride(int(sizeof(Vtx)));
    setIndexData(QByteArray());
    setBounds(QVector3D(0, 0, 0), QVector3D(Chunk::kSize, m_world ? m_world->height() : 0, Chunk::kSize));
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic,
                 int(offsetof(Vtx, x)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic,
                 int(offsetof(Vtx, nx)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic,
                 int(offsetof(Vtx, u)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic,
                 int(offsetof(Vtx, r)), QQuick3DGeometry::Attribute::F32Type);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic,
                 0, QQuick3DGeometry::Attribute::U32Type);
    update();
    emit meshRebuilt(); // F3 顶点汇总同步归零（与 buildMesh 完成同一通知面）
}

// t972 渐进同步队列的排空动作：呈现层（Main.qml _meshSyncTimer）逐帧限量调，把窗外欠账 / 载入后的
//   窗外重建请求落到与编辑同一条 buildMesh 链（Dirty 因由：内容驱动重建，与 finishLoad 首建同族）。
//   无条件重建（不检 dirty / sunRebuildDue）——caller 依据 deferredRebuildPending()/lightStale() 决策，
//   本方法只管执行（与 setChunkInRange catch-up 同款「决策在上、执行无条件」分工）。
void ChunkGeometry::refreshMesh()
{
    buildMesh(RebuildReason::Dirty);
}

// R20.08 WorldFacade 迁移：旧 myChunk()（m_world->chunks().chunk(m_cx, m_cz) 直取 Chunk*）退役——
//   chunk 存在/脏/流体专用脏三门改经 WorldFacade 收窄面查询（chunkgeometry.h chunkExists/chunkDirty/
//   chunkFluidOnlyDirty 三帮手，逐位同语义委托）。本类自此零 Chunk*（渲染侧唯一 Chunk 内部指针
//   消费点消除——refactor-plan §29.3 R20.08 验收①；矩阵 r2008c 结构钉守「myChunk 不回流」）。

// dirty 驱动（dev-spec t03 验收）：仅当本 chunk 脏才重建。worldChanged 每次编辑都发，
// 但 9 个 ChunkGeometry 各检各的 chunk 脏标记 → rebuild 次数 = dirty chunk 数（非脏跳过）。
// t155 编辑即时重建保证：setBlock → ChunkManager.markDirty → World::emit worldChanged（GUI 同线程
//   直连）→ 本槽**同步**执行 buildMesh(Dirty)，破 / 放后贴图当帧刷新（不延迟到太阳步进，<1 帧）。
//   clearDirty 由本（编辑）路径独占 —— 太阳刷新（setSunDir→buildMesh(Sun)）见到的 chunk 永远非脏
//   （编辑的 onWorldChanged 已同步清过），故清脏条件 chunkDirty()（R20.08 前为 `c->dirty()`）在
//   太阳路径恒 false 不清、在编辑路径
//   恒 true 清除，二者语义解耦、对任何太阳时序 immediate rebuild 都稳健（见 buildMesh 末尾清脏）。
void ChunkGeometry::onWorldChanged()
{
    // t472 重建窗口门控：窗外 chunk（!m_chunkInRange）跳过编辑即时重建 —— 重建是 CPU 大头，破块/放块
    //   只需当帧刷玩家周边 mesh。dirty 标记会被 World::clearAllDirty 清掉，错过由两路兜底：
    //   chunk 进窗口时 setChunkInRange(false→true) 的 catch-up buildMesh(Sun) 无条件重建；t972 起
    //   窗外段也参与绘制（有限世界全幅可见），错过**当场记欠账**（m_deferredRebuild）→ 呈现层
    //   渐进同步队列排空（refreshMesh），远处可见地形不再等玩家走近才更新。
    //   首次构建期（启动）chunkInRange 默认 true，此门控不影响首次 mesh 生成。
    if (!m_chunkInRange) {
        if (chunkDirty(m_cx, m_cz))
            m_deferredRebuild = true; // t972：内容重建欠账（本次 worldChanged 的变更窗外未建）
        return;
    }
    if (!chunkDirty(m_cx, m_cz)) return;
    // t188 perf：流体专用脏跳过 —— 当本 chunk 自上次 clearAllDirty 以来只收到流体类写（Air/Water/Lava，
    //   由 ChunkManager::setBlock 据 oldId/newId 分类累积于 chunk::fluidOnlyDirty），本段若为 terrain/cross/
    //   glass/ice（非 water/lava 段）则顶点不变（这些段只画非流体方块，流体写必产相同 mesh）→ 跳过重建。
    //   水流风暴时一 tick 数百段无谓重建的真因即此（共享 dirty 拖 terrain 段每 tick 重跑 culled/greedy）。
    //   water/lava 段（m_waterOnly / m_lavaOnly）恒重建（流体写确改变其水面/流面几何）。流体专用假定由
    //   clearAllDirty 复位 true、固体写清 false（固体 dominate → 必重建）；故「混窗」（流体+固体）终态 false →
    //   重建，无误跳。冰段（m_iceOnly）跳过同理：冰↔水经 setWaterSilent 写 Ice（实体）→ fluidOnly=false → 重建。
    if (chunkFluidOnlyDirty(m_cx, m_cz) && !m_waterOnly && !m_lavaOnly) return;
    buildMesh(RebuildReason::Dirty);
}

// ── R20.13 MeshBuilder 委托（plan §29.3 五验收的适配面）────────────────────────────────
// 旧 buildMesh 的网格算法本体已整体迁至 MeshBuilder::build（meshbuilder.cpp——PASS1 异形合批 /
// PASS2 六面 mask + 边界剔除 / dayMul 顶点色 / AO / 六段分流 / FrameProfiler 计数，搬移不改写，
// 禁两份网格逻辑并存）。本方法收敛为适配链：**采集稠密快照（验收②）→ MeshBuilder::build 产出
// owning ChunkMeshData（验收③）→ 灌 QQuick3D 几何（验收④：旧 QtQuick3DAdapter 消费形态，QML 面
// 零变化）**。dayMul/sunDir 等烘焙状态在采集时定格进快照元数据；顶点输出与旧路径逐位一致
// （验收⑤，矩阵 r2013a 字节比对承重）。
void ChunkGeometry::buildMesh(RebuildReason reason)
{
    // ── §29.5-W4（r2026）bake→worker 网格化路径选择器 ──────────────────────────────────
    // 通电世界（World::chunkMeshAsyncActive：sparse 流式会话 sink 已绑定，或 §29.7 t1060 起
    // fixed 世界经 enableFixedAsyncBake 使能——生产挂点 = StreamingBridge fixed 进入分支）
    // 先走异步：主线程定格快照 → 提交（requestId=(cx,cz,段,代次) 派生）→ 收割拍交付应用
    //（sparse = GameSession tick 尾 pumpStreamingTick ⑤ 段单点；fixed = StreamingBridge::
    // pumpTick 挂 WorldClock::ticked 的薄收割槽，单拍应用有界=风暴摊平）。提交被拒（队列
    // 满载 / 执行器已停 / 桥未就绪）或空快照（无 chunk——构建产物恒空，无作业必要）→ 落到
    // 下方同步内联回退（拒绝面计数可见；未通电世界门不入 = 旧行为零变化墙逐位原样）。
    if (m_world && m_world->chunkMeshAsyncActive() && submitMeshJobAsync(reason))
        return;

    // ── 同步内联路径（未通电世界的唯一路径 = 旧行为零变化墙；通电世界的回退面）──────────
    QElapsedTimer bt; bt.start(); // t155f：诊断编辑卡顿（每 chunk 重建耗时）
    // perf「mesh」桶：本 chunk 重建耗时累加进窗口——覆盖「采集快照 + MeshBuilder 构建 + QQuick3D
    //   灌注」全程（与旧路径同窗口同语义；事件计数 meshN 族已随网格本体迁入 MeshBuilder::build，
    //   经新路径照走）。settle t470 假设：静态读 dirty-gated，但若某路径每帧标脏会致全量重建 ——
    //   此桶量化真值。
    FrameProfiler::Scope profMesh("mesh");
    const bool haveChunk = chunkExists(m_cx, m_cz); // R20.08：存在门经 Facade（旧 myChunk() 直取退役）

    // 采集（验收②）：把本几何的烘焙状态（dayMul/sunDir/阴影/AO/greedy/六段路由开关）定格进
    //   快照元数据，再经 WorldFacade 收窄面把 chunk 及其 pad 邻域（边界面剔除 / AO 探针 / PCF 列顶
    //   的最大查询触达）一次性采成自持稠密域。!haveChunk / 无世界 → 空快照（height=0）：
    //   MeshBuilder 产出空 mesh——与旧路径 !haveChunk 同样产出空顶点，行为逐位一致（计数照走）。
    ChunkMeshBakeParams bake;
    bake.dayMul = m_dayMul;
    bake.sunDir = m_sunDir;
    bake.shadowsEnabled = m_shadowsEnabled;
    bake.aoEnabled = m_aoEnabled;
    bake.greedyMeshing = m_greedyMeshing;
    bake.waterOnly = m_waterOnly;
    bake.lavaOnly = m_lavaOnly;
    bake.glassOnly = m_glassOnly;
    bake.iceOnly = m_iceOnly;
    bake.cutoutOnly = m_cutoutOnly;
    bake.cutoutFolded = m_cutoutFolded;
    ChunkMeshSnapshot snap;
    if (haveChunk && m_world)
        snap = captureChunkMeshSnapshot(WorldFacade(*m_world), m_cx, m_cz, bake);

    // 构建（验收③⑤）+ 收尾：网格算法单一权威，owning 输出自持（顶点/索引/统计）→ 灌注/
    //   账本/观测收尾与异步交付路径共用 applyChunkMeshData（禁两份灌注逻辑并存）。
    //   Reason 与 RebuildReason 值域镜像（编译期互钉见 meshbuilder.cpp）。
    applyChunkMeshData(MeshBuilder::build(snap, MeshBuilder::Reason(int(reason))), reason,
                       m_sunDir, m_dayMul, bt);
}

// ── §29.5-W4（r2026）异步提交半边（选型立证见 .h 声明注释；收割半边 = GameSession
//    pumpStreamingTick 单点 → World::deliverBuiltChunkMesh → 本文件交付回调）────────────────
// 采集段主线程成本计入「mesh」窗口桶（同步路径同桶分段口径——提交相 = 采集，交付相 = 灌注；
// 两段不嵌套、构建段在 worker 线程不计入——异步化的收益即构建段离开主线程，桶语义如实分账）。
bool ChunkGeometry::submitMeshJobAsync(RebuildReason reason)
{
    FrameProfiler::Scope profMesh("mesh");
    const bool haveChunk = chunkExists(m_cx, m_cz);
    ChunkMeshBakeParams bake;
    bake.dayMul = m_dayMul;
    bake.sunDir = m_sunDir;
    bake.shadowsEnabled = m_shadowsEnabled;
    bake.aoEnabled = m_aoEnabled;
    bake.greedyMeshing = m_greedyMeshing;
    bake.waterOnly = m_waterOnly;
    bake.lavaOnly = m_lavaOnly;
    bake.glassOnly = m_glassOnly;
    bake.iceOnly = m_iceOnly;
    bake.cutoutOnly = m_cutoutOnly;
    bake.cutoutFolded = m_cutoutFolded;
    ChunkMeshSnapshot snap;
    if (haveChunk && m_world)
        snap = captureChunkMeshSnapshot(WorldFacade(*m_world), m_cx, m_cz, bake);
    if (!snap.valid())
        return false; // 空快照（无 chunk/无世界）：构建产物恒空网格——同步内联零成本语义，非回退

    // latest-snapshot-wins 路由层半边：上一作业在途 → 先显式注销（其产出交付时 miss →
    // World::droppedBuiltMeshes 可见丢弃）。本几何同时至多一个在途作业（提交前必注销旧账）。
    if (m_pendingRequestId != 0)
        m_world->cancelChunkMeshJob(m_pendingRequestId);

    // 提交代次先取后增（同段连发互异在途键——最新胜判据承重；派生选型见 deriveMeshJobRequestId）。
    const quint32 gen = m_meshSubmitGen++;
    const quint64 requestId = deriveMeshJobRequestId(m_cx, m_cz, segmentIndex(), gen);

    // 交付回调：捕获采集时刻的烘焙账本值（应用与采集之间 sun/dayMul 可能前移——账本必须记
    //   实际烘进顶点色的值，见 applyChunkMeshData 注）+ QPointer 存活守卫（W3 驱逐/池重建把
    //   几何销毁后交付照常到达 → 丢弃，悬垂不可能）。
    QPointer<ChunkGeometry> self(this);
    const QVector3D bakedSunDir = m_sunDir;
    const float bakedDayMul = m_dayMul;
    const Result<void> r = m_world->submitChunkMeshJob(
        snap, requestId,
        [self, requestId, reason, bakedSunDir, bakedDayMul](quint64 deliveredId,
                                                            ChunkMeshData &&mesh) {
            if (!self)
                return; // 几何已亡（世界换代/池重建/W3 驱逐）：交付 miss 已由 World 可见计数
            if (deliveredId != self->m_pendingRequestId) {
                // 最新胜第二道闸（双保险）：被新快照淘汰的在途产出（注销竞态防御面）——可见丢弃。
                FrameProfiler::instance()->count("meshNstale");
                return;
            }
            self->m_pendingRequestId = 0; // 交付收口：在途账清零
            // perf：交付相（灌注）计入「mesh」窗口桶（与提交相同桶分段，不嵌套）。
            FrameProfiler::Scope profApply("mesh");
            QElapsedTimer bt; bt.start();
            self->applyChunkMeshData(std::move(mesh), reason, bakedSunDir, bakedDayMul, bt);
            // F3 worker 列数据源：本窗口经收割交付应用的网格数（同步路径不计——win 行
            //   「worker N」= 异步化可见面；worker 构建段按 Reason::Dirty 计入 meshN/meshNdirty，
            //   阳光/昼夜驱动的异步重建在该分桶如实呈现为 dirty——计数分桶差异登记面）。
            FrameProfiler::instance()->count("meshNworker");
        });
    if (!r.isOk()) {
        // 同步回退面（执行器拒绝族：队列满载 kErrQueueFull / 已停 = 202 执行域码）——可见
        //   计数 + 告警，调用方走同步内联（网格仍正确产出）。
        FrameProfiler::instance()->count("meshNsyncFallback");
        qWarning("vo.render: chunk(%d,%d) async submit refused (code %d) - inline sync fallback",
                 m_cx, m_cz, r.error().code);
        return false;
    }
    m_pendingRequestId = requestId; // 在途账登记（交付回调的 latest-wins 判据锚）
    return true;
}

// ── §29.5-W4 requestId 派生（(cx,cz,段) 基座 + 提交代次）────────────────────────────────
// 布局 = hashMix64(ChunkKey{cx,cz}.packed()) 高位移入 | 段位 3 bit（bits 13-15）| 提交代次
//   13 bit（bits 0-12，mod 8192）。**为何基座之外还要代次位**：纯 chunk 键对同段连发产生同键
//   ——在途互异是最新胜判据（交付回调 deliveredId == m_pendingRequestId）与注册表 1:1 交付
//   擦除的承重前提，故同段每次提交以 per-geometry 单调代次互异；回绕周期 8192 ≫ 在途上界
//  （执行器请求队列 64 + 收割队列传递性有界 ≤ 受理数），回绕混淆在途窗口内结构性不可能。
//   基座哈希域 = 64 位哈希单射假设（hashColumn/hashVoxel 同门先例）；即便极端碰撞，交付回调
//   判据使误应用结构性不可能，最坏退化为一次丢弃 + 下次触发自然重提交（自愈面）。
quint64 ChunkGeometry::deriveMeshJobRequestId(int cx, int cz, int segIndex, quint32 submitGen)
{
    const quint64 base = hashMix64(ChunkKey{ cx, cz }.packed());
    return (base << 16) | (quint64(segIndex & 0x7) << 13) | quint64(submitGen & 0x1FFF);
}

// ── §29.5-W4 应用半边（同步/异步两路共用收尾链——禁两份灌注逻辑并存）────────────────────
// 自旧 buildMesh 尾段收敛而来（逐行原样搬移）：统计镜像 → 灌 QQuick3D（文档序）→ 烘焙账本
//   （bakedSunDir/bakedDayMul 形参 = 采集时刻定格值；同步路径传当前成员 = 同值）→ vo.render
//   观测（格式逐字原样——异步交付同格式同 reason 名，worker 化可见面在 F3 worker 列）→ 通知。
void ChunkGeometry::applyChunkMeshData(ChunkMeshData mesh, RebuildReason reason,
                                       const QVector3D &bakedSunDir, float bakedDayMul,
                                       const QElapsedTimer &bt)
{
    const int H = m_world ? m_world->height() : 0;
    constexpr int S = Chunk::kSize; // 16（X、Z chunk 边长）

    // 网格统计（t10 F3 叠层）：顶点 / 三角面数已自持于 ChunkMeshData，本几何只镜像（QML 只读面不变）。
    m_vertexCount = mesh.vertexCount;
    m_triangleCount = mesh.triangleCount;

    // 写入 QQuick3DGeometry（文档顺序：clear → 数据 → stride → bounds → 原语 → 属性 → update；
    //   t35 教训的文档序逐行保留——数据源从局部 QVector 换成 owning ChunkMeshData，字节布局同构）。
    clear();

    QByteArray vb;
    vb.resize(int(mesh.vertices.size() * sizeof(Vtx)));
    if (!vb.isEmpty())
        std::memcpy(vb.data(), mesh.vertices.constData(), size_t(vb.size()));
    setVertexData(vb);
    setStride(int(sizeof(Vtx))); // 48（pos3 + normal3 + uv2 + color4 rgba）

    QByteArray ib;
    ib.resize(int(mesh.indices.size() * sizeof(quint32)));
    if (!ib.isEmpty())
        std::memcpy(ib.data(), mesh.indices.constData(), size_t(ib.size()));
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
    //   重新累积，不会把编辑前旧方向也计入）。§29.5-W4：记账值 = 形参（采集时刻定格值）——同步路径
    //   形参即当前成员（同值，行为逐位不变）；异步路径应用时刻的成员可能已前移，误记会破量化门
    //  （把未烘进本网格的方向当已烘 → 太阳步进被跳过 → 软影陈旧）。
    m_lastBakedSunDir = bakedSunDir;
    m_lastBakedDayMul = bakedDayMul; // PLAN §2-H：记录「本次实际烘进顶点色的 dayMul」，下次 setDayMul 据此判量化门
    m_lastSunBakeNs = FrameProfiler::nowNs();
    m_deferredRebuild = false; // t972：本次重建已覆盖窗外欠账（内容 + 光照同源同烘）
    m_lightStale = false;

    // 可观测性（dev-spec t03 / t155 验收）：dirty = 编辑 / 初次加载即时重建（同步于 setBlock，破/放后当帧）；
    //   sun = 太阳跨步全量重建（绕 dirty，t155 编辑活跃期被 WorldClock 节流跳过）；water = 水段切换。
    //   读此日志可核对：破/放后立刻见 dirty 重建（无 3-4s 残留），编辑密集段无 sun 重建抢帧。
    static const char *const kReasonName[] = {"dirty", "sun", "water"};
    qInfo("vo.render: chunk(%d,%d) rebuilt [%s] - %lld verts / %lld idx (%lldus)",
          m_cx, m_cz, kReasonName[int(reason)], qint64(mesh.vertices.size()), qint64(mesh.indices.size()), qint64(bt.nsecsElapsed() / 1000));

    // 通知 F3 叠层刷新（顶点 / 三角面数已更新；t10）。
    emit meshRebuilt();
}
