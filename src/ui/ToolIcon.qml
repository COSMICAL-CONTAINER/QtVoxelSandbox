import QtQuick
// t420 资源包物品图标覆盖：用到模块 C++ 类型 ResourcePackManager（子目录文件须显式 import 自身模块，t41）。
import VoxelSandbox

// 工具图标（t33 / t233 / t264）：自绘原创像素工具（§9 override (a)，Canvas；非 MC 资产 PNG）。
//
// 五形状（t264 完整工具集后按 toolType 分支）：
//   - toolType 1（镐 Pickaxe，默认）：把手 + 顶部水平横梁 + 两端下勾（经典像素镐轮廓）。
//   - toolType 2（锄 Hoe）：把手 + 顶部一片宽扁横向锄刃（向前伸的刮土刃，区别于镐的下勾）。
//   - toolType 3（斧 Axe）：把手 + 顶部一侧的厚重斧刃（单边块状刃，区别于镐的对称双勾）。
//   - toolType 4（铲 Shovel）：把手 + 顶部方形铲斗（宽扁方铲头，掘土的工具头）。
//   - toolType 5（剑 Sword）：纵向长刃 + 护手 + 柄（无对角木柄；刃占主体，攻击武器轮廓）。
//
// tier 着色头部材质（把手 / 护手恒木色；剑刃全 tier 色）：
//   tier 1 = 木（褐铜色头）、tier 2 = 石（中灰头）、tier 3 = 铁（银白头）。
// 五类共用同一 tier→色映射（材质按 tier 一致，形状按 toolType 区分）。
//
// 消费点：Main.qml 的游戏内 hotbar delegate / 光标手持浮动图标 / 掉落实体、Inventory.qml 创造调色板、
// SurvivalInventory / CraftingTableUI / ChestUI / FurnaceUI 各槽 —— 凡 .hotbarVM.isTool(id) 为真的槽位
// 用本组件替代方块 Image（方块段走 iconSourceForBlock 的等距立方体 PNG；工具段无 PNG，纯 Canvas 自绘）。
// 调用方须同时传 tier（hotbarVM.toolTier）+ toolType（hotbarVM.toolType），缺省 toolType=1（镐）兜底。
Item {
    id: root
    property int tier: 1     // 1=木 2=石 3=铁（0 / 越界 → 兜底木色配色）
    property int toolType: 1 // 1=镐 Pickaxe（默认）/ 2=锄 Hoe / 3=斧 Axe / 4=铲 Shovel / 5=剑 Sword / 6=剪刀 Shears / 7=弓 Bow（0 / 越界 → 兜底镐形）

    // t420 资源包物品图标覆盖：pack 启用且该工具在「引擎工具 id → pack item 文件名」映射内、且包内 PNG 存在
    //   时，用 pack 的 item PNG 覆盖自绘 Canvas（缩到图标尺寸）；pack 关 / 无映射 → packImg.source 空 → Image
    //   隐藏、Canvas 自绘（现状不变）。红线（§9）：仅运行期读本地 gitignored pack 路径 PNG，绝不 bake 进 qrc/VCS。
    //   进程全局态（ResourcePackManager 静态 state）→ 每 icon 实例化一个轻 QObject 共享同一解析结果，开销可忽略。
    ResourcePackManager { id: rp }
    // 工具段 id 布局（item-ids.md §2 / toolregistry.h ToolId）由 (toolType, tier) 确定：type 1-5 =
    //   0x100+(type-1)*3+(tier-1)；shears/bow/fishingrod 独立 id。重建 itemId 供 pack 查询（与 C++ id 布局一致；
    //   id 若变只致 pack 回退自绘，不崩）。tier 越界兜底木（1）。
    function itemIdFromTypeTier(tt, t) {
        if (tt === 6) return 0x110  // 剪刀 Shears
        if (tt === 7) return 0x10F  // 弓 Bow
        if (tt === 8) return 0x111  // 钓鱼竿 FishingRod
        // t472 钻石镐脱离「每类 3 档 contiguous」布局（追加在 ToolId 末尾 0x112 保向后兼容）→ tier 4 特例映射，
        //   否则公式 0x100+(tt-1)*3+(tier-1) 对 (Pickaxe=1, tier=4) 错算成 0x103（HoeWood）→ pack 查询错图标。
        if (tt === 1 && t === 4) return 0x112  // 钻石镐 PickaxeDiamond
        if (tt < 1 || tt > 5) return 0
        const tier = (t < 1 || t > 3) ? 1 : t
        return 0x100 + (tt - 1) * 3 + (tier - 1)
    }
    Image {
        id: packImg
        anchors.fill: parent
        visible: source.length > 0
        // 触碰 tier/toolType/rp.active 建立绑定依赖（任一变 / pack 切换 → 重查 pack 源）。
        source: { const _a = rp.active; const _t = root.tier; const _tt = root.toolType
                  const id = root.itemIdFromTypeTier(root.toolType, root.tier)
                  return id >= 0 && _t >= 0 && _tt >= 0 && _a >= 0 ? rp.itemIconSource(id) : "" }
        fillMode: Image.PreserveAspectFit
        smooth: false // 像素硬边（同 Canvas imageSmoothingEnabled=false；MC item 图标为像素艺术）
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        // t424: pack 关 / 无映射时回退自绘。**不能用 `packImg.source.length === 0`**：Image.source 是 url
        //   类型，空 url 的 .length 在 QML JS 中为 undefined（非 0）→ `=== 0` 恒 false → canvas 永隐 → 所有
        //   非 pack 工具图标空白（hover 名仍对）。改 `!packImg.visible`：packImg.visible 走 `source.length > 0`，
        //   空 url 时 undefined > 0 = false（正确隐），取反即 canvas 显——与 packImg 互补且对空 url 稳健。
        visible: !packImg.visible
        // 尺寸 / tier / toolType 变化时重绘（Canvas 不自动据外部 property 刷新）。
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: requestPaint()
        // t424: canvas 由隐藏切回显（pack 切换 / 槽位换物后回退自绘）时不会自动重绘 → 显式重绘，
        //   否则该 canvas 此前隐藏期间未绘制、变显后仍空白（pack ON↔OFF 切换后非 pack 物品需此补绘）。
        onVisibleChanged: if (visible) requestPaint()
        Connections {
            target: root
            function onTierChanged() { canvas.requestPaint() }
            function onToolTypeChanged() { canvas.requestPaint() }
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

            // tier → 头部材质颜色（把手恒木色：通用工具柄；五类同 tier 同色）。
            //   t472 tier 4 = 钻石（青绿，机制对齐 MC 钻石工具配色）；目前仅镐有钻石档。
            const head  = root.tier === 4 ? "#4fd9d2"    // 钻石：青绿
                         : root.tier === 2 ? "#9a9a9a"    // 石：中灰
                         : root.tier === 3 ? "#d8d8e6"   // 铁：银白
                         : "#9c6b3c"                      // 木：褐铜（默认 / tier 0 兜底）
            const headDark = root.tier === 4 ? "#2e9a96"
                             : root.tier === 2 ? "#5a5a5a"
                             : root.tier === 3 ? "#8a8a9a"
                             : "#5a3a1c"
            const headLight = root.tier === 4 ? "#8ff0ea"
                              : root.tier === 2 ? "#c4c4c4"
                              : root.tier === 3 ? "#f0f0fa"
                              : "#c48a5a"
            const handle = "#7a5230" // 木把手
            const handleDark = "#4a3018"
            const light  = "#caa472" // 把手受光高光

            // t300 剪刀（toolType===6 / BlockRegistry::Shears）：两片 X 形交叉刀刃 + 中央枢轴 + 下端弹性弧环
            //   （剪羊毛的功能性工具轮廓）。早 return 跳过下方对角木柄 / 镐头默认（剪刀无木柄，整把 tier 着色，
            //   与弓 / 剑同策略）。机制等价 MC 1.0 剪刀（shears）；名称 / 图标全原创（§9a 区隔不照搬 MC 资产）。
            //   剪刀特征：上下两个尖端 + 中央铆钉 + 底部两侧弹性弧（手指环），轮廓一眼可辨。
            if (root.toolType === 6) {
                // t329 剪刀恒铁色（机制等价 MC 剪刀单一铁材质；ToolRegistry tier=1 仅记账不影响挖掘门槛 / 速度，
                //   故配色不跟随 tier）。块作用域 const 影子覆盖外层 head/headDark/headLight（tier 着色）→ 整把
                //   银白铁灰，而非木褐。与手持 / 掉落物 billboard 同源 ToolIcon 一致显铁色（剪刀无木柄、无 tier 变体）。
                const head = "#d8d8e6"        // 铁银白（= tier 3 head 色）
                const headDark = "#8a8a9a"    // 铁阴影
                const headLight = "#f0f0fa"   // 铁高光
                // 左刀刃（左上→右下对角，从尖端到枢轴）
                R(3, 3, 2, 2, head)            // 左上尖端
                R(5, 5, 2, 2, head)
                R(7, 7, 2, 2, head)
                R(4, 4, 1, 1, headLight)      // 刀刃受光高光
                R(6, 6, 1, 1, headLight)
                // 左刀刃下半（枢轴→右下，到底部弹性弧）
                R(9, 9, 2, 2, head)
                R(11, 11, 2, 2, head)
                R(10, 10, 1, 1, headLight)
                // 右刀刃（右上→左下对角，从尖端到枢轴）
                R(20, 3, 2, 2, head)           // 右上尖端
                R(18, 5, 2, 2, head)
                R(16, 7, 2, 2, head)
                R(19, 4, 1, 1, headLight)
                R(17, 6, 1, 1, headLight)
                // 右刀刃下半（枢轴→左下，到底部弹性弧）
                R(14, 9, 2, 2, head)
                R(12, 11, 2, 2, head)
                R(13, 10, 1, 1, headLight)
                // 中央枢轴（铆钉，深色圆点 = 两片刀刃的旋转中心）
                R(10, 9, 3, 3, headDark)
                R(11, 10, 1, 1, headLight)    // 铆钉受光高光
                // 底部弹性弧环（左右两侧椭圆环 = 手指环；剪刀的下端两个握环）
                R(13, 14, 3, 4, head)         // 右环（外弧）
                R(14, 15, 1, 2, headDark)     // 右环内孔描边（深色表「环洞」）
                R(8, 14, 3, 4, head)          // 左环（外弧）
                R(9, 15, 1, 2, headDark)      // 左环内孔描边
                R(8, 18, 3, 1, headDark)      // 左环底描边
                R(13, 18, 3, 1, headDark)     // 右环底描边
                return // 剪刀绘制完成（跳过下方工具柄 / 头）
            }
            // t304/t330 弓（toolType===7）：纵向 C 形弓身（凸左 / 凹右）+ 凹侧白弦 + 握把缠绳（远程武器轮廓）。
            //   早 return 跳过下方对角木柄 / 镐头默认（弓无木柄、整把 tier 着色，与 3D BowGeometry 同策略）。
            //   t330 修正：旧版弓身为竖直长方（无 C 弯）+ 弦用 headLight（木色）→ 观感「两根棍 / 木色弦」。
            //   新版弓身逐行阶梯拼成清晰 C 弧（凸侧 col7 握把 → 凹侧 col11-13 臂尖），弦改蜘蛛丝白独立于 tier。
            if (root.toolType === 7) {
                const silk = "#f5f5f5"             // 蜘蛛丝白弦（与 tier 木色分离；机制等价 MC 弓弦白）
                // 弓臂 C 弧（凸左 / 凹右，纵向对称；逐段阶梯从握把 col7 弯到臂尖 col11-13）
                R(7, 9, 2, 6, head)               // 握把段（左凸中段，col7-8 / 行9-14）
                R(8, 7, 2, 2, head)               // 上臂中段
                R(8, 15, 2, 2, head)              // 下臂中段
                R(9, 5, 2, 2, head)               // 上臂上段
                R(9, 17, 2, 2, head)              // 下臂下段
                R(10, 4, 2, 1, head)              // 上臂尖颈
                R(10, 19, 2, 1, head)             // 下臂尖颈
                R(11, 2, 3, 2, head)              // 上臂尖（弦接点，col11-13）
                R(11, 20, 3, 2, head)             // 下臂尖（弦接点）
                // 握把缠绳（凸侧深色短带，表「手握处」）
                R(6, 10, 1, 4, headDark)
                // 弓弦（凹侧 + 右 col14，连接上下臂尖；蜘蛛丝白）—— 旧版用 headLight 致木色弦，t330 改白
                R(14, 3, 1, 18, silk)
                return // 弓绘制完成（跳过下方工具柄 / 头）
            }
            // t401 钓鱼竿（toolType===8 / BlockRegistry::FishingRod）：长对角木杆 + 斜向白钓线 + 红白浮标（钓鱼工具轮廓）。
            //   早 return 跳过下方对角木柄 / 工具头默认（钓竿自成杆 + 线 + 浮标，不套用镐 / 锄等头部）。
            //   配色：杆 = 木色 handle（同 tier 把手）；线 = 蜘蛛丝白 silk；浮标 = 红顶 bobberRed + 白底 bobberWhite。
            if (root.toolType === 8) {
                const silk = "#f5f5f5"             // 蜘蛛丝白钓线（同弓弦白）
                const bobberRed = "#d83838"        // 浮标红顶
                const bobberWhite = "#f0f0f0"      // 浮标白底
                const bobberDark = "#8a1818"       // 浮标描边
                // 钓竿杆（从左下 (4,20) 到右上 (15,9) 的对角木柄，同镐 / 锄把手但更长更直）
                const rodSegs = [
                    [4, 20], [6, 18], [8, 16], [10, 14], [12, 12], [14, 10], [15, 9]
                ]
                for (const [c, r] of rodSegs) {
                    R(c, r, 2, 2, handle)
                    R(c, r, 1, 1, light)            // 左上角受光
                    R(c + 1, r + 1, 1, 1, handleDark) // 右下角阴影
                }
                // 杆尖（右上端，线接点）
                R(16, 8, 2, 2, handleDark)
                // 钓线（从杆尖 (17,8) 斜向右下到浮标 (20,17)，逐段阶梯）
                R(17, 9, 1, 2, silk)
                R(18, 11, 1, 2, silk)
                R(19, 13, 1, 2, silk)
                R(20, 15, 1, 2, silk)
                // 浮标（右下圆球：红顶 + 白底 + 描边，表「水面浮标」）
                R(19, 16, 5, 4, bobberWhite)       // 浮标主体（白底）
                R(19, 16, 5, 2, bobberRed)         // 红顶（上半红）
                R(19, 16, 5, 1, bobberDark)        // 顶描边
                R(19, 19, 5, 1, bobberDark)        // 底描边
                R(19, 17, 1, 3, bobberDark)        // 左描边
                R(23, 17, 1, 3, bobberDark)        // 右描边
                return // 钓竿绘制完成（跳过下方工具柄 / 头）
            }

            // 剑无对角木柄（整把纵向），其余四类共用对角木柄（从左下到右上）。
            if (root.toolType !== 5) {
                // —— 把手（镐 / 锄 / 斧 / 铲共用）：从左下 (4,20) 到右上 (14,10) 的对角木柄 ——
                const handleSegs = [
                    [4, 20], [6, 18], [8, 16], [10, 14], [12, 12], [14, 10]
                ]
                for (const [c, r] of handleSegs) {
                    R(c, r, 2, 2, handle)
                    R(c, r, 1, 1, light)            // 左上角受光
                    R(c + 1, r + 1, 1, 1, handleDark) // 右下角阴影
                }
            }

            // —— 头部：按 toolType 分支 ——
            if (root.toolType === 2) {
                // 锄头：以把手顶 (14,10) 为根，一片宽扁横向锄刃向右上方伸出 + 前端收窄。
                //   锄刃特征：宽扁、前端（远端）略下垂收尖，区别于镐的「横梁两端对称下勾」。
                // 主刃体（横向宽扁，接把手顶向右延展）
                R(14, 7, 9, 3, head)
                R(15, 6, 7, 1, headLight)   // 顶面高光
                R(14, 10, 9, 1, headDark)   // 底描边
                // 前端（远端）下垂收尖（锄刃刮土的「勾尖」朝下）
                R(22, 8, 2, 3, head)
                R(23, 9, 1, 2, head)
                R(24, 10, 1, 1, headDark)   // 最外尖
                R(22, 11, 2, 1, headDark)   // 前端底描边
            } else if (root.toolType === 3) {
                // 斧头：以把手顶 (14,10) 为根，单边厚重斧刃向右上方伸出（块状刃 + 弧形刃口）。
                //   斧特征：单侧厚刃（非镐的双端对称下勾），刃口朝下（砍木时刃切面朝下）。
                // 主刃体（厚方块，接把手顶向右延展）
                R(14, 6, 8, 6, head)
                R(15, 5, 6, 1, headLight)   // 顶面高光
                R(14, 6, 1, 6, headDark)    // 左侧描边（接柄侧阴影）
                // 刃口（右下弧形收窄，砍切面）
                R(22, 8, 2, 4, head)
                R(22, 12, 2, 1, headDark)   // 刃尖底描边
                R(23, 9, 1, 3, headLight)   // 刃口受光（金属反光面）
            } else if (root.toolType === 4) {
                // 铲头：以把手顶 (14,10) 为根，方形铲斗向右上方伸出（宽扁方铲）。
                //   铲特征：方形/梯形铲斗（掘土容器），前端平直刃口（区别于斧的弧形刃 / 锄的尖勾）。
                // 主铲体（方斗，接把手顶向右延展）
                R(14, 6, 9, 6, head)
                R(15, 5, 7, 1, headLight)   // 顶面高光
                R(14, 6, 1, 6, headDark)    // 左侧描边
                R(14, 12, 9, 1, headDark)   // 底刃口描边（平直铲刃）
                R(23, 6, 1, 6, headDark)    // 右侧描边
            } else if (root.toolType === 5) {
                // 剑（纵向长刃 + 护手 + 柄）：整把垂直，刃占上半（tier 色），柄在下半（木色）。
                //   剑特征：纵向对称长刃（攻击武器），区别于工具的对角柄 + 侧伸头。
                // 剑刃（上半，tier 金属色；从刃尖到护手）
                R(11, 2, 3, 12, head)
                R(12, 1, 1, 2, head)         // 刃尖收窄
                R(12, 2, 1, 11, headLight)   // 刃脊高光（中央竖线）
                R(10, 3, 1, 10, headDark)    // 刃左描边
                R(14, 3, 1, 10, headDark)    // 刃右描边
                // 护手（横向短梁，木色，刃与柄之间）
                R(8, 14, 9, 2, handle)
                R(8, 14, 9, 1, light)        // 护手顶受光
                R(8, 15, 9, 1, handleDark)   // 护手底描边
                // 剑柄（下半，木色短柄）
                R(11, 16, 3, 5, handle)
                R(11, 16, 1, 5, light)       // 柄左受光
                R(13, 16, 1, 5, handleDark)  // 柄右阴影
                // 柄首圆头（柄底防滑球，木色）
                R(10, 21, 5, 2, handle)
                R(10, 21, 5, 1, light)
                R(10, 22, 5, 1, handleDark)
            } else {
                // 镐头（默认）：以把手顶 (14,10) 为中心，水平横梁 + 两端下勾（像素镐经典轮廓）
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
}
