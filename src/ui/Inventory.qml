import QtQuick
// t127：创造调色板 ScrollBar（拖动指示）来自 QtQuick.Controls。纯 QML 模块不经 C++ 链接（同 t14
//   Particles3D）——CMakeLists 的 windeployqt POST_BUILD 已用 `--qmldir src/ui` 扫 import 语句自动部署
//   匹配的 QML 模块插件，故无 t94 tooltip 注释担心的「未部署→整文档加载失败」之忧（见 lessons-learned）。
import QtQuick.Controls
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

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

    // t79/t98 右键拖拽实时均分（spec t98：右键按下拖过 N 格 → 每滑入新格实时重算 floor(count/N) 等分、
    // 余数实时回光标浮动图标；松手不再二次均分）。创造背包仅 hotbar 行作分发目标（调色板=无限源，不作
    // 目标）。手势由 root 级 TapHandler(WithinBounds, RightButton) 总控；逐槽 HoverHandler 收集（addDragSlot
    // 即触发 redistributeLive）；release 据 N 决定（N≥2 已实时分完 / N==1 退化为单格右键）。dragSlots 存
    // 「组:下标」字符串；dragHeld* 为按下瞬间光标栈快照；dragOriginal/dragWritten 支撑实时重分的撤销机制。
    property bool rightDragActive: false
    property var dragSlots: []              // "hotbar:0" ..
    property string hoveredKey: ""
    property int dragHeldId: 0
    property int dragHeldCount: 0
    // t98 实时重分撤销机制：dragOriginal 记每槽 drag 前原始栈（首次 encounter 快照）；dragWritten 记本轮
    // 已写槽。每滑入新格 → 先据 dragOriginal 撤销 dragWritten、再按新 N 重分。beginRightDrag / endRightDrag 重置。
    property var dragOriginal: ({})
    property var dragWritten: ({})
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

    // t46：背包内 hotbar 行左键交互与主栏统一（用户反馈「现在背包内 hotbar 行不能左键交互」——
    // 旧版 hotbar 行用「创造覆盖」语义：持物点异 id 槽 → 原物被丢弃，等同于「不能正常移动/互换」）。
    // 本函数与 SurvivalInventory.qml 的 resolveClick 完全一致（拾取/放置/合并/互换 4 case），让创造
    // hotbar 行支持把物品在槽间搬动/互换，而不是覆盖销毁。调色板点击仍是「无限源拾取」（不变）。
    //   A 手持空 + 槽非空：拾取整栈（槽清空、held ← 该栈）。
    //   B 手持非空 + 槽空：放置整栈（槽 ← held、held 清空）。
    //   C 手持非空 + 同 id：合并至 maxStackSize(id)（方块 64 / 工具段 1），余数留 held；槽已满则无操作。
    //   D 手持非空 + 异 id：互换（槽 ↔ held）。
    // 返回 null = 无操作（空点空 / 同 id 槽已满）。手持栈状态由 hotbar VM 单一持有（PLAN §2：VM 单一权威）。
    function resolveClick(curId, curCount) {
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        if (heldId === 0) {
            if (curId === 0) return null                                       // 空手点空槽：无操作
            return { slotId: 0, slotCount: 0, heldId: curId, heldCount: curCount } // A 拾取整栈
        }
        if (curId === 0) {
            return { slotId: heldId, slotCount: heldCount, heldId: 0, heldCount: 0 } // B 放整栈
        }
        if (curId === heldId) {
            // C 合并：min(剩余空间, 手持数) 移入槽；手持余 0 → heldId 归 0（保持空栈不变式）。
            const cap = root.hotbar.maxStackSize(curId)
            const space = cap - curCount
            if (space <= 0) return null                                        // 槽已满（含工具段 cap=1）：无操作
            const move = Math.min(space, heldCount)
            const remain = heldCount - move
            return {
                slotId: curId, slotCount: curCount + move,
                heldId: remain > 0 ? heldId : 0, heldCount: remain
            }
        }
        return { slotId: heldId, slotCount: heldCount, heldId: curId, heldCount: curCount } // D 互换
    }

    // t49 右键语义（MC 1.0）：空手→拾取一半（floor(count/2)，单件特例取 1，否则 floor(1/2)=0 为无操作）；
    // 持物→放 1 个（空槽开新栈 / 同 id 未满 +1；异 id 槽 / 已满无操作，**不互换**）。返回与 resolveClick
    // 同形的 {slotId, slotCount, heldId, heldCount}；null = 无操作。与 SurvivalInventory.qml 的同名函数一致。
    function resolveRightClick(curId, curCount) {
        const heldId = root.hotbar.heldBlock
        const heldCount = root.hotbar.heldCount
        if (heldId === 0) {
            if (curId === 0) return null                                     // 空手点空槽：无操作
            let half = Math.floor(curCount / 2)
            if (half < 1) half = 1                                           // 单件：整件拿起
            return {
                slotId: curCount - half > 0 ? curId : 0, slotCount: curCount - half,
                heldId: curId, heldCount: half
            }
        }
        if (curId !== 0 && curId !== heldId) return null                     // 异 id 槽：无操作（不互换）
        const cap = root.hotbar.maxStackSize(heldId)
        if (curId === heldId && curCount >= cap) return null                 // 同 id 已满：无操作
        const remain = heldCount - 1
        return {
            slotId: heldId, slotCount: curCount + 1,
            heldId: remain > 0 ? heldId : 0, heldCount: remain
        }
    }

    // ── t79 右键拖拽均分辅助（创造背包仅 hotbar 行参与；调色板=无限源不作目标）──
    function slotKey(group, index) { return group + ":" + index }
    function dragHasKey(key) {
        for (let i = 0; i < root.dragSlots.length; ++i) if (root.dragSlots[i] === key) return true
        return false
    }
    function addDragSlot(key) {
        if (root.dragHasKey(key)) return
        // t108：异物槽不入 dragSlots（addDragSlot 前判）。仅在分发态（dragHeldId≠0）过滤——读槽当前栈，
        // 非空且 id≠dragHeldId 则跳过（与 redistributeLive 的 eligible 过滤一致；让绿框只亮真正会收物的
        // 空/同 id 槽）。dragHeldId=0（拿半手势）不过滤，让起点槽入 dragSlots 供 endRightDrag→singleRightClick。
        if (root.dragHeldId !== 0) {
            const p0 = key.split(":")
            const cur = root.readSlot(p0[0], parseInt(p0[1], 10))
            if (cur.id !== 0 && cur.id !== root.dragHeldId) return
        }
        root.dragSlots = root.dragSlots.concat([key])
        root.redistributeLive()                          // t98：每滑入新格实时重算 N 等分（撤销 + 重分）
    }
    // t98：补 main 分支（与 SurvivalInventory / CraftingTableUI 对齐），让 doMergeSameId 扫 main 同 id 时能
    // 正确读 / 写（创造一般 main 全空，无副作用；防 main 残留时清不掉致复制）。
    function readSlot(group, index) {
        if (group === "main")   return { id: root.hotbar.mainBlockIdAt(index), count: root.hotbar.mainCountAt(index) }
        if (group === "hotbar") return { id: root.hotbar.blockIdAt(index), count: root.hotbar.countAt(index) }
        return { id: 0, count: 0 }
    }
    function writeSlot(group, index, id, count) {
        if (group === "main")   root.hotbar.mainSetStack(index, id, count)
        else if (group === "hotbar") root.hotbar.setStack(index, id, count)
    }
    function beginRightDrag() {
        root.dragHeldId = root.hotbar.heldBlock
        root.dragHeldCount = root.hotbar.heldCount
        root.dragSlots = []
        root.dragOriginal = ({})                        // t98：重置原始栈快照
        root.dragWritten = ({})                         // t98：重置已写槽记录
        root.rightDragActive = true
        if (root.hoveredKey !== "") root.addDragSlot(root.hoveredKey)
    }
    function endRightDrag() {
        if (!root.rightDragActive) return
        root.rightDragActive = false
        const n = root.dragSlots.length
        // t98：N≥2 已在 drag 途中实时分完（redistributeLive 每滑入新格重算），此处不再均分。
        // N===1 退化为单格右键（拿半 / 放一）—— 按下未拖动的常规右键语义。N===0 无操作。
        if (n === 1) {
            const p = root.dragSlots[0].split(":")
            root.singleRightClick(p[0], parseInt(p[1], 10))
        }
        root.dragSlots = []
        root.dragOriginal = ({})
        root.dragWritten = ({})
    }
    // t98 实时均分（替代 t90 松手一次性 applyDragDistribute）：每滑入新格（addDragSlot 触发）即重算
    // floor(dragHeldCount/N) 入格、余数实时回光标（heldBlock/heldCount → Main.qml 浮动图标实时变）。撤销
    // 机制：dragOriginal 快照每槽 drag 前原始栈（首次 encounter 拍），dragWritten 记本轮已写槽；下次重分前
    // 先据 dragOriginal 把已写格恢复，再按新 N 重分 → 用户看到「滑第 2 格变对半、第 3 格变三等分」的实时
    // 反馈。合格过滤：空槽 / 同 id 未满；craft 合成格排除（本文件无，防御）；异物 / 已满槽跳过。守恒：写回
    // 总量 + 余数 = dragHeldCount 快照。N≤1 不分（保留单格右键给 endRightDrag 处理）。与 SurvivalInventory /
    // CraftingTableUI 同算法。
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
            if (p[0] === "craft") continue                              // 合成格排除（本文件无，防御）
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
        // N≤1 / 空手 / 无物：不分（保留单格右键给 endRightDrag；空手 drag 无意义）。余数 = 原始快照。
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
    function singleRightClick(group, index) {
        const cur = root.readSlot(group, index)
        const r = root.resolveRightClick(cur.id, cur.count)
        if (!r) return
        root.writeSlot(group, index, r.slotId, r.slotCount)
        root.hotbar.heldBlock = r.heldId
        root.hotbar.heldCount = r.heldCount
    }

    // t110 Shift+左键搬运（MC 1.0 背包）：hotbar 槽→首个空 main 槽（创造面板无 main 行，但 hotbar VM 持 27
    //   main 槽 → 搬运目标存在；与 SurvivalInventory / CraftingTableUI / FurnaceUI 同算法）。「空位」= id==0 的槽。
    //   无空位 → 无操作（shift 显式搬运语义）。本面板无 main / craft 槽，仅 hotbar 分支会被触发。
    function slotShiftLeft(group, index) {
        if (group === "hotbar") {
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

    // t110 数字键交换：当前 hover 槽 ↔ hotbar[idx] 整栈互换（与 SurvivalInventory 同算法；本面板 hoveredKey
    //   仅 "hotbar:N"，故等效于 hotbar 内重排）。读 / 写经 readSlot/writeSlot 路由。
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
    // 标空）。依赖 t97 main VM 共享（main + hotbar 同一份；创造一般 main 全空，等效只扫 hotbar）。守恒：合并
    // 后 (各槽 + 光标) 总量 = 合并前。与 SurvivalInventory / CraftingTableUI 同算法。
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

    // t79 右键拖拽均分总控（WithinBounds 让 pressed 跨格保持 true；右键单格 / 多格均由此驱动）。
    // 调色板左键拾取不受影响；遮罩 MouseArea 仅接 LeftButton，右键透传到本 TapHandler。
    TapHandler {
        acceptedButtons: Qt.RightButton
        gesturePolicy: TapHandler.WithinBounds
        onPressedChanged: {
            if (pressed) root.beginRightDrag()
            else root.endRightDrag()
        }
    }

    // 半透明遮罩：仅吸收点击（防穿透到背后的游戏/暂停层），**不关闭背包**——用户要求背包只能由
    // E / Esc 关闭（点背包 UI 外部不应关闭）。t49：手持物时点遮罩区（面板外）→ 整栈丢弃为实体（同 Q 丢弃）。
    // t158：acceptedButtons 限左键。原默认（全键）的 MouseArea 在面板之上、比 root 右键 TapHandler 更早
    //   抓 right press（面板/槽/销毁槽 TapHandler 均只接 LeftButton 不拦右键）→ root 右键 TapHandler 永不触发 →
    //   右键分半/放单（resolveRightClick）失效，持物右键反被这里当「点遮罩」丢弃。限左键后右键透到 root
    //   TapHandler（beginRightDrag/singleRightClick 生效）；左键丢弃语义不变（MC 右键外部本就不丢弃）。
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
                                    root.hotbar.heldCount = root.hotbar.isTool(modelData) ? 1 : 64
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
                                        if (hovered && root.rightDragActive) {
                                            root.addDragSlot(key)
                                        } else if (!hovered && root.rightDragActive && root.dragHasKey(key)) {
                                            // t108 回滑减格：离开已选格 → 从 dragSlots 移除并重算（撤销机制
                                            // 据快照恢复该槽原始态，再按新 N 重分 → 用户见「滑第 2 格变对半、滑回
                                            // 变回单格」的实时反馈，与滑入方向对称）。
                                            root.dragSlots = root.dragSlots.filter(k => k !== key)
                                            root.redistributeLive()
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
                                        const isDouble = (now - root.lastTapMs < 400) && (root.lastTapKey === key)
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
                                // t79：右键改由 root TapHandler 统一处理（单格右键 + 拖拽均分）；此处不再有右键 TapHandler。
                                // 均分拖拽高亮（扫过且待分发的合格格绿框；rightDragActive 期间才显）。
                                // t108：绿框加 (槽空||槽==heldId) 条件——异物槽纵使被扫过也不亮（addDragSlot 已过滤
                                // 入 dragSlots，此处显式条件双重保险：heldId/槽态在 drag 途中变化时仍准确）。
                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.color: "#7fe57f"; border.width: 2
                                    visible: {
                                        root.dragSlots; root.rightDragActive; root.hotbar.slotRevision
                                        return root.rightDragActive
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
