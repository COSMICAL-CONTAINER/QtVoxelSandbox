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
    case BlockRegistry::Grass:         return "icon_grass.png";
    case BlockRegistry::Dirt:          return "icon_dirt.png";
    case BlockRegistry::Stone:         return "icon_stone.png";
    case BlockRegistry::Cobble:        return "icon_cobble.png";
    case BlockRegistry::Log:           return "icon_log.png";
    case BlockRegistry::Planks:        return "icon_planks.png";
    case BlockRegistry::Leaves:        return "icon_leaves.png";
    case BlockRegistry::Sand:          return "icon_sand.png";
    case BlockRegistry::CraftingTable: return "icon_crafting_table.png"; // t50 工作台立方体图标
    case BlockRegistry::Furnace:       return "icon_furnace.png";        // t80 熔炉立方体图标
    case BlockRegistry::CoalOre:       return "icon_coal_ore.png";       // t84 煤矿石立方体图标
    case BlockRegistry::IronOre:       return "icon_iron_ore.png";       // t84 铁矿石立方体图标
    case BlockRegistry::Torch:         return "icon_torch.png";          // t88 火把立方体图标（伪光源）
    case BlockRegistry::Chest:         return "icon_chest.png";          // t173 箱子立方体图标（顶盖缝+侧铁箍）
    // t145/t163(d) 不完整方块图标：6 类木制半方块各走自己的区分图标（tools/build_cube_icons.py 程序生成）。
    //   t163(d) slab/stairs/trapdoor/pressure_plate 升级为 **3D dimetric 立体图标**（render_partial_3d 按
    //   实际形状投影：slab 半高 / stairs L 阶 / trapdoor 薄板 / pressure_plate 更薄更小，顶 + 两侧明暗同
    //   完整方块 cube icon）；door/fence 保留 flat 2D 剪影（高板 / 柱档剪影更直观）。6 类同为木板材质 →
    //   共享木纹观感，但形状各异 → hotbar / 创造调色板肉眼即可辨图，不依赖 displayName 区分。
    case BlockRegistry::WoodSlab:          return "icon_wood_slab.png";          // 木板台阶：3D 半高盒
    case BlockRegistry::WoodStairs:        return "icon_wood_stairs.png";        // 木板楼梯：3D L 阶（背墙 + 整步）
    case BlockRegistry::WoodFence:         return "icon_wood_fence.png";         // 木栅栏：2D 柱档剪影
    case BlockRegistry::WoodDoor:          return "icon_wood_door.png";          // 木板门：2D 高板剪影
    case BlockRegistry::WoodTrapdoor:      return "icon_wood_trapdoor.png";      // 木活板门：3D 薄板
    case BlockRegistry::WoodPressurePlate: return "icon_wood_pressure_plate.png";// 木板压力板：3D 更薄更小
    default: return nullptr; // air / 未知 / 工具段：无图标（t33 落地工具图标时扩展）
    }
}

// 物品 id 段：方块段 0..BlockRegistry::Count-1；工具段 id>=0x100（t33，不可堆叠）；
// 材料段 id>=0x200（t50 合成产物：木棒等，可堆叠 64）。
constexpr int kToolIdBase     = 0x100;
constexpr int kMaterialIdBase = 0x200; // 与 RecipeRegistry::MaterialIdBase 同源（t50）
// 方块单栈上限走 BlockRegistry::BlockDef.maxStack（t42 单一权威；MC 1.0 方块标准 64）。
// 创造风格默认填充直接读 maxStack(id)，不再本地硬编码 64（改表即同步）。

// id 合法性：air(0) / 方块段 (0,Count) / 工具段 (>=0x100) / 材料段 (>=0x200)。越段 id 一律拒
// （防 quint8 截断别名）。材料段 t50 新增（木棒等合成产物）。
bool isValidItemId(int id)
{
    return id == 0 || (id > 0 && id < int(BlockRegistry::Count)) || id >= kToolIdBase;
}
} // namespace

Hotbar::Hotbar(QObject *parent)
    : QObject(parent)
    // t49：构造期 9 槽全空（创造物品改由调色板点取到光标→放入 hotbar 槽才有；spec point 2「初始全空」）。
    , m_slots(9, ItemStack{0, 0})
    // t97：构造期 27 主栏槽全空（生存空背包起；三菜单共享同一份 VM 数据）。
    , m_mainSlots(27, ItemStack{0, 0})
{
}

void Hotbar::bumpRevision()
{
    ++m_slotRevision;
    emit slotsChanged();
}

