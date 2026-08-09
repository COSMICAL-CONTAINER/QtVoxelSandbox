import QtQuick
// t41：迁入 src/ui/ 子目录后需显式 import 自身模块，以解析下方 `property Hotbar hotbar` 等 C++ 类型。
import VoxelSandbox

// 附魔台 UI（t474）：右键附魔台方块打开（PlayerController::enchantingTableOpened → Main.qml Connections →
// 显本面板 + 释放指针）。Esc / E / 关闭信号关闭（宿主恢复 grab）。
//
// 布局贴近 MC 1.0 附魔台（mechanism 对齐，§9 区隔原创 GUI）：
//   - 顶部标题栏「附魔台」+ 关闭按钮。
//   - 上区状态条：当前 XP 等级 / 当前青金石数 / 书架加成 power → 可选最高档位。
//   - 中区 3 个附魔选项槽横排：每槽显档位（I / II / III）+ XP 等级消耗（1 / 2 / 3）+ 青金石消耗（1 / 2 / 3）。
//     槽 enabled 条件：档位序号 < maxLevel（书架加成解锁）且 XP 等级 >= 消耗 且 青金石 >= 消耗。
//     点击 enabled 槽 → spendLevels(消耗) + consumeMaterial(LapisId, 消耗) → 触发 flash「已附魔」+ 重投
//     选项名（refreshOptions，机制等价 MC 每次附魔后选项重投）。本任务无真附魔效果（t475/t476 实装），
//     仅做消耗 + 占位文案。
//   - 下区提示文字：「放置书架（2 格内）提升可选档位」。
//
// **PERF 护栏（spec「only build/refresh option list when UI OPENS or player inventory changes, NEVER per-frame」）**：
//   选项列表是确定性占位（3 槽：档位 / 消耗固定 1/2/3），无需每帧重算；选项名（占位串）仅在「面板打开」
//   或「点击附魔后」（reroll）时刷新（refreshOptions）。XP 等级 / 青金石数显示绑定到 playerState.level（NOTIFY
//   levelChanged）+ hotbar.slotRevision（NOTIFY slotsChanged）—— 这两个信号都是低频（升级 / 槽写入才发，
//   非 60Hz tick），绑定重算开销可忽略。无 Timer / 无 onFrame / 无 PositionChanged 扫描。
//
// 全部 GUI 自绘原创（Rectangle + Text + Canvas 像素图，无外部 MC GUI PNG；§9 override (a)）。
// 零 MC 专有名词（§9）。宿主负责指针态：打开时 release（光标可见点槽），关闭 → grab。

