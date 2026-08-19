#include "recipe.h"

#include <algorithm> // std::min（当前未直接用，留作未来 consumeCount > 1 扩展）

// 单一配方数据表（spec t50）。改配方属性只改这里，全工程生效（合成 UI 只读查）。
//
// pattern 行优先（[0..2]=顶行、[3..5]=中行、[6..8]=底行）；0=空格、>0=原料 id：
//   - planks（无序）：1 原木 → 4 木板。pattern 仅 [0]=Log；任意位置 1 原木即可合（2×2 / 3×3 均可）。
//   - stick（有序）：2 木板竖排 → 4 木棒。pattern [0]=Planks、[3]=Planks（左列竖排）；
//     最小包围盒 1×2，允许在 2×2 / 3×3 任意一列竖放。机制等价 MC 木棒配方。
//   - craftingTable（无序）：4 木板 → 1 工作台。pattern 2×2 全木板；多重集 {Planks:4}。
//   - woodPickaxe（有序 3×3）：顶行 3 木板 + 中列 2 木棒（T 形）→ 1 木镐。最小包围盒 3×3（满），
//     只能在工作台合（gridSize=3）。产物 = 木镐（ToolRegistry::PickaxeWood）。
//   - stonePickaxe（有序 3×3）：顶行 3 圆石 + 中列 2 木棒（T 形）→ 1 石镐。机制等价 MC 石镐配方；
//     原料 Cobble（BlockRegistry）+ 木棒（材料段 0x200）。产物 = 石镐（PickaxeStone，tier 2）。
//   - ironPickaxe（有序 3×3）：顶行 3 铁锭 + 中列 2 木棒（T 形）→ 1 铁镐。机制等价 MC 铁镐配方；
//     原料 IronIngot（材料段 RecipeRegistry::IronIngotId=0x203）+ 木棒。产物 = 铁镐（PickaxeIron，tier 3）。
//   - furnace（有序 3×3）：8 圆石围圈（中空）→ 1 熔炉。机制等价 MC 熔炉配方；中心空格，
//     9 圆石实心不匹配。仅工作台可合（gridSize=3）。
//   - torchCoal / torchCharcoal（无序 2×2）：煤炭+木棒 / 木炭+木棒 → 4 火把。机制等价 MC 火把配方；
//     煤与木炭等价（木炭由原木冶炼产出），合出火把供 t88 伪光源用。2 原料任意位置（2×2 / 3×3 均可）。
//
// ── 木棒 id（材料段）──
// spec t50 要求木棒作为独立可堆叠物品（4 件产出 + 木镐配方用 2 根）。本工程物品 id 段：
//   方块段 0..BlockRegistry::Count-1（不可堆叠 / 可堆叠视 maxStack）；工具段 >= 0x100（不可堆叠）。
// 木棒既非方块也非工具，需可堆叠（maxStack 64）。故新增「材料段」id >= 0x200：
//   木棒 id = 0x200（kStickId）。Hotbar 的 isValidItemId / maxStackSize / iconSourceForBlock /
//   nameForBlock 同步识别材料段（t50 扩展，见 hotbar.cpp）。QML 据材料段 id 自绘木棒图标（细长棕色矩形）。
namespace {
constexpr int kStickId = 0x200; // 木棒产物 id（材料段基址；与 Hotbar 材料段判定同源）

constexpr RecipeRegistry::Recipe kRecipes[] = {
    // planks：1 原木 → 4 木板（无序；2×2 背包栏即可合）
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::Log), 0, 0, 0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::Planks), 4, 1, "planks" },
    // stick：2 木板竖排 → 4 木棒（有序；左列竖排，最小包围盒 1×2，可在 2×2 / 3×3 任意列竖放）
    { int(RecipeRegistry::Inventory2x2), false,
      { int(BlockRegistry::Planks), 0, 0,
        int(BlockRegistry::Planks), 0, 0,
        0, 0, 0 },
      kStickId, 4, 1, "stick" },
    // craftingTable：4 木板 2×2 方阵 → 1 工作台（有序；机制等价 MC：须 2×2 方阵，非任意位置）。
    //   t166g：原 shapeless(true) 让 4 板子任意摆放都出工作台（用户反馈「不管怎么摆都可以」过松）→
    //   改 shaped(false)：shapedEqual 收缩到最小包围盒比逐格，输入须 2×2 方阵才匹配（1×4 线 / L 形不匹配）。
    { int(RecipeRegistry::Inventory2x2), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), 0,
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), 0,
        0, 0, 0 },
      int(BlockRegistry::CraftingTable), 1, 1, "crafting_table" },
    // woodPickaxe：3 木板顶行 + 中列 2 木棒（T 形）→ 1 木镐（有序 3×3，仅工作台）
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0,                       kStickId,                 0,
        0,                       kStickId,                 0 },
      int(ToolRegistry::PickaxeWood), 1, 1, "wood_pickaxe" },
    // stonePickaxe：3 圆石顶行 + 中列 2 木棒（T 形）→ 1 石镐（有序 3×3，仅工作台）。机制等价 MC 石镐。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble),
        0,                       kStickId,                 0,
        0,                       kStickId,                 0 },
      int(ToolRegistry::PickaxeStone), 1, 1, "stone_pickaxe" },
    // ironPickaxe：3 铁锭顶行 + 中列 2 木棒（T 形）→ 1 铁镐（有序 3×3，仅工作台）。机制等价 MC 铁镐。
    // 铁锭为材料段物品（IronIngotId=0x203，由铁原矿冶炼产出），非方块段 → 用命名常量引用。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        0,                           kStickId,                    0,
        0,                           kStickId,                    0 },
      int(ToolRegistry::PickaxeIron), 1, 1, "iron_pickaxe" },
    // diamondPickaxe（t472）：3 钻石顶行 + 中列 2 木棒（T 形）→ 1 钻石镐（有序 3×3，仅工作台）。机制等价 MC 钻石镐。
    //   钻石为材料段物品（DiamondId=0x212，由钻石矿挖掘掉落 —— 钻石矿需铁镐采掘）。钻石镐 tier 4 是采掘黑曜石
    //   Obsidian 的唯一工具（Obsidian.minToolTier=4）。T 形同铁镐配方（顶行材料换钻石）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId,
        0,                          kStickId,                   0,
        0,                          kStickId,                   0 },
      int(ToolRegistry::PickaxeDiamond), 1, 1, "diamond_pickaxe" },
    // t589 钻石工具补全（斧 / 铲 / 剑 / 锄）：与铁 / 金 / 铜同类同形（仅换原料为钻石 DiamondId），
    //   机制等价 MC 1.0 diamond tool 配方。产物追加在 ToolId 末尾（0x11D..0x120，不重排保向后兼容）。
    // diamondAxe：3 钻石 L 形 + 2 木棒 → 1 钻石斧（tier 4，speedMul 8.0 / 耐久 1561）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, 0,
        RecipeRegistry::DiamondId, kStickId,                  0,
        0,                         kStickId,                  0 },
      int(ToolRegistry::DiamondAxe), 1, 1, "diamond_axe" },
    // diamondShovel：1 钻石 + 2 木棒纵列 → 1 钻石铲。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, 0,        0,
        kStickId,                  0,        0,
        kStickId,                  0,        0 },
      int(ToolRegistry::DiamondShovel), 1, 1, "diamond_shovel" },
    // diamondSword：2 钻石纵列 + 1 木棒底 → 1 钻石剑（攻击 7，MC 1.0 最高剑伤）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, 0,        0,
        RecipeRegistry::DiamondId, 0,        0,
        kStickId,                  0,        0 },
      int(ToolRegistry::DiamondSword), 1, 1, "diamond_sword" },
    // diamondHoe：2 钻石顶行 + 中列 2 木棒 → 1 钻石锄。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, 0,
        0,                         kStickId,                  0,
        0,                         kStickId,                  0 },
      int(ToolRegistry::DiamondHoe), 1, 1, "diamond_hoe" },
    // t557 金 / 铜工具配方（机制等价 MC 1.0 gold tool 配方 + 本工程铜锭原料；每类与既有木/石/铁同形，仅换顶行 / 刃口
    //   材料为金锭 GoldIngotId（0x21F，金原矿冶炼产物）/ 铜锭 CopperIngotId（0x21D，铜原矿冶炼产物））。产物追加在
    //   ToolId 末尾（0x113..0x11C，不重排既有枚举保向后兼容）。五类各一条金 + 一条铜 = 10 条；形状与既有同类完全
    //   相同（镐 T 形 / 锄 2 材 / 斧 L 形 / 铲 1 材 / 剑 2 材），仅原料 id 换金属锭 → shaped 逐格比 id 不会互相冲突。
    // goldPickaxe：3 金锭顶行 + 中列 2 木棒 → 1 金镐（tier 5，speedMul 12.0 最快 / 耐久 32 最脆）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        0,                            kStickId,                   0,
        0,                            kStickId,                   0 },
      int(ToolRegistry::GoldPickaxe), 1, 1, "gold_pickaxe" },
    // goldHoe：2 金锭顶行 + 中列 2 木棒 → 1 金锄。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, 0,
        0,                            kStickId,                   0,
        0,                            kStickId,                   0 },
      int(ToolRegistry::GoldHoe), 1, 1, "gold_hoe" },
    // goldAxe：3 金锭 L 形 + 2 木棒 → 1 金斧。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, 0,
        RecipeRegistry::GoldIngotId, kStickId,                   0,
        0,                            kStickId,                  0 },
      int(ToolRegistry::GoldAxe), 1, 1, "gold_axe" },
    // goldShovel：1 金锭 + 2 木棒纵列 → 1 金铲。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, 0, 0,
        kStickId,                   0, 0,
        kStickId,                   0, 0 },
      int(ToolRegistry::GoldShovel), 1, 1, "gold_shovel" },
    // goldSword：2 金锭纵列 + 1 木棒底 → 1 金剑。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, 0, 0,
        RecipeRegistry::GoldIngotId, 0, 0,
        kStickId,                   0, 0 },
      int(ToolRegistry::GoldSword), 1, 1, "gold_sword" },
    // copperPickaxe：3 铜锭顶行 + 中列 2 木棒 → 1 铜镐（tier 6，speedMul 5.0 / 耐久 180，介石 / 铁之间）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId,
        0,                              kStickId,                     0,
        0,                              kStickId,                     0 },
      int(ToolRegistry::CopperPickaxe), 1, 1, "copper_pickaxe" },
    // copperHoe：2 铜锭顶行 + 中列 2 木棒 → 1 铜锄。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId, 0,
        0,                              kStickId,                     0,
        0,                              kStickId,                     0 },
      int(ToolRegistry::CopperHoe), 1, 1, "copper_hoe" },
    // copperAxe：3 铜锭 L 形 + 2 木棒 → 1 铜斧。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId, 0,
        RecipeRegistry::CopperIngotId, kStickId,                     0,
        0,                              kStickId,                    0 },
      int(ToolRegistry::CopperAxe), 1, 1, "copper_axe" },
    // copperShovel：1 铜锭 + 2 木棒纵列 → 1 铜铲。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, 0, 0,
        kStickId,                     0, 0,
        kStickId,                     0, 0 },
      int(ToolRegistry::CopperShovel), 1, 1, "copper_shovel" },
    // copperSword：2 铜锭纵列 + 1 木棒底 → 1 铜剑。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, 0, 0,
        RecipeRegistry::CopperIngotId, 0, 0,
        kStickId,                     0, 0 },
      int(ToolRegistry::CopperSword), 1, 1, "copper_sword" },
    // ── 锄（t233）：顶行 2 材料（左 + 中）+ 中列 2 木棒 → 1 锄（有序 3×3，仅工作台）。机制等价 MC 1.0 锄配方。
    //   与镐 T 形的差异：顶行**2**材料（镐为 3），最小包围盒 2×3 vs 镐 3×3 → shaped 匹配包围盒尺寸不同，
    //   不会与镐配方冲突（输入 2 材料 + 2 木棒 → 包围盒 2×3 命中锄、3 材料 + 2 木棒 → 包围盒 3×3 命中镐）。
    //   锄（type=Hoe）专用耕地、不参与挖掘速度（见 toolregistry.h 锄特殊语义注释）。
    //   产物 = 木锄 / 石锄 / 铁锄（HoeWood/Stone/Iron，tier 1/2/3 仅驱动耕地等级，非挖掘）。
    // woodHoe：2 木板顶行 + 中列 2 木棒 → 1 木锄。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), 0,
        0,                       kStickId,                 0,
        0,                       kStickId,                 0 },
      int(ToolRegistry::HoeWood), 1, 1, "wood_hoe" },
    // stoneHoe：2 圆石顶行 + 中列 2 木棒 → 1 石锄。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), 0,
        0,                       kStickId,                 0,
        0,                       kStickId,                 0 },
      int(ToolRegistry::HoeStone), 1, 1, "stone_hoe" },
    // ironHoe：2 铁锭顶行 + 中列 2 木棒 → 1 铁锄。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, 0,
        0,                           kStickId,                    0,
        0,                           kStickId,                    0 },
      int(ToolRegistry::HoeIron), 1, 1, "iron_hoe" },
    // ── t264 完整工具集：斧 / 铲 / 剑 × 木 / 石 / 铁（机制等价 MC 1.0 工具配方）──
    //   斧（Axe）：3 材料顶行左两 + 中行左一材料 + 中列 / 左下各 1 木棒（L 形斧头）。最小包围盒 2×3，
    //     与锄（2×3 但中行左为空）包围盒内容不同 → 不冲突（锄中行 [0,S]、斧中行 [M,S]）。
    //     产物 = 木斧 / 石斧 / 铁斧（AxeWood/Stone/Iron，伐木加速 t265）。
    // woodAxe：3 木板 L 形 + 2 木棒 → 1 木斧。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), 0,
        int(BlockRegistry::Planks), kStickId,                  0,
        0,                          kStickId,                  0 },
      int(ToolRegistry::AxeWood), 1, 1, "wood_axe" },
    // stoneAxe：3 圆石 L 形 + 2 木棒 → 1 石斧。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), 0,
        int(BlockRegistry::Cobble), kStickId,                  0,
        0,                          kStickId,                  0 },
      int(ToolRegistry::AxeStone), 1, 1, "stone_axe" },
    // ironAxe：3 铁锭 L 形 + 2 木棒 → 1 铁斧。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, 0,
        RecipeRegistry::IronIngotId, kStickId,                   0,
        0,                           kStickId,                   0 },
      int(ToolRegistry::AxeIron), 1, 1, "iron_axe" },
    //   铲（Shovel）：1 材料顶 + 2 木棒纵列 → 1 铲。最小包围盒 1×3（顶材料 + 下两棒），
    //     与剑（1×3 但顶两材料 + 下一棒）内容不同 → 不冲突（铲 [M,S,S] vs 剑 [M,M,S]）。
    //     产物 = 木铲 / 石铲 / 铁铲（ShovelWood/Stone/Iron，掘土沙加速 t265）。
    // woodShovel：1 木板 + 2 木棒纵列 → 1 木铲。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), 0, 0,
        kStickId,                   0, 0,
        kStickId,                   0, 0 },
      int(ToolRegistry::ShovelWood), 1, 1, "wood_shovel" },
    // stoneShovel：1 圆石 + 2 木棒纵列 → 1 石铲。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), 0, 0,
        kStickId,                   0, 0,
        kStickId,                   0, 0 },
      int(ToolRegistry::ShovelStone), 1, 1, "stone_shovel" },
    // ironShovel：1 铁锭 + 2 木棒纵列 → 1 铁铲。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, 0, 0,
        kStickId,                    0, 0,
        kStickId,                    0, 0 },
      int(ToolRegistry::ShovelIron), 1, 1, "iron_shovel" },
    //   剑（Sword）：2 材料纵列 + 1 木棒底 → 1 剑。最小包围盒 1×3（上两材料 + 下一棒），
    //     与铲（1×3 但上一材料 + 下两棒）内容不同 → 不冲突（剑 [M,M,S] vs 铲 [M,S,S]）。
    //     产物 = 木剑 / 石剑 / 铁剑（SwordWood/Stone/Iron，攻击伤害 t265）。
    // woodSword：2 木板纵列 + 1 木棒 → 1 木剑。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), 0, 0,
        int(BlockRegistry::Planks), 0, 0,
        kStickId,                   0, 0 },
      int(ToolRegistry::SwordWood), 1, 1, "wood_sword" },
    // stoneSword：2 圆石纵列 + 1 木棒 → 1 石剑。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), 0, 0,
        int(BlockRegistry::Cobble), 0, 0,
        kStickId,                   0, 0 },
      int(ToolRegistry::SwordStone), 1, 1, "stone_sword" },
    // ironSword：2 铁锭纵列 + 1 木棒 → 1 铁剑。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, 0, 0,
        RecipeRegistry::IronIngotId, 0, 0,
        kStickId,                    0, 0 },
      int(ToolRegistry::SwordIron), 1, 1, "iron_sword" },
    // furnace：8 圆石围圈（中空）→ 1 熔炉（有序 3×3，仅工作台）。机制等价 MC 熔炉配方；
    // 满包围盒 3×3（中心空）→ 输入也须 3×3 围圈（中心为空格），9 圆石（实心）不匹配。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble),
        int(BlockRegistry::Cobble), 0,                         int(BlockRegistry::Cobble),
        int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble) },
      int(BlockRegistry::Furnace), 1, 1, "furnace" },
    // torchCoal：煤炭+木棒 → 4 火把（无序 2×2）。机制等价 MC 火把配方；煤炭来自煤矿石挖掘掉落。
    // 2 原料任意位置即可（2×2 背包栏 / 3×3 工作台均可）。
    { int(RecipeRegistry::Inventory2x2), true,
      { RecipeRegistry::CoalId, kStickId, 0, 0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::Torch), 4, 1, "torch_coal" },
    // torchCharcoal：木炭+木棒 → 4 火把（无序 2×2）。机制等价 MC 木炭火把；煤与木炭等价
    // （木炭由原木冶炼产出），与 torchCoal 合出同一火把方块 → 闭环「原木→木炭→火把」。
    { int(RecipeRegistry::Inventory2x2), true,
      { RecipeRegistry::CharcoalId, kStickId, 0, 0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::Torch), 4, 1, "torch_charcoal" },
    // t387 红床：木板+羊毛 → 1 红床（无序 2×2）。机制等价 MC 1.0 床配方（3 板 + 3 羊毛 → 床）的简化版
    //   （本工程用 1 板 + 1 羊毛 → 1 床，降低合成摩擦；色变用独立 id 故配方只产默认红床，其余色变体创造调色板
    //   取用——本工程无染料系统）。羊毛原料用 RecipeRegistry::WoolId（材料段 0x20E，杀羊 / 剪羊毛掉落；创造调色板
    //   亦有）→ 生存可由羊获得；2 原料任意位置即可（2×2 背包栏 / 3×3 工作台均可）。产物 BedRed（方块段，可放置）。
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::Planks), RecipeRegistry::WoolId, 0, 0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::BedRed), 1, 1, "bed_red" },
    // ── t455 16 色床配方（机制等价 MC 1.0 床配方「3 同色羊毛 + 3 木板 → 该色床」）：每色一条有序 3×3 配方，
    //   顶行 3 同色羊毛方块 + 中行 3 木板 → 1 该色床。最小包围盒 3×2，仅工作台可合（gridSize=3）。原料为羊毛
    //   **方块** id（非材料段 wool 物品）→ 配方颜色严格匹配（3 红 wool → 红床，3 蓝 wool → 蓝床，机制等价 MC
    //   「床颜色 = 羊毛颜色」）。white 用既有 Wool=27（剪/杀羊获得）；其余色羊毛由创造调色板取用（无染料系统）。
    //   与既存简化红床配方（1 板+1 wool 物品，无序 2×2）不冲突：本配方包围盒 3×2 / 原料为方块段 wool，前者包围盒
    //   不定 / 原料为材料段 wool 物品 → shapedEqual 不会误匹配。每色 wool→bed id 对齐 BlockRegistry::Id 枚举序。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Wool), int(BlockRegistry::Wool), int(BlockRegistry::Wool),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedWhite), 1, 1, "bed_white" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolOrange), int(BlockRegistry::WoolOrange), int(BlockRegistry::WoolOrange),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedOrange), 1, 1, "bed_orange" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolMagenta), int(BlockRegistry::WoolMagenta), int(BlockRegistry::WoolMagenta),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedMagenta), 1, 1, "bed_magenta" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolLightBlue), int(BlockRegistry::WoolLightBlue), int(BlockRegistry::WoolLightBlue),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedLightBlue), 1, 1, "bed_light_blue" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolYellow), int(BlockRegistry::WoolYellow), int(BlockRegistry::WoolYellow),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedYellow), 1, 1, "bed_yellow" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolLime), int(BlockRegistry::WoolLime), int(BlockRegistry::WoolLime),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedLime), 1, 1, "bed_lime" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolPink), int(BlockRegistry::WoolPink), int(BlockRegistry::WoolPink),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedPink), 1, 1, "bed_pink" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolGray), int(BlockRegistry::WoolGray), int(BlockRegistry::WoolGray),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedGray), 1, 1, "bed_gray" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolLightGray), int(BlockRegistry::WoolLightGray), int(BlockRegistry::WoolLightGray),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedLightGray), 1, 1, "bed_light_gray" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolCyan), int(BlockRegistry::WoolCyan), int(BlockRegistry::WoolCyan),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedCyan), 1, 1, "bed_cyan" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolPurple), int(BlockRegistry::WoolPurple), int(BlockRegistry::WoolPurple),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedPurple), 1, 1, "bed_purple" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolBlue), int(BlockRegistry::WoolBlue), int(BlockRegistry::WoolBlue),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedBlue), 1, 1, "bed_blue" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolBrown), int(BlockRegistry::WoolBrown), int(BlockRegistry::WoolBrown),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedBrown), 1, 1, "bed_brown" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolGreen), int(BlockRegistry::WoolGreen), int(BlockRegistry::WoolGreen),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedGreen), 1, 1, "bed_green" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolRed), int(BlockRegistry::WoolRed), int(BlockRegistry::WoolRed),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedRed), 1, 1, "bed_red_block" },
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::WoolBlack), int(BlockRegistry::WoolBlack), int(BlockRegistry::WoolBlack),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::BedBlack), 1, 1, "bed_black" },
    // ── t134 不完整方块（木制半方块，机制等价 MC 配方；产物 id >= FirstPartial 走异形渲染）──
    //   slab：3 木板横排 → 6 木板台阶（有序 3×3，仅工作台）。MC「3 板横排→6 台阶」。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::WoodSlab), 6, 1, "wood_slab" },
    //   stairs：6 木板阶梯（顶满 / 中左两 / 底左一）→ 4 木板楼梯（有序 3×3，仅工作台）。MC 楼梯配方。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), 0,                         0,
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), 0,
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks) },
      int(BlockRegistry::WoodStairs), 4, 1, "wood_stairs" },
    //   fence：6 木板 + 2 木棒（板-棒-板 ×2 行）→ 3 木栅栏（有序 3×3，仅工作台）。MC 栅栏配方。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), kStickId,                  int(BlockRegistry::Planks),
        int(BlockRegistry::Planks), kStickId,                  int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::WoodFence), 3, 1, "wood_fence" },
    //   pressure_plate：2 木板横排 → 1 木板压力板（有序 2×2 背包栏；最小包围盒 2×1）。
    { int(RecipeRegistry::Inventory2x2), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), 0,
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::WoodPressurePlate), 1, 1, "wood_pressure_plate" },
    //   door：3 木板纵列 → 3 木板门（有序 3×3，仅工作台；最小包围盒 1×3）。MC 门配方（产 3）。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), 0, 0,
        int(BlockRegistry::Planks), 0, 0,
        int(BlockRegistry::Planks), 0, 0 },
      int(BlockRegistry::WoodDoor), 1, 1, "wood_door" }, // codereview C1: outputCount 3→1（门 maxStack=1，canTake 一次取不满 3；MC 输出槽暂存 3 门，本项目 canTake 不支持暂存）
    //   trapdoor：6 木板 2×3（两行三列）→ 2 木活板门（有序 3×3，仅工作台）。MC 活板门配方（产 2）。
    //     注：spec 原注「4 板方形→1(2x2)」与 craftingTable（无序 4 板 → 1 工作台）冲突 —— 4 板 2×2 输入
    //     的多重集 {Planks:4} 必先命中 shapeless 的 craftingTable，trapdoor 永不可合。spec 顶层指令
    //     「MC 配方照搬」据此优先：用 MC 实际配方（6 板 2×3 → 2），避开冲突且对齐 MC。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        0, 0, 0 },
      int(BlockRegistry::WoodTrapdoor), 2, 1, "wood_trapdoor" },
    // ── t412 圆石变体（cobble variants）：机制等价 MC 1.0 石质半方块配方。复用既有 slab/stairs/fence/pressure-plate
    //   配方形状（仅把木板换圆石 + 木棒），与木制半方块同形 → 同包围盒判定，互不冲突（原料不同）。
    //   cobble_slab：3 圆石横排 → 6 圆石台阶（有序 3×3，仅工作台；最小包围盒 3×1）。机制等价 MC 石台阶配方。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble),
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::CobbleSlab), 6, 1, "cobble_slab" },
    //   cobble_stairs：6 圆石阶梯（顶左 / 中左两 / 底满）→ 4 圆石楼梯（有序 3×3，仅工作台；最小包围盒 3×3）。
    //     机制等价 MC 石楼梯配方；与熔炉（8 圆石中空）包围盒内容不同（本 [4]=圆石填、熔炉 [4]=空）→ 不冲突。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), 0,                      0,
        int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), 0,
        int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble) },
      int(BlockRegistry::CobbleStairs), 4, 1, "cobble_stairs" },
    //   cobble_fence：6 圆石 + 2 木棒（石-棒-石 ×2 行）→ 3 圆石墙（有序 3×3，仅工作台）。复用木栅栏配方形状
    //     （机制等价栅栏配方；圆石墙用棒连接同木栅栏结构）。最小包围盒 3×2，与木栅栏包围盒同形但原料不同 → 不冲突。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), kStickId,                  int(BlockRegistry::Cobble),
        int(BlockRegistry::Cobble), kStickId,                  int(BlockRegistry::Cobble),
        0, 0, 0 },
      int(BlockRegistry::CobbleFence), 3, 1, "cobble_fence" },
    //   cobble_pressure_plate：2 圆石横排 → 1 圆石压力板（有序 2×2 背包栏；最小包围盒 2×1）。机制等价 MC 石压力板配方。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), 0,
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::CobblePressurePlate), 1, 1, "cobble_pressure_plate" },
    // ── t466 云杉木制品链配方（机制等价 MC 1.0 spruce 木制品；名称 / 贴图原创自绘 §9a）。复用既有木制品配方
    //   形状（仅把橡木木板 / 原木换云杉木板 / 云杉原木），与橡木木制品同形 → 同包围盒判定，互不冲突（原料不同）。
    //   spruce_planks：1 云杉原木 → 4 云杉木板（无序；2×2 背包栏 / 3×3 工作台均可，同橡木原木→橡木木板）。
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::SpruceLog), 0, 0, 0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::SprucePlanks), 4, 1, "spruce_planks" },
    //   spruce_slab：3 云杉木板横排 → 6 云杉台阶（有序 3×3，仅工作台；最小包围盒 3×1）。复用木板台阶配方形状
    //     （机制等价 MC 木台阶配方）；原料为云杉木板 → 与橡木木板台阶（原料 Planks）不冲突。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::SprucePlanks), int(BlockRegistry::SprucePlanks), int(BlockRegistry::SprucePlanks),
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::SpruceSlab), 6, 1, "spruce_slab" },
    //   spruce_fence：6 云杉木板 + 2 木棒（板-棒-板 ×2 行）→ 3 云杉栅栏（有序 3×3，仅工作台）。复用木栅栏配方形状
    //     （机制等价栅栏配方）；最小包围盒 3×2，与橡木木栅栏包围盒同形但原料不同 → 不冲突。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::SprucePlanks), kStickId,                  int(BlockRegistry::SprucePlanks),
        int(BlockRegistry::SprucePlanks), kStickId,                  int(BlockRegistry::SprucePlanks),
        0, 0, 0 },
      int(BlockRegistry::SpruceFence), 3, 1, "spruce_fence" },
    //   spruce_door：3 云杉木板纵列 → 1 云杉门（有序 3×3，仅工作台；最小包围盒 1×3）。复用木门配方形状
    //     （机制等价 MC 门配方）；outputCount=1（门 maxStack=1，canTake 一次取不满 3，同 wood_door）。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::SprucePlanks), 0, 0,
        int(BlockRegistry::SprucePlanks), 0, 0,
        int(BlockRegistry::SprucePlanks), 0, 0 },
      int(BlockRegistry::SpruceDoor), 1, 1, "spruce_door" },
    // t174 铁桶（空）：3 铁锭 V 形（顶左 + 顶右 + 底中）→ 1 空桶（有序 3×3，仅工作台）。机制等价 MC 1.0
    //   铁桶配方（3 铁锭 V 形）。最小包围盒 3×2（顶行两端 + 底行中），可在工作台任意 3×2 子区放（包围盒
    //   对齐后逐格比）。产物 BucketEmptyId（材料段 0x206，maxStack=1 不可堆叠 —— canTake 一次取 1 件）。
    //   V 形两端 + 底中 = MC 经典「水桶 / 铁桶」共用形状（倒 V 留开口）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, 0,                          RecipeRegistry::IronIngotId,
        0,                           RecipeRegistry::IronIngotId, 0,
        0, 0, 0 },
      RecipeRegistry::BucketEmptyId, 1, 1, "bucket_empty" },
    // t238 面包：3 小麦横排 → 1 面包（有序 3×3，仅工作台）。机制等价 MC 1.0 面包配方（顶行 3 麦穗 → 1 面包）；
    //   最小包围盒 3×1（满行），可在工作台任意一行竖 / 横平移摆放（shapedEqual 最小包围盒对齐）。3 列铺满
    //   → 包围盒 1×3 → 不与 2×3 的锄 / 3×3 的镐配方冲突（包围盒尺寸不同）。产物 BreadId（材料段 0x20A，
    //   maxStack=64 可堆叠；右键食 → +5 饥饿）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::WheatId, RecipeRegistry::WheatId, RecipeRegistry::WheatId,
        0, 0, 0, 0, 0, 0 },
      RecipeRegistry::BreadId, 1, 1, "bread" },
    // t304 弓：3 木棒左斜列 + 3 线右纵列 → 1 弓（有序 3×3，仅工作台）。机制等价 MC 1.0 弓配方
    //   （左斜三木棒 + 右纵三线）。最小包围盒 3×3（满），只能在 3×3 工作台合。产物 Bow（工具段 0x10F，
    //   maxStack=1 不可堆叠 → canTake 一次取 1 件）。木棒（材料段 0x200）+ 线（材料段 0x219，杀蜘蛛掉落）。
    { int(RecipeRegistry::Table3x3), false,
      { kStickId,                0,                          RecipeRegistry::StringId,
        0,                        kStickId,                  RecipeRegistry::StringId,
        kStickId,                0,                          RecipeRegistry::StringId },
      int(ToolRegistry::Bow), 1, 1, "bow" },
    // t304 箭：铁锭（顶）+ 木棒（中）+ 线（底）纵列 → 4 箭（有序 3×3，仅工作台）。机制等价 MC 1.0 箭配方
    //   （燧石 + 棒 + 羽毛纵列 → 4 箭）的本地化替代——本工程无燧石 / 羽毛，用铁锭代箭头、线代羽毛。
    //   最小包围盒 1×3（纵列），可在工作台任意一列竖放（shapedEqual 最小包围盒对齐 → 横放亦匹配）。
    //   产物 ArrowId（材料段 0x21A，maxStack=64；弓弹药）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, 0, 0,
        kStickId,                    0, 0,
        RecipeRegistry::StringId,    0, 0 },
      RecipeRegistry::ArrowId, 4, 1, "arrow" },
    // t300 剪刀：2 铁锭对角（左下 + 右上）→ 1 剪刀（有序 2×2，背包栏 / 工作台均可）。机制等价 MC 1.0 剪刀配方
    //   （2 铁锭对角线）。最小包围盒 2×2（满），shapedEqual 在 2×2 输入内直接匹配；在 3×3 工作台则包围盒对齐后
    //   逐格比（左上 2×2 子区放对角铁锭 → 匹配）。产物 Shears（工具段 0x110，maxStack=1 → canTake 一次取 1 件）。
    //   与锄（2×3）/ 铲（1×3）/ 剑（1×3）等包围盒尺寸不同 → 不冲突。剪刀的真正用途是右键剪羊毛（非挖掘），见
    //   playercontroller placeBlock shears 分支 + EntityManager::shearSheep。
    { int(RecipeRegistry::Inventory2x2), false,
      { 0,                        RecipeRegistry::IronIngotId, 0,
        RecipeRegistry::IronIngotId, 0,                         0,
        0, 0, 0 },
      int(ToolRegistry::Shears), 1, 1, "shears" },
    // t401 钓鱼竿：3 木棒反对角 + 2 线 → 1 钓鱼竿（有序 3×3，仅工作台）。机制等价 MC 1.0 钓竿配方
    //   （左下到右上的反对角三木棒 + 右下 L 形两线）。最小包围盒 3×3（满，[0][2] / [2][0] 两角占位 → 满框），
    //   只能在 3×3 工作台合。与弓配方（左斜三木棒 + 右纵三线）内容不同（弓 [0][0]=木棒、本 [0][0]=空）→ 不冲突。
    //   产物 FishingRod（工具段 0x111，maxStack=1 → canTake 一次取 1 件）。木棒（材料段 0x200）+ 线（材料段 0x219）。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                  0,                  kStickId,
        0,                  kStickId,           RecipeRegistry::StringId,
        kStickId,           RecipeRegistry::StringId, 0 },
      int(ToolRegistry::FishingRod), 1, 1, "fishing_rod" },
    // ── t345 护甲（5 套材质 × 4 部位 = 20 件；机制等价 MC 1.0 护甲配方，有序 3×3 仅工作台）──
    //   每材质 M（皮革 LeatherId / 铁 IronIngotId / 铜 CopperIngotId / 金 GoldIngotId / 钻石 DiamondId），
    //   4 部位共用 MC 经典形状（最小包围盒各异 → 互不冲突，且与既有工具 / 方块配方包围盒尺寸不同）：
    //     头盔 helmet（5 M）：顶行 3 + 中行左右两端（M M M / M . M），包围盒 3×2。
    //     胸甲 chestplate（8 M）：顶两行满 + 底行左右（M M M / M M M / M . M），包围盒 3×3（缺 [7]，
    //       与 furnace 缺 [4] / leggings 缺 [4,7] 内容不同 → 不冲突）。
    //     护腿 leggings（7 M）：顶行满 + 中下两行左右（M M M / M . M / M . M），包围盒 3×3（缺 [4,7]）。
    //     靴子 boots（4 M）：中下两行左右两端（. . . / M . M / M . M），包围盒 3×2（缺中列）。
    //   产物 = 各材质部位护甲（RecipeRegistry::*Helmet/Chestplate/Leggings/Boots，护甲段 0x300..0x313）。
    //   maxStack=1（Hotbar::maxStackSize 护甲段返 1）→ canTake 一次取 1 件（同工具 / 桶）。
    //   皮革来源：杀牛掉落 LeatherId（t242）→ 皮革护甲配方原料（spec「LEATHER comes from cow drops」）。

    // 皮革护甲（原料 LeatherId）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::LeatherId, RecipeRegistry::LeatherId, RecipeRegistry::LeatherId,
        RecipeRegistry::LeatherId, 0,                        RecipeRegistry::LeatherId,
        0, 0, 0 },
      int(RecipeRegistry::LeatherHelmet), 1, 1, "leather_helmet" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::LeatherId, RecipeRegistry::LeatherId, RecipeRegistry::LeatherId,
        RecipeRegistry::LeatherId, RecipeRegistry::LeatherId, RecipeRegistry::LeatherId,
        RecipeRegistry::LeatherId, 0,                        RecipeRegistry::LeatherId },
      int(RecipeRegistry::LeatherChestplate), 1, 1, "leather_chestplate" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::LeatherId, RecipeRegistry::LeatherId, RecipeRegistry::LeatherId,
        RecipeRegistry::LeatherId, 0,                        RecipeRegistry::LeatherId,
        RecipeRegistry::LeatherId, 0,                        RecipeRegistry::LeatherId },
      int(RecipeRegistry::LeatherLeggings), 1, 1, "leather_leggings" },
    { int(RecipeRegistry::Table3x3), false,
      { 0, 0, 0,
        RecipeRegistry::LeatherId, 0,                        RecipeRegistry::LeatherId,
        RecipeRegistry::LeatherId, 0,                        RecipeRegistry::LeatherId },
      int(RecipeRegistry::LeatherBoots), 1, 1, "leather_boots" },
    // 铁护甲（原料 IronIngotId）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, 0,                          RecipeRegistry::IronIngotId,
        0, 0, 0 },
      int(RecipeRegistry::IronHelmet), 1, 1, "iron_helmet" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, 0,                          RecipeRegistry::IronIngotId },
      int(RecipeRegistry::IronChestplate), 1, 1, "iron_chestplate" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, 0,                          RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, 0,                          RecipeRegistry::IronIngotId },
      int(RecipeRegistry::IronLeggings), 1, 1, "iron_leggings" },
    { int(RecipeRegistry::Table3x3), false,
      { 0, 0, 0,
        RecipeRegistry::IronIngotId, 0,                          RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, 0,                          RecipeRegistry::IronIngotId },
      int(RecipeRegistry::IronBoots), 1, 1, "iron_boots" },
    // 铜护甲（原料 CopperIngotId；MC 1.0 无铜护甲 → 自定配方，形状同其他材质）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId,
        RecipeRegistry::CopperIngotId, 0,                            RecipeRegistry::CopperIngotId,
        0, 0, 0 },
      int(RecipeRegistry::CopperHelmet), 1, 1, "copper_helmet" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId,
        RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId,
        RecipeRegistry::CopperIngotId, 0,                            RecipeRegistry::CopperIngotId },
      int(RecipeRegistry::CopperChestplate), 1, 1, "copper_chestplate" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId, RecipeRegistry::CopperIngotId,
        RecipeRegistry::CopperIngotId, 0,                            RecipeRegistry::CopperIngotId,
        RecipeRegistry::CopperIngotId, 0,                            RecipeRegistry::CopperIngotId },
      int(RecipeRegistry::CopperLeggings), 1, 1, "copper_leggings" },
    { int(RecipeRegistry::Table3x3), false,
      { 0, 0, 0,
        RecipeRegistry::CopperIngotId, 0,                            RecipeRegistry::CopperIngotId,
        RecipeRegistry::CopperIngotId, 0,                            RecipeRegistry::CopperIngotId },
      int(RecipeRegistry::CopperBoots), 1, 1, "copper_boots" },
    // 金护甲（原料 GoldIngotId）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, 0,                          RecipeRegistry::GoldIngotId,
        0, 0, 0 },
      int(RecipeRegistry::GoldHelmet), 1, 1, "gold_helmet" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, 0,                          RecipeRegistry::GoldIngotId },
      int(RecipeRegistry::GoldChestplate), 1, 1, "gold_chestplate" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, 0,                          RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, 0,                          RecipeRegistry::GoldIngotId },
      int(RecipeRegistry::GoldLeggings), 1, 1, "gold_leggings" },
    { int(RecipeRegistry::Table3x3), false,
      { 0, 0, 0,
        RecipeRegistry::GoldIngotId, 0,                          RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, 0,                          RecipeRegistry::GoldIngotId },
      int(RecipeRegistry::GoldBoots), 1, 1, "gold_boots" },
    // 钻石护甲（原料 DiamondId）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, 0,                         RecipeRegistry::DiamondId,
        0, 0, 0 },
      int(RecipeRegistry::DiamondHelmet), 1, 1, "diamond_helmet" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, 0,                         RecipeRegistry::DiamondId },
      int(RecipeRegistry::DiamondChestplate), 1, 1, "diamond_chestplate" },
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, 0,                         RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, 0,                         RecipeRegistry::DiamondId },
      int(RecipeRegistry::DiamondLeggings), 1, 1, "diamond_leggings" },
    { int(RecipeRegistry::Table3x3), false,
      { 0, 0, 0,
        RecipeRegistry::DiamondId, 0,                         RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, 0,                         RecipeRegistry::DiamondId },
      int(RecipeRegistry::DiamondBoots), 1, 1, "diamond_boots" },
    // t447 骨粉：1 骨头 → 3 骨粉（无序 2×2）。机制等价 MC 1.0 bone→3 bone meal（1 骨头磨 3 骨粉）。无序
    //   （单原料任意位置即可；2×2 背包栏 / 3×3 工作台均可）。骨粉为消耗品（每右键作物催熟一阶段即消耗 1），
    //   故取 MC 的 1:3 产出比使实用（3 骨粉 = 3 次催熟）。骨头原料 = RecipeRegistry::BoneId（0x217，杀骸骨掉落；
    //   创造调色板亦有）。产物 BonemealId（材料段 0x232，可堆叠 64）。
    { int(RecipeRegistry::Inventory2x2), true,
      { RecipeRegistry::BoneId, 0, 0, 0, 0, 0, 0, 0, 0 },
      RecipeRegistry::BonemealId, 3, 1, "bone_meal" },
    // t469 船：5 木板 U 形（底行 3 + 中行左右各 1，中空）→ 1 船（有序 3×3，仅工作台）。机制等价 MC 1.0 boat
    //   （MC 1.0 boat = 5 木板 U 形 / 底 3 + 中 2）。最小包围盒 3×2（底行 + 中行），与床（3×2 含羊毛）包围盒同尺寸但
    //   内容不同（船中行为木板 + 中空 / 床中行为羊毛 + 木板）→ 不冲突。橡木船用 Planks / 云杉船用 SprucePlanks；
    //   两配方同形（仅原料 id 不同）→ 不会互相匹配（shaped 逐格比 id）。
    //   oak_boat：5 橡木木板 U 形（中行左右 + 底行 3，中空 = 经典 MC 1.0 boat U）→ 1 橡木船。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                          0,                            0,
        int(BlockRegistry::Planks), 0,                            int(BlockRegistry::Planks),
        int(BlockRegistry::Planks), int(BlockRegistry::Planks),   int(BlockRegistry::Planks) },
      RecipeRegistry::OakBoatId, 1, 1, "oak_boat" },
    //   spruce_boat：5 云杉木板 U 形（同 oak_boat 形状，换 SprucePlanks 原料）→ 1 云杉船。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                               0,                               0,
        int(BlockRegistry::SprucePlanks), 0,                               int(BlockRegistry::SprucePlanks),
        int(BlockRegistry::SprucePlanks), int(BlockRegistry::SprucePlanks), int(BlockRegistry::SprucePlanks) },
      RecipeRegistry::SpruceBoatId, 1, 1, "spruce_boat" },
    // t565 矿车：5 铁锭 U 形（底行 3 + 中行左右各 1，中空）→ 1 矿车（有序 3×3，仅工作台）。机制等价 MC 1.0
    //   minecart（5 iron ingot U 形）。与船同形状（最小包围盒 3×2）但原料不同（铁锭 vs 木板）→ 不冲突。
    //   产物 MinecartId（材料段 0x23E；右键铁轨放置矿车实体 + 骑乘行驶）。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                             0,                             0,
        RecipeRegistry::IronIngotId,   0,                             RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId,   RecipeRegistry::IronIngotId,   RecipeRegistry::IronIngotId },
      RecipeRegistry::MinecartId, 1, 1, "minecart" },
    // t473 纸：3 甘蔗横排（任意一行）→ 3 纸（有序 3×3，仅工作台）。机制等价 MC 1.0 paper（3 sugar cane 横排 → 3 paper）。
    //   原料甘蔗 = BlockRegistry::Sugarcane（破甘蔗掉自身方块，可入合成格）。最小包围盒 3×1（一行 3 甘蔗）；
    //   shapedEqual 允许该行在网格内任意纵向平移（顶 / 中 / 底行均可命中）。产物 PaperId（材料段 0x237，可堆叠 64）。
    //   取 3 件产出与 MC 1:3 比一致（3 甘蔗 → 3 纸）。**只能工作台合**（一行 3 宽放不进 2×2 背包栏）。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Sugarcane), int(BlockRegistry::Sugarcane), int(BlockRegistry::Sugarcane),
        0, 0, 0,
        0, 0, 0 },
      RecipeRegistry::PaperId, 3, 1, "paper" },
    // t473 书：3 纸 + 1 皮革 → 1 书（有序 2×2，背包栏 / 工作台均可）。机制等价 MC 1.0 book（3 paper + 1 leather）。
    //   2×2 左上四格：纸×3（左上 / 右上 / 左下）+ 皮革×1（右下）；最小包围盒 2×2。原料 PaperId（本任务纸配方产物）+
    //   LeatherId（0x20D，杀牛 / 杀猪掉落，t242 + t473）。产物 BookId（材料段 0x238，可堆叠 64）。下游消费：附魔台 /
    //   附魔书 / 书架材料。2×2 gridSize 使其背包栏即可合（不强制工作台，便于早期附魔准备）。
    { int(RecipeRegistry::Inventory2x2), false,
      { RecipeRegistry::PaperId,   RecipeRegistry::PaperId,   0,
        RecipeRegistry::PaperId,   RecipeRegistry::LeatherId, 0,
        0,                         0,                         0 },
      RecipeRegistry::BookId, 1, 1, "book" },
    // t474 附魔台（enchanting_table）：1 书 + 2 钻石 + 4 黑曜石 → 1 附魔台（有序 3×3，仅工作台）。
    //   机制等价 MC 1.0 enchanting table 配方（book top-center / 2 diamonds middle-sides / 4 obsidian 其余）。
    //   最小包围盒 3×3（占用整工作台网格）；产物 EnchantingTable 方块（可堆叠 64）。原料 BookId（0x238，t473
    //   书配方产物）+ DiamondId（0x212，钻石矿挖掘掉落）+ Obsidian（黑曜石方块，流水/水源触静岩浆源凝固产物，
    //   t411/t472）。只能工作台合（3×3 包围盒放不进 2×2 背包栏，机制等价 MC 附魔台须工作台）。
    //   行优先 pattern[9]（[0..2]=顶行、[3..5]=中行、[6..8]=底行）：
    //     [0]=空 [1]=book [2]=空
    //     [3]=diamond [4]=obsidian [5]=diamond
    //     [6]=obsidian [7]=obsidian [8]=obsidian
    //   合计：1 book + 2 diamond + 4 obsidian，与 spec「2 diamond + 4 obsidian + 1 book」一致。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                                RecipeRegistry::BookId,    0,
        RecipeRegistry::DiamondId,        int(BlockRegistry::Obsidian), RecipeRegistry::DiamondId,
        int(BlockRegistry::Obsidian),     int(BlockRegistry::Obsidian), int(BlockRegistry::Obsidian) },
      int(BlockRegistry::EnchantingTable), 1, 1, "enchanting_table" },
    // t474 书架（bookshelf）：6 木板 + 3 书 → 1 书架（有序 3×3，仅工作台）。机制等价 MC 1.0 bookshelf 配方
    //   （上 / 下两行木板、中间一行 3 书）。最小包围盒 3×3（占用整工作台网格）；产物 Bookshelf 方块（可堆叠 64）。
    //   原料 Planks（橡木木板，原木合成产物）+ BookId（0x238，t473 书配方产物）。只能工作台合（3×3 包围盒）。
    //   pattern：[0..2]=planks / [3..5]=book / [6..8]=planks，合计 6 planks + 3 book，与 spec「6 planks + 3 books」一致。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks),
        RecipeRegistry::BookId,     RecipeRegistry::BookId,     RecipeRegistry::BookId,
        int(BlockRegistry::Planks), int(BlockRegistry::Planks), int(BlockRegistry::Planks) },
      int(BlockRegistry::Bookshelf), 1, 1, "bookshelf" },
    // t477 铁块（iron_block）：9 铁锭 3×3 满铺 → 1 铁块（有序 3×3，仅工作台）。机制等价 MC 1.0 iron block
    //   （9 ingots ↔ 1 block 存储方块，机制对标）。铁锭 = RecipeRegistry::IronIngotId（0x203，铁原矿冶炼产物）。
    //   产物 = IronBlock（铁砧配方前置）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId },
      int(BlockRegistry::IronBlock), 1, 1, "iron_block" },
    // t477 铁砧（anvil）：3 铁块顶行 + 中格 1 铁锭 + 底行 3 铁锭（4 铁锭）→ 1 完好铁砧（有序 3×3，仅工作台）。
    //   机制等价 MC 1.0 anvil 配方（顶 3 iron block + 中 1 iron ingot + 底 3 iron ingot = 3 块 + 4 锭）。
    //   产物 = Anvil（完好铁砧；损坏态 AnvilChipped/AnvilDamaged 不经合成，由使用产生）。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::IronBlock), int(BlockRegistry::IronBlock), int(BlockRegistry::IronBlock),
        0,                              RecipeRegistry::IronIngotId,  0,
        RecipeRegistry::IronIngotId,    RecipeRegistry::IronIngotId,  RecipeRegistry::IronIngotId },
      int(BlockRegistry::Anvil), 1, 1, "anvil" },
    // t620 矿物存储块 × 铁块：9 材料 3×3 满铺 ↔ 1 块 双向配方（机制等价 MC 1.0 coal/lapis/diamond/gold/
    //   redstone/iron block 的 9↔1 无损压缩存储）。正向（材料 → 块）：9 材料 3×3 满铺 → 1 块（有序 3×3，仅
    //   工作台；同 t477 铁块模式）。反向（块 → 材料）：1 块任意格单放 → 9 材料（无序 Inventory2x2 / 3×3
    //   均可——单原料 shapeless 在任意合成格可拆，机制等价 MC「单放方块即拆 9 个」）。铁块正向既有（t477），
    //   本段补铁块反向 + 其余五种双向。冲突检：正向 9 煤满铺多重集 {Coal:9} 唯一；反向单块 {CoalBlock:1} 唯一
    //   → 与铁砧（{IronBlock:3, IronIngot:4}）/ 指南针（{IronIngot:4, Redstone:1}）等均不冲突。
    //   coal：9 煤炭 → 1 煤炭块（燃料 800s，smelting.cpp 燃料表）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::CoalId, RecipeRegistry::CoalId, RecipeRegistry::CoalId,
        RecipeRegistry::CoalId, RecipeRegistry::CoalId, RecipeRegistry::CoalId,
        RecipeRegistry::CoalId, RecipeRegistry::CoalId, RecipeRegistry::CoalId },
      int(BlockRegistry::CoalBlock), 1, 1, "coal_block" },
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::CoalBlock), 0, 0, 0, 0, 0, 0, 0, 0 },
      RecipeRegistry::CoalId, 9, 1, "coal" },
    //   lapis：9 青金石 ↔ 1 青金石块（附魔材料的压缩存储）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::LapisId, RecipeRegistry::LapisId, RecipeRegistry::LapisId,
        RecipeRegistry::LapisId, RecipeRegistry::LapisId, RecipeRegistry::LapisId,
        RecipeRegistry::LapisId, RecipeRegistry::LapisId, RecipeRegistry::LapisId },
      int(BlockRegistry::LapisBlock), 1, 1, "lapis_block" },
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::LapisBlock), 0, 0, 0, 0, 0, 0, 0, 0 },
      RecipeRegistry::LapisId, 9, 1, "lapis" },
    //   diamond：9 钻石 ↔ 1 钻石块。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId,
        RecipeRegistry::DiamondId, RecipeRegistry::DiamondId, RecipeRegistry::DiamondId },
      int(BlockRegistry::DiamondBlock), 1, 1, "diamond_block" },
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::DiamondBlock), 0, 0, 0, 0, 0, 0, 0, 0 },
      RecipeRegistry::DiamondId, 9, 1, "diamond" },
    //   gold：9 金锭 ↔ 1 金块。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId },
      int(BlockRegistry::GoldBlock), 1, 1, "gold_block" },
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::GoldBlock), 0, 0, 0, 0, 0, 0, 0, 0 },
      RecipeRegistry::GoldIngotId, 9, 1, "gold_ingot" },
    //   redstone：9 红石粉 ↔ 1 红石块。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::RedstoneId, RecipeRegistry::RedstoneId, RecipeRegistry::RedstoneId,
        RecipeRegistry::RedstoneId, RecipeRegistry::RedstoneId, RecipeRegistry::RedstoneId,
        RecipeRegistry::RedstoneId, RecipeRegistry::RedstoneId, RecipeRegistry::RedstoneId },
      int(BlockRegistry::RedstoneBlock), 1, 1, "redstone_block" },
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::RedstoneBlock), 0, 0, 0, 0, 0, 0, 0, 0 },
      RecipeRegistry::RedstoneId, 9, 1, "redstone" },
    //   iron 反向（正向 t477 既存）：1 铁块 → 9 铁锭。
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::IronBlock), 0, 0, 0, 0, 0, 0, 0, 0 },
      RecipeRegistry::IronIngotId, 9, 1, "iron_ingot" },
    // t620 红石灯：4 红石粉十字 + 中心 1 玻璃 → 1 红石灯（有序 3×3，仅工作台）。机制对标 MC 1.0 redstone
    //   lamp（glowstone + 4 redstone）—— 本工程无荧石，用玻璃作壳（玻璃 = 沙子冶炼产物 GlassId 0x204，
    //   「透光壳内藏红石」语义）。最小包围盒 3×3 满铺（四角空 + 四边红石 + 中心玻璃），与指南针
    //   （{IronIngot:4, Redstone:1} 十字）/ 钟（{GoldIngot:4, Redstone:1} 十字）同形但原料不同（本为
    //   {Redstone:4, Glass:1}）→ 多重集唯一不冲突。产物 = RedstoneLamp（右键开关的可放置光源方块）。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                         RecipeRegistry::RedstoneId, 0,
        RecipeRegistry::RedstoneId, RecipeRegistry::GlassId,    RecipeRegistry::RedstoneId,
        0,                          RecipeRegistry::RedstoneId, 0 },
      int(BlockRegistry::RedstoneLamp), 1, 1, "redstone_lamp" },
    // t484 铁轨（rail）：6 铁锭（顶行 + 中行两行满铺）→ 16 铁轨（有序 3×3，仅工作台）。
    //   机制等价 MC 1.0 rail 配方（6 iron ingot 满两行 → 16 rail；MC 实际是「6 铁锭纵列」最小包围盒 1×6，
    //   本工作台 3×3 取「顶行 + 中行 = 6 锭」2×3 包围盒，对齐 MC 数量与质材，避开 1×6 在 3×3 内的歧义）。
    //   最小包围盒 3×2（顶 + 中两行铁锭满），与铁块（3×3 满铺）包围盒尺寸不同 → 不冲突。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        0,                           0,                           0 },
      int(BlockRegistry::Rail), 16, 1, "rail" },
    // t722 铁门（iron door）：6 铁锭**竖摆**（左两列 × 三行满）→ 1 铁门（有序 3×3，仅工作台）。
    //   机制等价 MC 1.0 iron door 配方（6 iron ingot 摆 2 宽 × 3 高竖排 → 1 iron door）。最小包围盒
    //   2×3（竖），与铁轨（{Iron:6} 横排 3×2 包围盒）**形状不同** → 有序匹配不冲突（shapedEqual 比
    //   包围盒尺寸 + 逐格——同多重集 {IronIngot:6} 但横竖摆位不同，MC 同此区分）。outputCount=1
    //   （门 maxStack=1，canTake 一次取不满 3，同 wood_door / spruce_door 口径）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, 0,
        RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, 0,
        RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, 0 },
      int(BlockRegistry::IronDoor), 1, 1, "iron_door" },
    // t638 动力铁轨（golden/powered rail）：6 金锭（顶两行满）+ 底行中位木棒 + 底行右位红石粉 → 6 动力轨
    //   （有序 3×3，仅工作台）。机制等价 MC 1.0 配方（6 gold ingot + 1 stick + 1 redstone → 6 powered
    //   rail）。多重集 {Gold:6, Stick:1, Redstone:1} 唯一 → 与指南针 / 钟的 {X:4, Redstone:1} 十字形和
    //   TNT 的 X 形均不冲突。最小包围盒 3×3（含底行）→ 与铁块（3×3 满 9 锭）包围盒同尺寸但原料组合唯一。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId,
        0,                            RecipeRegistry::StickId,    RecipeRegistry::RedstoneId },
      int(BlockRegistry::GoldenRail), 6, 1, "golden_rail" },
    // t638 探测铁轨（detector rail）：6 铁锭（顶两行满）+ 底行中位**石压力板** + 底行右位红石 → 6 探测轨
    //   （有序 3×3，仅工作台）。机制等价 MC 1.0 配方（6 iron ingot + 1 stone pressure plate + 1 redstone
    //   → 6 detector rail）。多重集 {Iron:6, StonePlate:1, Redstone:1} 唯一 → 不与普通铁轨 {Iron:6} /
    //   动力轨冲突（原料组合独一无二）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId,
        0,                            int(BlockRegistry::StonePressurePlate), RecipeRegistry::RedstoneId },
      int(BlockRegistry::DetectorRail), 6, 1, "detector_rail" },
    // t638 红石火把（redstone torch）：木棒上 + 红石粉下（竖列 2 格）→ 1 红石火把（有序 2×2 兼 3×3，
    //   背包栏 / 工作台均可）。机制等价 MC 1.0 配方（1 stick + 1 redstone → 1 redstone torch on stick）。
    //   最小包围盒 1×2（竖列），多重集 {Stick:1, Redstone:1} 唯一 → 不冲突（棒 / 红石其余配方均多件或带它料）。
    { int(RecipeRegistry::Inventory2x2), false,
      { RecipeRegistry::StickId,    0, 0,
        RecipeRegistry::RedstoneId, 0, 0,
        0, 0, 0 },
      int(BlockRegistry::RedstoneTorch), 1, 1, "redstone_torch" },
    // t565 白羊毛（wool）：4 线（2×2 满铺）→ 1 白羊毛（有序 2×2，背包栏 / 工作台均可）。机制等价 MC 1.0
    //   配方（4 string → 1 白羊毛；用户报「4 线合成白羊毛（背包 2×2 配方）」）。最小包围盒 2×2（满），
    //   shapedEqual 收缩后逐格比 → 2×2 背包栏 / 工作台角 2×2 均可合。产物 Wool=27（白色羊毛方块，可放置；
    //   区别材料段 WoolId=0x20E 物品）。与雪球（无序 2×2）多重集 {String:4} 唯一 → 不冲突。
    { int(RecipeRegistry::Inventory2x2), false,
      { RecipeRegistry::StringId, RecipeRegistry::StringId, 0,
        RecipeRegistry::StringId, RecipeRegistry::StringId, 0,
        0, 0, 0 },
      int(BlockRegistry::Wool), 1, 1, "wool" },
    // t485 TNT：5 火药（X 形对角）+ 4 沙（四角）→ 1 TNT（有序 3×3，仅工作台）。机制等价 MC 1.0 TNT 配方
    //   （5 gunpowder + 4 sand，沙填四角 + 中边、火药走对角与中心）。最小包围盒 3×3 满铺，与铁块（9 铁锭满铺）、
    //   TNT 自身的多重集 {火药:5, 沙:4} 唯一 → 不与既有配方冲突（原料组合独一无二）。
    { int(RecipeRegistry::Table3x3), false,
      { RecipeRegistry::GunpowderId, int(BlockRegistry::Sand),    RecipeRegistry::GunpowderId,
        int(BlockRegistry::Sand),    RecipeRegistry::GunpowderId, int(BlockRegistry::Sand),
        RecipeRegistry::GunpowderId, int(BlockRegistry::Sand),    RecipeRegistry::GunpowderId },
      int(BlockRegistry::TntBlock), 1, 1, "tnt" },
    // t507 木碗（bowl）：3 木板 V 形（左上 / 右上 / 左下，右下空）→ 1 木碗（有序 2×2，背包栏 / 工作台均可）。
    //   机制等价 MC 1.0 木碗配方（3 planks V 形）。最小包围盒 2×2（[P,P]/[P,0]），与 craftingTable（2×2 满铺 [P,P]/[P,P]）
    //   包围盒尺寸同但内容不同（木碗右下须为空）→ shaped 逐格比对区分（不冲突）。产物 = BowlId（材料段，蘑菇汤原料）。
    { int(RecipeRegistry::Inventory2x2), false,
      { int(BlockRegistry::Planks), int(BlockRegistry::Planks), 0,
        int(BlockRegistry::Planks), 0,                          0,
        0,                          0,                          0 },
      RecipeRegistry::BowlId, 1, 1, "bowl" },
    // t507 蘑菇汤（mushroom_stew）：碗 + 红蘑菇 + 白蘑菇 → 1 蘑菇汤（无序 2×2，背包栏 / 工作台均可）。
    //   机制等价 MC 1.0 mushroom_stew 配方（bowl + red mushroom + brown mushroom 任意摆放）。3 原料各 1 件、
    //   多重集 {Bowl:1, Mushroom:1, BrownMushroom:1} 唯一 → shapeless 不与既有配方冲突。产物 = MushroomStewId
    //   （材料段，maxStack=1 不可堆叠；右键食 +10 饥饿，食完返空碗 —— finishEating 特判）。
    { int(RecipeRegistry::Inventory2x2), true,
      { RecipeRegistry::BowlId, int(BlockRegistry::Mushroom), int(BlockRegistry::BrownMushroom),
        0,                       0,                            0,
        0,                       0,                            0 },
      RecipeRegistry::MushroomStewId, 1, 1, "mushroom_stew" },
    // t505 雪块（snow block）：4 雪球 2×2 方阵 → 1 雪块（有序 2×2，背包栏 / 工作台均可）。机制等价 MC 1.0
    //   雪块配方（4 snowballs → 1 snow block）。最小包围盒 2×2（满），与 craftingTable（2×2 满铺木板）包围盒
    //   尺寸同但原料不同（本为雪球 / 工作台为木板）→ shaped 逐格比对区分（不冲突）。原料 SnowballId（材料段
    //   0x23D，雪傀儡死亡掉落 / 铲挖雪层 / 铲挖雪块获得）。产物 = Snow（方块段 101，实心整立方；4 雪球 ↔ 1 雪块
    //   存储方块，机制对标 MC snow block 配方）。铲挖雪块掉 4 雪球 + 4 雪球合雪块 → 闭环（无损耗，机制等价 MC）。
    { int(RecipeRegistry::Inventory2x2), false,
      { RecipeRegistry::SnowballId, RecipeRegistry::SnowballId, 0,
        RecipeRegistry::SnowballId, RecipeRegistry::SnowballId, 0,
        0,                           0,                          0 },
      int(BlockRegistry::Snow), 1, 1, "snow_block" },
    // t567 指南针：4 铁锭十字 + 中心 1 红石 → 1 指南针（有序 3×3，仅工作台）。机制等价 MC 1.0 compass 配方
    //   （4 iron ingot 十字 + 1 redstone 中心）。最小包围盒 3×3 满铺（四角空 + 四边铁锭 + 中心红石），
    //   多重集 {铁锭:4, 红石:1} 唯一 → 与既有配方不冲突。产物 CompassId（材料段 0x23F；HUD 指针指向出生点）。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                           RecipeRegistry::IronIngotId, 0,
        RecipeRegistry::IronIngotId, RecipeRegistry::RedstoneId, RecipeRegistry::IronIngotId,
        0,                           RecipeRegistry::IronIngotId, 0 },
      RecipeRegistry::CompassId, 1, 1, "compass" },
    // t568 钟：4 金锭十字 + 中心 1 红石 → 1 钟（有序 3×3，仅工作台）。机制等价 MC 1.0 clock 配方
    //   （4 gold ingot 十字 + 1 redstone 中心）。最小包围盒 3×3 满铺，与指南针同形但原料不同（金锭 vs 铁锭）
    //   → shaped 逐格比对区分（不冲突）。产物 ClockId（材料段 0x240；HUD 显示当前昼夜相位）。
    { int(RecipeRegistry::Table3x3), false,
      { 0,                           RecipeRegistry::GoldIngotId, 0,
        RecipeRegistry::GoldIngotId, RecipeRegistry::RedstoneId, RecipeRegistry::GoldIngotId,
        0,                           RecipeRegistry::GoldIngotId, 0 },
      RecipeRegistry::ClockId, 1, 1, "clock" },
    // t720 画作（painting）：8 木棒围 1 羊毛（中空环）→ 1 画作（有序 3×3，仅工作台）。机制等价 MC 1.0
    //   painting 配方（8 sticks + any wool）。最小包围盒 3×3 满框（四边木棒 + 中心羊毛），多重集
    //   {木棒:8, 羊毛:1} 唯一 → 不与既有配方冲突。产物 PaintingId（材料段 0x242，maxStack=1 放置型
    //   物品；右键墙侧面 → 尺寸检测随机贴画，t720）。
    { int(RecipeRegistry::Table3x3), false,
      { kStickId,                    int(BlockRegistry::Wool),  kStickId,
        int(BlockRegistry::Wool),    0,                         int(BlockRegistry::Wool),
        kStickId,                    int(BlockRegistry::Wool),  kStickId },
      RecipeRegistry::PaintingId, 1, 1, "painting" },
    // t609 投掷器（dropper）：7 圆石（缺中心 + 上中）→ 1 投掷器（有序 3×3，仅工作台）。机制等价 MC 1.0
    //   dropper 配方（7 cobblestone——与熔炉 8 圆石围圈同族，差异 = 熔炉缺 [4] 中心 1 格、本缺 [1] 上中 +
    //   [4] 中心 2 格）。最小包围盒 3×3 满框（顶行两端 + 中行两端 + 底行 3 满占角 → 满框），与熔炉（缺 [4]）/
    //   护腿（缺 [4,7]）内容不同 → shaped 逐格比对区分（不冲突）。产物 Dropper 方块（踩压力板触发把全部物品
    //   弹出掉落物的机关盒；DispenserStore 9 槽库存 + 右键开 UI）。
    //     [0]=cobble [1]=空     [2]=cobble
    //     [3]=cobble [4]=空     [5]=cobble
    //     [6]=cobble [7]=cobble [8]=cobble
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), 0,                         int(BlockRegistry::Cobble),
        int(BlockRegistry::Cobble), 0,                         int(BlockRegistry::Cobble),
        int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble) },
      int(BlockRegistry::Dropper), 1, 1, "dropper" },
    // t627 压力板家族扩展配方（机制等价 MC 1.0 压力板 2 材料横排；同 wood_pressure_plate 的 2×2 背包栏模式——
    //   cobble_plate 历史误用 Table3x3，本族按 MC/木板口径统一 Inventory2x2）。
    //   stone_pressure_plate：2 石头横排 → 1（有序 2×2；最小包围盒 2×1）。多重集 {Stone:2} 唯一 → 不冲突。
    { int(RecipeRegistry::Inventory2x2), false,
      { int(BlockRegistry::Stone), int(BlockRegistry::Stone), 0,
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::StonePressurePlate), 1, 1, "stone_pressure_plate" },
    //   iron_pressure_plate：2 铁锭横排 → 1（有序 2×2；重质板——仅玩家/mob 触发）。多重集 {IronIngot:2} 唯一。
    { int(RecipeRegistry::Inventory2x2), false,
      { RecipeRegistry::IronIngotId, RecipeRegistry::IronIngotId, 0,
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::IronPressurePlate), 1, 1, "iron_pressure_plate" },
    //   gold_pressure_plate：2 金锭横排 → 1（有序 2×2；轻质板——掉落物即触发）。多重集 {GoldIngot:2} 唯一。
    { int(RecipeRegistry::Inventory2x2), false,
      { RecipeRegistry::GoldIngotId, RecipeRegistry::GoldIngotId, 0,
        0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::GoldPressurePlate), 1, 1, "gold_pressure_plate" },
    // t628 手动点火机关三件配方（机制等价 MC 1.0 lever / wooden button / stone button；此前 t490 只加了方块
    //   未接配方——机关仅创造调色板可得，生存不可合成，dev-plan t628「配方已有?核」核实为缺，补齐）。
    //   lever：1 圆石 + 1 木棒纵列（圆石上 / 木棒下）→ 1 杠杆（有序 2×2；最小包围盒 1×2）。机制等价 MC 1.0
    //     杠杆配方（cobble 上 + stick 下）。包围盒内容 {Cobble,Stick} 与锄（2×3）/ 铲（1×3）不同 → 不冲突。
    { int(RecipeRegistry::Inventory2x2), false,
      { int(BlockRegistry::Cobble), 0, 0,
        kStickId,             0, 0,
        0, 0, 0 },
      int(BlockRegistry::Lever), 1, 1, "lever" },
    //   wood_button：1 木板 → 1 木按钮（无序 2×2；单原料任意格）。机制等价 MC 1.0 按钮单材料配方（木板）。
    //     多重集 {Planks:1} 唯一（原木→木板是 {Log:1}、床是 {Planks:1,Wool:1}）→ 不冲突。
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::Planks), 0, 0, 0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::WoodButton), 1, 1, "wood_button" },
    //   stone_button：1 石头 → 1 石按钮（无序 2×2；单原料任意格）。石头经熔炉烧圆石产出（smelting）。
    //     多重集 {Stone:1} 唯一 → 不冲突。
    { int(RecipeRegistry::Inventory2x2), true,
      { int(BlockRegistry::Stone), 0, 0, 0, 0, 0, 0, 0, 0 },
      int(BlockRegistry::StoneButton), 1, 1, "stone_button" },
};

