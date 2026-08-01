import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

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
    //   撤销上轮写入再重分）。in / fuel / out / main / hotbar 五类槽统一参与（与 SurvivalInventory / CraftingTableUI
    //   同算法；本文件无 craft 合成格，故 redistributeLive 无 craft 排除分支）。
    property bool leftDragActive: false
    property var dragSlots: []              // "in:0" / "fuel:0" / "out:0" / "main:5" / "hotbar:0"
    property int dragHeldId: 0
    property int dragHeldCount: 0
    // 实时重分撤销机制：dragOriginal 记每槽 drag 前原始栈（首次 encounter 快照）；dragWritten 记本轮已写槽。
    // 每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginLeftDrag / endLeftDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})

    // ── 栈操作（与 CraftingTableUI.qml 完全一致：拾取/放置/合并/互换 4 case）──
    function resolveClick(curId, curCount) {
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        if (heldId === 0) {
            if (curId === 0) return null
            return { slotId: 0, slotCount: 0, heldId: curId, heldCount: curCount }
        }
        if (curId === 0) {
            return { slotId: heldId, slotCount: heldCount, heldId: 0, heldCount: 0 }
        }
        if (curId === heldId) {
            const cap = root.hotbar.maxStackSize(curId)
            const space = cap - curCount
            if (space <= 0) return null
            const move = Math.min(space, heldCount)
            const remain = heldCount - move
            return { slotId: curId, slotCount: curCount + move,
                     heldId: remain > 0 ? heldId : 0, heldCount: remain }
        }
        return { slotId: heldId, slotCount: heldCount, heldId: curId, heldCount: curCount }
    }
    function resolveRightClick(curId, curCount) {
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        if (heldId === 0) {
            if (curId === 0) return null
            let half = Math.floor(curCount / 2)
            if (half < 1) half = 1
            return { slotId: curCount - half > 0 ? curId : 0, slotCount: curCount - half,
                     heldId: curId, heldCount: half }
        }
        if (curId !== 0 && curId !== heldId) return null
        const cap = root.hotbar.maxStackSize(heldId)
        if (curId === heldId && curCount >= cap) return null
        const remain = heldCount - 1
        return { slotId: heldId, slotCount: curCount + 1,
                 heldId: remain > 0 ? heldId : 0, heldCount: remain }
    }

    // ── 槽读写辅助（统一 5 个组：in / fuel / out / main / hotbar）──
    //   in/fuel/out 走本地属性 + slotRev（熔炉 3 槽不退回背包，跨开关持久，spec t97 明确保留本地）；
    //   main 走 hotbar VM（t97 三菜单共享）；hotbar 走 VM.setStack。
    function readSlot(group, index) {
        if (group === "in")    return { id: root.inId,   count: root.inCount }
        if (group === "fuel")  return { id: root.fuelId, count: root.fuelCount }
        if (group === "out")   return { id: root.outId,  count: root.outCount }
        if (group === "main")  return { id: root.hotbar.mainBlockIdAt(index), count: root.hotbar.mainCountAt(index) }
        if (group === "hotbar")return { id: root.hotbar.blockIdAt(index), count: root.hotbar.countAt(index) }
        return { id: 0, count: 0 }
    }
    function writeSlot(group, index, id, count) {
        if (group === "in")        { root.inId = id;   root.inCount = count;   root.slotRev++ }
        else if (group === "fuel") { root.fuelId = id; root.fuelCount = count; root.slotRev++ }
        else if (group === "out")  { root.outId = id;  root.outCount = count;  root.slotRev++ }
        else if (group === "main") { root.hotbar.mainSetStack(index, id, count) }
        else if (group === "hotbar"){ root.hotbar.setStack(index, id, count) }
    }

    // ── t167 左键拖动均分辅助（与 SurvivalInventory / CraftingTableUI 同算法，独立维护保持文件可读）──
    function slotKey(group, index) { return group + ":" + index }
    function dragHasKey(key) {
        for (let i = 0; i < root.dragSlots.length; ++i) if (root.dragSlots[i] === key) return true
        return false
    }
    function addDragSlot(key) {
        if (root.dragHasKey(key)) return
        // 异物槽不入 dragSlots（addDragSlot 前判）。仅在分发态（dragHeldId≠0）过滤——读槽当前栈，非空且
        // id≠dragHeldId 则跳过（与 redistributeLive 的 eligible 过滤一致；让绿框只亮真正会收物的空/同 id 槽）。
        // dragHeldId=0（空手拖）不过滤，让起点槽入 dragSlots 供 endLeftDrag→singleLeftClick。
        if (root.dragHeldId !== 0) {
            const p0 = key.split(":")
            const cur = root.readSlot(p0[0], parseInt(p0[1], 10))
            if (cur.id !== 0 && cur.id !== root.dragHeldId) return
        }
        root.dragSlots = root.dragSlots.concat([key])   // 新数组引用 → 依赖 dragSlots 的绑定刷新
        root.redistributeLive()                          // 每滑入新格实时重算 N 等分（撤销 + 重分）
    }
    // 手势生命周期（root DragHandler.onActiveChanged 调）。
    function beginLeftDrag() {
        root.dragHeldId = root.hotbar.heldBlock
        root.dragHeldCount = root.hotbar.heldCount
        root.dragSlots = []
        root.dragOriginal = ({})                        // 重置原始栈快照
        root.dragWritten = ({})                         // 重置已写槽记录
        root.leftDragActive = true
        if (root.hoveredKey !== "") root.addDragSlot(root.hoveredKey)   // 起点槽（按下时指针所在格）
    }
    function endLeftDrag() {
        if (!root.leftDragActive) return
        root.leftDragActive = false
        const n = root.dragSlots.length
        // N≥2 已在 drag 途中实时分完（redistributeLive 每滑入新格重算），此处不再均分。
        // N===1 退化为单格左键（拾取/放置/合并/互换）—— 微拖（越阈值但未离开起点格）的常规左键语义。
        if (n === 1) {
            const p = root.dragSlots[0].split(":")
            root.singleLeftClick(p[0], parseInt(p[1], 10))
        }
        root.dragSlots = []
        root.dragOriginal = ({})
        root.dragWritten = ({})
    }
    // 实时均分：每滑入新格（addDragSlot 触发）即重算 floor(dragHeldCount/N) 入格、余数实时回光标。撤销机制：
    // dragOriginal 快照每槽 drag 前原始栈（首次 encounter 拍），dragWritten 记本轮已写槽；下次重分前先据
    // dragOriginal 把已写格恢复，再按新 N 重分 → 用户看到「滑第 2 格变对半、第 3 格变三等分」的实时反馈。
    // 合格过滤：空槽 / 同 id 未满；异物 / 已满槽跳过。守恒：写回总量 + 余数 = dragHeldCount 快照。N≤1 不分
    // （保留单格左键给 endLeftDrag 处理；空手 drag 无意义）。与 SurvivalInventory / CraftingTableUI 同算法。
    function redistributeLive() {
        // 1) 撤销上一轮写入（恢复 dragOriginal 记录的原始栈），确保重分前所有 dragSlots 回到 drag 前态。
        for (const key in root.dragWritten) {
            const wp = key.split(":")
            const orig = root.dragOriginal[key]
            root.writeSlot(wp[0], parseInt(wp[1], 10), orig.id, orig.count)
        }
        root.dragWritten = ({})

        const heldId = root.dragHeldId
        const total = root.dragHeldCount
        const cap = root.hotbar.maxStackSize(heldId)

        // 2) 重建合格清单；首次 encounter 的槽拍原始栈快照（此后该槽读到的是本轮写入值，须靠快照还原）。
        let eligible = []
        const seen = {}
        for (let i = 0; i < root.dragSlots.length; ++i) {
            const key = root.dragSlots[i]
            if (seen[key]) continue
            seen[key] = true
            const p = key.split(":")
            if (!root.dragOriginal[key]) {
                const cur = root.readSlot(p[0], parseInt(p[1], 10))
                root.dragOriginal[key] = { id: cur.id, count: cur.count }
            }
            const orig = root.dragOriginal[key]
            if (orig.id === 0 || (orig.id === heldId && orig.count < cap))
                eligible.push({ group: p[0], index: parseInt(p[1], 10), key: key, base: orig.count })
        }

        // n>total 截断 eligible 到 total 项（每格至少 1 件；N≤count）。如 8 件拖 9 格 → 第 9 格不分，避免被
        // 「扫过即亮绿框」错觉。截断在 n<=1 早退之前。
        let n = eligible.length
        if (n > total) { eligible = eligible.slice(0, total); n = eligible.length }
        // N≤1 / 空手 / 无物：不分（保留单格左键给 endLeftDrag；空手 drag 无意义）。余数 = 原始快照。
        if (n <= 1 || heldId === 0 || total <= 0) {
            root.hotbar.heldBlock = heldId
            root.hotbar.heldCount = total
            return
        }

        // 3) floor(total/N) 入格（cap 钳制防溢出），余数留光标；记 dragWritten 供下轮撤销。
        const per = Math.floor(total / n)
        let remaining = total
        if (per > 0) {
            for (let i = 0; i < n; ++i) {
                const e = eligible[i]
                const place = Math.min(per, cap - e.base)
                if (place <= 0) continue
                root.writeSlot(e.group, e.index, heldId, e.base + place)
                root.dragWritten[e.key] = true
                remaining -= place
            }
        }
        root.hotbar.heldBlock = remaining > 0 ? heldId : 0
        root.hotbar.heldCount = remaining
    }
    // 单格左键（N===1 微拖退路 = resolveClick：空手拾取 / 持物放置 / 合并 / 互换）。正常单击走 per-slot
    //   TapHandler.onTapped（slotLeft）；仅当左键越阈值但只扫过起点一格时经此路径补一次单击语义。
    function singleLeftClick(group, index) {
        const cur = root.readSlot(group, index)
        const r = root.resolveClick(cur.id, cur.count)
        if (!r) return
        root.writeSlot(group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }

    // 统一槽点击 dispatch（左键整组 / 右键半份）。由各槽的两个 TapHandler（左 / 右各一）调用。
    // t110：slotLeft 入口先查 window.shiftHeld → 走 Shift+左键搬运（slotShiftLeft），与 SurvivalInventory /
    //   CraftingTableUI / Inventory 的「TapHandler 内 if (window.shiftHeld) slotShiftLeft(...) return」等效但
    //   集中在一处（本文件 5 类槽均经 slotLeft 路由，单点拦截最简洁）。
    function slotLeft(group, index) {
        if (window.shiftHeld) { root.slotShiftLeft(group, index); return }
        const cur = root.readSlot(group, index)
        const r = root.resolveClick(cur.id, cur.count)
        if (!r) return
        root.writeSlot(group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }

    // t110 Shift+左键搬运（MC 1.0 背包）：main 槽→首个空 hotbar 槽；hotbar 槽→首个空 main 槽；其它组
    //   （in / fuel / out）无操作（spec 仅定义 main↔hotbar；熔炉 3 槽不参与搬运，避免误把冶炼输入搬走）。
    //   与 SurvivalInventory / CraftingTableUI / Inventory 同算法。
    function slotShiftLeft(group, index) {
        if (group === "main") {
            const src = root.readSlot("main", index)
            if (src.id === 0) return
            for (let i = 0; i < root.hotbar.slotCount; ++i) {
                if (root.readSlot("hotbar", i).id === 0) {
                    root.writeSlot("main", index, 0, 0)
                    root.writeSlot("hotbar", i, src.id, src.count)
                    return
                }
            }
        } else if (group === "hotbar") {
            const src = root.readSlot("hotbar", index)
            if (src.id === 0) return
            for (let i = 0; i < root.hotbar.mainCount; ++i) {
                if (root.readSlot("main", i).id === 0) {
                    root.writeSlot("hotbar", index, 0, 0)
                    root.writeSlot("main", i, src.id, src.count)
                    return
                }
            }
        }
    }

    // t110 数字键交换：当前 hover 槽 ↔ hotbar[idx] 整栈互换（与 SurvivalInventory / CraftingTableUI 同算法）。
    //   hoveredKey 覆盖 in/fuel/out/main/hotbar 五类组（HoverHandler 维护），均可与 hotbar[idx] 互换。
    function swapHoveredWithHotbar(hotbarIdx) {
        if (root.hoveredKey === "") return
        const parts = root.hoveredKey.split(":")
        if (parts.length !== 2) return
        const group = parts[0]
        const srcIdx = parseInt(parts[1], 10)
        if (Number.isNaN(srcIdx)) return
        const src = root.readSlot(group, srcIdx)
        const dst = root.readSlot("hotbar", hotbarIdx)
        root.writeSlot(group, srcIdx, dst.id, dst.count)
        root.writeSlot("hotbar", hotbarIdx, src.id, src.count)
    }
    function slotRight(group, index) {
        const cur = root.readSlot(group, index)
        const r = root.resolveRightClick(cur.id, cur.count)
        if (!r) return
        root.writeSlot(group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }

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
                            if (hovered && root.leftDragActive) root.addDragSlot(key)
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
                            if (hovered && root.leftDragActive) root.addDragSlot(key)
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
                            if (hovered && root.leftDragActive) root.addDragSlot(key)
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
                                if (hovered && root.leftDragActive) root.addDragSlot(key)
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
                                    if (hovered && root.leftDragActive) root.addDragSlot(key)
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
