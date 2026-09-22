#include "hopperstore.h"

#include "blockregistry.h" // maxStackSize（全 id 段堆叠上限单一权威；Game->Core 向下合法）

// t1080 漏斗内容存储实现（形状 / 口径逐字对齐 DispenserStore 族；头注 = hopperstore.h）。
// 物品栈 (id, count)，id=0 空栈；自动建条目（写入即建，同 ChestStore / DispenserStore）。
HopperStore::HopperStore(QObject *parent) : QObject(parent) {}

QString HopperStore::key(int x, int y, int z)
{
    return QStringLiteral("%1,%2,%3").arg(x).arg(y).arg(z);
}

bool HopperStore::parseKey(const QString &k, int &x, int &y, int &z)
{
    const QStringList parts = k.split(QLatin1Char(','));
    if (parts.size() != 3) return false;
    bool okx = false, oky = false, okz = false;
    x = parts.at(0).toInt(&okx);
    y = parts.at(1).toInt(&oky);
    z = parts.at(2).toInt(&okz);
    return okx && oky && okz;
}

int HopperStore::slotIdAt(int x, int y, int z, int index) const
{
    const auto it = m_hoppers.find(key(x, y, z));
    if (it == m_hoppers.end() || index < 0 || index >= kSlotsPerHopper) return 0;
    return it->second[index].id;
}

int HopperStore::slotCountAt(int x, int y, int z, int index) const
{
    const auto it = m_hoppers.find(key(x, y, z));
    if (it == m_hoppers.end() || index < 0 || index >= kSlotsPerHopper) return 0;
    return it->second[index].count;
}

int HopperStore::slotDurabilityAt(int x, int y, int z, int index) const
{
    const auto it = m_hoppers.find(key(x, y, z));
    if (it == m_hoppers.end() || index < 0 || index >= kSlotsPerHopper) return 0;
    return it->second[index].durability;
}

QVariantList HopperStore::slotEnchantsAt(int x, int y, int z, int index) const
{
    QVariantList out { 0, 0, 0, 0 };
    const auto it = m_hoppers.find(key(x, y, z));
    if (it == m_hoppers.end() || index < 0 || index >= kSlotsPerHopper) return out;
    for (int i = 0; i < 4; ++i) out[i] = it->second[index].enchants[i];
    return out;
}

QString HopperStore::slotNameAt(int x, int y, int z, int index) const
{
    const auto it = m_hoppers.find(key(x, y, z));
    if (it == m_hoppers.end() || index < 0 || index >= kSlotsPerHopper) return QString();
    return it->second[index].name;
}

void HopperStore::setSlot(int x, int y, int z, int index, int id, int count,
                         const QVariantList &enchants, const QString &name, int durability)
{
    if (index < 0 || index >= kSlotsPerHopper) return;
    Entry &h = m_hoppers[key(x, y, z)]; // 写入即建条目（同族口径）
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
    emit hopperChanged();
}

bool HopperStore::hasHopper(int x, int y, int z) const
{
    return m_hoppers.find(key(x, y, z)) != m_hoppers.end();
}

void HopperStore::clearHopper(int x, int y, int z)
{
    const auto it = m_hoppers.find(key(x, y, z));
    if (it == m_hoppers.end()) return; // 无条目 no-op（不无故发信号，同族口径）
    m_hoppers.erase(it);
    ++m_revision;
    emit hopperChanged();
}

void HopperStore::clearAll()
{
    if (m_hoppers.empty()) return; // 空 no-op（不无故发信号，同族口径）
    m_hoppers.clear();
    ++m_revision;
    emit hopperChanged();
}

QVariantList HopperStore::allHoppers() const
{
    QVariantList out;
    int x = 0, y = 0, z = 0;
    for (const auto &kv : m_hoppers) {
        if (!parseKey(kv.first, x, y, z)) continue;
        QVariantMap hm;
        hm.insert(QStringLiteral("x"), x);
        hm.insert(QStringLiteral("y"), y);
        hm.insert(QStringLiteral("z"), z);
        QVariantList slotList;
        for (int i = 0; i < kSlotsPerHopper; ++i) {
            QVariantMap sm;
            sm.insert(QStringLiteral("id"), kv.second[i].id);
            sm.insert(QStringLiteral("count"), kv.second[i].count);
            slotList.append(sm);
        }
        hm.insert(QStringLiteral("slots"), slotList);
        out.append(hm);
    }
    return out;
}

