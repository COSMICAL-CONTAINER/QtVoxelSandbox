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

// ── 左键整组栈操作（t38）：给定目标槽当前 (curId, curCount, curDur) 与光标手持栈 (heldBlock, heldCount,
//    heldDurability)，返回应写入的 {slotId, slotCount, slotDur, heldId, heldCount, heldDur}；null = 无操作。
//    4 种 case：
//      A 手持空 + 槽非空：拾取整栈（含工具耐久随实例走，curDur → heldDur 保真）。
//      B 手持非空 + 槽空：放置整栈（heldDur → slotDur 保真）。
//      C 手持非空 + 同 id：合并至 maxStackSize(id)（方块 64 / 工具段 1），余数留 held；槽满则无操作。
//        工具段 cap=1 → 已有同 id 槽恒满 → 不合并（A/B/D 路径覆盖工具搬运）。耐久不进合并键（同 id 工具不可
//        堆叠，故无「合并两把镐」语义）。
//      D 手持非空 + 异 id：互换（双方耐久随各自实例交换）。
//    纯函数（只读 root.hotbar），无副作用 —— 调用方据返回值写入对应槽 + 更新 held。
function resolveClick(root, curId, curCount, curDur, curEnch) {
    const heldId = root.hotbar.heldBlock
    const heldCount = root.hotbar.heldCount
    const heldDur = root.hotbar.heldDurability                            // t263 工具耐久随实例走
    const heldEnch = root.hotbar.heldEnchants()                           // t475 工具 / 护甲附魔随实例走
    const cEnch = curEnch ? curEnch : [0,0,0,0]                           // t475 本地组兜底 4 个 0
    if (heldId === 0) {
        if (curId === 0) return null                                       // 空手点空槽：无操作
        // t263 拾取工具：本地槽（craft/chest/in/fuel）不持耐久 → curDur=0/undefined → 视作新工具（满耐久，-1=自动）。
        //   hotbar/main 槽 curDur>0 → 实例耐久保真。非工具 curDur 恒 0（inert）。
        // t550 护甲同工具语义（有独立耐久）：main/hotbar/equip 槽护甲 curDur>0 → 实例耐久保真（修「护甲从背包
        //   拾起即满耐久」——拾起装备过的护甲丢失耐久，铁砧修复无从谈起）；本地槽护甲 curDur=0 → 视作新护甲（满耐久）。
        const pickupDur = (root.hotbar.isTool(curId) || root.hotbar.isArmor(curId)) ? (curDur > 0 ? curDur : -1) : 0
        return { slotId: 0, slotCount: 0, slotDur: 0, slotEnch: [0,0,0,0],// A 拾取整栈：槽清空（耐久 / 附魔随物品移走）
                 heldId: curId, heldCount: curCount, heldDur: pickupDur, heldEnch: cEnch }
    }
    if (curId === 0) {
        return { slotId: heldId, slotCount: heldCount, slotDur: heldDur, slotEnch: heldEnch, // B 放整栈：耐久 / 附魔随物品入槽
                 heldId: 0, heldCount: 0, heldDur: 0, heldEnch: [0,0,0,0] }
    }
    if (curId === heldId) {
        // C 合并：min(剩余空间, 手持数) 移入槽；手持余 0 → heldId 归 0（保持空栈不变式）。
        const cap = root.hotbar.maxStackSize(curId)
        const space = cap - curCount
        if (space <= 0) return null                                        // 槽已满（含工具段 cap=1）：无操作
        const move = Math.min(space, heldCount)
        const remain = heldCount - move
        return {
            slotId: curId, slotCount: curCount + move, slotDur: curDur, slotEnch: cEnch, // 同 id 合并：耐久 / 附魔不变（工具段不进此分支）
            heldId: remain > 0 ? heldId : 0, heldCount: remain, heldDur: remain > 0 ? heldDur : 0, heldEnch: remain > 0 ? heldEnch : [0,0,0,0]
        }
    }
    return { slotId: heldId, slotCount: heldCount, slotDur: heldDur, slotEnch: heldEnch, // D 互换：双方耐久 / 附魔随各自实例交换
             heldId: curId, heldCount: curCount, heldDur: curDur, heldEnch: cEnch }
}

