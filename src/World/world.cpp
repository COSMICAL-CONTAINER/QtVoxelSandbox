#include "world.h"

#include "blockregistry.h"

#include <QDebug>
#include <algorithm>
#include <cmath>

// t149 海平面（水位）：worldgen 沙滩带 / 沙漠水位 / 填水 / 树·矿石阈值的单一权威常量。
//   spec 原文 waterLevel=8 是 t119 重定标（heightAt 3..11 → 16..40）**之前**的旧地形范围；
//   t119 后 heightAt∈[16,40]，8 < min(16) → 无任何列满足 h<8 → 填水为零、沙滩不可见。故按
//   重定标同源取 24：约 11% 低洼列被淹（散布湖泊），出生列(8,8) h=27>24 保持陆地、近处低洼可见
//   水域（同 seed 复算核对）。语义不变（海平面淹低洼），仅数值随地形重定标。
//   全程纯函数于 seed + heightAt（fbm）→ 同 seed 同水域 / 沙滩分布（PLAN §2-K）。
constexpr int kWaterLevel = 24;

World::World(QObject *parent) : QObject(parent)
{
    generate(); // 默认参数生成（静默，不 emit）
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
    if (oldId != BlockRegistry::Air && id == BlockRegistry::Air)
        emit blockBroken(x, y, z, int(oldId)); // 破：带原方块 id（粒子/音效按它取色/取声）
    else if (id != BlockRegistry::Air)
        emit blockPlaced(x, y, z, int(id));    // 放：带新方块 id
    emit worldChanged(); // 触发 ChunkGeometry 重建（本回合整个单 mesh；mesher 拆分归 t03）
    return true;
}

// t133：世界坐标 state 读（跨 chunk 路由，经 ChunkManager）。越界 → 0。供 mesher / QML 查异形方块朝向。
quint8 World::stateAt(int x, int y, int z) const
{
    return m_chunks.stateAt(x, y, z);
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
    if (oldId != id) {
        // id 变化 → 发 broken/placed（同 4 参数语义：破带原 id、放带新 id）；id 不变只 state 变（门开合）不发。
        if (oldId != BlockRegistry::Air && id == BlockRegistry::Air)
            emit blockBroken(x, y, z, int(oldId));
        else if (id != BlockRegistry::Air)
            emit blockPlaced(x, y, z, int(id));
    }
    emit worldChanged(); // 异形方块 state 变（开合 / 朝向）需 mesh 重建
    return true;
}

// t117 FallingBlock 着地专用：m_chunks.setBlock 直写 + emit worldChanged，不发 blockPlaced（与玩家放置
// 语义分离，沿用 worldgen 直写不触发 blockPlaced 的既有约定）。仅在目标为空气时写入（防御）。越界 / 非空 → false。
bool World::setBlockFromEntity(int x, int y, int z, quint8 id)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air) return false; // 已占用 → 不覆盖
    m_chunks.setBlock(x, y, z, id); // 跨 chunk 写入 + 标目标脏 + 边界格标邻接脏
    emit worldChanged(); // 驱动 mesh 重建（不发 blockPlaced / blockBroken —— 系统事件非玩家动作）
    return true;
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
    const double n = fbm((x + m_seed) * 0.09, (z + m_seed) * 0.09); // [-1,1]
    const int h = int(std::lround(28.0 + n * 12.0));               // ~16..40
    return std::max(0, h);
}

// t117 沙漠群系判定（二次 fBm biome，PLAN §2-K 确定性）：用与高度噪声**不同**的空间频率（0.018 vs
// 0.09）+ seed 偏移（+7919）独立采样同一 fBm → 群系图与高度图解耦（低洼 / 高地与干旱 / 普通独立）。
// 低频 → 大区块连续（沙丘成片，非逐格斑点，机制等价 MC 1.0 沙漠群系的大尺度分布）。阈值 0.25 →
// ~36% 干旱（fbm 近似正态，>0.25 约三分之一面积）。纯函数于 seed → 同 seed 同群系分布。
bool World::isDesert(int x, int z) const
{
    const double b = fbm((x + m_seed + 7919) * 0.018, (z + m_seed + 7919) * 0.018); // [-1,1]
    return b > 0.25;
}

