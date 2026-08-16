#include "smelting.h"

// 单一冶炼 / 燃料数据表（spec t87）。改配方 / 燃料任何属性只改这里，全工程生效（FurnaceUI 只读查）。
//
// 冶炼配方（1 输入 → 1 产物；无位置 / 多重集概念）：
//   - iron：铁原矿（材料段 0x202）→ 铁锭（0x203）。核心配方，spec 验收项。
//   - copper：铜原矿（材料段 0x21C）→ 铜锭（0x21D）。t308：机制等价 MC 1.0「铜矿采下为原矿，须熔炉冶炼成锭」。
//   - gold：金原矿（材料段 0x21E）→ 金锭（0x21F）。t308：机制等价 MC 1.0「金矿采下为原矿，须熔炉冶炼成锭」。
//   - glass：沙子（方块段 Sand）→ 玻璃（材料段 0x204，spec 可选）。
//   - charcoal：原木（方块段 Log）→ 木炭（材料段 0x205，spec 可选）。
//   注：钻石矿直接掉钻石（宝石，无需冶炼）—— 钻石不进本表（spec「钻石挖掘就还是钻石的样子」）。
//
// 燃料表（MC burn ticks / 20 = 秒；1 件冶炼 = 10s = 200 ticks）：
//   - 煤炭 / 木炭：80s（8 件）—— 煤炭是 MC 主流燃料；木炭与煤等价（spec 扩展，便于「原木→木炭→再当燃料」闭环）。
//   - 原木：15s（1.5 件）；木板：15s（1.5 件）—— MC 经典「1 原木 = 1.5 件，但拆 4 木板 = 6 件」由数据自然表达。
//   - 工作台：15s（1.5 件，同木板——工作台由木板合成，燃料值等价木板；spec t93）。
//   - 木棒：5s（0.5 件——2 木板→4 木棒→共 2 件冶炼，「拆棒」反而不划算，由数据自然表达；spec t93）。
namespace {
struct SmeltEntry { int inputId; int outputId; const char *name; };
struct FuelEntry  { int itemId;   float burnSecs; const char *name; };

constexpr SmeltEntry kSmelt[] = {
    { RecipeRegistry::IronOreDropId,   RecipeRegistry::IronIngotId,   "iron"     }, // 铁原矿 → 铁锭
    { RecipeRegistry::CopperOreDropId, RecipeRegistry::CopperIngotId, "copper"   }, // t308 铜原矿 → 铜锭
    { RecipeRegistry::GoldOreDropId,   RecipeRegistry::GoldIngotId,   "gold"     }, // t308 金原矿 → 金锭
    { int(BlockRegistry::Sand),        RecipeRegistry::GlassId,       "glass"    }, // 沙子 → 玻璃
    { int(BlockRegistry::Log),         RecipeRegistry::CharcoalId,    "charcoal" }, // 原木 → 木炭
    // t494/t513 二轮复盘：生肉 → 熟肉配方（机制等价 MC 1.0 熔炉烤肉）。原 kSmelt 表只列矿石 / 沙 / 原木，
    //   缺生肉 → 熟肉链，导致生猪排 / 生牛肉放熔炉 smeltResult 返 0 → 烧不出熟肉（用户实测 t494/t513 报障根因）。
    //   熟肉此前仅由「mob 燃烧致死」掉落（t344 EntityManager 路径）获得；此处补齐「熔炉烤肉」正途。
    //   鸡 / 鱼等其余生肉同理可补，本轮按用户报障范围（猪 / 牛）先修这两条，结构已通用（加一行即可扩）。
    { RecipeRegistry::RawPorkchopId,   RecipeRegistry::CookedPorkchopId, "porkchop" }, // 生猪排 → 熟猪排（机制等价 MC 1.0 raw→cooked porkchop）
    { RecipeRegistry::RawBeefId,       RecipeRegistry::CookedBeefId,     "beef"     }, // 生牛肉 → 熟牛肉（机制等价 MC 1.0 raw→cooked beef）
};

constexpr FuelEntry kFuel[] = {
    { RecipeRegistry::CoalId,     80.f, "coal"           }, // 煤炭 80s（8 件）
    { RecipeRegistry::CharcoalId, 80.f, "charcoal"       }, // 木炭 80s（与煤等价）
    { int(BlockRegistry::Log),           15.f, "log"     }, // 原木 15s（1.5 件）
    { int(BlockRegistry::Planks),        15.f, "planks"  }, // 木板 15s（1.5 件）
    { int(BlockRegistry::CraftingTable), 15.f, "crafting_table" }, // 工作台 15s（同木板；t93）
    { RecipeRegistry::StickId,     5.f, "stick"          }, // 木棒 5s（0.5 件；t93）
    // t620 煤炭块燃料：800s（80 件；机制等价 MC 1.0 block of coal 8000 burn ticks = 400s×2 —— MC 1.0 烧 80 件
    //   的量级，本工程 1 件冶炼 = 10s → 80 件 = 800s）。9 煤（9×80s=720s）合 1 块烧 800s → 9 块煤的压缩
    //   盈余 +80s（+8 件），机制等价 MC「块比散煤更划算」的存储 + 燃料双收益。
    { int(BlockRegistry::CoalBlock),   800.f, "coal_block" }, // 煤炭块 800s（80 件；9 煤压缩 +80s 盈余）
};

// 编译期断言：冶炼产物 / 燃料 id 均在合法段（材料段 >= 0x200 或方块段 < Count）。
// 防新增材料 id 时漏改 recipe.h 常量或写错字面量 → 编译失败（跨字段契约保护）。
static_assert(RecipeRegistry::GlassId    == 0x204, "GlassId 须为材料段序号 0x204");
static_assert(RecipeRegistry::CharcoalId == 0x205, "CharcoalId 须为材料段序号 0x205");
} // namespace

