#include "brewingstore.h"

#include "blockregistry.h"

// t1097 酿造台内容存储实现（对齐 HopperStore / FurnaceStore 族口径；头注 = brewingstore.h）。
// 物品栈 (id, count)，id=0 空栈；写入即自动建条目（同 ChestStore / DispenserStore / HopperStore）。

// ── 酿造配方表（静态纯数据 = Game 层单一权威；机制 tick / 矩阵探针同源调用）──────────────────────
// MC 1.0 酿造链核实（wiki 2026 实读；机制等价实现，§9 原创名：地狱疣→灰烬疣 / 烈焰粉→燃烬粉）：
//   水瓶 + 灰烬疣 → 粗制药水（基础链）；粗制 + 糖 → 迅捷药水；粗制 + 燃烬粉 → 力量药水。
//   t1099 第二轮（粗制 + 效果原料 → 效果药水五链，wiki 口径与获取链裁定见 recipe.h 0x26B..0x274 注）：
//   粗制 + 岩浆膏 → 火抗（3:00）/ 粗制 + 幽灵泪 → 再生（0:45）/ 粗制 + 蜘蛛眼 → 中毒（0:45）/
//   粗制 + 发酵蛛眼 → 虚弱（1:30）/ 粗制 + 闪烁西瓜 → 瞬间治疗（即时）。
//   候选池登记（本表尾追加扩展位）：水瓶直酿行（1.0 水瓶+发酵蛛眼 → 虚弱 / +其余原料 → 凡庸药水）、
//   延长/强化二级链（红石 / 辉光岩）、喷溅药水。
int BrewingStore::brewResult(int ingredientId, int bottleId)
{
    // t1100 二级酿造门行（MC 1.0 redstone modifier）：红石粉只作用于**成品效果药水** → 对应延长版。
    //   水 / 粗制 / 瞬间治疗（1.0 口径：即时效果不可延长）/ 非瓶 → extendedPotionResult 答零 = 无映射。
    //   NEG 面登记：本门行 + extendedPotionResult 本体 = t1100 二级酿造 NEG-1 恰红触达面（摘除后
    //   编译仍绿——函数仅此一处调用、声明在 .h 幸存——行为柱 r2070a 恰红，其余腿不受影响）。
    if (ingredientId == RecipeRegistry::RedstoneId)
        return extendedPotionResult(bottleId);
    // t1101 三级酿造门行（MC 1.0 gunpowder modifier）：火药只作用于**成品药水（基础 6 + 延长 6）**
    //   → 对应喷溅版 ×12。水（任务负例口径：喷溅水瓶不交付）/ 粗制（无效果载体不可喷溅化）/ 瞬间
    //   治疗（即时效果喷溅 = 效能衰减面，候选池登记）/ 喷溅版再酿（喷溅 + 火药 = 无映射，任务负例）/
    //   非瓶 → splashPotionResult 答零 = 无映射。
    //   NEG 面登记：本门行 + splashPotionResult 本体 = t1101 火药映射 NEG-1 恰红触达面（摘除后编译
    //   仍绿——函数仅此一处调用、声明在 .h 幸存——行为柱 r2071a 恰红，其余腿不受影响）。
    if (ingredientId == RecipeRegistry::GunpowderId)
        return splashPotionResult(bottleId);
    if (bottleId == RecipeRegistry::WaterBottleId)
        return ingredientId == RecipeRegistry::AshWartId ? RecipeRegistry::AwkwardPotionId : 0;
    if (bottleId == RecipeRegistry::AwkwardPotionId)
    {
        if (ingredientId == RecipeRegistry::SugarId)
            return RecipeRegistry::SpeedPotionId;
        if (ingredientId == RecipeRegistry::BlazePowderId)
            return RecipeRegistry::StrengthPotionId;
        // t1099 第二轮五链（效果原料各自唯一 → if 链平铺，同上两行形态）。
        if (ingredientId == RecipeRegistry::MagmaCreamId)
            return RecipeRegistry::FireResistancePotionId;
        if (ingredientId == RecipeRegistry::GhastTearId)
            return RecipeRegistry::RegenerationPotionId;
        if (ingredientId == RecipeRegistry::SpiderEyeId)
            return RecipeRegistry::PoisonPotionId;
        if (ingredientId == RecipeRegistry::FermentedSpiderEyeId)
            return RecipeRegistry::WeaknessPotionId;
        if (ingredientId == RecipeRegistry::GlisteringMelonId)
            return RecipeRegistry::InstantHealthPotionId;
    }
    return 0;
}

