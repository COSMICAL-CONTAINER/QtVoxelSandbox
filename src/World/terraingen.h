#ifndef TERRAINGEN_H
#define TERRAINGEN_H

// R20.12 纯地形生成单一权威（refactor-plan §29.3 R20.12「迁移一个最小地形生成路径」——
// 本头即被迁移的那个最小路径：**基础地形柱生成**（Perlin fBm 采样 + 逐列方块选择），自
// world.cpp 的成员函数群与 generate() 首循环列体**逐行搬移**（体逐位同式，worldgen 逐位恒等
// 由矩阵 worldgen/determin/re-gen 三腿族守）。迁移后同步路径（World::generate 经 WorldColumnSink
// 写 m_chunks）与后台 worker（BackgroundGenerationWorker 经缓冲 sink 写 GeneratedChunkData）
// **共用本头同一份函数**——禁复制第二份生成逻辑（同 seed 同坐标输出逐位一致的承重结构）。
//
// ── 纯函数性（R20.12 验收②「seed、ChunkKey、generator version 决定输出」）──────────────
//   全部采样函数是「(seed, dims) → 值」的纯函数：唯一状态 = 构造期由 seed 确定性填出的置换表
//   （buildPermutation 线性同余，可复现）+ dims 快照，**构造后只读** → 跨线程共享读安全、
//   线程安全靠无共享可变态（worker 侧零全局 RNG、零运行期随机源——PLAN §2-K 既有证据的延伸）。
//   同一 (seed, dims) 的两个实例对所有坐标给出逐位相同的结果（矩阵 r2012b 双实例恒等腿钉）。
//   kGeneratorVersion 是输出三要素之一：生成算法语义变更时递增（当前 1 = 迁移自 world.cpp 的
//   原始算法，worldgen 逐位恒等），GeneratedChunkData 逐字节携带以便上游识别过期产物。
//
// ── 迁移清单（本单已迁 / 未迁——plan「不一次性迁移所有结构」的登记面）────────────────────
//   已迁（= 本头）：置换表 / noise2 / noise3 / fbm / heightAt / biomeComputeAt（纯计算本体，
//     World 侧 memo 保留在 World::biomeAt 不迁）/ seaCorner / seaColumnHeight / isSeaSandColumn /
//     hashColumn / hashVoxel / fade·lerp·grad2·grad3 / kWaterLevel / generate() 首循环列体
//     （fillTerrainColumn）。
//   未迁（= 仍为 World 同步专有，登记 R20.13+）：placeBedrock / scatterOres / carveCaves /
//     carveCaveEntrances / carveCanyon / placeGravelPockets / placeUndergroundWaterPools /
//     placeLavaLakes / placeDungeons / placeMineshaft / placeDesertTemple / placeJungleTemple /
//     placeStronghold / pruneFloatingSnowLayers / pruneUnsupportedWorldgenRails / fillWater /
//     freezeSurfaceWater / placeSurfaceLakes / placeSwampPools / placeTrees / placeJungleTrees /
//     placeTallGrass / placeDesertFlora / placeSwampFlora / placeFlowers / placeSugarcane /
//     placeSweetBerryBushes / findSpawnColumn / recomputeLightField / rebuild*Cells /
//     rebuildStructureRegions / biomeAt 的 memo 壳（memo 是透明缓存，语义面零影响）。
//
// ── 分层（PLAN §2）────────────────────────────────────────────────────────────────
//   World 层 header-only（同 chunklifecycle/generationjob 形态），非 QObject 无 AUTOMOC；
//   向下依赖 Core 叶子（blockregistry 常量表）+ QtGlobal/<cmath>/<vector>，不依赖
//   Renderer/Game/Entities/QML。Biome 枚举自 World 嵌套迁入（单一权威），World 以 using
//   别名保持全部调用点零改动。
//
// ── 值纪律──────────────────────────────────────────────────────────────────────────
//   Dims / TerrainColumnSummary 为纯值聚合；本头无任何可变全局 / 静态可变态（kWaterLevel /
//   kGeneratorVersion / kChunkSize 是 constexpr 常量）。

