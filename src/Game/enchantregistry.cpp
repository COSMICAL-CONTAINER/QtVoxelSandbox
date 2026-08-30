#include "enchantregistry.h"

#include "recipe.h" // t615 RecipeRegistry::BookId（categoryForItem 书载体判定；同层 Game，向下依赖 Core）

#include <algorithm> // std::clamp / std::min / std::max
#include <vector>

// 单一附魔数据表（spec t475；t960 扩弓 / 钓竿专属六条）。改附魔属性（maxLevel / weight / 互斥组 / 名）
// 只改这里，全工程生效。表行索引 == enchantId（连续 1..20；第 0 项是 NoEnchant 占位，使索引与枚举值 1:1 对齐）。
//
// t959 主类别（homeCategory，EnchantCategory；书附魔池按它成簇收窄——用户第五轮口径「书本附魔把很多
//   工具+装甲附魔冲突地混在一起」）：锐锋族/击退/燃焰 = Weapon、效率/精准/时运 = Tool、保护系/水上亲和
//   = Armor、耐久 = Universal（任何主类别池均可出）、NoEnchant 占位 = None。t960 弓/竿系专属附魔入表：
//   EnchantCatBow（劲射/震击/燃箭/不竭）+ EnchantCatRod（唤潮/缠咬），kBookMainCats 主类别轮同步扩五类。
//
// 互斥组（exclusiveGroup，机制等价 MC「同组附魔不可共存」；t615 按 dev-plan §3 表全接线）：
//   - 组 1：锐锋 / 亡灵杀手 / 节肢克星（三种伤害类型附魔三选一；MC 1.0 sharpness/Smite/BaneOfArthropods 互斥）。
//   - 组 2：效率 / 精准采集 / 时运（采集系三选一；MC 1.0 efficiency/silk-touch/fortune 互斥——原表漏，t615 补）。
//   - 组 3：保护 / 火焰保护 / 摔落保护 / 弹射物保护（保护系四选一；MC 1.0 同件护甲只允许一种保护附魔——
//     原表注「1.0 实际可共存」有误：MC 1.0 保护族同件互斥（Protection 与 Fire/Feather/Projectile Protection
//     不可共存，t475 注释按「1.0 之后才加」理解错误），t615 按 dev-plan 定稿表补组 3）。
//   - 组 0：无互斥（可与其他任意附魔共存）。t960 弓四件 / 竿两件全组 0（彼此无互斥面；不竭与「修复系」
//     互斥是惯例，本作无修复系附魔 → 登记即可，未来入修复系时改组号）。
//
// 适用域（appliesToMask，t615 按 dev-plan §3 表细化；t960 扩弓 / 竿位）：
//   - 锐锋族（1/2/3）= Weapon|Tool（斧亦可，机制等价 MC 斧附武器系）|BookItem（书载体全池）。
//   - 击退（4）/ 燃焰（5）= Weapon|BookItem（仅剑）。
//   - 效率（6）= Tool|BookItem；精准采集（7）= Tool|BookItem；时运（8）= Tool|BookItem。
//     （镐/铲/斧/锄的「精准采集不含锄、时运不含锄/斧」差异走 isApplicableForItem 逐物品精判——mask 是
//     附魔台大类池门，isApplicableForItem 是铁砧逐条适用权威。）
//   - 耐久（9）= Weapon|Tool|Armor|BowItem|RodItem|BookItem（全适用，t960 起含弓 / 钓竿）。
//   - 保护（10）/ 火焰保护（11）/ 弹射物保护（13）= Armor|BookItem（全护甲）。
//   - 摔落保护（12）= Armor|BookItem（**仅靴**，走 isApplicableForItem 精判）。
//   - 水上亲和（14）= Armor|BookItem（**仅头盔**，走 isApplicableForItem 精判）。
//   - t960 弓四件（15-18）= BowItem|BookItem（**仅弓**——「专属」= 适用物品门严：上剑 / 镐 / 护甲全拒，
//     书载体全过 → 铁砧敲弓附魔书 / 附魔台附弓均可达）。竿两件（19/20）= RodItem|BookItem（仅钓鱼竿）。
//   书（BookItem 位）：附魔台附书时 t959 起主类别成簇收窄（单次产物 ⊆ 随机主类别 ∪ 通用；跨施法
//     全 20 附魔长期仍都可达——见 selectEnchantsForItem 主类别 roll 注释）。
//
// 权重（weight，机制等价 MC 附魔 rarity）：越大越常被选中。MC 1.0 经典值：锐锋/效率/保护 = 10（常见），
//   精准采集 = 1（极稀有）、时运/燃焰/摔落保护/水上亲和 = 2、其余 = 5。t960 弓/竿系同分布对齐：
//   劲射 = 10（常见）、震击 = 5、燃箭 = 2（稀有）、不竭 = 1（极稀有，同精准采集）、缠咬 = 5、唤潮 = 2。
namespace {
constexpr EnchantRegistry::EnchantDef kEnchants[int(EnchantRegistry::EnchantCount)] = {
    /* 0 NoEnchant      */ { 0, EnchantRegistry::EnchantCatNone, 0, 0, 0, 0, "none",            "" },
    // ── 武器（剑 + 斧（锐锋族）；appliesToMask = Weapon|Tool|BookItem；主类别 = Weapon）──
    /* 1 Sharpness      */ { EnchantRegistry::Sharpness,     EnchantRegistry::EnchantCatWeapon, EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::BookItem, 5, 10, 1, "sharpness",     "\xe9\x94\x90\xe9\x94\x8b" },       // 锐锋
    /* 2 UndeadSlay     */ { EnchantRegistry::UndeadSlay,    EnchantRegistry::EnchantCatWeapon, EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::BookItem, 5,  5, 1, "undead_slay",   "\xe4\xba\xa1\xe7\x81\xb5\xe6\x9d\x80\xe6\x89\x8b" }, // 亡灵杀手
    /* 3 ArthropodSlay  */ { EnchantRegistry::ArthropodSlay, EnchantRegistry::EnchantCatWeapon, EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::BookItem, 5,  5, 1, "arthropod_slay","\xe8\x8a\x82\xe8\x82\xa2\xe5\x85\x8b\xe6\x98\x9f" }, // 节肢克星
    /* 4 Knockback      */ { EnchantRegistry::Knockback,     EnchantRegistry::EnchantCatWeapon, EnchantRegistry::Weapon | EnchantRegistry::BookItem, 2,  5, 0, "knockback",     "\xe5\x87\xbb\xe9\x80\x80" },       // 击退
    /* 5 FireAspect     */ { EnchantRegistry::FireAspect,    EnchantRegistry::EnchantCatWeapon, EnchantRegistry::Weapon | EnchantRegistry::BookItem, 2,  2, 0, "fire_aspect",   "\xe7\x87\x83\xe7\x84\xb0" },       // 燃焰
    // ── 工具（镐/锄/斧/铲；appliesToMask = Tool|BookItem；主类别 = Tool）──
    /* 6 Efficiency     */ { EnchantRegistry::Efficiency,    EnchantRegistry::EnchantCatTool, EnchantRegistry::Tool | EnchantRegistry::BookItem,   5, 10, 2, "efficiency",    "\xe6\x95\x88\xe7\x8e\x87" },       // 效率（t615 组 2 采集系互斥）
    /* 7 SilkTouch      */ { EnchantRegistry::SilkTouch,     EnchantRegistry::EnchantCatTool, EnchantRegistry::Tool | EnchantRegistry::BookItem,   1,  1, 2, "silk_touch",    "\xe7\xb2\xbe\xe5\x87\x86\xe9\x87\x87\xe9\x9b\x86" }, // 精准采集（组 2）
    /* 8 Fortune        */ { EnchantRegistry::Fortune,       EnchantRegistry::EnchantCatTool, EnchantRegistry::Tool | EnchantRegistry::BookItem,   3,  2, 2, "fortune",       "\xe6\x97\xb6\xe8\xbf\x90" },       // 时运（组 2）
    // ── 通用（武器/工具/护甲/弓/钓竿/书；appliesToMask 全类；主类别 = Universal）──
    /* 9 Unbreaking     */ { EnchantRegistry::Unbreaking,
                             EnchantRegistry::EnchantCatUniversal,
                             EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::Armor
                                 | EnchantRegistry::BowItem | EnchantRegistry::RodItem | EnchantRegistry::BookItem,
                             3, 5, 0, "unbreaking",    "\xe8\x80\x90\xe4\xb9\x85" },       // 耐久（t960 扩弓 / 竿位）
    // ── 护甲（appliesToMask = Armor|BookItem；摔落保护仅靴 / 水上亲和仅头盔走 isApplicableForItem 精判；
    //    主类别 = Armor）──
    /*10 Protection     */ { EnchantRegistry::Protection,     EnchantRegistry::EnchantCatArmor, EnchantRegistry::Armor | EnchantRegistry::BookItem, 4, 10, 3, "protection",    "\xe4\xbf\x9d\xe6\x8a\xa4" },       // 保护（t615 组 3 保护系互斥）
    /*11 FireProtection */ { EnchantRegistry::FireProtection, EnchantRegistry::EnchantCatArmor, EnchantRegistry::Armor | EnchantRegistry::BookItem, 4,  5, 3, "fire_protection","\xe7\x81\xab\xe7\x84\xb0\xe4\xbf\x9d\xe6\x8a\xa4" }, // 火焰保护（组 3）
    /*12 FeatherFall    */ { EnchantRegistry::FeatherFall,    EnchantRegistry::EnchantCatArmor, EnchantRegistry::Armor | EnchantRegistry::BookItem, 4,  2, 3, "feather_fall",  "\xe6\x91\x94\xe8\x90\xbd\xe4\xbf\x9d\xe6\x8a\xa4" }, // 摔落保护（组 3；仅靴）
    /*13 ProjectileProt */ { EnchantRegistry::ProjectileProt, EnchantRegistry::EnchantCatArmor, EnchantRegistry::Armor | EnchantRegistry::BookItem, 4,  5, 3, "projectile_prot","\xe5\xbc\xb9\xe5\xb0\x84\xe7\x89\xa9\xe4\xbf\x9d\xe6\x8a\xa4" }, // 弹射物保护（组 3）
    /*14 AquaAffinity   */ { EnchantRegistry::AquaAffinity,   EnchantRegistry::EnchantCatArmor, EnchantRegistry::Armor | EnchantRegistry::BookItem, 1,  2, 0, "aqua_affinity", "\xe6\xb0\xb4\xe4\xb8\x8a\xe4\xba\xb2\xe5\x92\x8c" }, // 水上亲和（仅头盔）
    // ── t960 弓专属四件（appliesToMask = BowItem|BookItem；仅弓；主类别 = Bow；全组 0）──
    /*15 Might         */ { EnchantRegistry::Might,        EnchantRegistry::EnchantCatBow, EnchantRegistry::BowItem | EnchantRegistry::BookItem, 3, 10, 0, "might",         "\xe5\x8a\xb2\xe5\xb0\x84" },       // 劲射
    /*16 BowShock      */ { EnchantRegistry::BowShock,     EnchantRegistry::EnchantCatBow, EnchantRegistry::BowItem | EnchantRegistry::BookItem, 2,  5, 0, "bow_shock",     "\xe9\x9c\x87\xe5\x87\xbb" },       // 震击
    /*17 BrightDraw    */ { EnchantRegistry::BrightDraw,   EnchantRegistry::EnchantCatBow, EnchantRegistry::BowItem | EnchantRegistry::BookItem, 1,  2, 0, "bright_draw",   "\xe7\x87\x83\xe7\xae\xad" },       // 燃箭
    /*18 NeverRun      */ { EnchantRegistry::NeverRun,     EnchantRegistry::EnchantCatBow, EnchantRegistry::BowItem | EnchantRegistry::BookItem, 1,  1, 0, "never_run",     "\xe4\xb8\x8d\xe7\xab\xad" },       // 不竭
    // ── t960 钓竿专属两件（appliesToMask = RodItem|BookItem；仅钓鱼竿；主类别 = Rod；全组 0）──
    /*19 TideCall      */ { EnchantRegistry::TideCall,     EnchantRegistry::EnchantCatRod, EnchantRegistry::RodItem | EnchantRegistry::BookItem, 3,  2, 0, "tide_call",     "\xe5\x94\xa4\xe6\xbd\xae" },       // 唤潮
    /*20 BiteCall      */ { EnchantRegistry::BiteCall,     EnchantRegistry::EnchantCatRod, EnchantRegistry::RodItem | EnchantRegistry::BookItem, 3,  5, 0, "bite_call",     "\xe7\xbc\xa0\xe5\x92\xac" },       // 缠咬
};

// 编译期表大小守卫：EnchantCount 变更后未同步本表 → 编译失败（防漏行 / 错位）。
static_assert(sizeof(kEnchants) / sizeof(kEnchants[0]) == size_t(EnchantRegistry::EnchantCount),
              "kEnchants 表大小须与 EnchantRegistry::EnchantCount 一致；新附魔需补行");

// 越界 / 非附魔 id → nullptr（统一入口；调用方判空）。表行索引 = enchantId（含第 0 项占位）。
const EnchantRegistry::EnchantDef *defAt(int enchantId)
{
    if (enchantId <= 0 || enchantId >= int(EnchantRegistry::EnchantCount)) return nullptr;
    return &kEnchants[size_t(enchantId)];
}

// 确定性 LCG 伪随机（同 EnchantingTableUI.qml refreshOptions 的种子演化；纯函数，无全局态）。
//   rs in/out（in-place 演化）；返回 [0, range) 内的值。机制等价 MC 附魔台「per-slot seed」。
uint lcgNext(uint &rs)
{
    rs = rs * 1103515245u + 12345u;
    return (rs / 65536u) % 32768u; // MSB 取高位（低周期性弱），同 glibc rand 简化版
}
} // namespace

