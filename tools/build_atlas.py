#!/usr/bin/env python3
"""把地形瓦片拼成水平条带纹理图集 atlas.png。

瓦片顺序必须与 chunkgeometry.cpp 的 N + BlockRegistry::tileIndex 注释一致
（一个偏差即渗色/错贴）：
  0 grass_top / 1 grass_side / 2 dirt / 3 stone / 4 sand
  5 cobble / 6 log_top / 7 log_side / 8 planks / 9 leaves
  10 crafting_table_top / 11 crafting_table_side
  12 furnace_top / 13 furnace_side / 14 furnace_front
  15 coal_ore / 16 iron_ore
（CC0 资产，来源见 docs/PLAN.md §L 资产管线；工作台贴图由 tools/build_crafting_table.py、
 熔炉贴图由 tools/build_furnace.py、矿石贴图由 tools/build_ore.py 程序生成原创像素图，§9 override (a)。）
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
