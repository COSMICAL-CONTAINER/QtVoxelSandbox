import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot 等）抽取自共享 JS 库
//   InventoryOps.js；本面板仅保留 5 本地槽路由 + 薄委托包装。算法单一权威收敛于此库。
import "InventoryOps.js" as InventoryOps

// 酿造面板（t1097）：右键酿造台方块打开（PlayerController::brewingStandOpened → Main.qml Connections →
// 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// 布局贴近 MC 1.0 酿造台 GUI 语义（§9 自绘原创）：顶部原料槽 + 中部酿造进度箭头（原材料→瓶行）+
// 底行 3 药水瓶槽 + 左下燃料槽（燃烬粉，燃料计量条），下部 3×9=27 主物品栏 + 9 hotbar 行
// （与漏斗 / 熔炉面板同布局，便于从背包取瓶 / 材料放入）。
//
// tick 面（关键设计）：酿造推进权威在 **C++ scanBrewingStands**（PlayerController tickImpl 管线，
// dt 累积常开——关面板照常酿造，机制等价 MC 台内进程持续）。本面板**不 tick**——只读 BrewingStore
// （revision 触碰刷新绑定），槽写经 setSlot 即时生效、C++ tick 下一帧直读 store（t177 三轮「tick 直读
// store」教训的 C++ 侧终局形态：呈现层零推进面 = 零绑定陈旧风险）。
//
// 槽位（BrewingStore::kSlot* 单一权威）：brew0/brew1/brew2 = 3 药水瓶槽(0..2) / ing = 原料槽(3) /
// fuel = 燃料槽(4)。酿造语义 = 瓶**原位变换**（brew 槽 id 自身变产物，无独立输出槽——MC 口径）。
//
// 物品移动：左键整组 / 右键半份 / 双击拿同类 / Shift 搬运，与漏斗面板同算法（InventoryOps 共享）。
//
// 状态持久：面板常驻（visible 切换不销毁）→ 槽内容跨开关持久（存于 BrewingStore per-block，存档走
// worldstore brewing 表）。关包仅归还光标手持栈（宿主 returnHeldToHotbar），不归还台槽（MC 行为）。
//
// 全部槽框 / 箭头 / 燃料条自绘原创（InvSlot 凹陷槽 + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点格子），关闭 → grab。

