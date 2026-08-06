#ifndef ARMOR_H
#define ARMOR_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString

#include "recipe.h" // ArmorId 枚举（id 段定义在 RecipeRegistry，单一权威；Game 同层向下依赖）

// 护甲注册表（Game 层；机制等价 MC 1.0 护甲系统）。
//
// 与 ToolRegistry / BlockRegistry 同风格：纯静态数据表，无实例、无 Q_OBJECT。本表持有「护甲物品 →
// 护甲属性」（tier 材质档 / piece 部位 / armorPoints 护甲值 / maxDurability 最大耐久 / 名）。护甲 id 段
// 定义在 RecipeRegistry::ArmorId（recipe.h，spec t345「recipe.h（Armor ids）」），本类只读查它、不另存 id 表。
//
// 物品 id 分段（与 Hotbar::ItemStack 的 id 字段一致）：
//   方块段 0 .. Count-1；工具段 >= 0x100；材料段 >= 0x200；**护甲段 >= ArmorIdBase（0x300）**。
//   护甲不可堆叠（Hotbar::maxStackSize 对护甲段返 1，同工具段语义 —— 每件独立耐久）。护甲「是护甲」的
//   判定（isArmor）与「是材料」的判定（isMaterial）互斥 —— Hotbar::isMaterial 对护甲段返 false，避免护甲
//   被误路由成可堆叠材料；图标渲染仍走 MaterialIcon（MaterialIcon.qml 的 switch 含护甲段 case，因护甲段
//   id 与材料段一样「非方块非工具 → QML 自绘」）。
//
// 护甲值（armorPoints，机制等价 MC 1.0 各材质 / 部位护甲值；spec「MC 1.0 values per material/piece」）：
//   每部位一行（头盔 / 胸甲 / 护腿 / 靴子），括号内为整套合计：
//     皮革 Leather  : 1 / 3 / 2 / 1   ( 7)   —— MC 1.0 leather
//     铁   Iron     : 2 / 6 / 5 / 2   (15)   —— MC 1.0 iron
//     铜   Copper   : 2 / 4 / 3 / 1   (10)   —— 自定（MC 1.0 无铜护甲；取皮革与铁之间，量级合理）
//     金   Gold     : 2 / 5 / 3 / 1   (11)   —— MC 1.0 gold
//     钻石 Diamond : 3 / 8 / 6 / 3   (20)   —— MC 1.0 diamond（满 20 = 护甲条 10 颗全亮 = 80% 减伤上限）
//
// 耐久（maxDurability，机制等价 MC 1.0 护甲耐久；spec「DURABILITY（degrades on hits）」）：
//   每部位一行（头盔 / 胸甲 / 护腿 / 靴子）。受击时每件 -1（Hotbar::damageArmor），归零即破损（槽位清空）。
//     皮革 :  55 /  80 /  75 /  65    铜 : 110 / 160 / 150 / 130（自定，皮革与金之间）
//     金   :  77 / 112 / 105 /  96    铁 : 165 / 240 / 225 / 195
//     钻石 : 363 / 528 / 495 / 429
//
// 减伤公式（spec「armor reduces incoming damage by its armor value」；机制等价 MC 1.0 护甲减伤）：
//   总护甲值 totalArmor（4 件之和，0..20）→ 减伤比例 = min(0.80, totalArmor * 0.04)（每点 4%，上限 80%）。
//   finalDamage = max(1, round(incoming * (1 - ratio)))（至少 1 点穿透，机制等价 MC「护甲不彻底免伤」）。
//   由 Main.qml 在 takeDamage 路由前调 hotbar.totalArmorPoints() + 本类 armorReductionFactor() 算最终扣血。
//
// 分层（PLAN §2）：本层属 Game，只依赖同层 RecipeRegistry（id 段），**不**依赖
// Renderer/Physics/QtQuick3D。依赖只向下（RecipeRegistry 亦属 Game，且其 id 段是跨层契约锚点）。
//
// §4 法律 + §9：护甲名用通用词（皮革头盔 / 铁胸甲 / 钻石靴子 ——「皮革 / 铁 / 铜 / 金 / 钻石」为通用材质名，
// 「头盔 / 胸甲 / 护腿 / 靴子」为通用部位名，非 MC 专名）；零 MC 专有名词。护甲图标在 QML 呈现层自绘原创
// （MaterialIcon.qml 的 Canvas 像素图，§9 override (a)；非 MC GUI PNG）。
class ArmorRegistry
{
public:
    // 护甲材质档（决定 armorPoints / maxDurability 量级；与 ArmorId 段内的 5 套对应）。
    enum ArmorTier : int {
        Leather  = 0,
        Iron     = 1,
        Copper   = 2,
        Gold     = 3,
        Diamond  = 4,
    };
    // 护甲部位（与生存背包左上 4 护甲槽纵向序一致：0=头盔 / 1=胸甲 / 2=护腿 / 3=靴子）。
    // 装备槽点击时据本枚举校验「持物部位 == 槽位部位」（MC 行为：头盔只能装头盔槽，不可装到胸甲槽）。
    enum ArmorPiece : int {
        Helmet     = 0,
        Chestplate = 1,
        Leggings   = 2,
        Boots      = 3,
    };

