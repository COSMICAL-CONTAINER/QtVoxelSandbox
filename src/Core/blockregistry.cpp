#include "blockregistry.h"

// 单一数据表：每方块一行 —— 外观 / 实体 / 挖掘 / 掉落 / 堆叠 / 名 全部集中（t42）。
// 行索引 == 方块 id（BlockRegistry::Id）。改方块任何属性只改这里，全工程生效（挖掘 / 掉落 /
// 背包 / mesher 全部走本表的只读查询）。数组大小用 Count 钉死，行序 == Id 枚举序，
// 初始值个数与 Count 不符 → 编译失败（防漏行 / 错位）。
//
// 字段填值依据（机制等价 MC 1.0，对齐机制而非精确数值；PLAN §9 通用名）：
//   - tile 序号：与 tools/build_atlas.py 打包顺序严格一致（一个偏差即渗色 / 错贴）。
//     grass 顶=grass_top(0) 底=dirt(2) 侧=grass_side(1)；log 顶/底=log_top(6) 侧=log_side(7)。
//     crafting_table 顶=crafting_table_top(10) 底=planks(8) 侧=crafting_table_side(11)（t50）。
//     furnace 顶=furnace_top(12) 底=furnace_top(12) 侧=furnace_side(13) 前(-Z)=furnace_front(14)（t80）。
//     frontTile 仅「有朝向」方块用（熔炉炉口）；其余方块 frontTile == sideTile，-Z 面与其它侧面无差异。
//   - hardness：grass/dirt/sand≈0.5~0.6；stone 1.5 / cobble 2.0（需镐）；log/planks 2.0；leaves 0.2；
//     crafting_table 2.5（木制，同 MC 工作台量级）；furnace 1.5（同石头，需镐；spec t80「同石头」）；
//     bedrock=-1.0（负值 → ToolRegistry::canMine 自动 false：t141 后生存不可破——updateMining 守 finishMiningAt；创造可瞬破，t141 移除 beginMining 守卫）。
//   - toolType + requiresTool（t265 解耦「速度加成工具」与「掉落工具门槛」）：
//       石类（stone/cobble/furnace/coal_ore/iron_ore）= Pickaxe + requiresTool=true（空手不掉落、需镐才掉，
//         机制等价 MC 石类「correct tool required to drop」）；
//       木类（log/planks/crafting_table/chest/wood_slab/stairs/fence/plate/door/trapdoor）= Axe + requiresTool=false
//         （空手也掉落，仅斧给速度加成，机制等价 MC 木类「no tool required, axe speeds up」）；
//       土沙草类（grass/dirt/sand/farmland）= Shovel + requiresTool=false（空手也掉落，仅铲给速度加成）；
//       其余（air/leaves/torch/tall_grass/wheat_crop/water/bedrock 特例）= NoTool（leaves 等无有效工具，
//         空手即基准速；bedrock toolType=Pickaxe 但 hardness<0 canMine=false 永不被挖，requiresTool 仅记账）。
//   - minToolTier：仅 requiresTool=true 的石类用 —— coal_ore=1（木镐可挖、掉煤材料）；iron_ore=2（需石镐，
//     木镐挖了不掉落，机制等价 MC 铁矿）；stone/cobble/furnace=1（木镐起掉落）。requiresTool=false 时忽略。
//   - dropId：方块自掉（id==自身），唯独 stone→cobble（MC：原石采下变圆石）；矿石→材料段 id（>=0x200，
//     由 RecipeRegistry::MaterialIdBase 契约定址；Core 层不依赖 Game，故此处用字面量 0x201=Coal /
//     0x202=IronOreDrop，t85 在 recipe.h 给这两个 id 命名 + 加图标/中文名）；air 不掉。
//   - maxStack：MC 1.0 方块标准 64；air=0（不可拾取 / 不可放置）。
namespace {
constexpr BlockRegistry::BlockDef kDefs[int(BlockRegistry::Count)] = {
    /* air            */ {int(BlockRegistry::Air),           0,  0, 0,  0,  false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false,                            0, 0,  0, "air",            ""},
    /* grass          */ {int(BlockRegistry::Grass),         0,  2, 1,  1,  true,  BlockRegistry::ShapeFull,     0.6f, int(BlockRegistry::Shovel),  0, false, int(BlockRegistry::Grass),         1, 64, "grass",          "草方块"}, // t265 铲加速（requiresTool=false 空手仍掉落）；顶=grass_top 底=dirt 侧=grass_side
    /* dirt           */ {int(BlockRegistry::Dirt),          2,  2, 2,  2,  true,  BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::Shovel),  0, false, int(BlockRegistry::Dirt),          1, 64, "dirt",           "泥土"}, // t265 铲加速（空手也掉落）
    /* stone          */ {int(BlockRegistry::Stone),         3,  3, 3,  3,  true,  BlockRegistry::ShapeFull,     1.5f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::Cobble),        1, 64, "stone",          "石头"}, // 需木镐+ 采掘；掉圆石（原石→圆石）
    /* cobble         */ {int(BlockRegistry::Cobble),        5,  5, 5,  5,  true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::Cobble),        1, 64, "cobble",         "圆石"},
    /* log            */ {int(BlockRegistry::Log),           6,  6, 7,  7,  true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::Log),            1, 64, "log",            "橡木原木"}, // t265 斧加速（requiresTool=false 空手仍掉落）；顶/底=log_top 侧=log_side
    /* planks         */ {int(BlockRegistry::Planks),        8,  8, 8,  8,  true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::Planks),         1, 64, "planks",         "橡木木板"}, // t265 斧加速（空手也掉落）
    /* leaves         */ {int(BlockRegistry::Leaves),        9,  9, 9,  9,  true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::NoTool),  0, false,                            0, 0, 64, "leaves",         "橡树树叶"}, // t305 dropId=0（叶掉木棒/树苗物品由 playercontroller dropLeafDrops 概率分流；不再自掉叶子方块）。无有效工具（MC 剑/剪加速，本工程留后续）。solid=true（遮挡天光，破叶触发光场重算）。
    /* sand           */ {int(BlockRegistry::Sand),          4,  4, 4,  4,  true,  BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::Shovel),  0, false, int(BlockRegistry::Sand),           1, 64, "sand",           "沙子"}, // t265 铲加速（空手也掉落）
    /* crafting_table */ {int(BlockRegistry::CraftingTable), 10, 8, 11, 11, true,  BlockRegistry::ShapeFull,     2.5f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::CraftingTable),  1, 64, "crafting_table", "工作台"}, // t265 斧加速（空手也掉落）；顶=crafting_table_top(10) 底=planks(8) 侧=crafting_table_side(11)（t50）
    /* furnace        */ {int(BlockRegistry::Furnace),       12, 12, 13, 14, true,  BlockRegistry::ShapeFull,     1.5f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::Furnace),        1, 64, "furnace",        "熔炉"}, // 顶=furnace_top(12) 底=furnace_top(12) 侧=furnace_side(13) 前(-Z)=furnace_front(14)（t80）；8 圆石围圈合成；同石头（需镐）
    /* coal_ore       */ {int(BlockRegistry::CoalOre),       15, 15, 15, 15, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 1, true,  0x201,                              1, 64, "coal_ore",       "煤矿石"}, // 各面=coal_ore(15)（t84）；散布于 stone 区段；木镐可挖；掉煤材料(0x201，t85 命名)
    /* iron_ore       */ {int(BlockRegistry::IronOre),       16, 16, 16, 16, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 2, true,  0x202,                              1, 64, "iron_ore",       "铁矿石"}, // 各面=iron_ore(16)（t84）；散布于 stone 区段；**需石镐**（minTier2，木镐挖不掉落）；掉铁原矿材料(0x202，t85 命名)
    /* torch          */ {int(BlockRegistry::Torch),         17, 17, 17, 17, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::Torch),         1, 64, "torch",          "火把"}, // 各面=torch(17)（t88）；solid=false 非实体碰撞、hardness=0 瞬破、NoTool；掉自身；伪光源（Main.qml 发光 Model，非 PointLight）
    /* bedrock        */ {int(BlockRegistry::Bedrock),       18, 18, 18, 18, true,  BlockRegistry::ShapeFull,    -1.0f, int(BlockRegistry::Pickaxe), 0, true,                             0, 0, 64, "bedrock",        "基岩"}, // 各面=bedrock(18)（t119）；solid=true 实体碰撞、**hardness=-1.0**（负值 → ToolRegistry::canMine 自动 false，任何模式/工具不可破，防创造秒破底层）、dropId=0 不掉落；worldgen y 0..4 坑洼层
    // ── t134 不完整方块（异形段 id >= FirstPartial=15）：6 类木制半方块。各面贴图=planks(8)、
    //   solid=false（非整立方 → 不挡邻居面剔除，避免相邻整立方被误剔出洞；**碰撞走 shape 子 AABB**，t146）、
    //   hardness=2.0（木质）、t265 toolType=Axe + requiresTool=false（斧加速、空手可采且掉落）、dropId=自身、dropCount=1。
    //   mesher 经 PartialBlockGeometry::append 按 (id,state) 生成异形顶点。maxStack：door=1（单件不可堆叠），其余 64。
    /* wood_slab      */ {int(BlockRegistry::WoodSlab),          8,  8, 8,  8, false, BlockRegistry::ShapeSlab,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::WoodSlab),          1, 64, "wood_slab",          "木板台阶"}, // t265 斧加速；state bit0=上半(1)/下半(0)；半高 0.5
    /* wood_stairs    */ {int(BlockRegistry::WoodStairs),        8,  8, 8,  8, false, BlockRegistry::ShapeStairs,   2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::WoodStairs),        1, 64, "wood_stairs",        "木板楼梯"}, // t265 斧加速；state[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z；bit2=上下倒置
    /* wood_fence     */ {int(BlockRegistry::WoodFence),         8,  8, 8,  8, false, BlockRegistry::ShapeFence,    2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::WoodFence),         1, 64, "wood_fence",         "木栅栏"}, // t265 斧加速；中心立柱 0.4 见方 × 1.5 高 + 四向横档连邻居（t209）；state=0
    /* wood_pressure_plate */ {int(BlockRegistry::WoodPressurePlate), 8, 8, 8, 8, false, BlockRegistry::ShapePlate, 2.0f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::WoodPressurePlate), 1, 64, "wood_pressure_plate", "木板压力板"}, // t265 斧加速；贴地薄板；state=0
    /* wood_door      */ {int(BlockRegistry::WoodDoor),          8,  8, 8,  8, false, BlockRegistry::ShapeDoor,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::WoodDoor),          1,  1, "wood_door",          "木板门"}, // t265 斧加速；两格高；maxStack=1；state bit3=上格 bit2=开 bit[1:0]=朝向
    /* wood_trapdoor  */ {int(BlockRegistry::WoodTrapdoor),      8,  8, 8,  8, false, BlockRegistry::ShapeTrapdoor, 2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::WoodTrapdoor),      1, 64, "wood_trapdoor",      "木活板门"}, // t265 斧加速；state bit0=开/合 bit[2:1]=开时朝向
    // ── t148 水（静水）：机制等价 MC 1.0 静水。solid=false（不挡邻居面剔除 → 相邻地形仍画自己的面）、
    //   shape=ShapeNone（**无碰撞 sub-AABB** → 玩家穿过，spec「物理 v1 穿过」；与 torch 同走 ShapeNone 路径）、
    //   **hardness=-1.0**（负值 → ToolRegistry::canMine 自动 false：任何模式/工具不可破，防创造秒破水；
    //   同 bedrock 哨兵语义）、toolType=NoTool / minTier=0、dropId=0 不掉落、dropCount=0、maxStack=64
    //   （worldgen 专属，不进创造调色板 / 不掉落 → maxStack 实不可达，填 64 与方块族一致）。
    //   各面贴图=water(19)（蓝半透观感由 Main.qml 水材质 opacity=0.7 实现，纹理本身不透明）。
    /* water         */ {int(BlockRegistry::Water),              19, 19, 19, 19, false, BlockRegistry::ShapeNone,    -1.0f, int(BlockRegistry::NoTool),  0, false,                            0, 0, 64, "water",            "水"},
    // ── t173 箱子（Chest）：机制等价 MC 1.0 箱子（右键开 27 槽物品栏 + 盖子开合动画；物品存 ChestStore）。
    //   整立方实体（solid=true / ShapeFull —— 与工作台 / 熔炉同走 mesher 整立方面路径，**非**异形方块）；
    //   hardness=2.5（木制，同工作台量级）、NoTool（空手可采且掉落自身）、各面贴图：顶=chest_top(20) /
    //   底=chest_top(20)（底面少见，同顶面木纹）、侧(+X/-X/+Z)=chest_side(21)、前(-Z)=chest_front(22，锁面朝 -Z）。
    //   maxStack=64（背包内可堆叠，机制等价 MC 箱子物品）。掉落自身。音色归 GroupWood（木质）。
    /* chest         */ {int(BlockRegistry::Chest),              20, 20, 21, 22, true,  BlockRegistry::ShapeFull,     2.5f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::Chest),          1, 64, "chest",          "箱子"}, // t265 斧加速（木制，空手也掉落）
    // ── t234 耕地（Farmland）：机制等价 MC 1.0 耕地（持锄右键泥土/草方块→耕地；干/湿两态由水源邻近判定）。
    //   hardness=0.6（同 grass/dirt 量级，NoTool 空手可采且掉落）、dropId=Dirt（破耕地掉泥土，机制等价 MC「耕地
    //   破坏返泥土」，非掉耕地自身）、dropCount=1、maxStack=64。
    //   字段复用（同 chest 复用 frontTile 作锁面、planks 复用 state 作双半砖 marker 的模式）：topTile=farmland_dry(26)
    //   （顶面，mesher 据 state 低 2 位湿润等级做顶点色暗化，darker=wetter；t406）/ frontTile=farmland_wet(27)（字段
    //   复用，Farmland 无 -Z 前面语义）/ bottomTile=sideTile=dirt(2)（耕地底/侧面同泥土）。
    //   **t408 矮盒渲染 + solid=false**：耕地顶面渲染在 0.9375（15/16，机制等价 MC 耕地比整立方矮 1 像素），经
    //     PartialBlockGeometry 画 [0,0.9375] 矮盒（顶=farmland_dry / 侧·底=dirt），露出 1/16 唇。solid=false（同 glass
    //     模式）→ 相邻整立方**不**因耕地剔面 → 画满高侧壁，填住矮盒上方的 1/16 缺口（否则 solid=true + 矮盒 → 缺口处
    //     无任何面 = 透视 x-ray 洞，lessons-learned t194 同族）。shape 仍 ShapeFull（碰撞/选中/raycast 走整格，三者与
    //     渲染解耦；collisionAABBs 特例 0.9375 不变）。光照仍满遮（lightOpacity 特例返 15，见 .cpp；solid=false 不会
    //     误降为全透）。
    //   **碰撞略矮 0.9375**：collisionAABBs 对 Farmland 特例返 {0,0,0,1,0.9375,1}（见 .cpp 实现处注释）。
    //   音色归 GroupGrass（同 grass/dirt 软土音）。
    /* farmland      */ {int(BlockRegistry::Farmland),            26,  2,  2, 27, false, BlockRegistry::ShapeFull,     0.6f, int(BlockRegistry::Shovel),  0, false, int(BlockRegistry::Dirt),           1, 64, "farmland",       "耕地"}, // t408 solid=false（矮盒渲染，邻整立方不剔面填唇缺口）；t265 铲加速（土类，空手也掉泥土）
    // ── t235 草丛（TallGrass）：机制等价 MC 1.0 草丛 / 蕨类（tall grass / fern）。**cross 形广告牌方块**
    //   （两片对角十字相交的双面 quad，billboard X 形贴图）—— 非 1×1×1 整立方、亦非段内异形方块段。
    //   solid=false（非实体 → 不挡邻居面剔除，相邻地形仍画自己的面；同 torch / 不完整方块语义）、
    //   shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 草丛可踩过）、hardness=0（瞬破，同 torch）、
    //   NoTool（空手可采且掉落）、dropId=0x208（小麦种子，材料段；Core 不依赖 Game，字面量与
    //   RecipeRegistry::SeedId 同源）、dropCount=1、maxStack=64。各面贴图=tall_grass(28)（green 草叶 +
    //   alpha 透明底）。音色归 GroupGrass（软草音，同 grass/dirt）。worldgen 在 grass 表层上方确定性散布。
    //   **不**掉落自身（掉小麦种子）；机制等价 MC「挖草丛掉小麦种子」。进创造调色板（t244 补全：草丛虽由 worldgen
    //   散布、非玩家常规放置，但创造页供测试 / 装饰直接取用；图标走 flat 2D 路径同火把，Hotbar::iconFileForBlock 返
    //   icon_tall_grass.png）。
    /* tall_grass    */ {int(BlockRegistry::TallGrass),            28, 28, 28, 28, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, 0x208,                              1, 64, "tall_grass",    "草丛"},
    // ── t236 小麦作物（WheatCrop）：机制等价 MC 1.0 小麦作物（wheat crop）。**cross 形广告牌方块**（与 TallGrass 同走
    //   PartialBlockGeometry 的 cross 几何段 [FirstCross, LastCross]；两片对角相交的双面 quad，alpha 透明底 cutout）。
    //   **生长阶段存 chunk state**（state = 阶段 0..7；WheatCropStageMax；0=刚种嫩芽、7=成熟），WorldClock tick 推进成长。
    //   solid=false（非实体 → 不挡邻居面剔除，同 torch / 草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC
    //   作物可踩过）、hardness=0（瞬破，同 torch / 草丛）、NoTool（空手可采且掉落）、dropId=0x208（小麦种子，材料段；
    //   **未成熟破块返种子** —— 成熟阶段掉小麦物品 + 额外种子归 t237 收割按 state 判定，本表 dropId 仅基础兜底）、
    //   dropCount=1、maxStack=64（不进创造调色板：作物经种子种出，t244 补全）。各面贴图=wheat_stage_<state>（tile 29..36）：
    //   本表 topTile/sideTile 存阶段 0 基底 tile 29，partialblockgeometry 的 WheatCrop case 内 state + 基底算实际阶段贴图
    //   （同 Water 流水贴图由 mesher 据 state 选的模式 —— 阶段贴图是呈现层选择，非 BlockDef 字段）。音色归 GroupGrass（软草音）。
    /* wheat_crop    */ {int(BlockRegistry::WheatCrop),             29, 29, 29, 29, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, 0x208,                              1, 64, "wheat_crop",    "小麦作物"},
    // ── t279 钻石矿（DiamondOre）：机制等价 MC 1.0 钻石矿（嵌于 stone 深层、需铁镐采掘、掉钻石材料）。整立方 opaque
    //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 coal/iron 矿石同族）、hardness=3.0（同 coal/iron
    //   量级，需镐）、toolType=Pickaxe、requiresTool=true、**minToolTier=3**（**需铁镐**才掉落 —— 木 / 石镐挖了不掉落，
    //   机制等价 MC 1.0 钻石矿需铁镐；t33 工具等级表 PickaxeIron tier=3）、dropId=0x212（钻石材料段，RecipeRegistry::DiamondId；
    //   Core 不依赖 Game 故用字面量 0x212）、dropCount=1、maxStack=64。各面贴图=diamond_ore(37)（石头底 + 青白菱斑晶体，
    //   原创自绘 §9a）。音色归 GroupStone（石质，同 coal/iron 矿石）。worldgen 高度分层散布于深层 y∈[5,16]（煤浅/铁中/钻石深）。
    /* diamond_ore   */ {int(BlockRegistry::DiamondOre),            37, 37, 37, 37, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 3, true,  0x212,                              1, 64, "diamond_ore",   "钻石矿石"}, // 各面=diamond_ore(37)（t279）；散布于 stone 深层 y∈[5,40]（t308：16→40）；需铁镐(minTier3)；掉钻石材料(0x212)
    // ── t300 羊毛方块（Wool）：机制等价 MC 1.0 羊毛（wool）。整立方 opaque（solid=true / ShapeFull —— 走 mesher
    //   整立方面路径，**非**异形，与 chest/farmland 同走段后整立方路径）、hardness=0.8（同 MC 1.0 羊毛量级）、
    //   toolType=Shears（剪刀给速度加成；requiresTool=false → 空手也掉落，仅速度受剪刀影响）、dropId=自身
    //   （破块掉羊毛方块，可放置）、dropCount=1、maxStack=64。各面贴图=wool(38)（奶白羊毛底 + 浅灰卷曲绒毛纹）。
    //   音色归 GroupWood（软质闷击，最接近 MC 羊毛 cloth SoundType）。
    /* wool         */ {int(BlockRegistry::Wool),                   38, 38, 38, 38, true,  BlockRegistry::ShapeFull,     0.8f, int(BlockRegistry::Shears),  0, false, int(BlockRegistry::Wool),           1, 64, "wool",          "羊毛"},
    // ── t305 树苗（Sapling）：机制等价 MC 1.0 橡树树苗（cross 形广告牌方块，种在草地/泥土上随时间生长成橡树）。
    //   cross 几何段（与 TallGrass / WheatCrop 同走 PartialBlockGeometry pushCross 双面双对角 quad，alpha 透明底 cutout）。
    //   solid=false（非实体 → 不挡邻居面剔除，同 torch / 草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC
    //   树苗可踩过）、hardness=0（瞬破，同 torch / 草丛）、NoTool（空手可采且掉落）、dropId=0x21B（**树苗物品**，材料段
    //   RecipeRegistry::SaplingItemId；Core 不依赖 Game 故用字面量 0x21B —— 破树苗掉树苗物品非树苗方块，玩家可回收再种；
    //   机制等价 MC 破树苗掉树苗物品）、dropCount=1、maxStack=64。各面贴图=sapling(39)（棕色短树干 + 绿色嫩叶小球冠，
    //   alpha 透明底；mesher 走 cross 几何段，材质 alphaCutoff cutout）。音色归 GroupGrass（软草音，同草丛 / 作物）。
    //   玩家持树苗物品右键草地/泥土种植（playercontroller useBlock 分支，同种子种植模式）；WorldClock tick 推进成长
    //   （world.tickSaplingGrowth）。**不**进方块创造调色板（树苗经物品种植；创造取树苗**物品**便于测试，见 creativeMaterials）。
    /* sapling      */ {int(BlockRegistry::Sapling),                 39, 39, 39, 39, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, 0x21B,                              1, 64, "sapling",       "橡树树苗"},
    // ── t308 铜矿（CopperOre）：机制等价 MC 1.0 铜矿（嵌于 stone 浅中层、需石镐采掘、掉铜原矿→熔炉烧铜锭）。整立方 opaque
    //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 coal/iron/diamond 矿石同族）、hardness=3.0
    //   （同族量级，需镐）、toolType=Pickaxe、requiresTool=true、**minToolTier=2**（**需石镐**才掉落 —— 木镐挖了不掉落，
    //   机制等价 MC 铜矿需石镐；同 iron 矿石门槛）、dropId=0x21C（**铜原矿**材料段，RecipeRegistry::CopperOreDropId；
    //   Core 不依赖 Game 故用字面量 0x21C —— 掉**原矿**非锭，机制等价 MC 1.0「铜/铁/金矿采下为原矿，须熔炉冶炼成锭」，
    //   区别于钻石矿直接掉钻石）、dropCount=1、maxStack=64。各面贴图=copper_ore(40)（石头底 + 橙铜斑 + 少量孔雀绿锈）。
    //   音色归 GroupStone（石质，同 coal/iron 矿石）。worldgen 高度分层散布于浅中层 y∈[5,45]（金属族中最浅、最常见；
    //   spec「铜铁金按序更稀少」→ 铜最常见）。
    /* copper_ore   */ {int(BlockRegistry::CopperOre),                40, 40, 40, 40, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 2, true,  0x21C,                              1, 64, "copper_ore",   "铜矿石"},
    // ── t308 金矿（GoldOre）：机制等价 MC 1.0 金矿（嵌于 stone 深层、需铁镐采掘、掉金原矿→熔炉烧金锭）。整立方 opaque
    //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 coal/iron/diamond/copper 矿石同族）、
    //   hardness=3.0（同族量级，需镐）、toolType=Pickaxe、requiresTool=true、**minToolTier=3**（**需铁镐**才掉落 ——
    //   木 / 石镐挖了不掉落，机制等价 MC 金矿需铁镐；同 diamond 矿石门槛）、dropId=0x21E（**金原矿**材料段，
    //   RecipeRegistry::GoldOreDropId；Core 不依赖 Game 故用字面量 0x21E —— 掉**原矿**非锭，机制等价 MC 1.0
    //   「金矿采下为原矿，须熔炉冶炼成金锭」）、dropCount=1、maxStack=64。各面贴图=gold_ore(41)（石头底 + 金黄斑簇）。
    //   音色归 GroupStone（石质）。worldgen 高度分层散布于深层 y∈[5,25]（金属族中最深、最稀有；spec「铜铁金按序更稀少」→ 金最稀有）。
    /* gold_ore     */ {int(BlockRegistry::GoldOre),                  41, 41, 41, 41, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 3, true,  0x21E,                              1, 64, "gold_ore",     "金矿石"},
    // ── t343 岩浆（Lava）：机制等价 MC 1.0 岩浆（lava，主世界慢流体）。solid=false（不挡邻居面剔除 → 相邻地形仍画自己的面；
    //   同 Water）、shape=ShapeNone（**无碰撞** → 玩家穿过；t344 着火扣血留后续）、**hardness=-1.0**（负值 → ToolRegistry::canMine
    //   自动 false：任何模式/工具不可破，防创造秒破；同 bedrock / Water 哨兵语义）、toolType=NoTool / minTier=0、dropId=0 不掉落、
    //   dropCount=0、maxStack=64（worldgen 专属，不进创造调色板 / 不掉落 → maxStack 实不可达，填 64 与方块族一致）。
    //   各面贴图=lava(42)（深红橙底 + 亮黄橙鼓泡 + 白炽热点，原创自绘 §9a；纹理不透明，岩浆段材质 opacity≈0.95 近不透、NoLighting
    //   暖色 baseColor 显自发光感）。音色归 GroupStone（石质兜底；岩浆专属 rumble 走 AudioManager lava 流声 proximity loop，非破块音）。
    //   worldgen placeLavaLakes 在 Y<30 封闭洞穴散布岩浆湖；玩家铁桶舀/放（playercontroller 桶分支 + HitLava 射线）。
    /* lava         */ {int(BlockRegistry::Lava),                     42, 42, 42, 42, false, BlockRegistry::ShapeNone,    -1.0f, int(BlockRegistry::NoTool),  0, false,                            0, 0, 64, "lava",          "岩浆"},
    // ── t387 床方块（bed）8 色变体：机制等价 MC 1.0 床（bed），简化为单格整立方（spec「head+foot 双格，或简化单格」
    //   → 取单格）。每色一个方块 id（连续段 [FirstBed, LastBed]）→ 创造调色板每色独立取用 + 右键放置（复用既有
    //   selectedBlockId → placeBlock 通用放置路径，无需新交互）。整立方 opaque（solid=true / ShapeFull —— 走 mesher
    //   整立方面路径，**非**异形，与 wool / chest 同族；简化：碰撞满格）、hardness=0.2（同 MC 1.0 床量级，软质）、
    //   toolType=Axe（木制床架；requiresTool=false → 空手也掉落，仅速度受斧影响）、dropId=自身（破床掉同色床方块，
    //   可放回）、dropCount=1、maxStack=64。各面贴图=default_bed_<color>（tile 43..50；彩色被面底 + 顶部枕垫亮带 +
    //   绗缝针脚暗点 + 边缘暗化，原创自绘 §9a）。配方 planks+wool → 红床（BedRed，默认色）；其余色变体创造调色板
    //   直接取用（无染料系统）。音色归 GroupWood（软质闷击，同 wool / chest）。睡觉机制归 t388。
    /* bed_red      */ {int(BlockRegistry::BedRed),                    43, 43, 43, 43, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedRed),        1, 64, "bed_red",      "红色床"}, // 配方产物（planks+wool）；默认色
    /* bed_orange   */ {int(BlockRegistry::BedOrange),                 44, 44, 44, 44, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedOrange),     1, 64, "bed_orange",   "橙色床"},
    /* bed_yellow   */ {int(BlockRegistry::BedYellow),                 45, 45, 45, 45, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedYellow),     1, 64, "bed_yellow",   "黄色床"},
    /* bed_green    */ {int(BlockRegistry::BedGreen),                  46, 46, 46, 46, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedGreen),      1, 64, "bed_green",    "绿色床"},
    /* bed_cyan     */ {int(BlockRegistry::BedCyan),                   47, 47, 47, 47, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedCyan),       1, 64, "bed_cyan",     "青色床"},
    /* bed_blue     */ {int(BlockRegistry::BedBlue),                   48, 48, 48, 48, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedBlue),       1, 64, "bed_blue",     "蓝色床"},
    /* bed_magenta  */ {int(BlockRegistry::BedMagenta),                49, 49, 49, 49, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedMagenta),    1, 64, "bed_magenta",  "品红色床"},
    /* bed_black    */ {int(BlockRegistry::BedBlack),                  50, 50, 50, 50, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedBlack),      1, 64, "bed_black",    "黑色床"},
    // ── t392 刷怪笼（Spawner）：机制等价 MC 1.0 刷怪笼（地下地牢中央放置的整立方方块，玩家在范围内时周期刷 1 敌对 mob，
    //   破坏后停止刷怪）。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 chest / wool
    //   同族）、hardness=5.0（同 MC 1.0 刷怪笼量级，需镐且耗时）、toolType=Pickaxe、requiresTool=true、minToolTier=1
    //   （木镐可破）、**dropId=0**（破块不掉落 —— MC 1.0 刷怪笼不可正常获得，本工程无精准采集故恒不掉落）、dropCount=0、
    //   maxStack=64（worldgen 专属 / 不掉落 → maxStack 实不可达，填 64 与方块族一致）。各面贴图=spawner(51)（暗蓝灰底
    //   + 铁灰栅栏 + 中心青绿光斑，原创自绘 §9a）。音色归 GroupStone（铁笼金属敲击）。worldgen placeDungeons 在地下地牢
    //   中央放置；玩家可破坏以停止刷怪（EntityManager::tickSpawners 扫到该格 blockAt != Spawner 即跳过）。不进创造调色板。
    /* spawner      */ {int(BlockRegistry::Spawner),                    51, 51, 51, 51, true,  BlockRegistry::ShapeFull,     5.0f, int(BlockRegistry::Pickaxe), 1, true,                             0, 0, 64, "spawner",      "刷怪笼"},
    // ── t394 沙漠群系内容（机制等价 MC 1.0 沙漠三件套：sandstone / cactus / dead bush；名称 / 贴图全原创自绘 §9a）：
    //   砂岩（Sandstone）：沙下成岩整立方。solid=true / ShapeFull（走 mesher 整立方面路径，**非**异形，与 chest/wool
    //   同族）、hardness=0.8（同 MC 1.0 砂岩量级）、toolType=Pickaxe、requiresTool=true、minToolTier=1（木镐可破，同
    //   cobble/stone 门槛）、dropId=自身（破砂岩掉砂岩方块，可放置）、dropCount=1、maxStack=64。各面贴图：顶=
    //   sandstone_top(52)（压实沙面 + 细密噪点 + 暗框）/ 底·侧=sandstone_side(53)（暖沙底 + 横向层理带）。音色归
    //   GroupStone（石质）。worldgen 在 desert 沙表层下铺砂岩（区别于直接下接 Stone）；进创造调色板（玩家可取用）。
    /* sandstone    */ {int(BlockRegistry::Sandstone),                  52, 53, 53, 53, true,  BlockRegistry::ShapeFull,     0.8f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::Sandstone),     1, 64, "sandstone",    "砂岩"},
    //   仙人掌（Cactus）：沙漠标志性植物方块（接触伤害实体）。solid=true / ShapeFull（整立方实体碰撞 → mob/玩家撞其侧
    //   或站其上即「接触」，接触伤害由 EntityManager/PlayerController 环境 tick 处理）、hardness=0.4（同 MC 1.0 仙人掌
    //   量级，软质）、toolType=NoTool（空手即采且掉落，机制等价 MC 仙人掌无工具要求）、requiresTool=false、dropId=
    //   自身（破仙人掌掉仙人掌方块，可放回）、dropCount=1、maxStack=64。各面贴图：顶·底=cactus_top(54)（绿截面 +
    //   同心方框环纹）/ 侧=cactus_side(55)（深绿底 + 4 垂直棱脊 + 棱上刺点）。音色归 GroupGrass（植物，软质）。
    //   worldgen 在 desert 沙顶散布 1-3 格高柱；放置预检（placeBlock）须 Sand/Cactus 在下方。进创造调色板。
    /* cactus       */ {int(BlockRegistry::Cactus),                     54, 54, 55, 55, true,  BlockRegistry::ShapeFull,     0.4f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::Cactus),         1, 64, "cactus",       "仙人掌"},
    //   枯死的灌木（DeadBush）：沙漠干旱地表枯枝装饰。cross 形广告牌方块（与 TallGrass/Sapling 同走 cross 几何段，两片
    //   对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。solid=false（非实体 → 不挡邻居面剔除，同 torch/
    //   草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过）、hardness=0（瞬破）、NoTool（空手可采）、dropId=0（破枯灌木
    //   无掉落，机制等价 MC 空手破 dead bush 无产物；创造调色板取用即得）、dropCount=0、maxStack=64。各面贴图=
    //   dead_bush(56)（透明底 + 棕褐放射干枝；alphaCutoff cutout）。音色归 GroupGrass（软草音）。worldgen 在 desert
    //   沙顶低密度散布；放置预检须 Sand 在下方。**段外 cross**（id 43 不在 [FirstCross,LastCross] 连续段内）→ 并入
    //   isCrossBillboard 谓词（同 Sapling 模式），mesher 路由一律读谓词。进创造调色板（装饰取用）。
    /* dead_bush    */ {int(BlockRegistry::DeadBush),                   56, 56, 56, 56, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false,                            0, 0, 64, "dead_bush",    "枯死的灌木"},
    // ── t395 雪原/针叶群系内容（机制等价 MC 1.0 寒冷群系三件套：snow / ice / spruce log；名称 / 贴图全原创自绘 §9a）：
    //   积雪层（SnowLayer）：雪原/针叶群系地表覆盖（worldgen 在 Snowy 群系把草顶替换为积雪层）。整立方 opaque
    //   （solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 sand / sandstone 同族；简化为满格整立方
    //   非 MC 薄雪层，避免异形几何复杂度）、hardness=0.2（同 MC 1.0 雪层量级，软质）、toolType=Shovel（铲加速；
    //   requiresTool=false → 空手也掉落，仅速度受铲影响）、dropId=自身（破积雪层掉积雪层方块，可放回）、dropCount=1、
    //   maxStack=64。各面贴图=snow(57)（冷白底 + 细密冰晶噪点）。音色归 GroupSand（颗粒雪响）。进创造调色板。
    /* snow_layer   */ {int(BlockRegistry::SnowLayer),                  57, 57, 57, 57, true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Shovel),  0, false, int(BlockRegistry::SnowLayer),     1, 64, "snow_layer",   "积雪层"},
    //   冰（Ice）：雪原/针叶群系水面冻结产物（worldgen freezeSurfaceWater 把 Snowy 群系海/湖表层水冻结为冰）。整立方
    //   opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形；可踩 / 实体碰撞；简化为不透明整立方，
    //   非 MC 半透冰，避开透明体积网格化复杂度）、hardness=0.5（同 MC 1.0 冰量级）、toolType=Pickaxe、requiresTool=true、
    //   minToolTier=1（木镐可破）、**dropId=0**（破冰不掉落 —— 机制等价 MC 1.0 冰需精准采集才掉落，本工程无精准采集
    //   故恒不掉落）、dropCount=0、maxStack=64（worldgen 专属冻结 / 不掉落 → maxStack 实不可达，填 64 与方块族一致）。
    //   各面贴图=ice(58)（浅蓝底 + 反光裂纹）。音色归 GroupStone（玻璃质敲击，最接近 MC 1.0 冰 glass SoundType）。
    //   不进创造调色板（worldgen 冻结获得；与 water / lava 同属「worldgen / 系统获得」语义）。
    /* ice          */ {int(BlockRegistry::Ice),                        58, 58, 58, 58, true,  BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::Pickaxe), 1, true,                             0, 0, 64, "ice",          "冰"},
    //   云杉原木（SpruceLog）：雪原/针叶群系云杉树的主干（worldgen placeSpruceTreeAt 在 Snowy 群系种云杉变种树）。
    //   整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 log / planks 同族；机制等价
    //   MC 1.0 云杉原木——寒冷群系针叶树变种，区别于橡木原木 Log）、hardness=2.0（同 MC 1.0 原木量级）、toolType=Axe
    //   （木类；requiresTool=false → 空手也掉落，仅速度受斧影响）、dropId=自身（破云杉原木掉云杉原木方块，可放置）、
    //   dropCount=1、maxStack=64。各面贴图：顶/底=spruce_log_top(59)（深棕同心年轮截面）/ 侧=spruce_log_side(60)
    //   （深棕垂直树皮条带，区别于橡木原木 log_side 的浅棕；云杉木特征为更深冷棕色）。音色归 GroupWood（木质）。
    //   进创造调色板。
    /* spruce_log   */ {int(BlockRegistry::SpruceLog),                  59, 59, 60, 60, true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::SpruceLog),     1, 64, "spruce_log",   "云杉原木"},
    // ── t396 沼泽群系内容（机制等价 MC 1.0 沼泽植物：lily pad / mushroom；名称 / 贴图全原创自绘 §9a）：
    //   睡莲（LilyPad）：沼泽浅水水面浮叶。**cross 路由的横向浮叶**（mesher PartialBlockGeometry::append 的 LilyPad
    //   case 画一片水平双面 quad 贴 cell 底部 → 浮于水面；同走 isCrossBillboard 路由 + alphaCutoff cutout，但几何为
    //   水平非竖直）。solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制
    //   等价 MC 睡莲薄叶无硬碰撞；可踩过）、hardness=0（瞬破）、NoTool（空手可采且掉落）、dropId=自身（破睡莲掉
    //   睡莲方块，可放回）、dropCount=1、maxStack=64。各面贴图=lily_pad(61)（透明底 + 绿色圆叶 + V 形缺口，alphaCutoff
    //   cutout）。音色归 GroupGrass（软植物音）。worldgen placeSwampFlora 在沼泽水格上方一格散布。进创造调色板。
    /* lily_pad     */ {int(BlockRegistry::LilyPad),                     61, 61, 61, 61, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::LilyPad),       1, 64, "lily_pad",     "睡莲"},
    //   蘑菇（Mushroom）：沼泽草地小蘑菇。cross 形广告牌方块（与 Sapling / DeadBush 同走 cross 几何段，两片对角相交
    //   双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。机制等价 MC 1.0 蘑菇（沼泽 / 阴暗草地小蘑菇）。
    //   solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过）、hardness=0（瞬破）、
    //   NoTool（空手可采且掉落）、dropId=自身（破蘑菇掉蘑菇方块，可放回）、dropCount=1、maxStack=64。各面贴图=
    //   mushroom(62)（透明底 + 米色菌柄 + 红底白斑菌盖，alphaCutoff cutout）。音色归 GroupGrass（软植物音）。
    //   worldgen placeSwampFlora 在沼泽草地格上方一格低密度散布。进创造调色板（装饰取用）。
    /* mushroom     */ {int(BlockRegistry::Mushroom),                    62, 62, 62, 62, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::Mushroom),      1, 64, "mushroom",     "蘑菇"},
    // ── t397 多群系装饰植物（机制等价 MC 1.0 花 / 甘蔗；名称 / 贴图全原创自绘 §9a）：
    //   花（Flower）4 色变体：每色一个方块 id（连续段 [FirstFlower, LastFlower]）→ 创造调色板每色独立取用 + 右键放置。
    //   cross 形广告牌方块（与 TallGrass / Sapling 同走 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）——
    //   非 1×1×1 整立方，spec「thin like tall grass」。solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone
    //   （**无碰撞** → 玩家穿过，机制等价 MC 花可踩过）、hardness=0（瞬破，同草丛 / 火把）、NoTool（空手可采且掉落）、
    //   dropId=自身（破花掉同色花方块，可放回）、dropCount=1、maxStack=64。各面贴图=flower_<color>（tile 63..66；
    //   透明底 + 绿茎 + 彩色花头，alphaCutoff cutout）。音色归 GroupGrass（软植物音，同草丛 / 蘑菇）。worldgen
    //   placeFlowers 在各群系草地低密度散布（机制等价 MC 各群系花点缀）。进创造调色板（每色独立取用）。
    /* flower_red    */ {int(BlockRegistry::FlowerRed),                   63, 63, 63, 63, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::FlowerRed),     1, 64, "flower_red",    "红花"},
    /* flower_yellow */ {int(BlockRegistry::FlowerYellow),                64, 64, 64, 64, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::FlowerYellow),  1, 64, "flower_yellow", "黄花"},
    /* flower_blue   */ {int(BlockRegistry::FlowerBlue),                  65, 65, 65, 65, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::FlowerBlue),    1, 64, "flower_blue",   "蓝花"},
    /* flower_white  */ {int(BlockRegistry::FlowerWhite),                 66, 66, 66, 66, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::FlowerWhite),   1, 64, "flower_white",  "白花"},
    //   甘蔗（Sugarcane）：水边生长的可叠高细茎植物。**cross 形广告牌方块**（与花 / 草丛同走 cross 几何段，两片对角
    //   相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方，呈细茎观感。worldgen placeSugarcane 在水域邻接的
    //   草地 / 沙地旁确定性散布 1..3 格高柱（同 cactus 1-3 高模式；spec「grows up to 3 tall at waters edges」），每格仅
    //   写空气格 → 不覆盖已生成的方块。solid=false（非实体 → 不挡邻居面剔除，同草丛 / 花）、shape=ShapeNone
    //   （**无碰撞** → 玩家穿过）、hardness=0（瞬破）、NoTool（空手可采且掉落）、dropId=自身（破甘蔗掉甘蔗方块，可放回 /
    //   可重种）、dropCount=1、maxStack=64。各面贴图=sugarcane(67)（透明底 + 绿色节段细茎 + 顶部尖叶，alphaCutoff
    //   cutout）。音色归 GroupGrass（软植物音）。**放置预检**（placeBlock）：目标格下方须为 Grass / Dirt / Sand /
    //   Sugarcane（机制等价 MC 甘蔗须草地 / 沙地 / 甘蔗支撑）。进创造调色板（玩家可取用 / 放置）。
    /* sugarcane    */ {int(BlockRegistry::Sugarcane),                    67, 67, 67, 67, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::Sugarcane),     1, 64, "sugarcane",    "甘蔗"},
    // t405 玻璃（Glass）：透明整立方（机制等价 MC 1.0 玻璃 glass）。solid=false（**关键**：仅作 mesher 邻居面剔除依据 →
    //   相邻实体方块不剔面 → 透过半透玻璃可见背后的方块；碰撞 / 选中 / 射线走 shape=ShapeFull / raycastAABBs / blockAt!=0
    //   故 glass 仍可踩 / 可瞄准 / 可破）、shape=ShapeFull（整立方，**非**异形）、hardness=0.3（同 MC 1.0 玻璃量级）、
    //   toolType=Pickaxe（石族）、requiresTool=false（空手可破且掉落——本工程无精准采集，玻璃可回收）、dropId=0x204
    //   （破玻璃掉玻璃物品 RecipeRegistry::GlassId，Core 不依赖 Game 故字面量 0x204，同 TallGrass 用 0x208 模式）、
    //   dropCount=1、maxStack=64。各面贴图=glass(68)（近白青底 + 暗边框 + 对角高光斜线，原创自绘 §9a；纹理不透明，
    //   半透由 glassOnly 段材质 opacity 实现，同 water 模式）。音色归 GroupStone（玻璃质敲击）。渲染走 glassOnly 段
    //   （独立半透材质 opacity:0.45 + NoLighting）；地形段跳过 Glass。lightOpacity=0（solid=false → default 返 0，透光）。
    /* glass        */ {int(BlockRegistry::Glass),                        68, 68, 68, 68, false, BlockRegistry::ShapeFull,     0.3f, int(BlockRegistry::Pickaxe), 0, false,                            0x204, 1, 64, "glass",        "玻璃"},
    // ── t407 胡萝卜/马铃薯作物（CarrotCrop/PotatoCrop）：机制等价 MC 1.0 carrot/potato 作物。**cross 形广告牌方块**
    //   （与小麦作物同走 PartialBlockGeometry 的 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）。
    //   种植：手持胡萝卜/马铃薯物品（CarrotId 0x22F / PotatoId 0x230）右键耕地 → 上方一格种本作物（playercontroller）。
    //   生长：World::tickCropGrowth 推进（同小麦），复用 WheatCropStageMax=7（8 年龄、age 7 成熟）。
    //   收割：成熟掉 1-4 个对应物品（playercontroller dropCropDrops）；未成熟掉 1 个。
    //   solid=false（非实体 → 不挡邻居面剔除，同小麦）/ shape=ShapeNone（无碰撞 → 玩家穿过，机制等价 MC 作物可踩过）、
    //   hardness=0（瞬破）/ NoTool（空手可采且掉落）、dropCount=1、maxStack=64。音色归 GroupGrass。
    //   dropId = 对应物品（CarrotId/PotatoId；Core 不依赖 Game 故字面量 0x22F/0x230，同 TallGrass 用 0x208 模式）；
    //   本表 dropId 仅基础兜底（未成熟破块返 1 个），成熟收割的 1-4 倍产出由 dropCropDrops 特例覆盖通用 drop 路径
    //   （同 WheatCrop/TallGrass 模式）。各面贴图存基底阶段 0 tile（69/73），mesher 在 cross 几何段据 state 选
    //   tile = 基底 + state/2（4 阶段贴图覆盖 8 年龄，机制对齐 MC 1.0 carrot/potato 4 张阶段贴图）。
    /* carrot_crop  */ {int(BlockRegistry::CarrotCrop),                    69, 69, 69, 69, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false,                           0x22F, 1, 64, "carrot_crop",  "胡萝卜作物"},
    /* potato_crop  */ {int(BlockRegistry::PotatoCrop),                    73, 73, 73, 73, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false,                           0x230, 1, 64, "potato_crop",  "马铃薯作物"},
    // ── t411 黑曜石（Obsidian）：机制等价 MC 1.0 obsidian（流水 state>0 触到静岩浆源 state=0 时，静岩浆源凝固成
    //   本方块——见 World::tickWaterFlow 流体交互 pass）。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方
    //   面路径，**非**异形，与 stone/cobble/sandstone 同族）、hardness=12.0（同 MC 1.0 obsidian 量级，极硬极耐挖）、
    //   toolType=Pickaxe（石族）、requiresTool=true、minToolTier=1（木镐起可破且掉落；本工程工具等级表无钻石镐门槛，
    //   简化为木镐即采，便于玩家回收流体交互产物）、dropId=自身（破黑曜石掉黑曜石方块，可放置）、dropCount=1、maxStack=64。
    //   各面贴图=obsidian(77)（深紫黑火山玻璃底 + 紫红纹理嵌点 + 少量品紫玻璃微反光，原创自绘 §9a）。音色归 GroupStone
    //   （石质）。worldgen 不直接生成（仅由 t411 流体交互产生），不进创造调色板（系统获得语义，同 ice）。
    /* obsidian     */ {int(BlockRegistry::Obsidian),                       77, 77, 77, 77, true,  BlockRegistry::ShapeFull,    12.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::Obsidian),      1, 64, "obsidian",     "黑曜石"},
    // ── t412 圆石变体（cobble variants）：机制等价 MC 1.0 石质半方块（cobblestone slab/stairs/wall/pressure-plate）。
    //   复用既有异形方块系统（PartialBlockGeometry 几何 + ShapeSlab/ShapeStairs/ShapeFence/ShapePlate 子 AABB），仅换
    //   圆石贴图（各面=cobble(5)）与石质属性：solid=false（非整立方 → 不挡邻居面剔除，同木制半砖）、hardness=2.0
    //   （同 cobble 量级）、toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且掉落，机制等价 MC 石类需镐）；
    //   dropId=自身（破块掉同种圆石变体，可放回）、dropCount=1、maxStack=64。音色归 GroupStone（石质，同 cobble）。
    //   各面贴图=cobble(5)（mesher 经 PartialBlockGeometry 按 (id,state) 生成异形顶点，tile 由 tileIndex 取本方块 sideTile）。
    /* cobble_slab          */ {int(BlockRegistry::CobbleSlab),          5, 5, 5, 5, false, BlockRegistry::ShapeSlab,     2.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::CobbleSlab),          1, 64, "cobble_slab",          "圆石台阶"}, // state bit0=上半(1)/下半(0)；半高 0.5（与 WoodSlab 同编码）
    /* cobble_stairs        */ {int(BlockRegistry::CobbleStairs),        5, 5, 5, 5, false, BlockRegistry::ShapeStairs,   2.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::CobbleStairs),        1, 64, "cobble_stairs",        "圆石楼梯"}, // state[1:0]=朝向 bit2=倒置（与 WoodStairs 同编码）
    /* cobble_fence         */ {int(BlockRegistry::CobbleFence),         5, 5, 5, 5, false, BlockRegistry::ShapeFence,    2.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::CobbleFence),         1, 64, "cobble_fence",         "圆石墙"}, // 中心立柱 + 四向横档连邻居（机制等价 MC 圆石墙；与 WoodFence 同几何）；state=0
    /* cobble_pressure_plate */ {int(BlockRegistry::CobblePressurePlate), 5, 5, 5, 5, false, BlockRegistry::ShapePlate,   2.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::CobblePressurePlate), 1, 64, "cobble_pressure_plate", "圆石压力板"}, // 贴地薄板（与 WoodPressurePlate 同几何）；state=0
};

