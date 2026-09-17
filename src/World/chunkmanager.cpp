#include "chunkmanager.h"

#include <algorithm>

#include "blockregistry.h" // t360 solidTopOffset（列顶实面高度；PCF 软影按方块真实模型高度判遮挡）

ChunkManager::ChunkManager(int width, int depth, int height)
{
    recreate(width, depth, height);
}

void ChunkManager::recreate(int width, int depth, int height)
{
    m_width = std::max(0, width);
    m_depth = std::max(0, depth);
    m_height = std::max(0, height);
    m_chunksX = (m_width + kSize - 1) / kSize;  // ceil：48→3；非整除时末列覆盖到 kSize 但世界越界判空
    m_chunksZ = (m_depth + kSize - 1) / kSize;
    // unique_ptr 是 move-only：不能用 assign(count, value)（需拷贝）。先 resize（默认构造=null），
    // 再逐格 move 赋值新 chunk。
    m_chunks.clear();
    m_chunks.resize(size_t(m_chunksX * m_chunksZ));
    // R20.10 生命周期侧表同步重建：全表 Loaded（默认稳态 = 常驻已加载——等价本单之前「全部
    //   chunk 永久驻留」的现行行为，plan 验收第一条「当前固定 10×10 世界仍可运行」的零变化基线；
    //   beginLoad 的零填充分区网格同样落 Loaded——加载窗语义与旧版无态时期一致，store 豁免域
    //   不受影响）。此后仅 setLifecycle 可写。
    m_lifecycle.clear();
    m_lifecycle.assign(size_t(m_chunksX * m_chunksZ), ChunkLifecycle::Loaded);
    // §29.5-W3：persist 域未落盘编辑集合随网格重建清空（旧键不指向当前栅格——侧表族清空同门）。
    m_unsavedEdits.clear();
    for (int cz = 0; cz < m_chunksZ; ++cz)
        for (int cx = 0; cx < m_chunksX; ++cx)
            // R20.05：网格索引公式经 ChunkKey::flatIndex 类型化包装（逐位同式 cx + chunksX*cz；
            //   布局零变化——本单只立类型，r2005d 源码钉摘即红）。
            m_chunks[size_t(ChunkKey{ cx, cz }.flatIndex(m_chunksX))] =
                std::make_unique<Chunk>(cx * kSize, cz * kSize, m_height);
}

Chunk *ChunkManager::chunk(int cx, int cz) const
{
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1：无界域键控路由——槽位不存在 = nullptr（现行「越界返回 nullptr」同值）。
        const auto it = m_sparse.find(ChunkKey{ cx, cz }.packed());
        return it == m_sparse.end() ? nullptr : it->second.chunk.get();
    }
    if (cx < 0 || cz < 0 || cx >= m_chunksX || cz >= m_chunksZ)
        return nullptr;
    // R20.05：同 recreate()——ChunkKey::flatIndex 包装（逐位同式）。
    return m_chunks[size_t(ChunkKey{ cx, cz }.flatIndex(m_chunksX))].get();
}

Chunk *ChunkManager::chunkAtWorld(int x, int z) const
{
    // §29.5-W1 sparse：x/z 无界——按查询门（未物化 → nullptr = 现行「坐标越界」同值）。
    if (m_mode == WorldMode::Sparse) {
        if (!chunkMaterialized(floorDiv(x, kSize), floorDiv(z, kSize)))
            return nullptr;
        return chunk(floorDiv(x, kSize), floorDiv(z, kSize));
    }
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return nullptr;
    // R20.05 示范采用：floorDiv 替代截断除法（守卫非负域上逐位等价——r=x%16≥0 且 (r<0)!=(b<0)
    //   恒假 → 无校正分支生效）；负坐标语义就此立起（R20.09 无界世界起真消费）。
    return chunk(floorDiv(x, kSize), floorDiv(z, kSize));
}

