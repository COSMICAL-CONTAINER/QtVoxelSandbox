#include "furnacestore.h"

#include <QStringList>
#include <QVariantMap>

// 熔炉内容存储（t177 二轮复盘）实现。纯存储类 —— 无光 / 无 World 依赖；FurnaceUI + Main.qml 路由读写。
// 物品栈语义同 Hotbar（id=0=空，count<=0 视空），但不复用 Hotbar VM 的槽（熔炉是独立方块内容器）。
// 结构对齐 cheststore.cpp（按坐标键控 + revision NOTIFY + all/load 落盘），仅槽位数（3 vs 27）+ 冶炼进度字段不同。

FurnaceStore::FurnaceStore(QObject *parent)
    : QObject(parent)
{
}

// 坐标键 "x,y,z"。简单可读、无位打包范围限制（坐标可负 / 可大）；熔炉数少，QString 哈希性能非热点（同 ChestStore）。
QString FurnaceStore::key(int x, int y, int z)
{
    return QString::number(x) + QLatin1Char(',') + QString::number(y) + QLatin1Char(',') + QString::number(z);
}

// 反解 key() 产物（allFurnaces 落盘时把 QString 键还原为坐标列）。坐标可负、可大 → toInt（非 toUInt）。
bool FurnaceStore::parseKey(const QString &k, int &x, int &y, int &z)
{
    const QStringList parts = k.split(QLatin1Char(','));
    if (parts.size() != 3) return false;
    bool ok = false;
    x = parts[0].toInt(&ok); if (!ok) return false;
    y = parts[1].toInt(&ok); if (!ok) return false;
    z = parts[2].toInt(&ok); if (!ok) return false;
    return true;
}

int FurnaceStore::slotIdAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerFurnace) return 0; // 越界 → 空栈
    const auto it = m_furnaces.find(key(x, y, z));
    if (it == m_furnaces.end()) return 0;            // 无此熔炉条目 → 空槽
    return it->second.slotArr[size_t(index)].id;
}

int FurnaceStore::slotCountAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerFurnace) return 0;
    const auto it = m_furnaces.find(key(x, y, z));
    if (it == m_furnaces.end()) return 0;
    return it->second.slotArr[size_t(index)].count;
}

// t647 实例元数据读（同 ChestStore 模式；越界 / 无此熔炉 → 缺省值）。
int FurnaceStore::slotDurabilityAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerFurnace) return -1;
    const auto it = m_furnaces.find(key(x, y, z));
    if (it == m_furnaces.end()) return -1;
    return it->second.slotArr[size_t(index)].durability;
}

QVariantList FurnaceStore::slotEnchantsAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerFurnace) return {0, 0, 0, 0};
    const auto it = m_furnaces.find(key(x, y, z));
    if (it == m_furnaces.end()) return {0, 0, 0, 0};
    const Slot &s = it->second.slotArr[size_t(index)];
    return { s.enchants[0], s.enchants[1], s.enchants[2], s.enchants[3] };
}

QString FurnaceStore::slotNameAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerFurnace) return QString();
    const auto it = m_furnaces.find(key(x, y, z));
    if (it == m_furnaces.end()) return QString();
    return it->second.slotArr[size_t(index)].name;
}

