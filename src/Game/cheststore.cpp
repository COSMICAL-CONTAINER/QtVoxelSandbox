#include "cheststore.h"

#include <QStringList>
#include <QVariantMap>

// 箱子内容存储（t173）实现。纯存储类 —— 无光 / 无 World 依赖；ChestUI + Main.qml 路由读写。
// 物品栈语义同 Hotbar（id=0=空，count<=0 视空），但不复用 Hotbar VM 的槽（箱子是独立方块内容器）。

ChestStore::ChestStore(QObject *parent)
    : QObject(parent)
{
}

// 坐标键 "x,y,z"。简单可读、无位打包范围限制（坐标可负 / 可大）；箱子数少，QString 哈希性能非热点。
QString ChestStore::key(int x, int y, int z)
{
    return QString::number(x) + QLatin1Char(',') + QString::number(y) + QLatin1Char(',') + QString::number(z);
}

// 反解 key() 产物（allChests 落盘时把 QString 键还原为坐标列）。坐标可负、可大 → toInt（非 toUInt）。
bool ChestStore::parseKey(const QString &k, int &x, int &y, int &z)
{
    const QStringList parts = k.split(QLatin1Char(','));
    if (parts.size() != 3) return false;
    bool ok = false;
    x = parts[0].toInt(&ok); if (!ok) return false;
    y = parts[1].toInt(&ok); if (!ok) return false;
    z = parts[2].toInt(&ok); if (!ok) return false;
    return true;
}

int ChestStore::slotIdAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerChest) return 0; // 越界 → 空栈
    const auto it = m_chests.find(key(x, y, z));
    if (it == m_chests.end()) return 0;            // 无此箱子条目 → 空槽
    return it->second.at(size_t(index)).id;
}

int ChestStore::slotCountAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerChest) return 0;
    const auto it = m_chests.find(key(x, y, z));
    if (it == m_chests.end()) return 0;
    return it->second.at(size_t(index)).count;
}

// 直接写某箱子某槽。index 越界忽略；id<=0 或 count<=0 → 清空该槽（保持空栈不变式：id==0 ⟺ count==0）。
// 自动建箱条目（首次写入某坐标即创建空 27 槽再写）。写入后 bump revision → ChestUI delegate 刷新。
void ChestStore::setSlot(int x, int y, int z, int index, int id, int count)
{
    if (index < 0 || index >= kSlotsPerChest) return;
    // 空栈归一：id<=0 或 count<=0 → 清空（id=0, count=0）。
    const int normId = (id > 0) ? id : 0;
    const int normCount = (normId > 0 && count > 0) ? count : 0;
    Chest &chest = m_chests[key(x, y, z)]; // 自动建条目（不存在则插入空 27 槽）
    chest[size_t(index)] = Slot{normId, normCount};
    ++m_revision;
    emit chestChanged();
}

// 移除某箱子条目（破块清孤儿）。不存在则 no-op（仍发 chestChanged 驱动任何残留绑定刷新，幂等安全）。
void ChestStore::clearChest(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_chests.erase(k) > 0) {
        ++m_revision;
        emit chestChanged();
    }
}

// t188 清空全部箱子（跨世界切换防泄漏）。空 → no-op（不无故发信号 / 不增 revision，幂等）。
void ChestStore::clearAll()
{
    if (m_chests.empty()) return;
    m_chests.clear();
    ++m_revision;
    emit chestChanged();
}

// t188 收集所有「含 ≥1 非空槽」的箱子为 QVariantList（落盘用）。全空箱子跳过（加载后缺失 = 空箱行为等价）。
//   形状：[{x,y,z, slots:[{id,count}×27]}, ...]。QString 键经 parseKey 还原为坐标列。
QVariantList ChestStore::allChests() const
{
    QVariantList out;
    for (auto it = m_chests.cbegin(); it != m_chests.cend(); ++it) {
        const Chest &chest = it->second;
        // 注意：局部变量名禁用 `slots`（Qt 关键字宏，Q_OBJECT 类内会被预处理器抹掉，见 lessons-learned）。
        QVariantList slotList;
        slotList.reserve(kSlotsPerChest);
        bool any = false;
        for (const Slot &s : chest) {
            QVariantMap sm;
            sm.insert(QStringLiteral("id"), s.id);
            sm.insert(QStringLiteral("count"), s.count);
            if (s.id > 0 && s.count > 0) any = true;
            slotList.append(sm);
        }
        if (!any) continue; // 全空箱子不落盘
        int x = 0, y = 0, z = 0;
        if (!parseKey(it->first, x, y, z)) continue; // 键损坏 → 跳过（不写残条目）
        QVariantMap cm;
        cm.insert(QStringLiteral("x"), x);
        cm.insert(QStringLiteral("y"), y);
        cm.insert(QStringLiteral("z"), z);
        cm.insert(QStringLiteral("slots"), slotList);
        out.append(cm);
    }
    return out;
}

// t188 用存档 QVariantList（同 allChests 形状）整体替换内存（先清空再填充；单次 emit chestChanged）。
//   替换语义 = 清旧世界残留 + 填本世界箱子（杜绝跨世界泄漏，同 t187 hotbarVM 模式）。空列表 → 仅清空。
//   slots 空栈归一同 setSlot（id<=0 或 count<=0 → 空）；index 越界 / 缺坐标 → 跳过该箱。
void ChestStore::loadAll(const QVariantList &chests)
{
    m_chests.clear();
    for (const QVariant &v : chests) {
        const QVariantMap cm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = cm.value(QStringLiteral("x")).toInt(&okx);
        const int y = cm.value(QStringLiteral("y")).toInt(&oky);
        const int z = cm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue;
        const QVariantList slotList = cm.value(QStringLiteral("slots")).toList();
        Chest chest; // 默认全空（Slot{id=0,count=0}）
        const int n = slotList.size();
        for (int i = 0; i < kSlotsPerChest && i < n; ++i) {
            const QVariantMap sm = slotList[i].toMap();
            const int id = sm.value(QStringLiteral("id")).toInt();
            const int count = sm.value(QStringLiteral("count")).toInt();
            chest[size_t(i)] = Slot{ id > 0 ? id : 0, (id > 0 && count > 0) ? count : 0 };
        }
        m_chests[key(x, y, z)] = chest;
    }
    ++m_revision;
    emit chestChanged(); // 状态整体替换 → 通知 ChestUI delegate 重读（即便结果为空，刷新到空态）
}
