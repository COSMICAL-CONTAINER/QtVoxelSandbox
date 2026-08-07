#!/usr/bin/env python3
"""生成刷怪笼（Spawner）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 刷怪笼（地下地牢中央放置的方块，玩家在范围内时周期性刷一只敌对 mob，
破坏后停止刷怪），但贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「铁笼里关着东西」——暗色背景 + 铁灰色栅栏（横向 + 纵向条带）+ 中心一团
幽幽光斑（拟笼中生物的轮廓 / 气场，让玩家一眼识别「这不是普通石块」）。6 面同贴图。

图案（固定位置色块，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：暗蓝灰（拟笼内阴影 / 黑暗洞穴感）实心；
  - 栅栏：铁灰色横向 + 纵向条带（拟铁笼骨架）；
  - 中心：一团青绿光斑（拟笼中生物气场；机制等价 MC 刷怪笼内旋转的小怪剪影，本工程取纯色光斑
    非动画 —— 旋转 mob 模型留后续任务）。

输出（覆盖写入 textures/）：
  default_spawner.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank():
    """暗蓝灰实心底（alpha=255：刷怪笼不透明整立方，与箱子 / 工作台同走整立方面路径）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 30.0   # R
    canvas[..., 1] = 32.0   # G
    canvas[..., 2] = 40.0   # B（暗蓝灰，与石头 / 基岩区分；拟笼内阴影）
    canvas[..., 3] = 255.0  # A（实心）
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    """实心矩形（含端点）。"""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def disc(canvas, cx, cy, r, rgb):
    """整数圆盘（半径 r 内填 rgb；越界忽略）。"""
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px(canvas, x, y, rgb)


def draw_spawner(canvas):
    """铁笼：暗底 + 铁灰栅栏（横纵条带）+ 中心青绿光斑（拟笼中生物气场）。"""
    iron_dark = np.array([58.0, 60.0, 70.0])    # 铁条暗部（深铁灰）
    iron_lite = np.array([110.0, 112.0, 122.0]) # 铁条亮部（高光边）
    glow_core = np.array([80.0, 200.0, 150.0])  # 青绿光斑核心（拟生物气场）
    glow_rim  = np.array([50.0, 120.0, 90.0])   # 光斑外圈（衰减）

    # 边框（贴图四边 1 像素铁框，强化「笼」的封闭感）。
    rect(canvas, 0, 0, TS - 1, 0, iron_dark)
    rect(canvas, 0, TS - 1, TS - 1, TS - 1, iron_dark)
    rect(canvas, 0, 0, 0, TS - 1, iron_dark)
    rect(canvas, TS - 1, 0, TS - 1, TS - 1, iron_dark)

    # 横向栅栏（3 条等距铁条，每条 1px 暗底 + 上沿 1px 高光）。
    for y in [4, 8, 12]:
        rect(canvas, 1, y, TS - 2, y, iron_dark)
        rect(canvas, 1, y - 1, TS - 2, y - 1, iron_lite)
    # 纵向栅栏（2 条等距铁条，与横向交叉成笼格）。
    for x in [5, 10]:
        rect(canvas, x, 1, x, TS - 2, iron_dark)
        rect(canvas, x - 1, 1, x - 1, TS - 2, iron_lite)

    # 中心光斑（拟笼中生物气场：外圈衰减 + 青绿核心）。
    disc(canvas, 8, 8, 3, glow_rim)
    disc(canvas, 8, 8, 2, glow_core)
    px(canvas, 8, 8, np.array([180.0, 240.0, 210.0]))  # 最亮核心点


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    c = blank()
    draw_spawner(c)
    save(c, "default_spawner")


if __name__ == "__main__":
    main()
