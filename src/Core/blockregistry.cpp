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
// t444 睡莲薄板碰撞顶面高度：与 partialblockgeometry.cpp LilyPad case 的浮叶 quad 高度 yp=1/16 同源
//   （玩家脚位停在睡莲顶面 = 水面 + 1/16，视觉立于浮叶上）。单一常量避免两处魔数漂移。
constexpr float kLilyPadTop = 1.0f / 16.0f;
constexpr BlockRegistry::BlockDef kDefs[int(BlockRegistry::Count)] = {
    /* air            */ {int(BlockRegistry::Air),           0,  0, 0,  0,  false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false,                            0, 0,  0, "air",            ""},
    /* grass          */ {int(BlockRegistry::Grass),         0,  2, 1,  1,  true,  BlockRegistry::ShapeFull,     0.6f, int(BlockRegistry::Shovel),  0, false, int(BlockRegistry::Dirt),          1, 64, "grass",          "草方块"}, // t265 铲加速（requiresTool=false 空手仍掉落）；顶=grass_top 底=dirt 侧=grass_side。**t500 dropId=Dirt**（生存挖掉泥土，机制等价 MC 1.0「草方块非精准采集掉泥土」）；精准采集附魔覆盖掉 Grass 自身（playercontroller finishMiningAt silk 分支）
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
    /* bed_red      */ {int(BlockRegistry::BedRed),                    43, 43, 43, 43, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedRed),        1, 64, "bed_red",      "红色床"}, // 配方产物（planks+wool）；默认色
    /* bed_orange   */ {int(BlockRegistry::BedOrange),                 44, 44, 44, 44, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedOrange),     1, 64, "bed_orange",   "橙色床"},
    /* bed_yellow   */ {int(BlockRegistry::BedYellow),                 45, 45, 45, 45, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedYellow),     1, 64, "bed_yellow",   "黄色床"},
    /* bed_green    */ {int(BlockRegistry::BedGreen),                  46, 46, 46, 46, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedGreen),      1, 64, "bed_green",    "绿色床"},
    /* bed_cyan     */ {int(BlockRegistry::BedCyan),                   47, 47, 47, 47, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedCyan),       1, 64, "bed_cyan",     "青色床"},
    /* bed_blue     */ {int(BlockRegistry::BedBlue),                   48, 48, 48, 48, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedBlue),       1, 64, "bed_blue",     "蓝色床"},
    /* bed_magenta  */ {int(BlockRegistry::BedMagenta),                49, 49, 49, 49, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedMagenta),    1, 64, "bed_magenta",  "品红色床"},
    /* bed_black    */ {int(BlockRegistry::BedBlack),                  50, 50, 50, 50, false, BlockRegistry::ShapeBed,     0.2f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::BedBlack),      1, 64, "bed_black",    "黑色床"},
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
    //   仙人掌（Cactus）：沙漠标志性植物方块（接触伤害实体）。**t445 几何缩到 ~80% 居中**：mesher 经
    //   PartialBlockGeometry 画 0.8×1.0×0.8 居中柱（X/Z [0.1,0.9]、Y 满高；机制对标 MC 1.0 仙人掌 14/16 细柱），
    //   非满格整立方。solid=**false**（同 Farmland / glass 模式：非满格渲染 → 不挡邻居面剔除 → 下方沙顶面画出、
    //   填住柱底 0.1 环隙防 x-ray 缝；碰撞 / 选中仍走 ShapeFull 整格，与渲染解耦）。shape=ShapeFull（整立方实体碰撞 →
    //   mob/玩家撞其侧或站其上即「接触」，接触伤害由 EntityManager/PlayerController 环境 tick 处理）、hardness=0.4
    //   （同 MC 1.0 仙人掌量级，软质）、toolType=NoTool（空手即采且掉落，机制等价 MC 仙人掌无工具要求）、
    //   requiresTool=false、dropId=自身（破仙人掌掉仙人掌方块，可放回）、dropCount=1、maxStack=64。各面贴图：顶·底=
    //   cactus_top(54)（绿截面 + 同心方框环纹）/ 侧=cactus_side(55)（深绿底 + 4 垂直棱脊 + 棱上刺点）。音色归
    //   GroupGrass（植物，软质）。worldgen 在 desert 沙顶散布 1-3 格高柱；放置预检（placeBlock）须 Sand/Cactus 在
    //   下方 + **水平 4 邻无方块**（t445 ④）；失撑 / 邻接方块 → World 把整柱转掉落物（t445 ②/④）。进创造调色板。
    /* cactus       */ {int(BlockRegistry::Cactus),                     54, 54, 55, 55, false, BlockRegistry::ShapeFull,     0.4f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::Cactus),         1, 64, "cactus",       "仙人掌"},
    //   枯死的灌木（DeadBush）：沙漠干旱地表枯枝装饰。cross 形广告牌方块（与 TallGrass/Sapling 同走 cross 几何段，两片
    //   对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。solid=false（非实体 → 不挡邻居面剔除，同 torch/
    //   草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过）、hardness=0（瞬破）、NoTool（空手可采）、dropId=0（破枯灌木
    //   无掉落，机制等价 MC 空手破 dead bush 无产物；创造调色板取用即得）、dropCount=0、maxStack=64。各面贴图=
    //   dead_bush(56)（透明底 + 棕褐放射干枝；alphaCutoff cutout）。音色归 GroupGrass（软草音）。worldgen 在 desert
    //   沙顶低密度散布；放置预检须 Sand 在下方。**段外 cross**（id 43 不在 [FirstCross,LastCross] 连续段内）→ 并入
    //   isCrossBillboard 谓词（同 Sapling 模式），mesher 路由一律读谓词。进创造调色板（装饰取用）。
    /* dead_bush    */ {int(BlockRegistry::DeadBush),                   56, 56, 56, 56, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false,                            0, 0, 64, "dead_bush",    "枯死的灌木"},
    // ── t395 雪原/针叶群系内容（机制等价 MC 1.0 寒冷群系三件套：snow / ice / spruce log；名称 / 贴图全原创自绘 §9a）：
    // ── t395 雪原/针叶群系内容（机制等价 MC 1.0 寒冷群系三件套：snow / ice / spruce log；名称 / 贴图全原创自绘 §9a）：
    //   积雪层（SnowLayer）：**t505 改薄层**（机制等价 MC 1.0 snow layer 8 层）——贴地薄板，高度由 state 驱动
    //   （state 0..7 → 高度 (state+1)/8，1/8..1.0 八级；MC 薄雪层可堆 8 层、玩家可踩 + 半格平滑 auto-step 上行）。
    //   solid=false / ShapeSnowLayer（走 PartialBlockGeometry 薄板渲染，**非**整立方；同 Farmland / glass 模式 ——
    //   solid=false → 相邻整立方不剔面、画出满高侧壁填住薄层上方缺口，防透视 x-ray 洞）。collision/selection =
    //   cell 底薄板 {0,0,0,1,height,1}（玩家立于薄层顶 = cell+height；高度 ≤0.5 时玩家 t163 auto-step 抬升 0.55
    //   即可跨过，无需跳）。hardness=0.2（软质）、**toolType=Shovel + requiresTool=true + minTier=0**（铲挖掉雪球；
    //   空手挖不掉落 —— 机制等价 MC 雪层铲挖掉雪球、空手无掉落）、dropId=0x23D（雪球 SnowballId，t510）、
    //   dropCount=1（基础兜底，每层雪球数由 playercontroller 按 state+1 精确掉落，同 WheatCrop 按 state 掉落模式）、
    //   maxStack=64。各面贴图=snow(57)（冷白底 + 细密冰晶噪点）。音色归 GroupSand（颗粒雪响）。进创造调色板。
    /* snow_layer   */ {int(BlockRegistry::SnowLayer),                  57, 57, 57, 57, false, BlockRegistry::ShapeSnowLayer, 0.2f, int(BlockRegistry::Shovel), 0, true,                            0x23D, 1, 64, "snow_layer",   "积雪层"},
    //   冰（Ice）：雪原/针叶群系水面冻结产物（worldgen freezeSurfaceWater / tickIceFreeze 把 Snowy 群系暴露天空
    //   的水源冻结为冰）。**t468 改透明整立方**（机制等价 MC 1.0 半透冰）：solid=**false** / ShapeFull（同 glass 契约 ——
    //   solid=false → 相邻实体方块不剔面 → 透过半透冰可见背后方块；碰撞 / 选中仍走 ShapeFull 整格可踩；走 mesher 的
    //   iceOnly 段 Blend 半透渲染，非旧 t395 不透明整立方）、hardness=0.5（同 MC 1.0 冰量级）、toolType=Pickaxe、
    //   requiresTool=true、minToolTier=1（木镐可破）、**dropId=0**（破冰不掉落 —— 机制等价 MC 1.0 冰需精准采集才掉落，
    //   本工程无精准采集故恒不掉落）、dropCount=0、maxStack=64。各面贴图=ice(58)（浅蓝底 + 反光裂纹）。音色归
    //   GroupStone（玻璃质敲击，最接近 MC 1.0 冰 glass SoundType）。冰上低摩擦（iceSlipApproach 中等）。进创造调色板。
    /* ice          */ {int(BlockRegistry::Ice),                        58, 58, 58, 58, false, BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::Pickaxe), 1, true,                             0, 0, 64, "ice",          "冰"},
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
    //   水平非竖直）。solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone（selection 空 / 不进 heightmap /
    //   不遮光；raycast 整格命中），但 **t444 碰撞特例**（collisionAABBs 返 cell 底 1/16 薄板 + isCollidable true）→
    //   玩家立于睡莲顶面不掉进水（水上行走辅助）。其余 ShapeNone 语义不变、hardness=0（瞬破）、NoTool（空手可采且掉落）、dropId=自身（破睡莲掉
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
    // ── t411 黑曜石（Obsidian）：机制等价 MC 1.0 obsidian（流体交互凝固产物——见 World::tickWaterFlow / tickLavaFlow
    //   流体交互 pass：流水触静岩浆源 / 静水源触静岩浆源 → 本方块）。整立方 opaque（solid=true / ShapeFull —— 走 mesher
    //   整立方面路径，**非**异形，与 stone/cobble/sandstone 同族）、hardness=50.0（同 MC 1.0 obsidian 量级，极硬极耐挖 ——
    //   t472 由 12.0 提到 MC 真值 50.0：钻石镐 speedMul 8.0 → 6.25s 慢挖（spec「+ slow」），低档镐 / 空手 speedMul 1.0 →
    //   50s 极慢且不掉落）、toolType=Pickaxe（石族）、requiresTool=true、minToolTier=4（**t472 需钻石镐** —— tier<4 的镐
    //   miningSpeedMul 恒 1.0（慢）+ canHarvest false（破后仅 AIR，spec「hand / lower-tier pick -> NO drop」）；仅钻石镐
    //   tier 4 给速度加成且掉落）、dropId=自身（破黑曜石掉黑曜石方块，可放置）、dropCount=1、maxStack=64。
    //   各面贴图=obsidian(77)（深紫黑火山玻璃底 + 紫红纹理嵌点 + 少量品紫玻璃微反光，原创自绘 §9a）。音色归 GroupStone
    //   （石质）。worldgen 不直接生成（仅由流体交互产生），不进创造调色板（系统获得语义，同 ice）。**抗爆**：destroySphereSilent
    //   跳过本方块（机制等价 MC obsidian 爆炸抗性 6000，免疫 Stalker/TNT 爆炸）。
    /* obsidian     */ {int(BlockRegistry::Obsidian),                       77, 77, 77, 77, true,  BlockRegistry::ShapeFull,    50.0f, int(BlockRegistry::Pickaxe), 4, true,  int(BlockRegistry::Obsidian),      1, 64, "obsidian",     "黑曜石"},
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
    // ── t413 垂直爬梯（vertical climb ladder）：机制等价 MC 1.0 梯子（ladder）。cross 形广告牌方块（两片对角相交双面 quad，
    //   贴 ladder(78) 瓦片，alpha 透明底 cutout）。solid=false（非实体 → 不挡邻居面剔除，同草丛）、shape=ShapeNone（无碰撞 →
    //   玩家穿入梯格；爬升走 PlayerController 物理，非碰撞）、hardness=0.4（同 MC 1.0 梯子量级，软质木质）、toolType=Axe
    //   （木质；requiresTool=false → 空手也掉落，仅速度受斧影响）、dropId=自身（破梯掉梯可放回）、dropCount=1、maxStack=64。
    //   各面贴图=ladder(78)；音色归 GroupWood。进创造调色板。t501 state[1:0] 编码所贴墙面水平方向（0=+X 1=-X 2=+Z
    //   3=-Z）；mesher（单片贴墙 quad）+ finishMiningAt（失撑掉落）读 state；collision/选中仍不读（ShapeNone）。
    /* ladder       */ {int(BlockRegistry::Ladder),                       78, 78, 78, 78, false, BlockRegistry::ShapeNone,     0.4f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::Ladder),           1, 64, "ladder",       "木梯"}, // t413/t501 贴墙竖直爬行梯（玩家入格+按前向上爬）；贴完整方块侧；state[1:0]=贴墙方向
    // ── t455 16 色 wool 其余 15 色变体（white 复用既有 Wool=27；本段 orange..black）。机制等价 MC 1.0 羊毛 16 色
    //   变体。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 white Wool 同族）、
    //   hardness=0.8、toolType=Shears（requiresTool=false 空手也掉落，仅剪刀给速度加成）、dropId=自身（破块掉同色
    //   羊毛方块，可放置）、dropCount=1、maxStack=64。各面贴图=default_wool_<color>（tile 79..93；卷绒纹 + 标准 16
    //   色着色，原创自绘 §9a）。音色归 GroupWood（同 white Wool）。**获得途径**：创造调色板直接取用（无染料系统，
    //   机制对齐 MC「彩色羊毛需染料」但染料留后续）。进创造调色板（每色独立取用 + 右键放置）。
    /* wool_orange     */ {int(BlockRegistry::WoolOrange),      79, 79, 79, 79, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolOrange),     1, 64, "wool_orange",     "橙色羊毛"},
    /* wool_magenta    */ {int(BlockRegistry::WoolMagenta),     80, 80, 80, 80, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolMagenta),    1, 64, "wool_magenta",    "品红色羊毛"},
    /* wool_light_blue */ {int(BlockRegistry::WoolLightBlue),   81, 81, 81, 81, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolLightBlue),  1, 64, "wool_light_blue", "浅蓝色羊毛"},
    /* wool_yellow     */ {int(BlockRegistry::WoolYellow),      82, 82, 82, 82, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolYellow),     1, 64, "wool_yellow",     "黄色羊毛"},
    /* wool_lime       */ {int(BlockRegistry::WoolLime),        83, 83, 83, 83, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolLime),       1, 64, "wool_lime",       "柠绿色羊毛"},
    /* wool_pink       */ {int(BlockRegistry::WoolPink),        84, 84, 84, 84, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolPink),       1, 64, "wool_pink",       "粉红色羊毛"},
    /* wool_gray       */ {int(BlockRegistry::WoolGray),        85, 85, 85, 85, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolGray),       1, 64, "wool_gray",       "灰色羊毛"},
    /* wool_light_gray */ {int(BlockRegistry::WoolLightGray),   86, 86, 86, 86, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolLightGray),  1, 64, "wool_light_gray", "浅灰色羊毛"},
    /* wool_cyan       */ {int(BlockRegistry::WoolCyan),        87, 87, 87, 87, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolCyan),       1, 64, "wool_cyan",       "青色羊毛"},
    /* wool_purple     */ {int(BlockRegistry::WoolPurple),      88, 88, 88, 88, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolPurple),     1, 64, "wool_purple",     "紫色羊毛"},
    /* wool_blue       */ {int(BlockRegistry::WoolBlue),        89, 89, 89, 89, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolBlue),       1, 64, "wool_blue",       "蓝色羊毛"},
    /* wool_brown      */ {int(BlockRegistry::WoolBrown),       90, 90, 90, 90, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolBrown),      1, 64, "wool_brown",      "棕色羊毛"},
    /* wool_green      */ {int(BlockRegistry::WoolGreen),       91, 91, 91, 91, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolGreen),      1, 64, "wool_green",      "绿色羊毛"},
    /* wool_red        */ {int(BlockRegistry::WoolRed),         92, 92, 92, 92, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolRed),        1, 64, "wool_red",        "红色羊毛"},
    /* wool_black      */ {int(BlockRegistry::WoolBlack),       93, 93, 93, 93, true, BlockRegistry::ShapeFull, 0.8f, int(BlockRegistry::Shears), 0, false, int(BlockRegistry::WoolBlack),      1, 64, "wool_black",      "黑色羊毛"},
    // ── t455 16 色床补齐 8 色新变体（既存 8 色床 id 32..39 不动；本段 white/light_blue/lime/pink/gray/light_gray/
    //   purple/brown）。机制等价 MC 1.0 床 16 色变体。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面
    //   路径，**非**异形，与既存床同族）、hardness=0.2、toolType=Axe（requiresTool=false 空手也掉落）、dropId=自身
    //   （破床掉同色床方块，可放回）、dropCount=1、maxStack=64。各面贴图=default_bed_<color>（tile 94..101；与同色
    //   羊毛同色板一致，原创自绘 §9a）。音色归 GroupWood（同既存床）。**配方**：3 同色羊毛 + 3 木板 → 该色床。
    //   睡觉机制（t388）经 isBed 谓词覆盖本段（同既存 8 色床）。进创造调色板（每色独立取用 + 右键放置）。
    /* bed_white       */ {int(BlockRegistry::BedWhite),      94, 94, 94, 94, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedWhite),      1, 64, "bed_white",      "白色床"},
    /* bed_light_blue  */ {int(BlockRegistry::BedLightBlue),  95, 95, 95, 95, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedLightBlue),  1, 64, "bed_light_blue", "浅蓝色床"},
    /* bed_lime        */ {int(BlockRegistry::BedLime),       96, 96, 96, 96, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedLime),       1, 64, "bed_lime",       "柠绿色床"},
    /* bed_pink        */ {int(BlockRegistry::BedPink),       97, 97, 97, 97, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedPink),       1, 64, "bed_pink",       "粉红色床"},
    /* bed_gray        */ {int(BlockRegistry::BedGray),       98, 98, 98, 98, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedGray),       1, 64, "bed_gray",       "灰色床"},
    /* bed_light_gray  */ {int(BlockRegistry::BedLightGray),  99, 99, 99, 99, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedLightGray),  1, 64, "bed_light_gray", "浅灰色床"},
    /* bed_purple      */ {int(BlockRegistry::BedPurple),    100,100,100,100, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedPurple),     1, 64, "bed_purple",     "紫色床"},
    /* bed_brown       */ {int(BlockRegistry::BedBrown),     101,101,101,101, false, BlockRegistry::ShapeBed, 0.2f, int(BlockRegistry::Axe), 0, false, int(BlockRegistry::BedBrown),      1, 64, "bed_brown",      "棕色床"},
    // ── t466 云杉木制品链（机制等价 MC 1.0 spruce 木制品；名称 / 贴图全原创自绘 §9a）。云杉原木 SpruceLog(46)
    //   已有（t395），本段是其延伸制品。复用既有木制品机制（solid/shape/碰撞/朝向解码/门双格），仅换 id + 贴图
    //   （tile = spruce_planks(102)，深色木纹区别橡木 planks(8)）。各面同贴图（同橡木木制品「一族共享一贴图」模式）。
    //   hardness=2.0（木质）、toolType=Axe + requiresTool=false（斧加速、空手可采且掉落）、dropId=自身（破块掉自身
    //   方块，可放回）、dropCount=1。mesher 经 PartialBlockGeometry::append 按 (id,state) 生成异形顶点（SpruceSlab/
    //   SpruceFence/SpruceDoor 与 WoodSlab/WoodFence/WoodDoor 同 case 几何，仅 tile 由 tileIndex 取本方块 sideTile
    //   =spruce_planks）。音色归 GroupWood（木质，同 planks 族）。配方：1 云杉原木 → 4 云杉木板（无序 2×2，同橡木）；
    //   云杉木板 → 台阶/栅栏/门（同橡木配方，原料换云杉木板）。进创造调色板（每件独立取用 + 右键放置）。
    /* spruce_planks */ {int(BlockRegistry::SprucePlanks),   102,102,102,102, true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::SprucePlanks),   1, 64, "spruce_planks", "云杉木板"}, // 整立方 opaque；state 复用 bit0 作双半砖合并 marker（DoubleSlabMarkerBit）
    /* spruce_slab   */ {int(BlockRegistry::SpruceSlab),     102,102,102,102, false, BlockRegistry::ShapeSlab,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::SpruceSlab),     1, 64, "spruce_slab",   "云杉台阶"}, // 半高（state bit0=上半(1)/下半(0)；与 WoodSlab 同编码）
    /* spruce_fence  */ {int(BlockRegistry::SpruceFence),    102,102,102,102, false, BlockRegistry::ShapeFence,    2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::SpruceFence),    1, 64, "spruce_fence",  "云杉栅栏"}, // 中心立柱 + 四向横档连邻居（与 WoodFence 同几何）；state=0
    /* spruce_door   */ {int(BlockRegistry::SpruceDoor),     102,102,102,102, false, BlockRegistry::ShapeDoor,     2.0f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::SpruceDoor),     1,  1, "spruce_door",   "云杉门"}, // 两格高；maxStack=1；state bit3=上格 bit2=开 bit[1:0]=朝向（与 WoodDoor 同编码）
    // ── t467 雪原浆果灌木丛（SweetBerryBush）：机制等价 MC 1.0 sweet berry bush（雪原 Snowy 群系散布的可采摘
    //   灌木）。**cross 形广告牌方块**（与 TallGrass / Sapling / 作物同走 PartialBlockGeometry 的 cross 几何段，两片
    //   对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。**3 生长阶段存 chunk state**（state = 阶段
    //   0..SweetBerryBushStageMax；0=无果嫩丛、1=小果、2=成熟可采摘）。solid=false（非实体 → 不挡邻居面剔除，同草丛 /
    //   作物）、shape=ShapeNone（**无碰撞** → 玩家穿过；机制等价 MC 浆果丛可踩过，stage>0 踩过受少量伤害归
    //   playercontroller 环境伤害 tick）、hardness=0（瞬破，同草丛 / 作物）、NoTool（空手可采且掉落）、dropId=0x233
    //   （甜浆果**物品**，材料段 RecipeRegistry::SweetBerryId；Core 不依赖 Game 故字面量 0x233 —— 破丛掉浆果物品非丛
    //   方块，机制等价 MC 破浆果丛掉浆果）、dropCount=1、maxStack=64。各面贴图=sweet_berry_bush_<state>（tile 103..105；
    //   本表 topTile/sideTile 存阶段 0 基底 tile 103，partialblockgeometry 的 SweetBerryBush case 内 state + 基底算实际
    //   阶段贴图，同 WheatCrop 模式）。音色归 GroupGrass（软植物音，同草丛 / 蘑菇）。worldgen placeSweetBerryBushes 在
    //   Snowy 群系 SnowLayer 地表上方低密度散布（机制等价 MC 寒冷群系浆果丛）。进创造调色板。
    /* sweet_berry_bush */ {int(BlockRegistry::SweetBerryBush), 103,103,103,103, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false,                           0x233, 1, 64, "sweet_berry_bush", "雪原浆果灌木丛"},
    // ── t468 冰的物理（机制等价 MC 1.0 packed ice / blue ice；名称 / 贴图全原创自绘 §9a）。Ice(45) 的更滑变种：
    //   滑动速度递增 Ice < PackIce < BlueIce（iceSlipApproach 越小越滑）。与 Ice 同属冰族（isIce 单一权威）——
    //   透明整立方（solid=false / ShapeFull —— 走 iceOnly 段 Blend 半透渲染，同 glass 契约；碰撞仍整格可踩）、
    //   hardness=0.5（同 Ice）、toolType=Pickaxe、requiresTool=true、minTier1（木镐可破）、**dropId=0**（破冰不掉落，
    //   机制等价 MC 冰需精准采集；本工程无精准采集故恒不掉落）、dropCount=0、maxStack=64。各面同贴图：PackIce=
    //   pack_ice(106)（实白底 + 细裂纹，比 Ice 更密实）/ BlueIce=blue_ice(107)（淡蓝底 + 纵向纹路，最滑）。音色归
    //   GroupStone（玻璃质，同 Ice）。worldgen 不直接生成（系统获得语义）；进创造调色板供测试 / 装饰。
    /* pack_ice     */ {int(BlockRegistry::PackIce),        106,106,106,106, false, BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::Pickaxe), 1, true,                             0, 0, 64, "pack_ice",     "浮冰"},
    /* blue_ice     */ {int(BlockRegistry::BlueIce),        107,107,107,107, false, BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::Pickaxe), 1, true,                             0, 0, 64, "blue_ice",     "蓝冰"},
    // ── t471 青金矿石（LapisOre）：机制等价 MC 1.0 青金石矿（嵌于 stone 深层、需石镐采掘、掉青金石物品）。
    //   整立方 opaque（solid=true / ShapeFull —— 走 mesher 整立方面路径，**非**异形，与 coal/iron/diamond/
    //   copper/gold 矿石同族）、hardness=3.0（同族量级，需镐）、toolType=Pickaxe、requiresTool=true、
    //   minToolTier=2（**需石镐**才掉落 —— 木镐挖了不掉落，机制等价 MC 1.0 青金矿需石镐；同 iron/copper 门槛）、
    //   dropId=0x236（青金石物品，材料段 RecipeRegistry::LapisId；Core 不依赖 Game 故字面量 0x236 —— 机制等价
    //   MC 1.0「青金矿采下直接掉青金石物品」，区别于铁/铜/金矿掉原矿须冶炼）、dropCount=1、maxStack=64。
    //   各面贴图=lapis_ore(108)（石头底 + 群青深蓝斑簇 + 黄铁矿金点，原创自绘 §9a；真实青金石矿物即「深蓝
    //   lazurite 底 + 黄铁矿 pyrite 金点」）。音色归 GroupStone（石质）。worldgen 高度分层散布于深层 y∈[5,31]
    //   （机制等价 MC 1.0 青金矿 Y<32 浅深层富集）。进创造调色板（与 coal/iron 矿石同走立方体图标）。
    /* lapis_ore    */ {int(BlockRegistry::LapisOre),        108,108,108,108, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 2, true,                           0x236, 1, 64, "lapis_ore",    "青金矿石"},
    // ── t474 附魔链两件方块（机制等价 MC 1.0 enchanting table / bookshelf；名称 / 贴图全原创自绘 §9a）：
    //   附魔台（EnchantingTable）：右键打开附魔 UI（3 选项槽，消耗 XP 等级 1/2/3 + 对应青金石 1/2/3）；
    //   周围书架数（≤15，2 格切比雪夫半径内）提升可选附魔等级上限。整立方 opaque（solid=true / ShapeFull
    //   —— 走 mesher 整立方面路径，**非**异形，与 crafting_table / furnace / chest 同族；MC 1.0 附魔台是
    //   异形低盒 + 顶上立书，本工程简化为整立方以复用既有渲染路径，机制等价非视觉对齐 §4）、
    //   hardness=5.0（同 MC 1.0 附魔台量级，石质偏硬）、toolType=Pickaxe、requiresTool=true、minTier1
    //   （木镐可破且掉落）、dropId=自身、dropCount=1、maxStack=64。各面贴图：顶=enchanting_table_top(109)
    //   （黑曜石深紫黑底 + 钻石青白菱斑 + 顶部立书轮廓）/ 底·侧·前=enchanting_table_side(110)（黑曜石
    //   深紫黑底 + 钻石嵌点 + 边缘暗化）。音色归 GroupStone（石质）。配方：1 书 + 2 钻石 + 4 黑曜石 → 1
    //   附魔台（工作台 3×3 有序）。进创造调色板。
    /* enchanting_table */ {int(BlockRegistry::EnchantingTable), 109, 110, 110, 110, true,  BlockRegistry::ShapeFull,   5.0f, int(BlockRegistry::Pickaxe), 1, true, int(BlockRegistry::EnchantingTable), 1, 64, "enchanting_table", "附魔台"},
    //   书架（Bookshelf）：纯装饰合成产物（机制等价 MC 1.0 bookshelf —— 仅作为附魔台加成来源；本工程无
    //   「书架可放书」物品栏，纯合成 / 放置方块）。整立方 opaque（solid=true / ShapeFull —— 走 mesher 整
    //   立方面路径，**非**异形，与 chest / wool 同族）、hardness=1.5（同 MC 1.0 书架量级，木质偏软）、
    //   toolType=Axe（木制；requiresTool=false → 空手也掉落，仅斧给速度加成）、dropId=自身（破书架掉书架
    //   方块，可放回 —— MC 1.0 破书架掉**书**物品，本工程掉书架方块以便玩家回收重放，区别于 MC 留记录）、
    //   dropCount=1、maxStack=64。各面贴图=bookshelf(111)（木板边框 + 中央书脊彩色书列）。音色归 GroupWood
    //   （木质）。配方：6 木板 + 3 书 → 1 书架（工作台 3×3 有序：上 / 下两行木板、中间一行 3 书）。进创造
    //   调色板（玩家可取用 / 放置；附魔台加成测试用）。
    /* bookshelf    */ {int(BlockRegistry::Bookshelf),       111,111,111,111, true,  BlockRegistry::ShapeFull,     1.5f, int(BlockRegistry::Axe),     0, false, int(BlockRegistry::Bookshelf),      1, 64, "bookshelf",    "书架"},
    // ── t477 铁块（IronBlock）：9 铁锭合成的金属存储方块（铁砧配方前置）。整立方 opaque（solid=true /
    //   ShapeFull，与 obsidian/wool 同族走整立方面路径）、hardness=5.0（金属偏硬）、Pickaxe、requiresTool=true、
    //   minTier1（木镐可破且掉落）、dropId=自身、dropCount=1、maxStack=64。各面=iron_block(112)（金属灰底+
    //   铆钉网格+高光）。音色归 GroupStone（金属质）。配方：9 铁锭 3×3 满铺 → 1 铁块。进创造调色板。
    /* iron_block   */ {int(BlockRegistry::IronBlock),       112,112,112,112, true,  BlockRegistry::ShapeFull,     5.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::IronBlock),       1, 64, "iron_block",   "铁块"},
    // ── t477 铁砧 3 损坏阶段（机制等价 MC 1.0 anvil：右键开铁砧 UI 修复/合并/重命名 + 自身损坏）。
    //   整立方 opaque（solid=true / ShapeFull，与附魔台同族走整立方面路径，**非**异形 —— MC 1.0 铁砧是异形
    //   低体+砧台，本工程简化为整立方，机制等价非视觉对齐 §4）、hardness=5.0（金属偏硬）、Pickaxe、
    //   requiresTool=true、minTier1（木镐可破且掉落）、dropId=自身（破任一阶段掉对应阶段铁砧，可放回）、
    //   dropCount=1、maxStack=64。顶面贴图按阶段递增裂纹（113 完好 / 115 微损 / 116 重损）；底·侧·前共享
    //   anvil_base(114)（深铁砧身）。音色归 GroupStone（金属质）。配方（仅完好 Anvil）：3 铁块顶行 + 4 铁锭
    //   中底行 → 1 完好铁砧。进创造调色板（仅 Anvil 完好；微损/重损由使用产生不进调色板）。
    /* anvil        */ {int(BlockRegistry::Anvil),           113,114,114,114, true,  BlockRegistry::ShapeFull,     5.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::Anvil),           1, 64, "anvil",        "铁砧"},
    /* anvil_chipped*/ {int(BlockRegistry::AnvilChipped),    115,114,114,114, true,  BlockRegistry::ShapeFull,     5.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::AnvilChipped),    1, 64, "anvil_chipped","微损铁砧"},
    /* anvil_damaged*/ {int(BlockRegistry::AnvilDamaged),    116,114,114,114, true,  BlockRegistry::ShapeFull,     5.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::AnvilDamaged),    1, 64, "anvil_damaged","重损铁砧"},
    // ── t482/t483 防御造物方块（机制等价 MC 1.0 雪傀儡 / 铁傀儡搭建材料；名称 / 贴图全原创自绘 §9a）。
    //   南瓜（Pumpkin）：造物头部方块（玩家放置南瓜 + 下方排列 → 触发造物生成）。整立方 opaque（solid=true /
    //   ShapeFull，与 chest/wool 同族走整立方面路径）、hardness=1.0（软质）、NoTool（空手可采且掉落）、
    //   dropId=自身、dropCount=1、maxStack=64。各面贴图：顶·底=pumpkin_top(119)/侧=pumpkin_side(117)/
    //   -Z 前面=pumpkin_face(118)（刻面双眼 + 锯齿嘴，机制等价 MC 刻面南瓜；作造物头时面朝玩家侧）。
    //   音色归 GroupGrass（软植物音）。进创造调色板（搭建用）。
    /* pumpkin      */ {int(BlockRegistry::Pumpkin),          119,119,117,118, true,  BlockRegistry::ShapeFull,     1.0f, int(BlockRegistry::NoTool),    0, false, int(BlockRegistry::Pumpkin),        1, 64, "pumpkin",     "南瓜"},
    //   雪块（Snow）：雪傀儡身体方块（南瓜 + 雪块×2 竖直搭建）。整立方 opaque（solid=true / ShapeFull，与
    //   SnowLayer 同族；区别 SnowLayer 是薄层、Snow 是实心满格 —— 雪傀儡机制等价 MC 用「雪块」满格而非薄雪层）、
    //   hardness=0.2（软质）、**toolType=Shovel + requiresTool=true + minTier=0**（t505：铲挖掉 4 雪球；空手挖不掉落
    //   —— 机制等价 MC 1.0 雪块铲挖掉 4 雪球、空手无掉落）、dropId=0x23D（雪球 SnowballId，材料段 t510）、
    //   **dropCount=4**（MC 1.0 雪块铲挖掉 4 雪球）、maxStack=64。各面贴图=snow(57)（冷白底+细密冰晶噪点，与
    //   SnowLayer 共享）。音色归 GroupSand（颗粒雪响，同 SnowLayer）。**4 雪球合 1 雪块**（recipe.cpp）。
    //   进创造调色板（搭建用；区别 SnowLayer 的薄层 —— 雪傀儡机制等价 MC 用「雪块」满格而非薄雪层）。
    /* snow         */ {int(BlockRegistry::Snow),             57, 57, 57, 57,  true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::Shovel),   0, true,                            0x23D, 4, 64, "snow",        "雪块"},
    // ── t484 废弃矿井结构方块（机制等价 MC 1.0 废弃矿井 mineshaft 的蛛网 / 铁轨；名称 / 贴图全原创自绘 §9a）。
    //   蜘蛛网（Cobweb）：cross 形蛛网（透明底 + 灰白蛛丝放射网纹，alphaCutoff cutout），与草丛 / 树苗同走 cross
    //   几何段。solid=false（不挡邻居面剔除）、ShapeNone（无碰撞，玩家穿过）、hardness=0（瞬破）、NoTool（空手可采）、
    //   dropId=0x219（线材料段 StringId；破蛛网掉线非蛛网方块，机制等价 MC 1.0 破蛛网掉线）、maxStack=64。
    //   各面=cobweb(120)。音色归 GroupGrass。worldgen placeMineshaft 散布；进创造调色板。
    /* cobweb       */ {int(BlockRegistry::Cobweb),          120,120,120,120, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),   0, false, 0x219,                                 1, 64, "cobweb",      "蜘蛛网"},
    //   铁轨（Rail）：贴地薄板 flat（透明底 + 棕色枕木 + 灰铁双轨，alphaCutoff cutout），mesher 走 PartialBlockGeometry
    //   的 Rail 水平 quad case（与睡莲横向浮叶同源）。solid=false、ShapeNone（无碰撞，玩家走过）、hardness=0（瞬破）、
    //   NoTool（空手可采且掉落）、dropId=自身（破铁轨掉铁轨方块，可放回）、maxStack=64。各面=rail(121)。
    //   音色归 GroupStone（金属质）。worldgen placeMineshaft 散布；进创造调色板。配方 6 铁锭 + 1 木棒 → 16 铁轨。
    /* rail         */ {int(BlockRegistry::Rail),            121,121,121,121, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),   0, false, int(BlockRegistry::Rail),           1, 64, "rail",        "铁轨"},
    // ── t485 沙漠神殿结构方块（机制等价 MC 1.0 沙漠神殿 desert temple 的 TNT / 切制砂岩；名称 / 贴图全原创自绘 §9a）。
    //   TNT（TntBlock）：可引爆爆炸物方块。整立方 opaque（solid=true / ShapeFull，与砂岩/箱子同走 culled 立方面
    //   路径）、hardness=0.0（MC 1.0 TNT 瞬破）、NoTool（空手可采且掉落）、dropId=自身（破 TNT 掉 TNT 方块）、
    //   dropCount=1、maxStack=64。各面=tnt(122)（深红药柱底+横向深棕捆带+中央亮黄标识+顶部引线点）。音色归
    //   GroupGrass（软质闷击，机制等价 MC 1.0 TNT 草地音色）。引爆：踩压力板触发（playercontroller 扫 footprint
    //   压力板下垫 TNT）→ EntityManager::detonateTntBlock（复用 destroySphereSilent 球形破坏 + 距离衰减伤玩家 +
    //   explosion 音/视，同 Stalker t284）。配方 5 火药 + 4 沙 → 1 TNT。进创造调色板。
    /* tnt          */ {int(BlockRegistry::TntBlock),         122,122,122,122, true,  BlockRegistry::ShapeFull,     0.0f, int(BlockRegistry::NoTool),   0, false, int(BlockRegistry::TntBlock),       1, 64, "tnt",         "TNT"},
    //   切制砂岩（CutSandstone）：装饰砂岩变体（机制等价 MC 1.0 cut sandstone——平滑+切割倒角边框，区别于普通
    //   砂岩层理纹）。整立方 opaque（solid=true / ShapeFull，与砂岩/石头同走 culled 立方面路径）、hardness=0.8
    //   （同砂岩量级，需镐）、Pickaxe、requiresTool=true、minTier1（木镐可破）、dropId=自身（破切制砂岩掉切制
    //   砂岩方块）、dropCount=1、maxStack=64。各面=cut_sandstone(123)（暖沙色平滑底+内陷矩形装饰边框）。音色归
    //   GroupStone（石质，同砂岩）。worldgen placeDesertTemple 金字塔外框装饰（与砂岩混排）。进创造调色板。
    /* cut_sandstone*/ {int(BlockRegistry::CutSandstone),     123,123,123,123, true,  BlockRegistry::ShapeFull,     0.8f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::CutSandstone),   1, 64, "cut_sandstone","切制砂岩"},
    // ── t486 丛林神殿结构方块（机制等价 MC 1.0 丛林神殿 jungle temple 的苔石 / 发射器；名称 / 贴图全原创自绘 §9a）。
    //   苔石（MossyCobble）：长满苔藓的圆石变体（机制等价 MC 1.0 mossy cobblestone——圆石上覆盖暗绿苔斑，潮湿
    //   阴暗环境的风化石材）。整立方 opaque（solid=true / ShapeFull，与圆石 / 砂岩同走 culled 立方面路径，**非**
    //   异形）、hardness=2.0（同圆石量级，需镐加速）、toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且
    //   掉落，同 Cobble）、dropId=自身（破苔石掉苔石方块，可放回）、dropCount=1、maxStack=64。
    //   各面贴图=mossy_cobble(124)（圆石灰底 + 散布暗绿苔藓斑簇，原创自绘 §9a；tools/build_mossy_cobble.py 程序
    //   生成）。音色归 GroupStone（石质，同 cobble 族）。worldgen placeJungleTemple 神殿主体（苔石建筑）。
    //   进创造调色板（玩家可取用 / 放置）。
    /* mossy_cobble */ {int(BlockRegistry::MossyCobble),       124,124,124,124, true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::MossyCobble),    1, 64, "mossy_cobble","苔石"},
    //   发射器（Dispenser）：受触发时朝所朝方向发射箭矢弹丸的机关方块（机制等价 MC 1.0 发射器 dispenser——无红石
    //   系统，故用「踩压力板 → 邻接发射器射箭」直接触发，spec「无红石用 dispenser 方块直接触发」）。整立方 opaque
    //   （solid=true / ShapeFull，与熔炉 / 箱子同走 culled 立方面路径，**非**异形）、hardness=3.5（同熔炉量级，石质
    //   偏硬）、toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且掉落，同熔炉）、
    //   dropId=自身（破发射器掉发射器方块，可放回）、dropCount=1、maxStack=64。各面贴图：顶/底=dispenser_top(125)
    //   （石质灰底 + 中央圆形排出口俯视环纹）/ 侧=dispenser_side(126)（石质灰底 + 边框暗带）/ 前面=dispenser_front(127)
    //   （石质灰底 + 中央暗腔排出口，所朝方向，mesher 据 state 选，同熔炉 tileFor 分支）。state bit[1:0]=朝向
    //   0=+X 1=-X 2=+Z 3=-Z（同 chest / furnace horizontalFacing）。音色归 GroupStone（石质）。触发：
    //   PlayerController::scanDispenserTraps 扫玩家 footprint——压力板的 4 水平邻格之一 == Dispenser →
    //   EntityManager::spawnArrow 朝压力板方向射箭（复用既有 Arrow 弹丸 tick，机制等价 MC 发射器射箭）。per-dispenser
    //   冷却防刷屏。worldgen placeJungleTemple 把发射器嵌入走廊石壁。进创造调色板。
    /* dispenser    */ {int(BlockRegistry::Dispenser),        125,125,126,127, true,  BlockRegistry::ShapeFull,     3.5f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::Dispenser),      1, 64, "dispenser",   "发射器"},
    // ── t487 要塞结构方块（机制等价 MC 1.0 要塞 stronghold 的石砖 / 石砖台阶·楼梯 / 末地传送门；名称 / 贴图全
    //   原创自绘 §9a；§9 区隔：末地/末影之眼为通用描述词，机制对齐非专名照搬）：
    //   石砖（StoneBrick）：石质整立方装饰方块（要塞墙体主体）。整立方 opaque（solid=true / ShapeFull ——
    //   走 mesher 整立方面路径，**非**异形，与 stone/cobble/mossy 同族）、hardness=1.5（同 stone 量级，需镐）、
    //   toolType=Pickaxe、requiresTool=true、minTier1（木镐可破且掉落）、dropId=自身（破石砖掉石砖方块，可放回）、
    //   dropCount=1、maxStack=64。各面贴图=stone_brick(128)（石质灰底 + 砖块缝纹网格）。音色归 GroupStone。
    //   worldgen placeStronghold 要塞墙体 / 走廊 / 房间围栏主体。进创造调色板。
    /* stone_brick  */ {int(BlockRegistry::StoneBrick),       128,128,128,128, true,  BlockRegistry::ShapeFull,     1.5f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::StoneBrick),     1, 64, "stone_brick",  "石砖"},
    //   石砖台阶（StoneBrickSlab）：半高（上/下半）。复用既有异形方块系统（ShapeSlab + PartialBlockGeometry
    //   几何），仅换石砖贴图（tile 128）+ 石质属性。solid=false（非整立方 → 不挡邻居面剔除，同木/圆石半砖）、
    //   hardness=1.5、Pickaxe、requiresTool=true、minTier1、dropId=自身、dropCount=1、maxStack=64。
    //   state bit0=上半(1)/下半(0)（与 WoodSlab / CobbleSlab 同编码）。经 isSlab 谓词并入异形路由（段外）。
    /* stone_brick_slab */ {int(BlockRegistry::StoneBrickSlab),   128,128,128,128, false, BlockRegistry::ShapeSlab,   1.5f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::StoneBrickSlab), 1, 64, "stone_brick_slab", "石砖台阶"},
    //   石砖楼梯（StoneBrickStairs）：整步 + 背墙。复用既有异形方块系统（ShapeStairs + PartialBlockGeometry
    //   几何），仅换石砖贴图（tile 128）+ 石质属性。solid=false、hardness=1.5、Pickaxe、requiresTool=true、
    //   minTier1、dropId=自身、dropCount=1、maxStack=64。state[1:0]=朝向 bit2=倒置（与 WoodStairs / CobbleStairs
    //   同编码）。经 isStairs 谓词并入异形路由（段外）。
    /* stone_brick_stairs */ {int(BlockRegistry::StoneBrickStairs), 128,128,128,128, false, BlockRegistry::ShapeStairs, 1.5f, int(BlockRegistry::Pickaxe), 1, true, int(BlockRegistry::StoneBrickStairs), 1, 64, "stone_brick_stairs", "石砖楼梯"},
    //   末地传送门（EndPortal）：要塞传送门房中央的传送门方块（机制等价 MC 1.0 end portal；§9 区隔：末地为
    //   通用描述词）。**整立方不透明**——简化为满格整立方（机制等价 MC 末地传送门「传送门平面」外观，本工程
    //   不做异形框架；激活后由 Main.qml 伪光源 + state 切换显星空黑洞视觉）。solid=false（非实体 → 不挡邻居
    //   面剔除，与地形解耦；机制等价 MC 末地传送门无碰撞可走过）/ ShapeFull（碰撞/选中仍走整格可踩/可瞄准）、
    //   **hardness=-1.0**（不可挖掘：canMine=false，任何模式/工具不破，防创造秒破传送门；同 bedrock/Water 哨兵
    //   语义）、dropId=0 不掉落、dropCount=0、maxStack=64（worldgen 专属 / 不掉落 → maxStack 实不可达，填 64
    //   与方块族一致）。各面贴图=end_portal(129)（深紫黑星空底 + 中心亮绿旋涡 + 散布星点）。音色归 GroupStone
    //   （石质兜底）。激活：玩家持末影之眼物品右键传送门 → placeBlock useBlock 分支翻 state bit0（激活态）+
    //   qInfo 日志（末地预热占位，不实现末地维度）。mesher 据 state bit0 切 end_portal(129)/end_portal_active(130)。
    //   不进创造调色板（worldgen 专属；玩家经末影之眼激活交互）。
    /* end_portal   */ {int(BlockRegistry::EndPortal),         129,129,129,129, false, BlockRegistry::ShapeFull,    -1.0f, int(BlockRegistry::NoTool),  0, false,                            0, 0, 64, "end_portal",   "末地传送门"},
    // t490 手动 TNT 点火机关方块（机制等价 MC 1.0 lever / wooden button / stone button；无红石故右键激活即点燃邻接
    //   TNT）。三者复用 ShapePlate（贴地薄板，同 WoodPressurePlate 几何）。PartialBlockGeometry 据 state bit0
    //   （激活态）切亮色高光（非 MC 资产，tools/build_lever_button.py 程序生成）。激活：placeBlock useBlock 分支
    //   检测命中机关 → 翻 state bit0 + 点燃水平四邻 TNT。
    /* lever        */ {int(BlockRegistry::Lever),           131,131,131,131, false, BlockRegistry::ShapePlate,    0.5f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::Lever),       1, 64, "lever",        "杠杆"},
    /* wood_button  */ {int(BlockRegistry::WoodButton),      132,132,132,132, false, BlockRegistry::ShapePlate,    0.5f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::WoodButton),  1, 64, "wood_button",  "木按钮"},
    /* stone_button */ {int(BlockRegistry::StoneButton),     133,133,133,133, false, BlockRegistry::ShapePlate,    0.5f, int(BlockRegistry::Pickaxe), 1, true,  int(BlockRegistry::StoneButton), 1, 64, "stone_button", "石按钮"},
    // ── t507 白蘑菇 / 棕蘑菇（BrownMushroom）：机制等价 MC 1.0 brown mushroom（沼泽 / 阴暗草地小蘑菇，与红蘑菇
    //   Mushroom=48 同族，仅配色区别 —— 棕色菌盖 + 米色菌柄）。cross 形广告牌方块（与 Mushroom / Sapling / DeadBush
    //   同走 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout）—— 非 1×1×1 整立方。solid=false（非实体 →
    //   不挡邻居面剔除，同草丛 / 红蘑菇）、shape=ShapeNone（**无碰撞** → 玩家穿过）、hardness=0（瞬破，同红蘑菇 /
    //   草丛）、NoTool（空手可采且掉落）、dropId=自身（破白蘑菇掉白蘑菇方块，可放回）、dropCount=1、maxStack=64。
    //   各面贴图=brown_mushroom(135)（透明底 + 米色菌柄 + 棕色菌盖白斑，alphaCutoff cutout）。音色归 GroupGrass
    //   （软植物音，同红蘑菇 / 草丛）。worldgen 在沼泽草地散布（同红蘑菇，与 placeSwampFlora 共址）；进创造调色板。
    //   **蘑菇汤配方原料**（recipe.cpp：碗 + 红蘑菇 + 白蘑菇 → 1 蘑菇汤）。§9 区隔：仅机制对齐 MC 1.0 brown mushroom，
    //   名称 / 贴图全原创自绘（§9a，tools/build_brown_mushroom.py）。
    /* brown_mushroom */ {int(BlockRegistry::BrownMushroom),  135,135,135,135, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, false, int(BlockRegistry::BrownMushroom), 1, 64, "brown_mushroom","白蘑菇"},
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
    /* ladder                  */ 65, // t413 木梯 → MC 1.0 ladder id 65
    // t455 16 色 wool 其余 15 色变体 → MC 1.0 wool id 35（统一；MC 1.0 羊毛颜色由 metadata 分，本工程用独立 id）。
    /* wool_orange             */ 35, /* wool_magenta            */ 35, /* wool_light_blue         */ 35,
    /* wool_yellow             */ 35, /* wool_lime               */ 35, /* wool_pink               */ 35,
    /* wool_gray               */ 35, /* wool_light_gray         */ 35, /* wool_cyan               */ 35,
    /* wool_purple             */ 35, /* wool_blue               */ 35, /* wool_brown              */ 35,
    /* wool_green              */ 35, /* wool_red                */ 35, /* wool_black              */ 35,
    // t455 16 色床补齐 8 色新变体 → MC 1.0 床 id 26（统一；同既存 8 色床，颜色由 metadata 分）。
    /* bed_white               */ 26, /* bed_light_blue          */ 26, /* bed_lime                */ 26, /* bed_pink */ 26,
    /* bed_gray                */ 26, /* bed_light_gray          */ 26, /* bed_purple              */ 26, /* bed_brown */ 26,
    // t466 云杉木制品链 → MC 1.0 无独立 id（1.0 仅橡木 planks id 5 / 木门 id 64 / 木栅栏 id 85 / 木台阶仅石 44；
    //   云杉变种 1.7+ 才以 metadata 分；本工程用独立 id 故无 1.0 等价，同 spruce_log=-1 模式）。
    /* spruce_planks           */ -1, // t466 云杉木板 → MC 1.0 planks id 5（仅橡木，云杉 1.7+ metadata 分，独立 id 故无 1.0 等价）
    /* spruce_slab             */ -1, // t466 云杉台阶 → MC 1.0 无等价（1.0 仅石台阶 id 44，木台阶 1.3+；云杉更晚）
    /* spruce_fence            */ -1, // t466 云杉栅栏 → MC 1.0 无等价（1.0 仅橡木栅栏 id 85，云杉栅栏 1.7+ metadata 分）
    /* spruce_door             */ -1, // t466 云杉门 → MC 1.0 无等价（1.0 仅橡木门 id 64，云杉门 1.8+ 独立 id）
    /* sweet_berry_bush        */ -1, // t467 雪原浆果灌木丛 → MC 1.0 无等价（sweet berry bush id 105 为 1.14+；本工程作 cross 装饰灌木故无 1.0 等价）
    /* pack_ice                */ -1, // t468 浮冰 → MC 1.0 无等价（packed_ice id 174 为 1.8+；1.0 仅 ice id 79；独立 id 故无 1.0 等价）
    /* blue_ice                */ -1, // t468 蓝冰 → MC 1.0 无等价（blue_ice id 211 为 1.13+；1.0 仅 ice id 79；独立 id 故无 1.0 等价）
    /* lapis_ore               */ 21, // t471 青金矿石 → MC 1.0 lapis lazuli ore id 21
    /* enchanting_table        */ 116, // t474 附魔台 → MC 1.0 enchanting table id 116
    /* bookshelf               */ 47,  // t474 书架 → MC 1.0 bookshelf id 47
    // t477 铁块 / 铁砧 → MC 1.0 对齐：iron block id 42（1.0 存在）；anvil id 145 为 1.4+（1.0 无铁砧）→ -1（资源包回退引擎自绘）。
    /* iron_block              */ 42,  // t477 铁块 → MC 1.0 iron block id 42
    /* anvil                   */ -1, // t477 铁砧 → MC 1.0 无等价（anvil id 145 为 1.4+；本工程独立 id 故无 1.0 等价）
    /* anvil_chipped           */ -1, // t477 微损铁砧 → MC 1.0 无等价（同 anvil，1.4+ 才以 metadata 分损坏态）
    /* anvil_damaged           */ -1, // t477 重损铁砧 → MC 1.0 无等价（同 anvil）
    // t482/t483 造物方块 → MC 1.0 对齐：pumpkin id 86（1.0 存在）；snow block id 80（同 snow_layer 取雪方块 80）。
    /* pumpkin                 */ 86, // t482 南瓜 → MC 1.0 pumpkin id 86
    /* snow                    */ 80, // t482 雪块 → MC 1.0 snow block id 80（同 SnowLayer 的 80；满格雪方块）
    // t484 废弃矿井结构方块 → MC 1.0 对齐：cobweb id 30（1.0 存在）；rail id 66（1.0 存在）。
    /* cobweb                  */ 30, // t484 蜘蛛网 → MC 1.0 cobweb id 30
    /* rail                    */ 66, // t484 铁轨 → MC 1.0 rail id 66
    // t485 沙漠神殿结构方块 → MC 1.0 对齐：TNT id 46（1.0 存在）；切制砂岩 cut sandstone id 43 为 1.8+ 独立 id
    //   （1.0 sandstone id 24 仅以 metadata 分变体，本工程用独立 id 故 cut_sandstone 无 1.0 等价 → -1，资源包回退引擎自绘）。
    /* tnt                     */ 46, // t485 TNT → MC 1.0 TNT id 46
    /* cut_sandstone           */ -1, // t485 切制砂岩 → MC 1.0 无等价（cut sandstone id 43 为 1.8+；1.0 sandstone id 24 仅 metadata 分变体，独立 id 故无 1.0 等价）
    // t486 丛林神殿结构方块 → MC 1.0 对齐：mossy cobblestone 为 cobble id 4 的 metadata 变体（data 2，1.0 无独立 id，
    //   独立 id 故取 cobble id 4 近似）；dispenser id 23（1.0 存在）。
    /* mossy_cobble            */ 4,  // t486 苔石 → MC 1.0 cobble id 4（mossy 为 data 2 变体，独立 id 故取 cobble 近似）
    /* dispenser               */ 23, // t486 发射器 → MC 1.0 dispenser id 23
    // t487 要塞结构方块 → MC 1.0 对齐：stone brick id 98（1.0 存在）；stone brick slab/stairs 为 stone slab id 44 /
    //   stairs id 67（1.0 仅以 metadata 分变体，独立 id 故取近似）；end portal frame id 120 / end portal id 119
    //   （1.0 末地传送门相关）—— 本工程 EndPortal 简化为整立方传送门方块，取 end portal id 119。
    /* stone_brick             */ 98,  // t487 石砖 → MC 1.0 stone brick id 98
    /* stone_brick_slab        */ 44,  // t487 石砖台阶 → MC 1.0 stone slab id 44（metadata 5 = stone brick；统一取 slab id）
    /* stone_brick_stairs      */ 67,  // t487 石砖楼梯 → MC 1.0 stairs id 67（1.0 楼梯含木/石/cobble/brick 统一 id）
    /* end_portal              */ 119, // t487 末地传送门 → MC 1.0 end portal id 119
    // t490 手动 TNT 点火机关 → MC 1.0 对齐：lever id 69（1.0 存在）；stone button id 77（1.0 存在）；
    //   wooden button id 143 为 1.5+ 独立 id（1.0 仅石按钮，木按钮 1.5+）→ -1（资源包回退引擎自绘）。
    /* lever                   */ 69,  // t490 杠杆 → MC 1.0 lever id 69
    /* wood_button             */ -1,  // t490 木按钮 → MC 1.0 无等价（wooden button id 143 为 1.5+；1.0 仅石按钮）
    /* stone_button            */ 77,  // t490 石按钮 → MC 1.0 stone button id 77
    // t507 白蘑菇 / 棕蘑菇 → MC 1.0 brown mushroom 仅以 item（id 39）或巨型菌盖方块（id 99）存在，无「小蘑菇植物
    //   方块」等价；本工程作 cross 装饰方块故无 1.0 等价（同 red Mushroom=48 取 -1 模式）。
    /* brown_mushroom          */ -1, // t507 白蘑菇 → MC 1.0 无等价（同 red mushroom；本工程作 cross 装饰故无 1.0 等价）
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
    if (blockId == SpruceSlab || blockId == SpruceFence || blockId == SpruceDoor) return true; // t466 段外云杉木制品（与 WoodSlab/WoodFence/WoodDoor 同几何）
    if (blockId == StoneBrickSlab || blockId == StoneBrickStairs) return true; // t487 段外石砖台阶/楼梯（与 WoodSlab/WoodStairs 同几何）
    if (blockId == Lever || blockId == WoodButton || blockId == StoneButton) return true; // t490 段外手动点火机关（与 WoodPressurePlate 同几何：贴地薄板）
    return blockId >= FirstPartial && blockId <= LastPartial;
}
bool BlockRegistry::isSlab(quint8 blockId)           { return blockId == WoodSlab || blockId == CobbleSlab || blockId == SpruceSlab || blockId == StoneBrickSlab; }
bool BlockRegistry::isStairs(quint8 blockId)         { return blockId == WoodStairs || blockId == CobbleStairs || blockId == StoneBrickStairs; }
bool BlockRegistry::isFence(quint8 blockId)          { return blockId == WoodFence || blockId == CobbleFence || blockId == SpruceFence; }
bool BlockRegistry::isPressurePlate(quint8 blockId)  { return blockId == WoodPressurePlate || blockId == CobblePressurePlate; }