// t155g：清所有 chunk 的 dirty 标记（World::setBlock 在 emit worldChanged 后调）。
//   旧版 buildMesh 末尾 clearDirty 由「先处理的 segment」抢先清掉共享 chunk 脏标记 →
//   后处理的 segment（terrain/water 二者之一）见 dirty=false 跳过重建 → 那段 mesh 陈旧到下个 sun-step。
//   改：buildMesh 不再清脏，统一由 World 在两段都重建完（emit worldChanged 同步返回后）清。
//   t188 perf：同时把 fluidOnlyDirty 复位 true（中性「假定下窗流体专用」），开新一轮累积窗口。
void ChunkManager::clearAllDirty()
{
    if (m_mode == WorldMode::Sparse) {
        for (auto &kv : m_sparse)
            if (kv.second.chunk) { kv.second.chunk->clearDirty(); kv.second.chunk->resetFluidOnlyDirty(); }
        return;
    }
    for (auto &c : m_chunks)
        if (c) { c->clearDirty(); c->resetFluidOnlyDirty(); }
}

// ── R20.10 Chunk lifecycle（选型与六态转移图见 chunklifecycle.h / chunkmanager.h）────────
ChunkLifecycle ChunkManager::lifecycleAt(int cx, int cz) const
{
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1：无槽位 → Absent（现行「越界 → Absent」同值——语义一致：无可用内容）。
        const auto it = m_sparse.find(ChunkKey{ cx, cz }.packed());
        return it == m_sparse.end() ? ChunkLifecycle::Absent : it->second.life;
    }
    if (cx < 0 || cz < 0 || cx >= m_chunksX || cz >= m_chunksZ)
        return ChunkLifecycle::Absent; // 越界语义 = 无可用内容（与 chunk() 越界 nullptr 一致）
    // R20.05：ChunkKey::flatIndex 包装（与 m_chunks 同式同索引——侧表同布局的落点）。
    return m_lifecycle[size_t(ChunkKey{ cx, cz }.flatIndex(m_chunksX))];
}

// 唯一转移入口：转移守卫（chunklifecycle.h 单一权威谓词）拦非法转移（含自转移）。**阴性轮
//   摘守卫摘这里的「调用点」**（t1051 教训：勿用 false && 前缀把条件守卫变无条件执行）——
//   摘后非法转移被接受 → r2010a 全图腿恰红。
bool ChunkManager::setLifecycle(int cx, int cz, ChunkLifecycle to)
{
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1：无槽位拒（现行「越界拒（无槽位可表态）」同值）；守卫谓词同一权威——
        // 稀疏只是存储形态分化，六态转移图零分叉。
        const auto it = m_sparse.find(ChunkKey{ cx, cz }.packed());
        if (it == m_sparse.end())
            return false; // 无槽位可表态
        if (!chunkLifecycleTransitionLegal(it->second.life, to))
            return false;
        it->second.life = to;
        return true;
    }
    if (cx < 0 || cz < 0 || cx >= m_chunksX || cz >= m_chunksZ)
        return false; // 越界拒（无槽位可表态）
    const size_t i = size_t(ChunkKey{ cx, cz }.flatIndex(m_chunksX));
    if (!chunkLifecycleTransitionLegal(m_lifecycle[i], to))
        return false;
    m_lifecycle[i] = to;
    return true;
}

// ── §29.5-W1 稀疏世界核（r2022）：统一谓词 + 稀疏初始化 + 物化 + 驻留枚举 ────────────────
// 存在性判断全库唯一落点（选型与谓词语义见 chunkmanager.h 注释；fixed 恒 true = 零变化墙）。
bool ChunkManager::chunkMaterialized(int cx, int cz) const
{
    if (m_mode == WorldMode::Fixed)
        return true;
    const auto it = m_sparse.find(ChunkKey{ cx, cz }.packed());
    return it != m_sparse.end() && chunkLifecycleQueryable(it->second.life);
}

