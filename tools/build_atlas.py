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
（CC0 资产，来源见 docs/PLAN.md §L 资产管线；工作台贴图由 tools/build_crafting_table.py、
 熔炉贴图由 tools/build_furnace.py、矿石贴图由 tools/build_ore.py、火把贴图由 tools/build_torch.py、
 基岩贴图由 tools/build_bedrock.py、水贴图由 tools/build_water.py、箱子贴图由 tools/build_chest.py、
 流水贴图由 tools/build_water_flow.py 程序生成原创像素图，§9 override (a)。）
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
