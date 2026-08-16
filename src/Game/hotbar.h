#ifndef HOTBAR_H
#define HOTBAR_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include <vector>

#include "armor.h"        // t345 护甲段 id + 属性（ArmorRegistry；护甲槽 / 减伤 / 图标走它）
#include "blockregistry.h" // 物品 id（方块段 0..Count-1；图标/中文名走单一注册表）
#include "enchantregistry.h" // t475 附魔段 id + 选择逻辑（EnchantRegistry；附魔元数据 / 选择 / 名走它）
#include "recipe.h"        // 材料段 id（>=0x200，t50 木棒）；nameForBlock 材料段查 RecipeRegistry::StickId
#include "smelting.h"      // t87 冶炼 / 燃料判定（smeltResult / fuelBurnSeconds 桥接到 QML）
#include "toolregistry.h"  // 工具段 id（>=0x100）；工具判定 / tier / 中文名 / 创造调色板走工具注册表（t33）

// 物品栈（t32 基础数据模型）：槽位从单一 quint8 block-id 升级为 {itemId, count, durability}，支持堆叠。
//   - id 复用 BlockRegistry id（方块段 0..Count-1，air=0 即空栈）；
//   - 预留工具段 id>=0x100（t33 落地，本任务仅留段；工具不可堆叠 → maxStackSize 返回 1）。
//   - count 上限经 Hotbar::maxStackSize(id)：方块 64、工具 1。
//   - durability（t263）：仅工具有意义（工具 count 恒 1 → 每实例独立耐久）；非工具栈恒 0（inert）。
//     工具实例耐久 ∈ [1, ToolRegistry::maxDurability(id)]；归零即破损（槽位清空，不会出现 durability=0 的工具栈）。
//   - enchants[4]（t475）：附魔元数据 —— 每槽一个 int，按 EnchantRegistry::pack(id,level) 打包
//     （(enchantId<<8)|level；0 = 空槽）。一件物品最多带 4 个附魔（机制等价 MC 1.0 单件多附魔）。仅工具 /
//     武器 / 护甲有意义（可附魔物品）；方块 / 材料段恒全 0（inert）。附魔随物品实例走（同耐久语义：工具 /
//     护甲 count 恒 1 → 每实例独立附魔；背包搬运经 setStack 显式传 enchants 保真）。
//   - 不变式：id==0 当且仅当 count==0（空栈）；非空栈 count 恒 >=1（写入处统一钳制）。
struct ItemStack {
    int id = 0;
    int count = 0;
    int durability = 0;     // t263 工具剩余耐久（非工具 / 空栈 = 0，inert）
    int enchants[4] = {0,0,0,0}; // t475 附魔元数据（4 槽 × (id<<8|level)；0 = 空槽；非可附魔物品恒全 0）
    // t477 自定义名（铁砧重命名写；空串 = 用注册表默认名 displayName）。随物品实例走（同耐久 / 附魔语义）。
    //   **当前未进 gatherPlayerState 序列化**（同 t475 附魔的会话内保真、跨存档不持久的对等缺口 —— 重命名
    //   在会话内生效；存档持久化归后续任务统一补附魔 + 名一并落盘）。
    //   显式默认成员初始化（= QString()） suppress -Wmissing-field-initializers：本结构体大量处用部分聚合
    //   初始化（ItemStack{0,0} / {id,count,dur}），无 DMI 的尾字段会触发该 -Wextra 警告（lessons-learned：
    //   项目零警告门按 -Wall -Wextra 口径）。
    QString customName = QString();

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
//   - resetForMode(mode)（显式清空 9 槽 + 主栏 + 光标手持物；t171 起不再由模式切换自动调用，保留为显式 API）
//   - heldBlock / heldCount（光标手持物 id + 数量；背包点击拾取/放置用，跨创造/生存共享同一手持栈）
//   - isTool / toolTier / creativeTools（t33：工具段判定 / 等级 / 创造调色板；QML 据 isTool 切方块
//     Image vs ToolIcon Canvas 自绘图标）
//   - scroll(delta) / creativeBlocks() / iconSourceForBlock / nameForBlock（同前；nameForBlock 工具段
//     走 ToolRegistry::displayName，iconSourceForBlock 工具段返空串 → QML 用 ToolIcon 自绘）
//   - t97 主栏 VM 共享：27 主栏槽（生存背包 / 工作台 / 熔炉三菜单共享同一份；熔炉 3 槽 + 合成格仍本地）。
//     mainCount（恒 27）/ mainRevision（NOTIFY=mainSlotsChanged）/ mainBlockIdAt / mainCountAt /
//     mainSetStack / mainAddStack。QML 三菜单删本地 mainSlots 数组，改读 VM（delegate 触碰 mainRevision，
//     同 hotbar 行 t55/t63 已验证模式）→ 三菜单主栏同步。addToAny(id,n) = 先合并 main 同 id 未满 → 再 hotbar
//     同 id → 再空槽（main → hotbar）；returnHeldToHotbar + pickupScan 改调它（拾取 / 丢弃回栏合并进主栏）。
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
    // t263 heldDurability：手持工具的剩余耐久（随工具实例走；背包内搬运经 InventoryOps 显式同步保真）。
    //   setHeldBlock 切换到新工具时自动填 maxDurability（创造调色板取件=满耐久）；pickup-from-slot 路径
    //   在 setHeldBlock 后紧接 setHeldDurability(slot 旧值) 覆盖为槽内实例耐久（InventoryOps.readSlot 透传）。
    Q_PROPERTY(int heldBlock READ heldBlock WRITE setHeldBlock NOTIFY heldBlockChanged)
    Q_PROPERTY(int heldCount READ heldCount WRITE setHeldCount NOTIFY heldBlockChanged)
    Q_PROPERTY(int heldDurability READ heldDurability WRITE setHeldDurability NOTIFY heldBlockChanged)
    // t475 光标手持物附魔（同 heldDurability 语义：随工具 / 护甲实例走；背包搬运经 setHeldEnchants 保真）。
    //   QML 边界走 Q_INVOKABLE heldEnchants() 取（QVariantList<int> 4 元素，每 = pack 值；lessons-learned：
    //   moc 拒绝 Q_PROPERTY(QVariantList)）；InventoryOps.readSlot / writeSlot 据此透传。空栈 / 非可附魔 → 4 个 0。
    //   NOTIFY 复用 heldBlockChanged（id / count / dur / enchants 强耦合，同信号驱动）。
    // t622 光标手持物自定义名（同 heldDurability / heldEnchants 语义：随物品实例走；槽↔光标整件搬运 / 铁砧
    //   改名产物上光标经本属性保真）。QString 无 QVariantList 的 moc 限制 → 可安全作 Q_PROPERTY，QML 直接
    //   赋值 `hotbar.heldCustomName = x`（同 heldDurability 模式）。NOTIFY 复用 heldBlockChanged。
    Q_PROPERTY(QString heldCustomName READ heldCustomName WRITE setHeldCustomName NOTIFY heldBlockChanged)
    // t97 主栏 VM 共享：27 主栏槽（生存背包 / 工作台 / 熔炉三菜单共享同一份）。mainRevision NOTIFY 驱动
    // 三菜单 delegate 触碰刷新（同 hotbar 行 slotRevision 模式）；mainCount CONSTANT=27 供 Repeater model。
    Q_PROPERTY(int mainCount READ mainCount CONSTANT)
    Q_PROPERTY(int mainRevision READ mainRevision NOTIFY mainSlotsChanged)
    // t345 护甲槽 VM（4 槽：头 / 胸 / 腿 / 脚，生存背包左上装备栏；三菜单不共享护甲 —— 护甲只属玩家自身，
    //   非主栏物品流，故独立 4 槽而非走 m_mainSlots）。armorRevision NOTIFY 驱动 SurvivalInventory 装备栏
    //   delegate 触碰刷新（同 hotbar / main 行 revision 模式）；armorCount CONSTANT=4 供 Repeater model。
    //   totalArmorPoints = 4 装备槽护甲值之和（0..20），驱动 Main.qml 护甲条 + 减伤（spec「icons filling
    //   with total armor」+「armor reduces incoming damage by its armor value」）。
    Q_PROPERTY(int armorCount READ armorCount CONSTANT)
    Q_PROPERTY(int armorRevision READ armorRevision NOTIFY armorSlotsChanged)
    Q_PROPERTY(int totalArmorPoints READ totalArmorPoints NOTIFY armorSlotsChanged)

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
    // t263 手持工具耐久（非工具 / 空手 = 0）。setHeldBlock 切工具 id 时已自动填 max；本 setter 供 pickup-from-slot
    //   路径覆盖为槽内实例耐久（保真搬运）。clamp 到 [0, maxDurability(id)]；非工具 id 写入静默归 0。
    int heldDurability() const { return m_heldStack.durability; }
    void setHeldDurability(int d);
    // t475 光标手持物附魔（QVariantList<int> 4 元素，每 = EnchantRegistry::pack 值；空手 / 非可附魔 → 4 个 0）。
    //   setter 供 pickup-from-slot 路径覆盖为槽内实例附魔（保真搬运）；列表不足 4 元素按 0 补齐，越界截断。
    //   t623：setHeldEnchants 补挂 Q_INVOKABLE —— QVariantList 参数不能走 Q_PROPERTY（moc 限制），而各面板
    //   槽点击处理器经 `hotbar.setHeldEnchants(...)` 裸调（非 Q_PROPERTY 赋值语法），未挂 Q_INVOKABLE 时该
    //   调用运行期 TypeError 被信号处理器静默吞 → **取物附魔恒丢**（「附魔台产物取出变普通」的直接根因）。
    Q_INVOKABLE QVariantList heldEnchants() const;
    Q_INVOKABLE void setHeldEnchants(const QVariantList &enchants);
    // t622 光标手持物自定义名（空串 = 用注册表默认名；空手恒空）。setter 供 pickup-from-slot / 铁砧改名产物
    //   路径覆盖（保真搬运）；空栈写入静默归空。经 Q_PROPERTY 暴露（QML 直接赋值）。
    QString heldCustomName() const { return m_heldStack.customName; }
    void setHeldCustomName(const QString &name);