bool ChunkManager::chunkContentPresent(int cx, int cz) const
{
    if (m_mode == WorldMode::Fixed)
        return true;
    const auto it = m_sparse.find(ChunkKey{ cx, cz }.packed());
    return it != m_sparse.end() && it->second.life != ChunkLifecycle::Absent
        && it->second.life != ChunkLifecycle::Evicting;
}

void ChunkManager::reinitializeSparse(int coreWidth, int coreDepth, int height)
{
    m_mode = WorldMode::Sparse;
    m_width = std::max(0, coreWidth); // 核心域（生成语义参考——TerrainGen 海域/钳高、出生回退）
    m_depth = std::max(0, coreDepth);
    m_height = std::max(0, height);
    m_chunksX = (m_width + kSize - 1) / kSize;  // 核心域 chunk 计数（信息性；查询域无界不由此界）
    m_chunksZ = (m_depth + kSize - 1) / kSize;
    m_chunks.clear();   // 稠密存储清空（防两套存储渗漏）
    m_lifecycle.clear();
    m_sparse.clear();   // 零 chunk 分配：全 Absent（槽位在 ensureChunk 时才物化）
    m_unsavedEdits.clear(); // §29.5-W3：persist 域编辑集合随 sparse 重置清空（全新世界无编辑）
}

// §29.5-W5b（r2028）fixed 归位（语义见头文件声明处；recreate 体复用——模式位归位 +
// 稠密空网格重建 = 全表 Loaded 常驻稳态，与 reinitializeSparse 对偶的最小迁移面）。
void ChunkManager::reinitializeFixed(int width, int depth, int height)
{
    m_mode = WorldMode::Fixed;
    recreate(width, depth, height);
}

Chunk *ChunkManager::ensureChunk(int cx, int cz)
{
    if (m_mode != WorldMode::Sparse)
        return chunk(cx, cz); // Fixed 无物化概念（防御；生产不走到——现行路由）
    const quint64 k = ChunkKey{ cx, cz }.packed();
    const auto it = m_sparse.find(k);
    if (it != m_sparse.end())
        return it->second.chunk.get();
    SparseSlot slot;
    slot.life = ChunkLifecycle::Absent;
    // 现行 Chunk 构造路径（recreate 同式：原点 = cx*kSize/cz*kSize，高度 = 世界高）。
    slot.chunk = std::make_unique<Chunk>(cx * kSize, cz * kSize, m_height);
    Chunk *raw = slot.chunk.get();
    m_sparse.emplace(k, std::move(slot));
    return raw;
}

// ── §29.5-W1b（r2023）：population 脚手架拆卸（语义与前置见 chunkmanager.h 声明注释）────
bool ChunkManager::releaseSparseChunk(int cx, int cz)
{
    if (m_mode != WorldMode::Sparse)
        return false; // Fixed 稠密网格无拆卸概念（零变化墙）
    return m_sparse.erase(ChunkKey{ cx, cz }.packed()) > 0;
}

// ── §29.5-W1b（r2023）：population 写域钳制（语义见 chunkmanager.h 声明注释）────────────
void ChunkManager::setPopulationWriteClamp(bool active, int anchorCx, int anchorCz)
{
    if (m_mode != WorldMode::Sparse)
        return; // Fixed 无 population 概念（防御；零变化墙）
    m_popClampActive = active;
    m_popClampCx = anchorCx;
    m_popClampCz = anchorCz;
}

QVector<QPair<int, int>> ChunkManager::sparseResidentKeysOrdered() const
{
    QVector<QPair<int, int>> keys;
    keys.reserve(int(m_sparse.size()));
    for (const auto &kv : m_sparse) {
        // 键逆映射走 ChunkKey::fromPacked 单一权威（补码位型双射——负坐标 chunk 同样正确还原）。
        const ChunkKey k = ChunkKey::fromPacked(kv.first);
        if (kv.second.chunk && chunkLifecycleQueryable(kv.second.life))
            keys.append({ k.cx, k.cz });
    }
    // cz 外 cx 内（= World residentChunkKeysOrdered 的枚举序契约；哈希容器序不可依赖 → 排序）。
    std::sort(keys.begin(), keys.end(), [](const QPair<int, int> &a, const QPair<int, int> &b) {
        return a.second != b.second ? a.second < b.second : a.first < b.first;
    });
    return keys;
}

