#ifndef MESHBUILDER_H
#define MESHBUILDER_H

// R20.13 MeshBuilder（refactor-plan §29.3 R20.13「先把 CPU 网格算法从 ChunkGeometry 中抽出」——
// 五验收即硬线，本头逐条落位）：
//   ①「MeshBuilder 不依赖 QQuick3D」——本头/meshbuilder.cpp 零 QQuick3D include（矩阵 r2013d
//      反探常驻钉死）；QQuick3D 灌注只发生在 ChunkGeometry（旧 QtQuick3DAdapter 形态，验收④）。
//   ②「输入是 snapshot」——唯一输入 = ChunkMeshSnapshot（R20.06 snapshot.h 头注登记的「稠密方块
//      域快照是 R20.13 MeshBuilder 消费侧」的本体）：采集函数把 ChunkGeometry 原先散布在网格循环
//      里的现查面（Facade blockAt/stateAt/光照三门 + 列顶 PCF 采样源）一次性采成自持稠密域，
//      MeshBuilder 只吃快照、不持 World 指针。
//   ③「输出是 owning ChunkMeshData」——顶点/索引/统计全部自持（值语义 QVector + 计数字段），
//      QObjectFree 编译期钉；消费方（ChunkGeometry / 未来 worker meshing）拿到的不是视图是数据。
//   ④「旧 QtQuick3DAdapter 可以消费新结果」——ChunkGeometry::buildMesh 收敛为
//      「采集快照 → MeshBuilder::build → 灌 QQuick3DGeometry」，Q_PROPERTY/信号面零变化。
//   ⑤「网格视觉和顶点统计与旧路径一致」——抽取 = **搬移不改写**：buildMesh 网格算法本体
//      （PASS1 异形/cross 合批 → PASS2 流体变高面/greedy mask/逐格 culled；dayMul 只乘天光、
//      AO 三探针、水-岩浆-玻璃-冰-水段-岩浆段-冰段分流、边界剔除）逐段自 chunkgeometry.cpp 迁入
//      meshbuilder.cpp，仅两类机械替换：数据来源（`m_world->skyLightAt(...)` → `snap.skyLightAt(...)`
//      等同签名快照访问器）与输出容器（局部 verts/idx → ChunkMeshData 成员引用）。顶点输出
//      逐位一致由矩阵 r2013a 承重（geo.vertexData/indexData 与 ChunkMeshData 字节比对）。
//      **单一权威**：迁移后 ChunkGeometry 不再含任何网格逻辑（kFaces/occludesNeighborFace/tileFor/
//      farmlandHydrBrightMul 全部同迁本文件；r2013d 反探钉「网格本体不回流」）——本单最大返工
//      风险正是「顺手复制一份再改」，结构上禁止两份网格逻辑并存。
//
// ── ChunkMeshSnapshot 稠密域设计（自持 / 可独立复制 / 无 QObject / 无指针成员）──────────
//   块·state·光域：pad=2 外扩（覆盖网格循环的最大查询触达——面邻格 ±1、AO 三探针自邻格再 ±1、
//   流向/栅栏/铁轨/红石粉水平探针 ±1），域宽 kDim = 16+2×2 = 20，x/z ∈ [origin-2, origin+17]，
//   y ∈ [0, H)。列顶域（PCF 软影采样源）：顶点 x/z ∈ [origin, origin+kChunk]（右/下边界面
//   顶点恰落 origin+16），沿太阳水平单位向步进 ≤ kMaxShadow=2 格，PCF 每步采样 floor(落点)
//   与 floor(落点)+1 两列（半格列贡献 0.5）→ 前向最大探测列 = origin+16+2+1 = origin+19
//   （t1056 #9：floor 后 +1 半格触达多占 1 列，旧 21 宽域 [origin-2, origin+18] 差此 1 列 →
//   域外 -1 不遮挡而 World 真值路径照常遮挡 = chunk 边缘极端姿态影偏亮）；后向最小探测列 =
//   origin-2（floor 不下探，恰落 -kMaxShadow）。域宽 kTopDim = 22（[origin-2, origin+19]，
//   两端恰紧）。accessor 与 World 同名查询**同 OOB 语义**
//   （blockAtWorld/stateAtWorld 域外/y 界外 → 0 空气；skyLightAt y≥H → 15 开阔天空、余 0；
//   blockLightAt y≥H → 0；columnTopAt 域外 → -1 不遮挡）——域内逐位等于 World 应答（采集即
//   现查），域外等价 World 越界面（移动机械替换后语义不变式）。
//   元数据 = 采集时定格的 ChunkGeometry 烘焙状态（dayMul/sunDir/shadowsEnabled/aoEnabled/
//   greedyMeshing/六段路由开关）+ 原点/世界高——「dayMul/sunDir 等 ChunkGeometry 状态在采集时
//   进快照元数据」（任务书），MeshBuilder 据此烘顶点色，自身零可变状态。
//   **采集成本如实登记**：每次重建新增一次 16×16×H（pad 域 20×20×H）列扫 = 4 数组 ×~38.4k 格查
//   + 22×22 列顶查（≈15 万次 Facade 查询；t1056 #9 域宽 21→22 同步如实化）。旧路径同量级查询散布在循环里（每格被自身 + 6 邻
//   重复读），采集把它们前移为一次性顺序扫——总量同阶、位置集中；这是后续 worker meshing 的
//   输入形态预演（快照可跨线程，网格算法纯函数于快照）。
//
// ── 分层（PLAN §2）──────────────────────────────────────────────────────────────
//   src/World（同 mesher 子系统 partialblockgeometry/voxellight 同层先例）。依据：①消费 Vtx /
//   PartialLightCtx / PartialNeighborCtx（partialblockgeometry.h，World 层）与 Chunk::kSize；
//   ②快照域是 chunk 网格（16×16×H + pad）——放 src/Core 违「Core 叶子不依赖 World」；放
//   src/Renderer 会造成 chunkgeometry(World)→meshbuilder(Renderer) 向上依赖破「依赖只向下」
//   铁律（t41/partialblockgeometry 同族判例）。向下依赖 Core 叶子（result.h QObjectFree 钉、
//   blockregistry 常量）+ World 层头，不依赖 Renderer/Game/QML/QQuick3D。
//
// ── 值纪律（R20.06）────────────────────────────────────────────────────────────
//   ChunkMeshSnapshot / ChunkMeshData 均全值成员（QVector/std::vector 自持，零指针零引用零
//   QObject），头内 static_assert 钉（摘即红在类型层成立）。MeshBuilder 纯静态无实例。