// 编译期表大小守卫：Count 变更后未同步本表 → 编译失败（防漏行 / 错位）。
static_assert(sizeof(kDefs) / sizeof(kDefs[0]) == int(BlockRegistry::Count),
              "kDefs 行数须与 BlockRegistry::Count 一致；新方块需补 BlockDef 行");

// 编译期图集越界守卫（t182 根除复发 bug 类）：扫描 kDefs 全部 tile 字段，最大者须 < AtlasTileCount。
//   - 任一方块 tile >= AtlasTileCount → 编译失败（该瓦片 UV 越界图集 → 采样到图集外/回绕 → 渗色）。
//   - 隐式校验 AtlasTileCount 不小于「实际用到的最大 tile + 1」：加新方块带新 tile（如 tile 23）却忘把
//     AtlasTileCount 从 23 改到 24 → 本断言失败 → 强制同步（堵住「mesher/BlockCube 各持魔数、漏改一份」
//     的回归源头，t54/t148/t182 三次同族 bug 的结构性根除）。
constexpr int computeMaxTile() {
    int m = 0;
    for (const BlockRegistry::BlockDef &d : kDefs) {
        if (d.topTile    > m) m = d.topTile;
        if (d.bottomTile > m) m = d.bottomTile;
        if (d.sideTile   > m) m = d.sideTile;
        if (d.frontTile  > m) m = d.frontTile;
    }
    return m;
}
static_assert(computeMaxTile() < int(BlockRegistry::AtlasTileCount),
              "某方块 tile 字段 >= BlockRegistry::AtlasTileCount → 图集越界采样（渗色/错贴）；"
              "新增瓦片须同步 AtlasTileCount 与 tools/build_atlas.py 的 TILES");

