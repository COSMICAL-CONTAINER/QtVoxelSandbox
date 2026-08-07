#!/usr/bin/env python3
"""生成仙人掌（Cactus）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t394 沙漠群系内容：仙人掌是沙漠标志性植物方块（可放置、接触伤害实体），机制等价 MC 1.0 仙人掌
（cactus）—— 立于沙地、触碰即伤。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「绿色多肉柱状植物」—— 整立方方块，但贴图表达「略收窄的柱 + 棱脊」：
  - cactus_top：顶面绿色 + 同心方框纹（拟截面木质部环 + 中央凹陷），表「仙人掌顶端口」。
  - cactus_side：侧面深绿底 + 垂直棱脊亮带（4 条）+ 棱上小刺点（黄白），表「多肉棱与刺」。
  - cactus_bottom：底面 = 顶面（截面，少见）。

接触伤害逻辑归 EntityManager / PlayerController（本脚本仅出贴图）。

输出（覆盖写入 textures/）：
  default_cactus_top.png   （tile 54，仙人掌顶面 / 底面）
  default_cactus_side.png  （tile 55，仙人掌侧面）

依赖：仅 PIL/numpy，无外部贴图。与 build_spawner.py / build_ore.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

_RNG = np.random.RandomState(3940)


def green_base(r, g, b):
    """绿色实心底（alpha=255：仙人掌不透明整立方，走整立方面路径）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = r
    canvas[..., 1] = g
    canvas[..., 2] = b
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def draw_top():
    """顶面：深绿底 + 同心方框（截面环纹）+ 中央暗点（凹陷）。"""
    c = green_base(58, 118, 64)  # 中绿（多肉截面）
    # 细密噪点（表多肉颗粒感）。
    dark = np.array([48.0, 100.0, 54.0])
    mask = _RNG.random((TS, TS)) < 0.25
    c[mask, 0:3] = dark
    # 同心方框（截面木质部环）：3 层向中心收缩。
    ring_lite = np.array([78.0, 140.0, 80.0])
    ring_dark = np.array([40.0, 86.0, 46.0])
    rect(c, 2, 2, TS - 3, TS - 3, ring_lite)
    rect(c, 2, 2, 2, TS - 3, ring_dark)
    rect(c, TS - 3, 2, TS - 3, TS - 3, ring_dark)
    rect(c, 2, 2, TS - 3, 2, ring_dark)
    rect(c, 2, TS - 3, TS - 3, TS - 3, ring_dark)
    rect(c, 5, 5, TS - 6, TS - 6, ring_lite)
    # 中央暗凹陷点。
    px(c, 7, 7, ring_dark)
    px(c, 8, 8, ring_dark)
    px(c, 7, 8, ring_dark)
    px(c, 8, 7, ring_dark)
    return c


def draw_side():
    """侧面：深绿底 + 4 条垂直棱脊亮带 + 棱上黄白刺点。"""
    c = green_base(50, 104, 56)  # 深绿（多肉皮层）
    # 细密竖向噪点（拟皮纹）。
    dark = np.array([42.0, 88.0, 48.0])
    mask = _RNG.random((TS, TS)) < 0.20
    c[mask, 0:3] = dark
    ridge = np.array([74.0, 136.0, 78.0])    # 棱脊亮带（受光凸棱）
    ridge_hi = np.array([96.0, 158.0, 96.0])  # 棱脊最高光
    spine = np.array([236.0, 226.0, 178.0])   # 黄白刺点
    # 4 条垂直棱脊（x = 2 / 6 / 9 / 13；非等距显自然）。
    for x in [2, 6, 9, 13]:
        rect(c, x, 1, x, TS - 2, ridge)
        rect(c, x, 1, x, 1, ridge_hi)        # 顶端高光
        rect(c, x, TS - 2, x, TS - 2, ridge_hi)
        # 棱上隔几格布刺点。
        for y in [4, 9, 12]:
            px(c, x, y, spine)
    # 左右边缘暗化（表柱体收窄的阴影）。
    rect(c, 0, 0, 0, TS - 1, np.array([38.0, 80.0, 42.0]))
    rect(c, TS - 1, 0, TS - 1, TS - 1, np.array([38.0, 80.0, 42.0]))
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_top(), "default_cactus_top")
    save(draw_side(), "default_cactus_side")


if __name__ == "__main__":
    main()
