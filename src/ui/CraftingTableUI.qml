import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

// 工作台 3×3 合成面板（t50 / t63 完整 UI）：右键工作台方块打开（PlayerController::craftingTableOpened →
// Main.qml Connections → 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// t63 布局贴近 MC 1.0 工作台：上部「左 3×3 合成格 + 箭头 + 右结果槽」，下部「3×9=27 主物品栏 + 9 hotbar 行」
// （无装备槽 / 无人偶预览，区别于生存背包）。合成检测走 C++ RecipeRegistry（Game 层单一权威；本组件只读查
// match）。**MC 式数量**：点结果槽 → 取 outputCount 件到光标；输入槽每原料格 consume 1 份（count-1，归 0
// 清 id，不清空整槽 → 可连点合多批）。
//
// 物品移动（合成格 / 主栏 / hotbar 间任意搬动）：左键整组 / 右键半份 / 单放，与 SurvivalInventory.qml 的
// resolveClick / resolveRightClick 同算法（共享 hotbar VM 的 heldBlock / heldCount 光标手持栈）。
//
// 关包（visible→false）：把 craft3 内容 addStack 回 hotbar（合并同类，同拾取），清空 craft3
// （MC 行为：合成格不持久化，关包即退回玩家背包）。主栏 mainSlots 为面板本地存储，组件不销毁 → 跨开关持久。
//
// 全部槽框 / 箭头自绘原创（InvSlot 凹陷槽 + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点格子），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（提供 heldBlock/heldCount/maxStackSize/iconSourceForBlock/
    // nameForBlock/isTool/isMaterial/slotRevision/addStack 等栈操作 + 图标 / 名查询）。
    property Hotbar hotbar
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49 同 SurvivalInventory：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区）。
    signal discardHeldRequested()

    // ── 尺寸常量 ──
    readonly property int slotSize: 40
    readonly property int gridN: 3
    // t63：3×9 主物品栏 + 9 hotbar 行（MC 1.0 工作台布局：上部 3×3 合成 + 产物，下部 3×9 物品栏 + hotbar）。
    readonly property int mainCols: 9
    readonly property int mainRows: 3

    // 3×3 合成格本地栈存储（与 hotbar VM 共享同一光标手持栈 heldBlock/heldCount）。
    // 数组改写不触发 QML 绑定 → 配 craftRev 版本号让 Image source / count / 结果槽重算。
    property var craftSlots: [0,0,0, 0,0,0, 0,0,0]
    property var craftCounts:[0,0,0, 0,0,0, 0,0,0]
    property int craftRev: 0

    // t97：27 主物品栏自本任务起上移至 hotbar VM（m_mainSlots），与 SurvivalInventory / FurnaceUI 三菜单共享
    // 同一份 → 三菜单主栏同步、returnHeldToHotbar/pickupScan 经 addToAny 能合并进主栏（修「主栏不同步 /
    // 丢弃回栏不合并」根因）。原本地 mainSlots/mainCounts/mainRev 数组删除，delegate 改读 VM（触碰
    // mainRevision + mainBlockIdAt/mainCountAt，同 SurvivalInventory 主栏模式）。与合成格 / hotbar 共享同一
    // hotbar VM 光标手持栈；左键整组 / 右键半份同 resolveClick / resolveRightClick。

    // t167 左键拖动均分（spec：左键按住拖过 N 格 → 实时均分 floor(count/N)、余数留光标）。手势由 root 级
    // DragHandler(LeftButton) 总控：按下不动时 per-slot 左键 TapHandler 抓（单点拾取/放置/合并/互换），一旦
    // 拖动越阈值 → DragHandler 激活夺抓 → onActiveChanged 驱动 begin/endLeftDrag；逐槽 HoverHandler 在
    // leftDragActive 期间收集扫过格子（addDragSlot 即触发 redistributeLive 实时重分）。dragSlots 存「组:下标」
    // 字符串（去重简单）；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制
    // （每滑入新格先撤销上轮写入再重分）。合成格 / 主栏 / hotbar 统一支持（合成格仅参与收集，不分发）。
    // 均分算法与 t79/t98 右键拖拽同源（右键拖拽 t166d 改 per-slot 单点后停用）；t167 把同一算法接到左键。
    property bool leftDragActive: false
    property var dragSlots: []              // "craft:2" / "main:5" / "hotbar:0"
    property string hoveredKey: ""
    property int dragHeldId: 0
    property int dragHeldCount: 0
    // t98 实时重分撤销机制：dragOriginal 记每槽 drag 前原始栈（首次 encounter 快照）；dragWritten 记本轮
    // 已写槽。每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginLeftDrag / endLeftDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})
    // t98 双击合并：lastTapMs/lastTapKey 记上次左键点击（槽 key）的时间戳与 key；400ms 内同槽二次点击 →
    // doMergeSameId（扫 main+hotbar 同 id 累加成满栈 64 一组、余数留光标）。
    property real lastTapMs: 0
    property string lastTapKey: ""

    // resolveClick / resolveRightClick：与 SurvivalInventory.qml 完全一致（拾取/放置/合并/互换 4 case）。
    // 复制而非抽公共组件，保持两文件独立可读（§9 自绘原创，不引入新公共依赖）。
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

    // ── t79 右键拖拽均分辅助（与 SurvivalInventory.qml 同算法，独立维护保持两文件可读）──
    function slotKey(group, index) { return group + ":" + index }
    function dragHasKey(key) {
        for (let i = 0; i < root.dragSlots.length; ++i) if (root.dragSlots[i] === key) return true
        return false
    }
    function addDragSlot(key) {
        if (root.dragHasKey(key)) return
        // t108：异物槽不入 dragSlots（addDragSlot 前判）。仅在分发态（dragHeldId≠0）过滤——读槽当前栈，
        // 非空且 id≠dragHeldId 则跳过（与 redistributeLive 的 eligible 过滤一致；让绿框只亮真正会收物的
        // 空/同 id 槽）。dragHeldId=0（空手拖）不过滤，让起点槽入 dragSlots 供 endLeftDrag→singleLeftClick。
        if (root.dragHeldId !== 0) {
            const p0 = key.split(":")
            const cur = root.readSlot(p0[0], parseInt(p0[1], 10))
            if (cur.id !== 0 && cur.id !== root.dragHeldId) return
        }
        root.dragSlots = root.dragSlots.concat([key])
        root.redistributeLive()                          // t98：每滑入新格实时重算 N 等分（撤销 + 重分）
    }
    function readSlot(group, index) {
        if (group === "craft")  return { id: root.craftSlots[index] || 0, count: root.craftCounts[index] || 0 }
        if (group === "main")   return { id: root.hotbar.mainBlockIdAt(index), count: root.hotbar.mainCountAt(index) }
        if (group === "hotbar") return { id: root.hotbar.blockIdAt(index), count: root.hotbar.countAt(index) }
        return { id: 0, count: 0 }
    }
    function writeSlot(group, index, id, count) {
        if (group === "craft")       { root.craftSlots[index] = id; root.craftCounts[index] = count; root.craftRev++ }
        else if (group === "main")   { root.hotbar.mainSetStack(index, id, count) }
        else if (group === "hotbar") { root.hotbar.setStack(index, id, count) }
    }
    function beginLeftDrag() {
        root.dragHeldId = root.hotbar.heldBlock
        root.dragHeldCount = root.hotbar.heldCount
        root.dragSlots = []
        root.dragOriginal = ({})                        // t98：重置原始栈快照
        root.dragWritten = ({})                         // t98：重置已写槽记录
        root.leftDragActive = true
        if (root.hoveredKey !== "") root.addDragSlot(root.hoveredKey)
    }
    function endLeftDrag() {
        if (!root.leftDragActive) return
        root.leftDragActive = false
        const n = root.dragSlots.length
        // t167：N≥2 已在 drag 途中实时分完（redistributeLive 每滑入新格重算），此处不再均分。
        // N===1 退化为单格左键（拾取/放置/合并/互换）—— 微拖（越阈值但未离开起点格）的常规左键语义。
        if (n === 1) {
            const p = root.dragSlots[0].split(":")
            root.singleLeftClick(p[0], parseInt(p[1], 10))
        }
        root.dragSlots = []
        root.dragOriginal = ({})
        root.dragWritten = ({})
    }
    // t98 实时均分（替代 t90 松手一次性 applyDragDistribute）：每滑入新格（addDragSlot 触发）即重算
    // floor(dragHeldCount/N) 入格、余数实时回光标（heldBlock/heldCount → Main.qml 浮动图标实时变）。撤销
    // 机制：dragOriginal 快照每槽 drag 前原始栈（首次 encounter 拍），dragWritten 记本轮已写槽；下次重分前
    // 先据 dragOriginal 把已写格恢复，再按新 N 重分 → 用户看到「滑第 2 格变对半、第 3 格变三等分」的实时
    // 反馈。合格过滤：空槽 / 同 id 未满；craft 合成格排除（避免改写合成输入、干扰 recipeMatch）；异物 / 已
    // 满槽跳过。守恒：写回总量 + 余数 = dragHeldCount 快照。N≤1 不分（保留单格左键给 endLeftDrag 处理）。
    // 与 Inventory / SurvivalInventory 同算法。
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
            if (p[0] === "craft") continue                              // 合成格排除（避免影响 recipeMatch）
            if (!root.dragOriginal[key]) {
                const cur = root.readSlot(p[0], parseInt(p[1], 10))
                root.dragOriginal[key] = { id: cur.id, count: cur.count }
            }
            const orig = root.dragOriginal[key]
            if (orig.id === 0 || (orig.id === heldId && orig.count < cap))
                eligible.push({ group: p[0], index: parseInt(p[1], 10), key: key, base: orig.count })
        }

        // t108：n>total 截断 eligible 到 total 项（每格至少 1 件；N≤count）。如 8 件拖 9 格 → 第 9 格不分，
        // 避免被「扫过即亮绿框」错觉（与 redistributeLive 的「异物/已满跳过」一致）。截断在 n<=1 早退之前。
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
    //   TapHandler.onTapped；仅当左键越阈值但只扫过起点一格时经此路径补一次单击语义。
    function singleLeftClick(group, index) {
        const cur = root.readSlot(group, index)
        const r = root.resolveClick(cur.id, cur.count)
        if (!r) return
        root.writeSlot(group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }

    // t110 Shift+左键搬运（MC 1.0 背包）：main 槽→首个空 hotbar 槽；hotbar 槽→首个空 main 槽；其它组（craft）
    //   无操作（spec 仅定义 main↔hotbar）。与 SurvivalInventory / Inventory / FurnaceUI 同算法。
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

    // t110 数字键交换：当前 hover 槽 ↔ hotbar[idx] 整栈互换（与 SurvivalInventory 同算法）。
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

    // t98 双击合并（MC：双击某槽 → 扫 main + hotbar 同 id 物品，累加成满栈 64 一组，余数留光标）。targetId
    // 取光标手持 id（典型流程：首次左键拾起该槽 → 二次点击同槽合并），fallback 到所点槽 id（首次为放置时光
    // 标空）。依赖 t97 main VM 共享（main + hotbar 同一份）。守恒：合并后 (各槽 + 光标) 总量 = 合并前。
    function doMergeSameId(group, index) {
        if (!root.hotbar) return
        let targetId = root.hotbar.heldBlock
        if (targetId === 0) {
            const cur = root.readSlot(group, index)
            targetId = cur.id
        }
        if (targetId === 0) return
        const cap = root.hotbar.maxStackSize(targetId)

        // 收集所有同 id 槽位 + 光标，求总量。
        const slots = []
        let total = 0
        if (root.hotbar.heldBlock === targetId) total += root.hotbar.heldCount
        for (let i = 0; i < root.hotbar.mainCount; ++i) {
            if (root.hotbar.mainBlockIdAt(i) === targetId) {
                total += root.hotbar.mainCountAt(i)
                slots.push({ group: "main", index: i })
            }
        }
        for (let i = 0; i < root.hotbar.slotCount; ++i) {
            if (root.hotbar.blockIdAt(i) === targetId) {
                total += root.hotbar.countAt(i)
                slots.push({ group: "hotbar", index: i })
            }
        }
        if (total <= 0) return

        // 清空所有同 id 槽 + 光标（即将重新打包）。
        for (let i = 0; i < slots.length; ++i) {
            root.writeSlot(slots[i].group, slots[i].index, 0, 0)
        }
        root.hotbar.heldBlock = 0
        root.hotbar.heldCount = 0

        // 重打包：满栈（cap）按扫描顺序填回前 numFull 个槽，余数（< cap）留光标。槽位充足（total 来自这些
        // 槽 + 光标，cap*(槽数+1) ≥ total），不会丢物品。
        const numFull = Math.min(Math.floor(total / cap), slots.length)
        for (let i = 0; i < numFull; ++i) {
            root.writeSlot(slots[i].group, slots[i].index, targetId, cap)
        }
        const cursorCount = total - numFull * cap
        if (cursorCount > 0) {
            root.hotbar.heldBlock = targetId
            root.hotbar.heldCount = cursorCount
        }
    }

    // 取当前合成格的 id 数组（触碰 craftRev 让 QML 绑定刷新时重算）。
    function craftIdArray() {
        root.craftRev
        return root.craftSlots
    }

    // 查匹配配方（走 C++ RecipeRegistry::match；3×3 输入）。返回 recipe 或 null。
    // 经 hotbar.recipeMatch 透传（Hotbar VM 桥接 C++ 静态类给 QML）。
    function matchedRecipe() {
        if (!root.hotbar) return null
        root.craftRev
        return root.hotbar.recipeMatch(root.craftSlots, 3)
    }

    // 关包归还合成栏（spec 同 SurvivalInventory）：visible→false 时把 craft3 内容 addStack 回 hotbar。
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

    onVisibleChanged: if (!visible) returnCraftToHotbar()

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

    // 半透明遮罩：仅吸收点击（防穿透），不关闭面板（E / Esc / closed 信号才关）。
    // 手持物时点遮罩区 → 丢弃为实体（同 SurvivalInventory）。
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

    // 面板：深色圆角，居中。t63 宽度容纳 3×9 主栏（9×40=360 + 2×16 边距 = 392）；高度 = 标题 + 3×3 合成 +
    // 3×9 主栏 + 9 hotbar 行 + 间距/边距（t77 删去提示行，高度随减 28）。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 32 = 392
        height: 372                                  // 标题(22) + 合成(120) + 主栏(120) + hotbar(40) + 间距/边距
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
                    text: "工作台"
                    color: "#eaf2ea"; font.pixelSize: 20; font.bold: true
                    anchors.left: parent.left
                }
                Text {
                    text: "[E] / [Esc] 关闭"
                    color: "#7fae7f"; font.pixelSize: 11
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 合成区：左 3×3 格 + 箭头 + 右结果槽。t77：合成行整体水平居中——本 Item 是 Column 子项，
            // 不能用 anchors（Qt Column positioner 禁止子项 anchors，见 Qt 文档），故算出左偏移 rowOffsetX
            // 加到 3 个绝对定位子元素的 x 上：3×3(120) + 间隙(28) + 结果槽(40) = 188 宽，在 360 行宽内居中，
            // 消除原左对齐（x=0..188）导致面板右侧大段空白的问题。
            Item {
                id: craftRow
                width: parent.width
                height: root.gridN * root.slotSize
                // 合成行整体宽（gridN*slotSize + 28 + slotSize = 188）居中所需的左偏移。
                readonly property real rowOffsetX: (width - (root.gridN * root.slotSize + 28 + root.slotSize)) / 2

                // 3×3 合成格。
                Grid {
                    id: craftGrid
                    x: craftRow.rowOffsetX; y: 0
                    columns: root.gridN; spacing: 0
                    Repeater {
                        model: root.gridN * root.gridN  // 9
                        delegate: Item {
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                            // 物品图标：方块段→等距立方体 Image；工具段→ToolIcon；材料段（木棒）→MaterialIcon 自绘。
                            Item {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: { root.craftRev; return (root.craftSlots[index] || 0) !== 0 }
                                Image {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return !root.hotbar.isTool(root.craftSlots[index] || 0)
                                                              && !root.hotbar.isMaterial(root.craftSlots[index] || 0) }
                                    source: { root.craftRev; return root.hotbar.iconSourceForBlock(root.craftSlots[index] || 0) }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return root.hotbar.isTool(root.craftSlots[index] || 0) }
                                    tier: { root.craftRev; return root.hotbar.toolTier(root.craftSlots[index] || 0) }
                                }
                                // 材料段（木棒）：MaterialIcon 自绘（§9a 原创，非 MC 资产）。
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return root.hotbar.isMaterial(root.craftSlots[index] || 0) }
                                    materialId: { root.craftRev; return root.craftSlots[index] || 0 }
                                }
                            }
                            // 栈数量（count>1 显数字）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { root.craftRev; return (root.craftCounts[index] || 0) > 1 }
                                text: { root.craftRev; return root.craftCounts[index] || 0 }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // 左键整组（resolveClick）；右键走 per-slot 右键 TapHandler（resolveRightClick）。
                            // 左键拖动均分由 root DragHandler + 逐槽 HoverHandler 收集（t167）；合成格仅收集不分发。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    // t110：Shift+左键搬运（craft 槽不在 main↔hotbar 范畴，slotShiftLeft 对 craft
                                    //   组无操作；普通左键走 resolveClick）。
                                    if (window.shiftHeld) { root.slotShiftLeft("craft", index); return }
                                    const r = root.resolveClick(root.craftSlots[index] || 0, root.craftCounts[index] || 0)
                                    if (!r) return
                                    root.craftSlots[index] = r.slotId
                                    root.craftCounts[index] = r.slotCount
                                    root.craftRev++
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                }
                            }
                            // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    const r = root.resolveRightClick(root.craftSlots[index] || 0, root.craftCounts[index] || 0)
                                    if (!r) return
                                    root.craftSlots[index] = r.slotId
                                    root.craftCounts[index] = r.slotCount
                                    root.craftRev++
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                }
                            }
                            HoverHandler {
                                // t99：跟踪槽显示 id。槽被丢弃/拾取/互换后变空时 hover 仍 true → onHoveredChanged
                                // 不重发 → tooltip 残留旧名。变空时主动清 hoveredItemId（spec 修法 a）。
                                property int trackedId: { root.craftRev; return root.craftSlots[index] || 0 }
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
                                    // t167：左键拖动期间进入新格 → 收集（集合只增不减；无 leave-remove 分支）。
                                    if (hovered && root.leftDragActive) {
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
                                    root.dragSlots; root.leftDragActive; root.craftRev
                                    const sid = root.craftSlots[index] || 0
                                    return root.leftDragActive
                                        && root.dragHasKey(root.slotKey("craft", index))
                                        && (sid === 0 || sid === root.dragHeldId)
                                }
                                z: 10
                            }
                        }
                    }
                }

                // 合成箭头（指向结果槽）：自绘像素图（§9a）。
                Canvas {
                    x: craftRow.rowOffsetX + root.gridN * root.slotSize + 2; y: root.slotSize + 10
                    width: 24; height: 20
                    onPaint: {
                        const ctx = getContext("2d"); ctx.reset()
                        ctx.imageSmoothingEnabled = false
                        ctx.fillStyle = "#8a8a8a"
                        ctx.fillRect(0, 8, 16, 4)
                        ctx.beginPath()
                        ctx.moveTo(16, 2); ctx.lineTo(24, 10); ctx.lineTo(16, 18); ctx.closePath()
                        ctx.fill()
                    }
                }

                // 结果槽（最右）：显匹配配方产物图标；点击 → 合成一批（消耗每原料 1、产出 outputCount 到光标）。
                Item {
                    x: craftRow.rowOffsetX + root.gridN * root.slotSize + 28; y: root.slotSize
                    width: root.slotSize; height: root.slotSize
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }

                    // 产物图标（触碰 craftRev 重算 matchedRecipe）。
                    Item {
                        anchors.centerIn: parent
                        width: 30; height: 30
                        // 产物 id / 数量（0 = 无匹配 → 隐藏）。matchedRecipe 返回 null 或 QVariantMap；
                        // 用 (r && r.field) || 0 防御 undefined（无匹配 / hotbar 未注入时回 0，免 QML 警告）。
                        property int outId: { root.craftRev; const r = root.matchedRecipe(); return (r && r.outputId) || 0 }
                        property int outCount: { root.craftRev; const r = root.matchedRecipe(); return (r && r.outputCount) || 0 }
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
                        }
                        MaterialIcon {
                            anchors.fill: parent
                            visible: parent.outId !== 0 && root.hotbar.isMaterial(parent.outId)
                            materialId: parent.outId
                        }
                        // 产物数量（>1 显数字）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: parent.outCount > 1
                            text: parent.outCount
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                    }

                    // 点击结果槽 → 合成（MC 式：消耗每原料 1、产出 outputCount 到光标；剩余留槽可连点）。
                    // 前置：matchedRecipe 非 null；光标能容纳产物（空 / 同 id 且累加不超 maxStack）。
                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onTapped: {
                            if (!root.hotbar) return
                            root.craftRev  // 触碰最新态
                            const r = root.matchedRecipe()
                            if (!r) return
                            const heldId = root.hotbar.heldBlock
                            const heldCount = root.hotbar.heldCount
                            const cap = root.hotbar.maxStackSize(r.outputId)
                            if (!root.hotbar.recipeCanTake(r.outputId, r.outputCount, heldId, heldCount, cap)) return
                            // 消耗：每个非空原料格 count-1（归 0 清 id）。原料用量恒 1（consumeCount=1）。
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
                        }
                    }
                }
            }

            // t63 / t97 3×9 主物品栏（27 槽）：读 hotbar VM（m_mainSlots，三菜单共享）；左键整组 / 右键半份
            // 取放（与 SurvivalInventory 主栏同模式）。主栏栈写经 hotbar.mainSetStack；与合成格 / hotbar 共享
            // 同一 hotbar VM 光标手持栈。物品可在 主栏 ↔ 合成格 ↔ hotbar 间任意搬动。
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
                            anchors.centerIn: parent
                            width: 30; height: 30
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
                        // 栈数量（count>1 显数字）。触碰 mainRevision 刷新（VM NOTIFY 驱动）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { root.hotbar.mainRevision; return mainCount > 1 }
                            text: { root.hotbar.mainRevision; return mainCount }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
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
                                const r = root.resolveClick(mainId, mainCount)
                                if (!r) return
                                root.hotbar.mainSetStack(index, r.slotId, r.slotCount)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
                        }
                        // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                const r = root.resolveRightClick(mainId, mainCount)
                                if (!r) return
                                root.hotbar.mainSetStack(index, r.slotId, r.slotCount)
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
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
                                if (hovered && root.leftDragActive) {
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
                                root.dragSlots; root.leftDragActive; root.hotbar.mainRevision
                                return root.leftDragActive
                                    && root.dragHasKey(root.slotKey("main", index))
                                    && (mainId === 0 || mainId === root.dragHeldId)
                            }
                            z: 10
                        }
                    }
                }
            }

            // t63 底部 9 槽 hotbar 行（同步游戏内 hotbar）：model 用固定整数 slotCount + delegate 持 slotId
            // 属性触碰 slotRevision（t55/t63 已验证写法，**不**用 slotList() 等长数组以免 Repeater 不重建）。
            // 左键整组 / 右键半份同主栏（hotbar 槽写经 hotbar.setStack；VM 单一权威）。不切真实选中（同 SurvivalInventory）。
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
                                anchors.centerIn: parent
                                width: 30; height: 30
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
                            // t166d per-slot 右键（拿半/放一），不依赖 hover/hoveredKey。
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
                                    if (hovered && root.leftDragActive) {
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