// t348 引擎方块 id → MC Java 1.0.0 方块数字 id 对齐表（资源包前置；单一权威，与 docs/item-ids.md 方块段
//   「MC 1.0.0」列一致）。行索引 == 引擎方块 id（与 kDefs 同序）。**不重排枚举**（保存档向后兼容）：本表仅作
//   「翻译层」，引擎 id 恒为存档权威。MC 1.0 无等价 → -1（WoodSlab：1.0 仅石台阶 44；CopperOre：1.17+）。
//   Water 取静水 id 8（MC 流水 id 9 同属 Water 方块，正映射取代表值 8）。新增方块须在此补一行（否则越界 -1）。
constexpr int kMcBlockId[int(BlockRegistry::Count)] = {
    /* air            */ 0,   /* grass          */ 2,   /* dirt           */ 3,   /* stone          */ 1,
    /* cobble         */ 4,   /* log            */ 17,  /* planks         */ 5,   /* leaves         */ 18,
    /* sand           */ 12,  /* crafting_table */ 58,  /* furnace        */ 61,  /* coal_ore       */ 16,
    /* iron_ore       */ 15,  /* torch          */ 50,  /* bedrock        */ 7,   /* wood_slab      */ -1,
    /* wood_stairs    */ 53,  /* wood_fence     */ 85,  /* wood_pressure_plate */ 72,
    /* wood_door      */ 64,  /* wood_trapdoor  */ 96,  /* water          */ 8,   /* chest          */ 54,
    /* farmland       */ 60,  /* tall_grass     */ 31,  /* wheat_crop     */ 59,  /* diamond_ore    */ 56,
    /* wool           */ 35,  /* sapling        */ 6,   /* copper_ore     */ -1,  /* gold_ore       */ 14,
    /* lava           */ 10,
    /* bed_red        */ 26,  /* bed_orange     */ 26,  /* bed_yellow     */ 26,  /* bed_green      */ 26, // t387 床 8 色变体 → MC 1.0 床 id 26（统一；MC 1.0 床颜色由 metadata 分，本工程用独立 id）
    /* bed_cyan       */ 26,  /* bed_blue       */ 26,  /* bed_magenta    */ 26,  /* bed_black      */ 26,
    /* spawner        */ 52, // t392 刷怪笼 → MC 1.0 mob spawner id 52
    /* sandstone      */ 24, // t394 砂岩 → MC 1.0 sandstone id 24
    /* cactus         */ 81, // t394 仙人掌 → MC 1.0 cactus id 81
    /* dead_bush      */ 32, // t394 枯死的灌木 → MC 1.0 dead bush id 32
    /* snow_layer     */ 80, // t395 积雪层 → MC 1.0 snow block id 80（满格雪方块；MC 薄雪层 id 78 为非整立方，本工程简化整立方故取雪方块 80）
    /* ice            */ 79, // t395 冰 → MC 1.0 ice id 79
    /* spruce_log     */ -1, // t395 云杉原木 → MC 1.0 无独立 id（1.0 仅橡木 log id 17，云杉 / 白桦等变种 1.7+ 才以 metadata 分；本工程用独立 id 故无 1.0 等价）
    /* lily_pad       */ -1, // t396 睡莲 → MC 1.0 无等价（lily pad id 111 为 1.7+ 物品；本工程作方块故无 1.0 等价）
    /* mushroom       */ -1, // t396 蘑菇 → MC 1.0 蘑菇仅以 item（红 40 / 棕 39）或巨型菌盖方块（红 100 / 棕 99）存在，无「小蘑菇植物方块」等价；本工程作 cross 装饰方块故无 1.0 等价
    /* flower_red     */ 37, // t397 红花 → MC 1.0 poppy（罂粟）id 37
    /* flower_yellow  */ 38, // t397 黄花 → MC 1.0 dandelion（蒲公英）id 38
    /* flower_blue    */ -1, // t397 蓝花 → MC 1.0 无等价（cornflower 矢车菊 id 28（1.0 为玫瑰丛 rose bush 的变体）；蓝花是本工程原创 4 色变体之一，无 1.0 等价故 -1）
    /* flower_white   */ -1, // t397 白花 → MC 1.0 无等价（oxeye daisy 雏菊 1.7+ id 34；本工程作花方块故无 1.0 等价）
    /* sugarcane      */ 83, // t397 甘蔗 → MC 1.0 sugar cane（reeds）id 83
    /* glass          */ 20, // t405 玻璃 → MC 1.0 glass id 20
    /* carrot_crop    */ 141, // t407 胡萝卜作物 → MC 1.0 carrot crop block id 141（age 由 metadata 分，统一取成熟态 id）
    /* potato_crop    */ 142, // t407 马铃薯作物 → MC 1.0 potato crop block id 142
    /* obsidian       */ 49, // t411 黑曜石 → MC 1.0 obsidian block id 49（流水触静岩浆源凝固产物）
    /* cobble_slab             */ 44, // t412 圆石台阶 → MC 1.0 stone slab id 44（metadata 3 = cobblestone；统一取 slab id）
    /* cobble_stairs           */ 67, // t412 圆石楼梯 → MC 1.0 stairs id 67（1.0 楼梯含木/石/cobble 统一 id）
    /* cobble_fence            */ -1, // t412 圆石墙 → MC 1.0 无等价（cobblestone wall id 139 为 1.4+；1.0 仅木栅栏 id 85）
    /* cobble_pressure_plate   */ 70, // t412 圆石压力板 → MC 1.0 stone pressure plate id 70
};
static_assert(sizeof(kMcBlockId) / sizeof(kMcBlockId[0]) == int(BlockRegistry::Count),
              "kMcBlockId 行数须与 BlockRegistry::Count 一致；新方块需补一行 MC 1.0 对齐值");
} // namespace

