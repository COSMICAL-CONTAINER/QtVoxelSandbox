import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 dispenser 本地槽路由 + 薄委托包装（供 QML 信号
//   处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，消除多面板逐字复制。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

// 发射器物品栏面板（t517 / 工作台蓝本）：右键发射器方块打开（PlayerController::dispenserOpened → Main.qml
// Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// 布局贴近 MC 1.0 发射器：上部「3×3=9 槽发射器物品栏」+ 下部「3×9=27 主物品栏 + 9 hotbar 行」（容器 +
// 背包布局，同工作台 / 熔炉 / 箱子结构）。机制等价 MC 右键发射器开物品栏界面。
//
// t517 本任务为「先界面」：原版 9 槽发射器内容存面板本地数组（dispSlots/dispCounts/dispRev），导致全世界
//   发射器共享一个物品栏、打掉不掉。**t542 修**：内容下沉到 DispenserStore（C++ VM，按方块坐标键控的 9 槽
//   3×3，同 ChestStore / FurnaceStore 模式）→ 每只发射器各自独立 9 槽、跨 UI 开关持久、破块掉内容。
//
// 物品移动（9 槽 / 主栏 / hotbar 间任意搬动）：左键整组 / 右键半份 / 单放 / 左键拖动均分 / 双击拿同类 /
//   Shift 搬运，与 CraftingTableUI / ChestUI 全套快捷操作同算法（共享 hotbar VM 的 heldBlock / heldCount
//   光标手持栈）。本面板把 "dispenser" 组经 localReadSlot/localWriteSlot 钩子路由到 DispenserStore（按坐标寻址）。
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
    // 宿主注入：DispenserStore（按 dispenserX/Y/Z 寻址的 per-block 9 槽 3×3 内容；slotIdAt/slotCountAt/
    //   setSlot/clearDispenser）。t542：替代旧 QML 本地数组（全世界发射器共享一份的 bug）。
    property DispenserStore dispenserStore
    // 宿主注入：当前所开发射器的方块世界坐标（dispenserOpened 携坐标 → Main.qml 存 window.dispenserX/Y/Z）。
    property int dispenserX: 0
    property int dispenserY: 0
    property int dispenserZ: 0
    // t609 面板标题（「发射器」/「投掷器」）：本面板被发射器（107）与投掷器（117）共用（同一 DispenserStore
    //   9 槽 + 同一物品搬动全套操作，仅弹出分派不同——发射器按物品种类弹丸 / 投掷器全部弹掉落物，均在 C++
    //   dispenseFromDispenser 分派，UI 层零差异）。宿主按所开方块 id 设标题（Main.qml openDispenser 处查
    //   theWorld.blockAt）；缺省「发射器」（既有行为兜底）。
    property string titleText: "发射器"
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49 同 CraftingTableUI / ChestUI：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区）。
    signal discardHeldRequested()
    // t228：请求宿主把光标手持栈**丢 1 件**为实体（右键拖出面板外；宿主接 player.dropHeldCursorOne）。
    //   左键整栈走 discardHeldRequested，右键逐个走本信号（spec「左键=全丢/右键=逐个」）。
    signal discardHeldOneRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int gridN: 3
    // MC 1.0 发射器布局：上部 3×3 容器，下部 3×9 物品栏 + hotbar。
    readonly property int mainCols: 9
    readonly property int mainRows: 3

    // 3×3 发射器容器 per-block 存储（DispenserStore，按 dispenserX/Y/Z 寻址）；与 hotbar VM 共享同一光标
    //   手持栈 heldBlock/heldCount。dispSlotCount 读 dispenserStore.slotCount（单一权威，恒 9 = 3×3）。
    //   t542：替代旧 QML 本地数组（dispSlots/dispCounts/dispRev）—— per-block 按坐标寻址，跨 UI 开关持久、
    //   破块掉内容（onBlockBroken(Dispenser) dump 9 槽 + clearDispenser）。
    readonly property int dispSlotCount: dispenserStore ? dispenserStore.slotCount : gridN * gridN
    // 触碰表达式：切发射器（dispenserX/Y/Z 变）或 DispenserStore.revision 变时，让所有读 disp 槽的绑定重算
    //   （同 ChestUI chestCoordRev 模式）。单独属性给 delegate 干净触碰点（避免每处裸写坐标 + revision）。
    property int dispCoordRev: (dispenserStore ? dispenserStore.revision : 0) + dispenserX * 131 + dispenserY * 17 + dispenserZ

    // t97：27 主物品栏自该任务起上移至 hotbar VM（m_mainSlots），与 SurvivalInventory / CraftingTableUI /
    //   FurnaceUI / ChestUI 多菜单共享同一份 → 主栏同步、returnHeldToHotbar/pickupScan 经 addToAny 能合并
    //   进主栏。与发射器槽 / hotbar 共享同一 hotbar VM 光标手持栈。

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    //   DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（单点拾取/放置/合并/互换），一旦
    //   拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler 在
    //   leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」
    //   字符串（去重简单）；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制
    //   （每滑入新格先撤销上轮写入再重分）。发射器槽 / 主栏 / hotbar 统一支持；右键分半走 per-slot 右键 TapHandler。
    property bool leftDragActive: false
    property var dragSlots: []              // "disp:2" / "main:5" / "hotbar:0"
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
    //   已写槽。每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginLeftDrag / endLeftDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    // t98 双击合并：lastTapMs/lastTapKey 记上次左键点击（槽 key）的时间戳与 key；280ms 内同槽二次点击 →
    //   doMergeSameId（扫 main+hotbar+disp 同 id 累加成满栈、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t180：dispenser 3×3 参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明 dispenser 为可拖拽
    //   本地组 → InventoryOps.groupIsDraggable 放行（addDragSlot 收集、redistributeLive 分发）、doMergeSameId
    //   扫 disp 槽。
    property var localDragGroups: ["dispenser"]
    // t180：dispenser 组槽位数（doMergeSameId 扫描范围）= dispSlotCount（单一权威读 dispenserStore.slotCount，
    //   恒 9 = 3×3 = DispenserStore::kSlotsPerDispenser）。
    function localSlotCount(group) { return group === "dispenser" ? root.dispSlotCount : 0 }

    // ── t168 / t542 面板专属槽路由：dispenser 容器格走 DispenserStore（按 dispenserX/Y/Z 寻址；main/hotbar
    //   由 InventoryOps 统一经 VM）。readSlot/writeSlot 薄包装委托 InventoryOps（含本地组分发 → 调本处
    //   localReadSlot/localWriteSlot）。dispCoordRev 触碰 dispenserX/Y/Z（切发射器时坐标变 → delegate 重读）。
    //   t647：实例元数据透传（耐久 / 附魔 / 名 —— 同 ChestStore 模式；修「附魔 / 改名物品放进发射器
    //   拿出来 / 弹出来变白板」）。
    function localReadSlot(group, index) {
        if (group === "dispenser" && root.dispenserStore) {
            return {
                id: root.dispenserStore.slotIdAt(root.dispenserX, root.dispenserY, root.dispenserZ, index),
                count: root.dispenserStore.slotCountAt(root.dispenserX, root.dispenserY, root.dispenserZ, index),
                durability: root.dispenserStore.slotDurabilityAt(root.dispenserX, root.dispenserY, root.dispenserZ, index),
                enchants: root.dispenserStore.slotEnchantsAt(root.dispenserX, root.dispenserY, root.dispenserZ, index),
                name: root.dispenserStore.slotNameAt(root.dispenserX, root.dispenserY, root.dispenserZ, index)
            }
        }
        return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0], name: "" }
    }
    function localWriteSlot(group, index, id, count, durability, enchants, name) {
        if (group !== "dispenser" || !root.dispenserStore) return
        root.dispenserStore.setSlot(root.dispenserX, root.dispenserY, root.dispenserZ, index, id, count,
                                    (Array.isArray(enchants) && enchants.length === 4) ? enchants : [],
                                    (typeof name === "string") ? name : "",
                                    (durability > 0) ? durability : -1)
    }
    // resolveClick / resolveRightClick（拾取/放置/合并/互换 + 半份）：算法见 InventoryOps（多面板共享）。
    //   返回 {slotId,slotCount,slotDur,slotEnch,heldId,heldCount,heldDur,heldEnch} 或 null=无操作；
    //   调用方据返回值写对应槽 + 更新 held。t622：+ curName 第 5 参（main/hotbar 槽实例名透传）。
    function resolveClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch, curName) }
    function resolveRightClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch, curName) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count, durability, enchants, name) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants, name) }

    // ── t79/t98/t108/t167 拖动均分 + t110 Shift/数字键搬运 + t98 双击合并：算法见 InventoryOps
    //   （多面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
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
    // t549 发射器 Shift+左键双向语义（spec「shift+左键应把物品直接放进去」；同附魔台 slotShiftLeftEnchant /
    //   箱子 slotShiftLeftChest 模式）：main/hotbar → 整栈并入发射器 9 槽（同 id 合并 → 空槽开新；满 → 余数
    //   留源槽）；dispenser 槽 → 整栈归还背包（addToAny）；背包满 → 余数留原槽（防丢物）。
    //   t647：并入 / 归还均透传实例元数据（耐久 / 附魔 / 名 —— 同 InventoryOps.addToChest 模式；空槽开新
    //   写源栈元数据，同 id 合并不动槽内元数据）。
    function slotShiftLeft(group, index) {
        if (!root.hotbar) return
        if (group === "main" || group === "hotbar") {
            const src = InventoryOps.readSlot(root, group, index)
            if (src.id === 0 || src.count <= 0) return
            // 并入发射器 9 槽：先同 id 未满槽合并，再空槽开新（同 addToChest 算法，槽位 = dispSlotCount）。
            const cap = root.hotbar.maxStackSize(src.id)
            let remaining = src.count
            for (let i = 0; i < root.dispSlotCount && remaining > 0; ++i) {
                const cur = InventoryOps.readSlot(root, "dispenser", i)
                if (cur.id === src.id && cur.count < cap) {
                    const move = Math.min(cap - cur.count, remaining)
                    InventoryOps.writeSlot(root, "dispenser", i, src.id, cur.count + move, cur.durability, cur.enchants, cur.name)
                    remaining -= move
                }
            }
            for (let i = 0; i < root.dispSlotCount && remaining > 0; ++i) {
                if (InventoryOps.readSlot(root, "dispenser", i).id === 0) {
                    const move = Math.min(cap, remaining)
                    InventoryOps.writeSlot(root, "dispenser", i, src.id, move, src.durability, src.enchants, src.name)
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
        if (group === "dispenser") {
            const src = InventoryOps.readSlot(root, "dispenser", index)
            if (src.id === 0 || src.count <= 0) return
            const remain = root.hotbar.addToAny(src.id, src.count, src.durability, src.enchants, src.name)
            InventoryOps.writeSlot(root, "dispenser", index, remain > 0 ? src.id : 0, remain,
                                  remain > 0 ? src.durability : 0,
                                  remain > 0 ? src.enchants : [0,0,0,0],
                                  remain > 0 ? src.name : "")
            return
        }
        InventoryOps.slotShiftLeft(root, group, index)
    }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }

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

    // 面板：深色圆角，居中。宽度容纳 9 槽行（9×40=360 + 2×16 边距 = 392）；高度 = 标题 + 3×3 发射器容器 +
    //   3×9 主栏 + 9 hotbar 行 + 间距/边距（同 CraftingTableUI 量级）。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 372                                  // 标题(22) + 发射器(120) + 主栏(120) + hotbar(40) + 间距/边距
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
                    text: root.titleText // t609 发射器 / 投掷器共用面板（宿主按方块 id 设标题）
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 发射器 3×3 容器格：整体水平居中（行宽 120 < 行内宽 360 → 居中，无右侧大段空白）。
            //   读 DispenserStore（按 dispenserX/Y/Z 寻址；dispCoordRev 驱动刷新）；左键整组 / 右键半份 取放
            //   （与主栏 / hotbar 共享同一 hotbar VM 光标手持栈）。栈写经 dispenserStore.setSlot（t542 per-block 存储）。
            Item {
                id: dispRow
                width: parent.width
                height: root.gridN * root.slotSize

                Grid {
                    id: dispGrid
                    // 3×3(120) 在 360 行宽内居中。
                    x: (dispRow.width - root.gridN * root.slotSize) / 2; y: 0
                    columns: root.gridN; spacing: 0
                    Repeater {
                        model: root.dispSlotCount  // t542：读 dispenserStore.slotCount（单一权威，恒 9 = 3×3）
                        delegate: Item {
                            property int dId: { const _r = root.dispCoordRev; return _r >= 0 ? (root.dispenserStore.slotIdAt(root.dispenserX, root.dispenserY, root.dispenserZ, index)) : 0 }
                            property int dCount: { const _r = root.dispCoordRev; return _r >= 0 ? (root.dispenserStore.slotCountAt(root.dispenserX, root.dispenserY, root.dispenserZ, index)) : 0 }
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                            // 物品图标：方块段→等距立方体 Image；工具段→ToolIcon；材料段（木棒）→MaterialIcon 自绘。
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: dId !== 0
                                Image {
                                    anchors.fill: parent
                                    visible: { const _r = root.dispCoordRev; return _r >= 0 ? (!root.hotbar.isTool(dId) && !root.hotbar.isMaterial(dId)) : false }
                                    source: { const _r = root.dispCoordRev; const _p = iconPackRefresh.active; return _r >= 0 && _p >= 0 ? (root.hotbar.iconSourceForBlock(dId)) : "" }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.dispCoordRev; return _r >= 0 ? (root.hotbar.isTool(dId)) : false }
                                    tier: { const _r = root.dispCoordRev; return _r >= 0 ? (root.hotbar.toolTier(dId)) : 0 }
                                    toolType: { const _r = root.dispCoordRev; return _r >= 0 ? (root.hotbar.toolType(dId)) : 0 }
                                }
                                // 材料段（木棒）：MaterialIcon 自绘（§9a 原创，非 MC 资产）。
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.dispCoordRev; return _r >= 0 ? (root.hotbar.isMaterial(dId)) : false }
                                    materialId: { const _r = root.dispCoordRev; return _r >= 0 ? (dId) : 0 }
                                }
                            }
                            // 栈数量（count>1 显数字）。触碰 dispCoordRev 刷新（DispenserStore NOTIFY 驱动）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { const _r = root.dispCoordRev; return _r >= 0 ? (dCount > 1) : false }
                                text: { const _r = root.dispCoordRev; return _r >= 0 ? (dCount) : "" }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // t647 附魔光晕（发射器 / 投掷器容器槽）：同主栏光晕配色。触碰 dispCoordRev 重算。
                            Rectangle {
                                anchors.fill: parent
                                visible: {
                                    const _r = root.dispCoordRev
                                    if (_r < 0 || dId === 0 || !root.dispenserStore) return false
                                    const e = root.dispenserStore.slotEnchantsAt(root.dispenserX, root.dispenserY, root.dispenserZ, index)
                                    return Array.isArray(e) && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
                                }
                                color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                                radius: 3
                                z: 3
                            }
                            // 左键整组（resolveClick）；右键走 per-slot 右键 TapHandler（resolveRightClick）。
                            // 左键拖动均分由 root DragHandler + 逐槽 HoverHandler 收集（t167）。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    // t110：Shift+左键搬运（dispenser 槽不在 main↔hotbar 范畴，slotShiftLeft 对
                                    //   dispenser 组无操作；普通左键走 resolveClick）。
                                    if (window.shiftHeld) { root.slotShiftLeft("dispenser", index); return }
                                    // t180：双击 disp 槽 → 拿同类（doMergeSameId 扫 main+hotbar+disp 同 id 累加成
                                    //   满栈、余数留光标；满栈按 main→hotbar→disp 顺序回填，故常把 disp 物品
                                    //   并入背包、清空容器槽便于重排）。
                                    const key = root.slotKey("dispenser", index)
                                    const now = Date.now()
                                    const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                                    root.lastTapMs = now
                                    root.lastTapKey = key
                                    if (isDouble) { root.doMergeSameId("dispenser", index); return }
                                    // t647：实例元数据透传（槽现持耐久 / 附魔 / 名）。
                                    const cur = InventoryOps.readSlot(root, "dispenser", index)
                                    const r = root.resolveClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                    if (!r) return
                                    root.localWriteSlot("dispenser", index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                    root.hotbar.setHeldEnchants(r.heldEnch)
                                    root.hotbar.heldCustomName = r.heldName   // t647 实例名 / 附魔随光标保真
                                }
                            }
                            // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const cur = InventoryOps.readSlot(root, "dispenser", index)
                                    const r = root.resolveRightClick(cur.id, cur.count, cur.durability, cur.enchants, cur.name)
                                    if (!r) return
                                    root.localWriteSlot("dispenser", index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                    root.hotbar.heldDurability = r.heldDur
                                    root.hotbar.setHeldEnchants(r.heldEnch)
                                    root.hotbar.heldCustomName = r.heldName   // t647 实例名 / 附魔随光标保真
                                }
                            }
                            HoverHandler {
                                // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                                //   不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                                property int trackedId: dId
                                onTrackedIdChanged: {
                                    if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                        root.hoveredItemId = 0
                                }
                                onHoveredChanged: {
                                    // t94 tooltip（dId 由 delegate 持有；触碰 dispCoordRev 刷新）。
                                    const itemId = dId
                                    if (hovered && itemId !== 0) {
                                        root.hoveredItemId = itemId
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                    } else if (root.hoveredItemId === itemId) {
                                        root.hoveredItemId = 0
                                    }
                                    const key = root.slotKey("dispenser", index)
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
                                    const _rev = root.dispCoordRev
                                    const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                                    const sid = dId
                                    const key = root.slotKey("dispenser", index)
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

            // t63 / t97 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，多菜单共享）；左键整组 / 右键半份
            //   取放（与 SurvivalInventory / CraftingTableUI 主栏同模式）。主栏栈写经 hotbar.mainSetStack；
            //   与发射器槽 / hotbar 共享同一 hotbar VM 光标手持栈。物品可在 主栏 ↔ 发射器槽 ↔ hotbar 间任意搬动。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    // model 用固定整数 mainCount（VM CONSTANT=27）；刷新靠每绑定触碰 mainRevision
                    // （Q_PROPERTY，NOTIFY=mainSlotsChanged）→ 经 mainBlockIdAt/mainCountAt 取最新栈值
                    // （同 hotbar 行 slotRevision 模式）。
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
                        // 栈数量（count>1 显数字）。触碰 mainRevision 刷新（VM NOTIFY 驱动）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount > 1) : false }
                            text: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount) : "" }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        // t647 附魔光晕（主栏槽）：同主栏光晕配色。触碰 mainRevision 重算。
                        Rectangle {
                            anchors.fill: parent
                            visible: {
                                const _r = root.hotbar.mainRevision
                                if (_r < 0 || mainId === 0) return false
                                return Array.isArray(mainEnch) && ((mainEnch[0] || 0) !== 0 || (mainEnch[1] || 0) !== 0 || (mainEnch[2] || 0) !== 0 || (mainEnch[3] || 0) !== 0)
                            }
                            color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                            radius: 3
                            z: 3
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
                                root.hotbar.heldCustomName = r.heldName
                            }
                        }
                        HoverHandler {
                            // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                            //   不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
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
            //   属性触碰 slotRevision。左键整组 / 右键半份同主栏（hotbar 槽写经 hotbar.setStack；VM 单一权威）。
            //   不切真实选中（同 SurvivalInventory / CraftingTableUI）。
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
                            // t647 附魔光晕（hotbar 槽）：同主栏光晕配色。触碰 slotRevision 重算。
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
                                //   不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
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
    //   顶层 import 新模块有「未部署→整文档加载失败」风险，见 lessons-learned）。各槽 HoverHandler 进入时写
    //   hoveredItemId + hoveredTipPos（槽顶中心在 root 坐标系下）；离开按 id 守卫清除（防相邻槽进出竞态互清）。
    //   名字走 hotbar.nameForBlock：方块→BlockRegistry::displayName、工具→ToolRegistry::displayName、
    //   材料段→本地通用名；air/空槽→空串→不显。工具后续将加「+攻击力」等字段，现阶段只名字。
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
    // t622 当前 hover 槽物品的实例名（铁砧改名；空串 → tooltip 走注册默认名）。据 hoveredKey 查 hotbar/main
    //   （dispenser 槽不持实例元数据——store 仅存 id/count 的既有边角）。
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
    // t763 当前 hover 槽物品的附魔列表文本（附魔数值可见性；同 ChestUI t647 模式，无 dispenser 分支——
    //   发射器槽不持附魔元数据）。
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
    // t763 武器伤害行（附魔数值可见性；同 SurvivalInventory t698 模式）：N = round(base + 0.5*锐锋级)，
    //   base 走 hotbar.itemAttackDamage（ToolRegistry 单一权威）。仅剑 / 斧（damage > 1）显。
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
        if (!Array.isArray(e)) return ""
        let sharp = 0
        for (let i = 0; i < 4; ++i) {
            if (((e[i] || 0) >> 8) === 1) { sharp = e[i] & 0xFF; break }   // EnchantRegistry::Sharpness = 1
        }
        const total = Math.round(root.hotbar.itemAttackDamage(root.hoveredItemId) + 0.5 * sharp)
        return "+" + total + " 攻击" + (sharp > 0 ? "（锐锋 +" + (0.5 * sharp) + "）" : "")
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
            // t763：附附魔行 + 攻击行（附魔数值可见性——附了锐锋 / 耐久的武器在发射器面板 hover 可见明细）。
            text: root.hotbar ? ((root.hoveredCustomName.length > 0 ? root.hoveredCustomName
                    : root.hotbar.nameForBlock(root.hoveredItemId))
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")
                + (root.hotbar.toolType(root.hoveredItemId) === 7 ? "  攻击 1-" + root.hotbar.bowArrowMaxDamage() : "")
                + (root.hoveredEnchantText.length > 0 ? "\n\n" + root.hoveredEnchantText : "")
                + (root.hoveredAttackText.length > 0 ? "\n\n" + root.hoveredAttackText : "")) : "" // t304 弓伤害 tooltip
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
