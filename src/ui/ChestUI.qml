import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` /
//   `property ChestStore chestStore` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 chest 本地槽路由 + 薄委托包装。算法单一权威
//   收敛于此库，消除四面板逐字复制（箱子与工作台 / 熔炉同模式接入）。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

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
    // t228：请求宿主把光标手持栈**丢 1 件**为实体（右键拖出面板外；宿主接 player.dropHeldCursorOne）。
    //   左键整栈走 discardHeldRequested，右键逐个走本信号（spec「左键=全丢/右键=逐个」）。
    signal discardHeldOneRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int mainCols: 9
    readonly property int mainRows: 3
    // 箱子自身 3×9=27 槽（MC 1.0 标准；与 chestStore.slotCount 同值，下方 chestSlotCount 显式绑定单一权威）。
    //   chestCols/chestRows 是 Grid **布局**维度（columns=9 → 3 行），与「数据条目数」分离：
    //   Repeater model 读 chestSlotCount（= chestStore.slotCount，数据权威），布局维度仍读 chestCols/chestRows。
    //   不变量 chestCols * chestRows === chestStore.slotCount（27）；将来扩双倍箱子（54）时右移 chestRows=6。
    readonly property int chestCols: 9
    readonly property int chestRows: 3
    // t173 复审：chest 槽条目数单一权威 = ChestStore::slotCount（Q_PROPERTY 暴露，恒 27）。
    //   Repeater model 绑本属性 → chestStore 内部 kSlotsPerChest 改动时 Repeater 自动跟随，
    //   不再依赖本地 chestRows*chestCols 与之「偶合相等」（防一边改漏致槽位数不一致）。
    readonly property int chestSlotCount: chestStore ? chestStore.slotCount : chestRows * chestCols

    // t167 左键拖动均分（同 CraftingTableUI：root 级 DragHandler(LeftButton) 总控，逐槽 HoverHandler
    //   在 leftDragActive 期间收集扫过格 → redistributeLive 实时重分）。chest / main / hotbar 统一支持。
    property bool leftDragActive: false
    property var dragSlots: []              // "chest:2" / "main:5" / "hotbar:0"
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
    // t180 / t173 复审：chest 组槽位数（doMergeSameId 扫描范围）= chestSlotCount（单一权威读 chestStore.slotCount，
    //   恒 27 = 3×9 = ChestStore::kSlotsPerChest）。chestCols*chestRows 布局维度同值，但数据条目数走 chestStore 权威。
    function localSlotCount(group) { return group === "chest" ? root.chestSlotCount : 0 }

    // ── t168 面板专属槽路由：chest 组走 ChestStore（按坐标寻址）；main/hotbar 由 InventoryOps 统一经 VM。
    //   readSlot/writeSlot 薄包装委托 InventoryOps（含本地组分发 → 调本处 localReadSlot/localWriteSlot）。
    //   chestCoordRev 触碰 chestX/Y/Z（切箱子时坐标变 → delegate 经此重读新箱子内容）。
    //   review L7：chest 槽透传附魔（durability / enchants 维度）——战利品附魔书（带随机附魔）与玩家放入的
    //   附魔工具 / 护甲经 InventoryOps 整件搬运路径（拾取 / 放置 / 互换 / Shift 归还）保真，不再入箱即失附魔。
    //   t622：chest 槽透传实例名（铁砧改名物品入箱随栈存取——slotNameAt / setSlot 第 8 参）。
    function localReadSlot(group, index) {
        if (group === "chest") {
            root.chestCoordRev
            return {
                id: root.chestStore.slotIdAt(root.chestX, root.chestY, root.chestZ, index),
                count: root.chestStore.slotCountAt(root.chestX, root.chestY, root.chestZ, index),
                durability: 0,
                enchants: root.chestStore.slotEnchantsAt(root.chestX, root.chestY, root.chestZ, index),
                name: root.chestStore.slotNameAt(root.chestX, root.chestY, root.chestZ, index)
            }
        }
        return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0], name: "" }
    }
    function localWriteSlot(group, index, id, count, durability, enchants, name) {
        if (group === "chest") {
            // durability 恒 0（箱子槽不持耐久，同 t263 既有边角：工具罕见进箱 → 入箱视作新工具）；
            // enchants（review L7）透传 ChestStore（附魔书 / 附魔工具随实例存取）；
            // name（t622）透传 ChestStore（改名物品随实例存取）。
            root.chestStore.setSlot(root.chestX, root.chestY, root.chestZ, index, id, count,
                                    (Array.isArray(enchants) && enchants.length === 4) ? enchants : [],
                                    (typeof name === "string") ? name : "")
        }
    }
    // resolveClick / resolveRightClick（拾取/放置/合并/互换 + 半份）：算法见 InventoryOps（五面板共享）。
    function resolveClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch, curName) }
    function resolveRightClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch, curName) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    // review L7：薄包装签名补 durability / enchants 形参透传（对齐 EnchantingTableUI rv3 模式）—— chest 槽
    //   TapHandler 经此路径写槽，4 参签名会把算好的实例附魔截掉（战利品附魔书 / 附魔工具入箱即失附魔）。
    //   多收实参对旧 4 参调用点无害（undefined → InventoryOps 缺省语义）。t622：+ name 第 7 参同透传。
    function writeSlot(group, index, id, count, durability, enchants, name) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants, name) }

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
    // t521 箱子 Shift+左键双向语义：先走 slotShiftLeftChest（main/hotbar→放入箱子；chest→归还背包），
    //   返回 false（未知组；箱子界面不会出现）才回退到通用 slotShiftLeft（main↔hotbar 整理）。仿熔炉 slotLeft
    //   的 slotShiftLeftFurnace 优先回退模式。
    function slotShiftLeft(group, index) {
        if (InventoryOps.slotShiftLeftChest(root, group, index)) return
        InventoryOps.slotShiftLeft(root, group, index)
    }
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
    // 手持物时点遮罩区 → 丢弃为实体（同 SurvivalInventory / CraftingTableUI）。t228：左键整栈 / 右键 1 件
    //   + 面板边界判定（面板内非槽位松手→不丢，修「左键拿物在面板内非槽位松手→直接丢地下」bug）。
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
                    model: root.chestSlotCount  // t173 复审：读 chestStore.slotCount（单一权威，恒 27；不再本地 chestRows*chestCols 偶合）
                    delegate: Item {
                        property int chId: { const _r = root.chestCoordRev; return _r >= 0 ? (root.chestStore.slotIdAt(root.chestX, root.chestY, root.chestZ, index)) : 0 }
                        property int chCount: { const _r = root.chestCoordRev; return _r >= 0 ? (root.chestStore.slotCountAt(root.chestX, root.chestY, root.chestZ, index)) : 0 }
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent; wellColor: "#3a2a18" }  // 箱子槽底偏木色（区隔玩家槽）
                        Item {
                            anchors.centerIn: parent
                            width: 30; height: 30
                            visible: chId !== 0
                            Image {
                                anchors.fill: parent
                                visible: { const _r = root.chestCoordRev; return _r >= 0 ? (!root.hotbar.isTool(chId) && !root.hotbar.isMaterial(chId)) : false }
                                source: { const _r = root.chestCoordRev; return _r >= 0 ? (root.hotbar.iconSourceForBlock(chId)) : "" }
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            ToolIcon {
                                anchors.fill: parent
                                visible: { const _r = root.chestCoordRev; return _r >= 0 ? (root.hotbar.isTool(chId)) : false }
                                tier: { const _r = root.chestCoordRev; return _r >= 0 ? (root.hotbar.toolTier(chId)) : 0 }
                                toolType: { const _r = root.chestCoordRev; return _r >= 0 ? (root.hotbar.toolType(chId)) : 0 }
                            }
                            MaterialIcon {
                                anchors.fill: parent
                                visible: { const _r = root.chestCoordRev; return _r >= 0 ? (root.hotbar.isMaterial(chId)) : false }
                                materialId: { const _r = root.chestCoordRev; return _r >= 0 ? (chId) : 0 }
                            }
                        }
                        // 栈数量（count>1 显数字）。触碰 chestCoordRev 刷新（ChestStore NOTIFY 驱动）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { const _r = root.chestCoordRev; return _r >= 0 ? (chCount > 1) : false }
                            text: { const _r = root.chestCoordRev; return _r >= 0 ? (chCount) : "" }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        // 左键整组（resolveClick）；右键走 per-slot 右键 TapHandler（resolveRightClick）。
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                // t521：Shift+左键 → chest 槽整栈归还背包（早于双击合并 / 普通左键）。
                                if (window.shiftHeld) { root.slotShiftLeft("chest", index); return }
                                // t98 双击合并：400ms 内同槽二次点击 → doMergeSameId。
                                const key = root.slotKey("chest", index)
                                const now = Date.now()
                                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                root.lastTapMs = now
                                root.lastTapKey = key
                                if (isDouble) { root.doMergeSameId("chest", index); return }
                                // review L7：经 localReadSlot 读（透传附魔——战利品附魔书 / 附魔工具随实例走）。
                                // t622：透传名（改名物品随实例走）。
                                const cur = root.readSlot("chest", index)
                                const r = root.resolveClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                if (!r) return
                                root.writeSlot("chest", index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
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
                                // review L7：经 localReadSlot 读（透传附魔）。
                                const cur = root.readSlot("chest", index)
                                const r = root.resolveRightClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                if (!r) return
                                root.writeSlot("chest", index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                                root.hotbar.heldDurability = r.heldDur
                                root.hotbar.setHeldEnchants(r.heldEnch)
                                root.hotbar.heldCustomName = r.heldName
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
                                // qml-touch 三轮：dragSlots/rightDragSlots/reversion 触碰参与 _ok 守卫（恒真），
                                //   防 AOT 死代码消除裸触碰 → 高亮不随拖拽集 / 版本号刷新。leftDragActive/
                                //   rightDragActive 在下方条件是 live 读、自带依赖（无需入守卫）。
                                const _ds = root.dragSlots
                                const _rds = root.rightDragSlots
                                const _rev = root.chestCoordRev
                                const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                                const key = root.slotKey("chest", index)
                                if (_ok && root.leftDragActive && root.dragHasKey(key)
                                    && (chId === 0 || chId === root.dragHeldId)) return true
                                return _ok && root.rightDragActive && root.rightDragHasKey(key)
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
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount > 1) : false }
                            text: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount) : "" }
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

    // t94 物品名悬停 tooltip（纯 QtQuick 自绘；同 CraftingTableUI）。各槽 HoverHandler 进入时写
    // hoveredItemId + hoveredTipPos；离开按 id 守卫清除（防相邻槽进出竞态互清）。名字走 hotbar.nameForBlock。
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
        if (parts[0] === "main") return root.hotbar.mainDurabilityAt(idx)
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
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")
                + (root.hotbar.toolType(root.hoveredItemId) === 7 ? "  攻击 1-" + root.hotbar.bowArrowMaxDamage() : "")) : "" // t304 弓伤害 tooltip
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