quint8 ChunkManager::blockAt(int x, int y, int z) const
{
    // §29.5-W1 sparse：y 域两模式同构（有限高）；x/z 无界——未物化 chunk 的查询走现行
    // 「坐标越界」同值早退（统一谓词单点，OOB 等价语义）。
    if (m_mode == WorldMode::Sparse) {
        if (y < 0 || y >= m_height)
            return 0;
        if (!chunkMaterialized(floorDiv(x, kSize), floorDiv(z, kSize)))
            return 0;
        Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
        return c ? c->blockAt(floorMod(x, kSize), y, floorMod(z, kSize)) : quint8(0);
    }
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0; // 世界越界 = 空气（面剔除画边界面；物理把界外当可走出/可坠落）
    // R20.05 示范采用：floorDiv/floorMod 替代「截断除法 + 减法重建」（守卫非负域上逐位等价，
    //   floorMod(x,16) == x - (x/16)*16 == x&15）；负坐标语义单一权威（floorDiv(-1,16)=-1 /
    //   floorMod(-1,16)=15）自本函数起可写，本单域仍 [0,W) 不变（越界早退先于路由）。
    Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
    return c ? c->blockAt(floorMod(x, kSize), y, floorMod(z, kSize)) : quint8(0);
}

// t121：世界坐标 (x,z) 列的 heightmap（PLAN §2-H）。路由到所在 chunk 的局部列；越界 / 无 chunk → -1。
int ChunkManager::heightmapAt(int x, int z) const
{
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1：未物化 → -1（现行「世界越界 = 无实体」同值）。
        if (!chunkMaterialized(floorDiv(x, kSize), floorDiv(z, kSize)))
            return -1;
        Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
        return c ? c->heightmapAt(floorMod(x, kSize), floorMod(z, kSize)) : -1;
    }
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return -1; // 世界越界 = 无实体（mesher 对越界列本就无面可画）
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->heightmapAt(x - (x / kSize) * kSize, z - (z / kSize) * kSize) : -1;
}

// t360 列顶实面世界 y（见头注释）：单次 chunk 路由取 heightmap + 列顶方块 block/state → solidTopOffset。
//   空列 / 越界 / 无 chunk → -1（不遮挡）。PCF 软影（voxellight.h sunShadow）每采样调本。
float ChunkManager::columnTopSurfaceY(int x, int z) const
{
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1：未物化 → -1（现行「越界不遮挡」同值）。
        if (!chunkMaterialized(floorDiv(x, kSize), floorDiv(z, kSize)))
            return -1.0f;
        Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
        if (!c) return -1.0f;
        const int lx = floorMod(x, kSize), lz = floorMod(z, kSize);
        const int hm = c->heightmapAt(lx, lz);
        if (hm < 0) return -1.0f;
        return float(hm) + BlockRegistry::solidTopOffset(c->blockAt(lx, hm, lz), c->stateAt(lx, hm, lz));
    }
    if (x < 0 || z < 0 || x >= m_width || z >= m_depth)
        return -1.0f;
    Chunk *c = chunk(x / kSize, z / kSize);
    if (!c) return -1.0f;
    const int lx = x - (x / kSize) * kSize;
    const int lz = z - (z / kSize) * kSize;
    const int hm = c->heightmapAt(lx, lz);
    if (hm < 0) return -1.0f;
    return float(hm) + BlockRegistry::solidTopOffset(c->blockAt(lx, hm, lz), c->stateAt(lx, hm, lz));
}

// t151 光场路由（世界坐标 → chunk 局部）。越界读返回 0（无光）。flood-fill 与 mesher 经此访问光场。
quint8 ChunkManager::skyLightAt(int x, int y, int z) const
{
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1：y 域同构；未物化 → 0（现行「越界无光」同值）。
        if (y < 0 || y >= m_height)
            return 0;
        if (!chunkMaterialized(floorDiv(x, kSize), floorDiv(z, kSize)))
            return 0;
        Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
        return c ? c->skyLightAt(floorMod(x, kSize), y, floorMod(z, kSize)) : quint8(0);
    }
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0;
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->skyLightAt(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize) : quint8(0);
}

