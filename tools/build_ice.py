#!/usr/bin/env python3
"""生成冰族（Ice / PackIce / BlueIce）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t395 雪/冰/云杉三件套：冰是雪原群系水面冻结产物（worldgen freezeSurfaceWater / t468 tickIceFreeze）。
t468 冰的物理：Ice 的更滑变种 PackIce（浮冰）/ BlueIce（蓝冰），滑动速度递增 Ice < PackIce < BlueIce。
机制等价 MC 1.0 ice / packed ice / blue ice —— 寒冷群系水面结冰 + 冰上低摩擦滑动。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。

视觉意图（三冰种由色调 + 纹路区分，半透由 iceOnly 段材质 opacity 统一实现）：
  Ice     （tile 58）：浅蓝底 + 几道亮反光裂纹 + 细密亮暗噪点（表冰晶 / 冻结纹路）。
  PackIce（tile 106）：淡蓝白压实冰 + 密实细裂纹 + 反光高光（区别于 Ice 的浅蓝透与 BlueIce 的
                       深蓝纵纹；带蓝调反光表冰晶重结晶，非纯白绒感以免误读为白色羊毛）。
  BlueIce（tile 107）：淡蓝底 + 纵向纹路（最滑，蓝冰特征）+ 细噪点。

输出（覆盖写入 textures/）：
  default_ice.png        （tile 58，冰各面贴图）
  default_pack_ice.png   （tile 106，浮冰各面贴图）
  default_blue_ice.png   （tile 107，蓝冰各面贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py / build_cactus.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(3951)


def base(rgb):
    """纯色实心底（alpha=255）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = rgb[0]
    canvas[..., 1] = rgb[1]
    canvas[..., 2] = rgb[2]
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def line(canvas, x0, y0, x1, y1, rgb):
    """Bresenham 画线（裂纹折线段）。"""
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


def draw_ice():
    """浅蓝底 + 细密噪点 + 几道亮反光裂纹（表冰面冻结纹路 / 反光）。"""
    c = base([156.0, 186.0, 220.0])  # 浅蓝，冰冷感
    lite = np.array([186.0, 212.0, 240.0])
    dark = np.array([132.0, 162.0, 200.0])
    mask = _RNG.random((TS, TS)) < 0.20
    c[mask, 0:3] = lite
    mask2 = _RNG.random((TS, TS)) < 0.18
    c[mask2, 0:3] = dark
    crack = np.array([216.0, 234.0, 250.0])
    line(c, 0, 3, 5, 6, crack)
    line(c, 5, 6, 9, 4, crack)
    line(c, 11, 0, 10, 5, crack)
    line(c, 10, 5, 14, 8, crack)
    line(c, 1, 11, 6, 10, crack)
    line(c, 6, 10, 8, 14, crack)
    line(c, 12, 12, 15, 10, crack)
    return c


def draw_pack_ice():
    """淡蓝白压实冰（机制等价 MC 1.0 packed ice）：淡蓝白基调 + 密实细裂纹 + 反光高光，区别于
    Ice 的浅蓝（更透）和 BlueIce 的深蓝（纵向纹路）。压实冰 = 冰晶受压重结晶，故更密实、更亮、
    带乳白蓝反光（非纯白绒感，避免误读为白色羊毛）。"""
    # 淡蓝白底（介于 Ice 浅蓝 156,186,220 与纯白之间；带明显蓝调显「冰」非「羊毛」）。
    c = base([200.0, 218.0, 232.0])
    lite = np.array([222.0, 236.0, 248.0])   # 反光高光（近白蓝，表压冰晶面反光）
    dark = np.array([168.0, 188.0, 212.0])   # 阴影蓝（裂纹边、压痕，蓝调显冰质感）
    # 像素级噪点：高光 + 阴影散布（压实冰的细密重结晶颗粒；比例适中，避免过白成羊毛）。
    mask = _RNG.random((TS, TS)) < 0.20
    c[mask, 0:3] = lite
    mask2 = _RNG.random((TS, TS)) < 0.24
    c[mask2, 0:3] = dark
    # 密实细裂纹（压实冰的标志性细密龟裂；用蓝白裂纹色显反光冰纹，非纯灰）。
    crack = np.array([184.0, 204.0, 226.0])
    line(c, 0, 2, 4, 5, crack)
    line(c, 4, 5, 8, 3, crack)
    line(c, 8, 3, 13, 6, crack)
    line(c, 2, 9, 6, 8, crack)
    line(c, 6, 8, 9, 12, crack)
    line(c, 11, 13, 15, 11, crack)
    line(c, 1, 14, 5, 13, crack)
    # 散布几粒亮高光点（冰晶面镜面反光，区别于哑光羊毛）。
    sparkle = np.array([240.0, 248.0, 255.0])
    c[4, 11, 0:3] = sparkle
    c[10, 2, 0:3] = sparkle
    c[13, 9, 0:3] = sparkle
    c[7, 14, 0:3] = sparkle
    return c


def draw_blue_ice():
    """淡蓝底 + 纵向纹路（蓝冰特征，最滑）+ 细噪点。"""
    c = base([120.0, 168.0, 214.0])  # 淡蓝（比 Ice 更蓝），最滑冰种
    lite = np.array([154.0, 196.0, 232.0])
    dark = np.array([92.0, 138.0, 186.0])
    mask = _RNG.random((TS, TS)) < 0.18
    c[mask, 0:3] = lite
    mask2 = _RNG.random((TS, TS)) < 0.16
    c[mask2, 0:3] = dark
    # 纵向纹路（蓝冰的标志性竖向螺旋/流纹）：几条竖向亮带 + 暗带。
    vein_lite = np.array([176.0, 210.0, 238.0])
    vein_dark = np.array([84.0, 128.0, 176.0])
    for y in range(TS):
        if y % 4 == 0:
            px(c, 3, y, vein_lite); px(c, 4, y, vein_lite)
        if y % 5 == 1:
            px(c, 9, y, vein_dark); px(c, 10, y, vein_dark)
        if y % 3 == 2:
            px(c, 12, y, vein_lite)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_ice(), "default_ice")
    save(draw_pack_ice(), "default_pack_ice")
    save(draw_blue_ice(), "default_blue_ice")


if __name__ == "__main__":
    main()
