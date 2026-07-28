#!/usr/bin/env python3
"""生成工作台（CraftingTable）方块的顶面 / 侧面贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 工作台（玩家右键打开 3×3 合成），但贴图为本项目程序生成的原创像素图，
**不**拷贝任何 MC 资产。基底取本工程既有木板（default_wood.png）平均色，保证与木板族配色一致；
顶面叠 3×3 网格刻线（表「合成格」语义），侧面叠暗色工具带 + 镂空方格（表「带工具槽的木柜」语义）。

输出（覆盖写入 textures/）：
  default_crafting_table_top.png  —— 顶面：木板底 + 3×3 网格刻线 + 角点
  default_crafting_table_side.png —— 侧面：木板底 + 顶部暗带 + 中部 2×2 镂空格

可复现：同输入（无随机源）→ 同输出（确定性，便于 CI 校验）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def plank_base():
    """读既有木板贴图，取不透明像素平均色作工作台底色（与木板族配色一致）。"""
    p = os.path.join(SRC, "default_wood.png")
    img = Image.open(p).convert("RGBA").resize((TS, TS), Image.NEAREST)
    arr = np.asarray(img, dtype=np.float64)
    opaque = arr[..., 3] >= 128
    base = arr[opaque][:, 0:3].mean(axis=0) if opaque.any() else np.array([160.0, 120.0, 60.0])
    return base  # (3,) RGB


def new_canvas(base):
    """以 base 色 + 木纹横条铺底（16×16 不透明）。"""
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = base
    c[..., 3] = 255.0
    # 木纹横条：每隔几行略微变暗，模拟木板纹理（原创，非 MC 资产）。
    grain = base * 0.88
    for y in (2, 6, 11, 14):
        c[y, :, 0:3] = grain
    return c


def draw_top(base):
    """顶面 = 木板底 + 3×3 网格刻线（2 竖 + 2 横）+ 角点小方块。"""
    c = new_canvas(base)
    line = base * 0.55     # 刻线暗色
    dot = base * 0.42      # 角点更暗
    # 2 竖刻线（x=5 / x=10）
    for x in (5, 10):
        c[:, x, 0:3] = line
    # 2 横刻线（y=5 / y=10）
    for y in (5, 10):
        c[y, :, 0:3] = line
    # 四个内角点（网格交叉处加深，强化「9 格」可读性）
    for x in (5, 10):
        for y in (5, 10):
            c[y, x, 0:3] = dot
    return c


def draw_side(base):
    """侧面 = 木板底 + 顶部暗带（工具柜上沿）+ 中部 2×2 镂空方格（工具槽）。"""
    c = new_canvas(base)
    band = base * 0.50     # 顶部暗带
    cell = base * 0.62     # 工具槽边框
    hole = base * 0.30     # 镂空内里更暗
    # 顶部暗带（rows 1..3）表「柜面上沿」
    for y in (1, 2, 3):
        c[y, :, 0:3] = band
    # 中部 2×2 镂空方格（rows 7..11, cols 3..6 与 9..12）—— 表「两列工具槽」
    for (x0, y0) in ((3, 7), (9, 7)):
        # 边框 4×4
        for y in range(y0, y0 + 4):
            c[y, x0, 0:3] = cell
            c[y, x0 + 3, 0:3] = cell
        for x in range(x0, x0 + 4):
            c[y0, x, 0:3] = cell
            c[y0 + 3, x, 0:3] = cell
        # 内里 2×2 镂空
        for y in range(y0 + 1, y0 + 3):
            for x in range(x0 + 1, x0 + 3):
                c[y, x, 0:3] = hole
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    base = plank_base()
    save(draw_top(base), "default_crafting_table_top")
    save(draw_side(base), "default_crafting_table_side")


if __name__ == "__main__":
    main()
