#!/usr/bin/env python3
"""把地形瓦片拼成水平条带纹理图集 atlas.png。

瓦片顺序必须与 chunkgeometry.cpp 的 N + BlockRegistry::tileIndex 注释一致
（一个偏差即渗色/错贴）：
  0 grass_top / 1 grass_side / 2 dirt / 3 stone / 4 sand
  5 cobble / 6 log_top / 7 log_side / 8 planks / 9 leaves
  10 crafting_table_top / 11 crafting_table_side
  12 furnace_top / 13 furnace_side / 14 furnace_front（t80，炉口朝 -Z）
  134 furnace_front_on（t494，熔炉点燃态前面：拱洞内亮黄橙火焰；mesher 据 Furnace state 点燃 bit 选 14/134）
  15 coal_ore / 16 iron_ore
  17 torch（t88，程序生成原创像素图）
  18 bedrock（t119，程序生成原创像素图）
  19 water（t148，程序生成原创像素图，纹理不透明；半透由 Main.qml 水材质 opacity=0.7 实现）
  20 chest_top / 21 chest_side / 22 chest_front（t173，程序生成原创像素图；箱子方块，机制等价 MC 1.0 箱子）
  23 water_flow（t197，程序生成原创像素图；**流水专用**贴图——水源 state=0 仍用 tile 19 静水（横向波纹），
     流水 state=1..7 用本 tile（左上→右下斜向条纹），与水面随 level 下降的阶梯感配合，传达「逐格衰减流动」。
     该 tile 不绑定任何方块 id（Water 方块 def 仍各面=19），mesher 在水段按 cell 的 state 选 19/23——属
     渲染层呈现选择，非方块属性。tools/build_water_flow.py 生成。）
  24 water_2（t223 水贴图动画第二帧：静水 flipbook frame 1——波纹位置相对 tile 19 略下移 1 像素 + 高光位移。
     **tXXX 静态水后 mesher 不再引用**（flipbook 换帧重建已消除，水段恒用 tile 19/23）；瓦片保留供将来
     material 级动画复用。不绑定方块 id；属渲染层呈现选择，非方块属性。tools/build_water.py 生成。）
  25 water_flow_2（t223 流水动画第二帧：流水 flipbook frame 1——斜纹沿流动方向 (+1,+1) 平移半周期。
     **tXXX 静态水后 mesher 不再引用**（同上）；瓦片保留。不绑定方块 id；属渲染层呈现选择，非方块属性。
     tools/build_water_flow.py 生成。）
  26 farmland_dry（t234 耕地顶面干态：浅棕色翻耕干土 + 纵向犁沟纹。Farmland 方块 topTile 字段 = 本 tile；
     mesher 据 Farmland state bit0=0（干）选本 tile 画 +Y 顶面。tools/build_farmland.py 生成。）
  27 farmland_wet（t234 耕地顶面湿态：深棕色湿润翻耕土 + 同犁沟纹（区别 dry 仅色更深）。Farmland 方块
     frontTile 字段 = 本 tile（字段复用：Farmland 无 -Z 前面语义，frontTile 唯一消费点是 tileFor 的湿态顶面）；
     mesher 据 Farmland state bit0=1（湿）选本 tile 画 +Y 顶面。tools/build_farmland.py 生成。）
  28 tall_grass（t235 草丛 cross 贴图：green 草叶 + alpha 透明底。TallGrass 方块各面 = 本 tile；mesher 走 cross
     几何段（PartialBlockGeometry pushCross 双面双对角 quad），chunk 地形材质 alphaCutoff:0.5 cutout 透明底。
     tools/build_tall_grass.py 程序生成原创像素图，§9 override (a)。）
  29..36 wheat_stage_0..7（t236 小麦作物 8 个生长阶段贴图：cross 几何段，alpha 透明底 cutout。WheatCrop 方块 def
     各面 = 29（基底阶段 0），mesher 在 PartialBlockGeometry::append 的 WheatCrop case 内据 chunk state 选
     tile = 29 + stage（0=嫩芽 → 5=满高绿叶 → 6/7=顶部抽金黄麦穗成熟）。阶段贴图选择是 mesher 呈现层据 state
     决定，非方块属性（同 Water 流水贴图模式）。tools/build_wheat.py 程序生成原创像素图，§9 override (a)。）
  37 diamond_ore（t279 钻矿石：散布于 stone 深层 y∈[5,16]，需铁镐采掘；机制等价 MC 1.0 钻石矿，名称/贴图
     原创自绘 §9a。各面同贴图=青白菱斑晶体嵌于石头底；tools/build_ore.py 程序生成原创像素图。）
  38 wool（t300 羊毛方块：剪羊毛 / 杀羊掉落；机制等价 MC 1.0 羊毛，名称/贴图原创自绘 §9a。
     各面同贴图=奶白羊毛底 + 浅灰卷曲绒毛纹；tools/build_wool.py 程序生成原创像素图。）
  39 sapling（t305 树苗 cross 贴图：棕色短树干 + 顶部绿色嫩叶小球冠，alpha 透明底 cutout。
     Sapling 方块各面 = 本 tile；mesher 走 cross 几何段（PartialBlockGeometry pushCross 双面双对角 quad），
     chunk 地形材质 alphaCutoff:0.5 cutout 透明底。机制等价 MC 1.0 橡树树苗（sapling）；名称/贴图原创自绘 §9a。
     tools/build_sapling.py 程序生成原创像素图。）
  40 copper_ore（t308 铜矿石：散布于 stone 浅中层 y∈[5,45]、需石镐采掘；机制等价 MC 1.0 铜矿，
     名称/贴图原创自绘 §9a。各面同贴图=石头底+橙铜斑+少量孔雀绿锈；tools/build_ore.py 程序生成原创像素图。）
  41 gold_ore（t308 金矿石：散布于 stone 深层 y∈[5,25]、需铁镐采掘；机制等价 MC 1.0 金矿，
     名称/贴图原创自绘 §9a。各面同贴图=石头底+金黄斑簇；tools/build_ore.py 程序生成原创像素图。）
（CC0 资产，来源见 docs/PLAN.md §L 资产管线；工作台贴图由 tools/build_crafting_table.py、
 熔炉贴图由 tools/build_furnace.py、矿石贴图由 tools/build_ore.py、火把贴图由 tools/build_torch.py、
 基岩贴图由 tools/build_bedrock.py、水贴图由 tools/build_water.py、箱子贴图由 tools/build_chest.py、
 流水贴图由 tools/build_water_flow.py、草丛贴图由 tools/build_tall_grass.py、小麦作物贴图由
 tools/build_wheat.py 程序生成原创像素图，§9 override (a)。）
"""
import os
from PIL import Image