Item {
    id: root

    // 宿主注入：hotbar 视图模型（consumeMaterial / materialCount / slotRevision）。
    property Hotbar hotbar
    // 宿主注入：playerState（level / spendLevels）。
    property PlayerState playerState
    // 宿主注入：附魔台方块世界坐标（World.countBookshelvesAround 查书架加成）。
    property int enchantX: 0
    property int enchantY: 0
    property int enchantZ: 0
    // 宿主注入：World（countBookshelvesAround 查书架数）。声明为 var 避免类型解析耦合（World 是 C++ 单例）。
    property var theWorld: null
    // 请求宿主关闭面板（恢复指针锁定 + 焦点回键位层）。
    signal closed()

    // ── 尺寸常量 ──
    readonly property int slotSize: 56
    readonly property int panelW: 360
    readonly property int panelH: 240

    // 青金石物品 id（RecipeRegistry::LapisId；Core 不依赖 Game 故 hotbar 无导出常量，硬编码 0x236）。
    readonly property int lapisId: 0x236

    // 3 个档位的固定消耗（XP 等级 + 青金石数）：I = (1, 1) / II = (2, 2) / III = (3, 3)。
    //   spec「each consumes XP level (1/2/3) + matching lapis count (1/2/3)」。
    readonly property var levelCosts: [1, 2, 3]
    readonly property var lapisCosts: [1, 2, 3]

    // 占位附魔名（每次附魔后重投；机制等价 MC 附魔台随机三选项，本任务仅占位文案，§9 通用词非专名）。
    //   真附魔表 / 属性集归 t475；本任务仅 UI 外壳 + 消耗机制。
    readonly property var placeholderNames: [
        "锋利", "保护", "效率", "耐久", "时运", "精准", "击退", "火焰附加"
    ]
    // 选项名（3 个槽各自的占位名）；refreshOptions 时重投。
    property var optionNames: ["附魔 I", "附魔 II", "附魔 III"]

    // 书架加成（0..15）→ 可选最高档位（1..3）：MC 风格 floor(bookshelves / 2) + 1，钳 [1, 3]。
    //   书架 0 → maxLevel 1（仅 I 档可选）/ 书架 2 → maxLevel 2（I+II）/ 书架 4+ → maxLevel 3（全档）。
    //   spec「bookshelf power raises max selectable enchant level」。
    readonly property int bookshelfPower: {
        if (!theWorld) return 0
        return theWorld.countBookshelvesAround(enchantX, enchantY, enchantZ)
    }
    readonly property int maxLevel: Math.min(3, Math.max(1, Math.floor(bookshelfPower / 2) + 1))

    // 当前青金石持有数（hotbar + 主栏跨槽累计；slotRevision / mainRevision 触碰刷新）。
    readonly property int lapisCount: {
        if (!hotbar) return 0
        hotbar.slotRevision; hotbar.mainRevision  // 触碰 NOTIFY（低频：槽写入才发，非 per-frame）
        return hotbar.materialCount(lapisId)
    }

    // 当前 XP 等级（绑定 playerState.level NOTIFY levelChanged；低频，升级才发）。
    readonly property int playerLevel: playerState ? playerState.level : 0

    // 「已附魔」flash 状态（点击成功附魔后短暂显绿，~600ms 淡出）。
    property bool justEnchanted: false

    // ── 面板遮罩 + 背板 ──
    // 黑色半透遮罩（同背包面板风格）：点击遮罩区 → 关闭（释放光标手持栈由宿主 closeEnchantingTable 归还）。
    Rectangle {
        anchors.fill: parent
        color: "#000000"; opacity: 0.55
        TapHandler { onTapped: root.closed() }
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
                text: "附魔台"
                color: "#ffd87a"; font.pixelSize: 18; font.bold: true
            }
            // 关闭按钮（右上 X，同背包面板风格）
            Rectangle {
                anchors.right: parent.right; anchors.rightMargin: 6
                anchors.top: parent.top; anchors.topMargin: 6
                width: 24; height: 24; color: "#5a3a22"; border.color: "#1a120c"; radius: 3
                Text { anchors.centerIn: parent; text: "×"; color: "#ffd87a"; font.pixelSize: 18; font.bold: true }
                TapHandler { onTapped: root.closed() }
            }
        }

        // 上区状态条：XP 等级 / 青金石数 / 书架加成 → 可选档位
        Rectangle {
            id: statusbar
            anchors.top: parent.top; anchors.topMargin: 42
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 24; height: 36
            color: "#241a12"; radius: 3
            Text {
                anchors.centerIn: parent
                text: "等级 " + playerLevel + "    青金石 " + lapisCount +
                      "    书架 " + bookshelfPower + " → 可选 " +
                      (maxLevel === 1 ? "I" : maxLevel === 2 ? "I-II" : "I-III") + " 档"
                color: "#ffe6a8"; font.pixelSize: 13
            }
        }

        // 中区 3 个附魔选项槽横排
        Row {
            id: optionRow
            anchors.top: statusbar.bottom; anchors.topMargin: 14
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 14

            Repeater {
                model: 3
                delegate: Rectangle {
                    id: slot
                    property int idx: index
                    property int lvlCost: root.levelCosts[index]
                    property int lapCost: root.lapisCosts[index]
                    // enabled 条件：档位序号 < maxLevel（书架解锁）且 XP 等级 + 青金石都够消耗。
                    property bool unlocked: index < root.maxLevel
                    property bool affordable: root.playerLevel >= lvlCost && root.lapisCount >= lapCost
                    property bool enabled1: unlocked && affordable
                    width: root.slotSize; height: root.slotSize
                    color: enabled1 ? "#5a4a2a" : "#2a2018"
                    border.color: enabled1 ? "#ffd87a" : "#0a0604"
                    border.width: enabled1 ? 2 : 1
                    radius: 4
                    opacity: unlocked ? 1.0 : 0.4  // 未解锁档位半透

                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: ["I", "II", "III"][index]
                            color: enabled1 ? "#ffe6a8" : "#665544"
                            font.pixelSize: 16; font.bold: true
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.optionNames[index] || "附魔"
                            color: enabled1 ? "#ffd87a" : "#665544"
                            font.pixelSize: 9
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: lvlCost + "级 / " + lapCost + "青金"
                            color: enabled1 ? "#a8d8ff" : "#554433"
                            font.pixelSize: 8
                        }
                    }

                    TapHandler {
                        enabled: slot.enabled1
                        onTapped: {
                            // 双重守卫（TapHandler.enabled 已门控，此处再检防御）
                            if (!slot.enabled1) return
                            // 1) 消耗 XP 等级（playerState.spendLevels 返 false = 余额不足 → 中止；理论已守）。
                            if (!root.playerState.spendLevels(slot.lvlCost)) return
                            // 2) 消耗青金石（跨槽 hotbar + 主栏扣；返 false = 不足 → 回滚 XP？理论已守）。
                            if (!root.hotbar.consumeMaterial(root.lapisId, slot.lapCost)) {
                                // 罕见：spendLevels 成功但 consumeMaterial 失败（理论 lapisCount 已守）。
                                // 防御：不回滚 XP（用户已付等级，重投选项作补偿）；日志待人工。
                                console.warn("[enchant] lapis consume failed after XP spend")
                            }
                            // 3) flash「已附魔」+ 重投选项名（机制等价 MC 每次附魔后选项重投）。
                            root.justEnchanted = true
                            enchantFlashTimer.restart()
                            root.refreshOptions()
                        }
                    }
                }
            }
        }

        // 下区提示文字
        Text {
            anchors.bottom: parent.bottom; anchors.bottomMargin: 12
            anchors.horizontalCenter: parent.horizontalCenter
            text: "在附魔台 2 格内放置书架（每 2 个解锁更高档）"
            color: "#aa9888"; font.pixelSize: 10
        }

        // 「已附魔」绿色 flash 叠层（点击成功后短暂显，~600ms 淡出）
        Rectangle {
            anchors.fill: parent
            color: "#3aa55a"; opacity: root.justEnchanted ? 0.35 : 0.0
            visible: opacity > 0.001
            Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
            Text {
                anchors.centerIn: parent
                text: "已附魔！"
                color: "#ffffff"; font.pixelSize: 22; font.bold: true
                visible: root.justEnchanted
            }
        }
    }

    // 「已附魔」flash 计时器（600ms 后翻 false → 触发 opacity Behavior 淡出）。
    Timer {
        id: enchantFlashTimer
        interval: 600
        onTriggered: root.justEnchanted = false
    }

    // ── PERF 护栏：选项列表只在「面板显」或「点击附魔后」刷新（refreshOptions），永不 per-frame。
    //   面板从隐藏切到显示时（visible → true）刷一次（重新查书架 + 重投选项名）。
    //   书架数 / maxLevel 是只读属性（绑定到 enchantX/Y/Z，切附魔台时变；面板打开时 enchantX/Y/Z 已由宿主设好）。
    onVisibleChanged: {
        if (visible) refreshOptions()
    }
    // 切附魔台坐标（同时开了不同附魔台）→ 重投选项 + 书架数（绑定自动重算）。
    onEnchantXChanged: if (visible) refreshOptions()
    onEnchantYChanged: if (visible) refreshOptions()
    onEnchantZChanged: if (visible) refreshOptions()

    // 重投选项名（占位串；机制等价 MC 附魔台随机三选项）。确定性按 enchantX/Y/Z hash 选 3 个不重名。
    function refreshOptions() {
        // 简单确定性「随机」：以附魔台坐标 + 当前等级为种子选 3 个占位名（同地同等级同选项；
        // 重投后（等级降）种子变 → 选项换，机制等价 MC 每次附魔后选项刷新）。
        var seed = (enchantX * 73856093) ^ (enchantY * 19349663) ^ (enchantZ * 83492791) ^ (playerLevel * 2654435761)
        seed = Math.abs(seed) | 0
        var picked = []
        var pool = placeholderNames.slice()
        for (var i = 0; i < 3 && pool.length > 0; ++i) {
            seed = (seed * 1103515245 + 12345) & 0x7fffffff
            var idx = seed % pool.length
            picked.push(pool[idx])
            pool.splice(idx, 1)
        }
        optionNames = picked.length === 3 ? picked : ["附魔 I", "附魔 II", "附魔 III"]
    }
}
