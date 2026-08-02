// InventoryOps.js — 背包/物品栏槽操作共享 JS 库（t168 抽取）
//
// 背景：t38/t49/t79/t98/t108/t110/t166/t167 一路演进下来，「左键整组 / 右键半份 / Shift 搬运 /
// 数字键交换 / 双击合并 / 左键拖动均分」这套栈操作算法在四张面板 QML 里被逐字复制了四份：
//   src/ui/Inventory.qml（创造背包）、SurvivalInventory.qml（生存背包）、CraftingTableUI.qml（工作台）、
//   FurnaceUI.qml（熔炉）。任一 bug 修复都要同步改四份，极易漏改致行为分叉。本库把这「同一份算法」
// 收敛为单一权威实现，面板只保留**面板专属**部分（本地槽路由 craft / in / fuel / out + 面板专属调度）。
//
// 设计契约（所有函数第一参数恒为 `root` —— 调用方所在 QML Item）：
//   root.hotbar              —— Hotbar 视图模型（单一权威：heldBlock/heldCount/maxStackSize/main*/setStack）。
//   root.dragSlots / dragOriginal / dragWritten / dragHeldId / dragHeldCount / leftDragActive / hoveredKey
//                            —— 拖动均分的可变态（仍由各面板持为 QProperty，本库经 root 读写；不另存副本，
//                               保证 QML 绑定（如绿框 visible 依赖 dragSlots）照常刷新）。
//   root.localReadSlot(group,index) / root.localWriteSlot(group,index,id,count)
//                            —— 面板专属槽组（craft / in / fuel / out）的读写钩子；main/hotbar 由本库统一
//                               走 root.hotbar（四面板一致），故本地钩子只处理非 main/hotbar 组。无本地组
//                               的面板（如 Inventory 创造背包）可不定义这两个钩子 —— 本库以真值检测兜底
//                               （readSlot 返回空栈、writeSlot 空操作），不会因缺钩子崩。
//
// 分层（PLAN §2）：本库属 UI/ViewModel 呈现层的纯算法工具，不依赖 World/Renderer；所有数据经注入的
//   root.hotbar VM 读写，VM 是单一权威（§2「ViewModel 单一权威」）。零 MC 专有名词（§9）。
//
// 自测：编译零警告；启动后四张背包面板的左/右键拾取放置、Shift 搬运、数字键交换、双击合并、左键拖动
//   均分行为与抽取前一致（人工肉眼，逐面板复测）。
.pragma library

