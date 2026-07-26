#ifndef HOTBAR_H
#define HOTBAR_H

#include <QObject>
#include <QString>
#include <QtQml/qqml.h>

#include <vector>

#include "blockregistry.h" // 方块 id（预置槽内容 + 图标映射走单一注册表）

// Hotbar 视图模型（UI/ViewModel 层）：9 槽的方块选择状态 + 槽位内容。
//
// 暴露给 QML：
//   - selectedSlot（当前槽 0..8，可读写）
//   - selectedBlockId（当前手持方块 id，供 PlayerController 右键放置绑定 —— t05）
//   - slotCount（恒 9）
//   - blockIdAt / iconSourceAt / nameAt：每槽方块 id / 图标 qrc 路径 / 内部名
//   - scroll(delta)：滚轮循环切换（指针捕获/未捕获都可用）
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

public:
    explicit Hotbar(QObject *parent = nullptr);

    int slotCount() const { return int(m_slots.size()); }
    int selectedSlot() const { return m_selectedSlot; }
    void setSelectedSlot(int slot);
    int selectedBlockId() const;

    // 每槽方块 id（air=0 即空槽）。越界返回 0。
    Q_INVOKABLE int blockIdAt(int slot) const;
    // 每槽图标 qrc 路径（取该方块最具代表性的面贴图；空槽返回 ""）。
    Q_INVOKABLE QString iconSourceAt(int slot) const;
    // 每槽方块内部名（调试/HUD 用）。空槽返回 "empty"。
    Q_INVOKABLE QString nameAt(int slot) const;
    // 滚轮循环：delta>0 向右（下标+1），delta<0 向左（下标-1），环绕到 [0, slotCount)。
    Q_INVOKABLE void scroll(int delta);

signals:
    void selectedSlotChanged();

private:
    // 9 槽预置内容：1–8 槽放 §4 的 8 方块，第 9 槽留空（air =「无放置」槽）。
    // 第 9 空槽亦是与 MC 默认满槽 hotbar 的区隔点之一（PLAN §9）。
    const std::vector<quint8> m_slots;
    int m_selectedSlot = 0;
};

#endif // HOTBAR_H