    // t97 主栏 VM（27 槽，三菜单共享）。mainCount 恒 27；mainRevision 随主栏栈写入自增。
    int mainCount() const { return int(m_mainSlots.size()); }
    int mainRevision() const { return m_mainRevision; }

    // t345 护甲槽 VM。armorCount 恒 4；armorRevision 随护甲栈写入自增；totalArmorPoints = 4 装备槽护甲值之和。
    int armorCount() const { return int(m_armorSlots.size()); }
    int armorRevision() const { return m_armorRevision; }
    int totalArmorPoints() const;

    // 工具段判定与属性（t33；供 QML delegate 据 isTool 选方块 Image vs ToolIcon Canvas 自绘）：
    //   - isTool(id)：id 是否工具段（>=0x100）。
    //   - toolTier(id)：工具等级（1=木 2=石 3=铁；0=非工具）。ToolIcon.qml 据 tier 着色工具头。
    //   - toolType(id)：工具类型（BlockRegistry::ToolType；Pickaxe=1 / Hoe=2 / Axe=3 / Shovel=4 / Sword=5 / 0=非工具）。
    //     QML 据 type 选 5 类工具 3D 几何（Pickaxe/Hoe/Axe/Shovel/Sword Geometry）+ ToolIcon 据 type 选 5 类像素图。
    //   - creativeTools()：创造调色板的工具 id（t264 完整工具集：5 类镐/锄/斧/铲/剑 × 3 档木/石/铁 = 15 件；
    //     工具不可堆叠，拾取时 heldCount=1）。
    Q_INVOKABLE bool isTool(int itemId) const;
    Q_INVOKABLE int toolTier(int itemId) const;
    Q_INVOKABLE int toolType(int itemId) const;
    Q_INVOKABLE QVariantList creativeTools() const;
    // 创造调色板材料段（t114）：木棒 / 煤炭 / 木炭 / 铁原矿 / 铁锭 / 玻璃（材料段 id >= 0x200，
    // 由 recipe.h RecipeRegistry::*Id 命名常量定义）。材料段与方块段分离 —— 非方块不可右键放置
    // （与工具段同为非方块调色板项），玩家据需取用到 hotbar 槽（合成 / 冶炼原料 / 装饰）。
    Q_INVOKABLE QVariantList creativeMaterials() const;
    // 材料段判定（t50：合成产物木棒等，id >= RecipeRegistry::MaterialIdBase=0x200；**含 t345 护甲段** 0x300..）。
    //   供 QML delegate 据 isMaterial 切到材料图标 Canvas 自绘。isMaterial 在全工程是「非方块非工具 → QML 自绘
    //   MaterialIcon」的渲染路由谓词；护甲同属此类 → 亦走 MaterialIcon。与 isTool 互斥（材料段 > 工具段上界）。
    Q_INVOKABLE bool isMaterial(int itemId) const;
    // t219 不完整方块段判定：id 是否异形方块段 [FirstPartial, LastPartial]（木板台阶 / 楼梯 / 栅栏 /
    //   压力板 / 门 / 活板门）。供 QML 手持 / 掉落贴图据此切 BlockCube（整立方，6 面图集）vs BillboardQuad
    //   平图标（异形在世界内非整立方 → 手持 / 掉落走 dimetric 立体图标 icon_wood_*.png，非「满格木板立方」）。
    //   **闭区间** [FirstPartial, LastPartial]（lessons-learned t194：单边 >= FirstPartial 会误路由段后整立方
    //   如 Chest(22) 进异形路径）。机制等价 MC「不完整方块手持 / 掉落显其立体图标」。
    Q_INVOKABLE bool isPartialBlock(int itemId) const;
    // t440 cross 广告牌方块段判定（手持 / 掉落渲染分流用）：cross 植物（草丛 / 作物 / 树苗 / 花 / 蘑菇 /
    //   睡莲 / 甘蔗 / 枯灌木 / 木梯）在世界内是双面对角 cross 广告牌（非整立方），手持 / 掉落须走 flat 2D
    //   图标 BillboardQuad（icon_*.png + alphaCutoff discard 透明底），非 BlockCube 满格立方——cross 贴图是
    //   透明底，BlockCube 材质无 alpha 处理会把透明底当不透明 → 渲成黑底方块（用户实测「火把/花/蘑菇/睡莲
    //   手持+掉落黑底」）。走 BlockRegistry::isCrossBillboard 单一权威（同 mesher cross 段路由谓词），避免
    //   QML 与 mesher 路由漂移。火把（id 13）在 QML 有独立手持 / 掉落分支（火焰动画 + 细立柱比例，mesher
    //   火把亦非 cross 段），故本谓词不含火把；其余 cross 方块统一经本谓词路由进 flat billboard 路径。
    Q_INVOKABLE bool isCrossBlock(int itemId) const;
    // t496 二轮复盘 床方块段判定（手持 / 掉落渲染分流用）：床（ShapeBed 低 3D 模型）在世界内是双格横置异形
    //   （非整立方），手持 / 掉落须走 bed 图标 BillboardQuad（icon_bed_<color>.png + alphaCutoff discard 透明底），
    //   非 BlockCube 满格立方——bed 的 tile 是整张被面色，BlockCube 6 面同色 → 渲成「一块色块」与床不可辨
    //   （用户复盘「第一人称手持拿的是方块立方体」）。走 BlockRegistry::isBed 单一权威（同 mesher bed 段路由
    //   谓词），避免 QML 与 mesher 路由漂移。供 QML 三处手持 / 掉落 delegate 据 isBed 切到 bed 图标 BillboardQuad。
    Q_INVOKABLE bool isBed(int itemId) const;
    // t345 护甲段判定（id 在护甲段 [ArmorIdBase, ArmorIdEnd) 内）。与 isTool / isMaterial 互斥（护甲段在
    //   材料段之上 0x300）。供 QML delegate 据 isArmor 切到护甲自绘图标 + 装备槽校验「部位匹配」。
    Q_INVOKABLE bool isArmor(int itemId) const;
    // t345 护甲部位 / 材质档 / 单件护甲值 / 最大耐久（透传 ArmorRegistry；非护甲 → 0/-1）。QML 装备槽点击
    //   校验「持物部位 == 槽位部位」+ tooltip 显护甲值 + 创造取件初始化耐久用。
    Q_INVOKABLE int armorPiece(int itemId) const;     // ArmorRegistry::ArmorPiece（0 头盔..3 靴子）；非护甲 -1
    Q_INVOKABLE int armorTier(int itemId) const;      // ArmorRegistry::ArmorTier（0 皮革..4 钻石）；非护甲 -1
    Q_INVOKABLE int armorPointsFor(int itemId) const; // 单件护甲值；非护甲 0
    Q_INVOKABLE int armorMaxDurability(int itemId) const; // 单件最大耐久；非护甲 0
    // ── t475 附魔桥接（透传 EnchantRegistry；QML 不能直接调 C++ 静态类）──
    //   - itemEnchantCategory(itemId)：物品的可附魔类别（0=None / 1=Weapon / 2=Tool / 4=Armor；单类别非叠加）。
    //     附魔台 UI 据此判「选中槽物品是否可附魔」+ 据类别选适用附魔池。
    //   - isEnchantable(itemId)：物品是否可附魔（category != None）。附魔台 UI 门控「选项槽 enabled」用。
    //   - enchantDisplayName(enchantId) / enchantMaxLevel(enchantId) / enchantLevelText(level)：附魔名 / 最大等级 /
    //     等级罗马数字后缀（如「III」）；tooltip / 附魔台显示「锐锋 III」用。
    //   - selectEnchantsPreview(category, offeredLevel, seed)：纯查询（不改槽态）—— 给定类别 + 提供等级 + 种子
    //     返回 EnchantRegistry::selectEnchants 结果（QVariantMap list {id,level}）。附魔台 UI 选项槽预览「这次会
    //     附上哪些」+ 点击时 enchantSelected 用同 seed 复算写入（预览 = 写入，机制等价 MC「点槽即所见即所得」）。
    Q_INVOKABLE int itemEnchantCategory(int itemId) const;
    Q_INVOKABLE bool isEnchantable(int itemId) const;
    Q_INVOKABLE QString enchantDisplayName(int enchantId) const;
    Q_INVOKABLE int enchantMaxLevel(int enchantId) const;
    Q_INVOKABLE QString enchantLevelText(int level) const;
    Q_INVOKABLE QVariantList selectEnchantsPreview(int category, int offeredLevel, int seed) const;
    Q_INVOKABLE QString enchantListText(const QVariantList &packed) const;
    // t615 附魔适用 / 冲突精判（透传 EnchantRegistry；铁砧敲附魔书逐条过滤用）：
    //   - enchantApplicableTo(enchantId, itemId)：附魔是否适用**具体物品**（剑类附魔不上镐 / 摔落保护仅靴 /
    //     水上亲和仅头盔等；dev-plan §3 表逐条）。AnvilUI 把书上附魔逐条试写 C 时判「不适用 → 不上（灰显）」。
    //   - enchantConflictsWith(a, b)：两附魔是否互斥（同 exclusiveGroup：锐锋族 / 采集族 / 保护系）。
    //     AnvilUI 判「书上附魔与 C 已有附魔冲突 → 不上（红字冲突）」。
    Q_INVOKABLE bool enchantApplicableTo(int enchantId, int itemId) const;
    Q_INVOKABLE bool enchantConflictsWith(int enchantIdA, int enchantIdB) const;
    // t590 附魔列表文本（tooltip / 槽位角标显示「物品有什么附魔」）：输入 4 槽 packed int（同
    //   ItemStack.enchants[4] 布局，即 enchantsAt / mainEnchantsAt / armorEnchantsAt 返回格式），
    //   逐槽拆包 id/level → 「锐锋 III」「效率 II」… 以换行连接；无附魔 → 空串。供各面板 tooltip
    //   附魔行显示（PLAN §9：附魔名走注册表原创中文通用词，非 MC 专名）。
    // ── t475 槽位附魔元数据读写（QVariantList<int> 4 元素，每 = EnchantRegistry::pack 值；0 = 空槽）──
    //   供 InventoryOps.readSlot / writeSlot 透传（同 durabilityAt / mainDurabilityAt 模式）：搬运工具 / 护甲时
    //   附魔随实例保真。空槽 / 非可附魔物品 → 4 个 0（inert）。
    Q_INVOKABLE QVariantList enchantsAt(int slot) const;
    Q_INVOKABLE QVariantList mainEnchantsAt(int slot) const;
    Q_INVOKABLE QVariantList armorEnchantsAt(int slot) const;
    // ── t477 槽位自定义名读写（铁砧重命名；空串 = 用注册表默认名）── 同 durabilityAt / enchantsAt 模式，
    //   供铁砧 UI 读（结果预览显重命名）+ 写（setCustomName）。空槽 / 无自定义名 → 空串。
    Q_INVOKABLE QString customNameAt(int slot) const;
    Q_INVOKABLE QString mainCustomNameAt(int slot) const;
    Q_INVOKABLE void setCustomName(int slot, const QString &name);
    Q_INVOKABLE void mainSetCustomName(int slot, const QString &name);
    // ── t477 铁砧三功能（作用于**当前选中 hotbar 槽**作为目标；C++ 单一权威算 + 写，UI 仅调 + 据 bool 决定提示）──
    //   机制等价 MC 1.0 铁砧 repair / enchant-merge / rename；目标 = 选中槽物品（简化：MC 铁砧有显式左右槽，
    //   本工程同附魔台 shell 模式操作选中槽，避免复制完整 InventoryOps 光标系统）。
    //   - anvilCanRepairSelected()：选中槽是工具 / 护甲（有耐久）且背包别处（hotbar 其它槽 + 主栏）有同 id 第二件 → true。
    //   - anvilDoRepairSelected()：执行修复（耐久 = min(选中 + 第二件 + 0.12*max, max)；消耗 1 件第二件）→ true；
    //     不满足前置（同 anvilCanRepairSelected）→ false（no-op）。
    //   - anvilCanMergeEnchantsSelected()：选中槽可附魔（category != None）且别处有同 id 带附魔的第二件 → true。
    //   - anvilDoMergeEnchantsSelected()：把第二件附魔合并到选中（逐附魔取 max 等级写入空槽，≤4 槽）+ 消耗第二件 → true。
    //   - anvilDoRenameSelected(name)：选中槽非空且 name 非空 → 写 customName → true（重命名，小 XP 消耗由 UI 调
    //     playerState.spendLevels 后再调本方法；本方法只写名）。
    //   三 Do 方法成功后 emit slotsChanged（槽内容变 → UI 刷新）。消耗材料扣第二件（hotbar 其它槽 + 主栏扫描）。
    Q_INVOKABLE bool anvilCanRepairSelected() const;
    Q_INVOKABLE bool anvilDoRepairSelected();
    Q_INVOKABLE bool anvilCanMergeEnchantsSelected() const;
    Q_INVOKABLE bool anvilDoMergeEnchantsSelected();
    Q_INVOKABLE bool anvilDoRenameSelected(const QString &name);
    // ── t550 铁砧二轮重做：本地左右槽（非选中槽 shell）的修复材料判定（纯查询，UI 算预览 / 消耗）──
    //   - anvilRepairMaterial(itemId)：该物品的修复材料 id（铁剑→铁锭 / 木镐→木板 / 石铲→圆石 / 钻石镐→钻石 /
    //     弓→线 / 剪→铁锭 / 钓竿→线 / 护甲→对应锭或皮革）；不可修复物品 → 0。机制等价 MC 1.0 铁砧修复材料
    //     （MC：木→木板 / 石→圆石 / 铁→铁锭 / 金→金锭 / 钻→钻石；护甲同材质锭）。
    //   - anvilCanRepairMaterial(itemId, materialId)：materialId 是否为 itemId 的修复材料（= 二者映射相等）。
    //     供 QML 在右槽放材料时判「能否触发修复」→ 亮产物槽 + 显等级。纯函数无副作用。
    Q_INVOKABLE int anvilRepairMaterial(int itemId) const;
    Q_INVOKABLE bool anvilCanRepairMaterial(int itemId, int materialId) const;
    // ── t476 附魔效果查询（供 Game 层 attack / mining calc point 直读 + 呈现层减伤算 EPF）──
    //   - selectedItemEnchantLevel(enchantId)：选中槽物品该附魔的等级（0=无；空槽 / 非可附魔 → 0）。
    //     PlayerController 在 attackMob（锐锋 / 亡灵 / 节肢 / 击退 / 燃焰）、updateMining（效率）、finishMiningAt
    //     （精准采集 / 时运）calc point 调，把附魔效果叠进伤害 / 速度 / 掉落（机制等价 MC「手持物品附魔生效」）。
    //   - armorEnchantLevelSum(enchantId)：4 装备槽该附魔等级之和（耐久 / 保护族累加用）。
    //   - armorProtectionFactor(cause)：受击减伤 EPF（0..~20），据 PlayerState::DeathCause 序数取匹配保护族
    //     累加（通用 Protection 每级 1 EPF + 匹配专项每级 2 EPF）。呈现层（Main.qml）在 takeDamage 前按 EPF
    //     算减伤比例（ratio = min(0.85, (armorRatio + epf*0.04))），与既有护甲值减伤叠加。
    int selectedItemEnchantLevel(int enchantId) const;
    int armorEnchantLevelSum(int enchantId) const;
    Q_INVOKABLE int armorProtectionFactor(int cause) const;

