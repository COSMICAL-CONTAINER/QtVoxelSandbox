import QtQuick

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
// 木棒既非方块（无等距立方体 PNG）也非工具（非 ToolIcon 镐形），煤/铁原矿/铁锭/玻璃/木炭/桶同理 → 均独立自绘。
//
// 消费点：Main.qml 的游戏内 hotbar delegate / 光标手持浮动图标 / 掉落实体 Repeater（sourceItem），
// Inventory.qml / SurvivalInventory.qml / CraftingTableUI.qml 各槽 —— 凡 hotbarVM.isMaterial(id) 为真
// 的槽位用本组件替代方块 Image / ToolIcon。新增材料在此 switch 加一分支即可全工程生效。
Item {
    id: root
    property int materialId: 0 // 材料段 id（0x200 木棒 / 0x201 煤 / 0x202 铁原矿 / 0x203 铁锭 / 0x204 玻璃 / 0x205 木炭 / 0x206 铁桶 / 0x207 装水铁桶 / 0x208 小麦种子 / 0x209 小麦物品 / 0x20A 面包 / 0x20B 生猪排 / 0x20C 生牛肉 / 0x20D 皮革 / 0x20E 羊毛；0/未知 → 兜底木棒）

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

            // 按 materialId 分流（default / 未知 → 兜底木棒，与旧行为一致）。
            switch (root.materialId) {
            case 0x200: drawStick();        break
            case 0x201: drawCoal();         break
            case 0x202: drawIronOre();      break
            case 0x203: drawIronIngot();    break
            case 0x204: drawGlass();        break
            case 0x205: drawCharcoal();     break
            case 0x206: drawBucketEmpty();  break // t174 铁桶（空）
            case 0x207: drawWaterBucket();  break // t174 装水铁桶
            case 0x208: drawSeed();         break // t235 小麦种子
            case 0x209: drawWheat();        break // t237 小麦物品（收割成熟小麦作物）
            case 0x20A: drawBread();        break // t238 面包（3 小麦合成；右键食 +5 饥饿）
            case 0x20B: drawRawPorkchop();  break // t242 杀猪掉落
            case 0x20C: drawRawBeef();      break // t242 杀牛掉落
            case 0x20D: drawLeather();      break // t242 杀牛掉落
            case 0x20E: drawWool();         break // t242 杀羊掉落
            default:    drawStick();        break
            }
        }
    }
}
