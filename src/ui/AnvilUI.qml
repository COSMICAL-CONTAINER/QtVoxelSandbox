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

// 铁砧 UI（t550 二轮重做，用户对 t543 不满；t576/t577/t578 三轮微调）：右键铁砧方块打开（PlayerController::
//   anvilOpened → Main.qml Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t576/t577/t578 三轮（用户复核 6 点不满）：
//   **t576 布局**：A/B 两输入槽中间加「+」号；删「放入物品与材料」提示文字（无产物时等级行静默空白）。
//   **t577 改名框**：移到产物槽上方（MC 铁砧名栏在顶）；左槽放入物品 → 框内占位显该物品当前名（注册默认名，
//     本地槽无 customName 通道）；输入 → 产物等级行显新名 + 等级；删「重命名」按钮（产物槽即执行入口）。
//   **t578 放入规则**（四分支，其余组合产物空）：
//     1. 左=工具/护甲 + 右=该物修复材料（anvilCanRepairMaterial）→ 修复产物（1 材料 +1/3 满耐久；
//        原 gate 要求「确有耐久缺失」repairMatNeeded>0 → 满耐久铁镐+铁锭产物空，已删此条件。
//        review L5 复检：满耐久（repairMatUse=0）会静默抽 1 级 → 现满耐久无改名产物空 / 有改名走 rename）。
//     2. 左右同 id（双铁镐/双护甲）→ 合并耐久（min(max, d1+d2+10% max)）+ 附魔并集（机制等价 MC 合并修复）。
//     3. 右=附魔书（0x227）→ 附魔合并（t550 已有，保持）。
//     4. 其余 → activeOp="" → 产物空（放不进产物格语义）。
//
// t606 四轮（R19.5 用户 8 点细节，铁砧 UI 细节批）：
//   **① 槽行下移**：操作区高 120→150（吸收面板底 ~30px 留白，主栏/hotbar 随之下移贴底），改名框下边距
//     0→4、槽行上距 8→16 —— A+B→C 槽行整体下移。
//   **② 自动填名**：左槽放入物品 → 改名框**自动填入**该物品当前名（注册默认名 nameForBlock；本地槽无
//     customName 通道，自定义名不保真）可直接修改；仅「框空 或 内容仍是上次自动填充值」时覆盖
//     （lastAutoName 守卫，用户改过则不动，防打字被覆盖）。
//   **③ 创造免经验**：player.mode === Creative → affordCost 恒真（等级行恒绿可付，不管需几级）+
//     takeProduct 跳过 spendLevels。材料消耗照旧（保守只免经验；MC 创造材料也免，此处不扩大）。
//   **④ B 不相关 → C 直接消失**：canRename 收紧 —— B 非空且非修复材料 / 非同 id / 非附魔书（三分支全不
//     匹配）→ 无任何产物（原 bug：A 工具 + B 泥土仍显改名产物）。B 空 + 名字真改过 → 单独改名合法。
//   **⑤ 改名框 UI**：宽度对齐槽行（376→176）+ 背景调浅（#2a2018→#3a3226）+ verticalAlignment 真居中
//     （原 anchors.verticalCenter 与 anchors.fill 叠用无效 → 文字贴顶）。
//   **⑥ 等级行只显消耗**：改名分支去产物名（「新名 · N 级」→「重命名 N 级」；名字已在框内 + tooltip 有）。
//   **⑦ 拿回物品清框**：左槽清空 → 改名框清空 + lastAutoName 复位；再放入 → 重新自动填名（②）。
//
// t626 五轮（R19.6 用户 6 点细节批）：
//   **① 高度压缩**：面板 430→370 —— Column 边距 16→12 / 行距 12→10 / 操作区 150→134（t606① 的下移扩高
//     部分回收；改名框上距 4→2、槽行上距 16→10）。宽度不变（392，用户「宽 OK」）。
//   **③ 改名框退出输入态**：任何槽交互 / 取产物成功后活动焦点归还键位层（defocusNameBox → window.
//     refocusKeyInput）——E 关面板 / 数字键切槽不再打进框；再点框才重新聚焦（改名框点按夺焦后焦点不会
//     自动回 keyInput，且单纯 focus=false 会把焦点丢给 contentItem，键位层 Keys 收不到任何键）。
//   **④ tooltip 名字行独立配色/位置**：名字（自定义/改名预览名金色加粗）/ 耐久（灰字独立行）/ 附魔（紫字
//     独立行）三行分列；修「改名物品 tooltip 丢耐久行」（原耐久只拼在默认名分支行尾）。
//   **⑥ 面板内 main/hotbar 行 tooltip 附附魔行**：hoveredEnchantText 补 hotbar/main 分支（原仅 anvil 三槽
//     ——附魔书放背包行不显携带附魔+等级；对齐 EnchantingTableUI / SurvivalInventory 模式）。
//   （② 取产物→光标：t622 已实现（heldCustomName 通道，光标空 → 产物带名上光标）核验不改；⑤ 取后清
//     A+B 防重复取：rv11/t578/t615 已实现——清空段在所有早退之后无条件执行，本轮核验+注释钉死，不改。）
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
// 产物输出路由（rv11 修「取产物静默销毁」→ t626② 收敛为恒光标）：点产物槽 → 消耗等级 + 材料后，产物
//   **写光标**（held 系统）：光标空 → 产物带耐久 / 附魔 / 名上光标（t622 heldCustomName 通道）；光标持
//   同 id 且装得下 → 合并；光标被异物占用 → **无操作**（不消耗等级 / 材料 / 输入；t626② 删旧「addToAny
//   入包」退路——那是 shift 语义，且旧实现把入包放在 spendLevels 拒付之前 = 等级不足时产物已入包 →
//   产物槽可反复点 = 无限复制。机制等价 MC：光标被占时铁砧产物不可取）。
//   产物占位 = 本地 anvil 组 index 2（preview 只显不可交互）。
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
    // t619 宿主注入：玩家进度 VM（铁砧成功操作埋点 progress.onAnvilUsed →「铁匠」成就）。
    //   var 避免类型解析耦合（同 player 模式）。
    property var progress: null
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
    //   权威）。**耐久 / 附魔 / 名随实例保真**（工具 / 护甲进左槽须修 / 合并，须保住实例耐久 + 附魔 + 铁砧改名；
    //   数组写入不触发绑定 → 显示 / 预览经 anvilRev 触碰驱动）。
    property var anvilSlots:  [0, 0, 0]
    property var anvilCounts: [0, 0, 0]
    property var anvilDur:    [0, 0, 0]
    property var anvilEnch:   [[0,0,0,0], [0,0,0,0], [0,0,0,0]]
    // t622 本地槽实例名（空串 = 注册表默认名）。左槽放入改名物品 → 名随实例进槽；takeProduct 产物 / 关包
    //   归还均透传（修「铁砧改名放回背包还是旧名」根因之一：本地槽无名通道）。
    property var anvilNames:  ["", "", ""]
    property int anvilRev: 0

    // 取槽附魔元数据（数组未初始化防御 → 4 个 0）。InventoryOps 读写经 readSlot/writeSlot 路由用它保真。
    function enchAt(idx) {
        const e = root.anvilEnch[idx]
        return (Array.isArray(e) && e.length === 4) ? e : [0, 0, 0, 0]
    }
    // t622 取槽实例名（数组未初始化防御 → 空串）。
    function nameAt(idx) {
        const n = root.anvilNames[idx]
        return (typeof n === "string") ? n : ""
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
    //   t622：local 槽透传实例名（铁砧改名物品进槽保真）。
    function localReadSlot(group, index) {
        if (group === "anvil")
            return { id: root.anvilSlots[index] || 0, count: root.anvilCounts[index] || 0,
                     durability: root.anvilDur[index] || 0, enchants: root.enchAt(index), name: root.nameAt(index) }
        return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0], name: "" }
    }
    function localWriteSlot(group, index, id, count, durability, enchants, name) {
        if (group !== "anvil") return
        root.anvilSlots[index] = id
        root.anvilCounts[index] = count
        // review rv3：durability 缺省经 InventoryOps 归一为 -1（自动）；本地槽只存实例值（>0）或 0，
        //   防 -1 残留进 anvilDur（returnAnvilToHotbar 的 `-1 || 0` 为真值会透传 -1 → addStack 视作新实例）。
        root.anvilDur[index] = (durability > 0) ? durability : 0
        const e = (Array.isArray(enchants) && enchants.length === 4) ? enchants : [0, 0, 0, 0]
        const arr = root.anvilEnch
        arr[index] = e.slice()
        root.anvilEnch = arr
        // t622 实例名随槽写入（undefined 兜底空串）。
        root.anvilNames[index] = (typeof name === "string") ? name : ""
        root.anvilRev++
    }
    function resolveClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveClick(root, curId, curCount, curDur, curEnch, curName) }
    function resolveRightClick(curId, curCount, curDur, curEnch, curName) { return InventoryOps.resolveRightClick(root, curId, curCount, curDur, curEnch, curName) }
    function readSlot(group, index) { return InventoryOps.readSlot(root, group, index) }
    function writeSlot(group, index, id, count, durability, enchants, name) { InventoryOps.writeSlot(root, group, index, id, count, durability, enchants, name) }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → InventoryOps.slotShiftLeft（Shift+左键搬运 anvil↔main↔hotbar）。
    //   t549：先走 slotShiftLeftAnvil（铁砧专属双向语义：工具/护甲→左槽 0 / 修复材料·附魔书→右槽 1 / anvil
    //   槽→归背包；同附魔台 slotShiftLeftEnchant 模式）。
    //   t180：可拖拽组（anvil/main/hotbar）双击 → doMergeSameId（拿同类）。resolveClick/resolveRightClick 算法见
    //   InventoryOps（六面板共享，调用点零改动）。t550 耐久 / 附魔透传（curDur/curEnch 取本地槽保真值）。
    // t626③ 改名框退出输入态：槽交互 / 取产物成功后把活动焦点归还键位层（宿主 window.refocusKeyInput）。
    //   根因：改名框 TextInput 点按夺焦（activeFocusOnPress）后，点槽不会自动失焦（槽 TapHandler 不抢
    //   焦点）→ 框持焦期间按 E 关面板 / 数字键切槽全打进框（E 变文本、1-9 变文本且 hotbar 不切）。焦点
    //   回键位层后 E / Esc / Shift / 数字键恢复面板语义；只有再点改名框才重新聚焦（TextInput 原生行为）。
    //   单纯 nameInput.focus = false 会把焦点丢给窗口 contentItem（keyInput 的 Keys 收不到键）→ 必须经
    //   宿主 forceActiveFocus(keyInput)。宿主未注入 refocusKeyInput（旧宿主）→ 真值兜底仅清本地焦点。
    function defocusNameBox() {
        if (window.refocusKeyInput) window.refocusKeyInput()
        else nameInput.focus = false
    }

    function slotLeft(group, index) {
        if (window.shiftHeld) { slotShiftLeftAnvil(group, index); root.defocusNameBox(); return }
        // t180：280ms 内同槽二次点击 + 可拖拽组 → 拿同类（doMergeSameId 扫 anvil+main+hotbar 同 id）。
        const key = group + ":" + index
        const now = Date.now()
        const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
        root.lastTapMs = now
        root.lastTapKey = key
        if (isDouble && InventoryOps.groupIsDraggable(root, group)) {
            root.defocusNameBox()   // review D1-d：t626③ 焦点修在此分支漏调（其它交互分支都退框）—— 双击拿同类也是槽交互，改名框须退出输入态（焦点回键位层）
            InventoryOps.doMergeSameId(root, group, index)
            return
        }
        const cur = InventoryOps.readSlot(root, group, index)
        const r = InventoryOps.resolveClick(root, cur.id, cur.count, cur.durability, cur.enchants, cur.name)
        if (!r) { root.defocusNameBox(); return }   // t626③ 无操作也退框（点了槽 = 意图离开输入态）
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
        root.hotbar.heldDurability = r.heldDur
        root.hotbar.setHeldEnchants(r.heldEnch)
        root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
        root.defocusNameBox()                     // t626③ 槽交互后改名框退出输入态（焦点回键位层）
    }
    function slotRight(group, index) {
        const cur = InventoryOps.readSlot(root, group, index)
        const r = InventoryOps.resolveRightClick(root, cur.id, cur.count, cur.durability, cur.enchants, cur.name)
        if (!r) return
        InventoryOps.writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
        root.hotbar.heldDurability = r.heldDur
        root.hotbar.setHeldEnchants(r.heldEnch)
        root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
        root.defocusNameBox()                     // t626③ 槽交互后改名框退出输入态（焦点回键位层）
    }

    // t549 铁砧 Shift+左键双向语义（spec「shift+左键应把工具直接放进去」；同附魔台 slotShiftLeftEnchant 模式）：
    //   - main/hotbar 工具 / 护甲（maxDur > 0）→ 整件入左输入槽 0（耐久 / 附魔随实例保真；槽 0 占用不覆盖）。
    //   - main/hotbar 修复材料（anvilCanRepairMaterial 对槽 0 物品为真）或附魔书（0x227）→ 并入右输入槽 1。
    //   - anvil 输入槽（0/1）→ 整件归还背包（addToAny）；背包满 → 余数留原槽（防丢物）。
    //   - 产物槽（index 2）→ 通用退路（preview 槽无 shift 语义）。
    //   - 其余物品 → 通用 slotShiftLeft（main↔hotbar 整理）。
    function slotShiftLeftAnvil(group, index) {
        if (!root.hotbar) return
        if (group === "main" || group === "hotbar") {
            const src = InventoryOps.readSlot(root, group, index)
            if (src.id === 0 || src.count <= 0) return
            // 工具 / 护甲（有耐久语义的物品）→ 左输入槽 0。t578：槽 0 已放同 id 物 → 第二件入右槽 1
            //   （同物合并分支输入）。t615 附魔书（0x227）入左槽（书书合并的 A 端）。review M2：槽 0 被
            //   **异物**占用时附魔书不再 no-op —— 落到下方材料分支入右槽 1（A=工具 + Shift 点附魔书 =
            //   敲书输入；原实现第一分支无条件吞书 → 槽 0 有物即 return，书永远到不了 B 槽、材料分支的
            //   isBook 附魔保真写入成死码）。
            if (root.maxDur(src.id) > 0 || src.id === 0x227) {
                const target = InventoryOps.readSlot(root, "anvil", 0)
                if (target.id === src.id && target.id !== 0) {
                    const t1 = InventoryOps.readSlot(root, "anvil", 1)
                    if (t1.id !== 0) return
                    InventoryOps.writeSlot(root, group, index, 0, 0, 0)
                    InventoryOps.writeSlot(root, "anvil", 1, src.id, 1, src.durability, src.enchants, src.name)
                    return
                }
                if (target.id === 0) {
                    InventoryOps.writeSlot(root, group, index, 0, 0, 0)
                    InventoryOps.writeSlot(root, "anvil", 0, src.id, 1, src.durability, src.enchants, src.name)
                    return
                }
                // 槽 0 异物占用：工具 / 护甲 → 不覆盖（no-op）；附魔书 → 落到下方材料分支（review M2）。
                if (src.id !== 0x227) return
            }
            // 修复材料（对槽 0 物品）或附魔书 → 右输入槽 1。
            const left = InventoryOps.readSlot(root, "anvil", 0)
            const isBook = src.id === 0x227   // RecipeRegistry::EnchantedBookId（同 canMerge 硬编码）
            if ((left.id !== 0 && root.hotbar.anvilCanRepairMaterial(left.id, src.id)) || isBook) {
                const target = InventoryOps.readSlot(root, "anvil", 1)
                if (target.id !== 0 && target.id !== src.id) return // 异物占位 → 不覆盖
                const cap = root.hotbar.maxStackSize(src.id)
                const space = cap - target.count
                if (space <= 0) return
                const move = Math.min(space, src.count)
                // t615 附魔书（maxStack=1，每本独立附魔列表）→ 整件入槽且**附魔随实例保真**（同工具语义）；
                //   修复材料（可堆叠、无附魔）→ 惯例 0。
                InventoryOps.writeSlot(root, "anvil", 1, src.id, target.count + move, 0,
                                      isBook ? src.enchants : [0,0,0,0])
                const remain = src.count - move
                InventoryOps.writeSlot(root, group, index, remain > 0 ? src.id : 0, remain, 0)
                return
            }
            // 其余 → 通用 main↔hotbar 搬运。
            InventoryOps.slotShiftLeft(root, group, index)
            return
        }
        if (group === "anvil" && (index === 0 || index === 1)) {
            const src = InventoryOps.readSlot(root, "anvil", index)
            if (src.id === 0 || src.count <= 0) return
            const remain = root.hotbar.addToAny(src.id, src.count, src.durability, src.enchants, src.name)
            InventoryOps.writeSlot(root, "anvil", index, remain > 0 ? src.id : 0, remain,
                                  remain > 0 ? src.durability : 0, remain > 0 ? src.enchants : [0,0,0,0],
                                  remain > 0 ? src.name : "")
            return
        }
        InventoryOps.slotShiftLeft(root, group, index)
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
    //   退回背包（MC 行为：关铁砧界面把输入槽物品退回背包）。t550 耐久 / 附魔随实例归还
    //   （第 3/4 参透传：工具 / 护甲保真回包）。t622 名随实例归还（第 5 参透传）。
    //   review rev2-C5：addToAny 带名守卫（带名整栈不并入既有栈 → 背包无空位时返 leftover）→ 余量经
    //   player.dropItemAtFront 丢实体（同 Main.returnHeldToHotbar 满包丢弃模式，§2-E 不静默吞）。
    //   review rv2-A3（aecd2b8 同批修法，漏网处）：addStack → addToAny（main 27 + hotbar 9 智能堆叠——旧版
    //   只填 hotbar 9 槽，主栏有空间仍报满 → 余量被不必要丢弃）。丢实体兜底保留（3dbe0de 注入的 player
    //   通道；§2-E 满包丢弃是既定模式）。
    function returnAnvilToHotbar() {
        if (!root.hotbar) return
        for (let i = 0; i < root.anvilSlots.length; ++i) {
            const id = root.anvilSlots[i] || 0
            const n = root.anvilCounts[i] || 0
            if (id !== 0 && n > 0) {
                const leftover = root.hotbar.addToAny(id, n, root.anvilDur[i] || 0, root.enchAt(i), root.nameAt(i))
                if (leftover > 0 && root.player) root.player.dropItemAtFront(id, leftover, root.enchAt(i), root.nameAt(i))
            }
        }
        for (let i = 0; i < root.anvilSlots.length; ++i) {
            root.anvilSlots[i] = 0
            root.anvilCounts[i] = 0
            root.anvilDur[i] = 0
        }
        root.anvilEnch = [[0,0,0,0], [0,0,0,0], [0,0,0,0]]
        root.anvilNames = ["", "", ""]
        root.anvilRev++
    }
    onVisibleChanged: {
        if (!visible) returnAnvilToHotbar()
        else { renameName = ""; nameInput.text = ""; lastAutoName = ""; lastResult = "" }
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
    // t606③ 创造模式免经验：player.mode === Creative → affordCost 恒真 + spendLevels 跳过（机制等价 MC
    //   创造铁砧免经验；材料消耗照旧——保守只免经验）。player 属性已由宿主注入（Main.qml player: player）。
    readonly property bool creativeMode: root.player && root.player.mode === PlayerController.Creative
    // 重命名输入文本。
    property string renameName: ""
    // t606②/⑦ 自动填名守卫：上次自动填入的名字（左槽放入物品时记录）。仅「框空 或 内容 === lastAutoName」
    //   时新的自动填充才覆盖 → 用户改过（≠lastAutoName）则换物不覆盖，防打字被顶掉。左槽清空时随框一起复位。
    property string lastAutoName: ""
    // 操作结果 flash（成功后短暂显绿）。
    property string lastResult: ""
    property bool justActed: false
    // t622 改名产物空槽落点（takeProduct 探路段找、落定段用；跨段存属性防块作用域撕裂）。
    //   t626② 已废弃：异物光标改「无操作」（不再退路找空槽入包）→ 两属性无消费者，删除。

    // t606②/⑦ 自动填名 / 拿回清框：左槽内容变（anvilRev 触碰）→ 有物：框空或内容仍是上次自动填充值
    //   （=== lastAutoName）→ 填入该物品当前名（nameForBlock 注册默认名；本地槽无 customName 通道）+
    //   记 lastAutoName；用户已改（≠lastAutoName）→ 不覆盖。左槽空 → 框清空 + lastAutoName 复位（重新
    //   放物再自动填名）。命令式改 nameInput.text 不触发 onTextEdited（程序赋值不发 textEdited）→
    //   renameName 需同步手写，避免两者失同步。
    //   ⚠ 直读 anvilSlots[0] 而非绑定属性 leftId：槽数组是原地元素赋值（不发 var NOTIFY）→ leftId 绑定
    //   要等 anvilRev 变更后才重求值；本 handler 与该重求值同信号触发、顺序不保证（stale leftId 会在
    //   takeProduct 清槽后把名字填回框里）。数组直读永远新鲜（Imperative 代码无 AOT 触碰问题）。
    onAnvilRevChanged: {
        const lid = root.anvilSlots[0] || 0
        if (lid !== 0) {
            // t622：优先显左槽物品实例名（nameAt(0)——改名物品重新放入铁砧时框内显其当前名而非注册默认名）；
            //   无实例名 → 注册默认名 nameForBlock。
            const nm = root.nameAt(0)
            const auto = nm.length > 0 ? nm : (root.hotbar ? root.hotbar.nameForBlock(lid) : "")
            if (nameInput.text.length === 0 || nameInput.text === root.lastAutoName) {
                nameInput.text = auto
                root.renameName = auto
                root.lastAutoName = auto
            }
        } else {
            nameInput.text = ""
            root.renameName = ""
            root.lastAutoName = ""
        }
    }
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
    // 右槽材料能否触发修复（t578 修「铁镐+铁锭产物空」）：左=工具/护甲 + 右=该物品修复材料即可合成。
    //   review L5：拆两层——repairPairValid（组合合法：左工具/护甲 + 右匹配材料，不看耐久）+
    //   canRepair（合法组合**且确有耐久缺失** repairMatNeeded>0）。t578 曾删耐久 gate（「满耐久也出产物」），
    //   但 repairMatUse()=0 时 cost=Math.max(1,0)=1 级、材料不耗、耐久不变 = **静默抽 1 级**（review L5）。
    //   现口径：满耐久无改名 → 产物空（不出产物即不收费）；满耐久 + 改名 → 走 rename 分支（只收改名 1 级，
    //   材料不动——canRename 据 repairPairValid 放行）。
    readonly property bool repairPairValid: {
        const _r = root.anvilRev
        if (_r < 0) return false
        if (root.leftId === 0 || root.matId === 0) return false
        if (root.leftCount <= 0) return false
        if (root.maxDur(root.leftId) <= 0) return false
        if (!root.hotbar || !root.hotbar.anvilCanRepairMaterial(root.leftId, root.matId)) return false
        return true
    }
    readonly property bool canRepair: root.repairPairValid && repairMatNeeded(root.leftId, root.leftDur) > 0
    // t578 同物合并分支：左右同 id（双铁镐/双护甲）→ 合并耐久 + 附魔并集（机制等价 MC 两件合并修复）。
    //   仅限有耐久语义的物品（工具/护甲）；可堆叠材料/方块同 id 不合成（落入「其余组合 → 产物空」）。
    readonly property bool canCombine: {
        const _r = root.anvilRev
        if (_r < 0) return false
        if (root.leftId === 0 || root.matId === 0) return false
        if (root.leftId !== root.matId) return false
        if (root.leftCount <= 0 || (root.anvilCounts[1] || 0) <= 0) return false
        return root.maxDur(root.leftId) > 0
    }
    // 附魔合并前置（t615 真附魔书）：左槽可附魔（工具 / 护甲，itemEnchantCategory != 0）+ 右槽附魔书
    //   → 书上附魔逐条尝试写入 C（适用过滤 + 冲突组 + 等级合并，见 computeBookMerge）。
    //   t615 书书合并：左槽 = 附魔书 + 右槽 = 附魔书 → C = 合并书（同合并规则；冲突项以 B 替换 A 生成 C，
    //   见 computeBookMerge 的 bookMerge 分支）。0x227 = RecipeRegistry::EnchantedBookId。
    readonly property bool canMerge: {
        const _r = root.anvilRev
        if (_r < 0) return false
        if (root.matId !== 0x227) return false
        // 分支一：左槽可附魔物（工具 / 武器 / 护甲）+ 右槽附魔书。review M3：普通书（0x238）**不是合法铁砧
        //   目标**——itemEnchantCategory(0x238)=BookItem ≠ 0 会放它进本分支，但 takeProduct 只对 leftId===0x227
        //   翻附魔书 id → 产物成「带附魔的普通书」（可堆叠 64，与普通书合并即掉附魔 + 进不了附魔台；A 满栈时
        //   其余 63 本直接销毁）。排除 0x238（书只能经附魔台变成附魔书 0x227，再进铁砧）。
        if (root.leftId !== 0 && root.leftId !== 0x238
                && root.hotbar && root.hotbar.itemEnchantCategory(root.leftId) !== 0) return true
        // 分支二：两本附魔书合并（左 = 附魔书 + 右 = 附魔书 → 合并书）。
        if (root.leftId === 0x227) return true
        return false
    }
    // t615 附魔书合并计算（纯函数；canMerge 真时生效）。把 B 槽书的附魔逐条尝试写入 C（初始 = A 附魔副本）：
    //   - 适用过滤：enchantApplicableTo（剑类附魔不上镐 / 摔落保护仅靴…）→ 不适用条目不上（灰显提示）。
    //   - 冲突组：与 C 已有附魔互斥（enchantConflictsWith；锐锋族 / 采集族 / 保护系）→ 不上（红字「冲突」）。
    //   - 等级合并：C 已有同款 Lc、书 Lb → max(Lc, Lb)；Lc==Lb → Lc+1（封顶 enchantMaxLevel）；C 无 → 直接写 Lb。
    //   - 4 槽写满后续条目不上（附魔槽上限）。
    //   返回 { out: 4-int 合并结果, applied: 成功写入条数, conflictNames: 冲突条目中文名表,
    //          inapplicableNames: 不适用条目中文名表, replaced: 书书合并时 B 顶掉 A 的冲突条数 }。
    //   书书合并（leftId===0x227）差异：A 也是书 → 目标域判定对「书」恒适用（载体）；同款等级合并同规则；
    //   **互斥冲突条目按用户口径「B 替换 A」**——A 上与 B 冲突的旧附魔被 B 的新附魔替换（如 A 锐锋 II +
    //   B 亡灵杀手 III → C 亡灵杀手 III，锐锋被顶掉）。
    function computeBookMerge() {
        const out = root.enchAt(0).slice()        // C 初始 = A 全属性（附魔 / 耐久 / 名走各字段）
        const src = root.enchAt(1)                // B 书附魔
        const isBookMerge = (root.leftId === 0x227)
        const conflictNames = []
        const inapplicableNames = []
        let applied = 0
        let replaced = 0
        for (let i = 0; i < 4; ++i) {
            const packed = src[i] || 0
            if (packed === 0) continue
            const eid = (packed >> 8) & 0xFF
            const lvl = packed & 0xFF
            const name = root.hotbar.enchantDisplayName(eid)
            // 适用过滤：工具 / 护甲目标按 isApplicableForItem 精判；书书合并对「书」恒适用（载体）。
            if (!isBookMerge && !root.hotbar.enchantApplicableTo(eid, root.leftId)) {
                inapplicableNames.push(name)
                continue
            }
            // 冲突组：与 C 已有附魔互斥 → 工具目标不上（红字冲突）；书书合并 → B 替换 A（先移除被顶旧条）。
            let conflictSlot = -1
            for (let j = 0; j < 4; ++j) {
                const p = out[j] || 0
                if (p !== 0 && root.hotbar.enchantConflictsWith((p >> 8) & 0xFF, eid)) { conflictSlot = j; break }
            }
            if (conflictSlot >= 0) {
                if (!isBookMerge) { conflictNames.push(name); continue }
                out[conflictSlot] = 0            // 书书合并：B 顶掉 A 的冲突旧附魔
                replaced++
            }
            // 等级合并：同款 → max / 相等 +1（封顶）；无 → 首个空槽写入。
            let slot = -1
            for (let j = 0; j < 4; ++j) {
                const p = out[j] || 0
                if (p !== 0 && ((p >> 8) & 0xFF) === eid) { slot = j; break }
            }
            if (slot >= 0) {
                const cur = out[slot] & 0xFF
                const cap = root.hotbar.enchantMaxLevel(eid)
                let merged = (cur === lvl) ? cur + 1 : Math.max(cur, lvl)
                merged = Math.min(merged, cap)
                out[slot] = (eid << 8) | merged
                applied++
            } else {
                let freeSlot = -1
                for (let j = 0; j < 4; ++j) {
                    if ((out[j] || 0) === 0) { freeSlot = j; break }
                }
                if (freeSlot >= 0) { out[freeSlot] = (eid << 8) | Math.min(lvl, root.hotbar.enchantMaxLevel(eid)); applied++ }
                // 4 槽满 → 不上（静默；附魔槽上限）。
            }
        }
        return { out: out, applied: applied, conflictNames: conflictNames,
                 inapplicableNames: inapplicableNames, replaced: replaced }
    }
    // review M9 同物合并（combine）附魔并集计算（纯函数；canCombine 真时生效）。合并内核与
    //   computeBookMerge 同规则 —— 两件输入的附魔逐条并集，机制等价 MC 两件合并（规则单一权威）：
    //   - 冲突组过滤：右件附魔与左件已有附魔互斥（enchantConflictsWith；锐锋族 / 采集族 / 保护系）→ **不上**
    //     （与敲书路径同口径；原实现无冲突过滤 → 锐锋剑 + 亡灵杀手剑可合出互斥共存，破坏同批建立的不变量）。
    //   - 等级合并：同款 → max(l, r)；相等 → +1（封顶 enchantMaxLevel；与 computeBookMerge 同口径，原实现
    //     同款同等级不加级、不封顶）。
    //   - 无同款 → 首个空槽写入（等级钳 maxLevel；4 槽满 → 静默不上）。
    //   两件同物 → 适用域天然一致（同 id 物品适用面相同），无需 enchantApplicableTo 再过滤（区别于敲书路径
    //   书可载任意附魔）。返回 4-int 合并结果。
    function computeCombineEnch(leftEnch, rightEnch) {
        const out = leftEnch.slice()               // C 初始 = 左件附魔（副本，勿污染 anvilEnch[0] 原数组）
        for (let i = 0; i < 4; ++i) {
            const packed = rightEnch[i] || 0
            if (packed === 0) continue
            const eid = (packed >> 8) & 0xFF
            const lvl = packed & 0xFF
            // 冲突组：与 C 已有附魔互斥 → 不上（合并路径无「B 替换 A」语义——两件都是真装备，保左件）。
            let conflict = false
            for (let j = 0; j < 4; ++j) {
                const p = out[j] || 0
                if (p !== 0 && root.hotbar.enchantConflictsWith((p >> 8) & 0xFF, eid)) { conflict = true; break }
            }
            if (conflict) continue
            // 等级合并：同款 → max / 相等 +1（封顶）；无 → 首个空槽写入。
            let slot = -1
            for (let j = 0; j < 4; ++j) {
                const p = out[j] || 0
                if (p !== 0 && ((p >> 8) & 0xFF) === eid) { slot = j; break }
            }
            if (slot >= 0) {
                const cur = out[slot] & 0xFF
                const cap = root.hotbar.enchantMaxLevel(eid)
                let merged = (cur === lvl) ? cur + 1 : Math.max(cur, lvl)
                out[slot] = (eid << 8) | Math.min(merged, cap)
            } else {
                for (let j = 0; j < 4; ++j) {
                    if ((out[j] || 0) === 0) {
                        out[j] = (eid << 8) | Math.min(lvl, root.hotbar.enchantMaxLevel(eid))
                        break
                    }
                }
            }
        }
        return out
    }
    // 重命名前置：左槽有物 + 名字**真改过**（≠自动填充值 lastAutoName；t606② 自动填名后框恒非空，
    //   改名产物须以用户实际修改为前提——未改名的物品单独放 A 不出产物，机制等价 MC「名字栏与当前名
    //   相同则无改名操作」）。改名可与修复 / 合并叠加（MC：改名 + 修复合算等级）。
    //   t606④ 收紧：B 非空且与 A 无任何合法关系（非修复材料 repairPairValid / 非同 id canCombine / 非附魔书
    //   canMerge 三分支全不中）→ 改名也不出产物（C 直接消失）——原 bug：A 工具 + B 泥土仍显改名产物。
    //   review L5：B = 匹配修复材料但满耐久（canRepair 假）→ 据 repairPairValid 放行 rename（只收改名 1 级，
    //   材料不动）。
    //   B 空 + 改了名 → 单独改名合法（用户规格：「只放 A、B 没东西那就是可以的」）。
    readonly property bool canRename: {
        const _r = root.anvilRev
        const _n = root.renameName
        if (_r < 0 || root.leftId === 0) return false
        if (_n.trim().length === 0) return false
        if (_n.trim() === root.lastAutoName.trim()) return false
        if (root.matId !== 0 && !root.repairPairValid && !root.canCombine && !root.canMerge) return false
        return true
    }
    // 改名是否叠加在修复 / 合并之上（产物名即时显新名）。
    readonly property bool renaming: root.canRename
    // 当前生效操作（t578 三分支：修复材料 → 同物合并 → 附魔书 → 改名；改名可叠加前三者）。
    //   返 "repair" / "combine" / "merge" / "rename" / ""（空 = 不匹配组合 → 产物空，t578）。
    readonly property string activeOp: {
        const _r = root.anvilRev
        const _n = root.renameName
        if (_r >= 0 && root.canRepair) return "repair"
        if (_r >= 0 && root.canCombine) return "combine"
        if (_r >= 0 && root.canMerge) return "merge"
        if (_n.length >= 0 && root.canRename) return "rename"
        return ""
    }
    // t550-review 修复实耗材料数 = min(右槽实有, 修满所需)。放 1 锭只修 1/3 耐久、费 1 级（机制等价 MC
    //   「每材料补 1/3」+ 用户原话「一个铁锭补 1/3 的耐久度」）；不强制凑满 3 个才能修。
    function repairMatUse() {
        const have = root.anvilCounts[1] || 0
        return Math.min(have, repairMatNeeded(root.leftId, root.leftDur))
    }
    // t615 附魔书合并预览（canMerge 时恒新鲜；触碰 anvilRev）。缓存属性（绑定间共享，防 cost / productEnch /
    //   conflictText 三处各调 computeBookMerge 重复计算）。
    readonly property var bookMerge: {
        const _r = root.anvilRev
        return _r >= 0 && root.canMerge ? root.computeBookMerge() : { out: [0,0,0,0], applied: 0, conflictNames: [], inapplicableNames: [], replaced: 0 }
    }
    // t615 附魔合并等级上限（用户口径①：敲附魔书有等级惩罚且「如果超过最大上限将显示过于昂贵」——
    //   机制等价 MC 铁砧 40 级上限 Too Expensive）。合并消耗 > 上限 → 过于昂贵（不可合，红字）。
    readonly property int kMergeCostCap: 40
    readonly property bool mergeTooExpensive: root.canMerge && (2 * root.bookMerge.applied) > root.kMergeCostCap
    // 所需 XP 等级（产物格下绿字数值）。修复 = 实耗材料数（至少 1——canRepair 已保证 repairMatNeeded>0
    //   → repairMatUse ≥1，review L5；原 max(1,·) 兜的是「满耐久静默抽 1 级」已移除）；同物合并 = 2（机制
    //   等价 MC 合并修复计费档）；t615 附魔书合并 = 成功写入条数 × 2（用户口径①「敲附魔书的等级惩罚」；
    //   全不适用 / 全冲突 → 条数 0 → 仍可出产物（只继承 A），消耗照算 0 级 + 书照扣）；改名 = 1；改名叠加 → +1。
    readonly property int cost: {
        const op = root.activeOp
        if (op === "repair") return Math.max(1, root.repairMatUse()) + (root.renaming ? 1 : 0)
        if (op === "combine") return 2 + (root.renaming ? 1 : 0)
        if (op === "merge") return 2 * root.bookMerge.applied + (root.renaming ? 1 : 0)
        if (op === "rename") return 1
        return 0
    }
    // t606③ 创造模式免经验：affordCost 恒真（不管需几级都绿、可付；材料消耗照旧——保守只免经验）。
    //   t615「过于昂贵」不可付：merge 消耗超 40 级上限 → 即使创造也拒（用户口径①的硬上限；MC 创造同拒）。
    readonly property bool affordCost: root.mergeTooExpensive ? false : (root.creativeMode || playerLevel >= cost)
    // 产物槽等级文字（绿字可承担 / 红字经验不足；t576 删「放入物品与材料」灰字提示——无产物时静默空白；
    //   t606⑥ 改名分支只显消耗不再带产物名——名字已在输入框内 + 产物 tooltip（hoveredProductName）有；
    //   t615 附魔书合并显「附魔 ×N · 消耗 M 级」；超 40 级上限 → 「过于昂贵」（红字，不可合））。
    readonly property string costText: {
        const op = root.activeOp
        if (op === "repair") return "修复 " + cost + " 级"
        if (op === "combine") return "合并 " + cost + " 级"
        if (op === "merge") {
            if (root.mergeTooExpensive) return "过于昂贵"
            return "附魔 ×" + root.bookMerge.applied + " · 消耗 " + cost + " 级"
        }
        if (op === "rename") return "重命名 " + cost + " 级"
        return ""
    }
    readonly property string costColor: {
        if (root.activeOp === "") return "transparent"
        if (root.mergeTooExpensive) return "#e06f5f"   // t615 过于昂贵（恒红，创造也拒）
        return root.affordCost ? "#6fe06f" : "#e06f5f"
    }
    // t615 冲突 / 不适用提示行（merge 时等级行下方红字列出未上的附魔；dev-plan §5 UI 呈现）：
    //   「冲突：锐锋」= 与 C 已有附魔互斥（不上）；「不适用：保护」= 剑类附魔不上镐等（不上）。
    //   书书合并的 B 替换 A 冲突**可合**（非拒）→ 不列冲突行，改列「替换：锐锋 → 亡灵杀手」省字版（仅计数）。
    readonly property string mergeConflictText: {
        const _r = root.anvilRev
        if (_r < 0 || root.activeOp !== "merge") return ""
        const bm = root.bookMerge
        let out = ""
        if (bm.conflictNames.length > 0) out += "冲突：" + bm.conflictNames.join("、")
        if (bm.inapplicableNames.length > 0) out += (out.length > 0 ? "　" : "") + "不适用：" + bm.inapplicableNames.join("、")
        if (root.leftId === 0x227 && bm.replaced > 0) out += (out.length > 0 ? "　" : "") + "替换 ×" + bm.replaced
        return out
    }
    // 产物槽内容（t578 四分支：修复 → 修后耐久；同物合并 → 合并耐久；附魔书合并/改名 → 左槽原样；
    //   其余不匹配组合 → 产物空）。id / 耐久 / 附魔（改名产物名走 hoveredProductName / 改名框）。
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
            // t550-review：预览与 takeProduct 同口径 —— 实耗材料数修（1 锭修 1/3）。
            return Math.min(max, root.leftDur + root.repairMatUse() * (max / 3))
        }
        if (op === "combine") {
            // t578 同物合并：产物耐久 = min(max, 左 + 右 + 10% max 附加奖励)（机制等价 MC 两件合并公式
            //   result = min(max, d1 + d2 + 0.1*max)；两件全满 → 恒满，玩家无收益也不亏）。
            const max = root.maxDur(root.leftId)
            const rDur = root.anvilDur[1] || 0
            return Math.min(max, Math.floor(root.leftDur + rDur + max * 0.1))
        }
        if (op === "merge" || op === "rename") {
            // t615 merge 书书合并（leftId=附魔书）：书无耐久 → 产物耐久 0；工具 / 护甲目标继承左槽耐久。
            return (op === "merge" && root.leftId === 0x227) ? 0 : root.leftDur
        }
        return 0
    }
    readonly property var productEnch: {
        const _r = root.anvilRev
        if (_r < 0) return [0, 0, 0, 0]
        // t615 merge：产物附魔 = computeBookMerge 结果（适用过滤 + 冲突 + 等级合并后的 C）。
        if (root.activeOp === "merge") return root.bookMerge.out
        // review M9：combine 预览 = computeCombineEnch（冲突过滤 + 等级合并同内核）——与 takeProduct
        //   实际产物同口径（原预览只显左件附魔，玩家看到的与拿到的不一致）。
        if (root.activeOp === "combine") return root.computeCombineEnch(root.enchAt(0), root.enchAt(1))
        return root.enchAt(0)
    }
    // ════════════════════════════════════════════════════════════════════════════

    // ── t550 三功能执行（真逻辑；t477 占位交互替换）──
    //   修复：消耗 1 级/材料 + 消耗右槽材料（每材料修 1/3 满耐久）→ 产物 = 左槽修后耐久。合并附魔：消耗 2 级 +
    //   消耗右槽附魔书 1 本（多本只扣 1 本，rv11 修「合并销毁整摞书」）。改名：改名框非空时叠加在修复 / 合并之上
    //   （+1 级）或单独生效 → 产物即时显新名。产物输出路由（rv11 → t626② 收敛）：**恒走光标**（空 → 带名上
    //   光标；同 id → 合并；异物光标 → 无操作不消耗），不再入包。成功后清左输入槽 + 推进铁砧损坏。
    function takeProduct() {
        if (root.activeOp === "") return
        if (!root.affordCost) return
        const op = root.activeOp
        // rv11 修「产物写选中槽静默销毁」：先判产物去向（光标可持 / 背包可收），装不下则**不消耗等级 /
        //   材料、无操作**（原实现无条件 spendLevels + setStack(selectedSlot) → 选中槽 64 泥土 / 另一把工具
        //   被直接覆盖销毁）。仿 CraftingTableUI 产物拾取的光标模式（held 系统）：
        //   - 光标空 / 同 id 且累加不超上限 → 产物落光标（改名产物除外：见下方空槽路径）。
        //   - 光标被异物占用 → addToAny 找背包空位（不动光标原物；返回未放入数 > 0 = 背包满 → 无操作）。
        //   - 改名产物（outName 非空）：held 光标栈无 customName 通道 → 须落定即带名，找空槽 setStack +
        //     setCustomName（hotbar 优先 → main；无空槽 → 无操作）。异物光标 / 满背包均不消耗、不破坏。
        //   t615 merge 书书合并：产物 id 翻附魔书（0x227）→ let（非 const）。
        let outId = root.leftId
        // 单独改名可作用于可堆叠物品（左槽非工具也能 rename）→ 产物保留整栈数量（机制等价 MC「改名整栈
        //   保留」；原实现硬编码 1 会销毁 64 泥土的 63 件）。修复 / 合并要求工具 / 护甲（cap=1 恒单件）。
        const outCount = (op === "rename") ? Math.max(1, root.leftCount) : 1
        let outDur = root.leftDur
        let outEnch = root.enchAt(0)
        // t622 实例名通道打通（held / 槽 / 本地槽全链）：默认继承左槽实例名（改名物品修复 / 合并后仍带
        //   原名——修「修一下装备名字没了」）；改名框非空时用新名覆盖（下方 renaming 段）。
        let outName = root.nameAt(0)
        if (op === "repair") {
            const max = root.maxDur(outId)
            // t550-review 修：按实耗材料数修（use = min(右槽实有, 修满所需)），1 锭修 1/3、费 1 级；
            //   不再按 repairMatNeeded 满额修（原 bug：右槽只 1 锭免费修满 3/3 还按 3 级收费）。
            //   review L5：满耐久 + 材料不再走本分支（canRepair 耐久 gate）——原路径 use=0 时产物耐久不变
            //   却收 max(1,0)=1 级 = 静默抽级；现满耐久 + 改名 → rename 分支（只收改名 1 级、材料不动），
            //   满耐久不改名 → 无产物。
            const use = root.repairMatUse()
            outDur = Math.min(max, root.leftDur + use * (max / 3))
            root.lastResult = "修复完成 +" + root.cost + "级"
            // review L5：op==="repair" 仅在 repairMatNeeded>0 时可达（canRepair 含耐久缺失 gate）→ use 恒
            //   ≥1（min(实有, 所需)，所需 ≥1）——原「满耐久 use=0 仍出产物收 1 级」路径已移除。
        } else if (op === "combine") {
            // t578 同物合并（双铁镐/双护甲）：产物耐久 = min(max, d1+d2+10% max)（与 productDur 预览同口径），
            //   附魔并集走 computeCombineEnch（review M9：冲突过滤 + 同款等级合并 + 封顶，与预览 productEnch
            //   同一内核；原实现内联并集无冲突过滤 / 无同级 +1 / 无封顶，且原地改 enchAt(0) 返回的活数组——
            //   产物路由无操作 return 时左槽附魔已被污染）；两件输入均被消耗。
            const max = root.maxDur(outId)
            const rDur = root.anvilDur[1] || 0
            outDur = Math.min(max, Math.floor(root.leftDur + rDur + max * 0.1))
            outEnch = root.computeCombineEnch(outEnch, root.enchAt(1))
            root.lastResult = "合并完成 +2级"
        } else if (op === "merge") {
            // t615 真附魔书合并（computeBookMerge：适用过滤 + 冲突组 + 等级合并；预览 = 写入同口径）。
            //   工具 / 护甲目标：产物 = A 属性 + 书上成功写入条目；书书合并（A=书）：产物 = 合并书
            //   （B 顶掉 A 冲突项 → id 恒附魔书 0x227，耐久 0）。全不适用 / 全冲突 → 产物仍出（只继承 A，
            //   红字提示），消耗照算（applied=0 → 0 级）+ 书照扣（B 消耗 1 本）。
            const bm = root.computeBookMerge()
            outEnch = bm.out
            if (root.leftId === 0x227) {
                outId = 0x227                       // 书书合并 → 产物恒附魔书
                outDur = 0                          // 书无耐久
            }
            root.lastResult = bm.applied > 0
                ? "附魔合并 ×" + bm.applied + "（消耗 " + root.cost + "级）"
                : "无适用附魔（消耗 " + root.cost + "级）"
        } else { // rename
            root.lastResult = "已重命名"
        }
        // 同物合并（combine）的右槽已在上方消耗段清空；merge 的右槽书也在上方扣。
        // 改名叠加：改名框非空 → 产物名 = 新名（覆盖原 customName）。
        if (root.renaming) outName = root.renameName.trim()

        // ── 产物路由（rv11 / t622 / t626② 重定）── 探路段改为**纯只读**（t626②/⑤ 根因修复）：
        //   旧版探路段对「异物光标」当场 addToAny 把产物写进背包——两个后果：(a) 用户点一下产物却直接
        //   入包（相当于 shift 效果，用户「应到光标」）；(b) 写包发生在 spendLevels 拒付 / 后续无操作 return
        //   **之前** → 等级不足时产物已入包、输入槽未清 → 产物槽仍显 → 再点再入包 = **无限复制**（A 工具 +
        //   B 附魔书可无限刷）。t626②：异物光标 → **无操作**（机制等价 MC——光标被占时铁砧产物不可取），
        //   不写包、不消耗、产物槽保留预览；腾空光标再点即正常到光标。任何副作用（扣等级 / 清槽 / 写入）
        //   只发生在探路全通过之后的落定段。
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        const cap = root.hotbar.maxStackSize(outId)
        // 光标被异物占用 → 无操作（t626②：不再退路入包——「左键取产物」恒指光标通道；入包是 shift 语义）。
        if (heldId !== 0 && heldId !== outId) return
        // 同 id 光标合并容量检查（held 同 id 且累加超上限 → 无操作）。
        if (heldId === outId && heldCount + outCount > cap) return

        // ── 探路通过 → 真消耗（等级 + 材料 + 输入槽）──
        //   t606③ 创造模式免经验：跳过 spendLevels（机制等价 MC 创造铁砧免 XP；材料消耗照旧——保守只免
        //   经验，MC 创造材料也免但此处不扩大）。spendLevels 拒付 → return（此时**零副作用**已发生——
        //   探路段纯只读，故等级不足不会留半完成态；t626⑤ 与复制 bug 同根，一并根治）。
        if (!root.creativeMode && !root.playerState.spendLevels(root.cost)) return
        if (op === "repair") {
            // 消耗右槽材料：每修 1/3 需 1 件（取到 0 清空）。op==="repair" 时 use 恒 ≥1（canRepair 耐久
            //   gate，review L5）；use 归 0 清空槽位防 0 数量残留。
            const use = root.repairMatUse()
            root.anvilCounts[1] = Math.max(0, (root.anvilCounts[1] || 0) - use)
            if (root.anvilCounts[1] <= 0) { root.anvilSlots[1] = 0; root.anvilDur[1] = 0; root.anvilNames[1] = "" }
        } else if (op === "combine") {
            // t578 同物合并消耗**两件输入**（下方通用清左槽段 + 此处清右槽；两件合成一件）。
            root.anvilSlots[1] = 0; root.anvilCounts[1] = 0; root.anvilDur[1] = 0; root.anvilNames[1] = ""
        } else if (op === "merge") {
            // rv11 修「合并销毁整摞书」：合并只消耗 1 本附魔书（MC 语义：一次合并吃 1 本），整摞余本留在
            //   右槽（count-1；归 0 才清空）。原实现 anvilSlots[1]=0; anvilCounts[1]=0 把整摞书全销毁。
            const remainBook = (root.anvilCounts[1] || 0) - 1
            if (remainBook > 0) {
                root.anvilCounts[1] = remainBook
            } else {
                root.anvilSlots[1] = 0; root.anvilCounts[1] = 0; root.anvilDur[1] = 0; root.anvilNames[1] = ""
            }
        }
        // 清左输入槽 + 改名框（lastAutoName 一并复位；下方 anvilRev++ 触发 onAnvilRevChanged 左槽空分支
        //   同步清框，此处先行保持不变量「renameName 与输入框同步」）。t622：名数组一并清（名已随产物走）。
        root.anvilSlots[0] = 0; root.anvilCounts[0] = 0; root.anvilDur[0] = 0
        root.anvilEnch = [[0,0,0,0], [0,0,0,0], [0,0,0,0]]
        root.anvilNames = ["", "", ""]
        root.renameName = ""; nameInput.text = ""; root.lastAutoName = ""

        // ── 产物落定（t626② 简化：探路段已把光标收敛为「空 或 同 id」两态，恒走光标通道）──
        //   t622：held 光标有 customName 通道（Q_PROPERTY）→ 改名产物同普通产物直接带名上光标
        //   （机制等价 MC 铁砧产物左键拿到光标）。
        if (heldId === 0) {
            // 光标空 → 产物上光标（耐久 / 附魔 / 名随实例保真——t622 heldCustomName 通道）。
            root.hotbar.heldBlock = outId
            root.hotbar.heldCount = outCount
            root.hotbar.heldDurability = outDur
            root.hotbar.setHeldEnchants(outEnch)
            root.hotbar.heldCustomName = outName
        } else {
            // 光标持同 id → 合并（探路已保证不超上限）。t622：改名产物（outName 非空）合并入同 id 光标
            //   栈属罕见边角（同 id 可堆叠物品改名）→ 光标名保持不变（合并不搬实例元数据，同 InventoryOps C 路径）。
            root.hotbar.heldCount = heldCount + outCount
        }

        root.anvilRev++
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        // t619 progress 成就埋点：铁砧成功执行修复/合并/重命名 →「铁匠」（CraftingTableUI 同模式：
        //   root.progress 由 Main.qml 注入，成功路径末尾单点调用）。
        if (root.progress) root.progress.onAnvilUsed()
        root.justActed = true
        actFlashTimer.restart()
        root.defocusNameBox()   // t626③ 取产物成功 → 改名框退出输入态（焦点回键位层）
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
    // EnchantingTableUI）。宽度与 CraftingTableUI / 附魔台一致（392）；高度 = 标题(22) + 操作区(134) +
    // 主栏(120) + hotbar(40) + 间距/边距。t626① 高度压缩：430→370（用户「整个 UI 偏高，宽 OK」）——
    //   Column 边距 16→12、行距 12→10、操作区 150→134（t606① 的下移扩高部分回收；槽行/标签仍可读）。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 370                                  // t626① 12+22+134+120+40 + 3×10 spacing + 2×12 margin = 370
        anchors.centerIn: parent
        radius: 14
        color: "#1b1f24"
        border.color: "#3a444f"
        border.width: 1

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

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

            // ── 操作区（t576/t577 三轮 + t606 四轮 + t626① 压高：改名框在顶（产物上方）+ A + B → 箭头 → C 产物 + 产物下等级）──
            // t577：改名框移到槽行上方（MC 铁砧：名字栏在最顶、产物在右）。用户输入 → 产物即显新名。
            //   耐久不进名字栏。无「重命名」按钮（用户要求：产物槽即执行入口）。
            // t576：A/B 两输入槽中间加「+」符号；删「放入物品与材料」灰字提示。
            // t606①：操作区高 120→150 + 槽行上距加大 —— A+B→C 槽行整体下移（原太靠上贴改名框）。
            // t606②：改名框 placeholder 占位层删（自动填名把真名写进输入框，占位层冗余）。
            // t606⑤：框宽收窄对齐槽行（376→176）+ 背景调浅（#2a2018 深棕 → #3a3226 浅棕）+ 文字垂直居中
            //   （TextInput anchors.fill 与 anchors.verticalCenter 叠用无效 → 文字贴顶；改 verticalAlignment）。
            // t626①：操作区 150→134（面板高度压缩批；改名框上距 4→2、槽行上距 16→10）。
            Item {
                id: anvilArea
                width: parent.width
                height: 134

                // ── 改名框（t577 顶部；t606⑤ 收窄调浅居中；t626① 上距 2）── TextInput + 持焦时 Esc 关面板（规格⑥）。
                //   t606②：放入物品自动填名（onAnvilRevChanged），框内恒真文本可编辑，无占位层。
                Rectangle {
                    id: renameBox
                    anchors.top: parent.top; anchors.topMargin: 2
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 176; height: 26
                    color: "#3a3226"
                    border.color: (root.renaming && root.affordCost && root.activeOp !== "") ? "#ffd87a"
                                  : nameInput.activeFocus ? "#8a7a5a" : "#141008"
                    border.width: (root.renaming && root.affordCost && root.activeOp !== "") ? 2
                                  : nameInput.activeFocus ? 2 : 1
                    radius: 3
                    TextInput {
                        id: nameInput
                        anchors.fill: parent
                        anchors.leftMargin: 8; anchors.rightMargin: 8
                        color: "#ffe6a8"; font.pixelSize: 12
                        verticalAlignment: TextInput.AlignVCenter   // t606⑤ 文字垂直居中（原贴顶）
                        selectByMouse: true
                        maximumLength: 20
                        clip: true
                        onTextEdited: root.renameName = text
                        // 规格⑥：改名框持焦时 Esc 由输入框自己处理 → 关面板（closeAnvil → grab + 焦点回键位层），
                        //   不吞键（其余按键正常进输入框打字）。仿聊天输入框 Keys 处理（chatInput Keys.onPressed）。
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Escape) {
                                window.closeAnvil()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                if (root.activeOp !== "" && root.affordCost) {
                                    root.takeProduct()
                                    event.accepted = true
                                }
                            }
                        }
                    }
                }

                // A + B → C 槽行（t576：A/B 间「+」号；t606① 上距 8→16 下移；t626① 上距 16→10 压高批微收）。
                Item {
                    id: slotRow
                    anchors.top: renameBox.bottom; anchors.topMargin: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 176
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
                    // t576「+」号（A + B 两输入合并语义；配色同箭头灰）。
                    Text {
                        x: root.slotSize + 2; y: 0
                        width: 14; height: root.slotSize
                        text: "+"
                        color: "#8a8a8a"; font.pixelSize: 20; font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    // 右输入槽（anvil 组 index 1；材料：铁锭等修复材料 / 附魔书 / t578 同物合并第二件）。
                    Item {
                        x: root.slotSize + 16; y: 0
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
                        x: root.slotSize * 2 + 24; y: root.slotSize / 2 - 8
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
                        x: root.slotSize * 2 + 56; y: 0
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

                // 产物槽下等级绿字（规格⑤：等级显示在产物格下绿字；t576 无产物 → 静默空白（删灰字提示）；
                //   t606⑥ 改名不再带产物名——只显消耗「重命名 N 级」，名字在输入框 + 产物 tooltip 已有）。
                Text {
                    anchors.top: slotRow.bottom; anchors.topMargin: 2
                    anchors.horizontalCenter: slotRow.horizontalCenter
                    anchors.horizontalCenterOffset: root.slotSize + 38   // 对准产物槽（槽心 = 行心 + 78）
                    text: root.costText
                    color: root.costColor
                    font.pixelSize: 12; font.bold: true
                    visible: text.toString().length > 0
                }

                // t615 冲突 / 不适用红字提示行（等级行下方；merge 时书上未写入的附魔逐条列出）。
                Text {
                    anchors.top: slotRow.bottom; anchors.topMargin: 20
                    anchors.horizontalCenter: slotRow.horizontalCenter
                    width: slotRow.width + 60
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WrapAnywhere
                    text: root.mergeConflictText
                    color: "#e08a7f"
                    font.pixelSize: 9
                    visible: text.toString().length > 0
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
                        // t647 附魔光晕（主栏槽）：同 SurvivalInventory 主栏光晕。触碰 mainRevision 重算。
                        Rectangle {
                            anchors.fill: parent
                            visible: {
                                const _r = root.hotbar.mainRevision
                                if (_r < 0 || mainId === 0) return false
                                const e = root.hotbar.mainEnchantsAt(index)
                                return e && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
                            }
                            color: Qt.rgba(0.55, 0.25, 0.9, 0.25)
                            radius: 3
                            z: 3
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
                            // t647 附魔光晕（hotbar 槽）：同 SurvivalInventory hotbar 行光晕。触碰 slotRevision 重算。
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
        // 该槽是否有附魔（预览产物 / 输入工具附魔光晕判定）。t647：4 槽全查（旧版只查 e[0] —— 合并书 /
        //   combine 产物附魔可落在 1..3 槽 → 光晕漏显）。
        property bool hasEnch: {
            const _r = root.anvilRev
            if (_r < 0) return false
            if (aslot.slotId === 0) return false
            const e = aslot.slotEnch
            return Array.isArray(e) && ((e[0] || 0) !== 0 || (e[1] || 0) !== 0 || (e[2] || 0) !== 0 || (e[3] || 0) !== 0)
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: {
                // t626③ 点槽即退出改名框输入态（焦点回键位层）——空手点空槽的无操作路径也退。
                root.defocusNameBox()
                // 产物预览槽：点击 = 取产物（执行当前修复/合并/改名并写选中槽 + 清输入）。
                if (aslot.preview) {
                    if (aslot.slotId !== 0) root.takeProduct()
                    return
                }
                if (window.shiftHeld) { root.slotShiftLeftAnvil(aslot.group, aslot.index); return }
                // t180：280ms 内同槽二次点击 → doMergeSameId（拿同类；扫 anvil+main+hotbar 同 id）。
                const key = root.slotKey(aslot.group, aslot.index)
                const now = Date.now()
                const isDouble = (now - root.lastTapMs < 280) && (root.lastTapKey === key)
                root.lastTapMs = now
                root.lastTapKey = key
                if (isDouble) { root.doMergeSameId(aslot.group, aslot.index); return }
                const r = root.resolveClick(aslot.slotId, aslot.slotCount, aslot.slotDur, aslot.slotEnch,
                                            root.nameAt(aslot.index))
                if (!r) return
                root.writeSlot(aslot.group, aslot.index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
                root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
            }
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                root.defocusNameBox()   // t626③ 点槽即退出改名框输入态
                if (aslot.preview) return   // 产物槽右键无操作
                const r = root.resolveRightClick(aslot.slotId, aslot.slotCount, aslot.slotDur, aslot.slotEnch,
                                                 root.nameAt(aslot.index))
                if (!r) return
                root.writeSlot(aslot.group, aslot.index, r.slotId, r.slotCount, r.slotDur, r.slotEnch, r.slotName)
                root.hotbar.heldBlock = r.heldId
                root.hotbar.heldCount = r.heldCount
                root.hotbar.heldDurability = r.heldDur
                root.hotbar.setHeldEnchants(r.heldEnch)
                root.hotbar.heldCustomName = r.heldName   // t622 实例名随光标保真
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
    //   t622：悬停输入槽（anvil:0/1）时优先显槽内实例名（nameAt——改名物品放回铁砧仍显其名）。
    property string hoveredProductName: {
        const _ar = root.anvilRev
        const _n = root.renameName
        if (_ar >= 0 && root.hoveredKey === "anvil:2" && root.renaming) return _n.trim()
        if (_ar >= 0 && (root.hoveredKey === "anvil:0" || root.hoveredKey === "anvil:1"))
            return root.nameAt(root.hoveredKey === "anvil:0" ? 0 : 1)
        return ""
    }
    // t615 当前 hover 槽物品的附魔列表文本（tooltip 附魔行；同 EnchantingTableUI / SurvivalInventory 模式）：
    //   anvil:0/1 = 本地槽实例附魔；anvil:2 = 产物预览附魔（productEnch——修复 / 合并 / 敲附魔书后的结果）。
    //   t626⑥ 补 hotbar/main 分支（原仅 anvil 三槽 → 附魔书 / 附魔工具放背包行 tooltip 不显携带附魔+等级；
    //   对齐 EnchantingTableUI hoveredEnchantText 的 hotbar/main 路由）。
    //   附魔书物品（0x227）带附魔 → 列出携带附魔（机制等价 MC enchanted book tooltip）。
    property string hoveredEnchantText: {
        if (!root.hotbar || !root.hoveredItemId || !root.hoveredKey) return ""
        const _ar = root.anvilRev
        const _sr = root.hotbar.slotRevision
        const _mr = root.hotbar.mainRevision
        const key = root.hoveredKey
        const parts = key.split(":")
        if (parts.length !== 2) return ""
        const idx = parseInt(parts[1], 10)
        if (Number.isNaN(idx)) return ""
        if (parts[0] === "hotbar") return _sr >= 0 ? root.hotbar.enchantListText(root.hotbar.enchantsAt(idx)) : ""
        if (parts[0] === "main") return _mr >= 0 ? root.hotbar.enchantListText(root.hotbar.mainEnchantsAt(idx)) : ""
        if (parts[0] !== "anvil") return ""
        if (idx === 2) return _ar >= 0 ? root.hotbar.enchantListText(root.productEnch) : ""
        return _ar >= 0 ? root.hotbar.enchantListText(root.enchAt(idx)) : ""
    }
    // t622 当前 hover 槽（hotbar/main）物品的实例名（铁砧改名；空串 → tooltip 走注册默认名）。anvil 槽的
    //   实例名走 hoveredProductName（含产物改名预览）。
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
        visible: root.hotbar && root.hoveredItemId !== 0 && tipName.text !== ""
        z: 1000
        width: tipCol.implicitWidth + 14
        height: tipCol.implicitHeight + 8
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
        // t626④ tooltip 三行分列（名字 / 耐久 / 附魔各自独立配色 + 独立行）：
        //   - 名字行：实例名 / 改名预览名 → 金色加粗（与改名框强调色同族）；注册默认名 → 常规白。
        //   - 耐久行：灰字独立行「耐久 cur/max」——修旧 bug：原耐久只拼在默认名分支行尾，改名物品耐久行
        //     直接丢失（hoveredProductName 命中分支不拼耐久）。
        //   - 附魔行：紫字独立行（enchantListText 自带多行；附魔书列出携带附魔 + 等级，t626⑥ 背包行同显）。
        //   分列实现 = Column 三 Text 各自 color（非 richText 拼接——改名文本是任意用户输入，richText 会
        //   被尖括号注入，逐元素 Text 无需转义）。
        Column {
            id: tipCol
            anchors.centerIn: parent
            spacing: 3
            Text {
                id: tipName
                property bool customNamed: root.hoveredProductName.length > 0 || root.hoveredCustomName.length > 0
                text: {
                    if (!root.hotbar) return ""
                    if (root.hoveredProductName.length > 0) return root.hoveredProductName
                    if (root.hoveredCustomName.length > 0) return root.hoveredCustomName
                    return root.hotbar.nameForBlock(root.hoveredItemId)
                }
                color: customNamed ? "#ffd87a" : "#f2f2f2"   // t626④ 自定义名金色（区分注册默认名白）
                font.pixelSize: 12
                font.bold: customNamed
            }
            Text {
                visible: root.hoveredDurability >= 0
                text: root.hoveredDurability >= 0
                      ? "耐久 " + root.hoveredDurability + "/" + root.maxDur(root.hoveredItemId) : ""
                color: "#9aa4ae"   // t626④ 耐久灰字（与名字行区分）
                font.pixelSize: 11
            }
            Text {
                visible: root.hoveredEnchantText.length > 0
                text: root.hoveredEnchantText
                color: "#c58af0"   // t626④ 附魔紫字（与附魔光晕同族色）
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