// t466 门方块统一谓词（单一权威，段外云杉门并入）：blockId == WoodDoor（连续段内）或 SpruceDoor（段外）即门。
//   供 playercontroller 的门放置 / 右键开合 / 破坏联动统一读，避免各处硬编码 WoodDoor id 判定（同 isFence
//   把段外圆石墙 / 云杉栅栏并入的模式）。单 id 故裸相等判定即可，仍提供谓词作单一权威。
bool BlockRegistry::isDoor(quint8 blockId)
{
    return blockId == WoodDoor || blockId == SpruceDoor;
}

// t412/t466 双半砖合并映射（木 / 石 / 云杉三族共用一套合并与掉落逻辑）：半砖 → 其满格整立方（合并写入目标）；
//   满格整立方 → 其半砖（破坏掉落）。非半砖 / 非双砖源 → Air / 0（兜底）。
quint8 BlockRegistry::slabFullBlock(quint8 slabId)
{
    if (slabId == WoodSlab)       return Planks;
    if (slabId == CobbleSlab)     return Cobble;
    if (slabId == SpruceSlab)     return SprucePlanks;
    if (slabId == StoneBrickSlab) return StoneBrick; // t487 石砖双半砖合并 → 石砖满格
    return Air;
}
quint8 BlockRegistry::fullBlockSlabDrop(quint8 fullId)
{
    if (fullId == Planks)           return WoodSlab;
    if (fullId == Cobble)           return CobbleSlab;
    if (fullId == SprucePlanks)     return SpruceSlab;
    if (fullId == StoneBrick)       return StoneBrickSlab; // t487 石砖满格双砖源 → 破块掉 2 石砖台阶
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
    if (blockId == BrownMushroom) return true; // t507 段外 cross（白蘑菇 / 棕蘑菇，同 Mushroom 模式）
    if (blockId == LilyPad) return true;  // t396 cross 路由的横向浮叶（几何水平非竖直 cross，但同走 PASS 1 alphaCutoff 路径，见头注释）
    if (blockId == Sugarcane) return true; // t397 段外 cross（甘蔗细茎，同 Sapling 模式）
    if (blockId == CarrotCrop) return true; // t407 段外 cross（胡萝卜作物，同小麦作物按 state 选阶段贴图）
    if (blockId == PotatoCrop) return true; // t407 段外 cross（马铃薯作物，同小麦作物按 state 选阶段贴图）
    if (blockId == Ladder) return true; // t413/t501 段外 cross（木梯贴墙竖直爬行梯；t501 改单片贴墙 quad 据 state 摆位，同走 PASS 1 alphaCutoff 路径）
    if (blockId == SweetBerryBush) return true; // t467 段外 cross（雪原浆果灌木丛，两片对角相交双面 quad 贴 stage 贴图）
    if (blockId == Cobweb) return true; // t484 段外 cross（蜘蛛网，两片对角相交双面 quad 贴蛛网贴图；矿井散布）
    if (blockId == Rail) return true;   // t484 cross 路由的贴地薄板（几何水平 quad 非竖直 cross，但同走 PASS 1 alphaCutoff 路径，见头注释；与睡莲同族）
    if (blockId >= FirstFlower && blockId <= LastFlower) return true; // t397 段外花段（4 色 cross）
    return blockId >= FirstCross && blockId <= LastCross;
}