void HopperStore::loadAll(const QVariantList &hoppers)
{
    m_hoppers.clear();
    for (const QVariant &v : hoppers) {
        const QVariantMap hm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = hm.value(QStringLiteral("x")).toInt(&okx);
        const int y = hm.value(QStringLiteral("y")).toInt(&oky);
        const int z = hm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue;
        Entry &h = m_hoppers[key(x, y, z)];
        const QVariantList slotList = hm.value(QStringLiteral("slots")).toList();
        for (int i = 0; i < kSlotsPerHopper && i < slotList.size(); ++i) {
            const QVariantMap sm = slotList.at(i).toMap();
            h[i].id = sm.value(QStringLiteral("id")).toInt();
            h[i].count = sm.value(QStringLiteral("count")).toInt();
            if (h[i].count <= 0) { h[i] = Slot(); continue; }
        }
    }
    ++m_revision;
    emit hopperChanged();
}

// 引擎插入原语（收集 / 接收推送共用；非 Q_INVOKABLE——机制决策零 QML）：把 count 件 id 物品插入
// 容腔，返回实际接受件数。路径：同 id 未满槽合并（仅元数据全空才堆叠）→ 首个空槽；满仓 / 无空位 →
// 已接受多少返多少（0 = 挂起，实体 / 栈原地保留）。cap = BlockRegistry::maxStackSize（全 id 段单一权威）。
int HopperStore::insertStack(int x, int y, int z, int id, int count, const QVariantList &enchants,
                            const QString &name, int durability)
{
    if (id <= 0 || count <= 0) return 0;
    Entry &h = m_hoppers[key(x, y, z)];
    const int cap = BlockRegistry::maxStackSize(id);
    int left = count;
    // ① 同 id 未满槽合并（元数据全空才堆叠——cap=1 物品 / 带附魔名耐久栈各占一槽）。附魔「全空」=
    //     全零表（slotEnchantsAt 读族恒返 4 元素表、isEmpty 恒 false——按 Hotbar 家族「0=无附魔」语义判）。
    const auto enchantsNone = [](const QVariantList &e) {
        for (const QVariant &v : e)
            if (v.toInt() != 0) return false;
        return true;
    };
    if (cap > 1 && enchantsNone(enchants) && name.isEmpty() && durability <= 0) {
        for (int i = 0; i < kSlotsPerHopper && left > 0; ++i) {
            Slot &s = h[i];
            if (s.id == id && s.count > 0 && s.count < cap) {
                const int take = qMin(left, cap - s.count);
                s.count += take;
                left -= take;
            }
        }
    }
    // ② 首个空槽落位（余量按 cap 分槽）。
    for (int i = 0; i < kSlotsPerHopper && left > 0; ++i) {
        Slot &s = h[i];
        if (s.id != 0 || s.count != 0) continue;
        const int take = qMin(left, cap);
        s.id = id;
        s.count = take;
        for (int e = 0; e < 4 && e < enchants.size(); ++e) s.enchants[e] = enchants.at(e).toInt();
        s.name = name;
        s.durability = durability;
        left -= take;
    }
    const int accepted = count - left;
    if (accepted > 0) {
        ++m_revision;
        emit hopperChanged();
    }
    return accepted;
}

QStringList HopperStore::hopperKeys() const
{
    QStringList out;
    out.reserve(int(m_hoppers.size()));
    for (const auto &kv : m_hoppers) out.append(kv.first);
    return out;
}

qreal HopperStore::hopperCooldown(int x, int y, int z) const
{
    const auto it = m_hoppers.find(key(x, y, z));
    return it == m_hoppers.end() ? 0.0 : it->second.cd;
}

void HopperStore::setHopperCooldown(int x, int y, int z, qreal cd)
{
    m_hoppers[key(x, y, z)].cd = cd; // 只写相位不 bump revision（纯簿记，无内容变更）
}
