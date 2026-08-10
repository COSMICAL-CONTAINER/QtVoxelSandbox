#include "enchantregistry.h"

#include <algorithm> // std::clamp / std::min / std::max
#include <vector>

// 单一附魔数据表（spec t475）。改附魔属性（maxLevel / weight / 互斥组 / 名）只改这里，全工程生效。
// 表行索引 == enchantId（连续 1..14；第 0 项是 NoEnchant 占位，使索引与枚举值 1:1 对齐）。
//
// 互斥组（exclusiveGroup，机制等价 MC「同组附魔不可共存」）：
//   - 组 1：锐锋 / 亡灵杀手 / 节肢克星（三种伤害类型附魔三选一；MC 1.0 sharpness/Smite/BaneOfArthropods 互斥）。
//   - 组 0：无互斥（可与其他任意附魔共存）。
//   注：MC 1.0 保护族（protection/fire-protection/feather-fall/projectile-protection）在 1.0 实际可共存
//   （1.0 之后才加单件限制），故本表保护族互斥组 = 0（可共存），仅伤害族互斥。
//
// 权重（weight，机制等价 MC 附魔 rarity）：越大越常被选中。MC 1.0 经典值：锐锋/效率/保护 = 10（常见），
//   精准采集 = 1（极稀有）、时运/燃焰/摔落保护/水上亲和 = 2、其余 = 5。
namespace {
constexpr EnchantRegistry::EnchantDef kEnchants[int(EnchantRegistry::EnchantCount)] = {
    /* 0 NoEnchant      */ { 0, 0, 0, 0, 0, "none",            "" },
    // ── 武器（剑；appliesToMask = Weapon）──
    /* 1 Sharpness      */ { EnchantRegistry::Sharpness,     EnchantRegistry::Weapon, 5, 10, 1, "sharpness",     "\xe9\x94\x90\xe9\x94\x8b" },       // 锐锋
    /* 2 UndeadSlay     */ { EnchantRegistry::UndeadSlay,    EnchantRegistry::Weapon, 5,  5, 1, "undead_slay",   "\xe4\xba\xa1\xe7\x81\xb5\xe6\x9d\x80\xe6\x89\x8b" }, // 亡灵杀手
    /* 3 ArthropodSlay  */ { EnchantRegistry::ArthropodSlay, EnchantRegistry::Weapon, 5,  5, 1, "arthropod_slay","\xe8\x8a\x82\xe8\x82\xa2\xe5\x85\x8b\xe6\x98\x9f" }, // 节肢克星
    /* 4 Knockback      */ { EnchantRegistry::Knockback,     EnchantRegistry::Weapon, 2,  5, 0, "knockback",     "\xe5\x87\xbb\xe9\x80\x80" },       // 击退
    /* 5 FireAspect     */ { EnchantRegistry::FireAspect,    EnchantRegistry::Weapon, 2,  2, 0, "fire_aspect",   "\xe7\x87\x83\xe7\x84\xb0" },       // 燃焰
    // ── 工具（镐/锄/斧/铲；appliesToMask = Tool）──
    /* 6 Efficiency     */ { EnchantRegistry::Efficiency,    EnchantRegistry::Tool,   5, 10, 0, "efficiency",    "\xe6\x95\x88\xe7\x8e\x87" },       // 效率
    /* 7 SilkTouch      */ { EnchantRegistry::SilkTouch,     EnchantRegistry::Tool,   1,  1, 0, "silk_touch",    "\xe7\xb2\xbe\xe5\x87\x86\xe9\x87\x87\xe9\x9b\x86" }, // 精准采集
    /* 8 Fortune        */ { EnchantRegistry::Fortune,       EnchantRegistry::Tool,   3,  2, 0, "fortune",       "\xe6\x97\xb6\xe8\xbf\x90" },       // 时运
    // ── 通用（武器/工具/护甲；appliesToMask = Weapon|Tool|Armor）──
    /* 9 Unbreaking     */ { EnchantRegistry::Unbreaking,
                             EnchantRegistry::Weapon | EnchantRegistry::Tool | EnchantRegistry::Armor,
                             3, 5, 0, "unbreaking",    "\xe8\x80\x90\xe4\xb9\x85" },       // 耐久
    // ── 护甲（头盔/胸甲/护腿/靴子；appliesToMask = Armor）──
    /*10 Protection     */ { EnchantRegistry::Protection,     EnchantRegistry::Armor, 4, 10, 0, "protection",    "\xe4\xbf\x9d\xe6\x8a\xa4" },       // 保护
    /*11 FireProtection */ { EnchantRegistry::FireProtection, EnchantRegistry::Armor, 4,  5, 0, "fire_protection","\xe7\x81\xab\xe7\x84\xb0\xe4\xbf\x9d\xe6\x8a\xa4" }, // 火焰保护
    /*12 FeatherFall    */ { EnchantRegistry::FeatherFall,    EnchantRegistry::Armor, 4,  2, 0, "feather_fall",  "\xe6\x91\x94\xe8\x90\xbd\xe4\xbf\x9d\xe6\x8a\xa4" }, // 摔落保护
    /*13 ProjectileProt */ { EnchantRegistry::ProjectileProt, EnchantRegistry::Armor, 4,  5, 0, "projectile_prot","\xe5\xbc\xb9\xe5\xb0\x84\xe7\x89\xa9\xe4\xbf\x9d\xe6\x8a\xa4" }, // 弹射物保护
    /*14 AquaAffinity   */ { EnchantRegistry::AquaAffinity,   EnchantRegistry::Armor, 1,  2, 0, "aqua_affinity", "\xe6\xb0\xb4\xe4\xb8\x8a\xe4\xba\xb2\xe5\x92\x8c" }, // 水上亲和
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
    return None; // 方块段 / 材料段 / 越界：不可附魔
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

    // 3) 加权不放回抽样 + 互斥组剔除。
    uint rs = uint(seed) ^ 0x9e3779b9u; // 种子扰动（避免 seed=0 退化；同槽同 seed 仍确定性）
    if (rs == 0) rs = 1;                // 防 LCG 陷 0
    int pickedExclusiveGroup = 0;       // 已选附魔的互斥组（0 = 无；非 0 则后续同组附魔被剔）
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
            if (pickedExclusiveGroup != 0 && e->exclusiveGroup == pickedExclusiveGroup) continue;
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
        if (chosen->exclusiveGroup != 0) pickedExclusiveGroup = chosen->exclusiveGroup;

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