#include <QtGlobal> // quint8 / quint32

#include <algorithm> // std::min / std::max / std::swap
#include <cmath>     // std::floor / std::sqrt / std::lround
#include <vector>

#include "blockregistry.h" // 方块 id 常量（Core 只读表——列体选择用）

// ── TerrainGen：(seed, dims) 参数化的纯地形采样器（构造后只读）────────────────────────
class TerrainGen
{
public:
    // t274/t306 群系枚举（自 World 嵌套迁入——单一权威；值域 0..6 编码即 World::biomeIdAt 口径）。
    enum class Biome { Plains, Hills, Desert, Forest, Snowy, Swamp, Jungle };

    // 世界尺寸快照（World 构造 TerrainGen 时定格；纯采样只读它）。
    struct Dims
    {
        int width = 0;
        int depth = 0;
        int height = 0;
    };

    // 生成器版本（R20.12 验收②三要素之一）：算法语义变更时递增；1 = 自 world.cpp 逐行迁入的
    //   原始算法（worldgen 逐位恒等基线）。GeneratedChunkData 携带本值供上游识别过期产物。
    static constexpr quint32 kGeneratorVersion = 1;
    // t149 海平面（自 world.cpp 迁入——单一权威常量；语义见其原注释：worldgen 沙滩带 / 填水 /
    //   树·矿石阈值的基准，t338 起海 + 沙滩集中于一角）。
    static constexpr int kWaterLevel = 58;
    // chunk 边长（= Chunk::kSize；Core 叶子 mathtypes 不能引用 World 的 Chunk，故本地立常量，
    //   与 Chunk::kSize 的恒等由 backgroundgeneration.h 编译期 static_assert 互钉）。
    static constexpr int kChunkSize = 16;

    explicit TerrainGen(int seed, Dims dims) : m_seed(seed), m_dims(dims) { buildPermutation(); }

    int seed() const { return m_seed; }
    Dims dims() const { return m_dims; }

    // ── 整数哈希（列级 / 体素级；自 world.cpp 逐行迁入，签名不变）───────────────────────
    // 整数哈希（FNV-1a + avalanche）：seed/x/z → 32 位确定性伪随机。纯函数，不依赖任何运行期
    // 随机源（PLAN §2-K：固定 seed → 完全一致的树分布）。与 Perlin 置换表独立，避免树位与高度噪声耦合。
    quint32 hashColumn(int seed, int x, int z) const
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
    quint32 hashVoxel(int seed, int x, int y, int z) const
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

    // ── Perlin 噪声族（自 world.cpp 逐行迁入；fade/lerp/grad 为头内 inline）────────────────
    double noise2(double x, double z) const
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
    double noise3(double x, double y, double z) const
    {
        const int X = int(std::floor(x)) & 255;
        const int Y = int(std::floor(y)) & 255;
        const int Z = int(std::floor(z)) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        z -= std::floor(z);
        const double u = fade(x), v = fade(y), w = fade(z);
        const int A = m_perm[X] + Y;
        const int AA = m_perm[A] + Z;
        const int AB = m_perm[A + 1] + Z;
        const int B = m_perm[X + 1] + Y;
        const int BA = m_perm[B] + Z;
        const int BB = m_perm[B + 1] + Z;
        const double x1 = x - 1.0;
        const double y1 = y - 1.0;
        const double z1 = z - 1.0;
        return lerp(
            lerp(
                lerp(grad3(m_perm[AA], x, y, z), grad3(m_perm[BA], x1, y, z), u),
                lerp(grad3(m_perm[AB], x, y1, z), grad3(m_perm[BB], x1, y1, z), u),
                v),
            lerp(
                lerp(grad3(m_perm[AA + 1], x, y, z1), grad3(m_perm[BA + 1], x1, y, z1), u),
                lerp(grad3(m_perm[AB + 1], x, y1, z1), grad3(m_perm[BB + 1], x1, y1, z1), u),
                v),
            w);
    }

    double fbm(double x, double z) const
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

