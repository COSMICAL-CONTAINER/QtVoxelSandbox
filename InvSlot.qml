import QtQuick

// 物品栏统一槽位背景（t24）：自绘原创凹陷斜面（sunken bevel：顶/左 1px 暗边 + 底/右 1px 亮边 →
// 凹陷观感），与游戏内 hotbar / 创造背包的槽框同风格。
//
// 抽出为独立组件的原因：生存背包单屏有 40+ 槽（主栏 27 + hotbar 9 + 合成 2×2 + 结果 1 + 护甲 4），
// 逐槽重复 5 个 Rectangle 既冗长又易抄错；统一一处定义、各槽复用，也方便后续背包（t18/t23）统一收编。
//
// 本组件**只画背景 + 可选高亮描边**；槽内容（方块图标 / 护甲像素图）由调用方作为兄弟节点叠在上方。
// §9 override (a)：本项目自绘原创，**非** MC GUI PNG。
Item {
    id: root

    // 槽井底色（不同区域可微调：合成 / 护甲用更暗的井底表「占位 / 无内容」，主栏 / hotbar 用标准井底）。
    property color wellColor: "#2f2f2f"
    // hover / 选中高亮描边（true 时画亮绿描边）。
    property bool highlight: false

    // 井底
    Rectangle { anchors.fill: parent; color: root.wellColor }
    // 凹陷斜面：顶 / 左 1px 暗边（阴影）
    Rectangle { color: "#0a0a0a"; width: parent.width; height: 1; anchors.top: parent.top }
    Rectangle { color: "#0a0a0a"; width: 1; height: parent.height; anchors.left: parent.left }
    // 凹陷斜面：底 / 右 1px 亮边（受光）
    Rectangle { color: "#5a5a5a"; width: parent.width; height: 1; anchors.bottom: parent.bottom }
    Rectangle { color: "#5a5a5a"; width: 1; height: parent.height; anchors.right: parent.right }
    // 高亮描边（hover / 选中态）
    Rectangle {
        anchors.fill: parent
        color: "transparent"; radius: 2
        border.color: root.highlight ? "#7fe57f" : "transparent"; border.width: 2
    }
}
