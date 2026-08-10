#include "loottable.h"

#include "recipe.h" // 材料段 id 常量（CoalId / RedstoneId / ...；同层 Game，向下依赖 Core）

#include <QRandomGenerator>

// 战利品表（t393）实现。纯静态数据 + 纯函数 —— 无 Q_OBJECT / 无实例 / 无 World 依赖（PLAN §2 分层）。

// 地牢箱子战利品池（8 条）。权重总和 = 30+30+25+20+18+8+6+4 = 141。
//   分布（每 roll 命中概率）：煤 / 面包 ~21% 各、线 ~18%、红石 ~14%、铁锭 ~13%（常见材料 ~87%）；
//   马鞍 ~5.7%、命名牌 ~4.3%（稀有 ~10%）；附魔书占位 ~2.8%（极稀有）。机制对齐 MC 1.0 dungeon chest
//   「常见材料多 / 稀有件少」分布。数量区间：常见材料 1..N（一堆），稀有件恒 1（单件，避免一堆马鞍）。
//   static 局部 + 函数返回 const 引用 —— 首次调用构造、后续零开销；调用方不持副本（单一权威）。
const std::vector<LootTable::Entry> &LootTable::dungeonChestPool()
{
    static const std::vector<Entry> pool = {
        { RecipeRegistry::CoalId,        30, 1, 6 }, // 煤炭：常见燃料，1..6 一堆
        { RecipeRegistry::BreadId,       30, 1, 3 }, // 面包：常见食物，1..3
        { RecipeRegistry::StringId,      25, 1, 6 }, // 线：常见（弓 / 钓竿原料），1..6
        { RecipeRegistry::RedstoneId,    20, 1, 5 }, // 红石粉：较常见，1..5
        { RecipeRegistry::IronIngotId,   18, 1, 4 }, // 铁锭：较常见，1..4
        { RecipeRegistry::SaddleId,       8, 1, 1 }, // 马鞍：稀有，单件
        { RecipeRegistry::NameTagId,       6, 1, 1 }, // 命名牌：稀有，单件
        { RecipeRegistry::EnchantedBookId, 4, 1, 1 }, // 附魔书占位：极稀有，单件
    };
    return pool;
}

// t401 钓鱼获物池（生鱼 / 垃圾 / 宝藏）。权重总和 = 60 + 42 + 6 = 108。
//   分布（每 roll 命中概率）：生鱼 ~55%（常见获物）；垃圾 ~39%（皮革 / 线 / 骨头 / 腐肉 / 木棒 / 墨囊，
//   各 ~4-9%）；宝藏 ~5.5%（马鞍 / 命名牌 / 钻石，各 ~1-3%）。机制对齐 MC 1.0 fishing loot 的「鱼常见 /
//   垃圾次之 / 宝藏稀有」三档分布。单次 roll = 一件获物（钓鱼拉起抽一次）。static 局部 + 返回 const 引用
//   （单一权威；调用方不持副本）。
const std::vector<LootTable::Entry> &LootTable::fishingPool()
{
    static const std::vector<Entry> pool = {
        // 生鱼（常见获物，高权重）。
        { RecipeRegistry::RawFishId,   60, 1, 1 }, // 生鱼：钓鱼常见获物（机制等价 MC 1.0 raw fish）
        // 垃圾（中权重；复用既有 mob 掉落 / 材料段物品）。
        { RecipeRegistry::LeatherId,   10, 1, 1 }, // 皮革：垃圾（破旧皮革）
        { RecipeRegistry::StringId,     8, 1, 3 }, // 线：垃圾（缠绕废线，1..3）
        { RecipeRegistry::BoneId,       8, 1, 2 }, // 骨头：垃圾（鱼骨，1..2）
        { RecipeRegistry::RottenFleshId,6, 1, 1 }, // 腐肉：垃圾（水中腐物）
        { RecipeRegistry::StickId,      6, 1, 2 }, // 木棒：垃圾（漂流枝，1..2）
        { RecipeRegistry::InkSacId,     4, 1, 1 }, // 墨囊：垃圾（墨汁囊）
        // 宝藏（稀有，低权重；复用 t393 战利品表物品 + 钻石）。
        { RecipeRegistry::SaddleId,     3, 1, 1 }, // 马鞍：宝藏（机制等价 MC 1.0 fishing treasure saddle）
        { RecipeRegistry::NameTagId,    2, 1, 1 }, // 命名牌：宝藏（机制等价 MC fishing treasure name tag）
        { RecipeRegistry::DiamondId,    1, 1, 1 }, // 钻石：极稀有宝藏
    };
    return pool;
}