bool EnchantRegistry::isEnchant(int enchantId)
{
    return defAt(enchantId) != nullptr;
}

const EnchantRegistry::EnchantDef *EnchantRegistry::enchant(int enchantId)
{
    return defAt(enchantId);
}

bool EnchantRegistry::isApplicable(int enchantId, int catMask)
{
    const EnchantDef *e = defAt(enchantId);
    return e && (e->appliesToMask & catMask) != 0;
}

// t959 附魔主类别（见 .h 注释）。非附魔 → EnchantCatNone。
int EnchantRegistry::homeCategory(int enchantId)
{
    const EnchantDef *e = defAt(enchantId);
    return e ? e->homeCategory : int(EnchantCatNone);
}

// t959 冲突组号（exclusiveGroup 单值形式；与 conflictsWith 同一数据源）。非附魔 → 0（无互斥）。
int EnchantRegistry::conflictGroup(int enchantId)
{
    const EnchantDef *e = defAt(enchantId);
    return e ? e->exclusiveGroup : 0;
}

int EnchantRegistry::maxLevel(int enchantId)
{
    const EnchantDef *e = defAt(enchantId);
    return e ? e->maxLevel : 0;
}

int EnchantRegistry::weight(int enchantId)
{
    const EnchantDef *e = defAt(enchantId);
    return e ? e->weight : 0;
}

