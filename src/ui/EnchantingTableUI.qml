import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t515：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   /slotShiftLeft 等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 main/hotbar VM 路由 + slotLeft/slotRight
//   面板 dispatch + 薄委托包装（供 QML 信号处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，
//   消除四面板逐字复制（同 CraftingTableUI / FurnaceUI / ChestUI 模式）。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

// 附魔台 UI（t515 工作台蓝本重做）：右键附魔台方块打开（PlayerController::enchantingTableOpened →
// Main.qml Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t515 用户要求「以工作台为蓝本重做」：现版是 shell-mode 选中槽消耗 UI（无背包），右键能开但用户要的是
//   工作台式「上方功能区 + 底部背包 4 行」布局。本任务把布局换成 CraftingTableUI 蓝本：
//   - 上区「附魔功能区」（t549 重做：真附魔）：左区两输入槽（0=武器/工具槽 + 1=青金石槽）+ 右侧 3 档位
//     选项（消耗 XP 等级 + **槽 1 青金石**，附魔来源 = UI 输入槽非背包任意；点击 → selectEnchants 同
//     seed 写入槽 0 物品附魔元数据，t475/t476 管线）。
//   - 下区「3×9 主物品栏 + 9 hotbar 行」= 底部背包 4 行（与工作台 / 熔炉 / 箱子同布局），玩家可在此放 / 取
//     背包物品（左键整组 / 右键半份 / 拖动均分 / Shift 搬运 / 双击拿同类，同 CraftingTableUI 全套快捷操作）。
//
// t549 三修：① Shift+左键把工具 / 青金石直接放进输入槽（slotShiftLeftEnchant 双向语义）+ 三 UI 开时
//   Shift 不再透传 player（防蹲，Main.qml bagOpen 扩到三 UI）；② 附魔消耗来源 = UI 槽 1 青金石（非背包）；
//   ③ 书架检测 bookshelfPower 触碰 worldEditRev（放 / 破方块重算，修「放书架显示还是 0」）。
//
// 全部 GUI 自绘原创（Rectangle + Text + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点槽），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（heldBlock/heldCount/maxStackSize/iconSourceForBlock/nameForBlock/
    // isTool/isMaterial/slotRevision/mainSetStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 宿主注入：playerState（level / spendLevels）—— 占位附魔功能区消耗 XP 用（功能后补，沿用 t474）。
    property PlayerState playerState
    // 宿主注入：附魔台方块世界坐标（占位功能区书架加成查询用，沿用 t474；theWorld.countBookshelvesAround）。
    property int enchantX: 0
    property int enchantY: 0
    property int enchantZ: 0
    // 宿主注入：World（countBookshelvesAround 查书架数）。声明为 var 避免类型解析耦合（World 是 C++ 单例）。
    property var theWorld: null
    // t549 宿主注入：世界栅格编辑版本号（Main.qml 放 / 破方块自增）。bookshelfPower 绑定触碰它 →
    //   UI 开着时放 / 破书架也能重算书架数（countBookshelvesAround 是 Q_INVOKABLE 无 NOTIFY，
    //   不触碰则绑定永不重算 → 用户报「旁边放书架显示还是 0」根因）。
    property int worldEditRev: 0
    // t619 宿主注入：玩家进度 VM（附魔成功埋点 progress.onEnchanted / onEnchantedBookObtained）。
    //   var 避免类型解析耦合（同 AnvilUI player 模式）。
    property var progress: null
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // 拖出丢弃：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区；同 CraftingTableUI）。
    signal discardHeldRequested()
    // t228：请求宿主把光标手持栈**丢 1 件**为实体（右键拖出面板外；宿主接 player.dropHeldCursorOne）。
    //   左键整栈走 discardHeldRequested，右键逐个走本信号（spec「左键=全丢/右键=逐个」）。
    signal discardHeldOneRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int mainCols: 9
    readonly property int mainRows: 3

    // ── t515 底部背包：27 主物品栏自 hotbar VM（m_mainSlots），与 SurvivalInventory / CraftingTableUI /
    //   FurnaceUI / ChestUI 五菜单共享同一份 → 五菜单主栏同步、returnHeldToHotbar/pickupScan 经 addToAny
    //   能合并进主栏。与 hotbar 共享同一 hotbar VM 光标手持栈；左键整组 / 右键半份同 resolveClick /
    //   resolveRightClick（InventoryOps 单一权威）。本面板无本地容器槽（无 craft / 无 in/fuel/out），故
    //   localReadSlot/localWriteSlot 返回空、localDragGroups 为 []（main/hotbar 全经 VM，无需本地组）。

    // t110：当前指针所在槽的「组:下标」key（供 window.hoveredSlotKey 提升 → 数字键交换 + t167 左键拖动
    //   起点槽）。各槽 HoverHandler onHoveredChanged 维护（进入写、离开按 key 守卫清除，防相邻槽进出竞态
    //   互清）。组名与 readSlot/writeSlot 一致：main / hotbar。
    property string hoveredKey: ""

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    //   DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（slotLeft 单点拾取/放置/合并/互换 /
    //   Shift 搬运），拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler
    //   在 leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」
    //   字符串（去重简单）；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制
    //   （每滑入新格先撤销上轮写入再重分）。main / hotbar 参与；本面板无 craft / in/fuel 本地组。t181 右键拖动
    //   （每格放 1 个）同源算法；dragActive 统一左/右拖动收集门控。均分算法与工作台 / 熔炉 / 箱子同源。
    property bool leftDragActive: false
    property var dragSlots: []              // "main:5" / "hotbar:0"
    property int dragHeldId: 0
    property int dragHeldCount: 0
    property int dragHeldDurability: 0      // t263 拖动期间手持工具耐久快照（松手回填光标保真）
    // t566 修「左键均分失效」：t475 InventoryOps.beginLeftDrag 写 root.dragHeldEnchants，本面板漏声明 →
    //   TypeError 被信号处理器吞 → leftDragActive 恒 false。补声明即恢复（详见 SurvivalInventory 同注释）。
    property var dragHeldEnchants: []       // t475 拖动期间手持附魔快照（松手回填光标保真）
    property string dragHeldName: ""        // t622 拖动期间手持实例名快照（松手 / 早退回填光标保真）
    // t181 右键拖动（每格放 1 个；区别于左键 floor(count/N) 均分）。dragActive 统一左/右拖动收集门控。
    property bool rightDragActive: false
    property var rightDragSlots: []
    property bool rightDragPlaced: false
    property bool dragActive: leftDragActive || rightDragActive
    // t98 实时重分撤销机制：dragOriginal 记每槽 drag 前原始栈（首次 encounter 快照）；dragWritten 记本轮
    // 已写槽。每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginLeftDrag / endLeftDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    // t98 双击合并：lastTapMs/lastTapKey 记上次左键点击（槽 key）的时间戳与 key；280ms 内同槽二次点击 →
    // doMergeSameId（扫 main+hotbar 同 id 累加成满栈、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // ── t544/t549 本地 enchant 组存储：左武器/工具槽（index 0）+ 青金石槽（index 1）。与 hotbar VM 共享同一光标
    //   手持栈 heldBlock/heldCount；左键整组 / 右键半份同 resolveClick / resolveRightClick（InventoryOps 单一
    //   权威）。面板关闭时 returnEnchantToHotbar 把输入槽内容退回背包（同 CraftingTableUI returnCraftToHotbar 模式）。
    //   t549：**耐久 / 附魔随实例保真**（同 AnvilUI anvilDur/anvilEnch 模式）—— 工具进槽 0 须保住实例耐久，
    //   附魔后写入的附魔元数据也存这里（enchantEnch[0]），点产物取出时随实例回光标。
    //   t622：**实例名随实例保真**（enchantNames[0]）——改名工具进附魔台附魔后取出仍带名（修「改名物品
    //   进附魔台出来丢名」）。
    property var enchantSlots:  [0, 0]
    property var enchantCounts: [0, 0]
    property var enchantDur:    [0, 0]
    property var enchantEnch:   [[0,0,0,0], [0,0,0,0]]
    property var enchantNames:  ["", ""]     // t622 本地槽实例名（空串 = 注册表默认名）
    property int enchantRev: 0

    // 取槽附魔元数据（数组未初始化防御 → 4 个 0）。
    function enchAt(idx) {
        const e = root.enchantEnch[idx]
        return (Array.isArray(e) && e.length === 4) ? e : [0, 0, 0, 0]
    }
    // t622 取槽实例名（数组未初始化防御 → 空串）。
    function nameAt(idx) {
        const n = root.enchantNames[idx]
        return (typeof n === "string") ? n : ""
    }
    // 槽 0 物品是否已有附魔（任一 packed 非 0）。
    function slot0HasEnch() {
        const e = root.enchAt(0)
        for (let i = 0; i < 4; ++i) if ((e[i] || 0) !== 0) return true
        return false
    }

    // t544：enchant 两槽参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明 enchant 为可拖拽本地组 →
    //   InventoryOps.groupIsDraggable 放行（addDragSlot 收集、redistributeLive 分发）、doMergeSameId 扫 enchant 槽。
    property var localDragGroups: ["enchant"]
    // t544：enchant 组槽位数（doMergeSameId 扫描范围）。enchantSlots 长 2（武器/工具 + 青金石）。
    function localSlotCount(group) { return group === "enchant" ? root.enchantSlots.length : 0 }

    // ── t515 / t544 / t549 面板专属槽路由：enchant 两槽走本地数组 + 版本号（main/hotbar 由 InventoryOps 统一经 VM）。
    //   readSlot/writeSlot 薄包装委托 InventoryOps（含本地组分发 → 调本处 localReadSlot/localWriteSlot）。
    //   t549：local 槽透传耐久 / 附魔（工具进槽 0 保真；附魔结果写入槽 0 的附魔元数据）。
    //   t622：local 槽透传实例名（改名工具进附魔台 → 附魔取出仍带名）。
    function localReadSlot(group, index) {
        if (group === "enchant")
            return { id: root.enchantSlots[index] || 0, count: root.enchantCounts[index] || 0,
                     durability: root.enchantDur[index] || 0, enchants: root.enchAt(index), name: root.nameAt(index) }
        return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0], name: "" }
    }
    function localWriteSlot(group, index, id, count, durability, enchants, name) {
        if (group !== "enchant") return
        root.enchantSlots[index] = id
        root.enchantCounts[index] = count
        // review rv3：durability 缺省经 InventoryOps 归一为 -1（自动）；本地槽只存实例值（>0）或 0，
        //   防 -1 残留进 enchantDur（returnEnchantToHotbar 的 `-1 || 0` 为真值会透传 -1 → addStack 视作新实例）。
        root.enchantDur[index] = (durability > 0) ? durability : 0
        const e = (Array.isArray(enchants) && enchants.length === 4) ? enchants : [0, 0, 0, 0]
        const arr = root.enchantEnch
        arr[index] = e.slice()
        root.enchantEnch = arr
        // t622 实例名随槽写入（undefined 兜底空串）。doEnchant 翻附魔书 id 时名保留（书若被改名仍带名）。
        root.enchantNames[index] = (typeof name === "string") ? name : ""
        root.enchantRev++
    }
    // 关包归还 enchant 输入槽（spec 同 CraftingTableUI returnCraftToHotbar）：visible→false 时把两槽内容
    //   addStack 回 hotbar（MC 行为：关附魔台界面把输入槽物品退回背包）。t549 耐久 / 附魔随实例归还。
    //   t622 名随实例归还（第 5 参透传）。
    function returnEnchantToHotbar() {
        if (!root.hotbar) return
        for (let i = 0; i < root.enchantSlots.length; ++i) {
            const id = root.enchantSlots[i] || 0
            const n = root.enchantCounts[i] || 0
            if (id !== 0 && n > 0)
                root.hotbar.addStack(id, n, root.enchantDur[i] || 0, root.enchAt(i), root.nameAt(i))
        }
        for (let i = 0; i < root.enchantSlots.length; ++i) {
            root.enchantSlots[i] = 0
            root.enchantCounts[i] = 0
            root.enchantDur[i] = 0
        }
        root.enchantEnch = [[0,0,0,0], [0,0,0,0]]
        root.enchantNames = ["", ""]
        root.enchantRev++
    }
    function resolveClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch, curName) }
    function resolveRightClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch, curName) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    // review rv3：薄包装签名补 durability / enchants 形参透传（对齐 AnvilUI）—— Main.qml dropFromHoveredSlot
    //   （Q 丢弃）经此路径写槽，4 参签名会把算好的实例耐久 / 附魔截掉（清槽路径 dur 缺省 -1 还会在
    //   localWriteSlot 写入残留 -1）。多收实参对 4 参调用点无害（undefined → InventoryOps 缺省语义）。
    //   t622：+ name 第 7 参同透传。
    function writeSlot(group, index, id, count, durability, enchants, name) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants, name) }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → InventoryOps.slotShiftLeft（Shift+左键搬运 main↔hotbar）。
    //   t549：Shift+左键先走 slotShiftLeftEnchant（附魔台专属双向语义：可附魔物→槽 0 / 青金石→槽 1 /
    //   enchant 槽→归背包；同 ChestUI slotShiftLeftChest / FurnaceUI slotShiftLeftFurnace 模式）。
    //   t180：可拖拽组（main/hotbar）双击 → doMergeSameId（拿同类）。resolveClick/resolveRightClick 算法见
    //   InventoryOps（五面板共享，调用点零改动）。
    function slotLeft(group, index) {
        if (window.shiftHeld) { slotShiftLeftEnchant(group, index); return }
        // t180：280ms 内同槽二次点击 + 可拖拽组 → 拿同类（doMergeSameId 扫 main+hotbar 同 id）。
        const key = group + ":" + index
        const now = Date.now()
        const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
        root.lastTapMs = now
        root.lastTapKey = key
        if (isDouble && InventoryOps.groupIsDraggable(root, group)) {
            InventoryOps.doMergeSameId(root, group, index)
            return
        }
        const cur = InventoryOps.readSlot(root, group, index)
        const r = InventoryOps.resolveClick(root, cur.id, cur.count, cur.durability, cur.enchants, cur.name)
        if (!r) return
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
        root.hotbar.heldDurability = r.heldDur
        root.hotbar.setHeldEnchants(r.heldEnch)
        root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
    }
    function slotRight(group, index) {
        const cur = InventoryOps.readSlot(root, group, index)
        const r = InventoryOps.resolveRightClick(root, cur.id, cur.count, cur.durability, cur.enchants, cur.name)
        if (!r) return
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
        root.hotbar.heldDurability = r.heldDur
        root.hotbar.setHeldEnchants(r.heldEnch)
        root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
    }

    // t549 附魔台 Shift+左键双向语义（spec「拿镐子按 shift+左键应把工具直接放进附魔台输入槽」）：
    //   仿 slotShiftLeftChest / slotShiftLeftFurnace 模式（面板专属 dispatch，全组覆盖不回退）：
    //   - main/hotbar 可附魔物（itemEnchantCategory != None 且尚未附魔）→ 整件入槽 0（耐久随实例保真；
    //     已附魔物品 MC 1.0 不允许再进附魔台 → 不入（走下方普通搬运语义退路：直接归包）。
    //   - main/hotbar 青金石（lapisId）→ 整栈并入槽 1（同 id 合并 → 空槽开新；上限 maxStackSize）。
    //   - enchant 槽 0/1 → 整栈归还背包（addToAny：main 同 id 合并 → hotbar 同 id → 空槽）；背包满 → 余数留原槽。
    //   - 非青金石非可附魔物 → 回退通用 slotShiftLeft（main↔hotbar 整理，MC 1.0 附魔台界面同背包整理语义）。
    //   防丢物：并入目标满 / 异物占位 → 余数留源槽（同 addToChest 模式）。
    function slotShiftLeftEnchant(group, index) {
        if (!root.hotbar) return
        if (group === "main" || group === "hotbar") {
            const src = InventoryOps.readSlot(root, group, index)
            if (src.id === 0 || src.count <= 0) return
            // 青金石 → 槽 1（同 id 合并 → 空槽开新；余数留源槽）。
            if (src.id === root.lapisId) {
                const target = InventoryOps.readSlot(root, "enchant", 1)
                if (target.id !== 0 && target.id !== src.id) return   // 异物占位 → 不覆盖
                const cap = root.hotbar.maxStackSize(src.id)
                const space = cap - target.count
                if (space <= 0) return                                // 目标满 → 无操作
                const move = Math.min(space, src.count)
                InventoryOps.writeSlot(root, "enchant", 1, src.id, target.count + move, 0)
                const remain = src.count - move
                InventoryOps.writeSlot(root, group, index, remain > 0 ? src.id : 0, remain,
                                      remain > 0 ? src.durability : 0, remain > 0 ? src.enchants : [0,0,0,0],
                                      remain > 0 ? src.name : "")
                return
            }
            // 可附魔物（工具 / 武器 / 护甲 / t615 书，类别 != None）且槽 0 有物件的「已附魔拒入」守卫 → 入槽 0。
            const cat = root.hotbar.itemEnchantCategory(src.id)
            if (cat !== 0) {
                const target = InventoryOps.readSlot(root, "enchant", 0)
                if (target.id !== 0) return                           // 槽 0 占用 → 不覆盖（先取出再放）
                // MC 1.0：已附魔物品不能再进附魔台。src 来自背包（可带附魔）→ 已附魔不入槽。
                const se = src.enchants
                for (let i = 0; i < 4; ++i) {
                    if (Array.isArray(se) && (se[i] || 0) !== 0) return
                }
                // t615 书可堆叠（maxStack 64）→ 只取 1 本入槽（余数留源槽，防「5 本进槽丢 4 本」）；
                //   工具 / 护甲 maxStack=1 → 恒整件（src.count===1，写 0 与写余数等价）。
                //   t622：实例名随物品入槽（nameAt 透传）。
                InventoryOps.writeSlot(root, group, index, src.count > 1 ? src.id : 0,
                                      Math.max(0, src.count - 1),
                                      src.count > 1 ? src.durability : 0,
                                      src.count > 1 ? src.enchants : [0,0,0,0],
                                      src.count > 1 ? src.name : "")
                InventoryOps.writeSlot(root, "enchant", 0, src.id, 1, src.durability, src.enchants, src.name)
                return
            }
            // 非青金石非可附魔 → 通用 main↔hotbar 搬运（同普通背包整理）。
            InventoryOps.slotShiftLeft(root, group, index)
            return
        }
        if (group === "enchant") {
            const src = InventoryOps.readSlot(root, "enchant", index)
            if (src.id === 0 || src.count <= 0) return
            // 整栈归还背包（addToAny 智能堆叠；背包满 → 余数留原槽，防丢物）。t622 名透传（第 5 参）。
            const remain = root.hotbar.addToAny(src.id, src.count, src.durability, src.enchants, src.name)
            InventoryOps.writeSlot(root, "enchant", index, remain > 0 ? src.id : 0, remain,
                                  remain > 0 ? src.durability : 0, remain > 0 ? src.enchants : [0,0,0,0],
                                  remain > 0 ? src.name : "")
            return
        }
        // 其它组（无）→ 通用退路。
        InventoryOps.slotShiftLeft(root, group, index)
    }

    // ── t79/t98/t108/t167 拖动均分 + t110 Shift/数字键搬运 + t98 双击合并：算法见 InventoryOps
    //   （五面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
    function slotKey(group, index) { return InventoryOps.slotKey(group, index) }
    function dragHasKey(key) { return InventoryOps.dragHasKey(root, key) }
    // t228：判定 root 坐标系点 (x,y) 是否落在面板矩形内（拖出丢弃门控；面板内非槽位松手→不丢）。
    function pointInsidePanel(x, y) { return InventoryOps.pointInsidePanel(root, panel, x, y) }
    function addDragSlot(key) { InventoryOps.addDragSlot(root, key) }
    function beginLeftDrag() { InventoryOps.beginLeftDrag(root) }
    function endLeftDrag() { InventoryOps.endLeftDrag(root) }
    // t181 右键拖动（每格放 1 个）：薄委托包装。
    function beginRightDrag() { InventoryOps.beginRightDrag(root) }
    function endRightDrag() { InventoryOps.endRightDrag(root) }
    function addRightDragSlot(key) { InventoryOps.addRightDragSlot(root, key) }
    // t205 右键拖拽绿框高亮：rightDragHasKey 判本格是否在 rightDragSlots（实际放了物的格集）。
    function rightDragHasKey(key) { return InventoryOps.rightDragHasKey(root, key) }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    // redistributeLive / singleLeftClick / slotShiftLeft：纯内部辅助（仅 InventoryOps 内部 / slotLeft 调用），
    //   算法已入 InventoryOps，此处不再持有副本。

    // ════════════════════════════════════════════════════════════════════════════
    // ── t549 附魔功能区重做：附魔来源 = UI 输入槽（非背包任意位置）──
    // 槽 0 = 待附魔工具 / 武器 / 护甲（须可附魔且未附魔）；槽 1 = 青金石（消耗来源，非背包）。
    // 3 档位选项消耗：XP 等级（playerState.spendLevels）+ **槽 1 青金石**（不再从背包任意扣）。
    // 点击 enabled 档位 → selectEnchantsPreview 同 seed 复算 → 写入槽 0 物品附魔元数据（t475 管线）。
    // 书架加成据 theWorld.countBookshelvesAround(enchantX/Y/Z) 算 → 提升可选档位。
    // 青金石物品 id（RecipeRegistry::LapisId；Core 不依赖 Game 故 hotbar 无导出常量，硬编码 0x236）。
    readonly property int lapisId: 0x236
    // t615 书 / 附魔书物品 id（RecipeRegistry::BookId / EnchantedBookId；附魔台附书载体 → 产附魔书）。
    readonly property int bookId: 0x238
    readonly property int enchantedBookId: 0x227
    // t590 三档消耗：青金石固定 1/2/3；XP 等级随书架加成升（1 档 1-3 级按书架 power）—— baseLevelCost
    //   = 1 + floor(power/7)：0-6 书架 → 1、7-13 书架 → 2、14+ 书架 → 3（满书架 1 档也贵到 3 级）。
    //   三档 = [base, base+1, base+2]（机制等价 MC 1.0 附魔消耗随书架升）。触碰 bookshelfPower（已绑
    //   worldEditRev）→ 放 / 破书架后重算档位消耗。maxLevel（书架解锁档位数）逻辑不变。
    readonly property int baseLevelCost: Math.min(3, Math.max(1, 1 + Math.floor(root.bookshelfPower / 7)))
    readonly property var levelCosts: [root.baseLevelCost, Math.min(5, root.baseLevelCost + 1), Math.min(5, root.baseLevelCost + 2)]
    readonly property var lapisCosts: [1, 2, 3]
    // t549 书架加成（0..15）：触碰 worldEditRev（放 / 破方块自增）—— countBookshelvesAround 是
    //   Q_INVOKABLE 无 NOTIFY，不触碰则 UI 开着放书架永不重算（用户报「显示还是 0」根因）。
    readonly property int bookshelfPower: {
        if (!theWorld) return 0
        const _e = worldEditRev
        return _e >= 0 ? theWorld.countBookshelvesAround(enchantX, enchantY, enchantZ) : 0
    }
    readonly property int maxLevel: Math.min(3, Math.max(1, Math.floor(bookshelfPower / 2) + 1))
    // t549 槽 0 待附魔物（id / 耐久；触碰 enchantRev）。空槽 / 青金石 / 不可附魔 → 0。
    readonly property int enchantItemId: { const _r = root.enchantRev; return _r >= 0 ? (root.enchantSlots[0] || 0) : 0 }
    // 槽 0 物品可附魔（类别 != None）且未附魔（MC 1.0 已附魔物品不能进附魔台）。t615 书（BookId →
    //   category=BookItem=8）亦过本门 → 三档附书产附魔书；附魔书自身（category=None）不可再附。
    readonly property bool itemReady: {
        const _r = root.enchantRev
        if (_r < 0 || !root.hotbar) return false
        if (root.enchantItemId === 0) return false
        if (root.hotbar.itemEnchantCategory(root.enchantItemId) === 0) return false
        return !root.slot0HasEnch()
    }
    // t549 当前青金石数 = **槽 1 内容**（附魔消耗来源是 UI 输入槽，非背包；spec ②）。触碰 enchantRev。
    readonly property int lapisCount: {
        const _r = root.enchantRev
        return _r >= 0 ? (root.enchantSlots[1] === root.lapisId ? (root.enchantCounts[1] || 0) : 0) : 0
    }
    // 当前 XP 等级（绑定 playerState.level NOTIFY levelChanged；低频，升级才发）。
    readonly property int playerLevel: playerState ? playerState.level : 0
    // 「已附魔」flash 状态（点击成功附魔后短暂显绿，~600ms 淡出）。
    property bool justEnchanted: false
    // PERF 护栏：三档 enabled / 消耗 / 附魔结果全部走绑定（itemReady 绑 enchantRev、playerLevel 绑
    //   levelChanged、bookshelfPower 绑 worldEditRev、档位消耗绑 bookshelfPower），低频 NOTIFY 自动重算、
    //   永不 per-frame。无逐帧刷新路径（占位名重投已移除，t590）。
    //   t544 关包归还：visible→false 时把 enchant 输入槽内容退回背包（同 CraftingTableUI returnCraftToHotbar 模式）。
    onVisibleChanged: {
        if (visible) {
            // t590 三档即时预览：槽 0 放入可附魔物 → 档位 enabled 立即刷新（itemReady 绑 enchantRev +
            //   playerLevel 绑 levelChanged + bookshelfPower 绑 worldEditRev 全自动重算，无需点击）。
            //   附魔结果保持 MC 语义选后揭晓（档位只显消耗），故无 refreshOptions 重投占位名逻辑。
        } else {
            returnEnchantToHotbar()
        }
    }
    // t590：移除原占位附魔名重投（refreshOptions/placeholderNames/optionNames）。MC 语义 = 档位只显等级消耗、
    //   选中施放后才揭晓结果（显在物品上：槽位紫光晕 + tooltip 附魔列表）；占位名既不随机又不匹配真结果，
    //   反而误导「这次会附到啥」。
    // t549 真附魔执行（点击 enabled 档位）：消耗 XP 等级 + **槽 1 青金石**（非背包）→ 同 seed 复算
    //   selectEnchantsPreview → 写入槽 0 物品的附魔元数据（t475 管线，预览 = 写入）→ 物品带附魔留槽 0
    //   （玩家左键取走；紫光晕 + tooltip 显示附魔情况）。
    // t615 附书：槽 0 放**书**（BookId，itemEnchantCategory=BookItem=8）→ 附魔后物品 id 翻成**附魔书**
    //   （EnchantedBookId），随机 1-N 条附魔写进其 enchants（书 = 全池随机，MC 语义）。附魔书 maxStack=1、
    //   不可再附（再放 → category=None 不进槽 0）。
    function doEnchant(slotIdx) {
        if (!root.hotbar || !root.playerState) return
        if (!root.itemReady) return
        const lvlCost = root.levelCosts[slotIdx] || 1
        const lapCost = root.lapisCosts[slotIdx] || 1
        // t590 等级 0 也能玩（MC 1.0 语义「低等级只能 1 档」）：1 档在 playerLevel===0 视为可附（消耗 0 级）；
        //   其余档位等级不足 → 拒（affordable 已置灰，此处防御）。
        //   review L6 修：青金石校验**恒生效**（不随 lvl0Tier1 短路）——旧条件 `!lvl0Tier1 && (lvl不足 || 青金
        //   不足)` 在 lvl0Tier1 时连青金石也不校验（防御缺口；UI affordable 置灰常态下遮住，但 doEnchant 可被
        //   其它路径调到）。等级豁免只针对 XP 档（lvl0Tier1），青金石是硬性材料消耗。
        const lvl0Tier1 = root.playerLevel === 0 && slotIdx === 0
        if ((root.playerLevel < lvlCost && !lvl0Tier1) || root.lapisCount < lapCost) return
        // review H1 修：普通左键可把整栈书（如 64 本）放进槽 0（resolveClick B 整栈放置；只有 Shift+左键才有
        //   「只取 1 本」语义），而 doEnchant 产物恒 1 本 → 其余 N-1 本曾被静默销毁。附魔只消耗 1 本：先把余下
        //   (count-1) 本归还背包（addToAny，同 slotShiftLeftEnchant 归还路径；书无耐久 / 无附魔，dur 传 0）。
        //   背包满装不下（remain>0）→ 余数留槽 0（同「余数留源槽」防丢物语义）且本轮不附魔（XP / 青金石均
        //   未扣、书全数保全，玩家腾位后重试）。槽 0 count>1 只可能是书（工具 / 护甲 cap=1 恒单件）。
        const srcCount0 = root.enchantCounts[0] || 0
        if (srcCount0 > 1) {
            const srcId0 = root.enchantSlots[0] || 0
            const remain = root.hotbar.addToAny(srcId0, srcCount0 - 1, 0)
            InventoryOps.writeSlot(root, "enchant", 0, srcId0, 1 + remain, 0)
            if (remain > 0) return
        }
        // t590 offeredLevel 映射：档位 I/II/III 基准 8/15/22 + 书架加成 floor(power/2)（书架 power 进档位池；
        //   15 书架 → 档位 3 offered≈29 近满 30 → 附魔数 3 + 单附魔等级趋 maxLevel）。机制等价 MC「书架提升附魔强度」。
        const offered = ([8, 15, 22][slotIdx] || 8) + Math.floor(root.bookshelfPower / 2)
        const seed = (enchantX * 73856093) ^ (enchantY * 19349663) ^ (enchantZ * 83492791)
                    ^ (root.enchantItemId * 40503) ^ (slotIdx * 7919) ^ (Date.now() & 0xffff)
        // 1) 扣 XP（等级 0 + 1 档 → 视为扣 0 级跳过；其余不足 spendLevels 拒 → 全回滚）。
        if (!lvl0Tier1 && !root.playerState.spendLevels(lvlCost)) return
        // 2) 扣槽 1 青金石（余数写回；不足已被上方 lapisCount 门控拦，此处防御）。
        const remainLapis = Math.max(0, root.lapisCount - lapCost)
        if (remainLapis > 0) InventoryOps.writeSlot(root, "enchant", 1, root.lapisId, remainLapis, 0)
        else                  InventoryOps.writeSlot(root, "enchant", 1, 0, 0, 0)
        // 3) 同 seed 复算选择 → 写入槽 0 附魔元数据（保留耐久）。t615 书 → id 翻附魔书 + 全池随机
        //   （itemEnchantCategory(BookId)=BookItem=8 → selectEnchants 全 14 附魔候选）。
        const cat = root.hotbar.itemEnchantCategory(root.enchantItemId)
        const picks = root.hotbar.selectEnchantsPreview(cat, offered, Math.abs(seed) | 0)
        const newEnch = [0, 0, 0, 0]
        for (let i = 0; i < picks.length && i < 4; ++i) {
            const m = picks[i]
            newEnch[i] = ((m.id << 8) | m.level)
        }
        const outId = (cat === 8) ? root.enchantedBookId : root.enchantItemId   // BookItem=8 → 附魔书
        // t622：翻附魔书 id 时实例名保留（书若被改名，附成附魔书仍带名；工具附魔同理不改名）。
        InventoryOps.writeSlot(root, "enchant", 0, outId, 1,
                               (cat === 8) ? 0 : (root.enchantDur[0] || 0), newEnch, root.nameAt(0))
        // t619 progress 成就埋点：附魔成功 →「附魔师」；附书产附魔书 →「书虫」（CraftingTableUI 同模式：
        //   UI 成功操作末尾调 progress.onXxx，root.progress 由 Main.qml 注入）。
        if (root.progress) {
            root.progress.onEnchanted()
            if (cat === 8) root.progress.onEnchantedBookObtained()
        }
        root.justEnchanted = true
        enchantFlashTimer.restart()
    }
    // ════════════════════════════════════════════════════════════════════════════

    // t167 左键拖动均分总控：DragHandler(LeftButton) 在 root 监听。按下不动时 per-slot 左键 TapHandler 抓
    //   （slotLeft 单点拾取/放置/合并/互换 / Shift 搬运）；拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged
    //   驱动 begin/endLeftDrag。逐槽 HoverHandler 在 leftDragActive 期间收集扫过格（addDragSlot 即
    //   redistributeLive 实时重分）。target:null 防 DragHandler 默认拖动父 Item（面板）。
    DragHandler {
        acceptedButtons: Qt.LeftButton
        target: null
        onActiveChanged: {
            if (active) root.beginLeftDrag()
            else root.endLeftDrag()
        }
    }
    // t181 右键拖动（每格放 1 个）：DragHandler(RightButton) 在 root 监听；拖动越阈值 → begin/endRightDrag。
    //   按下不动时 per-slot 右键 TapHandler 抓（slotRight 拿半 / 放一）。target:null 防 DragHandler 拖动父 Item。
    DragHandler {
        acceptedButtons: Qt.RightButton
        target: null
        onActiveChanged: {
            if (active) root.beginRightDrag()
            else root.endRightDrag()
        }
    }

    // 半透明遮罩：仅吸收点击（防穿透），不关闭面板（E / Esc / closed 信号才关）。
    // 手持物时点遮罩区 → 丢弃为实体（同 CraftingTableUI / FurnaceUI / SurvivalInventory）。t228：左键整栈 /
    //   右键 1 件 + 面板边界判定（面板内非槽位松手→不丢，修「左键拿物在面板内非槽位松手→直接丢地下」bug）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                // 空手仅吸收点击（防穿透），不丢弃。
                if (!root.hotbar || root.hotbar.heldBlock === 0) return
                // 面板内非槽位松手 → 不丢（物品留光标）；只有丢出整栏外才丢。
                if (root.pointInsidePanel(mouse.x, mouse.y)) return
                if (mouse.button === Qt.RightButton) root.discardHeldOneRequested()   // 右键逐个
                else                                root.discardHeldRequested()       // 左键整栈
            }
        }
    }

    // 面板：深色圆角，居中。宽度与 CraftingTableUI 一致（392）便于复用主栏布局；
    // 高度 = 标题(22) + 占位附魔功能区(~196) + 主栏(120) + hotbar(40) + 间距/边距。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 446                                  // 22 + 196 + 120 + 40 (=378) + 3×12 spacing(36) + 2×16 margin(32) = 446
        anchors.centerIn: parent
        radius: 14
        color: "#1b1f24"
        border.color: "#3a444f"
        border.width: 1

        Column {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // 标题行：左标题，右关闭提示。
            Item {
                width: parent.width
                height: 22
                Text {
                    text: "附魔台"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 占位附魔功能区（t544 重做：两槽 + 右侧三选项竖排）──
            // 布局：状态条（XP/青金石/书架→可选档位）+ 左区两槽（武器/工具槽 + 青金石槽）+ 右区 3 档位选项
            //   竖排 + 提示文字。青金石槽空占位画青金石轮廓图标（参考 MaterialIcon drawLapis 的青金石形状）。
            //   真附魔效果后补；选项点击沿用 t474 占位交互（消耗 XP + 青金石 → flash + 重投选项名）。
            Item {
                id: enchantArea
                width: parent.width
                height: 196

                // 上区状态条：XP 等级 / 青金石数 / 书架加成 → 可选档位。
                Rectangle {
                    id: statusbar
                    anchors.top: parent.top; anchors.topMargin: 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width; height: 28
                    color: "#241a12"; radius: 3
                    Text {
                        anchors.centerIn: parent
                        text: "等级 " + playerLevel + "    青金石 " + lapisCount +
                              "    书架 " + bookshelfPower + " → 可选 " +
                              (maxLevel === 1 ? "I" : maxLevel === 2 ? "I-II" : "I-III") + " 档"
                        color: "#ffe6a8"; font.pixelSize: 12
                    }
                }

                // 功能区主体：左区两槽（武器/工具 + 青金石）+ 右区三选项竖排。
                Item {
                    id: body
                    anchors.top: statusbar.bottom; anchors.topMargin: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width
                    height: 124

                    // ── 左区：两槽（enchant 组：0=武器/工具槽，1=青金石槽）。本地数组读写（enchantRev 驱动
                    //   刷新）；左键整组 / 右键半份取放（同主栏 / hotbar，InventoryOps 单一权威）。──
                    Column {
                        id: leftSlots
                        x: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12

                        // 武器 / 工具槽（enchant index 0）。
                        EnchantInputSlot {
                            width: root.slotSize; height: root.slotSize
                            group: "enchant"; index: 0
                            // qml-touch：槽内容读数组 + enchantRev 触碰参与返回（数组写入不触发绑定，需 rev 触碰）。
                            slotId: { const _r = root.enchantRev; return _r >= 0 ? (root.enchantSlots[0] || 0) : 0 }
                            slotCount: { const _r = root.enchantRev; return _r >= 0 ? (root.enchantCounts[0] || 0) : 0 }
                            caption: "武器/工具"
                        }
                        // 青金石槽（enchant index 1；空槽画青金石轮廓占位，指示接受青金石）。
                        EnchantInputSlot {
                            width: root.slotSize; height: root.slotSize
                            group: "enchant"; index: 1
                            slotId: { const _r = root.enchantRev; return _r >= 0 ? (root.enchantSlots[1] || 0) : 0 }
                            slotCount: { const _r = root.enchantRev; return _r >= 0 ? (root.enchantCounts[1] || 0) : 0 }
                            caption: ""
                            showLapisOutline: true   // 空槽占位画青金石轮廓（t544）
                        }
                    }

                    // ── 右区：3 档位选项竖排（1/2/3；t544 现竖排，修用户「横排」反馈）──
                    Column {
                        id: optionCol
                        x: 120
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Repeater {
                            model: 3
                            delegate: Rectangle {
                                id: optSlot
                                property int idx: index
                                property int lvlCost: root.levelCosts[index]
                                property int lapCost: root.lapisCosts[index]
                                // t549 enabled 条件：槽 0 有「可附魔且未附魔」物品（itemReady；附魔来源 =
                                //   UI 输入槽，非背包）+ 档位序号 < maxLevel（书架解锁）+ XP 等级 + 槽 1 青金石都够。
                                property bool unlocked: index < root.maxLevel
                                // t590 等级不足 → 档位置灰；但 1 档在 playerLevel===0 视为可负担（MC 1.0 语义
                                //   「低等级只能 1 档」——等级 0 生存玩家也能玩，消耗 0 级）。
                                property bool affordable: root.lapisCount >= lapCost && (root.playerLevel >= lvlCost || (index === 0 && root.playerLevel === 0))
                                property bool enabled1: root.itemReady && unlocked && affordable
                                width: 190; height: 36
                                color: enabled1 ? "#5a4a2a" : "#2a2018"
                                border.color: enabled1 ? "#ffd87a" : "#0a0604"
                                border.width: enabled1 ? 2 : 1
                                radius: 4
                                opacity: unlocked ? 1.0 : 0.4  // 未解锁档位半透

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 8
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: ["I", "II", "III"][index]
                                        color: enabled1 ? "#ffe6a8" : "#665544"
                                        font.pixelSize: 14; font.bold: true
                                    }
                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 0
                                        Text {
                                            // t590 保持 MC 语义：档位只显等级消耗，附魔结果选中施放后揭晓
                                            //   （显在物品上：槽位紫光晕 + tooltip 附魔列表）。
                                            text: "消耗 " + lvlCost + " 级"
                                            color: enabled1 ? "#ffe6a8" : "#665544"
                                            font.pixelSize: 11; font.bold: true
                                        }
                                        Text {
                                            text: "青金石 " + lapCost
                                            color: enabled1 ? "#a8d8ff" : "#554433"
                                            font.pixelSize: 9
                                        }
                                    }
                                }

                                TapHandler {
                                    enabled: optSlot.enabled1
                                    onTapped: {
                                        // t549 真附魔：门控（槽 0 itemReady + 档位解锁 + XP / 槽 1 青金石足）
                                        //   → doEnchant（消耗 XP + 槽 1 青金石 → selectEnchants 写入槽 0 物品）。
                                        if (!optSlot.enabled1) return
                                        root.doEnchant(optSlot.idx)
                                    }
                                }
                            }
                        }
                    }
                }

                // 提示文字。
                Text {
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "左槽放工具 / 武器 / 书 · 右槽放青金石（Shift+左键快速放入） · 书架解锁更高档 · 书附魔后成附魔书"
                    color: "#aa9888"; font.pixelSize: 10
                }

                // 「已附魔」绿色 flash 叠层（点击成功后短暂显，~600ms 淡出）。
                Rectangle {
                    anchors.fill: parent
                    color: "#3aa55a"; opacity: root.justEnchanted ? 0.30 : 0.0
                    visible: opacity > 0.001
                    Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                    Text {
                        anchors.centerIn: parent
                        text: "已附魔！"
                        color: "#ffffff"; font.pixelSize: 18; font.bold: true
                        visible: root.justEnchanted
                    }
                }
            }

            // t515 底部 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，五菜单共享）；左键整组 / 右键半份
            // 取放（与 CraftingTableUI / FurnaceUI / ChestUI 主栏同模式）。主栏栈写经 hotbar.mainSetStack；
            // 与 hotbar 共享同一 hotbar VM 光标手持栈。物品可在 主栏 ↔ hotbar 间任意搬动（本面板无 craft /
            // in/fuel 本地槽）。delegate 持 mainId/mainCount 触碰 mainRevision（Q_PROPERTY NOTIFY=mainSlotsChanged）。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    model: root.hotbar.mainCount
                    delegate: Item {
                        property int mainId: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainBlockIdAt(index)) : 0 }
                        property int mainCount: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainCountAt(index)) : 0 }
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent }
                        Item {
                            anchors.centerIn: parent
                            width: 30; height: 30
                            visible: mainId !== 0
                            Image {
                                anchors.fill: parent
                                visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (!root.hotbar.isTool(mainId) && !root.hotbar.isMaterial(mainId)) : false }
                                source: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.iconSourceForBlock(mainId)) : "" }
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            ToolIcon {
                                anchors.fill: parent
                                visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.isTool(mainId)) : false }
                                tier: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.toolTier(mainId)) : 0 }
                                toolType: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.toolType(mainId)) : 0 }
                            }
                            MaterialIcon {
                                anchors.fill: parent
                                visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.isMaterial(mainId)) : false }
                                materialId: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainId) : 0 }
                            }
                        }
                        // 栈数量（count>1 显数字）。触碰 mainRevision 刷新（VM NOTIFY 驱动）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount > 1) : false }
                            text: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount) : "" }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        TapHandler { acceptedButtons: Qt.LeftButton;  onTapped: root.slotLeft("main", index) }
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.slotRight("main", index) }
                        HoverHandler {
                            // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                            // 不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                            property int trackedId: mainId
                            onTrackedIdChanged: {
                                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                    root.hoveredItemId = 0
                            }
                            onHoveredChanged: {
                                // t94 tooltip（mainId 由 delegate 持有；触碰 mainRevision 刷新）。
                                const itemId = mainId
                                if (hovered && itemId !== 0) {
                                    root.hoveredItemId = itemId
                                    const p = parent.mapToItem(root, parent.width / 2, 0)
                                    root.hoveredTipPos = Qt.point(p.x, p.y)
                                } else if (root.hoveredItemId === itemId) {
                                    root.hoveredItemId = 0
                                }
                                const key = root.slotKey("main", index)
                                if (hovered) root.hoveredKey = key
                                else if (root.hoveredKey === key) root.hoveredKey = ""
                                // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                                if (hovered && root.dragActive) {
                                    root.addDragSlot(key)
                                }
                            }
                        }
                        // t167 均分拖拽高亮。
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: "#7fe57f"; border.width: 2
                            visible: {
                                const _ds = root.dragSlots
                                const _rds = root.rightDragSlots
                                const _rev = root.hotbar.mainRevision
                                const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                                const key = root.slotKey("main", index)
                                if (_ok && root.leftDragActive && root.dragHasKey(key)
                                    && (mainId === 0 || mainId === root.dragHeldId)) return true
                                return _ok && root.rightDragActive && root.rightDragHasKey(key)
                            }
                            z: 10
                        }
                    }
                }
            }

            // t515 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            // 属性触碰 slotRevision（t55/t63 已验证写法）。左键整组 / 右键半份同主栏（hotbar 槽写经
            // hotbar.setStack；VM 单一权威）。不切真实选中（同 SurvivalInventory / CraftingTableUI）。
            Item {
                width: root.mainCols * root.slotSize
                height: root.slotSize

                Row {
                    spacing: 0
                    Repeater {
                        model: root.hotbar.slotCount
                        delegate: Item {
                            property int slotId: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.blockIdAt(index)) : 0 }
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent }
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: slotId !== 0
                                Image {
                                    anchors.fill: parent
                                    visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (!root.hotbar.isTool(slotId) && !root.hotbar.isMaterial(slotId)) : false }
                                    source: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.iconSourceForBlock(slotId)) : "" }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.isTool(slotId)) : false }
                                    tier: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.toolTier(slotId)) : 0 }
                                    toolType: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.toolType(slotId)) : 0 }
                                }
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.isMaterial(slotId)) : false }
                                    materialId: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (slotId) : 0 }
                                }
                            }
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.countAt(index) > 1) : false }
                                text: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.countAt(index)) : "" }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            TapHandler { acceptedButtons: Qt.LeftButton;  onTapped: root.slotLeft("hotbar", index) }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.slotRight("hotbar", index) }
                            HoverHandler {
                                // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                                // 不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                                property int trackedId: slotId
                                onTrackedIdChanged: {
                                    if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                        root.hoveredItemId = 0
                                }
                                onHoveredChanged: {
                                    // t94 tooltip（slotId 由 delegate 持有；触碰 slotRevision 刷新）。
                                    if (hovered && slotId !== 0) {
                                        root.hoveredItemId = slotId
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                    } else if (root.hoveredItemId === slotId) {
                                        root.hoveredItemId = 0
                                    }
                                    const key = root.slotKey("hotbar", index)
                                    if (hovered) root.hoveredKey = key
                                    else if (root.hoveredKey === key) root.hoveredKey = ""
                                    // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                                    if (hovered && root.dragActive) {
                                        root.addDragSlot(key)
                                    }
                                }
                            }
                            // t167 均分拖拽高亮。
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: "#7fe57f"; border.width: 2
                                visible: {
                                    const _ds = root.dragSlots
                                    const _rds = root.rightDragSlots
                                    const _rev = root.hotbar.slotRevision
                                    const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                                    const key = root.slotKey("hotbar", index)
                                    if (_ok && root.leftDragActive && root.dragHasKey(key)
                                        && (slotId === 0 || slotId === root.dragHeldId)) return true
                                    return _ok && root.rightDragActive && root.rightDragHasKey(key)
                                }
                                z: 10
                            }
                        }
                    }
                }
            }
        }
    }

    // 「已附魔」flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
    Timer {
        id: enchantFlashTimer
        interval: 600
        onTriggered: root.justEnchanted = false
    }

    // EnchantInputSlot 组件：左区两槽（武器/工具槽 + 青金石槽）。读本地 enchant 数组（enchantRev 驱动刷新）；
    //   左键整组 / 右键半份取放（同主栏 / hotbar，InventoryOps 单一权威）。空槽时显 caption 小字；青金石槽
    //   （showLapisOutline=true）空槽时画青金石轮廓占位（参考 MaterialIcon drawLapis 的青金石八边形形状，
    //   t544 spec「青金石槽空白占位用青金石轮廓图标」；轮廓 = 暗淡描边 + 半透明底，指示该槽接受青金石）。
    component EnchantInputSlot : Item {
        id: eslot
        property string group: "enchant"
        property int index: 0
        property int slotId: 0
        property int slotCount: 0
        property string caption: ""
        property bool showLapisOutline: false

        InvSlot { anchors.fill: parent; wellColor: "#262b30" }
        // 空槽 + 青金石槽 → 青金石轮廓占位（Canvas 自绘八边形描边；参考 MaterialIcon drawLapis 形状）。
        Canvas {
            anchors.centerIn: parent
            width: 30; height: 30
            visible: eslot.slotId === 0 && eslot.showLapisOutline
            onPaint: {
                const ctx = getContext("2d"); ctx.reset()
                ctx.imageSmoothingEnabled = false
                // 青金石八边形轮廓（归一到 24×24 网格，参考 MaterialIcon drawLapis 行 6..17 / 列 5..18）：
                //   顶点按周长序：顶(8,6)→(16,6)→右斜(18,7)→(19,8)→(19,15)→(18,16)→底(16,17)→(8,17)→
                //   (6,16)→(5,15)→(5,8)→(6,7)→闭。整体平移到 Canvas 中心并放大到 30×30。
                const s = 30 / 24.0
                const ox = (30 - 24 * s) / 2   // = 0
                const oxx = 0
                const pts = [[8,6],[16,6],[18,7],[19,8],[19,15],[18,16],[16,17],[8,17],[6,16],[5,15],[5,8],[6,7]]
                ctx.beginPath()
                for (let i = 0; i < pts.length; ++i) {
                    const px = oxx + pts[i][0] * s
                    const py = ox + pts[i][1] * s
                    if (i === 0) ctx.moveTo(px, py)
                    else ctx.lineTo(px, py)
                }
                ctx.closePath()
                // 半透明底（淡蓝，表青金石接受槽）+ 暗淡描边（轮廓）。
                ctx.fillStyle = "rgba(34, 58, 160, 0.18)"
                ctx.fill()
                ctx.strokeStyle = "#4a66b8"
                ctx.lineWidth = 1.2
                ctx.stroke()
                // 顶面高光菱（薄亮线，参考 drawLapis 顶面高光）。
                ctx.strokeStyle = "#6082dc"
                ctx.lineWidth = 1.0
                ctx.beginPath()
                ctx.moveTo(oxx + 8 * s, ox + 6 * s); ctx.lineTo(oxx + 16 * s, ox + 6 * s)
                ctx.stroke()
                // 中心小字「青金」提示（缩到能辨识；槽位 caption 已由调用方省略）。
                ctx.fillStyle = "#7a8cc8"
                ctx.font = "bold 6px sans-serif"
                ctx.textAlign = "center"; ctx.textBaseline = "middle"
                ctx.fillText("青金", 15, 22)
            }
        }
        // 物品图标：方块段→等距立方体 Image；工具段→ToolIcon；材料段→MaterialIcon 自绘。
        Item {
            anchors.centerIn: parent
            width: 30; height: 30
            visible: eslot.slotId !== 0
            Image {
                anchors.fill: parent
                visible: { const _r = root.enchantRev; return _r >= 0 ? (!root.hotbar.isTool(eslot.slotId) && !root.hotbar.isMaterial(eslot.slotId)) : false }
                source: { const _r = root.enchantRev; return _r >= 0 ? (root.hotbar.iconSourceForBlock(eslot.slotId)) : "" }
                fillMode: Image.PreserveAspectFit; smooth: true
            }
            ToolIcon {
                anchors.fill: parent
                visible: { const _r = root.enchantRev; return _r >= 0 ? (root.hotbar.isTool(eslot.slotId)) : false }
                tier: { const _r = root.enchantRev; return _r >= 0 ? (root.hotbar.toolTier(eslot.slotId)) : 0 }
                toolType: { const _r = root.enchantRev; return _r >= 0 ? (root.hotbar.toolType(eslot.slotId)) : 0 }
            }
            MaterialIcon {
                anchors.fill: parent
                visible: { const _r = root.enchantRev; return _r >= 0 ? (root.hotbar.isMaterial(eslot.slotId)) : false }
                materialId: { const _r = root.enchantRev; return _r >= 0 ? (eslot.slotId) : 0 }
            }
            // t549 附魔光晕（槽内物品带附魔时浅紫半透明叠层；机制等价 MC 附魔光泽，同 AnvilUI）。
            Rectangle {
                anchors.fill: parent
                visible: {
                    const _r = root.enchantRev
                    if (_r < 0 || eslot.slotId === 0) return false
                    const e = root.enchAt(eslot.index)
                    return (e[0] || 0) !== 0
                }
                color: Qt.rgba(0.55, 0.25, 0.9, 0.30)
                radius: 3
            }
        }
        // 栈数量（count>1 显数字）。
        Text {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.rightMargin: 3; anchors.bottomMargin: 1
            visible: { const _r = root.enchantRev; return _r >= 0 ? (eslot.slotCount > 1) : false }
            text: { const _r = root.enchantRev; return _r >= 0 ? (eslot.slotCount) : "" }
            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
            font.pixelSize: 13; font.bold: true
        }
        // 槽位小字 caption（武器/工具 或 青金石；空槽时显，帮助辨识两槽布局；青金石槽 caption 空 → 用轮廓替代）。
        Text {
            anchors.centerIn: parent
            text: eslot.slotId === 0 ? eslot.caption : ""
            color: "#6a727a"; font.pixelSize: 9
            visible: text.toString().length > 0 && eslot.slotId === 0 && !eslot.showLapisOutline
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                // t549：Shift+左键 → 面板专属双向语义（可附魔物入槽 0 / 青金石入槽 1 / enchant 槽归包）。
                if (window.shiftHeld) { root.slotShiftLeftEnchant(eslot.group, eslot.index); return }
                // t180：280ms 内同槽二次点击 → doMergeSameId（拿同类；扫 enchant+main+hotbar 同 id）。
                const key = root.slotKey(eslot.group, eslot.index)
                const now = Date.now()
                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                root.lastTapMs = now
                root.lastTapKey = key
                if (isDouble) { InventoryOps.doMergeSameId(root, eslot.group, eslot.index); return }
                // t549：耐久 / 附魔 / t622 名随实例透传（curDur / curEnch / cur.name 取本地槽保真值）。
                const cur = InventoryOps.readSlot(root, eslot.group, eslot.index)
                const r = root.resolveClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                if (!r) return
                InventoryOps.writeSlot(root, eslot.group, eslot.index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
                root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                const cur = InventoryOps.readSlot(root, eslot.group, eslot.index)
                const r = root.resolveRightClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                if (!r) return
                InventoryOps.writeSlot(root, eslot.group, eslot.index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
                root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
            }
        }
        HoverHandler {
            // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged 不重发 →
            // tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
            property int trackedId: eslot.slotId
            onTrackedIdChanged: {
                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                    root.hoveredItemId = 0
            }
            onHoveredChanged: {
                const itemId = eslot.slotId
                if (hovered && itemId !== 0) {
                    root.hoveredItemId = itemId
                    const p = parent.mapToItem(root, parent.width / 2, 0)
                    root.hoveredTipPos = Qt.point(p.x, p.y)
                } else if (root.hoveredItemId === itemId) {
                    root.hoveredItemId = 0
                }
                const key = root.slotKey(eslot.group, eslot.index)
                if (hovered) root.hoveredKey = key
                else if (root.hoveredKey === key) root.hoveredKey = ""
                // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                if (hovered && root.dragActive) {
                    root.addDragSlot(key)
                }
            }
        }
        // t167 均分拖拽高亮。
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "#7fe57f"; border.width: 2
            visible: {
                const _ds = root.dragSlots
                const _rds = root.rightDragSlots
                const _rev = root.enchantRev
                const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                const key = root.slotKey(eslot.group, eslot.index)
                if (_ok && root.leftDragActive && root.dragHasKey(key)
                    && (eslot.slotId === 0 || eslot.slotId === root.dragHeldId)) return true
                return _ok && root.rightDragActive && root.rightDragHasKey(key)
            }
            z: 10
        }
    }

    // t94 物品名悬停 tooltip（纯 QtQuick 自绘；不引入 QtQuick.Controls —— 项目未链接 Qt6::QuickControls2，
    // 顶层 import 新模块有「未部署→整文档加载失败」风险，见 lessons-learned）。各槽 HoverHandler 进入时写
    // hoveredItemId + hoveredTipPos（槽顶中心在 root 坐标系下）；离开按 id 守卫清除（防相邻槽进出竞态互清）。
    // 名字走 hotbar.nameForBlock：方块→BlockRegistry::displayName、工具→ToolRegistry::displayName、
    // 材料段→本地通用名；air/空槽→空串→不显。工具后续将加「+攻击力」等字段，现阶段只名字。
    property int hoveredItemId: 0
    property point hoveredTipPos: Qt.point(0, 0)
    // t263 当前 hover 槽的工具剩余耐久（-1=未跟踪 → tooltip 不显耐久行）。据 hoveredKey 查 hotbar/main。
    property int hoveredDurability: {
        if (!root.hotbar || !root.hoveredItemId || !root.hotbar.isTool(root.hoveredItemId)) return -1
        // qml-touch 三轮：slotRevision/mainRevision 触碰参与返回（_sr>=0 / _mr>=0 恒真守卫），防 AOT 死代码
        //   消除裸触碰 → 同槽栈改写后 tooltip 耐久不刷新。
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const key = root.hoveredKey
        if (!key) return -1
        const parts = key.split(":")
        if (parts.length !== 2) return -1
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return -1
        if (parts[0] === "hotbar") return _sr >= 0 ? (root.hotbar.durabilityAt(idx)) : -1
        if (parts[0] === "main") return _mr >= 0 ? (root.hotbar.mainDurabilityAt(idx)) : -1
        return -1
    }
    // t590 当前 hover 槽物品的附魔列表文本（tooltip 附魔行）：据 hoveredKey 查 hotbar / main / enchant 三组。
    //   hotbar.enchantListText 把 4 槽 packed int 转「锐锋 III\n效率 II」；无附魔 → 空串 → tooltip 不追加行。
    //   触碰各 revision（qml-touch 三轮模式）→ 附魔写入 / 搬运后 tooltip 附魔行刷新（enchantListText 是
    //   Q_INVOKABLE，靠版本号触发重算）。
    property string hoveredEnchantText: {
        if (!root.hotbar || !root.hoveredItemId || !root.hoveredKey) return ""
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const _er = root.enchantRev
        const key = root.hoveredKey
        const parts = key.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        if (parts[0] === "hotbar") return _sr >= 0 ? root.hotbar.enchantListText(root.hotbar.enchantsAt(idx)) : ""
        if (parts[0] === "main") return _mr >= 0 ? root.hotbar.enchantListText(root.hotbar.mainEnchantsAt(idx)) : ""
        if (parts[0] === "enchant") return _er >= 0 ? root.hotbar.enchantListText(root.enchAt(idx)) : ""
        return ""
    }
    Rectangle {
        id: itemTip
        visible: root.hotbar && root.hoveredItemId !== 0 && tipLabel.text !== ""
        z: 1000
        width: tipLabel.implicitWidth + 14
        height: tipLabel.implicitHeight + 8
        color: "#101216"
        opacity: 0.94
        border.color: "#3a444f"
        border.width: 1
        radius: 3
        x: {
            let px = root.hoveredTipPos.x - width / 2
            if (px < 2) px = 2
            const maxX = root.width - width - 2
            if (px > maxX) px = maxX
            return px
        }
        y: {
            let py = root.hoveredTipPos.y - height - 6
            if (py < 2) py = root.hoveredTipPos.y + 6 // 顶部空间不足 → 翻到槽位下方
            return py
        }
        Text {
            id: tipLabel
            anchors.centerIn: parent
            // t263 工具槽 tooltip 附「cur/max」耐久行；非工具 / 未跟踪 → 仅显名。
            // t590 附魔行：物品带附魔 → 换行显附魔列表（如「锐锋 III\n效率 II」），无附魔 → 空串不追加。
            // t622：enchant 槽物品带实例名 → 优先显实例名（nameAt——改名工具放附魔台仍显其名）。
            text: root.hotbar ? ((root.hoveredKey.indexOf("enchant:") === 0 && root.nameAt(parseInt(root.hoveredKey.substring(8), 10)).length > 0
                    ? root.nameAt(parseInt(root.hoveredKey.substring(8), 10))
                    : root.hotbar.nameForBlock(root.hoveredItemId))
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")
                + (root.hoveredEnchantText.length > 0 ? "\n\n" + root.hoveredEnchantText : "")) : ""
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