const BlockRegistry::BlockDef &BlockRegistry::def(quint8 blockId)
{
    if (int(blockId) >= int(Count)) return kDefs[int(Air)]; // 越界 → air 兜底（不可挖掘、不掉落、不实体）
    return kDefs[int(blockId)];
}

int BlockRegistry::tileIndex(quint8 blockId, Face face)
{
    const BlockDef &d = def(blockId); // 越界 → air（topTile=0，与旧 tileFor 兜底同语义）
    switch (face) {
    case Top:    return d.topTile;
    case Bottom: return d.bottomTile;
    case NegZ:   return d.frontTile; // -Z 面 = 「前面」（熔炉炉口朝 -Z；其余方块 == sideTile）
    default:     return d.sideTile;  // +X / -X / +Z
    }
}

bool BlockRegistry::isSolid(quint8 blockId)      { return def(blockId).solid; }
BlockRegistry::Shape BlockRegistry::shape(quint8 blockId) { return def(blockId).shape; }

// t412 异形方块 / 半方块族统一谓词（单一权威，段外圆石变体并入，同 isCrossBillboard 段外 cross 模式）。
//   连续段 [FirstPartial, LastPartial]（6 类木制半方块）+ 段外圆石变体 4 类（CobbleSlab/Stairs/Fence/PressurePlate）。
//   Farmland 不在此谓词内（矮盒渲染经 chunkgeometry 单独并入 PASS 1，非 partial 子 AABB 形状族）。
bool BlockRegistry::isPartialBlock(quint8 blockId)
{
    if (blockId == CobbleSlab || blockId == CobbleStairs
        || blockId == CobbleFence || blockId == CobblePressurePlate) return true; // t412 段外圆石变体
    return blockId >= FirstPartial && blockId <= LastPartial;
}
bool BlockRegistry::isSlab(quint8 blockId)           { return blockId == WoodSlab || blockId == CobbleSlab; }
bool BlockRegistry::isStairs(quint8 blockId)         { return blockId == WoodStairs || blockId == CobbleStairs; }
bool BlockRegistry::isFence(quint8 blockId)          { return blockId == WoodFence || blockId == CobbleFence; }
bool BlockRegistry::isPressurePlate(quint8 blockId)  { return blockId == WoodPressurePlate || blockId == CobblePressurePlate; }