    // 每槽物品 id（air=0 即空栈）。越界返回 0。兼容旧消费者（player.selectedBlock 绑定 / 背包 swap）。
    Q_INVOKABLE int blockIdAt(int slot) const;
    // 每槽栈数量（空槽 0）。
    Q_INVOKABLE int countAt(int slot) const;
    // t263 每槽工具剩余耐久（非工具 / 空槽 = 0）。tooltip 显「cur/max」+ 搬运保真读它。
    Q_INVOKABLE int durabilityAt(int slot) const;
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
    // 直接写入栈 (slot, id, count, durability=-1, enchants={}, name="")；范围 + id 合法性 + count 上限校验；id==0 或 count<=0 → 清空该槽。
    //   durability（t263）：-1=自动（工具填 maxDurability=新工具、非工具 0）；>=0=显式（搬运保真，clamp 到 [1,max]）。
    //   enchants（t475）：QVariantList<int> 4 元素（每 = pack 值；缺省空 = 清空附魔）。搬运工具 / 护甲时透传槽内实例附魔保真。
    //   name（t622）：自定义名（铁砧重命名产物；缺省空 = 清名 = 注册表默认名）。整件搬运（拾取 / 放置 / 互换 /
    //     回栏 / 存档回填）透传实例名保真；可堆叠物品合并路径不传名（同附魔语义）。
    //   改当前选中槽时补发 selectedSlotChanged（驱动 selectedBlockId → player.selectedBlock 刷新）。
    Q_INVOKABLE void setStack(int slot, int id, int count, int durability = -1, const QVariantList &enchants = {}, const QString &name = QString());
    // 智能堆叠放入（t36 拾取消费）：先选中槽（空 / 同 id 可入 ——「入手」语义，用户核心诉求
    // 「手持空→入手；手持有(异)物→入背包」），再其它同 id 槽合并，再空槽；返回未放入数（0=全入）。
    // 非法 id 全额退回。改了选中槽内容时补发 selectedSlotChanged。
    //   durability（t263）：-1=自动（工具新实例满耐久，世界拾取 / 合成产物场景）；>=0=显式（掉落物拾取保真）。
    //   enchants（t475）：同 setStack（工具 / 护甲单件入空槽时写其实例附魔；可堆叠物品合并路径附魔恒 0）。
    //   name（t622）：同 enchants（工具 / 护甲 / 附魔书等 cap=1 物品空槽开新时写实例名；合并路径不传）。
    Q_INVOKABLE int addStack(int id, int n, int durability = -1, const QVariantList &enchants = {}, const QString &name = QString());
    // 从 slot 取最多 n 件（不超过该栈实际持有）；返回实际取走数；栈空则 id 归 0。
    Q_INVOKABLE int takeStack(int slot, int n);
    // 单件最大堆叠：方块段 64、工具段（id>=0x100，t33 预留）1（不可堆叠）。
    Q_INVOKABLE int maxStackSize(int id) const;
    // t263 工具最大耐久（透传 ToolRegistry::maxDurability；非工具 → 0）。QML tooltip / 创造取件初始化用。
    Q_INVOKABLE int toolMaxDurability(int id) const;
    // t304 弓箭最大伤害（满蓄力命中 HP；spec「弓伤害 tooltip」）。仅弓（toolType===Bow）有意义；其余返 0。
    //   QML tooltip 据本值显「攻击 1-N」（蓄力 1..N HP）；弓近战走徒手伤害（ToolRegistry::attackDamage 兜底），
    //   远程伤害由蓄力 + 箭命中决定（PlayerController bow fire / EntityManager Arrow）。
    Q_INVOKABLE int bowArrowMaxDamage() const;
    // t263 消耗选中槽工具 1 点耐久（生存挖掘完成 / 锄耕地调用）。非工具 / 空槽 → no-op；
    //   耐久归零 → 清空槽（工具破损消失）+ emit slotsChanged + emit toolBroken（t315 破损音）。创造模式由
    //   caller 不调本方法（不消耗）。
    Q_INVOKABLE void damageSelectedItem();
    // t474 跨槽材料消耗（附魔台每次附魔扣 1/2/3 青金石；青金石在 hotbar / 主栏任意槽散堆）：
    //   consumeMaterial(id, n)：扫全部 hotbar + 主栏槽，凑足 n 件 id 物品即扣（按槽逐个 takeStack，
    //   槽满扣后清空 id 移到下一槽）；凑不足则**回滚已扣**（恢复原态）+ 返 false（caller 不应推进附魔）。
    //   成功扣足 → emit slotsChanged + 返 true。id<=0 / n<=0 → 返 true（无消耗，防御）。机制等价 MC
    //   附魔台从背包任意位置扣青金石。单一权威：跨槽扣材料只此一处（避免各 UI 自写遍历）。
    Q_INVOKABLE bool consumeMaterial(int id, int n);
    // t474 跨槽材料计数（附魔 UI 显示「当前青金石 N」+ 点附魔槽前置「青金石 >= 消耗 ?」判定）。
    //   扫全部 hotbar + 主栏槽，返 id 物品总数（同 id 累加）。id<=0 → 0。只读，不改槽态。
    Q_INVOKABLE int materialCount(int id) const;
    // t50 合成桥接（QML 不能直接调 C++ 静态类 RecipeRegistry，经 VM 透传）：
    //   - recipeMatch(slotIds, gridSize)：slotIds 为行优先 id 数组（QVariantList<int>，0=空格），
    //     gridSize = 2（背包 2×2）/ 3（工作台 3×3）。返回匹配配方（QVariantMap：outputId/outputCount/
    //     consumeCount）或空 Map（无匹配）。UI 据此显产物图标 + 点击合成。
    //   - recipeCanTake(outId, outCount, heldId, heldCount, maxStack)：产物能否放入光标（空 / 同 id
    //     且累加不超 maxStack）。UI 点击结果槽前置判定。
    Q_INVOKABLE QVariantMap recipeMatch(const QVariantList &slotIds, int gridSize) const;
    Q_INVOKABLE bool recipeCanTake(int outId, int outCount, int heldId, int heldCount, int maxStack) const;
    // t87 冶炼 / 燃料桥接（QML 不能直接调 C++ 静态类 SmeltingRegistry，经 VM 透传；同 recipeMatch 模式）：
    //   - smeltResult(inputId)：输入物品 → 冶炼产物 id（0=不可冶炼）。
    //   - fuelBurnSeconds(fuelId)：燃料 → 燃烧秒数（0=不可燃；返回 int 秒，QML 友好且本表值均整数）。
    Q_INVOKABLE int smeltResult(int inputId) const;
    Q_INVOKABLE int fuelBurnSeconds(int fuelId) const;
    // t402 冶炼 XP 桥接（同 smeltResult / fuelBurnSeconds 模式）：透传 SmeltingRegistry::smeltXpReward
    //   给 QML（FurnaceUI 检测输出槽取走时按产物 id × 件数产经验球）。返回单件冶炼 XP（0=无 XP）。
    Q_INVOKABLE int smeltXpReward(int outputId) const;
    // 显式重置槽内容（清空 9 hotbar + 27 主栏 + 光标手持物）。t49 引入时由模式切换自动调用；t171 取消自动
    //   调用（cycleMode 切模式保留物品，用户诉求「创造↔生存切换不清空背包」）。保留为显式 API（供「清空
    //   背包」等场景）。mode 取 PlayerController::Mode 序数：0=Spectator 1=Creative 2=Survival；1/2 清空、0 不动。
    Q_INVOKABLE void resetForMode(int mode);
    // 兼容旧调用（t18 setSlotBlock）：等同 setStack(slot, id, id==0?0:1)。保留以防遗漏迁移点（如销毁槽清空）。
    Q_INVOKABLE void setSlotBlock(int slot, int blockId);
    // t314 `/give` 调试聊天命令（spec t314：debug 命令，**任意游戏模式**可调 —— 不属玩法经济，仅测试 / 调试用）。
    //   args = `/give` 之后的剩余串（空表示无参 → 回显用法）。预期格式 `/give <id> [count] [durability]`，
    //   id 可为方块段（1..Count-1）/ 工具段（>=0x100）/ 材料段（>=0x200）任一**已注册**物品；count 缺省 1；
    //   durability 缺省 = maxDurability（仅工具段有意义，非工具恒 0 inert）。
    //   调 addStack 智能堆叠（合并同 id → 入空槽；工具段单件单槽），返回聊天回显文案（spec t346：成功「给予
    //   玩家 <名> ×<n>」；工具 + 显式耐久 → 耐久变体「给予玩家耐久为 <dur> 的 <名> ×<n>」；失败「未知物品 id:
    //   <id>」/「用法: /give <id> [count] [durability]」），由 QML sendChat 经 appendChatMessage("", result,
    //   true) 显灰系统色。物品名走 nameForBlock（§9 通用词：草方块 / 铁剑 / 钻石 ...）；零 MC 专名。
    //   越段 / 未注册 / 非法 count（<1）/ 非法 durability（<1）→ 不改背包，返错误。
    Q_INVOKABLE QString give(const QString &args);

