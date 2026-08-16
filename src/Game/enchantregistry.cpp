#include "enchantregistry.h"

#include "recipe.h" // t615 RecipeRegistry::BookId（categoryForItem 书载体判定；同层 Game，向下依赖 Core）

#include <algorithm> // std::clamp / std::min / std::max
#include <vector>

// 单一附魔数据表（spec t475）。改附魔属性（maxLevel / weight / 互斥组 / 名）只改这里，全工程生效。
// 表行索引 == enchantId（连续 1..14；第 0 项是 NoEnchant 占位，使索引与枚举值 1:1 对齐）。
//
// 互斥组（exclusiveGroup，机制等价 MC「同组附魔不可共存」；t615 按 dev-plan §3 表全接线）：
//   - 组 1：锐锋 / 亡灵杀手 / 节肢克星（三种伤害类型附魔三选一；MC 1.0 sharpness/Smite/BaneOfArthropods 互斥）。
//   - 组 2：效率 / 精准采集 / 时运（采集系三选一；MC 1.0 efficiency/silk-touch/fortune 互斥——原表漏，t615 补）。
//   - 组 3：保护 / 火焰保护 / 摔落保护 / 弹射物保护（保护系四选一；MC 1.0 同件护甲只允许一种保护附魔——
//     原表注「1.0 实际可共存」有误：MC 1.0 保护族同件互斥（Protection 与 Fire/Feather/Projectile Protection
//     不可共存，t475 注释按「1.0 之后才加」理解错误），t615 按 dev-plan 定稿表补组 3）。
//   - 组 0：无互斥（可与其他任意附魔共存）。
//
// 适用域（appliesToMask，t615 按 dev-plan §3 表细化）：
//   - 锐锋族（1/2/3）= Weapon|Tool（斧亦可，机制等价 MC 斧附武器系）|BookItem（书载体全池）。
//   - 击退（4）/ 燃焰（5）= Weapon|BookItem（仅剑）。
//   - 效率（6）= Tool|BookItem；精准采集（7）= Tool|BookItem；时运（8）= Tool|BookItem。
//     （镐/铲/斧/锄的「精准采集不含锄、时运不含锄/斧」差异走 isApplicableForItem 逐物品精判——mask 是
//     附魔台大类池门，isApplicableForItem 是铁砧逐条适用权威。）
//   - 耐久（9）= Weapon|Tool|Armor|BookItem（全适用，机制等价 MC unbreaking 全装备通用）。
//   - 保护（10）/ 火焰保护（11）/ 弹射物保护（13）= Armor|BookItem（全护甲）。
//   - 摔落保护（12）= Armor|BookItem（**仅靴**，走 isApplicableForItem 精判）。
//   - 水上亲和（14）= Armor|BookItem（**仅头盔**，走 isApplicableForItem 精判）。
//   书（BookItem 位）：附魔台附书时全 14 附魔候选（机制等价 MC「书 = 全池」）。
//
// 权重（weight，机制等价 MC 附魔 rarity）：越大越常被选中。MC 1.0 经典值：锐锋/效率/保护 = 10（常见），
//   精准采集 = 1（极稀有）、时运/燃焰/摔落保护/水上亲和 = 2、其余 = 5。
namespace {
constexpr EnchantRegistry::EnchantDef kEnchants[int(EnchantRegistry::EnchantCount)] = {
    /* 0 NoEnchant      */ { 0, 0, 0, 0, 0, "none",            "" },
    // ── 武器（剑 + 斧（锐锋族）；appliesToMask = Weapon|Tool|BookItem）──
    /* 1 Sharpness      */ { EnchantRegistry::Sharpness,     EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::BookItem, 5, 10, 1, "sharpness",     "\xe9\x94\x90\xe9\x94\x8b" },       // 锐锋
    /* 2 UndeadSlay     */ { EnchantRegistry::UndeadSlay,    EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::BookItem, 5,  5, 1, "undead_slay",   "\xe4\xba\xa1\xe7\x81\xb5\xe6\x9d\x80\xe6\x89\x8b" }, // 亡灵杀手
    /* 3 ArthropodSlay  */ { EnchantRegistry::ArthropodSlay, EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::BookItem, 5,  5, 1, "arthropod_slay","\xe8\x8a\x82\xe8\x82\xa2\xe5\x85\x8b\xe6\x98\x9f" }, // 节肢克星
    /* 4 Knockback      */ { EnchantRegistry::Knockback,     EnchantRegistry::Weapon | EnchantRegistry::BookItem, 2,  5, 0, "knockback",     "\xe5\x87\xbb\xe9\x80\x80" },       // 击退
    /* 5 FireAspect     */ { EnchantRegistry::FireAspect,    EnchantRegistry::Weapon | EnchantRegistry::BookItem, 2,  2, 0, "fire_aspect",   "\xe7\x87\x83\xe7\x84\xb0" },       // 燃焰
    // ── 工具（镐/锄/斧/铲；appliesToMask = Tool|BookItem）──
    /* 6 Efficiency     */ { EnchantRegistry::Efficiency,    EnchantRegistry::Tool | EnchantRegistry::BookItem,   5, 10, 2, "efficiency",    "\xe6\x95\x88\xe7\x8e\x87" },       // 效率（t615 组 2 采集系互斥）
    /* 7 SilkTouch      */ { EnchantRegistry::SilkTouch,     EnchantRegistry::Tool | EnchantRegistry::BookItem,   1,  1, 2, "silk_touch",    "\xe7\xb2\xbe\xe5\x87\x86\xe9\x87\x87\xe9\x9b\x86" }, // 精准采集（组 2）
    /* 8 Fortune        */ { EnchantRegistry::Fortune,       EnchantRegistry::Tool | EnchantRegistry::BookItem,   3,  2, 2, "fortune",       "\xe6\x97\xb6\xe8\xbf\x90" },       // 时运（组 2）
    // ── 通用（武器/工具/护甲/书；appliesToMask = Weapon|Tool|Armor|BookItem）──
    /* 9 Unbreaking     */ { EnchantRegistry::Unbreaking,
                             EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::Armor | EnchantRegistry::BookItem,
                             3, 5, 0, "unbreaking",    "\xe8\x80\x90\xe4\xb9\x85" },       // 耐久
    // ── 护甲（appliesToMask = Armor|BookItem；摔落保护仅靴 / 水上亲和仅头盔走 isApplicableForItem 精判）──
    /*10 Protection     */ { EnchantRegistry::Protection,     EnchantRegistry::Armor | EnchantRegistry::BookItem, 4, 10, 3, "protection",    "\xe4\xbf\x9d\xe6\x8a\xa4" },       // 保护（t615 组 3 保护系互斥）
    /*11 FireProtection */ { EnchantRegistry::FireProtection, EnchantRegistry::Armor | EnchantRegistry::BookItem, 4,  5, 3, "fire_protection","\xe7\x81\xab\xe7\x84\xb0\xe4\xbf\x9d\xe6\x8a\xa4" }, // 火焰保护（组 3）
    /*12 FeatherFall    */ { EnchantRegistry::FeatherFall,    EnchantRegistry::Armor | EnchantRegistry::BookItem, 4,  2, 3, "feather_fall",  "\xe6\x91\x94\xe8\x90\xbd\xe4\xbf\x9d\xe6\x8a\xa4" }, // 摔落保护（组 3；仅靴）
    /*13 ProjectileProt */ { EnchantRegistry::ProjectileProt, EnchantRegistry::Armor | EnchantRegistry::BookItem, 4,  5, 3, "projectile_prot","\xe5\xbc\xb9\xe5\xb0\x84\xe7\x89\xa9\xe4\xbf\x9d\xe6\x8a\xa4" }, // 弹射物保护（组 3）
    /*14 AquaAffinity   */ { EnchantRegistry::AquaAffinity,   EnchantRegistry::Armor | EnchantRegistry::BookItem, 1,  2, 0, "aqua_affinity", "\xe6\xb0\xb4\xe4\xb8\x8a\xe4\xba\xb2\xe5\x92\x8c" }, // 水上亲和（仅头盔）
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
        // 剑 → 武器；镐 / 锄 / 斧 / 铲 → 工具；弓 / 剪刀 / 钓鱼竿 → None（专属附魔本任务不做）。
        if (t->type == int(BlockRegistry::Sword)) return Weapon;
        if (t->type == int(BlockRegistry::Pickaxe) || t->type == int(BlockRegistry::Hoe)
            || t->type == int(BlockRegistry::Axe) || t->type == int(BlockRegistry::Shovel)) return Tool;
        return None; // Bow / Shears / FishingRod
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
//   - 其余（效率全工具 / 耐久全适用 / 保护三族全护甲）：mask 判定即正确。
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
        switch (enchantId) {
        case Sharpness: case UndeadSlay: case ArthropodSlay:
            return isSword || isAxe;   // 锐锋族：剑 + 斧（锄 / 铲 / 弓拒）
        case Knockback: case FireAspect:
            return isSword;            // 击退 / 燃焰：仅剑
        case SilkTouch:
            return isPick || isShovel || isAxe; // 精准采集：镐/铲/斧（锄拒）
        case Fortune:
            return isPick || isShovel; // 时运：镐/铲（锄 / 斧拒）
        default:
            break; // 效率（全工具）/ 耐久（全适用）等：mask 已过 → 适用
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

// 附魔选择（机制等价 MC 1.0 附魔台加权随机 + offered-level 量级）。纯函数。
//   offeredLevel 1..30（来自 t474 书架加成映射到三槽的提供等级）；seed 任意 int。
//   步骤：
//     1) 候选 = 适用该类别的附魔（appliesToMask & category）。
//     2) 附魔数 count：offeredLevel 越高越多（1..3）；钳到候选数。
//     3) 加权不放回抽样 count 个（同 MC rarity weight；命中后从候选移除 + 剔除同互斥组的余下候选）。
//     4) 每个附魔等级：clamp(round(maxLevel * offeredLevel / 30) + 种子扰动, 1, maxLevel)。
//        maxLevel=1（精准采集 / 水上亲和）恒为 1。
QVariantList EnchantRegistry::selectEnchants(int category, int offeredLevel, int seed)
{
    QVariantList result;
    if (category == None) return result;
    // 钳 offeredLevel 到 [1, 30]（防御；UI 应保证）。
    const int lvl = std::clamp(offeredLevel, 1, 30);

    // 1) 候选池：适用该类别的附魔（1..14 扫一遍）。
    std::vector<const EnchantDef *> candidates;
    candidates.reserve(8);
    for (int i = 1; i < int(EnchantCount); ++i) {
        const EnchantDef *e = &kEnchants[size_t(i)];
        if ((e->appliesToMask & category) != 0) candidates.push_back(e);
    }
    if (candidates.empty()) return result;

    // 2) 附魔数（1..3）：offeredLevel >= 10 → 至少 2；>= 20 → 至多 3；钳到候选数。机制等价 MC「高等级附魔台
    //   选项给更多 / 更强附魔」。
    int count = 1;
    if (lvl >= 10) count = 2;
    if (lvl >= 20) count = 3;
    count = std::min(count, int(candidates.size()));

    // 3) 加权不放回抽样 + 互斥组剔除。review M1 修：互斥组用**位集**（每非 0 组一位）而非单值——旧单值
    //   pickedExclusiveGroup 会被后选组覆盖（先选组 1 锐锋再遇组 2 效率时组 1 记录丢失 → Sharpness+Smite
    //   或 Protection+FireProtection 可能同存于产物，自相矛盾：铁砧 conflictsWith 会拒之）。组号 ≤3 →
    //   quint32 位集足够（组号越界按无互斥处理，防御）。
    uint rs = uint(seed) ^ 0x9e3779b9u; // 种子扰动（避免 seed=0 退化；同槽同 seed 仍确定性）
    if (rs == 0) rs = 1;                // 防 LCG 陷 0
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

        // 4) 等级：基础 = round(maxLevel * lvl / 30)（offered 30 → 满；1 → 1 级）；maxLevel=1 恒 1。
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
