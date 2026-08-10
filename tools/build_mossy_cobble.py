#!/usr/bin/env python3
"""生成苔石方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t486 丛林神殿主体方块（机制等价 MC 1.0 mossy cobblestone——长满苔藓的圆石变体，潮湿阴暗环境的风化石材）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：圆石灰底 + 散布暗绿苔藓斑簇，读作「长苔的圆石」。

视觉意图：读作「布满苔藓的圆石」——
  - 主体：圆石灰底（多边形石块拼贴 + 深灰砂浆缝，同 cobble 美感），细密噪点表粗糙。
  - 苔藓：散布暗绿斑簇（不规则团块），覆盖约 35-45% 表面 → 「长满苔藓」读感（区别于纯圆石）。
  - 边角 / 缝隙更密苔藓（潮湿积聚处）。

输出（覆盖写入 textures/）：
  default_mossy_cobble.png   （tile 124，苔石各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_tnt.py / build_cut_sandstone.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(486)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def cobble_base():
    """圆石灰底：中灰底 + 多边形石块拼贴感（亮 / 暗双色噪声块）+ 深灰砂浆缝。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 110.0  # R
    canvas[..., 1] = 110.0  # G
    canvas[..., 2] = 110.0  # B（中灰圆石底）
    canvas[..., 3] = 255.0
    # 石块亮 / 暗双色噪声（表多边形石块拼贴的明暗差异）。
    lite = np.array([148.0, 148.0, 148.0])
    dark = np.array([82.0, 82.0, 82.0])
    m1 = _RNG.random((TS, TS)) < 0.22
    canvas[m1, 0:3] = lite
    m2 = _RNG.random((TS, TS)) < 0.22
    canvas[m2, 0:3] = dark
    # 深灰砂浆缝（几条短段，表石块间的砂浆接缝）。
    mortar = np.array([58.0, 58.0, 58.0])
    for _ in range(6):
        sx = int(_RNG.randint(0, TS - 4))
        sy = int(_RNG.randint(0, TS))
        ln = int(_RNG.randint(3, 7))
        for i in range(ln):
            px(canvas, sx + i, sy, mortar)
    return canvas


def moss_splotch(canvas, cx_, cy_, rad, rgb):
    """在 (cx_,cy_) 画一团不规则苔藓斑簇（半径 rad 的不规则填充）。"""
    for dy in range(-rad, rad + 1):
        for dx in range(-rad, rad + 1):
            if dx * dx + dy * dy <= rad * rad + _RNG.randint(-2, 3):
                # 苔藓斑簇内部颜色微抖（深 / 浅绿交替 → 苔藓质感）。
                vary = np.array([rgb[0] + _RNG.randint(-12, 13),
                                 rgb[1] + _RNG.randint(-10, 16),
                                 rgb[2] + _RNG.randint(-10, 11)])
                px(canvas, cx_ + dx, cy_ + dy, vary)


def draw_face():
    """苔石面：圆石灰底 + 散布暗绿苔藓斑簇（覆盖约 40%）+ 边角加密苔藓。"""
    c = cobble_base()
    # 暗绿苔藓主色（潮湿苔藓）。
    moss = np.array([62.0, 94.0, 48.0])
    # 散布 8-10 团苔藓斑簇（半径 1-2，随机位置）→ 覆盖约 35-45% 表面。
    for _ in range(9):
        mx = int(_RNG.randint(0, TS))
        my = int(_RNG.randint(0, TS))
        rad = int(_RNG.randint(1, 3))
        moss_splotch(c, mx, my, rad, moss)
    # 边角加密苔藓（四角各一团 → 潮湿积聚处苔藓更密）。
    for cx_, cy_ in [(1, 1), (TS - 2, 1), (1, TS - 2), (TS - 2, TS - 2)]:
        moss_splotch(c, cx_, cy_, 2, moss)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # 苔石各面同贴图（圆石灰底 + 散布苔藓斑簇；mesher 整立方路径 6 面统一用 tile 124）。
    save(draw_face(), "default_mossy_cobble")


if __name__ == "__main__":
    main()