    // ── t97 主栏 VM 栈操作（27 槽，生存背包 / 工作台 / 熔炉三菜单共享同一份；熔炉 3 槽 + 合成格仍本地）──
    //   - mainBlockIdAt(slot) / mainCountAt(slot)：每主栏槽栈数据（air=0=空栈；越界返 0）。QML delegate 触碰
    //     mainRevision 取最新值（同 hotbar 行 slotRevision 模式）。
    //   - mainSetStack(slot, id, count)：直接写主栏栈（背包点击放置 / 互换 / 拖拽均分写回主栏用）。校验同 setStack。
    //   - mainAddStack(id, n)：智能堆叠放入主栏（同 id 合并 → 空槽开新）；返回未放入数（关包归还合成栏到主栏可走它，
    //     但当前 returnCraftToHotbar 仍 addStack 回 hotbar；保留以备主栏级归还）。
    //   - addToAny(id, n)：跨 main + hotbar 的智能堆叠（拾取 / 丢弃回栏合并）。先合并 main 同 id 未满槽 → 再
    //     hotbar 同 id → 再空槽（main → hotbar）；返回未放入数。returnHeldToHotbar / pickupScan 改调它，
    //     使丢弃回栏 / 世界拾取能合并进主栏同 id（修「主栏不同步 / 回栏不合并」根因）。
    Q_INVOKABLE int mainBlockIdAt(int slot) const;
    Q_INVOKABLE int mainCountAt(int slot) const;
    // t263 主栏槽工具剩余耐久（非工具 / 空槽 = 0）。同 durabilityAt 的主栏版。
    Q_INVOKABLE int mainDurabilityAt(int slot) const;
    // 直接写入主栏栈（背包点击放置 / 互换 / 拖拽均分写回主栏用）。校验同 setStack；air/非法 id/count<=0 → 清空。
    //   durability（t263）：同 setStack（-1=自动 / >=0=显式保真）。
    //   enchants（t475）：同 setStack。
    //   name（t622）：同 setStack（整件搬运透传实例名）。
    Q_INVOKABLE void mainSetStack(int slot, int id, int count, int durability = -1, const QVariantList &enchants = {}, const QString &name = QString());
    // 智能堆叠放入主栏（同 id 合并 → 空槽开新）。返回未放入数。仅主栏范围（hotbar 由 addStack / addToAny 管）。
    //   durability（t263）：同 addStack。
    //   enchants（t475）：同 addStack。
    //   name（t622）：同 addStack。
    Q_INVOKABLE int mainAddStack(int id, int n, int durability = -1, const QVariantList &enchants = {}, const QString &name = QString());
    // 跨 main + hotbar 的智能堆叠（拾取 / 丢弃回栏合并）。优先序（spec t109）：
    //   durability（t263）：同 addStack（-1=自动 / >=0=显式保真，掉落物拾取场景）。
    //   enchants（t475）：同 addStack。
    //   name（t622）：同 addStack（掉落物拾取保真——改名物品丢出再捡不丢名）。
    Q_INVOKABLE int addToAny(int id, int n, int durability = -1, const QVariantList &enchants = {}, const QString &name = QString());