// 编译期断言：木棒 id 与 Hotbar 材料段基址（kMaterialIdBase=0x200）一致；改一处须同步另一处。
static_assert(kStickId == 0x200, "木棒 id 须与 Hotbar 材料段基址 0x200 一致");
// 木棒 id 经 recipe.h 的 RecipeRegistry::StickId（static constexpr）对外暴露（Hotbar 读它识别材料段）；
// 此处再断言 .cpp 内部常量与头文件常量一致（防漂移）。
static_assert(RecipeRegistry::StickId == kStickId, "recipe.h StickId 与 .cpp kStickId 须一致");
// 材料段矿石掉落 id（t85 命名）：Core 层 blockregistry.cpp 不依赖 Game，CoalOre/IronOre 的 dropId 用
// 字面量 0x201/0x202（见该文件注释「t85 命名」）；本处用 static_assert 钉死 recipe.h 常量 == 字面量，
// 任一处改动值而忘了同步另一处 → 编译失败（跨层数据契约的保护，因 Core 不能 include Game 头）。
static_assert(RecipeRegistry::CoalId        == 0x201, "CoalId 须与 BlockRegistry::CoalOre.dropId 字面量 0x201 一致");
static_assert(RecipeRegistry::IronOreDropId == 0x202, "IronOreDropId 须与 BlockRegistry::IronOre.dropId 字面量 0x202 一致");
static_assert(RecipeRegistry::IronIngotId   == 0x203, "IronIngotId 须为材料段序号 0x203");
// t308 铜/金原矿 + 锭跨层契约（同 Coal/Iron 模式）：Core 层 blockregistry.cpp CopperOre/GoldOre 的 dropId 用
//   字面量 0x21C/0x21E（Core 不 include Game 头）；本处钉死 recipe.h 常量 == 字面量，防漂移致掉落 / 冶炼断裂。
static_assert(RecipeRegistry::CopperOreDropId == 0x21C, "CopperOreDropId 须与 BlockRegistry::CopperOre.dropId 字面量 0x21C 一致");
static_assert(RecipeRegistry::CopperIngotId   == 0x21D, "CopperIngotId 须为材料段序号 0x21D");
static_assert(RecipeRegistry::GoldOreDropId   == 0x21E, "GoldOreDropId 须与 BlockRegistry::GoldOre.dropId 字面量 0x21E 一致");
static_assert(RecipeRegistry::GoldIngotId     == 0x21F, "GoldIngotId 须为材料段序号 0x21F");
// t471 青金石跨层契约（同 Coal/Iron/Copper/Gold 模式）：Core 层 blockregistry.cpp LapisOre 的 dropId 用字面量
//   0x236（Core 不 include Game 头）；本处钉死 recipe.h 常量 == 字面量，防漂移致掉落断裂。
static_assert(RecipeRegistry::LapisId         == 0x236, "LapisId 须与 BlockRegistry::LapisOre.dropId 字面量 0x236 一致");

