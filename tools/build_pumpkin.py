#!/usr/bin/env python3
"""生成南瓜（Pumpkin）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t482/t483 防御造物：南瓜是雪傀儡 / 铁傀儡的「头部」方块（玩家放置南瓜 + 下方排列 → 触发造物生成），
机制等价 MC 1.0 雪傀儡（南瓜 + 雪块×2）与铁傀儡（南瓜 + 铁块×4 T 形）。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名；机制等价 MC 刻面南瓜 jack o'lantern，原创配色）。

视觉意图（三张 16×16，原创程序生成像素图）：
  default_pumpkin_side.png（tile 117）：橙色南瓜侧面 —— 深橙底 + 纵向瓜棱深纹（3 道从上到下）+ 边缘暗化。
  default_pumpkin_face.png（tile 118）：橙色南瓜刻面前面 —— 深橙底 + 瓜棱 + 顶部中央短茎 + 刻面双眼
      （倒三角橙色发光眼）+ 锯齿嘴（水平折线，暖黄刻口）→ 造物头朝向玩家侧（Pumpkin frontTile=-Z）。
  default_pumpkin_top.png（tile 119）：橙色南瓜顶/底面 —— 深橙底 + 瓜棱 + 中央短茎（棕色四瓣小茎）+ 边缘暗化。

输出（覆盖写入 textures/）：default_pumpkin_side.png / default_pumpkin_face.png / default_pumpkin_top.png

依赖：仅 PIL/numpy，无外部贴图。与 build_snow.py / build_anvil.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 南瓜主色板（原创橙色系：暖橙底 + 深橙瓜棱 + 亮橙高光）。
ORANGE_BASE = np.array([230.0, 122.0, 40.0])     # 深暖橙底
ORANGE_RIDGE = np.array([196.0, 92.0, 24.0])     # 瓜棱深纹
ORANGE_HIGH = np.array([252.0, 170.0, 74.0])     # 高光亮橙
CARVE_COLOR = np.array([255.0, 210.0, 90.0])     # 刻口暖黄（双眼 / 嘴）
STEM_BASE = np.array([110.0, 84.0, 48.0])        # 茎棕
STEM_DARK = np.array([74.0, 56.0, 30.0])         # 茎暗


def _canvas():
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = ORANGE_BASE
    c[..., 3] = 255.0
    return c


def _draw_ridges(c):
    """纵向瓜棱深纹：3 道竖纹（x=3/7/11）+ 相邻高光 → 南瓜圆胖瓜棱感。"""
    for x in (3, 7, 11):
        c[x, 0:TS, 0:3] = ORANGE_RIDGE
        if x - 1 >= 0:
            c[x - 1, 0:TS, 0:3] = ORANGE_HIGH  # 每道棱左侧高光
    # 边缘暗化（圆胖体积感）。
    c[0, 0:TS, 0:3] = ORANGE_RIDGE
    c[TS - 1, 0:TS, 0:3] = ORANGE_RIDGE
    c[0:TS, 0, 0:3] = ORANGE_RIDGE
    c[0:TS, TS - 1, 0:3] = ORANGE_RIDGE


def _draw_stem(c, cx, cy):
    """顶部短茎（棕色四瓣小茎，中心在 (cx,cy)）。"""
    for (dx, dy) in ((0, 0), (1, 0), (0, -1), (-1, 0), (0, 1)):
        x, y = cx + dx, cy + dy
        if 0 <= x < TS and 0 <= y < TS:
            c[x, y, 0:3] = STEM_BASE
    c[cx, cy, 0:3] = STEM_DARK


def _draw_carved_face(c):
    """刻面双眼 + 锯齿嘴（暖黄刻口 → 造物头「发光脸」观感）。"""
    # 双眼：倒三角（y=5 底两角、y=4 顶角），左右对称。
    for ey in range(3, 6):
        for ex in range(3, 7):
            if ex in (3, 6) and ey == 3:
                continue  # 顶角只留中缝
            c[ex, ey, 0:3] = CARVE_COLOR
            c[TS - 1 - ex, ey, 0:3] = CARVE_COLOR
    # 锯齿嘴：两段折线（左右各一个锯齿），y=9..11。
    for x in range(2, 8):
        yy = 9 + ((x - 2) % 2)
        c[x, yy, 0:3] = CARVE_COLOR
        c[TS - 1 - x, yy, 0:3] = CARVE_COLOR
    # 嘴两端上翘小角。
    c[2, 9, 0:3] = CARVE_COLOR
    c[13, 9, 0:3] = CARVE_COLOR


def draw_side():
    c = _canvas()
    _draw_ridges(c)
    return c


def draw_face():
    c = _canvas()
    _draw_ridges(c)
    _draw_stem(c, 8, 1)
    _draw_carved_face(c)
    return c


def draw_top():
    c = _canvas()
    _draw_ridges(c)
    _draw_stem(c, 8, 8)  # 茎居中
    # 顶面加一圈浅色瓜棱高光（俯视圆顶感）。
    c[0:TS, 4, 0:3] = ORANGE_HIGH
    c[0:TS, 11, 0:3] = ORANGE_HIGH
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_side(), "default_pumpkin_side")
    save(draw_face(), "default_pumpkin_face")
    save(draw_top(), "default_pumpkin_top")


if __name__ == "__main__":
    main()
