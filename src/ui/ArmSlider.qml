import QtQuick

// 通用滑动条（纯 QtQuick 自绘；项目未链接 QtQuick.Controls → 不用 Slider 控件）。
// 最初是 SurvivalInventory 内联 component（t129 临时手部角度/位置调试），t139 把调试区
// 移到 ESC 设置面板后抽成独立文件供 Main.qml 设置面板复用。
// ArmSlider = track(Rectangle) + handle(Rectangle) + MouseArea（点 track 任意位置跳值 +
// 拖动连续改值）。value 单向写回宿主属性（宿主在 onValueChanged 里写回，见设置面板用法）。
// 复用既有自绘风格（NoLighting 不适用；2D 控件，纯 Rectangle 组合，§9 override (a) 自绘原创）。
Item {
    id: sl

    property real value: 0
    property real from: 0
    property real to: 1
    property string label: ""

    width: 320
    height: 30

    readonly property real __range: to - from
    readonly property real __norm: __range === 0 ? 0.0
        : Math.max(0.0, Math.min(1.0, (value - from) / __range))

    // 据 mouseX（相对 track）反算归一化值并写 value（imperative 赋值 → 破坏 value 的初始绑定，此后
    //   slider 为权威；onValueChanged 由宿主连到写回逻辑）。
    function __setFromX(mx) {
        const trackW = track.width > 0 ? track.width : sl.width
        const n = Math.max(0.0, Math.min(1.0, mx / trackW))
        sl.value = sl.from + n * sl.__range
    }

    Text {
        anchors.left: parent.left; anchors.top: parent.top
        text: sl.label
        color: "#9aa0a6"; font.pixelSize: 11
    }
    Text {
        anchors.right: parent.right; anchors.top: parent.top
        text: sl.value.toFixed(2)
        color: "#eaf2ea"; font.pixelSize: 11; font.bold: true
    }
    Rectangle {
        id: track
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.topMargin: 16
        height: 12; radius: 6
        color: "#2a2f36"
        Rectangle { // 已填充段（左→handle）
            width: sl.__norm * track.width; height: parent.height; radius: 6
            color: "#5a8a5a"
        }
        Rectangle { // handle（圆点，居中于 __norm）
            x: sl.__norm * Math.max(0, track.width - width)
            anchors.verticalCenter: parent.verticalCenter
            width: 10; height: 10; radius: 5
            color: "#eaf2ea"; border.color: "#3a444f"; border.width: 1
        }
        MouseArea {
            anchors.fill: parent
            onPressed: (mouse) => sl.__setFromX(mouse.x)
            onPositionChanged: (mouse) => sl.__setFromX(mouse.x)
            preventStealing: true
        }
    }
}
