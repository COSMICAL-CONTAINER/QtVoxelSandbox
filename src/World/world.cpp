#include "world.h"

#include "blockregistry.h"

#include <QDebug>
#include <QElapsedTimer> // t155c：recomputeLightAround 计时（测每帧编辑光照开销）
#include <algorithm>
#include <cmath>
#include <queue>   // t151：recomputeLightField 的 BFS flood-fill 队列
#include <unordered_map> // t185 tickWaterFlow 的 adds 哈希表（key = 体素线性编码 → 新 level，多源取 min）
#include <unordered_set> // t221 tickWaterFlow 的 evapKeys 集合（本 tick 将退场的格 key，供扩散 pass 跳过）

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

// t176 存档加载入口：重置到目标 seed 的零填充分区网格（不走 generate —— 由 WorldStore 写 chunk blob
//   覆盖）。recreate 把 25 chunk 全清零 + 全标脏（首帧重建）；buildPermutation 重建 Perlin 表使后续
//   heightAt 等查询用新 seed（一致性，虽加载路径主要靠存档而非 worldgen）。仅 emit seedChanged（dims 不变）；
//   **不** emit worldChanged（网格此时全空，finishLoad 写完 blob 后才统一触发重建，避免中间态重建浪费）。
void World::beginLoad(int seed)
{
    m_seed = seed;
    m_chunks.recreate(m_width, m_depth, m_height); // 零填充 + 全标脏（recreate 实现）
    buildPermutation();                            // 新 seed 的 Perlin 置换表（heightAt 查询一致性）
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
    emit worldChanged(); // 触发 25 个 ChunkGeometry 重建（terrain+water 两段）
    m_chunks.clearAllDirty();
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
    qInfo("vo.edit: setBlock %d,%d,%d  %d->%d", x, y, z, int(oldId), int(id)); // t155f 诊断：编辑时序
    if (oldId != BlockRegistry::Air && id == BlockRegistry::Air)
        emit blockBroken(x, y, z, int(oldId)); // 破：带原方块 id（粒子/音效按它取色/取声）
    else if (id != BlockRegistry::Air)
        emit blockPlaced(x, y, z, int(id));    // 放：带新方块 id
    recomputeLightAround(x, y, z, oldId, id); // t154：增量重 flood 编辑格周围有界盒（替代全量 recomputeLightField）
    emit worldChanged(); // 触发 ChunkGeometry 重建（terrain+water 两段 dirty chunk 同步重建）
    m_chunks.clearAllDirty(); // t155g：两段都重建完，统一清脏（防一段 clearDirty 抢清致另一段跳过 = 2s 卡顿根因）
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
    recomputeLightAround(x, y, z, oldId, id); // t154：增量重 flood（id 不变只 state 变 → 内部早退，光照无变化）
    emit worldChanged(); // 异形方块 state 变（开合 / 朝向）需 mesh 重建
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    return true;
}

