#!/usr/bin/env python3
"""生成末影祭坛（末地传送门框）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t620 末影祭坛贴图接入：本工程无独立祭坛框方块（MC 1.0 end portal frame），EndPortal 传送门方块本体
兼作祭坛——侧/底 = 框身石纹（endframe_side）、顶 = 框面（endframe_top，未放末影之眼）、放末影之眼激活
后顶面换「框面 + 中央之眼」合成图（endframe_eye，mesher tileFor 据 state bit0 选）。机制对齐非 MC 美术
照搬，名称 / 贴图纯原创自绘（零 MC 资产）。

视觉意图：读作「灰白色细孔框身的石祭坛」——
  - 框身（side）：冷灰白底 + 细密暗孔点（风化石框）+ 顶 / 底边缘暗化带。
  - 框面（top）：灰白框面 + 四角浅孔 + 中央暗绿凹槽（未放之眼）。
  - 之眼（eye）：框面 + 中央之眼（暗绿底 + 青绿瞳纹 + 暗瞳孔；放之眼后的激活读感）。

输出（覆盖写入 textures/）：
  default_endframe_side.png   （tile 140，祭坛侧 / 底面）
  default_endframe_top.png    （tile 141，祭坛顶面（未放末影之眼））
  default_endframe_eye.png    （tile 142，祭坛顶面（已放末影之眼））

依赖：仅 PIL/numpy，无外部贴图。与 build_end_portal.py / build_stone_brick.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(6201)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def frame_base():
    """冷灰白框身底：灰白底 + 细密暗孔点（风化石框）+ 顶 / 底边缘暗化带。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    # 冷灰白底（区别石砖的暖灰——末地系偏冷色）。
    canvas[..., 0] = 148.0
    canvas[..., 1] = 150.0
    canvas[..., 2] = 142.0
    canvas[..., 3] = 255.0
    # 细密暗孔点（风化细孔；确定性散布）。
    pore = np.array([112.0, 114.0, 106.0])
    for _ in range(22):
        px(canvas, int(_RNG.randint(0, TS)), int(_RNG.randint(0, TS)), pore)
    # 少量亮孔（风化高光）。
    pore_hi = np.array([176.0, 178.0, 170.0])
    for _ in range(8):
        px(canvas, int(_RNG.randint(0, TS)), int(_RNG.randint(0, TS)), pore_hi)
    # 顶 / 底边缘暗化带（分层框身）。
    edge = np.array([104.0, 106.0, 98.0])
    for x in range(TS):
        canvas[0, x, 0:3] = edge
        canvas[TS - 1, x, 0:3] = edge
    return canvas


def draw_side():
    """侧 / 底面：框身（frame_base 原样——侧 / 底无凹槽）。"""
    return frame_base()


def draw_top(with_eye):
    """顶面：灰白框面 + 四角浅孔 + 中央凹槽（with_eye=False 暗绿凹槽 / True 中央之眼亮纹）。"""
    c = frame_base()
    # 四角浅孔（与框身孔同族，顶面观感统一）。
    pore = np.array([112.0, 114.0, 106.0])
    for (cx, cy) in [(2, 2), (13, 2), (2, 13), (13, 13)]:
        c[cy, cx, 0:3] = pore
    # 中央凹槽（8×8 方形区，区别于框面）。
    slot_rim = np.array([92.0, 94.0, 88.0])     # 凹槽缘（暗一圈）
    slot_bg = np.array([64.0, 66.0, 62.0])      # 凹槽底（暗石）
    for y in range(4, 12):
        for x in range(4, 12):
            rim = (x in (4, 11)) or (y in (4, 11))
            c[y, x, 0:3] = slot_rim if rim else slot_bg
    if not with_eye:
        # 未放之眼：凹槽中央暗绿（凹槽深处隐约之眼轮廓）。
        dim_green = np.array([26.0, 66.0, 44.0])
        for y in range(6, 10):
            for x in range(6, 10):
                c[y, x, 0:3] = dim_green
    else:
        # 已放之眼：凹槽中央之眼亮纹（暗绿底 + 青绿瞳 + 暗瞳孔，激活读感）。
        eye_bg = np.array([30.0, 88.0, 58.0])    # 眼底暗绿
        eye_iris = np.array([86.0, 208.0, 150.0]) # 青绿瞳纹（亮）
        eye_pupil = np.array([12.0, 30.0, 22.0])  # 暗瞳孔
        for y in range(6, 10):
            for x in range(6, 10):
                c[y, x, 0:3] = eye_bg
        # 瞳纹环（围绕瞳孔的一圈亮青绿）。
        for y in range(7, 9):
            for x in range(7, 9):
                c[y, x, 0:3] = eye_iris if (x, y) not in ((7, 7), (8, 8)) else eye_pupil
        # 瞳孔中心 2×2 暗点（对角布置，避免机械十字）。
        c[7, 8, 0:3] = eye_pupil
        c[8, 7, 0:3] = eye_pupil
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_side(), "default_endframe_side")
    save(draw_top(with_eye=False), "default_endframe_top")
    save(draw_top(with_eye=True), "default_endframe_eye")


if __name__ == "__main__":
    main()
