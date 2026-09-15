#ifndef CHUNKMANAGER_H
#define CHUNKMANAGER_H

#include <QtGlobal> // quint8

#include <memory>
#include <vector>

#include "chunk.h"
#include "chunklifecycle.h" // R20.10 六态类型 + 转移表单一权威（ChunkLifecycle/chunkLifecycleTransitionLegal）
#include "mathtypes.h" // R20.05 基础类型（Core 叶子）：floorDiv/floorMod 路由 + ChunkKey 网格索引包装 + BlockPos 加性重载

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
};

#endif // CHUNKMANAGER_H
