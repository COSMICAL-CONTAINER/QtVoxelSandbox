#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include <QtGlobal> // quint8

#include <memory>
#include <unordered_map>
#include <vector>

#include <QVector> // §29.5-W1 sparseResidentKeysOrdered 返回类型（驻留集枚举收集面）

#include "chunk.h"
#include "chunklifecycle.h" // R20.10 六态类型 + 转移表单一权威（ChunkLifecycle/chunkLifecycleTransitionLegal）
#include "mathtypes.h" // R20.05 基础类型（Core 叶子）：floorDiv/floorMod 路由 + ChunkKey 网格索引包装 + BlockPos 加性重载

// ── §29.5-W1 稀疏世界核（r2022）：World / ChunkManager 构造模式 ─────────────────────────
//   Fixed = 现行稠密网格（recreate 全量分配 chunk + 全表 Loaded 稳态）——全库既有行为逐位不变
//   （fixed 默认 = 零变化承重墙 r2022a）。Sparse = 查询域 x/z 无界（y 仍有限高）+ chunk 按需
//   物化（初始化零 chunk 分配、全 Absent；出生半径预生成走 ①②③ 合法边到 Loaded 稳态，与
//   fixed create 终态同稳态）。
//
//   选型立证（为何不用「dims 哨兵参数化」——任务书两案取一，头注释立此存照）：
//   ① TerrainGen 的生成语义需要**真实 dims**：海域半径 = min(W,D)*3/10（seaColumnHeight /
//      seaCorner）、列填充钳高 = min(h, H-1)（fillTerrainColumn）——dims 哨兵（0 / 负值）会让
//      sparse 世界同 seed 生成结果 ≠ fixed 世界同区（r2022c 同 seed 区域恒等承重腿直接破）。
//   ② width/depth 是 World 的 Q_PROPERTY（QML / 渲染 / F3 消费面）且被群系 memo 尺寸、出生
//      回退列等十余处派生面读取——哨兵值会渗入全部派生面；显式模式位让「Fixed 走现行路径
//      零改动」成为结构性事实（fixed 分支语句原样保留，sparse 分支全部加性），零变化墙最强形态。
enum class WorldMode : quint8
{
    Fixed = 0,
    Sparse
};

// ChunkManager：持有一片连续的 chunk 列网格（width×depth 平面铺满，每 chunk 16×16 列），
// 负责「世界坐标 ↔ chunk/局部坐标」路由、跨 chunk blockAt/setBlock、越界判定、
// 与边界格的邻接 chunk 脏标记（为 t03 跨边界剔除准备）。
//
// World 层：**不**依赖 Renderer/Physics/QtQuick3D（PLAN §2 分层铁律 + 不变量 J）。
// 越界（世界坐标超出 [0,width)×[0,height)×[0,depth)）blockAt 视作空气、setBlock 拒绝。
class ChunkManager
{
public:
    // 默认构造 → 空 (0,0,0) 网格；World 持为成员后在 generate() 里 recreate 到实际尺寸。
    ChunkManager() : ChunkManager(0, 0, 0) {}
    ChunkManager(int width, int depth, int height);

    int width() const { return m_width; }
    int depth() const { return m_depth; }
    int height() const { return m_height; }
    int chunksX() const { return m_chunksX; }
    int chunksZ() const { return m_chunksZ; }
    int chunkCount() const { return int(m_chunks.size()); }

    // 世界坐标查询/写入（含跨 chunk 路由）。越界 blockAt 返回 0；setBlock 越界返回 false。
    // setBlock 成功写入后：标目标 chunk 脏；该格贴 chunk 边沿 → 同标邻接 chunk 脏（t03 准备）。
    quint8 blockAt(int x, int y, int z) const;
    bool setBlock(int x, int y, int z, quint8 id);
    // t133 不完整方块 state（朝向/开合）：世界坐标读 / 写（跨 chunk 路由，同 blockAt/setBlock）。
    //   setBlock 默认 state=0（兼容）；4 参数 setBlock 委托 5 参数 (id, 0)（新方块重置 state，防 stale）。
    quint8 stateAt(int x, int y, int z) const;
    bool setBlock(int x, int y, int z, quint8 id, quint8 state);
    // t121：世界坐标列的「自顶向下首个非空气」y（越界 / 空列 → -1）。mesher 据此判顶点见天（PLAN §2-H）。
    int heightmapAt(int x, int z) const;
    // t360 列顶实面世界 y（= heightmap + solidTopOffset(列顶方块)；越界 / 空列 → -1）。PCF 软影按方块真实
    //   模型高度判遮挡（修下半砖/合活版门被当整格高投整格黑影）。单次 chunk 路由（PCF 热路径：heightmap +
    //   该列顶方块 block/state 一次取齐，免 3 次重复 chunk 路由）。
    float columnTopSurfaceY(int x, int z) const;
    // t151 光场路由（世界坐标 ↔ chunk 局部）：sky/block 读 + 写 + 全清。越界读返回 0、写忽略。
    //   flood-fill（World）与 mesher 经此访问 per-voxel 光场（跨 chunk 自动路由）。OOB 语义统一交 caller。
    quint8 skyLightAt(int x, int y, int z) const;
    quint8 blockLightAt(int x, int y, int z) const;
    void setLight(int x, int y, int z, quint8 sky, quint8 block);
    void clearAllLight();

