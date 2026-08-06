#include "armor.h"

#include <algorithm> // std::clamp / std::min (C++17)

// 单一护甲数据表（spec t345）。改护甲属性只改这里，全工程生效（Hotbar / 减伤 / 图标只读查）。
//
// 表行索引 == itemId - ArmorIdBase（连续 20 项 = 5 套 × 4 部位）；行序 = tier 主序 × piece 次序：
//   皮革 4 件（头/胸/腿/靴）→ 铁 4 件 → 铜 4 件 → 金 4 件 → 钻石 4 件。
// 与 recipe.h 的 ArmorId 枚举序严格一致（static_assert 钉死，防漂移）。
namespace {
constexpr ArmorRegistry::ArmorDef kArmors[] = {
    // ── 皮革（Leather；机制等价 MC 1.0 leather armor：护甲 1/3/2/1=7、耐久 55/80/75/65）──
    { ArmorRegistry::Leather, ArmorRegistry::Helmet,     1,  55, "leather_helmet",     "\xe7\x9a\xae\xe9\x9d\xa9\xe5\xa4\xb4\xe7\x9b\xb2" },     // 皮革头盔
    { ArmorRegistry::Leather, ArmorRegistry::Chestplate, 3,  80, "leather_chestplate", "\xe7\x9a\xae\xe9\x9d\xa9\xe8\x83\xb8\xe7\x94\xb2" },     // 皮革胸甲
    { ArmorRegistry::Leather, ArmorRegistry::Leggings,   2,  75, "leather_leggings",   "\xe7\x9a\xae\xe9\x9d\xa9\xe6\x8a\xa4\xe8\x85\xbf" },     // 皮革护腿
    { ArmorRegistry::Leather, ArmorRegistry::Boots,      1,  65, "leather_boots",      "\xe7\x9a\xae\xe9\x9d\xa9\xe9\x9d\xb4\xe5\xad\x90" },     // 皮革靴子
    // ── 铁（Iron；机制等价 MC 1.0 iron armor：护甲 2/6/5/2=15、耐久 165/240/225/195）──
    { ArmorRegistry::Iron,    ArmorRegistry::Helmet,     2, 165, "iron_helmet",        "\xe9\x93\x81\xe5\xa4\xb4\xe7\x9b\xb2" },                 // 铁头盔
    { ArmorRegistry::Iron,    ArmorRegistry::Chestplate, 6, 240, "iron_chestplate",    "\xe9\x93\x81\xe8\x83\xb8\xe7\x94\xb2" },                 // 铁胸甲
    { ArmorRegistry::Iron,    ArmorRegistry::Leggings,   5, 225, "iron_leggings",      "\xe9\x93\x81\xe6\x8a\xa4\xe8\x85\xbf" },                 // 铁护腿
    { ArmorRegistry::Iron,    ArmorRegistry::Boots,      2, 195, "iron_boots",         "\xe9\x93\x81\xe9\x9d\xb4\xe5\xad\x90" },                 // 铁靴子
    // ── 铜（Copper；MC 1.0 无铜护甲 → 自定：护甲 2/4/3/1=10、耐久 110/160/150/130，皮革与金之间）──
    { ArmorRegistry::Copper,  ArmorRegistry::Helmet,     2, 110, "copper_helmet",      "\xe9\x93\x9c\xe5\xa4\xb4\xe7\x9b\xb2" },                 // 铜头盔
    { ArmorRegistry::Copper,  ArmorRegistry::Chestplate, 4, 160, "copper_chestplate",  "\xe9\x93\x9c\xe8\x83\xb8\xe7\x94\xb2" },                 // 铜胸甲
    { ArmorRegistry::Copper,  ArmorRegistry::Leggings,   3, 150, "copper_leggings",    "\xe9\x93\x9c\xe6\x8a\xa4\xe8\x85\xbf" },                 // 铜护腿
    { ArmorRegistry::Copper,  ArmorRegistry::Boots,      1, 130, "copper_boots",       "\xe9\x93\x9c\xe9\x9d\xb4\xe5\xad\x90" },                 // 铜靴子
    // ── 金（Gold；机制等价 MC 1.0 gold armor：护甲 2/5/3/1=11、耐久 77/112/105/96）──
    { ArmorRegistry::Gold,    ArmorRegistry::Helmet,     2,  77, "gold_helmet",        "\xe9\x87\x91\xe5\xa4\xb4\xe7\x9b\xb2" },                 // 金头盔
    { ArmorRegistry::Gold,    ArmorRegistry::Chestplate, 5, 112, "gold_chestplate",    "\xe9\x87\x91\xe8\x83\xb8\xe7\x94\xb2" },                 // 金胸甲
    { ArmorRegistry::Gold,    ArmorRegistry::Leggings,   3, 105, "gold_leggings",      "\xe9\x87\x91\xe6\x8a\xa4\xe8\x85\xbf" },                 // 金护腿
    { ArmorRegistry::Gold,    ArmorRegistry::Boots,      1,  96, "gold_boots",         "\xe9\x87\x91\xe9\x9d\xb4\xe5\xad\x90" },                 // 金靴子
    // ── 钻石（Diamond；机制等价 MC 1.0 diamond armor：护甲 3/8/6/3=20、耐久 363/528/495/429）──
    { ArmorRegistry::Diamond, ArmorRegistry::Helmet,     3, 363, "diamond_helmet",     "\xe9\x92\xbb\xe7\x9f\xb3\xe5\xa4\xb4\xe7\x9b\xb2" },     // 钻石头盔
    { ArmorRegistry::Diamond, ArmorRegistry::Chestplate, 8, 528, "diamond_chestplate", "\xe9\x92\xbb\xe7\x9f\xb3\xe8\x83\xb8\xe7\x94\xb2" },     // 钻石胸甲
    { ArmorRegistry::Diamond, ArmorRegistry::Leggings,   6, 495, "diamond_leggings",   "\xe9\x92\xbb\xe7\x9f\xb3\xe6\x8a\xa4\xe8\x85\xbf" },     // 钻石护腿
    { ArmorRegistry::Diamond, ArmorRegistry::Boots,      3, 429, "diamond_boots",      "\xe9\x92\xbb\xe7\x9f\xb3\xe9\x9d\xb4\xe5\xad\x90" },     // 钻石靴子
};

// 越界 / 非护甲 id → nullptr（统一入口；调用方判空）。表行索引 = itemId - ArmorIdBase。
const ArmorRegistry::ArmorDef *armorDefAt(int itemId)
{
    if (itemId < int(RecipeRegistry::ArmorIdBase)) return nullptr;
    const int idx = itemId - int(RecipeRegistry::ArmorIdBase);
    if (idx < 0 || idx >= int(RecipeRegistry::ArmorCount)) return nullptr;
    return &kArmors[size_t(idx)];
}
} // namespace