#include <QtGlobal> // quint8 / quint32
#include <QVector>
#include <QVector3D>

#include <type_traits>
#include <vector>

#include "partialblockgeometry.h" // Vtx（chunk 顶点格式 48B）——输出缓冲元素类型（单一权威）
#include "result.h"               // QObjectFree（值纪律编译期钉）

class WorldFacade; // 采集面（worldfacade.h；本头只前置声明，include 留在 .cpp）

// ── ChunkMeshBakeParams：采集时定格的烘焙参数（调用方从 ChunkGeometry 成员拷贝）──────────
// 默认值 = ChunkGeometry 成员默认值逐位对齐（dayMul 1.0 / sunDir 天顶 / 阴影开 / AO·greedy 关 /
// terrain 段 / cutoutFolded 折叠态）。
struct ChunkMeshBakeParams
{
    float dayMul = 1.0f;             // PLAN §2-H 昼夜天光乘子（只乘天光分量）
    QVector3D sunDir{0.f, 1.f, 0.f}; // t123 太阳方向（单位向量）
    bool shadowsEnabled = true;      // t166b PCF 软影开关
    bool aoEnabled = false;          // t1023 AO 接触阴影开关（默认关）
    bool greedyMeshing = false;      // t178 贪婪网格化开关（t183 默认 false）
    bool waterOnly = false;          // t148 水段
    bool lavaOnly = false;           // t343 岩浆段
    bool glassOnly = false;          // t405 玻璃段
    bool iceOnly = false;            // t468 冰段
    bool cutoutOnly = false;         // t326 cutout 段（t860 后 QML 停建）
    bool cutoutFolded = true;        // review28 #4 折叠杠杆（默认折叠）
};

// ── ChunkMeshSnapshot：单 chunk 网格化输入的稠密自持快照（验收②）──────────────────────
struct ChunkMeshSnapshot
{
    // 域几何常量（pad 推导见头注；kChunk 与 Chunk::kSize 的恒等在 meshbuilder.cpp 编译期互钉）。
    static constexpr int kChunk = 16;  // chunk 边长（= Chunk::kSize）
    static constexpr int kPad = 2;     // 块/state/光域外扩（AO 探针 ±2 最大触达；PCF 步进见 kTopLo）
    static constexpr int kDim = kChunk + 2 * kPad; // 20：块·state·光方域宽
    static constexpr int kTopLo = -2;              // 列顶域下界（PCF 后向探测恰 -kMaxShadow；floor 不下探）
    static constexpr int kTopDim = 22;             // 列顶域宽（[-2, +19]；t1056 #9：PCF 半格触达前向 +19）

