import QtQuick

// 生存模式物品栏 1.0（t24）：E 键开关（仅 Survival 模式 —— 宿主 Main.qml 已按模式分流：Survival
// 开本屏、Creative 开 t23 创造背包、Spectator E 无反应）。
//
// 贴近 MC 1.0 生存背包布局（spec 验收项）：
//   ① 左上 2×2 合成格 + 箭头 + 结果槽（合成功能属 Phase 1.1，本屏为**占位空槽**，spec 明确标注）；
//   ② 右上 4 护甲槽（头 / 胸 / 腿 / 脚，纵向）+ 角色预览（自绘人形剪影占位；真实装备 / 3D 模型属 Phase 1.1）；
//   ③ 下部 3×9=27 主栏（物品栈 / 采集属 Phase 1.1，本屏为**占位空槽**）；
//   ④ 最底 9 槽 hotbar 栏（同步游戏内 hotbar：读 hotbar VM，点击切换选中槽 + 选中选框）。
//
// 本组件只做**呈现 + hotbar 选择转发**：方块集 / 图标 / 中文名 / 槽位数据全部经注入的 hotbar VM
// （ViewModel 读 BlockRegistry，PLAN §2 分层：UI 不另持方块表副本）。合成 / 护甲 / 主栏为占位
// （Phase 1.1 真实逻辑），槽位布局存在且不崩即满足本任务验收。全部槽框 / 护甲图 / 角色预览本项目
// 自绘原创（InvSlot 凹陷槽 + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。零 MC 专有名词（§9）。
//
// 宿主负责指针态：背包打开时已 release（光标可见，可点 hotbar 槽）；关闭（closed 信号）→ 宿主恢复 grab。

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

    // 半透明遮罩：点击空白处 → 关闭（同创造背包交互）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea { anchors.fill: parent; onClicked: root.closed() }
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

            // ① 顶部区（合成 + 护甲 + 角色预览）。高度由护甲 4 槽纵向（4×slotSize）决定。
            Item {
                width: root.mainCols * root.slotSize   // 360
                height: root.armorCount * root.slotSize // 160

                // 2×2 合成格（左上）：占位空槽（合成功能属 Phase 1.1）。
                Grid {
                    x: 0; y: 0
                    columns: 2; spacing: 0
                    Repeater {
                        model: 4
                        delegate: Item {
                            width: root.slotSize; height: root.slotSize
                            InvSlot { anchors.fill: parent; wellColor: "#262b30" } // 更暗井底表「占位 / 无内容」
                        }
                    }
                }

                // 合成箭头（指向结果槽）：自绘像素图（§9 override (a)）。纵向居中于合成格中线（y≈40）。
                Canvas {
                    x: 86; y: 30
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

                // 结果槽（合成输出）：占位空槽（Phase 1.1）。纵向居中于合成格中线。
                Item {
                    x: 116; y: 20
                    width: root.slotSize; height: root.slotSize
                    InvSlot { anchors.fill: parent; wellColor: "#262b30" }
                }

                // 角色预览（右上）：自绘人形剪影占位（真实 3D 玩家模型属 Phase 1.1）。80 宽 × 160 高，
                // 贴右侧；内部坐标以左上为原点居中绘制（头 / 躯干 / 双臂 / 双腿）。
                Item {
                    x: parent.width - 80
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

                // 4 护甲槽（角色预览左侧，纵向：头 / 胸 / 腿 / 脚）：占位自绘图标（Phase 1.1 装备逻辑）。
                // 据槽 index 画 头盔 / 胸甲 / 护腿 / 靴 的暗灰金属像素图（§9 override (a) 原创，非 MC 资产）。
                Column {
                    x: parent.width - 80 - 8 - root.slotSize   // 预览左 8px + 1 槽宽
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

            // ③ 3×9 主栏（27 槽）：占位空槽（物品栈 / 采集属 Phase 1.1）。
            Grid {
                width: root.mainCols * root.slotSize
                height: root.mainRows * root.slotSize
                columns: root.mainCols; spacing: 0
                Repeater {
                    model: root.mainCols * root.mainRows   // 27
                    delegate: Item {
                        width: root.slotSize; height: root.slotSize
                        InvSlot { anchors.fill: parent }
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
                            Image {
                                anchors.centerIn: parent
                                width: 30; height: 30
                                visible: modelData !== 0
                                source: root.hotbar.iconSourceForBlock(modelData)
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                            }
                            // tap → 选中该槽（与游戏内 1–9 / 滚轮同效果；选中后右键放置 = 该槽方块）。
                            TapHandler { onTapped: root.hotbar.selectedSlot = index }
                        }
                    }
                }

                // 选中槽选框（raised bevel：顶 / 左 亮、底 / 右 暗 → 凸起观感），随 selectedSlot 位移。
                // 单独 overlay（不放进 Repeater）→ 选中态唯一；Behavior 让点击切换有平滑滑动感（同游戏内 hotbar）。
                // hotbar 在构造期可能瞬时为 null，故三元守 null（避免 QML 绑定求值期 null 解引用）。
                Item {
                    x: (root.hotbar ? root.hotbar.selectedSlot : 0) * root.slotSize - 1
                    y: -1
                    width: root.slotSize + 2; height: root.slotSize + 2
                    Behavior on x { NumberAnimation { duration: 70; easing.type: Easing.OutQuad } }
                    Rectangle { color: "#e8e8e8"; width: parent.width; height: 2; anchors.top: parent.top }
                    Rectangle { color: "#e8e8e8"; width: 2; height: parent.height; anchors.left: parent.left }
                    Rectangle { color: "#3a3a3a"; width: parent.width; height: 2; anchors.bottom: parent.bottom }
                    Rectangle { color: "#3a3a3a"; width: 2; height: parent.height; anchors.right: parent.right }
                }
            }
        }
    }
}