// t484 废弃矿井箱子战利品池（见 loottable.h 头注释）。权重总和 = 30+25+20+12+10+5+3 = 105。
//   分布（每 roll 命中概率）：煤 ~29% / 红石 ~24% / 铁锭 ~19%（常见矿物 ~71%）；
//   金锭 ~11% / 青金石 ~10%（次常见 ~21%）；钻石 ~5%（稀有）；附魔书占位 ~3%（极稀有）。
//   机制对齐 MC 1.0 mineshaft chest「矿物多 / 附魔书铁锭常见 / 钻石稀有」分布。数量区间：常见矿物 1..N（一堆），
//   稀有件恒 1（单件，避免一堆钻石）。static 局部 + 返回 const 引用（单一权威；调用方不持副本）。
const std::vector<LootTable::Entry> &LootTable::mineshaftChestPool()
{
    static const std::vector<Entry> pool = {
        { RecipeRegistry::CoalId,         30, 1, 6 }, // 煤炭：常见矿物燃料，1..6 一堆
        { RecipeRegistry::RedstoneId,     25, 1, 5 }, // 红石粉：常见矿物，1..5
        { RecipeRegistry::IronIngotId,    20, 1, 4 }, // 铁锭：常见金属（spec「铁锭」），1..4
        { RecipeRegistry::GoldIngotId,    12, 1, 3 }, // 金锭：次常见金属，1..3
        { RecipeRegistry::LapisId,        10, 1, 3 }, // 青金石：次常见矿物（附魔前置材料），1..3
        { RecipeRegistry::DiamondId,       5, 1, 1 }, // 钻石：稀有矿物，单件
        { RecipeRegistry::EnchantedBookId, 3, 1, 1 }, // 附魔书占位：极稀有（spec「附魔书」），单件
    };
    return pool;
}

// t485 沙漠神殿箱子战利品池（见 loottable.h 头注释）。权重总和 = 30+25+18+12+10+5+3 = 103。
//   分布（每 roll 命中概率）：腐肉 ~29% / 骨头 ~24%（亡灵掉落族常见 ~53%）；
//   金锭 ~17% / 青金石 ~12% / 红石 ~10%（矿物族次常见 ~39%）；钻石 ~5%（稀有）；附魔书占位 ~3%（极稀有）。
//   机制对齐 MC 1.0 沙漠神殿战利品（骨头/腐肉亡灵族 + 钻石/金/青金石矿物族）。数量区间：常见件 1..N（一堆），
//   稀有件恒 1（单件，避免一堆钻石）。static 局部 + 返回 const 引用（单一权威；调用方不持副本）。
const std::vector<LootTable::Entry> &LootTable::pyramidChestPool()
{
    static const std::vector<Entry> pool = {
        { RecipeRegistry::RottenFleshId,  30, 1, 4 }, // 腐肉：亡灵族常见（机制等价 MC 沙漠神殿腐肉），1..4
        { RecipeRegistry::BoneId,         25, 1, 5 }, // 骨头：亡灵族常见（spec「骨头」），1..5
        { RecipeRegistry::GoldIngotId,    18, 1, 3 }, // 金锭：次常见金属（spec「金」），1..3
        { RecipeRegistry::LapisId,        12, 1, 3 }, // 青金石：次常见矿物（spec「青金石」），1..3
        { RecipeRegistry::RedstoneId,     10, 1, 4 }, // 红石粉：次常见矿物，1..4
        { RecipeRegistry::DiamondId,       5, 1, 1 }, // 钻石：稀有矿物（spec「钻石」），单件
        { RecipeRegistry::EnchantedBookId, 3, 1, 1 }, // 附魔书占位：极稀有，单件
    };
    return pool;
}