// 燃料燃烧值：燃烬粉 = 20 次酿造（MC 1.0 blaze powder fuel 20 原值）；非燃料 → 0。
int BrewingStore::fuelOpsFor(int itemId)
{
    return itemId == RecipeRegistry::BlazePowderId ? kPowderFuelOps : 0;
}

// ── t1100 二级酿造映射小表（成品效果药水 → 延长版产物；头注见 brewingstore.h）──────────────────
// MC 1.0 redstone modifier 口径核实（wiki 2026 实读三元组留痕，机制等价实现）：红石粉作用在**成品
//   效果药水**上 = 延长效果时长（延长值逐链见 playercontroller.h 延长常量族）；水瓶 + 红石 / 粗制 +
//   红石 = 无映射（1.0 基准水瓶直酿行不在本单交付面，候选池登记）；瞬间治疗 + 红石 = 无映射（即时
//   效果无时长可延长，MC 1.0 无 extended healing 变体）。表形态留痕：6 行成对映射取独立 switch 小表
//   （非 brewResult if 链平铺——if 链已 8 行，平铺损可读性；小表 = 映射单一权威 + 负例面集中）。
int BrewingStore::extendedPotionResult(int potionId)
{
    switch (potionId) {
    case RecipeRegistry::SpeedPotionId:          return RecipeRegistry::ExtendedSpeedPotionId;
    case RecipeRegistry::StrengthPotionId:       return RecipeRegistry::ExtendedStrengthPotionId;
    case RecipeRegistry::FireResistancePotionId: return RecipeRegistry::ExtendedFireResistancePotionId;
    case RecipeRegistry::RegenerationPotionId:   return RecipeRegistry::ExtendedRegenerationPotionId;
    case RecipeRegistry::PoisonPotionId:         return RecipeRegistry::ExtendedPoisonPotionId;
    case RecipeRegistry::WeaknessPotionId:       return RecipeRegistry::ExtendedWeaknessPotionId;
    default: return 0; // 水 / 粗制 / 瞬间治疗 / 非瓶 / 延长版再酿（延长版 + 红石 = 无二级映射，MC 口径）
    }
}

