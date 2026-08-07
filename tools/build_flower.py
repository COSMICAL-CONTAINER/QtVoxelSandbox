#!/usr/bin/env python3
"""生成花（Flower）4 色变体 cross 贴图（16x16 像素，原创自绘，§9 override (a)）。

t397 多群系装饰植物：花是各群系草地的彩色装饰性 cross 广告牌方块（与草丛 / 树苗 / 蘑菇同走
cross 几何段），机制等价 MC 1.0 花（poppy / dandelion 等）。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。spec「thin like tall grass」即满格 cross。

视觉意图：读作「一棵小花」—— 透明底（alpha=0）+ 一根绿色茎 + 顶部彩色花头（4 色：红 / 黄 / 蓝 / 白）。
mesher 把本瓦片贴到 cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5 丢弃透明底 -> 仅花像素显
（机制等价 MC cutout 花）。透明底是关键：若无 alpha，cross 会显成两片实心板（非花）。

4 色变体共享茎（绿色）+ 花头轮廓，仅花头配色不同。机制等价 MC 各色花共用 cross 模型、贴图各异。

alpha bleed（同 build_sapling.py / build_mushroom.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）-> 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_flower_red.png     （tile 63，红花）
  default_flower_yellow.png  （tile 64，黄花）
  default_flower_blue.png    （tile 65，蓝花）
  default_flower_white.png   （tile 66，白花）

依赖：仅 PIL，无外部贴图。与 build_sapling.py / build_mushroom.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    同 build_sapling.py 的 bleed_alpha（t245 alpha 边缘修复）：见文件头注释。
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


# 茎配色（绿色，同草丛 / 树苗叶色族）：亮面 #5e9b2a / 暗面 #3a6a14。
STEM_LIGHT = (0x5e, 0x9b, 0x2a, 255)
STEM_DARK = (0x3a, 0x6a, 0x14, 255)
# 花头配色（4 色变体）：每色给 (亮面, 暗面, 花心点缀)。机制等价 MC 各色花各有花头配色。
#   红（poppy 罂粟）: 砖红 + 暗红 + 黄花心
#   黄（dandelion 蒲公英）: 亮黄 + 暗黄 + 橙花心
#   蓝（cornflower 矢车菊）: 钴蓝 + 深蓝 + 白花心
#   白（oxeye daisy 雏菊）: 米白 + 浅灰 + 黄花心
FLOWER_COLORS = {
    "red":    ((0xd2, 0x36, 0x2a, 255), (0x8a, 0x1c, 0x12, 255), (0xf2, 0xd2, 0x30, 255)),
    "yellow": ((0xe8, 0xc4, 0x1e, 255), (0xa8, 0x82, 0x10, 255), (0xd2, 0x6a, 0x10, 255)),
    "blue":   ((0x35, 0x6a, 0xc4, 255), (0x1a, 0x3a, 0x82, 255), (0xea, 0xea, 0xf0, 255)),
    "white":  ((0xee, 0xe8, 0xd8, 255), (0xb8, 0xb0, 0xa0, 255), (0xe8, 0xc4, 0x1e, 255)),
}


def build_flower(color_name):
    """生成一色花贴图。color_name = 'red' / 'yellow' / 'blue' / 'white'。"""
    petal_light, petal_dark, center = FLOWER_COLORS[color_name]
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()

    def put(x, y, color):
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = color

    # 茎：从画布底（y=15）向上到 y=6（占下半到中部），列 x=7/8（中央两列，圆柱明暗）。
    #   茎略偏左以让花头稍偏右、避免完全对称（自然感）。
    for y in range(6, 16):
        put(7, y, STEM_DARK)
        put(8, y, STEM_LIGHT)
    # 茎基部两片小叶（机制等价 MC 花茎下部小叶）：左右各一片绿色小叶。
    put(5, 12, STEM_DARK); put(6, 12, STEM_LIGHT)  # 左叶
    put(9, 11, STEM_LIGHT); put(10, 11, STEM_DARK)  # 右叶

    # 花头：以 (7.5, 4) 为中心的 5 瓣花形（机制等价 MC 花 5 瓣轮廓）。手调花瓣坐标呈「中心 + 4 周瓣」放射状。
    #   4 周瓣用亮 / 暗双色给立体感（受光面亮、背光面暗）；中心一点花心色点缀。
    #   花瓣坐标手调（x, y, light?）。
    petals = [
        # 上瓣
        (7, 2, True), (8, 2, False),
        # 左瓣
        (4, 4, True), (5, 4, True), (6, 5, False),
        # 右瓣
        (9, 4, False), (10, 4, False), (9, 5, True),
        # 下瓣（连接茎）
        (7, 6, False), (8, 6, True),
        # 中心环（亮 / 暗交错围中心）
        (7, 3, True), (8, 3, False), (6, 4, False), (9, 4, True),
        (7, 4, False), (8, 4, True), (7, 5, True), (8, 5, False),
    ]
    for (x, y, light) in petals:
        put(x, y, petal_light if light else petal_dark)

    # 花心（中心点彩色点缀）：机制等价 MC 花中心花蕊。位于花头中心 (7.5, 4)。
    put(7, 4, center)
    put(8, 4, center)

    # alpha bleed：把茎 / 花头颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_sapling.py）。
    img = bleed_alpha(img)
    return img


def main():
    for color_name in ("red", "yellow", "blue", "white"):
        img = build_flower(color_name)
        out = os.path.join(SRC, "default_flower_%s.png" % color_name)
        img.save(out)
        print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