// t486 丛林神殿箱子战利品池（见 loottable.h 头注释）。权重总和 = 28+24+16+12+10+5+4+3+2 = 104。
//   分布（每 roll 命中概率）：腐肉 ~27% / 骨头 ~23%（亡灵族常见 ~50%）；铁锭 ~15% / 金锭 ~12%（金属族次常见
//   ~27%）；箭 ~10%（发射器陷阱配套弹药，机制等价 MC 丛林神殿多箭）；钻石 ~5%（稀有）；马鞍 ~4% / 命名牌 ~3%
//   （稀有件 ~7%）；附魔书占位 ~2%（极稀有）。机制对齐 MC 1.0 丛林神殿战利品（骨头/腐肉亡灵族 + 金属族 + 箭
//   + 稀有件）。数量区间：常见件 1..N（一堆），稀有件恒 1（单件，避免一堆钻石/马鞍）。static 局部 + 返回
//   const 引用（单一权威；调用方不持副本）。
const std::vector<LootTable::Entry> &LootTable::jungleTempleChestPool()
{
    static const std::vector<Entry> pool = {
        { RecipeRegistry::RottenFleshId,  28, 1, 4 }, // 腐肉：亡灵族常见（机制等价 MC 丛林神殿腐肉），1..4
        { RecipeRegistry::BoneId,         24, 1, 5 }, // 骨头：亡灵族常见（spec 配套骸骨陷阱），1..5
        { RecipeRegistry::IronIngotId,    16, 1, 4 }, // 铁锭：次常见金属，1..4
        { RecipeRegistry::GoldIngotId,    12, 1, 3 }, // 金锭：次常见金属，1..3
        { RecipeRegistry::ArrowId,        10, 2, 8 }, // 箭：发射器陷阱配套弹药（机制等价 MC 丛林神殿多箭），2..8
        { RecipeRegistry::DiamondId,       5, 1, 1 }, // 钻石：稀有矿物，单件
        { RecipeRegistry::SaddleId,        4, 1, 1 }, // 马鞍：稀有件，单件
        { RecipeRegistry::NameTagId,       3, 1, 1 }, // 命名牌：稀有件，单件
        { RecipeRegistry::EnchantedBookId, 2, 1, 1 }, // 附魔书占位：极稀有，单件
    };
    return pool;
}

// 按 weight 有放回加权抽 rolls 次。RNG 由 seed 确定（QRandomGenerator(seed)；PLAN §2-K 精神：同 seed 同产物）。
//   pool 为空 / rolls<=0 → 空。weight<=0 条目跳过（不入总权重；若全 <=0 → 空）。maxCount<minCount → 取 minCount。
std::vector<LootTable::Stack> LootTable::roll(const std::vector<Entry> &pool, int rolls, quint32 seed)
{
    std::vector<Stack> out;
    if (rolls <= 0) return out;
    int totalWeight = 0;
    for (const Entry &e : pool)
        if (e.weight > 0) totalWeight += e.weight;
    if (totalWeight <= 0) return out; // 池无有效条目 → 空产物（不崩；caller 据空 vector 处理）

    QRandomGenerator rng(seed);
    out.reserve(size_t(rolls));
    for (int i = 0; i < rolls; ++i) {
        // 加权抽取：在 [0, totalWeight) 取一随机数，逐条累加权重直到超过 → 命中该条。
        int pick = int(rng.bounded(totalWeight)); // [0, totalWeight)
        const Entry *hit = nullptr;
        int acc = 0;
        for (const Entry &e : pool) {
            if (e.weight <= 0) continue;
            acc += e.weight;
            if (pick < acc) { hit = &e; break; }
        }
        if (!hit) continue; // 理论不可达（totalWeight>0 保证命中）；防御性跳过
        // 数量区间：[minCount, maxCount] 均匀随机；maxCount<minCount → 取 minCount（兜底）。
        const int lo = hit->minCount;
        const int hi = (hit->maxCount >= lo) ? hit->maxCount : lo;
        const int count = (hi > lo) ? (lo + int(rng.bounded(hi - lo + 1))) : lo; // [lo, hi] 含两端
        out.push_back(Stack{ hit->itemId, count });
    }
    return out;
}
