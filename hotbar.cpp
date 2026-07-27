#include "hotbar.h"

#include <algorithm>

namespace {
// 方块 id → 等距立方体图标文件名（顶 + 两侧，统一尺寸），由 tools/build_cube_icons.py
// 从 textures/ 下既有面贴图烘焙而成（复用图集 tile 合成，非 MC 资产；PLAN §2-L）。
// 用立方体而非单面平面贴图：源贴图尺寸不一（16/48 混排）→ 单面图会大小不统一；
// 立方体图标统一画布尺寸 + 顶/侧明暗强化可辨性（grass 顶绿侧褐、log 顶年轮侧树皮…）。
// air / 未知 → 返回 nullptr（无图标）。工具段 id（>=0x100，t33）暂无图标 → 返回 nullptr。
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
    default: return nullptr; // air / 未知 / 工具段：无图标（t33 落地工具图标时扩展）
    }
}

// 物品 id 段：方块段 0..BlockRegistry::Count-1；工具段 id>=0x100（t33 预留，本任务仅留段）。
constexpr int kToolIdBase = 0x100;
constexpr int kBlockMaxStack = 64; // MC 1.0 方块标准堆叠上限

// id 合法性：air(0) / 方块段 (0,Count) / 工具段 (>=0x100)。越段 id 一律拒（防 quint8 截断别名）。
bool isValidItemId(int id)
{
    return id == 0 || (id > 0 && id < int(BlockRegistry::Count)) || id >= kToolIdBase;
}
} // namespace

Hotbar::Hotbar(QObject *parent)
    : QObject(parent)
    , m_slots{
          // 创造风格默认：8 可放置方块各满栈 + 第 9 槽空（与原 hotbar 预置一致，切生存由 resetForMode 清空）。
          ItemStack{BlockRegistry::Grass,  kBlockMaxStack},
          ItemStack{BlockRegistry::Dirt,   kBlockMaxStack},
          ItemStack{BlockRegistry::Stone,  kBlockMaxStack},
          ItemStack{BlockRegistry::Cobble, kBlockMaxStack},
          ItemStack{BlockRegistry::Log,    kBlockMaxStack},
          ItemStack{BlockRegistry::Planks, kBlockMaxStack},
          ItemStack{BlockRegistry::Leaves, kBlockMaxStack},
          ItemStack{BlockRegistry::Sand,   kBlockMaxStack},
          ItemStack{BlockRegistry::Air,    0}, // 第 9 槽：空（选中后右键不放置）
      }
{
}

void Hotbar::bumpRevision()
{
    ++m_slotRevision;
    emit slotsChanged();
}

void Hotbar::setSelectedSlot(int slot)
{
    const int n = int(m_slots.size());
    if (n == 0) return;
    if (slot < 0) slot = 0;        // 数字键 / QML 直选：clamp 到合法区间
    if (slot >= n) slot = n - 1;
    if (slot == m_selectedSlot) return;
    m_selectedSlot = slot;
    emit selectedSlotChanged(); // selectedBlockId 从新选中栈派生，随之刷新
}

// 选中栈 id（空栈→Air）。player.selectedBlock 绑它 → 空栈时右键不放置（playercontroller 守 Air）。
int Hotbar::selectedBlockId() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size()))
        return int(BlockRegistry::Air);
    return m_slots[size_t(m_selectedSlot)].id;
}

int Hotbar::blockIdAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return int(BlockRegistry::Air);
    return m_slots[size_t(slot)].id;
}

int Hotbar::countAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return 0;
    return m_slots[size_t(slot)].count;
}

// 拷一份槽内容给 QML 作 Repeater model（QVariantList<int>：物品 id）。slotRevision 触碰的 model
// 绑定在 slotsChanged 后重算 → 返回新数组 → Repeater 整列重建。
QVariantList Hotbar::slotList() const
{
    QVariantList v;
    v.reserve(int(m_slots.size()));
    for (const ItemStack &s : m_slots) v.append(s.id);
    return v;
}

// 每栈 count（与 slotList 平行）。QML 数量 Text 触碰 slotRevision 刷新（同 model 绑定机制）。
QVariantList Hotbar::countList() const
{
    QVariantList v;
    v.reserve(int(m_slots.size()));
    for (const ItemStack &s : m_slots) v.append(s.count);
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
    // 方块段才查图标；工具段（>=0x100）暂无图标（t33 落地）。越界先判再 cast，防 quint8 截断别名。
    if (blockId <= 0 || blockId >= int(BlockRegistry::Count)) return QString();
    const char *file = iconFileForBlock(quint8(blockId));
    if (!file) return QString();
    return QStringLiteral("qrc:/textures/") + QString::fromLatin1(file);
}

QString Hotbar::nameAt(int slot) const
{
    return nameForBlock(blockIdAt(slot));
}