// t412 双半砖合并映射（木 / 石两族共用一套合并与掉落逻辑）：半砖 → 其满格整立方（合并写入目标）；
//   满格整立方 → 其半砖（破坏掉落）。非半砖 / 非双砖源 → Air / 0（兜底）。
quint8 BlockRegistry::slabFullBlock(quint8 slabId)
{
    if (slabId == WoodSlab)   return Planks;
    if (slabId == CobbleSlab) return Cobble;
    return Air;
}
quint8 BlockRegistry::fullBlockSlabDrop(quint8 fullId)
{
    if (fullId == Planks) return WoodSlab;
    if (fullId == Cobble) return CobbleSlab;
    return 0;
}

// t305 cross 广告牌方块统一谓词（单一权威）：连续段 [FirstCross, LastCross]（草丛 / 小麦作物）+ 段外 Sapling(28)
//   + 段外 DeadBush(43)（t394）+ 段外 Mushroom(48)（t396）+ 段外 LilyPad(47)（t396）+ 段外花段 [FirstFlower, LastFlower]
//   （t397 4 色）+ 段外 Sugarcane(53)（t397 细茎）。非连续 cross 方块 id 显式并入（同 Sapling 模式）。
//   mesher / 选中框路由一律读本谓词（避免各持区间判定漂移，见头注释）。
bool BlockRegistry::isCrossBillboard(quint8 blockId)
{
    if (blockId == Sapling) return true;
    if (blockId == DeadBush) return true; // t394 段外 cross（枯死的灌木，同 Sapling 模式）
    if (blockId == Mushroom) return true; // t396 段外 cross（蘑菇，同 Sapling 模式）
    if (blockId == LilyPad) return true;  // t396 cross 路由的横向浮叶（几何水平非竖直 cross，但同走 PASS 1 alphaCutoff 路径，见头注释）
    if (blockId == Sugarcane) return true; // t397 段外 cross（甘蔗细茎，同 Sapling 模式）
    if (blockId == CarrotCrop) return true; // t407 段外 cross（胡萝卜作物，同小麦作物按 state 选阶段贴图）
    if (blockId == PotatoCrop) return true; // t407 段外 cross（马铃薯作物，同小麦作物按 state 选阶段贴图）
    if (blockId >= FirstFlower && blockId <= LastFlower) return true; // t397 段外花段（4 色 cross）
    return blockId >= FirstCross && blockId <= LastCross;
}

