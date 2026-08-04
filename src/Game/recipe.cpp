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
