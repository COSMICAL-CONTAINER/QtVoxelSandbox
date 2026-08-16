#ifndef CHESTSTORE_H
#define CHESTSTORE_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>

#include "loottable.h" // LootTable::Stack（lootSlot 参数；同层 Game 静态纯函数）

#include <array>
#include <unordered_map>

// 箱子内容存储（Game 层 ViewModel；t173）。机制等价 MC 1.0「箱子内容存于方块」：每只箱子（按
// 世界方块坐标键控）持一份 27 槽物品内容，跨 UI 开关 / 跨面板持久（非 QML 本地态）；多只箱子各自
// 独立 27 槽。与 FurnaceUI（冶炼槽存 QML 本地、仅一只熔炉）的差异：本类把内容下沉到 C++ VM 层、
// 按坐标寻址，满足 spec t173「物品存 chunk state」= 物品随方块存（世界级 block-instance state）。
//
// 设计：纯存储，不持光 / 不依赖 World/Renderer（PLAN §2 分层：本层属 Game/ViewModel，只被 ChestUI
// 呈现层 + Main.qml 路由读写；零向上依赖）。物品栈语义同 Hotbar::ItemStack —— (id, count)，id=0 空栈。
// **不**复用 Hotbar VM 的 main/hotbar 槽（那是玩家随身背包；箱子是独立的方块内容器）。
//
// 暴露给 QML（moc 安全：列表数据走 Q_INVOKABLE + revision，同 Hotbar 模式）：
//   - slotCount（恒 27；Repeater model 用）
//   - revision（int，任一箱子任一槽写入自增；NOTIFY=chestChanged。ChestUI delegate 触碰 revision 取最新栈值）
//   - slotIdAt(x,y,z,index) / slotCountAt(x,y,z,index)：某箱子某槽的栈数据（id=0=空槽；越界 / 无此箱返 0）
//   - slotEnchantsAt(x,y,z,index)（review L7）：某箱子某槽的附魔元数据（QVariantList<int> 4 元素，每 =
//     EnchantRegistry::pack 值；0 = 空槽。战利品附魔书带随机附魔 + 玩家放入的附魔物品随栈存取）
//   - setSlot(x,y,z,index,id,count[,enchants])：直接写某箱子某槽（ChestUI 点击放置 / 互换 / 拖拽均分写回用；
//     enchants 缺省空 = 清附魔）
//   - clearChest(x,y,z)：移除某箱子条目（破块时 Main.qml.onBlockBroken 调，清孤儿内容）
//
// §4 法律 + §9：零 MC 专有名词（类名 / 字串「箱子」「Chest」为通用描述词）。
class ChestStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ChestStore)
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    // 内容版本号（任一槽写入自增）。ChestUI delegate 把它「触碰」进绑定 → 写入后整列刷新（同 Hotbar
    //   slotRevision / mainRevision 模式，moc 安全契约）。
    Q_PROPERTY(int revision READ revision NOTIFY chestChanged)

