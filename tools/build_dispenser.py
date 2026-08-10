#!/usr/bin/env python3
"""生成发射器方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t486 丛林神殿陷阱机关方块（机制等价 MC 1.0 发射器 dispenser——受触发时朝所朝方向发射箭矢弹丸；无红石系统
故用「踩压力板 → 邻接发射器射箭」直接触发）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：石质灰底
机关盒 + 中央暗腔排出口，读作「石质发射机关」。

视觉意图：读作「石质的发射机关盒」——
  - 主体：石质中灰底（同圆石 / 熔炉族石质感），细密噪点表粗糙。
  - 顶/底面（dispenser_top）：中央圆形排出口俯视环纹（同心方框 + 中央暗孔 → 从上方看排出口）。
  - 侧面（dispenser_side）：石质灰底 + 边框暗带（无排出口，侧面平整）。
  - 前面（dispenser_front）：中央暗腔排出口（深色凹陷方框 + 中央更暗孔 → 发射口，朝所朝方向）。

输出（覆盖写入 textures/）：
  default_dispenser_top.png     （tile 125，顶/底面）
  default_dispenser_side.png    （tile 126，三侧面）
  default_dispenser_front.png   （tile 127，前面 / 排出口所朝面）

依赖：仅 PIL/numpy，无外部贴图。与 build_tnt.py / build_mossy_cobble.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(4861)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def stone_base():
    """石质中灰底（同圆石 / 熔炉族石质感）+ 细密噪点。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 118.0  # R
    canvas[..., 1] = 118.0  # G
    canvas[..., 2] = 118.0  # B（中灰石质底）
    canvas[..., 3] = 255.0
    lite = np.array([142.0, 142.0, 142.0])
    dark = np.array([96.0, 96.0, 96.0])
    m1 = _RNG.random((TS, TS)) < 0.20
    canvas[m1, 0:3] = lite
    m2 = _RNG.random((TS, TS)) < 0.20
    canvas[m2, 0:3] = dark
    return canvas


def draw_top():
    """顶/底面：石质灰底 + 中央圆形排出口俯视环纹（同心方框 + 中央暗孔）。"""
    c = stone_base()
    ring = np.array([70.0, 70.0, 70.0])   # 排出口环（深灰）
    hole = np.array([36.0, 36.0, 36.0])   # 中央暗孔
    rim = np.array([150.0, 150.0, 150.0]) # 环外高光边
    # 同心方框（外环 6×6 / 内环 4×4 / 中央暗孔 2×2）→ 俯视排出口的环纹读感。
    rect(c, 5, 5, 10, 10, ring)
    rect(c, 6, 6, 9, 9, rim)   # 环内高光（金属 / 石质排出口内壁反光）
    rect(c, 6, 6, 9, 9, np.array([96.0, 96.0, 96.0]))  # 重置为底色再画暗孔（保环纹清晰）
    rect(c, 5, 5, 10, 10, ring)
    rect(c, 7, 7, 8, 8, hole)  # 中央暗孔（2×2）
    # 四角高光点（机械铆钉感）。
    stud = np.array([160.0, 160.0, 160.0])
    px(c, 2, 2, stud); px(c, TS - 3, 2, stud)
    px(c, 2, TS - 3, stud); px(c, TS - 3, TS - 3, stud)
    return c


def draw_side():
    """侧面：石质灰底 + 边框暗带（无排出口，侧面平整）+ 四角铆钉。"""
    c = stone_base()
    border = np.array([78.0, 78.0, 78.0])  # 边框暗带
    stud = np.array([160.0, 160.0, 160.0]) # 四角铆钉高光
    # 四周边框暗带（1 px）。
    rect(c, 0, 0, TS - 1, 0, border)
    rect(c, 0, TS - 1, TS - 1, TS - 1, border)
    rect(c, 0, 0, 0, TS - 1, border)
    rect(c, TS - 1, 0, TS - 1, TS - 1, border)
    # 四角铆钉（机械固定点）。
    for cx_, cy_ in [(1, 1), (TS - 2, 1), (1, TS - 2), (TS - 2, TS - 2)]:
        px(c, cx_, cy_, stud)
    return c


def draw_front():
    """前面（排出口所朝面）：石质灰底 + 中央暗腔排出口（深色凹陷方框 + 中央更暗孔）。"""
    c = stone_base()
    recess = np.array([60.0, 60.0, 60.0])  # 凹陷暗腔框（深灰）
    hole = np.array([24.0, 24.0, 24.0])    # 中央更暗孔（箭矢出口）
    rim = np.array([150.0, 150.0, 150.0])  # 凹陷上沿高光（立体凹陷感）
    # 中央暗腔排出口（6×6 凹陷框 + 顶沿高光 + 中央 2×2 更暗孔）。
    rect(c, 5, 5, 10, 10, recess)
    rect(c, 5, 5, 10, 5, rim)  # 凹陷顶沿高光（光自上方 → 上沿反光，立体凹陷）
    rect(c, 7, 7, 8, 8, hole)  # 中央更暗孔（箭矢出口）
    # 边框暗带（同侧面，统一机械盒读感）。
    border = np.array([78.0, 78.0, 78.0])
    rect(c, 0, 0, TS - 1, 0, border)
    rect(c, 0, TS - 1, TS - 1, TS - 1, border)
    rect(c, 0, 0, 0, TS - 1, border)
    rect(c, TS - 1, 0, TS - 1, TS - 1, border)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_top(), "default_dispenser_top")
    save(draw_side(), "default_dispenser_side")
    save(draw_front(), "default_dispenser_front")


if __name__ == "__main__":
    main()
