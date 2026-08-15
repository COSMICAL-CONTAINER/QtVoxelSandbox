import QtQuick
// t591：用 Basic 风格 ScrollBar（非默认 native Windows 风格）。native 风格不接受 contentItem/background
//   自定义（运行期 WRN "The current style does not support customization of this control"，自定义被忽略 =
//   仍显系统白条 = 用户「白色条不符合 UI 风格」根因）。Basic 风格控件可完整自定义（Qt 官方推荐路径）。
import QtQuick.Controls.Basic as Basic

// t591 项目统一样式滚动条：暗色细条（贴合深色 UI，替代系统 native 白条）。
//   背包调色板（Inventory.qml）/ 世界列表（WorldList.qml）/ 资源查看器（ResourceBrowser.qml）
//   三处 Flickable 共用 → 滚动条观感统一（"拉平"）。单一权威：改样式只动本文件。
//   手柄 = 6px 深灰圆角细条（hover 微亮 / pressed 更亮，提示可拖）；轨道透明（不画槽，
//   贴合深色面板无边框观感）。policy=AsNeeded：内容不足视口时不显（不占空间，同旧行为）。
Basic.ScrollBar {
    id: root
    policy: Basic.ScrollBar.AsNeeded
    // 手柄：深色圆角细条。Qt 按可视比例自动定手柄长 / 位置（本 Rectangle 只负责外观）。
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: root.pressed ? "#5a6a7a" : (root.hovered ? "#4a5a6a" : "#3a444f")
    }
    // 轨道：透明（不画槽，滚动时只显细手柄，与深色面板融一体）。
    background: Rectangle { color: "transparent" }
}
