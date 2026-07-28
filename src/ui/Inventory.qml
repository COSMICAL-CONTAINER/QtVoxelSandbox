import QtQuick
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

    // ① 调色板数据：8 实方块（creativeBlocks）+ 3 档镐（creativeTools，t33）+ 扩展空槽（id=0 → 空占位）。
    // 一次性求值的绑定（方块 / 工具集恒定；root.hotbar 由 null→对象 时重新求值）。空槽既是「可滚动」
    // 的内容，也占位示意未来 Phase 1.x 的 ~40 方块扩容（MC 1.0 创造页也是多行大网格）。
    readonly property var paletteModel: root.hotbar
        ? root.hotbar.creativeBlocks().concat(root.hotbar.creativeTools())
                                .concat([0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]) // 8 + 3 + 16 = 27（9 列 × 3 行）
        : []

    // 当前悬停方块的中文名（调色板/hotbar 槽 hover 时更新；§9 override (b) 中文通用词）。
    property string hoveredName: ""

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

    // 半透明遮罩：仅吸收点击（防穿透到背后的游戏/暂停层），**不关闭背包**——用户要求背包只能由
    // E / Esc 关闭（点背包 UI 外部不应关闭）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea { anchors.fill: parent } // 吸收点击（无 onClicked → 不关闭）
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

            // ① 调色板（Flickable 垂直可滚动）。
            Flickable {
                id: paletteFlick
                width: parent.width
                height: root.cellSize * 2 + 8 // 视口约 2 行；内容 3 行 → 可向下滚动一格
                clip: true
                contentWidth: paletteGrid.width
                contentHeight: paletteGrid.height
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

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

                            // 物品图标：方块段 → 等距立方体 Image；工具段（t33 isTool）→ ToolIcon 自绘镐。
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
                                onHoveredChanged: if (hovered) root.hoveredName = root.hotbar.nameForBlock(modelData)
                            }
                            TapHandler {
                                enabled: modelData !== 0
                                // 拾取到光标（创造调色板=无限源，不清减调色板）。方块满栈 64；工具不可堆叠 →
                                // count=1（t33）。setHeldBlock 已对工具段 id 校验合法（isValidItemId 含工具段）。
                                onTapped: {
                                    root.hotbar.heldBlock = modelData
                                    root.hotbar.heldCount = root.hotbar.isTool(modelData) ? 1 : 64
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
                text: "把 hotbar 方块拖到右侧销毁槽可清空该槽"
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
                            // model = 槽内容（QVariantList<方块id>）。触碰 slotRevision 建立 NOTIFY 依赖：setSlotBlock
                            // 改槽内容 → slotsChanged → slotRevision 自增 → 本绑定重算返回新数组 → Repeater 整列重建
                            // （invokable 返回值不被 NOTIFY 跟踪，故用版本号触发；modelData = 该槽方块 id，air=0 空槽）。
                            model: { root.hotbar.slotRevision; return root.hotbar.slotList() }
                            delegate: Item {
                                width: root.slotSize
                                height: root.slotSize
                                Rectangle { anchors.fill: parent; color: "#2f2f2f" } // 井底
                                // 凹陷斜面：顶/左 暗、底/右 亮
                                Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
                                Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
                                Rectangle { color: "#5a5a5a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
                                Rectangle { color: "#5a5a5a"; width: 1; height: parent.height; anchors.right: parent.right }

                                // 拖动源：图标 wrapper（仅非空槽可拖到销毁槽；hbIndex 标识源槽，供 DropArea 取用）。
                                // Drag.Automatic：拖动时图标跟随指针移动、释放后归位；落在 DropArea 内则触发 onDropped。
                                Item {
                                    id: dragIcon
                                    anchors.centerIn: parent
                                    width: 30; height: 30
                                    visible: modelData !== 0
                                    property int hbIndex: index // 自定义属性：被拖源所属 hotbar 槽下标
                                    Drag.active: iconDrag.active
                                    Drag.dragType: Drag.Automatic
                                    Drag.hotSpot.x: width / 2
                                    Drag.hotSpot.y: height / 2
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
                                    DragHandler {
                                        id: iconDrag
                                        target: dragIcon
                                        xAxis.enabled: true; yAxis.enabled: true
                                    }
                                }

                                // 栈数量（t32）：count>1 时右下角显数字（MC 风格：单件不显数）。
                                // countAt 是 Q_INVOKABLE，靠 slotRevision 触碰 model 绑定 → 整列重建时刷新。
                                Text {
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: 3
                                    anchors.bottomMargin: 1
                                    visible: root.hotbar.countAt(index) > 1
                                    text: root.hotbar.countAt(index)
                                    color: "#ffffff"
                                    style: Text.Outline; styleColor: "#000000"
                                    font.pixelSize: 13; font.bold: true
                                }

                                // hover → 状态行中文名；tap → t46 与主栏/生存背包统一的栈操作（拾取/放置/合并/互换）。
                                //   旧版用「创造覆盖」（持物点异 id 槽 → 原物丢弃），用户反馈「hotbar 行不能左键
                                //   交互」——现统一走 resolveClick：持物点异 id 槽 → 互换（原物入手持，不丢失）。
                                //   同时选中该槽（右键放置即用此槽方块）。拖到销毁槽仍走上面的 DragHandler。
                                HoverHandler {
                                    id: slotHover
                                    onHoveredChanged: if (hovered) root.hoveredName = root.hotbar.nameForBlock(modelData)
                                }
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

                // ③ 销毁槽（DropArea：拖入 hotbar 槽内容 → setSlotBlock(src 槽, air=0) 清空）。
                // 自绘原创垃圾桶图标（Canvas 像素图，§9 override (a)）；凹陷斜面 + 暗红井底表「销毁」语义。
                Item {
                    id: destroyWrap
                    width: root.slotSize
                    height: root.slotSize
                    anchors.right: parent.right

                    Rectangle { anchors.fill: parent; color: destroyDrop.containsDrag ? "#3a1a1a" : "#2a1414" }
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

                    // 点击销毁槽 → 丢弃当前手持物（与拖入销毁等效，提供点击路径）。
                    TapHandler { onTapped: root.hotbar.heldBlock = 0 }
                    DropArea {
                        id: destroyDrop
                        anchors.fill: parent
                        // drop.source = 被拖的 dragIcon（持有 hbIndex）；落在销毁槽 → 清空该 hotbar 槽。
                        onDropped: (drop) => {
                            const src = drop.source
                            if (src && src.hbIndex !== undefined)
                                root.hotbar.setSlotBlock(src.hbIndex, 0) // 0 = air → 清空该槽
                        }
                    }
                }
            }
        }
    }
}
