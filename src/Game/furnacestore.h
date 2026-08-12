#ifndef FURNACESTORE_H
#define FURNACESTORE_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>

#include <array>
#include <unordered_map>

// 熔炉内容存储（Game 层 ViewModel；t177 二轮复盘）。机制等价 MC 1.0「熔炉内容存于方块」：每只熔炉（按
// 世界方块坐标键控）持一份 in(1 槽) / fuel(1 槽) / out(1 槽) + 冶炼 tick 状态（burnRemain / burnTotal /
// smeltProgress），跨 UI 开关 / 跨面板持久（非 QML 本地态）；多只熔炉各自独立内容 + 进度。
//
// 修旧 bug：FurnaceUI.qml 旧把 in/fuel/out 存为 **QML 本地属性**（inId/inCount/...），全局单实例共享 →
//   「全世界熔炉共享一个物品栏」（打开任何熔炉都是同一内容）、关重开内容易丢、打掉不掉。本类把内容
//   下沉到 C++ VM 层、按坐标寻址，结构对齐已 per-block 的 ChestStore（箱子模式）。
//
// 设计（对齐 ChestStore）：纯存储，不持光 / 不依赖 World/Renderer（PLAN §2 分层：本层属 Game/ViewModel，
//   只被 FurnaceUI 呈现层 + Main.qml 路由读写；零向上依赖）。物品栈语义同 Hotbar::ItemStack —— (id, count)，
//   id=0 空栈。**不**复用 Hotbar VM 的 main/hotbar 槽（那是玩家随身背包；熔炉是独立的方块内容器）。
//
// 冶炼 tick 状态为何也存这里：FurnaceUI.qml 旧把 burnRemain/smeltProgress 存 QML 本地态，关闭面板会丢
//   （机制不等价 MC：熔炉关面板仍继续烧）。把进度下沉到本类 → 跨开关保留冶炼进度（用户关再开仍见进度）。
//   tick 推进仍由 FurnaceUI.tick（WorldClock 驱动）算，写回本类 setBurn/setSmelting（呈现层算 + VM 存，
//   同 chest「VM 存 / UI 读」模式，VM 不持 World 不持时钟）。
//
// 暴露给 QML（moc 安全：列表数据走 Q_INVOKABLE + revision，同 ChestStore / Hotbar 模式）：
//   - slotCount（恒 3；in=0 / fuel=1 / out=2；对齐 MC 熔炉 3 槽）
//   - revision（int，任一熔炉任一槽 / 进度写入自增；NOTIFY=furnaceChanged。FurnaceUI delegate 触碰 revision 取最新值）
//   - slotIdAt/slotCountAt(x,y,z,index)：某熔炉某槽栈数据（index 越界 / 无此熔炉返 0）
//   - setSlot(x,y,z,index,id,count)：直接写某熔炉某槽（FurnaceUI 放置 / 互换 / 拖拽均分写回用）
//   - burnProgressAt/smeltingProgressAt(x,y,z)：冶炼 tick 状态（burnRemain / smeltProgress；无此熔炉返 0）
//   - setBurn/setSmelting(x,y,z,val)：写冶炼 tick 状态（FurnaceUI.tick 末写回，跨开关保留）
//   - clearFurnace(x,y,z)：移除某熔炉条目（破块时 Main.qml.onBlockBroken 调，清孤儿内容）
//   - allFurnaces()：落盘用 QVariantList（Main.qml 传 worldStore.saveAll）
//   - loadAll(QVariantList)：整体替换内存（Main.qml.enterWorld 调，清旧世界残留 + 填本世界熔炉）
//
// §4 法律 + §9：零 MC 专有名词（类名 / 字串「熔炉」「Furnace」为通用描述词）。
class FurnaceStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FurnaceStore)
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    // 内容版本号（任一槽 / 进度写入自增）。FurnaceUI delegate 把它「触碰」进绑定 → 写入后整列刷新（同
    //   ChestStore revision / Hotbar slotRevision 模式，moc 安全契约）。
    Q_PROPERTY(int revision READ revision NOTIFY furnaceChanged)

