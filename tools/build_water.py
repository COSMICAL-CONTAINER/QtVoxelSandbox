#!/usr/bin/env python3
"""生成水（Water）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 静水（worldgen 在海平面以下低洼列填水；玩家穿过、不可破坏），但贴图为本项目
程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「蓝色静水」——比天空蓝略深、略饱和，带细微的亮暗波纹，让水在场景里一眼可辨
（与石头/草地色相区分）。**纹理本身不透明（alpha=255）**：半透观感由 Main.qml 水材质 opacity=0.7
实现（PrincipledMaterial opacity<1 → 透明通道；纹理 alpha 不参与），故此处画实心蓝即可
（若纹理带 alpha<1 反而可能与材质 opacity 相乘得过透；保持纹理不透明让透明度由材质单一控制）。

图案（固定位置色块 + 伪随机感散布，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：中蓝（#3a6ea5 量级）实心；
  - 波纹：数条横向亮蓝 / 暗蓝细带（拟水面反光与暗涌），刻意不等距、粗细不一，避免网格化人工感；
  - 高光：少量近白蓝点（拟阳光在水面的细碎反光）。

输出（覆盖写入 textures/）：
  default_water.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank():
    """中蓝实心底（alpha=255：透明度由材质 opacity 控制，纹理保持不透明）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 58.0   # R
    canvas[..., 1] = 110.0  # G
    canvas[..., 2] = 165.0  # B（中蓝，与天空蓝/草地绿区分）
    canvas[..., 3] = 255.0  # A（实心）
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def hline(canvas, x0, x1, y, rgb):
    """水平线段（含端点）。"""
    if not (0 <= y < TS):
        return
    for x in range(max(0, x0), min(TS, x1 + 1)):
        px(canvas, x, y, rgb)


def draw_water(canvas):
    """蓝色静水：中蓝底 + 横向亮暗波纹 + 少量高光反光点。"""
    light = np.array([110.0, 170.0, 215.0])  # 亮蓝（波纹反光）
    dark = np.array([40.0, 84.0, 132.0])     # 暗蓝（暗涌阴影）
    hi = np.array([195.0, 220.0, 240.0])     # 近白蓝（细碎阳光反光）

    # 横向波纹（亮 / 暗细带，刻意不等距、长度不一 → 拟自然水面，非网格化）。
    hline(canvas, 1, 4, 2, light)
    hline(canvas, 9, 12, 2, dark)
    hline(canvas, 3, 6, 5, dark)
    hline(canvas, 10, 14, 5, light)
    hline(canvas, 0, 3, 8, light)
    hline(canvas, 7, 11, 8, dark)
    hline(canvas, 12, 15, 8, light)
    hline(canvas, 2, 5, 11, dark)
    hline(canvas, 9, 13, 11, light)
    hline(canvas, 4, 8, 14, light)
    hline(canvas, 11, 15, 14, dark)

    # 细碎高光（单像素阳光反光，散布于亮波纹附近）。
    for (x, y) in [(2, 2), (11, 5), (1, 8), (12, 11), (6, 14)]:
        px(canvas, x, y, hi)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    c = blank()
    draw_water(c)
    save(c, "default_water")


if __name__ == "__main__":
    main()
