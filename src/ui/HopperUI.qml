import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 hopper 本地槽路由 + 薄委托包装（供 QML 信号
//   处理器经 root.xxx 调用）。算法单一权威收敛于此库，消除多面板逐字复制。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

// 漏斗物品栏面板（t1093 / 发射器面板蓝本）：右键漏斗方块打开（PlayerController::hopperOpened → Main.qml
// Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t1080 关单登记的「HopperUI 候选」授权面兑现（dev-spec：QML 玩法路径零迁移约束下的 UI 单向消费增量）。
// 布局贴近 MC 1.0 漏斗：上部「1×5=5 槽漏斗容腔」（机制等价 MC hopper 5 槽）+ 下部「3×9=27 主物品栏 +
// 9 hotbar 行」（容器 + 背包布局，同发射器 / 箱子结构）。
//
// 内容存储 = HopperStore（t1080 交付的 C++ VM，按方块坐标键控的 5 槽；读族 / 写族当即为漏斗引擎机制
//   面所用）→ 每只漏斗各自独立 5 槽、跨 UI 开关持久、破块 / 爆炸掉内容（引擎侧收口）。本面板把 "hopper"
//   组经 localReadSlot/localWriteSlot 钩子路由到 HopperStore（按 hopperX/Y/Z 寻址）。
//
// 物品移动（5 槽 / 主栏 / hotbar 间任意搬动）：左键整组 / 右键半份 / 单放 / 左键拖动均分 / 双击拿同类 /
//   Shift 搬运，与 DispenserUI / ChestUI 全套快捷操作同算法（共享 hotbar VM 的 heldBlock / heldCount
//   光标手持栈）。
//
// 全部槽框自绘原创（InvSlot 凹陷槽，无外部 MC GUI PNG；§9 override (a)）。零 MC 专有名词（§9）。
// 宿主负责指针态：打开时 release（光标可见点格子），关闭 → grab。

