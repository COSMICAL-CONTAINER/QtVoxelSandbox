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

// 每方块的等距立方体图标（顶 + 两侧，统一尺寸），由 tools/build_cube_icons.py
// 从 textures/ 下既有面贴图烘焙而成（复用图集 tile 合成，非 MC 资产；PLAN §2-L）。
// 用立方体而非单面平面贴图：源贴图尺寸不一（16/48 混排）→ 单面图会大小不统一；
// 立方体图标统一画布尺寸 + 顶/侧明暗强化可辨性（grass 顶绿侧褐、log 顶年轮侧树皮…）。
QString Hotbar::iconSourceAt(int slot) const
{
    const quint8 id = quint8(blockIdAt(slot));
    const char *file = nullptr;
    switch (id) {
    case BlockRegistry::Grass:  file = "icon_grass.png";  break;
    case BlockRegistry::Dirt:   file = "icon_dirt.png";   break;
    case BlockRegistry::Stone:  file = "icon_stone.png";  break;
    case BlockRegistry::Cobble: file = "icon_cobble.png"; break;
    case BlockRegistry::Log:    file = "icon_log.png";    break;
    case BlockRegistry::Planks: file = "icon_planks.png"; break;
    case BlockRegistry::Leaves: file = "icon_leaves.png"; break;
    case BlockRegistry::Sand:   file = "icon_sand.png";   break;
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