// 编译期断言：表行序与 recipe.h ArmorId 枚举序严格一致（tier 主序 × piece 次序）。任一处漏 / 错位 → 编译失败。
// （5 套 × 4 部位 = 20 项；枚举 id 与表行偏移一一对应。）
static_assert(sizeof(kArmors) / sizeof(kArmors[0]) == size_t(RecipeRegistry::ArmorCount),
              "护甲表行数须 == ArmorCount（5 套 × 4 部位 = 20）");
static_assert(RecipeRegistry::LeatherHelmet    == RecipeRegistry::ArmorIdBase + 0,  "LeatherHelmet 序");
static_assert(RecipeRegistry::IronHelmet       == RecipeRegistry::ArmorIdBase + 4,  "IronHelmet 序");
static_assert(RecipeRegistry::CopperHelmet     == RecipeRegistry::ArmorIdBase + 8,  "CopperHelmet 序");
static_assert(RecipeRegistry::GoldHelmet       == RecipeRegistry::ArmorIdBase + 12, "GoldHelmet 序");
static_assert(RecipeRegistry::DiamondHelmet    == RecipeRegistry::ArmorIdBase + 16, "DiamondHelmet 序");
static_assert(RecipeRegistry::DiamondBoots     == RecipeRegistry::ArmorIdBase + 19, "DiamondBoots 序（末项）");

bool ArmorRegistry::isArmor(int itemId)
{
    return armorDefAt(itemId) != nullptr;
}

const ArmorRegistry::ArmorDef *ArmorRegistry::armor(int itemId)
{
    return armorDefAt(itemId);
}

int ArmorRegistry::armorPoints(int itemId)
{
    const ArmorDef *a = armorDefAt(itemId);
    return a ? a->armorPoints : 0;
}

int ArmorRegistry::maxDurability(int itemId)
{
    const ArmorDef *a = armorDefAt(itemId);
    return a ? a->maxDurability : 0;
}

int ArmorRegistry::piece(int itemId)
{
    const ArmorDef *a = armorDefAt(itemId);
    return a ? a->piece : -1;
}

int ArmorRegistry::tier(int itemId)
{
    const ArmorDef *a = armorDefAt(itemId);
    return a ? a->tier : -1;
}

QString ArmorRegistry::displayName(int itemId)
{
    const ArmorDef *a = armorDefAt(itemId);
    if (!a) return QString();
    return QString::fromUtf8(a->display);
}

float ArmorRegistry::armorReductionFactor(int totalArmor)
{
    // 总护甲值 → 减伤比例（每点 4%，上限 80%）。机制等价 MC 1.0 护甲减伤公式（armor * 4%, cap 80%）。
    //   totalArmor 钳到 [0, 20]：满 20（钻石整套）= 0.80 上限；超出不计（与护甲条 10 颗 × 2 点 = 20 上限一致）。
    if (totalArmor <= 0) return 0.0f;
    const int clamped = std::clamp(totalArmor, 0, 20);
    return std::min(0.80f, clamped * 0.04f); // 最终扣血的 round 由 caller（Main.qml JS Math.round）做。
}
