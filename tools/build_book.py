#!/usr/bin/env python3
"""生成附魔台顶部摊开书贴图（16×16 像素，原创自绘，§9 override (a)）。

t638 ⑧ 附魔台顶摊开书：附魔台方块顶上（y=0.75 矮盒之上）立一本**打开的两页书**（机制等价 MC 1.0
附魔台顶 floating book）。几何 = PartialBlockGeometry EnchantingTable case 的 4 段薄盒页（左 2 段 +
右 2 段拼 V 形摊开角），各面贴本贴图（enchant_book tile 162）。

视觉意图：一张摊开的书页——白纸底 + 灰色字线（横排短划，左密右疏显「两边页」）+ 中央书脊暗线
（左右页分界）+ 页缘暗化。无 pack 等价（MC 的书是独立实体模型非方块贴图；包内无 enchant_book.png）
→ 程序贴图恒用。

输出（覆盖写入 textures/）：
  default_enchant_book.png  （tile 162，摊开书页——附魔台顶书盒专用）

依赖：仅 PIL，无外部贴图。与 build_enchanting_table.py 同风格。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 配色。
PAGE_BASE = np.array([232.0, 226.0, 206.0])   # 米白纸底（老书页暖白）
PAGE_LINE = np.array([120.0, 116.0, 128.0])   # 灰紫字线（墨迹淡显）
SPINE     = np.array([96.0, 62.0, 34.0])      # 中央书脊（深棕皮革）
SPINE_HI  = np.array([132.0, 92.0, 52.0])     # 书脊亮棱
EDGE      = np.array([180.0, 172.0, 148.0])   # 页缘暗化（纸卷边）


def draw_book_page():
    """摊开书页：米白底 + 横排字线（左右页各排，行距 2px）+ 中央书脊暗线 + 上下页缘暗化。"""
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = PAGE_BASE
    c[..., 3] = 255.0
    # 横排字线：左页 x 2..6、右页 x 9..13（避开书脊 7..8）；y = 3/5/7/9/11 行（行距 2，短线长随机略变——
    #   确定性固定图案）。
    line_lens_l = [5, 4, 5, 4, 5]   # 左页各行的字线长（x 起点 2，长 4..5）
    line_lens_r = [5, 5, 4, 5, 4]
    ys = [3, 5, 7, 9, 11]
    for i, y in enumerate(ys):
        for dx in range(line_lens_l[i]):
            c[y, 2 + dx, 0:3] = PAGE_LINE
        for dx in range(line_lens_r[i]):
            c[y, 9 + dx, 0:3] = PAGE_LINE
    # 中央书脊（x 7..8 纵贯；深棕 + 亮棱 x=7）。
    for y in range(1, TS - 1):
        c[y, 7, 0:3] = SPINE_HI
        c[y, 8, 0:3] = SPINE
    # 上下页缘暗化（纸卷边；y=0 / y=15 行）。
    for x in range(0, TS):
        c[0, x, 0:3] = EDGE
        c[TS - 1, x, 0:3] = EDGE
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_book_page(), "default_enchant_book")


if __name__ == "__main__":
    main()