// t387/t455 床方块统一谓词（单一权威）：id ∈ 既存 8 色段 [FirstBed, LastBed]（red..black）**或** t455 新增 8 色
//   段 [FirstExtraBed, LastExtraBed]（white/light_blue/lime/pink/gray/light_gray/purple/brown）即床。供 t388 睡觉
//   机制判定「命中格是否床」（右键床 → 跳清晨 + 重生点），避免各处自写 id 区间漂移（同 isCrossBillboard 段外
//   模式）。两段不连续（既存 8 色 32..39，新 8 色 78..85），故两段并判；改段时一处同步谓词即可。
bool BlockRegistry::isBed(quint8 blockId)
{
    return (blockId >= FirstBed && blockId <= LastBed)
        || (blockId >= FirstExtraBed && blockId <= LastExtraBed);
}

// t455 16 色 wool 统一谓词（单一权威）：white（Wool=27）**或** 15 色变体段 [FirstWoolVariant, LastWoolVariant]
//   （WoolOrange=63..WoolBlack=77）即羊毛。供未来染料 / 配方 / 渲染判定「是否羊毛方块」，避免各处自写 id 判定
//   漂移（同 isBed 模式）。white 与变体段不连续故两段并判。
bool BlockRegistry::isWool(quint8 blockId)
{
    return blockId == Wool
        || (blockId >= FirstWoolVariant && blockId <= LastWoolVariant);
}

