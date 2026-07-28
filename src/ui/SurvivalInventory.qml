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

    // ── 尺寸常量（集中一处便于对齐）──
    readonly property int slotSize: 40        // 统一槽尺寸（主栏 / hotbar / 合成 / 护甲同尺寸，贴近 1.0）
    readonly property int mainCols: 9
    readonly property int mainRows: 3
    readonly property int armorCount: 4

    // 主栏 / 合成格的本地物品栈存储（真实物品系统 / 合成配方解析属 Phase 1.1；本屏先支持点击拾取/放置，
    // 把「物品在背包内移动」核心交互打通——与 hotbar 槽共享同一 hotbar VM 的 heldBlock/heldCount 光标手持栈）。
    // air=0=空栈。t32：栈数量平行存于 mainCounts/craftCounts（与 hotbar VM 的 ItemStack 同模型）。
    // 数组元素改写不触发 QML 绑定，故配 mainRev/craftRev 版本号让 Image source / count 重算。
    property var mainSlots: [1,2,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0] // [test] 槽0=草(id1)×5, 槽1=泥土(id2)×3，供测试移动/互换
    property var mainCounts:[5,3,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0] // 平行数量
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

    // 半透明遮罩：仅吸收点击（防穿透到背后游戏层），**不关闭背包**——用户要求背包只能 E / Esc 关闭。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea { anchors.fill: parent } // 吸收点击（无 onClicked → 不关闭）
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
                                    visible: { root.craftRev; return !root.hotbar.isTool(root.craftSlots[index] || 0) }
                                    source: { root.craftRev; return root.hotbar.iconSourceForBlock(root.craftSlots[index] || 0) }
                                    fillMode: Image.PreserveAspectFit; smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: { root.craftRev; return root.hotbar.isTool(root.craftSlots[index] || 0) }
                                    tier: { root.craftRev; return root.hotbar.toolTier(root.craftSlots[index] || 0) }
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
                            // 点击拾取/放置/合并/互换（t38 栈感知；统一走 resolveClick；配方解析属 Phase 1.1，
                            // 结果槽暂不产出）。返回 null（空点空 / 同 id 槽已满）时不写、不 bump rev。
                            TapHandler {
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

                // 结果槽（合成输出，最右）：占位空槽（Phase 1.1）。居中于右侧合成区（y 居中 160 高）。
                Item {
                    x: parent.width - root.slotSize; y: 60
                    width: root.slotSize; height: root.slotSize
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }
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
                                visible: { root.mainRev; return !root.hotbar.isTool(root.mainSlots[index] || 0) }
                                source: { root.mainRev; return root.hotbar.iconSourceForBlock(root.mainSlots[index] || 0) }
                                fillMode: Image.PreserveAspectFit; smooth: true
                            }
                            ToolIcon {
                                anchors.fill: parent
                                visible: { root.mainRev; return root.hotbar.isTool(root.mainSlots[index] || 0) }
                                tier: { root.mainRev; return root.hotbar.toolTier(root.mainSlots[index] || 0) }
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
                        // 同 id 槽合并至 maxStack（方块 64 / 工具 1），余数留 held；异 id 互换；空点空无操作。
                        TapHandler {
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
                                    visible: !root.hotbar.isTool(modelData)
                                    source: root.hotbar.iconSourceForBlock(modelData)
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                }
                                ToolIcon {
                                    anchors.fill: parent
                                    visible: root.hotbar.isTool(modelData)
                                    tier: root.hotbar.toolTier(modelData)
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
                            // 并选中该槽 = 游戏内 1–9 / 滚轮等效（与主栏 / 合成格的差异：hotbar 槽有「选中」语义）。
                            TapHandler {
                                onTapped: {
                                    const r = root.resolveClick(root.hotbar.blockIdAt(index), root.hotbar.countAt(index))
                                    if (r) {
                                        root.hotbar.setStack(index, r.slotId, r.slotCount)
                                        root.hotbar.heldBlock = r.heldId
                                        root.hotbar.heldCount = r.heldCount
                                    }
                                    root.hotbar.selectedSlot = index
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
