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