public:
    explicit ChestStore(QObject *parent = nullptr);

    static constexpr int kSlotsPerChest = 27; // MC 1.0 箱子标准 27 槽（3×9）

    int slotCount() const { return kSlotsPerChest; }
    int revision() const { return m_revision; }

    // 某箱子某槽栈数据（id=0=空；index 越界 / 无此箱子条目 → 0）。
    Q_INVOKABLE int slotIdAt(int x, int y, int z, int index) const;
    Q_INVOKABLE int slotCountAt(int x, int y, int z, int index) const;
    // review L7 某箱子某槽附魔元数据（ItemStack.enchants[4] 同构 QVariantList<int> 4 元素 pack 值；空槽 /
    //   越界 / 无此箱 → {0,0,0,0}）。战利品附魔书首开填充时写入随机附魔；ChestUI 整件搬运路径透传。
    Q_INVOKABLE QVariantList slotEnchantsAt(int x, int y, int z, int index) const;
    // 直接写某箱子某槽（index 范围 + id 合法性校验；id<=0 或 count<=0 → 清空该槽）。自动建箱条目。
    //   enchants（review L7）：ItemStack.enchants[4] 同构 4-int pack 值；缺省 / 空列表 = 清附魔（可堆叠物品
    //   附魔恒 0 语义不变；仅附魔书 / 工具 / 护甲等 cap=1 物品随实例携带）。
    Q_INVOKABLE void setSlot(int x, int y, int z, int index, int id, int count,
                             const QVariantList &enchants = {});
    // 移除某箱子条目（破块清孤儿；不存在则 no-op）。spec「破箱掉落内容」属 Phase 1.1+，本轮直接弃内容。
    Q_INVOKABLE void clearChest(int x, int y, int z);
    // 清空全部箱子（跨世界切换时 Main.qml.enterWorld 经 loadAll 间接调；亦可直调）。空 → no-op（不无故发信号）。
    Q_INVOKABLE void clearAll();
    // 收集所有「含 ≥1 非空槽」的箱子为 QVariantList（每项 {x,y,z,slots:[{id,count}×27]}），供 Main.qml 传
    //   worldStore.saveAll(name, ...) 落盘。全空箱子跳过（落盘省行；加载后缺失条目 = 空 27 槽，行为等价）。
    Q_INVOKABLE QVariantList allChests() const;
    // 用存档 QVariantList（同 allChests 形状）整体替换内存内容（先清空再填充；单次 emit chestChanged）。
    //   Main.qml.enterWorld 调：chestStore.loadAll(worldStore.loadChests()) —— 替换语义即「清旧世界残留 +
    //   填本世界箱子」，杜绝跨世界泄漏（同 t187 hotbarVM「进世界前必清」模式）。空列表 → 仅清空。
    Q_INVOKABLE void loadAll(const QVariantList &chests);
    // t393 首开填充地牢战利品（机制等价 MC 1.0 dungeon chest loot）。仅对「尚未有条目」的箱子生效（首次开）：
    //   已有条目（曾开过 / 已填充）→ no-op 返 false（杜绝清空后重开再生战利品，机制对齐 MC「战利品 roll 一次」）。
    //   用 LootTable::dungeonChestPool + 坐标确定性 seed 抽 8 件（PLAN §2-K 精神：同箱子同战利品，世界重生成可复现），
    //   分散入随机空槽（同 id 不合并，机制等价 MC dungeon chest 多槽散布）。caller（Main.qml.openChest）须先确认
    //   该坐标是地牢箱（theWorld.isDungeonChest —— 由 chest state bit2 标记，worldgen 写入；玩家放置的无此标记）。
    //   分层（PLAN §2）：本层 Game，依赖同层 LootTable + QtCore；不依赖 World（「是否地牢箱」由 caller 查 World）。
    Q_INVOKABLE bool populateDungeonLoot(int x, int y, int z);
    // t484 首开填充废弃矿井战利品（机制等价 MC 1.0 mineshaft chest loot）。与 populateDungeonLoot 同源语义：
    //   仅对「尚未有条目」的箱子生效（首次开），用 LootTable::mineshaftChestPool + 坐标确定性 seed 抽 6 件
    //   （矿物 / 附魔书 / 铁锭等，PLAN §2-K 同箱同战利品）。caller（Main.qml.openChest）须先确认该坐标是矿井箱
    //   （theWorld.isMineshaftChest —— 由 chest state bit3 标记，worldgen placeMineshaft 写入）。
    Q_INVOKABLE bool populateMineshaftLoot(int x, int y, int z);
    // t485 首开填充沙漠神殿战利品（机制等价 MC 1.0 desert temple chest loot）。与 populateDungeonLoot /
    //   populateMineshaftLoot 同源语义：仅对「尚未有条目」的箱子生效（首次开），用 LootTable::pyramidChestPool
    //   + 坐标确定性 seed 抽 4 件（钻石 / 金 / 青金石 / 骨头 / 腐肉 / 红石 / 附魔书等，PLAN §2-K 同箱同战利品）。
    //   caller（Main.qml.openChest）须先确认该坐标是神殿箱（theWorld.isPyramidChest —— 由 chest state bit4 标记，
    //   worldgen placeDesertTemple 写入）。
    Q_INVOKABLE bool populatePyramidLoot(int x, int y, int z);
    // t486 首开填充丛林神殿战利品（机制等价 MC 1.0 jungle temple chest loot）。与 populateDungeonLoot /
    //   populateMineshaftLoot / populatePyramidLoot 同源语义：仅对「尚未有条目」的箱子生效（首次开），用
    //   LootTable::jungleTempleChestPool + 坐标确定性 seed 抽 5 件（骨头 / 腐肉 / 铁 / 金 / 钻石 / 箭 / 附魔书等，
    //   PLAN §2-K 同箱同战利品）。caller（Main.qml.openChest）须先确认该坐标是丛林神殿箱
    //   （theWorld.isJungleTempleChest —— 由 chest state bit5 标记，worldgen placeJungleTemple 写入）。
    Q_INVOKABLE bool populateJungleTempleLoot(int x, int y, int z);
    // t487 首开填充要塞战利品（机制等价 MC 1.0 stronghold chest loot）。与 populateDungeonLoot /
    //   populateMineshaftLoot / populatePyramidLoot / populateJungleTempleLoot 同源语义：仅对「尚未有条目」的
    //   箱子生效（首次开），用 LootTable::strongholdChestPool + 坐标确定性 seed 抽 6 件（末影之眼 / 骨头 / 腐肉 /
    //   铁锭 / 青金石 / 红石 / 钻石 / 附魔书等，PLAN §2-K 同箱同战利品）。caller（Main.qml.openChest）须先确认该
    //   坐标是要塞箱（theWorld.isStrongholdChest —— 由 chest state bit6 标记，worldgen placeStronghold 写入）。
    Q_INVOKABLE bool populateStrongholdLoot(int x, int y, int z);

