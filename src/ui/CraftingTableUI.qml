import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

// 工作台 3×3 合成面板（t50 / t63 完整 UI）：右键工作台方块打开（PlayerController::craftingTableOpened →
// Main.qml Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t63 布局贴近 MC 1.0 工作台：上部「左 3×3 合成格 + 箭头 + 右结果槽」，下部「3×9=27 主物品栏 + 9 hotbar 行」
// （无装备槽 / 无人偶预览，区别于生存背包）。合成检测走 C++ RecipeRegistry（Game 层单一权威；本组件只读查
// match）。**MC 式数量**：点结果槽 → 取 outputCount 件到光标；输入槽每原料格 consume 1 份（count-1，归 0
// 清 id，不清空整槽 → 可连点合多批）。
//
// 物品移动（合成格 / 主栏 / hotbar 间任意搬动）：左键整组 / 右键半份 / 单放，与 SurvivalInventory.qml 的
// resolveClick / resolveRightClick 同算法（共享 hotbar VM 的 heldBlock / heldCount 光标手持栈）。
//
// 关包（visible→false）：把 craft3 内容 addStack 回 hotbar（合并同类，同拾取），清空 craft3
// （MC 行为：合成格不持久化，关包即退回玩家背包）。主栏 mainSlots 为面板本地存储，组件不销毁 → 跨开关持久。
//
// 全部槽框 / 箭头自绘原创（InvSlot 凹陷槽 + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点格子），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（提供 heldBlock/heldCount/maxStackSize/iconSourceForBlock/
    // nameForBlock/isTool/isMaterial/slotRevision/addStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49 同 SurvivalInventory：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区）。
    signal discardHeldRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int gridN: 3
    // t63：3×9 主物品栏 + 9 hotbar 行（MC 1.0 工作台布局：上部 3×3 合成 + 产物，下部 3×9 物品栏 + hotbar）。
    readonly property int mainCols: 9
    readonly property int mainRows: 3

    // 3×3 合成格本地栈存储（与 hotbar VM 共享同一光标手持栈 heldBlock/heldCount）。
    // 数组改写不触发 QML 绑定 → 配 craftRev 版本号让 Image source / count / 结果槽重算。
    property var craftSlots: [0,0,0, 0,0,0, 0,0,0]
    property var craftCounts:[0,0,0, 0,0,0, 0,0,0]
    property int craftRev: 0

    // t63 3×9=27 主物品栏本地栈存储（复用 SurvivalInventory 主栏的 mainSlots/mainCounts/mainRev 本地数组 +
    // revision-touch 绑定模式）。与合成格 / hotbar 共享同一 hotbar VM 的 heldBlock/heldCount 光标手持栈；
    // 左键整组 / 右键半份同 resolveClick / resolveRightClick。数组突变靠 mainRev 版本号触发绑定刷新。
    // 注：本数组为面板本地（组件不销毁仅 visible=false → 跨开关持久），与 SurvivalInventory 主栏分立存储
    // （共享主栏需上移至 C++ VM，非本任务范围；hotbar 9 槽经 VM 共享是物品进出工作台的主通道）。
    property var mainSlots: [0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0]
    property var mainCounts:[0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0]
    property int mainRev: 0

    // t79 右键拖拽均分（spec：右键按下拖过 N 格 → 松手等分，每格 floor(count/N)，余数留手）。手势由 root
    // 级 TapHandler(WithinBounds, RightButton) 总控；逐槽 HoverHandler 收集扫过格子。合成格 / 主栏 / hotbar
    // 统一支持。dragSlots 存「组:下标」字符串（去重简单）；dragHeld* 为按下瞬间的光标栈快照。
    property bool rightDragActive: false
    property var dragSlots: []              // "craft:2" / "main:5" / "hotbar:0"
    property string hoveredKey: ""
    property int dragHeldId: 0
    property int dragHeldCount: 0

    // resolveClick / resolveRightClick：与 SurvivalInventory.qml 完全一致（拾取/放置/合并/互换 4 case）。
    // 复制而非抽公共组件，保持两文件独立可读（§9 自绘原创，不引入新公共依赖）。
    function resolveClick(curId, curCount) {
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        if (heldId === 0) {
            if (curId === 0) return null
            return { slotId: 0, slotCount: 0, heldId: curId, heldCount: curCount }
        }
        if (curId === 0) {
            return { slotId: heldId, slotCount: heldCount, heldId: 0, heldCount: 0 }
        }
        if (curId === heldId) {
            const cap = root.hotbar.maxStackSize(curId)
            const space = cap - curCount
            if (space <= 0) return null
            const move = Math.min(space, heldCount)
            const remain = heldCount - move
            return { slotId: curId, slotCount: curCount + move,
                     heldId: remain > 0 ? heldId : 0, heldCount: remain }
        }
        return { slotId: heldId, slotCount: heldCount, heldId: curId, heldCount: curCount }
    }
    function resolveRightClick(curId, curCount) {
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        if (heldId === 0) {
            if (curId === 0) return null
            let half = Math.floor(curCount / 2)
            if (half < 1) half = 1
            return { slotId: curCount - half > 0 ? curId : 0, slotCount: curCount - half,
                     heldId: curId, heldCount: half }
        }
        if (curId !== 0 && curId !== heldId) return null
        const cap = root.hotbar.maxStackSize(heldId)
        if (curId === heldId && curCount >= cap) return null
        const remain = heldCount - 1
        return { slotId: heldId, slotCount: curCount + 1,
                 heldId: remain > 0 ? heldId : 0, heldCount: remain }
    }

    // ── t79 右键拖拽均分辅助（与 SurvivalInventory.qml 同算法，独立维护保持两文件可读）──
    function slotKey(group, index) { return group + ":" + index }
    function dragHasKey(key) {
        for (let i = 0; i < root.dragSlots.length; ++i) if (root.dragSlots[i] === key) return true
        return false
    }
    function addDragSlot(key) {
        if (root.dragHasKey(key)) return
        root.dragSlots = root.dragSlots.concat([key])
    }
    function readSlot(group, index) {
        if (group === "craft")  return { id: root.craftSlots[index] || 0, count: root.craftCounts[index] || 0 }
        if (group === "main")   return { id: root.mainSlots[index] || 0,  count: root.mainCounts[index] || 0 }
        if (group === "hotbar") return { id: root.hotbar.blockIdAt(index), count: root.hotbar.countAt(index) }
        return { id: 0, count: 0 }
    }
    function writeSlot(group, index, id, count) {
        if (group === "craft")       { root.craftSlots[index] = id; root.craftCounts[index] = count; root.craftRev++ }
        else if (group === "main")   { root.mainSlots[index] = id;  root.mainCounts[index] = count;  root.mainRev++ }
        else if (group === "hotbar") { root.hotbar.setStack(index, id, count) }
    }
    function beginRightDrag() {
        root.dragHeldId = root.hotbar.heldBlock
        root.dragHeldCount = root.hotbar.heldCount
        root.dragSlots = []
        root.rightDragActive = true
        if (root.hoveredKey !== "") root.addDragSlot(root.hoveredKey)
    }
    function endRightDrag() {
        if (!root.rightDragActive) return
        root.rightDragActive = false
        const n = root.dragSlots.length
        if (n > 1) root.applyDragDistribute()
        else if (n === 1) {
            const p = root.dragSlots[0].split(":")
            root.singleRightClick(p[0], parseInt(p[1], 10))
        }
        root.dragSlots = []
    }
    // t90 右键拖拽均分修复（t82 回归）：**仅遍历真正滑过的 dragSlots（hover 过的 N 格）**，单次
    // floor(total/N) 等分入格、余数留手；**删 t82 的自动扫 main + hotbar + 删 while +1 循环**（那个版本会
    // 把未拖到的背包格也填满 → 刷物品）。异物 / 已满槽跳过（MC 行为：不同物品的槽不分）；craft 合成格
    // 排除（避免改写合成输入、干扰 recipeMatch）。守恒：写回总量 + 余数 = 拖拽前手持总量。与 Inventory /
    // SurvivalInventory 同算法。
    function applyDragDistribute() {
        const heldId = root.dragHeldId
        const total = root.dragHeldCount
        if (heldId === 0 || total <= 0) return
        const cap = root.hotbar.maxStackSize(heldId)

        // 仅遍历 dragSlots（真正滑过的格），去重 + 过滤合格（空 或 同 id 未满）。不自动扫 main/hotbar。
        const eligible = []
        const seen = {}
        for (let i = 0; i < root.dragSlots.length; ++i) {
            const key = root.dragSlots[i]
            if (seen[key]) continue
            seen[key] = true
            const p = key.split(":")
            if (p[0] === "craft") continue                              // 合成格排除（避免影响 recipeMatch）
            const cur = root.readSlot(p[0], parseInt(p[1], 10))
            if (cur.id === 0 || (cur.id === heldId && cur.count < cap))
                eligible.push({ group: p[0], index: parseInt(p[1], 10), count: cur.count })
        }
        const n = eligible.length
        if (n <= 0) return

        // 单次 floor(total/N) 入格（cap 钳制防溢出），余数留手；不循环、不扫全背包。
        const per = Math.floor(total / n)
        if (per <= 0) return
        let remaining = total
        for (let i = 0; i < n; ++i) {
            const e = eligible[i]
            const place = Math.min(per, cap - e.count)
            e.count += place
            remaining -= place
            root.writeSlot(e.group, e.index, heldId, e.count)
        }
        root.hotbar.heldBlock = remaining > 0 ? heldId : 0
        root.hotbar.heldCount = remaining
    }
    function singleRightClick(group, index) {
        const cur = root.readSlot(group, index)
        const r = root.resolveRightClick(cur.id, cur.count)
        if (!r) return
        root.writeSlot(group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }

    // 取当前合成格的 id 数组（触碰 craftRev 让 QML 绑定刷新时重算）。
    function craftIdArray() {
        root.craftRev
        return root.craftSlots
    }

    // 查匹配配方（走 C++ RecipeRegistry::match；3×3 输入）。返回 recipe 或 null。
    // 经 hotbar.recipeMatch 透传（Hotbar VM 桥接 C++ 静态类给 QML）。
    function matchedRecipe() {
        if (!root.hotbar) return null
        root.craftRev
        return root.hotbar.recipeMatch(root.craftSlots, 3)
    }

    // 关包归还合成栏（spec 同 SurvivalInventory）：visible→false 时把 craft3 内容 addStack 回 hotbar。
    function returnCraftToHotbar() {
        if (!root.hotbar) return
        for (let i = 0; i < root.craftSlots.length; ++i) {
            const id = root.craftSlots[i] || 0
            const n = root.craftCounts[i] || 0
            if (id !== 0 && n > 0) root.hotbar.addStack(id, n)
        }
        for (let i = 0; i < root.craftSlots.length; ++i) {
            root.craftSlots[i] = 0
            root.craftCounts[i] = 0
        }
        root.craftRev++
    }

    onVisibleChanged: if (!visible) returnCraftToHotbar()

    // t79 右键拖拽均分总控（WithinBounds 让 pressed 跨格保持 true；右键单格 / 多格均由此驱动）。
    // 左键不受影响（各槽左键 TapHandler 独立）；遮罩 MouseArea 仅接 LeftButton，右键透传到本 TapHandler。
    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.WithinBounds
        onPressedChanged: {
            if (pressed) root.beginRightDrag()
            else root.endRightDrag()
        }
    }

    // 半透明遮罩：仅吸收点击（防穿透），不关闭面板（E / Esc / closed 信号才关）。
    // 手持物时点遮罩区 → 丢弃为实体（同 SurvivalInventory）。
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

    // 面板：深色圆角，居中。t63 宽度容纳 3×9 主栏（9×40=360 + 2×16 边距 = 392）；高度 = 标题 + 3×3 合成 +
    // 3×9 主栏 + 9 hotbar 行 + 间距/边距（t77 删去提示行，高度随减 28）。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 372                                  // 标题(22) + 合成(120) + 主栏(120) + hotbar(40) + 间距/边距
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
                    text: "工作台"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 合成区：左 3×3 格 + 箭头 + 右结果槽。t77：合成行整体水平居中——本 Item 是 Column 子项，
            // 不能用 anchors（Qt Column positioner 禁止子项 anchors，见 Qt 文档），故算出左偏移 rowOffsetX
            // 加到 3 个绝对定位子元素的 x 上：3×3(120) + 间隙(28) + 结果槽(40) = 188 宽，在 360 行宽内居中，
            // 消除原左对齐（x=0..188）导致面板右侧大段空白的问题。
            Item {
                id: craftRow
                width: parent.width
                height: root.gridN * root.slotSize
                // 合成行整体宽（gridN*slotSize + 28 + slotSize = 188）居中所需的左偏移。
                readonly property real rowOffsetX: (width - (root.gridN * root.slotSize + 28 + root.slotSize)) / 2

                // 3×3 合成格。
                Grid {
                    id: craftGrid
                    x: craftRow.rowOffsetX; y: 0
                    columns: root.gridN; spacing: 0
                    Repeater {
                        model: root.gridN * root.gridN  // 9
                        delegate: Item {
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                            // 物品图标：方块段→等距立方体 Image；工具段→ToolIcon；材料段（木棒）→MaterialIcon 自绘。
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: { root.craftRev; return (root.craftSlots[index] || 0) !== 0 }
                                Image {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return !root.hotbar.isTool(root.craftSlots[index] || 0)
                                                              && !root.hotbar.isMaterial(root.craftSlots[index] || 0) }
                                    source: { root.craftRev; return root.hotbar.iconSourceForBlock(root.craftSlots[index] || 0) }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return root.hotbar.isTool(root.craftSlots[index] || 0) }
                                    tier: { root.craftRev; return root.hotbar.toolTier(root.craftSlots[index] || 0) }
                                }
                                // 材料段（木棒）：MaterialIcon 自绘（§9a 原创，非 MC 资产）。
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return root.hotbar.isMaterial(root.craftSlots[index] || 0) }
                                    materialId: { root.craftRev; return root.craftSlots[index] || 0 }
                                }
                            }
                            // 栈数量（count>1 显数字）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { root.craftRev; return (root.craftCounts[index] || 0) > 1 }
                                text: { root.craftRev; return root.craftCounts[index] || 0 }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // 左键整组（resolveClick）。右键改由 root TapHandler 统一处理（t49 单格 + t79 均分）。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    const r = root.resolveClick(root.craftSlots[index] || 0, root.craftCounts[index] || 0)
                                    if (!r) return
                                    root.craftSlots[index] = r.slotId
                                    root.craftCounts[index] = r.slotCount
                                    root.craftRev++
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                }
                            }
                            HoverHandler {
                                onHoveredChanged: {
                                    // t94 tooltip（craftSlots[index] 取当前栈 id；空栈不动 hoveredItemId）。
                                    const itemId = root.craftSlots[index] || 0
                                    if (hovered && itemId !== 0) {
                                        root.hoveredItemId = itemId
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                    } else if (root.hoveredItemId === itemId) {
                                        root.hoveredItemId = 0
                                    }
                                    const key = root.slotKey("craft", index)
                                    if (hovered) root.hoveredKey = key
                                    else if (root.hoveredKey === key) root.hoveredKey = ""
                                    if (hovered && root.rightDragActive) root.addDragSlot(key)
                                }
                            }
                            // t79 均分拖拽高亮。
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: "#7fe57f"; border.width: 2
                                visible: { root.dragSlots; root.rightDragActive; return root.rightDragActive && root.dragHasKey(root.slotKey("craft", index)) }
                                z: 10
                            }
                        }
                    }
                }

                // 合成箭头（指向结果槽）：自绘像素图（§9a）。
                Canvas {
                    x: craftRow.rowOffsetX + root.gridN * root.slotSize + 2; y: root.slotSize + 10
                    width: 24; height: 20
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        ctx.imageSmoothingEnabled = false
                        ctx.fillStyle = "#8a8a8a"
                        ctx.fillRect(0, 8, 16, 4)
                        ctx.beginPath()
                        ctx.moveTo(16, 2); ctx.lineTo(24, 10); ctx.lineTo(16, 18); ctx.closePath()
                        ctx.fill()
                    }
                }

                // 结果槽（最右）：显匹配配方产物图标；点击 → 合成一批（消耗每原料 1、产出 outputCount 到光标）。
                Item {
                    x: craftRow.rowOffsetX + root.gridN * root.slotSize + 28; y: root.slotSize
                    width: root.slotSize; height: root.slotSize
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }

                    // 产物图标（触碰 craftRev 重算 matchedRecipe）。
                    Item {
                        anchors.centerIn: parent
                        width: 30; height: 30
                        // 产物 id / 数量（0 = 无匹配 → 隐藏）。matchedRecipe 返回 null 或 QVariantMap；
                        // 用 (r && r.field) || 0 防御 undefined（无匹配 / hotbar 未注入时回 0，免 QML 警告）。
                        property int outId: { root.craftRev; const r = root.matchedRecipe(); return (r && r.outputId) || 0 }
                        property int outCount: { root.craftRev; const r = root.matchedRecipe(); return (r && r.outputCount) || 0 }
                        visible: outId !== 0
                        // t94 tooltip：仅在有产物时（visible）悬停显产物名。parent = 本 30×30 图标 Item。
                        HoverHandler {
                            onHoveredChanged: {
                                if (hovered && parent.outId !== 0) {
                                    root.hoveredItemId = parent.outId
                                    const p = parent.mapToItem(root, parent.width / 2, 0)
                                    root.hoveredTipPos = Qt.point(p.x, p.y)
                                } else if (root.hoveredItemId === parent.outId) {
                                    root.hoveredItemId = 0
                                }
                            }
                        }
                        Image {
                            anchors.fill: parent
                            visible: parent.outId !== 0 && !root.hotbar.isTool(parent.outId) && !root.hotbar.isMaterial(parent.outId)
                            source: parent.outId !== 0 ? root.hotbar.iconSourceForBlock(parent.outId) : ""
                            fillMode: Image.PreserveAspectFit; smooth: true
                        }
                        ToolIcon {
                            anchors.fill: parent
                            visible: parent.outId !== 0 && root.hotbar.isTool(parent.outId)
                            tier: root.hotbar.toolTier(parent.outId)
                        }
                        MaterialIcon {
                            anchors.fill: parent
                            visible: parent.outId !== 0 && root.hotbar.isMaterial(parent.outId)
                            materialId: parent.outId
                        }
                        // 产物数量（>1 显数字）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: parent.outCount > 1
                            text: parent.outCount
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                    }

                    // 点击结果槽 → 合成（MC 式：消耗每原料 1、产出 outputCount 到光标；剩余留槽可连点）。
                    // 前置：matchedRecipe 非 null；光标能容纳产物（空 / 同 id 且累加不超 maxStack）。
                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onTapped: {
                            if (!root.hotbar) return
                            root.craftRev  // 触碰最新态
                            const r = root.matchedRecipe()
                            if (!r) return
                            const heldId = root.hotbar.heldBlock
                            const heldCount = root.hotbar.heldCount
                            const cap = root.hotbar.maxStackSize(r.outputId)
                            if (!root.hotbar.recipeCanTake(r.outputId, r.outputCount, heldId, heldCount, cap)) return
                            // 消耗：每个非空原料格 count-1（归 0 清 id）。原料用量恒 1（consumeCount=1）。
                            for (let i = 0; i < root.craftSlots.length; ++i) {
                                if ((root.craftSlots[i] || 0) !== 0) {
                                    root.craftCounts[i] = (root.craftCounts[i] || 0) - 1
                                    if ((root.craftCounts[i] || 0) <= 0) {
                                        root.craftSlots[i] = 0
                                        root.craftCounts[i] = 0
                                    }
                                }
                            }
                            // 产出：加 outputCount 到光标。
                            root.hotbar.heldBlock = r.outputId
                            root.hotbar.heldCount = (heldId === r.outputId ? heldCount : 0) + r.outputCount
                            root.craftRev++
                        }
                    }
                }
            }

            // t63 3×9 主物品栏（27 槽）：左键整组 / 右键半份取放（与 SurvivalInventory 主栏同模式）。
            // 本地 mainSlots/mainCounts/mainRev 存储；与合成格 / hotbar 共享同一 hotbar VM 光标手持栈。
            // 数组突变靠 mainRev 触发各绑定重算（图标 / 数量）。物品可在 主栏 ↔ 合成格 ↔ hotbar 间任意搬动。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    model: root.mainCols * root.mainRows   // 27
                    delegate: Item {
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent }
                        Item {
                            anchors.centerIn: parent
                            width: 30; height: 30
                            visible: { root.mainRev; return (root.mainSlots[index] || 0) !== 0 }
                            Image {
                                anchors.fill: parent
                                visible: { root.mainRev; return !root.hotbar.isTool(root.mainSlots[index] || 0)
                                                          && !root.hotbar.isMaterial(root.mainSlots[index] || 0) }
                                source: { root.mainRev; return root.hotbar.iconSourceForBlock(root.mainSlots[index] || 0) }
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            ToolIcon {
                                anchors.fill: parent
                                visible: { root.mainRev; return root.hotbar.isTool(root.mainSlots[index] || 0) }
                                tier: { root.mainRev; return root.hotbar.toolTier(root.mainSlots[index] || 0) }
                            }
                            MaterialIcon {
                                anchors.fill: parent
                                visible: { root.mainRev; return root.hotbar.isMaterial(root.mainSlots[index] || 0) }
                                materialId: { root.mainRev; return root.mainSlots[index] || 0 }
                            }
                        }
                        // 栈数量（count>1 显数字）。触碰 mainRev 刷新。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { root.mainRev; return (root.mainCounts[index] || 0) > 1 }
                            text: { root.mainRev; return root.mainCounts[index] || 0 }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        // 左键整组（resolveClick）。右键改由 root TapHandler 统一处理（t49 单格 + t79 均分）。
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                const r = root.resolveClick(root.mainSlots[index] || 0, root.mainCounts[index] || 0)
                                if (!r) return
                                root.mainSlots[index] = r.slotId
                                root.mainCounts[index] = r.slotCount
                                root.mainRev++
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
                        }
                        HoverHandler {
                            onHoveredChanged: {
                                // t94 tooltip（mainSlots[index] 取当前栈 id）。
                                const itemId = root.mainSlots[index] || 0
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
                                if (hovered && root.rightDragActive) root.addDragSlot(key)
                            }
                        }
                        // t79 均分拖拽高亮。
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: "#7fe57f"; border.width: 2
                            visible: { root.dragSlots; root.rightDragActive; return root.rightDragActive && root.dragHasKey(root.slotKey("main", index)) }
                            z: 10
                        }
                    }
                }
            }

            // t63 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            // 属性触碰 slotRevision（t55/t63 已验证写法，**不**用 slotList() 等长数组以免 Repeater 不重建）。
            // 左键整组 / 右键半份同主栏（hotbar 槽写经 hotbar.setStack；VM 单一权威）。不切真实选中（同 SurvivalInventory）。
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
                                    const r = root.resolveClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index))
                                    if (r) {
                                        root.hotbar.setStack(index, r.slotId, r.slotCount)
                                        root.hotbar.heldBlock = r.heldId
                                        root.hotbar.heldCount = r.heldCount
                                    }
                                }
                            }
                            // 右键改由 root TapHandler 统一处理（t49 单格 + t79 均分）；此槽仅留 HoverHandler。
                            HoverHandler {
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
                                    if (hovered && root.rightDragActive) root.addDragSlot(key)
                                }
                            }
                            // t79 均分拖拽高亮。
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: "#7fe57f"; border.width: 2
                                visible: { root.dragSlots; root.rightDragActive; return root.rightDragActive && root.dragHasKey(root.slotKey("hotbar", index)) }
                                z: 10
                            }
                        }
                    }
                }
            }
        }
    }

    // t94 物品名悬停 tooltip（纯 QtQuick 自绘；不引入 QtQuick.Controls —— 项目未链接 Qt6::QuickControls2，
    // 顶层 import 新模块有「未部署→整文档加载失败」风险，见 lessons-learned）。各槽 HoverHandler 进入时写
    // hoveredItemId + hoveredTipPos（槽顶中心在 root 坐标系下）；离开按 id 守卫清除（防相邻槽进出竞态互清）。
    // 名字走 hotbar.nameForBlock：方块→BlockRegistry::displayName、工具→ToolRegistry::displayName、
    // 材料段→本地通用名；air/空槽→空串→不显。工具后续将加「+攻击力」等字段，现阶段只名字。
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