// 直接写某熔炉某槽。index 越界忽略；id<=0 或 count<=0 → 清空该槽（保持空栈不变式：id==0 ⟺ count==0）。
// 自动建熔炉条目（首次写入某坐标即创建空熔炉再写）。写入后 bump revision → FurnaceUI delegate 刷新。
//   t647 元数据（enchants / name / durability）随写入：仅非空栈持有；空栈恒清（同 ChestStore.setSlot 模式）。
void FurnaceStore::setSlot(int x, int y, int z, int index, int id, int count,
                           const QVariantList &enchants, const QString &name, int durability)
{
    if (index < 0 || index >= kSlotsPerFurnace) return;
    // 空栈归一：id<=0 或 count<=0 → 清空（id=0, count=0）。**t607 同源修**：count 归一在先（count<=0 →
    //   id 一并归 0），防「最后 1 件扣成 0」类写回存幽灵栈 {id>0,count=0}（同 DispenserStore 修法的
    //   防御性收口——当前 FurnaceUI tick 写入端已自行归零 id，此处兜底层不变式）。
    const int normCount = (id > 0 && count > 0) ? count : 0;
    const int normId = (normCount > 0) ? id : 0;
    Furnace &f = m_furnaces[key(x, y, z)]; // 自动建条目（不存在则插入空熔炉）
    Slot s;
    s.id = normId;
    s.count = normCount;
    for (int i = 0; i < 4; ++i)
        s.enchants[i] = (normId > 0 && i < enchants.size()) ? enchants.at(i).toInt() : 0;
    s.name = (normId > 0) ? name.trimmed() : QString();
    s.durability = (normId > 0) ? durability : -1; // 空栈恒「未初始化」（消费端归一满耐久）
    f.slotArr[size_t(index)] = s;
    ++m_revision;
    emit furnaceChanged();
}

qreal FurnaceStore::burnProgressAt(int x, int y, int z) const
{
    const auto it = m_furnaces.find(key(x, y, z));
    if (it == m_furnaces.end()) return 0.0;
    return it->second.burn;
}

qreal FurnaceStore::smeltingProgressAt(int x, int y, int z) const
{
    const auto it = m_furnaces.find(key(x, y, z));
    if (it == m_furnaces.end()) return 0.0;
    return it->second.smelting;
}

// 写冶炼 tick 状态（FurnaceUI.tick 末写回，跨开关保留冶炼进度）。负值钳 0（防写入异常负进度）。
void FurnaceStore::setBurn(int x, int y, int z, qreal val)
{
    Furnace &f = m_furnaces[key(x, y, z)]; // 自动建条目
    f.burn = (val > 0.0) ? val : 0.0;
    ++m_revision;
    emit furnaceChanged();
}

void FurnaceStore::setSmelting(int x, int y, int z, qreal val)
{
    Furnace &f = m_furnaces[key(x, y, z)]; // 自动建条目
    f.smelting = (val > 0.0) ? val : 0.0;
    ++m_revision;
    emit furnaceChanged();
}

// 移除某熔炉条目（破块清孤儿）。不存在则 no-op（仅实际 erase 命中时才 ++revision + emit furnaceChanged，
//   驱动残留绑定刷新；空 erase 不发信号免无谓抖动，幂等安全）。
void FurnaceStore::clearFurnace(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_furnaces.erase(k) > 0) {
        ++m_revision;
        emit furnaceChanged();
    }
}

// 清空全部熔炉（跨世界切换防泄漏）。空 → no-op（不无故发信号 / 不增 revision，幂等）。
void FurnaceStore::clearAll()
{
    if (m_furnaces.empty()) return;
    m_furnaces.clear();
    ++m_revision;
    emit furnaceChanged();
}

// 收集所有「含 ≥1 非空槽 或 有冶炼进度」的熔炉为 QVariantList（落盘用）。全空且无进度熔炉跳过
//   （加载后缺失 = 空熔炉行为等价）。形状：[{x,y,z, slots:[{id,count,enchants:[4],name,durability}×3], burn, smelt}, ...]。
//   t647：实例元数据（enchants / name / durability）随槽落盘 —— 老存档无此键 → 读回缺省（无附魔 / 无名 /
//   -1 归一满耐久，向后兼容）。
//   注：局部变量名禁用 `slots`（Qt 关键字宏，Q_OBJECT 类内会被预处理器抹掉，见 lessons-learned）。
QVariantList FurnaceStore::allFurnaces() const
{
    QVariantList out;
    for (auto it = m_furnaces.cbegin(); it != m_furnaces.cend(); ++it) {
        const Furnace &f = it->second;
        QVariantList slotList;
        slotList.reserve(kSlotsPerFurnace);
        bool any = false;
        for (const Slot &s : f.slotArr) {
            QVariantMap sm;
            sm.insert(QStringLiteral("id"), s.id);
            sm.insert(QStringLiteral("count"), s.count);
            if (s.id > 0 && s.count > 0) any = true;
            QVariantList enchList;
            enchList.reserve(4);
            for (int i = 0; i < 4; ++i) enchList.append(s.enchants[i]);
            sm.insert(QStringLiteral("enchants"), enchList);
            sm.insert(QStringLiteral("name"), s.name);
            sm.insert(QStringLiteral("durability"), s.durability);
            slotList.append(sm);
        }
        // 有冶炼进度（未烧完 / 未冶炼完）的熔炉也落盘（关再开恢复进度，机制等价 MC）。
        if (!any && f.burn <= 0.0 && f.smelting <= 0.0) continue; // 全空且无进度 → 不落盘
        int x = 0, y = 0, z = 0;
        if (!parseKey(it->first, x, y, z)) continue; // 键损坏 → 跳过（不写残条目）
        QVariantMap fm;
        fm.insert(QStringLiteral("x"), x);
        fm.insert(QStringLiteral("y"), y);
        fm.insert(QStringLiteral("z"), z);
        fm.insert(QStringLiteral("slots"), slotList);
        fm.insert(QStringLiteral("burn"), f.burn);
        fm.insert(QStringLiteral("smelt"), f.smelting);
        out.append(fm);
    }
    return out;
}

