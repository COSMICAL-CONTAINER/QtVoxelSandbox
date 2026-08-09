#!/usr/bin/env python3
"""生成书架（Bookshelf）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 书架（bookshelf），但贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

default_bookshelf.png（各面同贴图，简化：MC 书架顶/底是木板、侧是书；本工程六面同图便于复用既有
立方面路径 + 减少瓦片）：木板边框（橡木木板浅棕纹）+ 中央书脊彩色书列（红 / 蓝 / 绿 / 棕四本书脊
纵向排列，书脊高度略有错落表「书随意摆放」，每本配一行书页暗化带表「书顶见页」）。

色块位置固定（无随机源）→ 同输入同输出（确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）。

输出（覆盖写入 textures/）：
  default_bookshelf.png

依赖：无（纯合成像素图，不读既有贴图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def base():
    """橡木木板浅棕底（同 planks 配色族，作书架边框 / 底色）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    # 橡木木板浅棕底（同 default_wood.png 色调）
    canvas[..., 0:3] = np.array([160.0, 124.0, 76.0])
    canvas[..., 3] = 255.0
    # 木纹横纹（深棕带，表「木板纹理」）
    for y in [3, 7, 11]:
        for x in range(TS):
            canvas[y, x, 0:3] = np.array([124.0, 92.0, 52.0])
    # 木板缝（顶 / 底各一道暗缝，表「层叠木板」）
    for x in range(TS):
        canvas[0, x, 0:3] = np.array([96.0, 70.0, 38.0])
        canvas[TS - 1, x, 0:3] = np.array([96.0, 70.0, 38.0])
    return canvas


def draw_books(canvas):
    """中央书脊彩色书列：4 本不同色书脊纵向排列在 col 3..12，每本占 2-3 列宽、高度略错落。"""
    # 书脊色（红 / 蓝 / 绿 / 棕，每本一色；机制等价 MC 书架多色书脊）
    red    = np.array([168.0, 52.0, 48.0])
    blue   = np.array([52.0, 84.0, 168.0])
    green  = np.array([72.0, 138.0, 64.0])
    brown  = np.array([124.0, 80.0, 44.0])
    page   = np.array([220.0, 208.0, 172.0])   # 米黄书页（书顶见页）
    spine_dark = np.array([40.0, 28.0, 20.0])  # 书脊暗化（书与书之间隔）

    books = [
        # (起始 col, 宽度, 颜色, 顶 y, 底 y)
        (3,  2, red,   2, TS - 3),
        (6,  2, blue,  3, TS - 2),
        (9,  2, green, 2, TS - 3),
        (12, 1, brown, 3, TS - 2),
    ]
    for (cx, w, color, y0, y1) in books:
        for x in range(cx, cx + w):
            if x >= TS - 1:  # 留最右一列作木板边框
                continue
            for y in range(y0, y1):
                canvas[y, x, 0:3] = color
            # 书顶见页（米黄一条，表「书顶页边」）
            canvas[y0, x, 0:3] = page
            canvas[y0 + 1, x, 0:3] = page
            # 书脊暗化（底缘一行暗，表「书与下层隔板」）
            canvas[y1 - 1, x, 0:3] = spine_dark

    # 书列左右各留 1-2 列木板边框（已在 base 设木板底）；col 2 / col 14 作木板
    for y in range(2, TS - 2):
        canvas[y, 2, 0:3] = np.array([160.0, 124.0, 76.0])   # 左边框（木板）
        canvas[y, TS - 2, 0:3] = np.array([160.0, 124.0, 76.0])  # 右边框
        canvas[y, TS - 1, 0:3] = np.array([160.0, 124.0, 76.0])  # 最右边框
    return canvas


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_books(base()), "default_bookshelf")


if __name__ == "__main__":
    main()
