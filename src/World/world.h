#ifndef WORLD_H
#define WORLD_H

#include <QObject>
#include <QtGlobal> // quint32（hashColumn 确定性哈希返回类型）/ quint64（树叶衰减队列键）
#include <QtQml/qqml.h>

#include <unordered_set> // t325 树叶渐进衰减队列 m_decayingLeaves（坐标打包键的去重集合）
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
    // t385 天气态（全局：晴/雨/雪/雷；QML 据此变暗天空 + 切粒子）。NOTIFY=weatherChanged 驱动刷新。
    Q_PROPERTY(int weatherState READ weatherState NOTIFY weatherChanged)
    // t385 天空变暗乘子 [0,1]（0=晴不变暗；Thunder 最暗）。QML clearColor/cloudColor 据此拉暗。
    Q_PROPERTY(float weatherDarkness READ weatherDarkness NOTIFY weatherChanged)

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
    // 是否「实体碰撞」：t146 走 BlockRegistry::BlockDef.shape；t152 读 state；t261 门恒挡（门板开合都实存）。
    //   solid 仅作 mesher 邻居面剔除依据（不完整方块 solid=false → 不挡邻居整面）；**碰撞**看 shape + 开合态：
    //   常规整立方 / 不完整方块（slab/stairs/...）恒挡玩家（逐形状 sub-AABB 精确，见 collisionAABBsAt）；
    //   门**无论开合**都挡（门板实存于某一边：合=贴朝向边、开=旋 90° 贴铰链侧邻边 → 撞门板被挡、门洞方向
    //   可穿过，t261 修「开门四向全通」）；活版门合态挡、开态通（玩家穿过）；air/torch/water 不挡。
    //   与 isSolid（「有方块」= blockAt!=0，raycast 命中用）分离：不完整方块 isSolid=true（射线命中）且
    //   isCollidable=true（挡玩家，但只在其 sub-AABB 范围内）；torch isSolid=true（可着地/命中）但
    //   isCollidable=false（穿过）；开门 isSolid=true（射线命中）且 isCollidable=true（门板仍挡一面，t261）。
    Q_INVOKABLE bool isCollidable(int x, int y, int z) const {
        return BlockRegistry::isCollidable(blockAt(x, y, z), stateAt(x, y, z));
    }
    // t393 该格箱子是否「地牢生成箱」（worldgen placeDungeons 写入的箱子，state 带 ChestStateDungeonFlag bit2；
    //   玩家放置的箱子 state 仅低 2 位朝向，无此位）。Main.qml.openChest 据此判「是否首开填充地牢战利品」。
    //   分层（PLAN §2）：纯只读谓词（blockAt + stateAt + BlockRegistry），不写栅格。非箱子格 → false。
    Q_INVOKABLE bool isDungeonChest(int x, int y, int z) const {
        return blockAt(x, y, z) == BlockRegistry::Chest
            && (stateAt(x, y, z) & BlockRegistry::ChestStateDungeonFlag) != 0;
    }
    // t484 该格箱子是否「废弃矿井生成箱」（worldgen placeMineshaft 写入的箱子，state 带
    //   ChestStateMineshaftFlag bit3；玩家放置的箱子 / 地牢箱无此位）。Main.qml.openChest 据此判
    //   「是否首开填充矿井战利品」（LootTable::mineshaftChestPool）。分层（PLAN §2）：纯只读谓词
    //   （blockAt + stateAt + BlockRegistry），不写栅格。非箱子格 → false。
    Q_INVOKABLE bool isMineshaftChest(int x, int y, int z) const {
        return blockAt(x, y, z) == BlockRegistry::Chest
            && (stateAt(x, y, z) & BlockRegistry::ChestStateMineshaftFlag) != 0;
    }
    // t485 该格箱子是否「沙漠神殿生成箱」（worldgen placeDesertTemple 写入的箱子，state 带
    //   ChestStatePyramidFlag bit4；玩家放置的箱子 / 地牢箱 / 矿井箱无此位）。Main.qml.openChest 据此判
    //   「是否首开填充沙漠神殿战利品」（LootTable::pyramidChestPool：钻石 / 金 / 青金石 / 骨头 / 腐肉等）。
    //   分层（PLAN §2）：纯只读谓词（blockAt + stateAt + BlockRegistry），不写栅格。非箱子格 → false。
    Q_INVOKABLE bool isPyramidChest(int x, int y, int z) const {
        return blockAt(x, y, z) == BlockRegistry::Chest
            && (stateAt(x, y, z) & BlockRegistry::ChestStatePyramidFlag) != 0;
    }
    // t486 该格箱子是否「丛林神殿生成箱」（worldgen placeJungleTemple 写入的箱子，state 带
    //   ChestStateJungleFlag bit5；玩家放置的箱子 / 地牢箱 / 矿井箱 / 神殿箱无此位）。Main.qml.openChest 据此判
    //   「是否首开填充丛林神殿战利品」（LootTable::jungleTempleChestPool：骨头 / 腐肉 / 铁 / 金 / 钻石 / 箭等）。
    //   分层（PLAN §2）：纯只读谓词（blockAt + stateAt + BlockRegistry），不写栅格。非箱子格 → false。
    Q_INVOKABLE bool isJungleTempleChest(int x, int y, int z) const {
        return blockAt(x, y, z) == BlockRegistry::Chest
            && (stateAt(x, y, z) & BlockRegistry::ChestStateJungleFlag) != 0;
    }
    // t487 该格箱子是否「要塞生成箱」（worldgen placeStronghold 写入的箱子，state 带
    //   ChestStateStrongholdFlag bit6；玩家放置的箱子 / 地牢箱 / 矿井箱 / 神殿箱 / 丛林神殿箱无此位）。
    //   Main.qml.openChest 据此判「是否首开填充要塞战利品」（LootTable::strongholdChestPool：末影之眼 / 骨头 /
    //   腐肉 / 铁锭 / 青金石 / 红石 / 钻石 / 附魔书等）。分层（PLAN §2）：纯只读谓词（blockAt + stateAt +
    //   BlockRegistry），不写栅格。非箱子格 → false。
    Q_INVOKABLE bool isStrongholdChest(int x, int y, int z) const {
        return blockAt(x, y, z) == BlockRegistry::Chest
            && (stateAt(x, y, z) & BlockRegistry::ChestStateStrongholdFlag) != 0;
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
    // t360 列顶实面世界 y（= heightmapAt + solidTopOffset）：PCF 软影按方块真实模型高度判遮挡
    //   （修「下半砖 / 合活版门被当整格高 → 投出整格黑影、邻地误暗」）。空列 / 越界（heightmap<0）→ -1（不遮挡）。
    //   非 Q_INVOKABLE（仅 VoxelLight C++ 调；与 heightmapAt 同只读语义）。
    float columnTopSurfaceY(int x, int z) const;
    // t137 出生贴地表：worldgen 地表高度（纯函数于 seed + fbm，同 generate() 填充用的 heightAt）。
    //   暴露给 Game 层（PlayerController）查出生列地表，把玩家脚底贴到 h+1（消除 kSpawnY 兜底落差摔伤）。
    //   与 heightmapAt 的差异：heightAt = worldgen 地表（不含树 / 玩家编辑），heightmapAt = 当前列首个非空
    //   （含增量）。出生用 heightAt —— 贴 worldgen 地表，语义稳定（同 seed 同地表）。只读查询，不改栅格。
    Q_INVOKABLE int heightAt(int x, int z) const;
    // t374 群系查询（暴露给上层 Entities / 呈现层做群系化逻辑；worldgen 私有 Biome 枚举不外泄类型，仅返 int
    //   编码）。编码同私有 enum Biome：0=Plains, 1=Hills, 2=Desert, 3=Forest, 4=Snowy, 5=Swamp, 6=Jungle。纯函数于 seed（委托 biomeAt；
    //   PLAN §2-K 确定性，同 seed 同群系图）。分层（PLAN §2）：World 低层只读查询，不依赖 Entities / Renderer。
    //   消费点：EntityManager::pickPassiveMobType 据本值加权选被动生物类型（t374 群系化刷怪）。
    Q_INVOKABLE int biomeIdAt(int x, int z) const { return int(biomeAt(x, z)); }

    // t385 天气系统（机制等价 MC 1.0 天气：clear/rain/snow/thunder 随机转换；天空变暗；按群系）。
    //   全局单一天气态（weatherState）+ tickWeather 随机时长转换（QRandomGenerator 运行期模拟；天气是动态模拟
    //   非地形 worldgen 确定性，同 EntityManager 火 random extinguish 用 RNG）。局部降水类型据群系解析
    //   （weatherStateAt）：沙漠→晴、山地(Hills 冷高海拔)→雪、草原/森林→随全局态。雷态（Thunder）= 降水 + 风暴
    //   （天空更暗），雷电闪光 / 引燃留 t386。分层（PLAN §2）：本组属 World 层，只读 m_weather + biomeAt，
    //   不依赖 Renderer/Physics/Game。呈现层（QML 天空变暗 + 粒子）与 Entities 层（mob 灭火 / 日光燃烧门控）
    //   只读消费。
    int weatherState() const { return int(m_weather); }
    float weatherDarkness() const;
    // 局部降水类型（群系解析）：返回 Weather 枚举 int（0=Clear / 1=Rain / 2=Snow / 3=Thunder）。
    //   Clear→Clear；沙漠→Clear（永不降水）；山地(Hills)→Snow（冷）；草原/森林→随全局态（雨/雪/雷）。
    //   QML 据此选粒子类型；EntityManager / PlayerController 据此判灭火 / 日光燃烧门控。OOB 安全（biomeAt 纯函数）。
    Q_INVOKABLE int weatherStateAt(int x, int z) const;
    // 该位置是否正降水（= weatherStateAt != Clear）。mob 灭火 / 作物浇水 / 日光燃烧门控用。OOB 安全。
    Q_INVOKABLE bool isPrecipitatingAt(int x, int z) const;

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

    // t406 耕地湿润等级（spec「被附近水湿润、4 级湿润、越湿作物长得越快」）：给定耕地格 (x,y,z)，扫水源
    //   切比雪夫半径 4 水平 + 同/下一层（y / y-1，机制等价 MC farmland hydration 同高或低 1 层水均滋润），
    //   取最近水源切比雪夫距离 → 映射等级：dist 1→3、2→2、3→1、≥4/无水→0（共 4 级 0..3，越近水越湿）。
    //   唯一水源邻近判定权威：耕地时（playercontroller 锄头分支）写一次 + tickFarmlandHydration 周期复算。
    //   只读 m_chunks.blockAt（向下依赖）；世界空 → 0（干态兜底）。非 Q_INVOKABLE（C++ 调）。
    int farmlandHydrationLevel(int x, int y, int z) const;

    // t474 附魔台书架加成计数（机制等价 MC 1.0 enchanting table bookshelf power：附魔台周围 2 格切比雪夫
    //   距离内的书架数提升可选附魔等级上限）。给定附魔台方块格 (x,y,z)，扫 (x±2, y±2, z±2) 立方体范围内
    //   的 Bookshelf 数（共 5×5×5 = 125 格，去除中心附魔台自身），上限钳到 15（spec「count bookshelves
    //   within 2 blocks (<=15)」）。机制对齐 MC「中间需空气格」本工程简化为纯计数（不查阻隔）。
    //   呈现层 EnchantingTableUI 据本值算 maxEnchantLevel（书架数 / 2 + 1，钳 [1,3]，机制等价 MC 每两级
    //   书架解锁更高档）。分层（PLAN §2）：World 层只读 m_chunks.blockAt + BlockRegistry::isBookshelf，
    //   不依赖 Renderer/Game/UI。OOB blockAt 返 Air 安全（不计入）。
    Q_INVOKABLE int countBookshelvesAround(int x, int y, int z) const;

    // t117/t220 FallingBlock 着地专用：经 m_chunks.setBlock 直写（跨 chunk 路由 + 标脏 + 边界邻接，同 setBlock
    //   的写入路径）+ emit worldChanged（驱动 mesh 重建），但**不**发 blockPlaced（与玩家放置语义分离）。
    //   沿用 worldgen 经 m_chunks.setBlock 直写不触发 blockPlaced 的既有约定——避免 onBlockPlaced 的 survival
    //   takeStack 误触、多余粒子 / 音效把链式塌落刷成噪音。仅在目标格当前为**空气或水**时写入（t220：着地格
    //   由 FallingBlock 列扫保证为 air/水 —— 沙落水穿透后填堵水格；防御：其余已占用方块不覆盖）。越界 /
    //   非空非水 → false。非 Q_INVOKABLE（仅 EntityManager C++ 调）。
    bool setBlockFromEntity(int x, int y, int z, quint8 id);
    // t490fix 点火专用静默清方块（绕过 setBlockFromEntity 的 occ 守卫——TNT 是实体方块，occ 守卫会拒）。
    //   背景：playercontroller 右键机关 / 右键 TNT 本体 / 压力板四邻点燃 TNT 时，原写
    //   setBlockFromEntity(...,Air) 想「清掉原 TNT 方块再 spawnPrimedTnt」。但 setBlockFromEntity 有 occ 守卫
    //   （仅 air/水可被实体着地覆盖；沙落用），TNT 是实体方块 → occ 命中守卫 → 静默 return false 不写 →
    //   原 TNT 方块没清 + spawnPrimedTnt 在同格生成实体 = 1.5 格叠加；爆炸时 detonateTntSphere 球心格还是
    //   原 TNT 方块 → 连锁递归 + 坑越炸越大。
    //   语义：跨 chunk 直写 Air（无条件覆盖，**无 occ 守卫**）+ 复用 setBlockFromEntity 的全部写后钩子
    //   （noteGrowthWrite / noteFluidWrite / recomputeLightAround / pokeFluidDirty）+ emit worldChanged +
    //   clearAllDirty，**不发** broken/placed（免粒子音 spam，点火是系统事件非玩家破块）。occ 仍读出作
    //   oldId 传给 note / 光重算（保持生长 / 流体索引正确）。越界 → false。
    //   ⚠️ **仅供点火路径用**（playercontroller 3 处点燃 TNT 的清原方块）。不要用于破坏 / 沙着地 / 玩家
    //   放置——那些路径需要 occ 守卫或破块事件。非 Q_INVOKABLE（仅 Game/Physics C++ 调）。
    bool clearBlockSilent(int x, int y, int z);
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
    // t343 岩浆流 tick（spec「岩浆慢流（比水慢）」；机制等价 MC 1.0 主世界岩浆——比水慢 ~30 倍、扩散距离更短、
    //   **无源再生**）。由呈现层 Main.qml 经 WorldClock.ticked 桥接调用（每 100ms 一 tick；本方法内部节流到
    //   ~每 kLavaFlowTickInterval×0.1s 把波前推进 1 格 = ~3s/格，对比水 0.3s/格）。算法同 tickWaterFlow 的增量波前
    //   （快照 → 蒸发 → 扩散 → 应用），但：(a) 节流更慢（kLavaFlowTickInterval=30 vs 水 3）；(b) 扩散距离更短
    //   （kMaxLavaFlowLevel=3 vs 水 7）；(c) **无源再生 pass**（MC 1.0 主世界岩浆不形成无限源——两源相夹不升源，
    //   岩浆源被舀即永久消失）。下落（下方 air）→ 流岩浆 state=1；grounded（下方实体）→ 水平蔓延 state+1（≤3）；
    //   流岩浆失支撑 → 逐环凝固退场。稳态（worldgen 全源岩浆湖）每 tick 无变化 → setWaterSilent 全 false → 零重建。
    //   写入复用 setWaterSilent（**通用静默 state 写入口**——名字历史遗留 water-first，实现支持任意 id+state；岩浆
    //   蔓延 / 凝固是系统模拟非玩家动作，静默写避免发 broken/placed 噪音，机制等价 MC 岩浆流动无破/放反馈）。
    //   **末尾 ignite pass**（spec「木质方块邻岩浆概率着火焚毁」）：遍历本 tick 的岩浆格，扫水平 + 上下 6 邻，
    //   木类方块（Log/Planks/CraftingTable/Leaves/WoodSlab/Stairs/Fence/Plate/Door/Trapdoor/Chest）按确定性散布概率
    //   hashVoxel + 窗口序号 点燃焚毁（setBlock Air，发 blockBroken 触发破块粒子 / 音）。t344 完整着火系统（玩家扣血 /
    //   屏覆盖 / 熟肉掉落）留后续；本任务仅「邻岩浆木类概率焚毁」。
    Q_INVOKABLE void tickLavaFlow();
    // t236 小麦作物生长 tick（spec「WorldClock tick 推进成长 随机/timed」）：由呈现层 Main.qml 经
    //   WorldClock.ticked 桥接调用（每 100ms 一 tick；本方法内部节流到 ~每 kCropTickInterval×0.1s 做一次成长判定）。
    //   机制等价 MC 1.0 小麦生长：作物在耕地方块上、头顶光照足（skyLight ≥ kCropMinLight）时按**确定性散布概率**
    //   逐阶段升 state（0→1→…→7 成熟；7 = WheatCropStageMax 即成熟，t237 收割判 state==max 掉小麦）。
    //   「确定性散布」= 每个成长判定窗口内，每株作物据 hashVoxel(seed, x,y,z) + 当前窗口序号算一个伪随机值，
    //   落在概率窗口内才升阶段 → 不同作物错峰生长（非全部同步、贴近 MC random-tick 错落感），同时**纯函数于
    //   seed + 窗口序号**（无 Math.random / 时间源 → 同 seed 同窗口序号同结果，便于复现）。
    //   光照不足 / 下方非耕地 / 已成熟 → 不长（条件不满足即跳过，无副作用、不触发 worldChanged）。
    //   写入走 setWaterSilent（**通用的「静默 state 写」入口** —— 名字历史遗留 water-first，实现支持任意 id+state；
    //   作物升阶段是系统模拟非玩家动作 → 静默写避免发 blockBroken/blockPlaced 的粒子/音/拾取噪音，机制等价 MC
    //   「作物生长无破/放反馈」）。稳态（全成熟 / 无作物 / 全暗）每窗口无变化 → setWaterSilent 全 false → 不发
    //   worldChanged（无重建、无开销）。spectator/创造/生存均长（生长是世界模拟，与玩家模式无关）。
    //   分层（PLAN §2）：本方法属 World 层，只读 m_chunks + lightField + 发 worldChanged。不依赖 Renderer/Physics/Game。
    Q_INVOKABLE void tickCropGrowth();
    // t406 甘蔗生长 tick（spec「甘蔗 max5、仅邻水处长高、5 罕见」）：由呈现层 Main.qml 经 WorldClock.ticked 桥接调用
    //   （每 100ms 一 tick；节流到 ~每 kSugarcaneTickInterval×0.1s 一窗）。机制等价 MC 1.0 sugar cane 生长
    //   （random-tick 散布概率升柱）：扫甘蔗柱顶（上方为空气的甘蔗格），据柱基是否**沙地支撑**（t446：柱基支撑格须
    //   Sand，与 worldgen placeSugarcane 沙顶-only 一致 → 排除玩家误放 / 旧世界残留的草地 / 泥土 / 水中甘蔗柱，关闭
    //   「草上长高」残留路径）+ 柱基是否邻水（4 水平邻于基 / 基下一层）+ 柱高 < kSugarcaneMaxHeight(5) + 确定性散布概率
    //   → 命中即在柱顶上方一格长一格（setWaterSilent 静默写，无破/放反馈，机制等价 MC「甘蔗生长无反馈」）。柱基不
    //   邻水 / 非沙基 → 永不长；柱高已达 5 → 停长。t418 拔高潜力门：柱高 ≥3 时据列位一次性哈希判 kSugarcaneTallPct
    //   潜力 —— 多数柱止于 1..3、仅少数潜力柱可长到 4..5（满足 spec「1..3 common、5 rare」；修旧「全柱最终长到 5」
    //   稳态）。稳态（无甘蔗 / 全满高 / 全不邻水 / 全非沙基 / 全无潜力）每窗无变化 → 不发 worldChanged。散布确定性
    //   哈希（seed + 位置 + 窗口序号，PLAN §2-K，同 tickCropGrowth/tickSaplingGrowth）。分层（PLAN §2）：World 层，
    //   只读 / 写 m_chunks + 发 worldChanged。不依赖 Renderer/Physics/Game。
    Q_INVOKABLE void tickSugarcaneGrowth();
    // t406 耕地湿润复算 tick（spec「被附近水湿润、4 级」的动态实现）：由呈现层 Main.qml 经 WorldClock.ticked 桥接
    //   （每 100ms 一 tick；节流到 ~每 kFarmlandHydrTickInterval×0.1s 一窗）。扫全图 Farmland 格，逐格用
    //   farmlandHydrationLevel 复算湿润等级；与存档 state 低 2 位不等则 setWaterSilent 静默写新等级（驱动 mesher
    //   顶点色暗化重建 → 肉眼见近水耕地变深、远水耕地渐干）。机制等价 MC 1.0 farmland 随机 tick 补/失水（玩家后放
    //   水 / 挖水 → 耕地湿润度随之变）。稳态（湿润度全不变）每窗零写入、零 worldChanged。分层（PLAN §2）：World 层。
    Q_INVOKABLE void tickFarmlandHydration();
    // t305 树苗生长 tick（spec「树苗种植→长大成完整树（时间推进）」）：由呈现层 Main.qml 经 WorldClock.ticked
    //   桥接调用（每 100ms 一 tick；本方法内部节流到 ~每 kSaplingTickInterval×0.1s 做一次成长判定）。
    //   机制等价 MC 1.0 树苗生长（random-tick 式散布概率）：树苗在草地 / 泥土支撑 + 头顶光照足 + 主干列空气
    //   畅通时，按确定性散布概率（hashVoxel(seed,x,y,z) + 窗口序号）逐步判定 → 命中即清除树苗 + 在原位生成
    //   一棵完整橡树（复用 worldgen placeTreeAt 主干 + 树叶球冠）。光照不足 / 下方非草地泥土 / 主干列阻塞 →
    //   不长（条件不满足即跳过，无副作用、不触发 worldChanged）。生成树走 setVoxelIfAir（仅写空气格，不覆盖
    //   玩家编辑 / 已有方块；同 worldgen 树放置语义）。稳态（无树苗 / 全不满足）每窗口无变化 → 不发 worldChanged。
    //   spectator/创造/生存均长（生长是世界模拟，与玩家模式无关）。
    //   分层（PLAN §2）：本方法属 World 层，只读 m_chunks + lightField + 发 worldChanged。不依赖 Renderer/Physics/Game。
    Q_INVOKABLE void tickSaplingGrowth();
    // t514 甜浆果丛生长 tick（spec「浆果丛会长大（生长阶段）」）：由呈现层 Main.qml 经 WorldClock.ticked 桥接
    //   调用（每 100ms 一 tick；本方法内部节流到 ~每 kBerryBushTickInterval×0.1s 一窗）。机制等价 MC 1.0 sweet
    //   berry bush random-tick 生长：阶段 0..SweetBerryBushStageMax 的丛在「下方透光土壤支撑 + 头顶光照足 + 未成熟」
    //   时按确定性散布概率升一阶段。玩家种植落地 state=0（playercontroller t514 种植分支）；worldgen
    //   placeSweetBerryBushes 散布 state 1..2；玩家采摘把成熟丛降回 state 0（playercontroller t467 采摘分支）→
    //   本 tick 把这些 state<max 的丛再逐步推回成熟（形成「采→回 0→生长→成熟→可再采」循环，同小麦 / 树苗生长）。
    //   土壤支撑：下方为 Grass / Dirt / Farmland（机制等价 MC 浆果丛生于草地 / 泥土 / 耕地；与 playercontroller 种植
    //   分支 Grass/Dirt 一致 + 耕地兼容）。worldgen 丛立于 SnowLayer 上 —— SnowLayer 不算土壤（雪非泥土），故
    //   worldgen 丛不靠本 tick 升阶段（保持散布时阶段 1..2 稳态，采后回 0 即停长为枯丛，同 MC 雪原丛采后不长）。
    //   光照：头顶天光 >= kBerryBushMinLight（/15；机制等价 MC 浆果丛 light level 9+，夜间 / 洞穴不长）。
    //   静默写：升阶段走 setWaterSilent（系统模拟非玩家动作 → 无 broken/placed 反馈，机制等价 MC「生长无破/放反馈」）。
    //   perf（PLAN §2 / lessons perf-fluid-scan）：遍历生长方格索引 m_growthCells（O(丛格数)）而非全图 W×D×H；
    //   SweetBerryBush 经 isGrowthBlock 入索引、noteGrowthWrite 增量维护。稳态（无丛 / 全成熟 / 全无土壤 / 全暗）
    //   每窗零写入、零 worldChanged。spectator/创造/生存均长（生长是世界模拟，与玩家模式无关）。
    //   分层（PLAN §2）：本方法属 World 层，只读 m_chunks + lightField + 发 worldChanged。不依赖 Renderer/Physics/Game。
    Q_INVOKABLE void tickSweetBerryBushGrowth();
    // t468 结冰 tick（spec「寒冷群系(雪原 Snowy)暴露天空的水源(Water state==0)→冰；MC 规则：暴露天空 +
    //   寒冷生物群系→水变冰」）：由呈现层 Main.qml 经 WorldClock.ticked 桥接调用（每 100ms 一 tick；本方法内部
    //   节流到 ~每 kFreezeTickInterval×0.1s = 5s 一窗）。机制等价 MC 1.0 random-tick 结冰：扫 Snowy 群系列，自顶
    //   向下扫到进入阴影区（skyLight<15）停，对暴露天空（skyLightAt>=15）的水源（Water state==0）按散布概率
    //   kFreezePct 冻结为 Ice（setWaterSilent：直写 Ice + worldChanged 重建，非玩家破/放无反馈）。worldgen
    //   freezeSurfaceWater 已在生成期冻结雪原表层水；本 tick 处理玩家后放 / 冰破回水 / 动态暴露的延迟冻结。
    //   散布确定性哈希（seed + 位置 + 窗口序号 → 错峰冻结非瞬时全冻，PLAN §2-K 精神，同 tickCropGrowth）。
    //   稳态（无 Snowy 暴露水源 / 本窗散布落空）零写入、零 worldChanged。spectator/创造/生存均推进（结冰是世界
    //   模拟）。分层（PLAN §2）：本方法属 World 层，只读 m_chunks + biomeAt + skyLightAt + 发 worldChanged。
    Q_INVOKABLE void tickIceFreeze();
    // t495 普通冰融化 tick（spec「普通冰在高温/高亮环境（火把/熔炉/火）有概率融化成水」；机制等价 MC 1.0
    //   ice 受高方块光照射融化 —— 仅普通冰 Ice(45)，浮冰 PackIce / 蓝冰 BlueIce 永不融化）。由呈现层 Main.qml
    //   经 WorldClock.ticked 桥接调用（每 100ms 一 tick；本方法内部节流到 ~每 kIceMeltTickInterval×0.1s 一窗）。
    //   每窗遍历冰格索引（m_iceCells，O(冰格数) 替代全图扫描，同 tickIceFreeze / tickCropGrowth 的位置索引模式）：
    //   对每个 Ice 格查其「高亮邻居」—— 任一 6 正交邻格是发光方块（BlockRegistry::lightEmission(state)>0：火把/
    //   燃烧熔炉/岩浆/火/末地传送门）或本格格方块光 ≥ kIceMeltBlockLight（~12，机制等价 MC 冰需 light level ≥12
    //   从 ≥2 邻面照射才融；本工程简化为「单邻发光源或自身高方块光」即触发候选）→ 命中按散布概率 kIceMeltPct
    //   （hashVoxel(seed, x,y,z) + 窗口序号，PLAN §2-K 确定性散布，同 tickCropGrowth / tickIceFreeze 错峰）融为水
    //   （setWaterSilent 静默写 Water state=0：系统模拟非玩家破/放 → 无 broken/placed 噪音，机制等价 MC 冰融化无
    //   反馈；静默写同时把冰格移出 m_iceCells、把新水格入 m_waterCells，下游 tickWaterFlow / tickIceFreeze 自然续
    //   稳态）。散布 ~平均数秒/格（kIceMeltPct 窗概率 → 几何分布），贴近 MC 冰在火把旁数秒融的观感。
    //   稳态（无冰 / 无高亮邻 / 本窗散布落空）零写入、零 worldChanged。spectator/创造/生存均推进（融化是世界模拟）。
    //   分层（PLAN §2）：本方法属 World 层，只读 m_chunks + m_lightField(blockLightAt) + 发 worldChanged。
    //   perf（PLAN §2 / lessons perf-fluid-scan）：遍历 m_iceCells 位置索引（O(冰格数)）而非全图 W×D×H；冰在世界
    //   中数量有限（雪原表层冻结），扫描量 << 3.28M 全图。索引经 noteIceWrite 增量维护（同 m_waterCells 模式）。
    Q_INVOKABLE void tickIceMelt();
    // t325 树叶渐进消退 tick（spec「挖光一棵树所有原木→树叶消失」的渐进化修：旧 t305 瞬时清半树冠叶 →
    //   改为逐叶按概率渐退，散布 ~30-90s；t379 在 t325 基础上进一步放慢）。由呈现层 Main.qml 经 WorldClock.ticked 桥接调用（每 100ms 一 tick；
    //   本方法内部节流到 ~每 kLeafDecayTickInterval×0.1s 做一次判定窗口）。机制等价 MC 1.0 叶衰 random-tick：
    //   叶子一旦失撑（无原木支撑）即进入衰减，**每窗**按散布概率 kLeafDecayPct 独立判定是否本窗消失 → 几何分布
    //   散布寿命（非瞬时全消、不同叶错峰渐退）。散布确定性哈希（seed + 位置 + 窗口序号，PLAN §2-K 精神，同
    //   tickCropGrowth/tickSaplingGrowth）。命中叶批量静默清（m_chunks.setBlock 直写 Air + 标脏，不发 broken/
    //   placed → 无破叶粒子/音，自然衰减无反馈）+ 末尾对受影响区一次 refloodBox 重算光场 + 一次 worldChanged
    //   （避免逐叶 setBlock 的 N 次光场重算 + N 次重建请求，机制同旧 decayLeavesAround 批量清叶）。队列空（无
    //   失撑叶）或本窗无命中 → 零写入、零 worldChanged（稳态无开销）。非 Q_INVOKABLE？—— 是 Q_INVOKABLE：
    //   同 tickCropGrowth/tickSaplingGrowth 由 QML WorldClock 桥接调用。
    Q_INVOKABLE void tickLeafDecay();
    // t385 天气 tick（spec「天气状态机（晴/雨/雪/雷）+ 随机转换」）：由呈现层 Main.qml 经 WorldClock.ticked
    //   桥接调用（每 100ms 一 tick；本方法按 dt 推进天气态剩余计时，归零即随机转换）。机制等价 MC 1.0 天气：
    //   晴 ↔ 降水（雨/雪/雷）随机时长来回转换；晴→随机选雨(65%)/雪(25%)/雷(10%)，降水→晴。转换用 QRandomGenerator
    //   （运行期模拟，非 worldgen 确定性 —— 天气是动态模拟非地形生成）。态翻转 emit weatherChanged（驱动 QML
    //   天空变暗 + 粒子切换）。计时未到 → 仅减计时零开销（无写入 / 无 worldChanged）。spectator/创造/生存均推进
    //   （天气是世界模拟，与玩家模式无关）。分层（PLAN §2）：本方法属 World 层，只读 / 写 m_weather + 发 weatherChanged。
    Q_INVOKABLE void tickWeather(qreal dt);
    // t305 树叶失撑检测（t325 改造：不再瞬时清叶，改为入渐进衰减队列）。玩家破原木后扫破块点周围树叶，
    //   判定其是否仍「在原木支撑范围内」（机制等价 MC 1.0 叶子距原木 >4 格即衰减）。失撑叶（4 格切比雪夫距离
    //   内无原木）**入渐进衰减队列 m_decayingLeaves**（按坐标去重）→ 留待 tickLeafDecay 按概率逐叶渐退（散布
    //   ~10-30s，非瞬时全消）。**持久叶**（玩家放置、state 带 PersistentLeafBit）跳过 → 创造建筑用的悬空叶不衰。
    //   本方法只入队、不立即清叶、不发 worldChanged（清叶 + 光场重算 + worldChanged 收口到 tickLeafDecay）。
    //   非 Q_INVOKABLE（PlayerController C++ 调，破原木后触发）。
    //   分层（PLAN §2）：本方法属 World 层，只读 m_chunks + 写 m_decayingLeaves。
    void decayLeavesAround(int x, int y, int z);
    // t320 爆炸批量破坏（修 Stalker 爆炸后 FPS 崩塌 / 内存爆涨；机制等价 MC 苦力怕球形爆破的批量化）。
    //   根因：detonateStalker 旧路径对球内**每块**调 setWaterSilent → 每块 1× emit worldChanged + 1×
    //   recomputeLightAround（±15 盒 ~50k 体素）+ 触发 QML 各 worldChanged Connections 一次。半径 3 球
    //   破坏 ~30-100 块 → 一次爆炸 = 数十次 mesh 重建请求 + 数十次光场重 flood + 数十次 QML 重算 = 一帧
    //   垼掉数百 ms（FPS 8-9）。
    //   修法：本入口收口整个球的破坏 —— 逐块 m_chunks.setBlock 直写 Air + 标脏（**不** emit worldChanged、
    //   **不** clearAllDirty、**不** per-block recomputeLightAround），末尾对受影响球外接盒做**一次**
    //   refloodBox（doSky=true，遮光块破坏影响天光列）+ **一次** emit worldChanged + **一次** clearAllDirty
    //   （机制同 decayLeavesAround 批量清叶：N 写 1 emit，避重建风暴）。
    //   跳过 Air / Bedrock / Water（不破坏水体 / 基岩 / 空气；机制等价 MC 爆炸不毁水体）。caller 已自判
    //   "水中不破坏地形"（detonateStalker 的 originInWater 门控；水中爆炸仍伤玩家但不毁地形）。
    //   返回被破坏块（坐标 + 原 id）列表，供 caller 据原 id 派生掉落物（BlockRegistry::dropId）。无破坏 → 空。
    //   分层（PLAN §2）：本方法属 World 层，只读 / 写 m_chunks + lightField + 发 worldChanged；不依赖
    //   Renderer / Physics / Game。EntityManager（Entities 层）C++ 调（同 setWaterSilent / setBlockFromEntity）。
    struct DestroyedVoxel { int x, y, z; quint8 oldId; };
    std::vector<DestroyedVoxel> destroySphereSilent(int cx, int cy, int cz, float radius);

    // t445 仙人掌整柱坍落为掉落物（机制等价 MC 1.0 仙人掌失撑 / 邻接方块即整柱破坏掉落）。自 (x,y,z) 起向上
    //   逐格：凡 Cactus → 静默写 Air（m_chunks.setBlock 直写 + 标脏，**不**经 World::setBlock → 不递归触发本逻辑
    //   / 不重复发 blockBroken 链）+ emit blockBroken（破块粒子 / 音）+ emit blockDroppedAsItem（呈掉落物实体）+
    //   recomputeLightAround（遮光柱消失重 flood）。末尾若有破坏则 1 次 emit worldChanged + clearAllDirty（N 写
    //   1 emit，同 destroySphereSilent 批量收口）。供 setBlock 破块（② 失撑）/ 放块（④ 邻接）路径调。非 Q_INVOKABLE
    //   （内部 helper）。分层（PLAN §2）：World 层，只读 / 写 m_chunks + lightField + 发信号；不依赖 Game / Entities。
    void dropCactusColumn(int x, int y, int z);
    // t445 setBlock 编辑后仙人掌完整性复检（② 失撑 + ④ 邻接方块）。（x,y,z,oldId,id）= 本格刚发生的编辑。
    //   ②：本格被破为 Air 且被破块非 Cactus → 若正上方是 Cactus，则该 Cactus 失撑 → dropCactusColumn（递归整柱）。
    //      （被破块本身是 Cactus 时不在此处理 —— 玩家直破仙人掌的整柱坍落由 PlayerController 级联 spawnItem 负责，
    //      避免双重掉落。）④：本格新放了非 Air 方块 → 水平 4 邻任一为 Cactus 即「邻接方块」→ 该 Cactus 整柱掉落。
    //   静默 dropCactusColumn 不经 World::setBlock → 不重入本检查。供 4/5 参数 setBlock 末尾各调一次（编辑路径收口）。
    void checkCactusOnEdit(int x, int y, int z, quint8 oldId, quint8 id);

    // t504 setBlock 编辑后枯死灌木失撑复检（机制等价 MC 1.0 枯灌木失去下方支撑即坍落；同甘蔗 / 仙人掌支撑校验族）。
    //   （x,y,z,oldId,id）= 本格刚发生的编辑。本格被破为 Air（破下方支撑方块）→ 若正上方是 DeadBush → 该枯灌木失撑
    //   → 静默清 Air（m_chunks.setBlock 直写 + 标脏，不经 World::setBlock → 不递归触发）+ emit blockBroken（破块粒子 / 音，
    //   id 用 DeadBush：坍落的是枯灌木方块本身）+ emit blockDroppedAsItem（掉落物 = **木棒** 材料段 0x200；机制等价 MC
    //   dead bush 掉 0-2 木棒、不掉自身 —— 区别于花 / 蘑菇失撑掉自身）+ recomputeLightAround（遮光消失重 flood）+
    //   1 次 worldChanged + clearAllDirty。DeadBush 恒单格（无柱状生长，与 Cactus 不同），仅清正上方 1 格。dropId=0 故
    //   玩家直破枯灌木无产物（机制等价 MC 空手破 dead bush 无产物 / 剪刀才掉，本工程无剪刀）；仅失撑（破下方支撑）才掉木棒。
    //   木棒 id 用字面量 0x200（= RecipeRegistry::StickId，材料段基址 0x200）—— Core/World 不依赖 Game（PLAN §2 分层），
    //   不能 include recipe.h，与 blockregistry.cpp 矿石 dropId 用字面量 0x201/0x202 同模式。供 4/5 参数 setBlock 末尾各调
    //   一次（编辑路径收口）。非 Q_INVOKABLE（内部 helper）。
    void checkDeadBushOnEdit(int x, int y, int z, quint8 oldId, quint8 id);

    // t507 setBlock 编辑后花 / 蘑菇失撑复检（机制等价 MC 1.0 花 / 蘑菇失去下方支撑即掉自身；同甘蔗 / 仙人掌 /
    //   枯灌木支撑校验族）。（x,y,z,oldId,id）= 本格刚发生的编辑。本格被破为 Air（破下方支撑方块）→ 若正上方是
    //   Flower（4 色任一）/ Mushroom（红）/ BrownMushroom（白）→ 该植物失撑 → 静默清 Air（m_chunks.setBlock 直写
    //   + 标脏，不经 World::setBlock → 不递归触发）+ emit blockBroken（破块粒子 / 音）+ emit blockDroppedAsItem
    //   （呈掉落物实体，Main.qml 转 spawnItem）+ recomputeLightAround（遮光消失重 flood）+ 1 次 worldChanged +
    //   clearAllDirty。花 / 蘑菇恒单格（无柱状生长，与 Cactus 不同），仅清正上方 1 格。dropId=自身故玩家直破 /
    //   失撑都掉同色花 / 同种蘑菇方块（机制等价 MC 破花掉花、破蘑菇掉蘑菇；区别于枯灌木 dropId=0 无产物）。
    //   供 4/5 参数 setBlock 末尾各调一次（编辑路径收口）。非 Q_INVOKABLE（内部 helper）。
    void checkFlowerMushroomOnEdit(int x, int y, int z, quint8 oldId, quint8 id);

    // t494 压力板失撑掉落复检（机制等价 MC 1.0 压力板失去下方支撑即掉自身；同甘蔗 / 仙人掌 / 枯灌木支撑校验族）。
    //   压力板（Wood / Cobble）是贴地薄板（ShapePlate），下方须有完整支撑方块。本格被破为 Air（破下方支撑，含
    //   clearBlockSilent 点火路径——TNT 被引燃变实体、下方变 Air → 板上压力板失撑）→ 若正上方是压力板 → 失撑
    //   → 静默清 Air + emit blockBroken（破块粒子 / 音）+ emit blockDroppedAsItem（呈掉落物实体，dropId=自身 →
    //   掉木板 / 圆石压力板）+ recomputeLightAround + 1 次 worldChanged + clearAllDirty。压力板恒单格，仅清正上方 1 格。
    //   供 4/5 参数 setBlock + clearBlockSilent 末尾各调一次（编辑路径收口）。非 Q_INVOKABLE（内部 helper）。
    void checkPressurePlateOnEdit(int x, int y, int z, quint8 oldId, quint8 id);

    // t524 甘蔗整柱坍落为掉落物（机制等价 MC 1.0 甘蔗失去下方支撑即整柱破坏掉落；同仙人掌支撑校验族）。
    //   自 (x,y,z) 起向上逐格：凡 Sugarcane → 静默写 Air（m_chunks.setBlock 直写 + 标脏，**不**经 World::setBlock
    //   → 不递归触发本逻辑 / 不重复发 blockBroken 链）+ emit blockBroken（破块粒子 / 音）+ emit blockDroppedAsItem
    //   （呈掉落物实体，dropId=自身）+ recomputeLightAround（遮光柱消失重 flood）。末尾若有破坏则 1 次 emit
    //   worldChanged + clearAllDirty（N 写 1 emit，同 dropCactusColumn 批量收口）。供 checkSugarcaneOnEdit 失撑路径调。
    //   非 Q_INVOKABLE（内部 helper）。分层（PLAN §2）：World 层，只读 / 写 m_chunks + lightField + 发信号；不依赖 Game。
    void dropSugarcaneColumn(int x, int y, int z);
    // t524 setBlock 编辑后甘蔗失撑复检。（x,y,z,oldId,id）= 本格刚发生的编辑。
    //   失撑：本格被破为 Air 且被破块非 Sugarcane → 若正上方是 Sugarcane，则该甘蔗失撑（下方支撑方块没了）→
    //   dropSugarcaneColumn 整柱坍落。（被破块本身是 Sugarcane 时跳过 —— 玩家直破甘蔗的整柱坍落由 PlayerController
    //   级联 spawnItem 负责，避免双重掉落；同仙人掌 checkCactusOnEdit 的 oldId 守卫模式。）
    //   静默 dropSugarcaneColumn 不经 World::setBlock → 不重入本检查。供 4/5 参数 setBlock 末尾各调一次（编辑路径收口）。
    void checkSugarcaneOnEdit(int x, int y, int z, quint8 oldId, quint8 id);

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
    void weatherChanged(); // t385 天气态翻转（晴↔雨/雪/雷；驱动 QML 天空变暗 + 粒子切换）
    // t386 一次闪电击中（雷雨天随机触发）：携击中世界坐标 (x,y,z)。呈现层据此显屏幕白闪 + 雷声（playThunder）；
    //   实体层（mob / 玩家）据此对击中点附近实体造成伤害。World 自身已对击中点的木类方块引燃焚毁（见 strikeLightning）。
    //   机制等价 MC 1.0 雷击（机制等价，§9 改名非 MC 专名）。分层（PLAN §2）：World 低层只发语义事件，绝不反向
    //   依赖 Entities / Renderer —— 损伤 mob / 玩家由各自上层 Connections 消费本信号（同 blockBroken 模式）。
    void lightningStruck(int x, int y, int z);
    // 编辑语义事件（id：broken 带被破的原方块 id；placed 带新放方块 id）。
    void blockBroken(int x, int y, int z, int id);
    void blockPlaced(int x, int y, int z, int id);
    // t445 世界侧产出的掉落物（仙人掌失撑 / 邻接方块即整柱坍落为掉落物）：携世界坐标 + 方块 id。
    //   呈现层（Main.qml）据本信号 spawnItem 生成掉落实体（同 player.spawnItem / fallingBlockDropped 模式：
    //   World 低层只发语义事件，不反向依赖 Game/Entities）。仅仙人掌走此路径（玩家破块走 player.spawnItem）。
    void blockDroppedAsItem(int x, int y, int z, int id);

private:
    void generate();          // 重建置换表 + ChunkManager + 填充地形（静默，不 emit）
    void buildPermutation();  // 由 seed 填 512 置换表（线性同余，可复现）
    double noise2(double x, double z) const;
    double fbm(double x, double z) const;
    // t278 3D Perlin 噪声（洞穴 carve 用）。复用 buildPermutation 的 m_perm[512] 表（与 noise2 同源）；
    //   grad3 三维梯度。纯函数于 seed + (x,y,z) → 同 seed 同 3D 噪声场（PLAN §2-K）。范围 ~[-1,1]。
    double noise3(double x, double y, double z) const;
    // t274/t306 群系枚举（plains/forest/hills/desert 四分；机制等价 MC 1.0 大尺度群系，名称为通用描述词，§9 合规）。
    //   worldgen 内部用：heightAt 据群系选振幅、placeTrees/placeTallGrass 据群系选密度、isDesert 收口到
    //   biomeAt==Desert。纯函数于 seed（biomeAt 经 fBm）→ 同 seed 同群系图（PLAN §2-K 确定性）。私有嵌套枚举
    //   （worldgen 细节，不外泄到 QML；如需 F3 调试可后续暴露 Q_INVOKABLE 查询）。
    //   t306 在 t274 三分基础上把原 plains 中段 carve 出 forest：forest 多树（密闭林）、plains 少树多草（开阔草原），
    //   机制等价 MC 1.0 森林 / 平原群系分化（spec「森林（现多树）+ 草原（少树多草）」）。
    //   t481/t486 前置：新增 Jungle（6，编码 6）—— 第五条独立低频 fBm 从 forest 带 + plains 候选带里 carve 出
    //   丛林（温热湿润林地，高树浓叶，见 biomeAt / heightAt / placeJungleTrees）。Hills/Desert/Snowy/Swamp 判定
    //   均先于丛林早退 → 丛林绝不吞掉既有群系（spec「勿让既有 Desert/Swamp/Snowy 消失」）。
    enum class Biome { Plains, Hills, Desert, Forest, Snowy, Swamp, Jungle };
    // t385 天气状态机枚举（机制等价 MC 1.0 天气四态）。私有嵌套（天气细节，不外泄类型到 QML；
    //   Q_INVOKABLE weatherState / weatherStateAt 返 int 编码）。Thunder = 降水 + 风暴（雷电闪光 / 引燃留 t386）。
    enum class Weather : int { Clear = 0, Rain = 1, Snow = 2, Thunder = 3 };
    // t274/t306 群系判定（PLAN §2-K 确定性）：主群系 fBm（与高度噪声 0.09 / 旧沙漠噪声 0.018 均不同频率 0.012 +
    //   seed 偏移 +3571）→ 群系图与高度图解耦、与旧沙漠分布独立。低频 → 大区块连续（plains/forest/hills/desert
    //   成片，非逐格斑点，机制等价 MC 1.0 群系大尺度分布）。阈值三分主图：hills（少数，起伏）/ desert（少数，沙）/
    //   其余 plains 候选带。t306 在 plains 候选带内用**第二条独立低频 fBm**（频率 0.020 + seed 偏移 +977，与主图
    //   解耦）把 forest 从草原里 carve 出来 → 森林成片分布、与草原无缝衔接（二者同振幅 amp 2 → 边界零高差无缝）。
    //   纯函数于 seed → 同 seed 同群系分布（含 forest/plains 划分）。
    Biome biomeAt(int x, int z) const;
    // t117/t274 沙漠群系判定：收口到 biomeAt == Desert（单一权威；旧独立 fBm 实现已由 t274 biomeAt 统一）。
    //   供 generate（沙表层）/ placeTrees / placeTallGrass 跳过沙漠列。纯函数于 seed（经 biomeAt）。
    bool isDesert(int x, int z) const;
    // t338/t372 海域集中于一角（海 + 沙滩；spec「沙集中一角→沙滩+海」）。seed 派生选 4 角之一（seaCorner），
    //   四分之一圆盘（seaColumnHeight）重塑：角点最深海底 → 岸线干沙滩，再经过渡带平滑接邻接地形。
    //   seaColumnHeight 返回沙海盘 + 过渡带的重塑高度（>=0）或 -1（远内陆）；t372 加岸线 fBm 蜿蜒 + 高度噪声
    //   柔化（spec「沙滩太规整」）+ 外圈过渡带（spec「沙滩与森林高差突兀」→ beachTop smoothstep 到 heightAt）。
    //   isSeaSandColumn 仅沙海盘（内圆盘）为真 → generate 据此走沙表层；过渡带列走自然群系草地。两函数共用相同
    //   effectiveRadius 计算（沙→草切换恰好落在岸线 → 无缝）。纯函数于 seed + dims（PLAN §2-K）；出生列居中
    //   (80,80) 远离四角 → 不落海。heightAt 保持纯 fBm（不改），海域重塑仅在 generate/fillWater 显式应用 →
    //   placeTrees/placeTallGrass/scatterOres 经既有的「草顶 / 自然高度」守卫自然跳过海域（海列自然 surfaceY 处为
    //   air/水，非 Grass → 不种树/草），placeSurfaceLakes/placeUndergroundWaterPools 显式跳过。
    void seaCorner(int &cx, int &cz) const;
    int seaColumnHeight(int x, int z) const;
    bool isSeaSandColumn(int x, int z) const;

    // 确定性树木生成（PLAN §2-K）：在 generate() 末段于 grass 表层种橡树（原木主干+树叶球冠）。
    // 位置/形状纯由 seed 决定；禁用任何运行期随机源（QTime/时钟/全局 RNG）。
    void placeTrees();                                  // 遍历列、密度+间距筛选后散布
    // 单棵树：主干 trunkH 格 + 树冠。leafRand = 该列哈希的高位，驱动树冠四角叶的有无 → 每棵树冠轮廓
    // 各异（贴近 MC 橡树自然参差）。纯由 seed 派生（确定性，PLAN §2-K）。
    void placeTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand);
    // t395 单棵云杉（变种树）：主干 trunkH 格云杉原木（id SpruceLog）+ 顶部窄锥形树冠（普通树叶 id Leaves）。
    //   机制等价 MC 1.0 云杉（spruce）—— 比 oak 更高更窄、树冠呈收尖锥形（底层半径 2 渐收到顶尖半径 0）。
    //   worldgen placeTrees 在 Snowy 群系改种本变种（区别于橡树：深色云杉主干 + 高窄锥形树冠）。仅写空气格
    //   （setVoxelIfAir）→ 不覆盖主干 / 地形。纯由 seed 派生（leafRand 驱动锥层四角叶有无，确定性 PLAN §2-K）。
    void placeSpruceTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand);
    // t481/t486 前置 丛林树散布（PLAN §2-K 确定性）：遍历 Jungle 群系列，按 hashColumn(seed,x,z) 密度筛选 +
    //   间距栅格（3×3 邻域不得已有树干 → 主干间距 ≥2 列）散布高树。树干更高（5..7 格，spec「树干更高 ~5-7」）
    //   + 树冠更大更浓（placeJungleTreeAt 半径 3 大伞盖，spec「树冠更大更浓」）→ 丛林观感（高树浓叶）。
    //   仅 grass 表层（Jungle 地表为草，与 placeTrees 同守卫）种；纯函数于 seed + biomeAt → 同 seed 同分布。
    void placeJungleTrees();
    // t481/t486 前置 单棵丛林树：主干 trunkH 格原木 + 半径 3「大伞盖」树冠（比橡树半径 2 球冠更大更浓，
    //   底层两层满填、仅伞缘四角按 leafRand 有无 → 每棵轮廓各异）。主干先置、树冠后置且仅写空气格 → 不覆盖主干。
    void placeJungleTreeAt(int x, int surfaceY, int z, int trunkH, quint32 leafRand);
    // 确定性矿石散布（t84/t279，PLAN §2-K）：地形填充后遍历 stone 区段，按 hashVoxel(seed,x,y,z)
    //   决定是否替换为煤矿 / 铁矿 / **钻石矿**。**t279 高度分层**（煤浅 / 铁中 / 钻石深，机制等价 MC 1.0
    //   矿物随深度分层）：煤仅浅层（y≥8，靠近地表富集）、铁中层（y≤30）、钻石仅深层（y∈[5,16]，靠近基岩
    //   越富）。三矿判定用同一 hash 的不同位段（独立 → 可重叠区三矿共存、优先钻石 > 铁 > 煤排冲突）。
    //   仅替换 Stone；同 seed → 同矿脉分布；禁用任何运行期随机源。密度随深度上调（深层 stone 多、洞穴穿
    //   多 → 洞壁裸露矿更可见，spec「洞穴 carve 自然暴露」——carveCaves 在本 pass 之后挖走 stone/ore 暴露矿脉）。
    void scatterOres();
    // t119 底层基岩（PLAN §2-K 确定性）：地形填充后在 y 0..4 铺一层 Bedrock（不可破坏，hardness=-1.0）。
    // 厚度按 hashVoxel 坑洼（底实顶疏，机制等价 MC 1.0 基岩层）。仅覆盖最底几格；同 seed → 同分布。
    void placeBedrock();
    // t148 海平面填水（PLAN §2-K 确定性）：地形填充后在 waterLevel 以下的低洼列从 h+1 到 waterLevel
    //   填 Water（机制等价 MC 海洋 / 湖泊）。仅写空气格；同 seed → 同水域分布。waterLevel 见 .cpp 注释
    //   （t307：随地表抬高 24→58，保持「低于基线 6 格」使低洼 hills 仍见水 / 沙滩带）。
    void fillWater();
    // t235 草丛散布（PLAN §2-K 确定性）：地形 + 树 + 水定型后，遍历 grass 表层列（非沙漠 / 非沙滩水下 / 非水域），
    //   按 hashColumn(seed,x,z) 密度筛选在 grass 顶上方一格（surfaceY+1）置 TallGrass（仅写空气格 → 不覆盖
    //   树干 / 树叶 / 水）。同 seed → 同草丛分布；禁用任何运行期随机源。机制等价 MC 平原草丛点缀。
    void placeTallGrass();
    // t394 沙漠植被散布（PLAN §2-K 确定性）：遍历 desert 沙顶列，按 hashColumn(seed,x,z) 密度筛选在沙顶
    //   上方置仙人掌（1-3 格高柱，每格仅写空气格 → 不覆盖实块）或枯死的灌木（cross 广告牌，仅写空气格）。
    //   机制等价 MC 1.0 沙漠仙人掌 / 枯灌木点缀。纯函数于 seed + biomeAt（经 hashColumn）→ 同 seed 同分布；
    //   禁用任何运行期随机源。仅写空气格 → 不覆盖沙上已生成的方块（与 placeTrees/placeTallGrass 同守卫语义）。
    void placeDesertFlora();
    // t396 沼泽浅水池（PLAN §2-K 确定性）：遍历 Swamp 群系列，用低频 fbm（与其它噪声解耦）把约半数草地列
    //   改造成 1 格深浅水池（草顶 → Water 源，state=0）。机制等价 MC 1.0 沼泽「平地 + 浅水洼 + 草岛」地貌。
    //   Swamp 群系 heightAt amp=0（完美平坦，见 heightAt 注释）→ 全 Swamp 列等高 → 水源层同高、水平邻接为
    //   草岛（同高 Grass）→ 不溢流（稳态源层）。仅写 Swamp 非海列（海域独立）。走 m_chunks.setBlock 直写
    //   （worldgen 静默；光场随后 recomputeLightField 重算）。纯函数于 seed（biomeAt + fbm）。
    void placeSwampPools();
    // t396 沼泽植物散布（PLAN §2-K 确定性）：遍历 Swamp 群系列，在浅水格上方一格（水面 + 1）散布睡莲
    //   （LilyPad 横向浮叶，仅写空气格）+ 在草岛格上方一格低密度散布蘑菇（Mushroom cross 广告牌，仅写空气格）。
    //   机制等价 MC 1.0 沼泽睡莲浮水 + 阴暗草地小蘑菇。纯函数于 seed + biomeAt（经 hashColumn）→ 同 seed 同分布；
    //   禁用任何运行期随机源。仅写空气格 → 不覆盖水 / 草上已生成的方块（与 placeTrees/placeTallGrass 同守卫语义）。
    void placeSwampFlora();
    // t397 花散布（PLAN §2-K 确定性）：遍历各群系草地列（非沙漠 / 非雪原 / 非沙滩水下，与 placeTallGrass 同阈值），
    //   按 hashColumn(seed,x,z) 密度筛选在草顶上方一格（surfaceY+1）置 4 色花之一（cross 广告牌，仅写空气格）。
    //   机制等价 MC 1.0 各群系花点缀（平原多彩 / 森林少量 / 沼泽适量 / 山地稀疏）。各群系密度 + 色彩配比不同：
    //   plains 花最多且 4 色均布（开阔草原花海）、forest 适中偏黄 / 白（林下小花）、swamp 适中偏蓝（湿地野花）、
    //   hills 稀疏（裸岩 / 林少花）。色选独立哈希位段 (r>>16)%4 选色（与密度位段 r%100 解耦）。仅写空气格
    //   （setVoxelIfAir）→ 不覆盖草上已生成的方块（树 / 草丛）。纯函数于 seed + biomeAt → 同 seed 同分布；
    //   禁用任何运行期随机源（与 placeTallGrass / placeSwampFlora 同守卫语义）。
    void placeFlowers();
    // t397 甘蔗散布（PLAN §2-K 确定性）：在邻水**沙顶**（沙滩 / 海岸）上方确定性散布 1..3 格高甘蔗柱（Sugarcane
    //   cross，每格仅写空气格）。spec t446 收紧三条件：(1) 直接坐在 Sand 上、(2) 沙顶层 surfaceY 或其下一层
    //   surfaceY-1 的水平 4 邻有 Water（任意 state）、(3) 沙顶正上方为空气（不在水里 / 湖底生）。草地 / 泥土 /
    //   湖底 / 沼泽 / 水中一律排除。机制等价 MC 1.0 sugar cane 常见于水边沙岸。
    //   t446 根因（复现 seed 1337 全图 0 甘蔗）：沙顶 y 须用 seaColumnHeight（海域重塑高度），**非** heightAt
    //     （自然 fbm 高度）—— t338/t372 海面重塑后两者对海域列恒不等，旧实现误用 heightAt 读错 y → surf 恒非 Sand
    //     → 0 甘蔗。修：海域列用 seaColumnHeight 取真实沙顶；非海域列无沙顶直接跳过。
    //   t423：邻水查沙顶 surfaceY 与沙顶下一层 surfaceY-1 双层（沙滩沙顶常在 waterLevel+1、海水在 waterLevel 即
    //     沙顶下一层 → 须兼查 surfaceY-1 才命中海岸沙滩；与 tickSugarcaneGrowth 的 wateredAt(by)/wateredAt(by-1)
    //     同语义）。高度 1..3 独立哈希位段 (r>>16)%3 + 1（与密度位段 r%100 解耦），逐格向上仅写空气格 → 不覆盖已
    //     生成的方块（树 / 草 / 花）。纯函数于 seed + biomeAt + 海域（seaColumnHeight/isSeaSandColumn/hashColumn）→
    //     同 seed 同分布；禁用任何运行期随机源（与 placeTallGrass / placeDesertFlora 同守卫语义）。
    void placeSugarcane();
    // t467 雪原浆果灌木丛散布（PLAN §2-K 确定性）：遍历 Snowy 群系列，在积雪层（SnowLayer）地表上方一格低密度
    //   散布浆果灌木丛（SweetBerryBush cross 广告牌，仅写空气格）。机制等价 MC 1.0 sweet berry bush（寒冷群系浆果丛）。
    //   三守卫（同 placeTallGrass / placeFlowers 同族教训 t446 用对高度查询）：(1) 仅 Snowy 群系（biomeAt==Snowy，
    //   其它群系地表非雪）；(2) 地表须为 SnowLayer（雪原覆雪地表判定；海域 seaColumnHeight>=0 独立、湖/洞口顶替换了
    //   雪 → 跳过，不在水里 / 湖里生）；(3) surfaceY > kWaterLevel+1（不在沙滩带 / 水下生，机制等价 MC 浆果丛不生于
    //   水边沙）。阶段随机 1..2（独立哈希位段，与密度位段解耦）—— 不散布阶段 0（无果嫩丛无意义，worldgen 丛均带果）。
    //   纯函数于 seed + biomeAt（经 hashColumn）→ 同 seed 同分布；禁用任何运行期随机源。仅写空气格（setVoxelIfAir）
    //   → 不覆盖雪上已生成的方块（云杉树干 / 树叶 / 任何已占格）。
    void placeSweetBerryBushes();
    // t395 雪原/针叶群系水面冻结（PLAN §2-K 确定性）：遍历 Snowy 群系列，把海平面表层水（y==waterLevel 的 Water
    //   格）冻结为 Ice（机制等价 MC 1.0 寒冷群系水面结冰）。仅冻最顶层水面（同 MC 仅表层结冰；下层水保留）；
    //   地下水池（cy ≤ h-7 << waterLevel）不在 y==waterLevel 故不受影响。generate 在 fillWater 之后调（水已就位）。
    //   走 m_chunks.setBlock 直写（worldgen 静默，光场随后 recomputeLightField 重算）。纯函数于 seed（biomeAt）。
    void freezeSurfaceWater();
    // t278 洞穴隧道生成（PLAN §2-K 确定性）：terrain + 矿石散布之后、填水之前 carve 地下洞穴。两套叠加：
    //   (a) 3D Perlin 阈值洞（两路偏移 noise3 同时高于阈值 → 蜿蜒管状洞穴，机制等价 MC 1.0 Perlin 洞穴）；
    //   (b) Perlin worm 隧道 + 分叉（确定性起点、沿 noise 扰动方向逐球 carve、定期分叉 → 连通隧道网 + Y/十
    //       字路口）。范围 y ∈ (bedrockTop, h-4]：不动基岩底、保留表面草/土 + ≥1 格石顶 → 洞穴封闭地下（spec
    //       「内部黑暗」：天光 flood-fill 不穿实体 → 洞内无天光；recomputeLightField 在本 pass 之后跑）。
    //   纯函数于 seed（noise3 / hashColumn / hashVoxel）→ 同 seed 同洞穴分布。挖走 stone/dirt/ore，暴露矿石
    //   于洞壁（为 t279 洞穴裸露矿物铺路）；不挖 air/bedrock/water。
    void carveCaves();
    // t341 山坡洞口（spec「多地表连通洞穴入口 + 山坡」）：carveCaves 之后，在「山坡腰」列（4 邻列既有严格更高
    //   也有严格更低 = 处于坡面而非峰/谷）+「近表有真实洞穴 air（carveCaves 已挖空）」双重过滤下，确定性散布
    //   3×3 可通行大洞口（自地表下挖到既有洞穴顶格）。仅该列近表有 cave air 才开口 → 永不产孤立竖井（修 t339
    //   「无洞挖出 1×1 矿井」问题）。山坡低侧地形已低于洞口 → 该侧壁天然裸露可走入；高侧深入山体 = 山坡洞口观感。
    //   修 t309「1×1 竖井 + 3×3 仅 1 格浅坑」太窄不可走 → 现 3×3 全高。避开沙漠 / 沙滩 / 水下 / 低洼。纯函数于 seed
    //   （hashColumn + 已生成 chunk 的纯几何查询）→ 同 seed 同洞口分布（PLAN §2-K）。
    void carveCaveEntrances();
    // t342 大峡谷地貌（spec「地表长条裂缝（露天峡谷），内壁露矿石」）：scatterOres / carveCaves 之后、fillWater
    //   之前，确定性生成约 1 条贯穿地图的长窄露天峡谷。路径 = 长程 worm（自边界附近确定性出发、朝对侧 noise 缓
    //   弯行进 + 向 baseYaw 弱回复 → 蜿蜒贯穿）；横截面 = 上宽下窄阶梯 V 形（fbm 调制 → 弯曲峡壁）。自地表
    //   heightAt 下挖到峡谷底（落在矿层带内 → 两侧立壁纵贯煤/铜/铁/金矿层 → carve 暴露矿石于峡壁）。露天
    //   （清除地表草/土 → 天光直入）；不动基岩底层 / air / 水；跳过海域列（陆地地貌）。纯函数于 seed
    //   （hashColumn + noise2 / fbm）→ 同 seed 同峡谷（PLAN §2-K）。单条 worm → 每图约 1 条贯穿峡谷。
    void carveCanyon();
    // t309 地下水池（封闭洞穴静止水层；spec「地下水池（封闭洞穴静止水层）」）：carveCaves / carveCaveEntrances
    //   之后，地下深处确定性散布小型封闭水洼——carve 一个小椭球空腔（air 气室）+ 底层铺一层水源（state=0），
    //   形成「封闭洞穴静止水层」。空腔被周围实体岩石天然封闭 → 水源无水平 air 邻居可蔓延 → 稳态
    //   （tickWaterFlow 不扩散）；气室无天光 → 黑暗（机制等价 MC 1.0 地下水湖 / 封闭水洼）。纯函数于 seed
    //   （hashColumn）→ 同 seed 同水池分布（PLAN §2-K）。
    void placeUndergroundWaterPools();
    // t343 地下岩浆湖（spec「Y<30 随机封闭岩浆湖」；机制等价 MC 1.0 地下岩浆湖）：carveCaves /
    //   carveCaveEntrances 之后、fillWater 之前，地下深处（y < kLavaLakeMaxY=30）确定性散布小型封闭岩浆湖——
    //   carve 一个小椭球空腔（air 气室）+ 底层铺一层岩浆源（state=0），形成「封闭洞穴岩浆湖」。空腔被周围实体
    //   岩石天然封闭 → 岩浆源无水平 air 邻居可蔓延 → 稳态（tickLavaFlow 不扩散）；气室无天光 → 黑暗（机制等价
    //   MC 1.0 地下岩浆湖 / 封闭熔岩洼地）。纯函数于 seed（hashColumn）→ 同 seed 同岩浆湖分布（PLAN §2-K）。
    void placeLavaLakes();
    // t392 地下地牢（spec「地下小结构（圆石/石砖/苔石房），中央刷怪笼 + 1 战利品箱；worldgen 地下随机放置
    //   （一定密度）」；机制等价 MC 1.0 地牢 / 怪物房间）。carveCaves / carveCaveEntrances / placeLavaLakes 之后、
    //   fillWater 之前，地下深处（y ∈ [kBedrockTop+3, kDungeonMaxY]）确定性散布小型封闭房间：carve 一个
    //   W×H×D（默认 7×4×7）air 室 + 周界（地板 / 顶板 / 四壁）填 Cobble / Stone（机制等价 MC 1.0 地牢圆石 +
    //   苔石墙体；本工程暂无 mossy_cobble / stone_brick 方块，故墙体用 Cobble + Stone 混排）+ 中央放 Spawner +
    //   角落放 Chest（t393 战利品表填内容，本任务仅放置空箱方块）。空腔被实体墙天然封闭 → 房间内无天光 →
    //   黑暗（机制等价 MC 1.0 地牢黑暗 / 刷怪笼刷怪条件）。与既有洞穴重叠时（carveCaves 已挖空同位）→ 墙体
    //   在洞穴侧被截断仍可见地牢轮廓（同 MC 1.0 地牢可被洞穴穿墙暴露）。纯函数于 seed（hashColumn）→ 同 seed
    //   同地牢分布（PLAN §2-K）。**Spawner 不存清单**：tickSpawners 在 EntityManager 内**扫玩家周围**Spawner
    //   块（按需扫描，player-near 才扫），故 World 无需维护 spawner 位置列表 —— 破坏即停止刷怪由 tickSpawners
    //   查 blockAt != Spawner 自然实现（无 setBlock 钩子）。存档 round-trip：Spawner 是普通方块 id，chunk blob
    //   随存随读，加载后 tickSpawners 仍能扫到（同 Chest 物品存 ChestStore 独立于 chunk，Spawner 无状态）。
    void placeDungeons();
    // t484 废弃矿井（spec「地下（Y<50）随机生成：木栅栏立柱 + 矿车道（木地板/轨道）+ 蜘蛛网 + 暴露矿石 +
    //   宝藏箱子」；机制等价 MC 1.0 废弃矿井 mineshaft）。carveCaves / carveCaveEntrances / placeLavaLakes /
    //   placeDungeons 之后、fillWater 之前，地下深处（y ∈ [kBedrockTop+3, kMineshaftMaxY=48]）确定性散布
    //   矿井隧道系统：选起点 + 方向（hashColumn + seed 偏移，PLAN §2-K）→ 沿方向逐段 carve 一条 3×3 巷道
    //   （清空气、铺 Planks 木地板、按间距放 WoodFence 木栅栏立柱支撑、按间距放 Rail 铁轨、间隙散布 Cobweb 蜘蛛网、
    //   沿巷壁按 hashVoxel 散布矿石暴露、隧道末端 / 分支末端放带 ChestStateMineshaftFlag 标记的 Chest 宝藏箱，
    //   t393 同族首开填充由 Main.qml.openChest 据本标记触发 mineshaftChestPool）。隧道被周围实体岩天然封闭 →
    //   内部无天光 → 黑暗（机制等价 MC 1.0 矿井黑暗环境）。与既有洞穴重叠时（carveCaves 已挖空同位）→ 木地板 /
    //   立柱 / 铁轨仍画出（矿井结构叠加于洞穴）。纯函数于 seed（hashColumn / hashVoxel）→ 同 seed 同矿井分布
    //   （PLAN §2-K）。**宝藏箱内容**：Chest 物品存 ChestStore（同地牢箱），首开填充由 isMineshaftChest 判定。
    void placeMineshaft();
    // t485 沙漠神殿（spec「沙漠群系生成：金字塔外形（沙岩/切制沙岩）+ 地下密室 + 4 宝藏箱（钻石/金/青金石/
    //   骨头/腐肉）+ TNT 陷阱（踩压力板引爆）」；机制等价 MC 1.0 沙漠神殿 desert temple）。placeMineshaft 之后、
    //   fillWater 之前，**仅 Desert 群系**（isDesert 守卫，spec「沙漠群系生成」）确定性稀疏散布（grid 48，比矿井 36
    //   更稀；spec「低频」）：选沙漠中心点（hashColumn + seed 偏移，PLAN §2-K）→ 地表铺金字塔（阶梯砂岩 Sandstone +
    //   CutSandstone 顶饰，逐层缩半高成金字塔外形）→ 金字塔正下方地下挖密室（7×7×4 空气 + 砂岩墙 / 地板 / 顶板）→
    //   密室四角放 4 只带 ChestStatePyramidFlag 标记的 Chest 宝藏箱 → 密室中央 CobblePressurePlate 压力板下垫 3×3
    //   TntBlock（踩板 → playercontroller tick 扫 footprint 触发 detonateTntBlock → destroySphereSilent 球形破坏，
    //   机制等价 MC 1.0 沙漠神殿 TNT 陷阱）。纯函数于 seed（hashColumn / hashVoxel / biomeAt）→ 同 seed 同神殿分布
    //   （PLAN §2-K；确定性 + 不全图扫描，仅扫候选沙漠格）。**宝藏箱内容**：Chest 物品存 ChestStore，首开填充由
    //   isPyramidChest 判定 → pyramidChestPool（钻石 / 金 / 青金石 / 骨头 / 腐肉等）。
    void placeDesertTemple();
    // t486 丛林神殿（spec「丛林群系生成：苔石建筑 + 机关（绊线→发射器射箭，无红石用 dispenser 方块直接触发）
    //   + 宝藏箱」；机制等价 MC 1.0 丛林神殿 jungle temple）。placeDesertTemple 之后、fillWater 之前，**仅 Jungle
    //   群系**（biomeAt == Jungle 守卫，spec「丛林群系生成」）确定性稀疏散布（grid 40，略密于沙漠神殿 48 补偿丛林群系
    //   本身稀有；pct 50，spec「低频」→ 160×160 世界约 1-2 座）：选丛林中心点（hashColumn + seed 偏移，PLAN §2-K）→ 地表苔石建筑主体（MossyCobble 矩形围墙 +
    //   顶板 + 地板，spec「苔石建筑」）→ 内部走廊（空气 + 苔石墙）→ 走廊内嵌 Dispenser 陷阱（发射器嵌入走廊石壁
    //   朝向走廊中央 + 走廊地板 CobblePressurePlate 压力板；玩家踩板 → playercontroller tick 扫 footprint 触发
    //   scanDispenserTraps → spawnArrow 朝压力板方向射箭，机制等价 MC 1.0 丛林神殿发射器陷阱；无红石系统故
    //   「踩板直接触发」）→ 走廊尽头放 1 只带 ChestStateJungleFlag 标记的 Chest 宝藏箱。纯函数于 seed
    //   （hashColumn / hashVoxel / biomeAt）→ 同 seed 同神殿分布（PLAN §2-K；确定性 + 不全图扫描，仅扫候选丛林格）。
    //   **宝藏箱内容**：Chest 物品存 ChestStore，首开填充由 isJungleTempleChest 判定 → jungleTempleChestPool
    //   （骨头 / 腐肉 / 铁 / 金 / 钻石 / 箭 / 附魔书等）。
    void placeJungleTemple();
    // t487 要塞（spec「地下深（Y<30）生成：石砖迷宫 + 末地传送门房（末地传送门方块 + 12 末影之眼激活 → 末地
    //   预热，末地本身可推迟）+ 图书馆（书架，附魔加成）+ 银鱼刷怪笼」；机制等价 MC 1.0 要塞 stronghold）。
    //   placeJungleTemple 之后、fillWater 之前，地下深处（y ∈ [kBedrockTop+4, kStrongholdMaxY=30]，spec「Y<30」）
    //   确定性散布（grid 40，比矿井 36 略稀 → 要塞稀有；PLAN §2-K，仅扫候选格 → 不全图扫描）：选要塞中心点
    //   （hashColumn + seed 偏移）→ 生成一个 13×13×5 石砖地下迷宫：
    //     - 外圈石砖墙（StoneBrick 整立方围合）+ 地板 / 顶板（StoneBrick）→ 封闭黑暗（机制等价 MC 1.0 要塞石砖
    //       地牢氛围）；
    //     - 内部迷宫走廊（Air，石砖墙隔出十字形走道）+ 角落房间；
    //     - **图书馆**（一间 5×3×5 房间）：四壁内侧摆 Bookshelf（t474 书架，附魔台加成来源）+ 中央石砖台阶 /
    //       石砖楼梯（楼梯井装饰）→ 探索者可在附魔台旁补书架加成（机制等价 MC 1.0 要塞图书馆书架墙）；
    //     - **末地传送门房**（中央 7×7×3 房间）：地板中央 3×3 EndPortal（末地传送门方块，未激活 state=0）+
    //       周界 StoneBrick 墙 + 角落一只 ChestStateStrongholdFlag 标记的战利品箱（t487 首开填要塞战利品含末影之眼）；
    //     - **银鱼刷怪笼**（传送门房墙内 / 走廊）：Spawner + SpawnerStateSilverfishFlag state 标记 → tickSpawners
    //       据 flag 刷 Silverfish（机制等价 MC 1.0 要塞银鱼刷怪笼）；
    //     - 走廊交叉处散布 Cobweb 蜘蛛网（阴暗地下装饰，复用 t484 矿井蛛网机制）。
    //   纯函数于 seed（hashColumn / hashVoxel）→ 同 seed 同要塞分布（PLAN §2-K）。**宝藏箱内容**：Chest 物品存
    //   ChestStore，首开填充由 isStrongholdChest 判定 → strongholdChestPool（含末影之眼，激活传送门关键物品）。
    void placeStronghold();
    // t309 地表小湖泊（部分露出；spec「地表小湖泊（部分露出）」）：fillWater 之后，plains/forest 平坦地表
    //   确定性散布小型浅水湖——在局部低洼（disc heightAt 轻微起伏、湖岸外圈 ≥ surfaceY）的草地 carve 一个浅水盘
    //   （surfaceY-1 / surfaceY-2 两层水源），周围等高草地天然围成不溢漏的湖岸。湖部分露出（水面 = 周围草地顶 -1，
    //   肉眼可见）。t340：(a) fbm 调制每格有效半径 → 弯曲湖岸 / 半岛（非正圆）；(b) 约 half 湖在湖床之下藏空心穹顶
    //   气室（1 层石顶托水源 + stone 封闭 → 稳定 air 气室），「地表浅湖」与「地表浅湖 + 下伏空腔」两形态混排。
    //   仅 plains/forest，避开沙滩 / 水下 / 海平面附近（湖独立于海）。纯函数于 seed（hashColumn / fbm）→ 同 seed
    //   同湖泊分布（PLAN §2-K）。
    void placeSurfaceLakes();
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
    // t334 含 state 的增量光场入口（活版门开合：id 不变但 lightOpacity 翻转 → 须重 flood）。id 变更路径仍走
    //   上方 4 参数重载（state=0 委托；全实体 / air / 水 / 沙等的遮光与 state 无关，state=0 比较即正确）。
    void recomputeLightAround(int x, int y, int z, quint8 oldId, quint8 oldState,
                              quint8 newId, quint8 newState);
    // t154 有界盒清场 + 重 seed + 重 flood（recomputeLightAround 的实现核心，分离以便复用清/种/传播步骤）。
    //   doSky=true：两通道都重算（清两通道、重 seed 见天格 + 火把、边界种两通道、flood 两通道）。
    //   doSky=false：仅方块光（清方块光保留天光、重 seed 火把、边界种方块光、flood 方块光）——火把增删天光不变。
    //   t383 DEFINITIVE dirty-storm fix：reflood 后**仅对光场确有变化**的 chunk 标脏（reflood 前快照两通道、
    //     reflood 后逐 chunk 切片比对，首个变化格即标脏并提前退出）。旧版由 caller 标「盒内所有 chunk」脏
    //     （31×31 盒横跨 3~4 chunk → 每次破/放重建 9~16 chunk × 2 段 = 18~32 段/编辑），但 reflood 通常只改
    //     1~2 chunk 光 → 其余 chunk mesh 顶点色本就正确，无谓重建 = 持续破/放 dirty-storm 的真实根因（既非
    //     clearDirty 竞态也非缺批合并，而是「相邻失效」over-invalidate）。返回被标脏的 chunk 数（诊断）。
    //     三 caller（recomputeLightAround / decayLeavesAround / destroySphereSilent）统一经此精确标脏。
    int refloodBox(int x0, int y0, int z0, int x1, int y1, int z1, bool doSky);
    // t380r perf：批量流体 tick 延迟光照重算收口。setWaterSilent 在 m_batchFluid 下跳过 per-write
    //   recomputeLightAround（记入 m_pendingLightEdits）；本方法把延迟编辑的 ±R 盒之并做**一次** refloodBox
    //   （N 次重 flood → 1 次，同 destroySphereSilent t383 批量收口模式）。caller（tickWaterFlow /
    //   tickLavaFlow）在 m_batchFluid=false 后、emit worldChanged 前调用。正确性：每编辑影响区 ⊆ 其 ±R 盒
    //   ⊆ 联合盒 → 联合盒面边界种子法成立，终态与逐写 recomputeLightAround 一致。
    void flushPendingLightEdits();
    void setVoxelIfAir(int x, int y, int z, quint8 id);       // 仅写空气格（树冠不覆盖主干/地形）
    void setVoxelIfAir(int x, int y, int z, quint8 id, quint8 state); // t310：带 state（草变种 worldgen）
    quint32 hashColumn(int seed, int x, int z) const;         // 整数哈希（列级 seed/x/z）→ 确定性伪随机
    quint32 hashVoxel(int seed, int x, int y, int z) const;   // 整数哈希（体素级 seed/x/y/z）→ 矿石散布用
    // t380：块编辑后标记流体脏（驱动 tickWaterFlow/tickLavaFlow 早退）。查编辑格 + 6 正交邻是否含
    //   Water/Lava → 设对应 m_waterDirty/m_lavaDirty=true（见 m_*Dirty 字段头注释）。blockAt 越界返 Air
    //   安全（不需 bounds 检查）。编辑是 click-rate → 6 次 blockAt 可忽略。
    void pokeFluidDirty(int x, int y, int z);

    std::vector<int> m_perm;  // 512 置换表（Perlin）
    int m_width = 16, m_depth = 16, m_height = 16, m_seed = 1337;
    ChunkManager m_chunks;    // 多 chunk 存储 + 跨 chunk 路由（World 层；默认空，generate 重建）
    // t185 水流 tick 节流计数：tickWaterFlow() 每 100ms 被 WorldClock.ticked 调一次；累积到 kFlowTickInterval
    //   才把波前推进 1 格（~0.3s 一格 → 1 格/tick 流动动画可见）。MC 自身约 0.25s/格，本工程取 3（0.3s）平衡
    //   动画可见度与扫描开销（全图扫水格 ~1-2ms + 波前少量写入）。
    int m_flowTickCounter = 0;
    // t350 流体 tick 批量写标志：tickWaterFlow apply pass 开 → 每次 setWaterSilent 只写栅格 + 重光照，
    //   不 emit worldChanged / 不 clearAllDirty；caller 末尾一次性 emit + clearDirty。把「每 tick 写 N 格
    //   → N 次 worldChanged 扇出重建」合并为「1 次重建」，消除活跃扩散期卡顿。通用机制（t351 岩浆可同用）。
    bool m_batchFluid = false;
    // t380r perf：批量流体写延迟的光照重算缓冲（见 flushPendingLightEdits）。非批量（玩家/世界编辑）路径
    //   仍逐写即时 recomputeLightAround —— 光变化要立即反映；仅流体批量 tick 延迟合并。每项记录编辑坐标 +
    //   是否遮光变化（sky）—— 遮光变化须重 seed 天光列到顶（y1=H-1）。
    struct PendingLight { int x, y, z; bool sky; };
    std::vector<PendingLight> m_pendingLightEdits;
    // t380 流体脏标志（perf：tickWaterFlow/tickLavaFlow 早退优化）。稳态（海洋全源、无玩家扰动）时两 tick
    //   仍每 ~0.3s / ~3s 全图扫 W×D×H（80×80×64≈41 万格 × chunk 路由除法）只为发现「无候选 → 零变化」
    //   → 稳态帧每 0.3s 白付 ~3-5ms 扫描（= 大水域 / 长时游戏周期性卡顿；关联 t320/t354 屡报 lag）。改：
    //   仅当「自上次扫描后有流体相关编辑」才扫。setBlock / setBlockFromEntity / destroySphereSilent 调
    //   pokeFluidDirty（查编辑格 + 6 正交邻是否含 Water/Lava —— 流体只正交传播，对角不影响；块编辑可能扰动
    //   邻接流体平衡：破邻水的实体块 / 放块入水）；setWaterSilent 写 Water/Lava 时按 id/oldId 设对应标志
    //   （流体 tick 内部写入 → 链式扩散下 tick 续扫直到稳态）。tickWaterFlow/tickLavaFlow 入口检标志：
    //   false → 早退（跳过全图扫）；true → 扫描前清 false，apply 内 setWaterSilent 若真写则重设 true →
    //   稳态（零写入）后自然停扫。generate/finishLoad 末置两标志 true（一次性确认扫 → 新世界/存档态稳态
    //   即清，防御加载态含未稳流场）。标志只设 true（tick 自身清），不会漏触发（所有块写路径均 poke）。
    bool m_waterDirty = false;
    bool m_lavaDirty = false;
    // t488 perf：流体 tick **增量扫描活动盒**。根因：水×岩浆交互区（水流/岩浆流触邻凝固黑曜石/石头/圆石）
    //   每 tick 写入 → setWaterSilent / pokeFluidDirty 持续置 m_*Dirty → tickWaterFlow/tickLavaFlow 每回
    //   全量快照遍历**整个**水/岩浆格集合（大水体数万格 × 每格 ~15-40 blockAt）→ 用户实测 wat 29.7 + lav
    //   110.7 ms/s（lav 单次 ~330ms 每 3s 一次 spike）。修：只扫「最近有流体相关写入/编辑的区域」—— 每次
    //   setWaterSilent（写 Water/Lava 及凝固 Obsidian/Stone/Cobble，oldId/newId 含流体）与 pokeFluidDirty
    //   （块编辑邻接流体）把盒扩展到该格 ±1；tick 扫描前把盒拷到局部 + 清盒（下次 tick 盒 = 本次 tick 的
    //   写入），建快照时跳过盒外格 → 稳态大水体零扫描、交互区只扫波前 + 凝固区。正确性：流体格状态只可能
    //   因写入改变（本 tick apply 或外部编辑），任何会变化的格 ⊆ 最近写入 ±1 = 盒 → 无漏扫；流场波前 / 蒸发 /
    //   凝固级联逐 tick 经盒向前推（新写入再扩盒），行为与全量扫描一致。盒空但 m_*Dirty 置位（worldgen /
    //   加载一次性确认）→ 全量扫描兜底。ignite pass（邻岩浆木焚毁）用盒内格 —— 焚毁经 setBlock→pokeFluidDirty
    //   再扩盒，焚毁级联同样逐窗推进；远离活动的木块不焚毁（机制近似收敛，可接受）。
    int m_fluidActX0 = 0, m_fluidActY0 = 0, m_fluidActZ0 = 0;
    int m_fluidActX1 = 0, m_fluidActY1 = 0, m_fluidActZ1 = 0;
    bool m_fluidActValid = false; // 盒非空（有最近流体活动）
    // 把活动盒扩展到 (x,y,z)±1（含被写格本身，margin 1 覆盖 6 正交邻可反应格）。O(1)；流体 tick 内部写入 /
    //   块编辑触发，频率由活动强度决定。世界越界由写入路径先挡，此处不再钳。
    void fluidActExpand(int x, int y, int z);
    // 清空活动盒（每次流体 tick 扫描后 + 世界重置时）。扫描后清 → 盒只累积「自上次扫描以来」的活动。
    void fluidActReset();
    static constexpr int kFlowTickInterval = 3;   // tickWaterFlow 节流间隔（WorldClock tick 单位 = 100ms → 0.3s/格）
    static constexpr int kMaxFlowLevel = 7;       // 水流最大蔓延等级（state 1..7；机制等价 MC 1.0 流水 7 格扩散）
    // t343 岩浆流 tick 节流计数 + 常量：tickLavaFlow() 每 100ms 被 WorldClock.ticked 调一次；累积到
    //   kLavaFlowTickInterval 才把波前推进 1 格（~3s/格 → MC 主世界岩浆比水慢约 30 倍的可见缓慢流动）。
    //   kLavaFlowTickInterval=30（3s/格）+ kMaxLavaFlowLevel=3（最大 3 格水平扩散，vs 水 7）→ 岩浆流短而慢，
    //   机制等价 MC 1.0 主世界岩浆（Nether 岩浆与水同速，本工程仅主世界故取慢）。岩浆**无源再生**
    //   （MC 1.0 主世界岩浆不形成无限源）。
    int m_lavaFlowTickCounter = 0;
    int m_lavaIgniteIndex = 0; // ignite pass 窗口序号（喂 hashVoxel 散布概率 → 不同窗口不同木块错峰焚毁）
    static constexpr int kLavaFlowTickInterval = 30; // tickLavaFlow 节流间隔（WorldClock tick 单位 = 100ms → 3s/格）
    static constexpr int kMaxLavaFlowLevel    = 3;   // 岩浆流最大蔓延等级（state 1..3；MC 1.0 主世界岩浆 3 格扩散）
    static constexpr int kLavaIgnitePct       = 8;   // 邻岩浆木类每窗焚毁概率（%；8% → 平均 ~37s 焚毁，可见可验收）
    static constexpr int kLavaLakeMaxY        = 30;  // 岩浆湖最高 y（spec「Y<30」；仅地下深处）
    // t468 结冰 tick 节流计数 + 常量：tickIceFreeze() 每 100ms 被 WorldClock.ticked 调一次；累积到 kFreezeTickInterval
    //   才做一次冻结判定（~每 kFreezeTickInterval×0.1s = 5s 一窗）。窗口序号 m_freezeIntervalIndex 每窗 +1，喂入
    //   hashVoxel 散布概率 → 不同格不同窗错峰冻结（非瞬时全冻，PLAN §2-K 精神）。kFreezePct=20（每窗 20% 暴露水源
    //   冻结 → 平均 ~25s/格，可见可验收；worldgen 已冻结表层水，本 tick 主要处理动态暴露 / 玩家后放）。
    int m_freezeTickCounter = 0;
    int m_freezeIntervalIndex = 0;
    static constexpr int kFreezeTickInterval = 50;  // tickIceFreeze 节流间隔（WorldClock tick 单位 = 100ms → 5s/窗）
    static constexpr int kFreezePct          = 20;  // 每窗暴露水源冻结概率（%）
    // t495 普通冰融化 tick 节流计数 + 常量：tickIceMelt() 每 100ms 被 WorldClock.ticked 调一次；累积到
    //   kIceMeltTickInterval 才做一次融化判定（~每 kIceMeltTickInterval×0.1s 一窗）。窗口序号 m_iceMeltIntervalIndex
    //   每窗 +1，喂 hashVoxel 散布概率 → 不同冰格不同窗错峰融化（非瞬时全融，PLAN §2-K 精神，同 tickIceFreeze）。
    //   kIceMeltTickInterval=20（2s/窗）+ kIceMeltPct=25（每窗高亮邻候选冰格 25% 融化概率 → 几何分布平均 ~8s/格融化，
    //   贴近 MC 冰在火把旁数秒融的观感，可见可验收）。
    //   kIceMeltBlockLight=12：冰格自身方块光 ≥12 才算「高亮照射」（机制等价 MC 冰需 light level ≥12 融化；本工程
    //   简化为「邻发光源 OR 自身方块光 ≥12」即候选，二者其一即触发）。
    int m_iceMeltTickCounter = 0;
    int m_iceMeltIntervalIndex = 0;
    static constexpr int kIceMeltTickInterval = 20; // tickIceMelt 节流间隔（WorldClock tick 单位 = 100ms → 2s/窗）
    static constexpr int kIceMeltPct          = 25; // 每窗高亮邻候选冰格的融化概率（%；25% → 平均 ~8s/格融化）
    static constexpr int kIceMeltBlockLight   = 12; // 冰融化所需自身方块光阈值（/15；机制等价 MC ice light ≥12）
    // t236 小麦作物生长 tick 节流计数 + 常量：tickCropGrowth() 每 100ms 被 WorldClock.ticked 调一次；
    //   累积到 kCropTickInterval 才做一次成长判定（~每 kCropTickInterval×0.1s 一窗）。窗口序号 m_cropIntervalIndex
    //   每窗 +1，喂入 hashVoxel 散布概率 → 不同窗口不同作物错峰升阶段（防全部同步生长的机械感）。
    //   kCropTickInterval=25（2.5s/窗）+ kCropGrowPct=6% → 单株平均 ~42s/阶段、~5min 长满 7 阶段（可见、可验收；
    //   MC 1.0 约 31min 长满，本工程取快便于肉眼 / 测试复核，机制对齐非精确数值复刻）。kCropMinLight=9：头顶
    //   天光 ≥9/15 才长（机制等价 MC 作物需 light level 9+；夜间 / 洞穴不长）。
    //   **t447 减速修正**：旧 kCropGrowPct=35% 配耕地湿润倍率（dry 1×..wettest 4×）+ 雨水 2× → 湿润耕地
    //   growPct 常超 100% 被钳到 100%（每窗必升 = ~17s 长满 = 用户报「秒熟」）。降到 6% 后：dry ~42s/阶段、
    //   wettest(hydr=3) ~10s/阶段、wettest+rain ~5s/阶段——最大 48%（不再被钳到 100%，湿润不再秒熟），机制
    //   仍对齐 MC「湿润加速」但整体慢到合理。倍率 / 雨水逻辑（tickCropGrowth 内）不变，仅降基底概率。
    int m_cropTickCounter = 0;
    int m_cropIntervalIndex = 0;
    static constexpr int kCropTickInterval = 25;  // tickCropGrowth 节流间隔（WorldClock tick 单位 = 100ms → 2.5s/窗）
    static constexpr int kCropMinLight     = 9;   // 生长所需最低天光（/15；机制等价 MC 作物 light level 9+）
    static constexpr int kCropGrowPct      = 6;   // 每窗每株升阶段的散布概率（%；6% → dry 平均 ~42s/阶段；t447 由 35 降）
    // t406 甘蔗生长 tick 节流计数 + 常量：tickSugarcaneGrowth() 每 100ms 被 WorldClock.ticked 调一次；累积到
    //   kSugarcaneTickInterval 才做一次生长判定（~每 kSugarcaneTickInterval×0.1s 一窗）。窗口序号
    //   m_sugarcaneIntervalIndex 每窗 +1 喂入 hashVoxel 散布概率 → 不同甘蔗柱错峰生长。
    //   kSugarcaneTickInterval=50（5s/窗）+ kSugarcaneGrowPct=20% → 每柱每升一格平均 ~25s（可见、可验收；MC 1.0 约 ~每
    //   16 ticks 一次随机 tick 取快便于肉眼复核，机制对齐非精确数值复刻）。
    //   kSugarcaneMaxHeight=5：甘蔗柱最高 5 格（spec「max5」；但 5 高须罕见 → 由 kSugarcaneTallPct 门控）。
    //   kSugarcaneTallPct=10：柱基「拔高潜力」一次性门控（% of 列；列位 + seed 哈希，与窗口无关 → 稳态确定）。
    //     多数柱止于 worldgen 初生 1..3 高；仅 ~10% 潜力柱可继续长到 4..5（spec「5 rare / 1..3 common」；
    //     机制等价 MC 自然甘蔗多 1..3、偶有更高）。t418 修「全柱最终长到 5」bug：旧逻辑每柱不停生长直至封顶
    //     → 稳态全 5 高，与 spec 不符。
    int m_sugarcaneTickCounter = 0;
    int m_sugarcaneIntervalIndex = 0;
    static constexpr int kSugarcaneTickInterval = 50; // tickSugarcaneGrowth 节流间隔（WorldClock tick = 100ms → 5s/窗）
    static constexpr int kSugarcaneGrowPct      = 20; // 每窗每柱升一格的散布概率（%；20% → 平均 ~25s/升）
    static constexpr int kSugarcaneMaxHeight    = 5;  // 甘蔗柱最高格数（spec「max5」；超出停长）
    static constexpr int kSugarcaneTallPct      = 10; // 拔高潜力柱占比（%；稳态仅此比例柱可达 5 → 5 高罕见）
    // t406 耕地湿润复算 tick 节流计数 + 常量：tickFarmlandHydration() 每 100ms 被 WorldClock.ticked 调一次；累积到
    //   kFarmlandHydrTickInterval 才复算一次（~每 kFarmlandHydrTickInterval×0.1s 一窗）。复算用
    //   farmlandHydrationLevel（水源切比雪夫半径 4）→ 与存档 state 不等才静默写新等级。kFarmlandHydrTickInterval=30
    //   （3s/窗）→ 后放水 / 挖水约 3s 内耕地湿润度更新（可见、可验收；MC 1.0 走 random tick 较慢，取快便于肉眼复核）。
    int m_farmlandHydrTickCounter = 0;
    static constexpr int kFarmlandHydrTickInterval = 30; // tickFarmlandHydration 节流间隔（100ms → 3s/窗）
    // t305 树苗生长 tick 节流计数 + 常量：tickSaplingGrowth() 每 100ms 被 WorldClock.ticked 调一次；
    //   累积到 kSaplingTickInterval 才做一次成长判定（~每 kSaplingTickInterval×0.1s 一窗）。窗口序号
    //   m_saplingIntervalIndex 每窗 +1，喂入 hashVoxel 散布概率 → 不同窗口不同树苗错峰生长。
    //   kSaplingTickInterval=50（5s/窗）+ kSaplingGrowPct=10% → 单株平均 ~50s 长成（可见、可验收；MC 1.0 自然
    //   生长 ~1-5min，本工程取快便于肉眼 / 测试复核，机制对齐非精确数值复刻）。kSaplingMinLight=9：头顶
    //   天光 ≥9/15 才长（机制等价 MC 树苗 light level 9+；夜间 / 洞穴不长）。
    int m_saplingTickCounter = 0;
    int m_saplingIntervalIndex = 0;
    static constexpr int kSaplingTickInterval = 50; // tickSaplingGrowth 节流间隔（WorldClock tick 单位 = 100ms → 5s/窗）
    static constexpr int kSaplingMinLight     = 9;  // 生长所需最低天光（/15；机制等价 MC 树苗 light level 9+）
    static constexpr int kSaplingGrowPct      = 10; // 每窗每株长成的散布概率（%；10% → 平均 ~50s 长成）
    // t514 甜浆果丛生长 tick 节流计数 + 常量：tickSweetBerryBushGrowth() 每 100ms 被 WorldClock.ticked 调一次；
    //   累积到 kBerryBushTickInterval 才做一次成长判定（~每 kBerryBushTickInterval×0.1s 一窗）。窗口序号
    //   m_berryBushIntervalIndex 每窗 +1，喂入 hashVoxel 散布概率 → 不同丛错峰升阶段（防全部同步生长的机械感）。
    //   kBerryBushTickInterval=50（5s/窗）+ kBerryBushGrowPct=15% → 单丛平均 ~33s/阶段、~66s 从 state 0 长到成熟
    //   （可见、可验收；MC 1.0 浆果丛约 30min 长满，本工程取快便于肉眼 / 测试复核，机制对齐非精确数值复刻）。
    //   kBerryBushMinLight=9：头顶天光 >=9/15 才长（机制等价 MC 浆果丛 light level 9+；夜间 / 洞穴不长）。
    int m_berryBushTickCounter = 0;
    int m_berryBushIntervalIndex = 0;
    static constexpr int kBerryBushTickInterval = 50; // tickSweetBerryBushGrowth 节流间隔（100ms → 5s/窗）
    static constexpr int kBerryBushMinLight     = 9;  // 生长所需最低天光（/15；机制等价 MC 浆果丛 light level 9+）
    static constexpr int kBerryBushGrowPct      = 15; // 每窗每丛升阶段的散布概率（%；15% → 平均 ~33s/阶段）
    // t325 树叶渐进衰减队列 + 节流计数 + 常量：tickLeafDecay() 每 100ms 被 WorldClock.ticked 调一次；
    //   累积到 kLeafDecayTickInterval 才开一个判定窗口（~每 kLeafDecayTickInterval×0.1s 一窗）。窗口序号
    //   m_leafDecayIntervalIndex 每窗 +1，喂入 hashVoxel 散布概率 → 不同叶错峰渐退（非全部同步消失）。
    //   m_decayingLeaves 持当前失撑但尚未消失的叶坐标（坐标打包成 quint64 键，去重；队列空 → 稳态零开销早退）。
    //   kLeafDecayTickInterval=4（0.4s/窗）+ kLeafDecayPct=1（1%/窗）→ 几何分布平均寿命 ~40s、中位 ~28s、长尾至 90s+
    //   （t379：t325 原值 3/2 ~15s 仍偏快 → 放慢约 2.5×，叶子更持久渐退）；MC 1.0 叶衰为 random-tick 式渐退，
    //   本工程机制对齐非精确数值复刻。
    std::unordered_set<quint64> m_decayingLeaves; // 失撑叶坐标集合（packCell 打包键；tickLeafDecay 消费 + 清出队）
    // t425 perf：生长方格（作物 / 甘蔗 / 耕地 / 树苗）位置索引 —— 生长 tick（crop/sugarcane/farmland/sapling）
    //   遍历此集合（O(生长格数)）替代全图扫描（O(W×D×H)=3.3M，suspect c/d 掉帧主因）。写入路径
    //   （setBlock/setBlockFromEntity/setWaterSilent/setVoxelIfAir）经 noteGrowthWrite 增量维护；
    //   generate/beginLoad 清空、finishLoad 全图重建（存档 blob 直写不经写入路径）。
    std::unordered_set<quint64> m_growthCells;
    // perf：流体方格位置索引（Water / Lava 各一集）—— 流体 tick 遍历此集（O(流体格数)）替代全图扫描
    //   （O(W×D×H)=3.28M）。写入路径经 noteFluidWrite 增量维护；generate/beginLoad 清空、finishLoad
    //   全图重建（存档 blob / worldgen 直写不经写入路径）。键编码复用 packGrowthCell。稳态（无流体写入）
    //   时配合 m_waterDirty / m_lavaDirty 早退 → 零扫描；活跃流场时扫描量 = 流体格数（远 < 3.28M）。
    std::unordered_set<quint64> m_waterCells;
    std::unordered_set<quint64> m_lavaCells;
    // t495 perf：普通冰（Ice=45，不含 PackIce/BlueIce —— 那些永不融化）方格位置索引 —— 融化 tick（tickIceMelt）
    //   遍历此集（O(冰格数)）替代全图扫描（O(W×D×H)=3.28M）。写入路径经 noteIceWrite 增量维护；generate/beginLoad
    //   清空、finishLoad 全图重建（存档 blob / worldgen 直写不经写入路径）。键编码复用 packGrowthCell。稳态（无冰
    //   写入）时配合融化 tick 早退 → 零扫描；活跃融化时扫描量 = 冰格数（远 < 3.28M）。
    std::unordered_set<quint64> m_iceCells;
    int m_leafDecayTickCounter = 0;
    int m_leafDecayIntervalIndex = 0;
    static constexpr int kLeafDecayTickInterval = 4; // tickLeafDecay 节流间隔（WorldClock tick 单位 = 100ms → 0.4s/窗）
    static constexpr int kLeafDecayPct          = 1; // 每窗每叶消失的散布概率（%；1% → 平均 ~40s 渐退，散布至 90s+）
    // t385 天气态 + 计时（运行期随机模拟；构造 / generate / beginLoad 经 resetWeather 重置为 Clear + 随机晴时长）。
    Weather m_weather = Weather::Clear;
    float m_weatherTimer = 0.0f;  // 当前天气态剩余秒数（倒数到 0 → 随机转换；构造时设首个晴时长）
    // t386 闪电计时（仅雷态有意义）：当前到下一次闪电击中的剩余秒数（倒数到 0 → strikeLightning + 重置随机间隔）。
    //   resetWeather 设首击间隔；非雷态不递减（tickWeather 守 m_weather==Thunder）。机制等价 MC 1.0 雷暴期随机闪电。
    float m_lightningTimer = 0.0f;
    // t386 闪电间隔常量（秒；机制对齐 MC 1.0 雷暴期 ~每几秒一闪，取短便于肉眼 / 测试复核）。雷暴期内每
    //   kLightningIntervalMin..Max 秒随机一击（~4-12s/击）→ 一次 25-50s 雷暴平均 4-7 次闪电，可见可验收。
    static constexpr float kLightningIntervalMin = 4.0f;
    static constexpr float kLightningIntervalMax = 12.0f;
    // t385 天气时长常量（秒；机制对齐 MC 1.0「~0.5-1 天一态」取短便于肉眼 / 测试复核）。晴阶段 kClearWeatherMin..Max
    //   （~45-120s）、降水阶段 kWeatherDurMin..Max（~35-80s）、雷态 kThunderDurMin..Max（~25-50s，更短）。
    //   首场天气前的晴阶段 kInitialClearMin..Max 偏短（~20-45s）→ 进世界 ~1 分钟内可见首场天气（验收可见性）。
    static constexpr float kClearWeatherMin = 45.0f;
    static constexpr float kClearWeatherMax = 120.0f;
    static constexpr float kWeatherDurMin   = 35.0f;
    static constexpr float kWeatherDurMax   = 80.0f;
    static constexpr float kThunderDurMin   = 25.0f;
    static constexpr float kThunderDurMax   = 50.0f;
    static constexpr float kInitialClearMin = 20.0f;
    static constexpr float kInitialClearMax = 45.0f;
    // t385 重置天气态（Clear + 随机晴时长）；构造 / generate / beginLoad 调。态真翻才 emit weatherChanged
    //   （加载 / 新世界天气从 Clear 起；构造期无监听者，emit 亦无害）。
    void resetWeather();
    // t386 触发一次闪电击中（雷态 m_lightningTimer 归零时调）：在 [0,width)×[0,depth) 内随机选一列，取其列顶
    //   实面（heightmapAt）为击中点；空列（heightmap<0，如纯海域上空）→ 跳过不发信号（无可见落点）。击中点若为
    //   木类方块（同 tickLavaFlow isWoodLike 判定）→ setBlock Air 焚毁（发 blockBroken → 破块粒子/音，机制等价 MC
    //   雷击点燃木质）；非木类仅闪光 / 雷声 / 伤害（由上层 Connections 据 lightningStruck 信号消费）。末尾 emit
    //   lightningStruck(x,y,z) 驱动呈现层（白闪 + playThunder）+ 实体层（mob / 玩家近击中点伤害）。位置随机于世界
    //   内（World 不知玩家位 → 不依赖上层；闪光 / 雷声为全局反馈，落点仅决定火 / 实体伤）。分层（PLAN §2）：
    //   只读 m_chunks + 写栅格（setBlock）+ 发信号；不依赖 Renderer / Physics / Game / Entities。
    void strikeLightning();
    // t425 perf：生长方格集合增量维护（写入路径在 m_chunks.setBlock 后调；id 变更时按 oldId/newId 是否
    //   生长方块增删集合项；id 不变如作物升阶段 / 耕地湿润度变 → no-op）。
    void noteGrowthWrite(int x, int y, int z, quint8 oldId, quint8 newId);
    // t425 perf：全图扫描重建生长方格集合（finishLoad 存档 blob 直写后调一次；运行期由 noteGrowthWrite 维护）。
    void rebuildGrowthCells();
    // perf：流体方格（Water / Lava）位置索引增量维护 —— 流体 tick（tickWaterFlow / tickLavaFlow /
    //   tickIceFreeze）遍历此集合（O(流体格数)）替代全图扫描（O(W×D×H)=3.28M × chunk 路由除法）。
    //   写入路径（setBlock / setBlockFromEntity / setWaterSilent / setVoxelIfAir）在 m_chunks.setBlock 后调
    //   本方法（id 变更时按 oldId/newId 是否流体方块增删对应集合项；id 不变如水流 level 变 → no-op）。
    //   generate / finishLoad 末重建一次（worldgen / 存档 blob 直写不经写入路径）。键编码复用 packGrowthCell。
    void noteFluidWrite(int x, int y, int z, quint8 oldId, quint8 newId);
    // perf：全图扫描重建流体方格集合（generate / finishLoad 末调一次；运行期由 noteFluidWrite 增量维护）。
    void rebuildFluidCells();
    // t495 perf：普通冰（Ice=45，不含 PackIce/BlueIce）方格位置索引增量维护 —— 融化 tick（tickIceMelt）遍历此集合
    //   （O(冰格数)）替代全图扫描（同 noteFluidWrite / noteGrowthWrite 模式）。写入路径（setBlock / setBlockFromEntity /
    //   setWaterSilent / setVoxelIfAir / clearBlockSilent）在 m_chunks.setBlock 后调本方法（id 变更时按 oldId/newId 是否
    //   普通冰 Ice 增删集合项；id 不变 → no-op）。generate / finishLoad 末重建一次（worldgen / 存档 blob 直写不经写入路径）。
    //   键编码复用 packGrowthCell。仅 Ice=45 入集（PackIce/BlueIce 永不融化 → 不入集，免 tick 无谓扫描它们）。
    void noteIceWrite(int x, int y, int z, quint8 oldId, quint8 newId);
    // t495 perf：全图扫描重建普通冰方格集合（generate / finishLoad 末调一次；运行期由 noteIceWrite 增量维护）。
    void rebuildIceCells();
};

#endif // WORLD_H
