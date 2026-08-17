#include "cheststore.h"

#include "loottable.h" // t393 战利品表（dungeonChestPool / roll / enchantedBookEnchants）；同层 Game，向下依赖 Core
#include "recipe.h"    // RecipeRegistry::EnchantedBookId（战利品书附魔分流判定）；同层 Game

#include <QRandomGenerator>
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

// review L7 战利品 Stack → Slot（见 .h 头注释）：附魔书带随机附魔，其余物品附魔恒 0。
ChestStore::Slot ChestStore::lootSlot(const LootTable::Stack &st, quint32 boxSeed, int slotIndex)
{
    if (st.itemId != RecipeRegistry::EnchantedBookId) return Slot{st.itemId, st.count};
    const QVariantList ench = LootTable::enchantedBookEnchants(boxSeed ^ quint32(slotIndex) * 2654435761u);
    Slot s{st.itemId, st.count};
    for (int i = 0; i < 4; ++i)
        s.enchants[i] = (i < ench.size()) ? ench.at(i).toInt() : 0;
    return s;
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

// review L7 某箱子某槽附魔元数据（ItemStack.enchants[4] 同构 pack 值 4-int；空槽 / 越界 / 无此箱 → 全 0）。
QVariantList ChestStore::slotEnchantsAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerChest) return {0, 0, 0, 0};
    const auto it = m_chests.find(key(x, y, z));
    if (it == m_chests.end()) return {0, 0, 0, 0};
    const Slot &s = it->second.at(size_t(index));
    return { s.enchants[0], s.enchants[1], s.enchants[2], s.enchants[3] };
}

// t622 某箱子某槽自定义名（铁砧改名物品随栈存取；空槽 / 越界 / 无此箱 → 空串）。
QString ChestStore::slotNameAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerChest) return QString();
    const auto it = m_chests.find(key(x, y, z));
    if (it == m_chests.end()) return QString();
    return it->second.at(size_t(index)).name;
}

// review D2-c 某箱子某槽实例耐久（空槽 / 越界 / 无此箱 → 0；-1 = 未初始化）。
int ChestStore::slotDurabilityAt(int x, int y, int z, int index) const
{
    if (index < 0 || index >= kSlotsPerChest) return 0;
    const auto it = m_chests.find(key(x, y, z));
    if (it == m_chests.end()) return 0;
    return it->second.at(size_t(index)).durability;
}

