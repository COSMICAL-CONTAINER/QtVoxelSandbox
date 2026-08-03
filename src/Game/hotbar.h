#ifndef HOTBAR_H
#define HOTBAR_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include <vector>

#include "blockregistry.h" // 物品 id（方块段 0..Count-1；图标/中文名走单一注册表）
#include "recipe.h"        // 材料段 id（>=0x200，t50 木棒）；nameForBlock 材料段查 RecipeRegistry::StickId
#include "smelting.h"      // t87 冶炼 / 燃料判定（smeltResult / fuelBurnSeconds 桥接到 QML）
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
    Q_PROPERTY(int heldBlock READ heldBlock WRITE setHeldBlock NOTIFY heldBlockChanged)
    Q_PROPERTY(int heldCount READ heldCount WRITE setHeldCount NOTIFY heldBlockChanged)
    // t97 主栏 VM 共享：27 主栏槽（生存背包 / 工作台 / 熔炉三菜单共享同一份）。mainRevision NOTIFY 驱动
    // 三菜单 delegate 触碰刷新（同 hotbar 行 slotRevision 模式）；mainCount CONSTANT=27 供 Repeater model。
    Q_PROPERTY(int mainCount READ mainCount CONSTANT)
    Q_PROPERTY(int mainRevision READ mainRevision NOTIFY mainSlotsChanged)

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

    // t97 主栏 VM（27 槽，三菜单共享）。mainCount 恒 27；mainRevision 随主栏栈写入自增。
    int mainCount() const { return int(m_mainSlots.size()); }
    int mainRevision() const { return m_mainRevision; }

    // 工具段判定与属性（t33；供 QML delegate 据 isTool 选方块 Image vs ToolIcon Canvas 自绘）：
    //   - isTool(id)：id 是否工具段（>=0x100）。
    //   - toolTier(id)：工具等级（1=木 2=石 3=铁；0=非工具）。ToolIcon.qml 据 tier 着色镐头。
    //   - toolType(id)：工具类型（BlockRegistry::ToolType；Pickaxe=1 / Hoe=2 / 0=非工具）。QML 据 type
    //     选镐形（PickaxeGeometry）vs 锄形（HoeGeometry）3D 几何 + ToolIcon 据 type 选镐 vs 锄像素图。
    //   - creativeTools()：创造调色板的工具 id（3 档镐 + 3 档锄，t233；工具不可堆叠，拾取时 heldCount=1）。
    Q_INVOKABLE bool isTool(int itemId) const;
    Q_INVOKABLE int toolTier(int itemId) const;
    Q_INVOKABLE int toolType(int itemId) const;
    Q_INVOKABLE QVariantList creativeTools() const;
    // 创造调色板材料段（t114）：木棒 / 煤炭 / 木炭 / 铁原矿 / 铁锭 / 玻璃（材料段 id >= 0x200，
    // 由 recipe.h RecipeRegistry::*Id 命名常量定义）。材料段与方块段分离 —— 非方块不可右键放置
    // （与工具段同为非方块调色板项），玩家据需取用到 hotbar 槽（合成 / 冶炼原料 / 装饰）。
    Q_INVOKABLE QVariantList creativeMaterials() const;
    // 材料段判定（t50：合成产物木棒等，id >= RecipeRegistry::MaterialIdBase=0x200）。供 QML delegate
    // 据 isMaterial 切到材料图标 Canvas 自绘（细长棕色矩形 = 木棒）。与 isTool 互斥（材料段 > 工具段上界）。
    Q_INVOKABLE bool isMaterial(int itemId) const;
    // t219 不完整方块段判定：id 是否异形方块段 [FirstPartial, LastPartial]（木板台阶 / 楼梯 / 栅栏 /
    //   压力板 / 门 / 活板门）。供 QML 手持 / 掉落贴图据此切 BlockCube（整立方，6 面图集）vs BillboardQuad
    //   平图标（异形在世界内非整立方 → 手持 / 掉落走 dimetric 立体图标 icon_wood_*.png，非「满格木板立方」）。
    //   **闭区间** [FirstPartial, LastPartial]（lessons-learned t194：单边 >= FirstPartial 会误路由段后整立方
    //   如 Chest(22) 进异形路径）。机制等价 MC「不完整方块手持 / 掉落显其立体图标」。
    Q_INVOKABLE bool isPartialBlock(int itemId) const;

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
    // 显式重置槽内容（清空 9 hotbar + 27 主栏 + 光标手持物）。t49 引入时由模式切换自动调用；t171 取消自动
    //   调用（cycleMode 切模式保留物品，用户诉求「创造↔生存切换不清空背包」）。保留为显式 API（供「清空
    //   背包」等场景）。mode 取 PlayerController::Mode 序数：0=Spectator 1=Creative 2=Survival；1/2 清空、0 不动。
    Q_INVOKABLE void resetForMode(int mode);
    // 兼容旧调用（t18 setSlotBlock）：等同 setStack(slot, id, id==0?0:1)。保留以防遗漏迁移点（如销毁槽清空）。
    Q_INVOKABLE void setSlotBlock(int slot, int blockId);

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
    Q_INVOKABLE void mainSetStack(int slot, int id, int count);
    Q_INVOKABLE int mainAddStack(int id, int n);
    Q_INVOKABLE int addToAny(int id, int n);

signals:
    void selectedSlotChanged();
    // 槽内容变更（setStack/addStack/takeStack/resetForMode）。同时驱动 slotRevision 自增 → QML model
    // 绑定整列重建。
    void slotsChanged();
    void heldBlockChanged(); // 光标手持物变更（id 或 count；拾取/放置/丢弃）→ Main.qml 浮动图标 + 数量刷新
    // t97 主栏栈变更（mainSetStack / mainAddStack / addToAny 的 main 分支 / resetForMode）。同时驱动
    // mainRevision 自增 → 三菜单 delegate 触碰 mainRevision 的绑定重算（图标 / 数量同步刷新）。
    void mainSlotsChanged();

private:
    // 9 槽物品栈。t49：构造期全空（创造物品改由调色板点取→放入 hotbar 槽；不再预置 8 满栈）。
    std::vector<ItemStack> m_slots;
    int m_selectedSlot = 0;
    int m_slotRevision = 0;   // 槽内容版本号：每次栈写入自增，供 QML 绑定作 NOTIFY 触发器
    ItemStack m_heldStack;    // 光标手持物（背包点击拾取/放置；id=0=空手）

    // t97 主栏 VM（27 槽，三菜单共享）。构造期全空（生存空背包起；创造主栏不显，但仍持空数据无副作用）。
    std::vector<ItemStack> m_mainSlots;
    int m_mainRevision = 0;   // 主栏内容版本号：每次主栏栈写入自增，供三菜单 delegate 绑定作 NOTIFY 触发器

    void bumpRevision();      // ++m_slotRevision + emit slotsChanged（统一 hotbar 9 槽内容变更通知）
    void bumpMainRevision();  // ++m_mainRevision + emit mainSlotsChanged（统一主栏 27 槽内容变更通知）
};

#endif // HOTBAR_H