// t348 引擎材料段 id → MC Java 1.0.0 物品数字 id 对齐表（资源包前置；单一权威，与 docs/item-ids.md 材料 / mob
//   掉落 / 生物蛋段「MC 1.0.0」列一致）。行索引 = engineMaterialId - MaterialIdBase（覆盖 [0x200, 0x22E] = 47 项，
//   含材料 / mob 死亡掉落 / 生物蛋 / 熟肉 / 战利品表物品 / 鸡系列 / 鱿鱼系列六子集——MC 1.0 均为「物品」）。**不重排常量**（保存档 /
//   配方 / 掉落表向后兼容）。无 MC 1.0 等价（铁 / 金 / 铜原矿与锭 1.17+、熟羊肉 1.8+、命名牌 1.6+、附魔书 1.4+）
//   → -1；生物蛋（spawn_egg_*）→ 383（MC 1.0 单一 spawn egg id + metadata）。新增材料段物品须在此补一行（否则越界 -1）。
constexpr int kMcMaterialIdCount = RecipeRegistry::SpawnEggSquidId - RecipeRegistry::MaterialIdBase + 1; // 0x200..0x22E = 47
constexpr int kMcMaterialId[kMcMaterialIdCount] = {
    /* 0x200 stick        */ 280, /* 0x201 coal         */ 263, /* 0x202 iron_ore_drop */ -1,  /* 0x203 iron_ingot */ 265,
    /* 0x204 glass        */ 20,  /* 0x205 charcoal     */ 263, /* 0x206 bucket_empty */ 325, /* 0x207 water_bucket */ 326,
    /* 0x208 seed         */ 295, /* 0x209 wheat        */ 296, /* 0x20A bread        */ 297, /* 0x20B raw_porkchop */ 319,
    /* 0x20C raw_beef     */ 363, /* 0x20D leather      */ 334, /* 0x20E wool         */ 35,  /* 0x20F spawn_egg_pig */ 383,
    /* 0x210 spawn_egg_cow*/ 383, /* 0x211 spawn_egg_sheep */ 383, /* 0x212 diamond    */ 264, /* 0x213 spawn_egg_shambler */ 383,
    /* 0x214 spawn_egg_bones */ 383, /* 0x215 spawn_egg_stalker */ 383, /* 0x216 spawn_egg_spider */ 383,
    /* 0x217 bone         */ 352, /* 0x218 rotten_flesh */ 367, /* 0x219 string       */ 287, /* 0x21A arrow      */ 262,
    /* 0x21B sapling_item */ 6,   /* 0x21C copper_ore_drop */ -1, /* 0x21D copper_ingot */ -1, /* 0x21E gold_ore_drop */ -1,
    /* 0x21F gold_ingot   */ 266,
    /* 0x220 lava_bucket  */ 327,
    /* 0x221 cooked_porkchop */ 320, /* 0x222 cooked_beef */ 364, /* 0x223 cooked_mutton */ -1, // t344 烤肉（mutton 1.8+ → -1）
    /* 0x224 redstone     */ 331, /* 0x225 saddle       */ 329, /* 0x226 name_tag     */ -1,  /* 0x227 enchanted_book */ -1, // t393 战利品（命名牌 1.6+ / 附魔书 1.4+ → -1）
    /* 0x228 feather      */ 288, /* 0x229 raw_chicken  */ 365, /* 0x22A cooked_chicken */ 366, /* 0x22B egg */ 344,
    /* 0x22C spawn_egg_chicken */ 383, // t398 鸡系列（feather / raw_chicken / cooked_chicken / egg / spawn_egg）
    /* 0x22D ink_sac */ 351, /* 0x22E spawn_egg_squid */ 383, // t399 鱿鱼系列（ink_sac / spawn_egg_squid）
};
static_assert(kMcMaterialIdCount == 47, "材料段 MC 映射表长度须 = [MaterialIdBase, SpawnEggSquidId] = 47");
} // namespace

