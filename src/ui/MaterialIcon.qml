import QtQuick
// t420 资源包物品图标覆盖：用到模块 C++ 类型 ResourcePackManager（子目录文件须显式 import 自身模块，t41）。
import VoxelSandbox

// 材料图标（t50）：自绘原创像素图（§9 override (a)，Canvas；非 MC 资产 PNG）。
//
// 按 materialId 分支画形状（id 取自 src/Game/recipe.h RecipeRegistry::*Id；QML 无法直接 import
// C++ constexpr，故用同源字面量 + 注释钉死，改一处须同步 recipe.h）：
//   0x200 木棒   —— 斜置棕色长条（原木亮面 + 暗面 + 端面收口）。
//   0x201 煤炭   —— 黑色八边形块（顶面高光 + 底面阴影 + 矿物亮斑）。
//   0x202 铁原矿 —— 石质八边形块 + 橙棕色铁矿斑簇（表「含铁」，与铁矿石贴图同语义）。
//   0x203 铁锭   —— 银白水平梯形锭（顶受光 + 底阴影 + 两端斜切暗边）。
//   0x204 玻璃   —— 浅青半透方块（高光斜面 + 暗边框，表「透明硅块」；t87 沙子冶炼产物）。
//   0x205 木炭   —— 深棕黑八边形块（与煤同形、偏暖棕色，表「烧过的木」；t87 原木冶炼产物）。
//   0x206 铁桶（空）—— 灰金属桶身 + 提手弧 + 桶口椭圆（t174；机制等价 MC 铁桶，纯原创自绘）。
//   0x207 装水铁桶 —— 同空桶 + 桶内青蓝水液面（t174；机制等价 MC 装水铁桶）。
//   0x208 小麦种子 —— 几粒黄褐色麦种 + 胚芽细尖（t235；挖草丛掉落；种植 → 小麦作物 t236）。
//   0x209 小麦物品 —— 金黄麦穗（中央穗轴 + 两侧成对麦粒 + 淡黄麦秆）（t237；收割成熟小麦作物掉落）。
//   0x20A 面包   —— 金棕长条面包（顶弧 + 斜划口 + 两端圆收）（t238；3 小麦合成；右键食 +5 饥饿）。
//   0x20B 生猪排 —— 浅粉红肉块 + 白骨柄（t242；杀猪掉落，带骨肉排）。
//   0x20C 生牛肉 —— 深红肉块 + 白骨柄（t242；杀牛掉落，比猪排深红、肌理纹更密）。
//   0x20D 皮革   —— 棕黄兽皮 + 毛边 + 缝线孔（t242；杀牛掉落，鞣制皮革）。
//   0x20E 羊毛   —— 白色蓬松块 + 卷曲纹（t242；杀羊掉落，绒毛团）。
//   0x20F 生物蛋（猪）—— 粉红蛋壳 + 居中猪鼻纹样（t243；t251 重做：原共用奶白壳仅小斑难辨 → 改主色+强身份纹）。
//   0x210 生物蛋（牛）—— 棕底 + 大块白花斑（牛皮纹）+ 顶两小角（t243；t251 重做）。
//   0x211 生物蛋（羊）—— 奶白蛋壳 + 满布灰卷绒纹（羊毛卷）（t243；t251 重做）。
//   0x213 生物蛋（蹒跚者）—— 暗绿腐肉壳 + 棕褐腐斑 + 两颗红眼（t287/t303；机制等价僵尸 spawn egg）。
//   0x214 生物蛋（骸骨）—— 灰白骨壳 + 黑色眼窝 + 纵向肋骨纹（t287/t303；机制等价骷髅 spawn egg）。
//   0x215 生物蛋（潜行者）—— 深绿壳 + 浅绿迷彩斑 + 顶黄色火星（t287/t303；机制等价苦力怕 spawn egg；纯原创抽象配色，非 MC 苦力怕脸贴图）。
//   0x216 生物蛋（蜘蛛）—— 近黑壳 + 多颗红眼 + 两侧伸出的腿线（t285/t303；机制等价蜘蛛 spawn egg）。
//   0x212 钻石 —— 青白多面切割宝石（八边形轮廓 + 顶面高光菱 + 底阴影 + 几道切面棱线）（t279；机制等价 MC 钻石矿掉落）。
//   0x21C 铜原矿 —— 石质八边形块 + 橙铜斑簇 + 少量孔雀绿锈（t308；铜矿采下为原矿，须熔炉烧铜锭；区别于铁原矿的纯棕橙）。
//   0x21D 铜锭 —— 橙铜水平梯形锭（顶受光 + 底阴影 + 两端斜切暗边）（t308；铜原矿冶炼产物；铜工具 / 装备原料）。
//   0x21E 金原矿 —— 石质八边形块 + 金黄斑簇（t308；金矿采下为原矿，须熔炉烧金锭；最亮最暖的矿石族一眼可辨）。
//   0x21F 金锭 —— 金黄水平梯形锭（顶受光 + 底阴影 + 两端斜切暗边）（t308；金原矿冶炼产物；金工具 / 装备 / 钟原料）。
//   0x217 骨头 —— 米白骨段 + 两端膨节（骨骺）+ 髓腔暗点（t299；杀骸骨掉落，机制等价 MC 骨头）。
//   0x218 腐肉 —— 暗红褐腐块 + 绿霉斑 + 腐败裂痕（t299；杀蹒跚者掉落，区别于生牛肉的鲜亮红 + 横纹）。
//   0x219 线 —— 浅米黄缠绕线团（多圈同心椭圆 + 交叉亮丝）（t299；杀蜘蛛掉落，弓 / 钓竿原料）。
// 木棒既非方块（无等距立方体 PNG）也非工具（非 ToolIcon 镐形），煤/铁原矿/铁锭/玻璃/木炭/桶同理 → 均独立自绘。
//
// 消费点：Main.qml 的游戏内 hotbar delegate / 光标手持浮动图标 / 掉落实体 Repeater（sourceItem），
// Inventory.qml / SurvivalInventory.qml / CraftingTableUI.qml 各槽 —— 凡 hotbarVM.isMaterial(id) 为真
// 的槽位用本组件替代方块 Image / ToolIcon。新增材料在此 switch 加一分支即可全工程生效。
Item {
    id: root
    property int materialId: 0 // 材料段 id（0x200 木棒 / 0x201 煤 / 0x202 铁原矿 / 0x203 铁锭 / 0x204 玻璃 / 0x205 木炭 / 0x206 铁桶 / 0x207 装水铁桶 / 0x208 小麦种子 / 0x209 小麦物品 / 0x20A 面包 / 0x20B 生猪排 / 0x20C 生牛肉 / 0x20D 皮革 / 0x20E 羊毛 / 0x20F 生物蛋（猪）/ 0x210 生物蛋（牛）/ 0x211 生物蛋（羊）/ 0x213 生物蛋（蹒跚者）/ 0x214 生物蛋（骸骨）/ 0x215 生物蛋（潜行者）/ 0x216 生物蛋（蜘蛛）/ 0x212 钻石 / 0x217 骨头 / 0x218 腐肉 / 0x219 线 / 0x21A 箭（t304）/ 0x21B 树苗物品（t305）/ 0x21C 铜原矿 / 0x21D 铜锭 / 0x21E 金原矿 / 0x21F 金锭（t308）/ 0x221 熟猪排 / 0x222 熟牛肉 / 0x223 熟羊肉（t344）/ 0x224 红石粉 / 0x225 马鞍 / 0x226 命名牌 / 0x227 附魔书占位（t393 战利品表）；0/未知 → 兜底木棒）

    // t420 资源包物品图标覆盖：pack 启用且 materialId 在「引擎物品 id → pack item 文件名」映射内、且包内 PNG
    //   存在时，用 pack 的 item PNG 覆盖自绘 Canvas；pack 关 / 无映射 → packImg.source 空 → Image 隐藏、Canvas
    //   自绘（现状不变）。materialId 即引擎物品 id（材料段 + 护甲段共用本组件），直接查 pack。红线 §9：仅运行期
    //   读本地 gitignored pack 路径 PNG，不 bake 进 qrc/VCS。
    ResourcePackManager { id: rp }
    Image {
        id: packImg
        anchors.fill: parent
        visible: source.length > 0
        // 触碰 materialId/rp.active 建立绑定依赖（id 变 / pack 切换 → 重查 pack 源）。
        source: { root.materialId; rp.active; return rp.itemIconSource(root.materialId) }
        fillMode: Image.PreserveAspectFit
        smooth: false // 像素硬边（同 Canvas imageSmoothingEnabled=false；MC item 图标为像素艺术）
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        // t424: 同 ToolIcon。Image.source 是 url，空 url 的 .length 在 QML JS 为 undefined（非 0）→
        //   `=== 0` 恒 false → canvas 永隐 → 所有非 pack 材料 / 护甲图标空白（创造调色板 hover 名仍对）。
        //   改 `!packImg.visible`（packImg.visible 走 `> 0`，对空 url = undefined>false=false 正确），互补且稳健。
        visible: !packImg.visible
        onWidthChanged: requestPaint()
        onHeightChanged: request_paint_safe() // 防 0 尺寸
        Component.onCompleted: requestPaint()
        // t424: canvas 由隐藏切回显（pack 切换 / 槽位换物后回退自绘）时不自动重绘 → 显式重绘防空白。
        onVisibleChanged: if (visible) request_paint_safe()
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

            // 玻璃（0x204）：浅青半透方块。MC 风格玻璃为青白色透明硅块 + 高光斜面 + 暗边框。
            // 配色：face #b8d8e0（青白主体）/ light #e0f0f4（左上高光）/ edge #6a8a92（边框暗边）/
            // streak #f8ffff（高光斜线）。半透感靠浅色 + 高光表达（Canvas 无 alpha 渐变需求）。
            const drawGlass = () => {
                const face = "#b8d8e0", light = "#e0f0f4", edge = "#6a8a92", streak = "#f8ffff"
                // 方块主体（rows 6..18，cols 5..19，15×13）
                R(5, 6, 15, 13, face)
                // 左上高光（受光面，前两行两列）
                R(5, 6, 15, 1, light)
                R(5, 7, 2, 1, light)
                R(5, 6, 1, 4, light)
                // 暗边框（底 / 右，表玻璃块边棱）
                R(5, 18, 15, 1, edge)
                R(19, 6, 1, 13, edge)
                // 高光斜线（左上→右下，表玻璃反光）
                R(8, 9, 1, 1, streak)
                R(9, 10, 1, 1, streak)
                R(10, 11, 1, 1, streak)
                R(11, 12, 1, 1, streak)
            }

            // 木炭（0x205）：深棕黑八边形块。与煤同外轮廓（都是「烧过的碳块」族），靠偏暖深棕色
            // + 木纹裂痕区分「木炭」（vs 煤的纯黑）。配色：body #2a1e16 / light #3e2e22 / dark #160e08 /
            // streak #4a3424（木纹亮痕，表「木的碳化残留」）。
            const drawCharcoal = () => {
                const body = "#2a1e16", light = "#3e2e22", dark = "#160e08", streak = "#4a3424"
                // 八边形外轮廓（同 coal）
                R(7, 7, 11, 1, body)
                R(6, 8, 13, 1, body)
                R(5, 9, 15, 7, body)   // 主体 rows 9..15
                R(6, 16, 13, 1, body)
                R(7, 17, 11, 1, body)
                // 顶面受光（暖棕）
                R(7, 7, 11, 1, light)
                R(6, 8, 13, 1, light)
                R(5, 9, 4, 1, light)
                // 底面阴影
                R(6, 16, 13, 1, dark)
                R(7, 17, 11, 1, dark)
                R(11, 15, 4, 1, dark)
                // 木纹裂痕（几道亮痕，表「碳化的木纹」——与煤的纯矿物亮斑区分）
                R(8, 11, 3, 1, streak)
                R(12, 12, 2, 1, streak)
                R(9, 14, 4, 1, streak)
                R(14, 10, 1, 1, streak)
            }

            // 铁桶（空，0x206）：MC 风格铁桶 = 梯形桶身（上宽下窄）+ 顶部桶口椭圆 + 一侧提手弧。
            //   纯原创自绘（§9a）；灰金属配色（与铁锭同族），桶口深色椭圆表「开口」。
            //   配色：body #b0b0b8（桶身亮面）/ dark #707078（桶身暗面 + 底）/ edge #505058（桶口暗边）/
            //   handle #98989c（提手弧）/ mouth #3a3a42（桶口内阴影）。
            const drawBucketEmpty = () => {
                const body = "#b0b0b8", dark = "#707078", edge = "#505058", handle = "#98989c", mouth = "#3a3a42"
                // 桶身梯形（上宽 14 / 下宽 11，rows 10..18）
                R(5, 10, 14, 1, body)    // 顶行（桶口下沿）
                R(5, 11, 14, 6, body)    // 主体 rows 11..16
                R(6, 17, 12, 1, body)
                R(7, 18, 10, 1, dark)    // 底（暗）
                // 桶身右侧暗面（圆柱明暗）
                R(16, 11, 2, 6, dark)
                R(15, 17, 2, 1, dark)
                // 桶口椭圆（顶部，rows 8..9；宽于桶身顶 → 表「外翻桶口」）
                R(5, 8, 14, 1, edge)
                R(6, 9, 12, 1, mouth)    // 桶口内阴影（深色）
                // 提手弧（左上 → 右上，跨桶口上方）
                R(6, 6, 1, 2, handle)
                R(7, 5, 10, 1, handle)
                R(17, 6, 1, 2, handle)
            }

            // 装水铁桶（0x207）：同空桶 + 桶内青蓝水液面（桶口椭圆内填水色，表「装满水」）。
            //   水色与 default_water 蓝半透基调一致（#3a6ac8 浅版，表水面受光）。提手同空桶。
            const drawWaterBucket = () => {
                drawBucketEmpty() // 桶身（同空桶）
                const water = "#3a6ac8", waterLight = "#5878d8"
                // 桶口椭圆内的水液面（覆盖 mouth 阴影，表「装满水到桶口」）
                R(6, 9, 12, 1, water)
                R(7, 9, 4, 1, waterLight) // 水面高光（左上受光）
            }

            // 装岩浆铁桶（0x220，t343）：同空桶 + 桶内炽热岩浆液面（橙红 + 亮黄鼓泡，表「装满岩浆到桶口」）。
            //   岩浆色与 default_lava 暖橙基调一致（#c8401a 深红橙 + #ffb040 亮黄橙鼓泡）。机制等价 MC 装岩浆铁桶。
            const drawLavaBucket = () => {
                drawBucketEmpty() // 桶身（同空桶）
                const lava = "#c8401a", lavaHot = "#ffb040"
                // 桶口椭圆内的岩浆液面（覆盖 mouth 阴影，表「装满岩浆到桶口」）
                R(6, 9, 12, 1, lava)
                R(8, 9, 2, 1, lavaHot)  // 亮黄橙鼓泡高光（炽热核心）
                R(13, 9, 2, 1, lavaHot) // 另一鼓泡
            }

            // 小麦种子（0x208，t235）：几粒黄褐色麦种（椭圆粒 + 胚芽细尖）。机制等价 MC 小麦种子图标
            //   （麦粒形）；纯原创自绘（§9a）。配色：seed #c8a868（麦粒暖黄褐）/ light #e0c890（受光高光）/
            //   dark #8a6c38（阴影 + 胚沟）/ tip #6a8a3a（胚芽尖淡绿，表「将萌发」）。3 粒聚拢呈种子堆。
            const drawSeed = () => {
                const seed = "#c8a868", light = "#e0c890", dark = "#8a6c38", tip = "#6a8a3a"
                // 单粒麦种（椭圆 + 胚芽尖）：以 为中心画一粒。
                const grain = (cx, cy) => {
                    // 椭圆主体（4×6，竖向麦粒）
                    R(cx - 2, cy - 3, 4, 6, seed)
                    R(cx - 1, cy - 3, 2, 1, light)    // 顶受光
                    R(cx - 2, cy + 2, 4, 1, dark)     // 底阴影
                    R(cx + 1, cy - 3, 1, 6, dark)     // 右暗边（圆柱明暗）
                    R(cx, cy - 4, 1, 1, tip)          // 胚芽尖（顶端淡绿小点，表「将萌发」）
                }
                grain(8, 12)   // 主粒（中央偏左）
                grain(14, 10)  // 右上粒
                grain(12, 15)  // 右下粒
            }

            // 小麦物品（0x209，t237）：收割成熟小麦作物掉落。MC 风格小麦 = 金黄麦穗（中央穗轴 + 两侧成对麦粒
            //   + 下方淡黄麦秆）。机制等价 MC 小麦物品图标（一把麦穗）；纯原创自绘（§9a）。
            //   配色：grain #e8c860（麦粒金黄）/ grainLight #f8e890（受光高光）/ grainDark #b89838（穗轴 +
            //   麦粒暗边）/ stem #d8b878（麦秆淡黄）/ stemDark #a88848（麦秆暗面）。
            const drawWheat = () => {
                const grain = "#e8c860", grainLight = "#f8e890", grainDark = "#b89838"
                const stem = "#d8b878", stemDark = "#a88848"
                // 麦秆（中央竖线，rows 13..19；右 1px 暗面表圆柱明暗）
                R(11, 13, 2, 7, stem)
                R(12, 13, 1, 7, stemDark)
                // 麦穗中央穗轴（rows 5..12，比麦粒略深，串起两侧颗粒）
                R(11, 5, 2, 8, grainDark)
                // 麦粒：两侧成对斜颗粒（4 对，左右各一粒）。每粒 = 3×2 金黄椭圆 + 左上高光 + 右下暗边
                const ear = (cx, cy) => {
                    R(cx, cy, 3, 2, grain)
                    R(cx, cy, 1, 1, grainLight)        // 左上受光
                    R(cx + 2, cy + 1, 1, 1, grainDark) // 右下暗
                }
                ear(7, 6);  ear(14, 6)   // 第 1 对（最顶）
                ear(7, 8);  ear(14, 8)   // 第 2 对
                ear(7, 10); ear(14, 10)  // 第 3 对
                ear(7, 12); ear(14, 12)  // 第 4 对（最底，接麦秆）
                // 穗尖封粒（顶部 1 粒居中，收口麦穗顶端）
                R(10, 4, 4, 2, grain)
                R(10, 4, 2, 1, grainLight)
            }

            // 胡萝卜（0x22F，t400）：猪繁殖食物。MC 风格胡萝卜 = 橙红锥形根（上宽下尖）+ 顶端绿缨。
            //   机制等价 MC 胡萝卜图标；纯原创自绘（§9a）。配色：root #e8782a（橙红根）/ rootLight #f8a050
            //   （受光高光）/ rootDark #b04810（根尖 + 暗边 + 横纹）/ leaf #4a8a3a（绿缨）/ leafDark #2a5a1a。
            const drawCarrot = () => {
                const root = "#e8782a", rootLight = "#f8a050", rootDark = "#b04810"
                const leaf = "#4a8a3a", leafDark = "#2a5a1a"
                // 绿缨（顶端 3-4 条细叶，rows 3..6）
                R(8, 3, 1, 4, leaf);  R(11, 2, 2, 5, leaf);  R(14, 3, 1, 4, leaf)
                R(11, 2, 2, 1, leafDark) // 缨底暗收
                // 根主体（上宽下尖的锥：rows 7..18，逐行收窄）
                R(8, 7, 8, 2, root)        // 顶最宽（rows 7..8）
                R(9, 9, 6, 3, root)        // rows 9..11
                R(9, 12, 6, 3, root)       // rows 12..14
                R(10, 15, 4, 2, root)      // rows 15..16
                R(11, 17, 2, 2, root)      // rows 17..18（收尖）
                // 受光高光（左上橙亮带，表圆柱明暗）
                R(8, 7, 2, 6, rootLight)
                R(9, 13, 1, 2, rootLight)
                // 暗边 + 横纹（右侧 + 几道横纹表「根皮纹理」）
                R(14, 7, 2, 8, rootDark)   // 右暗边
                R(8, 10, 8, 1, rootDark)   // 横纹 1
                R(9, 13, 6, 1, rootDark)   // 横纹 2
            }

            // 马铃薯（0x230，t400）：猪繁殖食物。MC 风格马铃薯 = 棕黄椭圆块茎 + 凹凸「芽眼」斑点。
            //   机制等价 MC 马铃薯图标；纯原创自绘（§9a）。配色：skin #c89858（棕黄皮）/ skinLight #e0b878
            //   （受光高光）/ skinDark #8a6028（暗边 + 芽眼）/ eye #6a4818（深芽眼点）。
            const drawPotato = () => {
                const skin = "#c89858", skinLight = "#e0b878", skinDark = "#8a6028", eye = "#6a4818"
                // 块茎主体（椭圆，rows 6..17，两端收窄表「肾形块茎」）
                R(8, 6, 8, 1, skin)        // 顶行（窄）
                R(6, 7, 12, 2, skin)       // rows 7..8（宽）
                R(5, 9, 14, 6, skin)       // 主体 rows 9..14（最宽）
                R(6, 15, 12, 2, skin)      // rows 15..16（宽）
                R(8, 17, 8, 1, skinDark)   // 底行（窄 + 暗阴影）
                // 受光高光（左上亮带，表「椭圆凸面」）
                R(8, 6, 8, 1, skinLight)
                R(6, 7, 4, 2, skinLight)
                R(5, 9, 4, 3, skinLight)
                // 两端圆收（左 / 右各暗一格，表「圆头收口」）
                R(5, 10, 1, 4, skinDark)
                R(18, 10, 1, 4, skinDark)
                // 芽眼（3 个深色小斑点，表「块茎芽眼凹」）
                R(9, 10, 2, 1, eye)
                R(13, 12, 2, 1, eye)
                R(10, 14, 2, 1, eye)
            }

            // 面包（0x20A，t238）：3 小麦合成；右键食 +5 饥饿。MC 风格面包 = 金棕长条（顶弧 + 斜划口 +
            //   两端圆收）。机制等价 MC 面包图标（一块烤面包）；纯原创自绘（§9a）。
            //   配色：crust #c88848（面包皮金棕，主体）/ crustLight #e0a868（顶弧受光高光）/ crustDark
            //   #8a5828（底阴影 + 划口暗缝）/ score #6a3818（斜划口深棕，表「烤痕」）。
            const drawBread = () => {
                const crust = "#c88848", crustLight = "#e0a868", crustDark = "#8a5828", score = "#6a3818"
                // 面包体（rows 8..16，cols 4..19，长条略呈椭圆；两端收窄表圆头）
                R(6, 8, 12, 1, crust)       // 顶行（窄）
                R(5, 9, 14, 1, crust)       // 第二行（宽）
                R(4, 10, 16, 5, crust)      // 主体 rows 10..14
                R(5, 15, 14, 1, crust)      // 倒数第二行（宽）
                R(6, 16, 12, 1, crustDark)  // 底行（窄 + 暗阴影）
                // 顶弧受光（前两行亮色，表「圆顶反光」）
                R(6, 8, 12, 1, crustLight)
                R(5, 9, 14, 1, crustLight)
                R(4, 10, 5, 1, crustLight)  // 左上高光
                // 两端圆收（左 / 右各暗一格，表「圆头收口」）
                R(4, 11, 1, 3, crustDark)
                R(19, 11, 1, 3, crustDark)
                // 斜划口（3 道斜线，表「烤面包划痕」；每道 1 像素宽、斜置）
                R(8, 10, 2, 1, score)
                R(11, 9, 2, 1, score)
                R(14, 8, 2, 1, score)
            }

            // t242 mob 死亡掉落物（杀猪 / 牛 / 羊产出；机制等价 MC 1.0 生肉 / 皮革 / 羊毛，纯原创自绘 §9a）：
            //   生猪排 0x20B —— 浅粉红肉块 + 白骨柄（猪排 = 带骨肉块，色比牛肉浅、粉调更重）。
            //   生牛肉 0x20C —— 深红肉块 + 白骨柄（比猪排深红、肌理横纹）。
            //   皮革 0x20D —— 棕黄兽皮（带毛边 + 缝线孔，表「鞣制皮革」）。
            //   羊毛 0x20E —— 白色蓬松块（圆角 + 浅灰阴影表「绒毛团」）。
            //   肉块配色统一走「肉色 + 骨头 + 暗红肌理 + 高光」结构，靠主色调（粉 / 红）与肌理密度区分。
            //   drawRawMeat(kind) 共用骨架：kind="pig" 浅粉 / kind="beef" 深红。其余三块独立。
            const drawRawMeat = (kind) => {
                // 主色：猪排 #f09890（浅粉红，带粉调）/ 牛肉 #b03830（深红，饱和）。骨头 #f0e8d8（米白）。
                //   肌理暗纹 dark：猪 #c06058 / 牛 #781818（横纹深红）。高光 light：猪 #f8b8b0 / 牛 #d05048。
                const isPig = (kind === "pig")
                const meat  = isPig ? "#f09890" : "#b03830"
                const dark  = isPig ? "#c06058" : "#781818"
                const light = isPig ? "#f8b8b0" : "#d05048"
                const bone  = "#f0e8d8"
                const boneShade = "#a89878"
                // 肉块主体（rows 7..17，cols 5..18，圆角矩形；左下到右上的对角带骨）
                R(7, 7, 11, 1, meat)        // 顶行（窄）
                R(6, 8, 13, 9, meat)        // 主体 rows 8..16
                R(7, 17, 11, 1, meat)       // 底行（窄）
                // 顶 / 底圆角阴影
                R(7, 17, 11, 1, dark)
                // 肌理横纹（3-4 道横向暗纹，表「肌肉纤维」；牛肉更多更深）
                R(7, 10, 11, 1, dark)
                R(7, 13, 11, 1, dark)
                if (!isPig) { R(7, 11, 11, 1, dark); R(7, 14, 11, 1, dark) }
                // 高光（顶行亮色，表「湿润反光」）
                R(7, 8, 9, 1, light)
                // 骨柄（左下角到右上角的对角白骨，表「带骨肉排」）
                R(8, 15, 2, 2, bone)
                R(10, 13, 2, 2, bone)
                R(12, 11, 2, 2, bone)
                R(14, 9, 2, 2, bone)
                R(16, 7, 2, 2, bone)
                // 骨头暗边（圆头收口）
                R(8, 16, 2, 1, boneShade)
                R(16, 8, 2, 1, boneShade)
            }
            const drawRawPorkchop = () => drawRawMeat("pig")
            const drawRawBeef     = () => drawRawMeat("beef")

            // t344 烤肉（mob 燃烧致死掉落；机制等价 MC 1.0 cooked porkchop / beef / mutton，纯原创自绘 §9a）：
            //   与生肉同骨架（肉块 + 骨柄）但主色为「烤熟的褐色」（棕褐焦边 + 深褐肌理），一眼辨「熟」（vs 生肉鲜红/粉）。
            //   drawCookedMeat(kind) 共用骨架：kind="pig" 浅褐（猪排烤后偏粉褐）/ "beef" 深褐（牛肉烤后深棕）/
            //   "mutton" 中褐（羊肉烤后棕褐；无骨柄，整块烤肉）。骨柄米白（猪/牛带骨烤排；mutton 无骨）。
            const drawCookedMeat = (kind) => {
                // 主色：猪排 #a86848（烤后浅褐）/ 牛肉 #6a3818（深棕）/ 羊肉 #8a5028（中褐）。焦边 char #3a1a08。
                //   高光 light：猪 #c08868 / 牛 #8a5028 / 羊 #a86838（受光面，表「烤出的油亮」）。骨 bone #f0e8d8。
                const isPig = (kind === "pig"), isBeef = (kind === "beef")
                const meat  = isPig ? "#a86848" : isBeef ? "#6a3818" : "#8a5028"
                const dark  = isPig ? "#704028" : isBeef ? "#3a1a08" : "#5a3018"
                const light = isPig ? "#c08868" : isBeef ? "#8a5028" : "#a86838"
                const char  = "#3a1a08"
                const bone  = "#f0e8d8", boneShade = "#a89878"
                // 肉块主体（rows 7..17，cols 5..18，圆角矩形）
                R(7, 7, 11, 1, meat)
                R(6, 8, 13, 9, meat)        // 主体 rows 8..16
                R(7, 17, 11, 1, meat)
                // 底焦边（深褐，表「烤焦的底面」）
                R(7, 17, 11, 1, dark)
                // 烤痕（几道更深褐横向纹，表「烤出的焦纹」；牛肉最多最深）
                R(7, 10, 11, 1, dark)
                R(7, 13, 11, 1, dark)
                if (isBeef) { R(7, 11, 11, 1, char); R(7, 14, 11, 1, char) }
                // 高光（顶行油亮，表「烤出的油脂反光」）
                R(7, 8, 9, 1, light)
                if (!isBeef && !isPig) {
                    // mutton：整块烤肉无骨柄 → 顶面再加几粒深褐焦斑表「羊肉膻味焦皮」
                    R(9, 9, 1, 1, char); R(13, 12, 1, 1, char); R(11, 15, 1, 1, char)
                    return // mutton 无骨柄，到此结束
                }
                // 骨柄（左下到右上对角白骨，表「带骨烤排」——猪 / 牛带骨，mutton 无骨已 return）
                R(8, 15, 2, 2, bone)
                R(10, 13, 2, 2, bone)
                R(12, 11, 2, 2, bone)
                R(14, 9, 2, 2, bone)
                R(16, 7, 2, 2, bone)
                R(8, 16, 2, 1, boneShade)
                R(16, 8, 2, 1, boneShade)
            }
            const drawCookedPorkchop = () => drawCookedMeat("pig")
            const drawCookedBeef     = () => drawCookedMeat("beef")
            const drawCookedMutton   = () => drawCookedMeat("mutton")

            // 皮革（0x20D，t242）：杀牛掉落。MC 风格皮革 = 棕黄兽皮（带毛边 + 缝线孔）。纯原创自绘（§9a）。
            //   配色：hide #a87838（棕黄主体）/ hideLight #c89858（顶受光高光）/ hideDark #785020（暗边 + 毛尖）/
            //   stitch #4a3010（缝线孔深棕）。
            const drawLeather = () => {
                const hide = "#a87838", hideLight = "#c89858", hideDark = "#785020", stitch = "#4a3010"
                // 兽皮主体（不规则四边形，左上到右下倾斜；rows 7..17）
                R(6, 7, 12, 1, hide)
                R(5, 8, 14, 9, hide)        // 主体 rows 8..16
                R(6, 17, 12, 1, hide)
                // 顶受光高光（前两行 + 左上角）
                R(6, 7, 12, 1, hideLight)
                R(5, 8, 10, 1, hideLight)
                // 毛边（上下边沿的小三角毛尖，表「兽皮未修剪」）
                R(6, 6, 1, 1, hideDark); R(10, 6, 1, 1, hideDark); R(14, 6, 1, 1, hideDark)
                R(7, 18, 1, 1, hideDark); R(11, 18, 1, 1, hideDark); R(15, 18, 1, 1, hideDark)
                // 底阴影
                R(6, 17, 12, 1, hideDark)
                // 缝线孔（沿边缘等距分布的小深棕点，表「鞣制皮革的缝线孔」）
                R(7, 9, 1, 1, stitch); R(7, 12, 1, 1, stitch); R(7, 15, 1, 1, stitch)
                R(17, 9, 1, 1, stitch); R(17, 12, 1, 1, stitch); R(17, 15, 1, 1, stitch)
            }

            // 羊毛（0x20E，t242）：杀羊掉落。MC 风格羊毛 = 白色蓬松块（圆角 + 浅灰阴影表「绒毛团」）。
            //   纯原创自绘（§9a）。配色：wool #f0f0f0（白主体）/ woolLight #ffffff（顶高光）/ woolShade
            //   #c8c8c8（底阴影 + 卷曲纹）。
            const drawWool = () => {
                const wool = "#f0f0f0", woolLight = "#ffffff", woolShade = "#c8c8c8"
                // 蓬松块主体（圆角矩形，rows 7..17；四角内收表「蓬松圆团」）
                R(8, 7, 8, 1, wool)
                R(6, 8, 12, 9, wool)        // 主体 rows 8..16
                R(8, 17, 8, 1, wool)
                // 顶受光高光（顶行 + 中央亮带，表「绒毛反光」）
                R(8, 7, 8, 1, woolLight)
                R(8, 9, 8, 1, woolLight)
                // 底阴影（倒数两行）
                R(8, 16, 8, 1, woolShade)
                R(8, 17, 8, 1, woolShade)
                // 卷曲纹（散布的浅灰小点，表「羊毛卷曲」）
                R(8, 10, 1, 1, woolShade); R(12, 11, 1, 1, woolShade); R(15, 10, 1, 1, woolShade)
                R(9, 13, 1, 1, woolShade); R(13, 14, 1, 1, woolShade); R(16, 13, 1, 1, woolShade)
                R(10, 15, 1, 1, woolShade); R(14, 12, 1, 1, woolShade)
                // 四角圆收（暗一格表「圆角」）
                R(6, 8, 1, 1, woolShade); R(17, 8, 1, 1, woolShade)
                R(6, 16, 1, 1, woolShade); R(17, 16, 1, 1, woolShade)
            }

            // t243 生物蛋（spawn eggs）：创造模式物品，右键地面 → 生成对应被动生物。纯原创自绘（§9a）。
            //   t251 重做：原版三蛋共用奶白蛋壳 + 仅小斑点色区分 → 肉眼难辨（用户反馈「3 蛋贴图难辨」）。
            //   改为「蛋壳主色 = mob 主色 + 强身份纹样」三蛋一眼可辨（蛋形外轮廓三蛋一致，靠主色 + 纹样区分）：
            //     pig = 粉红蛋壳 + 居中猪鼻（椭圆鼻头 + 两鼻孔）；cow = 棕底 + 大块白花斑（牛皮纹）+ 顶两小角；
            //     sheep = 奶白蛋壳 + 满布灰卷绒纹（羊毛卷）。
            //   t303 扩展敌对四蛋（t287 引入物品但 MaterialIcon 无 case → 落 default drawStick 显木棒；用户「蛋显方块」
            //     泛指非蛋形）。沿用同一蛋形外轮廓 + 主色取各 mob 渲染色（playerController.cpp 蛋→mobType 映射），
            //     靠主色 + 身份纹样区分（§9 区隔：纯原创抽象纹样，非 MC 苦力怕脸 / 僵尸皮贴图；机制对齐即可）：
            //     shambler = 暗绿腐肉壳 + 棕褐腐斑 + 两颗红眼（僵尸眼）；bones = 灰白骨壳 + 黑眼窝 + 纵向肋骨纹；
            //     stalker = 深绿壳 + 浅绿迷彩斑 + 顶黄色火星（引信/爆炸身份，非苦力怕脸）；spider = 近黑壳 + 红眼簇 + 两侧腿线。
            //   蛋形 = 上下收窄椭圆（rows 6..18、中部最宽 12）；纹样固定坐标散布（确定性，非随机源）。
            const drawSpawnEgg = (kind) => {
                // 蛋形外轮廓（上下收窄椭圆；三蛋共用）。fillShell 据主色填蛋壳。
                const fillShell = (shell) => {
                    R(9, 6, 6, 1, shell)    // 顶行（窄）
                    R(7, 7, 10, 1, shell)
                    R(6, 8, 12, 9, shell)   // 主体 rows 8..16（最宽）
                    R(7, 17, 10, 1, shell)
                    R(9, 18, 6, 1, shell)   // 底行（圆收）
                }
                if (kind === "pig") {
                    // 猪：粉红蛋壳 + 顶高光 + 底暗影 + 居中猪鼻纹样（强身份，粉蛋 + 猪鼻一眼辨「猪」）。
                    const shell = "#f0a8b0", lite = "#f8c4c8", dark = "#c87888"
                    const snout = "#e08890", nostril = "#7a3848"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光（圆顶反光）
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影（体积感）
                    R(15, 9, 1, 7, dark)                               // 右暗边（圆柱明暗）
                    // 猪鼻（居中偏下，椭圆鼻头 + 两鼻孔）
                    R(10, 12, 4, 3, snout)    // 鼻头（椭圆块）
                    R(10, 12, 4, 1, dark)     // 鼻头上沿暗（立体）
                    R(11, 13, 1, 1, nostril)  // 左鼻孔
                    R(12, 13, 1, 1, nostril)  // 右鼻孔
                } else if (kind === "cow") {
                    // 牛：棕底 + 大块白花斑（牛皮纹）+ 顶两小角（棕白花 + 角一眼辨「牛」）。
                    const shell = "#5a4030", lite = "#7a5840", dark = "#3a2818"
                    const white = "#f0e8d8", horn = "#d8c8a8"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    // 顶两小角（蛋顶上方两小尖，牛的身份特征）
                    R(9, 5, 2, 1, horn)
                    R(13, 5, 2, 1, horn)
                    // 大块白花斑（牛皮典型纹，占显著面积）
                    R(7, 9, 4, 3, white)     // 左上花斑
                    R(12, 12, 4, 3, white)   // 右下花斑
                    R(11, 9, 2, 2, white)    // 中上花斑
                    R(8, 14, 2, 2, white)    // 左下小斑
                } else if (kind === "sheep") {
                    // 羊：奶白蛋壳 + 满布灰卷绒纹（羊毛卷）→ 奶白绒面一眼辨「羊」。
                    const shell = "#f5f0e8", lite = "#ffffff", dark = "#d0c8c0"
                    const curl = "#c8c0b8", curlDeep = "#b0a8a0"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    // 满布羊毛卷纹（小灰点散布 rows 9..16，拟绒毛凹凸）
                    const curls = [
                        [8, 9], [11, 9], [13, 9],
                        [7, 11], [10, 11], [12, 11], [14, 11],
                        [8, 13], [11, 13], [13, 13],
                        [9, 15], [12, 15], [14, 15]
                    ]
                    for (const [c, r] of curls) R(c, r, 1, 1, curl)
                    // 少量深卷提层次
                    R(10, 10, 1, 1, curlDeep); R(13, 12, 1, 1, curlDeep); R(8, 14, 1, 1, curlDeep)
                } else if (kind === "shambler") {
                    // 蹒跚者（机制等价僵尸）：暗绿腐肉壳 + 棕褐腐斑 + 两颗红眼（僵尸的红色双眼）
                    //   → 暗绿 + 红眼一眼辨「僵尸类」。纯原创抽象纹样（§9 区隔，非 MC 僵尸皮贴图）。
                    const shell = "#4a6a3a", lite = "#6a8a4a", dark = "#2a4a1a"
                    const rot = "#6a4a2a", eye = "#c83030"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    R(15, 9, 1, 7, dark)                               // 右暗边（圆柱明暗）
                    // 棕褐腐斑（散布的不规则深棕块，表「腐烂溃口」）
                    R(8, 10, 2, 2, rot)
                    R(13, 11, 2, 1, rot)
                    R(10, 14, 2, 1, rot)
                    R(14, 14, 1, 1, rot)
                    // 两颗红眼（居中偏上，僵尸的红色双眼 = 强身份特征）
                    R(10, 8, 1, 1, eye)
                    R(13, 8, 1, 1, eye)
                } else if (kind === "bones") {
                    // 骸骨（机制等价骷髅）：灰白骨壳 + 黑色眼窝 + 纵向肋骨纹
                    //   → 灰白 + 黑眼窝一眼辨「骷髅类」。纯原创抽象纹样（§9 区隔，非 MC 骷髅贴图）。
                    const shell = "#d8d8d0", lite = "#f0f0e8", dark = "#a8a8a0"
                    const rib = "#989890", socket = "#2a2a2a"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    R(15, 9, 1, 7, dark)                               // 右暗边
                    // 黑色眼窝（两个深黑小块，表「空洞眼窝」——骷髅身份）
                    R(9, 9, 2, 2, socket)
                    R(13, 9, 2, 2, socket)
                    // 纵向肋骨纹（几道竖向浅纹，表「肋骨」）
                    R(9, 12, 1, 4, rib)
                    R(12, 12, 1, 4, rib)
                    R(14, 13, 1, 3, rib)
                } else if (kind === "stalker") {
                    // 潜行者（机制等价苦力怕）：深绿壳 + 浅绿迷彩斑 + 顶黄色火星（引信将爆）
                    //   → 深绿 + 顶火星一眼辨「自爆类」。§9 区隔：纯原创抽象配色（迷彩斑 + 引信火星），
                    //   非 MC 苦力怕脸贴图（脸是 MC 强 trade dress，刻意避开；用「迷彩 + 火星」表同类机制）。
                    const shell = "#3a5a3a", lite = "#4a6a4a", dark = "#2a4a2a"
                    const speckle = "#5a7a4a", spark = "#f8d838"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    R(15, 9, 1, 7, dark)                               // 右暗边
                    // 浅绿迷彩斑（散布的浅绿小斑，表「伪装迷彩」——与 shambler 的腐斑区分：更小更密、纯绿系）
                    R(8, 10, 1, 1, speckle); R(11, 9, 1, 1, speckle); R(14, 10, 1, 1, speckle)
                    R(9, 12, 1, 1, speckle); R(13, 12, 1, 1, speckle)
                    R(8, 14, 1, 1, speckle); R(11, 15, 1, 1, speckle); R(14, 14, 1, 1, speckle)
                    // 顶黄色火星（蛋顶上方一格，表「引信将爆」——潜行者的爆炸身份，区别于 shambler 的红眼）
                    R(11, 5, 2, 1, spark)
                } else if (kind === "spider") {
                    // 蜘蛛：近黑壳 + 多颗红眼（蜘蛛复眼）+ 两侧伸出的腿线
                    //   → 黑 + 红眼 + 腿一眼辨「蜘蛛类」。纯原创抽象纹样（§9 区隔）。
                    const shell = "#2a1a1a", lite = "#4a2a2a", dark = "#1a0a0a"
                    const eye = "#c81818", leg = "#1a0a0a"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光（暗红褐提亮）
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    R(15, 9, 1, 7, dark)                               // 右暗边
                    // 多颗红眼（蜘蛛的红色复眼簇，居中偏上）
                    R(9, 9, 1, 1, eye); R(11, 9, 1, 1, eye); R(13, 9, 1, 1, eye)
                    R(10, 10, 1, 1, eye); R(12, 10, 1, 1, eye)
                    // 两侧伸出的腿线（每侧 3 段短暗线，从蛋身向外辐射，表「蜘蛛腿」）
                    R(5, 9, 1, 1, leg)
                    R(4, 11, 2, 1, leg)
                    R(5, 13, 1, 1, leg)
                    R(18, 9, 1, 1, leg)
                    R(18, 11, 2, 1, leg)
                    R(18, 13, 1, 1, leg)
                } else if (kind === "chicken") {
                    // 鸡（t398）：白羽壳 + 棕褐翅斑 + 顶红色鸡冠（鸡冠一眼辨「鸡」）。纯原创抽象纹样（§9 区隔）。
                    const shell = "#f5f0e4", lite = "#ffffff", dark = "#d8d0c2"
                    const brown = "#8a5a32", comb = "#c83030"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    R(15, 9, 1, 7, dark)                               // 右暗边
                    // 棕褐翅斑（散布的棕色小斑，表「翅膀羽色」）
                    R(8, 10, 1, 1, brown); R(11, 9, 1, 1, brown); R(14, 11, 1, 1, brown)
                    R(9, 13, 1, 1, brown); R(13, 14, 1, 1, brown); R(7, 12, 1, 1, brown)
                    // 顶红色鸡冠（蛋顶上方一格，鸡的身份特征）
                    R(10, 5, 4, 1, comb)
                } else if (kind === "squid") {
                    // 鱿鱼（t399）：深褐橘斑壳 + 底部下垂触腕线 + 顶小尖（触腕 + 顶尖一眼辨「鱿鱼」）。
                    //   纯原创抽象纹样（§9 区隔：非 MC 鱿鱼皮；机制对齐即可）。主色取 squid 渲染色 #6a4a3a。
                    const shell = "#6a4a3a", lite = "#8a6a4a", dark = "#3a2a1a"
                    const tentacle = "#3a2a1a"
                    fillShell(shell)
                    R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)          // 顶高光
                    R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark)       // 底暗影
                    R(15, 9, 1, 7, dark)                               // 右暗边
                    // 顶小尖（蛋顶上方一格，鱿鱼 mantle 尖顶身份）
                    R(11, 5, 2, 1, dark)
                    // 底部触腕线（蛋底下方几条短垂线，表「8 触腕下垂」）
                    R(8, 19, 1, 1, tentacle); R(10, 19, 1, 1, tentacle); R(12, 19, 1, 1, tentacle)
                }
            }

            // 钻石（0x212，t279）：钻石矿挖掘掉落（需铁镐）。MC 风格钻石 = 青白多面切割宝石（八边形轮廓 +
            //   顶面高光菱 + 底阴影 + 几道切面棱线表「切割宝石」）。机制等价 MC 钻石图标；纯原创自绘（§9a）。
            //   配色：face #4ad8d8（青白主体）/ light #a8f4f4（顶面高光菱）/ dark #2a8888（底阴影 + 切面暗棱）/
            //   spark #e8ffff（高光闪点）/ edge #1a5a5a（外轮廓暗边，表宝石切边）。
            const drawDiamond = () => {
                const face = "#4ad8d8", light = "#a8f4f4", dark = "#2a8888", spark = "#e8ffff", edge = "#1a5a5a"
                // 宝石八边形外轮廓（行 6..17，最宽 14；上下收窄表「切割宝石」对顶收口）
                R(8, 6, 8, 1, face)
                R(6, 7, 12, 1, face)
                R(5, 8, 14, 8, face)   // 主体 rows 8..15
                R(6, 16, 12, 1, face)
                R(8, 17, 8, 1, face)
                // 顶面高光菱（受光面，顶部菱形 → 表「台面切割」）
                R(8, 6, 8, 1, light)
                R(7, 7, 2, 1, light); R(15, 7, 2, 1, light)
                R(11, 8, 2, 1, light)
                // 底阴影（下两行 + 右下边，表体积）
                R(6, 16, 12, 1, dark)
                R(8, 17, 8, 1, dark)
                R(13, 14, 3, 1, dark)
                // 切面棱线（从中心向四角的细暗线，表「多面切割」立体感）
                R(10, 9, 1, 2, dark); R(14, 9, 1, 2, dark)   // 上两道竖棱
                R(8, 11, 2, 1, dark); R(15, 11, 2, 1, dark)  // 中两道斜棱
                R(9, 13, 1, 1, dark); R(14, 13, 1, 1, dark)  // 下两道斜棱
                // 高光闪点（中心左上一小亮点，表宝石反光）
                R(9, 9, 1, 1, spark)
                // 外轮廓暗边（八边形角顶收口，表「切割宝石棱角」）
                R(7, 6, 1, 1, edge); R(16, 6, 1, 1, edge)
                R(5, 8, 1, 2, edge); R(18, 8, 1, 2, edge)
            }

            // t308 铜/金原矿 + 锭（机制等价 MC 1.0「铜/铁/金矿采下为原矿，须熔炉冶炼成锭」）：
            //   通用骨架（矿石八边形 / 锭水平梯形）与铁原矿 / 铁锭同源，靠主色 + 斑簇色（铜橙 / 金黄）区分。
            //   drawRawOre(light, mid, dark, ore, oreDark, spots, extra?)：石质八边形 + 金属斑簇（每簇 2×2 亮 + 1×1 暗边）+
            //     可选 extra 回调（铜的孔雀绿锈点缀，铁无）。drawIngot(light, mid, dark, edge, hi?)：水平梯形锭 + 两端暗边。
            const drawRawOre = (light, mid, dark, ore, oreDark, spots, extra) => {
                // 石质主体（八边形外轮廓，同 coal / iron ore）
                R(7, 7, 11, 1, mid)
                R(6, 8, 13, 1, mid)
                R(5, 9, 15, 7, mid)   // 主体 rows 9..15
                R(6, 16, 13, 1, mid)
                R(7, 17, 11, 1, mid)
                R(7, 7, 11, 1, light)    // 顶面受光
                R(6, 8, 13, 1, light)
                R(5, 9, 5, 1, light)
                R(6, 16, 13, 1, dark)    // 底面阴影
                R(7, 17, 11, 1, dark)
                // 金属斑簇（散布；每簇 2×2 亮 + 1×1 暗边）
                for (const [c, r] of spots) {
                    R(c, r, 2, 2, ore); R(c + 1, r, 1, 1, oreDark)
                }
                if (extra) extra()
            }
            const drawIngot = (light, mid, dark, edge, hi) => {
                // 梯形锭外轮廓（行 7..14，中部最宽 16）
                R(6, 7, 12, 1, light)    // 顶面（受光）
                R(5, 8, 14, 1, light)
                R(4, 9, 16, 4, mid)      // 主体 rows 9..12
                R(5, 13, 14, 1, dark)
                R(6, 14, 12, 1, dark)    // 底面阴影
                if (hi) R(7, 7, 10, 1, hi)  // 顶面高光线（金属反光）
                R(4, 9, 1, 4, edge)      // 两端斜切暗边
                R(19, 9, 1, 4, edge)
            }

            // 铜原矿（0x21C，t308）：铜矿采下为原矿。石质八边形 + 橙铜斑簇（鲜亮橘橙，比铁更亮更橘）+
            //   少量孔雀绿锈（铜氧化身份证，铁无此色 → 区分铜/铁原矿）。配色：石 light #b8a890 / mid #988868 /
            //   dark #685848；铜斑 ore #d68038 / oreDark #a04818（鲜亮橘橙）；锈 patina #48a88a（孔雀绿）。
            const drawCopperOre = () => {
                const light = "#b8a890", mid = "#988868", dark = "#685848"
                const ore = "#d68038", oreDark = "#a04818", patina = "#48a88a"
                const spots = [[8, 10], [13, 11], [10, 13], [7, 14]]
                drawRawOre(light, mid, dark, ore, oreDark, spots, () => {
                    // 孔雀绿锈点（少量散布，表「铜氧化」—— 铜原矿的身份证，与铁原矿的纯棕橙铁锈区分）
                    R(9, 6, 1, 1, patina); R(12, 9, 1, 1, patina); R(15, 14, 1, 1, patina)
                })
            }
            // 铜锭（0x21D，t308）：铜原矿冶炼产物。水平梯形锭 + 橙铜配色（比铁锭更暖更橘）。配色：light
            //   #f0b878（橙铜受光）/ mid #c87838（橙铜主体）/ dark #8a4818（橙铜阴影）/ edge #5a3008（锭端暗边）/
            //   hi #f8d098（顶面高光，暖亮橙）。
            const drawCopperIngot = () => {
                drawIngot("#f0b878", "#c87838", "#8a4818", "#5a3008", "#f8d098")
            }
            // 金原矿（0x21E，t308）：金矿采下为原矿。石质八边形 + 金黄斑簇（最亮最暖的矿石族一眼可辨，
            //   区别于铜的橘橙 / 铁的棕橙）。配色：石 light #b8a890 / mid #988868 / dark #685848；
            //   金斑 ore #f4c630 / oreDark #b88810（饱和暖金）。
            const drawGoldOre = () => {
                const light = "#b8a890", mid = "#988868", dark = "#685848"
                const ore = "#f4c630", oreDark = "#b88810"
                const spots = [[8, 10], [13, 11], [10, 13], [7, 14], [14, 8]]
                drawRawOre(light, mid, dark, ore, oreDark, spots)
            }
            // 金锭（0x21F，t308）：金原矿冶炼产物。水平梯形锭 + 金黄配色（最亮的锭，区别于铜锭的橘橙 / 铁锭的银白）。
            //   配色：light #fce878（金黄受光）/ mid #e8b820（金黄主体）/ dark #a87810（深金阴影）/ edge #6a4808
            //   （锭端暗边）/ hi #fff8b0（顶面高光，近白金）。
            const drawGoldIngot = () => {
                drawIngot("#fce878", "#e8b820", "#a87810", "#6a4808", "#fff8b0")
            }

            // t299 敌对 mob 死亡掉落物（杀骸骨 / 蹒跚者 / 蜘蛛产出；机制等价 MC 1.0 敌对生物掉落，纯原创自绘 §9a）：
            //   骨头 0x217 —— 经典骨头形：纵向骨干 + 两端膨节（骨骺），米白色 + 暖阴影，一眼辨「骨头」
            //     （区别于皮革的棕黄兽皮 / 羊毛的白色绒团：骨头是硬质骨段 + 膨节，非柔韧皮毛）。
            //   腐肉 0x218 —— 暗红褐腐块 + 绿斑霉点 + 深色腐败裂痕（区别于生牛肉的鲜亮红 + 横纹肌理：
            //     腐肉主色明显更暗、带绿霉，表「变质」，非新鲜肉块）。
            //   线 0x219 —— 浅色缠绕线团（多圈同心椭圆 + 几道交叉亮丝），表「一缕缠起的线」（蜘蛛丝原料）。
            const drawBone = () => {
                // 米白骨干 + 暖阴影 + 两端膨节。配色：bone #ece4d0（骨干主体）/ boneLight #fafaf0（受光高光）/
                //   boneShade #b8a888（暗面 + 膨节阴影）/ marrow #6a5a40（骨端髓腔暗点，表「骨断面」）。
                const bone = "#ece4d0", boneLight = "#fafaf0", boneShade = "#b8a888", marrow = "#6a5a40"
                // 中央骨干（纵向竖条，rows 8..15；左右各 1px 受光 / 阴影表圆柱明暗）
                R(11, 8, 3, 8, bone)
                R(11, 8, 1, 8, boneLight)   // 左受光
                R(13, 8, 1, 8, boneShade)   // 右阴影
                // 上端膨节（骨骺，横向膨出 + 两侧圆球）
                R(8, 6, 9, 2, bone)
                R(8, 6, 9, 1, boneLight)
                R(8, 7, 9, 1, boneShade)
                R(7, 6, 2, 3, bone)         // 左圆球
                R(16, 6, 2, 3, bone)        // 右圆球
                R(7, 8, 2, 1, boneShade)
                R(16, 8, 2, 1, boneShade)
                // 下端膨节（同上端，镜像）
                R(8, 16, 9, 2, bone)
                R(8, 16, 9, 1, boneShade)
                R(8, 17, 9, 1, boneLight)
                R(7, 15, 2, 3, bone)
                R(16, 15, 2, 3, bone)
                R(7, 15, 2, 1, boneShade)
                R(16, 15, 2, 1, boneShade)
                // 髓腔暗点（上下膨节中心各一，表「骨断面腔」）
                R(11, 6, 2, 1, marrow)
                R(11, 17, 2, 1, marrow)
            }

            // 腐肉（0x218，t299）：杀蹒跚者（僵尸）掉落。MC 风格腐肉 = 暗红褐腐块 + 绿斑霉点 + 腐败裂痕。
            //   纯原创自绘（§9a）。区别于生牛肉：主色更暗（褐红 vs 鲜红）+ 绿霉斑（变质的身份特征）。
            //   配色：flesh #6a3028（暗红褐主体）/ fleshDark #3e1818（腐败裂痕 + 底阴影）/ fleshLight #8a4838
            //   （受光高光）/ mold #5a7038（绿霉斑，表「长霉」）/ moldDark #3a4820（霉斑暗芯）。
            const drawRottenFlesh = () => {
                const flesh = "#6a3028", fleshDark = "#3e1818", fleshLight = "#8a4838"
                const mold = "#5a7038", moldDark = "#3a4820"
                // 腐块主体（不规则圆角块，rows 7..17；比生牛肉更暗褐）
                R(7, 7, 11, 1, flesh)
                R(6, 8, 13, 9, flesh)        // 主体 rows 8..16
                R(7, 17, 11, 1, flesh)
                // 顶受光（前两行略亮，表「湿润但暗淡」）
                R(7, 7, 11, 1, fleshLight)
                R(6, 8, 9, 1, fleshLight)
                // 底阴影 + 圆角暗
                R(7, 17, 11, 1, fleshDark)
                R(6, 16, 1, 1, fleshDark); R(18, 16, 1, 1, fleshDark)
                // 腐败裂痕（几道不规则深色裂痕，表「组织崩解」）
                R(8, 10, 3, 1, fleshDark)
                R(13, 11, 2, 1, fleshDark)
                R(10, 13, 4, 1, fleshDark)
                R(7, 14, 2, 1, fleshDark)
                // 绿霉斑（散布的绿点 + 暗芯，表「长霉」——腐肉的身份特征，与生肉区分）
                R(9, 9, 2, 2, mold);  R(10, 9, 1, 1, moldDark)
                R(14, 13, 2, 2, mold);R(15, 14, 1, 1, moldDark)
                R(8, 14, 1, 1, mold)
                R(12, 9, 1, 1, moldDark)
            }

            // 线（0x219，t299）：杀蜘蛛掉落。MC 风格线 = 浅色缠绕线团（多圈同心椭圆 + 几道交叉亮丝）。
            //   纯原创自绘（§9a）。配色：thread #e8e0c8（浅米黄线体）/ threadLight #faf8e8（高光丝）/ threadShade
            //   #b8a888（阴影圈，表「缠绕层次」）。浅色 + 多圈缠绕一眼辨「一缕线」（非长直丝，而是缠起的线团）。
            const drawString = () => {
                const thread = "#e8e0c8", threadLight = "#faf8e8", threadShade = "#b8a888"
                // 线团主体（中央椭圆块，rows 8..16）
                R(8, 8, 9, 1, thread)
                R(6, 9, 13, 7, thread)       // 主体 rows 9..15
                R(8, 16, 9, 1, thread)
                // 外圈缠绕阴影（左右两侧暗边，表「线团圆柱明暗」）
                R(6, 9, 1, 7, threadShade)
                R(18, 9, 1, 7, threadShade)
                R(8, 16, 9, 1, threadShade)
                // 同心缠绕圈（几道横向 + 斜向暗线，表「一圈圈缠起的线」）
                R(8, 11, 11, 1, threadShade)
                R(8, 14, 11, 1, threadShade)
                R(9, 9, 1, 7, threadShade)
                R(15, 9, 1, 7, threadShade)
                // 高光亮丝（几道交叉亮线，表「丝线反光」）
                R(9, 10, 6, 1, threadLight)
                R(11, 12, 4, 1, threadLight)
                R(10, 13, 1, 2, threadLight)
                R(14, 10, 1, 3, threadLight)
            }

            // t304 箭（0x21A）：弓弹药。MC 风格箭 = 斜置杆 + 箭头 + 箭羽（fletching）。机制等价 MC 箭图标；
            //   纯原创自绘（§9a）。配色：shaft #9c7340（木杆，同木棒亮面）/ shaftDark #6b4f24（杆暗面）/
            //   tip #8a8a8a（灰色金属箭头）/ tipDark #585866（箭头暗边）/ fletch #e8e0c8（浅色箭羽，同线色族）。
            const drawArrow = () => {
                const shaft = "#9c7340", shaftDark = "#6b4f24"
                const tip = "#8a8a8a", tipDark = "#585866"
                const fletch = "#e8e0c8", fletchDark = "#b8a888"
                // 箭杆（从左下 (4,20) 到右上 (18,6) 的对角木杆，每步右上移，2px 宽）
                const segs = [
                    [4, 20], [5, 19], [6, 18], [7, 17], [8, 16], [9, 15],
                    [10, 14], [11, 13], [12, 12], [13, 11], [14, 10], [15, 9],
                    [16, 8], [17, 7]
                ]
                for (const [c, r] of segs) {
                    R(c, r, 2, 2, shaftDark)    // 杆暗面底
                    R(c, r, 1, 2, shaft)        // 杆亮面（左上）
                }
                // 箭头（右上端，三角尖朝右上）：菱形 + 暗边
                R(17, 5, 4, 2, tip)             // 箭头主体
                R(18, 4, 3, 1, tip)             // 箭尖上收
                R(19, 3, 2, 1, tipDark)         // 最尖
                R(17, 7, 4, 1, tipDark)         // 箭头底描边（接杆处）
                // 箭羽（左下端，三片尾羽）：浅色羽 + 暗边
                R(2, 18, 3, 2, fletch)          // 上尾羽
                R(2, 17, 1, 1, fletchDark)
                R(3, 20, 3, 2, fletch)          // 下尾羽
                R(3, 21, 1, 1, fletchDark)
                R(5, 19, 2, 1, fletchDark)      // 羽根暗边（接杆处）
            }

            // t398 鸡相关材料（机制等价 MC 1.0 鸡掉羽毛 + 生鸡肉 + 周期下蛋；纯原创自绘 §9a）。
            // 羽毛（0x228）：杀鸡掉落。MC 风格羽毛 = 斜置羽杆 + 两侧羽片 + 底部蓬松羽根。机制等价 MC 羽毛图标。
            //   配色：quill #f0ebd8（羽杆米白）/ barb #ffffff（羽片白）/ barbDark #c8c0b0（羽片暗边）/ root #8a7a5a（羽根灰褐）。
            const drawFeather = () => {
                const quill = "#f0ebd8", barb = "#ffffff", barbDark = "#c8c0b0", root = "#8a7a5a"
                // 羽杆（从右下 (16,18) 到左上 (7,4) 的对角主杆，2px 宽）
                const segs = [
                    [7, 4], [8, 5], [9, 6], [10, 7], [11, 8], [12, 9],
                    [13, 10], [14, 11], [15, 12], [16, 13]
                ]
                for (const [c, r] of segs) R(c, r, 1, 2, quill)
                // 羽片（杆两侧的白色羽支，斜向伸出 —— 杆左上侧 + 右下侧）
                const barbs = [
                    [5, 4], [6, 6], [7, 8], [8, 10], [9, 12], [10, 14],     // 左上侧羽片
                    [10, 5], [12, 6], [14, 7], [13, 9], [15, 10], [13, 12]  // 右下侧羽片
                ]
                for (const [c, r] of barbs) R(c, r, 2, 1, barb)
                // 羽片暗边（部分，提层次）
                R(5, 5, 1, 1, barbDark); R(9, 11, 1, 1, barbDark); R(13, 11, 1, 1, barbDark)
                // 羽根（右下端，蓬松灰褐点）
                R(16, 14, 2, 3, root)
                R(15, 15, 1, 2, root)
            }
            // 生鸡肉（0x229）：杀鸡掉落。MC 风格生肉 = 粉红肉块（带骨断口）。机制等价 MC 生鸡肉图标。
            //   配色：meat #e89090（生肉粉红）/ meatLight #f8b8b8（受光高光）/ meatDark #b85858（暗边 + 骨断口）。
            const drawRawChicken = () => {
                const meat = "#e89090", meatLight = "#f8b8b8", meatDark = "#b85858"
                R(6, 8, 12, 1, meat)            // 顶行
                R(5, 9, 14, 6, meat)            // 主体 rows 9..14
                R(6, 15, 12, 1, meatDark)       // 底阴影
                R(5, 9, 14, 1, meatLight)       // 顶受光
                R(4, 10, 1, 4, meatDark)        // 左暗边（圆收）
                R(18, 10, 1, 4, meatDark)       // 右暗边
                // 骨断口（左端白色小段，表「带骨」）
                R(5, 11, 1, 3, meatLight)
                R(4, 12, 1, 1, "#f0e8d8")
            }
            // 熟鸡肉（0x22A）：鸡燃烧致死掉落。MC 风格熟肉 = 金棕烤肉块（带骨断口 + 烤痕）。机制等价 MC 熟鸡肉图标。
            //   配色：meat #c87848（熟肉金棕）/ meatLight #e09868（受光高光）/ meatDark #8a4828（暗边 + 烤痕）。
            const drawCookedChicken = () => {
                const meat = "#c87848", meatLight = "#e09868", meatDark = "#8a4828"
                R(6, 8, 12, 1, meat)
                R(5, 9, 14, 6, meat)
                R(6, 15, 12, 1, meatDark)
                R(5, 9, 14, 1, meatLight)
                R(4, 10, 1, 4, meatDark)
                R(18, 10, 1, 4, meatDark)
                // 烤痕（两道斜向暗纹，表「烤过」）
                R(9, 10, 1, 3, meatDark)
                R(13, 11, 1, 3, meatDark)
                // 骨断口
                R(5, 11, 1, 3, meatLight)
                R(4, 12, 1, 1, "#f0e8d8")
            }
            // 蛋（0x22B）：鸡周期性下蛋掉落。MC 风格蛋 = 白色椭圆（顶高光 + 底阴影）。机制等价 MC 蛋图标。
            //   配色：shell #f5f0e4（蛋壳白）/ lite #ffffff（顶高光）/ dark #c8c0b0（底阴影）。
            const drawEgg = () => {
                const shell = "#f5f0e4", lite = "#ffffff", dark = "#c8c0b0"
                R(9, 6, 6, 1, shell)        // 顶行（窄）
                R(7, 7, 10, 1, shell)
                R(6, 8, 12, 9, shell)       // 主体 rows 8..16
                R(7, 17, 10, 1, shell)
                R(9, 18, 6, 1, shell)       // 底行（圆收）
                R(9, 6, 6, 1, lite); R(7, 7, 8, 1, lite)   // 顶高光
                R(7, 17, 10, 1, dark); R(9, 18, 6, 1, dark) // 底阴影
                R(15, 9, 1, 7, dark)        // 右暗边
            }
            // 墨囊（0x22D）：杀鱿鱼掉落。MC 风格墨囊 = 黑色水滴形囊袋（顶收窄 + 底圆鼓 + 顶部小颈）。
            //   机制等价 MC 1.0 ink sac 图标（§9 区隔：纯原创抽象水滴形，非照搬）。配色：ink #1a1a22（近黑墨囊）/
            //   inkLite #3a3a48（受光高光）/ inkDark #0a0a12（暗边 + 颈口）/ sheen #5a5a6a（亮面反光）。
            const drawInkSac = () => {
                const ink = "#1a1a22", inkLite = "#3a3a48", inkDark = "#0a0a12", sheen = "#5a5a6a"
                R(10, 6, 4, 1, inkDark)     // 顶颈口（窄收口）
                R(9, 7, 6, 1, ink)          // 颈下
                R(7, 8, 10, 9, ink)         // 囊袋主体 rows 8..16（中部最宽）
                R(8, 17, 8, 2, ink)         // 底部圆收
                R(9, 19, 6, 1, inkDark)     // 底暗
                R(7, 8, 10, 1, inkLite)     // 顶受光（囊袋上沿亮）
                R(7, 9, 1, 6, inkDark)      // 左暗边（圆收体积感）
                R(16, 9, 1, 6, inkDark)     // 右暗边
                R(9, 10, 2, 4, sheen)       // 左侧亮面反光（拟囊袋鼓面高光）
            }

            // t305 树苗物品（0x21B）：破叶概率掉落；右键草地/泥土种植 → Sapling 方块（WorldClock tick 推进成长）。
            //   机制等价 MC 1.0 橡树树苗（sapling）图标；纯原创自绘（§9a）。MC 风格树苗 = 一根棕色短树干 +
            //   顶部绿色嫩叶小球冠（树冠雏形）。配色：trunk #9c7340（树干亮面，同原木）/ trunkDark #6b4f24
            //   （树干暗面）/ leaf #6fae3a（嫩叶亮面，鲜绿）/ leafDark #3a6a1a（叶暗面）。
            const drawSapling = () => {
                const trunk = "#9c7340", trunkDark = "#6b4f24"
                const leaf = "#6fae3a", leafDark = "#3a6a1a"
                // 树干（画布底中下部，两列圆柱明暗：左暗 / 右亮）。占下半部分表「短树苗主干」。
                R(11, 12, 1, 8, trunkDark)   // 左暗面（cols 11, rows 12..19）
                R(12, 12, 1, 8, trunk)       // 右亮面（col 12）
                R(11, 19, 2, 1, trunkDark)   // 干底收口暗
                // 树冠（树干顶部小球状叶簇，rows 5..11；以 (11.5, 8) 为中心散布叶像素，亮/暗交错立体）
                R(9, 7, 5, 1, leaf)          // 顶横叶（受光亮）
                R(8, 8, 7, 3, leafDark)      // 中部主体叶（暗底，rows 8..10）
                R(8, 8, 7, 1, leaf)          // 中部上沿亮
                R(9, 11, 5, 1, leafDark)     // 底横叶（暗，收口）
                R(8, 9, 1, 2, leaf)          // 左亮边
                R(14, 9, 1, 2, leafDark)     // 右暗边
                R(10, 6, 3, 1, leaf)         // 顶尖叶（亮，表树冠顶）
                R(11, 5, 1, 1, leafDark)     // 最顶尖暗点
                // 几片散布亮叶（立体感）
                R(9, 10, 1, 1, leaf)
                R(13, 8, 1, 1, leaf)
                R(11, 10, 1, 1, leaf)
            }

            // t393 战利品表专用材料（地牢箱 / 渔获；机制等价 MC 1.0 dungeon chest loot 的稀有件 / 红石粉，
            //   纯原创自绘 §9a）。配色一眼可辨：红石=红粉堆 / 马鞍=棕鞍座 / 命名牌=纸签 + 细绳 / 附魔书=书 + 紫光晕。
            // 红石粉（0x224）：红色粉末小堆（不规则小红块 + 几粒亮红反光，表「一堆红石粉」）。
            //   配色：dust #c8302a（红石主体）/ dustLight #f0604a（受光高光）/ dustDark #8a1810（暗阴影）/ speck #ffa070（亮粉反光点）。
            const drawRedstone = () => {
                const dust = "#c8302a", dustLight = "#f0604a", dustDark = "#8a1810", speck = "#ffa070"
                // 不规则粉堆（中部最宽、上下收窄；表「一堆散粉」非整齐方块）
                R(9, 9, 6, 1, dust)
                R(7, 10, 10, 5, dust)        // 主体 rows 10..14
                R(9, 15, 6, 1, dust)
                // 顶受光（前两行亮，表「粉堆反光」）
                R(9, 9, 6, 1, dustLight)
                R(7, 10, 10, 1, dustLight)
                R(7, 10, 4, 1, dustLight)
                // 底阴影
                R(9, 15, 6, 1, dustDark)
                R(14, 11, 1, 4, dustDark)
                // 几粒亮粉反光点（散布，表「红石晶体反光」）
                R(9, 11, 1, 1, speck); R(12, 12, 1, 1, speck); R(10, 13, 1, 1, speck); R(13, 10, 1, 1, speck)
            }
            // 马鞍（0x225）：棕色皮革鞍座（座椅 + 前后翘起的鞍桥 + 两侧鞍裙）。机制等价 MC 马鞍图标。
            //   配色：leather #8a5a2b（鞍体棕）/ leatherLight #b07840（受光高光）/ leatherDark #5e3d1c（暗阴影 + 鞍桥）。
            const drawSaddle = () => {
                const leather = "#8a5a2b", leatherLight = "#b07840", leatherDark = "#5e3d1c"
                // 鞍座中央（座椅面，rows 10..13，最宽）
                R(6, 10, 12, 1, leather)     // 座椅顶（受光）
                R(5, 11, 14, 3, leather)     // 主体 rows 11..13
                R(6, 14, 12, 1, leatherDark) // 座椅底阴影
                // 前后鞍桥（翘起的鞍头 / 鞍尾，左右各一柱）
                R(4, 8, 3, 3, leather)       // 左鞍桥（前翘）
                R(17, 8, 3, 3, leather)      // 右鞍桥（后翘）
                R(4, 8, 3, 1, leatherDark)   // 鞍桥顶暗边
                R(17, 8, 3, 1, leatherDark)
                // 鞍裙（座椅下方两侧下垂的皮翼）
                R(5, 14, 2, 3, leatherDark)
                R(17, 14, 2, 3, leatherDark)
                // 受光高光（座椅顶面亮带，表「皮革反光」）
                R(7, 10, 10, 1, leatherLight)
                R(6, 11, 3, 1, leatherLight)
            }
            // 命名牌（0x226）：浅色纸签 + 顶部细绳挂耳 + 几行字线。机制等价 MC 命名牌图标。
            //   配色：paper #e8e0c8（纸签主体）/ paperLight #f8f4e4（受光高光）/ paperDark #b8a888（暗边 + 字线）/ cord #8a6c38（挂绳棕）。
            const drawNameTag = () => {
                const paper = "#e8e0c8", paperLight = "#f8f4e4", paperDark = "#b8a888", cord = "#8a6c38"
                // 纸签主体（圆角矩形，rows 9..17）
                R(8, 9, 8, 1, paper)
                R(6, 10, 12, 7, paper)       // 主体 rows 10..16
                R(8, 17, 8, 1, paper)
                // 顶受光（前两行亮，表「纸面反光」）
                R(8, 9, 8, 1, paperLight)
                R(6, 10, 12, 1, paperLight)
                R(6, 10, 3, 1, paperLight)
                // 底 / 右暗边（纸签厚度）
                R(8, 17, 8, 1, paperDark)
                R(17, 10, 1, 7, paperDark)
                // 顶部挂绳孔 + 挂绳（纸签顶上一小圆孔 + 向上的细绳挂耳）
                R(11, 8, 2, 1, paperDark)    // 圆孔（暗）
                R(11, 6, 2, 2, cord)         // 挂绳短杆
                R(10, 5, 4, 1, cord)         // 挂绳顶环
                // 字线（纸签上 3 行横线，表「写的字」）
                R(8, 12, 8, 1, paperDark)
                R(8, 14, 6, 1, paperDark)
                R(8, 16, 7, 1, paperDark)
            }
            // 附魔书占位（0x227）：一本合上的书 + 紫色附魔光晕（封面 + 书页 + 几道紫色光纹 / 闪点）。
            //   占位无真附魔（spec「enchanted-book-placeholder」）—— 仅图标表「带附魔光泽的书」。
            //   配色：cover #6a3a8a（封面紫）/ coverLight #9a5ab8（受光）/ page #e8e0c8（书页米黄）/ glow #c060e0（附魔紫光）/ spark #ffa0ff（光闪点）。
            const drawEnchantedBook = () => {
                const cover = "#6a3a8a", coverLight = "#9a5ab8", page = "#e8e0c8", glow = "#c060e0", spark = "#ffa0ff"
                // 书本主体（合上的书：封面矩形 + 右侧书页厚度条）
                R(5, 7, 12, 1, cover)        // 封面顶
                R(4, 8, 14, 8, cover)        // 封面主体 rows 8..15
                R(5, 16, 12, 1, cover)       // 封面底
                // 右侧书页厚度条（书的切口，米黄页边 + 暗阴影）
                R(16, 8, 2, 8, page)
                R(16, 8, 1, 8, "#b8a888")
                // 封面受光（左上亮，表「皮质封面反光」）
                R(5, 7, 12, 1, coverLight)
                R(4, 8, 4, 1, coverLight)
                // 封面装订线（左侧一道暗紫竖线，表「书脊」）
                R(4, 8, 1, 8, "#3a1a4a")
                // 附魔紫光晕（封面上的紫色光纹 + 闪点，表「附魔光泽」—— 占位的身份特征，区别于普通书）
                R(8, 10, 6, 1, glow)         // 上光纹
                R(8, 13, 6, 1, glow)         // 下光纹
                R(7, 11, 1, 2, glow)         // 左光带
                R(14, 11, 1, 2, glow)        // 右光带
                R(10, 11, 1, 1, spark)       // 中心光闪点
                R(12, 12, 1, 1, spark)
            }

            // 按 materialId 分流（default / 未知 → 兜底木棒，与旧行为一致）。
            // t345 护甲段（0x300..0x313，5 套材质 × 4 部位 = 20 件）。按 tier（配色）+ piece（形状）派生绘制，
            //   不为每件写独立 case（20 件 × 像素图过冗；tier/piece 两维即足够区分：皮革棕 / 铁银 / 铜橙 /
            //   金黄 / 钻石青 × 头盔圆穹 / 胸甲肩躯 / 护腿双腿 / 靴子双靴）。机制等价 MC 1.0 护甲图标族
            //   （§9 override (a) 纯原创自绘，非 MC GUI PNG）。
            const drawArmor = () => {
                const aid   = root.materialId - 0x300        // 0..19（护甲段内偏移）
                const piece = aid % 4                          // 0=头盔 1=胸甲 2=护腿 3=靴子
                const tier  = Math.floor(aid / 4)              // 0=皮革 1=铁 2=铜 3=金 4=钻石
                // 配色（按 tier 选 base / 亮 / 暗 三色）。皮革暖棕、金属银、铜橙、金黄、钻石青。
                const palettes = [
                    ["#8a5a2b", "#a87340", "#5e3d1c"], // 皮革 leather
                    ["#d8d8d8", "#f0f0f0", "#9a9a9a"], // 铁 iron
                    ["#c87850", "#e0966a", "#8a4f30"], // 铜 copper
                    ["#fad840", "#fff080", "#b89820"], // 金 gold
                    ["#4ee0c8", "#8af0e0", "#2a9886"], // 钻石 diamond
                ]
                const [base, light, dark] = palettes[tier] || palettes[0]
                if (piece === 0) {            // 头盔：帽檐 + 圆穹头罩 + 面罩缝
                    R(5, 6, 14, 2, base)      // 帽檐
                    R(7, 8, 10, 7, base)      // 头罩主体
                    R(7, 8, 10, 1, light)     // 顶受光
                    R(5, 6, 14, 1, light)     // 帽檐受光
                    R(7, 14, 10, 1, dark)     // 底阴影
                    R(9, 10, 6, 3, dark)      // 面罩缝（镂空感）
                } else if (piece === 1) {     // 胸甲：肩 + 躯干 + 中线
                    R(5, 6, 14, 3, base)      // 肩
                    R(6, 9, 12, 9, base)      // 躯干
                    R(5, 6, 14, 1, light)     // 肩受光
                    R(6, 9, 12, 1, light)     // 躯干顶受光
                    R(6, 17, 12, 1, dark)     // 底阴影
                    R(11, 9, 2, 9, dark)      // 中线（左右甲片分缝）
                } else if (piece === 2) {     // 护腿：腰 + 左右两腿
                    R(6, 6, 12, 3, base)      // 腰带
                    R(6, 6, 12, 1, light)     // 腰受光
                    R(6, 9, 5, 9, base)       // 左腿
                    R(13, 9, 5, 9, base)      // 右腿
                    R(6, 17, 5, 1, dark)      // 左腿底阴影
                    R(13, 17, 5, 1, dark)     // 右腿底阴影
                } else {                      // 靴子：左右两靴筒 + 鞋底
                    R(5, 9, 6, 8, base)       // 左靴筒
                    R(13, 9, 6, 8, base)      // 右靴筒
                    R(5, 9, 6, 1, light)      // 左靴顶受光
                    R(13, 9, 6, 1, light)     // 右靴顶受光
                    R(3, 16, 10, 2, dark)     // 鞋底（横向加深，连两靴）
                }
            }
            // 护甲段范围前置分支（避免为 20 个 id 各写 case；范围 [0x300, 0x314)）。
            if (root.materialId >= 0x300 && root.materialId < 0x314) { drawArmor(); return }

            // t401 生鱼（0x231）：钓竿拉起获物。MC 风格生鱼 = 银蓝色侧视鱼形（椭圆鱼身 + 尾鳍 + 背鳍 + 眼）。
            //   机制等价 MC 1.0 raw fish 图标（§9 区隔：纯原创抽象鱼形，非照搬）。配色：body #9fb8c8（银蓝鱼身）/
            //   bodyLite #c8d8e4（腹部受光）/ bodyDark #5a7080（背脊暗 + 描边）/ fin #4a6070（鳍，深蓝灰）/ eye #1a1a22（眼）。
            const drawRawFish = () => {
                const body = "#9fb8c8", bodyLite = "#c8d8e4", bodyDark = "#5a7080", fin = "#4a6070", eye = "#1a1a22"
                // 鱼身（左头右尾水平椭圆；头部略高、尾部收窄，rows 8..15 中部最宽）
                R(6, 9, 11, 6, body)           // 主体 rows 9..14
                R(7, 8, 9, 1, body)            // 顶沿（背部）
                R(7, 15, 9, 1, body)           // 底沿（腹部）
                R(5, 10, 2, 4, body)           // 头部前突（嘴部）
                R(5, 11, 1, 2, bodyDark)       // 嘴尖暗
                R(6, 9, 11, 1, bodyDark)       // 背脊暗线
                R(7, 15, 9, 1, bodyLite)       // 腹部受光亮
                R(8, 13, 8, 1, bodyLite)       // 腹侧亮带
                // 尾鳍（右侧分叉，上 / 下两片三角）
                R(17, 7, 4, 3, fin)            // 上尾鳍
                R(17, 14, 4, 3, fin)           // 下尾鳍
                R(17, 10, 2, 4, bodyDark)      // 尾柄（连鱼身的窄收口）
                // 背鳍（顶部三角，表鱼背鳍）
                R(9, 6, 5, 2, fin)
                R(10, 5, 3, 1, fin)
                // 眼（头部圆点）
                R(6, 10, 2, 2, eye)
                R(7, 10, 1, 1, bodyLite)       // 眼上反光
            }

            switch (root.materialId) {
            case 0x200: drawStick();        break
            case 0x201: drawCoal();         break
            case 0x202: drawIronOre();      break
            case 0x203: drawIronIngot();    break
            case 0x204: drawGlass();        break
            case 0x205: drawCharcoal();     break
            case 0x206: drawBucketEmpty();  break // t174 铁桶（空）
            case 0x207: drawWaterBucket();  break // t174 装水铁桶
            case 0x220: drawLavaBucket();   break // t343 装岩浆铁桶
            case 0x208: drawSeed();         break // t235 小麦种子
            case 0x209: drawWheat();        break // t237 小麦物品（收割成熟小麦作物）
            case 0x20A: drawBread();        break // t238 面包（3 小麦合成；右键食 +5 饥饿）
            case 0x20B: drawRawPorkchop();  break // t242 杀猪掉落
            case 0x20C: drawRawBeef();      break // t242 杀牛掉落
            case 0x20D: drawLeather();      break // t242 杀牛掉落
            case 0x20E: drawWool();         break // t242 杀羊掉落
            case 0x20F: drawSpawnEgg("pig");   break // t243 生物蛋（猪）
            case 0x210: drawSpawnEgg("cow");   break // t243 生物蛋（牛）
            case 0x211: drawSpawnEgg("sheep"); break // t243 生物蛋（羊）
            case 0x213: drawSpawnEgg("shambler"); break // t287/t303 生物蛋（蹒跚者；机制等价僵尸）
            case 0x214: drawSpawnEgg("bones");    break // t287/t303 生物蛋（骸骨；机制等价骷髅）
            case 0x215: drawSpawnEgg("stalker");  break // t287/t303 生物蛋（潜行者；机制等价苦力怕）
            case 0x216: drawSpawnEgg("spider");   break // t285/t303 生物蛋（蜘蛛）
            case 0x212: drawDiamond();        break // t279 钻石（钻石矿挖掘掉落）
            case 0x21C: drawCopperOre();      break // t308 铜原矿（铜矿石挖掘掉落；熔炉烧铜锭）
            case 0x21D: drawCopperIngot();    break // t308 铜锭（铜原矿冶炼产物）
            case 0x21E: drawGoldOre();        break // t308 金原矿（金矿石挖掘掉落；熔炉烧金锭）
            case 0x21F: drawGoldIngot();      break // t308 金锭（金原矿冶炼产物）
            case 0x217: drawBone();           break // t299 骨头（杀骸骨掉落）
            case 0x218: drawRottenFlesh();    break // t299 腐肉（杀蹒跚者掉落）
            case 0x219: drawString();         break // t299 线（杀蜘蛛掉落）
            case 0x21A: drawArrow();          break // t304 箭（弓弹药；铁锭+木棒+线合成 4 件）
            case 0x21B: drawSapling();        break // t305 树苗物品（破叶掉落；种植 → 树）
            case 0x221: drawCookedPorkchop(); break // t344 熟猪排（猪燃烧致死掉落）
            case 0x222: drawCookedBeef();     break // t344 熟牛肉（牛燃烧致死掉落）
            case 0x223: drawCookedMutton();   break // t344 熟羊肉（羊燃烧致死掉落）
            case 0x224: drawRedstone();       break // t393 红石粉（地牢战利品）
            case 0x225: drawSaddle();         break // t393 马鞍（地牢稀有战利品）
            case 0x226: drawNameTag();        break // t393 命名牌（地牢稀有战利品）
            case 0x227: drawEnchantedBook();  break // t393 附魔书占位（地牢极稀有战利品）
            case 0x228: drawFeather();        break // t398 羽毛（杀鸡掉落）
            case 0x229: drawRawChicken();     break // t398 生鸡肉（杀鸡掉落）
            case 0x22A: drawCookedChicken();  break // t398 熟鸡肉（鸡燃烧致死掉落）
            case 0x22B: drawEgg();            break // t398 蛋（鸡周期性下蛋掉落）
            case 0x22C: drawSpawnEgg("chicken"); break // t398 生物蛋（鸡）
            case 0x22D: drawInkSac();             break // t399 墨囊（杀鱿鱼掉落）
            case 0x22E: drawSpawnEgg("squid");    break // t399 生物蛋（鱿鱼）
            case 0x22F: drawCarrot();             break // t400 胡萝卜（猪繁殖食物；喂成体猪 → 求偶）
            case 0x230: drawPotato();             break // t400 马铃薯（猪繁殖食物；喂成体猪 → 求偶）
            case 0x231: drawRawFish();            break // t401 生鱼（钓竿拉起获物；机制等价 MC 1.0 raw fish）
            default:    drawStick();        break
            }
        }
    }
}
