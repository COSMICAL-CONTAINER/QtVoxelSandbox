#!/usr/bin/env python3
"""把地形瓦片拼成水平条带纹理图集 atlas.png。

瓦片顺序必须与 chunkgeometry.cpp 里的图集索引一致：
  0 grass_top, 1 grass_side, 2 dirt, 3 stone, 4 sand
（CC0 资产，来源见 docs/PLAN.md §L 资产管线。）
"""
import os
from PIL import Image

TILE = 16
TILES = [
    "default_grass_top",
    "default_grass_side",
    "default_dirt",
    "default_stone",
    "default_sand",
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