// t97：主栏版本号 bump（同 bumpRevision 的主栏版）。mainSetStack / mainAddStack / addToAny 的 main 分支 /
// resetForMode 调 → 三菜单 delegate 触碰 mainRevision 的绑定重算。
void Hotbar::bumpMainRevision()
{
    ++m_mainRevision;
    emit mainSlotsChanged();
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

// ── 材料段判定（t50）── id >= RecipeRegistry::MaterialIdBase（0x200）。与 isTool 互斥（材料段在工具段之上）。
bool Hotbar::isMaterial(int itemId) const { return itemId >= RecipeRegistry::MaterialIdBase; }

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

// 创造调色板材料段（t114）：木棒 / 煤炭 / 木炭 / 铁原矿 / 铁锭 / 玻璃（材料段 id >= 0x200，
// RecipeRegistry::*Id 命名常量；与 hotbar.cpp 材料段判定 / MaterialIcon 自绘图标同源）。
// 无限源（拾取时满栈 64；创造不耗）。非方块 → 右键不放置（playercontroller selectedBlock 守 Air），
// 与工具段同属「调色板可取、世界不可放」的非方块物品段。
QVariantList Hotbar::creativeMaterials() const
{
    return {
        int(RecipeRegistry::StickId),       // 木棒
        int(RecipeRegistry::CoalId),        // 煤炭
        int(RecipeRegistry::CharcoalId),    // 木炭
        int(RecipeRegistry::IronOreDropId), // 铁原矿
        int(RecipeRegistry::IronIngotId),   // 铁锭
        int(RecipeRegistry::GlassId)        // 玻璃
    };
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
// t50：追加工作台；t80：追加熔炉；t84：追加煤矿/铁矿石；t88：追加火把（伪光源方块，可在创造调色板
// 直接取用，便于测试放置 + 发光精灵效果）。t134：追加 6 类木制半方块（slab/stairs/fence/pressure_plate/
// door/trapdoor），可在创造直接取用测试异形放置 / 开合。
QVariantList Hotbar::creativeBlocks() const
{
    return { int(BlockRegistry::Grass),         int(BlockRegistry::Dirt),  int(BlockRegistry::Stone),
             int(BlockRegistry::Cobble),        int(BlockRegistry::Log),   int(BlockRegistry::Planks),
             int(BlockRegistry::Leaves),        int(BlockRegistry::Sand),
             int(BlockRegistry::CraftingTable), int(BlockRegistry::Furnace),
             int(BlockRegistry::CoalOre),       int(BlockRegistry::IronOre),
             int(BlockRegistry::Torch),
             int(BlockRegistry::Chest),                                    // t173 箱子（右键开 27 槽）
             // t134 木制半方块：
             int(BlockRegistry::WoodSlab),          int(BlockRegistry::WoodStairs),
             int(BlockRegistry::WoodFence),         int(BlockRegistry::WoodPressurePlate),
             int(BlockRegistry::WoodDoor),          int(BlockRegistry::WoodTrapdoor) };
}

QString Hotbar::iconSourceAt(int slot) const
{
    return iconSourceForBlock(blockIdAt(slot));
}

QString Hotbar::iconSourceForBlock(int blockId) const
{
    // 方块段才返回 PNG 路径；工具段（>=0x100，t33）/ 材料段（>=0x200，t50 木棒）图标由 QML 自绘
    // （ToolIcon / 材料图标 Canvas，§9a）→ 返空串，调用方据 isTool / isMaterial 切到对应自绘 delegate。
    // 越界先判再 cast，防 quint8 截断别名。
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
    // 走单一权威：方块段→BlockRegistry::displayName；工具段→ToolRegistry::displayName（t33）；
    // 材料段（t50 木棒 / t85 煤炭·铁原矿·铁锭）→ 本地通用名（材料段无注册表，名简单且少，就近返回）。
    // air / 越界 → 空串。PLAN §9：UI 不另存方块 / 工具名副本。
    if (blockId <= 0) return QString();
    if (blockId >= kMaterialIdBase) {
        // 材料段：木棒 / 煤炭 / 铁原矿 / 铁锭（id 取自 RecipeRegistry 常量，与 blockregistry.cpp 矿石
        // dropId 字面量同源）。任一漏返 → 空串（兜底，UI 不显名但不崩）。
        if (blockId == RecipeRegistry::StickId)       return QStringLiteral("木棒");
        if (blockId == RecipeRegistry::CoalId)        return QStringLiteral("煤炭");
        if (blockId == RecipeRegistry::IronOreDropId) return QStringLiteral("铁原矿");
        if (blockId == RecipeRegistry::IronIngotId)   return QStringLiteral("铁锭");
        // t87 冶炼产物（spec 可选）：沙子→玻璃、原木→木炭。
        if (blockId == RecipeRegistry::GlassId)       return QStringLiteral("玻璃");
        if (blockId == RecipeRegistry::CharcoalId)    return QStringLiteral("木炭");
        return QString();
    }
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

// 智能堆叠放入（t36 拾取消费）。优先序（t74 重排）：
//   0) 所有同 id 未满槽合并（含选中槽）——合并优先于空槽开新。
//   1) 空槽开新栈（选中槽优先，再按索引序）。
// 返回未放入数（0=全入；>0=背包满，caller 据此判「不拾取，entity 留」）。
//
// t74 根因：旧序把「选中槽空→开新栈」排在「合并同 id 槽」前 → 选中槽空时直接开新栈，
// 不查别处已有同 id 未满槽。例：第1槽草(未满) + 第2槽(选中,空) 挖草 → 草应进第1槽却进第2槽，
// 形成同物分散两栈的反直觉结果。新序：合并全程优先于开新，避免「同物分散」。
int Hotbar::addStack(int id, int n)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    int remaining = n;
    bool changed = false;

    // 0) 先扫所有已有同 id 未满槽合并（含选中槽）。合并优先于空槽开新（t74）。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_slots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; changed = true;
        }
    }
    // 1) 空槽开新栈：选中槽优先（「入手」语义：选中空时优先入手），再按索引序补其余空槽。
    if (remaining > 0 && m_selectedSlot >= 0 && m_selectedSlot < int(m_slots.size())) {
        ItemStack &sel = m_slots[size_t(m_selectedSlot)];
        if (sel.id == 0) {
            const int add = std::min(cap, remaining);
            sel = ItemStack{id, add}; remaining -= add; changed = true;
        }
    }
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

// ── t97 主栏 VM 栈操作（27 槽，三菜单共享）──

int Hotbar::mainBlockIdAt(int slot) const
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return int(BlockRegistry::Air);
    return m_mainSlots[size_t(slot)].id;
}

