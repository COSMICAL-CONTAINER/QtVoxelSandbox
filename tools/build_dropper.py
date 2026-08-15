#!/usr/bin/env python3
"""生成投掷器方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t609 投掷器（Dropper）：机制等价 MC 1.0 dropper——与发射器同族的机关盒，差异 = **全部物品**一律以掉落物
实体从排出口定向弹出（无箭 / 雪球 / 剑弹丸分派，「只投不射」）。名称 / 贴图纯原创自绘（§9 区隔，零 MC
资产 / 专名）。顶/底/侧复用熔炉贴图（furnace_top / furnace_side——机关盒家族石质观感，非本脚本产出）；
本脚本只生成前面（排出口所朝面）。

视觉意图：读作「石质的轻量投放机关盒」——
  - 前面（dropper_front）：石质灰底 + 中央**小**方形暗孔（2×2，比发射器 dispenser_front 的 6×6 大暗腔
    排出口更小更简——「只掉物品不射弹丸」的轻量出口读感）+ 四角铆钉。

输出（覆盖写入 textures/）：
  default_dropper_front.png    （tile 139，前面 / 排出口所朝面；顶/底/侧复用熔炉 12/13）

依赖：仅 PIL/numpy，无外部贴图。与 build_dispenser.py / build_tnt.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(6091)


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


def draw_front():
    """前面（排出口所朝面）：石质灰底 + 中央小方形暗孔（轻量出口，只掉物品不射弹丸）+ 边框暗带 + 四角铆钉。"""
    c = stone_base()
    rim = np.array([150.0, 150.0, 150.0])   # 出口上沿高光（立体凹陷感）
    hole = np.array([30.0, 30.0, 30.0])     # 中央小暗孔（物品出口）
    border = np.array([78.0, 78.0, 78.0])   # 边框暗带（同发射器侧面，统一机关盒读感）
    stud = np.array([160.0, 160.0, 160.0])  # 四角铆钉高光
    # 中央小方形排出口（3×3 暗孔 + 顶沿高光 → 轻量凹陷；明显小于发射器 front 的 6×6 大暗腔）。
    rect(c, 6, 7, 8, 9, hole)
    rect(c, 6, 6, 8, 6, rim)  # 出口顶沿高光（光自上方 → 上沿反光，立体凹陷）
    # 边框暗带（1 px）。
    rect(c, 0, 0, TS - 1, 0, border)
    rect(c, 0, TS - 1, TS - 1, TS - 1, border)
    rect(c, 0, 0, 0, TS - 1, border)
    rect(c, TS - 1, 0, TS - 1, TS - 1, border)
    # 四角铆钉（机械固定点，同发射器侧面）。
    for cx_, cy_ in [(1, 1), (TS - 2, 1), (1, TS - 2), (TS - 2, TS - 2)]:
        px(c, cx_, cy_, stud)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_front(), "default_dropper_front")


if __name__ == "__main__":
    main()
