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

// 生存模式物品栏 1.0（t24）：E 键开关（仅 Survival 模式 —— 宿主 Main.qml 已按模式分流：Survival
// 开本屏、Creative 开 t23 创造背包、Spectator E 无反应）。
//
// 贴近 MC 1.0 生存背包布局（spec 验收项；左右方位修正：人物装备栏在**左**、合成在**右**，对齐 1.0）：
//   ① 左上 4 护甲槽（头 / 胸 / 腿 / 脚，纵向）+ 角色预览（自绘人形剪影占位；真实装备 / 3D 模型属 Phase 1.1）；
//   ② 右上 2×2 合成格 + 箭头 + 结果槽（合成**功能**属 Phase 1.1；槽位本身参与 t38 物品移动）；
//   ③ 下部 3×9=27 主栏（t32/t38：本地 ItemStack store，左键整组拾取 / 放置 / 合并 / 互换）；
//   ④ 最底 9 槽 hotbar 栏（同步游戏内 hotbar：读 hotbar VM，左键同 t38 栈操作 + 选中该槽）。
//
// 本组件做**呈现 + 左键整组栈操作**：方块集 / 图标 / 中文名 / 槽位数据全部经注入的 hotbar VM
// （ViewModel 读 BlockRegistry，PLAN §2 分层：UI 不另持方块表副本）。主栏 / 合成格为本地 ItemStack
// store（与 hotbar 9 槽共享同一 hotbar VM 的 heldBlock/heldCount 光标手持栈）；合成配方解析 / 护甲装备
// 真实逻辑属 Phase 1.1。全部槽框 / 护甲图 / 角色预览本项目自绘原创（InvSlot 凹陷槽 + Canvas 像素图，
// 无外部 MC GUI PNG；§9 override (a)）。零 MC 专有名词（§9）。
//
// 宿主负责指针态：背包打开时已 release（光标可见，可点 hotbar 槽）；关闭（closed 信号）→ 宿主恢复 grab。
// 光标手持物浮动图标在宿主 Main.qml（z=300，enabled:false 不挡点击 —— 见 t37 修复）。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（提供 slotList / iconSourceForBlock / nameForBlock /
    // nameAt / selectedSlot / slotRevision）。
    property Hotbar hotbar
    // t551 宿主注入 PlayerController（Main.qml `player: player`，同 FurnaceUI/AnvilUI 模式）。角色预览
    //   CharacterPreview3D 经 `player: root.player` 传入（不能写 `player: player` —— 裸名会 shadow 成
    //   CharacterPreview3D 自身的同名属性 → 自引用恒 null，经验：传属性给子组件用 `root.xxx` 显式路径）。
    property var player: null
    // progress 玩家进度注入（Main.qml 经 `progress: progress` 绑定）：批量合成（InventoryOps
    //   slotShiftLeftCraft）统计 / 成就埋点用。InventoryOps .js 无 QML 全局 id 访问权 → 经 root 传，同 hotbar 模式。
    property var progress
    // 请求宿主关闭背包（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区时；宿主接 player.dropHeldCursor）。
    signal discardHeldRequested()
    // t228：请求宿主把光标手持栈**丢 1 件**为实体（右键拖出面板外；宿主接 player.dropHeldCursorOne）。
    //   左键整栈走 discardHeldRequested，右键逐个走本信号（spec「左键=全丢/右键=逐个」）。
    signal discardHeldOneRequested()
    // t345 护甲装备 / 脱下发生（装备槽 TapHandler 触发）→ 宿主播装备音（spec「equip/unequip SOUND」；
    //   当前复用 playPlace 作过渡装备音，专用护甲音属后续资产任务）。
    signal armorChanged()

    // t551 看鼠标指针：光标**窗口坐标**直接绑 Main.qml 的常驻 cursorTracker（overlayRoot 级 HoverHandler，
    //   review-12 修：point.globalPosition 在 Qt 6.11 HandlerPoint 不存在（绑 undefined → 恒 (0,0) 卡死最大
    //   转头角）；point.position 是窗口坐标且 bindable（同 Main.qml 光标浮动图标已验证用法）。
    //   喂给 CharacterPreview3D → 3D 人物转身/转头/抬头看鼠标。
    property point previewMouseScene: cursorTracker.point.position

    // ── 尺寸常量（集中一处便于对齐）──
    readonly property int slotSize: 40        // 统一槽尺寸（主栏 / hotbar / 合成 / 护甲同尺寸，贴近 1.0）
    readonly property int mainCols: 9
    readonly property int mainRows: 3
    readonly property int armorCount: 4

    // 主栏 / 合成格的物品栈存储。主栏（27 槽）自 t97 起上移至 hotbar VM（m_mainSlots），三菜单（生存背包 /
    // 工作台 / 熔炉）共享同一份 → 三菜单主栏同步、returnHeldToHotbar/pickupScan 经 addToAny 能合并进主栏
    // （修「主栏不同步 / 丢弃回栏不合并」根因）。合成格（2×2）仍本地（合成态属本面板，关包归还）。
    // 真实物品系统 / 合成配方解析属 Phase 1.1；本屏先支持点击拾取/放置，把「物品在背包内移动」核心交互打通
    // ——与 hotbar 槽共享同一 hotbar VM 的 heldBlock/heldCount 光标手持栈。air=0=空栈。数组元素改写不触发
    // QML 绑定，故配 craftRev 版本号让 Image source / count 重算（主栏走 VM 的 mainRevision NOTIFY）。
    // t49：主栏初始全空（spec point 2「空背包起」；VM 构造期 m_mainSlots 全 0）。
    property var craftSlots: [0,0,0,0] // 2×2 合成格（占位；配方解析属 Phase 1.1，结果槽暂不产出）
    property var craftCounts:[0,0,0,0] // 平行数量
    property int craftRev: 0

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    // DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（处理单点拾取/放置/合并/互换），
    // 一旦拖动越阈值 → DragHandler 激活夺抓（per-slot TapHandler 释）→ onActiveChanged 驱动 begin/endLeftDrag；
    // 逐槽 HoverHandler 在 leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。
    // dragSlots 存「组:下标」字符串（去重 / 比较简单）；leftDragActive 标手势进行中；dragHeld* 为按下瞬间光标
    // 栈快照（均分按原始量算，避免拖拽中途槽内容变化干扰）；dragOriginal/dragWritten 支撑实时重分的撤销机制
    // （每滑入新格先撤销上轮写入再重分）。背包主栏 / hotbar / 合成格统一支持；t203 起合成格经 localDragGroups
    //   声明亦参与分发（左键均分 / 右键每格放1），与主栏/hotbar 同。
    // 均分算法与 t79/t98 右键拖拽同源（右键拖拽 t166d 改 per-slot 单点后停用）；t167 把同一算法接到左键。
    property bool leftDragActive: false
    property var dragSlots: []              // 字符串数组（"craft:2" / "main:5" / "hotbar:0"）
    property string hoveredKey: ""          // 当前指针所在槽 key（HoverHandler 维护；beginLeftDrag 取起点槽）
    property int dragHeldId: 0
    property int dragHeldCount: 0
    property int dragHeldDurability: 0      // t263 拖动期间手持工具耐久快照（松手回填光标保真）
    // t566 修「左键均分失效」根因：t475 的 InventoryOps.beginLeftDrag/redistributeLive 读写 root.dragHeldEnchants，
    //   但本面板（及其它七面板）从未声明该属性 → JS 给 QML Item 不存在的属性赋值抛 TypeError，被 DragHandler
    //   信号处理器吞掉（lessons t205：QML 信号处理器内 JS 异常静默退化）→ leftDragActive 永远没置 true →
    //   左键拖动均分全失效（右键路径 beginRightDrag 不碰该属性 → 幸存）。补声明即恢复。
    property var dragHeldEnchants: []       // t475 拖动期间手持附魔快照（工具 / 护甲不可拆分，松手回填光标保真）
    property string dragHeldName: ""        // t622 拖动期间手持实例名快照（松手 / 早退回填光标保真）
    // t181 右键拖动（每格放 1 个；区别于左键 floor(count/N) 均分）。rightDragSlots 存已放格（去重，每格只放
    //   一次）；rightDragPlaced 标全程是否真放置过（空手 / 异 id 已满时为 false → endRightDrag 退化为单格右键）。
    //   dragActive 统一左/右拖动收集门控（HoverHandler 据 dragActive 调 addDragSlot，InventoryOps 内分发）。
    property bool rightDragActive: false
    property var rightDragSlots: []
    property bool rightDragPlaced: false
    property bool dragActive: leftDragActive || rightDragActive
    // t98 实时重分撤销机制：dragOriginal 记每槽 drag 前原始栈（首次 encounter 快照）；dragWritten 记本轮
    // 已写槽。每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginLeftDrag / endLeftDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    // t98 双击合并：lastTapMs/lastTapKey 记上次左键点击（槽 key）的时间戳与 key；400ms 内同槽二次点击 →
    // doMergeSameId（扫 main+hotbar 同 id 累加成满栈 64 一组、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t203：2×2 合成格接入完整快捷操作（右键放1 / 右键拖拽每格放1 / 左键拖动均分 / 双击拿同类），与主栏/
    //   hotbar 同（对齐 t180 工作台 craft 3×3）。声明 craft 为可拖拽本地组 → InventoryOps.groupIsDraggable
    //   放行（addDragSlot 收集、redistributeLive 左键均分分发、addRightDragSlot 右键每格放1、doMergeSameId
    //   扫 craft）。旧版 t180 注释「生存背包 2×2 craft 维持不参与」在此任务转为参与——合成格是纯输入槽
    //   （无熔炉 out 那种「异物污染输出槽阻断冶炼」的顾虑），左/右拖拽填入是 MC 标准交互；recipeMatch 据
    //   craftRev 自动重算，均分后布局若变由用户重排（同 CraftingTableUI）。
    property var localDragGroups: ["craft"]
    // t203：craft 组槽位数（doMergeSameId 扫描范围）。craftSlots 长 4（2×2）。
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
    // t38 左键整组（拾取/放置/合并/互换 4 case）+ t49 右键半份：算法见 InventoryOps.resolveClick /
    //   resolveRightClick（四面板共享）。返回 {slotId,slotCount,heldId,heldCount} 或 null=无操作；调用方
    //   据返回值写对应槽 + 更新 held（手持栈由 hotbar VM 单一持有，PLAN §2 VM 单一权威）。
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
    // t181 右键拖动（每格放 1 个）：薄委托包装，供 root DragHandler(RightButton) / HoverHandler 调用。
    function beginRightDrag() { InventoryOps.beginRightDrag(root) }
    function endRightDrag() { InventoryOps.endRightDrag(root) }
    function addRightDragSlot(key) { InventoryOps.addRightDragSlot(root, key) }
    // t205 右键拖拽绿框高亮：rightDragHasKey 判本格是否在 rightDragSlots（「实际放了物」的格集）。
    function rightDragHasKey(key) { return InventoryOps.rightDragHasKey(root, key) }
    // redistributeLive / singleLeftClick：纯内部辅助（仅 InventoryOps.addDragSlot / endLeftDrag 调用），
    //   算法已入 InventoryOps，此处不再持有副本。

    // t110 Shift+左键搬运（main↔hotbar）+ 数字键交换 + t98 双击合并：算法见 InventoryOps（四面板共享）。
    function slotShiftLeft(group, index) { InventoryOps.slotShiftLeft(root, group, index) }
    function swapHoveredWithHotbar(hotbarIdx) { InventoryOps.swapHoveredWithHotbar(root, hotbarIdx) }
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }
    // t230 生存 2×2 Shift+左键结果槽 → 批量合成（耗尽最小原料数；产物入背包非光标）。
    function slotShiftLeftCraft() { InventoryOps.slotShiftLeftCraft(root) }

    // t50 合成检测：读 2×2 craftSlots（行优先：[0]=TL [1]=TR [2]=BL [3]=BR）查 RecipeRegistry::match
    // （经 hotbar.recipeMatch 透传）。返回匹配配方的 QVariantMap（outputId/outputCount/consumeCount）或
    // 空 Map（无匹配）。触碰 craftRev 让绑定刷新时重算。
    function matchedRecipe() {
        if (!root.hotbar) return null
        root.craftRev
        const m = root.hotbar.recipeMatch(root.craftSlots, 2)
        return (m && m.outputId !== undefined) ? m : null
    }

    // t50 点击结果槽 → 合成（MC 式：消耗每原料 1、产出 outputCount 到光标；剩余留槽可连点）。
    function craftOne() {
        if (!root.hotbar) return
        root.craftRev
        const r = root.matchedRecipe()
        if (!r) return
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        const cap = root.hotbar.maxStackSize(r.outputId)
        if (!root.hotbar.recipeCanTake(r.outputId, r.outputCount, heldId, heldCount, cap)) return
        // 消耗：每个非空原料格 count-1（归 0 清 id）。原料用量恒 1。
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
        // t603 修：旧 `window.progress` 恒 undefined（Window 未声明该属性；progress 是 Main.qml 内 id 非
        //   window 属性）→ 单次合成上报从未发出。改用注入的 root.progress（同 CraftingTableUI t603）。
        if (root.progress) root.progress.onCraft(r.outputId)  // progress 统计合成 + 成就
    }

    // t49 关包归还合成栏（spec point 6）：面板隐藏（visible→false）时把 craftSlots 内容 addStack 回 hotbar
    // （合并同类，同拾取），清空 craftSlots。MC 行为：合成格不持久化，关包即退回玩家背包。仅本屏有合成格。
    // 初始 craftSlots 全 0 → 首次 onVisibleChanged（构造期 visible=false）遍历为空，无副作用。
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

    // t49：关包（visible→false）归还合成栏。visible=true（打开）时不动作。
    onVisibleChanged: if (!visible) returnCraftToHotbar()

    // t167 左键拖动均分总控：DragHandler(LeftButton) 在 root 监听。按下不动时 per-slot 左键 TapHandler 抓
    //   （单点拾取/放置/合并/互换 / Shift 搬运 / 双击合并）；一旦拖动越阈值，per-slot TapHandler 按 DragThreshold
    //   默认释抓、DragHandler 激活并夺抓 → onActiveChanged 驱动 begin/endLeftDrag。逐槽 HoverHandler 在
    //   leftDragActive 期间收集扫过格（addDragSlot 即 redistributeLive 实时重分）。target:null 防 DragHandler
    //   默认拖动父 Item（面板）；默认阈值即可区分「点击」与「拖动」。遮罩 MouseArea(LeftButton) 仅在面板外
    //   区域抓左键（点遮罩丢弃手持），不与本 handler 冲突（DragHandler 在槽区激活）。
    DragHandler {
        acceptedButtons: Qt.LeftButton
        target: null
        onActiveChanged: {
            if (active) root.beginLeftDrag()
            else root.endLeftDrag()
        }
    }
    // t181 右键拖动（每格放 1 个）：DragHandler(RightButton) 在 root 监听。按下不动时 per-slot 右键 TapHandler
    //   抓（拿半 / 放一）；拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endRightDrag。逐槽
    //   HoverHandler 在 dragActive 期间收集（addDragSlot 据 rightDragActive 分发到 addRightDragSlot）。target:null
    //   防 DragHandler 拖动父 Item；与左键 DragHandler 独立（各 acceptedButtons 单按钮）。
    DragHandler {
        acceptedButtons: Qt.RightButton
        target: null
        onActiveChanged: {
            if (active) root.beginRightDrag()
            else root.endRightDrag()
        }
    }

    // 半透明遮罩：仅吸收点击（防穿透到背后游戏层），**不关闭背包**——用户要求背包只能 E / Esc 关闭。
    // t49：手持物时点遮罩区（面板外）→ 整栈丢弃为实体（同 Q 丢弃）。t158：acceptedButtons 限左键（原默认
    //   全键的 MouseArea 抢 right press，致 per-slot 右键 TapHandler 永不触发）；限左键后右键透到槽 per-slot
    //   TapHandler（resolveRightClick 生效）。t228：再加右键（右键拖出 = 丢 1 件）+ 面板边界判定（面板内非槽位
    //   松手→不丢，修「左键拿物在面板内非槽位松手→直接丢地下」bug）。左键在槽区拖动 → DragHandler 接管均分。
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

    // 面板：深色圆角，居中。尺寸由内容（标题 + 顶部区 + 主栏 + hotbar）精确推出。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 2×16 边距 = 392
        // t158：恢复 t129 前的 410。t129 曾 +170（→580）容纳底部手臂调试 ArmSlider 区（⑤），
        //   t139 把 ArmSlider 迁到 ESC 设置面板时未回缩高度 → 底部留 ~170px 空白、hotbar 行悬在面板中部
        //   （用户报「生存背包底部手槽区空缺」）。回 410 后 hotbar 贴底、还原 MC 1.0 生存背包布局。
        height: 410
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
                    text: "生存物品栏"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ① 顶部区（左：护甲 + 角色预览；右：合成）—— 修正：人物装备栏在左，对齐 MC 1.0。高度由护甲 4 槽纵向决定。
            Item {
                width: root.mainCols * root.slotSize   // 360
                height: root.armorCount * root.slotSize // 160

                // 2×2 合成格（右上）：占位空槽（合成功能属 Phase 1.1）。左右修正：合成在右，对齐 MC 1.0。
                Grid {
                    x: parent.width - root.slotSize - 24 - 4 - 80; y: 40
                    columns: 2; spacing: 0
                    Repeater {
                        model: 4
                        delegate: Item {
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" }
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
                                // t50 材料段（木棒）：MaterialIcon 自绘（§9a 原创，非 MC 资产）。
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { const _r = root.craftRev; return _r >= 0 ? (root.hotbar.isMaterial(root.craftSlots[index] || 0)) : false }
                                    materialId: { const _r = root.craftRev; return _r >= 0 ? (root.craftSlots[index] || 0) : 0 }
                                }
                            }
                            // 栈数量（t32）：count>1 时右下角显数字。触碰 craftRev 刷新（数组突变靠版本号触发）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { const _r = root.craftRev; return _r >= 0 ? ((root.craftCounts[index] || 0) > 1) : false }
                                text: { const _r = root.craftRev; return _r >= 0 ? (root.craftCounts[index] || 0) : "" }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // t203：craft 2×2 接入完整快捷操作（与主栏/hotbar/工作台 craft 3×3 同）：左键整组
                            //   （resolveClick）+ 双击拿同类（doMergeSameId 扫 main+hotbar+craft）+ 左键拖动均分
                            //   （root DragHandler + 逐槽 HoverHandler 收集，redistributeLive 分发）+ 右键放1/拿半
                            //   （resolveRightClick）+ 右键拖拽每格放1（addRightDragSlot）。Shift+左键搬运对 craft
                            //   组无操作（不在 main↔hotbar 范畴）。配方解析走 recipeMatch（据 craftRev 自动重算）。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    // t110：Shift+左键搬运（craft 槽不在 main↔hotbar 范畴，slotShiftLeft 对 craft
                                    //   组无操作；普通左键走 resolveClick）。
                                    if (window.shiftHeld) { root.slotShiftLeft("craft", index); return }
                                    // t203 双击拿同类（同 main/hotbar/工作台 craft 3×3）：280ms 内同槽二次点击 →
                                    //   doMergeSameId（扫 main+hotbar+craft 同 id 累加成满栈、余数留光标）。
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
                            // t203 per-slot 右键（拿半/放一），与主栏/hotbar/工作台 craft 3×3 同（不依赖
                            //   hover/hoveredKey）。空手→拾取 floor(count/2)（单件取 1）；持物→放 1（空槽开新栈 /
                            //   同 id 未满 +1；异 id / 已满无操作，不互换）。
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
                                    // t167：左键拖动期间进入新格 → 收集（MC Java 集合只增不减：回滑不撤销已选格，
                                    // 故无 leave-remove 分支；redistributeLive 据 N 实时重分）。
                                    if (hovered && root.dragActive) {
                                        root.addDragSlot(key)
                                    }
                                }
                            }
                            // t167 均分拖拽高亮（扫过且待分发的合格格绿框；leftDragActive 期间才显）。
                            // 异物槽纵使被扫过也不亮（addDragSlot 已过滤入 dragSlots，此处显式条件双重保险）。
                            // t205：右键拖拽每格放1 同样亮 rightDragSlots 中的格（实际放了物的格）。
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

                // 合成箭头（指向结果槽）：自绘像素图（§9 override (a)）。居中于右侧合成区（y 居中 160 高）。
                Canvas {
                    x: parent.width - root.slotSize - 24; y: 70
                    width: 24; height: 20
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        ctx.imageSmoothingEnabled = false // 像素硬边（1.0 风格）
                        ctx.fillStyle = "#8a8a8a"
                        ctx.fillRect(0, 8, 16, 4)                          // 箭杆
                        ctx.beginPath()                                    // 箭头三角
                        ctx.moveTo(16, 2); ctx.lineTo(24, 10); ctx.lineTo(16, 18); ctx.closePath()
                        ctx.fill()
                    }
                }

                // t50 结果槽（合成输出，最右）：显匹配配方产物图标；点击 → 合成一批（消耗每原料 1、
                // 产出 outputCount 到光标；剩余留槽可连点）。无匹配时空。居中于右侧合成区（y 居中 160 高）。
                Item {
                    x: parent.width - root.slotSize; y: 60
                    width: root.slotSize; height: root.slotSize
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                    Item {
                        anchors.centerIn: parent
                        width: 30; height: 30
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
                    //   左键非 Shift / 右键 → 单次合成 craftOne（消耗每原料 1、产出 outputCount 到光标）。
                    //   MC 1.0：Shift+左键结果槽才批量；Shift+右键 / 右键均单次。
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onTapped: {
                            if (window.shiftHeld) { root.slotShiftLeftCraft(); return }
                            root.craftOne()
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: root.craftOne()
                    }
                }

                // 角色预览（护甲右侧，左半区）：t546 3D 玩家模型预览（第三人称视角，复用 Main.qml playerModel
                // 几何/配色 + 4 装备槽护甲 overlay），替代 2D Canvas 人形剪影。80 宽 × 160 高。
                // t551 注入 player（跟玩家走/蹲/跳动作）+ mouseScene（看鼠标指针，root 级 HoverHandler 追踪）。
                // t573 x 回调（用户「3D 模型太右了，往左一点」）：t551 从 slotSize+6 一口气移到 slotSize*2+6
                //   (=86) 过头 → 回调到 slotSize*2-10 (=70)，3D 人物目测居中于装备栏与合成区之间的空位。
                Item {
                    x: root.slotSize * 2 - 10
                    y: 0
                    width: 80
                    height: parent.height
                    CharacterPreview3D {
                        anchors.fill: parent
                        hotbar: root.hotbar
                        showHitboxes: window.showHitboxes
                        player: root.player
                        mouseScene: root.previewMouseScene
                    }
                }

                // 4 护甲槽（最左，纵向：头 / 胸 / 腿 / 脚）：t345 起接入 hotbar VM 装备栏（armorBlockIdAt 等）。
                //   槽 index 与 ArmorRegistry::ArmorPiece 同序（0 头盔 / 1 胸甲 / 2 护腿 / 3 靴子）。
                //   点击：持护甲且部位匹配该槽 → 装备（与槽内旧护甲互换到光标）；空手点有护甲槽 → 脱下到光标。
                //   部位不符 / 非护甲持物 → no-op（MC 行为：头盔不进胸甲槽）。悬停 → tooltip 显护甲名 + 护甲值。
                Column {
                    x: 0   // 最左（人物装备栏在左，对齐 MC 1.0）
                    y: 0
                    spacing: 0
                    Repeater {
                        model: root.hotbar.armorCount
                        delegate: Item {
                            // 装备槽栈 id / 数量 / 耐久（触碰 armorRevision → 装备 / 脱下 / 破损后重算；air=0 空）。
                            // t498 二轮复盘：改表达式形式 `armorRevision >= 0 ? xxxAt(index) : 0`（原语句块形式
                            //   `{ armorRevision; return xxxAt(index) }` 在低频 NOTIFY（armorRevision 仅装备/脱下/破损
                            //   时变）+ 静稳态下不可靠注册 NOTIFY 依赖 → 装备/脱下后 armId/armDur/armEnch 恒旧值。
                            //   后果：装备路径读 oldId=armId 读到旧值（脱下前的护甲 id，非空）→ 把「幻影旧件」写回
                            //   光标（heldBlock=oldId）→ 槽位已装备新件 + 光标也持有旧件 = 护甲复制。改表达式形式后
                            //   NOTIFY 属性参与值计算、依赖被可靠注册，装备/脱下后 armId 正确刷新（同 Main.qml
                            //   player 护甲 8 Model 的 t498 一轮修法，lessons-learned 钉死此模式）。
                            property int armId: root.hotbar.armorRevision >= 0 ? root.hotbar.armorBlockIdAt(index) : 0
                            property int armDur: root.hotbar.armorRevision >= 0 ? root.hotbar.armorDurabilityAt(index) : 0
                            property var armEnch: root.hotbar.armorRevision >= 0 ? root.hotbar.armorEnchantsAt(index) : [0,0,0,0]
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                            // t551 复原生存版空装备栏（用户「t546 做反了」）：t546 把空槽换成了逐格 3D 灰体预览
                            //   （ArmorSlot3D），用户要求**还原之前生存背包的空护甲槽占位图**（empty_armor_slot_*）。
                            //   恢复 t497 的 2D 空槽占位：pack 启用且有 empty_armor_slot_<piece>.png → 显 pack PNG
                            //   （alpha-test 透明底）；pack 关/无映射 → Canvas 自绘金属灰剪影（§9a 原创）；装备 →
                            //   MaterialIcon 护甲图。3D 人物保留在 CharacterPreview3D（唯一 3D 预览，非逐槽）。
                            //   piece = index（0 头盔 / 1 胸甲 / 2 护腿 / 3 靴子，与 ArmorRegistry::ArmorPiece 同序）。
                            ResourcePackManager { id: armorRp }
                            Image {
                                id: armorEmptyPackImg
                                anchors.centerIn: parent
                                width: 26; height: 26
                                visible: armId === 0 && source.toString().length > 0
                                // 触碰 armId/armorRp.active 建立绑定依赖（槽位变 / pack 切换 → 重查源）。
                                source: { const _r = armorRp.active; return _r >= 0 ? (armId === 0 ? armorRp.emptyArmorSlotSource(index) : "") : "" }
                                fillMode: Image.PreserveAspectFit
                                smooth: false // 像素硬边（同 Canvas imageSmoothingEnabled=false；MC 1.0 占位图为像素艺术）
                            }
                            // 空槽部位占位剪影（暗灰金属头盔/胸甲/护腿/靴像素图；§9a 原创，非 MC 资产）。
                            //   仅 armId===0 且 pack 无该空槽图标时显（有装备时让位给 MaterialIcon 护甲图；
                            //   pack 有空槽图时让位给上方 armorEmptyPackImg）。
                            Canvas {
                                anchors.centerIn: parent
                                width: 26; height: 26
                                visible: armId === 0 && !armorEmptyPackImg.visible
                                onPaint: {
                                    const ctx = getContext("2d"); ctx.reset()
                                    ctx.imageSmoothingEnabled = false
                                    const metal = "#9aa0a6", gap = "#262b30"
                                    ctx.fillStyle = metal
                                    if (index === 0) {                  // 头盔
                                        ctx.fillRect(5, 5, 16, 3)
                                        ctx.fillRect(7, 8, 12, 9)
                                        ctx.fillStyle = gap
                                        ctx.fillRect(9, 11, 8, 3)
                                    } else if (index === 1) {           // 胸甲
                                        ctx.fillRect(6, 5, 14, 4)
                                        ctx.fillRect(7, 9, 12, 13)
                                        ctx.fillStyle = gap
                                        ctx.fillRect(12, 10, 2, 10)
                                    } else if (index === 2) {           // 护腿
                                        ctx.fillRect(7, 5, 12, 4)
                                        ctx.fillRect(7, 9, 4, 13)
                                        ctx.fillRect(15, 9, 4, 13)
                                    } else {                            // 靴
                                        ctx.fillRect(6, 13, 6, 7)
                                        ctx.fillRect(14, 13, 6, 7)
                                        ctx.fillRect(4, 18, 10, 2)
                                        ctx.fillRect(12, 18, 10, 2)
                                    }
                                }
                            }
                            // 装备中的护甲图标（走 MaterialIcon 护甲段分支；armId!==0 时显）。
                            MaterialIcon {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: armId !== 0
                                materialId: armId
                            }
                            // t498 二轮复盘：装备槽内显「cur/max」耐久数字（小字，耐久条上方）。
                            //   用户报「进背包无耐久显示、只在 hover tooltip 显」→ 槽内常显数字（进背包即见），
                            //   不依赖 hover。满耐久也显（与耐久条常显一致）。触碰 armorRevision 损耗后重算。
                            Text {
                                visible: armId !== 0
                                text: { const _r = root.hotbar.armorRevision
                                    return _r >= 0 ? (root.hotbar.armorDurabilityAt(index) + "/" + root.hotbar.armorMaxDurability(armId)) : "" }
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: armorDurBar.top
                                anchors.bottomMargin: 0
                                color: "#dfe6df"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 9; font.bold: true
                            }
                            // 装备耐久条（t452 起；t498 二轮复盘改常显 + 表达式形式绑定）。
                            //   t498 修：① 用户报「进背包无进度条、耐久只在 hover tooltip 显」——根因是 visible 要求
                            //     curDur<maxDur（满耐久隐），新装备护甲恒满耐久 → 条永不显。改「常显」（仅 armId!==0
                            //     即显，满耐久也显绿条），与 MC 1.0 装备槽耐久条一致（MC 装备槽耐久条满耐久也显）。
                            //     ② 原 curDur/maxDur 用语句块形式绑 armDur（同 armId stale 根因）→ 受击损耗后条不刷新；
                            //     改表达式形式直接读 armorDurabilityAt（Q_INVOKABLE，恒最新）。
                            //   色 绿(>50%)/黄(20–50%)/红(<20%)；满耐久显满绿条。触碰 armorRevision → 损耗后重算。
                            Item {
                                id: armorDurBar
                                property int curDur: root.hotbar.armorRevision >= 0 ? root.hotbar.armorDurabilityAt(index) : 0
                                property int maxDur: root.hotbar.armorRevision >= 0 ? root.hotbar.armorMaxDurability(armId) : 0
                                property real ratio: maxDur > 0 ? curDur / maxDur : 0.0
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 3; anchors.rightMargin: 3; anchors.bottomMargin: 2
                                height: 3
                                visible: armId !== 0 && maxDur > 0 && curDur > 0
                                Rectangle { anchors.fill: parent; color: "#000000"; opacity: 0.55 }
                                Rectangle {
                                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                                    width: parent.width * (parent.maxDur > 0 ? parent.curDur / parent.maxDur : 0)
                                    color: parent.ratio > 0.5 ? "#5fd35f" : (parent.ratio >= 0.2 ? "#e8e85a" : "#e05050")
                                }
                            }
                            // t590 附魔光晕（装备槽）：已装备护甲带附魔 → 浅紫半透明叠层（机制等价 MC 附魔光泽，
                            //   同 EnchantInputSlot / HUD hotbar 光晕配色）。触碰 armorRevision 令附魔写入后重算。
                            Rectangle {
                                anchors.fill: parent
                                visible: {
                                    const _r = root.hotbar.armorRevision
                                    if (_r < 0 || armId === 0) return false
                                    const e = armEnch
                                    return Array.isArray(e) && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
                                }
                                color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                                radius: 3
                                z: 3
                            }
                            // 装备 / 脱下（左键单点）。护甲不可堆叠 → count 恒 1；走 armorSetStack（含部位校验）。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    // t377 Shift+左键装备槽护甲 → 整件归还背包（slotShiftLeft armor 分支）。
                                    //   spec「Shift+Left-click an equipped piece -> move to inventory」。
                                    if (window.shiftHeld) {
                                        root.slotShiftLeft("armor", index)
                                        root.armorChanged()
                                        return
                                    }
                                    const heldId = root.hotbar.heldBlock
                                    const heldCnt = root.hotbar.heldCount
                                    const heldDur = root.hotbar.heldDurability
                                    const heldEnch = root.hotbar.heldEnchants()
                                    const heldName = root.hotbar.heldCustomName       // t622 实例名随实例走
                                    // t498 二轮复盘：从 VM 直接读装备槽当前态（Q_INVOKABLE 恒最新），不走绑定属性
                                    //   armId/armDur/armEnch（low-frequency NOTIFY 下可能 stale → oldId 读到旧值 → 幻影
                                    //   旧件写回光标 = 护甲复制）。armId 绑定已改表达式形式兜底；此处再从 VM 读为纵深防御。
                                    const slotId = root.hotbar.armorBlockIdAt(index)
                                    const slotDur = root.hotbar.armorDurabilityAt(index)
                                    const slotEnch = root.hotbar.armorEnchantsAt(index)
                                    const slotName = root.hotbar.armorCustomNameAt(index) // t622 装备槽实例名
                                    const slotHas = slotId !== 0
                                    // 持物：须是护甲且部位匹配该槽 → 装备（与槽内旧物互换到光标）；否则 no-op。
                                    if (heldId !== 0) {
                                        if (!root.hotbar.isArmor(heldId)) return
                                        if (root.hotbar.armorPiece(heldId) !== index) return
                                        // 互换：先把槽内旧护甲取到光标，再装备手持护甲（armorSetStack 守部位）。
                                        //   t475 附魔随实例互换（旧物附魔 → 光标；手持附魔 → 装备槽）。
                                        //   t622 名随实例互换（同附魔）。
                                        const oldId = slotId, oldDur = slotDur, oldEnch = slotEnch, oldName = slotName
                                        root.hotbar.armorSetStack(index, 0, 0)        // 先清槽（脱下旧物）
                                        root.hotbar.armorSetStack(index, heldId, 1, heldDur, heldEnch, heldName) // 装备手持
                                        // 光标手持 = 旧物（若有），否则空。
                                        if (oldId !== 0) {
                                            root.hotbar.heldBlock = oldId
                                            root.hotbar.heldCount = 1
                                            root.hotbar.heldDurability = oldDur
                                            root.hotbar.setHeldEnchants(oldEnch)
                                            root.hotbar.heldCustomName = oldName   // t622 旧件名随实例回光标
                                        } else {
                                            root.hotbar.heldBlock = 0
                                            root.hotbar.heldCount = 0
                                        }
                                        root.armorChanged() // t345 装备音
                                        return
                                    }
                                    // 空手：槽有护甲 → 脱下到光标。
                                    if (slotHas) {
                                        root.hotbar.heldBlock = slotId
                                        root.hotbar.heldCount = 1
                                        root.hotbar.heldDurability = slotDur
                                        root.hotbar.setHeldEnchants(slotEnch)
                                        root.hotbar.heldCustomName = slotName       // t622 脱下带名保真
                                        root.hotbar.armorSetStack(index, 0, 0)
                                        root.armorChanged() // t345 脱下音
                                    }
                                }
                            }
                            // t345/t452 悬停 tooltip：显护甲名 + 护甲值（spec「hover an armor piece shows its armor value」）
                            //   + 耐久 cur/max（t452：槽内改耐久条后，数字移此）。同时写 hoveredTipPos 让 tooltip 定位
                            //   到本槽（原仅写 hoveredItemId/Value，tooltip 停留在上一槽位置）。
                            HoverHandler {
                                onHoveredChanged: {
                                    if (hovered && armId !== 0) {
                                        root.hoveredItemId = armId
                                        root.hoveredArmorValue = root.hotbar.armorPointsFor(armId)
                                        root.hoveredArmorDurability = armDur
                                        const p = parent.mapToItem(root, parent.width / 2, 0)
                                        root.hoveredTipPos = Qt.point(p.x, p.y)
                                        // t590：写 hoveredKey（armor:N）→ tooltip 附魔行能读 armorEnchantsAt。
                                        root.hoveredKey = root.slotKey("armor", index)
                                    } else if (root.hoveredItemId === armId) {
                                        root.hoveredItemId = 0
                                        root.hoveredArmorValue = 0
                                        root.hoveredArmorDurability = 0
                                        if (root.hoveredKey === root.slotKey("armor", index)) root.hoveredKey = ""
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ③ 3×9 主栏（27 槽）：t97 起读 hotbar VM（m_mainSlots，三菜单共享）。点击拾取/放置方块
            // （主栏栈写经 hotbar.mainSetStack，VM 单一权威；与 hotbar/合成格共享光标手持物）。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    // model 用固定整数 mainCount（VM CONSTANT=27）→ delegate 一次创建永驻；「刷新」靠
                    // 每绑定显式触碰 mainRevision（Q_PROPERTY，NOTIFY=mainSlotsChanged）→ 经 Q_INVOKABLE
                    // mainBlockIdAt/mainCountAt 取最新栈值（同 hotbar 行 slotRevision 模式，t55/t63 已验证）。
                    model: root.hotbar.mainCount
                    delegate: Item {
                        // 主栏槽栈 id / 数量 / 耐久（触碰 mainRevision → 主栏栈写入后重算；air=0 空槽）。
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
                        // 栈数量（t32）：count>1 时右下角显数字。触碰 mainRevision 刷新（VM NOTIFY 驱动）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount > 1) : false }
                            text: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (mainCount) : "" }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        // t640② 主栏工具耐久条（t183 HUD hotbar 耐久条模式推广到背包主栏）：槽底薄条
                        //   （DurabilityBar 复用组件），宽 ∝ remaining/max、绿/黄/红。满耐久仍显满绿条（背包常显
                        //   语义同护甲槽 t498；与 HUD hotbar 满耐久隐不同）。触碰 mainRevision → 磨损 / 换槽后重算。
                        //   非工具 / 空槽 → DurabilityBar 内部 maxDur<=0 / curDur<=0 自隐。
                        DurabilityBar {
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 3; anchors.rightMargin: 3; anchors.bottomMargin: 2
                            height: 3
                            property int cDur: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.mainDurabilityAt(index)) : 0 }
                            property int mDur: { const _r = root.hotbar.mainRevision; return _r >= 0 ? (root.hotbar.toolMaxDurability(mainId)) : 0 }
                            curDur: cDur
                            maxDur: mDur
                        }
                        // t590 附魔光晕（主栏槽）：槽内物品带附魔 → 浅紫半透明叠层（机制等价 MC 附魔光泽，
                        //   同 EnchantInputSlot / HUD hotbar 光晕配色）。触碰 mainRevision 令附魔写入后重算。
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
                        // t38 生存左键整组操作（拾取 / 放置 / 合并 / 互换）：统一走 resolveClick。
                        // 主栏槽写经 hotbar.mainSetStack（VM 单一权威；同 id 合并至 maxStack、异 id 互换）。
                        // 右键走 per-slot 右键 TapHandler（resolveRightClick）；左键拖动均分由 root DragHandler +
                        //   逐槽 HoverHandler 收集（t167）；此槽左键 TapHandler 处理单点（Shift 搬运 / 双击合并 / resolveClick）。
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
                        // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey（同左键 per-slot 模式）。
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
                        // t167 均分拖拽高亮（异物槽纵使被扫过也不亮）。
                        // t205：右键拖拽每格放1 同样亮 rightDragSlots 中的格。
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

            // ④ 9 槽 hotbar 栏（同步游戏内 hotbar）：读 hotbar VM，点击切换选中槽 + 选中选框。
            Item {
                width: root.mainCols * root.slotSize
                height: root.slotSize

                Row {
                    spacing: 0
                    Repeater {
                        // t63 修复（hotbar 行拾取/放入后不显图标，t55 复发）：
                        //   根因 —— 旧 `model: { slotRevision; slotList() }` 返回长度恒 9 的 JS 数组（QVariantList）
                        //   作 Repeater model。slotRevision 变时绑定重算返回新数组，但**长度不变（恒 9）** →
                        //   QQuickRepeater 视作「count 未变」→ 复用既有 delegate、不重建 → modelData 停在初值（全空）。
                        //   主栏（t97 前本地数组 + 本地版本号 mainRev；t97 起亦读 VM mainRevision）刷新正常，唯独
                        //   hotbar 行因「读 VM 的等长数组」踩此坑。
                        //
                        //   修法（与 Main.qml HUD hotbar t55 已验证写法一致）：model 改用**固定整数**（slotCount=9，
                        //   CONSTANT → delegate 一次创建永驻），把「刷新」责任下放到**每个依赖槽内容的绑定**：
                        //   每绑定显式触碰 slotRevision（Q_PROPERTY，NOTIFY=slotsChanged）→ 经 Q_INVOKABLE
                        //   blockIdAt(index)/countAt(index) 取最新栈值。Q_INVOKABLE 返回值不被 NOTIFY 跟踪，
                        //   但同绑定内先读 NOTIFY 属性 → 整绑定挂在该信号 → slotsChanged 后重算。delegate 持有
                        //   slotId 属性集中此模式，下游图标 / 数量全用 slotId / countAt(index) 而非 modelData。
                        model: root.hotbar.slotCount
                        delegate: Item {
                            // 槽物品 id（触碰 slotRevision → 拾取/放入后重算 blockIdAt(index)；air=0 空槽）。
                            property int slotId: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.blockIdAt(index)) : 0 }
                            // t590 槽内附魔（触碰 slotRevision → 附魔写入 / 搬运后重算；供附魔光晕 + tooltip）。
                            property var slotEnch: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.enchantsAt(index)) : [0,0,0,0] }
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
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
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
                            // 栈数量（t32）：count>1 时右下角显数字。触碰 slotRevision 刷新（countAt 是 Q_INVOKABLE，
                            // 靠版本号触发）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.countAt(index) > 1) : false }
                                text: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.countAt(index)) : "" }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // t640② 背包内 hotbar 行工具耐久条（同主栏 DurabilityBar；HUD hotbar 已有 t315 条，
                            //   背包面板补齐）。触碰 slotRevision → 磨损 / 换槽后重算。非工具 / 空槽自隐。
                            DurabilityBar {
                                anchors.left: parent.left; anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.leftMargin: 3; anchors.rightMargin: 3; anchors.bottomMargin: 2
                                height: 3
                                property int cDur: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.durabilityAt(index)) : 0 }
                                property int mDur: { const _r = root.hotbar.slotRevision; return _r >= 0 ? (root.hotbar.toolMaxDurability(slotId)) : 0 }
                                curDur: cDur
                                maxDur: mDur
                            }
                            // t590 附魔光晕（hotbar 槽）：槽内物品带附魔 → 浅紫半透明叠层（机制等价 MC 附魔光泽，
                            //   同 EnchantInputSlot / HUD hotbar 光晕配色）。触碰 slotRevision 令附魔写入后重算。
                            Rectangle {
                                anchors.fill: parent
                                visible: {
                                    const _r = root.hotbar.slotRevision
                                    if (_r < 0 || slotId === 0) return false
                                    const e = slotEnch
                                    return Array.isArray(e) && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
                                }
                                color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                                radius: 3
                                z: 3
                            }
                            // tap → t38 生存左键整组操作（拾取 / 放置 / 合并 / 互换）：统一走 resolveClick。
                            // hotbar 槽内容写经 hotbar.setStack（VM 单一权威；同 id 合并至 maxStack、异 id 互换）。
                            // t49：背包内点 hotbar 行**不切真实选中**（真实选中仅由游戏内 1–9 / 滚轮改）。
                            // 右键走 per-slot 右键 TapHandler（resolveRightClick）；左键拖动均分由 root DragHandler +
                            //   逐槽 HoverHandler 收集（t167）；此槽左键 TapHandler 处理单点（Shift / 双击合并 / resolveClick）。
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
                            // t167 均分拖拽高亮（异物槽纵使被扫过也不亮）。
                            // t205：右键拖拽每格放1 同样亮 rightDragSlots 中的格。
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

                // 选中槽选框（raised bevel：顶 / 左 亮、底 / 右 暗 → 凸起观感），随 selectedSlot 位移。
                // 单独 overlay（不放进 Repeater）→ 选中态唯一；Behavior 让点击切换有平滑滑动感（同游戏内 hotbar）。
                // hotbar 在构造期可能瞬时为 null，故三元守 null（避免 QML 绑定求值期 null 解引用）。
                Item {
                    visible: false // 用户要求：背包内 hotbar 行不显示选中白框（手持物由游戏内 HUD hotbar 体现）
                    x: (root.hotbar ? root.hotbar.selectedSlot : 0) * root.slotSize - 1
                    y: -1
                    width: root.slotSize + 2; height: root.slotSize + 2
                    Behavior on x { NumberAnimation { duration: 70; easing.type: Easing.OutQuad } }
                    // 选框四边统一白色（用户反馈右/下灰不协调 → 去 raised bevel 暗边）。
                    Rectangle { color: "#ffffff"; width: parent.width; height: 2; anchors.top: parent.top }
                    Rectangle { color: "#ffffff"; width: 2; height: parent.height; anchors.left: parent.left }
                    Rectangle { color: "#ffffff"; width: parent.width; height: 2; anchors.bottom: parent.bottom }
                    Rectangle { color: "#ffffff"; width: 2; height: parent.height; anchors.right: parent.right }
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
    // t345 当前 hover 槽的护甲值（>0 = 护甲 → tooltip 附「护甲 N」行；0 = 非护甲 / 未跟踪）。装备槽 HoverHandler
    //   写入；离开按 id 守卫清 0（同 hoveredItemId 模式，防相邻槽进出竞态互清）。
    property int hoveredArmorValue: 0
    // t452 当前 hover 护甲槽的装备剩余耐久（>0 = 已装备护甲且在跟踪 → tooltip 附「耐久 cur/max」行；0 = 未跟踪）。
    //   装备槽 HoverHandler 写入（槽内耐久改用条后，数字移 tooltip 显，同工具套路）；离开按 id 守卫清 0。
    property int hoveredArmorDurability: 0
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
    // t590 当前 hover 槽物品的附魔列表文本（tooltip 附魔行）：据 hoveredKey 查 hotbar / main / armor 三组。
    //   hotbar.enchantListText 把 4 槽 packed int 转「锐锋 III\n效率 II」；无附魔 → 空串 → tooltip 不追加行。
    //   触碰各 revision（qml-touch 三轮模式）→ 附魔写入 / 搬运后 tooltip 附魔行刷新。
    property string hoveredEnchantText: {
        if (!root.hotbar || !root.hoveredItemId || !root.hoveredKey) return ""
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const _ar = root.hotbar.armorRevision
        const key = root.hoveredKey
        const parts = key.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        if (parts[0] === "hotbar") return _sr >= 0 ? root.hotbar.enchantListText(root.hotbar.enchantsAt(idx)) : ""
        if (parts[0] === "main") return _mr >= 0 ? root.hotbar.enchantListText(root.hotbar.mainEnchantsAt(idx)) : ""
        if (parts[0] === "armor") return _ar >= 0 ? root.hotbar.enchantListText(root.hotbar.armorEnchantsAt(idx)) : ""
        return ""
    }
    // t622 当前 hover 槽物品的实例名（铁砧改名；空串 → tooltip 走注册默认名）。据 hoveredKey 查
    //   hotbar / main / armor 三组；触碰各 revision → 改名 / 搬运后 tooltip 名行刷新（同 hoveredEnchantText 模式）。
    property string hoveredCustomName: {
        if (!root.hotbar || !root.hoveredItemId || !root.hoveredKey) return ""
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const _ar = root.hotbar.armorRevision
        const key = root.hoveredKey
        const parts = key.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        if (parts[0] === "hotbar") return _sr >= 0 ? root.hotbar.customNameAt(idx) : ""
        if (parts[0] === "main") return _mr >= 0 ? root.hotbar.mainCustomNameAt(idx) : ""
        if (parts[0] === "armor") return _ar >= 0 ? root.hotbar.armorCustomNameAt(idx) : ""
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
            // t263 工具槽 tooltip 附「cur/max」耐久行（如「铁镐  5/250」）；非工具 / 未跟踪 → 仅显名。
            //   t345 护甲槽 tooltip 附「护甲 N」行（spec「hover an armor piece shows its armor value」）。
            //   t452 护甲槽 tooltip 附「耐久 cur/max」行（槽内改耐久条后数字移此，同工具套路）。
            //   t304 弓槽 tooltip 附「攻击 1-N」行。
            //   t590 附魔行：物品带附魔 → 换行显附魔列表（如「锐锋 III\n效率 II」），无附魔 → 空串不追加。
            //   t622：hover 槽带实例名（customNameAt）→ 优先显实例名（铁砧改名物品显其名）。
            text: root.hotbar ? ((typeof root.hoveredCustomName === "string" && root.hoveredCustomName.length > 0
                    ? root.hoveredCustomName : root.hotbar.nameForBlock(root.hoveredItemId))
                + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.hotbar.toolMaxDurability(root.hoveredItemId) : "")
                + (root.hoveredArmorValue > 0 ? "  护甲 " + root.hoveredArmorValue : "")
                + (root.hoveredArmorDurability > 0 ? "  耐久 " + root.hoveredArmorDurability + "/" + root.hotbar.armorMaxDurability(root.hoveredItemId) : "")
                + (root.hotbar.toolType(root.hoveredItemId) === 7 ? "  攻击 1-" + root.hotbar.bowArrowMaxDamage() : "")
                + (root.hoveredEnchantText.length > 0 ? "\n\n" + root.hoveredEnchantText : "")) : ""
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