// t117/t220 FallingBlock 着地专用：m_chunks.setBlock 直写 + emit worldChanged，不发 blockPlaced（与玩家放置
//   语义分离，沿用 worldgen 直写不触发 blockPlaced 的既有约定）。t220：仅在目标为**空气或水**时写入（着地格
//   由 FallingBlock 列扫保证为 air/水 —— 沙落水穿透后填堵水格；防御：其余已占用方块不覆盖）。越界 / 非空非水 → false。
bool World::setBlockFromEntity(int x, int y, int z, quint8 id)
{
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return false; // 越界拒绝
    const quint8 occ = m_chunks.blockAt(x, y, z);
    if (occ != BlockRegistry::Air && occ != BlockRegistry::Water) return false; // 仅空气 / 水可被实体着地覆盖
    m_chunks.setBlock(x, y, z, id); // 跨 chunk 写入 + 标目标脏 + 边界格标邻接脏
    recomputeLightAround(x, y, z, occ, id); // t154：增量重 flood（oldId=被覆盖的 air/水 → newId=id）
    emit worldChanged(); // 驱动 mesh 重建（不发 blockPlaced / blockBroken —— 系统事件非玩家动作）
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
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
    recomputeLightAround(x, y, z, lightOldId, id);
    emit worldChanged(); // 驱动 mesh 重建（水流是系统模拟，非玩家破/放 → 不发 broken/placed）
    m_chunks.clearAllDirty(); // t155g：两段重建完统一清脏
    return true;
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
//        - 水平蔓延：grounded 且 level < kMaxFlowLevel → 4 向邻居：air → 写 level+1（首达即最低）；
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
void World::tickWaterFlow()
{
    if (++m_flowTickCounter < kFlowTickInterval) return; // 节流：每 3 tick（~0.3s）把波前推进 1 格
    m_flowTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 体素线性 key：x + z*W + y*W*D。世界 ≤ 80×80×64 = 409600 < INT_MAX，编码安全。仅用于 adds 去重 / 取 min。
    auto keyOf = [W, D](int x, int y, int z) -> long long {
        return static_cast<long long>(x) + static_cast<long long>(z) * W
             + static_cast<long long>(y) * static_cast<long long>(W) * D;
    };

    // 1) 快照当前水格（tick 内栅格不变 —— 新增/蒸发在 pass 末统一应用）。
    struct WCell { int x, y, z; quint8 level; };
    std::vector<WCell> cells;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y) {
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Water)
                    cells.push_back({x, y, z, m_chunks.stateAt(x, y, z)});
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
    for (const WCell &c : cells) {
        if (evapKeys.count(keyOf(c.x, c.y, c.z))) continue;   // 退场中的格不扩散（断回填震荡）
        if (srcRegKeys.count(keyOf(c.x, c.y, c.z))) continue; // t224：升源格本 tick 不作流水扩散
        const int bk = belowKind(c.x, c.y, c.z);
        if (bk == 0) {
            // 下落：写下方为流水 level=1（**非源** —— 修 t174「下落成源灌满盆地」bug）。
            tryAdd(keyOf(c.x, c.y - 1, c.z), quint8(1));
        } else if (bk == 1 && c.level < kMaxFlowLevel) {
            // 水平蔓延：grounded 且未到最大流距 → 4 向邻居。air → 写 level+1；既有流水 → re-level 下调。
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
        // bk == 2（下方为水）：水下柱 —— 由该柱最底 grounded 格负责水平扩散，本格不动。
    }

    // 5) 应用：升源 → 蒸发 → 新增/重定级（三者经 keySet 互斥，顺序安全）。setWaterSilent 内部无变化
    //    → false → 稳态零重建。升源最前：升源格不在 evaps/evapKeys（pass 3 显式跳过）亦不在 adds（pass 4
    //    显式跳过 srcRegKeys），故先写 level 0 不会被后续覆盖。
    for (const WCell &s : srcRegs)
        setWaterSilent(s.x, s.y, s.z, BlockRegistry::Water, 0);
    for (const WCell &e : evaps)
        setWaterSilent(e.x, e.y, e.z, BlockRegistry::Air, 0);
    for (const auto &kv : adds) {
        const long long k = kv.first;
        const int x = static_cast<int>(k % W);
        const long long kz = k / W;
        const int z = static_cast<int>(kz % D);
        const int y = static_cast<int>(kz / D);
        setWaterSilent(x, y, z, BlockRegistry::Water, kv.second);
    }
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
    // t162：振幅 12→8（用户「太陡太过于陡峭」→ 更平缓少陡山），基线 28→30 → 地表 ~22..38。
    //   水位 24 仍相交（~22..24 低洼列见水），沙滩带 / 树·矿石阈值（waterLevel+1=25）同步成立。
    const double n = fbm((x + m_seed) * 0.09, (z + m_seed) * 0.09); // [-1,1]
    const int h = int(std::lround(30.0 + n * 8.0));                // ~22..38（t162 振幅减半更平缓）
    return std::max(0, h);
}

// t117 沙漠群系判定（二次 fBm biome，PLAN §2-K 确定性）：用与高度噪声**不同**的空间频率（0.018 vs
// 0.09）+ seed 偏移（+7919）独立采样同一 fBm → 群系图与高度图解耦（低洼 / 高地与干旱 / 普通独立）。
// 低频 → 大区块连续（沙丘成片，非逐格斑点，机制等价 MC 1.0 沙漠群系的大尺度分布）。阈值 0.25 →
// ~36% 干旱（fbm 近似正态，>0.25 约三分之一面积）。纯函数于 seed → 同 seed 同群系分布。
bool World::isDesert(int x, int z) const
{
    const double b = fbm((x + m_seed + 7919) * 0.018, (z + m_seed + 7919) * 0.018); // [-1,1]
    return b > 0.35; // t162：0.25→0.35 沙漠比例 ↓（用户「沙子太多、主要靠水边」；干旱整柱沙适度减少，沙滩带仍供沙）
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
    recomputeLightField(); // t151：地形 / 树定型后一次性算光场（worldgen 内 m_chunks.setBlock 直写不触此）
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
                if (BlockRegistry::isSolid(m_chunks.blockAt(x, y, z))) { firstOpaque = y; break; }
            }
            for (int y = firstOpaque + 1; y < H; ++y) {
                // 这些格必为非遮光（首个遮光之上）→ sky=15 种子；block 此时为 0（清场后）。
                m_chunks.setLight(x, y, z, 15, m_chunks.blockLightAt(x, y, z));
                skyQ.push({x, y, z});
            }
        }
    }

    // 种子 2 — 方块光（火把）：扫所有格，Torch → block=14（保留其天光值）。
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            for (int z = 0; z < D; ++z)
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Torch) {
                    m_chunks.setLight(x, y, z, m_chunks.skyLightAt(x, y, z), 14);
                    blockQ.push({x, y, z});
                }

    // BFS 天光传播：从种子向非遮光邻格衰减 1、取 max。
    while (!skyQ.empty()) {
        const Cell c = skyQ.front(); skyQ.pop();
        const quint8 cur = m_chunks.skyLightAt(c.x, c.y, c.z);
        if (cur <= 1) continue; // 衰减到 1 以下不再传播（下一步 = 0 无意义）
        const quint8 nv = quint8(cur - 1);
        for (const auto &d : dk) {
            const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue; // 越界跳过
            if (BlockRegistry::isSolid(m_chunks.blockAt(nx, ny, nz))) continue; // 遮光格不传光
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
        const quint8 nv = quint8(cur - 1);
        for (const auto &d : dk) {
            const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            if (BlockRegistry::isSolid(m_chunks.blockAt(nx, ny, nz))) continue;
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
void World::recomputeLightAround(int ex, int ey, int ez, quint8 oldId, quint8 newId)
{
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;
    if (ex < 0 || ey < 0 || ez < 0 || ex >= W || ey >= H || ez >= D) return;

    const bool opacityChanged = (BlockRegistry::isSolid(oldId) != BlockRegistry::isSolid(newId));
    const bool torchChanged = (oldId == BlockRegistry::Torch || newId == BlockRegistry::Torch);
    if (!opacityChanged && !torchChanged) return; // 光照无变化（如门开合：id 不变 → isSolid 不变、非火把）

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
    refloodBox(x0, y0, z0, x1, y1, z1, /*doSky=*/opacityChanged);
    // t155d：标记光盒覆盖的所有 chunk 脏 → setBlock 末 emit worldChanged 后这些 chunk **立即**重建
    //   （读新光场写顶点色），消除「编辑后邻 chunk 光照 / 火把光要等 2-3s 下个 sun-step 才刷新」。
    //   旧版只编辑 chunk 标脏（+ 边界邻接，为面剔除），光场虽即时更新（~1μs）但邻 chunk mesh 未重建 →
    //   陈旧到 sun-step。盒 31×31 横跨约 3~4 chunk → 9~16 chunk 重建（数 ms，单帧内可接受，远好于 2-3s）。
    constexpr int cs = 16; // Chunk::kSize
    for (int ccx = x0 / cs; ccx <= x1 / cs; ++ccx)
        for (int ccz = z0 / cs; ccz <= z1 / cs; ++ccz)
            if (Chunk *ch = m_chunks.chunk(ccx, ccz)) ch->markDirty();
    qInfo("vo.light: recomputeLightAround %lldus box=%dx%dx%d", t.elapsed(),
          x1 - x0 + 1, y1 - y0 + 1, z1 - z0 + 1); // t155c：实测（找卡顿根因）
}

// t154 有界盒清场 + 重 seed + 重 flood（recomputeLightAround 的实现核心）。盒外格作固定边界种子（衰减 1 流入），
//   盒内清零后从种子重传播 —— 等价于「以盒外为固定边界的盒内全量 re-flood」，结果与全局全量 re-flood 在盒内一致。
//
//   doSky=true（遮光变化）：两通道都重算。清两通道 → 重 seed 见天格(sky=15)+火把(block=14) → 边界种两通道 → flood 两通道。
//   doSky=false（仅火把增删）：天光不动。清方块光（保留天光）→ 重 seed 火把 → 边界种方块光 → flood 方块光。
//
//   边界种子：盒**表面格**的盒外邻（仅表面格有盒外邻，内部格无 —— 故只扫表面省功）。盒外邻值：y>=H → 天光 15
//   （开阔天空，与 skyLightAt OOB 同语义）；其余世界外（y<0 / x/z 越界）→ 0；盒内世界 → 其当前（未清）光值。
void World::refloodBox(int x0, int y0, int z0, int x1, int y1, int z1, bool doSky)
{
    const int W = m_width, D = m_depth, H = m_height;
    struct Cell { int x, y, z; };
    std::queue<Cell> skyQ, blockQ;
    static const int dk[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    auto inBox = [&](int x, int y, int z) {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1 && z >= z0 && z <= z1;
    };

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
                    if (BlockRegistry::isSolid(m_chunks.blockAt(x, y, z))) { firstOpaque = y; break; }
                for (int y = firstOpaque + 1; y < H; ++y) {
                    if (y < y0 || y > y1) continue;
                    m_chunks.setLight(x, y, z, 15, m_chunks.blockLightAt(x, y, z));
                    skyQ.push({x, y, z});
                }
            }
        }
    }

    // 3. 盒内重 seed 方块光：火把格 block=14（保留其天光值）。无论 doSky（火把光始终重 flood）。
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Torch) {
                    m_chunks.setLight(x, y, z, m_chunks.skyLightAt(x, y, z), 14);
                    blockQ.push({x, y, z});
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
        const quint8 curSky = m_chunks.skyLightAt(x, y, z);
        const quint8 curBlock = m_chunks.blockLightAt(x, y, z);
        const bool opaque = BlockRegistry::isSolid(m_chunks.blockAt(x, y, z));
        if (doSky && s > 0) {
            const quint8 in = quint8(s - 1);
            if (!opaque && in > curSky) { // 衰减 1 流入；遮光格不进光
                m_chunks.setLight(x, y, z, in, m_chunks.blockLightAt(x, y, z));
                skyQ.push({x, y, z});
            }
        }
        if (b > 0) {
            const quint8 in = quint8(b - 1);
            if (!opaque && in > curBlock) {
                m_chunks.setLight(x, y, z, m_chunks.skyLightAt(x, y, z), in);
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

    // 5. BFS 天光传播（仅 doSky）：从种子向盒内非遮光邻格衰减 1、取 max；盒外邻不传（其值固定，已作边界种子流入）。
    if (doSky) {
        while (!skyQ.empty()) {
            const Cell c = skyQ.front(); skyQ.pop();
            const quint8 cur = m_chunks.skyLightAt(c.x, c.y, c.z);
            if (cur <= 1) continue;
            const quint8 nv = quint8(cur - 1);
            for (const auto &d : dk) {
                const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
                if (!inBox(nx, ny, nz)) continue;
                if (BlockRegistry::isSolid(m_chunks.blockAt(nx, ny, nz))) continue;
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
        const quint8 nv = quint8(cur - 1);
        for (const auto &d : dk) {
            const int nx = c.x + d[0], ny = c.y + d[1], nz = c.z + d[2];
            if (!inBox(nx, ny, nz)) continue;
            if (BlockRegistry::isSolid(m_chunks.blockAt(nx, ny, nz))) continue;
            if (nv > m_chunks.blockLightAt(nx, ny, nz)) {
                m_chunks.setLight(nx, ny, nz, m_chunks.skyLightAt(nx, ny, nz), nv);
                blockQ.push({nx, ny, nz});
            }
        }
    }
}
