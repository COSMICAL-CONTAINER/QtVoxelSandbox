#!/usr/bin/env python3
"""生成石砖方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t487 要塞结构主体方块（机制等价 MC 1.0 stone brick——石质砖块砌墙）。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）：石质灰底 + 砖块缝纹网格（规则方砖拼接 + 深灰缝），读作「石砖墙」。

视觉意图：读作「规则的石质砖墙」——
  - 主体：石质灰底（中灰，同 stone 美感）。
  - 砖纹：规则砖块拼接网格（2 行 × 2 列大方砖 + 错缝；每砖块内部略噪点表粗糙石面）。
  - 砖缝：深灰缝纹分隔砖块（横向 1 行 + 竖向错位短段，拟砂浆勾缝）。

输出（覆盖写入 textures/）：
  default_stone_brick.png   （tile 128，石砖各面同贴图；石砖台阶 / 楼梯共享本 tile）

依赖：仅 PIL/numpy，无外部贴图。与 build_mossy_cobble.py / build_sandstone.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(487)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def draw_face():
    """石砖面：石质灰底 + 规则砖块拼接网格（错缝）+ 深灰砖缝。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    # 石质灰底（中灰，同 stone 美感）。
    canvas[..., 0] = 122.0
    canvas[..., 1] = 122.0
    canvas[..., 2] = 122.0
    canvas[..., 3] = 255.0
    # 砖块明暗双色噪声（表石面粗糙的明暗差异）。
    lite = np.array([142.0, 142.0, 142.0])
    dark = np.array([98.0, 98.0, 98.0])
    m1 = _RNG.random((TS, TS)) < 0.20
    canvas[m1, 0:3] = lite
    m2 = _RNG.random((TS, TS)) < 0.20
    canvas[m2, 0:3] = dark

    # 深灰砖缝颜色（砂浆勾缝，明显深于砖面）。
    seam = np.array([58.0, 58.0, 58.0])

    # 横向砖缝：第 0 / 8 行整行（把贴图分上下两行砖）。
    for x in range(TS):
        px(canvas, x, 0, seam)
        px(canvas, x, 8, seam)

    # 竖向砖缝（错缝 —— 上行砖缝在 x=8，下行砖缝在 x=0 与 x=8 之间错开）：
    #   上行砖（y 1..7）：竖缝在 x=8（上行两块砖：x 1..7 / x 9..15）。
    for y in range(1, 8):
        px(canvas, 8, y, seam)
    #   下行砖（y 9..15）：竖缝在 x=4 与 x=12（下行三块砖错缝：x 1..3 / x 5..11 / x 13..15）→ 错缝砌墙感。
    for y in range(9, TS):
        px(canvas, 4, y, seam)
        px(canvas, 12, y, seam)

    # 边框暗化（贴图四边 1 像素暗化，拟砖块边缘磨损）。
    edge = np.array([78.0, 78.0, 78.0])
    for i in range(TS):
        px(canvas, i, TS - 1, edge)
        px(canvas, TS - 1, i, edge)

    return canvas


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # 石砖各面同贴图（mesher 整立方路径 6 面统一用 tile 128；石砖台阶/楼梯经 tileIndex 取本方块 sideTile 共享）。
    save(draw_face(), "default_stone_brick")


if __name__ == "__main__":
    main()