QString EnchantRegistry::displayName(int enchantId)
{
    const EnchantDef *e = defAt(enchantId);
    return e ? QString::fromUtf8(e->display) : QString();
}

int EnchantRegistry::categoryForItem(int itemId)
{
    // 护甲段 → Armor（须先于工具段判定：护甲段 id 0x300.. > 工具段基址 0x100，二者互斥，顺序无歧义）。
    if (ArmorRegistry::isArmor(itemId)) return Armor;
    if (ToolRegistry::isTool(itemId)) {
        const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId);
        if (!t) return None;
        // 剑 → 武器；镐 / 斧 / 铲 → 工具；**锄 → None（t824：MC 1.0 锄无适用附魔 → 不可附魔）**；
        // **弓 → BowItem / 钓鱼竿 → RodItem（t960：专属附魔入池 → 可附魔）**；剪刀 → None（无专属附魔）。
        //   锄 / 剪刀判 None 连带收口三处门：① 附魔台槽 0 拒入（itemEnchantCategory==0）；② 本表
        //   isApplicableForItem 的 mask 门（None & 任意 mask = 0）→ 铁砧书合并逐条拒；③
        //   selectEnchantsForItem 空候选 → 不给选项。弓 / 竿判非 None 后同三门反向打开：附魔台可附、
        //   铁砧可敲书、selectEnchants 给专属池（劲射/震击/燃箭/不竭 + 耐久 / 唤潮/缠咬 + 耐久）。
        if (t->type == int(BlockRegistry::Sword)) return Weapon;
        if (t->type == int(BlockRegistry::Pickaxe) || t->type == int(BlockRegistry::Axe)
            || t->type == int(BlockRegistry::Shovel)) return Tool;
        if (t->type == int(BlockRegistry::Bow)) return BowItem;
        if (t->type == int(BlockRegistry::FishingRod)) return RodItem;
        return None; // 锄 / Shears
    }
    // t615 书（BookId=0x238）→ BookItem：附魔台附书载体（全池随机 → 产附魔书 EnchantedBookId）。
    //   注：附魔书物品（EnchantedBookId=0x227）**不**返回 BookItem（书已附魔不可再附，itemReady 域外）。
    if (itemId == RecipeRegistry::BookId) return BookItem;
    return None; // 方块段 / 材料段 / 越界：不可附魔
}

