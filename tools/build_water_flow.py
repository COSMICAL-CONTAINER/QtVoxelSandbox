#!/usr/bin/env python3
"""生成「流水」(Water, flowing) 方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t197 水位视觉：水源(state=0) 用静水贴图(default_water.png，横向波纹)；流水(state=1..7) 用本
贴图(default_water_flow.png)——同蓝色基底，但**斜向条纹**（左上→右下）取代横向波纹，传达「水在
流动方向上有动势」的视觉，配合水面随 level 下降的阶梯感，呈现 MC 式逐格衰减流动。

机制等价 MC 1.0「still_water / flowing_water 双贴图」（机制对齐，非名词照搬）；贴图为本项目程序生成
的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：与 default_water.png 同色系（中蓝基底 #3a6ea5 量级），仅纹理走向不同（斜向 vs 横向）→
玩家一眼区分「这格是流水」（水面更低 + 斜纹）与「这格是水源」（满高 + 横纹）。纹理本身不透明
（alpha=255）：半透观感由 Main.qml 水材质 opacity=0.7 统一控制（与 default_water 同契约）。

图案（固定位置色块，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：中蓝（同 default_water）实心；
  - 斜向亮带：若干左上→右下方向的亮蓝/暗蓝短斜线（拟流水方向反光与流速纹理）；
  - 高光：少量近白蓝点（拟阳光在水面的细碎反光，同 default_water 风格）。

t223 流水动画（机制等价 MC flowing_water flipbook）：输出**两帧** default_water_flow.png（frame 0）+
default_water_flow_2.png（frame 1）。frame 1 把斜向条纹沿 (+1,+1) 方向（= 流动方向）平移半周期，
两帧 flipbook 切换时斜纹看起来「向右下流动」→ 传达水流方向动势（spec「流水流体流动效果，参考 MC」）。
与静水 2 帧同节拍（Main.qml Timer ~800ms 切 phase 0/1，水段重建）。

输出（覆盖写入 textures/）：
  default_water_flow.png        （frame 0）
  default_water_flow_2.png      （frame 1，t223 动画第二帧）

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank():
    """中蓝实心底（同 default_water；alpha=255：透明度由材质 opacity 控制）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 58.0   # R
    canvas[..., 1] = 110.0  # G
    canvas[..., 2] = 165.0  # B（中蓝，与 default_water 同色系）
    canvas[..., 3] = 255.0  # A（实心）
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def diag(canvas, x0, y0, length, rgb):
    """斜向（左上→右下，x+1/y+1 同步）短斜线段，长 length 像素。越界截断。

    方向 = (+1, +1)：拟流水「从左上向右下流动」的视觉动势，与 default_water 的横向波纹走向明显
    区分 → 玩家据此一眼识别流水格。
    """
    for k in range(length):
        px(canvas, x0 + k, y0 + k, rgb)


def draw_flow(canvas, frame=0):
    """蓝色流水：中蓝底 + 左上→右下斜向亮暗短带 + 少量高光反光点。

    frame=0 / 1 两帧（t223 动画）：frame 1 把所有斜带起点沿 (+1,+1) 流动方向平移 1 像素
    （= 半个条纹周期）→ flipbook 切换时斜纹呈「向右下流动」动势（spec「流水流动效果，参考 MC」）。
    越界部分自然丢失（流水贴图边缘本就参差，无明显接缝）。
    """
    light = np.array([110.0, 170.0, 215.0])  # 亮蓝（流水反光，同 default_water 亮蓝）
    dark = np.array([40.0, 84.0, 132.0])     # 暗蓝（流速纹理阴影）
    hi = np.array([195.0, 220.0, 240.0])     # 近白蓝（细碎阳光反光）

    # frame 1：斜带起点沿流动方向 (+1,+1) 平移 1 像素 → 两帧间斜纹「流动」。frame 0 不偏移。
    dx = dy = 1 if frame == 1 else 0

    # 斜向短带（亮/暗交替，刻意不等距、长度不一 → 拟自然流水纹理，非网格化）。
    #   每条 diag(x0, y0, length) 沿 (+1,+1) 方向画 length 像素。
    diag(canvas, 1 + dx, 0 + dy, 4, light)
    diag(canvas, 0 + dx, 2 + dy, 3, dark)
    diag(canvas, 9 + dx, 1 + dy, 5, light)
    diag(canvas, 3 + dx, 4 + dy, 4, dark)
    diag(canvas, 11 + dx, 3 + dy, 4, light)
    diag(canvas, 6 + dx, 6 + dy, 5, dark)
    diag(canvas, 0 + dx, 7 + dy, 4, light)
    diag(canvas, 12 + dx, 7 + dy, 3, dark)
    diag(canvas, 4 + dx, 10 + dy, 5, light)
    diag(canvas, 10 + dx, 10 + dy, 4, dark)
    diag(canvas, 1 + dx, 12 + dy, 4, light)
    diag(canvas, 8 + dx, 12 + dy, 4, dark)

    # 细碎高光（单像素阳光反光，散布于亮斜带附近；frame 1 沿流动方向位移 1 像素）。
    hi_pts = [(3, 2), (10, 4), (7, 7), (12, 9), (5, 12)]
    for (x, y) in hi_pts:
        px(canvas, x + dx, y + dy, hi)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # frame 0（default_water_flow）+ frame 1（default_water_flow_2，t223 动画第二帧）。
    c0 = blank()
    draw_flow(c0, frame=0)
    save(c0, "default_water_flow")
    c1 = blank()
    draw_flow(c1, frame=1)
    save(c1, "default_water_flow_2")


if __name__ == "__main__":
    main()