int Hotbar::mainCountAt(int slot) const
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return 0;
    return m_mainSlots[size_t(slot)].count;
}

// 直接写入主栏栈（背包点击放置 / 互换 / 拖拽均分写回主栏用）。校验同 setStack；air/非法 id/count<=0 → 清空。
// 主栏槽与 selectedSlot 无关（不驱动 selectedBlockId），故无 selectedSlotChanged 补发。
void Hotbar::mainSetStack(int slot, int id, int count)
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return;
    if (!isValidItemId(id)) return;
    ItemStack target;
    if (id != 0 && count > 0) {
        const int cap = maxStackSize(id);
        target = ItemStack{id, std::min(count, cap)};
    }
    const ItemStack &cur = m_mainSlots[size_t(slot)];
    if (cur.id == target.id && cur.count == target.count) return;
    m_mainSlots[size_t(slot)] = target;
    qInfo().noquote() << "[inv] mainSetStack slot=" << slot << "id=" << target.id << "count=" << target.count;
    bumpMainRevision();
}

// 智能堆叠放入主栏（同 id 合并 → 空槽开新）。返回未放入数。仅主栏范围（hotbar 由 addStack / addToAny 管）。
int Hotbar::mainAddStack(int id, int n)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    int remaining = n;
    bool changed = false;
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; changed = true;
        }
    }
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add}; remaining -= add; changed = true;
        }
    }
    if (changed) bumpMainRevision();
    return remaining;
}

// 跨 main + hotbar 的智能堆叠（拾取 / 丢弃回栏合并）。优先序（spec t109）：
//   0) main 同 id 未满槽合并（合并全程优先于开新，避免同物分散两栈；spec「main 同 id 合并已在先」）
//   1) hotbar 同 id 未满槽合并
//   2) 空槽开新栈：**hotbar 优先 → main**
// 返回未放入数。returnHeldToHotbar（关包归还光标）+ pickupScan（世界拾取）改调它 → 拾取 / 丢弃回栏能
// 合并进主栏同 id（修「主栏不同步、丢弃回栏不合并」根因；旧 addStack 只看 hotbar 9 槽）。
//
// t109 根因：旧序「main 空 → hotbar 空」让空手拾取先塞主栏空槽，玩家挖块却要翻主栏找 → 违直觉。
// 拾取应优先落入可直接看见的 hotbar（MC 行为同此），故交换两空槽循环：hotbar 空优先于 main 空。
// main 同 id 合并仍先于 hotbar 同 id（已存在栈就地补满优于跨栏开新，保持「同物不分散」）。
int Hotbar::addToAny(int id, int n)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    int remaining = n;
    bool mainChanged = false, slotChanged = false;

    // 0) main 同 id 未满槽合并。
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; mainChanged = true;
        }
    }
    // 1) hotbar 同 id 未满槽合并。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_slots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; slotChanged = true;
        }
    }
    // 2) 空槽开新栈：hotbar 优先 → main（t109：拾取优先 hotbar，玩家挖块直接落在可见栏）。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_slots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add}; remaining -= add; slotChanged = true;
        }
    }
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add}; remaining -= add; mainChanged = true;
        }
    }
    if (mainChanged) bumpMainRevision();
    if (slotChanged) {
        bumpRevision();
        // 选中槽可能被填入新物品（id 变化）→ selectedBlockId 刷新；与 addStack 同理无条件补发。
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
    if (id >= kMaterialIdBase) return 64; // 材料段（t50 木棒等）：可堆叠 64（MC 标准）
    if (id >= kToolIdBase) return 1;      // 工具段（t33）：不可堆叠
    if (id <= 0 || id >= int(BlockRegistry::Count)) return 0; // air / 越界：不可堆叠（无意义）
    // 方块段走 BlockRegistry::BlockDef.maxStack（t42 单一权威；旧硬编码 64 迁移到表查，行为不变）。
    return BlockRegistry::maxStack(quint8(id));
}

