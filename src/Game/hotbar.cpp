#include "hotbar.h"

#include <QDebug>
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
// 方块单栈上限走 BlockRegistry::BlockDef.maxStack（t42 单一权威；MC 1.0 方块标准 64）。
// 创造风格默认填充直接读 maxStack(id)，不再本地硬编码 64（改表即同步）。

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
          // 满栈数走 BlockRegistry::BlockDef.maxStack（t42 单一权威；改某方块 maxStack 即自动同步）。
          ItemStack{BlockRegistry::Grass,  BlockRegistry::maxStack(BlockRegistry::Grass)},
          ItemStack{BlockRegistry::Dirt,   BlockRegistry::maxStack(BlockRegistry::Dirt)},
          ItemStack{BlockRegistry::Stone,  BlockRegistry::maxStack(BlockRegistry::Stone)},
          ItemStack{BlockRegistry::Cobble, BlockRegistry::maxStack(BlockRegistry::Cobble)},
          ItemStack{BlockRegistry::Log,    BlockRegistry::maxStack(BlockRegistry::Log)},
          ItemStack{BlockRegistry::Planks, BlockRegistry::maxStack(BlockRegistry::Planks)},
          ItemStack{BlockRegistry::Leaves, BlockRegistry::maxStack(BlockRegistry::Leaves)},
          ItemStack{BlockRegistry::Sand,   BlockRegistry::maxStack(BlockRegistry::Sand)},
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

// 选中栈 id（空栈 / 工具栈→Air）。player.selectedBlock 绑它 → 空栈或工具栈时右键不放置
// （playercontroller 守 Air）。工具非方块、不可放置（t33）：选中工具槽时 selectedBlockId 返 Air，
// 同时 HUD 的 player.selectedBlock 显 #0（无可放置方块），与「工具用于挖掘、非放置」语义一致。
int Hotbar::selectedBlockId() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size()))
        return int(BlockRegistry::Air);
    const int id = m_slots[size_t(m_selectedSlot)].id;
    if (ToolRegistry::isTool(id)) return int(BlockRegistry::Air); // 工具槽 → 视作无可放置方块
    return id;
}

// 选中栈的**原始** id（t34 工具感知挖掘用）：含工具段，不归一 Air。空栈 / 越界 → 0（=Air，
// ToolRegistry 视作空手挖 → speedMul=1、不掉需工具方块）。player.selectedItem 绑它。
int Hotbar::selectedItemId() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size()))
        return 0;
    return m_slots[size_t(m_selectedSlot)].id;
}

// ── 工具段桥接（t33）── 查 ToolRegistry 单一权威，QML delegate 据此选方块 Image vs ToolIcon Canvas。
bool Hotbar::isTool(int itemId) const { return ToolRegistry::isTool(itemId); }

int Hotbar::toolTier(int itemId) const
{
    const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId);
    return t ? t->tier : 0; // 非工具 → 0（ToolIcon 兜底木镐配色）
}

