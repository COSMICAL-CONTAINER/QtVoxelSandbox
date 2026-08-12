#!/usr/bin/env python3
"""生成白蘑菇 / 棕蘑菇（BrownMushroom）cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

t507 沼泽群系内容：白蘑菇 / 棕蘑菇是沼泽 / 阴暗草地的小蘑菇装饰性 cross 广告牌方块（与红蘑菇
Mushroom=48 / 草丛 / 树苗 / 枯灌木同走 cross 几何段），机制等价 MC 1.0 brown mushroom。名称 / 贴图
纯原创自绘（§9 区隔，零 MC 资产 / 专名）。仅配色区别于红蘑菇 —— 棕色菌盖 + 米色菌柄（红蘑菇是红色
菌盖 + 白斑）。是蘑菇汤配方原料（recipe.cpp：碗 + 红蘑菇 + 白蘑菇 → 1 蘑菇汤）。

视觉意图：读作「一棵小棕蘑菇」—— 透明底（alpha=0）+ 一根米色菌柄 + 顶部棕色菌盖（带浅色斑点，
拟经典棕伞蘑菇）。mesher 把本瓦片贴到 cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5
丢弃透明底 → 仅蘑菇像素显（机制等价 MC cutout 蘑菇）。透明底是关键：若无 alpha，cross 会显成两片
实心板（非蘑菇）。

alpha bleed（同 build_mushroom.py / build_dead_bush.py / build_sapling.py）：把透明像素的 RGB 用最近
不透明邻居颜色填上（alpha 保持 0）→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_brown_mushroom.png   （tile 135，白蘑菇 / 棕蘑菇 cross 贴图）

依赖：仅 PIL，无外部贴图。与 build_mushroom.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    同 build_mushroom.py 的 bleed_alpha（t245 alpha 边缘修复）：见文件头注释。
    """
    img = img.convert("RGBA")
    W, H = img.size
    px = img.load()
    for _ in range(max(W, H) * 2):
        changed = False
        snap = [px[x, y] for y in range(H) for x in range(W)]
        for y in range(H):
            for x in range(W):
                r0, g0, b0, a0 = snap[y * W + x]
                if a0 != 0 or (r0, g0, b0) != (0, 0, 0):
                    continue
                ar = ag = ab = 0
                n = 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dx == 0 and dy == 0:
                            continue
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < W and 0 <= ny < H:
                            r, g, b, a = snap[ny * W + nx]
                            if a != 0 or (r, g, b) != (0, 0, 0):
                                ar += r; ag += g; ab += b; n += 1
                if n > 0:
                    px[x, y] = (ar // n, ag // n, ab // n, 0)
                    changed = True
        if not changed:
            break
    return img


def main():
    # 透明底（alpha=0）。
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()

    def put(x, y, color):
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = color

    # 配色：菌柄米白（亮 / 暗，与红蘑菇同）、菌盖棕（亮 / 暗，区别于红蘑菇的红色菌盖）、浅黄斑（菌盖点缀）。
    stem_light = (0xe8, 0xdd, 0xc0, 255)  # 米白亮面（同红蘑菇菌柄）
    stem_dark = (0xb8, 0xad, 0x90, 255)   # 米白暗面（同红蘑菇菌柄）
    cap_light = (0xa0, 0x6a, 0x35, 255)   # 棕黄亮面（受光圆顶，棕蘑菇特征暖棕）
    cap_dark = (0x6a, 0x42, 0x1c, 255)    # 深棕暗面 / 菌盖底缘（背光）
    spot = (0xe8, 0xc8, 0x8a, 255)        # 浅黄褐斑（菌盖点缀，区别于红蘑菇白斑）

    # 菌柄：从画布底（y=15）向上到 y=7（占下半部分），列 x=6/7（中央偏左两列，圆柱明暗）。同红蘑菇菌柄。
    for y in range(7, 16):
        put(6, y, stem_dark)
        put(7, y, stem_light)
    # 柄底略宽（蘑菇柄基部膨大）。
    put(5, 14, stem_dark); put(5, 15, stem_dark)
    put(8, 14, stem_light); put(8, 15, stem_light)

    # 菌盖：伞状半球，覆盖柄顶 y=3..7，水平跨度 x=3..10（宽于柄）。亮 / 暗双色给圆顶立体感（同红蘑菇伞形）。
    #   坐标手调呈伞状轮廓（与红蘑菇 cap_rows 同形，仅配色区别）。
    cap_rows = [
        # (y, x_start, x_end) —— 菌盖每行的水平覆盖（含两端）
        (3, 6, 7),    # 顶尖
        (4, 5, 8),    # 上窄
        (5, 4, 9),    # 中宽
        (6, 3, 10),   # 最宽（盖缘）
        (7, 4, 9),    # 盖底（略收，伞缘）
    ]
    for (y, xs, xe) in cap_rows:
        for x in range(xs, xe + 1):
            # 中央两列（柄位置之上）用暗色（菌盖顶部背光），两侧亮色（受光圆顶）。
            is_top_shade = (x == 6 or x == 7) and y <= 5
            put(x, y, cap_dark if is_top_shade else cap_light)
        # 盖底缘（y==6/7 的两端）暗化 → 伞缘阴影。
        if y >= 6:
            put(xs, y, cap_dark)
            put(xe, y, cap_dark)

    # 浅黄褐斑（棕蘑菇菌盖经典斑点）：在盖面散布几个浅黄小点（避开柄列 / 盖缘暗带）。
    spots = [(5, 4), (8, 4), (4, 5), (9, 5), (7, 6), (5, 6), (8, 6)]
    for (x, y) in spots:
        put(x, y, spot)

    # alpha bleed：把菌柄 / 菌盖颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_mushroom.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_brown_mushroom.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
