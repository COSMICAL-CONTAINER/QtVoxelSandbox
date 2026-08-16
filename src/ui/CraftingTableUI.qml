import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 craft 本地槽路由 + 薄委托包装（供 QML 信号处理器
//   经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，消除四面板逐字复制。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

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
    // progress 玩家进度注入（Main.qml 经 `progress: progress` 绑定）：批量合成（InventoryOps
    //   slotShiftLeftCraft）统计 / 成就埋点用。InventoryOps .js 无 QML 全局 id 访问权 → 经 root 传，同 hotbar 模式。
    property var progress
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49 同 SurvivalInventory：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区）。
    signal discardHeldRequested()
    // t228：请求宿主把光标手持栈**丢 1 件**为实体（右键拖出面板外；宿主接 player.dropHeldCursorOne）。
    //   左键整栈走 discardHeldRequested，右键逐个走本信号（spec「左键=全丢/右键=逐个」）。
    signal discardHeldOneRequested()

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

    // t97：27 主物品栏自本任务起上移至 hotbar VM（m_mainSlots），与 SurvivalInventory / FurnaceUI 三菜单共享
    // 同一份 → 三菜单主栏同步、returnHeldToHotbar/pickupScan 经 addToAny 能合并进主栏（修「主栏不同步 /
    // 丢弃回栏不合并」根因）。原本地 mainSlots/mainCounts/mainRev 数组删除，delegate 改读 VM（触碰
    // mainRevision + mainBlockIdAt/mainCountAt，同 SurvivalInventory 主栏模式）。与合成格 / hotbar 共享同一
    // hotbar VM 光标手持栈；左键整组 / 右键半份同 resolveClick / resolveRightClick。

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    // DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（单点拾取/放置/合并/互换），一旦
    // 拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler 在
    // leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」
    // 字符串（去重简单）；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制
    // （每滑入新格先撤销上轮写入再重分）。合成格 / 主栏 / hotbar 统一支持——t180 起 craft 经 localDragGroups
    //   声明为可拖拽组，参与收集 + 分发（旧版仅收集不分发）；右键分半走 per-slot 右键 TapHandler。
    // 均分算法与 t79/t98 右键拖拽同源（右键拖拽 t166d 改 per-slot 单点后停用）；t167 把同一算法接到左键。
    property bool leftDragActive: false
    property var dragSlots: []              // "craft:2" / "main:5" / "hotbar:0"
    property string hoveredKey: ""
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
    // doMergeSameId（扫 main+hotbar+craft 同 id 累加成满栈、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t180：craft 3×3 参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明 craft 为可拖拽本地组 →
    //   InventoryOps.groupIsDraggable 放行（addDragSlot 收集、redistributeLive 分发）、doMergeSameId 扫 craft
    //   槽。旧版 hardcode 排除 craft（避免改写合成输入），t180 改为按面板声明接入：双击/拖拽填合成格是 MC
    //   标准交互；recipeMatch 据 craftRev 自动重算，合并后布局若变由用户重排。
    property var localDragGroups: ["craft"]
    // t180：craft 组槽位数（doMergeSameId 扫描范围）。craftSlots 长 9（3×3）。
    function localSlotCount(group) { return group === "craft" ? root.craftSlots.length : 0 }

    // ── t168 面板专属槽路由：craft 合成格走本地数组 + 版本号（main/hotbar 由 InventoryOps 统一经 VM）。
    //   readSlot/writeSlot 薄包装委托 InventoryOps（含本地组分发 → 调本处 localReadSlot/localWriteSlot）。
    function localReadSlot(group, index) {
        if (group === "craft") return { id: root.craftSlots[index] || 0, count: root.craftCounts[index] || 0 }
        return { id: 0, count: 0 }
    }
    function localWriteSlot(group, index, id, count) {
        if (group === "craft") { root.craftSlots[index] = id; root.craftCounts[index] = count; root.craftRev++ }
    }
    // resolveClick / resolveRightClick（拾取/放置/合并/互换 + 半份）：算法见 InventoryOps（四面板共享）。
    //   返回 {slotId,slotCount,heldId,heldCount} 或 null=无操作；调用方据返回值写对应槽 + 更新 held。
    //   t622：+ curName 第 5 参（main/hotbar 槽实例名透传；craft 本地槽不持名——合成原料无名语义）。
    function resolveClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch, curName) }
    function resolveRightClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch, curName) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count, durability, enchants, name) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants, name) }

    // ── t79/t98/t108/t167 拖动均分 + t110 Shift/数字键搬运 + t98 双击合并：算法见 InventoryOps
    //   （四面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
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
    // redistributeLive / singleLeftClick：纯内部辅助（仅 InventoryOps.addDragSlot / endLeftDrag 调用），
    //   算法已入 InventoryOps，此处不再持有副本。
    function slotShiftLeft(group, index) { InventoryOps.slotShiftLeft(root, group, index) }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }
    // t230 工作台 3×3 Shift+左键结果槽 → 批量合成（耗尽最小原料数；产物入背包非光标）。
    function slotShiftLeftCraft() { InventoryOps.slotShiftLeftCraft(root) }
    // t230 单次合成（结果槽普通左/右键）：消耗每非空原料 1、产出 outputCount 到光标。原 inline 在结果槽
    //   TapHandler 内；抽出为函数供左/右 TapHandler 共用，左键先判 shift 走批量、否则走单次。
    function craftOneResult() {
        if (!root.hotbar) return
        root.craftRev
        const r = root.matchedRecipe()
        if (!r) return
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        const cap = root.hotbar.maxStackSize(r.outputId)
        if (!root.hotbar.recipeCanTake(r.outputId, r.outputCount, heldId, heldCount, cap)) return
        for (let i = 0; i < root.craftSlots.length; ++i) {
            if ((root.craftSlots[i] || 0) !== 0) {
                root.craftCounts[i] = (root.craftCounts[i] || 0) - 1
                if ((root.craftCounts[i] || 0) <= 0) {
                    root.craftSlots[i] = 0
                    root.craftCounts[i] = 0
                }
            }
        }
        root.hotbar.heldBlock = r.outputId
        root.hotbar.heldCount = (heldId === r.outputId ? heldCount : 0) + r.outputCount
        root.craftRev++
        // t603 修：旧代码 `window.progress` —— Main.qml 的 Window **从未声明 progress 属性**（progress 是
        //   Main.qml 内 id，非 window 属性）→ 恒 undefined → 单次合成的统计 / 成就上报从未发出（用户
        //   「合成工作台成就触发不了 + 统计合成次数恒 0」）。改用注入的 root.progress（Main.qml
        //   `progress: progress` 绑定，同 InventoryOps 批量路径 root.progress 模式）。
        if (root.progress) root.progress.onCraft(r.outputId)  // progress 统计合成 + 成就
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

    // t167 左键拖动均分总控：DragHandler(LeftButton) 在 root 监听。按下不动时 per-slot 左键 TapHandler 抓
    //   （单点拾取/放置/合并/互换 / Shift 搬运 / 双击合并）；拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged
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
    //   按下不动时 per-slot 右键 TapHandler 抓（拿半 / 放一）。target:null 防 DragHandler 拖动父 Item。
    DragHandler {
        acceptedButtons: Qt.RightButton
        target: null
        onActiveChanged: {
            if (active) root.beginRightDrag()
            else root.endRightDrag()
        }
    }

    // 半透明遮罩：仅吸收点击（防穿透），不关闭面板（E / Esc / closed 信号才关）。
    // 手持物时点遮罩区 → 丢弃为实体（同 SurvivalInventory）。t228：左键整栈 / 右键 1 件 + 面板边界判定
    //   （面板内非槽位松手→不丢，修「左键拿物在面板内非槽位松手→直接丢地下」bug）。
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
                                visible: { const _r = root.craftRev; return _r >= 0 ? ((root.craftSlots[index] || 0) !== 0) : false }
                                Image {
                                    anchors.fill: parent
                                    visible: {
                                        const _r = root.craftRev
                                        return _r >= 0 ? (!root.hotbar.isTool(root.craftSlots[index] || 0)
                                                          && !root.hotbar.isMaterial(root.craftSlots[index] || 0)) : false
                                    }
                                    source: { const _r = root.craftRev; return _r >= 0 ? (root.hotbar.iconSourceForBlock(root.craftSlots[index] || 0)) : "" }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.craftRev; return _r >= 0 ? (root.hotbar.isTool(root.craftSlots[index] || 0)) : false }
                                    tier: { const _r = root.craftRev; return _r >= 0 ? (root.hotbar.toolTier(root.craftSlots[index] || 0)) : 0 }
                                    toolType: { const _r = root.craftRev; return _r >= 0 ? (root.hotbar.toolType(root.craftSlots[index] || 0)) : 0 }
                                }
                                // 材料段（木棒）：MaterialIcon 自绘（§9a 原创，非 MC 资产）。
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.craftRev; return _r >= 0 ? (root.hotbar.isMaterial(root.craftSlots[index] || 0)) : false }
                                    materialId: { const _r = root.craftRev; return _r >= 0 ? (root.craftSlots[index] || 0) : 0 }
                                }
                            }
                            // 栈数量（count>1 显数字）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { const _r = root.craftRev; return _r >= 0 ? ((root.craftCounts[index] || 0) > 1) : false }
                                text: { const _r = root.craftRev; return _r >= 0 ? (root.craftCounts[index] || 0) : "" }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // 左键整组（resolveClick）；右键走 per-slot 右键 TapHandler（resolveRightClick）。
                            // 左键拖动均分由 root DragHandler + 逐槽 HoverHandler 收集（t167）；合成格仅收集不分发。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    // t110：Shift+左键搬运（craft 槽不在 main↔hotbar 范畴，slotShiftLeft 对 craft
                                    //   组无操作；普通左键走 resolveClick）。
                                    if (window.shiftHeld) { root.slotShiftLeft("craft", index); return }
                                    // t180：双击 craft 槽 → 拿同类（doMergeSameId 扫 main+hotbar+craft 同 id 累加成
                                    //   满栈、余数留光标；满栈按 main→hotbar→craft 顺序回填，故常把 craft 物品
                                    //   并入背包、清空合成格便于重排）。
                                    const key = root.slotKey("craft", index)
                                    const now = Date.now()
                                    const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                    root.lastTapMs = now
                                    root.lastTapKey = key
                                    if (isDouble) { root.doMergeSameId("craft", index); return }
                                    const r = root.resolveClick(root.craftSlots[index] || 0, root.craftCounts[index] || 0, 0)
                                    if (!r) return
                                    root.craftSlots[index] = r.slotId
                                    root.craftCounts[index] = r.slotCount
                                    root.craftRev++
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                }
                            }
                            // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const r = root.resolveRightClick(root.craftSlots[index] || 0, root.craftCounts[index] || 0, 0)
                                    if (!r) return
                                    root.craftSlots[index] = r.slotId
                                    root.craftCounts[index] = r.slotCount
                                    root.craftRev++
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                }
                            }
                            HoverHandler {
                                // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                                // 不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                                property int trackedId: { const _r = root.craftRev; return _r >= 0 ? (root.craftSlots[index] || 0) : 0 }
                                onTrackedIdChanged: {
                                    if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                        root.hoveredItemId = 0
                                }
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
                                    // qml-touch 三轮：dragSlots/rightDragSlots/revision 触碰入 _ok 守卫（恒真），
                                    //   防 AOT 死代码消除裸触碰 → 高亮不随拖拽集 / 版本号刷新。
                                    const _ds = root.dragSlots
                                    const _rds = root.rightDragSlots
                                    const _rev = root.craftRev
                                    const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                                    const sid = root.craftSlots[index] || 0
                                    const key = root.slotKey("craft", index)
                                    if (_ok && root.leftDragActive && root.dragHasKey(key)
                                        && (sid === 0 || sid === root.dragHeldId)) return true
                                    return _ok && root.rightDragActive && root.rightDragHasKey(key)
                                }
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
                        property int outId: { const _r = root.craftRev; const r = root.matchedRecipe(); return _r >= 0 ? ((r && r.outputId) || 0) : 0 }
                        property int outCount: { const _r = root.craftRev; const r = root.matchedRecipe(); return _r >= 0 ? ((r && r.outputCount) || 0) : 0 }
                        visible: outId !== 0
                        // t94 tooltip：仅在有产物时（visible）悬停显产物名。parent = 本 30×30 图标 Item。
                        HoverHandler {
                            // t99：跟踪槽显示 id（产物）。槽变空时 hover 仍 true → onHoveredChanged 不重发 →
                            // tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                            property int trackedId: parent.outId
                            onTrackedIdChanged: {
                                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                    root.hoveredItemId = 0
                            }
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
                            toolType: root.hotbar.toolType(parent.outId)
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

                    // 点击结果槽 → 合成。t230：左键 Shift → 批量合成（耗尽最小原料数；产物入背包），
                    //   左键非 Shift / 右键 → 单次合成（消耗每原料 1、产出 outputCount 到光标；剩余留槽可连点）。
                    //   MC 1.0：Shift+左键结果槽才批量；Shift+右键 / 右键均单次。前置：matchedRecipe 非 null；
                    //   单次还需光标能容纳产物（空 / 同 id 且累加不超 maxStack）。
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: {
                            if (window.shiftHeld) { root.slotShiftLeftCraft(); return }
                            root.craftOneResult()
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: root.craftOneResult()
                    }
                }
            }

            // t63 / t97 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，三菜单共享）；左键整组 / 右键半份
            // 取放（与 SurvivalInventory 主栏同模式）。主栏栈写经 hotbar.mainSetStack；与合成格 / hotbar 共享
            // 同一 hotbar VM 光标手持栈。物品可在 主栏 ↔ 合成格 ↔ hotbar 间任意搬动。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    // model 用固定整数 mainCount（VM CONSTANT=27）；刷新靠每绑定触碰 mainRevision
                    // （Q_PROPERTY，NOTIFY=mainSlotsChanged）→ 经 mainBlockIdAt/mainCountAt 取最新栈值
                    // （同 hotbar 行 slotRevision 模式，t55/t63 已验证）。
                    model: root.hotbar.mainCount
                    delegate: Item {
                        property int mainId: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainBlockIdAt(index)) : 0 }
                        property int mainCount: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainCountAt(index)) : 0 }
                        property int mainDur: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainDurabilityAt(index)) : 0 } // t263 工具耐久
                        property var mainEnch: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainEnchantsAt(index)) : 0 } // t475 附魔
                        property string mainName: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainCustomNameAt(index)) : "" } // t622 实例名
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
                        // 左键整组（resolveClick）；右键走 per-slot 右键 TapHandler（resolveRightClick）。
                        // 左键拖动均分由 root DragHandler + 逐槽 HoverHandler 收集（t167）。
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                // t110：Shift+左键 → main 槽搬运到首个空 hotbar 槽（早于双击合并 / 普通左键）。
                                if (window.shiftHeld) { root.slotShiftLeft("main", index); return }
                                // t98 双击合并：400ms 内同槽二次点击 → doMergeSameId（拾起 + 合并）。
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
                        // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
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
                            }
                        }
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
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    // t110：Shift+左键 → hotbar 槽搬运到首个空 main 槽（早于双击合并 / 普通左键）。
                                    if (window.shiftHeld) { root.slotShiftLeft("hotbar", index); return }
                                    // t98 双击合并：400ms 内同槽二次点击 → doMergeSameId。
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
                            // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
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
    // t622 当前 hover 槽物品的实例名（铁砧改名；空串 → tooltip 走注册默认名）。据 hoveredKey 查 hotbar/main；
    //   触碰 revision → 改名 / 搬运后刷新（同 hoveredDurability 模式）。
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
            // t622：hover 槽物品带实例名 → 优先显实例名（hoveredCustomName——改名物品显其名）。
            text: root.hotbar ? ((root.hoveredCustomName.length > 0 ? root.hoveredCustomName
                    : root.hotbar.nameForBlock(root.hoveredItemId))
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")
                + (root.hotbar.toolType(root.hoveredItemId) === 7 ? "  攻击 1-" + root.hotbar.bowArrowMaxDamage() : "")) : "" // t304 弓伤害 tooltip
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
