#include "hotbar.h"

namespace {
// 方块 id → 等距立方体图标文件名（顶 + 两侧，统一尺寸），由 tools/build_cube_icons.py
// 从 textures/ 下既有面贴图烘焙而成（复用图集 tile 合成，非 MC 资产；PLAN §2-L）。
// 用立方体而非单面平面贴图：源贴图尺寸不一（16/48 混排）→ 单面图会大小不统一；
// 立方体图标统一画布尺寸 + 顶/侧明暗强化可辨性（grass 顶绿侧褐、log 顶年轮侧树皮…）。
// air / 未知 → 返回 nullptr（无图标）。
const char *iconFileForBlock(quint8 id)
{
    switch (id) {
    case BlockRegistry::Grass:  return "icon_grass.png";
    case BlockRegistry::Dirt:   return "icon_dirt.png";
    case BlockRegistry::Stone:  return "icon_stone.png";
    case BlockRegistry::Cobble: return "icon_cobble.png";
    case BlockRegistry::Log:    return "icon_log.png";
    case BlockRegistry::Planks: return "icon_planks.png";
    case BlockRegistry::Leaves: return "icon_leaves.png";
    case BlockRegistry::Sand:   return "icon_sand.png";
    default: return nullptr; // air / 未知：无图标
    }
}
} // namespace

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

// 拷一份槽内容给 QML 作 Repeater model（QVariantList<int>）。slotRevision 触碰的 model 绑定在
// slotsChanged 后重算 → 返回新数组 → Repeater 整列重建。
QVariantList Hotbar::slotList() const
{
    QVariantList v;
    v.reserve(int(m_slots.size()));
    for (quint8 id : m_slots) v.append(int(id));
    return v;
}

// 创造背包网格用：所有可放置方块 id（实体方块，air 除外）。id 取自 BlockRegistry（单一权威）。
QVariantList Hotbar::creativeBlocks() const
{
    return { int(BlockRegistry::Grass),  int(BlockRegistry::Dirt),  int(BlockRegistry::Stone),
             int(BlockRegistry::Cobble), int(BlockRegistry::Log),   int(BlockRegistry::Planks),
             int(BlockRegistry::Leaves), int(BlockRegistry::Sand) };
}

QString Hotbar::iconSourceAt(int slot) const
{
    return iconSourceForBlock(blockIdAt(slot));
}

QString Hotbar::iconSourceForBlock(int blockId) const
{
    if (blockId < 0 || blockId >= int(BlockRegistry::Count)) return QString(); // 越界先判再 cast，防 quint8 截断别名（如 id=257→grass）
    const char *file = iconFileForBlock(quint8(blockId));
    if (!file) return QString(); // air / 未知槽：无图标
    return QStringLiteral("qrc:/textures/") + QString::fromLatin1(file);
}

QString Hotbar::nameAt(int slot) const
{
    return nameForBlock(blockIdAt(slot));
}

QString Hotbar::nameForBlock(int blockId) const
{
    // 走 BlockRegistry::displayName（单一权威；PLAN §9：UI 不另存方块名副本）。air/越界 → 空串。
    if (blockId < 0 || blockId >= int(BlockRegistry::Count)) return QString();
    return BlockRegistry::displayName(quint8(blockId));
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

void Hotbar::setSlotBlock(int slot, int blockId)
{
    if (slot < 0 || slot >= int(m_slots.size())) return;
    if (blockId < 0 || blockId >= int(BlockRegistry::Count)) return; // 仅合法 id（air 允许 = 清空槽）
    const quint8 id = quint8(blockId);
    if (m_slots[size_t(slot)] == id) return;
    m_slots[size_t(slot)] = id;
    ++m_slotRevision;          // 版本号自增 → QML Repeater 的 model 绑定（触碰 slotRevision）整列重建
    emit slotsChanged();
    // 若改的是当前选中槽 → 手持方块（selectedBlockId）也变了。selectedBlockId 的 NOTIFY 只能挂一个
    // 信号（selectedSlotChanged），故此处补发它，让消费者（player.selectedBlock 绑定 / HUD 手持名）
    // 刷新；非选中槽变更不发，避免无谓重算。
    if (slot == m_selectedSlot) emit selectedSlotChanged();
}