void World::generate()
{
    buildPermutation();
    m_chunks.recreate(m_width, m_depth, m_height); // 重建 chunk 网格（全新零填充 chunk，全脏）

    // 填充地形（逐列规则，仅放大到 width×depth）：表层选择由「群系 + 水位」决定，下层 dirt / 深 stone。
    //   - 沙漠群系（isDesert）：整柱沙（y 0..h 全 Sand，机制等价 MC 沙漠沙层）；底层基岩由 placeBedrock 覆盖。
    //   - 非沙漠且 h <= waterLevel+1：沙滩带 / 水下沙底。spec 显式沙滩带 h∈[wl-1,wl+1]（海平面 ±1 的可见沙滩），
    //     h<wl 的更低列被 fillWater 淹没 → 其表层同样取沙（水下沙底，草不生于水下）。表层 Sand / 下 Dirt / 深 Stone。
    //   - 非沙漠且 h > waterLevel+1：正常陆地（表层 Grass / 下 Dirt / 深 Stone）。
    //   旧的 sandLevel=3 低洼沙判定被水位阈值（24+1=25）完全覆盖（h<=3 << 25 必属水下沙底），故删除。
    //   逐列独立 → 跨 chunk 边界天然连续，无浮空/悬崖；同 seed 确定（fbm/纯函数，PLAN §2-K）。
    //   走 ChunkManager.setBlock 跨 chunk 写入（初始全脏，其脏标记在此无副作用）。
    int desertCols = 0, sandyCols = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int h = std::min(heightAt(x, z), m_height - 1);
            const bool desert = isDesert(x, z);
            const bool sandy = (!desert) && (h <= kWaterLevel + 1); // 沙滩带(wl±1) + 水下(h<wl)
            if (desert) ++desertCols;
            else if (sandy) ++sandyCols;
            for (int y = 0; y <= h; ++y) {
                quint8 b;
                if (desert) {
                    b = BlockRegistry::Sand; // 整柱沙
                } else if (sandy) {
                    if (y == h)          b = BlockRegistry::Sand;  // 沙表层（沙滩 / 水下沙底）
                    else if (y >= h - 2) b = BlockRegistry::Dirt;  // 表层下土
                    else                 b = BlockRegistry::Stone; // 深石
                } else {
                    if (y == h)          b = BlockRegistry::Grass; // 草地表层
                    else if (y >= h - 2) b = BlockRegistry::Dirt;  // 土
                    else                 b = BlockRegistry::Stone; // 石
                }
                m_chunks.setBlock(x, y, z, b);
            }
        }
    }
    qInfo() << "worldgen: desert =" << desertCols << "beach/underwater =" << sandyCols
            << "/" << (m_width * m_depth);

    placeBedrock(); // t119：底层基岩（y 0..4 坑洼，底实顶疏；不可破坏）。先于矿石 / 树（仅覆盖最底几格）
    scatterOres(); // 地形填充后确定性散布矿石（stone 区段，t84；先于树木，树只动地表空气无冲突）
    fillWater(); // t148：海平面以下低洼列填水（地形之上；先于树木 → 水占格使树不生于水中，setVoxelIfAir 守）
    placeTrees(); // 地形填充后确定性种树（grass 表层，PLAN §2-K）
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
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return; // 世界越界跳过（setVoxelIfAir 已含边界判，配合 placeTrees 的钳制双重保险）
    if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air)
        return;
    m_chunks.setBlock(x, y, z, id);
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