// t615 附魔是否适用**具体物品**（铁砧敲附魔书的逐条适用过滤权威；dev-plan §3 表逐条核对）。
//   区别于 isApplicable(id, catMask)（大类门）：本方法对「同大类内的工具类型 / 护甲部位」精判：
//   - 锐锋族（1/2/3）：剑 + 斧（appliesToMask 已含 Weapon|Tool，天然通过；本方法对锄/铲显式拒）。
//   - 击退（4）/燃焰（5）：仅剑（mask 已限 Weapon；斧落 mask 判定即拒）。
//   - 精准采集（7）：镐/铲/斧（mask=Tool 含锄 → 此处锄拒）；时运（8）：镐/铲（锄/斧拒）。
//   - 摔落保护（12）：仅靴（mask=Armor 含四部位 → 此处非靴拒）；水上亲和（14）：仅头盔（非头盔拒）。
//   - t960 弓四件（15-18）：仅弓（mask 已限 BowItem；其他物品落 mask 判定即拒）。竿两件（19/20）：仅钓竿。
//   - 其余（效率全工具 / 耐久全适用含弓竿 / 保护三族全护甲）：mask 判定即正确。
bool EnchantRegistry::isApplicableForItem(int enchantId, int itemId)
{
    const EnchantDef *e = defAt(enchantId);
    if (!e) return false;
    // 大类先过（None 类物品恒不适用；书载体 mask 已含 BookItem → 天然通过）。
    if ((e->appliesToMask & categoryForItem(itemId)) == 0) return false;
    // 同大类内的精判（工具类型 / 护甲部位）。
    if (const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId)) {
        const bool isSword = (t->type == int(BlockRegistry::Sword));
        const bool isAxe = (t->type == int(BlockRegistry::Axe));
        const bool isPick = (t->type == int(BlockRegistry::Pickaxe));
        const bool isShovel = (t->type == int(BlockRegistry::Shovel));
        const bool isBow = (t->type == int(BlockRegistry::Bow));
        const bool isRod = (t->type == int(BlockRegistry::FishingRod));
        switch (enchantId) {
        case Sharpness: case UndeadSlay: case ArthropodSlay:
            return isSword || isAxe;   // 锐锋族：剑 + 斧（锄 / 铲 / 弓拒）
        case Knockback: case FireAspect:
            return isSword;            // 击退 / 燃焰：仅剑
        case SilkTouch:
            return isPick || isShovel || isAxe; // 精准采集：镐/铲/斧（锄拒）
        case Fortune:
            return isPick || isShovel; // 时运：镐/铲（锄 / 斧拒）
        case Might: case BowShock: case BrightDraw: case NeverRun:
            return isBow;              // 弓四件：仅弓（mask 已限 BowItem；书载体不走本分支）
        case TideCall: case BiteCall:
            return isRod;              // 竿两件：仅钓鱼竿
        default:
            break; // 效率（镐/斧/铲——锄经 categoryForItem=None 已在 mask 门拒）/ 耐久（全适用）等：mask 已过 → 适用
        }
        return true;
    }
    if (ArmorRegistry::isArmor(itemId)) {
        const int piece = ArmorRegistry::piece(itemId);
        switch (enchantId) {
        case FeatherFall:
            return piece == ArmorRegistry::Boots;   // 摔落保护：仅靴
        case AquaAffinity:
            return piece == ArmorRegistry::Helmet;  // 水上亲和：仅头盔
        default:
            break; // 保护 / 火焰保护 / 弹射物保护：全护甲
        }
        return true;
    }
    // 书载体（BookItem）或其它：mask 已过 → 适用（附魔书上任何附魔对「书」都合法——书是载体非穿戴物）。
    return true;
}

