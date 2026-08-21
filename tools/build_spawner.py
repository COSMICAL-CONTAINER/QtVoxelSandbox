#!/usr/bin/env python3
"""生成刷怪笼（Spawner）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 刷怪笼（地下地牢中央放置的方块，玩家在范围内时周期性刷一只敌对 mob，
破坏后停止刷怪），但贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「铁笼里关着东西」——铁灰色栅栏（横向 + 纵向条带）围成笼格，格间
**透明孔（alpha=0）**；笼内由 QML spawnerHost delegate 渲染缓慢旋转的迷你蠹虫模型
（t760）经孔透视，机制等价 MC 刷怪笼内旋转的小怪剪影（本工程用真 3D 迷你模型替代
剪影，观感更立体）。6 面同贴图。

图案（固定位置色块，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：暗蓝灰 RGB + alpha=0（t760 笼面由不透明整立方改 cutout 栅格：孔透明供笼内
    迷你蠹虫透视；孔下保留暗蓝灰 RGB 而非 (0,0,0)，供**非 Mask 材质消费者**——如手持
    BlockCube 的不透明渲染路径、图集离线投影——在忽略 alpha 时优雅降级显示暗底铁笼
    而非黑块）；
  - 栅栏：铁灰色横向 + 纵向条带（alpha=255 实心，拟铁笼骨架；含上沿/左沿高光）；
  - 中心光斑：t760 **删除**（旧版青绿光斑落在每面正中，正对视线挡住笼内视线；
    「内有活物」信号改由旋转迷你蠹虫 delegate 本体承担，不再需要贴图代理）。

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
    """暗蓝灰底 + alpha=0（t760 cutout 契约：栅格孔透明，RGB 留暗底供降级）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 30.0   # R
    canvas[..., 1] = 32.0   # G
    canvas[..., 2] = 40.0   # B（暗蓝灰，与石头 / 基岩区分；拟笼内阴影）
    canvas[..., 3] = 0.0    # A（t760：整面透明起手，仅栅栏/边框像素由 draw_spawner 写实心）
    return canvas


def px(canvas, x, y, rgb, a=255.0):
    """单像素写入（RGB + 指定 alpha；越界忽略；t760 加 a 参数支撑 cutout）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb
        canvas[y, x, 3] = a


def rect(canvas, x0, y0, x1, y1, rgb, a=255.0):
    """实心矩形（含端点）。"""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb, a)


def draw_spawner(canvas):
    """铁笼：铁灰栅栏（横纵条带，alpha=255）+ 格间透明孔（基底 alpha=0 透出）。"""
    iron_dark = np.array([58.0, 60.0, 70.0])    # 铁条暗部（深铁灰）
    iron_lite = np.array([110.0, 112.0, 122.0]) # 铁条亮部（高光边）

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

    # t760：中心青绿光斑删除——正对每面视线会挡住笼内迷你蠹虫；
    # 「内有活物」信号由 spawnerHost 的旋转迷你蠹虫 delegate 本体承担。


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
