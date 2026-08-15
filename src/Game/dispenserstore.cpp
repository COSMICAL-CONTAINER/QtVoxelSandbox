#include "dispenserstore.h"

#include <QStringList>
#include <QVariantMap>

// 发射器内容存储（t542）实现。纯存储类 —— 无光 / 无 World 依赖；DispenserUI + Main.qml 路由读写。
// 物品栈语义同 Hotbar（id=0=空，count<=0 视空），但不复用 Hotbar VM 的槽（发射器是独立方块内容器）。
// 结构对齐 cheststore.cpp / furnacestore.cpp（按坐标键控 + revision NOTIFY + all/load 落盘），仅槽位数（9）
// 与箱子（27）/ 熔炉（3）不同；无冶炼进度字段（发射器是纯存储容器）。

DispenserStore::DispenserStore(QObject *parent)
    : QObject(parent)
{
}

// 坐标键 "x,y,z"。简单可读、无位打包范围限制（坐标可负 / 可大）；发射器数少，QString 哈希性能非热点（同
//   ChestStore / FurnaceStore）。
QString DispenserStore::key(int x, int y, int z)
{
    return QString::number(x) + QLatin1Char(',') + QString::number(y) + QLatin1Char(',') + QString::number(z);
}

// 反解 key() 产物（allDispensers 落盘时把 QString 键还原为坐标列）。坐标可负、可大 → toInt（非 toUInt）。
bool DispenserStore::parseKey(const QString &k, int &x, int &y, int &z)
{
    const QStringList parts = k.split(QLatin1Char(','));
    if (parts.size() != 3) return false;
    bool ok = false;
    x = parts[0].toInt(&ok); if (!ok) return false;
    y = parts[1].toInt(&ok); if (!ok) return false;
    z = parts[2].toInt(&ok); if (!ok) return false;
    return true;
}

int DispenserStore::slotIdAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerDispenser) return 0; // 越界 → 空栈
    const auto it = m_dispensers.find(key(x, y, z));
    if (it == m_dispensers.end()) return 0;            // 无此发射器条目 → 空槽
    return it->second.at(size_t(index)).id;
}

int DispenserStore::slotCountAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerDispenser) return 0;
    const auto it = m_dispensers.find(key(x, y, z));
    if (it == m_dispensers.end()) return 0;
    return it->second.at(size_t(index)).count;
}

// 直接写某发射器某槽。index 越界忽略；id<=0 或 count<=0 → 清空该槽（保持空栈不变式：id==0 ⟺ count==0）。
// 自动建发射器条目（首次写入某坐标即创建空 9 槽再写）。写入后 bump revision → DispenserUI delegate 刷新。
void DispenserStore::setSlot(int x, int y, int z, int index, int id, int count)
{
    if (index < 0 || index >= kSlotsPerDispenser) return;
    // 空栈归一：id<=0 或 count<=0 → 清空（id=0, count=0）。**t607 修**：count 归 0 时 id 必须一并归 0 ——
    //   旧版 normId 只看 id，dispenseFromDispenser 写回「最后 1 件扣成 0」(itemId>0, count=0) 时存成
    //   {id>0, count=0} 幽灵栈，破「id==0 ⟺ count==0」不变式：UI 槽判空按 id → 图标残留；发射取物按
    //   count → 不再发射；点击拾取拿到 count=0 的「消失物品」。归一以 count 为先（count 无效 → 整栈空）。
    const int normCount = (id > 0 && count > 0) ? count : 0;
    const int normId = (normCount > 0) ? id : 0;
    Dispenser &d = m_dispensers[key(x, y, z)]; // 自动建条目（不存在则插入空 9 槽）
    d[size_t(index)] = Slot{normId, normCount};
    ++m_revision;
    emit dispenserChanged();
}

// t607 查询某坐标是否有发射器条目（即便 9 槽全空也算「有」）。身份语义：有条目 = 玩家库存发射器（放置时
//   Main.qml 调 ensureDispenser 注册 / UI 写入自动建 / 存档加载）——库存空（含最后一个投掷物用完清零）踩板
//   无动作（陷阱解除）；无条目 = 神殿陷阱发射器（worldgen 不写 store）——保持 t579 fallback 默认射箭。
bool DispenserStore::hasDispenser(int x, int y, int z) const
{
    return m_dispensers.find(key(x, y, z)) != m_dispensers.end();
}

