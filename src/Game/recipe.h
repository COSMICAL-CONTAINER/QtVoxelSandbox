#ifndef RECIPE_H
#define RECIPE_H

#include <QtGlobal> // quint8

#include "blockregistry.h" // 方块 id（原料 / 产物方块段）
#include "toolregistry.h"  // 工具段 id（产物：木镐等；Game 同层，向下依赖 Core）

// 合成配方注册表 + 匹配算法（Game 层；机制等价 MC 1.0 合成）。
//
// 与 BlockRegistry / ToolRegistry 同风格：纯静态数据表，无实例、无 Q_OBJECT。配方表是合成系统
// 的单一权威（UI 呈现层 SurvivalInventory.qml / CraftingTableUI.qml 与未来的冶炼系统都**只读**
// 查它，不各持副本 —— PLAN §2：数据单一）。输入 / 产出物品 id 复用 BlockRegistry（方块段）+
// ToolRegistry（工具段）；本类不另存物品表。
//
// 配方模型（spec t50）：
//   - gridSize：合成格尺寸（2 = 2×2 背包合成栏；3 = 3×3 工作台）。**关键**：2×2 配方（planks /
//     stick / craftingTable）也能在 3×3 工作台里合成；3×3 配方（woodPickaxe）只能在工作台。
//     匹配算法据此自适应（见 match()）。
//   - shapeless：true = 无序（只看原料多重集，位置任意）；false = 有序（位置敏感，按 MC「最小
//     包围盒」规则：输入与配方各自收缩到非空格的最小矩形，逐格比 id）。
//   - pattern[9]：行优先（[0..2]=顶行、[3..5]=中行、[6..8]=底行）；0=空格、>0=原料 id。
//     2×2 配方仅用 [0..3]（顶行 [0,1]、底行 [3,4] —— 注意 2×2 视为左上 2×2 子矩阵）。
//   - outputId / outputCount：一次合成产出物品 id 与数量。
//
// 数量处理（spec「MC 式数量」）：
//   - 点结果槽 → 取 outputCount 件到光标；输入槽每原料格 consume 1 份（count-1，归 0 清 id），
//     **不清空整槽**（剩余留槽，可继续合成）。这与 MC 一致（「2 原木 → 一次 4 板、原料 -1」可连点）。
//   - 每次合成的「原料用量」恒为 1（consumeCount 字段保留扩展位，当前配方全 1）。
//
// 分层（PLAN §2）：本层属 Game，只依赖 Core（BlockRegistry）+ 同层 ToolRegistry，**不**依赖
// Renderer/Physics/QtQuick3D。依赖只向下。合成匹配是纯函数（输入网格 → 匹配配方），无副作用；
// 实际消耗 / 产出由 UI 层（SurvivalInventory.qml / CraftingTableUI.qml）在点结果槽时执行（写本地
// craft 栈 + hotbar 光标手持栈），本类只负责「能不能合 / 合出什么」判定。
//
// §4 法律 + §9：配方名 / 产物名用通用词（木板 / 木棒 / 工作台 / 木镐）；零 MC 专有名词。
class RecipeRegistry
{
public:
    // 合成格尺寸（决定配方在哪种合成台可用：2×2 背包栏 / 3×3 工作台）。
    enum GridSize : int {
        Inventory2x2 = 2,
        Table3x3     = 3,
    };

