#!/usr/bin/env python3
"""生成羊毛方块贴图（16×16 像素，原创程序自绘，§9 override (a)）。

机制等价 MC 1.0 羊毛方块（wool）—— 名称 / 贴图全原创、不拷贝任何 MC 资产（PLAN §9 区隔）。
本脚本程序生成 16×16 实心贴图（奶白羊毛底 + 浅灰卷曲绒毛纹 + 偶发深灰阴影斑），
供羊毛方块六面铺同图（mesher 走 culled 立方面路径，同 stone/dirt）。

t455 补全 16 色 wool：白色（default_wool.png）保留既有奶白卷绒纹（tile 38，零回归）；
其余 15 色 default_wool_<color>.png（tile 79..93）用标准 16 色 RGB 色板对卷绒纹着色
（机制等价 MC 1.0 羊毛 16 色变体；色板为本工程自选近似值，非 MC 资产）。

图案采用固定位置 + 确定性散布（无随机源 → CI 可复现，与 build_mob.py / build_chest.py 同风格）。

输出（覆盖写入 textures/default_wool.png + textures/default_wool_<color>.png）。
依赖：仅 PIL。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 白色羊毛配色（保留既有 default_wool.png 观感，零回归）：
WOOL = (240, 240, 238, 255)        # 主体奶白
WOOL_LIGHT = (255, 255, 255, 255)  # 高光（受光凸起）
WOOL_SHADE = (205, 205, 202, 255)  # 阴影（凹槽卷曲）
WOOL_DARK = (175, 175, 172, 255)   # 深阴影斑（偶发，表绒毛团阴影）

# t455 标准 16 色 RGB 色板（机制等价 MC 1.0 羊毛 16 色；本工程自选近似值，非 MC 资产）。
# 顺序对齐 BlockRegistry::WoolColor（0..15：white/orange/magenta/light_blue/yellow/lime/pink/
# gray/light_gray/cyan/purple/blue/brown/green/red/black）。white 复用既有 default_wool.png，
# 故本表仅生成其余 15 色变体贴图。
WOOL_COLORS = [
    ("orange",     (222, 120,  30)),
    ("magenta",    (185,  75, 165)),
    ("light_blue", ( 70, 150, 210)),
    ("yellow",     (210, 180,  40)),
    ("lime",       ( 95, 175,  45)),
    ("pink",       (225, 145, 175)),
    ("gray",       ( 70,  70,  80)),
    ("light_gray", (155, 155, 160)),
    ("cyan",       ( 65, 135, 145)),
    ("purple",     (130,  60, 165)),
    ("blue",       ( 55,  70, 165)),
    ("brown",      (115,  75,  45)),
    ("green",      ( 70, 130,  55)),
    ("red",        (150,  40,  40)),
    ("black",      ( 30,  30,  38)),
]


def _shade(rgb, k):
    """对基色 rgb 按 k 缩放（k>1 提亮 / k<1 压暗），钳到 0..255。"""
    return tuple(max(0, min(255, int(round(c * k)))) for c in rgb)


def make_wool():
    """白色羊毛（保留既有观感，零回归）。"""
    img = Image.new("RGBA", (TS, TS), WOOL)
    px = img.load()
    # 确定性散布的「卷曲纹」像素：沿几条对角弧线撒浅灰阴影 + 高光，表绒毛团的起伏。
    shade_cells = [
        (1, 2), (2, 3), (3, 3), (4, 4), (5, 5), (6, 6),
        (9, 2), (10, 3), (11, 4), (12, 4), (13, 5),
        (2, 9), (3, 10), (4, 11), (5, 11), (6, 12),
        (9, 9), (10, 10), (11, 11), (12, 12), (13, 12),
        (14, 1), (1, 14), (14, 14),
    ]
    light_cells = [
        (2, 2), (3, 4), (5, 4), (4, 6), (6, 5),
        (10, 2), (12, 3), (11, 5), (13, 4),
        (3, 9), (5, 10), (4, 12), (2, 11),
        (10, 9), (12, 10), (11, 12), (13, 11),
        (7, 1), (8, 2), (7, 14), (8, 13),
    ]
    dark_cells = [
        (0, 7), (15, 8), (7, 0), (8, 15), (0, 0), (15, 15),
    ]
    for (x, y) in shade_cells:
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = WOOL_SHADE
    for (x, y) in light_cells:
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = WOOL_LIGHT
    for (x, y) in dark_cells:
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = WOOL_DARK
    return img


def make_wool_colored(base):
    """彩色羊毛（base = RGB tuple）：同卷绒纹位置着色，由 base 派生 light/shade/dark。

    确定性（无随机）；卷曲纹像素坐标与白色版完全一致 → 16 色观感风格统一（仅色调不同）。
    """
    sheet = base + (255,)
    light = _shade(base, 1.18) + (255,)
    shade = _shade(base, 0.82) + (255,)
    dark = _shade(base, 0.62) + (255,)
    img = Image.new("RGBA", (TS, TS), sheet)
    px = img.load()
    shade_cells = [
        (1, 2), (2, 3), (3, 3), (4, 4), (5, 5), (6, 6),
        (9, 2), (10, 3), (11, 4), (12, 4), (13, 5),
        (2, 9), (3, 10), (4, 11), (5, 11), (6, 12),
        (9, 9), (10, 10), (11, 11), (12, 12), (13, 12),
        (14, 1), (1, 14), (14, 14),
    ]
    light_cells = [
        (2, 2), (3, 4), (5, 4), (4, 6), (6, 5),
        (10, 2), (12, 3), (11, 5), (13, 4),
        (3, 9), (5, 10), (4, 12), (2, 11),
        (10, 9), (12, 10), (11, 12), (13, 11),
        (7, 1), (8, 2), (7, 14), (8, 13),
    ]
    dark_cells = [
        (0, 7), (15, 8), (7, 0), (8, 15), (0, 0), (15, 15),
    ]
    for (x, y) in shade_cells:
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = shade
    for (x, y) in light_cells:
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = light
    for (x, y) in dark_cells:
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = dark
    return img


def main():
    # 白色（保留既有观感）。
    img = make_wool()
    out = os.path.join(SRC, "default_wool.png")
    img.save(out)
    print("wrote", out, img.size)
    # t455 其余 15 色变体（标准 16 色色板，去除 white）。
    for key, base in WOOL_COLORS:
        img = make_wool_colored(base)
        out = os.path.join(SRC, "default_wool_%s.png" % key)
        img.save(out)
        print("wrote", out, img.size)


if __name__ == "__main__":
    main()