    // ── t345 护甲槽 VM（4 槽：头 / 胸 / 腿 / 脚）── 玩家自身装备（非物品流，独立 4 槽）。
    //   - armorBlockIdAt(slot) / armorCountAt(slot) / armorDurabilityAt(slot)：每装备槽栈数据（air=0=空；越界返 0）。
    //     QML 装备栏 delegate 触碰 armorRevision 取最新值（同 hotbar / main 行 revision 模式）。
    //   - armorSetStack(slot, id, count, durability)：直接写装备槽（装备 / 脱下用）。校验：slot 范围 + id 须为
    //     护甲段（或 0=清空）+ 部位须匹配该槽（头盔槽只接头盔，MC 行为）+ count 钳 1（护甲不可堆叠）。
    //     非护甲 / 部位不符 → no-op（不改槽、不发信号）；id==0 → 清空该槽（脱下）。
    //   - totalArmorPoints()：4 装备槽护甲值之和（0..20）；Q_PROPERTY 暴露 + 减伤比例算（armorReductionFactor）。
    //   - damageArmor()：受击时每件装备 -1 耐久（spec「degrades on hits」）；归零 → 清空该槽（破损消失）。
    //     由 Main.qml 在 takeDamage 路由后调（每受一次击 4 件各 -1，机制等价 MC 护甲耐久损耗）。
    //   - creativeArmor()：创造调色板护甲段（5 套 × 4 部位 = 20 件；拾取即满耐久单件，供测试 / 直接装备）。
    Q_INVOKABLE int armorBlockIdAt(int slot) const;
    Q_INVOKABLE int armorCountAt(int slot) const;
    Q_INVOKABLE int armorDurabilityAt(int slot) const;
    // t622 装备槽自定义名读（同 customNameAt / mainCustomNameAt 模式；越界 / 无名 → 空串）。护甲可被改名
    //   （铁砧 rename 作用于工具 / 护甲 / 可堆叠物），装备 / 脱下搬运经 InventoryOps 透传保真。
    Q_INVOKABLE QString armorCustomNameAt(int slot) const;
    // 直接写装备槽（装备 / 脱下用）。校验：slot 范围 + id 须为护甲段（或 0=清空）+ 部位须匹配该槽 + count 钳 1。
    //   name（t622）：同 setStack（护甲整件装备 / 脱下透传实例名）。
    Q_INVOKABLE void armorSetStack(int slot, int id, int count, int durability = -1, const QVariantList &enchants = {}, const QString &name = QString());
    Q_INVOKABLE QVariantList creativeArmor() const;
    Q_INVOKABLE void damageArmor();
    // t475 附魔选中槽物品（附魔台点选项槽 → 写附魔元数据到目标物品）。机制等价 MC 1.0 附魔台点槽即附魔。
    //   offeredLevel 1..30（来自 t474 书架加成映射到三槽）；seed 该槽随机种子（与 selectEnchantsPreview 同 seed →
    //   预览 = 写入）。流程：取选中槽物品 → category = itemEnchantCategory(id)；category==None / 空槽 / 已有附魔
    //   → no-op（UI 应已门控；MC 1.0 不允许重复附魔已附魔物品）。selectEnchants(category, offeredLevel, seed) →
    //   写入 item.enchants[4]（清空旧 + 填新；最多 4 个）。bumpRevision + 补发 selectedSlotChanged（附魔显示刷新）。
    //   不改 id / count / durability（附魔是叠加元数据，非替换物品）。返回 true = 已附魔；false = 不附魔物品 / 已有附魔。
    Q_INVOKABLE bool enchantSelected(int offeredLevel, int seed);
    // t377 在世界中右键手持护甲 → 装备 / 互换（spec t377「held armor RIGHT-CLICK = equip/swap」）。
    //   取当前选中槽护甲：空对应部位槽 → 直接装备；占用 → 先把旧件换回选中槽（手持），再装备新件。
    //   返回 true = 已处理（caller PlayerController 据此消费右键，不走 placeBlock）；选中非护甲 → false。
    //   护甲不可堆叠 → count 恒 1；耐久随实例保真搬运。机制等价 MC 1.0 右键装备护甲。
    Q_INVOKABLE bool equipSelectedArmor();

signals:
    void selectedSlotChanged();
    // 槽内容变更（setStack/addStack/takeStack/resetForMode）。同时驱动 slotRevision 自增 → QML model
    // 绑定整列重建。
    void slotsChanged();
    void heldBlockChanged(); // 光标手持物变更（id 或 count；拾取/放置/丢弃）→ Main.qml 浮动图标 + 数量刷新
    // t315 选中槽工具耐久归零破损（damageSelectedItem 归零分支 emit）。itemId = 破损工具 id（QML 据此播
    //   破损音；未来可按工具材质分流音色）。槽位清空在 emit 前已完成（机制等价 MC「工具耐久耗尽即消失」）。
    //   呈现层（Main.qml）经 Connections 路由到 AudioManager.playToolBreak；分层（PLAN §2）：VM 只发语义事件，
    //   不直接调音频（音频层只消费）。
    void toolBroken(int itemId);
    // t97 主栏栈变更（mainSetStack / mainAddStack / addToAny 的 main 分支 / resetForMode）。同时驱动
    // mainRevision 自增 → 三菜单 delegate 触碰 mainRevision 的绑定重算（图标 / 数量同步刷新）。
    void mainSlotsChanged();
    // t345 护甲槽栈变更（armorSetStack / damageArmor / resetForMode）。同时驱动 armorRevision 自增 +
    // totalArmorPoints 重算 → SurvivalInventory 装备栏 delegate + Main.qml 护甲条 / 减伤刷新。
    void armorSlotsChanged();
    // t345 护甲耐久归零破损（damageArmor 归零分支 emit）。itemId = 破损护甲 id（槽位清空在 emit 前完成）。
    //   呈现层可据此播破损音（机制等价 MC 护甲耗尽破损声；当前复用 toolBreak 音，未来按材质分流）。
    void armorBroken(int itemId);

private:
    // 9 槽物品栈。t49：构造期全空（创造物品改由调色板点取→放入 hotbar 槽；不再预置 8 满栈）。
    std::vector<ItemStack> m_slots;
    int m_selectedSlot = 0;
    int m_slotRevision = 0;   // 槽内容版本号：每次栈写入自增，供 QML 绑定作 NOTIFY 触发器
    ItemStack m_heldStack;    // 光标手持物（背包点击拾取/放置；id=0=空手）