    // 材料段基址（合成产物中「既非方块也非工具、可堆叠」的材料物品 id 区间，>= 0x200）。
    // 当前成员：木棒(0x200) / 煤炭(0x201) / 铁原矿(0x202) / 铁锭(0x203)。与 Hotbar 的材料段判定
    // （id >= 0x200）同源；改一处须同步另一处（.cpp 有 static_assert 对齐）。
    // 矿石→材料掉落由 BlockRegistry::BlockDef.dropId 引用（Core 层不依赖 Game，故 blockregistry.cpp
    // 用字面量 0x201/0x202；本处给字面量命名，全工程通过 RecipeRegistry::CoalId 等引用）。
    static constexpr int MaterialIdBase = 0x200;
    static constexpr int StickId        = 0x200; // 木棒：4 件产出（stick 配方）+ 木镐配方原料
    static constexpr int CoalId         = 0x201; // 煤炭：煤矿石挖掘掉落（BlockRegistry::CoalOre.dropId）；可作燃料 / 未来冶炼原料
    static constexpr int IronOreDropId  = 0x202; // 铁原矿：铁矿石挖掘掉落（BlockRegistry::IronOre.dropId）；熔炉冶炼为铁锭
    static constexpr int IronIngotId    = 0x203; // 铁锭：铁原矿冶炼产物；石镐 / 铁镐配方原料
    // t87 冶炼产物（SmeltingRegistry 用；与 Coal/铁系列同属材料段 >= 0x200，可堆叠 64）：
    static constexpr int GlassId        = 0x204; // 玻璃：沙子冶炼产物（spec 可选项）
    static constexpr int CharcoalId     = 0x205; // 木炭：原木冶炼产物（spec 可选项；与煤等价燃料）
    // t174 铁桶（功能性物品，复用材料段 id 区间；但**不可堆叠** —— Hotbar::maxStackSize 对这两个 id 返回 1）。
    //   机制等价 MC 1.0 铁桶：空桶可合成（3 铁锭 V 形）+ 右键舀水（命中水 → 装水铁桶 + 水源消失）/
    //   倒水（命中面相邻空气格 → 放置水源 + 桶变空）。属材料段（id >= 0x200）便于 isMaterial 分流到 MaterialIcon
    //   自绘桶图标（BucketEmptyId 画空桶 / WaterBucketId 画装水桶），但 maxStack=1 与工具段同语义（单件）。
    //   与 ToolRegistry::isTool（[0x100,0x103)）互斥 —— 桶非工具，不影响挖掘速度 / 掉落判定。
    static constexpr int BucketEmptyId  = 0x206; // 铁桶（空）：3 铁锭 V 形合成；右键水舀水 → WaterBucketId
    static constexpr int WaterBucketId  = 0x207; // 装水铁桶：空桶舀水得；右键倒出水水源 → BucketEmptyId
    // t235 小麦种子：材料段 0x208。挖草丛（TallGrass）掉落（BlockRegistry::TallGrass.dropId=0x208）；
    //   机制等价 MC 1.0「挖草丛掉小麦种子」。可堆叠 64；创造调色板可取用。**种植**（右键耕地→种小麦作物）
    //   归 t236（WheatCrop 方块 + 生长阶段）；本任务仅注册物品（可持 / 可掉 / 创造可取）。
    //   t237：收割小麦作物（WheatCrop）时也掉落种子（未成熟作物 / 成熟作物的额外种子）。
    static constexpr int SeedId         = 0x208; // 小麦种子：挖草丛掉落；种植 → 小麦作物（t236）；收割返种（t237）
    // t237 小麦物品：材料段 0x209。收割**成熟**小麦作物（WheatCrop，state==WheatCropStageMax）掉落
    //   （机制等价 MC 1.0「成熟小麦收割掉小麦物品」）。可堆叠 64；创造调色板补全归 t244。
    //   小麦物品是非方块 / 非工具材料（同种子 / 木棒 / 煤），走 MaterialIcon 自绘图标 + 本地通用名。
    //   下游消费：t238 面包配方（3 小麦 → 1 面包；面包为后续任务，本任务仅注册小麦物品使其可持 / 可掉 / 可堆叠）。
    static constexpr int WheatId        = 0x209; // 小麦物品：收割成熟小麦作物掉落；面包原料（t238）
    // t238 面包：材料段 0x20A。3 小麦合成（仅工作台 3×3 横排）；右键食 → 恢复饱食度（PlayerController
    //   useItem 分支 +5 hunger；机制等价 MC 1.0 面包 +5 hunger / 2.5 鼓腿）。可堆叠 64；非方块（材料段）
    //   → 右键不放置，走「食用」分支（同桶 / 种子：在 selectedBlock Air 守卫之前分流）。MaterialIcon 自绘
    //   面包块图标。创造调色板补全归 t244；本任务仅注册使其可合 / 可食 / 可堆叠。
    static constexpr int BreadId        = 0x20A; // 面包：3 小麦合成；右键食 → +5 饥饿（t238）
    // t242 mob 死亡掉落（材料段 0x20B..0x20E；机制等价 MC 1.0 被动生物掉落物；非方块、可堆叠 64）：
    //   杀猪掉生猪排 / 杀牛掉生牛肉 + 皮革 / 杀羊掉羊毛。EntityManager mobDied 信号 → Main.qml 据本 id 调
    //   ItemEntityManager.spawnItem 生成掉落实体（同 spawnItem 模式；PLAN §2 分层：Entities 层发语义事件、
    //   呈现层只消费）。MaterialIcon 自绘图标 + 进创造调色板均由 t244 完成（Hotbar::creativeMaterials 拾取即满栈 64，
    //   供测试 / 装饰直接取用；生存时由 mob 死亡掉落 / 拾取获得）。名称 / 模型全原创（§9 区隔，零 MC 资产 / 专名）。
    static constexpr int RawPorkchopId  = 0x20B; // 生猪排：杀猪掉落（机制等价 MC 1.0 raw porkchop）
    static constexpr int RawBeefId      = 0x20C; // 生牛肉：杀牛掉落（机制等价 MC 1.0 raw beef）
    static constexpr int LeatherId      = 0x20D; // 皮革：杀牛掉落（机制等价 MC 1.0 leather）
    static constexpr int WoolId         = 0x20E; // 羊毛：杀羊掉落（机制等价 MC 1.0 wool）
    // t243 生物蛋（spawn eggs）：材料段 0x20F..0x211。创造模式物品，右键地面 → EntityManager::spawnMobTyped
    //   生成对应被动生物（猪 / 牛 / 羊；机制等价 MC 1.0 spawn egg）。三种蛋各映射一种 mobType（与
    //   EntityManager::MobType MobPig/Cow/Sheep 同值）；PlayerController::placeBlock 据本 id 分流走「使用」分支
    //   （同桶 / 锄 / 种子：非方块材料段 → 在 selectedBlock Air 守卫之前分流，不走方块放置路径）。可堆叠 64
    //   （机制等价 MC 1.0 spawn egg maxStack 64；走材料段默认 maxStack=64，无需特例）。生存消耗 1 蛋 / 创造不耗
    //   （同种子 / 面包模式）。MaterialIcon 自绘蛋形图标（壳 + mob 配色斑点）；创造调色板补全归 t244。名称 /
    //   图标全原创（§9 区隔，零 MC 资产 / 专名）。
    static constexpr int SpawnEggPigId   = 0x20F; // 生物蛋（猪）：右键地面 → 生成猪（MobPig）
    static constexpr int SpawnEggCowId   = 0x210; // 生物蛋（牛）：右键地面 → 生成牛（MobCow）
    static constexpr int SpawnEggSheepId = 0x211; // 生物蛋（羊）：右键地面 → 生成羊（MobSheep）