// 用存档 QVariantList（同 allFurnaces 形状）整体替换内存（先清空再填充；单次 emit furnaceChanged）。
//   替换语义 = 清旧世界残留 + 填本世界熔炉（杜绝跨世界泄漏，同 chestStore.loadAll 模式）。空列表 → 仅清空。
//   slots 空栈归一同 setSlot（id<=0 或 count<=0 → 空）；index 越界 / 缺坐标 → 跳过该熔炉。
//   t647：实例元数据读回（老存档无键 → 缺省：无附魔 / 无名 / -1 满耐久，向后兼容）。
void FurnaceStore::loadAll(const QVariantList &furnaces)
{
    m_furnaces.clear();
    for (const QVariant &v : furnaces) {
        const QVariantMap fm = v.toMap();
        bool okx = false, oky = false, okz = false;
        const int x = fm.value(QStringLiteral("x")).toInt(&okx);
        const int y = fm.value(QStringLiteral("y")).toInt(&oky);
        const int z = fm.value(QStringLiteral("z")).toInt(&okz);
        if (!okx || !oky || !okz) continue;
        const QVariantList slotList = fm.value(QStringLiteral("slots")).toList();
        Furnace f; // 默认全空（Slot{id=0,count=0}）+ burn/smelt=0
        const int n = slotList.size();
        for (int i = 0; i < kSlotsPerFurnace && i < n; ++i) {
            const QVariantMap sm = slotList[i].toMap();
            const int id = sm.value(QStringLiteral("id")).toInt();
            const int count = sm.value(QStringLiteral("count")).toInt();
            const int normCount = (id > 0 && count > 0) ? count : 0; // t607：count 无效 → 整栈空（清洗幽灵栈）
            Slot s;
            s.id = normCount > 0 ? id : 0;
            s.count = normCount;
            if (s.id > 0) {
                const QVariantList enchList = sm.value(QStringLiteral("enchants")).toList();
                for (int e = 0; e < 4; ++e)
                    s.enchants[e] = (e < enchList.size()) ? enchList.at(e).toInt() : 0;
                s.name = sm.value(QStringLiteral("name")).toString(); // t647 老存档无 name 键 → 空串
                const int dur = sm.value(QStringLiteral("durability")).toInt();
                s.durability = sm.contains(QStringLiteral("durability")) ? dur : -1; // 老档无键 → -1（归一满耐久）
            }
            f.slotArr[size_t(i)] = s;
        }
        f.burn = fm.value(QStringLiteral("burn")).toDouble();
        f.smelting = fm.value(QStringLiteral("smelt")).toDouble();
        if (f.burn < 0.0) f.burn = 0.0;
        if (f.smelting < 0.0) f.smelting = 0.0;
        m_furnaces[key(x, y, z)] = std::move(f);
    }
    ++m_revision;
    emit furnaceChanged(); // 状态整体替换 → 通知 FurnaceUI delegate 重读（即便结果为空，刷新到空态）
}