// ── 右键语义（t49，MC 1.0）：空手→拾取一半（floor(count/2)，单件特例取 1）；持物→放 1 个
//    （空槽开新栈 / 同 id 未满 +1；异 id 槽 / 已满无操作，**不互换**）。返回与 resolveClick 同形；null = 无操作。
//    t263 工具段（cap=1）右键：空手点工具槽 → 单件特例 half=1 整件拿起（耐久保真）；持工具点空槽 → 放 1（=整件，
//    耐久保真）；持工具点同 id 槽恒满 → 无操作；持工具点异 id 槽 → 无操作（不互换）。耐久随实例全程透传。
function resolveRightClick(root, curId, curCount, curDur, curEnch) {
    const heldId = root.hotbar.heldBlock
    const heldCount = root.hotbar.heldCount
    const heldDur = root.hotbar.heldDurability
    const heldEnch = root.hotbar.heldEnchants()
    const cEnch = curEnch ? curEnch : [0,0,0,0]
    if (heldId === 0) {
        if (curId === 0) return null                                     // 空手点空槽：无操作
        let half = Math.floor(curCount / 2)
        if (half < 1) half = 1                                           // 单件（工具段）：整件拿起
        // 工具单件 half==curCount → 槽清空（slotDur=0）。t263 拾取工具耐久：本地槽 curDur=0 → 视作新工具（-1=自动满）。
        // t550 护甲同工具语义（有独立耐久）：curDur>0 → 实例耐久保真（修「护甲拾起即满耐久」）。
        const cleared = (curCount - half) <= 0
        const pickupDur = (root.hotbar.isTool(curId) || root.hotbar.isArmor(curId)) ? (curDur > 0 ? curDur : -1) : 0
        return {
            slotId: cleared ? 0 : curId, slotCount: curCount - half, slotDur: cleared ? 0 : curDur,
            slotEnch: cleared ? [0,0,0,0] : cEnch,
            heldId: curId, heldCount: half, heldDur: pickupDur, heldEnch: cEnch
        }
    }
    if (curId !== 0 && curId !== heldId) return null                     // 异 id 槽：无操作（不互换）
    const cap = root.hotbar.maxStackSize(heldId)
    if (curId === heldId && curCount >= cap) return null                 // 同 id 已满（工具段恒满）：无操作
    const remain = heldCount - 1
    // 放 1 个到槽：工具段 cap=1 → 放的就是整件（heldDur/heldEnch → slotDur/slotEnch 保真）；光标余 0 → heldDur/heldEnch 归 0。
    return {
        slotId: heldId, slotCount: curCount + 1, slotDur: heldDur, slotEnch: heldEnch,
        heldId: remain > 0 ? heldId : 0, heldCount: remain, heldDur: remain > 0 ? heldDur : 0, heldEnch: remain > 0 ? heldEnch : [0,0,0,0]
    }
}

// ── 槽读写统一路由（t168 抽取；t263 加 durability 维度；t475 加 enchants 维度）──
//    main/hotbar 走 hotbar VM（四面板一致 → 收敛于此）；其它组（craft / in / fuel / out）委托面板的
//    localReadSlot/localWriteSlot 钩子。无钩子面板（Inventory）以真值检测兜底（readSlot 空栈 / writeSlot 空操作）。
//    index 对 main/hotbar 有效，对本地单槽组忽略。
//    t263 durability：main/hotbar 槽透传工具耐久（readSlot 返 .durability / writeSlot 第 6 参）；本地组（craft/
//      in/fuel/out）当前不持耐久（工具罕见进本地组 → 读写恒 0，setStack 对工具 dur=0 归一为 1 = 满耐久兜底，
//      即本地组里的工具视作新工具；可接受边角，主路径 hotbar/main 已保真）。
//    t475 enchants：main/hotbar/armor 槽透传附魔元数据（readSlot 返 .enchants = 4-int 数组 / writeSlot 第 7 参）。
//      附魔仅工具 / 武器 / 护甲（cap==1 不可堆叠）有意义 → 只在「整件搬运」路径（拾取 / 放置 / 互换 / Shift /
//      数字键 / 双击）透传；可堆叠物品的合并 / 拖动均分路径附魔恒 0（无意义）。本地组（craft/in/fuel/out）不持
//      附魔（同耐久：工具罕见进本地组 → 读 4 个 0；setStack 对工具空附魔 = 无附魔兜底，可接受边角）。
function readSlot(root, group, index) {
    if (group === "main")   return { id: root.hotbar.mainBlockIdAt(index), count: root.hotbar.mainCountAt(index), durability: root.hotbar.mainDurabilityAt(index), enchants: root.hotbar.mainEnchantsAt(index) }
    if (group === "hotbar") return { id: root.hotbar.blockIdAt(index), count: root.hotbar.countAt(index), durability: root.hotbar.durabilityAt(index), enchants: root.hotbar.enchantsAt(index) }
    // t377 装备槽（4 护甲槽：头/胸/腿/脚；index = 部位）。InventoryOps 路由护甲槽读，供 slotShiftLeft「Shift+左键
    //   装备中的护甲 → 整件归还背包」用（同 main/hotbar 经 VM 统一）。t475 含 .enchants（护甲可附魔）。
    if (group === "armor") return { id: root.hotbar.armorBlockIdAt(index), count: root.hotbar.armorCountAt(index), durability: root.hotbar.armorDurabilityAt(index), enchants: root.hotbar.armorEnchantsAt(index) }
    if (root.localReadSlot) {
        const r = root.localReadSlot(group, index)
        // t475 本地组钩子不返 enchants → 补默认 4 个 0（统一结构，下游无 null 判定）。
        if (!r.enchants) r.enchants = [0, 0, 0, 0]
        return r
    }
    return { id: 0, count: 0, durability: 0, enchants: [0, 0, 0, 0] }
}
function writeSlot(root, group, index, id, count, durability, enchants) {
    // t263 durability 缺省 -1（=自动：工具满耐久 / 非工具 0）；resolveClick 等显式传实例耐久时保真。
    const dur = (durability === undefined) ? -1 : durability
    // t475 enchants 缺省 undefined → main/hotbar/armor 走 VM 默认（清空附魔）；显式传 4-int 数组时保真搬运。
    const ench = (enchants === undefined) ? [] : enchants
    if (group === "main")        { root.hotbar.mainSetStack(index, id, count, dur, ench); return }
    if (group === "hotbar")      { root.hotbar.setStack(index, id, count, dur, ench); return }
    // t377 装备槽写经 armorSetStack（VM 守部位匹配 + count 钳 1）；slotShiftLeft 护甲装备 / 脱下用。
    //   t475 含 enchants（护甲可附魔；装备 / 脱下搬运保真）。
    if (group === "armor")       { root.hotbar.armorSetStack(index, id, count, dur, ench); return }
    if (root.localWriteSlot)     root.localWriteSlot(group, index, id, count)
}

