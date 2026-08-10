#ifndef ENCHANTREGISTRY_H
#define ENCHANTREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString
#include <QVariantList>

#include "blockregistry.h" // ToolType（武器/工具判定借 ToolType.Sword/Pickaxe/...）
#include "toolregistry.h"  // 工具段 id → ToolDef（type/tier）
#include "armor.h"         // 护甲段判定（ArmorRegistry::isArmor）

// 附魔注册表 + 附魔选择逻辑（Game 层；机制等价 MC 1.0 附魔系统）。
//
// 与 ToolRegistry / ArmorRegistry 同风格：纯静态数据表，无实例、无 Q_OBJECT。本表持有「附魔 →
// 附魔属性」（类别适用面 / 最大等级 / 权重 / 名）。附魔 id 是本表自有的小整数段（1..14，见 EnchantId 枚举），
// **不**进物品 id 段（附魔是物品的元数据，附在 ItemStack.enchants[4] 上，非独立物品）。
//
// 物品类别（机制等价 MC 1.0「附魔按物品类型分流」）：
//   - Weapon（剑）：锐锋 / 亡灵杀手 / 节肢克星 / 击退 / 燃焰。
//   - Tool（镐 / 锄 / 斧 / 铲）：效率 / 精准采集 / 时运。
//   - Armor（头盔 / 胸甲 / 护腿 / 靴子）：保护 / 火焰保护 / 摔落保护 / 弹射物保护 / 水上亲和。
//   - 耐久（Unbreaking）适用**全部三类**（武器 / 工具 / 护甲均可附），用 appliesToMask 位掩码表达。
//   弓 / 剪刀 / 钓鱼竿的专属附魔（力量 / 无限 / 经验修补 …）属另一套机制，本任务（t475）不做。
//
// 附魔选择（机制等价 MC 1.0 附魔台「按 offered level + 随机种子从适用池加权抽 1–3 个附魔 + 各自等级」）：
//   selectEnchants(category, offeredLevel, seed) 给定物品类别 + 提供等级（1..30，来自 t474 书架加成）+
//   每槽随机种子，返回 [{id, level}, ...]（已剔除互斥冲突，如锐锋 / 亡灵杀手 / 节肢克星三选一）。
//   offeredLevel 越高 → 附魔数越多（1→3）+ 单附魔等级越高（趋近 maxLevel）。纯函数（无副作用 / 无 IO），
//   附魔台 UI 点选项槽时调 → 把结果写入目标物品 ItemStack.enchants（Hotbar::enchantSelected）。
//
// §4 法律 + §9：附魔名用**通用描述词**（锐锋 / 亡灵杀手 / 节肢克星 / 击退 / 燃焰 / 效率 / 精准采集 / 时运 /
//   耐久 / 保护 / 火焰保护 / 摔落保护 / 弹射物保护 / 水上亲和）—— 非 MC 专名（sharpness / Smite / … 仅为
//   机制等价参考，代码 / 用户可见字串绝不用原名）。机制对齐 MC Java 1.0.0，名词 / 数值原创。
//
// 分层（PLAN §2）：本层属 Game，只依赖 Core（BlockRegistry::ToolType）+ 同层 ToolRegistry / ArmorRegistry
// （判定物品类别），**不**依赖 Renderer/Physics/QtQuick3D。依赖只向下。
class EnchantRegistry
{
public:
    // 物品类别（决定哪些附魔适用）。用位掩码表达「适用面」（耐久适用多类）。
    enum Category : int {
        None   = 0,
        Weapon = 1,  // bit0
        Tool   = 2,  // bit1
        Armor  = 4,  // bit2
    };

    // 附魔 id（本表自有小整数段 1..14；0 = 无附魔 / 空槽哨兵）。追加新附魔在末尾续号，不重排（ItemStack.
    //   enchants[4] 每槽按 (id<<8)|level 打包，旧存档 / 数据向后兼容）。机制等价 MC 1.0 附魔集（§9 改名）：
    //   - 武器（剑）：锐锋(sharpness) / 亡灵杀手(Smite) / 节肢克星(arthropod-slayer) / 击退(knockback) / 燃焰(fire-aspect)。
    //   - 工具：效率(efficiency) / 精准采集(silk-touch) / 时运(fortune)。
    //   - 通用：耐久(unbreaking) —— 适用武器 / 工具 / 护甲三类的全部。
    //   - 护甲：保护(protection) / 火焰保护(fire-protection) / 摔落保护(feather-fall) /
    //     弹射物保护(projectile-protection) / 水上亲和(aqua-affinity)。
    enum EnchantId : int {
        NoEnchant       = 0,
        Sharpness       = 1,  // 锐锋：剑伤害 +0.5*level（呈现层 attackDamage 叠加；max 5）
        UndeadSlay      = 2,  // 亡灵杀手：对亡灵类 mob 伤害 +（机制等价 MC Smite；max 5）
        ArthropodSlay   = 3,  // 节肢克星：对节肢类 mob 伤害 +（机制等价 MC arthropod-slayer；max 5）
        Knockback       = 4,  // 击退：命中 mob 击退距离 +（机制等价 MC knockback；max 2）
        FireAspect      = 5,  // 燃焰：命中 mob 点燃（机制等价 MC fire-aspect；max 2）
        Efficiency      = 6,  // 效率：挖掘速度 +（机制等价 MC efficiency；max 5）
        SilkTouch       = 7,  // 精准采集：掉落方块自身（机制等价 MC silk-touch；max 1）
        Fortune         = 8,  // 时运：掉落倍率 +（机制等价 MC fortune；max 3）
        Unbreaking      = 9,  // 耐久：耐久消耗概率 -（机制等价 MC unbreaking；max 3；适用武器/工具/护甲）
        Protection      = 10, // 保护：通用减伤（机制等价 MC protection；max 4）
        FireProtection  = 11, // 火焰保护：火焰伤害减免（机制等价 MC fire-protection；max 4）
        FeatherFall     = 12, // 摔落保护：摔落伤害减免（机制等价 MC feather-fall；max 4）
        ProjectileProt  = 13, // 弹射物保护：弹射物伤害减免（机制等价 MC projectile-protection；max 4）
        AquaAffinity    = 14, // 水上亲和：水下挖掘速度（机制等价 MC aqua-affinity；max 1）
        EnchantCount    = 15, // 哨兵：合法附魔 id 上界（1..14；0 = 无附魔）。
    };