    // 配方定义（每条一行；单一权威）。改配方任何属性只改 kRecipes 一行，全工程生效。
    struct Recipe {
        int gridSize;        // 2 或 3（合成台尺寸；2×2 配方亦可在 3×3 工作台合成）
        bool shapeless;      // true = 无序（多重集匹配）；false = 有序（最小包围盒逐格匹配）
        int pattern[9];      // 行优先；0=空格 / >0=原料 id；2×2 配方仅 [0,1,3,4] 有效
        int outputId;        // 产物物品 id（方块段或工具段）
        int outputCount;     // 一次合成产出数量
        int consumeCount;    // 每原料格每次合成消耗数（当前全 1；留扩展位）
        const char *name;    // 内部 / 调试用名（通用词；非面向用户）
    };

    // 在给定输入网格中找匹配配方。
    //   grid     = 行优先 id 数组（0=空格 / >0=物品 id）；
    //   gridSize = 输入合成台尺寸（2 = 背包 2×2 / 3 = 工作台 3×3）。
    // 返回首个匹配的 Recipe*（无匹配 → nullptr）。匹配规则：
    //   - 仅尝试 gridSize <= 输入尺寸的配方（2×2 配方在 3×3 输入里也能合；3×3 配方在 2×2 输入里不能）。
    //   - shapeless：输入非空格的多重集 == 配方 pattern 非空格的多重集（与位置无关）。
    //   - shaped：输入收缩到最小非空包围盒、配方 pattern（在自身 gridSize 上）收缩到最小非空包围盒；
    //     二者包围盒尺寸相同且逐格 id 相同 → 匹配（MC「最小包围盒」规则，允许图案在网格内任意平移）。
    // 空输入（全 0）→ 恒 nullptr（无配方匹配空网格）。
    static const Recipe *match(const int *grid, int gridSize);

    // 产物是否能放入光标手持栈（spec「MC 式数量」前置判定，供 UI 决定是否合成）：
    // 光标空 OR（同 id 且 heldCount + outputCount <= maxStack）→ true。maxStack 走 Hotbar::maxStackSize
    // （方块 64 / 工具 1）；此处由 caller 传 maxStack 解耦（本类不依赖 Hotbar）。
    static bool canTake(const Recipe &r, int heldId, int heldCount, int maxStack);

private:
    RecipeRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // RECIPE_H