// ── 拖动均分辅助（t79/t98/t108/t167）──
// 槽 key（"组:下标"）。字符串而非对象：去重 / 比较 / 拆分都直接，无需手写对象相等。
function slotKey(group, index) { return group + ":" + index }

// t228 面板边界判定（spec「拖出面板边界判定」）：给定 root 坐标系下的点 (x, y) 与面板 Item（各面板的
//   `panel` Rectangle），返回该点是否落在面板矩形内。背包丢弃门控用：mask MouseArea 全屏铺底，点击位置
//   在面板内（即使是非槽位的空白）→ 不丢弃（物品留光标）；只有真正点出整栏外（mask 区）才丢。修「左键拿物
//   在面板内非槽位松手 → 直接丢地下」bug：原 mask 无边界判定，面板内空点击穿透即触发丢弃。
//   坐标映射：mask `anchors.fill: parent`（= root）→ mask 坐标 == root 坐标；panel.mapFromItem(root, x, y)
//   把点换算到 panel 自身坐标系（左上角原点），再判是否落在 [0,width]×[0,height]。panel 作为 id 在 mask 之后
//   声明，但 QML id 在运行期解析，onClicked 触发时 panel 已实例化 → 引用安全。
function pointInsidePanel(root, panelItem, x, y) {
    if (!panelItem) return false
    const local = panelItem.mapFromItem(root, x, y)
    return local.x >= 0 && local.x < panelItem.width && local.y >= 0 && local.y < panelItem.height
}
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
    // 空/同 id 槽）。t204：同 id 已满槽亦不入（redistributeLive 会把它剔出 eligible，此处同步剔除让绿框
    //   不误导 + 让手持数上限按「真正会收物的格」计数）。dragHeldId=0（空手拖）不过滤，让起点槽入
    //   dragSlots 供 endLeftDrag→singleLeftClick。
    if (root.dragHeldId !== 0) {
        const cur = readSlot(root, p0[0], parseInt(p0[1], 10))
        if (cur.id !== 0 && cur.id !== root.dragHeldId) return
        if (cur.id === root.dragHeldId) {
            const cap0 = root.hotbar.maxStackSize(root.dragHeldId)
            if (cur.count >= cap0) return
        }
        // t204：左键拖拽上限=手持数。每合格格至少分 1 件 → 收集格数 ≤ dragHeldCount；超出即不收集
        //   （不高亮、不分配），从源头让「绿格数 ≤ 手持数」。dragSlots 此处仅含真正会收物的合格格
        //   （异物/已满已在上剔），故 length 即「已收集的合格格数」，与 redistributeLive 的
        //   eligible.slice(0,total) 同源（截断在此前置 = 高亮与分配一并收紧，而非仅分配截断、高亮仍全亮）。
        //   空手拖（dragHeldId=0，外层 if 已挡）不受此限；dragHeldCount=0 防御性跳过（理论不出现）。
        if (root.dragHeldCount > 0 && root.dragSlots.length >= root.dragHeldCount) return
    }
    root.dragSlots = root.dragSlots.concat([key])   // 新数组引用 → 依赖 dragSlots 的绑定刷新
    redistributeLive(root)                          // t98：每滑入新格实时重算 N 等分（撤销 + 重分）
}
// 手势生命周期（root DragHandler.onActiveChanged 调）。
//   t263 dragHeldDurability 快照手持工具耐久（工具段不可拆分 → 拖动期间耐久不变，松手时回填光标保真）。
//   t475 dragHeldEnchants 快照手持附魔（同耐久语义：工具 / 护甲拖动期间附魔不变，松手时回填光标保真）。
function beginLeftDrag(root) {
    root.dragHeldId = root.hotbar.heldBlock
    root.dragHeldCount = root.hotbar.heldCount
    root.dragHeldDurability = root.hotbar.heldDurability
    root.dragHeldEnchants = root.hotbar.heldEnchants()
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
        writeSlot(root, wp[0], parseInt(wp[1], 10), orig.id, orig.count, orig.durability, orig.enchants)
    }
    root.dragWritten = ({})

    const heldId = root.dragHeldId
    const total = root.dragHeldCount
    const heldDur = root.dragHeldDurability                       // t263 工具耐久快照（拖动期间不变）
    const heldEnch = root.dragHeldEnchants                        // t475 附魔快照（同耐久：工具 / 护甲拖动期间不变）
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
            root.dragOriginal[key] = { id: cur.id, count: cur.count, durability: cur.durability, enchants: cur.enchants }
        }
        const orig = root.dragOriginal[key]
        if (orig.id === 0 || (orig.id === heldId && orig.count < cap))
            eligible.push({ group: p[0], index: parseInt(p[1], 10), key: key, base: orig.count, dur: orig.durability, ench: orig.enchants })
    }

    // t108/t204：n>total 截断 eligible 到 total 项（每格至少 1 件；N≤count）。t204 起 dragSlots 收集已在
    //   addDragSlot 按 dragHeldCount 上限预裁（绿格数 ≤ 手持数），故此处 n≤total 通常恒成立；本截断保留为
    //   防御纵深（addDragSlot 未覆盖的边界，如未来动态改 dragSlots）。截断在 n<=1 早退之前。
    let n = eligible.length
    if (n > total) { eligible = eligible.slice(0, total); n = eligible.length }
    // N≤1 / 空手 / 无物：不分（保留单格左键给 endLeftDrag；空手 drag 无意义）。余数 = 原始快照。
    //   t263 工具段（cap=1）拖动：total=1 / N>1 → per=0 → 不分；耐久随物品留光标（heldDur 回填保真）。
    //   t475 附魔同理：工具 / 护甲拖动期间附魔留光标（heldEnch 回填保真）。
    if (n <= 1 || heldId === 0 || total <= 0) {
        root.hotbar.heldBlock = heldId
        root.hotbar.heldCount = total
        root.hotbar.heldDurability = heldDur
        root.hotbar.setHeldEnchants(heldEnch)
        return
    }

    // 3) floor(total/N) 入格（cap 钳制防溢出），余数留光标；记 dragWritten 供下轮撤销。
    //   t263 同 id 合并（方块段）耐久不变（e.dur = 槽原始耐久）；工具段不进此分支（cap=1 恒满不进 eligible 合并）。
    //   t475 同 id 合并附魔不变（e.ench = 槽原始附魔；可堆叠物品恒 4 个 0）。
    const per = Math.floor(total / n)
    let remaining = total
    if (per > 0) {
        for (let i = 0; i < n; ++i) {
            const e = eligible[i]
            const place = Math.min(per, cap - e.base)
            if (place <= 0) continue
            writeSlot(root, e.group, e.index, heldId, e.base + place, e.dur, e.ench)
            root.dragWritten[e.key] = true
            remaining -= place
        }
    }
    root.hotbar.heldBlock = remaining > 0 ? heldId : 0
    root.hotbar.heldCount = remaining
    root.hotbar.heldDurability = remaining > 0 ? heldDur : 0
    root.hotbar.setHeldEnchants(remaining > 0 ? heldEnch : [0,0,0,0])
}