    // 附魔定义。表行索引 == enchantId（连续 1..14；详见 enchantregistry.cpp kEnchants）。
    struct EnchantDef {
        int id;              // EnchantId
        int appliesToMask;   // Category 位掩码（适用物品类别并集；耐久 = Weapon|Tool|Armor）
        int maxLevel;        // 最大等级（1..5）
        int weight;          // 权重（越大越常被选中；机制等价 MC 附魔 rarity weight）
        int exclusiveGroup;  // 互斥组（同组附魔不能共存于同一物品；0 = 无互斥）。锐锋/亡灵杀手/节肢克星 = 组 1。
        const char *name;    // 内部 / 调试用名（英文标识符；非面向用户）
        const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词）
    };

    // 附魔判定（id 在 [1, EnchantCount) 内）。
    static bool isEnchant(int enchantId);
    // 取附魔定义。非附魔 id → nullptr。
    static const EnchantDef *enchant(int enchantId);

    // 附魔是否适用给定物品类别（appliesToMask & catMask ≠ 0）。
    static bool isApplicable(int enchantId, int catMask);
    // 最大等级。非附魔 → 0。
    static int maxLevel(int enchantId);
    // 权重。非附魔 → 0。
    static int weight(int enchantId);
    // 用户可见中文显示名（PLAN §9 override (b) 通用词）。非附魔 → 空串。
    static QString displayName(int enchantId);

    // 物品类别（单一权威：据 item id 查 ToolRegistry / ArmorRegistry；决定哪些附魔适用）。
    //   - 护甲段（ArmorRegistry::isArmor）→ Armor。
    //   - 工具段（ToolRegistry::isTool）→ 据 ToolDef.type：Sword→Weapon / Pickaxe·Hoe·Axe·Shovel→Tool /
    //     Bow·Shears·FishingRod → None（本任务不做弓 / 剪刀 / 钓竿专属附魔）。
    //   - 方块段 / 材料段 / 越界 → None（不可附魔）。
    // 返回 Category 位值（None / Weapon / Tool / Armor）；非位掩码叠加（单类别）。
    static int categoryForItem(int itemId);

    // 附魔选择（机制等价 MC 1.0 附魔台）。纯函数：给定物品类别 + 提供等级 + 随机种子 → 返回
    //   [{id: int, level: int}, ...]（QVariantMap list；已剔互斥冲突、已剔重复、等级钳到 maxLevel）。
    //   offeredLevel 1..30（来自 t474 书架加成映射到三槽）；seed 任意 int（每槽种子 → 同槽同 seed 同结果，
    //   重投时 seed 变 → 选项换）。附魔数 = 1..3（offeredLevel 越高越多）；单附魔等级随 offeredLevel 趋 maxLevel。
    //   category=None → 空 list。机制对齐 MC（加权随机 + 等级量级），非数值 1:1。
    static QVariantList selectEnchants(int category, int offeredLevel, int seed);

    // 打包 / 拆包附魔到 ItemStack.enchants[4] 每槽的 int（(enchantId<<8)|level；0 = 空槽）。
    //   供 Hotbar 内部读写用（QML 边界走 QVariantList<int> 4 元素，每元素 = 本 pack 值）。
    static int pack(int enchantId, int level);
    static int packEnchantId(int packed);
    static int packLevel(int packed);

    // 等级 → 罗马数字后缀字符串（如 level=3 → "III"；level=1 → "I"）。供 tooltip / 附魔台显示「锐锋 III」。
    //   level<=0 → 空串；level 1..5 → I/II/III/IV/V；>5 → 阿拉伯数字（防御）。
    static QString levelSuffix(int level);

private:
    EnchantRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // ENCHANTREGISTRY_H