quint8 ChunkManager::blockLightAt(int x, int y, int z) const
{
    if (m_mode == WorldMode::Sparse) {
        if (y < 0 || y >= m_height)
            return 0;
        if (!chunkMaterialized(floorDiv(x, kSize), floorDiv(z, kSize)))
            return 0;
        Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
        return c ? c->blockLightAt(floorMod(x, kSize), y, floorMod(z, kSize)) : quint8(0);
    }
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0;
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->blockLightAt(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize) : quint8(0);
}

void ChunkManager::setLight(int x, int y, int z, quint8 sky, quint8 block)
{
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1：y 域同构；未物化忽略（现行「越界忽略」同值——flood 对未物化邻格不落写）。
        if (y < 0 || y >= m_height)
            return;
        if (!chunkContentPresent(floorDiv(x, kSize), floorDiv(z, kSize)))
            return;
        Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
        if (c) c->setLight(floorMod(x, kSize), y, floorMod(z, kSize), sky, block);
        return;
    }
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return; // 越界忽略（flood 自行跳过 OOB 邻居）
    Chunk *c = chunk(x / kSize, z / kSize);
    if (c) c->setLight(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize, sky, block);
}

// 全部 chunk 光场归零（re-flood 前清场，World::recomputeLightField 调）。
void ChunkManager::clearAllLight()
{
    if (m_mode == WorldMode::Sparse) {
        for (auto &kv : m_sparse)
            if (kv.second.chunk) kv.second.chunk->clearLight();
        return;
    }
    for (auto &cp : m_chunks)
        if (cp) cp->clearLight();
}

// t133：世界坐标 state 读（跨 chunk 路由，同 blockAt）。越界 / 无 chunk → 0（常规方块无 state）。
quint8 ChunkManager::stateAt(int x, int y, int z) const
{
    if (m_mode == WorldMode::Sparse) {
        if (y < 0 || y >= m_height)
            return 0;
        if (!chunkMaterialized(floorDiv(x, kSize), floorDiv(z, kSize)))
            return 0;
        Chunk *c = chunk(floorDiv(x, kSize), floorDiv(z, kSize));
        return c ? c->stateAt(floorMod(x, kSize), y, floorMod(z, kSize)) : quint8(0);
    }
    if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth)
        return 0; // 世界越界 = 无 state
    Chunk *c = chunk(x / kSize, z / kSize);
    return c ? c->stateAt(x - (x / kSize) * kSize, y, z - (z / kSize) * kSize) : quint8(0);
}

bool ChunkManager::setBlock(int x, int y, int z, quint8 id)
{
    return setBlock(x, y, z, id, quint8(0)); // t133：默认 state=0（兼容；新方块重置 state 防 stale）
}

