#!/usr/bin/env python3
"""生成南瓜茎（PumpkinStem）作物 4 阶段贴图（16×16 像素，原创自绘，§9 override (a)）。

t1105 南瓜农作链：南瓜茎 = 南瓜种子种于耕地长成的 cross 作物（机制等价 MC 1.0 pumpkin stem）。
MelonStem 同门第二实例（同一张生长 / 结果梯，仅结果面 = 南瓜）：4 张阶段贴图覆盖 8 个年龄
（基底 + state/2，t407 carrot/potato 口径）；tile 201..204。

视觉意图（四张 16×16，透明底 cross cutout）：
  default_pumpkin_stem_0.png（tile 201）：嫩芽——中央短茎 + 两片斜展子叶（melon stem stage0 同构）。
  default_pumpkin_stem_1.png（tile 202）：短蔓——底部三叉小蔓。
  default_pumpkin_stem_2.png（tile 203）：立蔓——中轴蔓 + 一根侧卷须。
  default_pumpkin_stem_3.png（tile 204）：成熟蔓——粗壮中轴 + 双侧卷须 + 小黄花点（待结果读感）。

与瓜茎（build_melon.py 197..200）的区隔：同一套蔓形结构，**色调转深绿棕**（叶绿素消退的南瓜蔓
读感），与西瓜茎的黄绿系拉开。

输出（覆盖写入 textures/）：
  default_pumpkin_stem_0.png .. _3.png   （tile 201..204）

依赖：仅 PIL/numpy，无外部贴图。与 build_melon.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 南瓜茎色板（深绿棕系——比瓜茎黄绿一档更沉，叶蔓成熟转木质的南瓜蔓读感）。
STEM_LIGHT = np.array([96.0, 122.0, 54.0])
STEM_BASE = np.array([70.0, 92.0, 34.0])
STEM_DARK = np.array([46.0, 60.0, 22.0])
TENDRIL_DARK = np.array([34.0, 44.0, 16.0])
FLOWER_DOT = np.array([232.0, 198.0, 92.0])  # 小黄花点（南瓜开黄花，原创读感）


def _stem_canvas():
    c = np.zeros((TS, TS, 4), dtype=np.float64)  # 透明底（cross cutout）
    return c


def _stem_pixels(c, cols, light, base, dark):
    """在给定列画一段直立蔓茎（带暗色描边 + 亮侧）。cols = 每行的 x 坐标表（行号 → 茎列）。"""
    for y, x in cols.items():
        c[x, y, 0:3] = base
        c[x, y, 3] = 255.0
        if x + 1 < TS:
            c[x + 1, y, 0:3] = dark
            c[x + 1, y, 3] = 255.0
        if x - 1 >= 0 and y % 3 == 0:
            c[x - 1, y, 0:3] = light
            c[x - 1, y, 3] = 255.0


def draw_stem(stage):
    """南瓜茎 4 阶段（透明底 cross）：stage 0 嫩芽 → 1 短蔓 → 2 立蔓 → 3 成熟蔓（卷须 + 小黄花点）。"""
    c = _stem_canvas()
    if stage == 0:
        # 嫩芽：中央短茎 + 两片斜展子叶（与瓜茎 stage0 同构，色系转深绿棕）。
        c[7, 12, 0:3] = STEM_BASE; c[7, 12, 3] = 255.0
        c[7, 13, 0:3] = STEM_BASE; c[7, 13, 3] = 255.0
        c[7, 14, 0:3] = STEM_DARK; c[7, 14, 3] = 255.0
        for (x, y) in ((6, 12), (5, 11), (6, 13)):
            c[x, y, 0:3] = STEM_LIGHT; c[x, y, 3] = 255.0
        for (x, y) in ((8, 12), (9, 11), (8, 13)):
            c[x, y, 0:3] = STEM_LIGHT; c[x, y, 3] = 255.0
    elif stage == 1:
        # 短蔓：底部三叉小蔓（中蔓直立 + 两侧低卷）。
        _stem_pixels(c, {11: 7, 12: 7, 13: 7, 14: 7}, STEM_LIGHT, STEM_BASE, STEM_DARK)
        for (x, y) in ((5, 14), (6, 13), (9, 14), (10, 13)):
            c[x, y, 0:3] = STEM_LIGHT; c[x, y, 3] = 255.0
    elif stage == 2:
        # 立蔓：中轴蔓长高 + 一根侧卷须。
        _stem_pixels(c, {6: 7, 7: 7, 8: 7, 9: 7, 10: 7, 11: 7, 12: 7, 13: 7, 14: 7},
                     STEM_LIGHT, STEM_BASE, STEM_DARK)
        for (x, y) in ((10, 6), (11, 5), (12, 5), (12, 6)):
            c[x, y, 0:3] = STEM_BASE; c[x, y, 3] = 255.0
        c[12, 7, 0:3] = TENDRIL_DARK; c[12, 7, 3] = 255.0
    else:
        # 成熟蔓：粗壮中轴 + 双侧卷须 + 小黄花点（结果读感——茎熟即待结果）。
        _stem_pixels(c, {4: 7, 5: 7, 6: 7, 7: 7, 8: 7, 9: 7, 10: 7, 11: 7, 12: 7, 13: 7, 14: 7},
                     STEM_LIGHT, STEM_BASE, STEM_DARK)
        for (x, y) in ((4, 5), (5, 4), (6, 5), (10, 5), (11, 4), (12, 5)):
            c[x, y, 0:3] = STEM_BASE; c[x, y, 3] = 255.0
        for (x, y) in ((3, 6), (4, 6), (12, 6), (13, 6)):
            c[x, y, 0:3] = TENDRIL_DARK; c[x, y, 3] = 255.0
        c[9, 3, 0:3] = FLOWER_DOT; c[9, 3, 3] = 255.0    # 小黄花点 1
        c[10, 3, 0:3] = FLOWER_DOT; c[10, 3, 3] = 255.0  # 小黄花点 2
        c[6, 14, 0:3] = STEM_DARK; c[6, 14, 3] = 255.0   # 根部暗点
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    for st in range(4):
        save(draw_stem(st), "default_pumpkin_stem_%d" % st)


if __name__ == "__main__":
    main()