    // 取 chunk（网格坐标 cx,cz；越界返回 nullptr）。
    Chunk *chunk(int cx, int cz) const;
    // 世界坐标 (x,z) 所在 chunk（越界 nullptr）。
    Chunk *chunkAtWorld(int x, int z) const;
    // R20.05 BlockPos 最小示范采用（加性 C++ 重载，非 Q_INVOKABLE——QML 面本单不动）：与 int 版
    //   全等（体内直转调；r2005c 腿对真 rig 世界钉「重载面同源」）。只立类型可用性，不迁移调用点。
    Chunk *chunkAtWorld(BlockPos p) const { return chunkAtWorld(p.x, p.z); }
    quint8 blockAt(BlockPos p) const { return blockAt(p.x, p.y, p.z); }
    bool setBlock(BlockPos p, quint8 id) { return setBlock(p.x, p.y, p.z, id); }
    // t155g：清所有 chunk 的 dirty（World 在 emit worldChanged 后调 —— 此时所有 dirty chunk 的
    //   terrain+water 两段 ChunkGeometry 都已在槽里重建完毕，统一清脏避免「一段 clearDirty 抢清致另一段跳过」）。
    void clearAllDirty();

    // ── R20.10 Chunk lifecycle（refactor-plan §29.3；类型/转移表单一权威见 chunklifecycle.h）──
    //   **选型：态存 ChunkManager 侧表（std::vector<ChunkLifecycle>，与 m_chunks 同布局同索引），
    //   不存 Chunk 内字段**。理由三条：
    //   ① 豁免域隔离——worldstore 直读 chunk blob（Chunk::voxelData 三数组，R20.08 关单登记豁免）
    //     是存档往返权威；态放 Chunk 内会给存档语义引入「该不该序列化」的诱惑面，侧表让 Chunk 保持
    //     纯体素容器、store 路径结构上看不见生命周期（「没有未保存数据被静默丢失」在本单最小解释下
    //     结构性成立，r2010d 实证）。
    //   ② Absent/Evicting 语义需要「实例缺席仍可表态」——最小解释下驱逐不销毁 Chunk 对象（数据
    //     保留策略是登记的后续单），实例字段无法表达「对象在但内容不可用」；侧表可以。
    //   ③ 定长稠密 vector 与 m_chunks 同生同灭：recreate() 是唯一的初始化点（全表 Loaded），O(1)
    //     索引、零分配churn，与既有网格布局（ChunkKey::flatIndex）同式。
    //   消费面收口：查询走 lifecycleAt()（World::chunks() 只读引用即可达）；写走 setLifecycle()
    //   （唯一转移入口，转移守卫在此——摘此调用点即阴性红，见 .cpp）。World 另有 C++ 面
    //   setChunkLifecycle forwarder（非 Q_INVOKABLE——生命周期决策不进 QML，plan 验收第四条）。
    //   默认稳态 = 全表 Loaded（常驻已加载）：固定 10×10 / fresh 48×48 世界行为零变化的基线
    //   （r2010b 钉）。门查询可见面（mesher 三门）= {Loaded, Active}，见 chunklifecycle.h。
    ChunkLifecycle lifecycleAt(int cx, int cz) const; // 越界 → Absent（语义一致：无可用内容）
    bool setLifecycle(int cx, int cz, ChunkLifecycle to); // 唯一转移入口：非法转移（含自转移）拒 false

