#include "loottable.h"

#include "enchantregistry.h" // review L7 enchantedBookEnchants：selectEnchantsForItem(BookId) 随机附魔（t959 起主类别成簇）
#include "recipe.h" // 材料段 id 常量（CoalId / RedstoneId / ...）+ BookId（t824 起附魔选择按物品；同层 Game，向下依赖 Core）

#include <QRandomGenerator>

// 战利品表（t393）实现。纯静态数据 + 纯函数 —— 无 Q_OBJECT / 无实例 / 无 World 依赖（PLAN §2 分层）。

// 地牢箱子战利品池（10 条）。权重总和 = 30+30+25+20+18+8+6+4+2+10 = 153。
//   分布（每 roll 命中概率）：煤 / 面包 ~20% 各、线 ~16%、红石 ~13%、铁锭 ~12%（常见材料 ~81%）；
//   可可豆 ~6.5%（t1092——MC 1.0 地牢池原行字面：Beta 1.2（2011-01）起在池、早于 1.0.0 正式版 =
//   1.0 基准内，行字段权重 10 / 数量 1..2 按 1.0 原值落表；百分比差于 MC 原池 ~10.4% 系两池权重
//   基数不同，非口径漂移）；马鞍 ~5.2%、命名牌 ~3.9%（稀有 ~9%）；附魔书占位 ~2.6%（极稀有）；
//   音乐盘（琥珀旋律）~1.3%（t1083 极稀有收藏件——机制对齐 MC 1.0 dungeon chest 的音乐盘稀有掉落
//   位）。数量区间：常见材料 1..N（一堆），稀有件恒 1（单件，避免一堆马鞍 / 盘）——可可豆 1..2 为
//   MC 1.0 原行字面（唯一例外）。static 局部 + 函数返回 const 引用 —— 首次调用构造、后续零开销；
//   调用方不持副本（单一权威）。
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
        { RecipeRegistry::MusicDiscAmberId, 2, 1, 1 }, // t1083 音乐盘（琥珀旋律）：极稀有收藏件，单件
        { RecipeRegistry::CocoaBeanId,   10, 1, 2 }, // t1092 可可豆：MC 1.0 原行字面（权重 10 / 1..2）；棕染料唯一生存源
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

// t484 废弃矿井箱子战利品池（见 loottable.h 头注释）。权重总和 = 30+25+20+12+10+5+3+2+10+1 = 118。
//   分布（每 roll 命中概率）：煤 ~25% / 红石 ~21% / 铁锭 ~17%（常见矿物 ~63%）；
//   金锭 ~10% / 青金石 ~8.5%（次常见 ~18.5%）；瓜种 ~8.5%（t1103：MC Beta 1.8 起矿井运输箱即携瓜种
//   ——1.0 基准内的西瓜唯一战利品行位；pre-1.3 逐项权重活 wiki 未逐项留档，行位/数量区间按时代原值
//   （种子族 2..4）、权重按种子族惯例 10 落表（t1092 可可豆行同门沿革注），行位沿革注即本注）；
//   钻石 ~4.2%（稀有）；附魔书占位 ~2.5%（极稀有）；音乐盘（深巷回声）~1.7%（t1083 极稀有收藏件
//   ——机制对齐 MC 1.0 矿井箱的音乐盘稀有掉落位）；金苹果 ~0.8%（t1116：1.0 地牢箱金苹果口径——
//   MC 1.0.0 地牢箱池 11 条目之 1 行 / 权重 1 / 数量恒 1 件，本工程按原值落表；t484「矿物族替代」
//   面随本行解除，苹果获取面三路闭环不变[橡树叶 1/200 + 金苹果合成 + 本行]）。
//   机制对齐 MC 1.0 mineshaft chest「矿物多 / 附魔书铁锭常见 / 钻石稀有」分布 + 1.0 地牢箱金苹果
//   稀有件位。数量区间：常见矿物 1..N（一堆），稀有件恒 1（单件，避免一堆钻石/金苹果）。static
//   局部 + 返回 const 引用（单一权威；调用方不持副本）。
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
        { RecipeRegistry::MusicDiscEchoId, 2, 1, 1 }, // t1083 音乐盘（深巷回声）：极稀有收藏件，单件
        { RecipeRegistry::MelonSeedsId,   10, 2, 4 }, // t1103 瓜种：矿井运输箱原行位（Beta 1.8 起即携；种子族 2..4 / 权重 10 惯例，沿革注见头注）
        { RecipeRegistry::GoldenAppleId,  1, 1, 1 }, // t1116 金苹果：1.0 地牢箱金苹果口径（11 条目之 1 行 / 权重 1 / 恒 1 件）；t484「矿物族替代」面解除，段尾追加
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