// t428 床配对格偏移（state 解码 → 配对另一半相对本格的水平 (dx,dz)）。bit[1:0]=head→foot 方向 0=+X 1=-X 2=+Z 3=-Z
//   （值域与 horizontalFacing / chestFrontFace 同源；语义 = head 半指向 foot 半的轴向）；bit3=head(1)/foot(0)。
//   t496：玩家放置时 head 落 foot 的「玩家朝向同向」邻格（bedFacing = horizontalFacing ^ 1 = head→foot 方向）。
//   本格为 foot(bit3=0) 时配对(head)在 -front（= 玩家前向，远离玩家）；本格为 head(bit3=1) 时配对(foot)在 +front。
//   y 同层（dy=0）。纯 state 解码（单一权威，同 chestFrontFace 模式）；playercontroller 放置 / 破坏联动读此，
//   不各处自写朝向。
void BlockRegistry::bedPartnerOffset(quint8 state, int &dx, int &dz)
{
    static constexpr int kFrontX[4] = { 1, -1, 0, 0 };
    static constexpr int kFrontZ[4] = { 0, 0, 1, -1 };
    const int f = state & 3;
    const int sgn = (state & 8) ? +1 : -1; // head → +front 找 foot；foot → -front 找 head
    dx = sgn * kFrontX[f];
    dz = sgn * kFrontZ[f];
}