// t615 冲突组查询：同组（exclusiveGroup != 0 且相等）即互斥；同 id 不算冲突（等级合并走铁砧 Lc==Lb→+1 路径）。
bool EnchantRegistry::conflictsWith(int enchantId, int otherEnchantId)
{
    if (enchantId == otherEnchantId) return false;
    const EnchantDef *a = defAt(enchantId);
    const EnchantDef *b = defAt(otherEnchantId);
    if (!a || !b) return false;
    return a->exclusiveGroup != 0 && a->exclusiveGroup == b->exclusiveGroup;
}

// t824 附魔选择（按**具体物品**过滤，附魔台三档选项池单一权威；机制等价 MC 1.0 附魔台加权随机 +
//   offered-level 量级）。纯函数。offeredLevel 1..30（来自 t474 书架加成映射到三槽）；seed 任意 int。
//   与旧 selectEnchants(category,..) 的差别仅在候选池：isApplicableForItem(enchantId, itemId) 逐条精判
//   （镐 / 铲不出亡灵杀手；胸甲不出摔落保护；锄 / 弓空池）替代大类 mask 门。书（BookId）mask 全过 →
//   t959 起主类别成簇收窄（附书 / 战利品附魔书同入口同收窄，loottable 复用；见下方主类别 roll 注释）。
//   步骤：
//     1) 候选 = 适用该物品的附魔（isApplicableForItem 精判，1..14 扫一遍）。
//     1b) t959 书载体：LCG 先 roll 主类别（武器/工具/装甲三选一）→ 候选收窄到「该类 ∪ 通用」。
//     2) 附魔数 count：offeredLevel 越高越多（1..3）；钳到候选数。
//     3) 加权不放回抽样 count 个（同 MC rarity weight；命中后从候选移除 + 剔除同互斥组的余下候选）。
//     4) 每个附魔等级：clamp(round(maxLevel * offeredLevel / 30) + 种子扰动, 1, maxLevel)。
//        maxLevel=1（精准采集 / 水上亲和）恒为 1。
QVariantList EnchantRegistry::selectEnchantsForItem(int itemId, int offeredLevel, int seed)
{
    QVariantList result;
    const int category = categoryForItem(itemId);
    if (category == None) return result;
    // 钳 offeredLevel 到 [1, 30]（防御；UI 应保证）。
    const int lvl = std::clamp(offeredLevel, 1, 30);

    // 1) 候选池：适用该**物品**的附魔（t824 isApplicableForItem 精判；书载体 mask 含 BookItem 全过）。
    std::vector<const EnchantDef *> candidates;
    candidates.reserve(8);
    for (int i = 1; i < int(EnchantCount); ++i) {
        if (isApplicableForItem(i, itemId)) candidates.push_back(&kEnchants[size_t(i)]);
    }
    if (candidates.empty()) return result;

    // 种子扰动 LCG 初值（避免 seed=0 退化；同槽同 seed 仍确定性）。t959 上移到主类别 roll 之前——
    //   书路径 1b) 要先消耗一次 LCG 定主类别，非书路径首个 LCG 消费点不变（流语义零漂移）。
    uint rs = uint(seed) ^ 0x9e3779b9u;
    if (rs == 0) rs = 1;                // 防 LCG 陷 0

    // 1b) t959 书附魔主类别收窄（用户第五轮口径「书本附魔把很多工具+装甲附魔冲突地混在一起」）：
    //   书载体 isApplicableForItem 全过 → 旧版一次施法从全池并集抽 1-3 条，产物常是「保护+效率+
    //   锐锋」这类**任何单件物品都戴不上**的跨类乱炖。收窄 = 施法时先由同一 LCG 定一个**主类别**
    //   （武器/工具/装甲/弓/竿五类均匀轮——t959 起三类；t960 弓/竿系入表扩五类 = t959 预留的扩展位，
    //   弓附魔只上弓 / 竿附魔只上钓竿的「专属」物品门不变，书是万能载体 → 书池五类轮换皆可出），
    //   本轮产物全部从「该类池 ∪ 通用（耐久）」抽 → 单次产物同类成簇、跨类混出绝迹；conflictGroup 同组
    //   互斥由步骤 3 位集抽样在收窄池上照旧生效（保护系四件互斥 / 锐锋族三选一 / 采集系三选一）。
    //   跨施法（不同 seed）主类别随机轮换 → 全 20 附魔在书池长期仍都可达（union 不收窄，只收窄单次
    //   产物内部）。直附面（工具/武器/护甲/弓/竿）不经此分支——池本就按 isApplicableForItem 逐物品精判
    //   （t824），无跨类混出问题。roll 在抽样**前**消耗同一 LCG 流 → 同 seed 恒同主类别，t917「预告
    //   与施放严格同源」（同 seed 同产物）与 PLAN §2-K 确定性契约保持。
    if (category == BookItem) {
        static constexpr int kBookMainCats[5] = { EnchantCatWeapon, EnchantCatTool, EnchantCatArmor,
                                                  EnchantCatBow, EnchantCatRod };
        const int mainCat = kBookMainCats[lcgNext(rs) % 5u];
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [mainCat](const EnchantDef *e) {
                                            return e->homeCategory != mainCat
                                                && e->homeCategory != int(EnchantCatUniversal);
                                        }),
                         candidates.end());
        if (candidates.empty()) return result; // 防御（五类主类别池恒非空，理论不可达）
    }

    // 2) 附魔数（1..3）：offeredLevel >= 10 → 至少 2；>= 20 → 至多 3；钳到候选数。机制等价 MC「高等级附魔台
    //   选项给更多 / 更强附魔」。
    int count = 1;
    if (lvl >= 10) count = 2;
    if (lvl >= 20) count = 3;
    count = std::min(count, int(candidates.size()));

    // 3) 加权不放回抽样 + 互斥组剔除。review M1 修：互斥组用**位集**（每非 0 组一位）而非单值——旧单值
    //   pickedExclusiveGroup 会被后选组覆盖（先选组 1 锐锋再遇组 2 效率时组 1 记录丢失 → Sharpness+Smite
    //   或 Protection+FireProtection 可能同存于产物，自相矛盾：铁砧 conflictsWith 会拒之）。组号 ≤3 →
    //   quint32 位集足够（组号越界按无互斥处理，防御）。rs 已在 1b) 前初始化（t959 上移）。
    quint32 pickedGroups = 0;           // 已选附魔的互斥组位集（bit(g-1) 置位；0 = 尚无互斥组）
    std::vector<int> pickedIds;
    pickedIds.reserve(size_t(count));

    for (int n = 0; n < count; ++n) {
        // 过滤候选：移除已选 id + 已占互斥组的同组附魔。
        std::vector<const EnchantDef *> pool;
        pool.reserve(candidates.size());
        for (const EnchantDef *e : candidates) {
            bool alreadyPicked = false;
            for (int pid : pickedIds) if (pid == e->id) { alreadyPicked = true; break; }
            if (alreadyPicked) continue;
            if (e->exclusiveGroup > 0 && e->exclusiveGroup < 32
                && (pickedGroups & (1u << (e->exclusiveGroup - 1))) != 0) continue;
            pool.push_back(e);
        }
        if (pool.empty()) break; // 候选耗尽（互斥剔完）→ 提前结束

        // 加权随机选一个：累计权重 + LCG 取模。
        int totalW = 0;
        for (const EnchantDef *e : pool) totalW += std::max(1, e->weight);
        uint r = lcgNext(rs) % uint(totalW);
        const EnchantDef *chosen = pool.front();
        for (const EnchantDef *e : pool) {
            if (r < uint(std::max(1, e->weight))) { chosen = e; break; }
            r -= uint(std::max(1, e->weight));
        }
        pickedIds.push_back(chosen->id);
        // 互斥组位集置位（review M1：多组共存累积，不覆盖既有组）。
        if (chosen->exclusiveGroup > 0 && chosen->exclusiveGroup < 32)
            pickedGroups |= (1u << (chosen->exclusiveGroup - 1));

        // 4) 等级：基础 = round(maxLevel * lvl / 30)（offered 30 → 满；1 → 1 级）；maxLevel=1 恒 1
        //    （精准采集 / 水上亲和 / t960 燃箭 / 不竭）。
        int eLevel = 1;
        if (chosen->maxLevel > 1) {
            eLevel = (chosen->maxLevel * lvl + 15) / 30; // 四舍五入（+15 = half-up）
            // 种子扰动 ±1（让同 offered 不同 seed 有等级变化；机制等价 MC 附魔等级随机性）。
            if (lcgNext(rs) & 1) eLevel += 1;
            eLevel = std::clamp(eLevel, 1, chosen->maxLevel);
        }
        QVariantMap m;
        m.insert(QStringLiteral("id"), chosen->id);
        m.insert(QStringLiteral("level"), eLevel);
        result.append(m);
    }
    return result;
}

