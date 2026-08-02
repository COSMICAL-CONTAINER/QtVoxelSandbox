import QtQuick
import QtQuick.Controls // ScrollBar（同 Inventory.qml t127，Flickable 滚动指示）
import VoxelSandbox

// 世界列表 / 新建世界（t176）：从主菜单「单人模式」进入。列出已有存档 + 新建（输入名字 + 种子，默认 42）
// + 进入 / 删除 / 返回。通信：playRequested(file,name) / backRequested() 由宿主 Window 连接。
//
// 区隔要求（PLAN §9）：不照搬 MC 世界列表的灰底平铺缩略图 + 泥土按钮；沿用 MainMenu 的深色纵向渐变 +
// 圆角按钮风格，与主菜单视觉统一。世界数据经 WorldStore（ViewModel，World 层）取，本组件只做呈现 + 输入。
Item {
    id: root

    property WorldStore store
    // 已选世界（file 名，相对 saves/）+ 其显示名。null = 未选。
    property string selectedFile: ""
    property string selectedName: ""

    signal playRequested(string file, string name)
    signal backRequested()

    // 全屏深色纵向渐变背景（与 MainMenu 一致，营造菜单氛围）。
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#16202b" }
            GradientStop { position: 0.6; color: "#0d141c" }
            GradientStop { position: 1.0; color: "#070a0e" }
        }
    }

    // 列表数据：从 store.worldList() 拉取（QVariantList<QVariantMap>）。visible 变 true / 新建 / 删除后刷新。
    ListModel { id: worldsModel }
    function refresh() {
        if (!store) return
        worldsModel.clear()
        const list = store.worldList()
        for (let i = 0; i < list.length; ++i) worldsModel.append(list[i])
        // 刷新后若已选世界已不在（被删）→ 清选择。
        let stillThere = false
        for (let j = 0; j < worldsModel.count; ++j)
            if (worldsModel.get(j).file === root.selectedFile) { stillThere = true; break }
        if (!stillThere) { root.selectedFile = ""; root.selectedName = "" }
    }
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: if (visible) refresh()

    Column {
        anchors.fill: parent
        anchors.margins: 40
        spacing: 18

        // 标题 + 返回。
        Row {
            width: parent.width
            spacing: 16
            Text {
                text: "单人模式 — 世界列表"
                color: "#eaf2ea"; font.pixelSize: 32; font.bold: true; font.letterSpacing: 2
                style: Text.Outline; styleColor: "#000000"
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: parent.width - 400; height: 1 } // 弹性间隔
            Rectangle {
                width: 120; height: 36; radius: 8
                color: backArea.containsMouse ? "#333c47" : "#222a32"
                border.color: "#3a444f"; border.width: 1
                anchors.verticalCenter: parent.verticalCenter
                Text { anchors.centerIn: parent; text: "返回"; color: "#cdd6dd"; font.pixelSize: 14 }
                MouseArea {
                    id: backArea; anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor; onClicked: root.backRequested()
                }
            }
        }

        // 主体：左世界列表 + 右新建 / 详情面板。
        Row {
            width: parent.width
            height: parent.height - 80
            spacing: 20

            // 左：世界列表（可滚动）。
            Rectangle {
                width: parent.width * 0.6 - 10; height: parent.height
                color: "#0e151d"; border.color: "#243040"; border.width: 1; radius: 8
                Flickable {
                    anchors.fill: parent; anchors.margins: 8
                    clip: true; contentHeight: worldsCol.height
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    Column {
                        id: worldsCol
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: worldsModel
                            delegate: Rectangle {
                                width: worldsCol.width; height: 56; radius: 6
                                color: root.selectedFile === model.file ? "#243a4a"
                                     : (itemArea.containsMouse ? "#1a2733" : "#141d27")
                                border.color: root.selectedFile === model.file ? "#4f9fd0" : "#243040"
                                border.width: root.selectedFile === model.file ? 2 : 1
                                MouseArea {
                                    id: itemArea; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.selectedFile = model.file; root.selectedName = model.name }
                                }
                                Column {
                                    anchors.left: parent.left; anchors.leftMargin: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 4
                                    Text {
                                        text: model.name
                                        color: "#eaf2ea"; font.pixelSize: 16; font.bold: true
                                    }
                                    Text {
                                        // 种子 + 上次游玩时间（ playedAt=0 显示「未游玩」）。
                                        text: "种子: " + model.seed + "    " +
                                              (model.playedAt > 0
                                                 ? "上次游玩: " + new Date(model.playedAt).toLocaleString()
                                                 : "未游玩")
                                        color: "#8aa0b0"; font.pixelSize: 12
                                    }
                                }
                            }
                        }
                        Text {
                            visible: worldsModel.count === 0
                            text: "暂无世界 — 在右侧新建一个"
                            color: "#6a7a8a"; font.pixelSize: 14
                            anchors.horizontalCenter: parent.horizontalCenter
                            topPadding: 30
                        }
                    }
                }
            }

            // 右：新建世界 + 选中世界操作。
            Column {
                width: parent.width * 0.4 - 10; height: parent.height
                spacing: 16

                // 新建世界面板。height 跟随内容 implicitHeight（上下 margins 各 16）——
                // 固定 200 装不下 5 项（标题 + 名称 + 种子 + 按钮 + spacing）会把
                // 「创建并进入世界」挤出底边框；按内容自适应也防 DPI/字体变粗后复发。
                Rectangle {
                    width: parent.width; radius: 8
                    color: "#0e151d"; border.color: "#243040"; border.width: 1
                    height: createCol.implicitHeight + 32
                    Column {
                        id: createCol
                        anchors.fill: parent; anchors.margins: 16; spacing: 12
                        Text { text: "新建世界"; color: "#7fae7f"; font.pixelSize: 16; font.bold: true }
                        Column {
                            width: parent.width; spacing: 6
                            Text { text: "世界名称"; color: "#8aa0b0"; font.pixelSize: 12 }
                            Rectangle {
                                width: parent.width; height: 34; radius: 4
                                color: "#0a1018"; border.color: "#2a3848"; border.width: 1
                                TextInput {
                                    id: nameInput
                                    anchors.fill: parent; anchors.margins: 8
                                    color: "#eaf2ea"; font.pixelSize: 14
                                    text: "新世界"; selectByMouse: true; clip: true
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                        Column {
                            width: parent.width; spacing: 6
                            Text { text: "种子（默认 42）"; color: "#8aa0b0"; font.pixelSize: 12 }
                            Rectangle {
                                width: parent.width; height: 34; radius: 4
                                color: "#0a1018"; border.color: "#2a3848"; border.width: 1
                                TextInput {
                                    id: seedInput
                                    anchors.fill: parent; anchors.margins: 8
                                    color: "#eaf2ea"; font.pixelSize: 14
                                    text: "42"; selectByMouse: true; clip: true
                                    verticalAlignment: Text.AlignVCenter
                                    // 限制为整数（允许负号）；非法或空 → 创建时回退 42。
                                    validator: IntValidator { bottom: -2147483647; top: 2147483647 }
                                }
                            }
                        }
                        Rectangle {
                            width: parent.width; height: 38; radius: 6
                            color: createArea.containsPress ? "#335c33"
                                 : createArea.containsMouse ? "#4f8a4f" : "#3a6a3a"
                            border.color: "#7fe57f"; border.width: 1
                            MouseArea {
                                id: createArea; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (!store) return
                                    const seedText = seedInput.text.trim()
                                    // 空字符串 IntValidator 仍接受但 parseInt 得 NaN → 回退 42（spec 默认）。
                                    let seed = parseInt(seedText, 10)
                                    if (isNaN(seed)) seed = 42
                                    const name = nameInput.text.trim().length > 0 ? nameInput.text.trim() : "新世界"
                                    const file = store.createWorld(name, seed)
                                    if (file.length > 0) {
                                        root.playRequested(file, name) // 新建即进入（机制等价 MC「创建世界并游玩」）
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                text: "创建并进入世界"; color: "#eaf6ea"; font.pixelSize: 14; font.bold: true
                            }
                        }
                    }
                }

                // 选中世界操作（进入 / 删除）。
                Rectangle {
                    width: parent.width; radius: 8
                    color: "#0e151d"; border.color: "#243040"; border.width: 1
                    height: 200
                    Column {
                        anchors.fill: parent; anchors.margins: 16; spacing: 12
                        Text {
                            text: root.selectedFile.length > 0 ? ("已选: " + root.selectedName) : "未选择世界"
                            color: root.selectedFile.length > 0 ? "#eaf2ea" : "#6a7a8a"
                            font.pixelSize: 15; font.bold: true
                            elide: Text.ElideRight; width: parent.width
                        }
                        Rectangle {
                            width: parent.width; height: 40; radius: 6
                            enabled: root.selectedFile.length > 0
                            opacity: enabled ? 1.0 : 0.4
                            color: playArea.containsPress ? "#335c33"
                                 : playArea.containsMouse ? "#4f8a4f" : "#3a6a3a"
                            border.color: "#7fe57f"; border.width: 1
                            MouseArea {
                                id: playArea; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: if (root.selectedFile.length > 0)
                                    root.playRequested(root.selectedFile, root.selectedName)
                            }
                            Text { anchors.centerIn: parent; text: "进入世界"; color: "#eaf6ea"; font.pixelSize: 15; font.bold: true }
                        }
                        Rectangle {
                            width: parent.width; height: 36; radius: 6
                            enabled: root.selectedFile.length > 0
                            opacity: enabled ? 1.0 : 0.4
                            color: delArea.containsMouse ? "#5a2a2a" : "#2a1a1a"
                            border.color: "#8a3a3a"; border.width: 1
                            MouseArea {
                                id: delArea; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.selectedFile.length === 0 || !store) return
                                    store.deleteWorld(root.selectedFile)
                                    root.selectedFile = ""; root.selectedName = ""
                                    root.refresh()
                                }
                            }
                            Text { anchors.centerIn: parent; text: "删除世界"; color: "#e0a0a0"; font.pixelSize: 13 }
                        }
                    }
                }
            }
        }
    }
}
