import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox
// t516：背包槽操作算法（resolveClick/resolveRightClick/readSlot/writeSlot/redistributeLive/doMergeSameId
//   /slotShiftLeft 等）抽取自共享 JS 库 InventoryOps.js；本面板仅保留 main/hotbar VM 路由 + anvil 本地槽路由
//   + 薄委托包装（供 QML 信号处理器经 root.xxx 调用，调用点零改动）。算法单一权威收敛于此库，
//   消除五面板逐字复制（同 CraftingTableUI / FurnaceUI / ChestUI / EnchantingTableUI 模式）。
import "InventoryOps.js" as InventoryOps

// qml-touch 三轮：本文件所有「触碰 NOTIFY 属性」的绑定统一改表达式形式
//   `{ const _r = <rev>; return _r >= 0 ? (<expr>) : <fallback> }`（触碰值参与返回值），防 qmlcachegen
//   AOT 把裸语句触碰 `<rev>;` 当死代码消除 → 依赖不注册 → revision 变后绑定永不重算（机制/返回值不变）。

// 铁砧 UI（t550 二轮重做，用户对 t543 不满）：右键铁砧方块打开（PlayerController::anvilOpened → Main.qml
// Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t550 用户要求（逐条落实）：
//   **① A+B=C 两输入都在左边**：仿 MC 1.0 铁砧——左列两槽 = 左输入（待修复/附魔/重命名的工具/护甲）+ 右输入
//     （材料：铁锭等修复材料 / 附魔书）；右列单槽 = 产物槽（预览修复/合并/重命名结果）。箭头左→右指向产物。
//   **② 格子上无「左输入/右输入/产物」文字**：槽内零 caption（AnvilSlot 不再画槽位小字）。
//   **③ 去掉下面三行文字 + 按钮**：移除「修复/附魔合并/重命名」三个 AnvilActionButton。
//   **④ 只显示最上面消耗等级 + 改名**：产物槽下绿字显所需等级（放东西能出产物即显）；改名输入框 + 按钮保留。
//   **⑤ 等级显示在产物格下绿字**：修复 = 所需材料数（1 材料修 1/3 满耐久，3 材料修满；1 级/材料，至少 1 级）；
//     合并附魔 = 2 级；重命名 = 1 级。可承担 → 绿字；经验不足 → 红字提示。无产物 → 灰字提示。
//   **⑥ 改名框 Esc 卡死修复**：重命名 TextInput 持焦时 Esc 由输入框 Keys 自行处理关面板（closeAnvil → grab），
//     不吞键（TextInput 正常打字键仍进输入框；Esc 不触发输入框「吞键」路径）。
//   **⑦ 修复功能参考 MC**：左放铁盔甲 + 右放铁锭 → 产物修复（3 锭修满，1 锭补 1/3 耐久）；修工具同理；
//     改名 = 产物格显示新名。**真修复**：点产物槽 → 消耗 XP 等级 + 消耗右槽材料 + 修左槽耐久 → 产物入选中
//     hotbar 槽 + 清两输入槽。**真改名**：左槽有物 + 改名框非空 → 产物格即时显新名；点产物 → 消耗 1 级 +
//     写 customName → 入选中槽。**附魔合并**：右槽附魔书（t393 占位无真附魔）→ 点产物 → 消耗 2 级 + 消耗书，
//     附魔合并逻辑就位（当前占位书无附魔 → 产物 = 左槽原样）。
//
// 修复材料映射（Hotbar::anvilRepairMaterial，C++ 单一权威）：木→木板 / 石→圆石 / 铁→铁锭 / 钻石→钻石 /
//   弓→线 / 剪刀→铁锭 / 钓竿→线 / 护甲→同材质锭或皮革。每材料修 1/3 满耐久（ceil，3 材料修满 = 用户规格）。
//
// 产物输出路由：点产物槽 → 消耗等级 + 材料后，把产物栈写**选中 hotbar 槽**（setStack，耐久/附魔/自定义名随实例
//   保真；同 t477 铁砧 shell 目标 = 选中槽）。产物占位 = 本地 anvil 组 index 2（preview 只显不可交互）。
//
// 全部 GUI 自绘原创（Rectangle + Text + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点槽 / 输入名），关闭 → grab。
//
// **PERF 护栏（spec「anvil UI recomputes ONLY on input slot change, never per-frame」）**：
//   所有显示绑定到 slotRevision / mainRevision / anvilRev / playerState.levelChanged（低频 NOTIFY：
//   槽写入 / 升级才发，非 60Hz tick）。无 Timer / 无 onFrame / 无 PositionChanged 扫描（仅操作成功 flash
//   用一次性 Timer 600ms 翻 false）。enabled 重算由 NOTIFY 驱动，非每帧。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（heldBlock/heldCount/maxStackSize/iconSourceForBlock/nameForBlock/
    // isTool/isMaterial/slotRevision/mainSetStack 等栈操作 + 图标 / 名查询 + anvilRepairMaterial 修复材料判定）。
    property Hotbar hotbar
    // 宿主注入：playerState（level / spendLevels）—— 修复/合并/重命名消耗 XP 等级。
    property PlayerState playerState
    // 宿主注入：PlayerController（damageAnvil 推进铁砧损坏）。声明 var 避免类型解析耦合。
    property var player: null
    // 宿主注入：铁砧方块世界坐标（player.damageAnvil 推进损坏）。
    property int anvilX: 0
    property int anvilY: 0
    property int anvilZ: 0
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

    // t550 本地三槽存储：anvil 组（0=左输入 / 1=右输入材料 / 2=产物预览占位）。与 hotbar VM 共享同一光标
    //   手持栈 heldBlock/heldCount；左键整组 / 右键半份同 resolveClick / resolveRightClick（InventoryOps 单一
    //   权威）。**耐久 / 附魔随实例保真**（工具 / 护甲进左槽须修 / 合并，须保住实例耐久 + 附魔；数组写入不触发
    //   绑定 → 显示 / 预览经 anvilRev 触碰驱动）。
    property var anvilSlots:  [0, 0, 0]
    property var anvilCounts: [0, 0, 0]
    property var anvilDur:    [0, 0, 0]
    property var anvilEnch:   [[0,0,0,0], [0,0,0,0], [0,0,0,0]]
    property int anvilRev: 0

    // 取槽附魔元数据（数组未初始化防御 → 4 个 0）。InventoryOps 读写经 readSlot/writeSlot 路由用它保真。
    function enchAt(idx) {
        const e = root.anvilEnch[idx]
        return (Array.isArray(e) && e.length === 4) ? e : [0, 0, 0, 0]
    }

    // t110：当前指针所在槽的「组:下标」key（供 window.hoveredSlotKey 提升 → 数字键交换 + t167 左键拖动
    //   起点槽）。各槽 HoverHandler onHoveredChanged 维护（进入写、离开按 key 守卫清除，防相邻槽进出竞态
    //   互清）。组名与 readSlot/writeSlot 一致：anvil / main / hotbar。
    property string hoveredKey: ""

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    //   DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（slotLeft 单点拾取/放置/合并/互换 /
    //   Shift 搬运），拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler
    //   在 leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」
    //   字符串（去重简单）；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制
    //   （每滑入新格先撤销上轮写入再重分）。anvil / main / hotbar 参与；t181 右键拖动（每格放 1 个）同源算法。
    property bool leftDragActive: false
    property var dragSlots: []              // "anvil:0" / "main:5" / "hotbar:0"
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
    // doMergeSameId（扫 anvil+main+hotbar 同 id 累加成满栈、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // t543：anvil 三槽参与快捷操作（左键拖动均分 / 双击拿同类 / 右键分半）。声明 anvil 为可拖拽本地组 →
    //   InventoryOps.groupIsDraggable 放行（addDragSlot 收集、redistributeLive 分发）、doMergeSameId 扫 anvil 槽。
    property var localDragGroups: ["anvil"]
    // t543：anvil 组槽位数（doMergeSameId 扫描范围）。anvilSlots 长 3（左/右输入 + 中产物）。
    function localSlotCount(group) { return group === "anvil" ? root.anvilSlots.length : 0 }

    // ── t550 面板专属槽路由：anvil 三槽走本地数组 + 版本号（main/hotbar 由 InventoryOps 统一经 VM）。
    //   readSlot/writeSlot 薄包装委托 InventoryOps（含本地组分发 → 调本处 localReadSlot/localWriteSlot）。
    //   t550：local 槽透传耐久 / 附魔（工具 / 护甲进槽保真；材料 / 方块段恒 0 / 4 个 0 inert）。
    function localReadSlot(group, index) {
        if (group === "anvil")
            return { id: root.anvilSlots[index] || 0, count: root.anvilCounts[index] || 0,
                     durability: root.anvilDur[index] || 0, enchants: root.enchAt(index) }
        return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0] }
    }
    function localWriteSlot(group, index, id, count, durability, enchants) {
        if (group !== "anvil") return
        root.anvilSlots[index] = id
        root.anvilCounts[index] = count
        root.anvilDur[index] = durability || 0
        const e = (Array.isArray(enchants) && enchants.length === 4) ? enchants : [0, 0, 0, 0]
        const arr = root.anvilEnch
        arr[index] = e.slice()
        root.anvilEnch = arr
        root.anvilRev++
    }
    function resolveClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch) }
    function resolveRightClick(curId, curCount, curDur, curEnch) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count, durability, enchants) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants) }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → InventoryOps.slotShiftLeft（Shift+左键搬运 anvil↔main↔hotbar）。
    //   t180：可拖拽组（anvil/main/hotbar）双击 → doMergeSameId（拿同类）。resolveClick/resolveRightClick 算法见
    //   InventoryOps（六面板共享，调用点零改动）。t550 耐久 / 附魔透传（curDur/curEnch 取本地槽保真值）。
    function slotLeft(group, index) {
        if (window.shiftHeld) { InventoryOps.slotShiftLeft(root, group, index); return }
        // t180：280ms 内同槽二次点击 + 可拖拽组 → 拿同类（doMergeSameId 扫 anvil+main+hotbar 同 id）。
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
    //   （六面板共享）。本处仅薄委托包装，供 QML 信号处理器 / 绑定经 root.xxx 调用（调用点零改动）。
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
    function doMergeSameId(group, index) { InventoryOps.doMergeSameId(root, group, index) }

    // 关包归还 anvil 输入槽（spec 同 CraftingTableUI returnCraftToHotbar）：visible→false 时把三槽内容
    //   addStack 回 hotbar（MC 行为：关铁砧界面把输入槽物品退回背包）。t550 耐久 / 附魔随实例归还
    //   （addStack 第 3/4 参透传：工具 / 护甲保真回包）。
    function returnAnvilToHotbar() {
        if (!root.hotbar) return
        for (let i = 0; i < root.anvilSlots.length; ++i) {
            const id = root.anvilSlots[i] || 0
            const n = root.anvilCounts[i] || 0
            if (id !== 0 && n > 0)
                root.hotbar.addStack(id, n, root.anvilDur[i] || 0, root.enchAt(i))
        }
        for (let i = 0; i < root.anvilSlots.length; ++i) {
            root.anvilSlots[i] = 0
            root.anvilCounts[i] = 0
            root.anvilDur[i] = 0
        }
        root.anvilEnch = [[0,0,0,0], [0,0,0,0], [0,0,0,0]]
        root.anvilRev++
    }
    onVisibleChanged: {
        if (!visible) returnAnvilToHotbar()
        else { renameName = ""; nameInput.text = ""; lastResult = "" }
    }

    // ════════════════════════════════════════════════════════════════════════════
    // ── t550 铁砧操作区（A+B=C 左两输入 + 右产物 + 产物下等级 + 改名）──
    // 触碰 anvilRev（槽写入才发）建立依赖 —— 输入槽内容变时预览 / 等级 / 可用性重算（数组写入不触发绑定，
    //   故用 anvilRev 触碰参与返回，同 CraftingTableUI craftRev 模式）。
    readonly property int leftId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[0] || 0) : 0 }
    readonly property int matId:  { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[1] || 0) : 0 }
    readonly property int leftDur: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilDur[0] || 0) : 0 }
    readonly property int leftCount: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilCounts[0] || 0) : 0 }
    // 当前 XP 等级（绑定 playerState.level NOTIFY levelChanged；低频）。
    readonly property int playerLevel: playerState ? playerState.level : 0
    // 重命名输入文本。
    property string renameName: ""
    // 操作结果 flash（成功后短暂显绿）。
    property string lastResult: ""
    property bool justActed: false

    // 工具 / 护甲最大耐久（修复算 1/3 基准 + 预览耐久上限）。单一权威：Hotbar 透传 Tool/ArmorRegistry。
    function maxDur(id) {
        if (!root.hotbar) return 0
        const t = root.hotbar.toolMaxDurability(id)
        if (t > 0) return t
        return root.hotbar.armorMaxDurability(id)
    }
    // 修复所需材料数（每材料修 1/3 满耐久，ceil 取整；满耐久 → 0 = 无需修）。
    function repairMatNeeded(id, dur) {
        const max = root.maxDur(id)
        if (max <= 0) return 0
        const miss = max - dur
        if (miss <= 0) return 0
        const per = max / 3
        const n = Math.ceil(miss / per)
        return Math.min(n, 3)
    }
    // 右槽材料能否触发修复（leftId 是工具/护甲 + 右槽是该物品修复材料 + 确有耐久缺失）。
    readonly property bool canRepair: {
        const _r = root.anvilRev
        if (_r < 0) return false
        if (root.leftId === 0 || root.matId === 0) return false
        if (root.leftCount <= 0) return false
        if (root.maxDur(root.leftId) <= 0) return false
        if (!root.hotbar || !root.hotbar.anvilCanRepairMaterial(root.leftId, root.matId)) return false
        return repairMatNeeded(root.leftId, root.leftDur) > 0
    }
    // 附魔合并前置：左槽可附魔（工具 / 护甲，itemEnchantCategory != 0）+ 右槽附魔书（t393 占位无真附魔
    //   → 合并为空操作，产物 = 左槽原样）。0x227 = RecipeRegistry::EnchantedBookId。
    readonly property bool canMerge: {
        const _r = root.anvilRev
        if (_r < 0) return false
        if (root.leftId === 0 || root.matId !== 0x227) return false
        if (!root.hotbar || root.hotbar.itemEnchantCategory(root.leftId) === 0) return false
        return true
    }
    // 重命名前置：左槽有物 + 改名框非空。改名可与修复 / 合并叠加（MC：改名 + 修复合算等级）。
    readonly property bool canRename: {
        const _r = root.anvilRev
        const _n = root.renameName
        return _r >= 0 && root.leftId !== 0 && _n.trim().length > 0
    }
    // 改名是否叠加在修复 / 合并之上（产物名即时显新名）。
    readonly property bool renaming: root.canRename
    // 当前生效操作（修复优先 → 合并 → 改名；改名可叠加修复 / 合并）。返 "repair" / "merge" / "rename" / ""。
    readonly property string activeOp: {
        const _r = root.anvilRev
        const _n = root.renameName
        if (_r >= 0 && root.canRepair) return "repair"
        if (_r >= 0 && root.canMerge) return "merge"
        if (_n.length >= 0 && root.canRename) return "rename"
        return ""
    }
    // 所需 XP 等级（产物格下绿字数值）。修复 = 所需材料数（至少 1）；合并 = 2；改名 = 1；
    //   改名叠加修复 / 合并 → +1（机制等价 MC「改名在修复费之上另加 1 级」）。
    readonly property int cost: {
        const op = root.activeOp
        if (op === "repair") return Math.max(1, root.repairMatNeeded(root.leftId, root.leftDur)) + (root.renaming ? 1 : 0)
        if (op === "merge") return 2 + (root.renaming ? 1 : 0)
        if (op === "rename") return 1
        return 0
    }
    readonly property bool affordCost: playerLevel >= cost
    // 产物槽等级文字（绿字可承担 / 红字经验不足 / 灰字提示）。
    readonly property string costText: {
        const op = root.activeOp
        if (op === "repair") return "修复 " + cost + " 级"
        if (op === "merge") return "合并附魔 " + cost + " 级"
        if (op === "rename") return "重命名 " + cost + " 级"
        return "放入物品与材料"
    }
    readonly property string costColor: {
        if (root.activeOp === "") return "#6a727a"
        return root.affordCost ? "#6fe06f" : "#e06f5f"
    }
    // 产物槽内容（修复 → 修后耐久；合并/改名 → 左槽原样）。id / 耐久 / 附魔（改名产物名走 hoveredProductName）。
    readonly property int productId: {
        const _r = root.anvilRev
        if (_r < 0) return 0
        const op = root.activeOp
        if (op === "") return 0
        return root.leftId
    }
    readonly property int productDur: {
        const _r = root.anvilRev
        if (_r < 0) return 0
        const op = root.activeOp
        if (op === "repair") {
            const max = root.maxDur(root.leftId)
            if (max <= 0) return root.leftDur
            return Math.min(max, root.leftDur + root.repairMatNeeded(root.leftId, root.leftDur) * (max / 3))
        }
        if (op === "merge" || op === "rename") return root.leftDur
        return 0
    }
    readonly property var productEnch: {
        const _r = root.anvilRev
        return _r >= 0 ? root.enchAt(0) : [0, 0, 0, 0]
    }
    // ════════════════════════════════════════════════════════════════════════════

    // ── t550 三功能执行（真逻辑；t477 占位交互替换）──
    //   修复：消耗 1 级/材料 + 消耗右槽材料（每材料修 1/3 满耐久）→ 产物 = 左槽修后耐久。合并附魔：消耗 2 级 +
    //   消耗右槽附魔书（占位无真附魔 → 产物 = 左槽原样）。改名：改名框非空时叠加在修复 / 合并之上（+1 级）或
    //   单独生效 → 产物即时显新名。产物输出路由 = 选中 hotbar 槽（setStack 保真耐久/附魔/名）；成功后清两输入槽
    //   + 推进铁砧损坏。
    function takeProduct() {
        if (root.activeOp === "") return
        if (!root.affordCost) return
        const op = root.activeOp
        if (!root.playerState.spendLevels(root.cost)) return
        let outDur = root.leftDur
        let outEnch = root.enchAt(0)
        let outName = root.hotbar.customNameAt(root.hotbar.selectedSlot)
        if (op === "repair") {
            const max = root.maxDur(root.leftId)
            outDur = Math.min(max, root.leftDur + root.repairMatNeeded(root.leftId, root.leftDur) * (max / 3))
            // 消耗右槽材料：每修 1/3 需 1 件（取到 0 清空）。
            const need = root.repairMatNeeded(root.leftId, root.leftDur)
            root.anvilCounts[1] = Math.max(0, (root.anvilCounts[1] || 0) - need)
            if (root.anvilCounts[1] <= 0) { root.anvilSlots[1] = 0; root.anvilDur[1] = 0 }
            root.lastResult = "修复完成 +" + root.cost + "级"
        } else if (op === "merge") {
            // 右槽附魔书合并到左槽（逐附魔：已有同 id → 取 max 等级；否则写首个空槽 ≤4）。占位书无真附魔
            //   → 循环空转（产物 = 左槽原样附魔）；真附魔书数据接入后此逻辑即生效。
            const src = root.enchAt(1)
            for (let i = 0; i < 4; ++i) {
                const packed = src[i] || 0
                if (packed === 0) continue
                const eid = (packed >> 8) & 0xFF
                const lvl = packed & 0xFF
                let slot = -1
                for (let j = 0; j < 4; ++j) {
                    const p = outEnch[j] || 0
                    if (p !== 0 && ((p >> 8) & 0xFF) === eid) { slot = j; break }
                }
                if (slot >= 0) {
                    const cur = outEnch[slot] & 0xFF
                    outEnch[slot] = (eid << 8) | Math.max(cur, lvl)
                } else {
                    for (let j = 0; j < 4; ++j) {
                        if ((outEnch[j] || 0) === 0) { outEnch[j] = packed; break }
                    }
                }
            }
            root.anvilSlots[1] = 0; root.anvilCounts[1] = 0; root.anvilDur[1] = 0
            root.lastResult = "合并完成 +2级"
        } else { // rename
            root.lastResult = "已重命名"
        }
        // 改名叠加：改名框非空 → 产物名 = 新名（覆盖原 customName）。
        if (root.renaming) outName = root.renameName.trim()
        // 产物写选中 hotbar 槽（setStack 保真耐久 / 附魔 / 自定义名；覆盖选中槽 = MC 铁砧取产物占选中位）。
        root.hotbar.setStack(root.hotbar.selectedSlot, root.leftId, 1, outDur, outEnch)
        if (outName.length > 0) root.hotbar.setCustomName(root.hotbar.selectedSlot, outName)
        // 清左输入槽 + 改名框。
        root.anvilSlots[0] = 0; root.anvilCounts[0] = 0; root.anvilDur[0] = 0
        root.anvilEnch = [[0,0,0,0], [0,0,0,0], [0,0,0,0]]
        root.renameName = ""; nameInput.text = ""
        root.anvilRev++
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.justActed = true
        actFlashTimer.restart()
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
    // 手持物时点遮罩区 → 丢弃为实体（同 CraftingTableUI / FurnaceUI / 附魔台 / SurvivalInventory）。t228：
    //   左键整栈 / 右键 1 件 + 面板边界判定（面板内非槽位松手→不丢，修「左键拿物在面板内非槽位松手→直接丢地下」bug）。
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

    // 面板：深色圆角，居中。t543 深色风格统一（#1b1f24，同 CraftingTableUI / FurnaceUI / ChestUI /
    // EnchantingTableUI）。宽度与 CraftingTableUI / 附魔台一致（392）；高度 = 标题(22) + 操作区(150) +
    // 主栏(120) + hotbar(40) + 间距/边距。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 430                                  // 22 + 150 + 120 + 40 (=332) + 4×12 spacing(48) + 2×16 margin(32) ≈ 412 → 取 430 留余量
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
                    text: "铁砧"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // ── 操作区（t550：A+B=C —— 左列两输入 + 右产物 + 产物下等级 + 改名行）──
            // 左列：左输入槽（工具/护甲，index 0）在上、右输入槽（材料/附魔书，index 1）在下；
            // 右列：产物槽（index 2，preview 只显）+ 产物下绿字等级。箭头左→右指向产物。
            // 行内无任何槽位文字（用户要求②）。行宽 44+44+28+40 = 156 → 居中。
            Item {
                id: anvilArea
                width: parent.width
                height: 150

                // A+B=C 三槽行。
                Item {
                    id: slotRow
                    anchors.top: parent.top; anchors.topMargin: 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 156
                    height: 40

                    // 左输入槽（anvil 组 index 0；武器 / 工具 / 护甲）。
                    Item {
                        x: 0; y: 0
                        width: root.slotSize; height: root.slotSize
                        AnvilSlot {
                            anchors.fill: parent
                            group: "anvil"; index: 0
                            // qml-touch：槽内容读数组 + anvilRev 触碰参与返回（数组写入不触发绑定，需 rev 触碰）。
                            slotId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[0] || 0) : 0 }
                            slotCount: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilCounts[0] || 0) : 0 }
                            slotDur: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilDur[0] || 0) : 0 }
                            slotEnch: { const _r = root.anvilRev; return _r >= 0 ? (root.enchAt(0)) : [0, 0, 0, 0] }
                        }
                    }
                    // 右输入槽（anvil 组 index 1；材料：铁锭等修复材料 / 附魔书）。
                    Item {
                        x: root.slotSize + 4; y: 0
                        width: root.slotSize; height: root.slotSize
                        AnvilSlot {
                            anchors.fill: parent
                            group: "anvil"; index: 1
                            slotId: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilSlots[1] || 0) : 0 }
                            slotCount: { const _r = root.anvilRev; return _r >= 0 ? (root.anvilCounts[1] || 0) : 0 }
                            slotEnch: { const _r = root.anvilRev; return _r >= 0 ? (root.enchAt(1)) : [0, 0, 0, 0] }
                        }
                    }
                    // 左→中箭头（输入流向产物）。
                    Canvas {
                        x: root.slotSize * 2 + 8; y: root.slotSize / 2 - 8
                        width: 24; height: 16
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset()
                            ctx.imageSmoothingEnabled = false
                            ctx.fillStyle = "#8a8a8a"
                            ctx.fillRect(0, 6, 16, 4)
                            ctx.beginPath()
                            ctx.moveTo(16, 0); ctx.lineTo(24, 8); ctx.lineTo(16, 16); ctx.closePath()
                            ctx.fill()
                        }
                    }
                    // 产物槽（anvil 组 index 2；preview 只显产物预览，点它取产物）。绿框提示可出产物。
                    Item {
                        x: root.slotSize * 2 + 36; y: 0
                        width: root.slotSize; height: root.slotSize
                        AnvilSlot {
                            anchors.fill: parent
                            group: "anvil"; index: 2
                            preview: true
                            slotId: { const _r = root.anvilRev; return _r >= 0 ? (root.productId) : 0 }
                            slotCount: 1
                            slotDur: { const _r = root.anvilRev; return _r >= 0 ? (root.productDur) : 0 }
                            slotEnch: root.productEnch
                        }
                    }
                }

                // 产物槽下等级绿字（规格⑤：等级显示在产物格下绿字；无产物 → 灰字提示）。
                Text {
                    anchors.top: slotRow.bottom; anchors.topMargin: 2
                    anchors.horizontalCenter: slotRow.horizontalCenter
                    anchors.horizontalCenterOffset: root.slotSize + 18   // 对准产物槽（槽心 = 行心 + 58）
                    text: root.costText
                    color: root.costColor
                    font.pixelSize: 12; font.bold: true
                    visible: text.toString().length > 0
                }

                // ── 改名行（规格③④：只留改名功能，无三按钮文字）──
                // TextInput + 重命名按钮。输入框持焦时 Esc 由本行 Keys 处理关面板（规格⑥修复卡死）。
                Rectangle {
                    anchors.top: slotRow.bottom; anchors.topMargin: 38
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: panel.width - 16; height: 28
                    color: "#2a2018"
                    border.color: (root.renaming && root.affordCost && root.activeOp !== "") ? "#ffd87a" : "#0a0604"
                    border.width: (root.renaming && root.affordCost && root.activeOp !== "") ? 2 : 1
                    radius: 3
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        TextInput {
                            id: nameInput
                            anchors.verticalCenter: parent.verticalCenter
                            width: 200
                            color: "#ffe6a8"; font.pixelSize: 12
                            selectByMouse: true
                            maximumLength: 20
                            onTextEdited: root.renameName = text
                            // 规格⑥：改名框持焦时 Esc 由输入框自己处理 → 关面板（closeAnvil → grab + 焦点回键位层），
                            //   不吞键（其余按键正常进输入框打字）。仿聊天输入框 Keys 处理（chatInput Keys.onPressed）。
                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Escape) {
                                    window.closeAnvil()
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    if (root.renaming && root.affordCost && root.activeOp !== "") {
                                        root.takeProduct()
                                        event.accepted = true
                                    }
                                }
                            }
                            Text {
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                text: "输入新名…"
                                color: "#665544"; font.pixelSize: 12
                                visible: parent.text.length === 0
                            }
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 90; height: 22
                            color: (root.renaming && root.affordCost && root.activeOp !== "") ? "#5a4a2a" : "#2a2018"
                            border.color: "#1a120c"; radius: 3
                            Text {
                                anchors.centerIn: parent
                                text: "重命名 1级"
                                color: (root.renaming && root.affordCost && root.activeOp !== "") ? "#ffe6a8" : "#665544"
                                font.pixelSize: 10; font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            TapHandler {
                                enabled: root.renaming && root.affordCost && root.activeOp !== ""
                                onTapped: root.takeProduct()
                            }
                        }
                    }
                }

                // 「操作成功」绿色 flash 叠层（取产物后短暂显，~600ms 淡出）。
                Rectangle {
                    anchors.fill: parent
                    color: "#3aa55a"; opacity: root.justActed ? 0.30 : 0.0
                    visible: opacity > 0.001
                    Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                }
            }

            // ── t543 底部 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，六菜单共享）；左键整组 / 右键半份
            //   取放（与 CraftingTableUI / FurnaceUI / ChestUI / 附魔台主栏同模式）。主栏栈写经 hotbar.mainSetStack；
            //   与 anvil 三槽 / hotbar 共享同一 hotbar VM 光标手持栈。delegate 持 mainId/mainCount 触碰 mainRevision。
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

            // ── t543 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            //   属性触碰 slotRevision（t55/t63 已验证写法）。左键整组 / 右键半份同主栏（hotbar 槽写经
            //   hotbar.setStack；VM 单一权威）。**无数字角标**（同工作台 / 熔炉统一）。
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

        // 「操作成功」绿色 flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
        // 放 panel 内与叠层同域（便于 opacity Behavior 绑 root.justActed）。
    }

    // 操作成功 flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
    Timer {
        id: actFlashTimer
        interval: 600
        onTriggered: root.justActed = false
    }

    // AnvilSlot 组件：A+B+C 三槽布局的单槽（index 0=左输入 / 1=右输入材料 / 2=产物预览）。读本地 anvil 数组
    //   （anvilRev 驱动刷新）；左键整组 / 右键半份取放（同主栏 / hotbar，InventoryOps 单一权威）。
    //   t550 改版：① 槽内零 caption 文字（用户要求②）② preview=true 时只显产物预览、点它取产物（不参与
    //   常规拾取/放置）③ slotDur / slotEnch 透传（工具 / 护甲实例耐久 / 附魔保真，修复 / 合并前置）④ 耐久条
    //   （剩余耐久 < max 时槽底画绿/橙条）⑤ 改名产物工具提示显新名。
    component AnvilSlot : Item {
        id: aslot
        property string group: "anvil"
        property int index: 0
        property int slotId: 0
        property int slotCount: 0
        property int slotDur: 0
        property var slotEnch: [0, 0, 0, 0]
        property bool preview: false

        InvSlot {
            anchors.fill: parent
            wellColor: "#262b30"
            // 产物预览槽：绿框提示可出产物（放东西能出产物即显所需等级，规格⑤）。
            highlight: aslot.preview && aslot.slotId !== 0
        }
        // 物品图标：方块段→等距立方体 Image；工具段→ToolIcon；材料段→MaterialIcon 自绘。
        Item {
            anchors.centerIn: parent
            width: 30; height: 30
            visible: aslot.slotId !== 0
            Image {
                anchors.fill: parent
                visible: { const _r = root.anvilRev; return _r >= 0 ? (!root.hotbar.isTool(aslot.slotId) && !root.hotbar.isMaterial(aslot.slotId)) : false }
                source: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.iconSourceForBlock(aslot.slotId)) : "" }
                fillMode: Image.PreserveAspectFit; smooth: true
            }
            ToolIcon {
                anchors.fill: parent
                visible: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.isTool(aslot.slotId)) : false }
                tier: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.toolTier(aslot.slotId)) : 0 }
                toolType: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.toolType(aslot.slotId)) : 0 }
            }
            MaterialIcon {
                anchors.fill: parent
                visible: { const _r = root.anvilRev; return _r >= 0 ? (root.hotbar.isMaterial(aslot.slotId)) : false }
                materialId: { const _r = root.anvilRev; return _r >= 0 ? (aslot.slotId) : 0 }
            }
            // 附魔光晕（产物 / 输入槽带附魔时浅紫半透明叠层；机制等价 MC 附魔光泽）。
            Rectangle {
                anchors.fill: parent
                visible: aslot.slotId !== 0 && aslot.hasEnch
                color: Qt.rgba(0.55, 0.25, 0.9, 0.30)
                radius: 3
            }
        }
        // 栈数量（count>1 显数字；产物恒 1 不显）。
        Text {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.rightMargin: 3; anchors.bottomMargin: 1
            visible: { const _r = root.anvilRev; return _r >= 0 ? (aslot.slotCount > 1) : false }
            text: { const _r = root.anvilRev; return _r >= 0 ? (aslot.slotCount) : "" }
            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
            font.pixelSize: 13; font.bold: true
        }
        // 耐久条（工具 / 护甲剩余耐久 < max 时槽底绿/橙条；修复前直观可见耐久缺口）。maxDur 覆盖工具 + 护甲。
        Rectangle {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 2
            anchors.left: parent.left; anchors.leftMargin: 3
            anchors.right: parent.right; anchors.rightMargin: 3
            height: 3
            radius: 1
            visible: {
                const _r = root.anvilRev
                if (_r < 0) return false
                if (aslot.slotId === 0 || aslot.preview) return false   // 产物预览不画条（耐久已由修复/改名决定）
                const max = root.maxDur(aslot.slotId)
                return max > 0 && aslot.slotDur > 0 && aslot.slotDur < max
            }
            color: {
                const max = root.maxDur(aslot.slotId)
                if (max > 0 && aslot.slotDur <= max * 0.25) return "#e05f4f"   // <25% 橙红
                return "#6fe06f"                                               // 其余绿
            }
        }
        // 该槽是否有附魔（预览产物 / 输入工具附魔光晕判定）。
        property bool hasEnch: {
            const _r = root.anvilRev
            if (_r < 0) return false
            if (aslot.slotId === 0) return false
            const e = aslot.slotEnch
            return Array.isArray(e) && (e[0] || 0) !== 0
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                // 产物预览槽：点击 = 取产物（执行当前修复/合并/改名并写选中槽 + 清输入）。
                if (aslot.preview) {
                    if (aslot.slotId !== 0) root.takeProduct()
                    return
                }
                if (window.shiftHeld) { InventoryOps.slotShiftLeft(root, aslot.group, aslot.index); return }
                // t180：280ms 内同槽二次点击 → doMergeSameId（拿同类；扫 anvil+main+hotbar 同 id）。
                const key = root.slotKey(aslot.group, aslot.index)
                const now = Date.now()
                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                root.lastTapMs = now
                root.lastTapKey = key
                if (isDouble) { root.doMergeSameId(aslot.group, aslot.index); return }
                const r = root.resolveClick(aslot.slotId, aslot.slotCount, aslot.slotDur, aslot.slotEnch)
                if (!r) return
                root.writeSlot(aslot.group, aslot.index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                if (aslot.preview) return   // 产物槽右键无操作
                const r = root.resolveRightClick(aslot.slotId, aslot.slotCount, aslot.slotDur, aslot.slotEnch)
                if (!r) return
                root.writeSlot(aslot.group, aslot.index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
            }
        }
        HoverHandler {
            // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged 不重发 →
            // tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
            property int trackedId: aslot.slotId
            onTrackedIdChanged: {
                if (hovered && trackedId === 0 && root.hoveredItemId !== 0)
                    root.hoveredItemId = 0
            }
            onHoveredChanged: {
                const itemId = aslot.slotId
                if (hovered && itemId !== 0) {
                    root.hoveredItemId = itemId
                    const p = parent.mapToItem(root, parent.width / 2, 0)
                    root.hoveredTipPos = Qt.point(p.x, p.y)
                } else if (root.hoveredItemId === itemId) {
                    root.hoveredItemId = 0
                }
                const key = root.slotKey(aslot.group, aslot.index)
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
                const _rev = root.anvilRev
                const _ok = _rev >= 0 && _ds.length >= 0 && _rds.length >= 0
                const key = root.slotKey(aslot.group, aslot.index)
                if (_ok && root.leftDragActive && root.dragHasKey(key)
                    && (aslot.slotId === 0 || aslot.slotId === root.dragHeldId)) return true
                return _ok && root.rightDragActive && root.rightDragHasKey(key)
            }
            z: 10
        }
    }

    // t94 物品名悬停 tooltip（纯 QtQuick 自绘；不引入 QtQuick.Controls —— 项目未链接 Qt6::QuickControls2，
    // 顶层 import 新模块有「未部署→整文档加载失败」风险，见 lessons-learned）。各槽 HoverHandler 进入时写
    // hoveredItemId + hoveredTipPos（槽顶中心在 root 坐标系下）；离开按 id 守卫清除（防相邻槽进出竞态互清）。
    // 名字走 hotbar.nameForBlock：方块→BlockRegistry::displayName、工具→ToolRegistry::displayName、
    // 材料段→本地通用名；air/空槽→空串→不显。产物预览改名 → 显新名（hoveredProductName）。
    property int hoveredItemId: 0
    property point hoveredTipPos: Qt.point(0, 0)
    // 当前 hover 槽的工具剩余耐久（-1=未跟踪 → tooltip 不显耐久行）。据 hoveredKey 查 hotbar/main/anvil。
    //   maxDur 覆盖工具 + 护甲（护甲修复 tooltip 亦显 cur/max）。
    property int hoveredDurability: {
        if (!root.hotbar || !root.hoveredItemId || root.maxDur(root.hoveredItemId) <= 0) return -1
        // qml-touch 三轮：slotRevision/mainRevision/anvilRev 触碰参与返回（_sr>=0 恒真守卫），防 AOT 死代码
        //   消除裸触碰 → 同槽栈改写后 tooltip 耐久不刷新。
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const _ar = root.anvilRev
        const key = root.hoveredKey
        if (!key) return -1
        const parts = key.split(":")
        if (parts.length !== 2) return -1
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return -1
        if (parts[0] === "hotbar") return _sr >= 0 ? (root.hotbar.durabilityAt(idx)) : -1
        if (parts[0] === "main") return _mr >= 0 ? (root.hotbar.mainDurabilityAt(idx)) : -1
        if (parts[0] === "anvil") {
            // 产物预览槽（index 2）→ 显预览耐久（修复后）；输入槽（index 0）→ 本地实例耐久。
            if (idx === 2) return _ar >= 0 ? (root.productDur) : -1
            return _ar >= 0 ? (root.anvilDur[idx] || 0) : -1
        }
        return -1
    }
    // 产物预览槽 tooltip 名（改名叠加 / 单独改名时即时显新名；非改名操作 → 空 = 用注册表默认名）。
    property string hoveredProductName: {
        const _ar = root.anvilRev
        const _n = root.renameName
        if (_ar >= 0 && root.hoveredKey === "anvil:2" && root.renaming) return _n.trim()
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
            // t263 工具/护甲槽 tooltip 附「cur/max」耐久行；无耐久 / 未跟踪 → 仅显名。产物改名 → 显新名。
            text: root.hotbar ? (root.hoveredProductName.length > 0 ? root.hoveredProductName
                    : root.hotbar.nameForBlock(root.hoveredItemId)
                        + (root.hoveredDurability >= 0 ? "  " + root.hoveredDurability + "/" + root.maxDur(root.hoveredItemId) : "")) : ""
            color: "#f2f2f2"
            font.pixelSize: 12
        }
    }
}
