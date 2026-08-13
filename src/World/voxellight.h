#ifndef VOXELLIGHT_H
#define VOXELLIGHT_H

#include <QtGlobal>  // quint8 / quint32 不直接用，留作类型上下文
#include <QVector3D>

#include <algorithm> // std::clamp
#include <cmath>     // std::sqrt / std::floor

#include "world.h" // heightmapAt / skyLightAt / blockLightAt（World 层；本头属 World 层）

// 体素光照 / 软影采样（t153 PCF 软影 + t151 真光场 顶点色）：mesher（chunkgeometry）与
// 掉落沙方块（BlockCube，t257）共用同一套「heightmap 正交深度图 PCF 软影 + 单格光场 max(sky,block)」
// 实现 —— 单点定义，杜绝「两处各持魔数 / 各写一遍 PCF 循环、调参漂移」（lessons-learned：成对契约 +
// 复用既有模式；同图集 N、tileIndex「单一权威」同族教训）。
//
// 分层（PLAN §2 铁律：依赖只向下）：本头属 World 层（只读 World::heightmapAt / skyLightAt / blockLightAt，
// 不反向写栅格）。
//   - chunkgeometry（src/World/，mesher）同层 include —— 合规。
//   - BlockCube（src/Renderer/）向下 include World —— 合规（Renderer 高于 World）。
namespace VoxelLight {

// t153 PCF 软影调参（spec「kMaxShadow 短 / kSunMin 高」；方案③：t151 顶点光基底 + heightmap 正交深度图）。
//   见 sunShadow()。文件作用域单点定义，mesher 与 BlockCube 共用 → 调一处即全场景同步（含掉落沙）。
//   - kSunMin：太阳高度门（sunDir.y 下限）。太阳低于此（黎明/黄昏/夜间）不投影 → 避免低角度极长影扫出
//     世界；门偏高 → 仅近正午投影（用户嫌 t135「影一大坨」：高门 + 短步把影收紧到日中、贴近障碍）。
//   - kSunFade：门附近淡入淡出带宽。sunDir.y 量化跨步到门两侧时影因子平滑过 0，无突变跳变。
//   - kMaxShadow：投影步进上限（格）。短 → 影紧凑、计算省（每顶点 kMaxShadow×4 次 heightmap 查询），
//     且低角度时影被截断不无限延伸。
constexpr float kSunMin    = 0.30f; // ≈17.5° 仰角门（max 仰角 50°→sin=0.766；日中窗 [17.5°,50°]）
constexpr float kSunFade   = 0.10f; // 门附近 ±0.10 band 平滑淡入
//   t472 性能：4→2。每顶点 PCF 软影耗 kMaxShadow×4 次 heightmap 查询（4 步 × 2×2 PCF），是 mesh 重建
//   的主要 CPU 税（每顶点 16 次 → 8 次，减半）。影缩短到 2 格（仍贴障碍根脚可见，只是离障碍更近处才显影）；
//   PCF 2×2 软过渡保留，影边仍软。配合视距门控（远 chunk 不重建）+ sun 步进稀化，PCF 税进一步降。
constexpr int   kMaxShadow = 2;     // 步进上限 2 格（每顶点 2×4=8 次 heightmap 查询；影短促紧凑）

// t166 顶点色钳制（PLAN §2-H / §M）：未照明格（深洞无天光 / 无火把）的底亮度 —— 防纯黑撕裂、保留
//   微弱可辨识（MC 为纯黑，此处取小底兼顾可玩性；火把光池 0.93 与之强对比，洞穴暗 / 火把亮一目了然）。
//   kVcMax：满光封顶 1.0（NoLighting 无法 overbright，贴图原色即最亮）。mesher 与 BlockCube 共用 →
//   掉落沙与地形同亮度曲线（修「暗处挖底沙 → 掉落沙明显变亮」根因）。
constexpr float kVcMin = 0.08f; // 暗部地板最低亮度（洞穴/阴影最低，仍远低于火把光池 0.93 保持对比）
constexpr float kVcMax = 1.0f;

// t153 PCF 软影（方案③：t151 顶点光基底 + heightmap 正交深度图 PCF 0..1 软过渡）。
//   给定世界空间顶点 (wx,wy,wz)，沿太阳「水平方向」步进 kMaxShadow 格，逐步采样路径所过列的列顶实面
//   （= 该列正交深度；t360 列顶实面世界 y = heightmap + solidTopOffset，按方块真实模型高度：整立方 1.0 /
//   下半砖 0.5 / 合活版门 0.1875…，取代旧 heightmap+1.0 整格假设）。若列顶面高于太阳光线在该列的高度 → 该列遮挡。
//   PCF：每步采样路径点周围 2×2 最近整数列（floor/ceil）取平均 → 半格列贡献 0.5 遮挡，影边 0..1 软过渡
//   （非硬二值）。返回 [0,1] 软影因子（0=全亮、1=全影）。shadowsEnabled=false / world==null → 直接返 0
//   （跳过 PCF 提速；掉落沙未接 world 时退化为无软影的全亮基底）。
//
//   方向基底（不开 lit 红线）：顶点 vc 的天光分量由 t151 flood-fill 光场决定（开敞见天 / 洞穴暗），PCF 在此
//   基础上把「太阳被邻近高地遮挡」处再压暗；火把方光（blockLight）不受影（调用方取 max(sky*(1-sh), block)
//   保留）。昼夜乘子（dayMul）现烘进顶点色的**天空分量**（vertexLight 内 sky*(1-sh)*dayMul，PLAN §2-H 修复：
//   方块光时间不变），故影因子本身时间不变 —— 仅随 sunDir（量化跨步）变。
//   退化：太阳低于门 kSunMin（含夜间 sunDir.y<=0）→ 0；太阳近天顶（水平分量≈0）→ 退化不投影；门附近按
//   kSunFade 平滑淡入，防量化跨步时影突变。
inline float sunShadow(const World *world, const QVector3D &sunDir, bool shadowsEnabled,
                       float wx, float wy, float wz)
{
    if (!world) return 0.0f;
    if (!shadowsEnabled) return 0.0f;
    if (sunDir.y() <= kSunMin) return 0.0f;                        // 太阳低于门 → 不投影（黎明/黄昏/夜间）
    const float sh = std::sqrt(sunDir.x() * sunDir.x() + sunDir.z() * sunDir.z());
    if (sh < 1e-3f) return 0.0f;                                    // 太阳近天顶 → 影无方向感，退化不投影
    const float invSh = 1.0f / sh;
    const float hxp = sunDir.x() * invSh, hzp = sunDir.z() * invSh; // 水平面归一太阳方向
    const float vyp = sunDir.y() * invSh;                           // 单位水平距离的垂直爬升（= tan(仰角)）
    // 门附近窄带平滑淡入（防量化跨步影突变）：sunDir.y∈[kSunMin, kSunMin+kSunFade] → 0..1。
    const float elevFade = std::clamp((sunDir.y() - kSunMin) / kSunFade, 0.0f, 1.0f);
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
                // t360 列顶实面世界 y（按方块真实模型高度：下半砖 0.5 / 合活版门 0.1875 / 整立方 1.0…），
                //   取代旧「heightmap+1.0 整格」假设 —— 修下半砖 / 合活版门被当整格高投出整格黑影、邻地误暗。
                //   top<0（空列 / 越界）永不遮挡。
                const float top = world->columnTopSurfaceY(x0 + xi, z0 + zi);
                if (top >= 0.0f && top > rayY) ++occluded;
                ++total;
            }
        }
    }
    if (total == 0) return 0.0f;
    return (float(occluded) / float(total)) * elevFade;
}