// t133：写 id + state + 标脏（含边界邻接）。与 4 参数版同一路径，仅多写一字节 state。
bool ChunkManager::setBlock(int x, int y, int z, quint8 id, quint8 state)
{
    // ── 域门（两模式分流；Fixed 分支为现行语句原样——零变化墙）────────────────────────
    if (m_mode == WorldMode::Sparse) {
        // §29.5-W1b：population 写域钳制（population 期间激活——见 chunkmanager.h 声明注释；
        // 平时/fixed 恒 off，本分支零进入 = 写路径零变化）。先于写门：窗外写恒拒（与「未物化
        // 拒」同值 false 同形），令远端候选溢写在「邻块已/未物化」两序下同为拒 = 确定性。
        if (m_popClampActive) {
            const int tcx = floorDiv(x, kSize), tcz = floorDiv(z, kSize);
            if (tcx < m_popClampCx - 1 || tcx > m_popClampCx + 1
                || tcz < m_popClampCz - 1 || tcz > m_popClampCz + 1)
                return false;
        }
        if (y < 0 || y >= m_height)
            return false; // y 域两模式同构（现行越界拒绝同值）
        // §29.5-W1：x/z 无界——按写门（未物化拒 = 现行「坐标越界」同值 false；Loading/
        // Generated/Loaded/Active 内容在途或可用可写——出生半径预生成的列体填充在 Loading 态写入）。
        if (!chunkContentPresent(floorDiv(x, kSize), floorDiv(z, kSize)))
            return false;
    } else if (x < 0 || y < 0 || z < 0 || x >= m_width || y >= m_height || z >= m_depth) {
        return false; // 世界越界：拒绝
    }
    // R20.05 示范采用：floorDiv/floorMod（同 blockAt——守卫非负域上逐位等价）。
    const int cx = floorDiv(x, kSize), cz = floorDiv(z, kSize);
    const int lx = floorMod(x, kSize), lz = floorMod(z, kSize);
    Chunk *c = chunk(cx, cz);
    if (!c) return false;
    // t188 perf：读 oldId 用于「流体专用脏」分类。isFluidLike(oldId) && isFluidLike(id) = 流体类写
    //   （水流/岩浆流扩散/蒸发/re-leveling、桶倒水）→ 不动 fluidOnlyDirty（保留中性 true / 已被固体清则续 false）；
    //   否则（任一为实体方块：破/放固体、水×岩浆→Stone/Cobble/Obsidian、沙着地）→ clearFluidOnlyDirty
    //   （固体 dominate → terrain 类段须重建）。classification 对目标 chunk 与边界邻接 chunk 同源（同一次写）。
    const quint8 oldId = c->blockAt(lx, y, lz);
    const bool fluidOnly = BlockRegistry::isFluidLike(oldId) && BlockRegistry::isFluidLike(id);
    if (!fluidOnly) c->clearFluidOnlyDirty(); // 固体写 → 否决「流体专用」假定
    c->setBlock(lx, y, lz, id, state);
    c->markDirty();
    // 边界格（local x/z 贴 chunk 边沿）→ 该格面可见性可能影响邻接 chunk 的边界剔除，
    // 标邻接 chunk 脏（dev-spec t02 验收；为 t03「跨边界破放不破坏邻居 mesh」准备）。
    // t188 perf：边界流体写对邻接 chunk 的 terrain 类段同样无影响（流体格不进 terrain 段、不影响实体面剔除）
    //   → 固体否决同步传给邻接 chunk（同一次写的分类），保邻接 terrain 类段也按同一 fluidOnlyDirty 行为。
    if (lx == kSize - 1) { if (Chunk *n = chunk(cx + 1, cz)) { n->markDirty(); if (!fluidOnly) n->clearFluidOnlyDirty(); } } // +X 邻
    if (lx == 0)         { if (Chunk *n = chunk(cx - 1, cz)) { n->markDirty(); if (!fluidOnly) n->clearFluidOnlyDirty(); } } // -X 邻
    if (lz == kSize - 1) { if (Chunk *n = chunk(cx, cz + 1)) { n->markDirty(); if (!fluidOnly) n->clearFluidOnlyDirty(); } } // +Z 邻
    if (lz == 0)         { if (Chunk *n = chunk(cx, cz - 1)) { n->markDirty(); if (!fluidOnly) n->clearFluidOnlyDirty(); } } // -Z 邻
    // ── §29.5-W3：persist 域未落盘编辑标记（单漏斗尾部；语义见 chunkmanager.h 声明注释）──
    //   生成写抑制窗（UnsavedEditWriteWindow）之外的真实内容写 → 该 chunk 记一笔未落盘编辑。
    //   幂等集合插入；生成窗内（worldgen sink / adopt 守卫应用 / population 窗口重放）零记录。
    if (m_editSuspendDepth == 0)
        m_unsavedEdits.insert(ChunkKey{ cx, cz }.packed());
    return true;
}