Item {
    id: root

    // t745 pack 开关 → activeChanged → 方块图标绑定重算（iconSourceForBlock 双态路由：pack 开 = pack
    //   贴图渲染 / pack 关 = 程序原生；下方各槽图标绑定的 _p 守卫同 slotRevision 的 AOT 模式）。
    ResourcePackManager { id: iconPackRefresh }

    // 宿主注入：hotbar 视图模型（提供 heldBlock/heldCount/maxStackSize/iconSourceForBlock/
    // nameForBlock/isTool/isMaterial/slotRevision/main*/addStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 宿主注入：HopperStore（t1080；按 hopperX/Y/Z 寻址的 per-block 5 槽容腔；slotIdAt/slotCountAt/
    //   setSlot——与漏斗引擎机制面共用同一 store，UI 只是读族 / 写族的呈现层消费端）。
    property HopperStore hopperStore
    // 宿主注入：当前所开漏斗的方块世界坐标（hopperOpened 携坐标 → Main.qml 存 window.hopperX/Y/Z）。
    property int hopperX: 0
    property int hopperY: 0
    property int hopperZ: 0
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // 同 DispenserUI / ChestUI：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区）。
    signal discardHeldRequested()
    // 同 t228：请求宿主把光标手持栈**丢 1 件**为实体（右键拖出面板外；左键整栈走 discardHeldRequested）。
    signal discardHeldOneRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    // MC 1.0 漏斗布局：上部 1×5 容器（HopperStore::kSlotsPerHopper 单一权威），下部 3×9 物品栏 + hotbar。
    readonly property int mainCols: 9
    readonly property int mainRows: 3

    // 1×5 漏斗容腔 per-block 存储（HopperStore，按 hopperX/Y/Z 寻址）；与 hotbar VM 共享同一光标
    //   手持栈 heldBlock/heldCount。hopSlotCount 读 hopperStore.slotCount（单一权威，恒 5）。
    readonly property int hopSlotCount: hopperStore ? hopperStore.slotCount : 5
    // 触碰表达式：切漏斗（hopperX/Y/Z 变）或 HopperStore.revision 变时，让所有读 hop 槽的绑定重算
    //   （同 DispenserUI dispCoordRev / ChestUI chestCoordRev 模式）。
    property int hopCoordRev: (hopperStore ? hopperStore.revision : 0) + hopperX * 131 + hopperY * 17 + hopperZ

    // t97：27 主物品栏自 t97 起上移至 hotbar VM（m_mainSlots），与 SurvivalInventory / 各容器面板共享
    //   同一份 → 主栏同步、returnHeldToHotbar/pickupScan 经 addToAny 能合并进主栏。与漏斗槽 / hotbar
    //   共享同一 hotbar VM 光标手持栈。

    // t167 左键拖动均分：手势由 root 级 DragHandler(LeftButton) 总控（同 DispenserUI 全套；本面板为
    //   InventoryOps 单一权威的薄消费端）。
    property bool leftDragActive: false
    property var dragSlots: []              // "hop:2" / "main:5" / "hotbar:0"
    property string hoveredKey: ""
    property int dragHeldId: 0
    property int dragHeldCount: 0
    property int dragHeldDurability: 0
    property var dragHeldEnchants: []       // t475 拖动期间手持附魔快照（松手回填光标保真）
    property string dragHeldName: ""        // t622 拖动期间手持实例名快照
    property bool rightDragActive: false
    property var rightDragSlots: []
    property bool rightDragPlaced: false
    property bool dragActive: leftDragActive || rightDragActive
    // t98 实时重分撤销机制（同 DispenserUI）。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t180：hopper 1×5 参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明 hopper 为可拖拽
    //   本地组 → InventoryOps.groupIsDraggable 放行。
    property var localDragGroups: ["hopper"]
    // t180：hopper 组槽位数（doMergeSameId 扫描范围）= hopSlotCount（单一权威读 hopperStore.slotCount，恒 5）。
    function localSlotCount(group) { return group === "hopper" ? root.hopSlotCount : 0 }

    // ── 面板专属槽路由：hop 容器格走 HopperStore（按 hopperX/Y/Z 寻址；main/hotbar 由 InventoryOps
    //   统一经 VM）。t647 同族：实例元数据透传（耐久 / 附魔 / 名——HopperStore 读族 / 写族全量在位）。
    function localReadSlot(group, index) {
        if (group === "hopper" && root.hopperStore) {
            return {
                id: root.hopperStore.slotIdAt(root.hopperX, root.hopperY, root.hopperZ, index),
                count: root.hopperStore.slotCountAt(root.hopperX, root.hopperY, root.hopperZ, index),
                durability: root.hopperStore.slotDurabilityAt(root.hopperX, root.hopperY, root.hopperZ, index),
                enchants: root.hopperStore.slotEnchantsAt(root.hopperX, root.hopperY, root.hopperZ, index),
                name: root.hopperStore.slotNameAt(root.hopperX, root.hopperY, root.hopperZ, index)
            }
        }
        return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0], name: "" }
    }
    function localWriteSlot(group, index, id, count, durability, enchants, name) {
        if (group !== "hopper" || !root.hopperStore) return
        root.hopperStore.setSlot(root.hopperX, root.hopperY, root.hopperZ, index, id, count,
                                 // review26 #9：enchants 可为 C++ 序列对象（Array.isArray 恒 false）→
                                 //   list4 归一（DispenserUI 同款防御）。
                                 InventoryOps.list4(enchants),
                                 (typeof name === "string") ? name : "",
                                 (durability > 0) ? durability : -1)
    }
    // resolveClick / resolveRightClick（拾取/放置/合并/互换 + 半份）：算法见 InventoryOps（多面板共享）。
    function resolveClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch, curName) }
    function resolveRightClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch, curName) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count, durability, enchants, name) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants, name) }

    // ── 拖动均分 + Shift/数字键搬运 + 双击合并：算法见 InventoryOps，本处仅薄委托包装（同 DispenserUI）。
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
    // t549 同族：Shift+左键双向搬运（main/hotbar → 整栈并入漏斗 5 槽；hop 槽 → 整栈归还背包 addToAny；
    //   背包满 → 余数留源槽）。并入 / 归还均透传实例元数据（同 addToChest 算法）。
    function slotShiftLeft(group, index) {
        if (!root.hotbar) return
        if (group === "main" || group === "hotbar") {
            const src = InventoryOps.readSlot(root, group, index)
            if (src.id === 0 || src.count <= 0) return
            const cap = root.hotbar.maxStackSize(src.id)
            let remaining = src.count
            for (let i = 0; i < root.hopSlotCount && remaining > 0; ++i) {
                const cur = InventoryOps.readSlot(root, "hopper", i)
                if (cur.id === src.id && cur.count < cap) {
                    const move = Math.min(cap - cur.count, remaining)
                    InventoryOps.writeSlot(root, "hopper", i, src.id, cur.count + move, cur.durability, cur.enchants, cur.name)
                    remaining -= move
                }
            }
            for (let i = 0; i < root.hopSlotCount && remaining > 0; ++i) {
                if (InventoryOps.readSlot(root, "hopper", i).id === 0) {
                    const move = Math.min(cap, remaining)
                    InventoryOps.writeSlot(root, "hopper", i, src.id, move, src.durability, src.enchants, src.name)
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
        if (group === "hopper") {
            const src = InventoryOps.readSlot(root, "hopper", index)
            if (src.id === 0 || src.count <= 0) return
            const remain = root.hotbar.addToAny(src.id, src.count, src.durability, src.enchants, src.name)
            InventoryOps.writeSlot(root, "hopper", index, remain > 0 ? src.id : 0, remain,
                                  remain > 0 ? src.durability : 0,
                                  remain > 0 ? src.enchants : [0,0,0,0],
                                  remain > 0 ? src.name : "")
            return
        }
        InventoryOps.slotShiftLeft(root, group, index)
    }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }

    // t167 左键拖动均分总控（同 DispenserUI；target:null 防拖动父 Item）。
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

    // 面板：深色圆角，居中（同族 #1b1f24 风格）。宽度容纳 9 槽行（9×40=360 + 2×16 边距 = 392）；
    //   高度 = 标题(22) + 漏斗行(40) + 主栏(120) + hotbar(40) + 间距/边距（比发射器矮一行）。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 292                                  // 标题(22) + 漏斗行(40) + 主栏(120) + hotbar(40) + 间距/边距
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
                    text: "漏斗"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 漏斗 1×5 容器格：整体水平居中。读 HopperStore（按 hopperX/Y/Z 寻址；hopCoordRev 驱动刷新）；
            //   左键整组 / 右键半份取放（与主栏 / hotbar 共享同一 hotbar VM 光标手持栈）。栈写经
            //   hopperStore.setSlot（per-block 存储；引擎机制面共用同一权威）。
            Item {
                id: hopRow
                width: parent.width
                height: root.slotSize

                Row {
                    id: hopGrid
                    x: (hopRow.width - root.hopSlotCount * root.slotSize) / 2; y: 0
                    spacing: 0
                    Repeater {
                        model: root.hopSlotCount  // 单一权威读 hopperStore.slotCount（恒 5）
                        delegate: Item {
                            property int hId: { const _r = root.hopCoordRev; return _r >= 0 ? (root.hopperStore.slotIdAt(root.hopperX, root.hopperY, root.hopperZ, index)) : 0 }
                            property int hCount: { const _r = root.hopCoordRev; return _r >= 0 ? (root.hopperStore.slotCountAt(root.hopperX, root.hopperY, root.hopperZ, index)) : 0 }
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                            // 物品图标：方块段→等距立方体 Image；工具段→ToolIcon；材料段→MaterialIcon 自绘。
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: hId !== 0
                                Image {
                                    anchors.fill: parent
                                    visible: { const _r = root.hopCoordRev; return _r >= 0 ? (!root.hotbar.isTool(hId) && !root.hotbar.isMaterial(hId)) : false }
                                    source: { const _r = root.hopCoordRev; const _p = iconPackRefresh.active; return _r >= 0 && _p >= 0 ? (root.hotbar.iconSourceForBlock(hId)) : "" }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.hopCoordRev; return _r >= 0 ? (root.hotbar.isTool(hId)) : false }
                                    tier: { const _r = root.hopCoordRev; return _r >= 0 ? (root.hotbar.toolTier(hId)) : 0 }
                                    toolType: { const _r = root.hopCoordRev; return _r >= 0 ? (root.hotbar.toolType(hId)) : 0 }
                                }
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.hopCoordRev; return _r >= 0 ? (root.hotbar.isMaterial(hId)) : false }
                                    materialId: { const _r = root.hopCoordRev; return _r >= 0 ? (hId) : 0 }
                                }
                            }
                            // 栈数量（count>1 显数字）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { const _r = root.hopCoordRev; return _r >= 0 ? (hCount > 1) : false }
                                text: { const _r = root.hopCoordRev; return _r >= 0 ? (hCount) : "" }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // t647 同族附魔光晕（漏斗槽；HopperStore 元数据全量在位 → 光晕可用）。
                            Rectangle {
                                anchors.fill: parent
                                visible: {
                                    const _r = root.hopCoordRev
                                    if (_r < 0 || hId === 0 || !root.hopperStore) return false
                                    const e = root.hopperStore.slotEnchantsAt(root.hopperX, root.hopperY, root.hopperZ, index)
                                    return !!e && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
                                }
                                color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                                radius: 3
                                z: 3
                            }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    if (window.shiftHeld) { root.slotShiftLeft("hopper", index); return }
                                    // t98 双击合并：280ms 内同槽二次点击 → doMergeSameId。
                                    const key = root.slotKey("hopper", index)
                                    const now = Date.now()
                                    const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                    root.lastTapMs = now
                                    root.lastTapKey = key
                                    if (isDouble) { root.doMergeSameId("hopper", index); return }
                                    const cur = InventoryOps.readSlot(root, "hopper", index)
                                    const r = root.resolveClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                    if (!r) return
                                    root.localWriteSlot("hopper", index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                    root.hotbar.setHeldEnchants(r.heldEnch)
                                    root.hotbar.heldCustomName = r.heldName
                                }
                            }
                            // t166d 同族 per-slot 右键（拿半/放一）。
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const cur = InventoryOps.readSlot(root, "hopper", index)
                                    const r = root.resolveRightClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                    if (!r) return
                                    root.localWriteSlot("hopper", index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                    root.hotbar.setHeldEnchants(r.heldEnch)
                                    root.hotbar.heldCustomName = r.heldName
                                }
                            }
                            HoverHandler {
                                // t99：槽变空时主动清 hoveredItemId（tooltip 残留防）。
                                property int trackedId: hId
                                onTrackedIdChanged: {
                                    if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                        root.hoveredItemId = 0
                                }
                                onHoveredChanged: {
                                    const itemId = hId
                                    if (hovered && itemId !== 0) {
                                        root.hoveredItemId = itemId
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                    } else if (root.hoveredItemId === itemId) {
                                        root.hoveredItemId = 0
                                    }
                                    const key = root.slotKey("hopper", index)
                                    if (hovered) root.hoveredKey = key
                                    else if (root.hoveredKey === key) root.hoveredKey = ""
                                    if (hovered && root.dragActive) {
                                        root.addDragSlot(key)
                                    }
                                }
                            }
                            // t167 均分拖拽高亮（_ds/_rds/_rev 触碰入 _ok 守卫，防 AOT 死代码消除）。
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: "#7fe57f"; border.width: 2
                                visible: {
                                    const _ds = root.dragSlots
                                    const _rds = root.rightDragSlots
                                    const _rev = root.hopCoordRev
                                    const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                                    const sid = hId
                                    const key = root.slotKey("hopper", index)
                                    if (_ok && root.leftDragActive && root.dragHasKey(key)
                                        && (sid === 0 || sid === root.dragHeldId)) return true
                                    return _ok && root.rightDragActive && root.rightDragHasKey(key)
                                }
                                z: 10
                            }
                        }
                    }
                }
            }

            // 3×9 主物品栏（27 槽）：读 hotbar VM（多菜单共享）；与漏斗槽 / hotbar 共享光标手持栈。
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
                                if (hovered && root.dragActive) {
                                    root.addDragSlot(key)
                                }
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

            // 底部 9 槽 hotbar 行（同步游戏内 hotbar；不切真实选中，同族口径）。
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
                                visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.countAt(index) > 1) : false }
                                text: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.countAt(index)) : "" }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            Rectangle {
                                anchors.fill: parent
                                visible: {
                                    const _r = root.hotbar.slotRevision
                                    if (_r < 0 || slotId === 0) return false
                                    const e = root.hotbar.enchantsAt(index)
                                    return e && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
                                }
                                color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                                radius: 3
                                z: 3
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
                                    const r = root.resolveClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index), root.hotbar.durabilityAt(index), root.hotbar.enchantsAt(index), root.hotbar.customNameAt(index))
                                    if (r) {
                                        root.hotbar.setStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                        root.hotbar.heldBlock = r.heldId
                                        root.hotbar.heldCount = r.heldCount
                                        root.hotbar.heldDurability = r.heldDur
                                        root.hotbar.setHeldEnchants(r.heldEnch)
                                        root.hotbar.heldCustomName = r.heldName
                                    }
                                }
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const r = root.resolveRightClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index), root.hotbar.durabilityAt(index), root.hotbar.enchantsAt(index), root.hotbar.customNameAt(index))
                                    if (r) {
                                        root.hotbar.setStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                        root.hotbar.heldBlock = r.heldId
                                        root.hotbar.heldCount = r.heldCount
                                        root.hotbar.heldDurability = r.heldDur
                                        root.hotbar.setHeldEnchants(r.heldEnch)
                                        root.hotbar.heldCustomName = r.heldName
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
                                    if (hovered && root.dragActive) {
                                        root.addDragSlot(key)
                                    }
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

    // t94 同族物品名悬停 tooltip（纯 QtQuick 自绘；不引入 QtQuick.Controls —— lessons-learned 在册风险）。
    property int hoveredItemId: 0
    property point hoveredTipPos: Qt.point(0, 0)
    property int hoveredDurability: {
        if (!root.hotbar || !root.hoveredItemId || !root.hotbar.isTool(root.hoveredItemId)) return -1
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
    // t622 同族实例名（漏斗槽持全量元数据 → hop 组也查）。
    property string hoveredCustomName: {
        if (!root.hotbar || !root.hoveredItemId || !root.hoveredKey) return ""
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const parts = root.hoveredKey.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        if (parts[0] === "hotbar") return _sr >= 0 ? root.hotbar.customNameAt(idx) : ""
        if (parts[0] === "main") return _mr >= 0 ? root.hotbar.mainCustomNameAt(idx) : ""
        return ""
    }
    property string hoveredEnchantText: {
        if (!root.hotbar || !root.hoveredItemId || !root.hoveredKey) return ""
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const parts = root.hoveredKey.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        if (parts[0] === "hotbar") return _sr >= 0 ? root.hotbar.enchantListText(root.hotbar.enchantsAt(idx)) : ""
        if (parts[0] === "main") return _mr >= 0 ? root.hotbar.enchantListText(root.hotbar.mainEnchantsAt(idx)) : ""
        return ""
    }
    property string hoveredAttackText: {
        if (!root.hotbar || !root.hoveredItemId) return ""
        if (root.hotbar.itemAttackDamage(root.hoveredItemId) <= 1) return ""
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const key = root.hoveredKey
        if (!key) return ""
        const parts = key.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        let e = null
        if (parts[0] === "hotbar")      e = _sr >= 0 ? root.hotbar.enchantsAt(idx) : null
        else if (parts[0] === "main")   e = _mr >= 0 ? root.hotbar.mainEnchantsAt(idx) : null
        else return ""
        if (!e) return ""
        const total = root.hotbar.displayAttackDamage(root.hoveredItemId, e)
        return "+" + total + root.hotbar.displayFamilyBonusText(e) + " 攻击"
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
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")
                + (root.hotbar.toolType(root.hoveredItemId) === 7 ? "  攻击 1-" + root.hotbar.bowArrowMaxDamage() : "")
                + (root.hoveredEnchantText.length > 0 ? "\n\n" + root.hoveredEnchantText : "")
                + (root.hoveredAttackText.length > 0 ? "\n\n" + root.hoveredAttackText : "")) : ""
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
