import QtQuick

// 材料图标（t50）：自绘原创像素图（§9 override (a)，Canvas；非 MC 资产 PNG）。
//
// 按 materialId 分支画形状（id 取自 src/Game/recipe.h RecipeRegistry::*Id；QML 无法直接 import
// C++ constexpr，故用同源字面量 + 注释钉死，改一处须同步 recipe.h）：
//   0x200 木棒   —— 斜置棕色长条（原木亮面 + 暗面 + 端面收口）。
//   0x201 煤炭   —— 黑色八边形块（顶面高光 + 底面阴影 + 矿物亮斑）。
//   0x202 铁原矿 —— 石质八边形块 + 橙棕色铁矿斑簇（表「含铁」，与铁矿石贴图同语义）。
//   0x203 铁锭   —— 银白水平梯形锭（顶受光 + 底阴影 + 两端斜切暗边）。
// 木棒既非方块（无等距立方体 PNG）也非工具（非 ToolIcon 镐形），煤/铁原矿/铁锭同理 → 均独立自绘。
//
// 消费点：Main.qml 的游戏内 hotbar delegate / 光标手持浮动图标 / 掉落实体 Repeater（sourceItem），
// Inventory.qml / SurvivalInventory.qml / CraftingTableUI.qml 各槽 —— 凡 hotbarVM.isMaterial(id) 为真
// 的槽位用本组件替代方块 Image / ToolIcon。新增材料在此 switch 加一分支即可全工程生效。
Item {
    id: root
    property int materialId: 0 // 材料段 id（0x200 木棒 / 0x201 煤 / 0x202 铁原矿 / 0x203 铁锭；0/未知 → 兜底木棒）

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

            // 木棒（0x200）：从左下 (5,19) 到右上 (17,7) 的对角长条（每步右上移，3px 宽 + 1px 高光 + 1px 暗面）。
            // 配色：原木亮面 #9c7340、暗面 #6b4f24、端面 #4a3018（与木板族配色一致）。
            const drawStick = () => {
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

            // 煤炭（0x201）：黑色八边形块。顶面受光提亮、底面压暗、撒几粒矿物亮斑 → 远看是「黑煤块」
            // 而非纯黑方块（纯黑在深色背景下不可辨）。配色：body #2c2c2c / 高光 #3e3e3e / 阴影 #161616 / 亮斑 #4a4a4a。
            const drawCoal = () => {
                const body = "#2c2c2c", light = "#3e3e3e", dark = "#161616", speck = "#4a4a4a"
                // 八边形外轮廓（行 7..17，最宽 15）
                R(7, 7, 11, 1, body)
                R(6, 8, 13, 1, body)
                R(5, 9, 15, 7, body)   // 主体 rows 9..15
                R(6, 16, 13, 1, body)
                R(7, 17, 11, 1, body)
                // 顶面受光（上两行 + 左上角）
                R(7, 7, 11, 1, light)
                R(6, 8, 13, 1, light)
                R(5, 9, 4, 1, light)
                // 底面阴影（下两行 + 右下边）
                R(6, 16, 13, 1, dark)
                R(7, 17, 11, 1, dark)
                R(11, 15, 4, 1, dark)
                // 矿物亮斑（几粒，表煤层反光）
                R(9, 11, 1, 1, speck)
                R(14, 12, 1, 1, speck)
                R(11, 13, 2, 1, speck)
                R(8, 14, 1, 1, speck)
            }

            // 铁原矿（0x202）：石质八边形块 + 橙棕色铁矿斑簇。与煤块同外轮廓（都是「矿石」族），
            // 靠石色基底 + 铁锈斑区分「含铁」。配色：石 light #b8a890 / mid #988868 / dark #685848；
            // 矿斑 ore #c87038 / oreDark #9a4818（与铁矿石贴图橙色基调一致）。
            const drawIronOre = () => {
                const light = "#b8a890", mid = "#988868", dark = "#685848"
                const ore = "#c87038", oreDark = "#9a4818"
                // 石质主体（同 coal 八边形外轮廓）
                R(7, 7, 11, 1, mid)
                R(6, 8, 13, 1, mid)
                R(5, 9, 15, 7, mid)
                R(6, 16, 13, 1, mid)
                R(7, 17, 11, 1, mid)
                // 顶面受光
                R(7, 7, 11, 1, light)
                R(6, 8, 13, 1, light)
                R(5, 9, 5, 1, light)
                // 底面阴影
                R(6, 16, 13, 1, dark)
                R(7, 17, 11, 1, dark)
                // 铁矿斑簇（橙棕色，散布表「含铁」；每簇 2×2 亮 + 1×1 暗边）
                R(8, 10, 2, 2, ore);  R(9, 10, 1, 1, oreDark)
                R(13, 11, 2, 2, ore); R(14, 12, 1, 1, oreDark)
                R(10, 13, 2, 2, ore); R(11, 13, 1, 1, oreDark)
                R(7, 14, 1, 1, ore)
                R(15, 9, 1, 1, oreDark)
            }

            // 铁锭（0x203）：银白水平梯形锭（顶略宽、底略窄、两端斜切）。MC 风格锭为水平金属条，
            // 顶面高光 + 底面阴影 + 两端暗边 → 立体金属感。配色：light #e8e8f0 / mid #b8b8c4 /
            // dark #787888 / edge #585866 / 高光线 #f8f8ff。
            const drawIronIngot = () => {
                const light = "#e8e8f0", mid = "#b8b8c4", dark = "#787888", edge = "#585866"
                // 梯形锭外轮廓（行 7..14，中部最宽 16）
                R(6, 7, 12, 1, light)    // 顶面（受光）
                R(5, 8, 14, 1, light)
                R(4, 9, 16, 4, mid)      // 主体 rows 9..12
                R(5, 13, 14, 1, dark)
                R(6, 14, 12, 1, dark)    // 底面阴影
                // 顶面高光线（更亮一条，表金属反光）
                R(7, 7, 10, 1, "#f8f8ff")
                // 两端斜切暗边（左 / 右各一列，表锭端收口）
                R(4, 9, 1, 4, edge)
                R(19, 9, 1, 4, edge)
            }

            // 按 materialId 分流（default / 未知 → 兜底木棒，与旧行为一致）。
            switch (root.materialId) {
            case 0x200: drawStick();     break
            case 0x201: drawCoal();      break
            case 0x202: drawIronOre();   break
            case 0x203: drawIronIngot(); break
            default:    drawStick();     break
            }
        }
    }
}