// 直接写某箱子某槽。index 越界忽略；id<=0 或 count<=0 → 清空该槽（保持空栈不变式：id==0 ⟺ count==0）。
//   自动建箱条目（首次写入某坐标即创建空 27 槽再写）。写入后 bump revision → ChestUI delegate 刷新。
//   **t607 同源修**：count 归一在先（count<=0 → id 一并归 0），防「最后 1 件扣成 0」类写回存幽灵栈
//   {id>0,count=0}（同 DispenserStore 修法的防御性收口——当前 UI 写入端已自行归零，此处兜底层不变式）。
//   enchants（review L7）：4-int pack 值随栈写入（空栈恒清附魔）；不足 4 元素按 0 补齐 = 清空语义。
//   name（t622）：实例名随栈写入（空栈恒清名；trim 防全空格）。
//   durability（review D2-c）：实例耐久随栈写入（空栈恒归 -1 = 未初始化；>0 显式保真 —— ChestUI
//   localWriteSlot 已归一（工具实例值或 0），此处原样存：0 与 -1 在消费端（addToAny 的 normalizeDurability）
//   同义归满耐久 / 归 0，兜底不改语义）。
void ChestStore::setSlot(int x, int y, int z, int index, int id, int count, const QVariantList &enchants, const QString &name, int durability)
{
    if (index < 0 || index >= kSlotsPerChest) return;
    // 空栈归一：id<=0 或 count<=0 → 清空（id=0, count=0）。
    const int normCount = (id > 0 && count > 0) ? count : 0;
    const int normId = (normCount > 0) ? id : 0;
    Chest &chest = m_chests[key(x, y, z)]; // 自动建条目（不存在则插入空 27 槽）
    Slot &slot = chest[size_t(index)];
    slot.id = normId;
    slot.count = normCount;
    for (int i = 0; i < 4; ++i)
        slot.enchants[i] = (normId > 0 && i < enchants.size()) ? enchants.at(i).toInt() : 0;
    slot.name = (normId > 0) ? name.trimmed() : QString();
    slot.durability = (normId > 0) ? durability : -1; // review D2-c：空栈恒「未初始化」（消费端归一满耐久）
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
//   形状：[{x,y,z, slots:[{id,count,enchants:[4],name,durability}×27]}, ...]。QString 键经 parseKey 还原为坐标列。
//   enchants（review L7）：附魔书 / 附魔工具的 per-instance 附魔随槽落盘（老存档无此键 → 读回全 0 = 无附魔，
//   向后兼容）。name（t622）：改名物品实例名随槽落盘（老存档无此键 → 读回空 = 默认名，向后兼容）。
//   durability（review D2-c）：磨损工具 / 护甲实例耐久随槽落盘（老存档无此键 → 读回 -1 = 未初始化 →
//   取出端 normalizeDurability 归一满耐久，同旧档工具语义，向后兼容）。
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
            QVariantList enchList;
            enchList.reserve(4);
            for (int i = 0; i < 4; ++i) enchList.append(s.enchants[i]);
            sm.insert(QStringLiteral("enchants"), enchList);
            sm.insert(QStringLiteral("name"), s.name);
            sm.insert(QStringLiteral("durability"), s.durability);
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
        Chest chest; // 默认全空（Slot{id=0,count=0,enchants 全 0,name 空}）
        const int n = slotList.size();
        for (int i = 0; i < kSlotsPerChest && i < n; ++i) {
            const QVariantMap sm = slotList[i].toMap();
            const int id = sm.value(QStringLiteral("id")).toInt();
            const int count = sm.value(QStringLiteral("count")).toInt();
            const int normCount = (id > 0 && count > 0) ? count : 0; // t607：count 无效 → 整栈空（清洗幽灵栈）
            Slot s;                                    // review L7：默认全 0（老存档无 enchants 键 → 无附魔）
            s.id = normCount > 0 ? id : 0;
            s.count = normCount;
            if (s.id > 0) {
                const QVariantList enchList = sm.value(QStringLiteral("enchants")).toList();
                for (int e = 0; e < 4; ++e)
                    s.enchants[e] = (e < enchList.size()) ? enchList.at(e).toInt() : 0;
                s.name = sm.value(QStringLiteral("name")).toString(); // t622 老存档无 name 键 → 空串
                // review D2-c 老存档无 durability 键 → toInt() 缺省 0 → 落 -1（未初始化 = 取出端归一满耐久，
                //   与旧档「箱子里的工具取出满耐久」行为一致）。新存档原样读回（含 -1 / 实例值）。
                const int dur = sm.value(QStringLiteral("durability")).toInt();
                s.durability = sm.contains(QStringLiteral("durability")) ? dur : -1;
            }
            chest[size_t(i)] = s;
        }
        m_chests[key(x, y, z)] = chest;
    }
    ++m_revision;
    emit chestChanged(); // 状态整体替换 → 通知 ChestUI delegate 重读（即便结果为空，刷新到空态）
}