    // ── 元数据（采集时定格；含 ChunkGeometry 烘焙状态——MeshBuilder 零外部可变输入）──
    int originX = 0; // chunk 世界起点（cx * kChunk）
    int originZ = 0;
    int height = 0;                  // 世界高 H（0 = 无效/空快照 → build 产出空 mesh）
    float dayMul = 1.0f;
    QVector3D sunDir{0.f, 1.f, 0.f};
    bool shadowsEnabled = true;
    bool aoEnabled = false;
    bool greedyMeshing = false;
    bool waterOnly = false;
    bool lavaOnly = false;
    bool glassOnly = false;
    bool iceOnly = false;
    bool cutoutOnly = false;
    bool cutoutFolded = true;

    // ── 稠密域（自持缓冲；索引布局 (lx+kPad) + kDim*((lz+kPad) + kDim*ly)）──────────────
    std::vector<quint8> blocks;     // 体素 id（世界坐标块域）
    std::vector<quint8> states;     // 异形 state
    std::vector<quint8> skyLight;   // 天光（0..15）
    std::vector<quint8> blockLight; // 方块光（0..15）
    std::vector<float> columnTop;   // 列顶实面世界 y（World::columnTopSurfaceY 采集值；空列/越界 -1）

    static size_t cellIndex(int lx, int ly, int lz)
    {
        return size_t(lx + kPad) + size_t(kDim) * (size_t(lz + kPad) + size_t(kDim) * size_t(ly));
    }
    static size_t topIndex(int lx, int lz)
    {
        return size_t(lx - kTopLo) + size_t(kTopDim) * size_t(lz - kTopLo);
    }

    bool valid() const
    {
        const size_t want = size_t(kDim) * size_t(kDim) * size_t(height > 0 ? height : 0);
        return height > 0 && blocks.size() == want && states.size() == want
            && skyLight.size() == want && blockLight.size() == want
            && columnTop.size() == size_t(kTopDim) * size_t(kTopDim);
    }

    // ── 查询面：与 ChunkGeometry 原私有帮手 / World 查询**同名同签名同 OOB 语义**──────
    // （buildMesh 体机械替换 `m_world->`/裸调用 → `snap.` 后语义不变式成立的关键）。
    // 世界坐标体素 id（World::blockAt 同语义：y 界外/域外 → 0 空气）。
    quint8 blockAtWorld(int wx, int wy, int wz) const
    {
        if (wy < 0 || wy >= height)
            return 0;
        const int lx = wx - originX, lz = wz - originZ;
        if (lx < -kPad || lx >= kChunk + kPad || lz < -kPad || lz >= kChunk + kPad)
            return 0;
        return blocks[cellIndex(lx, wy, lz)];
    }
    // 世界坐标 state（World::stateAt 同语义：界外 → 0）。
    quint8 stateAtWorld(int wx, int wy, int wz) const
    {
        if (wy < 0 || wy >= height)
            return 0;
        const int lx = wx - originX, lz = wz - originZ;
        if (lx < -kPad || lx >= kChunk + kPad || lz < -kPad || lz >= kChunk + kPad)
            return 0;
        return states[cellIndex(lx, wy, lz)];
    }
    // 天光（World::skyLightAt 同语义：y≥height 开阔天空 15；y<0/域外 → 0）。
    quint8 skyLightAt(int wx, int wy, int wz) const
    {
        if (wy >= height)
            return 15;
        if (wy < 0)
            return 0;
        const int lx = wx - originX, lz = wz - originZ;
        if (lx < -kPad || lx >= kChunk + kPad || lz < -kPad || lz >= kChunk + kPad)
            return 0;
        return skyLight[cellIndex(lx, wy, lz)];
    }
    // 方块光（World::blockLightAt 同语义：y≥height → 0；y<0/域外 → 0）。
    quint8 blockLightAt(int wx, int wy, int wz) const
    {
        if (wy >= height)
            return 0;
        if (wy < 0)
            return 0;
        const int lx = wx - originX, lz = wz - originZ;
        if (lx < -kPad || lx >= kChunk + kPad || lz < -kPad || lz >= kChunk + kPad)
            return 0;
        return blockLight[cellIndex(lx, wy, lz)];
    }
    // 列顶实面（ChunkManager::columnTopSurfaceY 同语义：域外 → -1 不遮挡）。t1056 #9 起域
    // [kTopLo, kTopLo+kTopDim) 已盖满 PCF 全部探测触达——域外 -1 分支保留为纯防御（正常路径恒不触），
    // 域外即返回 -1 的行为面由矩阵 r2030a 以真实快照逐探测列对 World 真值核验。
    float columnTopAt(int wx, int wz) const
    {
        const int lx = wx - originX, lz = wz - originZ;
        if (lx < kTopLo || lx >= kTopLo + kTopDim || lz < kTopLo || lz >= kTopLo + kTopDim)
            return -1.0f;
        return columnTop[topIndex(lx, lz)];
    }

