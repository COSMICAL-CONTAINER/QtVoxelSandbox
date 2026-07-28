import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

// 工作台 3×3 合成面板（t50）：右键工作台方块打开（PlayerController::craftingTableOpened →
// Main.qml Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// 布局贴近 MC 1.0 工作台：左 3×3 合成格 + 箭头 + 右结果槽。合成检测走 C++ RecipeRegistry
// （Game 层单一权威；本组件只读查 match）。**MC 式数量**：点结果槽 → 取 outputCount 件到光标；
// 输入槽每原料格 consume 1 份（count-1，归 0 清 id，不清空整槽 → 可连点合多批）。
//
// 物品移动（合成格 ↔ 光标手持栈）：左键整组 / 右键半份 / 单放，与 SurvivalInventory.qml 的
// resolveClick / resolveRightClick 同算法（共享 hotbar VM 的 heldBlock / heldCount 光标手持栈）。
//
// 关包（visible→false）：把 craft3 内容 addStack 回 hotbar（合并同类，同拾取），清空 craft3
// （MC 行为：合成格不持久化，关包即退回玩家背包）。
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

    // 3×3 合成格本地栈存储（与 hotbar VM 共享同一光标手持栈 heldBlock/heldCount）。
    // 数组改写不触发 QML 绑定 → 配 craftRev 版本号让 Image source / count / 结果槽重算。
    property var craftSlots: [0,0,0, 0,0,0, 0,0,0]
    property var craftCounts:[0,0,0, 0,0,0, 0,0,0]
    property int craftRev: 0

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

    // 面板：深色圆角，居中。
    Rectangle {
        id: panel
        width: root.gridN * root.slotSize + 24 + root.slotSize + 32  // 3×3(120) + 箭头区(24) + 结果(40) + 边距(32) = 216
        height: root.gridN * root.slotSize + 80                       // 120 + 标题/边距
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

            // 合成区：左 3×3 格 + 箭头 + 右结果槽。
            Item {
                width: parent.width
                height: root.gridN * root.slotSize

                // 3×3 合成格。
                Grid {
                    id: craftGrid
                    x: 0; y: 0
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
                            // 左键整组 / 右键半份（与 SurvivalInventory 一致）。
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
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const r = root.resolveRightClick(root.craftSlots[index] || 0, root.craftCounts[index] || 0)
                                    if (!r) return
                                    root.craftSlots[index] = r.slotId
                                    root.craftCounts[index] = r.slotCount
                                    root.craftRev++
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                }
                            }
                        }
                    }
                }

                // 合成箭头（指向结果槽）：自绘像素图（§9a）。
                Canvas {
                    x: root.gridN * root.slotSize + 2; y: root.slotSize + 10
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
                    x: root.gridN * root.slotSize + 28; y: root.slotSize
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

            // 提示行。
            Text {
                width: parent.width
                color: "#7d8893"; font.pixelSize: 11
                text: "3×3 合成：放入原料 → 取产物。木镐 = 顶行 3 木板 + 中列 2 木棒"
            }
        }
    }
}
