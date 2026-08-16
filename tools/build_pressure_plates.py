#!/usr/bin/env python3
"""生成压力板族新三件的贴图（16×16 像素，原创自绘，§9 override (a)）。

t627 压力板家族扩展（机制等价 MC 1.0 stone / iron(heavy) / gold(light) pressure plate）。
既存 WoodPressurePlate（贴图=planks 8）/ CobblePressurePlate（贴图=cobble 5）复用整块族贴图；
本脚本为三件新方块生成**独立瓦片**（贴地薄板专用读感——整面是「板」而非整块立方六面同图）。

视觉意图（同族语言：四周边框暗带 + 中央板面 + 材质底噪，读作「贴地的薄触发板」）——
  - stone_pressure_plate：石质灰底 + 边框暗带 + 中央板面微亮内圈（石板触发器）。
  - iron_pressure_plate：  金属浅灰底 + 边框暗带 + 四角铆钉（重质金属板——玩家级重量才触发）。
  - gold_pressure_plate：  金黄底 + 边框暗带 + 中央亮金板面高光（轻质金板——掉落物即可触发）。

输出（覆盖写入 textures/）：
  default_stone_pressure_plate.png  （tile 154，StonePressurePlate 各面同贴图）
  default_iron_pressure_plate.png   （tile 155，IronPressurePlate 各面同贴图）
  default_gold_pressure_plate.png   （tile 156，GoldPressurePlate 各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_lever_button.py / build_dropper.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(6271)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def noisy_base(r, g, b, lite, dark, p_lite=0.18, p_dark=0.18):
    """材质底 + 细密噪点（同 build_lever_button.py 的 wood_base/stone_base 模式）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = float(r)
    canvas[..., 1] = float(g)
    canvas[..., 2] = float(b)
    canvas[..., 3] = 255.0
    m1 = _RNG.random((TS, TS)) < p_lite
    canvas[m1, 0:3] = np.array(lite, dtype=np.float64)
    m2 = _RNG.random((TS, TS)) < p_dark
    canvas[m2, 0:3] = np.array(dark, dtype=np.float64)
    return canvas


def draw_stone_plate():
    """石压力板：石质灰底 + 边框暗带 + 中央板面微亮内圈（贴地石板触发器）。"""
    c = noisy_base(118, 118, 118, (138, 138, 138), (98, 98, 98))
    rim = np.array([84.0, 84.0, 84.0])      # 底座边框暗石
    face = np.array([150.0, 150.0, 150.0])  # 中央板面（略亮于底 → 微凸板面读感）
    face_hi = np.array([166.0, 166.0, 166.0])  # 板面上沿高光
    # 底座四周边框暗带（机关基座感，同 lever/button 的底框语言）。
    rect(c, 0, 0, TS - 1, 0, rim)
    rect(c, 0, TS - 1, TS - 1, TS - 1, rim)
    rect(c, 0, 0, 0, TS - 1, rim)
    rect(c, TS - 1, 0, TS - 1, TS - 1, rim)
    # 中央板面（6..9 内圈方形，微亮 + 顶沿高光 → 踩踏的「板面」）。
    rect(c, 5, 5, 10, 10, face)
    rect(c, 5, 5, 10, 5, face_hi)
    return c


def draw_iron_plate():
    """铁压力板：金属浅灰底 + 边框暗带 + 四角铆钉（重质金属板）。"""
    c = noisy_base(176, 176, 180, (198, 198, 202), (150, 150, 155), 0.12, 0.12)
    rim = np.array((104.0, 104.0, 108.0))    # 金属底座边框暗带
    rivet = np.array((216.0, 216.0, 220.0))  # 铆钉亮金属
    face = np.array((196.0, 196.0, 200.0))   # 中央板面
    rect(c, 0, 0, TS - 1, 0, rim)
    rect(c, 0, TS - 1, TS - 1, TS - 1, rim)
    rect(c, 0, 0, 0, TS - 1, rim)
    rect(c, TS - 1, 0, TS - 1, TS - 1, rim)
    # 中央板面。
    rect(c, 5, 5, 10, 10, face)
    # 四角铆钉（2×2，金属板固定件）。
    for (rx, ry) in ((2, 2), (12, 2), (2, 12), (12, 12)):
        rect(c, rx, ry, rx + 1, ry + 1, rivet)
    return c


def draw_gold_plate():
    """金压力板：金黄底 + 边框暗带 + 中央亮金板面高光（轻质金板）。"""
    c = noisy_base(212, 175, 60, (232, 196, 88), (182, 143, 42), 0.15, 0.15)
    rim = np.array((150.0, 116.0, 30.0))    # 金底座边框暗带
    face = np.array((238.0, 205.0, 92.0))   # 中央板面（亮金）
    face_hi = np.array((252.0, 232.0, 158.0))  # 板面高光（轻质板的「光」读感）
    rect(c, 0, 0, TS - 1, 0, rim)
    rect(c, 0, TS - 1, TS - 1, TS - 1, rim)
    rect(c, 0, 0, 0, TS - 1, rim)
    rect(c, TS - 1, 0, TS - 1, TS - 1, rim)
    # 中央板面 + 双点高光。
    rect(c, 5, 5, 10, 10, face)
    rect(c, 6, 6, 7, 7, face_hi)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_stone_plate(), "default_stone_pressure_plate")
    save(draw_iron_plate(), "default_iron_pressure_plate")
    save(draw_gold_plate(), "default_gold_pressure_plate")


if __name__ == "__main__":
    main()
