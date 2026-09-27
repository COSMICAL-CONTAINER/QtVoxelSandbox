#ifndef BREWINGSTORE_H
#define BREWINGSTORE_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>
#include <QStringList>

#include <array>
#include <unordered_map>

#include "recipe.h" // 材料段 id 常量（酿造成分 / 燃烬粉燃料；Game 同层）

// 酿造台内容存储（Game 层 ViewModel；t1097）。机制等价 MC 1.0「酿造内容存于方块」：每台酿造台（按世界
// 方块坐标键控）持 3 药水瓶槽 + 1 材料（原料）槽 + 1 燃料槽 + 酿造 tick 状态（brewProgress 秒 /
// fuelOpsLeft 剩余可酿次数），跨 UI 开关 / 跨面板持久（非 QML 本地态）；多台各自独立。结构对齐同族
// per-block 容器（ChestStore 27 槽 / FurnaceStore 3 槽 / DispenserStore 9 槽 / HopperStore 5 槽）——
// **不**新开容器抽象（单一权威铁律：形状 / 读写口径逐字对齐 HopperStore / FurnaceStore）。
//
// MC 1.0 酿造语义核实（wiki 2026 实读三元组，机制等价实现；§9 零 MC 专名）：
//   ① 燃料 = 烈焰粉（机制等价「燃烬粉」BlazePowderId 0x244，t726 烈焰链既有）——**1 粉 = 恰好 20 次
//     酿造操作**（kPowderFuelOps=20，MC 原值）；燃料计每完成一次操作 -1（一次操作=一批转换全部
//     合格瓶位，与瓶数无关），计量归零且有合格瓶位时才消耗新粉并重置 20（scanBrewingStands 补燃分支）。
//   ② 单次酿造耗时 400 ticks = 20 秒（kBrewSecs=20.0，MC 原值）；**一次酿造同时转换所有合格瓶位**
//     （1 份原料可酿 ≤3 瓶，MC 口径），完成时消耗 1 份原料。
//   ③ 基础链 = 水瓶（水瓶 + 地狱疣 → 粗制药水）→ 效果材料 → 效果药水；瓶**在原位转换**（药水槽自身
//     id 变为产物，非炉式独立输出槽；机制等价 MC 酿造台瓶原位变换）。
//
// 酿造配方表（静态纯数据，本类持有 = Game 层单一权威；第一轮 3 行 + t1099 第二轮 5 行 + 周边静态查询）：
//   水瓶 + 灰烬疣（AshWartId）→ 粗制药水；粗制药水 + 糖（SugarId）→ 迅捷药水；
//   粗制药水 + 燃烬粉（BlazePowderId）→ 力量药水（MC 1.0 烈焰粉既是燃料又是力量原料的同置双语义照搬）；
//   t1099：粗制 + 岩浆膏 → 火抗 / + 幽灵泪 → 再生 / + 蜘蛛眼 → 中毒 / + 发酵蛛眼 → 虚弱 / + 闪烁西瓜
//   → 瞬间治疗（MC 1.0 五链原值；原料获取链裁定见 recipe.h 0x26B..0x274 注——岩浆膏 / 幽灵泪 / 闪烁西瓜
//   创造专属，蜘蛛眼 / 发酵蛛眼生存可达）。
//   t1100：二级酿造 = 成品效果药水 + 红石粉 → 对应延长版 ×6（迅捷 / 力量 / 火抗 8:00、再生 / 中毒 1:30、
//   虚弱 4:00；wiki 逐链核实留痕见 recipe.h 0x275..0x27A 注 + playercontroller.h 延长常量族）。映射 =
//   extendedPotionResult 小表（成品→延长成对映射，表规模 6 行故取独立函数面而非 if 链平铺——留痕：
//   if 链已 8 行，再平铺 6 行可读性崩；小表 + 单 gate 行 = 映射单一权威 + 负例面集中）。
//   t1101：三级酿造面 = 成品药水（基础 6 + 延长 6）+ 火药 → 对应喷溅版 ×12（右键投掷弹丸，落地碎裂
//   范围结算；口径留痕见 recipe.h 0x27B..0x286 注 + playercontroller.h 喷溅常量族）。映射 =
//   splashPotionResult 小表（extendedPotionResult 同门：12 行成对映射 + 单 gate 行；水 / 粗制 /
//   瞬间治疗 / 喷溅版再酿 / 非瓶 = 无映射负例面）。
//   t1102：水瓶直酿面交付两行（水瓶 + 发酵蛛眼 → 虚弱——喷溅虚弱链前置 / 水瓶 + 糖 → 凡庸药水
//   MundanePotionId 0x287 可饮无效果同粗制口径），行位接在水瓶分支粗制三元组**之前**（t1097 既录
//   三元组行原样幸存 = r2067d 源钉零修订）；其余 1.0 面（辉光强化 / 即时效果喷溅 / modifier 对喷溅
//   再酿 / 闪烁西瓜·红石→凡庸(延长)）**登记后续轮**（候选池，裁定留痕见 recipe.h 0x287 注）。
//
// 设计（对齐 HopperStore / FurnaceStore）：纯存储，不持光 / 不依赖 World/Renderer（PLAN §2 分层：本层属
// Game/ViewModel，机制 tick = PlayerController::scanBrewingStands（Game 层直调 Q_INVOKABLE 读族 + 引擎
// 原语），Main.qml 装配 / 持久化读写；零向上依赖）。物品栈语义同 Hotbar::ItemStack —— (id, count)，
// id=0 空栈。
//
// QML 面：revision + brewingChanged 通知沿在位（同 FurnaceStore 模式）；BrewingUI 单向消费（读族取最新 +
// setSlot 写回；tick 权威在 C++ scanBrewingStands——呈现层不推进酿造，t177 三轮「tick 直读 store」教训的
// C++ 侧终局形态）。
//
// §4 法律 + §9：零 MC 专有名词（「酿造」「药水」「灰烬疣」为通用词 / 原创名；机制等价记载合法）。