// t795 附魔台书架门槛公式（单一权威；见头注释）。tierForBookshelves 与游戏模式无关 —— 创造模式同样
//   须书架达标（1 档恒开；≥5 → 2；≥10 → 3）。offeredLevelFor：floor(bs*20*(tier+1)/33)+(tier+1) 钳 [1,30]
//   （非负整数除法 == floor，与原 QML Math.floor 同式；bs=15 → [10,20,30]，顶格 30 仅满 15 书架）。
int EnchantRegistry::tierForBookshelves(int bookshelves)
{
    if (bookshelves >= 10) return 3;
    if (bookshelves >= 5) return 2;
    return 1;
}

int EnchantRegistry::offeredLevelFor(int bookshelves, int tierIdx)
{
    const int t = std::clamp(tierIdx, 0, 2) + 1;   // 档位序号 0..2 → 1..3
    const int bs = std::clamp(bookshelves, 0, 15); // World 计数本就 ≤15，防御钳
    return std::clamp(bs * 20 * t / 33 + t, 1, 30);
}

int EnchantRegistry::pack(int enchantId, int level)
{
    if (enchantId <= 0) return 0; // 0 = 空槽
    return (enchantId << 8) | (level & 0xff);
}

int EnchantRegistry::packEnchantId(int packed)
{
    return (packed >> 8) & 0xff;
}

