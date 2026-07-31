#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString（用户可见中文名）

// 方块注册表（单一权威数据源；Core 层）。
//
// 把「方块 id → 外观瓦片 / 是否实体 / 中文名 / 硬度 / 采掘要求 / 掉落 / 堆叠上限」全部
// 收敛到**一处** BlockDef 表，取代散落在 chunkgeometry::tileFor、toolregistry::kBlockMine、
// hotbar::kBlockMaxStack 里的硬编码。mesher(Renderer) / worldgen(World) / 挖掘系统(Game) /
// 背包 Hotbar(ViewModel) 都**只读**本表，不得各持副本（PLAN §2：世界数据单一）。
//
// 分层（PLAN §2）：本层属 Core（仅依赖 QtGlobal/QString），**不得**依赖
// Renderer/QtQuick3D/Physics/QtQuick/World/Game。依赖只向下。ToolType 枚举放本层是因为
// 「方块需要何种工具采掘」本质是方块的属性；ToolRegistry(Game) 复用之（向下依赖 Core）。
//
// 方块 id（稳定可引用；worldgen/网格/存档都按 id 引用，勿随意改顺序/插值）：
//   0=air 1=grass 2=dirt 3=stone 4=cobble 5=log 6=planks 7=leaves 8=sand 9=crafting_table
//   10=furnace 11=coal_ore 12=iron_ore 13=torch 14=bedrock
// air 恒 solid=false / hardness=0 / 不掉落。方块名用通用词，零 MC 专有名词（PLAN §9）。
class BlockRegistry
{
public:
    // 方块 id（与体素栅格的 quint8 存储对齐；底层类型 quint8 便于直接赋给栅格）。
    enum Id : quint8 {
        Air           = 0,
        Grass         = 1,
        Dirt          = 2,
        Stone         = 3,
        Cobble        = 4,
        Log           = 5,
        Planks        = 6,
        Leaves        = 7,
        Sand          = 8,
        CraftingTable = 9, // 工作台：右键打开 3×3 合成（t50）；机制等价 MC 工作台，名称/贴图原创（§9）。
        Furnace       = 10, // 熔炉：8 圆石围圈合成（t80）；机制等价 MC 熔炉，名称/贴图原创（§9）。
        CoalOre       = 11, // 煤矿石：散布于 stone 区段（t84）；机制等价 MC 煤矿，名称/贴图原创（§9）。
        IronOre       = 12, // 铁矿石：散布于 stone 区段（t84）；机制等价 MC 铁矿，名称/贴图原创（§9）。
        Torch         = 13, // 火把：伪光源方块（t88）；机制等价 MC 火把。solid=false 非实体碰撞、
                           // hardness=0 瞬破、NoTool；掉落自身。**视觉光源走伪光源**（Main.qml 发光
                           // Model + 光晕，NoLighting 高 baseColor 暖色），**非** QtQuick3D PointLight
                           // （lit 材质渲染红线，lessons-learned；真 flood-fill 方块光留 PLAN §M）。
        Bedrock       = 14, // 基岩：世界底层不可破坏方块（t119）；机制等价 MC 基岩。solid=true 实体碰撞、
                           // **hardness=-1.0**（负值 → ToolRegistry::canMine 自动 false，任何模式任何工具
                           // 均不可破，防创造秒破底层）、dropId=0（破不掉落，但实际不可破故永不触发）、
                           // 各面同贴图（tile 18）。worldgen 在 y 0..4 按 hashVoxel 坑洼铺一层（底实顶疏）。
        // ── t134 不完整方块（异形几何段，id >= FirstPartial）：6 类木制半方块，机制等价 MC 1.0
        //   (id, metadata) 方块模型。solid=false（同 torch：非整立方 → 不挡邻居面剔除，避免相邻整立方
        //   被错误剔除出「洞」；逐形状精确碰撞留后续任务）。各面同贴图=planks(8)、hardness=2.0（木质）、
        //   NoTool（空手可采且掉落）。掉落自身。mesher 经 PartialBlockGeometry::append 按 (id,state) 生成
        //   异形顶点并合批进 chunk mesh。state 编码朝向 / 开合 / 半位（见各枚举注释 + partialblockgeometry.cpp）。
        WoodSlab          = 15, // 木板台阶：半高（上/下半）。state bit0 = 上半(1)/下半(0)。
        WoodStairs        = 16, // 木板楼梯：下层整步 + 上层背墙。state[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z（楼梯朝该向开）。
        WoodFence         = 17, // 木栅栏：中心立柱（0.4 见方）。state=0（单格；连接邻居留后续）。
        WoodPressurePlate = 18, // 木板压力板：贴地薄板。state=0。
        WoodDoor          = 19, // 木板门：两格高（下/上格同 id）。state: bit3=上格(1)/下格(0)、bit2=开(1)/合(0)、
                                //   bit[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z。maxStack=1（单件，不可堆叠）。
        WoodTrapdoor      = 20, // 木板活板门：水平/竖直薄板。state bit0=开(1，竖直贴边)/合(0，水平贴地)，
                                //   bit[2:1]=开时朝向 0=+X 1=-X 2=+Z 3=-Z。
        Count         = 21, // 哨兵：已定义方块数（含 air），也是合法 id 的上界（id < Count）。
    };

