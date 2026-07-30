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
    // craftingTable：4 木板 → 1 工作台（无序；2×2 全木板）
    { int(RecipeRegistry::Inventory2x2), true,
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
    // furnace：8 圆石围圈（中空）→ 1 熔炉（有序 3×3，仅工作台）。机制等价 MC 熔炉配方；
    // 满包围盒 3×3（中心空）→ 输入也须 3×3 围圈（中心为空格），9 圆石（实心）不匹配。
    { int(RecipeRegistry::Table3x3), false,
      { int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble),
        int(BlockRegistry::Cobble), 0,                         int(BlockRegistry::Cobble),
        int(BlockRegistry::Cobble), int(BlockRegistry::Cobble), int(BlockRegistry::Cobble) },
      int(BlockRegistry::Furnace), 1, 1, "furnace" },
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