// t387 床方块段统一谓词（单一权威）：id ∈ [FirstBed, LastBed]（8 色变体）即床。供 t388 睡觉机制判定「命中格
//   是否床」（右键床 → 跳清晨 + 重生点），避免各处自写 id 区间漂移（同 isCrossBillboard 模式）。连续段，裸区间即可。
bool BlockRegistry::isBed(quint8 blockId)
{
    return blockId >= FirstBed && blockId <= LastBed;
}

// t397 花方块段统一谓词（单一权威）：id ∈ [FirstFlower, LastFlower]（4 色变体）即花。供 worldgen placeFlowers
//   / 放置预检 / 未来花相关机制判定「是否花」，避免各处自写 id 区间漂移（同 isBed / isCrossBillboard 模式）。
//   连续段，裸区间即可。
bool BlockRegistry::isFlower(quint8 blockId)
{
    return blockId >= FirstFlower && blockId <= LastFlower;
}

// 方块是否「有碰撞 sub-AABB」（考虑开合态）。air / torch / water（ShapeNone）→ false。
//   越界 → false（air 兜底）。单一权威：isCollidable 与 collisionAABBs 共用，保证「预判」与「精确碰撞」
//   对开合态一致。t261：门恒挡（门板开合都实存 —— 合贴朝向边 / 开旋 90° 贴铰链侧邻边）。t359：活版门开合都实存
//   （合=水平薄板顶站立 / 开=铰链侧整高竖直板顶站立 —— 「半门 / 1 格高 ledge」，玩家立于板顶 + 蹲行走）。
bool BlockRegistry::isCollidable(quint8 blockId, quint8 state)
{
    switch (def(blockId).shape) {
    case ShapeDoor:     return true;             // t261 门板无论开合都实存（合=贴朝向边 / 开=旋后贴铰链侧），恒挡一面
    case ShapeTrapdoor: return true;             // t359 开合都实存：合=水平薄板顶站立 / 开=铰链侧整高竖直板顶站立（见 collisionAABBs/shapeBoxes）
    case ShapeNone:     return false;            // air / torch / water：无碰撞
    default:            return true;             // Full/Slab/Stairs/Fence/Plate：无开合概念，恒挡
    }
}
float BlockRegistry::hardness(quint8 blockId)    { return def(blockId).hardness; }
int   BlockRegistry::toolType(quint8 blockId)    { return def(blockId).toolType; }
int   BlockRegistry::minToolTier(quint8 blockId) { return def(blockId).minToolTier; }
bool  BlockRegistry::requiresTool(quint8 blockId){ return def(blockId).requiresTool; } // t265 掉落是否需匹配工具
int   BlockRegistry::dropId(quint8 blockId)      { return def(blockId).dropId; }
int   BlockRegistry::dropCount(quint8 blockId)   { return def(blockId).dropCount; }
int   BlockRegistry::maxStack(quint8 blockId)    { return def(blockId).maxStack; }

const char *BlockRegistry::blockName(quint8 blockId)
{
    if (int(blockId) >= int(Count)) return "unknown"; // 与旧契约一致：越界 → "unknown"（非 air.name）
    return kDefs[int(blockId)].name;
}

QString BlockRegistry::displayName(quint8 blockId)
{
    if (int(blockId) >= int(Count)) return QString(); // 越界 → 空串（兜底）
    // 源文件 UTF-8 编码；MinGW GCC 默认 input/exec charset = UTF-8，故 const char* 字面量为
    // UTF-8 字节，fromUtf8 正确解码（与项目既有中文注释同源；跨编译器稳健靠「UTF-8 字面量 + fromUtf8」）。
    return QString::fromUtf8(kDefs[int(blockId)].display);
}

