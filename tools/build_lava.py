#!/usr/bin/env python3
"""生成岩浆（Lava）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 岩浆（缓慢流动的熔融岩浆；worldgen 在 Y<30 封闭洞穴生成岩浆湖；玩家穿过、不可
破坏），但贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「炽热岩浆」——深红橙底 + 更亮黄橙的鼓泡 / 裂纹 + 少量近白炽热点，让岩浆在黑暗
洞穴里一眼可辨（与石头 / 沙色相区分，自发光感由材质高 baseColor 暖色 + NoLighting 呈现）。
**纹理本身不透明（alpha=255）**：岩浆段材质 opacity≈0.95（近不透，区别于水 0.7 半透——岩浆浓稠），
故此处画实心暖色即可。

图案（固定位置色块 + 伪随机感散布，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：深红橙（#a8321a 量级）实心；
  - 鼓泡：数个亮黄橙 / 暗红圆斑（拟岩浆表面冷却皮破裂露出的熔融核心），刻意不等距、大小不一；
  - 炽热点：少量近白黄点（拟最热处的白炽反光）。

输出（覆盖写入 textures/）：
  default_lava.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank():
    """深红橙实心底（alpha=255：岩浆近不透明，透明度由材质 opacity 控制，纹理保持不透明）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 168.0   # R
    canvas[..., 1] = 50.0    # G
    canvas[..., 2] = 26.0    # B（深红橙，与石头/沙区分；自发光暖色由材质 baseColor 加强）
    canvas[..., 3] = 255.0   # A（实心）
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def disc(canvas, cx, cy, r, rgb):
    """整数圆盘（半径 r 内的所有像素填 rgb；越界忽略）。拟岩浆鼓泡的圆形核心。"""
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px(canvas, x, y, rgb)


def draw_lava(canvas):
    """炽热岩浆：深红橙底 + 亮黄橙 / 暗红圆斑（冷却皮破裂露熔融核心）+ 近白炽热点。"""
    hot = np.array([255.0, 196.0, 60.0])   # 亮黄橙（熔融核心，最显眼）
    crust = np.array([120.0, 28.0, 16.0])  # 暗红（冷却凝固皮，比基底更深）
    white = np.array([255.0, 240.0, 200.0])  # 近白黄（最热处白炽反光）

    # 鼓泡圆斑（大小不一、刻意不等距 → 拟岩浆表面自然鼓泡，非网格化）。
    disc(canvas, 3, 3, 2, hot)
    disc(canvas, 12, 4, 1, hot)
    disc(canvas, 8, 7, 2, hot)
    disc(canvas, 2, 11, 1, hot)
    disc(canvas, 13, 12, 2, hot)
    disc(canvas, 7, 13, 1, hot)
    # 冷却皮暗斑（散布于亮鼓泡之间，拟岩浆表面部分凝固的深色结皮）。
    disc(canvas, 6, 2, 1, crust)
    disc(canvas, 11, 9, 1, crust)
    disc(canvas, 4, 8, 1, crust)
    disc(canvas, 14, 7, 1, crust)
    disc(canvas, 9, 11, 1, crust)
    # 白炽热点（单像素最亮，散布于大鼓泡中心，拟最热处的白炽反光）。
    hi_pts = [(3, 3), (8, 7), (13, 12), (12, 4)]
    for (x, y) in hi_pts:
        px(canvas, x, y, white)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    c = blank()
    draw_lava(c)
    save(c, "default_lava")


if __name__ == "__main__":
    main()
