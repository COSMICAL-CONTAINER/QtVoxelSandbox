#ifndef WORLDFACADE_H
#define WORLDFACADE_H

// R20.08 WorldFacade（refactor-plan §29.3 R20.08「WorldFacade：把 World 的查询和写入接口收窄」）：
// World 的**收窄查询/写入接口层**——新代码（GameSession 及后续 R20 主线组件）对世界的读写统一
// 走本面，不再直取 Chunk 内部指针、不再自行散落规则判定。
//
// ── 验收对照（plan §29.3 R20.08 原文四条）──────────────────────────────────────────
// ① 「新代码不再直接获取 Chunk 内部指针」：本头是唯一授权的 chunk 路由消费点（World 层内）——
//    public 面**零 Chunk\*/ChunkManager 外泄**（无 chunks() 转发、无 Chunk* 返回值/参数，结构性
//    不可能经本面拿到 chunk 指针）；本类内部经 World::chunks() 路由（收口点自有域，同 worldgen /
//    worldstore 豁免口径）。示范迁移：GameSession（写入面 + 查询面，R20.07 新代码）与
//    ChunkGeometry（mesher 的 chunk 脏门查询，渲染侧唯一 Chunk* 消费点——迁移后 mesher 零
//    Chunk*）。其余现存调用点（全为只读 World public API 或豁免域）登记于 docs R20.08 关单
//    迁移清单，本单不全量迁移。
// ② 「World 规则使用统一查询」：高频读（block/state/光照/heightmap/biome/天气/碰撞谓词）在
//    本面单一入口收拢——World 早已是规则权威（isCollidable/isFullCubeAt/... 内部只读
//    ChunkManager + BlockRegistry 单一权威），本面**逐行委托不复制**（防两套规则漂移）。
// ③ 「Renderer 不需要持有可变 World」：渲染面访问形态盘点（R20.08 关单登记）——ChunkGeometry /
//    BlockCube 对 World 的全部消费均为**只读**（blockAt/stateAt/光照/heightmap + worldChanged
//    信号），零可变写调用；脏门经本面查询后 mesher 连 Chunk* 都不再持有。类型级收口
//   （Q_PROPERTY World* → const World* / 只读视图）留后续（会动 QML 绑定面，本批渲染行为
//    零变化），收口计划见 docs R20.08 关单。
// ④ 「旧 World 暂时仍可作为 Implementation」：本类是**包装不是替换**——World 类零改动（旧
//    World 仍是唯一实现与 QML façade），本面只是它上方的一层收窄视图（纯转发，无状态、无
//    QObject、无信号——观察 World 信号仍经 World 本体或 GameSession 事件面）。
//
// 写入面口径：可变写收窄到显式入口。玩家/命令语义写 = setBlock 家族（PLAN §2-C「写栅格的唯一
// 入口」——R20.07 GameSession 命令委托的同一终端权威），本面包装后对 GameSession/新代码暴露；
// 静默写族（setBlockSilent / setWaterSilent / setBlockFromEntity / clearBlockSilent / ...）仍
// 在 World public 面（既有系统模拟路径，调用点全量收口归 R20.09 EditBuffer 正席——本单登记，
// 不收）。 facade 只暴露命令语义写（id / id+state），**不**转发静默族（收窄 = 不新增旁路）。
//
// 分层（PLAN §2）：World 层组件（同 gamesession.h 模式的 header-only；向下依赖 World +
// Core/mathtypes），不依赖 Renderer/Game/Entities/QML；不反向被 World 依赖。

#include "world.h" // World：实现权威（本类纯转发，零逻辑复制）

#include "chunklifecycle.h" // R20.10 六态门谓词 chunkLifecycleQueryable（单一权威，勿复制）
#include "mathtypes.h" // BlockPos（Core 叶子——新代码的坐标类型门面）

// World 的收窄视图（非 QObject：纯值语义包装——拷贝/临时构造均廉价，持 World 引用不拥有）。
// 命名对齐 World 同名方法（一一映射可读性）；BlockPos 优先（新代码坐标面），跨 chunk 的
// chunk 网格门查询以 cx/cz 命名显式分层（chunk 坐标 ≠ 方块坐标，不混用）。
class WorldFacade
{
public:
    // 持 World 引用（视图不拥有——World 生命周期归 caller / QML，视图可随时重建）。
    explicit WorldFacade(World &world) : m_world(&world) {}