    // t97 主栏 VM（27 槽，三菜单共享）。构造期全空（生存空背包起；创造主栏不显，但仍持空数据无副作用）。
    std::vector<ItemStack> m_mainSlots;
    int m_mainRevision = 0;   // 主栏内容版本号：每次主栏栈写入自增，供三菜单 delegate 绑定作 NOTIFY 触发器

    // t345 护甲槽 VM（4 槽：玩家自身装备，非物品流）。构造期全空；slot 0=头盔 1=胸甲 2=护腿 3=靴子
    //   （与 SurvivalInventory 装备栏纵向序 + ArmorRegistry::ArmorPiece 同序）。
    std::vector<ItemStack> m_armorSlots;
    int m_armorRevision = 0;  // 护甲内容版本号：每次护甲栈写入自增，供装备栏 / 护甲条 / 减伤绑定作 NOTIFY 触发器

    void bumpRevision();      // ++m_slotRevision + emit slotsChanged（统一 hotbar 9 槽内容变更通知）
    void bumpMainRevision();  // ++m_mainRevision + emit mainSlotsChanged（统一主栏 27 槽内容变更通知）
    void bumpArmorRevision(); // ++m_armorRevision + emit armorSlotsChanged（统一护甲 4 槽内容变更通知）
};

#endif // HOTBAR_H
