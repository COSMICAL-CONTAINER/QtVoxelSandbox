#!/usr/bin/env python3
"""生成积雪层（SnowLayer）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t395 雪原/针叶群系内容：积雪层是雪原群系地表覆盖（worldgen 在 Snowy 群系草顶替换为积雪层），
机制等价 MC 1.0 积雪（snow）—— 寒冷群系地表覆雪。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「压实的白色雪面」—— 整立方方块，六面同贴图（与沙子 / 石头同走整立方面路径）：
  纯白底 + 极淡的冷蓝阴影 + 细密亮暗噪点（表积雪颗粒 / 冰晶反光），非纯平死白。

输出（覆盖写入 textures/）：
  default_snow.png   （tile 57，积雪层各面贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py / build_cactus.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(3950)


def snow_base():
    """冷白实心底（alpha=255：积雪层不透明整立方，走整立方面路径）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 244.0  # R
    canvas[..., 1] = 246.0  # G
    canvas[..., 2] = 252.0  # B（冷白，略带蓝调显「积雪冷感」）
    canvas[..., 3] = 255.0
    return canvas


def draw_snow():
    """纯白底 + 细密亮暗噪点（积雪颗粒 / 冰晶反光）+ 极淡冷蓝阴影。"""
    c = snow_base()
    # 暗点：略偏冷蓝灰（积雪阴影 / 压实凹陷）。
    shadow = np.array([214.0, 220.0, 234.0])
    mask = _RNG.random((TS, TS)) < 0.22
    c[mask, 0:3] = shadow
    # 亮点：近纯白高光（冰晶反光）。
    sparkle = np.array([255.0, 255.0, 255.0])
    mask2 = _RNG.random((TS, TS)) < 0.14
    c[mask2, 0:3] = sparkle
    # 四角与边缘极淡暗化（表压实边缘 / 非纯平），1px 软框。
    edge = np.array([226.0, 232.0, 244.0])
    for x in range(TS):
        c[0, x, 0:3] = edge
        c[TS - 1, x, 0:3] = edge
        c[x, 0, 0:3] = edge
        c[x, TS - 1, 0:3] = edge
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_snow(), "default_snow")


if __name__ == "__main__":
    main()