// t487 要塞箱子战利品池（见 loottable.h 头注释）。权重总和 = 28+24+16+12+10+5+3+2 = 100。
//   分布（每 roll 命中概率）：腐肉 ~28% / 骨头 ~24%（亡灵族常见 ~52%）；铁锭 ~16%（金属族次常见）；青金石
//   ~12% / 红石 ~10%（矿物族次常见 ~22%）；暗渊之眼 ~6%（稀有关键件 —— 激活传送门的关键物品，机制等价 MC
//   1.0 要塞暗渊之眼掉落，低权重须探索多箱）；钻石 ~5%（稀有）；附魔书占位 ~2%（极稀有）。机制对齐 MC 1.0
//   要塞战利品（暗渊之眼标志性掉落 + 亡灵族 + 矿物族 + 稀有件）。数量区间：常见件 1..N（一堆），稀有件恒 1
//   （单件，避免一堆钻石/暗渊之眼）。static 局部 + 返回 const 引用（单一权威；调用方不持副本）。
const std::vector<LootTable::Entry> &LootTable::strongholdChestPool()
{
    static const std::vector<Entry> pool = {
        { RecipeRegistry::RottenFleshId,  28, 1, 4 }, // 腐肉：亡灵族常见（要塞石砖房阴森环境），1..4
        { RecipeRegistry::BoneId,         24, 1, 5 }, // 骨头：亡灵族常见，1..5
        { RecipeRegistry::IronIngotId,    16, 1, 4 }, // 铁锭：次常见金属（要塞藏物），1..4
        { RecipeRegistry::LapisId,        12, 1, 3 }, // 青金石：次常见矿物（附魔前置材料），1..3
        { RecipeRegistry::RedstoneId,     10, 1, 4 }, // 红石粉：次常见矿物，1..4
        { RecipeRegistry::EndEyeId,        6, 1, 1 }, // 暗渊之眼：稀有关键件（激活传送门，机制等价 MC 1.0 要塞掉落），单件
        { RecipeRegistry::DiamondId,       5, 1, 1 }, // 钻石：稀有矿物，单件
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

// review L7 战利品附魔书随机附魔（见 .h 头注释）。两段 RNG：① QRandomGenerator(seed) 抽 offeredLevel
//   [5,25)（低中档强度——跨选择器的 10/20 阈值 → 附魔数 1..3 条均可能，机制等价 MC loot 书
//   附魔数随机）；② 选择器内部确定性 LCG（同 seed 同结果，PLAN §2-K）。产物打包为
//   ItemStack.enchants[4] 同构 QVariantList<int>（pack 值；不足 4 条按 0 补齐）。
QVariantList LootTable::enchantedBookEnchants(quint32 seed)
{
    QRandomGenerator rng(seed);
    const int offered = 5 + int(rng.bounded(20)); // [5, 25)：<10 → 1 条 / 10..19 → 2 条 / >=20 → 3 条
    const QVariantList picks = EnchantRegistry::selectEnchantsForItem(
        RecipeRegistry::BookId, offered, int(rng.generate())); // t824 按物品 + t959 书池主类别成簇（选择器单一权威内部收窄）
    QVariantList packed;
    packed.reserve(4);
    for (int i = 0; i < 4; ++i) {
        if (i < picks.size()) {
            const QVariantMap m = picks.at(i).toMap();
            packed.append(EnchantRegistry::pack(m.value(QStringLiteral("id")).toInt(),
                                                m.value(QStringLiteral("level")).toInt()));
        } else {
            packed.append(0); // 空槽哨兵（不足 4 条按 0 补齐）
        }
    }
    return packed;
}