signals:
    // 任一箱子任一槽内容变更（setSlot）/ 条目移除（clearChest）。驱动 revision 自增 + ChestUI delegate 刷新。
    void chestChanged();

private:
    // 单格物品栈（id=0 空栈）。同 Hotbar::ItemStack 语义，但本类自持（Game 层不依赖 Hotbar 的私有结构）。
    //   review L7 enchants[4]：per-instance 附魔元数据（每元素 = EnchantRegistry::pack 值；0 = 空槽）——
    //   战利品附魔书带随机附魔、玩家放入的附魔工具 / 护甲随实例存取（附魔书 maxStack=1 → 每槽恒单件，
    //   附魔与栈一一对应，无堆叠歧义）。
    struct Slot {
        int id = 0;
        int count = 0;
        int enchants[4] = {0, 0, 0, 0};
    };
    using Chest = std::array<Slot, kSlotsPerChest>;

    // 坐标 → 箱子内容。QString 键（"x,y,z"）—— 简单可读、无位打包范围限制；箱子数少，性能非热点。
    std::unordered_map<QString, Chest> m_chests;
    int m_revision = 0;

    static QString key(int x, int y, int z); // "x,y,z"
    // 反解 key() 产物（"x,y,z" → x,y,z；坐标可负）。格式不符 → false。
    static bool parseKey(const QString &k, int &x, int &y, int &z);
    // review L7 战利品 Stack → Slot 转换（五个 populate*Loot 共用的单一权威）：附魔书（EnchantedBookId）
    //   额外经 LootTable::enchantedBookEnchants 随机生成 1-3 条附魔（seed = 箱子 seed 混 slot 序号 → 同箱
    //   同战利品可复现，PLAN §2-K）；其余物品附魔恒 0。
    static Slot lootSlot(const LootTable::Stack &st, quint32 boxSeed, int slotIndex);
};

#endif // CHESTSTORE_H
