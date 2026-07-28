#ifndef HOTBAR_H
#define HOTBAR_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqml.h>

#include <vector>

#include "blockregistry.h" // 物品 id（方块段 0..Count-1；图标/中文名走单一注册表）
#include "toolregistry.h"  // 工具段 id（>=0x100）；工具判定 / tier / 中文名 / 创造调色板走工具注册表（t33）

// 物品栈（t32 基础数据模型）：槽位从单一 quint8 block-id 升级为 {itemId, count}，支持堆叠。
//   - id 复用 BlockRegistry id（方块段 0..Count-1，air=0 即空栈）；
//   - 预留工具段 id>=0x100（t33 落地，本任务仅留段；工具不可堆叠 → maxStackSize 返回 1）。
//   - count 上限经 Hotbar::maxStackSize(id)：方块 64、工具 1。
//   - 不变式：id==0 当且仅当 count==0（空栈）；非空栈 count 恒 >=1（写入处统一钳制）。
struct ItemStack {
    int id = 0;
    int count = 0;

    bool isEmpty() const { return id == 0 || count <= 0; }
};

// Hotbar 视图模型（UI/ViewModel 层）：9 槽的物品栈选择状态 + 光标手持栈。
//
// 暴露给 QML（moc 安全：列表数据走 Q_INVOKABLE + slotRevision，**勿**用 Q_PROPERTY(QVariantList)
// —— 本工具链 moc 拒绝后者，见 lessons-learned）：
//   - selectedSlot（当前槽 0..8，可读写）
//   - selectedBlockId（从选中栈 id 派生；空栈 / 工具栈→Air→右键不放置。工具非方块不可放置，t33）
//   - slotCount（恒 9）
//   - slotRevision（int，随槽内容变更自增；NOTIFY slotsChanged。QML 把它「触碰」进 Repeater 的
//     model 绑定 → 槽内容改写后整列重建，图标/数量同步刷新）
//   - slotList()（QVariantList<int>：每槽物品 id；Repeater model，兼容旧消费者）
//   - countList()（QVariantList<int>：每栈 count；与 slotList 平行，触碰 slotRevision 刷新）
//   - blockIdAt / countAt / iconSourceAt / nameAt：每槽栈数据（id / 数量 / 图标 / 中文名）
//   - addStack(id, n)（智能堆叠：同 id 槽先累加至上限，再入空槽；返回未放入数；t36 拾取消费）
//   - takeStack(slot, n)（从槽取最多 n 件；返回实际取走数；栈空则 id 归 0；t36 丢弃/放置消费）
//   - setStack(slot, id, count)（直接写栈；背包点击放置/互换用）
//   - setSlotBlock(slot, id)（兼容旧调用：等同 setStack(slot, id, id==0?0:1)；销毁槽清空用）
//   - maxStackSize(id)（方块 64 / 工具段 1）
//   - resetForMode(mode)（创造=满栈 / 生存=全空；mode 切换时 QML 调）
//   - heldBlock / heldCount（光标手持物 id + 数量；背包点击拾取/放置用，跨创造/生存共享同一手持栈）
//   - isTool / toolTier / creativeTools（t33：工具段判定 / 等级 / 创造调色板；QML 据 isTool 切方块
//     Image vs ToolIcon Canvas 自绘图标）
//   - scroll(delta) / creativeBlocks() / iconSourceForBlock / nameForBlock（同前；nameForBlock 工具段
//     走 ToolRegistry::displayName，iconSourceForBlock 工具段返空串 → QML 用 ToolIcon 自绘）
//
// 分层（PLAN §2）：本层属 ViewModel，只依赖 World 的 BlockRegistry 数据，**不**依赖
// Renderer/Physics/QtQuick3D。物品→id / 物品→图标 的映射只查 BlockRegistry，不另持方块表副本。
// §4 法律 + §9：UI 布局与 MC 差异化；物品名用通用词，图标取本工程自带 PNG（非 MC 资产）。
class Hotbar : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Hotbar)
    Q_PROPERTY(int selectedSlot READ selectedSlot WRITE setSelectedSlot NOTIFY selectedSlotChanged)
    Q_PROPERTY(int selectedBlockId READ selectedBlockId NOTIFY selectedSlotChanged)
    // 选中栈的**原始**物品 id（t34 工具感知挖掘用）：含工具段（>=0x100），不归一为 Air。
    // 与 selectedBlockId 的差异：选中工具槽时 selectedBlockId→Air（右键不放置），但 selectedItemId
    // 仍是工具 id → player.selectedItem 绑定 → ToolRegistry 据此算挖掘速度 / 掉落判定。
    Q_PROPERTY(int selectedItemId READ selectedItemId NOTIFY selectedSlotChanged)
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    Q_PROPERTY(int slotRevision READ slotRevision NOTIFY slotsChanged)
    // 光标手持物（背包内点击拾取/放置的「拿在鼠标上的物品栈」，id=0 即空手）。创造/生存背包共用同一
    // VM → 两面板共享同一手持栈（在创造背包拾起、切生存背包仍持着）。Main.qml 据此画跟随光标的浮动图标。
    // heldCount 与 heldBlock 共享 NOTIFY=heldBlockChanged（二者强耦合：有 id 必有 count）。
    Q_PROPERTY(int heldBlock READ heldBlock WRITE setHeldBlock NOTIFY heldBlockChanged)
    Q_PROPERTY(int heldCount READ heldCount WRITE setHeldCount NOTIFY heldBlockChanged)

