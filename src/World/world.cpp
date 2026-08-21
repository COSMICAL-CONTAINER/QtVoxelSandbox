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
    m_decayingLeaves.clear(); // t325 网格重置 → 渐进衰减队列作废（坐标已不指向当前栅格；防误清新世界叶）
    m_growthCells.clear();   // t425 网格重置 → 生长方格索引作废（finishLoad 写完 blob 后 rebuildGrowthCells 全图重建）
    m_waterCells.clear();    // perf：网格重置 → 流体方格索引作废（finishLoad 写完 blob 后 rebuildFluidCells 全图重建）
    m_lavaCells.clear();
    m_iceCells.clear();      // t495：网格重置 → 普通冰方格索引作废（finishLoad 写完 blob 后 rebuildIceCells 全图重建）
    m_fireCells.clear();     // t724：网格重置 → 火焰方格索引作废（finishLoad 写完 blob 后 rebuildFireCells 全图重建）
    m_powerDirty.clear();    // t656：网格重置 → 红石电力脏集作废（finishLoad 末全量重建红石族脏集）
    // 审查修 B5（t724-t729 复盘）：清要塞传送门坐标 —— 旧版只在世界生成（placeStronghold）记录，读档后
    //   残留上一世界坐标会让暗渊之眼（t729）朝错误方向飞；finishLoad 末 rebindStrongholdPortalFromVoxels
    //   从存档体素反推回写（若本世界确有要塞）。
    m_hasStronghold = false;
    fluidActReset();         // t488：网格重置 → 活动盒作废（旧世界坐标不指向新栅格；finishLoad 置 dirty → 首次全量扫描兜底）
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
    struct Cluster { int minX, maxX, minZ, maxZ, y; int count; };
    std::vector<Cluster> clusters;
    const int W = m_width, D = m_depth, H = m_height;
    for (int y = 0; y < H; ++y) {
        for (int z = 0; z < D; ++z) {
            for (int x = 0; x < W; ++x) {
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::EndPortal) continue;
                // 就近并入既有簇（环 5×5 足迹：bbox 外扩 3 覆盖环内任意两框架间距；同层 ±1 容地面高差）。
                bool merged = false;
                for (Cluster &c : clusters) {
                    if (std::abs(y - c.y) > 1) continue;
                    if (x < c.minX - 3 || x > c.maxX + 3 || z < c.minZ - 3 || z > c.maxZ + 3) continue;
                    c.minX = std::min(c.minX, x); c.maxX = std::max(c.maxX, x);
                    c.minZ = std::min(c.minZ, z); c.maxZ = std::max(c.maxZ, z);
                    ++c.count;
                    merged = true;
                    break;
                }
                if (!merged) clusters.push_back({ x, x, z, z, y, 1 });
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
    m_strongholdPortalY = c.y;
    m_strongholdPortalZ = (c.minZ + c.maxZ) / 2;
    qInfo() << "worldload: stronghold rebind ->" << m_strongholdPortalX << m_strongholdPortalY
            << m_strongholdPortalZ << "frames=" << c.count;
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
//   一次性 3.3M 扫描在生成 / 加载期可接受（非每 tick）。
void World::rebuildFireCells()
{
    m_fireCells.clear();
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

void World::setWidth(int w)  { if (w == m_width)  return; m_width = w;  generate(); emit widthChanged();  emit worldChanged(); }
void World::setDepth(int d)  { if (d == m_depth)  return; m_depth = d;  generate(); emit depthChanged();  emit worldChanged(); }
void World::setHeight(int h) { if (h == m_height) return; m_height = h; generate(); emit heightChanged(); emit worldChanged(); }
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
    if (oldId == id) return false; // 无变化
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
    checkRailOnEdit(x, y, z, oldId, id);      // t565：铁轨连接重算（放 / 破 Rail 或其邻 → 本轨 + 邻轨连接位更新）
    checkEndPortalIntegrity(x, y, z, oldId, id); // t664：末地传送门完整性复检（框架破 → 门面消失）
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

// t146 给定格的碰撞 sub-AABB（世界坐标）。读 blockAt + stateAt → BlockRegistry::collisionAABBs 取 cell-local
//   子盒 → 偏移到世界坐标。越界 blockAt=0(air) → collisionAABBs 空 → 返回空。玩家碰撞（PlayerController）
//   逐格逐 sub-AABB 测试。同源 partialblockgeometry 的 state 解码（碰撞形状 == 渲染形状）。
std::vector<BlockRegistry::BlockAABB> World::collisionAABBsAt(int x, int y, int z) const
{
    const quint8 id = m_chunks.blockAt(x, y, z);
    const quint8 st = m_chunks.stateAt(x, y, z);
    const std::vector<BlockRegistry::BlockAABB> local = BlockRegistry::collisionAABBs(id, st);
    std::vector<BlockRegistry::BlockAABB> out;
    out.reserve(local.size());
    const float fx = float(x), fy = float(y), fz = float(z);
    for (const BlockRegistry::BlockAABB &a : local)
        out.push_back({a.minX + fx, a.minY + fy, a.minZ + fz,
                       a.maxX + fx, a.maxY + fy, a.maxZ + fz});
    return out;
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
    if (oldId == id && oldState == state) return false; // id 与 state 均无变化
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
    checkRailOnEdit(x, y, z, oldId, id);      // t565：铁轨连接重算（放 / 破 Rail 或其邻 → 本轨 + 邻轨连接位更新）
    checkEndPortalIntegrity(x, y, z, oldId, id); // t664：末地传送门完整性复检（框架破 → 门面消失）
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
    checkPressurePlateOnEdit(x, y, z, occ, id); // t494：压力板失撑（TNT 被引燃清 Air → 板上压力板失撑掉落）
    // rv-low-batch2 补齐：点火静默清绕过 setBlock 编辑钩子族 → 邻轨连接 / 雪层坍落同样漏复检（机制同 t494
    //   压力板漏检根因：清成 Air 改变邻轨连接位 / 正上方雪层失撑）。批量路径破坏后统一补调（见
    //   destroySphereSilent 末尾同族补调注释）。
    checkRailOnEdit(x, y, z, occ, id);      // t565：邻轨连接重算（清 Air → 邻轨断向 / 形态切换）
    checkSnowLayerOnEdit(x, y, z, occ, id); // t527：正上方雪层失撑 → 整柱坍落为携带层数的下落实体
    notePowerWrite(x, y, z, occ, id);       // t656：红石电力脏标记（TNT 被点火清 Air → 邻粉 / 邻接收器重算）
    return true;
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
    checkRailOnEdit(x, y, z, oldId, id);         // t565：铁轨连接重算
    checkEndPortalIntegrity(x, y, z, oldId, id); // t664：末地传送门完整性复检
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
            if (!isWoodLike(nb)) continue;
            // 散布概率判定（hashVoxel + 窗口序号 → 确定性伪随机，PLAN §2-K）。命中即焚毁。
            const quint32 hv = hashVoxel(m_seed ^ 0x1A7A, nx, ny, nz) ^ (quint32(m_lavaIgniteIndex) * 2654435761u);
            if ((hv % 100u) < unsigned(kLavaIgnitePct))
                burnTargets.push_back({nx, ny, nz});
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
            checkCactusOnEdit(b.x, b.y, b.z, oldId, BlockRegistry::Air); // ② 失撑复检（正上方 Cactus 整柱坍落，同 setBlock 路径）
            checkDeadBushOnEdit(b.x, b.y, b.z, oldId, BlockRegistry::Air); // t504：枯灌木失撑复检（正上方枯灌木掉落，同 setBlock 路径）
            checkSugarcaneOnEdit(b.x, b.y, b.z, oldId, BlockRegistry::Air); // t524：甘蔗失撑复检（正上方甘蔗整柱坍落，同 setBlock 路径）
            checkSnowLayerOnEdit(b.x, b.y, b.z, oldId, BlockRegistry::Air); // t527：积雪层失撑复检（正上方雪层整柱坍落，同 setBlock 路径）
            pokeFluidDirty(b.x, b.y, b.z); // 焚毁邻接流体 → 标脏 + 活动盒扩展（级联焚毁 / 水流入新坑，同旧 setBlock 语义）
        }
        m_batchFluid = false;
        flushPendingLightEdits(); // 延迟光照重算 → 联合盒一次 refloodBox（无编辑则 no-op）
        emit worldChanged();       // 一次重建（terrain/water 两段各检各的 dirty → 仅脏 chunk 重建）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏（同逐格路径 emit→clear 顺序）
    }
}

// t724 火焰方块系统 tick（见 world.h 头注释）。机制等价 MC 1.0 fire：每窗对活跃火格做寿命 / 蔓延 / 上窜
//   三判定，散布确定性哈希（PLAN §2-K，同 tickLavaFlow ignite pass）。遍历 m_fireCells 位置索引（O(火格数)，
//   lessons perf-fluid-scan：绝不全图扫描）。写入走 4 参数 setBlock（火格写入低频 —— 每窗至多每格 1 写，
//   无需 lava 式批量收口；setBlock 发 blockBroken/blockPlaced → 破块粒子/音 + Main.qml fireHost delegate
//   挂卸自动跟随）。
//   快照校验：迭代中 setBlock 会增删 m_fireCells（重入 noteFireWrite）→ 先拷贝键集再遍历；blockAt != Fire
//   的陈旧项（防御：批量直写路径漏 note 时不崩）直接跳过不误判。
void World::tickFire()
{
    if (++m_fireTickCounter < kFireTickInterval) return; // 节流：0.5s/窗
    m_fireTickCounter = 0;
    ++m_fireIntervalIndex; // 窗口序号（喂 hashVoxel 散布概率 → 不同窗不同火格错峰判定）
    if (m_fireCells.empty()) return;

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

        // (a) 寿命：6 邻（kNb 已含下方 {0,-1,0}）均无可燃方块 → 火无燃料，按概率自熄（setBlock Air → blockBroken
        //     粒子/音 + QML fireHost 收 delegate + noteFireWrite 移除索引）。机制等价 MC 无燃料火渐熄。
        //     审查修 B13（t724-t729 复盘）：旧注释写「6 邻 + 下方」——kNb[6] 本身含下方，逻辑对注释误导，改准。
        bool hasFuel = false;
        for (const auto &n : kNb) {
            const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            if (BlockRegistry::flammable(m_chunks.blockAt(nx, ny, nz))) { hasFuel = true; break; }
        }
        if (!hasFuel) {
            const quint32 hv = hashVoxel(m_seed ^ 0xF177, x, y, z) ^ (quint32(m_fireIntervalIndex) * 2654435761u);
            if ((hv % 100u) < unsigned(kFireExtinguishPct))
                setBlock(x, y, z, BlockRegistry::Air);
            continue; // 无燃料火本窗不做蔓延 / 上窜（燃料都不在，谈不上烧过去）
        }

        // 安全阀：活跃火格超 cap → 本窗不再新增（既有火照常走 (a) 熄灭收敛；防链式大火烧穿重建预算）。
        if (int(m_fireCells.size()) > kFireCellCap) continue;

        // (b) 蔓延：5% 随机选一 6 邻格，为可燃方块 → 点燃（setBlock Fire → blockPlaced + QML delegate 挂载 +
        //     noteFireWrite 入索引，下窗作为新火格继续判定 → 链式烧穿木屋）。可燃物本体被火替换（机制等价
        //     MC 火吞可燃物；非「可燃物旁生火」——蔓延即燃烧，木块变火格）。
        {
            const quint32 hv = hashVoxel(m_seed ^ 0xF179, x, y, z) ^ (quint32(m_fireIntervalIndex) * 2654435761u);
            if ((hv % 100u) < unsigned(kFireSpreadPct)) {
                const auto &n = kNb[hv % 6u];
                const int nx = x + n[0], ny = y + n[1], nz = z + n[2];
                if (nx >= 0 && ny >= 0 && nz >= 0 && nx < W && ny < H && nz < D
                    && BlockRegistry::flammable(m_chunks.blockAt(nx, ny, nz)))
                    setBlock(nx, ny, nz, BlockRegistry::Fire);
            }
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
    }
    // rv-low-batch2 补齐：爆炸批量直写绕过 setBlock 编辑钩子族 → 邻轨连接 / 雪层坍落漏复检（同 tickLavaFlow
    //   焚毁路径逐块调 checkXxxOnEdit 的模式）。对每个破坏格补 checkRailOnEdit（球内破坏改变邻轨连接位 →
    //   直 / 拐角 / 十字形态重算）+ checkSnowLayerOnEdit（破坏格正上方雪层失撑 → 整柱坍落为携带层数的下落
    //   实体）。两检查自带早退（非 Rail / 正上方非 SnowLayer → no-op 零写入零 emit），球 ≤343 格 O(1) × N，
    //   爆炸稀有可忽略。各自的 emit worldChanged / snowLayerFell 与下方收口 emit 叠加无害（重建幂等）。
    for (const DestroyedVoxel &d : destroyed) {
        checkRailOnEdit(d.x, d.y, d.z, d.oldId, BlockRegistry::Air);
        checkSnowLayerOnEdit(d.x, d.y, d.z, d.oldId, BlockRegistry::Air);
    }
    emit worldChanged();
    m_chunks.clearAllDirty();
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
    // ④ 邻接方块：本格新放非 Air 方块 → 水平 4 邻任一为 Cactus 即「邻接方块」→ 该 Cactus 整柱掉落
    //   （机制等价 MC 1.0 仙人掌邻接任何方块即被扎破；覆盖玩家放沙旁 / 落沙落旁等非玩家放置路径）。
    if (id != BlockRegistry::Air) {
        constexpr int kNb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (const auto &d : kNb) {
            const int nx = x + d[0], nz = z + d[1];
            if (nx >= 0 && nz >= 0 && nx < m_width && nz < m_depth
                && y >= 0 && y < m_height
                && m_chunks.blockAt(nx, y, nz) == BlockRegistry::Cactus)
                dropCactusColumn(nx, y, nz);
        }
    }
}

// t504 setBlock 编辑后枯死灌木失撑复检（见 world.h 头注释）。机制等价 MC 1.0 枯灌木失去下方支撑即坍落（同甘蔗 /
//   仙人掌支撑校验族）；但坍落产物为 **木棒**（材料段 0x200）而非枯灌木自身（机制等价 MC dead bush 掉 0-2 木棒，
//   不掉自身 —— 与破花掉花 / 破蘑菇掉蘑菇不同：枯灌木 dropId=0 故即便掉自身也无意义，故失撑走木棒）。
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
//   仅清正上方 1 格。破下方支撑（id==Air 且 oldId 非 flower/mushroom）→ 正上方是 Flower/Mushroom/BrownMushroom
//   → 静默清 Air + emit 破块反馈 + 掉落物（dropId=自身）+ 重 flood 光。玩家直破花 / 蘑菇（oldId==flower/mushroom
//   → id==Air）走 finishMiningAt 通用 drop 路径（dropId=自身方块），避免双重掉落。经 isFlower / isMushroom 单一
//   权威谓词覆盖花 4 色 + 红 / 白两蘑菇（不各处自写 id 判定，同 isBed / isIce 段不连续并判模式）。
//   t571 标注【自然失撑掉落：恒发（含创造）】。
void World::checkFlowerMushroomOnEdit(int x, int y, int z, quint8 oldId, quint8 id)
{
    // 仅本格被破为 Air 且被破块非花 / 蘑菇时，查正上方是否花 / 蘑菇失撑。（被破块本身是花 / 蘑菇时跳过 ——
    //   玩家直破花 / 蘑菇的掉落由通用 finishMiningAt drop 路径负责（dropId=自身），避免双重掉落。）
    if (id != BlockRegistry::Air || BlockRegistry::isFlower(oldId)
        || BlockRegistry::isMushroom(oldId)) return;
    const int by = y + 1;
    if (by < 0 || by >= m_height) return;
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth) return;
    const quint8 above = m_chunks.blockAt(x, by, z);
    if (!BlockRegistry::isFlower(above) && !BlockRegistry::isMushroom(above)) return;
    // 花 / 蘑菇失撑 → 静默清 Air（直写 + 标脏，不经 World::setBlock → 不重入本检查）+ 发破块反馈 + 掉落物 + 重 flood 光。
    m_chunks.setBlock(x, by, z, BlockRegistry::Air);
    noteGrowthWrite(x, by, z, above, BlockRegistry::Air); // 花 / 蘑菇非生长方块 → no-op，保持一致
    emit blockBroken(x, by, z, int(above));                 // 破块粒子 / 音（机制等价 MC 失撑坍落反馈）
    emit blockDroppedAsItem(x, by, z, int(above));         // 呈掉落物实体（Main.qml spawnItem；dropId=自身）
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
                 m_chunks.blockAt(x + dx, y - 1, z + dz) };
    };
    const quint8 curState = m_chunks.stateAt(x, y, z);
    quint8 con = BlockRegistry::railConnections(rb, curState,
                                                probe(1, 0), probe(-1, 0),
                                                probe(0, 1), probe(0, -1));
    // t638：探测轨 bit4（通电视觉）/ t658 动力轨 bit4（通电贴图）不参与连接 —— 合并回写（连接重算不清
    //   「被压过」/「通电」亮态标记）。t666：轴偏好位 bit5 守恒写回（孤轨轴向保活）。
    const quint8 preserved = quint8(curState
        & (BlockRegistry::RailAxisEWFlag
           | (rb == BlockRegistry::DetectorRail ? BlockRegistry::DetectorRailStateOnFlag : 0)
           | (rb == BlockRegistry::GoldenRail ? BlockRegistry::GoldenRailStateOnFlag : 0)));
    con = quint8(con | preserved);
    // t666 轴偏好位镜像当前轴（有连接时）：直轨最后形态 = 孤轨形态（MC 轨断连后保留 metadata 语义）。
    //   拐角（2 垂直位）不算轴 → 保持现有偏好位不变。
    if ((con & (BlockRegistry::RailConnPx | BlockRegistry::RailConnNx)) ==
        (BlockRegistry::RailConnPx | BlockRegistry::RailConnNx))
        con = quint8(con | BlockRegistry::RailAxisEWFlag);      // 贯穿 X → EW 偏好
    else if ((con & (BlockRegistry::RailConnPz | BlockRegistry::RailConnNz)) ==
             (BlockRegistry::RailConnPz | BlockRegistry::RailConnNz))
        con = quint8(con & quint8(~BlockRegistry::RailAxisEWFlag)); // 贯穿 Z → NS 偏好
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
    bool changed = false;
    // t733 铁轨失撑掉落（R19.11「挖掉铁轨底部方块 → 铁轨不得浮空」；普通 / 动力 / 探测三族统一，isRail
    //   单一权威）。守卫（同 checkPressurePlateOnEdit）：仅当本格刚被**清为 Air** 且被清块本身非铁轨——
    //   玩家直破铁轨的掉落由 finishMiningAt 通用 drop 路径负责（三族 dropId=自身），此处再掉会双掉。
    //   判定：正上方是铁轨、且本格（铁轨唯一支撑位，恒为正下方——轨不贴墙、无 state 附着编码可解）已非
    //   有效支撑（isTopFlushSupport 单一权威：完整立方 ∨ 上半砖，t741；与红石粉 / 门族同语义）→ 铁轨立即
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
    if (id == BlockRegistry::Air && !BlockRegistry::isRail(oldId)) {
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
        || BR::isTnt(id)                                 // 接收器：TNT（t658）
        || BR::isDispenser(id) || BR::isDropper(id)      // 接收器：发射器 / 投掷器（t658）
        || id == BR::IronDoor                            // 接收器：铁门（t722，仅红石驱动开合）
        || id == BR::IronTrapdoor                        // 接收器：铁活板门（t723，仅红石驱动开合）
        || BR::isLever(id) || BR::isWoodButton(id) || BR::isStoneButton(id) // 源：拉杆 / 按钮（state bit0）
        || BR::isPressurePlate(id)                       // 源：压力板（state bit0）
        || id == BR::DetectorRail;                       // 源：探测轨有车标记（state bit4）
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

// 编辑路径电力脏标记（挂 4/5 参数 setBlock / setWaterSilent / clearBlockSilent 末尾，同 checkRailOnEdit
// 收口模式）：本格属红石族（粉 / 源 / 接收器）→ 本格入脏集；否则查 6 邻**任意红石族格**（t706：旧版只查
// 邻粉——破掉「源 | 石 | TNT」中间的石块这类无粉场景不入脏集 → 邻源 / 邻接收器电力永不复算 = 用户实测
// 「红石块 / 火把点不着 TNT」的可达性缺口之一；扩到全红石族后惰性块编辑也能唤醒两侧电路），命中才继续
// （全无 → 本次编辑与电力无关，no-op）。
void World::notePowerWrite(int x, int y, int z, quint8 oldId, quint8 newId)
{
    if (!isPowerFamilyBlock(oldId) && !isPowerFamilyBlock(newId)) {
        // 快路径：本格前后都非红石族 → 只有邻格红石连通性可能受影响（如破掉源与接收器之间的石块）。查
        //   6 邻任意红石族格（粉 / 源 / 接收器）才继续（全无 → no-op）。
        static constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        bool anyPower = false;
        for (const auto &d : kNb)
            if (isPowerFamilyBlock(m_chunks.blockAt(x + d[0], y + d[1], z + d[2]))) { anyPower = true; break; }
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
        static constexpr int kDiag[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (const auto &h : kDiag) {
            const int nx = x + h[0], ny = y - 1, nz = z + h[1];
            if (ny >= 0 && BlockRegistry::isRedstoneDust(m_chunks.blockAt(nx, ny, nz)))
                m_powerDirty.insert(packGrowthCell(nx, ny, nz));
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
        if (!seeded && y + 1 < H) {
            for (const auto &h : kHDir2) {
                const int tx = x + h[0], ty = y + 1, tz = z + h[1];
                if (m_chunks.blockAt(tx, ty, tz) != BlockRegistry::RedstoneTorch) continue;
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
            || b == BlockRegistry::IronTrapdoor)       // t723 铁活板门（仅红石驱动开合；单格）
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
    //   receivers 内 isReceivingPower 的动力轨（直供轨），沿轴向 4 邻（x±1 / z±1 同 y 动力轨）分层 BFS，
    //   深度 < kGoldenRailChainMax；可达轨 chainPowered。期望位 = direct ∪ chain——下方 receivers 循环
    //   统一按本表写位（升 / 降沿对称：去源 → direct 消失 → chain 收缩 → 远端轨熄灭）。
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
        constexpr int kGoldenRailChainMax = 8; // 链最长 8 根（MC 1.0 块供电轨链上限；种子自身 1 根 + 向外扩 7 根）
        static constexpr int kAxial[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (int depth = 1; depth < kGoldenRailChainMax && !frontierG.empty(); ++depth) {
            std::vector<GCell> nextG;
            for (const GCell &c : frontierG) {
                for (const auto &a : kAxial) {
                    const int nx = c.x + a[0], ny = c.y, nz = c.z + a[1]; // 同 y 轴向邻（信号不爬坡）
                    if (!inBounds(nx, ny, nz)) continue;
                    if (m_chunks.blockAt(nx, ny, nz) != BlockRegistry::GoldenRail) continue;
                    goldenSeenAll.insert(packGrowthCell(nx, ny, nz)); // 链外轨也记（降沿复查）
                    const quint64 nk = packGrowthCell(nx, ny, nz);
                    if (goldenPowered.insert(nk).second) nextG.push_back({nx, ny, nz});
                }
            }
            frontierG = std::move(nextG);
        }
    }
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
            const bool wantOn = powered || goldenPowered.count(k) > 0;
            const bool on = (st & BlockRegistry::GoldenRailStateOnFlag) != 0;
            if (on != wantOn) {
                m_chunks.setBlock(x, y, z, b, quint8(wantOn ? (st | BlockRegistry::GoldenRailStateOnFlag)
                                                             : (st & quint8(~BlockRegistry::GoldenRailStateOnFlag))));
                // t704 链波前推进：本轨位翻转 → 同轴向邻的动力轨通电位可能因此变（升：链外扩一步；
                //   降：熄灭收缩一步）→ 轴向邻轨入脏集，下一 tick 复查（同粉变化格回插模式；m_chunks
                //   .setBlock 静默写不经 notePowerWrite → 手动补）。稳定后不再翻转 → 不再入集 → 稳态停。
                static constexpr int kAx2[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (const auto &a : kAx2) {
                    const int nx = x + a[0], nz = z + a[1];
                    if (inBounds(nx, y, nz) && m_chunks.blockAt(nx, y, nz) == BlockRegistry::GoldenRail)
                        m_powerDirty.insert(packGrowthCell(nx, y, nz));
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
            static constexpr int kAx3[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (const auto &a : kAx3) {
                const int nx = x + a[0], nz = z + a[1];
                if (inBounds(nx, y, nz) && m_chunks.blockAt(nx, y, nz) == BlockRegistry::GoldenRail)
                    m_powerDirty.insert(packGrowthCell(nx, y, nz));
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
        // t740 再排除**基座环粉**（火把 4 个斜下格的通电粉）：t740 斜下供粉后，立在方块顶面的火把喂
        //   亮的地面环粉同时是支撑块的水平 6 邻 → 无形状语义的 v1 读法会把火把自己的输出当输入 →
        //   反相熄灭 → 环粉断电 → 重亮 → 振荡（用户「灯闪 / 时亮时不亮」形态）。MC 里粉按连接形状
        //   供电（点 / 切向线不向侧邻块灌电）——环粉永不回灌支撑；v1 无形状位 → 直接排除火把斜下
        //   4 格的粉。支撑仍可被同层线 / 其它侧的源供电（NOT 门输入路径不变）。
        const bool attachPowered = [&]() {
            static constexpr int kNb2[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            const int sx = x + ax, sy = y + ay, sz = z + az;
            for (const auto &d : kNb2) {
                const int nx = sx + d[0], ny = sy + d[1], nz = sz + d[2];
                if (nx == x && ny == y && nz == z) continue; // 跳过火把自身（防自反馈振荡）
                if (powerSourceLevel(nx, ny, nz) > 0) return true; // 真实源（拉杆 / 红石块…）在环位也照常供电
                // t740：仅排除**粉**在基座环位的回灌（见上注）——源不受形状语义影响，仍可 NOT 门输入。
                if (ny == y - 1 && qAbs(nx - x) + qAbs(nz - z) == 1) continue;
                const quint8 nb = m_chunks.blockAt(nx, ny, nz);
                if (BlockRegistry::isRedstoneDust(nb)
                    && (m_chunks.stateAt(nx, ny, nz) & BlockRegistry::RedstoneDustPowerMask) > 0)
                    return true;
            }
            return false;
        }(); // 附着块被供电（含粉 / 源；不含本火把与其基座环输出）
        const bool off = (st & BlockRegistry::RedstoneTorchStateOffFlag) != 0;
        if (attachPowered != off) {
            // 供电 → 置熄灭位；失电 → 清熄灭位重亮。附着位（低 3 位）不动。
            const quint8 ns = quint8(attachPowered ? (st | BlockRegistry::RedstoneTorchStateOffFlag)
                                                   : (st & quint8(~BlockRegistry::RedstoneTorchStateOffFlag)));
            m_chunks.setBlock(x, y, z, BlockRegistry::RedstoneTorch, ns);
            recomputeLightAround(x, y, z, BlockRegistry::RedstoneTorch, st, BlockRegistry::RedstoneTorch, ns);
            // 火把供能变化（15↔0）→ 其 6 邻粉须重算 → 火把格重入脏集（下一 tick 传播；定点迭代）。
            m_powerDirty.insert(k);
            any = true;
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
World::Biome World::biomeAt(int x, int z) const
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
    m_decayingLeaves.clear(); // t325 全新世界无失撑叶 → 清渐进衰减队列（防旧世界坐标误清新世界叶）
    m_growthCells.clear();   // t425 全新世界 → 清生长方格索引（worldgen placeSugarcane 经 setVoxelIfAir 增量重建）
    m_waterCells.clear();    // perf：全新世界 → 清流体方格索引（worldgen 直写 chunk 不经写入路径 → 末尾 rebuildFluidCells 全图重建）
    m_lavaCells.clear();
    m_iceCells.clear();      // t495：全新世界 → 清普通冰方格索引（worldgen freezeSurfaceWater 直写 chunk → 末尾 rebuildIceCells 全图重建）
    m_fireCells.clear();     // t724：全新世界 → 清火焰方格索引（worldgen 无火 → 稳态空集零开销；玩家点燃经 noteFireWrite 增量维护）
    m_powerDirty.clear();    // t656：全新世界 → 清红石电力脏集（worldgen 无红石电路 → 稳态空集零开销）
    fluidActReset();         // t488：全新世界 → 活动盒作废（generate 末置 dirty → 首次全量扫描兜底）
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
                // t503 仙人掌柱 4 邻守卫：MC 1.0 仙人掌不可邻接任何方块（邻接即被扎破掉落）。worldgen 散布时
                //   跳过「柱位任一格的水平 4 邻有实体方块」的位置（否则生成即立即破坏掉落，等同浪费 + 留下掉落物
                //   堆积）。整柱（surfaceY+1..surfaceY+height）4 邻全无实体方块（isSolid）才放置。沙丘起伏时邻格可能
                //   更高（实体沙）→ 守卫跳过，仅平坦沙顶散布（机制等价 MC 沙漠仙人掌稀疏独立柱，不挤在沙丘边）。
                //   注意 isSolid 取 mesher 邻居面剔除语义（同 setBlock 放块路径 checkCactusOnEdit ④ 守卫），覆盖
                //   沙 / 石 / 木等实体方块；非 solid（草丛 / 火把 / 水）不算「邻接方块」（MC 仙人掌可邻草丛 / 水）。
                const int height = 1 + int((r >> 16) % 3u);
                bool neighborsClear = true;
                for (int i = 0; i < height && neighborsClear; ++i) {
                    const int yy = surfaceY + 1 + i;
                    if (yy >= m_height) break;
                    constexpr int kNb[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                    for (const auto &d : kNb) {
                        if (BlockRegistry::isSolid(m_chunks.blockAt(x + d[0], yy, z + d[1]))) {
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

//   确定性矿石散布（t84/t279/t308/t569，PLAN §2-K）：遍历 stone 区段（generate 把 y<h-2 的格填 Stone，沙漠/沙滩表层
//   除外），按 hashVoxel(seed,x,y,z) 的不同位段做密度筛选 → 替换为煤矿 / 铜矿 / 铁矿 / 金矿 / 钻石矿 / 红石矿。
//   **高度分层**（机制等价 MC 1.0 矿物随深度分层 + spec t308「铜铁金按序更稀少」）：
//     - 钻石（diamond_ore）：深层 y∈[kDiamondMin=5, kDiamondMax=40]（紧贴基岩 kBedrockTop=4 之上）。
//       密度最低（稀有，0.4%）。**t308 深度修正**：上界 16→40（用户 research 后定，地表 ~62、洞穴贯穿深层 →
//       深挖更易见钻矿石；密度仍最低故整体稀有度不变）。需铁镐（minTier3）。
//     - 金（gold_ore）：深层 y∈[kOreMin=5, kGoldMax=25]（机制等价 MC 金矿深层富集）。密度次低（稀有，0.5%）。
//       金属族中最稀有（spec「铜铁金按序更稀少」→ 金最稀有）。需铁镐（minTier3）。掉金原矿→熔炉烧金锭。
//     - 青金（lapis_ore，t471）：深层 y∈[kOreMin=5, kLapisMax=31]（机制等价 MC 1.0 青金矿 Y<32 浅深层富集）。
//       密度中低（稀有，0.6%）。需石镐（minTier2，同 iron/copper 门槛）。掉青金石物品（附魔前置材料，t471）。
//     - 红 石（redstone_ore，t569）：最深层 y∈[kOreMin=5, kRedstoneMax=16]（机制等价 MC 1.0 红石矿 Y<16
//       深层富集）。密度与钻石相当（0.4%）。需铁镐（minTier3，同 diamond/gold 门槛）。掉 4 红石粉（指南针 /
//       钟合成材料）。玩家走过 / 挖掘时点亮微弱红光（playercontroller scanRedstoneOre，机制等价 MC 触发发光）。
//     - 铁（iron_ore）：中层 y∈[kOreMin=5, kIronMax=30]（机制等价 MC 铁矿中下层富集）。中等密度（0.7%）。
//       需石镐（minTier2）。掉铁原矿→熔炉烧铁锭。
//     - 铜（copper_ore）：浅中层 y∈[kOreMin=5, kCopperMax=45]（机制等价 MC 铜矿浅中层富集）。密度次高（0.9%）。
//       金属族中最常见 / 最浅（spec「铜铁金按序更稀少」→ 铜最常见）。需石镐（minTier2）。掉铜原矿→熔炉烧铜锭。
//     - 煤（coal_ore）：浅层 y∈[kCoalMin=8, stoneTop]（机制等价 MC 煤矿靠近地表富集）。最高密度（1.0%）。
//       木镐可挖（minTier1）。直接掉煤炭（燃料 / 火把原料，无需冶炼）。
//   判定用两路独立哈希（r = hashVoxel(seed,...) 给钻石/铁/煤沿用旧位段 0/8/16，保旧矿脉分布；r2 = hashVoxel
//   (seed^黄金比例常量,...) 给金/铜/青金/红石独立流）→ 7 矿各自独立。判定序（重叠区稀有矿优先）：钻石 > 金 >
//   青金 > 红石 > 铁 > 铜 > 煤（先中者胜、一格至多一矿）。仅替换 Stone；同 seed → 同矿脉分布；禁用任何运行期
//   随机源（QTime/时钟/全局 RNG）。
//
//   **洞穴裸露矿物**（spec 核心）：worldgen 顺序 scatterOres → carveCaves，carveCaves（t278）挖走 stone/ore
//   暴露矿脉于洞壁。各矿按深度分层 + 洞穴贯穿 → 各层洞壁天然见对应矿脉（spec「洞穴 carve 自然暴露」）。
void World::scatterOres()
{
    constexpr int kOreMin      = 5;   // 矿物起始 y（紧贴基岩 kBedrockTop=4 之上；基岩层 y 0..4 不布矿）
    constexpr int kCoalMin     = 8;   // 煤起始 y（仅浅层；机制等价 MC 煤靠近地表富集）
    constexpr int kDiamondMin  = 5;   // 钻石起始 y（= kOreMin，紧贴基岩）
    constexpr int kDiamondMax  = 40;  // 钻石上界 y（t308：16→40，用户 research 后定；地表 ~62 深挖更易见）
    constexpr int kGoldMax     = 25;  // 金上界 y（t308；机制等价 MC 金矿深层富集；金属族最深）
    constexpr int kLapisMax    = 31;  // 青金上界 y（t471；机制等价 MC 1.0 青金矿 Y<32 浅深层富集）
    constexpr int kRedstoneMax = 16;  // 红石上界 y（t569；机制等价 MC 1.0 红石矿 Y<16 最深层富集）
    constexpr int kIronMax     = 30;  // 铁上界 y（机制等价 MC 铁矿中下层富集）
    constexpr int kCopperMax   = 45;  // 铜上界 y（t308；机制等价 MC 铜矿浅中层富集；金属族最浅）

    // 密度（/10000，每体素命中概率）：钻石 / 红石最稀 < 金 < 青金 < 铁 < 铜 < 煤（最常见）。
    //   spec t308「铜铁金按序更稀少」→ 铜(0.9%) > 铁(0.7%) > 金(0.5%)；钻石(0.4%) / 煤(1.0%) 各为两端。
    //   青金(0.6%) 介于金(0.5%) 与 铁(0.7%) 之间（MC 1.0 青金稀有度近金 / 铁）。红石(0.4%) 与钻石相当
    //   （t569；MC 1.0 红石在最深层 Y<16 与钻石共层、稀有度相当）。洞穴 carve 暴露后矿脉出露更
    //   可见（spec「洞穴裸露矿物」）；密度调到「分层肉眼可辨 + 不过密糊洞壁」。
    constexpr unsigned kDiamondPct  = 40;   // /10000 → 0.4%（钻石，需铁镐 minTier3；稀有深层）
    constexpr unsigned kGoldPct     = 50;   // /10000 → 0.5%（金，需铁镐 minTier3；金属族最稀有，t308）
    constexpr unsigned kLapisPct    = 60;   // /10000 → 0.6%（青金，需石镐 minTier2；t471 附魔前置材料，深层 Y<32）
    constexpr unsigned kRedstonePct = 40;   // /10000 → 0.4%（红石，需铁镐 minTier3；t569 最深层 Y<16，稀有度同钻石）
    constexpr unsigned kIronPct     = 70;   // /10000 → 0.7%（铁，需石镐 minTier2；中层）
    constexpr unsigned kCopperPct   = 90;   // /10000 → 0.9%（铜，需石镐 minTier2；金属族最常见 / 最浅，t308）
    constexpr unsigned kCoalPct     = 100;  // /10000 → 1.0%（煤，木镐可挖 minTier1；浅层最常见）

    int coalPlaced = 0, copperPlaced = 0, ironPlaced = 0, goldPlaced = 0, diamondPlaced = 0, lapisPlaced = 0, redstonePlaced = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int h = std::min(heightAt(x, z), m_height - 1);
            // t149：水位阈值取代旧 kSandLevel=3 —— 沙滩带(wl±1)/水下(h<wl) 列表层为沙、沙漠整柱沙，
            //   这些列无 stone 区段（或被水位淹没），跳过（与 generate / placeTrees 同阈值）。
            if (h <= kWaterLevel + 1) continue;

            // stone 区段：y < h-2（与 generate 填 Stone 同阈值；y in [h-2,h] 是 dirt/grass）。
            // 上界 h-3 即「< h-2」的最大整数；y 非负由循环保证。
            const int stoneTop = h - 3;
            for (int y = 0; y <= stoneTop; ++y) {
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::Stone)
                    continue; // 仅替换 stone（防御：树根/边界异常格不动；已生成的它种矿也不动）
                const quint32 r  = hashVoxel(m_seed, x, y, z);
                // 第二路独立哈希流（黄金比例常量作 seed salt → 与 r 良好解耦）给金 / 铜判定，避免与 r 的
                //   位段（0/8/16）重叠；钻石/铁/煤沿用 r 的旧位段保旧矿脉分布不变（仅钻石 Y 上界扩到 40）。
                const quint32 r2 = hashVoxel(int(quint32(m_seed) ^ 0x9E3779B9u), x, y, z);
                // 判定序（重叠区稀有矿优先）：钻石 > 金 > 铁 > 铜 > 煤。先中者胜 → 一格至多一矿。
                if (y >= kDiamondMin && y <= kDiamondMax) {
                    if (((r       ) % 10000u) < kDiamondPct) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::DiamondOre);
                        ++diamondPlaced;
                        continue;
                    }
                }
                if (y >= kOreMin && y <= kGoldMax) {
                    if (((r2      ) % 10000u) < kGoldPct) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::GoldOre);
                        ++goldPlaced;
                        continue;
                    }
                }
                if (y >= kOreMin && y <= kLapisMax) {
                    // 青金走 r2 >> 16 位段（r2 现仅用位段 0=金 / 8=铜，位段 16 空闲）→ 与金 / 铜独立，
                    //   不扰旧矿脉分布。判定序位于金之后、铁之前（重叠区稀有度近金，先于铁）。
                    if (((r2 >> 16) % 10000u) < kLapisPct) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::LapisOre);
                        ++lapisPlaced;
                        continue;
                    }
                }
                if (y >= kOreMin && y <= kRedstoneMax) {
                    // 红石走 r2 >> 24 位段（r2 现用位段 0=金 / 8=铜 / 16=青金，位段 24 空闲）→ 与金 / 铜 /
                    //   青金独立，不扰旧矿脉分布（t569）。判定序位于青金之后、铁之前（重叠区 Y<16 与钻石 /
                    //   金 / 青金共层，稀有矿优先；红石与钻石稀有度相当故置于铁 / 铜前）。
                    if (((r2 >> 24) % 10000u) < kRedstonePct) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::RedstoneOre);
                        ++redstonePlaced;
                        continue;
                    }
                }
                if (y >= kOreMin && y <= kIronMax) {
                    if (((r  >> 8) % 10000u) < kIronPct) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::IronOre);
                        ++ironPlaced;
                        continue;
                    }
                }
                if (y >= kOreMin && y <= kCopperMax) {
                    if (((r2 >> 8) % 10000u) < kCopperPct) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::CopperOre);
                        ++copperPlaced;
                        continue;
                    }
                }
                if (y >= kCoalMin) {
                    if (((r  >> 16) % 10000u) < kCoalPct) {
                        m_chunks.setBlock(x, y, z, BlockRegistry::CoalOre);
                        ++coalPlaced;
                        continue;
                    }
                }
            }
        }
    }
    qInfo() << "worldgen: ores placed = coal" << coalPlaced << "copper" << copperPlaced
            << "iron" << ironPlaced << "gold" << goldPlaced
            << "diamond" << diamondPlaced << "lapis" << lapisPlaced
            << "redstone" << redstonePlaced; // 同 seed → 同计数（确定性核对）
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
    //   (1a) 高源瀑布门控检测 —— 排水**前**探测峡壁环带真实含水（review-L9，详下）；(1b) 排水带 —— 排干
    //   峡谷带内残余水（兜底盘外边缘池水渗出）；(2) 邻接侧洞 —— 沿峡壁刻短隧道连既有洞穴；(1c) 高源瀑布
    //   置源 —— 门控命中才在峡心柱高悬一格水源，t350 限流下成细瀑布 + 小水洼（点缀非泛滥）。
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

    // t376 (1a) 高源瀑布门控检测（review-L9 修复；**必须先于排水带跑**，见下）。排水带固定半径：
    //   盘上限 (~5) + 2 余量 → 7；检测的壁环外沿即取它（壁环 = carve 盘上限之外、排水带之内的岩壁圈）。
    constexpr int kDrainRadius = kBaseRadius + kTopExtra + 2;
    // t601 原门控在候选格 ±1（carve 盘**内**）采样 —— 盘内已被本 pass carve 排空恒 Air；且 worldgen 顺序
    //   海水由其后的 fillWater 才灌、此刻唯一的天然水（地下水池）尚未被排水带排干 → 门控构造性几乎永不
    //   命中（t601 commit 自述「现实里门控基本不命中」），瀑布特性形同移除。修（恢复 t601 意图：峡壁切穿
    //   含水层才渗水成瀑）：在排水带排干**之前**探测**峡壁环带** —— 4 主向 × 距离 [盘上限+1, 排水半径] 的
    //   壁列 × 全高 [kFloor, topY] 扫 Water（池水此刻仍在壁内）。路径点按 kFallEvery 步进采样（瀑布频次
    //   适度：非每点都试，命中即停取**首个**——一峡谷一瀑）；未命中保持干涸峡谷（门控初衷不变：无中生有
    //   的孤立水源不生成，频次天然收敛于真实含水层接触）。全程纯函数于 seed（路径 + blockAt 均确定，
    //   PLAN §2-K）。
    constexpr size_t kFallEvery = 8; // 检测步进（path ≤280 → 至多 ~35 个候选点）
    int waterfallX = -1, waterfallZ = -1, waterfallY = -1;
    for (size_t i = 0; i < path.size() && waterfallY < 0; i += kFallEvery) {
        const CanyonPt &wp = path[i];
        if (wp.span < 6) continue; // 至少 6 格落差才有「瀑布」观感
        const int topY = kFloor + (wp.span * 3) / 4; // 高位（距底 3/4 跨度），其下峡谷空气 → 细瀑
        if (topY >= m_height) continue;
        static const int kDirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
        bool wallWater = false;
        for (const auto &d : kDirs) {
            for (int dist = kBaseRadius + kTopExtra + 1; dist <= kDrainRadius && !wallWater; ++dist) {
                const int nx = wp.ix + d[0] * dist;
                const int nz = wp.iz + d[1] * dist;
                if (nx < 0 || nx >= m_width || nz < 0 || nz >= m_depth) continue;
                for (int y = kFloor; y <= topY; ++y) { // 全高扫：含水层接触可能在低位
                    if (m_chunks.blockAt(nx, y, nz) == BlockRegistry::Water) { wallWater = true; break; }
                }
            }
        }
        if (wallWater) { waterfallX = wp.ix; waterfallZ = wp.iz; waterfallY = topY; }
    }

    // t376 (1b) 排水带：盘外（半径 > 当前盘半径）仍可能有地下水池残水紧贴峡壁 → 暴露后渗出。沿路径中心以
    //   固定半径（kDrainRadius）逐柱排干 [kFloor, surfaceY] 内的水格 → 峡谷带内无水可渗。池水远端（带外）
    //   仍被实体岩封闭（稳态）。一次 worldgen 开销可接受。（(1a) 检测在其前 —— 壁环水此刻尚未排干。）
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

    // t376 (1c) 高源瀑布置源（review-L9）：(1a) 在排水前已确认峡壁环带存在真实含水（地下水池切壁）→
    //   此刻（排水带已干、但壁环水源仍被实体岩封在原位）在峡心柱置一格 Water 源——t350 限流下成细瀑布 +
    //   小水洼（点缀非泛滥；机制语义「峡壁切穿含水层渗出」）。置源格仍须为峡谷空气（carve 已过 → 恒真，
    //   防御性再判）；未命中门控（waterfallY<0）→ 不置水（无中生有的孤立水源不生成，保持干涸峡谷）。
    if (waterfallY >= 0
        && waterfallY < m_height
        && m_chunks.blockAt(waterfallX, waterfallY, waterfallZ) == BlockRegistry::Air) {
        m_chunks.setBlock(waterfallX, waterfallY, waterfallZ, BlockRegistry::Water); // 源（state 默认 0）
    } else {
        waterfallY = -1; // 记 -1 供下方确定性日志核对
    }

    qInfo() << "worldgen: grand canyon carved =" << carvedVoxels
            << "(steps" << steps << "floor" << kFloor << ")"; // 同 seed → 同计数（确定性核对）
    qInfo() << "worldgen: canyon drained =" << drainedCells
            << "side caves =" << sideCaves
            << "waterfall y =" << waterfallY; // t376 确定性核对
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
//   房间几何（固定 5×4×5 = 内部空气体积 W×H×D，墙体在 [-1, W]×[-1, H]×[-1, D] 外圈）：
//     - 地板 / 顶板 / 四壁：填 Cobble（默认）+ Stone（按 hashVoxel 散布少量石块混排，机制等价 MC 1.0 地牢
//       圆石 + 苔石 + 石砖混合墙体；本工程无 mossy_cobble / stone_brick 方块故用 cobble + stone 二者混排）。
//     - 内部 (0..W-1, 0..H-1, 0..D-1)：置 Air（清空原 stone / ore / cave air → 干净房间）。不动 Bedrock
//       （基岩层不可破）。
//     - 中央 (W/2, 1, D/2)：置 Spawner（地板上方一格 = 站立高度；玩家走过来触发刷怪）。
//     - 角落 (0, 1, 0)：置 Chest（t393 填战利品内容；本任务仅放置空箱方块，机制等价 MC 1.0 地牢箱子）。
//
//   空腔被实体墙天然封闭 → 房间内无天光 → 黑暗（机制等价 MC 1.0 地牢黑暗环境 + 刷怪笼刷怪条件）。
//   与既有洞穴重叠时（carveCaves 已挖空同位）→ 墙体在洞穴侧被截断，地牢轮廓仍可见（同 MC 1.0 地牢被洞穴
//   穿墙暴露）。t343 岩浆湖之后（避免岩浆湖填进地牢房间 —— placeLavaLakes 不动 Cobble 墙体，地牢墙体先于
//   岩浆湖不存在 → 顺序无关；此处放其后保持「流体 worldgen 优先于结构」惯例，避免岩浆与房间争夺同列）。
//   fillWater 之前（房间独立于海平面；fillWater 仅填地表低洼 → 地下房间不被灌水）。
void World::placeDungeons()
{
    constexpr int kDungeonGrid     = 24;     // 候选网格间距（t426：18→24，比岩浆湖 16 明显更稀 → 地牢更稀有）
    constexpr unsigned kDungeonPct = 10u;    // 候选命中概率（t426：35%→10%，spec「稀有但房间级」；10% → 每网格平均 ~0.10 个地牢）
    constexpr int kBedrockTop      = 4;      // 不动基岩（同 carveCaves / placeBedrock）
    constexpr int kSurfaceFloor    = 6;      // 与地表保留的最小距离（地牢上方至少 6 格石顶 → 不破地表、封闭黑暗）
    constexpr int kDungeonMaxY     = 36;     // 地牢最高 y（spec「地下」；避开近地表 / 仅地下深处）
    constexpr int kRoomW           = 7;      // 房间内部宽度（t426：5→7，房间级而非「几格」；X 方向格子数）
    constexpr int kRoomH           = 4;      // 房间内部高度（Y 方向格子数；3-4 高范围，取 4 ≈ MC 1.0 地牢高度）
    constexpr int kRoomD           = 7;      // 房间内部深度（t426：5→7；Z 方向格子数）
    // 房间边界（墙在 [-1, kRoomW] / [-1, kRoomD] 外圈）→ 留 (kRoomW+2) 格 X/Z 边界防越界。
    constexpr int kMargin          = (kRoomW > kRoomD ? kRoomW : kRoomD) + 1;

    int placed = 0;
    const int dungSeed = m_seed + 12037; // 地牢哈希偏移（与其它 worldgen hashColumn 解耦）
    for (int bx = kDungeonGrid / 2; bx < m_width; bx += kDungeonGrid) {
        for (int bz = kDungeonGrid / 2; bz < m_depth; bz += kDungeonGrid) {
            const quint32 r = hashColumn(dungSeed, bx, bz);
            if ((r % 100u) >= kDungeonPct) continue; // 概率筛选
            const int span = kDungeonGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
                continue; // 留 margin 格边界（房间墙体半径 ≤ margin 不越界）
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠地牢（避免与海水柱冲突）
            const int h = std::min(heightAt(cx, cz), m_height - 1);
            // 地牢 y 范围：基岩之上 ~ kDungeonMaxY 之下；上方至少留 kSurfaceFloor 格石顶（不破地表、封闭黑暗）。
            const int yLo = kBedrockTop + 2;
            const int yHi = std::min(kDungeonMaxY - kRoomH, h - kSurfaceFloor - kRoomH);
            if (yHi <= yLo) continue; // 此列地下空间不足（极低洼 / 山顶浅层）→ 跳过
            const int yRange = yHi - yLo + 1;
            const int cy = yLo + int((r >> 9) & 0x1Fu) % yRange; // 房间底面（地板）y

            // 房间墙体材料：Cobble（默认）+ Stone（散布混排，机制等价 MC 1.0 苔石 / 石砖混入）。
            //   per-cell hash 位 → ~25% Stone / ~75% Cobble（Cobble 主体显「圆石房」，Stone 点缀差异）。
            auto wallBlock = [&](int wx, int wy, int wz) -> quint8 {
                const quint32 wb = hashVoxel(dungSeed ^ 0x5a5a, wx, wy, wz);
                return (wb % 100u) < 25u ? BlockRegistry::Stone : BlockRegistry::Cobble;
            };

            // 1) 周界填墙（地板 / 顶板 / 四壁）：遍历 [-1, kRoomW]×[−1, kRoomH]×[−1, kRoomD] 外圈，
            //    对每个边界格置 wallBlock（不动 Bedrock）。内部空气在步骤 2 清空。
            for (int dy = -1; dy <= kRoomH; ++dy) {
                const int yy = cy + dy;
                if (yy < 0 || yy >= m_height) continue;
                const bool yEdge = (dy == -1 || dy == kRoomH); // 地板（dy=-1）/ 顶板（dy=kRoomH）
                for (int dx = -1; dx <= kRoomW; ++dx) {
                    for (int dz = -1; dz <= kRoomD; ++dz) {
                        const bool xEdge = (dx == -1 || dx == kRoomW);
                        const bool zEdge = (dz == -1 || dz == kRoomD);
                        if (!yEdge && !xEdge && !zEdge) continue; // 内部格由步骤 2 处理（清空气）
                        const int px = cx + dx, pz = cz + dz;
                        const quint8 cur = m_chunks.blockAt(px, yy, pz);
                        if (cur == BlockRegistry::Bedrock) continue; // 不动基岩（保留 worldgen 底层）
                        m_chunks.setBlock(px, yy, pz, wallBlock(px, yy, pz));
                    }
                }
            }
            // 2) 内部清空气（W×H×D）：覆盖原 stone / ore / cave air → 干净房间（防墙体填充误入内部、
            //    防 cave 残余格子留洞）。不动 Bedrock（防穿透基岩底层）。
            for (int dy = 0; dy < kRoomH; ++dy) {
                const int yy = cy + dy;
                if (yy < 0 || yy >= m_height) continue;
                for (int dx = 0; dx < kRoomW; ++dx) {
                    for (int dz = 0; dz < kRoomD; ++dz) {
                        const quint8 cur = m_chunks.blockAt(cx + dx, yy, cz + dz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(cx + dx, yy, cz + dz, BlockRegistry::Air);
                    }
                }
            }
            // 3) 中央 Spawner（地板上方一格 = cy+1 = 站立高度）。覆盖原空气格；不动非空气（防 cave 重叠时
            //    误覆盖既有方块，但步骤 2 已清空气 → 此处恒为 Air，覆盖安全）。
            m_chunks.setBlock(cx + kRoomW / 2, cy + 1, cz + kRoomD / 2, BlockRegistry::Spawner);
            // 4) 角落 Chest（与 Spawner 对角 = 角落 (0, 1, 0)）：t393 首开填充地牢战利品（ChestStore::populateDungeonLoot，
            //    由 Main.qml.openChest 据下面的 state 标记触发）。state 带 ChestStateDungeonFlag(bit2) 标「地牢生成箱」
            //    → World::isDungeonChest 返 true → 玩家首开时填充；玩家自放的箱子无此标记 → 不填（机制对齐 MC）。
            //    朝向低 2 位 = 0（chestFrontFace 兜底 NegZ；worldgen 不关心箱子朝向）。
            m_chunks.setBlock(cx, cy + 1, cz, BlockRegistry::Chest, BlockRegistry::ChestStateDungeonFlag);
            ++placed;
        }
    }
    qInfo() << "worldgen: underground dungeons =" << placed; // 同 seed → 同计数（确定性核对）
}

// t484/t565 废弃矿井（见 world.h 头注释）。机制等价 MC 1.0 废弃矿井 mineshaft：地下深处（Y<50）的**连通巷道
//   网络**（t565 重做：旧版单条直线巷道 → 用户报「生成直线，应连通 / 角落生成洞穴」）。含中央交叉洞室（多巷道
//   汇合处的方形洞穴，t565 ①⑥）+ 木栅栏立柱 + 木板 / 石头地板（t565 ⑤：按矿井 hash 二选一，非恒木板）+ 连续
//   铁轨（t565 ④：铺后统一算连接 state → 直轨 / 拐角 / 十字自动互连）+ 蜘蛛网 + 暴露矿石 + 立柱火把（t565 ③：
//   立柱顶确定性散布 Torch，机制等价 MC 矿井昏暗火把照明）+ 宝藏箱子。确定性散布（hashColumn + seed 偏移，
//   PLAN §2-K），同 seed 同分布。
//
//   结构几何（一个矿井 = 中央交叉洞室 + 3..4 条折线巷道）：
//     - **中央交叉洞室**（t565 ①⑥「角落生成洞穴 / 两边长条连通」）：7×7×4 空气洞穴（地板 y=sy、内部空气
//       y=sy+1..sy+4），地板同巷道材质（Planks 或 Stone）。四壁保留原岩（不清 → 天然围岩），洞室与各巷道
//       端口互通（巷道自洞室边缘向外延伸 → 玩家可从任一巷道走进洞室再出去 = 连通网络，修「两条长条互不相通」）。
//     - **折线巷道**（t565 ①「应转弯」）：每条巷道自洞室边缘出发，先沿主向推进 lenA 段，再**转向 ±90°**
//       （hash 选左 / 右转）推进 lenB 段 → L 形折线。每段 3 宽 × 3 高内部空气 + 1 层地板；两段共享拐角格
//       自然衔接（无缝）。巷道数 3..4（hash 选）→ 自洞室向 3..4 个方向辐射。
//     - **地板**（t565 ⑤「矿坑底可石头非木板」）：按矿井 hash（(r>>20)&1）选 Planks（旧观感）或 Stone（石底
//       变体），全矿井统一。
//     - 立柱（每 kFenceInterval 段一对 WoodFence 两格高）/ 蛛网（~12% 上角 Cobweb）/ 矿石（~15% 巷壁
//       CoalOre/IronOre）：同旧版语义。**立柱顶火把**（t565 ③）：~kTorchPct 概率在立柱正上（y=sy+3）置
//       Torch state=0（块光 flood14 照亮巷道 + 呈现层 enterWorld 全图扫描建伪光源 delegate；不占巷道行走层
//       与铁轨线 → 无冲突）。
//     - **铁轨**（t565 ④「放下应连接 / 能转弯」）：巷道中线（w=0）**每段连续铺**（旧版隔 2 段铺一根 → 断续）
//       + 洞室中央十字两排。全部铺完后统一经 BlockRegistry::railConnections 算连接 state（直 / 拐角 / 十字
//       形态自动得出，mesher 据此切贴图；与运行期 checkRailOnEdit 同一权威）。
//     - 宝藏箱（洞室内偏侧；带 ChestStateMineshaftFlag → isMineshaftChest → 首开填充矿井战利品）。
//
//   巷道被周围实体岩天然封闭 → 内部无天光 → 黑暗 + 火把点光（机制等价 MC 1.0 矿井环境）。与既有洞穴重叠时
//   （carveCaves 已挖空同位）→ 结构仍画出（矿井叠加于洞穴，同 placeDungeons 墙体被洞穴截断）。
//   placeDungeons 之后、fillWater 之前（独立于海平面；fillWater 仅填地表低洼 → 地下矿井不被灌水）。
void World::placeMineshaft()
{
    constexpr int kMineshaftGrid    = 36;     // 候选网格间距（比地牢 24 更稀 → 矿井更稀有；spec「随机生成」）
    constexpr unsigned kMinePct     = 40u;    // 候选命中概率（每网格平均 ~0.40 个矿井 → 160×160 世界约 6 个矿井）
    constexpr int kBedrockTop       = 4;      // 不动基岩（同 carveCaves / placeDungeons）
    constexpr int kSurfaceFloor     = 6;      // 与地表保留的最小距离（矿井上方至少 6 格石顶 → 不破地表、封闭黑暗）
    constexpr int kMineshaftMaxY    = 48;     // 矿井最高 y（spec「Y<50」；地下深处）
    constexpr int kTunnelLenMin     = 5;      // 巷道单段最短长度（段数；L 形两段各取 → 总长 10..20）
    constexpr int kTunnelLenMax     = 10;     // 巷道单段最长长度
    constexpr int kTunnelH          = 3;      // 巷道内部高度（空气层数；y=sy+1..sy+kTunnelH）
    constexpr int kRoomHalf         = 3;      // 中央交叉洞室半宽（7×7）
    constexpr int kRoomH            = 4;      // 洞室内部高度（比巷道高 1 → 洞穴感）
    constexpr int kFenceInterval    = 4;      // 木栅栏立柱间隔（每 N 段一对立柱）
    constexpr unsigned kCobwebPct   = 12u;    // 上角蛛网概率（每段每侧 ~12%）
    constexpr unsigned kOrePct      = 15u;    // 巷壁矿石概率（每段每侧 ~15%）
    constexpr unsigned kTorchPct    = 55u;    // 立柱顶火把概率（每对立柱 ~55% → 巷道沿途常见火把）
    constexpr int kMargin           = 16;     // 留边界（折线巷道两段 + 洞室半径 → 较大余量防越界）

    const int mineSeed = m_seed + 15047; // 矿井哈希偏移（与其它 worldgen hashColumn 解耦）
    int placed = 0;
    int chests = 0; // rv9-10：矿井宝箱计数（日志核对 > 0 —— 旧版轨行冲突恒 0 的回归检测）
    for (int bx = kMineshaftGrid / 2; bx < m_width; bx += kMineshaftGrid) {
        for (int bz = kMineshaftGrid / 2; bz < m_depth; bz += kMineshaftGrid) {
            const quint32 r = hashColumn(mineSeed, bx, bz);
            if ((r % 100u) >= kMinePct) continue; // 概率筛选
            const int span = kMineshaftGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            // 留 margin 边界（折线巷道沿两方向走 kTunnelLenMax×2 段 + 洞室半径 → 半径 ≤ kMargin 不越界）。
            if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
                continue;
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠矿井（避免与海水柱冲突）
            const int h = std::min(heightAt(cx, cz), m_height - 1);
            // 矿井 y 范围：基岩之上 ~ kMineshaftMaxY 之下；上方至少留 kSurfaceFloor 格石顶。
            const int yLo = kBedrockTop + 2;
            const int yHi = std::min(kMineshaftMaxY - kRoomH - 1, h - kSurfaceFloor - kRoomH - 1);
            if (yHi <= yLo) continue; // 此列地下空间不足 → 跳过
            const int yRange = yHi - yLo + 1;
            const int sy = yLo + int((r >> 9) & 0x1Fu) % yRange; // 地板 y（巷道底面 = 洞室地板）

            // t565 ⑤ 地板材质：按矿井 hash 二选一（Planks 旧观感 / Stone 石底变体）。全矿井统一。
            const quint8 floorBlock = ((r >> 20) & 1u) ? BlockRegistry::Stone : BlockRegistry::Planks;

            // t565 ④ 铁轨铺设记录：铺完统一算连接 state（直 / 拐角 / 十字形态）。
            std::vector<std::array<int, 3>> railCells;

            // 逐格铺地板 + 清空气（px,pz 列，地板 y0、内部空气 y0+1..y0+roomH；不动 Bedrock）。
            const auto carveCell = [&](int px, int pz, int y0, int roomH) {
                if (px < 0 || pz < 0 || px >= m_width || pz >= m_depth) return; // 防御（margin 已保证不越界）
                const quint8 fb0 = m_chunks.blockAt(px, y0, pz);
                if (fb0 != BlockRegistry::Bedrock)
                    m_chunks.setBlock(px, y0, pz, floorBlock);
                for (int dy = 1; dy <= roomH; ++dy) {
                    const int yy = y0 + dy;
                    if (yy >= m_height) break;
                    const quint8 ib = m_chunks.blockAt(px, yy, pz);
                    if (ib == BlockRegistry::Bedrock) continue;
                    m_chunks.setBlock(px, yy, pz, BlockRegistry::Air);
                }
            };

            // 1) 中央交叉洞室（7×7×kRoomH 空气 + 地板；t565 ①⑥「角落生成洞穴 / 连通」）。
            for (int dx = -kRoomHalf; dx <= kRoomHalf; ++dx)
                for (int dz = -kRoomHalf; dz <= kRoomHalf; ++dz)
                    carveCell(cx + dx, cz + dz, sy, kRoomH);
            // 洞室中央十字铁轨（两排贯通 → 与四向巷道轨衔接）。
            {
                const int yy = sy + 1;
                if (yy < m_height) {
                    for (int d = -kRoomHalf; d <= kRoomHalf; ++d) {
                        if (m_chunks.blockAt(cx + d, yy, cz) == BlockRegistry::Air) {
                            m_chunks.setBlock(cx + d, yy, cz, BlockRegistry::Rail);
                            railCells.push_back({cx + d, yy, cz});
                        }
                        if (m_chunks.blockAt(cx, yy, cz + d) == BlockRegistry::Air) {
                            m_chunks.setBlock(cx, yy, cz + d, BlockRegistry::Rail);
                            railCells.push_back({cx, yy, cz + d});
                        }
                    }
                }
            }
            // 洞室宝藏箱（机制同旧版末端箱；带 ChestStateMineshaftFlag → 首开填充矿井战利品）。
            //   旧版放 (cx±kRoomHalf-1, sy+1, cz) 恰在 X 向轨行上 → ib != Rail 守卫恒 false → 宝箱绝迹
            //   （rv9-10 复盘）。改放十字轨行外的四角落内点 (cx±2, cz±2)（7×7 洞室内、轨行间空区），
            //   四角按 hash 顺序试放，首个非轨 / 非基岩格落地。
            {
                const int yy = sy + 1;
                if (yy < m_height) {
                    const int cornerOff = int((r >> 21) & 3u); // 起始角（0..3）→ 同 seed 确定性
                    for (int t = 0; t < 4; ++t) {
                        const int sx2 = (((cornerOff + t) & 1u) != 0u) ? 2 : -2;
                        const int sz2 = (((cornerOff + t) & 2u) != 0u) ? 2 : -2;
                        const quint8 ib = m_chunks.blockAt(cx + sx2, yy, cz + sz2);
                        if (ib != BlockRegistry::Bedrock && ib != BlockRegistry::Rail) {
                            m_chunks.setBlock(cx + sx2, yy, cz + sz2, BlockRegistry::Chest,
                                              BlockRegistry::ChestStateMineshaftFlag);
                            ++chests;
                            break;
                        }
                    }
                }
            }

            // 2) 折线巷道（3..4 条，每条 = 主向 lenA 段 + ±90° 转向 lenB 段；t565 ①⑥「两边长条 + 角落连通」）。
            const int corridors = 3 + int((r >> 22) & 1u); // 3 或 4 条
            for (int ci = 0; ci < corridors; ++ci) {
                // 主向（4 水平主向，由 hash 高位 + 巷道序号混合选；机制等价 MC 矿井巷道多向延伸）。
                const int dirIdx = int(((r >> (24 + 2 * ci)) ^ (quint32(ci) * 0x9E37u)) & 3u);
                int dx = 0, dz = 0;
                switch (dirIdx) {
                case 0: dx =  1; dz =  0; break; // +X
                case 1: dx = -1; dz =  0; break; // -X
                case 2: dx =  0; dz =  1; break; // +Z
                case 3: dx =  0; dz = -1; break; // -Z
                }
                const quint32 rh = hashVoxel(mineSeed ^ (0xDEC0 + quint32(ci)), cx, sy, cz); // 巷道参数 hash
                const int lenA = kTunnelLenMin + int((rh >> 2) & 0xFu) % (kTunnelLenMax - kTunnelLenMin + 1);
                const int lenB = kTunnelLenMin + int((rh >> 6) & 0xFu) % (kTunnelLenMax - kTunnelLenMin + 1);
                const int turnSign = ((rh >> 10) & 1u) ? 1 : -1; // 转向 ±90°（左 / 右转）

                // 两段推进（段 A 主向 lenA 段 → 转向 → 段 B lenB 段）。每段 3 宽 × 3 高 + 地板。
                int legAx = cx, legAz = cz; // 段 A 末端游标（段 B 续接起点）
                for (int leg = 0; leg < 2; ++leg) {
                    const int legLen = (leg == 0) ? lenA : lenB;
                    if (leg == 1) {
                        // 转向 ±90°：(dx,dz) → (dz,-dx) × turnSign。
                        const int ndx = turnSign * dz, ndz = -turnSign * dx;
                        dx = ndx; dz = ndz;
                    }
                    // 起点：段 0 = 洞室边缘外一格（step0 即 carve 该格）；段 1 = 段 A 末端（先推进再 carve，免重复刻）。
                    int ax = (leg == 0) ? cx + dx * (kRoomHalf + 1) : legAx;
                    int az = (leg == 0) ? cz + dz * (kRoomHalf + 1) : legAz;
                    for (int step = 0; step < legLen; ++step) {
                        if (leg == 1 || step > 0) { ax += dx; az += dz; }
                        // 垂直宽度轴 perp = 方向旋转 90°：(perpX, perpZ) = (-dz, dx)；w ∈ {-1,0,+1} → 3 宽截面。
                        for (int w = -1; w <= 1; ++w)
                            carveCell(ax + w * (-dz), az + w * dx, sy, kTunnelH);
                        // 木栅栏立柱（每 kFenceInterval 段，w=±1 边缘，y=sy+1..sy+2 两格高立柱）。
                        if (step % kFenceInterval == 0) {
                            for (int w = -1; w <= 1; w += 2) { // w = -1, +1
                                const int px = ax + w * (-dz);
                                const int pz = az + w * dx;
                                for (int dy = 1; dy <= 2; ++dy) {
                                    const int yy = sy + dy;
                                    if (yy >= m_height) break;
                                    const quint8 ib = m_chunks.blockAt(px, yy, pz);
                                    if (ib == BlockRegistry::Bedrock) continue;
                                    m_chunks.setBlock(px, yy, pz, BlockRegistry::WoodFence);
                                }
                                // t565 ③ 立柱顶火把：~kTorchPct 概率在立柱正上（y=sy+3，立柱顶 sy+2 之上）置
                                //   Torch state=0（贴地形态立在柱顶；块光 flood14 照亮巷道 + 呈现层 enterWorld
                                //   扫描建伪光源 delegate）。柱顶在行走层之上、偏离铁轨中线 → 不与轨 / 箱冲突。
                                const int ty = sy + 3;
                                if (ty < m_height) {
                                    const quint32 th = hashVoxel(mineSeed ^ 0x70C4, px, ty, pz);
                                    if ((th % 100u) < kTorchPct
                                        && m_chunks.blockAt(px, ty, pz) == BlockRegistry::Air)
                                        m_chunks.setBlock(px, ty, pz, BlockRegistry::Torch, 0);
                                }
                            }
                        }
                        // t565 ④ 铁轨：w=0 中线每段连续铺（贴地板；仅空气格放 → 拐角 / 立柱等占用处自然跳过）。
                        {
                            const int yy = sy + 1;
                            if (yy < m_height && m_chunks.blockAt(ax, yy, az) == BlockRegistry::Air) {
                                m_chunks.setBlock(ax, yy, az, BlockRegistry::Rail);
                                railCells.push_back({ax, yy, az});
                            }
                        }
                        // 蜘蛛网（按 hashVoxel 概率，w=±1 上角 y=sy+kTunnelH；仅空气格放，防覆盖立柱顶端）。
                        for (int w = -1; w <= 1; w += 2) {
                            const int px = ax + w * (-dz);
                            const int pz = az + w * dx;
                            const int yy = sy + kTunnelH;
                            if (yy >= m_height) continue;
                            const quint32 wh = hashVoxel(mineSeed ^ 0xC0B, px, yy, pz);
                            if ((wh % 100u) >= kCobwebPct) continue;
                            const quint8 ib = m_chunks.blockAt(px, yy, pz);
                            if (ib == BlockRegistry::Bedrock) continue;
                            m_chunks.setBlock(px, yy, pz, BlockRegistry::Cobweb);
                        }
                        // 暴露矿石（按 hashVoxel 概率，w=±2 巷壁 y=sy+1..sy+kTunnelH；仅实体石类格置换）。
                        for (int w = -2; w <= 2; w += 4) { // w = -2, +2
                            const int px = ax + w * (-dz);
                            const int pz = az + w * dx;
                            for (int dy = 1; dy <= kTunnelH; ++dy) {
                                const int yy = sy + dy;
                                if (yy >= m_height) break;
                                const quint32 oh = hashVoxel(mineSeed ^ 0xCAFE, px, yy, pz);
                                if ((oh % 100u) >= kOrePct) continue;
                                const quint8 ib = m_chunks.blockAt(px, yy, pz);
                                // 仅在实体石类方块处置矿（不动 Bedrock / Air / 已放结构方块）。
                                if (ib != BlockRegistry::Stone && ib != BlockRegistry::Dirt) continue;
                                const quint8 ore = ((oh >> 8) & 1u) ? BlockRegistry::IronOre
                                                                    : BlockRegistry::CoalOre;
                                m_chunks.setBlock(px, yy, pz, ore);
                            }
                        }
                        if (leg == 0) { legAx = ax; legAz = az; } // 记段 A 末端（段 B 续接）
                    }
                }
            }

            // t565 ④ 铁轨连接统一重算（直 / 拐角 / 十字形态由邻轨互连自动得出；与运行期 checkRailOnEdit
            //   同一权威 BlockRegistry::railConnections）。worldgen 直写不 emit（generate 末尾统一 worldChanged）。
            //   t666/t667：连接计算器改三高探针签名 —— worldgen 矿井轨全同层（无坡度）→ 上 / 下置 Air；
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
            ++placed;
        }
    }
    qInfo() << "worldgen: underground mineshafts =" << placed
            << "chests =" << chests; // 同 seed → 同计数（确定性核对；chests>0 = 宝箱轨行冲突已修）
}

// t485 沙漠神殿（见 world.h 头注释）。机制等价 MC 1.0 沙漠神殿 desert temple：沙漠地表的阶梯金字塔 + 正下方地下
//   密室 + 4 宝藏箱 + 中央压力板下 TNT 陷阱（踩板引爆）。确定性散布（hashColumn + seed 偏移，PLAN §2-K）。
//
//   结构几何（中心 (cx,surfaceY,cz)，surfaceY = 沙漠沙顶 heightAt）：
//     A) 金字塔（阶梯砂岩，逐层缩成金字塔外形）：
//        - 8 层（layer 0..kPyramidH-1），layer L 位于 y=surfaceY+L，layer L 的水平半边 = kPyramidHalf - L。
//        - 每层填一层 Sandstone 实心盘（[-half, +half]² 范围内逐格置 Sandstone，覆盖沙顶 / 空气，不动 Bedrock）；
//          顶饰最高层（layer L==kPyramidH-1，半边=1 → 3×3）置 CutSandstone 区分顶冠（机制等价 MC 神殿顶部装饰）。
//        - 层叠加 → 阶梯金字塔外形（底 15×15，每升 1 层半边 -1，顶 3×3 CutSandstone 顶冠）。
//     B) 地下密室（金字塔正下方，封入地下）：
//        - 内部 7×7×4 空气（roomW=7 / roomH=4）；地板 y=floorY=surfaceY-kChamberDepth（kChamberDepth=7，深地下）；
//          内部空气 y∈[floorY+1 .. floorY+roomH]；天花板块 y=floorY+roomH+1=surfaceY-2。
//        - 周界（地板 / 天花板 / 四壁）填 Sandstone（沙漠成岩，机制等价 MC 神殿密室砂岩墙）→ 封闭无天光（黑暗，
//          spec「地下密室」）。不动 Bedrock。
//     C) 4 宝藏箱（密室四角，y=floorY+1 站立高度）：带 ChestStatePyramidFlag bit4 标记 → isPyramidChest 返 true →
//        Main.qml.openChest 首开填充 pyramidChestPool 战利品（钻石 / 金 / 青金石 / 骨头 / 腐肉等）。朝向低 2 位=0。
//     D) TNT 陷阱（密室中央地板）：3×3 TntBlock 位于 y=floorY（密室地板层，中央 3×3 替换砂岩地板为 TNT），
//        中央格正上方 y=floorY+1（站立层）置 CobblePressurePlate（沙漠石质主题）。玩家进入密室踩压力板 →
//        playercontroller tick 扫 footprint 格（压力板 + 下方 TNT）→ detonateTntBlock → destroySphereSilent
//        球形破坏（破坏方块 + 衰减伤玩家 + explosion 音/视，机制等价 MC 1.0 沙漠神殿踩板引爆 TNT）。
//
//   placeMineshaft 之后、fillWater 之前（仅 Desert 群系 → 与海 / 湖独立；fillWater 仅填海域低洼，沙漠内陆不被灌水）。
//   纯函数于 seed + biomeAt（经 hashColumn / hashVoxel）→ 同 seed 同神殿分布（PLAN §2-K）。仅扫候选沙漠格 → 不全图扫描。
void World::placeDesertTemple()
{
    constexpr int kTempleGrid     = 48;     // 候选网格间距（比矿井 36 更稀 → 神殿更稀有；spec「低频」）
    constexpr unsigned kTemplePct = 45u;    // 候选命中概率（仅沙漠候选 → 已天然稀有；45% 命中 → 沙漠中可见但不密集）
    constexpr int kPyramidHalf    = 7;      // 金字塔底半边（底 15×15 = (2*7+1)²）
    constexpr int kPyramidH       = 8;      // 金字塔层数（layer 0..7；顶冠 layer 7 半边=0 → 但取 min 1 保 3×3 顶冠）
    constexpr int kChamberDepth   = 7;      // 密室地板相对地表的深度（surfaceY-7；深地下、封入沙/石）
    constexpr int kRoomW          = 7;      // 密室内部宽度（X/Z 格子数；7×7 内部）
    constexpr int kRoomH          = 4;      // 密室内部高度（Y 空气层数）
    constexpr int kTntHalf        = 1;      // TNT 陷阱半边（3×3 = (2*1+1)²，置于密室地板中央）
    constexpr int kBedrockTop      = 4;      // 不动基岩顶（同 carveCaves / placeDungeons / placeMineshaft）
    // 留边界（金字塔底半边 7 + 密室半边 3 + 抖动余量 → 半径 ≤ 8 不越界）。
    constexpr int kMargin = (kPyramidHalf > kRoomW / 2 ? kPyramidHalf : kRoomW / 2) + 1;

    int placed = 0;
    const int templeSeed = m_seed + 19487; // 神殿哈希偏移（与其它 worldgen hashColumn 解耦）
    for (int bx = kTempleGrid / 2; bx < m_width; bx += kTempleGrid) {
        for (int bz = kTempleGrid / 2; bz < m_depth; bz += kTempleGrid) {
            const quint32 r = hashColumn(templeSeed, bx, bz);
            if ((r % 100u) >= kTemplePct) continue; // 概率筛选
            const int span = kTempleGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
                continue; // 留 margin 边界（金字塔 + 密室半径 ≤ margin 不越界）
            // 仅 Desert 群系（spec「沙漠群系生成」；biomeAt 收口单一权威）。非沙漠 → 跳过（不在草原 / 森林生神殿）。
            if (!isDesert(cx, cz)) continue;
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠神殿（避免与海水柱冲突）
            const int surfaceY = std::min(heightAt(cx, cz), m_height - 1);
            const int floorY = surfaceY - kChamberDepth;
            const int ceilBlockY = floorY + kRoomH + 1; // 天花板块 y（内部空气顶 + 1）
            if (floorY < kBedrockTop + 1) continue; // 密室地板太低（贴基岩）→ 跳过本候选（保墙 / 地板完整）
            if (ceilBlockY >= m_height) continue;   // 几何保护（surfaceY 异常高时防越界）
            for (int layer = 0; layer < kPyramidH; ++layer) {
                const int yy = surfaceY + layer;
                if (yy < 0 || yy >= m_height) continue;
                const int half = kPyramidHalf - layer;
                if (half < 1) break; // 金字塔已收顶（半边 ≤ 0）→ 上层不再画
                const bool topCap = (layer == kPyramidH - 1) || (half <= 1); // 顶冠 / 最小层用 CutSandstone 区分
                for (int dx = -half; dx <= half; ++dx) {
                    for (int dz = -half; dz <= half; ++dz) {
                        const int px = cx + dx, pz = cz + dz;
                        const quint8 cur = m_chunks.blockAt(px, yy, pz);
                        if (cur == BlockRegistry::Bedrock) continue; // 不动基岩
                        m_chunks.setBlock(px, yy, pz,
                                          topCap ? BlockRegistry::CutSandstone : BlockRegistry::Sandstone);
                    }
                }
            }

            // B) 地下密室（金字塔正下方）：地板 y=floorY，内部 7×7×4 空气，周界砂岩墙 / 地板 / 顶板。
            const int roomHalf = kRoomW / 2; // 3（内部 7×7 = [-3, +3]²）            // 周界填砂岩（地板 / 天花板 / 四壁）—— 遍历 [-roomHalf-1, roomHalf+1]³ 外圈，边界格置 Sandstone（不动 Bedrock）。
            for (int dy = -1; dy <= kRoomH; ++dy) {
                const int yy = floorY + dy;
                if (yy < 0 || yy >= m_height) continue;
                const bool yEdge = (dy == -1 || dy == kRoomH); // 地板（dy=-1）/ 天花板（dy=kRoomH）
                for (int dx = -roomHalf - 1; dx <= roomHalf + 1; ++dx) {
                    for (int dz = -roomHalf - 1; dz <= roomHalf + 1; ++dz) {
                        const bool xEdge = (dx == -roomHalf - 1 || dx == roomHalf + 1);
                        const bool zEdge = (dz == -roomHalf - 1 || dz == roomHalf + 1);
                        if (!yEdge && !xEdge && !zEdge) continue; // 内部格由下一步清空气
                        const int px = cx + dx, pz = cz + dz;
                        const quint8 cur = m_chunks.blockAt(px, yy, pz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(px, yy, pz, BlockRegistry::Sandstone);
                    }
                }
            }
            // 内部清空气（7×7×4，覆盖原沙 / 石 / 矿 → 干净密室；不动 Bedrock）。
            for (int dy = 0; dy < kRoomH; ++dy) {
                const int yy = floorY + dy;
                if (yy < 0 || yy >= m_height) continue;
                for (int dx = -roomHalf; dx <= roomHalf; ++dx) {
                    for (int dz = -roomHalf; dz <= roomHalf; ++dz) {
                        const quint8 cur = m_chunks.blockAt(cx + dx, yy, cz + dz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(cx + dx, yy, cz + dz, BlockRegistry::Air);
                    }
                }
            }

            // C) 4 宝藏箱（密室四角，y=floorY+1 站立高度）：带 ChestStatePyramidFlag 标记 → 首开填充神殿战利品。
            //    四角 = 内部 [-roomHalf, -roomHalf] / [+roomHalf, -roomHalf] / [-roomHalf, +roomHalf] / [+roomHalf, +roomHalf]。
            const int chestY = floorY + 1;
            const int cornerOff[4][2] = {{-roomHalf, -roomHalf}, {roomHalf, -roomHalf},
                                          {-roomHalf, roomHalf}, {roomHalf, roomHalf}};
            for (const auto &c : cornerOff) {
                const int px = cx + c[0], pz = cz + c[1];
                if (chestY < m_height) {
                    const quint8 cur = m_chunks.blockAt(px, chestY, pz);
                    if (cur != BlockRegistry::Bedrock) // 不动基岩（防御）
                        m_chunks.setBlock(px, chestY, pz, BlockRegistry::Chest,
                                          BlockRegistry::ChestStatePyramidFlag);
                }
            }

            // D) TNT 陷阱（密室中央地板）：3×3 TntBlock 于 y=floorY（替换砂岩地板），中央格上方 y=floorY+1 置压力板。
            //    玩家踩压力板 → playercontroller 扫 footprint（压力板 + 下方 TNT）→ detonateTntBlock 球形破坏。
            for (int dx = -kTntHalf; dx <= kTntHalf; ++dx) {
                for (int dz = -kTntHalf; dz <= kTntHalf; ++dz) {
                    const int px = cx + dx, pz = cz + dz;
                    const quint8 cur = m_chunks.blockAt(px, floorY, pz);
                    if (cur != BlockRegistry::Bedrock)
                        m_chunks.setBlock(px, floorY, pz, BlockRegistry::TntBlock);
                }
            }
            // 中央压力板（CobblePressurePlate，沙漠石质主题；圆石压力板区别于木质，更贴合神殿石质风）。
            if (chestY < m_height) {
                const quint8 cur = m_chunks.blockAt(cx, chestY, cz);
                if (cur == BlockRegistry::Air) // 仅空气格放（防覆盖已放宝藏箱 / TNT）
                    m_chunks.setBlock(cx, chestY, cz, BlockRegistry::CobblePressurePlate);
            }
            ++placed;
        }
    }
    qInfo() << "worldgen: desert temples =" << placed; // 同 seed → 同计数（确定性核对）
}

// t486 丛林神殿（见 world.h 头注释）。机制等价 MC 1.0 丛林神殿 jungle temple：丛林地表的苔石建筑 + 内部走廊 +
//   发射器陷阱（踩压力板 → 邻接发射器射箭，无红石用 dispenser 直接触发）+ 宝藏箱。确定性散布
//   （hashColumn + seed 偏移，PLAN §2-K）。
//
//   结构几何（中心 (cx,surfaceY,cz)，surfaceY = 丛林草顶 heightAt；floorY = surfaceY，建筑坐于地表）：
//     A) 苔石平台地板（floorY）：[-half, +half]² × 1 层 MossyCobble（覆盖草 / 土 / 石，不动 Bedrock）→
//        建筑坐于平整苔石基座（spec「苔石建筑」）。
//     B) 苔石围墙 + 天花板：围墙 = 外圈 (|dx|==half 或 |dz|==half) y∈[floorY+1 .. floorY+3]（3 高）MossyCobble；
//        +X 墙中央 (dz=0) 留 2 高入口（floorY+1/floorY+2 不放墙 → 玩家可走入）；天花板 = y=floorY+4 全 [-half,+half]²
//        MossyCobble（封顶无天光 → 内部黑暗，机制等价 MC 神殿阴暗环境）。不动 Bedrock。
//     C) 内部空气（floorY+1 .. floorY+3 × [-half+1, +half-1]²）：清空气成 9×9×3 走廊（覆盖原土/石 → 干净室内）。
//        + 顶上一格 (floorY+5) 清空气 → 屋顶不被地表 / 树叶埋（肉眼可见苔石顶）。
//     D) 发射器陷阱（走廊两侧石壁嵌 Dispenser + 走廊地板 CobblePressurePlate）：
//        - Dispenser 嵌入 ±Z 围墙（dz=±half，替换底部墙块 y=floorY+1），朝走廊中央（±Z 侧分别朝 ∓Z）；
//          state 编码朝向（+Z 侧 dispenser 朝 -Z = state 3 / -Z 侧 dispenser 朝 +Z = state 2，同 chest/furnace 编码）。
//        - CobblePressurePlate 置 Dispenser 朝向的相邻走廊格（dz=±(half-1)）y=floorY+1（玩家踩板 →
//          playercontroller scanDispenserTraps 扫 footprint 查压力板的 4 水平邻格之一 == Dispenser → spawnArrow
//          朝压力板方向射箭，机制等价 MC 1.0 丛林神殿发射器陷阱；无红石故「踩板直接触发」）。
//        - 走廊纵深放 2 组（dx=-1 / dx=+1），玩家走入触发两次 → 多波箭雨（机制等价 MC 丛林神殿多发射器）。
//     E) 宝藏箱（走廊尽头 -X 端 (dx=-(half-1), dz=0)，y=floorY+1）：带 ChestStateJungleFlag bit5 标记 →
//        isJungleTempleChest 返 true → Main.qml.openChest 首开填充 jungleTempleChestPool 战利品（骨头 / 腐肉 /
//        铁 / 金 / 钻石 / 箭 / 附魔书等）。
//
//   placeDesertTemple 之后、fillWater 之前（仅 Jungle 群系 → 与海 / 湖独立；fillWater 仅填海域低洼，丛林内陆不被
//   灌水）。纯函数于 seed + biomeAt（经 hashColumn / hashVoxel）→ 同 seed 同神殿分布（PLAN §2-K）。仅扫候选丛林
//   格 → 不全图扫描。
void World::placeJungleTemple()
{
    constexpr int kTempleGrid     = 40;     // 候选网格间距（略密于沙漠神殿 48 → 丛林群系本身较稀有，补偿密度使神殿可被发现）
    constexpr unsigned kTemplePct = 50u;    // 候选命中概率（仅丛林候选 → 已天然稀有；50% 命中 → 160×160 世界约 1-2 座神殿，spec「低频」）
    constexpr int kHalf           = 5;      // 建筑外圈半边（11×11 = (2*5+1)² 外圈；9×9 内部 = (2*4+1)²）
    constexpr int kWallH          = 3;      // 围墙高度（内部空气层数 y∈[floorY+1 .. floorY+3]）
    constexpr int kBedrockTop     = 4;      // 不动基岩顶（同 carveCaves / placeDungeons / placeMineshaft / placeDesertTemple）
    // 留边界（外圈半边 5 + 抖动余量 → 半径 ≤ 6 不越界）。
    constexpr int kMargin = kHalf + 1;

    int placed = 0;
    const int templeSeed = m_seed + 22617; // 丛林神殿哈希偏移（与其它 worldgen hashColumn 解耦）
    for (int bx = kTempleGrid / 2; bx < m_width; bx += kTempleGrid) {
        for (int bz = kTempleGrid / 2; bz < m_depth; bz += kTempleGrid) {
            const quint32 r = hashColumn(templeSeed, bx, bz);
            if ((r % 100u) >= kTemplePct) continue; // 概率筛选
            const int span = kTempleGrid / 2;
            const int jx = int((r >> 1) & 0xFu) % (span + 1) - span / 2;
            const int jz = int((r >> 5) & 0xFu) % (span + 1) - span / 2;
            const int cx = bx + jx, cz = bz + jz;
            if (cx < kMargin || cz < kMargin || cx >= m_width - kMargin || cz >= m_depth - kMargin)
                continue; // 留 margin 边界（建筑半径 ≤ margin 不越界）
            // 仅 Jungle 群系（spec「丛林群系生成」；biomeAt 收口单一权威）。非丛林 → 跳过（不在草原 / 森林 / 沙漠生神殿）。
            if (biomeAt(cx, cz) != Biome::Jungle) continue;
            if (seaColumnHeight(cx, cz) >= 0) continue; // 海域不叠神殿（避免与海水柱冲突）
            const int surfaceY = std::min(heightAt(cx, cz), m_height - 1);
            const int floorY = surfaceY;            // 苔石地板 = 地表草顶（建筑坐于地表）
            const int ceilY = floorY + kWallH + 1;  // 天花板 y（内部空气顶 + 1 = floorY+4）
            if (floorY < kBedrockTop + 1) continue; // 地板太低（贴基岩）→ 跳过
            if (ceilY >= m_height - 1) continue;    // 几何保护（surfaceY 异常高时防越界，留 1 格顶上清空气）

            // A) 苔石平台地板（floorY）：[-half, +half]² × 1 层 MossyCobble（不动 Bedrock）。
            for (int dx = -kHalf; dx <= kHalf; ++dx) {
                for (int dz = -kHalf; dz <= kHalf; ++dz) {
                    const int px = cx + dx, pz = cz + dz;
                    const quint8 cur = m_chunks.blockAt(px, floorY, pz);
                    if (cur == BlockRegistry::Bedrock) continue;
                    m_chunks.setBlock(px, floorY, pz, BlockRegistry::MossyCobble);
                }
            }

            // B) 苔石围墙（外圈 y∈[floorY+1 .. floorY+3]）+ 天花板（y=ceilY）。
            //    +X 墙中央 (dz=0) 留 2 高入口（floorY+1/floorY+2 不放墙 → 玩家可走入，机制等价 MC 神殿入口）。
            for (int dy = 1; dy <= kWallH; ++dy) {
                const int yy = floorY + dy;
                if (yy >= m_height) break;
                for (int d = -kHalf; d <= kHalf; ++d) {
                    // -X / +X 墙（dx=±half），整列 dz；+X 墙 dz=0 留入口（dy<=2 不放）。
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        const int px = cx + sgn * kHalf;
                        if (sgn == 1 && d == 0 && dy <= 2) continue; // +X 墙 dz=0 入口（2 高）
                        const quint8 cur = m_chunks.blockAt(px, yy, cz + d);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(px, yy, cz + d, BlockRegistry::MossyCobble);
                    }
                    // -Z / +Z 墙（dz=±half），整行 dx。
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        const int pz = cz + sgn * kHalf;
                        const quint8 cur = m_chunks.blockAt(cx + d, yy, pz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(cx + d, yy, pz, BlockRegistry::MossyCobble);
                    }
                }
            }
            // 天花板（y=ceilY，全 [-half,+half]² MossyCobble，封顶无天光）。
            if (ceilY < m_height) {
                for (int dx = -kHalf; dx <= kHalf; ++dx) {
                    for (int dz = -kHalf; dz <= kHalf; ++dz) {
                        const quint8 cur = m_chunks.blockAt(cx + dx, ceilY, cz + dz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(cx + dx, ceilY, cz + dz, BlockRegistry::MossyCobble);
                    }
                }
            }

            // C) 内部空气（floorY+1 .. floorY+3 × [-half+1, +half-1]² = 9×9×3 走廊）+ 顶上 1 格清空气（屋顶可见）。
            for (int dy = 1; dy <= kWallH; ++dy) {
                const int yy = floorY + dy;
                if (yy >= m_height) break;
                for (int dx = -(kHalf - 1); dx <= (kHalf - 1); ++dx) {
                    for (int dz = -(kHalf - 1); dz <= (kHalf - 1); ++dz) {
                        const quint8 cur = m_chunks.blockAt(cx + dx, yy, cz + dz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(cx + dx, yy, cz + dz, BlockRegistry::Air);
                    }
                }
            }
            if (ceilY + 1 < m_height) {
                for (int dx = -kHalf; dx <= kHalf; ++dx) {
                    for (int dz = -kHalf; dz <= kHalf; ++dz) {
                        const quint8 cur = m_chunks.blockAt(cx + dx, ceilY + 1, cz + dz);
                        if (cur == BlockRegistry::Bedrock) continue;
                        m_chunks.setBlock(cx + dx, ceilY + 1, cz + dz, BlockRegistry::Air);
                    }
                }
            }

            // D) 发射器陷阱：走廊纵深 2 组（dx=-1 / dx=+1），每组 ±Z 两侧各一 Dispenser（嵌 ±Z 围墙 dz=±half，
            //    y=floorY+1）+ 走廊地板 CobblePressurePlate（dz=±(half-1)，y=floorY+1，dispenser 朝向相邻格）。
            //    dispenser state：+Z 侧（dz=+half）朝 -Z（state 3）/ -Z 侧（dz=-half）朝 +Z（state 2）
            //    （chestFrontFace 编码 0=+X 1=-X 2=+Z 3=-Z）。
            const int trapY = floorY + 1;
            for (int tdx : {-1, 1}) {
                // -Z 侧 dispenser（dz=-half）朝 +Z（state 2）+ 压力板（dz=-(half-1)）。
                {
                    const int px = cx + tdx, pz = cz - kHalf;
                    if (trapY < m_height) {
                        const quint8 cur = m_chunks.blockAt(px, trapY, pz);
                        if (cur != BlockRegistry::Bedrock)
                            m_chunks.setBlock(px, trapY, pz, BlockRegistry::Dispenser, /*state*/ 2);
                    }
                    const int ppz = cz - (kHalf - 1);
                    if (trapY < m_height) {
                        const quint8 cur = m_chunks.blockAt(px, trapY, ppz);
                        if (cur == BlockRegistry::Air) // 仅空气格放（防覆盖已放宝藏箱）
                            m_chunks.setBlock(px, trapY, ppz, BlockRegistry::CobblePressurePlate);
                    }
                }
                // +Z 侧 dispenser（dz=+half）朝 -Z（state 3）+ 压力板（dz=+(half-1)）。
                {
                    const int px = cx + tdx, pz = cz + kHalf;
                    if (trapY < m_height) {
                        const quint8 cur = m_chunks.blockAt(px, trapY, pz);
                        if (cur != BlockRegistry::Bedrock)
                            m_chunks.setBlock(px, trapY, pz, BlockRegistry::Dispenser, /*state*/ 3);
                    }
                    const int ppz = cz + (kHalf - 1);
                    if (trapY < m_height) {
                        const quint8 cur = m_chunks.blockAt(px, trapY, ppz);
                        if (cur == BlockRegistry::Air)
                            m_chunks.setBlock(px, trapY, ppz, BlockRegistry::CobblePressurePlate);
                    }
                }
            }

            // E) 宝藏箱（走廊尽头 -X 端 dx=-(half-1), dz=0，y=floorY+1）：带 ChestStateJungleFlag 标记 → 首开填充
            //    丛林神殿战利品。朝向低 2 位 = 0（chestFrontFace 兜底 NegZ；worldgen 不关心箱子朝向）。
            {
                const int px = cx - (kHalf - 1), pz = cz;
                if (trapY < m_height) {
                    const quint8 cur = m_chunks.blockAt(px, trapY, pz);
                    if (cur != BlockRegistry::Bedrock) // 不动基岩（防御）
                        m_chunks.setBlock(px, trapY, pz, BlockRegistry::Chest,
                                          BlockRegistry::ChestStateJungleFlag);
                }
            }
            ++placed;
        }
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
//       SpawnerStateSilverfishFlag → tickSpawners 刷 Silverfish）+ 战利品箱（Chest + ChestStateStrongholdFlag
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
    constexpr int kHalf            = 22;    // 建筑外圈半边（45×45 = (2*22+1)² 外圈；t713 扩建自 21×21 约 4.5× 面积）
    constexpr int kMargin          = kHalf + 1; // 留边界（外圈半边 22 + 抖动余量 → 半径 ≤ 23 不越界）
    constexpr int kWallH           = 8;     // 墙体高度层数（dy 1..8；地板 dy=0 / 顶板 dy=9 → 净高 8 格）。t759
                                            //   5→8：传送门房净空修复 —— 框架立于高台顶 dy=4，旧净高 5 时框架
                                            //   之上仅 1 格 Air（玩家 1.8 高无法跨过框架环走进环中心）；整体加高 3
                                            //   后框架顶之上 4 格 Air ≥ 验收「至少 3 格」。取「顶板上移 + 墙加高」
                                            //   全局方案而非仅传送门房局部抬顶：框架层 dy=4 与全部相对坐标零变化
                                            //   （B5 读档反推 / m_strongholdPortalY 记录值零回归），且避免局部抬顶
                                            //   在新旧顶板交界处留「天然地形邻接面」（洞穴恰过即漏光破封闭黑暗）。
                                            //   入口走廊 / 北走廊 / 楼梯同为内部空间 → 净高同步 8（同步检查通过）。
                                            //   worldgen 常量改动仅新世界生效（旧存档体素不回填，B5 反推自适应）。

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

        // ── t713 要塞结构扩建（自 t665 的 21×21 走廊+房间布局放大约 4.5× 面积；45×45 水平足迹，竖直
        //   dy ∈ [0,6] 地板/墙/顶板；各房间 / 走廊同步放大 + 新增东北 / 东南储藏龛）：
        //     中央大厅（13×13）← 东入口走廊（5 宽×15 深）┐ + 东北 / 东南储藏龛（7×5 各带 1 宽门洞）
        //                          ← 西走廊（5 宽×5 深）→ 图书馆（10×17 大房：书架墙 + 中央书架岛）
        //                          ← 北走廊（5 宽×5 深）→ 传送门房（25×10：岩浆河 + 高台 + 楼梯 + 12 框架环）
        //                          ← 南门洞（3 宽）→ 战利品/银鱼房（17×11：刷怪笼 + 宝箱 + 蛛网）
        //   房间 / 走廊 = 内部空间（dy 1..8 清 Air，玩家可走；t759 墙高 5→8）；其余 = StoneBrick 墙（hash 变体：
        //   ~10% MossyCobble 苔石砖观感 / ~8% MonsterEgg 怪物蛋嵌墙）。确定性（hashVoxel + strongSeed，
        //   同 seed 同结构同散布，PLAN §2-K）。
        // 1) 地板（dy=0）+ 顶板（dy=kWallH+1=9）：全幅 StoneBrick（封闭黑暗）。
        for (int dx = -kHalf; dx <= kHalf; ++dx) {
            for (int dz = -kHalf; dz <= kHalf; ++dz) {
                put(dx, 0, dz, BlockRegistry::StoneBrick);
                put(dx, kWallH + 1, dz, BlockRegistry::StoneBrick);
            }
        }

        // 内部空间谓词（走廊 / 房间内部 → 清 Air；非内部 → 墙）。中央大厅 + 四向走廊 + 四设施房 + 两储藏龛。
        //   t713 各矩形已做静态推演核对（tools 侧 Python 同款谓词 flood-fill 连通性 = 980 内部格全连通）。
        auto insideSpace = [](int dx, int dz) {
            if (dx >= -6 && dx <= 6 && dz >= -6 && dz <= 6) return true;       // 中央大厅（13×13）
            if (dx >= 7 && dx <= 21 && dz >= -2 && dz <= 2) return true;       // 东入口走廊（5 宽×15 深）
            if (dx >= 12 && dx <= 18 && dz >= -8 && dz <= -4) return true;     // 东北储藏龛（7×5）
            if (dx >= 14 && dx <= 16 && dz == -3) return true;                 // 东北龛门洞（3 宽）
            if (dx >= 12 && dx <= 18 && dz >= 4 && dz <= 8) return true;       // 东南储藏龛（7×5）
            if (dx >= 14 && dx <= 16 && dz == 3) return true;                  // 东南龛门洞（3 宽）
            if (dx >= -2 && dx <= 2 && dz >= -11 && dz <= -7) return true;     // 北走廊（5 宽×5 深）
            if (dx >= -12 && dx <= 12 && dz >= -21 && dz <= -12) return true;  // 传送门房内部（25×10）
            if (dx >= -1 && dx <= 1 && dz == 7) return true;                   // 南门洞（3 宽）
            if (dx >= -8 && dx <= 8 && dz >= 8 && dz <= 18) return true;       // 战利品/银鱼房内部（17×11）
            if (dx >= -11 && dx <= -7 && dz >= -2 && dz <= 2) return true;     // 西走廊（5 宽×5 深）
            if (dx >= -21 && dx <= -12 && dz >= -8 && dz <= 8) return true;    // 图书馆内部（10×17）
            return false;
        };

        // 2) 墙体 + 内部空间（y 1..8）：非内部格 = 石砖（hash 变体 MossyCobble 苔石 / MonsterEgg 怪物蛋嵌墙，
        //    机制等价 MC 要塞石砖墙混嵌怪物蛋；确定性 → 同 seed 同墙同蛋）；内部格 = 清 Air（玩家可走入）。
        for (int dy = 1; dy <= kWallH; ++dy) {
            for (int dx = -kHalf; dx <= kHalf; ++dx) {
                for (int dz = -kHalf; dz <= kHalf; ++dz) {
                    if (insideSpace(dx, dz)) {
                        put(dx, dy, dz, BlockRegistry::Air); // 走廊 / 房间内部
                        continue;
                    }
                    // 墙格：确定性变体（hashVoxel 高 8 位分流 —— 苔石砖 ~10% / 怪物蛋 ~8% / 石砖余下）。
                    const quint32 wh = hashVoxel(strongSeed ^ 0x665, cx + dx, cy + dy, cz + dz);
                    if ((wh % 100u) < 8u)
                        put(dx, dy, dz, BlockRegistry::MonsterEgg);    // 怪物蛋嵌墙（瞬破出蠹虫）
                    else if ((wh % 100u) < 18u)
                        put(dx, dy, dz, BlockRegistry::MossyCobble);   // 苔石砖变体（石砖同族观感）
                    else
                        put(dx, dy, dz, BlockRegistry::StoneBrick);
                }
            }
        }

        // 3) 中央大厅四角承重柱（(±3,±3)，dy 1..8 随 kWallH 撑到新顶）：石砖实心柱撑顶（扩厅观感 + 结构叙事；
        //    柱距内部边 3 格 → 不挡四向走廊动线，仅大厅中心 7×7 仍开阔）。
        for (int pxIdx = -1; pxIdx <= 1; pxIdx += 2) {
            for (int pzIdx = -1; pzIdx <= 1; pzIdx += 2) {
                for (int dy = 1; dy <= kWallH; ++dy)
                    put(pxIdx * 3, dy, pzIdx * 3, BlockRegistry::StoneBrick);
            }
        }

        // 4) **传送门房**（x -12..12, z -21..-12 内部 25×10）—— 岩浆河 + 石砖高台 + 3 级楼梯 + 12 框架环 +
        //    3×3 岩浆盆 + 银鱼刷怪笼（机制等价 MC 1.0 传送门房：熔岩池环绕 + 石砖台 + 框架环 + 台上银鱼笼；
        //    t664 框架/门面机制接线；t713 修「平台太小站不下 / 岩浆位置错 / 楼梯悬浮挡路」）：
        //    （t759 净空修复：框架层 dy=4 之上 dy 5..8 全 Air —— 框架顶以上 4 格通行空间，玩家 1.8 高可跨过
        //    框架环走进环中心（旧净高 5 时仅 1 格，跨不过去）；北走廊 / 东入口走廊 / 楼梯同为内部空间随墙高
        //    同步净高 8，同步检查通行无局促）
        //    a) **石砖高台**：x -6..6 × z -21..-16（13×6 footprint），dy 1..3 实心石砖 → 顶面 y=4 可站立
        //       （13×6 台面远大于旧 5×1 单排，12 框架环 + 内圈 3×3 全部落在台上，玩家上台自如）；
        //    b) **3×3 岩浆盆**：环内圈（x -1..1, z -19..-17）在 dy=3 层挖槽灌岩浆 —— 恰在门面（框架环激活后
        //       tryOpenEndPortal 在 dy=4 生成 3×3 门面）**正下方一格**（修旧版岩浆在 dy=0 与平台错位）；
        //       盆底 dy=2 仍是实心石砖托底（岩浆不下漏）；
        //    c) **12 框架环**：中心 (0, 4, -18)，**标准 ±2 方形环**（与 endPortalRingComplete / tryOpenEndPortal
        //       的环几何完全一致 —— review-r19.8 H2 修：旧放 5×3 矩形使完整性检查恒 false）。环 = 四边各 3 格
        //       不含四角：x=±2 排 z∈{-19,-18,-17}；z=-20/-16 排 x∈{-1,0,1}。**预置眼睛**：约 10% 框架初始
        //       state 激活（EndPortalStateActiveFlag，机制等价 MC 1.0 约 10% 框架自带眼睛；确定性 hashVoxel）；
        //    d) **3 级石砖楼梯**：中轴 3 宽（x -1..1），dz -15..-13 三块 StoneBrickStairs 逐级 dy 3..1、
        //       state=2（朝 +Z 开 → 背墙在 -Z 侧 / 整步在南半 —— 玩家自南（dz 大）向北走上台阶）。步行序列
        //       地面 y=1 → 南半步 1.5 → 北半 2.0 → 2.5 → 3.0 → 3.5 → 4.0（高台顶）：每步 Δ0.5 ≤ 玩家
        //       auto-step 0.55 → 可步行登台（修旧版楼梯悬浮挡路）。**台下实心填充**（dz -15 填 dy 1..2、
        //       dz -14 填 dy 1、dz -13 天然地面）→ 楼梯不悬浮；
        //    e) **环沟岩浆河**：高台外圈 dz -21..-16（dx -12..12 减高台减楼梯 footprint）dy=0 灌岩浆 +
        //       高台南侧 dz=-15 行（高台外 dx ±7..±12）置石砖栏（防岩浆南漫入房间地面）—— 河被高台（实心）/
        //       石砖栏 / 北墙三面围死 → 静态无 air 邻（worldgen 后 tickLavaFlow 稳态不流，机制等价 MC 传送门
        //       房熔岩环沟）；岩浆与高台侧壁接触（高台石砖非木质）不触发焚毁；
        //    f) **银鱼刷怪笼**：环中心正上方 (0, 5, -18)（高台顶 y=4 之上 1 格）—— 机制等价 MC 1.0 要塞
        //       传送门房楼梯尽头 / 传送门上方的 silverfish spawner；SpawnerStateSilverfishFlag → tickSpawners
        //       刷 Silverfish（复用 t487 既有机制）。
        // 4a) 高台（13×6 实心，dy 1..3）。
        for (int dx = -6; dx <= 6; ++dx) {
            for (int dz = -21; dz <= -16; ++dz) {
                for (int dy = 1; dy <= 3; ++dy)
                    put(dx, dy, dz, BlockRegistry::StoneBrick);
            }
        }
        // 4d) 楼梯 + 台下填充（先放填充 / 楼梯，后挖岩浆盆 → 盆仅在内圈，无交叠）。
        for (int dx = -1; dx <= 1; ++dx) {
            // 楼梯三块（dz -15 dy3 / dz -14 dy2 / dz -13 dy1；state 2 = 朝 +Z 开）。
            put(dx, 3, -15, BlockRegistry::StoneBrickStairs, 2);
            put(dx, 2, -14, BlockRegistry::StoneBrickStairs, 2);
            put(dx, 1, -13, BlockRegistry::StoneBrickStairs, 2);
            // 台下实心填充（防悬浮）：dz -15 填 dy 1..2、dz -14 填 dy 1。
            put(dx, 1, -15, BlockRegistry::StoneBrick);
            put(dx, 2, -15, BlockRegistry::StoneBrick);
            put(dx, 1, -14, BlockRegistry::StoneBrick);
        }
        // 4e) 环沟岩浆河 + 南侧石砖栏（dy=0）。
        for (int dx = -12; dx <= 12; ++dx) {
            for (int dz = -21; dz <= -16; ++dz) {
                if (dx >= -6 && dx <= 6) continue;                 // 高台 footprint（dy=0 处已是地板石砖）
                if (dz >= -15) continue;                            // 仅 dz -21..-16 环沟带
                put(dx, 0, dz, BlockRegistry::Lava);                // 岩浆河（高台外围一圈）
            }
        }
        for (int dx = -12; dx <= 12; ++dx) {                        // 南侧石砖栏（dz=-15 高台外）
            if (dx >= -1 && dx <= 1) continue;                      // 楼梯 footprint（楼梯块自身实体）
            if (dx >= -6 && dx <= 6) continue;                      // 高台南沿（实心）
            put(dx, 0, -15, BlockRegistry::StoneBrick);
        }
        // 4b) 3×3 岩浆盆（dy=3 高台内挖槽灌岩浆；门面正下方一格）。
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -19; dz <= -17; ++dz)
                put(dx, 3, dz, BlockRegistry::Lava);
        }
        // 4c) 12 框架环（中心 (0, 4, -18)，标准 ±2 方形环）+ 预置眼睛（~10%）。
        for (int pdx = -2; pdx <= 2; ++pdx) {
            for (int pdz = -20; pdz <= -16; ++pdz) {
                // 方形环 12 格（四边各 3，不含四角）：x=±2 时 z∈{-19,-18,-17}；z=-20/-16 时 x∈{-1,0,1}。
                const bool onRingExact = (pdx == -2 || pdx == 2) ? (pdz >= -19 && pdz <= -17)
                                                                 : (pdz == -20 || pdz == -16) && (pdx >= -1 && pdx <= 1);
                if (!onRingExact) continue;
                // 预置眼睛（~10%）：确定性 hash → 该框架初始激活（无需玩家插眼；机制等价 MC 约 1/10 预置）。
                const quint32 eh = hashVoxel(strongSeed ^ 0x664, cx + pdx, cy + 4, cz + pdz);
                const quint8 st = (eh % 100u) < 10u ? BlockRegistry::EndPortalStateActiveFlag : 0u;
                put(pdx, 4, pdz, BlockRegistry::EndPortal, st); // 末地传送门框架（state 0=未放 / bit0=已放眼）
            }
        }
        // 4f) 银鱼刷怪笼（环中心正上方，高台顶 y=4 之上 1 格）。
        put(0, 5, -18, BlockRegistry::Spawner, BlockRegistry::SpawnerStateSilverfishFlag);

        // 5) 图书馆（x -21..-12, z -8..8 房间；内部 10×17）—— 书架墙（机制等价 MC 1.0 要塞图书馆：
        //    书架贴墙排布，附魔台加成来源 t474）+ 中央书架岛 + 蛛网装饰。书架替换房间内壁（东墙内壁 x=-12 +
        //    西墙内壁 x=-13 列、南北墙内壁 z=∓7 / ±7 行，dy 1..3 —— t713 书架墙从 2 高升 3 高配大厅净高）。
        for (int dy = 1; dy <= 3; ++dy) {
            for (int d = -7; d <= 7; ++d) {
                put(-12, dy, d, BlockRegistry::Bookshelf);    // 东墙内壁书架列（面向房间）
                put(-13, dy, d, BlockRegistry::Bookshelf);    // 西墙内壁书架列（x=-21 墙内侧一排）
            }
            for (int d = -20; d <= -13; ++d) {
                put(d, dy, -7, BlockRegistry::Bookshelf);     // 南墙内壁书架行
                put(d, dy, 7, BlockRegistry::Bookshelf);      // 北墙内壁书架行
            }
        }
        // 中央书架岛（x -18..-15 × z -3..3，dy 1..2 两层 + 顶层 dy=3 中央一排）—— 大房中景 + 环岛走道。
        for (int dx = -18; dx <= -15; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                put(dx, 1, dz, BlockRegistry::Bookshelf);
                put(dx, 2, dz, BlockRegistry::Bookshelf);
            }
        }
        for (int dx = -17; dx <= -16; ++dx)
            put(dx, 3, 0, BlockRegistry::Bookshelf); // 岛顶中央点缀（十字顶饰）
        // 图书馆中央蛛网 + 阶梯装饰（确定性；同 seed 同分布）。
        {
            const quint32 wh = hashVoxel(strongSeed ^ 0x665, cx - 10, cy + 1, cz - 1);
            if ((wh % 100u) < 50u)
                put(-10, 1, -1, BlockRegistry::Cobweb);
            put(-10, 1, 0, BlockRegistry::StoneBrickSlab);
        }

        // 6) 银鱼刷怪笼 + 战利品箱（南房，x -8..8, z 8..18）：双刷怪笼（房间放大 → 两笼错位散布）+ 双宝箱
        //    （tickSpawners 据 flag 刷 Silverfish，机制等价 MC 1.0 要塞银鱼刷怪笼）+ 宝箱靠角
        //    （ChestStateStrongholdFlag → 首开填要塞战利品含末影之眼，激活传送门关键物品）。
        put(-4, 1, 12, BlockRegistry::Spawner, BlockRegistry::SpawnerStateSilverfishFlag);
        put(4, 1, 15, BlockRegistry::Spawner, BlockRegistry::SpawnerStateSilverfishFlag);
        put(6, 1, 17, BlockRegistry::Chest, BlockRegistry::ChestStateStrongholdFlag);
        put(-6, 1, 17, BlockRegistry::Chest, BlockRegistry::ChestStateStrongholdFlag);
        // 南房中央蛛网（阴湿地牢氛围；确定性）。
        {
            const quint32 wh = hashVoxel(strongSeed ^ 0x665, cx + 0, cy + 1, cz + 12);
            if ((wh % 100u) < 50u)
                put(-1, 1, 12, BlockRegistry::Cobweb);
        }

        // 7) 走廊蛛网装饰（机制等价 MC 要塞走廊的残破感）：内部空间内确定性散布 Cobweb（~8%，**仅 dy=1
        //    贴地**——不悬空；t713 移除旧版楼梯装饰散布 → 根除 t682「楼梯悬浮挡路」类回归），跳过全部设施房
        //    （传送门房 / 图书馆 / 南房 / 储藏龛 —— 特征格所在房间整房跳过，比旧 bbox 精确跳过更保守）。
        //    确定性 → 同 seed 同装饰（PLAN §2-K）。
        for (int dx = -kHalf + 1; dx <= kHalf - 1; ++dx) {
            for (int dz = -kHalf + 1; dz <= kHalf - 1; ++dz) {
                if (!insideSpace(dx, dz)) continue; // 仅内部空间（走廊）装饰
                if (dx >= -12 && dx <= 12 && dz >= -21 && dz <= -11) continue; // 传送门房 + 北走廊跳过（岩浆/楼梯/框架保留）
                if (dx >= -21 && dx <= -12 && dz >= -8 && dz <= 8) continue;   // 图书馆跳过（书架保留）
                if (dz >= 7 && dz <= 18) continue;                             // 南门洞 + 南房跳过（刷怪笼/宝箱保留）
                if (dx >= 12 && dx <= 18 && (dz <= -3 || dz >= 3)) continue;   // 东北 / 东南储藏龛跳过
                if (std::abs(dx) == 3 && std::abs(dz) == 3) continue;          // 大厅承重柱格（柱体保留）
                const quint32 wh = hashVoxel(strongSeed ^ 0x487, cx + dx, cy + 1, cz + dz);
                if ((wh % 100u) < 8u)
                    put(dx, 1, dz, BlockRegistry::Cobweb);        // 蛛网（走廊残破感；贴地不悬空）
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
        m_hasStronghold = true;
        m_strongholdPortalX = candidates[size_t(bestIdx)][0];
        m_strongholdPortalY = candidates[size_t(bestIdx)][1] + 4;
        m_strongholdPortalZ = candidates[size_t(bestIdx)][2] - 18;
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
