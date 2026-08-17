#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString（用户可见中文名）
#include <vector>   // collisionAABBs/selectionAABBs 返回 std::vector<BlockAABB>

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
//   10=furnace 11=coal_ore 12=iron_ore 13=torch 14=bedrock ... 21=water 22=chest 23=farmland
//   24=tall_grass（草丛；cross 广告牌方块）。25=wheat_crop（小麦作物；cross + state=生长阶段 0..7）。
//   26=diamond_ore（t279 钻矿石；散布于 stone 深层 y∈[5,40]，需铁镐采掘）。
//   27=wool（t300 羊毛方块）。28=sapling（t305 树苗；cross 广告牌方块，种在草地/泥土上随时间生长成橡树）。
//   29=copper_ore（t308 铜矿石；散布于 stone 浅中层 y∈[5,45]，需石镐采掘；掉铜原矿→熔炉烧铜锭）。
//   30=gold_ore（t308 金矿石；散布于 stone 深层 y∈[5,25]，需铁镐采掘；掉金原矿→熔炉烧金锭）。
//   41=sandstone / 42=cactus / 43=dead_bush（t394 沙漠三件套：砂岩沙下成岩 / 仙人掌接触伤害 / 枯灌木装饰）。
//   44=snow_layer / 45=ice / 46=spruce_log（t395 雪原/针叶三件套：地表覆雪 / 水面冻结冰 / 云杉树主干）。
//   47=lily_pad / 48=mushroom（t396 沼泽植物：水面浮叶 / 草地小蘑菇）。
//   49..52=flower_red/yellow/blue/white / 53=sugarcane（t397 多群系装饰植物：4 色花 + 水边甘蔗）。
//   54=glass（t405 玻璃：沙子冶炼产物方块；透明整立方——solid=false 透面剔除 + glassOnly 段半透渲染，
//      机制等价 MC 1.0 玻璃 glass）。
//   115=brown_mushroom（t507 白蘑菇 / 棕蘑菇：cross 形蘑菇，机制等价 MC 1.0 brown mushroom；蘑菇汤原料）。
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
        Bedrock       = 14, // 基岩：世界底层方块（t119）；机制等价 MC 基岩。solid=true 实体碰撞、
                           // **hardness=-1.0**（负值 → ToolRegistry::canMine 自动 false → 生存不可破：
                           // updateMining 内 if(canMine&&progress>=1.0) 守 finishMiningAt；t141 后创造可瞬破，
                           // beginMining 守卫已移除）、dropId=0（破不掉落）、各面同贴图（tile 18）。
                           // worldgen 在 y 0..4 按 hashVoxel 坑洼铺一层（底实顶疏）。
        // ── t134 不完整方块（异形几何段，id ∈ [FirstPartial, LastPartial]）：6 类木制半方块，机制等价 MC 1.0
        //   (id, metadata) 方块模型。solid=false（同 torch：非整立方 → 不挡邻居面剔除，避免相邻整立方
        //   被错误剔除出「洞」；逐形状精确碰撞留后续任务）。各面同贴图=planks(8)、hardness=2.0（木质）、
        //   NoTool（空手可采且掉落）。掉落自身。mesher 经 PartialBlockGeometry::append 按 (id,state) 生成
        //   异形顶点并合批进 chunk mesh。state 编码朝向 / 开合 / 半位（见各枚举注释 + partialblockgeometry.cpp）。
        WoodSlab          = 15, // 木板台阶：半高（上/下半）。state bit0 = 上半(1)/下半(0)。
        WoodStairs        = 16, // 木板楼梯：整步 + 背墙。state[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z（楼梯朝该向开）；bit2=上下倒置（整步在上、背墙在下）。
        WoodFence         = 17, // 木栅栏：中心立柱（0.4 见方 × 1.5 高）+ 四向横档连邻居（t209）。state=0
                                 //   （连接由 mesher 读水平邻居 id 运行期决定，非 state 编码；机制等价 MC栅栏）。
        WoodPressurePlate = 18, // 木板压力板：贴地薄板。state=0。
        WoodDoor          = 19, // 木板门：两格高（下/上格同 id）。state: bit3=上格(1)/下格(0)、bit2=开(1)/合(0)、
                                //   bit[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z。maxStack=1（单件，不可堆叠）。
        WoodTrapdoor      = 20, // 木板活板门：水平/竖直薄板。state bit0=开(1，竖直贴边)/合(0，水平贴地)，
                                //   bit[2:1]=开时朝向 0=+X 1=-X 2=+Z 3=-Z。
        Water              = 21, // 水（t148/t174）：机制等价 MC 1.0 流水。solid=false（不挡邻居面剔除 → 地形贴着水
                                  //   仍画自己的面）、shape=ShapeNone（**无碰撞** → 玩家穿过；t174 浮力/游泳走
                                  //   PlayerController 水中物理分支，非碰撞）、**hardness=-1.0**（不可挖掘：
                                  //   canMine=false，任何模式/工具不破，防创造秒破；同 bedrock 哨兵语义）、dropId=0
                                  //   （破不掉落）、各面同贴图=water(19) 蓝半透。
                                  //   worldgen 在 waterLevel 以下低洼列填水（h<wl 从 h+1 到 wl）—— 全作**水源**
                                  //   （state=0）。渲染：mesher 把水面剔出独立几何段、材质 opacity=0.7 半透；
                                  //   水-水邻接面互剔（nb==Water 剔除）。不进创造调色板（worldgen 专属；非玩家可放置
                                  //   方块 —— 玩家经铁桶舀/倒水交互，t174）。
                                  //   **t174 水流 state 编码**（复用 chunk m_states 并行数组；与不完整方块 state 同存储）：
                                  //     state 0 = 水源（无限；worldgen 填 / 玩家铁桶倒）
                                  //     state 1..7 = 流水（距水源的蔓延距离；MC 式扩散，最大 7 格水平距离）。
                                  //   World::tickWaterFlow()（t185 重做为增量波前）每 ~0.3s 把波前推进 1 格：水源/流水
                                  //   grounded（下方实体）→ 水平蔓延 state+1（≤7）；悬崖边（下方 air）→ 下落为流水 state=1
                                  //   （**非水源**）；流水失支撑（水源被舀/隔断）→ 逐环蒸发。1 格/tick 流动动画可见、不灌满
                                  //   整个平面。**t197 水位视觉**：state 同时驱动渲染——mesher 水段按 state 降水面高度
                                  //   （水源 1.0 / 流水 (8-level)/8 逐级降），流水(state>0) 各面用 water_flow(23) 贴图，
                                  //   水源(state=0) 用 water(19)。水面高度仅由 mesher 渲染层读取 state 计算，不进 BlockDef
                                  //   （本表方块 id=21 仍各面=19 静水；流水贴图是 mesher 呈现层选择，非方块属性）。
        Chest          = 22, // 箱子（t173）：机制等价 MC 1.0 箱子。solid=true / ShapeFull（整立方实体碰撞、
                                  //   mesher 正常画 6 面顶/侧/前贴图=chest_top/side/front，与工作台 / 熔炉同走整立方
                                  //   渲染路径 —— 非异形，**不**进 PartialBlockGeometry）、hardness=2.5（木制）、NoTool
                                  //   （空手可采且掉落自身）、各面贴图 chest_top(20)/chest_side(21)/chest_front(22)。
                                  //   右键打开 27 槽物品栏 UI（playercontroller 发 chestOpened 信号 → Main.qml 开
                                  //   ChestUI + 盖子开合动画）；**物品内容存 ChestStore（Game 层，按方块坐标键控）**，
                                  //   spec「物品存 chunk state」= 物品随方块存（世界级 block-instance state，非 QML
                                  //   面板本地态，机制等价 MC「箱子内容存于方块」；多箱子各自独立 27 槽）。破块时
                                  //   ChestStore.clearChest 清条目（内容不退回玩家背包，机制等价 MC 破箱掉落本属 1.1+）。
        Farmland       = 23, // 耕地（t234/t408）：机制等价 MC 1.0 耕地。持锄右键泥土/草方块→变耕地（playercontroller
                                  //   useBlock 分支：检手持为 Hoe 工具 + 命中格 Dirt/Grass → setBlock(Farmland, moist)）。
                                  //   **t408 矮盒渲染（露出 1/16 唇）+ solid=false**：mesher 经 PartialBlockGeometry 画 [0,0.9375]
                                  //   矮盒（顶=farmland_dry(26) / 侧·底=dirt(2)），机制等价 MC 耕地比整立方矮 1 像素。solid=false
                                  //   （同 glass 模式）→ 相邻整立方**不**因耕地剔面 → 画满高侧壁填住矮盒上方 1/16 缺口（solid=true
                                  //   会留透视 x-ray 洞）。shape 仍 ShapeFull（碰撞/选中/raycast 走整格，与渲染解耦；collisionAABBs
                                  //   特例 0.9375 不变）。光照仍满遮（lightOpacity 特例返 15，solid=false 不误降全透）。hardness=0.6
                                  //   （同 grass/dirt 量级，NoTool 空手可采）、dropId=Dirt（破耕地掉泥土，机制等价 MC
                                  //   「耕地破坏返泥土」，非掉耕地自身）、dropCount=1、maxStack=64。各面贴图：顶=farmland_dry(26)
                                  //   （mesher 据 state 低 2 位湿润等级暗化顶点色，见 FarmlandHydrationMask）/ 侧·底=dirt(2)。
                                  //   **碰撞略矮 0.9375（15/16）**：collisionAABBs 对 Farmland 特例返 {0,0,0,1,0.9375,1}
                                  //   （机制等价 MC 耕地碰撞箱矮 1 像素；selectionAABBs 仍走 ShapeFull 整格，选中框不缩，
                                  //   raycast 经 isFullCube=true 走整格命中 —— 三者解耦：碰撞矮、选中满、射线整格，零相互干扰）。
                                  //   state 低 2 位编码湿润等级 0..3（见 FarmlandHydrationMask），由 playercontroller 耕地时 +
                                  //   World::tickFarmlandHydration 周期复算据水源邻近距离写入。越湿顶面顶点色越暗（darker=wetter）。
        WheatCrop      = 25, // 小麦作物（t236）：机制等价 MC 1.0 小麦作物（wheat crop）。**cross 形广告牌方块**
                                  //   （与 TallGrass 同走 PartialBlockGeometry 的 cross 几何段 [FirstCross, LastCross]；
                                  //   两片对角相交的双面 quad，alpha 透明底 cutout）。**生长阶段存 chunk state**（state = 阶段
                                  //   0..7；0=刚种下的嫩芽、7=成熟可收割），WorldClock tick 推进成长（world.tickCropGrowth；
                                  //   每株据光强 + 耕地支撑 + 确定性散布概率逐步升阶段）。solid=false（非实体 → 不挡邻居面剔除，
                                  //   同 torch / 草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 作物可踩过 — 踩踏
                                  //   退化耕地留后续任务）、hardness=0（瞬破，同 torch / 草丛）、NoTool（空手可采且掉落）、
                                  //   dropId=0x208（小麦种子，材料段；Core 不依赖 Game 故字面量与 RecipeRegistry::SeedId 同源；
                                  //   **未成熟阶段破块返种子**——成熟阶段破块掉小麦物品 + 额外种子归 t237 收割按 state 判定，
                                  //   本表 dropId 仅作基础兜底）、dropCount=1、maxStack=64。**进创造调色板**（t244 补全：作物
                                  //   经种子种出、非玩家常规放置，但创造页供测试 / 装饰取用；图标取成熟阶段 7 金黄麦穗）。
                                  //   各面贴图=wheat_stage_<state>（tile 29..36，mesher 走 cross 几何段时据 state 选；本表
                                  //   topTile/sideTile 存阶段 0 基底 tile 29，partialblockgeometry 的 WheatCrop case 内 state +
                                  //   基底算出实际阶段贴图）。音色归 GroupGrass（软草音，同草丛）。
        TallGrass      = 24, // 草丛（t235）：机制等价 MC 1.0 草丛 / 蕨类（tall grass / fern）。**cross 形广告牌方块**
                                  //   （两片对角十字相交的 quad，billboard X 形贴图，alpha 透明底 cutout）—— 非 1×1×1 整立方，
                                  //   亦非段内异形方块段（id > LastPartial）。worldgen 在 grass 表层上方确定性散布（placeTallGrass），
                                  //   同 seed 同分布（PLAN §2-K）。solid=false（非实体 → 不挡邻居面剔除 → 相邻地形仍画自己的面；
                                  //   同 torch / 不完整方块语义）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 草丛可踩过）、
                                  //   hardness=0（瞬破，同 torch）、NoTool（空手可采且掉落）、dropId=小麦种子（材料段 0x208，
                                  //   RecipeRegistry::SeedId；Core 不依赖 Game 故用字面量 0x208）、dropCount=1、maxStack=64。
                                  //   各面贴图=tall_grass(28)（green 草叶 + alpha 透明底；mesher 走 cross 几何段，材质 alphaCutoff
                                  //   cutout 透明底）。音色归 GroupGrass（软草音）。**渲染走 PartialBlockGeometry::append 的 cross
                                  //   case**（合批进 chunk mesh，复用顶点色光照；cross 双面双对角 quad）；chunkgeometry 路由进 cross 段
                                  //   [FirstCross, LastCross]、不进立方面 PASS。raycastAABBs 整格命中（ShapeNone 非 torch 兜底）→
                                  //   可瞄准 / 破坏。**进创造调色板**（t244 补全：草丛由 worldgen 散布、非玩家常规放置，
                                  //   但创造页供测试 / 装饰取用，图标走 flat 2D 路径同火把）。
        DiamondOre     = 26, // 钻矿石（t279/t308）：散布于 stone **深层**（worldgen scatterOres 高度分层 y∈[5,40]，机制等价
                                  //   MC 1.0 钻石矿「越近基岩越富」，§2-K 确定性散布）。整立方 opaque（solid=true / ShapeFull
                                  //   —— 走 mesher 整立方面路径，**非**异形，与 coal/iron 矿石同族）、hardness=3.0（同 coal/iron
                                  //   量级，需镐）、toolType=Pickaxe、requiresTool=true、**minToolTier=3**（**需铁镐**才掉落 —— 木 / 石
                                  //   镐挖了不掉落，机制等价 MC 1.0 钻石矿需铁镐；t33 工具等级表 PickaxeIron tier=3）、dropId=0x212
                                  //   （钻石材料段，RecipeRegistry::DiamondId；Core 不依赖 Game 故用字面量 0x212）、dropCount=1、
                                  //   maxStack=64。各面贴图=diamond_ore(37)（石头底 + 青白菱斑晶体，原创自绘 §9a）。音色归 GroupStone
                                  //   （石质，同 coal/iron 矿石）。**洞穴裸露**：worldgen 顺序 scatterOres → carveCaves，carve 挖走
                                  //   stone/ore 暴露矿脉于洞壁（t278 既已铺路）→ 深层洞穴壁天然见钻矿石（spec「洞穴裸露矿物」）。
                                  //   进创造调色板（与 coal/iron 矿石同走立方体图标，t279 补全）。**t308 深度修正**：钻石上界
                                  //   y 16→40（用户 research 后定，深挖更易见；散布密度仍最低故整体稀有度不变）。
        CopperOre      = 29, // 铜矿石（t308）：机制等价 MC 1.0 铜矿（嵌于 stone 浅中层、需石镐采掘、掉铜原矿→熔炉烧铜锭）。
                                  //   整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 coal/iron/diamond
                                  //   矿石同族）、hardness=3.0（同族量级，需镐）、toolType=Pickaxe、requiresTool=true、**minToolTier=2**
                                  //   （**需石镐**才掉落 —— 木镐挖了不掉落，机制等价 MC 铜矿需石镐；同 iron 矿石门槛）、dropId=0x21C
                                  //   （**铜原矿**材料段，RecipeRegistry::CopperOreDropId；Core 不依赖 Game 故用字面量 0x21C ——
                                  //   掉落**原矿**非锭，机制等价 MC 1.0「铜/铁/金矿采下为原矿，须熔炉冶炼成锭」，区别于钻石矿直接掉钻石）、
                                  //   dropCount=1、maxStack=64。各面贴图=copper_ore(40)（石头底 + 橙铜斑 + 少量孔雀绿锈，原创自绘 §9a）。
                                  //   音色归 GroupStone（石质）。worldgen 高度分层散布于**浅中层** y∈[5,45]（金属族中最浅、最常见；
                                  //   spec「铜铁金按序更稀少」→ 铜最常见）。**洞穴裸露**：carveCaves 暴露矿脉于洞壁。进创造调色板。
        GoldOre        = 30, // 金矿石（t308）：机制等价 MC 1.0 金矿（嵌于 stone 深层、需铁镐采掘、掉金原矿→熔炉烧金锭）。整立方
                                  //   opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 coal/iron/diamond/copper
                                  //   矿石同族）、hardness=3.0（同族量级，需镐）、toolType=Pickaxe、requiresTool=true、**minToolTier=3**
                                  //   （**需铁镐**才掉落 —— 木 / 石镐挖了不掉落，机制等价 MC 金矿需铁镐；同 diamond 矿石门槛）、dropId=0x21E
                                  //   （**金原矿**材料段，RecipeRegistry::GoldOreDropId；Core 不依赖 Game 故用字面量 0x21E ——
                                  //   掉落**原矿**非锭，机制等价 MC 1.0「金矿采下为原矿，须熔炉冶炼成金锭」）、dropCount=1、maxStack=64。
                                  //   各面贴图=gold_ore(41)（石头底 + 金黄斑簇，原创自绘 §9a）。音色归 GroupStone（石质）。worldgen
                                  //   高度分层散布于**深层** y∈[5,25]（金属族中最深、最稀有；spec「铜铁金按序更稀少」→ 金最稀有）。
                                  //   **洞穴裸露**：carveCaves 暴露矿脉于洞壁。进创造调色板。
        Wool           = 27, // 羊毛方块（t300）：机制等价 MC 1.0 羊毛（wool）。整立方 opaque（solid=true / ShapeFull
                                  //   —— 走 mesher 整立方面路径，**非**异形）、hardness=0.8（同 MC 1.0 羊毛量级）、
                                  //   toolType=Shears（剪刀给速度加成；空手也掉落，requiresTool=false）、dropId=自身
                                  //   （破块掉羊毛方块，可放置）、dropCount=1、maxStack=64。各面贴图=wool(38)（奶白羊毛底
                                  //   + 浅灰卷曲绒毛纹，原创自绘 §9a）。**获得途径**：剪刀剪羊毛（EntityManager shearSheep →
                                  //   sheepSheared 信号 → 掉落羊毛方块）/ 杀羊掉落（mobDied → Main.qml 据本 id spawnItem）。
                                  //   音色归 GroupWood（软质闷击，最接近 MC 羊毛 cloth SoundType）。进创造调色板（t300 补全）。
        Sapling        = 28, // 树苗（t305）：机制等价 MC 1.0 橡树树苗（sapling）。**cross 形广告牌方块**（与 TallGrass /
                                  //   WheatCrop 同走 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。
                                  //   玩家持树苗物品（RecipeRegistry::SaplingItemId，材料段 0x21B）右键草地 / 泥土 → 在其上方一格
                                  //   种下树苗（playercontroller useBlock 分支，同种子种植模式）。WorldClock tick 推进成长
                                  //   （world.tickSaplingGrowth，机制等价 MC random-tick 生长）：树苗在草地 / 泥土支撑 + 头顶光照足 +
                                  //   主干列空气畅通时按确定性散布概率生长，长成后清除树苗 + 在原位生成一棵完整橡树（复用 worldgen
                                  //   placeTreeAt 主干 + 树叶球冠）。solid=false（非实体 → 不挡邻居面剔除，同 torch / 草丛）、
                                  //   shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 树苗可踩过）、hardness=0（瞬破，同 torch /
                                  //   草丛）、NoTool（空手可采且掉落）、dropId=SaplingItemId（破树苗掉树苗**物品**，材料段 0x21B —— 非掉
                                  //   树苗方块，机制等价 MC 破树苗掉树苗物品，玩家可回收再种）、dropCount=1、maxStack=64。各面贴图=
                                  //   sapling(39)（棕色短树干 + 绿色嫩叶小球冠，alpha 透明底；mesher 走 cross 几何段，材质 alphaCutoff
                                  //   cutout 透明底）。音色归 GroupGrass（软草音，同草丛 / 作物）。**树苗物品由树叶衰减 / 玩家破叶掉落**
                                  //   （playercontroller dropLeafDrops：破叶概率掉树苗物品 + 木棒）。**不**进方块创造调色板（树苗经
                                  //   物品种植、非玩家常规放置；创造调色板取树苗**物品**便于测试，见 creativeMaterials）。
        Lava           = 31, // 岩浆（t343）：机制等价 MC 1.0 岩浆（lava）——**比水慢得多**的流体（tickLavaFlow 节流 ~3s/格 vs 水 0.3s/格；
                                  //   更短扩散距离 kMaxLavaFlowLevel=3 vs 水 7；**无源再生**——MC 1.0 主世界岩浆不形成无限源）。
                                  //   solid=false（不挡邻居面剔除 → 相邻地形仍画自己的面；同 Water）、shape=ShapeNone（**无碰撞** → 玩家穿过；
                                  //   t344 着火扣血留后续）、**hardness=-1.0**（不可挖掘：canMine=false，任何模式/工具不破，防创造秒破；同
                                  //   bedrock / Water 哨兵语义）、dropId=0 不掉落、dropCount=0、maxStack=64（worldgen 专属，不进创造调色板 /
                                  //   不掉落 → maxStack 实不可达，填 64 与方块族一致）。各面贴图=lava(42)（深红橙底 + 亮黄橙鼓泡 + 白炽热点，
                                  //   原创自绘 §9a；纹理不透明，岩浆段材质 opacity≈0.95 近不透、NoLighting 暖色 baseColor 显自发光感）。
                                  //   **t343 岩浆 state 编码**（复用 chunk m_states 并行数组；与水 state 同存储语义）：
                                  //     state 0 = 岩浆源（worldgen 填 / 玩家铁桶倒；源被舀即消失，无再生）
                                  //     state 1..3 = 流岩浆（距源的蔓延距离；MC 式扩散，最大 3 格水平距离）。
                                  //   World::tickLavaFlow() 每 ~3s 把波前推进 1 格（机制等价 MC 主世界岩浆 ~30 倍水速差）：源 / 流 grounded
                                  //   （下方实体）→ 水平蔓延 state+1（≤3）；悬崖（下方 air）→ 下落为流岩浆 state=1；流岩浆失支撑 → 逐环凝固退场。
                                  //   worldgen placeLavaLakes 在 Y<30 封闭洞穴散布小型岩浆湖（圆盘源 + 上方气室，周围石壁封闭 → 稳态不蔓延）。
                                  //   **t343 交互**：(a) 铁桶舀 / 放（playercontroller 桶分支 + HitLava 射线）；(b) 木质方块邻岩浆概率着火焚毁
                                  //   （tickLavaFlow 末 ignite pass：Log/Planks/CraftingTable/Leaves 等木类 + 概率 setBlock Air）；(c) 掉落物丢入
                                  //   岩浆被摧毁（ItemEntityManager tick 检中心格 == Lava → releaseSlot）。不进创造调色板（worldgen / 桶交互获得）。
        // ── t387 床方块（bed）8 色变体：机制等价 MC 1.0 床（bed）。**t428 双格化**（原 t387 简化为单格整立方；
        //   t428 改为 head+foot 双格横置，如门但水平——玩家放置 foot 于命中面相邻格，head 自动落于 foot 的
        //   「玩家朝向反向」水平邻格；state 编码见 BlockRegistry::bedPartnerOffset）。每色一个方块 id（连续段
        //   [FirstBed, LastBed]）→ 创造调色板每个色变体独立取用 + 右键放置（复用既有 selectedBlockId → placeBlock
        //   通用放置路径，无需新交互；物品系统是 id 驱动，故「同 id 不同 state」的色变无法经背包表达 → 多 id）。
        //   低 3D 模型（t457：~0.3 格高 = 四角木柱腿 + 木板面 + 羊毛面，上方留空气可躺；solid=false /
        //   ShapeBed —— 走 PartialBlockGeometry 异形渲染路径，**非**整立方面；碰撞 = cell 底低盒 ~0.31 高，
        //   机制等价 MC 床矮半高 hitbox）、hardness=0.2（同 MC 1.0 床量级，软质）、toolType=Axe
        //   （木制床架；requiresTool=false → 空手也掉落，仅速度受斧影响）、dropId=自身（破床掉同色床方块，可放回）、
        //   dropCount=1、maxStack=64。各面贴图=default_bed_<color>（tile 43..50；彩色被面底 + 顶部枕垫亮带 +
        //   绗缝针脚暗点 + 边缘暗化，原创自绘 §9a）。音色归 GroupWood（软质闷击，同 wool / chest）。**配方**：
        //   planks + wool → 红床（BedRed，默认 / 最标志性色，recipe.cpp）；其余色变体创造调色板直接取用（本工程
        //   无染料系统，色变不经合成获得，机制对齐 MC 1.0「彩色羊毛 / 床需染料」但染料留后续任务）。**睡觉机制**
        //   （夜间右键床跳清晨 + 重生点）归 t388（isBed 谓词为其单一权威判定）。
        BedRed         = 32, // 红床：配方产物（planks+wool）；默认色。
        BedOrange      = 33, // 橙床
        BedYellow      = 34, // 黄床
        BedGreen       = 35, // 绿床
        BedCyan        = 36, // 青床
        BedBlue        = 37, // 蓝床
        BedMagenta     = 38, // 品红床
        BedBlack       = 39, // 黑床
        Spawner        = 40, // 刷怪笼（t392）：机制等价 MC 1.0 刷怪笼（mob spawner）。整立方 opaque
                                  //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 chest /
                                  //   wool 同族）、hardness=5.0（同 MC 1.0 刷怪笼量级，需镐且耗时）、toolType=Pickaxe、
                                  //   requiresTool=true、minToolTier=1（木镐可破）、**dropId=0**（破块不掉落 —— MC 1.0
                                  //   刷怪笼不可正常获得，仅创造 / 精准采集；本工程无精准采集故恒不掉落）、dropCount=0、
                                  //   maxStack=64。各面贴图=spawner(51)（暗蓝灰底 + 铁灰栅栏 + 中心青绿光斑，原创自绘 §9a）。
                                  //   音色归 GroupStone（铁笼金属敲击感，最接近 MC 1.0 刷怪笼 metal SoundType）。
                                  //   **worldgen 专属**（placeDungeons 在地下地牢中央放置），不进创造调色板（非玩家常规
                                  //   放置 —— 与水 / 岩浆同属「worldgen / 系统获得」语义）；玩家可破坏以**停止刷怪**
                                  //   （spec「spawner ... can be broken to stop」—— 破坏后 EntityManager::tickSpawners 扫到
                                  //   该格 blockAt != Spawner 即跳过，刷怪停止）。**刷怪 tick**：EntityManager::tickSpawners
                                  //   周期扫玩家周围 Spawner 块，玩家在 kSpawnerPlayerRange 内 + 该笼周 kSpawnerMobCheckRadius
                                  //   内敌对数 < kSpawnerLocalCap + 全局 hostileCount < kHostileMobCap 时，在笼旁合法空气格
                                  //   spawn 1 只敌对 mob（Shambler / Bones 等概率，机制等价 MC 1.0 刷怪笼周期刷怪 +
                                  //   玩家近才刷 + 数量上限）。玩家不在范围 / 笼被破 → 不刷（spec「player near 才刷」、
                                  //   「broken → stop」）。
        // ── t394 沙漠群系内容（机制等价 MC 1.0 沙漠三件套：sandstone / cactus / dead bush；名称 / 贴图全原创自绘 §9a）：
        Sandstone      = 41, // 砂岩：沙漠沙表层下的成岩石质层（worldgen 在 desert 沙下铺砂岩，区别于直接下接 Stone）。
                                  //   整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 chest /
                                  //   wool 同族）、hardness=0.8（同 MC 1.0 砂岩量级，需镐且耗时；同 cobble/stone 族）、
                                  //   toolType=Pickaxe、requiresTool=true、minToolTier=1（木镐可破，同 cobble/stone 门槛）、
                                  //   dropId=自身（破砂岩掉砂岩方块，可放置）、dropCount=1、maxStack=64。各面贴图：顶=
                                  //   sandstone_top(52) / 侧·底=sandstone_side(53)（暖沙色 + 横向层理带，原创自绘 §9a）。
                                  //   音色归 GroupStone（石质，同 cobble/stone）。进创造调色板（玩家可取用 / 放置）。
        Cactus         = 42, // 仙人掌：沙漠标志性植物方块（worldgen 在 desert 沙顶散布 1-3 格高柱；玩家可放置在沙 / 仙人掌
                                  //   上）。**t445 几何缩到 ~80% 居中**：mesher 经 PartialBlockGeometry 画 0.8×1.0×0.8 居中柱
                                  //   （X/Z [0.1,0.9] / Y 满高，机制对标 MC 1.0 仙人掌 14/16 细柱），非满格整立方。solid=false
                                  //   （同 Farmland / glass 模式：非满格 → 不挡邻居面剔除 → 下方沙顶画出填住柱底环隙；光照仍满遮，
                                  //   lightOpacity 特例返 15）、shape=ShapeFull（碰撞 / 选中仍走整格，与渲染解耦 → 实体碰撞 →
                                  //   mob/玩家撞其侧或站其上即「接触」）、hardness=0.4（同 MC 1.0 仙人掌量级，软质）、
                                  //   toolType=NoTool（空手即采且掉落，机制等价 MC 仙人掌无工具要求）、requiresTool=false、
                                  //   dropId=自身（破仙人掌掉仙人掌方块，可放回）、dropCount=1、maxStack=64。各面贴图：顶·底=
                                  //   cactus_top(54)（绿截面 + 同心方框环纹）/ 侧=cactus_side(55)（深绿底 + 4 垂直棱脊 + 棱上刺点，
                                  //   原创自绘 §9a）。音色归 GroupGrass（植物，软质）。**接触伤害**（spec「contact damages
                                  //   entities that touch it」）：EntityManager mob tick + PlayerController 环境 tick 检测实体
                                  //   脚位/身体格及其水平 4 邻 + 脚下格任一 == Cactus 即「接触」（**t445 ③ 全方位**：玩家侧改查
                                  //   footprint 格 + 水平 4 邻，覆盖撞侧面而非仅站顶）→ 每 kCactusDamageInterval(0.5s) 扣
                                  //   1HP（机制等价 MC 仙人掌触碰即伤）。**放置预检**（placeBlock）：目标格下方须为 Sand 或
                                  //   Cactus（机制等价 MC 仙人掌须沙地 / 仙人掌支撑）+ **水平 4 邻无方块**（t445 ④，机制等价 MC
                                  //   仙人掌不可邻接任何方块），否则拒放。**失撑 / 邻接方块即整柱转掉落物**（t445 ②/④）：World
                                  //   setBlock 破块时若正上方是仙人掌（且被破块非仙人掌本身）→ dropCactusColumn 递归向上把整柱
                                  //   转 Air + 发 blockDroppedAsItem（呈掉落物）；放块时若邻接仙人掌 → 同（立即破坏掉落）。
                                  //   进创造调色板。
        DeadBush       = 43, // 枯死的灌木：沙漠干旱地表的枯枝装饰（worldgen 在 desert 沙顶低密度散布）。**cross 形广告牌
                                  //   方块**（与 TallGrass / Sapling 同走 PartialBlockGeometry 的 cross 几何段，两片对角相交
                                  //   双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。机制等价 MC 1.0 dead bush（沙漠
                                  //   枯灌木，纯装饰 / 空手破无产物）。solid=false（非实体 → 不挡邻居面剔除，同 torch / 草丛）、
                                  //   shape=ShapeNone（**无碰撞** → 玩家穿过，同草丛）、hardness=0（瞬破）、NoTool（空手可采）、
                                  //   dropId=0（破枯灌木无掉落 —— 机制等价 MC 空手破 dead bush 无产物；本工程无剪刀剪取，故
                                  //   恒无掉落，创造调色板取用即得）、dropCount=0、maxStack=64。各面贴图=dead_bush(56)
                                  //   （透明底 + 棕褐放射干枝，原创自绘 §9a；mesher 走 cross 几何段 + alphaCutoff cutout）。
                                  //   音色归 GroupGrass（软草音，同草丛）。**放置预检**（placeBlock）：目标格下方须为 Sand
                                  //   （机制等价 MC 枯灌木生于沙地），否则拒放。进创造调色板（装饰取用）。
                                  //   **段外 cross**：DeadBush id(43) 不在 [FirstCross,LastCross]=[24,25] 连续段内（多方块夹
                                  //   中间且非 cross），故并入 isCrossBillboard 谓词（同 Sapling 模式 —— 单一权威，避免 mesher /
                                  //   选中框多处分流漂移），mesher 路由一律读谓词。
        // ── t395 雪原/针叶群系内容（机制等价 MC 1.0 寒冷群系三件套：snow / ice / spruce log；名称 / 贴图全原创自绘 §9a）：
        SnowLayer      = 44, // 积雪层：雪原/针叶群系地表覆盖（worldgen 在 Snowy 群系把草顶替换为积雪层）。
                                  //   **t505 改薄层**（机制等价 MC 1.0 snow layer 8 层）：贴地薄板，高度由 state 驱动
                                  //   （state 0..7 → 高度 (state+1)/8，1/8..1.0 八级；机制等价 MC 薄雪层可堆 8 层、
                                  //   玩家可踩 + 半格平滑 auto-step 上行）。solid=false / ShapeSnowLayer（走
                                  //   PartialBlockGeometry 薄板渲染，**非**整立方；同 Farmland / glass 模式 ——
                                  //   solid=false → 相邻整立方不剔面、画出满高侧壁填住薄层上方缺口，防透视 x-ray 洞）。
                                  //   collision/selection = cell 底薄板 {0,0,0,1,height,1}（玩家立于薄层顶 = cell+height；
                                  //   高度 ≤0.5 时玩家 t163 auto-step 抬升 0.55 即可跨过，无需跳）。
                                  //   hardness=0.2（同 MC 1.0 雪层量级，软质）、**toolType=Shovel + requiresTool=true +
                                  //   minToolTier=0**（铲挖掉雪球；空手挖不掉落 —— 机制等价 MC 1.0 雪层铲挖掉雪球、
                                  //   空手无掉落；minTier=0 → 任意等级铲均可采掘）、dropId=0x23D（雪球 SnowballId 材料段，
                                  //   t510）、dropCount=1（基础兜底，每层雪球数由 playercontroller 按 state+1 精确掉落，
                                  //   同 WheatCrop 按 state 掉落模式）、maxStack=64。各面贴图=snow(57)（冷白底 +
                                  //   细密冰晶噪点）。音色归 GroupSand（颗粒雪响）。进创造调色板（玩家可取用 / 放置）。
                                  //   **state 经 m_states 落 SQLite round-trip 保真**（存档读回仍带层数；旧存档雪层
                                  //   state=0 → 1/8 薄层，迁移自然）。worldgen 在 Snowy 群系草顶散布薄层（state 随机 0..2）。
        Ice            = 45, // 冰：雪原/针叶群系水面冻结产物（worldgen freezeSurfaceWater / tickIceFreeze 把 Snowy 群系
                                  //   暴露天空的水源冻结为冰）。**t468 改透明整立方**（机制等价 MC 1.0 半透冰）：solid=false /
                                  //   ShapeFull（同 glass 契约 —— solid=false → 相邻实体方块不剔面 → 透过半透冰可见背后方块；
                                  //   碰撞 / 选中仍走 ShapeFull 整格，可踩 / 实体碰撞；走 mesher 的 iceOnly 段 Blend 半透渲染，
                                  //   非旧 t395 不透明整立方）、hardness=0.5（同 MC 1.0 冰量级）、toolType=Pickaxe、requiresTool=true、
                                  //   minToolTier=1（木镐可破）、**dropId=0**（破冰不掉落 —— 机制等价 MC 1.0 冰需精准采集才掉落，
                                  //   本工程无精准采集故恒不掉落）、dropCount=0、maxStack=64。各面贴图=ice(58)（浅蓝底 + 反光
                                  //   裂纹，原创自绘 §9a）。音色归 GroupStone（玻璃质敲击，最接近 MC 1.0 冰 glass SoundType）。
                                  //   **冰上低摩擦**：玩家 / 掉落物在冰面上滑动速度衰减极慢（机制等价 MC 冰滑行手感）；滑动
                                  //   速度递增 Ice < PackIce < BlueIce（iceSlipFactor）。进创造调色板（t468：与 PackIce/BlueIce 一并
                                  //   取出测试 / 装饰；旧 t395「worldgen 系统获得不进调色板」语义由 PackIce/BlueIce 创造取用覆盖）。
        SpruceLog      = 46, // 云杉原木：雪原/针叶群系云杉树的主干（worldgen placeSpruceTreeAt 在 Snowy 群系种云杉变种树）。
        // ── t396 沼泽群系内容（机制等价 MC 1.0 沼泽植物：lily pad / mushroom；名称 / 贴图全原创自绘 §9a）：
        LilyPad        = 47, // 睡莲：沼泽浅水水面浮叶（worldgen placeSwampFlora 在沼泽水格上方一格散布）。
                                  //   **cross 路由的横向浮叶**（mesher 经 PartialBlockGeometry::append 的 LilyPad case 画一片
                                  //   水平双面 quad 贴 cell 底部 → 浮于水面；与 TallGrass 等竖直 cross 同走 isCrossBillboard
                                  //   路由 + alphaCutoff cutout 透明底，但几何为水平非竖直）。solid=false（非实体 → 不挡邻居
                                  //   面剔除，同 torch / 草丛）、shape=ShapeNone（selection 空 / 不进 heightmap / 不遮光 / 不挡
                                  //   邻居面剔除；raycast 整格命中可瞄准破），但 **t444 碰撞特例**：collisionAABBs 返 cell 底
                                  //   1/16 薄板（顶面 = 浮叶 quad 高度）→ 玩家立于睡莲顶面不掉进水（水上行走辅助；机制对标
                                  //   MC lily pad 可站立）。isCollidable 同步 true（与碰撞一致，保 hasGroundBelowAt 脚底支撑
                                  //   复探）。其余 ShapeNone 语义不变（torch/water 仍穿过）、hardness=0（瞬破，同草丛）、NoTool
                                  //   （空手可采且掉落）、dropId=自身
                                  //   （破睡莲掉睡莲方块，可放回）、dropCount=1、maxStack=64。各面贴图=lily_pad(61)
                                  //   （透明底 + 绿色圆叶 + V 形缺口，alphaCutoff cutout）。音色归 GroupGrass（软植物音）。
                                  //   进创造调色板（玩家可取用 / 放置）。
        Mushroom       = 48, // 蘑菇：沼泽草地小蘑菇（worldgen placeSwampFlora 在沼泽草地格上方一格低密度散布）。
                                  //   cross 形广告牌方块（与 Sapling / DeadBush 同走 cross 几何段，两片对角相交双面 quad，
                                  //   alpha 透明底 cutout）—— 非 1×1×1 整立方。机制等价 MC 1.0 蘑菇（沼泽 / 阴暗草地小蘑菇）。
                                  //   solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过）、
                                  //   hardness=0（瞬破）、NoTool（空手可采且掉落）、dropId=自身（破蘑菇掉蘑菇方块，可放回）、
                                  //   dropCount=1、maxStack=64。各面贴图=mushroom(62)（透明底 + 米色菌柄 + 红底白斑菌盖，
                                  //   alphaCutoff cutout）。音色归 GroupGrass（软植物音）。进创造调色板（装饰取用）。
        // ── t397 多群系装饰植物（机制等价 MC 1.0 花 / 甘蔗；名称 / 贴图全原创自绘 §9a）：
        //   花（Flower）4 色变体：每色一个方块 id（连续段 [FirstFlower, LastFlower]）→ 创造调色板每色独立取用 +
        //   右键放置（复用既有 selectedBlockId → placeBlock 通用放置路径，预检须草地 / 泥土支撑）。cross 形广告牌方块
        //   （与 TallGrass / Sapling 同走 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方，
        //   「thin like tall grass」（spec 原话）。机制等价 MC 1.0 花（poppy / dandelion 等），名称 / 贴图全原创自绘。
        //   solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 花可踩过）、
        //   hardness=0（瞬破，同草丛 / 火把）、NoTool（空手可采且掉落）、dropId=自身（破花掉同色花方块，可放回）、
        //   dropCount=1、maxStack=64。各面贴图=flower_<color>（tile 63..66；透明底 + 茎 + 花头，alphaCutoff cutout）。
        //   音色归 GroupGrass（软植物音，同草丛 / 蘑菇）。worldgen placeFlowers 在各群系草地低密度散布（plains 多彩 /
        //   forest 少量 / swamp 适量 / hills 稀疏；机制等价 MC 各群系花点缀）。进创造调色板（每色独立取用）。
        FlowerRed      = 49, // 红花（机制等价 MC 罂粟 poppy，标志性色）
        FlowerYellow   = 50, // 黄花（机制等价 MC 蒲公英 dandelion）
        FlowerBlue     = 51, // 蓝花（机制等价 MC 矢车菊 cornflower；原创配色）
        FlowerWhite    = 52, // 白花（机制等价 MC 雏菊 oxeye daisy）
        Sugarcane      = 53, // 甘蔗（机制等价 MC 1.0 sugar cane / reeds）：水边生长的可叠高细茎植物。**cross 形广告牌
                                  //   方块**（与花 / 草丛同走 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）—— 非
                                  //   1×1×1 整立方，呈细茎观感（机制对标 MC 甘蔗「细于整立方」）。worldgen placeSugarcane 在水域
                                  //   邻接的草地 / 沙地旁确定性散布 1..3 格高柱（同 cactus 1-3 高模式；spec「grows up to 3 tall
                                  //   at waters edges」），每格仅写空气格 → 不覆盖已生成的方块。solid=false（非实体 → 不挡邻居面
                                  //   剔除，同草丛 / 花）、shape=ShapeNone（**无碰撞** → 玩家穿过）、hardness=0（瞬破）、NoTool
                                  //   （空手可采且掉落）、dropId=自身（破甘蔗掉甘蔗方块，可放回 / 可重种）、dropCount=1、maxStack=64。
                                  //   各面贴图=sugarcane(67)（透明底 + 绿色节段细茎 + 顶部尖叶，alphaCutoff cutout）。音色归
                                  //   GroupGrass（软植物音）。**放置预检**（placeBlock）：目标格下方须为 Grass / Dirt / Sand /
                                  //   Sugarcane（机制等价 MC 甘蔗须草地 / 沙地 / 甘蔗支撑，且须邻水 —— 邻水判定留 worldgen，玩家
                                  //   放置仅守支撑，机制等价 MC 创造放置不强制邻水）。进创造调色板（玩家可取用 / 放置）。
        // ── t405 玻璃方块（机制等价 MC 1.0 玻璃 glass）：沙子熔炉冶炼产物（SmeltingRegistry 沙子→玻璃物品 0x204；
        //   玩家持玻璃物品右键放置 → 玻璃方块）。**透明整立方**——本任务核心：玻璃须真正**透视**（透过玻璃可见背后
        //   的方块 / 实体），机制等价 MC 1.0 玻璃。
        //   solid=false（**关键**：solid 仅作 mesher 邻居面剔除依据，见 t146 注。solid=false → 相邻实体方块**不**因玻璃
        //   而剔面 → 石头 / 地形贴着玻璃仍画自己的面 → 透过半透玻璃可见背后的方块（修「玻璃身后方块被剔面 → 透视见底 /
        //   x-ray 空壳」）。碰撞 / 选中 / 射线阻挡不读 solid（走 shape / raycastAABBs / blockAt!=0），故 glass 仍可踩 /
        //   可瞄准 / 可破）。shape=ShapeFull（整立方实体碰撞 + 选中框；与 ice / sandstone 同走整格，**非**异形——不进
        //   PartialBlockGeometry）、hardness=0.3（同 MC 1.0 玻璃量级，薄脆）、toolType=Pickaxe（玻璃采掘归石族）、
        //   requiresTool=false（空手可破且掉落——本工程无精准采集，玻璃可回收，便于沙子→玻璃→重放闭环）、dropId=0x204
        //   （破玻璃掉玻璃**物品** RecipeRegistry::GlassId，材料段；Core 不依赖 Game 故用字面量 0x204，同 TallGrass 用
        //   0x208 模式）、dropCount=1、maxStack=64。各面贴图=glass(68)（近白青底 + 暗边框 + 对角高光斜线，原创自绘 §9a；
        //   透明感由 glassOnly 段材质 opacity≈0.45 实现，纹理本身不透明——同 water 模式：纹理不透 + 材质半透）。
        //   音色归 GroupStone（玻璃质敲击，最接近 MC 1.0 玻璃 glass SoundType，同 ice）。**渲染**：mesher 路由进
        //   ChunkGeometry 的 glassOnly 段（独立半透材质 opacity:0.45 + NoLighting + 顶点色光照，机制等价 waterOnly /
        //   lavaOnly 的透明分流）；地形段跳过 Glass（避免与玻璃段重复绘制 + 被当不透明地形）。glassOnly 段面剔除：
        //   邻实体剔（避免与实体面共面 z-fight）、邻 Glass 剔（玻璃-玻璃共面不重复绘制）、邻空气画（半透面，透视关键）。
        //   lightOpacity=0（玻璃透光——机制等价 MC 玻璃 lightOpacity 0；solid=false 已致全透，玻璃与其它 solid=false
        //   方块同）。进创造调色板经玻璃**物品**（0x204，creativeMaterials，MaterialIcon drawGlass 图标），非方块段。
        Glass          = 54, // 玻璃：沙子冶炼产物方块；透明整立方（solid=false + glassOnly 段半透渲染）。
        // ── t407 胡萝卜/马铃薯作物（crop）：机制等价 MC 1.0 carrot/potato 作物。**cross 形广告牌方块**（与 WheatCrop
        //   同走 PartialBlockGeometry 的 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。
        //   MC 1.0 carrot/potato 作物与小麦同走「8 生长年龄、age 7 成熟」机制，故**复用 WheatCropStageMax=7** 作共享
        //   阶段上界（state = 阶段 0..7；7 = 成熟可收割，机制等价 MC age 0..7）。世界由 World::tickCropGrowth 推进成长
        //   （同小麦：据光强 + 耕地支撑 + 湿润 + 确定性散布概率逐窗升阶段）。**种植**：手持胡萝卜/马铃薯物品
        //   （RecipeRegistry::CarrotId/PotatoId，t400 已注册作猪繁殖食物）右键耕地 → 在其上方一格种下本作物方块
        //   （playercontroller useBlock 分支，同种子种小麦模式）。**收割**：破成熟作物掉 1-4 个对应物品（MC 1.0 成熟
        //   作物掉 1-4，机制对齐；未成熟掉 1 个）。solid=false（非实体 → 不挡邻居面剔除，同 torch / 草丛 / 小麦）、
        //   shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 作物可踩过）、hardness=0（瞬破，同小麦）、NoTool（空手
        //   可采且掉落）、dropCount=1、maxStack=64。音色归 GroupGrass（软草音，同小麦作物）。**作物方块不进创造调色板**
        //   （同树苗：由对应物品种植获得，创造调色板取物品即可，t400 已补 creativeMaterials）。
        //   各面贴图=carrot_crop_0..3(69..72) / potato_crop_0..3(73..76)（MC 1.0 carrot/potato 4 张阶段贴图，每张覆盖
        //   2 个年龄：age 0-1→tex0、2-3→tex1、4-5→tex2、6-7→tex3；mesher 在 cross 几何段据 state 选 tile = 基底 + state/2，
        //   区别于小麦的基底 + state 全 8 阶段贴图）。方块 def topTile/sideTile 存基底阶段 0 tile（69/73），几何段算实际。
        CarrotCrop     = 55, // 胡萝卜作物：cross 形作物方块（机制等价 MC 1.0 carrot crop）；dropId=CarrotId(0x22F)
        PotatoCrop     = 56, // 马铃薯作物：cross 形作物方块（机制等价 MC 1.0 potato crop）；dropId=PotatoId(0x230)
        // ── t411 黑曜石（Obsidian）：机制等价 MC 1.0 obsidian（流体交互凝固产物——见 World::tickWaterFlow /
        //   tickLavaFlow 流体交互 pass：流水触静岩浆源 / 静水源触静岩浆源 → 本方块）。整立方 opaque（solid=true /
        //   ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 stone/cobble/sandstone 同族）、hardness=50.0
        //   （同 MC 1.0 obsidian 真值，极硬极耐挖）、toolType=Pickaxe（石族）、requiresTool=true、minToolTier=4
        //   （**t472 需钻石镐**：tier<4 的镐 / 空手慢挖且不掉落，仅钻石镐 tier 4 给速度加成且掉落）、dropId=自身
        //   （破黑曜石掉黑曜石方块，可放置）、dropCount=1、maxStack=64。各面贴图=obsidian(77)（深紫黑火山玻璃底 +
        //   紫红纹理嵌点 + 少量品紫玻璃微反光，原创自绘 §9a）。音色归 GroupStone（石质）。worldgen 不直接生成（仅由
        //   流体交互产生），不进创造调色板（系统获得语义，同 ice）。**抗爆**：destroySphereSilent 跳过本方块（机制
        //   等价 MC obsidian 爆炸抗性 6000，免疫 Stalker/TNT 爆炸）。
        Obsidian       = 57, // 黑曜石：流体交互凝固产物（流水/水源触静岩浆源 → 本方块；t411/t472）
        // ── t412 圆石变体（cobble variants）：机制等价 MC 1.0 石质半方块（cobblestone slab/stairs/wall/pressure-plate）。
        //   复用既有异形方块系统（PartialBlockGeometry 几何 + ShapeSlab/ShapeStairs/ShapeFence/ShapePlate 子 AABB），
        //   仅换圆石贴图（tile 5，各面同）与石质属性（hardness 2.0、Pickaxe、requiresTool=true、minTier1、GroupStone）。
        //   id 段外（不与 [FirstPartial,LastPartial]=15..20 相邻 —— 中间夹大量非异形方块），故经 isPartialBlock /
        //   isSlab / isStairs / isFence / isPressurePlate 谓词并入异形路由（单一权威，同 isCrossBillboard 段外 cross 模式）。
        CobbleSlab          = 58, // 圆石台阶：半高（上/下半）。state bit0=上半(1)/下半(0)（与 WoodSlab 同编码）。
        CobbleStairs        = 59, // 圆石楼梯：整步 + 背墙。state[1:0]=朝向 bit2=倒置（与 WoodStairs 同编码）。
        CobbleFence         = 60, // 圆石墙：中心立柱 + 四向横档连邻居（机制等价 MC 圆石墙；与 WoodFence 同几何）。
        CobblePressurePlate = 61, // 圆石压力板：贴地薄板（与 WoodPressurePlate 同几何）。
        // ── t413 / t501 垂直爬梯（vertical climb ladder）：机制等价 MC 1.0 梯子（ladder）。既有 WoodStairs 是
        //   台阶式楼梯（逐级步行上行），本方块是**竖直爬行**梯——玩家走进梯格 + 按前即逐格**向上爬**（竖井用）。
        //   t501 放置改贴**完整立方方块的侧面**（机制等价 MC 梯子须贴实体方块面）：placeBlock 时命中面须为完整
        //   立方（isFullCube）方块的**侧面**（顶/底面拒；草丛/门/活版门/栅栏等不完整方块的侧亦拒），木梯贴该面；
        //   state[1:0] 编码所贴墙面水平方向（0=+X 1=-X 2=+Z 3=-Z，与 horizontalFacing/chest 同源）。几何由 t413
        //   的两片对角 cross 改为**单片贴墙 quad**（贴所贴面、贴图朝外朝玩家侧，PartialBlockGeometry Ladder case
        //   据 state 摆位）。支撑墙被破 → finishMiningAt dropUnsupportedLaddersAround 据解 state 失撑掉落（同火把）。
        //   solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone（**无碰撞** → 玩家穿入梯格；爬升由
        //   PlayerController 检测「玩家 AABB 覆盖的梯格」+ 按前覆写垂直速度实现，非碰撞）、hardness=0.4（同 MC 1.0 梯子量级，
        //   软质木质）、toolType=Axe（木质梯；requiresTool=false → 空手也掉落，仅速度受斧影响）、dropId=自身（破梯掉梯，
        //   可放回）、dropCount=1、maxStack=64。各面贴图=ladder(78)（透明底 + 棕色两根纵轨 + 横向梯级，alphaCutoff cutout）。
        //   音色归 GroupWood（木质，同 planks 族）。进创造调色板（玩家可取用 / 放置）。
        Ladder          = 62, // 木梯：贴墙竖直爬行梯（玩家入格 + 按前向上爬；贴完整方块侧 + state 编码贴墙方向）
        // ── t455 16 色 wool 其余 15 色变体（white 复用既有 Wool=27；本段为 orange..black，id 63..77）。
        //   机制等价 MC 1.0 羊毛 16 色变体。每色一个方块 id（物品系统是 id 驱动 → 同 id 不同 state 无法经背包
        //   表达色变 → 多 id）。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，
        //   与 white Wool / chest 同族）、hardness=0.8、toolType=Shears（requiresTool=false 空手也掉落）、dropId=自身
        //   （破块掉同色羊毛方块，可放置）、dropCount=1、maxStack=64。各面贴图=default_wool_<color>（tile 79..93；
        //   卷绒纹 + 标准 16 色着色，原创自绘 §9a）。音色归 GroupWood（同 white Wool）。**获得途径**：创造调色板
        //   直接取用（本工程无染料系统，机制对齐 MC「彩色羊毛需染料」但染料留后续任务）；白色羊毛仍由剪 / 杀羊获得。
        //   进创造调色板（每色独立取用 + 右键放置）。isWool(id) 单一权威谓词供未来染料 / 配方判定。
        WoolOrange     = 63, // 橙色羊毛
        WoolMagenta    = 64, // 品红色羊毛
        WoolLightBlue  = 65, // 浅蓝色羊毛
        WoolYellow     = 66, // 黄色羊毛
        WoolLime       = 67, // 柠绿色羊毛
        WoolPink       = 68, // 粉红色羊毛
        WoolGray       = 69, // 灰色羊毛
        WoolLightGray  = 70, // 浅灰色羊毛
        WoolCyan       = 71, // 青色羊毛
        WoolPurple     = 72, // 紫色羊毛
        WoolBlue       = 73, // 蓝色羊毛
        WoolBrown      = 74, // 棕色羊毛
        WoolGreen      = 75, // 绿色羊毛
        WoolRed        = 76, // 红色羊毛
        WoolBlack      = 77, // 黑色羊毛
        // ── t455 16 色床补齐 8 色新变体（既存 8 色床 id 32..39 不动；本段为 white/light_blue/lime/pink/gray/
        //   light_gray/purple/brown，id 78..85）。机制等价 MC 1.0 床 16 色变体。每色一个方块 id（同 wool 多 id 模式）。
        //   整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与既存床同族）、hardness=0.2、
        //   toolType=Axe（requiresTool=false 空手也掉落）、dropId=自身（破床掉同色床方块，可放回）、dropCount=1、
        //   maxStack=64。各面贴图=default_bed_<color>（tile 94..101；与同色羊毛同色板一致，原创自绘 §9a）。
        //   音色归 GroupWood（同既存床）。**配方**：3 同色羊毛 + 3 木板 → 该色床（recipe.cpp 每色一条）。
        //   睡觉机制（t388）经 isBed 谓词覆盖本段（同既存 8 色床）。进创造调色板（每色独立取用 + 右键放置）。
        BedWhite       = 78, // 白色床（配方：3 白色羊毛 + 3 木板）
        BedLightBlue   = 79, // 浅蓝色床
        BedLime        = 80, // 柠绿色床
        BedPink        = 81, // 粉红色床
        BedGray        = 82, // 灰色床
        BedLightGray   = 83, // 浅灰色床
        BedPurple      = 84, // 紫色床
        BedBrown       = 85, // 棕色床
        // ── t466 云杉木制品链（机制等价 MC 1.0 spruce 木制品；名称 / 贴图全原创自绘 §9a）：
        //   云杉木板 / 云杉木制品复用既有木制品机制（solid/shape/碰撞/朝向解码/门双格），仅换 id+贴图（深色
        //   木纹 spruce_planks 区别于橡木 planks）。云杉原木 SpruceLog(46) 已有（t395），本段是其延伸制品。
        //   tile 全部 = spruce_planks(102)（同橡木 WoodSlab/Fence/Door 复用 planks(8) 的「一族木制品共享一贴图」
        //   模式；区别仅深色木纹）。
        SprucePlanks   = 86, // 云杉木板：独立方块 + 深色木纹贴图（区别橡木 OakPlanks）。整立方 opaque
                                  //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，与 Planks 同族）；
                                  //   hardness=2.0、toolType=Axe（requiresTool=false 空手也掉落）、dropId=自身、
                                  //   dropCount=1、maxStack=64。配方：1 云杉原木 → 4 云杉木板（同橡木原木→橡木木板）。
                                  //   state 复用 bit0 作双半砖合并 marker（DoubleSlabMarkerBit，见 blockregistry.cpp
                                  //   fullBlockSlabDrop：SprucePlanks → SpruceSlab），机制同 Planks→WoodSlab。
        SpruceSlab     = 87, // 云杉台阶：半高（state bit0=上半(1)/下半(0)；与 WoodSlab 同编码）。solid=false
                                  //   / ShapeSlab（走 PartialBlockGeometry 异形渲染，与 WoodSlab 同几何）；hardness=2.0、
                                  //   Axe、dropId=自身、dropCount=1、maxStack=64。配方：3 云杉木板横排 → 6 云杉台阶。
        SpruceFence    = 88, // 云杉栅栏：中心立柱 + 四向横档连邻居（与 WoodFence 同几何；连接由 mesher 读水平
                                  //   邻居 id 运行期决定）。solid=false / ShapeFence；hardness=2.0、Axe、dropId=自身、
                                  //   dropCount=1、maxStack=64。配方：云杉木板-棒-云杉木板 ×2 行 → 3 云杉栅栏。
        SpruceDoor     = 89, // 云杉门：两格高（下/上格同 id）。state bit3=上格(1)/下格(0)、bit2=开(1)/合(0)、
                                  //   bit[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z（与 WoodDoor 同编码；门双格放置/破坏联动/右键
                                  //   开合经 isDoor 谓词统一处理）。maxStack=1（单件，不可堆叠）。solid=false / ShapeDoor；
                                  //   hardness=2.0、Axe、dropId=自身、dropCount=1。配方：3 云杉木板纵列 → 1 云杉门。
        // ── t467 雪原浆果灌木丛（SweetBerryBush）：机制等价 MC 1.0 甜浆果丛（sweet berry bush）——雪原 Snowy 群系
        //   散布的可采摘灌木。**cross 形广告牌方块**（与 TallGrass / Sapling / 作物同走 PartialBlockGeometry 的 cross
        //   几何段，两片对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。**3 生长阶段存 chunk state**
        //   （state = 阶段 0..SweetBerryBushStageMax；0=无果嫩丛、1=小果、2=成熟可采摘），worldgen placeSweetBerryBushes
        //   散布阶段 1..2 的丛、玩家右键成熟丛采摘（playercontroller useBlock 分支）。solid=false（非实体 → 不挡邻居面
        //   剔除，同草丛 / 作物）、shape=ShapeNone（**无碰撞** → 玩家穿过；机制等价 MC 浆果丛可踩过，stage>0 踩过受少量
        //   伤害归 playercontroller 环境伤害 tick）、hardness=0（瞬破，同草丛 / 作物）、NoTool（空手可采且掉落）、
        //   dropId=0x233（甜浆果**物品**，材料段 RecipeRegistry::SweetBerryId；Core 不依赖 Game 故字面量 0x233 —— 破丛掉
        //   浆果物品非丛方块，机制等价 MC 破浆果丛掉浆果）、dropCount=1、maxStack=64。各面贴图=sweet_berry_bush_<state>
        //   （tile 103..105；mesher 走 cross 几何段时据 state 选 stage 0/1/2 贴图；本表 topTile/sideTile 存阶段 0 基底
        //   tile 103，partialblockgeometry 的 SweetBerryBush case 内 state + 基底算实际阶段贴图，同 WheatCrop 模式）。
        //   音色归 GroupGrass（软植物音，同草丛 / 蘑菇）。worldgen 在 Snowy 群系 SnowLayer 地表上方低密度散布（机制
        //   等价 MC 寒冷群系浆果丛）。进创造调色板（玩家可取用 / 放置，便于测试 / 装饰）。
        SweetBerryBush = 90, // 雪原浆果灌木丛：cross 形灌木（机制等价 MC 1.0 sweet berry bush）；3 阶段、右键采摘
        // ── t468 冰的物理（机制等价 MC 1.0 ice / packed ice / blue ice：结冰 + 冰上低摩擦滑动）：
        //   浮冰（PackIce）/ 蓝冰（BlueIce）是 Ice 的更滑变种（冰上滑动速度递增 Ice < PackIce < BlueIce）。
        //   与 Ice(45) 同属「冰族」——透明整立方（solid=false + ShapeFull，走 iceOnly 段 Blend 半透渲染，
        //   同 glass 契约；碰撞仍整格可踩）、hardness=0.5（同 Ice）、toolType=Pickaxe、requiresTool=true、
        //   minTier1（木镐可破）、dropId=0（破冰不掉落，机制等价 MC 冰需精准采集）、dropCount=0、maxStack=64。
        //   各面同贴图：PackIce=pack_ice(106)（实白 + 细裂纹，比 Ice 更密实）/ BlueIce=blue_ice(107)（淡蓝 +
        //   纵向纹路，最滑）。音色归 GroupStone（玻璃质，同 Ice）。worldgen 不直接生成（系统获得：Ice 在雪原
        //   worldgen 冻结 / 玩家合成 PackIce/BlueIce 留后续）；进创造调色板供测试 / 装饰（与 Ice 一并取出）。
        //   isIce(id) 单一权威谓词覆盖三者（Ice/PackIce/BlueIce），t469 船会复用判冰面加船速。
        PackIce        = 91, // 浮冰：Ice 的更滑变种（机制等价 MC 1.0 packed ice）；滑动速度中等
        BlueIce        = 92, // 蓝冰：最滑冰变种（机制等价 MC 1.0 blue ice）；滑动速度最快
        // ── t471 青金矿石（LapisOre）：机制等价 MC 1.0 青金石矿（lapis lazuli ore）—— 嵌于 stone 深层、需石镐
        //   采掘、掉青金石物品（附魔台每次附魔消耗 1-3 个 + 经验等级，附魔前置材料）。整立方 opaque
        //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 coal/iron/diamond/copper/gold
        //   矿石同族）、hardness=3.0（同族量级，需镐）、toolType=Pickaxe、requiresTool=true、minToolTier=2
        //   （**需石镐**才掉落 —— 木镐挖了不掉落，机制等价 MC 1.0 青金矿需石镐；同 iron/copper 矿石门槛）、
        //   dropId=0x236（青金石物品，材料段 RecipeRegistry::LapisId；Core 不依赖 Game 故用字面量 0x236 ——
        //   机制等价 MC 1.0「青金矿采下直接掉青金石物品」（多枚），区别于铁/铜/金矿掉原矿须冶炼）、dropCount=1、
        //   maxStack=64。各面贴图=lapis_ore(108)（石头底 + 群青深蓝斑簇 + 黄铁矿金点，原创自绘 §9a；真实青金石
        //   矿物即「深蓝 lazurite 底 + 黄铁矿 pyrite 金点」）。音色归 GroupStone（石质，同 coal/iron 矿石）。
        //   worldgen 高度分层散布于深层 y∈[5,31]（机制等价 MC 1.0 青金矿 Y<32 浅深层富集）。**洞穴裸露**：
        //   carveCaves 暴露矿脉于洞壁（同其它矿石）。**进创造调色板**（与 coal/iron 矿石同走立方体图标）。
        LapisOre       = 93, // 青金矿石：散布于 stone 深层 y∈[5,31]；需石镐采掘；掉青金石物品（t471 附魔前置材料）
        // ── t474 附魔链两件方块（机制等价 MC 1.0 enchanting table / bookshelf；名称 / 贴图全原创自绘 §9a）：
        //   附魔台（EnchantingTable）：右键打开附魔 UI（playercontroller useBlock 分支发 enchantingTableOpened
        //   信号 → Main.qml 显 EnchantingTableUI：3 附魔选项预览槽，每槽消耗 XP 等级 1/2/3 + 对应青金石 1/2/3；
        //   周围书架数（≤15，2 格切比雪夫半径内）提升可选附魔等级上限，机制等价 MC 1.0 书架加成）。
        //   **t620 改 0.75 高矮盒**（机制等价 MC 1.0 附魔台 12/16 高非整块；此前简化为整立方，pack 贴图
        //   接入时改半高）：mesher 走 PartialBlockGeometry 画 [0,0.75] 矮盒；solid=false（同 Farmland 模式
        //   —— 不挡邻居面剔除防 x-ray 洞，光照仍满遮）；碰撞矮盒 0.75 / selection·raycast 仍整格
        //   （ShapeFull，三者解耦同 Farmland）、hardness=5.0（同 MC 1.0 附魔台量级，石质偏硬）、toolType=Pickaxe、
        //   requiresTool=true、minToolTier=1（木镐可破且掉落）、dropId=自身（破块掉附魔台方块，可放回）、
        //   dropCount=1、maxStack=64。各面贴图：顶=enchanting_table_top(109)（黑曜石底+钻石纹+顶部立书轮廓）/
        //   底=obsidian(77)（demo 包 enchanting_table_bottom.png 与 obsidian.png 逐像素相同，复用瓦片）/
        //   侧·前=enchanting_table_side(110)（黑曜石底+钻石嵌点+边缘暗化；pack 合成裁掉顶部 0.25 空白）。
        //   音色归 GroupStone（石质）。
        //   配方：1 书 + 2 钻石 + 4 黑曜石 → 1 附魔台（工作台 3×3 有序，recipe.cpp）。进创造调色板。
        EnchantingTable = 94, // 附魔台：右键开附魔 UI（3 选项槽，消耗 XP 等级 + 青金石）；书架加成；配方书+钻石+黑曜石
        //   书架（Bookshelf）：纯装饰合成产物（机制等价 MC 1.0 bookshelf —— 仅作为附魔台加成来源；本工程无
        //   「书架可放书」物品栏，纯合成 / 放置方块）。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立面
        //   面路径，**非**异形，与 chest / wool 同族）、hardness=1.5（同 MC 1.0 书架量级，木质偏软）、toolType=Axe
        //   （木制；requiresTool=false → 空手也掉落，仅斧给速度加成）、dropId=自身（破书架掉书架方块，可放回 ——
        //   机制简化：MC 1.0 破书架掉**书**物品，本工程掉书架方块以便玩家回收重放，区别于 MC 留记录）、
        //   dropCount=1、maxStack=64。各面贴图：顶·底=bookshelf(111)（木板边框 + 中央书脊彩色书列）/ 侧·前=
        //   bookshelf(111)（同顶；机制等价 MC 书架各面书脊纹，本工程六面同贴图简化）。音色归 GroupWood（木质）。
        //   配方：6 木板 + 3 书 → 1 书架（工作台 3×3 有序：上 / 下两行木板、中间一行 3 书，recipe.cpp）。
        //   进创造调色板（玩家可取用 / 放置；附魔台加成测试用）。isBookshelf 单一权威谓词供 World::countBookshelvesAround
        //   （附魔台加成）判定「是否书架」，避免各处硬编码 Bookshelf id 判定漂移（同 isBed / isLadder 模式）。
        Bookshelf       = 95, // 书架：合成产物（6 木板 + 3 书）；附魔台加成来源（2 格内计数 ≤15）；木质整立方
        // ── t477 铁块 + 铁砧（机制等价 MC 1.0 iron block / anvil；名称 / 贴图全原创自绘 §9a）：
        //   铁块（IronBlock）：**9 铁锭合成**的金属存储方块（机制等价 MC 1.0 iron block；3 铁块 + 4 铁锭合成铁砧
        //   的前置）。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 obsidian /
        //   wool 同族）、hardness=5.0（同 MC 1.0 铁块量级，金属偏硬）、toolType=Pickaxe、requiresTool=true、
        //   minTier1（木镐可破且掉落）、dropId=自身、dropCount=1、maxStack=64。各面贴图=iron_block(112)
        //   （金属灰底 + 细密铆钉网格 + 高光，原创自绘 §9a）。音色归 GroupStone（金属质，同 obsidian 族）。
        //   配方：9 铁锭 3×3 满铺 → 1 铁块（recipe.cpp）。进创造调色板（玩家可取用 / 放置）。
        IronBlock       = 96, // 铁块：9 铁锭合成存储方块；铁砧配方前置（3 铁块 + 4 铁锭 → 铁砧）
        //   铁砧（Anvil）：右键开铁砧界面（playercontroller useBlock 分支发 anvilOpened(x,y,z) → Main.qml 显
        //   AnvilUI：修复 / 附魔合并 / 重命名三功能，各消耗 XP 等级）。机制等价 MC 1.0 铁砧。**铁砧自身耐久**：
        //   3 个损坏阶段（完好 / 微损 / 重损），每次成功操作有概率损坏 +1；重损态再损坏 → 方块碎裂移除
        //   （playercontroller damageAnvil 滚概率，据当前阶段 → 写下一阶段 id 或 setBlock Air）。**三阶段用三个
        //   方块 id**（非 state 编码）—— 因 mesher 整立方路径按 BlockDef 静态 tile 渲染、无 per-state top tile
        //   选择先例（water/wheat 走水段/cross 段），故同 bed/wool 多 id 模式（每阶段独立 tile 顶面 + 共享侧面）。
        //   isAnvil 单一权威谓词覆盖三阶段（playercontroller 右键开 UI / 破块掉落统一读它）。
        //   整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形；MC 1.0 铁砧是异形低体
        //   + 上方砧台，本工程简化为整立方以复用既有渲染路径，机制等价非视觉对齐 §4）、hardness=5.0（同 MC
        //   1.0 铁砧量级，金属偏硬）、toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且掉落）、
        //   dropId=自身（破任一阶段铁砧掉对应阶段铁砧方块，可放回；玩家仅在创造调色板取用**完好**铁砧）、
        //   dropCount=1、maxStack=64。各面贴图：顶=anvil_top / anvil_damaged_1_top / anvil_damaged_2_top
        //   （tile 113/115/116，深铁砧台 + 砧面轮廓 + 阶段递增裂纹）/ 底·侧·前=anvil_base(114)（深铁砧身，
        //   三阶段共享）。音色归 GroupStone（金属质）。配方：3 铁块顶行 + 4 铁锭中底行（T 形下方实心）→ 1 完好
        //   铁砧（recipe.cpp 有序 3×3）。进创造调色板（仅完好铁砧 Anvil；微损 / 重损不进调色板，由使用产生）。
        Anvil           = 97, // 铁砧（完好）：右键开铁砧 UI（修复/合并/重命名，耗 XP）；3 铁块+4 铁锭合成；损坏阶段 0
        AnvilChipped    = 98, // 铁砧（微损）：使用损坏态 1（顶面 anvil_damaged_1_top，细裂纹）；再损→重损
        AnvilDamaged    = 99, // 铁砧（重损）：使用损坏态 2（顶面 anvil_damaged_2_top，粗裂纹）；再损→碎裂移除
        // ── t482/t483 防御造物方块（机制等价 MC 1.0 雪傀儡 / 铁傀儡搭建材料；名称 / 贴图全原创自绘 §9a）：
        //   南瓜（Pumpkin）：雪傀儡 / 铁傀儡的「头」（玩家放置南瓜并搭好下方排列 → 触发造物生成）。整立方
        //   opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 chest / wool 同族）、
        //   hardness=1.0（同 MC 1.0 南瓜量级，软质）、toolType=NoTool（空手可采且掉落）、requiresTool=false、
        //   dropId=自身（破南瓜掉南瓜方块，可放回）、dropCount=1、maxStack=64。各面贴图：顶·底=
        //   pumpkin_top(119)（橙色瓜顶 + 中央短茎）/ 侧=pumpkin_side(117)（橙色 + 纵向瓜棱深纹）/-Z 前面=
        //   pumpkin_face(118)（橙色 + 刻面双眼 + 锯齿嘴，作造物头时朝向玩家侧，机制等价 MC 刻面南瓜 jack o'lantern）。
        //   音色归 GroupGrass（软植物音）。**造物触发**：PlayerController::placeBlock 放置南瓜后检测下方排列
        //   （雪块×2 竖直 → 雪傀儡 / 铁块×4 T 形 → 铁傀儡），命中 → 生成对应防御造物 + 静默移除结构方块。
        //   进创造调色板（玩家取用 / 搭建）。
        Pumpkin         = 100, // 南瓜：造物头部方块（雪傀儡 / 铁傀儡搭建触发物）
        //   雪块（Snow）：雪傀儡的身体（南瓜 + 雪块×2 竖直搭建）。整立方 opaque（solid=true / ShapeFull ——
        //   走 mesher 整立方面路径，**非**异形，与 SnowLayer 同族；区别 SnowLayer 是薄层、Snow 是实心满格 ——
        //   雪傀儡机制等价 MC 用「雪块」满格而非薄雪层，故独立方块 id）、hardness=0.2（同 MC 1.0 雪块量级，软质）、
        //   **toolType=Shovel + requiresTool=true + minTier=0**（t505：铲挖掉 4 雪球；空手挖不掉落 —— 机制等价
        //   MC 1.0 雪块铲挖掉 4 雪球、空手无掉落）、dropId=0x23D（雪球 SnowballId，材料段 t510）、**dropCount=4**
        //   （MC 1.0 雪块铲挖掉 4 雪球，机制等价非 1）、maxStack=64。各面贴图=snow(57)（冷白底 + 细密冰晶噪点，
        //   与 SnowLayer 共享；tools/build_snow.py 程序生成）。音色归 GroupSand（颗粒雪响，同 SnowLayer）。
        //   **造物触发**：placeBlock 放置南瓜后检测下方雪块×2 → 生成雪傀儡 + 移除结构。**4 雪球合 1 雪块**
        //   （recipe.cpp 4 雪球 → 1 雪块，机制等价 MC 1.0 雪块配方）。进创造调色板（玩家取用 / 搭建）。
        Snow            = 101, // 雪块：造物身体方块（雪傀儡 2 块竖直）
        // ── t484 废弃矿井结构方块（机制等价 MC 1.0 废弃矿井 mineshaft 的蛛网 / 铁轨；名称 / 贴图全原创自绘 §9a）：
        //   蜘蛛网（Cobweb）：**cross 形广告牌方块**（与 TallGrass / Sapling 同走 PartialBlockGeometry 的 cross 几何段，
        //   两片对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。机制等价 MC 1.0 蛛网（cobweb / web）。
        //   worldgen placeMineshaft 在矿井巷道间隙确定性散布（同 seed 同分布，PLAN §2-K）。
        //   solid=false（非实体 → 不挡邻居面剔除，同草丛 / 火把）、shape=ShapeNone（**无碰撞** → 玩家穿过；
        //   t565：玩家 footprint（脚位 / 眼位格）在蛛网内 → 水平速度 ×0.15 大幅减速（playercontroller step，
        //   机制等价 MC 1.0 蛛网粘滞减速））、hardness=4.0（t565：空手挖极慢 —— 机制等价 MC 1.0 cobweb 空手
        //   ~20s 挖不掉）、toolType=Sword + requiresTool=true + minTier=0（t565：**须持剑挖才掉落**（任何剑
        //   tier≥0 都可），空手 / 其它工具挖破**无掉落**（同石类 requiresTool 语义；机制等价 MC 1.0 蛛网须
        //   剑 / 剪刀快速采集，本工程无剪刀剪取故剑为唯一采集工具））、dropId=0x219（**线**材料段
        //   RecipeRegistry::StringId；Core 不依赖 Game 故用字面量 0x219 —— 破蛛网掉线而非蛛网方块，
        //   机制等价 MC 1.0「破蛛网掉线」）、dropCount=1、maxStack=64。各面贴图=cobweb(120)（透明底 +
        //   灰白蛛丝放射网纹，alphaCutoff cutout）。音色归 GroupGrass（软植物音，同草丛）。进创造调色板。
        //   t565 配方：4 线（2×2 满铺）→ 1 白羊毛（Wool；recipe.cpp）。
        Cobweb          = 102, // 蜘蛛网：cross 形蛛网（机制等价 MC 1.0 cobweb）；矿井散布；无碰撞；粘人减速；剑挖掉线
        //   铁轨（Rail）：**贴地薄板 cross/flat 方块**（mesher 走 PartialBlockGeometry 的 Rail case 画一片水平
        //   双面 quad 贴 cell 底部 → 平铺地面，与 LilyPad 横向浮叶同源几何；alpha 透明底 cutout）—— 非 1×1×1 整立方。
        //   机制等价 MC 1.0 铁轨（rail）。worldgen placeMineshaft 在矿井木地板上确定性散布（同 seed 同分布）。
        //   **t565 连接 state**：state 4 位 = 水平 4 向连接位（RailConnPx=1/RailConnNx=2/RailConnPz=4/RailConnNz=8），
        //   放置 / 邻轨破放时由 World::checkRailOnEdit / placeBlock 自动计算（与相邻 Rail 互连；0/1 连接 → 直轨、
        //   对向 2 连接 → 直轨、邻向 2 连接 → 90° 拐角、3+ 连接 → 十字），mesher 据连接位选 121（直 NS）/
        //   UV 旋转（直 EW）/ 136（拐角）/ 137（十字）—— 机制等价 MC 1.0 rail 自动连接 + 转弯。
        //   solid=false（非实体 → 不挡邻居面剔除，同睡莲）、shape=ShapeNone（**无碰撞** → 玩家走过；矿车沿轨
        //   行驶（t565 MinecartManager，Entities 层））、hardness=0（瞬破）、NoTool（空手可采且掉落）、
        //   dropId=自身（破铁轨掉铁轨方块，可放回）、dropCount=1、maxStack=64。各面贴图=rail(121)（透明底 +
        //   棕色枕木 + 灰铁双轨，alphaCutoff cutout）。音色归 GroupStone（金属质敲击，最接近 MC 1.0 铁轨 metal
        //   SoundType）。进创造调色板（玩家可取用 / 放置）。配方：6 铁锭 + 1 木棒（中行）→ 16 铁轨（工作台 3×3
        //   有序，recipe.cpp；机制等价 MC 1.0 铁轨配方）。
        Rail            = 103, // 铁轨：贴地薄板 flat（机制等价 MC 1.0 rail）；矿井木地板散布；自动连接 + 拐角 + 矿车行驶
        // ── t485 沙漠神殿结构方块（机制等价 MC 1.0 沙漠神殿 desert temple 的 TNT / 切制砂岩；名称 / 贴图全原创自绘 §9a）：
        //   TNT（TntBlock）：可引爆的爆炸物方块（机制等价 MC 1.0 TNT）。整立方 opaque（solid=true / ShapeFull ——
        //   走 mesher 整立方面路径，**非**异形，与砂岩/箱子同族）、hardness=0.0（MC 1.0 TNT 瞬破，无工具要求）、
        //   toolType=NoTool（空手可采且掉落）、requiresTool=false、dropId=自身（破 TNT 掉 TNT 方块，可放回）、
        //   dropCount=1、maxStack=64。各面贴图=tnt(122)（深红药柱底 + 横向深棕捆带 + 中央亮黄标识 + 顶部引线点，
        //   原创自绘 §9a）。音色归 GroupGrass（软质闷击，机制等价 MC 1.0 TNT 草地音色）。**引爆路径**：
        //   踩压力板触发（playercontroller tick 扫玩家 footprint 格——压力板下垫 TNT 即引爆）→ EntityManager::
        //   detonateTntBlock（复用 destroySphereSilent 球形破坏 + 距离衰减伤玩家 + explosion 音/视，同 Stalker
        //   爆炸路径 t284）。配方：5 火药 + 4 沙 → 1 TNT（recipe.cpp；机制等价 MC 1.0 TNT）。进创造调色板。
        TntBlock        = 104, // TNT：可引爆爆炸物方块（机制等价 MC 1.0 TNT）；沙漠神殿陷阱 + 创造可放置 / 合成
        //   切制砂岩（CutSandstone）：装饰用砂岩变体（机制等价 MC 1.0 切制砂岩 cut sandstone——表面更平滑、带
        //   切割倒角边框，区别于普通砂岩 Sandstone 的横向层理纹）。整立方 opaque（solid=true / ShapeFull —— 走
        //   mesher 整立方面路径，**非**异形，与砂岩/石头同族）、hardness=0.8（同砂岩量级，需镐）、toolType=Pickaxe、
        //   requiresTool=true、minTier1（木镐可破）、dropId=自身（破切制砂岩掉切制砂岩方块，可放回）、dropCount=1、
        //   maxStack=64。各面贴图=cut_sandstone(123)（暖沙色平滑底 + 内陷矩形装饰边框，原创自绘 §9a）。
        //   音色归 GroupStone（石质，同砂岩）。worldgen placeDesertTemple 金字塔外框装饰（与砂岩混排）。
        //   进创造调色板（玩家可取用 / 放置）。
        CutSandstone    = 105, // 切制砂岩：装饰砂岩变体（机制等价 MC 1.0 cut sandstone）；金字塔外框 + 创造可放置
        // ── t486 丛林神殿结构方块（机制等价 MC 1.0 丛林神殿 jungle temple 的苔石 / 发射器；名称 / 贴图全原创自绘 §9a）：
        //   苔石（MossyCobble）：长满苔藓的圆石变体（机制等价 MC 1.0 mossy cobblestone——圆石上覆盖苔斑，
        //   丛林 / 地牢 / 神殿等阴暗潮湿环境的风化石材）。整立方 opaque（solid=true / ShapeFull，与圆石/砂岩
        //   同走 culled 立方面路径，**非**异形）、hardness=2.0（同圆石量级，需镐加速）、toolType=Pickaxe、
        //   requiresTool=true、minTier1（木镐可破且掉落；同 Cobble）、dropId=自身（破苔石掉苔石方块，可放回）、
        //   dropCount=1、maxStack=64。各面贴图=mossy_cobble(124)（圆石灰底 + 散布的暗绿苔藓斑簇，原创自绘
        //   §9a；tools/build_mossy_cobble.py 程序生成）。音色归 GroupStone（石质，同 cobble 族）。worldgen
        //   placeJungleTemple 神殿主体（苔石建筑，spec「苔石建筑」）。进创造调色板（玩家可取用 / 放置）。
        MossyCobble     = 106, // 苔石：长苔圆石变体（机制等价 MC 1.0 mossy cobblestone）；丛林神殿主体 + 创造可放置
        //   发射器（Dispenser）：受触发时朝所朝方向发射箭矢弹丸的机关方块（机制等价 MC 1.0 发射器 dispenser——
        //   无红石系统，故用「踩压力板 → 邻接发射器射箭」直接触发，spec「无红石用 dispenser 方块直接触发」）。
        //   整立方 opaque（solid=true / ShapeFull，与熔炉/箱子同走 culled 立方面路径，**非**异形）、hardness=3.5
        //   （同熔炉量级，石质偏硬）、toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且掉落，同熔炉）、
        //   dropId=自身（破发射器掉发射器方块，可放回）、dropCount=1、maxStack=64。
        //   各面贴图：顶/底=dispenser_top(125)（石质灰底 + 中央圆形排出口俯视环纹）/ 侧=dispenser_side(126)
        //   （石质灰底 + 边框暗带）/ 前面（所朝方向，排出口）=dispenser_front(127)（石质灰底 + 中央暗腔排出口）。
        //   **state 编码朝向**：bit[1:0] = 0=+X 1=-X 2=+Z 3=-Z（与 chest / furnace / door horizontalFacing 同源）；
        //   放置时前面朝玩家（同熔炉）；mesher 据 state 选前面贴图（同 furnace tileFor 分支）。
        //   音色归 GroupStone（石质）。**触发路径**：PlayerController::scanDispenserTraps 扫玩家 footprint 格——
        //   任一格为压力板（Wood/Cobble）且其 4 水平邻格之一 == Dispenser → EntityManager::spawnArrow 从发射器
        //   格中心朝压力板方向（= 玩家所在）水平射箭（复用既有 Arrow 弹丸 tick：抛物 + 命中伤害，机制等价
        //   MC 1.0 发射器射箭）。每发射器 per-dispenser 冷却（防每帧刷屏）。worldgen placeJungleTemple 把发射器
        //   嵌入走廊石壁（朝向走廊中央的压力板）。进创造调色板（玩家可取用 / 放置 / 自建机关）。
        Dispenser       = 107, // 发射器：踩压力板触发的射箭机关方块（机制等价 MC 1.0 dispenser）；丛林神殿陷阱
        // ── t487 要塞结构方块（机制等价 MC 1.0 要塞 stronghold 的石砖 / 石砖台阶/楼梯 / 末地传送门；名称 / 贴图
        //   全原创自绘 §9a；§9 区隔：末地/末影之眼为通用描述词，机制对齐非专名照搬）：
        //   石砖（StoneBrick）：石质整立方装饰方块（要塞墙体主体；机制等价 MC 1.0 stone brick）。整立方 opaque
        //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 stone/cobble/mossy 同族）、
        //   hardness=1.5（同 stone 量级，需镐）、toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且掉落）、
        //   dropId=自身（破石砖掉石砖方块，可放回）、dropCount=1、maxStack=64。各面贴图=stone_brick(128)
        //   （石质灰底 + 砖块缝纹网格，原创自绘 §9a；tools/build_stone_brick.py 程序生成）。音色归 GroupStone。
        //   worldgen placeStronghold 要塞墙体 / 走廊 / 房间围栏主体。进创造调色板（玩家可取用 / 放置）。
        StoneBrick      = 108, // 石砖：要塞墙体主体（机制等价 MC 1.0 stone brick）；石质整立方 + 砖纹贴图
        //   石砖台阶（StoneBrickSlab）：半高（上/下半）。复用既有异形方块系统（ShapeSlab + PartialBlockGeometry
        //   几何），仅换石砖贴图（tile 128）+ 石质属性。solid=false（非整立方 → 不挡邻居面剔除，同木/圆石半砖）、
        //   hardness=1.5、Pickaxe、requiresTool=true、minTier1、dropId=自身、dropCount=1、maxStack=64。
        //   state bit0=上半(1)/下半(0)（与 WoodSlab / CobbleSlab 同编码）。经 isSlab 谓词并入异形路由（段外，
        //   同 CobbleSlab 模式）。mesher PartialBlockGeometry::append 的 slab case 内 tile 由 tileIndex 取本方块
        //   sideTile=stone_brick(128)。音色归 GroupStone。worldgen placeStronghold 走廊 / 楼梯井装饰；进创造调色板。
        StoneBrickSlab  = 109, // 石砖台阶：半高（机制等价 MC 1.0 stone brick slab）；复用 ShapeSlab 几何 + 石砖贴图
        //   石砖楼梯（StoneBrickStairs）：整步 + 背墙。复用既有异形方块系统（ShapeStairs + PartialBlockGeometry
        //   几何），仅换石砖贴图（tile 128）+ 石质属性。solid=false、hardness=1.5、Pickaxe、requiresTool=true、
        //   minTier1、dropId=自身、dropCount=1、maxStack=64。state[1:0]=朝向 bit2=倒置（与 WoodStairs / CobbleStairs
        //   同编码）。经 isStairs 谓词并入异形路由（段外，同 CobbleStairs 模式）。音色归 GroupStone。
        //   worldgen placeStronghold 楼梯井；进创造调色板。
        StoneBrickStairs= 110, // 石砖楼梯：整步+背墙（机制等价 MC 1.0 stone brick stairs）；复用 ShapeStairs 几何
        //   末地传送门（EndPortal）：要塞传送门房中央的传送门方块（机制等价 MC 1.0 end portal；§9 区隔：末地为
        //   通用描述词，机制对齐非 MC 专名照搬）。**整立方不透明**——简化为满格整立方（机制等价 MC 1.0 末地传送门
        //   的「传送门平面」外观，本工程不做异形框架；激活后由 Main.qml 伪光源 + state 切换显星空黑洞视觉）。
        //   solid=false（非实体 → 不挡邻居面剔除，与地形解耦；机制等价 MC 末地传送门无碰撞可走过）/ ShapeFull
        //   （碰撞 / 选中仍走整格可踩 / 可瞄准）、**hardness=-1.0**（不可挖掘：canMine=false，任何模式/工具不破，
        //   防创造秒破传送门；同 bedrock / Water 哨兵语义）、dropId=0 不掉落、dropCount=0、maxStack=64
        //   （worldgen 专属 / 不掉落 → maxStack 实不可达，填 64 与方块族一致）。各面贴图=t620 末影祭坛三面（见下 endframe 化段；原创自绘 §9a；tools/build_endframe.py 程序生成）。
        //   音色归 GroupStone（石质兜底）。**激活机制**：玩家持末影之眼物品（EndEyeId）右键传送门 → placeBlock
        //   useBlock 分支检测命中 EndPortal + 持 EndEyeId → 翻 state bit0（激活态）+ emit swingArm + qInfo 日志
        //   （末地预热占位：仅激活效果 + 日志，不实现末地维度，spec「实际传送末地可推迟为占位/告警」）。
        //   **t620 endframe 化**：贴图从程序星空切到末影祭坛三面（侧·底=endframe_side(140) / 顶（未放之眼）=
        //   endframe_top(141) / 顶（激活态）=endframe_eye(142)（框面+中央之眼亮纹，tileFor per-face+
        //   state 选；程序星空 129/130 仍留图集但已无引用）。
        //   t634 进创造调色板（要塞祭坛可取用 / 自建末地祭坛测试；创造瞬破 drop=0、生存不可破同基岩）。state 经 m_states 落 SQLite round-trip 保真。
        EndPortal       = 111, // 末地传送门：要塞传送门房中央（机制等价 MC 1.0 end portal）；末影之眼右键激活（占位）
        // ── t490 手动 TNT 点火机关方块（机制等价 MC 1.0 lever / 木按钮 / 石按钮；无红石系统，故用「右键激活 →
        //   点燃水平四邻 TNT」简化为单次脉冲触发，spec「若时间紧，拉杆/按钮可简化为放置即点燃邻接 TNT 或右键触发
        //   单一路径」）。名称 / 贴图全原创自绘 §9a（tools/build_lever_button.py 程序生成）。三者本身是可放置 / 可破的
        //   装饰机关方块（进创造调色板），右键激活时点燃邻接 TNT（机制等价 MC 1.0 红石点火源——本项目无红石，故
        //   把「激活即点火邻接 TNT」直接绑在右键动作上）。复用既有异形方块系统（ShapePlate 贴地薄板几何 + 单 id
        //   谓词路由），机制等价 MC 1.0 三类机关（杠杆 lever / 木按钮 wooden button / 石按钮 stone button），仅机制
        //   对齐非照搬 MC 美术。
        //   Lever（杠杆）：木质底座 + 竖直 / 拉下两态扳手柄（state bit0=激活态）。solid=false（非整立方 → 不挡
        //     邻居面剔除，同压力板）、ShapePlate（碰撞/选中贴地薄板，与 WoodPressurePlate 同几何——简化为贴地薄
        //     板而非墙面附着，本项目放置即贴地，机制对标可接受）、hardness=0.5（木软，徒手可破）、NoTool、
        //     requiresTool=false（空手掉落）、dropId=自身、dropCount=1、maxStack=64。各面贴图=lever(131)
        //     （木质底座 + 中央竖直扳柄 + 顶部圆柄头；激活态由 mesher 据 state bit0 切亮色高光，非 MC 资产）。
        //     音色归 GroupWood（木质）。**激活路径**：PlayerController placeBlock useBlock 分支检测命中 Lever →
        //     翻 state bit0（扳柄态）+ 点燃水平四邻 TNT（spawnPrimedTnt）+ emit swingArm（机制等价 MC 1.0 杠杆扳动即
        //     红石脉冲点火 TNT）。进创造调色板（玩家可取用 / 放置 / 自建机关）。
        Lever           = 112, // 杠杆：右键扳动 → 点燃水平四邻 TNT（机制等价 MC 1.0 lever；简化无红石）
        //   WoodButton（木按钮）：木质小按钮（state bit0=激活态，按下后短暂亮起）。solid=false / ShapePlate
        //     （与 Lever 同几何）、hardness=0.5、NoTool、requiresTool=false、dropId=自身、dropCount=1、maxStack=64。
        //     各面贴图=button_wood(132)（木质底座 + 中央凸起圆钮；激活态由 mesher 据 state bit0 切亮色高光）。
        //     音色归 GroupWood。**激活路径**：同 Lever——右键 → 翻 state bit0 + 点燃水平四邻 TNT（机制等价 MC 1.0
        //     木按钮按下红石脉冲点火 TNT）。进创造调色板。
        WoodButton      = 113, // 木按钮：右键按下 → 点燃水平四邻 TNT（机制等价 MC 1.0 wooden button；简化无红石）
        //   StoneButton（石按钮）：石质小按钮（state bit0=激活态，按下后短暂亮起）。solid=false / ShapePlate
        //     （与 Lever / WoodButton 同几何）、hardness=0.5、Pickaxe（石质）、requiresTool=true、minTier1（木镐可破
        //     且掉落）、dropId=自身、dropCount=1、maxStack=64。各面贴图=button_stone(133)（石质底座 + 中央凸起圆钮；
        //     激活态由 mesher 据 state bit0 切亮色高光）。音色归 GroupStone。**激活路径**：同 Lever/WoodButton——
        //     右键 → 翻 state bit0 + 点燃水平四邻 TNT（机制等价 MC 1.0 石按钮按下红石脉冲点火 TNT）。进创造调色板。
        StoneButton     = 114, // 石按钮：右键按下 → 点燃水平四邻 TNT（机制等价 MC 1.0 stone button；简化无红石）
        // ── t507 白蘑菇 / 棕蘑菇（BrownMushroom）：机制等价 MC 1.0 brown mushroom（沼泽 / 阴暗草地小蘑菇，
        //   与红蘑菇 Mushroom=48 同族，仅配色区别 —— 棕色菌盖 + 米色菌柄）。**cross 形广告牌方块**（与 Mushroom /
        //   Sapling / DeadBush 同走 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。
        //   solid=false（非实体 → 不挡邻居面剔除，同草丛 / 红蘑菇）、shape=ShapeNone（**无碰撞** → 玩家穿过）、
        //   hardness=0（瞬破，同红蘑菇 / 草丛）、NoTool（空手可采且掉落）、dropId=自身（破白蘑菇掉白蘑菇方块，可放回）、
        //   dropCount=1、maxStack=64。各面贴图=brown_mushroom(135)（透明底 + 米色菌柄 + 棕色菌盖白斑，alphaCutoff cutout）。
        //   音色归 GroupGrass（软植物音，同红蘑菇 / 草丛）。worldgen 在沼泽草地散布（同红蘑菇，与 placeSwampFlora
        //   共址）；进创造调色板（装饰取用）。**蘑菇汤配方原料**（recipe.cpp：碗 + 红蘑菇 + 白蘑菇 → 1 蘑菇汤）。
        //   §9 区隔：仅机制对齐 MC 1.0 brown mushroom，名称 / 贴图全原创自绘（§9a，tools/build_brown_mushroom.py）。
        //   isMushroom(id) 单一权威谓词覆盖红 / 白两蘑菇（mesher cross 路由 / 失撑掉落 / 放置预检统一读，避免各处
        //   硬编码 2 个 id 判定漂移，同 isBed / isIce 模式）。
        BrownMushroom   = 115, // 白蘑菇 / 棕蘑菇：cross 形蘑菇（机制等价 MC 1.0 brown mushroom）；蘑菇汤原料
        RedstoneOre    = 116, // 红石矿石（t569）：机制等价 MC 1.0 红石矿（嵌于 stone 最深层 y∈[5,16]、需铁镐采掘、
                                  //   掉 4 红石粉；走过 / 挖掘时 state bit0 置亮 → 方块光 flood 微弱泛光后自动熄灭，
                                  //   机制等价 MC「玩家走近 / 触碰红石矿发光」）。整立方 opaque（solid=true / ShapeFull，
                                  //   与 coal/iron/diamond/copper/gold/lapis 矿石同族）、hardness=3.0（同族量级，需镐）、
                                  //   toolType=Pickaxe、requiresTool=true、minToolTier=3（需铁镐才掉落，机制等价 MC
                                  //   红石矿需铁镐；同 diamond/gold 门槛）、dropId=0x224（红石粉材料段
                                  //   RecipeRegistry::RedstoneId；Core 不依赖 Game 故用字面量 0x224）、dropCount=4
                                  //   （MC 1.0 红石矿掉 4 粉，时运可放大）、maxStack=64。各面贴图=redstone_ore(138)
                                  //   （石头底 + 鲜红菱斑矿粒，复制钻石矿斑块布局改红色，原创自绘 §9a）。音色归
                                  //   GroupStone（石质）。worldgen scatterOres 高度分层散布于**最深层** y∈[5,16]
                                  //   （金属族中最深，机制等价 MC 1.0 红石 Y<16 深层富集）。洞穴裸露同其它矿石。
                                  //   进创造调色板。
        // ── t609 投掷器（Dropper）：机制等价 MC 1.0 dropper（与发射器同族的机关盒，差异 = **全部物品**一律以
        //   掉落物实体弹出，无箭 / 雪球 / 剑弹丸分派——「只投不射」）。整立方 opaque（solid=true / ShapeFull，
        //   与熔炉 / 发射器同走 culled 立方面路径，**非**异形）、hardness=3.5（同发射器 / 熔炉量级，石质偏硬）、
        //   toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且掉落，同熔炉）、dropId=自身（破投掷器掉
        //   投掷器方块，可放回）、dropCount=1、maxStack=64。
        //   各面贴图：顶/底=furnace_top(12)（复用熔炉顶——机关盒家族石质顶面，同 MC 投掷器侧面即熔炉质感的观感；
        //   pack 侧经既存 tileFilenameMap {12→furnace_top.png} 自动覆盖）/ 侧=furnace_side(13)（复用熔炉侧面，
        //   同上自动 pack 覆盖）/ 前面（排出口所朝方向）=dropper_front(139)（石质灰底 + 中央**小**方形暗孔——
        //   比发射器的大暗腔排出口更小更简的「轻量出口」读感，只掉物品不射弹丸；tools/build_dropper.py 程序生成
        //   原创自绘 §9a；pack 侧 tileFilenameMap {139→dropper_front_horizontal.png} t620 已接）。
        //   **state 编码朝向**（同发射器 / 熔炉 / 箱子 chestFrontFace 编码）：bit[1:0] = 0=+X 1=-X 2=+Z 3=-Z；
        //   放置时排出口面朝玩家（同熔炉）；mesher 据 state 选前面贴图（同 furnace / dispenser tileFor 分支）。
        //   音色归 GroupStone（石质）。**触发路径**（同发射器，PlayerController::scanDispenserTraps 扩展）：
        //   踩压力板且其 4 水平邻格之一 == Dropper → dispenseFromDispenser 投掷器分支 = **全部物品**走
        //   spawnItemAt 从排出口定向弹出掉落物（机制等价 MC 1.0 dropper 弹出物品）；per-dispenser 冷却共用。
        //   **库存**：复用 DispenserStore（9 槽 per-block，按坐标键控——发射器 / 投掷器共用同一 store，key 是
        //   坐标故不冲突；玩家放置 ensureDispenser 注册 / 右键开同一 DispenserUI（标题显「投掷器」）/ 破块
        //   掉内容 + 清条目，全走发射器既有链）。配方：7 圆石（上排满 + 中 / 下排左右，中心 + 下中空）→ 1 投掷器
        //   （recipe.cpp；机制等价 MC 1.0 dropper 7 cobble）。进创造调色板（玩家可取用 / 放置 / 自建机关）。
        Dropper         = 117, // 投掷器：踩压力板触发的弹掉落物机关方块（机制等价 MC 1.0 dropper）；全部物品弹出
        // ── t620 矿物存储块（mineral storage blocks；机制等价 MC 1.0 coal/lapis/diamond/gold/redstone block ——
        //   9 材料 ↔ 1 块 双向配方的「压缩存储」方块；铁块 IronBlock=96 既存（t477），本段补齐其余五种）。
        //   全族整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，与 iron_block / obsidian 同族）。
        //   采掘统一石镐族：Pickaxe + requiresTool=true（掉落依赖镐；机制等价 MC 1.0 存储块需镐）。minTier 按
        //   对应**矿物**的门槛对齐（与挖矿同镐才配拆块，机制等价 MC「块与矿同镐」）：coal=木镐1 / lapis=石镐2 /
        //   diamond=铁镐3 / gold=铁镐3 / redstone=铁镐3。dropId=自身（破块掉同种块，可放回；机制等价 MC 1.0
        //   存储块无精准采集也掉自身）、dropCount=1、maxStack=64。各面贴图=对应 *_block 瓦片（六面同，机制
        //   等价 MC 1.0 存储块六面同贴图）。音色归 GroupStone（石质 / 金属质，同 iron_block 族）。全部进创造
        //   调色板。配方（recipe.cpp）双向：9 材料 3×3 满铺 → 1 块；1 块（任意格单放）→ 9 材料（机制等价
        //   MC 1.0「9↔1」无损拆装）。coal_block 额外可作**熔炉燃料**（smelting.cpp 燃料表 800s=80 件，机制
        //   等价 MC 1.0 煤块烧 80 件 = 煤×10 的 9 倍效率 +1 件盈余）。
        CoalBlock       = 118, // 煤炭块：9 煤炭 ↔ 1 块；燃料 800s（80 件）；木镐采掘
        LapisBlock      = 119, // 青金石块：9 青金石 ↔ 1 块；石镐采掘（附魔材料的压缩存储）
        DiamondBlock    = 120, // 钻石块：9 钻石 ↔ 1 块；铁镐采掘（财富炫耀 / 压缩存储）
        GoldBlock       = 121, // 金块：9 金锭 ↔ 1 块；铁镐采掘（同金矿门槛）
        RedstoneBlock   = 122, // 红石块：9 红石粉 ↔ 1 块；铁镐采掘（红石粉的压缩存储）
        // ── t620 红石灯（RedstoneLamp）：机制等价 MC 1.0 redstone lamp —— 右键切换 on/off 的**可放置光源**方块
        //   （本项目无红石系统，故简化为「右键直接开关」取代「红石信号驱动」，spec 简化许可）。放置 = off 态；
        //   右键翻 state bit0（RedstoneLampStateOnFlag）切 on/off：on 态换 redstone_lamp_on 贴图 + **方块光
        //   等级 15**（lightEmission 状态感知版据 bit0 返 15 / 0，同 t494 燃烧熔炉 / t569 点亮红石矿的 state
        //   驱动光照模式 —— recomputeLightAround 检出 lightSourceChanged 即重 flood 方块光，无伪光源）。
        //   整立方 opaque（solid=true / ShapeFull）、hardness=0.3（同 MC 1.0 红石灯量级，玻璃质软）、
        //   toolType=Pickaxe（玻璃质敲击族，同 glass / ice）、requiresTool=false（空手可破且掉落——无精准采集
        //   语义的红石灯走「可回收」口径，同 glass 本工程取舍）、dropId=自身、dropCount=1、maxStack=64。
        //   各面贴图=redstone_lamp_off(152)（off 态 def 默认）/ on 态 mesher tileFor 据 state bit0 换
        //   redstone_lamp_on(153)（暖黄亮芯 vs 灰暗壳，机制等价 MC 1.0 两态贴图）。音色归 GroupStone
        //   （玻璃质，同 glass / ice）。合成：4 红石粉 + 1 玻璃 → 1 红石灯（十字围心；无荧石故用玻璃作壳，
        //   机制对标 MC glowstone+redstone 的本地化）。进创造调色板。
        RedstoneLamp    = 123, // 红石灯：右键开关的可放置光源（on=光 15 + 亮贴图；机制等价 MC 1.0 redstone lamp）
        // ── t627 压力板家族扩展（plate family；机制等价 MC 1.0 stone / iron(heavy) / gold(light) pressure
        //   plate）。三者复用既有异形方块系统（ShapePlate 贴地薄板 + PartialBlockGeometry plate case），
        //   仅贴图 / 采掘属性 / **触发权重**差异。触发语义（t627 边沿化）：玩家/mob/掉落物站上 → 踩下沿触发
        //   一次（scanTntTraps / scanDispenserTraps 只在踩下沿 fire；离开重置 armed，再踩再触发）+ state bit0
        //   = PressurePlateStatePressedFlag（踩下视觉：mesher 据 bit0 把薄板高度 1/16 压到 1/32，机制等价
        //   MC 压力板被压下变矮）。**触发权重**（BlockRegistry::pressurePlateAccepts 单一权威）：
        //     - 石压力板（StonePressurePlate）：玩家 + mob + 掉落物全触发（同木/圆石板，机制等价 MC 1.0 石板）。
        //     - 铁压力板（IronPressurePlate）：仅玩家 + mob（重质金属板——机制等价 MC 1.0 heavy weighted plate
        //       的「重」语义本地化：需生物级重量，掉落物太轻不触发）。
        //     - 金压力板（GoldPressurePlate）：仅掉落物（轻质金板——机制等价 MC 1.0 light weighted plate 的
        //       「轻」语义本地化：任何掉落物即可触发，是「掉落物传感器」/红石前置玩法件）。
        //   采掘属性：stone=Pickaxe requiresTool=true minTier1（同圆石板）；iron/gold=Pickaxe
        //   requiresTool=true minTier1（金属薄板木镐可拆，机制等价 MC 金属板任意镐可采）。solid=false /
        //   ShapePlate（与 WoodPressurePlate 同几何，非整立方 → 不挡邻居面剔除）。dropId=自身 / dropCount=1 /
        //   maxStack=64。音色归 GroupStone（石质/金属质）。放置放宽同木/圆石板（isPressurePlate 谓词统一覆盖）。
        //   配方（t627，recipe.cpp）：stone=2 石头横排 / iron=2 铁锭横排 / gold=2 金锭横排（机制等价 MC 1.0
        //   压力板 2 材料配方，同木/圆石板既存模式）。进创造调色板（机关件 tab，紧随圆石压力板）。
        StonePressurePlate = 124, // 石压力板：玩家+mob+掉落物触发（同木/圆石板）；踩下沿 fire 一次 + 薄板压半
        IronPressurePlate  = 125, // 铁压力板（重）：仅玩家+mob 触发；踩下沿 fire 一次 + 薄板压半
        GoldPressurePlate  = 126, // 金压力板（轻）：仅掉落物触发；踩下沿 fire 一次 + 薄板压半
        // ── t638 铁轨家族扩展 + 红石火把 + 附魔台翻页书（机制等价 MC 1.0 powered rail / detector rail /
        //   redstone torch / 附魔台顶摊开的书；名称 / 程序贴图全原创自绘 §9a；pack 贴图运行期映射）。
        //   动力铁轨（GoldenRail；spec 命名 PoweredRail 的本地化——MC 1.0 官方名 powered rail / golden
        //   rail 均指 id 27）：贴地薄板（与 Rail 同几何——水平双面 quad 贴 cell 底 1/16）；**仅直线连接**
        //   （无拐角——机制等价 MC 1.0 动力轨不能转弯）；矿车驶过时**加速**（MinecartManager tickRiddenCart
        //   读脚下格 == GoldenRail → 目标速度提升到动力档，机制等价 MC 1.0 powered rail 给矿车 boost）。
        //   solid=false / ShapeNone（无碰撞，玩家走过；同 Rail）、hardness=0（瞬破）、NoTool（空手可采且
        //   掉落）、dropId=自身、dropCount=1、maxStack=64。各面贴图=rail_golden(157)（透明底 + 金轨双线
        //   + 红石连接点；程序回退 tools/build_rail_family.py）；pack 侧 {157→powered_rail.png}（MC 现代名
        //   powered_rail = 1.8 名 golden_rail；_powered 变体是通电亮态——本工程无红石信号恒断电，留注释）。
        //   state 复用 Rail 4 位连接编码（RailConnPx/Nx/Pz/Nz）+ mesher 直线形态（连接位只取对向 → 水平
        //   quad 沿 X 或 Z 铺，UV 旋转同 Rail 直轨；拐角连接位（邻向 2）按直线取「与上一段行进向对齐」…
        //   简化：动力轨只看 ±X / ±Z 对向连接，无对向时取任一向，永不画拐角贴图）。音色 GroupStone（金属）。
        //   配方（MC 1.0）：6 金锭（两行满）+ 中行 木棒+红石粉+木棒（MC 实际 6 gold + stick + redstone
        //   最小包围盒 2×3；本工程 3×3 有序取上两行金锭满 + 中行 棒-红石-棒）→ 6 动力轨。
        GoldenRail     = 127, // 动力铁轨：矿车驶过加速（boost）；仅直线连接；配方 6 金锭+棒+红石
        //   探测铁轨（DetectorRail）：贴地薄板（同 Rail 几何）；**矿车驶上时通电视觉**——state bit0
        //   （DetectorRailStateOnFlag）置位 → mesher 换 rail_detector_on(160) 亮红贴图（占位反馈；真红石
        //   信号输出留红石大轮，spec「踩过变 powered 贴图」）。矿车离开不清位（保持「被压过」状态直到
        //   再编辑——简化：MC 探测轨矿车离开即断，本工程做「驶过闪亮」→ MinecartManager tick 置位 + 定
        //   时清位走 setWaterSilent state 写。直线连接同动力轨（无拐角）。solid=false / ShapeNone /
        //   hardness=0 / NoTool / dropId=自身 / maxStack=64。各面=rail_detector(158)（程序回退透明底 +
        //   石枕 + 亮红探测点）；pack {158→detector_rail.png}（_on 变体 {160→detector_rail_on.png}）。
        //   音色 GroupStone。配方（MC 1.0）：6 铁锭 + 石压力板 + 红石 → 6 探测轨。
        DetectorRail  = 128, // 探测铁轨：矿车驶过通电视觉（占位）；仅直线连接；配方 6 铁锭+石压力板+红石
        //   红石火把（RedstoneTorch）：**常亮 ON 装饰光源**（真红石信号源留红石大轮——本工程无红石系统，
        //   同 t620 红石灯「右键开关」简化口径的姊妹简化：恒亮不熄）。solid=false / ShapeNone（无碰撞、
        //   不挡邻居面剔除）、lightEmission=7（MC 1.0 红石火把光 level 7，约为火把 14 的一半——暗红氛围
        //   光）、hardness=0（瞬破）、NoTool（空手可采且掉落）、dropId=自身、dropCount=1、maxStack=64。
        //   渲染：cross 形广告牌（两片对角双面 quad 贴 redstone_torch(161) 瓦片——透明底 + 深棕柄 + 亮红
        //   焰头，alphaCutoff cutout；与植物族同走 cutout 段，区别于普通火把的 torchHost QML delegate——
        //   方块网格路径更省 delegate 且常亮光源走真方块光 flood（lightEmission）非伪光源）。放置预检：
        //   同火把（需实体邻居支撑——下 / 四侧之一 isSolid，placeBlock Torch 分支扩展）。
        //   pack {161→redstone_torch_on.png}（on 常亮态；off 熄灭态不接——本方块恒亮）。
        //   音色 GroupWood（木质柄）。配方：木棒 + 红石粉（竖列 2 格）→ 1 红石火把（机制等价 MC 1.0
        //   redstone torch on a stick）。
        RedstoneTorch = 129, // 红石火把：常亮装饰光源（光 7）；cross 形；配方 木棒+红石粉
        Count           = 130, // 哨兵：已定义方块数（含 air），也是合法 id 的上界（id < Count）。
    };

    // t387 床方块段哨兵：id ∈ [FirstBed, LastBed] 为床色变体（既存 8 色）。t455 补齐 16 色：追加 8 色新变体段
    //   [FirstExtraBed, LastExtraBed]（white/light_blue/lime/pink/gray/light_gray/purple/brown）。isBed(id) 单一权威
    //   谓词供 t388 睡觉机制（右键床 → 跳清晨 + 重生点）判定「命中格是否床」，覆盖**两个**连续段（既存 8 色 + 新 8 色），
    //   避免各处自写 id 区间漂移（同 isCrossBillboard 段外 cross 模式）。改段时一处同步谓词即可。
    static constexpr int FirstBed = BedRed;
    static constexpr int LastBed  = BedBlack;
    static constexpr int FirstExtraBed = BedWhite;    // t455 新增 8 色床段下界
    static constexpr int LastExtraBed  = BedBrown;    // t455 新增 8 色床段上界
    static bool isBed(quint8 blockId);

    // t457 床低 3D 模型几何常量（cell-local [0,1]）—— PartialBlockGeometry 渲染 + shapeBoxes 碰撞共用同一组值，
    //   保证「碰撞盒顶 = 渲染床垫顶」（玩家立于床垫顶）。kBedMattressTop=床垫顶高（~0.31 = 5/16，低床，碰撞盒顶）；
    //   kBedLegTop=腿顶（3/16）；kBedLegHalf=腿半宽（1/16，角柱 2/16 见方）；kBedPlankTop=木板面顶（4/16）；
    //   kBedInset=床垫 footprint 内缩。
    //   t496 床头板 / 床尾板 / 枕头高度（纯视觉，不进碰撞 —— 碰撞仍走 kBedMattressTop 低盒，机制等价 MC 床
    //   低矮 hitbox + 视觉上凸出的床头板）：kBedHeadboardTop=床头板顶（9/16，床头端竖立木板，高于床垫表
    //   「床头」）；kBedFootboardTop=床尾板顶（7/16，床尾端竖立矮木板，比床头板矮）；kBedPillowTop=枕头顶
    //   （7/16，床头端床垫上的白色枕头，略凸于床垫）；kBedBoardThick=床头板/床尾板厚度（2/16 = kBedInset，
    //   端面条带正好贴满床垫内缩外 → 与内缩床垫零重叠 / 零共面 z-fight）。
    static constexpr float kBedMattressTop  = 0.3125f; // 5/16（碰撞盒顶 = 床垫顶）
    static constexpr float kBedLegTop       = 0.1875f; // 3/16（腿高 = 床架底）
    static constexpr float kBedLegHalf      = 0.0625f; // 1/16（腿半宽 → 角柱 2/16 见方）
    static constexpr float kBedPlankTop     = 0.25f;   // 4/16（床架顶 = 床垫底；床板从此处起，坐在床架上）
    static constexpr float kBedInset        = 0.125f;  // 2/16（床垫 footprint 内缩；= 床板厚度 → 床板端面条带与床垫不重叠）
    static constexpr float kBedHeadboardTop = 0.5625f; // 9/16（床头板顶，床头端竖立木板，纯视觉不进碰撞）
    static constexpr float kBedFootboardTop = 0.4375f; // 7/16（床尾板顶，床尾端竖立矮木板，纯视觉不进碰撞）
    static constexpr float kBedPillowTop    = 0.4375f; // 7/16（枕头顶，床头端床垫上白色枕，纯视觉不进碰撞）
    static constexpr float kBedBoardThick   = 0.125f;  // 2/16（床头板 / 床尾板厚度 = kBedInset，端面条带贴满内缩外）

    // t455 16 色 wool 统一谓词（单一权威）：white（Wool=27）+ 15 色变体段 [FirstWoolVariant, LastWoolVariant]
    //   （WoolOrange=63..WoolBlack=77）即羊毛。供未来染料 / 配方 / 渲染判定「是否羊毛方块」，避免各处自写 id
    //   判定漂移（同 isBed / isCrossBillboard 模式）。white 与变体段不连续（Wool=27 夹在中间），故两段并判。
    static constexpr int FirstWoolVariant = WoolOrange; // 15 色羊毛变体段下界（white 复用 Wool=27）
    static constexpr int LastWoolVariant  = WoolBlack;  // 15 色羊毛变体段上界
    static bool isWool(quint8 blockId);

    // t428 床 state 编码 + 配对格偏移（机制等价 MC 1.0 床 head+foot 双格横置，如门但水平相邻）。每半格存
    //   state——bit3 = head(1)/foot(0)、bit[1:0] = head→foot 方向（0=+X 1=-X 2=+Z 3=-Z，与 door/trapdoor/chest
    //   同源编码值域；语义 = 从 head 半指向 foot 半的水平轴向）。t496：head 落 foot 的「玩家朝向同向」水平邻格
    //   （玩家面 +X 时 foot 在脚下、head 在 +X 前方 → 头朝远离玩家躺下，机制等价 MC 床头朝玩家前方）。放置时
    //   bedFacing = horizontalFacing ^ 1（玩家前向反向 = head→foot 方向），playercontroller placeBlock 处定权。
    //   bedPartnerOffset(state)：给定本格 state，返回配对格（另一半）相对本格的水平 (dx,dz)（y 同层故 dy=0）；
    //   playercontroller 放置 / 破坏联动读此单一权威，不各处自写朝向解码（同 chestFrontFace 模式）。
    static void bedPartnerOffset(quint8 state, int &dx, int &dz);

    // t397 花方块段哨兵：id ∈ [FirstFlower, LastFlower] 为花色变体（4 色）。isFlower(id) 单一权威谓词供 worldgen /
    //   放置预检 / 未来花相关机制（如花生成染料、花蜜）判定「是否花」，避免各处自写 id 区间漂移（同 isBed /
    //   isCrossBillboard 模式）。连续段（无段外夹入），故裸区间判定即可；仍提供 isFlower 谓词作单一权威（改段时
    //   一处同步）。worldgen placeFlowers 据本谓词把各色花散布到草地（不各持 4 个 id 判定）。
    static constexpr int FirstFlower = FlowerRed;
    static constexpr int LastFlower  = FlowerWhite;
    static bool isFlower(quint8 blockId);

    // t507 蘑菇统一谓词（单一权威）：blockId == Mushroom（红，=48）或 BrownMushroom（白 / 棕，=115）即蘑菇。
    //   供 mesher cross 路由（已并入 isCrossBillboard，本谓词专供失撑掉落 / 放置预检等「蘑菇族」语义判定读，
    //   避免把「cross 渲染」与「蘑菇族机制」耦合 —— 未来若有不可食 / 不可入蘑菇汤配方的 cross 蘑菇变体，
    //   蘑菇汤配方 / 失撑掉落仍只读本谓词不误判）。两 id 不连续（Mushroom=48 夹中间、BrownMushroom=115 段末）
    //   故显式并判（同 isIce / isBed 段不连续并判模式）；改族时一处同步谓词即可。
    static bool isMushroom(quint8 blockId);

    // t413 垂直爬梯统一谓词（单一权威）：blockId == Ladder 即梯。供 PlayerController 爬升物理判定
    //   「玩家 AABB 覆盖的格是否梯」（入梯格 + 按前 → 向上爬）+ mesher cross 路由分流，避免各处自写 id 判定漂移
    //   （同 isBed / isCrossBillboard 模式）。单 id 故裸相等判定即可，仍提供谓词作单一权威（改 id 时一处同步）。
    static bool isLadder(quint8 blockId);
    // t485 TNT 统一谓词（单一权威）：blockId == TntBlock 即 TNT。供 PlayerController TNT 陷阱触发判定
    //   （扫玩家 footprint 格——压力板下垫 TNT 即引爆）+ EntityManager::detonateTntBlock + worldgen
    //   placeDesertTemple 写入，避免各处硬编码 TntBlock id 判定（同 isLadder 单 id 模式）。单 id 故裸相等
    //   判定即可，仍提供谓词作单一权威（未来追加 TNT 变体时一处同步）。
    static bool isTnt(quint8 blockId);
    // t486 发射器统一谓词（单一权威）：blockId == Dispenser 即发射器。供 PlayerController 发射器陷阱触发判定
    //   （扫玩家 footprint 格——压力板的 4 水平邻格之一 == Dispenser 即触发）+ worldgen placeJungleTemple 写入，
    //   避免各处硬编码 Dispenser id 判定（同 isTnt / isLadder 单 id 模式）。单 id 故裸相等判定，仍提供谓词作
    //   单一权威（未来追加发射器变体时一处同步）。
    static bool isDispenser(quint8 blockId);
    // t609 投掷器统一谓词（单一权威）：blockId == Dropper 即投掷器。供 PlayerController 发射器陷阱触发判定
    //   （scanDispenserTraps 扫玩家 footprint 格——压力板的 4 水平邻格之一为**发射器或投掷器**即触发；投掷器
    //   分支 = 全部物品弹出掉落物，无弹丸分派）+ 右键开 UI / 放置朝向 / 破块掉内容判定，避免各处硬编码
    //   Dropper id 判定（同 isDispenser 单 id 模式）。
    static bool isDropper(quint8 blockId);
    // t569 红石矿石统一谓词（单一权威）：blockId == RedstoneOre 即红石矿。供 PlayerController 的走过 / 挖掘
    //   触发点亮判定（scanRedstoneOre footprint 扫描 + updateMining 目标判定）+ World worldgen scatterOres 写入
    //   读，避免各处硬编码 RedstoneOre id 判定漂移（同 isTnt / isLadder 单 id 模式）。
    static bool isRedstoneOre(quint8 blockId);
    // t620 红石灯统一谓词（单一权威）：blockId == RedstoneLamp 即红石灯。供 PlayerController placeBlock useBlock
    //   分支判定「右键命中格是否红石灯 → 翻开关态」（避免各处硬编码 id 判定漂移，同 isTnt / isLadder 单 id
    //   模式）。单 id 故裸相等判定即可，仍提供谓词作单一权威（未来追加变体时一处同步）。
    static bool isRedstoneLamp(quint8 blockId);
    // t490 手动 TNT 点火机关统一谓词（单一权威）。Lever / WoodButton / StoneButton 三者机制等价 MC 1.0 lever /
    //   wooden button / stone button——右键激活即点燃水平四邻 TNT（本项目无红石，故把「激活脉冲」直接绑在右键动作）。
    //   isLever / isWoodButton / isStoneButton 各单 id 裸相等判定；isManualIgniter（任一机关）供 placeBlock useBlock
    //   分支统一判定「右键命中的机关方块 → 点燃邻接 TNT」，避免三处硬编码 id 判定漂移（同 isTnt 单 id 模式 +
    //   聚合谓词模式，单一权威，未来追加机关变体时一处同步）。
    static bool isLever(quint8 blockId);
    static bool isWoodButton(quint8 blockId);
    static bool isStoneButton(quint8 blockId);
    static bool isManualIgniter(quint8 blockId); // 任一手动点火机关（Lever / WoodButton / StoneButton）
    // t487 要塞结构方块统一谓词（单一权威）：blockId == EndPortal 即末地传送门。供 PlayerController placeBlock
    //   useBlock 分支判定「右键命中格是否末地传送门 → 持末影之眼激活」（避免各处硬编码 id 判定漂移，同 isLadder
    //   单 id 模式）。单 id 故裸相等判定，仍提供谓词作单一权威（未来追加变体时一处同步）。
    static bool isEndPortal(quint8 blockId);
    // t474 书架统一谓词（单一权威）：blockId == Bookshelf 即书架。供 World::countBookshelvesAround（附魔台
    //   加成计算：扫切比雪夫半径 2 内书架数 ≤15）判定「是否书架」，避免各处硬编码 Bookshelf id 判定漂移
    //   （同 isLadder 单 id 模式）。单 id 故裸相等判定即可，仍提供谓词作单一权威（未来追加书架变体时一处同步）。
    static bool isBookshelf(quint8 blockId);
    // t474 附魔台统一谓词（单一权威）：blockId == EnchantingTable 即附魔台。供 playercontroller placeBlock
    //   useBlock 分支判定「右键命中格是否附魔台 → 开 EnchantingTableUI」（避免各处硬编码 id 判定漂移，同
    //   isLadder 模式）。单 id 故裸相等判定，仍提供谓词作单一权威（未来变体时一处同步）。
    static bool isEnchantingTable(quint8 blockId);
    // t477 铁砧统一谓词（单一权威，覆盖 3 损坏阶段 Anvil/AnvilChipped/AnvilDamaged）：供 playercontroller
    //   placeBlock useBlock 分支判定「右键命中格是否铁砧 → 开 AnvilUI」+ 破块掉落判定，避免各处硬编码 3 个
    //   id 判定漂移（同 isIce 三阶段模式）。三 id 不连续于单一区间（紧邻 97/98/99，实为连续段，故裸区间亦可，
    //   但仍提供谓词作单一权威，改段时一处同步）。
    static bool isAnvil(quint8 blockId);
    // t477 铁砧损坏阶段（0=完好 Anvil / 1=微损 AnvilChipped / 2=重损 AnvilDamaged）；非铁砧 → 0。
    static int anvilDamageStage(quint8 blockId);
    // t477 铁砧损坏 +1 后的目标方块 id：完好→微损 / 微损→重损 / 重损→Air（碎裂移除）。非铁砧 → 原 id。
    //   playercontroller damageAnvil 据本单一权威推进阶段（不各处自写 id 推进，同 bedPartnerOffset 模式）。
    static quint8 anvilNextStage(quint8 blockId);

    // t468 冰族统一谓词（单一权威，机制等价 MC 1.0 ice / packed ice / blue ice）：blockId == Ice / PackIce /
    //   BlueIce 即冰。供 PlayerController 冰滑行物理 + ItemEntityManager 物品冰摩擦 + mesher iceOnly 段路由
    //   + t469 船冰面加速（单一权威，避免各处自写三个 id 判定漂移，同 isBed / isWool 模式）。三个 id 不连续
    //   （Ice=45 夹中间，PackIce=91 / BlueIce=92 段末），故显式并判；改族时一处同步谓词即可。
    static bool isIce(quint8 blockId);
    // t468 冰面「滑动接近率」（1/s；越小越滑）：玩家水平速度向目标速度的指数接近速率。Ice 中等滑 / PackIce
    //   更滑 / BlueIce 最滑（机制等价 MC 1.0 ice < packed_ice < blue_ice 滑度递增）。非冰 → 0（caller 据此走
    //   瞬时设速的常规地面路径）。单一权威：玩家与（未来）船的冰面手感都读它，避免两处魔数漂移。
    static float iceSlipApproach(quint8 blockId);

    // t188 perf：流体类格子判定（Air / Water / Lava）。供 ChunkManager::setBlock 把 chunk 标「流体专用脏」
    //   （fluidOnlyDirty）—— 当一次写操作 oldId/newId **均**属流体类时，terrain/cross/glass/ice 段顶点不变
    //   （它们只画非流体方块），可跳过重建（水流风暴时一 tick 数百段无谓重建的真因）。三 id 非连续
    //   （Air=0 / Water=21 / Lava=31）故显式并判。仅这三者：冰（融化经 setWaterSilent 写 Water 触发本判定，
    //   oldId=Ice 非流体类 → fluidOnly=false → ice 段重建，正确）、玻璃（同）等均非流体类 → 固体路径重建。
    static bool isFluidLike(quint8 blockId);

    // t133 不完整方块段起止哨兵：id ∈ [FirstPartial, LastPartial] 走 PartialBlockGeometry 异形渲染
    //   （mesher 合批进 chunk mesh，不走 1×1×1 立方面路径）。t134 落地 6 类（WoodSlab=15 ... WoodTrapdoor=20）。
    //   机制等价 MC 1.0 (id, metadata) 方块模型：id ∈ [FirstPartial, LastPartial] 即「异形方块」（非整立方）。
    //   **必须用闭区间**：段后追加的方块虽 id 更大但**非异形** —— Water(21) 走水段、Chest(22) 是整立方
    //   ShapeFull 走 culled 立方面。旧「`b >= FirstPartial` 单边判定」会把这类段后整立方 / 非异形方块误
    //   路由进 PartialBlockGeometry（其 switch 无对应 case → 追加 0 顶点 → 渲染透明）。t194 箱子放置后
    //   透明（Chest 透视格子）即此根因。mesher / 选中框路由一律用 `>= FirstPartial && <= LastPartial`。
    static constexpr int FirstPartial = 15;
    static constexpr int LastPartial  = WoodTrapdoor; // 20（异形段上界；新增异形方块追加时同步右移）

    // t412 异形方块统一谓词（单一权威，段外圆石变体并入，同 isCrossBillboard 段外 cross 模式）：
    //   isPartialBlock：走 PartialBlockGeometry 异形渲染（mesher PASS 1 路由 + PASS 2 continue）。含连续段
    //     [FirstPartial, LastPartial]（6 类木制半方块）+ 段外圆石变体 4 类（CobbleSlab/Stairs/Fence/PressurePlate）。
    //     Farmland 经 chunkgeometry 单独并入 PASS 1（矮盒渲染，非 partial 子 AABB 形状），故不在此谓词内。
    //   isSlab/isStairs/isFence/isPressurePlate：placeBlock 放置态 / 双半砖合并 / 栅栏连接判定按形状分流，
    //     同形状不同材质（木 / 石）共用一套放置与合并逻辑（仅贴图 / 硬度 / 掉落差异走 BlockDef 表）。
    //   mesher / 选中框 / playercontroller 一律读本谓词，不各持 id 判定（PLAN §2 单一权威，避免多处分流漂移）。
    static bool isPartialBlock(quint8 blockId);
    static bool isSlab(quint8 blockId);
    static bool isStairs(quint8 blockId);
    static bool isFence(quint8 blockId);
    static bool isPressurePlate(quint8 blockId);

    // t466 门方块统一谓词（单一权威，段外云杉门并入，同 isBed / isCrossBillboard 段外模式）：
    //   blockId == WoodDoor（连续段内单一 id）或 SpruceDoor（段外）即门。供 playercontroller 的门放置
    //   （两格预检 + 双格写入）、右键开合（state 翻 bit2 + 配对格同翻）、破坏联动（破任一格清配对格）
    //   统一读「是否门」，避免各处硬编码 WoodDoor id 判定（同 isFence 把段外圆石墙 / 云杉栅栏并入的模式）。
    //   单 id 故裸相等判定即可，仍提供谓词作单一权威（未来追加新材质门时一处同步）。
    static bool isDoor(quint8 blockId);

    // t235 cross 广告牌方块段哨兵：id ∈ [FirstCross, LastCross] 走 PartialBlockGeometry 的 cross 几何
    //   （两片对角十字相交的双面 quad，机制等价 MC 草丛 / 花 / 作物的 cross 模型）。与 [FirstPartial, LastPartial]
    //   的「轴对齐盒体异形」**不同类** —— cross 是对角双面平面（非盒组合），故独立成段（避免与 partial 盒体几何混在
    //   同一 switch 误生成）。t235 落地 TallGrass=24；t236 追加 WheatCrop=25（同 cross 模型 + 按 state 阶段选贴图）。
    //   mesher 路由用闭区间 [FirstCross, LastCross]（同 partial 段教训 lessons-learned t194：闭区间防段后整立方误进
    //   cross 路径）。新增 cross 方块（花 / 其它作物）追加时右移 LastCross。
    //   **t305 树苗（Sapling=28）也是 cross 广告牌方块**，但其 id（28）不在 [FirstCross, LastCross]=[24,25] 连续段内
    //   （DiamondOre=26 / Wool=27 夹在中间，二者非 cross）→ 不能简单右移 LastCross（会把 26/27 误判为 cross）。故
    //   mesher 路由改用 isCrossBillboard(id) 谓词（见下）替代裸区间判定：[FirstCross,LastCross] ∪ {Sapling}。
    //   新增 cross 方块若 id 亦不连续，同样并入 isCrossBillboard（单一权威，避免 mesher / 选中框多处分流漂移）。
    static constexpr int FirstCross = TallGrass; // 24
    static constexpr int LastCross  = WheatCrop; // 25（cross 段上界；新增连续 cross 方块追加时同步右移）

    // t305 cross 广告牌方块统一谓词（单一权威）：true 表示该方块走 PartialBlockGeometry 的 cross 几何段
    //   （两片对角相交双面 quad）。涵盖连续段 [FirstCross, LastCross]（草丛 / 小麦作物）+ 段外 Sapling(28)
    //   + 段外 DeadBush(43)（t394）+ 段外 Mushroom(48)（t396）+ 段外 LilyPad(47)（t396：横向浮叶，几何为水平
    //   quad 非竖直 cross，但同走本路由 + alphaCutoff cutout 路径 —— PartialBlockGeometry::append 的 LilyPad
    //   case 内画水平 quad）+ 段外花段 [FirstFlower, LastFlower]（t397：4 色 cross）+ 段外 Sugarcane(53)（t397：
    //   细茎 cross，1..3 高叠柱）+ 段外 CarrotCrop(55)/PotatoCrop(56)（t407：作物 cross，同小麦作物按 state 选阶段贴图）
    //   + 段外 Ladder(62)（t413：木梯竖直爬行梯 cross）+ 段外 SweetBerryBush(90)（t467：雪原浆果灌木丛 cross，按 state 选 3 阶段贴图）。
    //   mesher（chunkgeometry 3 处路由）+ 选中框（Main.qml isCross 分流）一律读本谓词，不各持区间判定
    //   （PLAN §2：单一权威，避免「mesher 路由到 cross 但选中框仍按区间漏某方块」撕裂）。
    static bool isCrossBillboard(quint8 blockId);

    // t236 小麦作物生长阶段：state = 阶段 0..7（0=刚种嫩芽、7=成熟）。mesher（PartialBlockGeometry::append 的
    //   WheatCrop case）据 state 选对应阶段贴图（tile 29..36）；world.tickCropGrowth 据光强 + 耕地支撑 + 确定性
    //   散布概率把未成熟作物的 state 逐步 +1 直到本上界。state 经 m_states 落 SQLite round-trip 保真（存档读回
    //   仍带阶段 → 重载后继续生长 / 收割按阶段判成熟）。唯一消费点：partialblockgeometry 的 WheatCrop case（贴图
    //   选择）+ world tickCropGrowth（成长上界判定）+ t237 收割（state==max 判成熟掉小麦）。mesher / collisionAABBs /
    //   selectionAABBs 不读 wheat state（wheat 走 ShapeNone + cross 几何，state inert 于碰撞/选中）→ 复用 state 作
    //   阶段编码零回归（同 PlanksFromDoubleSlabBit / Farmland state 复用 state 作 marker 的模式）。
    // t407：本常量同时是**胡萝卜/马铃薯作物的共享阶段上界**（CarrotCrop/PotatoCrop 与小麦同走 MC 1.0「8 年龄、
    //   age 7 成熟」机制，state 编码与判定完全同小麦）。tickCropGrowth / 收割 / 几何阶段贴图选择对三种作物统一读
    //   本常量（不另立 CropStageMax，避免三处常量漂移；改名 WheatCropStageMax 会触动多文件故保留原名 + 本注释）。
    static constexpr quint8 WheatCropStageMax = 7;

    // t467 雪原浆果灌木丛生长阶段：state = 阶段 0..2（0=无果嫩丛、1=小果、2=成熟可采摘）。mesher
    //   （PartialBlockGeometry::append 的 SweetBerryBush case）据 state 选对应阶段贴图（tile 103..105）；
    //   worldgen placeSweetBerryBushes 散布阶段 1..2 的丛；玩家右键成熟丛（state==max）采摘得 2-3 浆果 + 丛回
    //   阶段 0（playercontroller useBlock 分支走 5 参数 setBlock 降阶段，id 不变只 state 变 + worldChanged 重建 mesh，
    //   同 t447 骨粉催熟模式）。state 经 m_states 落 SQLite round-trip 保真（存档读回仍带阶段）。唯一消费点：
    //   partialblockgeometry 的 SweetBerryBush case（贴图选择）+ playercontroller 采摘（state==max 判成熟）+
    //   环境伤害 tick（stage>0 踩过受少量伤害）。mesher / collisionAABBs / selectionAABBs 不读 bush state
    //   （bush 走 ShapeNone + cross 几何，state inert 于碰撞/选中）→ 复用 state 作阶段编码零回归
    //   （同 WheatCropStage / PlanksFromDoubleSlabBit 复用 state 的模式）。
    static constexpr quint8 SweetBerryBushStageMax = 2;

    // t310 草变种（矮/中/高）state 编码：mesher（PartialBlockGeometry::append 的 TallGrass case）据 state 选
    //   cross 高度。机制对标 MC 1.0 cross 草丛，但按用户要求做 3 高度变种（MC 1.0 原版仅 1 格满高 cross）。
    //   worldgen placeTallGrass 按群系分流密度写入对应 state（PLAN §2-K 确定性；新世界生效）。
    //   state 经 m_states 落 SQLite round-trip 保真。mesher 消费点：partialblockgeometry 的 TallGrass case
    //   （cross 高度选择）。collision/selection 不读 tall grass state（ShapeNone + cross 几何，state inert 于
    //   碰撞/选中）→ 复用 state 作变种编码零回归（同 WheatCropStage / Farmland state 复用模式）。
    static constexpr quint8 TallGrassVariantMax = 2; // variant 上界（state 越界 clamp 兜底）
    enum TallGrassVariant : quint8 {
        TallGrassShort  = 0, // 矮草：cross 半格高（0..0.5）
        TallGrassMedium = 1, // 中草：cross 满格高（0..1.0；与旧版满高草丛外观一致）
        TallGrassTall   = 2, // 高草：cross 两格高（0..2.0，顶点延伸进上格；机制等价 MC 高草 / large fern）
    };

    // t206 双半砖（合并态）state 标记：两块互补半砖（上+下）同格合并时，placeBlock 写 Planks(id=6) +
    //   本 bit 标记「源自双半砖」（旧实现写 Planks state=0 → 破块掉 1× Planks，用户报「双半砖挖掉掉 1 全木板」）。
    //   finishMiningAt 检本 bit → 破块掉 2× WoodSlab（机制等价 MC「double slab 破坏掉 2 块半砖」，非 1 块木板）。
    //   **为何标在 Planks 而非 WoodSlab 上**：合并结果在视觉 / 碰撞 / 剔除上本就是「满格整立方」（与 Planks
    //   同走 solid=true 立方面剔除 → 相邻实体面正确剔除，无 z-fight；WoodSlab solid=false 会与邻实体整面 z-fight）。
    //   仅改掉落语义、不动渲染 / 碰撞 / 选中框 → 零回归。state 对 ShapeFull（Planks）本属 inert（mesher /
    //   collisionAABBs / selectionAABBs 均不读 state），此处复用作 marker，**唯一消费点**是 finishMiningAt 掉落判定。
    //   常规放置的 Planks state 恒 0（4 参数 setBlock 默认 / worldgen）→ 不误判为双砖。state 经 m_states 落 SQLite
    //   round-trip 保真（存档读回仍带本 bit → 重载后破块仍掉 2 块半砖）。
    static constexpr quint8 PlanksFromDoubleSlabBit = 0x01; // Planks state bit0 = 源自双半砖合并（仅 Planks 复用）
    // t412 双半砖合并泛化（圆石变体）：两块互补半砖同格合并 → 写入该半砖对应的「满格整立方」(WoodSlab→Planks /
    //   CobbleSlab→Cobble / SpruceSlab→SprucePlanks) + DoubleSlabMarkerBit 标记；finishMiningAt 检本 bit → 破块掉
    //   2× 对应半砖（机制等价 MC「double slab 破坏掉 2 块半砖」）。满格方块的 state 对 ShapeFull inert
    //   （mesher / collision / 选中均不读），复用 bit0 作 marker 零回归（同 PlanksFromDoubleSlabBit 模式；
    //   本常量值与之一致 = 0x01，木 / 石 / 云杉三族共用）。
    //   slabFullBlock(slabId)：半砖 → 其满格整立方（WoodSlab→Planks / CobbleSlab→Cobble / SpruceSlab→SprucePlanks；
    //   非半砖→Air）。
    //   fullBlockSlabDrop(fullId)：满格整立方 → 其半砖（Planks→WoodSlab / Cobble→CobbleSlab / SprucePlanks→SpruceSlab；
    //   非双砖源→0）。
    static constexpr quint8 DoubleSlabMarkerBit = 0x01; // 满格方块 state bit0 = 源自双半砖合并（Planks / Cobble 复用）
    static quint8 slabFullBlock(quint8 slabId);
    static quint8 fullBlockSlabDrop(quint8 fullId);

    // t234/t406 耕地湿润度 state 编码：state 低 2 位 = 湿润等级 0..3（FarmlandHydrationMask 取低 2 位）。
    //   机制等价 MC 1.0 farmland hydration（4 级湿润；越湿作物长得越快、湿润度由贴图色深肉眼可辨）：
    //   - level 0（干）：浅色翻耕干土（farmland_dry 贴图 + 顶点色不暗化）；
    //   - level 1..3（渐湿）：mesher 对 +Y 顶面顶点色按等级递增暗化（darker=wetter，机制等价 MC 耕地越湿越深）。
    //   等级由 World::farmlandHydrationLevel 据水源切比雪夫距离（同/下一层、半径 4 内）算出：dist 1→3、2→2、3→1、
    //   ≥4/无水→0。耕地时（playercontroller 锄头分支）写一次；World::tickFarmlandHydration 周期复算（动态补水：
    //   后放水 / 雨后水位变化 → 远水耕地渐干、近水耕地转湿，机制等价 MC 耕地随机 tick 补/失水）。
    //   **消费点**：ChunkGeometry 顶点色暗化（+Y 顶面据 level 调亮 ×{1.0,0.82,0.64,0.46}，进 greedy 合并键防
    //   不同湿润度共面误并）+ tickCropGrowth（小麦作物升阶段概率 × (1+level)：越湿长得越快）。
    //   collisionAABBs / selectionAABBs / raycastAABBs 不读 farmland state（farmland 走 ShapeFull + collision 特例，
    //   state inert 于碰撞/选中）→ 复用 state 作湿润编码零回归（同 PlanksFromDoubleSlabBit / torch state 复用模式）。
    //   state 经 m_states 落 SQLite round-trip 保真（存档读回仍带湿润等级）。旧存档（t234 单 bit 湿润）读回 state∈{0,1}
    //   → 经 tickFarmlandHydration 复算自动迁移到 4 级编码（无显式迁移代码：tick 周期覆盖）。
    static constexpr quint8 FarmlandHydrationMask = 0x03; // Farmland state 低 2 位 = 湿润等级（仅 Farmland 复用）
    static constexpr int  FarmlandHydrationMax  = 3;      // 湿润等级上界（level 3 = 最湿；dist 1 邻水）

    // t305 持久树叶 state 标记：bit0 = 玩家放置（持久，不参与自然衰减）。机制等价 MC 1.0「玩家放置的树叶不衰减」
    //   —— worldgen 生成的树叶 state=0（衰减候选：失去原木支撑即消失）；玩家创造模式放置的树叶写 state=本 bit
    //   （标记持久，decayLeavesAround 跳过 → 创造建筑用的悬空树叶不被清掉）。**唯一消费点**：
    //   World::decayLeavesAround（破原木后扫邻叶衰减，跳过 state bit0=1 的持久叶）。mesher / collisionAABBs /
    //   selectionAABBs 不读 leaves state（leaves 走 ShapeFull + culled 立方面，state inert 于渲染/碰撞/选中）
    //   → 复用 state 作持久标记零回归（同 PlanksFromDoubleSlabBit / torch state 复用 state 作 marker 的模式）。
    //   state 经 m_states 落 SQLite round-trip 保真（存档读回仍带持久标记 → 重载后创造树叶仍不衰减）。
    //   常规（worldgen / 自然）树叶 state 恒 0 → 衰减候选。placeBlock 对 Leaves 显式写 PersistentLeafBit。
    static constexpr quint8 PersistentLeafBit = 0x01; // Leaves state bit0 = 玩家放置（持久，不衰减；仅 Leaves 复用）

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

    // 工具类型（决定哪类工具给方块挖掘**速度加成**；BlockDef.toolType 与 ToolRegistry::ToolDef.type 共用同一枚举）。
    // 放 Core 层是因为它属「方块的采掘属性」。当前 5 档：镐（石类）/ 斧（木类）/ 铲（土沙草类）/
    // 剑（攻击，不参与挖掘速度）/ 锄（专用耕地，不参与挖掘）。机制等价 MC 1.0 工具类型分流。
    // **toolType 只决定速度加成，掉落是否依赖工具看 BlockDef.requiresTool**（t265 解耦）：木 / 土 / 沙类
    //   方块 toolType=Axe/Shovel 但 requiresTool=false → 空手仍掉落、仅速度无加成；石类 toolType=Pickaxe
    //   且 requiresTool=true → 空手不掉落且无加成。
    enum ToolType : int {
        NoTool  = 0, // 无有效工具：任何手持物均无速度加成（基准速 1.0）；空手可采且掉落
        Pickaxe = 1, // 镐：石类方块加速（stone / cobble / furnace / coal_ore / iron_ore / copper_ore / gold_ore / diamond_ore；
                     //   requiresTool=true 需镐才掉落）
        Hoe     = 2, // 锄：专用耕地（右键草/泥土→耕地，机制留后续任务）。**不参与挖掘速度**——
                     //   本工程无任何方块的 BlockDef.toolType 取 Hoe（耕地是非方块语义、走 useBlock 交互，
                     //   非「采掘所需工具」），故 ToolRegistry::miningSpeedMul 对持锄挖任何方块恒返 1.0
                     //   （等同空手），机制等价 MC 1.0「锄不影响挖掘」。
        Axe     = 3, // 斧：木类方块加速（原木 / 木板 / 工作台 / 箱 / 木台阶 / 楼梯 / 栅栏 / 压力板 / 门 / 活版门；
                     //   requiresTool=false → 空手也掉落，仅速度受斧影响）。t265 落实方块 toolType→Axe 映射。
        Shovel  = 4, // 铲：土沙草类方块加速（沙 / 泥土 / 草方块 / 耕地；requiresTool=false → 空手也掉落）。
                     //   t265 落实方块 toolType→Shovel 映射。砾（gravel）方块待后续追加。
        Sword   = 5, // 剑：攻击伤害加成（ToolRegistry::attackDamage，t265 落实），**不参与挖掘速度**——
                     //   本工程无任何方块的 toolType 取 Sword（剑是武器、非采掘工具），miningSpeedMul 恒 1.0。
                     //   机制等价 MC 1.0「剑不加速挖掘（蛛网除外），其价值在攻击伤害」。
        Shears  = 6, // 剪刀（t300）：专用剪羊毛（右键羊→剪羊毛，EntityManager shearSheep）+ 给羊毛方块挖掘
                     //   速度加成（Wool.toolType=Shears；requiresTool=false → 空手也掉落，仅速度受剪刀影响）。
                     //   机制等价 MC 1.0 剪刀（shears：剪羊毛 / 加速羊毛 / 加速蛛网）。**唯一**取本类型的方块是
                     //   Wool；其余方块持剪刀 miningSpeedMul 恒 1.0（类型不匹配，等同空手）。
        Bow     = 7, // 弓（t304）：远程武器（右键长按拉弓 → 松开射箭）。**不参与挖掘速度**——本工程无任何
                     //   方块的 BlockDef.toolType 取 Bow（弓是远程武器、非采掘工具），miningSpeedMul 恒 1.0
                     //   （等同空手）。机制等价 MC 1.0「弓不影响挖掘」。仅用作 ToolRegistry::ToolDef.type 标识，
                     //   供 ToolIcon / 手持 3D 几何（BowGeometry）/ tooltip 据 toolType===Bow 分流到弓形渲染。
                     //   真实伤害 / 速度来自拉弓蓄力（PlayerController bow draw），不走 attackDamage（弓近战 = 徒手）。
        FishingRod = 8, // 钓鱼竿（t401）：右键抛浮标入水 → 等咬钩 → 拉起获物（t393 战利品表）。**不参与挖掘速度**——
                     //   本工程无任何方块的 BlockDef.toolType 取 FishingRod（钓竿是功能工具、非采掘工具），miningSpeedMul
                     //   恒 1.0（等同空手）。机制等价 MC 1.0「钓竿不影响挖掘」。仅用作 ToolRegistry::ToolDef.type 标识，
                     //   供 ToolIcon / tooltip 据 toolType===FishingRod 分流到钓竿图标渲染。获物 / 时序由 PlayerController
                     //   抛竿 / 拉起驱动（不走 attackDamage，钓竿近战 = 徒手）。
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

    // t146 方块碰撞 / 选中形状（决定 collisionAABBs/selectionAABBs）。机制等价 MC「方块 VoxelShape」：
    //   完整立方（常规方块）走 ShapeFull；air/torch 走 ShapeNone（无碰撞 sub-AABB，torch 选中框由
    //   Main.qml isTorch 分支特殊定向、不走 selectionAABBs 几何）；不完整方块段（id >= FirstPartial）
    //   各 shape 复用 partialblockgeometry.cpp 的 state 解码生成对应子 AABB（slab/stairs/fence/plate/
    //   door/trapdoor），使「碰撞/选中形状」与「渲染形状」同源 —— 改一处 state 编码须同步另一处。
    //   分层：本枚举属 Core（仅数据属性），不依赖 Renderer/Mesher。
    enum Shape : int {
        ShapeNone     = 0, // air / torch：无碰撞 sub-AABB（torch 不挡玩家；选中框由 Main.qml isTorch 分支）
        ShapeFull     = 1, // 常规整立方：collision/selection = {0,0,0,1,1,1}
        ShapeSlab     = 2, // 木板台阶：半高（state bit0=上半 → {0,0.5,0,1,1,1}；下半 → {0,0,0,1,0.5,1}）
        ShapeStairs   = 3, // 木板楼梯：整步 + 背墙（朝向 state[1:0]；bit2=倒置 → 整步/背墙 y 区间垂直镜像）
        ShapeFence    = 4, // 木栅栏：中心立柱 {0.3,0,0.3,0.7,1.5,0.7}（t209 1.5 高不可越；横档纯视觉不进碰撞）
        ShapePlate    = 5, // 木板压力板：贴地薄板 {0.0625,0,0.0625,0.9375,0.0625,0.9375}
        ShapeDoor     = 6, // 木板门：满高薄板（state 朝向/开合；上下半由所在格的 y 自然分，bit3 标识上下）
        ShapeTrapdoor = 7, // 木活板门：合=水平薄板 / 开=竖直薄板（state bit0 开合 + bit[2:1] 朝向）
        ShapeBed      = 8, // t457 床：低 3D 模型（~0.3 格高 = 四角木柱腿 + 木板面 + 羊毛面，上方留空气可躺）。
                           //   collision/selection = cell 底低盒 {0,0,0,1,0.3125,1}（玩家立于床垫顶 = 床顶 ~0.31 高，
                           //   机制等价 MC 床矮半高 hitbox；solid=false 避免相邻整立方误剔面出 x-ray 洞，同 Farmland /
                           //   Cactus 模式）。渲染走 PartialBlockGeometry（legs+plank+wool 子盒），不走整立方面。
                           //   state bit[1:0]=朝向、bit3=head(1)/foot(0)（同 door；head 半加枕头枕垫区分头/脚）。
        ShapeSnowLayer = 9, // t505 积雪层薄层：贴地薄板，高度由 state 驱动（state 0..7 → 高度 (state+1)/8，
                            //   1/8 .. 1.0 八级；机制等价 MC 1.0 snow layer 8 层堆叠）。solid=false（同 Farmland /
                            //   glass —— 非满格 → 相邻整立方不剔面、画出满高侧壁填住薄层上方的缺口，防透视 x-ray
                            //   洞）。渲染走 PartialBlockGeometry（顶面 snow 贴图的薄板 pushBox，y[0,height]）。
                            //   collision/selection/raycast = cell 底薄板 {0,0,0,1,height,1}（玩家立于薄层顶 = cell+height；
                            //   机制等价 MC 薄雪层可踩 + 半格平滑 auto-step 上行；高度 ≤0.5 时玩家 t163 auto-step
                            //   抬升 0.55 即可跨过，无需跳）。state 经 m_states 落 SQLite round-trip 保真。
    };

    // t505 积雪层（SnowLayer）层数上界（state 0..7 = 8 级高度）。机制等价 MC 1.0 snow layer 8 层
    //   （state 0=1 层雪 / state 7=8 层雪 ≈ 满格）。state 经 m_states 落 SQLite round-trip 保真。
    //   唯一消费点：snowLayerHeight（state → 高度比）、playercontroller 雪层掉雪球（state+1 个雪球）、
    //   mesher PartialBlockGeometry SnowLayer case（薄板高度）。collision/selectionAABBs/solidTopOffset
    //   经 snowLayerHeight 复用，单一权威（改高度映射只改本函数）。state 越界 clamp 到 0..7 兜底。
    static constexpr quint8 SnowLayerStageMax = 7;

    // t505 积雪层 state → 薄板高度（cell-local [0,1]，state 0..7 → 1/8..1.0）。state 越界 clamp 到
    //   [0, 7] 兜底。mesher / collision / selection / solidTopOffset / worldgen / playercontroller 掉落
    //   统一读本函数（单一权威，避免各处自写 (state+1)/8 漂移）。
    static float snowLayerHeight(quint8 state);

    // t146 方块子碰撞/选中盒（**cell-local [0,1]^3 AABB**；世界坐标由 caller + (bx,by,bz) 偏移）。
    //   min/max 各轴，min <= max。完整方块单盒 {0,0,0,1,1,1}；异形方块可能多盒（stairs = 下步 + 背墙）。
    //   纯数据结构（POD），可值拷贝进 std::vector 返回。Core 层定义，World/Physics 只读消费。
    struct BlockAABB {
        float minX, minY, minZ;
        float maxX, maxY, maxZ;
    };

    // 方块定义（每方块一项；单一权威数据源）。改方块任何属性只改 kDefs 一行，全工程生效。
    // 行索引 == 方块 id（air 行同时作越界 / 不可挖掘 / 不掉落兜底）。
    struct BlockDef {
        int id;              // 方块 id（= 行索引；自描述，便于日志 / 调试对照）
        int topTile;         // +Y(Top) 图集瓦片序号
        int bottomTile;      // -Y(Bottom) 瓦片序号
        int sideTile;        // +X / -X / +Z（三个侧面统一）瓦片序号
        int frontTile;       // -Z(NegZ「前面」) 瓦片序号（熔炉炉口等有朝向的方块用）；多数 == sideTile
        bool solid;          // 实体（参与碰撞 / culled 面剔除）；air=false。t146 注：solid 仅作 **mesher
                             //   邻居面剔除** 的依据（不完整方块 solid=false → 不挡邻居整面，避免相邻整立方
                             //   被误剔出洞）。**碰撞**改走 shape（solid=false 的不完整方块仍有碰撞 sub-AABB）。
        Shape shape;         // t146 碰撞/选中形状（Shape 枚举）。决定 collisionAABBs/selectionAABBs。
        float hardness;      // 基础硬度（挖掘耗时基准；<=0 → 不可挖掘，canMine=false）
        int toolType;        // 「有效工具」类型（ToolType；决定哪类工具给挖掘**速度加成**，t265：斧→木 / 铲→土沙草 / 镐→石）。
                             //   NoTool=无任何工具给加成（空手即基准速）。**与 requiresTool 正交**：toolType 只管速度，
                             //   能否掉落看 requiresTool（木 / 土 / 沙虽 toolType=Axe/Shovel 但 requiresTool=false → 空手仍掉落）。
        int minToolTier;     // 采掘所需最低工具等级（仅 requiresTool=true 时才作「掉落 + 速度」的等级门槛；
                             //   requiresTool=false 时忽略，任意等级的正确类型工具均给速度加成）
        bool requiresTool;   // t265 「掉落是否需要匹配工具」：true=必须持 toolType 且 tier>=minToolTier 才掉落
                             //   （石 / 圆石 / 矿石 / 熔炉等石类）；false=空手也掉落（木 / 土 / 沙 / 草 —— 速度受工具
                             //   影响，但产物不依赖工具，机制等价 MC 1.0「不需工具方块空手可采且掉落」）。
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
    //   19=water（t148；6 面同贴图，蓝半透——纹理本身不透明，半透由材质 opacity=0.7 实现）
    //   20=chest_top / 21=chest_side / 22=chest_front（t173；箱子顶=盖缝+铰链、侧=铁箍带、前=锁孔）
    //   23=water_flow（t197；**流水专用**贴图，不绑定任何方块 id——Water 方块 def 仍各面=19 静水；
    //      mesher 在水段按 cell 的 state 选 19(水源)/23(流水)。属渲染层呈现选择，非方块属性）。
    //   24=water_2（t223 静水动画第二帧；**tXXX 静态水后 mesher 不再引用**——flipbook 换帧重建已消除，
    //      水段恒用 19/23（phase 0 帧）。图集保留瓦片供将来 material 级动画 / 手工切换，索引不回收）。
    //   25=water_flow_2（t223 流水动画第二帧；同上，**tXXX 静态水后 mesher 不再引用**，图集保留）。
    //   26=farmland_dry（t234 耕地顶面干态；浅色翻耕干土，纵向犁沟纹）。
    //   27=farmland_wet（t234 耕地顶面湿态；深色湿润翻耕土，同犁沟纹 + 深色；mesher 据 Farmland state bit0
    //      选 26(干)/27(湿)；Farmland 方块 def topTile=26，tileFor 特例覆盖）。
    //   28=tall_grass（t235 草丛 cross 贴图；green 草叶 + alpha 透明底；cross 几何段材质 alphaCutoff cutout 透明底）。
    //   29..36=wheat_stage_0..7（t236 小麦作物 8 个生长阶段贴图；cross 几何段，alpha 透明底 cutout。mesher 在
    //      PartialBlockGeometry::append 的 WheatCrop case 内据 state 选 tile = 29 + stage；WheatCrop 方块 def 各面
    //      = 29（基底阶段 0），阶段贴图选择是 mesher 呈现层据 state 决定，非 BlockDef 字段——同 Water 流水贴图模式）。
    //   37=diamond_ore（t279 钻矿石；散布于 stone 深层 y∈[5,40]、需铁镐采掘；机制等价 MC 1.0 钻石矿，名称/贴图
    //      原创自绘 §9a；各面同贴图=石头底+青白菱斑晶体）。
    //   38=wool（t300 羊毛方块；剪羊毛 / 杀羊掉落；机制等价 MC 1.0 羊毛，名称/贴图原创自绘 §9a；
    //      各面同贴图=奶白羊毛底+浅灰卷曲绒毛纹）。
    //   39=sapling（t305 树苗 cross 贴图；棕色短树干 + 绿色嫩叶小球冠，alpha 透明底 cutout；
    //      Sapling 方块各面=本 tile，mesher 走 cross 几何段；机制等价 MC 1.0 橡树树苗，名称/贴图原创自绘 §9a）。
    //   40=copper_ore（t308 铜矿石；散布于 stone 浅中层 y∈[5,45]、需石镐采掘；机制等价 MC 1.0 铜矿，
    //      名称/贴图原创自绘 §9a；各面同贴图=石头底+橙铜斑+孔雀绿锈）。
    //   41=gold_ore（t308 金矿石；散布于 stone 深层 y∈[5,25]、需铁镐采掘；机制等价 MC 1.0 金矿，
    //      名称/贴图原创自绘 §9a；各面同贴图=石头底+金黄斑簇）。
    //   42=lava（t343 岩浆；各面同贴图=深红橙底+亮黄橙鼓泡+白炽热点，原创自绘 §9a；岩浆段材质 opacity≈0.95 近不透）。
    //   43..50=bed_red..bed_black（t387 床方块 8 色变体；简化单格整立方，机制等价 MC 1.0 床。各面同贴图=彩色被面底
    //      + 顶部枕垫亮带 + 绗缝针脚暗点 + 边缘暗化，原创自绘 §9a；tools/build_bed.py 程序生成。配方 planks+wool → 红床）。
    //   51=spawner（t392 刷怪笼；机制等价 MC 1.0 刷怪笼，名称/贴图原创自绘 §9a；各面同贴图=暗蓝灰底 + 铁灰栅栏
    //      + 中心青绿光斑；tools/build_spawner.py 程序生成）。
    //   52=sandstone_top / 53=sandstone_side（t394 砂岩；沙下成岩整立方；顶=压实沙面 / 侧=层理带；
    //      tools/build_sandstone.py 程序生成原创像素图）。
    //   54=cactus_top / 55=cactus_side（t394 仙人掌；接触伤害整立方；顶=绿截面环纹 / 侧=棱脊+刺点；
    //      tools/build_cactus.py 程序生成原创像素图）。
    //   56=dead_bush（t394 枯死的灌木 cross 贴图；透明底 + 棕褐放射干枝；alphaCutoff cutout；
    //      tools/build_dead_bush.py 程序生成原创像素图）。
    //   57=snow（t395 积雪层各面贴图；冷白底+细密冰晶噪点；SnowLayer 各面=本 tile；tools/build_snow.py 程序生成原创像素图）。
    //   58=ice（t395 冰各面贴图；浅蓝底+反光裂纹；Ice 各面=本 tile；tools/build_ice.py 程序生成原创像素图）。
    //   59=spruce_log_top / 60=spruce_log_side（t395 云杉原木；顶=深棕同心年轮截面 / 侧=深棕垂直树皮条带；
    //      区别于橡木原木 log_top/log_side 的浅棕；tools/build_spruce.py 程序生成原创像素图）。
    //   61=lily_pad（t396 睡莲 cross 路由的横向浮叶贴图；透明底 + 绿色圆叶 + V 形缺口，alphaCutoff cutout；
    //      LilyPad 各面=本 tile，mesher 走 isCrossBillboard 路由的 LilyPad 横向 quad case）。
    //   62=mushroom（t396 蘑菇 cross 贴图；透明底 + 米色菌柄 + 红底白斑菌盖，alphaCutoff cutout；
    //      Mushroom 各面=本 tile，mesher 走 cross 几何段）。
    //   63..66=flower_red/yellow/blue/white（t397 花 4 色变体 cross 贴图；透明底 + 绿茎 + 彩色花头，
    //      alphaCutoff cutout；各色 Flower 方块各面=本 tile，mesher 走 cross 几何段；tools/build_flower.py 程序生成原创像素图）。
    //   67=sugarcane（t397 甘蔗 cross 贴图；透明底 + 绿色节段细茎 + 顶部尖叶，alphaCutoff cutout；
    //      Sugarcane 各面=本 tile，mesher 走 cross 几何段；tools/build_sugarcane.py 程序生成原创像素图）。
    //   68=glass（t405 玻璃各面贴图；近白青底 + 暗边框 + 对角高光斜线；Glass 各面=本 tile，mesher 走 glassOnly
    //   69..72=carrot_crop_0..3（t407 胡萝卜作物 4 阶段贴图；cross 几何段，alpha 透明底 cutout。MC 1.0 carrot 4 张
    //      阶段贴图，每张覆盖 2 个年龄；mesher 在 PartialBlockGeometry::append 的 CarrotCrop case 内据 state 选
    //      tile = 69 + state/2。CarrotCrop 方块 def 各面=69（基底阶段 0））。
    //   73..76=potato_crop_0..3（t407 马铃薯作物 4 阶段贴图；同 carrot_crop 4 阶段机制；PotatoCrop def 各面=73；
    //      mesher 在 PotatoCrop case 内选 tile = 73 + state/2）。
    //      半透段；纹理不透明，半透由材质 opacity 实现，同 water 模式；tools/build_glass.py 程序生成原创像素图）。
    //   78=ladder（t413 木梯 cross 贴图；透明底 + 棕色两根纵轨 + 横向梯级，alphaCutoff cutout；Ladder 各面=本 tile，
    //      mesher 走 cross 几何段；机制等价 MC 1.0 梯子，名称/贴图原创自绘 §9a；tools/build_ladder.py 程序生成原创像素图）。
    //   79..93=wool_orange..wool_black（t455 16 色 wool 其余 15 色变体；white 复用 tile 38。卷绒纹 + 标准 16 色着色，
    //      原创自绘 §9a；tools/build_wool.py 程序生成。WoolOrange=63..WoolBlack=77 各面=本段对应 tile）。
    //   94..101=bed_white..bed_brown（t455 16 色床补齐 8 色新变体；既存 8 色床在 tile 43..50。与同色羊毛同色板，
    //      原创自绘 §9a；tools/build_bed.py 程序生成。BedWhite=78..BedBrown=85 各面=本段对应 tile）。
    //   103..105=sweet_berry_bush_0..2（t467 雪原浆果灌木丛 3 阶段贴图；cross 几何段，alpha 透明底 cutout。
    //      SweetBerryBush 方块 def 各面=103（基底阶段 0），mesher 在 PartialBlockGeometry::append 的
    //      SweetBerryBush case 内据 state 选 tile = 103 + stage（0/1/2）。机制等价 MC 1.0 sweet berry bush；
    //      名称/贴图原创自绘 §9a；tools/build_sweet_berry.py 程序生成原创像素图）。
    //   106=pack_ice（t468/t495 浮冰各面贴图；淡蓝白压实冰 + 密实细裂纹 + 反光高光，非白羊毛；
    //      tools/build_ice.py 程序生成）/ 107=blue_ice（t468 蓝冰各面贴图；淡蓝 + 纵向纹路，最滑）。
    //   108=lapis_ore（t471/t493 青金矿石各面贴图；非 pack 时=石头底 + 群青深蓝斑簇 + 黄铁矿金点，
    //      原创自绘 §9a；pack 激活时由 resourcepackmanager 用包内 lapis_ore.png（包内 stone 底纹 +
    //      青金斑，与普通石头风格一致 → 矿脉不再一眼可见）；LapisOre 各面=本 tile；tools/build_ore.py 程序生成）。
    //   117=pumpkin_side（t482 南瓜侧面贴图；橙色 + 纵向瓜棱深纹；tools/build_pumpkin.py 程序生成原创像素图）。
    //   118=pumpkin_face（t482 南瓜前面贴图；橙色 + 刻面双眼 + 锯齿嘴；Pumpkin frontTile=本 tile，机制等价
    //     MC 刻面南瓜 jack o'lantern；作造物头时面朝玩家侧）。
    //   119=pumpkin_top（t482 南瓜顶/底面贴图；橙色瓜顶 + 中央短茎；Pumpkin top/bottomTile=本 tile）。
    //   120=cobweb（t484 蜘蛛网 cross 贴图；透明底 + 灰白蛛丝放射网纹，alphaCutoff cutout；Cobweb 各面=本 tile，
    //      mesher 走 cross 几何段；机制等价 MC 1.0 cobweb；tools/build_cobweb.py 程序生成原创像素图）。
    //   121=rail（t484 铁轨贴地薄板 flat 贴图；透明底 + 棕色枕木 + 灰铁双轨，alphaCutoff cutout；Rail 各面=本 tile，
    //      mesher 走 PartialBlockGeometry Rail 水平 quad case；机制等价 MC 1.0 rail；tools/build_rail.py 程序生成原创像素图）。
    //   134=furnace_front_on（t494 熔炉点燃态前面贴图；圆石底 + 拱框 + 拱洞内亮黄橙火焰，机制等价 MC 1.0
    //     熔炉燃烧时正面发光；mesher 据 Furnace state 的 FurnaceStateLitFlag 选 14(灭)/134(点燃)；
    //     tools/build_furnace.py 程序生成原创像素图）。
    //   135=brown_mushroom（t507 白蘑菇 / 棕蘑菇 cross 贴图；透明底 + 米色菌柄 + 棕色菌盖白斑，alphaCutoff cutout；
    //     BrownMushroom 各面=本 tile，mesher 走 cross 几何段；机制等价 MC 1.0 brown mushroom；
    //     tools/build_brown_mushroom.py 程序生成原创像素图）。
    //   136=rail_corner（t565 铁轨 90° 拐角贴图：双轨自南边进入向左（西）弯出；透明底 + 棕色枕木 + 灰铁双轨，
    //     alphaCutoff cutout；不绑定 BlockDef 瓦片字段 —— mesher 据铁轨 state 连接位选 121(直 NS)/UV 旋转(直 EW)/
    //     136(拐角)/137(十字)，同 Water 流水贴图 19/23 的「呈现层据 state 选瓦片」模式；tools/build_rail.py 生成）。
    //   137=rail_cross（t565 铁轨十字交叉贴图：南北 + 东西双轨叠交 + 中央方枕木；同上不绑定 BlockDef）。
    // 图集由 tools/build_atlas.py 打包全部 138 瓦片；mesher / BlockCube 都读本常量算每瓦片 UV
    //   宽 1/AtlasTileCount —— **单一权威**，与 build_atlas.py 的 TILES 长度严格对齐。
    // -Z 面（NegZ「前面」）走 frontTile（熔炉炉口；其余方块 frontTile == sideTile，无视觉差异）。
    static int tileIndex(quint8 blockId, Face face);

    //   109=enchanting_table_top（t474/t620 附魔台顶面贴图；非 pack = 黑曜石深紫黑底 + 钻石青白菱斑 + 顶部立书轮廓，
    //      原创自绘 §9a；pack = enchanting_table_top.png；EnchantingTable 顶面=本 tile；tools/build_enchanting_table.py）。
    //   110=enchanting_table_side（t474/t620 附魔台侧面贴图（底面复用 obsidian(77)；非 pack = 黑曜石深紫黑底 +
    //      钻石嵌点 + 边缘暗化，原创自绘 §9a；pack 合成时裁掉 enchanting_table_side.png 顶部 0.25 空白
    //      → 有效 0.75 部分整张贴 0.75 高侧面（无缝，机制等价 MC 附魔台 12/16 高侧贴图）；EnchantingTable
    //      底=obsidian(77)/侧=本 tile；tools/build_enchanting_table.py）。
    //   111=bookshelf（t474/t620 书架侧面贴图；木板边框 + 中央书脊彩色书列（红 / 蓝 / 绿 / 棕书脊），原创自绘
    //      §9a；t620 起书架 per-face：侧/前=本 tile、顶/底=planks(8)（机制等价 MC bookshelf 顶底木板）；
    //      tools/build_bookshelf.py 程序生成）。

    // 图集瓦片总数（atlas.png 横排瓦片数 = 最大 tile 序号 + 1；当前 138）。
    //   **单一权威**：mesher(chunkgeometry) 与 BlockCube（第一/第三人称手持 + 掉落/下落实体）
    //   都读本常量算每瓦片 UV 子区宽 1/AtlasTileCount。消除「mesher 与 BlockCube 各持一份魔数、
    //   加新瓦片后忘记同步一份」的复发 bug 类——历史已踩 3 次（t54: 10→12、t148: 12→20、t173: 20→23
    //   漏改 BlockCube → 手持/掉落物贴图「杂交」：UV 按 1/20 算偏宽、tile t 采到 [t/20,(t+1)/20] 而真实
    //   瓦片在 [t/23,(t+1)/23] → 泥土采到半块石头、树叶采到木板，肉眼「不是实际方块」）。
    //   .cpp 内 static_assert 守卫：kDefs 任一 tile 字段 >= AtlasTileCount → 编译失败（防 tile 越界）。
    //   新增瓦片时同步改本常量 + tools/build_atlas.py 的 TILES（两处须一致）。
    //   t565：136=rail_corner / 137=rail_cross（铁轨拐角 / 十字贴图；不绑定 BlockDef 瓦片字段，mesher
    //   据铁轨 state 选 121/136/137 —— 同 Water 流水贴图模式）。136..137 尚无 BlockDef 引用故
    //   static_assert 仍只守到 135，本常量先就位供 mesher 引用。
    //   t569：138=redstone_ore（红石矿石贴图；石头底 + 鲜红菱斑矿粒，复制钻石矿斑块布局改红；
    //   tools/build_ore.py 程序生成；RedstoneOre 各面=本 tile）。
    //   t609：139=dropper_front（投掷器前面（排出口所朝面）贴图；石质灰底 + 中央小方形暗孔（比发射器的大
    //   暗腔排出口更小更简的轻量出口读感）；Dropper 前面=本 tile（mesher 据 state 选，同发射器 tileFor 分支）；
    //   顶/底/侧复用熔炉 12/13；tools/build_dropper.py 程序生成）。
    //   t620：140..142=末影祭坛三张（EndPortal 方块的 endframe 化视觉：140=endframe_side 侧/底（灰白细孔
    //   框身）/ 141=endframe_top 顶（未放末影之眼：框面 + 中央暗绿凹槽）/ 142=endframe_eye 顶（已放之眼：
    //   框面 + 中央之眼亮纹，mesher tileFor 据 EndPortal state bit0 选 141/142）；tools/build_endframe.py
    //   程序生成原创像素图）。
    //   t620：143..146=门上下半 per-face 四张（机制等价 MC 1.0 门两格高：143=door_wood_upper（橡木上半
    //   格栅窗）/ 144=door_wood_lower（橡木下半锁孔板）/ 145=door_spruce_upper / 146=door_spruce_lower（云杉
    //   深冷棕同布局）；PartialBlockGeometry door case 据 state bit3 选 upper/lower——kDefs 的 WoodDoor/
    //   SpruceDoor topTile=upper / bottomTile=lower 承载该选择；tools/build_door.py 程序生成）。
    //   t620：147..151=矿物存储块五张（coal/lapis/diamond/gold/redstone block 六面同；机制等价 MC 1.0
    //   9↔1 压缩存储方块。铁块 tile 112 既存 t477）/ 152=redstone_lamp_off（红石灯 off 态灰暗壳）/
    //   153=redstone_lamp_on（红石灯 on 态暖黄亮芯；mesher tileFor 据 RedstoneLamp state bit0 选 152/153）；
    //   tools/build_mineral_blocks.py 程序生成原创像素图。
    //   t627：154..156=压力板家族扩展三张（stone/iron/gold pressure plate——贴地薄板专用瓦片：边框暗带 +
    //   中央板面，材质色区分「石灰 / 金属铆钉 / 亮金」；StonePressurePlate/IronPressurePlate/
    //   GoldPressurePlate 各面=本族 tile；mesher 走 PartialBlockGeometry plate case，踩下态 state bit0
    //   压半高；tools/build_pressure_plates.py 程序生成原创像素图）。
    //   t638：157..163=铁轨家族扩展 + 红石火把 + 附魔台翻页书 + 仙人掌底面七张：
    //   157=rail_golden（动力铁轨断常态：金轨双线 + 石枕；GoldenRail 各面=本 tile；mesher 直线 UV 同 Rail
    //       直轨；pack {157→powered_rail.png}）、158=rail_detector（探测铁轨断常态：铁轨 + 亮红探测点；
    //       pack {158→detector_rail.png}）、159/160=两轨**通电视觉**变体（rail_golden_on / rail_detector_on
    //       ——本工程动力轨恒断电（无红石信号）不消费 159（留图集备用）；探测轨矿车驶过 state bit0 →
    //       mesher 换 160（pack {160→detector_rail_on.png}））、161=redstone_torch（红石火把 cross 贴图：
    //       透明底 + 深棕柄 + 亮红焰头；pack {161→redstone_torch_on.png}）、162=enchant_book（附魔台顶摊开
    //       书两页：白纸底 + 灰字线 + 中央书脊——PartialBlockGeometry EnchantingTable case 顶书盒专用；
    //       无 pack 等价（MC 书是独立实体模型非方块贴图）→ 程序贴图恒用）、163=cactus_bottom（仙人掌底面：
    //       更暗绿截面（区别 top 的中央凹陷）；Cactus bottomTile=本 tile——观察者视角可见柱底；pack
    //       {163→cactus_bottom.png}）。tools/build_rail_family.py / build_book.py / build_cactus.py 程序生成。
    static constexpr int AtlasTileCount = 164;

    // t489 流体条带动画（材质级 flipbook，替代 t222/t223 重建式水动画）——水/岩浆段改采样**独立条带纹理**
    //   （不走共享图集 voxelAtlas），面 UV 烘焙为「单帧区域」v∈[0,1/N]（帧 0 区），帧切换由材质
    //   positionV 动画（QtQuick3D Texture 在 6.11 已把 vOffset 更名 positionV）驱动——**mesh 一次性构建、
    //   动画纯材质参数，零 buildMesh**（F3 [w]/[s] reb 不回升）。
    //   **单一权威**：条带构建方（resourcepackmanager：包内帧缩放拼条带 / tools/build_fluid_strips.py：
    //   程序生成条带）与消费方（chunkgeometry UV 烘焙 + Main.qml positionV 动画）三方都读本常量算帧区
    //   宽 1/N。改帧数必须三方同步——否则 UV 子区与 positionV 步长错配 → 采到相邻帧或帧间缝。
    //   - kWaterStripFrames=32：水条带 32 帧（静水列 32 帧 + 流水列 32 帧，2 列各 32 帧）。MC 1.0 静水
    //     flipbook 为 32 帧（frametime=2 tick），demo 包 water_still.png 实测 16×512 = 32 帧、water_flow.png
    //     32×1024 = 32 帧 → 包内帧数天然与本常量对齐；包内帧不足时 resourcepackmanager 末尾补齐（循环）。
    //   - kLavaStripFrames=16：岩浆条带 16 帧（单列）。MC 1.0 岩浆 flipbook 为 16 帧（demo 包 lava_still.png
    //     16×320 = 20 帧、lava_flow.png 32×512 = 16 帧）→ 取前 16 帧（机制对齐 MC 1.0 16 帧前向循环，
    //     非 1.13+ 的 ping-pong；demo 包多出的 4 帧 + 回放被裁）。
    //   - kFluidStripFramePx=16：条带帧像素边长（与图集瓦片 kTile=16 同源；包内帧 >16 缩放到 16）。
    static constexpr int kWaterStripFrames = 32;
    static constexpr int kLavaStripFrames = 16;
    static constexpr int kFluidStripFramePx = 16;

    // 方块是否实体（参与碰撞 / culled 面剔除）。air 恒 false；torch 亦 false（非实体、不挡邻居面）；
    // 其余填表 solid=true。越界/未知 id 返回 false。mesher 邻居面剔除走本谓词（单一权威），
    //   切勿在渲染层另写 `!= 0`（会把 torch 当 solid → 误剔邻居面 → 透明 bug，见 t130）。
    static bool isSolid(quint8 blockId);
    // t146 方块碰撞/选中形状（BlockDef.shape；越界 → air 行 = ShapeNone）。
    static Shape shape(quint8 blockId);
    // 方块是否「有碰撞 sub-AABB」（考虑开合态）：决定 collisionAABBs 是否非空 + World::isCollidable
    //   预判。单一权威：isCollidable 与 collisionAABBs 共用，保证「预判」与「精确碰撞」对开合态同源。
    //   - 门（ShapeDoor）：**恒 true** —— 门板无论开合都实存于某一边（合=贴朝向边、开=旋 90° 贴铰链侧
    //     邻边，几何见 shapeBoxes 的 ShapeDoor 分支 + partialblockgeometry WoodDoor 渲染同源），玩家撞
    //     门板那两面被挡、门板切线方向（含开门后的「门洞」方向）可穿过。t261 修「开门四向全通」：旧名
    //     isCollidableWhenClosed 对开门返 false → collisionAABBs 空 → 门板凭空消失、四向皆通；改恒 true 后
    //     collisionAABBs 返回 shapeBoxes 算出的「旋后贴边」panel AABB，开门仍挡铰链那一面（机制等价
    //     MC 门打开后门板贴墙仍挡一面、仅门洞方向可过）。函数随之从 isCollidableWhenClosed 改名 isCollidable
    //     （「开态通」对门已不成立，旧名误导）。
    //   - 活版门（ShapeTrapdoor）：**恒 true**（t359）—— 合=水平贴地板（顶面站立）/ 开=铰链侧整高竖直板
    //     （「半门 / 1 格高 ledge」：玩家立于板顶 y=1.0 + 蹲行走，不再穿透）。t335 的「铰链侧 3/16 唇边」特例
    //     （薄于玩家 footprint 半宽 0.3 → 无真支撑穿透）已废，碰撞改走 shapeBoxes 整高竖直板（与渲染 /
    //     selectionAABBs 三者同源）。
    //   - 其余有碰撞形状（Full/Slab/Stairs/Fence/Plate）→ true（无开合概念，恒挡）。
    //   - air / torch / water（ShapeNone）→ false。
    //   越界 → false（air 兜底）。
    static bool isCollidable(quint8 blockId, quint8 state);
    // t146 方块碰撞 sub-AABB（cell-local [0,1]^3；复用 partialblockgeometry state 解码 → 与渲染形状同源）。
    //   玩家碰撞迭代玩家 AABB 覆盖的格子，逐 sub-AABB（+ 格偏移到世界坐标）做 3 轴重叠测试。
    //   air/torch → 空；常规整立方 → 单盒 {0,0,0,1,1,1}；异形 → 形状对应的多盒（stairs 2 盒 等）。
    //   越界 / air 行 → 空。机制等价 MC「方块 VoxelShape」（机制对齐，非名词照搬）。
    static std::vector<BlockAABB> collisionAABBs(quint8 blockId, quint8 state);
    // t146 方块选中框 sub-AABB（cell-local [0,1]^3；选中框线框按此画棱）。当前与 collisionAABBs 同数据
    //   （异形方块 VoxelShape 与 outline shape 多数一致）；分离接口备将来分歧（如某些方块选中框略放宽）。
    //   Main.qml 的 SelectionWireBoxes 几何据本方法画每个 sub-AABB 的 12 棱（贴合实际形状，非全格）。
    static std::vector<BlockAABB> selectionAABBs(quint8 blockId, quint8 state);

    // t213 方块是否占满整格立方（shape == ShapeFull）。R18d 三任务共用基础谓词（dev-plan t213/t220/t226）：
    //   - t213 射线穿「不完整方块的空气部分」命中后方块（isFullCube → 射线进格即中；否则须命中 sub-AABB）；
    //   - t220 沙子下落遇不完整方块 → 变掉落物（仅完整方块可支撑沙）；
    //   - t226 箱子上方完整方块 → 阻挡开盖（不完整方块上方可开）。
    //   water/torch/air（ShapeNone）→ false；不完整方块段（ShapeSlab/...）→ false；常规整立方
    //   （grass/stone/log/planks/.../chest 等）→ true。机制等价 MC「方块是否完整立方」。
    static bool isFullCube(quint8 blockId);

    // t334 per-block 光衰减量（lightOpacity；机制等价 MC 1.0 lightOpacity 0..15）：光穿入该格时损失的光级。
    //   flood-fill（World recomputeLightField / refloodBox）据本值算邻格衰减 = max(1, lightOpacity)，取代旧的
    //   isSolid 二值遮光（旧实现在所有 solid=false 的异形方块上恒「全透」，致合活版门 / 台阶也透光 —— 与形状语义矛盾）。
    //   state 参与：活版门开合态决定是否遮光。
    //   - 全实体方块（isSolid=true：草/泥/石/叶/箱/矿...）→ 15（满遮光，等同旧 isSolid 跳过；保既有行为）。
    //   - 活版门（WoodTrapdoor）：合态（state bit0=0）→ 15（合=水平贴地满遮光，修「合活版门仍透光」）；
    //     开态（bit0=1）→ 0（开=竖直贴边，光全透，保既有行为）。
    //   - 台阶（WoodSlab）→ 7（半遮光：占空比 0.5 → opacity = floor(0.5×15) = 7 → 衰减 7，满光 15→8 约半减，
    //     机制等价 MC「半砖按占空比衰减天光」）。
    //   - 其余（air/torch/water/stairs/fence/plate/door/cross）→ 0（全透，保既有行为 —— 本任务不动它们）。
    //   越界 → 0（air 兜底，等同全透）。**仅光照消费**：mesher 邻居面剔除仍走 isSolid（solid 字段语义不变），
    //   二者解耦（同 torch「solid=false 但参与剔除判定」既有模式；新增的遮光规则不污染面剔除）。
    static quint8 lightOpacity(quint8 blockId, quint8 state);

    // t351 方块自发光强度（lightEmission；机制等价 MC 1.0 方块光 level 0..15）：该格作为方块光**种子**的强度。
    //   flood-fill（World recomputeLightField / refloodBox）据本值把发光格种入方块光 BFS。火把=14（既有），
    //   岩浆=15（MC 1.0 岩浆光 level 15，地底发光照亮洞穴；机制对齐非精确复刻）。其余 → 0（不自发光）。
    //   越界 → 0。与 lightOpacity 解耦：岩浆 lightOpacity=0（solid=false 全透）但 lightEmission=15（既透光又自发光）。
    static quint8 lightEmission(quint8 blockId);
    // t494 状态感知自发光强度：默认按 state=0 委托单参版（保持既有火把/岩浆/末地传送门行为零回归）。仅个别方块
    //   的自发光与 state 相关 —— 当前唯一特例：燃烧中的熔炉（id==Furnace 且 state 含 FurnaceStateLitFlag bit2）
    //   → 13（MC 1.0 熔炉光 level 13，略低于火把 14）。熄灭态（bit2 清）→ 0（同普通方块不自发光）。
    //   仅 World 光照 flood 种子调用此重载（读 cell 真实 state 区分燃/熄）；非状态相关发光方块两版等价。
    static quint8 lightEmission(quint8 blockId, quint8 state);

    // t360 列顶实面 Y 偏移（cell-local 0..~1.5）：该格最高实面在世界 y = cellY + 本值。PCF 软影据此判列顶
    //   是否挡光（取代旧「heightmap+1.0 整格」假设）—— 修「下半砖 / 合活版门被当整格高 → 投出整格黑影、
    //   邻地误暗」（上半砖 / 整立方顶恰在 cellY+1 → 不变，无回归）。机制等价 MC 按方块实际模型高度投影。
    //   全实体 1.0；下半砖 0.5 / 上半砖 1.0；合活版门 0.1875 / 开 1.0；楼梯背墙到顶 1.0；栅栏 1.5；压板 0.0625。
    static float solidTopOffset(quint8 blockId, quint8 state);

    // t213 射线命中 sub-AABB（cell-local [0,1]^3）：射线进入含该方块的体素后，**须命中其中某个 sub-AABB**
    //   才算选中——不完整方块 / 火把 / 木梯的「空气部分」让射线穿过命中后方块（修「挖半砖背后的方块却撸掉了
    //   半砖/火把/木梯」，命中点是否落在该方块 sub-AABB 内）。与 selectionAABBs 的差异：
    //   - 完整立方（ShapeFull）→ 单盒 {0,0,0,1,1,1}（射线进格即中，等同旧行为）；
    //   - 不完整方块段（ShapeSlab/...）→ 同 selectionAABBs（实体 sub 形状；空气部分穿过）；
    //   - 火把（ShapeNone，selectionAABBs 空 → 选中框由 Main.qml isTorch 分支特殊定向）→ 此处给一个贴火把
    //     视觉范围的中央小立柱盒（瞄柄/焰才命中，格角落空气穿过）；
    //   - 木梯（ShapeNone，t501：selectionAABBs 空）→ 此处给一个贴墙薄板盒（按 state[1:0] 贴墙方向摆位、
    //     3/16 厚）—— 准星完全落在木梯视觉面才命中，瞄格中空气穿过命中后方块（机制对标火把）；
    //   - air/water → 单盒 {0,0,0,1,1,1}（water 经 HitWater 命中整格舀水；air 不进本路径，兜底）。
    //   火把朝向需邻居上下文（由呈现层 QML 持 + 邻居推导），Core 层无 World → 此处取覆盖所有朝向焰/柄的
    //   保守中央区（焰恒在格中央偏上）。木梯朝向由 state[1:0] 直接读（玩家放置时写入，无需邻居推导）。
    //   raycast 用本方法 + stateAt 做命中点 vs sub-AABB 精确测试。
    static std::vector<BlockAABB> raycastAABBs(quint8 blockId, quint8 state);

    // t214 火把附着方向（存 chunk state，低 3 位编码）：火把放置时记录其所「贴」的唯一支撑邻居方向，
    //   破该邻居即掉落（不「粘」到附近其它 solid 邻居）。机制等价 MC「火把附着面被移除即脱落」。
    //   编码与 Main.qml orientFromNormal/torchNeighborSolid 同源约定（QML orient 串 ↔ 本枚举）：
    //     up↔TorchFloor / px↔TorchOnNX / nx↔TorchOnPX / pz↔TorchOnNZ / nz↔TorchOnPZ。
    //   state 经 m_states 落 SQLite round-trip 保真（存档读回仍带附着方向）。旧存档 / worldgen 火把
    //   state=0 → TorchFloor（贴地）兜底，行为对齐「地面火把」（不会误判为无支撑而掉落）。
    //   唯一消费点：PlayerController::finishMiningAt（破块后扫邻火把，附着格失撑即掉）。mesher /
    //   collisionAABBs / selectionAABBs 均不读 torch state（torch 走 ShapeNone + Main.qml isTorch 分支），
    //   故复用 state 作附着编码零回归（同 PlanksFromDoubleSlabBit 复用 state 作 marker 的模式）。
    enum TorchAttach : quint8 {
        TorchFloor = 0, // 支撑 = 下方 (y-1)：玩家点中顶面放置（QML "up"，立柱）
        TorchOnNX  = 1, // 支撑 = -X 邻：玩家点中 +X 面放置（QML "px"，柄伸 +X 嵌 -X 墙）
        TorchOnPX  = 2, // 支撑 = +X 邻：玩家点中 -X 面放置（QML "nx"，柄伸 -X 嵌 +X 墙）
        TorchOnNZ  = 3, // 支撑 = -Z 邻：玩家点中 +Z 面放置（QML "pz"，柄伸 +Z 嵌 -Z 墙）
        TorchOnPZ  = 4, // 支撑 = +Z 邻：玩家点中 -Z 面放置（QML "nz"，柄伸 -Z 嵌 +Z 墙）
    };

    // t638 铁轨家族统一谓词（单一权威）：blockId == Rail（普通）/ GoldenRail（动力）/ DetectorRail（探测）
    //   即铁轨。供 (a) railConnections 连接计算（家族互连——机制等价 MC 1.0 三种轨同轨互连）；(b) mesher
    //   贴地薄板路由（PartialBlockGeometry Rail case 三 id 共用）；(c) MinecartManager 沿轨行驶（pickTrackStep
    //   / 钉轨面判据 family）；(d) playercontroller 矿车放置目标判定（== Rail → isRail）。三 id 连续段
    //   [Rail=103, DetectorRail=128] 不连续（中间夹 20+ 个其它方块）故显式并判（同 isIce / isMushroom 模式）；
    //   改族时一处同步谓词即可。
    static bool isRail(quint8 blockId);
    // t638 探测铁轨 state bit4（值 16）=「矿车驶过」标记（机制等价 MC 1.0 detector rail 被矿车压住时输出
    //   信号——本工程无红石系统，简化为通电视觉：bit4=1 → mesher 换 rail_detector_on(160) 亮红贴图；
    //   MinecartManager tick 驶过置位（setWaterSilent state 写，同红石灯开关模式）。**bit4 不与连接位冲突**
    //   （连接位占 bit0..3 = RailConnPx/Nx/Pz/Nz；此前误用 bit0 会与 +X 连接位相撞——轨连 +X 时恒显亮）。
    //   离开不清位（保持「被压过」亮态；邻块编辑时 checkRailOnEdit 重算连接写全量 state 会顺带清本位，
    //   占位语义可接受）。collisionAABBs / selectionAABBs 不读 state（ShapeNone）→ 复用 bit4 零回归。
    //   state 经 m_states 落 SQLite round-trip 保真。
    static constexpr quint8 DetectorRailStateOnFlag = 0x10;
    // t638 木门透光：门上半格栅窗是透光窗格（机制等价 MC 1.0 门上半窗透光）。门 solid=false（不挡邻居面
    //   剔除）→ lightOpacity 默认 0 已全透；本常量仅作 mesher 立面透光语义锚点（门格光衰减 = 0，无遮），
    //   防 future「按 solid 满遮」重构回退。消费点：lightOpacity（WoodDoor/SpruceDoor 恒 0）。
    static constexpr quint8 DoorWindowLightOpacity = 0;

    // t565 铁轨连接位（存 Rail 方块 chunk state，4 位 = 水平 4 向「与相邻铁轨互连」标记）。放置铁轨 /
    //   破 / 放任何邻块后由 World::checkRailOnEdit（破邻复检）+ PlayerController::placeBlock（放置时计算）
    //   重算：连接 = 该水平 4 邻格为 Rail。mesher（PartialBlockGeometry Rail case）据连接位选形态 ——
    //   0/1 连接 → 直轨（沿唯一连接向 / 默认 NS）、对向 2 连接（±X 或 ±Z）→ 直轨（EW 时 UV 旋转 90°）、
    //   邻向 2 连接（如 +X+Z）→ 90° 拐角（tile 136 换 UV 旋转 / 镜像映射四向）、3+ 连接 → 十字（tile 137）。
    //   机制等价 MC 1.0 rail 自动连接 + 转弯（本项目无坡道 / 动力铁轨）。state 经 m_states 落 SQLite
    //   round-trip 保真；旧存档 / worldgen 铁轨 state 由放置路径重算（placeMineshaft 直写后经同一计算器
    //   统一算连接，见 World::recomputeRailConnections）。collisionAABBs / selectionAABBs 不读 rail
    //   state（ShapeNone），复用 state 作连接编码零回归（同 torch attach 编码模式）。
    //   t638：连接判定扩为 isRail 家族（普通 / 动力 / 探测轨互连）；动力 / 探测轨**不画拐角 / 十字**
    //   （mesher 直线化——机制等价 MC 1.0 golden/detector rail 无 corner 形态），普通轨拐角 / 十字不变。
    static constexpr quint8 RailConnPx = 0x01; // +X 邻为 Rail（轨延伸向 +X）
    static constexpr quint8 RailConnNx = 0x02; // -X 邻为 Rail
    static constexpr quint8 RailConnPz = 0x04; // +Z 邻为 Rail
    static constexpr quint8 RailConnNz = 0x08; // -Z 邻为 Rail
    // 算 (x,y,z) 处 Rail 的连接 state：4 向水平邻格为 Rail → 置对应位。任一邻 → 非 0。纯函数（邻块
    //   id 数组入参），供 placeBlock（放置时算 placeState）/ World::checkRailOnEdit（破邻复检）/
    //   placeMineshaft（worldgen 铺轨后统一算）共用 —— 单一权威，杜绝各处自写连接判定漂移。
    static quint8 railConnections(quint8 px, quint8 nx, quint8 pz, quint8 nz);
    // 由放置命中面外法线（指向玩家侧）推火把附着方向。torch target = hitBlock + normal，故 normal +X
    //   → 火把在 hitBlock 的 +X 侧 → 其支撑 = 火把的 -X 邻 = hitBlock（TorchOnNX）。ny>0 → TorchFloor。
    //   无法线（不应发生）→ TorchFloor 兜底。placeBlock 据此写 state；与 torchPlaced 信号传出的命中面
    //   法线（QML 据之算 prefOrient）同源 → C++ 附着判定与 QML 渲染朝向放置时一致。
    static TorchAttach torchOrientFromNormal(int nx, int ny, int nz);
    // 火把支撑邻居相对偏移 (dx,dy,dz)（state 解码）：TorchFloor→(0,-1,0)；OnNX→(-1,0,0)；OnPX→(+1,0,0)；
    //   OnNZ→(0,0,-1)；OnPZ→(0,0,+1)。越界 state 值 → TorchFloor 兜底。finishMiningAt 据此定位唯一附着格。
    static void torchAttachOffset(quint8 state, int &dx, int &dy, int &dz);

    // t501 木梯贴墙方向（存 chunk state，低 2 位编码水平方向）：木梯贴**完整立方方块的侧面**（机制等价
    //   MC 1.0 ladder 须贴实体方块面，不能贴草丛/门/活版门等不完整方块的侧）。placeBlock 放置时据「玩家点击
    //   命中面的外法线」推导木梯所贴墙面方向，写入 state；mesher 据 state 把单片贴墙 quad 摆到对应面（贴图
    //   朝外，即朝玩家侧）；finishMiningAt 据 state 定位唯一支撑墙，墙被破 → 木梯掉落（不粘到附近其它邻居）。
    //   编码 0=+X 1=-X 2=+Z 3=-Z（=「支撑墙所在的水平方向」，与 horizontalFacing / chest state 同源 4 向编码）。
    //   state 经 m_states 落 SQLite round-trip 保真（旧存档 state=0 → +X 墙兜底；木梯仅玩家放置、罕见旧存档）。
    //   **唯一消费点**：PartialBlockGeometry（mesher 据 state 摆贴墙 quad 位置）+ PlayerController 失撑掉落
    //   （dropUnsupportedLaddersAround 据 state 定位支撑墙）。collisionAABBs / selectionAABBs 不读 ladder state
    //   （Ladder 走 ShapeNone 无碰撞，选中框由 Main.qml 分流；零回归）。
    // 由放置命中面外法线（指向玩家侧）推木梯贴墙方向：玩家点中 +X 面（nx>0）→ 木梯在命中方块的 +X 侧 →
    //   命中方块是其 -X 邻 → 支撑墙在木梯的 -X 侧 → state=1(-X)。其余三向同理。ny≠0（顶/底面）→ 木梯须贴
    //   侧墙，顶/底面非合法贴墙方向 → 返回 -1（placeBlock 拒绝放置）；与火把允许地面（TorchFloor）不同，
    //   木梯仅水平贴墙（spec「贴方块侧边」）。
    static int ladderFaceFromNormal(int nx, int ny, int nz);
    // 木梯支撑墙相对偏移 (dx,dz)（state 解码，dy 恒 0：墙在水平方向）：state 0(+X)→(+1,0,0)；1(-X)→(-1,0,0)；
    //   2(+Z)→(0,0,+1)；3(-Z)→(0,0,-1)。越界 state 值 → +X 兜底（与 ladderFaceFromNormal 兜底一致）。
    //   dropUnsupportedLaddersAround 据此定位唯一支撑墙格。
    static void ladderSupportOffset(quint8 state, int &dx, int &dz);

    // t225 箱子朝向（存 chunk state，低 2 位编码水平朝向）：放置时记录箱子「前面（锁面，chest_front 贴图）」
    //   朝哪一侧，mesher 据此把 chest_front 贴到对应面（其余三侧面 chest_side、顶/底 chest_top）。机制等价
    //   MC 1.0 箱子放置时锁面朝向玩家。编码与 horizontalFacing 同源（0=+X 1=-X 2=+Z 3=-Z）= 箱子前面所朝方向。
    //   state 经 m_states 落 SQLite round-trip 保真（旧存档箱子 state=0 → 前面 +X 兜底；箱子仅玩家放置、
    //   罕见旧存档，朝向变化可接受）。**唯一消费点**：ChunkGeometry::tileFor（mesher 据 state 选前面贴图）。
    //   collisionAABBs / selectionAABBs 不读 chest state（chest 走 ShapeFull 整立方）→ 复用 state 作朝向编码
    //   零回归（同 PlanksFromDoubleSlabBit / torch state 复用 state 作 marker 的模式）。BlockCube 手持 / 掉落
    //   走 stateless tileIndex（前面恒 -Z，旧默认）→ 物品图标外观不变。
    // chestFrontFace(state)：返回前面所朝的 Face（PosX/NegX/PosZ/NegZ）；低 2 位解码，越界位 → NegZ 兜底。
    static Face chestFrontFace(quint8 state);
    // t393 箱子 state bit2（值 4）=「地牢生成箱」标记（worldgen placeDungeons 写入；玩家放置的箱子无此位）。
    //   仅供 World::isDungeonChest 读 → Main.qml.openChest 据此判「是否首开填充地牢战利品」。**不**影响
    //   chestFrontFace（后者只读低 2 位 state&3，bit2 被忽略 → 朝向编码零回归）；collisionAABBs /
    //   selectionAABBs 亦不读 chest state → 复用 bit2 作 marker 零回归（同 torch / 双半砖 marker 同族）。
    //   state 经 m_states 落 SQLite round-trip 保真（旧存档箱子 state=0 → 非地牢箱，不填充，安全）。
    static constexpr quint8 ChestStateDungeonFlag = 0x04;
    // t484 箱子 state bit3（值 8）=「废弃矿井生成箱」标记（worldgen placeMineshaft 写入；玩家放置的箱子 / 地牢箱
    //   无此位）。仅供 World::isMineshaftChest 读 → Main.qml.openChest 据此判「是否首开填充矿井战利品」
    //   （LootTable::mineshaftChestPool：矿物 / 附魔书 / 铁锭等，区别于地牢表）。**不**影响 chestFrontFace
    //   （后者只读低 2 位 state&3，bit3 被忽略 → 朝向编码零回归）；collisionAABBs / selectionAABBs 亦不读
    //   chest state → 复用 bit3 作 marker 零回归（同 ChestStateDungeonFlag bit2 / torch marker 同族）。
    //   state 经 m_states 落 SQLite round-trip 保真（旧存档箱子 state 无 bit3 → 非矿井箱，不填充，安全）。
    static constexpr quint8 ChestStateMineshaftFlag = 0x08;
    // t485 箱子 state bit4（值 16）=「沙漠神殿生成箱」标记（worldgen placeDesertTemple 写入；玩家放置的箱子 /
    //   地牢箱 / 矿井箱无此位）。仅供 World::isPyramidChest 读 → Main.qml.openChest 据此判「是否首开填充
    //   沙漠神殿战利品」（LootTable::pyramidChestPool：钻石 / 金 / 青金石 / 骨头 / 腐肉等，区别于地牢 / 矿井表）。
    //   **不**影响 chestFrontFace（后者只读低 2 位 state&3，bit4 被忽略 → 朝向编码零回归）；collisionAABBs /
    //   selectionAABBs 亦不读 chest state → 复用 bit4 作 marker 零回归（同 ChestStateDungeonFlag bit2 /
    //   ChestStateMineshaftFlag bit3 / torch marker 同族）。state 经 m_states 落 SQLite round-trip 保真
    //   （旧存档箱子 state 无 bit4 → 非神殿箱，不填充，安全）。
    static constexpr quint8 ChestStatePyramidFlag = 0x10;
    // t486 箱子 state bit5（值 32）=「丛林神殿生成箱」标记（worldgen placeJungleTemple 写入；玩家放置的箱子 /
    //   地牢箱 / 矿井箱 / 神殿箱无此位）。仅供 World::isJungleTempleChest 读 → Main.qml.openChest 据此判
    //   「是否首开填充丛林神殿战利品」（LootTable::jungleTempleChestPool：骨头 / 腐肉 / 铁锭 / 金锭 / 钻石 /
    //   箭 / 附魔书等，区别于地牢 / 矿井 / 神殿表）。**不影响** chestFrontFace（后者只读低 2 位 state&3，bit5
    //   被忽略 → 朝向编码零回归）；collisionAABBs / selectionAABBs 亦不读 chest state → 复用 bit5 作 marker
    //   零回归（同 ChestStateDungeonFlag bit2 / Mineshaft bit3 / Pyramid bit4 / torch marker 同族）。state 经
    //   m_states 落 SQLite round-trip 保真（旧存档箱子 state 无 bit5 → 非丛林神殿箱，不填充，安全）。
    static constexpr quint8 ChestStateJungleFlag = 0x20;
    // t487 箱子 state bit6（值 64）=「要塞生成箱」标记（worldgen placeStronghold 写入；玩家放置的箱子 /
    //   地牢箱 / 矿井箱 / 神殿箱 / 丛林神殿箱无此位）。仅供 World::isStrongholdChest 读 → Main.qml.openChest
    //   据此判「是否首开填充要塞战利品」（LootTable::strongholdChestPool：末影之眼 / 骨头 / 腐肉 / 铁锭 /
    //   青金石 / 红石 / 钻石 / 附魔书等，区别于其它结构表）。**不影响** chestFrontFace（后者只读低 2 位 state&3，
    //   bit6 被忽略 → 朝向编码零回归）；collisionAABBs / selectionAABBs 亦不读 chest state → 复用 bit6 作 marker
    //   零回归（同 ChestStateDungeonFlag bit2 / Mineshaft bit3 / Pyramid bit4 / Jungle bit5 / torch marker 同族）。
    //   state 经 m_states 落 SQLite round-trip 保真（旧存档箱子 state 无 bit6 → 非要塞箱，不填充，安全）。
    static constexpr quint8 ChestStateStrongholdFlag = 0x40;
    // t487 末地传送门 state bit0（值 1）=「已激活」标记（玩家持末影之眼右键传送门翻此位）。mesher 据 bit0 切
    //   end_portal(129) / end_portal_active(130) 贴图（激活后中心旋涡更亮）。state 经 m_states 落 SQLite
    //   round-trip 保真。旧存档 / worldgen 传送门 state=0 → 未激活（玩家需放末影之眼激活）。
    static constexpr quint8 EndPortalStateActiveFlag = 0x01;
    // t487 刷怪笼 state bit0（值 1）=「银鱼刷怪笼」标记（worldgen placeStronghold 写入；地牢刷怪笼 state=0
    //   无此位）。EntityManager::tickSpawners 据本位分流：bit0=1 → spawn Silverfish（要塞银鱼）；bit0=0 →
    //   spawn Shambler/Bones（地牢默认）。**不影响** spawner 其它行为（玩家破坏即停止刷怪由 blockAt!=Spawner
    //   自然实现，与 state 无关）。state 经 m_states 落 SQLite round-trip 保真（旧存档 spawner state=0 → 地牢
    //   默认刷怪，非要塞银鱼，安全）。
    static constexpr quint8 SpawnerStateSilverfishFlag = 0x01;
    // t494 熔炉 state bit2（值 4）=「燃烧中」标记（机制等价 MC 1.0 熔炉冶炼进行时正面发光）。FurnaceUI
    //   冶炼 tick 在点燃（有燃料 + 有可冶炼输入）/熄火（燃料烧尽 / 输入断 / 取走）边界翻转本位：经
    //   PlayerController::setFurnaceLit 走 5 参数 setBlock（id 不变 → 只发 worldChanged 重建 mesh、不发
    //   broken/placed；保留低 2 位朝向编码）。mesher（ChunkGeometry::tileFor）据本位选前面贴图
    //   14(furnace_front 灭) / 134(furnace_front_on 带火)；侧面 / 顶底不受影响（同 furnace_side / furnace_top）。
    //   **不影响** chestFrontFace（后者只读低 2 位 state&3，bit2 被忽略 → 朝向编码零回归）；collisionAABBs /
    //   selectionAABBs 亦不读 furnace state（furnace 走 ShapeFull 整立方）→ 复用 bit2 作 marker 零回归
    //   （同 ChestStateDungeonFlag bit2 / torch marker 同族）。state 经 m_states 落 SQLite round-trip 保真
    //   （旧存档熔炉 state 无 bit2 → 非燃烧态，安全；重新点燃 / 熄火时 FurnaceUI 重写本位）。
    static constexpr quint8 FurnaceStateLitFlag = 0x04;
    // t569 红石矿石 state bit0（值 1）=「点亮」标记（机制等价 MC 1.0 红石矿被玩家走近 / 触摸时发光数秒后
    //   熄灭）。PlayerController::scanRedstoneOre（footprint 扫描，同 scanTntTraps 采样）与 updateMining
    //   （挖掘目标为红石矿）在触发时置本位 + 经 setRedstoneOreLit 走 5 参数 setBlock（id 不变 → 只发
    //   worldChanged 重建 mesh）；定时器（playercontroller）到时清本位熄灭。光照：lightEmission 状态感知版
    //   据本位返 9（微弱、阴沉红光 —— 低于火把 14，机制等价 MC 红石矿点亮光 level 9）→ recomputeLightAround
    //   检出 lightSourceChanged 触发方块光增量重 flood。mesher 不读本位（贴图恒 138，光照由顶点色 block 通道
    //   呈现泛光，非 MC 式亮贴图切换）；collisionAABBs / selectionAABBs 亦不读 state（ShapeFull 整立方）→
    //   复用 bit0 作 marker 零回归。state 经 m_states 落 SQLite round-trip 保真（旧存档红石矿 state=0 → 未点亮，安全）。
    static constexpr quint8 RedstoneOreStateLitFlag = 0x01;
    // t569 红石矿石点亮时长（秒；机制等价 MC 1.0 红石矿被触碰后亮约 5 秒后自熄）。playercontroller
    //   置亮时启动定时器，到期清 bit0 熄灭（同 MC 单次触碰一次点亮窗口）。
    static constexpr float RedstoneOreLitSeconds = 5.0f;
    // t620 红石灯 state bit0（值 1）=「点亮」标记（机制等价 MC 1.0 红石灯受红石信号点亮——本项目无红石
    //   系统，简化为右键直接开关：placeBlock useBlock 分支翻本位）。放置时 state=0（off 态，机制等价 MC
    //   1.0 红石灯放置即灭）。消费点：① mesher（ChunkGeometry::tileFor）据本位选全六面贴图
    //   redstone_lamp_off(152) / redstone_lamp_on(153)（六面同换，非 per-face）；② lightEmission 状态感知版
    //   据本位返 15（MC 1.0 红石灯光 level 15，同岩浆档 —— recomputeLightAround 检出 lightSourceChanged
    //   即重 flood 方块光）。5 参数 setBlock（id 不变只 state 变 → 仅 worldChanged 重建 mesh，不发
    //   broken/placed）。collisionAABBs / selectionAABBs 不读 state（ShapeFull 整立方）→ 复用 bit0 作
    //   开关编码零回归（同 FurnaceStateLitFlag bit2 / RedstoneOreStateLitFlag bit0 模式）。state 经
    //   m_states 落 SQLite round-trip 保真（存档读回仍带开关态；旧存档无此方块故无迁移）。
    static constexpr quint8 RedstoneLampStateOnFlag = 0x01;
    // t627 压力板 state bit0（值 1）=「踩下」标记（机制等价 MC 1.0 压力板被实体压下）。PlayerController
    //   ::updatePressurePlates（footprint + mob + 掉落物扫描，同 scanTntTraps 采样族的统一触发源）在踩下沿 /
    //   离开沿翻转本位（5 参数 setBlock，id 不变 → 仅 worldChanged 重建 mesh，同门开合模式）；mesher
    //   （PartialBlockGeometry plate case）据本位把薄板高度 1/16 压到 1/32 → 「被压下去变矮」视觉。触发权重
    //   （wood/cobble/stone=全部 / iron=仅玩家+mob / gold=仅掉落物）由 pressurePlateAccepts 单一权威判定。
    //   collisionAABBs / selectionAABBs 不读 plate state（ShapePlate 薄板碰撞恒 1/16，踩下不改碰撞 → 复用
    //   bit0 零回归，同 RedstoneOreStateLitFlag 模式）。state 经 m_states 落 SQLite round-trip 保真。
    static constexpr quint8 PressurePlateStatePressedFlag = 0x01;
    // t627 压力板触发权重单一权威：给定压力板 id + 触发源类型（byItem=true 掉落物 / false 玩家或 mob），
    //   返回该源能否触发此板。wood/cobble/stone=全触发；iron=仅玩家+mob（重质，掉落物不触发）；gold=仅
    //   掉落物（轻质，任何掉落物即触发）。非压力板 → false。供 PlayerController::updatePressurePlates
    //   统一读（不各处硬编码 id 判定，同 isPressurePlate 谓词模式；未来追加权重变体时一处同步）。
    static bool pressurePlateAccepts(quint8 blockId, bool byItem);

    // 挖掘 / 掉落 / 堆叠属性访问器（t42 集中表查；越界 → air 行默认：hardness=0 / NoTool / 不掉落 / maxStack=0）。
    static float hardness(quint8 blockId);    // 基础硬度
    static int toolType(quint8 blockId);      // 有效工具类型（ToolType；给挖掘速度加成的工具类）
    static int minToolTier(quint8 blockId);   // 最低工具等级（仅 requiresTool=true 时作门槛）
    static bool requiresTool(quint8 blockId); // t265 掉落是否需要匹配工具（true=石类需镐才掉；false=木土沙空手也掉）
    static int dropId(quint8 blockId);        // 破坏后掉落物品 id（<=0=不掉落）
    static int dropCount(quint8 blockId);     // 掉落数量
    static int maxStack(quint8 blockId);      // 单栈最大堆叠（仅方块段 0..Count-1）

    // t490fix 全 id 段堆叠上限（Core 层权威查询；机制对齐 Hotbar::maxStackSize 但不依赖 Game 层）。
    //   覆盖方块段（0..Count-1）+ 非方块物品段：工具段 [0x100,0x200) → 1（独立耐久不可叠）；材料段 ≥0x200 → 64
    //   （木棒 / 煤 / 骨头 / 腐肉 / 箭 / 火药 / 羽毛 等 mob 掉落物可叠，机制等价 MC 1.0 材料 maxStack 64）；air / 越界 → 0。
    //   仅供 Entities 层（ItemEntityManager 掉落物就近合并）查堆叠上限 —— Entities 不能 include Game 层 Hotbar，
    //   故 Core 提供此纯函数（Core 不依赖 Game；与 Hotbar::maxStackSize 的桶 / 蘑菇汤 / 护甲特例（maxStack=1）相比，
    //   这些物品不会作为 mob / 破块掉落物出现（仅玩家 Q 键丢弃），即便错误地按 64 合并也无害 —— 拾取时 Hotbar.addStack
    //   会按真实 maxStack=1 自然分槽，掉落实体阶段按 64 合并只是视觉多显一个 count，无数据错）。仅掉落物合并语义。
    //   ⚠️ **本函数不替代 Hotbar::maxStackSize 作为背包槽的权威上限**（背包仍读 Hotbar 那套含桶 / 汤特例）；本函数
    //   仅用于「掉落实体阶段就近合并」的近似上限。
    static int maxStackSize(int itemId);   // 全 id 段堆叠上限（掉落物合并用）

    // 内部/调试用方块名（**非**面向用户字串；通用词）。越界/未知 id 返回 "unknown"。
    static const char *blockName(quint8 blockId);

    // 用户可见的**中文**显示名（PLAN §9 override (b)：通用描述词）。
    //   air → 空串；越界/未知 id → 空串（兜底）。
    // 与 blockName()（内部英文标识符）分离：本方法供 HUD/背包等面向用户文本消费；
    // 字面量为 UTF-8，由 fromUtf8 解码（与项目既有中文注释同源）。
    //   grass=草方块 dirt=泥土 stone=石头 cobble=圆石 log=橡木原木
    //   planks=橡木木板 leaves=橡树树叶 sand=沙子 crafting_table=工作台 furnace=熔炉
    //   coal_ore=煤矿石 iron_ore=铁矿石 bedrock=基岩 ... farmland=耕地
    static QString displayName(quint8 blockId);

    // 音效材质分组（t118）：id → MaterialGroup 纯函数。AudioManager 据此选 break / mining / step
    // 音色（spec「按方块材质分组 clip 池」）。越界 / 未知 → GroupDefault（AudioManager 内部兜底
    // 复用 GroupStone 音色）。机制等价 MC「按方块材质 SoundType 选声」（机制对齐，非名词照搬）。
    static MaterialGroup materialGroup(quint8 blockId);

    // t348 引擎方块 id → MC Java 1.0.0 方块数字 id 的**对齐映射**（资源包加载前置）。**不重排枚举值**——
    //   .sqlite 存档的 chunk blob 按引擎 id 落盘（Air=0 / Grass=1 / ...），重排 BlockRegistry::Id 会破坏所有旧
    //   存档；故采用「映射层」而非「重排常量」对齐 MC 1.0：未来资源包加载器据此把引擎方块翻译成 MC 1.0
    //   terrain.png 的贴图槽（MC 1.0 按 block id 在 terrain.png 中定位 tile）。无 MC 1.0 等价的方块（如木板台阶
    //   WoodSlab——1.0 仅有石台阶 id 44、木板台阶 1.3+ 才有；铜矿石 CopperOre——1.17+ 才有）→ 返回 -1（资源包回退
    //   引擎过程化贴图）。**单一权威**（PLAN §2）：docs/item-ids.md 方块段「MC 1.0.0」列与本表须一致（改一处同步
    //   另一处）。分层：本表属 Core（仅依赖 QtGlobal）；工具 / 材料段的 MC 映射归 ToolRegistry / RecipeRegistry
    //   （Game 层各自映射自身段，向下依赖 Core 不破铁律）。越界 id → -1。
    static int mcBlockId(quint8 engineId);

private:
    BlockRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // BLOCKREGISTRY_H
