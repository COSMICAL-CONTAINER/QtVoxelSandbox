import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 in/fuel/out 本地槽路由 + slotLeft/slotRight 面板
//   dispatch + 薄委托包装（供 QML 信号处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，
//   消除四面板逐字复制。
import "InventoryOps.js" as InventoryOps

// 熔炉冶炼面板（t87）：右键熔炉方块打开（PlayerController::furnaceOpened → Main.qml Connections → 显本
// 面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// 布局贴近 MC 1.0 熔炉：上部「左输入槽 + 火焰（下接燃料槽）+ 右输出槽」（中箭头显冶炼进度），下部
// 「3×9=27 主物品栏 + 9 hotbar 行」（与工作台 / 生存背包同布局，便于从背包取铁原矿 / 煤放入熔炉）。
//
// 冶炼逻辑（spec「燃料燃烧→累积热量→输入转输出」）：
//   - WorldClock.ticked（10Hz，每 tick 0.1s）驱动本面板 tick(dt)；Main.qml Connections 转发。
//   - 燃料未燃 + 输入可冶炼 + 输出有空位 + 燃料槽有燃料 → 点燃 1 件燃料（consume fuel，置 burnRemain）。
//   - 燃料燃烧（burnRemain>0）：burnRemain -= dt；若可冶炼则 smeltProgress += dt。
//   - smeltProgress >= kSmeltSecs(10s) → 消耗 1 输入、产出 1 输出（MC 标准 200 ticks = 10s / 件）。
//   - 配方 / 燃料查 C++ SmeltingRegistry（Game 层单一权威，经 hotbar VM 的 smeltResult / fuelBurnSeconds
//     透传；本组件只读查）。
//
// 物品移动（输入 / 燃料 / 输出 / 主栏 / hotbar 间任意搬动）：左键整组 / 右键半份 / 单放，与
// CraftingTableUI.qml 的 resolveClick / resolveRightClick 同算法（共享 hotbar VM 的 heldBlock /
// heldCount 光标手持栈）。每槽用两个 TapHandler（左 / 右各一）区分按键——比单 TapHandler 读 pressedButtons
// 稳健（tapped 信号在 release 时触发，pressedButtons 语义有歧义；两 handler 各 acceptedButtons 单按钮无歧义）。
//
// 状态持久：面板常驻（visible 切换、不销毁）→ 输入 / 燃料 / 输出 / 冶炼进度 / 主栏内容跨开关持久
// （机制等价 MC「熔炉内容存于方块」；多熔炉 / 真存档属 Phase 1.1+）。关包仅归还光标手持栈（宿主
// returnHeldToHotbar），不归还熔炉槽（MC 行为：熔炉槽不退回玩家背包）。
//
// 全部槽框 / 火焰 / 箭头自绘原创（InvSlot 凹陷槽 + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点格子），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（heldBlock/heldCount/maxStackSize/iconSourceForBlock/nameForBlock/
    // isTool/isMaterial/slotRevision/smeltResult/fuelBurnSeconds/setStack 等）。
    property Hotbar hotbar
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // 拖出丢弃：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区；同 CraftingTableUI）。
    signal discardHeldRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int mainCols: 9
    readonly property int mainRows: 3
    // 单次冶炼耗时（秒）。MC 1.0 标准 200 ticks = 10s（与 SmeltingRegistry::kSmeltSecs 同源）。
    readonly property real kSmeltSecs: 10.0

    // ── 熔炉 3 槽本地栈存储（输入 / 燃料 / 输出；跨开关持久）──
    // 数组改写不触发 QML 绑定 → 配 slotRev 版本号让图标 / 数量 / 火焰 / 进度重算。
    property int inId: 0;    property int inCount: 0
    property int fuelId: 0;  property int fuelCount: 0
    property int outId: 0;   property int outCount: 0
    property int slotRev: 0   // 熔炉 3 槽内容版本号（任何槽改写自增 → 绑定刷新）

    // 冶炼运行态：burnRemain = 当前燃料剩余燃烧秒数（>0 表「正在烧」）；smeltProgress = 当前件累积秒数
    // （0..kSmeltSecs；满则产 1 件、归零或留余）。ticked 驱动推进（见 tick()）。
    property real burnRemain: 0.0
    property real smeltProgress: 0.0

    // t97：27 主物品栏自本任务起上移至 hotbar VM（m_mainSlots），与 SurvivalInventory / CraftingTableUI 三
    // 菜单共享同一份 → 三菜单主栏同步、returnHeldToHotbar/pickupScan 经 addToAny 能合并进主栏。原本地
    // mainSlots/mainCounts/mainRev 数组删除，delegate 改读 VM（触碰 mainRevision + mainBlockIdAt/mainCountAt，
    // 同 SurvivalInventory / CraftingTableUI 主栏模式）。与熔炉槽 / hotbar 共享同一 hotbar VM 光标手持栈。

    // 火焰闪烁动画驱动：燃烧时 0..1 循环（NumberAnimation），Canvas 据此调火焰高度 / 宽度 → 视觉跳动。
    property real flameFlicker: 0.0

    // t110：当前指针所在槽的「组:下标」key（供 window.hoveredSlotKey 提升 → 数字键交换 + t167 左键拖动
    //   起点槽）。各槽 HoverHandler onHoveredChanged 维护（进入写、离开按 key 守卫清除，防相邻槽进出竞态
    //   互清）。组名与 readSlot/writeSlot 一致：in / fuel / out / main / hotbar。
    property string hoveredKey: ""

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    //   DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（slotLeft 单点拾取/放置/合并/互换 /
    //   Shift 搬运），拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler
    //   在 leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」
    //   字符串；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制（每滑入新格先
    //   撤销上轮写入再重分）。in / fuel / main / hotbar 参与（t180：in/fuel 经 localDragGroups=["in","fuel"]
    //   声明为可拖拽组；out 输出槽不参与拖拽/合并，防异物污染输出槽阻断冶炼）；与 SurvivalInventory /
    //   CraftingTableUI 同算法；本文件无 craft 合成格。
    property bool leftDragActive: false
    property var dragSlots: []              // "in:0" / "fuel:0" / "out:0" / "main:5" / "hotbar:0"
    property int dragHeldId: 0
    property int dragHeldCount: 0
    // 实时重分撤销机制：dragOriginal 记每槽 drag 前原始栈（首次 encounter 快照）；dragWritten 记本轮已写槽。
    // 每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginLeftDrag / endLeftDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    // t181 右键拖动（每格放 1 个；区别于左键 floor(count/N) 均分）。dragActive 统一左/右拖动收集门控。
    property bool rightDragActive: false
    property var rightDragSlots: []
    property bool rightDragPlaced: false
    property bool dragActive: leftDragActive || rightDragActive

    // t180：双击拿同类时间戳/key（slotLeft 入口判双击；同 CraftingTableUI）。熔炉旧版 slotLeft 无双击路径，
    //   双击任何槽都只走两次单点 resolveClick；现可拖拽组槽支持双击合并。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t180：熔炉 in/fuel 输入槽参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明为可拖拽本地组 →
    //   InventoryOps.groupIsDraggable 放行（addDragSlot 收集、redistributeLive 分发）、doMergeSameId 扫 in/fuel
    //   槽。out 输出槽不声明 → groupIsDraggable 拒收：拖拽不分发进 out（防异物污染输出槽阻断冶炼——
    //   canSmelt 守 outId===0||outId===resultId）、双击合并不扫 out（输出只取不合）；out 高亮也不亮（绑
    //   dragHasKey，非可拖拽组不入 dragSlots）。旧版「out 也参与拖拽分发」的潜在污染 bug 一并消除。
    property var localDragGroups: ["in", "fuel"]
    // t180：本地组槽位数（doMergeSameId 扫描范围）。in/fuel 各 1 槽；out 不在 localDragGroups 故不查。
    function localSlotCount(group) { return (group === "in" || group === "fuel") ? 1 : 0 }

    // ── t168 面板专属槽路由：熔炉 in/fuel/out 走本地属性 + slotRev（不退回背包、跨开关持久，spec t97 保留
    //   本地）；main/hotbar 由 InventoryOps 统一经 VM。readSlot/writeSlot 薄包装委托 InventoryOps（本地组
    //   分发 → 调本处 localReadSlot/localWriteSlot）。
    function localReadSlot(group, index) {
        if (group === "in")    return { id: root.inId,   count: root.inCount }
        if (group === "fuel")  return { id: root.fuelId, count: root.fuelCount }
        if (group === "out")   return { id: root.outId,  count: root.outCount }
        return { id: 0, count: 0 }
    }
    function localWriteSlot(group, index, id, count) {
        if (group === "in")        { root.inId = id;   root.inCount = count;   root.slotRev++ }
        else if (group === "fuel") { root.fuelId = id; root.fuelCount = count; root.slotRev++ }
        else if (group === "out")  { root.outId = id;  root.outCount = count;  root.slotRev++ }
    }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count) { InventoryOps.writeSlot(root, group, index, id, count) }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → InventoryOps.slotShiftLeft（Shift+左键搬运 main↔hotbar；
    //   in/fuel/out 不参与，避免误把冶炼输入搬走）。t180：可拖拽组（main/hotbar/in/fuel）双击 → doMergeSameId
    //   （拿同类）；out 非可拖拽，双击退化为两次单点（拾起+放回，净无操作）。resolveClick/resolveRightClick
    //   算法见 InventoryOps。
    function slotLeft(group, index) {
        if (window.shiftHeld) { InventoryOps.slotShiftLeft(root, group, index); return }
        // t180：280ms 内同槽二次点击 + 可拖拽组 → 拿同类（doMergeSameId 扫 main+hotbar+in+fuel 同 id）。
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
        const r = InventoryOps.resolveClick(root, cur.id, cur.count)
        if (!r) return
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }
    function slotRight(group, index) {
        const cur = InventoryOps.readSlot(root, group, index)
        const r = InventoryOps.resolveRightClick(root, cur.id, cur.count)
        if (!r) return
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }

    // ── t79/t98/t108/t167 拖动均分 + t110 数字键交换：算法见 InventoryOps（四面板共享）。本处薄委托包装，
    //   供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。本面板用字面 key（"in:0" 等），无需 slotKey。
    function dragHasKey(key) { return InventoryOps.dragHasKey(root, key) }
    function addDragSlot(key) { InventoryOps.addDragSlot(root, key) }
    function beginLeftDrag() { InventoryOps.beginLeftDrag(root) }
    function endLeftDrag() { InventoryOps.endLeftDrag(root) }
    // t181 右键拖动（每格放 1 个）：薄委托包装。
    function beginRightDrag() { InventoryOps.beginRightDrag(root) }
    function endRightDrag() { InventoryOps.endRightDrag(root) }
    function addRightDragSlot(key) { InventoryOps.addRightDragSlot(root, key) }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    // redistributeLive / singleLeftClick / slotShiftLeft：纯内部辅助（仅 InventoryOps 内部 / slotLeft 调用），
    //   算法已入 InventoryOps，此处不再持有副本。


    // ── 冶炼 tick（spec：燃料燃烧→累积热量→输入转输出；WorldClock.ticked 驱动，每 tick dt=0.1s）──
    // 纯本地态推进 + 查 SmeltingRegistry（经 hotbar VM 透传）；无副作用到 C++（除 hotbar.setStack，热栏写入）。
    function tick(dt) {
        if (!root.hotbar) return
        let changed = false

        // 当前输入的冶炼产物（输入空 / 不可冶炼 → 0）。
        let resultId = root.inId !== 0 ? root.hotbar.smeltResult(root.inId) : 0

        // 「可冶炼一件」判定：输入非空 + 可冶炼 + 输出空或同产物未满。
        const canSmelt = () => {
            if (root.inId === 0 || root.inCount <= 0) return false
            if (resultId === 0) return false
            if (root.outId !== 0 && root.outId !== resultId) return false  // 输出槽有异物 → 不产（避免覆盖）
            if (root.outId === resultId && root.outCount >= root.hotbar.maxStackSize(resultId)) return false
            return true
        }

        // 点燃：未燃烧 + 可冶炼 + 燃料槽有可燃物 → consume 1 燃料、置 burnRemain。
        // MC 行为：仅在「有活干」时才点火（避免空烧燃料）。
        if (root.burnRemain <= 0 && root.fuelId !== 0 && root.fuelCount > 0 && canSmelt()) {
            const burn = root.hotbar.fuelBurnSeconds(root.fuelId)
            if (burn > 0) {
                root.fuelCount = root.fuelCount - 1
                if (root.fuelCount <= 0) { root.fuelId = 0; root.fuelCount = 0 }
                root.burnRemain = burn
                changed = true
            }
        }

        // 燃烧中：燃料时间递减；可冶炼则累积进度；进度满则产 1 件（循环防大 dt 漏产）。
        if (root.burnRemain > 0) {
            root.burnRemain = Math.max(0.0, root.burnRemain - dt)
            if (canSmelt()) {
                root.smeltProgress += dt
                while (root.smeltProgress >= root.kSmeltSecs && canSmelt()) {
                    // 消耗 1 输入。
                    root.inCount = root.inCount - 1
                    if (root.inCount <= 0) { root.inId = 0; root.inCount = 0 }
                    // 产出 1 输出。
                    if (root.outId === 0) { root.outId = resultId; root.outCount = 1 }
                    else { root.outCount = root.outCount + 1 }
                    root.smeltProgress = root.smeltProgress - root.kSmeltSecs
                    changed = true
                    // 输入可能因消耗而空 → 重算产物防御（同槽 id 不变，仅 count 减，重算幂等）。
                    resultId = root.inId !== 0 ? root.hotbar.smeltResult(root.inId) : 0
                }
            }
            // 燃料烧尽或输入中断时 smeltProgress 保留（MC 行为：部分进度不丢失，仅不再推进）。
        }

        if (changed) root.slotRev++
    }

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
    // 手持物时点遮罩区 → 丢弃为实体（同 CraftingTableUI / SurvivalInventory）。
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

    // 面板：深色圆角，居中。宽度与 CraftingTableUI 一致（392）便于复用主栏布局；
    // 高度 = 内容（标题+熔炉行+主栏+hotbar）+ 3×spacing + 2×margin，刚好容纳 → hotbar 贴底无空白带（同工作台）。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 334                                  // 22+84+120+40(=266) + 3×12 spacing(36) + 2×16 margin(32) = 334
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
                    text: "熔炉"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 熔炉行：左输入槽 + 火焰（下接燃料槽） + 中箭头 + 右输出槽。整体水平居中（同 CraftingTableUI
            // craftRow 的 rowOffsetX 算法：行宽 = 2*slotSize + 28 + slotSize = 148，在 360 行宽内居中）。
            Item {
                id: furnaceRow
                width: parent.width
                height: root.slotSize + 4 + root.slotSize   // 顶槽(slotSize) + 间距(4) + 燃料槽完整高(slotSize)，容纳燃料槽不溢出
                readonly property real rowOffsetX: (width - (2 * root.slotSize + 28 + root.slotSize)) / 2

                // 输入槽（左上）。
                Item {
                    x: furnaceRow.rowOffsetX; y: 0
                    width: root.slotSize; height: root.slotSize
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                    // 图标（方块 Image / 工具 ToolIcon / 材料 MaterialIcon 三分流；触碰 slotRev 刷新）。
                    Item {
                        anchors.centerIn: parent; width: 30; height: 30
                        property int itemId: { root.slotRev; return root.inId }
                        visible: itemId !== 0
                        Image {
                            anchors.fill: parent
                            visible: parent.itemId !== 0 && !root.hotbar.isTool(parent.itemId) && !root.hotbar.isMaterial(parent.itemId)
                            source: parent.itemId !== 0 ? root.hotbar.iconSourceForBlock(parent.itemId) : ""
                            fillMode: Image.PreserveAspectFit; smooth: true
                        }
                        ToolIcon { anchors.fill: parent; visible: parent.itemId !== 0 && root.hotbar.isTool(parent.itemId); tier: root.hotbar.toolTier(parent.itemId) }
                        MaterialIcon { anchors.fill: parent; visible: parent.itemId !== 0 && root.hotbar.isMaterial(parent.itemId); materialId: parent.itemId }
                    }
                    Text {
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.rightMargin: 3; anchors.bottomMargin: 1
                        visible: { root.slotRev; return root.inCount > 1 }
                        text: { root.slotRev; return root.inCount }
                        color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                        font.pixelSize: 13; font.bold: true
                    }
                    TapHandler { acceptedButtons: Qt.LeftButton;  onTapped: root.slotLeft("in", 0) }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.slotRight("in", 0) }
                    // t94 tooltip：悬停显输入物品名（inId 经 slotRev 刷新；空槽不显）。
                    HoverHandler {
                        // t99：跟踪槽显示 id。槽变空时 hover 仍 true → onHoveredChanged 不重发 → tooltip
                        // 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                        property int trackedId: { root.slotRev; return root.inId }
                        onTrackedIdChanged: {
                            if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                root.hoveredItemId = 0
                        }
                        onHoveredChanged: {
                            if (hovered && root.inId !== 0) {
                                root.hoveredItemId = root.inId
                                const p = parent.mapToItem(root, parent.width / 2, 0)
                                root.hoveredTipPos = Qt.point(p.x, p.y)
                            } else if (root.hoveredItemId === root.inId) {
                                root.hoveredItemId = 0
                            }
                            // t110：track hoveredKey 供数字键交换 / t167 左键拖动起点槽（in:0）。
                            const key = "in:0"
                            if (hovered) root.hoveredKey = key
                            else if (root.hoveredKey === key) root.hoveredKey = ""
                            // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                            if (hovered && root.dragActive) root.addDragSlot(key)
                        }
                    }
                    // t167 均分拖拽高亮（异物槽纵使被扫过也不亮）。
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: "#7fe57f"; border.width: 2
                        visible: {
                            root.dragSlots; root.leftDragActive; root.slotRev
                            return root.leftDragActive
                                && root.dragHasKey("in:0")
                                && (root.inId === 0 || root.inId === root.dragHeldId)
                        }
                        z: 10
                    }
                }

                // 燃料槽（输入槽下方）。MC 布局：燃料在输入下方、火焰夹其间。
                Item {
                    x: furnaceRow.rowOffsetX; y: root.slotSize + 4
                    width: root.slotSize; height: root.slotSize - 4
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                    Item {
                        anchors.centerIn: parent; width: 26; height: 26
                        property int itemId: { root.slotRev; return root.fuelId }
                        visible: itemId !== 0
                        Image {
                            anchors.fill: parent
                            visible: parent.itemId !== 0 && !root.hotbar.isTool(parent.itemId) && !root.hotbar.isMaterial(parent.itemId)
                            source: parent.itemId !== 0 ? root.hotbar.iconSourceForBlock(parent.itemId) : ""
                            fillMode: Image.PreserveAspectFit; smooth: true
                        }
                        ToolIcon { anchors.fill: parent; visible: parent.itemId !== 0 && root.hotbar.isTool(parent.itemId); tier: root.hotbar.toolTier(parent.itemId) }
                        MaterialIcon { anchors.fill: parent; visible: parent.itemId !== 0 && root.hotbar.isMaterial(parent.itemId); materialId: parent.itemId }
                    }
                    Text {
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.rightMargin: 3; anchors.bottomMargin: 1
                        visible: { root.slotRev; return root.fuelCount > 1 }
                        text: { root.slotRev; return root.fuelCount }
                        color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                        font.pixelSize: 13; font.bold: true
                    }
                    TapHandler { acceptedButtons: Qt.LeftButton;  onTapped: root.slotLeft("fuel", 0) }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.slotRight("fuel", 0) }
                    // t94 tooltip：悬停显燃料物品名（fuelId 经 slotRev 刷新）。
                    HoverHandler {
                        // t99：跟踪槽显示 id。槽变空时 hover 仍 true → onHoveredChanged 不重发 → tooltip
                        // 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                        property int trackedId: { root.slotRev; return root.fuelId }
                        onTrackedIdChanged: {
                            if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                root.hoveredItemId = 0
                        }
                        onHoveredChanged: {
                            if (hovered && root.fuelId !== 0) {
                                root.hoveredItemId = root.fuelId
                                const p = parent.mapToItem(root, parent.width / 2, 0)
                                root.hoveredTipPos = Qt.point(p.x, p.y)
                            } else if (root.hoveredItemId === root.fuelId) {
                                root.hoveredItemId = 0
                            }
                            // t110：track hoveredKey 供数字键交换 / t167 左键拖动起点槽（fuel:0）。
                            const key = "fuel:0"
                            if (hovered) root.hoveredKey = key
                            else if (root.hoveredKey === key) root.hoveredKey = ""
                            // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                            if (hovered && root.dragActive) root.addDragSlot(key)
                        }
                    }
                    // t167 均分拖拽高亮（异物槽纵使被扫过也不亮）。
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: "#7fe57f"; border.width: 2
                        visible: {
                            root.dragSlots; root.leftDragActive; root.slotRev
                            return root.leftDragActive
                                && root.dragHasKey("fuel:0")
                                && (root.fuelId === 0 || root.fuelId === root.dragHeldId)
                        }
                        z: 10
                    }
                }

                // 火焰图标（输入 / 燃料之间）：burnRemain>0 时显并闪烁（flameFlicker 驱动 Canvas 重绘）。
                // 自绘原创像素火焰（§9a）。flameFlicker 由 NumberAnimation（燃烧时跑）驱动。
                Canvas {
                    id: flameCanvas
                    x: furnaceRow.rowOffsetX + 4; y: root.slotSize - 6
                    width: root.slotSize - 8; height: 14
                    visible: root.burnRemain > 0
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        ctx.imageSmoothingEnabled = false
                        // 火焰高度随 flicker 0..1 在 8..14 间跳；底宽随 flicker 微变。
                        const fh = 8 + root.flameFlicker * 6
                        const fw = 8 + root.flameFlicker * 3
                        const cx = width / 2
                        // 外焰（橙红）：底部宽矩形 + 顶部三角。
                        ctx.fillStyle = "#e85a18"
                        ctx.fillRect(cx - fw / 2, height - fh, fw, fh)
                        ctx.beginPath()
                        ctx.moveTo(cx - fw / 2, height - fh + 2)
                        ctx.lineTo(cx, height - fh - 4)
                        ctx.lineTo(cx + fw / 2, height - fh + 2)
                        ctx.closePath(); ctx.fill()
                        // 内焰（黄亮）：稍小一圈。
                        ctx.fillStyle = "#f8c020"
                        ctx.fillRect(cx - fw / 4, height - fh + 3, fw / 2, fh - 4)
                    }
                    Connections { target: root; function onFlameFlickerChanged() { flameCanvas.requestPaint() } }
                    Component.onCompleted: flameCanvas.requestPaint()
                }

                // 冶炼进度箭头（火焰右侧 → 输出槽）：按 smeltProgress / kSmeltSecs 填充。
                // 自绘像素箭头（§9a）：底色暗灰 + 进度亮绿覆盖（clip 按比例）。
                Canvas {
                    id: smeltArrow
                    x: furnaceRow.rowOffsetX + root.slotSize + 2; y: root.slotSize / 2 - 10
                    width: 24; height: 20
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        ctx.imageSmoothingEnabled = false
                        // 底色箭头轮廓（暗灰）。
                        ctx.fillStyle = "#3a3a3a"
                        ctx.fillRect(0, 8, 16, 4)
                        ctx.beginPath()
                        ctx.moveTo(16, 2); ctx.lineTo(24, 10); ctx.lineTo(16, 18); ctx.closePath()
                        ctx.fill()
                        // 进度覆盖（亮绿）：按 smeltProgress 比例裁剪宽度。
                        const ratio = root.kSmeltSecs > 0 ? Math.max(0, Math.min(1, root.smeltProgress / root.kSmeltSecs)) : 0
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
                    Connections { target: root; function onSmeltProgressChanged() { smeltArrow.requestPaint() } }
                    Component.onCompleted: smeltArrow.requestPaint()
                }

                // 输出槽（最右）：冶炼产物堆叠。可左键拾取 / 右键半份（同其它槽；MC 输出槽可取可换）。
                Item {
                    x: furnaceRow.rowOffsetX + 2 * root.slotSize + 28; y: 0
                    width: root.slotSize; height: root.slotSize
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                    Item {
                        anchors.centerIn: parent; width: 30; height: 30
                        property int itemId: { root.slotRev; return root.outId }
                        visible: itemId !== 0
                        Image {
                            anchors.fill: parent
                            visible: parent.itemId !== 0 && !root.hotbar.isTool(parent.itemId) && !root.hotbar.isMaterial(parent.itemId)
                            source: parent.itemId !== 0 ? root.hotbar.iconSourceForBlock(parent.itemId) : ""
                            fillMode: Image.PreserveAspectFit; smooth: true
                        }
                        ToolIcon { anchors.fill: parent; visible: parent.itemId !== 0 && root.hotbar.isTool(parent.itemId); tier: root.hotbar.toolTier(parent.itemId) }
                        MaterialIcon { anchors.fill: parent; visible: parent.itemId !== 0 && root.hotbar.isMaterial(parent.itemId); materialId: parent.itemId }
                    }
                    Text {
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.rightMargin: 3; anchors.bottomMargin: 1
                        visible: { root.slotRev; return root.outCount > 1 }
                        text: { root.slotRev; return root.outCount }
                        color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                        font.pixelSize: 13; font.bold: true
                    }
                    TapHandler { acceptedButtons: Qt.LeftButton;  onTapped: root.slotLeft("out", 0) }
                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.slotRight("out", 0) }
                    // t94 tooltip：悬停显产物物品名（outId 经 slotRev 刷新）。
                    HoverHandler {
                        // t99：跟踪槽显示 id。槽变空时 hover 仍 true → onHoveredChanged 不重发 → tooltip
                        // 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                        property int trackedId: { root.slotRev; return root.outId }
                        onTrackedIdChanged: {
                            if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                root.hoveredItemId = 0
                        }
                        onHoveredChanged: {
                            if (hovered && root.outId !== 0) {
                                root.hoveredItemId = root.outId
                                const p = parent.mapToItem(root, parent.width / 2, 0)
                                root.hoveredTipPos = Qt.point(p.x, p.y)
                            } else if (root.hoveredItemId === root.outId) {
                                root.hoveredItemId = 0
                            }
                            // t110：track hoveredKey 供数字键交换 / t167 左键拖动起点槽（out:0）。
                            const key = "out:0"
                            if (hovered) root.hoveredKey = key
                            else if (root.hoveredKey === key) root.hoveredKey = ""
                            // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                            if (hovered && root.dragActive) root.addDragSlot(key)
                        }
                    }
                    // t167 均分拖拽高亮（异物槽纵使被扫过也不亮）。
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: "#7fe57f"; border.width: 2
                        visible: {
                            root.dragSlots; root.leftDragActive; root.slotRev
                            return root.leftDragActive
                                && root.dragHasKey("out:0")
                                && (root.outId === 0 || root.outId === root.dragHeldId)
                        }
                        z: 10
                    }
                }
            }

            // 3×9 主物品栏（27 槽）：t97 起读 hotbar VM（m_mainSlots，三菜单共享）；左键整组 / 右键半份取放
            // （与 CraftingTableUI 主栏同模式；slotLeft/slotRight 经 readSlot/writeSlot 路由到 VM.mainSetStack）。
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
                        property int mainId: { root.hotbar.mainRevision; return root.hotbar.mainBlockIdAt(index) }
                        property int mainCount: { root.hotbar.mainRevision; return root.hotbar.mainCountAt(index) }
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent }
                        Item {
                            anchors.centerIn: parent; width: 30; height: 30
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
                        TapHandler { acceptedButtons: Qt.LeftButton;  onTapped: root.slotLeft("main", index) }
                        TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.slotRight("main", index) }
                        // t94 tooltip：悬停显主栏槽物品名（mainId 由 delegate 持有；触碰 mainRevision 刷新）。
                        HoverHandler {
                            // t99：跟踪槽显示 id。槽变空时 hover 仍 true → onHoveredChanged 不重发 → tooltip
                            // 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
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
                                // t110：track hoveredKey 供数字键交换 / t167 左键拖动收集（main:index）。
                                const key = "main:" + index
                                if (hovered) root.hoveredKey = key
                                else if (root.hoveredKey === key) root.hoveredKey = ""
                                // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                                if (hovered && root.dragActive) root.addDragSlot(key)
                            }
                        }
                        // t167 均分拖拽高亮（异物槽纵使被扫过也不亮）。
                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.color: "#7fe57f"; border.width: 2
                            visible: {
                                root.dragSlots; root.leftDragActive; root.hotbar.mainRevision
                                return root.leftDragActive
                                    && root.dragHasKey("main:" + index)
                                    && (mainId === 0 || mainId === root.dragHeldId)
                            }
                            z: 10
                        }
                    }
                }
            }

            // 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            // 属性触碰 slotRevision（t55/t63 已验证写法）。左键整组 / 右键半份同主栏；不切真实选中。
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
                                anchors.centerIn: parent; width: 30; height: 30
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
                            TapHandler { acceptedButtons: Qt.LeftButton;  onTapped: root.slotLeft("hotbar", index) }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: root.slotRight("hotbar", index) }
                            // t94 tooltip：悬停显 hotbar 槽物品名（slotId 触碰 slotRevision 刷新）。
                            HoverHandler {
                                // t99：跟踪槽显示 id。槽变空时 hover 仍 true → onHoveredChanged 不重发 → tooltip
                                // 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
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
                                    // t110：track hoveredKey 供数字键交换 / t167 左键拖动收集（hotbar:index）。
                                    const key = "hotbar:" + index
                                    if (hovered) root.hoveredKey = key
                                    else if (root.hoveredKey === key) root.hoveredKey = ""
                                    // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                                    if (hovered && root.dragActive) root.addDragSlot(key)
                                }
                            }
                            // t167 均分拖拽高亮（异物槽纵使被扫过也不亮）。
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                border.color: "#7fe57f"; border.width: 2
                                visible: {
                                    root.dragSlots; root.leftDragActive; root.hotbar.slotRevision
                                    return root.leftDragActive
                                        && root.dragHasKey("hotbar:" + index)
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

    // 火焰闪烁动画：burnRemain>0 时跑 NumberAnimation（0→1 循环 ~0.25s），驱动 flameFlicker → Canvas 重绘。
    // 燃烧停时 running=false（火焰隐，flameFlicker 停在末值不影响 —— Canvas visible 绑 burnRemain）。
    NumberAnimation on flameFlicker {
        from: 0.0; to: 1.0; duration: 250; loops: Animation.Infinite
        running: root.burnRemain > 0
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
