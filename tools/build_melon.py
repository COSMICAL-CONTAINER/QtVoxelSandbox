#!/usr/bin/env python3
"""生成西瓜（Melon）方块与瓜茎（MelonStem）作物的贴图（16×16 像素，原创自绘，§9 override (a)）。

t1103 闪烁西瓜生存整链：瓜块 = 瓜茎结果 + 9 瓣合成的存储/收获方块（机制等价 MC 1.0 melon），
瓜茎 = 种瓜种于耕地长成的 cross 作物（机制等价 MC 1.0 melon stem；茎蔓完整原型候选池登记，
本工程以作物方块 state 承载茎，4 张阶段贴图覆盖 8 个年龄，t407 carrot/potato 口径）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名；「西瓜」为通用词）。

视觉意图（六张 16×16，原创程序生成像素图）：
  default_melon_top.png（tile 195）：瓜块顶/底面 —— 深绿底 + 浅绿不规则网格纹（瓜皮网纹）+ 边缘暗化。
  default_melon_side.png（tile 196）：瓜块侧面 —— 深绿底 + 纵向波浪棱带（浅绿 / 暗绿交替，无刻面）。
  default_melon_stem_0.png..default_melon_stem_3.png（tile 197..200）：瓜茎 4 视觉阶段 —— 透明底 +
      绿色蔓茎；阶段越低茎越短，越高越卷曲粗壮；末阶段（stage 3 = 成熟）顶部带深色卷须 + 小花点。

输出（覆盖写入 textures/）：
  default_melon_top.png                （tile 195）
  default_melon_side.png               （tile 196）
  default_melon_stem_0.png .. _3.png   （tile 197..200）

依赖：仅 PIL/numpy，无外部贴图。与 build_pumpkin.py / build_carrot_potato.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 西瓜主色板（原创绿色系：深绿底 + 浅绿网纹 + 高光；与甘蔗 / 草丛绿系区隔——偏青的瓜皮绿）。
MELON_BASE = np.array([58.0, 122.0, 48.0])      # 瓜皮深绿底
MELON_NET = np.array([132.0, 184.0, 96.0])      # 网纹浅绿
MELON_DARK = np.array([38.0, 84.0, 30.0])       # 瓜皮暗绿（棱带 / 边缘）
MELON_HIGH = np.array([168.0, 212.0, 120.0])    # 高光亮绿
# 瓜茎色板（蔓茎绿，比瓜皮更黄绿一档；卷须深绿）。
STEM_LIGHT = np.array([122.0, 156.0, 58.0])
STEM_BASE = np.array([88.0, 120.0, 40.0])
STEM_DARK = np.array([56.0, 80.0, 24.0])
TENDRIL_DARK = np.array([44.0, 62.0, 18.0])
FLOWER_DOT = np.array([228.0, 212.0, 130.0])    # 小花点（黄瓜开小黄花，原创读感）


def _melon_canvas():
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = MELON_BASE
    c[..., 3] = 255.0
    return c


def _stem_canvas():
    c = np.zeros((TS, TS, 4), dtype=np.float64)  # 透明底（cross cutout）
    return c


def draw_melon_top():
    """瓜顶/底面：深绿底 + 不规则浅绿网格纹（瓜皮网纹读感）+ 边缘暗化。"""
    c = _melon_canvas()
    # 网格纹：两横两纵浅绿条带 + 对角短线段 → 不规则网纹。
    for y in (4, 10):
        c[0:TS, y, 0:3] = MELON_NET
    for x in (4, 10):
        c[x, 0:TS, 0:3] = MELON_NET
    for (x0, y0) in ((1, 1), (13, 1), (1, 13), (13, 13), (7, 7)):
        c[x0, y0, 0:3] = MELON_NET
        c[x0 + 1, y0, 0:3] = MELON_DARK
        c[x0, y0 + 1, 0:3] = MELON_DARK
    # 边缘暗化（体积感）。
    c[0, 0:TS, 0:3] = MELON_DARK
    c[TS - 1, 0:TS, 0:3] = MELON_DARK
    c[0:TS, 0, 0:3] = MELON_DARK
    c[0:TS, TS - 1, 0:3] = MELON_DARK
    return c


def draw_melon_side():
    """瓜侧：深绿底 + 纵向波浪棱带（浅绿 / 暗绿交替）——瓜皮条纹读感，无刻面（MC melon 无 face 语义）。"""
    c = _melon_canvas()
    for x in (2, 6, 10, 14):
        c[x, 0:TS, 0:3] = MELON_DARK          # 暗绿棱带
        c[x - 1, 0:TS, 0:3] = MELON_NET       # 棱带左浅绿高光
        # 波浪：每条带中段点几个暗点 → 波纹读感。
        c[x - 1, 3, 0:3] = MELON_DARK
        c[x - 1, 9, 0:3] = MELON_DARK
        c[x, 6, 0:3] = MELON_NET
        c[x, 12, 0:3] = MELON_NET
    # 边缘暗化。
    c[0, 0:TS, 0:3] = MELON_DARK
    c[TS - 1, 0:TS, 0:3] = MELON_DARK
    c[0:TS, 0, 0:3] = MELON_DARK
    c[0:TS, TS - 1, 0:3] = MELON_DARK
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
    """瓜茎 4 阶段（透明底 cross）：stage 0 嫩芽 → 1 短蔓 → 2 立蔓 → 3 成熟蔓（卷须 + 小花点）。"""
    c = _stem_canvas()
    if stage == 0:
        # 嫩芽：中央短茎 + 两片斜展子叶（carrot/potato stage0 同款「刚发芽」形态）。
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
        # 成熟蔓：粗壮中轴 + 双侧卷须 + 小花点（结果读感——茎熟即待结果）。
        _stem_pixels(c, {4: 7, 5: 7, 6: 7, 7: 7, 8: 7, 9: 7, 10: 7, 11: 7, 12: 7, 13: 7, 14: 7},
                     STEM_LIGHT, STEM_BASE, STEM_DARK)
        for (x, y) in ((4, 5), (5, 4), (6, 5), (10, 5), (11, 4), (12, 5)):
            c[x, y, 0:3] = STEM_BASE; c[x, y, 3] = 255.0
        for (x, y) in ((3, 6), (4, 6), (12, 6), (13, 6)):
            c[x, y, 0:3] = TENDRIL_DARK; c[x, y, 3] = 255.0
        c[9, 3, 0:3] = FLOWER_DOT; c[9, 3, 3] = 255.0    # 小花点 1
        c[10, 3, 0:3] = FLOWER_DOT; c[10, 3, 3] = 255.0  # 小花点 2
        c[6, 14, 0:3] = STEM_DARK; c[6, 14, 3] = 255.0   # 根部暗点
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_melon_top(), "default_melon_top")
    save(draw_melon_side(), "default_melon_side")
    for st in range(4):
        save(draw_stem(st), "default_melon_stem_%d" % st)


if __name__ == "__main__":
    main()
