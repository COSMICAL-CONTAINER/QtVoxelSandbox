import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t516：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   /slotShiftLeft 等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 main/hotbar VM 路由 + slotLeft/slotRight
//   面板 dispatch + 薄委托包装（供 QML 信号处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，
//   消除五面板逐字复制（同 CraftingTableUI / FurnaceUI / ChestUI / EnchantingTableUI 模式）。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

// 铁砧 UI（t516 工作台蓝本重做）：右键铁砧方块打开（PlayerController::anvilOpened → Main.qml Connections →
// 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t516 用户要求「以工作台为蓝本重做」：现版（t477）是 shell-mode 选中槽消耗 UI（无背包），右键能开但用户要
//   工作台式「上方功能区 + 底部背包 4 行」布局。本任务把布局换成 CraftingTableUI 蓝本：
//   - 上区「修复 / 附魔合并 / 重命名功能区」（沿用 t477 机制：以**当前选中 hotbar 槽**为目标，三功能各消耗
//     XP 等级；机制等价 MC 1.0 铁砧 repair / enchant-merge / rename，目标 = 选中槽物品，简化掉显式左右槽 +
//     InventoryOps 光标系统）。每次成功操作调 player.damageAnvil(anvilX/Y/Z) 推进铁砧损坏（~1/3 概率 +1
//     阶段；重损再损碎裂移除）。
//   - 下区「3×9 主物品栏 + 9 hotbar 行」= 底部背包 4 行（与工作台 / 熔炉 / 箱子 / 附魔台同布局），玩家可在此
//     放 / 取背包物品（左键整组 / 右键半份 / 拖动均分 / Shift 搬运 / 双击拿同类，同 CraftingTableUI 全套快捷操作）。
//     底部 hotbar 行同步游戏内 hotbar 且**可点击切选中槽**（切目标 → 上方功能区重算 canRepair/Merge）。
//
// 全部 GUI 自绘原创（Rectangle + Text + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点槽 / 输入名），关闭 → grab。
//
// **PERF 护栏（spec「anvil UI recomputes ONLY on input slot change, never per-frame」）**：
//   所有显示绑定到 hotbar.slotRevision / selectedSlotChanged / mainRevision / playerState.levelChanged /
//   hotbar.anvilCanRepair* NOTIFY（低频：槽写入 / 选槽 / 升级才发，非 60Hz tick）。enabled 条件同 low-freq NOTIFY。
//   无 Timer / 无 onFrame / 无 PositionChanged 扫描（仅操作成功 flash 用一次性 Timer 600ms 翻 false）。
//   target / enabled 重算由 NOTIFY 驱动，非每帧。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（heldBlock/heldCount/maxStackSize/iconSourceForBlock/nameForBlock/
    // isTool/isMaterial/slotRevision/mainSetStack 等栈操作 + 图标 / 名查询 + anvil 三功能作用于选中槽）。
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

    // ── t516 底部背包：27 主物品栏自 hotbar VM（m_mainSlots），与 SurvivalInventory / CraftingTableUI /
    //   FurnaceUI / ChestUI / EnchantingTableUI 六菜单共享同一份 → 六菜单主栏同步、returnHeldToHotbar/pickupScan
    //   经 addToAny 能合并进主栏。与 hotbar 共享同一 hotbar VM 光标手持栈；左键整组 / 右键半份同 resolveClick /
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
    //   （每格放 1 个）同源算法；dragActive 统一左/右拖动收集门控。均分算法与工作台 / 熔炉 / 箱子 / 附魔台同源。
    property bool leftDragActive: false
    property var dragSlots: []              // "main:5" / "hotbar:0"
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
    // doMergeSameId（扫 main+hotbar 同 id 累加成满栈、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t516 本面板无本地容器槽（main/hotbar 全经 VM）→ localDragGroups 为空（无 craft / in/fuel/out），
    //   InventoryOps.groupIsDraggable 对未声明组拒收：拖拽 / 双击合并只走 main+hotbar（同 SurvivalInventory / 附魔台）。
    property var localDragGroups: []
    function localSlotCount(group) { return 0 }  // 无本地组槽位（doMergeSameId 扫描范围仅 main+hotbar）

    // ── t516 面板专属槽路由：无本地组（main/hotbar 由 InventoryOps 统一经 VM）。readSlot/writeSlot 薄包装
    //   委托 InventoryOps（无本地组分发 → localReadSlot/localWriteSlot 返空）。slotLeft/slotRight 统一槽点击
    //   dispatch（左键整组 / 右键半份 + Shift 搬运 + 双击拿同类），算法见 InventoryOps（六面板共享）。
    function localReadSlot(group, index) { return { id: 0, count: 0 } }
    function localWriteSlot(group, index, id, count) { /* 无本地组 */ }
    function resolveClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch) }
    function resolveRightClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count) { InventoryOps.writeSlot(root, group, index, id, count) }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → InventoryOps.slotShiftLeft（Shift+左键搬运 main↔hotbar）。
    //   t180：可拖拽组（main/hotbar）双击 → doMergeSameId（拿同类）。resolveClick/resolveRightClick 算法见
    //   InventoryOps（六面板共享，调用点零改动）。
    function slotLeft(group, index) {
        if (window.shiftHeld) { InventoryOps.slotShiftLeft(root, group, index); return }
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
    // redistributeLive / singleLeftClick / slotShiftLeft：纯内部辅助（仅 InventoryOps 内部 / slotLeft 调用），
    //   算法已入 InventoryOps，此处不再持有副本。

    // ════════════════════════════════════════════════════════════════════════════
    // ── 修复 / 附魔合并 / 重命名功能区（t477 沿用；以选中 hotbar 槽为目标）──
    // 选中槽 = hotbar.selectedSlot（绑定 NOTIFY selectedSlotChanged；低频）。三功能 C++ 单一权威算 + 写
    //   （hotbar.anvilCanRepairSelected / anvilDoRepairSelected / anvilCanMergeEnchantsSelected /
    //   anvilDoMergeEnchantsSelected / anvilDoRenameSelected），UI 仅调 + 据 bool 决定提示。机制等价 MC 1.0
    //   铁砧，目标 = 选中槽物品（简化掉显式左右槽 + 光标系统）。
    // 当前选中槽（绑定 hotbar.selectedSlot NOTIFY selectedSlotChanged；低频）。
    readonly property int selSlot: hotbar ? hotbar.selectedSlot : 0
    // 触碰 slotRevision（低频：槽写入才发）建立依赖 —— 选中槽内容变时 target / enabled 重算。
    readonly property int slotRev: hotbar ? hotbar.slotRevision : 0
    // 选中槽物品数据（绑定 selSlot + slotRev；低频重算）。
    readonly property int targetId: hotbar ? hotbar.blockIdAt(selSlot) : 0
    readonly property int targetCount: hotbar ? hotbar.countAt(selSlot) : 0
    readonly property int targetDur: hotbar ? hotbar.durabilityAt(selSlot) : 0
    readonly property int targetMaxDur: hotbar ? hotbar.toolMaxDurability(targetId) : 0
    readonly property string targetIcon: hotbar ? hotbar.iconSourceForBlock(targetId) : ""
    readonly property string targetName: hotbar ? hotbar.nameForBlock(targetId) : ""
    readonly property var targetEnchants: hotbar ? hotbar.enchantsAt(selSlot) : [0,0,0,0]
    // 当前 XP 等级（绑定 playerState.level NOTIFY levelChanged；低频）。
    readonly property int playerLevel: playerState ? playerState.level : 0
    // enabled 条件（绑定 slotRev + playerLevel；低频重算）。
    readonly property bool canRepair: hotbar && hotbar.anvilCanRepairSelected()
    readonly property bool canMerge: hotbar && hotbar.anvilCanMergeEnchantsSelected()
    readonly property bool canRename: targetId !== 0 && renameName.length > 0
    readonly property bool affordRepair: playerLevel >= repairCost
    readonly property bool affordMerge: playerLevel >= mergeCost
    readonly property bool affordRename: playerLevel >= renameCost
    // 重命名输入文本。
    property string renameName: ""
    // 操作结果 flash（成功后短暂显绿）。
    property string lastResult: ""
    property bool justActed: false
    // PERF 护栏：面板从隐藏切到显示时清状态（重命名输入 + 结果提示）。
    onVisibleChanged: {
        if (visible) { renameName = ""; nameInput.text = ""; lastResult = "" }
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

    // 面板：深色圆角，居中。宽度与 CraftingTableUI / 附魔台一致（392）便于复用主栏布局；
    // 高度 = 标题(22) + 修复/合并/重命名功能区(~210) + 主栏(120) + hotbar(40) + 间距/边距。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 490                                  // 22 + 210 + 120 + 40 (=392) + 3×12 spacing(36) + 2×16 margin(32) ≈ 460 → 取 490 留余量
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

            // ── 修复 / 附魔合并 / 重命名功能区（t516 spec：上方功能区，沿用 t477 机制）──
            // 布局：状态条（XP 等级 + 操作结果）+ 目标物品展示（选中槽图标 + 名 + 耐久 + 附魔）+
            //   三功能按钮（修复 / 附魔合并 / 重命名行）。
            Item {
                id: anvilArea
                width: parent.width
                height: 210

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

                // 目标物品展示（选中槽）。
                Rectangle {
                    id: targetBox
                    anchors.top: statusbar.bottom; anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width; height: 64
                    color: "#2a2018"; border.color: "#5a4a2a"; border.width: 1; radius: 4

                    // 目标槽方块（图标 + 数量）。
                    Rectangle {
                        id: targetSlot
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: root.slotSize; height: root.slotSize
                        color: "#1a120c"; border.color: "#ffd87a"; border.width: 2; radius: 3
                        Image {
                            anchors.centerIn: parent
                            width: root.slotSize - 8; height: root.slotSize - 8
                            // qml url-guard：source 判空必用 source.toString().length > 0（.length 恒 undefined）。
                            source: targetIcon.toString().length > 0 ? targetIcon : ""
                            visible: source.toString().length > 0
                            fillMode: Image.Pad
                        }
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.margins: 2
                            text: targetCount > 1 ? targetCount : ""
                            color: "#ffffff"; font.pixelSize: 12; font.bold: true
                            visible: targetCount > 1
                        }
                    }

                    // 目标名 + 耐久 + 附魔。
                    Column {
                        anchors.left: targetSlot.right; anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: targetId === 0 ? "（空槽 — 按 1-9 或点底部槽选目标）" : targetName
                            color: targetId === 0 ? "#665544" : "#ffe6a8"
                            font.pixelSize: 13; font.bold: true
                        }
                        // 耐久条（仅工具 / 护甲显）。
                        Rectangle {
                            visible: targetMaxDur > 0
                            width: 220; height: 10
                            color: "#0a0604"; border.color: "#3a2a1a"; border.width: 1; radius: 2
                            Rectangle {
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                anchors.margins: 1
                                width: parent.width > 2 ? Math.max(0, (parent.width - 2) * Math.max(0, Math.min(1, targetDur / Math.max(1, targetMaxDur)))) : 0
                                height: parent.height - 2
                                color: targetDur / Math.max(1, targetMaxDur) > 0.5 ? "#4aa55a" : (targetDur / Math.max(1, targetMaxDur) > 0.25 ? "#d8a84a" : "#c84a4a")
                                radius: 1
                            }
                            Text {
                                anchors.centerIn: parent
                                text: targetDur + " / " + targetMaxDur
                                color: "#ffffff"; font.pixelSize: 8
                            }
                        }
                        // 附魔列表（显非空附魔名 + 等级）。
                        Text {
                            text: root.formatEnchants(targetEnchants)
                            color: "#a8d8ff"; font.pixelSize: 10
                            visible: text.toString().length > 0
                        }
                    }
                }

                // 三功能按钮（Column）。
                Column {
                    id: actions
                    anchors.top: targetBox.bottom; anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6

                    // 修复。
                    AnvilActionButton {
                        width: panel.width - 16; height: 30
                        title: "修复（合并同物品耐久）"
                        costText: repairCost + " 级"
                        enabled1: root.canRepair && root.affordRepair
                        reasonText: !root.canRepair ? "需选中工具/护甲 + 背包有同款第二件" : (!root.affordRepair ? "经验不足" : "")
                        onActivated: root.doRepair()
                    }
                    // 附魔合并。
                    AnvilActionButton {
                        width: panel.width - 16; height: 30
                        title: "附魔合并（合并同物品附魔）"
                        costText: mergeCost + " 级"
                        enabled1: root.canMerge && root.affordMerge
                        reasonText: !root.canMerge ? "需选中可附魔物 + 背包有带附魔同款第二件" : (!root.affordMerge ? "经验不足" : "")
                        onActivated: root.doMerge()
                    }
                    // 重命名行（含 TextInput 特化内联）。
                    Rectangle {
                        width: panel.width - 16; height: 30
                        color: "#2a2018"
                        border.color: (root.canRename && root.affordRename) ? "#ffd87a" : "#0a0604"
                        border.width: (root.canRename && root.affordRename) ? 2 : 1; radius: 3
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            TextInput {
                                id: nameInput
                                anchors.verticalCenter: parent.verticalCenter
                                width: 180
                                color: "#ffe6a8"; font.pixelSize: 12
                                selectByMouse: true
                                maximumLength: 20
                                onTextEdited: root.renameName = text
                                Text {
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                    text: "输入新名…"
                                    color: "#665544"; font.pixelSize: 12
                                    visible: parent.text.length === 0
                                }
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 80; height: 24
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

            // t516 底部 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，六菜单共享）；左键整组 / 右键半份
            //   取放（与 CraftingTableUI / FurnaceUI / ChestUI / 附魔台主栏同模式）。主栏栈写经 hotbar.mainSetStack；
            //   与 hotbar 共享同一 hotbar VM 光标手持栈。物品可在 主栏 ↔ hotbar 间任意搬动（本面板无 craft /
            //   in/fuel 本地槽）。delegate 持 mainId/mainCount 触碰 mainRevision（Q_PROPERTY NOTIFY=mainSlotsChanged）。
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

            // t516 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            //   属性触碰 slotRevision（t55/t63 已验证写法）。左键整组 / 右键半份同主栏（hotbar 槽写经
            //   hotbar.setStack；VM 单一权威）。**额外**：hotbar 槽被点（左 / 右 / 切目标）后更新选中槽
            //   （setSelectedSlot），让上方功能区重算 canRepair/Merge —— 但点槽本身就是 InventoryOps 的取放语义，
            //   切目标由下方显式「点槽号角标」TapHandler 负责（避免与取放冲突）。
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
                            // 选中槽高亮（金边 + 深底；非选中走 InvSlot 默认凹陷）。
                            Rectangle {
                                anchors.fill: parent
                                color: index === root.selSlot ? "#3a2e1a" : "#161a1f"
                                border.color: index === root.selSlot ? "#ffd87a" : "#2a3038"
                                border.width: index === root.selSlot ? 2 : 1
                                radius: 3
                            }
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
                            // 槽号角标（左上小数字；点它切选中槽 = 切铁砧目标，不取放物品）。
                            Text {
                                anchors.top: parent.top; anchors.left: parent.left
                                anchors.margins: 2
                                text: (index + 1)
                                color: index === root.selSlot ? "#ffd87a" : "#6a727a"
                                font.pixelSize: 9; font.bold: index === root.selSlot
                            }
                            Rectangle {
                                anchors.top: parent.top; anchors.left: parent.left
                                width: 14; height: 12
                                color: "transparent"
                                TapHandler { onTapped: root.hotbar.setSelectedSlot(index) }
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

    // ── 三功能执行（消耗 XP + 调 hotbar + 推进铁砧损坏）──
    function doRepair() {
        if (!root.canRepair || !root.affordRepair) return
        if (!root.playerState.spendLevels(repairCost)) return
        if (!root.hotbar.anvilDoRepairSelected()) return
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已修复"
        root.justActed = true
        actFlashTimer.restart()
    }
    function doMerge() {
        if (!root.canMerge || !root.affordMerge) return
        if (!root.playerState.spendLevels(mergeCost)) return
        if (!root.hotbar.anvilDoMergeEnchantsSelected()) return
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已合并附魔"
        root.justActed = true
        actFlashTimer.restart()
    }
    function doRename() {
        if (!root.canRename || !root.affordRename) return
        if (!root.playerState.spendLevels(renameCost)) return
        if (!root.hotbar.anvilDoRenameSelected(renameName)) return
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已重命名"
        root.renameName = ""           // 清输入
        nameInput.text = ""
        root.justActed = true
        actFlashTimer.restart()
    }

    // 4 槽 packed 附魔 → 显示串（如 "锐锋 III / 耐久 I"）。空 → 空串。
    function formatEnchants(enchants) {
        if (!enchants || enchants.length === 0) return ""
        var parts = []
        for (var i = 0; i < enchants.length; ++i) {
            var packed = enchants[i]
            if (!packed) continue
            var eid = (packed >> 8) & 0xff
            var lvl = packed & 0xff
            var name = enchantDisplayName(eid)
            if (name.length > 0) parts.push(name + " " + romanSuffix(lvl))
        }
        return parts.join(" / ")
    }
    // 附魔 id → 中文名（hotbar 未暴露逐 id 查名，故本地内联极简映射 —— 仅本 UI 显示用，避免新增 Q_INVOKABLE。
    //   §9 通用词非专名）。
    function enchantDisplayName(eid) {
        var names = {
            1: "锐锋", 2: "亡灵杀手", 3: "节肢克星", 4: "击退", 5: "燃焰",
            6: "效率", 7: "精准采集", 8: "时运", 9: "耐久",
            10: "保护", 11: "火焰保护", 12: "摔落保护", 13: "弹射物保护", 14: "水上亲和"
        }
        return names[eid] || ""
    }
    function romanSuffix(n) {
        if (n <= 0) return ""
        if (n <= 5) return ["I","II","III","IV","V"][n-1]
        return "" + n
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
                font.pixelSize: 12; font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: btn.costText
                color: btn.enabled1 ? "#a8d8ff" : "#554433"
                font.pixelSize: 11
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