    // t133 不完整方块段起点哨兵：WoodSlab(15) 起的 id 走 PartialBlockGeometry 异形渲染（mesher 合批进
    //   chunk mesh，不走 1×1×1 立方面路径）。t134 已落地 6 类（WoodSlab=15 ... WoodTrapdoor=20，Count=21）；
    //   mesher 的 `b >= FirstPartial` 分支现对这 6 id 激活。机制等价 MC 1.0 (id, metadata) 方块模型：
    //   id >= FirstPartial 即「异形方块」（非整立方）。
    static constexpr int FirstPartial = 15;

    // 面索引（与 Renderer 的 kFaces 顺序一致，是 World/Renderer 共享的轴向约定）：
    //   0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z
    enum Face : int {
        PosX   = 0,
        NegX   = 1,
        Top    = 2, // +Y
        Bottom = 3, // -Y
        PosZ   = 4,
        NegZ   = 5,
    };

    // 工具类型（决定能采掘哪类方块；BlockDef.toolType 与 ToolRegistry::ToolDef.type 共用同一枚举）。
    // 当前仅镐；未来扩铲 / 斧时按序追加。放 Core 层是因为它属「方块的采掘属性」。
    enum ToolType : int {
        NoTool  = 0, // 空手可采且掉落
        Pickaxe = 1, // 镐：采掘石类方块（stone / cobble）
    };

    // 音效材质分组（t118）：决定破 / 挖 / 走音色按方块材质分流（石 / 木 / 草 / 沙 / 叶 5 组 +
    // 兜底 Default）。放 Core 层是因为「方块敲起来听感是哪类材质」本质是方块的属性（与 hardness /
    // toolType 同性质），由 BlockRegistry 单一权威给出，AudioManager（Core/Platform）只读查询、
    // 不另持映射副本（PLAN §2：世界数据单一）。groupFor() 是 id → MaterialGroup 的纯函数。
    //   - Stone：stone / cobble / furnace / coal_ore / iron_ore（石质，硬、脆、明亮敲击）
    //   - Wood：log / planks / crafting_table（木质，中空闷击）
    //   - Grass：grass / dirt（软土 / 草皮，柔垫）
    //   - Sand：sand（颗粒沙响）
    //   - Leaves：leaves（叶沙沙）
    //   - Default：air / torch / 未知（用 Stone 兜底音色；多数无 mining 音路径）
    enum MaterialGroup : int {
        GroupStone   = 0,
        GroupWood    = 1,
        GroupGrass   = 2,
        GroupSand    = 3,
        GroupLeaves  = 4,
        GroupDefault = 5, // 哨兵：合法组上界（实际播放时 GroupDefault → 复用 GroupStone 兜底）
    };

    // 方块定义（每方块一项；单一权威数据源）。改方块任何属性只改 kDefs 一行，全工程生效。
    // 行索引 == 方块 id（air 行同时作越界 / 不可挖掘 / 不掉落兜底）。
    struct BlockDef {
        int id;              // 方块 id（= 行索引；自描述，便于日志 / 调试对照）
        int topTile;         // +Y(Top) 图集瓦片序号
        int bottomTile;      // -Y(Bottom) 瓦片序号
        int sideTile;        // +X / -X / +Z（三个侧面统一）瓦片序号
        int frontTile;       // -Z(NegZ「前面」) 瓦片序号（熔炉炉口等有朝向的方块用）；多数 == sideTile
        bool solid;          // 实体（参与碰撞 / culled 面剔除）；air=false
        float hardness;      // 基础硬度（挖掘耗时基准；<=0 → 不可挖掘，canMine=false）
        int toolType;        // 采掘所需工具类型（ToolType；NoTool=空手可采且掉落）
        int minToolTier;     // 采掘所需最低工具等级（toolType=NoTool 时忽略，恒 0）
        int dropId;          // 破坏后掉落物品 id（<=0 → 不掉落；stone→cobble 等「冶炼转化」在此表达）
        int dropCount;       // 掉落数量（生存破块产出物品实体数；创造秒破不掉落，由 caller 判）
        int maxStack;        // 单栈最大堆叠（Hotbar / 背包上限；air=0；工具段另走 ToolRegistry，恒 1）
        const char *name;    // 内部 / 调试用名（通用词，英文标识符；非面向用户）
        const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词；air=空串）
    };