// ── 匹配算法 ──

namespace {
// 取网格非空格的最小包围盒（含）。全空 → empty=true。
// 网格 n×n，行优先；返回 {x0,y0,x1,y1}（含端点）。全空时 empty=true（字段值未定义，调用方不读）。
struct BBox { int x0, y0, x1, y1; bool empty; };

BBox minBBox(const int *grid, int n)
{
    int x0 = n, y0 = n, x1 = -1, y1 = -1;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (grid[y * n + x] != 0) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    return { x0, y0, x1, y1, x1 < 0 };
}

// 无序匹配：输入与 pattern 的非空格多重集相同（位置无关）。
// **pattern 恒为 3×3=9 格行优先**（与 gridSize 无关：2×2 配方也用 9 格存，末位补 0）；
// 输入网格 inputN×inputN（2 或 3）。两种尺寸极小 → O(n²) 可接受。
// （旧实现误把 patN=gridSize 当 pattern 步长 → 2×2 配方只读前 4 格、漏计数 → code review 关键 bug）
bool shapelessEqual(const int *input, int inputN, const int *pattern)
{
    int inIds[9], inCnt[9], inKinds = 0;
    for (int i = 0; i < inputN * inputN; ++i) {
        const int id = input[i];
        if (id == 0) continue;
        int j = 0;
        for (; j < inKinds; ++j) if (inIds[j] == id) { ++inCnt[j]; break; }
        if (j == inKinds) { inIds[inKinds] = id; inCnt[inKinds] = 1; ++inKinds; }
    }
    int patIds[9], patCnt[9], patKinds = 0;
    for (int i = 0; i < 9; ++i) { // pattern 恒 9 格
        const int id = pattern[i];
        if (id == 0) continue;
        int j = 0;
        for (; j < patKinds; ++j) if (patIds[j] == id) { ++patCnt[j]; break; }
        if (j == patKinds) { patIds[patKinds] = id; patCnt[patKinds] = 1; ++patKinds; }
    }
    if (inKinds != patKinds) return false;
    for (int i = 0; i < inKinds; ++i) {
        int j = 0;
        for (; j < patKinds; ++j) if (patIds[j] == inIds[i]) {
            if (patCnt[j] != inCnt[i]) return false;
            break;
        }
        if (j == patKinds) return false; // input 有而 pattern 无此 id
    }
    return true;
}

// 有序匹配（MC 最小包围盒规则）：输入与 pattern 各自收缩到最小非空包围盒，尺寸相同且逐格 id 相同。
// 允许图案在网格内任意平移（包围盒对齐后比内容）；pattern 内的 0（如 T 形木镐的中部空格）要求
// 输入对应位也为 0。**pattern 恒以步长 3 读**（3×3 行优先），输入以 inputN 读。
bool shapedEqual(const int *input, int inputN, const int *pattern)
{
    const BBox ib = minBBox(input, inputN);
    const BBox pb = minBBox(pattern, 3); // pattern 恒 3×3
    if (ib.empty || pb.empty) return false;
    const int iw = ib.x1 - ib.x0 + 1, ih = ib.y1 - ib.y0 + 1;
    const int pw = pb.x1 - pb.x0 + 1, ph = pb.y1 - pb.y0 + 1;
    if (iw != pw || ih != ph) return false;
    for (int y = 0; y < ih; ++y) {
        for (int x = 0; x < iw; ++x) {
            const int ic = input[(ib.y0 + y) * inputN + (ib.x0 + x)];
            const int pc = pattern[(pb.y0 + y) * 3 + (pb.x0 + x)]; // 步长 3
            if (ic != pc) return false;
        }
    }
    return true;
}
} // namespace

