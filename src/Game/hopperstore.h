#ifndef HOPPERSTORE_H
#define HOPPERSTORE_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QStringList>

#include <array>
#include <unordered_map>

// 漏斗内容存储（Game 层 ViewModel；t1080）。机制等价 MC 1.5+ hopper「5 槽容腔存于方块」：每只漏斗（按世界
// 方块坐标键控）持一份 5 槽物品容腔，跨 UI 开关 / 跨面板持久（非 QML 本地态）；多只漏斗各自独立 5 槽。结构
// 对齐同族 per-block 方块内容器（ChestStore 27 槽 / FurnaceStore 3 槽 / DispenserStore 9 槽）——**不**新开容器
// 抽象（单一权威铁律：家族形状 / 读写口径逐字的 DispenserStore 对齐，禁第二份形状）。
//
// 设计（对齐 ChestStore / DispenserStore）：纯存储，不持光 / 不依赖 World/Renderer（PLAN §2 分层：本层属
// Game/ViewModel，由 PlayerController::scanHoppers 机制面 + Main.qml 装配 / 持久化读写；零向上依赖）。物品
// 栈语义同 Hotbar::ItemStack —— (id, count)，id=0 空栈。**不**复用 Hotbar VM 的 main/hotbar 槽（玩家随身
// 背包与方块容器分立，同族口径）。
//
// 机制面（t1080）：引擎（PlayerController::scanHoppers）读槽走 Q_INVOKABLE 读族（C++ 直调合法）、写槽走
// setSlot / 本类引擎原语 insertStack（非 Q_INVOKABLE；收集 / 接收推送共用插入面：同 id 未满槽合并 →
// 首个空槽；返回实际接受件数，0 = 满仓 / 无空位，调用方按「挂起不销毁」处理——机制等价 MC 漏斗满仓拒收。
// 合并只认「同 id 且两者元数据全空」——enchants/name/durability 任一非空则不堆叠，cap=1 物品各占一槽）。
//
// QML 面：revision + hopperChanged 通知沿在位（同 ChestStore / DispenserStore 模式）；本单 UI 如实降级
// （禁为此单新开 QML 玩法路径），读族 / 写族供未来 HopperUI 呈现层复用（登记候选池）。
//
// §4 法律 + §9：零 MC 专有名词（「漏斗」「Hopper」为通用描述词——工业/农用给料漏斗先于 MC 存在的通用
// 机械词；机制等价记载合法）。

class HopperStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HopperStore)
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    Q_PROPERTY(int revision READ revision NOTIFY hopperChanged)

public:
    explicit HopperStore(QObject *parent = nullptr);

    static constexpr int kSlotsPerHopper = 5;
    int slotCount() const { return kSlotsPerHopper; }
    int revision() const { return m_revision; }
    int entryCount() const { return int(m_hoppers.size()); }

    Q_INVOKABLE int slotIdAt(int x, int y, int z, int index) const;
    Q_INVOKABLE int slotCountAt(int x, int y, int z, int index) const;
    Q_INVOKABLE int slotDurabilityAt(int x, int y, int z, int index) const;
    Q_INVOKABLE QVariantList slotEnchantsAt(int x, int y, int z, int index) const;
    Q_INVOKABLE QString slotNameAt(int x, int y, int z, int index) const;
    Q_INVOKABLE void setSlot(int x, int y, int z, int index, int id, int count,
                             const QVariantList &enchants = {}, const QString &name = QString(),
                             int durability = -1);
    Q_INVOKABLE bool hasHopper(int x, int y, int z) const;
    Q_INVOKABLE void clearHopper(int x, int y, int z);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE QVariantList allHoppers() const;
    Q_INVOKABLE void loadAll(const QVariantList &hoppers);

    int insertStack(int x, int y, int z, int id, int count, const QVariantList &enchants,
                    const QString &name, int durability);

    // 引擎面（非 Q_INVOKABLE；t1080 机制 tick 用）：条目键列表快照（scanHoppers 遍历用——快照迭代，
    //   防遍历中 insertStack 自动建条目改桶结构失效）+ 每漏斗冷却相位（run-phase，不落盘）。
    QStringList hopperKeys() const;
    qreal hopperCooldown(int x, int y, int z) const;
    void setHopperCooldown(int x, int y, int z, qreal cd);
    // 键编解码（引擎 / 探针消费——scanHoppers 遍历 hopperKeys 后反解坐标；与 DispenserStore key 同构）。
    static QString key(int x, int y, int z);
    static bool parseKey(const QString &k, int &x, int &y, int &z);

signals:
    void hopperChanged();

private:
    struct Slot {
        int id = 0;
        int count = 0;
        int enchants[4] = {0, 0, 0, 0};
        QString name;
        int durability = -1;
    };
    // 单只漏斗条目 = 5 槽 + 引擎冷却相位（cd<0 允许——首 tick 即跑一轮；setHopperCooldown 随写）。
    struct Entry {
        std::array<Slot, kSlotsPerHopper> slotArr;
        qreal cd = 0.0;

        // 槽下标透传（引擎 / 读族按 entry[i] 访问——数组语义与旧 using 别名逐位同）。
        Slot &operator[](int i) { return slotArr[std::size_t(i)]; }
        const Slot &operator[](int i) const { return slotArr[std::size_t(i)]; }
    };

    std::unordered_map<QString, Entry> m_hoppers;
    int m_revision = 0;
};

#endif // HOPPERSTORE_H
