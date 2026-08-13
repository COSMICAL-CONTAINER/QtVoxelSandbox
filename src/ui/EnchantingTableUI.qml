import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t515：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   /slotShiftLeft 等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 main/hotbar VM 路由 + slotLeft/slotRight
//   面板 dispatch + 薄委托包装（供 QML 信号处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，
//   消除四面板逐字复制（同 CraftingTableUI / FurnaceUI / ChestUI 模式）。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

// 附魔台 UI（t515 工作台蓝本重做）：右键附魔台方块打开（PlayerController::enchantingTableOpened →
// Main.qml Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t515 用户要求「以工作台为蓝本重做」：现版是 shell-mode 选中槽消耗 UI（无背包），右键能开但用户要的是
//   工作台式「上方功能区 + 底部背包 4 行」布局。本任务把布局换成 CraftingTableUI 蓝本：
//   - 上区「附魔功能区」（占位，功能后补）：保留 t474 的 3 档位选项槽 + XP/青金石/书架状态条作为占位
//     附魔入口（消耗机制沿用 t474，真附魔效果归 t475/t476 后续实装）。spec 明确「先做界面，功能后补」。
//   - 下区「3×9 主物品栏 + 9 hotbar 行」= 底部背包 4 行（与工作台 / 熔炉 / 箱子同布局），玩家可在此放 / 取
//     背包物品（左键整组 / 右键半份 / 拖动均分 / Shift 搬运 / 双击拿同类，同 CraftingTableUI 全套快捷操作）。
//
// 本任务**不**做真附魔（消耗 + 占位 flash 沿用 t474，作为功能区的占位交互；功能后补）。核心交付 = 工作台
//   蓝本布局 + 底部 4 行背包能放/取物品（物品移动经 InventoryOps 单一权威，与工作台 / 熔炉 / 箱子共享算法）。
//
// 全部 GUI 自绘原创（Rectangle + Text + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点槽），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（heldBlock/heldCount/maxStackSize/iconSourceForBlock/nameForBlock/
    // isTool/isMaterial/slotRevision/mainSetStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 宿主注入：playerState（level / spendLevels）—— 占位附魔功能区消耗 XP 用（功能后补，沿用 t474）。
    property PlayerState playerState
    // 宿主注入：附魔台方块世界坐标（占位功能区书架加成查询用，沿用 t474；theWorld.countBookshelvesAround）。
    property int enchantX: 0
    property int enchantY: 0
    property int enchantZ: 0
    // 宿主注入：World（countBookshelvesAround 查书架数）。声明为 var 避免类型解析耦合（World 是 C++ 单例）。
    property var theWorld: null
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

    // ── t515 底部背包：27 主物品栏自 hotbar VM（m_mainSlots），与 SurvivalInventory / CraftingTableUI /
    //   FurnaceUI / ChestUI 五菜单共享同一份 → 五菜单主栏同步、returnHeldToHotbar/pickupScan 经 addToAny
    //   能合并进主栏。与 hotbar 共享同一 hotbar VM 光标手持栈；左键整组 / 右键半份同 resolveClick /
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
    //   （每格放 1 个）同源算法；dragActive 统一左/右拖动收集门控。均分算法与工作台 / 熔炉 / 箱子同源。
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

    // t515 本面板无本地容器槽（main/hotbar 全经 VM）→ localDragGroups 为空（无 craft / in/fuel/out），
    //   InventoryOps.groupIsDraggable 对未声明组拒收：拖拽 / 双击合并只走 main+hotbar（同 SurvivalInventory）。
    property var localDragGroups: []
    function localSlotCount(group) { return 0 }  // 无本地组槽位（doMergeSameId 扫描范围仅 main+hotbar）

    // ── t515 面板专属槽路由：无本地组（main/hotbar 由 InventoryOps 统一经 VM）。readSlot/writeSlot 薄包装
    //   委托 InventoryOps（无本地组分发 → localReadSlot/localWriteSlot 返空）。slotLeft/slotRight 统一槽点击
    //   dispatch（左键整组 / 右键半份 + Shift 搬运 + 双击拿同类），算法见 InventoryOps（五面板共享）。
    function localReadSlot(group, index) { return { id: 0, count: 0 } }
    function localWriteSlot(group, index, id, count) { /* 无本地组 */ }
    function resolveClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch) }
    function resolveRightClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count) { InventoryOps.writeSlot(root, group, index, id, count) }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → InventoryOps.slotShiftLeft（Shift+左键搬运 main↔hotbar）。
    //   t180：可拖拽组（main/hotbar）双击 → doMergeSameId（拿同类）。resolveClick/resolveRightClick 算法见
    //   InventoryOps（五面板共享，调用点零改动）。
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
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    // redistributeLive / singleLeftClick / slotShiftLeft：纯内部辅助（仅 InventoryOps 内部 / slotLeft 调用），
    //   算法已入 InventoryOps，此处不再持有副本。

    // ════════════════════════════════════════════════════════════════════════════
    // ── 占位附魔功能区（t474 沿用；功能后补）──
    // 3 档位选项槽消耗 XP 等级 + 青金石（点槽 → playerState.spendLevels + hotbar.consumeMaterial）；
    // 书架加成据 theWorld.countBookshelvesAround(enchantX/Y/Z) 算 → 提升可选档位。
    // 本任务**不做真附魔**（消耗 + 占位 flash 沿用 t474 作占位交互；真附魔效果归 t475/t476 后续实装）。
    // 青金石物品 id（RecipeRegistry::LapisId；Core 不依赖 Game 故 hotbar 无导出常量，硬编码 0x236）。
    readonly property int lapisId: 0x236
    // 3 个档位的固定消耗（XP 等级 + 青金石数）：I = (1, 1) / II = (2, 2) / III = (3, 3)。
    readonly property var levelCosts: [1, 2, 3]
    readonly property var lapisCosts: [1, 2, 3]
    // 占位附魔名（每次附魔后重投；机制等价 MC 附魔台随机三选项，本任务仅占位文案，§9 通用词非专名）。
    readonly property var placeholderNames: [
        "锋利", "保护", "效率", "耐久", "时运", "精准", "击退", "火焰附加"
    ]
    property var optionNames: ["附魔 I", "附魔 II", "附魔 III"]
    // 书架加成（0..15）→ 可选最高档位（1..3）：floor(bookshelves / 2) + 1，钳 [1, 3]。
    readonly property int bookshelfPower: {
        if (!theWorld) return 0
        return theWorld.countBookshelvesAround(enchantX, enchantY, enchantZ)
    }
    readonly property int maxLevel: Math.min(3, Math.max(1, Math.floor(bookshelfPower / 2) + 1))
    // 当前青金石持有数（hotbar + 主栏跨槽累计；slotRevision / mainRevision 触碰刷新）。
    readonly property int lapisCount: {
        if (!hotbar) return 0
        hotbar.slotRevision; hotbar.mainRevision  // 触碰 NOTIFY（低频：槽写入才发，非 per-frame）
        return hotbar.materialCount(lapisId)
    }
    // 当前 XP 等级（绑定 playerState.level NOTIFY levelChanged；低频，升级才发）。
    readonly property int playerLevel: playerState ? playerState.level : 0
    // 「已附魔」flash 状态（点击成功附魔后短暂显绿，~600ms 淡出）。
    property bool justEnchanted: false
    // PERF 护栏：选项列表只在「面板显」或「点击附魔后」刷新（refreshOptions），永不 per-frame。
    //   面板从隐藏切到显示时（visible → true）刷一次；书架数 / maxLevel 是只读属性（绑定自动重算）。
    onVisibleChanged: { if (visible) refreshOptions() }
    onEnchantXChanged: if (visible) refreshOptions()
    onEnchantYChanged: if (visible) refreshOptions()
    onEnchantZChanged: if (visible) refreshOptions()
    function refreshOptions() {
        var seed = (enchantX * 73856093) ^ (enchantY * 19349663) ^ (enchantZ * 83492791) ^ (playerLevel * 2654435761)
        seed = Math.abs(seed) | 0
        var picked = []
        var pool = placeholderNames.slice()
        for (var i = 0; i < 3 && pool.length > 0; ++i) {
            seed = (seed * 1103515245 + 12345) & 0x7fffffff
            var idx = seed % pool.length
            picked.push(pool[idx])
            pool.splice(idx, 1)
        }
        optionNames = picked.length === 3 ? picked : ["附魔 I", "附魔 II", "附魔 III"]
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
    // 手持物时点遮罩区 → 丢弃为实体（同 CraftingTableUI / FurnaceUI / SurvivalInventory）。t228：左键整栈 /
    //   右键 1 件 + 面板边界判定（面板内非槽位松手→不丢，修「左键拿物在面板内非槽位松手→直接丢地下」bug）。
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

    // 面板：深色圆角，居中。宽度与 CraftingTableUI 一致（392）便于复用主栏布局；
    // 高度 = 标题(22) + 占位附魔功能区(~132) + 主栏(120) + hotbar(40) + 间距/边距。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 382                                  // 22 + 132 + 120 + 40 (=314) + 3×12 spacing(36) + 2×16 margin(32) = 382
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
                    text: "附魔台"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 占位附魔功能区（t515 spec：上方功能区占位，功能后补）──
            // 沿用 t474 的 3 档位选项槽 + 状态条作为占位附魔入口（消耗机制保留，真附魔效果后补）。
            // 布局：状态条（XP/青金石/书架→可选档位）+ 3 档位选项槽横排 + 提示文字。
            Item {
                id: enchantArea
                width: parent.width
                height: 132

                // 上区状态条：XP 等级 / 青金石数 / 书架加成 → 可选档位。
                Rectangle {
                    id: statusbar
                    anchors.top: parent.top; anchors.topMargin: 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width; height: 30
                    color: "#241a12"; radius: 3
                    Text {
                        anchors.centerIn: parent
                        text: "等级 " + playerLevel + "    青金石 " + lapisCount +
                              "    书架 " + bookshelfPower + " → 可选 " +
                              (maxLevel === 1 ? "I" : maxLevel === 2 ? "I-II" : "I-III") + " 档"
                        color: "#ffe6a8"; font.pixelSize: 12
                    }
                }

                // 3 档位选项槽横排（占位附魔入口；消耗 XP + 青金石，点成功 → flash + 重投选项名）。
                Row {
                    id: optionRow
                    anchors.top: statusbar.bottom; anchors.topMargin: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 12

                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            id: optSlot
                            property int idx: index
                            property int lvlCost: root.levelCosts[index]
                            property int lapCost: root.lapisCosts[index]
                            // enabled 条件：档位序号 < maxLevel（书架解锁）且 XP 等级 + 青金石都够消耗。
                            property bool unlocked: index < root.maxLevel
                            property bool affordable: root.playerLevel >= lvlCost && root.lapisCount >= lapCost
                            property bool enabled1: unlocked && affordable
                            width: 100; height: 60
                            color: enabled1 ? "#5a4a2a" : "#2a2018"
                            border.color: enabled1 ? "#ffd87a" : "#0a0604"
                            border.width: enabled1 ? 2 : 1
                            radius: 4
                            opacity: unlocked ? 1.0 : 0.4  // 未解锁档位半透

                            Column {
                                anchors.centerIn: parent
                                spacing: 1
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: ["I", "II", "III"][index] + "  " + (root.optionNames[index] || "附魔")
                                    color: enabled1 ? "#ffe6a8" : "#665544"
                                    font.pixelSize: 12; font.bold: true
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: lvlCost + "级 / " + lapCost + "青金"
                                    color: enabled1 ? "#a8d8ff" : "#554433"
                                    font.pixelSize: 9
                                }
                            }

                            TapHandler {
                                enabled: optSlot.enabled1
                                onTapped: {
                                    // 占位附魔交互（功能后补）：消耗 XP + 青金石 → flash + 重投选项名。
                                    if (!optSlot.enabled1) return
                                    if (!root.playerState.spendLevels(optSlot.lvlCost)) return
                                    if (!root.hotbar.consumeMaterial(root.lapisId, optSlot.lapCost)) {
                                        console.warn("[enchant] lapis consume failed after XP spend")
                                    }
                                    root.justEnchanted = true
                                    enchantFlashTimer.restart()
                                    root.refreshOptions()
                                }
                            }
                        }
                    }
                }

                // 提示文字。
                Text {
                    anchors.bottom: parent.bottom; anchors.bottomMargin: 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "在附魔台 2 格内放置书架（每 2 个解锁更高档） · 附魔功能待实装"
                    color: "#aa9888"; font.pixelSize: 10
                }

                // 「已附魔」绿色 flash 叠层（点击成功后短暂显，~600ms 淡出）。
                Rectangle {
                    anchors.fill: parent
                    color: "#3aa55a"; opacity: root.justEnchanted ? 0.30 : 0.0
                    visible: opacity > 0.001
                    Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                    Text {
                        anchors.centerIn: parent
                        text: "已附魔！"
                        color: "#ffffff"; font.pixelSize: 18; font.bold: true
                        visible: root.justEnchanted
                    }
                }
            }

            // t515 底部 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，五菜单共享）；左键整组 / 右键半份
            // 取放（与 CraftingTableUI / FurnaceUI / ChestUI 主栏同模式）。主栏栈写经 hotbar.mainSetStack；
            // 与 hotbar 共享同一 hotbar VM 光标手持栈。物品可在 主栏 ↔ hotbar 间任意搬动（本面板无 craft /
            // in/fuel 本地槽）。delegate 持 mainId/mainCount 触碰 mainRevision（Q_PROPERTY NOTIFY=mainSlotsChanged）。
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

            // t515 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            // 属性触碰 slotRevision（t55/t63 已验证写法）。左键整组 / 右键半份同主栏（hotbar 槽写经
            // hotbar.setStack；VM 单一权威）。不切真实选中（同 SurvivalInventory / CraftingTableUI）。
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
    }

    // 「已附魔」flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
    Timer {
        id: enchantFlashTimer
        interval: 600
        onTriggered: root.justEnchanted = false
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