// t393 首开填充地牢战利品（见 cheststore.h 头注释）。已有条目 → no-op 返 false（首开一次性 roll）；
//   无条目 → 用 LootTable 抽 kDungeonRolls 件，分散入随机空槽，建条目 + bump revision + emit chestChanged。
bool ChestStore::populateDungeonLoot(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_chests.find(k) != m_chests.end()) return false; // 已开过 / 已填充 → 不再生（首开一次性）

    // 坐标确定性 seed（PLAN §2-K）：同坐标箱子 → 同战利品；与 worldgen hashVoxel 同族（坐标混合，非时间源）。
    //   用无符号 32 位混合（与 world.cpp hashColumn/hashVoxel 同模式），坐标可负故先转 quint32（位模式）。
    const quint32 sx = quint32(quint32(x) * 73856093u);
    const quint32 sy = quint32(quint32(y) * 19349663u);
    const quint32 sz = quint32(quint32(z) * 83492791u);
    const quint32 seed = (sx ^ sy ^ sz) ^ 0xC0FFEEu; // 加盐防低坐标退化到 0

    // 抽取战利品 stack 列表（最多 kDungeonRolls 件，同 id 不合并）。
    const auto &pool = LootTable::dungeonChestPool();
    const std::vector<LootTable::Stack> stacks = LootTable::roll(pool, LootTable::kDungeonRolls, seed);

    // 建空箱子条目，逐 stack 找随机空槽写入（同 id 不合并 → 分散多槽，机制等价 MC dungeon chest 散布）。
    Chest chest; // 默认 27 空槽（Slot{id=0,count=0}）
    QRandomGenerator slotRng(seed ^ 0x9E3779B9u); // 与抽取 RNG 不同的盐 → 槽位分布独立
    for (const LootTable::Stack &st : stacks) {
        if (st.itemId <= 0 || st.count <= 0) continue;
        // 找一个随机空槽（最多试 kSlotsPerChest 次 + 线性兜底；27 槽放 8 件绰绰有余，必命中）。
        int slot = -1;
        for (int t = 0; t < kSlotsPerChest; ++t) {
            const int cand = int(slotRng.bounded(kSlotsPerChest));
            if (chest[size_t(cand)].id == 0) { slot = cand; break; }
        }
        if (slot < 0) { // 随机全撞已占（极不可能 8 件撞满 27 槽）→ 线性扫首个空槽兜底
            for (int i = 0; i < kSlotsPerChest; ++i)
                if (chest[size_t(i)].id == 0) { slot = i; break; }
        }
        if (slot < 0) break; // 真填满（roll 数 ≤ 槽数，理论不可达）→ 弃余
        chest[size_t(slot)] = lootSlot(st, seed, slot); // review L7：附魔书带随机附魔（seed 混 slot 序号）
    }

    m_chests[k] = std::move(chest);
    ++m_revision;
    emit chestChanged(); // 首开写入 → 通知 ChestUI delegate 重读（开盖前已填，ChestUI 显时即见战利品）
    return true; // 已填充（首开一次性 roll 成功）
}

// t484 首开填充废弃矿井战利品（见 cheststore.h 头注释）。与 populateDungeonLoot 同源逻辑，仅换战利品池
//   （LootTable::mineshaftChestPool：矿物 / 附魔书 / 铁锭等）+ 抽取次数（kMineshaftRolls=6）+ 坐标确定性
//   seed 盐（0x5BD1E995 异或，与地牢盐 0xC0FFEE 解耦 → 同坐标矿井箱与地牢箱战利品独立）。
bool ChestStore::populateMineshaftLoot(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_chests.find(k) != m_chests.end()) return false; // 已开过 / 已填充 → 不再生（首开一次性）

    // 坐标确定性 seed（PLAN §2-K）：同坐标箱子 → 同战利品（与 populateDungeonLoot 同模式，盐不同 → 两表独立）。
    const quint32 sx = quint32(quint32(x) * 73856093u);
    const quint32 sy = quint32(quint32(y) * 19349663u);
    const quint32 sz = quint32(quint32(z) * 83492791u);
    const quint32 seed = (sx ^ sy ^ sz) ^ 0x5BD1E995u; // 矿井专用盐（与地牢 0xC0FFEE 解耦）

    // 抽取战利品 stack 列表（最多 kMineshaftRolls 件，同 id 不合并）。
    const auto &pool = LootTable::mineshaftChestPool();
    const std::vector<LootTable::Stack> stacks = LootTable::roll(pool, LootTable::kMineshaftRolls, seed);

    // 建空箱子条目，逐 stack 找随机空槽写入（同 id 不合并 → 分散多槽，机制等价 MC 矿井箱散布）。
    Chest chest; // 默认 27 空槽
    QRandomGenerator slotRng(seed ^ 0x9E3779B9u); // 与抽取 RNG 不同的盐 → 槽位分布独立
    for (const LootTable::Stack &st : stacks) {
        if (st.itemId <= 0 || st.count <= 0) continue;
        int slot = -1;
        for (int t = 0; t < kSlotsPerChest; ++t) {
            const int cand = int(slotRng.bounded(kSlotsPerChest));
            if (chest[size_t(cand)].id == 0) { slot = cand; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < kSlotsPerChest; ++i)
                if (chest[size_t(i)].id == 0) { slot = i; break; }
        }
        if (slot < 0) break;
        chest[size_t(slot)] = lootSlot(st, seed, slot); // review L7：附魔书带随机附魔（seed 混 slot 序号）
    }

    m_chests[k] = std::move(chest);
    ++m_revision;
    emit chestChanged();
    return true;
}

