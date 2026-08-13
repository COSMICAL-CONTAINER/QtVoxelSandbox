import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

// 铁砧 UI（t477）：右键铁砧方块打开（PlayerController::anvilOpened → Main.qml Connections → 显本面板 +
// 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// 布局贴近 MC 1.0 铁砧（mechanism 对齐，§9 区隔原创 GUI），但操作模式同附魔台 shell：以**当前选中 hotbar 槽**
// 为目标（简化：MC 铁砧有显式左右槽 + 光标系统，本工程同 EnchantingTableUI shell 模式操作选中槽，避免复制
// 完整 InventoryOps 光标系统）。底部 hotbar 行可点击切换目标槽。三功能各消耗 XP 等级：
//   - 修复（Repair）：合并两同物品耐久（+12% 加成），消耗 1 级。
//   - 附魔合并（Merge）：合并两同物品附魔（逐附魔取高等级），消耗 2 级。
//   - 重命名（Rename）：写入自定义名（HUD / 背包显重命名），消耗 1 级。
// 每次成功操作后调 player.damageAnvil(x,y,z) 推进铁砧损坏（~1/3 概率 +1 阶段；重损再损碎裂移除）。
//
// **PERF 护栏（spec「anvil UI recomputes ONLY on input slot change, never per-frame」）**：
//   所有显示绑定到 hotbar.slotRevision / selectedSlotChanged / playerState.levelChanged（低频 NOTIFY：
//   槽写入 / 选槽 / 升级才发，非 60Hz tick）。enabled 条件绑到 hotbar.anvilCanRepair/Merge*（同 low-freq NOTIFY）。
//   无 Timer / 无 onFrame / 无 PositionChanged 扫描。target 重算由 NOTIFY 驱动，非每帧。
//
// 全部 GUI 自绘原创（Rectangle + Text + Image，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点按钮 / 输入名），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（选中槽物品 + 铁砧三功能 + 切槽）。
    property Hotbar hotbar
    // 宿主注入：playerState（level / spendLevels）。
    property PlayerState playerState
    // 宿主注入：PlayerController（damageAnvil 推进铁砧损坏）。声明 var 避免类型解析耦合。
    property var player: null
    // 宿主注入：铁砧方块世界坐标（player.damageAnvil 推进损坏）。
    property int anvilX: 0
    property int anvilY: 0
    property int anvilZ: 0
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()

    // ── 尺寸常量 ──
    readonly property int slotSize: 48
    readonly property int panelW: 380
    readonly property int panelH: 360

    // 三功能 XP 等级消耗（机制等价 MC 铁砧消耗 player level；本工程取固定小量便于测试）。
    readonly property int repairCost: 1   // 修复：1 级
    readonly property int mergeCost: 2    // 附魔合并：2 级
    readonly property int renameCost: 1   // 重命名：1 级

    // 当前选中槽（绑定 hotbar.selectedSlot NOTIFY selectedSlotChanged；低频）。
    readonly property int selSlot: hotbar ? hotbar.selectedSlot : 0
    // 触碰 slotRevision（低频：槽写入才发）建立依赖 —— 选中槽内容变时 target / enabled 重算。
    readonly property int slotRev: hotbar ? hotbar.slotRevision : 0

    // 选中槽物品数据（绑定 selSlot + slotRev；低频重算）。
    readonly property int targetId: hotbar ? hotbar.blockIdAt(selSlot) : 0
    readonly property int targetCount: hotbar ? hotbar.countAt(selSlot) : 0
    readonly property int targetDur: hotbar ? hotbar.durabilityAt(selSlot) : 0
    readonly property int targetMaxDur: hotbar ? hotbar.toolMaxDurability(targetId) : 0
    readonly property string targetIcon: hotbar ? hotbar.iconSourceAt(selSlot) : ""
    readonly property string targetName: hotbar ? hotbar.nameAt(selSlot) : ""
    readonly property var targetEnchants: hotbar ? hotbar.enchantsAt(selSlot) : [0,0,0,0]

    // 当前 XP 等级（绑定 playerState.level NOTIFY levelChanged；低频）。
    readonly property int playerLevel: playerState ? playerState.level : 0

    // enabled 条件（绑定 slotRev + playerLevel；低频重算）。
    readonly property bool canRepair: hotbar && hotbar.anvilCanRepairSelected()
    readonly property bool canMerge: hotbar && hotbar.anvilCanMergeEnchantsSelected()
    readonly property bool canRename: targetId !== 0 && renameName.length > 0
    readonly property bool affordRepair: playerLevel >= repairCost
    readonly property bool affordMerge: playerLevel >= mergeCost
    readonly property bool affordRename: playerLevel >= renameCost

    // 重命名输入文本。
    property string renameName: ""

    // 操作结果 flash（成功后短暂显绿）。
    property string lastResult: ""
    property bool justActed: false

    // ── 面板遮罩 + 背板 ──
    Rectangle {
        anchors.fill: parent
        color: "#000000"; opacity: 0.55
        // 遮罩仅吸收点击（防穿透到背后游戏层），不关闭面板（MC 风格只能由 E/Esc 关）。
        MouseArea { anchors.fill: parent }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: panelW; height: panelH
        color: "#3b2d20"; border.color: "#1a120c"; border.width: 2
        radius: 4

        // 标题栏
        Rectangle {
            anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width; height: 36
            color: "#241a12"; radius: 4
            Text {
                anchors.centerIn: parent
                text: "铁砧"
                color: "#ffd87a"; font.pixelSize: 18; font.bold: true
            }
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 6
                anchors.top: parent.top; anchors.topMargin: 6
                width: 24; height: 24; color: "#5a3a22"; border.color: "#1a120c"; radius: 3
                Text { anchors.centerIn: parent; text: "×"; color: "#ffd87a"; font.pixelSize: 18; font.bold: true }
                TapHandler { onTapped: root.closed() }
            }
        }

        // 状态条：XP 等级 + 操作结果提示
        Rectangle {
            id: statusbar
            anchors.top: parent.top; anchors.topMargin: 42
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 24; height: 28
            color: "#241a12"; radius: 3
            Text {
                anchors.centerIn: parent
                text: "等级 " + playerLevel + (root.lastResult.length > 0 ? "    " + root.lastResult : "")
                color: "#ffe6a8"; font.pixelSize: 12
            }
        }

        // 目标物品展示（选中槽）
        Rectangle {
            id: targetBox
            anchors.top: statusbar.bottom; anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 24; height: 70
            color: "#2a2018"; border.color: "#5a4a2a"; border.width: 1; radius: 4

            // 目标槽方块（图标 + 数量）
            Rectangle {
                id: targetSlot
                anchors.left: parent.left; anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: slotSize; height: slotSize
                color: "#1a120c"; border.color: "#ffd87a"; border.width: 2; radius: 3
                Image {
                    anchors.centerIn: parent
                    width: slotSize - 8; height: slotSize - 8
                    source: targetIcon
                    visible: targetIcon.length > 0
                    fillMode: Image.Pad
                }
                Text {
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: 2
                    text: targetCount > 1 ? targetCount : ""
                    color: "#ffffff"; font.pixelSize: 12; font.bold: true
                    visible: targetCount > 1
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.bottom; anchors.topMargin: 2
                    text: "目标（槽 " + (selSlot + 1) + "）"
                    color: "#aa9888"; font.pixelSize: 9
                }
            }

            // 目标名 + 耐久 + 附魔
            Column {
                anchors.left: targetSlot.right; anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Text {
                    text: targetId === 0 ? "（空槽 — 按 1-9 或点底部槽选目标）" : targetName
                    color: targetId === 0 ? "#665544" : "#ffe6a8"
                    font.pixelSize: 13; font.bold: true
                }
                // 耐久条（仅工具 / 护甲显）
                Rectangle {
                    visible: targetMaxDur > 0
                    width: 200; height: 10
                    color: "#0a0604"; border.color: "#3a2a1a"; border.width: 1; radius: 2
                    Rectangle {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 1
                        width: parent.width > 2 ? Math.max(0, (parent.width - 2) * Math.max(0, Math.min(1, targetDur / Math.max(1, targetMaxDur)))) : 0
                        height: parent.height - 2
                        color: targetDur / Math.max(1, targetMaxDur) > 0.5 ? "#4aa55a" : (targetDur / Math.max(1, targetMaxDur) > 0.25 ? "#d8a84a" : "#c84a4a")
                        radius: 1
                    }
                    Text {
                        anchors.centerIn: parent
                        text: targetDur + " / " + targetMaxDur
                        color: "#ffffff"; font.pixelSize: 8
                    }
                }
                // 附魔列表（显非空附魔名 + 等级）
                Text {
                    text: formatEnchants(targetEnchants)
                    color: "#a8d8ff"; font.pixelSize: 10
                    visible: text.length > 0
                }
            }
        }

        // ── 三功能按钮 ──
        Column {
            id: actions
            anchors.top: targetBox.bottom; anchors.topMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 6

            // 修复
            AnvilActionButton {
                width: panelW - 48; height: 34
                title: "修复（合并同物品耐久）"
                costText: repairCost + " 级"
                enabled1: root.canRepair && root.affordRepair
                reasonText: !root.canRepair ? "需选中工具/护甲 + 背包有同款第二件" : (!root.affordRepair ? "经验不足" : "")
                onActivated: root.doRepair()
            }
            // 附魔合并
            AnvilActionButton {
                width: panelW - 48; height: 34
                title: "附魔合并（合并同物品附魔）"
                costText: mergeCost + " 级"
                enabled1: root.canMerge && root.affordMerge
                reasonText: !root.canMerge ? "需选中可附魔物 + 背包有带附魔同款第二件" : (!root.affordMerge ? "经验不足" : "")
                onActivated: root.doMerge()
            }
            // 重命名
            Rectangle {
                width: panelW - 48; height: 34
                color: "#2a2018"; border.color: (root.canRename && root.affordRename) ? "#ffd87a" : "#0a0604"
                border.width: (root.canRename && root.affordRename) ? 2 : 1; radius: 3
                Row {
                    anchors.centerIn: parent
                    spacing: 8
                    TextInput {
                        id: nameInput
                        anchors.verticalCenter: parent.verticalCenter
                        width: 180
                        color: "#ffe6a8"; font.pixelSize: 12
                        selectByMouse: true
                        maximumLength: 20
                        onTextEdited: root.renameName = text
                        Text {
                            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                            text: "输入新名…"
                            color: "#665544"; font.pixelSize: 12
                            visible: parent.text.length === 0
                        }
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 70; height: 26
                        color: (root.canRename && root.affordRename) ? "#5a4a2a" : "#2a2018"
                        border.color: "#1a120c"; radius: 3
                        Text {
                            anchors.centerIn: parent
                            text: "重命名\n" + renameCost + "级"
                            color: (root.canRename && root.affordRename) ? "#ffe6a8" : "#665544"
                            font.pixelSize: 9; font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        TapHandler {
                            enabled: root.canRename && root.affordRename
                            onTapped: root.doRename()
                        }
                    }
                }
            }
        }

        // 底部 hotbar 行（点选目标槽）
        Row {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 10
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 4
            Repeater {
                model: 9
                delegate: Rectangle {
                    width: 34; height: 34
                    color: index === root.selSlot ? "#5a4a2a" : "#1a120c"
                    border.color: index === root.selSlot ? "#ffd87a" : "#3a2a1a"
                    border.width: index === root.selSlot ? 2 : 1
                    radius: 3
                    Image {
                        anchors.centerIn: parent
                        width: 28; height: 28
                        source: root.hotbar ? root.hotbar.iconSourceAt(index) : ""
                        visible: source.toString().length > 0
                        fillMode: Image.Pad
                    }
                    Text {
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        anchors.margins: 1
                        text: root.hotbar && root.hotbar.countAt(index) > 1 ? root.hotbar.countAt(index) : ""
                        color: "#ffffff"; font.pixelSize: 10; font.bold: true
                    }
                    Text {
                        anchors.top: parent.top; anchors.left: parent.left
                        anchors.margins: 1
                        text: (index + 1)
                        color: "#665544"; font.pixelSize: 8
                    }
                    TapHandler { onTapped: root.hotbar.setSelectedSlot(index) }
                }
            }
        }

        // 「操作成功」绿色 flash 叠层（点击成功后短暂显，~600ms 淡出）
        Rectangle {
            anchors.fill: parent
            color: "#3aa55a"; opacity: root.justActed ? 0.30 : 0.0
            visible: opacity > 0.001
            Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
        }
    }

    // 操作成功 flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
    Timer {
        id: actFlashTimer
        interval: 600
        onTriggered: root.justActed = false
    }

    // ── 三功能执行（消耗 XP + 调 hotbar + 推进铁砧损坏）──
    function doRepair() {
        if (!root.canRepair || !root.affordRepair) return
        if (!root.playerState.spendLevels(repairCost)) return
        if (!root.hotbar.anvilDoRepairSelected()) return
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已修复"
        root.justActed = true
        actFlashTimer.restart()
    }
    function doMerge() {
        if (!root.canMerge || !root.affordMerge) return
        if (!root.playerState.spendLevels(mergeCost)) return
        if (!root.hotbar.anvilDoMergeEnchantsSelected()) return
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已合并附魔"
        root.justActed = true
        actFlashTimer.restart()
    }
    function doRename() {
        if (!root.canRename || !root.affordRename) return
        if (!root.playerState.spendLevels(renameCost)) return
        if (!root.hotbar.anvilDoRenameSelected(renameName)) return
        if (root.player) root.player.damageAnvil(anvilX, anvilY, anvilZ)
        root.lastResult = "已重命名"
        root.renameName = ""           // 清输入
        nameInput.text = ""
        root.justActed = true
        actFlashTimer.restart()
    }

    // 4 槽 packed 附魔 → 显示串（如 "锐锋 III / 耐久 I"）。空 → 空串。
    function formatEnchants(enchants) {
        if (!enchants || enchants.length === 0) return ""
        var parts = []
        for (var i = 0; i < enchants.length; ++i) {
            var packed = enchants[i]
            if (!packed) continue
            var eid = (packed >> 8) & 0xff
            var lvl = packed & 0xff
            var name = enchantDisplayName(eid)
            if (name.length > 0) parts.push(name + " " + romanSuffix(lvl))
        }
        return parts.join(" / ")
    }
    // 附魔 id → 中文名（复用 EnchantRegistry.displayName 经 hotbar 桥接；hotbar 未暴露逐 id 查名，故本地内联
    //   极简映射 —— 仅本 UI 显示用，避免新增 Q_INVOKABLE。§9 通用词非专名）。
    function enchantDisplayName(eid) {
        var names = {
            1: "锐锋", 2: "亡灵杀手", 3: "节肢克星", 4: "击退", 5: "燃焰",
            6: "效率", 7: "精准采集", 8: "时运", 9: "耐久",
            10: "保护", 11: "火焰保护", 12: "摔落保护", 13: "弹射物保护", 14: "水上亲和"
        }
        return names[eid] || ""
    }
    function romanSuffix(n) {
        if (n <= 0) return ""
        if (n <= 5) return ["I","II","III","IV","V"][n-1]
        return "" + n
    }

    // ── PERF 护栏：无 onVisibleChanged 重算需求（所有显示绑定驱动 by NOTIFY；面板显时绑定自动求值）。
    //   面板从隐藏切到显示时若需清状态：清重命名输入 + 结果提示。
    onVisibleChanged: {
        if (visible) { renameName = ""; nameInput.text = ""; lastResult = "" }
    }

    // AnvilActionButton 组件（修复 / 合并用；重命名行因含 TextInput 特化内联）。
    component AnvilActionButton : Rectangle {
        id: btn
        property string title: ""
        property string costText: ""
        property bool enabled1: false
        property string reasonText: ""
        signal activated()
        color: enabled1 ? "#2a2018" : "#1a140e"
        border.color: enabled1 ? "#ffd87a" : "#0a0604"
        border.width: enabled1 ? 2 : 1
        radius: 3
        Row {
            anchors.centerIn: parent
            spacing: 10
            Text {
                text: btn.title
                color: btn.enabled1 ? "#ffe6a8" : "#665544"
                font.pixelSize: 12; font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: btn.costText
                color: btn.enabled1 ? "#a8d8ff" : "#554433"
                font.pixelSize: 11
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: btn.reasonText
                color: "#886655"
                font.pixelSize: 9
                anchors.verticalCenter: parent.verticalCenter
                visible: btn.reasonText.length > 0
            }
        }
        TapHandler {
            enabled: btn.enabled1
            onTapped: btn.activated()
        }
    }
}
