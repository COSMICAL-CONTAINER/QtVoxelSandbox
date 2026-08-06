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
    // t273 修复(b)：放水源（id==Water）时 poke 水流节流计数 → 下次 tickWaterFlow 即蔓延（详见 5 参数 setBlock 同名注释）。
    if (id == BlockRegistry::Water)
        m_flowTickCounter = kFlowTickInterval - 1;
    // t343：放岩浆源（id==Lava）时 poke 岩浆流节流计数 → 下次 tickLavaFlow 即蔓延（机制同水 poke）。
    if (id == BlockRegistry::Lava)
        m_lavaFlowTickCounter = kLavaFlowTickInterval - 1;
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
    if (m_batchFluid) return true; // t350 流体 tick 批量写：累积栅格写 + 重光照，末尾由 caller 统一 emit + clearDirty
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
    if (anyChange) {
        emit worldChanged();       // 一次重建（terrain/water 两段各检各的 dirty → 仅脏 chunk 重建）
        m_chunks.clearAllDirty();  // 两段重建完统一清脏（同逐格路径的 emit→clear 顺序）
    }
}

// t343 岩浆流 tick（见 world.h 头注释）。机制等价 MC 1.0 主世界岩浆：比水慢 ~30 倍、扩散 3 格、无源再生。
//   算法同 tickWaterFlow 的增量波前（快照 → 蒸发 → 扩散 → 应用），但去掉源再生 pass（岩浆不形成无限源）、
//   节流更慢（kLavaFlowTickInterval=30 → 3s/格）、扩散更短（kMaxLavaFlowLevel=3）。末尾 ignite pass 焚毁邻岩浆木类。
void World::tickLavaFlow()
{
    if (++m_lavaFlowTickCounter < kLavaFlowTickInterval) return; // 节流：每 30 tick（~3s）把波前推进 1 格
    m_lavaFlowTickCounter = 0;
    ++m_lavaIgniteIndex; // ignite 窗口序号（每流 tick +1，喂散布概率 → 错峰焚毁）
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    auto keyOf = [W, D](int x, int y, int z) -> long long {
        return static_cast<long long>(x) + static_cast<long long>(z) * W
             + static_cast<long long>(y) * static_cast<long long>(W) * D;
    };

    // 1) 快照当前岩浆格。
    struct LCell { int x, y, z; quint8 level; };
    std::vector<LCell> cells;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y) {
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Lava)
                    cells.push_back({x, y, z, m_chunks.stateAt(x, y, z)});
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

    // 3) 扩散 pass：只把波前推 1 格（1 格/tick 缓慢动画）。跳过退场格。下落 + 水平蔓延不互斥（同 tickWaterFlow t272）。
    for (const LCell &c : cells) {
        if (evapKeys.count(keyOf(c.x, c.y, c.z))) continue; // 退场中的格不扩散
        const int bk = belowKind(c.x, c.y, c.z);
        if (bk == 0) {
            tryAdd(keyOf(c.x, c.y - 1, c.z), quint8(1)); // 下落为流岩浆 state=1（非源）
        }
        if (bk != 2 && c.level < kMaxLavaFlowLevel) {
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
    for (const LCell &e : evaps)
        setWaterSilent(e.x, e.y, e.z, BlockRegistry::Air, 0);
    for (const auto &kv : adds) {
        const long long k = kv.first;
        const int x = static_cast<int>(k % W);
        const long long kz = k / W;
        const int z = static_cast<int>(kz % D);
        const int y = static_cast<int>(kz / D);
        setWaterSilent(x, y, z, BlockRegistry::Lava, kv.second);
    }

    // 5) ignite pass（spec「木质方块邻岩浆概率着火焚毁」）：遍历本 tick 岩浆格，扫 6 邻，木类方块按散布概率焚毁。
    //    散布确定性（hashVoxel + 窗口序号，PLAN §2-K）→ 同 seed 同窗口同焚毁，错峰非全部同步烧光。setBlock Air
    //    发 blockBroken → 触发破块粒子 / 音（机制等价 MC 木块被岩浆点燃焚毁）。t344 完整着火系统留后续。
    auto isWoodLike = [](quint8 id) -> bool {
        using BR = BlockRegistry;
        return id == BR::Log || id == BR::Planks || id == BR::CraftingTable || id == BR::Leaves
            || id == BR::WoodSlab || id == BR::WoodStairs || id == BR::WoodFence
            || id == BR::WoodPressurePlate || id == BR::WoodDoor || id == BR::WoodTrapdoor || id == BR::Chest;
    };
    for (const LCell &c : cells) {
        const int neigh[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto &n : neigh) {
            const int nx = c.x + n[0], ny = c.y + n[1], nz = c.z + n[2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= W || ny >= H || nz >= D) continue;
            const quint8 nb = m_chunks.blockAt(nx, ny, nz);
            if (!isWoodLike(nb)) continue;
            // 散布概率判定（hashVoxel + 窗口序号 → 确定性伪随机，PLAN §2-K）。命中即焚毁。
            const quint32 hv = hashVoxel(m_seed ^ 0x1A7A, nx, ny, nz) ^ (quint32(m_lavaIgniteIndex) * 2654435761u);
            if ((hv % 100u) < unsigned(kLavaIgnitePct)) {
                setBlock(nx, ny, nz, BlockRegistry::Air); // 焚毁（发 blockBroken → 破块粒子/音）
            }
        }
    }
}

// t236 小麦作物生长 tick（见 world.h 头注释）。机制等价 MC 1.0 小麦生长（random-tick 式散布概率升阶段）。
//   节流到 ~每 kCropTickInterval tick（2.5s）做一次成长判定窗口；每窗遍历全图作物格，符合条件者按确定性散布
//   概率升 state 一档（0→1→…→WheatCropStageMax=7 成熟）。写入走 setWaterSilent（静默 state 写，无破/放反馈）。
void World::tickCropGrowth()
{
    if (++m_cropTickCounter < kCropTickInterval) return; // 节流：每 kCropTickInterval tick（~2.5s）做一次判定
    m_cropTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 1) 快照当前作物格 + 阶段（tick 内栅格不变 —— 升阶段在 pass 末统一应用，避免半遍历态读到刚升的阶段）。
    struct CCell { int x, y, z; quint8 stage; };
    std::vector<CCell> cells;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y) {
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::WheatCrop)
                    cells.push_back({x, y, z, m_chunks.stateAt(x, y, z)});
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
        // 确定性散布概率：纯函数于 seed + 位置 + 窗口序号（PLAN §2-K 精神：worldgen 确定性；此处生长模拟亦
        //   走确定性哈希，无 Math.random / 时间源 → 同 seed 同窗口序号下结果一致，便于复现）。全 int 运算
        //   避免符号转换告警（hashVoxel 参数为 int）。
        const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_cropIntervalIndex) * 0x9E3779B9u));
        const int hy = c.y * 7 + int(c.stage);
        const quint32 h = hashVoxel(mixedSeed, c.x, hy, c.z);
        if (int(h & 0xFFFFu) % 100 >= kCropGrowPct) continue;             // 散布落空 → 本窗不升
        grows.push_back(c);
    }

    // 3) 应用升阶段（静默写：setWaterSilent 支持任意 id+state；作物升阶段是系统模拟，无破/放反馈）。
    //    setWaterSilent 内部对「无变化」早退（oldId==newId && oldState==newState）；但作物升阶段 stage→stage+1
    //    必有变化 → 每株触发一次 emit worldChanged。无作物可升时 grows 为空 → 零写入、零 worldChanged（稳态无开销）。
    //    注：逐株 emit worldChanged 会导致多株同窗升阶段时多次 mesh 重建请求 —— 25 个 ChunkGeometry 各检各的 dirty，
    //    仅含升阶段作物的 chunk 真正重建（per-chunk dirty 协作，见 lessons-learned t03），故实际重建 = 受影响 chunk 数。
    for (const CCell &g : grows)
        setWaterSilent(g.x, g.y, g.z, BlockRegistry::WheatCrop, quint8(g.stage + 1));

    ++m_cropIntervalIndex; // 窗口序号 +1（喂入下次散布哈希 → 不同窗口不同株错峰）
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
                if (m_chunks.blockAt(lx, ly, lz) != BlockRegistry::Leaves) continue;
                if ((m_chunks.stateAt(lx, ly, lz) & BlockRegistry::PersistentLeafBit) != 0)
                    continue; // t305 玩家放置叶（持久）不衰
                // 查 kDecayRadius 切比雪夫距离内有无原木（任一命中即保留 —— 仍有原木支撑）。
                bool hasLog = false;
                for (int ox = -kDecayRadius; ox <= kDecayRadius && !hasLog; ++ox) {
                    for (int oy = -kDecayRadius; oy <= kDecayRadius && !hasLog; ++oy) {
                        for (int oz = -kDecayRadius; oz <= kDecayRadius && !hasLog; ++oz) {
                            if (m_chunks.blockAt(lx + ox, ly + oy, lz + oz) == BlockRegistry::Log) {
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
    if (m_decayingLeaves.empty()) return;                       // 稳态（无失撑叶）→ 零开销早退
    if (++m_leafDecayTickCounter < kLeafDecayTickInterval) return; // 节流：每 kLeafDecayTickInterval tick（~0.4s）开一窗
    m_leafDecayTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 散布种子混入窗口序号（每窗一新种子 → 每叶每窗一新伪随机滚，渐进渐退；全 int 运算避符号转换告警）。
    const int mixedSeed = int(quint32(m_seed) ^ (quint32(m_leafDecayIntervalIndex) * 0x9E3779B9u));

    // 逐叶判定：队列项可能已被他途清除（玩家破叶 / 爆炸 / 重置）→ 块非 Leaves 即视为已消失，出队（不再衰减）。
    //   命中叶先记坐标（decayed）+ 累计受影响 AABB，末尾批量写 + 一次 reflood + worldChanged。
    std::vector<quint64> decayed;
    int minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
    bool any = false;
    for (auto it = m_decayingLeaves.begin(); it != m_decayingLeaves.end(); ) {
        int lx, ly, lz;
        unpackLeafCell(*it, lx, ly, lz);
        if (m_chunks.blockAt(lx, ly, lz) != BlockRegistry::Leaves) {
            it = m_decayingLeaves.erase(it); // 已被他途清除 → 出队
            continue;
        }
        const quint32 h = hashVoxel(mixedSeed, lx, ly, lz);
        if (int(h & 0xFFFFu) % 100 < kLeafDecayPct) {
            decayed.push_back(*it);                       // 本窗命中 → 待清
            if (!any) { minX = maxX = lx; minY = maxY = ly; minZ = maxZ = lz; any = true; }
            else {
                if (lx < minX) minX = lx; if (lx > maxX) maxX = lx;
                if (ly < minY) minY = ly; if (ly > maxY) maxY = ly;
                if (lz < minZ) minZ = lz; if (lz > maxZ) maxZ = lz;
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
                if (b == BlockRegistry::Air || b == BlockRegistry::Bedrock || b == BlockRegistry::Water)
                    continue; // 空气 / 基岩 / 水不破坏（机制等价 MC 爆炸：不毁水体、不破基岩）
                m_chunks.setBlock(bx, by, bz, BlockRegistry::Air); // 直写 + 标脏（含跨 chunk 边界邻接脏），不 emit
                destroyed.push_back({bx, by, bz, b});
                if (!any) { minX = maxX = bx; minY = maxY = by; minZ = maxZ = bz; any = true; }
                else {
                    if (bx < minX) minX = bx; if (bx > maxX) maxX = bx;
                    if (by < minY) minY = by; if (by > maxY) maxY = by;
                    if (bz < minZ) minZ = bz; if (bz > maxZ) maxZ = bz;
                }
            }
    if (destroyed.empty()) return destroyed;
    // 末尾统一：1 次 refloodBox 重算光场（球外接盒扩 1 格余量，doSky=true 两通道都算 —— 破坏的多为
    //   solid 遮光块，天光列随之变化）+ 1 次 emit worldChanged + 1 次 clearAllDirty（机制同 decayLeavesAround）。
    const int bx0 = std::max(0, minX - 1), by0 = std::max(0, minY - 1), bz0 = std::max(0, minZ - 1);
    const int bx1 = std::min(m_width - 1, maxX + 1), by1 = std::min(m_height - 1, maxY + 1), bz1 = std::min(m_depth - 1, maxZ + 1);
    refloodBox(bx0, by0, bz0, bx1, by1, bz1, /*doSky=*/true);
    emit worldChanged();
    m_chunks.clearAllDirty();
    qInfo("vo.edit: explosion destroyed = %d (center %d,%d,%d r=%g)",
          int(destroyed.size()), cx, cy, cz, double(radius)); // 可观测：一次爆炸的破坏块数
    return destroyed;
}

// t305 树苗生长 tick（见 world.h 头注释）。机制等价 MC 1.0 树苗生长（random-tick 式散布概率）。
//   节流到 ~每 kSaplingTickInterval tick（5s）做一次成长判定窗口；每窗遍历全图树苗格，符合条件者按
//   确定性散布概率长成 → 清除树苗 + 在原位生成完整橡树（placeTreeAt 主干 + 树叶球冠）。
void World::tickSaplingGrowth()
{
    if (++m_saplingTickCounter < kSaplingTickInterval) return; // 节流：每 kSaplingTickInterval tick（~5s）判一次
    m_saplingTickCounter = 0;
    const int W = m_width, D = m_depth, H = m_height;
    if (W <= 0 || D <= 0 || H <= 0) return;

    // 1) 快照当前树苗格（tick 内栅格不变 —— 生长在 pass 末统一应用，避免半遍历态读到刚生成的树）。
    struct SCell { int x, y, z; int trunkH; };
    std::vector<SCell> cells;
    for (int x = 0; x < W; ++x)
        for (int z = 0; z < D; ++z)
            for (int y = 0; y < H; ++y)
                if (m_chunks.blockAt(x, y, z) == BlockRegistry::Sapling)
                    cells.push_back({x, y, z, 0});

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
    // t306：原 plains 候选带（b ∈ [-0.4,0.5]）用第二条独立低频 fBm 把 forest 从草原里 carve 出来。
    //   独立频率 0.020 + seed 偏移 +977（与主群系图 0.012/+3571、高度图 0.09 均不同）→ 森林图与三者解耦；
    //   低频 → 森林成片（非逐格斑点，机制等价 MC 1.0 森林群系大尺度分布）。
    //   t373：阈值 0.15→0.40（fbm 近似正态居中 0）。旧 0.15 实测森林吞没草原（草原几乎不可见），
    //   因 4 阶 fbm 实际分布比名义 [-1,1] 收窄、0.15 已落入正半区主流段 → 森林占比偏高。提至 0.40
    //   把森林压成少数（候选带内 ~15-20%），草原重新成为大片开阔地带（spec「大草原」原意）；森林仍
    //   成片共存（spec「森林+草原」二者共存，森林不消失）。纯函数于 seed → 同 seed 同 forest/plains 划分（PLAN §2-K）。
    const double f = fbm((x + m_seed + 977) * 0.020, (z + m_seed + 977) * 0.020); // [-1,1]
    return (f > 0.40) ? Biome::Forest : Biome::Plains;
}

// t117/t274 沙漠群系判定：收口到 biomeAt == Desert（单一权威）。旧 t117 独立 fBm（0.018/+7919/0.35）
//   已由 t274 biomeAt 统一 —— 现沙漠分布随群系图（0.012/+3571）走，新世界生效（旧存档走 chunk blob
//   不受影响，spec 明示）。供 generate（沙表层）/ placeTrees / placeTallGrass 跳过沙漠列。
bool World::isDesert(int x, int z) const
{
    return biomeAt(x, z) == Biome::Desert;
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
    int desertCols = 0, seaCols = 0, plainsCols = 0, hillsCols = 0, forestCols = 0;
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
            // t255：沙漠表层沙厚度 4..6 格（hashColumn 低 2 位派生，PLAN §2-K 确定性；非沙漠列置 0 不用）。
            const int desertSandThickness = desert ? (4 + int(hashColumn(m_seed, x, z) % 3u)) : 0;
            if (desert) ++desertCols;
            else if (inSandSea) ++seaCols;
            // t306：森林地表仍为草（机制等价 MC 森林地表草地），仅树/草密度分化 → surface 填充无需分流 forest。
            if (bio == Biome::Plains) ++plainsCols;
            else if (bio == Biome::Hills) ++hillsCols;
            else if (bio == Biome::Forest) ++forestCols;
            for (int y = 0; y <= h; ++y) {
                quint8 b;
                if (inSandSea) {
                    // t338 海域：沙表层（海底 / 沙滩）+ Dirt + Stone（机制等价 MC 海岸沙 + 水下沙底）。沙海盘优先于
                    //   群系（海覆盖任何群系，统一沙底）；fillWater 随后在海盆（h<waterLevel）灌满海水到海平面。
                    if (y == h)          b = BlockRegistry::Sand;  // 沙表层（海底 / 沙滩）
                    else if (y >= h - 2) b = BlockRegistry::Dirt;  // 表层下土
                    else                 b = BlockRegistry::Stone; // 深石
                } else if (desert) {
                    // t255：仅表层 desertSandThickness 格沙（h-y < thickness），下接 Stone（修旧整柱沙贯穿基岩 bug）。
                    b = (h - y < desertSandThickness) ? BlockRegistry::Sand : BlockRegistry::Stone;
                } else {
                    if (y == h)          b = BlockRegistry::Grass; // 草地表层
                    else if (y >= h - 2) b = BlockRegistry::Dirt;  // 土
                    else                 b = BlockRegistry::Stone; // 石
                }
                m_chunks.setBlock(x, y, z, b);
            }
        }
    }
    // t274/t306：群系分布可观测（plains 应为多数 / forest 次之 / hills + desert 少数）；同 seed → 同分布（确定性核对）。
    qInfo() << "worldgen: biomes plains =" << plainsCols << "forest =" << forestCols
            << "hills =" << hillsCols << "desert =" << desertCols
            << "sea/beach =" << seaCols
            << "/" << (m_width * m_depth);

    placeBedrock(); // t119：底层基岩（y 0..4 坑洼，底实顶疏；不可破坏）。先于矿石 / 树（仅覆盖最底几格）
    scatterOres(); // 地形填充后确定性散布矿石（stone 区段，t84；先于树木，树只动地表空气无冲突）
    carveCaves(); // t278：洞穴隧道 carve（terrain + 矿石之后；挖走 stone/dirt/ore 暴露矿石于洞壁 → 为 t279 铺路）。
                  //   先于填水 → 水只填地表低洼列（h+1..waterLevel），不灌地下洞穴；先于树/草 → 表面特征放于完整地表。
    carveCaveEntrances(); // t341：山坡洞口（carveCaves 之后 → 连通既有洞穴网络；先于地下水 → 洞口路径干净；先于填水
                          //   / 树 / 草 → 洞口刻在完整地表）。仅该列近表有真实洞穴 air 才开口 → 永不产孤立竖井。
    placeUndergroundWaterPools(); // t309：封闭地下水池（carveCaves / 入口之后；先于填水 → 地表海平面与地下水各自独立）。
    placeLavaLakes(); // t343：地下封闭岩浆湖（Y<30；carveCaves 之后 → 湖独立于洞穴；先于填水 → 岩浆不与海水冲突）。
    carveCanyon(); // t342：大峡谷（caves/ores 之后 → 峡壁既有矿石层被 carve 暴露；先于填水 → 内陆干涸峡谷，
                   //   fillWater 仅填海域故不灌峡谷；先于树/草 → placeTrees/placeTallGrass 据「草顶」守卫天然跳过峡谷列）。
    fillWater(); // t148：海平面以下低洼列填水（地形之上；先于树木 → 水占格使树不生于水中，setVoxelIfAir 守）
    placeSurfaceLakes(); // t309：地表小湖泊（fillWater 之后 → 湖独立于海；先于树 / 草 → 树 / 草据「草顶」守卫跳过湖列）。
    placeTrees(); // 地形填充后确定性种树（grass 表层，PLAN §2-K）
    placeTallGrass(); // t235：grass 表层上方确定性散布草丛（PLAN §2-K；树定型后，仅写空气格不覆盖树）
    recomputeLightField(); // t151：地形 / 树 / 草丛定型后一次性算光场（worldgen 内 m_chunks.setBlock 直写不触此）
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

    int placed = 0;
    for (int z = 0; z < m_depth; ++z) {
        for (int x = 0; x < m_width; ++x) {
            const int surfaceY = heightAt(x, z);
            // t149：水位阈值取代旧 kSandLevel=3 —— 沙滩带(wl±1)/水下(h<wl)/低洼不种树（机制等价 MC 树不生于沙滩/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            const Biome bio = biomeAt(x, z);
            if (bio == Biome::Desert) continue;  // t117 沙漠群系不种树（机制等价 MC 沙漠无树）
            // t309：跳过非草顶列（地表湖水面 / 洞穴入口竖井顶等已把草替换 → 不种树；机制等价 MC 树仅生于草地）。
            //   读栅格当前方块（heightAt 是 worldgen 高度、不含 t309 改动）→ 湖列水面 / 洞口 air 皆被本守卫拦截。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::Grass) continue;

            // t306 群系分流密度：forest 密闭 / plains 稀疏 / hills 零星。
            const unsigned densityPct = (bio == Biome::Forest) ? unsigned(kForestTreePct)
                                        : (bio == Biome::Plains) ? unsigned(kPlainsTreePct)
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

            // leafRand = 哈希高位 → 驱动树冠四角叶随机（每棵轮廓各异）；与密度(低位)/高度(>>8)字段不重叠。
            placeTreeAt(x, surfaceY, z, trunkH, r >> 16);
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
    constexpr int kHillsGrassPct  = 12; // 山地稀疏（裸岩 / 林感）

    // t310 各群系草变种配比（矮 / 中 / 高，% ；累积阈值见下方 vr 判定）。plains 矮/中为主、forest 林下茂盛多
    //   中/高、hills 矮草为主（裸岩稀疏）。机制等价 MC 群系草高分化（草原短草 / 森林高草）。
    struct VarMix { int shortPct, mediumPct; }; // 高草 = 100 - short - medium 兜底
    constexpr VarMix kPlainsMix = { 55, 35 }; // plains：55% 矮 / 35% 中 / 10% 高
    constexpr VarMix kForestMix = { 20, 40 }; // forest：20% 矮 / 40% 中 / 40% 高（林下茂盛）
    constexpr VarMix kHillsMix  = { 65, 25 }; // hills：65% 矮 / 25% 中 / 10% 高

    int placed = 0;
    int plainsCols = 0, hillsCols = 0, forestCols = 0;
    for (int x = 0; x < m_width; ++x) {
        for (int z = 0; z < m_depth; ++z) {
            const int surfaceY = heightAt(x, z);
            // 与 placeTrees 同阈值：沙滩带(wl±1)/水下(h<wl)/低洼不生草丛（机制等价 MC 草丛不生于沙/水下）。
            if (surfaceY <= kWaterLevel + 1) continue;
            const Biome bio = biomeAt(x, z);
            if (bio == Biome::Desert) continue; // t117/t274 沙漠群系不生草丛（机制等价 MC 沙漠无草）
            // t309：跳过非草顶列（地表湖 / 洞口顶把草替换 → 草丛不生于水面 / 洞口；机制等价 MC 草丛仅生于草地）。
            if (m_chunks.blockAt(x, surfaceY, z) != BlockRegistry::Grass) continue;

            // t274/t306 群系分流密度：plains 密集 / forest 适中 / hills 稀疏。
            const unsigned densityPct = (bio == Biome::Plains) ? unsigned(kPlainsGrassPct)
                                        : (bio == Biome::Forest) ? unsigned(kForestGrassPct)
                                                                 : unsigned(kHillsGrassPct);
            const quint32 r = hashColumn(m_seed, x, z);
            if (r % 100u >= densityPct) continue; // 密度筛选

            // t310 草变种：独立哈希位段 (r>>16)%100 选矮/中/高（与密度位段 r%100 解耦）。
            const VarMix mix = (bio == Biome::Plains) ? kPlainsMix
                             : (bio == Biome::Forest) ? kForestMix
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
            else ++hillsCols;
        }
    }
    qInfo() << "worldgen: tall grass placed =" << placed
            << "(plains" << plainsCols << "/ forest" << forestCols
            << "/ hills" << hillsCols << ")"; // 同 seed → 同计数（确定性核对）
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

// 确定性矿石散布（t84/t279/t308，PLAN §2-K）：遍历 stone 区段（generate 把 y<h-2 的格填 Stone，沙漠/沙滩表层
//   除外），按 hashVoxel(seed,x,y,z) 的不同位段做密度筛选 → 替换为煤矿 / 铜矿 / 铁矿 / 金矿 / 钻石矿。
//   **高度分层**（机制等价 MC 1.0 矿物随深度分层 + spec t308「铜铁金按序更稀少」）：
//     - 钻石（diamond_ore）：深层 y∈[kDiamondMin=5, kDiamondMax=40]（紧贴基岩 kBedrockTop=4 之上）。
//       密度最低（稀有，0.4%）。**t308 深度修正**：上界 16→40（用户 research 后定，地表 ~62、洞穴贯穿深层 →
//       深挖更易见钻矿石；密度仍最低故整体稀有度不变）。需铁镐（minTier3）。
//     - 金（gold_ore）：深层 y∈[kOreMin=5, kGoldMax=25]（机制等价 MC 金矿深层富集）。密度次低（稀有，0.5%）。
//       金属族中最稀有（spec「铜铁金按序更稀少」→ 金最稀有）。需铁镐（minTier3）。掉金原矿→熔炉烧金锭。
//     - 铁（iron_ore）：中层 y∈[kOreMin=5, kIronMax=30]（机制等价 MC 铁矿中下层富集）。中等密度（0.7%）。
//       需石镐（minTier2）。掉铁原矿→熔炉烧铁锭。
//     - 铜（copper_ore）：浅中层 y∈[kOreMin=5, kCopperMax=45]（机制等价 MC 铜矿浅中层富集）。密度次高（0.9%）。
//       金属族中最常见 / 最浅（spec「铜铁金按序更稀少」→ 铜最常见）。需石镐（minTier2）。掉铜原矿→熔炉烧铜锭。
//     - 煤（coal_ore）：浅层 y∈[kCoalMin=8, stoneTop]（机制等价 MC 煤矿靠近地表富集）。最高密度（1.0%）。
//       木镐可挖（minTier1）。直接掉煤炭（燃料 / 火把原料，无需冶炼）。
//   判定用两路独立哈希（r = hashVoxel(seed,...) 给钻石/铁/煤沿用旧位段 0/8/16，保旧矿脉分布；r2 = hashVoxel
//   (seed^黄金比例常量,...) 给金/铜独立流）→ 5 矿各自独立。判定序（重叠区稀有矿优先）：钻石 > 金 > 铁 > 铜 > 煤
//   （先中者胜、一格至多一矿）。仅替换 Stone；同 seed → 同矿脉分布；禁用任何运行期随机源（QTime/时钟/全局 RNG）。
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
    constexpr int kIronMax     = 30;  // 铁上界 y（机制等价 MC 铁矿中下层富集）
    constexpr int kCopperMax   = 45;  // 铜上界 y（t308；机制等价 MC 铜矿浅中层富集；金属族最浅）

    // 密度（/10000，每体素命中概率）：钻石最稀 < 金 < 铁 < 铜 < 煤（最常见）。
    //   spec t308「铜铁金按序更稀少」→ 铜(0.9%) > 铁(0.7%) > 金(0.5%)；钻石(0.4%) / 煤(1.0%) 各为两端。
    //   洞穴 carve 暴露后矿脉出露更可见（spec「洞穴裸露矿物」）；密度调到「分层肉眼可辨 + 不过密糊洞壁」。
    constexpr unsigned kDiamondPct = 40;   // /10000 → 0.4%（钻石，需铁镐 minTier3；稀有深层）
    constexpr unsigned kGoldPct    = 50;   // /10000 → 0.5%（金，需铁镐 minTier3；金属族最稀有，t308）
    constexpr unsigned kIronPct    = 70;   // /10000 → 0.7%（铁，需石镐 minTier2；中层）
    constexpr unsigned kCopperPct  = 90;   // /10000 → 0.9%（铜，需石镐 minTier2；金属族最常见 / 最浅，t308）
    constexpr unsigned kCoalPct    = 100;  // /10000 → 1.0%（煤，木镐可挖 minTier1；浅层最常见）

    int coalPlaced = 0, copperPlaced = 0, ironPlaced = 0, goldPlaced = 0, diamondPlaced = 0;
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
            << "diamond" << diamondPlaced; // 同 seed → 同计数（确定性核对）
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
    int wormSteps = 0;
    for (size_t wi = 0; wi < worms.size(); ++wi) {
        Worm &w = worms[wi];
        int step = 0;
        while (w.life > 0) {
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
    //   t376：同时记录路径中心（ix,iz + surfaceY + 朝向 yaw），供 carve 后三个 post-pass 用：
    //   (1) 排水带 —— 排干峡谷带内残余水（兜底盘外边缘池水渗出）；(2) 邻接侧洞 —— 沿峡壁刻短隧道连既有洞穴；
    //   (3) 单点高源瀑布 —— 一格水源高悬峡心柱，t350 限流下成细瀑布 + 小水洼（点缀非泛滥）。
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

    // t376 (1) 排水带：盘外（半径 > 当前盘半径）仍可能有地下水池残水紧贴峡壁 → 暴露后渗出。沿路径中心以
    //   固定半径（盘上限 + 余量）逐柱排干 [kFloor, surfaceY] 内的水格 → 峡谷带内无水可渗。池水远端（带外）
    //   仍被实体岩封闭（稳态）。一次 worldgen 开销可接受。
    constexpr int kDrainRadius = kBaseRadius + kTopExtra + 2; // 盘上限 (~6) + 2 余量 → 8
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

    // t376 (3) 单点高源瀑布点缀：取路径 ~1/3 处一格水源，高悬于峡心柱（其下全程峡谷空气）。t350 限流下
    //   仅垂直下落成细瀑 + 落点 grounded 后水平蔓延 ≤kMaxFlowLevel 格 = 小水洼（点缀非泛滥）。span 太小
    //   （无落差）则跳过。最后放置，避免被排水带 / 侧洞覆盖。
    int waterfallY = -1;
    if (!path.empty()) {
        const size_t wi = path.size() / 3;
        const CanyonPt &wp = path[wi];
        if (wp.span >= 6) { // 至少 6 格落差才有「瀑布」观感
            waterfallY = kFloor + (wp.span * 3) / 4; // 高位（距底 3/4 跨度），其下峡谷空气 → 细瀑
            if (waterfallY < m_height && m_chunks.blockAt(wp.ix, waterfallY, wp.iz) == BlockRegistry::Air)
                m_chunks.setBlock(wp.ix, waterfallY, wp.iz, BlockRegistry::Water); // 源（state 默认 0）
        }
    }

    qInfo() << "worldgen: grand canyon carved =" << carvedVoxels
            << "(steps" << steps << "floor" << kFloor << ")"; // 同 seed → 同计数（确定性核对）
    qInfo() << "worldgen: canyon drained =" << drainedCells
            << "side caves =" << sideCaves
            << "waterfall y =" << waterfallY; // t376 确定性核对
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
    // t375「湖太少」：网格 18→12、命中 25→50 → 候选密度约 4.5×（密度 ∝ pct/grid²）。仅放宽候选密度，
    //   不动「局部低洼」几何判定（湖盆有效性须保留，否则斜坡上的水会流空 → 坏湖）。同 seed 仍确定。
    constexpr int kLakeGrid     = 12;      // 候选网格间距（t375：18→12 加密采样）
    constexpr unsigned kLakePct = 50u;     // 候选命中概率（t375：25→50 翻倍）
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

    // 种子 2 — 方块光（发光方块）：扫所有格，lightEmission>0（火把=14 / 岩浆=15）→ block=该值（保留天光）。
    //   t351：岩浆自发光 15，地底岩浆湖照亮封闭洞穴（MC 1.0 岩浆光 level 15）。沿用 BlockRegistry::lightEmission
    //   单一权威（火把/岩浆/未来发光方块均经此），消除「每加一个光源改一处种子」回归类。
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
            for (int z = 0; z < D; ++z) {
                const quint8 emission = BlockRegistry::lightEmission(m_chunks.blockAt(x, y, z));
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
    const bool lightSourceChanged = (BlockRegistry::lightEmission(oldId) > 0 || BlockRegistry::lightEmission(newId) > 0);
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

    // 3. 盒内重 seed 方块光：发光格（lightEmission>0：火把=14 / 岩浆=15）→ block=该值（保留天光）。无论 doSky。
    //   t351：岩浆自发光 15，流/凝时（tickLavaFlow → setWaterSilent → recomputeLightAround）须重 flood 其方块光。
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                const quint8 emission = BlockRegistry::lightEmission(m_chunks.blockAt(x, y, z));
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
}