// t50 合成桥接：QML 不能直接调 C++ 静态 RecipeRegistry，经 VM 透传（VM 属 Game 同层，向下查 RecipeRegistry）。
// slotIds 为 QVariantList<int>（行优先 id，0=空格）；gridSize = 2 / 3。返回匹配配方的 QVariantMap
// （outputId / outputCount / consumeCount）或空 Map（无匹配 / 输入尺寸非法）。
QVariantMap Hotbar::recipeMatch(const QVariantList &slotIds, int gridSize) const
{
    QVariantMap empty;
    if (gridSize < 2 || gridSize > 3) return empty;
    const int n = gridSize * gridSize;
    if (slotIds.size() < n) return empty;
    // 提取 id 到栈缓冲（RecipeRegistry::match 取 const int*；2×2/3×3 最多 9 格）。
    int grid[9] = {0};
    for (int i = 0; i < n; ++i) {
        bool ok = false;
        const int id = slotIds.at(i).toInt(&ok);
        grid[i] = ok ? id : 0;
    }
    const RecipeRegistry::Recipe *r = RecipeRegistry::match(grid, gridSize);
    if (!r) return empty;
    QVariantMap m;
    m.insert(QStringLiteral("outputId"), r->outputId);
    m.insert(QStringLiteral("outputCount"), r->outputCount);
    m.insert(QStringLiteral("consumeCount"), r->consumeCount);
    return m;
}

// t50：产物能否放入光标（空 / 同 id 且累加不超 maxStack）。透传 RecipeRegistry::canTake。
bool Hotbar::recipeCanTake(int outId, int outCount, int heldId, int heldCount, int maxStack) const
{
    // 构造临时 Recipe 走 canTake（canTake 只读 outputId/outputCount，其余字段无关）。
    RecipeRegistry::Recipe r{};
    r.outputId = outId;
    r.outputCount = outCount;
    return RecipeRegistry::canTake(r, heldId, heldCount, maxStack);
}

// t87 冶炼 / 燃料桥接：透传 SmeltingRegistry 静态查询给 QML（FurnaceUI 的 tick / 槽校验消费）。
// 返回 int（产物 id / 燃烧秒数；0 = 不可冶炼 / 不可燃），QML 友好且与 recipeMatch 的整数语义一致。
int Hotbar::smeltResult(int inputId) const
{
    return SmeltingRegistry::smeltResult(inputId);
}

int Hotbar::fuelBurnSeconds(int fuelId) const
{
    return int(SmeltingRegistry::fuelBurnSeconds(fuelId));
}

// 显式重置槽内容（清空 9 hotbar + 27 主栏 + 光标手持物）。t49 引入时由 Main.qml::onModeChanged 在每次
//   模式切换自动调用；t171 取消该自动调用 —— cycleMode 切模式**保留物品**（用户诉求「创造↔生存切换不清空
//   背包」）。本方法保留为显式重置 API（供未来「清空背包」按钮等场景），不再被模式切换触发。
//   mode 沿用 PlayerController::Mode 序数：1=Creative / 2=Survival → 清空；0=Spectator → 不动（观察者
//   hotbar 隐藏，清空无意义；保留数据以便切回创造 / 生存时仍在）。主栏 27 槽同清（VM 共享，不随面板销毁）。
void Hotbar::resetForMode(int mode)
{
    if (mode == 1 || mode == 2) {
        // Creative / Survival：全空（创造源=调色板无限拾取；生存=空背包起，采集/拾取由 t34-t36 填入）。
        for (ItemStack &s : m_slots) s = ItemStack{0, 0};
        for (ItemStack &s : m_mainSlots) s = ItemStack{0, 0};
    }
    // mode==0（Spectator）：不动（hotbar 隐藏，槽内容无意义）。
    m_heldStack = ItemStack{};
    bumpRevision();
    bumpMainRevision();
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