    // 取方块定义（const 引用）。air / 越界 → 返回 air 行（不可挖掘、不掉落、不实体）。
    // 供 ToolRegistry（挖掘 / 掉落判定）与 Hotbar（maxStack）等只读查询，避免各持副本。
    static const BlockDef &def(quint8 blockId);

    // 给定方块 id 与面，返回**图集瓦片序号**（须与 tools/build_atlas.py 打包顺序一致）。
    // 越界/未知 id 返回 0（兜底，与旧 tileFor 同语义）。
    //
    // 瓦片顺序（一个偏差即渗色/错贴）：
    //   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
    //   5=cobble 6=log_top 7=log_side 8=planks 9=leaves
    //   10=crafting_table_top 11=crafting_table_side
    //   12=furnace_top 13=furnace_side 14=furnace_front（t80；炉口朝 -Z）
    //   15=coal_ore 16=iron_ore（t84；矿石各面同贴图）
    //   17=torch（t88；6 面同贴图，近黑底+火焰图案）
    //   18=bedrock（t119；6 面同贴图，深灰斑驳不可破坏底岩）
    // 图集由 tools/build_atlas.py 打包全部 19 瓦片；mesher / BlockCube 的 N=19 与之严格对齐。
    // -Z 面（NegZ「前面」）走 frontTile（熔炉炉口；其余方块 frontTile == sideTile，无视觉差异）。
    static int tileIndex(quint8 blockId, Face face);

    // 方块是否实体（参与碰撞 / culled 面剔除）。air 恒 false；torch 亦 false（非实体、不挡邻居面）；
    // 其余填表 solid=true。越界/未知 id 返回 false。mesher 邻居面剔除走本谓词（单一权威），
    //   切勿在渲染层另写 `!= 0`（会把 torch 当 solid → 误剔邻居面 → 透明 bug，见 t130）。
    static bool isSolid(quint8 blockId);

    // 挖掘 / 掉落 / 堆叠属性访问器（t42 集中表查；越界 → air 行默认：hardness=0 / NoTool / 不掉落 / maxStack=0）。
    static float hardness(quint8 blockId);    // 基础硬度
    static int toolType(quint8 blockId);      // 采掘所需工具类型（ToolType）
    static int minToolTier(quint8 blockId);   // 最低工具等级
    static int dropId(quint8 blockId);        // 破坏后掉落物品 id（<=0=不掉落）
    static int dropCount(quint8 blockId);     // 掉落数量
    static int maxStack(quint8 blockId);      // 单栈最大堆叠

    // 内部/调试用方块名（**非**面向用户字串；通用词）。越界/未知 id 返回 "unknown"。
    static const char *blockName(quint8 blockId);

    // 用户可见的**中文**显示名（PLAN §9 override (b)：通用描述词）。
    //   air → 空串；越界/未知 id → 空串（兜底）。
    // 与 blockName()（内部英文标识符）分离：本方法供 HUD/背包等面向用户文本消费；
    // 字面量为 UTF-8，由 fromUtf8 解码（与项目既有中文注释同源）。
    //   grass=草方块 dirt=泥土 stone=石头 cobble=圆石 log=橡木原木
    //   planks=橡木木板 leaves=橡树树叶 sand=沙子 crafting_table=工作台 furnace=熔炉
    //   coal_ore=煤矿石 iron_ore=铁矿石 bedrock=基岩
    static QString displayName(quint8 blockId);

    // 音效材质分组（t118）：id → MaterialGroup 纯函数。AudioManager 据此选 break / mining / step
    // 音色（spec「按方块材质分组 clip 池」）。越界 / 未知 → GroupDefault（AudioManager 内部兜底
    // 复用 GroupStone 音色）。机制等价 MC「按方块材质 SoundType 选声」（机制对齐，非名词照搬）。
    static MaterialGroup materialGroup(quint8 blockId);

private:
    BlockRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // BLOCKREGISTRY_H
