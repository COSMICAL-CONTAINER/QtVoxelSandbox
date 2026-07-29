import QtQuick

// 生命心 / 饥饿鼓腿 单格图标（t22）：自绘原创像素图，Canvas 逐像素填充（§9 override (a)：
// **非** MC GUI PNG，本项目程序生成）。kind 决定形状、level 决定填充态。
//
//   kind  : "heart"（生命心）| "hunger"（饥饿鼓腿）
//   level : 0=empty（仅暗轮廓）/ 1=half（左半亮）/ 2=full（整格亮）
//
// 像素位图 8×6（1=填充），scale=2 → 16×12 像素图，居中于 18×18 画布。8 列宽保证 half 分界
// 恰为左 4 列（偶宽），与 MC「半心=左半红」一致。先整体画暗（空态轮廓），再按 level 叠亮色：
// half 仅左半、full 整格。两态位图形状对称，故 empty/half/full 共用同一轮廓，仅亮色覆盖范围不同。
Canvas {
    id: root

    property string kind: "heart" // "heart" | "hunger"
    property int level: 2         // 0=empty 1=half 2=full

    width: 18
    height: 18

    onKindChanged: requestPaint()
    onLevelChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    // 8×6 像素位图（1=该格填充）。心：双峰顶 + 圆肩 + 尖底；鼓腿：左上肉球 + 右下骨柄。
    readonly property var heartBmp: [
        [0,1,1,0,0,1,1,0],
        [1,1,1,1,1,1,1,1],
        [1,1,1,1,1,1,1,1],
        [0,1,1,1,1,1,1,0],
        [0,0,1,1,1,1,0,0],
        [0,0,0,1,1,0,0,0],
    ]
    readonly property var hungerBmp: [
        [0,0,1,1,1,1,0,0],
        [0,1,1,1,1,1,1,0],
        [0,1,1,1,1,1,1,0],
        [0,0,0,1,1,1,1,0],
        [0,0,0,0,1,1,0,0],
        [0,0,0,0,1,1,0,0],
    ]

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.imageSmoothingEnabled = false // 像素硬边，不抗锯齿（1.0 风格）

        const bmp = root.kind === "hunger" ? root.hungerBmp : root.heartBmp
        const rows = bmp.length
        const cols = bmp[0].length
        const scale = 2
        const ox = Math.floor((root.width  - cols * scale) / 2)
        const oy = Math.floor((root.height - rows * scale) / 2)

        // 暗色（空态）：心=暗红、鼓腿=暗棕。先整体铺暗轮廓 → level=0 时只剩它即空态。
        const dim = root.kind === "hunger" ? "#4a3520" : "#5a1a1a"
        // 亮色（满态）：心=红、鼓腿=熟肉棕。
        const bright = root.kind === "hunger" ? "#b5783a" : "#d22e2e"

        for (let r = 0; r < rows; ++r) {
            for (let c = 0; c < cols; ++c) {
                if (!bmp[r][c]) continue
                const px = ox + c * scale
                const py = oy + r * scale
                ctx.fillStyle = dim
                ctx.fillRect(px, py, scale, scale)
                // 叠亮：full=整格；half=仅左半（像素中心落于左半才亮）。
                // half 分支必须 `level >= 1` 守门，否则 level=0（空）时左半仍被涂亮 → 空心视觉等同半心，
                // 扣血时无法分辨「剩 1 点」与「已耗尽」。守门后：0=全暗、1=左半亮、2=整格亮，三态可分。
                if (root.level >= 2 || (root.level >= 1 && (c + 0.5) * scale < cols * scale / 2)) {
                    ctx.fillStyle = bright
                    ctx.fillRect(px, py, scale, scale)
                }
            }
        }
    }
}