// t397 花方块段统一谓词（单一权威）：id ∈ [FirstFlower, LastFlower]（4 色变体）即花。供 worldgen placeFlowers
//   / 放置预检 / 未来花相关机制判定「是否花」，避免各处自写 id 区间漂移（同 isBed / isCrossBillboard 模式）。
//   连续段，裸区间即可。
bool BlockRegistry::isFlower(quint8 blockId)
{
    return blockId >= FirstFlower && blockId <= LastFlower;
}

// t507 蘑菇统一谓词（单一权威，见 blockregistry.h 头注释）：blockId == Mushroom（红，=48）或
//   BrownMushroom（白 / 棕，=115）即蘑菇。两 id 不连续（48 夹中间、115 段末）故显式并判
//   （同 isIce / isBed 段不连续并判模式）。供失撑掉落 / 放置预检 / 蘑菇汤配方判定「蘑菇族」语义统一读，
//   避免各处硬编码 2 个 id 判定漂移。
bool BlockRegistry::isMushroom(quint8 blockId)
{
    return blockId == Mushroom || blockId == BrownMushroom;
}

// t413 垂直爬梯统一谓词（单一权威）：blockId == Ladder 即梯。供 PlayerController 爬升物理 + mesher cross 路由分流
//   （已并入 isCrossBillboard；本谓词专供爬升逻辑读「是否梯」，避免把「cross 渲染」与「可爬」语义耦合——
//   未来若有不可爬的 cross 方块，爬升仍只读本谓词不误判）。单 id 故裸相等判定。
bool BlockRegistry::isLadder(quint8 blockId)
{
    return blockId == Ladder;
}

