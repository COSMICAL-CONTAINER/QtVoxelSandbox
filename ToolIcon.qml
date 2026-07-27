import QtQuick

// 工具图标（t33）：自绘原创像素镐（§9 override (a)，Canvas；非 MC 资产 PNG）。
//
// 按 tier 着色镐头材质（把手恒木色）：
//   tier 1 = 木镐（褐铜色头）、tier 2 = 石镐（中灰头）、tier 3 = 铁镐（银白头）。
//
// 消费点：Main.qml 的游戏内 hotbar delegate / 光标手持浮动图标，以及 Inventory.qml 创造调色板、
// SurvivalInventory.qml 各槽 —— 凡 .hotbarVM.isTool(id) 为真的槽位用本组件替代方块 Image
// （方块段走 iconSourceForBlock 的等距立方体 PNG；工具段无 PNG，纯 Canvas 自绘）。
//
// tier 由调用方经 hotbarVM.toolTier(itemId) 传入（1/2/3）。未来加铲 / 斧需按 toolType 分支
// 扩展（当前仅镐一种形状）。
Item {
    id: root
    property int tier: 1 // 1=木 2=石 3=铁（0 / 越界 → 兜底木镐配色）

    Canvas {
        id: canvas
        anchors.fill: parent
        // 尺寸 / tier 变化时重绘（Canvas 不自动据外部 property 刷新）。
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
        Connections {
            target: root
            function onTierChanged() { canvas.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.imageSmoothingEnabled = false // 像素硬边（1.0 风格）

            // 以 24×24 设计网格按比例缩放到当前画布尺寸（Math.floor/ceil 对齐像素栅栏防糊边）。
            const px = Math.max(canvas.width, canvas.height) / 24.0
            const R = (col, row, w, h, color) => {
                ctx.fillStyle = color
                ctx.fillRect(Math.floor(col * px), Math.floor(row * px),
                             Math.ceil(w * px), Math.ceil(h * px))
            }

            // tier → 镐头颜色（把手恒木色：通用工具柄）。
            const head  = root.tier === 2 ? "#9a9a9a"    // 石镐：中灰
                         : root.tier === 3 ? "#d8d8e6"   // 铁镐：银白
                         : "#9c6b3c"                      // 木镐：褐铜（默认 / tier 0 兜底）
            const headDark = root.tier === 2 ? "#5a5a5a"
                             : root.tier === 3 ? "#8a8a9a"
                             : "#5a3a1c"
            const handle = "#7a5230" // 木把手
            const handleDark = "#4a3018"
            const light  = "#caa472" // 把手受光高光
            const headLight = root.tier === 2 ? "#c4c4c4"
                              : root.tier === 3 ? "#f0f0fa"
                              : "#c48a5a"

            // —— 把手：从左下 (4,20) 到右上 (14,10) 的对角木柄（每步右上移 2px，2×2 僗 + 1px 高光）——
            const handleSegs = [
                [4, 20], [6, 18], [8, 16], [10, 14], [12, 12], [14, 10]
            ]
            for (const [c, r] of handleSegs) {
                R(c, r, 2, 2, handle)
                R(c, r, 1, 1, light)            // 左上角受光
                R(c + 1, r + 1, 1, 1, handleDark) // 右下角阴影
            }

            // —— 镐头：以把手顶 (14,10) 为中心，水平横梁 + 两端下勾（像素镐经典轮廓）——
            // 主梁（接把手顶，向左右延展）
            R(7, 7, 11, 2, head)
            R(8, 6, 9, 1, headLight)    // 顶面高光
            R(7, 9, 11, 1, headDark)    // 底描边
            // 左下勾（向外向下弯）
            R(5, 8, 2, 2, head)
            R(3, 9, 2, 2, head)
            R(3, 9, 1, 1, headDark)
            // 右下勾
            R(18, 8, 2, 2, head)
            R(20, 9, 2, 2, head)
            R(21, 9, 1, 1, headDark)
            // 两端尖（最外像素收窄）
            R(2, 10, 1, 1, headDark)
            R(22, 10, 1, 1, headDark)
        }
    }
}
