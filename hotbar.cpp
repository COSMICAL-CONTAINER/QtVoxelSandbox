#include "hotbar.h"

Hotbar::Hotbar(QObject *parent)
    : QObject(parent)
    , m_slots{
          BlockRegistry::Grass,
          BlockRegistry::Dirt,
          BlockRegistry::Stone,
          BlockRegistry::Cobble,
          BlockRegistry::Log,
          BlockRegistry::Planks,
          BlockRegistry::Leaves,
          BlockRegistry::Sand,
          BlockRegistry::Air, // 第 9 槽：空（选中后右键不放置任何方块）
      }
{
}

void Hotbar::setSelectedSlot(int slot)
{
    const int n = int(m_slots.size());
    if (n == 0) return;
    if (slot < 0) slot = 0;        // 数字键 / QML 直选：clamp 到合法区间
    if (slot >= n) slot = n - 1;
    if (slot == m_selectedSlot) return;
    m_selectedSlot = slot;
    emit selectedSlotChanged();
}

int Hotbar::selectedBlockId() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size()))
        return int(BlockRegistry::Air);
    return int(m_slots[size_t(m_selectedSlot)]);
}

int Hotbar::blockIdAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return int(BlockRegistry::Air);
    return int(m_slots[size_t(slot)]);
}

// 每方块最具代表性的面贴图（侧/单一），用于 hotbar 缩略图。
// 文件名与 textures/ 下自带 PNG 对齐（已在 CMake qt_add_resources 中，非 MC 资产）。
QString Hotbar::iconSourceAt(int slot) const
{
    const quint8 id = quint8(blockIdAt(slot));
    const char *file = nullptr;
    switch (id) {
    case BlockRegistry::Grass:  file = "default_grass_side.png"; break; // 草：侧最可辨
    case BlockRegistry::Dirt:   file = "default_dirt.png";       break;
    case BlockRegistry::Stone:  file = "default_stone.png";      break;
    case BlockRegistry::Cobble: file = "default_cobble.png";     break;
    case BlockRegistry::Log:    file = "default_tree.png";       break; // 原木：侧
    case BlockRegistry::Planks: file = "default_wood.png";       break;
    case BlockRegistry::Leaves: file = "default_leaves.png";     break;
    case BlockRegistry::Sand:   file = "default_sand.png";       break;
    default: return QString(); // 空 / 未知槽：无图标
    }
    return QStringLiteral("qrc:/textures/") + QString::fromLatin1(file);
}

QString Hotbar::nameAt(int slot) const
{
    const int id = blockIdAt(slot);
    if (id == BlockRegistry::Air) return QStringLiteral("empty");
    return QString::fromLatin1(BlockRegistry::blockName(quint8(id)));
}

void Hotbar::scroll(int delta)
{
    const int n = int(m_slots.size());
    if (n == 0) return;
    // 仅取方向（±1），对 n 取模环绕 —— 防止触控板一次吐大 delta 跳多格。
    int s = (m_selectedSlot + (delta > 0 ? 1 : -1)) % n;
    if (s < 0) s += n;
    setSelectedSlot(s);
}
