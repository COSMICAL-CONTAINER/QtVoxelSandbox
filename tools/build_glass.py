#!/usr/bin/env python3
"""生成玻璃（Glass）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t405 沙子冶炼产物：玻璃是熔炉烧沙子（SmeltingRegistry 沙子→玻璃物品 0x204）后玩家可放置的**透明整立方**
方块，机制等价 MC 1.0 玻璃（glass）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「半透的浅青白玻璃面」—— 整立方方块，六面同贴图（**纹理本身不透明** alpha=255；观感的
「透明」由 ChunkGeometry 的 glassOnly 段材质 opacity≈0.45 实现，同 water 模式：纹理不透 + 材质半透）。
纹理提供玻璃的「质感细节」而非透明度：
  近白青底 + 暗边框（表玻璃块边棱）+ 一道对角高光斜线（表玻璃反光）+ 细密亮暗噪点（表玻璃微观不平整）。
t838② 边框增实（用户「玻璃方块边缘太透明」）：边框自 1px 浅灰环（delta 仅 ~46/255，经材质 opacity 0.45
  洗后与底面几乎不可辨）加宽加深为 **2px 双色环**——外圈 1px 深棱（delta ~120/255）+ 内圈 1px 过渡棱
  （delta ~60/255）。材质半透是均匀乘子（整面 ×0.45）→ 边框对比度按比例保留：外棱在半透后仍明显深于
  面心，玻璃块的棱线读感「实」起来。pack 态瓦片（包内 glass.png）不适用 solidify（玻璃合法半透），
  本增实仅程序态自绘。

输出（覆盖写入 textures/）：
  default_glass.png   （tile 68，玻璃各面贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_ice.py / build_sandstone.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(4051)


def glass_base():
    """近白青实心底（alpha=255：纹理不透明整立方；透明由 glassOnly 段材质 opacity 实现）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 214.0  # R
    canvas[..., 1] = 226.0  # G
    canvas[..., 2] = 234.0  # B（近白青，玻璃冷净感）
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def line(canvas, x0, y0, x1, y1, rgb):
    """Bresenham 画线（高光斜线）。"""
    dx = abs(x1 - x0)
    dy = abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx - dy
    x, y = x0, y0
    while True:
        px(canvas, x, y, rgb)
        if x == x1 and y == y1:
            break
        e2 = 2 * err
        if e2 > -dy:
            err -= dy
            x += sx
        if e2 < dx:
            err += dx
            y += sy


def draw_edge(c):
    """t838② 边框增实：2px 双色环——外圈深棱 + 内圈过渡棱（各画一圈 1px 环；两次调用同函数供噪点后回画）。"""
    edge_outer = np.array([96.0, 118.0, 142.0])   # 外圈深棱（近蓝灰；对比底面 delta ~120/255）
    edge_inner = np.array([152.0, 170.0, 188.0])  # 内圈过渡棱（delta ~60/255，深棱到面心的缓冲带）
    # 外圈（贴瓦片最外 1px）。
    for i in range(TS):
        px(c, i, 0, edge_outer)
        px(c, i, TS - 1, edge_outer)
        px(c, 0, i, edge_outer)
        px(c, TS - 1, i, edge_outer)
    # 内圈（第 2 圈 1px；四角由外圈覆盖）。
    for i in range(1, TS - 1):
        px(c, i, 1, edge_inner)
        px(c, i, TS - 2, edge_inner)
        px(c, 1, i, edge_inner)
        px(c, TS - 2, i, edge_inner)


def draw_glass():
    """近白青底 + 2px 双色暗边框（t838② 增实）+ 对角高光斜线 + 细密噪点（透明由材质 opacity 承担）。"""
    c = glass_base()
    draw_edge(c)
    # 一道亮对角高光斜线（左上→右下，表玻璃反光；与 build_ice 的折线裂纹区分——玻璃用单一干净反光更「玻璃」；
    #   起止收在 2px 边框内侧（2,3 → 11,12）不被棱线截断）。
    gloss = np.array([248.0, 252.0, 255.0])
    line(c, 2, 3, 11, 12, gloss)
    # 细密亮暗噪点（表玻璃微观不平整 / 微反光颗粒；低密度以免喧宾夺主；范围含边框行——下方回画覆盖）。
    lite = np.array([232.0, 240.0, 246.0])
    dark = np.array([196.0, 210.0, 222.0])
    mask = _RNG.random((TS, TS)) < 0.10
    c[mask, 0:3] = lite
    mask2 = _RNG.random((TS, TS)) < 0.08
    c[mask2, 0:3] = dark
    # 边框覆盖回画（噪点可能盖到边框像素，重画两圈保证棱线连贯）。
    draw_edge(c)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_glass(), "default_glass")


if __name__ == "__main__":
    main()
