import QtQuick

// 主菜单（t17）：启动时全屏首显（Main.qml 初始 appState="menu"）。提供「开始游戏 / 退出」入口。
//
// 区隔要求（PLAN §9）：不照搬 MC 主菜单的平移背景图 + 居中窄灰按钮列 + logo 美术；
// 采用纵向深色渐变背景 + 大字距标题 + 圆角大按钮（主操作绿色强调），整体冷调氛围。
//
// 仅依赖 QtQuick（无特殊模块），故在 Main.qml 直接实例化（非 Loader 隔离）。
// 通信：发 startRequested / quitRequested 信号，由宿主 Window 连接（解耦菜单与 app 状态机）。
Item {
    id: root

    signal startRequested()
    signal quitRequested()

    // 全屏深色纵向渐变背景（区别于游戏的明亮天空 clearColor，营造菜单氛围）。
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#16202b" }
            GradientStop { position: 0.6; color: "#0d141c" }
            GradientStop { position: 1.0; color: "#070a0e" }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 30

        // 标题区：大字距标题 + 副标题 + 强调色细线（极简装饰，非 MC 资产）。
        Column {
            spacing: 10
            anchors.horizontalCenter: parent.horizontalCenter
            Text {
                text: "VOXEL SANDBOX"
                color: "#eaf2ea"
                font.pixelSize: 56
                font.bold: true
                font.letterSpacing: 6
                style: Text.Outline
                styleColor: "#000000"
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Text {
                text: "a creative voxel world"
                color: "#7fae7f"
                font.pixelSize: 16
                font.letterSpacing: 2
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Rectangle {
                width: 120; height: 2; radius: 1
                color: "#3a6a3a"
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        // 按钮区：主操作「开始」绿色强调；次操作「退出」中性。圆角 + 悬停/按下反馈。
        Column {
            spacing: 16
            anchors.horizontalCenter: parent.horizontalCenter

            // 开始游戏
            Rectangle {
                width: 240; height: 52; radius: 10
                color: startArea.containsPress ? "#335c33"
                      : startArea.containsMouse ? "#4f8a4f" : "#3a6a3a"
                border.color: "#7fe57f"
                border.width: startArea.containsMouse ? 2 : 1
                Behavior on border.width { NumberAnimation { duration: 80 } }
                Text {
                    anchors.centerIn: parent
                    text: "Start Game"
                    color: "#eaf6ea"; font.pixelSize: 18; font.bold: true
                }
                MouseArea {
                    id: startArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.startRequested()
                }
            }

            // 退出游戏
            Rectangle {
                width: 240; height: 52; radius: 10
                color: quitArea.containsPress ? "#272f37"
                      : quitArea.containsMouse ? "#333c47" : "#222a32"
                border.color: "#3a444f"
                border.width: quitArea.containsMouse ? 2 : 1
                Behavior on border.width { NumberAnimation { duration: 80 } }
                Text {
                    anchors.centerIn: parent
                    text: "Quit"
                    color: "#cdd6dd"; font.pixelSize: 18
                }
                MouseArea {
                    id: quitArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.quitRequested()
                }
            }
        }
    }

    // 底部版本提示
    Text {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 18
        color: "#55606a"
        font.pixelSize: 12
        text: "Phase 1.0 — creative sandbox spike"
    }
}