    // ── 群系 / 高度 / 海域（自 world.cpp 逐行迁入；biomeAt 的 memo 壳留在 World）────────────
    // t905 perf：biomeAt 的 fBm 计算本体（原 biomeAt 函数体原样迁移，零语义变化）。最多 5 条 4 阶 fBm
    //   （主群系图 + 丛林 + 森林 + 雪原 + 沼泽，每条 4 次 noise2）→ 单次 ~20 次 Perlin 采样。
    Biome biomeComputeAt(int x, int z) const
    {
        const double b = fbm((x + m_seed + 3571) * 0.012, (z + m_seed + 3571) * 0.012); // [-1,1]
        if (b > 0.5) return Biome::Hills;
        if (b < -0.4) return Biome::Desert;
        // t481/t486 前置 丛林（Jungle）：第五条独立低频 fBm（频率 0.014 + seed 偏移 +5133）。低频 →
        //   丛林成片（非逐格斑点）。从 Forest/Plains 中分出。Hills/Desert 判定先于丛林早退、
        //   Snowy/Swamp 判定也在丛林-plains 判定之前早退 → 丛林绝不吞掉既有 Desert/Swamp/Snowy。
        constexpr double kJungleBiomeThresh = 0.25; // 实测（160×160 全域，10 seed 均值 ~13.5%）
        const double j = fbm((x + m_seed + 5133) * 0.014, (z + m_seed + 5133) * 0.014); // [-1,1]
        // t306：原 plains 候选带（b ∈ [-0.4,0.5]）用第二条独立低频 fBm 把 forest 从草原里 carve 出来。
        //   t373：阈值 0.15→0.40（把森林压成少数，草原重新成为大片开阔地带）。
        const double f = fbm((x + m_seed + 977) * 0.020, (z + m_seed + 977) * 0.020); // [-1,1]
        if (f > 0.40) return (j > kJungleBiomeThresh) ? Biome::Jungle : Biome::Forest; // 森林带内
        // t395 雪原/针叶群系：第三条独立低频 fBm（频率 0.016 + seed 偏移 +6420）。
        const double s = fbm((x + m_seed + 6420) * 0.016, (z + m_seed + 6420) * 0.016); // [-1,1]
        if (s > 0.45) return Biome::Snowy;
        // t396 沼泽群系：第四条独立低频 fBm（频率 0.024 + seed 偏移 +8842）。
        const double sw = fbm((x + m_seed + 8842) * 0.024, (z + m_seed + 8842) * 0.024); // [-1,1]
        if (sw > 0.30) return Biome::Swamp;
        return (j > kJungleBiomeThresh) ? Biome::Jungle : Biome::Plains; // 平原剩余带内
    }

    // t137/t307 地表高度（= 原世界高度图；按群系选振幅）。heightAt = 群系判定 + heightWithBiome；
    //   fillTerrainColumn 已持本列群系 → 直调 heightWithBiome 免二次群系计算（原版靠 World memo
    //   达成同效果；纯版用参数传递，值逐位一致）。
    int heightAt(int x, int z) const { return heightWithBiome(x, z, biomeComputeAt(x, z)); }

    int heightWithBiome(int x, int z, Biome bio) const
    {
        // t307：地表基线 64，振幅沿 t162/t274 用户调定的平缓值（plains/forest 2、hills 7、desert 3、
        //   snow 3、swamp 0 完美平坦、jungle 5）。同 seed 确定（fbm 纯函数，PLAN §2-K）。
        const double n = fbm((x + m_seed) * 0.09, (z + m_seed) * 0.09); // [-1,1]
        double amp;
        switch (bio) {
            case Biome::Hills: amp = 7.0; break; // 起伏（山地感，仅此群系有显著地形变化）
            case Biome::Desert: amp = 3.0; break; // 平缓沙丘
            case Biome::Forest: amp = 5.0; break; // 森林（t341）：amp 2→5 起伏
            case Biome::Snowy: amp = 3.0; break; // 雪原/针叶（t395）：平缓起伏
            case Biome::Swamp: amp = 0.0; break; // 沼泽（t396）：完美平坦（浅水池稳态前提）
            case Biome::Jungle: amp = 5.0; break; // 丛林（t481/t486 前置）：与森林同级
            case Biome::Plains: // 草原（多数陆地）
            default: amp = 2.0; break; // 极平（spec「大草原=平地」）
        }
        const int h = int(std::lround(64.0 + n * amp));
        return std::max(0, h);
    }