// t485 TNT 统一谓词（单一权威）：blockId == TntBlock 即 TNT。供 PlayerController TNT 陷阱触发判定
//   （扫玩家 footprint 格——压力板下垫 TNT 即引爆）+ EntityManager::detonateTntBlock + worldgen
//   placeDesertTemple，避免各处硬编码 TntBlock id 判定（同 isLadder 单 id 模式）。
bool BlockRegistry::isTnt(quint8 blockId)
{
    return blockId == TntBlock;
}

// t486 发射器统一谓词（单一权威）：blockId == Dispenser 即发射器。供 PlayerController 发射器陷阱触发判定
//   （扫玩家 footprint 格——压力板的 4 水平邻格之一 == Dispenser 即触发）+ worldgen placeJungleTemple 写入，
//   避免各处硬编码 Dispenser id 判定（同 isTnt / isLadder 单 id 模式）。单 id 故裸相等判定。
bool BlockRegistry::isDispenser(quint8 blockId)
{
    return blockId == Dispenser;
}

// t490 手动 TNT 点火机关统一谓词（单一权威；见 blockregistry.h 头注释）。Lever / WoodButton / StoneButton 三者
//   机制等价 MC 1.0 lever / button——右键激活即点燃水平四邻 TNT。各单 id 裸相等判定；isManualIgniter 聚合三者
//   供 placeBlock useBlock 分支统一判定「右键命中的机关方块 → 点燃邻接 TNT」。
bool BlockRegistry::isLever(quint8 blockId)       { return blockId == Lever; }
bool BlockRegistry::isWoodButton(quint8 blockId)  { return blockId == WoodButton; }
bool BlockRegistry::isStoneButton(quint8 blockId) { return blockId == StoneButton; }
bool BlockRegistry::isManualIgniter(quint8 blockId)
{
    return blockId == Lever || blockId == WoodButton || blockId == StoneButton;
}

// t487 末地传送门统一谓词（单一权威）：blockId == EndPortal 即末地传送门。供 PlayerController placeBlock
//   useBlock 分支判定「右键命中格是否末地传送门 → 持末影之眼激活」（避免各处硬编码 id 判定漂移，同 isLadder
//   单 id 模式）。单 id 故裸相等判定，仍提供谓词作单一权威（未来追加变体时一处同步）。
bool BlockRegistry::isEndPortal(quint8 blockId)
{
    return blockId == EndPortal;
}

// t474 书架统一谓词（单一权威）：blockId == Bookshelf 即书架。供 World::countBookshelvesAround（附魔台加成
//   计算：扫切比雪夫半径 2 内书架数 ≤15）判定「是否书架」，避免各处硬编码 Bookshelf id 判定漂移（同 isLadder
//   单 id 模式）。单 id 故裸相等判定，仍提供谓词作单一权威（未来追加书架变体时一处同步）。
bool BlockRegistry::isBookshelf(quint8 blockId)
{
    return blockId == Bookshelf;
}

