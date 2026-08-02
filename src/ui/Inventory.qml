import QtQuick
// t127：创造调色板 ScrollBar（拖动指示）来自 QtQuick.Controls。纯 QML 模块不经 C++ 链接（同 t14
//   Particles3D）——CMakeLists 的 windeployqt POST_BUILD 已用 `--qmldir src/ui` 扫 import 语句自动部署
//   匹配的 QML 模块插件，故无 t94 tooltip 注释担心的「未部署→整文档加载失败」之忧（见 lessons-learned）。
import QtQuick.Controls
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t168：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   等）抽取自共享 JS 库 InventoryOps.js；本面板无本地槽组（仅 main/hotbar），故只保留薄委托包装（供 QML
//   信号处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，消除四面板逐字复制。
import "InventoryOps.js" as InventoryOps

// 创造模式物品栏 1.0（t23）：E 键开关（仅 Creative 模式 —— 宿主 Main.qml 已按模式分流：Creative
// 开本面板、Survival 开 t24 生存背包、Spectator E 无反应）。
//
// 三段式 1.0 布局：
//   ① 顶部可滚动全方块调色板（8 实方块 + 扩展空槽；Flickable 支持未来 ~40 方块扩容滚动）；
//   ② 底部 9 槽 hotbar 栏（与游戏内 hotbar 同步：读同一 hotbar VM，选中槽选框高亮、点击切换选中）；
//   ③ 销毁槽（拖入 hotbar 槽内容 → setSlotBlock(slot, air=0) 清空该槽）。
// 调色板点击方块 → 装入当前选中 hotbar 槽（保留 t18 行为）；中文方块名作状态行/悬停标签（§9 override (b)）。
//
// 本组件只做**呈现 + 输入转发**：方块集 / 图标 / 中文名 / 槽位改写全部经注入的 hotbar VM
// （ViewModel 读 BlockRegistry，PLAN §2 分层：UI 不另持方块表副本）。全部槽框/选框/销毁图标本项目
// 自绘原创（Rectangle + Canvas，无外部 MC GUI PNG；§9 override (a)）。零 MC 专有名词（§9）。
//
// 宿主负责指针态：背包打开时已 release（光标可见，可点/拖）；关闭（closed 信号）→ 宿主恢复 grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（提供 creativeBlocks / slotList / iconSourceForBlock /
    // nameForBlock / setSlotBlock / selectedSlot / slotRevision）。
    property Hotbar hotbar
    // 请求宿主关闭背包（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区时；宿主接 player.dropHeldCursor）。
    signal discardHeldRequested()
    // t120：创造拿物品（调色板点击 → 拿到光标 / 手）→ 请求宿主弹手动画（Main.qml 接 handPopAnim.start）。
    //   机制等价生存拾取的手弹反馈，但创造无实体销毁、不发 player.itemPickedUp（那是实体拾取专用信号）；
    //   故经此信号让宿主单独触发手弹（spec「创造拿物品到手也触发 handPopAnim」）。仅调色板「无限源拿取」
    //   发，hotbar 槽间搬动 / 互换不算「拿新物」。
    signal itemTaken()

    // ① 调色板数据：9 实方块（creativeBlocks，含工作台 CraftingTable，t59）+ 3 档镐（creativeTools，t33）
    // + 6 材料（creativeMaterials，t114：木棒 / 煤炭 / 木炭 / 铁原矿 / 铁锭 / 玻璃）+ 扩展空槽（id=0 → 空占位）。
    // 一次性求值的绑定（方块 / 工具 / 材料集恒定；root.hotbar 由 null→对象 时重新求值）。
    // 空槽既是「可滚动」的内容，也占位示意未来 Phase 1.x 的 ~40 方块扩容（MC 1.0 创造页也是多行大网格）。
    readonly property var paletteModel: root.hotbar
        ? root.hotbar.creativeBlocks().concat(root.hotbar.creativeTools())
                                .concat(root.hotbar.creativeMaterials())
                                .concat([0,0,0,0,0,0,0,0,0]) // 13 方块 + 3 镐 + 6 材料 + 9 空 = 31（≥27 占满 9 列 × 3 行 + 余）
        : []

    // 当前悬停方块的中文名（调色板/hotbar 槽 hover 时更新；§9 override (b) 中文通用词）。
    property string hoveredName: ""

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。创造背包仅
    //   hotbar 行作分发目标（调色板=无限源，不作目标）。手势由 root 级 DragHandler(LeftButton) 总控：按下
    //   不动时 per-slot 左键 TapHandler 抓（单点拾取/放置/合并/互换 / 调色板无限拿取），拖动越阈值 → DragHandler
    //   激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler 在 leftDragActive 期间收集
    //   （addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」字符串；dragHeld* 为按下瞬间
    //   光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制。均分算法与 t79/t98 右键拖拽同源（t166d
    //   改 per-slot 右键单点后右键拖拽停用）；t167 把同一算法接到左键。
    property bool leftDragActive: false
    property var dragSlots: []              // "hotbar:0" ..
    property string hoveredKey: ""
    property int dragHeldId: 0
    property int dragHeldCount: 0
    // t98 实时重分撤销机制：dragOriginal 记每槽 drag 前原始栈（首次 encounter 快照）；dragWritten 记本轮
    // 已写槽。每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginLeftDrag / endLeftDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    // t181 右键拖动（每格放 1 个；区别于左键 floor(count/N) 均分）。创造背包仅 hotbar 行作分发目标。
    //   dragActive 统一左/右拖动收集门控（HoverHandler 据 dragActive 调 addDragSlot，InventoryOps 内分发）。
    property bool rightDragActive: false
    property var rightDragSlots: []
    property bool rightDragPlaced: false
    property bool dragActive: leftDragActive || rightDragActive
    // t98 双击合并：lastTapMs/lastTapKey 记上次左键点击（槽 key）的时间戳与 key；400ms 内同槽二次点击 →
    // doMergeSameId（扫 main+hotbar 同 id 累加成满栈 64 一组、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // ── 尺寸常量（集中一处便于对齐）──
    readonly property int paletteCols: 9
    readonly property int cellSize: 42       // 调色板单格
    readonly property int slotSize: 40       // hotbar 单格（与游戏内 hotbar 视觉一致）
    readonly property int bevelDark: 0       // 凹陷斜面：顶/左 暗边
    readonly property int bevelLight: 0      // 凹陷斜面：底/右 亮边

    // t46/t49 左键整组（拾取/放置/合并/互换）+ 右键半份：算法见 InventoryOps.resolveClick /
    //   resolveRightClick（四面板共享）。本面板 hotbar 行支持把物品在槽间搬动/互换（非「创造覆盖」销毁）；
    //   调色板点击仍是「无限源拾取」（在 TapHandler 内直接 setHeldBlock，不走 resolveClick）。
    function resolveClick(curId, curCount) { return InventoryOps.resolveClick(root, curId, curCount) }
    function resolveRightClick(curId, curCount) { return InventoryOps.resolveRightClick(root, curId, curCount) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count) { InventoryOps.writeSlot(root, group, index, id, count) }

    // ── t79/t98/t108/t167 拖动均分 + t110 Shift/数字键搬运 + t98 双击合并：算法见 InventoryOps
    //   （四面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
    //   创造背包仅 hotbar 行作均分目标（调色板=无限源，不作目标）；本面板无 craft 本地组（localReadSlot 缺省
    //   → InventoryOps 兜底空栈，安全）。
    function slotKey(group, index) { return InventoryOps.slotKey(group, index) }
    function dragHasKey(key) { return InventoryOps.dragHasKey(root, key) }
    function addDragSlot(key) { InventoryOps.addDragSlot(root, key) }
    function beginLeftDrag() { InventoryOps.beginLeftDrag(root) }
    function endLeftDrag() { InventoryOps.endLeftDrag(root) }
    // t181 右键拖动（每格放 1 个）：薄委托包装。
    function beginRightDrag() { InventoryOps.beginRightDrag(root) }
    function endRightDrag() { InventoryOps.endRightDrag(root) }
    function addRightDragSlot(key) { InventoryOps.addRightDragSlot(root, key) }
    // redistributeLive / singleLeftClick：纯内部辅助（仅 InventoryOps.addDragSlot / endLeftDrag 调用），
    //   算法已入 InventoryOps，此处不再持有副本。
    function slotShiftLeft(group, index) { InventoryOps.slotShiftLeft(root, group, index) }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }

    // t167 左键拖动均分总控：DragHandler(LeftButton) 在 root 监听。按下不动时 per-slot 左键 TapHandler 抓
    //   （单点拾取/放置/合并/互换 / 调色板无限拿取 / Shift 搬运 / 双击合并）；拖动越阈值 → DragHandler 激活夺抓
    //   → onActiveChanged 驱动 begin/endLeftDrag。逐槽 HoverHandler 在 leftDragActive 期间收集扫过格（addDragSlot
    //   即 redistributeLive 实时重分）。target:null 防 DragHandler 默认拖动父 Item（面板）。注：t166d 把右键拖拽
    //   改 per-slot 右键单点后停用（hoveredKey 不可靠疑为「右键全失效」根因）；左键走 DragHandler 激活夺抓，
    //   不依赖 root TapHandler 的 hoveredKey（HoverHandler 仅在 leftDragActive 期间收集，且集合只增不减）。
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

    // 半透明遮罩：仅吸收点击（防穿透到背后的游戏/暂停层），**不关闭背包**——用户要求背包只能由
    // E / Esc 关闭（点背包 UI 外部不应关闭）。t49：手持物时点遮罩区（面板外）→ 整栈丢弃为实体（同 Q 丢弃）。
    // t158：acceptedButtons 限左键（原全键 MouseArea 抢 right press 致 per-slot 右键 TapHandler 永不触发）。
    //   限左键后右键透到槽 per-slot TapHandler（resolveRightClick 生效）。左键点遮罩 → 丢弃；左键在槽区拖动 →
    //   DragHandler 接管均分（t167）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onClicked: {
                // 拖出丢弃（spec point 5）：手持物点背包外 → 请求宿主丢弃；空手仅吸收点击。
                if (root.hotbar && root.hotbar.heldBlock !== 0) root.discardHeldRequested()
            }
        }
    }

    // 面板：深色圆角，居中。
    Rectangle {
        id: panel
        width: 470
        height: 312
        anchors.centerIn: parent
        radius: 14
        color: "#1b1f24"
        border.color: "#3a444f"
        border.width: 1

        // t136：兜底吸收面板内「空点击」（落空调色板格 / 标题 / 状态行 / 间距等无子 handler 区域），
        //   不让事件穿透到背后遮罩 MouseArea——该 MouseArea 持物时 onDiscardHeldRequested 会把光标手持栈
        //   丢弃为实体（用户报「创造拿物后点空调色板，物品消失」根因：空格 TapHandler enabled:false →
        //   事件穿透落遮罩 → 误丢弃）。Pointer Handlers 协作语义：子 palette/hotbar/销毁槽的 TapHandler 仍
        //   优先处理（更深的 handler 先 fire 各自 onTapped），本 handler 仅兜住未被处理的左键空点击；
        //   其 passive grab 同时阻止事件继续下沉到遮罩 MouseArea（Qt6：handler 截获则 MouseArea 不收）。
        //   仅左键：右键全归 root 右键 TapHandler 独占（t79 拿半/均分手势），互不冲突（t138：销毁槽已无 DragHandler）。
        TapHandler {
            acceptedButtons: Qt.LeftButton
        }

        Column {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            // 标题行：左标题，右关闭提示。
            Item {
                width: parent.width
                height: 24
                Text {
                    text: "创造物品栏"
                    color: "#eaf2ea"
                    font.pixelSize: 20
                    font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"
                    font.pixelSize: 11
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 状态行：当前选中槽 + 悬停方块中文名（hoveredName 随 hover 更新）。
            Text {
                width: parent.width
                color: "#9fb0c0"
                font.pixelSize: 12
                text: "当前选中：第 " + (root.hotbar ? root.hotbar.selectedSlot + 1 : 1) + " 槽 · "
                      + (root.hotbar ? root.hotbar.nameAt(root.hotbar.selectedSlot) : "")
                      + (root.hoveredName !== "" ? "    |    悬停：" + root.hoveredName : "")
            }

            // ① 调色板（Flickable 垂直可滚动 + ScrollBar 指示拖动；t127）。
            //   t127 根因：原视口仅 cellSize*2+8（2 行）+ 无 ScrollBar → 火把（第 13 项，9 列排第 2 行第 4 格）
            //   贴在视口边缘、下方工具/材料（第 3-4 行）既滚不出也无指示。修：(a) 视口抬到 cellSize*3+12 容 3 行
            //   → 火把完整可见且下方留滚动余量；(b) 挂 ScrollBar.vertical(policy AsNeeded) → 内容超视口时显
            //   拖动条，可拖 / 滚轮滚到第 4 行的工具 / 材料。视口抬升后整列内容 272 ≤ 面板可用高 284，不破布局。
            Flickable {
                id: paletteFlick
                width: parent.width
                height: root.cellSize * 3 + 12 // t127：视口容 3 行（火把第 2 行完整可见 + 下方可滚）
                clip: true
                contentWidth: paletteGrid.width
                contentHeight: paletteGrid.height
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                // t127：内容超出视口（4 行 > 3 行视口）时显垂直拖动条；policy=AsNeeded 即不足时不占空间。
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Grid {
                    id: paletteGrid
                    columns: root.paletteCols
                    spacing: 4
                    width: root.paletteCols * root.cellSize + (root.paletteCols - 1) * 4
                    anchors.horizontalCenter: parent.horizontalCenter

                    Repeater {
                        model: root.paletteModel
                        delegate: Item {
                            width: root.cellSize
                            height: root.cellSize

                            // 凹陷斜面槽框（顶/左 暗、底/右 亮 → 凹陷观感；与游戏内 hotbar 同风格）。
                            Rectangle { anchors.fill: parent; color: "#222831" } // 井底
                            Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
                            Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
                            Rectangle { color: "#5a5a5a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
                            Rectangle { color: "#5a5a5a"; width: 1; height: parent.height; anchors.right: parent.right }

                            // 物品图标：方块段 → 等距立方体 Image；工具段（t33 isTool）→ ToolIcon 自绘镐；
                            // 材料段（t50 isMaterial）→ MaterialIcon 自绘木棒（创造一般不直接取材料，但兼容）。
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: modelData !== 0
                                Image {
                                    anchors.fill: parent
                                    visible: !root.hotbar.isTool(modelData) && !root.hotbar.isMaterial(modelData)
                                    source: root.hotbar.iconSourceForBlock(modelData)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: root.hotbar.isTool(modelData)
                                    tier: root.hotbar.toolTier(modelData)
                                }
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: root.hotbar.isMaterial(modelData)
                                    materialId: modelData
                                }
                            }
                            // hover 高亮边框（仅实体方块）。
                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                radius: 2
                                border.color: cellHover.hovered && modelData !== 0 ? "#7fe57f" : "transparent"
                                border.width: 2
                            }

                            // hover → 状态行中文名；click → 拾取到光标（创造调色板=无限源：点击即「拿在鼠标上」，
                            // 再点 hotbar 槽放置。MC 创造背包交互）。
                            HoverHandler {
                                id: cellHover
                                enabled: modelData !== 0
                                onHoveredChanged: {
                                    // t94：进入写 hoveredItemId + 槽顶中心位置（root 坐标系）驱动 tooltip；
                                    // 离开按 id 守卫清除（防相邻槽进出竞态互清，见文件末 itemTip 注释）。
                                    if (hovered) {
                                        root.hoveredName = root.hotbar.nameForBlock(modelData)
                                        root.hoveredItemId = modelData
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                    } else if (root.hoveredItemId === modelData) {
                                        root.hoveredItemId = 0
                                    }
                                }
                            }
                            TapHandler {
                                enabled: modelData !== 0
                                // 拾取到光标（创造调色板=无限源，不清减调色板）。方块满栈 64；工具不可堆叠 →
                                // count=1（t33）。setHeldBlock 已对工具段 id 校验合法（isValidItemId 含工具段）。
                                onTapped: {
                                    // t136：换拿前先把旧光标手持栈丢为实体（创造调色板=无限源，旧物应「丢回世界」
                                    //   而非被下方赋值直接覆盖凭空消失）。discardHeldRequested → dropHeldCursor
                                    //   同步清 heldBlock/heldCount 并在玩家前方 spawn 实体；信号同线程直连，
                                    //   返回时 heldBlock 已为 0，随后赋新值安全。空手（heldBlock===0）跳过丢弃。
                                    if (root.hotbar.heldBlock !== 0) root.discardHeldRequested()
                                    root.hotbar.heldBlock = modelData
                                    // t174：count 走 maxStackSize（单一权威）—— 工具 1 / 桶 1（不可堆叠）/ 方块·材料 64。
                                    //   旧 `isTool ? 1 : 64` 对桶（材料段 0x206/0x207 maxStack=1）误给 64（放入槽被 setStack
                                    //   钳到 1，但光标浮动图标会短暂显 64）→ 统一走 maxStackSize 修正。
                                    root.hotbar.heldCount = root.hotbar.maxStackSize(modelData)
                                    root.itemTaken()  // t120：创造拿物品 → 宿主弹手（handPopAnim）
                                }
                            }
                        }
                    }
                }
            }

            // 销毁槽用法提示。
            Text {
                width: parent.width
                color: "#7d8893"
                font.pixelSize: 11
                text: "点击右侧销毁槽可丢弃当前手持物"
            }

            // ② 底部 9 槽 hotbar 栏（同步游戏内 hotbar） + ③ 销毁槽。
            Item {
                width: parent.width
                height: root.slotSize

                // hotbar 栏（左）：凹陷槽 + 选中槽选框（与游戏内 hotbar 视觉一致；点击切换选中、可拖到销毁槽）。
                Item {
                    id: hbBar
                    width: 9 * root.slotSize
                    height: root.slotSize
                    anchors.left: parent.left

                    Row {
                        id: hbRow
                        spacing: 0
                        Repeater {
                            // t63 修复（hotbar 行拾取/放入后不显图标，t55 复发；与 SurvivalInventory / HUD hotbar 同根因）：
                            //   旧 `model: { slotRevision; slotList() }` 返回长度恒 9 的 JS 数组（QVariantList）作 Repeater
                            //   model → 长度不变 → QQuickRepeater 复用 delegate、不重建 → modelData 停在初值（全空）。
                            //   修法（与 Main.qml HUD hotbar t55 已验证写法一致）：model 改用固定整数 slotCount=9
                            //   （CONSTANT → delegate 一次创建永驻），刷新责任下放到每个依赖槽内容的绑定：触碰
                            //   slotRevision → 经 Q_INVOKABLE blockIdAt(index) 取最新值。delegate 持 slotId 属性集中
                            //   此模式（dragIcon / 图标 / hover 全用 slotId，不再依赖 modelData）。
                            model: root.hotbar.slotCount
                            delegate: Item {
                                // 槽物品 id（触碰 slotRevision → 拾取/放入后重算 blockIdAt(index)；air=0 空槽）。
                                property int slotId: { root.hotbar.slotRevision; return root.hotbar.blockIdAt(index) }
                                width: root.slotSize
                                height: root.slotSize
                                Rectangle { anchors.fill: parent; color: "#2f2f2f" } // 井底
                                // 凹陷斜面：顶/左 暗、底/右 亮
                                Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
                                Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
                                Rectangle { color: "#5a5a5a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
                                Rectangle { color: "#5a5a5a"; width: 1; height: parent.height; anchors.right: parent.right }

                                // 物品图标 wrapper（仅非空槽显图标）。
                                Item {
                                    id: dragIcon
                                    anchors.centerIn: parent
                                    width: 30; height: 30
                                    visible: slotId !== 0
                                    Image {
                                        anchors.fill: parent
                                        visible: { root.hotbar.slotRevision; return !root.hotbar.isTool(slotId) && !root.hotbar.isMaterial(slotId) }
                                        source: { root.hotbar.slotRevision; return root.hotbar.iconSourceForBlock(slotId) }
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
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

                                // 栈数量（t32）：count>1 时右下角显数字（MC 风格：单件不显数）。
                                // 触碰 slotRevision 刷新（countAt 是 Q_INVOKABLE，靠版本号触发；model 现为固定整数，
                                // 不再靠「整列重建」刷新数量，故每绑定显式触碰版本号）。
                                Text {
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: 3
                                    anchors.bottomMargin: 1
                                    visible: { root.hotbar.slotRevision; return root.hotbar.countAt(index) > 1 }
                                    text: { root.hotbar.slotRevision; return root.hotbar.countAt(index) }
                                    color: "#ffffff"
                                    style: Text.Outline; styleColor: "#000000"
                                    font.pixelSize: 13; font.bold: true
                                }

                                // hover → 状态行中文名；tap → t46 与主栏/生存背包统一的栈操作（拾取/放置/合并/互换）。
                                //   旧版用「创造覆盖」（持物点异 id 槽 → 原物丢弃），用户反馈「hotbar 行不能左键
                                //   交互」——现统一走 resolveClick：持物点异 id 槽 → 互换（原物入手持，不丢失）。
                                //   t49：背包内点 hotbar 行**不切真实选中**（删 selectedSlot 赋值；真实选中仅由游戏内
                                //   1–9 / 滚轮改）。右键 = 拿一半 / 放一个（resolveRightClick）；销毁走点击销毁槽（t138：无 DragHandler）。
                                HoverHandler {
                                    id: slotHover
                                    // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                                    // 不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                                    property int trackedId: slotId
                                    onTrackedIdChanged: {
                                        if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                                            root.hoveredItemId = 0
                                    }
                                    onHoveredChanged: {
                                        console.log("[hovDBG] hotbar:" + index + " hovered=" + hovered) // t166e 诊断：hover 是否到槽
                                        // t94 tooltip（仅非空槽显名；空槽不动 hoveredItemId，免覆盖邻槽态）。
                                        if (hovered) {
                                            root.hoveredName = root.hotbar.nameForBlock(slotId)
                                            if (slotId !== 0) {
                                                root.hoveredItemId = slotId
                                                const p = parent.mapToItem(root, parent.width / 2, 0)
                                                root.hoveredTipPos = Qt.point(p.x, p.y)
                                            }
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
                                        const r = root.resolveClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index))
                                        if (r) {
                                            root.hotbar.setStack(index, r.slotId, r.slotCount)
                                            root.hotbar.heldBlock = r.heldId
                                            root.hotbar.heldCount = r.heldCount
                                        }
                                    }
                                }
                                // t166d：per-slot 右键（拿半/放一），不依赖 hover/hoveredKey（同左键 per-slot 模式，可靠）。
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
                                // t167 均分拖拽高亮（扫过且待分发的合格格绿框；leftDragActive 期间才显）。
                                // 异物槽纵使被扫过也不亮（addDragSlot 已过滤入 dragSlots，此处显式条件双重保险）。
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

                    // 选中槽选框（raised bevel：顶/左 亮、底/右 暗 → 凸起观感），随 selectedSlot 位移。
                    // 单独 overlay（不放进 Repeater）→ 选中态唯一；Behavior 让点击切换有平滑滑动感（同游戏内 hotbar）。
                    Item {
                        x: root.hotbar.selectedSlot * root.slotSize - 1
                        y: -1
                        width: root.slotSize + 2
                        height: root.slotSize + 2
                        Behavior on x { NumberAnimation { duration: 70; easing.type: Easing.OutQuad } }
                        // 选框四边统一白色（用户反馈右/下灰不协调 → 去 raised bevel 暗边）。
                        Rectangle { color: "#ffffff"; width: parent.width; height: 2; anchors.top: parent.top }
                        Rectangle { color: "#ffffff"; width: 2; height: parent.height; anchors.left: parent.left }
                        Rectangle { color: "#ffffff"; width: parent.width; height: 2; anchors.bottom: parent.bottom }
                        Rectangle { color: "#ffffff"; width: 2; height: parent.height; anchors.right: parent.right }
                    }
                }

                // ③ 销毁槽（点击 → 丢弃当前光标手持栈；setHeldBlock(0) 清 id+count）。
                // 自绘原创垃圾桶图标（Canvas 像素图，§9 override (a)）；凹陷斜面 + 暗红井底表「销毁」语义。
                // t138：原 DragHandler(右键)+DropArea「拖入销毁」与 root 右键 TapHandler 抢右键 grab → 右键拿半/均分
                //   手势失效；删 DragHandler/DropArea，销毁改纯点击（左键），右键全归 root TapHandler 独占。
                //   销毁能力不丢：点 hotbar 槽拾取到光标 → 点销毁槽丢弃（setHeldBlock(0) 同步清 count）。
                Item {
                    id: destroyWrap
                    width: root.slotSize
                    height: root.slotSize
                    anchors.right: parent.right

                    Rectangle { anchors.fill: parent; color: "#2a1414" }
                    Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
                    Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
                    Rectangle { color: "#7a3a3a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
                    Rectangle { color: "#7a3a3a"; width: 1; height: parent.height; anchors.right: parent.right }

                    // 自绘垃圾桶像素图（原创；无外部 PNG）。
                    Canvas {
                        anchors.centerIn: parent
                        width: 22; height: 22
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.imageSmoothingEnabled = false // 像素硬边（1.0 风格）
                            const lit = "#c9c9c9"   // 桶身亮色
                            const cut = "#2a1414"   // 桶身竖纹镂空（=井底色，形成竖条）
                            // 顶把手
                            ctx.fillStyle = lit; ctx.fillRect(8, 1, 6, 2)
                            // 桶盖
                            ctx.fillRect(4, 4, 14, 2)
                            // 桶身（梯形：上宽下窄）
                            ctx.beginPath()
                            ctx.moveTo(6, 7); ctx.lineTo(16, 7); ctx.lineTo(14, 19); ctx.lineTo(8, 19); ctx.closePath()
                            ctx.fillStyle = lit; ctx.fill()
                            // 桶身竖纹镂空
                            ctx.fillStyle = cut
                            ctx.fillRect(9, 9, 1, 8)
                            ctx.fillRect(12, 9, 1, 8)
                        }
                    }

                    // 点击销毁槽 → 丢弃当前光标手持栈（setHeldBlock(0) 一并清 id+count）。
                    // 仅左键：右键全归 root 右键 TapHandler 独占（t79 拿半/均分手势），避免再抢右键 grab（t138）。
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: root.hotbar.heldBlock = 0
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