int EnchantRegistry::packLevel(int packed)
{
    return packed & 0xff;
}

// t476 读物品附魔等级：扫 4 槽 packed int，首个 id 匹配槽的 level（0 = 无该附魔）。
//   机制等价 MC「读 item enchantments list 取某附魔等级」。同附魔不重复（selectEnchants 已剔），首个即唯一。
int EnchantRegistry::findLevel(const int *enchants, int enchantId)
{
    if (!enchants || !isEnchant(enchantId)) return 0;
    for (int i = 0; i < 4; ++i) {
        if (packEnchantId(enchants[i]) == enchantId)
            return std::max(0, packLevel(enchants[i]));
    }
    return 0;
}

QString EnchantRegistry::levelSuffix(int level)
{
    switch (level) {
    case 1:  return QStringLiteral("I");
    case 2:  return QStringLiteral("II");
    case 3:  return QStringLiteral("III");
    case 4:  return QStringLiteral("IV");
    case 5:  return QStringLiteral("V");
    default: return level > 5 ? QString::number(level) : QString();
    }
}

// t825 手持武器攻击伤害（显示与实战同源单一权威；见头注释）。基础走 ToolRegistry::attackDamage（剑 tier
//   倍率 / 斧 tier+1 / 余徒手 1），锐锋 +0.5*级。attackMob 以此为起点再叠对族加成 / 暴击；tooltip 攻击行
//   经 Hotbar::displayAttackDamage 取整同值 —— 两处消费一个公式，杜绝「实战加了显示没加」漂移。
float EnchantRegistry::weaponAttackDamage(int itemId, const int *enchants)
{
    return float(ToolRegistry::attackDamage(itemId))
         + 0.5f * float(findLevel(enchants, int(Sharpness)));
}

