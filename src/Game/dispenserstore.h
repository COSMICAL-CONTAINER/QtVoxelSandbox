#ifndef DISPENSERSTORE_H
#define DISPENSERSTORE_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>

#include <array>
#include <unordered_map>

// 发射器内容存储（Game 层 ViewModel；t542）。机制等价 MC 1.0「发射器内容存于方块」：每只发射器（按
// 世界方块坐标键控）持一份 9 槽（3×3）物品内容，跨 UI 开关 / 跨面板持久（非 QML 本地态）；多只发射器
// 各自独立 9 槽。结构对齐已 per-block 的 ChestStore（箱子 27 槽）/ FurnaceStore（熔炉 3 槽）。
//
// 修旧 bug（t517「界面先行」遗留）：DispenserUI.qml 旧把 9 槽存为 **QML 本地数组**（dispSlots/dispCounts），
//   全局单实例共享 → 「全世界发射器共享一个物品栏」（打开任何发射器都是同一内容）、关重开内容易丢、
//   打掉不掉。本类把内容下沉到 C++ VM 层、按坐标寻址，结构对齐 ChestStore / FurnaceStore（同族 per-block
//   方块内容器）。
//
// 设计（对齐 ChestStore / FurnaceStore）：纯存储，不持光 / 不依赖 World/Renderer（PLAN §2 分层：本层属
//   Game/ViewModel，只被 DispenserUI 呈现层 + Main.qml 路由读写；零向上依赖）。物品栈语义同 Hotbar::ItemStack
//   —— (id, count)，id=0 空栈。**不**复用 Hotbar VM 的 main/hotbar 槽（那是玩家随身背包；发射器是独立的
//   方块内容器）。
//
// 暴露给 QML（moc 安全：列表数据走 Q_INVOKABLE + revision，同 ChestStore / FurnaceStore / Hotbar 模式）：
//   - slotCount（恒 9；Repeater model 用）
//   - revision（int，任一发射器任一槽写入自增；NOTIFY=dispenserChanged。DispenserUI delegate 触碰 revision 取最新栈值）
//   - slotIdAt/slotCountAt(x,y,z,index)：某发射器某槽栈数据（id=0=空；越界 / 无此发射器返 0）
//   - setSlot(x,y,z,index,id,count)：直接写某发射器某槽（DispenserUI 放置 / 互换 / 拖拽均分写回用）
//   - clearDispenser(x,y,z)：移除某发射器条目（破块时 Main.qml.onBlockBroken 调，清孤儿内容）
//   - allDispensers()：落盘用 QVariantList（Main.qml 传 worldStore 落盘）
//   - loadAll(QVariantList)：整体替换内存（Main.qml.enterWorld 调，清旧世界残留 + 填本世界发射器）
//
// §4 法律 + §9：零 MC 专有名词（类名 / 字串「发射器」「Dispenser」为通用描述词）。
class DispenserStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DispenserStore)
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    // 内容版本号（任一槽写入自增）。DispenserUI delegate 把它「触碰」进绑定 → 写入后整列刷新（同
    //   ChestStore / FurnaceStore revision / Hotbar slotRevision 模式，moc 安全契约）。
    Q_PROPERTY(int revision READ revision NOTIFY dispenserChanged)

public:
    explicit DispenserStore(QObject *parent = nullptr);

    static constexpr int kSlotsPerDispenser = 9; // 3×3 发射器容器（机制等价 MC 1.0 发射器 9 槽）

    int slotCount() const { return kSlotsPerDispenser; }
    int revision() const { return m_revision; }

    // 某发射器某槽栈数据（id=0=空；index 越界 / 无此发射器条目 → 0）。
    Q_INVOKABLE int slotIdAt(int x, int y, int z, int index) const;
    Q_INVOKABLE int slotCountAt(int x, int y, int z, int index) const;
    // 直接写某发射器某槽（index 范围校验；id<=0 或 count<=0 → 清空该槽）。自动建发射器条目。
    Q_INVOKABLE void setSlot(int x, int y, int z, int index, int id, int count);
    // 移除某发射器条目（破块清孤儿；不存在则 no-op）。spec「破发射器掉内容」由 Main.qml.onBlockBroken 先
    //   spawnItem 掉落 9 槽内容、再调本方法清条目（机制等价 MC 破发射器掉落内容）。
    Q_INVOKABLE void clearDispenser(int x, int y, int z);
    // 清空全部发射器（跨世界切换时 Main.qml.enterWorld 经 loadAll 间接调；亦可直调）。空 → no-op（不无故发信号）。
    Q_INVOKABLE void clearAll();
    // 收集所有「含 ≥1 非空槽」的发射器为 QVariantList（每项 {x,y,z,slots:[{id,count}×9]}），供 Main.qml 传
    //   worldStore 落盘。全空发射器跳过（落盘省行；加载后缺失条目 = 空 9 槽，行为等价）。
    Q_INVOKABLE QVariantList allDispensers() const;
    // 用存档 QVariantList（同 allDispensers 形状）整体替换内存内容（先清空再填充；单次 emit dispenserChanged）。
    //   Main.qml.enterWorld 调：dispenserStore.loadAll(worldStore.loadDispensers()) —— 替换语义即「清旧世界残留 +
    //   填本世界发射器」，杜绝跨世界泄漏（同 chestStore.loadAll / furnaceStore.loadAll 模式）。空列表 → 仅清空。
    Q_INVOKABLE void loadAll(const QVariantList &dispensers);

signals:
    // 任一发射器任一槽内容变更（setSlot）/ 条目移除（clearDispenser）。驱动 revision 自增 + DispenserUI delegate 刷新。
    void dispenserChanged();

private:
    // 单格物品栈（id=0 空栈）。同 Hotbar::ItemStack 语义，但本类自持（Game 层不依赖 Hotbar 的私有结构）。
    struct Slot {
        int id = 0;
        int count = 0;
    };
    using Dispenser = std::array<Slot, kSlotsPerDispenser>;

    // 坐标 → 发射器内容。QString 键（"x,y,z"）—— 简单可读、无位打包范围限制；发射器数少，性能非热点。
    std::unordered_map<QString, Dispenser> m_dispensers;
    int m_revision = 0;

    static QString key(int x, int y, int z); // "x,y,z"
    // 反解 key() 产物（"x,y,z" → x,y,z；坐标可负）。格式不符 → false。
    static bool parseKey(const QString &k, int &x, int &y, int &z);
};

#endif // DISPENSERSTORE_H