    // 逐域恒等（元数据精确比对 + 五数组全等；r2013b 快照拷贝独立性断言面）。
    bool identical(const ChunkMeshSnapshot &o) const
    {
        return originX == o.originX && originZ == o.originZ && height == o.height
            && dayMul == o.dayMul && sunDir.x() == o.sunDir.x() && sunDir.y() == o.sunDir.y()
            && sunDir.z() == o.sunDir.z() && shadowsEnabled == o.shadowsEnabled
            && aoEnabled == o.aoEnabled && greedyMeshing == o.greedyMeshing
            && waterOnly == o.waterOnly && lavaOnly == o.lavaOnly && glassOnly == o.glassOnly
            && iceOnly == o.iceOnly && cutoutOnly == o.cutoutOnly && cutoutFolded == o.cutoutFolded
            && blocks == o.blocks && states == o.states && skyLight == o.skyLight
            && blockLight == o.blockLight && columnTop == o.columnTop;
    }
};

// 值纪律编译期钉（R20.06 snapshot.h 同款：owning 缓冲 → 可拷贝构造/可赋值而非 trivially copyable）。
static_assert(QObjectFree<ChunkMeshSnapshot>, "ChunkMeshSnapshot must not carry QObject (plan §4.2/R20.13)");
static_assert(std::is_copy_constructible_v<ChunkMeshSnapshot> && std::is_copy_assignable_v<ChunkMeshSnapshot>,
              "ChunkMeshSnapshot must be independently copyable (plan §29.3 R20.13)");

// ── ChunkMeshData：MeshBuilder 的 owning 输出（验收③）────────────────────────────────
// 顶点/索引缓冲 + 统计全部自持；ChunkGeometry（旧 QtQuick3DAdapter 形态）按需灌 QQuick3D，
// 未来 worker meshing 可整体 move 跨线程。布局与旧 buildMesh 局部 QVector 逐位同构
// （Vtx 48B stride / quint32 索引 / Triangles 每面 6 索引）。
struct ChunkMeshData
{
    QVector<Vtx> vertices;    // 自持顶点缓冲（Vtx = pos3+normal3+uv2+color4，partialblockgeometry.h）
    QVector<quint32> indices; // 自持索引缓冲
    int vertexCount = 0;      // 统计自持（t10 F3 叠层口径；= vertices.size()）
    int triangleCount = 0;    // = indices.size() / 3
};

static_assert(QObjectFree<ChunkMeshData>, "ChunkMeshData must not carry QObject (plan §29.3 R20.13)");
static_assert(std::is_copy_constructible_v<ChunkMeshData> && std::is_copy_assignable_v<ChunkMeshData>,
              "ChunkMeshData must be independently copyable (plan §29.3 R20.13)");

// ── MeshBuilder：CPU 网格算法单一权威（验收①⑤）──────────────────────────────────────
// 纯静态无实例：build(快照, 原因) → owning ChunkMeshData。零 World 指针、零 QQuick3D、零可变
// 全局（mask 缓冲/UV 常量均函数局部）——同输入同输出（线程化就绪的纯函数形态）。
class MeshBuilder
{
public:
    // 重建原因（镜像 ChunkGeometry::RebuildReason；恒等在 chunkgeometry.cpp 编译期互钉）。
    enum class Reason { Dirty, Sun, Water };

    static ChunkMeshData build(const ChunkMeshSnapshot &snap, Reason reason);

private:
    MeshBuilder() = delete; // 纯静态工具，无实例。
};

// ── captureChunkMeshSnapshot：稠密快照采集（16×16×H 列扫一次；验收②生产侧）────────────
// 经 WorldFacade 收窄查询面逐格现采（blockAt/stateAt/光照/height/columnTopSurfaceY）——采集
// 后快照自持，底层世界后续改动不影响已采快照（r2013b 行为级钉）。只应在持有世界的线程调
// （同步采集，无线程原语——t1023c 白名单纪律不触）。
ChunkMeshSnapshot captureChunkMeshSnapshot(const WorldFacade &world, int cx, int cz,
                                           const ChunkMeshBakeParams &bake);

#endif // MESHBUILDER_H