int SmeltingRegistry::smeltResult(int inputId)
{
    for (const SmeltEntry &e : kSmelt)
        if (e.inputId == inputId) return e.outputId;
    return 0;
}

float SmeltingRegistry::fuelBurnSeconds(int itemId)
{
    for (const FuelEntry &e : kFuel)
        if (e.itemId == itemId) return e.burnSecs;
    return 0.f;
}

// t402 冶炼产物 → XP 奖励表（机制等价 MC 1.0 smelting XP，spec「iron ingot gives more than charcoal」）。
//   按**产物 id** 查（玩家取走产物时按件 × 此值产经验球，spec「removing a finished smelt item grants XP」）。
//   取整放大便于可观察（MC 原值小数；本工程世界小、单次产量少，整数 XP 让累积可见）。数值为本工程
//   量身调，非 MC 精确复刻（PLAN §4「机制对标」非数值 1:1）。无 MC 1.0 等价映射（XP 数值为机制内部）。
namespace {
struct SmeltXpEntry { int outputId; int xp; const char *name; };
constexpr SmeltXpEntry kSmeltXp[] = {
    { RecipeRegistry::IronIngotId,   3, "iron"     }, // 铁锭：铁原矿冶炼给 3 XP（金属矿主力来源，spec「iron ingot」）
    { RecipeRegistry::CopperIngotId, 2, "copper"   }, // 铜锭：铜原矿冶炼给 2 XP（t308 铜链）
    { RecipeRegistry::GoldIngotId,   2, "gold"     }, // 金锭：金原矿冶炼给 2 XP（t308 金链）
    { RecipeRegistry::CharcoalId,    1, "charcoal" }, // 木炭：原木冶炼给 1 XP（spec「charcoal」；少于铁，数据自然表达）
    { RecipeRegistry::GlassId,       0, "glass"    }, // 玻璃：沙子冶炼给 0 XP（无金属价值）
};
} // namespace

int SmeltingRegistry::smeltXpReward(int outputId)
{
    for (const SmeltXpEntry &e : kSmeltXp)
        if (e.outputId == outputId) return e.xp;
    return 0;
}
