#!/usr/bin/env python3
"""生成沙砾（Gravel）方块的贴图（16×16 像素，原创自绘，§9 override (a)；t761）。

视觉意图：读作「松散的灰色碎砾堆积」——
  - 底色中灰（比 stone 的冷灰略暖略浅；沙砾是砾石不是岩块，更松散斑驳）。
  - 深浅两档卵石碎砾斑（比沙子的细密噪点**颗粒更大更稀疏**——砾石粒 > 沙粒，肉眼可辨
    「这是砾石不是灰沙」；深斑 = 暗砾石、亮斑 = 受光碎石面）。
  - 无层理、无暗框（区别于砂岩 / 石砖的成岩纹理——沙砾是松散碎砾，不成岩）。

与 build_sand.py 同风格（程序生成原创像素图；确定性 seed 同图案可复现）。

输出（覆盖写入 textures/）：
  default_gravel.png  （tile 179，沙砾六面同贴图；不透明整立方）

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；与 build_*.py 家族同一约定，便于复现校验）。
_RNG = np.random.RandomState(761)


def gravel_base():
    """中灰砾石底（alpha=255：不透明整立方，与沙 / 石头同走整立方面路径）。

    (136,130,126)：R≈G≈B 的中性灰、R 略高于 B（极轻暖偏，区别 stone 的冷蓝灰）；
    比 stone 底 (~125,125,125) 略亮 → 同为灰色系但「更浅更斑驳」可区分。
    """
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 136.0  # R
    canvas[..., 1] = 130.0  # G
    canvas[..., 2] = 126.0  # B（中灰轻暖偏）
    canvas[..., 3] = 255.0
    return canvas


def pebbles(canvas, rng):
    """撒深浅两档卵石碎砾斑（颗粒 > 沙子的细噪点，稀疏大块）。

    2×2 小簇（ rng 随机膨胀一角）而非单像素：碎砾是「一小块石头」不是「一粒尘」；
    深斑密度 0.28 / 亮斑 0.14 —— 深斑多于亮斑（阴影面多于受光面，堆积体读感）。
    """
    dark = np.array([104.0, 98.0, 94.0])    # 暗砾石（深灰暖偏）
    lite = np.array([172.0, 166.0, 158.0])  # 受光碎石面（浅灰）
    for _ in range(26):  # 深色碎砾簇（确定性循环计数 → 同图案）
        x, y = rng.randint(0, TS), rng.randint(0, TS)
        canvas[y, x, 0:3] = dark
        # 随机向右 / 下膨胀一格成 2 像素小簇（越界回绕，图集瓦片独立无拼接约束）
        if rng.random() < 0.6:
            canvas[(y + 1) % TS, x, 0:3] = dark
        if rng.random() < 0.4:
            canvas[y, (x + 1) % TS, 0:3] = dark
    for _ in range(13):  # 亮色受光碎砾簇（约深簇半数）
        x, y = rng.randint(0, TS), rng.randint(0, TS)
        canvas[y, x, 0:3] = lite
        if rng.random() < 0.5:
            canvas[(y + 1) % TS, x, 0:3] = lite


def draw():
    """六面同图：中灰砾石底 + 深浅卵石碎砾斑（无层理、无暗框）。"""
    c = gravel_base()
    pebbles(c, _RNG)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw(), "default_gravel")


if __name__ == "__main__":
    main()
