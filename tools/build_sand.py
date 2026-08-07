#!/usr/bin/env python3
"""生成沙子（Sand）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t404 调色修正：旧 default_sand.png 偏橙 / 近泥土（均值约 R202 G138 B70，R-G≈64，读作橙色），
不像真实沙滩沙。本脚本重画为明显偏黄的海滩沙色——R≈G（黄）+ B 偏低（暖），读作黄色而非橙色。

视觉意图：读作「松散的浅黄沙滩沙粒」——
  - 底色浅黄（比 sandstone 亮一档：沙松散明亮、砂岩压实偏暗，二者同色系可区分）。
  - 细密噪点表沙粒质感（暗点 / 亮点交替），与 sandstone 顶面同手法但更松散。
  - 无层理、无暗框（区别于砂岩：沙是松散粒，不是成岩）。

输出（覆盖写入 textures/）：
  default_sand.png  （tile 4，沙子六面同贴图；不透明整立方）

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py / build_ore.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_*.py 顺序对齐）。
_RNG = np.random.RandomState(404)


def sand_base():
    """浅黄沙色实心底（alpha=255：不透明整立方，与 sandstone / 石头同走整立方面路径）。

    R≈G（≈230）+ B≈158：R-G≈12（明显黄，非橙），G-B≈72（暖黄偏）。
    比 sandstone 底色 (222,205,148) 亮约 +10/+15，松散沙更明亮。
    """
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 234.0  # R
    canvas[..., 1] = 222.0  # G
    canvas[..., 2] = 158.0  # B（浅黄海滩沙色，明显黄）
    canvas[..., 3] = 255.0
    return canvas


def speckle(canvas, rng, density=0.35):
    """撒细密噪点（暗点 / 亮点交替），表松散沙粒质感。确定性（传入 rng）。"""
    dark = np.array([214.0, 200.0, 138.0])   # 略深沙粒（仍偏黄）
    lite = np.array([248.0, 236.0, 175.0])   # 略亮沙粒高光
    mask = rng.random((TS, TS)) < density
    canvas[mask, 0:3] = dark
    mask2 = rng.random((TS, TS)) < density * 0.6
    canvas[mask2, 0:3] = lite


def draw():
    """顶 / 侧 / 底同图：浅黄沙底 + 细密松散沙粒噪点（无层理、无暗框）。"""
    c = sand_base()
    speckle(c, _RNG, density=0.32)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw(), "default_sand")


if __name__ == "__main__":
    main()