QVariantList Hotbar::creativeTools() const
{
    // 创造调色板 3 档镐（无限源：拾取时 heldCount=1，工具不可堆叠）。
    return {int(ToolRegistry::PickaxeWood), int(ToolRegistry::PickaxeStone), int(ToolRegistry::PickaxeIron)};
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
    // 方块段才返回 PNG 路径；工具段（>=0x100，t33）图标由 QML ToolIcon.qml Canvas 自绘（§9a）→ 返空串，
    // 调用方据 isTool(id) 切到 ToolIcon。越界先判再 cast，防 quint8 截断别名。
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
    // 走单一权威：方块段→BlockRegistry::displayName；工具段→ToolRegistry::displayName（t33）。
    // air / 越界 → 空串。PLAN §9：UI 不另存方块 / 工具名副本。
    if (blockId <= 0) return QString();
    if (ToolRegistry::isTool(blockId)) return ToolRegistry::displayName(blockId);
    if (blockId >= int(BlockRegistry::Count)) return QString();
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
    qInfo().noquote() << "[inv] setStack slot=" << slot << "id=" << target.id << "count=" << target.count;
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

// 智能堆叠放入（t36 拾取消费）。优先序（spec「先选中槽（空/同 id 可入），否则找空槽」）：
//   0) 选中槽：同 id 未满 → 累加（与手持合并）；空槽 → 开新栈（入手）。二选一，同 id 优先于开新栈。
//   1) 其它同 id 槽合并（跳过选中槽）。
//   2) 其它空槽开新栈（跳过选中槽）。
// 返回未放入数（0=全入；>0=背包满，caller 据此判「不拾取，entity 留」）。
int Hotbar::addStack(int id, int n)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    int remaining = n;
    bool changed = false;

    // 0) 选中槽优先（「入手」语义：手持空 → 入手；手持同物 → 合并入手）。
    if (m_selectedSlot >= 0 && m_selectedSlot < int(m_slots.size())) {
        ItemStack &sel = m_slots[size_t(m_selectedSlot)];
        if (remaining > 0 && sel.id == id && sel.count < cap) {
            const int add = std::min(cap - sel.count, remaining);
            sel.count += add; remaining -= add; changed = true;
        } else if (remaining > 0 && sel.id == 0) {
            const int add = std::min(cap, remaining);
            sel = ItemStack{id, add}; remaining -= add; changed = true;
        }
    }
    // 1) 其它同 id 槽合并（跳过选中槽，已在上步处理）。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        if (int(i) == m_selectedSlot) continue;
        ItemStack &s = m_slots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; changed = true;
        }
    }
    // 2) 其它空槽开新栈（跳过选中槽）。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        if (int(i) == m_selectedSlot) continue;
        ItemStack &s = m_slots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add}; remaining -= add; changed = true;
        }
    }
    if (changed) {
        bumpRevision();
        // 选中槽可能被填入新物品（id 变化）→ selectedBlockId 刷新。addStack 拾取场景难预判命中哪槽，
        // 无条件补发（罕见操作，开销可忽）。
        emit selectedSlotChanged();
    }
    qInfo().noquote() << "[inv] addStack id=" << id << " n=" << n << " remaining=" << remaining
                      << " slots=[" << m_slots[0].id << m_slots[1].id << m_slots[2].id << m_slots[3].id
                      << m_slots[4].id << m_slots[5].id << m_slots[6].id << m_slots[7].id << m_slots[8].id << "]";
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
    if (id <= 0 || id >= int(BlockRegistry::Count)) return 0; // air / 越界：不可堆叠（无意义）
    // 方块段走 BlockRegistry::BlockDef.maxStack（t42 单一权威；旧硬编码 64 迁移到表查，行为不变）。
    return BlockRegistry::maxStack(quint8(id));
}

// 按模式重置：Creative 满 / Survival 空 / Spectator 不动。同时清空光标手持物。
void Hotbar::resetForMode(int mode)
{
    if (mode == 1) {
        // Creative：8 可放置方块各满栈 + 第 9 空槽（创造=无限源，每槽充足）。
        // 满栈数走 BlockRegistry::BlockDef.maxStack（t42 单一权威；改某方块 maxStack 即自动同步）。
        const int ids[8] = {BlockRegistry::Grass,  BlockRegistry::Dirt,   BlockRegistry::Stone,
                            BlockRegistry::Cobble, BlockRegistry::Log,    BlockRegistry::Planks,
                            BlockRegistry::Leaves, BlockRegistry::Sand};
        for (size_t i = 0; i < m_slots.size(); ++i) {
            if (i < 8) m_slots[i] = ItemStack{ids[i], BlockRegistry::maxStack(quint8(ids[i]))};
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
    qInfo().noquote() << "[inv] setHeldBlock -> id=" << id << " count=" << m_heldStack.count;
    emit heldBlockChanged();
}

void Hotbar::setHeldCount(int n)
{
    if (n < 0) n = 0;
    if (n == m_heldStack.count) return;
    m_heldStack.count = n;
    emit heldBlockChanged();
}