public:
    explicit Hotbar(QObject *parent = nullptr);

    int slotCount() const { return int(m_slots.size()); }
    int selectedSlot() const { return m_selectedSlot; }
    void setSelectedSlot(int slot);
    int selectedBlockId() const;
    int selectedItemId() const; // 选中栈原始 id（工具段透传；不归一 Air）
    int slotRevision() const { return m_slotRevision; }
    int heldBlock() const { return m_heldStack.id; }
    void setHeldBlock(int id);
    int heldCount() const { return m_heldStack.count; }
    void setHeldCount(int n);

    // 工具段判定与属性（t33；供 QML delegate 据 isTool 选方块 Image vs ToolIcon Canvas 自绘）：
    //   - isTool(id)：id 是否工具段（>=0x100）。
    //   - toolTier(id)：工具等级（1=木 2=石 3=铁；0=非工具）。ToolIcon.qml 据 tier 着色镐头。
    //   - creativeTools()：创造调色板的 3 档镐 id（工具不可堆叠，拾取时 heldCount=1）。
    Q_INVOKABLE bool isTool(int itemId) const;
    Q_INVOKABLE int toolTier(int itemId) const;
    Q_INVOKABLE QVariantList creativeTools() const;

    // 每槽物品 id（air=0 即空栈）。越界返回 0。兼容旧消费者（player.selectedBlock 绑定 / 背包 swap）。
    Q_INVOKABLE int blockIdAt(int slot) const;
    // 每槽栈数量（空槽 0）。
    Q_INVOKABLE int countAt(int slot) const;
    // 每槽图标 qrc 路径（统一尺寸的等距立方体图标；空槽返回 ""）。
    Q_INVOKABLE QString iconSourceAt(int slot) const;
    // 每槽物品的中文显示名（HUD/背包标签用；走 BlockRegistry::displayName）。空槽返回空串。
    Q_INVOKABLE QString nameAt(int slot) const;
    // 由物品 id 取图标 qrc 路径 / 中文显示名（创造背包按 id 列方块，复用 hotbar 同一套映射）。
    Q_INVOKABLE QString iconSourceForBlock(int blockId) const;
    Q_INVOKABLE QString nameForBlock(int blockId) const;
    // 槽内容（QVariantList<int>：每槽物品 id）。QML Repeater 以之为 model；配合 slotRevision 触碰
    // 绑定实现刷新。注：方法名不能取 slots —— Qt 关键字宏（signals/slots），会展开成空致编译失败。
    Q_INVOKABLE QVariantList slotList() const;
    // 每栈 count（QVariantList<int>，与 slotList 平行）。QML 数量显示触碰 slotRevision 刷新。
    Q_INVOKABLE QVariantList countList() const;
    // 创造背包网格：全部可放置方块 id（air 除外）。ViewModel 读 BlockRegistry（单一权威）；恒定。
    Q_INVOKABLE QVariantList creativeBlocks() const;
    // 滚轮循环：delta>0 向右（下标+1），delta<0 向左（下标-1），环绕到 [0, slotCount)。
    Q_INVOKABLE void scroll(int delta);

    // ── 栈操作（t32 基础；t36 拾取/丢弃消费）──
    // 直接写入栈 (slot, id, count)；范围 + id 合法性 + count 上限校验；id==0 或 count<=0 → 清空该槽。
    // 改当前选中槽时补发 selectedSlotChanged（驱动 selectedBlockId → player.selectedBlock 刷新）。
    Q_INVOKABLE void setStack(int slot, int id, int count);
    // 智能堆叠放入（t36 拾取消费）：先选中槽（空 / 同 id 可入 ——「入手」语义，用户核心诉求
    // 「手持空→入手；手持有(异)物→入背包」），再其它同 id 槽合并，再空槽；返回未放入数（0=全入）。
    // 非法 id 全额退回。改了选中槽内容时补发 selectedSlotChanged。
    Q_INVOKABLE int addStack(int id, int n);
    // 从 slot 取最多 n 件（不超过该栈实际持有）；返回实际取走数；栈空则 id 归 0。
    Q_INVOKABLE int takeStack(int slot, int n);
    // 单件最大堆叠：方块段 64、工具段（id>=0x100，t33 预留）1（不可堆叠）。
    Q_INVOKABLE int maxStackSize(int id) const;
    // 按模式重置槽内容（创造=8 可放置方块各满栈 + 第 9 空槽 / 生存=全空 / 观察者=不动）。
    // mode 取 PlayerController::Mode 序数：0=Spectator 1=Creative 2=Survival。同时清空光标手持物。
    Q_INVOKABLE void resetForMode(int mode);
    // 兼容旧调用（t18 setSlotBlock）：等同 setStack(slot, id, id==0?0:1)。保留以防遗漏迁移点（如销毁槽清空）。
    Q_INVOKABLE void setSlotBlock(int slot, int blockId);

signals:
    void selectedSlotChanged();
    // 槽内容变更（setStack/addStack/takeStack/resetForMode）。同时驱动 slotRevision 自增 → QML model
    // 绑定整列重建。
    void slotsChanged();
    void heldBlockChanged(); // 光标手持物变更（id 或 count；拾取/放置/丢弃）→ Main.qml 浮动图标 + 数量刷新

private:
    // 9 槽物品栈。构造期填创造风格默认（8 方块满栈 + 第 9 空槽）；切生存由 resetForMode 清空。
    std::vector<ItemStack> m_slots;
    int m_selectedSlot = 0;
    int m_slotRevision = 0;   // 槽内容版本号：每次栈写入自增，供 QML 绑定作 NOTIFY 触发器
    ItemStack m_heldStack;    // 光标手持物（背包点击拾取/放置；id=0=空手）

    void bumpRevision();      // ++m_slotRevision + emit slotsChanged（统一槽内容变更通知）
};

#endif // HOTBAR_H
