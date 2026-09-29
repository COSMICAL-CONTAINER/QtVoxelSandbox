#!/usr/bin/env python3
"""Generate the sign board tile (16x16, original procedural pixel art).

t1113 roster large-item batch: one new atlas tile 209 (sign_board — oak plank
base + darker border frame + four faint text-line bands, matching the 1.0
sign-board silhouette). Both sign ids (StandingSign / WallSign) share this
tile; the wall variant differs only in geometry placement, not art. §9a
original pixel art, zero MC assets.
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16

# 橡木板配色（§9a 原创自绘——与 planks 瓦同族的暖木底，零 MC 文本专名资产）。
WOOD = (186.0, 140.0, 76.0)
WOOD_L = (204.0, 160.0, 92.0)
WOOD_D = (148.0, 106.0, 54.0)
FRAME = (108.0, 76.0, 38.0)   # 板面边框（暗木框——1.0 sign board 轮廓带口径）
LINE = (128.0, 92.0, 46.0)    # 四行淡文本带（未写字的空行刻线）


def draw_sign_board():
    """板面：橡木板底 + 四周暗框 + 对角高光 + 四行水平淡文本带（行带等距、不出框）。"""
    c = np.zeros((TS, TS, 4), np.float64)
    c[..., 0:3] = WOOD
    c[..., 3] = 255.0
    # 四周暗框（1px）。
    c[0, :, 0:3] = FRAME
    c[TS - 1, :, 0:3] = FRAME
    c[:, 0, 0:3] = FRAME
    c[:, TS - 1, 0:3] = FRAME
    # 对角高光斜线（左上受光，与工程程序瓦同族）。
    for i in range(1, TS - 1):
        c[i, i + 1, 0:3] = WOOD_L
    # 木纹暗纹（三条水平木理线）。
    for y in (4, 9, 13):
        for x in range(2, TS - 2):
            if x % 3 != 1:
                c[y, x, 0:3] = WOOD_D
    # 四行淡文本带（等距 4 行：y=5,7,9,11；带首尾各缩 3px 不触框——空行刻线观感）。
    for y in (5, 7, 9, 11):
        for x in range(3, TS - 3):
            c[y, x, 0:3] = LINE
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    save(draw_sign_board(), "default_sign_board")