// t474 附魔台统一谓词（单一权威）：blockId == EnchantingTable 即附魔台。供 playercontroller placeBlock
//   useBlock 分支判定「右键命中格是否附魔台 → 开 EnchantingTableUI」（避免各处硬编码 id 判定漂移，同
//   isLadder 模式）。单 id 故裸相等判定，仍提供谓词作单一权威（未来变体时一处同步）。
bool BlockRegistry::isEnchantingTable(quint8 blockId)
{
    return blockId == EnchantingTable;
}

// t477 铁砧统一谓词（单一权威，覆盖 3 损坏阶段 Anvil/AnvilChipped/AnvilDamaged）：供 playercontroller
//   useBlock 判定「右键命中格是否铁砧 → 开 AnvilUI」+ 破块掉落判定，避免各处硬编码 3 个 id 判定漂移
//   （同 isIce 三阶段并判模式）。
bool BlockRegistry::isAnvil(quint8 blockId)
{
    return blockId == Anvil || blockId == AnvilChipped || blockId == AnvilDamaged;
}

// t477 铁砧损坏阶段（0=完好 / 1=微损 / 2=重损）；非铁砧 → 0。
int BlockRegistry::anvilDamageStage(quint8 blockId)
{
    if (blockId == AnvilChipped) return 1;
    if (blockId == AnvilDamaged) return 2;
    return 0; // Anvil（完好）或非铁砧
}

// t477 铁砧损坏 +1 后的目标方块 id：完好→微损 / 微损→重损 / 重损→Air（碎裂移除）。
//   playercontroller damageAnvil 据本单一权威推进阶段（不各处自写 id 推进）。非铁砧 → 原 id（不变）。
quint8 BlockRegistry::anvilNextStage(quint8 blockId)
{
    if (blockId == Anvil)        return quint8(AnvilChipped);
    if (blockId == AnvilChipped) return quint8(AnvilDamaged);
    if (blockId == AnvilDamaged) return quint8(Air); // 重损再损 → 碎裂移除
    return blockId; // 非铁砧 → 不变
}

// t468 冰族统一谓词（单一权威）：Ice(45) / PackIce(91) / BlueIce(92) 即冰。三 id 不连续（Ice 夹中间）故显式并判。
//   供 PlayerController 冰滑行 + ItemEntityManager 物品冰摩擦 + mesher iceOnly 段路由 + t469 船冰面加速统一读，
//   避免各处自写三 id 判定漂移（同 isBed / isWool 段不连续并判模式）。
bool BlockRegistry::isIce(quint8 blockId)
{
    return blockId == Ice || blockId == PackIce || blockId == BlueIce;
}

// t468 冰面「滑动接近率」（1/s；越小越滑）—— 玩家水平速度向目标速度的指数接近速率（PlayerController::step
//   冰分支用 1 - exp(-rate*dt) 做 lerp）。机制等价 MC 1.0 ice < packed_ice < blue_ice 滑度递增：Ice 中等滑 /
//   PackIce 更滑 / BlueIce 最滑。非冰 → 0（caller 据 0 走常规地面瞬时设速路径，不进冰滑行分支）。单一权威：
//   玩家与未来船的冰面手感都读它，避免两处魔数漂移。
float BlockRegistry::iceSlipApproach(quint8 blockId)
{
    if (blockId == Ice)     return 8.0f;  // 冰：中等滑（接近率 8/s；松键后 ~0.4s 明显滑行）
    if (blockId == PackIce) return 4.5f;  // 浮冰：更滑（接近率 4.5/s；松键后滑得更远）
    if (blockId == BlueIce) return 2.8f;  // 蓝冰：最滑（接近率 2.8/s；松键后滑得最远）
    return 0.0f;                          // 非冰（caller 走常规地面路径）
}

// t505 积雪层 state → 薄板高度（cell-local [0,1]；state 0..7 → 1/8..1.0；机制等价 MC 1.0 snow layer 8 层）。
//   state 越界 clamp 到 [0, SnowLayerStageMax=7] 兜底（防异常 / 旧存档脏 state）。单一权威：mesher
//   （PartialBlockGeometry SnowLayer case 薄板高度）+ collisionAABBs / selectionAABBs / raycastAABBs /
//   solidTopOffset / worldgen / playercontroller 雪层掉雪球（state+1）统一读本函数，避免各处自写 (state+1)/8
//   漂移。返回 (clamp(state,0,7)+1)/8.0f → state 0=1/8(0.125) .. state 7=8/8(1.0 满格)。
float BlockRegistry::snowLayerHeight(quint8 state)
{
    const quint8 s = (state > SnowLayerStageMax) ? SnowLayerStageMax : state;
    return float(int(s) + 1) / 8.0f;
}

