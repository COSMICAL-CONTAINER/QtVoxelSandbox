import QtQuick

// 创造风格背包（t18）：E 键开关，列出全部 8 方块网格；点击方块 → 装入 hotbar 当前选中槽。
// 本文件只做**呈现 + 点击转发**：方块集 / 图标映射 / 槽位改写全部经注入的 hotbar VM
// （ViewModel 读 BlockRegistry，PLAN §2 分层：UI 不另持方块表副本）。零 MC 名词/资产（§9）。
//
// 区隔（PLAN §9）：不照搬 MC 的「物品栏 + 玩家模型预览 + 标签页」整组布局；只做单页方块网格，
// 深色面板 + 圆角槽 + 绿系强调色（对齐既有 hotbar/HUD 配色）。
//
// 宿主（Main.qml）负责指针态：背包打开时已 release（光标可见，可点格子）；点击格子只调
// hotbar.setSlotBlock，不直接改指针态。关闭（Esc/E/点遮罩）发 closed() → 宿主恢复 grab。
Item {
    id: root

    // 宿主注入：hotbar 视图模型（提供 creativeBlocks / iconSourceForBlock / nameForBlock /
    // setSlotBlock / selectedSlot）。
    property Hotbar hotbar
    // 请求宿主关闭背包（恢复指针锁定 + 焦点回键位层）。
    signal closed()

    // 半透明遮罩：点击遮罩任意空白处 → 关闭（类暂停叠层的「点外恢复」交互）。
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        MouseArea {
            anchors.fill: parent
            onClicked: root.closed()
        }
    }

    // 面板：深色圆角，居中。宽高按 4 列网格 + 标题区预留。
    Rectangle {
        id: panel
        width: 380
        height: 300
        anchors.centerIn: parent
        radius: 14
        color: "#1b1f24"
        border.color: "#3a444f"
        border.width: 1

        Column {
            anchors.centerIn: parent
            spacing: 14

            Text {
                text: "Inventory"
                color: "#eaf2ea"
                font.pixelSize: 22
                font.bold: true
                anchors.horizontalCenter: parent.horizontalCenter
            }
            // 目标槽提示：点格子会装入 hotbar 当前选中槽（#selectedSlot+1）。selectedSlot 变化即刷新。
            Text {
                text: "click a block to equip into hotbar slot #" + (root.hotbar.selectedSlot + 1)
                color: "#9fb0c0"
                font.pixelSize: 12
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: "[E] / [Esc] close"
                color: "#7fae7f"
                font.pixelSize: 11
                anchors.horizontalCenter: parent.horizontalCenter
            }

            // 8 方块网格（4 列 × 2 行）。model = hotbar.creativeBlocks（方块 id 列表）；
            // delegate 按 id 取图标/名，点击 → setSlotBlock(当前选中槽, id)。
            Grid {
                columns: 4
                spacing: 10
                anchors.horizontalCenter: parent.horizontalCenter

                Repeater {
                    model: root.hotbar.creativeBlocks()
                    delegate: Item {
                        width: 72
                        height: 72

                        Rectangle {
                            anchors.fill: parent
                            radius: 8
                            color: cellArea.containsPress ? "#2c3540"
                                 : cellArea.containsMouse ? "#262d36" : "#222831"
                            border.color: cellArea.containsMouse ? "#7fe57f" : "#3a444f"
                            border.width: cellArea.containsMouse ? 2 : 1
                            Behavior on border.width { NumberAnimation { duration: 70 } }
                        }

                        // 方块图标（等距立方体，与 hotbar 完全一致的源；统一 PreserveAspectFit 进框）。
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.verticalCenterOffset: -6
                            width: 46
                            height: 46
                            source: root.hotbar.iconSourceForBlock(modelData)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 4
                            text: root.hotbar.nameForBlock(modelData)
                            color: "#9fb0c0"
                            font.pixelSize: 10
                        }

                        MouseArea {
                            id: cellArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.hotbar.setSlotBlock(root.hotbar.selectedSlot, modelData)
                        }
                    }
                }
            }
        }
    }
}
