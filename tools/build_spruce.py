#!/usr/bin/env python3
"""生成云杉（Spruce）木方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t395 雪原/针叶群系内容：云杉原木是雪原群系云杉树的主干（worldgen placeSpruceTreeAt 在 Snowy 群系
种云杉变种树），机制等价 MC 1.0 云杉（spruce log）—— 寒冷群系的针叶树变种。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。

t466 云杉木制品链：在原木基础上延伸出云杉木板（SprucePlanks，深色木纹区别橡木 OakPlanks）。云杉木制品
（SpruceSlab 台阶 / SpruceFence 栅栏 / SpruceDoor 门）与橡木木制品同形，仅 tile 换 spruce_planks
（一族木制品共享一贴图，同橡木 WoodSlab/Fence/Door 复用 planks(8) 模式）。

视觉意图：读作「深色木质」—— 与橡木（log/planks）同族但色更深（云杉木特征）：
  - spruce_log_top：截面同心年轮（深棕底 + 向心环纹），表「树干截面」。
  - spruce_log_side：垂直树皮纹（深棕底 + 竖向条带 + 细密皮纹），表「深色树皮」。
  - spruce_planks：横排木板（深棕底 + 水平板缝 + 竖向板缝 + 木纹颗粒），表「深色木板」。
    与 default_tree_top / default_tree（原木）/ default_wood（橡木木板）同结构、不同色
    （更深冷棕 → 区分云杉变种；橡木木板偏暖橙棕，云杉木板偏冷深棕）。

输出（覆盖写入 textures/）：
  default_spruce_log_top.png   （tile 59，云杉原木顶面 / 底面）
  default_spruce_log_side.png  （tile 60，云杉原木侧面）
  default_spruce_planks.png    （tile 102，云杉木板 / 台阶 / 栅栏 / 门 共享贴图）
  default_spruce_leaves.png    （tile 175，云杉树叶各面；t714 雪原云杉树冠针叶）

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py / build_cactus.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(3952)


def bark_base(r, g, b):
    """深棕实心底（alpha=255：云杉原木不透明整立方，走整立方面路径）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = r
    canvas[..., 1] = g
    canvas[..., 2] = b
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def draw_top():
    """顶面：深棕底 + 同心年轮（向心收缩方框）+ 中央暗髓心（树干截面）。"""
    c = bark_base(66, 52, 38)  # 深棕（云杉木截面，比原木更深）
    # 细密噪点（木纹颗粒）。
    dark = np.array([54.0, 42.0, 30.0])
    mask = _RNG.random((TS, TS)) < 0.22
    c[mask, 0:3] = dark
    # 同心年轮（向心收缩方框；机制等价树干截面年轮）。
    ring_lite = np.array([88.0, 70.0, 52.0])
    ring_dark = np.array([48.0, 36.0, 26.0])
    rect(c, 2, 2, TS - 3, TS - 3, ring_lite)
    rect(c, 2, 2, 2, TS - 3, ring_dark)
    rect(c, TS - 3, 2, TS - 3, TS - 3, ring_dark)
    rect(c, 2, 2, TS - 3, 2, ring_dark)
    rect(c, 2, TS - 3, TS - 3, TS - 3, ring_dark)
    rect(c, 5, 5, TS - 6, TS - 6, ring_lite)
    # 中央暗髓心。
    rect(c, 7, 7, 8, 8, ring_dark)
    return c


def draw_side():
    """侧面：深棕底 + 垂直树皮条带（深浅交替）+ 细密竖向皮纹（深色树皮）。"""
    c = bark_base(58, 44, 32)  # 深棕（云杉树皮，比原木更深冷）
    # 细密竖向噪点（拟皮纹）。
    dark = np.array([46.0, 34.0, 24.0])
    mask = _RNG.random((TS, TS)) < 0.20
    c[mask, 0:3] = dark
    # 垂直树皮条带（深浅交替，x = 2 / 5 / 8 / 11 / 14；非等距显自然）。
    ridge_lite = np.array([78.0, 60.0, 44.0])  # 受光凸棱（亮）
    ridge_dark = np.array([42.0, 30.0, 20.0])    # 凹槽（暗）
    for i, x in enumerate([2, 5, 8, 11, 14]):
        band = ridge_lite if (i % 2 == 0) else ridge_dark
        rect(c, x, 1, x, TS - 2, band)
    # 上下边缘暗化（表圆柱树干收端的阴影）。
    rect(c, 0, 0, TS - 1, 0, np.array([38.0, 28.0, 18.0]))
    rect(c, 0, TS - 1, TS - 1, TS - 1, np.array([38.0, 28.0, 18.0]))
    return c