// t485 首开填充沙漠神殿战利品（见 cheststore.h 头注释）。与 populateDungeonLoot / populateMineshaftLoot 同源逻辑，
//   仅换战利品池（LootTable::pyramidChestPool：钻石 / 金 / 青金石 / 骨头 / 腐肉等）+ 抽取次数（kPyramidRolls=4）
//   + 坐标确定性 seed 盐（0xDE5E72B 异或，与地牢盐 0xC0FFEE / 矿井盐 0x5BD1E995 解耦 → 同坐标神殿箱与地牢 / 矿井
//   箱战利品各自独立）。
bool ChestStore::populatePyramidLoot(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_chests.find(k) != m_chests.end()) return false; // 已开过 / 已填充 → 不再生（首开一次性）

    // 坐标确定性 seed（PLAN §2-K）：同坐标箱子 → 同战利品（与 populateDungeonLoot / populateMineshaftLoot 同模式，
    //   盐不同 → 三表独立）。
    const quint32 sx = quint32(quint32(x) * 73856093u);
    const quint32 sy = quint32(quint32(y) * 19349663u);
    const quint32 sz = quint32(quint32(z) * 83492791u);
    const quint32 seed = (sx ^ sy ^ sz) ^ 0xDE5E72B5u; // 神殿专用盐（与地牢 0xC0FFEE / 矿井 0x5BD1E995 解耦）

    // 抽取战利品 stack 列表（最多 kPyramidRolls=4 件，同 id 不合并）。
    const auto &pool = LootTable::pyramidChestPool();
    const std::vector<LootTable::Stack> stacks = LootTable::roll(pool, LootTable::kPyramidRolls, seed);

    // 建空箱子条目，逐 stack 找随机空槽写入（同 id 不合并 → 分散多槽，机制等价 MC 沙漠神殿箱散布）。
    Chest chest; // 默认 27 空槽
    QRandomGenerator slotRng(seed ^ 0x9E3779B9u); // 与抽取 RNG 不同的盐 → 槽位分布独立
    for (const LootTable::Stack &st : stacks) {
        if (st.itemId <= 0 || st.count <= 0) continue;
        int slot = -1;
        for (int t = 0; t < kSlotsPerChest; ++t) {
            const int cand = int(slotRng.bounded(kSlotsPerChest));
            if (chest[size_t(cand)].id == 0) { slot = cand; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < kSlotsPerChest; ++i)
                if (chest[size_t(i)].id == 0) { slot = i; break; }
        }
        if (slot < 0) break;
        chest[size_t(slot)] = lootSlot(st, seed, slot); // review L7：附魔书带随机附魔（seed 混 slot 序号）
    }

    m_chests[k] = std::move(chest);
    ++m_revision;
    emit chestChanged();
    return true;
}