class BrewingStore : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BrewingStore)
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    Q_PROPERTY(int revision READ revision NOTIFY brewingChanged)

public:
    explicit BrewingStore(QObject *parent = nullptr);

    static constexpr int kSlotsPerBrewing = 5; // 3 药水瓶槽(0..2) + 原料槽(3) + 燃料槽(4)（MC 酿造台 5 槽）
    // 槽位语义索引（MC 酿造台布局：底行 3 瓶 / 顶原料 / 左下燃料）。
    static constexpr int kSlotPotion0    = 0;
    static constexpr int kSlotPotion1    = 1;
    static constexpr int kSlotPotion2    = 2;
    static constexpr int kSlotIngredient = 3;
    static constexpr int kSlotFuel       = 4;

    // 酿造常量（MC 1.0 原值核实留痕）：
    static constexpr int   kPowderFuelOps = 20;   // 1 燃烬粉 = 20 次酿造（MC blaze powder fuel 20）
    static constexpr qreal kBrewSecs      = 20.0; // 单次酿造 400 ticks = 20s（MC 原值）

    int slotCount() const { return kSlotsPerBrewing; }
    int revision() const { return m_revision; }
    int entryCount() const { return m_stands.size(); }

    Q_INVOKABLE int slotIdAt(int x, int y, int z, int index) const;
    Q_INVOKABLE int slotCountAt(int x, int y, int z, int index) const;
    Q_INVOKABLE int slotDurabilityAt(int x, int y, int z, int index) const;
    Q_INVOKABLE QVariantList slotEnchantsAt(int x, int y, int z, int index) const;
    Q_INVOKABLE QString slotNameAt(int x, int y, int z, int index) const;
    Q_INVOKABLE void setSlot(int x, int y, int z, int index, int id, int count,
                             const QVariantList &enchants = {}, const QString &name = QString(),
                             int durability = -1);
    // 酿造 tick 状态（无条目 → 0；scanBrewingStands 读写 / UI 读）。
    Q_INVOKABLE qreal brewProgressAt(int x, int y, int z) const;
    Q_INVOKABLE int fuelOpsAt(int x, int y, int z) const;
    Q_INVOKABLE void setBrewProgress(int x, int y, int z, qreal val);
    Q_INVOKABLE void setFuelOps(int x, int y, int z, int val);
    Q_INVOKABLE bool hasBrewing(int x, int y, int z) const;
    Q_INVOKABLE void clearBrewing(int x, int y, int z);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE QVariantList allBrewingStands() const;
    Q_INVOKABLE void loadAll(const QVariantList &stands);

    // 引擎面（非 Q_INVOKABLE；t1097 机制 tick 用）：条目键列表快照（scanBrewingStands 遍历用——快照迭代，
    //   防遍历中 setSlot 自动建条目改桶结构失效；HopperStore::hopperKeys 同门）。
    QStringList standKeys() const;

    // 键编解码（引擎 / 探针消费；与 HopperStore key 同构）。
    static QString key(int x, int y, int z);
    static bool parseKey(const QString &k, int &x, int &y, int &z);

    // ── 酿造配方表（静态纯函数 = Game 层单一权威；机制 tick / 探针同源调用）────────────────────
    // 酿造转换：原料 ing 作用在瓶 bottleId 上 → 产物 id（无映射 / 非瓶 → 0）。
    static int brewResult(int ingredientId, int bottleId);
    // t1100 二级酿造映射（红石 modifier）：成品效果药水 → 延长版产物 id（水 / 粗制 / 瞬间治疗 / 非瓶 → 0）。
    //   **仅经 brewResult 的红石门行接入**（机制 tick / 探针统一走 brewResult 单一入口，本函数不单独
    //   对外承接转换——调用面唯一 = brewingstore.cpp 内 gate 行；矩阵探针也只经 brewResult 断言，
    //   保 NEG 恰红单腿归因：摘映射 = gate 行 + 本体一并摘除，编译仍绿、行为柱恰红）。
    static int extendedPotionResult(int potionId);
    // t1101 三级酿造映射（火药 modifier）：成品药水（基础 6 + 延长 6）→ 喷溅版产物 id（水 / 粗制 /
    //   瞬间治疗 / 喷溅版再酿 / 非瓶 → 0）。**仅经 brewResult 的火药门行接入**（机制 tick / 探针统一走
    //   brewResult 单一入口，本函数不单独对外承接转换——调用面唯一 = brewingstore.cpp 内 gate 行；
    //   矩阵探针也只经 brewResult 断言，保 NEG 恰红单腿归因：摘映射 = gate 行 + 本体一并摘除，编译
    //   仍绿、行为柱恰红。extendedPotionResult 同门先例）。
    static int splashPotionResult(int potionId);
    // 燃料燃烧值：itemId 可燃 → 剩余可酿次数（燃烬粉 20）；非燃料 → 0。
    static int fuelOpsFor(int itemId);

signals:
    void brewingChanged();

private:
    struct Slot {
        int id = 0;
        int count = 0;
        int enchants[4] = {0, 0, 0, 0};
        QString name;
        int durability = -1;
    };
    // 单台酿造台条目 = 5 槽 + 酿造进度（0..kBrewSecs 秒）+ 剩余可酿次数（燃烬粉燃烧计量）。
    //   成员名禁用 `slots`（Qt 关键字宏，lessons-learned）→ slotArr。
    struct Entry {
        std::array<Slot, kSlotsPerBrewing> slotArr;
        qreal progress = 0.0;
        int fuelOps = 0;

        Slot &operator[](int i) { return slotArr[std::size_t(i)]; }
        const Slot &operator[](int i) const { return slotArr[std::size_t(i)]; }
    };

    std::unordered_map<QString, Entry> m_stands;
    int m_revision = 0;
};

#endif // BREWINGSTORE_H
