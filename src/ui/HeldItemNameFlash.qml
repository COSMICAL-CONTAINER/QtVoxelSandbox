import QtQuick

// t977 切换物品栏物品名浮显（R19.17 🅶 杂项组收官；dev-plan t977）：切槽（滚轮 / 1-9 数字键 →
//   Hotbar::selectedSlotChanged）→ 短暂浮显当前选中槽物品名，白字，停留 holdMs 后经 fadeMs 淡出。
//   附魔物品显详情（名称 + 换行 + 「耐久: cur/max」 + 换行 + 逐条「附魔名 罗马等级」），改名物品显
//   改名后名字（多附魔工具区分用，用户第五轮口径）。机制等价 MC 切槽物品名浮显；文案 / 绘制全原创
//   （§9 override (a)：Text 白字黑描边自绘，非 MC GUI PNG）。
//
//   **AOT 契约（lessons-learned t976 / t177 / t498，注释即契约）**：文本组装绝不走跨单元绑定——本组件
//   对 Hotbar VM 的唯一读法是 Connections 信号处理器内直调 Q_INVOKABLE slotDetailText()（命令式、
//   事件级：切槽才读一次，无每帧直发信号）；显隐 / 不透明度决策只读本单元内属性（text / opacity），
//   零跨单元绑定 → qmlcachegen AOT 静态编译造不出「恒初值」的跨单元链（t976「已消耗耐久不显条」
//   同根的整类缺陷在此结构性不可达）。
//
//   - 空槽 / 空手 → 不显（口径登记：显「空手」从简不做，dev-plan t977）。
//   - 同槽内容变更 ≠ 切槽：拾取入手 / 铁砧改名 / 附魔 / 工具破损清槽都会对选中槽补发 selectedSlotChanged
//     （setStack 家族驱动 selectedBlockId 刷新）→ lastShownSlot 守卫拦下，只写真切槽才浮显。
//   - 菜单期 / 进世界读档：applyPlayerState 灌存档 selectedSlot 早于 appState 切 playing（Main.qml
//     enterWorld 顺序）→ active=false 分支只重同步基线，读档恢复不闪名。
//   - Timer 属 UI chrome（toast / 聊天淡出同族，t884 清点口径豁免）：**不 gate worldRunning**——暂停期
//     淡完无副作用，本组件与世界模拟零耦合。
//   - 连续切槽（滚轮快滚）：holdTimer.restart() 天然防抖——只显最后槽（任务口径「连续切槽重置计时」）。
Item {
    id: root

    // Hotbar VM（Main.qml `hotbar: hotbarVM` 显式路径注入；注入名与属性名不同名 → 无 t551 shadow 面）。
    property var hotbar: null
    // 游玩态门（Main.qml 绑 appState === "playing" && 非 Spectator）：false 期间 selectedSlotChanged
    // 只重同步基线不浮显（菜单 / 读档恢复 / 观察者），true 后的真切槽才显。
    property bool active: false
    // 停留 / 淡出时长（用户口径「一定时长淡出」定稿：停留 2s + 0.3s 淡出）。普通属性非 readonly：
    // 矩阵探针调短做确定性计时腿；生产路径不改默认值（默认形态由源码钉钉住）。
    property int holdMs: 2000
    property int fadeMs: 300

    // 尺寸随文本（挂载点 anchors.bottom / horizontalCenter 按本块几何锚定）。
    width: label.implicitWidth
    height: label.implicitHeight

    // 上次浮显过的槽下标（-1=未浮显过）：同槽信号（内容变更类）静默的守卫基线。
    property int lastShownSlot: -1

    Text {
        id: label
        text: ""
        color: "#ffffff" // 用户口径：白字（黑描边保证亮 / 暗槽底与地形背景可读，同 HUD hotbar 数量字）
        style: Text.Outline
        styleColor: "#000000"
        font.pixelSize: 14
        horizontalAlignment: Text.AlignHCenter
        opacity: 0
        visible: text.length > 0 && opacity > 0
    }

    // 停留计时：holdMs 到 → 启动淡出。声明式 interval 绑 holdMs（探针调短即生效）。
    Timer {
        id: holdTimer
        interval: root.holdMs
        repeat: false
        onTriggered: fadeOut.restart()
    }
    NumberAnimation {
        id: fadeOut
        target: label
        property: "opacity"
        to: 0
        duration: root.fadeMs
    }

    // 切槽浮显入口：组装文本 → 立即显（无淡入，用户口径「切槽触发 → 立即显」）→ 重置停留计时。
    function flash() {
        if (!root.hotbar)
            return
        const slot = root.hotbar.selectedSlot
        if (slot === root.lastShownSlot)
            return // 同槽内容变更 ≠ 切槽（拾取入手 / 改名 / 附魔 / 破损清槽也补发 selectedSlotChanged）
        root.lastShownSlot = slot
        // AOT 契约：Q_INVOKABLE 直读组装（单一权威在 Hotbar::slotDetailText），不走绑定。
        const txt = root.hotbar.slotDetailText(slot)
        label.text = txt
        if (txt.length === 0) { // 空槽 / 空手 → 不显（口径登记）
            holdTimer.stop()
            fadeOut.stop()
            label.opacity = 0
            return
        }
        fadeOut.stop()
        label.opacity = 1 // 立即显（opacity 直写不走 Behavior → 零淡入）
        holdTimer.restart()
    }

    Connections {
        target: root.hotbar
        function onSelectedSlotChanged() {
            if (!root.hotbar)
                return
            if (!root.active) { // 菜单 / 读档恢复期：只重同步基线，不浮显（防进世界闪上局持物名）
                root.lastShownSlot = root.hotbar.selectedSlot
                return
            }
            root.flash()
        }
    }

    Component.onCompleted: {
        if (hotbar)
            lastShownSlot = hotbar.selectedSlot // 首槽基线（创建期不浮显）
    }
}