// 单格左键（N===1 微拖退路 = resolveClick：空手拾取 / 持物放置 / 合并 / 互换）。正常单击走 per-slot
//   TapHandler.onTapped（DragHandler 未激活）；仅当左键越阈值但只扫过起点一格时经此路径补一次单击语义。
//   t263 耐久随实例透传（readSlot→resolveClick→writeSlot 全程携带 slotDur/heldDur）。
function singleLeftClick(root, group, index) {
    const cur = readSlot(root, group, index)
    const r = resolveClick(root, cur.id, cur.count, cur.durability, cur.enchants)
    if (!r) return
    writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
    root.hotbar.heldBlock = r.heldId
    root.hotbar.heldCount = r.heldCount
    root.hotbar.heldDurability = r.heldDur                         // t263 工具耐久保真（setHeldBlock 已自动填 max，此处覆盖为实例值）
    root.hotbar.setHeldEnchants(r.heldEnch)                        // t475 附魔随实例保真（setHeldBlock 已清空，此处覆盖为实例值）
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
//   t205 根因：旧版 `parseInt(p[1], 10)` 误写成 `p`（本作用域只有 `p0`，无 `p`）→ 运行期 ReferenceError。
//   QML 信号处理器（DragHandler.onActiveChanged / 逐槽 HoverHandler → addDragSlot → 此处）抛出的 JS 异常被
//   QML 引擎吞掉（仅 console 报错，不崩 app），故右键拖拽每滑入一格即抛错、placeOneInSlot 永不执行 → 全程
//   不放物；只有松手时 endRightDrag 的微拖退路（rightDragPlaced 仍 false → singleRightClick）补放 1 个到松手格。
//   用户观感 = 「右键单击放1正常，但右键拖只放1个/没激活」。修：`p` → `p0`。这类「信号处理器内引用未定义变量」
//   的 ReferenceError 是 QML/JS 静默退化典型：app 不崩、编译期 qmlcachegen 不查函数体未定义引用、harness 三测
//   全过，唯独 run+肉眼见功能坏 —— 凡 JS 信号处理器路径改完必 run 验证，勿信「编译过=对」。
//   t205 增强：仅 placeOneInSlot 真放置时才把格记入 rightDragSlots —— rightDragSlots 即「实际收到物品的格」，
//   供各面板绿框高亮（rightDragHasKey）准确反映「此格已放」（与左键 dragSlots 高亮对称），避免异 id/已满格
//   误亮框。rightDragPlaced（全程是否真放置过，endRightDrag 微拖退路用）与「单格是否入 rightDragSlots」同源。
function addRightDragSlot(root, key) {
    if (!root.rightDragActive) return
    const p0 = key.split(":")
    if (!groupIsDraggable(root, p0[0])) return
    if (rightDragHasKey(root, key)) return
    if (!placeOneInSlot(root, p0[0], parseInt(p0[1], 10))) return   // 未放置（空手/异 id/已满）→ 不入高亮集
    root.rightDragSlots = root.rightDragSlots.concat([key])
}
// 往指定槽放 1 个（空槽开新栈 / 同 id 未满 +1；异 id / 已满 / 手空 → 跳过，不互换）。返回是否真放置。
//   t263 工具段（cap=1）右键拖：放 1 = 放整件（heldDur → slotDur 保真）；手持余 0 → heldDur 归 0。
//   t475 附魔同理：空槽开新工具 / 护甲写手持附魔（heldEnch → slotEnch 保真）；同 id 合并附魔不变（可堆叠恒 0）。
function placeOneInSlot(root, group, index) {
    const heldId = root.hotbar.heldBlock
    const heldCount = root.hotbar.heldCount
    const heldDur = root.hotbar.heldDurability
    const heldEnch = root.hotbar.heldEnchants()
    if (heldId === 0 || heldCount <= 0) return false    // 空手：无物可放
    const cur = readSlot(root, group, index)
    if (cur.id !== 0 && cur.id !== heldId) return false  // 异 id：跳过（不互换）
    const cap = root.hotbar.maxStackSize(heldId)
    if (cur.id === heldId && cur.count >= cap) return false // 同 id 已满：跳过
    // 同 id 合并（方块段）耐久 / 附魔不变（cur.durability / cur.enchants）；空槽开新工具（工具段）写手持耐久 / 附魔（保真）。
    const slotDur = (cur.id === heldId) ? cur.durability : heldDur
    const slotEnch = (cur.id === heldId) ? cur.enchants : heldEnch
    writeSlot(root, group, index, heldId, cur.count + 1, slotDur, slotEnch)
    const remain = heldCount - 1
    root.hotbar.heldBlock = remain > 0 ? heldId : 0
    root.hotbar.heldCount = remain
    root.hotbar.heldDurability = remain > 0 ? heldDur : 0
    root.hotbar.setHeldEnchants(remain > 0 ? heldEnch : [0,0,0,0])
    root.rightDragPlaced = true
    return true
}
// 单格右键（微拖退路 = resolveRightClick：空手拿半 / 持物放一）。t263 耐久透传（同 singleLeftClick）。
function singleRightClick(root, group, index) {
    const cur = readSlot(root, group, index)
    const r = resolveRightClick(root, cur.id, cur.count, cur.durability, cur.enchants)
    if (!r) return
    writeSlot(root, group, index, r.slotId, r.slotCount, r.slotDur, r.slotEnch)
    root.hotbar.heldBlock = r.heldId
    root.hotbar.heldCount = r.heldCount
    root.hotbar.heldDurability = r.heldDur
    root.hotbar.setHeldEnchants(r.heldEnch)                        // t475 附魔随实例保真
}

// t110 Shift+左键搬运（MC 1.0 背包）+ t230 扩展（craft 归包）：main 槽→首个空 hotbar 槽；hotbar 槽→首个空
//   main 槽；craft 槽→智能归还背包（main → hotbar，同 id 合并）；其它组（in / fuel / out）无操作（spec t110
//   仅定义 main↔hotbar；t230 craft 归包；熔炉 in/fuel/out 由 slotShiftLeftFurnace 在 FurnaceUI 单独处理）。
//   main/hotbar 整栈搬：源清空、目标写入源原内容；「空位」= id==0 的槽，无空位 → 无操作（shift 是显式搬运
//   语义，与普通左键的拾取/放置区分）。t230 craft 归包走 addToAny（同 id 合并 → 空槽开新），背包满 → 余数
//   留 craft 槽（不清空，防丢物）。
function slotShiftLeft(root, group, index) {
    // t377 Shift+左键装备护甲：main/hotbar 持有护甲 → 装到对应部位槽（旧件换回源槽）。
    //   机制等价 MC 1.0「Shift+左键背包护甲 → 自动装备到对应槽」（spec t377「survival inventory Shift+Left-click an
    //   armor piece -> equip to its slot」）。护甲不可堆叠 → count 恒 1；耐久随实例保真搬运。非护甲走下方通用搬运。
    if (group === "main" || group === "hotbar") {
        const src = readSlot(root, group, index)
        if (src.id !== 0 && root.hotbar.isArmor(src.id)) {
            const piece = root.hotbar.armorPiece(src.id)
            const old = readSlot(root, "armor", piece)
            writeSlot(root, group, index, 0, 0, 0)                          // 取出源槽护甲
            writeSlot(root, "armor", piece, src.id, 1, src.durability, src.enchants)      // 装备到部位槽（附魔随实例）
            if (old.id !== 0) writeSlot(root, group, index, old.id, old.count, old.durability, old.enchants) // 旧件换回源槽
            return
        }
    }
    // t377 Shift+左键装备槽护甲 → 整件归还背包（addToAny：main 同 id 合并 → hotbar 同 id → 空槽）。
    //   spec t377「Shift+Left-click an equipped piece -> move to inventory」。背包满 → 余数留装备槽。
    //   t475 附魔随实例归还（addToAny 第 4 参透传）。
    if (group === "armor") {
        const src = readSlot(root, "armor", index)
        if (src.id === 0 || src.count <= 0) return
        const remain = root.hotbar.addToAny(src.id, src.count, src.durability, src.enchants)
        writeSlot(root, "armor", index, remain > 0 ? src.id : 0, remain, src.durability, remain > 0 ? src.enchants : [0,0,0,0])
        return
    }
    if (group === "main") {
        const src = readSlot(root, "main", index)
        if (src.id === 0) return
        for (let i = 0; i < root.hotbar.slotCount; ++i) {
            if (readSlot(root, "hotbar", i).id === 0) {
                writeSlot(root, "main", index, 0, 0, 0)
                writeSlot(root, "hotbar", i, src.id, src.count, src.durability, src.enchants)   // t263/t475 耐久 / 附魔随实例搬运
                return
            }
        }
    } else if (group === "hotbar") {
        const src = readSlot(root, "hotbar", index)
        if (src.id === 0) return
        for (let i = 0; i < root.hotbar.mainCount; ++i) {
            if (readSlot(root, "main", i).id === 0) {
                writeSlot(root, "hotbar", index, 0, 0, 0)
                writeSlot(root, "main", i, src.id, src.count, src.durability, src.enchants)     // t263/t475 耐久 / 附魔随实例搬运
                return
            }
        }
    } else if (group === "craft") {
        // t230：合成槽 Shift+左键 → 整栈归还背包（spec「背包内合成槽物品按 shift 左键也会回到背包槽」）。
        //   走 addToAny（main 同 id 合并 → hotbar 同 id → 空槽），与拾取/丢弃回栏同源；优于「仅查空槽」的
        //   main/hotbar 路径（修「同 id 未满栈旁还有空位却搬不进去」边角）。背包满 → 余数留 craft 槽。
        //   t263 本地 craft 槽不持耐久（工具罕见进合成格）→ addToAny 自动填满耐久（合成产物视为新工具）。
        if (!root.hotbar) return
        const src = readSlot(root, "craft", index)
        if (src.id === 0 || src.count <= 0) return
        const remain = root.hotbar.addToAny(src.id, src.count)
        writeSlot(root, "craft", index, remain > 0 ? src.id : 0, remain)
    }
}

// t521 箱子 Shift+左键双向语义（spec「箱子界面 shift+左键物品应放入箱子；反之从箱子槽移入背包」）。
//   仿工作台 / 熔炉的 Shift+左键双向语义（slotShiftLeftFurnace 同模式）：仅 ChestUI 调用。
//   caller（ChestUI.slotShiftLeft）先调本函数，返回 true 表已处理（不回退）、false 表「未知组」→ caller 回退
//   到通用 slotShiftLeft（main↔hotbar 搬运）。箱子界面 main/hotbar/chest 三组均被本函数处理（总返 true），
//   故通用版本在箱子界面永不被触达 —— 箱子界面不需要 main↔hotbar 整理（那是普通背包 / 生存背包的功能），
//   也不应走 slotShiftLeft 的护甲装备分支（箱子界面 shift 护甲应入箱子，非装备；装备是 SurvivalInventory 功能）。
//   - main/hotbar → 整栈放入箱子（addToChest：同 id 合并 → 空槽开新）；箱子满 → 余数留源槽（防丢物）。
//   - chest → 整栈归还背包（addToAny：main 同 id 合并 → hotbar 同 id → 空槽）；背包满 → 余数留原槽。
//   t263/t475 main/hotbar 源槽写回时透传 src.durability/src.enchants（VM 组保真：工具 / 护甲 shift 入箱余数回源
//   不丢耐久 / 附魔）；chest 本地组不持耐久 / 附魔（同 craft/in/fuel/out）→ 写回不传（localWriteSlot 只取前 4 参）。
function slotShiftLeftChest(root, group, index) {
    if (!root.hotbar) return false
    if (group === "main" || group === "hotbar") {
        const src = readSlot(root, group, index)
        if (src.id === 0 || src.count <= 0) return true                  // 空槽：已处理（无操作）
        const remain = addToChest(root, src.id, src.count)
        writeSlot(root, group, index, remain > 0 ? src.id : 0, remain,
                  remain > 0 ? src.durability : 0, remain > 0 ? src.enchants : [0,0,0,0])
        return true
    }
    if (group === "chest") {
        const src = readSlot(root, "chest", index)
        if (src.id === 0 || src.count <= 0) return true
        const remain = root.hotbar.addToAny(src.id, src.count)            // 归还背包（main→hotbar 智能堆叠）
        writeSlot(root, "chest", index, remain > 0 ? src.id : 0, remain)
        return true
    }
    return false   // 未知组（ChestUI 无此情形）→ caller 回退
}
// addToChest：往当前所开箱子智能堆叠放入（同 id 合并 → 空槽开新）；返回未放入数。箱子满 → 余数留源
//   （slotShiftLeftChest 写回源槽）。仿 Hotbar::addToAny 的多槽泛化（addToAny 只管 main/hotbar；箱子是独立
//   27 槽容器，须本处遍历）。槽位数读 root.localSlotCount("chest")（ChestUI 声明 = chestStore.slotCount，恒 27），
//   真值检测兜底（无钩子面板返 0 → 不放入）。
//   本地组不持耐久 / 附魔（同 craft/in/fuel/out）→ 写回不传 dur/ench（工具罕见进箱子，可接受边角；与既有
//   chest 槽左键放置 / 拖拽均分写回一致）。
function addToChest(root, id, count) {
    const slotCount = root.localSlotCount ? root.localSlotCount("chest") : 0
    const cap = root.hotbar.maxStackSize(id)
    let remaining = count
    // 1) 合并同 id 未满槽（同 id 堆叠至上限）。
    for (let i = 0; i < slotCount && remaining > 0; ++i) {
        const cur = readSlot(root, "chest", i)
        if (cur.id === id && cur.count < cap) {
            const move = Math.min(cap - cur.count, remaining)
            writeSlot(root, "chest", i, id, cur.count + move)
            remaining -= move
        }
    }
    // 2) 开新空槽（同 id 已无处可并 → 散入空槽）。
    for (let i = 0; i < slotCount && remaining > 0; ++i) {
        const cur = readSlot(root, "chest", i)
        if (cur.id === 0) {
            const move = Math.min(cap, remaining)
            writeSlot(root, "chest", i, id, move)
            remaining -= move
        }
    }
    return remaining
}

// t230 熔炉 Shift+左键智能入出（spec「熔炉 Shift+点击→智能入（可烧物→上格/燃料→下格）/ 出」）。
//   仅 FurnaceUI 调用：caller（FurnaceUI.slotLeft）先调本函数，返回 true 表已处理（不回退）、false 表
//   「非炉料 main/hotbar 或未知组」→ caller 回退到通用 slotShiftLeft（main↔hotbar 搬运）。
//   - main/hotbar 上炉料 → 智能：可烧物（smeltResult≠0）优先入 in 槽；否则燃料（fuelBurnSeconds>0）入 fuel
//     槽；都不是 → 返回 false 让 caller 回退到 main↔hotbar 搬运（MC 1.0：非炉料按通用背包整理）。
//   - in/fuel/out → 整栈归还背包（addToAny：main 同 id 合并 → hotbar 同 id → 空槽）；out 提取、in/fuel 取回。
//   跨槽搬运：合并同类 / 空槽开新（同 id 合并至上限，异物占位不覆盖，目标满则部分搬、源留余）。
function slotShiftLeftFurnace(root, group, index) {
    if (!root.hotbar) return false
    if (group === "main" || group === "hotbar") {
        const src = readSlot(root, group, index)
        if (src.id === 0 || src.count <= 0) return true                  // 空槽：已处理（无操作）
        const isSmeltable = root.hotbar.smeltResult(src.id) !== 0
        const isFuel = root.hotbar.fuelBurnSeconds(src.id) > 0
        if (!isSmeltable && !isFuel) return false                        // 非炉料：回退通用 main↔hotbar
        // 可烧物优先（原木既可烧木炭又是燃料 → MC 选输入槽优先）。
        const targetGroup = isSmeltable ? "in" : "fuel"
        const target = readSlot(root, targetGroup, 0)
        if (target.id !== 0 && target.id !== src.id) return true         // 异物占位 → 不覆盖（无操作）
        const cap = root.hotbar.maxStackSize(src.id)
        const space = cap - target.count
        if (space <= 0) return true                                      // 目标满 → 无操作
        const move = Math.min(space, src.count)
        // t263 炉料槽（in/fuel）本地不持耐久 → 写手持耐久保真（src.durability）；工具段罕见进炉（非燃料/可烧物）。
        writeSlot(root, targetGroup, 0, src.id, target.count + move, src.durability)
        const remain = src.count - move
        writeSlot(root, group, index, remain > 0 ? src.id : 0, remain, remain > 0 ? src.durability : 0)
        return true
    }
    if (group === "in" || group === "fuel" || group === "out") {
        // 整栈归还背包（提取 out / 取回 in/fuel）。addToAny 智能合并；背包满 → 余数留原槽。
        const src = readSlot(root, group, index)
        if (src.id === 0 || src.count <= 0) return true
        const remain = root.hotbar.addToAny(src.id, src.count)
        writeSlot(root, group, index, remain > 0 ? src.id : 0, remain)
        return true
    }
    return false   // 未知组（FurnaceUI 无此情形）→ caller 回退
}

// t230 工作台 3×3 / 生存 2×2 Shift+左键结果槽 → 批量合成（spec「Shift+点合成产物→批量合成，耗尽最小原料数，
//   一次入背包」）。仅 CraftingTableUI / SurvivalInventory 调用（两面板均有 craftSlots/craftCounts/craftRev/
//   matchedRecipe）。机制等价 MC 1.0：shift+左键结果槽 → 一次合多次，产物入背包（非光标）。
//   - maxCrafts = 每非空原料槽 count 的最小值（每合成消耗每非空槽 1，同既有单次 craftOne consume 规则）。
//     例：planks 配方（1 原木 → 4 板），原料槽 [2,0,0,0] → maxCrafts=2 → 8 板；火把（1 煤+1 棍 → 4 把），
//     煤槽 count=4 / 棍槽 count=3 → maxCrafts=3 → 12 把。
//   - t268：光标持有任意物品（含非产物的异物）时也批量合——产物入背包（addToAny 只动 main/hotbar、不碰光标），
//     光标原物品原样保留；空间钳按「光标仅持有同产物时贡献容量」算，背包放不下则裁 maxCrafts，绝不丢光标原物。
//   - 防丢物：先扫 main+hotbar+光标 对 outputId 的可用空间，按 floor(space/outputCount) 把 maxCrafts 钳到
//     「产物放得下」；空间不足 → 不消耗原料、无操作（同 MC「背包满则停止合成」）。
//   - 守恒：消耗 maxCrafts × 每非空槽 1；产出 maxCrafts × outputCount；addToAny 入背包（main 同 id → hotbar
//     同 id → 空槽），余数（理论上已被空间钳到 ≤ 光标位）入光标。
function slotShiftLeftCraft(root) {
    if (!root.hotbar || !root.matchedRecipe) return false
    root.craftRev
    const r = root.matchedRecipe()
    if (!r) return true                                          // 无匹配 → 无操作但已处理（不回退）
    const heldId = root.hotbar.heldBlock
    // t268：移除「异物手持 → 无操作」早退。spec「工作台左键拿取物品时 shift+左键 → 一键批量合成（覆盖手持态）」：
    //   光标持有任意物品（含非产物的异物）时 shift+左键结果槽也应批量合。产物入背包（addToAny 只动 main/hotbar、
    //   不碰光标），光标原物品原样保留。空间钳按「光标仅持有同产物时贡献容量」算（下方 space 分支已如此），背包
    //   放不下则 maxCrafts 被裁到「放得下」的量（绝不丢光标原物）。
    // maxCrafts = 每非空原料槽 count 最小值。
    let maxCrafts = -1
    for (let i = 0; i < root.craftSlots.length; ++i) {
        if ((root.craftSlots[i] || 0) !== 0) {
            const c = root.craftCounts[i] || 0
            if (maxCrafts < 0 || c < maxCrafts) maxCrafts = c
        }
    }
    if (maxCrafts <= 0) return true                              // 无原料 → 无操作
    // 钳到产物空间（防丢物）：扫 main+hotbar+光标 对 outputId 的可用容量。
    const cap = root.hotbar.maxStackSize(r.outputId)
    const heldCount = root.hotbar.heldCount
    let space = 0
    if (heldId === r.outputId) space += (cap - heldCount)
    else if (heldId === 0) space += cap
    // t268：heldId 为非产物异物时两分支均不命中 → 光标贡献 0 产物容量（产物只入 main/hotbar，不动光标原物）。
    for (let i = 0; i < root.hotbar.mainCount; ++i) {
        const s = readSlot(root, "main", i)
        if (s.id === 0) space += cap
        else if (s.id === r.outputId) space += (cap - s.count)
    }
    for (let i = 0; i < root.hotbar.slotCount; ++i) {
        const s = readSlot(root, "hotbar", i)
        if (s.id === 0) space += cap
        else if (s.id === r.outputId) space += (cap - s.count)
    }
    const maxCraftsBySpace = Math.floor(space / r.outputCount)
    if (maxCrafts > maxCraftsBySpace) maxCrafts = maxCraftsBySpace
    if (maxCrafts <= 0) return true                              // 无产物空间 → 不消耗原料、无操作
    // 一次性消耗 maxCrafts 次（每非空槽 -maxCrafts；归 0 清 id）。
    for (let i = 0; i < root.craftSlots.length; ++i) {
        if ((root.craftSlots[i] || 0) !== 0) {
            const remain = (root.craftCounts[i] || 0) - maxCrafts
            if (remain <= 0) { root.craftSlots[i] = 0; root.craftCounts[i] = 0 }
            else root.craftCounts[i] = remain
        }
    }
    // 产出 maxCrafts × outputCount 入背包（addToAny：main → hotbar 智能堆叠）；余数（背包满）入光标。
    const total = maxCrafts * r.outputCount
    const remain = root.hotbar.addToAny(r.outputId, total)
    // t268：仅当光标为空或持有同产物时才把余数并入光标；持有非产物异物时不覆盖（防丢光标原物）。
    //   理论上空间钳已保证 heldId 为异物时 remain=0（maxCrafts 按 main+hotbar 容量裁），此为防御纵深。
    if (remain > 0 && (heldId === 0 || heldId === r.outputId)) {
        const prevHeldCount = (heldId === r.outputId) ? heldCount : 0
        root.hotbar.heldBlock = r.outputId
        root.hotbar.heldCount = prevHeldCount + remain
    }
    root.craftRev++
    // progress 统计：批量合成 maxCrafts 次（craftsCount 加 maxCrafts + 成就按产物判一次）。root.progress 由
    //   面板从 Main.qml 注入（InventoryOps .js 无 QML 全局 id 访问权 → 经 root 传，同 hotbar 注入模式）。
    if (root.progress) {
        for (let p = 0; p < maxCrafts; ++p) root.progress.onCraft(r.outputId) // 计 maxCrafts 次合成 + 成就（幂等）
    }
    return true
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
    // t263 双方耐久随各自实例交换（数字键搬运工具保真）。t475 附魔同理随实例交换。
    writeSlot(root, group, srcIdx, dst.id, dst.count, dst.durability, dst.enchants)
    writeSlot(root, "hotbar", hotbarIdx, src.id, src.count, src.durability, src.enchants)
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

    // t263 工具段（cap=1）双击：total=1=单件 → 拾起的就是那把工具，耐久须随实例到光标。在清空前快照首槽
    //   耐久（slots 至少 1 项才会进到此；方块段 durability 恒 0 → 快照值 inert 不影响方块合并语义）。
    const firstSlot = slots.length > 0 ? readSlot(root, slots[0].group, slots[0].index) : null
    const firstDur = firstSlot ? firstSlot.durability : 0
    const firstEnch = firstSlot ? firstSlot.enchants : [0,0,0,0]

    // 清空所有同 id 槽 + 光标（即将重新打包）。
    for (let i = 0; i < slots.length; ++i) {
        writeSlot(root, slots[i].group, slots[i].index, 0, 0, 0)
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
        writeSlot(root, slots[i].group, slots[i].index, targetId, cap, 0, [0,0,0,0])  // 回填满栈（方块段，耐久 / 附魔 inert=0）
    }
    const tail = remain - numFull * cap
    if (tail > 0 && numFull < slots.length) {
        writeSlot(root, slots[numFull].group, slots[numFull].index, targetId, tail, 0, [0,0,0,0])
    }
    if (cursorCount > 0) {
        root.hotbar.heldBlock = targetId
        root.hotbar.heldCount = cursorCount
        // 工具段单件：耐久 / 附魔 = 清空前快照的首槽值（firstDur / firstEnch）。方块段 inert=0；非工具 normalizeDurability 归 0。
        root.hotbar.heldDurability = firstDur
        root.hotbar.setHeldEnchants(firstEnch)
    }
}