// t607 确保某坐标有发射器条目（无则建空 9 槽 + bump revision + emit；已有则 no-op 不发信号）。玩家放置
//   发射器时 Main.qml.onBlockPlaced 调（注册「玩家库存发射器」身份，区分 worldgen 神殿陷阱）。
void DispenserStore::ensureDispenser(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_dispensers.find(k) != m_dispensers.end()) return; // 已有 → no-op（幂等，不无故发信号）
    m_dispensers.emplace(k, Dispenser{}); // 建空 9 槽条目（Slot{id=0,count=0}）
    ++m_revision;
    emit dispenserChanged();
}

// 移除某发射器条目（破块清孤儿）。不存在则 no-op（仅实际 erase 命中时才 ++revision + emit dispenserChanged，
//   驱动残留绑定刷新；空 erase 不发信号免无谓抖动，幂等安全）。
void DispenserStore::clearDispenser(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_dispensers.erase(k) > 0) {
        ++m_revision;
        emit dispenserChanged();
    }
}

// 清空全部发射器（跨世界切换防泄漏）。空 → no-op（不无故发信号 / 不增 revision，幂等）。
void DispenserStore::clearAll()
{
    if (m_dispensers.empty()) return;
    m_dispensers.clear();
    ++m_revision;
    emit dispenserChanged();
}

// 收集所有发射器条目为 QVariantList（落盘用）。形状：[{x,y,z, slots:[{id,count}×9]}, ...]。
//   **t607**：全空发射器**也落盘**（去掉旧 `!any → skip`）——条目存在与否是身份语义（hasDispenser：玩家库存
//   发射器 vs 神殿陷阱 fallback），清空后的玩家发射器若不落盘，重载后条目消失 → 退回神殿默认射箭行为。
//   注：局部变量名禁用 `slots`（Qt 关键字宏，Q_OBJECT 类内会被预处理器抹掉，见 lessons-learned）。
QVariantList DispenserStore::allDispensers() const
{
    QVariantList out;
    for (auto it = m_dispensers.cbegin(); it != m_dispensers.cend(); ++it) {
        const Dispenser &d = it->second;
        QVariantList slotList;
        slotList.reserve(kSlotsPerDispenser);
        for (const Slot &s : d) {
            QVariantMap sm;
            sm.insert(QStringLiteral("id"), s.id);
            sm.insert(QStringLiteral("count"), s.count);
            slotList.append(sm);
        }
        int x = 0, y = 0, z = 0;
        if (!parseKey(it->first, x, y, z)) continue; // 键损坏 → 跳过（不写残条目）
        QVariantMap dm;
        dm.insert(QStringLiteral("x"), x);
        dm.insert(QStringLiteral("y"), y);
        dm.insert(QStringLiteral("z"), z);
        dm.insert(QStringLiteral("slots"), slotList);
        out.append(dm);
    }
    return out;
}

// 用存档 QVariantList（同 allDispensers 形状）整体替换内存（先清空再填充；单次 emit dispenserChanged）。
//   替换语义 = 清旧世界残留 + 填本世界发射器（杜绝跨世界泄漏，同 chestStore.loadAll / furnaceStore.loadAll 模式）。
//   空列表 → 仅清空。slots 空栈归一同 setSlot（t607：count<=0 → id 一并归 0，旧存档幽灵栈 {id>0,count=0}
//   加载时被清洗）；index 越界 / 缺坐标 → 跳过该发射器。
void DispenserStore::loadAll(const QVariantList &dispensers)
{
    m_dispensers.clear();
    for (const QVariant &v : dispensers) {
        const QVariantMap dm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = dm.value(QStringLiteral("x")).toInt(&okx);
        const int y = dm.value(QStringLiteral("y")).toInt(&oky);
        const int z = dm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue;
        const QVariantList slotList = dm.value(QStringLiteral("slots")).toList();
        Dispenser d; // 默认全空（Slot{id=0,count=0}）
        const int n = slotList.size();
        for (int i = 0; i < kSlotsPerDispenser && i < n; ++i) {
            const QVariantMap sm = slotList[i].toMap();
            const int id = sm.value(QStringLiteral("id")).toInt();
            const int count = sm.value(QStringLiteral("count")).toInt();
            const int normCount = (id > 0 && count > 0) ? count : 0; // t607：count 无效 → 整栈空
            d[size_t(i)] = Slot{ normCount > 0 ? id : 0, normCount };
        }
        m_dispensers[key(x, y, z)] = std::move(d);
    }
    ++m_revision;
    emit dispenserChanged(); // 状态整体替换 → 通知 DispenserUI delegate 重读（即便结果为空，刷新到空态）
}
