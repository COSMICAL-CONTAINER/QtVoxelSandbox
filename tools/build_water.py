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

t223 水贴图动画（机制等价 MC 静水 flipbook）：输出**两帧** default_water.png（frame 0）+
default_water_2.png（frame 1）。两帧同色系、波纹位置略有偏移（frame 1 把横向波纹整体下移 1 像素 +
高光位移），mesher 据慢速 phase（0/1）在两帧间切换 → 静水呈现轻微「呼吸 / 荡漾」感（spec「静止水
2 帧慢播，勿快」）。慢播节拍由 Main.qml Timer（~800ms）驱动重建水段，避免快闪刺眼。

输出（覆盖写入 textures/）：
  default_water.png        （frame 0）
  default_water_2.png      （frame 1，t223 动画第二帧）

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


def draw_water(canvas, frame=0):
    """蓝色静水：中蓝底 + 横向亮暗波纹 + 少量高光反光点。

    frame=0 / 1 两帧（t223 动画）：frame 1 把横向波纹整体下移 1 像素 + 高光位移 1 格，
    使两帧 flipbook 切换时水面呈轻微「荡漾 / 呼吸」感（同色系、仅纹理位置略变 → 静而不死）。
    下移用 y+1（超出 16 行的波纹自然丢失，由 draw_water 在调用前对 frame 1 用偏移后的 y 重画）。
    """
    light = np.array([110.0, 170.0, 215.0])  # 亮蓝（波纹反光）
    dark = np.array([40.0, 84.0, 132.0])     # 暗蓝（暗涌阴影）
    hi = np.array([195.0, 220.0, 240.0])     # 近白蓝（细碎阳光反光）

    # frame 1：波纹 y 整体 +1（荡漾位移）；高光也位移一格。frame 0 不偏移。
    dy = 1 if frame == 1 else 0

    # 横向波纹（亮 / 暗细带，刻意不等距、长度不一 → 拟自然水面，非网格化）。
    hline(canvas, 1, 4, 2 + dy, light)
    hline(canvas, 9, 12, 2 + dy, dark)
    hline(canvas, 3, 6, 5 + dy, dark)
    hline(canvas, 10, 14, 5 + dy, light)
    hline(canvas, 0, 3, 8 + dy, light)
    hline(canvas, 7, 11, 8 + dy, dark)
    hline(canvas, 12, 15, 8 + dy, light)
    hline(canvas, 2, 5, 11 + dy, dark)
    hline(canvas, 9, 13, 11 + dy, light)
    hline(canvas, 4, 8, 14 + dy, light)
    hline(canvas, 11, 15, 14 + dy, dark)

    # 细碎高光（单像素阳光反光，散布于亮波纹附近；frame 1 位移一格）。
    hi_pts = [(2, 2), (11, 5), (1, 8), (12, 11), (6, 14)]
    for (x, y) in hi_pts:
        px(canvas, x, y + dy, hi)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # frame 0（default_water）+ frame 1（default_water_2，t223 动画第二帧）。
    c0 = blank()
    draw_water(c0, frame=0)
    save(c0, "default_water")
    c1 = blank()
    draw_water(c1, frame=1)
    save(c1, "default_water_2")


if __name__ == "__main__":
    main()
