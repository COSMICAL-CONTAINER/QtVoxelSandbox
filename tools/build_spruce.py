#!/usr/bin/env python3
"""生成云杉原木（SpruceLog）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t395 雪原/针叶群系内容：云杉原木是雪原群系云杉树的主干（worldgen placeSpruceTreeAt 在 Snowy 群系
种云杉变种树），机制等价 MC 1.0 云杉（spruce log）—— 寒冷群系的针叶树变种。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「深色木质树干」—— 与原木（log）同族但色更深（云杉木特征）：
  - spruce_log_top：截面同心年轮（深棕底 + 向心环纹），表「树干截面」。
  - spruce_log_side：垂直树皮纹（深棕底 + 竖向条带 + 细密皮纹），表「深色树皮」。
  与 default_tree_top / default_tree（原木）同结构、不同色（更深冷棕 → 区分云杉变种）。

输出（覆盖写入 textures/）：
  default_spruce_log_top.png   （tile 59，云杉原木顶面 / 底面）
  default_spruce_log_side.png  （tile 60，云杉原木侧面）

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py / build_cactus.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(3952)


def bark_base(r, g, b):
    """深棕实心底（alpha=255：云杉原木不透明整立方，走整立方面路径）。"""
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
    """顶面：深棕底 + 同心年轮（向心收缩方框）+ 中央暗髓心（树干截面）。"""
    c = bark_base(66, 52, 38)  # 深棕（云杉木截面，比原木更深）
    # 细密噪点（木纹颗粒）。
    dark = np.array([54.0, 42.0, 30.0])
    mask = _RNG.random((TS, TS)) < 0.22
    c[mask, 0:3] = dark
    # 同心年轮（向心收缩方框；机制等价树干截面年轮）。
    ring_lite = np.array([88.0, 70.0, 52.0])
    ring_dark = np.array([48.0, 36.0, 26.0])
    rect(c, 2, 2, TS - 3, TS - 3, ring_lite)
    rect(c, 2, 2, 2, TS - 3, ring_dark)
    rect(c, TS - 3, 2, TS - 3, TS - 3, ring_dark)
    rect(c, 2, 2, TS - 3, 2, ring_dark)
    rect(c, 2, TS - 3, TS - 3, TS - 3, ring_dark)
    rect(c, 5, 5, TS - 6, TS - 6, ring_lite)
    # 中央暗髓心。
    rect(c, 7, 7, 8, 8, ring_dark)
    return c


def draw_side():
    """侧面：深棕底 + 垂直树皮条带（深浅交替）+ 细密竖向皮纹（深色树皮）。"""
    c = bark_base(58, 44, 32)  # 深棕（云杉树皮，比原木更深冷）
    # 细密竖向噪点（拟皮纹）。
    dark = np.array([46.0, 34.0, 24.0])
    mask = _RNG.random((TS, TS)) < 0.20
    c[mask, 0:3] = dark
    # 垂直树皮条带（深浅交替，x = 2 / 5 / 8 / 11 / 14；非等距显自然）。
    ridge_lite = np.array([78.0, 60.0, 44.0])  # 受光凸棱（亮）
    ridge_dark = np.array([42.0, 30.0, 20.0])    # 凹槽（暗）
    for i, x in enumerate([2, 5, 8, 11, 14]):
        band = ridge_lite if (i % 2 == 0) else ridge_dark
        rect(c, x, 1, x, TS - 2, band)
    # 上下边缘暗化（表圆柱树干收端的阴影）。
    rect(c, 0, 0, TS - 1, 0, np.array([38.0, 28.0, 18.0]))
    rect(c, 0, TS - 1, TS - 1, TS - 1, np.array([38.0, 28.0, 18.0]))
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_top(), "default_spruce_log_top")
    save(draw_side(), "default_spruce_log_side")


if __name__ == "__main__":
    main()