    // t338 海域角点（4 角之一；seed 派生确定性）。
    void seaCorner(int &cx, int &cz) const
    {
        const quint32 r = hashColumn(m_seed, 0x5EA1u, 0xC0A5u);
        cx = (r & 1u) ? m_dims.width - 1 : 0;
        cz = (r & 2u) ? m_dims.depth - 1 : 0;
    }

    // t338/t372 海域列高度。返回 -1 = 远内陆；0..dims.height-1 = 海域重塑地表 y（沙海盘缓坡 /
    //   过渡带 smoothstep）。纯函数于 seed + dims + heightAt（fbm）（PLAN §2-K）。
    int seaColumnHeight(int x, int z) const
    {
        if (m_dims.width <= 0 || m_dims.depth <= 0) return -1;
        int cx, cz;
        seaCorner(cx, cz);
        const int dx = x - cx, dz = z - cz;
        const double dist = std::sqrt(double(dx) * dx + double(dz) * dz);
        const int seaRadius = std::min(m_dims.width, m_dims.depth) * 3 / 10; // 海域半径（地图短边 30%）

        // t372 岸线蜿蜒：低频 fBm 抖动有效半径 → 自然蜿蜒岸线（独立频率 0.07 + seed 偏移 +5331）。
        const double shore = fbm((x + m_seed + 5331) * 0.07, (z + m_seed + 5331) * 0.07); // [-1,1]
        const double effectiveRadius = double(seaRadius) * (1.0 + 0.12 * shore);          // ±12% 蜿蜒

        constexpr int kSeaDepth = 6;                  // 角点海深（水位之下格数）
        const int seaFloor = kWaterLevel - kSeaDepth; // 角点海底（最深）
        const int beachTop = kWaterLevel + 1;         // 岸线干沙滩（水位 +1）

        if (dist <= effectiveRadius) {
            // 沙海盘（海盆 + 干沙滩）：缓坡 + 高度噪声（柔化规整线性坡；独立频率 0.15 + seed 偏移 +8842）。
            const double t = dist / effectiveRadius; // 0（角点）..1（岸线）
            const double heightNoise = fbm((x + m_seed + 8842) * 0.15, (z + m_seed + 8842) * 0.15) * 1.5;
            const int h = int(std::lround(seaFloor + (beachTop - seaFloor) * t + heightNoise));
            return std::max(0, std::min(h, m_dims.height - 1));
        }

        // t372 高度过渡带：沙盘外圈把高度从 beachTop smoothstep 过渡到自然 heightAt。
        const double blendWidth = double(seaRadius) * 0.30; // 过渡带宽（海域半径 30%）
        if (dist <= effectiveRadius + blendWidth) {
            const int naturalH = std::min(heightAt(x, z), m_dims.height - 1);
            const double bt = (dist - effectiveRadius) / blendWidth; // 0（接沙盘）..1（接内陆）
            const double e = bt * bt * (3.0 - 2.0 * bt);             // smoothstep
            const int h = int(std::lround(beachTop + (naturalH - beachTop) * e));
            return std::max(0, std::min(h, m_dims.height - 1));
        }
        return -1; // 远内陆 → 走自然 heightAt
    }

