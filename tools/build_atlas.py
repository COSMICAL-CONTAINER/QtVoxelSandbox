#!/usr/bin/env python3
"""把地形瓦片拼成水平条带纹理图集 atlas.png。

瓦片顺序必须与 chunkgeometry.cpp 的 N + BlockRegistry::tileIndex 注释一致
（一个偏差即渗色/错贴）：
  0 grass_top / 1 grass_side / 2 dirt / 3 stone / 4 sand
  5 cobble / 6 log_top / 7 log_side / 8 planks / 9 leaves
  10 crafting_table_top / 11 crafting_table_side
  12 furnace_top / 13 furnace_side / 14 furnace_front
  15 coal_ore / 16 iron_ore
  17 torch（t88，程序生成原创像素图）
  18 bedrock（t119，程序生成原创像素图）
  19 water（t148，程序生成原创像素图，纹理不透明；半透由 Main.qml 水材质 opacity=0.7 实现）
  20 chest_top / 21 chest_side / 22 chest_front（t173，程序生成原创像素图；箱子方块，机制等价 MC 1.0 箱子）
  23 water_flow（t197，程序生成原创像素图；**流水专用**贴图——水源 state=0 仍用 tile 19 静水（横向波纹），
     流水 state=1..7 用本 tile（左上→右下斜向条纹），与水面随 level 下降的阶梯感配合，传达「逐格衰减流动」。
     该 tile 不绑定任何方块 id（Water 方块 def 仍各面=19），mesher 在水段按 cell 的 state 选 19/23——属
     渲染层呈现选择，非方块属性。tools/build_water_flow.py 生成。）
  24 water_2（t223 水贴图动画第二帧：静水 flipbook frame 1——波纹位置相对 tile 19 略下移 1 像素 + 高光位移，
     mesher 在水段据慢速 waterAnimPhase 0/1 在 tile 19/24 间切换 → 静水轻微荡漾感。不绑定方块 id；属渲染层
     呈现选择，非方块属性。tools/build_water.py 生成。）
  25 water_flow_2（t223 流水动画第二帧：流水 flipbook frame 1——斜纹沿流动方向 (+1,+1) 平移半周期，
     mesher 据水段 waterAnimPhase 在 tile 23/25 间切换 → 斜纹「向右下流动」动势。不绑定方块 id；属渲染层
     呈现选择，非方块属性。tools/build_water_flow.py 生成。）
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