// t486 首开填充丛林神殿战利品（见 cheststore.h 头注释）。与 populateDungeonLoot / populateMineshaftLoot /
//   populatePyramidLoot 同源逻辑，仅换战利品池（LootTable::jungleTempleChestPool：骨头 / 腐肉 / 铁 / 金 / 钻石 /
//   箭 / 马鞍 / 命名牌 / 附魔书等）+ 抽取次数（kJungleRolls=5）+ 坐标确定性 seed 盐（0x5CA1F00D 异或，与地牢盐
//   0xC0FFEE / 矿井盐 0x5BD1E995 / 神殿盐 0xDE5E72B5 解耦 → 同坐标丛林神殿箱与其它箱战利品各自独立）。
bool ChestStore::populateJungleTempleLoot(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_chests.find(k) != m_chests.end()) return false; // 已开过 / 已填充 → 不再生（首开一次性）

    // 坐标确定性 seed（PLAN §2-K）：同坐标箱子 → 同战利品（与其它三表同模式，盐不同 → 四表独立）。
    const quint32 sx = quint32(quint32(x) * 73856093u);
    const quint32 sy = quint32(quint32(y) * 19349663u);
    const quint32 sz = quint32(quint32(z) * 83492791u);
    const quint32 seed = (sx ^ sy ^ sz) ^ 0x5CA1F00Du; // 丛林神殿专用盐（与其它三表解耦）

    // 抽取战利品 stack 列表（最多 kJungleRolls=5 件，同 id 不合并）。
    const auto &pool = LootTable::jungleTempleChestPool();
    const std::vector<LootTable::Stack> stacks = LootTable::roll(pool, LootTable::kJungleRolls, seed);

    // 建空箱子条目，逐 stack 找随机空槽写入（同 id 不合并 → 分散多槽，机制等价 MC 丛林神殿箱散布）。
    Chest chest; // 默认 27 空槽
    QRandomGenerator slotRng(seed ^ 0x9E3779B9u); // 与抽取 RNG 不同的盐 → 槽位分布独立
    for (const LootTable::Stack &st : stacks) {
        if (st.itemId <= 0 || st.count <= 0) continue;
        int slot = -1;
        for (int t = 0; t < kSlotsPerChest; ++t) {
            const int cand = int(slotRng.bounded(kSlotsPerChest));
            if (chest[size_t(cand)].id == 0) { slot = cand; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < kSlotsPerChest; ++i)
                if (chest[size_t(i)].id == 0) { slot = i; break; }
        }
        if (slot < 0) break;
        chest[size_t(slot)] = lootSlot(st, seed, slot); // review L7：附魔书带随机附魔（seed 混 slot 序号）
    }

    m_chests[k] = std::move(chest);
    ++m_revision;
    emit chestChanged();
    return true;
}

// t487 首开填充要塞战利品（见 cheststore.h 头注释）。与 populateDungeonLoot / populateMineshaftLoot /
//   populatePyramidLoot / populateJungleTempleLoot 同源逻辑，仅换战利品池（LootTable::strongholdChestPool：
//   末影之眼 / 骨头 / 腐肉 / 铁锭 / 青金石 / 红石 / 钻石 / 附魔书等）+ 抽取次数（kStrongholdRolls=6）+
//   坐标确定性 seed 盐（0x5A17D1C7 专用盐，与其它四表解耦 → 同坐标要塞箱与其它箱战利品各自独立）。
bool ChestStore::populateStrongholdLoot(int x, int y, int z)
{
    const QString k = key(x, y, z);
    if (m_chests.find(k) != m_chests.end()) return false; // 已开过 / 已填充 → 不再生（首开一次性）

    // 坐标确定性 seed（PLAN §2-K）：同坐标箱子 → 同战利品（与其它四表同模式，盐不同 → 五表独立）。
    const quint32 sx = quint32(quint32(x) * 73856093u);
    const quint32 sy = quint32(quint32(y) * 19349663u);
    const quint32 sz = quint32(quint32(z) * 83492791u);
    const quint32 seed = (sx ^ sy ^ sz) ^ 0x5A17D1C7u; // 要塞专用盐（0x5A17D1C7，与其它四表解耦）

    // 抽取战利品 stack 列表（最多 kStrongholdRolls=6 件，同 id 不合并）。
    const auto &pool = LootTable::strongholdChestPool();
    const std::vector<LootTable::Stack> stacks = LootTable::roll(pool, LootTable::kStrongholdRolls, seed);

    // 建空箱子条目，逐 stack 找随机空槽写入（同 id 不合并 → 分散多槽，机制等价 MC 要塞箱散布）。
    Chest chest; // 默认 27 空槽
    QRandomGenerator slotRng(seed ^ 0x9E3779B9u); // 与抽取 RNG 不同的盐 → 槽位分布独立
    for (const LootTable::Stack &st : stacks) {
        if (st.itemId <= 0 || st.count <= 0) continue;
        int slot = -1;
        for (int t = 0; t < kSlotsPerChest; ++t) {
            const int cand = int(slotRng.bounded(kSlotsPerChest));
            if (chest[size_t(cand)].id == 0) { slot = cand; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < kSlotsPerChest; ++i)
                if (chest[size_t(i)].id == 0) { slot = i; break; }
        }
        if (slot < 0) break;
        chest[size_t(slot)] = lootSlot(st, seed, slot); // review L7：附魔书带随机附魔（seed 混 slot 序号）
    }

    m_chests[k] = std::move(chest);
    ++m_revision;
    emit chestChanged();
    return true;
}