    // t372 沙海盘判定（与 seaColumnHeight 共用完全相同的 effectiveRadius 计算——确定性一致）。
    bool isSeaSandColumn(int x, int z) const
    {
        if (m_dims.width <= 0 || m_dims.depth <= 0) return false;
        int cx, cz;
        seaCorner(cx, cz);
        const int dx = x - cx, dz = z - cz;
        const double dist = std::sqrt(double(dx) * dx + double(dz) * dz);
        const int seaRadius = std::min(m_dims.width, m_dims.depth) * 3 / 10;
        const double shore = fbm((x + m_seed + 5331) * 0.07, (z + m_seed + 5331) * 0.07); // 与 seaColumnHeight 同源
        const double effectiveRadius = double(seaRadius) * (1.0 + 0.12 * shore);
        return dist <= effectiveRadius;
    }

    // ── 每列地形填充（generate() 首循环列体逐行迁入——同步路径与后台 worker 共用的同一函数）──
    // 列体选择规则（t255/t338/t394/t526/t761 语义，原注释随迁）：
    //   - 沙漠群系：表层 4-6 格沙（colHash 低 2 位派生）下接砂岩层 3-5 格（bit[9:8] 派生）再下 Stone。
    //   - 沙海盘（inSandSea）：表层 Sand（t761 两级确定性哈希混排 Gravel 滩斑）+ Dirt + Stone。
    //   - 雪原内陆：SnowLayer 薄层（state 0..2，hashColumn bit[6:4] 派生）→ Snow 块 → Dirt → Stone；
    //     雪原过渡带（海域重塑带）不覆雪 → 泥顶。
    //   - 其余内陆：Grass 表层 / 下 Dirt / 深 Stone。
    // sink 协议：sink.write(x, y, z, id, state)——World 侧写 m_chunks.setBlock（5 参守卫入口），
    //   worker 侧写 GeneratedChunkData 缓冲（自持数据）。返回列摘要（群系 / 是否沙海盘 / 填充高）
    //   供 World::generate 的群系统计与 fillWater 等同源消费。
    struct TerrainColumnSummary
    {
        Biome biome = Biome::Plains;
        bool sandSea = false; // 仅沙海盘（海盆 + 干沙滩）列
        int height = -1;      // 本列填充顶 y（含）；-1 = 无填充（不发生于合法 dims）
    };