// 确定性树木散布：遍历列，按哈希(seed,x,z) 决定是否尝试种树；占用栅格保证主干间距 ≥2 列。
// 仅在 grass 表层（heightAt > waterLevel+1，与 generate() 沙层判定同阈值）种；沙滩/水下/沙漠/越界/近邻有
// 树干 → 跳过。主干高度按世界高度钳制（留出树冠空间），放不下最小树则确定性跳过。全部纯函数于 seed → 可复现。
void World::placeTrees()
{
    std::vector<char> occupied(size_t(m_width) * size_t(m_depth), 0); // 主干占用栅格（1=该列已有树干）

    constexpr int kMinTrunk    = 4; // 主干最少格数
    constexpr int kMaxTrunk    = 7; // 最多 7（低洼处可达）；高处按世界高度钳到 4 → 高度自然参差（用户诉求）
    constexpr int kCanopyExtra = 2; // 树冠在主干顶之上再升的格数（十字冠层 + 尖顶）
    constexpr int kDensityPct  = 2; // 每 grass 列 ~2% 尝试 → 经间距筛选后零星分布（不密集成林）

    int placed = 0;
    for (int z = 0; z < m_depth; ++z) {
        for (int x = 0; x < m_width; ++x) {
            const int surfaceY = heightAt(x, z);
            // t149：水位阈值取代旧 kSandLevel=3 —— 沙滩带(wl±1)/水下(h<wl)/低洼不种树（机制等价 MC 树不生于沙滩/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            if (isDesert(x, z)) continue;         // t117 沙漠群系不种树（机制等价 MC 沙漠无树）

            const quint32 r = hashColumn(m_seed, x, z);
            if (r % 100u >= unsigned(kDensityPct)) continue; // 密度筛选

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

            // leafRand = 哈希高位 → 驱动树冠四角叶随机（每棵轮廓各异）；与密度(低位)/高度(>>8)字段不重叠。
            placeTreeAt(x, surfaceY, z, trunkH, r >> 16);
            occupied[size_t(x) + size_t(m_width) * size_t(z)] = 1;
            ++placed;
        }
    }
    qInfo() << "worldgen: trees placed =" << placed; // 可观测：同 seed → 同计数（确定性核对）
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

// 确定性矿石散布（t84，PLAN §2-K）：遍历 stone 区段（generate 把 y<h-2 的格填 Stone，沙漠/沙滩表层除外），
// 按 hashVoxel(seed,x,y,z) 的低 16 位（% 10000）做密度筛选 → 替换为煤矿/铁矿。仅替换 Stone
// （不动 dirt/grass/sand/log/leaves，也不动已生成的另一种矿：判定只针对 Stone 格）。铁矿比煤矿
// 略稀（贴近 MC 1.0 铁比煤少）；两矿共用同一 hash 的不同阈值段 → 互斥（一格至多一种矿）。
// 全程纯函数于 seed → 可复现；禁用任何运行期随机源（QTime/时钟/全局 RNG）。
//
// 密度（/10000）：煤矿 0.8%、铁矿 0.5%（散点式，非 MC 的脉状集群——脉状留后续 worldgen 增强）。
// 铁矿判定先于煤矿（pct < kIronPct 优先），使铁的稀有度不被煤矿阈值「吃掉」。
void World::scatterOres()
{
    constexpr unsigned kIronPct = 50;  // /10000 → 0.5%（铁，需石镐）
    constexpr unsigned kCoalPct = 80;  // /10000 → 0.8%（煤，木镐可挖）

    int coalPlaced = 0, ironPlaced = 0;
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
                    continue; // 仅替换 stone（防御：树根/边界异常格不动）
                const quint32 r = hashVoxel(m_seed, x, y, z);
                const unsigned pct = unsigned(r % 10000u);
                if (pct < kIronPct) {
                    m_chunks.setBlock(x, y, z, BlockRegistry::IronOre);
                    ++ironPlaced;
                } else if (pct < kIronPct + kCoalPct) {
                    m_chunks.setBlock(x, y, z, BlockRegistry::CoalOre);
                    ++coalPlaced;
                }
            }
        }
    }
    qInfo() << "worldgen: ores placed = coal" << coalPlaced << "iron" << ironPlaced; // 同 seed → 同计数（确定性核对）
}

// t148 海平面填水（PLAN §2-K 确定性）：遍历列，地表高度 h < waterLevel 的低洼列从 h+1 到 waterLevel
//   填 Water（机制等价 MC 海洋 / 湖泊：低洼被水淹没到统一海平面）。仅在空气格写入（防御：不动地形 /
//   基岩 / 矿石）。经 m_chunks.setBlock 直写（跨 chunk 路由 + 标脏 + 边界邻接 + heightmap 增量维护），
//   不触发 blockPlaced（同 worldgen 既有约定——系统事件非玩家放置）。
//
//   waterLevel 取文件级 kWaterLevel（=24，见 world.cpp 顶部注释）：spec 原文 8 为 t119 重定标前地形范围
//   （旧 heightAt 3..11），现 heightAt∈[16,40] → 8 < min(16) 无列满足 → 重定标到 24，约 11% 列被淹（散布
//   湖泊），出生列(8,8) h=27>24 保持陆地、近处低洼可见水域。t149 沙滩带/沙漠水位/树·矿石阈值均同源用此
//   常量（generate 沙表层 / placeTrees / scatterOres 阈值 = waterLevel+1）。
//   全程纯函数于 seed + heightAt（fbm）→ 同 seed 同水域分布；禁用任何运行期随机源（PLAN §2-K）。
void World::fillWater()
{
    int waterCells = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int h = std::min(heightAt(x, z), m_height - 1);
            if (h >= kWaterLevel) continue; // 此列地表高于海平面 → 无水
            // 从地表上方一格到海平面填水（h+1..kWaterLevel）。低洼列被水淹没到统一海平面。
            for (int y = h + 1; y <= kWaterLevel && y < m_height; ++y) {
                if (m_chunks.blockAt(x, y, z) != BlockRegistry::Air)
                    continue; // 仅写空气格（防御：不动已存在方块）
                m_chunks.setBlock(x, y, z, BlockRegistry::Water);
                ++waterCells;
            }
        }
    }
    qInfo() << "worldgen: water cells =" << waterCells; // 同 seed → 同计数（确定性核对）
}