    // ── §29.5-W1 稀疏世界核（r2022）────────────────────────────────────────────────────
    //   模式与统一谓词（存在性判断全库唯一落点——查询门/写门/物化门皆经此三谓词，禁散落第二份）：
    //   chunkMaterialized  = 查询门（对 mesher 门 / 驻留集同谓词）：Fixed 恒 true（稠密网格
    //     chunk 恒在且恒驻留——零变化的结构性根据）；Sparse = 槽位已物化且生命周期可查询
    //     （{Loaded, Active} = chunkLifecycleQueryable——未物化/在途/驱逐中的 chunk 查询一律
    //     走现行「坐标越界」同值早退，即 OOB 等价语义）。
    //   chunkContentPresent = 写门：Fixed 恒 true；Sparse = 槽位已物化且内容在途或可用
    //     （{Loading, Generated, Loaded, Active}——出生半径预生成的列体填充发生在 Loading 态；
    //     Absent/Evicting 拒写 = 现行「坐标越界」同值 false）。
    WorldMode mode() const { return m_mode; }
    bool chunkMaterialized(int cx, int cz) const;
    bool chunkContentPresent(int cx, int cz) const;
    // sparse 初始化：核心域尺寸定格（生成语义参考——查询域无界与此无关）+ 零 chunk 分配
    // （m_sparse 清空 = 全 Absent；稠密存储清空防渗漏）。仅 sparse 构造路径调（World 构造分化）。
    void reinitializeSparse(int coreWidth, int coreDepth, int height);
    // 物化槽位：已存在返回既有；不存在则经**现行 Chunk 构造路径**（recreate 同式
    // make_unique<Chunk>(cx*kSize, cz*kSize, m_height)）落一个 Absent 槽。Fixed 模式无物化
    // 概念（防御回退现行 chunk()——生产不会走到）。
    Chunk *ensureChunk(int cx, int cz);
    // ── §29.5-W1b sparse population parity（r2023）：population 脚手架拆卸 ────────────────
    // 擦除 sparse 槽位与 chunk 实例（**数据不保留**——与 R20.10/W3 驱逐的「最小解释下驱逐
    // 不销毁 Chunk 对象」数据保留语义刻意不同：脚手架是 population 窗口的一次性读域，其内容
    // 将由该 chunk 自己的正式物化链（sparseGenerateChunk 全量 population 重放）逐位重derive，
    // 保留反而是污染源——读闭合论证见 world.cpp sparsePopulateChunk 头注释处置表）。
    // 前置：槽位生命周期已被调用方经 setChunkLifecycle 唯一入口驱动到 Absent（⑥⑦ 合法边）；
    // 本方法只负责擦槽（Fixed 模式拒——稠密网格无拆卸概念）。返回是否确有槽位被擦除。
    bool releaseSparseChunk(int cx, int cz);
    // ── §29.5-W1b sparse population parity（r2023）：population 写域钳制 ──────────────────
    // sparse population 期间把 setBlock 写域钳到锚 chunk ±1 chunk（= scaffold 读窗）：窗外写入
    // 一律拒。两个语义依据（world.cpp sparsePopulateChunk 处置表「精确性论证」段）：
    //   ① 顺序无关性——全候选域 pass 的远端候选溢写若落入「本 population 时恰已物化的真邻块」
    //      （终态内容），会把 pass-k 数学 applied 到终态上 = 按加载次序污染邻块（r2023c 恰红
    //      实证）；钳窗外后，远端溢写在「邻块存在/不存在」两序下同为拒 = 确定性；
    //   ② 邻块保护——真邻块终态只能由其自身 population 全量重放产出（逐位 fixed 恒等面），
    //      他块 population 的部分窗投影不得触碰（其溢写已由邻块自身 population 同值落位）。
    // 恒 inactive 于 fixed 模式与 population 之外（写路径零开销零行为差 = 零变化墙）。
    void setPopulationWriteClamp(bool active, int anchorCx, int anchorCz);
    // 驻留集枚举（sparse 版）：物化且可查询的槽位键，按 cz 外 cx 内排序（= World
    // residentChunkKeysOrdered 的枚举序契约，r2021 权威面在 sparse 域的同序延伸）。
    QVector<QPair<int, int>> sparseResidentKeysOrdered() const;

    // 尺寸变化时重建网格（清空旧 chunk，新建零填充 chunk，全部脏）。
    void recreate(int width, int depth, int height);

private:
    static constexpr int kSize = Chunk::kSize;

    int m_width = 0, m_depth = 0, m_height = 0;
    int m_chunksX = 0, m_chunksZ = 0;                  // = ceil(width/kSize), ceil(depth/kSize)
    std::vector<std::unique_ptr<Chunk>> m_chunks;      // 索引 [cx + m_chunksX * cz]
    // R20.10 生命周期侧表（与 m_chunks 同布局同索引；选型理由见类方法注释）。recreate() 全表
    //   置 Loaded（默认稳态）；此后仅 setLifecycle 可写（唯一转移入口）。
    std::vector<ChunkLifecycle> m_lifecycle;

    // ── §29.5-W1 稀疏存储（r2022；Fixed 模式下恒空——两套存储互不渗漏）─────────────────
    //   选型：std::unordered_map（键 = ChunkKey::packed() 补码位型双射——负坐标 chunk 同样合法）
    //   而非稠密 vector 的对偶键控版。槽位 = chunk 实例 + 生命周期同槽（与 Fixed「m_chunks +
    //   m_lifecycle 稠密 vector 对」同构的键控形态；生命周期决策面仍走 setLifecycle 唯一守卫
    //   入口，见 chunklifecycle.h——稀疏只是存储形态分化，六态转移图零分叉）。构造后 Absent
    //   槽位零分配（零 unique_ptr、零 Chunk），物化走 ensureChunk（现行 Chunk 构造路径）。
    struct SparseSlot
    {
        std::unique_ptr<Chunk> chunk;
        ChunkLifecycle life = ChunkLifecycle::Absent; // 物化前不落槽 → 槽位存在即非 Absent 初见
    };
    WorldMode m_mode = WorldMode::Fixed; // 默认 Fixed = 全库既有行为逐位不变的结构性事实
    std::unordered_map<quint64, SparseSlot> m_sparse;
    // §29.5-W1b：population 写域钳制态（population 期间激活；fixed/平时恒 off = 零变化墙）。
    // 锚 chunk 坐标 ±1 chunk = 写域；掩码语义见 setPopulationWriteClamp 声明注释。
    bool m_popClampActive = false;
    int m_popClampCx = 0, m_popClampCz = 0;
};

#endif // CHUNKMANAGER_H