    // ── 查询面（const，高频读收拢；语义与 World 同名方法逐位一致——r2008a 等价腿钉）──
    // 体素 id（越界返 0 空气——World::blockAt 同语义）。
    quint8 blockAt(BlockPos p) const { return m_world->blockAt(p); }
    quint8 blockAt(int x, int y, int z) const { return m_world->blockAt(x, y, z); }
    // 不完整方块 state（朝向/开合；越界返 0）。
    quint8 stateAt(BlockPos p) const { return m_world->stateAt(p.x, p.y, p.z); }
    quint8 stateAt(int x, int y, int z) const { return m_world->stateAt(x, y, z); }
    // 「非 air 实存」谓词（raycast 选体 / mesher 邻居剔除口径；碰撞语义用 isCollidableAt）。
    bool isSolidAt(BlockPos p) const { return m_world->isSolid(p.x, p.y, p.z); }
    // 规则统一查询：碰撞谓词（BlockRegistry::isCollidable(blockAt, stateAt) 单一权威）。
    bool isCollidableAt(BlockPos p) const { return m_world->isCollidable(p.x, p.y, p.z); }
    // 规则统一查询：完整立方谓词（BlockRegistry::isFullCube 单一权威——支撑/着落判定基础）。
    bool isFullCubeAt(BlockPos p) const { return m_world->isFullCubeAt(p.x, p.y, p.z); }
    // 光场（BFS flood-fill，World 单一权威；越界语义同 World：y≥height 天光 15 / 其余 0）。
    quint8 skyLightAt(int x, int y, int z) const { return m_world->skyLightAt(x, y, z); }
    quint8 blockLightAt(int x, int y, int z) const { return m_world->blockLightAt(x, y, z); }
    // 天光 heightmap（列首个非空气 y，越界/空列 -1）与 worldgen 地表高度（同 seed 同值）。
    int heightmapAt(int x, int z) const { return m_world->heightmapAt(x, z); }
    int heightAt(int x, int z) const { return m_world->heightAt(x, z); }
    // 群系编码（0..6，World::biomeIdAt 单一权威——纯函数于 seed）。
    int biomeIdAt(int x, int z) const { return m_world->biomeIdAt(x, z); }
    // 天气（全局态 + 群系解析局部降水；mob 灭火 / 作物 / 呈现层共用判据）。
    int weatherStateAt(int x, int z) const { return m_world->weatherStateAt(x, z); }
    bool isPrecipitatingAt(int x, int z) const { return m_world->isPrecipitatingAt(x, z); }
    // 世界高（尺度元数据；R20.13 ChunkMeshSnapshot 采集面用——稠密域 y 容量定格）。
    int height() const { return m_world->height(); }
    // t360 列顶实面世界 y（PCF 软影采样源；R20.13 快照采集统一走收窄面——World 同名方法逐位转发）。
    float columnTopSurfaceY(int x, int z) const { return m_world->columnTopSurfaceY(x, z); }

    // ── chunk 网格门查询（R20.08 示范迁移点专用：mesher 脏门 / 存在门——之前渲染侧经
    //    World::chunks().chunk(cx,cz) 直取 Chunk* 读 dirty()/fluidOnlyDirty()，现收拢为本面
    //    三查询；委托 World 层同一路由，语义逐位一致。cx/cz 为 chunk 网格坐标，越界返 false）──
    //    **R20.10 生命周期接线**：三门在「chunk 对象在位」之上叠加生命周期门
    //    （chunkLifecycleQueryable = 态 ∈ {Loaded, Active}，单一权威见 chunklifecycle.h）——
    //    chunkExistsAt = 对象在位 && 态可查询；chunkDirtyAt / chunkFluidOnlyDirtyAt = 存在门
    //    为真才读脏标记，否则恒 false（mesher 门跳过）。默认稳态全 chunk = Loaded → 三门与
    //    R20.08 行为逐位一致（r2008 全绿常驻 + r2010b 零变化实证）；卸载（Loaded→Evicting→
    //    Absent）后三门恒 false、重载（Absent→Loading→Generated→Loaded）后恢复（r2010c 端到
    //    端）。ChunkGeometry 侧零改动（门经本面继承生命周期语义，r2008c 结构钉照常在位）。
    bool chunkExistsAt(int cx, int cz) const
    {
        const Chunk *c = m_world->chunks().chunk(cx, cz);
        return c != nullptr && chunkLifecycleQueryable(m_world->chunks().lifecycleAt(cx, cz));
    }
    bool chunkDirtyAt(int cx, int cz) const
    {
        const Chunk *c = m_world->chunks().chunk(cx, cz);
        return c && chunkLifecycleQueryable(m_world->chunks().lifecycleAt(cx, cz)) && c->dirty();
    }
    bool chunkFluidOnlyDirtyAt(int cx, int cz) const
    {
        const Chunk *c = m_world->chunks().chunk(cx, cz);
        return c && chunkLifecycleQueryable(m_world->chunks().lifecycleAt(cx, cz)) && c->fluidOnlyDirty();
    }

    // ── 写入面（显式入口；玩家/命令语义——R20.07 GameSession 命令委托的同一终端权威）──
    // 写方块（id 变才走写入路径 → 重置 state=0；成功发 blockBroken/blockPlaced 语义事件 +
    // worldChanged + 全套写后钩子——World::setBlock「写栅格的唯一入口」逐位转发，不复制）。
    // 越界 / 无变化 → false（World 权威语义）。
    bool setBlock(BlockPos p, quint8 id) { return m_world->setBlock(p, id); }
    bool setBlock(int x, int y, int z, quint8 id) { return m_world->setBlock(x, y, z, id); }
    // 写 id + state（id 不变只 state 变——如门开合：不发 broken/placed，仍发 worldChanged）。
    bool setBlockWithState(BlockPos p, quint8 id, quint8 state)
    {
        return m_world->setBlock(p.x, p.y, p.z, id, state);
    }
    bool setBlockWithState(int x, int y, int z, quint8 id, quint8 state)
    {
        return m_world->setBlock(x, y, z, id, state);
    }

private:
    World *m_world; // 非拥有（World& 的指针形态——便于 ChunkGeometry 可空世界场景复用语义）
};

#endif // WORLDFACADE_H
