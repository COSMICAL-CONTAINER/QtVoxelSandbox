#include "cheststore.h"

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

int ChestStore::slotIdAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerChest) return 0; // 越界 → 空栈
    const auto it = m_chests.constFind(key(x, y, z));
    if (it == m_chests.constEnd()) return 0;            // 无此箱子条目 → 空槽
    return it->at(size_t(index)).id;
}

int ChestStore::slotCountAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerChest) return 0;
    const auto it = m_chests.constFind(key(x, y, z));
    if (it == m_chests.constEnd()) return 0;
    return it->at(size_t(index)).count;
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
