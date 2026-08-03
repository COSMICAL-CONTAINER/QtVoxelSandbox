#ifndef WORLD_H
#define WORLD_H

#include <QObject>
#include <QtGlobal> // quint32（hashColumn 确定性哈希返回类型）
#include <QtQml/qqml.h>

#include <vector>

#include "blockregistry.h" // isCollidable 走 BlockDef.solid（t88 火把 non-solid 不挡玩家）
#include "chunkmanager.h" // 内部多 chunk 存储（World 层，不外泄到 QML）

// 体素世界（QML façade + 单一数据源）：内部由 ChunkManager 持一片 chunk 列网格路由（本回合
// 3×3=9 chunk，世界 48×48×16）；Perlin fBm 生成地形，被网格(ChunkGeometry)与物理
// (PlayerController)共同查询 —— 二者读同一份栅格，保证「看得见的方块=碰得到的方块」。
//
// 对外 QML API：blockAt/isSolid/setBlock/width/depth/height/seed/worldChanged/blockBroken/
// blockPlaced（与单 chunk 版完全一致；Main.qml 仅改 width/depth=48）。多 chunk 化是实现细节，
// 不外泄（PLAN §2 不变量 J：单层 chunk 抽象，不引入 chunklet）。
//
// 方块 id（定义见 BlockRegistry）：0=air 1=grass 2=dirt 3=stone 4=cobble
// 5=log 6=planks 7=leaves 8=sand。+Y 朝上。世界坐标越界 blockAt 返回 0(空气)。
class World : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(World)
    Q_PROPERTY(int width  READ width  WRITE setWidth  NOTIFY widthChanged)
    Q_PROPERTY(int depth  READ depth  WRITE setDepth  NOTIFY depthChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(int seed   READ seed   WRITE setSeed   NOTIFY seedChanged)
    // chunk 列网格尺寸（= ceil(width/16)、ceil(depth/16)）；暴露给 Renderer/QML，供 t03 每
    // chunk mesher 的 Repeater 决定 Model 数量、t10 F3 计数。仅随 width/depth 变化。
    Q_PROPERTY(int chunksX READ chunksX NOTIFY widthChanged)
    Q_PROPERTY(int chunksZ READ chunksZ NOTIFY depthChanged)

public:
    explicit World(QObject *parent = nullptr);

    int width() const  { return m_width; }
    int depth() const  { return m_depth; }
    int height() const { return m_height; }
    int seed() const   { return m_seed; }
    int chunksX() const { return m_chunks.chunksX(); }
    int chunksZ() const { return m_chunks.chunksZ(); }
    void setWidth(int w);
    void setDepth(int d);
    void setHeight(int h);
    void setSeed(int s);

    // 越界返回 0（空气）。跨 chunk 由 ChunkManager 路由；网格与物理都用它。
    Q_INVOKABLE quint8 blockAt(int x, int y, int z) const;
    // 是否「存在方块」（非 air）：raycast 选体 / 掉落实体着地 / mesher 邻居剔除 等用。
    // 注：名为 isSolid 但语义是「非 air 实存」（保留以兼容既有调用点）。碰撞语义见 isCollidable。
    Q_INVOKABLE bool isSolid(int x, int y, int z) const { return blockAt(x, y, z) != 0; }
    // t220 该格方块是否「完整立方」（BlockRegistry::isFullCube(blockAt)），供呈现层 maybeTriggerFallingBlock
    //   判「沙下方是否可支撑」：仅完整立方支撑沙；下方为 air / 水 / 不完整方块（火把 / 半砖 / ...）→ 沙失撑
    //   下落（沙落水穿透填堵水格、沙遇不完整方块变掉落物 由 EntityManager::tick 落体判定）。与 isSolid
    //   （「非 air 实存」）分离 —— isSolid 把水 / 火把 / 半砖当实存（旧落沙 isSolid 停格 / 射线命中用），
    //   isFullCubeAt 严判完整立方（t213/t220/t226 共用基础谓词 isFullCube 的世界坐标版）。
    Q_INVOKABLE bool isFullCubeAt(int x, int y, int z) const {
        return BlockRegistry::isFullCube(blockAt(x, y, z));
    }
    // 是否「实体碰撞」：t146 走 BlockRegistry::BlockDef.shape；t152 改读 state + isCollidableWhenClosed
    //   （门/活版门合态挡、开态通）。solid 仅作 mesher 邻居面剔除依据（不完整方块 solid=false → 不挡邻居
    //   整面）；**碰撞**看 shape + 开合态：常规整立方 / 不完整方块（slab/stairs/...）恒挡玩家（逐形状 sub-AABB
    //   精确，见 collisionAABBsAt）；门 / 活版门**合态**挡、**开态**通（玩家穿过）；air/torch/water 不挡。
    //   与 isSolid（「有方块」= blockAt!=0，raycast 命中用）分离：不完整方块 isSolid=true（射线命中）且
    //   isCollidable=true（合态挡玩家，但只在其 sub-AABB 范围内）；torch isSolid=true（可着地/命中）但
    //   isCollidable=false（穿过）；开门 isSolid=true（射线命中）但 isCollidable=false（穿过，spec「开门通」）。
    Q_INVOKABLE bool isCollidable(int x, int y, int z) const {
        return BlockRegistry::isCollidableWhenClosed(blockAt(x, y, z), stateAt(x, y, z));
    }
    // t146 给定格的碰撞 sub-AABB（**世界坐标**；cell-local AABB + (x,y,z) 偏移）。air/torch → 空；
    //   常规整立方 → 单盒覆盖整格；不完整方块 → 形状对应的多 sub-AABB（slab/stairs/fence/plate/door/
    //   trapdoor，state 解码同 partialblockgeometry）。玩家碰撞迭代玩家 AABB 覆盖的所有格，逐 sub-AABB
    //   做 3 轴严格重叠测试（aabbHitsSolid / moveAxis 贴面 / overlapsPlayerAABB 放置校验）。
    //   分层（PLAN §2）：本方法属 World 层，只读 ChunkManager（blockAt + stateAt）+ BlockRegistry，
    //   不依赖 Renderer/Physics；PlayerController（Game/Physics）只读消费。
    std::vector<BlockRegistry::BlockAABB> collisionAABBsAt(int x, int y, int z) const;

    // t121 天光 heightmap（PLAN §2-H「per-column 天光——自顶向下首个实体」）：世界坐标列首个非空气的 y
    //（越界 / 空列 → -1）。mesher 据此判顶点见天（ly >= hm → 天光 1.0）/ 地下（暗 0.2）。经 ChunkManager
    // 路由到 chunk 局部列；heightmap 由 setBlock 增量维护（worldgen / 玩家编辑 / 实体写入均经此入口）。
    Q_INVOKABLE int heightmapAt(int x, int z) const { return m_chunks.heightmapAt(x, z); }
    // t137 出生贴地表：worldgen 地表高度（纯函数于 seed + fbm，同 generate() 填充用的 heightAt）。
    //   暴露给 Game 层（PlayerController）查出生列地表，把玩家脚底贴到 h+1（消除 kSpawnY 兜底落差摔伤）。
    //   与 heightmapAt 的差异：heightAt = worldgen 地表（不含树 / 玩家编辑），heightmapAt = 当前列首个非空
    //   （含增量）。出生用 heightAt —— 贴 worldgen 地表，语义稳定（同 seed 同地表）。只读查询，不改栅格。
    Q_INVOKABLE int heightAt(int x, int z) const;

    // t151 真光场查询（PLAN §2-H / §M）：世界坐标 per-voxel 天光 / 方块光（各 0..15）。mesher 据此写顶点色。
    //   光场由 BFS flood-fill 算出：worldgen 末走全量 recomputeLightField()；玩家编辑（setBlock / 实体写入）
    //   走**增量** recomputeLightAround()（t154，编辑格周围有界盒重 flood，避免每次全量卡顿）。存 chunk
    //   第三数组。OOB 语义：y >= height → 天光 15（开阔天空，顶面采样）/ 方块光 0；y < 0 或 x/z 越界 → 0。
    //   Q_INVOKABLE 便于 F3 调试叠层（规划）查光；mesher 经 m_world 调用。只读，不改栅格。
    Q_INVOKABLE quint8 skyLightAt(int x, int y, int z) const;
    Q_INVOKABLE quint8 blockLightAt(int x, int y, int z) const;

    // 写栅格的唯一入口（PLAN §2-C 精神：当前 GUI 线程单写者）。经 ChunkManager 跨 chunk 写入 +
    // 标目标 chunk 脏；该格贴 chunk 边沿（x/z 在 16 边）→ 同标邻接 chunk 脏（为 t03 跨边界剔除）。
    // 越界 / 无变化返回 false。成功改动后发 blockBroken/blockPlaced（语义事件，供 t14 粒子 / t11 音效）
    // 与 worldChanged（驱动 ChunkGeometry 重建；本回合仍重建整个单 mesh，mesher 拆分归 t03）。
    Q_INVOKABLE bool setBlock(int x, int y, int z, quint8 id);
    // t133 不完整方块 state（朝向 / 开合）：世界坐标读 / 写（跨 chunk 路由）。
    //   setBlock(x,y,z,id,state)：写 id + state；id 不变只 state 变（如门开合）不发 broken/placed，但仍
    //   发 worldChanged（驱动 mesh 重建 —— 异形方块开合需重网格化）。4 参数 setBlock 不改 state 语义
    //   （id 变才走写入路径 → 重置 state=0，由 ChunkManager 4 参数版委托 (id,0) 实现）。
    Q_INVOKABLE quint8 stateAt(int x, int y, int z) const;
    Q_INVOKABLE bool setBlock(int x, int y, int z, quint8 id, quint8 state);

    // t117/t220 FallingBlock 着地专用：经 m_chunks.setBlock 直写（跨 chunk 路由 + 标脏 + 边界邻接，同 setBlock
    //   的写入路径）+ emit worldChanged（驱动 mesh 重建），但**不**发 blockPlaced（与玩家放置语义分离）。
    //   沿用 worldgen 经 m_chunks.setBlock 直写不触发 blockPlaced 的既有约定——避免 onBlockPlaced 的 survival
    //   takeStack 误触、多余粒子 / 音效把链式塌落刷成噪音。仅在目标格当前为**空气或水**时写入（t220：着地格
    //   由 FallingBlock 列扫保证为 air/水 —— 沙落水穿透后填堵水格；防御：其余已占用方块不覆盖）。越界 /
    //   非空非水 → false。非 Q_INVOKABLE（仅 EntityManager C++ 调）。
    bool setBlockFromEntity(int x, int y, int z, quint8 id);
    // t174 水流系统静默写入（同 setBlockFromEntity 语义：经 m_chunks.setBlock 直写 + emit worldChanged
    //   重建 mesh，但**不**发 broken/placed —— 水流蔓延是系统模拟非玩家动作，避免触发粒子/音效/拾取噪音）。
    //   与 setBlockFromEntity 的差异：支持 state（水流 state 1..7 编码蔓延距离）；无条件覆盖（水流可改既有
    //   水格的 state，或把蒸发的水格写回 air）。id==Air 且 state==0 表示蒸发（清格）。越界 / 无变化 → false。
    //   id 合法性由 caller 保证（tickWaterFlow 仅传 Water/Air）。复用 recomputeLightAround 增量光照（水
    //   isSolid=false → 非遮光，光场通常无变化，内部早退）。
    //   t174 舀水（playercontroller 空桶）亦走本方法 —— 舀走水格是水流系统操作（非玩家破块），避免 setBlock
    //   触发 blockBroken(Water) 的破块粒子/音（机制等价 MC 舀水无反馈）。非 Q_INVOKABLE（C++ 调）。
    bool setWaterSilent(int x, int y, int z, quint8 id, quint8 state);
    // t185 水流重做（增量波前扩散；修 t174 全量 BFS 的「瞬间填平 + 闪烁」bug）。由呈现层 Main.qml 经
    //   WorldClock.ticked 桥接调用（每 100ms 一 tick；本方法内部节流到 ~每 0.3s 推进波前 1 格）。
    //   机制等价 MC 1.0 流水：水源（state=0）向外**1 格 1 格**水平蔓延出 state 1..7 的流水（最大 7 格扩散
    //   距离；spec 的「8 格」含水源算），每扩散 1 格 level+1（水位降 1）；遇悬崖（下方为 air）→ 下落为**流水**
    //   level=1（**非水源** —— t174 把下落当源致「灌满整个盆地」之 bug 即此修复），落到地面 grounded 后继续
    //   水平蔓延；流水失支撑（水源被舀/隔断）→ 逐环蒸发（1 格/tick 渐退）。最终流场收敛停（不填满整个平面）。
    //   稳态流场（海洋全源）每 tick 无变化 → setWaterSilent 全 false → 不触发 worldChanged（无重建、无闪烁）。
    //   玩家倒水（setBlock Water state=0）/ 舀水（setWaterSilent Air）改水格 → 下一 tick 波前自动增/退。
    //   **t224 水融合（MC 1.0 水合并 + 源再生调研后实现）**：新增两机制消除「两股流水相遇明显边界 / 各为
    //   固方块」(i) **源再生 pass**（infinite-water rule）—— 流水格被 ≥2 水平水源邻居夹住 + 下方实体/水源
    //   → 升为水源（级联多 tick；2×2 池 / 两源夹一格 → 中间升源 = 两滩融合为连续水源体）；(ii) **re-leveling**
    //   —— 既有流水邻居若能被提供更低 level（更近源）则下调（旧「只写 air、首达者独占」致中线阶梯边界 → 下调
    //   使中线格 = min(两源距)，多 tick 收敛为 V 形平滑）。MC level 语义 = min(源到该格曼哈顿距离)，两流相遇
    //   天然平滑无硬边界。worldgen 海/湖全为水源 → 稳态零变化；玩家单桶水仅 1 源邻居 → 不升源（同 MC）。
    Q_INVOKABLE void tickWaterFlow();

    // 暴露内部 chunk 网格给 Renderer/Game 层（只读引用；t03 per-chunk mesher、t10 F3 计数用）。
    const ChunkManager &chunks() const { return m_chunks; }

    // t176 存档加载（PLAN §2-L SQLite WorldStore；World 层）：beginLoad 把世界重置为目标 seed 的
    //   零填充分区网格（**不**走 generate —— 加载用 DB 里的 chunk blob 覆盖，玩家编辑过的地形从存档
    //   恢复，而非 worldgen 重生），finishLoad 在 WorldStore 写完所有 chunk blob 后调（重算 heightmap +
    //   标全脏 + emit worldChanged → 25 个 ChunkGeometry 重建为加载地形）。二者配套：beginLoad 不 emit
    //   worldChanged（此时网格为零填充，重建无意义），finishLoad 才一次性触发重建。
    //   尺寸固定（80×80×64，QML 构造期定稿），仅 seed 随存档变 → recreate 用现有 m_width/depth/height。
    Q_INVOKABLE void beginLoad(int seed);
    Q_INVOKABLE void finishLoad();
    // t176 新世界生成入口（与 beginLoad 互补）：按 seed 全量 worldgen（recreate 网格 + 地形 / 树 / 矿 / 水 /
    //   基岩 / 光场）+ emit seedChanged/worldChanged。与 setSeed 的差异：setSeed 在新 seed == 旧 seed 时
    //   no-op（不重生），而本方法无条件重生（切到同 seed 的不同存档时仍要重建网格 —— 否则上一世界的编辑
    //   残留）。供 WorldStore「无存档地形 → 新世界」路径调。
    Q_INVOKABLE void regenerate(int seed);

signals:
    void widthChanged();
    void depthChanged();
    void heightChanged();
    void seedChanged();
    void worldChanged(); // 生成/编辑后发出 → 网格重建
    // 编辑语义事件（id：broken 带被破的原方块 id；placed 带新放方块 id）。
    void blockBroken(int x, int y, int z, int id);
    void blockPlaced(int x, int y, int z, int id);

private:
    void generate();          // 重建置换表 + ChunkManager + 填充地形（静默，不 emit）
    void buildPermutation();  // 由 seed 填 512 置换表（线性同余，可复现）
    double noise2(double x, double z) const;
    double fbm(double x, double z) const;
    // t117 沙漠群系判定（二次 fBm biome，PLAN §2-K 确定性）：与高度噪声独立采样（不同空间频率 + seed
    // 偏移），低频 → 大区块连续（沙丘 / 平原成片，非逐格斑点）。阈值化得干旱 / 普通二分群系。纯函数
    // 于 seed → 同 seed 同群系分布（与 heightAt 同源确定性）。
    bool isDesert(int x, int z) const;

    // 确定性树木生成（PLAN §2-K）：在 generate() 末段于 grass 表层种橡树（原木主干+树叶球冠）。
    // 位置/形状纯由 seed 决定；禁用任何运行期随机源（QTime/时钟/全局 RNG）。
    void placeTrees();                                  // 遍历列、密度+间距筛选后散布
    // 单棵树：主干 trunkH 格 + 树冠。leafRand = 该列哈希的高位，驱动树冠四角叶的有无 → 每棵树冠轮廓
    // 各异（贴近 MC 橡树自然参差）。纯由 seed 派生（确定性，PLAN §2-K）。
    void placeTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand);
    // 确定性矿石散布（t84，PLAN §2-K）：地形填充后遍历 stone 区段，按 hashVoxel(seed,x,y,z)
    // 决定是否替换为煤矿/铁矿。仅替换 Stone；同 seed → 同矿脉分布；禁用任何运行期随机源。
    void scatterOres();
    // t119 底层基岩（PLAN §2-K 确定性）：地形填充后在 y 0..4 铺一层 Bedrock（不可破坏，hardness=-1.0）。
    // 厚度按 hashVoxel 坑洼（底实顶疏，机制等价 MC 1.0 基岩层）。仅覆盖最底几格；同 seed → 同分布。
    void placeBedrock();
    // t148 海平面填水（PLAN §2-K 确定性）：地形填充后在 waterLevel 以下的低洼列从 h+1 到 waterLevel
    //   填 Water（机制等价 MC 海洋 / 湖泊）。仅写空气格；同 seed → 同水域分布。waterLevel 见 .cpp 注释
    //   （spec 8 为 t119 前地形，现重定标到 24 以与 [16,40] 地形相交使水域可见）。
    void fillWater();
    // t235 草丛散布（PLAN §2-K 确定性）：地形 + 树 + 水定型后，遍历 grass 表层列（非沙漠 / 非沙滩水下 / 非水域），
    //   按 hashColumn(seed,x,z) 密度筛选在 grass 顶上方一格（surfaceY+1）置 TallGrass（仅写空气格 → 不覆盖
    //   树干 / 树叶 / 水）。同 seed → 同草丛分布；禁用任何运行期随机源。机制等价 MC 平原草丛点缀。
    void placeTallGrass();
    // t151 真光场**全量**重算（PLAN §2-H / §M）：per-voxel BFS flood-fill 天光（自顶，sky=15）+ 火把方块光
    //   （radius14，block=14），衰减 1、仅穿过非遮光格、取 max。**仅 worldgen 末调一次**（全图 147k 体素 ×2
    //   通道约数十 ms，玩家编辑频率下不可接受）。玩家编辑走增量 recomputeLightAround()（t154）。
    //   存 chunk 第三数组（m_lightField）。时间不变（昼夜乘子由 QML baseColor 承担）。无随机源（纯栅格）。
    void recomputeLightField();
    // t154 增量光场（核心 perf）：编辑格周围有界盒内重 flood，替代每次 setBlock 的全量重算。依据 oldId/newId
    //   判定影响面：遮光变化（isSolid 翻转）→ 天光列 first-opaque 可能翻转 → 整列高 ±15 盒、两通道重 flood；
    //   仅火把增删（遮光不变）→ 火把半径 ±15 盒、只重 flood 方块光（天光不动）。id 不变且非火把（如门开合）→
    //   光照无变化，直接 return（不发世界重算）。盒半径 = 最大光值 15：编辑格对盒外格曼哈顿距离 ≥16 >15 →
    //   盒外光值必不被编辑影响，故盒外作固定边界种子衰减 1 流入盒内，盒内清零后从种子重传播 → 结果与全量一致。
    void recomputeLightAround(int x, int y, int z, quint8 oldId, quint8 newId);
    // t154 有界盒清场 + 重 seed + 重 flood（recomputeLightAround 的实现核心，分离以便复用清/种/传播步骤）。
    //   doSky=true：两通道都重算（清两通道、重 seed 见天格 + 火把、边界种两通道、flood 两通道）。
    //   doSky=false：仅方块光（清方块光保留天光、重 seed 火把、边界种方块光、flood 方块光）——火把增删天光不变。
    void refloodBox(int x0, int y0, int z0, int x1, int y1, int z1, bool doSky);
    void setVoxelIfAir(int x, int y, int z, quint8 id);       // 仅写空气格（树冠不覆盖主干/地形）
    quint32 hashColumn(int seed, int x, int z) const;         // 整数哈希（列级 seed/x/z）→ 确定性伪随机
    quint32 hashVoxel(int seed, int x, int y, int z) const;   // 整数哈希（体素级 seed/x/y/z）→ 矿石散布用

    std::vector<int> m_perm;  // 512 置换表（Perlin）
    int m_width = 16, m_depth = 16, m_height = 16, m_seed = 1337;
    ChunkManager m_chunks;    // 多 chunk 存储 + 跨 chunk 路由（World 层；默认空，generate 重建）
    // t185 水流 tick 节流计数：tickWaterFlow() 每 100ms 被 WorldClock.ticked 调一次；累积到 kFlowTickInterval
    //   才把波前推进 1 格（~0.3s 一格 → 1 格/tick 流动动画可见）。MC 自身约 0.25s/格，本工程取 3（0.3s）平衡
    //   动画可见度与扫描开销（全图扫水格 ~1-2ms + 波前少量写入）。
    int m_flowTickCounter = 0;
    static constexpr int kFlowTickInterval = 3;   // tickWaterFlow 节流间隔（WorldClock tick 单位 = 100ms → 0.3s/格）
    static constexpr int kMaxFlowLevel = 7;       // 水流最大蔓延等级（state 1..7；机制等价 MC 1.0 流水 7 格扩散）
};

#endif // WORLD_H