// t826 击退附魔强度（单一权威；见头注释）。每级 +3.0 倍冲量：无附魔 1.0（~1.1 格）/ I 4.0（~4.5 格）/
//   II 7.0（~7.9 格），量级对齐 MC 1.0「击退 I 明显推离 / II 飞出数格」。负级防御钳 0。
float EnchantRegistry::knockbackStrength(int level)
{
    return 1.0f + 3.0f * float(std::max(0, level));
}

// ── t960 弓 / 钓竿专属附魔效果公式（单一权威；数值自定，注释「用户口径：弓四件+竿两件专属」）──
//   效果接线点：劲射 → playercontroller endBowDraw 箭伤计算；震击 / 燃箭 → Game 层算倍率 / 时长后经
//   spawnArrowPlayer 参数下传（Entities 层不 include Game——分层铁律，箭实体携 per-entity 值，t505
//   per-entity 字段先例）；不竭 → endBowDraw 免消耗；唤潮 / 缠咬 → useFishingRod 甩竿时算好经
//   spawnBobber 参数下传（等待缩放 / 判定窗附加量存在浮标实体上，鱼跑重掷照用）。

// 劲射：箭伤 = base + 1×级（满弓 6 → III 9）。mightLevel 负值防御钳 0（无上限钳——铁砧合并已封顶
//   maxLevel=3，此处不做双重钳以免两处口径漂移）。
int EnchantRegistry::bowArrowDamage(int baseDamage, int mightLevel)
{
    return baseDamage + std::max(0, mightLevel);
}

// 震击：箭击退倍率 = 1 + 1.0×级（I ×2 / II ×3；乘在箭基线击退强度上 → 位移同倍放大）。负级钳 0。
float EnchantRegistry::bowKnockbackMultiplier(int level)
{
    return 1.0f + 1.0f * float(std::max(0, level));
}

// 燃箭：级 ≥1 → 点燃 5.0s（机制等价 MC flame 一击 5s 量级；max 1 不分级）；级 0 → 0（无点燃）。
float EnchantRegistry::bowIgniteSeconds(int level)
{
    return level > 0 ? 5.0f : 0.0f;
}

// 唤潮：等待期倍率 = 1 − 0.2×级（I ×0.8 / II ×0.6 / III ×0.4 → [5,30]s 掷骰 → III [2,12]s）。
//   级钳 [0,3]（防御：铁砧合并已封顶 maxLevel=3，防未来路径漏钳把等待缩成 0）。
float EnchantRegistry::rodWaitScale(int level)
{
    return 1.0f - 0.2f * float(std::clamp(level, 0, 3));
}

// 缠咬：判定窗附加量 = 0.5s×级（I +0.5s / III +1.5s）。kBobberBiteWindowSec=1.0 基值用户口径钉死不动，
//   附加式加宽；级钳 [0,3]（同上防御）。
float EnchantRegistry::rodBiteWindowExtra(int level)
{
    return 0.5f * float(std::clamp(level, 0, 3));
}
