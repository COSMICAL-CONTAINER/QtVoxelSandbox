#ifndef SMELTING_H
#define SMELTING_H

#include <QtGlobal> // qreal / qint32

#include "blockregistry.h" // 方块段 id（Sand/Log/Planks：冶炼原料 / 燃料）
#include "recipe.h"        // 材料段 id（IronOreDrop/Coal/IronIngot/Glass/Charcoal）

// 冶炼 / 燃料注册表（Game 层；机制等价 MC 1.0 熔炉）。
//
// 与 BlockRegistry / RecipeRegistry / ToolRegistry 同风格：纯静态数据表，无实例、无 Q_OBJECT。
// 冶炼配方表与燃料表是熔炉系统的单一权威（FurnaceUI.qml 只读查，不另持副本 —— PLAN §2：数据单一）。
// 输入 / 产出 / 燃料物品 id 复用 BlockRegistry（方块段）+ RecipeRegistry（材料段）；本类不另存物品表。
//
// 配方模型（spec t87）：
//   - smeltResult(inputId)：输入物品 → 冶炼产物 id（0 = 不可冶炼）。MC 熔炉配方是「1 输入 → 1 产物」，
//     无有序 / 多重集概念（每次消耗 1 件输入产出 1 件产物）。
//   - fuelBurnSeconds(itemId)：燃料 → 燃烧秒数（0 = 不可燃）。MC 燃料按 burn ticks 计，此处换算成秒
//     （200 ticks = 10s = 1 件冶炼时间）。
//
// 冶炼节律（spec「冶炼 tick：燃料燃烧→累积热量→输入转输出」）：
//   - 单次冶炼耗时 kSmeltSecs = 10s（MC 1.0 标准 200 ticks）。
//   - 燃料燃烧期间累积 smeltProgress；满 kSmeltSecs → 消耗 1 输入产出 1 输出，progress 归零（或留余）。
//   - 燃料未燃且（输入可冶炼 + 输出有空位 + 燃料槽有燃料）→ 点燃 1 件燃料（consume fuel，置 burnRemain）。
//   - 输入空 / 输出满 / 不可冶炼 → 不点燃、不累积（progress 保留但不推进，机制等价 MC）。
//
// 分层（PLAN §2）：本层属 Game，只依赖 Core（BlockRegistry）+ 同层 RecipeRegistry（材料段 id），
// **不**依赖 Renderer/Physics/QtQuick3D。依赖只向下。判定是纯函数（输入 id → 产物 / 燃烧秒数），
// 无副作用；实际消耗 / 产出由 FurnaceUI 在 tick 内执行（写本地槽 + 光标手持栈），本类只负责
// 「能不能冶 / 冶出什么 / 烧多久」判定。
//
// §4 法律 + §9：产物名用通用词（铁锭 / 玻璃 / 木炭）；零 MC 专有名词。
class SmeltingRegistry
{
public:
    // 输入物品 id → 冶炼产物 id（0 = 不可冶炼）。机制等价 MC 熔炉配方：
    //   - 铁原矿 → 铁锭（核心，spec t87 验收项「放铁原矿+煤→冶炼出铁锭」）。
    //   - 铜原矿 → 铜锭（t308；机制等价 MC 1.0「铜矿采下为原矿，须熔炉冶炼成锭」）。
    //   - 金原矿 → 金锭（t308；机制等价 MC 1.0「金矿采下为原矿，须熔炉冶炼成锭」）。
    //   - 沙子 → 玻璃（spec 可选；玻璃为材料段新物品 GlassId=0x204）。
    //   - 原木 → 木炭（spec 可选；木炭为材料段新物品 CharcoalId=0x205）。
    //   注：钻石矿直接掉钻石（宝石无需冶炼）→ 钻石不进本表（spec「钻石挖掘就还是钻石的样子」）。
    static int smeltResult(int inputId);

    // 燃料物品 id → 燃烧秒数（0 = 不可燃）。机制等价 MC 燃料表（burn ticks / 20 = 秒）：
    //   - 煤炭 80s（8 件冶炼）/ 木炭 80s（与煤等价燃料，spec 扩展）。
    //   - 原木 15s / 木板 15s / 工作台 15s（各 1.5 件；工作台同木板燃料值；MC 经典「原木先拆木板再烧更划算」由数据自然表达）。
    //   - 木棒 5s（0.5 件；spec t93——「2 木板拆 4 木棒」反而不划算，由数据自然表达）。
    static float fuelBurnSeconds(int itemId);

    // 单次冶炼耗时（秒）。MC 1.0 标准 200 ticks = 10s。
    static constexpr float kSmeltSecs = 10.f;

private:
    SmeltingRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // SMELTING_H