// ── t1101 三级酿造映射小表（成品药水 → 喷溅版产物；extendedPotionResult 同门）────────────────────
// MC 1.0 gunpowder modifier 口径核实（wiki 2026 实读留痕，机制等价实现）：火药作用在**成品药水**上 =
//   对应喷溅版（基础 6 + 延长 6 各一喷溅位，id 段位见 recipe.h 0x27B..0x286 注）；水瓶 + 火药 = 无映射
//   （任务负例口径：喷溅水瓶不交付，候选池登记）；粗制 + 火药 = 无映射（无效果载体不可喷溅化）；
//   瞬间治疗 + 火药 = 无映射（即时效果喷溅 = 效能邻近衰减面，本单不交付，候选池登记）；喷溅版 + 火药
//   再酿 = 无映射（喷溅版非酿造底物）。表形态留痕：12 行成对映射取独立 switch 小表（t1100 同门——
//   小表 = 映射单一权威 + 负例面集中）。喷溅版效果 / 时长结算面 = playercontroller（splashEffectType /
//   splashBaseSeconds / applySplashPotion，Game 层单一权威）。
int BrewingStore::splashPotionResult(int potionId)
{
    switch (potionId) {
    case RecipeRegistry::SpeedPotionId:                  return RecipeRegistry::SplashSpeedPotionId;
    case RecipeRegistry::StrengthPotionId:               return RecipeRegistry::SplashStrengthPotionId;
    case RecipeRegistry::FireResistancePotionId:         return RecipeRegistry::SplashFireResistancePotionId;
    case RecipeRegistry::RegenerationPotionId:           return RecipeRegistry::SplashRegenerationPotionId;
    case RecipeRegistry::PoisonPotionId:                 return RecipeRegistry::SplashPoisonPotionId;
    case RecipeRegistry::WeaknessPotionId:               return RecipeRegistry::SplashWeaknessPotionId;
    case RecipeRegistry::ExtendedSpeedPotionId:          return RecipeRegistry::SplashExtendedSpeedPotionId;
    case RecipeRegistry::ExtendedStrengthPotionId:       return RecipeRegistry::SplashExtendedStrengthPotionId;
    case RecipeRegistry::ExtendedFireResistancePotionId: return RecipeRegistry::SplashExtendedFireResistancePotionId;
    case RecipeRegistry::ExtendedRegenerationPotionId:   return RecipeRegistry::SplashExtendedRegenerationPotionId;
    case RecipeRegistry::ExtendedPoisonPotionId:         return RecipeRegistry::SplashExtendedPoisonPotionId;
    case RecipeRegistry::ExtendedWeaknessPotionId:       return RecipeRegistry::SplashExtendedWeaknessPotionId;
    default: return 0; // 水 / 粗制 / 瞬间治疗 / 喷溅版再酿 / 非瓶 / 延长版喷溅再喷溅（火药对喷溅版无映射）
    }
}

BrewingStore::BrewingStore(QObject *parent) : QObject(parent) {}

QString BrewingStore::key(int x, int y, int z)
{
    return QStringLiteral("%1,%2,%3").arg(x).arg(y).arg(z);
}

bool BrewingStore::parseKey(const QString &k, int &x, int &y, int &z)
{
    const QStringList parts = k.split(QLatin1Char(','));
    if (parts.size() != 3) return false;
    bool okx = false, oky = false, okz = false;
    x = parts.at(0).toInt(&okx);
    y = parts.at(1).toInt(&oky);
    z = parts.at(2).toInt(&okz);
    return okx && oky && okz;
}

int BrewingStore::slotIdAt(int x, int y, int z, int index) const
{
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end() || index < 0 || index >= kSlotsPerBrewing) return 0;
    return it->second[index].id;
}

int BrewingStore::slotCountAt(int x, int y, int z, int index) const
{
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end() || index < 0 || index >= kSlotsPerBrewing) return 0;
    return it->second[index].count;
}

int BrewingStore::slotDurabilityAt(int x, int y, int z, int index) const
{
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end() || index < 0 || index >= kSlotsPerBrewing) return 0;
    return it->second[index].durability;
}

QVariantList BrewingStore::slotEnchantsAt(int x, int y, int z, int index) const
{
    QVariantList out { 0, 0, 0, 0 };
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end() || index < 0 || index >= kSlotsPerBrewing) return out;
    for (int i = 0; i < 4; ++i) out[i] = it->second[index].enchants[i];
    return out;
}

QString BrewingStore::slotNameAt(int x, int y, int z, int index) const
{
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end() || index < 0 || index >= kSlotsPerBrewing) return QString();
    return it->second[index].name;
}

void BrewingStore::setSlot(int x, int y, int z, int index, int id, int count,
                         const QVariantList &enchants, const QString &name, int durability)
{
    if (index < 0 || index >= kSlotsPerBrewing) return;
    Entry &h = m_stands[key(x, y, z)]; // 写入即建条目（同族口径）
    Slot &s = h[index];
    if (count <= 0) id = 0; // count 归一在先，防 {id>0,count=0} 幽灵栈（同 DispenserStore t607）
    if (id <= 0) {
        s = Slot();
    } else {
        s.id = id;
        s.count = count;
        for (int i = 0; i < 4 && i < enchants.size(); ++i) s.enchants[i] = enchants.at(i).toInt();
        for (int i = int(enchants.size()); i < 4; ++i) s.enchants[i] = 0;
        s.name = name;
        s.durability = durability;
    }
    ++m_revision;
    emit brewingChanged();
}

