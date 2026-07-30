#!/usr/bin/env python3
"""生成矿石（CoalOre / IronOre）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 矿石（嵌于 stone 区段、需镐采掘），但贴图为本项目程序生成的原创像素图，
**不**拷贝任何 MC 资产。基底取本工程既有石头（default_stone.png）逐像素副本，保证与
「石族」纹理一致（视觉上读得出「石头里嵌着矿」）；矿石用固定位置的色块簇点缀：

  - 煤矿（coal_ore）：石头底 + 多簇近黑斑块（煤层外露，配少量高光暗化边）；
  - 铁矿（iron_ore）：石头底 + 多簇棕橙斑点（铁锈色，配浅色高光）。

色块位置固定（无随机源）→ 同输入同输出（确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
斑块布局刻意打散、不对称，避免「网格化 / 重复纹理」的人工感。

输出（覆盖写入 textures/）：
  default_coal_ore.png
  default_iron_ore.png

依赖：本脚本须先有 textures/default_stone.png（既有 CC0/原创资产）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def stone_base():
    """读既有石头贴图原样作底（逐像素副本 → 矿石嵌于真实石头纹理中，族内一致）。"""
    p = os.path.join(SRC, "default_stone.png")
    img = Image.open(p).convert("RGBA").resize((TS, TS), Image.NEAREST)
    return np.asarray(img, dtype=np.float64).copy()


def paint_blobs(canvas, centers, radius, color, highlight=None):
    """在 canvas 上画若干圆形色块（半径 radius 的菱形/十字邻域，像素硬边）。
    centers = [(x, y), ...]；color = (r, g, b)；highlight 可选高光色（画在色块左上偏移一格）。
    """
    for (cx, cy) in centers:
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if abs(dx) + abs(dy) > radius:
                    continue  # 菱形邻域（曼哈顿距离 ≤ radius）
                x, y = cx + dx, cy + dy
                if 0 <= x < TS and 0 <= y < TS:
                    canvas[y, x, 0:3] = color
        if highlight is not None:
            # 左上偏移一格画高光（拟光源从左上）
            hx, hy = cx - 1, cy - 1
            if 0 <= hx < TS and 0 <= hy < TS:
                canvas[hy, hx, 0:3] = highlight


def draw_coal(canvas):
    """煤矿：石头底 + 多簇近黑煤层斑块（散布，刻意不对称；少量高光暗化）。"""
    coal_dark = np.array([28.0, 28.0, 30.0])      # 近黑（煤层外露）
    coal_hi = np.array([70.0, 70.0, 74.0])        # 高光（边缘反光）
    # 簇心位置（固定；打散不对称，避开边角溢出）
    centers = [(3, 4), (10, 3), (12, 9), (5, 11), (8, 7)]
    paint_blobs(canvas, centers, radius=1, color=coal_dark, highlight=coal_hi)
    return canvas


def draw_iron(canvas):
    """铁矿：石头底 + 多簇棕橙铁锈斑点（散布；浅色高光显金属反光）。"""
    iron_base = np.array([196.0, 144.0, 92.0])    # 棕橙（铁锈外露）
    iron_hi = np.array([238.0, 196.0, 140.0])     # 高光（金属反光）
    centers = [(4, 3), (11, 4), (3, 10), (12, 11), (8, 8), (7, 12)]
    paint_blobs(canvas, centers, radius=1, color=iron_base, highlight=iron_hi)
    return canvas


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_coal(stone_base()), "default_coal_ore")
    save(draw_iron(stone_base()), "default_iron_ore")


if __name__ == "__main__":
    main()
