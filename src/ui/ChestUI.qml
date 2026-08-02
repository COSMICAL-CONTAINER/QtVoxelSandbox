import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` /
//   `property ChestStore chestStore` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 chest 本地槽路由 + 薄委托包装。算法单一权威
//   收敛于此库，消除四面板逐字复制（箱子与工作台 / 熔炉同模式接入）。
import "InventoryOps.js" as InventoryOps

// 箱子物品栏面板（t173 / t179 修复右键无效）：右键箱子方块打开（PlayerController::chestOpened(x,y,z) →
// Main.qml Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// 布局贴近 MC 1.0 箱子：上部「箱子自身 27 槽（3×9）」+ 下部「玩家 3×9=27 主物品栏 + 9 hotbar 行」。
// 箱子 27 槽内容存 ChestStore（Game 层 VM，按方块世界坐标键控 → 跨 UI 开关 / 跨面板持久；多只箱子各自
// 独立 27 槽）。主栏 / hotbar 共享 hotbar VM（与 SurvivalInventory / CraftingTableUI / FurnaceUI 三菜单
// 同一份 → 主栏同步）。
//
// 物品移动（箱子 / 主栏 / hotbar 间任意搬动）：左键整组 / 右键半份 / 单放 / 左键拖动均分，与其它面板的
// resolveClick / resolveRightClick / redistributeLive 同算法（共享 hotbar VM 的 heldBlock / heldCount
// 光标手持栈）。本面板把 "chest" 组经 localReadSlot/localWriteSlot 钩子路由到 ChestStore（按坐标寻址）。
//
// 关包（visible→false）：returnHeldToHotbar 由宿主调（同工作台 / 熔炉）；箱子内容持久存于 ChestStore
// （非面板本地态），关包不退回（机制等价 MC 箱子内容随方块存）。破箱时 Main.qml.onBlockBroken(Chest)
// 调 chestStore.clearChest 清孤儿条目（内容不退回玩家，spec「破箱掉落内容」属 Phase 1.1+）。
//
// 全部槽框自绘原创（InvSlot 凹陷槽，无外部 MC GUI PNG；§9 override (a)）。零 MC 专有名词（§9）。
// 宿主负责指针态：打开时 release（光标可见点格子），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（玩家随身背包；提供 heldBlock/heldCount/maxStackSize/iconSourceForBlock/
    // nameForBlock/isTool/isMaterial/slotRevision/main*/addStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 宿主注入：ChestStore 视图模型（按方块坐标键控的 27 槽箱子内容；slotIdAt/slotCountAt/setSlot/
    // clearChest/revision）。本面板据此读写「这只箱子」的内容。
    property ChestStore chestStore
    // 宿主注入：当前所开箱子的方块世界坐标（player.chestOpened 携带 → Main.qml.openChest 存此 →
    // 透传本面板）。ChestStore 据此坐标寻址该箱子的 27 槽；切箱子（关再开另一只）时坐标变 → 本面板
    // 经 chestCoordRev 触发 delegate 重读新箱子内容。
    property int chestX
    property int chestY
    property int chestZ
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49 同 SurvivalInventory / CraftingTableUI：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 /
    // 点遮罩区）。
    signal discardHeldRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int mainCols: 9
    readonly property int mainRows: 3
    // 箱子自身 3×9=27 槽（MC 1.0 标准；ChestStore::kSlotsPerChest 同值）。
    readonly property int chestCols: 9
    readonly property int chestRows: 3

    // t167 左键拖动均分（同 CraftingTableUI：root 级 DragHandler(LeftButton) 总控，逐槽 HoverHandler
    //   在 leftDragActive 期间收集扫过格 → redistributeLive 实时重分）。chest / main / hotbar 统一支持。
    property bool leftDragActive: false
    property var dragSlots: []              // "chest:2" / "main:5" / "hotbar:0"
    property string hoveredKey: ""
    property int dragHeldId: 0
    property int dragHeldCount: 0
    // t98 实时重分撤销机制（同 CraftingTableUI）。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    // t98 双击合并：280ms 内同槽二次点击 → doMergeSameId（扫 main+hotbar+chest 同 id 累加成满栈、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t180：chest 参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明 chest 为可拖拽本地组——
    //   t168 抽 InventoryOps 后 drag 路径已让 chest 分发（旧 redistributeLive 仅排除 craft），t180 把「是否参与」
    //   泛化为 localDragGroups 声明，此处显式声明以维持 chest 拖拽分发（不声明会被 groupIsDraggable 拒收、
    //   回退为不可拖拽——回归）。双击合并也随之扫 chest 槽（修旧版「点 chest 槽常 total=0 空操作」：旧版
    //   doMergeSameId 只扫 main/hotbar，chest 物品不并入）。
    property var localDragGroups: ["chest"]
    // t180：chest 组槽位数（doMergeSameId 扫描范围）= chestRows*chestCols（3×9=27，ChestStore::kSlotsPerChest）。
    function localSlotCount(group) { return group === "chest" ? root.chestRows * root.chestCols : 0 }

    // ── t168 面板专属槽路由：chest 组走 ChestStore（按坐标寻址）；main/hotbar 由 InventoryOps 统一经 VM。
    //   readSlot/writeSlot 薄包装委托 InventoryOps（含本地组分发 → 调本处 localReadSlot/localWriteSlot）。
    //   chestCoordRev 触碰 chestX/Y/Z（切箱子时坐标变 → delegate 经此重读新箱子内容）。
    function localReadSlot(group, index) {
        if (group === "chest") {
            root.chestCoordRev
            return {
                id: root.chestStore.slotIdAt(root.chestX, root.chestY, root.chestZ, index),
                count: root.chestStore.slotCountAt(root.chestX, root.chestY, root.chestZ, index)
            }
        }
        return { id: 0, count: 0 }
    }
    function localWriteSlot(group, index, id, count) {
        if (group === "chest") {
            root.chestStore.setSlot(root.chestX, root.chestY, root.chestZ, index, id, count)
        }
    }
    // resolveClick / resolveRightClick（拾取/放置/合并/互换 + 半份）：算法见 InventoryOps（五面板共享）。
    function resolveClick(curId, curCount) { return InventoryOps.resolveClick(root, curId, curCount) }
    function resolveRightClick(curId, curCount) { return InventoryOps.resolveRightClick(root, curId, curCount) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count) { InventoryOps.writeSlot(root, group, index, id, count) }

    // ── t79/t98/t108/t167 拖动均分 + t110 Shift/数字键搬运 + t98 双击合并：算法见 InventoryOps
    //   （五面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
    function slotKey(group, index) { return InventoryOps.slotKey(group, index) }
    function dragHasKey(key) { return InventoryOps.dragHasKey(root, key) }
    function addDragSlot(key) { InventoryOps.addDragSlot(root, key) }
    function beginLeftDrag() { InventoryOps.beginLeftDrag(root) }
    function endLeftDrag() { InventoryOps.endLeftDrag(root) }
    function slotShiftLeft(group, index) { InventoryOps.slotShiftLeft(root, group, index) }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }

    // t179 触碰表达式：切箱子（chestX/Y/Z 变）或 ChestStore.revision 变时，让所有读 chest 槽的绑定重算。
    //   单独属性（而非裸写 chestX + chestStore.revision）是为了给 delegate 一个干净的触碰点（避免每处
    //   重复写四个表达式）。
    property int chestCoordRev: chestStore.revision + chestX * 131 + chestY * 17 + chestZ

    // t167 左键拖动均分总控（同 CraftingTableUI）：DragHandler(LeftButton) 在 root 监听。按下不动时
    //   per-slot 左键 TapHandler 抓；拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动
    //   begin/endLeftDrag。target:null 防 DragHandler 默认拖动父 Item（面板）。
    DragHandler {
        acceptedButtons: Qt.LeftButton
        target: null
        onActiveChanged: {
            if (active) root.beginLeftDrag()
            else root.endLeftDrag()
        }
    }

    // 半透明遮罩：仅吸收点击（防穿透），不关闭面板（E / Esc / closed 信号才关）。
    // 手持物时点遮罩区 → 丢弃为实体（同 SurvivalInventory / CraftingTableUI）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (root.hotbar && root.hotbar.heldBlock !== 0) root.discardHeldRequested()
            }
        }
    }

    // 面板：深色圆角，居中。宽度容纳 9 槽行（9×40=360 + 2×16 边距 = 392）；高度 = 标题 + 箱子 3×9 +
    // 主栏 3×9 + hotbar + 间距/边距（同 CraftingTableUI 量级）。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 372                                  // 标题(22) + 箱子(120) + 主栏(120) + hotbar(40) + 间距/边距
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
                    text: "箱子"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 箱子自身 27 槽（3×9）：读 ChestStore（按 chestX/Y/Z 寻址）；左键整组 / 右键半份 取放
            // （与主栏 / hotbar 共享同一 hotbar VM 光标手持栈）。栈写经 chestStore.setSlot。
            Grid {
                width: root.chestCols * root.slotSize
                height: root.chestRows * root.slotSize
                columns: root.chestCols; spacing: 0
                Repeater {
                    model: root.chestRows * root.chestCols  // 27
                    delegate: Item {
                        property int chId: { root.chestCoordRev; return root.chestStore.slotIdAt(root.chestX, root.chestY, root.chestZ, index) }
                        property int chCount: { root.chestCoordRev; return root.chestStore.slotCountAt(root.chestX, root.chestY, root.chestZ, index) }
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent; wellColor: "#3a2a18" }  // 箱子槽底偏木色（区隔玩家槽）
                        Item {
                            anchors.centerIn: parent
                            width: 30; height: 30
                            visible: chId !== 0
                            Image {
                                anchors.fill: parent
                                visible: { root.chestCoordRev; return !root.hotbar.isTool(chId) && !root.hotbar.isMaterial(chId) }
                                source: { root.chestCoordRev; return root.hotbar.iconSourceForBlock(chId) }
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            ToolIcon {
                                anchors.fill: parent
                                visible: { root.chestCoordRev; return root.hotbar.isTool(chId) }
                                tier: { root.chestCoordRev; return root.hotbar.toolTier(chId) }
                            }
                            MaterialIcon {
                                anchors.fill: parent
                                visible: { root.chestCoordRev; return root.hotbar.isMaterial(chId) }
                                materialId: { root.chestCoordRev; return chId }
                            }
                        }
                        // 栈数量（count>1 显数字）。触碰 chestCoordRev 刷新（ChestStore NOTIFY 驱动）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { root.chestCoordRev; return chCount > 1 }
                            text: { root.chestCoordRev; return chCount }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        // 左键整组（resolveClick）；右键走 per-slot 右键 TapHandler（resolveRightClick）。
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                // t98 双击合并：400ms 内同槽二次点击 → doMergeSameId。
                                const key = root.slotKey("chest", index)
                                const now = Date.now()
                                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                root.lastTapMs = now
                                root.lastTapKey = key
                                if (isDouble) { root.doMergeSameId("chest", index); return }
                                const r = root.resolveClick(chId, chCount)
                                if (!r) return
                                root.chestStore.setSlot(root.chestX, root.chestY, root.chestZ, index, r.slotId, r.slotCount)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
                        }
                        // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                const r = root.resolveRightClick(chId, chCount)
                                if (!r) return
                                root.chestStore.setSlot(root.chestX, root.chestY, root.chestZ, index, r.slotId, r.slotCount)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
                        }
                        HoverHandler {
                            // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                            // 不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                            property int trackedId: chId
                            onTrackedIdChanged: {
                                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                    root.hoveredItemId = 0
                            }
                            onHoveredChanged: {
                                // t94 tooltip（chId 由 delegate 持有；触碰 chestCoordRev 刷新）。
                                if (hovered && chId !== 0) {
                                    root.hoveredItemId = chId
                                    const p = parent.mapToItem(root, parent.width / 2, 0)
                                    root.hoveredTipPos = Qt.point(p.x, p.y)
                                } else if (root.hoveredItemId === chId) {
                                    root.hoveredItemId = 0
                                }
                                const key = root.slotKey("chest", index)
                                if (hovered) root.hoveredKey = key
                                else if (root.hoveredKey === key) root.hoveredKey = ""
                                // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                                if (hovered && root.leftDragActive) {
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
                                root.dragSlots; root.leftDragActive; root.chestCoordRev
                                return root.leftDragActive
                                    && root.dragHasKey(root.slotKey("chest", index))
                                    && (chId === 0 || chId === root.dragHeldId)
                            }
                            z: 10
                        }
                    }
                }
            }

            // t97 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，三菜单共享）；左键整组 / 右键半份
            // 取放（与 SurvivalInventory / CraftingTableUI 主栏同模式）。主栏栈写经 hotbar.mainSetStack。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    model: root.hotbar.mainCount
                    delegate: Item {
                        property int mainId: { root.hotbar.mainRevision; return root.hotbar.mainBlockIdAt(index) }
                        property int mainCount: { root.hotbar.mainRevision; return root.hotbar.mainCountAt(index) }
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent }
                        Item {
                            anchors.centerIn: parent
                            width: 30; height: 30
                            visible: mainId !== 0
                            Image {
                                anchors.fill: parent
                                visible: { root.hotbar.mainRevision; return !root.hotbar.isTool(mainId) && !root.hotbar.isMaterial(mainId) }
                                source: { root.hotbar.mainRevision; return root.hotbar.iconSourceForBlock(mainId) }
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            ToolIcon {
                                anchors.fill: parent
                                visible: { root.hotbar.mainRevision; return root.hotbar.isTool(mainId) }
                                tier: { root.hotbar.mainRevision; return root.hotbar.toolTier(mainId) }
                            }
                            MaterialIcon {
                                anchors.fill: parent
                                visible: { root.hotbar.mainRevision; return root.hotbar.isMaterial(mainId) }
                                materialId: { root.hotbar.mainRevision; return mainId }
                            }
                        }
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { root.hotbar.mainRevision; return mainCount > 1 }
                            text: { root.hotbar.mainRevision; return mainCount }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                // t110：Shift+左键 → main 槽搬运到首个空 hotbar 槽（早于双击合并 / 普通左键）。
                                if (window.shiftHeld) { root.slotShiftLeft("main", index); return }
                                // t98 双击合并：400ms 内同槽二次点击 → doMergeSameId。
                                const key = root.slotKey("main", index)
                                const now = Date.now()
                                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                root.lastTapMs = now
                                root.lastTapKey = key
                                if (isDouble) { root.doMergeSameId("main", index); return }
                                const r = root.resolveClick(mainId, mainCount)
                                if (!r) return
                                root.hotbar.mainSetStack(index, r.slotId, r.slotCount)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                const r = root.resolveRightClick(mainId, mainCount)
                                if (!r) return
                                root.hotbar.mainSetStack(index, r.slotId, r.slotCount)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
                        }
                        HoverHandler {
                            property int trackedId: mainId
                            onTrackedIdChanged: {
                                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                    root.hoveredItemId = 0
                            }
                            onHoveredChanged: {
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
                                if (hovered && root.leftDragActive) {
                                    root.addDragSlot(key)
                                }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: "#7fe57f"; border.width: 2
                            visible: {
                                root.dragSlots; root.leftDragActive; root.hotbar.mainRevision
                                return root.leftDragActive
                                    && root.dragHasKey(root.slotKey("main", index))
                                    && (mainId === 0 || mainId === root.dragHeldId)
                            }
                            z: 10
                        }
                    }
                }
            }

            // 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            // 属性触碰 slotRevision。左键整组 / 右键半份同主栏（hotbar 槽写经 hotbar.setStack）。不切真实选中。
            Item {
                width: root.mainCols * root.slotSize
                height: root.slotSize

                Row {
                    spacing: 0
                    Repeater {
                        model: root.hotbar.slotCount
                        delegate: Item {
                            property int slotId: { root.hotbar.slotRevision; return root.hotbar.blockIdAt(index) }
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent }
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: slotId !== 0
                                Image {
                                    anchors.fill: parent
                                    visible: { root.hotbar.slotRevision; return !root.hotbar.isTool(slotId) && !root.hotbar.isMaterial(slotId) }
                                    source: { root.hotbar.slotRevision; return root.hotbar.iconSourceForBlock(slotId) }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { root.hotbar.slotRevision; return root.hotbar.isTool(slotId) }
                                    tier: { root.hotbar.slotRevision; return root.hotbar.toolTier(slotId) }
                                }
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { root.hotbar.slotRevision; return root.hotbar.isMaterial(slotId) }
                                    materialId: { root.hotbar.slotRevision; return slotId }
                                }
                            }
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { root.hotbar.slotRevision; return root.hotbar.countAt(index) > 1 }
                                text: { root.hotbar.slotRevision; return root.hotbar.countAt(index) }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    if (window.shiftHeld) { root.slotShiftLeft("hotbar", index); return }
                                    const key = root.slotKey("hotbar", index)
                                    const now = Date.now()
                                    const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                    root.lastTapMs = now
                                    root.lastTapKey = key
                                    if (isDouble) { root.doMergeSameId("hotbar", index); return }
                                    const r = root.resolveClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index))
                                    if (r) {
                                        root.hotbar.setStack(index, r.slotId, r.slotCount)
                                        root.hotbar.heldBlock = r.heldId
                                        root.hotbar.heldCount = r.heldCount
                                    }
                                }
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const r = root.resolveRightClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index))
                                    if (r) {
                                        root.hotbar.setStack(index, r.slotId, r.slotCount)
                                        root.hotbar.heldBlock = r.heldId
                                        root.hotbar.heldCount = r.heldCount
                                    }
                                }
                            }
                            HoverHandler {
                                property int trackedId: slotId
                                onTrackedIdChanged: {
                                    if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                        root.hoveredItemId = 0
                                }
                                onHoveredChanged: {
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
                                    if (hovered && root.leftDragActive) {
                                        root.addDragSlot(key)
                                    }
                                }
                            }
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: "#7fe57f"; border.width: 2
                                visible: {
                                    root.dragSlots; root.leftDragActive; root.hotbar.slotRevision
                                    return root.leftDragActive
                                        && root.dragHasKey(root.slotKey("hotbar", index))
                                        && (slotId === 0 || slotId === root.dragHeldId)
                                }
                                z: 10
                            }
                        }
                    }
                }
            }
        }
    }

    // t94 物品名悬停 tooltip（纯 QtQuick 自绘；同 CraftingTableUI）。各槽 HoverHandler 进入时写
    // hoveredItemId + hoveredTipPos；离开按 id 守卫清除（防相邻槽进出竞态互清）。名字走 hotbar.nameForBlock。
    property int hoveredItemId: 0
    property point hoveredTipPos: Qt.point(0, 0)
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
            text: root.hotbar ? root.hotbar.nameForBlock(root.hoveredItemId) : ""
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
