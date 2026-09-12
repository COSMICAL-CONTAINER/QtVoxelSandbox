#include "world.h"

#include "blockregistry.h"
#include "frameprofiler.h" // perf：tick 函数计时进 w* 桶（诊断 WorldClock 10Hz 路径开销）

#include <QDebug>
#include <QElapsedTimer> // t155c：recomputeLightAround 计时（测每帧编辑光照开销）
#include <QRandomGenerator> // t385 天气态随机时长转换（运行期模拟；同 EntityManager 火 random extinguish）
#include <algorithm>
#include <array> // t564：placeStronghold 候选要塞坐标集（std::array<int,3>）
#include <cmath>
#include <queue>   // t151：recomputeLightField 的 BFS flood-fill 队列
#include <unordered_map> // t185 tickWaterFlow 的 adds 哈希表（key = 体素线性编码 → 新 level，多源取 min）
#include <unordered_set> // t221 tickWaterFlow 的 evapKeys 集合（本 tick 将退场的格 key，供扩散 pass 跳过）

// t425 perf：生长方块（作物 / 甘蔗 / 耕地 / 树苗）位置索引的坐标打包 / 解包 + 成员判定。
//   生长 tick（tickCropGrowth / tickSugarcaneGrowth / tickFarmlandHydration / tickSaplingGrowth）旧版每窗
//   **全图扫 W×D×H**（160×160×128 ≈ 3.3M 格 / 数十 ms）即便世界无任何生长方块也照扫 —— 在放大世界（t276
//   10×10 chunk）上成持续掉帧主因（suspect c/d：新扫描 + 甘蔗/耕地 tick 扫全图）。改维护一份「生长方格」位置
//   集合（m_growthCells），写入路径（setBlock/setBlockFromEntity/setWaterSilent/setVoxelIfAir）经 noteGrowthWrite
//   增量维护，生长 tick 改遍历该集合（O(生长格数) 而非 O(全图)）；集合空 → 各 tick 零开销早退。
//   打包布局同 packLeafCell（三轴各 16 位；世界 ≤160³ 远小于 16 位范围），定义在文件顶部供生长 tick（早于
//   packLeafCell 第 891 行）使用。
static inline quint64 packGrowthCell(int x, int y, int z)
{
    return (quint64(quint16(x)) << 32) | (quint64(quint16(y)) << 16) | quint64(quint16(z));
}
static inline void unpackGrowthCell(quint64 k, int &x, int &y, int &z)
{
    x = int(quint16(k >> 32));
    y = int(quint16(k >> 16));
    z = int(quint16(k));
}
// 是否「生长方块」（生长 tick 关心的类：作物 / 甘蔗 / 耕地 / 树苗 / t514 浆果丛）。只读 BlockRegistry 枚举。
static inline bool isGrowthBlock(quint8 id)
{
    using BR = BlockRegistry;
    return id == BR::WheatCrop || id == BR::CarrotCrop || id == BR::PotatoCrop
        || id == BR::Sugarcane || id == BR::Farmland || id == BR::Sapling
        || id == BR::SweetBerryBush; // t514：浆果丛生长 tick 据 m_growthCells 遍历（O(丛格数) 替代全图扫描）
}

// t149 海平面（水位）：worldgen 沙滩带 / 沙漠水位 / 填水 / 树·矿石阈值的单一权威常量。
//   spec 原文 waterLevel=8 是 t119 重定标（heightAt 3..11 → 16..40）**之前**的旧地形范围；
//   t119 后 heightAt∈[16,40]，8 < min(16) → 无任何列满足 h<8 → 填水为零、沙滩不可见。故按
//   重定标同源取 24：约 11% 低洼列被淹（散布湖泊），出生列(8,8) h=27>24 保持陆地、近处低洼可见
//   水域（同 seed 复算核对）。语义不变（海平面淹低洼），仅数值随地形重定标。
//   全程纯函数于 seed + heightAt（fbm）→ 同 seed 同水域 / 沙滩分布（PLAN §2-K）。
//   t307：地表基线 30→64（地表抬高至 ~64），水位同源抬高保持「低于基线 6 格」的相对几何不变
//   （低洼 hills 仍见水 / 沙滩带，比例同 t162/t274）→ 24→58。同 seed 仍确定（fbm 纯函数）。
//   t338：海 + 沙滩改为集中于一角（seaColumnHeight 四分之一圆盘缓坡，角点 seaFloor=waterLevel-6 → 边缘
//   waterLevel+1 干沙滩），不再「全域低洼列散水/散沙」。本常量仍是海平面 / 海底 / 沙滩阈值的单一权威
//   （fillWater 灌到 waterLevel；沙滩环 = waterLevel+1）。同 seed 仍确定（fbm + seaColumnHeight 纯函数）。
constexpr int kWaterLevel = 58;

World::World(QObject *parent) : QObject(parent)
{
    generate(); // 默认参数生成（静默，不 emit）
}

// t176 存档加载入口：重置到目标 seed 的零填充分区网格（不走 generate —— 由 WorldStore 写 chunk blob
//   覆盖）。recreate 把 25 chunk 全清零 + 全标脏（首帧重建）；buildPermutation 重建 Perlin 表使后续
//   heightAt 等查询用新 seed（一致性，虽加载路径主要靠存档而非 worldgen）。仅 emit seedChanged（dims 不变）；
//   **不** emit worldChanged（网格此时全空，finishLoad 写完 blob 后才统一触发重建，避免中间态重建浪费）。
void World::beginLoad(int seed)
{
    m_seed = seed;
    m_chunks.recreate(m_width, m_depth, m_height); // 零填充 + 全标脏（recreate 实现）
    buildPermutation();                            // 新 seed 的 Perlin 置换表（heightAt 查询一致性）
    m_biomeCache.clear(); // t905 perf：seed 换新 → 群系 memo 作废（懒重建；见 world.h m_biomeCache 注释）
    m_decayingLeaves.clear(); // t325 网格重置 → 渐进衰减队列作废（坐标已不指向当前栅格；防误清新世界叶）
    m_growthCells.clear();   // t425 网格重置 → 生长方格索引作废（finishLoad 写完 blob 后 rebuildGrowthCells 全图重建）
    m_waterCells.clear();    // perf：网格重置 → 流体方格索引作废（finishLoad 写完 blob 后 rebuildFluidCells 全图重建）
    m_lavaCells.clear();
    m_iceCells.clear();      // t495：网格重置 → 普通冰方格索引作废（finishLoad 写完 blob 后 rebuildIceCells 全图重建）
    m_fireCells.clear();     // t724：网格重置 → 火焰方格索引作废（finishLoad 写完 blob 后 rebuildFireCells 全图重建）
    m_burningCells.clear();  // t843：网格重置 → 燃烧侧表作废（燃烧态不进存档 → 读档自然熄灭，dev-spec 明示可接受）
    m_torchBurnout.clear();  // review26 #6：网格重置 → burnout 计数侧表作废（运行期瞬态不进存档，读档自然复位）
    m_powerDirty.clear();    // t656：网格重置 → 红石电力脏集作废（finishLoad 末全量重建红石族脏集）
    // 审查修 B5（t724-t729 复盘）：清要塞传送门坐标 —— 旧版只在世界生成（placeStronghold）记录，读档后
    //   残留上一世界坐标会让暗渊之眼（t729）朝错误方向飞；finishLoad 末 rebindStrongholdPortalFromVoxels
    //   从存档体素反推回写（若本世界确有要塞）。
    m_hasStronghold = false;
    m_spawnColX = -1; // t756：出生列同步清（防旧世界残留坐标误导出生定位）；finishLoad 末从存档体素重新解析
    m_spawnColZ = -1;
    // t1020：结构区域表同步清（防旧世界区域残留误导进入判定）；finishLoad 末 rebuildStructureRegions
    //   从 seed 纯算术重推导落表（错误中断路径 = 空表 → inside* 恒 false，安全）。
    for (int k = 0; k < StructureKindCount; ++k) m_structureRegions[k].clear();
    fluidActReset();         // t488：网格重置 → 活动盒作废（旧世界坐标不指向新栅格；finishLoad 置 dirty → 首次全量扫描兜底）
    gravLightReset();        // t933：网格重置 → 重力级联光照联合盒 / 批标志防御清（正常路径级联收尾已清；防任何中途路径残留批态）
    resetWeather(); // t385 加载存档 → 天气从 Clear 重起（防上一世界天气态残留）
    emit seedChanged();
}

// t176 存档加载收尾：WorldStore 写完所有 chunk blob 后调。逐 chunk 重算 heightmap（存档只存体素/光场，
//   heightmap 派生于体素）、标全脏（保 25 个 ChunkGeometry 都重建为加载地形）、emit worldChanged 触发
//   重建。recreate 已标脏，此处 markDirty 为防御（万一某 chunk 被中途 clearDirty）。
void World::finishLoad()
{
    for (int cz = 0; cz < m_chunks.chunksZ(); ++cz) {
        for (int cx = 0; cx < m_chunks.chunksX(); ++cx) {
            if (Chunk *c = m_chunks.chunk(cx, cz)) {
                c->recomputeAllHeightmaps();
                c->markDirty();
                // t972：blob 直写不经 ChunkManager::setBlock（固体写会顺带清 fluidOnlyDirty）→
                //   recreate 出厂 chunk 的 m_fluidOnlyDirty=true 原样带进本 worldChanged → t188
                //   流体专用跳过分支把 terrain/glass/ice 段的载入首建整段吞掉（窗内段靠
                //   setChunkInRange catch-up 巧合兜底、同 chunk 重进世界则显上一局 mesh）。
                //   载入 = 全内容换代（固液混合任意），「本窗只收流体写」假定不成立 → 显式清，
                //   与 generate 路径（worldgen 经 ChunkManager 写天然清）对齐，保全部段当帧重建。
                c->clearFluidOnlyDirty();
            }
        }
    }
    // r2-B2 读档机关态归一（按钮按下视觉弹起）：须在 emit worldChanged 之前跑 → mesh 一次建对（按下视觉
    //   不闪现）。见 normalizeLoadedMechanismState 头注释。
    normalizeLoadedMechanismState();
    emit worldChanged(); // 触发 25 个 ChunkGeometry 重建（terrain+water 两段）
    m_chunks.clearAllDirty();
    // t380：加载存档后置流体脏 —— 存档可能含未稳流场（玩家存档时水正流），首次流体 tick 重扫恢复流动。
    m_waterDirty = true;
    m_lavaDirty = true;
    // t425：存档 blob 由 WorldStore 直写 chunk（不经 World 写入路径 → noteGrowthWrite 不会捕获）→ 全图重建
    //   生长方格索引一次，使后续生长 tick 走 O(生长格数) 遍历而非全图扫描（一次性 3.3M 扫描在加载期可接受）。
    rebuildGrowthCells();
    // perf：同上 —— 存档 blob 直写不经写入路径 → noteFluidWrite 不会捕获 → 全图重建流体方格索引一次，
    //   使后续流体 tick（water/lava/ice）走 O(流体格数) 遍历而非全图扫描（一次性 3.3M 扫描在加载期可接受）。
    rebuildFluidCells();
    // t495：同上 —— 存档 blob 直写不经写入路径 → noteIceWrite 不会捕获 → 全图重建普通冰方格索引一次，
    //   使后续融化 tick（tickIceMelt）走 O(冰格数) 遍历而非全图扫描。
    rebuildIceCells();
    // t724：同上 —— 全图重建火焰方格索引一次，使后续 tickFire 走 O(火格数) 遍历而非全图扫描。
    rebuildFireCells();
    // t656：存档可能含红石电路（拉杆扳开 / 红石块 / 粉连线）→ 全图扫红石族格入电力脏集一次（一次性
    //   3.3M 扫描在加载期可接受），下一 tickRedstone 局部重算恢复电路态（灯亮 / 轨通电等）。稳态后
    //   脏集由编辑路径增量维护。
    {
        const int W2 = m_width, D2 = m_depth, H2 = m_height;
        for (int x = 0; x < W2; ++x)
            for (int z = 0; z < D2; ++z)
                for (int y = 0; y < H2; ++y)
                    if (isPowerFamilyBlock(m_chunks.blockAt(x, y, z)))
                        m_powerDirty.insert(packGrowthCell(x, y, z));
    }
    // 审查修 B5（t724-t729 复盘）：从体素反推要塞传送门坐标回写（存档不落盘 m_strongholdPortal* —— 旧版
    //   只在 generate 的 placeStronghold 记录，读档后丢失 / 陈旧）。同 rebuildFireCells 的一次性全图扫描
    //   模式（加载期可接受，非每 tick）。
    rebindStrongholdPortalFromVoxels();
    // t756：出生列同 B5 模式从存档体素重新解析 —— 存档不落盘 m_spawnCol*（generate 记录值只对新世界有效），
    //   且玩家可能已挖掉 / 改造原出生列（建房 / 种树）。须在 recomputeAllHeightmaps 之后（heightmapAt 判据
    //   依赖重算后的列顶）。一次性有界环扫（加载期可接受）。
    findSpawnColumn();
    // t1020：结构区域表重落表（同 B5 重推导口径 —— 候选选择是 seed 的纯函数（PLAN §2-K），读档后
    //   纯算术重推导 = 生成期同表；零序列化、零体素扫描，旧存档（t1020 前生成）立即可判）。
    rebuildStructureRegions();
}

// 审查修 B5（t724-t729 复盘）：读档后从体素反推要塞末地传送门中心格回写 m_strongholdPortal*。旧版三坐标
//   只在世界生成（placeStronghold）记录 → 读档（beginLoad+finishLoad）后丢失或残留旧世界坐标，暗渊之眼
//   （t729）飞错方向 / 误走「无要塞」直飞兜底。扫描对象 = 末地传送门框架（EndPortal=111）而非激活门面
//   （EndPortalSurface=131）：门面只在 12 框架全激活后存在（t664），未激活要塞扫门面必漏 → 扫框架则任何
//   存档都能反推（框架 worldgen 必放、生存不可挖，见 blockregistry.h EndPortal 注释）。框架环足迹 5×5：
//   就近并入同簇（XZ 落簇 bbox 外扩 3 内且同层 ±1），取「格数最多、平局取距世界中心（=出生点）最近」的簇
//   （同 placeStronghold 的 best 选择语义；创造玩家自建框架环属边缘情形，多格优先 + 近出生点优先大体还原
//   worldgen 选择）。簇 bbox 中心 = 环中心：标准环相对坐标 x∈[-2,2] / z∈[-20,-16] → 中心 (0,-18)，y=框架
//   层 = 生成期记录的 cy+4，与 placeStronghold 写入值一致。≥3 格才算环（低于此视为残骸 / 散置框架不绑定）。
//   一次性全图扫描（~数 M blockAt），同 rebuildFireCells 先例；直读 m_chunks（finishLoad 时机无信号语义，
//   同 rebuildFireCells 约定）。
void World::rebindStrongholdPortalFromVoxels()
{
    // 审查修 L14：簇的 y 判据从「与簇基准 c.y（首帧层、合并从不更新）差 ≤1」改为「与簇 y 区间
    //   [minY,maxY] 外扩 1 相交」+ per-layer 计数（绑定层取帧数最多的众数层，平局取低层 —— y 升序扫
    //   首见层先入表，严格大于保先者，确定性）。旧版逐格 y 递变的多层框架环（玩家自建 / 旧存档地形
    //   变动的边缘情形）第 3 格起 |y−c.y|>1 → 分裂成多簇，best 可能取到 <3 格残簇直接漏绑。
    //   worldgen 12 框架全同层 → yCounts 单桶、绑定层 = 该层，行为不变。
    struct Cluster {
        int minX, maxX, minZ, maxZ, minY, maxY;
        int count;
        std::vector<std::pair<int, int>> yCounts; // (层 y, 该层帧数) —— 众数层作绑定层
    };
    std::vector<Cluster> clusters;
    const int W = m_width, D = m_depth, H = m_height;
    for (int y = 0; y < H; ++y) {
        for (int z = 0; z < D; ++z) {
            for (int x = 0; x < W; ++x) {
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::EndPortal) continue;
                // 就近并入既有簇（环 5×5 足迹：bbox 外扩 3 覆盖环内任意两框架间距；y 区间外扩 1 相交
                //   容层间 ≤1 的阶梯衔接 / 多层环 —— 审查修 L14）。
                bool merged = false;
                for (Cluster &c : clusters) {
                    if (y < c.minY - 1 || y > c.maxY + 1) continue;
                    if (x < c.minX - 3 || x > c.maxX + 3 || z < c.minZ - 3 || z > c.maxZ + 3) continue;
                    c.minX = std::min(c.minX, x); c.maxX = std::max(c.maxX, x);
                    c.minZ = std::min(c.minZ, z); c.maxZ = std::max(c.maxZ, z);
                    c.minY = std::min(c.minY, y); c.maxY = std::max(c.maxY, y);
                    ++c.count;
                    bool ySeen = false;
                    for (auto &p : c.yCounts) {
                        if (p.first == y) { ++p.second; ySeen = true; break; }
                    }
                    if (!ySeen) c.yCounts.push_back({ y, 1 });
                    merged = true;
                    break;
                }
                if (!merged) clusters.push_back({ x, x, z, z, y, y, 1, { { y, 1 } } });
            }
        }
    }
    // 选簇：格数最多优先（完整 12 环 > 残环 > 玩家散放），平局取距世界中心最近（同 placeStronghold 的
    //   bestIdx 语义 —— worldgen 放的就是距出生点最近那座）。bestCount 起 2 → 仅 ≥3 格的簇可入选。
    int best = -1;
    int bestCount = 2;
    double bestDistSq = 1e18;
    const double centerX = double(m_width) * 0.5, centerZ = double(m_depth) * 0.5;
    for (size_t i = 0; i < clusters.size(); ++i) {
        const Cluster &c = clusters[i];
        if (c.count < 3) continue;
        const double dx = (c.minX + c.maxX) * 0.5 - centerX;
        const double dz = (c.minZ + c.maxZ) * 0.5 - centerZ;
        const double d = dx * dx + dz * dz;
        if (c.count > bestCount || (c.count == bestCount && d < bestDistSq)) {
            best = int(i);
            bestCount = c.count;
            bestDistSq = d;
        }
    }
    if (best < 0) {
        m_hasStronghold = false; // 存档无要塞 / 框架已毁 → 无目标（掷眼走直飞兜底；beginLoad 已清，此处确认）
        qInfo() << "worldload: stronghold rebind -> none";
        return;
    }
    const Cluster &c = clusters[size_t(best)];
    m_hasStronghold = true;
    m_strongholdPortalX = (c.minX + c.maxX) / 2;
    // 绑定层 = 众数层（帧数最多的 y；平局取低层 —— 确定性，见函数头 L14 注释）。单层环（worldgen 全部
    //   情形）= 该层，与生成期记录的 cy+4 一致；多层环取主层。
    int bindY = c.minY;
    int bindCount = -1;
    for (const auto &p : c.yCounts) {
        if (p.second > bindCount) { bindCount = p.second; bindY = p.first; }
    }
    m_strongholdPortalY = bindY;
    m_strongholdPortalZ = (c.minZ + c.maxZ) / 2;
    qInfo() << "worldload: stronghold rebind ->" << m_strongholdPortalX << m_strongholdPortalY
            << m_strongholdPortalZ << "frames=" << c.count;
}

// t1000 成就「隔墙有眼」单一权威判定（契约见 world.h 声明处头注释）。结构原点 = m_strongholdPortal*
//   记录值反解（记录偏移常量 kStrongholdPortalDy/Dz 与 placeStronghold 记录处同源引用，防漂移）；足迹 =
//   水平 ±kStrongholdHalf（含墙环 cell）+ 竖直 [cy, cy+kStrongholdWallH+1]（含地板 / 顶板层）。连续坐标
//   floor 取整后逐轴闭区间（cell 口径与 placeStronghold 体素足迹一一对应）。m_hasStronghold=false 恒 false。
bool World::insideStronghold(double x, double y, double z) const
{
    if (!m_hasStronghold) return false;
    const int cx = m_strongholdPortalX;                            // 记录点 dx 偏移 0
    const int cy = m_strongholdPortalY - kStrongholdPortalDy;      // 记录 y = cy + 4 → 反解原点
    const int cz = m_strongholdPortalZ - kStrongholdPortalDz;      // 记录 z = cz - 18 → 反解原点
    const int bx = int(std::floor(x)), by = int(std::floor(y)), bz = int(std::floor(z));
    return bx >= cx - kStrongholdHalf && bx <= cx + kStrongholdHalf
        && bz >= cz - kStrongholdHalf && bz <= cz + kStrongholdHalf
        && by >= cy && by <= cy + kStrongholdWallH + 1;
}

// ── t1020 成就树扩展：四结构「进入区域」判定（契约见 world.h 声明处头注释）──

// inside* 四谓词共享实现：区域表线性扫描（每结构 ~0-8 个区域，O(区域数) 纯算术零栅格访问；每 tick
//   PlayerController 至多 4 次调用 × 4 谓词 —— 成本可忽略，同 insideStronghold O(1) 先例量级）。
//   cell 闭区间口径与足迹定义一一对应（连续坐标 floor 取整；同 insideStronghold）。
bool World::insideStructureRegion(int kind, double x, double y, double z) const
{
    if (kind < 0 || kind >= StructureKindCount) return false;
    const int bx = int(std::floor(x)), by = int(std::floor(y)), bz = int(std::floor(z));
    for (const StructureSite &s : m_structureRegions[kind]) {
        if (bx >= s.minX && bx <= s.maxX && by >= s.minY && by <= s.maxY
            && bz >= s.minZ && bz <= s.maxZ)
            return true;
    }
    return false;
}

bool World::insideDungeon(double x, double y, double z) const
{
    return insideStructureRegion(StructureDungeon, x, y, z);
}

bool World::insideMineshaft(double x, double y, double z) const
{
    return insideStructureRegion(StructureMineshaft, x, y, z);
}

bool World::insideDesertTemple(double x, double y, double z) const
{
    return insideStructureRegion(StructureDesertTemple, x, y, z);
}

bool World::insideJungleTemple(double x, double y, double z) const
{
    return insideStructureRegion(StructureJungleTemple, x, y, z);
}

// 区域表读取（矩阵探针 / F3 调试口径）。region = [minX,minY,minZ,maxX,maxY,maxZ]（cell 闭区间）。
int World::structureRegionCount(int kind) const
{
    if (kind < 0 || kind >= StructureKindCount) return -1;
    return int(m_structureRegions[kind].size());
}

QVariantList World::structureRegion(int kind, int index) const
{
    QVariantList out;
    if (kind < 0 || kind >= StructureKindCount) return out;
    if (index < 0 || index >= int(m_structureRegions[kind].size())) return out;
    const StructureSite &s = m_structureRegions[kind][size_t(index)];
    out << s.minX << s.minY << s.minZ << s.maxX << s.maxY << s.maxZ;
    return out;
}

// 结构区域表重建（generate 末 / finishLoad 末各调一次；先清后填幂等）。区域表 = 四结构候选表的直接
//   投影（候选表自带足迹闭区间）→ 「同 seed 同区域」由候选选择单源（sites() 上收）+ 纯函数性（PLAN
//   §2-K）双保险；旧存档（t1020 前生成）finishLoad 重推导后立即可判，无序列化迁移面。
void World::rebuildStructureRegions()
{
    for (int k = 0; k < StructureKindCount; ++k) m_structureRegions[k].clear();
    m_structureRegions[StructureDungeon] = dungeonSites();
    m_structureRegions[StructureMineshaft] = mineshaftSites();
    m_structureRegions[StructureDesertTemple] = desertTempleSites();
    m_structureRegions[StructureJungleTemple] = jungleTempleSites();
}

// 地牢候选表（选择段自 placeDungeons 原循环头逐字迁移 —— 概率 / 抖动 / margin / 海列 / 高度窗 /
//   房间尺寸位域全部同值同序；placeDungeons 几何与 rebuildStructureRegions 共同消费本表 → 选择单源）。
//   足迹 = 周界墙环闭区间 [cx-1, cx+roomW]×[cz-1, cz+roomD]×[cy-1, cy+kDungeonRoomH]（同 placeDungeons
//   步骤 1 周界遍历域；「进入结构」口径 = 踏进墙环即算，同 t1000 要塞墙环 cell 先例）。
std::vector<World::StructureSite> World::dungeonSites() const
{
    constexpr int kDungeonGrid  = 24;     // 候选网格间距（同 placeDungeons，t426 口径）
    constexpr unsigned kDungeonPct = 10u; // 候选命中概率（t426：10%）
    constexpr int kBedrockTop   = 4;      // 不动基岩（同 carveCaves / placeDungeons）
    constexpr int kSurfaceFloor = 6;      // 与地表保留的最小距离
    constexpr int kDungeonMaxY  = 36;     // 地牢最高 y（spec「地下」）
    constexpr int kRoomWMax     = 7;      // 房间最大内宽（kMargin 基准）
    constexpr int kMargin       = kRoomWMax + 1;
    std::vector<StructureSite> out;
    const int dungSeed = m_seed + kDungeonSeedOff;
    for (int bx = kDungeonGrid / 2; bx < m_width; bx += kDungeonGrid) {
        for (int bz = kDungeonGrid / 2; bz < m_depth; bz += kDungeonGrid) {
            const quint32 r = hashColumn(dungSeed, bx, bz);
            if ((r % 100u) >= kDungeonPct) continue; // 概率筛选
            const int span = kDungeonGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
                continue; // 留 margin 格边界
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠地牢
            const int h = std::min(heightAt(cx, cz), m_height - 1);
            const int yLo = kBedrockTop + 2;
            const int yHi = std::min(kDungeonMaxY - kDungeonRoomH, h - kSurfaceFloor - kDungeonRoomH);
            if (yHi <= yLo) continue; // 此列地下空间不足
            const int yRange = yHi - yLo + 1;
            const int cy = yLo + int((r >> 9) & 0x1Fu) % yRange; // 房间底面（地板）y
            const int roomW = ((r >> 28) & 1u) ? 5 : 7; // t995 内空宽 5/7（bit28）
            const int roomD = ((r >> 29) & 1u) ? 5 : 7; // t995 内空深 5/7（bit29）
            StructureSite s;
            s.cx = cx; s.cz = cz; s.y = cy;
            s.roomW = roomW; s.roomD = roomD; s.r = r; // 几何消费位域（刷怪笼权重 / 豁口数原样携带）
            s.minX = cx - 1; s.maxX = cx + roomW;
            s.minZ = cz - 1; s.maxZ = cz + roomD;
            s.minY = cy - 1; s.maxY = cy + kDungeonRoomH;
            out.push_back(s);
        }
    }
    return out;
}

// 废弃矿井候选表（选择段自 placeMineshaft 原循环头逐字迁移，同 dungeonSites 单源口径）。
//   足迹 = 「起点厅 + 巷道最大延伸」包络闭区间。L 形两腿**垂直**（review0907 低 #3 勘误：旧注释按
//   两腿共线 5+10+10 推出 26，把腿 B 的延伸错加到腿 A 轴上）：腿 A 自起点厅房缘（正向 cx+5 /
//   负向 cx−6）沿一轴推进 lenA−1 ≤ 9 → 轴向最远 6+9 = 15；腿 B 自肘点沿**另一轴**推进 lenB ≤ 10
//   （另一轴上腿 A 段自身已覆盖 15）；蛛网支廊垂直向 ≤ snl+1 = 7。水平半边 = 15 + 1 墙环 = 16；
//   竖直 [sy-6, sy+5]（斜坡段地板每 2 步降 1、≤10 步 → 最低 sy-5，含下壁 -6；拱带 / 支撑柱冠最高
//   sy+4，含顶 +5）。口径 = t1000 要塞 45×45 外圈足迹同款「结构区域足迹含墙环实心」—— 包络内巷道间
//   实心岩同算入区（登记：不逐 piece 精确，进出起点厅必触发）。包络收窄使「废矿来客」触发 bbox 与
//   矿井氛围音 zone 门控同源收窄（预期效果）；区域表纯重推导，旧档无迁移面。
std::vector<World::StructureSite> World::mineshaftSites() const
{
    constexpr int kMineshaftGrid    = 36;     // 候选网格间距（同 placeMineshaft）
    constexpr unsigned kMinePct     = 40u;    // 候选命中概率
    constexpr int kBedrockTop       = 4;      // 不动基岩
    constexpr int kSurfaceFloor     = 6;      // 与地表保留的最小距离
    constexpr int kMineshaftMaxY    = 48;     // 矿井最高 y（spec「Y<50」）
    constexpr int kRoomH            = 4;      // 矿井高度预算（y 范围公式沿用 t565 口径）
    constexpr int kMargin           = 16;     // 留边界（同 placeMineshaft）
    constexpr int kEnvHalf          = 16;     // 巷道包络半边（负向腿最远 6 + lenA-1 9 = 15，+1 墙环 = 16；L 形垂直两腿，推导见函数头）
    constexpr int kEnvYLoDrop       = 6;      // 包络竖直下探（斜坡最低 sy-5 + 下壁 1）
    constexpr int kEnvYHiLift       = 5;      // 包络竖直上探（拱带 sy+4 + 顶 1）
    std::vector<StructureSite> out;
    const int mineSeed = m_seed + kMineshaftSeedOff;
    for (int bx = kMineshaftGrid / 2; bx < m_width; bx += kMineshaftGrid) {
        for (int bz = kMineshaftGrid / 2; bz < m_depth; bz += kMineshaftGrid) {
            const quint32 r = hashColumn(mineSeed, bx, bz);
            if ((r % 100u) >= kMinePct) continue; // 概率筛选
            const int span = kMineshaftGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
                continue; // 留 margin 边界
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠矿井
            const int h = std::min(heightAt(cx, cz), m_height - 1);
            const int yLo = kBedrockTop + 2;
            const int yHi = std::min(kMineshaftMaxY - kRoomH - 1, h - kSurfaceFloor - kRoomH - 1);
            if (yHi <= yLo) continue; // 此列地下空间不足
            const int yRange = yHi - yLo + 1;
            const int sy = yLo + int((r >> 9) & 0x1Fu) % yRange; // 地板 y（起点厅 / 巷道底面）
            StructureSite s;
            s.cx = cx; s.cz = cz; s.y = sy;
            s.minX = cx - kEnvHalf; s.maxX = cx + kEnvHalf;
            s.minZ = cz - kEnvHalf; s.maxZ = cz + kEnvHalf;
            s.minY = sy - kEnvYLoDrop; s.maxY = sy + kEnvYHiLift;
            out.push_back(s);
        }
    }
    return out;
}

// 沙漠神殿落位五守卫（t1010 siteOk lambda 上收方法：margin / 群系 / 海域 / 塔顶越界 / 密室贴基岩，
//   同序同语义逐字迁移；概率主路径（desertTempleSites）/ 保底补座（同函数）/ tryPlace 落位（place*）
//   三路共用 → 落位判据永不漂移）。
bool World::desertTempleSiteOk(int cx, int cz) const
{
    constexpr int kMargin = kDesertTempleHalf + 1; // 留边界（金字塔 + 密室半径 ≤ margin 不越界）
    constexpr int kBedrockTop = 4;                 // 不动基岩顶（同 carveCaves / placeDungeons / placeMineshaft）
    if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
        return false; // 留 margin 边界
    // 仅 Desert 群系（spec「沙漠群系生成」；biomeAt 收口单一权威）。非沙漠 → 拒。
    if (!isDesert(cx, cz)) return false;
    if (seaColumnHeight(cx, cz) >= 0) return false;            // 海域不叠神殿（避免与海水柱冲突）
    const int surfaceY = std::min(heightAt(cx, cz), m_height - 1);
    if (surfaceY + kDesertTempleTopLayer >= m_height) return false; // 塔顶越界保护（surfaceY 异常高防溢出）
    const int floorY = surfaceY - kDesertTempleChamberDrop;    // 密室地板 y（TNT 层）
    return floorY - 1 >= kBedrockTop + 1;                      // 密室地板贴基岩 → 拒（保地板完整）
}

// 沙漠神殿候选表（t485 主路径 + t1010 保底补座选择段自 placeDesertTemple 逐字迁移；tryPlace 落位几何
//   留在 placeDesertTemple 消费本表）。足迹 = 金字塔 + 地下密室联合 bbox：水平 [cx±kDesertTempleHalf]
//   （21×21 外圈）、竖直 [surfaceY-kDesertTempleChamberDrop-1, surfaceY+kDesertTempleTopLayer]（密室
//   壳底到顶冠；机制等价 MC 1.0 advancement 结构 bbox —— 踏进金字塔 / 密室任一即算进入神殿）。
std::vector<World::StructureSite> World::desertTempleSites() const
{
    constexpr int kTempleGrid     = 48;     // 候选网格间距（比矿井 36 更稀 → 神殿更稀有；spec「低频」）
    constexpr unsigned kTemplePct = 45u;    // 候选命中概率（仅沙漠候选 → 已天然稀有）
    std::vector<StructureSite> sites;
    const int templeSeed = m_seed + kDesertTempleSeedOff;

    // 主路径（t485 原口径逐字保留）：48 格网格候选 × 45% 命中 × 抖动 ±12 → 过 siteOk 五守卫入选。
    for (int bx = kTempleGrid / 2; bx < m_width; bx += kTempleGrid) {
        for (int bz = kTempleGrid / 2; bz < m_depth; bz += kTempleGrid) {
            const quint32 r = hashColumn(templeSeed, bx, bz);
            if ((r % 100u) >= kTemplePct) continue; // 概率筛选
            const int span = kTempleGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (!desertTempleSiteOk(cx, cz)) continue;
            StructureSite s;
            s.cx = cx; s.cz = cz; s.y = std::min(heightAt(cx, cz), m_height - 1);
            s.minX = cx - kDesertTempleHalf; s.maxX = cx + kDesertTempleHalf;
            s.minZ = cz - kDesertTempleHalf; s.maxZ = cz + kDesertTempleHalf;
            s.minY = s.y - kDesertTempleChamberDrop - 1; s.maxY = s.y + kDesertTempleTopLayer;
            sites.push_back(s);
        }
    }

    // t1010 保底（review0905 #3 口径升级：触发门从「全世界 0 座」改「出生圈 R 内 0 座」）。原口径：沙漠
    //   神殿网格稀（160² 仅 3×3=9 网格候选）× 45% 命中 × Desert 群系占比 ~15-20%（成片）→ 期望 ~0.7
    //   座/世界，近半数含沙漠世界 0 落位（用户实测「新世界未见到神殿」根因：联合概率过低，非放置静默
    //   失败 —— 地表 y 取 heightAt 与地形填充同源，R19.19 的 64 高错位仅存在于 rig 探针侧）。但
    //   「placed == 0」只兜全世界 0 座 —— 主路径恰在世界角落落 1 座时保底不触发，出生区（世界中心游走
    //   圈）仍 0 神殿，用户主诉「所在地即 Desert 却从未见神殿」只解了一半。现口径：主路径最近座距世界
    //   中心 > kTempleSpawnGuaranteeRadius（world.h 单一权威，56 覆盖出生游走圈）且世界含 Desert 群系
    //   → 全图合格列（同 siteOk 五守卫）选距世界中心最近补座 ≥1；补座选列逻辑不变（已选距中心最近合格
    //   列 → 只要 R 内存在合格列，补座天然落 R 内）。确定性纯函数（同 seed 同点位，PLAN §2-K）→「出生
    //   圈内有沙漠群系必见神殿」。中心距离并列 → 扫描序（x 外 z 内）先到者胜，仍确定。极小世界（边长 <
    //   2×margin，或 R 超半边 → 门恒开）如实 0 / 退化为旧口径，登记。
    //   t1020 迁移注：主路径入选表空 ⟺ 距离门取 +∞（保底必触发），语义同位。
    constexpr bool kDesertBiomeGuarantee = true; // 阴性轮钉：置 false → 含沙漠世界可出生区 0 落位 → P-t1010b 必落腿红
    const double centerX = double(m_width) * 0.5, centerZ = double(m_depth) * 0.5;
    double nearestCenterSq = 1e18; // 主路径最近座到中心距离平方（入选表空 → +∞ → 保底必触发）
    for (const StructureSite &s : sites) {
        const double dx = double(s.cx) - centerX, dz = double(s.cz) - centerZ;
        nearestCenterSq = std::min(nearestCenterSq, dx * dx + dz * dz);
    }
    const double guaranteeRSq = double(kTempleSpawnGuaranteeRadius) * double(kTempleSpawnGuaranteeRadius);
    if (nearestCenterSq > guaranteeRSq && kDesertBiomeGuarantee) {
        bool hasDesert = false; // 群系在场扫描（biomeAt 列级 memo，generate 主循环已填满 → 纯数组读）
        for (int x = 0; x < m_width && !hasDesert; ++x)
            for (int z = 0; z < m_depth && !hasDesert; ++z)
                hasDesert = isDesert(x, z);
        if (hasDesert) {
            int bestX = -1, bestZ = -1;
            double bestDistSq = 1e18;
            for (int x = 0; x < m_width; ++x)
                for (int z = 0; z < m_depth; ++z) {
                    if (!isDesert(x, z)) continue; // 先群系短门（memo 读）→ 贵守卫（海 / 高度 fbm）只跑沙漠列
                    if (!desertTempleSiteOk(x, z)) continue;
                    const double dx = double(x) - centerX, dz = double(z) - centerZ;
                    const double d = dx * dx + dz * dz;
                    if (d < bestDistSq) { bestDistSq = d; bestX = x; bestZ = z; } // 距中心最近（同要塞口径）
                }
            if (bestX >= 0) { // 极小世界（边长 < 2×margin）可无合格列 → 如实 0
                StructureSite s;
                s.cx = bestX; s.cz = bestZ; s.y = std::min(heightAt(bestX, bestZ), m_height - 1);
                s.minX = bestX - kDesertTempleHalf; s.maxX = bestX + kDesertTempleHalf;
                s.minZ = bestZ - kDesertTempleHalf; s.maxZ = bestZ + kDesertTempleHalf;
                s.minY = s.y - kDesertTempleChamberDrop - 1; s.maxY = s.y + kDesertTempleTopLayer;
                sites.push_back(s);
            }
        }
    }
    return sites;
}

// 丛林神殿落位五守卫（t1010 siteOk lambda 上收方法：margin / 群系 / 海域 / 屋顶越界 / 地板贴基岩，
//   同序同语义逐字迁移；三路共用口径同 desertTempleSiteOk）。
bool World::jungleTempleSiteOk(int cx, int cz) const
{
    constexpr int kMargin = kJungleTempleHalf + 1; // 留边界（建筑半径 ≤ margin 不越界）
    constexpr int kBedrockTop = 4;                 // 不动基岩顶（同 carveCaves / placeDungeons / placeDesertTemple）
    if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
        return false; // 留 margin 边界
    // 仅 Jungle 群系（spec「丛林群系生成」；biomeAt 收口单一权威）。非丛林 → 拒。
    if (biomeAt(cx, cz) != Biome::Jungle) return false;
    if (seaColumnHeight(cx, cz) >= 0) return false;            // 海域不叠神殿（避免与海水柱冲突）
    const int surfaceY = std::min(heightAt(cx, cz), m_height - 1);
    if (surfaceY + kJungleTempleRoofY + 1 >= m_height) return false; // 屋顶越界保护（roof y=S+11 + 头部余量）
    return surfaceY >= kBedrockTop + 1;                        // 地板贴基岩 → 拒
}

// 丛林神殿候选表（t486 主路径 + t1010 保底补座选择段自 placeJungleTemple 逐字迁移）。足迹 =
//   苔石建筑 bbox：水平 [cx±kJungleTempleHalf]（15×15）、竖直 [surfaceY, surfaceY+kJungleTempleRoofY]
//   （地板层到屋顶层；建筑坐于地表无地下基座 —— t1004 登记口径）。
std::vector<World::StructureSite> World::jungleTempleSites() const
{
    constexpr int kTempleGrid     = 40;     // 候选网格间距（略密于沙漠神殿 48 → 丛林群系本身较稀有，补偿密度）
    constexpr unsigned kTemplePct = 50u;    // 候选命中概率（仅丛林候选 → 已天然稀有；spec「低频」）
    std::vector<StructureSite> sites;
    const int templeSeed = m_seed + kJungleTempleSeedOff;

    // 主路径（t486 原口径逐字保留）：40 格网格候选 × 50% 命中 × 抖动 ±10 → 过 siteOk 五守卫入选。
    for (int bx = kTempleGrid / 2; bx < m_width; bx += kTempleGrid) {
        for (int bz = kTempleGrid / 2; bz < m_depth; bz += kTempleGrid) {
            const quint32 r = hashColumn(templeSeed, bx, bz);
            if ((r % 100u) >= kTemplePct) continue; // 概率筛选
            const int span = kTempleGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (!jungleTempleSiteOk(cx, cz)) continue;
            StructureSite s;
            s.cx = cx; s.cz = cz; s.y = std::min(heightAt(cx, cz), m_height - 1);
            s.minX = cx - kJungleTempleHalf; s.maxX = cx + kJungleTempleHalf;
            s.minZ = cz - kJungleTempleHalf; s.maxZ = cz + kJungleTempleHalf;
            s.minY = s.y; s.maxY = s.y + kJungleTempleRoofY;
            sites.push_back(s);
        }
    }

    // t1010 保底（review0905 #3 口径升级，同 desertTempleSites 注）：丛林神殿 4×4=16 候选 × 50% 命中
    //   × Jungle 占比 ~13.5%（成片）→ 期望 ~1.1 座/世界。旧「placed == 0」只兜全世界 0 座 —— 主路径在
    //   世界角落落座时出生区仍 0 神殿。现口径：主路径最近座距世界中心 > kTempleSpawnGuaranteeRadius
    //   （world.h 单一权威）且世界含 Jungle 群系 → 全图合格列（同 siteOk 五守卫）选距世界中心最近补座
    //   ≥1：确定性纯函数（同 seed 同点位）→「出生圈内有丛林群系必见神殿」。极小世界（边长 < 2×margin，
    //   或 R 超半边 → 门恒开退化为旧口径）可无合格列 → 如实 0（登记）。
    //   t1020 迁移注：主路径入选表空 ⟺ 距离门取 +∞（同 desertTempleSites 口径）。
    constexpr bool kJungleBiomeGuarantee = true; // 阴性轮钉：置 false → 含丛林世界可出生区 0 落位 → P-t1010b 必落腿红
    const double centerX = double(m_width) * 0.5, centerZ = double(m_depth) * 0.5;
    double nearestCenterSq = 1e18; // 主路径最近座到中心距离平方（入选表空 → +∞ → 保底必触发）
    for (const StructureSite &s : sites) {
        const double dx = double(s.cx) - centerX, dz = double(s.cz) - centerZ;
        nearestCenterSq = std::min(nearestCenterSq, dx * dx + dz * dz);
    }
    const double guaranteeRSq = double(kTempleSpawnGuaranteeRadius) * double(kTempleSpawnGuaranteeRadius);
    if (nearestCenterSq > guaranteeRSq && kJungleBiomeGuarantee) {
        bool hasJungle = false; // 群系在场扫描（biomeAt 列级 memo → 纯数组读）
        for (int x = 0; x < m_width && !hasJungle; ++x)
            for (int z = 0; z < m_depth && !hasJungle; ++z)
                hasJungle = biomeAt(x, z) == Biome::Jungle;
        if (hasJungle) {
            int bestX = -1, bestZ = -1;
            double bestDistSq = 1e18;
            for (int x = 0; x < m_width; ++x)
                for (int z = 0; z < m_depth; ++z) {
                    if (biomeAt(x, z) != Biome::Jungle) continue; // 先群系短门（memo 读）→ 贵守卫只跑丛林列
                    if (!jungleTempleSiteOk(x, z)) continue;
                    const double dx = double(x) - centerX, dz = double(z) - centerZ;
                    const double d = dx * dx + dz * dz;
                    if (d < bestDistSq) { bestDistSq = d; bestX = x; bestZ = z; } // 距中心最近（同要塞口径）
                }
            if (bestX >= 0) {
                StructureSite s;
                s.cx = bestX; s.cz = bestZ; s.y = std::min(heightAt(bestX, bestZ), m_height - 1);
                s.minX = bestX - kJungleTempleHalf; s.maxX = bestX + kJungleTempleHalf;
                s.minZ = bestZ - kJungleTempleHalf; s.maxZ = bestZ + kJungleTempleHalf;
                s.minY = s.y; s.maxY = s.y + kJungleTempleRoofY;
                sites.push_back(s);
            }
        }
    }
    return sites;
}

// t756 出生列确定性解析（机制等价 MC 1.0 spawn 搜索「自中心外扫找首个安全露天落点」；见头注释四守卫）。
//   根因背景：placeTrees 无出生邻域豁免，固定出生列 (80,80) 恰命中密度筛选即生树（种子 42 即中）；而出生链
//   只按 heightAt（纯 fBm 地表、不含树）贴 Y → 玩家脚底嵌树干 / 头部嵌树冠。此处改「假定出生列」为「选定
//   出生列」：树冠半径 2 会越过任何半径 1 的 worldgen 排除带（方案 B 弱点），选定法从栅格真值侧一次保证
//   「出生格 + 头部格 Air、脚下实体支撑且在真地表」。环序固定（r=0 中心起，环内 dx 外层 / dz 内层）→
//   同 seed 同出生列（PLAN §2-K，无运行期随机源）。分层（PLAN §2）：只读 heightAt / blockAt /
//   heightmapAt（m_chunks 直读，generate 末 / finishLoad 末两时机均无信号语义，同 rebuildFireCells 约定）。
void World::findSpawnColumn()
{
    m_spawnColX = -1; // 先复位（全图无合格列时保持 -1 → getter 回退世界中心列）
    m_spawnColZ = -1;
    const int cx = m_width / 2, cz = m_depth / 2;
    const int maxR = std::max(cx, cz); // 覆盖全图的最小环上界
    for (int r = 0; r <= maxR; ++r) {
        // 环遍历：整方形 [-r,r]² 内只走 chebyshev 距离 == r 的边环（r=0 退化为中心列）。
        for (int dx = -r; dx <= r; ++dx) {
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                const int x = cx + dx, z = cz + dz;
                if (x < 0 || z < 0 || x >= m_width || z >= m_depth) continue; // 环越界列跳过
                // 有效地表 = generate 地形填充同式（世界矮于 fBm 地表时钳到顶 —— 否则 blockAt 按越界读 Air，
                // 支撑判据全列误否决；测试小世界 96×96×48 即此情形，实体地形顶在 47 而裸 heightAt ~62..66）。
                const int h = std::min(heightAt(x, z), m_height - 1);
                if (h < 0) continue; // 防御：零高世界无地表
                const quint8 support = m_chunks.blockAt(x, h, z);
                // (a) 实体支撑：完整立方（Grass/Dirt/Sand/Stone/Gravel/Snow…）或积雪层（Snowy 地表结构顶层
                //     SnowLayer 薄板可踩，同 placeTrees 的「草顶 / 雪顶」守卫口径）。树叶 / 原木 / 草丛 /
                //     水面薄物（睡莲）均非完整立方 → 天然否决「出生在树冠 / 洞顶上」。水下 / 湖列由 (b) 否决
                //     （水面占 h+1），沙滩干列可站属合法裸地表（机制等价 MC 出生不限群系）。
                if (!BlockRegistry::isFullCube(support) && support != BlockRegistry::SnowLayer) continue;
                // (b) 出生格 + 头部格全 Air（树干占 h+1、树冠占 h+1..h+3、草丛 / 花 / 水面占 h+1 的列全部
                //     落选）。越界格 blockAt 恒 Air → 世界顶之上的两格 = 开放天空，合法（小世界钳顶列）。
                if (m_chunks.blockAt(x, h + 1, z) != BlockRegistry::Air) continue;
                if (m_chunks.blockAt(x, h + 2, z) != BlockRegistry::Air) continue;
                // (c) 真地表：当前列首个非空恰为支撑本身 → 头顶无任何占位（洞口开口 / 悬浮雪 / 结构残留
                //     一票否决；(b) 的列级强化双保险 —— heightmap 由 setBlock 增量维护，树 / 草 / 玩家编辑均覆盖）。
                if (m_chunks.heightmapAt(x, z) != h) continue;
                m_spawnColX = x;
                m_spawnColZ = z;
                qInfo() << "worldgen: spawn column ->" << x << z << "surfaceY" << h;
                return;
            }
        }
    }
    qInfo() << "worldgen: spawn column -> none (getter falls back to center)";
}

// t425 perf：生长方格索引增量维护。写入路径（setBlock/setBlockFromEntity/setWaterSilent/setVoxelIfAir）在
//   m_chunks.setBlock 之后调本方法。id 不变（仅 state 变，如作物升阶段 / 耕地湿润度变）→ 成员资格不变 → no-op。
//   id 变更 → 按 oldId/newId 是否生长方块增删集合项。O(1) 哈希操作（编辑低频，开销可忽略），换得生长 tick
//   从 O(全图 3.3M) 降到 O(生长格数)。
void World::noteGrowthWrite(int x, int y, int z, quint8 oldId, quint8 newId)
{
    if (oldId == newId) return; // id 不变 → 成员资格不变（作物升阶段 / 耕地湿润度变均 id 不变）
    const quint64 k = packGrowthCell(x, y, z);
    if (isGrowthBlock(oldId)) m_growthCells.erase(k);
    if (isGrowthBlock(newId)) m_growthCells.insert(k);
}

// t425 perf：全图扫描重建生长方格索引。仅加载存档（finishLoad）调一次 —— 存档 blob 直写不经写入路径。
//   运行期由 noteGrowthWrite 增量维护，无需重扫。一次性 3.3M 扫描在加载期可接受（非每 tick）。
void World::rebuildGrowthCells()
{
    m_growthCells.clear();
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y)
                if (isGrowthBlock(m_chunks.blockAt(x, y, z)))
                    m_growthCells.insert(packGrowthCell(x, y, z));
}

// perf：流体方格集合增量维护（同 noteGrowthWrite 模式）。id 不变（仅 state 变，如水流 level 升降 /
//   岩浆流 level 变）→ 成员资格不变 → no-op。id 变更 → 按 oldId/newId 是否 Water/Lava 增删对应集合项。
//   O(1) 哈希操作（编辑 / 流体 tick 写入低频），换得流体 tick 从 O(全图 3.28M) 降到 O(流体格数)。
void World::noteFluidWrite(int x, int y, int z, quint8 oldId, quint8 newId)
{
    if (oldId == newId) return; // id 不变 → 成员资格不变（水流 level 变 / 岩浆流 level 变均 id 不变）
    const quint64 k = packGrowthCell(x, y, z); // 复用同一坐标打包（x<<32|y<<16|z）
    if (oldId == BlockRegistry::Water) m_waterCells.erase(k);
    else if (oldId == BlockRegistry::Lava) m_lavaCells.erase(k);
    if (newId == BlockRegistry::Water) m_waterCells.insert(k);
    else if (newId == BlockRegistry::Lava) m_lavaCells.insert(k);
}

// perf：全图扫描重建流体方格集合（generate / finishLoad 末调一次 —— worldgen / 存档 blob 直写不经写入路径）。
//   运行期由 noteFluidWrite 增量维护，无需重扫。一次性 3.3M 扫描在生成 / 加载期可接受（非每 tick）。
void World::rebuildFluidCells()
{
    m_waterCells.clear();
    m_lavaCells.clear();
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y) {
                const quint8 b = m_chunks.blockAt(x, y, z);
                if (b == BlockRegistry::Water) m_waterCells.insert(packGrowthCell(x, y, z));
                else if (b == BlockRegistry::Lava) m_lavaCells.insert(packGrowthCell(x, y, z));
            }
}

// t495 perf：普通冰（Ice=45）方格集合增量维护（同 noteFluidWrite 模式）。id 不变 → 成员资格不变 → no-op。
//   id 变更 → 按 oldId/newId 是否普通冰 Ice 增删集合项。仅 Ice=45 入集（PackIce/BlueIce 永不融化 → 不入集，
//   免 tickIceMelt 无谓扫描它们）。O(1) 哈希操作（编辑低频），换得融化 tick 从 O(全图 3.28M) 降到 O(冰格数)。
void World::noteIceWrite(int x, int y, int z, quint8 oldId, quint8 newId)
{
    if (oldId == newId) return; // id 不变 → 成员资格不变
    const quint64 k = packGrowthCell(x, y, z); // 复用同一坐标打包
    if (oldId == BlockRegistry::Ice) m_iceCells.erase(k);
    if (newId == BlockRegistry::Ice) m_iceCells.insert(k);
}

// t495 perf：全图扫描重建普通冰方格集合（generate / finishLoad 末调一次；运行期由 noteIceWrite 增量维护）。
//   一次性 3.3M 扫描在生成 / 加载期可接受（非每 tick）。仅 Ice=45 入集。
void World::rebuildIceCells()
{
    m_iceCells.clear();
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y)
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Ice)
                    m_iceCells.insert(packGrowthCell(x, y, z));
}

// t724 perf：火焰方格集合增量维护（同 noteIceWrite 模式，见 world.h 头注释）。id 不变 → no-op；
//   id 变更 → 按 oldId/newId 是否 Fire 增删集合项。O(1) 哈希操作，换得 tickFire 从 O(全图 3.28M)
//   降到 O(火格数)。
void World::noteFireWrite(int x, int y, int z, quint8 oldId, quint8 newId)
{
    if (oldId == newId) return; // id 不变 → 成员资格不变
    const quint64 k = packGrowthCell(x, y, z); // 复用同一坐标打包
    if (oldId == BlockRegistry::Fire) m_fireCells.erase(k);
    if (newId == BlockRegistry::Fire) m_fireCells.insert(k);
}

// t724 perf：全图扫描重建火焰方格集合（generate / finishLoad 末调一次；运行期由 noteFireWrite 维护）。
//   一次性 3.3M 扫描在生成 / 加载期可接受（非每 tick）。t843：兼清燃烧侧表（generate / 读档 = 世界重置，
//   运行期燃烧瞬态作废 = 读档自然熄灭）。
void World::rebuildFireCells()
{
    m_fireCells.clear();
    m_burningCells.clear();
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y)
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Fire)
                    m_fireCells.insert(packGrowthCell(x, y, z));
}

// r2-B2/B3 读档机关态归一：存档 chunk blob 持久化方块 id+state，但机关的**瞬态伴生表**（Game 层内存表，
//   如按钮按下倒计时 m_buttonRecoverCells）不进存档；实体（mob / 掉落物）也不进存档。两类读档陈旧态在此归一：
//   ① 按钮（r2-B2）：存档时按下窗内（bit0=1 落盘）→ 读档后无复位表项 → 永不自动弹回，且右键「bit0=1 拒绝
//      再按」→ 永久卡按下态（只能破块重放）。机制等价 MC 1.0 按钮是纯瞬态（存档按钮恒弹起）——归一为弹起。
//   ② 压力板（r2-B3）：存档时被 mob / 掉落物压着（bit0=1 落盘）→ 读档后实体不复活 → 无人清位 → 压下视觉
//      永残留（金板唯一触发源是掉落物 → 读档必陈旧）。归一为弹起；玩家 / 新 mob 仍站着 → 读档首 tick
//      updatePressurePlates 置回 bit0（B1 的基线抑制保证该次置位不产沿、不误触发陷阱）——视觉无感、语义正确。
//   全图扫一次（同 rebuildGrowthCells / rebuildFluidCells / rebuildIceCells 加载期一次性模式，非每 tick
//   的流体扫描反模式——成本仅在加载路径，~3.3M 格 blockAt）。**只清按钮 / 压力板的 bit0**：其余方块的 bit0
//   语义各异（红石灯开关态 = 玩家设置的持久态 / 探测铁轨「驶过」态 = 设计上持久 / 门半朝向编码 / 拉杆扳开 =
//   持续激活语义）——一律不动，防归一误伤非机关状态（两者 state 也只有 bit0 一位在用，清位即归零语义）。
//   直写 m_chunks.setBlock（id 不变只 state 变；不发信号 / 不 clearAllDirty——本方法在 finishLoad 的 emit
//   worldChanged 之前调，重建与清脏由 finishLoad 统一收口，同 worldgen 直写约定）。分层（PLAN §2）：World
//   层只读写 m_chunks + BlockRegistry 谓词，不依赖 Game。
void World::normalizeLoadedMechanismState()
{
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y) {
                const quint8 b = m_chunks.blockAt(x, y, z);
                const bool staleMech = BlockRegistry::isWoodButton(b) || BlockRegistry::isStoneButton(b)
                                    || BlockRegistry::isPressurePlate(b);
                if (!staleMech) continue;
                const quint8 st = m_chunks.stateAt(x, y, z);
                if (st & 1)
                    m_chunks.setBlock(x, y, z, b, quint8(st & quint8(~1))); // 清 bit0（弹起；id 不变）
            }
}

// t176 新世界生成：按 seed 全量 worldgen + emit。generate() 内部 recreate 网格（清上一世界残留），
//   故无需先 beginLoad。emit seedChanged（QML 绑定刷新）+ worldChanged（ChunkGeometry 重建）。
void World::regenerate(int seed)
{
    m_seed = seed;
    generate();
    emit seedChanged();
    emit worldChanged();
}

// review #20（出生点/进度组）：尺寸 setter 补 emit seedChanged —— 信号语义扩位为「世界内容整体换代」
//   （seed 数值变 **或** 尺寸重建）。消费端 PlayerController::onWorldSeedChanged 复位重生点正需要它：
//   尺寸 setter 走 generate 重选出生列（findSpawnColumn），但旧世界的派生缓存（重生点坐标）不再指向新
//   栅格，不 emit 则永留旧尺寸坐标（潜伏坑：生产路径 regenerate/beginLoad 均 emit 未踩中，setter 链
//   目前仅测试工具连调）。取舍——不引入节流 / 单一 worldReset 信号：①消费端仅 onWorldSeedChanged 一处
//   （幂等复位，零重建副作用；generate 本就每 setter 各跑一次，emit 不新增重建，三连发只是三次廉价复位）；
//   ②seedChanged 兼任 Q_PROPERTY seed 的 NOTIFY，值未变的额外通知只致绑定重求值同值（Qt 允许，无害）；
//   ③改单一 resize 入口要动 ~20 处测试调用点且无行为差异，不为潜伏坑扩 API 面。
void World::setWidth(int w)  { if (w == m_width)  return; m_width = w;  generate(); emit widthChanged();  emit seedChanged(); emit worldChanged(); }
void World::setDepth(int d)  { if (d == m_depth)  return; m_depth = d;  generate(); emit depthChanged();  emit seedChanged(); emit worldChanged(); }
void World::setHeight(int h) { if (h == m_height) return; m_height = h; generate(); emit heightChanged(); emit seedChanged(); emit worldChanged(); }
void World::setSeed(int s)   { if (s == m_seed)   return; m_seed = s;   generate(); emit seedChanged();   emit worldChanged(); }

quint8 World::blockAt(int x, int y, int z) const
{
    return m_chunks.blockAt(x, y, z); // 世界越界由 ChunkManager 判定 → 0(空气)
}

// 写栅格的唯一入口（PLAN §2-C 精神：GUI 线程单写者）。先读旧值判断有无变化（无变化不发信号，
// 避免误触发 mesh 重建 / 粒子），再经 ChunkManager 跨 chunk 写入 + 标脏（含边界邻接）。
// 信号语义不变：破带原 id、放带新 id；worldChanged 触发网格重建（本回合整个单 mesh）。
// t133：不改 state 语义 —— 此 4 参数版仅在 id 变化时走写入路径（经 ChunkManager 4 参数 → 委托
//   5 参数 (id,0)，新方块重置 state=0）。异形方块的 state 由 5 参数 setBlock 显式管理（下方）。
bool World::setBlock(int x, int y, int z, quint8 id)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    const quint8 oldId = m_chunks.blockAt(x, y, z);
    // t843：燃烧态清除放在无变化早退**之前**——任何对本格的显式写调用（含同 id no-op 写，如测试复原）
    //   都视为换上新方块实例 → 燃烧作废（栅格 id 从不因燃烧改变，早退路径不清则陈旧燃烧态挂在复原块上）。
    //   review25 #2：同 id 早退路径（oldId==id）无任何其它信号（无 broken/placed/worldChanged）→ 清表时
    //   补发 blockDoused 精确驱动 QML 面火 overlay 摘除（否则 delegate 泄漏成假燃块）；有变化路径随后必发
    //   broken/placed + worldChanged（cleanupVis 兜底），不重复发。
    const bool wasBurning = m_burningCells.remove(packGrowthCell(x, y, z)) > 0;
    if (oldId == id) {
        if (wasBurning) emit blockDoused(x, y, z);
        return false; // 无变化
    }
    m_chunks.setBlock(x, y, z, id); // 跨 chunk 写入 + 标目标脏 + 边界格标邻接脏（→5 参数 id,0 重置 state）
    noteGrowthWrite(x, y, z, oldId, id); // t425：维护生长方格索引（生长 tick 据 it 遍历，免全图扫描）
    noteFluidWrite(x, y, z, oldId, id);  // perf：维护流体方格索引（流体 tick 据它遍历，免全图扫描）
    noteIceWrite(x, y, z, oldId, id);    // t495：维护普通冰方格索引（融化 tick 据它遍历，免全图扫描）
    noteFireWrite(x, y, z, oldId, id);   // t724：维护火焰方格索引（tickFire 据它遍历，免全图扫描）
    qInfo("vo.edit: setBlock %d,%d,%d  %d->%d", x, y, z, int(oldId), int(id)); // t155f 诊断：编辑时序
    if (oldId != BlockRegistry::Air && id == BlockRegistry::Air)
        emit blockBroken(x, y, z, int(oldId)); // 破：带原方块 id（粒子/音效按它取色/取声）
    else if (id != BlockRegistry::Air)
        emit blockPlaced(x, y, z, int(id));    // 放：带新方块 id
    recomputeLightAround(x, y, z, oldId, id); // t154：增量重 flood 编辑格周围有界盒（替代全量 recomputeLightField）
    emit worldChanged(); // 触发 ChunkGeometry 重建（terrain+water 两段 dirty chunk 同步重建）
    m_chunks.clearAllDirty(); // t155g：两段都重建完，统一清脏（防一段 clearDirty 抢清致另一段跳过 = 2s 卡顿根因）
    // t273 修复(b)：放水源（id==Water）时 poke 水流节流计数 → 下次 tickWaterFlow 即蔓延（详见 5 参数 setBlock 同名注释）。
    if (id == BlockRegistry::Water)
        m_flowTickCounter = kFlowTickInterval - 1;
    // t343：放岩浆源（id==Lava）时 poke 岩浆流节流计数 → 下次 tickLavaFlow 即蔓延（机制同水 poke）。
    if (id == BlockRegistry::Lava)
        m_lavaFlowTickCounter = kLavaFlowTickInterval - 1;
    pokeFluidDirty(x, y, z); // t380：块编辑可能扰动邻接流体平衡 → 标流体脏（驱动 tickWaterFlow/tickLavaFlow 早退后重扫）
    checkCactusOnEdit(x, y, z, oldId, id); // t445：仙人掌失撑（②）/ 邻接方块（④）整柱坍落复检
    checkDeadBushOnEdit(x, y, z, oldId, id); // t504：枯死灌木失撑（破下方支撑 → 正上方枯灌木掉落）复检
    checkFlowerMushroomOnEdit(x, y, z, oldId, id); // t507：花 / 蘑菇失撑（破下方支撑 → 正上方花 / 蘑菇掉落）复检
    checkPressurePlateOnEdit(x, y, z, oldId, id); // t494：压力板失撑（破下方支撑 → 正上方压力板掉落）复检
    checkSugarcaneOnEdit(x, y, z, oldId, id); // t524：甘蔗失撑（破下方支撑 → 正上方甘蔗整柱坍落）复检
    checkSnowLayerOnEdit(x, y, z, oldId, id); // t527：积雪层失撑（破下方支撑 → 正上方积雪层整柱坍落为携带层数的下落实体）复检
    checkGravityBlockOnEdit(x, y, z, oldId, id); // t799：沙/沙砾失撑坍落复检（放置自检①+支撑变化②；4 参数放置/挖掘主入口）
    checkRailOnEdit(x, y, z, oldId, id);      // t565：铁轨连接重算（放 / 破 Rail 或其邻 → 本轨 + 邻轨连接位更新）
    checkEndPortalIntegrity(x, y, z, oldId, id); // t664：末地传送门完整性复检（框架破 → 门面消失）
    checkFireOnEdit(x, y, z); // t843：6 邻立地火失撑即时熄灭（破支撑 → 火当场灭；钩子族收口）
    // review #27：余烬门门框失撑熄灭并入写入钩子族（本入口无变化早退 → 此处 oldId != id 恒真）；破 / 置换
    //   本格非空内容 → 扫 6 邻门面熄灭（纯放置入 Air 格不触发，同玩家挖掘路径语义）。
    if (oldId != BlockRegistry::Air)
        breakNetherPortalsAround(x, y, z);
    checkPaintingSupportOnEdit(x, y, z, oldId, id); // t837①：画作支撑墙失撑 → 整画掉落（钩子族同口径）
    notePowerWrite(x, y, z, oldId, id);       // t656：红石电力脏标记（红石族编辑 / 邻粉 → 局部重算入队）
    return true;
}

// t133：世界坐标 state 读（跨 chunk 路由，经 ChunkManager）。越界 → 0。供 mesher / QML 查异形方块朝向。
quint8 World::stateAt(int x, int y, int z) const
{
    return m_chunks.stateAt(x, y, z);
}

// t151 光场读（PLAN §2-H / §M）。OOB 语义：y >= height = 开阔天空（天光 15，供顶面采样）/ 方块光 0；
//   y < 0 或 x/z 越界 → 0。in-bounds 经 ChunkManager 路由到 chunk 局部。mesher 经 m_world 调用。
quint8 World::skyLightAt(int x, int y, int z) const
{
    if (y >= m_height) return 15; // 世界顶之上 = 开阔天空（顶面 / 高墙顶采样得满天光）
    return m_chunks.skyLightAt(x, y, z); // 其余越界（y<0 / x/z 出界）→ ChunkManager 返回 0
}

quint8 World::blockLightAt(int x, int y, int z) const
{
    if (y >= m_height) return 0; // 世界顶之上无方块光（火把光不溢出世界）
    return m_chunks.blockLightAt(x, y, z);
}

// t360 列顶实面世界 y（见头注释）：heightmap + solidTopOffset(列顶方块)。空列 → -1。PCF 软影采样用。
//   委托 ChunkManager 单次 chunk 路由版（PCF 热路径，免 3 次重复 chunk 路由）。
float World::columnTopSurfaceY(int x, int z) const
{
    return m_chunks.columnTopSurfaceY(x, z);
}

// t146 给定格的碰撞 sub-AABB（世界坐标）。读 blockAt + stateAt → BlockRegistry::collisionAABBsInto 取
//   cell-local 子盒直写调用方缓冲 → 原地偏移到世界坐标（t859 起零堆分配，见 world.h 头注释；旧行为
//   「按值返回 vector + 内部再建 local vector」两次分配/查询退役）。越界 blockAt=0(air) → 0 盒。
//   玩家碰撞（PlayerController）逐格逐 sub-AABB 测试。同源 partialblockgeometry 的 state 解码
//   （碰撞形状 == 渲染形状）。
int World::collisionAABBsAt(int x, int y, int z, BlockRegistry::BlockAABB *out, int cap) const
{
    const quint8 id = m_chunks.blockAt(x, y, z);
    const quint8 st = m_chunks.stateAt(x, y, z);
    const int n = BlockRegistry::collisionAABBsInto(id, st, out, cap);
    const float fx = float(x), fy = float(y), fz = float(z);
    for (int i = 0; i < n; ++i) {
        out[i].minX += fx; out[i].minY += fy; out[i].minZ += fz;
        out[i].maxX += fx; out[i].maxY += fy; out[i].maxZ += fz;
    }
    return n;
}

// t865/t867 统一支撑顶面查询（头注释见 world.h）：碰撞 sub-AABB 真顶为单一权威 —— 整立方快路径
//   （isFullCube 且非 Farmland/附魔台矮盒特例）零 AABB 构建；SnowLayer 按 state 真顶（与盒同源的快路径
//   特例，免为薄层建盒）；其余（slab/stairs/fence/plate/door/trapdoor/bed/lily-pad/矮盒特例）走
//   BlockRegistry::collisionTopY（review26 #20 起免构建镜像 —— 只取 maxY 标量不建 vector，resting 掉落物
//   每帧两格窗复探在异形支撑上不再堆分配；等价性由矩阵探针钉死）；无碰撞格（轨/火把/花草/作物/火/水/岩浆）
//   盒空 → -1 不承载。
float World::supportTopYAt(int x, int y, int z) const
{
    const quint8 id = m_chunks.blockAt(x, y, z);
    if (id == BlockRegistry::Air) return -1.0f;
    if (id == BlockRegistry::SnowLayer)
        return float(y) + BlockRegistry::snowLayerHeight(m_chunks.stateAt(x, y, z));
    if (BlockRegistry::isFullCube(id) && id != BlockRegistry::Farmland
        && id != BlockRegistry::EnchantingTable)
        return float(y) + 1.0f;
    const float top = BlockRegistry::collisionTopY(id, m_chunks.stateAt(x, y, z));
    return (top < 0.0f) ? -1.0f : float(y) + top;
}

// t775 点级碰撞占据查询（头注释见 world.h）：取点所在格的碰撞 sub-AABB，任一盒严格包含该点 → true。
//   玩家（t160）与矿车骑乘（t775）窒息共用本判据（PlayerController tickSuffocation 单链，旧两处内联
//   收敛单一权威）。mob（t254）窒息**未迁移**：EntityManager 独立实现同判据（复审 #24 口径修正，
//   见 world.h 头注释）。
bool World::pointBlockedByCollision(float x, float y, float z) const
{
    // t859：out-param 版（栈上小缓冲，零堆分配；旧行为按值 range-for vector）。
    BlockRegistry::BlockAABB boxes[BlockRegistry::kMaxAABBsPerCell];
    const int n = collisionAABBsAt(int(std::floor(x)), int(std::floor(y)), int(std::floor(z)),
                                   boxes, BlockRegistry::kMaxAABBsPerCell);
    for (int i = 0; i < n; ++i) {
        const BlockRegistry::BlockAABB &b = boxes[i];
        if (x > b.minX && x < b.maxX && y > b.minY && y < b.maxY && z > b.minZ && z < b.maxZ)
            return true;
    }
    return false;
}

// t133：写 id + state + 标脏（含边界邻接）。变化判定含 state：oldId==id && oldState==state 才视为无变化
//   （id 不变只 state 变 —— 如 door/trapdoor 右键开合 —— 仍需重网格化，故走写入 + worldChanged）。
//   信号语义：仅 id 变化发 broken/placed；id 不变只 state 变不发（非破 / 放，是开合动作），仅 worldChanged。
bool World::setBlock(int x, int y, int z, quint8 id, quint8 state)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    const quint8 oldId = m_chunks.blockAt(x, y, z);
    const quint8 oldState = m_chunks.stateAt(x, y, z);
    // t843：燃烧态清除放在无变化早退之前（同 4 参数版：任何显式写调用 = 换新实例，燃烧作废）。
    //   review25 #2：同 id+state 早退路径无任何其它信号 → 清表时补发 blockDoused（同 4 参数版口径）。
    const bool wasBurning = m_burningCells.remove(packGrowthCell(x, y, z)) > 0;
    if (oldId == id && oldState == state) {
        if (wasBurning) emit blockDoused(x, y, z);
        return false; // id 与 state 均无变化
    }
    m_chunks.setBlock(x, y, z, id, state); // 跨 chunk 写 id+state + 标目标脏 + 边界格标邻接脏
    noteGrowthWrite(x, y, z, oldId, id); // t425：维护生长方格索引（生长 tick 据 it 遍历，免全图扫描）
    noteFluidWrite(x, y, z, oldId, id);  // perf：维护流体方格索引（流体 tick 据它遍历，免全图扫描）
    noteIceWrite(x, y, z, oldId, id);    // t495：维护普通冰方格索引（融化 tick 据它遍历，免全图扫描）
    noteFireWrite(x, y, z, oldId, id);   // t724：维护火焰方格索引（tickFire 据它遍历，免全图扫描）
    if (oldId != id) {
        // id 变化 → 发 broken/placed（同 4 参数语义：破带原 id、放带新 id）；id 不变只 state 变（门开合）不发。
        if (oldId != BlockRegistry::Air && id == BlockRegistry::Air)
            emit blockBroken(x, y, z, int(oldId));
        else if (id != BlockRegistry::Air)
            emit blockPlaced(x, y, z, int(id));
    }
    recomputeLightAround(x, y, z, oldId, oldState, id, state); // t334：传 state（活版门开合：id 不变但 lightOpacity 0↔15 翻转 → 须重 flood）
    emit worldChanged(); // 异形方块 state 变（开合 / 朝向）需 mesh 重建
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    // t273 修复(b)「放水后不立即流动」：玩家经桶 setBlock 放水源（Air→Water，或流水 state>0→水源升源）后，水流
    //   应下一 tick（~100ms）即开始蔓延，而非等节流计数残留最久 ~0.3s 才首格（用户观感「放完不动」）。把节流计数
    //   推到「下次 tickWaterFlow 即满足阈值」——其开头 `if (++m_flowTickCounter < kFlowTickInterval) return;`，故置
    //   kFlowTickInterval-1 使下次 ++ 后恰好达阈值、立即处理波前。仅 Water 触发（放水是流动的源事件；其余方块编辑与
    //   水流无关）。worldgen 填水走 m_chunks.setBlock 直写不经此 → 不受影响（生成期水域全源、稳态无蔓延需求）。此
    //   poke 不改后续蔓延节奏（仍 ~0.3s/格动画），只让首格即时（机制等价 MC 倒水即刻外溢）。舀水走 setWaterSilent
    //   不经此（舀水是退场、按既定节奏衰退即可，非本任务范围）。
    if (id == BlockRegistry::Water)
        m_flowTickCounter = kFlowTickInterval - 1;
    // t343：放岩浆源时 poke 岩浆流节流计数（机制同上方水 poke，让首格即时蔓延）。
    if (id == BlockRegistry::Lava)
        m_lavaFlowTickCounter = kLavaFlowTickInterval - 1;
    pokeFluidDirty(x, y, z); // t380：块编辑可能扰动邻接流体平衡 → 标流体脏
    checkCactusOnEdit(x, y, z, oldId, id); // t445：仙人掌失撑（②）/ 邻接方块（④）整柱坍落复检
    checkDeadBushOnEdit(x, y, z, oldId, id); // t504：枯死灌木失撑（破下方支撑 → 正上方枯灌木掉落）复检
    checkFlowerMushroomOnEdit(x, y, z, oldId, id); // t507：花 / 蘑菇失撑（破下方支撑 → 正上方花 / 蘑菇掉落）复检
    checkPressurePlateOnEdit(x, y, z, oldId, id); // t494：压力板失撑（破下方支撑 → 正上方压力板掉落）复检
    checkSugarcaneOnEdit(x, y, z, oldId, id); // t524：甘蔗失撑（破下方支撑 → 正上方甘蔗整柱坍落）复检
    checkSnowLayerOnEdit(x, y, z, oldId, id); // t527：积雪层失撑（破下方支撑 → 正上方积雪层整柱坍落为携带层数的下落实体）复检
    checkGravityBlockOnEdit(x, y, z, oldId, id); // t799：沙/沙砾失撑坍落复检（放置自检①+支撑变化②；5 参数放置/开合主入口）
    checkRailOnEdit(x, y, z, oldId, id);      // t565：铁轨连接重算（放 / 破 Rail 或其邻 → 本轨 + 邻轨连接位更新）
    checkEndPortalIntegrity(x, y, z, oldId, id); // t664：末地传送门完整性复检（框架破 → 门面消失）
    checkTrapdoorDoorSupportOnEdit(x, y, z, oldId, id); // t851：活板门 / 门失撑掉落复检（支撑被破 / 被换非实体 → 掉落成物品）
    checkFireOnEdit(x, y, z); // t843：6 邻立地火失撑即时熄灭（同 4 参数版钩子族收口）
    // review #27：余烬门门框失撑熄灭并入写入钩子族（同 4 参数版；state-only 写 oldId==id 不触发）。
    if (oldId != BlockRegistry::Air && oldId != id)
        breakNetherPortalsAround(x, y, z);
    checkPaintingSupportOnEdit(x, y, z, oldId, id); // t837①：画作支撑墙失撑 → 整画掉落（钩子族同口径；
    //   画格自清重入由 m_inRemovePainting 守卫，放置入 Air 格的画锚写天然 no-op——墙格恒非 Air）
    notePowerWrite(x, y, z, oldId, id);       // t656：红石电力脏标记（红石族编辑 / 邻粉 → 局部重算入队；state-only 写亦触发——拉杆 / 按钮翻位即此路径）
    return true;
}

// t117/t220 FallingBlock 着地专用：m_chunks.setBlock 直写 + emit worldChanged，不发 blockPlaced（与玩家放置
//   语义分离，沿用 worldgen 直写不触发 blockPlaced 的既有约定）。t220：仅在目标为**空气或水**时写入（着地格
//   由 FallingBlock 列扫保证为 air/水 —— 沙落水穿透后填堵水格；防御：其余已占用方块不覆盖）。越界 / 非空非水 → false。
bool World::setBlockFromEntity(int x, int y, int z, quint8 id)
{
    return setBlockFromEntity(x, y, z, id, quint8(0)); // 委托 5 参数版（state=0；沙/圆石着地不带 state）
}

// t527 积雪层下落实体着地专用（5 参数带 state；4 参数版委托）。同 setBlockFromEntity 语义，写带 state 的方块。
//   state=layers-1 保留层数（state 0..7 = 1..8 层）。occ 守卫同（仅 air/水可被着地覆盖）。越界 / 非空非水 → false。
bool World::setBlockFromEntity(int x, int y, int z, quint8 id, quint8 state)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    const quint8 occ = m_chunks.blockAt(x, y, z);
    if (occ != BlockRegistry::Air && occ != BlockRegistry::Water) return false; // 仅空气 / 水可被实体着地覆盖
    m_chunks.setBlock(x, y, z, id, state); // 跨 chunk 写 id+state + 标目标脏 + 边界格标邻接脏
    noteGrowthWrite(x, y, z, occ, id); // t425：维护生长方格索引（沙落覆盖作物 / 耕地时正确移除）
    noteFluidWrite(x, y, z, occ, id);  // perf：维护流体方格索引（沙落覆盖水时正确移除水格）
    noteIceWrite(x, y, z, occ, id);    // t495：维护普通冰方格索引（沙落覆盖冰时正确移除）
    noteFireWrite(x, y, z, occ, id);   // t724：维护火焰方格索引（沙落灭火时正确移除）
    recomputeLightAround(x, y, z, occ, id); // t154：增量重 flood（oldId=被覆盖的 air/水 → newId=id）
    emit worldChanged(); // 驱动 mesh 重建（不发 blockPlaced / blockBroken —— 系统事件非玩家动作）
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    pokeFluidDirty(x, y, z); // t380：沙着地可能覆盖水 / 邻接流体 → 标流体脏（驱动流体 tick 重扫）
    notePowerWrite(x, y, z, occ, id); // t656：红石电力脏标记（落体着地改变粉路通断 → 邻粉重算；红石族外 no-op）
    // review #27：余烬门门框失撑熄灭并入写入钩子族。本入口 occ 守卫限 Air/水 → 仅「沙落填水格」（水被
    //   置换出本格）触发；落进空气格不触发（纯放置语义）。
    if (occ != BlockRegistry::Air && occ != id)
        breakNetherPortalsAround(x, y, z);
    return true;
}

// rv-low-batch1 塌落雪层叠层合并专用（详见 world.h 头注释）：塌落 SnowLayer 落在另一 SnowLayer 上 →
//   合并层数覆盖写回（state=total-1）。仅 EntityManager FallingBlock(SnowLayer) 着地合并调。
bool World::setSnowLayerMerge(int x, int y, int z, quint8 state)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    if (m_chunks.blockAt(x, y, z) != BlockRegistry::SnowLayer) return false; // 防御：仅既有雪层可被合并
    const quint8 occ = BlockRegistry::SnowLayer;
    m_chunks.setBlock(x, y, z, BlockRegistry::SnowLayer, state); // 覆盖写 id+state + 标脏 + 边界邻接
    // 写后钩子：id 不变（SnowLayer→SnowLayer）→ 生长 / 流体 / 冰索引与光照均无变化（早退路径），但保持
    //   同族写入路径一致性（沙着地 / 点火清格均调全套钩子）。
    noteGrowthWrite(x, y, z, occ, BlockRegistry::SnowLayer);
    noteFluidWrite(x, y, z, occ, BlockRegistry::SnowLayer);
    noteIceWrite(x, y, z, occ, BlockRegistry::SnowLayer);
    noteFireWrite(x, y, z, occ, BlockRegistry::SnowLayer);
    recomputeLightAround(x, y, z, occ, BlockRegistry::SnowLayer);
    emit worldChanged(); // 驱动 mesh 重建（薄板高度随 state 变；不发 placed/broken —— 系统事件）
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    return true;
}

// t490fix 点火专用静默清方块（详见 world.h 头注释）：照搬 setBlockFromEntity 主体（同写后钩子），仅删掉
//   occ 守卫（无条件覆盖为 Air）。occ 仍读出作 oldId 传给 note / 光重算，保持生长 / 流体索引正确。越界 → false。
//   仅 playercontroller 3 处点火路径用（右键机关四邻 / 右键 TNT 本体 / 压力板四邻）。
bool World::clearBlockSilent(int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    const quint8 occ = m_chunks.blockAt(x, y, z); // 旧方块（作 oldId 传给 note / 光重算；不再守卫拒非空）
    const quint8 id = BlockRegistry::Air;
    m_chunks.setBlock(x, y, z, id); // 跨 chunk 写入 + 标目标脏 + 边界格标邻接脏（无条件覆盖为 Air）
    noteGrowthWrite(x, y, z, occ, id); // t425：维护生长方格索引（TNT 不属生长段 → no-op，但同族写入路径保持一致）
    noteFluidWrite(x, y, z, occ, id);  // perf：维护流体方格索引（TNT 不属流体 → no-op；保持一致）
    noteIceWrite(x, y, z, occ, id);    // t495：维护普通冰方格索引（TNT 不属冰 → no-op；保持一致）
    noteFireWrite(x, y, z, occ, id);   // t724：维护火焰方格索引（点火清格若碰巧清到 Fire → 正确移除）
    recomputeLightAround(x, y, z, occ, id); // t154：增量重 flood（oldId=TNT → newId=Air；TNT 遮光 → 移除放天光）
    emit worldChanged(); // 驱动 mesh 重建（不发 blockPlaced / blockBroken —— 点火是系统事件非玩家动作）
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    pokeFluidDirty(x, y, z); // t380：邻接流体可能受影响 → 标流体脏（保守；TNT 不属流体通常无影响）
    // 审查修 #4 口径合一：点火静默清绕过 setBlock 编辑钩子族 → 邻域附着物复检统一走 recheckAttachments-
    //   AfterClear（旧版只补压力板 / 铁轨 / 雪层三项，仙人掌 / 枯灌木 / 花 / 甘蔗 / 火把族仍漏——同根因分散
    //   补调；现与 dropGravityColumn 共一入口，未来新增附着物只扩 recheck 一处）。重力复检仍显式补调
    //   （recheck 刻意不含 checkGravityBlockOnEdit，防柱内重入，见其头注释）。
    recheckAttachmentsAfterClear(x, y, z, occ); // t494/t565/t527 + t445/t504/t507/t524 + 火把族（全量复检）
    checkGravityBlockOnEdit(x, y, z, occ, id); // t799：正上方沙/沙砾失撑坍落（TNT 点火清格 → 上方沙柱塌落砸在引燃 TNT 上）
    breakNetherPortalsAround(x, y, z); // review #27：余烬门熄灭并入钩子族（TNT 点火清格邻接门面 → 熄门；occ==Air 时 6 邻无门亦快速 no-op）
    // t837① 画作失撑由 recheckAttachmentsAfterClear 内 checkPaintingSupportOnEdit 覆盖（TNT 引燃时画挂
    //   TNT 墙 → 随清格整画掉落）。
    notePowerWrite(x, y, z, occ, id);       // t656：红石电力脏标记（TNT 被点火清 Air → 邻粉 / 邻接收器重算）
    return true;
}

// t1013 矿井箱转正摘除（头注释见 .h）：worldgen 标记位（Chest + ChestStateMineshaftFlag）是唯一授权 ——
//   守卫不过（玩家箱 / 其它结构箱 / 非箱子 / 越界）一律 false 不动。过守卫 → clearBlockSilent（写后钩子
//   族 / worldChanged / 存档 blob 全套，不发 blockBroken —— 防 onBlockBroken(22) 掉内容 + clearChest 把
//   转正链刚登记的内容键条目当场清掉）。
bool World::clearMineshaftChest(int x, int y, int z)
{
    if (!isMineshaftChest(x, y, z)) return false;
    return clearBlockSilent(x, y, z);
}

// t174 水流静默写入（同 setBlockFromEntity 语义：直写 + worldChanged，不发 broken/placed）。支持 state
//   （水流等级 1..7）；无条件覆盖（蒸发时 id=Air state=0，水流改 state 时直接覆盖）。无变化（id+state 均同）
//   → false（防无谓 worldChanged 重建）。越界 → false。caller（tickWaterFlow）保证 id 合法（Water/Air）。
bool World::setWaterSilent(int x, int y, int z, quint8 id, quint8 state)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    const quint8 oldId = m_chunks.blockAt(x, y, z);
    const quint8 oldState = m_chunks.stateAt(x, y, z);
    if (oldId == id && oldState == state) return false; // 无变化（含 id 同 state 同）
    const quint8 lightOldId = oldId; // recomputeLightAround 用编辑前后 id（水 isSolid=false 非遮光，光照通常无变化）
    m_chunks.setBlock(x, y, z, id, state); // 跨 chunk 写 id+state + 标目标脏 + 边界格标邻接脏
    noteGrowthWrite(x, y, z, lightOldId, id); // t425：维护生长方格索引（作物升阶段 id 不变 → no-op；甘蔗生长 / 耕地增删正确）
    noteFluidWrite(x, y, z, lightOldId, id);  // perf：维护流体方格索引（水/岩浆增删 / level 变 id 不变 → 流体格增删正确）
    noteIceWrite(x, y, z, lightOldId, id);    // t495：维护普通冰方格索引（冰↔水融化/冻结经 setWaterSilent → 索引正确增删）
    noteFireWrite(x, y, z, lightOldId, id);   // t724：维护火焰方格索引（可燃物点燃 / 火熄成水经此入口 → 索引正确增删）
    // t380r perf：批量流体 tick 延迟光照重算（N 次 per-write recomputeLightAround → 末尾联合盒 1 次
    //   refloodBox；同 destroySphereSilent t383 批量收口模式）。判据与 recomputeLightAround 早退一致：
    //   t334 遮光翻转（lightOpacity 变）+ t351 发光增删（岩浆）。t494 改用状态感知版 lightEmission
    //   （传 oldState/state）—— 燃烧熔炉（lit bit2）经本入口写时也检出光变（单参版恒 0，潜在陷阱）。
    const bool lightSky = BlockRegistry::lightOpacity(lightOldId, oldState) != BlockRegistry::lightOpacity(id, state);
    const bool lightSource = BlockRegistry::lightEmission(lightOldId, oldState) > 0 || BlockRegistry::lightEmission(id, state) > 0;
    if (m_batchFluid) {
        // t488 perf：天光通道按「列上方是否真能到达」细化。遮光翻转（lightSky）时，若本格上方天光本就被
        //   某遮光块挡死（如地下岩浆池，列上覆土石）→ 本编辑不改变任何天光（该处恒 0），sky 置 false →
        //   批量 flush 时 anySky 可能变 false → refloodBox 不再整列（y1=H-1）重 seed 天光，只重算方块光 +
        //   天光通道保持原值。水×岩浆交互（→黑曜石/石头/圆石，全是 opaque 形成）多为地下深区 → 列上必被
        //   遮挡 → sky=false → lav 桶 reflood 从 ~125ms 降到 ~40ms（用户实测 lav 110.7ms/s 的剩大头）。
        //   正确性：天光在「本格上方无遮光块」时才可能受本编辑影响（新 opaque 挡下方 / 移除挡块放天光）；
        //   上方已有遮光块 → 本格上方天光恒 0、下方天光由上方遮光块决定（与本编辑无关）→ sky 不变，跳过正确。
        //   非批量路径（recomputeLightAround）不动（逐写即时 re flood，编辑路径须立即反映，代价可接受）。
        bool skyEff = lightSky;
        if (skyEff) {
            bool blockedAbove = false;
            for (int yy = y + 1; yy < m_height; ++yy) {
                if (BlockRegistry::lightOpacity(m_chunks.blockAt(x, yy, z), m_chunks.stateAt(x, yy, z)) > 0) {
                    blockedAbove = true;
                    break; // 上方首个遮光块即挡死天光（常见：地下交互列上覆土石，1-2 次 blockAt 早退）
                }
            }
            if (blockedAbove) skyEff = false;
        }
        if (lightSky || lightSource)
            m_pendingLightEdits.push_back({x, y, z, skyEff});
    } else {
        recomputeLightAround(x, y, z, lightOldId, id);
    }
    // t380：流体 tick 内部写入 → 标流体脏（链式扩散：本次写入改变流场 → 下次 tick 续扫直到稳态）。
    //   按 id / oldId 设对应标志：写 / 移除 Water → m_waterDirty；写 / 移除 Lava → m_lavaDirty。
    //   非 fluid 写入（作物升阶 / 羊吃草经此入口）id/oldId 均非 Water/Lava → 不设标志，无副作用。
    if (id == BlockRegistry::Water || lightOldId == BlockRegistry::Water) m_waterDirty = true;
    if (id == BlockRegistry::Lava || lightOldId == BlockRegistry::Lava) m_lavaDirty = true;
    // t488：流体相关写（Water/Lava 增删 + 凝固 Obsidian/Stone/Cobble 覆盖流体）→ 活动盒扩到该格 ±1
    //   （相邻流体格下 tick 据它重扫；见 m_fluidAct* 头注释）。非流体写不扩（其不置 dirty → 无扫描）。
    if (id == BlockRegistry::Water || id == BlockRegistry::Lava
        || lightOldId == BlockRegistry::Water || lightOldId == BlockRegistry::Lava)
        fluidActExpand(x, y, z);
    notePowerWrite(x, y, z, lightOldId, id); // t656：红石电力脏标记（机关 state 静默写——压力板压下 / 探测轨有车标记经本入口；红石族外 no-op 零开销）
    // 审查修 L6：补齐 checkRailOnEdit 调用 —— checkRailOnEdit 头注释承诺「五个写入口末尾全部调它」，
    //   旧版本入口只调 notePowerWrite 漏了它（注释不实，后续维护者会踩坑）。流体写（Water/Air/凝固族 /
    //   作物升阶等）不直接触及轨族：连接重算对非轨格单次 blockAt 早退（13 邻探 + 失撑查 ≈ 十几次读/写，
    //   流体批量热路径可承受）；实际可达效果 = 「水漫入轨下支撑格 / 蒸发清 Air 后正上方铁轨失撑」的
    //   潜在边角（水非 isTopFlushSupport → 轨坍落），补调后与其余四入口同口径。批量路径
    //   （m_batchFluid）中 checkRailOnEdit 末尾的条件 emit+clearAllDirty 只在真有轨变化时触发 =
    //   多一次中间重建（同 destroySphereSilent 逐格调用的先例），批量终态不受破坏。
    checkRailOnEdit(x, y, z, lightOldId, id);
    // t799：流体静默写亦复检沙/沙砾失撑（水蒸发清 Air / 水漫入支撑格替换为非完整立方 → 正上方沙坍落；
    //   同审查修 L6 给 checkRailOnEdit 补本入口的口径——批量流体热路径下非重力族单次 blockAt 早退，
    //   真坍落才走整柱清除 + 中间 worldChanged（同 checkRailOnEdit 批量先例，批量终态不受破坏）。
    //   t527 雪层未挂本入口（雪层贴地生成、水上无雪），沙/砾可在水中失撑故挂）。
    checkGravityBlockOnEdit(x, y, z, lightOldId, id);
    // t903：植物族失撑复检亦挂本入口——羊吃草 Grass→Dirt 经此静默写（t897），草丛唯一合法支撑 = 草方块
    //   （plantGroundBlock 收紧后）→ 支撑被置换为泥土等非草面时正上方草丛须掉落（用户定稿「只能放草方块」
    //   的失撑同口径）。非族格快速路径 = 单次 blockAt + isGroundPlant（同 checkRailOnEdit /
    //   checkGravityBlockOnEdit 批量先例，流体批量热路径可承受；真掉落才走中间 worldChanged）。
    checkFlowerMushroomOnEdit(x, y, z, lightOldId, id);
    // review #27：余烬门门框失撑熄灭并入写入钩子族（焚毁 / 蒸发等「非空内容被置换」写触发；门面自清
    //   经 m_inRemoveNetherPortal 守卫早退无重入）。快速路径 = 6 邻 blockAt 读（无门格即返），流体批量
    //   热路径可承受（同 checkRailOnEdit / checkGravityBlockOnEdit 批量先例口径）。
    // review #27 + t837①：门 / 画作失撑复检（门框 / 画墙被置换为非实体 → 各自整域消失 / 掉落）。纯放置
    //   （Air 格写入）不触发——门框 / 画墙恒原非 Air。快速路径 = 数次 blockAt 邻读（无门格 / 画格即返），
    //   流体批量热路径可承受（同 notePowerWrite 快路径量级）。
    if (lightOldId != BlockRegistry::Air && lightOldId != id) {
        breakNetherPortalsAround(x, y, z);
        checkPaintingSupportOnEdit(x, y, z, lightOldId, id);
    }
    if (m_batchFluid) return true; // t350 流体 tick 批量写：累积栅格写 + 重光照，末尾由 caller 统一 emit + clearDirty
    emit worldChanged(); // 驱动 mesh 重建（水流是系统模拟，非玩家破/放 → 不发 broken/placed）
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    return true;
}

// t669 通用静默写（详见 world.h 头注释）：照搬 5 参数 setBlock 主体（全套写后钩子 + worldChanged），仅不发
//   blockPlaced/blockBroken。背景：锄耕地 / 踩踏回土等「工具/物理交互改写非玩家语义方块」原走 setBlock →
//   发 blockPlaced → QML onBlockPlaced 的「生存放置消耗 1 件」把选中槽误扣（t669① 锄头 / t669② 泥土——
//   同一根因），故这些系统事件改走本入口（同 setBlockFromEntity/setWaterSilent 既有「系统写不发放置事件」约定）。
bool World::setBlockSilent(int x, int y, int z, quint8 id, quint8 state)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    const quint8 oldId = m_chunks.blockAt(x, y, z);
    const quint8 oldState = m_chunks.stateAt(x, y, z);
    if (oldId == id && oldState == state) return false; // 无变化（含 id 同 state 同）
    m_chunks.setBlock(x, y, z, id, state); // 跨 chunk 写 id+state + 标目标脏 + 边界格标邻接脏
    noteGrowthWrite(x, y, z, oldId, id); // t425：维护生长方格索引（耕地增删 / 覆盖作物正确移除）
    noteFluidWrite(x, y, z, oldId, id);  // perf：维护流体方格索引（id 变更时正确增删）
    noteIceWrite(x, y, z, oldId, id);    // t495：维护普通冰方格索引（id 变更时正确增删）
    noteFireWrite(x, y, z, oldId, id);   // t724：维护火焰方格索引（id 变更时正确增删）
    recomputeLightAround(x, y, z, oldId, oldState, id, state); // t154：增量重 flood（透光性变化时重算）
    emit worldChanged(); // 驱动 mesh 重建（不发 blockPlaced / blockBroken —— 系统事件非玩家破/放）
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    if (id == BlockRegistry::Water)
        m_flowTickCounter = kFlowTickInterval - 1;  // 通用静默写亦支持放水源（同 5 参数 setBlock 的 poke 语义）
    if (id == BlockRegistry::Lava)
        m_lavaFlowTickCounter = kLavaFlowTickInterval - 1;
    pokeFluidDirty(x, y, z); // t380：块编辑可能扰动邻接流体平衡 → 标流体脏
    checkCactusOnEdit(x, y, z, oldId, id);       // t445：仙人掌失撑（②）/ 邻接方块（④）整柱坍落复检
    checkDeadBushOnEdit(x, y, z, oldId, id);     // t504：枯死灌木失撑复检
    checkFlowerMushroomOnEdit(x, y, z, oldId, id); // t507：花 / 蘑菇失撑复检
    checkPressurePlateOnEdit(x, y, z, oldId, id); // t494：压力板失撑复检
    checkSugarcaneOnEdit(x, y, z, oldId, id);    // t524：甘蔗失撑复检
    checkSnowLayerOnEdit(x, y, z, oldId, id);    // t527：积雪层失撑复检
    checkGravityBlockOnEdit(x, y, z, oldId, id); // t799：沙/沙砾失撑坍落复检（系统静默写路径收口，同族）
    checkRailOnEdit(x, y, z, oldId, id);         // t565：铁轨连接重算
    checkEndPortalIntegrity(x, y, z, oldId, id); // t664：末地传送门完整性复检
    checkTrapdoorDoorSupportOnEdit(x, y, z, oldId, id); // t851：活板门 / 门失撑掉落复检（系统静默写路径同族收口）
    // review #27：余烬门门框失撑熄灭并入写入钩子族（系统静默写路径——火吞可燃物 / 系统清格，同 5 参数
    //   setBlock 谓词：本格非空内容被置换才触发）。
    if (oldId != BlockRegistry::Air && oldId != id)
        breakNetherPortalsAround(x, y, z);
    checkPaintingSupportOnEdit(x, y, z, oldId, id); // t837①：画作支撑墙失撑 → 整画掉落（系统静默写同族收口）
    notePowerWrite(x, y, z, oldId, id);          // t656：红石电力脏标记
    return true;
}

// t380r：收口批量流体写延迟的光照重算（见 setWaterSilent 延迟分支 + world.h m_pendingLightEdits）。
//   联合盒 = 各延迟编辑的 ±R 盒之并（recomputeLightAround 单编辑盒：x0=ex-R,x1=ex+R,z0=ez-R,z1=ez+R；
//   sky 编辑 y1=H-1 覆盖整列重 seed 天光，非 sky y1=ey+R）。doSky=任一 sky（超集，正确）。refloodBox
//   精确标脏（t383）→ 终态与逐写 recomputeLightAround 一致，仅把 N 次重 flood 合并为 1 次。
void World::flushPendingLightEdits()
{
    if (m_pendingLightEdits.empty()) return;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) { m_pendingLightEdits.clear(); return; }
    constexpr int R = 15; // = recomputeLightAround 的盒半径（最大光值）
    const auto &f = m_pendingLightEdits.front();
    int x0 = f.x - R, y0 = f.y - R, z0 = f.z - R;
    int x1 = f.x + R, y1max = f.y + R, z1 = f.z + R;
    bool anySky = f.sky;
    for (size_t i = 1; i < m_pendingLightEdits.size(); ++i) {
        const PendingLight &e = m_pendingLightEdits[i];
        if (e.x - R < x0) x0 = e.x - R;
        if (e.x + R > x1) x1 = e.x + R;
        if (e.y - R < y0) y0 = e.y - R;
        if (e.y + R > y1max) y1max = e.y + R;
        if (e.z - R < z0) z0 = e.z - R;
        if (e.z + R > z1) z1 = e.z + R;
        anySky = anySky || e.sky;
    }
    const int y1 = anySky ? (H - 1) : std::min(H - 1, y1max);
    x0 = std::max(0, x0); y0 = std::max(0, y0); z0 = std::max(0, z0);
    x1 = std::min(W - 1, x1); z1 = std::min(D - 1, z1);
    if (x0 > x1 || y0 > y1 || z0 > z1) { m_pendingLightEdits.clear(); return; }
    refloodBox(x0, y0, z0, x1, y1, z1, anySky);
    m_pendingLightEdits.clear();
}

// t185 水流重做（增量波前扩散；修 t174 全量 BFS 的「瞬间填平 + 闪烁」bug）。每 kFlowTickInterval
//   （=3，~0.3s）把波前推进 1 格（tickWaterFlow 每 100ms 被 WorldClock.ticked 调，内部节流计数）。
//
//   旧实现（t174）每 tick 从所有水源做**全量 BFS** 重算整片流场 → 玩家一放水桶，下一 tick 整个 7 格扩散
//   + 所有下落柱立刻全部到位 = 「瞬间填平」；且下落水流被当**水源**（state=0）→ 每层下落都重新作源向四方
//   满扩散 → 级联灌满整个盆地（leetcode 接雨水式）。全量重算 + 多源 BFS 在活跃期还会震荡 → 「一闪一闪」。
//
//   新算法（增量、单步波前；spec「水源向外 1 格 1 格流动，最终停，不填满整个平面」）：
//   1) 快照当前所有水格（pos + level）。state 0=水源（永久，仅玩家/铁桶/worldgen 放置/移除）；
//      1..7=流水（每扩散 1 格 level+1，机制等价 MC 1.0 流水 7 格扩散；spec 的「8 格」含水源算）。
//   2) 源再生 pass（t224 水融合 / MC 1.0 infinite-water rule；详见下方调研结论）：流水格若被 ≥2 个
//        水源邻居（水平 4 向）夹住且下方为实体或水源（grounded/supported）→ 升为水源（level 0）。
//        每_tick 仅标记符合条件的格；级联在多 tick 内完成（A 升源后下一 tick 其邻居 B 才凑齐 2 源 → 升）。
//        入 srcRegKeys（升源格 key），供蒸发 / 扩散 pass 跳过（升源格本 tick 既不蒸发也不作流水扩散）。
//   3) 蒸发 pass（流水失支撑 → 退场，1 格/tick 渐退；跳过 srcRegKeys —— 即将升源的格不蒸发）。
//        水源（level=0）永不蒸发。结果入 evapKeys（供扩散 pass 跳过退场格，断 t221「向内回填」震荡）。
//   4) 扩散 pass（只把波前推 1 格，**有流动动画**；跳过 evapKeys + srcRegKeys）：
//        - t221：跳过退场格（不向内回填 → 退场单向 = 蔓延镜像）。
//        - 下落：cell 下方为 air → 下方写**流水 level=1**（**非水源**！修 t174「下落成源灌满盆地」）。
//          **t272 cascade**：下落与水平蔓延不再互斥 —— 边缘 / 悬空水格（下方 air）同时下落 + 水平外扩，
//          使水流「多流一格再下落」（修「悬崖边直接断」：旧 `else if` 让 bk==0 只下落断了水平蔓延）。
//        - 水平蔓延：bk != 2（非水下柱）且 level < kMaxFlowLevel → 4 向邻居：air → 写 level+1（首达即最低）；
//          **t224 re-leveling**：既有流水邻居若能被提供更低 level（c.level+1 < 邻居现 level）→ 下调之
//          （平滑「两滩水融合」：旧实现只写 air、既有水永不下调 → 两股流水相遇在中线形成首达者独占的
//          阶梯边界，观感「明显边界 / 各为固方块」；下调使中线格 = min(两源距)，多 tick 收敛为 V 形平滑）。
//   5) 应用：升源 → 蒸发 → 新增/重定级（三者不相交 —— srcReg/evap/adds 经 keySet 互斥）。经 setWaterSilent
//      写入（系统模拟非玩家动作，不发 broken/placed，仅 worldChanged 重建 mesh）。
//
//   ── t224 MC 1.0 水融合调研结论（先调研、后实现；据此设计 pass 2 + pass 4 re-leveling）──
//   MC Java 1.0 流体规则（机制对齐，非名词照搬）：
//   (a) **level 语义**：水源 level=0；流水 level=1..7（每离源 +1，最大水平扩散 7 格）；下落水为「falling」
//       （满高柱，不再水平衰减——本工程统一下落为 level=1 fresh flow，机制等价、不灌满盆地）。
//   (b) **水平扩散**：每格 level = min(所有源到该格的曼哈顿距离)。两股流水相遇时，中线格取两源距之 min →
//       天然 V 形平滑（**无硬边界**）。旧实现「只写 air、首达者独占」违反本不变量 → 阶梯边界 bug。
//   (c) **源再生（infinite-water / 两滩融合的核心）**：一个**流水**格满足下列条件 → 转为**水源**：
//        - 水平 4 向邻居中**至少 2 个是水源**（level=0）；且
//        - 下方为**实体方块或水源**（grounded/supported；下方为 air 或流水不算 —— 流水会排干，无法长期托住新源）。
//       经典 2×2 池（对角两源）→ 另两空格各被两源夹 → 升源 → 全源。两玩家倒水点距离 ≤2（中间格被两源夹）
//       → 中间格升源 → 两滩融合成连续水源体。源再生级联（多 tick）直到区域被非 grounded 边界（悬崖 / 无 2 源）止。
//   (d) 本工程 worldgen fillWater 把海 / 湖全填为**水源**（state=0）→ 稳态海洋全源、pass 2 无候选 → 零变化。
//       玩家倒单桶（1 源）扩散出的流水无 2 源邻居 → 不升源（与 MC 单桶不形成无限源一致）。
//   收敛性：升源（level 0..0 单调，源集只增）、re-leveling（level 只下调、下界 1）、扩散（bounded by 7）、
//      蒸发（失支撑链有限）→ 有界单调 → 必收敛。稳态（全源池）四 pass 全无候选 → setWaterSilent 全 false
//      → 不触发 worldChanged → 无重建、无闪烁（修「一闪一闪」）。活跃扩散每 tick 仅波前 ≤ 数格变化
//      （远少于旧全量 diff）→ 重建量小、1 格/tick 动画可见（修「瞬间填平」）。
//
//   分层（PLAN §2）：本方法属 World 层，只读/写 m_chunks + 发 worldChanged。不依赖 Renderer/Physics/Game。
//   呈现层（Main.qml）经 WorldClock.ticked 桥接调用（QML 同时持 World + WorldClock，向下合法）。
// t380：块编辑后标记流体脏（驱动 tickWaterFlow/tickLavaFlow 早退优化；详见 m_waterDirty/m_lavaDirty 头注释）。
//   查编辑格 + 6 正交邻是否含 Water/Lava → 设对应标志 true。流体只正交传播，故对角邻不影响（不查）。
//   覆盖两类触发：(a) 直接写流体（id==Water/Lava）；(b) 破 / 改流体邻接的实体块（如破水边的石头 → 水应流入
//   新空气；放块入水 → 邻接水流平衡变）。blockAt 越界返 Air（ChunkManager）→ 无需 bounds 检查。
void World::pokeFluidDirty(int x, int y, int z)
{
    const int neigh[7][3] = {{0,0,0},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &n : neigh) {
        const quint8 b = m_chunks.blockAt(x + n[0], y + n[1], z + n[2]);
        if (b == BlockRegistry::Water) m_waterDirty = true;
        else if (b == BlockRegistry::Lava) m_lavaDirty = true;
    }
    // t488：编辑点邻接流体 → 活动盒扩到该格 ±1（流体 tick 据此只扫盒内区域，见 m_fluidAct* 头注释）。
    fluidActExpand(x, y, z);
}

// t488：活动盒扩展到 (x,y,z)±1。首次扩展（盒空）以该格为初值；后续取并（min/max）。O(1)。
void World::fluidActExpand(int x, int y, int z)
{
    if (!m_fluidActValid) {
        m_fluidActX0 = m_fluidActX1 = x;
        m_fluidActY0 = m_fluidActY1 = y;
        m_fluidActZ0 = m_fluidActZ1 = z;
        m_fluidActValid = true;
    } else {
        if (x - 1 < m_fluidActX0) m_fluidActX0 = x - 1;
        if (x + 1 > m_fluidActX1) m_fluidActX1 = x + 1;
        if (y - 1 < m_fluidActY0) m_fluidActY0 = y - 1;
        if (y + 1 > m_fluidActY1) m_fluidActY1 = y + 1;
        if (z - 1 < m_fluidActZ0) m_fluidActZ0 = z - 1;
        if (z + 1 > m_fluidActZ1) m_fluidActZ1 = z + 1;
    }
}

// t488：清空活动盒（每次流体 tick 扫描后 + 世界重置时）。扫描后清 → 盒只累积「自上次扫描以来」的活动。
void World::fluidActReset()
{
    m_fluidActValid = false;
    m_fluidActX0 = m_fluidActY0 = m_fluidActZ0 = 0;
    m_fluidActX1 = m_fluidActY1 = m_fluidActZ1 = 0;
}

// t933：重力级联光照联合盒扩到 (x,y,z)（见 world.h m_gravLight* 头注释）。O(1)；盒存坍落格本体
//   （±15 余量在级联收尾 refloodBox 调用点统一外扩，与 recomputeLightAround 的盒公式同式）。
void World::gravLightExpand(int x, int y, int z)
{
    if (!m_gravLightAny) {
        m_gravLightX0 = m_gravLightX1 = x;
        m_gravLightY0 = m_gravLightY1 = y;
        m_gravLightZ0 = m_gravLightZ1 = z;
        m_gravLightAny = true;
    } else {
        if (x < m_gravLightX0) m_gravLightX0 = x;
        if (x > m_gravLightX1) m_gravLightX1 = x;
        if (y < m_gravLightY0) m_gravLightY0 = y;
        if (y > m_gravLightY1) m_gravLightY1 = y;
        if (z < m_gravLightZ0) m_gravLightZ0 = z;
        if (z > m_gravLightZ1) m_gravLightZ1 = z;
    }
}

// t933：清空重力级联光照联合盒（级联收尾消费后 + 世界重置时防御清）。O(1)。
void World::gravLightReset()
{
    m_batchGravity = false;
    m_gravLightAny = false;
    m_gravLightX0 = m_gravLightY0 = m_gravLightZ0 = 0;
    m_gravLightX1 = m_gravLightY1 = m_gravLightZ1 = 0;
}

void World::tickWaterFlow()
{
    FrameProfiler::Scope prof("wWater"); // perf：含节流 / 早退（稳态零开销仍计入极小常数，便于对照）
    if (++m_flowTickCounter < kFlowTickInterval) return; // 节流：每 3 tick（~0.3s）把波前推进 1 格
    m_flowTickCounter = 0;
    // t380 perf：稳态（海洋全源、无玩家扰动）早退 —— 跳过全图 W×D×H 扫描（~41 万格 chunk 路由除法 ≈ 3-5ms）。
    //   m_waterDirty 由 setBlock/setBlockFromEntity/destroySphereSilent 的 pokeFluidDirty + setWaterSilent 写
    //   Water 时设 true。本行清 false；apply 内 setWaterSilent 若真写则重设 → 稳态（零写入）后停扫。
    if (!m_waterDirty) return;
    m_waterDirty = false;
    // t488：拷贝活动盒到局部（本次扫描范围）+ 清盒（下次 tick 盒 = 本次 tick 的写入 / 编辑 → 波前逐 tick
    //   向前推，级联正确）。盒空（worldgen / 加载一次性确认 / 无近期流体活动但标志残留）→ 全量快照兜底。
    const bool actValid = m_fluidActValid;
    const int ax0 = m_fluidActX0, ay0 = m_fluidActY0, az0 = m_fluidActZ0;
    const int ax1 = m_fluidActX1, ay1 = m_fluidActY1, az1 = m_fluidActZ1;
    fluidActReset();
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    QElapsedTimer t380t;
    t380t.start(); // t380 perf：可观测活跃扫描耗时（仅非稳态扫描打，稳态早退不打 → 无噪音）

    // 体素线性 key：x + z*W + y*W*D。世界 ≤ 80×80×64 = 409600 < INT_MAX，编码安全。仅用于 adds 去重 / 取 min。
    auto keyOf = [W, D](int x, int y, int z) -> long long {
        return static_cast<long long>(x) + static_cast<long long>(z) * W
             + static_cast<long long>(y) * static_cast<long long>(W) * D;
    };

    // 1) 快照当前水格（tick 内栅格不变 —— 新增/蒸发在 pass 末统一应用）。
    //    perf：遍历 m_waterCells（O(水格数)）替代全图 W×D×H 扫描（O(3.28M) × chunk 路由除法）。集合由
    //      noteFluidWrite 增量维护（写入路径 setBlock / setWaterSilent 等）；rebuildFluidCells 在 generate /
    //      finishLoad 末全图重建一次。防御：某条直写路径漏 noteFluidWrite 致集合含过期项（cell 已非水）→
    //      blockAt 复核跳过（不影响正确性，仅少扫一格；稳态早退保证零扫描时此遍历不跑）。
    struct WCell { int x, y, z; quint8 level; };
    std::vector<WCell> cells;
    cells.reserve(m_waterCells.size());
    for (const quint64 k : m_waterCells) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        // t488：盒过滤 —— 只保留「最近有流体相关写入/编辑区域」内的格（±1 margin 已覆盖可反应格）；盒空
        //   不滤（worldgen / 加载全量确认）。索引项可能含过期格（直写漏通知）→ blockAt 复核跳过（同旧路径）。
        if (actValid && (x < ax0 || x > ax1 || y < ay0 || y > ay1 || z < az0 || z > az1)) continue;
        if (m_chunks.blockAt(x, y, z) != BlockRegistry::Water) continue; // 过期索引项（直写漏通知）→ 跳过
        cells.push_back({x, y, z, m_chunks.stateAt(x, y, z)});
    }

    // t411 流体交互 pass A（流水 → 静岩浆源 → 黑曜石）：遍历快照中的**流水**格（state>0），查 6 正交邻是否
    //   为**静岩浆源**（Lava state=0）；命中则把该静岩浆源凝固为 Obsidian。机制等价 MC 1.0「流水触岩浆源 →
    //   黑曜石」（本 pass 仅流水触发；水源触岩浆源的凝固归 tickLavaFlow pass B 的 t472 补丁——双源亦产黑曜石，
    //   机制对齐 spec「water source + lava source → obsidian」）。6 正交邻覆盖「水流自上而下浇到岩浆源顶」的瀑布
    //   情形。凝固目标延迟到批量应用阶段写入（流场计算 pass 2-4 读旧栅格，但水本就无法流入岩浆/obsidian 实体 →
    //   无副作用）。setWaterSilent 写 Obsidian：旧 id=Lava → 内部标 m_lavaDirty，驱动下次岩浆 tick 续扫该岩浆格的
    //   流岩浆邻居（它们因源被凝固而失支撑，应凝固/退场）。
    std::vector<WCell> obsidianTargets;
    {
        static const int neigh[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const WCell &c : cells) {
            if (c.level == 0) continue; // 仅流水触发交互（水源触岩浆不凝固）
            for (const auto &n : neigh) {
                const int nx = c.x + n[0], ny = c.y + n[1], nz = c.z + n[2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
                if (m_chunks.blockAt(nx, ny, nz) == BlockRegistry::Lava
                    && m_chunks.stateAt(nx, ny, nz) == 0) {
                    obsidianTargets.push_back({nx, ny, nz, 0});
                }
            }
        }
    }

    // 新增表：key → 新 level（多源指向同一格取 min = 最短源距，机制对齐 MC）。
    std::unordered_map<long long, quint8> adds;
    auto tryAdd = [&](long long k, quint8 lvl) {
        auto it = adds.find(k);
        if (it == adds.end()) adds.emplace(k, lvl);
        else if (it->second > lvl) it->second = lvl;
    };

    // t1012④ 水冲刷附着块登记（附着块族单一权威 = BlockRegistry::isAttachableBlock：火把各 state /
    //   红石火把 / 蜘蛛网）：扩散落点是附着块格 → 记入 washed（保留被毁 id 供 dropId 掉落），本 tick 应用段
    //   置 Air + 发 blockDroppedAsItem，随后 adds 对同格写 Water = 同 tick「冲毁 + 入水」。机制等价 MC 1.0
    //   「流水冲毁非实体附着物并掉落」。掉落链对齐玩家挖除（BlockRegistry::dropId——火把/红石火把掉自身、
    //   蛛网掉线 0x219）。石头等非附着块不 wash（阴性对照腿）。washKeys 去重（多源指向同一格只毁一次）。
    struct WashedCell { int x, y, z; quint8 id; }; // 被毁格 + 原方块 id（dropId 掉落用）
    std::vector<WashedCell> washed;
    std::unordered_set<long long> washKeys;
    auto tryWashCell = [&](int x, int y, int z) -> bool {
        if (x < 0 || y < 0 || z < 0 || x >= W || y >= H || z >= D) return false;
        const quint8 id = m_chunks.blockAt(x, y, z);
        if (!BlockRegistry::isAttachableBlock(id)) return false;
        const long long k = keyOf(x, y, z);
        if (!washKeys.count(k)) {
            washKeys.insert(k);
            washed.push_back({x, y, z, id});
        }
        return true;
    };

    // 下方格类别：y==0 视为实体底（基岩层不可下落）；否则查 m_chunks。
    //   0=air(下落) / 1=solid(grounded，水平蔓延) / 2=water(水下柱，本格既不下落也不蔓延)。
    auto belowKind = [&](int x, int y, int z) -> int {
        if (y == 0) return 1;
        const quint8 b = m_chunks.blockAt(x, y - 1, z);
        if (b == BlockRegistry::Air) return 0;
        if (b == BlockRegistry::Water) return 2;
        return 1;
    };
    static const int hd[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    // 2) 源再生 pass（t224 水融合 / MC 1.0 infinite-water rule）：流水格被 ≥2 水平水源邻居夹住 +
    //    下方为实体或水源 → 升为水源（level 0）。每 tick 仅标记符合条件的格；级联在多 tick 完成（A 升源
    //    后下一 tick 邻居 B 才凑齐 2 源 → 升）。结果入 srcRegKeys —— 蒸发 / 扩散 pass 据此跳过升源格
    //    （本 tick 不蒸发、亦不作流水扩散；下一 tick 作水源正常扩散）。机制等价 MC「2×2 池 / 两源夹一格
    //    → 中间升源」即经典无限水机。worldgen 海/湖全为水源 → 无流水候选 → 稳态零变化；玩家单桶水扩散
    //    出的流水仅 1 源邻居 → 不升源（与 MC 单桶不形成无限源一致）。两玩家倒水点距 ≤2 → 中间格被两源夹
    //    → 升源 → 两滩融合为连续水源体（用户诉求「两股流水相遇应融合，现明显边界 / 各为固方块」之修复）。
    std::vector<WCell> srcRegs;
    std::unordered_set<long long> srcRegKeys;
    for (const WCell &c : cells) {
        if (c.level == 0) continue; // 已是水源
        // a) 水平 4 向水源邻居计数（MC：至少 2 个水源夹住本格）。
        int srcNeighbors = 0;
        for (const auto &d : hd) {
            const int nx = c.x + d[0], nz = c.z + d[1];
            if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
            if (m_chunks.blockAt(nx, c.y, nz) == BlockRegistry::Water
                && m_chunks.stateAt(nx, c.y, nz) == 0) {
                ++srcNeighbors;
            }
        }
        if (srcNeighbors < 2) continue;
        // b) 下方须为实体方块或水源（MC：grounded/supported；下方 air 或流水不算 —— 流水会排干无法托住新源）。
        bool supportedBelow = false;
        if (c.y == 0) {
            supportedBelow = true; // 世界底 = 基岩实心
        } else {
            const quint8 bb = m_chunks.blockAt(c.x, c.y - 1, c.z);
            if (bb == BlockRegistry::Air) {
                supportedBelow = false;
            } else if (bb == BlockRegistry::Water) {
                supportedBelow = (m_chunks.stateAt(c.x, c.y - 1, c.z) == 0); // 仅水源托得住
            } else {
                supportedBelow = true; // 实体方块
            }
        }
        if (!supportedBelow) continue;
        srcRegs.push_back(c);
        srcRegKeys.insert(keyOf(c.x, c.y, c.z));
    }

    // 3) 蒸发 pass（先算，供扩散 pass 跳过「本 tick 将退场」的格）：流水（level>0）失支撑 → 退场。
    //    水源（level=0）永不蒸发（玩家/铁桶/worldgen 管）。t224：跳过 srcRegKeys —— 即将升源的格不蒸发
    //    （其本就 2 源邻居 → 必有更低 level 邻居 → supported，本不会进 evaps，此处显式跳过为防御 / 可读）。
    //    结果同时入 evapKeys（key 集合）—— 扩散 pass 据此跳过退场格，断「向内回填」震荡（t221）。
    std::vector<WCell> evaps;
    std::unordered_set<long long> evapKeys;
    for (const WCell &c : cells) {
        if (c.level == 0) continue;
        if (srcRegKeys.count(keyOf(c.x, c.y, c.z))) continue; // t224：即将升源，不蒸发
        bool supported = false;
        // a) 上方有水 → 被下落柱 / 上方源灌养（支撑）。
        if (c.y + 1 < H && m_chunks.blockAt(c.x, c.y + 1, c.z) == BlockRegistry::Water)
            supported = true;
        // b) 水平方向有更低 level 的水邻居（指向源方向）→ 支撑。
        if (!supported) {
            for (const auto &d : hd) {
                const int nx = c.x + d[0], nz = c.z + d[1];
                if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                if (m_chunks.blockAt(nx, c.y, nz) != BlockRegistry::Water) continue;
                if (m_chunks.stateAt(nx, c.y, nz) < c.level) { supported = true; break; }
            }
        }
        if (!supported) {
            evaps.push_back(c);
            evapKeys.insert(keyOf(c.x, c.y, c.z));
        }
    }

    // 4) 扩散 pass：只把波前推 1 格（不级联 —— 新格下一 tick 才继续扩散 → 1 格/tick 动画）。
    //    跳过 evapKeys（退场格）+ srcRegKeys（升源格）：前者防向内回填震荡（t221），后者升源格本 tick
    //    不作流水扩散（下一 tick 作水源扩散）。t224 re-leveling：对既有流水邻居，若能提供更低 level 则下调
    //    （平滑两滩融合 —— 旧「只写 air、首达者独占」致中线阶梯边界 bug 之修复）。
    //    t272 平面边缘 cascade：「下落」与「水平蔓延」**不再互斥** —— 边缘 / 悬空水格（下方 air）同时
    //    下落 + 向水平 air 邻居扩散（旧实现 bk==0 只下落、`else if` 断了水平蔓延 → 悬崖边水柱仅 1 格宽即
    //    垂直断流，用户观感「直接断」）。新格（悬崖外的悬空水）下一 tick 自身下方 air → 再下落 + 再外扩，
    //    形成「向外悬空延伸 → 下落」的 cascade（机制等价 MC 流水越崖外扩几格再成瀑；受 kMaxFlowLevel
    //    收束，不会无限外扩）。bk==2（水下柱）仍跳过水平蔓延：由该柱最底 grounded 格负责扩散，柱内每格
    //    都扩散会重复写邻居。
    for (const WCell &c : cells) {
        if (evapKeys.count(keyOf(c.x, c.y, c.z))) continue;   // 退场中的格不扩散（断回填震荡）
        if (srcRegKeys.count(keyOf(c.x, c.y, c.z))) continue; // t224：升源格本 tick 不作流水扩散
        const int bk = belowKind(c.x, c.y, c.z);
        if (bk == 0) {
            // 下落：写下方为流水 level=1（**非源** —— 修 t174「下落成源灌满盆地」bug）。
            tryAdd(keyOf(c.x, c.y - 1, c.z), quint8(1));
        } else if (bk == 1 && tryWashCell(c.x, c.y - 1, c.z)) {
            // t1012④ 下方是附着块（贴地火把等）→ 冲毁登记 + 同 tick 灌入流水 level=1（同下落口径；
            //   机制等价 MC 流水浇毁脚下火把）。
            tryAdd(keyOf(c.x, c.y - 1, c.z), quint8(1));
        }
        // 水平蔓延：仅当本格 grounded（下方为实体方块，bk==1）才向水平 air 邻居扩散；air → 写 level+1；
        //   既有流水 → re-level 下调。t350 修「单桶水流遇崖边悬空 cascade → 淹平面（tsunami）」：
        //   旧实现 `bk != 2` 让 bk==0（下方 air）同时下落 + 水平外扩，每格悬空水都向 4 方各推 1 格再下落 →
        //   无界平面淹没（一桶水铺满整片）。MC 规则：流水仅在 solid 支撑上水平扩散；下方为 air 即**只**垂直
        //   下落（已写下方 level=1），不外扩。水流沿 solid 推到崖边 → 边缘格下方 air → 下落成柱 → 落点
        //   grounded 格继续 7 格水平蔓延（机制等价 MC 流水 grounded spread + 越崖成瀑）。bk==2（水下柱）本格
        //   不扩散（由该柱最底 grounded 格负责）；bk==0（悬空）只下落不扩散。
        if (bk == 1 && c.level < kMaxFlowLevel) {
            for (const auto &d : hd) {
                const int nx = c.x + d[0], nz = c.z + d[1];
                if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                const long long nbKey = keyOf(nx, c.y, nz);
                // 不动本 tick 退场 / 升源的邻居（前者将变 air、后者将变源；写它们会被 apply 后续覆盖 = 错）。
                if (evapKeys.count(nbKey) || srcRegKeys.count(nbKey)) continue;
                const quint8 nbId = m_chunks.blockAt(nx, c.y, nz);
                if (nbId == BlockRegistry::Air) {
                    // 蔓延到 air（首达即最低 level，机制对齐 MC 最短源距）。
                    tryAdd(nbKey, quint8(c.level + 1));
                } else if (tryWashCell(nx, c.y, nz)) {
                    // t1012④ 邻格是附着块（墙面火把 / 红石火把 / 蛛网）→ 冲毁登记 + 同 tick 灌入流水
                    //   （level+1 同 air 蔓延口径；机制等价 MC 流水漫过冲毁附着物并掉落）。
                    tryAdd(nbKey, quint8(c.level + 1));
                } else if (nbId == BlockRegistry::Water) {
                    // t224 re-leveling：既有流水邻居若能被提供更低 level（更近源）→ 下调之。
                    //   旧实现只入 air → 两股流水相遇在中线，首达者独占该格 level，后到者被 `!=Air` 挡在
                    //   外 → 中线两侧 level 不平滑（阶梯边界 = 用户「明显边界」）。下调使该格 = min(两源距)，
                    //   多 tick 内逐级收敛为 V 形平滑（每 tick 下调 1 级，与波前 1 格/tick 动画一致）。
                    //   只下调（offered < 现级），不上调；水源（level 0）永不被 re-level（仅 pass 2 升源）。
                    const quint8 nbLvl = m_chunks.stateAt(nx, c.y, nz);
                    const quint8 offered = quint8(c.level + 1);
                    if (nbLvl > 0 && offered < nbLvl) tryAdd(nbKey, offered);
                }
            }
        }
    }

    // 5) 应用：升源 → 蒸发 → 新增/重定级（三者经 keySet 互斥，顺序安全）。setWaterSilent 内部无变化
    //    → false → 稳态零重建。升源最前：升源格不在 evaps/evapKeys（pass 3 显式跳过）亦不在 adds（pass 4
    //    显式跳过 srcRegKeys），故先写 level 0 不会被后续覆盖。
    //    t350 流体 tick 批量写：开 m_batchFluid 使每次 setWaterSilent 只写栅格 + 重光照、**不** emit
    //    worldChanged、**不** clearAllDirty；末尾本 tick 所有改动一次性 emit worldChanged（驱动 terrain/water
    //    两段重建完）+ clearAllDirty。修「活跃扩散每 tick 写 N 格 → N 次 worldChanged → N×全 chunk
    //    onWorldChanged 扇出 + N×recomputeMeshStats 全扫」的卡顿。批量遵循两段共享 dirty 协议（emit 后两段
    //    同步重建、再统一清脏），与逐格路径终态一致，仅把 N 次重建并为 1 次。
    m_batchFluid = true;
    bool anyChange = false;
    // t411：先写流水→静岩浆源凝固为 Obsidian（与 srcRegs/evaps/adds 操作的格互不相交 —— obsidian 目标是 Lava
    //   源格，其余三者操作 Water/Air 格；故写入顺序安全）。
    for (const WCell &o : obsidianTargets)
        anyChange |= setWaterSilent(o.x, o.y, o.z, BlockRegistry::Obsidian, 0);
    for (const WCell &s : srcRegs)
        anyChange |= setWaterSilent(s.x, s.y, s.z, BlockRegistry::Water, 0);
    for (const WCell &e : evaps)
        anyChange |= setWaterSilent(e.x, e.y, e.z, BlockRegistry::Air, 0);
    // t1012④ 水冲刷附着块应用：置 Air + 发 blockDroppedAsItem（dropId 对齐玩家挖除）。先于 adds——
    //   同格 tryAdd 已登记 → 随后写 Water，同 tick 完成「冲毁 + 入水」。setWaterSilent 内部 notePowerWrite
    //   承接红石火把被毁的红石重算链（t976/t977 AOT 教训口径：跨单元绑定不在本调用重估，QML 读该格的
    //   绑定经本 tick 末尾一次 emit worldChanged 的信号 handler 读取）。掉落信号在 m_batchFluid 批量写期间
    //   发出：blockDroppedAsItem 只驱动呈现层 ItemEntityManager 生成掉落实体（不写栅格 / 不发 worldChanged），
    //   与批量协议正交（同 detonateStalker 批内发 explosionDroppedItem 先例）。
    for (const WashedCell &wsh : washed) {
        if (setWaterSilent(wsh.x, wsh.y, wsh.z, BlockRegistry::Air, 0)) {
            emit blockDroppedAsItem(wsh.x, wsh.y, wsh.z, BlockRegistry::dropId(wsh.id));
            anyChange = true;
        }
    }
    for (const auto &kv : adds) {
        const long long k = kv.first;
        const int x = static_cast<int>(k % W);
        const long long kz = k / W;
        const int z = static_cast<int>(kz % D);
        const int y = static_cast<int>(kz / D);
        anyChange |= setWaterSilent(x, y, z, BlockRegistry::Water, kv.second);
    }
    m_batchFluid = false;
    flushPendingLightEdits(); // t380r：批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (anyChange) {
        emit worldChanged();       // 一次重建（terrain/water 两段各检各的 dirty → 仅脏 chunk 重建）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏（同逐格路径的 emit→clear 顺序）
    } else if (actValid) {
        // t563 ①：本次 tick 走「活动盒过滤快照」（actValid=true）却**零写入** —— 盒只盖「近期活动区域」，
        //   可能没盖住仍要流动的格（多流场共存 / 玩家在别处编辑 / 峡谷上游瀑布仍在级联）→ 若就此停扫，
        //   m_waterDirty 已在上方清 false → 下 tick 早退 → 级联中断（用户实测「峡谷水流到一半不流了，
        //   放方块才续流」—— 放方块 poke 又重扫一次，然后再次停在半路）。修法：保持 dirty → 下 tick
        //   盒已空（fluidActReset 清掉、零写入不重建）→ 全量快照兜底扫一次，保证级联收敛完整（正确性）。
        //   代价：每次「盒过滤稳态」后多 1 次全量快照（一次性，可接受；真稳态 → 全量也无写入 → dirty 保持
        //   false → 停扫，早退优化不受影响）。
        m_waterDirty = true;
    }
    // t380 perf：活跃水扫描耗时（仅非稳态扫描打：cells=参与计算的水格数、writes=本 tick 实写数、settled=是否收敛停扫）。
    //   t488：box=活动盒是否命中（1=盒过滤快照 / 0=全量兜底）—— 看 cells 数量对比即可知盒收窄了多少扫描范围。
    //   注：elapsed() 单位 ms（旧版误标 "us"，t488 改正 —— 此值含末尾批量 reflood+emit 重建，非纯扫描）。
    qInfo("vo.perf: tickWaterFlow %lldms cells=%d writes=%d settled=%d box=%d",
          t380t.elapsed(), int(cells.size()), int(srcRegs.size() + evaps.size() + int(adds.size())),
          anyChange ? 0 : 1, actValid ? 1 : 0);
}

// t343 岩浆流 tick（见 world.h 头注释）。机制等价 MC 1.0 主世界岩浆：比水慢 ~30 倍、扩散 3 格、无源再生。
//   算法同 tickWaterFlow 的增量波前（快照 → 蒸发 → 扩散 → 应用），但去掉源再生 pass（岩浆不形成无限源）、
//   节流更慢（kLavaFlowTickInterval=30 → 3s/格）、扩散更短（kMaxLavaFlowLevel=3）。末尾 ignite pass 焚毁邻岩浆木类。
void World::tickLavaFlow()
{
    FrameProfiler::Scope prof("wLava"); // perf：含节流 / 早退
    if (++m_lavaFlowTickCounter < kLavaFlowTickInterval) return; // 节流：每 30 tick（~3s）把波前推进 1 格
    m_lavaFlowTickCounter = 0;
    // t380 perf：稳态早退（同 tickWaterFlow；m_lavaDirty 由 pokeFluidDirty / setWaterSilent 写 Lava 时设）。
    if (!m_lavaDirty) return;
    m_lavaDirty = false;
    // t488：拷贝活动盒到局部（本次扫描范围）+ 清盒（同 tickWaterFlow；盒空 → 全量快照兜底）。
    const bool actValid = m_fluidActValid;
    const int ax0 = m_fluidActX0, ay0 = m_fluidActY0, az0 = m_fluidActZ0;
    const int ax1 = m_fluidActX1, ay1 = m_fluidActY1, az1 = m_fluidActZ1;
    fluidActReset();
    ++m_lavaIgniteIndex; // ignite 窗口序号（每流 tick +1，喂散布概率 → 错峰焚毁）
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    QElapsedTimer t380t;
    t380t.start(); // t380 perf：活跃岩浆扫描耗时（仅非稳态打）

    auto keyOf = [W, D](int x, int y, int z) -> long long {
        return static_cast<long long>(x) + static_cast<long long>(z) * W
             + static_cast<long long>(y) * static_cast<long long>(W) * D;
    };

    // 1) 快照当前岩浆格（perf：遍历 m_lavaCells O(岩浆格数) 替代全图扫描 O(3.28M)；同 tickWaterFlow）。
    struct LCell { int x, y, z; quint8 level; };
    std::vector<LCell> cells;
    cells.reserve(m_lavaCells.size());
    for (const quint64 k : m_lavaCells) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        // t488：盒过滤（同 tickWaterFlow；只扫最近活动区域，盒空 → 全量兜底）。
        if (actValid && (x < ax0 || x > ax1 || y < ay0 || y > ay1 || z < az0 || z > az1)) continue;
        if (m_chunks.blockAt(x, y, z) != BlockRegistry::Lava) continue; // 过期索引项 → 跳过
        cells.push_back({x, y, z, m_chunks.stateAt(x, y, z)});
    }

    // t438 流体交互 pass B（流岩浆 → 静水源→石头 / 流水→圆石）：遍历快照中的**流岩浆**格（state>0），查 6
    //   正交邻的水格，按对方 state 凝固：**静水源**（Water state=0）→ **Stone**；**流水**（state>0）→ **Cobblestone**。
    //   机制等价 MC 1.0「流岩浆触静水→石头」「流岩浆触流水→圆石」（spec t438 三规则之二、三）。
    //   **t438 修 t411 两处 bug**：(1) 旧实现流岩浆+静水源恒产 Cobblestone，spec 要求 Stone（流岩浆把水源烧成石）；
    //   (2) 旧实现只查对方 source（state==0），**流水+流岩浆相遇时双方都不是 source → 两侧 pass 互不反应 = 水火共融
    //   不凝固 bug 的真根因**——现补「流水→圆石」分支，两流相遇即凝固。仅流岩浆触发（岩浆源触水不反应）；6 正交
    //   邻覆盖「流岩浆自上而下浇到水顶」的瀑布情形。凝固目标延迟到批量应用阶段写入（流场计算 pass 2-3 读旧栅格，
    //   但岩浆本就无法流入水/stone/cobble 实体 → 无副作用）。setWaterSilent 写入：旧 id=Water → 内部标
    //   m_waterDirty，驱动下次水 tick 续扫该水格邻居（被凝固的水消失 → 邻水可能失支撑应退场/扩散）。
    //   交互规则完整矩阵（与 tickWaterFlow pass A 互补、无重叠）：
    //     流水 + 岩浆源 → 黑曜石（pass A，改岩浆格） / 水源 + 岩浆源 → 黑曜石（本 pass t472 补丁，改岩浆格）
    //     流岩浆 + 水源 → 石头（本 pass，改水格） / 流岩浆 + 流水 → 圆石（本 pass，改水格）
    struct SolidifyTarget { int x, y, z; quint8 result; };
    std::vector<SolidifyTarget> solidifyTargets;
    {
        static const int neigh[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const LCell &c : cells) {
            if (c.level == 0) continue; // 仅流岩浆触发交互（岩浆源触水不凝固）
            for (const auto &n : neigh) {
                const int nx = c.x + n[0], ny = c.y + n[1], nz = c.z + n[2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
                if (m_chunks.blockAt(nx, ny, nz) != BlockRegistry::Water) continue;
                const quint8 wState = m_chunks.stateAt(nx, ny, nz);
                // 静水源 → 石头（spec「流岩浆+静水→石头」）；流水 → 圆石（spec「流岩浆+流水→圆石」）。
                // 两支均显式转 quint8（BlockRegistry::Id 枚举），避免 -Wextra 枚举/标量混用告警（lessons-learned）。
                const quint8 result = (wState == 0) ? quint8(BlockRegistry::Stone)
                                                    : quint8(BlockRegistry::Cobble);
                solidifyTargets.push_back({nx, ny, nz, result});
            }
        }
    }

    // t472 流体交互 pass B 补丁（静岩浆源 + 静水源 → 黑曜石）：遍历快照中的**岩浆源**格（state==0），查 6 正交邻是否
    //   为**静水源**（Water state==0）；命中则把本岩浆源凝固为 Obsidian。机制等价 spec t472「water source + lava
    //   source → obsidian」（双源静置凝固）。**与 pass A「流水→岩浆源」互补、无重叠**：pass A（tickWaterFlow）由流水
    //   触发改岩浆格，本支由岩浆源视角查水源邻接改岩浆格 —— 二者改的都是岩浆源格、但触发条件不同（流水 vs 水源邻接）；
    //   一旦凝固为 Obsidian 即非岩浆 → 下次 tick 不再命中任一支，无双触发。流岩浆（state>0）由上方 solidify pass 处理
    //   （触水源→石头 / 流水→圆石），故本支只看岩浆源（state==0）。凝固目标延迟到批量应用阶段写入（与 solidifyTargets
    //   同批；obsidianTargets 是岩浆源格，与 solidifyTargets 水格 / evaps+adds 流岩浆格互不相交 → 写入顺序安全）。
    std::vector<LCell> obsidianTargets;
    {
        static const int neigh[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const LCell &c : cells) {
            if (c.level != 0) continue; // 仅岩浆源触发本支（流岩浆由上方 solidify pass 处理）
            for (const auto &n : neigh) {
                const int nx = c.x + n[0], ny = c.y + n[1], nz = c.z + n[2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
                if (m_chunks.blockAt(nx, ny, nz) == BlockRegistry::Water
                    && m_chunks.stateAt(nx, ny, nz) == 0) {
                    obsidianTargets.push_back(c); // 凝固本岩浆源格（机制对齐 MC：岩浆源被水凝固为黑曜石）
                    break; // 任一水源邻接即凝固本格，无需重复登记
                }
            }
        }
    }

    std::unordered_map<long long, quint8> adds;
    auto tryAdd = [&](long long k, quint8 lvl) {
        auto it = adds.find(k);
        if (it == adds.end()) adds.emplace(k, lvl);
        else if (it->second > lvl) it->second = lvl;
    };

    // 下方格类别（同 tickWaterFlow）：0=air(下落) / 1=solid(grounded) / 2=lava(岩浆下柱，不下落不蔓延)。
    auto belowKind = [&](int x, int y, int z) -> int {
        if (y == 0) return 1;
        const quint8 b = m_chunks.blockAt(x, y - 1, z);
        if (b == BlockRegistry::Air) return 0;
        if (b == BlockRegistry::Lava) return 2;
        return 1;
    };
    static const int hd[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    // 2) 蒸发 pass（流岩浆 state>0 失支撑 → 凝固退场；岩浆源 state=0 永不退场）。无源再生 pass（岩浆不形成无限源）。
    std::vector<LCell> evaps;
    std::unordered_set<long long> evapKeys;
    for (const LCell &c : cells) {
        if (c.level == 0) continue; // 岩浆源永不退场（玩家/铁桶/worldgen 管）
        bool supported = false;
        if (c.y + 1 < H && m_chunks.blockAt(c.x, c.y + 1, c.z) == BlockRegistry::Lava) supported = true; // 上方岩浆灌养
        if (!supported) {
            for (const auto &d : hd) {
                const int nx = c.x + d[0], nz = c.z + d[1];
                if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                if (m_chunks.blockAt(nx, c.y, nz) != BlockRegistry::Lava) continue;
                if (m_chunks.stateAt(nx, c.y, nz) < c.level) { supported = true; break; } // 水平更低 level 邻居（近源）支撑
            }
        }
        if (!supported) {
            evaps.push_back(c);
            evapKeys.insert(keyOf(c.x, c.y, c.z));
        }
    }

    // 3) 扩散 pass：只把波前推 1 格（1 格/tick 缓慢动画）。跳过退场格。
    //    水平蔓延守卫 = bk==1（**仅 grounded 格向水平 air 邻居扩散**），同 tickWaterFlow 的 t350 修法
    //    （机制等价 MC 流体仅在 solid 支撑上水平扩散；下方为 air 即**只**垂直下落、不外扩）。
    //    perf 收敛修复（lava-never-settles-bug）：旧守卫 `bk != 2` 让 bk==0（悬空，下方 air）的流岩浆同时
    //    下落 + 向 4 方水平外扩 → 每个新悬空格再产 4 个悬空格 → 无界平面蔓延（机制同 t350 修前水的
    //    tsunami bug），cells 单调增长（实测 fresh world 277→462→636→724 writes 35-107 settled 恒 0），
    //    每 3s tick 28-39ms（lav 桶主源，9 FPS 元凶）。改 bk==1 后悬空格只下落、落到 grounded 格才水平推 7
    //    格（kMaxLavaFlowLevel）→ 自然收敛稳态（同水 settle=1）。机制与水完全对称（仅常量不同：岩浆 3s/格
    //    vs 水 0.3s/格、岩浆 maxLevel 3 vs 水 7）。
    for (const LCell &c : cells) {
        if (evapKeys.count(keyOf(c.x, c.y, c.z))) continue; // 退场中的格不扩散
        const int bk = belowKind(c.x, c.y, c.z);
        if (bk == 0) {
            tryAdd(keyOf(c.x, c.y - 1, c.z), quint8(1)); // 下落为流岩浆 state=1（非源）
        }
        if (bk == 1 && c.level < kMaxLavaFlowLevel) {
            for (const auto &d : hd) {
                const int nx = c.x + d[0], nz = c.z + d[1];
                if (nx < 0 || nz < 0 || nx >= W || nz >= D) continue;
                const long long nbKey = keyOf(nx, c.y, nz);
                if (evapKeys.count(nbKey)) continue;
                const quint8 nbId = m_chunks.blockAt(nx, c.y, nz);
                if (nbId == BlockRegistry::Air) {
                    tryAdd(nbKey, quint8(c.level + 1)); // 蔓延到 air
                } else if (nbId == BlockRegistry::Lava) {
                    // re-leveling（同水）：既有流岩浆邻居若能被提供更低 level → 下调（平滑两股岩浆融合）。
                    const quint8 nbLvl = m_chunks.stateAt(nx, c.y, nz);
                    const quint8 offered = quint8(c.level + 1);
                    if (nbLvl > 0 && offered < nbLvl) tryAdd(nbKey, offered);
                }
            }
        }
    }

    // 4) 应用：蒸发 → 新增/重定级（经 keySet 互斥）。setWaterSilent 是通用静默 state 写入口（支持任意 id+state）。
    //    t380 perf：批量写（同 tickWaterFlow 的 m_batchFluid）—— 把「每岩浆格 1× emit worldChanged +
    //    clearDirty」合并为末尾 1 次 emit + clear，消除活跃岩浆扩散期 N 次重建扇出（同 t350 水流批量化的根因）。
    m_batchFluid = true;
    bool anyChange = false;
    // t472：先写岩浆源→Obsidian（与 solidifyTargets/evaps/adds 互不相交 —— obsidianTargets 是岩浆源格，
    //   solidifyTargets 是水格，evaps/adds 操作流岩浆/air 格；故写入顺序安全）。
    for (const LCell &o : obsidianTargets)
        anyChange |= setWaterSilent(o.x, o.y, o.z, BlockRegistry::Obsidian, 0);
    // t438：先写流岩浆凝固水格（静水源→Stone / 流水→Cobblestone；与 evaps/adds 操作的格互不相交 ——
    //   solidifyTargets 是 Water 格，evaps/adds 操作 Lava/Air 格；故写入顺序安全）。
    for (const SolidifyTarget &s : solidifyTargets)
        anyChange |= setWaterSilent(s.x, s.y, s.z, s.result, 0);
    for (const LCell &e : evaps)
        anyChange |= setWaterSilent(e.x, e.y, e.z, BlockRegistry::Air, 0);
    for (const auto &kv : adds) {
        const long long k = kv.first;
        const int x = static_cast<int>(k % W);
        const long long kz = k / W;
        const int z = static_cast<int>(kz % D);
        const int y = static_cast<int>(kz / D);
        anyChange |= setWaterSilent(x, y, z, BlockRegistry::Lava, kv.second);
    }
    m_batchFluid = false;
    flushPendingLightEdits(); // t380r：批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (anyChange) {
        emit worldChanged();      // 一次重建（terrain/water 两段各检各的 dirty → 仅脏 chunk 重建）
        m_chunks.clearAllDirty(); // 两段重建完统一清脏
    } else if (actValid) {
        // t563 ①：同 tickWaterFlow 的盒过滤兜底 —— 盒过滤快照后零写入，但盒外可能仍有岩浆要动（级联中断）→
        //   保持 dirty → 下 tick 盒空 → 全量快照兜底，保证岩浆流场收敛完整。
        m_lavaDirty = true;
    }
    // 5) ignite pass（spec「木质方块邻岩浆概率着火焚毁」）：先收集焚毁目标（扫描），日志后再批量应用。
    //    散布确定性（hashVoxel + 窗口序号，PLAN §2-K）→ 同 seed 同窗口同焚毁，错峰非全部同步烧光。setBlock Air
    //    发 blockBroken → 触发破块粒子 / 音（机制等价 MC 木块被岩浆点燃焚毁）。t344 完整着火系统留后续。
    //    t488 perf 批量焚毁（N 焚毁 1 重建）：旧实现逐焚毁调 setBlock → 每焚毁 1× recomputeLightAround +
    //    1× emit worldChanged + 1× clearAllDirty = 一岩浆 tick 30-48 次 chunk mesh 重建（lav 桶 155ms spike，
    //    用户实测 lav 110.7ms/s 的组成部分）。改：先收集全部焚毁目标，再批量应用 —— m_batchFluid 下直写
    //    （只标脏 + 延迟光照，不 emit）+ 逐焚毁 emit blockBroken（粒子/音反馈，不触发 mesh 重建）+ 末尾
    //    1 次 flushPendingLightEdits + 1 次 emit worldChanged + 1 次 clearAllDirty（同 destroySphereSilent
    //    t383 批量收口模式）。blockBroken 信号语义不变（焚毁仍破块粒子 / 音）；仅把「N 次世界重建」折叠为 1 次。
    auto isWoodLike = [](quint8 id) -> bool {
        using BR = BlockRegistry;
        return id == BR::Log || id == BR::SpruceLog || id == BR::Planks || id == BR::CraftingTable || id == BR::Leaves
            || id == BR::SpruceLeaves // t714 云杉树叶（木质可燃，同橡树叶）
            || id == BR::WoodSlab || id == BR::WoodStairs || id == BR::WoodFence
            || id == BR::WoodPressurePlate || id == BR::WoodDoor || id == BR::WoodTrapdoor || id == BR::Chest
            || id == BR::SprucePlanks || id == BR::SpruceSlab || id == BR::SpruceFence || id == BR::SpruceDoor; // t466 云杉木制品（木质，邻岩浆焚毁）
    };
    struct BurnTarget { int x, y, z; };
    std::vector<BurnTarget> burnTargets;
    for (const LCell &c : cells) {
        const int neigh[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto &n : neigh) {
            const int nx = c.x + n[0], ny = c.y + n[1], nz = c.z + n[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            const quint8 nb = m_chunks.blockAt(nx, ny, nz);
            // t891① 入口门 = isWoodLike ∪ flammable（可燃全表）：点燃面向**全部可燃方块**（dev-plan
            //   「邻可燃方块点燃」——书架 / 树苗 / 草丛此前岩浆完全不碰，机制补齐）；焚毁目标集保持原
            //   isWoodLike（焚毁语义 / 节奏零回归，非木类可燃不进 burnTargets）。
            if (!(isWoodLike(nb) || BlockRegistry::flammable(nb))) continue;
            // 散布概率判定（hashVoxel + 窗口序号 → 确定性伪随机，PLAN §2-K）。t891① 单掷双义（**点燃优先**）：
            //   掷中 → 先走 igniteFlammableAt 单一入口把该邻格**点燃进燃烧态**（栅格 id 不变 + blockIgnited
            //   面火 overlay + 计时烧毁 + 同态蔓延——核此前岩浆路径只有「焚毁」没有「点燃」，机制补齐 MC 1.0
            //   岩浆点燃相邻可燃物语义；湿燃料防火带 / 门整扇湿判收口在入口内部，三入口同口径）；点燃成功
            //   （或已在燃——幂等拒 / 对偶联动早燃）→ 本窗**焚毁让位**（烧毁终局由 tickFire (d) 燃烧计时独家
            //   承担，防一格两份消耗的双触发）；入口拒（非可燃木类如箱子 / 湿燃料）→ 回落既有**焚毁**路径
            //   （原语义与节奏保留——同盐同率，箱子 / 湿板的焚毁行为零回归）。盐值 0x1A7A 不变 → 事件发生
            //   率与既有节奏完全一致，仅命中后的语义分层。**焚毁目标仍限 isWoodLike**（非木类可燃——书架 /
            //   树苗 / 草丛——掷中且点燃被拒（湿）→ 本窗跳过不焚毁，等下窗再试点燃）。
            const quint32 hv = hashVoxel(m_seed ^ 0x1A7A, nx, ny, nz) ^ (quint32(m_lavaIgniteIndex) * 2654435761u);
            if ((hv % 100u) < unsigned(kLavaIgnitePct)) {
                if (igniteFlammableAt(nx, ny, nz) || isBurningAt(nx, ny, nz)) continue;
                if (isWoodLike(nb))
                    burnTargets.push_back({nx, ny, nz});
            }
        }
    }
    // t488：box=活动盒是否命中（1=盒过滤快照 / 0=全量兜底）—— 看 cells 数量对比即可知盒收窄了多少扫描范围。
    //   burns=本 tick 岩浆焚毁木块数（ignite pass 收集数；成本在下方批量 reflood+重建，不在此行计时内）。
    //   注：elapsed() 单位 ms（旧版误标 "us"，t488 改正 —— 此值含末尾批量 reflood+emit 重建，非纯扫描）。
    qInfo("vo.perf: tickLavaFlow %lldms cells=%d writes=%d settled=%d box=%d burns=%d",
          t380t.elapsed(), int(cells.size()),
          int(obsidianTargets.size() + solidifyTargets.size() + evaps.size() + int(adds.size())),
          anyChange ? 0 : 1, actValid ? 1 : 0, int(burnTargets.size()));

    if (!burnTargets.empty()) {
        m_batchFluid = true;
        for (const BurnTarget &b : burnTargets) {
            const quint8 oldId = m_chunks.blockAt(b.x, b.y, b.z);
            if (oldId == BlockRegistry::Air) continue; // 两岩浆格邻同一木块重复命中 → 已焚毁，跳过（防重复 blockBroken / 光编辑）
            m_chunks.setBlock(b.x, b.y, b.z, BlockRegistry::Air); // 直写 + 标脏（含跨 chunk 边界邻接），不 emit
            noteGrowthWrite(b.x, b.y, b.z, oldId, BlockRegistry::Air); // t425：木类非生长方块 → no-op，保持一致
            noteFluidWrite(b.x, b.y, b.z, oldId, BlockRegistry::Air);  // perf：木类非流体 → no-op，保持一致
            noteIceWrite(b.x, b.y, b.z, oldId, BlockRegistry::Air);    // t495：木类非冰 → no-op，保持一致
            noteFireWrite(b.x, b.y, b.z, oldId, BlockRegistry::Air);  // t724：木类非火 → no-op，保持一致（若焚毁邻火格上木则由本 tick 侧 guard 兜底）
            m_pendingLightEdits.push_back({b.x, b.y, b.z, true});      // 木→air 天光通（遮光块消失，sky），延迟联合 reflood
            emit blockBroken(b.x, b.y, b.z, int(oldId));               // 焚毁破块粒子 / 音（机制等价 MC 燃烧破块反馈）
            // review24 #3 口径合一：旧版逐焚毁格只补 cactus / deadbush / sugarcane / snowlayer 四钩子 →
            //   岩浆焚毁铁轨支撑后轨悬浮（#15 病征的另一入口）、压力板 / 花 / 活板门族同样漏。改调
            //   recheckAttachmentsAfterClear 单一入口（含上述四钩子 + rail / plate / flower / trapdoor /
            //   6 邻火把——未来新增附着物只扩 recheck 一处）；重力复检仍显式补调（recheck 刻意不含，防
            //   重入）。recheck 内各族实际掉落时自 emit worldChanged（check* 兄弟口径）→ 批量段中一次
            //   中间重建（同 setWaterSilent 挂 checkRailOnEdit 的既有先例），批量终态不受破坏。
            recheckAttachmentsAfterClear(b.x, b.y, b.z, oldId);
            checkGravityBlockOnEdit(b.x, b.y, b.z, oldId, BlockRegistry::Air); // t799：沙/沙砾失撑复检（焚毁木支撑 → 正上方沙柱坍落，同 setBlock 路径）
            breakNetherPortalsAround(b.x, b.y, b.z); // review24 #3：岩浆焚毁门面邻格可燃木 → 熄门（与火吞噬同块木同语义；#27 钩此前漏挂本路径——oldId 必非 Air 木类）
            pokeFluidDirty(b.x, b.y, b.z); // 焚毁邻接流体 → 标脏 + 活动盒扩展（级联焚毁 / 水流入新坑，同旧 setBlock 语义）
        }
        m_batchFluid = false;
        flushPendingLightEdits(); // 延迟光照重算 → 联合盒一次 refloodBox（无编辑则 no-op）
        emit worldChanged();       // 一次重建（terrain/water 两段各检各的 dirty → 仅脏 chunk 重建）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏（同逐格路径 emit→clear 顺序）
    }
}

// t724 火焰方块系统 tick（见 world.h 头注释）。机制等价 MC 1.0 fire：每窗对活跃火格做寿命 / 蔓延 / 上窜
//   判定 + 对燃烧格做计时 / 抑制 / 同态蔓延（t843 第 4 次语义重做：可燃物直燃双轨制——火格（3D 立地火）
//   与燃烧格（可燃方块本体着火，栅格 id 不变）并存）。散布确定性哈希（PLAN §2-K，同 tickLavaFlow ignite
//   pass）。遍历 m_fireCells / m_burningCells 位置索引（O(格数)，lessons perf-fluid-scan：绝不全图扫描）。
//   写入走 4 参数 setBlock（火格写入低频 —— 每窗至多每格 1 写，无需 lava 式批量收口；setBlock 发
//   blockBroken/blockPlaced → 破块粒子/音 + Main.qml fireHost delegate 挂卸自动跟随）。
//   Review 2026-08-23 #5 抑制层（t843 已接入双轨）：露天降雨 / 邻水 → 火格加速自熄 + 蔓延掷骰减半 +
//   湿燃料（目标 6 邻含水）不点燃；燃烧格掷浇熄（火灭块存）。抑制判定收口 fireRainExposedAt /
//   fireWaterNeighborAt（紧随本函数，火格 / 燃烧格两侧共用）。
//   快照校验：迭代中 setBlock 会增删 m_fireCells（重入 noteFireWrite）→ 先拷贝键集再遍历；blockAt != Fire
//   的陈旧项（防御：批量直写路径漏 note 时不崩）直接跳过不误判。
void World::tickFire()
{
    if (++m_fireTickCounter < kFireTickInterval) return; // 节流：0.5s/窗
    m_fireTickCounter = 0;
    ++m_fireIntervalIndex; // 窗口序号（喂 hashVoxel 散布概率 → 不同窗不同火格错峰判定）
    if (m_fireCells.empty() && m_burningCells.empty()) return;

    const int W = m_width, D = m_depth, H = m_height;
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    // 先拷贝快照再遍历：本 tick 的 setBlock 会增删 m_fireCells（重入 noteFireWrite），直接迭代集合
    //   是 UB（迭代器失效）。快照内陈旧项（blockAt != Fire）跳过。
    std::vector<quint64> snapshot(m_fireCells.begin(), m_fireCells.end());

    for (const quint64 k : snapshot) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        if (x < 0 || y < 0 || z < 0 || x >= W || y >= H || z >= D) continue; // 越界防御
        if (m_chunks.blockAt(x, y, z) != BlockRegistry::Fire) continue;     // 陈旧项跳过

        // (a0) t843 失撑即灭：自身 6 邻无实体且下方非火柱 → 立即熄灭（非概率、不等窗——「立地火支撑消失
        //   → 立即熄灭」的 tick 侧兜底；玩家破支撑的即时路径走 checkFireOnEdit 编辑钩子。上窜火 (c) 的
        //   连续性支撑 = 下方火格，柱基熄 → 柱体逐窗失撑级联塌）。
        if (!fireSupportedAt(x, y, z)) {
            setBlock(x, y, z, BlockRegistry::Air);
            continue;
        }

        // (a) 寿命 + 环境抑制：6 邻（kNb 已含下方 {0,-1,0}）均无可燃方块 → 火无燃料，按概率自熄（setBlock
        //     Air → blockBroken 粒子/音 + QML fireHost 收 delegate + noteFireWrite 移除索引）。机制等价 MC
        //     无燃料火渐熄。审查修 B13（t724-t729 复盘）：旧注释写「6 邻 + 下方」——kNb[6] 本身含下方，
        //     逻辑对注释误导，改准。
        //     Review 2026-08-23 #5：露天降雨（fireRainExposedAt）或自身 6 邻含水（燃料扫描顺带采集）→
        //     抑制态，按 kFireSuppressExtinguishPct 加速自熄——**压过燃料**（被雨浇 / 水泡的火即使邻着
        //     可燃物也在熄灭中：雨天 / 水桶是玩家对火势的反制手段）。抑制态幸存火不 continue：仍走 (b)
        //     但掷骰减半（余烬被浇时偶尔溅火星，非硬停）；本窗掷中熄灭则不再外传。
        bool hasFuel = false, waterAdjacent = false;
        for (const auto &n : kNb) {
            const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            const quint8 nbId = m_chunks.blockAt(nx, ny, nz);
            if (BlockRegistry::flammable(nbId)) hasFuel = true;          // 燃料（可燃邻）
            else if (nbId == BlockRegistry::Water) waterAdjacent = true; // #5 邻水（抑制源之一，同扫顺采免二次遍历）
            if (hasFuel && waterAdjacent) break;                         // 两态齐知 → 早退
        }
        const bool rainExposed = fireRainExposedAt(x, y, z); // #5 露天降雨（晴天全局早退，近零开销）
        const bool suppressed = rainExposed || waterAdjacent;
        if (!hasFuel || suppressed) {
            const quint32 hv = hashVoxel(m_seed ^ 0xF177, x, y, z) ^ (quint32(m_fireIntervalIndex) * 2654435761u);
            const int extinguishPct = suppressed ? kFireSuppressExtinguishPct : kFireExtinguishPct;
            if ((hv % 100u) < unsigned(extinguishPct))
                setBlock(x, y, z, BlockRegistry::Air);
            if (!hasFuel) continue; // 无燃料火本窗不做蔓延 / 上窜（燃料都不在，谈不上烧过去）
            if (m_chunks.blockAt(x, y, z) != BlockRegistry::Fire) continue; // 抑制掷中已熄 → 本窗不再外传
        }

        // 安全阀：活跃火格 + 燃烧格合计超 cap → 本窗不再新增（既有火照常走 (a) 熄灭收敛；防链式大火烧穿
        //   重建预算。t843 起燃烧格与火格共用一份预算——两者都是「链式烧穿 mesh 重建预算」的消耗方）。
        if (int(m_fireCells.size()) + int(m_burningCells.size()) > kFireCellCap) continue;

        // (b) 蔓延：对 6 邻**逐格**独立掷 kFireSpreadPermille（#5 叠加补偿后 2.5%/邻/窗；t804 逐邻独立掷修
        //     「点不然木头」体感）。#5 叠加补偿：逐邻 5% 时被 k 个火格包围的块每窗 1-(1-p)^k（k=2~3 →
        //     10~14%/窗 ≈ 3~6s/块），多火源下木屋几十秒烧穿且无反制 → 降到 2.5%（k=2 → ~5%/窗 ≈ 10s/块；
        //     单点燃 ~20s/块，兼顾两体感）。抑制态幸存火掷骰减半（kFireSpreadDampPermille）。#5 湿燃料
        //     防火带不在此处拦（收口在 igniteFlammableAt 内部——三入口统一口径，见其实现注释）。
        //     t843 语义重做：命中 → 可燃邻**进燃烧态**（igniteFlammableAt 单一入口：栅格 id 不变 + 侧表
        //     计时 + blockIgnited 信号驱动面火 overlay；门整扇联动收口其中）——旧「setBlock(Fire) 吞块
        //     替换」退役（机制对齐 MC fire-on-face：火舔到可燃物 = 物本身着火，非火吞物；烧毁发生在燃烧
        //     计时归零的 (d) pass）。逐邻掷须邻间独立：哈希混入邻格坐标（x*3+nx 等 6 邻互异）防同一掷值
        //     复用于多邻；掷基 1000（‰）承接 2.5% 半个百分点粒度。kFireCellCap 安全阀不变。
        const int spreadPermille = suppressed ? kFireSpreadDampPermille : kFireSpreadPermille;
        for (const auto &n : kNb) {
            const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            if (!BlockRegistry::flammable(m_chunks.blockAt(nx, ny, nz))) continue;
            const quint32 hv = hashVoxel(m_seed ^ 0xF179, x * 3 + nx, y * 3 + ny, z * 3 + nz)
                               ^ (quint32(m_fireIntervalIndex) * 2654435761u);
            if ((hv % 1000u) < unsigned(spreadPermille))
                igniteFlammableAt(nx, ny, nz); // t843：点燃为燃烧态（幂等：已在燃 → 内部 no-op）
        }

        // (c) 上窜：下方格 == Fire（火柱）且上方为空气 → 概率上方生火（火焰柱向上舔；机制等价 MC 火向
        //     上方空气格蔓延）。上方已非空气（已烧到 / 已熄）→ no-op。
        if (y > 0 && m_chunks.blockAt(x, y - 1, z) == BlockRegistry::Fire
            && y + 1 < H && m_chunks.blockAt(x, y + 1, z) == BlockRegistry::Air) {
            const quint32 hv = hashVoxel(m_seed ^ 0xF17B, x, y, z) ^ (quint32(m_fireIntervalIndex) * 2654435761u);
            if ((hv % 100u) < unsigned(kFireRisePct))
                setBlock(x, y + 1, z, BlockRegistry::Fire);
        }
    }

    // (d) t843 燃烧态推进（快照遍历 m_burningCells——迭代中的烧毁摘除 / 同态点燃会改表）。每窗三步：
    //   陈旧校验 → 抑制浇熄掷骰 → 计时-1（归零烧毁）/ 同态蔓延掷骰。散布哈希用独立盐值（0xF17D 浇熄 /
    //   0xF17E 蔓延）与火格掷骰（0xF177/0xF179）解耦——同一邻格被火格与燃烧格同时掷骰时两次独立试验。
    if (m_burningCells.empty()) return;
    const QHash<quint64, quint8> burnSnapshot(m_burningCells);
    for (auto it = burnSnapshot.cbegin(), burnEnd = burnSnapshot.cend(); it != burnEnd; ++it) {
        int x, y, z;
        unpackGrowthCell(it.key(), x, y, z);
        if (x < 0 || y < 0 || z < 0 || x >= W || y >= H || z >= D) { // 越界防御
            m_burningCells.remove(it.key());
            continue;
        }
        const quint8 id = m_chunks.blockAt(x, y, z);
        if (!BlockRegistry::flammable(id)) { // 陈旧项（被挖 / 被静默直写替换——setBlock 主入口写时已清）
            m_burningCells.remove(it.key());
            continue;
        }
        // 抑制浇熄（#5 三谓词接入燃烧轨）：露天降雨 / 自身 6 邻水 → 按 kFireSuppressExtinguishPct 掷浇熄。
        //   **火灭块存**：摘侧表项但不动栅格（被雨浇 / 水泼的燃块保住本体——玩家反制手段；对照火格抑制 =
        //   连火一起熄，因火格本身就是暂态）。
        //   review25 #2：本摘表路径栅格不变（无 blockBroken）+ 侧表直摘（无 worldChanged）→ 补发
        //   blockDoused(x,y,z) 精确驱动 QML 面火 overlay 摘除（否则假火 delegate 永久残留 / 泄漏）。
        if (fireRainExposedAt(x, y, z) || fireWaterNeighborAt(x, y, z)) {
            const quint32 hv = hashVoxel(m_seed ^ 0xF17D, x, y, z)
                               ^ (quint32(m_fireIntervalIndex) * 2654435761u);
            if ((hv % 100u) < unsigned(kFireSuppressExtinguishPct)) {
                m_burningCells.remove(it.key());
                emit blockDoused(x, y, z);
                continue;
            }
        }
        // 计时-1 → 归零即烧毁（t724 语义：无掉落——烧毁走 setBlock(Fire) 放置语义替换，不发 blockBroken /
        //   不走挖块掉落链）。烧穿位在 cap 内**燃起 3D 余烬火**（setBlock Fire → blockPlaced → QML fireHost
        //   挂 delegate；链式烧穿的能量来源——余烬火持续对可燃邻掷骰，木墙/木屋可靠烧穿而非随燃块耗尽
        //   而止，机制等价 MC 烧穿位留火）；cap 外直接 Air（安全阀口径与 (b)/(c) 一致）。门烧毁收尾：配对
        //   半扇若仍是门**且不在燃烧中**（独立计时未到 / 点燃时无对偶后补放的半门）→ 同窗一并清（#16
        //   整扇燃烧的收尾面）。对偶仍在燃（联动同计时 → 快照遍历顺序导致本窗尚未处理它）→ 跳过不抢拍：
        //   抢写 Air 会误发 blockBroken（烧毁无掉落语义破口）+ 摘掉对偶的燃烧态使其失去自行烧毁机会。
        //   注意 state 在 setBlock(Fire) 后已被重置——判向用配对半扇自身 state（快照教训同 t134/#16：先写
        //   后读必丢位；这里对偶格的 state 未被动过，直接读它）。
        const int remain = int(it.value()) - 1;
        if (remain <= 0) {
            const quint8 st = m_chunks.stateAt(x, y, z); // 烧毁前快照本格 state（判配对方向；setBlock 后即丢）
            m_burningCells.remove(it.key());
            const bool flare = int(m_fireCells.size()) + int(m_burningCells.size()) <= kFireCellCap;
            setBlock(x, y, z, flare ? BlockRegistry::Fire : BlockRegistry::Air);
            if (BlockRegistry::isDoor(id)) {
                const int py = ((st & 8) != 0) ? y - 1 : y + 1; // 配对半扇（上配下 y-1 / 下配上 y+1）
                if (py >= 0 && py < H && BlockRegistry::isDoor(m_chunks.blockAt(x, py, z))
                    && !m_burningCells.contains(packGrowthCell(x, py, z))
                    && !fireWaterNeighborAt(x, py, z) // review24 #1：湿对偶跳过——点燃后才泼到半扇上的水同样
                                                       //   保住该半扇（整扇湿判拦的是点燃入口，这里拦烧尽收尾；
                                                       //   无此守卫则「干半扇烧尽 → 湿半扇连带 Air」防火带破）
                    && !fireRainExposedAt(x, py, z)) // review25 #9：雨浇对偶同护——对偶半扇已被雨掷中浇熄
                                                     //   （(d) 抑制掷骰「火灭块存」）或本身露天淋雨时，本半扇
                                                     //   烧尽收尾不得连带清 Air（与点燃入口/浇熄掷骰同判
                                                     //   fireRainExposedAt；无此守卫则水泼保得住、雨浇保不住
                                                     //   ——抑制源行为不对称，露天门整扇无一生还）
                    setBlock(x, py, z, BlockRegistry::Air); // 同窗收尾（对偶已自行烧毁 → 已非门不命中；
                                                            //   对偶在燃 → 留它本窗自烧，不误发 broken）
            }
            // review27 #7：烧尽终局补附着复检——本格已燃失（Fire/Air），贴墙火把 / 侧挂活板门 / 顶立门 /
            //   铁轨 / 画作等附着物随支撑消失掉落，不再悬空残留（legacy 焚毁路径 recheckAttachmentsAfterClear
            //   同款收口；t891 岩浆点燃改道 + t843 火蔓延两路烧尽终局共用此处，后者为既有缺口顺带收口）。
            //   门自身不入本扫：烧尽门的配对半扇清理由上方带湿/雨守卫的专用分支负责——本扫的 door 支撑
            //   复检无湿守卫，抢跑会把 review24 #1 / review25 #9 特意保住的湿/雨对偶半扇误清。
            if (!BlockRegistry::isDoor(id))
                recheckAttachmentsAfterClear(x, y, z, id);
            continue;
        }
        m_burningCells[it.key()] = quint8(remain);
        // 同态蔓延：向 6 邻可燃格掷 kFireSpreadPermille（与火格 (b) 同率共常量、独立盐值；湿燃料拦截收口在
        //   igniteFlammableAt 内部，同 (b) 口径）。安全阀：合计超 cap → 本窗不再新增。
        if (int(m_fireCells.size()) + int(m_burningCells.size()) > kFireCellCap) continue;
        for (const auto &n : kNb) {
            const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            if (!BlockRegistry::flammable(m_chunks.blockAt(nx, ny, nz))) continue;
            const quint32 hv = hashVoxel(m_seed ^ 0xF17E, x * 3 + nx, y * 3 + ny, z * 3 + nz)
                               ^ (quint32(m_fireIntervalIndex) * 2654435761u);
            if ((hv % 1000u) < unsigned(kFireSpreadPermille))
                igniteFlammableAt(nx, ny, nz); // 幂等：已在燃 → 内部 no-op（多燃源同窗同标不重置计时）
        }
    }
}

// Review 2026-08-23 #5 火环境抑制判定 ①：露天降雨（见 world.h 头注释）。判定序 = 便宜前置：晴天（全局
//   Clear）首判早退——isPrecipitatingAt 内含 biomeAt（4 次 fBm×4 阶噪声，entitymanager t 节流先例），
//   绝不为每火格在晴天白跑（lessons perf：环境判定先查全局态门）；越界防御次之；skyLight（数组读）先于
//   群系解析。语义与 t385 作物浇雨（world.cpp tickCropGrowth）/ mob 雨灭火（entitymanager）同口径：
//   skyLightAt>=15 = 头顶无遮挡（屋内 / 树冠下淋不到）。
bool World::fireRainExposedAt(int x, int y, int z) const
{
    if (m_weather == Weather::Clear) return false; // 晴天全局早退（绝大多数窗零噪声开销）
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth) return false;
    if (m_chunks.skyLightAt(x, y, z) < 15) return false; // 头顶有遮挡 → 淋不到（露天判定，同作物 / mob 口径）
    return isPrecipitatingAt(x, z); // 该列正降水（雨 / 雪 / 雷皆降水皆灭火；沙漠列恒 Clear 天然豁免）
}

// Review 2026-08-23 #5 火环境抑制判定 ②：本格 6 邻含 Water（见 world.h 头注释；OOB 方向跳过，边界火格
//   靠世界内侧判）。一判定两用：火格自身（抑制态加速自熄）与蔓延目标格（湿燃料不点燃）——两侧水敏
//   口径天然合一，t843 火语义重做时随 ① 整体搬走。
bool World::fireWaterNeighborAt(int x, int y, int z) const
{
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &n : kNb) {
        const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
        if (nx < 0 || ny < 0 || nz < 0 || nx >= m_width || ny >= m_height || nz >= m_depth) continue;
        if (m_chunks.blockAt(nx, ny, nz) == BlockRegistry::Water) return true;
    }
    return false;
}

// t843 燃烧计时档位（见 world.h 私有段头注释）：木类族 5s 烧毁；轻质族（叶/苗/草）2s 闪燃。
//   两档与 BlockRegistry::flammable 表同步维护（新增可燃方块默认归木类档）。
int World::burnWindowsFor(quint8 blockId)
{
    using BR = BlockRegistry;
    switch (blockId) {
    case BR::Leaves: case BR::SpruceLeaves:
    case BR::Sapling: case BR::TallGrass:
        return kBurnWindowsLight;
    default:
        return kBurnWindowsWood;
    }
}

// t843 可燃物直燃入口（见 world.h 头注释）：点燃 (x,y,z) 的可燃方块进燃烧态（栅格 id 不变——燃烧是
//   World 层侧表瞬态）。三入口共用：打火石右键可燃方块（PlayerController）/ tickFire (b) 火格蔓延 /
//   tickFire (d) 同态蔓延。门整扇联动（Review #16 燃烧态迁移版）：目标是门 → 配对半扇（state bit3
//   上下互补 y∓1）同为门时一并点燃（门作为整体燃烧；烧毁收尾在 (d)）。幂等：已在燃 / 非可燃 / 越界 →
//   false 不重置计时。emit blockIgnited 每点燃格一信号（呈现层 burningHost 挂面火 overlay）。
bool World::igniteFlammableAt(int x, int y, int z)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth) return false;
    const quint8 id = m_chunks.blockAt(x, y, z);
    if (!BlockRegistry::flammable(id)) return false; // 非可燃（含 Air / Fire / 石类）→ 拒（打火石回退立地火路径）
    if (fireWaterNeighborAt(x, y, z)) return false;  // #5 湿燃料防火带（**三入口统一口径**收口在此：直燃 / 火格
                                                     //   蔓延 / 同态蔓延——水邻可燃物不可点燃，泼出的水即火势
                                                     //   边界；review-g #5 确定性 verdict 随 t843 语义迁移保持）
    // review24 #1 整扇湿判提前：门两半各占一格，水通常只邻其中一半（护城河沿的门 / 船坞门）——只按本格
    //   湿判放行干半扇时，门整扇联动点燃 + (d) 烧毁收尾的「对偶仍是门 → 同窗 Air」兜底会把被水保护的
    //   湿半扇连带焚毁（不可逆，恰是防火带承诺要阻止的损害）。门作为整体燃烧（#16）→ 防火带同按整扇判：
    //   主目标**或其配对半扇**含水 → 整扇不燃（蔓延 / 直燃 / 同态三入口都经本函数 → 一处拦全部）。
    if (BlockRegistry::isDoor(id)) {
        const quint8 stPair = m_chunks.stateAt(x, y, z);
        const int pyPair = ((stPair & 8) != 0) ? y - 1 : y + 1;
        if (pyPair >= 0 && pyPair < m_height
            && BlockRegistry::isDoor(m_chunks.blockAt(x, pyPair, z))
            && fireWaterNeighborAt(x, pyPair, z))
            return false; // 配对半扇含水 → 整扇不燃（湿半扇所在门格永不被点燃焚毁）
    }
    const quint64 k = packGrowthCell(x, y, z);
    if (m_burningCells.contains(k)) return false;    // 已在燃 → 幂等 no-op
    m_burningCells.insert(k, quint8(burnWindowsFor(id)));
    emit blockIgnited(x, y, z);
    // 门整扇联动：本格 state bit3 判上/下半扇 → 配对半扇 y∓1 仍为门（非湿由上方整扇湿判提前保证——任一
    //   半扇含水已整扇 return false，review24 #1）→ 一并点燃（同窗同计时 → 同窗烧毁；(d) 烧毁收尾再兜
    //   后补放的半门）。
    if (BlockRegistry::isDoor(id)) {
        const quint8 st = m_chunks.stateAt(x, y, z);
        const int py = ((st & 8) != 0) ? y - 1 : y + 1;
        if (py >= 0 && py < m_height) {
            const quint8 pid = m_chunks.blockAt(x, py, z);
            if (BlockRegistry::isDoor(pid)) {
                const quint64 pk = packGrowthCell(x, py, z);
                if (!m_burningCells.contains(pk)) {
                    m_burningCells.insert(pk, quint8(burnWindowsFor(pid)));
                    emit blockIgnited(x, py, z);
                }
            }
        }
    }
    return true;
}

// t843 燃烧态查询（见 world.h 头注释）：侧表命中 + 防御校验（块已被换成非可燃 → 视未燃——静默直写路径
//   替换块后 ≤1 窗内 (d) 自愈摘除，期间查询不给假阳性）。
bool World::isBurningAt(int x, int y, int z) const
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth) return false;
    if (!m_burningCells.contains(packGrowthCell(x, y, z))) return false;
    return BlockRegistry::flammable(m_chunks.blockAt(x, y, z));
}

// t843 3D 立地火支撑判定（见 world.h 头注释）：6 邻任一「实体可依附面」（isSolid **或** isCollidable——
//   门 / 活板门等薄板族 solid=false 但碰撞实体（ShapeDoor 薄板碰撞，isCollidable 与 collisionAABBs 共用
//   单一权威），MC 里火可贴门面烧；Leaves 本就 solid=true）或正下方是 Fire（(c) 上窜火的火柱链连续性
//   支撑）→ 有撑。草丛 / 树苗（ShapeNone 无碰撞）不算支撑（MC 同款：非实体面不挂火）。
bool World::fireSupportedAt(int x, int y, int z) const
{
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    if (y > 0 && m_chunks.blockAt(x, y - 1, z) == BlockRegistry::Fire) return true; // 火柱链
    for (const auto &n : kNb) {
        const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
        if (nx < 0 || ny < 0 || nz < 0 || nx >= m_width || ny >= m_height || nz >= m_depth) continue;
        const quint8 nid = m_chunks.blockAt(nx, ny, nz);
        if (BlockRegistry::isSolid(nid) || BlockRegistry::isCollidable(nid, m_chunks.stateAt(nx, ny, nz)))
            return true;
    }
    return false;
}

// t843 编辑后立地火失撑即时复检（见 world.h 头注释；同 checkRailOnEdit 编辑钩子族模式）：本格刚发生
//   编辑 → 扫 6 邻的 Fire 格（支撑方块的消失必发生在火格 6 邻之内，扫一圈即全覆盖），失撑
//   （fireSupportedAt 假）→ setBlock Air **立即**熄灭（发 blockBroken → 破块粒子/音 + QML fireHost 收
//   delegate；不等下一 0.5s 判定窗）。递归有界：熄灭写 Air 再入本检查，但每次移除一个 Fire 格（单调
//   递减）+ Air 非 Fire 不再触发熄灭路径。
void World::checkFireOnEdit(int x, int y, int z)
{
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &n : kNb) {
        const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
        if (nx < 0 || ny < 0 || nz < 0 || nx >= m_width || ny >= m_height || nz >= m_depth) continue;
        if (m_chunks.blockAt(nx, ny, nz) != BlockRegistry::Fire) continue;
        if (!fireSupportedAt(nx, ny, nz))
            setBlock(nx, ny, nz, BlockRegistry::Air); // 失撑即灭（tickFire (a0) 逐窗兜底静默直写路径）
    }
}

// t236 小麦作物生长 tick（见 world.h 头注释）。机制等价 MC 1.0 小麦生长（random-tick 式散布概率升阶段）。
//   节流到 ~每 kCropTickInterval tick（2.5s）做一次成长判定窗口；每窗遍历全图作物格，符合条件者按确定性散布
//   概率升 state 一档（0→1→…→WheatCropStageMax=7 成熟）。写入走 setWaterSilent（静默 state 写，无破/放反馈）。
void World::tickCropGrowth()
{
    FrameProfiler::Scope prof("wCrop"); // perf：含节流 / 早退（无作物 / 全暗零开销仍计极小常数）
    if (++m_cropTickCounter < kCropTickInterval) return; // 节流：每 kCropTickInterval tick（~2.5s）做一次判定
    m_cropTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 1) 快照当前作物格 + 阶段（tick 内栅格不变 —— 升阶段在 pass 末统一应用，避免半遍历态读到刚升的阶段）。
    //   t407：快照涵盖全部三种作物（小麦 / 胡萝卜 / 马铃薯）—— 三者生长机制完全同构（同耕地支撑 + 光照 +
    //   湿润 + 确定性散布概率，复用 WheatCropStageMax 共享阶段上界），故共一生长判定，仅写入时按各自 id。
    //   t425：遍历生长方格索引 m_growthCells（O(生长格数)）替代全图扫描（O(W×D×H)=3.3M）；顺带剔除被直接写入
    //     清掉的生长格（blockAt 已非生长方块 → 索引项过期），防索引随直接写入单调累积。
    struct CCell { int x, y, z; quint8 id; quint8 stage; };
    std::vector<CCell> cells;
    {
        std::vector<quint64> stale;
        for (quint64 k : m_growthCells) {
            int x, y, z;
            unpackGrowthCell(k, x, y, z);
            const quint8 b = m_chunks.blockAt(x, y, z);
            if (!isGrowthBlock(b)) { stale.push_back(k); continue; } // 直接写入清掉 → 剔除过期索引项
            if (b == BlockRegistry::WheatCrop
                || b == BlockRegistry::CarrotCrop
                || b == BlockRegistry::PotatoCrop)
                cells.push_back({x, y, z, b, m_chunks.stateAt(x, y, z)});
        }
        for (quint64 k : stale) m_growthCells.erase(k);
    }

    // 2) 成长判定：每株据「下方耕地支撑 + 头顶光照足 + 未成熟」筛后，按确定性散布概率决定本窗是否升阶段。
    //    散布：hashVoxel(seed, x, y, z) 混入窗口序号 m_cropIntervalIndex 取低 16 位 % 100，落在 [0, kCropGrowPct)
    //    内即升 → 不同株错峰（非全部同步）、同 seed 同窗口序号同结果（无随机源，可复现）。
    std::vector<CCell> grows;
    for (const CCell &c : cells) {
        if (c.stage >= BlockRegistry::WheatCropStageMax) continue;       // 已成熟 → 不再升
        if (c.y == 0) continue;                                           // 世界底无「下方耕地」支撑
        if (m_chunks.blockAt(c.x, c.y - 1, c.z) != BlockRegistry::Farmland)
            continue;                                                     // 下方非耕地 → 不长（作物需耕地支撑）
        if (m_chunks.skyLightAt(c.x, c.y, c.z) < kCropMinLight) continue; // 头顶天光不足 → 不长（夜间/洞穴）
        // t385 雨水浇作物（spec「浇作物」）：作物露天（skyLightAt>=15 = 头顶无遮挡）且所在列正降水（雨/雪/雷，
        //   群系解析）→ 本窗生长概率翻倍（机制等价 MC 雨水加速作物生长 / 维持耕地湿润）。降水是世界态（m_weather）
        //   运行期模拟，故此判定打破「确定性哈希」纯函数性 —— 与 rain 灭火同理（动态天气影响世界模拟，合理）。
        const bool cropRained = (m_chunks.skyLightAt(c.x, c.y, c.z) >= 15) && isPrecipitatingAt(c.x, c.z);
        // t406 耕地湿润加速作物（spec「越湿作物长得越快」）：读支撑耕地 state 低 2 位湿润等级（0..3），等级越高
        //   升阶段概率倍率越大（dry 1× → wettest 4×）。湿润等级由 tickFarmlandHydration 据水源邻近距离周期复算 →
        //   近水耕地种的小麦明显比远水快长（机制等价 MC farmland moisture 加速作物生长）。全 int 运算避符号告警。
        const int hydr = int(m_chunks.stateAt(c.x, c.y - 1, c.z) & BlockRegistry::FarmlandHydrationMask);
        const int hydrMul = 1 + hydr; // 1×（干）.. 4×（最湿）
        int growPct = (cropRained ? (kCropGrowPct * 2) : kCropGrowPct) * hydrMul;
        if (growPct > 100) growPct = 100; // 钳到散布概率上界（% 运算域 [0,100)）
        // 确定性散布概率：纯函数于 seed + 位置 + 窗口序号（PLAN §2-K 精神：worldgen 确定性；此处生长模拟亦
        //   走确定性哈希，无 Math.random / 时间源 → 同 seed 同窗口序号下结果一致，便于复现）。全 int 运算
        //   避免符号转换告警（hashVoxel 参数为 int）。
        const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_cropIntervalIndex) * 0x9E3779B9u));
        const int hy = c.y * 7 + int(c.stage);
        const quint32 h = hashVoxel(mixedSeed, c.x, hy, c.z);
        if (int(h & 0xFFFFu) % 100 >= growPct) continue;                  // 散布落空 → 本窗不升
        grows.push_back(c);
    }

    // 3) 应用升阶段（静默批量写：m_batchFluid 收口使每株 setWaterSilent 只写栅格 + 延迟光照重算、**不**
    //    emit worldChanged / 不 clearAllDirty；末尾本窗所有改动一次性 emit + clear，把「N 株升阶段 = N 次
    //    emit worldChanged + N×全 chunk onWorldChanged 扇出 + N×clearAllDirty」折叠为 1 次（同 tickWaterFlow
    //    t350 批量收口模式）。修 perf「非批量静默写 tick 每 setWaterSilent 各发一次 worldChanged → clearAllDirty
    //    清完又被下一格标回 → N 次重建请求」的 mesh 风暴。setWaterSilent 内部对「无变化」早退（作物升阶段
    //    stage→stage+1 必有变化 → anyChange 真 → 末尾 1 次 emit）。无作物可升时 grows 为空 → 零写入零 emit（稳态无开销）。
    m_batchFluid = true;
    bool anyChange = false;
    for (const CCell &g : grows)
        anyChange |= setWaterSilent(g.x, g.y, g.z, g.id, quint8(g.stage + 1)); // t407：按各自作物 id 写回（小麦/胡萝卜/马铃薯）
    m_batchFluid = false;
    flushPendingLightEdits(); // t380r：批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (anyChange) {
        emit worldChanged();       // 一次重建（terrain/water 两段各检各的 dirty → 仅脏 chunk 重建）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏（同 tickWaterFlow emit→clear 顺序）
    }

    ++m_cropIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同窗口不同株错峰）
}

// t406 耕地湿润等级（见 world.h 头注释）。扫水源切比雪夫半径 4 水平 + 同 / 下一层（y / y-1），取最近水源
//   切比雪夫水平距离 → 映射 4 级 0..3（越近水越湿）。机制等价 MC 1.0 farmland hydration（同高 / 低 1 层水滋润、
//   半径 4 内湿润）。只读 m_chunks.blockAt（向下依赖）；世界空 → 0（干态兜底）。
int World::farmlandHydrationLevel(int x, int y, int z) const
{
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return 0;
    constexpr int kRadius = 4; // MC 1.0 farmland hydration 半径（水同高 / 低 1 层、水平 4 格内即滋润）
    int bestDist = kRadius + 1; // 超半径哨兵（= 无水 → 干）
    for (int dy = 0; dy >= -1; --dy) {       // 本层 y + 下一层 y-1（同 / 低 1 层水均滋润）
        const int yy = y + dy;
        for (int dx = -kRadius; dx <= kRadius; ++dx) {
            for (int dz = -kRadius; dz <= kRadius; ++dz) {
                if (m_chunks.blockAt(x + dx, yy, z + dz) != BlockRegistry::Water) continue;
                const int d = std::max(std::abs(dx), std::abs(dz)); // 切比雪夫水平距离
                if (d < bestDist) bestDist = d;
            }
        }
    }
    // 距离 → 等级：dist 1→3、2→2、3→1、≥4/无水→0（4 级 0..3，darker=wetter）。
    const int level = kRadius - bestDist; // dist 1→3、2→2、3→1、4→0、哨兵(5)→-1
    return (level > 0) ? level : 0;
}

// t474 附魔台书架加成计数（见 world.h 头注释）。t649 对齐 MC 1.0 书架计数规则（机制等价，无专有资产）：
//   只数**水平切比雪夫距离 == 2 的 16 格环带**（dx/dz 满足 max(|dx|,|dz|)==2；≤1 的贴身格与 ≥3 的远处不计）×
//   **两层高度**（书架 y 层 = 附魔台 y 层 + 上一层 y+1；楼下 / 楼上两层以外的书架不计）的书架；且**书架与附魔台
//   之间须有空气通路**——书架格与附魔台之间的半步格（书架位向附魔台方向 1 格、与书架同 y）须为 Air，紧贴书架
//   堆满整墙（半步格被填）则该书架不计（「中间隔一格空气」的空间要求，t649 前的 5×5×5 立方体计数缺失此判定 →
//   4 个书架即顶满档位的根因）。上限 15。
//   忠实简化（相对 MC 的完整 air 检查）：MC 还查「附魔台自身 y 与 y+1 的中间列空气」，本实现只查**书架半步格**
//   的空气——两者对绝大多数摆法结果一致，差异仅在书架空心塔等罕见构造；以书架半步格为准更直观（每书架独立判定）。
//   纯只读（blockAt + BlockRegistry::isBookshelf）；OOB 返 Air 安全（不计入 / 半步 OOB 视作不通 → 不计）；世界空 → 0。
int World::countBookshelvesAround(int x, int y, int z) const
{
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return 0;
    constexpr int kMaxBookshelves = 15; // spec 上限（>15 仍按 15 算）
    int count = 0;
    // 16 格环带（水平切比雪夫距离 == 2）× 两层（y / y+1）。半步格 = 书架位向附魔台方向走 1 格（同 y）：
    //   书架在 (x+2, z) → 半步 (x+1, z)；书架在 (x-2, z+2) 角位 → 半步 (x-1, z+1)（两轴各半步，对角连线中点）。
    for (int dy = 0; dy <= 1; ++dy) {
        const int yy = y + dy;
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dz = -2; dz <= 2; ++dz) {
                if (std::max(std::abs(dx), std::abs(dz)) != 2) continue; // 只看 ==2 环带（≤1 贴身 / 中间格不计）
                if (!BlockRegistry::isBookshelf(m_chunks.blockAt(x + dx, yy, z + dz))) continue;
                // 半步格空气判定（书架与附魔台间通路）：非 Air（被方块填）→ 该书架不计。
                const quint8 midId = m_chunks.blockAt(x + dx / 2, yy, z + dz / 2);
                if (midId != quint8(BlockRegistry::Air)) continue;
                ++count;
                if (count >= kMaxBookshelves) return kMaxBookshelves; // 早退：达上限即返（避免无谓遍历余格）
            }
        }
    }
    return count;
}

// t691 全图收集指定 id 方块坐标（见 world.h 头注释）：三层 for 直扫 m_chunks.blockAt（C++ 侧一次性
//   O(体积)，非 QML 逐格 Q_INVOKABLE 往返——后者 3.28M 次调用开销不可接受）。读档 / worldgen 直写
//   不发 blockPlaced → 呈现层事件驱动位置表（如附魔台悬浮书）读档后恒空，本方法供其一次性重建。
QVariantList World::collectBlocksOfId(quint8 blockId) const
{
    QVariantList out;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return out;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y)
                if (m_chunks.blockAt(x, y, z) == blockId)
                    out << x << y << z; // 平铺 [x,y,z,...]
    return out;
}

// t406 甘蔗生长 tick（见 world.h 头注释）。机制等价 MC 1.0 sugar cane random-tick 生长。
void World::tickSugarcaneGrowth()
{
    FrameProfiler::Scope prof("wSug"); // perf：含节流 / 早退
    if (++m_sugarcaneTickCounter < kSugarcaneTickInterval) return; // 节流：每 kSugarcaneTickInterval tick（~5s）一窗
    m_sugarcaneTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 1) 快照柱顶甘蔗格（上方为空气的甘蔗格 = 有长高余地的柱顶；中间 / 底格上方是甘蔗 → 非柱顶，跳过）。
    //   t425：遍历生长方格索引 m_growthCells（O(生长格数)）替代全图扫描；顺带剔除过期索引项。
    struct SCell { int x, y, z; };
    std::vector<SCell> tops;
    {
        std::vector<quint64> stale;
        for (quint64 k : m_growthCells) {
            int x, y, z;
            unpackGrowthCell(k, x, y, z);
            const quint8 b = m_chunks.blockAt(x, y, z);
            if (!isGrowthBlock(b)) { stale.push_back(k); continue; }
            if (b != BlockRegistry::Sugarcane) continue;
            if (y + 1 >= H) continue;                                  // 柱顶贴世界顶 → 无上方空间，不长
            if (m_chunks.blockAt(x, y + 1, z) != BlockRegistry::Air) continue; // 上方非空气（被挡）→ 非可长柱顶
            tops.push_back({x, y, z});
        }
        for (quint64 k : stale) m_growthCells.erase(k);
    }
    if (tops.empty()) { ++m_sugarcaneIntervalIndex; return; }

    // 散布种子混入窗口序号（每窗一新种子 → 每柱每窗一新伪随机滚，错峰生长；全 int 运算避符号转换告警）。
    const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_sugarcaneIntervalIndex) * 0x9E3779B9u));

    // 2) 逐柱顶判定：找柱基（向下走到非甘蔗）+ 算柱高 + 柱基邻水 → 散布概率升一格。
    std::vector<SCell> grows;
    for (const SCell &t : tops) {
        // 找柱基（向下走到下方非甘蔗格；世界底兜底）。柱基 = 这株甘蔗的「根」格（其下为沙地支撑）。
        int by = t.y;
        while (by - 1 >= 0 && m_chunks.blockAt(t.x, by - 1, t.z) == BlockRegistry::Sugarcane) --by;
        // t446：仅沙基甘蔗可长（spec「必须沙地支撑」，与 worldgen placeSugarcane 沙顶-only 一致）。柱基支撑格
        //   by-1 须为 Sand —— 排除玩家误放 / 旧世界残留于草地 / 泥土 / 水中的甘蔗柱，关闭「草上长高」残留路径。
        if (by - 1 < 0 || m_chunks.blockAt(t.x, by - 1, t.z) != BlockRegistry::Sand) continue;
        const int height = t.y - by + 1; // 柱高（含柱顶）
        if (height >= kSugarcaneMaxHeight) continue; // 已达 5 格上限 → 停长（spec「max5」）
        // t418 拔高潜力门（列位 + seed 一次性哈希，与窗口无关 → 稳态确定）。worldgen 初生 1..3 高；多数柱止于 3，
        //   仅 kSugarcaneTallPct 潜力柱可超 3 长到 4..5（spec「1..3 common、5 rare」）。修旧「每柱不停长直至封顶
        //   → 稳态全 5 高」bug：高度 ≥3 且无拔高潜力 → 本柱不再升格。用列哈希（同 worldgen 源）非体素哈希，
        //   使「潜力」是柱的固有属性（同柱每窗判定一致），不随窗口抖动。
        if (height >= 3) {
            const quint32 baseHash = hashColumn(m_seed, t.x, t.z);
            if (int(baseHash % 100u) >= kSugarcaneTallPct) continue; // 无拔高潜力 → 止于 3（5 高罕见）
        }

        // 柱基邻水判定（4 水平邻于基 y / 基下一层 y-1）：与 worldgen placeSugarcane 同语义（沙顶邻水）。
        //   基下一层 = 沙地格（worldgen 在 surfaceY 查水）；基层查水兼容海岸浅水。
        //   不邻水 → 永不长（spec「仅邻水处长高」）。
        auto wateredAt = [&](int yy) -> bool {
            if (yy < 0 || yy >= H) return false;
            const int nx[4] = { t.x + 1, t.x - 1, t.x,     t.x };
            const int nz[4] = { t.z,     t.z,     t.z + 1, t.z - 1 };
            for (int i = 0; i < 4; ++i) {
                const int ax = nx[i], az = nz[i];
                if (ax < 0 || az < 0 || ax >= W || az >= D) continue;
                if (m_chunks.blockAt(ax, yy, az) == BlockRegistry::Water) return true;
            }
            return false;
        };
        if (!wateredAt(by) && !wateredAt(by - 1)) continue; // 柱基两层均不邻水 → 不长

        // 确定性散布概率（PLAN §2-K 精神，同 tickCropGrowth）。
        const quint32 h = hashVoxel(mixedSeed, t.x, t.y, t.z);
        if (int(h & 0xFFFFu) % 100 >= kSugarcaneGrowPct) continue; // 散布落空 → 本窗不长
        grows.push_back({t.x, t.y + 1, t.z}); // 在柱顶上方一格长一格
    }

    // 3) 应用生长（setWaterSilent 静默批量写：m_batchFluid 收口使每次写入只写栅格 + 延迟光照，末尾 1 次 emit +
    //    clearAllDirty，把「N 株长高 = N 次 emit worldChanged + N×clearAllDirty（清完又被下一格标回）」折叠为 1 次，
    //    同 tickCropGrowth / tickWaterFlow 批量收口模式。无生长时 grows 为空 → 零写入零 emit（稳态无开销）。
    m_batchFluid = true;
    bool anyChange = false;
    for (const SCell &g : grows)
        anyChange |= setWaterSilent(g.x, g.y, g.z, BlockRegistry::Sugarcane, 0);
    m_batchFluid = false;
    flushPendingLightEdits(); // t380r：批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (anyChange) {
        emit worldChanged();       // 一次重建（仅脏 chunk）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏
    }

    ++m_sugarcaneIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同窗不同柱错峰）
}

// t406 耕地湿润复算 tick（见 world.h 头注释）。机制等价 MC 1.0 farmland 随机 tick 补 / 失水。
void World::tickFarmlandHydration()
{
    FrameProfiler::Scope prof("wFarm"); // perf：含节流 / 早退
    if (++m_farmlandHydrTickCounter < kFarmlandHydrTickInterval) return; // 节流：每 kFarmlandHydrTickInterval tick（~3s）一窗
    m_farmlandHydrTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 快照耕地格 + 当前湿润等级（tick 内栅格不变 —— 写入在末尾统一应用）。
    //   t425：遍历生长方格索引 m_growthCells（O(生长格数)）替代全图扫描；顺带剔除过期索引项。
    struct FCell { int x, y, z; quint8 hydr; };
    std::vector<FCell> cells;
    {
        std::vector<quint64> stale;
        for (quint64 k : m_growthCells) {
            int x, y, z;
            unpackGrowthCell(k, x, y, z);
            const quint8 b = m_chunks.blockAt(x, y, z);
            if (!isGrowthBlock(b)) { stale.push_back(k); continue; }
            if (b == BlockRegistry::Farmland)
                cells.push_back({x, y, z, quint8(m_chunks.stateAt(x, y, z) & BlockRegistry::FarmlandHydrationMask)});
        }
        for (quint64 k : stale) m_growthCells.erase(k);
    }

    // 复算湿润等级；与存档不等才记为待写（changes.hydr 存新等级；避免稳态零意义写入 + 零 worldChanged）。
    std::vector<FCell> changes;
    for (const FCell &c : cells) {
        const quint8 newHydr = quint8(farmlandHydrationLevel(c.x, c.y, c.z));
        if (newHydr != c.hydr) changes.push_back({c.x, c.y, c.z, newHydr});
    }

    // 应用（setWaterSilent 静默批量写 Farmland + 新湿润等级；m_batchFluid 收口把「N 块耕地变湿 = N 次
    //    emit worldChanged + N×clearAllDirty」折叠为 1 次 emit + clear（同 tickCropGrowth / tickWaterFlow 批量
    //    收口模式）。驱动 mesher 顶点色暗化重建 → 肉眼见湿润度变。changes 空（稳态无变化）→ 零写入零 emit。
    m_batchFluid = true;
    bool anyChange = false;
    for (const FCell &c : changes)
        anyChange |= setWaterSilent(c.x, c.y, c.z, BlockRegistry::Farmland, c.hydr);
    m_batchFluid = false;
    flushPendingLightEdits(); // t380r：批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (anyChange) {
        emit worldChanged();       // 一次重建（仅脏 chunk）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏
    }
}

// t325 树叶渐进衰减队列的坐标打包 / 解包（文件内工具）。世界 ≤ 256³（实际 80×80×64），三轴各取低 16 位
//   打包成 quint64 键供 std::unordered_set 去重。入队的坐标恒非负（decayLeavesAround 已钳到 [0,W/H/D)）。
static inline quint64 packLeafCell(int x, int y, int z)
{
    return (quint64(quint16(x)) << 32) | (quint64(quint16(y)) << 16) | quint64(quint16(z));
}
static inline void unpackLeafCell(quint64 k, int &x, int &y, int &z)
{
    x = int(quint16(k >> 32));
    y = int(quint16(k >> 16));
    z = int(quint16(k));
}

// t305 树叶失撑检测（t325 改造：见 world.h 头注释）。机制等价 MC 1.0 叶衰：叶子距最近原木 >4 格（切比雪夫）即失撑。
//   玩家破原木 → 扫破块点周围 kScanRadius 盒内叶子，逐叶查 kDecayRadius 内有无原木，无则失撑。
//   t325：失撑叶**入渐进衰减队列 m_decayingLeaves**（按坐标去重）→ 不再瞬时清。持久叶（玩家放置，state 带
//   PersistentLeafBit）跳过。本方法只入队；清叶 + 光场重算 + worldChanged 收口到 tickLeafDecay（逐窗按概率渐退）。
void World::decayLeavesAround(int x, int y, int z)
{
    constexpr int kScanRadius  = 7;  // 扫描盒半径（覆盖单棵橡树树冠 ~5 宽 + 余量；树冠在主干顶 ±2 内）
    constexpr int kDecayRadius = 4;  // 叶子存活所需距原木的切比雪夫距离（机制等价 MC 1.0 叶 4 格内不衰）
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 收集失撑叶（kDecayRadius 切比雪夫距离内无原木 + 非持久叶）→ 入渐进衰减队列（坐标去重，多次破原木 /
    //   多棵同扫只入队一次）。不立即清叶、不发 worldChanged —— 渐退由 tickLeafDecay 驱动。
    for (int dx = -kScanRadius; dx <= kScanRadius; ++dx)
        for (int dy = -kScanRadius; dy <= kScanRadius; ++dy)
            for (int dz = -kScanRadius; dz <= kScanRadius; ++dz) {
                const int lx = x + dx, ly = y + dy, lz = z + dz;
                if (lx < 0 || ly < 0 || lz < 0 || lx >= W || ly >= H || lz >= D) continue;
                // t714：云杉叶（SpruceLeaves）与橡树叶同入衰减候选（同族叶机制；持久位 state bit0 同语义）。
                const quint8 leafId = m_chunks.blockAt(lx, ly, lz);
                if (leafId != BlockRegistry::Leaves && leafId != BlockRegistry::SpruceLeaves) continue;
                if ((m_chunks.stateAt(lx, ly, lz) & BlockRegistry::PersistentLeafBit) != 0)
                    continue; // t305 玩家放置叶（持久）不衰
                // 查 kDecayRadius 切比雪夫距离内有无原木（任一命中即保留 —— 仍有原木支撑）。
                //   t395：原木支撑同时认橡木 Log 与云杉 SpruceLog（云杉树冠靠 SpruceLog 主干支撑；否则云杉叶会
                //   误判失撑而衰减）。机制等价 MC「叶距任一原木类 ≤4 格即不衰」。
                bool hasLog = false;
                for (int ox = -kDecayRadius; ox <= kDecayRadius && !hasLog; ++ox) {
                    for (int oy = -kDecayRadius; oy <= kDecayRadius && !hasLog; ++oy) {
                        for (int oz = -kDecayRadius; oz <= kDecayRadius && !hasLog; ++oz) {
                            const quint8 nb = m_chunks.blockAt(lx + ox, ly + oy, lz + oz);
                            if (nb == BlockRegistry::Log || nb == BlockRegistry::SpruceLog) {
                                hasLog = true;
                            }
                        }
                    }
                }
                if (hasLog) continue; // 仍有原木支撑 → 保留（不入队）
                m_decayingLeaves.insert(packLeafCell(lx, ly, lz)); // 失撑 → 入渐进衰减队列（去重）
            }
}

// t325 树叶渐进消退 tick（见 world.h 头注释）。机制等价 MC 1.0 叶衰 random-tick 渐退：队列内每叶每窗按散布概率
//   kLeafDecayPct 独立判定是否本窗消失 → 几何分布散布寿命（平均 ~40s、中位 ~28s、长尾 90s+，非瞬时全消；
//   t379 在 t325 基础上放慢约 2.5×）。
//   命中叶批量静默清（m_chunks.setBlock 直写 Air + 标脏，不发 broken/placed → 无破叶粒子/音，自然衰减无反馈）
//   + 末尾对受影响区一次 refloodBox 重算光场 + 一次 worldChanged（避免逐叶 N 次光场重算 + N 次重建请求）。
//   队列空（稳态无失撑叶）→ 零开销早退；本窗无命中 → 零写入、零 worldChanged。散布确定性哈希（PLAN §2-K 精神，
//   同 tickCropGrowth/tickSaplingGrowth：seed + 位置 + 窗口序号 → 可复现，无 Math.random / 时间源）。
void World::tickLeafDecay()
{
    FrameProfiler::Scope prof("wLeaf"); // perf：含队列空早退（稳态零开销）
    if (m_decayingLeaves.empty()) return;                       // 稳态（无失撑叶）→ 零开销早退
    if (++m_leafDecayTickCounter < kLeafDecayTickInterval) return; // 节流：每 kLeafDecayTickInterval tick（~0.4s）开一窗
    m_leafDecayTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 散布种子混入窗口序号（每窗一新种子 → 每叶每窗一新伪随机滚，渐进渐退；全 int 运算避符号转换告警）。
    const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_leafDecayIntervalIndex) * 0x9E3779B9u));

    // 逐叶判定：队列项可能已被他途清除（玩家破叶 / 爆炸 / 重置）→ 块非叶（Leaves / SpruceLeaves）即视为已消失，
    //   出队（不再衰减）。命中叶先记坐标（decayed）+ 累计受影响 AABB，末尾批量写 + 一次 reflood + worldChanged。
    std::vector<quint64> decayed;
    int minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    bool any = false;
    for (auto it = m_decayingLeaves.begin(); it != m_decayingLeaves.end(); ) {
        int lx, ly, lz;
        unpackLeafCell(*it, lx, ly, lz);
        const quint8 qid = m_chunks.blockAt(lx, ly, lz);
        if (qid != BlockRegistry::Leaves && qid != BlockRegistry::SpruceLeaves) {
            it = m_decayingLeaves.erase(it); // 已被他途清除 → 出队
            continue;
        }
        const quint32 h = hashVoxel(mixedSeed, lx, ly, lz);
        if (int(h & 0xFFFFu) % 100 < kLeafDecayPct) {
            decayed.push_back(*it);                       // 本窗命中 → 待清
            if (!any) { minX = maxX = lx; minY = maxY = ly; minZ = maxZ = lz; any = true; }
            else {
                if (lx < minX) minX = lx;
                if (lx > maxX) maxX = lx;
                if (ly < minY) minY = ly;
                if (ly > maxY) maxY = ly;
                if (lz < minZ) minZ = lz;
                if (lz > maxZ) maxZ = lz;
            }
            it = m_decayingLeaves.erase(it);              // 命中 → 出队（不再参与后续窗口）
        } else {
            ++it;                                         // 本窗未命中 → 留队，下窗再滚
        }
    }

    ++m_leafDecayIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同叶错峰渐退）
    if (decayed.empty()) return; // 本窗无命中 → 零写入、零 worldChanged

    // 批量静默清叶（m_chunks.setBlock 直写 + 标脏，不发 broken/placed）。末尾统一重算光场 + 一次 worldChanged
    //   （避免逐叶 setBlock 的 N 次光场重算 + N 次重建；机制同旧 decayLeavesAround 批量清叶）。
    for (quint64 k : decayed) {
        int lx, ly, lz;
        unpackLeafCell(k, lx, ly, lz);
        m_chunks.setBlock(lx, ly, lz, BlockRegistry::Air);
    }
    // 对受影响区做一次有界盒光场重 flood（叶 solid=true → air 改变遮光，天光从原叶位漏下，需重算）。
    //   盒扩 1 格余量（光传播到邻格）；钳到世界界内。doSky=true 两通道都重算（叶遮挡影响天光，叶本身非火把）。
    const int bx0 = std::max(0, minX - 1), by0 = std::max(0, minY - 1), bz0 = std::max(0, minZ - 1);
    const int bx1 = std::min(W - 1, maxX + 1), by1 = std::min(H - 1, maxY + 1), bz1 = std::min(D - 1, maxZ + 1);
    refloodBox(bx0, by0, bz0, bx1, by1, bz1, /*doSky=*/true);
    emit worldChanged();
    m_chunks.clearAllDirty();
    qInfo("vo.edit: leaves decayed = %d", int(decayed.size())); // 可观测：本窗渐退叶计数
}

// t320 爆炸批量破坏（见 world.h 头注释；机制同 decayLeavesAround 批量清叶：N 写 1 emit，避重建风暴）。
std::vector<World::DestroyedVoxel> World::destroySphereSilent(int cx, int cy, int cz, float radius)
{
    std::vector<DestroyedVoxel> destroyed;
    // t1033 被毁床登记（state 须在 setBlock(Air) 前 capture——id 变更重置 state=0，同门 t134；
    //   语义信号见 world.h signals 注释，末尾统一 emit，同本函数 N 写 1 emit 批量口径）。
    struct BedCellHit { int x, y, z; quint8 state; };
    std::vector<BedCellHit> destroyedBeds;
    if (radius <= 0.0f || m_width <= 0 || m_depth <= 0 || m_height <= 0) return destroyed;
    const int r = int(std::ceil(radius));
    const float r2 = radius * radius;
    int minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    bool any = false;
    for (int dz = -r; dz <= r; ++dz)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                const float fdx = float(dx), fdy = float(dy), fdz = float(dz);
                if (fdx * fdx + fdy * fdy + fdz * fdz > r2) continue; // 球外跳过
                const int bx = cx + dx, by = cy + dy, bz = cz + dz;
                if (bx < 0 || bz < 0 || bx >= m_width || bz >= m_depth || by < 0 || by >= m_height) continue;
                const quint8 b = m_chunks.blockAt(bx, by, bz);
                if (b == BlockRegistry::Air || b == BlockRegistry::Bedrock || b == BlockRegistry::Water
                    || b == BlockRegistry::Obsidian)
                    continue; // 空气 / 基岩 / 水 / 黑曜石不破坏（机制等价 MC 爆炸：不毁水体、不破基岩；
                              //   t472 黑曜石爆炸抗性 6000 → 免疫 Stalker/TNT 爆炸，spec「blast-resistant」）
                if (BlockRegistry::isBed(b))
                    destroyedBeds.push_back({bx, by, bz, m_chunks.stateAt(bx, by, bz)}); // t1033：毁前 capture
                m_chunks.setBlock(bx, by, bz, BlockRegistry::Air); // 直写 + 标脏（含跨 chunk 边界邻接脏），不 emit
                destroyed.push_back({bx, by, bz, b});
                if (!any) { minX = maxX = bx; minY = maxY = by; minZ = maxZ = bz; any = true; }
                else {
                    if (bx < minX) minX = bx;
                    if (bx > maxX) maxX = bx;
                    if (by < minY) minY = by;
                    if (by > maxY) maxY = by;
                    if (bz < minZ) minZ = bz;
                    if (bz > maxZ) maxZ = bz;
                }
            }
    if (destroyed.empty()) return destroyed;
    // 末尾统一：1 次 refloodBox 重算光场（球外接盒扩 1 格余量，doSky=true 两通道都算 —— 破坏的多为
    //   solid 遮光块，天光列随之变化）+ 1 次 emit worldChanged + 1 次 clearAllDirty（机制同 decayLeavesAround）。
    const int bx0 = std::max(0, minX - 1), by0 = std::max(0, minY - 1), bz0 = std::max(0, minZ - 1);
    const int bx1 = std::min(m_width - 1, maxX + 1), by1 = std::min(m_height - 1, maxY + 1), bz1 = std::min(m_depth - 1, maxZ + 1);
    refloodBox(bx0, by0, bz0, bx1, by1, bz1, /*doSky=*/true);
    // t380：爆炸破坏球外接盒内的实体块 → 邻接水 / 岩浆可能失支撑流动。球内 Water/Lava 已跳过不破坏，
    //   但球边缘外的流体邻接关系变了（如炸开含水柱旁的石头 → 水流入新坑）。盒内必有流体邻接则标脏，
    //   驱动下次流体 tick 重扫；盒内无流体 → pokeFluidDirty 不设（无副作用）。爆炸是稀有事件 → 逐破坏块
    //   poke 略重，直接置两标志 true 保守（稳态扫描后即清，一次性开销可忽略）。
    m_waterDirty = true;
    m_lavaDirty = true;
    // t488：破坏区扩进活动盒（±1 由 fluidActExpand 单格并）→ 下次流体 tick 只扫破坏区而非全量快照。
    //   逐破坏块 O(1)（半径 3 球 ≤ ~343 项），爆炸稀有可忽略。
    for (const DestroyedVoxel &d : destroyed) fluidActExpand(d.x, d.y, d.z);
    // t495 code review 补：爆炸批量直写不经 note*Write → m_iceCells / m_waterCells / m_lavaCells /
    //   m_growthCells 残留已被清成 Air 的 stale key（tick 侧有防御 guard 不崩，但每窗白扫）。
    //   爆炸稀有 → 逐破坏块 O(1) note 维护（同 dropCactusColumn / clearBlockSilent 口径），索引保持精确。
    for (const DestroyedVoxel &d : destroyed) {
        noteIceWrite(d.x, d.y, d.z, d.oldId, BlockRegistry::Air);
        noteFireWrite(d.x, d.y, d.z, d.oldId, BlockRegistry::Air); // t724：同族补齐——爆炸直写绕过 setBlock 编辑钩子
        noteFluidWrite(d.x, d.y, d.z, d.oldId, BlockRegistry::Air);
        noteGrowthWrite(d.x, d.y, d.z, d.oldId, BlockRegistry::Air);
        notePowerWrite(d.x, d.y, d.z, d.oldId, BlockRegistry::Air); // t683：同族补齐——爆炸直写绕过 setBlock
            //   编辑钩子 → 炸掉红石族（粉 / 源 / 接收器）或粉旁石块后 m_powerDirty 不含该格 → 邻粉 / 灯 /
            //   轨电力不重算（幽灵电：灯恒亮 / 轨恒加速）直到玩家再编辑。逐破坏块 O(1)（同上三 note 口径）。
            //   t942：本口即爆炸链尾的红石重扫挂点——notePowerWrite 内对源 / 粉 / 轨编辑格追加
            //   dirtyGoldenRailChainFrom 链走查，被毁能量源喂着的整条动力轨链同 tick 入脏集，下一 tick
            //   一次 pass 全灭（不再翻转波前逐 tick 收缩）。TNT（detonateTntSphere）与 Stalker
            //   （detonateStalker）两爆炸入口共用本函数 = 一处覆盖。
    }
    // review24 #3 口径合一：爆炸批量直写绕过 setBlock 编辑钩子族 → 旧版逐破坏格只补 rail / snow /
    //   trapdoor 三钩子（+ 火把族由 EntityManager 单独兜底），仙人掌 / 枯灌木 / 花 / 压力板 / 甘蔗失撑
    //   仍悬空残留。改调 recheckAttachmentsAfterClear 单一入口（含上述三钩子 + 缺失各族 + 6 邻火把 /
    //   红石火把扫——未来新增附着物只扩 recheck 一处）；重力复检仍显式补调（recheck 刻意不含
    //   checkGravityBlockOnEdit，防柱内重入，见其头注释）。recheck 内各族自带早退（非对应族 → no-op
    //   零写入零 emit），真掉落时自 emit worldChanged 与下方收口 emit 叠加无害（重建幂等，球 ≤343 格，
    //   爆炸稀有可忽略）。
    for (const DestroyedVoxel &d : destroyed) {
        recheckAttachmentsAfterClear(d.x, d.y, d.z, d.oldId);
        checkGravityBlockOnEdit(d.x, d.y, d.z, d.oldId, BlockRegistry::Air); // t799：爆炸破坏支撑 → 弹坑上缘沙/沙砾柱坍落（旧 QML 链不发信号 → 悬空残留）
        breakNetherPortalsAround(d.x, d.y, d.z); // review #27：爆炸拆掉门面 6 邻非抗爆格（黑曜石框免疫）→ 邻接门面熄灭；d.oldId 必非 Air（破坏列表构造）
    }
    emit worldChanged();
    m_chunks.clearAllDirty();
    // t1033：被毁床逐格语义事件（携毁前 state；消费端 PlayerController 判锚床 → clearBedSpawn +
    //   bedSpawnLost，见 world.h signals 注释）。世界写入已收口后才发（消费者只见终态，零重入面）。
    for (const BedCellHit &bd : destroyedBeds)
        emit blockDestroyedBed(bd.x, bd.y, bd.z, int(bd.state));
    qInfo("vo.edit: explosion destroyed = %d (center %d,%d,%d r=%g)",
          int(destroyed.size()), cx, cy, cz, double(radius)); // 可观测：一次爆炸的破坏块数
    return destroyed;
}

// t445 仙人掌整柱坍落为掉落物（见 world.h 头注释）。机制等价 MC 1.0 仙人掌失撑 / 邻接方块即整柱破坏掉落。
//   自 (x,y,z) 起向上逐格静默清 Cactus + 发 blockBroken（粒子 / 音）+ blockDroppedAsItem（掉落物）+ 重 flood 光，
//   末尾 1 次 worldChanged + clearAllDirty（N 写 1 emit，同 destroySphereSilent 批量收口）。静默写不经 World::setBlock
//   → 不递归触发 ②/④ 检查（无重入），也不发额外 blockBroken 链（本方法自发）。空柱（首格非 Cactus）→ no-op。
//   t571 标注【自然失撑掉落：恒发（含创造）】—— World 层无 drop 标志概念，失撑坍落是结构后果，模式无关。
void World::dropCactusColumn(int x, int y, int z)
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    bool any = false;
    int cy = y;
    while (cy >= 0 && cy < m_height && m_chunks.blockAt(x, cy, z) == BlockRegistry::Cactus) {
        m_chunks.setBlock(x, cy, z, BlockRegistry::Air); // 静默直写 + 标脏（含边界邻接）；不经 World::setBlock（无重入）
        noteGrowthWrite(x, cy, z, BlockRegistry::Cactus, BlockRegistry::Air); // t425：仙人掌本不在生长索引，保持一致 no-op
        emit blockBroken(x, cy, z, int(BlockRegistry::Cactus));                // 破块粒子 / 音（机制等价 MC 整柱坍落反馈）
        emit blockDroppedAsItem(x, cy, z, int(BlockRegistry::Cactus));        // 呈掉落物实体（Main.qml spawnItem）
        recomputeLightAround(x, cy, z, BlockRegistry::Cactus, BlockRegistry::Air); // 遮光柱消失 → 重 flood 邻域光场
        any = true;
        ++cy;
    }
    if (any) {
        emit worldChanged();        // 驱动 mesh 重建（细柱段消失）
        m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
    }
}

// t445 setBlock 编辑后仙人掌完整性复检（见 world.h 头注释；② 失撑 + ④ 邻接方块）。
void World::checkCactusOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    // ② 失撑：本格破为 Air 且被破块非 Cactus → 正上方 Cactus 失撑 → 整柱掉落。（被破块为 Cactus 时跳过 ——
    //   玩家直破仙人掌的整柱坍落由 PlayerController 级联 spawnItem 负责，避免双重掉落。）
    if (id == BlockRegistry::Air && oldId != BlockRegistry::Cactus) {
        if (y + 1 < m_height && m_chunks.blockAt(x, y + 1, z) == BlockRegistry::Cactus)
            dropCactusColumn(x, y + 1, z);
    }
    // ④ 邻接方块：本格新放**整立方方块**（BlockRegistry::isFullCube —— shape == ShapeFull，t213 单一权威
    //   谓词）→ 水平 4 邻任一为 Cactus 即「邻接方块」→ 该 Cactus 整柱掉落（覆盖玩家放沙旁 / 落沙落旁等非玩家
    //   放置路径；机制等价 MC 仙人掌旁边贴完整方块才碎）。
    //   **t984 口径翻案**（用户 9-01 原话「我的口径是能放下来，而不是仙人掌会掉落，你之前一直都做错了」）：
    //   旧门槛 `id != Air`（非空即触发）把铁轨 / 火把 / 压力板等非完整方块也当「邻接方块」→ 贴仙人掌放置
    //   成功后反手把仙人掌整柱炸掉（t911 旧钉「铁轨非仙人掌合法邻面」据此作废）。t984 先收紧为
    //   `BlockRegistry::isSolid(id)`；**review0903 #1 再修为 `isFullCube(id)`**：isSolid 是 mesher 邻居面剔除
    //   语义的代理，对 Glass / Ice / Spawner 等 solid=false 但 ShapeFull 的整立方漏放（MC 语义与 t984 修前
    //   行为均应碎仙人掌）——整立方才是「完整方块」的权威口径。与 t503 worldgen 仙人掌柱 4 邻守卫
    //   （placeDesertFlora）**同一谓词同源**（worldgen / 放置 / 挖除三路径口径一致）：整立方（沙 / 石 / 木 /
    //   玻璃 / 冰 / 刷怪笼等）邻接才碎；轨族 / 火把 / 压力板 / 雪层 / 半砖等非整立方（含仙人掌自身
    //   ShapeCactus）邻接不触发 → 放置成功、仙人掌不动。
    //   自动下矿车系统不受影响（仙人掌撞毁矿车 t866② 走 Entities 层接触判定，与方块邻接口径解耦）。
    //   **t911 整柱口径修**（保留）：命中可能在柱**中段**——先下探柱基再整柱坍落。
    //   **t945 玩家路径核验**（保留）：本分支只在**放置成功后**反应（无预检拒绝「邻仙人掌」放置位）。
    //   被拒放置（悬空轨位）不到本分支。整立方邻接仍坍落的既有语义由 P-t984 石头 / 玻璃对照腿 +
    //   P-t945 (g) 钉。
    if (id != BlockRegistry::Air && BlockRegistry::isFullCube(id)) {
        constexpr int kNb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (const auto &d : kNb) {
            const int nx = x + d[0], nz = z + d[1];
            if (nx >= 0 && nz >= 0 && nx < m_width && nz < m_depth
                && y >= 0 && y < m_height
                && m_chunks.blockAt(nx, y, nz) == BlockRegistry::Cactus) {
                int baseY = y; // 下探柱基（柱中段命中 → 整柱从基座坍落）
                while (baseY > 0 && m_chunks.blockAt(nx, baseY - 1, nz) == BlockRegistry::Cactus)
                    --baseY;
                dropCactusColumn(nx, baseY, nz);
            }
        }
    }
}

// t504 setBlock 编辑后枯死灌木失撑复检（见 world.h 头注释）。机制等价 MC 1.0 枯灌木失去下方支撑即坍落（同甘蔗 /
//   仙人掌支撑校验族）；但坍落产物为 **木棒**（材料段 0x200）而非枯灌木自身（机制等价 MC dead bush 掉 0-2 木棒，
//   不掉自身 —— 与蘑菇掉蘑菇不同（t788 起破花掉染料物品而非花方块）：枯灌木 dropId=0 故即便掉自身也无意义，故失撑走木棒）。
//   t571 标注【自然失撑掉落：恒发（含创造）】。
//   DeadBush 恒单格（无柱状生长），故仅清正上方 1 格（与 Cactus dropCactusColumn 逐柱不同）。
//   玩家直破枯灌木（oldId==DeadBush → id==Air）走 finishMiningAt，dropId=0 → 无产物；仅失撑（破下方支撑方块，
//   oldId 非 DeadBush → 正上方 DeadBush 掉木棒）才发掉落物，避免双重掉落。静默直写不经 World::setBlock → 不递归触发本检查。
//   产物 id 用字面量 0x200（= RecipeRegistry::StickId，材料段基址 0x200）—— Core/World 层不依赖 Game（PLAN §2 分层），
//   故不能 include recipe.h；与 blockregistry.cpp 矿石 dropId 用字面量 0x201/0x202 同模式（单一权威契约对齐）。
void World::checkDeadBushOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    // 仅本格被破为 Air 且被破块非 DeadBush 时，查正上方是否 DeadBush 失撑。（被破块本身是 DeadBush 时跳过 ——
    //   玩家直破枯灌木的掉落由 dropId=0 决定无产物，避免双重掉落。）
    if (id != BlockRegistry::Air || oldId == BlockRegistry::DeadBush) return;
    const int by = y + 1;
    if (by < 0 || by >= m_height) return;
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    if (m_chunks.blockAt(x, by, z) != BlockRegistry::DeadBush) return;
    // 枯灌木失撑 → 静默清 Air（直写 + 标脏，不经 World::setBlock → 不重入本检查）+ 发破块反馈 + 掉落木棒 + 重 flood 光。
    constexpr int kStickItemId = 0x200; // 木棒（= RecipeRegistry::StickId，材料段基址 0x200；Core 不依赖 Game 故字面量）
    m_chunks.setBlock(x, by, z, BlockRegistry::Air);
    noteGrowthWrite(x, by, z, BlockRegistry::DeadBush, BlockRegistry::Air); // 枯灌木非生长方块 → no-op，保持一致
    emit blockBroken(x, by, z, int(BlockRegistry::DeadBush));        // 破块粒子 / 音（坍落的是枯灌木方块，id 用 DeadBush）
    emit blockDroppedAsItem(x, by, z, kStickItemId);                // 掉落物 = 木棒（材料段 0x200；机制等价 MC dead bush 掉木棒）
    recomputeLightAround(x, by, z, BlockRegistry::DeadBush, BlockRegistry::Air); // solid=false 故遮光变化小，仍重 flood 保正确
    emit worldChanged();        // 驱动 mesh 重建（cross 段消失）
    m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
}

// t507 setBlock 编辑后花 / 蘑菇失撑复检（见 world.h 头注释）。机制等价 MC 1.0 花 / 蘑菇失去下方支撑即掉自身
//   （同甘蔗 / 仙人掌 / 枯灌木支撑校验族）。花 / 蘑菇恒单格（无柱状生长，与 Cactus dropCactusColumn 不同），故
//   仅清正上方 1 格。破下方支撑（id==Air 且 oldId 非族内植物）→ 正上方是族内植物（花 / 蘑菇 / 草丛）
//   → 静默清 Air + emit 破块反馈 + 掉落物（dropId=自身）+ 重 flood 光。玩家直破族内植物（oldId∈族
//   → id==Air）走 finishMiningAt 通用 drop 路径（dropId=自身方块），避免双重掉落。族成员判定经
//   BlockRegistry::isGroundPlant 单一权威谓词（t847 收口 R19.13 终审 C-M1：t847 只把草丛收进放置预检、
//   失撑族没跟 → 挖掉下方泥土后草丛悬空永存；放置 / 失撑两面共用谓词锁族成员集，加族必两面齐动）。
//   同族既有口径（登记不扩）：本钩子对花 / 蘑菇只在 id==Air 的编辑生效——支撑被置换成非合法着地面（锄地
//   变耕地）不掉，花 / 蘑菇自 t507 起同口径（保持同族统一，置换面留后续整族一批）。
//   t903 失撑同口径收紧（**只草丛**，花 / 蘑菇置换面口径保留）：草丛唯一合法支撑 = 草方块
//   （plantGroundBlock 单一权威收紧后）→ 本格被**置换为非草面**（羊吃草 Grass→Dirt 经 setWaterSilent /
//   锄地变耕地 / 任意替换）且正上方是草丛 → 草丛失撑掉落（dropId=自身种子）。为覆盖羊吃草路径，
//   setWaterSilent 亦挂本钩子（同审查修 L6 给 checkRailOnEdit 补挂的先例——非族格单次 blockAt 早退，
//   流体批量热路径可承受）。
//   t571 标注【自然失撑掉落：恒发（含创造）】。
void World::checkFlowerMushroomOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    // 被破 / 被置换块本身是族内植物时跳过 —— 玩家直破花 / 蘑菇 / 草丛的掉落由通用 finishMiningAt drop
    // 路径负责，避免双重掉落。
    if (BlockRegistry::isGroundPlant(oldId)) return;
    const int by = y + 1;
    if (by < 0 || by >= m_height) return;
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    const quint8 above = m_chunks.blockAt(x, by, z);
    if (!BlockRegistry::isGroundPlant(above)) return;
    // 失撑判据：①本格被破为 Air（t507 全族口径）；②t903 草丛置换面 —— 正上方是草丛且本格被置换为非合法
    //   着地面（plantGroundBlock 单一权威；Air 亦非合法面，①是②对草丛的子集——①保留为花 / 蘑菇的 Air 路径，
    //   ②只对草丛收口）。
    const bool supportBroken = id == BlockRegistry::Air;
    const bool grassOnNonGrass = above == BlockRegistry::TallGrass
                                 && !BlockRegistry::plantGroundBlock(BlockRegistry::TallGrass, id);
    if (!supportBroken && !grassOnNonGrass) return;
    // 花 / 蘑菇失撑 → 静默清 Air（直写 + 标脏，不经 World::setBlock → 不重入本检查）+ 发破块反馈 + 掉落物 + 重 flood 光。
    m_chunks.setBlock(x, by, z, BlockRegistry::Air);
    noteGrowthWrite(x, by, z, above, BlockRegistry::Air); // 花 / 蘑菇非生长方块 → no-op，保持一致
    emit blockBroken(x, by, z, int(above));                 // 破块粒子 / 音（机制等价 MC 失撑坍落反馈）
    // t788 起掉落物改走 dropId（与玩家直破同源）：花掉**对应色染料**（dropId=材料段染料字面量）、蘑菇掉自身
    //   （蘑菇 dropId 不变=自身）。原实现直传方块 id → 花失撑掉花方块、玩家直破掉染料，两路不一致且留「破支撑
    //   绕过染料链取花方块」的生存漏洞；统一走 dropId 后蘑菇行为逐位不变（回归无感），花对齐染色链正道。
    emit blockDroppedAsItem(x, by, z, BlockRegistry::dropId(above)); // 呈掉落物实体（Main.qml spawnItem）
    recomputeLightAround(x, by, z, above, BlockRegistry::Air); // solid=false 故遮光变化小，仍重 flood 保正确
    emit worldChanged();        // 驱动 mesh 重建（cross 段消失）
    m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
}

// t494 压力板失撑掉落复检（见 world.h 头注释）。机制等价 MC 1.0 压力板失去下方支撑即掉自身。
//   压力板（Wood/Cobble）是贴地薄板（ShapePlate），下方须有完整支撑方块。本格被破为 Air（破下方支撑）→ 正上方
//   是压力板 → 失撑 → 静默清 Air + emit 破块反馈 + 掉落物（dropId=自身 → 掉木板 / 圆石压力板）+ 重 flood 光。
//   玩家直破压力板（oldId==plate → id==Air）走 finishMiningAt 通用 drop 路径（dropId=自身），避免双重掉落。
//   触发场景（用户报）：① 压力板放 TNT 上，TNT 被引燃（clearBlockSilent 把下方 TNT 清成 Air → 板上压力板失撑）；
//   ② 挖掘压力板下方的方块（setBlock 破下方支撑 → 板上压力板失撑）。木 / 圆石压力板都掉。
//   t571 标注【自然失撑掉落：恒发（含创造）】。
void World::checkPressurePlateOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    // 仅本格被破为 Air 且被破块非压力板时，查正上方是否压力板失撑。（被破块本身是压力板时跳过 ——
    //   玩家直破压力板的掉落由 finishMiningAt 通用 drop 路径负责（dropId=自身），避免双重掉落。）
    if (id != BlockRegistry::Air || BlockRegistry::isPressurePlate(oldId)) return;
    const int by = y + 1;
    if (by < 0 || by >= m_height) return;
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    const quint8 above = m_chunks.blockAt(x, by, z);
    if (!BlockRegistry::isPressurePlate(above)) return;
    // 压力板失撑 → 静默清 Air（直写 + 标脏，不经 World::setBlock → 不重入本检查）+ 发破块反馈 + 掉落物 + 重 flood 光。
    m_chunks.setBlock(x, by, z, BlockRegistry::Air);
    noteGrowthWrite(x, by, z, above, BlockRegistry::Air); // 压力板非生长方块 → no-op，保持一致
    emit blockBroken(x, by, z, int(above));                 // 破块粒子 / 音（机制等价 MC 失撑坍落反馈）
    emit blockDroppedAsItem(x, by, z, int(above));         // 呈掉落物实体（dropId=自身 → 掉压力板）
    recomputeLightAround(x, by, z, above, BlockRegistry::Air); // solid=false 故遮光变化小，仍重 flood 保正确
    emit worldChanged();        // 驱动 mesh 重建（薄板消失）
    m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
}

// t851 活板门 / 门失撑掉落复检（支撑校验族，checkPressurePlateOnEdit 同款模式；见 .h 头注释）。
//   （x,y,z,oldId,id）= 本格刚发生的编辑。本格编辑后不再是本族方块（破为 Air / 换成火把等非依附面
//   内容——state-only 开合写 id==id 天然早退）→ 两路扫：
//   ② 正上方：活板门（须依附任一实体面）/ 门下扇（须站齐平地面 isTopFlushSupport——同 placeBlock t741
//      口径，天然拒门叠门：Door 非完整立方非上半砖）失去本格支撑 → 级联掉落。
//   ③ 四水平邻的活板门：其**侧撑**可能正是本格（贴墙浮空板拆墙场景）→ 依附面全失则级联掉落。
//   被破块本身是本族时跳过（玩家直破的掉落由 finishMiningAt 通用路径 + 门配对联动负责，防双重掉落；
//   同压力板 / 甘蔗 oldId 守卫模式）。依附判定走 BlockRegistry::trapdoorSupportBlock 单一权威
//   （isCollidable 且排除活板门/门自身——与放置预检同谓词零漂移，「板套板悬浮叠」两侧一致拒）。
void World::checkTrapdoorDoorSupportOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    Q_UNUSED(oldId); // 守卫按「编辑后本格是否仍本族」（id 谓词）判——oldId 保留供 checkXxxOnEdit 族签名一致
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    // 单格依附面快照谓词：活板门在 (bx,by,bz) 是否仍依附任一实体面（下方 + 四侧；trapdoorSupportBlock
    //   单一权威——排除活板门/门自身）。
    const auto hasAttach = [&](int bx, int by, int bz) -> bool {
        if (by - 1 >= 0 && BlockRegistry::trapdoorSupportBlock(m_chunks.blockAt(bx, by - 1, bz),
                                                               m_chunks.stateAt(bx, by - 1, bz)))
            return true;
        static constexpr int kNb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (const auto &d : kNb) {
            const int nx = bx + d[0], nz = bz + d[1];
            if (nx < 0 || nz < 0 || nx >= m_width || nz >= m_depth) continue;
            if (BlockRegistry::trapdoorSupportBlock(m_chunks.blockAt(nx, by, nz),
                                                    m_chunks.stateAt(nx, by, nz)))
                return true;
        }
        return false;
    };
    const int by = y + 1;
    if (by >= 0 && by < m_height) {
        const quint8 above = m_chunks.blockAt(x, by, z);
        if (BlockRegistry::isTrapdoor(above)) {
            if (!hasAttach(x, by, z))
                dropUnsupportedDoorsAbove(x, by, z); // 板失撑 → 自身 + 其上门级联掉落
        } else if (BlockRegistry::isDoor(above) && (m_chunks.stateAt(x, by, z) & 8) == 0) {
            // 门下扇：须齐平支撑（isTopFlushSupport 单一权威——完整立方 / 上半砖顶面）。
            if (!BlockRegistry::isTopFlushSupport(id, m_chunks.stateAt(x, y, z)))
                dropUnsupportedDoorsAbove(x, by, z);
        }
    }
    // ③ 贴墙板拆墙：**本层**四水平邻的活板门若依附面全失（其侧撑可能正是本格）→ 各自级联掉落。
    static constexpr int kNb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (const auto &d : kNb) {
        const int nx = x + d[0], nz = z + d[1];
        if (nx < 0 || nz < 0 || nx >= m_width || nz >= m_depth) continue;
        if (y < 0 || y >= m_height) continue;
        if (BlockRegistry::isTrapdoor(m_chunks.blockAt(nx, y, nz)) && !hasAttach(nx, y, nz))
            dropUnsupportedDoorsAbove(nx, y, nz);
    }
}

// t851 失撑级联掉落（见 .h 头注释）：从 (x,y,z) 的本族方块起向上逐格清「连续活板门柱 / 连续双格门」，
//   各格发 blockBroken（粒子/音）+ blockDroppedAsItem（dropId=自身物品形态）+ 光照重 flood，末尾一次
//   worldChanged + clearAllDirty（N 写 1 emit 批量收口，同 dropSugarcaneColumn）。静默直写不经
//   World::setBlock → 无重入。机制等价 MC 1.0 附着方块支撑移除即脱落为物品。【自然失撑掉落：恒发
//   （含创造）】—— 破坏支撑是因、附着物脱落是果（t571 族标注口径）。
void World::dropUnsupportedDoorsAbove(int x, int y, int z)
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    if (y < 0 || y >= m_height) return;
    bool any = false;
    int cy = y;
    while (cy < m_height) {
        const quint8 b = m_chunks.blockAt(x, cy, z);
        if (b == BlockRegistry::Air) break; // 结构到顶（连续柱扫到空气即止）
        if (BlockRegistry::isDoor(b)) {
            // 门：先清配对半扇（本格是下扇→上扇 cy+1；本格是上扇→下扇 cy-1），两格各发一次信号
            //   （每格一个掉落物实体——MC 1.0 破坏双格门亦出 2 个掉落物），随后 cy 推进越过上扇继续
            //   向上扫下一扇门的下扇（门叠门通天链逐扇脱落）。
            const bool lowerIsThis = (m_chunks.stateAt(x, cy, z) & 8) == 0; // bit3=1 上扇 / 0 下扇
            const int uy = lowerIsThis ? cy + 1 : cy - 1;
            if (uy >= 0 && uy < m_height && BlockRegistry::isDoor(m_chunks.blockAt(x, uy, z))) {
                m_chunks.setBlock(x, uy, z, BlockRegistry::Air); // 静默直写 + 标脏（不经 World::setBlock 无重入）
                noteGrowthWrite(x, uy, z, b, BlockRegistry::Air); // 门非生长方块 → no-op，保持同族写入一致
                noteFluidWrite(x, uy, z, b, BlockRegistry::Air);
                noteIceWrite(x, uy, z, b, BlockRegistry::Air);
                noteFireWrite(x, uy, z, b, BlockRegistry::Air);
                emit blockBroken(x, uy, z, int(b));               // 破块粒子 / 音
                emit blockDroppedAsItem(x, uy, z, BlockRegistry::dropId(b)); // 呈掉落物（Main.qml spawnItem）
                recomputeLightAround(x, uy, z, b, BlockRegistry::Air);
                any = true;
            }
            m_chunks.setBlock(x, cy, z, BlockRegistry::Air);
            noteGrowthWrite(x, cy, z, b, BlockRegistry::Air);
            noteFluidWrite(x, cy, z, b, BlockRegistry::Air);
            noteIceWrite(x, cy, z, b, BlockRegistry::Air);
            noteFireWrite(x, cy, z, b, BlockRegistry::Air);
            emit blockBroken(x, cy, z, int(b));
            emit blockDroppedAsItem(x, cy, z, BlockRegistry::dropId(b));
            recomputeLightAround(x, cy, z, b, BlockRegistry::Air);
            any = true;
            cy = lowerIsThis ? cy + 2 : cy + 1; // 下扇起点 → 跳过已清的上扇；上扇起点 → 上扇已清、下一步即上一扇下扇
            continue;
        }
        if (!BlockRegistry::isTrapdoor(b)) break; // 非本族（如沙柱）→ 级联到此为止
        m_chunks.setBlock(x, cy, z, BlockRegistry::Air); // 活板门：清自身后向上续扫（活板门套活板门悬浮叠链）
        noteGrowthWrite(x, cy, z, b, BlockRegistry::Air);
        noteFluidWrite(x, cy, z, b, BlockRegistry::Air);
        noteIceWrite(x, cy, z, b, BlockRegistry::Air);
        noteFireWrite(x, cy, z, b, BlockRegistry::Air);
        emit blockBroken(x, cy, z, int(b));
        emit blockDroppedAsItem(x, cy, z, BlockRegistry::dropId(b));
        recomputeLightAround(x, cy, z, b, BlockRegistry::Air);
        any = true;
        ++cy;
    }
    if (!any) return;
    emit worldChanged();      // 驱动 mesh 重建（N 写 1 emit 批量收口）
    m_chunks.clearAllDirty(); // 两段重建完统一清脏（同 setBlock 末尾）
}

// t524 甘蔗整柱坍落为掉落物（见 world.h 头注释）。机制等价 MC 1.0 甘蔗失去下方支撑即整柱破坏掉落。
//   自 (x,y,z) 起向上逐格静默清 Sugarcane + 发 blockBroken（粒子 / 音）+ blockDroppedAsItem（掉落物）+ 重 flood 光，
//   末尾 1 次 worldChanged + clearAllDirty（N 写 1 emit，同 dropCactusColumn 批量收口）。静默写不经 World::setBlock
//   → 不递归触发 checkSugarcaneOnEdit（无重入），也不发额外 blockBroken 链（本方法自发）。空柱（首格非 Sugarcane）→ no-op。
//   t571 标注【自然失撑掉落：恒发（含创造）】—— 同 dropCactusColumn；「打掉甘蔗下面的沙子 → 整柱坍落」即走此路径。
void World::dropSugarcaneColumn(int x, int y, int z)
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    bool any = false;
    int cy = y;
    while (cy >= 0 && cy < m_height && m_chunks.blockAt(x, cy, z) == BlockRegistry::Sugarcane) {
        m_chunks.setBlock(x, cy, z, BlockRegistry::Air); // 静默直写 + 标脏（含边界邻接）；不经 World::setBlock（无重入）
        noteGrowthWrite(x, cy, z, BlockRegistry::Sugarcane, BlockRegistry::Air); // 维护生长方格索引（甘蔗属生长块，须清键）
        emit blockBroken(x, cy, z, int(BlockRegistry::Sugarcane));                // 破块粒子 / 音（机制等价 MC 整柱坍落反馈）
        emit blockDroppedAsItem(x, cy, z, int(BlockRegistry::Sugarcane));        // 呈掉落物实体（Main.qml spawnItem；dropId=自身）
        recomputeLightAround(x, cy, z, BlockRegistry::Sugarcane, BlockRegistry::Air); // solid=false 故遮光变化小，仍重 flood 保正确
        any = true;
        ++cy;
    }
    if (any) {
        emit worldChanged();        // 驱动 mesh 重建（cross 细茎段消失）
        m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
    }
}

// t524 setBlock 编辑后甘蔗失撑复检（见 world.h 头注释）。机制等价 MC 1.0 甘蔗失去下方支撑即整柱坍落（同仙人掌 /
//   枯灌木 / 花 / 蘑菇 / 压力板支撑校验族）。（x,y,z,oldId,id）= 本格刚发生的编辑。
//   ② 失撑：本格破为 Air 且被破块非 Sugarcane → 正上方甘蔗柱失去下方支撑方块（沙 / 草 / 泥土 / 甘蔗）→ 整柱掉落。
//   （被破块为 Sugarcane 时跳过 —— 玩家直破甘蔗的整柱坍落由 PlayerController 级联 spawnItem 负责（t418），避免双重掉落；
//   同仙人掌 checkCactusOnEdit 的 oldId 守卫模式。）静默 dropSugarcaneColumn 不经 World::setBlock → 不重入本检查。
void World::checkSugarcaneOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    if (id != BlockRegistry::Air || oldId == BlockRegistry::Sugarcane) return;
    const int by = y + 1;
    if (by < 0 || by >= m_height) return;
    if (m_chunks.blockAt(x, by, z) != BlockRegistry::Sugarcane) return;
    dropSugarcaneColumn(x, by, z);
}

// t527 积雪层支撑掉落复检（见 world.h 头注释；机制等价甘蔗 / 仙人掌支撑校验族，区别 MC 雪层无重力 —— 本工程
//   用户明确要「雪层失撑后掉落保留层数」）。（x,y,z,oldId,id）= 本格刚发生的编辑。
//   失撑：本格被破为 Air → 若正上方是 SnowLayer，则该雪层柱失撑 → 整柱（自正上方起所有连续 SnowLayer 格）
//   坍落为一个**携带层数 metadata**的下落方块实体（snowLayerFell 信号）。**保留层数**：整柱各格 (state+1) 层
//   累加、cap 8（state 7=8 层=满格≈雪块）。**无 oldId 守卫**：玩家直破中间雪层（oldId==SnowLayer）→ finishMiningAt
//   仅对被破格本身掉雪球；其正上方雪层通过本复检独立坍落（避免中间被破后上方永久浮空）。
void World::checkSnowLayerOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    Q_UNUSED(oldId); // 无 oldId 守卫 —— 破任一格（含直破雪层）其正上方雪层都失撑（见上方注释）；参数保留供
    //   checkXxxOnEdit 族签名一致（同 checkCactusOnEdit / checkDeadBushOnEdit）。
    // 仅本格被破为 Air 时查正上方雪层柱失撑（放块 / state 变不触发）。无 oldId 守卫 —— 破任一格（含直破雪层）
    //   其正上方雪层都失撑（finishMiningAt 对被破格本身掉雪球，与正上方柱坍落正交不重复）。
    if (id != BlockRegistry::Air) return;
    const int by = y + 1;
    if (by < 0 || by >= m_height) return;
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    if (m_chunks.blockAt(x, by, z) != BlockRegistry::SnowLayer) return;
    // 整柱坍落：自 by 起向上逐格清连续 SnowLayer，累加 (state+1) 层（cap 8；state 7=8 层=满格≈雪块）。
    //   静默直写（m_chunks.setBlock + 标脏，不经 World::setBlock → 不重入本检查）+ emit blockBroken（破块粒子 / 音）+
    //   recomputeLightAround（遮光柱消失重 flood）。末尾 1 次 emit snowLayerFell（柱底 + 总层数）+ worldChanged +
    //   clearAllDirty（N 写 1 emit，同 dropCactusColumn 批量收口）。
    int totalLayers = 0;
    int cy = by;
    bool any = false;
    while (cy >= 0 && cy < m_height && m_chunks.blockAt(x, cy, z) == BlockRegistry::SnowLayer) {
        const quint8 st = m_chunks.stateAt(x, cy, z);
        totalLayers += int(st) + 1; // state 0..7 = 1..8 层
        m_chunks.setBlock(x, cy, z, BlockRegistry::Air); // 静默直写 + 标脏（含边界邻接）；不经 World::setBlock（无重入）
        noteGrowthWrite(x, cy, z, BlockRegistry::SnowLayer, BlockRegistry::Air); // 雪层非生长方块 → no-op，保持一致
        noteFluidWrite(x, cy, z, BlockRegistry::SnowLayer, BlockRegistry::Air);  // 雪层非流体 → no-op，保持一致
        noteIceWrite(x, cy, z, BlockRegistry::SnowLayer, BlockRegistry::Air);    // 雪层非冰 → no-op，保持一致
        noteFireWrite(x, cy, z, BlockRegistry::SnowLayer, BlockRegistry::Air);  // 雪层非火 → no-op，保持一致
        emit blockBroken(x, cy, z, int(BlockRegistry::SnowLayer));               // 破块粒子 / 音（机制等价 MC 失撑坍落反馈）
        recomputeLightAround(x, cy, z, BlockRegistry::SnowLayer, BlockRegistry::Air); // solid=false 故遮光变化小，仍重 flood 保正确
        any = true;
        ++cy;
    }
    if (!any) return;
    // cap 到 8 层（state 7 = 满格 ≈ 雪块）：整柱多层累加超 8 取 8（罕见多格雪柱坍落；用户「8 层掉 8 层≈雪块」语义）。
    if (totalLayers > int(BlockRegistry::SnowLayerStageMax) + 1)
        totalLayers = int(BlockRegistry::SnowLayerStageMax) + 1;
    // 柱底 by（最早失撑的雪层原位）+ 总层数 → 呈现层转 spawnFallingBlockState（state = totalLayers-1 metadata）。
    emit snowLayerFell(x, by, z, totalLayers);
    emit worldChanged();        // 驱动 mesh 重建（雪层薄板段消失）
    m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
}

// t799 重力方块（沙 / 沙砾；t794 扩铁砧三阶段）整柱坍落 helper（头注释见 world.h）：自 (x,y,z) 起向上逐格
//   清**连续重力方块**（BlockRegistry::isGravityBlock 单一权威；混合沙/沙砾/铁砧柱各自保留 id）。每格：静默
//   写 Air（m_chunks.setBlock
//   直写 + 标脏，不经 World::setBlock → 不递归触发 checkGravityBlockOnEdit / 不重复发 broken/placed 链）+
//   note*Write 索引维护（同雪柱坍落口径——重力方块非流体/冰/火/生长段，理论 no-op，保持入口一致防未来
//   把某重力方块归入索引段后漏维护）+ emit blockBroken（破块粒子 / 音，机制等价 MC 失撑坍落反馈）+
//   recomputeLightAround（实体沙柱消失重 flood）+ emit gravityBlockFell（每格一信号一实体，着地各自还原）+
//   recheckAttachmentsAfterClear（审查修 #4：清格后邻域附着物复检——柱顶 / 柱侧火把 / 铁轨 / 甘蔗等随支撑消失
//   掉落 / 坍落，不再悬空残留）。
//   末尾 1 次 worldChanged + clearAllDirty（N 写 1 emit，同 dropCactusColumn 批量收口）。空首格 → no-op。
void World::dropGravityColumn(int x, int y, int z)
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    if (y < 0 || y >= m_height) return;
    int cy = y;
    int firstY = y; // t933：本柱首坍落格 y（列末单次重光照的联合盒底）
    bool any = false;
    while (cy < m_height && BlockRegistry::isGravityBlock(m_chunks.blockAt(x, cy, z))) {
        const quint8 b = m_chunks.blockAt(x, cy, z);
        m_chunks.setBlock(x, cy, z, BlockRegistry::Air); // 静默直写 + 标脏（含边界邻接）；不经 World::setBlock（无重入）
        noteGrowthWrite(x, cy, z, b, BlockRegistry::Air); // 沙非生长方块 → no-op（同雪柱口径保持一致）
        noteFluidWrite(x, cy, z, b, BlockRegistry::Air);  // 沙非流体 → no-op（同上）
        noteIceWrite(x, cy, z, b, BlockRegistry::Air);    // 沙非冰 → no-op（同上）
        noteFireWrite(x, cy, z, b, BlockRegistry::Air);   // 沙非火 → no-op（同上）
        emit blockBroken(x, cy, z, int(b));               // 破块粒子 / 音（机制等价 MC 失撑坍落反馈）
        // t933 perf：逐格 recomputeLightAround 退役 —— 单次 = 「±15 × 到世界顶」两通道重 flood（~数十 k 体素）
        //   + 一次 qInfo 落盘；沙坑爆炸级联数十柱 × 每柱 ~10 格 = 数百次全盒重 flood 是 8FPS 的 C++ 侧主源
        //   （见 world.h m_gravLight* 头注释）→ 收口到柱末一次；级联批进行中再并入级联联合盒（见函数尾）。
        emit gravityBlockFell(x, cy, z, int(b));          // 呈现层转 spawnFallingBlock（每格一实体，保留真实 id）
        // 审查修 #4：本格已清 Air → 补邻域附着物复检（正上方族 + 6 邻火把 / 红石火把）。柱中段清格时上方
        //   仍是下一格沙 → 各 check 早退 no-op；只有清到**柱顶格**时柱顶 / 柱侧附着物才被本扫带走（含级联：
        //   甘蔗整柱 / 雪层整柱由各 check 内部的 drop*Column 继续向上收）。不含重力复检（防柱内指数重入，
        //   见 recheckAttachmentsAfterClear 头注释）。caller 末尾 1 次 worldChanged 覆盖本扫的静默写。
        recheckAttachmentsAfterClear(x, cy, z, b);
        breakNetherPortalsAround(x, cy, z); // review #27：坍落清格邻接门面 → 熄门（同钩子族口径；b 非空构造）
        if (!any) firstY = cy;
        any = true;
        ++cy;
        FrameProfiler::instance()->count("gravCellN"); // t933 可观测：本窗坍落重力格数（F3 cnt 行）
    }
    if (!any) return;
    // t933 perf：坍落可观测（F3 cnt 行 / 1s 窗聚合，FrameProfiler 单例）。
    FrameProfiler::instance()->count("gravColN");
    if (m_batchGravity) {
        // t933 级联批进行中（cascadeGravityAround 独占置位）：并入联合盒即返 —— 光照重算 + worldChanged +
        //   clearAllDirty 收口到级联末尾**一次**（等价性论证见 world.h m_gravLight* 头注释）。
        gravLightExpand(x, firstY, z);
        gravLightExpand(x, cy - 1, z);
        return;
    }
    // t933 非批路径（① 放置自检 / ② 直接上方支线的单柱坍落）：柱末**一次** refloodBox 覆盖全柱各格盒之并
    //   （x/z ±15；y 自首格 -15 到世界顶 —— 与逐格 recomputeLightAround 的 opacity 分支盒公式一致；重力
    //   方块均遮光 → doSky=true）。终态与逐格重 flood 等价：每格影响 ⊆ 其 ±15 盒 ⊆ 并盒，盒外格不受影响。
    {
        constexpr int R = 15;
        const int lx0 = std::max(0, x - R), lx1 = std::min(m_width - 1, x + R);
        const int lz0 = std::max(0, z - R), lz1 = std::min(m_depth - 1, z + R);
        const int ly0 = std::max(0, firstY - R), ly1 = m_height - 1;
        if (ly0 <= ly1)
            refloodBox(lx0, ly0, lz0, lx1, ly1, lz1, /*doSky=*/true);
    }
    emit worldChanged();      // 驱动 mesh 重建（沙柱消失）
    m_chunks.clearAllDirty(); // 两段重建完统一清脏（同 setBlock 末尾）
}

// 审查修 #4（Review 2026-08-23 中危；头注释见 world.h）：静默清格后的邻域附着物复检。dropGravityColumn
//   每清一格调 + clearBlockSilent 末尾调（口径合一）。正上方族直接复用 setBlock 主入口的 check*OnEdit
//   （自带早退——正上方非对应方块 → no-op 零写入零 emit——与掉落语义 / 批量收口 emit），6 邻火把 / 红石
//   火把本层内联扫（原口径在 PlayerController::dropUnsupportedTorchesAround——Game 层私有，静默清格路径
//   够不着；此处 World 层等价移植：state 解码唯一附着格 + torchSupportBlock 仍撑则保留，放置 / 掉落同
//   口径不漂移）。火把清格走「静默直写 + 全套 note + blockBroken + blockDroppedAsItem(dropId) +
//   recomputeLightAround」（火把是光源，移除须重 flood；红石火把是电力族，notePowerWrite 入脏集）；
//   实际掉落 ≥1 个火把时末尾自 emit worldChanged + clearAllDirty（review24 #2：红石火把是 chunk mesh
//   几何，clearBlockSilent 的收口 emit 在本函数**之前** → 不自 emit 则火把格错过重建信号 = 幽灵网格
//   残留；同 ① check* 兄弟「真有写入才 emit」口径，无掉落零 emit）。**不含
//   checkGravityBlockOnEdit**（见 world.h 头注释：柱内重入 = 指数级递归重扫；重力延续由
//   dropGravityColumn 自身循环 / caller 显式补调负责）。
void World::recheckAttachmentsAfterClear(int x, int y, int z, quint8 oldId)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth) return;
    const quint8 id = BlockRegistry::Air; // 清格复检恒按「本格现内容 = Air」口径（check 族签名第 5 参）
    // ① 正上方附着族（各自内部守卫 id==Air + oldId 非本族 → 玩家直破路径不双掉，与 setBlock 主入口零差异）：
    checkCactusOnEdit(x, y, z, oldId, id);         // t445：仙人掌失撑整柱掉落
    checkDeadBushOnEdit(x, y, z, oldId, id);       // t504：枯灌木失撑掉木棒
    checkFlowerMushroomOnEdit(x, y, z, oldId, id); // t507：花 / 蘑菇失撑掉 dropId
    checkPressurePlateOnEdit(x, y, z, oldId, id);  // t494：压力板失撑掉落
    checkSugarcaneOnEdit(x, y, z, oldId, id);      // t524：甘蔗失撑整柱掉落
    checkSnowLayerOnEdit(x, y, z, oldId, id);      // t527：雪层失撑整柱坍落为携带层数的下落实体
    checkRailOnEdit(x, y, z, oldId, id);           // t565/t733：铁轨失撑掉落 + 邻轨连接重算
    checkTrapdoorDoorSupportOnEdit(x, y, z, oldId, id); // t851：活板门 / 门失撑级联掉落（静默清格公共复检收口）
    checkPaintingSupportOnEdit(x, y, z, oldId, id); // t837①：画作支撑墙失撑 → 整画掉落（静默清格公共复检收口）
    // ② 6 邻火把 / 红石火把（火把非 solid 不撑他火把 → 单趟扫即足够，无级联）：
    bool torchDropped = false; // review24 #2：本扫是否实际掉落 ≥1 火把（决定收口 emit 是否发——无掉落零 emit）
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &d : kNb) {
        const int tx = x + d[0], ty = y + d[1], tz = z + d[2];
        if (tx < 0 || ty < 0 || tz < 0 || tx >= m_width || ty >= m_height || tz >= m_depth) continue;
        const quint8 tb = m_chunks.blockAt(tx, ty, tz);
        // t638 ⑥：红石火把同火把附着编码（torchAttachOffset 掩熄灭位），一并扫。
        if (tb != BlockRegistry::Torch && tb != BlockRegistry::RedstoneTorch) continue;
        int ax, ay, az;
        BlockRegistry::torchAttachOffset(m_chunks.stateAt(tx, ty, tz), ax, ay, az);
        const int sx = tx + ax, sy = ty + ay, sz = tz + az;
        if (sx != x || sy != y || sz != z) continue; // 附着格非本清格 → 本清格不是它的支撑（各清格各自复检）
        if (BlockRegistry::torchSupportBlock(m_chunks.blockAt(sx, sy, sz), m_chunks.stateAt(sx, sy, sz)))
            continue; // 附着格仍支撑（R1 口径 a890bfa 合成判定）→ 火把保留（同 L12：放置 / 掉落同口径）
        m_chunks.setBlock(tx, ty, tz, BlockRegistry::Air); // 静默直写 + 标脏（含边界邻接）；不经 World::setBlock（无重入）
        noteGrowthWrite(tx, ty, tz, tb, BlockRegistry::Air); // 火把非生长方块 → no-op，保持同族写入一致
        noteFluidWrite(tx, ty, tz, tb, BlockRegistry::Air);  // 非流体 → no-op（同上）
        noteIceWrite(tx, ty, tz, tb, BlockRegistry::Air);    // 非冰 → no-op（同上）
        noteFireWrite(tx, ty, tz, tb, BlockRegistry::Air);   // 非火 → no-op（同上）
        notePowerWrite(tx, ty, tz, tb, BlockRegistry::Air);  // 红石火把是电力族 → 邻网络下 tick 重算（t683 同口径）
        emit blockBroken(tx, ty, tz, int(tb));               // 破块粒子 / 音（机制等价 MC 火把附着面移除脱落）
        emit blockDroppedAsItem(tx, ty, tz, BlockRegistry::dropId(tb)); // 掉落物（Main.qml spawnItem，count 恒 1）
        recomputeLightAround(tx, ty, tz, tb, BlockRegistry::Air); // 火把是光源 → 移除须重 flood（清光源）
        torchDropped = true;
    }
    // review24 #2：火把 / 红石火把是 chunk mesh 几何（partialblockgeometry）——实际掉落 ≥1 时自 emit
    //   worldChanged + clearAllDirty（与 ① check* 兄弟同口径）。旧版「不发自己的 worldChanged、靠两 caller
    //   末尾批量收口覆盖」对 dropGravityColumn 成立（emit 在循环后）但对 clearBlockSilent 不成立（其
    //   worldChanged/clearAllDirty 在 recheck **之前**已收口）→ 红石火把格直写标脏后错过本次重建信号 =
    //   视觉幽灵火把残留（掉落物已生成，一把火把两处可见）。caller 侧多发一次幂等 worldChanged 无害
    //   （每实际掉落格至多 1 次自 emit——check 族柱顶 ≤1 趟 + 火把族逐侧挂格各 1 次；同 destroySphereSilent
    //   逐格调 check* 的既有先例；clearBlockSilent 的主 emit 在前、本 emit 在后 → 后者才是携带火把清格的重建信号）。
    if (torchDropped) {
        emit worldChanged();      // 驱动 mesh 重建（火把段消失）
        m_chunks.clearAllDirty(); // 两段重建完统一清脏（同 check* 兄弟末尾）
    }
}

// t799 重力方块失撑复检（头注释见 world.h；机制等价 MC 1.0「沙放火把上立即落 / 支撑失效即刻落」；t794
//   铁砧三阶段同谓词入族——失撑判定与沙同判，着地语义分叉由 EntityManager tick 分派）。
//   旧实现（t117/t220）：Main.qml maybeTriggerFallingBlock 消费 blockPlaced/blockBroken 信号在呈现层
//   嵌套 setBlock(air)+spawnFallingBlock —— 放置路径实测不触发（用户报「沙放火把 / 睡莲 / 草丛 / 半砖上
//   稳定站住，只有 >1 格落差才变掉落物」），且爆炸 / TNT 点火 / 焚毁等静默写入口完全绕过该 QML 链。
//   本检查下沉 World 层（同甘蔗 / 雪层支撑校验族先例）后：写入口全收口，两路径同一谓词。
void World::checkGravityBlockOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    Q_UNUSED(oldId); // 各分支都只看编辑后状态（id + 邻格现值）；参数保留供 checkXxxOnEdit 族签名一致
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    if (y < 0 || y >= m_height) return;
    // ① 放置自检：本格刚写入重力方块且下方非完整立方支撑（火把 / 睡莲 / 草丛 / 半砖 / 空气 / 水…）→ 坍落。
    //   y==0（世界底）无下格 → 视为失撑（实体落出世界由 EntityManager tick 移除，同旧 QML 版 y>0 守卫语义）。
    //   t930：不再提前 return —— 放置重力方块同属「一格内的编辑」，其 26 邻域既有悬空重力方块亦须复检
    //   （用户口径「放置一个方块在它一格之内也更新沙子悬浮状态」）；本格柱坍落后已为 Air → ③ 扫描天然跳过。
    if (BlockRegistry::isGravityBlock(id)) {
        const bool supported = (y > 0) && BlockRegistry::isFullCube(m_chunks.blockAt(x, y - 1, z));
        if (!supported) dropGravityColumn(x, y, z);
    } else if (!BlockRegistry::isFullCube(id)) {
        // ② 支撑变化复检（直接上方支线）：本格编辑后非完整立方（被破为 Air / 换成不完整方块）且正上方是
        //   重力方块 → 上方坍落。覆盖：挖支撑（→Air）、支撑被替换为火把 / 半砖等（放上去那刻上方沙即落）、
        //   水蒸发 / 焚毁 / 爆炸。编辑后仍完整立方 → 直接上方不失撑（但 ③ 邻域扫描仍跑——放置完整立方
        //   同属编辑，须更新邻域沙悬浮态）。
        if (y + 1 < m_height && BlockRegistry::isGravityBlock(m_chunks.blockAt(x, y + 1, z)))
            dropGravityColumn(x, y + 1, z);
    }
    // ③ t930 26 邻域级联复检：任何编辑（破坏 / 放置）→ 扫本格 26 邻域重力方块失撑态并连锁坍落。
    cascadeGravityAround(x, y, z);
}

// t930 26 邻域重力级联复检（头注释见 world.h）。BFS 队列元素 = 「须扫其 26 邻」的格；种子 = 编辑格，
//   之后每个坍落清掉的柱格 + 柱顶上方格入队（周边连锁 + 柱顶附着物脱落后其上方支线）。全程只读
//   blockAt（失撑判定）+ dropGravityColumn（静默清 + 信号），无 check*OnEdit 重入。
void World::cascadeGravityAround(int x, int y, int z)
{
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    if (y < 0 || y >= m_height) return;
    // t933 perf：级联批收口（见 world.h m_gravLight* 头注释）—— 全程置 m_batchGravity，dropGravityColumn
    //   批内并入联合盒（不逐格重光照、不逐柱 emit worldChanged）；级联末对联合盒 ±15 一次 refloodBox +
    //   一次 worldChanged + clearAllDirty。沙坑爆炸场景从「数十柱 × 每柱 ~10 格 × 全盒重 flood + 每柱 1 次
    //   QML 扇出」降到「一次联合盒重 flood + 一次扇出」（destroySphereSilent t320 批量收口同先例）。
    //   无重入（dropGravityColumn / recheckAttachmentsAfterClear 均不含 checkGravityBlockOnEdit，见各头
    //   注释）→ 标志不会被嵌套级联破坏；真有坍落才收尾 emit（无坍落零 emit，与逐柱口径一致）。
    FrameProfiler::instance()->count("cascN"); // t933 可观测：本窗级联扫描次数（F3 cnt 行）
    gravLightReset();
    m_batchGravity = true;
    std::vector<std::array<int, 3>> queue;
    queue.push_back({x, y, z});
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        const int qx = queue[qi][0], qy = queue[qi][1], qz = queue[qi][2];
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (dx == 0 && dy == 0 && dz == 0) continue;
                    const int nx = qx + dx, ny = qy + dy, nz = qz + dz;
                    if (nx < 0 || nz < 0 || nx >= m_width || nz >= m_depth) continue;
                    if (ny < 0 || ny >= m_height) continue;
                    if (!BlockRegistry::isGravityBlock(m_chunks.blockAt(nx, ny, nz))) continue; // 邻格非沙族 → 零成本早退
                    // 失撑判定（同 ① 口径）：世界底无下格视为失撑；下方完整立方 → 有支撑。
                    if (ny > 0 && BlockRegistry::isFullCube(m_chunks.blockAt(nx, ny - 1, nz))) continue;
                    // 失撑 → 整柱坍落。先记录柱范围（坍后为 Air 读不出）→ 柱格 + 柱顶上方格入队（连锁）。
                    int top = ny;
                    while (top + 1 < m_height
                           && BlockRegistry::isGravityBlock(m_chunks.blockAt(nx, top + 1, nz)))
                        ++top;
                    for (int cy = ny; cy <= top; ++cy) queue.push_back({nx, cy, nz});
                    if (top + 1 < m_height) queue.push_back({nx, top + 1, nz});
                    dropGravityColumn(nx, ny, nz);
                }
    }
    // t933 级联收尾：一次联合盒重光照（±15 余量 + 遮光翻转到世界顶，与 recomputeLightAround 的 opacity
    //   分支盒公式一致）+ 一次 worldChanged + clearAllDirty。无坍落 → 零 emit 零重算（干净早退）。
    const bool dropped = m_gravLightAny;
    int bx0 = 0, by0 = 0, bz0 = 0, bx1 = 0, by1 = 0, bz1 = 0;
    if (dropped) {
        constexpr int R = 15;
        bx0 = std::max(0, m_gravLightX0 - R);
        by0 = std::max(0, m_gravLightY0 - R);
        bz0 = std::max(0, m_gravLightZ0 - R);
        bx1 = std::min(m_width - 1, m_gravLightX1 + R);
        by1 = m_height - 1; // 遮光翻转 → 天光列 first-opaque 须重 seed 到顶（同 recomputeLightAround）
        bz1 = std::min(m_depth - 1, m_gravLightZ1 + R);
    }
    gravLightReset();
    if (!dropped) return;
    refloodBox(bx0, by0, bz0, bx1, by1, bz1, /*doSky=*/true);
    emit worldChanged();      // 驱动 mesh 重建（本批全部坍落柱）
    m_chunks.clearAllDirty(); // 两段重建完统一清脏（同 destroySphereSilent 批量收口）
}

// t565 铁轨连接重算（见 world.h 头注释）：读 (x,y,z) 的 4 向 × 3 高（同层 / 上 / 下 —— 坡度邻轨存在性，
//   t667）邻块 id → BlockRegistry::railConnections（单一权威，t666 规则集）→ 与当前 state 不同则静默直写
//   新 state（m_chunks.setBlock(id,state) 标脏；不经 World::setBlock → 无重入 / 无破放信号 —— 连接变化是
//   系统派生态非玩家动作）。非铁轨格 / state 未变 → no-op。
//   t638：轨判定扩 isRail 家族（普通 / 动力 / 探测轨互连——机制等价 MC 1.0 三种轨同轨互连）；写入保留
//   探测轨 / 动力轨的 bit4 通电视觉位（DetectorRailStateOnFlag / GoldenRailStateOnFlag——只重算低 4 位连接，
//   不清「被压过」标记）。t666：轴偏好位 bit5（RailAxisEWFlag）随当前轴守恒写回（0 连接保孤轨轴向；
//   有连接时镜像当前轴 —— 见 railConnections 头注释规则）。
void World::recomputeRailConnections(int x, int y, int z, bool &outChanged)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth) return;
    const quint8 rb = m_chunks.blockAt(x, y, z);
    if (!BlockRegistry::isRail(rb)) return;
    const auto probe = [&](int dx, int dz) -> BlockRegistry::RailProbe {
        return { m_chunks.blockAt(x + dx, y, z + dz),
                 m_chunks.blockAt(x + dx, y + 1, z + dz),
                 m_chunks.blockAt(x + dx, y - 1, z + dz),
                 m_chunks.stateAt(x + dx, y, z + dz), // t983 ① 平行拒连读侧臂 bit5 轴
                 m_chunks.stateAt(x + dx, y + 1, z + dz), // review0906 #5 坡臂判读上层邻连接位/轴
                 m_chunks.stateAt(x + dx, y - 1, z + dz) }; // review0906 #5 坡臂判读下层邻连接位/轴
    };
    const quint8 curState = m_chunks.stateAt(x, y, z);
    quint8 con = BlockRegistry::railConnections(rb, curState,
                                                probe(1, 0), probe(-1, 0),
                                                probe(0, 1), probe(0, -1));
    // t638：探测轨 bit4（通电视觉）/ t658 动力轨 bit4（通电贴图）不参与连接 —— 合并回写（连接重算不清
    //   「被压过」/「通电」亮态标记）。t666：轴偏好位 bit5 守恒写回（孤轨轴向保活）。t812：转辙器位
    //   bit6（弯向记忆）/ bit7（通电记忆）守恒写回——T 交叉的切弯记忆不被邻块编辑重算冲掉（动力 / 探测
    //   轨 bit6/7 恒 0，并入无害）。
    const quint8 preserved = quint8(curState
        & (BlockRegistry::RailAxisEWFlag
           | BlockRegistry::RailSwitchCurveFlag
           | BlockRegistry::RailSwitchPoweredFlag
           | (rb == BlockRegistry::DetectorRail ? BlockRegistry::DetectorRailStateOnFlag : 0)
           | (rb == BlockRegistry::GoldenRail ? BlockRegistry::GoldenRailStateOnFlag : 0)));
    con = quint8(con | preserved);
    // t666 轴偏好位镜像当前轴（有连接时）：直轨最后形态 = 孤轨形态（MC 轨断连后保留 metadata 语义）。
    //   拐角（2 垂直位）不算轴 → 保持现有偏好位不变。t1018：单轴连接也镜像——延伸松弛可产生与放置
    //   面向（bit5）相反的连接（EW 面向新轨接续 NS 线端），bit5 随实连轴翻转，孤轨形态 = 最后实连轴。
    //   review0907 A-P3-1：单轴分支加交叉轴零位守卫——拐角（X+Z 各一位）同样满足「本轴有位」，原条件
    //   会把拐角误判成单轴而翻转偏好位（与上行「拐角不算轴」注释相悖，属防雷收口）。
    if ((con & (BlockRegistry::RailConnPx | BlockRegistry::RailConnNx)) ==
        (BlockRegistry::RailConnPx | BlockRegistry::RailConnNx))
        con = quint8(con | BlockRegistry::RailAxisEWFlag);      // 贯穿 X → EW 偏好
    else if ((con & (BlockRegistry::RailConnPz | BlockRegistry::RailConnNz)) ==
             (BlockRegistry::RailConnPz | BlockRegistry::RailConnNz))
        con = quint8(con & quint8(~BlockRegistry::RailAxisEWFlag)); // 贯穿 Z → NS 偏好
    else if ((con & (BlockRegistry::RailConnPx | BlockRegistry::RailConnNx)) != 0
             && (con & (BlockRegistry::RailConnPz | BlockRegistry::RailConnNz)) == 0)
        con = quint8(con | BlockRegistry::RailAxisEWFlag);      // t1018 单 X 臂（无 Z 位）→ EW 偏好
    else if ((con & (BlockRegistry::RailConnPz | BlockRegistry::RailConnNz)) != 0
             && (con & (BlockRegistry::RailConnPx | BlockRegistry::RailConnNx)) == 0)
        con = quint8(con & quint8(~BlockRegistry::RailAxisEWFlag)); // t1018 单 Z 臂（无 X 位）→ NS 偏好
    if (con == curState) return; // 连接未变 → 不写（防无谓标脏）
    m_chunks.setBlock(x, y, z, rb, con); // 静默直写 + 标脏（含边界邻接）
    // 铁轨族 solid=false 不遮光 → 光场无变化，免 recomputeLightAround。
    outChanged = true;
}

// t565 setBlock 编辑后铁轨连接复检（见 world.h 头注释；机制等价 MC 1.0 rail 放 / 破自动连接 / 断开）。
//   任何编辑都可能改变「本格 Rail 自身 + 邻域 Rail」的连接位（放 Rail → 互连；破 Rail → 邻轨断向）。
//   重算范围 = 本格 + 四向各 3 高（同层 / 上 / 下）—— t667 坡度引入后，置 / 破轨会改变邻列 ±1 高轨的
//   连接（新轨低 1 格 → 上方台阶轨新增下坡连接位，反之亦然），故 13 格（本格 + 4 向 × 3 高）都要复检。
//   有实际 state 写入才 1 次 worldChanged（批量收口）。静默直写不重入。
//   t733 起本函数兼任铁轨编辑钩子的**失撑掉落**入口（见函数体内 t733 注释段）：支撑位被清 → 正上方铁轨
//   坍落为掉落物，先于连接重算执行。
void World::checkRailOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    Q_UNUSED(id); // 审查修 #15 后失撑判定只读「本格现内容」（blockAt/stateAt），编辑后 id 不再参与判定；
                  //   参数保留供 check*OnEdit 族签名一致（其余调用方 / 头注释均按 5 参契约）。
    bool changed = false;
    // t733 铁轨失撑掉落（R19.11「挖掉铁轨底部方块 → 铁轨不得浮空」；普通 / 动力 / 探测三族统一，isRail
    //   单一权威）。守卫：被编辑块本身非铁轨——玩家直破铁轨的掉落由 finishMiningAt 通用 drop 路径负责
    //   （三族 dropId=自身），此处再掉会双掉。审查修 #15（Review 2026-08-23 低危）：旧守卫还要求
    //   `id == Air`（仅挖掘 / 爆炸清格触发），**本格被换成非支撑方块**（冰融化成水 setWaterSilent 写
    //   Water / 可燃物焚毁写 Fire / 放火把等异形方块）时不触发 → 冰融成水后轨悬浮到水蒸发。改为失撑
    //   判定只看「本格现内容」：正上方是铁轨、且本格（铁轨唯一支撑位，恒为正下方——轨不贴墙、无 state
    //   附着编码可解）已非有效支撑（isTopFlushSupport 单一权威：完整立方 ∨ 上半砖，t741；与红石粉 / 门族
    //   同语义；水 / 火 / 火把等均非满顶支撑）→ 铁轨立即
    //   坍落为掉落物（连接位 / 动力轨通电位 / 探测轨压过位随方块清除一并丢弃，掉落物 = 自身物品）。
    //   机制等价 MC「铁轨支撑方块被移除即脱落，不浮空残留，不重新粘到别处」。t571②【自然失撑掉落：恒发
    //   （含创造）】—— World 层无 drop 标志概念，失撑坍落是结构后果（同板 / 甘蔗 / 仙人掌族）。
    //   **为何收口在本 hook 而非 playercontroller dropUnsupported* 族（t738/t739/t744 先例）**：本函数是
    //   铁轨编辑钩子，五个写入口（4/5 参数 setBlock（玩家挖 / 放）、clearBlockSilent（TNT 三条点火路径
    //   变实体）、setWaterSilent、destroySphereSilent 逐破坏格（Stalker / TNT 陆地爆炸；水下链式引燃走
    //   clearBlockSilent））末尾全部调它 → **一处实现即覆盖挖掘 / 爆炸三路 / TNT 点火三路全部失撑源**，
    //   未来新增静默清路径自动继承（避免 t744「每条点火路径逐处补扫」的漏点维护成本）。掉落通道走
    //   blockDroppedAsItem（Main.qml → spawnItem，同 World 支撑校验族 cactus / plate / sugarcane）。
    //   **先掉轨再重算连接**：下方 13 格重算表覆盖被掉轨的全部水平邻（y+1 行在列）→ 邻轨连接位按
    //   「轨已消失」重算，不残留指向空位的连接形态。notePowerWrite 维护电力脏集（动力轨是接收器 /
    //   探测轨是源，isPowerFamilyBlock 含两者——轨被清后邻网络下 tick 重算，机制同 t683 爆炸补 note）。
    if (!BlockRegistry::isRail(oldId)) {
        const int ry = y + 1;
        if (x >= 0 && z >= 0 && x < m_width && z < m_depth && ry >= 0 && ry < m_height) {
            const quint8 rb = m_chunks.blockAt(x, ry, z);
            if (BlockRegistry::isRail(rb)
                && !BlockRegistry::isTopFlushSupport(m_chunks.blockAt(x, y, z), m_chunks.stateAt(x, y, z))) {
                m_chunks.setBlock(x, ry, z, BlockRegistry::Air); // 静默清（直写 + 标脏，不经 World::setBlock → 不重入本检查）
                notePowerWrite(x, ry, z, rb, BlockRegistry::Air); // 电力脏标记（动力 / 探测轨在电力族，t683 同口径）
                emit blockBroken(x, ry, z, int(rb));               // 破块粒子 / 音（机制等价 MC 失撑坍落反馈）
                emit blockDroppedAsItem(x, ry, z, int(rb));        // 掉落物 = 自身（三族 dropId=自身；state 丢弃）
                recomputeLightAround(x, ry, z, rb, BlockRegistry::Air); // 轨不遮光，仍重 flood 保正确（同板族）
                changed = true; // 并入末尾 1 次 worldChanged + clearAllDirty 收口（N 写 1 emit）
            }
        }
    }
    // （连接重算不筛编辑类型——放 / 破任意方块都可能改变邻轨连接，简单正确。）
    #define VO_RECOMPUTE_RC(XX, YY, ZZ) recomputeRailConnections(XX, YY, ZZ, changed)
    VO_RECOMPUTE_RC(x, y, z);            // 本格若是 Rail → 重算自身连接
    VO_RECOMPUTE_RC(x + 1, y, z);        // +X 列（同 / 上 / 下 3 高）Rail 回连 / 断连
    VO_RECOMPUTE_RC(x + 1, y + 1, z);
    VO_RECOMPUTE_RC(x + 1, y - 1, z);
    VO_RECOMPUTE_RC(x - 1, y, z);        // -X 列
    VO_RECOMPUTE_RC(x - 1, y + 1, z);
    VO_RECOMPUTE_RC(x - 1, y - 1, z);
    VO_RECOMPUTE_RC(x, y, z + 1);        // +Z 列
    VO_RECOMPUTE_RC(x, y + 1, z + 1);
    VO_RECOMPUTE_RC(x, y - 1, z + 1);
    VO_RECOMPUTE_RC(x, y, z - 1);        // -Z 列
    VO_RECOMPUTE_RC(x, y + 1, z - 1);
    VO_RECOMPUTE_RC(x, y - 1, z - 1);
    #undef VO_RECOMPUTE_RC
    if (changed) {
        emit worldChanged();        // 驱动 mesh 重建（铁轨形态切换）
        m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
    }
}

// t664 末地传送门框架环完整性检查（纯读；见 world.h 头注释）。环 = 12 框架格围绕 3×3 内圈中心 (cx,cy,cz)：
//   ±2 环上不含四角 —— {(cx±2, y, cz-1..cz+1)} ∪ {(cx-1..cx+1, y, cz±2)}。全部为 EndPortal 框架且
//   state bit0 激活（EndPortalStateActiveFlag）→ 环完整。任一缺失 / 未激活 / 越界 → false。
bool World::endPortalRingComplete(int cx, int cy, int cz) const
{
    // 环 12 格：每条边 3 个（x = cx±2 时 z ∈ {cz-1,cz,cz+1}；z = cz±2 时 x ∈ {cx-1,cx,cx+1}）。
    for (int i = -1; i <= 1; ++i) {
        const int ring[4][2] = {
            {cx + 2, cz + i}, {cx - 2, cz + i},
            {cx + i, cz + 2}, {cx + i, cz - 2},
        };
        for (const auto &p : ring) {
            if (p[0] < 0 || p[0] >= m_width || p[1] < 0 || p[1] >= m_depth) return false;
            if (cy < 0 || cy >= m_height) return false;
            const quint8 b = m_chunks.blockAt(p[0], cy, p[1]);
            if (b != BlockRegistry::EndPortal) return false; // 非框架格 → 环断裂
            if ((m_chunks.stateAt(p[0], cy, p[1]) & BlockRegistry::EndPortalStateActiveFlag) == 0)
                return false; // 框架未放眼激活 → 环未就绪
        }
    }
    return true;
}

// t664 尝试打开末地传送门（见 world.h 头注释）：环完整 → 3×3 内圈 (cx±1, cy, cz±1) 各格写门面
//   （EndPortalSurface=131 薄星平面）。门面格若已存在同 id → 跳过（防重复写）。静默直写 m_chunks
//   （不经 World::setBlock → 不重入完整性复检 / 不逐格发 broken/placed）+ 末尾 1 次 worldChanged
//   （N 写 1 emit，同 dropCactusColumn 批量收口模式）+ clearAllDirty。门面是普通方块 → 存档持久化。
bool World::tryOpenEndPortal(int cx, int cy, int cz)
{
    if (!endPortalRingComplete(cx, cy, cz)) return false; // 环未就绪 → 不开
    bool changed = false;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const int px = cx + dx, pz = cz + dz;
            if (px < 0 || px >= m_width || pz < 0 || pz >= m_depth || cy < 0 || cy >= m_height) continue;
            if (m_chunks.blockAt(px, cy, pz) == BlockRegistry::EndPortalSurface) continue; // 已开 → 跳过
            m_chunks.setBlock(px, cy, pz, BlockRegistry::EndPortalSurface); // 静默直写 + 标脏
            changed = true;
        }
    }
    if (changed) {
        emit worldChanged();
        m_chunks.clearAllDirty();
    }
    return true;
}

// t664 编辑后末地传送门完整性复检（见 world.h 头注释；同 checkRailOnEdit 模式）。任何编辑都可能让环失效
//   （框架被破 → 门面应全消失）。复检范围 = 本格周围 ±3 立方体内的 EndPortalSurface 门面格；对每块门面
//   反查其环中心（门面在内圈 3×3，中心 ∈ {px-1..px+1} × {pz-1..pz+1} 九候选），环不完整 → 静默清门面。
//   静默直写不经 setBlock → 不重入本检查；末尾 1 次 worldChanged 批量收口。
void World::checkEndPortalIntegrity(int x, int y, int z, quint8 oldId, quint8 id)
{
    Q_UNUSED(oldId); Q_UNUSED(id); // 任何编辑都可能影响门面（框架被破 / 门面自身被瞬破）→ 不筛编辑类型
    // 快速筛：本编辑若与环完全无关（非框架 / 非门面 / 非环邻）→ 周围扫不到门面则 no-op（扫描代价可接受：
    //   每编辑 ±3 立方体 ~7³=343 格块读，仅含门面时继续；世界内门面极少）。
    bool changed = false;
    const int x0 = std::max(0, x - 3), x1 = std::min(m_width - 1, x + 3);
    const int y0 = std::max(0, y - 3), y1 = std::min(m_height - 1, y + 3);
    const int z0 = std::max(0, z - 3), z1 = std::min(m_depth - 1, z + 3);
    for (int sy = y0; sy <= y1; ++sy) {
        for (int sz = z0; sz <= z1; ++sz) {
            for (int sx = x0; sx <= x1; ++sx) {
                if (m_chunks.blockAt(sx, sy, sz) != BlockRegistry::EndPortalSurface) continue;
                // 门面格 (sx,sy,sz)：反查环中心候选（门面在内圈 3×3 → 中心 ∈ sx±1 / sz±1 九候选）。
                bool keep = false;
                for (int cx = sx - 1; cx <= sx + 1 && !keep; ++cx) {
                    for (int cz = sz - 1; cz <= sz + 1; ++cz) {
                        if (endPortalRingComplete(cx, sy, cz)) { keep = true; break; }
                    }
                }
                if (!keep) {
                    // 环不完整 → 门面消失（静默清 Air + 标脏，防门面悬浮在半开环里）。
                    m_chunks.setBlock(sx, sy, sz, BlockRegistry::Air);
                    changed = true;
                }
            }
        }
    }
    if (changed) {
        emit worldChanged();
        m_chunks.clearAllDirty();
    }
}

// t806 余烬门点燃检测（见 world.h 头注释；t725 v1 写死 2×3 内腔的 PlayerController 版本泛化下沉 World
//   层单一权威 —— 同末地门三件套（endPortalRingComplete / tryOpenEndPortal）模式）。MC 规则参数表
//   （dev-spec t806 + t848 上限对齐 MC 1.0；单一权威常量，改门尺寸只动这 2 行）：
//     内腔开口宽 w ∈ [2, 21]（框外沿 4..23）、高 h ∈ [3, 21]（框外沿 5..23）——t806 时代上限 4×5，
//     t848 放宽至 21×21 内腔 = 23×23 框外沿（MC 1.0 传送门最大尺寸）；22+ 超限拒点；
//     矩形开口、黑曜石框（底梁 w 格 / 顶梁 w 格 / 左右边柱各 h 格）；四角不检查（MC 1.0 角块可选）。
//     边柱只验「本格是黑曜石」不验独占 → 相邻两门共用中间一竖列黑曜石时各自独立成门（t848 共享柱
//     语义；state=axis 按各自门面朝向独立编码，连通域熄灭按 axis+连通域天然互不干扰）。
// 检测流程（从「点燃格在开口哪一格」出发，任一点燃位同成门）：
//   ① 下探底梁：自点燃格向下 ≤kPortalMaxInteriorH(21) 步找黑曜石（点燃格可能在开口 3..21 层任意一层）。
//   ② 左探左沿：自底梁上一层向 -u 扫 ≤kPortalMaxInteriorW-1(20) 步空气（开口最宽 21 → 点燃列距左沿
//      ≤20；步进有界——OOB blockAt 返 Air 会让无界扫描滑出世界）。
//   ③ 量宽 / 量高：自左沿列在开口底层向 +u 量连续空气列数、自左沿列向上量连续空气层数（步进以 21 为界
//      截断 → 22+ 超限开口量出恒 21，随后 ④ 柱 / 梁校验打在实为内腔空气的「假框位」上自然判败 = 超限
//      拒点。t806 时代此处以旧上限 kMaxW-1=3 截断 → 5 宽内腔恒测 4 → ④ 右柱校验打第 5 内腔列判败，
//      即用户实测「4×4 以上点不着」的根因位）。
//   ④ 矩形 + 框架校验：开口 w×h 全空气（防 L 形 / 腔内异物）+ 梁柱全黑曜石。
// 全命中 → 开口整面填 NetherPortal（state=axis；逐格 setBlock 发 blockPlaced → 呈现层 portalHost 逐格
//   建 delegate + 放置音，机制对标 MC 点燃瞬间整门成形的多点事件）。t848 性能注：检测 O(w×h) ≤ 441 格
//   读；点燃写 ≤ 441 格 setBlock（一次 21×21 满门 ~441 次写入钩子族扇出，一次性代价同一次小规模爆炸可
//   接受；Air→门格纯放置不触发 breakNetherPortalsAround → 填门不自扰；每次写后 clearAllDirty 即时收口
//   → 无脏 chunk 累积风暴）。
// 越界 blockAt 返 Air ≠ Obsidian → 梁 / 柱校验自然判败（无 OOB 风险）。
bool World::tryIgniteNetherPortal(int ix, int iy, int iz)
{
    // t848 尺寸单一权威常量：内腔宽 2..21 / 高 3..21（框外沿 4×5 最小 .. 23×23 最大 = MC 1.0 上限）。
    //   t806 旧上限 4×5 时用户实测「4×4 以上点不着」：5 宽内腔在 ③ 量宽被旧 kMaxW-1=3 截断 → w 恒测 4 →
    //   ④ 右柱校验打到更宽内腔的空气格判败（4×5 本身并未失效——t806 探针 ⑧ 已证其四角点燃位全过，
    //   盲区在从未测过「用户期望的更大门」）。
    constexpr int kMinW = 2, kPortalMaxInteriorW = 21; // 内腔开口宽 2..21
    constexpr int kMinH = 3, kPortalMaxInteriorH = 21; // 内腔开口高 3..21
    const auto obs = [&](int x, int y, int z) -> bool {
        return m_chunks.blockAt(x, y, z) == BlockRegistry::Obsidian;
    };
    const auto air = [&](int x, int y, int z) -> bool {
        return m_chunks.blockAt(x, y, z) == BlockRegistry::Air;
    };
    // 单平面检测（u = 门展开轴水平单位向量）：找到含点燃格的黑曜石矩形开口 → 整面填门返 true。
    const auto tryPlane = [&](int ux, int uz, quint8 axisState) -> bool {
        // ① 下探底梁：自点燃格向下 ≤kPortalMaxInteriorH 步（点燃格可能在开口任意一层）。
        int yBase = -1;
        for (int dy = 1; dy <= kPortalMaxInteriorH; ++dy) {
            if (obs(ix, iy - dy, iz)) { yBase = iy - dy; break; }
        }
        if (yBase < 0) return false;                        // 点燃柱下方无底梁 → 非门
        // ② 左探开口左沿：自底梁上一层向 -u 扫空气（≤kPortalMaxInteriorW-1 步；命中非空气格即停——边柱是黑曜石）。
        int cx = ix, cz = iz;                               // 开口最左内柱（先假定点燃列即左沿）
        for (int s = 1; s <= kPortalMaxInteriorW - 1; ++s) {
            if (!air(ix - s * ux, yBase + 1, iz - s * uz)) break;
            cx = ix - s * ux; cz = iz - s * uz;
        }
        const int innerY0 = yBase + 1;                      // 开口底层
        // ③ 量宽（左沿列右侧的连续空气列数；开口总宽 = w+1 含左沿列自身）与量高（左沿列向上连续空气层数）。
        int wRight = 0;
        while (wRight < kPortalMaxInteriorW - 1 && air(cx + (wRight + 1) * ux, innerY0, cz + (wRight + 1) * uz))
            ++wRight;
        const int w = wRight + 1;
        if (w < kMinW) return false;                        // 低于最小宽（孤立柱 / 双柱贴墙）→ 拒
        int h = 0;
        while (h < kPortalMaxInteriorH && air(cx, innerY0 + h, cz)) ++h;
        if (h < kMinH) return false;                        // 低于最小高（开口顶层非黑曜石顶梁）→ 拒
        // ④ 矩形 + 框架校验：开口 w 列各格（底梁 / 开口 / 顶梁）全合规 + 左右边柱各 h 格黑曜石。
        //    四角（底/顶梁两端外斜角）不在任何检查列内 → 角块可有可无（MC 1.0 语义）。
        for (int c = 0; c < w; ++c) {
            const int bx = cx + c * ux, bz = cz + c * uz;   // 开口第 c 列
            if (!obs(bx, yBase, bz) || !obs(bx, innerY0 + h, bz)) return false; // 缺底 / 顶梁格
            for (int r = 0; r < h; ++r)
                if (!air(bx, innerY0 + r, bz)) return false;  // 非矩形（某列矮 / 腔内异物）→ 拒
        }
        for (int r = 0; r < h; ++r) {                       // 左右边柱（两翼各 h 格）
            if (!obs(cx - ux, innerY0 + r, cz - uz)) return false;
            if (!obs(cx + w * ux, innerY0 + r, cz + w * uz)) return false;
        }
        // 全命中 → 开口整面 w×h 填门面（state=axis：0=X 平面 / 1=Z 平面）。
        for (int c = 0; c < w; ++c)
            for (int r = 0; r < h; ++r)
                setBlock(cx + c * ux, innerY0 + r, cz + c * uz, BlockRegistry::NetherPortal, axisState);
        return true;
    };
    // 先试 X 平面（门沿 X 展开 / 面朝 ±Z），再试 Z 平面（门正交两向各试一次，机制等价 MC 检测顺序）。
    if (tryPlane(1, 0, quint8(0))) return true;
    return tryPlane(0, 1, quint8(1));
}

// t806 余烬门连通域熄灭（见 world.h 头注释；t725 自 PlayerController 下沉 World 层，逻辑逐行同源）。
//   BFS 收集 ±u（门展开轴水平）/ ±Y 同 axis 的 NetherPortal 格 → 全部 setWaterSilent 清 Air（静默：
//   多格逐格 blockBroken 会刷粒子/音风暴；worldChanged 仍逐格发 → 呈现层 portalHost cleanupVis 清孤儿）。
//   尺寸无关：连通域天然覆盖任意大小门（2×3 最小 .. 21×21 最大，t848）。门无物品形态（dropId=0）→ 无掉落。
void World::removeNetherPortalAt(int px, int py, int pz, int axis)
{
    // 门展开轴 u（axis=0 → 门沿 X 展开 / 面朝 ±Z；axis=1 → 沿 Z 展开 / 面朝 ±X）。
    const int ux = (axis == 0) ? 1 : 0;
    const int uz = (axis == 0) ? 0 : 1;
    struct Cell { int x, y, z; };
    std::vector<Cell> cells;
    std::vector<Cell> frontier{{px, py, pz}};
    while (!frontier.empty()) {
        const Cell c = frontier.back();
        frontier.pop_back();
        bool seen = false;
        for (const Cell &s : cells) {
            if (s.x == c.x && s.y == c.y && s.z == c.z) { seen = true; break; }
        }
        if (seen) continue;
        const quint8 bid = m_chunks.blockAt(c.x, c.y, c.z);
        const bool isSeed = (c.x == px && c.y == py && c.z == pz);
        if (bid != BlockRegistry::NetherPortal && !isSeed) continue;      // 非门格 → 不入域
        if (bid == BlockRegistry::NetherPortal && int(m_chunks.stateAt(c.x, c.y, c.z) & 1) != axis)
            continue;                                                     // 异轴门（X/Z 面贴邻）→ 不连
        cells.push_back(c);
        // 4 向扩展（门平面内：±u 水平 + ±Y 垂直）。
        frontier.push_back({c.x + ux, c.y, c.z + uz});
        frontier.push_back({c.x - ux, c.y, c.z - uz});
        frontier.push_back({c.x, c.y + 1, c.z});
        frontier.push_back({c.x, c.y - 1, c.z});
    }
    // 清域：setWaterSilent 静默清（主破坏格已由 caller 走 setBlock 清 + 发过一次事件；域内守卫防双清）。
    //   review #27：置 m_inRemoveNetherPortal 守卫——setWaterSilent 已并入 breakNetherPortalsAround 钩子
    //   族，置位期间钩子早退（本 BFS 一次收完整扇门，清域写不再嵌套触发二次连通域清除）。
    m_inRemoveNetherPortal = true;
    for (const Cell &c : cells) {
        if (m_chunks.blockAt(c.x, c.y, c.z) != BlockRegistry::NetherPortal)
            continue; // 种子已被 caller 清 / 异轴守卫已滤（防御双清）
        setWaterSilent(c.x, c.y, c.z, BlockRegistry::Air, 0);
    }
    m_inRemoveNetherPortal = false;
}

// t806 余烬门门框失撑熄灭（见 world.h 头注释；t725 自 PlayerController 下沉 World 层，逻辑同源）。
//   破块后扫 6 邻的 NetherPortal，各自经连通域熄灭整扇门。恒熄（含创造）：门失效是结构后果非掉落。
//   review #27：并入 World 写入钩子族（对照 checkEndPortalIntegrity 模式）—— 爆炸 / 焚毁 / 坍落 / 静默
//   清格等一切「拆格」路径与玩家挖掘同口径熄门（此前钩子仅挂 playercontroller 一处，系统路径拆门框后
//   门面残留）。快速路径 = 6 次 blockAt 邻读（无门格即返，流体批量热路径可承受，同 notePowerWrite 快
//   路径量级）。removeNetherPortalAt 清域期间（m_inRemoveNetherPortal）早退防嵌套 BFS。
void World::breakNetherPortalsAround(int x, int y, int z)
{
    if (m_inRemoveNetherPortal) return; // 连通域清除自管整扇门；其清域写不再重入本钩子
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &n : kNb) {
        const int px = x + n[0], py = y + n[1], pz = z + n[2];
        if (m_chunks.blockAt(px, py, pz) != BlockRegistry::NetherPortal) continue;
        const int axis = int(m_chunks.stateAt(px, py, pz) & 1);
        removeNetherPortalAt(px, py, pz, axis);
    }
}

// t837① 画作连通域移除（t721 自 PlayerController 下沉 World 层，见 world.h 头注释；BFS / 矩形圈定逻辑
//   逐行同源——终审修 M1 的「锚格反解矩形只清本画」语义原样迁移）。掉落走 blockDroppedAsItem（t851
//   dropUnsupportedDoorsAbove 同链；Main.qml spawnItem 掉画作物品 0x242 = dropId(Painting)）。
void World::removePaintingAt(int px, int py, int pz, int face, bool drop)
{
    int ux = 0, uz = 0;
    BlockRegistry::paintingRightOffset(face, ux, uz);
    const quint8 faceBits = quint8((face & 3) << BlockRegistry::PaintingStateFaceShift);
    // BFS 收集连通域（种子 (px,py,pz) 允许已被清 Air —— 直挖路径主破坏格先走 setBlock；邻格侧种子恒是画）。
    struct Cell { int x, y, z; };
    struct Rect { int ax, ay, az, w, h; }; // 画矩形：锚格(左上) + 沿 u 宽 w + 向下高 h
    std::vector<Cell> cells;
    std::vector<Rect> rects;
    std::vector<Cell> frontier{{px, py, pz}};
    while (!frontier.empty()) {
        const Cell c = frontier.back();
        frontier.pop_back();
        bool seen = false;
        for (const Cell &s : cells) {
            if (s.x == c.x && s.y == c.y && s.z == c.z) { seen = true; break; }
        }
        if (seen) continue;
        const quint8 bid = m_chunks.blockAt(c.x, c.y, c.z);
        const bool isSeed = (c.x == px && c.y == py && c.z == pz);
        if (bid != BlockRegistry::Painting && !isSeed) continue;        // 非画格 → 不入域
        if (bid == BlockRegistry::Painting
            && quint8(m_chunks.stateAt(c.x, c.y, c.z) & BlockRegistry::PaintingStateFaceMask) != faceBits)
            continue;                                                    // 异面画（共格平面对墙）→ 不连
        cells.push_back(c);
        if (bid == BlockRegistry::Painting
            && (m_chunks.stateAt(c.x, c.y, c.z) & BlockRegistry::PaintingStateAnchorFlag)) {
            // 锚格 index 反解本画矩形（终审修 M1：画身份由锚格承载，供下方按矩形圈定清理范围）。
            Rect r{c.x, c.y, c.z, 1, 1};
            BlockRegistry::paintingSize(int(m_chunks.stateAt(c.x, c.y, c.z)
                                            & BlockRegistry::PaintingStateIndexMask), r.w, r.h);
            rects.push_back(r);
        }
        // 4 向扩展（画面平面内：±u 水平 + ±Y 垂直）。
        frontier.push_back({c.x + ux, c.y, c.z + uz});
        frontier.push_back({c.x - ux, c.y, c.z - uz});
        frontier.push_back({c.x, c.y + 1, c.z});
        frontier.push_back({c.x, c.y - 1, c.z});
    }
    // 矩形包含判定：格沿 u 的偏移 du ∈ [0,w) 且 y ∈ (ay-h, ay]（画自锚格向下展开 h 格）。
    const auto inRect = [ux, uz](const Rect &r, int x, int y, int z) {
        const int du = (x - r.ax) * ux + (z - r.az) * uz;
        return du >= 0 && du < r.w && y <= r.ay && y > r.ay - r.h;
    };
    // 目标矩形 = 含种子坐标的锚格矩形（直挖清的是非锚格时，本画锚格仍在域内 → 仍可反解定位）。
    const Rect *target = nullptr;
    for (const Rect &r : rects) {
        if (inRect(r, px, py, pz)) { target = &r; break; }
    }
    // 掉落：整张画只 1 件画作物品（机制等价 MC 破画掉 1 个 painting item）。落点 = 种子格（玩家瞄的格）。
    if (drop)
        emit blockDroppedAsItem(px, py, pz, BlockRegistry::dropId(BlockRegistry::Painting));
    // 清域（终审修 M1：只清目标画）：target 命中 → 仅清该矩形内格（邻画在其自身矩形外 → 完好保留）；
    //   无 target（种子 = 被 caller 先清的锚格，本画矩形已不可反解）→ 清域内不被任何已识别矩形覆盖的
    //   残余格（邻画格已被其锚格矩形覆盖 → 跳过）。setWaterSilent 静默清（不发 blockBroken —— 多格画的
    //   逐格粒子/音会刷成风暴；主破坏格由 caller finishMiningAt 顶部的 setBlock 已清 + 已发一次事件；
    //   失撑路径种子格也在此静默清，同火把失撑模式）。worldChanged 仍逐格发 → mesh / 呈现层 paintingHost
    //   清孤儿（onWorldChanged 校验）。m_inRemovePainting 置位防 setWaterSilent 的画作钩子重入。
    m_inRemovePainting = true;
    for (const Cell &c : cells) {
        if (target) {
            if (!inRect(*target, c.x, c.y, c.z)) continue; // 邻画的格 → 不清（终审修 M1）
        } else {
            bool covered = false;
            for (const Rect &r : rects) {
                if (inRect(r, c.x, c.y, c.z)) { covered = true; break; } // 邻画矩形内 → 不清
            }
            if (covered) continue;
        }
        if (c.x == px && c.y == py && c.z == pz && m_chunks.blockAt(c.x, c.y, c.z) != BlockRegistry::Painting)
            continue; // 种子已被 caller 清（防御双清）
        setWaterSilent(c.x, c.y, c.z, BlockRegistry::Air, 0);
    }
    m_inRemovePainting = false;
}

// t837① 画作支撑墙失撑掉落复检（见 world.h 头注释；checkTrapdoorDoorSupportOnEdit 同款钩子族模式）。
void World::checkPaintingSupportOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    Q_UNUSED(oldId); // 守卫按「编辑后本格是否仍有效画墙」（id 谓词）判——oldId 保留供 checkXxxOnEdit 族签名一致
    if (m_inRemovePainting) return; // 连通域清除自管整张画；其清域写不再重入本钩子
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth || y < 0 || y >= m_height) return;
    // 本格新内容仍是有效画墙（R1 口径：isCollidable ∨ isFullCube，与 tryPlacePainting cellOk 同源）→
    //   画保留（置换为另一实体墙块 / state-only 写不动支撑）。
    if (BlockRegistry::isCollidable(id, m_chunks.stateAt(x, y, z)) || BlockRegistry::isFullCube(id))
        return;
    // 扫 4 水平邻的画（画的支撑墙恒在水平向；画格与其墙格同层）——任一画的墙格 == 本格 → 整画掉落。
    constexpr int kHoriz[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (const auto &o : kHoriz) {
        const int px = x + o[0], py = y, pz = z + o[1];
        if (px < 0 || pz < 0 || px >= m_width || pz >= m_depth) continue;
        if (m_chunks.blockAt(px, py, pz) != BlockRegistry::Painting) continue;
        const quint8 st = m_chunks.stateAt(px, py, pz);
        const int face = int(st & BlockRegistry::PaintingStateFaceMask) >> BlockRegistry::PaintingStateFaceShift;
        int wx = 0, wz = 0;
        BlockRegistry::paintingWallOffset(face, wx, wz);
        if (px + wx == x && pz + wz == z)
            removePaintingAt(px, py, pz, face, /*drop=*/true); // 恒掉（含创造，t571 自然失撑语义）
    }
}

// ── t656/t657/t658 红石电力系统 v1（见 world.h notePowerWrite / tickRedstone 头注释）──

// 红石族判定（粉 / 全部电源 / 全部接收器 —— notePowerWrite 触发筛选 + tickRedstone 接收器扫描共用）。
//   t722：IronDoor 并入接收器族（门两格——任一格被供电即整门开；state bit2 写入见 recomputePowerLocal
//   Phase B 分支）。t723：IronTrapdoor 同并入（state bit0 开合，见同处分支）。
bool World::isPowerFamilyBlock(quint8 id)
{
    using BR = BlockRegistry;
    return BR::isRedstoneDust(id)                        // 导线（t656）
        || id == BR::RedstoneBlock                       // 恒电源（t657）
        || id == BR::RedstoneTorch                       // 反相电源（t657）
        || id == BR::RedstoneLamp                        // 接收器：灯（t658）
        || id == BR::GoldenRail                          // 接收器：动力轨（t658）
        || id == BR::Rail                                // 接收器：普通轨 T 交叉转辙器（t812；非转辙器
                                                         //   形态接收器分支 no-op——入族保编辑可达性，
                                                         //   同动力轨全族入族先例）
        || BR::isTnt(id)                                 // 接收器：TNT（t658）
        || BR::isDispenser(id) || BR::isDropper(id)      // 接收器：发射器 / 投掷器（t658）
        || id == BR::IronDoor                            // 接收器：铁门（t722，仅红石驱动开合）
        || id == BR::IronTrapdoor                        // 接收器：铁活板门（t723，仅红石驱动开合）
        || id == BR::NoteBlock                           // 接收器：音符盒（t1028，通电上升沿发声；bit5 记忆位）
        || BR::isLever(id) || BR::isWoodButton(id) || BR::isStoneButton(id) // 源：拉杆 / 按钮（state bit0）
        || BR::isPressurePlate(id)                       // 源：压力板（state bit0）
        || id == BR::DetectorRail;                       // 源：探测轨有车标记（state bit4）
}

// t937 ② 电源族布局谓词（粉以外）：红石块 / 红石火把 / 拉杆 / 木石按钮 / 压力板 / 探测轨。与
//   powerSourceLevel 的源集合同员（按下 / 亮灭只影响重算**结果**，不影响「是否须重算」——纯 id 判定）。
//   notePowerWrite 快路径的邻格收窄面用：普通编辑只有挨着「电力读数的输入端」（粉 ∪ 本族）才可能改读数。
static bool isPowerEmitterBlock(quint8 id)
{
    using BR = BlockRegistry;
    return id == BR::RedstoneBlock
        || id == BR::RedstoneTorch
        || BR::isLever(id) || BR::isWoodButton(id) || BR::isStoneButton(id)
        || BR::isPressurePlate(id)
        || id == BR::DetectorRail;
}

// 电源对邻格的强电值（机制等价 MC 1.0 各电源直供 15）：红石块恒 15；红石火把亮态 15（熄灭态 0）；
// 拉杆 / 按钮按下态（state bit0）15；压力板压下态（state bit0）15；探测轨「有车」标记（state bit4）15。
// 其余 → 0。只读 m_chunks + state。
int World::powerSourceLevel(int x, int y, int z) const
{
    const quint8 b = m_chunks.blockAt(x, y, z);
    const quint8 st = m_chunks.stateAt(x, y, z);
    if (b == BlockRegistry::RedstoneBlock) return 15;                 // t657 恒电源
    if (b == BlockRegistry::RedstoneTorch)
        return (st & BlockRegistry::RedstoneTorchStateOffFlag) ? 0 : 15; // t657 亮态供能 / 熄灭（反相）不供
    if (BlockRegistry::isLever(b) || BlockRegistry::isWoodButton(b)
        || BlockRegistry::isStoneButton(b) || BlockRegistry::isPressurePlate(b))
        return (st & 1) ? 15 : 0;                                     // 拉杆 / 按钮按下 / 压力板压下（state bit0）
    if (b == BlockRegistry::DetectorRail)
        return (st & BlockRegistry::DetectorRailStateOnFlag) ? 15 : 0; // t658 探测轨有车（bit4）
    return 0;
}

// isPowerSource 的谓词版（对外只读查询：该格是否为「当前正供能的有效电源」）。
bool World::isPowerSource(int x, int y, int z) const
{
    return powerSourceLevel(x, y, z) > 0;
}

// (x,y,z) 接收器是否被邻格供电（邻源激活 → 15；或邻粉电力级 >0）。v1 简化：全向 6 正交邻读
// （机制等价 MC 接收器 any 邻信号；无前后向输入面语义 —— MC 的 directional 接收器留后续任务）。
bool World::isReceivingPower(int x, int y, int z) const
{
    static constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &d : kNb) {
        const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        if (powerSourceLevel(nx, ny, nz) > 0) return true; // 邻源直供
        const quint8 nb = m_chunks.blockAt(nx, ny, nz);
        if (BlockRegistry::isRedstoneDust(nb)
            && (m_chunks.stateAt(nx, ny, nz) & BlockRegistry::RedstoneDustPowerMask) > 0)
            return true; // 邻粉通电
    }
    return false;
}

// t704 动力轨链深度上限（机制等价 MC 1.0 powered rail 链最长 8 根：直供种子 1 根 + 向外扩 7 根）。
//   t936 提为文件级单一来源：recomputePowerLocal 链 BFS（定深）与 notePowerWrite 放置沿重算（脏集
//   扩散半径）共用 —— 两处深度禁各写一份（review26 #1 口径漂移防线；链上限改值须两处同步生效）。
static constexpr int kGoldenRailChainMax = 8;

// t936 动力轨链邻步（单一权威，见 world.h 头注释）：视 (x,y,z) 为动力轨，解水平轴向 (ax,az) 的邻接
//   动力轨格。邻列 x/z 出界 → false；y±1 越界 blockAt 安全返 Air（ChunkManager 契约）→ 探针视无轨。
//   探针层命中非动力轨（普通 / 探测轨）→ false —— 链只在动力轨间传导（t704 语义，同层优先序不破）。
//   **t937 ① 连接位门槛**：步源格是动力轨时还须持有该轴向连接位（railConnections 写入 / mesher 形态 /
//   矿车 pickTrackStep 消费的同一物理连接权威）。旧版只看空间存在性 → 平行独立轨互不连接也被横穿
//   （用户实测「一个红石点亮两条平行轨道」）。步源非动力轨（t936 破坏沿的编辑格——破后 Air 无
//   state）不设门槛：那是入脏集的拓扑发现，点亮由重算侧本门槛把关。
bool World::goldenRailChainStep(int x, int y, int z, int ax, int az, int &nx, int &ny, int &nz) const
{
    if (m_chunks.blockAt(x, y, z) == BlockRegistry::GoldenRail) {
        const quint8 con = quint8(m_chunks.stateAt(x, y, z) & 0x0F); // 低 4 位连接位（bit4 通电位不参与）
        const quint8 need = (ax > 0) ? BlockRegistry::RailConnPx
                        : (ax < 0) ? BlockRegistry::RailConnNx
                        : (az > 0) ? BlockRegistry::RailConnPz
                                   : BlockRegistry::RailConnNz;
        if ((con & need) == 0) return false; // 本轨不朝该向连接 → 链不由此走（平行邻轨在此被挡下）
    }
    const int px = x + ax, pz = z + az;
    if (px < 0 || px >= m_width || pz < 0 || pz >= m_depth) return false;
    const int dy = BlockRegistry::railProbeDelta(
        { m_chunks.blockAt(px, y,     pz),
          m_chunks.blockAt(px, y + 1, pz),
          m_chunks.blockAt(px, y - 1, pz) });
    if (dy == INT_MIN) return false; // 该向三高皆无轨
    ny = y + dy;
    if (ny < 0 || ny >= m_height) return false;
    nx = px; nz = pz;
    return m_chunks.blockAt(nx, ny, nz) == BlockRegistry::GoldenRail;
}

// t937 ② 动力轨「链内直供种子」反向有界走查（见 world.h 头注释）：激活集纯函数的完备判据——正向 BFS
//   只能从本 pass 扫描域内的种子外扩；种子在域外（触发点落在链中段旁，如拉杆 / 灯放到亮链轨旁）时
//   旧版把「域内无种子」误判成「灭」先写暗、随后 tick 波前摸到真种子再重亮 = 用户实测「灭一下又亮」。
//   本走查从待判轨沿链（goldenRailChainStep 同一权威，含连接位门槛）走 ≤ kGoldenRailChainMax-1 步找
//   直供轨：找到 → 本轨应亮（链距 ≤7 恰是 BFS 从种子可覆盖的范围，两向同深等价）；找不到 → 灭是
//   终态（真无种子）。防环：seen 去重（环轨不死循环）。只读 m_chunks。
bool World::goldenRailChainHasFedSeed(int x, int y, int z) const
{
    if (isReceivingPower(x, y, z)) return true; // 本轨直供（调用侧多已判，防御直调）
    static constexpr int kAxFed[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    struct FCell { int x, y, z; };
    std::vector<FCell> frontier{{x, y, z}};
    std::unordered_set<quint64> seen{packGrowthCell(x, y, z)};
    for (int depth = 1; depth < kGoldenRailChainMax && !frontier.empty(); ++depth) {
        std::vector<FCell> next;
        for (const FCell &c : frontier) {
            for (const auto &a : kAxFed) {
                int nx, ny, nz;
                if (!goldenRailChainStep(c.x, c.y, c.z, a[0], a[1], nx, ny, nz)) continue;
                if (!seen.insert(packGrowthCell(nx, ny, nz)).second) continue;
                if (isReceivingPower(nx, ny, nz)) return true; // 链内 ≤7 步有直供种子 → 本轨应亮
                next.push_back({nx, ny, nz});
            }
        }
        frontier = std::move(next);
    }
    return false; // 链距 ≤7 内无直供轨 → 灭即终态
}

// t936/t942 动力轨链入脏集走查（单一权威，见 world.h 头注释）：从 (x,y,z) 出发把链上动力轨全部入
//   m_powerDirty。编辑格自身是动力轨（放置 / 置换）→ 自身入环；否则（破坏后的 Air 位——无 state 可读；
//   或 t942 源 / 粉编辑格）不设连接位门槛，先 4 轴向三高探针发现邻轨（坡链 ±1 层同收），再沿链
//   goldenRailChainStep BFS ≤ kGoldenRailChainMax 步（seen 去重，环轨不死循环）。调用方：
//   notePowerWrite（t936 轨编辑 + t942 ② 源 / 粉编辑扩位）与 recomputePowerLocal Phase A2 粉电平
//   翻转回插（t942 ③）—— 链几何判定禁第二套（goldenRailChainStep 单一权威）。
//   review0830 #2 垂直供电种子补探：isReceivingPower 按 **6 正交邻**读源，而水平 4 轴探针列恒为
//   (x±1, y-1..y+1, z) / (x, y-1..y+1, z±1)——永不含 (x, y±1, z)。源在轨正上 / 正下方（红石块顶
//   放轨的紧凑布线）且链**爬坡延伸**时，种子轨的链邻 = 编辑格 ±（轴向, ±2 层）——在三高探针窗外，
//   旧版发现步零收获 → 种子轨只经锚点 6 邻以 receivers 身份熄灭、链其余轨靠 t704 翻转波前逐 tick
//   收缩（t942 核心承诺「整链同 tick 入脏、一次 pass 全灭」的垂直版未闭合）。修 = 仅**非轨编辑格**
//   （轨链永不垂直延伸——goldenRailChainStep 只走水平轴向，轨编辑格自身入环已覆盖链上全部轨）补探
//   (x, y±1, z) 两格是否动力轨并作为 BFS 起点。
void World::dirtyGoldenRailChainFrom(int x, int y, int z)
{
    ++m_railChainWalks; // review0830 #10 探针计数（走查触发面收窄的行为级判据）
    struct RCell { int x, y, z; };
    std::vector<RCell> frontier, next;
    std::unordered_set<quint64> chainSeen;
    const auto tryStep = [&](int cx, int cy, int cz, const int (&dir)[2], std::vector<RCell> &out) {
        int sx, sy, sz;
        if (!goldenRailChainStep(cx, cy, cz, dir[0], dir[1], sx, sy, sz)) return;
        if (chainSeen.insert(packGrowthCell(sx, sy, sz)).second) out.push_back({sx, sy, sz});
    };
    // 起始环：放置 → 新轨自身入环（破坏时编辑格已非轨，跳过）；两翼 → 把编辑格当轨位探 4 轴向
    //   （破坏前的邻轨恰在这些探针层上——放置时同一步兼收既有邻轨；t942 源 / 粉编辑格同此步收
    //   其喂着的种子轨）。
    static constexpr int kAxC[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    if (m_chunks.blockAt(x, y, z) == BlockRegistry::GoldenRail
        && chainSeen.insert(packGrowthCell(x, y, z)).second) {
        frontier.push_back({x, y, z});
    }
    for (const auto &a : kAxC) tryStep(x, y, z, a, frontier);
    // review0830 #2 垂直种子补探（仅非轨编辑格；理由见函数头注释）。
    if (m_chunks.blockAt(x, y, z) != BlockRegistry::GoldenRail) {
        for (const int dy : { 1, -1 }) {
            const int sy = y + dy;
            if (sy < 0 || sy >= m_height) continue;
            if (m_chunks.blockAt(x, sy, z) == BlockRegistry::GoldenRail
                && chainSeen.insert(packGrowthCell(x, sy, z)).second)
                frontier.push_back({x, sy, z});
        }
    }
    for (int depth = 0; depth < kGoldenRailChainMax && !frontier.empty(); ++depth) {
        next.clear();
        for (const RCell &c : frontier) {
            m_powerDirty.insert(packGrowthCell(c.x, c.y, c.z)); // 链轨入脏集 → 下 tick 锚点化
            for (const auto &a : kAxC) tryStep(c.x, c.y, c.z, a, next);
        }
        frontier.swap(next);
    }
}

// 编辑路径电力脏标记（挂 4/5 参数 setBlock / setWaterSilent / clearBlockSilent 末尾，同 checkRailOnEdit
// 收口模式）：本格属红石族（粉 / 源 / 接收器）→ 本格入脏集；否则查 6 邻**粉 / 电源格**才继续（全无 →
// 本次编辑与电力无关，no-op；t937 ② 收窄，见快路径注释）。t706：旧版只查邻粉——破掉「源 | 石 | TNT」
// 中间的石块这类无粉场景不入脏集 → 邻源电力永不复算 = 用户实测「红石块 / 火把点不着 TNT」的可达性
// 缺口之一；扩到粉 + 电源后惰性块编辑仍能唤醒两侧电路。
void World::notePowerWrite(int x, int y, int z, quint8 oldId, quint8 newId)
{
    if (!isPowerFamilyBlock(oldId) && !isPowerFamilyBlock(newId)) {
        // 快路径（t937 ② 收窄）：本格前后都非红石族 → 只有当邻格是**电力读数的输入端**（粉 / 电源）才
        //   可能受影响。接收器（动力轨 / 灯 / TNT / 铁门……）读的是邻源 / 邻粉——普通方块与空气在
        //   powerSourceLevel / isRedstoneDust 读数里同为 0 → 纯接收器旁的普通编辑（轨旁放 / 挖土石、
        //   t930 级联沙逐格着地）不改变任何电力读数，须 no-op 早退。旧版「任意红石族邻格」把接收器也
        //   算上 → 触发面扩大到一切邻轨 / 邻灯编辑 = 用户实测重算风暴 + 扫描域无种子的先误熄再波前
        //   重亮两拍闪烁；收窄后这些编辑零红石重算。粉 / 电源邻保留触发：t706 可达性、粉失撑掉落的
        //   邻粉入脏集等路径语义不变（接收器 / 源本体的失撑掉落走各自编辑写——oldId 属红石族，天然
        //   慢路径触发）。
        static constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        bool anyPower = false;
        for (const auto &d : kNb) {
            const quint8 nb = m_chunks.blockAt(x + d[0], y + d[1], z + d[2]);
            if (BlockRegistry::isRedstoneDust(nb) || isPowerEmitterBlock(nb)) { anyPower = true; break; }
        }
        if (!anyPower) return;
    }
    m_powerDirty.insert(packGrowthCell(x, y, z)); // 编辑格入脏集（tickRedstone 从此锚点展开）
    // 6 邻中的粉格也入脏集（邻粉电力可能因本编辑变化——源被放 / 破、粉被断路等）。
    static constexpr int kNb2[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &d : kNb2) {
        const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
        if (BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz)))
            m_powerDirty.insert(packGrowthCell(nx, ny, nz));
    }
    // t740 火把斜下环粉入脏集（同上可达性动机）：t740 起火把经 4 个斜下格喂粉，但拆 / 放火把的
    //   编辑锚点经 6 正交种子够不到斜角粉 → 环粉保留陈旧电力（拆火把后灯不灭——降沿失达，t706
    //   同类）。仅火把编辑需要（火把 ↔ 斜下粉是唯一的斜角供电关系）。
    if (oldId == BlockRegistry::RedstoneTorch || newId == BlockRegistry::RedstoneTorch) {
        // review27 #23：火把**被拆**（oldId==Torch）→ 同格 burnout 侧表摘键——MC 拆火把重放即重置熔断。
        //   旧版唯一摘表路径是到期（tickTorchBurnout），拆 / 重放残留两缺口：(a) 计数窗内拆后同格重放 →
        //   新火把继承 flips（可能不到 8 翻就熔断）；(b) 冷却锁定期内拆后重放 → 锁定门 continue 跳过新火把
        //   评估，基座已供电也错误亮到冷却到期（最长 8s）。失撑自拆（recheckAttachmentsAfterClear 走
        //   notePowerWrite(Torch→Air)）同经本口摘键 = 语义一致；熔断翻转自身是静默直写不经本口，不受影响。
        if (oldId == BlockRegistry::RedstoneTorch)
            m_torchBurnout.erase(packGrowthCell(x, y, z));
        static constexpr int kDiag[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (const auto &h : kDiag) {
            const int nx = x + h[0], ny = y - 1, nz = z + h[1];
            if (ny >= 0 && BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz)))
                m_powerDirty.insert(packGrowthCell(nx, ny, nz));
        }
    }
    // t936 动力轨放置 / 破坏沿重算 + t942 ② 源 / 粉编辑扩位（顺序无关不变量：激活集 = 世界布局的纯函数，
    //   与摆放先后无关）。t936 根因：recomputePowerLocal 的接收器扫描域 = 锚点 + 其 **6 正交邻**，而坡链
    //   相邻轨是斜角（轴向 ±1 层，t910 几何）→ 后放的动力轨虽贴着已通电链，pass 内 receivers 够不到链上
    //   ≤8 格外的直供种子 → 通电位恒判灭 = 用户实测「先放上坡动力轨再激活一段，后放的不被激活（要全部
    //   摆好再激活才行）」。对称缺口同根：破坡链中段轨 → 远翼轨不在任何 6 正交扫描域 → 残留通电位永不熄。
    //   修法（lessons「探测域必须与写入侧权威一致」）：编辑动力轨（放 / 破 / 置换）→ 从编辑格沿链走
    //   kGoldenRailChainMax 步，沿途动力轨全部入脏集 → 下 tick 它们成为锚点，链上直供轨（若有）进
    //   receivers 成种子，t704/t910 BFS 从**真种子**定深重亮 / 收缩。影响半径：轨 R 因编辑 E 翻转必链距
    //   ≤ 上限（翻亮 = 存在种子 P 使 d(R,P) ≤ 上限-1 且路径经 E → d(R,E) ≤ 上限-1；断链翻暗同理）→
    //   定深走查全覆盖。代价：每次动力轨编辑一次性 ≤4 向 ×8 深有限走（seen 去重，环轨不死循环），编辑
    //   路径非热路径。
    //   t942 ② 源 / 粉编辑同走查（用户实测「TNT/苦力怕炸掉红石块 / 火把后部分动力轨仍激活」）：源 / 粉
    //   是链的电平输入端，其编辑（含 destroySphereSilent 批量逐格补 note 的爆炸路径）翻转种子轨通电位时
    //   旧版只把编辑格 + 邻粉入脏集——种子轨经锚点 6 正交邻入 receivers 虽会翻转，但链其余轨只能靠
    //   翻转波前逐 tick 收缩（8 根链 ~5 tick），这段「链尾仍亮」窗口即用户所见的激活残留。扩位后从源 /
    //   粉编辑格同走链（编辑格非轨 → 无连接位门槛，三高探针发现其贴邻种子轨，t936 破坏沿同口径）→
    //   整链同 tick 入脏集、下一 tick 一次 pass 全灭 / 全亮（升沿本就一次 pass——t937 ② goldenPowered
    //   并入写集；降沿对齐同语义）。快路径（t937 ②）不受扰：普通方块编辑仍在收窄谓词处早退，本走查
    //   只在慢路径（红石族编辑）后追加。
    if (oldId == BlockRegistry::GoldenRail || newId == BlockRegistry::GoldenRail
        || isPowerEmitterBlock(oldId) || isPowerEmitterBlock(newId)
        || BlockRegistry::isRedstoneDust(oldId) || BlockRegistry::isRedstoneDust(newId)) {
        dirtyGoldenRailChainFrom(x, y, z);
    }
}

// review26 #6 火把 burnout 参数（机制近似 MC 红石火把熔断；MC 原值：60s 窗内 8 次翻转 → 熄 160 game
//   tick = 8s。本工程红石 tick 10Hz → 秒值按 tick 数折算，语义钉死）：确定性整数计数（PLAN §2-K）。
static constexpr int kTorchBurnoutFlipLimit     = 8;   // 窗内翻转达 8 次 → 熔断（MC：8 toggles）
static constexpr int kTorchBurnoutWindowTicks   = 600; // 计数窗 60s（deadline 自窗内首翻起算，不逐翻刷新）
static constexpr int kTorchBurnoutCooldownTicks = 80;  // 熔断冷却 8s（MC 160 game tick @20Hz；10Hz 折 80）

// review26 #6 火把 burnout 计时（tickRedstone 头段调）：冷却 / 计数窗逐红石 tick 递减。独立于 m_powerDirty
//   早退 —— 熔断锁定后火把稳定熄灭、周围粉失电收敛，火把格**脱离脏集**，冷却只能在此处全局走（否则锁
//   死永远不解）。冷却归零 → 摘表 + 火把格入脏集（同 tick 的 recompute 重评 → attach 失电即重亮，电路若
//   仍在振荡则再累积 8 翻再熔断 = MC「冷却后可再振荡」）；窗归零 → flips 清零摘表（慢电路每翻间隔 > 窗
//   永不累积到熔断）。摘表后条目生命周期 ≤ 窗+冷却（600+80 tick），不随火把数累积。
void World::tickTorchBurnout()
{
    if (m_torchBurnout.empty()) return; // 稳态零开销（同 tickRedstone 早退口径）
    for (auto it = m_torchBurnout.begin(); it != m_torchBurnout.end(); ) {
        TorchBurnout &bo = it->second;
        bool expire = false;
        if (bo.cooldownTicks > 0) {
            --bo.cooldownTicks;
            if (bo.cooldownTicks == 0) { // 解锁：格入脏集重评（火把可能已被玩家拆走 → 重算 no-op 无害）
                m_powerDirty.insert(it->first);
                expire = true;
            }
        } else if (bo.windowTicks > 0) {
            --bo.windowTicks;
            if (bo.windowTicks == 0) { bo.flips = 0; expire = true; } // 窗到期 → 计数作废
        } else {
            expire = true; // 空条目（防御；正常路径摘表时已清）
        }
        if (expire) {
            it = m_torchBurnout.erase(it);
        } else {
            ++it;
        }
    }
}

// 电力局部重算核心（tickRedstone 消费 m_powerDirty）：两阶段——
//   Phase A（粉传播）：从脏锚点 BFS 收集连通粉域（上界 kPowerFloodCap）。域内每粉电力 = 16 - 距最近
//     **活跃源**的线距（t707 源连通距离 BFS：6 正交邻粉 + t702 爬墙斜角粉各算一跳，源直供邻格 15，
//     每经一粉 -1，距 >15 视为不达 → 0）。连接位 = 水平 4 向邻粉 + 爬墙斜角即置（6 向含上下爬墙——
//     v1 简化，MC 需台阶引导）。state 变化才静默写（防无谓 worldChanged）。
//     t707 修正：旧 t692 双缓冲快照（读邻粉 snap-1）在去源时产生回声振荡（邻源格读到远端陈旧高值
//     回喂 → 整条线 ~3s 才全暗且逐格闪烁，即用户实测「压力板松开延迟灭 / 断续」）；BFS 距离精确解
//     升 / 降沿均一次收敛，无回声 / 无振荡（机制等价 MC 导线通断即时贯通）。
//   Phase B（接收器 + 反相火把）：扫域内粉的 6 邻接收器 + 脏锚点自身接收器：通电态翻转（红石灯 bit0 /
//     动力轨 bit4）；TNT / 发射器 / 投掷器通电上升沿 → 发触发信号（信号由呈现层消费，本层不 spawn）。
//     红石火把反相（t657）：扫域内火把，其附着格（torchAttachOffset 解码）被供电（isReceivingPower）→
//     置熄灭位；附着格失电 → 清熄灭位重亮（经 m_powerDirty 再入集让下 tick 传播其供能变化）。
// 返回是否有 state 写入 / 信号（caller 收口 1 次 worldChanged + clearAllDirty）。
bool World::recomputePowerLocal()
{
    if (m_powerDirty.empty()) return false;
    ++m_powerRecomputePasses; // t937 ②：探针判据（普通编辑零重算 = 计数不动；见 powerRecomputePasses 头注释）
    // 快照脏锚点（BFS 展开中可能再入集 —— 火把重亮 / 粉电力变化级联；快照后清集，新入集留下一 tick）。
    std::vector<quint64> anchors(m_powerDirty.begin(), m_powerDirty.end());
    m_powerDirty.clear();

    static constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const int W = m_width, H = m_height, D = m_depth;

    // Phase A：BFS 收集连通粉域（从各锚点出发；锚点自身或 6 邻可达的粉全部入域）。
    std::unordered_set<quint64> region;   // 粉连通域（含锚点粉 + 其传播可达的全部粉）
    std::vector<quint64> frontier;        // BFS 队列
    const auto inBounds = [&](int x, int y, int z) { return x >= 0 && x < W && y >= 0 && y < H && z >= 0 && z < D; };
    const auto seedDust = [&](int x, int y, int z) {
        if (!inBounds(x, y, z)) return;
        if (!BlockRegistry::isRedstoneDust(m_chunks.blockAt(x, y, z))) return;
        const quint64 k = packGrowthCell(x, y, z);
        if (region.insert(k).second) frontier.push_back(k);
    };
    for (const quint64 a : anchors) {
        int ax, ay, az;
        unpackGrowthCell(a, ax, ay, az);
        seedDust(ax, ay, az); // 锚点自身是粉
        for (const auto &d : kNb) seedDust(ax + d[0], ay + d[1], az + d[2]); // 锚点邻粉（源旁粉 / 断路边粉）
    }
    // BFS 展开（粉 6 向互连 + t702 爬墙斜角互连：水平邻的 y±1 有粉 = 1 格台阶爬墙连接，连接位 /
    //   电力互通都视该对为邻 → BFS 亦沿斜角展开，粉域覆盖爬墙链整体）。
    static constexpr int kHDir2[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (size_t qi = 0; qi < frontier.size() && int(region.size()) < kPowerFloodCap; ++qi) {
        int fx, fy, fz;
        unpackGrowthCell(frontier[qi], fx, fy, fz);
        for (const auto &d : kNb) {
            if (int(region.size()) >= kPowerFloodCap) break;
            seedDust(fx + d[0], fy + d[1], fz + d[2]);
        }
        for (const auto &h : kHDir2) {
            if (int(region.size()) >= kPowerFloodCap) break;
            seedDust(fx + h[0], fy + 1, fz + h[1]); // 爬墙斜角（上 / 下一格）
            seedDust(fx + h[0], fy - 1, fz + h[1]);
        }
    }

    bool any = false;                 // 有实际 state 写入 / 触发信号
    // t707 电力模型改为「源连通距离 BFS」（替代 t692 双缓冲快照「读邻粉 snap-1」）。去源（OFF）根因：
    //   相位 A 已把整条连通粉路全量入域，release 后邻源格仍读到远端邻粉的**陈旧 snap** → 15 降到 13 而非 0；
    //   相邻格交错回喂（回声）→ 整条线每 ~2 tick 才降 1 级、最长 ~3s 才全暗且逐格闪烁 = 用户实测「压力板
    //   松开红石延迟灭 / 断续」。BFS 距离模型无递归依赖：电力 = 16 - 距最近**活跃源**的线距（源直供邻格 15、
    //   每经一粉 -1、爬墙斜角同样算一跳），升 / 降沿均一次 BFS 算定整域——去源 → 无活跃源 → 全域瞬时 0
    //   （无回声 / 无振荡；机制等价 MC 导线通断即时贯通）。传播语义不变（15 格衰减 / 爬墙 / 源直供 15），
    //   仅把「迭代收敛」换成「图距离精确解」。稳态（无写入）不再入脏集 → 零开销。
    std::unordered_map<quint64, int> dist; // 域内粉 → 距最近活跃源的线距（1..15；缺省 = 不达 → 电力 0）
    dist.reserve(region.size() * 2);
    std::vector<quint64> bfsq;            // 距离 BFS 队列
    // 播种：域内粉的 6 正交邻有活跃源（源开关是外部驱动——写 state / 改 id 已落地，实时读无需快照）→ 距 1。
    //   源经斜角不供能（同 isReceivingPower / 旧读法只查 6 正交邻）。
    for (const quint64 k : region) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        bool seeded = false;
        for (const auto &d : kNb) {
            if (powerSourceLevel(x + d[0], y + d[1], z + d[2]) > 0) {
                seeded = true;
                break;
            }
        }
        // t740 火把斜下供粉（机制等价 MC 1.0：立在方块顶面的红石火把为**贴地一圈斜角粉**供 15——火把格
        //   比这些粉高一格且水平错一格，6 正交读不到 → 经典「火把立块上、地面粉环绕」布线在 v1 整圈死粉
        //   （用户实测「火把 / 粉点不着 TNT」的可达形态之一）。仅红石火把有此形态：其它源（拉杆 / 按钮 /
        //   压力板 / 探测轨）都装在自身格内、无抬升，6 正交已等价 MC；火把亮态判定与 powerSourceLevel 同
        //   源（OffFlag 位）。斜角喂粉只入 BFS 种子（粉 state 电力级），不改 isReceivingPower 的接收器读
        //   ——接收器（TNT / 灯 / …）挨着通电粉即亮，语义不变。
        //   复审 #6（2026-08-22）：仅**立式**火把（TorchFloor）有斜下供电 —— 墙上插的火把（attach 低 3 位
        //   1..4）在 MC 只强充能其附着块，不向斜对角粉直接供电；旧 seeding 不读 attach 形态 → 墙上装饰
        //   火把把墙脚一圈粉全部点亮（意外通电）。torchAttachOffset 是 attach 编码单一权威（越界 state →
        //   floor 兜底），与 Phase B 反相分支同源解码（两处立式语义一致）。
        if (!seeded && y + 1 < H) {
            for (const auto &h : kHDir2) {
                const int tx = x + h[0], ty = y + 1, tz = z + h[1];
                if (m_chunks.blockAt(tx, ty, tz) != BlockRegistry::RedstoneTorch) continue;
                int tax, tay, taz;
                BlockRegistry::torchAttachOffset(m_chunks.stateAt(tx, ty, tz), tax, tay, taz);
                if (tay != -1) continue; // 墙挂态（支撑在水平邻）→ 不 seed 斜角粉
                if ((m_chunks.stateAt(tx, ty, tz) & BlockRegistry::RedstoneTorchStateOffFlag) != 0)
                    continue; // 熄灭态（反相）不供能
                seeded = true;
                break;
            }
        }
        if (seeded) {
            dist[k] = 1;
            bfsq.push_back(k);
        }
    }
    // 外扩：每 hop +1，沿 6 正交邻粉 + 爬墙斜角粉（t702 爬墙 = 1 格台阶，同样算一跳衰减）；距 >15 视为不达。
    static constexpr int kPowerMaxDist = 15; // 15 格衰减上限（power = 16 - dist；dist=15 → 电力 1）
    for (size_t qi = 0; qi < bfsq.size(); ++qi) {
        int x, y, z;
        unpackGrowthCell(bfsq[qi], x, y, z);
        const int nd = dist[bfsq[qi]] + 1;
        if (nd > kPowerMaxDist) continue;
        const auto tryReach = [&](int nx, int ny, int nz) {
            const quint64 nk = packGrowthCell(nx, ny, nz);
            if (!region.count(nk) || dist.count(nk)) return;
            if (!BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz))) return;
            dist[nk] = nd;
            bfsq.push_back(nk);
        };
        for (const auto &d : kNb) tryReach(x + d[0], y + d[1], z + d[2]); // 6 正交邻粉
        for (const auto &h : kHDir2) {                                    // 爬墙斜角粉（上 / 下一格）
            tryReach(x + h[0], y + 1, z + h[1]);
            tryReach(x + h[0], y - 1, z + h[1]);
        }
    }
    // Phase A2：域内粉重算电力（距离精确解）+ 连接位（水平 4 向邻粉 + 爬墙斜角），静默写 state。
    for (const quint64 k : region) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        const quint8 cur = m_chunks.stateAt(x, y, z);
        quint8 conn = 0;
        for (int di = 0; di < 6; ++di) {
            const int nx = x + kNb[di][0], ny = y + kNb[di][1], nz = z + kNb[di][2];
            const quint8 nb = m_chunks.blockAt(nx, ny, nz);
            if (BlockRegistry::isRedstoneDust(nb)) {
                // review-r19.8 H1 修：连接位只存**水平 4 向**（高半字节 0x01/0x02/0x04/0x08，同铁轨
                //   连接位序）——旧 6 向 conn<<4 使 +Z/-Z（di4/5 的 0x10/0x20）溢出 8 位被截、+Y/-Y
                //   （di2/3 的 0x04/0x08）错占 Pz/Nz 位 → 沿 Z 铺粉渲染成孤立点。垂直邻粉连接不落 state
                //   （v1 渲染省略垂直画线，注释见 partialblockgeometry）；电力传播走 BFS 距离（含垂直 hop，
                //   与连接位无关，垂直供电不受影响）。
                if (di == 0)      conn |= BlockRegistry::RedstoneDustConnPx; // +X
                else if (di == 1) conn |= BlockRegistry::RedstoneDustConnNx; // -X
                else if (di == 4) conn |= BlockRegistry::RedstoneDustConnPz; // +Z
                else if (di == 5) conn |= BlockRegistry::RedstoneDustConnNz; // -Z
                // di 2/3（+Y/-Y 垂直邻粉）：不进连接位（见上）
            }
        }
        // t702 上墙连接位（机制等价 MC 1.0 粉沿 1 格台阶爬墙）：水平 4 向的**上 / 下一格**有粉 → 该向也
        //   置连接位（渲染画向该向的爬坡斜线，同铁轨 t667 坡向语义）。
        //   review26 #15（登记不修，知情近似）：爬墙连接与同层连接**共用同一组 4 个水平连接位**（state 高
        //   半字节仅 4 位，无空闲位存「坡向」标记）→ 消费端若把连接位当「同层有臂」读，坡上形态与平地不一致。
        //   已知影响面：redstoneDustPowersNeighbor 的「臂轴延长端」形状判定（火把基座环）会把爬坡斜角粉当
        //   同层臂 —— 粉沿台阶爬坡且一端贴火把基座时，坡上拐角可能被误判贯穿直线（坡上 NOT 门行为与平地
        //   不一致；布局罕见）。修法需另设坡向位（位不够）或判定时回读邻格 y±1 几何（每判定两格 world 读，
        //   热路径代价）—— 权衡后登记近似；渲染（爬坡斜线画对）与电力 BFS（距离模型自带 y±1 hop）不受影响。
        static constexpr int kHDir[4][3] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        static constexpr quint8 kHConnBit[4] = {
            BlockRegistry::RedstoneDustConnPx, BlockRegistry::RedstoneDustConnNx,
            BlockRegistry::RedstoneDustConnPz, BlockRegistry::RedstoneDustConnNz};
        for (int h = 0; h < 4; ++h) {
            const int nx = x + kHDir[h][0], nz = z + kHDir[h][2];
            for (const int dy : { 1, -1 }) {
                const int ny = y + dy;
                if (!inBounds(nx, ny, nz)) continue;
                if (BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz)))
                    conn |= kHConnBit[h]; // 该向有爬墙粉 → 连线（渲染画斜段）
            }
        }
        const auto it = dist.find(k);
        const int power = (it != dist.end()) ? (16 - it->second) : 0; // 距最近源 d → 16-d（d=1 → 邻源 15）
        const quint8 ns = quint8(conn << 4) | quint8(power & BlockRegistry::RedstoneDustPowerMask);
        // review0830 #10 走查口径（注释即契约）：链走查的输入端是粉的**电力位**（动力轨经
        //   isReceivingPower 读邻粉电力级；连接位只驱动渲染画线，不改任何电力读数）→ 仅电力位变化
        //   才回插整链走查；连接位单独变化（邻格放 / 拆致 conn 位翻转、电力未变）零走查——旧版挂
        //   `ns != cur` 内，连接位翻转也触发整链走查 + any（每次新建 2 vector + 1 unordered_set）。
        const quint8 oldPower = quint8(cur & BlockRegistry::RedstoneDustPowerMask);
        if (ns != cur) {
            m_chunks.setBlock(x, y, z, BlockRegistry::RedstoneDust, ns); // 静默直写 + 标脏（同 recomputeRailConnections 模式）
            // 通电翻转 → 粉微红光 7 增删 → 局部重 flood 方块光（幂等安全；断电时同检出光变）。
            recomputeLightAround(x, y, z, BlockRegistry::RedstoneDust, cur,
                                 BlockRegistry::RedstoneDust, ns);
            // 变化格回插脏集（下 tick 复查——域被 kPowerFloodCap 截断时续扩 / 火把反相级联复查）+ 其
            //   6 邻粉 + 爬墙斜角粉入集。BFS 模型下整域一次收敛，稳态不再入集 → 零开销。
            m_powerDirty.insert(k);
            for (const auto &d : kNb) {
                const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                if (inBounds(nx, ny, nz) && BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz)))
                    m_powerDirty.insert(packGrowthCell(nx, ny, nz));
            }
            for (const auto &h : kHDir2) {
                for (const int dy : { 1, -1 }) {
                    const int nx = x + h[0], ny = y + dy, nz = z + h[1];
                    if (inBounds(nx, ny, nz) && BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz)))
                        m_powerDirty.insert(packGrowthCell(nx, ny, nz));
                }
            }
            // t942 ③ 粉电平翻转 → 本粉位贴邻的动力轨链一并入脏集（暗沿一次收敛）：旧版只回插邻粉，链轨
            //   靠翻转波前逐 tick 熄灭——源—粉—轨—链布局拆源 / 断粉后链尾 ~5 tick 仍亮（「部分动力轨
            //   仍激活」的粉传形态；直接供能形态由 t942 ② 编辑侧走查覆盖，本处补幸存粉的中继形态）。
            //   走查与 notePowerWrite 同一权威（dirtyGoldenRailChainFrom 从粉位出发：粉位非轨无门槛，
            //   三高探针找贴邻种子轨）。升沿对称无害（t937 ② goldenPowered 本就一次 pass 点亮整链；多入
            //   的脏锚点重算 no-op 不写 state）。review0830 #10：仅电力位变化触发（见上方口径注释）。
            if (power != oldPower)
                dirtyGoldenRailChainFrom(x, y, z);
            any = true;
        }
    }

    // Phase B：接收器通电位 + 触发信号 + 红石火把反相。扫描域 = 粉域的 6 邻 + 脏锚点自身。
    //   去重集（一接收器可能被多粉邻接 / 既是锚点又是粉邻）。
    std::unordered_set<quint64> receivers;
    const auto addReceiver = [&](int x, int y, int z) {
        if (!inBounds(x, y, z)) return;
        const quint8 b = m_chunks.blockAt(x, y, z);
        if (BlockRegistry::isTnt(b) || BlockRegistry::isRedstoneLamp(b) || b == BlockRegistry::GoldenRail
            || BlockRegistry::isDispenser(b) || BlockRegistry::isDropper(b)
            || b == BlockRegistry::IronDoor            // t722 铁门（仅红石驱动开合；上下两格各自入集，接收器分支内同翻）
            || b == BlockRegistry::IronTrapdoor        // t723 铁活板门（仅红石驱动开合；单格）
            || b == BlockRegistry::NoteBlock           // t1028 音符盒（通电上升沿发声；bit5 通电记忆位做真沿）
            || b == BlockRegistry::Rail)               // t812 普通轨转辙器（T 交叉升沿切弯；非转辙器
                                                       //   形态分支内 no-op）
            receivers.insert(packGrowthCell(x, y, z));
    };
    for (const quint64 k : region) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        for (const auto &d : kNb) addReceiver(x + d[0], y + d[1], z + d[2]);
    }
    for (const quint64 a : anchors) {
        int x, y, z;
        unpackGrowthCell(a, x, y, z);
        addReceiver(x, y, z);
        // t657/t658：锚点的 6 邻接收器也入扫描（如：锚点旁放上电源 / 破掉中间粉 → 锚点邻的灯 / 轨 / TNT
        //   电力翻转，但锚点自身非粉 → 不经粉域邻接覆盖）。
        for (const auto &d : kNb) addReceiver(x + d[0], y + d[1], z + d[2]);
    }
    // t704 动力轨链式激活预计算（机制等价 MC 1.0 powered rail 信号沿同向链传播，链最长 8 根）：被红石块 /
    //   火把 / 粉等直接供电的轨把信号传给**同轴向**（沿轨延伸方向）相邻的动力轨，链上每根依次接力——块直接
    //   激活 1 根 → 该根向同向链传播共 ≤8 根（用户实测「红石块只激活贴邻 1 根」→ 本链补全）。实现：种子 =
    //   receivers 内 isReceivingPower 的动力轨（直供轨），沿轴向 4 邻分层 BFS，深度 < kGoldenRailChainMax；
    //   可达轨 chainPowered。期望位 = direct ∪ chain——下方 receivers 循环统一按本表写位（升 / 降沿对称：
    //   去源 → direct 消失 → chain 收缩 → 远端轨熄灭）。
    //   **t910 沿坡传播**（spec「充能扩散沿轨走向含升降」）：轴向邻不再钉同 y —— 每向先 railProbeDelta
    //   三高探针（same/up/down，与 railConnections / 矿车 pickTrackStep 同一权威）解出**该向轨几何层的
    //   高度差**，邻轨取 c.y+delta。旧版「同 y 轴向邻（信号不爬坡）」→ 上坡的动力轨链（每根 +1）一根都
    //   传不到（用户报「上坡的动力铁轨被红石激活只亮贴邻，平地却能传 8 根」）。平链 delta 恒 0 行为不变；
    //   同层优先的探针序保「平/坡并存时走平面连接」（railConnections 同款）。
    //   同 pass 同步展开（不走跨 tick 级联）：金轨通电位是渲染 / boost 语义位（powerSourceLevel 不读它
    //   → 无自反馈 / 无双缓冲快照隔离需求），BFS 定深即结果确定。
    //   链轨不互供**电力**（只传通电位）——MC 语义：动力轨链是「信号延伸器」不是电源，链轨旁的 TNT / 灯
    //   不因此点亮（powerSourceLevel 无 GoldenRail 条目，正确）。
    std::unordered_set<quint64> goldenPowered; // 动力轨格（packGrowthCell）→ 应通电（直供 ∪ 链传）
    std::unordered_set<quint64> goldenSeenAll; // 本 pass 见到的全部动力轨（含链外扩的；降沿清位用）
    {
        struct GCell { int x, y, z; };
        std::vector<GCell> frontierG;
        for (const quint64 k : receivers) {
            int x, y, z;
            unpackGrowthCell(k, x, y, z);
            if (m_chunks.blockAt(x, y, z) != BlockRegistry::GoldenRail) continue;
            goldenSeenAll.insert(k);
            if (!isReceivingPower(x, y, z)) continue; // 非直供轨：等链扩散到达
            if (goldenPowered.insert(k).second) frontierG.push_back({x, y, z});
        }
        // kGoldenRailChainMax 已提文件级（t936：与 notePowerWrite 放置沿重算共用同一深度源）。
        static constexpr int kAxial[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (int depth = 1; depth < kGoldenRailChainMax && !frontierG.empty(); ++depth) {
            std::vector<GCell> nextG;
            for (const GCell &c : frontierG) {
                for (const auto &a : kAxial) {
                    // t910：三高探针解该向轨层差（same 优先 / up / down；无轨不传）+ 邻轨须动力轨 ——
                    //   t936 收口进 goldenRailChainStep 单一权威（放置沿重算同一步，禁第二套判定）。
                    int nx, ny, nz;
                    if (!goldenRailChainStep(c.x, c.y, c.z, a[0], a[1], nx, ny, nz)) continue;
                    goldenSeenAll.insert(packGrowthCell(nx, ny, nz)); // 链外轨也记（降沿复查）
                    const quint64 nk = packGrowthCell(nx, ny, nz);
                    if (goldenPowered.insert(nk).second) nextG.push_back({nx, ny, nz});
                }
            }
            frontierG = std::move(nextG);
        }
    }
    // t937 ② 链传集并入接收器写集：种子 → BFS → 写闭环同 pass 完成（亮沿一次 pass 点亮整链）。旧版只有
    //   扫描域内（锚点 + 6 邻）的轨被写，链其余轨等 t704 波前逐 tick 点亮 = 每 tick 一整趟重算 machinery
    //   （粉域 BFS + 接收器扫描）空转——「全轨激活重算」风暴的放大器。goldenPowered 内恒为动力轨
    //   （goldenRailChainStep 过滤）→ 并入只影响动力轨分支，其余接收器类型不受扰。
    for (const quint64 k : goldenPowered) receivers.insert(k);
    for (const quint64 k : receivers) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        const quint8 b = m_chunks.blockAt(x, y, z);
        const quint8 st = m_chunks.stateAt(x, y, z);
        const bool powered = isReceivingPower(x, y, z);
        if (b == BlockRegistry::RedstoneLamp) {
            // t658 红石灯：电力驱动亮灭（state bit0 复用既有 RedstoneLampStateOnFlag —— mesher 贴图 /
            //   lightEmission 15 光照链全复用）。右键手动开关已移除（电力是唯一驱动源，机制等价 MC）。
            const bool on = (st & BlockRegistry::RedstoneLampStateOnFlag) != 0;
            if (on != powered) {
                m_chunks.setBlock(x, y, z, b, quint8(powered ? (st | BlockRegistry::RedstoneLampStateOnFlag)
                                                             : (st & quint8(~BlockRegistry::RedstoneLampStateOnFlag))));
                recomputeLightAround(x, y, z, b, st, b,
                                     quint8(powered ? (st | BlockRegistry::RedstoneLampStateOnFlag)
                                                    : (st & quint8(~BlockRegistry::RedstoneLampStateOnFlag))));
                any = true;
            }
        } else if (b == BlockRegistry::GoldenRail) {
            // t658 动力轨：通电位 bit4（GoldenRailStateOnFlag）—— mesher 换 rail_golden_on(159) 通电贴图
            //   + MinecartManager boost 读此位（通电才加速）。连接位（低 4 位）保留不动。
            //   t704：通电位 = 直供（isReceivingPower）∪ 链传（goldenPowered 预计算——同轴向邻接的已通电
            //   动力轨接力传导，链 ≤8 根）。降沿对称：goldenPowered 不含本轨且非直供 → 熄灭。
            //   t937 ②「先算后清」：欲写灭前先 goldenRailChainHasFedSeed 反向走查——链距 ≤7 内若有直供
            //   种子则本轨应亮（扫描域外的种子正向 BFS 够不到，旧版即误熄后靠波前数 tick 重亮 = 用户
            //   实测「灭一下又亮」两拍闪烁）。走查与 BFS 同深同权威 → 终态仍是布局纯函数（t936 不变量
            //   保持），只是消灭了错误中间态。仅在「廉价判据全灭且当前亮」时走查（编辑路径非热路径）。
            bool wantOn = powered || goldenPowered.count(k) > 0;
            const bool on = (st & BlockRegistry::GoldenRailStateOnFlag) != 0;
            if (!wantOn && on)
                wantOn = goldenRailChainHasFedSeed(x, y, z);
            if (on != wantOn) {
                m_chunks.setBlock(x, y, z, b, quint8(wantOn ? (st | BlockRegistry::GoldenRailStateOnFlag)
                                                             : (st & quint8(~BlockRegistry::GoldenRailStateOnFlag))));
                // t704 链波前推进：本轨位翻转 → 链邻动力轨通电位可能因此变（升：链外扩一步；降：熄灭
                //   收缩一步）→ 链邻轨入脏集，下一 tick 复查（同粉变化格回插模式；m_chunks.setBlock 静默
                //   写不经 notePowerWrite → 手动补）。稳定后不再翻转 → 不再入集 → 稳态停。
                //   t937：邻位经 goldenRailChainStep 解（三高探针 + **连接位门槛**，同一权威）——旧版
                //   空间 ±1 层直插把平行独立轨也拉进重算（横穿脏标记面）；链翻转的传播本就只沿连接走。
                static constexpr int kAx2[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (const auto &a : kAx2) {
                    int nx, ny, nz;
                    if (!goldenRailChainStep(x, y, z, a[0], a[1], nx, ny, nz)) continue;
                    m_powerDirty.insert(packGrowthCell(nx, ny, nz));
                }
                any = true;
            }
        } else if (b == BlockRegistry::IronDoor) {
            // t722 铁门：电力驱动开合（state bit2，同门族开合编码——渲染 / 碰撞经 ShapeDoor 解码，开 = 板
            //   旋 90° 贴铰链边）。上升沿开 / 下降沿关（机制等价 MC 1.0 铁门 only-redstone；徒手不开——
            //   playercontroller 门开合分支已排除 IronDoor）。**两格同翻**：本格与配对格（bit3 判上/下，
            //   上格 y-1 / 下格 y+1）都写同一开合位（配对格自身也常在 receivers 集内——粉 / 源邻接任一格
            //   都会入集，两侧写互相幂等；配对格不在集（仅本格被供）时经本分支同步翻，保两格 state 一致
            //   ——破坏联动 / 渲染读 bit3+bit2 的契约不破）。朝向位（bit[1:0]）/ 上下位（bit3）原样保留。
            //   静默写 m_chunks.setBlock（同粉 / 轨模式：标脏 + worldChanged 收口在 tickRedstone 末尾，
            //   不逐门 emit）；门开合改变 lightOpacity 吗？门恒 DoorWindowLightOpacity=0（开合都透光）→
            //   无需 recomputeLightAround。
            const bool on = (st & 4) != 0;
            if (on != powered) {
                const quint8 ns = quint8(powered ? (st | 4) : (st & quint8(~4)));
                m_chunks.setBlock(x, y, z, b, ns);
                const int py = ((st & 8) != 0) ? y - 1 : y + 1; // 配对格（上格配下 y-1 / 下格配上 y+1）
                if (inBounds(x, py, z) && m_chunks.blockAt(x, py, z) == BlockRegistry::IronDoor) {
                    const quint8 pst = m_chunks.stateAt(x, py, z);
                    // 配对格同步写本格的开合结果（其上下位 / 朝向位是自己的，只覆写 bit2）
                    m_chunks.setBlock(x, py, z, BlockRegistry::IronDoor,
                                      quint8(powered ? (pst | 4) : (pst & quint8(~4))));
                }
                any = true;
            }
        } else if (b == BlockRegistry::IronTrapdoor) {
            // t723 铁活板门：电力驱动开合（state bit0，同活板门族编码——合=0 水平薄板 / 开=1 竖直贴边）。
            //   上升沿开 / 下降沿关（机制等价 MC 1.0 iron trapdoor only-redstone；徒手不开——playercontroller
            //   活板门右键分支只认 WoodTrapdoor，IronTrapdoor 天然不进该分支）。朝向位（bit[2:1]）不写
            //   （放置恒 0 → 开门侧固定 +X 边；无手开路径朝向永不交互变化，机制等价 MC 铁活板门开门方向
            //   固定）。静默写 m_chunks.setBlock（同铁门模式：标脏 + tickRedstone 末尾 1 次 worldChanged）。
            //   lightOpacity 合 15 / 开 0 的翻转不走 recomputeLightAround（见 lightOpacity 注释——开合透光
            //   差由后续邻格光编辑自然收敛）。
            const bool on = (st & 1) != 0;
            if (on != powered) {
                m_chunks.setBlock(x, y, z, b, quint8(powered ? (st | 1) : (st & quint8(~1))));
                any = true;
            }
        } else if (b == BlockRegistry::NoteBlock) {
            // t1028 音符盒：通电**上升沿**发声一次（MC 口径：稳定通电不复响、断电再通再响）。沿检测 =
            //   state bit5 通电记忆位（RailSwitchPoweredFlag 先例——没有记忆位则每次电力复算触达都误判
            //   新升沿 = 连音振荡；TNT 的 `if(powered) emit` 不适用：TNT 点燃即清 Air 天然一次性，音符盒
            //   通电后仍在）。升沿：bit5=0 且 powered → 发 noteBlockPlayed(坐标, 音高, 音色族) + 置 bit5；
            //   降沿：只清 bit5（不发声）。音高 = state 低 5 位调音段（右键调音写入，0..24 半音）；音色族
            //   = 下方方块材质投影（BlockRegistry::noteTimbreFamily 单一权威，越界 y=0 兜底 piano）。
            //   World 层只发语义事件不出声（refactor-plan §29.4：音频走 Event 链，Main.qml →
            //   AudioManager.playNote）。bit5 写入走 m_chunks.setBlock 静默写（同门/轨模式：标脏 +
            //   tickRedstone 末尾 1 次 worldChanged；音高段原样保留）。
            const bool was = (st & BlockRegistry::NoteBlockStatePoweredFlag) != 0;
            if (powered != was) {
                m_chunks.setBlock(x, y, z, b,
                                  quint8(powered ? (st | BlockRegistry::NoteBlockStatePoweredFlag)
                                                 : (st & quint8(~BlockRegistry::NoteBlockStatePoweredFlag))));
                if (powered)
                    emit noteBlockPlayed(x, y, z, BlockRegistry::noteBlockPitch(st),
                                         int(inBounds(x, y - 1, z)
                                                 ? BlockRegistry::noteTimbreFamily(m_chunks.blockAt(x, y - 1, z))
                                                 : BlockRegistry::NoteTimbrePiano));
                any = true;
            }
        } else if (b == BlockRegistry::Rail) {
            // t812 普通轨 T 交叉转辙器（机制等价 MC 1.0 rail junction：岔尖与贯穿轴某一侧连成弯，红石
            //   切到另一侧，断电保持位置不回弹）。上升沿（bit7 通电记忆 0→1）→ railSwitchToggledState
            //   切弯（bit6 翻转 + 连接位重写；Core 单一权威，与 railConnections T 分支同布局分解）；
            //   下降沿只清 bit7 弯向保持。非转辙器形态（直 / 拐角 / 四向全连——0/1/2/4 臂）helper 返
            //   原 state → no-op（电力对普通轨仅 T 交叉生效）。连接位变化经 m_chunks.setBlock 标脏 +
            //   tickRedstone 末尾 worldChanged → mesher 象限 / 矿车 pickTrackStep 同帧读到新弯向（三
            //   消费端同源 state，t771 架构）。源覆盖 = isReceivingPower（红石块 / 火把 / 拉杆 / 按钮 /
            //   压力板 / 粉，6 正交邻）——本层一处接入全源生效。
            const bool was = (st & BlockRegistry::RailSwitchPoweredFlag) != 0;
            if (powered != was) {
                const quint8 ns = powered
                    ? BlockRegistry::railSwitchToggledState(
                          b, st, true,
                          BlockRegistry::RailProbe{ m_chunks.blockAt(x + 1, y, z),
                                                    m_chunks.blockAt(x + 1, y + 1, z),
                                                    m_chunks.blockAt(x + 1, y - 1, z) },
                          BlockRegistry::RailProbe{ m_chunks.blockAt(x - 1, y, z),
                                                    m_chunks.blockAt(x - 1, y + 1, z),
                                                    m_chunks.blockAt(x - 1, y - 1, z) },
                          BlockRegistry::RailProbe{ m_chunks.blockAt(x, y, z + 1),
                                                    m_chunks.blockAt(x, y + 1, z + 1),
                                                    m_chunks.blockAt(x, y - 1, z + 1) },
                          BlockRegistry::RailProbe{ m_chunks.blockAt(x, y, z - 1),
                                                    m_chunks.blockAt(x, y + 1, z - 1),
                                                    m_chunks.blockAt(x, y - 1, z - 1) })
                    : quint8(st & quint8(~BlockRegistry::RailSwitchPoweredFlag)); // 降沿：只清记忆，不回弹
                if (ns != st) {
                    m_chunks.setBlock(x, y, z, b, ns);
                    any = true;
                }
            }
        } else if (BlockRegistry::isTnt(b)) {
            // t658 TNT：通电**上升沿**触发一次（点燃后清 Air 由信号消费端做——同一链路防双触发）。
            if (powered) {
                // t706 可观测性：用户实测「火把 / 红石块 / 粉都点不着 TNT」——链路静态核对完整（锚点邻扫 +
                //   isReceivingPower 直供），本行让每次电力点火进日志可核（缺本行 = 脏集未及 / 信号断）。
                qInfo("vo.red: power TNT at %d,%d,%d", x, y, z);
                emit powerTntTriggered(x, y, z); // 呈现层：clearBlockSilent + spawnPrimedTnt（同机关点火链）
                any = true;
            }
        } else if (BlockRegistry::isDispenser(b) || BlockRegistry::isDropper(b)) {
            // t658 发射器 / 投掷器：电力复算触达本机器 → 发信号让消费端复检（fireDispenserAtQml 内做
            //   **真上升沿**门控——t689 修「持续通电每 2s（消费端冷却）连发」：稳定通电（如拉杆保持扳开）
            //   时本分支每个电力活动 tick 都会命中，旧 `if (powered) emit` 使信号每 tick 发 → 消费端只剩
            //   2s 冷却节流 = 连发到库存空。现信号 = 「本机器电力态可能变了」（升 / 降沿都会触达），沿检测
            //   归消费端（读 isReceivingPower 与上 tick 基线集比较，仅 unpowered→powered 转换才 fire）。
            emit powerDispenserTriggered(x, y, z); // 呈现层：fireDispenserAtQml（沿检测 + 冷却 / 朝向 / 库存复用）
            any = true;
        }
    }
    // t704 链传补写：链上动力轨可能不在 receivers 集（链可伸出脏域 ≤8 格）——goldenPowered 内未在
    //   receivers 出现的轨在此按同一语义写位。熄灭（降沿）路径：链外轨一旦不满足（direct ∪ chain），
    //   其**锚点侧轨**的位翻转经 m_chunks.setBlock 写入 → 但链外轨不在 receivers → 不会自动复查。
    //   处理：BFS 同时收集「本 pass 见过的全部动力轨」（含链外）——熄灭时它们若仍带通电位而 goldenPowered
    //   已不含 → 清位。见下方 goldenSeenAll。
    for (const quint64 k : goldenSeenAll) {
        if (receivers.count(k)) continue; // receivers 循环已处理（同一逻辑，勿双写）
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        if (m_chunks.blockAt(x, y, z) != BlockRegistry::GoldenRail) continue;
        const quint8 st = m_chunks.stateAt(x, y, z);
        const bool wantOn = isReceivingPower(x, y, z) || goldenPowered.count(k) > 0;
        const bool on = (st & BlockRegistry::GoldenRailStateOnFlag) != 0;
        if (on != wantOn) {
            m_chunks.setBlock(x, y, z, BlockRegistry::GoldenRail,
                              quint8(wantOn ? (st | BlockRegistry::GoldenRailStateOnFlag)
                                            : (st & quint8(~BlockRegistry::GoldenRailStateOnFlag))));
            // t704 链波前推进（同上 receivers 循环分支——位翻转 → 轴向邻轨入脏集下 tick 复查）。
            //   t910：邻轨含 ±1 层（坡链波前，同上）。
            static constexpr int kAx3[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (const auto &a : kAx3) {
                for (int dy = -1; dy <= 1; ++dy) {
                    const int nx = x + a[0], ny = y + dy, nz = z + a[1];
                    if (inBounds(nx, ny, nz) && m_chunks.blockAt(nx, ny, nz) == BlockRegistry::GoldenRail)
                        m_powerDirty.insert(packGrowthCell(nx, ny, nz));
                }
            }
            any = true;
        }
    }

    // Phase B2：红石火把反相（t657 NOT 门）—— 扫脏锚点自身 + 粉域 6 邻的火把：附着格被供电 → 熄灭；
    //   失电 → 重亮（重亮后其供能变化须再传播 → 火把格入脏集，下一 tick 定点迭代收尾）。
    std::unordered_set<quint64> torches;
    const auto addTorch = [&](int x, int y, int z) {
        if (!inBounds(x, y, z)) return;
        if (m_chunks.blockAt(x, y, z) == BlockRegistry::RedstoneTorch)
            torches.insert(packGrowthCell(x, y, z));
    };
    for (const quint64 k : region) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        for (const auto &d : kNb) addTorch(x + d[0], y + d[1], z + d[2]);
    }
    for (const quint64 a : anchors) {
        int x, y, z;
        unpackGrowthCell(a, x, y, z);
        addTorch(x, y, z);
        // t657：锚点 6 邻火把入扫描（附着格是被编辑块时反相复检）+ **二跳火把**（附着格是锚点的邻块时——
        //   场景：红石火把立在 B 上，拉杆 / 电源贴在 B 的另一**侧面**（拉杆格与火把格曼哈顿距 2，经 B 中转：
        //   拉杆→B→火把）。只扫一跳会漏（火把非锚点也非锚点邻格）→ 火把永不反相。二跳 = 锚点 6 邻的 6 邻
        //   （≤36 格查表，编辑路径低成本）。attachPowered 判据（附着块被供电）天然正确覆盖——本处只解决
        //   「火把进入复检集」的可达性。
        for (const auto &d : kNb) {
            const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            addTorch(nx, ny, nz);
            for (const auto &d2 : kNb) addTorch(nx + d2[0], ny + d2[1], nz + d2[2]);
        }
    }
    for (const quint64 k : torches) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        const quint8 st = m_chunks.stateAt(x, y, z);
        int ax, ay, az;
        BlockRegistry::torchAttachOffset(st, ax, ay, az);
        // t657 附着块供电判定 —— **排除火把自身**（机制等价 MC：火把不向其所附着的方块供能 —— 否则
        //   亮火把给自己的支撑供电 → 反相熄灭 → 失电重亮 → 永久振荡（自反馈）。isReceivingPowerEx 沿
        //   isReceivingPower 逻辑但跳过火把格 (x,y,z)（该火把自身）。
        // t740 基座环粉 → **t869 形状输出语义**：旧 t740 对「火把斜下 4 格的粉」整体豁免（修「灯闪 / 时亮时
        //   不亮」= 装饰环粉被火把喂亮后回灌支撑的无稳态振荡），但整体豁免把**所有**贴基座粉都判哑——
        //   粉线终止于 / 拐入支撑块的 NOT 门输入与时钟回路一并失效（用户实测「红石高频 / 无限电路上版本
        //   有、本版没了」的回归根因，2026-08-21 3686e27 引入）。MC 真语义是**形状**的：粉只向其所指
        //   （开放端）方块供电、贯穿直线的侧向不供电 → 改用 BlockRegistry::redstoneDustPowersNeighbor
        //   （连接位反推开放端）：端点 / 拐角朝块 → 供能（时钟 / NOT 门恢复）；贯穿直线贴块而过 → 不供能
        //   （t740 装饰环场景保持稳定）；源（拉杆 / 红石块…）不受形状影响照常供能。
        const bool attachPowered = [&]() {
            static constexpr int kNb2[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            const int sx = x + ax, sy = y + ay, sz = z + az;
            for (const auto &d : kNb2) {
                const int nx = sx + d[0], ny = sy + d[1], nz = sz + d[2];
                if (nx == x && ny == y && nz == z) continue; // 跳过火把自身（防自反馈振荡）
                if (powerSourceLevel(nx, ny, nz) > 0) return true; // 真实源（拉杆 / 红石块…）在任意位也照常供电
                const quint8 nb = m_chunks.blockAt(nx, ny, nz);
                if (BlockRegistry::isRedstoneDust(nb)
                    && (m_chunks.stateAt(nx, ny, nz) & BlockRegistry::RedstoneDustPowerMask) > 0
                    && BlockRegistry::redstoneDustPowersNeighbor(m_chunks.stateAt(nx, ny, nz),
                                                                 sx - nx, sz - nz)) // t869：形状输出（开放端朝块才计）
                    return true;
            }
            return false;
        }(); // 附着块被供电（含形状达的粉 / 源；不含本火把与其非所指粉）
        const bool off = (st & BlockRegistry::RedstoneTorchStateOffFlag) != 0;
        // review26 #6 burnout 锁定门：熔断冷却期内不评估不翻转（保持熄灭态——重亮被抑制；冷却到期由
        //   tickTorchBurnout 摘表 + 入脏集唤醒重评）。门在 attachPowered 翻转判定之前。
        {
            const auto boIt = m_torchBurnout.find(k);
            if (boIt != m_torchBurnout.end() && boIt->second.cooldownTicks > 0) continue;
        }
        if (attachPowered != off) {
            // review26 #6 burnout 计数：每次翻转入窗（窗内首翻开窗，deadline 不逐翻刷新——慢电路每翻
            //   间隔 > 窗则窗到期清零永不熔断；5Hz 振荡窗内累积 8 次 → 熔断锁熄 8s，冷却后可再振荡
            //   = MC 火把熔断近似）。熔断时若本次是自然熄灭向（attachPowered）照写熄灭；若是重亮向
            //   （失电重亮）则**抑制**（ns==st 不写）——两种方向都终止于「锁定熄灭」。
            TorchBurnout &bo = m_torchBurnout[k]; // 引用稳（unordered_map 插入不失效引用）
            if (bo.windowTicks == 0) {
                bo.flips = 0; // 新窗首翻（上一窗已到期 / 首次）
                bo.windowTicks = quint16(kTorchBurnoutWindowTicks);
            }
            ++bo.flips;
            const bool burnout = bo.flips >= kTorchBurnoutFlipLimit;
            if (burnout) {
                bo.cooldownTicks = quint16(kTorchBurnoutCooldownTicks);
                bo.flips = 0;
                bo.windowTicks = 0;
            }
            // 供电 → 置熄灭位；失电 → 清熄灭位重亮（burnout 抑制重亮）。附着位（低 3 位）不动。
            const quint8 ns = (burnout || attachPowered)
                                  ? quint8(st | BlockRegistry::RedstoneTorchStateOffFlag)
                                  : quint8(st & quint8(~BlockRegistry::RedstoneTorchStateOffFlag));
            if (ns != st) { // 熔断抑制重亮时无写入（状态保持 + 不触发传播；burnout 表自身已在计时）
                m_chunks.setBlock(x, y, z, BlockRegistry::RedstoneTorch, ns);
                recomputeLightAround(x, y, z, BlockRegistry::RedstoneTorch, st, BlockRegistry::RedstoneTorch, ns);
                // 火把供能变化（15↔0）→ 其 6 邻粉须重算 → 火把格重入脏集（下一 tick 传播；定点迭代）。
                m_powerDirty.insert(k);
                // 审查 #2（t740 降沿失达补口）：本翻转分支是静默直写（m_chunks.setBlock），不经 notePowerWrite
                //   的火把斜下环入脏集；而 Phase A 锚点展开只播 6 正交种子，(±1,-1,0)/(0,-1,±1) 斜角粉从火把格
                //   出发永不可达 → 熄灭 / 重亮两方向环粉都保留陈旧电力（NOT 门灯恒亮 / TNT 假信号，直到该粉
                //   线被任意其它编辑触碰）。修法与 notePowerWrite 的 kDiag 环入脏集同表（kHDir2）同判定：翻转
                //   时把 4 个斜下粉一并入脏集，下一 tick 锚点自身是粉即入域重算（升 / 降沿各收敛一次）。
                //   复审 #6：与供电 seeding 同步只对**立式**火把（ay==-1）扩脏集 —— 墙挂火把与斜下粉无供电
                //   关系（seeding 已拒），翻转时不再把无关斜角粉拉入重算（两处立式语义保持一致；notePowerWrite
                //   的编辑路径扩集是可达性超集（拆火把时旧 attach state 不可得），重算幂等无害）。
                if (ay == -1) {
                    for (const auto &h : kHDir2) {
                        const int nx = x + h[0], ny = y - 1, nz = z + h[1];
                        if (ny >= 0 && BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz)))
                            m_powerDirty.insert(packGrowthCell(nx, ny, nz));
                    }
                }
                any = true;
            } // ns != st（burnout 抑制重亮分支到此）
        }
    }
    return any;
}

// t656 电力 tick（WorldClock 10Hz 桥接，同 tickWaterFlow 模式）：脏集空 → 零开销早退（稳态；普通世界 /
//   无红石电路时每 tick 仅一次判空）。脏集非空 → recomputePowerLocal 局部重算，有写入才 1 次
//   worldChanged + clearAllDirty 收口（批量收口，防每粉一次重建风暴）。传播跨多 tick 稳定（机制等价
//   MC 红石的多 tick 传播延迟；一格 100ms —— 比真实的 1 redstone tick（0.1s）恰同量级）。
void World::tickRedstone()
{
    tickTorchBurnout(); // review26 #6：burnout 计时独立于脏集早退（锁定火把脱离脏集后冷却仍要走到期）
    if (m_powerDirty.empty()) return; // 稳态零开销（lessons perf-fluid-scan：无红石场景不扫描）
    FrameProfiler::Scope prof("wRed"); // perf：红石重算计时进 w* 桶
    if (recomputePowerLocal()) {
        emit worldChanged();        // 驱动 mesh 重建（粉通断 / 灯亮灭 / 轨通电贴图切换）
        m_chunks.clearAllDirty();   // 两段重建完统一清脏（同 setBlock 末尾）
    }
}

// t305 树苗生长 tick（见 world.h 头注释）。机制等价 MC 1.0 树苗生长（random-tick 式散布概率）。
//   节流到 ~每 kSaplingTickInterval tick（5s）做一次成长判定窗口；每窗遍历全图树苗格，符合条件者按
//   确定性散布概率长成 → 清除树苗 + 在原位生成完整橡树（placeTreeAt 主干 + 树叶球冠）。
void World::tickSaplingGrowth()
{
    FrameProfiler::Scope prof("wSap"); // perf：含节流 / 早退
    if (++m_saplingTickCounter < kSaplingTickInterval) return; // 节流：每 kSaplingTickInterval tick（~5s）判一次
    m_saplingTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 1) 快照当前树苗格（tick 内栅格不变 —— 生长在 pass 末统一应用，避免半遍历态读到刚生成的树）。
    //   t425：遍历生长方格索引 m_growthCells（O(生长格数)）替代全图扫描；顺带剔除过期索引项（本 tick 末段
    //     直写 m_chunks.setBlock(Air) 清树苗不经 noteGrowthWrite → 下一窗此剔除回收之，防索引累积）。
    struct SCell { int x, y, z; int trunkH; };
    std::vector<SCell> cells;
    {
        std::vector<quint64> stale;
        for (quint64 k : m_growthCells) {
            int x, y, z;
            unpackGrowthCell(k, x, y, z);
            const quint8 b = m_chunks.blockAt(x, y, z);
            if (!isGrowthBlock(b)) { stale.push_back(k); continue; }
            if (b == BlockRegistry::Sapling)
                cells.push_back({x, y, z, 0});
        }
        for (quint64 k : stale) m_growthCells.erase(k);
    }

    // 2) 成长判定：每株据「下方草地/泥土支撑 + 头顶光照足 + 主干列畅通」筛后，按确定性散布概率决定本窗是否长成。
    //    trunkH 据哈希高位取 4..6（与 worldgen placeTrees 同范围，保长出的树与自然树一致），按世界高度钳制。
    //    主干列检查：g.y+1..g.y+trunkH+2 须畅通（树苗位 g.y 清后置原木、上方 trunkH-1 格原木 + 树冠 ~3 格空间）。
    std::vector<SCell> grows;
    for (SCell &c : cells) {
        if (c.y == 0) continue; // 世界底无下方支撑
        const quint8 below = m_chunks.blockAt(c.x, c.y - 1, c.z);
        if (below != BlockRegistry::Grass && below != BlockRegistry::Dirt)
            continue; // 须草地 / 泥土支撑（机制等价 MC 树苗需泥土/草地）
        if (m_chunks.skyLightAt(c.x, c.y, c.z) < kSaplingMinLight)
            continue; // 头顶天光不足（夜间 / 洞穴不长）
        const quint32 r = hashColumn(m_seed, c.x, c.z);
        int trunkH = 4 + int((r >> 8) % 3u); // 4..6（与 worldgen 同源：hashColumn 高位 >> 8）
        const int maxTrunk = (H - 1) - c.y - 2; // 留 2 格树冠余量后主干上限
        if (maxTrunk < 4) continue;             // 此位放不下最小树 → 不长（保留树苗，等条件；条件永不满则永不长，可接受）
        if (trunkH > maxTrunk) trunkH = maxTrunk;
        // 主干 + 树冠空间须畅通（树苗位 g.y 由 placeTreeAt 置原木，故查 g.y+1 起的 trunkH-1 + 树冠 3 格）。
        bool clear = true;
        for (int t = 1; t <= trunkH + 2; ++t) {
            if (c.y + t >= H) { clear = false; break; }
            if (m_chunks.blockAt(c.x, c.y + t, c.z) != BlockRegistry::Air) { clear = false; break; }
        }
        if (!clear) continue; // 主干列阻塞 → 此窗不长（保留树苗，下窗再试）
        // 确定性散布概率：纯函数于 seed + 位置 + 窗口序号（PLAN §2-K 精神，无 Math.random / 时间源 → 可复现）。
        const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_saplingIntervalIndex) * 0x9E3779B9u));
        const quint32 h = hashVoxel(mixedSeed, c.x, c.y, c.z);
        if (int(h & 0xFFFFu) % 100 >= kSaplingGrowPct) continue; // 散布落空 → 本窗不长
        c.trunkH = trunkH;
        grows.push_back(c);
    }

    // 3) 应用生长：清树苗（m_chunks.setBlock 静默）+ placeTreeAt 生成完整橡树（setVoxelIfAir 仅写空气格，
    //    不覆盖玩家编辑 / 已有方块）+ 局部光场重算（树干/叶遮挡改变天光）。多棵同窗长成合并一次 worldChanged。
    //    placeTreeAt(x, surfaceY=y-1, ...) → 主干从 (y-1)+1 = y 起（树苗位），向上 trunkH 格 + 树冠。
    //    leafRand = hashColumn 高位（与 worldgen 同源，驱动树冠四角叶有无 → 每棵轮廓各异）。
    bool any = false;
    for (const SCell &g : grows) {
        m_chunks.setBlock(g.x, g.y, g.z, BlockRegistry::Air); // 清树苗（静默，无 broken/placed）
        const quint32 lr = hashColumn(m_seed ^ 0x5BD1E995u, g.x, g.z); // 异或扰 leafRand 与密度字段（同 worldgen 风格）
        placeTreeAt(g.x, g.y - 1, g.z, g.trunkH, lr >> 16);
        // 局部光场重算：树主体（Log/Leaves，solid=true）从原树苗（solid=false）转 opaque → 遮光变化 → 盒内重 flood。
        //   盒半径 = 光值 15（recomputeLightAround 内部），覆盖整棵树（~5 宽、~8 高）。多棵独立调（各自盒）。
        recomputeLightAround(g.x, g.y, g.z, BlockRegistry::Sapling, BlockRegistry::Log);
        any = true;
    }
    if (any) {
        emit worldChanged(); // 触发重建（per-chunk dirty 协作：仅含新生成树的 chunk 真正重建）
        m_chunks.clearAllDirty();
        qInfo("vo.edit: saplings grew = %d", int(grows.size())); // 可观测：长成树苗计数
    }
    ++m_saplingIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同窗口不同株错峰）
}

// t514 甜浆果丛生长 tick（见 world.h 头注释）。机制等价 MC 1.0 sweet berry bush random-tick 生长。
void World::tickSweetBerryBushGrowth()
{
    FrameProfiler::Scope prof("wBerry"); // perf：含节流 / 早退
    if (++m_berryBushTickCounter < kBerryBushTickInterval) return; // 节流：每 kBerryBushTickInterval tick（~5s）判一次
    m_berryBushTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 1) 快照当前浆果丛格 + 阶段（tick 内栅格不变 —— 升阶段在 pass 末统一应用，避免半遍历态读到刚升的阶段）。
    //   t425：遍历生长方格索引 m_growthCells（O(生长格数)）替代全图扫描；顺带剔除被直接写入清掉的生长格。
    struct BCell { int x, y, z; quint8 stage; };
    std::vector<BCell> cells;
    {
        std::vector<quint64> stale;
        for (quint64 k : m_growthCells) {
            int x, y, z;
            unpackGrowthCell(k, x, y, z);
            const quint8 b = m_chunks.blockAt(x, y, z);
            if (!isGrowthBlock(b)) { stale.push_back(k); continue; } // 直接写入清掉 → 剔除过期索引项
            if (b == BlockRegistry::SweetBerryBush)
                cells.push_back({x, y, z, m_chunks.stateAt(x, y, z)});
        }
        for (quint64 k : stale) m_growthCells.erase(k);
    }

    // 2) 成长判定：每丛据「下方透光土壤支撑 + 头顶光照足 + 未成熟」筛后，按确定性散布概率决定本窗是否升阶段。
    //    散布：hashVoxel(seed, x, y, z) 混入窗口序号 m_berryBushIntervalIndex 取低 16 位 % 100，落在
    //    [0, kBerryBushGrowPct) 内即升 → 不同丛错峰、同 seed 同窗口序号同结果（无随机源，可复现）。
    //    土壤支撑：下方为 Grass / Dirt / Farmland（机制等价 MC 浆果丛生于草地 / 泥土 / 耕地；与 playercontroller
    //      种植分支 Grass/Dirt 一致 + 耕地兼容）。**t514 二轮复盘：SnowLayer 亦算有效支撑** —— worldgen
    //      placeSweetBerryBushes 把丛散布在 Snowy 群系雪顶（surfaceY=SnowLayer）正上方，雪层下方才是 Grass/Dirt
    //      （generate 把 Snowy 群系草顶替换为薄雪层）。旧版仅认 Grass/Dirt/Farmland → worldgen 雪顶丛「下方=SnowLayer」
    //      永不满足 → state 1 丛永不升到 2（采后回 0 的丛也永不重长），用户实测「丛一直是放下的阶段不往成熟长」。
    //      现 SnowLayer 视为透光支撑（机制等价 MC 寒冷群系浆果丛在覆雪地表仍生长 —— 雪层薄不阻根系、雪下仍是土）。
    std::vector<BCell> grows;
    for (const BCell &c : cells) {
        if (c.stage >= BlockRegistry::SweetBerryBushStageMax) continue;   // 已成熟 → 不再升
        if (c.y == 0) continue;                                           // 世界底无「下方土壤」支撑
        const quint8 below = m_chunks.blockAt(c.x, c.y - 1, c.z);
        if (below != BlockRegistry::Grass && below != BlockRegistry::Dirt
            && below != BlockRegistry::Farmland && below != BlockRegistry::SnowLayer)
            continue;                                                     // 下方非透光土壤 / 雪层 → 不长
        if (m_chunks.skyLightAt(c.x, c.y, c.z) < kBerryBushMinLight)
            continue;                                                     // 头顶天光不足 → 不长（夜间 / 洞穴）
        // 确定性散布概率：纯函数于 seed + 位置 + 窗口序号（PLAN §2-K 精神，无 Math.random / 时间源 → 可复现）。
        //   全 int 运算避免符号转换告警（hashVoxel 参数为 int）。
        const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_berryBushIntervalIndex) * 0x9E3779B9u));
        const int hy = c.y * 7 + int(c.stage);
        const quint32 h = hashVoxel(mixedSeed, c.x, hy, c.z);
        if (int(h & 0xFFFFu) % 100 >= kBerryBushGrowPct) continue;        // 散布落空 → 本窗不升
        grows.push_back(c);
    }

    // 3) 应用升阶段（静默批量写：m_batchFluid 收口使每丛 setWaterSilent 只写栅格 + 延迟光照重算、**不**
    //    emit worldChanged / 不 clearAllDirty；末尾本窗所有改动一次性 emit + clear，把「N 丛升阶段 = N 次
    //    emit worldChanged + N×全 chunk onWorldChanged 扇出 + N×clearAllDirty」折叠为 1 次（同 tickCropGrowth /
    //    tickWaterFlow 批量收口模式）。setWaterSilent 对「无变化」早退（stage→stage+1 必有变化 → anyChange 真）。
    //    无丛可升时 grows 为空 → 零写入零 emit（稳态无开销）。
    m_batchFluid = true;
    bool anyChange = false;
    for (const BCell &g : grows)
        anyChange |= setWaterSilent(g.x, g.y, g.z, BlockRegistry::SweetBerryBush, quint8(g.stage + 1));
    m_batchFluid = false;
    flushPendingLightEdits(); // 批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (anyChange) {
        emit worldChanged();       // 一次重建（mesher 据 state 选 stage tile → 升阶段丛贴图换新）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏（同 tickCropGrowth emit→clear 顺序）
    }

    ++m_berryBushIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同窗口不同丛错峰）
}

// t791 骨粉催熟统一入口（见 world.h 头注释）。三类目标 MC 1.0 语义：作物 +2..3 阶段 / 树苗 45% 即时成树 /
//   浆果丛 +1 阶段；骰子确定性（hashVoxel(seed ⊕ 使用序号) → 可复现、同株连续使用错峰，PLAN §2-K）。
//   分层（PLAN §2）：World 层，读写 m_chunks + 发 worldChanged；不依赖 Renderer/Game/UI。
bool World::applyBonemeal(int x, int y, int z)
{
    const quint8 id = m_chunks.blockAt(x, y, z);
    const quint8 st = m_chunks.stateAt(x, y, z);
    // 确定性骰子：seed 混使用序号（每次有效使用 +1）→ 同株连续骨粉推进值不同、同 seed 同使用序列结果一致。
    //   全 int 运算避符号转换告警（hashVoxel 参数 int，同 tickCropGrowth 散布模式）。
    const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_bonemealUseIndex) * 0x9E3779B9u));
    const quint32 roll = hashVoxel(mixedSeed, x, y, z);

    // ① 未成熟作物：+2..3 阶段（钳到 WheatCropStageMax=7；三种作物共享阶段上界，blockregistry.h 注释）。
    //    写入走 5 参数 setBlock（id 不变只 state 变 → 不发 broken/placed、发 worldChanged 重建阶段贴图，
    //    同 t447 playercontroller 原路径——骨粉是玩家动作，非系统模拟，不走 setWaterSilent 批量静默路径）。
    if (id == BlockRegistry::WheatCrop || id == BlockRegistry::CarrotCrop
        || id == BlockRegistry::PotatoCrop) {
        if (st >= BlockRegistry::WheatCropStageMax) return false; // 已成熟 → 无效应不消耗（MC 同）
        //    t1030 登记口径：成熟施用二选一取「无效应不消耗」，非 MC 观感的「消耗无生长」；
        //    P-t1030a 成熟腿行为级钉死——阶段与槽计数俱不动。
        const int advance = kBonemealCropAdvanceMin
            + int((roll >> 8) % quint32(kBonemealCropAdvanceMax - kBonemealCropAdvanceMin + 1));
        const int newSt = std::min(int(st) + advance, int(BlockRegistry::WheatCropStageMax));
        setBlock(x, y, z, id, quint8(newSt));
        ++m_bonemealUseIndex; // 有效使用 → 序号 +1（喂下次骰子）
        return true;
    }

    // ② 树苗：45% 概率即时成树（概率判定非阶段推进——树苗无生长阶段，机制等价 MC 1.0 sapling bone meal）。
    //    使用即耗：判定落空 / 守卫不满足也返 true（caller 据此扣 1 骨粉，MC 1.0 对树苗使用骨粉即消耗）。
    if (id == BlockRegistry::Sapling) {
        ++m_bonemealUseIndex; // 骨粉对树苗「使用即耗」（先 +1：骰子 roll 已取本使用序号的值）
        // 守卫（同 tickSaplingGrowth，唯光照豁免——骨粉强制生长不等天光）：下方草地/泥土 + 高度容得下
        //   最小树 + 主干列畅通（trunkH 同源 hashColumn 4..6，按世界高度钳制；树冠留 2 格余量）。
        if (y == 0) return true; // 世界底无下方支撑（守卫不满足 → 不长，但骨粉已耗）
        const quint8 below = m_chunks.blockAt(x, y - 1, z);
        if (below != BlockRegistry::Grass && below != BlockRegistry::Dirt) return true;
        const quint32 r = hashColumn(m_seed, x, z);
        int trunkH = 4 + int((r >> 8) % 3u); // 4..6（与 worldgen / tickSaplingGrowth 同源：hashColumn >> 8）
        const int maxTrunk = (m_height - 1) - y - 2; // 留 2 格树冠余量后主干上限
        if (maxTrunk < 4) return true;               // 此位放不下最小树 → 永不成树（树苗保留，骨粉白耗）
        if (trunkH > maxTrunk) trunkH = maxTrunk;
        // 主干 + 树冠空间须畅通（树苗位 y 清后置原木，故查 y+1 起的 trunkH-1 + 树冠 ~3 格空间）。
        bool clear = true;
        for (int t = 1; t <= trunkH + 2; ++t) {
            if (m_chunks.blockAt(x, y + t, z) != BlockRegistry::Air) { clear = false; break; }
        }
        if (!clear) return true; // 主干列阻塞 → 不长（树苗保留，骨粉白耗；MC 同——空间不足不成树）
        // 45% 判定（roll 低 16 位 % 100 < kBonemealSaplingPct，同 tickSaplingGrowth 散布判式）。
        if (int(roll & 0xFFFFu) % 100 < kBonemealSaplingPct) {
            // 长成：清树苗（静默，无 broken/placed）+ placeTreeAt 完整橡树 + 局部光场重算 + worldChanged
            //   （逐句同 tickSaplingGrowth 应用段；setVoxelIfAir 仅写空气格不覆盖玩家编辑）。
            m_chunks.setBlock(x, y, z, BlockRegistry::Air);
            const quint32 lr = hashColumn(m_seed ^ 0x5BD1E995u, x, z); // 异或扰 leafRand 与密度字段（同 tickSaplingGrowth）
            placeTreeAt(x, y - 1, z, trunkH, lr >> 16);
            // 局部光场重算：树主体（Log/Leaves，solid=true）从原树苗（solid=false）转 opaque → 遮光变化 → 盒内重 flood。
            recomputeLightAround(x, y, z, BlockRegistry::Sapling, BlockRegistry::Log);
            emit worldChanged();      // 触发重建（per-chunk dirty 协作：仅含新生成树的 chunk 真正重建）
            m_chunks.clearAllDirty(); // 重建完统一清脏（同 tickSaplingGrowth emit→clear 顺序）
        }
        return true;
    }

    // ③ 未成熟浆果丛：+1 阶段（丛仅 3 阶段 0/1/2，一骨粉推一阶段；MC sweet berry bush bone meal 单阶段推进）。
    if (id == BlockRegistry::SweetBerryBush) {
        if (st >= BlockRegistry::SweetBerryBushStageMax) return false; // 已成熟 → 无效应不消耗（MC 同）
        setBlock(x, y, z, id, quint8(st + 1)); // id 不变 + state+1（同 ① 写入路径，驱动阶段贴图重建）
        ++m_bonemealUseIndex;
        return true;
    }

    return false; // 非三类目标 → 无效应不消耗（机制等价 MC 骨粉对非生长目标无效应）
}

// --- Perlin（2D fBm）---
static double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
static double lerp(double a, double b, double t) { return a + t * (b - a); }
static double grad2(int hash, double x, double z)
{
    int h = hash & 7;
    double u = h < 4 ? x : z;
    double v = h < 4 ? z : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
}
// t278 3D Perlin 梯度（与 grad2 同源；hash 低 4 位选 12 个 3D 梯度方向之一，标准 Perlin grad3）。
//   供 noise3 用，洞穴 carve 的 3D 噪声场。
static double grad3(int hash, double x, double y, double z)
{
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

void World::buildPermutation()
{
    // 置换表（线性同余 RNG，可复现；同 seed → 同表 → 同高度图）
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

// t278 3D Perlin 噪声（机制等价标准 Perlin 3D；洞穴 carve 的 3D 标量场）。复用 noise2 的 fade/lerp/m_perm。
//   索引链 m_perm[X]+Y → m_perm[..]+Z 与 noise2 同模式（m_perm 512 项，中间索引 ≤510、+1 ≤511 安全）。
//   纯函数于 seed（m_perm 由 buildPermutation 派生于 seed）→ 同 seed 同 3D 噪声场（PLAN §2-K）。范围 ~[-1,1]。
double World::noise3(double x, double y, double z) const
{
    const int X = int(std::floor(x)) & 255;
    const int Y = int(std::floor(y)) & 255;
    const int Z = int(std::floor(z)) & 255;
    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);
    const double u = fade(x), v = fade(y), w = fade(z);
    const int A  = m_perm[X]     + Y;
    const int AA = m_perm[A]     + Z;
    const int AB = m_perm[A + 1] + Z;
    const int B  = m_perm[X + 1] + Y;
    const int BA = m_perm[B]     + Z;
    const int BB = m_perm[B + 1] + Z;
    const double x1 = x - 1.0;
    const double y1 = y - 1.0;
    const double z1 = z - 1.0;
    return lerp(
        lerp(
            lerp(grad3(m_perm[AA],     x,  y,  z ), grad3(m_perm[BA],     x1, y,  z ), u),
            lerp(grad3(m_perm[AB],     x,  y1, z ), grad3(m_perm[BB],     x1, y1, z ), u),
            v),
        lerp(
            lerp(grad3(m_perm[AA + 1], x,  y,  z1), grad3(m_perm[BA + 1], x1, y,  z1), u),
            lerp(grad3(m_perm[AB + 1], x,  y1, z1), grad3(m_perm[BB + 1], x1, y1, z1), u),
            v),
        w);
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
    // t119：高度由 16 重定标到 64（地表抬升、留出基岩底层 + 更厚石层 + 更高天空间）。
    // 原 7+n*4（地表 ~3..11）→ 28+n*12（地表 ~16..40）：基岩层 y 0..4 在地表之下，石层 12..36
    // 厚度（散布矿石有空间），天空间 24..48（树 / 飞行）。同 seed 仍确定（fbm 纯函数）。
    // t162：振幅 12→8（用户「太陡太过于陡峭」→ 更平缓少陡山），基线 28→30 → 地表 ~22..38。
    //   水位 24 仍相交（~22..24 低洼列见水），沙滩带 / 树·矿石阈值（waterLevel+1=25）同步成立。
    // t274：群系分流 + 整体振幅降低（用户「现纯山地凹凸不平 → 大草原平地、山地仅特定群系」）。
    //   每个群系独立振幅 —— plains 极平（amp 2，多数陆地）、hills 起伏保留山地感（amp 7，少数），
    //   desert 平缓沙丘（amp 3）。共用基线 30 → 群系边界高差有界（最坏 plains↔hills ≈7 格，低频
    //   群系边界稀少），无浮空 / 悬崖。水位 24：plains(28..32)/desert(27..33) 恒高于水 → 草原 / 沙漠
    //   主体无水；hills(23..37) 低洼列见水 / 沙滩带（waterLevel+1=25 阈值仍成立）。同 seed 确定
    //   （fbm + biomeAt 均纯函数，PLAN §2-K）。机制等价 MC 1.0 群系化高度图（plains 平 / hills 起伏 /
    //   desert 沙，spec 原意）。
    // t307：地表整体抬高 —— 基线 30→64（用户「现地表 ~30，计划 ~64+，至少地面 64 格左右」），
    //   振幅沿 t162/t274 用户已调定的平缓值（plains/forest 2、hills 7、desert 3）不动（避免回退用户
    //   反复要求的「大草原平地」）。结果地表：plains/forest ~62..66、desert ~61..67、hills ~57..71
    //   （地表中位 ~64，满足「地面 64 格」）。世界高度同步 64→128（Main.qml）留出树冠（地表+~10）+
    //   天空间 / 飞行。水位 24→58 同源抬高（保持「低于基线 6 格」→ 低洼 hills 仍见水 / 沙滩带，
    //   比例同 t162/t274）；沙滩带 / 树·矿石阈值（waterLevel+1=59）同步成立。同 seed 确定（fbm +
    //   biomeAt 纯函数，PLAN §2-K）。
    const double n = fbm((x + m_seed) * 0.09, (z + m_seed) * 0.09); // [-1,1]
    double amp;
    switch (biomeAt(x, z)) {
        case Biome::Hills:  amp = 7.0; break; // 起伏（山地感，仅此群系有显著地形变化）
        case Biome::Desert: amp = 3.0; break; // 平缓沙丘
        case Biome::Forest: amp = 5.0; break; // 森林（t341）：amp 2→5 起伏（用户「森林要更起伏」→ 产山坡供洞口贴附；
                                             //   不再与 plains 同振幅 → 森林/草原边界有小幅高差，但低于 hills amp 7，
                                             //   保持「大草原平地」仍成立）。t306 原 amp 2（与 plains 同）已废。
        case Biome::Snowy:  amp = 3.0; break; // 雪原/针叶（t395）：平缓起伏（介于 plains 2 与 desert 3 之间；覆雪地表
                                             //   宜平缓，少悬崖；机制等价 MC 1.0 雪原 / 针叶平缓地形）。
        case Biome::Swamp:  amp = 0.0; break; // 沼泽（t396）：**完美平坦**（amp 0 → 全 Swamp 列等高于基线 64）。
                                             //   平坦是浅水池稳态的前提 —— placeSwampPools 把约半数草顶改造成 1 格深
                                             //   Water 源，全列等高 → 水源层水平邻接同高草岛（Grass）→ 不溢流（机制等价
                                             //   MC 1.0 沼泽平地 + 浅水洼地貌；非 MC 沼泽的微起伏，本工程取严格平坦保水源稳定）。
        case Biome::Jungle: amp = 5.0; break; // 丛林（t481/t486 前置）：**略高于平原、同森林级**（spec「丛林振幅略高于
                                             //   平原、同森林级」→ amp 5 与 Forest 同级；温热湿润低地轻微起伏，供高树
                                             //   扎根 + 与森林边界高差小 → 无缝衔接。不取 plains 的 amp 2 —— 丛林非开阔
                                             //   草原，且与森林邻接时同振幅保边界零高差）。
        case Biome::Plains: // 草原（多数陆地）
        default:            amp = 2.0; break; // 极平（spec「大草原=平地」）
    }
    const int h = int(std::lround(64.0 + n * amp));
    return std::max(0, h);
}

// t274 群系判定（PLAN §2-K 确定性）：单一群系 fBm（频率 0.012，seed 偏移 +3571，与高度噪声 0.09、
//   旧 t117 沙漠噪声 0.018 均不同）→ 群系图与高度图 / 旧沙漠分布解耦。低频 → 大区块连续（plains/hills/
//   desert 成片，机制等价 MC 1.0 群系大尺度分布，非逐格斑点）。阈值三分（fbm 近似正态）：
//     b > 0.5  → Hills  （少数，~15-20%：起伏山地，spec「山地仅特定群系」）
//     b < -0.4 → Desert （少数，~15-20%：沙）
//     其余     → Plains （多数，~60-70%：平坦草原，spec「大草原」原意）
//   纯函数于 seed → 同 seed 同群系图。biomeAt 是群系的唯一权威：isDesert / heightAt / placeTallGrass
//   均经此读群系，保证三处判定一致（不会出现「同列 generate 判沙漠、placeTrees 判草原」的撕裂）。
// t905 perf：biomeAt = 列级 memo 入口（头注释见 world.h m_biomeCache / biomeComputeAt）。在界列走缓存：
//   未命中 → biomeComputeAt 算 fBm 并回填。越界列不走缓存（保持旧口径 —— 直接算 fBm，返回某确定群系，
//   行为与加 memo 前逐字一致）。缓存尺寸自检兜底漏清（size 不匹配 = 尺寸已变 → 整表重建）。
World::Biome World::biomeAt(int x, int z) const
{
    if (x >= 0 && z >= 0 && x < m_width && z < m_depth) {
        const size_t want = size_t(m_width) * size_t(m_depth);
        if (m_biomeCache.size() != want)
            m_biomeCache.assign(want, 0xFF); // 懒建 / 尺寸换代自愈（generate/beginLoad 亦显式清）
        quint8 &slot = m_biomeCache[size_t(x) + size_t(z) * size_t(m_width)];
        if (slot != 0xFF) return Biome(slot);
        const Biome b = biomeComputeAt(x, z);
        slot = quint8(b); // Biome 编码 0..6（< 0xFF 哨兵），见 enum class Biome
        return b;
    }
    return biomeComputeAt(x, z);
}

// t905 perf：biomeAt 的 fBm 计算本体（原 biomeAt 函数体原样迁移，零语义变化；仅由 biomeAt memo 未命中调）。
World::Biome World::biomeComputeAt(int x, int z) const
{
    const double b = fbm((x + m_seed + 3571) * 0.012, (z + m_seed + 3571) * 0.012); // [-1,1]
    if (b > 0.5)  return Biome::Hills;
    if (b < -0.4) return Biome::Desert;
    // t481/t486 前置 丛林（Jungle）：第五条独立低频 fBm（频率 0.014 + seed 偏移 +5133，与主群系图 0.012/+3571、
    //   森林图 0.020/+977、雪原图 0.016/+6420、沼泽图 0.024/+8842、高度图 0.09 均不同）→ 丛林图与五者解耦。
    //   低频 → 丛林成片（非逐格斑点，机制等价 MC 1.0 丛林大尺度分布）。**从 Forest/Plains 中分出**：同一张 j 图
    //   在森林候选带（f>0.40）内把 Jungle 从 Forest 里 carve 出、在平原剩余候选带内把 Jungle 从 Plains 里 carve 出
    //   → 丛林区域跨森林/平原连片（两处都读 j，非两次独立随机 → 边界无缝）。Hills/Desert 判定先于丛林早退、
    //   Snowy/Swamp 判定也在丛林-plains 判定之前早退 → 丛林**绝不**吞掉既有 Desert/Swamp/Snowy（spec「勿让既有
    //   Desert/Swamp/Snowy 消失」；hills 先于丛林 → 山地也保留）。阈值 kJungleBiomeThresh → 全图 ~10-20% 列成丛林
    //   （10 seed 实测均值 ~13.5%，见 kJungleBiomeThresh 旁注释）。纯函数于 seed → 同 seed 同丛林分布（PLAN §2-K）。
    constexpr double kJungleBiomeThresh = 0.25; // 实测（160×160 全域，Python 复刻同款 Perlin fBm 遍历 10 seed）：丛林
                                                //   平均 ~13.5%（seed 1337 = 13.4%、seed 42 = 15.1%），落 spec「~10-20%」中段；
                                                //   fBm 阈值单图分区随 seed 有方差（5%..20%），均值即目标带（机制等价 MC 群系面积随 seed 变）。
    const double j = fbm((x + m_seed + 5133) * 0.014, (z + m_seed + 5133) * 0.014); // [-1,1]
    // t306：原 plains 候选带（b ∈ [-0.4,0.5]）用第二条独立低频 fBm 把 forest 从草原里 carve 出来。
    //   独立频率 0.020 + seed 偏移 +977（与主群系图 0.012/+3571、高度图 0.09 均不同）→ 森林图与三者解耦；
    //   低频 → 森林成片（非逐格斑点，机制等价 MC 1.0 森林群系大尺度分布）。
    //   t373：阈值 0.15→0.40（fbm 近似正态居中 0）。旧 0.15 实测森林吞没草原（草原几乎不可见），
    //   因 4 阶 fbm 实际分布比名义 [-1,1] 收窄、0.15 已落入正半区主流段 → 森林占比偏高。提至 0.40
    //   把森林压成少数（候选带内 ~15-20%），草原重新成为大片开阔地带（spec「大草原」原意）；森林仍
    //   成片共存（spec「森林+草原」二者共存，森林不消失）。纯函数于 seed → 同 seed 同 forest/plains 划分（PLAN §2-K）。
    const double f = fbm((x + m_seed + 977) * 0.020, (z + m_seed + 977) * 0.020); // [-1,1]
    if (f > 0.40) return (j > kJungleBiomeThresh) ? Biome::Jungle : Biome::Forest; // 森林带内：丛林 fBm 高 → Jungle
    // t395 雪原/针叶群系：用第三条独立低频 fBm 把 Snowy 从草原里 carve 出来。独立频率 0.016 + seed 偏移 +6420
    //   （与主群系图 0.012/+3571、森林图 0.020/+977、高度图 0.09 均不同）→ 雪原图与四者解耦；低频 → 雪原成片
    //   （非逐格斑点，机制等价 MC 1.0 寒冷群系大尺度分布）。阈值 0.45 → 候选带内少数（~10-15%）成雪原（与沙漠 /
    //   森林同为少数群系，草原仍占多数）。纯函数于 seed → 同 seed 同雪原分布（PLAN §2-K）。
    const double s = fbm((x + m_seed + 6420) * 0.016, (z + m_seed + 6420) * 0.016); // [-1,1]
    if (s > 0.45) return Biome::Snowy;
    // t396 沼泽群系：用第四条独立低频 fBm 把 Swamp 从草原里 carve 出来。独立频率 0.024 + seed 偏移 +8842
    //   （与主群系图 0.012/+3571、森林图 0.020/+977、雪原图 0.016/+6420、高度图 0.09 均不同）→ 沼泽图与五者解耦；
    //   低频 → 沼泽成片（非逐格斑点，机制等价 MC 1.0 沼泽大尺度分布）。阈值 0.30 → 候选带内少数（~15-20%）成
    //   沼泽（略多于雪原，沼泽为本任务标志性群系；仍为少数，草原占多数）。纯函数于 seed → 同 seed 同沼泽分布（§2-K）。
    const double sw = fbm((x + m_seed + 8842) * 0.024, (z + m_seed + 8842) * 0.024); // [-1,1]
    if (sw > 0.30) return Biome::Swamp;
    return (j > kJungleBiomeThresh) ? Biome::Jungle : Biome::Plains; // 平原剩余带内：丛林 fBm 高 → Jungle（从 Plains 分出）
}

// t117/t274 沙漠群系判定：收口到 biomeAt == Desert（单一权威）。旧 t117 独立 fBm（0.018/+7919/0.35）
//   已由 t274 biomeAt 统一 —— 现沙漠分布随群系图（0.012/+3571）走，新世界生效（旧存档走 chunk blob
//   不受影响，spec 明示）。供 generate（沙表层）/ placeTrees / placeTallGrass 跳过沙漠列。
bool World::isDesert(int x, int z) const
{
    return biomeAt(x, z) == Biome::Desert;
}

// t385 天空变暗乘子（见 world.h 头注释）。雷暴最暗、雨中等、雪阴沉略暗、晴不变暗。供 QML clearColor/cloudColor。
float World::weatherDarkness() const
{
    switch (m_weather) {
        case Weather::Thunder: return 0.55f; // 雷暴：显著变暗
        case Weather::Rain:    return 0.35f; // 雨天：中等变暗（阴沉）
        case Weather::Snow:    return 0.22f; // 雪天：略暗（阴沉）
        case Weather::Clear:   return 0.0f;  // 晴：不变暗
    }
    return 0.0f; // 防御（enum 已全覆盖；-Wreturn-type 兜底）
}

// t385 局部降水类型（群系解析，见 world.h 头注释）。Clear→Clear；沙漠→Clear（永不降水）；
//   山地(Hills 冷 / 高海拔)→Snow（降水即雪）；草原 / 森林→随全局态（雨 / 雪 / 雷）。OOB 安全（biomeAt 纯函数）。
int World::weatherStateAt(int x, int z) const
{
    if (m_weather == Weather::Clear) return int(Weather::Clear);
    const Biome b = biomeAt(x, z);
    if (b == Biome::Desert) return int(Weather::Clear); // 沙漠干燥 → 永不降水（机制等价 MC 沙漠无雨 / 雪）
    if (b == Biome::Hills)  return int(Weather::Snow);  // 山地（冷）→ 降水即雪（机制等价 MC 冷群系下雪）
    if (b == Biome::Snowy)  return int(Weather::Snow);  // t395 雪原/针叶（冷）→ 降水即雪（机制等价 MC 寒冷群系下雪；spec「weather SNOWS here」）
    return int(m_weather);                               // 草原 / 森林（暖）→ 随全局态（雨 / 雪 / 雷）
}

// t385 该位置是否正降水（= weatherStateAt != Clear）。mob 灭火 / 作物浇水 / 日光燃烧门控用。
bool World::isPrecipitatingAt(int x, int z) const
{
    return weatherStateAt(x, z) != int(Weather::Clear);
}

// review27 #11 着火实体雨灭判据（见 world.h 头注释；玩家 / mob 两侧共用单一权威）。
bool World::rainExtinguishesAt(int x, int y, int z) const
{
    return skyLightAt(x, y, z) >= 15 && isPrecipitatingAt(x, z);
}

// t385 重置天气态（见 world.h 头注释）：Clear + 随机首场晴时长（偏短便于进世界即见天气）。态真翻才 emit。
//   t386：同时重置闪电计时（雷态进入时第一击的随机间隔；非雷态不递减，无副作用）。
void World::resetWeather()
{
    auto *rng = QRandomGenerator::global();
    const float dur = kInitialClearMin + float(rng->generateDouble()) * (kInitialClearMax - kInitialClearMin);
    const bool changed = (m_weather != Weather::Clear);
    m_weather = Weather::Clear;
    m_weatherTimer = dur;
    m_lightningTimer = kLightningIntervalMin + float(rng->generateDouble()) * (kLightningIntervalMax - kLightningIntervalMin);
    if (changed) emit weatherChanged();
}

// t1016 存档天气恢复（头注释见 .h）：设态 + 按该态的随机时长窗重抽剩余时长（与 tickWeather 转换时
//   设时同口径；恢复「态」不恢复「剩余秒数」——秒数不进存档，重抽 = 进世界后正常随机模拟续跑）。
//   非法 state 静默拒（保当前态，防脏档破枚举不变量）；态未翻（同态恢复）不 emit（零噪声）。
void World::setWeatherState(int state)
{
    if (state < int(Weather::Clear) || state > int(Weather::Thunder)) return;
    auto *rng = QRandomGenerator::global();
    const Weather next = Weather(state);
    if (next == Weather::Clear) {
        m_weatherTimer = kClearWeatherMin + float(rng->generateDouble()) * (kClearWeatherMax - kClearWeatherMin);
    } else {
        const float lo = (next == Weather::Thunder) ? kThunderDurMin : kWeatherDurMin;
        const float hi = (next == Weather::Thunder) ? kThunderDurMax : kWeatherDurMax;
        m_weatherTimer = lo + float(rng->generateDouble()) * (hi - lo);
    }
    const bool changed = (m_weather != next);
    m_weather = next;
    if (changed) emit weatherChanged(); // 态翻转 → 驱动 QML 天空变暗 + 粒子切换
}

// t385 天气 tick（见 world.h 头注释）。机制等价 MC 1.0 天气：晴 ↔ 降水（雨/雪/雷）随机时长转换。
void World::tickWeather(qreal dt)
{
    FrameProfiler::Scope prof("wWeath"); // perf：计时未到零开销
    if (m_weatherTimer <= 0.0f) return; // 防御（构造已设首时长；正常不触发）
    m_weatherTimer -= float(dt);
    if (m_weatherTimer > 0.0f) return;  // 计时未到 → 不转换（零开销：无写入 / 无 emit）

    auto *rng = QRandomGenerator::global();
    Weather next;
    if (m_weather == Weather::Clear) {
        // 晴 → 随机选一种降水：雨 65%（最常见）/ 雪 25% / 雷 10%（机制等价 MC 雨多于雪 / 雷）。
        const int r = int(rng->bounded(100)); // [0,100)
        if (r < 10)      next = Weather::Thunder;
        else if (r < 35) next = Weather::Snow;
        else             next = Weather::Rain;
        // 降水持续时长（雷态偏短）。
        const float lo = (next == Weather::Thunder) ? kThunderDurMin : kWeatherDurMin;
        const float hi = (next == Weather::Thunder) ? kThunderDurMax : kWeatherDurMax;
        m_weatherTimer = lo + float(rng->generateDouble()) * (hi - lo);
    } else {
        // 降水 → 晴。
        next = Weather::Clear;
        m_weatherTimer = kClearWeatherMin + float(rng->generateDouble()) * (kClearWeatherMax - kClearWeatherMin);
    }
    m_weather = next;
    emit weatherChanged(); // 态翻转 → 驱动 QML 天空变暗 + 粒子切换

    // t386 闪电：仅雷态推进。进入雷态时 m_lightningTimer 已由 resetWeather / 上一击重置为首击间隔；递减到 0 →
    //   strikeLightning（随机落点 + 引燃 + emit lightningStruck）+ 重置下一击随机间隔。非雷态不递减（零开销）。
    //   spec「雷雨天随机闪电（闪光+雷声），可点燃木/伤害实体」。机制等价 MC 1.0 雷暴期随机闪电。
    if (m_weather == Weather::Thunder) {
        m_lightningTimer -= float(dt);
        if (m_lightningTimer <= 0.0f) {
            strikeLightning();
            auto *rng = QRandomGenerator::global();
            m_lightningTimer = kLightningIntervalMin + float(rng->generateDouble()) * (kLightningIntervalMax - kLightningIntervalMin);
        }
    }
}

// t386 触发一次闪电击中（见 world.h 头注释）：随机世界内一列为落点，列顶实面为击中 y；木类方块焚毁；emit 信号。
void World::strikeLightning()
{
    auto *rng = QRandomGenerator::global();
    const int x = int(rng->bounded(m_width));   // [0, width)
    const int z = int(rng->bounded(m_depth));   // [0, depth)
    const int y = heightmapAt(x, z);            // 列顶首个实面 y（空列 → -1）
    if (y < 0) return;                          // 空列（纯海域 / 全空气上空）无可见落点 → 不发信号
    // 击中点木类方块焚毁（机制等价 MC 雷击点燃木质；无 Fire 方块 → setBlock Air 焚毁 + blockBroken 粒子/音，
    //   同 tickLavaFlow ignite pass 语义）。非木类不焚毁（仅闪光 / 雷声 / 伤害，由上层据信号消费）。
    const quint8 id = blockAt(x, y, z);
    auto isWoodLike = [](quint8 bid) -> bool {
        using BR = BlockRegistry;
        return bid == BR::Log || bid == BR::SpruceLog || bid == BR::Planks || bid == BR::CraftingTable || bid == BR::Leaves
            || bid == BR::SpruceLeaves // t714 云杉树叶（木质可燃，同橡树叶）
            || bid == BR::WoodSlab || bid == BR::WoodStairs || bid == BR::WoodFence
            || bid == BR::WoodPressurePlate || bid == BR::WoodDoor || bid == BR::WoodTrapdoor || bid == BR::Chest
            || bid == BR::SprucePlanks || bid == BR::SpruceSlab || bid == BR::SpruceFence || bid == BR::SpruceDoor; // t466 云杉木制品（木质，雷击焚毁）
    };
    if (isWoodLike(id)) {
        setBlock(x, y, z, BlockRegistry::Air); // 焚毁（发 blockBroken → 破块粒子 / 音 + worldChanged 重建）
    }
    emit lightningStruck(x, y, z); // 驱动呈现层（白闪 + playThunder）+ 实体层（mob / 玩家近击中点伤害）
}

// t338 海域角点（4 角之一；seed 派生确定性）。海域（海 + 沙滩）集中于此角，内陆无散沙 / 散水。
//   hashColumn 用固定独立坐标（与其它 worldgen hashColumn 解耦）→ 同 seed 同角（PLAN §2-K）。
void World::seaCorner(int &cx, int &cz) const
{
    const quint32 r = hashColumn(m_seed, 0x5EA1u, 0xC0A5u);
    cx = (r & 1u) ? m_width - 1 : 0;
    cz = (r & 2u) ? m_depth - 1 : 0;
}

// t338/t372 海域列高度（海 + 沙滩集中于一角）。返回：
//   -1 = 远内陆（dist 超过海域半径 + 过渡带 → 走自然 heightAt，无海沙 / 海水）
//   0..m_height-1 = 海域重塑地表 y：
//     · 沙海盘（dist <= effectiveRadius）：角点最深 seaFloor → 岸线 beachTop 缓坡（+ 高度噪声柔化）；
//       h<waterLevel 为海底由 fillWater 灌水，h==waterLevel+1 为干沙滩。表层 Sand（见 isSeaSandColumn）。
//     · 过渡带（effectiveRadius < dist <= effectiveRadius+blendWidth）：高度由 beachTop smoothstep 过渡到
//       自然 heightAt → 消除「岸线 59 ↔ 邻接森林 62-66」的悬崖（t372）；表层走自然群系草地（isSeaSandColumn=false）。
//   t372 海岸线柔化（spec「沙滩太规整」）：低频 fBm 抖动 effectiveRadius → 蜿蜒岸线（非规整圆弧）；
//   高度噪声 → 海底/沙滩微起伏（非完美平面）。纯函数于 seed + dims + heightAt（fbm）（PLAN §2-K）。
//   generate 据此重塑地形（沙底/沙滩），fillWater 仅在沙海盘（h<waterLevel）灌水，
//   placeSurfaceLakes/placeUndergroundWaterPools 据此跳过海域（避免叠湖 / 误挖海水柱）。
int World::seaColumnHeight(int x, int z) const
{
    if (m_width <= 0 || m_depth <= 0) return -1;
    int cx, cz;
    seaCorner(cx, cz);
    const int dx = x - cx, dz = z - cz;
    const double dist = std::sqrt(double(dx) * dx + double(dz) * dz);
    const int seaRadius = std::min(m_width, m_depth) * 3 / 10; // 海域半径（地图短边 30% → 一角可见海）

    // t372 岸线蜿蜒（spec「沙滩太规整」）：低频 fBm 抖动有效半径 → 自然蜿蜒岸线（非规整圆弧）。
    //   独立频率 0.07 + seed 偏移 +5331（与高度图 0.09 / 群系 0.012 均解耦）→ 同 seed 同岸线（PLAN §2-K）。
    const double shore = fbm((x + m_seed + 5331) * 0.07, (z + m_seed + 5331) * 0.07); // [-1,1]
    const double effectiveRadius = double(seaRadius) * (1.0 + 0.12 * shore);          // ±12% 蜿蜒

    constexpr int kSeaDepth = 6;                      // 角点海深（水位之下格数）
    const int seaFloor = kWaterLevel - kSeaDepth;     // 角点海底（最深）
    const int beachTop = kWaterLevel + 1;             // 岸线干沙滩（水位 +1）

    if (dist <= effectiveRadius) {
        // 沙海盘（海盆 + 干沙滩）：缓坡 + 高度噪声（柔化规整线性坡）。
        //   高度噪声独立频率 0.15 + seed 偏移 +8842 → 海底 / 沙滩微起伏（非完美平面，PLAN §2-K）。
        const double t = dist / effectiveRadius;                       // 0（角点）..1（岸线）
        const double heightNoise = fbm((x + m_seed + 8842) * 0.15, (z + m_seed + 8842) * 0.15) * 1.5;
        const int h = int(std::lround(seaFloor + (beachTop - seaFloor) * t + heightNoise));
        return std::max(0, std::min(h, m_height - 1));
    }

    // t372 高度过渡带（spec「沙滩与邻接森林高差突兀」）：沙盘外圈把高度从 beachTop smoothstep 过渡到
    //   自然 heightAt → 消除岸线处 cliff（beach 59 ↔ forest 62-66 突跳）。表层走自然群系（草地），故与
    //   generate 的沙表层判定（isSeaSandColumn）分离。纯函数于 seed + heightAt（fbm）→ 同 seed 同过渡（§2-K）。
    const double blendWidth = double(seaRadius) * 0.30; // 过渡带宽（海域半径 30%）
    if (dist <= effectiveRadius + blendWidth) {
        const int naturalH = std::min(heightAt(x, z), m_height - 1);
        const double bt = (dist - effectiveRadius) / blendWidth; // 0（接沙盘）..1（接内陆）
        const double e = bt * bt * (3.0 - 2.0 * bt);            // smoothstep（缓和、切线水平 → 无缝拼接）
        const int h = int(std::lround(beachTop + (naturalH - beachTop) * e));
        return std::max(0, std::min(h, m_height - 1));
    }
    return -1; // 远内陆 → 走自然 heightAt
}

// t372 沙海盘判定（spec「沙滩表层」）。返回该列是否为真正沙表层（海盆 + 干沙滩）。与 seaColumnHeight
//   共用完全相同的 effectiveRadius 计算（确定性一致：沙→草表层切换恰好落在岸线，与高度过渡带起点重合 →
//   沙滩边缘无缝接草地）。过渡带列（dist > effectiveRadius）返回 false → 走自然群系草地。纯函数于
//   seed + dims（PLAN §2-K）。供 generate 决定沙表层 / 草地表层。
bool World::isSeaSandColumn(int x, int z) const
{
    if (m_width <= 0 || m_depth <= 0) return false;
    int cx, cz;
    seaCorner(cx, cz);
    const int dx = x - cx, dz = z - cz;
    const double dist = std::sqrt(double(dx) * dx + double(dz) * dz);
    const int seaRadius = std::min(m_width, m_depth) * 3 / 10;
    const double shore = fbm((x + m_seed + 5331) * 0.07, (z + m_seed + 5331) * 0.07); // 与 seaColumnHeight 同源
    const double effectiveRadius = double(seaRadius) * (1.0 + 0.12 * shore);
    return dist <= effectiveRadius;
}

void World::generate()
{
    buildPermutation();
    m_chunks.recreate(m_width, m_depth, m_height); // 重建 chunk 网格（全新零填充 chunk，全脏）
    m_biomeCache.clear(); // t905 perf：seed / 尺寸换新 → 群系 memo 作废（懒重建；generate 首遍逐列填回）
    m_decayingLeaves.clear(); // t325 全新世界无失撑叶 → 清渐进衰减队列（防旧世界坐标误清新世界叶）
    m_growthCells.clear();   // t425 全新世界 → 清生长方格索引（worldgen placeSugarcane 经 setVoxelIfAir 增量重建）
    m_waterCells.clear();    // perf：全新世界 → 清流体方格索引（worldgen 直写 chunk 不经写入路径 → 末尾 rebuildFluidCells 全图重建）
    m_lavaCells.clear();
    m_iceCells.clear();      // t495：全新世界 → 清普通冰方格索引（worldgen freezeSurfaceWater 直写 chunk → 末尾 rebuildIceCells 全图重建）
    m_fireCells.clear();     // t724：全新世界 → 清火焰方格索引（worldgen 无火 → 稳态空集零开销；玩家点燃经 noteFireWrite 增量维护）
    m_burningCells.clear();  // review25 #1：全新世界 → 清燃烧侧表（regenerate/setSeed/setWidth/setDepth/setHeight 直调 generate 不经
    m_torchBurnout.clear();  // review26 #6：全新世界 → burnout 计数侧表作废（同 burningCells 世界复位口径）
                             //   beginLoad/rebuildFireCells → 漏清则旧世界燃烧坐标污染新世界：isBurningAt 假阳性点燃无辜可燃块 +
                             //   tickFire 倒计时继续烧毁替换（无掉落不可逆）+ 该格打火石被守卫拦成 no-op）
    m_powerDirty.clear();    // t656：全新世界 → 清红石电力脏集（worldgen 无红石电路 → 稳态空集零开销）
    fluidActReset();         // t488：全新世界 → 活动盒作废（generate 末置 dirty → 首次全量扫描兜底）
    gravLightReset();        // t933：全新世界 → 重力级联光照联合盒 / 批标志防御清（同 beginLoad 口径）
    resetWeather(); // t385 全新世界 → 天气从 Clear 重起（构造 / regenerate / 改尺寸均经 generate）

    // 填充地形（逐列规则，仅放大到 width×depth）：表层选择由「群系 + 海域」决定，下层 dirt / 深 stone。
    //   - 沙漠群系（isDesert）：**仅表层 4-6 格沙**下接 Stone（t255 修正：旧实现整柱沙 y 0..h 全 Sand →
    //     沙贯穿到基岩层，挖沙挖到底全沙、且沙柱占满石层使矿石无分布空间）。表层沙厚度按 hashColumn 派生
    //     4..6（确定性，PLAN §2-K，沙丘高低起伏感）；其下 Stone 由 scatterOres 散布矿石、底层由 placeBedrock
    //     覆盖基岩（机制等价 MC 沙漠：薄沙层 + 沙岩/石基底；本工程无沙岩方块故直接下接 Stone）。
    //   - 海域（t338 seaColumnHeight >= 0，集中于一角）：海盆 + 沙滩。该列地表重塑为缓坡（角点最深海底 →
    //     边缘干沙滩），表层 Sand / 下 Dirt / 深 Stone；fillWater 随后在海盆（h<waterLevel）灌满海水。海优先于
    //     群系（海覆盖任何群系，统一沙底）。
    //   - 其余内陆：正常陆地（表层 Grass / 下 Dirt / 深 Stone）。
    //   t338：旧「全域 h<=waterLevel+1 → 散布沙滩/水下沙」已移除 —— 内陆低洼列不再产散沙（spec「内陆无散沙」），
    //     沙 + 海水集中于此一角。逐列独立 → 跨 chunk 边界天然连续；同 seed 确定（fbm / seaColumnHeight 纯函数，§2-K）。
    //   走 ChunkManager.setBlock 跨 chunk 写入（初始全脏，其脏标记在此无副作用）。
    int desertCols = 0, seaCols = 0, plainsCols = 0, hillsCols = 0, forestCols = 0, snowyCols = 0, swampCols = 0, jungleCols = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const Biome bio = biomeAt(x, z);
            const bool desert = (bio == Biome::Desert); // t274：经 biomeAt 单一权威（原 isDesert 收口于此）
            // t338/t372：海域（海 + 沙滩）集中于一角。seaColumnHeight 返回沙海盘 + 过渡带的重塑高度（>=0）；
            //   isSeaSandColumn 仅沙海盘为真 → 沙表层；过渡带（高度已平滑过渡到 heightAt）走自然群系草地。
            const int seaH = seaColumnHeight(x, z);
            const bool inSeaHeight = (seaH >= 0);                       // 沙海盘 + 过渡带（高度重塑）
            const bool inSandSea = inSeaHeight && isSeaSandColumn(x, z); // 仅沙海盘（沙表层 / 灌水）
            const int h = inSeaHeight ? seaH : std::min(heightAt(x, z), m_height - 1);
            // t255/t394：沙漠列确定性哈希（PLAN §2-K）—— 沙厚度 / 砂岩厚度各取不同位段派生（解耦）。
            //   非沙漠列置 0 不用（下方 thickness 判定跳过）。
            const quint32 colHash = desert ? hashColumn(m_seed, x, z) : 0u;
            // t255：沙漠表层沙厚度 4..6 格（colHash 低 2 位派生）。
            const int desertSandThickness = desert ? (4 + int(colHash % 3u)) : 0;
            // t394：沙下砂岩层厚度 3..5 格（colHash bit[9:8] 派生；沙下成岩，机制等价 MC 沙漠沙下砂岩）。
            const int desertSandstoneThickness = desert ? (3 + int((colHash >> 8) % 3u)) : 0;
            // t761 沙海盘表层沙砾混排（机制等价 MC 1.0 海岸砾石滩斑 / 砾石海底）：两级确定性哈希——
            //   ① 4×4 粗格（x>>2,z>>2 共享决策）按 kGravelBeachPct% 选「砾石斑带」；② 带内逐列 65% 兑现。
            //   为什么两级：纯列级独立掷硬币成「撒胡椒面」（单列孤立砾石不读作滩斑），先成带再参差兑现
            //   → 成片但边缘破碎的砾石滩观感。密度旋钮 = kGravelBeachPct（常量可调，约 16% 沙海面列）。
            //   仅沙海盘（inSandSea）表层 y==h 一格（沙滩面 / 海底面）；沙砾同受重力（与沙同族塌落链）。
            constexpr unsigned kGravelBeachPct = 25u;  // 砾石斑带命中概率（密度主旋钮：25% 带 × 65% 列兑现 ≈ 16%）
            constexpr unsigned kGravelBeachFill = 65u; // 带内列兑现概率（调小 → 斑更稀碎；调大 → 斑更整片）
            const bool beachGravel = inSandSea
                && ((hashColumn(m_seed + 7611, x >> 2, z >> 2) % 100u) < kGravelBeachPct)
                && ((hashColumn(m_seed + 7612, x, z) % 100u) < kGravelBeachFill);
            if (desert) ++desertCols;
            else if (inSandSea) ++seaCols;
            // t306：森林地表仍为草（机制等价 MC 森林地表草地），仅树/草密度分化 → surface 填充无需分流 forest。
            if (bio == Biome::Plains) ++plainsCols;
            else if (bio == Biome::Hills) ++hillsCols;
            else if (bio == Biome::Forest) ++forestCols;
            else if (bio == Biome::Snowy) ++snowyCols;
            else if (bio == Biome::Swamp) ++swampCols;
            else if (bio == Biome::Jungle) ++jungleCols;
            for (int y = 0; y <= h; ++y) {
                quint8 b;
                if (inSandSea) {
                    // t338 海域：沙表层（海底 / 沙滩）+ Dirt + Stone（机制等价 MC 海岸沙 + 水下沙底）。沙海盘优先于
                    //   群系（海覆盖任何群系，统一沙底）；fillWater 随后在海盆（h<waterLevel）灌满海水到海平面。
                    //   t761：表层按列确定性混排沙砾（beachGravel 两级哈希，见上方常量注释）——砾石滩斑 /
                    //   砾石海底观感（机制等价 MC 1.0 海岸 gravel 滩）。
                    if (y == h)          b = beachGravel ? BlockRegistry::Gravel : BlockRegistry::Sand;  // 沙表层（海底 / 沙滩；t761 概率混砾）
                    else if (y >= h - 2) b = BlockRegistry::Dirt;  // 表层下土
                    else                 b = BlockRegistry::Stone; // 深石
                } else if (desert) {
                    // t255/t394：表层 desertSandThickness 格沙（h-y < thickness）下接 Sandstone 层（h-y <
                    //   thickness+sandstoneThickness）再下接 Stone（修旧整柱沙贯穿基岩 bug）。机制等价 MC 1.0
                    //   沙漠沙下砂岩层（沙压成岩）。沙 / 砂岩厚度均 colHash 不同位段派生 → 确定性（PLAN §2-K）。
                    if (h - y < desertSandThickness)                       b = BlockRegistry::Sand;      // 表层沙
                    else if (h - y < desertSandThickness + desertSandstoneThickness) b = BlockRegistry::Sandstone; // 沙下砂岩
                    else                                                b = BlockRegistry::Stone;      // 深石
                } else {
                    // t526 雪原地表结构（机制等价 MC 寒冷群系覆雪；区别旧版「雪层直接铺在泥土上」）：
                    //   泥→雪块→积雪层（y==h-1=Snow 整块、y==h=SnowLayer 薄层）→ 不生成草方块（雪原地表改泥土）。
                    //   ① 远离海边 / 沙滩：海域重塑带（inSeaHeight = 沙海盘 + 过渡带，含沙滩缓坡）的 Snowy 列**不覆雪**
                    //      （改泥土顶，区别旧版「沙滩边雪层」），仅内陆 Snowy 列才覆雪。
                    //   ② 雪层下雪块过渡（不直接泥上雪层）：避免雪层塌陷感 + 与「8 层≈雪块」语义一致（雪层下有雪块承托）。
                    //   t505 旧逻辑（SnowLayer 直接铺在草顶上）已重写为下方 Dirt→Snow→SnowLayer 三层。
                    const bool isSnowy = (bio == Biome::Snowy);
                    if (isSnowy && inSeaHeight) {
                        // t526 海域过渡带：雪原列不覆雪（远离海边 / 沙滩）、不生成草方块 → 泥顶。下 Dirt / Stone。
                        if (y == h)          b = BlockRegistry::Dirt;   // 过渡带泥顶（雪原列不草不雪）
                        else if (y >= h - 2) b = BlockRegistry::Dirt;   // 表层下土
                        else                 b = BlockRegistry::Stone;  // 深石
                    } else if (isSnowy) {
                        // t526 内陆雪原：SnowLayer 薄层（state 0..2 = 1/8..3/8 厚真实积雪）→ Snow 整块 → Dirt → Stone。
                        if (y == h) {
                            // SnowLayer 薄层（state 0..2 随机；与旧 t505 同 slHash 独立位段派生，确定性）。
                            //   m_chunks.setBlock 5 参数版写 id+state（worldgen 静默；光场随后 recomputeLightField 重算）。
                            const quint32 slHash = hashColumn(m_seed, x, z);
                            const quint8 snowState = quint8((slHash >> 4) % 3u); // 0..2（独立位段，确定性）
                            m_chunks.setBlock(x, y, z, BlockRegistry::SnowLayer, snowState);
                            continue; // 已写 SnowLayer（含 state），跳过下方默认 setBlock（其会重置 state=0）
                        } else if (y == h - 1) {
                            b = BlockRegistry::Snow; // 雪层下雪块过渡（泥→雪块→积雪层）
                        } else if (y >= h - 2) {
                            b = BlockRegistry::Dirt; // 表层下土
                        } else {
                            b = BlockRegistry::Stone; // 深石
                        }
                    } else if (y == h) {
                        b = BlockRegistry::Grass;      // 草地表层（非雪原列）
                    } else if (y >= h - 2) {
                        b = BlockRegistry::Dirt;       // 土
                    } else {
                        b = BlockRegistry::Stone;      // 石
                    }
                }
                m_chunks.setBlock(x, y, z, b);
            }
        }
    }
    // t274/t306：群系分布可观测（plains 应为多数 / forest 次之 / hills + desert 少数）；同 seed → 同分布（确定性核对）。
    qInfo() << "worldgen: biomes plains =" << plainsCols << "forest =" << forestCols
            << "hills =" << hillsCols << "desert =" << desertCols
            << "snowy =" << snowyCols
            << "swamp =" << swampCols
            << "jungle =" << jungleCols
            << "sea/beach =" << seaCols
            << "/" << (m_width * m_depth);

    placeBedrock(); // t119：底层基岩（y 0..4 坑洼，底实顶疏；不可破坏）。先于矿石 / 树（仅覆盖最底几格）
    scatterOres(); // 地形填充后确定性散布矿石（stone 区段，t84；先于树木，树只动地表空气无冲突）
    placeGravelPockets(); // t761：地下浅层沙砾矿袋（scatterOres 之后——矿石先占位不被覆盖；carveCaves 之前——
                          //   洞穴切穿矿袋 → 洞壁裸露沙砾，同矿石「carve 暴露」语义）。
    carveCaves(); // t278：洞穴隧道 carve（terrain + 矿石之后；挖走 stone/dirt/ore 暴露矿石于洞壁 → 为 t279 铺路）。
                  //   先于填水 → 水只填地表低洼列（h+1..waterLevel），不灌地下洞穴；先于树/草 → 表面特征放于完整地表。
    carveCaveEntrances(); // t341：山坡洞口（carveCaves 之后 → 连通既有洞穴网络；先于地下水 → 洞口路径干净；先于填水
                          //   / 树 / 草 → 洞口刻在完整地表）。仅该列近表有真实洞穴 air 才开口 → 永不产孤立竖井。
    placeUndergroundWaterPools(); // t309：封闭地下水池（carveCaves / 入口之后；先于填水 → 地表海平面与地下水各自独立）。
    placeLavaLakes(); // t343：地下封闭岩浆湖（Y<30；carveCaves 之后 → 湖独立于洞穴；先于填水 → 岩浆不与海水冲突）。
    placeDungeons(); // t392：地下地牢（carveCaves / 岩浆湖之后 → 房间独立；先于填水 → 不与海水冲突；先于峡谷 / 树 / 草 → 地表特征放于完整地表）。
    placeMineshaft(); // t484：废弃矿井（placeDungeons 之后 → 矿井独立；先于填水 → 不与海水冲突；先于峡谷 / 树 / 草 → 地表特征放于完整地表）。
    placeDesertTemple(); // t485：沙漠神殿（placeMineshaft 之后 → 神殿独立；仅 Desert 群系；先于填水 → 不与海水冲突；先于峡谷 / 树 / 草 → 金字塔放于完整沙漠地表）。
    placeJungleTemple(); // t486：丛林神殿（placeDesertTemple 之后 → 神殿独立；仅 Jungle 群系；先于填水 → 不与海水冲突；先于峡谷 / 树 / 草 → 苔石建筑放于完整丛林地表）。
    placeStronghold(); // t487：要塞（placeJungleTemple 之后 → 要塞独立；先于填水 → 不与海水冲突；先于峡谷 / 树 / 草 → 地下石砖迷宫放于完整地下）。
    carveCanyon(); // t342：大峡谷（caves/ores 之后 → 峡壁既有矿石层被 carve 暴露；先于填水 → 内陆干涸峡谷，
                   //   fillWater 仅填海域故不灌峡谷；先于树/草 → placeTrees/placeTallGrass 据「草顶」守卫天然跳过峡谷列）。
    pruneFloatingSnowLayers(); // t716 ③：carve 类 pass 之后清扫悬浮雪层（峡谷盘 / 洞口开口挖掉支撑格留下的
                               //   悬空 SnowLayer → 直删；先于填水 / 树草 → 后续特征据「雪顶」守卫不再误判）。
    fillWater(); // t148：海平面以下低洼列填水（地形之上；先于树木 → 水占格使树不生于水中，setVoxelIfAir 守）
    freezeSurfaceWater(); // t395：Snowy 群系海/湖表层水冻结为冰（fillWater 之后水已就位；先于树 / 草）
    placeSurfaceLakes(); // t309：地表小湖泊（fillWater 之后 → 湖独立于海；先于树 / 草 → 树 / 草据「草顶」守卫跳过湖列）。
    placeSwampPools(); // t396：Swamp 群系浅水池（fillWater / 地表湖之后 → 沼泽水独立；先于树 / 草 → 水格使树 / 草据「草顶」守卫跳过）。
    placeTrees(); // 地形填充后确定性种树（grass 表层，PLAN §2-K）
    placeJungleTrees(); // t481/t486 前置：Jungle 群系高树（5..7 格 + 大伞盖）单独散布（placeTrees 已跳过 Jungle 列）
    placeTallGrass(); // t235：grass 表层上方确定性散布草丛（PLAN §2-K；树定型后，仅写空气格不覆盖树）
    placeDesertFlora(); // t394：沙漠沙顶确定性散布仙人掌（1-3 格高柱）+ 枯死的灌木（PLAN §2-K；草丛后，仅写空气格）
    placeSwampFlora(); // t396：Swamp 群系睡莲（水面）+ 蘑菇（草岛）；PLAN §2-K；仅写空气格不覆盖水 / 草 / 树
    placeFlowers(); // t397：各群系草地确定性散布 4 色花（PLAN §2-K；草丛后，仅写空气格不覆盖草 / 树）
    placeSugarcane(); // t397：水域邻接陆地确定性散布 1..3 格高甘蔗（PLAN §2-K；花后，仅写空气格不覆盖草 / 树 / 花）
    placeSweetBerryBushes(); // t467：Snowy 群系雪顶确定性散布浆果灌木丛（PLAN §2-K；甘蔗后，仅写空气格不覆盖雪上已占格）
    findSpawnColumn(); // t756：全部地表特征（树 / 草 / 花 / 甘蔗 / 浆果丛）定型后选出生列 —— 裸地表判据读的是最终栅格
    recomputeLightField(); // t151：地形 / 树 / 草丛定型后一次性算光场（worldgen 内 m_chunks.setBlock 直写不触此）
    // perf：worldgen 末全图重建流体方格索引一次 —— worldgen 经 m_chunks.setBlock / fillWater 直写 chunk
    //   （不经 World 写入路径 → noteFluidWrite 不捕获）。一次性 3.3M 扫描在生成期可接受（非每 tick），
    //   使后续流体 tick 走 O(流体格数) 遍历而非全图扫描。
    rebuildFluidCells();
    // t495：worldgen 末全图重建普通冰方格索引一次 —— worldgen freezeSurfaceWater 经 m_chunks.setBlock 直写 chunk
    //   （不经写入路径 → noteIceWrite 不捕获）。一次性扫描在生成期可接受（非每 tick），使后续融化 tick（tickIceMelt）
    //   走 O(冰格数) 遍历而非全图扫描。
    rebuildIceCells();
    // t380：worldgen 末置流体脏 → 进世界后首次 tickWaterFlow/tickLavaFlow 各扫一次确认稳态（海洋 / 岩浆湖
    //   全源 → 零候选 → 即清标志停扫）。一次性确认扫描（防御：避免标志初始 false 漏掉 worldgen 引入的流场）。
    m_waterDirty = true;
    m_lavaDirty = true;
    // t1020：结构区域表落表（place* 选择已收口 sites() → 本重推导 = 同表投影；纯算术零体素访问）。
    rebuildStructureRegions();
}

// 整数哈希（FNV-1a + avalanche）：seed/x/z → 32 位确定性伪随机。纯函数，不依赖任何运行期随机源
// （PLAN §2-K：固定 seed → 完全一致的树分布）。与 Perlin 置换表独立，避免树位与高度噪声耦合。
quint32 World::hashColumn(int seed, int x, int z) const
{
    quint32 h = 0x811c9dc5u; // FNV-1a basis
    auto step = [&h](quint32 v) {
        h ^= v;
        h *= 0x01000193u; // FNV-1a prime
    };
    step(quint32(seed));
    step(quint32(x));
    step(quint32(z));
    // FNV-1a 单轮扩散偏弱，补一轮 xorshift-mix 提高 avalanche（低位用于密度判定，须质量好）。
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h;
}

// 体素级哈希（FNV-1a + 同款 avalanche）：seed/x/y/z → 32 位确定性伪随机。与 hashColumn 同算法、
// 多喂一个 y，供 scatterOres 做 3D 散布（矿石按体素而非按列分布）。纯函数（PLAN §2-K）。
quint32 World::hashVoxel(int seed, int x, int y, int z) const
{
    quint32 h = 0x811c9dc5u; // FNV-1a basis
    auto step = [&h](quint32 v) {
        h ^= v;
        h *= 0x01000193u; // FNV-1a prime
    };
    step(quint32(seed));
    step(quint32(x));
    step(quint32(y));
    step(quint32(z));
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    return h;
}

// 仅在合法边界且当前为空气时写入。树冠据此不覆盖主干/地形；跨 chunk 写入 + 脏标记由 ChunkManager 处理。
void World::setVoxelIfAir(int x, int y, int z, quint8 id)
{
    setVoxelIfAir(x, y, z, id, quint8(0)); // 委托 5 参数版（state=0，常规方块默认 state）
}

// t310 带 state 版：草变种 worldgen 需写 state（矮/中/高）。其余语义同上（仅写空气格）。
void World::setVoxelIfAir(int x, int y, int z, quint8 id, quint8 state)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return; // 世界越界跳过（setVoxelIfAir 已含边界判，配合 placeTrees 的钳制双重保险）
    if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air)
        return;
    m_chunks.setBlock(x, y, z, id, state);
    noteGrowthWrite(x, y, z, BlockRegistry::Air, id); // t425：worldgen placeSugarcane 经此 → 甘蔗入索引
    noteFluidWrite(x, y, z, BlockRegistry::Air, id);  // perf：worldgen 填水若经此 → 水格入索引（防御性，主要靠 generate 末 rebuildFluidCells）
    noteIceWrite(x, y, z, BlockRegistry::Air, id);    // t495：worldgen 冻结水若经此 → 冰格入索引（防御性，主要靠 generate 末 rebuildIceCells）
    noteFireWrite(x, y, z, BlockRegistry::Air, id);   // t724：worldgen 若经此写 Fire → 火格入索引（防御性，主要靠 generate / finishLoad 末 rebuildFireCells）
}

// 单棵橡树：surfaceY=草顶 y；主干 trunkH 格原木(id5)从 surfaceY+1 起；顶部树叶(id7)树冠。
// 树冠四层（贴近 MC 橡树「底宽上尖」）：底层 5×5（四角按 leafRand 随机有无 → 每棵轮廓各异）、
// 次层 3×3 去中心、第三层 3×3 去角（十字）、顶尖 1 叶。leafRand 由调用方从 hashColumn 高位传入
// （PLAN §2-K：纯 seed 派生，确定性）。主干先置、树冠后置且仅写空气格 → 树冠绝不覆盖主干。
void World::placeTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand)
{
    const int trunkBase = surfaceY + 1;
    const int trunkTopY = trunkBase + trunkH - 1;

    // 主干（地表上方空气，逐格置原木）。
    for (int y = trunkBase; y <= trunkTopY; ++y)
        setVoxelIfAir(x, y, z, BlockRegistry::Log);

    const auto absi = [](int v) { return v < 0 ? -v : v; };

    // 底层（trunkTopY-1）：5×5 去中心。四角（|dx|=2 且 |dz|=2）按 leafRand 低 4 位各一位决定有无
    // → 树冠底部轮廓每棵不同（有的方、有的圆/缺角）。边中点(±2,0)/(0,±2) 与 (±1,±1) 常置。
    const int yLow = trunkTopY - 1;
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dz = -2; dz <= 2; ++dz) {
            if (dx == 0 && dz == 0) continue; // 主干列保留原木
            if (absi(dx) == 2 && absi(dz) == 2) {
                const unsigned bit = (dx > 0 ? 1u : 0u) + (dz > 0 ? 2u : 0u); // 0..3 → 四角各一位
                if (!((leafRand >> bit) & 1u)) continue; // 该角本轮不生叶
            }
            setVoxelIfAir(x + dx, yLow, z + dz, BlockRegistry::Leaves);
        }
    }
    // 次层（trunkTopY）：3×3 去中心 → 绕主干八邻格叶。
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            setVoxelIfAir(x + dx, trunkTopY, z + dz, BlockRegistry::Leaves);
        }
    // 第三层（trunkTopY+1）：3×3 去角 → 十字（中心+四正交），圆润收口。
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx != 0 && dz != 0) continue; // 去四角
            setVoxelIfAir(x + dx, trunkTopY + 1, z + dz, BlockRegistry::Leaves);
        }
    // 顶尖单叶。
    setVoxelIfAir(x, trunkTopY + 2, z, BlockRegistry::Leaves);
}

// t395 单棵云杉（变种树，机制等价 MC 1.0 spruce）：surfaceY=雪顶 y；主干 trunkH 格云杉原木（id SpruceLog）
//   从 surfaceY+1 起；顶部窄锥形树冠（**t714 起云杉针叶 id SpruceLeaves**（133，深蓝绿贴图 tile 175）——
//   t714 前复用橡树叶 Leaves（用户「雪原树冠现在还是橡树叶」）；机制属性同 Leaves，衰减支撑同认 Log/
//   SpruceLog）。树冠呈「底宽顶尖」锥形（贴近 MC 云杉针叶树冠）：自 trunkBase+1 起逐层向上，底层半径 2
//   渐收到顶尖半径 0；半径 2 的层四角按 leafRand 各位决定有无 → 每棵锥冠轮廓各异。
//   主干先置、树冠后置且仅写空气格 → 树冠绝不覆盖主干。纯由 seed 派生（leafRand，确定性 PLAN §2-K）。
void World::placeSpruceTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand)
{
    const int trunkBase = surfaceY + 1;
    const int trunkTopY = trunkBase + trunkH - 1;

    // 主干（地表上方空气，逐格置云杉原木）。
    for (int y = trunkBase; y <= trunkTopY; ++y)
        setVoxelIfAir(x, y, z, BlockRegistry::SpruceLog);

    const auto absi = [](int v) { return v < 0 ? -v : v; };

    // 锥形树冠：自 trunkBase+1 到 trunkTopY+1（顶尖）逐层；底层半径 2、上半半径 1、顶尖单叶。
    //   半径 2 层四角（|dx|=2 且 |dz|=2）按 leafRand 低 4 位各一位决定有无 → 锥冠底部轮廓每棵不同。
    const int canopyLow = trunkBase + 1;
    const int canopyTop = trunkTopY + 1;
    const int canopyH = canopyTop - canopyLow; // 锥高（层数 - 1）
    for (int y = canopyLow; y <= canopyTop; ++y) {
        int radius;
        if (y == canopyTop) {
            // 顶尖单叶（锥顶）。
            setVoxelIfAir(x, y, z, BlockRegistry::SpruceLeaves);
            continue;
        }
        // 上半径 1（锥上半收窄）、下半径 2（锥下半宽）。canopyH<=0 时统一半径 1（极矮云杉兜底）。
        radius = (canopyH > 0 && (y - canopyLow) >= (canopyH + 1) / 2) ? 1 : 2;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (dx == 0 && dz == 0) continue; // 主干列保留原木
                if (absi(dx) > radius || absi(dz) > radius) continue; // 切比雪夫半径
                // 半径 2 层四角按 leafRand 决定有无（与橡树 placeTreeAt 同模式）。
                if (radius == 2 && absi(dx) == 2 && absi(dz) == 2) {
                    const unsigned bit = (dx > 0 ? 1u : 0u) + (dz > 0 ? 2u : 0u); // 0..3 → 四角各一位
                    if (!((leafRand >> bit) & 1u)) continue; // 该角本轮不生叶
                }
                setVoxelIfAir(x + dx, y, z + dz, BlockRegistry::SpruceLeaves);
            }
        }
    }
}

// t481/t486 前置 单棵丛林树（机制等价 MC 1.0 丛林树）：surfaceY=草顶 y；主干 trunkH 格原木（id5）从 surfaceY+1 起；
//   顶部「大伞盖」树冠（普通树叶 id Leaves）。树冠比橡树（placeTreeAt 半径 2 球冠）更大更浓（spec「树冠更大更浓」）：
//   trunkTopY-1 / trunkTopY 两层半径 3（7×7）**满填**（仅伞缘四角按 leafRand 低 4 位各一位决定有无 → 每棵伞缘轮廓
//   各异；橡树底层也是 5×5 四角随机，丛林伞更大 + 中层半径 2 / 上层半径 1 全满填 → 更密），trunkTopY+1 半径 2（5×5
//   去中心满填）、trunkTopY+2 半径 1（3×3 十字）、trunkTopY+3 顶尖单叶 → 共 5 层大伞（比橡树 4 层多一层 + 每层更宽）。
//   主干先置、树冠后置且仅写空气格（setVoxelIfAir）→ 树冠绝不覆盖主干 / 地形。纯由 seed 派生（leafRand，确定性 PLAN §2-K）。
void World::placeJungleTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand)
{
    const int trunkBase = surfaceY + 1;
    const int trunkTopY = trunkBase + trunkH - 1;

    // 主干（地表上方空气，逐格置原木）。
    for (int y = trunkBase; y <= trunkTopY; ++y)
        setVoxelIfAir(x, y, z, BlockRegistry::Log);

    const auto absi = [](int v) { return v < 0 ? -v : v; };

    // 大伞盖：自 trunkTopY-1 到 trunkTopY+3 逐层。半径：底层两层 3、中层 2、上层 1、顶尖 0。
    //   半径 3 层四角（|dx|=3 且 |dz|=3）按 leafRand 低 4 位各一位决定有无（与橡树 placeTreeAt 同模式）→ 伞缘轮廓各异；
    //   半径 ≤2 层**满填**（去中心主干列）→ 比橡树（底层 5×5 四角半随机 + 上三层稀）更浓（spec「更密叶」）。
    for (int y = trunkTopY - 1; y <= trunkTopY + 3; ++y) {
        const int radius = (y <= trunkTopY) ? 3
                         : (y == trunkTopY + 1) ? 2
                         : (y == trunkTopY + 2) ? 1
                                                : 0;
        if (radius == 0) { // 顶尖单叶（伞顶收口）。
            setVoxelIfAir(x, y, z, BlockRegistry::Leaves);
            continue;
        }
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                if (dx == 0 && dz == 0) continue; // 主干列保留原木
                if (absi(dx) > radius || absi(dz) > radius) continue; // 切比雪夫半径
                // 半径 3 层四角按 leafRand 决定有无（与橡树 placeTreeAt 同模式）；半径 ≤2 层满填（更浓）。
                if (radius == 3 && absi(dx) == 3 && absi(dz) == 3) {
                    const unsigned bit = (dx > 0 ? 1u : 0u) + (dz > 0 ? 2u : 0u); // 0..3 → 四角各一位
                    if (!((leafRand >> bit) & 1u)) continue; // 该角本轮不生叶
                }
                setVoxelIfAir(x + dx, y, z + dz, BlockRegistry::Leaves);
            }
        }
    }
}

// t481/t486 前置 丛林树散布（见 world.h 头注释）：遍历 Jungle 群系列，按 hashColumn(seed,x,z) 密度筛选 + 间距栅格
//   （3×3 邻域不得已有树干 → 主干间距 ≥2 列，同 placeTrees）散布高树。仅在 grass 表层（heightAt > waterLevel+1，
//   同 placeTrees 阈值）种；沙滩/水下/越界/近邻有树干 → 跳过。树干更高（5..7 格，spec「树干更高 ~5-7」；橡树 4..7、
//   云杉 6..9）+ 树冠更大更浓（placeJungleTreeAt 半径 3 大伞盖，spec「树冠更大更浓」）→ 丛林观感（高树浓叶）。
//   密度 14% > 森林 10% → 丛林更密（机制等价 MC 1.0 丛林密林；间距封顶 ~25% → 14% 全数通过间距）。
//   placeTrees 已在 biomeAt==Jungle 列跳过（丛林树只由本 pass 散布）→ 不与橡树重复。纯函数于 seed + biomeAt
//   （经 hashColumn）→ 同 seed 同分布；禁用任何运行期随机源（PLAN §2-K）。
void World::placeJungleTrees()
{
    std::vector<char> occupied(size_t(m_width) * size_t(m_depth), 0); // 主干占用栅格（1=该列已有树干）

    constexpr int kMinJungleTrunk = 5; // 丛林主干最少格数（spec「树干更高 ~5-7」；高于橡树 4）
    constexpr int kMaxJungleTrunk = 7; // 最多 7（同橡树上限，但下界更高 → 平均更高）
    constexpr int kCanopyExtra    = 3; // 大伞盖在主干顶之上再升的格数（半径 1 层 + 顶尖）
    constexpr unsigned kJungleTreePct = 14; // 丛林树密度（% of grass 列；高于森林 10 → 更密，机制等价 MC 丛林密林）

    int placed = 0;
    for (int z = 0; z < m_depth; ++z) {
        for (int x = 0; x < m_width; ++x) {
            if (biomeAt(x, z) != Biome::Jungle) continue; // 仅丛林群系
            const int surfaceY = heightAt(x, z);
            // 与 placeTrees 同阈值：沙滩带(wl±1)/水下(h<wl)/低洼不种树（机制等价 MC 树不生于沙滩/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            // 仅草顶列种（Jungle 地表为 Grass，与 generate 同；地表湖 / 洞口顶替换了草 → 不种）。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::Grass) continue;

            const quint32 r = hashColumn(m_seed, x, z);
            if (r % 100u >= kJungleTreePct) continue; // 密度筛选

            // 间距：主干列的 3×3 邻域（chebyshev 距离 ≤1）不得已有树干 → 保证主干间距 ≥2 列（同 placeTrees）。
            bool tooClose = false;
            for (int dz = -1; dz <= 1 && !tooClose; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= m_width || nz >= m_depth) continue;
                    if (occupied[size_t(nx) + size_t(m_width) * size_t(nz)]) { tooClose = true; break; }
                }
            }
            if (tooClose) continue;

            // 主干高度（哈希高位取 [kMinJungleTrunk,kMaxJungleTrunk]，与密度位段 r%100 解耦），按世界高度钳制
            // （留出大伞盖空间）。放不下最小树 → 确定性跳过。
            int trunkH = kMinJungleTrunk + int((r >> 8) % unsigned(kMaxJungleTrunk - kMinJungleTrunk + 1));
            const int maxTrunkH = (m_height - 1) - surfaceY - kCanopyExtra; // 留出伞盖空间后主干上限
            if (maxTrunkH < kMinJungleTrunk) continue; // 此列放不下最小丛林树 → 确定性跳过
            if (trunkH > maxTrunkH) trunkH = maxTrunkH;

            placeJungleTreeAt(x, surfaceY, z, trunkH, r >> 16); // leafRand 高位 → 伞缘四角叶有无（每棵轮廓各异）
            occupied[size_t(x) + size_t(m_width) * size_t(z)] = 1;
            ++placed;
        }
    }
    qInfo() << "worldgen: jungle trees placed =" << placed; // 同 seed → 同计数（确定性核对）
}

// 确定性树木散布：遍历列，按哈希(seed,x,z) 决定是否尝试种树；占用栅格保证主干间距 ≥2 列。
// 仅在 grass 表层（heightAt > waterLevel+1，与 generate() 沙层判定同阈值）种；沙滩/水下/沙漠/越界/近邻有
// 树干 → 跳过。主干高度按世界高度钳制（留出树冠空间），放不下最小树则确定性跳过。全部纯函数于 seed → 可复现。
// t306 群系分流密度（spec「森林（现多树）+ 草原（少树多草）」）：forest 密闭成林 / plains 开阔偶见孤树 /
//   hills 零星。机制等价 MC 1.0 森林/平原树密度分化。密度纯函数于 seed + biomeAt → 同 seed 同树分布。
void World::placeTrees()
{
    std::vector<char> occupied(size_t(m_width) * size_t(m_depth), 0); // 主干占用栅格（1=该列已有树干）

    constexpr int kMinTrunk    = 4; // 主干最少格数
    constexpr int kMaxTrunk    = 7; // 最多 7（低洼处可达）；高处按世界高度钳到 4 → 高度自然参差（用户诉求）
    constexpr int kCanopyExtra = 2; // 树冠在主干顶之上再升的格数（十字冠层 + 尖顶）
    // t306 群系分流树木密度（每 grass 列尝试概率 %）。间距筛选（主干 3×3 邻域不得已有树干 → 主干间距 ≥2 列）
    //   封顶实际密度 ≈ 25%，故 forest 10% 全数通过间距 → 密林观感；plains 1% → 开阔草原偶见孤树。
    constexpr int kForestTreePct = 10; // 森林密闭（spec「森林=现多树」）
    constexpr int kPlainsTreePct = 1;  // 草原稀疏（spec「草原=少树」）
    constexpr int kHillsTreePct  = 2;  // 山地零星（保留 t274 既有）
    constexpr int kSnowyTreePct  = 9;  // t395 雪原/针叶：针叶林密闭（机制等价 MC taiga 密植云杉；接近 forest 密度）

    int placed = 0;
    for (int z = 0; z < m_depth; ++z) {
        for (int x = 0; x < m_width; ++x) {
            const int surfaceY = heightAt(x, z);
            // t149：水位阈值取代旧 kSandLevel=3 —— 沙滩带(wl±1)/水下(h<wl)/低洼不种树（机制等价 MC 树不生于沙滩/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            const Biome bio = biomeAt(x, z);
            if (bio == Biome::Desert) continue;  // t117 沙漠群系不种树（机制等价 MC 沙漠无树）
            // t481/t486 前置：丛林群系跳过本 pass —— 丛林树（更高 + 大伞盖）由 placeJungleTrees 单独散布
            //   （同 spec 命名；不在本橡树 pass 重复种，避免「稀疏橡树混进密林」）。
            if (bio == Biome::Jungle) continue;
            // t309：跳过非草顶 / 非雪顶列（地表湖水面 / 洞穴入口竖井顶等已把草 / 雪替换 → 不种树；机制等价 MC 树仅
            //   生于草地 / 雪地）。读栅格当前方块（heightAt 是 worldgen 高度、不含 t309 改动）→ 湖列水面 / 洞口 air
            //   皆被本守卫拦截。t395：Snowy 群系地表为 SnowLayer（覆雪）→ 云杉生于雪上（机制等价 MC 寒冷群系针叶树）。
            const quint8 surf = m_chunks.blockAt(x, surfaceY, z);
            if (surf != BlockRegistry::Grass && surf != BlockRegistry::SnowLayer) continue;
            // t714 ①云杉树底须接泥土（用户「雪原云杉树底现悬空 / 长在细雪上」）：Snowy 列的 t526 地表结构为
            //   Dirt(h-2)→Snow(h-1)→SnowLayer(h)——雪层只是薄覆雪非土壤。加「支撑块须实体」守卫：SnowLayer
            //   顶列须其下一格（surfaceY-1）是 Snow（雪块过渡层）或 Dirt（过渡带泥顶）→ 雪层坐在实体上、树根
            //   扎入真实地面。非此结构（洞口 / 峡谷边缘雪层下空气、湖缘浮雪）→ 不种（机制等价 MC 树需实体土壤）。
            //   Grass 顶列（非雪原）本就坐在 Dirt 上 → 不查（守卫只对 Snowy 覆雪列生效）。
            if (surf == BlockRegistry::SnowLayer) {
                const quint8 under1 = surfaceY >= 1 ? m_chunks.blockAt(x, surfaceY - 1, z) : quint8(BlockRegistry::Air);
                if (under1 != BlockRegistry::Snow && under1 != BlockRegistry::Dirt) continue; // 雪层悬空 → 不种（t714 ①）
            }

            // t306/t395 群系分流密度：forest 密闭 / plains 稀疏 / hills 零星 / snowy 针叶林密植。
            const unsigned densityPct = (bio == Biome::Forest) ? unsigned(kForestTreePct)
                                        : (bio == Biome::Plains) ? unsigned(kPlainsTreePct)
                                        : (bio == Biome::Snowy)  ? unsigned(kSnowyTreePct)
                                                                 : unsigned(kHillsTreePct);
            const quint32 r = hashColumn(m_seed, x, z);
            if (r % 100u >= densityPct) continue; // 密度筛选

            // 间距：主干列的 3×3 邻域（chebyshev 距离 ≤1）不得已有树干 → 保证主干间距 ≥2 列。
            bool tooClose = false;
            for (int dz = -1; dz <= 1 && !tooClose; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= m_width || nz >= m_depth) continue;
                    if (occupied[size_t(nx) + size_t(m_width) * size_t(nz)]) { tooClose = true; break; }
                }
            }
            if (tooClose) continue;

            // 主干高度（哈希高位取 [kMinTrunk,kMaxTrunk]），按世界高度钳制使其不越界。
            int trunkH = kMinTrunk + int((r >> 8) % unsigned(kMaxTrunk - kMinTrunk + 1));
            const int maxTrunkH = (m_height - 1) - surfaceY - kCanopyExtra; // 留出树冠空间后主干上限
            if (maxTrunkH < kMinTrunk) continue; // 此列放不下最小树 → 确定性跳过
            if (trunkH > maxTrunkH) trunkH = maxTrunkH;

            // t395 雪原/针叶群系改种云杉变种（机制等价 MC taiga 云杉）：主干更高（云杉特征）+ 窄锥形树冠。
            //   云杉主干高度独立范围 kMinSpruceTrunk..kMaxSpruceTrunk（> 橡树），同源按世界高度钳制。
            //   leafRand = 哈希高位 → 驱动树冠四角叶随机（每棵轮廓各异）；与密度(低位)/高度(>>8)字段不重叠。
            if (bio == Biome::Snowy) {
                constexpr int kMinSpruceTrunk = 6; // 云杉主干最少格数（高于橡树 4）
                constexpr int kMaxSpruceTrunk = 9; // 最多 9（高耸针叶树）
                int spruceH = kMinSpruceTrunk + int((r >> 8) % unsigned(kMaxSpruceTrunk - kMinSpruceTrunk + 1));
                if (spruceH > maxTrunkH) spruceH = maxTrunkH;
                if (spruceH < kMinSpruceTrunk) continue; // 此列放不下最小云杉 → 确定性跳过
                placeSpruceTreeAt(x, surfaceY, z, spruceH, r >> 16);
            } else {
                placeTreeAt(x, surfaceY, z, trunkH, r >> 16);
            }
            occupied[size_t(x) + size_t(m_width) * size_t(z)] = 1;
            ++placed;
        }
    }
    qInfo() << "worldgen: trees placed =" << placed; // 可观测：同 seed → 同计数（确定性核对）
}

// t235/t274 草丛确定性散布（PLAN §2-K）：遍历列，在 grass 表层（heightAt > waterLevel+1，非沙漠，与 generate
//   草表层 / placeTrees / scatterOres 同阈值）上方一格（surfaceY+1）按 hashColumn(seed,x,z) 密度筛选置
//   TallGrass。仅写空气格（setVoxelIfAir）→ 不覆盖已生成的树干 / 树叶 / 水。无运行期随机源（纯 seed 派生）→
//   同 seed 同草丛分布。机制等价 MC 1.0 平原草丛点缀。
//   草丛占 surfaceY+1（grass 顶上方一格）；worldgen 顺序保证 placeTrees 先跑（树占 surfaceY+1 起若干格），
//   故树干列的 surfaceY+1 已被 Log 占据 → setVoxelIfAir 跳过（草丛不抢树位）。无列间距筛选（草丛密度天然高，
//   无需像树那样保证间距；机制等价 MC 平原草丛密集点缀）。
//   t337 群系分流密度修正（spec「森林=密树多草，草原=少树适量草，沙/海=无草」）：forest 草丛茂盛（35%，林下
//   密下木）、plains 适中（18%，开阔草原点缀）、hills 稀疏（12%，山地裸岩 / 林裸露感）、desert 无（biomeAt==
//   Desert 已跳过）。注：旧 t306/t274 令 plains(40%)>forest(18%)（草原多草），观感「全图铺满草」且森林不显密 →
//   t337 反转关系为 forest>plains，使森林读「密集」、草原读「开阔」，过渡呈 forest 35→plains 18→hills 12 自然
//   递降。纯函数于 seed + biomeAt → 同 seed 同分布。
//   t310 草变种（矮/中/高）：密度筛选后用**独立哈希位段** (r>>16)%100 选变种（与密度位段 r%100 解耦 → 密度与
//   变种分布互不污染），各群系变种配比不同——plains 以矮/中为主（典型草地）、forest 林下多中/高草（茂盛下木）、
//   hills 以矮草为主（裸露稀疏）。高草(2 格)需其上一格为空气（顶点延伸进上格）；被占则降级中草避免穿透实块。
void World::placeTallGrass()
{
    // t337 群系密度表（% of grass 列生草丛）：forest 茂盛 / plains 适中 / hills 稀疏（spec「森林多草，草原适量草」）。
    constexpr int kPlainsGrassPct = 18; // 草原适量（spec「草原=少树适量草」：开阔点缀；旧 40% 偏密致全图铺草）
    constexpr int kForestGrassPct = 35; // 森林茂盛（spec「森林=密树多草」：林下密下木；旧 18% 偏稀致森林不显密）
    constexpr int kJungleGrassPct = 35; // 丛林茂盛（t481/t486 前置：同森林，林下密下木；丛林高树浓叶 + 茂密林下草）
    constexpr int kHillsGrassPct  = 12; // 山地稀疏（裸岩 / 林感）

    // t310 各群系草变种配比（矮 / 中 / 高，% ；累积阈值见下方 vr 判定）。plains 矮/中为主、forest 林下茂盛多
    //   中/高、hills 矮草为主（裸岩稀疏）。机制等价 MC 群系草高分化（草原短草 / 森林高草）。
    struct VarMix { int shortPct, mediumPct; }; // 高草 = 100 - short - medium 兜底
    constexpr VarMix kPlainsMix = { 55, 35 }; // plains：55% 矮 / 35% 中 / 10% 高
    constexpr VarMix kForestMix = { 20, 40 }; // forest：20% 矮 / 40% 中 / 40% 高（林下茂盛）
    constexpr VarMix kJungleMix = { 15, 35 }; // jungle：15% 矮 / 35% 中 / 50% 高（比森林更茂盛的林下高草）
    constexpr VarMix kHillsMix  = { 65, 25 }; // hills：65% 矮 / 25% 中 / 10% 高

    int placed = 0;
    int plainsCols = 0, hillsCols = 0, forestCols = 0, jungleCols = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int surfaceY = heightAt(x, z);
            // 与 placeTrees 同阈值：沙滩带(wl±1)/水下(h<wl)/低洼不生草丛（机制等价 MC 草丛不生于沙/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            const Biome bio = biomeAt(x, z);
            if (bio == Biome::Desert) continue; // t117/t274 沙漠群系不生草丛（机制等价 MC 沙漠无草）
            if (bio == Biome::Snowy) continue;  // t395 雪原/针叶地表覆雪 → 不生草丛（机制等价 MC 寒冷群系雪地无草）
            // t309：跳过非草顶列（地表湖 / 洞口顶把草替换 → 草丛不生于水面 / 洞口；机制等价 MC 草丛仅生于草地）。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::Grass) continue;

            // t274/t306 群系分流密度：plains 密集 / forest 适中 / hills 稀疏。
            const unsigned densityPct = (bio == Biome::Plains) ? unsigned(kPlainsGrassPct)
                                        : (bio == Biome::Forest) ? unsigned(kForestGrassPct)
                                        : (bio == Biome::Jungle) ? unsigned(kJungleGrassPct)
                                                                 : unsigned(kHillsGrassPct);
            const quint32 r = hashColumn(m_seed, x, z);
            if (r % 100u >= densityPct) continue; // 密度筛选

            // t310 草变种：独立哈希位段 (r>>16)%100 选矮/中/高（与密度位段 r%100 解耦）。
            const VarMix mix = (bio == Biome::Plains) ? kPlainsMix
                             : (bio == Biome::Forest) ? kForestMix
                             : (bio == Biome::Jungle) ? kJungleMix
                                                      : kHillsMix;
            const unsigned vr = (r >> 16) % 100u;
            quint8 variant = (vr < unsigned(mix.shortPct))                       ? quint8(BlockRegistry::TallGrassShort)
                           : (vr < unsigned(mix.shortPct + mix.mediumPct))       ? quint8(BlockRegistry::TallGrassMedium)
                                                                                 : quint8(BlockRegistry::TallGrassTall);

            const int y = surfaceY + 1; // grass 顶上方一格
            if (y >= m_height) continue; // 世界顶之上不放（防御）
            // 高草(2 格)顶点延伸进上格 → 上格须为空气；被树叶/实块占据则降级中草（避免穿透实块视觉错乱）。
            if (variant == BlockRegistry::TallGrassTall
                && (y + 1 >= m_height || m_chunks.blockAt(x, y + 1, z) != BlockRegistry::Air)) {
                variant = BlockRegistry::TallGrassMedium;
            }
            setVoxelIfAir(x, y, z, BlockRegistry::TallGrass, variant);
            ++placed;
            if (bio == Biome::Plains) ++plainsCols;
            else if (bio == Biome::Forest) ++forestCols;
            else if (bio == Biome::Jungle) ++jungleCols;
            else ++hillsCols;
        }
    }
    qInfo() << "worldgen: tall grass placed =" << placed
            << "(plains" << plainsCols << "/ forest" << forestCols
            << "/ jungle" << jungleCols
            << "/ hills" << hillsCols << ")"; // 同 seed → 同计数（确定性核对）
}

// t394 沙漠植被散布（机制等价 MC 1.0 沙漠仙人掌 + 枯灌木点缀）：遍历 desert 沙顶列，按 hashColumn 密度筛选
//   在沙顶上方一格（surfaceY+1）置仙人掌柱（1-3 格高，仅写空气格）或枯死的灌木（cross 广告牌，仅写空气格）。
//   纯函数于 seed + biomeAt（经 hashColumn，PLAN §2-K）→ 同 seed 同分布；禁用任何运行期随机源。
//   仅写空气格（setVoxelIfAir）→ 不覆盖沙上已生成的方块 / 树 / 草丛（与 placeTrees/placeTallGrass 同守卫语义；
//   事实上 desert 列 placeTrees/placeTallGrass 已跳过，此处仅与沙海 / 洞口空气守卫配合）。
//   密度：仙人掌稀疏（~3% 沙漠列）、枯灌木适中（~6%）—— 沙漠少植被但仍有点缀，机制等价 MC 沙漠稀疏植被。
void World::placeDesertFlora()
{
    constexpr unsigned kCactusPct   = 3;  // 仙人掌密度（% of 沙漠沙顶列；稀疏点缀）
    constexpr unsigned kDeadBushPct = 6;  // 枯死的灌木密度（% of 沙漠沙顶列；适中点缀）
    int cactusPlaced = 0, deadBushPlaced = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            if (biomeAt(x, z) != Biome::Desert) continue; // 仅沙漠群系
            const int surfaceY = heightAt(x, z);
            // 与 placeTrees / placeTallGrass 同阈值：沙滩带(wl±1)/水下(h<wl)不生（机制等价 MC 沙漠植被不生于沙滩/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            // 仅沙顶列生（机制等价 MC 仙人掌 / 枯灌木生于沙；沙海盘 / 洞口顶替换了沙 → 跳过）。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::Sand) continue;

            const quint32 r = hashColumn(m_seed, x, z);
            const unsigned cr = r % 100u; // 密度位段
            if (cr < kCactusPct) {
                // t503 仙人掌柱 4 邻守卫：仙人掌旁贴完整方块才碎（review0903 #1 口径：整立方 isFullCube 邻接才
                //   触发 checkCactusOnEdit ④；轨 / 火把 / 压力板 / 雪层 / 半砖等非整立方邻接合法）。worldgen
                //   散布时跳过「柱位任一格的水平 4 邻有整立方方块」的位置（否则生成即立即破坏掉落，等同浪费 +
                //   留下掉落物堆积）。整柱（surfaceY+1..surfaceY+height）4 邻全无整立方方块（isFullCube）才放置。
                //   沙丘起伏时邻格可能更高（实体沙）→ 守卫跳过，仅平坦沙顶散布（机制等价 MC 沙漠仙人掌稀疏
                //   独立柱，不挤在沙丘边）。
                //   注意 isFullCube 取 shape==ShapeFull 整立方语义——**与 setBlock 放块路径 checkCactusOnEdit ④
    //   守卫同一谓词同源**（t445 起声明、t984 起兑现为谓词门槛、review0903 #1 起两路径同用
    //   isFullCube）：worldgen / 放置 / 挖除三路径口径一致；非整立方（草丛 / 火把 / 水 / 轨族 /
    //   压力板 / 雪层 / 半砖等异形；Glass / Ice / Spawner 虽 solid=false 但属整立方 → 算邻接）
    //   不算「邻接方块」。
                const int height = 1 + int((r >> 16) % 3u);
                bool neighborsClear = true;
                for (int i = 0; i < height && neighborsClear; ++i) {
                    const int yy = surfaceY + 1 + i;
                    if (yy >= m_height) break;
                    constexpr int kNb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                    for (const auto &d : kNb) {
                        if (BlockRegistry::isFullCube(m_chunks.blockAt(x + d[0], yy, z + d[1]))) {
                            neighborsClear = false; // 邻接实体方块 → 跳过此柱位
                            break;
                        }
                    }
                }
                // 仙人掌柱：高度 1..3（独立位段 (r>>16)%3 + 1，与密度位段解耦）。逐格向上仅写空气格
                //   （不穿透树叶 / 实块；遇非空气即止）。机制等价 MC 仙人掌可叠高。4 邻守卫已过 → 放置不会立即破。
                if (neighborsClear) {
                    for (int i = 0; i < height; ++i) {
                        const int y = surfaceY + 1 + i;
                        if (y >= m_height) break;
                        if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air) break; // 遇实块即止（不覆盖）
                        setVoxelIfAir(x, y, z, BlockRegistry::Cactus, 0);
                        ++cactusPlaced;
                    }
                }
            } else if (cr < kCactusPct + kDeadBushPct) {
                // 枯死的灌木（cross 广告牌）：沙顶上方一格（surfaceY+1），须空气（不覆盖）。
                const int y = surfaceY + 1;
                if (y < m_height && m_chunks.blockAt(x, y, z) == BlockRegistry::Air) {
                    setVoxelIfAir(x, y, z, BlockRegistry::DeadBush, 0);
                    ++deadBushPlaced;
                }
            }
        }
    }
    qInfo() << "worldgen: desert flora placed cactus =" << cactusPlaced
            << "dead_bush =" << deadBushPlaced; // 同 seed → 同计数（确定性核对）
}

// t396 沼泽浅水池（见 world.h 头注释）：遍历 Swamp 群系列，用低频 fbm（与地形 / 群系图均解耦）把约半数草地列
//   的草顶（y==surfaceY 的 Grass）改造成 1 格深 Water 源（state=0），余下保留为草岛。机制等价 MC 1.0 沼泽
//   「平地 + 浅水洼 + 草岛」地貌。Swamp 群系 heightAt amp=0 → 全 Swamp 列 surfaceY 等高（基线 64）→ 水源层
//   水平邻接同高草岛（Grass，solid）→ 不溢流（稳态源层，tickWaterFlow 无候选）。低频 fbm 阈值 0.0 → 约 50%
//   列成水、50% 留草岛，成片分布（非逐格斑点，机制等价 MC 沼泽大尺度水洼）。
//   仅处理 Swamp 非海列（海域 seaColumnHeight>=0 独立，跳过）；surfaceY 须明显高于海平面（沼泽水独立于海，
//   不溢入海）。走 m_chunks.setBlock 直写（worldgen 静默；光场随后 recomputeLightField 重算 → Water 全透光正确）。
//   纯函数于 seed（biomeAt + fbm）→ 同 seed 同沼泽水分布（PLAN §2-K）。
void World::placeSwampPools()
{
    int pools = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            if (biomeAt(x, z) != Biome::Swamp) continue; // 仅沼泽群系
            if (seaColumnHeight(x, z) >= 0) continue;     // 海域独立（海 / 沙滩不叠沼泽水）
            const int surfaceY = std::min(heightAt(x, z), m_height - 1);
            // 避开海平面附近（沼泽水独立于海、不溢入海）。Swamp amp=0 → surfaceY=64 > waterLevel+3=61 恒成立。
            if (surfaceY <= kWaterLevel + 3) continue;
            // 仅改草顶列（机制等价 MC 沼泽浅水生于草地；沙滩 / 水下 / 洞口替换了草 → 跳过）。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::Grass) continue;

            // 低频 fbm（独立频率 0.20 + seed 偏移 +1503，与地形 0.09 / 群系图 0.012-0.024 均解耦）→ 成片水洼。
            //   阈值 0.0 → 约 50% 列成水（fbm 近似对称居中 0），余草岛。
            const double pw = fbm((x + m_seed + 1503) * 0.20, (z + m_seed + 1503) * 0.20); // [-1,1]
            if (pw <= 0.0) continue; // 草岛（保留 Grass）
            // 草顶 → Water 源（1 格深，下方的 Dirt 仍托住水源）。直写（worldgen 静默）。
            m_chunks.setBlock(x, surfaceY, z, BlockRegistry::Water);
            ++pools;
        }
    }
    qInfo() << "worldgen: swamp pools =" << pools; // 同 seed → 同计数（确定性核对）
}

// t396 沼泽植物散布（见 world.h 头注释）：遍历 Swamp 群系列，在浅水格上方一格（surfaceY+1，水面之上）散布
//   睡莲（LilyPad 横向浮叶，仅写空气格）+ 在草岛格上方一格（surfaceY+1）低密度散布蘑菇（Mushroom cross 广告牌，
//   仅写空气格）。机制等价 MC 1.0 沼泽睡莲浮水 + 阴暗草地小蘑菇。
//   密度：睡莲 ~25% 水格（水面点缀，非满铺）、蘑菇 ~8% 草岛格（稀疏阴暗处冒头）—— 沼泽植物适量点缀（机制等价
//   MC 沼泽睡莲 / 蘑菇稀疏分布）。仅写空气格（setVoxelIfAir）→ 不覆盖水 / 草上已生成的方块（树 / 草丛）。
//   纯函数于 seed + biomeAt（经 hashColumn，PLAN §2-K）→ 同 seed 同分布；禁用任何运行期随机源。
void World::placeSwampFlora()
{
    constexpr unsigned kLilyPct   = 25; // 睡莲密度（% of 沼泽水格；水面点缀，非满铺）
    constexpr unsigned kMushPct   = 8;  // 蘑菇密度（% of 沼泽草岛格；稀疏阴暗处冒头）
    int lilyPlaced = 0, mushPlaced = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            if (biomeAt(x, z) != Biome::Swamp) continue; // 仅沼泽群系
            const int surfaceY = std::min(heightAt(x, z), m_height - 1);
            const int y = surfaceY + 1; // 水面 / 草顶上方一格（植物放置位）
            if (y >= m_height) continue; // 世界顶之上不放（防御）
            const quint8 surf = m_chunks.blockAt(x, surfaceY, z);
            const quint32 r = hashColumn(m_seed, x, z);
            if (surf == BlockRegistry::Water) {
                // 浅水格 → 睡莲（横向浮叶）：密度筛选后置 LilyPad 于水面上一格（cell 底部 quad 浮于水面）。
                if (r % 100u >= kLilyPct) continue; // 密度筛选
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air) continue; // 上方须空气（不覆盖树 / 实块）
                setVoxelIfAir(x, y, z, BlockRegistry::LilyPad, 0);
                ++lilyPlaced;
            } else if (surf == BlockRegistry::Grass) {
                // 草岛格 → 蘑菇（cross 广告牌）：低密度筛选后置 Mushroom 于草顶上一格。
                if (r % 100u >= kMushPct) continue; // 密度筛选
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air) continue; // 上方须空气（不覆盖树 / 草丛）
                setVoxelIfAir(x, y, z, BlockRegistry::Mushroom, 0);
                ++mushPlaced;
            }
        }
    }
    qInfo() << "worldgen: swamp flora placed lily =" << lilyPlaced
            << "mushroom =" << mushPlaced; // 同 seed → 同计数（确定性核对）
}

// t397 花散布（见 world.h 头注释）：遍历各群系草地列，按 hashColumn 密度 + 群系色彩配比散布 4 色花之一于草顶上方
//   一格（surfaceY+1）。机制等价 MC 1.0 各群系花点缀（平原多彩 / 森林少量 / 沼泽适量 / 山地稀疏）。
//   密度筛选（r%100 < pct）+ 色选（独立位段 (r>>16)%4）解耦 → 密度与色彩分布互不污染。仅写空气格（setVoxelIfAir）
//   → 不覆盖草上已生成的方块（树 / 草丛）。与 placeTallGrass 同阈值（非沙漠 / 非雪原 / 非沙滩水下 / grass 顶）。
//   纯函数于 seed + biomeAt（经 hashColumn，PLAN §2-K）→ 同 seed 同分布；禁用任何运行期随机源。
//   worldgen 顺序：placeTallGrass 之后（草丛占草顶上方一格优先），花仅写空气格 → 已被草丛 / 树占的列自然跳过。
void World::placeFlowers()
{
    // 各群系花密度（% of grass 列）。机制等价 MC 1.0 各群系花点缀密度分化：
    //   plains 多彩（草原花海，spec「平原多彩」）、forest 适中（林下小花）、swamp 适中（湿地野花）、hills 稀疏（裸岩少花）。
    //   取低于对应群系草丛密度（placeTallGrass：plains 18 / forest 35 / hills 12）→ 花点缀在草丛之间，不喧宾夺主。
    constexpr unsigned kPlainsFlowerPct = 10; // 平原花海（多彩点缀，spec「平原多彩」）
    constexpr unsigned kForestFlowerPct = 6;  // 森林林下小花（适量，不抢密草风头）
    constexpr unsigned kSwampFlowerPct  = 8;  // 沼泽湿地野花（草岛点缀）
    constexpr unsigned kHillsFlowerPct  = 4;  // 山地稀疏（裸岩 / 林少花）

    int placed = 0;
    int red = 0, yellow = 0, blue = 0, white = 0; // 各色计数（可观测 / 确定性核对）
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int surfaceY = heightAt(x, z);
            // 与 placeTallGrass 同阈值：沙滩带(wl±1)/水下(h<wl)/低洼不生花（机制等价 MC 花不生于沙/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            const Biome bio = biomeAt(x, z);
            if (bio == Biome::Desert) continue; // 沙漠群系不生花（机制等价 MC 沙漠无花）
            if (bio == Biome::Snowy) continue;  // 雪原/针叶地表覆雪 → 不生花（机制等价 MC 寒冷群系雪地无花）
            // 仅草顶列生花（机制等价 MC 花生于草地；地表湖 / 洞口顶替换了草 → 跳过）。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::Grass) continue;

            const quint32 r = hashColumn(m_seed, x, z);
            const unsigned densityPct = (bio == Biome::Plains) ? kPlainsFlowerPct
                                        : (bio == Biome::Forest) ? kForestFlowerPct
                                        : (bio == Biome::Swamp)  ? kSwampFlowerPct
                                                                 : kHillsFlowerPct; // hills 兜底
            if (r % 100u >= densityPct) continue; // 密度筛选

            // 色选：独立哈希位段 (r>>16)%4 选色（与密度位段 r%100 解耦 → 密度与色彩分布互不污染）。
            //   4 色均布（各 25%）；机制等价 MC 各色花在群系内随机点缀。
            const unsigned colorPick = (r >> 16) % 4u;
            quint8 flowerId = quint8(BlockRegistry::FlowerRed);
            if      (colorPick == 1) flowerId = quint8(BlockRegistry::FlowerYellow);
            else if (colorPick == 2) flowerId = quint8(BlockRegistry::FlowerBlue);
            else if (colorPick == 3) flowerId = quint8(BlockRegistry::FlowerWhite);

            const int y = surfaceY + 1; // 草顶上方一格
            if (y >= m_height) continue; // 世界顶之上不放（防御）
            // 仅写空气格 → 不覆盖草上已生成的方块（树干 / 树叶 / 草丛）。已被占的列自然跳过（草丛先于此 pass）。
            if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air) continue;
            setVoxelIfAir(x, y, z, flowerId, 0);
            ++placed;
            if      (flowerId == BlockRegistry::FlowerRed)    ++red;
            else if (flowerId == BlockRegistry::FlowerYellow) ++yellow;
            else if (flowerId == BlockRegistry::FlowerBlue)   ++blue;
            else                                              ++white;
        }
    }
    qInfo() << "worldgen: flowers placed =" << placed
            << "(red" << red << "/ yellow" << yellow
            << "/ blue" << blue << "/ white" << white << ")"; // 同 seed → 同计数（确定性核对）
}

// t397 甘蔗散布（见 world.h 头注释）：在邻水**沙顶**（沙滩 / 海岸）上方确定性散布 1..3 格高甘蔗柱。
//   spec t446 收紧：必须 (1) 直接坐在 Sand 方块上、(2) 水平 4 邻（沙顶层 surfaceY 或其下一层 surfaceY-1）有 Water
//   （任意 state）、(3) 沙顶正上方为空气（不在水里 / 湖底生）。草地 / 泥土 / 湖底 / 水中一律排除。
//   机制等价 MC 1.0 sugar cane 仅生于水边沙岸（beach/sand near water, not forest lakes）。
//   t446 根因修复（复现：seed 1337 全图 0 甘蔗）：本世界沙顶**只**出现在海域沙海盘（沙滩 / 海底），其沙顶层 y 是
//     seaColumnHeight（海域重塑高度），而**非** heightAt（自然 fbm 高度）—— t338/t372 把海面高度重塑与自然高度解耦
//     后，两者对海域列恒不等（实测 1854 个海沙列里 0 个 heightAt==seaColumnHeight）。旧实现误用 surfaceY=heightAt
//     → 在错误的 y 读 surf → surf 恒非 Sand → 整张图跳过 → 0 甘蔗。修：海域列用 seaColumnHeight 取真实沙顶 y；
//     非海域列（内陆草地 / 湖底 / 沼泽）seaColumnHeight<0 → 无沙顶，直接跳过（草地滨水 / 湖底 / 沼泽水天然排除）。
//   「邻水」双层查水（surfaceY / surfaceY-1）：沙滩沙顶常在 waterLevel+1、海水在 waterLevel（沙顶下一层）→ 须兼查
//     surfaceY-1 才命中海岸（t423）；与 tickSugarcaneGrowth 的 wateredAt(by)/wateredAt(by-1) 同语义。
//   「不在水里」：沙顶正上方（surfaceY+1）须为 Air —— 海底沙顶（seaH ≤ waterLevel）正上方是海水，在此排除（机制
//     等价 MC 甘蔗不生于水下）；仅干沙滩海岸（seaH ≥ waterLevel+1）生。逐格向上仅写空气格（setVoxelIfAir）→ 不覆盖
//     已生成的方块（树 / 草 / 花）。高度 1..3 独立哈希位段 (r>>16)%3 + 1（与密度位段 r%100 解耦）。
//   纯函数于 seed + biomeAt + 海域（seaColumnHeight / isSeaSandColumn / hashColumn，PLAN §2-K）→ 同 seed 同分布；
//   禁用任何运行期随机源。worldgen 顺序：placeFlowers 之后（花占草顶上方一格优先），甘蔗仅写空气格。
void World::placeSugarcane()
{
    constexpr unsigned kSugarcanePct = 10; // 邻水沙滩列生甘蔗密度（% of 邻水沙顶列；机制等价 MC 水边甘蔗稀疏散布
                                          //  t547④：30% → 10%（1/3），「沙滩生成太频繁」——甘蔗成片过长，降密度）
    int placed = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const Biome bio = biomeAt(x, z);
            if (bio == Biome::Desert) continue; // 沙漠群系甘蔗归 placeDesertFlora（仙人掌 / 枯灌木）—— 不在此散布
            // t446：取**真实沙顶 y**。本世界沙顶只出现在海域沙海盘（generate 在 seaColumnHeight 处铺 Sand）；
            //   海域列的沙顶 y = seaColumnHeight（非 heightAt —— 两者对海域列恒不等，见头注释根因）。非海域列
            //   （内陆草地 / 湖底 / 沼泽 / 海岸过渡带草地）seaColumnHeight<0 或非沙顶 → 无沙顶，跳过（草地滨水 /
            //   湖底 / 沼泽水天然排除，兑现 spec「不在草地 / 湖底生」）。
            const int seaH = seaColumnHeight(x, z);
            if (seaH < 0 || !isSeaSandColumn(x, z)) continue; // 非海域沙顶列 → 无沙，跳过
            const int surfaceY = seaH; // 真实沙顶 y（generate 在此铺 Sand）
            if (surfaceY <= 0 || surfaceY >= m_height) continue; // 防御（界内）
            // 双保险：须沙顶（generate 在 seaH 铺沙，正常恒真；防御性读栅格确认）。
            const quint8 surf = m_chunks.blockAt(x, surfaceY, z);
            if (surf != BlockRegistry::Sand) continue;

            // 「不在水里」：沙顶正上方须为空气。海底沙顶（seaH ≤ waterLevel）正上方是海水 → 排除（机制等价 MC
            //   甘蔗不生于水下）；仅干沙滩海岸（seaH ≥ waterLevel+1）正上方为空气才生。
            if (surfaceY + 1 >= m_height) continue;
            if (m_chunks.blockAt(x, surfaceY + 1, z) != BlockRegistry::Air) continue;

            // 「邻水」判定：沙顶（surfaceY）或沙顶下一层（surfaceY-1）的水平 4 邻任一为 Water（任意 state）。
            //   t423：须兼查 surfaceY-1 —— 沙滩沙顶常在 waterLevel+1、海水在 waterLevel（沙顶下一层），仅查
            //   surfaceY 会漏掉整片海岸沙滩；双层语义同 tickSugarcaneGrowth 的 wateredAt(by)/wateredAt(by-1)。
            const auto isWaterNb = [&](int yy, int dx, int dz) -> bool {
                if (yy < 0 || yy >= m_height) return false;
                const int nx = x + dx, nz = z + dz;
                if (nx < 0 || nz < 0 || nx >= m_width || nz >= m_depth) return false;
                return m_chunks.blockAt(nx, yy, nz) == BlockRegistry::Water;
            };
            const bool adjacentToWater =
                isWaterNb(surfaceY, 1, 0) || isWaterNb(surfaceY, -1, 0)
                || isWaterNb(surfaceY, 0, 1) || isWaterNb(surfaceY, 0, -1)
                || isWaterNb(surfaceY - 1, 1, 0) || isWaterNb(surfaceY - 1, -1, 0)
                || isWaterNb(surfaceY - 1, 0, 1) || isWaterNb(surfaceY - 1, 0, -1);
            if (!adjacentToWater) continue; // 远水陆地不生甘蔗

            const quint32 r = hashColumn(m_seed, x, z);
            if (r % 100u >= kSugarcanePct) continue; // 密度筛选

            // 高度 1..3（独立位段 (r>>16)%3 + 1，与密度位段解耦）。逐格向上仅写空气格 → 不穿透树叶 / 实块。
            //   机制等价 MC 甘蔗 1..3 格柱（spec「grows up to 3 tall」）。
            const int height = 1 + int((r >> 16) % 3u);
            for (int i = 0; i < height; ++i) {
                const int y = surfaceY + 1 + i;
                if (y >= m_height) break;
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air) break; // 遇实块即止（不覆盖）
                setVoxelIfAir(x, y, z, BlockRegistry::Sugarcane, 0);
                ++placed;
            }
        }
    }
    qInfo() << "worldgen: sugarcane placed =" << placed; // 同 seed → 同计数（确定性核对）
}

// t467 雪原浆果灌木丛散布（见 world.h 头注释）：遍历 Snowy 群系列，在积雪层（SnowLayer）地表上方一格低密度
//   散布浆果灌木丛（SweetBerryBush cross 广告牌，仅写空气格）。机制等价 MC 1.0 sweet berry bush（寒冷群系浆果丛）。
//   三守卫（同 placeTallGrass / placeFlowers 同族；t446 教训：用对 heightAt / seaColumnHeight）：
//   (1) 仅 Snowy 群系（biomeAt==Snowy；其它群系地表非雪 → SnowLayer 守卫天然跳过）；
//   (2) 地表须为 SnowLayer（generate 在 Snowy 群系把草顶替换为 SnowLayer，故真实雪顶 y = heightAt 自然地表；
//       海域 seaColumnHeight>=0 独立、地表湖 / 洞口顶替换了雪 → surf 恒非 SnowLayer → 跳过，不在水里 / 湖里生）；
//   (3) surfaceY > kWaterLevel+1（不在沙滩带 / 水下生，同 placeTallGrass 阈值；机制等价 MC 浆果丛不生于水边沙）。
//   阶段随机 1..2（独立哈希位段 (r>>16)&1 + 1，与密度位段 r%100 解耦）—— worldgen 丛均带果（阶段 0 无果嫩丛无散布意义，
//   玩家采摘后丛回阶段 0 由 tickSweetBerryBushGrowth 重新长，同小麦 / 树苗生长机制）。仅写空气格（setVoxelIfAir）
//   → 不覆盖雪上已生成的方块（云杉树干 / 树叶 / 任何已占格）。纯函数于 seed + biomeAt（经 hashColumn，PLAN §2-K）。
void World::placeSweetBerryBushes()
{
    constexpr unsigned kBushPct = 5; // 雪原雪顶列生浆果丛密度（% of 雪顶列；低密度点缀，机制等价 MC 浆果丛稀疏）
    int placed = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            if (biomeAt(x, z) != Biome::Snowy) continue; // 仅雪原/针叶群系
            const int surfaceY = heightAt(x, z);
            // 同 placeTallGrass / placeFlowers 阈值：沙滩带(wl±1)/水下(h<wl)/低洼不生（机制等价 MC 浆果丛不生于沙/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            // 仅雪顶列生（机制等价 MC 浆果丛生于雪原覆雪地表；海域 / 地表湖 / 洞口顶替换了雪 → 跳过）。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::SnowLayer) continue;

            const quint32 r = hashColumn(m_seed, x, z);
            if (r % 100u >= kBushPct) continue; // 密度筛选

            // 阶段随机 1..2（独立位段 (r>>16)&1 + 1，与密度位段解耦）。worldgen 丛均带果（不散布阶段 0）。
            const quint8 stage = quint8(1u + ((r >> 16) & 1u)); // 1 或 2

            const int y = surfaceY + 1; // 雪顶上方一格
            if (y >= m_height) continue; // 世界顶之上不放（防御）
            // 仅写空气格 → 不覆盖雪上已生成的方块（云杉树干 / 树叶）。已被占的列自然跳过。
            if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air) continue;
            setVoxelIfAir(x, y, z, BlockRegistry::SweetBerryBush, stage);
            ++placed;
        }
    }
    qInfo() << "worldgen: sweet berry bush placed =" << placed; // 同 seed → 同计数（确定性核对）
}

// t395 雪原/针叶群系水面冻结（见 world.h 头注释）：遍历 Snowy 群系列，把海平面表层水（y==waterLevel 的 Water
//   格）冻结为 Ice（机制等价 MC 1.0 寒冷群系水面结冰）。仅冻最顶层水面（同 MC 仅表层结冰；下层水保留为水源）。
//   地下水池（placeUndergroundWaterPools 的 cy ≤ h-7，对任意 surfaceY 恒 < waterLevel）不在 y==waterLevel → 不受
//   影响；故扫描固定 y==waterLevel 一层即精准命中「海 / 低洼地表水表面」而不误冻地下水。走 m_chunks.setBlock
//   直写（worldgen 静默；光场随后 recomputeLightField 重算 → Ice 满遮光正确计入）。纯函数于 seed（biomeAt，§2-K）。
void World::freezeSurfaceWater()
{
    if (kWaterLevel >= m_height) return; // 极端：世界高度不足（防御）
    int frozen = 0;
    const int y = kWaterLevel;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            if (biomeAt(x, z) != Biome::Snowy) continue; // 仅雪原/针叶群系冻结
            if (m_chunks.blockAt(x, y, z) == BlockRegistry::Water) {
                m_chunks.setBlock(x, y, z, BlockRegistry::Ice);
                ++frozen;
            }
        }
    }
    qInfo() << "worldgen: frozen surface ice =" << frozen; // 同 seed → 同计数（确定性核对）
}

// t468 结冰 tick（spec「寒冷群系暴露天空的水源→冰」）：见 world.h 头注释。每 5s 一窗，遍历水格索引
//   （m_waterCells，O(水格数)），挑 Snowy 群系 + 暴露天空（skyLightAt>=15）的水源（Water state==0）按散布
//   概率冻结为 Ice（setWaterSilent 静默写 + worldChanged）。worldgen freezeSurfaceWater 已在生成期冻结雪原表层水；
//   本 tick 处理玩家后放 / 冰破回水 / 动态暴露的延迟冻结（机制等价 MC random-tick 结冰）。
void World::tickIceFreeze()
{
    FrameProfiler::Scope prof("wIce"); // perf：含节流 / 早退
    if (++m_freezeTickCounter < kFreezeTickInterval) return; // 节流：每 kFreezeTickInterval tick（~5s）做一次判定
    m_freezeTickCounter = 0;
    if (m_width <= 0 || m_depth <= 0 || m_height <= 0) return;

    const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_freezeIntervalIndex) * 0x9E3779B9u)); // 窗口序号混入散布种子
    int frozen = 0;
    // perf：遍历 m_waterCells（O(水格数)）替代全图 W×D×H 扫描（O(3.28M)）。原全图扫「自顶向下进入阴影区即停」
    //   的列短路优化，改为「逐水格直接判 skyLightAt>=15」—— 语义等价（暴露天空的水源 skyLight=15），且免扫
    //   非水格 / 阴影区格。先收集冻结目标再统一应用：setWaterSilent 写 Ice 会经 noteFluidWrite 删 m_waterCells
    //   里的水格项 → 边遍历边删会迭代器失效（unordered_set erase 破坏当前迭代器）。
    struct FreezeTarget { int x, y, z; };
    std::vector<FreezeTarget> toFreeze;
    toFreeze.reserve(m_waterCells.size());
    for (const quint64 k : m_waterCells) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        if (biomeAt(x, z) != Biome::Snowy) continue; // 仅雪原/针叶群系结冰（寒冷生物群系）
        if (m_chunks.blockAt(x, y, z) != BlockRegistry::Water) continue; // 过期索引项 → 跳过
        if (m_chunks.stateAt(x, y, z) != 0) continue; // 仅水源结冰（流水 state>0 不结，机制等价 MC）
        if (m_chunks.skyLightAt(x, y, z) < 15) continue; // 阴影区不暴露天空 → 不冻（原全图扫的列短路等价）
        // t495 二轮复盘 表层水面守卫：仅冻「水面顶层」——上方一格非水非冰（即暴露空气 / 实体的水面表面）才冻。
        //   根因：本工程水 lightOpacity=0（不衰减天光，区别于 MC 1.0 水遮光），故水柱里每个水源格 skyLight 恒 15 →
        //   旧判定「skyLight>=15 即冻」会把整柱水从海面冻到海底（用户实测「冰填满整柱海水直到沙底」）。机制等价
        //   MC 1.0「仅水面顶层结冰」：水柱内部（上方还有水 / 已冻冰）不算暴露水面 → 不二次冻结下层。守卫只判 y+1
        //   一格：连续水柱里只有最顶格的 y+1 是空气（或非水非冰）→ 仅顶格进 toFreeze。ice 在 lightOpacity=0 下同样
        //   不遮天光，故已冻冰层下方的次格水 y+1 是 ice → 被守卫拦下，不继续向下冻（避免冰盖一旦形成即整柱冻透）。
        if (y + 1 < m_height) {
            const quint8 above = m_chunks.blockAt(x, y + 1, z);
            if (above == BlockRegistry::Water || BlockRegistry::isIce(above)) continue; // 上方有水/冰 → 非水面顶层 → 跳过
        }
        // 散布概率：seed + 位置 + 窗口序号哈希 → 不同格不同窗错峰冻结（非瞬时全冻，PLAN §2-K）。
        const quint32 h = hashVoxel(mixedSeed, x, y, z);
        if (int(h % 100u) >= kFreezePct) continue; // 散布落空 → 本窗不冻
        toFreeze.push_back({x, y, z});
    }
    // 批量静默写 Ice（m_batchFluid 收口把「N 格结冰 = N 次 emit worldChanged + N×clearAllDirty（清完又被下一格
    //    标回 → N 次重建请求）」折叠为 1 次 emit + clear；同 tickWaterFlow t350 批量收口模式）。setWaterSilent
    //    内部无变化早退（已冻结 / 已非水格不写入）；frozen 计数仅供可观测日志。
    m_batchFluid = true;
    for (const FreezeTarget &t : toFreeze) {
        if (setWaterSilent(t.x, t.y, t.z, BlockRegistry::Ice, 0)) ++frozen; // 静默写 Ice（系统模拟，非玩家破/放 → 无反馈）
    }
    m_batchFluid = false;
    flushPendingLightEdits(); // t380r：批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (frozen > 0) {
        emit worldChanged();       // 一次重建（仅脏 chunk）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏
    }
    ++m_freezeIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同窗口不同格错峰冻结）
    if (frozen > 0) qInfo("vo.world: tickIceFreeze frozen=%d", frozen); // 可观测性（同 tickCropGrowth）
}

// t495 普通冰融化 tick（spec「普通冰在高温/高亮环境（火把/熔炉/火）有概率融化成水」；机制等价 MC 1.0 ice 受高
//   方块光照射融化 —— 仅普通冰 Ice(45)，浮冰 PackIce / 蓝冰 BlueIce 永不融化）：见 world.h 头注释。每 2s 一窗，
//   遍历冰格索引（m_iceCells，O(冰格数)），挑「高亮邻」候选（6 正交邻发光源 OR 自身方块光 ≥ kIceMeltBlockLight）
//   按散布概率融化 Ice→Water（setWaterSilent 静默写 + worldChanged）。worldgen freezeSurfaceWater 已在生成期冻结
//   雪原表层水；本 tick 处理玩家把冰放到火把旁 / 把火把放在冰旁等动态高亮场景的延迟融化。
void World::tickIceMelt()
{
    FrameProfiler::Scope prof("wIceMelt"); // perf：含节流 / 早退
    if (++m_iceMeltTickCounter < kIceMeltTickInterval) return; // 节流：每 kIceMeltTickInterval tick（~2s）做一次判定
    m_iceMeltTickCounter = 0;
    if (m_width <= 0 || m_depth <= 0 || m_height <= 0) return;

    const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_iceMeltIntervalIndex) * 0x85EBCA6Bu)); // 窗口序号混入散布种子
    int melted = 0;
    // perf：遍历 m_iceCells（O(冰格数)）替代全图 W×D×H 扫描（O(3.28M)）。先收集融化目标再统一应用：setWaterSilent
    //   写 Water 会经 noteIceWrite 删 m_iceCells 里的冰格项 → 边遍历边删会迭代器失效（unordered_set erase 破坏当前
    //   迭代器，同 tickIceFreeze 教训）。索引项可能过期（某条直写路径漏 noteIceWrite）→ blockAt 复核跳过非冰格。
    struct MeltTarget { int x, y, z; };
    std::vector<MeltTarget> toMelt;
    toMelt.reserve(m_iceCells.size());
    // 6 正交邻偏移（冰融化查水平 + 上下邻的发光方块；机制等价 MC 冰从任意邻面受光照射均可融）。
    static const int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const quint64 k : m_iceCells) {
        int x, y, z;
        unpackGrowthCell(k, x, y, z);
        if (m_chunks.blockAt(x, y, z) != BlockRegistry::Ice) continue; // 过期索引项（非冰，如已融化 / 被破）→ 跳过
        // 「高亮邻」判定（机制等价 MC ice light ≥12 融化；本工程简化为二者其一即触发候选）：
        //   (a) 6 正交邻格任一为发光方块（BlockRegistry::lightEmission(id,state)>0：火把 14 / 燃烧熔炉 13 / 岩浆 15 /
        //       末地传送门 10）—— 即「邻接热源 / 强光源」；
        //   (b) OR 本格方块光（blockLightAt）≥ kIceMeltBlockLight(12) —— 火把近场照射（火把 14 衰减 1 → 距 1 格 = 13，
        //       距 2 格 = 12，均 ≥12 触发；机制等价 MC 冰需 light level ≥12 从 ≥2 邻面照射）。
        //   二者其一 → 本格为融化候选。注意不查天光（skyLight）：MC 冰只在「方块光 / 高亮」下融，阳光下不融
        //   （阳光下冰原不会自然融），故仅方块光路径。
        bool litNeighbor = false;
        for (const auto &d : kNb) {
            const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
            const quint8 nid = m_chunks.blockAt(nx, ny, nz);
            if (nid == BlockRegistry::Air) continue; // air 不发光（lightEmission 早返 0，但显式跳过省一次 stateAt）
            const quint8 nst = m_chunks.stateAt(nx, ny, nz);
            if (BlockRegistry::lightEmission(nid, nst) > 0) { litNeighbor = true; break; } // 邻发光源 → 候选
        }
        if (!litNeighbor && m_chunks.blockLightAt(x, y, z) < kIceMeltBlockLight) continue; // 无高亮邻 + 自身方块光不足 → 不融
        // 散布概率：seed + 位置 + 窗口序号哈希 → 不同格不同窗错峰融化（非瞬时全融，PLAN §2-K）。
        const quint32 h = hashVoxel(mixedSeed, x, y, z);
        if (int(h % 100u) >= kIceMeltPct) continue; // 散布落空 → 本窗不融
        toMelt.push_back({x, y, z});
    }
    // 批量静默写 Water（m_batchFluid 收口把「N 格融化 = N 次 emit worldChanged + N×clearAllDirty」折叠为 1 次 emit +
    //    clear；同 tickIceFreeze t350 批量收口模式）。Ice 融为水源 state=0（机制等价 MC 冰融化成水源；下游
    //    tickWaterFlow 自然处理水源蔓延 / tickIceFreeze 在雪原群系可重新冻结 → 冰-水动态循环稳态）。
    //    setWaterSilent 内部无变化早退；melted 计数仅供可观测日志。
    m_batchFluid = true;
    for (const MeltTarget &t : toMelt) {
        if (setWaterSilent(t.x, t.y, t.z, BlockRegistry::Water, 0)) ++melted; // 静默写 Water（系统模拟，非玩家破/放 → 无反馈）
    }
    m_batchFluid = false;
    flushPendingLightEdits(); // t380r：批量写延迟的光照重算 → 联合盒一次 refloodBox（无延迟编辑则 no-op）
    if (melted > 0) {
        emit worldChanged();       // 一次重建（仅脏 chunk）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏
    }
    ++m_iceMeltIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同窗口不同格错峰融化）
    if (melted > 0) qInfo("vo.world: tickIceMelt melted=%d", melted); // 可观测性（同 tickIceFreeze）
}

// t119 底层基岩：遍历列，在 y 0..4 铺一层 Bedrock（不可破坏方块，hardness=-1.0 → canMine=false）。
// 厚度按 hashVoxel(seed,x,y,z) 确定 —— 底层（y 小）近乎全实，顶层（y=4）稀疏（坑洼露出上方石层），
// 机制等价 MC 1.0 基岩层「底实顶疏」。具体阈值：(hash%100) < (5-y)*25 → 置 Bedrock，否则保留地形原样：
//   y=0 → <125（恒真）→ 100% 基岩（实心底，防世界底部 void / 玩家坠落出界）
//   y=1 → <100（恒真）→ 100% 基岩
//   y=2 → <75 → 75% 基岩（开始有坑洼）
//   y=3 → <50 → 50% 基岩
//   y=4 → <25 → 25% 基岩（最疏，向上过渡到普通石层）
// 注：spec 原文「(hash%100) < (5-y)*25 留 air，否则 Bedrock」的「留 air」语义会把 y=0 全置空气（底部 void，
//   世界无底、玩家坠落出界）——与「基岩作不可破坏底」的机制目标矛盾。此处把判定结果置为 Bedrock（而非 air），
//   既满足 spec 验收「基岩层坑洼」（坑洼=上层基岩稀疏处露出石），又保证底部实心不 void。
//   越界（y>=m_height）天然由循环上界挡住；hashVoxel 纯函数于 seed → 同 seed 同基岩分布（PLAN §2-K）。
void World::placeBedrock()
{
    constexpr int kBedrockTop = 4; // 基岩层上界（含）；y 0..4 共 5 层
    if (m_height <= 0) return;     // 极端：无高度世界不铺基岩（防御）
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int top = std::min(kBedrockTop, m_height - 1); // 高度不足时只铺到顶
            for (int y = 0; y <= top; ++y) {
                const quint32 r = hashVoxel(m_seed, x, y, z);
                if ((r % 100u) < unsigned((5 - y) * 25))
                    m_chunks.setBlock(x, y, z, BlockRegistry::Bedrock);
                // 否则保留 generate() 已填的 Stone（坑洼 = 上层基岩缺位处露出石）
            }
        }
    }
}

//   t1014 矿脉化 worldgen（MC 1.0 式矿脉，取代旧逐体素散点 t84/t279/t308/t471/t569）：
//
//   旧口径（已退役）：全 stone 区段逐体素双路 hash 密度筛选（0.4%~1.0%/体素）→ 全图胡椒面散点。
//   P-t1014 基线实测散点签名（5 seed × 128×128×64，carve 后存活）：煤/铜/铁/金/钻/青连通域
//   ≥96% 为孤块、连接率 conn4 ≈ 0%（矿脉化阴性轮敏感签名）。
//
//   新口径：**每矿种脉形 profile + 每 16×16 cell 定数成脉**（机制等价 MC 1.0「每 chunk N 次
//   vein try」口径的确定性投影）——每 cell 每矿种脉数 = kBase + (hash%100 < kFrac)%，cell 内
//   hash 抖动落点，逐 profile 印章（stamp）：
//   ── 六矿种脉形 profile（煤/铁按用户逐字口径；金/红石/钻石/青金按 MC 1.0 wiki size 口径，
//      高度带「以项目现有高度带为准对齐」逐字沿用旧散点常量；铜为 t308 项目自产矿种按 blob 并入）──
//     - 煤   coal：2×2×5 长条（横轴 hash 二选一）+ 周围 6 邻壳 hash 贴连散块（25%）→ 均值 ~28 块/脉
//     - 铁   iron：2×2×2 立方 + 8 角斜对贴块（35%）→ 均值 ~11 块/脉（MC 1.0 size 8 口径）
//     - 金   gold：MC 1.0 size 8 → 8 步漂移游走 blob（60% 主轴漂移 + 40% 全向 6 邻）
//     - 钻石 diam：MC 1.0 size 8 → 8 步 blob（上界 40 = t308 用户口径「深挖更易见」）
//     - 红石 rs  ：MC 1.0 size 8 → 8 步 blob（y<16 最深层）
//     - 青金 lapis：MC 1.0 size 7 → 7 步 blob（y<32 一带）
//     - 铜   copper：t308 项目矿种（MC 无 1.0 对应）→ 8 步 blob（y<45 浅中层）
//
//   经济（**频率经济不涨总矿量**，用户硬口径）：每矿种总块数与旧散点 carve 后存活口径持平
//   （±12% 窗；P-t1014 基线 @128×128×64：coal 7868 / copper 5084 / iron 2467 / gold 1526 /
//   diamond 2106 / lapis 2530 / redstone 28393）。kBase/kFrac 即按此校准。
//   【登记（既有口径如实沿用，非本单新引入）】旧红石散点 (r2>>24)%10000 为 8 位截断 → 有效密度
//   40/256≈15.6%（≠当年代注释 0.4%）→ 红石散点总量被推高约 40×。t1014 按字面「与现散点口径
//   持平」沿用该有效密度校准脉频（每 cell ~60 脉 → 深层红石近镶嵌毯状成片）；密度再平衡属后续
//   玩法平衡任务，不在本单内擅自改动。
//
//   脉内断连（用户口径「允许断连、基本都连接起来」）：游走 blob / 长条 / 立方自身 6 连通；重叠带
//   先到先得（印章序同旧判定序：钻>金>青>红>铁>铜>煤）+ carve 切穿 + 非石格拒绝 → 天然小断连率。
//   连接率窗 conn4 ≥ 60%（P-t1014 量出）。
//
//   结构避让（既有优先级别破）：本 pass 仍先于 placeGravelPockets / carveCaves / 矿井 / 神殿 /
//   要塞——结构覆盖矿石、gravel 只置换 Stone、carve 挖走 stone/ore 暴露矿脉于洞壁，语义全保留。
//   逐列海检（tryOre h ≤ wl+1 拒）/ 逐格 stone 区段上界 y ≤ h-3 / 基岩层不布矿 / 仅置换 Stone，
//   均沿用旧口径（逐格重查 heightAt → 高度带按列自适应，128 高世界煤脉自然上探）。review0906
//   #11：cell 级海列整格跳过已移除（旧口径按中心列一票否决整 cell = 海岸带整 16×16 零矿 patch，
//   ≠ 旧散点逐列跳过分布；现纯海 cell 在 tryOre 逐列被拒，分布恢复旧貌）。矿井巷壁暴露矿（t565
//   IronOre/CoalOre，见 placeMineshaft）独立于本 pass，不受影响。
//   确定性：全部 hashVoxel(seed ⊕ 矿盐, ...) 纯函数（PLAN §2-K），同 seed 同矿脉；禁运行期随机源。
void World::scatterOres()
{
    constexpr int kBedrockTop = 4; // 同 placeBedrock：基岩层 y 0..4 不布矿（旧 kOreMin=5 同源）
    constexpr int kCell       = 16; // 成脉网格（MC chunk 水平口径）

    // 高度带（逐字沿用旧散点常量——「以项目现有高度带为准对齐」）。
    constexpr int kOreMin      = 5;   // 矿物起始 y（紧贴基岩 kBedrockTop=4 之上；基岩层 y 0..4 不布矿）
    constexpr int kCoalMin     = 8;
    constexpr int kDiamondMin  = 5;
    constexpr int kDiamondMax  = 40;
    constexpr int kGoldMax     = 25;
    constexpr int kLapisMax    = 31;
    constexpr int kRedstoneMax = 16;
    constexpr int kIronMax     = 30;
    constexpr int kCopperMax   = 45;

    // 脉形 profile（kBase/kFrac = 每 cell 脉数经济校准，见头注释；kSteps = blob 游走步数；
    //   kShellPct = 煤壳/铁角贴块概率；kKind = 0 blob 游走 / 1 煤长条 / 2 铁立方角）。
    //   review0907 A-P1-2：煤 yMax=0 哨兵 = 无硬帽，上界纯列自适应（hc-5，对齐旧散点 stoneTop=h-3
    //   口径）—— 原 60 硬帽使 128 高世界高山列煤脉被封顶（与头注释「128 高世界煤脉自然上探」声明相悖）；
    //   64 高 rig 里 hc-5 ≤ 58 < 60 本就不触发帽 → 腿行为不变（只放开 128 高世界上探）。
    struct VeinProfile {
        quint8  block;
        quint32 salt;     // 矿盐（t1014 专用盐值；cell / 落点 / 逐块决策三流解耦）
        int     yMin, yMax;
        int     base, frac, steps, shellPct, kind;
    };
    // 数组序 = 印章序（重叠区稀有矿先到先得，同旧散点判定序：钻>金>青>红>铁>铜>煤）。
    constexpr VeinProfile kProfiles[] = {
        { BlockRegistry::DiamondOre,  0x01D1A5u, kDiamondMin, kDiamondMax,  5, 50, 8,  0, 0 },
        { BlockRegistry::GoldOre,     0x0A0D7Bu, kOreMin, kGoldMax,         4, 15, 8,  0, 0 },
        { BlockRegistry::LapisOre,    0x01A915u, kOreMin, kLapisMax,        7, 60, 7,  0, 0 },
        { BlockRegistry::RedstoneOre, 0x2ED57Eu, kOreMin, kRedstoneMax,    82, 75, 8, 0, 0 },
        { BlockRegistry::IronOre,     0x12D0E1u, kOreMin, kIronMax,         4, 40, 0, 35, 2 },
        { BlockRegistry::CopperOre,   0x0C09E5u, kOreMin, kCopperMax,      13, 92, 8,  0, 0 },
        { BlockRegistry::CoalOre,     0x0C0A15u, kCoalMin,     0,           4, 60, 0, 25, 1 },
    };
    static constexpr int kOreKindCount = 7;

    // 置矿原语：越界 / 基岩层 / 海列 / stone 区段上界（y > h-3，同旧「y < h-2」）/ 非 Stone 拒绝。
    //   heightAt 逐格调 → 矿带按列自适应；先到先得由「仅置换 Stone」保证（重叠带稀有矿优先）。
    const auto tryOre = [&](int x, int y, int z, quint8 id) -> bool {
        if (x < 0 || x >= m_width || z < 0 || z >= m_depth || y < 0 || y >= m_height) return false;
        if (y <= kBedrockTop) return false;
        const int h = std::min(heightAt(x, z), m_height - 1);
        if (h <= kWaterLevel + 1) return false;
        if (y > h - 3) return false;
        if (m_chunks.blockAt(x, y, z) != BlockRegistry::Stone) return false;
        m_chunks.setBlock(x, y, z, id);
        return true;
    };

    static const int kD6[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    int placedByKind[kOreKindCount] = {};

    for (int cz = 0; cz < m_depth; cz += kCell) {
        for (int cx = 0; cx < m_width; cx += kCell) {
            // review0906 #11：cell 中心列高**不再作海列整格跳过**——旧口径按中心列一票否决整个
            //   16×16 cell，海洋中心 cell 内的陆地列（海岸过渡带）从此完全无矿 = 整 16×16 零矿
            //   patch，与旧散点「逐列跳过」分布不符。恢复旧分布：海列拒绝兜底在 tryOre 逐列海检
            //   （h ≤ wl+1 即拒；veinN 计算便宜，纯海 cell 的脉在逐列 tryOre 全数被拒 = 零成本余量）。
            //   hc 仍保留：煤带（kind==1）上界的 cell 列高自适应（纯海 cell 中煤落点 y 同受逐列
            //   tryOre 兜底，无矿溢出）。
            const int hc = std::min(heightAt(std::min(cx + kCell / 2, m_width - 1),
                                             std::min(cz + kCell / 2, m_depth - 1)), m_height - 1);
            for (int k = 0; k < kOreKindCount; ++k) {
                const VeinProfile &p = kProfiles[k];
                // 每 cell 脉数 = base + (hash%100 < frac)%（每矿种独立盐流，确定性）。
                const int veinN = p.base
                    + int((hashVoxel(int(quint32(m_seed) ^ p.salt), cx, 0x5E1D, cz) % 100u) < quint32(p.frac) ? 1 : 0);
                for (int i = 0; i < veinN; ++i) {
                    // 落点：cell 内 hash 抖动（bit0-3 x / bit4-7 z / bit8+ y 带 / bit16-18 姿态）。
                    const quint32 hv = hashVoxel(int(quint32(m_seed) ^ (p.salt * 31u + 17u)), cx, i, cz);
                    // review0907 A-P1-2：煤（kind==1）yMax=0 哨兵 → 上界 = hc-5 纯列自适应（逐块仍受
                    //   tryOre 的 y ≤ h-3 逐列石门兜底；64 高世界 hc-5 ≤ 58 与旧 min(60,·) 等价）。
                    const int span = (p.kind == 1)
                        ? std::max(1, (p.yMax > 0 ? std::min(p.yMax, hc - 5) : hc - 5) - p.yMin + 1) // 煤带上界随 cell 列高（长条+壳 ≤ h-3）
                        : (p.kind == 2)
                            ? std::max(1, p.yMax - p.yMin) // 铁立方占 vy..vy+1 → 抖动上界收一格保 vy+1 ≤ 带
                            : p.yMax - p.yMin + 1;
                    const int vx = cx + int(hv & 15u);
                    const int vz = cz + int((hv >> 4) & 15u);
                    const int vy = p.yMin + int((hv >> 8) % quint32(span));
                    const int decSalt = int(quint32(m_seed) ^ (p.salt ^ 0x51E1Du));

                    if (p.kind == 1) {
                        // ── 煤：2×2×5 长条（横轴 hash 二选一）+ 周围 6 邻壳 hash 贴连散块 ──
                        const bool axX = (hv >> 16) & 1u;
                        int core[20][3];
                        int nCore = 0;
                        for (int t = 0; t < 5; ++t)
                            for (int a = 0; a < 2; ++a)
                                for (int b = 0; b < 2; ++b) {
                                    const int x = vx + (axX ? t : a);
                                    const int y = vy + b;
                                    const int z = axX ? vz + a : vz + t;
                                    if (tryOre(x, y, z, p.block)) ++placedByKind[k];
                                    core[nCore][0] = x; core[nCore][1] = y; core[nCore][2] = z; ++nCore;
                                }
                        for (int c = 0; c < nCore; ++c)
                            for (const auto &d : kD6) {
                                const int nx = core[c][0] + d[0];
                                const int ny = core[c][1] + d[1];
                                const int nz = core[c][2] + d[2];
                                bool inCore = false;
                                for (int q = 0; q < nCore && !inCore; ++q)
                                    inCore = core[q][0] == nx && core[q][1] == ny && core[q][2] == nz;
                                if (inCore) continue;
                                if ((hashVoxel(decSalt, nx, ny, nz) % 100u) >= quint32(p.shellPct)) continue;
                                if (tryOre(nx, ny, nz, p.block)) ++placedByKind[k];
                            }
                    } else if (p.kind == 2) {
                        // ── 铁：2×2×2 立方 + 8 角斜对贴块（hash 贴块；角块 y 同样夹进矿带 → 带内零溢出）──
                        for (int a = 0; a < 2; ++a)
                            for (int b = 0; b < 2; ++b)
                                for (int c = 0; c < 2; ++c)
                                    if (tryOre(vx + a, vy + b, vz + c, p.block)) ++placedByKind[k];
                        for (int a = -1; a <= 2; a += 3)
                            for (int b = -1; b <= 2; b += 3)
                                for (int c = -1; c <= 2; c += 3) {
                                    const int cy = vy + b;
                                    if (cy < p.yMin || cy > p.yMax) continue; // 角块不越带
                                    if ((hashVoxel(decSalt, vx + a, cy, vz + c) % 100u) < quint32(p.shellPct))
                                        if (tryOre(vx + a, cy, vz + c, p.block)) ++placedByKind[k];
                                }
                    } else {
                        // ── blob：MC 1.0 size 口径漂移游走（60% 主轴漂移 / 失位或越带转全向 6 邻）──
                        int x = vx, y = vy, z = vz;
                        const int axis = int((hv >> 16) % 3u);
                        const int sgn = ((hv >> 18) & 1u) ? 1 : -1;
                        bool offStone = false; // 失位（非石/带外）→ 转全向步，防贴壁耗步
                        for (int s = 0; s < p.steps; ++s) {
                            if (tryOre(x, y, z, p.block)) ++placedByKind[k];
                            else offStone = true;
                            const quint32 rv = hashVoxel(decSalt, x, y, z);
                            int dx = 0, dy = 0, dz = 0;
                            if (!offStone && int(rv % 100u) < 60) {
                                dx = (axis == 0) ? sgn : 0;
                                dy = (axis == 1) ? sgn : 0;
                                dz = (axis == 2) ? sgn : 0;
                            } else {
                                const int d = int((rv >> 8) % 6u);
                                dx = kD6[d][0]; dy = kD6[d][1]; dz = kD6[d][2];
                            }
                            // 矿带硬夹：下一步 y 出带 → 抹平竖向分量（纯竖漂移则改全向水平步）。
                            if (y + dy < p.yMin || y + dy > p.yMax) {
                                dy = 0;
                                if (dx == 0 && dz == 0) {
                                    // review0906 #18：重挑**限水平 4 向**——旧 6 向重挑仍可能抽中竖向
                                    //   （kD6[2]/[3]）→ dy 已抹平 + dx=dz=0 = 零位移步，同格重试到步数
                                    //   耗尽（脉提前夭折，尺寸分布有偏）。水平 4 向保证重挑必走一步。
                                    static const int kH4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                                    const int d = int((rv >> 8) % 4u);
                                    dx = kH4[d][0]; dz = kH4[d][1];
                                }
                            }
                            x += dx; y += dy; z += dz;
                        }
                    }
                }
            }
        }
    }
    qInfo() << "worldgen: ores placed = coal" << placedByKind[6] << "copper" << placedByKind[5]
            << "iron" << placedByKind[4] << "gold" << placedByKind[1]
            << "diamond" << placedByKind[0] << "lapis" << placedByKind[2]
            << "redstone" << placedByKind[3]; // 同 seed → 同计数（确定性核对）
}

// t761 沙砾矿袋（见 world.h 头注释）。机制等价 MC 1.0 地下 gravel 砾石袋：地下浅层小团 Gravel 替换 Stone。
//   确定性散布（hashColumn + seed 偏移，PLAN §2-K），结构同 placeUndergroundWaterPools 的「网格 + 概率筛选
//   + 抖动」模式，但产出不是空腔而是**材质替换**（只把 Stone 换成 Gravel；不动基岩 / 矿石 / dirt / 既有
//   洞穴 air → 与洞穴重叠时洞壁一圈沙砾、与矿石相邻互不覆盖，机制等价 MC 砾石袋被洞穴切穿暴露于洞壁）。
//   密度低（网格 / 概率常量可调，每图十余小袋）+ **浅层带**（地表下 [kShallowMin, kShallowMax]，玩家下挖
//   几格即遇——机制等价 MC gravel 浅层常见；留 ≥4 格顶盖防直接露天成「砾石丘」）。pass 序：scatterOres 之后
//   （矿石先占位、砾袋不覆盖矿石）/ carveCaves 之前（后到的 carve 切穿矿袋 → 洞壁裸露沙砾，同矿石暴露语义）。
//   海域列不跳过：纯替换无空腔（对比 placeUndergroundWaterPools 挖空腔须避海水柱），海底之下砾石袋自然。
void World::placeGravelPockets()
{
    constexpr int     kPocketGrid = 16;     // 候选网格间距（密度主旋钮：越大越稀；同 kPoolGrid 量级）
    constexpr unsigned kPocketPct = 45u;    // 候选命中概率（密度副旋钮：越大越多；t761 取值 → 每图约十余袋）
    constexpr int     kBedrockTop = 4;      // 不动基岩（同 carveCaves / placeBedrock）
    constexpr int     kShallowMin = 4;      // 浅层带上界：地表下至少几格（保顶盖封闭，同 kSurfaceCeil 语义）
    constexpr int     kShallowMax = 18;     // 浅层带下界：地表下至多几格（浅层富集；调大 → 深处也有）

    int placed = 0;
    const int pocketSeed = m_seed + 7610; // 矿袋哈希偏移（与其它 worldgen hashColumn 解耦）
    for (int bx = kPocketGrid / 2; bx < m_width; bx += kPocketGrid) {
        for (int bz = kPocketGrid / 2; bz < m_depth; bz += kPocketGrid) {
            const quint32 r = hashColumn(pocketSeed, bx, bz);
            if ((r % 100u) >= kPocketPct) continue; // 概率筛选
            const int span = kPocketGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < 3 || cz < 3 || cx >= m_width - 3 || cz >= m_depth - 3) continue; // 留 3 格边界（半径 ≤3 不越界）
            const int h = std::min(heightAt(cx, cz), m_height - 1);
            // 浅层带 y 范围：地表下 [kShallowMin, kShallowMax]（顶盖 ≥4 格；浅层富集）。
            const int yHi = h - kShallowMin;
            const int yLo = std::max(kBedrockTop + 1, h - kShallowMax);
            if (yHi <= yLo) continue; // 此列地下空间不足（极浅 / 极矮列）→ 跳过
            const int cy = yLo + int((r >> 9) & 0x1Fu) % (yHi - yLo + 1);
            const int rad = 2 + int((r >> 14) & 1u);       // 水平半径 2..3（小袋）
            const int layers = 1 + int((r >> 16) & 1u) + int((r >> 17) & 1u); // 竖向 1..3 层（小团非薄饼）

            // 小团替换（水平圆盘 × 竖向 layers 层，竖向居中）：仅替换 Stone → Gravel。不动基岩 / 矿石 /
            //   dirt / air / 水 → 与既有特征天然共存。沙砾受重力但 worldgen 静态放置即稳态（支撑判定在
            //   游玩期破坏时才触发，同沙海盘沙柱惯例）。
            const int rad2 = rad * rad;
            for (int dy = 0; dy < layers; ++dy) {
                const int yy = cy + dy - (layers - 1) / 2;
                if (yy <= kBedrockTop || yy >= m_height) continue;
                for (int dx = -rad; dx <= rad; ++dx) {
                    for (int dz = -rad; dz <= rad; ++dz) {
                        if (dx * dx + dz * dz > rad2) continue; // 圆盘
                        if (m_chunks.blockAt(cx + dx, yy, cz + dz) == BlockRegistry::Stone)
                            m_chunks.setBlock(cx + dx, yy, cz + dz, BlockRegistry::Gravel);
                    }
                }
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: gravel pockets =" << placed; // 同 seed → 同计数（确定性核对）
}

// t278 洞穴隧道生成（PLAN §2-K 确定性；spec「3D Perlin 阈值 / random-worm 隧道 + 分叉路口；内部黑暗；连通性」）。
//   两套叠加，互补：
//   ── (a) 3D Perlin 阈值洞（蜿蜒管状洞穴）────────────────────────────────────────────────────────
//   遍历地下 stone/dirt/ore 格（y ∈ (bedrockTop, h-4]），取两路**偏移** noise3（同噪声场不同坐标偏移 → 解耦），
//   两路同时高于阈值才挖空。单路阈值给 blobby 洞穴（连通性差）；两路交集把 blobby 收敛成更细长的管（接近
//   MC 1.0 Perlin 洞穴形态 —— 「两 noise 的交集」天然是 1-流形管状区域）。spec「3D Perlin 阈值」即此路径。
//   ── (b) Perlin worm 隧道 + 分叉（连通隧道网 + Y/十字路口）──────────────────────────────────────
//   确定性起点（hashColumn 散布网格 + 概率筛选），worm 沿 noise3 扰动方向逐球 carve（球重叠 → 连续管），
//   定期分叉：子 worm 偏转 yaw（±30°..90°）→ 与父 worm 在分叉点交汇成 Y 形路口；不同 worm 的管相交成十字路口。
//   spec「random-worm 隧道 + 分叉路口 / 连通性」即此路径。worm 总预算上限防最坏情况爆炸。
//
//   范围限定（spec「内部黑暗」）：y ∈ (bedrockTop, h-4] —— 不动基岩底层（hardness=-1 不可破，作世界底），
//   保留表面 grass/dirt（y ∈ [h-2, h]）+ ≥1 格石顶（y = h-3）→ 洞穴**封闭**于地下，与地表之间有完整石层相隔。
//   故 recomputeLightField（本 pass 之后跑）的天光 BFS 从列顶首个实体（= 表面 grass）向非遮光邻格衰减传播，
//   被完整石层挡住、绝不渗入洞内 → 洞穴天然黑暗（机制等价 MC「封闭洞穴无天光」；洞口 / 天坑会漏天光属后续任务）。
//
//   确定性：noise3 / hashColumn / hashVoxel 均纯函数于 seed → worm 起点位 / 方向轨迹 / 分叉决策 / 阈值噪声
//   全确定 → 同 seed 同 seed 同栅格（PLAN §2-K）。worm 方向依赖 noise3（位置 → 噪声 → 方向 → 下一位置），
//   整条链纯函数 → 路径完全可复现。wormId / stepIndex 喂 hashVoxel 做分叉判定（与矿石 hashVoxel 用真实体素
//   坐标正交 —— z 槽取常量 0x7027 远超世界 z 范围 → 不与矿石哈希冲突）。
//
//   性能：80×80×64 世界，(a) 阈值扫 ~190k 格 × 2 noise3 ≈ 数 ms；(b) worm ~30 起点 + 分叉 ≤80 worm × 80 步 ×
//   ~25 体素/球 ≈ 160k 写 + ~12k noise3 ≈ 数十 ms。worldgen 一次性可接受。挖走 stone/dirt/ore 暴露矿石于洞壁
//   （t279 洞穴裸露矿物直接读栅格即得）。经 m_chunks.setBlock 直写（跨 chunk 路由 + 标脏 + heightmap 增量维护），
//   不发 blockBroken（worldgen 既有约定——系统事件非玩家破块）。
void World::carveCaves()
{
    constexpr int kBedrockTop   = 4;     // 基岩层上界（与 placeBedrock 同源；不挖基岩）
    constexpr int kSurfaceCeil  = 4;     // 表面之下留几格（保 ≥1 石顶：dirt 在 [h-2,h-1]、grass 在 h，故 h-3 起挖则留 y=h-3 石顶）

    // ── (a) 3D Perlin 阈值洞穴 ──
    constexpr double kNoiseFreq   = 0.06;   // 阈值噪声频率（特征尺度 ~16 格 → 洞穴数格宽）
    constexpr double kNoiseOffset = 100.5;  // 第二路噪声坐标偏移（与第一路解耦）
    constexpr double kCarveThresh = 0.32;   // 两路 noise3 都 > 此值才挖空（交集 → 蜿蜒管状洞穴）。
                                            //   noise3 实测 σ≈0.28（按 worldgen 日志回算）→ 两路 0.32 交集 ≈ 2%
    //   地下体素被挖 → 蜿蜒走廊式洞穴（机制等价 MC 1.0 Perlin 洞穴）。

    int noiseCarved = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int h = std::min(heightAt(x, z), m_height - 1);
            const int yMax = h - kSurfaceCeil; // 留表面 + ≥1 石顶
            for (int y = kBedrockTop + 1; y <= yMax; ++y) {
                const quint8 b = m_chunks.blockAt(x, y, z);
                if (b == BlockRegistry::Air)     continue; // 已空（他处挖过）不重复
                if (b == BlockRegistry::Bedrock) continue; // 基岩不可破
                if (b == BlockRegistry::Water)   continue; // 防御（地下应无水）
                const double fx = x * kNoiseFreq, fy = y * kNoiseFreq, fz = z * kNoiseFreq;
                const double n1 = noise3(fx, fy, fz);
                const double n2 = noise3(fx + kNoiseOffset, fy, fz + kNoiseOffset);
                if (n1 > kCarveThresh && n2 > kCarveThresh) {
                    m_chunks.setBlock(x, y, z, BlockRegistry::Air);
                    ++noiseCarved;
                }
            }
        }
    }

    // ── (b) Perlin worm 隧道 + 分叉 ──
    constexpr int    kWormGrid   = 16;     // worm 起点网格间距（每 ~16×16 区域 1 候选 → 160×160 约 50 起点）
    constexpr int    kWormLife   = 60;     // worm 主寿命（步）
    constexpr double kWormStep   = 0.75;   // 步距（< 半径 → 球重叠成连续管，无断点）
    constexpr double kWormRadius = 1.5;    // 管半径（直径 ~3 格，可通行；MC 1.0 洞穴常 ~2-3 格宽）
    constexpr int    kMaxWorms   = 150;    // 全场 worm 总预算（含分叉；**须 > 起点数** 否则分叉永不触发）
    constexpr double kTurnRate   = 0.20;   // 单步 yaw 扰动上限（弧度，~11° → 平滑曲率）
    constexpr double kPitchRate  = 0.10;   // 单步 pitch 扰动上限（弧度，~6°）
    constexpr double kPitchClamp = 0.55;   // 俯仰钳制（防管变井 / 陡穿地层；~32°）
    constexpr int    kForkEvery  = 18;     // 每 N 步判定一次分叉
    constexpr unsigned kForkPct  = 35u;    // 分叉概率（%；判定时机满足时）

    // 球形 carve：把 (px,py,pz) 半径 r 内的实体天然方块（非 air/bedrock/water）置 air。
    //   体素中心 = 整数坐标 +0.5；距离比球半径平方（避免 sqrt）。边界格越界跳过。
    auto carveSphere = [&](double px, double py, double pz, double r) {
        const int ir = int(r) + 1;
        const int cx = int(std::floor(px)), cy = int(std::floor(py)), cz = int(std::floor(pz));
        const double r2 = r * r;
        for (int oy = -ir; oy <= ir; ++oy)
            for (int ox = -ir; ox <= ir; ++ox)
                for (int oz = -ir; oz <= ir; ++oz) {
                    const double gx = double(cx + ox) + 0.5 - px;
                    const double gy = double(cy + oy) + 0.5 - py;
                    const double gz = double(cz + oz) + 0.5 - pz;
                    if (gx * gx + gy * gy + gz * gz > r2) continue;
                    const int x = cx + ox, y = cy + oy, z = cz + oz;
                    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth) continue;
                    const quint8 b = m_chunks.blockAt(x, y, z);
                    if (b == BlockRegistry::Air || b == BlockRegistry::Bedrock || b == BlockRegistry::Water) continue;
                    m_chunks.setBlock(x, y, z, BlockRegistry::Air);
                }
    };

    // worm 状态：位置 + 方向（球坐标 yaw/pitch）+ 寿命 + id（确定性喂 hashVoxel 做分叉判定）。
    struct Worm { double x, y, z, yaw, pitch; int life; int id; };
    std::vector<Worm> worms;
    int nextWormId = 0;

    // 确定性起点：hashColumn 散布网格（seed +7919 偏移 → 与树/草的 hashColumn(m_seed,...) 解耦）。
    //   每格 1 候选：hash 低位 50% 概率生 worm（密度控制：网格 + 概率双重）；起点在 cell 内 ±抖动；
    //   起始 yaw 由 hash 高位派生（0..2π 全方位）→ worm 朝向各异。y 选 [bedrockTop+2, h-ceil-1] 内随机层。
    int placedStarts = 0;
    const int caveSeed = m_seed + 7919; // 洞穴哈希偏移（与树/草 hashColumn 解耦；纯整数加，确定性）
    for (int bx = kWormGrid / 2; bx < m_width; bx += kWormGrid) {
        for (int bz = kWormGrid / 2; bz < m_depth; bz += kWormGrid) {
            const quint32 r = hashColumn(caveSeed, bx, bz);
            if ((r & 1u) == 0u) continue; // 50% 概率生 worm（密度控制）
            // cell 内 ±span/2 抖动（避免网格化排列的机械感）
            const int span = kWormGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int sx = bx + jx, sz = bz + jz;
            if (sx < 1 || sz < 1 || sx >= m_width - 1 || sz >= m_depth - 1) continue; // 留 1 格边界
            const int h = std::min(heightAt(sx, sz), m_height - 1);
            const int yLo = kBedrockTop + 2;
            const int yHi = h - kSurfaceCeil - 1;
            if (yHi <= yLo) continue; // 此列地下空间不足（极低洼 / 水下）→ 跳过
            const int yRange = yHi - yLo + 1;
            const int sy = yLo + int((r >> 9) & 0x1Fu) % yRange;
            const double yaw0 = double((r >> 14) & 0x3FFu) / 1024.0 * 6.28318530717958647692; // 0..2π
            worms.push_back({double(sx) + 0.5, double(sy) + 0.5, double(sz) + 0.5, yaw0, 0.0, kWormLife, nextWormId++});
            ++placedStarts;
        }
    }

    // 推进 worm 队列（索引循环：子 worm 入队尾，wi 跟进 → 宽度优先展开整张隧道网）。分叉仅在总数 < kMaxWorms
    //   时允许（预算上限）。worm 出界（x/z 边 / y 越基岩顶或世界顶）即杀（break），不留悬空段。
    //   t759 修悬空引用：引用须在 while 每步重取 —— 分叉 push_back 可能使 vector 扩容搬移存储，跨步持有的
    //   `Worm &` 指向已释放旧缓冲（UB；是否崩取决于堆布局，重复重生成世界（改种子/尺寸）可复现崩溃 ——
    //   修法=每步经 worms[wi] 现取，push_back 后下一步自动绑到新存储）。
    int wormSteps = 0;
    for (size_t wi = 0; wi < worms.size(); ++wi) {
        int step = 0;
        while (worms[wi].life > 0) {
            Worm &w = worms[wi]; // 每步重取（t759：分叉扩容后旧引用悬空）
            carveSphere(w.x, w.y, w.z, kWormRadius);
            ++wormSteps;
            // 方向扰动：noise3 采样 worm 头位置 → 平滑曲线（位置驱动，纯函数 → 路径可复现）。
            const double df = 0.10;
            const double n1 = noise3(w.x * df,           w.y * df, w.z * df);
            const double n2 = noise3(w.x * df + 33.3,    w.y * df, w.z * df - 17.7);
            w.yaw   += n1 * kTurnRate;
            w.pitch += n2 * kPitchRate;
            if (w.pitch >  kPitchClamp) w.pitch =  kPitchClamp;
            if (w.pitch < -kPitchClamp) w.pitch = -kPitchClamp;
            // 推进（球坐标 → 笛卡尔方向 × 步距）。yaw 决定水平朝向、pitch 决定垂直分量（yaw 不影响 y）。
            const double cp = std::cos(w.pitch);
            w.x += cp * std::cos(w.yaw) * kWormStep;
            w.y +=     std::sin(w.pitch) * kWormStep;
            w.z += cp * std::sin(w.yaw) * kWormStep;
            // 出界 → 杀 worm。
            if (w.x < 1.0 || w.z < 1.0 || w.x >= double(m_width) - 1.0 || w.z >= double(m_depth) - 1.0) break;
            if (w.y < double(kBedrockTop + 1) || w.y >= double(m_height) - 1) break;
            ++step;
            --w.life;
            // 分叉：每 kForkEvery 步、且 worm 总数 < kMaxWorms 时，按 hashVoxel(seed,id,step,0x7027) % 100 概率生子。
            //   子 worm：yaw 偏转 ±30°..90°（偏转角由 hash 派生，符号随机）→ 与父 worm 在分叉点交汇成 Y 形路口；
            //   pitch 取父 pitch 反向减半（子趋向不同深度）；寿命 = 父寿命 2/3（子隧道较短）。spec「分叉路口」即此。
            if ((step % kForkEvery) == 0 && int(worms.size()) < kMaxWorms) {
                const quint32 fr = hashVoxel(m_seed, w.id, step, int(0x7027));
                if ((fr % 100u) < kForkPct) {
                    const double turn = (double((fr >> 8) & 0x3FFu) / 1024.0) * 1.04719755119659774615 + 0.52359877559829887308; // ~30°..90°
                    const double sign = ((fr >> 18) & 1u) ? -1.0 : 1.0;
                    Worm child = w;
                    child.id    = nextWormId++;
                    child.yaw   += sign * turn;
                    child.pitch = -w.pitch * 0.5;
                    child.life  = (w.life * 2) / 3;
                    if (child.life < 20) child.life = 20; // 子隧道至少 20 步（够形成可见分叉）
                    worms.push_back(child);
                }
            }
        }
    }

    qInfo() << "worldgen: caves carved = noise" << noiseCarved
            << "+ worm-steps" << wormSteps
            << "(starts" << placedStarts << "worms" << int(worms.size()) << ")"; // 同 seed → 同计数（确定性核对）
}

// t148 海平面填水（PLAN §2-K 确定性）：遍历列，地表高度 h < waterLevel 的低洼列从 h+1 到 waterLevel
//   填 Water（机制等价 MC 海洋 / 湖泊：低洼被水淹没到统一海平面）。仅在空气格写入（防御：不动地形 /
//   基岩 / 矿石）。经 m_chunks.setBlock 直写（跨 chunk 路由 + 标脏 + 边界邻接 + heightmap 增量维护），
//   不触发 blockPlaced（同 worldgen 既有约定——系统事件非玩家放置）。
//
//   waterLevel 取文件级 kWaterLevel（=58，见 world.cpp 顶部注释）：随 t307 地表抬高（基线 30→64）同源
//   抬高（24→58，保持「低于基线 6 格」相对几何）→ 低洼 hills 仍见水 / 沙滩带（比例同 t162/t274），
//   草原 / 沙漠主体无水（hills ~57..71 仅 57 低洼列见水）。出生列(80,80) 地表 ~64>58 保持陆地。t149 沙滩
//   带 / 沙漠水位 / 树·矿石阈值均同源用此常量（generate 沙表层 / placeTrees / scatterOres 阈值 = waterLevel+1）。
//   全程纯函数于 seed + heightAt（fbm）→ 同 seed 同水域分布；禁用任何运行期随机源（PLAN §2-K）。
void World::fillWater()
{
    int waterCells = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            // t338：海水仅集中于海域一角（seaColumnHeight >= 0 的海盆，seaH < waterLevel）。旧「全域低洼列
            //   (h<waterLevel) 灌水」已移除 → 内陆低洼列不再产散布水洼（spec「内陆无散沙 / 散水」，沙随水走）。
            //   海域海底 = seaColumnHeight（与 generate 填充一致）；沙滩环（seaH=waterLevel+1）高于海平面不灌水。
            const int seaH = seaColumnHeight(x, z);
            if (seaH < 0) continue;              // 内陆不灌水（消除散布水洼）
            if (seaH >= kWaterLevel) continue;   // 沙滩环（waterLevel+1）高于海平面 → 无水
            // 从海底上方一格到海平面填水（seaH+1..kWaterLevel）。海盆被水淹没到统一海平面。
            for (int y = seaH + 1; y <= kWaterLevel && y < m_height; ++y) {
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air)
                    continue; // 仅写空气格（防御：不动已存在方块）
                m_chunks.setBlock(x, y, z, BlockRegistry::Water);
                ++waterCells;
            }
        }
    }
    qInfo() << "worldgen: water cells =" << waterCells; // 同 seed → 同计数（确定性核对）
}

// t341 山坡洞口（见 world.h 头注释）。机制等价 MC 1.0 山坡洞口 / 天坑：在「山坡腰」列把既有地下洞穴网络与
//   地表连通，天光经洞口 BFS 渗入洞内（recomputeLightField 在本 pass 之后跑）。
//   t339 移除了原 t309 的「向下 1×1 竖井」（无洞穴时挖出规整 1 格宽垂直气柱 = 地下「矿井」感）。t341 重写为三步：
//   (1) 山坡过滤 —— 列必须处于坡腰（既有更高邻列 = 非峰、也有更低邻列 = 非谷，且最大高差 ≥ kSlopeDrop）→
//       洞口落在坡面（halfway up），低侧地形已低于洞口 → 该侧壁天然裸露可走入、高侧深入山体 = 山坡洞口观感。
//   (2) 洞穴连通过滤 —— 仅当该列近表（surfaceY-1 向下 kMaxReach 内）存在 carveCaves 已挖出的 cave air 才开口；
//       找不到则跳过 → **永不产孤立竖井**（修 t339「无洞挖出矿井」问题：每个洞口都连真实洞穴）。
//   (3) 大洞口 —— 3×3 水平（x/z 各 ±1）× 自 surfaceY 下挖到 caveY（垂直，含洞穴顶格）= 可通行（2-3 宽 × 数格高，
//       玩家 0.6×1.8 轻松进出；修 t309 的「1×1 竖井 + 3×3 仅 1 格浅坑」太窄不可走）。
//   确定性散布（hashColumn + seed 偏移，PLAN §2-K）：更密网格 + 更高概率（grid 10 / 60% vs 旧 18 / 30% → 更多洞口）
//   + 网格内抖动 → 候选列；再经「山坡 + 近表有洞」双重几何过滤。仅 plains/forest/hills（hills amp 7 自然有坡、
//   forest t341 amp 5 新增坡；plains amp 2 平坦 → 山坡过滤天然排除）；跳过沙漠（沙底无草土、不像洞口）/ 海域
//   （海 + 沙滩，避免海水灌入 / 沙底）/ 低洼（surfaceY <= waterLevel+2 → 洞口会灌海水）。经 m_chunks.setBlock
//   直写（跨 chunk 路由 + 标脏 + heightmap 增量维护），不发 blockBroken（worldgen 既有约定）。纯函数于 seed
//   （hashColumn + 已生成 chunk 的纯几何查询）→ 同 seed 同洞口分布。
void World::carveCaveEntrances()
{
    constexpr int kEntranceGrid   = 10;      // 候选网格间距（比旧 t309 的 18 更密 → 更多洞口）
    constexpr unsigned kEntrancePct = 60u;   // 候选命中概率（%；比旧 30 更高 → 更多洞口）
    constexpr int kBedrockTop      = 4;      // 不挖基岩（与 carveCaves / placeBedrock 同源）
    constexpr int kMaxReach        = 6;      // 自地表向下找洞穴的最大扫描格数（找不到则不开口 → 无孤立竖井；浅 sinkhole）
    constexpr int kSlopeDrop       = 2;      // 山坡判定：邻列最大高差 ≥ 此值（真坡面，非平坦）
    constexpr int kMouthHalf       = 1;      // 开口半宽（3×3 = ±1）

    int placed = 0;
    const int entranceSeed = m_seed + 3091;  // 洞口哈希偏移（与树 / 草 / 洞穴 hashColumn 解耦；纯整数加，确定性）
    for (int bx = kEntranceGrid / 2; bx < m_width; bx += kEntranceGrid) {
        for (int bz = kEntranceGrid / 2; bz < m_depth; bz += kEntranceGrid) {
            const quint32 r = hashColumn(entranceSeed, bx, bz);
            if ((r % 100u) >= kEntrancePct) continue; // 概率筛选
            // 网格内 ±span/2 抖动（避免网格化排列的机械感，同 carveCaves worm 起点抖动）。
            const int span = kEntranceGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int x = bx + jx, z = bz + jz;
            if (x < 2 || z < 2 || x >= m_width - 2 || z >= m_depth - 2) continue; // 留 2 格边界（3×3 开口不越界）
            if (seaColumnHeight(x, z) >= 0) continue; // 海域（海 + 沙滩）不开口（避免海水灌入 / 沙底）
            const Biome bio = biomeAt(x, z);
            if (bio == Biome::Desert) continue; // 沙漠沙底不开口（无草 / 土 → 暴露纯沙不像洞口）

            const int surfaceY = std::min(heightAt(x, z), m_height - 1);
            if (surfaceY <= kWaterLevel + 2) continue; // 避开沙滩 / 水下 / 低洼（洞口不应灌入海水）

            // 山坡判定（t341）：列必须处于「坡腰」—— 4 邻列既有严格更高（非峰）也有严格更低（非谷），且最大
            //   高差 ≥ kSlopeDrop（真坡面）。平坦地（plains amp 2）四邻 ≈ surfaceY → hMaxNb/hMinNb 都 ≈ surfaceY
            //   → 两个严格不等式之一必假 → 跳过；故平原天然无洞口，洞口只落在有起伏的 hills / forest 坡面。
            const int n1 = std::min(heightAt(x + 1, z), m_height - 1);
            const int n2 = std::min(heightAt(x - 1, z), m_height - 1);
            const int n3 = std::min(heightAt(x, z + 1), m_height - 1);
            const int n4 = std::min(heightAt(x, z - 1), m_height - 1);
            const int hMaxNb = std::max({n1, n2, n3, n4});
            const int hMinNb = std::min({n1, n2, n3, n4});
            if (hMaxNb <= surfaceY) continue;          // 无严格更高邻列 = 局部峰 → 不开口
            if (hMinNb >= surfaceY) continue;          // 无严格更低邻列 = 局部谷 → 不开口
            if (hMaxNb - hMinNb < kSlopeDrop) continue; // 坡度不足 → 平坦地不开口

            // 自地表向下找既有洞穴 air（carveCaves 已挖空）。找不到 → 该列近表无洞，不开口（杜绝孤立竖井）。
            int caveY = -1;
            const int yFloor = std::max(kBedrockTop + 1, surfaceY - kMaxReach);
            for (int y = surfaceY - 1; y >= yFloor; --y) {
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Air) { caveY = y; break; }
            }
            if (caveY < 0) continue; // 近表无洞穴 → 不开口（每个洞口都连真实洞穴，无「矿井」式孤立竖井）

            // 开口（t341）：3×3 水平（x/z 各 ±kMouthHalf）× 自 surfaceY 下到 caveY（垂直，含洞穴顶格）= 可通行大洞口。
            //   仅挖实体天然方块（grass/dirt/stone/ore），不动 air（已是空）/ bedrock / water（防御，本层应无水）。
            for (int dy = 0; dy <= surfaceY - caveY; ++dy) {
                const int y = surfaceY - dy;
                for (int dx = -kMouthHalf; dx <= kMouthHalf; ++dx)
                    for (int dz = -kMouthHalf; dz <= kMouthHalf; ++dz) {
                        const quint8 b = m_chunks.blockAt(x + dx, y, z + dz);
                        if (b == BlockRegistry::Air || b == BlockRegistry::Bedrock || b == BlockRegistry::Water) continue;
                        m_chunks.setBlock(x + dx, y, z + dz, BlockRegistry::Air);
                    }
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: cave entrances =" << placed; // 同 seed → 同计数（确定性核对）
}

// t342 大峡谷地貌（见 world.h 头注释）。机制等价 MC ravine / 真实大峡谷：一条贯穿地图的长窄露天裂缝，两侧立壁
//   纵贯矿层带 → 峡壁裸露矿石。
//   路径 = 长程 worm：自地图边界附近确定性出发（hashColumn 选边 + 沿边散布起点），朝对侧 baseYaw 方向行进；
//   yaw 受 noise2 平滑扰动 + 向 baseYaw 的弱回复力（kRestore）→ 蜿蜒长裂缝（非直线、非急转、保证贯穿而非打转），
//   步距 < 底半径 → 水平盘重叠成连续长沟。出界（抵达对侧边缘）即停。
//   横截面 = 上宽下窄阶梯 V 形：逐层 y 自峡谷底到地表，半径 = 底半径 + (y 进度)*顶额外 + fbm 峡壁调制（弯曲不规则，
//   非完美圆柱）；顶部宽 / 底部窄 = 真实峡谷剖面（spec「widening slightly」）。kFloor 选在矿层带（煤 8+ / 铜 / 铁 /
//   金 5+）之内 → 两侧立壁纵贯多矿层 → carve 暴露矿石于峡壁（spec「内壁露矿石」；worldgen 顺序 scatterOres →
//   carveCaves → ... → carveCanyon：矿石先布好，峡谷再 carve，壁面矿石显）。
//   **露天**：自峡谷底到地表全挖（清除 grass/dirt → 天光直入；recomputeLightField 在本 pass 之后跑 → BFS 自峡谷顶
//   向下衰减，峡谷明亮而非黑，与封闭地下洞穴相反）。不动基岩底层（kFloor > bedrockTop）、不动 air / 水；跳过海域列
//   （seaColumnHeight >= 0，峡谷为陆地地貌、不与海角水互动）。确定性：起点 / 朝向 / 路径全纯函数于 seed
//   （hashColumn + noise2 / fbm）→ 同 seed 同峡谷（PLAN §2-K）。~1 条/图：单条 worm（无散布网格）→ 每图约 1 条贯穿峡谷。
//   经 m_chunks.setBlock 直写（跨 chunk 路由 + 标脏 + heightmap 增量维护），不发 blockBroken（worldgen 既有约定）。
void World::carveCanyon()
{
    constexpr int kFloor       = 22;    // 峡谷底 y（远高于基岩层 0..4，carveDisc 另跳过 Bedrock；落在矿层带内 → 峡壁裸露煤/铜/铁/金矿层）
    constexpr int kBaseRadius  = 3;     // 底部半径（窄底；直径 ~6）
    constexpr int kTopExtra    = 2;     // 顶部额外半径（上宽下窄阶梯；顶半径 = base + extra ~5，spec「widening slightly」）
    constexpr double kStep     = 0.7;   // 步距（< 底半径 → 水平盘重叠成连续长沟）
    constexpr double kTurnRate = 0.05;  // 噪声 yaw 扰动系数（单步 ~2.9° → 长程缓弯）
    constexpr double kRestore  = 0.02;  // 向 baseYaw 的弱回复系数（保证贯穿地图而非原地打转）
    constexpr int kMaxSteps    = 280;   // 最大步数（步距 0.7 + 回复 → ~150 格贯穿 160 地图；出界即停）
    constexpr double kWallFreq = 0.21;  // 峡壁 fbm 频率（弯曲峡壁 / 不规则半径调制）

    // ── 确定性起点 + 朝向 ── 选起始边（0=z- / 1=z+ / 2=x- / 3=x+），沿边 hash 散布起点；baseYaw 朝对侧（向地图内）。
    const int canyonSeed = m_seed + 9342; // 峡谷哈希偏移（与其它 worldgen hashColumn 解耦；纯整数加，确定性）
    const quint32 r0 = hashColumn(canyonSeed, 7, 7);
    const unsigned edge = r0 & 3u;
    const double t = double((r0 >> 2) % 1000u) / 1000.0;                       // 0..1 沿边位置
    const double yawJitter = (double((r0 >> 12) & 0x3FFu) / 1024.0 - 0.5) * 0.8; // 起始 ±0.4 rad 偏转
    const double inset = 10.0;                                                  // 起点距边界（留余量不贴边）
    double px, pz, baseYaw;
    switch (edge) {
        case 0: px = 12.0 + t * (m_width  - 24.0); pz = inset;                   baseYaw =  1.57079632679489661923; break; // +z（朝南）
        case 1: px = 12.0 + t * (m_width  - 24.0); pz = double(m_depth) - inset; baseYaw = -1.57079632679489661923; break; // -z（朝北）
        case 2: px = inset;                       pz = 12.0 + t * (m_depth - 24.0); baseYaw = 0.0;                          break; // +x（朝东）
        default:px = double(m_width) - inset;     pz = 12.0 + t * (m_depth - 24.0); baseYaw =  3.14159265358979323846;  break; // -x（朝西）
    }
    double yaw = baseYaw + yawJitter;

    // 水平盘 carve（中心 cx/cz、高度 y、半径 r）：盘内实体天然方块（非 air/bedrock/water）置 air；跳过海域列。
    //   体素中心 = 整数坐标 +0.5；距离比半径平方（避免 sqrt）。盘半径 = V 形剖面按 y 插值 + fbm 峡壁调制。
    int carvedVoxels = 0;
    auto carveDisc = [&](double cx, double cz, int y, double r) {
        const int ir = int(r) + 1;
        const int icx = int(std::floor(cx)), icz = int(std::floor(cz));
        const double r2 = r * r;
        for (int ox = -ir; ox <= ir; ++ox)
            for (int oz = -ir; oz <= ir; ++oz) {
                const double gx = double(icx + ox) + 0.5 - cx;
                const double gz = double(icz + oz) + 0.5 - cz;
                if (gx * gx + gz * gz > r2) continue;
                const int x = icx + ox, z = icz + oz;
                if (x < 0 || z < 0 || x >= m_width || z >= m_depth) continue;
                if (seaColumnHeight(x, z) >= 0) continue; // 海域（海 + 沙滩）不开峡（峡谷为陆地地貌）
                const quint8 b = m_chunks.blockAt(x, y, z);
                // t376：地下水池（placeUndergroundWaterPools，先于峡谷）若与峡谷相交，carve 会把池水暴露给
                //   峡谷空气 → 即便 t350 已限流，峡壁仍会从边缘池水渗出。故盘内水格一并挖空（排干）保峡谷干涸；
                //   盘外池水仍被实体岩封闭（稳态）。盘外的边缘渗水由下方 post-pass 排水带兜底。仅跳过 air / 基岩。
                if (b == BlockRegistry::Air || b == BlockRegistry::Bedrock) continue;
                m_chunks.setBlock(x, y, z, BlockRegistry::Air);
                ++carvedVoxels;
            }
    };

    // ── 推进 worm，逐层 carve V 形剖面 ──
    //   t376：同时记录路径中心（ix,iz + surfaceY + 朝向 yaw），供 carve 后 post-pass 用：
    //   排水带 —— 排干峡谷带内残余水（兜底盘外边缘池水渗出）；邻接侧洞 —— 沿峡壁刻短隧道连既有洞穴。
    //   t929 退役：旧 t601/t376「高源瀑布」（峡壁含水层门控检测 + 峡心柱悬空置一格水源）整体移除 ——
    //   悬空水源即用户实测的「峡谷中央孤立水方块直接掉落、旁边无支撑」（水源本身永不蒸发，但下方恒
    //   canyon air → tick 起即泄成孤立下落水柱）。机制定版：峡谷为干涸陆地地貌，worldgen 不产任何
    //   无支撑水源（PLAN 口径同 pruneFloatingSnowLayers：worldgen 自产悬空物一律不生成）。
    struct CanyonPt { int ix, iz, surfaceY, span; double yaw; };
    std::vector<CanyonPt> path;
    path.reserve(kMaxSteps);
    int steps = 0;
    for (int step = 0; step < kMaxSteps; ++step) {
        const int ix = int(px), iz = int(pz);
        if (ix < 1 || iz < 1 || ix >= m_width - 1 || iz >= m_depth - 1) break; // 出界（已贯穿到对侧）→ 停
        const int surfaceY = std::min(heightAt(ix, iz), m_height - 1);
        const int span = std::max(1, surfaceY - kFloor);
        // 自峡谷底到地表逐层 carve。半径 = base + (y 进度)*topExtra + fbm 峡壁调制 → 上宽下窄 + 弯曲不规则。
        for (int y = kFloor; y <= surfaceY; ++y) {
            const double f = double(y - kFloor) / double(span);                // 0（底）..1（顶）
            const double wall = fbm((double(ix) + m_seed) * kWallFreq + 5.5,
                                    (double(iz) + m_seed) * kWallFreq - 2.3);   // [-1,1] 峡壁噪声
            const double r = double(kBaseRadius) + f * double(kTopExtra) + 0.7 * wall;
            if (r <= 0.0) continue;
            carveDisc(px, pz, y, r);
        }
        path.push_back({ix, iz, surfaceY, span, yaw});
        // 推进（水平面内；yaw 决定朝向，无 pitch → 长程水平裂缝）。噪声缓弯 + 向 baseYaw 回复（保证贯穿）。
        const double df = 0.05;
        const double n = noise2(px * df + 11.1, pz * df - 7.7);                // [-1,1] 平滑扰动
        yaw += n * kTurnRate + (baseYaw - yaw) * kRestore;
        px += std::cos(yaw) * kStep;
        pz += std::sin(yaw) * kStep;
        ++steps;
    }

    // t376 排水带：盘外（半径 > 当前盘半径）仍可能有地下水池残水紧贴峡壁 → 暴露后渗出。沿路径中心以
    // 固定半径（kDrainRadius = 盘上限 + 2 余量）逐柱排干 [kFloor, surfaceY] 内的水格 → 峡谷带内无水可
    // 渗。池水远端（带外）仍被实体岩封闭（稳态）。一次 worldgen 开销可接受。
    constexpr int kDrainRadius = kBaseRadius + kTopExtra + 2;
    int drainedCells = 0;
    for (const CanyonPt &p : path) {
        const int R2 = kDrainRadius * kDrainRadius;
        for (int ox = -kDrainRadius; ox <= kDrainRadius; ++ox)
            for (int oz = -kDrainRadius; oz <= kDrainRadius; ++oz) {
                if (ox * ox + oz * oz > R2) continue;
                const int x = p.ix + ox, z = p.iz + oz;
                if (x < 0 || z < 0 || x >= m_width || z >= m_depth) continue;
                if (seaColumnHeight(x, z) >= 0) continue; // 海域不排（海独立于峡谷）
                const int topY = std::min(p.surfaceY, m_height - 1);
                for (int y = kFloor; y <= topY; ++y) {
                    if (m_chunks.blockAt(x, y, z) == BlockRegistry::Water) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::Air);
                        ++drainedCells;
                    }
                }
            }
    }

    // t376 (2) 邻接侧洞：每 kSideEvery 个路径点刻一条垂直于峡谷走向的短隧道进峡壁 → 可探索的壁龛，
    //   常与 carveCaves 已有的洞穴网络（壁后）连通。复用 carveDisc（同款排干 + 海域跳过），三层 y 保通行。
    constexpr int    kSideEvery  = 24;     // 每隔多少路径点刻一条侧洞（path ≤280 → 最多 ~11 条）
    constexpr int    kSideLen    = 7;      // 隧道长度（进壁格数）
    constexpr double kSideRadius = 1.6;    // 隧道半径（直径 ~3，可通行）
    constexpr double kHalfPi     = 1.57079632679489661923;
    int sideCaves = 0;
    for (size_t i = kSideEvery / 2; i < path.size(); i += kSideEvery) {
        const CanyonPt &p = path[i];
        const double sign = (((i / kSideEvery) & 1u) != 0u) ? 1.0 : -1.0; // 索引奇偶定侧（确定性）
        const double perpYaw = p.yaw + sign * kHalfPi;                    // 垂直于峡谷走向
        const double startOff = double(kBaseRadius + kTopExtra) + 1.0;    // 起点：贴峡壁外侧
        double tx = double(p.ix) + 0.5 + std::cos(perpYaw) * startOff;
        double tz = double(p.iz) + 0.5 + std::sin(perpYaw) * startOff;
        for (int s = 0; s < kSideLen; ++s) {
            for (int y = kFloor; y <= kFloor + 2; ++y)                     // 3 层高隧道，贴峡底可走入
                carveDisc(tx, tz, y, kSideRadius);
            tx += std::cos(perpYaw);
            tz += std::sin(perpYaw);
        }
        ++sideCaves;
    }

    // t929：旧 t376 (1c) 高源瀑布置源退役（峡心柱悬空一格 Water 源）——即用户实测「峡谷中央孤立水方块
    //   直接掉落、旁边无支撑」的来源；worldgen 不再在峡谷带内产任何水源（排水带 + carve 排干已清零，
    //   此处不再补新）。瀑布观感退役为干涸峡谷；游玩期玩家自行倒水仍可造瀑（tickWaterFlow 正常路径）。

    qInfo() << "worldgen: grand canyon carved =" << carvedVoxels
            << "(steps" << steps << "floor" << kFloor << ")"; // 同 seed → 同计数（确定性核对）
    qInfo() << "worldgen: canyon drained =" << drainedCells
            << "side caves =" << sideCaves; // t376 确定性核对（t929 瀑布源退役）
}

// t716 ③ 雪层支撑守卫（见 world.h 头注释）：全图扫 SnowLayer，正下方非实体（air / 水）→ 直删该雪层。
//   成因链：carveCanyon 盘顶 / carveCaveEntrances 3×3 开口顶取「中心列 surfaceY」，比中心高一格的邻列其
//   表面 SnowLayer 恰在 carve 顶之上 1 格、支撑格恰被挖 → 悬空细雪浮在峡谷 / 洞口上方（用户复盘「雪原
//   细雪悬浮峡谷上」）。守卫在所有 carve 类 pass 之后一次跑（generate 内 carveCanyon 后调用；也覆盖洞口 /
//   地下水池空腔等一切「雪层下方被挖空」的来源）。判定「非实体」用 !isSolid（air / 水 / cross / 雪层自身
//   等均非 solid——雪层下叠雪层是合法堆叠态（state 高度叠加），但 worldgen 不产叠层（surfaceY 单层），
//   叠层堆积只发生在游玩期塌落合并 → 下方 SnowLayer 仍会被本守卫误删？——不会：本函数仅在 worldgen 期
//   跑一次，游玩期塌落堆叠发生在其后（checkSnowLayerOnEdit 管游玩期失撑坍落，与本守卫分工不重叠）。
//   幂等：删除后重扫无变化；纯查询 + 直删（m_chunks.setBlock，不发 blockBroken——worldgen 约定）。
void World::pruneFloatingSnowLayers()
{
    int pruned = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            for (int y = 1; y < m_height; ++y) { // y=0 下方无格（基岩域），从 1 起
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::SnowLayer) continue;
                if (!BlockRegistry::isSolid(m_chunks.blockAt(x, y - 1, z))) {
                    m_chunks.setBlock(x, y, z, BlockRegistry::Air);
                    ++pruned;
                }
            }
        }
    }
    if (pruned > 0)
        qInfo() << "worldgen: pruned floating snow layers =" << pruned; // 同 seed → 同计数（确定性核对）
}

// t309 地下水池（见 world.h 头注释）。机制等价 MC 1.0 地下水湖 / 封闭水洼：地下深处小型封闭空腔 + 底层水源。
//   确定性散布（hashColumn + seed 偏移，PLAN §2-K）：网格采样 + 概率筛选 + 抖动 → 在地下 y 范围内选中心，
//   carve 一个小圆盘空腔（底层水源 + 上方 air 气室），空腔被周围实体岩石天然封闭 → 水源稳态（不蔓延）+ 黑暗。
//   y 范围 (bedrockTop+3, h-surfaceCeil-airAbove-1]：紧贴基岩之上 + 地表之下足够深（上方留石顶 → 封闭）。
//   经 m_chunks.setBlock 直写；纯函数于 seed → 同 seed 同水池分布。
void World::placeUndergroundWaterPools()
{
    constexpr int kPoolGrid      = 14;      // 候选网格间距
    constexpr unsigned kPoolPct  = 40u;     // 候选命中概率
    constexpr int kBedrockTop    = 4;       // 不动基岩（同 carveCaves / placeBedrock）
    constexpr int kSurfaceCeil   = 4;       // 与 carveCaves 同源（保地表下若干格不挖 → 水池上方有石顶封闭）
    constexpr int kAirAbove      = 2;       // 水面之上的空气层数（形成「水 + 气室」封闭空腔）

    int placed = 0;
    const int poolSeed = m_seed + 5309; // 水池哈希偏移（与其它 worldgen hashColumn 解耦）
    for (int bx = kPoolGrid / 2; bx < m_width; bx += kPoolGrid) {
        for (int bz = kPoolGrid / 2; bz < m_depth; bz += kPoolGrid) {
            const quint32 r = hashColumn(poolSeed, bx, bz);
            if ((r % 100u) >= kPoolPct) continue; // 概率筛选
            const int span = kPoolGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < 3 || cz < 3 || cx >= m_width - 3 || cz >= m_depth - 3) continue; // 留 3 格边界（半径 ≤3 不越界）
            if (seaColumnHeight(cx, cz) >= 0) continue; // t338：海域已有海，不叠地下水池（heightAt 为纯自然高度，会按自然高度算 y 范围误挖入海水柱）
            const int h = std::min(heightAt(cx, cz), m_height - 1);
            // 水池 y 范围：基岩之上 ~ 地表之下足够深（保上方有石顶 → 封闭黑暗）。
            const int yLo = kBedrockTop + 3;
            const int yHi = h - kSurfaceCeil - kAirAbove - 1;
            if (yHi <= yLo) continue; // 此列地下空间不足（极低洼 / 水下）→ 跳过
            const int yRange = yHi - yLo + 1;
            const int cy = yLo + int((r >> 9) & 0x1Fu) % yRange;
            const int rad = 2 + int((r >> 14) & 1u); // 半径 2..3

            // 挖圆盘空腔（disc × {底层水源 + 上方 kAirAbove 层 air}）。仅覆盖 disc 范围；不触碰外部岩壁 → 天然封闭。
            //   底层（cy）水源；cy+1..cy+kAirAbove 空气（气室）；其上保留原岩（石顶）。空腔被周围实体岩包围：
            //   disc 外（距离 > rad）是未挖的 stone/dirt → 水源水平邻居为水（disc 内）或实体（disc 外）→ 无 air 邻居
            //   → 不蔓延（tickWaterFlow 稳态）；下方（cy-1）实体 → 水源落地；气室上方实体 → 无天光（黑暗）。
            //   与既有洞穴重叠时（carveCaves 已挖空同位）→ 水源进洞穴底部、形成洞穴内水洼（也是 spec「地下水池」）。
            const int rad2 = rad * rad;
            for (int dx = -rad; dx <= rad; ++dx) {
                for (int dz = -rad; dz <= rad; ++dz) {
                    if (dx * dx + dz * dz > rad2) continue; // 圆盘
                    const int px = cx + dx, pz = cz + dz;
                    // 底层水源（覆盖既有 cave air / stone / ore，但不动 bedrock / 已有水）。
                    const quint8 fb = m_chunks.blockAt(px, cy, pz);
                    if (fb != BlockRegistry::Bedrock && fb != BlockRegistry::Water)
                        m_chunks.setBlock(px, cy, pz, BlockRegistry::Water);
                    // 水面之上空气层（气室）；越界 / 基岩不动。
                    for (int ay = 1; ay <= kAirAbove; ++ay) {
                        const int yy = cy + ay;
                        if (yy >= m_height) break;
                        const quint8 ab = m_chunks.blockAt(px, yy, pz);
                        if (ab == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(px, yy, pz, BlockRegistry::Air);
                    }
                }
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: underground water pools =" << placed; // 同 seed → 同计数（确定性核对）
}

// t343 地下岩浆湖（见 world.h 头注释）。机制等价 MC 1.0 地下岩浆湖：Y<30 封闭洞穴内的小型岩浆洼地。
//   确定性散布（hashColumn + seed 偏移，PLAN §2-K），结构与 placeUndergroundWaterPools 同源（圆盘空腔 + 底层源 +
//   上方气室），但填 Lava 源（state=0）且仅散布于 y < kLavaLakeMaxY(30) 的地下深处。空腔被周围实体岩封闭 →
//   岩浆源无水平 air 邻居 → 稳态（tickLavaFlow 不扩散）；气室无天光 → 黑暗（仅岩浆自发光暖色，但本工程岩浆段
//   走 NoLighting 材质非真光源，气室仍记为暗）。纯函数于 seed → 同 seed 同岩浆湖分布。
void World::placeLavaLakes()
{
    constexpr int kPoolGrid      = 16;      // 候选网格间距（比水池略稀 → 岩浆湖更稀有）
    constexpr unsigned kPoolPct  = 30u;     // 候选命中概率
    constexpr int kBedrockTop    = 4;       // 不动基岩（同 carveCaves / placeBedrock）
    constexpr int kAirAbove      = 2;       // 岩浆面之上的空气层数（形成「岩浆 + 气室」封闭空腔）

    int placed = 0;
    const int poolSeed = m_seed + 9309; // 岩浆湖哈希偏移（与其它 worldgen hashColumn 解耦）
    for (int bx = kPoolGrid / 2; bx < m_width; bx += kPoolGrid) {
        for (int bz = kPoolGrid / 2; bz < m_depth; bz += kPoolGrid) {
            const quint32 r = hashColumn(poolSeed, bx, bz);
            if ((r % 100u) >= kPoolPct) continue; // 概率筛选
            const int span = kPoolGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < 3 || cz < 3 || cx >= m_width - 3 || cz >= m_depth - 3) continue; // 留 3 格边界
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠岩浆湖（避免与海水柱冲突）
            // 岩浆湖 y 范围：基岩之上 ~ kLavaLakeMaxY 之下（spec「Y<30」）。地下深处封闭洞穴。
            const int yLo = kBedrockTop + 3;
            const int yHi = kLavaLakeMaxY - 1;
            if (yHi <= yLo) continue;
            const int yRange = yHi - yLo + 1;
            const int cy = yLo + int((r >> 9) & 0x1Fu) % yRange;
            const int rad = 2 + int((r >> 14) & 1u); // 半径 2..3

            // 挖圆盘空腔（disc × {底层岩浆源 + 上方 kAirAbove 层 air}）。仅覆盖 disc 范围；不触碰外部岩壁 → 天然封闭。
            //   底层（cy）岩浆源；cy+1..cy+kAirAbove 空气（气室）；其上保留原岩（石顶）。空腔被周围实体岩包围 →
            //   岩浆源水平邻居为岩浆（disc 内）或实体（disc 外）→ 无 air 邻居 → 不蔓延（tickLavaFlow 稳态）。
            const int rad2 = rad * rad;
            for (int dx = -rad; dx <= rad; ++dx) {
                for (int dz = -rad; dz <= rad; ++dz) {
                    if (dx * dx + dz * dz > rad2) continue; // 圆盘
                    const int px = cx + dx, pz = cz + dz;
                    const quint8 fb = m_chunks.blockAt(px, cy, pz);
                    if (fb != BlockRegistry::Bedrock && fb != BlockRegistry::Lava)
                        m_chunks.setBlock(px, cy, pz, BlockRegistry::Lava); // 底层岩浆源（覆盖 cave air/stone/ore，不动 bedrock/已有岩浆）
                    for (int ay = 1; ay <= kAirAbove; ++ay) { // 岩浆面之上空气层（气室）；越界 / 基岩不动。
                        const int yy = cy + ay;
                        if (yy >= m_height) break;
                        const quint8 ab = m_chunks.blockAt(px, yy, pz);
                        if (ab == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(px, yy, pz, BlockRegistry::Air);
                    }
                }
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: underground lava lakes =" << placed; // 同 seed → 同计数（确定性核对）
}

// t392 地下地牢（见 world.h 头注释）。机制等价 MC 1.0 地牢 / 怪物房间：地下深处的小型封闭石室，中央放刷怪笼
//   + 角落放战利品箱。确定性散布（hashColumn + seed 偏移，PLAN §2-K），结构与 placeUndergroundWaterPools /
//   placeLavaLakes 同源（网格采样 + 概率筛选 + 抖动 + y 范围派生），但 carve 出的是矩形房间 + 周界填墙。
//
//   房间几何（t995 对照 MC 修正：内空 W×D ∈ {5,7}² 随机 × 高 4，墙体在 [-1, W]×[-1, H]×[-1, D] 外圈；
//   t426 时代恒 7×7）：
//     - 地板：Cobble + MossyCobble 逐块独立随机 25% 圆石 / 75% 苔石（t999 对照 minecraft.wiki Monster
//       Room 逐方块考据；旧 t995「地板 50% / 墙顶 25%」口径作废 —— t995a 探针合法演化）。墙 / 顶：普通
//       圆石零苔。per-cell hash 分流，确定性。
//     - 内部 (0..W-1, 0..H-1, 0..D-1)：置 Air（清空原 stone / ore / cave air → 干净房间）。不动 Bedrock
//       （基岩层不可破）。
//     - 中央 (W/2, 1, D/2)：置 Spawner（地板上方一格 = 站立高度；玩家走过来触发刷怪）。t786 起 state 带
//       mob 类型；t999 起加权池 = 僵尸 50% / 骷髅 25% / 蜘蛛 25%（爬行者退出地牢池；见步骤 3 权重表）。
//     - 墙脚豁口 1-5 个（t999 考据）：2 高空气开口，向邻近空气 / 洞穴逐格挖穿（≤12 格；无空气邻域 →
//       此豁口放弃，全间皆弃 → 全封，日志登记），机制等价 MC 地牢「被洞穴暴露」的生成显式化。
//     - 战利品箱 0-2 个（t999 MC 尝试规则）：2 箱位 × 各 3 次尝试，目标格 Air 且四水平邻恰一实心
//       （贴墙；Chest 不计入实心 → 偶然自然成双箱布局。项目无双箱合并态：ChestState 仅低 2 位朝向 +
//       结构 flag（bit2-6）无双箱语义，战利品逐坐标独立填 → 相邻两箱保持独立单箱，登记偏差）。全部
//       尝试失败 → 0 箱（MC 少见态，登记）。旧 t995「角位必放 + ~50% 对角」口径作废（bit30 退役）。
//
//   空腔被实体墙天然封闭 → 房间内无天光 → 黑暗（机制等价 MC 1.0 地牢黑暗环境 + 刷怪笼刷怪条件）。
//   与既有洞穴重叠时（carveCaves 已挖空同位）→ 墙体在洞穴侧被截断，地牢轮廓仍可见（同 MC 1.0 地牢被洞穴
//   穿墙暴露）。t343 岩浆湖之后（避免岩浆湖填进地牢房间 —— placeLavaLakes 不动 Cobble 墙体，地牢墙体先于
//   岩浆湖不存在 → 顺序无关；此处放其后保持「流体 worldgen 优先于结构」惯例，避免岩浆与房间争夺同列）。
//   fillWater 之前（房间独立于海平面；fillWater 仅填地表低洼 → 地下房间不被灌水）。
void World::placeDungeons()
{
    constexpr int kRoomH           = kDungeonRoomH; // 房间内部高度（Y 方向格子数；与 dungeonSites() 选择窗同源类常量）

    int placed = 0;
    int totalOpeningCells = 0; // t999 豁口合计（登记用）
    int sealedRooms = 0;       // t999 全封房间数（无空气邻域 → 0 豁口，登记用）
    const int dungSeed = m_seed + kDungeonSeedOff; // 地牢哈希偏移（与 dungeonSites() 同源类常量，防两处漂移）
    // t1020 候选选择上收：概率筛选 / 网格抖动 / margin / 海列 / 高度窗 / 内空尺寸位域已收口 dungeonSites()
    //   （单一权威 —— 本函数只消费表驱动几何；rebuildStructureRegions 区域重推导同表 → 「同 seed 同候选」
    //   由结构保证。原循环头选择段逐字迁移，逐值同序，行为零变化）。
    const std::vector<StructureSite> sites = dungeonSites();
    for (const StructureSite &site : sites) {
        const quint32 r = site.r;
        const int cx = site.cx, cz = site.cz;
        const int cy = site.y;
        const int roomW = site.roomW;
        const int roomD = site.roomD;
        {
            // 房间石材（t999 对照 minecraft.wiki Monster Room 逐方块考据）：地板逐块独立随机 25% 圆石 /
            //   75% 苔石；墙与顶恒普通圆石（零苔）。旧「地板 50% / 墙顶 25%」口径作废（t995a 探针合法演化：
            //   断言翻转为地板苔率窗 + 墙零苔）。per-cell hash 位分流，确定性 → 同 seed 同墙。
            auto wallBlock = [&](int wx, int wy, int wz, bool isFloor) -> quint8 {
                if (!isFloor) return BlockRegistry::Cobble; // t999 墙 / 顶普通圆石（零苔）
                const quint32 wb = hashVoxel(dungSeed ^ 0x5a5a, wx, wy, wz);
                return (wb % 100u) < 75u ? BlockRegistry::MossyCobble : BlockRegistry::Cobble; // t999 地板 75% 苔石
            };

            // 1) 周界填墙（地板 / 顶板 / 四壁）：遍历 [-1, roomW]×[−1, kRoomH]×[−1, roomD] 外圈，
            //    对每个边界格置 wallBlock（不动 Bedrock）。内部空气在步骤 2 清空。
            for (int dy = -1; dy <= kRoomH; ++dy) {
                const int yy = cy + dy;
                if (yy < 0 || yy >= m_height) continue;
                const bool yEdge = (dy == -1 || dy == kRoomH); // 地板（dy=-1）/ 顶板（dy=kRoomH）
                for (int dx = -1; dx <= roomW; ++dx) {
                    for (int dz = -1; dz <= roomD; ++dz) {
                        const bool xEdge = (dx == -1 || dx == roomW);
                        const bool zEdge = (dz == -1 || dz == roomD);
                        if (!yEdge && !xEdge && !zEdge) continue; // 内部格由步骤 2 处理（清空气）
                        const int px = cx + dx, pz = cz + dz;
                        const quint8 cur = m_chunks.blockAt(px, yy, pz);
                        if (cur == BlockRegistry::Bedrock) continue; // 不动基岩（保留 worldgen 底层）
                        m_chunks.setBlock(px, yy, pz, wallBlock(px, yy, pz, dy == -1));
                    }
                }
            }
            // 2) 内部清空气（W×H×D）：覆盖原 stone / ore / cave air → 干净房间（防墙体填充误入内部、
            //    防 cave 残余格子留洞）。不动 Bedrock（防穿透基岩底层）。
            for (int dy = 0; dy < kRoomH; ++dy) {
                const int yy = cy + dy;
                if (yy < 0 || yy >= m_height) continue;
                for (int dx = 0; dx < roomW; ++dx) {
                    for (int dz = 0; dz < roomD; ++dz) {
                        const quint8 cur = m_chunks.blockAt(cx + dx, yy, cz + dz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(cx + dx, yy, cz + dz, BlockRegistry::Air);
                    }
                }
            }
            // 3) 中央 Spawner（地板上方一格 = cy+1 = 站立高度）。覆盖原空气格；不动非空气（防 cave 重叠时
            //    误覆盖既有方块，但步骤 2 已清空气 → 此处恒为 Air，覆盖安全）。
            //    t999 考据口径：地牢笼按 hash r 的 bit20-27（256 档）加权随机带 mob 类型 state（僵尸 50% /
            //    骷髅 25% / 蜘蛛 25% —— minecraft.wiki Monster Room；爬行者（Stalker）退出地牢池，旧
            //    40/25/20/15 池作废 —— t786 加权分布腿合法演化；蠹虫不在地牢池，要塞专属）。分层：World
            //    不依赖 Entities → 表存 BlockRegistry 完整 state 常量（数值契约 = EntityManager::MobType）；
            //    tickSpawners 经 EntityManager::spawnerMobTypeForState 解码同刷。
            //    确定性（PLAN §2-K）：同 seed 同分布；r 低 20 位已被概率/jx/jz/cy 用走，bit20-27 独立采样。
            static constexpr quint8 kDungeonSpawnerStates[3] = {
                BlockRegistry::SpawnerStateShambler,  // 僵尸笼（128/256，最常见 → 创造放置默认亦此型）
                BlockRegistry::SpawnerStateBones,     // 骷髅笼（64/256）
                BlockRegistry::SpawnerStateSpider,    // 蜘蛛笼（64/256）
            };
            static constexpr int kDungeonSpawnerWeights[3] = { 128, 64, 64 }; // 合计 256（t999 50/25/25；改权重须保持和 256）
            const int spawnerPick = int((r >> 20) & 0xFFu); // [0, 255]
            quint8 spawnerState = BlockRegistry::SpawnerStateShambler; // 兜底（权重和 <256 时最常见型）
            int spawnerAcc = 0;
            for (int si = 0; si < 3; ++si) {
                spawnerAcc += kDungeonSpawnerWeights[si];
                if (spawnerPick < spawnerAcc) { spawnerState = kDungeonSpawnerStates[si]; break; }
            }
            m_chunks.setBlock(cx + roomW / 2, cy + 1, cz + roomD / 2, BlockRegistry::Spawner, spawnerState);
            // 4) 墙脚豁口（t999 考据：1-5 个 2 高空气开口，通向邻近空气 / 洞穴 —— MC 地牢「被洞穴暴露」
            //    的生成显式化；项目有限世界洞穴不一定贴邻 → 先搜「最近空气柱」再挖，搜不到 → 全封，日志
            //    登记）。豁口数 = 1 + (r>>14 & 7)%5 ∈ [1,5]（r bit14-16，与既有位域零耦合；旧对角二箱位
            //    bit30 随角箱口径一并退役）。
            //    合格空气柱：房间 footprint（含墙环）之外、距外框曼哈顿 ≤ kOpeningMaxDepth，且列内
            //    [cy-2, cy+3] 存在 Air（取距 cy 最近空气层为目标层 ty，同距取 y 低者 → 确定性；垂直窗
            //    放宽 = 「挖向最近空气」口径 —— 洞穴地面与房脚 exact 对齐并非考据要求，到达后以竖向
            //    连接段接通）。每豁口取当前最近未用柱（防多豁口聚同柱），自贴墙环格（主轴侧非角格）
            //    经 L 形路径 2 高挖穿覆土；先扫后挖：路径 / 连接段遇 Bedrock / Water / Lava / Chest /
            //    Spawner → 弃此柱取次近（不半挖、不触流体、不毁结构方块）。
            constexpr int kOpeningMaxDepth = 12; // 空气柱搜索半径（距房间外框曼哈顿）
            const int openingCount = 1 + int((r >> 14) & 7u) % 5; // t999 豁口数 1..5
            constexpr int kOpenScanSide = 2 * (7 + 1 + kOpeningMaxDepth) + 1; // 扫描方边（最大内空 + 墙环 + 两侧半径，全跨度）
            int candX[kOpenScanSide * kOpenScanSide], candZ[kOpenScanSide * kOpenScanSide];
            int candY[kOpenScanSide * kOpenScanSide], candDist[kOpenScanSide * kOpenScanSide];
            int candN = 0;
            const int scanXLo = std::max(0, cx - roomW - 1 - kOpeningMaxDepth);
            const int scanXHi = std::min(m_width - 1, cx + roomW + 1 + kOpeningMaxDepth);
            const int scanZLo = std::max(0, cz - roomD - 1 - kOpeningMaxDepth);
            const int scanZHi = std::min(m_depth - 1, cz + roomD + 1 + kOpeningMaxDepth);
            const int probeY[6] = { cy, cy + 1, cy - 1, cy + 2, cy - 2, cy + 3 }; // 距房脚近者优先
            for (int sz2 = scanZLo; sz2 <= scanZHi; ++sz2) { // 采集合格空气柱（z 外 x 内固定序 → 确定性）
                for (int sx2 = scanXLo; sx2 <= scanXHi; ++sx2) {
                    const int ddx = sx2 - cx, ddz = sz2 - cz;
                    const int adx = ddx < 0 ? -ddx : ddx, adz = ddz < 0 ? -ddz : ddz;
                    if (adx <= roomW + 1 && adz <= roomD + 1) continue; // 房间 footprint（含墙环）排除
                    const int ddxo = adx > roomW + 1 ? adx - (roomW + 1) : 0; // 距外框曼哈顿距离
                    const int ddzo = adz > roomD + 1 ? adz - (roomD + 1) : 0;
                    const int dist = ddxo + ddzo;
                    if (dist > kOpeningMaxDepth || candN >= kOpenScanSide * kOpenScanSide) continue;
                    int ty = 0; // 目标空气层（探针序 = |ty-cy| 升序，同距 y 低者优先 → 确定性）
                    bool found = false;
                    for (int pi2 = 0; pi2 < 6 && !found; ++pi2)
                        if (m_chunks.blockAt(sx2, probeY[pi2], sz2) == BlockRegistry::Air) {
                            ty = probeY[pi2]; found = true;
                        }
                    if (!found) continue; // 列内无空气 → 非候选
                    candX[candN] = sx2; candZ[candN] = sz2; candY[candN] = ty;
                    candDist[candN] = dist; ++candN;
                }
            }
            int openingsDug = 0;
            bool candUsed[kOpenScanSide * kOpenScanSide] = {};
            for (int oi = 0; oi < openingCount; ++oi) {
                int best = -1; // 最近未用合格柱（同距取扫描序靠前 → 确定性）
                for (int ci = 0; ci < candN; ++ci)
                    if (!candUsed[ci] && (best < 0 || candDist[ci] < candDist[best])) best = ci;
                if (best < 0) break; // 无空气柱可通 → 剩余豁口全弃（全封候选）
                candUsed[best] = true;
                const int tx = candX[best], tz = candZ[best], ty = candY[best];
                const int ddx = tx - cx, ddz = tz - cz;
                const bool xDom = (ddx < 0 ? -ddx : ddx) - (roomW + 1) >= (ddz < 0 ? -ddz : ddz) - (roomD + 1);
                // 贴墙环格（主轴侧非角格）：豁口面孔 = 房间墙，位置随目标柱钳位
                const int rx = xDom ? (cx + (ddx > 0 ? roomW : -1))
                                    : std::min(cx + roomW - 1, std::max(cx, tx));
                const int rz = xDom ? std::min(cz + roomD - 1, std::max(cz, tz))
                                    : (cz + (ddz > 0 ? roomD : -1));
                const int sxx = xDom ? (ddx > 0 ? 1 : -1) : (tx > rx ? 1 : (tx < rx ? -1 : 0));
                const int szz = xDom ? (tz > rz ? 1 : (tz < rz ? -1 : 0)) : (ddz > 0 ? 1 : -1);
                // L 形路径预扫（含贴墙环格 R 自身 = 破墙点；先 x 后 z / 先 z 后 x）+ 竖向连接段预扫：
                // 遇 Bedrock / Water / Lava / Chest / Spawner → 弃柱（不半挖 / 不触流体 / 不毁结构）
                bool blocked = false;
                auto pathCellBlocked = [&](int qx, int qy, int qz) {
                    const quint8 b = m_chunks.blockAt(qx, qy, qz);
                    return b == BlockRegistry::Bedrock || b == BlockRegistry::Water
                        || b == BlockRegistry::Lava || b == BlockRegistry::Chest
                        || b == BlockRegistry::Spawner;
                };
                if (xDom) { // 先 x 直达目标列（含环格 R），再 z 归位
                    for (int qx = rx; qx != tx + (sxx > 0 ? 1 : -1) && !blocked; qx += sxx)
                        blocked = pathCellBlocked(qx, cy, rz) || pathCellBlocked(qx, cy + 1, rz);
                    for (int qz = rz + szz; szz != 0 && qz != tz + (szz > 0 ? 1 : -1) && !blocked; qz += szz)
                        blocked = pathCellBlocked(tx, cy, qz) || pathCellBlocked(tx, cy + 1, qz);
                } else {    // 先 z 直达目标行（含环格 R），再 x 归位
                    for (int qz = rz; qz != tz + (szz > 0 ? 1 : -1) && !blocked; qz += szz)
                        blocked = pathCellBlocked(rx, cy, qz) || pathCellBlocked(rx, cy + 1, qz);
                    for (int qx = rx + sxx; sxx != 0 && qx != tx + (sxx > 0 ? 1 : -1) && !blocked; qx += sxx)
                        blocked = pathCellBlocked(qx, cy, tz) || pathCellBlocked(qx, cy + 1, tz);
                }
                for (int qy = cy + 2; !blocked && qy <= ty; ++qy)      // 竖向连接段（上行：cy+2..ty）
                    blocked = pathCellBlocked(tx, qy, tz);
                for (int qy = ty; !blocked && qy <= cy - 1; ++qy)      //           （下行：ty..cy-1）
                    blocked = pathCellBlocked(tx, qy, tz);
                if (blocked) continue; // 此柱路径不可挖 → 取次近柱
                auto carveCellY = [&](int qx, int qy, int qz) {
                    if (m_chunks.blockAt(qx, qy, qz) != BlockRegistry::Air)
                        m_chunks.setBlock(qx, qy, qz, BlockRegistry::Air);
                };
                auto carveCellFoot = [&](int qx, int qz) { // 2 高豁口：脚部层 + 头部层
                    carveCellY(qx, cy, qz);
                    carveCellY(qx, cy + 1, qz);
                };
                if (xDom) {
                    for (int qx = rx; qx != tx + (sxx > 0 ? 1 : -1); qx += sxx) carveCellFoot(qx, rz);
                    for (int qz = rz + szz; szz != 0 && qz != tz + (szz > 0 ? 1 : -1); qz += szz)
                        carveCellFoot(tx, qz);
                } else {
                    for (int qz = rz; qz != tz + (szz > 0 ? 1 : -1); qz += szz) carveCellFoot(rx, qz);
                    for (int qx = rx + sxx; sxx != 0 && qx != tx + (sxx > 0 ? 1 : -1); qx += sxx)
                        carveCellFoot(qx, tz);
                }
                if (ty >= cy + 2)
                    for (int qy = cy + 2; qy <= ty; ++qy) carveCellY(tx, qy, tz); // 上行连接
                else if (ty <= cy - 1)
                    for (int qy = ty; qy <= cy - 1; ++qy) carveCellY(tx, qy, tz); // 下行连接
                ++openingsDug;
            }
            totalOpeningCells += openingsDug;
            if (openingsDug == 0) ++sealedRooms; // 四向皆无空气邻域 → 全封（登记）
            // 5) 战利品箱（t999 MC 尝试规则，考据 minecraft.wiki Monster Room）：2 个箱位 × 各 3 次尝试。
            //    每次尝试按 hashVoxel（箱位 + 尝试序独立盐）在内空取 (x,z)、y = cy+1；条件 = 目标格为 Air
            //    且四水平邻**恰一个实心**（贴墙；Chest 不计入实心 → 沿墙第二箱可与首箱相邻 → 自然成双箱
            //    布局；Spawner 计入实心，同 MC 通用 solid 判定）。全部尝试失败 → 0 箱（MC 少见态）。
            //    t995 旧「角位必放 + ~50% 对角二箱」口径作废（bit30 退役）；t995c 探针合法演化为尝试规则
            //    分布断言（0-2 箱 + 每箱贴墙核）。豁口先于箱（步骤 4 已定稿墙体 → 箱规则见最终几何）。
            auto chestSolidNeighbors = [&](int px, int py, int pz) -> int {
                static const int kChestDirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                int n = 0;
                for (const auto &d : kChestDirs) {
                    const quint8 nb = m_chunks.blockAt(px + d[0], py, pz + d[1]);
                    if (nb != BlockRegistry::Air && nb != BlockRegistry::Chest) ++n; // Chest 不计入实心（自然成双）
                }
                return n;
            };
            for (int slot = 0; slot < 2; ++slot) { // t999 2 箱位 × 3 尝试
                for (int attempt = 0; attempt < 3; ++attempt) {
                    const quint32 cb = hashVoxel(dungSeed ^ (0xC7E5u + quint32(slot * 4 + attempt)), cx, cy, cz);
                    const int px = cx + int(cb & 0xFu) % roomW;
                    const int pz = cz + int((cb >> 8) & 0xFu) % roomD;
                    if (m_chunks.blockAt(px, cy + 1, pz) != BlockRegistry::Air) continue; // 目标非空气
                    if (chestSolidNeighbors(px, cy + 1, pz) != 1) continue; // t999 贴墙：恰一实心邻
                    m_chunks.setBlock(px, cy + 1, pz, BlockRegistry::Chest, BlockRegistry::ChestStateDungeonFlag);
                    break; // 本箱位落箱 → 下一箱位
                }
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: underground dungeons =" << placed; // 同 seed → 同计数（确定性核对）
    // t999 豁口登记（同 seed → 同数值；全封 = 四向皆无空气邻域的房间，考据允许态）
    qInfo() << "worldgen: dungeon wall-foot openings =" << totalOpeningCells
            << "(fully sealed rooms" << sealedRooms << "/" << placed << ")";
}

// t484/t565 废弃矿井（见 world.h 头注释）。机制等价 MC 1.0 废弃矿井 mineshaft；**t1001 逐方块重建**（考据
//   minecraft.wiki Mineshaft/Structure 子页 + 主条目）：placeMineshaft 内部 piece 化重写，piece 表 ——
//     ① pieceStartRoom 起点厅：10×10 footprint（每矿井恰一座；考据「up to 4 exits + spawns once」），
//        拱顶 = 外环净高 3 / 内芯净高 4 双段剖面 + 外环 sy+4 一圈 Planks 拱带（springing course）；
//        出口 = hash 洗牌 4 主向取前 3..4 向（各向至多一条巷道 → 出口数 3..4 ∈ 考据 1-4 窗）；
//        t1011 角壁火把 ×4（头层墙插 + 洞穴壁空地板回退 → 入口厅宏观可见照明）。
//     ② pieceCorridor 巷道：自起点厅房缘放射，3 宽 × 3 高截面 + L 形折线（沿用 t565）；支撑组每
//        kSupportInterval=4 格一道（两侧 WoodFence 双柱 sy+1..sy+2 + 柱顶封盖：~55% Torch 火把 /
//        其余 Planks 木板柱冠（考据蓝图「layer 3 木板封柱顶」）；组级 hash 25% 缺失 = 考据「部分
//        支撑缺失」，step 0/4 豁免锚定间距窗）；t1011 跨中壁挂火把（step ≡ 2 mod interval 顶层
//        巷壁两侧成对墙插、洞穴合并段壁空 → 贴面格地板火把回退、斜坡段同布）→ 相邻火把步距 ≤ 2 格；
//        残缺轨（中线 hash 70% 保留 →
//        考据「铁轨残缺不连续」）；顶角蛛网 12% +
//        巷壁矿石 15%（沿用 t565 口径）。
//     ③ pieceIntersection 交叉口：巷道途中 5×5 通高开口 + 四角 WoodFence 双层双柱 + Planks 柱冠
//        （考据交叉口木柱支撑；**口径分歧登记**：/Structure 子页画 3×3 + 三层角柱，采批头 5×5 双层弃用子页形）。
//     ④ pieceSlope 斜坡段：折线第二段改对角下切（无楼梯块；地板每 2 步降 1 格，3 宽截面随降，钳底
//        ≥ 基岩顶+2）——考据斜坡巷道逐层下剖。
//     ⑤ pieceSpiderRoom 洞穴蛛网走廊（t1012 ② 用户裁决，弃 t1001 7×7×3 房间形）：巷侧垂直向支廊 =
//        1-2 宽 × 2-3 高 × 3-6 长窄廊（hash 选型；对齐 /Structure 子页 3×6×3 走廊形口径）+ 满网
//        （廊体全部空气格 → Cobweb，旧 45% 散布与 solidAround 守卫随房间形废除）+ 笼（Spawner
//        state=SpawnerStateCaveSpider——t1012③ 偏差转正：旧「偏差登记 MobSpider / 0x0E 保持」退役，
//        矿井蛛网走廊的刷怪笼就是洞穴蜘蛛笼，state=(20<<1)=0x28）；笼位居廊中，
//        笼位及其轴两邻格恒 3 层 carve（夹网巢 → 笼周 Chebyshev≤2 三层满网 ≥8 网 → t786「≥8 网 = 矿井
//        蛛笼」分流口径在任意宽 × 高 × 长选型下都不破）；旧 7×7 双短端栅栏随房间形废除（走廊形无短端，
//        口径登记）。
//     ⑥ 矿井箱：t1013 起 = **箱子矿车生成标记**（考据「轨上运输矿车」转正；R19.19 偏差「箱落地轨旁」
//        退役）：自已铺 railCells 取一格，四向首个空地落 Chest + ChestStateMineshaftFlag，进世界由
//        convertMineshaftChests 摘块转正为箱子矿车实体（内容键 = 标记格，详见 placeMineshaft 段内注释）。
//   保留 t565 既有口径：网格 36 / 40% 频率、Y<48、矿石 15%、hashColumn/hashVoxel 确定性（PLAN §2-K）。
//   铺后统一经 BlockRegistry::railConnections 算连接 state（直 / 拐角 / 十字形态自动得出，mesher 据此切
//   贴图；与运行期 checkRailOnEdit 同一权威）。t1012 ① 地板政策：嵌岩段（下方实地）直接 Stone 不铺木板、
//   洞穴空腔段（下方被 carveCaves 掏空）铺 Planks 桥面保可走（用户口径；t565 ⑤ 全矿井 hash 二选一废除）。
//   t1012 ③ 段 B 推进回归修复：pieceCorridor 恢复 t565 推进条件 `leg==1 || step>0`（t1001 重写回归 →
//   段 B 不推进、L 第二腿缺失 / 斜坡段同柱逐层坑，见 pieceCorridor 内注释）。
//   巷道被周围实体岩天然封闭 → 内部无天光 → 黑暗 + 火把点光（机制等价 MC 1.0 矿井环境）。与既有洞穴重叠时
//   （carveCaves 已挖空同位）→ 结构仍画出（矿井叠加于洞穴，同 placeDungeons 墙体被洞穴截断）。
//   t1011 光源归因登记（用户实测「矿井里见光但 F3 bl:0 没看到火把」）：bl 通道非零必须近处有火把（火把
//   lightEmission 14、recomputeLightField 全场重 flood 于 generate 末尾），bl:0 的「见光」必为**天光通道**
//   —— 矿井段壁与 carveCaves 洞穴网络交叠处形成豁口，洞穴连通地表 → 天光 BFS 经豁口渗入巷道（skyLightAt>0、
//   F3 sl 非零），观感即「有光但无火把」。修复面 = 火把覆盖不足（原柱顶 55%×2 侧 + 斜坡段零火把 → 局部
//   长暗段），非火把不落块（P-t1001 ⑦ 池化火把 ≥15 已证落块）。t1011 跨中壁挂补光后密度窗由 P-t1011 钉。
//   placeDungeons 之后、fillWater 之前（独立于海平面；fillWater 仅填地表低洼 → 地下矿井不被灌水）。
void World::placeMineshaft()
{
    constexpr int kBedrockTop       = 4;      // 不动基岩（斜坡段钳底用；同 carveCaves / placeDungeons）
    constexpr int kRoomHalf         = 5;      // t1001 起点厅半幅（10×10 footprint [cx-5, cx+4]×[cz-5, cz+4]）
    constexpr int kRoomRingH        = 3;      // 起点厅外环净高（拱顶低段；外环 = 距边 0 的格子）
    constexpr int kRoomCoreH        = 4;      // 起点厅内芯净高（拱顶高段；比外环高 1 → 双段拱剖面）
    constexpr int kTunnelLenMin     = 5;      // 巷道单段最短长度（段数；L 形两段各取 → 总长 10..20）
    constexpr int kTunnelLenMax     = 10;     // 巷道单段最长长度
    constexpr int kTunnelH          = 3;      // 巷道内部高度（空气层数；y=sy+1..sy+kTunnelH）
    constexpr int kSupportInterval  = 4;      // t1001 支撑间距（每 4 格一道支撑组；考据支撑组间隔；阴性轮敏感位）
    constexpr unsigned kSupportSkipPct = 25u; // 支撑组缺失概率（考据「部分缺失随机」；step 0/4 豁免锚定间距窗）
    constexpr unsigned kRailPct     = 70u;    // 残缺轨保留率（中线每步 hash 判定 → 考据「铁轨残缺不连续」）
    constexpr unsigned kCobwebPct   = 12u;    // 巷道顶角蛛网概率（每段每侧 ~12%；沿用 t565）
    constexpr unsigned kOrePct      = 15u;    // 巷壁矿石概率（每段每侧 ~15%；沿用 t565）
    constexpr unsigned kTorchPct    = 55u;    // 支撑组火把概率（每组 ~55% → 考据「火把部分巷道」）
    constexpr unsigned kSpiderRoomPct = 60u;  // 巷道事件：蛛网走廊支廊概率（余下 25% 交叉口 / ~15% 斜坡段）
    constexpr unsigned kIntersectionPct = 25u; // 巷道事件：交叉口概率
    constexpr int kSpiderCorrLenMin = 3;      // t1012 ② 蛛网走廊长度下限（3-6 长 hash 选型）
    constexpr int kSpiderCorrLenMax = 6;      // t1012 ② 蛛网走廊长度上限

    const int mineSeed = m_seed + kMineshaftSeedOff; // 矿井哈希偏移（与 mineshaftSites() 同源类常量，防两处漂移）
    int placed = 0;
    int chests = 0; // 矿井宝箱计数（日志核对 > 0；t1001 箱落地轨旁）
    // t1020 候选选择上收（同 placeDungeons 口径）：概率筛选 / 网格抖动 / margin / 海列 / 高度窗已收口
    //   mineshaftSites()（单一权威；rebuildStructureRegions 区域重推导同表）。原循环头选择段逐字迁移，
    //   逐值同序，行为零变化。
    for (const StructureSite &msSite : mineshaftSites()) {
        const int cx = msSite.cx, cz = msSite.cz;
        const int sy = msSite.y;
        {

            // t1012 ① 地板政策（用户口径，t565 ⑤「全矿井 hash 二选一」废除）：材质不再按矿井 hash 统一，
            //   而随每列下方实际承载切换（carveCell 内逐格判定）——嵌岩段（下方实地）直接 Stone 不铺木板；
            //   洞穴空腔段（下方被 carveCaves 掏空）铺 Planks 桥面保可走。

            // t565 ④ 铁轨铺设记录：铺完统一算连接 state（直 / 拐角 / 十字形态）；t1001 ⑥ 矿井箱
            //   「落地轨旁」亦从本表取锚（考据轨上运输矿车 → 偏差落地等价）。
            std::vector<std::array<int, 3>> railCells;
            // t1043 裁-1 生成期防水（伴随义务，非豁免——isAttachableBlock 轨族入水毁族的配套面）：
            //   轨的**放置**从巷道 walk 内联改为「候选先收集 → walk 完成后统一过干燥门再落块」。
            //   候选 = hash 保留率判过的中线格（与旧内联同一 hash 序，逐格判定不变）；落块推迟到
            //   pieceCorridor / 起点厅 / 途中事件全部 carve 完毕之后，干燥门才能看到完整巷道周边
            //   （breach 可能在某条轨之后才 carve 出来）。摘此门 → 洪流巷道的轨在运行期首水 tick
            //   成片被冲（= review0906 #10 当初登记的「矿井轨网被一次洞口洪流掏空」，改由生成期
            //   防水承接而非水蚀豁免；P-t1043b 阴性轮敏感位）。
            std::vector<std::array<int, 3>> railCandidates;

            // 逐格铺地板 + 清空气（px,pz 列，地板 y0、内部空气 y0+1..y0+roomH；不动 Bedrock；越界钳制）。
            //   t1012 ① 地板政策：下方 Air = 洞穴空腔 → Planks 桥面；下方实地（默认嵌岩）→ Stone 石底。
            const auto carveCell = [&](int px, int pz, int y0, int roomH) {
                if (px < 0 || pz < 0 || px >= m_width || pz >= m_depth) return; // 防御（margin 已保证不越界）
                const quint8 fb0 = m_chunks.blockAt(px, y0, pz);
                if (fb0 != BlockRegistry::Bedrock) {
                    const bool overCavity = y0 > 0
                        && m_chunks.blockAt(px, y0 - 1, pz) == BlockRegistry::Air;
                    m_chunks.setBlock(px, y0, pz,
                                      overCavity ? BlockRegistry::Planks : BlockRegistry::Stone);
                }
                for (int dy = 1; dy <= roomH; ++dy) {
                    const int yy = y0 + dy;
                    if (yy >= m_height) break;
                    const quint8 ib = m_chunks.blockAt(px, yy, pz);
                    if (ib == BlockRegistry::Bedrock) continue;
                    m_chunks.setBlock(px, yy, pz, BlockRegistry::Air);
                }
            };
            // 结构块直写（不动 Bedrock；越界钳制）——piece 支撑 / 拱带 / 笼 / 栅栏 / 柱冠共用。
            const auto putStruct = [&](int px, int py, int pz, quint8 id, quint8 st) {
                if (px < 0 || pz < 0 || px >= m_width || pz >= m_depth) return;
                if (py < 0 || py >= m_height) return;
                if (m_chunks.blockAt(px, py, pz) == BlockRegistry::Bedrock) return;
                m_chunks.setBlock(px, py, pz, id, st);
            };

            // ── t1001 piece 表（顺序：pieceSpiderRoom / pieceIntersection / pieceCorridor 定义 →
            //    pieceStartRoom 落地并放射巷道 → 统一重算轨连接 → 矿井箱落地轨旁）──

            // ⑤ pieceSpiderRoom 洞穴蛛网走廊（t1012 ② 走廊形）：1-2 宽 × 2-3 高 × 3-6 长窄廊 + 满网 +
            //   笼（Spawner state=SpawnerStateCaveSpider——t1012③ **偏差转正**：旧「登记偏差 MobSpider /
            //   0x0E 保持」退役，矿井蛛网走廊的刷怪笼就是洞穴蜘蛛笼（MC 口径），state=(20<<1)=0x28）；
            //   笼位居中、笼位 ±1 轴邻格恒 3 层夹网巢（笼周满网 ≥8 → t786 分流口径不破）。
            const auto pieceSpiderRoom = [&](int ax2, int az2, int dirIdx) {
                const int ppx = (dirIdx < 2) ? 0 : 1; // 支廊轴 = 巷道垂直向（dir 0/1 = X 向巷道 → Z 向支廊）
                const int ppz = (dirIdx < 2) ? 1 : 0;
                const int tdx = (dirIdx == 0) ? 1 : (dirIdx == 1) ? -1 : 0; // 巷轴（廊宽展开向）
                const int tdz = (dirIdx == 2) ? 1 : (dirIdx == 3) ? -1 : 0;
                const int sgn = (hashVoxel(mineSeed ^ 0x5ED1u, ax2, sy, az2) & 1u) ? 1 : -1; // 巷道哪侧（t1001 盐保留）
                const quint32 dh = hashVoxel(mineSeed ^ 0x5C1Au, ax2, sy, az2); // 走廊形选型 hash（t1012 盐）
                const int snw = 1 + int(dh & 1u);         // 宽 1-2
                const int snh = 2 + int((dh >> 2) & 1u);  // 高 2-3
                const int snl = kSpiderCorrLenMin
                    + int((dh >> 4) & 3u) % (kSpiderCorrLenMax - kSpiderCorrLenMin + 1); // 长 3-6
                const int cageOff = 2 + snl / 2;          // 笼位（廊体 = off 2..snl+1，居中）
                const auto nestH = [&](int off) {         // 笼位 ±1 轴邻格恒 3 层（夹网巢）
                    return (off >= cageOff - 1 && off <= cageOff + 1) ? 3 : snh;
                };
                for (int off = 2; off <= snl + 1; ++off)  // 廊体 carve（穿墙 2 格起）
                    for (int w2 = 0; w2 < snw; ++w2)
                        carveCell(ax2 + ppx * sgn * off + tdx * w2,
                                  az2 + ppz * sgn * off + tdz * w2, sy, nestH(off));
                putStruct(ax2 + ppx * sgn * cageOff, sy + 1, az2 + ppz * sgn * cageOff,
                          BlockRegistry::Spawner, BlockRegistry::SpawnerStateCaveSpider); // t1012③ 偏差转正：洞穴蜘蛛笼（旧 MobSpider 0x0E 退役）
                for (int off = 2; off <= snl + 1; ++off) { // 满网：廊体全部空气格 → Cobweb（笼格 / 基岩不动）
                    for (int w2 = 0; w2 < snw; ++w2)
                        for (int dy2 = 1; dy2 <= nestH(off); ++dy2) {
                            const int px2 = ax2 + ppx * sgn * off + tdx * w2;
                            const int pz2 = az2 + ppz * sgn * off + tdz * w2;
                            const int py2 = sy + dy2;
                            if (py2 >= m_height) continue;
                            if (m_chunks.blockAt(px2, py2, pz2) != BlockRegistry::Air) continue;
                            m_chunks.setBlock(px2, py2, pz2, BlockRegistry::Cobweb, 0);
                        }
                }
            };

            // ③ pieceIntersection 交叉口：5×5 通高开口 + 四角 WoodFence 双层双柱（sy+1..sy+2）+
            //   Planks 柱冠（sy+3）——考据交叉口木柱支撑。
            const auto pieceIntersection = [&](int ix, int iz) {
                for (int dx2 = -2; dx2 <= 2; ++dx2)
                    for (int dz2 = -2; dz2 <= 2; ++dz2)
                        carveCell(ix + dx2, iz + dz2, sy, kTunnelH);
                for (int sx2 = -2; sx2 <= 2; sx2 += 4)
                    for (int sz2 = -2; sz2 <= 2; sz2 += 4) {
                        for (int dy2 = 1; dy2 <= 2; ++dy2) // 双层双柱
                            putStruct(ix + sx2, sy + dy2, iz + sz2, BlockRegistry::WoodFence, 0);
                        putStruct(ix + sx2, sy + 3, iz + sz2, BlockRegistry::Planks, 0); // 柱冠
                    }
            };

            // ② pieceCorridor 巷道：3 宽 × 3 高 + L 形折线（t565）；支撑组每 kSupportInterval 格
            //   （双柱 + 柱顶封盖：~55% 火把 / 其余木板柱冠，组级 25% 缺失、step 0/4 豁免）；残缺轨
            //   （hash 70% 保留）；顶角蛛网 12% + 巷壁矿石 15%（沿用 t565）；途中事件 evStep：
            //   ~60% pieceSpiderRoom / ~25% pieceIntersection / 余 pieceSlope（段 B 对角下切）。
            const auto pieceCorridor = [&](int ci, int dirIdx) {
                int dx = (dirIdx == 0) ? 1 : (dirIdx == 1) ? -1 : 0;
                int dz = (dirIdx == 2) ? 1 : (dirIdx == 3) ? -1 : 0;
                const quint32 rh = hashVoxel(mineSeed ^ (0xDEC0u + quint32(ci)), cx, sy, cz); // 巷道参数 hash（t565 盐）
                const int lenA = kTunnelLenMin + int((rh >> 2) & 0xFu) % (kTunnelLenMax - kTunnelLenMin + 1);
                const int lenB = kTunnelLenMin + int((rh >> 6) & 0xFu) % (kTunnelLenMax - kTunnelLenMin + 1);
                const int turnSign = ((rh >> 10) & 1u) ? 1 : -1; // 转向 ±90°（左 / 右转）
                // 巷道事件（每巷道恰一；腿 A evStep 落位）。
                const quint32 ev = hashVoxel(mineSeed ^ (0x1717u + quint32(ci) * 0x9E37u), cx, sy, cz);
                const unsigned evRoll = ev % 100u;
                const int evStep = 2 + int((ev >> 8) & 7u) % (lenA - 3); // 2..lenA-2（lenA ≥ 5）
                bool slopeLegB = false; // ④ pieceSlope：段 B 对角下切标记
                int curY = sy;          // 斜坡段当前地板 y
                // 起点：段 0 = 起点厅房缘外一格（+ 向 +kRoomHalf / - 向 -(kRoomHalf+1)）；段 1 = 段 A
                //   末端续接（t1012 ③ 回归修复：恢复 t565 推进条件 `leg==1 || step>0` —— 段 B 首步自段 A
                //   末格沿新向推进入拐角肘格；t1001 重写误改成段 B 入腿预推进 + `leg==0 && step>0` →
                //   段 B 整腿不推进、lenB 步重复刻同一截面（L 折线第二腿缺失 / 斜坡段变同柱逐层坑））。
                int ax = cx + (dx > 0 ? kRoomHalf : (dx < 0 ? -(kRoomHalf + 1) : 0));
                int az = cz + (dz > 0 ? kRoomHalf : (dz < 0 ? -(kRoomHalf + 1) : 0));
                for (int leg = 0; leg < 2; ++leg) {
                    const int legLen = (leg == 0) ? lenA : lenB;
                    bool inSlope = false;
                    if (leg == 1) {
                        const int ndx = turnSign * dz, ndz = -turnSign * dx; // 转向 ±90°：(dx,dz) → (dz,-dx) × turnSign
                        dx = ndx; dz = ndz;
                        inSlope = slopeLegB;
                    }
                    for (int step = 0; step < legLen; ++step) {
                        if (leg == 1 || step > 0) { ax += dx; az += dz; } // t565 推进条件（t1012 ③ 恢复；段 B 逐格推进）
                        if (inSlope && (step & 1) != 0) // pieceSlope：每 2 步地板降 1 格（对角下切，无楼梯块）
                            curY = std::max(curY - 1, kBedrockTop + 2); // 钳底不贴基岩
                        for (int w = -1; w <= 1; ++w) // 3 宽截面（地板 + kTunnelH 高空气）
                            carveCell(ax + w * (-dz), az + w * dx, curY, kTunnelH);
                        // 途中事件（腿 A evStep；carve 后落位 → 事件块不被本步 carve 清掉）。
                        if (leg == 0 && step == evStep) {
                            if (evRoll < kSpiderRoomPct) pieceSpiderRoom(ax, az, dirIdx);
                            else if (evRoll < kSpiderRoomPct + kIntersectionPct) pieceIntersection(ax, az);
                            else slopeLegB = true;
                        }
                        // 支撑组（每 kSupportInterval 段；斜坡段裸壁不设；step < 2×interval 豁免缺失 →
                        //   相邻支撑步距 4 可观测，阴性轮敏感）。双柱（w=±1，sy+1..sy+2）+ 柱顶封盖：
                        //   ~kTorchPct 立柱顶火把（sy+3 柱顶、巷道净空顶层内，考据「火把部分巷道」；
                        //   仅空气格 → 不出浮石），否则 Planks 柱冠（考据蓝图「layer 3 木板封柱顶」）。
                        if (!inSlope && step % kSupportInterval == 0) {
                            const bool skip = step >= 2 * kSupportInterval
                                && (hashVoxel(mineSeed ^ 0x50C7u, ax, sy + 1, az) % 100u) < kSupportSkipPct;
                            if (!skip) {
                                for (int w = -1; w <= 1; w += 2) {
                                    const int px = ax + w * (-dz), pz = az + w * dx;
                                    putStruct(px, curY + 1, pz, BlockRegistry::WoodFence, 0);
                                    putStruct(px, curY + 2, pz, BlockRegistry::WoodFence, 0);
                                    const int ty = curY + 3; // 柱顶（净空顶层内 → 火把可立）
                                    bool torch = false;
                                    if (ty < m_height) {
                                        const quint32 th = hashVoxel(mineSeed ^ 0x70C4u, px, ty, pz);
                                        if ((th % 100u) < kTorchPct
                                            && m_chunks.blockAt(px, ty, pz) == BlockRegistry::Air) {
                                            m_chunks.setBlock(px, ty, pz, BlockRegistry::Torch, 0);
                                            torch = true; // 块光 flood14 照亮巷道（呈现层建伪光源 delegate）
                                        }
                                    }
                                    if (!torch)
                                        putStruct(px, ty, pz, BlockRegistry::Planks, 0); // 木板柱冠
                                }
                            }
                        }
                        // t1011 壁挂火把（跨中补光，宏观可见照明口径）：跨中步（step ≡ 2 mod kSupportInterval）
                        //   两侧巷壁贴面格顶层（curY+kTunnelH，与柱顶火把同高带）各挂一只壁火把（成对壁灯，
                        //   斜坡段同样布设、curY 随地板降 → 斜坡段不再零火把全黑）。主形态 = 墙插（TorchAttach
                        //   墙插编码，mesher 倾柄位姿 / finishMiningAt 附着格定位同运行期墙火把）；**豁口回退**
                        //   = 巷壁被既有洞穴掏空（carveCaves 同位重叠 → w=±2 顶层空气，墙插守卫拒绝）时改落
                        //   贴面格地板火把（TorchFloor，支撑 = 地板 Stone/Planks，洞穴合并段照明不缺位 —— 用户
                        //   「见光无火把」巷道恰多为此形态）。仅空气格才落（不出浮空火把/不覆轨网）。先于本步
                        //   蛛网顶角判定 → 同格火把优先（蛛网仅空气格）。密度账：柱顶 55%×2 侧 + 跨中成对
                        //   必挂（墙插或地板回退）→ 相邻火把步距 ≤ 2 格，任何走廊位置火把本体或其 14 级光斑
                        //   可见（P-t1011 密度窗钉）。
                        if (step % kSupportInterval == 2) {
                            const int wy = curY + kTunnelH; // 顶层（柱顶同高带）
                            for (int w = -1; w <= 1; w += 2) {
                                const int px = ax + w * (-dz), pz = az + w * dx;  // 巷壁贴面格（w=±1）
                                const int wx = px + w * (-dz), wz = pz + w * dx;  // 支撑壁（w=±2）
                                // review0905 #4 卫生收口：本块原用裸 blockAt/setBlock（无显式界内判断）
                                //   ——极端越界巷道（margin 失守段）会借「blockAt 越界返 Air」走地板回退
                                //   分支多落火把 / 静默漏放，越界语义与同函数其余写入（putStruct 守卫口：
                                //   越界静默跳过）不一致。现贴面格 / 支撑壁越界 → 整对跳过（不落火把不回退
                                //   地板），写入收口 putStruct 同口 → 四向语义对齐（P-t1011 正常域数字不变）。
                                if (px < 0 || pz < 0 || px >= m_width || pz >= m_depth
                                    || wx < 0 || wz < 0 || wx >= m_width || wz >= m_depth)
                                    continue;
                                if (wy >= m_height) continue;
                                if (m_chunks.blockAt(px, wy, pz) != BlockRegistry::Air) continue;
                                if (m_chunks.blockAt(wx, wy, wz) != BlockRegistry::Air) { // 主形态：墙插
                                    quint8 attach = BlockRegistry::TorchFloor; // 兜底（dx/dz 单位轴 → 必命中下四分支）
                                    if (w * (-dz) == -1)      attach = BlockRegistry::TorchOnNX;
                                    else if (w * (-dz) == 1)  attach = BlockRegistry::TorchOnPX;
                                    else if (w * dx == -1)    attach = BlockRegistry::TorchOnNZ;
                                    else                      attach = BlockRegistry::TorchOnPZ;
                                    putStruct(px, wy, pz, BlockRegistry::Torch, attach);
                                } else if (curY >= 0 // 豁口回退：壁空（洞穴合并段）→ 贴面格地板火把
                                           && curY < m_height
                                           && m_chunks.blockAt(px, curY, pz) != BlockRegistry::Air
                                           && m_chunks.blockAt(px, curY + 1, pz) == BlockRegistry::Air) {
                                    putStruct(px, curY + 1, pz, BlockRegistry::Torch, 0);
                                }
                            }
                        }
                        // 残缺轨（中线 hash 保留率 kRailPct → 考据「铁轨残缺不连续」；斜坡段随地板
                        //   降层）。t1043：只做 hash 判定登记候选，**落块推迟**到 walk 后的干燥门统一
                        //   执行（见 railCandidates 声明处注释 + 落块处 t1043 注释）。
                        {
                            const int ry = curY + 1;
                            if (ry < m_height
                                && (hashVoxel(mineSeed ^ 0x5A17u, ax, ry, az) % 100u) < kRailPct) {
                                railCandidates.push_back({ax, ry, az});
                            }
                        }
                        // 蜘蛛网（顶角 w=±1 y=curY+kTunnelH ~12%；仅空气格，沿用 t565）。
                        for (int w = -1; w <= 1; w += 2) {
                            const int px = ax + w * (-dz);
                            const int pz = az + w * dx;
                            const int yy = curY + kTunnelH;
                            if (yy >= m_height) continue;
                            if ((hashVoxel(mineSeed ^ 0xC0Bu, px, yy, pz) % 100u) >= kCobwebPct) continue;
                            if (m_chunks.blockAt(px, yy, pz) != BlockRegistry::Air) continue;
                            m_chunks.setBlock(px, yy, pz, BlockRegistry::Cobweb, 0);
                        }
                        // 暴露矿石（w=±2 巷壁 y=curY+1..curY+kTunnelH ~15%；仅 Stone/Dirt 置换，沿用 t565）。
                        for (int w = -2; w <= 2; w += 4) {
                            const int px = ax + w * (-dz);
                            const int pz = az + w * dx;
                            for (int dy = 1; dy <= kTunnelH; ++dy) {
                                const int yy = curY + dy;
                                if (yy >= m_height) break;
                                const quint32 oh = hashVoxel(mineSeed ^ 0xCAFEu, px, yy, pz);
                                if ((oh % 100u) >= kOrePct) continue;
                                const quint8 ib = m_chunks.blockAt(px, yy, pz);
                                if (ib != BlockRegistry::Stone && ib != BlockRegistry::Dirt) continue;
                                m_chunks.setBlock(px, yy, pz,
                                                  ((oh >> 8) & 1u) ? BlockRegistry::IronOre
                                                                   : BlockRegistry::CoalOre, 0);
                            }
                        }
                    }
                }
            };

            // ① pieceStartRoom 起点厅：10×10 footprint + 双段拱顶（外环净高 kRoomRingH / 内芯 kRoomCoreH，
            //   外环 sy+4 一圈 Planks 拱带 = springing course）；出口 = hash 洗牌 4 主向取前 corridors 向
            //   （Fisher-Yates 于 eh，各向至多一条巷道 → 出口数 3..4）。
            {
                const quint32 eh = hashVoxel(mineSeed ^ 0xE017u, cx, sy, cz);
                int dirs[4] = { 0, 1, 2, 3 };
                for (int i = 3; i > 0; --i) { // Fisher-Yates（eh 驱动，确定性）
                    const int j = int((eh >> (2 * i)) & 3u) % (i + 1);
                    const int t = dirs[i]; dirs[i] = dirs[j]; dirs[j] = t;
                }
                const int corridors = 3 + int((eh >> 8) & 1u); // 3..4 条（考据 up to 4 exits）
                for (int dx = 0; dx < 2 * kRoomHalf; ++dx)     // 地板 + 双段拱顶 carve
                    for (int dz = 0; dz < 2 * kRoomHalf; ++dz) {
                        const bool edge = (dx == 0 || dx == 2 * kRoomHalf - 1
                                           || dz == 0 || dz == 2 * kRoomHalf - 1);
                        const int px = cx - kRoomHalf + dx, pz = cz - kRoomHalf + dz;
                        carveCell(px, pz, sy, edge ? kRoomRingH : kRoomCoreH);
                        if (edge) // 外环拱带（低段顶一圈木板）
                            putStruct(px, sy + kRoomRingH + 1, pz, BlockRegistry::Planks, 0);
                    }
                for (int ci = 0; ci < corridors; ++ci) // 放射巷道（洗牌向 → 各向至多一条）
                    pieceCorridor(ci, dirs[ci]);
                // t1011 起点厅角壁火把 ×4（入口厅宏观可见照明收口 —— 用户入井即见光亮；角格远离四轴
                //   出口嘴 → P-t1001 出口探针不受扰）。主形态 = 墙插（±X 外壁，TorchAttach 墙插 state）；
                //   壁被洞穴掏空 → 贴面格地板火把回退（同巷道壁挂口径，不出浮空火把）。
                for (int sx2 = -1; sx2 <= 1; sx2 += 2)
                    for (int sz2 = -1; sz2 <= 1; sz2 += 2) {
                        const int px = (sx2 < 0) ? cx - kRoomHalf : cx + kRoomHalf - 1;
                        const int pz = (sz2 < 0) ? cz - kRoomHalf : cz + kRoomHalf - 1;
                        const int py = sy + 2; // 头层（环带 sy+4 之下 → 拱带 36 格不受扰）
                        if (py >= m_height) continue;
                        if (m_chunks.blockAt(px, py, pz) != BlockRegistry::Air) continue;
                        if (m_chunks.blockAt(px + sx2, py, pz) != BlockRegistry::Air) {
                            putStruct(px, py, pz, BlockRegistry::Torch,
                                      (sx2 < 0) ? quint8(BlockRegistry::TorchOnNX)
                                                : quint8(BlockRegistry::TorchOnPX));
                        } else if (sy >= 0 && sy < m_height
                                   && m_chunks.blockAt(px, sy, pz) != BlockRegistry::Air
                                   && m_chunks.blockAt(px, sy + 1, pz) == BlockRegistry::Air) {
                            putStruct(px, sy + 1, pz, BlockRegistry::Torch, BlockRegistry::TorchFloor);
                        }
                    }
            }

            // t1043 裁-1 生成期防水落块（干燥门；伴随义务——见 railCandidates 声明处注释）：
            //   walk（含途中事件 / 蛛网支廊 / 斜坡段）全部 carve 完毕后，对每个候选轨格核查
            //   「干燥格」：本格仍为 Air（walk 中被后续 carve / 结构覆盖的候选如弃）且**切比雪夫
            //   距 2 的 5×5×5 邻域无水**。半径 2 的几何依据：placeUndergroundWaterPools 先于矿井
            //   → 池水存活层与巷道 breach 的接触面必落在巷道截宽（w=±2 外一圈）或巷道尽头外一格、
            //   且与轨行（curY+1）的竖直差 ≤ 巷道净高（≤3）→ 与最近候选轨的切比雪夫距 ≤ 2，半径 2
            //   恰覆盖全部直接 breach 几何（不虚放大面）。干燥候选才落块（state=0，连接位由下方
            //   统一重算覆盖）；湿候选跳过 = 「水下段不放轨」（轨只在干燥格放置）。登记残余：breach
            //   洪流沿巷道最长可再蔓 7 格（kMaxFlowLevel）—— 距水 >2 的轨不在本门内（全量防水=
            //   运行期流场模拟，超出「最小面」定案；parity-ledger 裁-1 行登记）。
            for (const auto &cand : railCandidates) {
                const int rx = cand[0], ry = cand[1], rz = cand[2];
                if (ry < 0 || ry >= m_height) continue;
                if (m_chunks.blockAt(rx, ry, rz) != BlockRegistry::Air) continue;
                bool wet = false;
                for (int ddx = -2; ddx <= 2 && !wet; ++ddx)
                    for (int ddy = -2; ddy <= 2 && !wet; ++ddy)
                        for (int ddz = -2; ddz <= 2 && !wet; ++ddz)
                            if (m_chunks.blockAt(rx + ddx, ry + ddy, rz + ddz)
                                == BlockRegistry::Water)
                                wet = true;
                if (wet) continue; // t1043 干燥门：水边 / 水下段不放轨（P-t1043b 行为面 + 源钉本体）
                m_chunks.setBlock(rx, ry, rz, BlockRegistry::Rail, 0);
                railCells.push_back({rx, ry, rz});
            }

            // t565 ④ 铁轨连接统一重算（直 / 拐角 / 十字形态由邻轨互连自动得出；与运行期 checkRailOnEdit
            //   同一权威 BlockRegistry::railConnections）。worldgen 直写不 emit（generate 末尾统一 worldChanged）。
            //   t666/t667：连接计算器改三高探针签名 —— 残缺轨以同层为主（斜坡段跨层由上 / 下探针兜住）；
            //   curState 传 0（worldgen 新铺轨无既有轴偏好）；返回值只留低 4 位连接（无 bit4/bit5 语义）。
            for (const auto &rc : railCells) {
                const int rx = rc[0], ry = rc[1], rz = rc[2];
                const auto probe = [&](int dx, int dz) -> BlockRegistry::RailProbe {
                    return { m_chunks.blockAt(rx + dx, ry, rz + dz),
                             m_chunks.blockAt(rx + dx, ry + 1, rz + dz),
                             m_chunks.blockAt(rx + dx, ry - 1, rz + dz) };
                };
                const quint8 con = BlockRegistry::railConnections(
                    BlockRegistry::Rail, 0,
                    probe(1, 0), probe(-1, 0), probe(0, 1), probe(0, -1));
                m_chunks.setBlock(rx, ry, rz, BlockRegistry::Rail, quint8(con & 0x0F));
            }

            // ⑥ 矿井箱（t1013 起 = 箱子矿车**生成标记**，R19.19 偏差「箱落地轨旁」退役）：从已铺 railCells
            //   hash 取一格，四向（hash 起转）首个空地落 Chest + ChestStateMineshaftFlag。worldgen 阶段
            //   实体管理器不存在（Entities 层不可从 World 触达 + 实体不进存档）→ 标记块即持久化面：进世界
            //   时 PlayerController::convertMineshaftChests 全图扫标记 → clearMineshaftChest 静默摘块 +
            //   ChestStore.registerCart（内容键 = 标记格）+ spawnChestCart（邻轨吸附落车）。玩家挖毁箱车
            //   → ChestStore 键条目清 → 存档后不再回生；未毁 → 条目持久 → 重进世界按键回生（内容物不丢）。
            if (!railCells.empty()) {
                const quint32 ch = hashVoxel(mineSeed ^ 0xC8E5u, cx, sy, cz);
                const std::array<int, 3> &rc = railCells[static_cast<size_t>(ch % railCells.size())];
                static const int kChestOff[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                const int rot = int((ch >> 8) & 3u);
                for (int t = 0; t < 4; ++t) {
                    const int px2 = rc[0] + kChestOff[(rot + t) & 3][0];
                    const int pz2 = rc[2] + kChestOff[(rot + t) & 3][1];
                    if (px2 < 0 || pz2 < 0 || px2 >= m_width || pz2 >= m_depth) continue;
                    if (m_chunks.blockAt(px2, rc[1], pz2) == BlockRegistry::Air) {
                        m_chunks.setBlock(px2, rc[1], pz2, BlockRegistry::Chest,
                                          BlockRegistry::ChestStateMineshaftFlag);
                        ++chests;
                        break;
                    }
                }
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: underground mineshafts =" << placed
            << "chests =" << chests; // 同 seed → 同计数（确定性核对；chests>0 = 箱落地轨旁在位）
}

// t485/t1003 沙漠神殿（见 world.h 头注释）。机制等价 MC 1.0 沙漠神殿 desert temple：沙漠地表的阶梯金字塔 +
//   正下方地下密室 + 4 宝藏箱 + 中央压力板下 TNT 陷阱（踩板引爆）。确定性散布（hashColumn + seed 偏移，PLAN §2-K）。
//
//   t1003 逐方块重建（对齐 minecraft.wiki Desert pyramid/Structure 考据 + R19.19 批头清单；子页逐格网格为
//   图片化表格未能机器转换，缺口按子页材料表 + 机制知识设计并如实登记）：
//
//   结构几何（中心 (cx,surfaceY,cz)，surfaceY = 沙漠沙顶 heightAt）：
//     A) 21×21 逐层金字塔壳（blueprint 层 0..10）：层 L 位于 y=surfaceY+L，半边表 kLayerHalf =
//        {10,10,9,9,8,8,7,7,6,6,5}（底 21 两层一收 → 顶 11）；每层实心 Sandstone 盘（不动 Bedrock），
//        顶层 11×11 CutSandstone 顶冠（偏差 6 时代口径：刻纹砂岩 → CutSandstone）。
//     B) 地面大厅（[-7,7]² × y∈[S+1,S+4] 清空气；外墙随层收进 1..3 格厚）。
//     C) 地面风玫瑰纹样：地板 y=S 菱域 |dx|+|dz|≤5 棋盘 WoolOrange（(dx+dz) 偶数格，24 格、四折旋转
//        对称）+ 中心 WoolBlue 1 格（偏差 3 时代口径：1.8 前羊毛版橙 / 蓝 —— 蓝块正下方即压板 / TNT 陷阱）。
//     D) 塔面安卡纹样：四面 WoolOrange（行表 v1..3 竖井+门侧条纹 / v4 横杠 / v5..6 环侧 / v7 环顶；
//        每面 21 格、四折旋转对称；写在 layer v 外环面 lateral=±kLayerHalf[v] 阶梯面上）。
//     E) 入口三处：正面（+Z）主入口 |u|≤1 × v∈[1,3] 阶梯门洞（外环 z=half..8 逐层穿环）+ v=4 |u|≤2
//        CutSandstone 门楣；后侧（-Z）两副入口 u=±5 × v∈[1,2]。
//     F) 顶窗 + 顶部暗腔：L8/L9 内腔 [-4,4]² 清空气；四面 v=8 |u|=0 穿环 1×1 窗洞。
//     G) 地下密室：地板 y=floorY=S-12（blueprint「Layer -11: Chest + Pressure Plate」口径：板 / 箱立于
//        floorY+1=S-11 层），内部 7×7×4 空气（y∈[S-11,S-8]），周界 Sandstone 壳封闭黑暗（不动 Bedrock）。
//     H) 4 宝藏箱：北墙 (±2,-3) / 南墙 (±2,+3) 各 2 只、朝向房心（state 低 2 位 = chestFrontFace），带
//        ChestStatePyramidFlag bit4 → isPyramidChest 返 true → 首开填充 pyramidChestPool 战利品。
//     I) TNT 陷阱：密室中央地板层 3×3 TntBlock（9 格）+ 中央格正上 StonePressurePlate（MC 石质压力板）。
//        玩家踩板 → playercontroller tick 扫 footprint（scanTntTraps）→ detonateTntBlock → destroySphereSilent
//        球形破坏 + 邻接 TNT 连环引爆（机制等价 MC 1.0 踩板引爆 TNT 陷阱）。
//     J) 暗渠（隐藏地窖入口）：+Z 面 u=+4 低处 1×2 入口 → 对角阶梯隧道（每步 z-1 / y-1，1×2 断面）沿
//        +X 侧潜入密室顶部侧壁开口（y∈[S-9,S-8]）→ 玩家跳落 treasure room（MC 神殿秘密通道机制）。
//
//   placeMineshaft 之后、fillWater 之前（仅 Desert 群系 → 与海 / 湖独立；fillWater 仅填海域低洼，沙漠内陆不被灌水）。
//   纯函数于 seed + biomeAt（经 hashColumn / hashVoxel）→ 同 seed 同神殿分布（PLAN §2-K）。仅扫候选沙漠格 → 不全图扫描。
void World::placeDesertTemple()
{
    constexpr int kPyramidHalf    = kDesertTempleHalf;      // 金字塔底半边（t1020 上收类常量：siteOk / 区域足迹同源）
    constexpr int kPyramidTopLayer = kDesertTempleTopLayer; // 顶层 layer 10（半边 5 → 11×11 CutSandstone 顶冠；blueprint 层 0..10）
    constexpr int kHallHalf       = 7;      // 地面大厅内半边（15×15 内厅）
    constexpr int kChamberFloorDrop = 12;   // 密室地板相对地表深度（= kDesertTempleChamberDrop 类常量同值；
                                            //   本字面量为 P-t1003 源码钉契约；blueprint「Layer -11:
                                            //   Chest + Pressure Plate」口径 → 板 / 箱层 = 地表 -11 = floorY+1）
    constexpr int kRoomHalf       = 3;      // 密室内部半边（7×7 内部）
    constexpr int kRoomH          = 4;      // 密室内部高度（Y 空气层数 y ∈ [floorY+1 .. floorY+kRoomH]）
    constexpr int kTntHalf        = 1;      // TNT 陷阱半边（3×3 = (2*1+1)²，置于密室地板层中央）
    // 逐层半边表（layer 0..10）：底 21 两层一收（21,21,19,...,11）→ 阶梯金字塔外形（blueprint 层宽序列）。
    constexpr int kLayerHalf[kPyramidTopLayer + 1] = { 10, 10, 9, 9, 8, 8, 7, 7, 6, 6, 5 };

    int placed = 0;

    // 单格写入辅助（越界 / 基岩守卫；与 placeStronghold put 同模式）。t1003 全几何经此两口。
    //   t1010 起上提至落位器外层 —— 概率主路径与保底补座两路共用（口一致 → 保底不产生口径外写入）。
    auto putSolid = [&](int px, int yy, int pz, quint8 id) {
        if (px < 0 || px >= m_width || yy < 0 || yy >= m_height
            || pz < 0 || pz >= m_depth) return;
        if (m_chunks.blockAt(px, yy, pz) == BlockRegistry::Bedrock) return; // 不动基岩
        m_chunks.setBlock(px, yy, pz, id);
    };
    auto carveAir = [&](int px, int yy, int pz) {
        if (px < 0 || px >= m_width || yy < 0 || yy >= m_height
            || pz < 0 || pz >= m_depth) return;
        if (m_chunks.blockAt(px, yy, pz) == BlockRegistry::Bedrock) return; // 不动基岩
        m_chunks.setBlock(px, yy, pz, BlockRegistry::Air);
    };
    // t1010 落位门单源收口（siteOk）：margin / 群系 / 海域 / 塔顶越界 / 密室贴基岩 五守卫，与旧内联
    //   逐条同序同语义（t485 原口径零变化）；概率主路径与保底补座共用 → 两路落位判据永不漂移。
    //   t1020：判据本体上收 desertTempleSiteOk()（本薄包装仅存 t1010 源码钉字面 + tryPlace 消费口）。
    auto siteOk = [&](int cx, int cz) {
        return desertTempleSiteOk(cx, cz);
    };
    // t1010 单座落位器（tryPlace）：过 siteOk 五守卫 → 几何 A..J 全量落地。几何体原样包一层作用域
    //   （缩进容器，逐字节零漂移 → t1003 源码钉 / 逐方块断言零回归）；返回是否实际落位。
    auto tryPlace = [&](int cx, int cz) {
        if (!siteOk(cx, cz)) return false;
        const int surfaceY = std::min(heightAt(cx, cz), m_height - 1);
        const int floorY = surfaceY - kChamberFloorDrop; // 密室地板 y（TNT 层）
        {

            // ── A) 21×21 逐层金字塔壳：每层实心 Sandstone 盘，顶层 11×11 CutSandstone 顶冠（偏差 6）。
            for (int layer = 0; layer <= kPyramidTopLayer; ++layer) {
                const int yy = surfaceY + layer;
                const int half = kLayerHalf[layer];
                const bool topCap = (layer == kPyramidTopLayer);
                for (int dx = -half; dx <= half; ++dx)
                    for (int dz = -half; dz <= half; ++dz)
                        putSolid(cx + dx, yy, cz + dz,
                                 topCap ? BlockRegistry::CutSandstone : BlockRegistry::Sandstone);
            }

            // ── B) 地面大厅：[-7,7]² × y∈[S+1,S+4] 清空气（外墙随层收进 1..3 格厚）。
            for (int dy = 1; dy <= 4; ++dy)
                for (int dx = -kHallHalf; dx <= kHallHalf; ++dx)
                    for (int dz = -kHallHalf; dz <= kHallHalf; ++dz)
                        carveAir(cx + dx, surfaceY + dy, cz + dz);

            // ── C) 地面风玫瑰纹样：菱域 |dx|+|dz|≤5 棋盘 WoolOrange（(dx+dz) 偶数格，24 格）+ 中心
            //       WoolBlue（偏差 3 时代口径；蓝块正下方即压板 / TNT 陷阱 —— 玩家定位暗室的考据地标）。
            for (int dx = -5; dx <= 5; ++dx) {
                for (int dz = -5; dz <= 5; ++dz) {
                    if (((dx + dz) & 1) != 0) continue;            // 棋盘半域（奇偶留空 → 棋盘观感）
                    if (std::abs(dx) + std::abs(dz) > 5) continue; // 菱域半径（风玫瑰外形）
                    if (dx == 0 && dz == 0) {
                        putSolid(cx, surfaceY, cz, BlockRegistry::WoolBlue); // 中心蓝块
                        continue;
                    }
                    putSolid(cx + dx, surfaceY, cz + dz, BlockRegistry::WoolOrange);
                }
            }

            // ── D) 塔面安卡纹样：行表 v → |u| 在场掩码（bit0=|u|0 … bit3=|u|3）。v1..3 竖井 + 门侧条纹 /
            //       v4 横杠 / v5..6 环侧 / v7 环顶 → 安卡（上环 + 横杠 + 下垂柱）；四面同一行表 → 四折旋转对称。
            //       review0905 #5 考据收口（minecraft.wiki Desert_Pyramid/Structure 蓝图 raw wikitext
            //       逐层解析，2026-09-07 抓取；原文归档 docs/review0905_wiki_desert_pyramid/）：MC 本尊
            //       完整安卡形（橙陶瓦环+横杠+垂柱、蓝陶瓦心）只在**地面层中央**（蓝图 Layer 0）与密室
            //       （Layer -4）各一次；外立面仅前侧双角塔面有「双竖橙条 + 錾制砂岩心」条带装饰（非完整
            //       安卡），门楣上方即切制砂岩（蓝图 Layer 3 前排 AAA），且本尊整体埋沙、无地面门洞 ——
            //       MC 外立面**本就无完整安卡**。本项目把安卡再创作居中于四面（登记偏差 3 时代羊毛口径的
            //       延伸）：正面被门洞 + 门楣截断 = 与 MC「门面门洞区无完整纹样」忠实同构，非缺陷
            //       （P-t1003 断言按此口径）。
            constexpr int kAnkhRowMask[kPyramidTopLayer + 1] = {
                0, 0b1001, 0b1001, 0b1001, 0b0111, 0b0100, 0b0100, 0b0011, 0, 0, 0
            };
            for (int v = 1; v <= 7; ++v) {
                const int half = kLayerHalf[v];
                for (int u = -3; u <= 3; ++u) {
                    if ((kAnkhRowMask[v] & (1 << (u < 0 ? -u : u))) == 0) continue;
                    // 四面同写：±Z 面 (u,·,±half) / ±X 面 (±half,·,u) —— 阶梯面逐层内收（考据塔面纹样）。
                    putSolid(cx + u, surfaceY + v, cz + half, BlockRegistry::WoolOrange);
                    putSolid(cx + u, surfaceY + v, cz - half, BlockRegistry::WoolOrange);
                    putSolid(cx + half, surfaceY + v, cz + u, BlockRegistry::WoolOrange);
                    putSolid(cx - half, surfaceY + v, cz + u, BlockRegistry::WoolOrange);
                }
            }

            // ── E) 入口三处：正面主入口（+Z：|u|≤1 × v∈[1,3]，外环 z=half..8 逐层穿环 → 阶梯门洞）+
            //       CutSandstone 门楣（v=4 |u|≤2 外环面）；后侧两副入口（-Z：u=±5 × v∈[1,2]）。
            for (int v = 1; v <= 3; ++v)
                for (int u = -1; u <= 1; ++u)
                    for (int z = kLayerHalf[v]; z >= 8; --z)
                        carveAir(cx + u, surfaceY + v, cz + z);
            for (int u = -2; u <= 2; ++u)
                putSolid(cx + u, surfaceY + 4, cz + kLayerHalf[4], BlockRegistry::CutSandstone);
            for (int v = 1; v <= 2; ++v)
                for (const int us : { -5, 5 })
                    for (int z = kLayerHalf[v]; z >= 8; --z)
                        carveAir(cx + us, surfaceY + v, cz - z);

            // ── F) 顶窗 + 顶部暗腔：L8/L9 内腔 [-4,4]² 清空气；四面 v=8 |u|=0 穿环 1×1 窗洞（z=half..5）。
            for (int dy = 8; dy <= 9; ++dy)
                for (int dx = -4; dx <= 4; ++dx)
                    for (int dz = -4; dz <= 4; ++dz)
                        carveAir(cx + dx, surfaceY + dy, cz + dz);
            for (int d = kLayerHalf[8]; d >= 5; --d) {
                carveAir(cx, surfaceY + 8, cz + d);
                carveAir(cx, surfaceY + 8, cz - d);
                carveAir(cx + d, surfaceY + 8, cz);
                carveAir(cx - d, surfaceY + 8, cz);
            }

            // ── G) 地下密室：地板面 y=floorY（dy=0，中央 3×3 由 I) 步换 TNT）/ 天花板 y=floorY+kRoomH+1
            //       壳 Sandstone，内部 7×7×4 清空气（y∈[floorY+1, floorY+4]，封闭黑暗；机制等价 MC 神殿
            //       砂岩密室）。
            for (int dy = 0; dy <= kRoomH + 1; ++dy) {
                const int yy = floorY + dy;
                for (int dx = -kRoomHalf - 1; dx <= kRoomHalf + 1; ++dx) {
                    for (int dz = -kRoomHalf - 1; dz <= kRoomHalf + 1; ++dz) {
                        const bool edge = (dy == 0 || dy == kRoomH + 1
                                           || dx == -kRoomHalf - 1 || dx == kRoomHalf + 1
                                           || dz == -kRoomHalf - 1 || dz == kRoomHalf + 1);
                        if (edge) putSolid(cx + dx, yy, cz + dz, BlockRegistry::Sandstone);
                        else      carveAir(cx + dx, yy, cz + dz);
                    }
                }
            }

            // ── H) 4 宝藏箱：北墙 (±2,-3) / 南墙 (±2,+3)，state 低 2 位朝房心（2=+Z / 3=-Z）+
            //       ChestStatePyramidFlag bit4 → 首开填充神殿战利品（坐标口径 blueprint Layer -11）。
            constexpr int kChestOff[4][3] = { { -2, 2, -3 }, { 2, 2, -3 }, { -2, 3, 3 }, { 2, 3, 3 } };
            for (const auto &c : kChestOff) {
                const int px = cx + c[0], py = floorY + 1, pz = cz + c[2];
                if (px < 0 || px >= m_width || py < 0 || py >= m_height
                    || pz < 0 || pz >= m_depth) continue;
                if (m_chunks.blockAt(px, py, pz) == BlockRegistry::Bedrock) continue; // 不动基岩（防御）
                m_chunks.setBlock(px, py, pz, BlockRegistry::Chest,
                                  quint8(c[1] | BlockRegistry::ChestStatePyramidFlag));
            }

            // ── I) TNT 陷阱：中央 3×3 TntBlock 置地板层 + 中央格正上 StonePressurePlate（MC 石质板）。
            //       踩板 → scanTntTraps → detonateTntBlock 球形破坏 + 邻接 TNT 连环引爆（9 格一链）。
            for (int dx = -kTntHalf; dx <= kTntHalf; ++dx)
                for (int dz = -kTntHalf; dz <= kTntHalf; ++dz)
                    putSolid(cx + dx, floorY, cz + dz, BlockRegistry::TntBlock);
            putSolid(cx, floorY + 1, cz, BlockRegistry::StonePressurePlate);

            // ── J) 暗渠（隐藏地窖入口）：+Z 面 u=+4 低处 1×2 入口 → 对角阶梯隧道（每步 z-1 / y-1，
            //       1×2 断面）沿 +X 侧潜入，k=10 端头 (4, S-9..S-8, 0) 与密室顶部空气层接通。
            carveAir(cx + 4, surfaceY + 1, cz + kLayerHalf[1]);
            carveAir(cx + 4, surfaceY + 2, cz + kLayerHalf[1]);
            for (int k = 1; k <= 10; ++k) {
                carveAir(cx + 4, surfaceY + 1 - k, cz + 10 - k);
                carveAir(cx + 4, surfaceY + 2 - k, cz + 10 - k);
            }
        } // 几何作用域结束（A..J 原样）
        return true;
    };

    // t1020 候选选择上收：概率主路径（48 格网格候选 × 45% 命中 × 抖动 ±12 → 过 siteOk 五守卫）与
    //   t1010 保底补座（主路径 0 落位且含沙漠群系 → 距世界中心最近合格列补座 ≥1）的选择段已收口
    //   desertTempleSites()（单一权威；rebuildStructureRegions 区域重推导同表）。本函数只消费表驱动
    //   落位（siteOk 预筛 → tryPlace 恒成功 → 落位数 = 候选数，与原口径一致）。
    for (const StructureSite &dtSite : desertTempleSites()) {
        if (tryPlace(dtSite.cx, dtSite.cz)) ++placed;
    }
    qInfo() << "worldgen: desert temples =" << placed; // 同 seed → 同计数（确定性核对）
}

// t486/t1004 丛林神殿（见 world.h 头注释）。机制等价 MC 1.0 丛林神殿 jungle temple：丛林地表的三层苔石
//   建筑 + 发射器箭陷阱 + 拉杆谜题藏宝。确定性散布（hashColumn + seed 偏移，PLAN §2-K）。
//
//   t1004 逐方块重建（对齐 minecraft.wiki Jungle pyramid/Structure 材料表 + 蓝图注记 + R19.19 批头清单；
//   子页逐格网格为图片化表格未能机器转换，缺口按材料表 + 机制知识设计并如实登记；层区间 -3..10 的地下
//   基座层省略 —— 两箱置于地表层【登记】）：
//
//   结构几何（中心 (cx,surfaceY,cz)，surfaceY = 丛林草顶 heightAt，floorY = surfaceY；footprint 15×15 半边 7，
//   三层形制：地面层内空 y∈[S+1,S+3] + 楼板 S+4 / 二层内空 S+5..S+7 + 楼板 S+8 / 三层 11×11（半边 5）内空
//   y∈[S+9,S+10] + 屋顶 S+11）：
//     A) 苔石混排（偏差口径：Cobble/MossyCobble 逐格 hash 40% 苔，机制等价 MC「cobblestone or mossy」
//        材料行）：地板 y=S 全幅 + 周界墙（|dx|=7 / |dz|=7，y S+1..S+3 / S+5..S+7）+ 楼板 S+4 / S+8 +
//        屋顶 S+11 + 隔断 / 龛墙 / 电路支座同族。
//     B) 地面布局：西入口（-X 墙 dz=0，2 高）；陷阱走廊（dz∈[-1,1]，dx∈[-6,-2]，北隔断 dz=-2 全高 /
//        南隔断 dz=2 dx∈[-6,-4]）；宝藏龛（dx∈[4,6], dz∈[-1,1]，墙 = x=3 列 dz∈[-2,1] + dz=±2 行 dx∈[4,6]，
//        角格 (3,·,2) 由组合锁汇流红石线封角）；西北侧室（dx∈[-6,-4], dz∈[-6,-4]，隔断 dz=-3 dx∈[-6,-4]
//        留 dx=-5 门洞 + dx=-3 列 dz∈[-6,-3]）。
//     C) 发射器陷阱 2 组（【偏差登记：MC 绊线钩+线 → 压力板触发】；「箭 2-14 发」编码于 dispenser state
//        bit[5:2]，运行期陷阱路径恒有箭为既登记行为，state 供考据读数）：
//        - 走廊中段（dx=-4）：±Z 侧隔断嵌 Dispenser 朝走廊（state 低 2 位 = 2(+Z)/3(-Z)）+ 走廊地板
//          CobblePressurePlate (dx=-4, dz=±1) —— 踩板 4 水平邻发射器 → scanDispenserTraps 射箭。
//        - 转角箱前（西北侧室北墙）：Dispenser (-5/-4, S+1, -7) 朝 +Z + 板 (-5/-4, S+1, -6)。
//     D) 拉杆谜题（【偏差登记：MC 3 拉杆 + 粘性活塞门 → 项目无活塞：三拉杆红石 AND 门 + IronDoor；
//        MC 谜题墙 3 刻纹石砖 → 3 拉杆底座 StoneBrick】【登记：MC 正确组合逐 seed 随机 → 本工程固定
//        全 ON 唯一组合】；布局受两条件约束——t869 粉形状输出语义（dot 粉 / 贯穿直线侧向不供电，供电走
//        「臂粉开放端 / 线端延长端 / 真实源直供」）+ 接收器 6 邻直读无形状（通电粉不得贴门格））：
//        南墙 3 拉杆（x=-3/0/+3，y=S+2，dz=6，附着 +Z 墙）→ 双粉支线（dz=5/4，dz=4 粉持 +Z 臂 →
//        形状输出指 +Z 侧）→ B 块（dz=3；拉杆 ON 直供 + 臂粉所指，两输入共挂）→ 侧附红石火把 NOT
//        （dz=2，附着 B 块）→ 汇流红石线（dz=1，x∈[-3,2]）+ 拐角粉 (2,dz=2)（3 号火把 -X 侧馈入）→
//        线端（x=2）+X 开放端直指终 NOT 座 B_m（3,dz=1，龛西墙格；贴邻 3 号火把常亮亦馈——两路均为
//        「非全 ON」信号）→ 终级 NOT 火把（4,dz=1，附 -X 邻 B_m）→ 门侧红石粉（4,S+2,dz=0，与门保持
//        1 格，6 邻直供）→ IronDoor（x=3 列 dz=0，2 格，仅红石驱动开合 t722）。红石拓扑 = 3 输入 AND
//        （NOT-NOR-NOT）：恰全 ON 组合开门，其余 7 组合恒闭 —— 粉电平变化回插脏集 + 火把自回插桥接
//        粉岛，约 5 tick 收敛（确定性方块事件链）。
//     E) 宝藏箱 2 只（ChestStateJungleFlag bit5 → isJungleTempleChest 首开填充 jungleTempleChestPool）：
//        主箱（谜题后龛内 dx=6, dz=0，朝 -X）+ 侧箱（西北侧室角 dx=-6, dz=-6，朝 +X）。
//     F) 垂直交通：CobbleStairs 双跑（地面→二层 dz=-5 列 x=-1..2；二层→三层 dz=-4 列 x=1..4，末阶嵌
//        楼板开口齐平）。蛛网 6 处定角装饰。
//
//   placeDesertTemple 之后、fillWater 之前（仅 Jungle 群系 → 与海 / 湖独立；fillWater 仅填海域低洼，丛林内陆
//   不被灌水）。纯函数于 seed + biomeAt（经 hashColumn / hashVoxel）→ 同 seed 同神殿分布（PLAN §2-K）。
//   仅扫候选丛林格 → 不全图扫描。**宝藏箱内容**：Chest 物品存 ChestStore，首开填充由 isJungleTempleChest
//   判定 → jungleTempleChestPool（骨头 / 腐肉 / 铁 / 金 / 钻石 / 箭 / 附魔书等）。
void World::placeJungleTemple()
{
    constexpr int kHalf           = kJungleTempleHalf; // 建筑外圈半边（t1020 上收类常量：siteOk / 区域足迹同源）
    constexpr int kMossyPct       = 40u;    // 苔石混排苔占比（逐格 hash；机制等价 MC「cobble or mossy」）

    // t1004 组合锁电路局部坐标（dz 行；全 circuit 位于南半场，门在 x=3 列 dz=0）。
    //   拉杆 x ∈ {-3, 0, +3}（y=S+2，dz=6，附 +Z 墙）；支线双粉 dz=5/4；B 块 dz=3；NOT 火把 dz=2；
    //   汇流粉 dz=1（x∈[-3,2]）+ 拐角粉 (2,dz=2)；B_m (3, dz=1，龛西墙格)；终 NOT 火把 (4, dz=1)；
    //   门侧粉 (4, S+2, dz=0)。
    static const int kLevers[3] = { -3, 0, 3 };
    // 汇流线平衡态（三 NOT 火把常亮馈电；state = conn<<4 | power：源邻 15，逐粉衰减 14；x=2 格持
    //   Nx+Pz 双臂）。拐角粉 (2,dz=2) 恒 0x2F（Nz 臂 + 3 号火把直馈 15）。
    //   ⚠ review0905 #2 耦合告示：本表 + 下方 kMergeElbowState 是**手写红石稳态字节**（worldgen 直写
    //   首帧，靠脏集回插 + 火把自回插 ~5 tick 收敛到真稳态）——与红石电力语义强耦合。改任何红石常量 /
    //   传播规则（火把馈电 15、粉逐格衰减、t869 形状连接、conn 编码）必须同步重推本表并重跑 P-t1004
    //   （逐格静态 + 8 组合行为腿）。登记与可选根治方案见 dev-plan t1004 回标。
    static const quint8 kMergeStates[6] = { 0x1F, 0x3E, 0x3E, 0x3F, 0x3E, 0x6E };
    static const quint8 kMergeElbowState = 0x2F;

    int placed = 0;
    const int templeSeed = m_seed + kJungleTempleSeedOff; // 丛林神殿哈希偏移（与 jungleTempleSites() 同源类常量）

    // 单格写入辅助（越界 / 基岩守卫；与 placeDesertTemple 同模式）。
    //   t1010 起上提至落位器外层 —— 概率主路径与保底补座两路共用（口一致 → 保底不产生口径外写入）。
    auto put = [&](int px, int yy, int pz, quint8 id, quint8 state = 0) {
        if (px < 0 || px >= m_width || yy < 0 || yy >= m_height
            || pz < 0 || pz >= m_depth) return;
        if (m_chunks.blockAt(px, yy, pz) == BlockRegistry::Bedrock) return; // 不动基岩
        m_chunks.setBlock(px, yy, pz, id, state);
    };
    auto carveAir = [&](int px, int yy, int pz) {
        if (px < 0 || px >= m_width || yy < 0 || yy >= m_height
            || pz < 0 || pz >= m_depth) return;
        if (m_chunks.blockAt(px, yy, pz) == BlockRegistry::Bedrock) return; // 不动基岩
        m_chunks.setBlock(px, yy, pz, BlockRegistry::Air);
    };
    // t1010 落位门单源收口（siteOk）：margin / 群系 / 海域 / 屋顶越界 / 地板贴基岩 五守卫，与旧内联
    //   逐条同序同语义（t486 原口径零变化）；概率主路径与保底补座共用 → 两路落位判据永不漂移。
    //   t1020：判据本体上收 jungleTempleSiteOk()（本薄包装仅存 t1010 源码钉字面 + tryPlace 消费口）。
    auto siteOk = [&](int cx, int cz) {
        return jungleTempleSiteOk(cx, cz);
    };
    // t1010 单座落位器（tryPlace）：过 siteOk 五守卫 → 几何 A..J 全量落地。几何体原样包一层作用域
    //   （缩进容器，逐字节零漂移 → t1004 源码钉 / 逐方块断言零回归）；返回是否实际落位。
    auto tryPlace = [&](int cx, int cz) {
        if (!siteOk(cx, cz)) return false;
        const int surfaceY = std::min(heightAt(cx, cz), m_height - 1);
        {
            // 苔石混排（逐格 hash 40% 苔 —— 机制等价 MC 材料行「cobblestone or mossy cobblestone」）。
            auto mixPut = [&](int px, int yy, int pz) {
                const bool mossy = (hashVoxel(templeSeed ^ 0x9E37u, px, yy, pz) % 100u) < kMossyPct;
                put(px, yy, pz, mossy ? BlockRegistry::MossyCobble : BlockRegistry::Cobble);
            };

            // ── A) 地板 y=S（全幅 15×15 混排；建筑坐于地表）。
            for (int dx = -kHalf; dx <= kHalf; ++dx)
                for (int dz = -kHalf; dz <= kHalf; ++dz)
                    mixPut(cx + dx, surfaceY, cz + dz);

            // ── B) 内空清场：地面层 [-6,6]²×[S+1,S+3] / 二层 [-6,6]²×[S+5,S+7] / 三层 [-4,4]²×[S+9,S+10]。
            for (int dy = 1; dy <= 3; ++dy) {
                for (int dx = -6; dx <= 6; ++dx)
                    for (int dz = -6; dz <= 6; ++dz) {
                        carveAir(cx + dx, surfaceY + dy, cz + dz);
                        carveAir(cx + dx, surfaceY + dy + 4, cz + dz); // 二层（S+5..S+7）
                    }
            }
            for (int dy = 9; dy <= 10; ++dy)
                for (int dx = -4; dx <= 4; ++dx)
                    for (int dz = -4; dz <= 4; ++dz)
                        carveAir(cx + dx, surfaceY + dy, cz + dz);

            // ── C) 周界墙（地面层 + 二层；西入口 -X 墙 dz=0 留 2 高门洞）。
            for (int d = -kHalf; d <= kHalf; ++d) {
                for (int sgn = -1; sgn <= 1; sgn += 2) {
                    for (int dy = 1; dy <= 3; ++dy) {
                        if (!(sgn == -1 && d == 0 && dy <= 2)) // -X 墙 dz=0 入口（2 高）
                            mixPut(cx + sgn * kHalf, surfaceY + dy, cz + d);
                        mixPut(cx + sgn * kHalf, surfaceY + dy + 4, cz + d); // 二层墙（S+5..S+7）
                        mixPut(cx + d, surfaceY + dy, cz + sgn * kHalf);
                        mixPut(cx + d, surfaceY + dy + 4, cz + sgn * kHalf);
                    }
                }
            }
            //   西入口门槛清障（-X 侧 dx=-7/-8 × dz=0 × 2 高切穿坡地：神殿坐于丛林斜坡时邻列地表高于
            //   地板会埋住门洞；后置树干仍可能长入门洞 —— MC wiki 亦载「外部方块（树叶等）可能生成进
            //   结构内部」，探针按净样口径弃样登记）。
            for (int dx = -8; dx <= -7; ++dx)
                for (int dy = 1; dy <= 2; ++dy)
                    carveAir(cx + dx, surfaceY + dy, cz + 0);

            // ── D) 隔断 / 龛墙 / 侧室（y S+1..S+3）。
            for (int dy = 1; dy <= 3; ++dy) {
                const int yy = surfaceY + dy;
                for (int dx = -6; dx <= -2; ++dx) mixPut(cx + dx, yy, cz - 2); // 走廊北隔断
                for (int dx = -6; dx <= -4; ++dx) mixPut(cx + dx, yy, cz + 2); // 走廊南隔断（短段）
                for (int dz = -2; dz <= 1; ++dz) mixPut(cx + 3, yy, cz + dz);  // 宝藏龛西墙列（角格 dz=2 由汇流线封）
                for (int dx = 4; dx <= 6; ++dx) {                              // 宝藏龛 ±Z 墙行
                    mixPut(cx + dx, yy, cz - 2);
                    mixPut(cx + dx, yy, cz + 2);
                }
                for (int dx = -6; dx <= -4; ++dx)                              // 西北侧室南隔断（dx=-5 留门洞）
                    if (dx != -5) mixPut(cx + dx, yy, cz - 3);
                for (int dz = -6; dz <= -3; ++dz) mixPut(cx - 3, yy, cz + dz); // 西北侧室东隔断列
            }

            // ── E) 楼板 / 三层墙 / 屋顶：S+4 / S+8 全幅 15×15（楼梯口嵌阶）；三层墙（|dx|=5 / |dz|=5，
            //       y S+9..S+10，立于 S+8 楼板上）；屋顶 S+11 全幅 11×11。
            for (int dx = -kHalf; dx <= kHalf; ++dx)
                for (int dz = -kHalf; dz <= kHalf; ++dz) {
                    if (!(dx == 2 && dz == -5)) mixPut(cx + dx, surfaceY + 4, cz + dz);
                    if (!(dx == 4 && dz == -4)) mixPut(cx + dx, surfaceY + 8, cz + dz);
                }
            for (int d = -5; d <= 5; ++d)
                for (int dy = 9; dy <= 10; ++dy)
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        mixPut(cx + sgn * 5, surfaceY + dy, cz + d);
                        mixPut(cx + d, surfaceY + dy, cz + sgn * 5);
                    }
            for (int dx = -5; dx <= 5; ++dx)
                for (int dz = -5; dz <= 5; ++dz)
                    mixPut(cx + dx, surfaceY + 11, cz + dz);

            // ── F) 垂直交通：CobbleStairs 双跑（state[1:0]=0 朝 +X 逐阶上行；末阶嵌楼板口齐平）。
            for (int i = 0; i < 4; ++i) {
                put(cx - 1 + i, surfaceY + 1 + i, cz - 5, BlockRegistry::CobbleStairs); // 地面 → 二层
                put(cx + 1 + i, surfaceY + 5 + i, cz - 4, BlockRegistry::CobbleStairs); // 二层 → 三层
            }

            // ── G) t1004 拉杆谜题（3 输入红石 AND 门 + IronDoor；电路拓扑见头注释 D 段）。布局受两条件
            //   约束：(1) t869 粉形状输出语义——dot 粉 / 贯穿直线侧向不供电 → 支线双粉臂指 B 块、汇流线端
            //   +X 开放端直指 B_m；(2) 接收器（铁门）6 邻直读无形状语义 → 通电粉不得贴门格 → 门侧粉退至
            //   (4,S+2,dz=0)（带座），与门保持 1 格。
            //   支座（混排）：支线双粉座 dz=5/4 + B 块座 dz=3（x=±3/0）、汇流粉座 dz=1（x∈[-3,2]）、
            //   拐角粉座 (2,dz=2)、门侧粉座 (4,S+1,dz=0)；B_m 落龛西墙格 (3,S+2,dz=1)（座 = 墙体本身）。
            for (const int lx : kLevers) {
                mixPut(cx + lx, surfaceY + 1, cz + 5);
                mixPut(cx + lx, surfaceY + 1, cz + 4);
                mixPut(cx + lx, surfaceY + 1, cz + 3);
            }
            for (int mx = -3; mx <= 2; ++mx) mixPut(cx + mx, surfaceY + 1, cz + 1);
            mixPut(cx + 2, surfaceY + 1, cz + 2);
            mixPut(cx + 4, surfaceY + 1, cz + 0);
            //   三支线双粉（dz=5/4，断电态）+ 三 B 块（dz=3；拉杆 ON 直供 B——真实源任意位供电——
            //   臂粉形状输出同指 B；两输入共挂一块）。
            for (const int lx : kLevers) {
                put(cx + lx, surfaceY + 2, cz + 5, BlockRegistry::RedstoneDust, 0);
                put(cx + lx, surfaceY + 2, cz + 4, BlockRegistry::RedstoneDust, 0);
                put(cx + lx, surfaceY + 2, cz + 3, BlockRegistry::Cobble);
            }
            //   三 NOT 火把（dz=2，附着 +Z 邻 B 块 = TorchOnPZ=4；常亮 ON）。
            for (const int lx : kLevers)
                put(cx + lx, surfaceY + 2, cz + 2, BlockRegistry::RedstoneTorch, 4);
            //   汇流粉线（dz=1，x∈[-3,2]）+ 拐角粉 (2,dz=2)（3 号 NOT 火把 -X 侧馈入；
            //   平衡态：三火把常亮，源邻 15 逐粉衰减 14；x=2 格持 Nx+Pz 双臂）。
            for (int mx = -3; mx <= 2; ++mx)
                put(cx + mx, surfaceY + 2, cz + 1, BlockRegistry::RedstoneDust, kMergeStates[mx + 3]);
            put(cx + 2, surfaceY + 2, cz + 2, BlockRegistry::RedstoneDust, kMergeElbowState);
            //   终 NOT：B_m (3,dz=1，龛西墙格；「非全 ON」双路信号——汇流线端 +X 开放端形状供电 + 贴邻
            //   3 号火把常亮直供) + 熄灭态火把 (4,dz=1)（附着 -X 邻 B_m = TorchOnNX=1 | OffFlag 0x08）。
            put(cx + 3, surfaceY + 2, cz + 1, BlockRegistry::Cobble);
            put(cx + 4, surfaceY + 2, cz + 1, BlockRegistry::RedstoneTorch,
                quint8(1 | BlockRegistry::RedstoneTorchStateOffFlag));
            //   门侧粉（(4,S+2,dz=0)，断电态；亮时 6 邻直供铁门——接收器无形状语义；与门保持 1 格，
            //   汇流粉任何残余电力都触不到门格）。
            put(cx + 4, surfaceY + 2, cz + 0, BlockRegistry::RedstoneDust, 0);
            //   三拉杆（y=S+2，dz=6，附 +Z 墙 = MechAttachOnPZ=3 → bit[3:1]=0x06；初态全 OFF）。
            //   拉杆底座 3 格 StoneBrick（偏差登记：MC 谜题墙刻纹石砖 → 石砖底座）。
            for (const int lx : kLevers) {
                put(cx + lx, surfaceY + 2, cz + 7, BlockRegistry::StoneBrick);
                put(cx + lx, surfaceY + 2, cz + 6, BlockRegistry::Lever, quint8(0x06));
            }
            //   IronDoor（x=3 列 dz=0，下格 state=0 朝 +X 合 / 上格 bit3；仅红石驱动开合 t722）。
            put(cx + 3, surfaceY + 1, cz + 0, BlockRegistry::IronDoor, 0);
            put(cx + 3, surfaceY + 2, cz + 0, BlockRegistry::IronDoor, 0x08);

            // ── H) 发射器陷阱 2 组（【偏差登记：绊线 → 压力板】；「箭 2-14 发」编码 state bit[5:2]）。
            auto putTrap = [&](int px, int yy, int pz, quint8 facing) {
                const quint8 ammo = quint8(2 + int(hashVoxel(templeSeed ^ 0xA2C0u, px, yy, pz) % 13)); // [2,14]
                put(px, yy, pz, BlockRegistry::Dispenser, quint8(facing | (ammo << 2)));
            };
            //   走廊中段（dx=-4）：±Z 隔断嵌发射器朝走廊 + 走廊地板压力板。
            putTrap(cx - 4, surfaceY + 1, cz - 2, 2); // 北侧朝 +Z（低 2 位编码，同 chestFrontFace）
            putTrap(cx - 4, surfaceY + 1, cz + 2, 3); // 南侧朝 -Z
            put(cx - 4, surfaceY + 1, cz - 1, BlockRegistry::CobblePressurePlate);
            put(cx - 4, surfaceY + 1, cz + 1, BlockRegistry::CobblePressurePlate);
            //   转角箱前（西北侧室北墙）：2 发射器朝 +Z + 2 压力板。
            putTrap(cx - 5, surfaceY + 1, cz - 7, 2);
            putTrap(cx - 4, surfaceY + 1, cz - 7, 2);
            put(cx - 5, surfaceY + 1, cz - 6, BlockRegistry::CobblePressurePlate);
            put(cx - 4, surfaceY + 1, cz - 6, BlockRegistry::CobblePressurePlate);

            // ── I) 宝藏箱 2 只（ChestStateJungleFlag → 首开填充丛林神殿战利品；低 2 位朝向）。
            put(cx + 6, surfaceY + 1, cz + 0, BlockRegistry::Chest,
                quint8(1 | BlockRegistry::ChestStateJungleFlag)); // 主箱（谜题龛内）朝 -X
            put(cx - 6, surfaceY + 1, cz - 6, BlockRegistry::Chest,
                quint8(BlockRegistry::ChestStateJungleFlag)); // 侧箱（侧室角）朝 +X（review0904 勘误：旧 state 1=−X 与两处「朝 +X」注释矛盾，判 state 错注释对）

            // ── J) 蛛网 6 处定角装饰（机制等价 MC 丛林神殿阴暗角落蛛网观感）。
            put(cx - 6, surfaceY + 3, cz - 6, BlockRegistry::Cobweb);
            put(cx + 6, surfaceY + 3, cz + 6, BlockRegistry::Cobweb);
            put(cx + 6, surfaceY + 7, cz - 6, BlockRegistry::Cobweb);
            put(cx - 6, surfaceY + 7, cz + 6, BlockRegistry::Cobweb);
            put(cx + 4, surfaceY + 10, cz + 4, BlockRegistry::Cobweb);
            put(cx - 4, surfaceY + 10, cz - 4, BlockRegistry::Cobweb);
        } // 几何作用域结束（A..J 原样）
        return true;
    };

    // t1020 候选选择上收：概率主路径（40 格网格候选 × 50% 命中 × 抖动 ±10 → 过 siteOk 五守卫）与
    //   t1010 保底补座（主路径 0 落位且含丛林群系 → 距世界中心最近合格列补座 ≥1）的选择段已收口
    //   jungleTempleSites()（单一权威；rebuildStructureRegions 区域重推导同表）。本函数只消费表驱动
    //   落位（siteOk 预筛 → tryPlace 恒成功 → 落位数 = 候选数，与原口径一致）。
    for (const StructureSite &jtSite : jungleTempleSites()) {
        if (tryPlace(jtSite.cx, jtSite.cz)) ++placed;
    }
    qInfo() << "worldgen: jungle temples =" << placed; // 同 seed → 同计数（确定性核对）
}

// t487/t665/t713 要塞（见 world.h 头注释）。机制等价 MC 1.0 要塞 stronghold：地下深处（Y<30）的石砖**走廊 +
//   房间 piece 拼接**结构 + 末地传送门房（悬空熔岩台 + 12 框架环）+ 图书馆（书架墙 + 蛛网 + 怪物蛋）+ 银鱼
//   刷怪笼。确定性散布（hashColumn + seed 偏移，PLAN §2-K），结构与 placeDungeons / placeJungleTemple 同源
//   （网格采样 + 概率筛选 + 抖动 + y 范围派生）。
//
//   布局（t713 扩建 ~4.5×：45×45 水平 footprint，dx/dz ∈ [-22,22]；竖直 dy ∈ [0,9]，墙高 8 → 内部净高 8 格
//   （t759 自墙高 5 / 净高 5 加高 3 —— 传送门房框架上方通行空间修复，见 kWallH 注释））：
//     - 地板（dy=0）与顶板（dy=9）：全幅 StoneBrick（封闭黑暗，机制等价 MC 1.0 要塞石砖地牢氛围）。
//     - **内部空间**（走廊 / 房间内部，dy 1..8）：清 Air 玩家可走；其余非内部格 = 石砖墙（hash 变体：
//       ~10% MossyCobble 苔石 / ~8% MonsterEgg 怪物蛋嵌墙 —— 机制等价 MC 要塞石砖墙混嵌银鱼怪物蛋）。
//     - **中央大厅**（13×13，dx/dz ∈ [-6,6]）：四向走廊交汇点 + 四角石砖承重柱（dy 1..8，随 kWallH 撑到顶）。
//     - **东入口走廊**（5 宽×15 深，x 7..21, z -2..2）：玩家从东侧周界墙门洞走入。
//     - **东北 / 东南储藏龛**（7×5，x 12..18, z ∓8..∓4；1 宽门洞 x 14..16 接走廊）：空置龛 + 蛛网（观感扩容）。
//     - **北走廊**（5 宽，x -2..2, z -11..-7）→ **传送门房**（25×10，x -12..12, z -21..-12）：北侧环沟岩浆河
//       （dy=0，72 格，被高台 / 石砖栏 / 周界墙三面封闭 → 静态不流）+ **石砖高台**（13×6，x -6..6, z -21..-16，
//       dy 1..3 实心，顶面 y=4 可站立）+ 中轴 **3 级石砖楼梯**（dz -15..-13，dy 3..1，每步 Δ0.5 ≤ auto-step
//       0.55 → 玩家从房内地面 y=1 走上高台顶 y=4；台下实心填充不悬浮）+ **12 框架环**（EndPortal=111 框架，
//       中心 (0, 4, -18) 标准 ±2 方形环；~10% 预置末影之眼 state 激活，机制等价 MC 1.0 框架预置眼睛；t664
//       激活/完整性机制接线）+ **3×3 岩浆盆**（环内圈正下方一格 dy=3 —— 门面（dy=4）正下方，机制等价 MC 1.0
//       传送门房熔岩池位置）+ **银鱼刷怪笼**（环中心正上方 (0, 5, -18)，机制等价 MC 1.0 要塞传送门房上方
//       silverfish spawner）。
//     - **西走廊**（5 宽，x -11..-7）→ **图书馆**（10×17，x -21..-12, z -8..8）：四壁 Bookshelf 书架墙（附魔台
//       加成来源 t474）+ 中央书架岛 + 蛛网 + 石砖台阶装饰。
//     - **南门洞**（x -1..1, z=7）→ **战利品/银鱼房**（17×11，x -8..8, z 8..18）：银鱼刷怪笼（Spawner +
//       SpawnerStateSilverfish → tickSpawners 刷 Silverfish）+ 战利品箱（Chest + ChestStateStrongholdFlag
//       → 首开填要塞战利品含末影之眼）。
//     - **走廊装饰**：内部空间确定性散布 Cobweb 蛛网（~8%，仅 dy=1 贴地，不悬空；跳过全部设施房 → 不再
//       误盖框架 / 平台 / 楼梯 —— t682 楼梯悬浮挡路根因是散布楼梯装饰，t713 移除楼梯装饰只留蛛网）。
//   placeJungleTemple 之后、fillWater 之前（独立于海平面；fillWater 仅填地表低洼 → 地下要塞不被灌水）。
//   纯函数于 seed（hashColumn / hashVoxel）→ 同 seed 同要塞分布（PLAN §2-K）。仅扫候选格 → 不全图扫描。
void World::placeStronghold()
{
    constexpr int kStrongholdGrid  = 40;    // 候选网格间距（略密于矿井 36 → 要塞稀有但可寻；160×160 世界约 1-2 座）
    constexpr unsigned kStrongholdPct = 55u; // 候选命中概率（仅地下深处候选 → 已天然稀有；55% → 每网格平均 ~0.55 座）
    constexpr int kBedrockTop      = 4;     // 不动基岩顶（同 placeDungeons / placeMineshaft / placeJungleTemple）
    constexpr int kStrongholdMaxY  = 30;    // 要塞最高 y（spec「Y<30 地下深」；避开近地表 / 仅地下深处）
    constexpr int kSurfaceGap      = 5;     // 与地表保留的最小距离（要塞上方至少 5 格石顶 → 不破地表、封闭黑暗）
    constexpr int kHalf            = kStrongholdHalf; // 建筑外圈半边（45×45 = (2*22+1)² 外圈；t713 扩建自 21×21 约 4.5× 面积）。
                                                       //   t1000 起引用类常量 kStrongholdHalf（与 insideStronghold
                                                       //   判定同源，防两处字面量漂移）。
    constexpr int kMargin          = kHalf + 1; // 留边界（外圈半边 22 + 抖动余量 → 半径 ≤ 23 不越界）
    constexpr int kWallH           = kStrongholdWallH; // 墙体高度层数（dy 1..8；地板 dy=0 / 顶板 dy=9 → 净高 8 格）。t759
                                            //   5→8：传送门房净空修复 —— 框架立于高台顶 dy=4，旧净高 5 时框架
                                            //   之上仅 1 格 Air（玩家 1.8 高无法跨过框架环走进环中心）；整体加高 3
                                            //   后框架顶之上 4 格 Air ≥ 验收「至少 3 格」。取「顶板上移 + 墙加高」
                                            //   全局方案而非仅传送门房局部抬顶：框架层 dy=4 与全部相对坐标零变化
                                            //   （B5 读档反推 / m_strongholdPortalY 记录值零回归），且避免局部抬顶
                                            //   在新旧顶板交界处留「天然地形邻接面」（洞穴恰过即漏光破封闭黑暗）。
                                            //   入口走廊 / 北走廊 / 楼梯同为内部空间 → 净高同步 8（同步检查通过）。
                                            //   worldgen 常量改动仅新世界生效（旧存档体素不回填，B5 反推自适应）。
                                            //   t1000 起引用类常量 kStrongholdWallH（同源防漂移）。

    int placed = 0;
    const int strongSeed = m_seed + 26513; // 要塞哈希偏移（与其它 worldgen hashColumn 解耦；纯整数加，确定性）

    // t564：候选要塞坐标集（cx, cy, cz）—— 循环内只收集、不放置；循环后选距出生点最近的一座放置
    //   （全图至多一个末地传送门）。
    std::vector<std::array<int, 3>> candidates;

    // 单座要塞放置器（placeAt）：put 捕获 placeAt 的 (cx,cy,cz)（与旧内联版 put 捕获循环变量的语义一致）。
    auto placeAt = [&](int cx, int cy, int cz) {
        // 单格写入辅助（越界 / 基岩守卫；与 placeDungeons wallBlock 同模式）。state 可选（默认 0，
        //   与 4 参数 setBlock 语义一致；Spawner 银鱼 flag / Chest 要塞 flag / 楼梯朝向走 5 参数版）。
        auto put = [&](int dx, int dy, int dz, quint8 id, quint8 state = 0) {
            const int px = cx + dx, yy = cy + dy, pz = cz + dz;
            if (px < 0 || px >= m_width || yy < 0 || yy >= m_height || pz < 0 || pz >= m_depth) return;
            const quint8 cur = m_chunks.blockAt(px, yy, pz);
            if (cur == BlockRegistry::Bedrock) return; // 不动基岩
            if (state == 0)
                m_chunks.setBlock(px, yy, pz, id);
            else
                m_chunks.setBlock(px, yy, pz, id, state);
        };

        // ── t1002 要塞 piece 链重建（R19.19 最大项；逐方块，形制忠实 minecraft.wiki/w/Stronghold 考据，
        //    piece 全自有命名 StartSpiral / FiveWay / PortalRoom / LibraryLarge / LibrarySmall /
        //    PrisonHall / StoreRoom / RoomCrossEmpty / RoomCrossFountain / RoomCrossPillar /
        //    Corridor / ChestCorridor / FillerCorridor / StairsStraight）。引擎三段式：
        //    A) 实心壳：45×45 全幅地板 (dy=0) / 顶板 (dy=9) 普通石砖（行走面统一 + t759「框架顶 +5 层
        //       顶板」契约）+ dy1..8 变体砖**逐块随机**：普通 45% / 苔石砖 30% / 裂纹 20% / 怪物蛋 5%
        //       （hashVoxel 逐体素确定性；机制等价 MC 要塞石砖 metadata 随机。替换 t713 旧「~8% 蛋 +
        //       ~10% 苔圆石」嵌墙口径，差额属逐块随机化登记；苔圆石退役、换真苔石砖 t998）；
        //    B) piece 链派型（不写块）：固定候选格架（17 slot 无重叠划分 + 16 门洞表，格架即布局契约，
        //       注释即图）+ hash 驱动权重表逐浮动 slot 派型（房间表 / 走廊表分列）+ 固定 spine 链深标注
        //       （StartSpiral=1 → … → PortalRoom=5 ≥ kPortalChainDepth=5 契约；图书馆 slot 链深 ≥
        //       kLibraryMinChainDepth=4）+ 失败换子 seed 重试（kStrongholdChainAttempts 上限防御；
        //       当前格架构造上恒过，循环保留为格架未来演化的防御）；
        //    C) 逐 piece 落地：先全量掏刻内部（Air dy1..8）+ 门洞（1 宽 × 2 高），再逐 piece 家具盖章
        //       （书架 / 阁楼木板 / 栅栏 / 梯 / 铁栏杆牢房 / 铁门 / 岩浆盆 / 12 框架环 / 刷怪笼逐方块）。
        //    传送门房**固定于原点局部坐标**：环心 (0,4,-18) ≡ 类常量 kStrongholdPortalDy/Dz、房体与
        //    t713 旧几何同位 → B5 读档框架反推 / insideStronghold 足迹 / t1000 成就 bounds 全链零改动
        //    （t1000 依赖选型 (a) 强化版：不放宽常量、链半径天然 ≤ ±22，P-t1000 零演化）。
        //    半径钳制登记：MC 链 ≤50 间 / 水平半径 112；本工程 160² 世界钳到 45×45 盒 → 17 间紧凑
        //    piece（≤ kStrongholdPieceCap=50 ✓）；传送门房链深按 piece 路径长度口径 =5（MC「距起点
        //    ≥5 间」）；图书馆 1-2 座（MC 0-2 → 保证 ≥1 属登记偏差：探针稳定 + 探索价值）；转角 piece
        //    未单列 slot（格架转向由 FiveWay 四臂 / StartSpiral 三出口承担，登记近似）；t713 南房双
        //    银鱼笼 / 双箱随旧布局退役（登记：银鱼由 5% 怪物蛋墙 ~430 格 + 传送门房笼承担，箱由图书馆 /
        //    储藏室 / 箱走廊承担）。

        // ── Phase A：实心壳（地板 / 顶板普通石砖；墙体逐块变体）。────────────────────────────────
        for (int dx = -kHalf; dx <= kHalf; ++dx) {
            for (int dz = -kHalf; dz <= kHalf; ++dz) {
                put(dx, 0, dz, BlockRegistry::StoneBrick);          // 地板（普通石砖：行走面统一）
                put(dx, kWallH + 1, dz, BlockRegistry::StoneBrick); // 顶板（普通石砖：t759 契约层）
                for (int dy = 1; dy <= kWallH; ++dy) {
                    // 墙体逐块变体（确定性 hashVoxel → 同 seed 同散布，PLAN §2-K。**阴性轮钉**：比例
                    //   字面 45/75/95 被 P-t1002 源码钉定）。
                    const quint32 wv = hashVoxel(strongSeed ^ 0x5102u, cx + dx, cy + dy, cz + dz);
                    const unsigned m = wv % 100u;
                    put(dx, dy, dz, (m < 45u)  ? BlockRegistry::StoneBrick
                                  : (m < 75u)  ? BlockRegistry::MossyStoneBrick
                                  : (m < 95u)  ? BlockRegistry::CrackedStoneBrick
                                               : BlockRegistry::MonsterEgg);
                }
            }
        }

        // ── Phase B：piece 链派型（格架 + 权重表 + 链深 + 重试防御；不写块）。────────────────────
        constexpr int kStrongholdPieceCap      = 50; // 链 piece 数上限（wiki ≤50；格架 17 ≤ 50 static_assert 钉）
        constexpr int kPortalChainDepth        = 5;  // 传送门房最小链深（wiki「距起点 ≥5 间」；spine 恒 5 ✓）
        constexpr int kLibraryMinChainDepth    = 4;  // 图书馆最小链深（wiki「距起点 ≥4 间」；图书馆 slot 深 5/7 ✓）
        constexpr int kStrongholdChainAttempts = 8;  // 链派型失败换子 seed 重试上限（防御）

        enum PieceKind {
            PieceStartSpiral, PieceFiveWay, PiecePortalRoom,
            PieceLibraryLarge, PieceLibrarySmall, PiecePrisonHall, PieceStoreRoom,
            PieceRoomEmpty, PieceRoomFountain, PieceRoomPillar,
            PieceCorridor, PieceChestCorridor, PieceFillerCorridor, PieceStairsStraight
        };
        // 格架 slot 表（footprint 含 1 圈墙；x0<x1 / z0<z1；z 负=北）。17 slot 互不重叠（静态推演核对：
        //   相邻 slot 共享 2 格厚墙带，如 P6 墙列 x=-12 与 C5 墙列 x=-11），depth = 自 StartSpiral 起
        //   piece 路径长度（1..7 全部 ≤ 链上限；探针钉 spine 行）。fixed: -1 = 浮动（权重表派型）。
        struct ShSlot { int x0, z0, x1, z1, depth, fixed; };
        constexpr ShSlot slotTable[17] = { // 命名避讳 Qt 空宏 `slots`（qobjectdefs.h），宏吞数组名编译期即碎
            //    x0   z0   x1   z1  depth fixed
            {  -4,  13,   4,  21, 1, PieceStartSpiral    }, // S0 起始螺旋梯（三出口 N/W/E）
            {  -2,   8,   2,  12, 2, PieceCorridor       }, // C1 南北走廊（spine 固定）
            {  -5,  -3,   5,   7, 3, PieceFiveWay        }, // P2 五向交叉（四臂 N/S/E/W）
            {  -2, -10,   2,  -4, 4, PieceStairsStraight }, // C3 北向直梯段（spine 固定；登门前圆石楼梯坡）
            { -13, -22,  13, -11, 5, PiecePortalRoom     }, // PR 传送门房（固定局部坐标，环心 (0,4,-18)）
            { -11,  -2,  -6,   2, 4, -1                  }, // C5 西臂走廊（浮动）
            { -22,  -6, -12,  10, 5, PieceLibraryLarge   }, // P6 大图书馆（固定；链深 5 ≥ 4 ✓）
            { -19, -11, -15,  -7, 6, -1                  }, // C7 西北连接走廊（浮动）
            { -22, -21, -14, -12, 7, -1                  }, // P8 西北角房（浮动；第二图书馆候选，链深 7 ≥ 4 ✓）
            {   6,  -2,  11,   2, 4, -1                  }, // C9 东臂走廊（浮动）
            {  12,  -6,  22,  10, 5, PiecePrisonHall     }, // P10 监狱厅（固定；铁栏杆牢房 + 铁门）
            {  15, -11,  19,  -7, 6, -1                  }, // C11 东北连接走廊（浮动）
            {  14, -21,  22, -12, 7, -1                  }, // P12 东北角房（浮动；第二图书馆候选）
            { -11,  15,  -5,  19, 2, -1                  }, // C13 西南走廊（浮动）
            { -22,  13, -12,  21, 3, PieceStoreRoom      }, // P14 储藏室（固定；圆石芯 + 木板阁楼 + 梯 + 上层箱）
            {   5,  15,  11,  19, 2, -1                  }, // C15 东南走廊（浮动）
            {  12,  13,  22,  21, 3, -1                  }, // P16 东南角房（浮动）
        };
        constexpr int kSlotCount       = int(sizeof(slotTable) / sizeof(slotTable[0]));
        constexpr int kSlotPortal      = 4;
        constexpr int kSlotLibLarge    = 6;
        constexpr int kSlotLibPocketA  = 8;
        constexpr int kSlotLibPocketB  = 12;
        static_assert(kSlotCount <= kStrongholdPieceCap, "stronghold piece chain must stay within wiki 50-piece cap");
        static_assert(slotTable[kSlotPortal].depth >= kPortalChainDepth, "portal room chain depth contract");
        static_assert(slotTable[kSlotLibLarge].depth >= kLibraryMinChainDepth, "library chain depth contract");
        // 门洞表（16 处：相邻 slot 墙带上掏 1 宽 × 2 高通行口；axis=1 → x=fix, z∈[lo,hi]；axis=0 → z=fix, x∈[lo,hi]）。
        struct ShDoor { int fix, lo, hi, axis; };
        constexpr ShDoor doors[16] = {
            {   0,  12,  13, 1 }, // S0↔C1
            {   0,   7,   8, 1 }, // C1↔P2
            {   0,  -4,  -3, 1 }, // P2↔C3
            {   0, -11, -10, 1 }, // C3↔PR（传送门房入口——铁栏杆格栅盖章见 stampPortalRoom g1）
            {   0,  -6,  -5, 0 }, // P2↔C5
            {   0, -12, -11, 0 }, // C5↔P6
            { -17,  -7,  -6, 1 }, // P6↔C7
            { -17, -12, -11, 1 }, // C7↔P8
            {   0,   5,   6, 0 }, // P2↔C9
            {   0,  11,  12, 0 }, // C9↔P10
            {  17,  -7,  -6, 1 }, // P10↔C11
            {  17, -12, -11, 1 }, // C11↔P12
            {  17,  -5,  -4, 0 }, // S0↔C13
            {  17, -12, -11, 0 }, // C13↔P14
            {  17,   4,   5, 0 }, // S0↔C15
            {  17,  11,  12, 0 }, // C15↔P16
        };
        // 权重表（权重自定、形制忠实；**阴性轮钉**：表行字面被 P-t1002 源码钉定，打乱即红）。
        //   房间浮动表（P8/P12/P16 三间）：房间交叉变体（空 / 喷泉 / 柱）+ 小图书馆。
        struct ShWeight { PieceKind kind; int w; const char *name; };
        const ShWeight roomTable[] = {
            { PieceRoomEmpty,    6, "RoomCrossEmpty"    },
            { PieceRoomFountain, 5, "RoomCrossFountain" },
            { PieceRoomPillar,   5, "RoomCrossPillar"   },
            { PieceLibrarySmall, 4, "LibrarySmall"      },
        };
        //   走廊浮动表（C5/C7/C9/C11/C13/C15 六段）：直走廊（50% 侧龛）/ 箱走廊 / 填充走廊。
        const ShWeight corrTable[] = {
            { PieceCorridor,      10, "CorridorStraight" },
            { PieceChestCorridor,  4, "ChestCorridor"    },
            { PieceFillerCorridor, 4, "FillerCorridor"   },
        };
        auto isCorrSlot = [](int i) { return i == 5 || i == 7 || i == 9 || i == 11 || i == 13 || i == 15; };
        auto weightedPick = [&](const ShWeight *table, int n, int sub, int slotIdx) {
            int total = 0;
            for (int i = 0; i < n; ++i) total += table[i].w;
            unsigned r = hashVoxel(sub ^ 0x51CEu, 101 + slotIdx, 7, 0) % unsigned(total);
            for (int i = 0; i < n; ++i) {
                if (r < unsigned(table[i].w)) return table[i].kind;
                r -= unsigned(table[i].w);
            }
            return table[n - 1].kind; // 不可达兜底
        };
        // 传送门房保证生成旗（MC：传送门房必生成，缺则整链重开——本引擎格架固定位置硬保证 + 核验防御；
        //   **阴性轮钉**：置 false → 传送门房哈希概率化且不重试 → P-t1002 传送门腿恰红）。
        constexpr bool kPortalGrateGuaranteed = true;
        PieceKind kind[kSlotCount] = {};
        bool portalStamped = false;
        int  attempt = 0;
        for (; attempt < kStrongholdChainAttempts; ++attempt) {
            const int sub = strongSeed + attempt * 7919; // 子 seed（重试防御；确定性 → 同 seed 同链）
            for (int i = 0; i < kSlotCount; ++i) {
                if (slotTable[i].fixed >= 0) { kind[i] = PieceKind(slotTable[i].fixed); continue; }
                kind[i] = weightedPick(isCorrSlot(i) ? corrTable : roomTable,
                                       isCorrSlot(i) ? 3 : 4, sub, i);
            }
            // 图书馆座数 1-2（登记偏差 MC 0-2 → 保证 ≥1）：第二座挂 P8/P12 之一（链深 7 ≥ 4 ✓）。
            const int nLib = 1 + int(hashVoxel(sub ^ 0xB00Cu, 202, 0, 0) % 2u);
            if (nLib == 2)
                kind[hashVoxel(sub ^ 0xB00Du, 203, 0, 0) % 2u ? kSlotLibPocketA : kSlotLibPocketB] = PieceLibrarySmall;
            portalStamped = kPortalGrateGuaranteed
                            || (hashVoxel(sub ^ 0x9E9u, 205, 0, 0) % 100u) < 55u; // 摘除保证 → 概率化
            // 链契约核验：传送门房在场 + 图书馆 ∈ [1,2] 座 + piece 数 ≤ 上限。构造上恒过（portal 固定
            //   slot / P6 固定大图书馆），循环为格架未来演化的防御（不重试无保证的 portal——「无保证」
            //   语义即如此）。
            int libs = 0;
            for (int i = 0; i < kSlotCount; ++i)
                if (kind[i] == PieceLibraryLarge || kind[i] == PieceLibrarySmall) ++libs;
            if ((!kPortalGrateGuaranteed || portalStamped) && libs >= 1 && libs <= 2
                && kSlotCount <= kStrongholdPieceCap)
                break;
        }

        // ── Phase C：逐 piece 落地（先全量掏刻 + 门洞，后家具盖章）。────────────────────────────
        auto carveInterior = [&](const ShSlot &s) {
            for (int dx = s.x0 + 1; dx <= s.x1 - 1; ++dx)
                for (int dz = s.z0 + 1; dz <= s.z1 - 1; ++dz)
                    for (int dy = 1; dy <= kWallH; ++dy)
                        put(dx, dy, dz, BlockRegistry::Air);
        };
        for (int i = 0; i < kSlotCount; ++i) carveInterior(slotTable[i]);
        for (const ShDoor &d : doors)
            for (int b = d.lo; b <= d.hi; ++b)
                for (int dy = 1; dy <= 2; ++dy)
                    put(d.axis ? d.fix : b, dy, d.axis ? b : d.fix, BlockRegistry::Air);

        // slot 内部矩形（footprint 内缩 1 圈墙）。
        auto iRect = [](const ShSlot &s, int &ix0, int &iz0, int &ix1, int &iz1) {
            ix0 = s.x0 + 1; iz0 = s.z0 + 1; ix1 = s.x1 - 1; iz1 = s.z1 - 1;
        };

        // StartSpiral（起始螺旋梯）：中央承重柱 + 绕柱螺旋踏步（4 石砖楼梯 + 6 石砖台阶；wiki SPSD
        //   187 砖 + 6 平滑石台阶——平滑石台阶方块本工程未收录 → 石砖台阶等价替（登记）；MC 多层盘旋
        //   压平为绕柱装饰坡，单层引擎登记）+ 2 火把。
        auto stampStartSpiral = [&](const ShSlot &s) {
            const int mx = (s.x0 + s.x1) / 2, mz = (s.z0 + s.z1) / 2;
            for (int dy = 1; dy <= kWallH; ++dy) put(mx, dy, mz, BlockRegistry::StoneBrick);
            put(mx + 1, 1, mz,     BlockRegistry::StoneBrickStairs, 0);
            put(mx,     1, mz + 1, BlockRegistry::StoneBrickSlab);
            put(mx,     2, mz + 1, BlockRegistry::StoneBrickStairs, 2);
            put(mx - 1, 2, mz,     BlockRegistry::StoneBrickSlab);
            put(mx - 1, 3, mz,     BlockRegistry::StoneBrickStairs, 1);
            put(mx,     3, mz - 1, BlockRegistry::StoneBrickSlab);
            put(mx,     4, mz - 1, BlockRegistry::StoneBrickStairs, 3);
            put(mx + 1, 4, mz - 1, BlockRegistry::StoneBrickSlab);
            put(mx + 1, 5, mz,     BlockRegistry::StoneBrickSlab);
            put(mx - 1, 5, mz + 1, BlockRegistry::StoneBrickSlab);
            put(mx - 2, 1, mz - 2, BlockRegistry::Torch);
            put(mx + 2, 1, mz + 2, BlockRegistry::Torch);
        };

        // FiveWay（五向交叉）：室内沿台阶环 + 中心十字 = 恰 36 石砖台阶 + 中央 1 火把（wiki SH5C
        //   36 台阶 + 1 火把精确复刻；壳体石砖 ~560 ∈ wiki 536-572 带内）。MC 第五向「上」出口由
        //   北向直梯段（C3）承担（登记近似）。
        auto stampFiveWay = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            for (int dx = ix0; dx <= ix1; ++dx)
                for (int dz = iz0; dz <= iz1; ++dz)
                    if (dx == ix0 || dx == ix1 || dz == iz0 || dz == iz1)
                        put(dx, 1, dz, BlockRegistry::StoneBrickSlab);
            const int mx = (ix0 + ix1) / 2, mz = (iz0 + iz1) / 2;
            put(mx + 1, 1, mz, BlockRegistry::StoneBrickSlab);
            put(mx - 1, 1, mz, BlockRegistry::StoneBrickSlab);
            put(mx, 1, mz + 1, BlockRegistry::StoneBrickSlab);
            put(mx, 1, mz - 1, BlockRegistry::StoneBrickSlab);
            put(mx, 1, mz, BlockRegistry::Torch);
        };

        // StairsStraight（直梯段）：贴墙圆石楼梯 3 级坡 + 台下实心填充（wiki SHSSD 275 砖 + 18 圆石
        //   楼梯——壳体块数级达成；楼梯 18→3、平层化（单层引擎不改层高，坡为登坡装饰）登记近似）。
        auto stampStairsStraight = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            put(ix0, 1, iz0,     BlockRegistry::CobbleStairs, 2);
            put(ix0, 2, iz0 + 1, BlockRegistry::CobbleStairs, 2);
            put(ix0, 1, iz0 + 1, BlockRegistry::StoneBrick);      // 台下填充（防悬浮）
            put(ix0, 3, iz0 + 2, BlockRegistry::CobbleStairs, 2);
            put(ix0, 1, iz0 + 2, BlockRegistry::StoneBrick);
            put(ix0, 2, iz0 + 2, BlockRegistry::StoneBrick);
        };

        // PortalRoom（传送门房；**固定局部坐标**，与 t713 旧房体同位 → B5 读档反推 / t1000 bounds 零
        //   回归）：石砖高台（13×6，dy1..3，台面 dy=4 可站立）+ 3×3 岩浆盆（dy=3，激活门面正下方
        //   一格）+ 12 框架环（标准 ±2 方形环 = endPortalRingComplete 同几何，~10% 预嵌眼）+ 3 级
        //   石砖楼梯（台下实心）+ 环沟岩浆河 + 南栏 + 银鱼刷怪笼（wiki SHPR 704 砖 + 15 岩浆 + 8 楼梯
        //   + 12 框架 + 1 笼的块数级复刻；岩浆盆 9 + 河沟 72（12 列×6 行；review0904 勘误旧注释 ~108）≥ 15 ✓）+ **39 铁栏杆格栅**（wiki 39
        //   精确总数，三段分配：入口格栅 7 + 岩浆河护栏 24 + 高台护栏 8——MC 分配口径不同总和同，登记）。
        auto stampPortalRoom = [&]() {
            // a) 高台（x -6..6 × z -21..-16，dy1..3 实心）。
            for (int dx = -6; dx <= 6; ++dx)
                for (int dz = -21; dz <= -16; ++dz)
                    for (int dy = 1; dy <= 3; ++dy)
                        put(dx, dy, dz, BlockRegistry::StoneBrick);
            // d) 3 级楼梯（x -1..1，dz -15..-13 逐级 dy 3..1，state=2 朝 +Z 开 → 自南向北登台）+ 台下填充。
            for (int dx = -1; dx <= 1; ++dx) {
                put(dx, 3, -15, BlockRegistry::StoneBrickStairs, 2);
                put(dx, 2, -14, BlockRegistry::StoneBrickStairs, 2);
                put(dx, 1, -13, BlockRegistry::StoneBrickStairs, 2);
                put(dx, 1, -15, BlockRegistry::StoneBrick);
                put(dx, 2, -15, BlockRegistry::StoneBrick);
                put(dx, 1, -14, BlockRegistry::StoneBrick);
            }
            // e) 环沟岩浆河（dy=0，高台外围；三面围死稳态不流）+ 南侧石砖栏。
            for (int dx = -12; dx <= 12; ++dx)
                for (int dz = -21; dz <= -16; ++dz) {
                    if (dx >= -6 && dx <= 6) continue;              // 高台 footprint（dy=0 已是地板石砖）
                    if (dz >= -15) continue;                        // 仅 dz -21..-16 环沟带
                    put(dx, 0, dz, BlockRegistry::Lava);
                }
            for (int dx = -12; dx <= 12; ++dx) {
                if (dx >= -1 && dx <= 1) continue;                  // 楼梯 footprint
                if (dx >= -6 && dx <= 6) continue;                  // 高台南沿
                put(dx, 0, -15, BlockRegistry::StoneBrick);
            }
            // b) 3×3 岩浆盆（dy=3 高台内挖槽灌岩浆；盆底 dy=2 实心托底）。
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -19; dz <= -17; ++dz)
                    put(dx, 3, dz, BlockRegistry::Lava);
            // c) 12 框架环（中心 (0,4,-18)，四边各 3 不含四角）+ ~10% 预嵌眼（确定性 hash）。
            for (int pdx = -2; pdx <= 2; ++pdx)
                for (int pdz = -20; pdz <= -16; ++pdz) {
                    const bool onRingExact = (pdx == -2 || pdx == 2) ? (pdz >= -19 && pdz <= -17)
                                                : (pdz == -20 || pdz == -16) && (pdx >= -1 && pdx <= 1);
                    if (!onRingExact) continue;
                    const quint32 eh = hashVoxel(strongSeed ^ 0x664u, cx + pdx, cy + 4, cz + pdz);
                    const quint8 st = (eh % 100u) < 10u ? BlockRegistry::EndPortalStateActiveFlag : 0u;
                    put(pdx, 4, pdz, BlockRegistry::EndPortal, st);
                }
            // f) 银鱼刷怪笼（环中心正上方；t786 显式银鱼 type → tickSpawners 刷 Silverfish）。
            put(0, 5, -18, BlockRegistry::Spawner, BlockRegistry::SpawnerStateSilverfish);
            // g) 39 铁栏杆格栅（wiki SHPR 39 精确总数）。
            //    g1) 入口格栅（z=-10 门洞面 3 宽 × 3 高 = 9 − 东侧 1×2 通行口 = 7；wiki「入口恒格栅」，
            //        通行口留 东列 dy1..2 供玩家侧身入内——门洞表只掏 x=0 单列、格栅柱又盖回 x=0 门洞面
            //        → 通行口须在此**显式掏穿** C3/PR 双墙带（z=-10/-11）接房内，否则洞位留 Phase A 变体砖
            //        入口堵死（t759 (d) 腿钉通行口 (1,dy1..2,-10) Air）。
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = 1; dy <= 3; ++dy) {
                    if (dx == 1 && dy <= 2) {                       // 通行口：掏穿双墙带（z -10/-11）
                        put(dx, dy, -10, BlockRegistry::Air);
                        put(dx, dy, -11, BlockRegistry::Air);
                        continue;
                    }
                    put(dx, dy, -10, BlockRegistry::IronBars);
                }
            //    g2) 岩浆河护栏（南栏 dy0 之上 dy1..2，x ±7..±12 两段 12 格 × 2 = 24；防坠河 + 栅窗观感）。
            for (int dx = -12; dx <= 12; ++dx) {
                if (dx >= -6 && dx <= 6) continue;                  // 高台 footprint
                if (dx >= -1 && dx <= 1) continue;                  // 楼梯 footprint
                put(dx, 1, -15, BlockRegistry::IronBars);
                put(dx, 2, -15, BlockRegistry::IronBars);
            }
            //    g3) 高台边缘护栏（台面 dy4 上 1 高，x=±6 × z -20..-17 = 8；环区中带 x±2 不挡登台动线）。
            for (int dz = -20; dz <= -17; ++dz) {
                put(-6, 4, dz, BlockRegistry::IronBars);
                put(6, 4, dz, BlockRegistry::IronBars);
            }
        };

        // LibraryLarge（大图书馆；wiki SHLi 大：895 砖 + 233 书架 + 166 木板 + 46 栅栏 + 14 火把 +
        //   7 梯 + 2 箱——书架 231 / 木板 104 / 栅栏 13 / 火把 14 ✓ / 梯 7 ✓ / 箱 2 ✓，块数级忠实，
        //   木板 166→104 / 栅栏 46→13 登记近似）：贴墙书架带 dy1..3 + 阁楼木板层 dy4（梯洞）+ 阁楼
        //   书架带 dy5..6 + 中央书架岛 3×9×3 + 栅栏护栏 + 贴北墙梯柱 dy1..7 + 阁楼双箱 + 双层 14 火把。
        auto stampLibraryLarge = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            for (int dz = iz0; dz <= iz1; ++dz)
                for (int dy = 1; dy <= 3; ++dy) {
                    put(ix0, dy, dz, BlockRegistry::Bookshelf);         // 西墙书架带（90）
                    put(ix1, dy, dz, BlockRegistry::Bookshelf);         // 东墙书架带
                    if (dy <= 2) {                                       // 阁楼书架带 dy5..6（60）
                        put(ix0 + 1, dy + 4, dz, BlockRegistry::Bookshelf);
                        put(ix1 - 1, dy + 4, dz, BlockRegistry::Bookshelf);
                    }
                }
            const int mx = (ix0 + ix1) / 2, mz = (iz0 + iz1) / 2;
            for (int dx = ix0 + 1; dx <= ix1 - 1; ++dx)                  // 阁楼木板层 dy4（梯洞外满铺 104）
                for (int dz = iz0; dz <= iz1; ++dz)
                    if (!(dx == mx && dz == iz1)) put(dx, 4, dz, BlockRegistry::Planks);
            for (int dx = mx - 1; dx <= mx + 1; ++dx)                    // 中央书架岛 3×9×3（81）
                for (int dz = mz - 4; dz <= mz + 4; ++dz)
                    for (int dy = 1; dy <= 3; ++dy)
                        put(dx, dy, dz, BlockRegistry::Bookshelf);
            for (int dy = 1; dy <= kWallH - 1; ++dy)                     // 梯柱 dy1..7（贴北墙 +Z → state 2）
                put(mx, dy, iz1, BlockRegistry::Ladder, 2);              //   梯洞格 dy4 由本柱占据（爬穿阁楼）
            for (int dx = ix0 + 1; dx <= ix1 - 1; ++dx) {                // 栅栏护栏（阁楼南北沿，梯口让位）
                put(dx, 5, iz0, BlockRegistry::WoodFence);
                if (dx != mx) put(dx, 5, iz1, BlockRegistry::WoodFence);
            }
            put(ix0 + 2, 5, iz1 - 1, BlockRegistry::Chest, BlockRegistry::ChestStateStrongholdFlag); // 阁楼双箱
            put(ix1 - 2, 5, iz0 + 1, BlockRegistry::Chest, BlockRegistry::ChestStateStrongholdFlag);
            for (int dz = iz0 + 1; dz <= iz1 - 1; dz += 4) {             // 下层 8 火把
                put(ix0 + 2, 1, dz, BlockRegistry::Torch);
                put(ix1 - 2, 1, dz, BlockRegistry::Torch);
            }
            for (int dz = iz0 + 1; dz <= iz1 - 1; dz += 6) {             // 阁楼 6 火把（岛肩位）
                put(mx - 1, 5, dz, BlockRegistry::Torch);
                put(mx + 1, 5, dz, BlockRegistry::Torch);
            }
        };

        // LibrarySmall（小图书馆；wiki SHLi 小：627 砖 + 161 书架 + 32 木板 + 8 火把 + 1 箱——环墙
        //   书架带 + 内环矮书架（块数级 ~86 登记）、4 火把（8→4 登记）、1 箱、无梯/栅栏/阁楼（单层）；
        //   蛛网 7% 口径两型图书馆共用）。
        auto stampLibrarySmall = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            for (int dx = ix0; dx <= ix1; ++dx)
                for (int dz = iz0; dz <= iz1; ++dz)
                    if (dx == ix0 || dx == ix1 || dz == iz0 || dz == iz1)
                        for (int dy = 1; dy <= 3; ++dy)
                            put(dx, dy, dz, BlockRegistry::Bookshelf);   // 环墙书架带
            for (int dx = ix0 + 2; dx <= ix1 - 2; ++dx)                  // 内环书架（矮两阶）
                for (int dz = iz0 + 2; dz <= iz1 - 2; ++dz)
                    if (dx == ix0 + 2 || dx == ix1 - 2 || dz == iz0 + 2 || dz == iz1 - 2)
                        for (int dy = 1; dy <= 2; ++dy)
                            put(dx, dy, dz, BlockRegistry::Bookshelf);
            put(ix0 + 2, 1, iz0 + 1, BlockRegistry::Torch);
            put(ix1 - 2, 1, iz0 + 1, BlockRegistry::Torch);
            put(ix0 + 2, 1, iz1 - 1, BlockRegistry::Torch);
            put(ix1 - 2, 1, iz1 - 1, BlockRegistry::Torch);
            put((ix0 + ix1) / 2, 1, iz0 + 1, BlockRegistry::Chest, BlockRegistry::ChestStateStrongholdFlag);
        };

        // PrisonHall（监狱厅；wiki SHPH：300 砖 + 20 铁栏杆 + 2 铁门——栏杆 / 铁门精确复刻：三间北向
        //   牢房，两间栅门房（双栏柱 6 + 顶栏 1 = 各 7 栏 + 1 铁门）+ 一间全栅房（6 栏）= 20 栏 + 2 铁门
        //   （仅红石可开 = MC 铁门口径 ✓）；砖数壳体块数级）。
        auto stampPrisonHall = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            const int fz = iz0 + 2;                                      // 牢房栅面 z（北墙内侧两格）
            for (int c = 0; c < 3; ++c) {
                const int cx0 = ix0 + c * 3;                             // 三间 3 宽牢房（9 宽内室恰分）
                if (c < 2) {                                             // 栅门房
                    for (int dy = 1; dy <= 3; ++dy) {
                        put(cx0, dy, fz, BlockRegistry::IronBars);
                        put(cx0 + 2, dy, fz, BlockRegistry::IronBars);
                    }
                    put(cx0 + 1, 1, fz, BlockRegistry::IronDoor, 2);     // 下格（朝 +Z 开面南）
                    put(cx0 + 1, 2, fz, BlockRegistry::IronDoor, 10);    // 上格（bit3 上位 | 朝向 2）
                    put(cx0 + 1, 3, fz, BlockRegistry::IronBars);
                } else {                                                 // 全栅房
                    for (int dx = cx0; dx <= cx0 + 2; ++dx)
                        for (int dy = 1; dy <= 2; ++dy)
                            put(dx, dy, fz, BlockRegistry::IronBars);
                }
            }
            put(ix0 + 1, 1, iz1 - 1, BlockRegistry::Torch);
            put(ix1 - 1, 1, iz1 - 1, BlockRegistry::Torch);
        };

        // StoreRoom（储藏室；wiki Store 变体：51 圆石 + 40 木板 + 3 梯 + 1 箱 + 1 火把——木板/梯/箱/火把
        //   精确，圆石 39（3×3 芯 dy1..3 + 四角柱，51→39 登记））：圆石芯 + 木板阁楼 dy4（梯洞）+
        //   贴芯梯柱 + 上层箱。
        auto stampStoreRoom = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            const int mx = (ix0 + ix1) / 2, mz = (iz0 + iz1) / 2;
            for (int dx = mx - 1; dx <= mx + 1; ++dx)                    // 圆石芯 3×3×3（27）
                for (int dz = mz - 1; dz <= mz + 1; ++dz)
                    for (int dy = 1; dy <= 3; ++dy)
                        put(dx, dy, dz, BlockRegistry::Cobble);
            for (int d = 1; d <= 3; ++d) {                               // 四角柱 1×1×3（+12 = 39 圆石）
                put(mx - 2, d, mz - 2, BlockRegistry::Cobble);
                put(mx + 2, d, mz - 2, BlockRegistry::Cobble);
                put(mx - 2, d, mz + 2, BlockRegistry::Cobble);
                put(mx + 2, d, mz + 2, BlockRegistry::Cobble);
            }
            for (int dx = ix0; dx <= ix1 - 1; ++dx)                      // 木板阁楼 dy4（梯洞外满铺）
                for (int dz = iz0; dz <= iz1; ++dz)
                    if (!(dx == mx + 1 && dz == mz)) put(dx, 4, dz, BlockRegistry::Planks);
            for (int dy = 1; dy <= 4; ++dy)                              // 梯柱 dy1..4（贴芯 -X 面 → state 1；
                put(mx + 1, dy, mz, BlockRegistry::Ladder, 1);           //   dy4 = 阁楼洞格，爬穿阁楼）
            put(mx - 2, 5, mz, BlockRegistry::Chest, BlockRegistry::ChestStateStrongholdFlag); // 上层箱
            put(mx - 1, 1, iz0 + 1, BlockRegistry::Torch);
        };

        // RoomCrossEmpty（空房交叉）：中央 1 火把（wiki SHRC 406 砖壳体块数级）。
        auto stampRoomEmpty = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            put((ix0 + ix1) / 2, 1, (iz0 + iz1) / 2, BlockRegistry::Torch);
        };

        // RoomCrossFountain（喷泉房）：3×3 池（8 砖沿 + 1 水心）+ 四角柱 4 砖（wiki +19 砖 +1 水 →
        //   12 砖 + 1 水登记）；水被砖沿围死 + 地板托底 → 稳态不蔓延（同传送门房岩浆盆稳态口径）。
        auto stampRoomFountain = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            const int mx = (ix0 + ix1) / 2, mz = (iz0 + iz1) / 2;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    put(mx + dx, 1, mz + dz, (dx || dz) ? BlockRegistry::StoneBrick : BlockRegistry::Water);
            for (int dx = -1; dx <= 1; dx += 2)
                for (int dz = -1; dz <= 1; dz += 2)
                    put(mx + dx, 2, mz + dz, BlockRegistry::StoneBrick);
        };

        // RoomCrossPillar（柱房）：中央 1×1×3 砖柱 + 顶棚 8 台阶环 + 柱脚 4 火把（wiki +3 砖 + 8 台阶 +
        //   4 火把精确复刻）。
        auto stampRoomPillar = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            const int mx = (ix0 + ix1) / 2, mz = (iz0 + iz1) / 2;
            for (int dy = 1; dy <= 3; ++dy) put(mx, dy, mz, BlockRegistry::StoneBrick);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (dx || dz) put(mx + dx, kWallH, mz + dz, BlockRegistry::StoneBrickSlab);
            for (int dx = -1; dx <= 1; dx += 2)
                for (int dz = -1; dz <= 1; dz += 2)
                    put(mx + dx, 1, mz + dz, BlockRegistry::Torch);
        };

        // Corridor（直走廊）：50% hash 侧龛（1×1×2 壁龛 + 龛内火把；近似 MC 50% 侧出口——格架无侧
        //   slot，出口退化为壁龛，登记）。
        auto stampCorridor = [&](const ShSlot &s, int sub, int idx) {
            if ((hashVoxel(sub ^ 0xC0Au, idx, 11, 0) % 100u) >= 50u) return;
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            const bool negSide = (hashVoxel(sub ^ 0xC0Bu, idx, 12, 0) % 2u) == 0u;
            int nx, nz;
            if (ix1 - ix0 <= iz1 - iz0) {                                // 南北向走廊 → 东西壁龛
                nx = negSide ? ix0 - 1 : ix1 + 1; nz = (iz0 + iz1) / 2;
            } else {                                                     // 东西向走廊 → 南北壁龛
                nx = (ix0 + ix1) / 2; nz = negSide ? iz0 - 1 : iz1 + 1;
            }
            put(nx, 1, nz, BlockRegistry::Air);
            put(nx, 2, nz, BlockRegistry::Air);
            put(nx, 1, nz, BlockRegistry::Torch);
        };

        // ChestCorridor（箱走廊）：角落 1 箱（要塞战利品 flag；wiki SHCC 1 箱 ✓）+ 对角 1 火把。
        auto stampChestCorridor = [&](const ShSlot &s) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            put(ix0, 1, iz0, BlockRegistry::Chest, BlockRegistry::ChestStateStrongholdFlag);
            put(ix1, 1, iz1, BlockRegistry::Torch);
        };

        // FillerCorridor（填充走廊）：1-2 圆石塌方 rubble（wiki SHFC「半塌走廊」口径近似）。
        auto stampFillerCorridor = [&](const ShSlot &s, int sub, int idx) {
            int ix0, iz0, ix1, iz1; iRect(s, ix0, iz0, ix1, iz1);
            const int mx = (ix0 + ix1) / 2, mz = (iz0 + iz1) / 2;
            put(mx, 1, mz, (hashVoxel(sub ^ 0xF11Au, idx, 13, 0) % 2u) ? BlockRegistry::Cobble
                                                                       : BlockRegistry::StoneBrick);
            if (hashVoxel(sub ^ 0xF11Bu, idx, 14, 0) % 2u)
                put(ix0 + (ix1 - mx), 1, mz, BlockRegistry::Cobble);
        };

        // 盖章分派（portal 仅在 portalStamped 时落地——「保证生成」语义见 kPortalGrateGuaranteed）。
        const int subSeed = strongSeed + attempt * 7919;
        for (int i = 0; i < kSlotCount; ++i) {
            const ShSlot &s = slotTable[i];
            switch (kind[i]) {
            case PieceStartSpiral:    stampStartSpiral(s); break;
            case PieceFiveWay:        stampFiveWay(s); break;
            case PieceStairsStraight: stampStairsStraight(s); break;
            case PieceLibraryLarge:   stampLibraryLarge(s); break;
            case PieceLibrarySmall:   stampLibrarySmall(s); break;
            case PiecePrisonHall:     stampPrisonHall(s); break;
            case PieceStoreRoom:      stampStoreRoom(s); break;
            case PieceRoomEmpty:      stampRoomEmpty(s); break;
            case PieceRoomFountain:   stampRoomFountain(s); break;
            case PieceRoomPillar:     stampRoomPillar(s); break;
            case PieceCorridor:       stampCorridor(s, subSeed, i); break;
            case PieceChestCorridor:  stampChestCorridor(s); break;
            case PieceFillerCorridor: stampFillerCorridor(s, subSeed, i); break;
            case PiecePortalRoom:     if (portalStamped) stampPortalRoom(); break;
            }
        }

        // 蛛网 7%（wiki 图书馆口径精确复刻：图书馆室内 dy1..4 空气格逐格 hash，仅空气格替换；
        //   期望 ~20 网 vs wiki 均值 ~29 同量级）。
        for (int i = 0; i < kSlotCount; ++i) {
            if (kind[i] != PieceLibraryLarge && kind[i] != PieceLibrarySmall) continue;
            int ix0, iz0, ix1, iz1; iRect(slotTable[i], ix0, iz0, ix1, iz1);
            for (int dx = ix0; dx <= ix1; ++dx)
                for (int dz = iz0; dz <= iz1; ++dz)
                    for (int dy = 1; dy <= 4; ++dy) {
                        if (m_chunks.blockAt(cx + dx, cy + dy, cz + dz) != BlockRegistry::Air) continue;
                        if ((hashVoxel(strongSeed ^ 0x3EBu, cx + dx, cy + dy, cz + dz) % 100u) < 7u)
                            put(dx, dy, dz, BlockRegistry::Cobweb);
                    }
        }

    };

    for (int bx = kStrongholdGrid / 2; bx < m_width; bx += kStrongholdGrid) {
        for (int bz = kStrongholdGrid / 2; bz < m_depth; bz += kStrongholdGrid) {
            const quint32 r = hashColumn(strongSeed, bx, bz);
            if ((r % 100u) >= kStrongholdPct) continue; // 概率筛选
            const int span = kStrongholdGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
                continue; // 留 margin 边界（建筑半径 ≤ margin 不越界）
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠要塞（避免与海水柱冲突）
            const int h = std::min(heightAt(cx, cz), m_height - 1);
            // 要塞 y 范围：基岩之上 ~ kStrongholdMaxY 之下；上方至少留 kSurfaceGap 格石顶（不破地表、封闭黑暗）。
            //   t759 注：yHi 随 kWallH(5→8) 同步下移 3 → 同 seed 的要塞地板 y 与旧版不同（内部最高 Air 层仍
            //   ≤ kStrongholdMaxY，语义不变）。worldgen 常量改动仅新生成世界生效，旧存档体素不回填（B5 读档
            //   反推自适应旧几何）。
            const int yLo = kBedrockTop + 2;
            const int yHi = std::min(kStrongholdMaxY - kWallH, h - kSurfaceGap - kWallH);
            if (yHi <= yLo) continue; // 此列地下空间不足（极低洼 / 山顶浅层）→ 跳过
            const int yRange = yHi - yLo + 1;
            const int cy = yLo + int((r >> 9) & 0x1Fu) % yRange; // 地板（底面）y

            // t564：只收集候选（不立即放置）—— 全图至多一座要塞 / 一个末地传送门（用户「同一区块/出生点
            //   附近生成好几个末地传送门，应至多一个」）。放置延迟到循环后选「距出生点（世界中心）最近」的
            //   一座执行（见下方 placeAt + bestIdx 段）。
            candidates.push_back({cx, cy, cz});
        }
    }

    // t564：从候选里选距世界中心（=出生点，t276 大世界居中）最近的一座放置 → **至多一个末地传送门**。
    //   旧版每 40 格网格 55% 命中 → 160×160 世界 ~9 座要塞（每座含 3×3 传送门），出生点附近多座 → 用户
    //   「生成好几个末地传送门」。改为全图至多一座、且落在出生点附近（玩家探索即可寻，机制等价 MC 1.0
    //   要塞「环出生点分布」的单座近点，本工程小世界取一座）。
    int bestIdx = -1;
    double bestDistSq = 1e18;
    const double centerX = double(m_width) * 0.5, centerZ = double(m_depth) * 0.5;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const double dx = double(candidates[i][0]) - centerX;
        const double dz = double(candidates[i][2]) - centerZ;
        const double d = dx * dx + dz * dz;
        if (d < bestDistSq) { bestDistSq = d; bestIdx = int(i); }
    }
    if (bestIdx >= 0) {
        placeAt(candidates[size_t(bestIdx)][0], candidates[size_t(bestIdx)][1], candidates[size_t(bestIdx)][2]);
        // t729 记录要塞末地传送门中心格（供暗渊之眼右击寻路目标；见 world.h m_strongholdPortal* 头注释）。
        //   传送门房中心 = 环中心 (x), 门面 dy=4 (y), 房内 dz 中心 -18 (z)；全图唯一。重置先于判定（无候选 → 清）。
        //   t1000：偏移引用类常量 kStrongholdPortalDy/Dz（与 insideStronghold 反解同源，防两处字面量漂移）。
        m_hasStronghold = true;
        m_strongholdPortalX = candidates[size_t(bestIdx)][0];
        m_strongholdPortalY = candidates[size_t(bestIdx)][1] + kStrongholdPortalDy;
        m_strongholdPortalZ = candidates[size_t(bestIdx)][2] + kStrongholdPortalDz;
        ++placed;
    } else {
        m_hasStronghold = false; // 无候选要塞 → 清目标（世界重建 / 新种子时防陈旧坐标）
    }
    qInfo() << "worldgen: strongholds =" << placed; // 同 seed → 同计数（确定性核对）
}

// t309 地表小湖泊（见 world.h 头注释）。机制等价 MC 1.0 地表小湖泊 / 池塘：地表局部低洼处的浅水洼。
//   确定性散布（hashColumn + seed 偏移，PLAN §2-K）：网格采样 + 概率筛选 + 抖动 → 选半径 2..3，**局部低洼**判定
//   （disc 内 heightAt ∈ {surfaceY-1,surfaceY,surfaceY+1}（轻微起伏）、湖岸外圈 heightAt ≥ surfaceY（中心是相对低点））
//   → carve 一个**下凹**浅水盘：disc 内清除 surfaceY..localH 的方块（开顶 → 湖露天），surfaceY-1/surfaceY-2 两层置水源。
//   湖岸（外圈 ≥ surfaceY）在水面（surfaceY-1）处为实体土 → 湖水水平邻居无 air → 不溢漏 / 不蔓延（稳态）；湖面低于
//   周围地表 1 格 + 开顶 → 部分露出（肉眼可见）。相比「整片严格等高」更易达成（局部低洼比大块平坦常见得多）。
//   t340 形态丰富化：(a) 湖岸改用 fbm 调制每格有效半径 → 弯曲湖岸 / 半岛（非正圆 / 非垂直圆柱）；(b) 约 half 湖
//   （per-lake hash 位）在湖床之下藏一个空心穹顶气室（保留 1 层石顶 → 水源不漏；穹顶被 stone 封闭 → 稳定 air 气室），
//   形成「地表浅湖 + 下伏空腔」与「纯地表浅湖」两种形态混排。仅 plains/forest；避开沙滩 / 水下 / 海平面附近（湖独立
//   于海）。经 m_chunks.setBlock 直写；fbm / hashColumn 纯函数 → 同 seed 同湖形（PLAN §2-K）。
void World::placeSurfaceLakes()
{
    // 湖泊密度标定（密度 ∝ pct/grid²）。t375 把 grid 18→12、pct 25→50（密度约 4.5×）后实测「湖太多」，
    //   t427 仅下调命中概率（pct 50→20；grid 保留 12 以维持细网格的空间均匀分布）→ 密度约 0.14，介于
    //   t375 前「太少」(0.077) 与 t375 后「太多」(0.347) 之间 = 偶发 / 适度。不动「局部低洼」几何判定
    //   （湖盆有效性须保留，否则斜坡上的水会流空 → 坏湖）。同 seed 仍确定。
    constexpr int kLakeGrid     = 12;      // 候选网格间距（保留 t375 细采样；湖密度靠 pct 调）
    constexpr unsigned kLakePct = 20u;     // 候选命中概率（t427：50→20 调回适度密度）
    constexpr int kLakeDepth    = 2;       // 湖深（水源层数：surfaceY-1 .. surfaceY-2）

    int placed = 0;
    int caverns = 0; // t340：湖下空心穹顶气室计数（形态混排核对）
    const int lakeSeed = m_seed + 7309; // 湖泊哈希偏移（与其它 worldgen hashColumn 解耦）
    for (int bx = kLakeGrid / 2; bx < m_width; bx += kLakeGrid) {
        for (int bz = kLakeGrid / 2; bz < m_depth; bz += kLakeGrid) {
            const quint32 r = hashColumn(lakeSeed, bx, bz);
            if ((r % 100u) >= kLakePct) continue; // 概率筛选
            const int span = kLakeGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int x = bx + jx, z = bz + jz;
            const int rad = 2 + int((r >> 9) & 1u); // 半径 2..3
            const bool wantCavern = (r >> 17) & 1u;  // t340：约半数湖下藏空心穹顶气室（形态混排：地表浅湖 / 湖+下伏空腔）
            // 留 rad+1 边界（局部低洼判定扫 disc + 湖岸外圈，须全在界内）。
            if (x - (rad + 1) < 0 || z - (rad + 1) < 0
                || x + (rad + 1) >= m_width || z + (rad + 1) >= m_depth) continue;
            if (seaColumnHeight(x, z) >= 0) continue; // t338：海域不叠地表湖（海独立于湖；heightAt 纯自然高度会误判海列为可挖平坦地 → 误挖海水柱）
            const Biome bio = biomeAt(x, z);
            if (bio != Biome::Plains && bio != Biome::Forest) continue; // 仅 plains/forest
            const int surfaceY = std::min(heightAt(x, z), m_height - 1);
            // 避开海平面附近（湖独立于海、不溢入海）：湖面须明显高于海平面。
            if (surfaceY <= kWaterLevel + 3) continue;

            // 局部低洼判定（chebyshev 距离 d）：
            //   disc（d ≤ rad）heightAt ∈ [surfaceY-1, surfaceY+1]（轻微起伏；含中心）；
            //   湖岸外圈（d == rad+1）heightAt ≥ surfaceY（中心是相对低点 → 湖岸在水面 surfaceY-1 处为实体土，围成不溢漏湖盆）。
            //   此条件比「整片严格等高」宽得多（局部低洼在 amp=2 平原常见），保证湖确有产出而非全被筛掉。
            bool ok = true;
            for (int dx = -(rad + 1); dx <= (rad + 1) && ok; ++dx) {
                for (int dz = -(rad + 1); dz <= (rad + 1); ++dz) {
                    const int localH = std::min(heightAt(x + dx, z + dz), m_height - 1);
                    const int adx = dx < 0 ? -dx : dx;
                    const int adz = dz < 0 ? -dz : dz;
                    const int d = adx > adz ? adx : adz; // chebyshev 距离
                    if (d <= rad) {
                        if (localH < surfaceY - 1 || localH > surfaceY + 1) { ok = false; break; } // disc 轻微起伏
                    } else {
                        if (localH < surfaceY) { ok = false; break; } // 湖岸须 ≥ surfaceY（围成湖盆）
                    }
                }
            }
            if (!ok) continue;

            // carve 下凹浅水盘（t340：不规则湖岸 + 部分湖下藏空心穹顶）。水面 = surfaceY-1（低于周围地表 1 格 → 露天可见的凹陷湖）。
            const int waterSurface = surfaceY - 1;
            for (int dx = -rad; dx <= rad; ++dx) {
                for (int dz = -rad; dz <= rad; ++dz) {
                    const int px = x + dx, pz = z + dz;
                    // t340 不规则湖岸：fbm（世界坐标采样 → 确定性、与地形 fbm 解耦）调制每格有效半径（rad ± ~0.9）
                    //   → 弯曲湖岸 / 半岛，非正圆。被剔除的 disc 格保留为草地半岛：其 heightAt ≥ surfaceY-1（局部低洼
                    //   判定保证）→ 在水面 surfaceY-1 处为实体 → 水平邻居无 air → 不溢漏（稳态）。loop 格全在
                    //   chebyshev ≤ rad（≤ 测试区 rad+1）→ 不越低洼判定范围。
                    const double shore = fbm((px + m_seed) * 0.33 + 31.7, (pz + m_seed) * 0.33 + 19.3); // [-1,1]
                    const double effR = double(rad) + 0.9 * shore;
                    if (double(dx * dx + dz * dz) > effR * effR) continue; // 半岛 / 湖岸外（保留为草地）
                    const int localH = std::min(heightAt(px, pz), m_height - 1);
                    // 开顶：清除 surfaceY..localH 的方块（移除草地「盖」→ 湖露天；localH<surfaceY 时此循环不执行，该列本就低）。
                    for (int y = surfaceY; y <= localH; ++y) {
                        const quint8 b = m_chunks.blockAt(px, y, pz);
                        if (b == BlockRegistry::Bedrock) continue; // 不动基岩
                        m_chunks.setBlock(px, y, pz, BlockRegistry::Air);
                    }
                    // 水源 2 层（surfaceY-1 .. surfaceY-2），替换草 / 土（不动基岩）。底部（surfaceY-3）实体土托住水源。
                    for (int ay = 0; ay < kLakeDepth; ++ay) {
                        const int yy = waterSurface - ay;
                        if (yy < 0) break;
                        const quint8 b = m_chunks.blockAt(px, yy, pz);
                        if (b == BlockRegistry::Bedrock) continue; // 不动基岩
                        m_chunks.setBlock(px, yy, pz, BlockRegistry::Water);
                    }
                }
            }

            // t340 形态混排：约半数湖在湖床之下藏一个空心穹顶气室（spec「surface 湖 + 下伏 hollow 穹顶」）。
            //   水源不能直接悬于 air 之上（tickWaterFlow 下落 pass 会把水泄入下方 air → 淹没气室），故保留
            //   surfaceY-3 一层石顶：其上水源（surfaceY-2）落在实体上不漏；穹顶 air 自 surfaceY-4 起。穹顶被
            //   周围 stone 包围（水平内缩 rad-1 → ≥1 石壁；垂直不触基岩）→ 封闭气室，tickWaterFlow 不动 air → 稳态。
            //   穹顶 = 自顶向下逐层半径递减的圆盘（顶层最宽、底层收尖）→ 穹形 / 拱顶，非垂直竖井。
            if (wantCavern) {
                constexpr int kBedrockTop = 4; // 不动基岩（同 carveCaves / placeUndergroundWaterPools）
                const int ceilY = surfaceY - 3;       // 石顶（保留实体；其上 surfaceY-2 水源落于此）
                const int rxR = rad - 1;              // 水平最大半径（≥1；内缩 → 留 ≥1 石壁）
                const int domeH = rxR + 1;            // 穹顶高度（顶层最宽、逐层 -1 → 穹形）
                const int bottomY = ceilY - domeH;    // 穹顶最低 air 层的下一格（须高于基岩层）
                if (rxR >= 1 && bottomY >= kBedrockTop + 1) {
                    for (int ly = 0; ly < domeH; ++ly) {
                        const int yy = ceilY - 1 - ly; // 自石顶下第一格起向下挖 air（surfaceY-4, surfaceY-5, ...）
                        const int lr = rxR - ly;       // 顶层 lr=rxR，逐层 -1 → 收尖穹顶
                        if (lr < 0) break;
                        const int lr2 = lr * lr;
                        for (int dx = -lr; dx <= lr; ++dx) {
                            for (int dz = -lr; dz <= lr; ++dz) {
                                if (dx * dx + dz * dz > lr2) continue; // 圆盘
                                const quint8 b = m_chunks.blockAt(x + dx, yy, z + dz);
                                if (b == BlockRegistry::Bedrock || b == BlockRegistry::Water) continue; // 不动基岩 / 水
                                m_chunks.setBlock(x + dx, yy, z + dz, BlockRegistry::Air);
                            }
                        }
                    }
                    ++caverns;
                }
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: surface lakes =" << placed << "(with cavern" << caverns << ")"; // 同 seed → 同计数（确定性核对）
}

// t151 真光场 BFS flood-fill（PLAN §2-H「方块光独立 flood-fill、时间不变」+ §M）。
//   两通道（天光 sky / 方块光 block，各 0..15）分别 flood，规则相同：种子格赋初值，向 6 邻接「非遮光格」
//   （!isSolid —— air/torch/水/异形透光；leaves/solid 等遮光）传播、每步衰减 1、取 max。结果存 chunk 第三数组。
//
//   天光种子：逐列自顶向下找首个遮光方块（isSolid）；其上方所有非遮光格 = 「见天」→ sky=15（地表 / 天空间
//     全亮；洞穴入口以下的空气靠 BFS 横向渗入并衰减，越深越暗，模拟 MC 天光 flood）。
//   方块光种子：火把格（id==Torch）→ block=14（radius14；机制等价 MC 火把光，泛光照亮 14 格半径内的洞穴 /
//     室内）。衰减 1 / 步 → 曼哈顿距离 d 处 block=14-d（d>=14 无光）。
//
//   **仅 worldgen 末调一次**（全图 48×48×64≈147k 体素 ×2 通道全图 flood 约 20-40ms）。玩家编辑（setBlock /
//     实体写入）改走增量 recomputeLightAround()（t154）：编辑格周围有界盒重 flood，单次 <1ms（典型编辑）。
//     时间不变：昼夜乘子由 QML baseColor（terrainLight(skyLight) 平滑 lerp）承担；光场只随栅格变。
//     无随机源（纯栅格派生）。跨 chunk 经 ChunkManager 路由 → 光场无缝跨越边界。
void World::recomputeLightField()
{
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return; // 极端：无尺寸不 flood

    m_chunks.clearAllLight(); // 清场：两通道从种子重新传播

    struct Cell { int x, y, z; };
    std::queue<Cell> skyQ, blockQ;

    // 6 向邻居偏移（轴对齐；光按曼哈顿距离衰减）。
    static const int dk[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    // 种子 1 — 天光：逐列自顶向下找首个遮光方块；其上方非遮光格种 sky=15。
    for (int x = 0; x < W; ++x) {
        for (int z = 0; z < D; ++z) {
            int firstOpaque = H; // 遮光顶（H = 整列无遮光 → 全列见天）
            for (int y = H - 1; y >= 0; --y) {
                // t334：遮光判据改用 lightOpacity > 0（取代旧 isSolid）—— 半砖(7) / 合活版门(15) 也「遮光」、
                //   截断本列见天 seed，下方靠 BFS 衰减渗光（半砖半减 / 合活版门满遮）。
                if (BlockRegistry::lightOpacity(m_chunks.blockAt(x, y, z), m_chunks.stateAt(x, y, z)) > 0) { firstOpaque = y; break; }
            }
            for (int y = firstOpaque + 1; y < H; ++y) {
                // 这些格必为非遮光（首个遮光之上）→ sky=15 种子；block 此时为 0（清场后）。
                m_chunks.setLight(x, y, z, 15, m_chunks.blockLightAt(x, y, z));
                skyQ.push({x, y, z});
            }
        }
    }

    // 种子 2 — 方块光（发光方块）：扫所有格，lightEmission>0（火把=14 / 岩浆=15 / 燃烧熔炉=13）→ block=该值（保留天光）。
    //   t351：岩浆自发光 15，地底岩浆湖照亮封闭洞穴（MC 1.0 岩浆光 level 15）。沿用 BlockRegistry::lightEmission
    //   单一权威（火把/岩浆/未来发光方块均经此），消除「每加一个光源改一处种子」回归类。
    //   t494：调状态感知版（传 cell state）—— 燃烧中的熔炉（state bit2）发 13、熄灭熔炉发 0（普通方块不自发光）。
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            for (int z = 0; z < D; ++z) {
                const quint8 emission = BlockRegistry::lightEmission(m_chunks.blockAt(x, y, z), m_chunks.stateAt(x, y, z));
                if (emission > 0) {
                    m_chunks.setLight(x, y, z, m_chunks.skyLightAt(x, y, z), emission);
                    blockQ.push({x, y, z});
                }
            }

    // BFS 天光传播：从种子向邻格衰减 max(1, lightOpacity)、取 max（t334：取代旧 isSolid 二值「遮光格不传」——
    //   半砖半减 / 合活版门满遮 / 实体满遮；透明格仍衰减 1 = 旧行为）。
    while (!skyQ.empty()) {
        const Cell c = skyQ.front(); skyQ.pop();
        const quint8 cur = m_chunks.skyLightAt(c.x, c.y, c.z);
        if (cur <= 1) continue; // 衰减到 1 以下不再传播（任一邻格 prop = cur-max(1,op) ≤ 0）
        for (const auto &d : dk) {
            const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue; // 越界跳过
            const quint8 nbOp = BlockRegistry::lightOpacity(m_chunks.blockAt(nx, ny, nz), m_chunks.stateAt(nx, ny, nz));
            const int prop = int(cur) - std::max(1, int(nbOp)); // 进入邻格的衰减（实体 15 → 满 0 不传，同旧 continue）
            if (prop <= 0) continue;
            const quint8 nv = quint8(prop);
            if (nv > m_chunks.skyLightAt(nx, ny, nz)) {
                m_chunks.setLight(nx, ny, nz, nv, m_chunks.blockLightAt(nx, ny, nz)); // 更新 sky，保留 block
                skyQ.push({nx, ny, nz});
            }
        }
    }

    // BFS 方块光（火把）传播：同规则，独立通道。
    while (!blockQ.empty()) {
        const Cell c = blockQ.front(); blockQ.pop();
        const quint8 cur = m_chunks.blockLightAt(c.x, c.y, c.z);
        if (cur <= 1) continue;
        for (const auto &d : dk) {
            const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            const quint8 nbOp = BlockRegistry::lightOpacity(m_chunks.blockAt(nx, ny, nz), m_chunks.stateAt(nx, ny, nz));
            const int prop = int(cur) - std::max(1, int(nbOp));
            if (prop <= 0) continue;
            const quint8 nv = quint8(prop);
            if (nv > m_chunks.blockLightAt(nx, ny, nz)) {
                m_chunks.setLight(nx, ny, nz, m_chunks.skyLightAt(nx, ny, nz), nv); // 更新 block，保留 sky
                blockQ.push({nx, ny, nz});
            }
        }
    }
}

// t154 增量光场入口（PLAN §2-H / §M + 性能）：编辑格 (ex,ey,ez) 由 oldId→newId 后，按影响面在有界盒内重 flood，
//   替代旧「每次 setBlock 全量 recomputeLightField」（147k 体素 ×2 通道全图 BFS ≈ 20-40ms → 破/放卡顿）。
//
//   影响面判定（据 oldId/newId 的遮光性 + 是否火把）：
//   - 遮光变化（isSolid(old) != isSolid(new)）：天光的列 first-opaque 可能翻转（破/放实体方块）→ 天光须重算，
//     且翻转可能影响整列（编辑格下方所有原见天格翻暗）→ 盒覆盖**整列高**。同时方块光路径也可能被遮光变化阻断
//     → 两通道都重 flood。
//   - 仅火把增删（遮光不变，old/new 之一为 Torch）：天光全局有效不翻 → 只重 flood 方块光（火把半径盒）。
//   - 两者皆否（如门开合：id 不变、isSolid 不变、非火把）→ 光照无变化，直接 return（仍发 worldChanged 重建 mesh）。
//
//   盒半径 R = 15 = 最大光值。关键不变量：光衰减 1/步、最大 15 → 编辑格对任何格的光贡献 ≤ max(0, 15-曼哈顿距离)；
//     故**盒外格（曼哈顿 ≥16）的光值必不被本编辑影响**（无论增减、无论遮光翻转的列格 —— 翻转列格也在编辑列内、
//     其影响半径同样 ≤15）。于是盒外格作「固定边界种子」向盒内流入（衰减 1），盒内清零后从种子重传播 →
//     盒内结果与全量 re-flood 严格一致、盒外不变。典型编辑盒 ~30k 格（球）/~60k 格（圆柱），clear + flood <1ms。
// t334 旧 4 参数入口（id 变更路径）：state=0 委托。全实体方块 / air / 水 / 沙 / 树苗·原木等的遮光与 state 无关
//   （lightOpacity(*, 0) 即正确），故 id 变更路径不需 state。仅活版门开合（id 不变、state 翻转）需 state ——
//   走 6 参数重载（setBlock 5 参数版直传 oldState/state）。
void World::recomputeLightAround(int ex, int ey, int ez, quint8 oldId, quint8 newId)
{
    recomputeLightAround(ex, ey, ez, oldId, quint8(0), newId, quint8(0));
}

void World::recomputeLightAround(int ex, int ey, int ez, quint8 oldId, quint8 oldState,
                                 quint8 newId, quint8 newState)
{
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    if (ex < 0 || ey < 0 || ez < 0 || ex >= W || ey >= H || ez >= D) return;

    // t334：遮光变化判据改用 lightOpacity（取代旧 isSolid）—— 半砖放/破（0↔7）、合↔开活版门（0↔15）均能检出
    //   翻转 → 触发重 flood。id 不变且非火把且 lightOpacity 不变（如门开合：lightOpacity 恒 0）→ 光照无变化，早退。
    const bool opacityChanged = (BlockRegistry::lightOpacity(oldId, oldState) != BlockRegistry::lightOpacity(newId, newState));
    // t351：发光方块增删（火把/岩浆）触发方块光重 flood。岩浆 lightOpacity=0（solid=false）→ opacityChanged 恒 false，
    //   若不纳入本判据则岩浆流/凝（tickLavaFlow → setWaterSilent → 此处）会因「无变化」早退 → 岩浆光不更新。
    //   t494：改用状态感知版（传 oldState/newState）—— 熔炉点燃/熄火（id 不变、state bit2 翻转）须检出光变重 flood；
    //   单参版两 Furnace 均 0 → 恒 false → 燃烧熔炉光永不更新（flood 不发生）。
    const bool lightSourceChanged = (BlockRegistry::lightEmission(oldId, oldState) > 0 || BlockRegistry::lightEmission(newId, newState) > 0);
    if (!opacityChanged && !lightSourceChanged) return; // 光照无变化（如门开合：lightOpacity 恒 0、非发光方块）

    QElapsedTimer t; t.start(); // t155c：测编辑光照开销（找卡顿根因）
    constexpr int R = 15; // = 最大光值：编辑对盒外格（曼哈顿 ≥16）无影响 → 边界种子法成立（见上注释）
    const int x0 = std::max(0, ex - R), x1 = std::min(W - 1, ex + R);
    const int z0 = std::max(0, ez - R), z1 = std::min(D - 1, ez + R);
    int y0, y1;
    if (opacityChanged) {
        // t155c：y0 由 0 改 ey-R（编辑下方光照变化衰减 ≤R，更深处已暗不变 → 不必清/重 seed 全列到底）。
        //   y1 仍 H-1（天光列 first-opaque 须扫到顶重 seed，保正确）。盒缩小 → 清/重 seed 量 ↓，编辑更快。
        y0 = std::max(0, ey - R);
        y1 = H - 1;
    } else {
        // 火把半径球盒：遮光不变 → 天光不翻，只需火把半径内重 flood 方块光。
        y0 = std::max(0, ey - R);
        y1 = std::min(H - 1, ey + R);
    }
    // t383：refloodBox 内部精确标脏（仅光场确有变化的 chunk），旧版「盒内全 chunk 标脏」已移除 ——
    //   消除持续破/放的 dirty-storm（典型编辑只 1~2 chunk 光变，旧版每次标 9~16 chunk → 18~32 段/编辑重建）。
    //   编辑 chunk + 边界邻接（setBlock 已标）+ 此处光变 chunk → emit worldChanged 后仅这些重建。
    const int dirty = refloodBox(x0, y0, z0, x1, y1, z1, /*doSky=*/opacityChanged);
    // t933 perf：per-call qInfo 落盘退役（每次 recomputeLightAround 一条日志 = logHandler 锁 + QTextStream
    //   + flush 磁盘同步；级联 / 流体活跃期每秒数十次 = 主线程隐性 I/O 风暴）。聚合计数进 FrameProfiler
    //   （"refloodN"，F3 cnt 行 1s 窗报告）；仅**异常慢**（>3ms，单次编辑重 flood 不应至此）才落一条日志
    //   保住 t155c「找卡顿根因」的可观测性。
    FrameProfiler::instance()->count("lightEditN");
    if (t.elapsed() > 3)
        qInfo("vo.light: recomputeLightAround %lldus box=%dx%dx%d dirtyChunks=%d", t.elapsed(),
              x1 - x0 + 1, y1 - y0 + 1, z1 - z0 + 1, dirty); // t383：dirtyChunks = 实际光变重建的 chunk 数
}

// t154 有界盒清场 + 重 seed + 重 flood（recomputeLightAround 的实现核心）。盒外格作固定边界种子（衰减 1 流入），
//   盒内清零后从种子重传播 —— 等价于「以盒外为固定边界的盒内全量 re-flood」，结果与全局全量 re-flood 在盒内一致。
//
//   doSky=true（遮光变化）：两通道都重算。清两通道 → 重 seed 见天格(sky=15)+火把(block=14) → 边界种两通道 → flood 两通道。
//   doSky=false（仅火把增删）：天光不动。清方块光（保留天光）→ 重 seed 火把 → 边界种方块光 → flood 方块光。
//
//   边界种子：盒**表面格**的盒外邻（仅表面格有盒外邻，内部格无 —— 故只扫表面省功）。盒外邻值：y>=H → 天光 15
//   （开阔天空，与 skyLightAt OOB 同语义）；其余世界外（y<0 / x/z 越界）→ 0；盒内世界 → 其当前（未清）光值。
int World::refloodBox(int x0, int y0, int z0, int x1, int y1, int z1, bool doSky)
{
    // t933 可观测：所有光照重 flood 的单一漏斗计数（编辑增量 recomputeLightAround / 爆炸与级联批量收口 /
    //   流体延迟 flushPendingLightEdits / 叶衰批量均经此）→ F3 cnt 行 1s 窗聚合。稳态（无编辑无坍落）应
    //   恒 0；换世界后仍非 0 = 有跨世界存活的写入源（t933 跨世界泄漏排查的直接判据）。
    FrameProfiler::instance()->count("refloodN");
    const int W = m_width, D = m_depth, H = m_height;
    struct Cell { int x, y, z; };
    std::queue<Cell> skyQ, blockQ;
    static const int dk[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto inBox = [&](int x, int y, int z) {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
    };

    // t383：reflood 前快照盒内两通道光场，reflood 后逐 chunk 切片比对 → 仅「光场确有变化」的 chunk 标脏。
    //   典型破/放实体块只动编辑列天光（其余列 reflood 后与清前逐格相同 → 不标脏、不重建）。
    //   快照 ≤ ~120KB（31×64×31×2），读/比对各 <0.5ms，省下的无谓重建（数 ms~数十 ms）远大于此。
    const size_t bw = size_t(x1 - x0 + 1), bh = size_t(y1 - y0 + 1), bd = size_t(z1 - z0 + 1);
    std::vector<quint8> snapSky(bw * bh * bd), snapBlock(bw * bh * bd);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                const size_t i = size_t(x - x0) + bw * (size_t(z - z0) + bd * size_t(y - y0));
                snapSky[i] = m_chunks.skyLightAt(x, y, z);
                snapBlock[i] = m_chunks.blockLightAt(x, y, z);
            }

    // 1. 清盒内：doSky → 两通道归零；否则仅清方块光（保留天光 —— 火把增删不动天光）。
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                if (doSky)
                    m_chunks.setLight(x, y, z, 0, 0);
                else
                    m_chunks.setLight(x, y, z, m_chunks.skyLightAt(x, y, z), 0);
            }

    // 2. 盒内重 seed 天光（仅 doSky）：每列自顶向下首个遮光方块之上 = 见天 → sky=15（与全量 recomputeLightField
    //    同语义：isSolid 作遮光判据）。仅 seed 落在盒内 y 范围的见天格（盒外 y 范围的格未清，保留旧值）。
    if (doSky) {
        for (int x = x0; x <= x1; ++x) {
            for (int z = z0; z <= z1; ++z) {
                int firstOpaque = H; // 整列无遮光 → 全列见天
                for (int y = H - 1; y >= 0; --y)
                    // t334：遮光判据 lightOpacity > 0（取代 isSolid）—— 半砖(7) / 合活版门(15) 也截断见天 seed。
                    if (BlockRegistry::lightOpacity(m_chunks.blockAt(x, y, z), m_chunks.stateAt(x, y, z)) > 0) { firstOpaque = y; break; }
                for (int y = firstOpaque + 1; y < H; ++y) {
                    if (y < y0 || y > y1) continue;
                    m_chunks.setLight(x, y, z, 15, m_chunks.blockLightAt(x, y, z));
                    skyQ.push({x, y, z});
                }
            }
        }
    }

    // 3. 盒内重 seed 方块光：发光格（lightEmission>0：火把=14 / 岩浆=15 / 燃烧熔炉=13）→ block=该值（保留天光）。无论 doSky。
    //   t351：岩浆自发光 15，流/凝时（tickLavaFlow → setWaterSilent → recomputeLightAround）须重 flood 其方块光。
    //   t494：调状态感知版（传 cell state）—— 燃烧熔炉（state bit2）发 13；熄灭熔炉发 0（其格清零后不入种子，自然无光）。
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                const quint8 emission = BlockRegistry::lightEmission(m_chunks.blockAt(x, y, z), m_chunks.stateAt(x, y, z));
                if (emission > 0) {
                    m_chunks.setLight(x, y, z, m_chunks.skyLightAt(x, y, z), emission);
                    blockQ.push({x, y, z});
                }
            }

    // 4. 盒外边界种子：盒表面格的盒外邻值衰减 1 流入盒内格（光从盒外不变区域渗入）。仅扫盒表面格（内部格无盒外邻）。
    auto applyBoundary = [&](int x, int y, int z, int nx, int ny, int nz) {
        quint8 s = 0, b = 0;
        if (ny >= H) {
            s = 15; // 世界顶之上 = 开阔天空（顶面采样）
        } else if (nx >= 0 && nz >= 0 && nx < W && nz < D && ny >= 0) {
            s = m_chunks.skyLightAt(nx, ny, nz); // 盒内世界格：当前（未清）光值
            b = m_chunks.blockLightAt(nx, ny, nz);
        }
        if (s <= 0 && b <= 0) return;
        // t334：流入衰减按本格 lightOpacity（取代旧 isSolid 二值「遮光格不进光」）—— 透明衰减 1（旧行为）/
        //   半砖衰减 7 / 实体·合活版门衰减 15（满遮断流，同旧 opaque 跳过）。int 运算防 quint8 下溢。
        const quint8 op = BlockRegistry::lightOpacity(m_chunks.blockAt(x, y, z), m_chunks.stateAt(x, y, z));
        const int dec = std::max(1, int(op));
        const quint8 curSky = m_chunks.skyLightAt(x, y, z);
        const quint8 curBlock = m_chunks.blockLightAt(x, y, z);
        if (doSky && s > 0) {
            const int in = int(s) - dec;
            if (in > int(curSky)) { // 衰减 dec 流入；满遮(dec≥15→in≤0) 不进光
                m_chunks.setLight(x, y, z, quint8(in), m_chunks.blockLightAt(x, y, z));
                skyQ.push({x, y, z});
            }
        }
        if (b > 0) {
            const int in = int(b) - dec;
            if (in > int(curBlock)) {
                m_chunks.setLight(x, y, z, m_chunks.skyLightAt(x, y, z), quint8(in));
                blockQ.push({x, y, z});
            }
        }
    };
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                const bool surface = (x == x0 || x == x1 || y == y0 || y == y1 || z == z0 || z == z1);
                if (!surface) continue;
                for (const auto &d : dk) {
                    const int nx = x + d[0], ny = y + d[1], nz = z + d[2];
                    if (!inBox(nx, ny, nz)) applyBoundary(x, y, z, nx, ny, nz);
                }
            }

    // 5. BFS 天光传播（仅 doSky）：从种子向盒内邻格衰减 max(1, lightOpacity)、取 max（t334：取代旧 isSolid 二值）；
    //    盒外邻不传（其值固定，已作边界种子流入）。
    if (doSky) {
        while (!skyQ.empty()) {
            const Cell c = skyQ.front(); skyQ.pop();
            const quint8 cur = m_chunks.skyLightAt(c.x, c.y, c.z);
            if (cur <= 1) continue;
            for (const auto &d : dk) {
                const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
                if (!inBox(nx, ny, nz)) continue;
                const quint8 nbOp = BlockRegistry::lightOpacity(m_chunks.blockAt(nx, ny, nz), m_chunks.stateAt(nx, ny, nz));
                const int prop = int(cur) - std::max(1, int(nbOp));
                if (prop <= 0) continue;
                const quint8 nv = quint8(prop);
                if (nv > m_chunks.skyLightAt(nx, ny, nz)) {
                    m_chunks.setLight(nx, ny, nz, nv, m_chunks.blockLightAt(nx, ny, nz));
                    skyQ.push({nx, ny, nz});
                }
            }
        }
    }
    // 6. BFS 方块光传播（火把）：同规则，独立通道。
    while (!blockQ.empty()) {
        const Cell c = blockQ.front(); blockQ.pop();
        const quint8 cur = m_chunks.blockLightAt(c.x, c.y, c.z);
        if (cur <= 1) continue;
        for (const auto &d : dk) {
            const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (!inBox(nx, ny, nz)) continue;
            const quint8 nbOp = BlockRegistry::lightOpacity(m_chunks.blockAt(nx, ny, nz), m_chunks.stateAt(nx, ny, nz));
            const int prop = int(cur) - std::max(1, int(nbOp));
            if (prop <= 0) continue;
            const quint8 nv = quint8(prop);
            if (nv > m_chunks.blockLightAt(nx, ny, nz)) {
                m_chunks.setLight(nx, ny, nz, m_chunks.skyLightAt(nx, ny, nz), nv);
                blockQ.push({nx, ny, nz});
            }
        }
    }

    // t383：reflood 完成 → 逐 chunk 切片比对快照，光场确有变化的 chunk 标脏（精确替代旧「盒内全 chunk 标脏」）。
    //   充要：chunk 需重建 ⟺ 其顶点色输入（sky/block）变。逐格比对不漏（变化的格其 chunk 必标）不多
    //   （未变 chunk 不标，避免 dirty-storm）。逐 chunk 切片 + 首变即退出（changed）—— 比「逐格标」少路由、
    //   逐 chunk 自然去重（一次 markDirty/chunk）。doSky=false 时 reflood 不动天光 → 比对天光恒等，
    //   仅方块光变化触发（语义不变）。m_chunks.chunk() 越界返 nullptr（盒内坐标本在界内，安全）。
    constexpr int cs = 16; // Chunk::kSize
    int changedChunks = 0;
    for (int ccx = x0 / cs; ccx <= x1 / cs; ++ccx) {
        for (int ccz = z0 / cs; ccz <= z1 / cs; ++ccz) {
            Chunk *ch = m_chunks.chunk(ccx, ccz);
            if (!ch) continue;
            const int ax0 = std::max(x0, ccx * cs), ax1 = std::min(x1, ccx * cs + cs - 1);
            const int az0 = std::max(z0, ccz * cs), az1 = std::min(z1, ccz * cs + cs - 1);
            bool changed = false;
            for (int x = ax0; x <= ax1 && !changed; ++x)
                for (int y = y0; y <= y1 && !changed; ++y)
                    // perf：z 循环上界用 chunk 局部 az1（非全局 z1）—— 旧版用 z1 会读到**下一 chunk**的格
                    //   （ccz 中间块 az1 < z1），把邻 chunk 的光变误判为本 chunk 光变 → 误标本 chunk 脏 →
                    //   放大 dirty-storm（本只光变那 1 个 chunk，却连带标了盒内其后所有 chunk）。az1 亦因此
                    //   恒未使用触发 -Wextra。改 az1 后切片严格限于本 chunk、逐 chunk 精确比对。
                    for (int z = az0; z <= az1 && !changed; ++z) {
                        const size_t i = size_t(x - x0) + bw * (size_t(z - z0) + bd * size_t(y - y0));
                        if (snapSky[i] != m_chunks.skyLightAt(x, y, z) ||
                            snapBlock[i] != m_chunks.blockLightAt(x, y, z))
                            changed = true;
                    }
            if (changed) { ch->markDirty(); ++changedChunks; }
        }
    }
    return changedChunks;
}
