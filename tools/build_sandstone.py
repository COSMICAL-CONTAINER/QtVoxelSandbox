#!/usr/bin/env python3
"""生成砂岩（Sandstone）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t394 沙漠群系内容：砂岩是沙子下方石质层（worldgen 在沙漠沙表层下铺砂岩），机制等价 MC 1.0
砂岩（sandstone）—— 沙漠地下成岩。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「沙子压实成岩的石头」—— 与沙子同色系（暖黄沙色）但更致密、有横向层理纹（沉积岩）。
  - sandstone_top：顶面较平滑，暖沙色底 + 细密噪点（表压实沙面，区别于松散沙粒）。
  - sandstone_side：侧面有 2-3 条横向深色层理带（沉积层），强化「成岩」读感。
  - sandstone_bottom：底面 = 侧面层理（少见，复用 side）。

输出（覆盖写入 textures/）：
  default_sandstone_top.png   （tile 52，砂岩顶面）
  default_sandstone_side.png  （tile 53，砂岩侧面 / 底面）

依赖：仅 PIL/numpy，无外部贴图。与 build_spawner.py / build_ore.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(394)


def sand_base():
    """暖沙色实心底（alpha=255：砂岩不透明整立方，与石头 / 沙子同走整立方面路径）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 222.0  # R
    canvas[..., 1] = 205.0  # G
    canvas[..., 2] = 148.0  # B（暖沙黄，介于沙子亮黄与石头灰之间）
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def speckle(canvas, rng, lo, hi, density=0.35):
    """撒细密噪点（lo 暗点 / hi 亮点交替），表沙粒质感。确定性（传入 rng）。"""
    dark = np.array([208.0, 190.0, 135.0])
    lite = np.array([235.0, 218.0, 162.0])
    mask = rng.random((TS, TS)) < density
    canvas[mask, 0:3] = dark
    mask2 = rng.random((TS, TS)) < density * 0.6
    canvas[mask2, 0:3] = lite


def draw_top():
    """顶面：平滑压实沙面 + 细密噪点（区别于松散沙粒的 sand 贴图）。"""
    c = sand_base()
    speckle(c, _RNG, 0, 0, density=0.30)
    # 四边略暗（表压实边缘），1px 暗框。
    rect(c, 0, 0, TS - 1, 0, np.array([198.0, 180.0, 128.0]))
    rect(c, 0, TS - 1, TS - 1, TS - 1, np.array([198.0, 180.0, 128.0]))
    rect(c, 0, 0, 0, TS - 1, np.array([198.0, 180.0, 128.0]))
    rect(c, TS - 1, 0, TS - 1, TS - 1, np.array([198.0, 180.0, 128.0]))
    return c


def draw_side():
    """侧面：暖沙色底 + 2-3 条横向深色层理带（沉积岩层理）。"""
    c = sand_base()
    speckle(c, _RNG, 0, 0, density=0.25)
    stratum = np.array([188.0, 168.0, 118.0])  # 层理暗带
    stratum_hi = np.array([232.0, 214.0, 158.0])  # 层理上沿高光
    # 3 条横向层理带（沉积层；位置 3 / 8 / 12）。
    for y in [3, 8, 12]:
        rect(c, 0, y, TS - 1, y, stratum)
        rect(c, 0, y - 1, TS - 1, y - 1, stratum_hi)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_top(), "default_sandstone_top")
    save(draw_side(), "default_sandstone_side")


if __name__ == "__main__":
    main()
