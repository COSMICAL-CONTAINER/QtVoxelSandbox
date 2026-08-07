#!/usr/bin/env python3
"""生成冰（Ice）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t395 雪原/针叶群系内容：冰是雪原群系水面冻结产物（worldgen freezeSurfaceWater 把 Snowy 群系海/湖
表层水冻结为冰），机制等价 MC 1.0 冰（ice）—— 寒冷群系水面结冰。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「半透的浅蓝冰面」—— 整立方方块，六面同贴图（不透明纹理；观感的「冰」感由冷蓝调 +
反光裂纹表达，不做真半透以避开透明体积网格化的复杂度，§9a 自绘原创）：
  浅蓝底 + 几道更亮的反光裂纹 + 细密亮暗噪点（表冰晶 / 冻结纹路）。

输出（覆盖写入 textures/）：
  default_ice.png   （tile 58，冰各面贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py / build_cactus.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(3951)


def ice_base():
    """浅蓝实心底（alpha=255：冰不透明整立方，走整立方面路径；冷蓝调显「冰」）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 156.0  # R
    canvas[..., 1] = 186.0  # G
    canvas[..., 2] = 220.0  # B（浅蓝，冰冷感）
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def line(canvas, x0, y0, x1, y1, rgb):
    """Bresenham 画线（裂纹折线段）。"""
    dx = abs(x1 - x0)
    dy = abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx - dy
    x, y = x0, y0
    while True:
        px(canvas, x, y, rgb)
        if x == x1 and y == y1:
            break
        e2 = 2 * err
        if e2 > -dy:
            err -= dy
            x += sx
        if e2 < dx:
            err += dx
            y += sy


def draw_ice():
    """浅蓝底 + 细密噪点 + 几道亮反光裂纹（表冰面冻结纹路 / 反光）。"""
    c = ice_base()
    # 细密亮暗噪点（冰晶颗粒）。
    lite = np.array([186.0, 212.0, 240.0])
    dark = np.array([132.0, 162.0, 200.0])
    mask = _RNG.random((TS, TS)) < 0.20
    c[mask, 0:3] = lite
    mask2 = _RNG.random((TS, TS)) < 0.18
    c[mask2, 0:3] = dark
    # 几道亮反光裂纹（折线，自边缘向内延伸；机制等价冰面龟裂反光）。
    crack = np.array([216.0, 234.0, 250.0])
    line(c, 0, 3, 5, 6, crack)
    line(c, 5, 6, 9, 4, crack)
    line(c, 11, 0, 10, 5, crack)
    line(c, 10, 5, 14, 8, crack)
    line(c, 1, 11, 6, 10, crack)
    line(c, 6, 10, 8, 14, crack)
    line(c, 12, 12, 15, 10, crack)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_ice(), "default_ice")


if __name__ == "__main__":
    main()
