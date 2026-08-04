#!/usr/bin/env python3
"""生成羊毛方块贴图 default_wool.png（16×16 像素，原创程序自绘，§9 override (a)）。

机制等价 MC 1.0 羊毛方块（wool）—— 名称 / 贴图全原创、不拷贝任何 MC 资产（PLAN §9 区隔）。
本脚本程序生成一张 16×16 实心贴图（奶白羊毛底 + 浅灰卷曲绒毛纹 + 偶发深灰阴影斑），
供羊毛方块（BlockRegistry::Wool）六面铺同图（mesher 走 culled 立方面路径，同 stone/dirt）。

图案采用固定位置 + 确定性散布（无随机源 → CI 可复现，与 build_mob.py / build_chest.py 同风格）。

输出（覆盖写入 textures/default_wool.png）。
依赖：仅 PIL。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 配色（机制等价 MC 羊毛 = 白色绒毛块；纯原创自绘 §9a）：
WOOL = (240, 240, 238, 255)        # 主体奶白
WOOL_LIGHT = (255, 255, 255, 255)  # 高光（受光凸起）
WOOL_SHADE = (205, 205, 202, 255)  # 阴影（凹槽卷曲）
WOOL_DARK = (175, 175, 172, 255)   # 深阴影斑（偶发，表绒毛团阴影）


def make_wool():
    img = Image.new("RGBA", (TS, TS), WOOL)
    px = img.load()
    # 确定性散布的「卷曲纹」像素：沿几条对角弧线撒浅灰阴影 + 高光，表绒毛团的起伏。
    #   浅灰阴影（凹）+ 高光（凸）交替 → 羊毛细密卷绒观感（非 MC 资产，纯程序自绘）。
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


def main():
    img = make_wool()
    out = os.path.join(SRC, "default_wool.png")
    img.save(out)
    print("wrote", out, img.size)


if __name__ == "__main__":
    main()