// t146 方块形状 → cell-local [0,1]^3 子 AABB（**state 解码镜像 partialblockgeometry.cpp**，使碰撞/选中
//   形状与渲染形状同源：改一处 state 编码须同步另一处）。异形方块拆 1~2 个轴对齐子盒，与 mesher 的
//   pushBox 几何一一对应（slab 半高 / stairs 下步+背墙 / fence 中心柱 / plate 薄板 / door 薄板 / trapdoor
//   合=水平 / 开=竖直）。air/torch → 空；ShapeFull → 单盒 {0,0,0,1,1,1}。
namespace {
// 朝向 state[1:0] → 朝该向「开」（板在对侧半）。复用于 door / stairs 的薄板半 footprint 推导。
//   与 partialblockgeometry.cpp 同编码：0=+X 1=-X 2=+Z 3=-Z。
struct Half { float a0, a1; }; // 沿某轴的半 footprint 区间（0..0.5 或 0.5..1）
Half facingWall(int facing) {
    switch (facing & 3) {
    case 0: return {0.0f, 0.5f};  // 朝 +X 开 → 板在 -X 半
    case 1: return {0.5f, 1.0f};  // 朝 -X 开 → 板在 +X 半
    default: return {0.0f, 1.0f}; // codereview H1: +Z/-Z 向（X 轴全 footprint）——原 {0,0} 零体积致楼梯墙无碰撞
    }
}
Half facingWallZ(int facing) {
    switch (facing & 3) {
    case 2: return {0.0f, 0.5f};  // 朝 +Z 开 → 板在 -Z 半
    case 3: return {0.5f, 1.0f};  // 朝 -Z 开 → 板在 +Z 半
    default: return {0.0f, 1.0f}; // codereview H1: +X/-X 向（Z 轴全 footprint）——原 {0,0} 零体积致楼梯墙无碰撞
    }
}
std::vector<BlockRegistry::BlockAABB> shapeBoxes(BlockRegistry::Shape sh, quint8 state)
{
    std::vector<BlockRegistry::BlockAABB> out;
    switch (sh) {
    case BlockRegistry::ShapeFull:
        out.push_back({0, 0, 0, 1, 1, 1});
        return out;
    case BlockRegistry::ShapeNone:
        return out; // air / torch：无碰撞 sub-AABB
    case BlockRegistry::ShapeSlab: {
        const bool upper = (state & 1) != 0;
        out.push_back(upper ? BlockRegistry::BlockAABB{0, 0.5f, 0, 1, 1, 1}
                            : BlockRegistry::BlockAABB{0, 0, 0, 1, 0.5f, 1});
        return out;
    }
    case BlockRegistry::ShapeStairs: {
        // 整步（全 footprint 半高）+ 背墙（朝向对侧半 footprint 的另半高）。
        //   state[1:0]=朝向；t147 bit2=倒置 → 整步/背墙 y 区间垂直镜像（与 partialblockgeometry.cpp 同编码）。
        const bool inverted = (state & 4) != 0;
        const float stepY0 = inverted ? 0.5f : 0.0f, stepY1 = inverted ? 1.0f : 0.5f;
        const float wallY0 = inverted ? 0.0f : 0.5f, wallY1 = inverted ? 0.5f : 1.0f;
        out.push_back({0, stepY0, 0, 1, stepY1, 1});
        const Half hx = facingWall(int(state));
        const Half hz = facingWallZ(int(state));
        out.push_back({hx.a0, wallY0, hz.a0, hx.a1, wallY1, hz.a1});
        return out;
    }
    case BlockRegistry::ShapeFence:
        // t209 立柱 1.5 高（与 partialblockgeometry 渲染立柱同高）。maxY=1.5 探入上格 0.5 → 玩家跳跃顶点
        //   ~1.25 < 1.5 跳不过（机制等价 MC 栅栏 1.5 高不可越）。仅立柱碰撞（横档纯视觉，不进 AABB；
        //   机制等价 MC 栅栏 VoxelShape 仅立柱）。玩家跨格 X/Z 移动 + 跳跃时，PlayerController::overlapSubAABBs
        //   的 Y 取样向下扩 1 格（catch 上格以下立柱探入的 AABB），故 1.5 高碰撞对跳跃 / 立柱顶站立均生效。
        out.push_back({0.3f, 0, 0.3f, 0.7f, 1.5f, 0.7f}); // 中心立柱 0.4 见方 × 1.5 高（与 mesher 同）
        return out;
    case BlockRegistry::ShapePlate:
        out.push_back({0.0625f, 0, 0.0625f, 0.9375f, 0.0625f, 0.9375f}); // 贴地薄板 1/16 厚
        return out;
    case BlockRegistry::ShapeDoor: {
        // 合：薄板贴朝向边（厚 3/16）；开：板旋 90° 贴邻边。满高（y 0..1）。
        const int facing = state & 3;
        const bool open = (state & 4) != 0;
        const float t0 = 0.8125f, t1 = 1.0f, s0 = 0.0f, s1 = 0.1875f; // 厚 3/16
        float bx0 = 0, bx1 = 1, bz0 = 0, bz1 = 1;
        if (!open) {
            switch (facing) {
            case 0: bx0 = t0; bx1 = t1; break; // 朝 +X → 板贴 +X 边
            case 1: bx0 = s0; bx1 = s1; break; // 朝 -X → 板贴 -X 边
            case 2: bz0 = t0; bz1 = t1; break; // 朝 +Z → 板贴 +Z 边
            case 3: bz0 = s0; bz1 = s1; break; // 朝 -Z → 板贴 -Z 边
            }
        } else {
            switch (facing) {
            case 0: bz0 = t0; bz1 = t1; break; // 原 +X → 旋到 +Z 边
            case 1: bz0 = s0; bz1 = s1; break; // 原 -X → 旋到 -Z 边
            case 2: bx0 = t0; bx1 = t1; break; // 原 +Z → 旋到 +X 边
            case 3: bx0 = s0; bx1 = s1; break; // 原 -Z → 旋到 -X 边
            }
        }
        out.push_back({bx0, 0, bz0, bx1, 1, bz1});
        return out;
    }
    case BlockRegistry::ShapeTrapdoor: {
        const bool open = (state & 1) != 0;
        if (!open) {
            out.push_back({0, 0, 0, 1, 0.1875f, 1}); // 合：水平薄板贴地
        } else {
            const int facing = (state >> 1) & 3;
            const float t0 = 0.8125f, t1 = 1.0f, s0 = 0.0f, s1 = 0.1875f;
            float bx0 = 0, bx1 = 1, bz0 = 0, bz1 = 1;
            switch (facing) {
            case 0: bx0 = t0; bx1 = t1; break; // +X 边
            case 1: bx0 = s0; bx1 = s1; break; // -X 边
            case 2: bz0 = t0; bz1 = t1; break; // +Z 边
            case 3: bz0 = s0; bz1 = s1; break; // -Z 边
            }
            out.push_back({bx0, 0, bz0, bx1, 1, bz1}); // 开：竖直薄板贴边
        }
        return out;
    }
    }
    return out; // 未知 shape → 空（兜底）
}
} // namespace

// collision 与 selection **同源**（t217 修正 t208）：两者都走 shapeBoxes（贴合渲染形状：门=薄板选中框、
//   与视觉一致）。开门（t261）：门板旋 90° 贴铰链侧邻边 → shapeBoxes 返回「旋后贴边」panel AABB，
//   collisionAABBs 非空 → 玩家撞门板被挡、门洞方向可穿过（修「开门四向全通」）。活版门开态（t359）→ 走 shapeBoxes
//   整高竖直板（铰链侧 y[0,1]，可立于顶 + 蹲行走；t335 的唇边特例因薄于玩家 footprint 半宽未真支撑已废）。
//
// t208 曾对门合态返回**满格整立方** {0,0,0,1,1,1}（防穿隧道 + 单格门），代价是关门时四面皆挡——玩家
//   「不打开门完全进不去」，违反 MC 门「门占一面薄板、仅门面板那一面合时挡 / 开则通、其余恒通」语义
//   （dev-plan t217）。t217 改回薄板碰撞，t208 满格的两条理由都已结构性消除：
//   (1) creative-fly 路径已补子步（playercontroller.cpp step() creative-fly 分支：任意轴单步 ≤0.4 格）→
//       子步位移 < 玩家宽 0.6 → 必与路径上薄板（厚 3/16=0.1875）重叠被检出（穿隧道阈值 0.7875 > 0.4）。
//   (2) 门放置已强制两格（playercontroller placeBlock 查 ty+1 在界内且为空气，否则整门拒）→ 单格门根因 2 消除。
//   故薄板碰撞成立：门仅在其面板法线轴上合时挡（沿门板法线穿越被阻），开门则门板旋到铰链侧邻边仍挡那一面
//   （t261）；门板切线轴（与面板平行的两侧）恒通——玩家可贴门板侧面走过。selectionAABBs 同源 → 选中框
//   + F3+B 碰撞箱均显薄板。
std::vector<BlockRegistry::BlockAABB> BlockRegistry::collisionAABBs(quint8 blockId, quint8 state)
{
    if (!isCollidable(blockId, state)) return {}; // air / torch / water → 无碰撞 sub-AABB（玩家穿过）
    // t234 耕地碰撞略矮（15/16=0.9375）：机制等价 MC 耕地碰撞箱比整立方矮 1 像素。Farmland 走 ShapeFull
    //   （mesher 邻居面剔除 + raycast isFullCube=true 整格命中 + selectionAABBs 整格选中框，三者不动），
    //   仅碰撞在此特例返矮盒 → 玩家脚位停在 cell+0.9375（渲染顶面 cell+1.0 略高于脚位 → 视觉如站在浅翻耕沟，
    //   同 MC 耕地观感）。与 selectionAABBs 解耦：选中框仍整格（玩家瞄准/破块按整格，无 1/16 误差烦恼）。
    if (blockId == Farmland)
        return {BlockAABB{0, 0, 0, 1, 0.9375f, 1}};
    // t359 活版门开态碰撞 = 整高竖直板（同 shapeBoxes，无特例覆盖）。机制等价「半门 / 1 格高 ledge」：
    //   开活板门铰链侧整高 [0,1] 竖直板可站立于顶（y=1.0）+ 蹲行走 → 不再穿透。
    //   t335 曾对此返「铰链侧 3/16 宽 × 3/16 高的唇边」(板身穿过)，但唇边太薄（0.1875 < 玩家 footprint
    //   半宽 0.3）：玩家中心立于格内时 footprint [0.2,0.8] 不触铰链条 [0.8125,1.0] → 无支撑穿透（t335 未真修复、
    //   t359 复发根因）。现直接走 shapeBoxes（开=整高板 y[0,1]），与渲染 / selectionAABBs 三者同源；脚下支撑复探
    //   （playercontroller step() 脚底 -0.05 探地）取该板顶面 → 站稳。合态（state bit0=0）shapeBoxes 返水平薄板
    //   y[0,0.1875] → 顶面行走（不变）。
    return shapeBoxes(def(blockId).shape, state);
}
std::vector<BlockRegistry::BlockAABB> BlockRegistry::selectionAABBs(quint8 blockId, quint8 state)
{
    return shapeBoxes(def(blockId).shape, state);
}

// t213 isFullCube（dev-plan R18d 三任务共用谓词）：shape == ShapeFull 即整格立方。water/torch/air
//   （ShapeNone）与不完整方块段（ShapeSlab/...）→ false；常规整立方（grass..chest）→ true。
bool BlockRegistry::isFullCube(quint8 blockId)
{
    return def(blockId).shape == ShapeFull;
}

// t334 per-block 光衰减量（见头注释）。flood-fill 据本值算邻格衰减 = max(1, lightOpacity)，取代旧 isSolid 二值遮光。
//   全实体方块（isSolid）满遮光 → 15（保旧语义）；活版门合=15 / 开=0；台阶=7（半遮）；其余 → 0（全透）。
quint8 BlockRegistry::lightOpacity(quint8 blockId, quint8 state)
{
    if (isSolid(blockId)) return 15;                  // 全实体方块：满遮光（保旧 isSolid 光照语义）
    switch (blockId) {
    case WoodTrapdoor: return (state & 1) ? 0 : 15;   // 合=满遮（修「合活版门透光」）/ 开=全透
    case WoodSlab:     return 7;                      // 半遮光（占空比 0.5 → floor(0.5×15)=7 → 衰减 7，约半减）
    case CobbleSlab:   return 7;                      // t412 圆石台阶半遮光（同 WoodSlab，半高占空比 0.5）
    case Farmland:     return 15;                     // t408 耕地 solid=false（矮盒渲染）但仍是 opaque 土块 → 满遮光
    default:           return 0;                      // 其余全透（air/torch/water/stairs/fence/plate/door/cross）
    }
}

// t351 方块自发光强度（见头注释）：火把=14（既有）、岩浆=15（地底发光，MC 1.0 岩浆光 level 15）、其余 0。
quint8 BlockRegistry::lightEmission(quint8 blockId)
{
    switch (blockId) {
    case Torch: return 14;  // 既有：火把方块光种子 14（radius14 泛光）
    case Lava:  return 15;  // t351：岩浆方块光种子 15（地底发光照亮洞穴；MC 1.0 岩浆光 level 15）
    default:    return 0;   // 其余不自发光
    }
}