public:
    explicit FurnaceStore(QObject *parent = nullptr);

    static constexpr int kSlotsPerFurnace = 3; // MC 1.0 熔炉 3 槽：in(0) / fuel(1) / out(2)
    // 槽位语义索引（对齐 MC 熔炉布局：上左输入 / 下燃料 / 右输出）。FurnaceUI 据 index 路由组名 in/fuel/out。
    static constexpr int kSlotIn = 0;
    static constexpr int kSlotFuel = 1;
    static constexpr int kSlotOut = 2;

    int slotCount() const { return kSlotsPerFurnace; }
    int revision() const { return m_revision; }

    // 某熔炉某槽栈数据（id=0=空；index 越界 / 无此熔炉条目 → 0）。
    Q_INVOKABLE int slotIdAt(int x, int y, int z, int index) const;
    Q_INVOKABLE int slotCountAt(int x, int y, int z, int index) const;
    // 直接写某熔炉某槽（index 范围校验；id<=0 或 count<=0 → 清空该槽）。自动建熔炉条目。
    Q_INVOKABLE void setSlot(int x, int y, int z, int index, int id, int count);
    // 冶炼 tick 状态（无此熔炉条目 → 0；FurnaceUI.tick 末读 / 写）。
    Q_INVOKABLE qreal burnProgressAt(int x, int y, int z) const;
    Q_INVOKABLE qreal smeltingProgressAt(int x, int y, int z) const;
    // 写冶炼 tick 状态（FurnaceUI.tick 末写回，跨开关保留冶炼进度）。自动建熔炉条目。
    Q_INVOKABLE void setBurn(int x, int y, int z, qreal val);
    Q_INVOKABLE void setSmelting(int x, int y, int z, qreal val);
    // 移除某熔炉条目（破块清孤儿；不存在则 no-op）。spec「破熔炉掉内容」由 Main.qml.onBlockBroken 先 spawnItem
    //   掉落 in/fuel/out 内容、再调本方法清条目（机制等价 MC 破熔炉掉落内容）。
    Q_INVOKABLE void clearFurnace(int x, int y, int z);
    // 清空全部熔炉（跨世界切换时 Main.qml.enterWorld 经 loadAll 间接调；亦可直调）。空 → no-op（不无故发信号）。
    Q_INVOKABLE void clearAll();
    // 收集所有「含 ≥1 非空槽 或 有冶炼进度」的熔炉为 QVariantList（每项 {x,y,z, slots:[{id,count}×3],
    //   burn, smelt}），供 Main.qml 传 worldStore.saveAll 落盘。全空且无进度熔炉跳过（落盘省行；加载后
    //   缺失条目 = 空熔炉，行为等价）。
    Q_INVOKABLE QVariantList allFurnaces() const;
    // 用存档 QVariantList（同 allFurnaces 形状）整体替换内存内容（先清空再填充；单次 emit furnaceChanged）。
    //   Main.qml.enterWorld 调：furnaceStore.loadAll(worldStore.loadFurnaces()) —— 替换语义即「清旧世界残留 +
    //   填本世界熔炉」，杜绝跨世界泄漏（同 chestStore.loadAll 模式）。空列表 → 仅清空。
    Q_INVOKABLE void loadAll(const QVariantList &furnaces);

signals:
    // 任一熔炉任一槽 / 进度变更（setSlot / setBurn / setSmelting）/ 条目移除（clearFurnace）。
    //   驱动 revision 自增 + FurnaceUI delegate 刷新。
    void furnaceChanged();

private:
    // 单格物品栈（id=0 空栈）。同 Hotbar::ItemStack 语义，但本类自持（Game 层不依赖 Hotbar 的私有结构）。
    struct Slot {
        int id = 0;
        int count = 0;
    };
    // 单只熔炉内容：3 槽 + 冶炼 tick 状态（burnRemain 燃烧剩余 / smeltProgress 冶炼进度）。
    //   burnTotal 不落盘（点燃时随 burnRemain 一起重置 → 关再开只需 burnRemain 即可恢复视觉，比例条下次
    //   点燃刷新；存盘省一字段。FurnaceUI.tick 点燃时 setBurn 写 burnRemain，burnTotal 由本地态即时记）。
    //   成员名禁用 `slots`（Qt 关键字宏，Q_OBJECT 类内会被预处理器抹掉，见 lessons-learned）→ 用 slotArr。
    struct Furnace {
        std::array<Slot, kSlotsPerFurnace> slotArr;
        qreal burn = 0.0;       // 当前燃料剩余燃烧秒数（>0 = 正在烧）
        qreal smelting = 0.0;   // 当前件累积冶炼秒数（0..kSmeltSecs；满则产 1 件）
    };

    // 坐标 → 熔炉内容。QString 键（"x,y,z"）—— 简单可读、无位打包范围限制；熔炉数少，性能非热点。
    std::unordered_map<QString, Furnace> m_furnaces;
    int m_revision = 0;

    static QString key(int x, int y, int z); // "x,y,z"
    // 反解 key() 产物（"x,y,z" → x,y,z；坐标可负）。格式不符 → false。
    static bool parseKey(const QString &k, int &x, int &y, int &z);
};

#endif // FURNACESTORE_H