// 方块是否「有碰撞 sub-AABB」（考虑开合态）。air / torch / water（ShapeNone）→ false。
//   越界 → false（air 兜底）。单一权威：isCollidable 与 collisionAABBs 共用，保证「预判」与「精确碰撞」
//   对开合态一致。t261：门恒挡（门板开合都实存 —— 合贴朝向边 / 开旋 90° 贴铰链侧邻边）。t359：活版门开合都实存
//   （合=水平薄板顶站立 / 开=铰链侧整高竖直板顶站立 —— 「半门 / 1 格高 ledge」，玩家立于板顶 + 蹲行走）。
bool BlockRegistry::isCollidable(quint8 blockId, quint8 state)
{
    Q_UNUSED(state); // 开合态由 shapeBoxes 内部解码（shape 决定碰撞族；state 仅对 door/trapdoor 精确 AABB 用，
                     //   shapeBoxes 内部读 state，本函数只需 shape 族判定）→ 避免 -Wextra unused-parameter。
    // t444 睡莲薄叶可踩（spec「可在上面走 / 水上行走辅助」）：shape=ShapeNone（selection 空 / 不挡邻居面剔除 /
    //   不遮光 / 不进 heightmap），但碰撞当可踩实体（collisionAABBs 特例返 cell 底薄板）。isCollidable 须与
    //   collisionAABBs 一致返 true，否则 hasGroundBelowAt（脚底支撑复探，读 isCollidable）会判睡莲「无支撑」→
    //   蹲下边缘安全误锁移动。仅此一 ShapeNone 方块特例；torch / water 仍 false（穿过）。
    if (blockId == LilyPad) return true;
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

// t490fix 全 id 段堆叠上限（掉落物合并用；详见 blockregistry.h 头注释）。Core 层纯函数，不依赖 Game。
int   BlockRegistry::maxStackSize(int itemId)
{
    // air / 非法 → 0（不可堆叠，无意义；掉落物合并时 maxStack<=1 不合并）。
    if (itemId <= 0) return 0;
    // 方块段（0..Count-1）：走 BlockDef.maxStack（t42 单一权威；多数 64，门 / 活版门等单件 1）。
    if (itemId < int(Count)) return def(quint8(itemId)).maxStack;
    // 工具段 [0x100, 0x200)：独立耐久 → 不可堆叠（机制等价 MC 1.0 工具 maxStack 1）。含镐 / 斧 / 铲 / 剑 / 锄 / 弓 / 剪刀 / 钓竿。
    if (itemId < 0x200) return 1;
    // 材料段 ≥ 0x200：可堆叠 64（木棒 / 煤 / 铁锭 / 骨头 / 腐肉 / 箭 / 火药 / 羽毛 / 线 / 皮革 / 墨囊 / 蛋 等 mob 掉落物 + 合成材料）。
    //   含护甲段（≥0x300）—— 护甲不会作为 mob / 破块掉落物出现（仅玩家 Q 键丢弃），按 64 合并无害（拾取 Hotbar.addStack 按
    //   真实 maxStack=1 分槽）。桶 / 蘑菇汤（材料段内 maxStack=1 的特例）同理 —— 仅玩家持有 / 丢弃，掉落实体阶段按 64 合并无数据错。
    return 64;
}

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
    case BlockRegistry::ShapeBed:
        // t457/t496 床低盒（与 partialblockgeometry 床床垫顶同高）：cell 底低盒 y[0, kBedMattressTop ~0.31]（床垫顶）。
        //   玩家立于床垫顶（机制等价 MC 床矮半高 hitbox；非整格满高碰撞）。foot / head 半同盒（碰撞不区分头脚）。
        //   t496：床头板 / 床尾板 / 枕头视觉凸出碰撞盒顶（partialblockgeometry 渲染到 9/16 / 7/16），但碰撞仍走
        //   本低盒（机制等价 MC 床低 hitbox + 视觉床头板凸出 —— 玩家可站床垫顶、床头板不挡碰撞）。
        out.push_back({0, 0, 0, 1, BlockRegistry::kBedMattressTop, 1});
        return out;
    case BlockRegistry::ShapeSnowLayer: {
        // t505 积雪层薄板（机制等价 MC 1.0 snow layer 8 层）：cell 底薄板 y[0, snowLayerHeight(state)]。
        //   高度由 state 驱动（state 0..7 → 1/8..1.0；snowLayerHeight 单一权威）。玩家立于薄层顶 = cell+height；
        //   高度 ≤0.5 时玩家 t163 auto-step 抬升 0.55 即可跨过（机制等价 MC 薄雪层可踩 + 半格平滑上行）。
        //   与 partialblockgeometry SnowLayer case 渲染同源（同一 height，碰撞与渲染贴合）。
        const float h = BlockRegistry::snowLayerHeight(state);
        out.push_back({0, 0, 0, 1, h, 1});
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
    // t444 睡莲水上行走（spec「站在睡莲上不掉进水 / 水上行走辅助」）：睡莲 shape=ShapeNone（selection 空、
    //   raycast 整格命中、不挡邻居面剔除、不进 heightmap、不遮光），但须当可踩实体 → 在此特例返 cell 底薄板
    //   （顶面 = 睡莲 quad 高度 1/16，与 partialblockgeometry.cpp LilyPad case 的 yp 同源）。玩家脚位停在睡莲顶面
    //   （= 水面 + 1/16）→ 站在睡莲上不掉进水（机制等价 MC 1.0 lily pad 薄叶可站立）。仅碰撞特例；selection /
    //   raycast / solid / 光照仍走 ShapeNone（四者解耦，同 Farmland 矮盒碰撞特例模式）。薄板厚 1/16 < kEmbedTol(0.1)
    //   且玩家立于板顶（非嵌入）→ isLockedBuried / extrudeEmbedded 不误触（边界 FP 不计嵌入，见 playercontroller）。
    if (blockId == LilyPad)
        return {BlockAABB{0, 0, 0, 1, kLilyPadTop, 1}};
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
    if (isBed(blockId)) return 15;                    // t457 床 solid=false（低 3D 渲染）但仍是 opaque 实体木床 → 满遮光（同 Farmland/Cactus）
    switch (blockId) {
    case WoodTrapdoor: return (state & 1) ? 0 : 15;   // 合=满遮（修「合活版门透光」）/ 开=全透
    case WoodSlab:     return 7;                      // 半遮光（占空比 0.5 → floor(0.5×15)=7 → 衰减 7，约半减）
    case CobbleSlab:   return 7;                      // t412 圆石台阶半遮光（同 WoodSlab，半高占空比 0.5）
    case SpruceSlab:   return 7;                      // t466 云杉台阶半遮光（同 WoodSlab/CobbleSlab，半高占空比 0.5）
    case StoneBrickSlab: return 7;                    // t487 石砖台阶半遮光（同 WoodSlab/CobbleSlab/SpruceSlab，半高占空比 0.5）
    case Farmland:     return 15;                     // t408 耕地 solid=false（矮盒渲染）但仍是 opaque 土块 → 满遮光
    case Cactus:       return 15;                     // t445 仙人掌 solid=false（0.8 细柱渲染）但仍是 opaque 实体植物 → 满遮光
    default:           return 0;                      // 其余全透（air/torch/water/stairs/fence/plate/door/cross）
    }
}

// t351 方块自发光强度（见头注释）：火把=14（既有）、岩浆=15（地底发光，MC 1.0 岩浆光 level 15）、其余 0。
quint8 BlockRegistry::lightEmission(quint8 blockId)
{
    switch (blockId) {
    case Torch: return 14;  // 既有：火把方块光种子 14（radius14 泛光）
    case Lava:  return 15;  // t351：岩浆方块光种子 15（地底发光照亮洞穴；MC 1.0 岩浆光 level 15）
    case EndPortal: return 10; // t487：末地传送门方块光种子 10（地下黑暗要塞中的星绿旋涡泛光，玩家可见传送门；
                               //    机制等价 MC 1.0 末地传送门自发光显眼，非 MC 精确光级，仅照亮自身 + 近旁）
    default:    return 0;   // 其余不自发光
    }
}

// t494 状态感知自发光（见头注释）：非状态相关方块（火把/岩浆/末地传送门）委托单参版（state=0 等价）；
//   熔炉按 lit bit2 翻转：燃烧中 → 13（MC 1.0 熔炉光 level 13），熄灭 → 0。World 光照 flood 种子用此版
//   读 cell 真实 state 区分燃/熄。
quint8 BlockRegistry::lightEmission(quint8 blockId, quint8 state)
{
    // t494：燃烧中的熔炉自发光 13（机制等价 MC 1.0 熔炉冶炼进行时正面发光 level 13）。state bit2 = lit flag。
    if (blockId == Furnace && (state & FurnaceStateLitFlag)) return 13;
    return lightEmission(blockId); // 其余（含熄灭熔炉）按 id-only 表（火把/岩浆/末地传送门等，与 state 无关）
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
    case ShapeBed:      return kBedMattressTop;               // t457 床床垫顶 ~0.31（PCF 软影遮挡高度同床垫顶）
    case ShapeSnowLayer: return snowLayerHeight(state);       // t505 积雪层薄板顶 = snowLayerHeight(state)（1/8..1.0）
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
        if (blockId == Ladder) {
            // t501 木梯精确 sub-AABB（spec「应像火把：不优先选中梯子、可透视穿过，只有指针完全对准梯子才选中」）。
            //   mesher（partialblockgeometry Ladder case）画**单片贴墙 quad**：state[1:0] 决定贴墙方向（=支撑墙所在的
            //   水平方向），quad 贴该面 cell 边内缩 1/16、覆盖另两轴全 [0,1]。这里给一个**贴该面的薄板**（垂直墙法线
            //   方向 3/16 厚、贴 cell 边 1/16 嵌墙余量侧），让准星完全落在木梯视觉面时才命中、瞄格中空气部分穿过
            //   命中后方块（修「爬梯时挖掘优先选中梯子、挖不了旁边方块」）。木梯无碰撞 → 玩家穿入梯格属正常，
            //   起点退化分支（startPartial）会判眼位在 sub-AABB 外 → 不退化、继续命中后方（与火把贴脸同源）。
            //   朝向（state[1:0]）：0=+X 1=-X 2=+Z 3=-Z（与 ladderSupportOffset / partialblockgeometry 同编码）。
            constexpr float kWall = 1.0f / 16.0f;  // 视觉 quad 贴 cell 边内缩量（与 mesher kInset 同源）
            constexpr float kDepth = 3.0f / 16.0f; // 薄板沿墙法线厚度（视觉 quad 是 0 厚，加 3/16 容差使准星微偏亦命中）
            const int face = state & 3;
            switch (face) {
            case 0:  // 支撑墙 +X：quad 贴 x=1-kWall → 薄板 [1-kWall-kDepth, 1-kWall]
                return {BlockAABB{1.0f - kWall - kDepth, 0.0f, 0.0f, 1.0f - kWall, 1.0f, 1.0f}};
            case 1:  // 支撑墙 -X：quad 贴 x=kWall → 薄板 [kWall, kWall+kDepth]
                return {BlockAABB{kWall, 0.0f, 0.0f, kWall + kDepth, 1.0f, 1.0f}};
            case 2:  // 支撑墙 +Z：quad 贴 z=1-kWall → 薄板 [1-kWall-kDepth, 1-kWall]
                return {BlockAABB{0.0f, 0.0f, 1.0f - kWall - kDepth, 1.0f, 1.0f, 1.0f - kWall}};
            default: // case 3：支撑墙 -Z：quad 贴 z=kWall → 薄板 [kWall, kWall+kDepth]
                return {BlockAABB{0.0f, 0.0f, kWall, 1.0f, 1.0f, kWall + kDepth}};
            }
        }
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

// t501 木梯贴墙方向（见头注释）：玩家点击命中面外法线推所贴墙面水平方向。仅水平面（ny==0）合法 —— 顶/底面
//   非贴墙方向，返回 -1（placeBlock 拒绝放置）。4 向编码同 horizontalFacing（0=+X 1=-X 2=+Z 3=-Z）。
//   「支撑墙所在方向」= 命中方块相对木梯格的方向：玩家点中 +X 面（nx>0）→ 木梯在命中方块 +X 侧 → 命中方块
//   是木梯 -X 邻 → 支撑墙在 -X 方向 → 返 1。其余三向同理。
int BlockRegistry::ladderFaceFromNormal(int nx, int ny, int nz)
{
    if (ny != 0) return -1; // 顶/底面非合法贴墙方向 → 拒（placeBlock 须查此返值）
    if (nx > 0) return 1;   // 点中 +X 面 → 支撑墙在 -X 侧 → state=1
    if (nx < 0) return 0;   // 点中 -X 面 → 支撑墙在 +X 侧 → state=0
    if (nz > 0) return 3;   // 点中 +Z 面 → 支撑墙在 -Z 侧 → state=3
    if (nz < 0) return 2;   // 点中 -Z 面 → 支撑墙在 +Z 侧 → state=2
    return 0;               // 无法线（不应发生）→ +X 兜底
}

// t501 木梯支撑墙相对偏移（state 解码）：支撑墙在水平方向（dy 恒 0）。越界 state 值 → +X 兜底。
void BlockRegistry::ladderSupportOffset(quint8 state, int &dx, int &dz)
{
    switch (state & 3) {
    case 0: dx =  1; dz =  0; return; // 支撑墙在 +X 邻
    case 1: dx = -1; dz =  0; return; // 支撑墙在 -X 邻
    case 2: dx =  0; dz =  1; return; // 支撑墙在 +Z 邻
    default: dx = 0; dz = -1; return; // 支撑墙在 -Z 邻（含越界高位兜底为 -Z；& 3 后 case 3）
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
    case CutSandstone: // t485 切制砂岩 → 石质音色（同砂岩族）
    case Obsidian: // t411 黑曜石 → 石质音色（致密火山玻璃，同 cobble/stone 族）
    case CobbleSlab: case CobbleStairs: case CobbleFence: case CobblePressurePlate: // t412 圆石变体 → 石质音色（同 cobble 族）
    case EnchantingTable: // t474 附魔台 → 石质音色（黑曜石+钻石基座，石质偏硬，同 obsidian 族）
    case IronBlock: case Anvil: case AnvilChipped: case AnvilDamaged: // t477 铁块/铁砧 → 石质音色（金属质，同 obsidian 族；机制等价 MC 1.0 iron/anvil metal SoundType）
    case Rail: // t484 铁轨 → 石质音色（金属质敲击，最接近 MC 1.0 铁轨 metal SoundType）
    case MossyCobble: // t486 苔石 → 石质音色（长苔圆石，同 cobble 族）
    case Dispenser: // t486 发射器 → 石质音色（石质机关盒，同 furnace 族）
    case StoneBrick: // t487 石砖 → 石质音色（石质整立方，同 stone 族）
    case StoneBrickSlab: case StoneBrickStairs: // t487 石砖台阶/楼梯 → 石质音色（同 stone 族）
    case EndPortal: // t487 末地传送门 → 石质兜底音色（不可破，仅创造敲响兜底）
        return GroupStone;
    case Ice: // t395 冰 → 石质音色（玻璃质敲击，最接近 MC 1.0 冰 glass SoundType）
    case Glass: // t405 玻璃 → 石质音色（玻璃质敲击，最接近 MC 1.0 玻璃 glass SoundType，同 ice）
        return GroupStone;
    case Log: case Planks: case CraftingTable:
    case WoodSlab: case WoodStairs: case WoodFence:
    case WoodPressurePlate: case WoodDoor: case WoodTrapdoor: // t134 木制半方块 → 木质音色
    case Chest: // t173 箱子 → 木质音色
    case Wool: // t300 羊毛 → 木质音色（软质闷击，最接近 MC 1.0 羊毛 cloth SoundType）
    case WoolOrange: case WoolMagenta: case WoolLightBlue: case WoolYellow: // t455 16 色 wool 变体 → 木质音色（同 white Wool）
    case WoolLime: case WoolPink: case WoolGray: case WoolLightGray:
    case WoolCyan: case WoolPurple: case WoolBlue: case WoolBrown:
    case WoolGreen: case WoolRed: case WoolBlack:
    case BedRed: case BedOrange: case BedYellow: case BedGreen: // t387 床 → 木质音色（软质被面闷击，同 wool）
    case BedCyan: case BedBlue: case BedMagenta: case BedBlack:
    case BedWhite: case BedLightBlue: case BedLime: case BedPink: // t455 16 色床新变体 → 木质音色（同既存床）
    case BedGray: case BedLightGray: case BedPurple: case BedBrown:
    case SpruceLog: // t395 云杉原木 → 木质音色（同 log / planks 族）
    case SprucePlanks: case SpruceSlab: case SpruceFence: case SpruceDoor: // t466 云杉木制品 → 木质音色（同 planks 族）
    case Ladder: // t413 木梯 → 木质音色（木质梯，同 planks 族）
    case Bookshelf: // t474 书架 → 木质音色（木板边框，同 planks 族）
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
    case BrownMushroom: // t507 白蘑菇 / 棕蘑菇 → 软草音色（同红蘑菇；机制等价 MC brown mushroom SoundType = grass）
    case FlowerRed: case FlowerYellow: case FlowerBlue: case FlowerWhite: // t397 4 色花 → 软草音色（软植物，同草丛；机制等价 MC 花 SoundType = grass）
    case Sugarcane: // t397 甘蔗 → 软草音色（细茎软植物，同草丛；机制等价 MC sugar cane SoundType = grass）
    case CarrotCrop: // t407 胡萝卜作物 → 软草音色（同小麦作物；机制等价 MC 作物 SoundType = grass）
    case PotatoCrop: // t407 马铃薯作物 → 软草音色（同小麦作物；机制等价 MC 作物 SoundType = grass）
    case Pumpkin: // t482 南瓜 → 软草音色（瓜类植物，同草丛；机制等价 MC pumpkin SoundType = wood 取软草近似）
    case Cobweb: // t484 蜘蛛网 → 软草音色（蛛丝软质，同草丛；机制等价 MC cobweb SoundType = grass）
    case TntBlock: // t485 TNT → 软草音色（火药捆软质闷击；机制等价 MC 1.0 TNT SoundType = grass）
        return GroupGrass;
    case Sand:
    case SnowLayer: // t395 积雪层 → 颗粒雪响（软质颗粒，最接近 MC 1.0 雪 snow SoundType）
    case Snow: // t482 雪块 → 颗粒雪响（同积雪层；雪傀儡身体材质）
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
