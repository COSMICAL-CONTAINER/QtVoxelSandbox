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
    // t49/t356：请求宿主把光标手持栈「丢出到世界」生成实体（拖出面板外释放 / 点遮罩区时）。宿主一律走
    //   player.dropHeldCursor 落地（创造 / 生存一致 —— t356 恢复创造拖出生实体，修 t292 把创造 dismiss 统一
    //   改成「凭空消失」致丢不出物的回归；调色板无限源的「归还」另走 returnHeldToVoidRequested，见下）。
    signal discardHeldRequested()
    // t228/t356：右键拖出面板外丢 1 件 → 宿主一律 player.dropHeldCursorOne（落地 1 实体，创造/生存一致）。
    //   左键整栈走 discardHeldRequested，右键逐个走本信号（spec「左键=全丢/右键=逐个」）。
    signal discardHeldOneRequested()
    // t356：创造调色板「归还光标手持物到虚空」（点原格切换归还 t318 / 换拿时旧物回虚空 t136）。调色板=无限源，
    //   「归还」即凭空消失（heldBlock=0）、不丢世界实体（t292 创造语义）。区别于 discardHeldRequested（拖出=丢世界）：
    //   t318 前二者共用 discardHeldRequested，且 t292 把该信号宿主改成「创造凭空消失」→ 创造拖出/丢热键一并变
    //   虚空、丢不出物。t356 把「归还虚空」与「丢出世界」拆成两个意图信号，互不串台（归还=虚空 / 拖出=世界）。
    signal returnHeldToVoidRequested()
    // t120：创造拿物品（调色板点击 → 拿到光标 / 手）→ 请求宿主弹手动画（Main.qml 接 handPopAnim.start）。
    //   机制等价生存拾取的手弹反馈，但创造无实体销毁、不发 player.itemPickedUp（那是实体拾取专用信号）；
    //   故经此信号让宿主单独触发手弹（spec「创造拿物品到手也触发 handPopAnim」）。仅调色板「无限源拿取」
    //   发，hotbar 槽间搬动 / 互换不算「拿新物」。
    signal itemTaken()

    // t511 二轮复盘：箱子 tab 点击 → 宿主切生存模式背包。分层（PLAN §2）：本面板只发意图，是否切模式由宿主
    //   （持 PlayerController）定。宿主 setMode(Survival) + inventoryOpen 保持 true（面板由 player.mode 绑定自动
    //   换成 SurvivalInventory）；heldBlock 是 hotbar VM 共享光标栈（跨创造/生存同一栈）→ 切模式天然保留，玩家可
    //   在创造护甲 tab 拿钻石护腿后点箱子 tab 切生存背包直接穿上。第一轮曾改成「保持创造综合页」被用户否决
    //   （「点箱子竟不切生存」）。
    signal switchToSurvivalRequested()

    // ① 调色板数据：t511 改为分类 tabs（MC 1.0 式）。currentTab 决定调色板只显某一类（方块 / 工具 / 材料 /
    //   护甲 / 食物）。各分类 id 段恒定（方块→creativeBlocks、工具→creativeTools、材料→creativeMaterials、
    //   护甲→creativeArmor），食物段从 creativeMaterials 里挑可食用子集（JS 端按 RecipeRegistry 材料段 id 常量
    //   过滤 —— 这些常量未单独 Q_INVOKABLE 暴露，故在 QML 端镜像一份 id 表）。root.hotbar 由 null→对象 时重求值。
    // t511 去掉「扩展空槽」（旧版尾巴 [0,0,...]）：分类 tabs 下每页内容已天然较短，空槽只会撑出滚动空白，删。
    // currentTab==5（箱子）= 保持创造的综合页（材料+护甲，filteredPalette case 5 合并显示；见下）。
    readonly property var paletteModel: root.hotbar ? root.filteredPalette() : []

    // t511 分类 tabs 当前页索引：0 方块 / 1 工具 / 2 材料 / 3 护甲 / 4 食物 / 5 箱子（综合页：材料+护甲，
    //   保持创造模式，便于创造直接拿起护甲穿上；不复用生存背包）。
    property int currentTab: 0

    // t511 食物段 id 表（从 creativeMaterials 里挑可食用子集；镜像 RecipeRegistry 材料段 id 常量，
    //   因 RecipeRegistry 未 Q_INVOKABLE 暴露给 QML，且全工程「可食用」判定散在 playercontroller 内联，
    //   无单一权威谓词可查 —— 故本处集中维护创造调色板用的食物 id 列表）。§9 通用词中文名由 nameForBlock 给。
    //   涵盖：面包 / 生·熟猪牛羊鸡肉 / 胡萝卜 / 马铃薯 / 蘑菇汤 / 苹果(无苹果物品) / 甜浆果 / 生鱼 / 蛋（食）。
    readonly property var foodIds: [
        0x20A, // 面包
        0x20B, // 生猪排
        0x20C, // 生牛肉
        0x221, // 熟猪排
        0x222, // 熟牛肉
        0x223, // 熟羊肉
        0x229, // 生鸡肉
        0x22A, // 熟鸡肉
        0x22B, // 蛋（可食）
        0x22F, // 胡萝卜
        0x230, // 马铃薯
        0x231, // 生鱼
        0x233, // 甜浆果
        0x23C  // 蘑菇汤
    ]

    // t508 船物品 id 表（spec「船归工具 tab，非材料 tab」）：OakBoatId=0x234 / SpruceBoatId=0x235。原入材料段
    //   （creativeMaterials），用户报「创造背包船归材料 tab，应放工具 tab」→ 改入工具段。下文 filteredPalette：
    //   工具 tab（currentTab===1）末尾追加；材料 tab（currentTab===2）显式排除（防双显）。id 与 hotbar.cpp /
    //   RecipeRegistry::OakBoatId/SpruceBoatId 同源（材料段 0x200+，非方块）。
    readonly property var boatIds: [0x234, 0x235]

    // t511 据 currentTab 过滤调色板 id 列表。食物段 = creativeMaterials 与 foodIds 的交集（保 materials 内顺序）。
    function filteredPalette() {
        if (!root.hotbar) return []
        if (root.currentTab === 0) return root.hotbar.creativeBlocks()
        if (root.currentTab === 1) {
            // t508 工具段末尾追加船（spec「船归工具 tab」）。船非工具类（ToolRegistry 枚举外）但语义上属
            //   「功能性载具」（同弓 / 剪刀 / 钓鱼竿 —— 右键使用、非放置），归工具段更合理。
            const tools = root.hotbar.creativeTools().slice()
            for (let i = 0; i < root.boatIds.length; ++i) tools.push(root.boatIds[i])
            return tools
        }
        if (root.currentTab === 2) {
            // 材料段 = creativeMaterials 去掉已划进食物段的项 + t508 去掉船（船已移到工具段，防双显）。
            const mats = root.hotbar.creativeMaterials()
            const out = []
            for (let i = 0; i < mats.length; ++i) {
                if (root.foodIds.indexOf(mats[i]) === -1 && root.boatIds.indexOf(mats[i]) === -1) out.push(mats[i])
            }
            return out
        }
        if (root.currentTab === 3) return root.hotbar.creativeArmor()
        if (root.currentTab === 4) {
            // 食物段：按 foodIds 顺序从 materials 取交集（materials 是权威 id 源；foodIds 仅定「哪些算食物 + 顺序」）。
            const mats = root.hotbar.creativeMaterials()
            const out = []
            for (let i = 0; i < root.foodIds.length; ++i) {
                if (mats.indexOf(root.foodIds[i]) !== -1) out.push(root.foodIds[i])
            }
            return out
        }
        // t511 二轮复盘：箱子 tab 不再用 currentTab=5 综合页（第一轮「保持创造」被否决）—— 点击箱子 tab 直接
        //   发 switchToSurvivalRequested 切生存背包（宿主 setMode(Survival)，本面板由 player.mode 绑定自动隐藏，
        //   SurvivalInventory 接管）。故 filteredPalette 无 case 5（currentTab 永不取 5）。
        return []
    }

    // 当前悬停方块的中文名（调色板/hotbar 槽 hover 时更新；§9 override (b) 中文通用词）。
    property string hoveredName: ""

    // t512 创造调色板 hover 物品 id（仅调色板单元格 hover 时非 0；区别于 hoveredItemId —— 后者 hotbar 槽
    //   hover 也写，故另设专属性区分「调色板无限源 hover」与「已有 hotbar 槽 hover」）。供宿主 Main.qml 数字键
    //   1-9 路由：调色板 hover + 1-9 → 强制替换对应 hotbar 槽（覆盖原物，不论原槽有无）；hotbar 槽 hover + 1-9
    //   → 仍走既有 swapHoveredWithHotbar 整栈互换（t110）。进入写 modelData、离开按 id 守卫清（防相邻格进出竞态互清）。
    property int creativeHoveredItemId: 0

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
    property int dragHeldDurability: 0      // t263 拖动期间手持工具耐久快照（松手回填光标保真）
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
    function resolveClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch) }
    function resolveRightClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count) { InventoryOps.writeSlot(root, group, index, id, count) }

    // ── t79/t98/t108/t167 拖动均分 + t110 Shift/数字键搬运 + t98 双击合并：算法见 InventoryOps
    //   （四面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
    //   创造背包仅 hotbar 行作均分目标（调色板=无限源，不作目标）；本面板无 craft 本地组（localReadSlot 缺省
    //   → InventoryOps 兜底空栈，安全）。
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
    // t205 右键拖拽绿框高亮：rightDragSlots 即「实际收到物品的格」，rightDragHasKey 判本格是否在集。
    function rightDragHasKey(key) { return InventoryOps.rightDragHasKey(root, key) }
    // redistributeLive / singleLeftClick：纯内部辅助（仅 InventoryOps.addDragSlot / endLeftDrag 调用），
    //   算法已入 InventoryOps，此处不再持有副本。
    function slotShiftLeft(group, index) { InventoryOps.slotShiftLeft(root, group, index) }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }

    // t512 创造调色板 hover 物品 + 数字键 1-9 → 强制替换对应 hotbar 槽（覆盖原物，不论原槽有无物品）。
    //   机制等价 MC 1.0 创造模式 hotbar：hover 创造物品按 1-9 直接把一组该物品塞进对应 hotbar 槽（1→槽0 ...
    //   9→槽8）。区别于 swapHoveredWithHotbar（t110 整栈互换、需源槽）：调色板=无限源，无源槽概念 → 直接 setStack
    //   覆盖目标槽（不读原槽、不还光标）。count 走 maxStackSize（方块/材料 64、工具/桶/护甲 1）；durability=-1
    //   （VM normalizeDurability 自动满耐久 = 创造取新物语义）；enchants 空（无附魔，创造取新物语义）。
    //   分层（PLAN §2）：本面板只做槽位改写（经 hotbar VM），宿主 Main.qml 负责按键路由 + hover 态提升。
    function forceReplaceHotbarFromCreative(hotbarIdx) {
        if (!root.hotbar) return
        const id = root.creativeHoveredItemId
        if (id === 0) return
        root.hotbar.setStack(hotbarIdx, id, root.hotbar.maxStackSize(id))
    }

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
    // t158：acceptedButtons 含左键（原全键 MouseArea 抢 right press 致 per-slot 右键 TapHandler 永不触发）。
    //   t228：再加右键（右键拖出 = 丢 1 件）；右键在槽区由 per-slot TapHandler 优先抓（resolveRightClick），
    //   到本 MouseArea 的右键必是面板外释放。左键点遮罩 → 丢整栈；右键点遮罩 → 丢 1 件；左键在槽区拖动 →
    //   DragHandler 接管均分（t167）。
    //   t228 边界判定：点击位置在**面板矩形内**（含标题 / 状态行 / 间距等非槽位）→ **不丢**（物品留光标），
    //   修「左键拿物在面板内非槽位松手 → 直接丢地下」bug（原 mask 无边界判定，面板内空点击穿透即丢）。
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
            spacing: 8

            // t511 分类 tabs（MC 1.0 式创造背包分类）：方块 / 工具 / 材料 / 护甲 / 食物 / 生存模式背包。
            //   前 5 项切 currentTab（调色板只显该类，filteredPalette 过滤）；生存模式背包 tab（原「箱子」tab，
            //   t511 二轮复盘改名）→ 发 switchToSurvivalRequested 由宿主切 player.mode=Survival（inventoryOpen 保持
            //   true，面板由 mode 绑定自动换成 SurvivalInventory；heldBlock 共享光标栈天然保留跨切）。
            //   选中态：白底深字 + 下沉边；未选：暗底亮字。自绘原创（§9 override (a)，无 MC GUI PNG）。
            //   去掉旧「创造物品栏」标题 + 「[E]/[Esc] 关闭」提示（用户嫌啰嗦；关闭键提示已在 HUD/暂停叠层）。
            Row {
                id: tabBar
                spacing: 2
                width: parent.width

                Repeater {
                    // [标签, 对应 currentTab（-1=生存模式背包特殊，不进 currentTab）]。
                    model: [
                        { label: "方块", tab: 0 },
                        { label: "工具", tab: 1 },
                        { label: "材料", tab: 2 },
                        { label: "护甲", tab: 3 },
                        { label: "食物", tab: 4 },
                        { label: "生存模式背包", tab: -1 }
                    ]
                    delegate: Rectangle {
                        width: Math.floor((parent.width - (6 - 1) * 2) / 6)
                        height: 26
                        // t511 二轮复盘：生存模式背包 tab（tab:-1）发 switchToSurvivalRequested 切生存，不再用
                        //   currentTab=5 综合页。本 Inventory 面板只在 Creative 显（Main.qml 绑定），切到 Survival 后
                        //   自动隐藏 → 该 tab 永远不会有「选中态」（选中态只在 SurvivalInventory 里体现，那边独立）。
                        //   故 isSelected 判据保留 (tab===-1 ? false : currentTab===tab)，-1 恒未选（切走即隐）。
                        property bool isSelected: (modelData.tab === -1)
                                                  ? false
                                                  : (root.currentTab === modelData.tab)
                        color: isSelected ? "#5a8a4a" : "#262b30" // 选中绿底 / 未选暗底
                        border.color: isSelected ? "#7fe57f" : "#3a444f"
                        border.width: 1
                        radius: 3
                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: isSelected ? "#ffffff" : "#9fb0c0"
                            font.pixelSize: 12
                            font.bold: isSelected
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                if (modelData.tab === -1) {
                                    // t511 二轮复盘：生存模式背包 tab → 发 switchToSurvivalRequested 由宿主切生存。
                                    //   heldBlock 是 hotbar VM 共享光标栈（跨创造/生存同一栈）→ 切模式天然保留，
                                    //   玩家可在护甲 tab 拿钻石护腿后点本 tab 切生存背包穿上。第一轮「保持创造综合页」
                                    //   被用户否决（「点箱子竟不切生存，应切过去」）。
                                    root.switchToSurvivalRequested()
                                } else {
                                    root.currentTab = modelData.tab
                                }
                            }
                        }
                    }
                }
            }

            // ① 调色板（Flickable 垂直可滚动 + ScrollBar 指示拖动；t127）。
            //   t511：去掉标题行 / 状态行后腾出纵向空间 → 视口抬到 cellSize*4+16 容 4 行（分类页内容短时一屏全显；
            //   长（方块页 ~60 项）仍可滚）。ScrollBar.vertical policy=AsNeeded 即不足时不占空间。
            Flickable {
                id: paletteFlick
                width: parent.width
                height: root.cellSize * 4 + 16 // t511：视口容 4 行（去标题/状态行腾出的空间回填到调色板）
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
                                    toolType: root.hotbar.toolType(modelData)
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
                                        root.creativeHoveredItemId = modelData   // t512 调色板 hover（数字键 1-9 强制替换源）
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                    } else {
                                        if (root.hoveredItemId === modelData) root.hoveredItemId = 0
                                        // t512 离开调色板格：按 id 守卫清（防相邻格进出竞态互清，同 hoveredItemId 模式）。
                                        if (root.creativeHoveredItemId === modelData) root.creativeHoveredItemId = 0
                                    }
                                }
                            }
                            TapHandler {
                                enabled: modelData !== 0
                                // 拾取到光标（创造调色板=无限源，不清减调色板）。方块满栈 64；工具不可堆叠 →
                                // count=1（t33）。setHeldBlock 已对工具段 id 校验合法（isValidItemId 含工具段）。
                                onTapped: {
                                    // t318：切换式归还（修 t292 遗留「点原格又拿起该格」）。创造调色板=无限源，
                                    //   点「当前手持物同格（原格）」= 放回（heldBlock===modelData → returnHeldToVoidRequested，
                                    //   凭空消失回虚空，创造不丢世界，t292）；再点同格 = 重新拿起。旧版无脑 dismiss+re-pick
                                    //   → 点原格 dismiss 后立刻赋同值（heldBlock 复原），用户观感「没归还、重复拾取」。
                                    //   现 heldBlock===modelData 早退走归还，构成 true toggle（拿起→点原格归还→再点拿起）。
                                    //   t356：归还走 returnHeldToVoidRequested（=虚空），不复用 discardHeldRequested（=丢世界实体），
                                    //   否则 t318 归还路径会把「丢世界」意图与「回虚空」混淆。
                                    if (root.hotbar.heldBlock === modelData) {
                                        root.returnHeldToVoidRequested()
                                        return
                                    }
                                    // t136/t292：换拿前先显式 dismiss 旧光标手持栈（防被下方赋值直接覆盖成「凭空消失」
                                    //   的隐性路径——显式走信号让宿主统一处理）。创造调色板=无限源，旧物 dismiss 即回
                                    //   虚空（heldBlock=0，t292：不丢出到世界）；信号同线程直连，返回时 heldBlock 已为 0，
                                    //   随后赋新值安全。空手（heldBlock===0）跳过。异格（heldBlock!==modelData）走此分支 =
                                    //   换拿（旧物回虚空 → 新物上手，MC 创造调色板语义）。t356：同走 returnHeldToVoidRequested。
                                    if (root.hotbar.heldBlock !== 0) root.returnHeldToVoidRequested()
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

            // t511 去掉「点击右侧销毁槽可丢弃当前手持物」提示 Text（用户嫌啰嗦；销毁槽的垃圾桶图标已自解释）。

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
                                        toolType: { root.hotbar.slotRevision; return root.hotbar.toolType(slotId) }
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
                                        const r = root.resolveClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index), root.hotbar.durabilityAt(index), root.hotbar.enchantsAt(index))
                                        if (r) {
                                            root.hotbar.setStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
                                            root.hotbar.heldBlock = r.heldId
                                            root.hotbar.heldCount = r.heldCount
                                            root.hotbar.heldDurability = r.heldDur
                                            root.hotbar.setHeldEnchants(r.heldEnch)
                                        }
                                    }
                                }
                                // t166d：per-slot 右键（拿半/放一），不依赖 hover/hoveredKey（同左键 per-slot 模式，可靠）。
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: {
                                        const r = root.resolveRightClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index), root.hotbar.durabilityAt(index), root.hotbar.enchantsAt(index))
                                        if (r) {
                                            root.hotbar.setStack(index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
                                            root.hotbar.heldBlock = r.heldId
                                            root.hotbar.heldCount = r.heldCount
                                            root.hotbar.heldDurability = r.heldDur
                                            root.hotbar.setHeldEnchants(r.heldEnch)
                                        }
                                    }
                                }
                                // t167 均分拖拽高亮（扫过且待分发的合格格绿框；leftDragActive 期间才显）。
                                // 异物槽纵使被扫过也不亮（addDragSlot 已过滤入 dragSlots，此处显式条件双重保险）。
                                // t205：右键拖拽（每格放1）同样显绿框 —— rightDragSlots 仅含「实际放了物」的格
                                //   （addRightDragSlot 据 placeOneInSlot 成败收录），故 rightDragHasKey 即「此格已放」。
                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.color: "#7fe57f"; border.width: 2
                                    visible: {
                                        root.dragSlots; root.rightDragSlots
                                        root.leftDragActive; root.rightDragActive; root.hotbar.slotRevision
                                        const key = root.slotKey("hotbar", index)
                                        if (root.leftDragActive && root.dragHasKey(key)
                                            && (slotId === 0 || slotId === root.dragHeldId)) return true
                                        return root.rightDragActive && root.rightDragHasKey(key)
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
    // t263 当前 hover 槽的工具剩余耐久（-1=未跟踪：创造调色板 / 本地槽 / 非工具 → tooltip 不显耐久行）。
    //   据 hoveredKey（"组:下标"）查 hotbar/main 真实耐久；触碰 slotRevision/mainRevision 令搬运后刷新。
    property int hoveredDurability: {
        if (!root.hotbar || !root.hoveredItemId || !root.hotbar.isTool(root.hoveredItemId)) return -1
        root.hotbar.slotRevision; root.hotbar.mainRevision  // 触碰：栈写入后重算
        const key = root.hoveredKey
        if (!key) return -1
        const parts = key.split(":")
        if (parts.length !== 2) return -1
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return -1
        if (parts[0] === "hotbar") return root.hotbar.durabilityAt(idx)
        if (parts[0] === "main") return root.hotbar.mainDurabilityAt(idx)
        return -1  // 本地槽（craft 等）不持耐久 → 不显
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
            // t263 工具槽 tooltip 附「cur/max」耐久行（如「铁镐  5/250」）；非工具 / 未跟踪 → 仅显名。
            text: root.hotbar ? (root.hotbar.nameForBlock(root.hoveredItemId)
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")
                + (root.hotbar.toolType(root.hoveredItemId) === 7 ? "  攻击 1-" + root.hotbar.bowArrowMaxDamage() : "")) : "" // t304 弓伤害 tooltip
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
