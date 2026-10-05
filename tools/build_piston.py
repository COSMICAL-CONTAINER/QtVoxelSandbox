#!/usr/bin/env python3
"""生成活塞方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t1135 活塞（Piston；机制等价 MC 1.0 piston——红石驱动推动机关方块）。名称「活塞」为通用机械词
（先于 MC 的通用机械件名），零 MC 专有名词 / 资产；贴图程序生成原创自绘 §9a。

视觉意图：读作「石质机关匣体 + 木质推板面」——
  - 侧/底面（piston_side 211）：石质匣体底 + 顶部 4px 木质推板边带（与推板面同色系相连）+
    匣体螺栓点阵——「缸体」一眼语义。
  - 朝向面（piston_face 210）：木推板 + 居中 8×8 浅木方芯（推板中芯），四角螺栓暗点——
    「推板」一眼语义。
  - alpha 恒不透明（255）：整立方六面无透明语义。

输出（覆盖写入 textures/）：
  default_piston_face.png  （tile 210，朝向面）
  default_piston_side.png  （tile 211，侧/底面）

依赖：仅 PIL/numpy，无外部贴图。与 build_note_block.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 色板：石质匣体（主/亮棱/暗缝）/ 木推板（板面/受光/暗缘）/ 中芯（浅木）/ 螺栓（近黑）。
STONE = np.array([118.0, 118.0, 120.0])     # 匣体主色（中性灰石）
STONE_HI = np.array([140.0, 140.0, 143.0])  # 受光棱线
STONE_DK = np.array([88.0, 88.0, 92.0])     # 缝线 / 阴影
WOOD = np.array([150.0, 116.0, 70.0])       # 推板板面（橡木中调）
WOOD_HI = np.array([176.0, 140.0, 90.0])    # 板面受光
WOOD_DK = np.array([108.0, 80.0, 46.0])     # 板缘 / 缝
CORE = np.array([196.0, 162.0, 112.0])      # 推板中芯（浅木）
BOLT = np.array([52.0, 52.0, 56.0])         # 螺栓（近黑，匣体族内最深）


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0] = rgb[0]
        canvas[y, x, 1] = rgb[1]
        canvas[y, x, 2] = rgb[2]
        canvas[y, x, 3] = 255


def main():
    # ── piston_side（211）：石质匣体 + 顶木带 + 螺栓点阵 ──────────────────────────
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    for y in range(TS):
        for x in range(TS):
            px(img, x, y, STONE)
    for i in range(TS):                    # 受光棱线（上/左，顶光源惯例）
        px(img, i, 0, STONE_HI)
        px(img, 0, i, STONE_HI)
    for g in (5, 10):                      # 匣体竖缝
        for i in range(TS):
            px(img, g, i, STONE_DK)
    for i in range(5):                     # 顶部 5px 木质推板边带（与推板面同色系相连）
        for x in range(TS):
            px(img, x, i, WOOD_DK if i == 4 else WOOD)
    for x in range(TS):
        px(img, x, 0, WOOD_HI)             # 木带受光上缘
    for cx, cy in ((2, 8), (8, 8), (13, 8), (2, 13), (8, 13), (13, 13)):  # 螺栓点阵
        px(img, cx, cy, BOLT)
    img[:, :, 3] = 255
    out_side = os.path.join(SRC, "default_piston_side.png")
    Image.fromarray(img.astype(np.uint8), "RGBA").save(out_side)
    print(f"wrote {out_side} ({TS}x{TS})")

    # ── piston_face（210）：木推板 + 居中方芯 + 四角螺栓 ─────────────────────────
    face = np.zeros((TS, TS, 4), dtype=np.float64)
    for y in range(TS):
        for x in range(TS):
            px(face, x, y, WOOD)
    for g in (0, 5, 10, 15):               # 板缝（木拼板语义，note_block 同门）
        for i in range(TS):
            px(face, g, i, WOOD_DK)
            px(face, i, g, WOOD_DK)
    for i in range(TS):                    # 受光上/左棱线
        px(face, i, 0, WOOD_HI)
        px(face, 0, i, WOOD_HI)
    for y in range(4, 12):                 # 居中 8×8 浅木方芯
        for x in range(4, 12):
            px(face, x, y, CORE)
    for i in range(4, 12):                 # 方芯内缘暗线（立体感）
        px(face, i, 4, WOOD_DK)
        px(face, 4, i, WOOD_DK)
    for cx, cy in ((1, 1), (14, 1), (1, 14), (14, 14)):  # 四角螺栓暗点
        px(face, cx, cy, BOLT)
    face[:, :, 3] = 255
    out_face = os.path.join(SRC, "default_piston_face.png")
    Image.fromarray(face.astype(np.uint8), "RGBA").save(out_face)
    print(f"wrote {out_face} ({TS}x{TS})")


if __name__ == "__main__":
    main()