    // 护甲定义。表行索引 == itemId - ArmorIdBase（连续）；详见 armor.cpp kArmors。
    struct ArmorDef {
        int tier;            // ArmorTier（皮革 / 铁 / 铜 / 金 / 钻石）
        int piece;           // ArmorPiece（头盔 / 胸甲 / 护腿 / 靴子）
        int armorPoints;     // 护甲值（装备时贡献到 totalArmor；驱动护甲条 + 减伤比例）
        int maxDurability;   // 最大耐久（受击 -1；归零破损）。恒 > 0。
        const char *name;    // 内部 / 调试用名（通用词，英文标识符；非面向用户）
        const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词）
    };

    // 护甲判定（id 在护甲段 [ArmorIdBase, ArmorIdBase+ArmorCount) 内）。越段 / 方块 / 工具 / 材料 → false。
    static bool isArmor(int itemId);

    // 取护甲定义（tier / piece / armorPoints / maxDurability / 名）。非护甲 id → nullptr。
    static const ArmorDef *armor(int itemId);

    // 护甲值（装备时贡献的护甲点数）。非护甲 → 0。Hotbar::totalArmorPoints 累加 4 装备槽调用本方法。
    static int armorPoints(int itemId);
    // 最大耐久（受击上限）。非护甲 → 0。Hotbar 初始化新护甲实例耐久 + tooltip 显「cur/max」用。
    static int maxDurability(int itemId);
    // 部位（Helmet/Chestplate/Leggings/Boots）。非护甲 → -1。装备槽点击校验「部位匹配」用。
    static int piece(int itemId);
    // 材质档（Leather/Iron/Copper/Gold/Diamond）。非护甲 → -1。MaterialIcon 据此选配色用。
    static int tier(int itemId);

    // 用户可见中文显示名（护甲段；PLAN §9 override (b) 通用词）。
    //   皮革头盔 / 皮革胸甲 / 皮革护腿 / 皮革靴子；铁 / 铜 / 金 / 钻石 同理。非护甲 → 空串。
    static QString displayName(int itemId);

    // 减伤比例（0.0..0.80）：totalArmor 点 → min(0.80, totalArmor*0.04)。供 Main.qml 在 takeDamage 前
    //   把原始伤害乘以 (1 - armorReductionFactor(totalArmor)) 得最终扣血（机制等价 MC 1.0 护甲减伤）。
    //   totalArmor 钳到 [0, 20]（满 20 = 80% 减伤上限；超出不计 —— 与护甲条 10 颗上限一致）。
    static float armorReductionFactor(int totalArmor);

private:
    ArmorRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // ARMOR_H
