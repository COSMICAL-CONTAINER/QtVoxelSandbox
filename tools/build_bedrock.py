#!/usr/bin/env python3
"""生成基岩（Bedrock）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 基岩（世界底层不可破坏方块，hardness=-1.0 → canMine=false，任何模式 / 工具
均不可破，防创造秒破底层），但贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「深灰斑驳的不可破坏底岩」——比石头更暗、更杂色，混合近黑 / 深灰 / 暗紫灰斑块，
呈现「极度压实、混沌、不属于常规石层」的质感（玩家一眼能区分它与普通石头）。6 面同贴图。

图案（固定位置色块 + 伪随机感散布，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：深灰近黑（#2b2b33）实心 alpha=255（防 Mask blend 渲成黑块，lessons-learned 同族坑）；
  - 斑块：多簇不等大小的深灰 / 暗紫灰 / 极黑嵌点（刻意不对称、打散，避免网格化人工感）；
  - 高光：少量浅灰点（拟矿物结晶反光，强化「不可破坏」的硬质感）。

输出（覆盖写入 textures/）：
  default_bedrock.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank():
    """深灰近黑底（实心 alpha=255；防 Mask blend 渲成黑块，lessons-learned）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 43.0   # R
    canvas[..., 1] = 43.0   # G
    canvas[..., 2] = 51.0   # B（微紫灰，与纯黑石头区分）
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


def paint_blobs(canvas, centers, color):
    """在 canvas 上画若干 1~2 像素斑块（centers = [(x, y), ...]）。
    中心 + 4 邻（菱形）填 color，营造不规则深色矿物嵌点。
    """
    for (cx, cy) in centers:
        px(canvas, cx, cy, color)
        for (dx, dy) in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            px(canvas, cx + dx, cy + dy, color)


def draw_bedrock(canvas):
    """深灰斑驳底岩：基底 + 多簇不等大暗色斑块 + 少量高光。"""
    dark_grey  = np.array([54.0, 54.0, 62.0])   # 中深灰（主体斑块）
    mid_grey   = np.array([38.0, 38.0, 46.0])   # 更暗灰（凹陷阴影）
    black      = np.array([20.0, 20.0, 26.0])   # 近黑（裂隙）
    purple_grey= np.array([46.0, 40.0, 52.0])   # 暗紫灰（异质矿物嵌点）
    highlight  = np.array([86.0, 86.0, 96.0])   # 浅灰高光（结晶反光）

    # 大斑块（2×2 实心矩形）—— 构成主体斑驳纹理，刻意不对称分布。
    rect(canvas, 2, 3, 3, 4, dark_grey)
    rect(canvas, 9, 2, 10, 3, dark_grey)
    rect(canvas, 11, 8, 12, 9, mid_grey)
    rect(canvas, 4, 10, 5, 11, dark_grey)
    rect(canvas, 7, 12, 8, 13, mid_grey)
    rect(canvas, 13, 13, 14, 14, dark_grey)

    # 小斑块（菱形 1px + 邻域）—— 散布的暗色矿物嵌点 / 异质紫灰点。
    paint_blobs(canvas, [(6, 2), (12, 5), (3, 7), (8, 9), (14, 10), (5, 13)], black)
    paint_blobs(canvas, [(1, 6), (10, 11), (13, 2)], purple_grey)
    paint_blobs(canvas, [(7, 5), (2, 12), (11, 14)], mid_grey)

    # 高光（单像素）—— 拟矿物结晶反光，强化硬质感。位置避开暗斑块。
    for (x, y) in [(5, 4), (10, 6), (3, 9), (12, 12), (8, 14)]:
        px(canvas, x, y, highlight)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    c = blank()
    draw_bedrock(c)
    save(c, "default_bedrock")


if __name__ == "__main__":
    main()