TILE = 16
TILES = [
    "default_grass_top",            # 0 grass_top
    "default_grass_side",           # 1 grass_side
    "default_dirt",                 # 2 dirt
    "default_stone",                # 3 stone
    "default_sand",                 # 4 sand
    "default_cobble",               # 5 cobble
    "default_tree_top",             # 6 log_top
    "default_tree",                 # 7 log_side
    "default_wood",                 # 8 planks
    "default_leaves",               # 9 leaves
    "default_crafting_table_top",   # 10 crafting_table_top（t50）
    "default_crafting_table_side",  # 11 crafting_table_side（t50）
    "default_furnace_top",          # 12 furnace_top（t80）
    "default_furnace_side",         # 13 furnace_side（t80）
    "default_furnace_front",        # 14 furnace_front（t80，炉口朝 -Z）
    "default_coal_ore",             # 15 coal_ore（t84，程序生成原创像素图）
    "default_iron_ore",             # 16 iron_ore（t84，程序生成原创像素图）
    "default_torch",                # 17 torch（t88，程序生成原创像素图；6 面同贴图）
    "default_bedrock",              # 18 bedrock（t119，程序生成原创像素图；6 面同贴图，深灰斑驳）
    "default_water",                # 19 water（t148，程序生成原创像素图；6 面同贴图，纹理不透明，半透由材质 opacity 实现）
    "default_chest_top",            # 20 chest_top（t173，程序生成原创像素图；箱子顶面=盖缝+铰链+锁印）
    "default_chest_side",           # 21 chest_side（t173，程序生成原创像素图；箱子侧面=铁箍带+板缝）
    "default_chest_front",          # 22 chest_front（t173，程序生成原创像素图；箱子前面=盖缝+锁孔）
    "default_water_flow",           # 23 water_flow（t197，程序生成原创像素图；流水专用——斜向条纹区别于
                                    #    静水横向波纹；mesher 在水段按 cell state 选 19(水源)/23(流水)，非方块属性）
    "default_water_2",              # 24 water_2（t223 静水动画第二帧；mesher 据水段 waterAnimPhase 在 19/24 间切换）
    "default_water_flow_2",         # 25 water_flow_2（t223 流水动画第二帧；mesher 据水段 waterAnimPhase 在 23/25 间切换）
    "default_farmland_dry",         # 26 farmland_dry（t234 耕地顶面干态；浅色翻耕干土 + 纵向犁沟纹；tools/build_farmland.py 程序生成原创像素图）
    "default_farmland_wet",         # 27 farmland_wet（t234 耕地顶面湿态；深色湿润翻耕土 + 同犁沟纹；mesher 据 Farmland state bit0 选 26(干)/27(湿)）
    "default_tall_grass",           # 28 tall_grass（t235 草丛 cross 贴图；green 草叶 + alpha 透明底；mesher 走 cross 几何段 + alphaCutoff cutout；tools/build_tall_grass.py 程序生成原创像素图）
    "default_wheat_stage_0",        # 29 wheat_stage_0（t236 小麦作物阶段 0 = 刚种嫩芽；mesher 在 cross 几何段据 state 选 29..36）
    "default_wheat_stage_1",        # 30 wheat_stage_1
    "default_wheat_stage_2",        # 31 wheat_stage_2
    "default_wheat_stage_3",        # 32 wheat_stage_3
    "default_wheat_stage_4",        # 33 wheat_stage_4
    "default_wheat_stage_5",        # 34 wheat_stage_5（满高绿叶）
    "default_wheat_stage_6",        # 35 wheat_stage_6（顶部初抽金黄麦穗）
    "default_wheat_stage_7",        # 36 wheat_stage_7（成熟：穗更密、全金黄）
    "default_diamond_ore",          # 37 diamond_ore（t279 钻矿石；散布于 stone 深层 y∈[5,16]；需铁镐；
                                    #    机制等价 MC 1.0 钻石矿，名称/贴图原创自绘 §9a；各面同贴图=青白菱斑晶体）
    "default_wool",                 # 38 wool（t300 羊毛方块；剪羊毛 / 杀羊掉落；机制等价 MC 1.0 羊毛，
                                    #    名称/贴图原创自绘 §9a；各面同贴图=奶白羊毛底+浅灰卷曲绒毛纹）
    "default_sapling",              # 39 sapling（t305 树苗 cross 贴图；棕色短树干+绿色嫩叶小球冠，alpha 透明底 cutout；
                                    #    Sapling 方块各面=本 tile，mesher 走 cross 几何段；机制等价 MC 1.0 橡树树苗；tools/build_sapling.py 程序生成原创像素图）
    "default_copper_ore",           # 40 copper_ore（t308 铜矿石；散布于 stone 浅中层 y∈[5,45]、需石镐采掘；机制等价 MC 1.0 铜矿，
                                    #    名称/贴图原创自绘 §9a；各面同贴图=石头底+橙铜斑+孔雀绿锈；tools/build_ore.py 程序生成）
    "default_gold_ore",             # 41 gold_ore（t308 金矿石；散布于 stone 深层 y∈[5,25]、需铁镐采掘；机制等价 MC 1.0 金矿，
                                    #    名称/贴图原创自绘 §9a；各面同贴图=石头底+金黄斑簇；tools/build_ore.py 程序生成）
    "default_lava",                 # 42 lava（t343 岩浆；机制等价 MC 1.0 岩浆——慢流、不可破、玩家穿过；worldgen Y<30 封闭
                                    #    岩浆湖 + 玩家铁桶舀/放；木质方块邻岩浆概率着火焚毁；掉落物丢入被摧毁。名称/贴图原创自绘
                                    #    §9a；各面同贴图=深红橙底+亮黄橙鼓泡+白炽热点；tools/build_lava.py 程序生成原创像素图。
                                    #    纹理不透明，岩浆段材质 opacity≈0.95 近不透、配 NoLighting 暖色 baseColor 显自发光感）
    # t387 床方块（bed）8 色变体：简化单格整立方（spec「head+foot 双格，或简化单格」→ 取单格），机制等价 MC 1.0
    #   床。各色变体一张 16×16 实心贴图（彩色被面底 + 顶部枕垫亮带 + 绗缝针脚暗点 + 边缘暗化），六面铺同图（同
    #   wool / chest）。配方 planks+wool → 红床（默认色，最标志性）；其余色变体创造调色板直接取用（无染料系统）。
    #   名称 / 贴图全原创纯色，零 MC 资产（§9 区隔）；tools/build_bed.py 程序生成原创像素图。
    "default_bed_red",              # 43 bed_red（红床；配方产物默认色）
    "default_bed_orange",           # 44 bed_orange（橙床）
    "default_bed_yellow",           # 45 bed_yellow（黄床）
    "default_bed_green",            # 46 bed_green（绿床）
    "default_bed_cyan",             # 47 bed_cyan（青床）
    "default_bed_blue",             # 48 bed_blue（蓝床）
    "default_bed_magenta",          # 49 bed_magenta（品红床）
    "default_bed_black",            # 50 bed_black（黑床）
    "default_spawner",              # 51 spawner（t392 刷怪笼；地下地牢中央放置的整立方方块，
                                    #    玩家在范围内时周期性刷一只敌对 mob，破坏后停止；机制等价 MC 1.0 刷怪笼，
                                    #    名称/贴图原创自绘 §9a；各面同贴图=暗蓝灰底+铁灰栅栏+中心青绿光斑；
                                    #    tools/build_spawner.py 程序生成原创像素图）
    # t394 沙漠群系内容：砂岩（沙下成岩）/ 仙人掌（接触伤害）/ 枯死的灌木（cross 装饰）。机制等价 MC 1.0
    #   沙漠三件套（sandstone / cactus / dead bush），名称/贴图原创自绘 §9a；各由独立 build_*.py 程序生成。
    "default_sandstone_top",        # 52 sandstone_top（砂岩顶面；压实沙面 + 细密噪点 + 暗框）
    "default_sandstone_side",       # 53 sandstone_side（砂岩侧面/底面；暖沙底 + 横向层理带）
    "default_cactus_top",           # 54 cactus_top（仙人掌顶面/底面；绿截面 + 同心方框环纹 + 中央凹陷）
    "default_cactus_side",          # 55 cactus_side（仙人掌侧面；深绿底 + 4 垂直棱脊亮带 + 棱上刺点）
    "default_dead_bush",            # 56 dead_bush（枯死的灌木 cross 贴图；透明底 + 棕褐放射干枝；alphaCutoff cutout）
    # t395 雪原/针叶群系内容：积雪层（地表覆雪）/ 冰（水面冻结）/ 云杉原木（云杉树主干）。机制等价 MC 1.0
    #   寒冷群系三件套（snow / ice / spruce log），名称/贴图原创自绘 §9a；各由独立 build_*.py 程序生成。
    "default_snow",                 # 57 snow（积雪层各面贴图；冷白底+细密冰晶噪点；SnowLayer 各面=本 tile）
    "default_ice",                  # 58 ice（冰各面贴图；浅蓝底+反光裂纹；Ice 各面=本 tile）
    "default_spruce_log_top",       # 59 spruce_log_top（云杉原木顶/底面；深棕同心年轮截面）
    "default_spruce_log_side",      # 60 spruce_log_side（云杉原木侧面；深棕垂直树皮条带）
    "default_lily_pad",             # 61 lily_pad（t396 睡莲 cross 路由的横向浮叶贴图；透明底 + 绿色圆叶 + V 形缺口；
                                    #    alphaCutoff cutout；LilyPad 各面=本 tile，mesher 走 LilyPad 横向 quad case）
    "default_mushroom",             # 62 mushroom（t396 蘑菇 cross 贴图；透明底 + 米色菌柄 + 红底白斑菌盖；
                                    #    alphaCutoff cutout；Mushroom 各面=本 tile，mesher 走 cross 几何段）
    # t397 多群系装饰植物：花 4 色变体 + 甘蔗（机制等价 MC 1.0 花 / 甘蔗；名称 / 贴图原创自绘 §9a）。
    #   各色花 / 甘蔗 cross 贴图（透明底 + 茎 + 花头 / 细茎，alphaCutoff cutout）；各 Flower / Sugarcane 方块各面=本 tile，
    #   mesher 走 cross 几何段（PartialBlockGeometry pushCross 双面双对角 quad）；tools/build_flower.py +
    #   tools/build_sugarcane.py 程序生成原创像素图。
    "default_flower_red",           # 63 flower_red（红花 cross 贴图；机制等价 MC poppy 罂粟，名称/贴图原创自绘 §9a）
    "default_flower_yellow",        # 64 flower_yellow（黄花 cross 贴图；机制等价 MC dandelion 蒲公英）
    "default_flower_blue",          # 65 flower_blue（蓝花 cross 贴图；原创 4 色变体之一）
    "default_flower_white",         # 66 flower_white（白花 cross 贴图；机制等价 MC oxeye daisy 雏菊）
    "default_sugarcane",            # 67 sugarcane（甘蔗 cross 贴图；机制等价 MC sugar cane 芦苇；透明底 + 绿色节段细茎 + 顶部尖叶）
    "default_glass",                # 68 glass（t405 玻璃各面贴图；近白青底 + 暗边框 + 对角高光斜线；Glass 各面=本 tile，
                                    #    mesher 走 glassOnly 半透段；纹理不透明，半透由材质 opacity 实现，同 water 模式；
                                    #    tools/build_glass.py 程序生成原创像素图）
    # t407 胡萝卜/马铃薯作物 4 阶段贴图（机制等价 MC 1.0 carrot/potato crop 4 张阶段贴图；每张覆盖 2 个年龄：
    #   age 0-1→stage0、2-3→stage1、4-5→stage2、6-7→stage3）。cross 几何段（PartialBlockGeometry pushCross 双面双
    #   对角 quad），alpha 透明底 cutout。CarrotCrop def 各面=69 / PotatoCrop def 各面=73（基底阶段 0）；mesher 在
    #   PartialBlockGeometry::append 的 CarrotCrop/PotatoCrop case 内据 state 选 tile = 基底 + state/2。
    #   tools/build_carrot_potato.py 程序生成原创像素图（§9 override (a)；零 MC 资产）。
    "default_carrot_crop_0",        # 69 carrot_crop_0（age 0-1：嫩芽）
    "default_carrot_crop_1",        # 70 carrot_crop_1（age 2-3：拔高）
    "default_carrot_crop_2",        # 71 carrot_crop_2（age 4-5：叶丛）
    "default_carrot_crop_3",        # 72 carrot_crop_3（age 6-7：成熟，橙红胡萝卜根露出土）
    "default_potato_crop_0",        # 73 potato_crop_0（age 0-1：嫩芽）
    "default_potato_crop_1",        # 74 potato_crop_1（age 2-3：拔高）
    "default_potato_crop_2",        # 75 potato_crop_2（age 4-5：叶丛）
    "default_potato_crop_3",        # 76 potato_crop_3（age 6-7：成熟，棕黄马铃薯块茎露出土）
    "default_obsidian",             # 77 obsidian（t411 黑曜石；流水触静岩浆源凝固产物——机制等价 MC 1.0
                                    #    obsidian，名称/贴图原创自绘 §9a；各面同贴图=深紫黑火山玻璃底+紫红纹理嵌点+
                                    #    少量品紫玻璃微反光；tools/build_obsidian.py 程序生成原创像素图）
    "default_ladder",               # 78 ladder（t413/t501/t519 木梯贴墙贴图；透明底 + 棕色两根纵轨（贴瓦片两侧）+
                                    #    4 道横向梯级满铺轨间；alphaCutoff cutout；Ladder 各面=本 tile，mesher 走贴墙
                                    #    quad 几何段（t501 单片贴墙 quad，整张贴图铺满 face）；机制等价 MC 1.0 梯子，
                                    #    名称/贴图原创自绘 §9a；tools/build_ladder.py 程序生成原创像素图；t519 满格版修
                                    #    「放下形状上下宽粗糙」——纵轨贴瓦片两侧而非居中，整张无大块透明留白）
    # t455 16 色 wool 其余 15 色变体（white 复用 tile 38 default_wool；本段为 orange..black，tile 79..93）。
    #   机制等价 MC 1.0 羊毛 16 色变体；名称/贴图全原创自绘 §9a（build_wool.py 程序生成卷绒纹 + 标准 16 色着色）。
    #   各色羊毛方块各面=本 tile，走 culled 立方面路径（同 white Wool）。每色一个方块 id（WoolOrange=63..WoolBlack=77）。
    "default_wool_orange",          # 79 wool_orange
    "default_wool_magenta",         # 80 wool_magenta
    "default_wool_light_blue",      # 81 wool_light_blue
    "default_wool_yellow",          # 82 wool_yellow
    "default_wool_lime",            # 83 wool_lime
    "default_wool_pink",            # 84 wool_pink
    "default_wool_gray",            # 85 wool_gray
    "default_wool_light_gray",      # 86 wool_light_gray
    "default_wool_cyan",            # 87 wool_cyan
    "default_wool_purple",          # 88 wool_purple
    "default_wool_blue",            # 89 wool_blue
    "default_wool_brown",           # 90 wool_brown
    "default_wool_green",           # 91 wool_green
    "default_wool_red",             # 92 wool_red
    "default_wool_black",           # 93 wool_black
    # t455 16 色床补齐 8 色新变体（white/light_blue/lime/pink/gray/light_gray/purple/brown，tile 94..101）。
    #   既存 8 色床在 tile 43..50（不动）；本段为新色，与 build_wool.py 同色板（羊毛↔床同色一致）。
    #   机制等价 MC 1.0 床 16 色变体；名称/贴图全原创自绘 §9a（build_bed.py 程序生成被面+枕垫+绗缝针脚）。
    #   每色一个方块 id（BedWhite=78..BedBrown=85），配方 = 3 同色羊毛 + 3 木板 → 该色床。
    "default_bed_white",            # 94 bed_white
    "default_bed_light_blue",       # 95 bed_light_blue
    "default_bed_lime",             # 96 bed_lime
    "default_bed_pink",             # 97 bed_pink
    "default_bed_gray",             # 98 bed_gray
    "default_bed_light_gray",       # 99 bed_light_gray
    "default_bed_purple",           # 100 bed_purple
    "default_bed_brown",            # 101 bed_brown
    # t466 云杉木板（tile 102）：云杉木制品链共享贴图（SprucePlanks / SpruceSlab / SpruceFence / SpruceDoor
    #   一族木制品共享，同橡木 WoodSlab/Fence/Door 复用 planks(8) 模式）。深色木纹区别橡木木板；机制等价
    #   MC 1.0 spruce 木制品。tools/build_spruce.py 程序生成原创像素图（§9 override (a)）。
    "default_spruce_planks",        # 102 spruce_planks（云杉木板 / 台阶 / 栅栏 / 门 共享；深色木纹）
    # t467 雪原浆果灌木丛 3 阶段贴图（SweetBerryBush cross 几何段，alpha 透明底 cutout；机制等价 MC 1.0
    #   sweet berry bush）。mesher 在 PartialBlockGeometry::append 的 SweetBerryBush case 内据 state 选
    #   tile = 103 + stage（0 无果嫩丛 / 1 小果 / 2 成熟红浆果簇）。SweetBerryBush 方块 def 各面 = 基底 103。
    #   名称 / 贴图原创自绘 §9a；tools/build_sweet_berry.py 程序生成原创像素图。
    "default_sweet_berry_bush_0",   # 103 sweet_berry_bush_0（阶段 0 无果嫩丛；SweetBerryBush def 基底 tile）
    "default_sweet_berry_bush_1",   # 104 sweet_berry_bush_1（阶段 1 小果；mesher 据 state 选）
    "default_sweet_berry_bush_2",   # 105 sweet_berry_bush_2（阶段 2 成熟红浆果簇；右键采摘得 2-3 浆果）
    "default_pack_ice",             # 106 pack_ice（t468 浮冰各面贴图；实白底+细裂纹，比 Ice 更密实；PackIce 各面=本 tile）
    "default_blue_ice",             # 107 blue_ice（t468 蓝冰各面贴图；淡蓝底+纵向纹路，最滑；BlueIce 各面=本 tile）
    "default_lapis_ore",            # 108 lapis_ore（t471 青金矿石各面贴图；石头底+群青深蓝斑簇+黄铁矿金点；散布于 stone 深层 y∈[5,31]）
    "default_enchanting_table_top", # 109 enchanting_table_top（t474 附魔台顶面贴图；黑曜石深紫黑底+钻石青白四角嵌点+中央立书轮廓；EnchantingTable 顶面=本 tile；tools/build_enchanting_table.py 程序生成原创像素图）
    "default_enchanting_table_side", # 110 enchanting_table_side（t474 附魔台侧面/底面/前面贴图；黑曜石深紫黑底+钻石青白四角嵌点+顶/底边缘暗化带；EnchantingTable 底/侧/前=本 tile）
    "default_bookshelf",           # 111 bookshelf（t474 书架各面贴图；橡木木板边框+中央书脊彩色书列（红/蓝/绿/棕）；Bookshelf 各面=本 tile；tools/build_bookshelf.py 程序生成原创像素图）
    "default_iron_block",          # 112 iron_block（t477 铁块各面贴图；金属灰底+铆钉网格+高光；IronBlock 各面=本 tile；tools/build_anvil.py 程序生成原创像素图）
    "default_anvil_top",           # 113 anvil_top（t477 铁砧完好顶面贴图；深铁砧台+宽砧面+尖角+边缘暗化；Anvil 顶面=本 tile；tools/build_anvil.py 程序生成原创像素图）
    "default_anvil_base",          # 114 anvil_base（t477 铁砧侧/底/前面贴图；深铁砧身+横向分层暗带；Anvil/AnvilChipped/AnvilDamaged 底·侧·前共享本 tile；tools/build_anvil.py）
    "default_anvil_damaged_1_top", # 115 anvil_damaged_1_top（t477 微损铁砧顶面贴图；砧台+一条细裂纹；AnvilChipped 顶面=本 tile；tools/build_anvil.py）
    "default_anvil_damaged_2_top", # 116 anvil_damaged_2_top（t477 重损铁砧顶面贴图；砧台+粗裂纹网+尖角缺角；AnvilDamaged 顶面=本 tile；tools/build_anvil.py）
    # t482/t483 防御造物方块（机制等价 MC 1.0 雪傀儡 / 铁傀儡搭建材料；名称 / 贴图全原创自绘 §9a）。
    #   tools/build_pumpkin.py 程序生成原创像素图（橙色南瓜系：底 + 纵向瓜棱深纹 + 高光）。
    "default_pumpkin_side",       # 117 pumpkin_side（南瓜侧面贴图；深橙底 + 纵向瓜棱深纹 + 边缘暗化；Pumpkin sideTile=本 tile）
    "default_pumpkin_face",       # 118 pumpkin_face（南瓜刻面前面贴图；橙色 + 刻面双眼 + 锯齿嘴 + 顶部短茎；Pumpkin frontTile=本 tile，
                                  #    作造物头时面朝玩家侧，机制等价 MC 刻面南瓜 jack o'lantern）
    "default_pumpkin_top",        # 119 pumpkin_top（南瓜顶/底面贴图；橙色瓜顶 + 中央短茎 + 高光；Pumpkin top/bottomTile=本 tile）
    # t484 废弃矿井结构方块（机制等价 MC 1.0 废弃矿井 mineshaft 的蛛网 / 铁轨；名称 / 贴图全原创自绘 §9a）。
    #   各由独立 build_*.py 程序生成原创像素图（透明底 + alphaCutoff cutout；mesher 走 cross / 水平 quad case）。
    "default_cobweb",             # 120 cobweb（蜘蛛网 cross 贴图；透明底 + 灰白蛛丝放射网纹；Cobweb 各面=本 tile，
                                  #    mesher 走 cross 几何段；机制等价 MC 1.0 cobweb；tools/build_cobweb.py 程序生成原创像素图）
    "default_rail",               # 121 rail（铁轨贴地薄板 flat 贴图；透明底 + 棕色枕木 + 灰铁双轨；Rail 各面=本 tile，
                                  #    mesher 走 PartialBlockGeometry Rail 水平 quad case；机制等价 MC 1.0 rail；
                                  #    tools/build_rail.py 程序生成原创像素图）
    # t485 沙漠神殿结构方块（机制等价 MC 1.0 沙漠神殿 desert temple 的 TNT / 切制砂岩；名称 / 贴图全原创自绘 §9a）。
    #   各由独立 build_*.py 程序生成原创像素图（不透明整立方，与砂岩/箱子同走 culled 立方面路径）。
    "default_tnt",                # 122 tnt（TNT 各面同贴图；深红药柱底 + 横向深棕捆带 + 中央亮黄标识 + 顶部引线点；
                                  #    机制等价 MC 1.0 TNT——可引爆的爆炸物方块；沙漠神殿 TNT 陷阱 + 创造可放置 / 合成；
                                  #    tools/build_tnt.py 程序生成原创像素图）
    "default_cut_sandstone",      # 123 cut_sandstone（切制砂岩各面同贴图；暖沙色平滑底 + 内陷矩形装饰边框，区别于
                                  #    普通砂岩的层理纹；机制等价 MC 1.0 切制砂岩——沙漠神殿金字塔外框装饰变体；
                                  #    tools/build_cut_sandstone.py 程序生成原创像素图）
    # t486 丛林神殿结构方块（机制等价 MC 1.0 丛林神殿 jungle temple 的苔石 / 发射器；名称 / 贴图全原创自绘 §9a）。
    #   各由独立 build_*.py 程序生成原创像素图（不透明整立方，与圆石/砂岩/熔炉同走 culled 立方面路径）。
    "default_mossy_cobble",       # 124 mossy_cobble（苔石各面同贴图；圆石灰底 + 散布暗绿苔藓斑簇；机制等价 MC 1.0
                                  #    mossy cobblestone 长苔圆石变体；丛林神殿主体；tools/build_mossy_cobble.py 程序生成）
    "default_dispenser_top",      # 125 dispenser_top（发射器顶/底面贴图；石质灰底 + 中央圆形排出口俯视环纹；
                                  #    Dispenser 顶/底=本 tile；tools/build_dispenser.py 程序生成原创像素图）
    "default_dispenser_side",     # 126 dispenser_side（发射器侧面贴图；石质灰底 + 边框暗带 + 四角铆钉；
                                  #    Dispenser 三侧面=本 tile；tools/build_dispenser.py 程序生成原创像素图）
    "default_dispenser_front",    # 127 dispenser_front（发射器前面（排出口所朝面）贴图；石质灰底 + 中央暗腔排出口；
                                  #    Dispenser 前面=本 tile（mesher 据 state 选，同熔炉 tileFor 分支）；
                                  #    tools/build_dispenser.py 程序生成原创像素图）
    # t487 要塞结构方块（机制等价 MC 1.0 要塞 stronghold 的石砖 / 末地传送门；名称 / 贴图全原创自绘 §9a）。
    #   各由独立 build_*.py 程序生成原创像素图（石砖走 culled 立方面路径；末地传送门 solid=false 整立方）。
    "default_stone_brick",        # 128 stone_brick（石砖各面同贴图；石质灰底 + 砖块缝纹网格；StoneBrick 各面=本 tile；
                                  #    StoneBrickSlab/StoneBrickStairs 经 tileIndex 取 sideTile 共享本 tile；
                                  #    机制等价 MC 1.0 stone brick——要塞墙体主体；tools/build_stone_brick.py 程序生成）
    "default_end_portal",         # 129 end_portal（末地传送门未激活态各面贴图；深紫黑星空底 + 中心暗绿旋涡；EndPortal
                                  #    各面=本 tile，mesher 据 state bit0 选 129(未激活)/130(激活)；机制等价 MC 1.0 end portal
                                  #    ——要塞传送门房中央；§9 区隔末地为通用描述词；tools/build_end_portal.py 程序生成）
    "default_end_portal_active",  # 130 end_portal_active（末地传送门激活态各面贴图；深紫黑星空底 + 中心亮绿旋涡 + 白绿
                                  #    高光；玩家持末影之眼右键传送门翻 state bit0 → mesher 切本 tile 显激活视觉；
                                  #    不绑定方块 id（EndPortal def 各面=129），属 mesher 据 state 的呈现选择，非方块属性；
                                  #    tools/build_end_portal.py 程序生成）
    # t490 手动 TNT 点火机关方块（机制等价 MC 1.0 lever / wooden button / stone button；无红石故右键激活即点燃邻接
    #   TNT）。各由独立 build_lever_button.py 程序生成原创像素图（不透明贴地薄板，同 WoodPressurePlate 几何）。
    "default_lever",              # 131 lever（杠杆各面同贴图；木质底座 + 中央竖直扳柄 + 顶部圆柄头；右键扳动点燃邻接 TNT）
    "default_wood_button",        # 132 wood_button（木按钮各面同贴图；木质底座 + 中央凸起圆钮；右键按点点燃邻接 TNT）
    "default_stone_button",       # 133 stone_button（石按钮各面同贴图；石质底座 + 中央凸起圆钮；右键按点点燃邻接 TNT）
    "default_furnace_front_on",   # 134 furnace_front_on（t494 熔炉点燃态前面贴图；圆石底 + 拱框 + 拱洞内亮黄橙火焰；
                                  #    mesher 据 Furnace state 的点燃 bit（FurnaceStateLitFlag）选 14(灭)/134(点燃)；
                                  #    机制等价 MC 1.0 熔炉燃烧时正面发光；tools/build_furnace.py 程序生成原创像素图）
    "default_brown_mushroom",     # 135 brown_mushroom（t507 白蘑菇 / 棕蘑菇 cross 贴图；透明底 + 米色菌柄 + 棕色菌盖
                                  #    浅黄褐斑，alphaCutoff cutout；BrownMushroom 各面=本 tile，mesher 走 cross 几何段；
                                  #    机制等价 MC 1.0 brown mushroom；tools/build_brown_mushroom.py 程序生成原创像素图）
    # t565 铁轨转弯 / 交叉贴图（机制等价 MC 1.0 rail corner / crossing；不绑定 BlockDef 瓦片字段 —— Rail def
    #   各面仍 = 121（直轨 NS），corner/cross 由 mesher 据铁轨 state 连接位选贴（同 Water 流水贴图 19/23 的
    #   「呈现层据 state 选瓦片」模式，非方块属性）；tools/build_rail.py 程序生成原创像素图）。
    "default_rail_corner",        # 136 rail_corner（铁轨 90° 拐角：双轨自南边进入向左（西）弯出；其余三向由
                                  #    mesher 换 UV 旋转 / 镜像映射（一张贴图四用））
    "default_rail_cross",         # 137 rail_cross（铁轨十字交叉：南北 + 东西双轨叠交 + 中央方枕木）
    "default_redstone_ore",       # 138 redstone_ore（t569 红石矿石；石头底 + 鲜红菱斑矿粒——复制钻石矿斑块布局
                                  #    改红；RedstoneOre 各面=本 tile；机制等价 MC 1.0 红石矿，走过/挖掘点亮
                                  #    微弱红光；tools/build_ore.py 程序生成原创像素图）
    # t609 投掷器（Dropper）前面贴图（机制等价 MC 1.0 dropper——全部物品弹出掉落物的机关盒）。顶/底/侧复用
    #   熔炉 12/13（机关盒家族石质观感）；本 tile 仅前面（排出口所朝面，mesher 据 state 选，同发射器分支）。
    #   tools/build_dropper.py 程序生成原创像素图（石质灰底 + 中央小方形暗孔，区别发射器的大暗腔）。
    "default_dropper_front",      # 139 dropper_front（投掷器前面（排出口）贴图；石质灰底 + 中央小方形暗孔；
                                  #    Dropper 前面=本 tile；tools/build_dropper.py 程序生成原创像素图）
]
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
OUT = os.path.join(SRC, "atlas.png")

atlas = Image.new("RGBA", (TILE * len(TILES), TILE), (0, 0, 0, 255))
for i, name in enumerate(TILES):
    img = Image.open(os.path.join(SRC, name + ".png")).convert("RGBA")
    if img.size != (TILE, TILE):
        img = img.resize((TILE, TILE), Image.NEAREST)
    atlas.paste(img, (i * TILE, 0))
atlas.save(OUT)
print("wrote", OUT, atlas.size)