bool BrewingStore::hasBrewing(int x, int y, int z) const
{
    return m_stands.find(key(x, y, z)) != m_stands.end();
}

void BrewingStore::clearBrewing(int x, int y, int z)
{
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end()) return;
    m_stands.erase(it);
    ++m_revision;
    emit brewingChanged();
}

void BrewingStore::clearAll()
{
    if (m_stands.empty()) return;
    m_stands.clear();
    ++m_revision;
    emit brewingChanged();
}

QVariantList BrewingStore::allBrewingStands() const
{
    QVariantList out;
    int x = 0, y = 0, z = 0;
    for (const auto &kv : m_stands) {
        if (!parseKey(kv.first, x, y, z)) continue;
        QVariantMap hm;
        hm.insert(QStringLiteral("x"), x);
        hm.insert(QStringLiteral("y"), y);
        hm.insert(QStringLiteral("z"), z);
        QVariantList slotList;
        for (int i = 0; i < kSlotsPerBrewing; ++i) {
            QVariantMap sm;
            sm.insert(QStringLiteral("id"), kv.second[i].id);
            sm.insert(QStringLiteral("count"), kv.second[i].count);
            slotList.append(sm);
        }
        hm.insert(QStringLiteral("slots"), slotList);
        hm.insert(QStringLiteral("progress"), kv.second.progress);
        hm.insert(QStringLiteral("fuelOps"), kv.second.fuelOps);
        out.append(hm);
    }
    return out;
}

void BrewingStore::loadAll(const QVariantList &stands)
{
    m_stands.clear();
    for (const QVariant &v : stands) {
        const QVariantMap hm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = hm.value(QStringLiteral("x")).toInt(&okx);
        const int y = hm.value(QStringLiteral("y")).toInt(&oky);
        const int z = hm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue;
        Entry &h = m_stands[key(x, y, z)];
        const QVariantList slotList = hm.value(QStringLiteral("slots")).toList();
        for (int i = 0; i < kSlotsPerBrewing && i < slotList.size(); ++i) {
            const QVariantMap sm = slotList.at(i).toMap();
            h[i].id = sm.value(QStringLiteral("id")).toInt();
            h[i].count = sm.value(QStringLiteral("count")).toInt();
            if (h[i].count <= 0) { h[i] = Slot(); continue; }
        }
        h.progress = hm.value(QStringLiteral("progress")).toDouble();
        h.fuelOps = hm.value(QStringLiteral("fuelOps")).toInt();
    }
    ++m_revision;
    emit brewingChanged();
}

qreal BrewingStore::brewProgressAt(int x, int y, int z) const
{
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end()) return 0.0;
    return it->second.progress;
}

int BrewingStore::fuelOpsAt(int x, int y, int z) const
{
    const auto it = m_stands.find(key(x, y, z));
    if (it == m_stands.end()) return 0;
    return it->second.fuelOps;
}

void BrewingStore::setBrewProgress(int x, int y, int z, qreal val)
{
    m_stands[key(x, y, z)].progress = val < 0.0 ? 0.0 : val; // 写入即建条目
    ++m_revision;
    emit brewingChanged();
}

void BrewingStore::setFuelOps(int x, int y, int z, int val)
{
    m_stands[key(x, y, z)].fuelOps = val < 0 ? 0 : val; // 写入即建条目
    ++m_revision;
    emit brewingChanged();
}

// 引擎键快照（scanBrewingStands 遍历用；HopperStore::hopperKeys 同门——值拷贝防迭代失效）。
QStringList BrewingStore::standKeys() const
{
    QStringList out;
    out.reserve(int(m_stands.size()));
    for (const auto &kv : m_stands) out.append(kv.first);
    return out;
}