const RecipeRegistry::Recipe *RecipeRegistry::match(const int *grid, int gridSize)
{
    if (!grid || gridSize < 2) return nullptr;
    bool any = false;
    for (int i = 0; i < gridSize * gridSize; ++i) if (grid[i] != 0) { any = true; break; }
    if (!any) return nullptr; // 空网格不匹配任何配方

    for (const Recipe &r : kRecipes) {
        if (r.gridSize > gridSize) continue; // 3×3 配方不在 2×2 输入里合
        if (r.shapeless) {
            if (shapelessEqual(grid, gridSize, r.pattern)) return &r;
        } else {
            if (shapedEqual(grid, gridSize, r.pattern)) return &r;
        }
    }
    return nullptr;
}

bool RecipeRegistry::canTake(const Recipe &r, int heldId, int heldCount, int maxStack)
{
    if (r.outputCount <= 0) return false;
    if (heldId == 0) return r.outputCount <= maxStack;             // 空光标 → 须一次放得下
    if (heldId != r.outputId) return false;                        // 异物光标 → 不合（MC：拿不下）
    return heldCount + r.outputCount <= maxStack;                  // 同物 → 累加不超上限
}

// t348 引擎材料段 id → MC Java 1.0.0 物品数字 id（资源包前置；见 kMcMaterialId 表注释）。越界（非材料段 /
//   超出 [MaterialIdBase, SpawnEggSquidId]）→ -1（资源包回退引擎自绘 MaterialIcon）。
int RecipeRegistry::mcMaterialId(int engineMaterialId)
{
    const int idx = engineMaterialId - MaterialIdBase;
    if (idx < 0 || idx >= kMcMaterialIdCount) return -1;
    return kMcMaterialId[idx];
}
