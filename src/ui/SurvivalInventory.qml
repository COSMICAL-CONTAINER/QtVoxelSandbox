import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

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
    // 请求宿主关闭背包（恢复指针锁定 + 焦点回键位层）。
    signal closed()
    // t49：请求宿主把光标手持栈丢弃为实体（拖出面板外释放 / 点遮罩区时；宿主接 player.dropHeldCursor）。
    signal discardHeldRequested()

    // ── 尺寸常量（集中一处便于对齐）──
    readonly property int slotSize: 40        // 统一槽尺寸（主栏 / hotbar / 合成 / 护甲同尺寸，贴近 1.0）
    readonly property int mainCols: 9
    readonly property int mainRows: 3
    readonly property int armorCount: 4

    // 主栏 / 合成格的本地物品栈存储（真实物品系统 / 合成配方解析属 Phase 1.1；本屏先支持点击拾取/放置，
    // 把「物品在背包内移动」核心交互打通——与 hotbar 槽共享同一 hotbar VM 的 heldBlock/heldCount 光标手持栈）。
    // air=0=空栈。t32：栈数量平行存于 mainCounts/craftCounts（与 hotbar VM 的 ItemStack 同模型）。
    // 数组元素改写不触发 QML 绑定，故配 mainRev/craftRev 版本号让 Image source / count 重算。
    // t49：主栏初始全空（spec point 2「空背包起」；删 [test] 草/泥土预置数据）。
    property var mainSlots: [0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0]
    property var mainCounts:[0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0]
    property int mainRev: 0
    property var craftSlots: [0,0,0,0] // 2×2 合成格（占位；配方解析属 Phase 1.1，结果槽暂不产出）
    property var craftCounts:[0,0,0,0] // 平行数量
    property int craftRev: 0

    // t38 生存左键整组栈操作的核心算法。给定目标槽当前 (curId, curCount) 与 hotbar VM 的手持栈
    // (heldBlock, heldCount)，返回应写入的 {slotId, slotCount, heldId, heldCount}；返回 null = 无操作。
    // 4 种 case（spec「左键 → 整组到 heldStack；点空槽放整组；点同 id 合并至 64 余留 held；点异 id 互换」）：
    //   A 手持空 + 槽非空：拾取整栈（槽清空、held ← 该栈）。
    //   B 手持非空 + 槽空：放置整栈（槽 ← held、held 清空）。
    //   C 手持非空 + 同 id：合并至 maxStackSize(id)（方块 64 / 工具段 1），余数留 held；槽已满则无操作。
    //   D 手持非空 + 异 id：互换（槽 ↔ held）。
    // 主栏 / 合成格 / hotbar 槽三类槽位共用此算法：调用方据返回值写入对应存储
    // （主栏 / 合成格 → 本地数组 + bump 对应 rev；hotbar 槽 → 走 hotbar.setStack）。手持栈状态由
    // hotbar VM 单一持有（heldBlock/heldCount），创造 / 生存共享，跨面板一致（PLAN §2：VM 单一权威）。
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

    // t49 右键语义（MC 1.0）：空手→拾取一半（floor(count/2)，单件特例取 1）；持物→放 1 个（空槽开新栈 /
    // 同 id 未满 +1；异 id 槽 / 已满无操作，**不互换**）。返回与 resolveClick 同形的 {slotId, slotCount,
    // heldId, heldCount}；null = 无操作。主栏 / 合成格 / hotbar 槽三类共用（与 Inventory.qml 一致）。
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

    // 半透明遮罩：仅吸收点击（防穿透到背后游戏层），**不关闭背包**——用户要求背包只能 E / Esc 关闭。
    // t49：手持物时点遮罩区（面板外）→ 整栈丢弃为实体（同 Q 丢弃）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea {
            anchors.fill: parent
            onClicked: {
                // 拖出丢弃（spec point 5）：手持物点背包外 → 请求宿主丢弃；空手仅吸收点击。
                if (root.hotbar && root.hotbar.heldBlock !== 0) root.discardHeldRequested()
            }
        }
    }

    // 面板：深色圆角，居中。尺寸由内容（标题 + 顶部区 + 主栏 + hotbar）精确推出。
    Rectangle {
        id: panel
        width: root.mainCols * root.slotSize + 32   // 360 + 2×16 边距 = 392
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
                                // t50 材料段（木棒）：MaterialIcon 自绘（§9a 原创，非 MC 资产）。
                                MaterialIcon {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return root.hotbar.isMaterial(root.craftSlots[index] || 0) }
                                    materialId: { root.craftRev; return root.craftSlots[index] || 0 }
                                }
                            }
                            // 栈数量（t32）：count>1 时右下角显数字。触碰 craftRev 刷新（数组突变靠版本号触发）。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: { root.craftRev; return (root.craftCounts[index] || 0) > 1 }
                                text: { root.craftRev; return root.craftCounts[index] || 0 }
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // 点击拾取/放置/合并/互换（t38 栈感知；左键走 resolveClick；t49 右键走 resolveRightClick）。
                            // 配方解析属 Phase 1.1，结果槽暂不产出。返回 null（空点空 / 同 id 槽已满）时不写、不 bump rev。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    const r = root.resolveClick(root.craftSlots[index] || 0, root.craftCounts[index] || 0)
                                    if (!r) return
                                    root.craftSlots[index] = r.slotId
                                    root.craftCounts[index] = r.slotCount
                                    root.craftRev++
                                    root.hotbar.heldBlock = r.heldId
                                    root.hotbar.heldCount = r.heldCount
                                }
                            }
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
                        property int outId: { root.craftRev; const r = root.matchedRecipe(); return (r && r.outputId) || 0 }
                        property int outCount: { root.craftRev; const r = root.matchedRecipe(); return (r && r.outputCount) || 0 }
                        visible: outId !== 0
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
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: parent.outCount > 1
                            text: parent.outCount
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                    }
                    // 左/右键均触发合成（MC：结果槽左键取一批）。
                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onTapped: root.craftOne()
                    }
                }

                // 角色预览（护甲右侧，左半区）：自绘人形剪影占位（真实 3D 玩家模型属 Phase 1.1）。80 宽 × 160 高；
                // 内部坐标以左上为原点居中绘制（头 / 躯干 / 双臂 / 双腿）。
                Item {
                    x: root.slotSize + 6
                    y: 0
                    width: 80
                    height: parent.height
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            const ctx = getContext("2d"); ctx.reset()
                            ctx.imageSmoothingEnabled = false
                            const skin = "#caa472"   // 肤色（占位）
                            const shirt = "#3a6a9a"  // 衣服（占位）
                            const pants = "#3a3a5a"  // 裤（占位）
                            ctx.fillStyle = skin
                            ctx.fillRect(32, 24, 16, 16)               // 头
                            ctx.fillStyle = shirt
                            ctx.fillRect(30, 40, 20, 28)               // 躯干
                            ctx.fillRect(20, 40, 8, 26)                // 左臂
                            ctx.fillRect(52, 40, 8, 26)                // 右臂
                            ctx.fillStyle = pants
                            ctx.fillRect(30, 68, 8, 32)                // 左腿
                            ctx.fillRect(42, 68, 8, 32)                // 右腿
                        }
                    }
                }

                // 4 护甲槽（最左，纵向：头 / 胸 / 腿 / 脚）：占位自绘图标（Phase 1.1 装备逻辑）。
                // 据槽 index 画 头盔 / 胸甲 / 护腿 / 靴 的暗灰金属像素图（§9 override (a) 原创，非 MC 资产）。
                Column {
                    x: 0   // 最左（人物装备栏在左，对齐 MC 1.0）
                    y: 0
                    spacing: 0
                    Repeater {
                        model: root.armorCount
                        delegate: Item {
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                            Canvas {
                                anchors.centerIn: parent
                                width: 26; height: 26
                                onPaint: {
                                    const ctx = getContext("2d"); ctx.reset()
                                    ctx.imageSmoothingEnabled = false
                                    const metal = "#9aa0a6"   // 暗灰金属（占位）
                                    const gap = "#262b30"     // 镂空用井底色
                                    ctx.fillStyle = metal
                                    if (index === 0) {                  // 头盔
                                        ctx.fillRect(5, 5, 16, 3)       // 帽檐
                                        ctx.fillRect(7, 8, 12, 9)       // 头罩
                                        ctx.fillStyle = gap
                                        ctx.fillRect(9, 11, 8, 3)       // 面罩缝（挖空）
                                    } else if (index === 1) {           // 胸甲
                                        ctx.fillRect(6, 5, 14, 4)       // 肩
                                        ctx.fillRect(7, 9, 12, 13)      // 躯干
                                        ctx.fillStyle = gap
                                        ctx.fillRect(12, 10, 2, 10)     // 中线
                                    } else if (index === 2) {           // 护腿
                                        ctx.fillRect(7, 5, 12, 4)       // 腰
                                        ctx.fillRect(7, 9, 4, 13)       // 左腿
                                        ctx.fillRect(15, 9, 4, 13)      // 右腿
                                    } else {                            // 靴
                                        ctx.fillRect(6, 13, 6, 7)       // 左靴筒
                                        ctx.fillRect(14, 13, 6, 7)      // 右靴筒
                                        ctx.fillRect(4, 18, 10, 2)      // 左鞋底
                                        ctx.fillRect(12, 18, 10, 2)     // 右鞋底
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ③ 3×9 主栏（27 槽）：点击拾取/放置方块（本地 mainSlots 存储；与 hotbar/合成格共享光标手持物）。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    model: root.mainCols * root.mainRows   // 27
                    delegate: Item {
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent }
                        Item {
                            anchors.centerIn: parent
                            width: 30; height: 30
                            visible: { root.mainRev; return (root.mainSlots[index] || 0) !== 0 }
                            Image {
                                anchors.fill: parent
                                visible: { root.mainRev; return !root.hotbar.isTool(root.mainSlots[index] || 0)
                                                          && !root.hotbar.isMaterial(root.mainSlots[index] || 0) }
                                source: { root.mainRev; return root.hotbar.iconSourceForBlock(root.mainSlots[index] || 0) }
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            ToolIcon {
                                anchors.fill: parent
                                visible: { root.mainRev; return root.hotbar.isTool(root.mainSlots[index] || 0) }
                                tier: { root.mainRev; return root.hotbar.toolTier(root.mainSlots[index] || 0) }
                            }
                            MaterialIcon {
                                anchors.fill: parent
                                visible: { root.mainRev; return root.hotbar.isMaterial(root.mainSlots[index] || 0) }
                                materialId: { root.mainRev; return root.mainSlots[index] || 0 }
                            }
                        }
                        // 栈数量（t32）：count>1 时右下角显数字。触碰 mainRev 刷新（数组突变靠版本号触发）。
                        Text {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: 3; anchors.bottomMargin: 1
                            visible: { root.mainRev; return (root.mainCounts[index] || 0) > 1 }
                            text: { root.mainRev; return root.mainCounts[index] || 0 }
                            color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                            font.pixelSize: 13; font.bold: true
                        }
                        // t38 生存左键整组操作（拾取 / 放置 / 合并 / 互换）：统一走 resolveClick。
                        // t49 右键 = 拿一半 / 放一个（resolveRightClick）。同 id 合并至 maxStack（方块 64 / 工具 1），
                        // 余数留 held；异 id 互换（左键）/ 不操作（右键）；空点空无操作。
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                const r = root.resolveClick(root.mainSlots[index] || 0, root.mainCounts[index] || 0)
                                if (!r) return
                                root.mainSlots[index] = r.slotId
                                root.mainCounts[index] = r.slotCount
                                root.mainRev++
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                const r = root.resolveRightClick(root.mainSlots[index] || 0, root.mainCounts[index] || 0)
                                if (!r) return
                                root.mainSlots[index] = r.slotId
                                root.mainCounts[index] = r.slotCount
                                root.mainRev++
                                root.hotbar.heldBlock = r.heldId
                                root.hotbar.heldCount = r.heldCount
                            }
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
                        // 同创造背包 hotbar 栏：触碰 slotRevision 建 NOTIFY 依赖 → setSlotBlock 改槽内容时整列重建
                        // （invokable 返回值不被 NOTIFY 跟踪，故用版本号触发；modelData = 该槽方块 id，air=0 空槽）。
                        model: { root.hotbar.slotRevision; return root.hotbar.slotList() }
                        delegate: Item {
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent }
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
                            // 栈数量（t32）：count>1 时右下角显数字。countAt 是 Q_INVOKABLE，靠 slotRevision
                            // 触碰 model 绑定 → 整列重建时刷新。
                            Text {
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.rightMargin: 3; anchors.bottomMargin: 1
                                visible: root.hotbar.countAt(index) > 1
                                text: root.hotbar.countAt(index)
                                color: "#ffffff"; style: Text.Outline; styleColor: "#000000"
                                font.pixelSize: 13; font.bold: true
                            }
                            // tap → t38 生存左键整组操作（拾取 / 放置 / 合并 / 互换）：统一走 resolveClick。
                            // hotbar 槽内容写经 hotbar.setStack（VM 单一权威；同 id 合并至 maxStack、异 id 互换）。
                            // t49：背包内点 hotbar 行**不切真实选中**（删 selectedSlot 赋值；真实选中仅由游戏内
                            // 1–9 / 滚轮改）；右键 = 拿一半 / 放一个（resolveRightClick）。
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: {
                                    const r = root.resolveClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index))
                                    if (r) {
                                        root.hotbar.setStack(index, r.slotId, r.slotCount)
                                        root.hotbar.heldBlock = r.heldId
                                        root.hotbar.heldCount = r.heldCount
                                    }
                                }
                            }
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
}