// ── 左键整组栈操作（t38）：给定目标槽当前 (curId, curCount) 与光标手持栈 (heldBlock, heldCount)，
//    返回应写入的 {slotId, slotCount, heldId, heldCount}；null = 无操作。4 种 case：
//      A 手持空 + 槽非空：拾取整栈。
//      B 手持非空 + 槽空：放置整栈。
//      C 手持非空 + 同 id：合并至 maxStackSize(id)（方块 64 / 工具段 1），余数留 held；槽满则无操作。
//      D 手持非空 + 异 id：互换。
//    纯函数（只读 root.hotbar），无副作用 —— 调用方据返回值写入对应槽 + 更新 held。
function resolveClick(root, curId, curCount) {
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

// ── 右键语义（t49，MC 1.0）：空手→拾取一半（floor(count/2)，单件特例取 1）；持物→放 1 个
//    （空槽开新栈 / 同 id 未满 +1；异 id 槽 / 已满无操作，**不互换**）。返回与 resolveClick 同形；null = 无操作。
function resolveRightClick(root, curId, curCount) {
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

// ── 槽读写统一路由（t168 抽取）：main/hotbar 走 hotbar VM（四面板一致 → 收敛于此）；其它组
//    （craft / in / fuel / out）委托面板的 localReadSlot/localWriteSlot 钩子。无钩子面板（Inventory）
//    以真值检测兜底（readSlot 空栈 / writeSlot 空操作）。index 对 main/hotbar 有效，对本地单槽组忽略。
function readSlot(root, group, index) {
    if (group === "main")   return { id: root.hotbar.mainBlockIdAt(index), count: root.hotbar.mainCountAt(index) }
    if (group === "hotbar") return { id: root.hotbar.blockIdAt(index), count: root.hotbar.countAt(index) }
    if (root.localReadSlot) return root.localReadSlot(group, index)
    return { id: 0, count: 0 }
}
function writeSlot(root, group, index, id, count) {
    if (group === "main")        { root.hotbar.mainSetStack(index, id, count); return }
    if (group === "hotbar")      { root.hotbar.setStack(index, id, count); return }
    if (root.localWriteSlot)     root.localWriteSlot(group, index, id, count)
}

// ── 拖动均分辅助（t79/t98/t108/t167）──
// 槽 key（"组:下标"）。字符串而非对象：去重 / 比较 / 拆分都直接，无需手写对象相等。
function slotKey(group, index) { return group + ":" + index }
function dragHasKey(root, key) {
    for (let i = 0; i < root.dragSlots.length; ++i) if (root.dragSlots[i] === key) return true
    return false
}
// t180：判定某组是否参与「左键拖动均分 + 双击拿同类」。main/hotbar 恒参与（五面板一致）；本地组
//   （craft / in / fuel / out / chest）仅当面板在 root.localDragGroups 数组中声明时参与——未声明则不参与
//   （生存背包 2×2 craft、熔炉 out 槽保持不参与；主栏/hotbar 不受影响）。声明见各面板：
//   CraftingTableUI=["craft"]、FurnaceUI=["in","fuel"]、ChestUI=["chest"]。把「参与与否」从旧 hardcode
//   （redistributeLive 写死排除 "craft"、doMergeSameId 写死只扫 main/hotbar）泛化为按面板声明，契合 t168
//   「一处改处处生效」：新增面板/组只要设 localDragGroups 即接入全套快捷操作。
function groupIsDraggable(root, group) {
    if (group === "main" || group === "hotbar") return true
    const g = root.localDragGroups
    return Array.isArray(g) && g.indexOf(group) >= 0
}
function addDragSlot(root, key) {
    // t181：右键拖动走独立路径（每格放 1 个，无重分配）；左键拖动走原 redistributeLive 均分路径。
    //   HoverHandler 的收集条件改为 dragActive（leftDragActive || rightDragActive），此处据 rightDragActive
    //   分发到 addRightDragSlot，避免左/右拖动状态机互相干扰（两套 dragSlots 独立）。
    if (root.rightDragActive) { addRightDragSlot(root, key); return }
    const p0 = key.split(":")
    // t180：非可拖拽组不入 dragSlots——既免无谓重算，也防误导性绿框高亮（高亮 visible 绑 dragHasKey，
    //   不收集即不亮；旧版扫过熔炉 out 槽会亮绿框却永不分发，现为静默跳过）。
    if (!groupIsDraggable(root, p0[0])) return
    if (dragHasKey(root, key)) return
    // t108：异物槽不入 dragSlots（addDragSlot 前判）。仅在分发态（dragHeldId≠0）过滤——读槽当前栈，
    // 非空且 id≠dragHeldId 则跳过（与 redistributeLive 的 eligible 过滤一致；让绿框只亮真正会收物的
    // 空/同 id 槽）。dragHeldId=0（空手拖）不过滤，让起点槽入 dragSlots 供 endLeftDrag→singleLeftClick。
    if (root.dragHeldId !== 0) {
        const cur = readSlot(root, p0[0], parseInt(p0[1], 10))
        if (cur.id !== 0 && cur.id !== root.dragHeldId) return
    }
    root.dragSlots = root.dragSlots.concat([key])   // 新数组引用 → 依赖 dragSlots 的绑定刷新
    redistributeLive(root)                          // t98：每滑入新格实时重算 N 等分（撤销 + 重分）
}
// 手势生命周期（root DragHandler.onActiveChanged 调）。
function beginLeftDrag(root) {
    root.dragHeldId = root.hotbar.heldBlock
    root.dragHeldCount = root.hotbar.heldCount
    root.dragSlots = []
    root.dragOriginal = ({})                        // t98：重置原始栈快照
    root.dragWritten = ({})                         // t98：重置已写槽记录
    root.leftDragActive = true
    if (root.hoveredKey !== "") addDragSlot(root, root.hoveredKey)   // 起点槽（按下时指针所在格）
}
function endLeftDrag(root) {
    if (!root.leftDragActive) return
    root.leftDragActive = false
    const n = root.dragSlots.length
    // t167：N≥2 已在 drag 途中实时分完（redistributeLive 每滑入新格重算），此处不再均分。
    // N===1 退化为单格左键（拾取/放置/合并/互换）—— 微拖（越阈值但未离开起点格）的常规左键语义。
    // N===0 无操作（DragHandler 激活时 hoveredKey 已空 / 起点格未入集合的极端情形）。
    if (n === 1) {
        const p = root.dragSlots[0].split(":")
        singleLeftClick(root, p[0], parseInt(p[1], 10))
    }
    root.dragSlots = []
    root.dragOriginal = ({})
    root.dragWritten = ({})
}

// t98 实时均分（替代 t90 松手一次性 applyDragDistribute）：每滑入新格（addDragSlot 触发）即重算
//   floor(dragHeldCount/N) 入格、余数实时回光标（heldBlock/heldCount → Main.qml 浮动图标实时变）。撤销
//   机制：dragOriginal 快照每槽 drag 前原始栈（首次 encounter 拍），dragWritten 记本轮已写槽；下次重分前
//   先据 dragOriginal 把已写格恢复，再按新 N 重分 → 用户看到「滑第 2 格变对半、第 3 格变三等分」的实时
//   反馈。合格过滤：空槽 / 同 id 未满；非可拖拽组跳过（groupIsDraggable 判定，t180：旧版 hardcode 排除
//   "craft"，现泛化为「面板未在 localDragGroups 声明的本地组跳过」——工作台 craft 3×3 现参与、熔炉 out
//   不参与防异物污染输出槽阻断冶炼、生存背包 2×2 craft 维持不参与）；异物 / 已满槽跳过。守恒：写回总量 +
//   余数 = dragHeldCount 快照。N≤1 不分（保留单格左键给 endLeftDrag 处理；空手 drag 无意义）。
function redistributeLive(root) {
    // 1) 撤销上一轮写入（恢复 dragOriginal 记录的原始栈），确保重分前所有 dragSlots 回到 drag 前态。
    for (const key in root.dragWritten) {
        const wp = key.split(":")
        const orig = root.dragOriginal[key]
        writeSlot(root, wp[0], parseInt(wp[1], 10), orig.id, orig.count)
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
        if (!groupIsDraggable(root, p[0])) continue                 // t180：非可拖拽组跳过（替代旧 hardcode "craft" 排除）
        if (!root.dragOriginal[key]) {
            const cur = readSlot(root, p[0], parseInt(p[1], 10))
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
            writeSlot(root, e.group, e.index, heldId, e.base + place)
            root.dragWritten[e.key] = true
            remaining -= place
        }
    }
    root.hotbar.heldBlock = remaining > 0 ? heldId : 0
    root.hotbar.heldCount = remaining
}

// 单格左键（N===1 微拖退路 = resolveClick：空手拾取 / 持物放置 / 合并 / 互换）。正常单击走 per-slot
//   TapHandler.onTapped（DragHandler 未激活）；仅当左键越阈值但只扫过起点一格时经此路径补一次单击语义。
function singleLeftClick(root, group, index) {
    const cur = readSlot(root, group, index)
    const r = resolveClick(root, cur.id, cur.count)
    if (!r) return
    writeSlot(root, group, index, r.slotId, r.slotCount)
    root.hotbar.heldBlock = r.heldId
    root.hotbar.heldCount = r.heldCount
}

// ── t181 右键拖动（每格放 1 个；区别于左键 floor(count/N) 均分）──
//   MC Java 右键拖拽语义：右键按住拖过 N 格 → 每滑入新格放 1 个（空槽开新栈 / 同 id 未满 +1；异 id / 已满 /
//   手空跳过）。**无重分配**（左键均分每加新格全部重分 floor(count/N)；右键只对新格 +1，已放格不回撤）。
//   手势由 root 级 DragHandler(RightButton) 总控（onActiveChanged 驱动 begin/endRightDrag）；逐槽 HoverHandler
//   在 dragActive（left||right）期间收集，addDragSlot 据 rightDragActive 分发到 addRightDragSlot。微拖退路：drag
//   越阈值但全程未放置（空手 / 起点槽异 id 已满）→ 退化为单格右键（拿半 / 放一），与左键 singleLeftClick 同语义。
function rightDragHasKey(root, key) {
    for (let i = 0; i < root.rightDragSlots.length; ++i) if (root.rightDragSlots[i] === key) return true
    return false
}
function beginRightDrag(root) {
    root.rightDragActive = true
    root.rightDragSlots = []
    root.rightDragPlaced = false
    if (root.hoveredKey !== "") addRightDragSlot(root, root.hoveredKey)   // 起点槽立即放 1（持物时）
}
function endRightDrag(root) {
    if (!root.rightDragActive) return
    root.rightDragActive = false
    // 微拖退路：drag 越阈值但全程未放置（空手 / 起点异 id 已满 / 未收集到格）→ 退化为单格右键（拿半 / 放一），
    //   与左键 singleLeftClick 同语义。一旦放置过（持物拖入合格格）不再补单点（避免重复放置）。
    if (!root.rightDragPlaced && root.hoveredKey !== "") {
        const p = root.hoveredKey.split(":")
        if (p.length === 2) singleRightClick(root, p[0], parseInt(p[1], 10))
    }
    root.rightDragSlots = []
    root.rightDragPlaced = false
}
// 每滑入新格放 1 个（addDragSlot 据 rightDragActive 分发到此）。空手 / 异 id / 已满 → 跳过且不计入 placed。
function addRightDragSlot(root, key) {
    if (!root.rightDragActive) return
    const p0 = key.split(":")
    if (!groupIsDraggable(root, p0[0])) return
    if (rightDragHasKey(root, key)) return
    root.rightDragSlots = root.rightDragSlots.concat([key])
    placeOneInSlot(root, p0[0], parseInt(p[1], 10))
}
// 往指定槽放 1 个（空槽开新栈 / 同 id 未满 +1；异 id / 已满 / 手空 → 跳过，不互换）。
function placeOneInSlot(root, group, index) {
    const heldId = root.hotbar.heldBlock
    const heldCount = root.hotbar.heldCount
    if (heldId === 0 || heldCount <= 0) return          // 空手：无物可放
    const cur = readSlot(root, group, index)
    if (cur.id !== 0 && cur.id !== heldId) return        // 异 id：跳过（不互换）
    const cap = root.hotbar.maxStackSize(heldId)
    if (cur.id === heldId && cur.count >= cap) return    // 同 id 已满：跳过
    writeSlot(root, group, index, heldId, cur.count + 1)
    const remain = heldCount - 1
    root.hotbar.heldBlock = remain > 0 ? heldId : 0
    root.hotbar.heldCount = remain
    root.rightDragPlaced = true
}
// 单格右键（微拖退路 = resolveRightClick：空手拿半 / 持物放一）。
function singleRightClick(root, group, index) {
    const cur = readSlot(root, group, index)
    const r = resolveRightClick(root, cur.id, cur.count)
    if (!r) return
    writeSlot(root, group, index, r.slotId, r.slotCount)
    root.hotbar.heldBlock = r.heldId
    root.hotbar.heldCount = r.heldCount
}

// t110 Shift+左键搬运（MC 1.0 背包）：main 槽→首个空 hotbar 槽；hotbar 槽→首个空 main 槽；其它组
//   （craft / in / fuel / out）无操作（spec 仅定义 main↔hotbar）。整栈搬：源清空、目标写入源原内容。
//   「空位」= id==0 的槽。无空位 → 无操作（shift 是显式搬运语义，与普通左键的拾取/放置区分）。
function slotShiftLeft(root, group, index) {
    if (group === "main") {
        const src = readSlot(root, "main", index)
        if (src.id === 0) return
        for (let i = 0; i < root.hotbar.slotCount; ++i) {
            if (readSlot(root, "hotbar", i).id === 0) {
                writeSlot(root, "main", index, 0, 0)
                writeSlot(root, "hotbar", i, src.id, src.count)
                return
            }
        }
    } else if (group === "hotbar") {
        const src = readSlot(root, "hotbar", index)
        if (src.id === 0) return
        for (let i = 0; i < root.hotbar.mainCount; ++i) {
            if (readSlot(root, "main", i).id === 0) {
                writeSlot(root, "hotbar", index, 0, 0)
                writeSlot(root, "main", i, src.id, src.count)
                return
            }
        }
    }
}

// t110 数字键交换（spec「数字键 1-9：背包开 + hoveredKey → 与 hotbar[idx] 交换」）：当前 hover 槽 ↔
//   hotbar[idx] 整栈互换。源 = root.hoveredKey（"组:下标"）；经 readSlot/writeSlot 路由（main/hotbar/craft/
//   in/fuel/out 均覆盖）。group==hotbar 且 srcIdx==idx 时退化为自交换（写入相同值，无副作用）。
function swapHoveredWithHotbar(root, hotbarIdx) {
    if (root.hoveredKey === "") return
    const parts = root.hoveredKey.split(":")
    if (parts.length !== 2) return
    const group = parts[0]
    const srcIdx = parseInt(parts[1], 10)
    if (Number.isNaN(srcIdx)) return
    const src = readSlot(root, group, srcIdx)
    const dst = readSlot(root, "hotbar", hotbarIdx)
    writeSlot(root, group, srcIdx, dst.id, dst.count)
    writeSlot(root, "hotbar", hotbarIdx, src.id, src.count)
}

// t181 双击拿手上（MC：双击某槽 → 扫 main + hotbar（+ 面板声明的本地组）同 id 物品，**全部拾取到光标**，
//   非自动合并到背包首个槽）。targetId 取光标手持 id（典型流程：首次左键拾起该槽 → 二次点击同槽合并），
//   fallback 到所点槽 id（首次为放置时光标空）。t180：扫描范围加 root.localDragGroups（工作台 craft 3×3、
//   熔炉 in/fuel、箱子 chest）。守恒：合并后 (各槽 + 光标) 总量 = 合并前。t181：光标优先拿满（min(total,
//   cap)），余量回填槽（旧实现把满栈塞回前 numFull 个槽、光标只拿余数 → 用户误以为「合并到首个槽」）。
function doMergeSameId(root, group, index) {
    if (!root.hotbar) return
    let targetId = root.hotbar.heldBlock
    if (targetId === 0) {
        const cur = readSlot(root, group, index)
        targetId = cur.id
    }
    if (targetId === 0) return
    const cap = root.hotbar.maxStackSize(targetId)

    // t180：扫描范围 = main + hotbar + root.localDragGroups（声明的本地组）。未声明 localDragGroups 的面板
    //   （创造背包 / 生存背包）只扫 main+hotbar（行为不变）。读走 readSlot 统一路由（main/hotbar→VM、本地组
    //   →localReadSlot），与 drag 路径同源；满栈按扫描顺序回填前 numFull 个槽（main→hotbar→本地），余数留光标。
    const groupList = [["main", root.hotbar.mainCount], ["hotbar", root.hotbar.slotCount]]
    const localGroups = root.localDragGroups
    if (Array.isArray(localGroups)) {
        for (let i = 0; i < localGroups.length; ++i) {
            const lg = localGroups[i]
            groupList.push([lg, root.localSlotCount ? root.localSlotCount(lg) : 0])
        }
    }

    // 收集所有同 id 槽位 + 光标，求总量。
    const slots = []
    let total = 0
    if (root.hotbar.heldBlock === targetId) total += root.hotbar.heldCount
    for (let gi = 0; gi < groupList.length; ++gi) {
        const gname = groupList[gi][0]
        const gcnt = groupList[gi][1]
        for (let i = 0; i < gcnt; ++i) {
            const s = readSlot(root, gname, i)
            if (s.id === targetId) {
                total += s.count
                slots.push({ group: gname, index: i })
            }
        }
    }
    if (total <= 0) return

    // 清空所有同 id 槽 + 光标（即将重新打包）。
    for (let i = 0; i < slots.length; ++i) {
        writeSlot(root, slots[i].group, slots[i].index, 0, 0)
    }
    root.hotbar.heldBlock = 0
    root.hotbar.heldCount = 0

    // t181：双击 = 拾取全部同类到**光标**（非自动合并到背包首个槽）。光标优先拿满（min(total, cap)）；
    //   余量（total > cap 时）按扫描顺序回填入槽（满栈优先前 numFull 个、尾余入下一槽），物品守恒。
    //   旧实现把满栈塞回前 numFull 个槽、光标只拿余数 → 用户看到「物品合并到首个槽、光标坐标不准」
    //   （物品去了首个槽而非光标手上）。total ≤ cap 时光标拿全部、所有同 id 槽清空 = 全部拾起到手。
    const cursorCount = Math.min(total, cap)
    const remain = total - cursorCount
    const numFull = Math.min(Math.floor(remain / cap), slots.length)
    for (let i = 0; i < numFull; ++i) {
        writeSlot(root, slots[i].group, slots[i].index, targetId, cap)
    }
    const tail = remain - numFull * cap
    if (tail > 0 && numFull < slots.length) {
        writeSlot(root, slots[numFull].group, slots[numFull].index, targetId, tail)
    }
    if (cursorCount > 0) {
        root.hotbar.heldBlock = targetId
        root.hotbar.heldCount = cursorCount
    }
}