// t151/t257 单格光照 → 顶点色值（mesher 立方面与 BlockCube 掉落沙共用同一光照公式 → 渲染一致）。
//   vc = clamp(max(sky/15 × (1 - 软影) × dayMul, block/15), kVcMin, kVcMax)。
//   sky/block 取自**邻格** (nx,ny,nz)（面所朝向外侧的空气格，同 chunk 立方面约定：面的可见光来自其前方
//   的光场）；软影采样于**该顶点的世界位** (wx,wy,wz)（per-vertex PCF，影边光栅化平滑）。world==null →
//   返 1.0（全亮，保 item entity / 手持 / 热栏图标未接 world 时的既有全亮行为，lessons-learned t144）。
//
// PLAN §2-H 不变量（夜间火把发光修复）：dayMul（昼夜天光乘子）只乘**天光分量** sky*(1-软影)，**绝不**
//   乘方块光分量 block。机制：方块光（火把/熔炉 BFS flood-fill）时间不变，昼夜只应调制天光；max 取大者
//   后钳制 → 火把光池（block/15≈0.93）在任何 dayMul 下都全亮发光，仅天光铺底（地表 / 洞穴顶）随昼夜变。
//   旧实现把 dayMul 留在 QML baseColor（baseColor×vertexColor），它会同时压暗 block 通道 → 夜间火把只剩
//   0.37；改把 dayMul 烘进天空分量、地形 baseColor 设白（mesher 与 BlockCube 共用本函数同步），方块光独立。
inline float vertexLight(const World *world, const QVector3D &sunDir, bool shadowsEnabled,
                         float dayMul,
                         int nx, int ny, int nz, float wx, float wy, float wz)
{
    if (!world) return 1.0f;
    const float nbSkyF = world->skyLightAt(nx, ny, nz) / 15.0f;
    const float nbBlockF = world->blockLightAt(nx, ny, nz) / 15.0f;
    const float shadow = sunShadow(world, sunDir, shadowsEnabled, wx, wy, wz);
    return std::clamp(std::max(nbSkyF * (1.0f - shadow) * dayMul, nbBlockF), kVcMin, kVcMax);
}

} // namespace VoxelLight

#endif // VOXELLIGHT_H
