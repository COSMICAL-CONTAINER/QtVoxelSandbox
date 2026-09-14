#ifndef MATHTYPES_H
#define MATHTYPES_H

// R20.05 基础类型（refactor-plan §29.3 R20.05 + §5.1 voxel_base 方向）：坐标 / 时间数学原语
// 的单一权威落脚头。本单契约 =「只立类型 + 最小示范采用，不 wholesale 迁移调用点」——
//   - floorDiv / floorMod：向下取整除 / 模（负坐标正确语义）。plan §5.1 原文「负坐标的
//     floorDiv 和 floorMod 必须统一实现；所有模块禁止自行实现负坐标换算」。现状盘点
//     （R20.05 落回标）：chunk 路由全部集中在 ChunkManager 且为「x<0 早退守卫 + 截断除法
//     x/16 + 减法重建 x-cx*16」——非负域上正确，负坐标域没有任何语义（越界=空气/拒绝）；
//     本函数是未来无界世界（R20.09 chunk 生命周期）写负坐标换算的唯一合法姿势。
//   - ChunkKey：chunk 坐标 (cx,cz) → 可哈希 / 可排序 / 可打包键（plan §5.1「必须使用有符号
//     坐标；不允许把世界坐标直接转换为无符号数组下标」）。本单不改 ChunkManager 网格布局，
//     只把其索引公式包装成类型（flatIndex）。
//   - BlockPos：方块坐标 (x,y,z) int + 基本运算 / 邻接。
//   - Tick：固定 tick 基准常量（节流常量族盘点与迁移边界见 struct Tick 头注释）。
// 分层（PLAN §2）：Core 叶子——只依赖 QtGlobal/<array>/<cstddef>，不依赖
// World/Renderer/Game/Entities/Qt 任何容器头（哈希自包含，无随机盐）。
// 本单示范采用面（各 2-3 处，行为逐位不变——非负域上 floorDiv==截断除法、floorMod==减法
// 重建式；worldgen 逐位恒等由矩阵 worldgen 腿族守）：chunkmanager.cpp 路由 ×3、
// worldclock.h kTickMs 声明点、world.h/chunkmanager.h BlockPos 加性重载。「摘即红」由
// 矩阵 r2005d 源码钉（pinSet 剥注释后计数）承担。

#include <QtGlobal> // quint32 / quint64
#include <array>    // BlockPos::neighbors
#include <cstddef>  // size_t（std::hash 特化 / qHash 签名）

// ── floorDiv / floorMod：向下取整除 / 模（负坐标语义单一权威）──────────────────
// 恒等式 a == b * floorDiv(a,b) + floorMod(a,b) 成立；floorMod 结果与 b 同号（或 0）——
// 数学 floor 语义（负坐标落「左下」chunk，机制等价 MC）：
//   floorDiv(-1,16) = -1、floorMod(-1,16) = 15（方块 x=-1 → chunk -1 的局部 15）。
//   C++ 原生 / 与 % 向零截断（-7/16==0、-7%16==-7），负坐标直接用会把 -1..-15 全错映进
//   chunk 0 / 负局部坐标；移位掩码式（x>>4 / x&15）则依赖补码与算术移位语义，可读性陷阱。
// 前置条件：b != 0；且非 (a == INT_MIN && b == -1)（该整除溢出，同原生 / 的 UB 边界）。
inline int floorDiv(int a, int b)
{
    int q = a / b;
    const int r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        --q; // 截断方向与 floor 相反时向 -∞ 校正
    return q;
}

inline int floorMod(int a, int b)
{
    int r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b; // 余数折进与 b 同号的半开区间
    return r;
}

