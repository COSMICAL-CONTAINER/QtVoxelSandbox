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
    // t192 重命名内联编辑态：true = 展开「输入框 + 确认/取消」替换「重命名」按钮。选中切走时自动复位。
    property bool renaming: false

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
    onVisibleChanged: {
        if (visible) refresh()
        else renaming = false   // t192 面板隐藏 → 退出内联重命名态
    }
    Component.onCompleted: if (visible) refresh()
    // t192 选中切走 → 退出内联重命名态，免编辑框悬空挂着上一个世界的名。
    onSelectedFileChanged: renaming = false

    // t192 确认重命名：store.renameWorld(只改 world_meta.name，.sqlite 文件名不动) → 更新 selectedName +
    //   refresh() 让列表 delegate 显新名。空名由 store 层回退「新世界」，此处同步 trim 后再传。
    function confirmRename() {
        if (!store || root.selectedFile.length === 0) { root.renaming = false; return }
        const name = renameInput.text.trim().length > 0 ? renameInput.text.trim() : "新世界"
        if (store.renameWorld(root.selectedFile, name)) root.selectedName = name
        // 无论成败都退出编辑态：失败时 store 已 qWarning（§2-E），refresh 显旧名，用户可再点重命名重试。
        root.renaming = false
        root.refresh()
    }
    function cancelRename() { root.renaming = false }

    Column {
        anchors.fill: parent
        anchors.margins: 40
        spacing: 18

        // 标题 + 返回。
        // t493 修「返回按钮跑到窗口外」旧 bug（一直存在，窄窗口触发）：旧 Row 用固定宽弹性间隔
        //   `Item{width: parent.width-400}`，窗口窄时（parent.width-400 < 0 或不够标题+按钮+边距）间隔先被
        //   压缩、按钮被推到父容器右侧之外（用户见「返回按钮只剩一半在窗口里」）。改 anchors 布局：标题
        //   锚左、返回按钮锚右、中间不占宽（标题与按钮重叠风险低——标题短、按钮固定宽 120、边距 40）。
        //   标题过长时截断（elide）而非溢出挤压按钮。
        Item {
            width: parent.width
            height: 40
            Text {
                id: wlTitle
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: "单人模式 — 世界列表"
                color: "#eaf2ea"; font.pixelSize: 32; font.bold: true; font.letterSpacing: 2
                style: Text.Outline; styleColor: "#000000"
                elide: Text.ElideRight
                width: parent.width - 200   // 给右侧返回按钮留位（120 按钮 + 余量）
            }
            Rectangle {
                id: wlBackBtn
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 120; height: 36; radius: 8
                color: backArea.containsMouse ? "#333c47" : "#222a32"
                border.color: "#3a444f"; border.width: 1
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
                                    // 单击仅选中；双击直接进入该世界（机制等价 MC 世界列表双击进入）。
                                    // MouseArea 在双击时先发 onClicked（选中）再发 onDoubleClicked（进入），
                                    // 故无需额外延时门控：选中先行，进入随后。
                                    onClicked: { root.selectedFile = model.file; root.selectedName = model.name }
                                    onDoubleClicked: if (model.file) root.playRequested(model.file, model.name)
                                }
                                // t191 截图封面缩略图（左侧 44×44）：store.coverPath 拼成 file:/// URL；
                                //   无封面（新世界 / 抓帧失败）→ Image 加载失败留空 → 透出底层灰槽作占位。
                                //   cache:false 每次重读盘（封面被「保存退出」覆盖后刷新见新画面，不留陈旧缓存）。
                                Rectangle {
                                    id: coverSlot
                                    anchors.left: parent.left; anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 44; height: 44; radius: 4
                                    color: "#0a1018"; border.color: "#2a3848"; border.width: 1
                                    clip: true
                                    Image {
                                        anchors.fill: parent
                                        source: root.store ? "file:///" + root.store.coverPath(model.file) : ""
                                        fillMode: Image.PreserveAspectCrop
                                        cache: false
                                        asynchronous: true
                                        sourceSize.width: 88; sourceSize.height: 88
                                    }
                                }
                                Column {
                                    anchors.left: parent.left; anchors.leftMargin: 66
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 4
                                    Text {
                                        text: model.name
                                        color: "#eaf2ea"; font.pixelSize: 16; font.bold: true
                                        width: worldsCol.width - 66 - 14
                                        elide: Text.ElideRight
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

                // 选中世界操作（进入 / 重命名 / 删除）。height 跟随内容 implicitHeight ——
                // t189 教训：固定高度装不下展开后的「输入框 + 确认/取消」会挤出底边框；按内容自适应。
                Rectangle {
                    width: parent.width; radius: 8
                    color: "#0e151d"; border.color: "#243040"; border.width: 1
                    height: selCol.implicitHeight + 32
                    Column {
                        id: selCol
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
                        // t192 重命名：已选且非编辑态显「重命名」按钮；编辑态由下方编辑行替换。
                        Rectangle {
                            width: parent.width; height: 36; radius: 6
                            visible: root.selectedFile.length > 0 && !root.renaming
                            color: renArea.containsMouse ? "#33455a" : "#22323f"
                            border.color: "#4a6a8a"; border.width: 1
                            MouseArea {
                                id: renArea; anchors.fill: parent; hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    renameInput.text = root.selectedName
                                    root.renaming = true
                                    renameInput.forceActiveFocus()
                                    renameInput.selectAll()
                                }
                            }
                            Text { anchors.centerIn: parent; text: "重命名"; color: "#bcd0e6"; font.pixelSize: 13 }
                        }
                        // t192 重命名编辑行（复用 nameInput 风格的 TextInput + 确认/取消）。
                        Column {
                            width: parent.width; spacing: 8
                            visible: root.renaming
                            Rectangle {
                                width: parent.width; height: 34; radius: 4
                                color: "#0a1018"; border.color: "#2a3848"; border.width: 1
                                TextInput {
                                    id: renameInput
                                    anchors.fill: parent; anchors.margins: 8
                                    color: "#eaf2ea"; font.pixelSize: 14
                                    selectByMouse: true; clip: true
                                    verticalAlignment: Text.AlignVCenter
                                    Keys.onReturnPressed: root.confirmRename()
                                    Keys.onEnterPressed: root.confirmRename()
                                    Keys.onEscapePressed: root.cancelRename()
                                }
                            }
                            Row {
                                width: parent.width; spacing: 8
                                Rectangle {
                                    width: (parent.width - 8) / 2; height: 34; radius: 6
                                    color: okRenArea.containsPress ? "#335c33"
                                         : okRenArea.containsMouse ? "#4f8a4f" : "#3a6a3a"
                                    border.color: "#7fe57f"; border.width: 1
                                    MouseArea {
                                        id: okRenArea; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.confirmRename()
                                    }
                                    Text { anchors.centerIn: parent; text: "确认"; color: "#eaf6ea"; font.pixelSize: 13; font.bold: true }
                                }
                                Rectangle {
                                    width: (parent.width - 8) / 2; height: 34; radius: 6
                                    color: cancelRenArea.containsMouse ? "#3a3a3a" : "#222a32"
                                    border.color: "#4a4f55"; border.width: 1
                                    MouseArea {
                                        id: cancelRenArea; anchors.fill: parent; hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: root.cancelRename()
                                    }
                                    Text { anchors.centerIn: parent; text: "取消"; color: "#cdd6dd"; font.pixelSize: 13 }
                                }
                            }
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
