import QtQuick

// 材料图标（t50）：自绘原创像素图（§9 override (a)，Canvas；非 MC 资产 PNG）。
//
// 当前仅木棒（RecipeRegistry::StickId = 0x200）：斜置棕色长条（原木色亮面 + 暗面阴影）。
// 木棒既非方块（无等距立方体 PNG）也非工具（非 ToolIcon 镐形），需独立自绘。
//
// 消费点：Main.qml 的游戏内 hotbar delegate / 光标手持浮动图标，Inventory.qml / SurvivalInventory.qml /
// CraftingTableUI.qml 各槽 —— 凡 hotbarVM.isMaterial(id) 为真的槽位用本组件替代方块 Image / ToolIcon。
//
// 未来加更多材料（如铁锭 / 线 / 纸）时按 materialId 分支扩展形状（当前仅木棒一种）。
Item {
    id: root
    property int materialId: 0 // 材料段 id（当前仅 0x200=木棒；0/越界 → 兜底木棒）

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: request_paint_safe() // 防 0 尺寸
        Component.onCompleted: requestPaint()
        function request_paint_safe() { if (width > 0 && height > 0) requestPaint() }
        Connections {
            target: root
            function onMaterialIdChanged() { canvas.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.imageSmoothingEnabled = false // 像素硬边（1.0 风格）

            // 以 24×24 设计网格按比例缩放（与 ToolIcon 同网格，槽内尺寸一致）。
            const px = Math.max(canvas.width, canvas.height) / 24.0
            const R = (col, row, w, h, color) => {
                ctx.fillStyle = color
                ctx.fillRect(Math.floor(col * px), Math.floor(row * px),
                             Math.ceil(w * px), Math.ceil(h * px))
            }

            // 木棒：从左下 (5,19) 到右上 (17,7) 的对角长条（每步右上移，3px 宽 + 1px 高光 + 1px 暗面）。
            // 配色：原木亮面 #9c7340、暗面 #6b4f24、端面 #4a3018（与木板族配色一致）。
            const light = "#9c7340"
            const dark  = "#6b4f24"
            const end   = "#4a3018"
            const segs = [
                [5, 19], [6, 18], [7, 17], [8, 16], [9, 15], [10, 14],
                [11, 13], [12, 12], [13, 11], [14, 10], [15, 9], [16, 8]
            ]
            for (const [c, r] of segs) {
                R(c, r, 3, 3, dark)       // 暗面底
                R(c, r, 2, 2, light)      // 亮面（左上）
            }
            // 两端端面（深色收口，表「切断的木棒端」）
            R(4, 20, 2, 2, end)
            R(17, 7, 2, 2, end)
        }
    }
}
