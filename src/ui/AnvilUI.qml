import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t516：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   /slotShiftLeft 等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 main/hotbar VM 路由 + anvil 本地槽路由
//   + 薄委托包装（供 QML 信号处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，
//   消除五面板逐字复制（同 CraftingTableUI / FurnaceUI / ChestUI / EnchantingTableUI 模式）。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

// 铁砧 UI（t543 三槽布局重做）：右键铁砧方块打开（PlayerController::anvilOpened → Main.qml Connections →
// 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t543 用户要求「三槽布局 + 深色风格 + 底部无数字」：
//   - **三槽布局（仿 MC 1.0 铁砧）**：左输入槽（武器/工具）+ 右输入槽（附魔书/第二件）→ 中间产物槽
//     （左 ← 产物 → 右，两侧箭头指向中心）。三槽为面板本地 anvil 组（anvilSlots/anvilCounts/anvilRev），
//     可放 / 取背包物品（左键整组 / 右键半份 / 拖动均分 / 双击拿同类 / Shift 搬运，同 CraftingTableUI 全套
//     快捷操作，InventoryOps 单一权威）。
//   - **功能后补**：修复 / 附魔合并 / 重命名功能区保留（沿用 t477 机制，消耗 XP 等级），真修复 / 真合并 /
//     真重命名属后续任务；本任务先界面布局对。按钮 enabled 条件基于左输入槽内容（有物品 + XP 足够），
//     执行 = 消耗 XP + flash + 清空左输入槽（占位交互，机制等价 MC 消耗物品 + 经验，产出待后补）。
//   - **深色风格统一**：面板 #1b1f24（同 CraftingTableUI / FurnaceUI / ChestUI / EnchantingTableUI），
//     槽框 InvSlot 默认，无 t516 的暗橙色调（修用户「颜色暗橙不对」）。
//   - **底部 4 行背包无数字**：3×9 主物品栏 + 9 hotbar 行，hotbar 行**不标数字角标**（同工作台 / 熔炉统一；
//     修用户「底部 hotbar 标了数字」）。
//   - **关包归还**：visible→false 时把 anvil 输入槽内容 addStack 回 hotbar（同 CraftingTableUI returnCraftToHotbar
//     模式；机制等价 MC 关铁砧界面把输入槽物品退回背包）。
//
// 全部 GUI 自绘原创（Rectangle + Text + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点槽 / 输入名），关闭 → grab。
//
// **PERF 护栏（spec「anvil UI recomputes ONLY on input slot change, never per-frame」）**：
//   所有显示绑定到 slotRevision / mainRevision / anvilRev / playerState.levelChanged（低频 NOTIFY：
//   槽写入 / 升级才发，非 60Hz tick）。无 Timer / 无 onFrame / 无 PositionChanged 扫描（仅操作成功 flash
//   用一次性 Timer 600ms 翻 false）。enabled 重算由 NOTIFY 驱动，非每帧。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（heldBlock/heldCount/maxStackSize/iconSourceForBlock/nameForBlock/
    // isTool/isMaterial/slotRevision/mainSetStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 宿主注入：playerState（level / spendLevels）—— 三功能消耗 XP 等级。
    property PlayerState playerState
    // 宿主注入：PlayerController（damageAnvil 推进铁砧损坏）。声明 var 避免类型解析耦合。
    property var player: null
    // 宿主注入：铁砧方块世界坐标（player.damageAnvil 推进损坏）。
    property int anvilX: 0
    property int anvilY: 0
    property int anvilZ: 0
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

    // 三功能 XP 等级消耗（机制等价 MC 铁砧消耗 player level；本工程取固定小量便于测试）。
    readonly property int repairCost: 1   // 修复：1 级
    readonly property int mergeCost: 2    // 附魔合并：2 级
    readonly property int renameCost: 1   // 重命名：1 级

    // ── t543 三槽本地存储：anvil 组（0=左输入 / 1=右输入 / 2=中产物）。与 hotbar VM 共享同一光标手持栈
    //   heldBlock/heldCount；左键整组 / 右键半份同 resolveClick / resolveRightClick（InventoryOps 单一权威）。
    //   中产物槽（index 2）当前为可交互槽（功能后补的产物预览占位）；面板关闭时 returnAnvilToHotbar 把
    //   输入槽内容退回背包（同 CraftingTableUI returnCraftToHotbar 模式）。
    property var anvilSlots:  [0, 0, 0]
    property var anvilCounts: [0, 0, 0]
    property int anvilRev: 0

    // t110：当前指针所在槽的「组:下标」key（供 window.hoveredSlotKey 提升 → 数字键交换 + t167 左键拖动
    //   起点槽）。各槽 HoverHandler onHoveredChanged 维护（进入写、离开按 key 守卫清除，防相邻槽进出竞态
    //   互清）。组名与 readSlot/writeSlot 一致：anvil / main / hotbar。
    property string hoveredKey: ""

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    //   DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（slotLeft 单点拾取/放置/合并/互换 /
    //   Shift 搬运），拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler
    //   在 leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」
    //   字符串（去重简单）；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制
    //   （每滑入新格先撤销上轮写入再重分）。anvil / main / hotbar 参与；t181 右键拖动（每格放 1 个）同源算法。
    property bool leftDragActive: false
    property var dragSlots: []              // "anvil:0" / "main:5" / "hotbar:0"
    property int dragHeldId: 0
    property int dragHeldCount: 0
    property int dragHeldDurability: 0      // t263 拖动期间手持工具耐久快照（松手回填光标保真）
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
    // doMergeSameId（扫 anvil+main+hotbar 同 id 累加成满栈、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t543：anvil 三槽参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明 anvil 为可拖拽本地组 →
    //   InventoryOps.groupIsDraggable 放行（addDragSlot 收集、redistributeLive 分发）、doMergeSameId 扫 anvil 槽。
    property var localDragGroups: ["anvil"]
    // t543：anvil 组槽位数（doMergeSameId 扫描范围）。anvilSlots 长 3（左/右输入 + 中产物）。
    function localSlotCount(group) { return group === "anvil" ? root.anvilSlots.length : 0 }

    // ── t543 面板专属槽路由：anvil 三槽走本地数组 + 版本号（main/hotbar 由 InventoryOps 统一经 VM）。
    //   readSlot/writeSlot 薄包装委托 InventoryOps（含本地组分发 → 调本处 localReadSlot/localWriteSlot）。
    function localReadSlot(group, index) {
        if (group === "anvil") return { id: root.anvilSlots[index] || 0, count: root.anvilCounts[index] || 0 }
        return { id: 0, count: 0 }
    }
    function localWriteSlot(group, index, id, count) {
        if (group === "anvil") { root.anvilSlots[index] = id; root.anvilCounts[index] = count; root.anvilRev++ }
    }
    function resolveClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch) }
    function resolveRightClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count) { InventoryOps.writeSlot(root, group, index, id, count) }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → InventoryOps.slotShiftLeft（Shift+左键搬运 anvil↔main↔hotbar）。
    //   t180：可拖拽组（anvil/main/hotbar）双击 → doMergeSameId（拿同类）。resolveClick/resolveRightClick 算法见
    //   InventoryOps（六面板共享，调用点零改动）。
    function slotLeft(group, index) {
        if (window.shiftHeld) { InventoryOps.slotShiftLeft(root, group, index); return }
        // t180：280ms 内同槽二次点击 + 可拖拽组 → 拿同类（doMergeSameId 扫 anvil+main+hotbar 同 id）。
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
        const r = InventoryOps.resolveClick(root, cur.id, cur.count, cur.durability, cur.enchants)
        if (!r) return
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
        root.hotbar.heldDurability = r.heldDur
        root.hotbar.setHeldEnchants(r.heldEnch)
    }
    function slotRight(group, index) {
        const cur = InventoryOps.readSlot(root, group, index)
        const r = InventoryOps.resolveRightClick(root, cur.id, cur.count, cur.durability, cur.enchants)
        if (!r) return
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
        root.hotbar.heldDurability = r.heldDur
        root.hotbar.setHeldEnchants(r.heldEnch)
    }

    // ── t79/t98/t108/t167 拖动均分 + t110 Shift/数字键搬运 + t98 双击合并：算法见 InventoryOps
    //   （六面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
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
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }
    // redistributeLive / singleLeftClick / slotShiftLeft：纯内部辅助（仅 InventoryOps 内部 / slotLeft 调用），
    //   算法已入 InventoryOps，此处不再持有副本。

    // 关包归还 anvil 输入槽（spec 同 CraftingTableUI returnCraftToHotbar）：visible→false 时把三槽内容
    //   addStack 回 hotbar（MC 行为：关铁砧界面把输入槽物品退回背包）。
    function returnAnvilToHotbar() {
        if (!root.hotbar) return
        for (let i = 0; i < root.anvilSlots.length; ++i) {
            const id = root.anvilSlots[i] || 0
            const n = root.anvilCounts[i] || 0
            if (id !== 0 && n > 0) root.hotbar.addStack(id, n)
        }
        for (let i = 0; i < root.anvilSlots.length; ++i) {
            root.anvilSlots[i] = 0
            root.anvilCounts[i] = 0
        }
        root.anvilRev++
    }
    onVisibleChanged: {
        if (!visible) returnAnvilToHotbar()
        else { renameName = ""; nameInput.text = ""; lastResult = "" }
    }

    // ════════════════════════════════════════════════════════════════════════════
    // ── 三功能功能区（t543 保留；功能后补，占位交互）──
    // 左输入槽内容（index 0）作占位目标：按钮 enabled 条件 = 左槽非空 + XP 足够。真修复 / 真合并 / 真重命名
    //   属后续任务；本任务执行 = 消耗 XP + flash + 清空左槽（占位，机制等价 MC 消耗物品 + 经验）。
    // 触碰 anvilRev（槽写入才发）建立依赖 —— 输入槽内容变时 target / enabled 重算（数组写入不触发绑定，
    //   故用 anvilRev 触碰参与返回，同 CraftingTableUI craftRev 模式）。
    readonly property int leftId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[0] || 0) : 0 }
    readonly property int rightId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[1] || 0) : 0 }
    // 当前 XP 等级（绑定 playerState.level NOTIFY levelChanged；低频）。
    readonly property int playerLevel: playerState ? playerState.level : 0
    // enabled 条件（绑定 anvilRev + playerLevel；低频重算）。
    readonly property bool canRepair: leftId !== 0
    readonly property bool canMerge: rightId !== 0
    readonly property bool canRename: leftId !== 0 && renameName.length > 0
    readonly property bool affordRepair: playerLevel >= repairCost
    readonly property bool affordMerge: playerLevel >= mergeCost
    readonly property bool affordRename: playerLevel >= renameCost
    // 重命名输入文本。
    property string renameName: ""
    // 操作结果 flash（成功后短暂显绿）。
    property string lastResult: ""
    property bool justActed: false
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
    // 手持物时点遮罩区 → 丢弃为实体（同 CraftingTableUI / FurnaceUI / 附魔台 / SurvivalInventory）。t228：
    //   左键整栈 / 右键 1 件 + 面板边界判定（面板内非槽位松手→不丢，修「左键拿物在面板内非槽位松手→直接丢地下」bug）。
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

    // 面板：深色圆角，居中。t543 深色风格统一（#1b1f24，同 CraftingTableUI / FurnaceUI / ChestUI /
    // EnchantingTableUI；修用户「颜色暗橙不对」）。宽度与 CraftingTableUI / 附魔台一致（392）；
    // 高度 = 标题(22) + 三槽功能区(~150) + 主栏(120) + hotbar(40) + 间距/边距。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 430                                  // 22 + 150 + 120 + 40 (=332) + 4×12 spacing(48) + 2×16 margin(32) ≈ 412 → 取 430 留余量
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
                    text: "铁砧"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 三槽功能区（t543 spec：左输入 + 右输入 = 中产物，仿 MC 铁砧三槽布局）──
            // 状态条（XP 等级 + 操作结果）+ 三槽行（左槽 / 右槽 → 中产物槽）+ 三功能按钮。
            Item {
                id: anvilArea
                width: parent.width
                height: 150

                // 状态条：XP 等级 + 操作结果提示。
                Rectangle {
                    id: statusbar
                    anchors.top: parent.top; anchors.topMargin: 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width; height: 28
                    color: "#241a12"; radius: 3
                    Text {
                        anchors.centerIn: parent
                        text: "等级 " + playerLevel + (root.lastResult.length > 0 ? "    " + root.lastResult : "")
                        color: "#ffe6a8"; font.pixelSize: 12
                    }
                }

                // 三槽行：左输入槽 [40] + 箭头 + 中产物槽 [40] + 箭头 + 右输入槽 [40]。
                // 整体水平居中（行宽 40+28+40+28+40 = 176 < 360 → 居中，无大段空白）。
                Item {
                    id: slotRow
                    anchors.top: statusbar.bottom; anchors.topMargin: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: root.slotSize * 3 + 28 * 2   // 120 + 56 = 176
                    height: root.slotSize

                    // 左输入槽（anvil 组 index 0；武器 / 工具）。
                    Item {
                        x: 0; y: 0
                        width: root.slotSize; height: root.slotSize
                        AnvilSlot {
                            anchors.fill: parent
                            group: "anvil"; index: 0
                            // qml-touch：槽内容读数组 + anvilRev 触碰参与返回（数组写入不触发绑定，需 rev 触碰）。
                            slotId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[0] || 0) : 0 }
                            slotCount: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilCounts[0] || 0) : 0 }
                            caption: "左输入"
                        }
                    }
                    // 左→中箭头。
                    Canvas {
                        x: root.slotSize + 2; y: root.slotSize / 2 - 8
                        width: 24; height: 16
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset()
                            ctx.imageSmoothingEnabled = false
                            ctx.fillStyle = "#8a8a8a"
                            ctx.fillRect(0, 6, 16, 4)
                            ctx.beginPath()
                            ctx.moveTo(16, 0); ctx.lineTo(24, 8); ctx.lineTo(16, 16); ctx.closePath()
                            ctx.fill()
                        }
                    }
                    // 中产物槽（anvil 组 index 2；功能后补的产物占位）。
                    Item {
                        x: root.slotSize + 28; y: 0
                        width: root.slotSize; height: root.slotSize
                        AnvilSlot {
                            anchors.fill: parent
                            group: "anvil"; index: 2
                            slotId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[2] || 0) : 0 }
                            slotCount: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilCounts[2] || 0) : 0 }
                            caption: "产物"
                        }
                    }
                    // 右→中箭头（镜像：右槽产物流向中槽）。
                    Canvas {
                        x: root.slotSize * 2 + 28 + 2; y: root.slotSize / 2 - 8
                        width: 24; height: 16
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset()
                            ctx.imageSmoothingEnabled = false
                            ctx.fillStyle = "#8a8a8a"
                            ctx.fillRect(8, 6, 16, 4)
                            ctx.beginPath()
                            ctx.moveTo(8, 0); ctx.lineTo(0, 8); ctx.lineTo(8, 16); ctx.closePath()
                            ctx.fill()
                        }
                    }
                    // 右输入槽（anvil 组 index 1；附魔书 / 第二件）。
                    Item {
                        x: root.slotSize * 2 + 28 * 2; y: 0
                        width: root.slotSize; height: root.slotSize
                        AnvilSlot {
                            anchors.fill: parent
                            group: "anvil"; index: 1
                            slotId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[1] || 0) : 0 }
                            slotCount: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilCounts[1] || 0) : 0 }
                            caption: "右输入"
                        }
                    }
                }

                // 三功能按钮（Column）。
                Column {
                    anchors.top: slotRow.bottom; anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6

                    // 修复。
                    AnvilActionButton {
                        width: panel.width - 16; height: 26
                        title: "修复（左槽工具 · 功能后补）"
                        costText: repairCost + " 级"
                        enabled1: root.canRepair && root.affordRepair
                        reasonText: !root.canRepair ? "需左输入槽放工具" : (!root.affordRepair ? "经验不足" : "")
                        onActivated: root.doRepair()
                    }
                    // 附魔合并。
                    AnvilActionButton {
                        width: panel.width - 16; height: 26
                        title: "附魔合并（右槽附魔书 · 功能后补）"
                        costText: mergeCost + " 级"
                        enabled1: root.canMerge && root.affordMerge
                        reasonText: !root.canMerge ? "需右输入槽放附魔书" : (!root.affordMerge ? "经验不足" : "")
                        onActivated: root.doMerge()
                    }
                    // 重命名行（含 TextInput 特化内联）。
                    Rectangle {
                        width: panel.width - 16; height: 26
                        color: "#2a2018"
                        border.color: (root.canRename && root.affordRename) ? "#ffd87a" : "#0a0604"
                        border.width: (root.canRename && root.affordRename) ? 2 : 1; radius: 3
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            TextInput {
                                id: nameInput
                                anchors.verticalCenter: parent.verticalCenter
                                width: 150
                                color: "#ffe6a8"; font.pixelSize: 11
                                selectByMouse: true
                                maximumLength: 20
                                onTextEdited: root.renameName = text
                                Text {
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                    text: "输入新名…"
                                    color: "#665544"; font.pixelSize: 11
                                    visible: parent.text.length === 0
                                }
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 80; height: 22
                                color: (root.canRename && root.affordRename) ? "#5a4a2a" : "#2a2018"
                                border.color: "#1a120c"; radius: 3
                                Text {
                                    anchors.centerIn: parent
                                    text: "重命名 " + renameCost + "级"
                                    color: (root.canRename && root.affordRename) ? "#ffe6a8" : "#665544"
                                    font.pixelSize: 10; font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                TapHandler {
                                    enabled: root.canRename && root.affordRename
                                    onTapped: root.doRename()
                                }
                            }
                        }
                    }
                }

                // 「操作成功」绿色 flash 叠层（点击成功后短暂显，~600ms 淡出）。
                Rectangle {
                    anchors.fill: parent
                    color: "#3aa55a"; opacity: root.justActed ? 0.30 : 0.0
                    visible: opacity > 0.001
                    Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                }
            }

            // ── t543 底部 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，六菜单共享）；左键整组 / 右键半份
            //   取放（与 CraftingTableUI / FurnaceUI / ChestUI / 附魔台主栏同模式）。主栏栈写经 hotbar.mainSetStack；
            //   与 anvil 三槽 / hotbar 共享同一 hotbar VM 光标手持栈。delegate 持 mainId/mainCount 触碰 mainRevision。
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

            // ── t543 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            //   属性触碰 slotRevision（t55/t63 已验证写法）。左键整组 / 右键半份同主栏（hotbar 槽写经
            //   hotbar.setStack；VM 单一权威）。**无数字角标**（同工作台 / 熔炉统一；修用户「底部 hotbar 标了数字」）。
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

        // 「操作成功」绿色 flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
        // 放 panel 内与叠层同域（便于 opacity Behavior 绑 root.justActed）。
    }

    // 操作成功 flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
    Timer {
        id: actFlashTimer
        interval: 600
        onTriggered: root.justActed = false
    }

    // ── 三功能执行（占位交互，功能后补）：消耗 XP + 清空对应输入槽 + flash + 推进铁砧损坏。──
    //   真修复 / 真合并 / 真重命名逻辑（把附魔 / 耐久 / 自定义名应用到产物槽）属后续任务。
    function doRepair() {
        if (!root.canRepair || !root.affordRepair) return
        if (!root.playerState.spendLevels(repairCost)) return
        root.anvilSlots[0] = 0; root.anvilCounts[0] = 0; root.anvilRev++
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已消耗物品（修复功能后补）"
        root.justActed = true
        actFlashTimer.restart()
    }
    function doMerge() {
        if (!root.canMerge || !root.affordMerge) return
        if (!root.playerState.spendLevels(mergeCost)) return
        root.anvilSlots[1] = 0; root.anvilCounts[1] = 0; root.anvilRev++
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已消耗附魔书（合并功能后补）"
        root.justActed = true
        actFlashTimer.restart()
    }
    function doRename() {
        if (!root.canRename || !root.affordRename) return
        if (!root.playerState.spendLevels(renameCost)) return
        root.anvilSlots[0] = 0; root.anvilCounts[0] = 0; root.anvilRev++
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已重命名（功能后补）"
        root.renameName = ""           // 清输入
        nameInput.text = ""
        root.justActed = true
        actFlashTimer.restart()
    }

    // AnvilActionButton 组件（修复 / 合并用；重命名行因含 TextInput 特化内联）。
    component AnvilActionButton : Rectangle {
        id: btn
        property string title: ""
        property string costText: ""
        property bool enabled1: false
        property string reasonText: ""
        signal activated()
        color: enabled1 ? "#2a2018" : "#1a140e"
        border.color: enabled1 ? "#ffd87a" : "#0a0604"
        border.width: enabled1 ? 2 : 1
        radius: 3
        Row {
            anchors.centerIn: parent
            spacing: 10
            Text {
                text: btn.title
                color: btn.enabled1 ? "#ffe6a8" : "#665544"
                font.pixelSize: 11; font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: btn.costText
                color: btn.enabled1 ? "#a8d8ff" : "#554433"
                font.pixelSize: 10
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: btn.reasonText
                color: "#886655"
                font.pixelSize: 9
                anchors.verticalCenter: parent.verticalCenter
                visible: btn.reasonText.length > 0
            }
        }
        TapHandler {
            enabled: btn.enabled1
            onTapped: btn.activated()
        }
    }

    // AnvilSlot 组件：三槽布局的单槽（左输入 / 右输入 / 中产物）。读本地 anvil 数组（anvilRev 驱动刷新）；
    //   左键整组 / 右键半份取放（同主栏 / hotbar，InventoryOps 单一权威）；槽底小字 caption（"左输入" 等）。
    component AnvilSlot : Item {
        id: aslot
        property string group: "anvil"
        property int index: 0
        property int slotId: 0
        property int slotCount: 0
        property string caption: ""

        InvSlot { anchors.fill: parent; wellColor: "#262b30" }
        // 物品图标：方块段→等距立方体 Image；工具段→ToolIcon；材料段→MaterialIcon 自绘。
        Item {
            anchors.centerIn: parent
            width: 30; height: 30
            visible: aslot.slotId !== 0
            Image {
                anchors.fill: parent
                visible: { const _r = root.anvilRev; return _r >= 0 ? (!root.hotbar.isTool(aslot.slotId) && !root.hotbar.isMaterial(aslot.slotId)) : false }
                source: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.iconSourceForBlock(aslot.slotId)) : "" }
                fillMode: Image.PreserveAspectFit; smooth: true
            }
            ToolIcon {
                anchors.fill: parent
                visible: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.isTool(aslot.slotId)) : false }
                tier: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.toolTier(aslot.slotId)) : 0 }
                toolType: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.toolType(aslot.slotId)) : 0 }
            }
            MaterialIcon {
                anchors.fill: parent
                visible: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.isMaterial(aslot.slotId)) : false }
                materialId: { const _r = root.anvilRev; return _r >= 0 ? (aslot.slotId) : 0 }
            }
        }
        // 栈数量（count>1 显数字）。
        Text {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.rightMargin: 3; anchors.bottomMargin: 1
            visible: { const _r = root.anvilRev; return _r >= 0 ? (aslot.slotCount > 1) : false }
            text: { const _r = root.anvilRev; return _r >= 0 ? (aslot.slotCount) : "" }
            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
            font.pixelSize: 13; font.bold: true
        }
        // 槽位小字 caption（左输入 / 右输入 / 产物；空槽时显，帮助辨识三槽布局）。
        Text {
            anchors.centerIn: parent
            text: aslot.slotId === 0 ? aslot.caption : ""
            color: "#6a727a"; font.pixelSize: 9
            visible: text.toString().length > 0
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                if (window.shiftHeld) { InventoryOps.slotShiftLeft(root, aslot.group, aslot.index); return }
                // t180：280ms 内同槽二次点击 → doMergeSameId（拿同类；扫 anvil+main+hotbar 同 id）。
                const key = root.slotKey(aslot.group, aslot.index)
                const now = Date.now()
                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                root.lastTapMs = now
                root.lastTapKey = key
                if (isDouble) { root.doMergeSameId(aslot.group, aslot.index); return }
                const r = root.resolveClick(aslot.slotId, aslot.slotCount, 0)
                if (!r) return
                root.anvilSlots[aslot.index] = r.slotId
                root.anvilCounts[aslot.index] = r.slotCount
                root.anvilRev++
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                const r = root.resolveRightClick(aslot.slotId, aslot.slotCount, 0)
                if (!r) return
                root.anvilSlots[aslot.index] = r.slotId
                root.anvilCounts[aslot.index] = r.slotCount
                root.anvilRev++
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
            }
        }
        HoverHandler {
            // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged 不重发 →
            // tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
            property int trackedId: aslot.slotId
            onTrackedIdChanged: {
                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                    root.hoveredItemId = 0
            }
            onHoveredChanged: {
                const itemId = aslot.slotId
                if (hovered && itemId !== 0) {
                    root.hoveredItemId = itemId
                    const p = parent.mapToItem(root, parent.width / 2, 0)
                    root.hoveredTipPos = Qt.point(p.x, p.y)
                } else if (root.hoveredItemId === itemId) {
                    root.hoveredItemId = 0
                }
                const key = root.slotKey(aslot.group, aslot.index)
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
                const _rev = root.anvilRev
                const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                const key = root.slotKey(aslot.group, aslot.index)
                if (_ok && root.leftDragActive && root.dragHasKey(key)
                    && (aslot.slotId === 0 || aslot.slotId === root.dragHeldId)) return true
                return _ok && root.rightDragActive && root.rightDragHasKey(key)
            }
            z: 10
        }
    }

    // t94 物品名悬停 tooltip（纯 QtQuick 自绘；不引入 QtQuick.Controls —— 项目未链接 Qt6::QuickControls2，
    // 顶层 import 新模块有「未部署→整文档加载失败」风险，见 lessons-learned）。各槽 HoverHandler 进入时写
    // hoveredItemId + hoveredTipPos（槽顶中心在 root 坐标系下）；离开按 id 守卫清除（防相邻槽进出竞态互清）。
    // 名字走 hotbar.nameForBlock：方块→BlockRegistry::displayName、工具→ToolRegistry::displayName、
    // 材料段→本地通用名；air/空槽→空串→不显。
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
            text: root.hotbar ? (root.hotbar.nameForBlock(root.hoveredItemId)
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")) : ""
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