    template <typename Sink>
    TerrainColumnSummary fillTerrainColumn(int x, int z, Sink &sink) const
    {
        const Biome bio = biomeComputeAt(x, z);
        const bool desert = (bio == Biome::Desert); // t274：经群系单一权威（原 isDesert 收口）
        // t338/t372：海域（海 + 沙滩）集中于一角。seaColumnHeight 返回沙海盘 + 过渡带的重塑高度（>=0）；
        //   isSeaSandColumn 仅沙海盘为真 → 沙表层；过渡带走自然群系草地。
        const int seaH = seaColumnHeight(x, z);
        const bool inSeaHeight = (seaH >= 0);                       // 沙海盘 + 过渡带（高度重塑）
        const bool inSandSea = inSeaHeight && isSeaSandColumn(x, z); // 仅沙海盘（沙表层 / 灌水）
        const int h = inSeaHeight ? seaH : std::min(heightWithBiome(x, z, bio), m_dims.height - 1);
        // t255/t394：沙漠列确定性哈希（PLAN §2-K）——沙厚度 / 砂岩厚度各取不同位段派生（解耦）。
        const quint32 colHash = desert ? hashColumn(m_seed, x, z) : 0u;
        const int desertSandThickness = desert ? (4 + int(colHash % 3u)) : 0;
        const int desertSandstoneThickness = desert ? (3 + int((colHash >> 8) % 3u)) : 0;
        // t761 沙海盘表层沙砾混排：① 4×4 粗格共享决策按 kGravelBeachPct% 选「砾石斑带」；
        //   ② 带内逐列 kGravelBeachFill% 兑现（先成带再参差兑现 → 成片但边缘破碎的砾石滩观感）。
        constexpr unsigned kGravelBeachPct = 25u;  // 砾石斑带命中概率（密度主旋钮）
        constexpr unsigned kGravelBeachFill = 65u; // 带内列兑现概率
        const bool beachGravel = inSandSea
            && ((hashColumn(m_seed + 7611, x >> 2, z >> 2) % 100u) < kGravelBeachPct)
            && ((hashColumn(m_seed + 7612, x, z) % 100u) < kGravelBeachFill);
        for (int y = 0; y <= h; ++y) {
            quint8 b;
            if (inSandSea) {
                // t338 海域：沙表层（海底 / 沙滩）+ Dirt + Stone；t761 表层按列确定性混排沙砾。
                if (y == h) b = beachGravel ? BlockRegistry::Gravel : BlockRegistry::Sand;
                else if (y >= h - 2) b = BlockRegistry::Dirt;  // 表层下土
                else b = BlockRegistry::Stone;                 // 深石
            } else if (desert) {
                // t255/t394：表层沙 4..6 格下接 Sandstone 3..5 格再下 Stone（机制等价 MC 沙漠沙下砂岩）。
                if (h - y < desertSandThickness) b = BlockRegistry::Sand; // 表层沙
                else if (h - y < desertSandThickness + desertSandstoneThickness)
                    b = BlockRegistry::Sandstone; // 沙下砂岩
                else b = BlockRegistry::Stone;    // 深石
            } else {
                // t526 雪原地表结构：泥→雪块→积雪层；海域过渡带雪原列不覆雪（泥顶）。
                const bool isSnowy = (bio == Biome::Snowy);
                if (isSnowy && inSeaHeight) {
                    // t526 海域过渡带：雪原列不覆雪、不生成草方块 → 泥顶。
                    if (y == h) b = BlockRegistry::Dirt;
                    else if (y >= h - 2) b = BlockRegistry::Dirt;
                    else b = BlockRegistry::Stone;
                } else if (isSnowy) {
                    // t526 内陆雪原：SnowLayer 薄层（state 0..2）→ Snow 块 → Dirt → Stone。
                    if (y == h) {
                        // SnowLayer 薄层（state 0..2 随机；独立位段派生，确定性）。
                        const quint32 slHash = hashColumn(m_seed, x, z);
                        const quint8 snowState = quint8((slHash >> 4) % 3u); // 0..2（独立位段）
                        sink.write(x, y, z, BlockRegistry::SnowLayer, snowState);
                        continue; // 已写 SnowLayer（含 state），跳过下方默认 write（其会重置 state=0）
                    } else if (y == h - 1) {
                        b = BlockRegistry::Snow; // 雪层下雪块过渡
                    } else if (y >= h - 2) {
                        b = BlockRegistry::Dirt; // 表层下土
                    } else {
                        b = BlockRegistry::Stone; // 深石
                    }
                } else if (y == h) {
                    b = BlockRegistry::Grass; // 草地表层（非雪原列）
                } else if (y >= h - 2) {
                    b = BlockRegistry::Dirt; // 土
                } else {
                    b = BlockRegistry::Stone; // 石
                }
            }
            sink.write(x, y, z, b, quint8(0)); // 常规方块 state=0（同 4 参 setBlock 委托语义）
        }
        return TerrainColumnSummary{ bio, inSandSea, h };
    }

private:
    // 置换表（线性同余 RNG，可复现；同 seed → 同表 → 同高度图）。构造期一次性填充，此后只读。
    void buildPermutation()
    {
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

    // Perlin 渐变 / 缓动基元（自 world.cpp 文件级 static 逐行迁入——仅本头噪声族使用）。
    static double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
    static double lerp(double a, double b, double t) { return a + t * (b - a); }
    static double grad2(int hash, double x, double z)
    {
        int h = hash & 7;
        double u = h < 4 ? x : z;
        double v = h < 4 ? z : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -2.0 * v : 2.0 * v);
    }
    // t278 3D Perlin 梯度（与 grad2 同源；hash 低 4 位选 12 个 3D 梯度方向之一）。
    static double grad3(int hash, double x, double y, double z)
    {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }

    int m_seed;              // 世界种子（纯函数三要素之一；构造后只读）
    Dims m_dims;             // 世界尺寸快照（构造后只读）
    std::vector<int> m_perm; // 512 置换表（Perlin；构造期由 seed 确定性填充，此后只读）
};

#endif // TERRAINGEN_H