QString Hotbar::nameForBlock(int blockId) const
{
    // 走 BlockRegistry::displayName（单一权威；PLAN §9：UI 不另存方块名副本）。air/越界/工具段 → 空串。
    if (blockId <= 0 || blockId >= int(BlockRegistry::Count)) return QString();
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

// ── 栈操作 ──

// 直接写入栈。air/非法 id/count<=0 → 清空该槽（id=0,count=0）；否则 count 钳到 maxStackSize(id)。
void Hotbar::setStack(int slot, int id, int count)
{
    if (slot < 0 || slot >= int(m_slots.size())) return;
    if (!isValidItemId(id)) return;
    ItemStack target;
    if (id != 0 && count > 0) {
        const int cap = maxStackSize(id);
        target = ItemStack{id, std::min(count, cap)};
    } // else: 空栈（id=0 或 count<=0 → 清空）
    const ItemStack &cur = m_slots[size_t(slot)];
    if (cur.id == target.id && cur.count == target.count) return;
    m_slots[size_t(slot)] = target;
    bumpRevision();
    // 改当前选中槽 → 手持方块（selectedBlockId）也变了。selectedBlockId 的 NOTIFY 只能挂一个
    // 信号（selectedSlotChanged），故此处补发它，让消费者（player.selectedBlock 绑定 / HUD 手持名）
    // 刷新；非选中槽变更不发，避免无谓重算。
    if (slot == m_selectedSlot) emit selectedSlotChanged();
}

// 兼容旧调用（t18）：单件写入。等同 setStack(slot, id, id==0?0:1)。
void Hotbar::setSlotBlock(int slot, int blockId)
{
    setStack(slot, blockId, blockId == 0 ? 0 : 1);
}

// 智能堆叠放入：同 id 槽先填满，再入空槽。返回未放入数（0=全部放入）。
int Hotbar::addStack(int id, int n)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    int remaining = n;
    bool changed = false;
    // 1) 先填同 id 且未满的槽（合并已有栈）。
    for (ItemStack &s : m_slots) {
        if (remaining <= 0) break;
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add;
            remaining -= add;
            changed = true;
        }
    }
    // 2) 再入首个空槽（开新栈）。
    for (ItemStack &s : m_slots) {
        if (remaining <= 0) break;
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add};
            remaining -= add;
            changed = true;
        }
    }
    if (changed) {
        bumpRevision();
        // 选中槽可能被填入新物品（id 变化）→ selectedBlockId 刷新。addStack 拾取场景难预判命中哪槽，
        // 无条件补发（罕见操作，开销可忽）。
        emit selectedSlotChanged();
    }
    return remaining;
}

// 从 slot 取最多 n 件。返回实际取走数；栈空则 id 归 0（保持空栈不变式）。
int Hotbar::takeStack(int slot, int n)
{
    if (slot < 0 || slot >= int(m_slots.size()) || n <= 0) return 0;
    ItemStack &s = m_slots[size_t(slot)];
    if (s.id == 0 || s.count <= 0) return 0;
    const int take = std::min(s.count, n);
    s.count -= take;
    if (s.count <= 0) s = ItemStack{0, 0};
    bumpRevision();
    if (slot == m_selectedSlot) emit selectedSlotChanged(); // 选中栈可能因取空而变 Air
    return take;
}

int Hotbar::maxStackSize(int id) const
{
    if (id >= kToolIdBase) return 1; // 工具段（t33）：不可堆叠
    return kBlockMaxStack;           // 方块段：64
}

// 按模式重置：Creative 满 / Survival 空 / Spectator 不动。同时清空光标手持物。
void Hotbar::resetForMode(int mode)
{
    if (mode == 1) {
        // Creative：8 可放置方块各满栈 + 第 9 空槽（创造=无限源，每槽充足）。
        const int ids[8] = {BlockRegistry::Grass,  BlockRegistry::Dirt,   BlockRegistry::Stone,
                            BlockRegistry::Cobble, BlockRegistry::Log,    BlockRegistry::Planks,
                            BlockRegistry::Leaves, BlockRegistry::Sand};
        for (size_t i = 0; i < m_slots.size(); ++i) {
            if (i < 8) m_slots[i] = ItemStack{ids[i], kBlockMaxStack};
            else       m_slots[i] = ItemStack{0, 0};
        }
    } else if (mode == 2) {
        // Survival：全空（用户诉求：空背包起；采集/拾取由 t34-t36 填入）。
        for (ItemStack &s : m_slots) s = ItemStack{0, 0};
    }
    // mode==0（Spectator）：不动（hotbar 隐藏，槽内容无意义）。
    m_heldStack = ItemStack{};
    bumpRevision();
    emit heldBlockChanged();           // 手持物被清空 → 浮动图标隐
    emit selectedSlotChanged();        // selectedBlockId 可能因栈变空而变 Air
}

// 光标手持物 id。setHeldBlock(0) 同步清 count；setHeldBlock(非0) 时若 count 为 0 补 1（防「有 id 无
// count」中间态——QML 拾取整栈时会紧接 setHeldCount 覆盖为真实数量）。
void Hotbar::setHeldBlock(int id)
{
    if (!isValidItemId(id)) return;
    if (id == m_heldStack.id) return;
    if (id == 0) {
        m_heldStack = ItemStack{0, 0};
    } else {
        m_heldStack.id = id;
        if (m_heldStack.count <= 0) m_heldStack.count = 1;
    }
    emit heldBlockChanged();
}

void Hotbar::setHeldCount(int n)
{
    if (n < 0) n = 0;
    if (n == m_heldStack.count) return;
    m_heldStack.count = n;
    emit heldBlockChanged();
}