// ── 64 位整数终混（哈希内核；两键类型共用，无随机盐 = 跨进程 / 平台哈希稳定）──────
inline quint64 hashMix64(quint64 z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// ── ChunkKey：chunk 列坐标 (cx,cz) → 统一键 ────────────────────────────────────
// 有符号坐标（plan §5.1）；packed 打包用补码映射（quint32）保 int32 域双射 → 负坐标不
// 别名（对照：world.cpp 既有 packGrowthCell 的 quint16 截断打包在负 / 大坐标域会静默环绕
// ——本类型是「负坐标安全」的键权威，本单不迁移旧打包点）。
struct ChunkKey
{
    int cx = 0;
    int cz = 0;

    // 世界坐标 → chunk 坐标（floorDiv 语义：负坐标进「左下」chunk）。chunkSize 由 caller
    // 传入——Core 叶子不依赖 World，不能引用 Chunk::kSize；单一权威靠调用方传参 + 矩阵
    // r2005b 腿互钉（Chunk::kSize == 16 与本参数取值）。
    static ChunkKey fromWorld(int wx, int wz, int chunkSize)
    {
        return ChunkKey{ floorDiv(wx, chunkSize), floorDiv(wz, chunkSize) };
    }

    // 既有 ChunkManager 网格布局（chunkmanager.cpp chunk()/recreate() 的 m_chunks 索引
    // cx + chunksX * cz）的类型化包装——逐位同式，本单只包装不改布局（示范采用点）。
    int flatIndex(int chunksX) const { return cx + chunksX * cz; }

    // 打包 64 位键：两轴各取补码位型（quint32）拼接。int 域 → quint32 是双射 → packed 对
    // 全部 (cx,cz) 单射（可逆，见 fromPacked）。
    quint64 packed() const
    {
        return (quint64(quint32(cx)) << 32) | quint64(quint32(cz));
    }
    static ChunkKey fromPacked(quint64 k)
    {
        return ChunkKey{ int(quint32(k >> 32)), int(quint32(k)) };
    }

    // 哈希：hashMix64 终混是双射 → hash() 在 64 位域内单射（矩阵 r2005b 钉「探针网格上
    // 键不同则哈希必不同」的强性质）。
    quint64 hash() const { return hashMix64(packed()); }

    friend bool operator==(const ChunkKey &a, const ChunkKey &b) { return a.cx == b.cx && a.cz == b.cz; }
    friend bool operator!=(const ChunkKey &a, const ChunkKey &b) { return !(a == b); }
    // 「可排序」：字典序全序（cx 主、cz 次）。本单无排序消费面，仅立总序语义。
    friend bool operator<(const ChunkKey &a, const ChunkKey &b)
    {
        return a.cx != b.cx ? a.cx < b.cx : a.cz < b.cz;
    }
};

// ── BlockPos：方块坐标 (x,y,z) + 基本运算 / 邻接 ────────────────────────────────
// 值语义纯代数：不携带越界检查 / chunk 路由（那是 ChunkManager 的职责，见其 BlockPos
// 加性重载——本单示范「新类型可被 World 使用」）。坐标可负（类型层不做任何域假设）。
struct BlockPos
{
    int x = 0;
    int y = 0;
    int z = 0;

    // 6 正交邻（+X,-X,+Y,-Y,+Z,-Z 固定序）。
    std::array<BlockPos, 6> neighbors() const
    {
        return { BlockPos{ x + 1, y, z }, BlockPos{ x - 1, y, z },
                 BlockPos{ x, y + 1, z }, BlockPos{ x, y - 1, z },
                 BlockPos{ x, y, z + 1 }, BlockPos{ x, y, z - 1 } };
    }

    int manhattanLength() const
    {
        const int ax = x < 0 ? -x : x;
        const int ay = y < 0 ? -y : y;
        const int az = z < 0 ? -z : z;
        return ax + ay + az;
    }

    // 6-邻接：曼哈顿距离恰 1（共面贴邻；对角 / 同格不算——对角是切比雪夫 1、曼哈顿 2）。
    bool isAdjacentTo(const BlockPos &o) const { return BlockPos{ x - o.x, y - o.y, z - o.z }.manhattanLength() == 1; }

    friend bool operator==(const BlockPos &a, const BlockPos &b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    friend bool operator!=(const BlockPos &a, const BlockPos &b) { return !(a == b); }
    friend BlockPos operator+(const BlockPos &a, const BlockPos &b)
    {
        return BlockPos{ a.x + b.x, a.y + b.y, a.z + b.z };
    }
    friend BlockPos operator-(const BlockPos &a, const BlockPos &b)
    {
        return BlockPos{ a.x - b.x, a.y - b.y, a.z - b.z };
    }
};

// ── 哈希双面（std::unordered_* 与 QHash/QSet 均可用）─────────────────────────────
namespace std {
template <> struct hash<ChunkKey>
{
    size_t operator()(const ChunkKey &k) const noexcept { return size_t(k.hash()); }
};
template <> struct hash<BlockPos>
{
    size_t operator()(const BlockPos &p) const noexcept
    {
        // 96 位坐标域 → 64 位：正整数乘法错位 + 终混（普通散列，无单射声明）。
        const quint64 h = quint64(quint32(p.x)) * 0x9E3779B97F4A7C15ull
            ^ quint64(quint32(p.y)) * 0xC2B2AE3D27D4EB4Full
            ^ quint64(quint32(p.z)) * 0x165667B19E3779F9ull;
        return size_t(hashMix64(h));
    }
};
} // namespace std

inline size_t qHash(const ChunkKey &k, size_t seed = 0) noexcept
{
    return size_t(k.hash()) ^ seed;
}
inline size_t qHash(const BlockPos &p, size_t seed = 0) noexcept
{
    return std::hash<BlockPos>{}(p) ^ seed;
}

// ── Tick：固定 tick 基准常量（单一权威命名落脚）────────────────────────────────
// 盘点（R20.05 落回标；本单**不** wholesale 迁移，家族登记在此防漂移，R20.07 GameSession
// 固定 Tick 时收口）：
//   基准 tick：WorldClock::kTickMs=100（10Hz；声明点已改指 Tick::kClockTickMs = 本单示范
//     采用①；World 内全部节流常量以「本 tick 的个数」为单位）。
//   tick 间隔常量族（world.h 私有 static，单位 = 基准 tick 个数 ×0.1s）：
//     kFlowTickInterval=3 / kLavaFlowTickInterval=30 / kFireTickInterval=5 /
//     kFreezeTickInterval=50 / kIceMeltTickInterval=20 / kCropTickInterval=25 /
//     kSugarcaneTickInterval=50 / kFarmlandHydrTickInterval=30 / kSaplingTickInterval=50 /
//     kBerryBushTickInterval=50 / kLeafDecayTickInterval=4（另 kBurnWindowsWood=10 /
//     kBurnWindowsLight=4 为燃烧窗档，单位同）。
//   异构域登记：EntityManager::kAiTickInterval=4（单位 = 渲染帧，60Hz 错峰节流，非本 tick 域）；
//     PlayerController 物理 tick ~16ms（60Hz 帧，同异构域）。
struct Tick
{
    static constexpr int kClockTickMs = 100;      // WorldClock 基准 tick 周期（10Hz）
    static constexpr float kClockTickSecs = 0.1f; // 同上秒制（ticked(deltaSecs) 携带值口径）
};

#endif // MATHTYPES_H
