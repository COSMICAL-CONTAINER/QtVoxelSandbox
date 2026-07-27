#ifndef HOTBAR_H
#define HOTBAR_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqml.h>

#include <vector>

#include "blockregistry.h" // 方块 id（预置槽内容 + 图标映射走单一注册表）

// Hotbar 视图模型（UI/ViewModel 层）：9 槽的方块选择状态 + 槽位内容。
//
// 暴露给 QML：
//   - selectedSlot（当前槽 0..8，可读写）
//   - selectedBlockId（当前手持方块 id，供 PlayerController 右键放置绑定 —— t05）
//   - slotCount（恒 9）
//   - slotRevision（int，随槽内容变更自增；NOTIFY slotsChanged。QML 把它「触碰」进 Repeater
//     的 model 绑定 → 槽内容改写后整列重建，图标/手持名同步刷新。为何不直接把 slots 做成
//     Q_PROPERTY(QVariantList)：本工具链 moc 拒绝 Q_PROPERTY 里的 QVariantList 类型；改用
//     Q_INVOKABLE slots() 取数据 + int slotRevision 做 NOTIFY 触发器，行为等价且 moc 安全）
//   - slots()（QVariantList<int>：每槽方块 id；Repeater model）
//   - creativeBlocks()（QVariantList：全部可放置方块 id；创造背包网格用，恒定不变）
//   - blockIdAt / iconSourceAt / nameAt：每槽方块 id / 图标 qrc 路径 / 中文显示名（空槽空串）
//   - iconSourceForBlock / nameForBlock：按方块 id 取图标/中文显示名（创造背包按 id 列方块，复用同一映射）
//   - scroll(delta)：滚轮循环切换（指针捕获/未捕获都可用）
//   - setSlotBlock(slot,id)：创造风格背包（t18）把某槽方块改写（air=清空）
//
// 分层（PLAN §2）：本层属 ViewModel，只依赖 World 的 BlockRegistry 数据，**不**依赖
// Renderer/Physics/QtQuick3D。方块→id / 方块→图标 的映射只查 BlockRegistry，不另持方块表副本。
// §4 法律 + §9：UI 布局与 MC 差异化 —— 仅做 hotbar 选择态本身（不复刻完整 HUD 布局），
// 方块名用通用词，图标取本工程自带 PNG（非 MC 资产）。
class Hotbar : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Hotbar)
    Q_PROPERTY(int selectedSlot READ selectedSlot WRITE setSelectedSlot NOTIFY selectedSlotChanged)
    Q_PROPERTY(int selectedBlockId READ selectedBlockId NOTIFY selectedSlotChanged)
    Q_PROPERTY(int slotCount READ slotCount CONSTANT)
    Q_PROPERTY(int slotRevision READ slotRevision NOTIFY slotsChanged)
    // 光标手持方块（背包内点击拾取/放置的「拿在鼠标上的物品」，0=空手）。创造/生存背包共用同一 VM
    // → 两面板共享同一「手持物」状态（在创造背包拾起、切生存背包仍持着）。Main.qml 据此画跟随光标的浮动图标。
    Q_PROPERTY(int heldBlock READ heldBlock WRITE setHeldBlock NOTIFY heldBlockChanged)

public:
    explicit Hotbar(QObject *parent = nullptr);

    int slotCount() const { return int(m_slots.size()); }
    int selectedSlot() const { return m_selectedSlot; }
    void setSelectedSlot(int slot);
    int selectedBlockId() const;
    int slotRevision() const { return m_slotRevision; }
    int heldBlock() const { return m_heldBlock; }
    void setHeldBlock(int id);

    // 每槽方块 id（air=0 即空槽）。越界返回 0。
    Q_INVOKABLE int blockIdAt(int slot) const;
    // 每槽图标 qrc 路径（统一尺寸的等距立方体图标；空槽返回 ""）。
    Q_INVOKABLE QString iconSourceAt(int slot) const;
    // 每槽方块的中文显示名（HUD/背包标签用；走 BlockRegistry::displayName）。空槽返回空串。
    Q_INVOKABLE QString nameAt(int slot) const;
    // 由方块 id 取图标 qrc 路径 / 中文显示名（创造背包按 id 列方块，复用 hotbar 同一套映射）。
    Q_INVOKABLE QString iconSourceForBlock(int blockId) const;
    Q_INVOKABLE QString nameForBlock(int blockId) const;
    // 槽内容（QVariantList<int>）。QML Repeater 以之为 model；配合 slotRevision 触碰绑定实现刷新。
    // 注：方法名不能取 slots —— 它是 Qt 关键字宏（signals/slots 机制），会展开成空致编译失败。
    Q_INVOKABLE QVariantList slotList() const;
    // 创造背包网格：全部可放置方块 id（air 除外）。ViewModel 读 BlockRegistry（单一权威）；恒定。
    Q_INVOKABLE QVariantList creativeBlocks() const;
    // 滚轮循环：delta>0 向右（下标+1），delta<0 向左（下标-1），环绕到 [0, slotCount)。
    Q_INVOKABLE void scroll(int delta);
    // 创造风格背包（t18）：把某槽方块改为 blockId（air=清空）。范围校验后写 m_slots + 发信号。
    Q_INVOKABLE void setSlotBlock(int slot, int blockId);

signals:
    void selectedSlotChanged();
    // 槽内容变更（setSlotBlock）。同时驱动 slotRevision 自增 → QML model 绑定整列重建。
    void slotsChanged();
    void heldBlockChanged(); // 光标手持物变更（拾取/放置/丢弃）→ Main.qml 浮动图标显隐刷新

private:
    // 9 槽（可写：创造背包装备改写）；预置 §4 的 8 方块，第 9 槽留空（air =「无放置」槽）。
    // 第 9 空槽亦是与 MC 默认满槽 hotbar 的区隔点之一（PLAN §9）。
    std::vector<quint8> m_slots;
    int m_selectedSlot = 0;
    int m_slotRevision = 0; // 槽内容版本号：每次 setSlotBlock 自增，供 QML 绑定作 NOTIFY 触发器
    int m_heldBlock = 0;    // 光标手持方块 id（背包点击拾取/放置；0=空手）。跨创造/生存背包共享。
};

#endif // HOTBAR_H