def draw_planks():
    """云杉木板（深色木纹）：深棕底 + 水平板缝（3 排错缝）+ 竖向板缝 + 细密木纹颗粒。

    视觉对标 default_wood（橡木木板）结构但色更深冷棕（云杉木特征）：橡木偏暖橙棕，云杉偏冷深棕。
    4 像素宽一排板，三排错缝（顶排板缝在 x=7/8、中排无竖缝（连续长板）、底排 x=3/4），机制对标 MC
    木板「planks」纹理（水平板缝 + 错缝竖板）。各面同贴图（同橡木木板，一族木制品共享）。
    """
    c = bark_base(74, 56, 40)  # 深冷棕底（云杉木板，比橡木 default_wood 更深更冷）
    # 细密木纹颗粒（沿水平方向的木纤维噪点，比树皮细）。
    grain_dark = np.array([60.0, 44.0, 30.0])
    grain_lite = np.array([92.0, 72.0, 54.0])
    mask_d = _RNG.random((TS, TS)) < 0.18
    c[mask_d, 0:3] = grain_dark
    mask_l = _RNG.random((TS, TS)) < 0.10
    c[mask_l, 0:3] = grain_lite
    # 水平板缝（深线，y=0 / 5 / 10 / 15；4 像素一排板 + 1 像素缝）。
    seam = np.array([44.0, 32.0, 22.0])
    for y in [0, 5, 10, 15]:
        rect(c, 0, y, TS - 1, y, seam)
    # 竖向板缝（每排板内错缝，机制对标 MC 木板错缝；中排连续长板无竖缝）。
    rect(c, 7, 1, 8, 4, seam)    # 顶排板缝
    rect(c, 3, 6, 4, 9, seam)    # 底排板缝
    rect(c, 11, 11, 12, 14, seam)  # 最底排板缝
    # 板缝端钉点（深色小点，强化「木板」语义；非每条缝都加免过密）。
    nail = np.array([36.0, 26.0, 18.0])
    px(c, 7, 2, nail); px(c, 8, 2, nail)
    px(c, 3, 7, nail); px(c, 4, 7, nail)
    px(c, 11, 12, nail); px(c, 12, 12, nail)
    return c


def draw_leaves():
    """t714 云杉树叶：深蓝绿针叶底 + 噪点 + 针簇放射短线 + 部分透明孔（cutout 观感）。

    结构对标 default_leaves.png（oak 48×48：深绿底 + 噪点 + ~22% 透明簇孔 → mesher cutout 半镂空叶冠），
    但色调换「深冷蓝绿」（云杉针叶特征 —— MC spruce leaves 相对 oak 更暗更蓝），加斜向针簇短线（针叶
    读感，区别阔叶的团状）。透明孔 ~20%（略少于 oak 的 22% → 云杉冠观感更密实，贴合针叶树密冠）。
    """
    TS2 = 48  # 与 oak leaves 同分辨率（default_leaves.png 是 48×48）
    c = np.zeros((TS2, TS2, 4), dtype=np.float64)
    # 深蓝绿针叶底（区别 oak 的亮绿 #069040 量级；云杉 = 更暗更蓝的 #0e4f3c 量级）。
    base = np.array([14.0, 74.0, 56.0])
    c[..., 0] = base[0]
    c[..., 1] = base[1]
    c[..., 2] = base[2]
    c[..., 3] = 255.0
    # 细密噪点（叶簇明暗颗粒；两档：暗针影 + 亮针高光）。
    rng2 = np.random.RandomState(7141)
    dark = np.array([8.0, 52.0, 38.0])
    lite = np.array([26.0, 104.0, 76.0])
    m_d = rng2.random((TS2, TS2)) < 0.24
    c[m_d, 0:3] = dark
    m_l = rng2.random((TS2, TS2)) < 0.12
    c[m_l, 0:3] = lite
    # 斜向针簇短线（~45° 短笔触 → 针叶放射读感；区别 oak 团状纹理）。每条线 4-6 像素、沿 (+1,-1) 方向。
    for _ in range(130):
        x0 = int(rng2.randint(0, TS2 - 8))
        y0 = int(rng2.randint(4, TS2))
        ln = int(rng2.randint(4, 7))
        shade = lite if rng2.random() < 0.45 else dark
        for t in range(ln):
            x = x0 + t
            y = y0 - t
            if 0 <= x < TS2 and 0 <= y < TS2:
                c[y, x, 0:3] = shade
    # 透明孔（簇状 ~20%）：随机种子点 + 2×2 / 3×2 小簇 → cutout 半镂空叶冠（同 oak leaves 语义）。
    for _ in range(58):
        x0 = int(rng2.randint(0, TS2 - 3))
        y0 = int(rng2.randint(0, TS2 - 3))
        w = 2 + int(rng2.randint(0, 2))
        h = 2
        c[y0:y0 + h, x0:x0 + w, 3] = 0.0
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_top(), "default_spruce_log_top")
    save(draw_side(), "default_spruce_log_side")
    save(draw_planks(), "default_spruce_planks")  # t466 云杉木板（一族木制品共享 tile 102）
    save(draw_leaves(), "default_spruce_leaves")  # t714 云杉树叶（tile 175，雪原云杉树冠针叶）


if __name__ == "__main__":
    main()
