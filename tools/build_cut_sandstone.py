#!/usr/bin/env python3
"""生成切制砂岩（CutSandstone）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t485 沙漠神殿金字塔外框方块（机制等价 MC 1.0 切制砂岩 / 平滑砂岩——装饰用砂岩变体，区别于
普通砂岩 sandstone 的层理纹）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「精密切割的砂岩砖」—— 与砂岩同色系（暖黄沙色），但表面更平滑、带一个内陷的
矩形装饰边框（表「切割打磨」工艺，区别于普通砂岩的横向层理带）。
  - 主体：暖沙色平滑底 + 细密噪点。
  - 内陷矩形边框：四边各留 2px 暗色边（表切割倒角），中央留平滑面。

输出（覆盖写入 textures/）：
  default_cut_sandstone.png   （tile 123，切制砂岩各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(485485)


def sand_base():
    """暖沙色实心底（alpha=255：切制砂岩不透明整立方）。同 build_sandstone.py 沙色。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 222.0  # R
    canvas[..., 1] = 205.0  # G
    canvas[..., 2] = 148.0  # B（暖沙黄，与砂岩一致）
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def speckle(canvas, rng, density=0.22):
    """撒细密噪点（暗/亮交替），表沙粒质感（比普通砂岩更细——「打磨过」）。"""
    dark = np.array([208.0, 190.0, 135.0])
    lite = np.array([235.0, 218.0, 162.0])
    mask = rng.random((TS, TS)) < density
    canvas[mask, 0:3] = dark
    mask2 = rng.random((TS, TS)) < density * 0.6
    canvas[mask2, 0:3] = lite


def draw_face():
    """各面：平滑暖沙底 + 内陷矩形装饰边框（表「切割打磨」工艺，区别于砂岩层理）。"""
    c = sand_base()
    speckle(c, _RNG, density=0.20)
    bevel = np.array([188.0, 168.0, 118.0])   # 内陷边框暗带（切割倒角阴影）
    bevel_hi = np.array([240.0, 222.0, 168.0])  # 内陷边框上沿高光
    # 外框（贴图边缘 1px 暗框，表砖块外缘）。
    rect(c, 0, 0, TS - 1, 0, bevel)
    rect(c, 0, TS - 1, TS - 1, TS - 1, bevel)
    rect(c, 0, 0, 0, TS - 1, bevel)
    rect(c, TS - 1, 0, TS - 1, TS - 1, bevel)
    # 内陷矩形装饰边框（距外缘 2px，表切割倒角的内框；四边 1px 暗带 + 1px 高光上沿）。
    #   水平内框线（rows 2 / 13）。
    rect(c, 2, 2, TS - 3, 2, bevel)
    rect(c, 2, 3, TS - 3, 3, bevel_hi)
    rect(c, 2, TS - 4, TS - 3, TS - 4, bevel_hi)
    rect(c, 2, TS - 3, TS - 3, TS - 3, bevel)
    #   垂直内框线（cols 2 / 13）。
    rect(c, 2, 2, 2, TS - 3, bevel)
    rect(c, 3, 2, 3, TS - 3, bevel_hi)
    rect(c, TS - 4, 2, TS - 4, TS - 3, bevel_hi)
    rect(c, TS - 3, 2, TS - 3, TS - 3, bevel)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_face(), "default_cut_sandstone")


if __name__ == "__main__":
    main()