// t360 列顶实面 Y 偏移（见头注释）：PCF 软影用本值替代「heightmap+1.0 整格」假设，按方块真实模型高度判遮挡。
//   与 shapeBoxes 的 maxY 同源（单一权威：改形状只改一处），但免建 vector —— PCF 热路径每顶点 16 次查询。
float BlockRegistry::solidTopOffset(quint8 blockId, quint8 state)
{
    switch (def(blockId).shape) {
    case ShapeSlab:     return (state & 1) ? 1.0f : 0.5f;     // 上半砖顶=1.0 / 下半砖顶=0.5
    case ShapeTrapdoor: return (state & 1) ? 1.0f : 0.1875f;  // 开=竖直板到顶 1.0 / 合=水平薄板顶 0.1875
    case ShapeFence:    return 1.5f;                          // 中心立柱 1.5 高
    case ShapePlate:    return 0.0625f;                       // 贴地薄板 1/16 高
    case ShapeStairs:   return 1.0f;                          // 背墙到顶（整步+背墙最高点 = cellY+1）
    case ShapeFull:     return 1.0f;                          // 整立方
    case ShapeDoor:     return 1.0f;                          // 满高薄板
    default:            return 1.0f;                          // ShapeNone（air/torch/water）不入 heightmap 顶，兜底 1.0
    }
}

// t213 射线命中 sub-AABB：射线进入含该方块的体素后须命中某个 sub-AABB 才算选中（不完整方块/火把的空气
//   部分让射线穿过命中后方块）。完整立方 / air / water → 整格单盒（进格即中，等同旧行为；water 经 HitWater
//   命中整格舀水）；不完整方块段 → 同 selectionAABBs（实体 sub 形状，与渲染/碰撞同源）；火把（ShapeNone，
//   selectionAABBs 空）→ 中央小立柱盒（贴火把视觉范围：覆盖 up 立柱 + 各墙向焰心，格角落空气穿过）。
//   火把朝向由邻居定（呈现层持），Core 无 World → 取保守中央区覆盖所有朝向的焰 + 立柱中段（焰恒在格中央偏上）。
std::vector<BlockRegistry::BlockAABB> BlockRegistry::raycastAABBs(quint8 blockId, quint8 state)
{
    const Shape sh = def(blockId).shape;
    if (sh == ShapeFull)
        return {BlockAABB{0, 0, 0, 1, 1, 1}}; // 整格：射线进格即中（等同旧行为）
    if (sh == ShapeNone) {
        if (blockId == Torch)
            // 火把中央立柱 0.4 见方 × 0.85 高：up 立柱 [0.44,0.56]×[0,0.7] + 各墙向焰心 (≈0.5,≈0.8) 均落此盒；
            //   格角落 / 顶部以上空气穿过 → 修「挖火把背后的方块却撸掉火把」。朝向由邻居定（呈现层持），
            //   此处保守中央区覆盖所有朝向焰 + 立柱中段（瞄焰/柄命中、瞄角落穿）。
            return {BlockAABB{0.3f, 0.0f, 0.3f, 0.7f, 0.85f, 0.7f}};
        return {BlockAABB{0, 0, 0, 1, 1, 1}}; // air / water → 整格（air 不进本路径兜底；water 整格舀水）
    }
    // 不完整方块段（ShapeSlab/...）→ 同 selectionAABBs（实体 sub 形状；空气部分穿过命中后方块）。
    return shapeBoxes(sh, state);
}

// t214 火把附着方向由命中面外法线推导（与 Main.qml orientFromNormal 同源）：ny>0（点中顶面）→ 立柱
//   TorchFloor（支撑 = 下方）；±X / ±Z 面各映射到对侧邻（normal 指向玩家侧，火把在 hitBlock + normal 侧，
//   故其支撑 = 火把的反法线邻 = hitBlock）。无轴向命中（不应发生）→ TorchFloor 兜底。
BlockRegistry::TorchAttach BlockRegistry::torchOrientFromNormal(int nx, int ny, int nz)
{
    if (ny > 0) return TorchFloor;
    if (nx > 0) return TorchOnNX; // 点中 +X 面 → 火把在 +X 侧 → 支撑 -X 邻（QML "px"）
    if (nx < 0) return TorchOnPX; // 点中 -X 面 → 火把在 -X 侧 → 支撑 +X 邻（QML "nx"）
    if (nz > 0) return TorchOnNZ; // 点中 +Z 面 → 火把在 +Z 侧 → 支撑 -Z 邻（QML "pz"）
    if (nz < 0) return TorchOnPZ; // 点中 -Z 面 → 火把在 -Z 侧 → 支撑 +Z 邻（QML "nz"）
    return TorchFloor;
}

// t214 火把支撑邻居相对偏移（state → 附着格相对坐标）。越界 state 值（> TorchOnPZ）→ TorchFloor 兜底
//   （防读脏 state 崩；旧存档 state=0 即 TorchFloor，行为对齐地面火把）。
void BlockRegistry::torchAttachOffset(quint8 state, int &dx, int &dy, int &dz)
{
    switch (state) {
    case TorchFloor: dx =  0; dy = -1; dz =  0; return; // 立柱：支撑 = 下方
    case TorchOnNX:  dx = -1; dy =  0; dz =  0; return; // 柄伸 +X：支撑 = -X 邻
    case TorchOnPX:  dx =  1; dy =  0; dz =  0; return; // 柄伸 -X：支撑 = +X 邻
    case TorchOnNZ:  dx =  0; dy =  0; dz = -1; return; // 柄伸 +Z：支撑 = -Z 邻
    case TorchOnPZ:  dx =  0; dy =  0; dz =  1; return; // 柄伸 -Z：支撑 = +Z 邻
    default:         dx =  0; dy = -1; dz =  0; return; // 越界 → 地面火把兜底
    }
}

// t225 箱子前面（锁面）所朝 Face（state 低 2 位解码，与 horizontalFacing 同源 0=+X 1=-X 2=+Z 3=-Z）。
//   越界高位忽略（& 3）；mesher 据此把 chest_front 贴到对应面。无需「兜底」分支 —— 低 2 位四值全合法。
BlockRegistry::Face BlockRegistry::chestFrontFace(quint8 state)
{
    switch (state & 3) {
    case 0: return PosX;
    case 1: return NegX;
    case 2: return PosZ;
    default: return NegZ;
    }
}

// 音效材质分组（t118）：id → MaterialGroup 纯函数（按 BlockRegistry::Id 枚举值分支，单一权威）。// AudioManager 据此选 break / mining / step 音色；越界 / air / torch / 未知 → GroupDefault
// （AudioManager 内部用 GroupStone 兜底播放，避免缺组静默）。
// 机制等价 MC「方块 → SoundType」（机制对齐，非名词照搬）。新方块追加时按材质归入对应组或补新组。
BlockRegistry::MaterialGroup BlockRegistry::materialGroup(quint8 blockId)
{
    switch (blockId) {
    case Stone: case Cobble: case Furnace: case CoalOre: case IronOre: case DiamondOre:
    case CopperOre: case GoldOre: // t308 铜/金矿石 → 石质音色（同 coal/iron/diamond 矿石族）
    case Spawner: // t392 刷怪笼 → 石质音色（铁笼金属敲击感，最接近 MC 1.0 刷怪笼 metal SoundType）
    case Sandstone: // t394 砂岩 → 石质音色（成岩，同 cobble/stone 族）
    case Obsidian: // t411 黑曜石 → 石质音色（致密火山玻璃，同 cobble/stone 族）
    case CobbleSlab: case CobbleStairs: case CobbleFence: case CobblePressurePlate: // t412 圆石变体 → 石质音色（同 cobble 族）
        return GroupStone;
    case Ice: // t395 冰 → 石质音色（玻璃质敲击，最接近 MC 1.0 冰 glass SoundType）
    case Glass: // t405 玻璃 → 石质音色（玻璃质敲击，最接近 MC 1.0 玻璃 glass SoundType，同 ice）
        return GroupStone;
    case Log: case Planks: case CraftingTable:
    case WoodSlab: case WoodStairs: case WoodFence:
    case WoodPressurePlate: case WoodDoor: case WoodTrapdoor: // t134 木制半方块 → 木质音色
    case Chest: // t173 箱子 → 木质音色
    case Wool: // t300 羊毛 → 木质音色（软质闷击，最接近 MC 1.0 羊毛 cloth SoundType）
    case BedRed: case BedOrange: case BedYellow: case BedGreen: // t387 床 → 木质音色（软质被面闷击，同 wool）
    case BedCyan: case BedBlue: case BedMagenta: case BedBlack:
    case SpruceLog: // t395 云杉原木 → 木质音色（同 log / planks 族）
        return GroupWood;
    case Grass: case Dirt:
    case Farmland: // t234 耕地 → 软土音色（同 grass/dirt；机制等价 MC 耕地 SoundType = ground）
    case TallGrass: // t235 草丛 → 软草音色（同 grass；机制等价 MC 草丛 SoundType = grass）
    case WheatCrop: // t236 小麦作物 → 软草音色（同草丛；机制等价 MC 作物 SoundType = grass）
    case Sapling: // t305 树苗 → 软草音色（同草丛 / 作物；机制等价 MC 树苗 SoundType = grass）
    case Cactus: // t394 仙人掌 → 软草音色（植物，软质多肉；机制等价 MC 仙人掌 SoundType = cloth，本工程取软草近似）
    case DeadBush: // t394 枯死的灌木 → 软草音色（枯枝软质，同草丛；机制等价 MC dead bush SoundType = grass）
    case LilyPad: // t396 睡莲 → 软草音色（浮叶软质植物，同草丛；机制等价 MC lily pad SoundType = grass）
    case Mushroom: // t396 蘑菇 → 软草音色（软质真菌，同草丛；机制等价 MC mushroom SoundType = grass / stone 取软草近似）
    case FlowerRed: case FlowerYellow: case FlowerBlue: case FlowerWhite: // t397 4 色花 → 软草音色（软植物，同草丛；机制等价 MC 花 SoundType = grass）
    case Sugarcane: // t397 甘蔗 → 软草音色（细茎软植物，同草丛；机制等价 MC sugar cane SoundType = grass）
    case CarrotCrop: // t407 胡萝卜作物 → 软草音色（同小麦作物；机制等价 MC 作物 SoundType = grass）
    case PotatoCrop: // t407 马铃薯作物 → 软草音色（同小麦作物；机制等价 MC 作物 SoundType = grass）
        return GroupGrass;
    case Sand:
    case SnowLayer: // t395 积雪层 → 颗粒雪响（软质颗粒，最接近 MC 1.0 雪 snow SoundType）
        return GroupSand;
    case Leaves:
        return GroupLeaves;
    default:
        return GroupDefault; // air / torch / water / 越界 / 未知 → 兜底（AudioManager 复用 Stone 音色）
    }
}

// t348 引擎方块 id → MC Java 1.0.0 方块数字 id（资源包前置；见 kMcBlockId 表注释）。越界 id → -1（无 MC 等价，
//   资源包回退引擎过程化贴图）。
int BlockRegistry::mcBlockId(quint8 engineId)
{
    if (int(engineId) >= int(Count)) return -1; // 越界 → 无 MC 等价
    return kMcBlockId[int(engineId)];
}