Item {
    id: root

    // t745 pack 开关 → 方块图标绑定重算（同 HopperUI）。
    ResourcePackManager { id: iconPackRefresh }

    // 宿主注入：hotbar 视图模型（heldBlock/heldCount/maxStackSize/iconSourceForBlock/nameForBlock/
    // isTool/isMaterial/slotRevision/main*/addStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 宿主注入：BrewingStore（按 brewingX/Y/Z 寻址的 per-block 5 槽 + 酿造进度 / 燃料计量；与 C++
    //   scanBrewingStands 机制面共用同一 store，UI 只是读族 / 写族的呈现层消费端）。
    property BrewingStore brewingStore
    // 宿主注入：当前所开酿造台的方块世界坐标（brewingStandOpened 携坐标 → Main.qml 存 window.brewingX/Y/Z）。
    property int brewingX: 0
    property int brewingY: 0
    property int brewingZ: 0
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // 请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区；左键整栈）。
    signal discardHeldRequested()
    // 请求宿主把光标手持栈**丢 1 件**为实体（右键拖出面板外）。
    signal discardHeldOneRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int mainCols: 9
    readonly property int mainRows: 3
    // 单次酿造耗时（秒）——呈现层只读显示（ BrewingStore::kBrewSecs 单一权威的 QML 镜像常量；
    //   C++ tick 是唯一推进面，本值仅驱动进度箭头比例）。
    readonly property real kBrewSecsUi: 20.0

    // 触碰表达式：切台（brewingX/Y/Z 变）或 BrewingStore.revision 变时，所有读台槽 / 进度的绑定重算
    //   （同 HopperUI hopCoordRev / FurnaceUI furnaceCoordRev 模式）。
    property int brewCoordRev: (brewingStore ? brewingStore.revision : 0) + brewingX * 131 + brewingY * 17 + brewingZ

    // 酿造进度（0..kBrewSecs，只读绑定；箭头比例）+ 燃料剩余可酿次数（燃料条比例源）。
    property real brewProgress: { const _r = root.brewCoordRev; return _r >= 0 ? (root.brewingStore ? root.brewingStore.brewProgressAt(root.brewingX, root.brewingY, root.brewingZ) : 0.0) : 0.0 }
    property int fuelOps: { const _r = root.brewCoordRev; return _r >= 0 ? (root.brewingStore ? root.brewingStore.fuelOpsAt(root.brewingX, root.brewingY, root.brewingZ) : 0) : 0 }

    // t167 左键拖动均分 + t181 右键拖动（与漏斗面板同构状态面；InventoryOps 消费）。
    property bool leftDragActive: false
    property var dragSlots: []
    property string hoveredKey: ""
    property int dragHeldId: 0
    property int dragHeldCount: 0
    property int dragHeldDurability: 0
    property var dragHeldEnchants: []
    property string dragHeldName: ""
    property bool rightDragActive: false
    property var rightDragSlots: []
    property bool rightDragPlaced: false
    property bool dragActive: leftDragActive || rightDragActive
    property var dragOriginal: ({})
    property var dragWritten: ({})
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t180：5 台槽全部参与快捷操作（拖动均分 / 双击拿同类 / 右键分半）。本地组 = 台槽五组。
    property var localDragGroups: ["brew0", "brew1", "brew2", "ing", "fuel"]
    function localSlotCount(group) { return (group === "brew0" || group === "brew1" || group === "brew2" || group === "ing" || group === "fuel") ? 1 : 0 }

    // ── 面板专属槽路由：台槽走 BrewingStore（按 brewingX/Y/Z 寻址；main/hotbar 由 InventoryOps 统一经
    //   VM）。group 名 → 槽位下标单一映射（BrewingStore::kSlot* 语义序）。t647 同族：实例元数据透传。
    function brewingSlotIndex(group) {
        if (group === "brew0") return 0
        if (group === "brew1") return 1
        if (group === "brew2") return 2
        if (group === "ing")   return 3
        if (group === "fuel")  return 4
        return -1
    }
    function localReadSlot(group, index) {
        const si = root.brewingSlotIndex(group)
        if (si < 0 || !root.brewingStore) return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0], name: "" }
        return {
            id: root.brewingStore.slotIdAt(root.brewingX, root.brewingY, root.brewingZ, si),
            count: root.brewingStore.slotCountAt(root.brewingX, root.brewingY, root.brewingZ, si),
            durability: root.brewingStore.slotDurabilityAt(root.brewingX, root.brewingY, root.brewingZ, si),
            enchants: root.brewingStore.slotEnchantsAt(root.brewingX, root.brewingY, root.brewingZ, si),
            name: root.brewingStore.slotNameAt(root.brewingX, root.brewingY, root.brewingZ, si)
        }
    }
    function localWriteSlot(group, index, id, count, durability, enchants, name) {
        const si = root.brewingSlotIndex(group)
        if (si < 0 || !root.brewingStore) return
        root.brewingStore.setSlot(root.brewingX, root.brewingY, root.brewingZ, si, id, count,
                                 InventoryOps.list4(enchants),
                                 (typeof name === "string") ? name : "",
                                 (durability > 0) ? durability : -1)
    }
    function resolveClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch, curName) }
    function resolveRightClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch, curName) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count, durability, enchants, name) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants, name) }

    // ── 拖动均分 + Shift/数字键搬运 + 双击合并：算法见 InventoryOps，本处薄委托包装（同漏斗面板）。
    function slotKey(group, index) { return InventoryOps.slotKey(group, index) }
    function dragHasKey(key) { return InventoryOps.dragHasKey(root, key) }
    function pointInsidePanel(x, y) { return InventoryOps.pointInsidePanel(root, panel, x, y) }
    function addDragSlot(key) { InventoryOps.addDragSlot(root, key) }
    function beginLeftDrag() { InventoryOps.beginLeftDrag(root) }
    function endLeftDrag() { InventoryOps.endLeftDrag(root) }
    function beginRightDrag() { InventoryOps.beginRightDrag(root) }
    function endRightDrag() { InventoryOps.endRightDrag(root) }
    function addRightDragSlot(key) { InventoryOps.addRightDragSlot(root, key) }
    function rightDragHasKey(key) { return InventoryOps.rightDragHasKey(root, key) }
    // Shift+左键双向搬运（main/hotbar → 整栈并入台 5 槽；台槽 → 整栈归还背包；同漏斗 slotShiftLeft 门）。
    function slotShiftLeft(group, index) {
        if (!root.hotbar) return
        if (group === "main" || group === "hotbar") {
            const src = InventoryOps.readSlot(root, group, index)
            if (src.id === 0 || src.count <= 0) return
            const cap = root.hotbar.maxStackSize(src.id)
            let remaining = src.count
            for (let s = 0; s < 5 && remaining > 0; ++s) {
                const g = root.groupOfIndex(s)
                const cur = root.localReadSlot(g, 0)
                if (cur.id === src.id && cur.count < cap) {
                    const move = Math.min(cap - cur.count, remaining)
                    root.localWriteSlot(g, 0, src.id, cur.count + move, cur.durability, cur.enchants, cur.name)
                    remaining -= move
                }
            }
            for (let s = 0; s < 5 && remaining > 0; ++s) {
                const g = root.groupOfIndex(s)
                if (root.localReadSlot(g, 0).id === 0) {
                    const move = Math.min(cap, remaining)
                    root.localWriteSlot(g, 0, src.id, move, src.durability, src.enchants, src.name)
                    remaining -= move
                }
            }
            if (remaining !== src.count) {
                InventoryOps.writeSlot(root, group, index, remaining > 0 ? src.id : 0, remaining,
                                      remaining > 0 ? src.durability : 0,
                                      remaining > 0 ? src.enchants : [0,0,0,0],
                                      remaining > 0 ? src.name : "")
            }
            return
        }
        if (root.brewingSlotIndex(group) >= 0) {
            const src = root.localReadSlot(group, index)
            if (src.id === 0 || src.count <= 0) return
            const remain = root.hotbar.addToAny(src.id, src.count, src.durability, src.enchants, src.name)
            root.localWriteSlot(group, index, remain > 0 ? src.id : 0, remain,
                                remain > 0 ? src.durability : 0,
                                remain > 0 ? src.enchants : [0,0,0,0],
                                remain > 0 ? src.name : "")
            return
        }
        InventoryOps.slotShiftLeft(root, group, index)
    }
    // 槽位下标 → 组名（slotShiftLeft 扫描用；kSlot* 语义序单一映射）。
    function groupOfIndex(si) {
        if (si === 0) return "brew0"
        if (si === 1) return "brew1"
        if (si === 2) return "brew2"
        if (si === 3) return "ing"
        return "fuel"
    }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }

    DragHandler {
        acceptedButtons: Qt.LeftButton
        target: null
        onActiveChanged: {
            if (active) root.beginLeftDrag()
            else root.endLeftDrag()
        }
    }
    DragHandler {
        acceptedButtons: Qt.RightButton
        target: null
        onActiveChanged: {
            if (active) root.beginRightDrag()
            else root.endRightDrag()
        }
    }

    // 半透明遮罩：仅吸收点击（防穿透），不关闭面板。手持物点面板外 → 丢弃（左键整栈 / 右键 1 件）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: (mouse) => {
                if (!root.hotbar || root.hotbar.heldBlock === 0) return
                if (root.pointInsidePanel(mouse.x, mouse.y)) return
                if (mouse.button === Qt.RightButton) root.discardHeldOneRequested()
                else                                root.discardHeldRequested()
            }
        }
    }

    // 面板：深色圆角居中（同族风格）。高度 = 标题(22) + 台行(88) + 主栏(120) + hotbar(40) + 间距/边距。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 340
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
                    text: "酿造台"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 台行：原料槽（顶中）+ 酿造进度箭头（原料→瓶行）+ 3 瓶槽（底中）+ 燃料槽（左下，燃料计量条）。
            //   布局贴近 MC 1.0 酿造 GUI 语义（§9 自绘原创）：原料在上 / 瓶行在下 / 燃料左下。
            Item {
                id: standRow
                width: parent.width
                height: root.slotSize * 2 + 8
                readonly property real cx: width / 2

                // 原料槽（顶中，偏左半格——为箭头留右侧通道）。
                StandSlot { grp: "ing"; idx: 0; x: standRow.cx - root.slotSize - 30; y: 0 }
                // 酿造进度箭头（原料槽右下 → 瓶行）：brewProgress/kBrewSecsUi 比例填充（自绘，同熔炉箭头族）。
                Canvas {
                    id: brewArrow
                    x: standRow.cx - 12; y: root.slotSize - 14
                    width: 24; height: 20
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        ctx.imageSmoothingEnabled = false
                        ctx.fillStyle = "#3a3a3a"
                        ctx.fillRect(0, 8, 16, 4)
                        ctx.beginPath()
                        ctx.moveTo(16, 2); ctx.lineTo(24, 10); ctx.lineTo(16, 18); ctx.closePath()
                        ctx.fill()
                        const ratio = root.kBrewSecsUi > 0 ? Math.max(0, Math.min(1, root.brewProgress / root.kBrewSecsUi)) : 0
                        if (ratio > 0) {
                            ctx.save()
                            ctx.beginPath()
                            ctx.rect(0, 0, 24 * ratio, 20)
                            ctx.clip()
                            ctx.fillStyle = "#7fe57f"
                            ctx.fillRect(0, 8, 16, 4)
                            ctx.beginPath()
                            ctx.moveTo(16, 2); ctx.lineTo(24, 10); ctx.lineTo(16, 18); ctx.closePath()
                            ctx.fill()
                            ctx.restore()
                        }
                    }
                    Connections { target: root; function onBrewProgressChanged() { brewArrow.requestPaint() } }
                    Component.onCompleted: brewArrow.requestPaint()
                }
                // 3 药水瓶槽（底行居中）——瓶原位变换语义槽。
                StandSlot { grp: "brew0"; idx: 0; x: standRow.cx - root.slotSize * 1.5 + 20; y: root.slotSize + 8 }
                StandSlot { grp: "brew1"; idx: 0; x: standRow.cx - root.slotSize * 0.5 + 20; y: root.slotSize + 8 }
                StandSlot { grp: "brew2"; idx: 0; x: standRow.cx + root.slotSize * 0.5 + 20; y: root.slotSize + 8 }
                // 燃料槽（左下）+ 燃料计量条（fuelOps/kPowderFuelOpsUi 比例——满 20 次刚点燃 / 0 近耗尽）。
                StandSlot { grp: "fuel"; idx: 0; x: standRow.cx - root.slotSize * 3.2; y: root.slotSize + 8 }
                Rectangle {
                    x: standRow.cx - root.slotSize * 3.2 + 2; y: root.slotSize * 2 + 6
                    height: 3
                    // 燃料计量比例（0..1 → 宽度；fuelOps=0 → 不可见，同熔炉燃料条口径）。
                    readonly property real opsFull: 20.0 // BrewingStore::kPowderFuelOps 的 QML 镜像（仅显示比例）
                    width: root.fuelOps > 0 ? Math.max(0, (root.slotSize - 4) * Math.min(1, root.fuelOps / opsFull)) : 0
                    color: "#e85a18"
                    visible: root.fuelOps > 0
                    z: 5
                }
            }

            // 3×9 主物品栏（27 槽）：读 hotbar VM（多菜单共享）；与台槽 / hotbar 共享光标手持栈。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    model: root.hotbar.mainCount
                    delegate: Item {
                        property int mainId: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainBlockIdAt(index)) : 0 }
                        property int mainCount: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainCountAt(index)) : 0 }
                        property int mainDur: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainDurabilityAt(index)) : 0 }
                        property var mainEnch: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainEnchantsAt(index)) : 0 }
                        property string mainName: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainCustomNameAt(index)) : "" }
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent }
                        Item {
                            anchors.centerIn: parent
                            width: 30; height: 30
                            visible: mainId !== 0
                            Image {
                                anchors.fill: parent
                                visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (!root.hotbar.isTool(mainId) && !root.hotbar.isMaterial(mainId)) : false }
                                source: { const _r = root.hotbar.mainRevision; const _p = iconPackRefresh.active; return _r >= 0 && _p >= 0 ? (root.hotbar.iconSourceForBlock(mainId)) : "" }
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
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount > 1) : false }
                            text: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount) : "" }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        Rectangle {
                            anchors.fill: parent
                            visible: {
                                const _r = root.hotbar.mainRevision
                                if (_r < 0 || mainId === 0) return false
                                return !!mainEnch && ((mainEnch[0] || 0) !== 0 || (mainEnch[1] || 0) !== 0 || (mainEnch[2] || 0) !== 0 || (mainEnch[3] || 0) !== 0)
                            }
                            color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                            radius: 3
                            z: 3
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                if (window.shiftHeld) { root.slotShiftLeft("main", index); return }
                                const key = root.slotKey("main", index)
                                const now = Date.now()
                                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                root.lastTapMs = now
                                root.lastTapKey = key
                                if (isDouble) { root.doMergeSameId("main", index); return }
                                const r = root.resolveClick(mainId, mainCount, mainDur, mainEnch, mainName)
                                if (!r) return
                                root.hotbar.mainSetStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                                root.hotbar.heldDurability = r.heldDur
                                root.hotbar.setHeldEnchants(r.heldEnch)
                                root.hotbar.heldCustomName = r.heldName
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                const r = root.resolveRightClick(mainId, mainCount, mainDur, mainEnch, mainName)
                                if (!r) return
                                root.hotbar.mainSetStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                                root.hotbar.heldDurability = r.heldDur
                                root.hotbar.setHeldEnchants(r.heldEnch)
                                root.hotbar.heldCustomName = r.heldName
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
                                if (hovered && root.dragActive) root.addDragSlot(key)
                            }
                        }
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

            // 底部 9 槽 hotbar 行（同步游戏内 hotbar）：不切真实选中。
            Item {
                width: root.mainCols * root.slotSize
                height: root.slotSize
                Row {
                    spacing: 0
                    Repeater {
                        model: root.hotbar.slotCount
                        delegate: Item {
                            property int slotId: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.blockIdAt(index)) : 0 }
                            property int slotCountN: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.countAt(index)) : 0 }
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent }
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: slotId !== 0
                                Image {
                                    anchors.fill: parent
                                    visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (!root.hotbar.isTool(slotId) && !root.hotbar.isMaterial(slotId)) : false }
                                    source: { const _r = root.hotbar.slotRevision; const _p = iconPackRefresh.active; return _r >= 0 && _p >= 0 ? (root.hotbar.iconSourceForBlock(slotId)) : "" }
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
                                visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (slotCountN > 1) : false }
                                text: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (slotCountN) : "" }
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
                                    const cur = InventoryOps.readSlot(root, "hotbar", index)
                                    const r = root.resolveClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                    if (!r) return
                                    root.hotbar.setStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                    root.hotbar.setHeldEnchants(r.heldEnch)
                                    root.hotbar.heldCustomName = r.heldName
                                }
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const cur = InventoryOps.readSlot(root, "hotbar", index)
                                    const r = root.resolveRightClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                    if (!r) return
                                    root.hotbar.setStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                    root.hotbar.setHeldEnchants(r.heldEnch)
                                    root.hotbar.heldCustomName = r.heldName
                                }
                            }
                            HoverHandler {
                                property int trackedId: slotId
                                onTrackedIdChanged: {
                                    if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                        root.hoveredItemId = 0
                                }
                                onHoveredChanged: {
                                    const itemId = slotId
                                    if (hovered && itemId !== 0) {
                                        root.hoveredItemId = itemId
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                    } else if (root.hoveredItemId === itemId) {
                                        root.hoveredItemId = 0
                                    }
                                    const key = root.slotKey("hotbar", index)
                                    if (hovered) root.hoveredKey = key
                                    else if (root.hoveredKey === key) root.hoveredKey = ""
                                    if (hovered && root.dragActive) root.addDragSlot(key)
                                }
                            }
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

    // ── 台槽组件（5 槽共用）：grp = 本地组名（brew0/brew1/brew2/ing/fuel）。读 BrewingStore（brewCoordRev
    //   驱动刷新）；左键整组 / 右键半份 / 双击合并 / Shift 搬运 / 拖拽高亮 / 附魔光晕（漏斗槽 delegate 同门）。
    component StandSlot : Item {
        id: standSlotRoot
        property string grp: "ing"
        property int idx: 0
        width: root.slotSize; height: root.slotSize
        // 本槽栈数据（触碰 brewCoordRev 刷新——表达式形式防 AOT 死代码消除，lessons 铁律）。
        property int slotItemId: {
            const _r = root.brewCoordRev
            return _r >= 0 ? (root.brewingStore ? root.brewingStore.slotIdAt(root.brewingX, root.brewingY, root.brewingZ, root.brewingSlotIndex(standSlotRoot.grp)) : 0) : 0
        }
        property int slotItemCount: {
            const _r = root.brewCoordRev
            return _r >= 0 ? (root.brewingStore ? root.brewingStore.slotCountAt(root.brewingX, root.brewingY, root.brewingZ, root.brewingSlotIndex(standSlotRoot.grp)) : 0) : 0
        }
        InvSlot { anchors.fill: parent; wellColor: "#262b30" }
        Item {
            anchors.centerIn: parent
            width: 30; height: 30
            visible: standSlotRoot.slotItemId !== 0
            Image {
                anchors.fill: parent
                visible: { const _r = root.brewCoordRev; return _r >= 0 ? (!root.hotbar.isTool(standSlotRoot.slotItemId) && !root.hotbar.isMaterial(standSlotRoot.slotItemId)) : false }
                source: { const _r = root.brewCoordRev; const _p = iconPackRefresh.active; return _r >= 0 && _p >= 0 ? (root.hotbar.iconSourceForBlock(standSlotRoot.slotItemId)) : "" }
                fillMode: Image.PreserveAspectFit; smooth: true
            }
            ToolIcon {
                anchors.fill: parent
                visible: { const _r = root.brewCoordRev; return _r >= 0 ? (root.hotbar.isTool(standSlotRoot.slotItemId)) : false }
                tier: { const _r = root.brewCoordRev; return _r >= 0 ? (root.hotbar.toolTier(standSlotRoot.slotItemId)) : 0 }
                toolType: { const _r = root.brewCoordRev; return _r >= 0 ? (root.hotbar.toolType(standSlotRoot.slotItemId)) : 0 }
            }
            MaterialIcon {
                anchors.fill: parent
                visible: { const _r = root.brewCoordRev; return _r >= 0 ? (root.hotbar.isMaterial(standSlotRoot.slotItemId)) : false }
                materialId: { const _r = root.brewCoordRev; return _r >= 0 ? (standSlotRoot.slotItemId) : 0 }
            }
        }
        Text {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.rightMargin: 3; anchors.bottomMargin: 1
            visible: { const _r = root.brewCoordRev; return _r >= 0 ? (standSlotRoot.slotItemCount > 1) : false }
            text: { const _r = root.brewCoordRev; return _r >= 0 ? (standSlotRoot.slotItemCount) : "" }
            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
            font.pixelSize: 13; font.bold: true
        }
        // t647 同族附魔光晕（BrewingStore 元数据全量在位 → 光晕可用）。
        Rectangle {
            anchors.fill: parent
            visible: {
                const _r = root.brewCoordRev
                if (_r < 0 || standSlotRoot.slotItemId === 0 || !root.brewingStore) return false
                const e = root.brewingStore.slotEnchantsAt(root.brewingX, root.brewingY, root.brewingZ, root.brewingSlotIndex(standSlotRoot.grp))
                return !!e && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
            }
            color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
            radius: 3
            z: 3
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                if (window.shiftHeld) { root.slotShiftLeft(standSlotRoot.grp, 0); return }
                const key = root.slotKey(standSlotRoot.grp, 0)
                const now = Date.now()
                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                root.lastTapMs = now
                root.lastTapKey = key
                if (isDouble) { root.doMergeSameId(standSlotRoot.grp, 0); return }
                const cur = root.localReadSlot(standSlotRoot.grp, 0)
                const r = root.resolveClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                if (!r) return
                root.localWriteSlot(standSlotRoot.grp, 0, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
                root.hotbar.heldCustomName = r.heldName
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                const cur = root.localReadSlot(standSlotRoot.grp, 0)
                const r = root.resolveRightClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                if (!r) return
                root.localWriteSlot(standSlotRoot.grp, 0, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
                root.hotbar.heldCustomName = r.heldName
            }
        }
        HoverHandler {
            property int trackedId: standSlotRoot.slotItemId
            onTrackedIdChanged: {
                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                    root.hoveredItemId = 0
            }
            onHoveredChanged: {
                const itemId = standSlotRoot.slotItemId
                if (hovered && itemId !== 0) {
                    root.hoveredItemId = itemId
                    const p = parent.mapToItem(root, parent.width / 2, 0)
                    root.hoveredTipPos = Qt.point(p.x, p.y)
                } else if (root.hoveredItemId === itemId) {
                    root.hoveredItemId = 0
                }
                const key = root.slotKey(standSlotRoot.grp, 0)
                if (hovered) root.hoveredKey = key
                else if (root.hoveredKey === key) root.hoveredKey = ""
                if (hovered && root.dragActive) root.addDragSlot(key)
            }
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "#7fe57f"; border.width: 2
            visible: {
                const _ds = root.dragSlots
                const _rds = root.rightDragSlots
                const _rev = root.brewCoordRev
                const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                const sid = standSlotRoot.slotItemId
                const key = root.slotKey(standSlotRoot.grp, 0)
                if (_ok && root.leftDragActive && root.dragHasKey(key)
                    && (sid === 0 || sid === root.dragHeldId)) return true
                return _ok && root.rightDragActive && root.rightDragHasKey(key)
            }
            z: 10
        }
    }

    // t94 物品名悬停 tooltip（同漏斗面板：hotbar.nameForBlock + 实例名 + 耐久行；纯 QtQuick 自绘）。
    property int hoveredItemId: 0
    property point hoveredTipPos: Qt.point(0, 0)
    property int hoveredDurability: {
        if (!root.hotbar || !root.hoveredItemId || !root.hotbar.isTool(root.hoveredItemId)) return -1
        root.hotbar.slotRevision; root.hotbar.mainRevision
        const key = root.hoveredKey
        if (!key) return -1
        const parts = key.split(":")
        if (parts.length !== 2) return -1
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return -1
        if (parts[0] === "hotbar") return root.hotbar.durabilityAt(idx)
        if (parts[0] === "main") return root.hotbar.mainDurabilityAt(idx)
        return -1
    }
    property string hoveredCustomName: {
        if (!root.hotbar || !root.hoveredItemId || !root.hoveredKey) return ""
        root.hotbar.slotRevision; root.hotbar.mainRevision
        const parts = root.hoveredKey.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        if (parts[0] === "hotbar") return root.hotbar.customNameAt(idx)
        if (parts[0] === "main") return root.hotbar.mainCustomNameAt(idx)
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
            if (py < 2) py = root.hoveredTipPos.y + 6
            return py
        }
        Text {
            id: tipLabel
            anchors.centerIn: parent
            text: root.hotbar ? ((root.hoveredCustomName.length > 0 ? root.hoveredCustomName
                    : root.hotbar.nameForBlock(root.hoveredItemId))
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")) : ""
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
