#ifndef CHESTSTORE_H
#define CHESTSTORE_H

#include <QObject>
#include <QtQml/qqml.h>

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
//   - setSlot(x,y,z,index,id,count)：直接写某箱子某槽（ChestUI 点击放置 / 互换 / 拖拽均分写回用）
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
    // 直接写某箱子某槽（index 范围 + id 合法性校验；id<=0 或 count<=0 → 清空该槽）。自动建箱条目。
    Q_INVOKABLE void setSlot(int x, int y, int z, int index, int id, int count);
    // 移除某箱子条目（破块清孤儿；不存在则 no-op）。spec「破箱掉落内容」属 Phase 1.1+，本轮直接弃内容。
    Q_INVOKABLE void clearChest(int x, int y, int z);

signals:
    // 任一箱子任一槽内容变更（setSlot）/ 条目移除（clearChest）。驱动 revision 自增 + ChestUI delegate 刷新。
    void chestChanged();

private:
    // 单格物品栈（id=0 空栈）。同 Hotbar::ItemStack 语义，但本类自持（Game 层不依赖 Hotbar 的私有结构）。
    struct Slot {
        int id = 0;
        int count = 0;
    };
    using Chest = std::array<Slot, kSlotsPerChest>;

    // 坐标 → 箱子内容。QString 键（"x,y,z"）—— 简单可读、无位打包范围限制；箱子数少，性能非热点。
    std::unordered_map<QString, Chest> m_chests;
    int m_revision = 0;

    static QString key(int x, int y, int z); // "x,y,z"
};

#endif // CHESTSTORE_H
