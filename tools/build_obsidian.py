#!/usr/bin/env python3
"""生成黑曜石（Obsidian）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 黑曜石（流水触静岩浆源凝固产物；本工程 t411 流体交互生成），但贴图为本项目程序
生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「极深紫黑、带紫红纹理与微光的硬质火山玻璃」——比石头更深、更紫，混合近黑深紫
底 + 紫红 / 暗紫斑块 + 少量品紫高光（拟玻璃反光），呈「急速冷却凝固的致密玻璃」质感（玩家一眼
能区分它与普通石头 / 基岩）。6 面同贴图。

图案（固定位置色块 + 伪随机感散布，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：极深紫黑（#150c1a）实心 alpha=255（防 Mask blend 渲成黑块，lessons-learned 同族坑）；
  - 斑块：多簇不等大小的暗紫 / 紫红 / 近黑嵌点（刻意不对称、打散，避免网格化人工感）；
  - 高光：少量品紫点（拟致密玻璃微反光，强化「急冷玻璃」的硬质感）。

输出（覆盖写入 textures/）：
  default_obsidian.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank():
    """极深紫黑底（实心 alpha=255；防 Mask blend 渲成黑块，lessons-learned）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 21.0   # R
    canvas[..., 1] = 12.0   # G
    canvas[..., 2] = 26.0   # B（深紫，与基岩的微紫灰、石头的中性灰区分）
    canvas[..., 3] = 255.0  # A（实心）
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    """实心矩形（含端点）。"""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def paint_blobs(canvas, centers, color):
    """在 canvas 上画若干 1~2 像素斑块（centers = [(x, y), ...]）。
    中心 + 4 邻（菱形）填 color，营造不规则紫红纹理嵌点。
    """
    for (cx, cy) in centers:
        px(canvas, cx, cy, color)
        for (dx, dy) in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            px(canvas, cx + dx, cy + dy, color)


def draw_obsidian(canvas):
    """深紫黑火山玻璃：基底 + 多簇不等大紫红 / 暗紫斑块 + 少量品紫高光。"""
    dark_purple = np.array([42.0, 22.0, 54.0])   # 中暗紫（主体纹理斑块）
    mid_purple  = np.array([30.0, 14.0, 40.0])   # 更暗紫（凹陷阴影）
    near_black  = np.array([12.0, 6.0, 18.0])    # 近黑（裂隙 / 冷凝收缩缝）
    purplish    = np.array([58.0, 28.0, 62.0])   # 紫红（异质矿物嵌点）
    highlight   = np.array([104.0, 60.0, 110.0]) # 品紫高光（致密玻璃微反光）

    # 大斑块（2×2 实心矩形）—— 构成主体紫黑纹理，刻意不对称分布。
    rect(canvas, 2, 3, 3, 4, dark_purple)
    rect(canvas, 9, 2, 10, 3, dark_purple)
    rect(canvas, 11, 8, 12, 9, mid_purple)
    rect(canvas, 4, 10, 5, 11, dark_purple)
    rect(canvas, 7, 12, 8, 13, mid_purple)
    rect(canvas, 13, 13, 14, 14, dark_purple)

    # 小斑块（菱形 1px + 邻域）—— 散布的紫红 / 近黑 / 暗紫纹理嵌点。
    paint_blobs(canvas, [(6, 2), (12, 5), (3, 7), (8, 9), (14, 10), (5, 13)], near_black)
    paint_blobs(canvas, [(1, 6), (10, 11), (13, 2)], purplish)
    paint_blobs(canvas, [(7, 5), (2, 12), (11, 14)], mid_purple)

    # 高光（单像素）—— 拟致密玻璃微反光，强化「急冷玻璃」硬质感。位置避开暗斑块。
    for (x, y) in [(5, 4), (10, 6), (3, 9), (12, 12), (8, 14)]:
        px(canvas, x, y, highlight)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    c = blank()
    draw_obsidian(c)
    save(c, "default_obsidian")


if __name__ == "__main__":
    main()
