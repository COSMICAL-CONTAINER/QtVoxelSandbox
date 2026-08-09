#!/usr/bin/env python3
"""生成雪原浆果灌木丛（SweetBerryBush）3 阶段 cross 贴图（16x16 像素，原创自绘，§9 override (a)）。

t467 雪原 Snowy 群系散布的可采摘灌木：cross 形广告牌方块（与 TallGrass / Sapling / 作物同走
PartialBlockGeometry 的 cross 几何段，两片对角相交双面 quad，alpha 透明底 cutout），机制等价 MC 1.0
sweet berry bush。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

3 生长阶段（state = 阶段 0..2；SweetBerryBushStageMax=2）：
  stage 0（无果嫩丛）：纯绿色枝叶丛，无浆果（玩家采摘后丛回此态重新长）。
  stage 1（小果）：绿色丛 + 几颗小青红浆果（未熟）。
  stage 2（成熟）：绿色丛 + 饱满红浆果簇（右键采摘得 2-3 浆果）。

mesher（PartialBlockGeometry::append 的 SweetBerryBush case）把本瓦片贴到 cross 的两片对角双面 quad；
chunk 地形材质 alphaCutoff:0.5 丢弃透明底 -> 仅丛像素显（机制等价 MC cutout）。透明底是关键：若无 alpha，
cross 会显成两片实心板（非灌木）。

alpha bleed（同 build_flower.py / build_sapling.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）-> 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_sweet_berry_bush_0.png  （tile 103，无果嫩丛）
  default_sweet_berry_bush_1.png  （tile 104，小果）
  default_sweet_berry_bush_2.png  （tile 105，成熟红浆果簇）

依赖：仅 PIL，无外部贴图。与 build_flower.py / build_sapling.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    同 build_flower.py 的 bleed_alpha（alpha 边缘修复）：见文件头注释。
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


# 枝叶配色（绿色冷调，呼应雪原寒冷群系）：亮面 #4a7a3a / 暗面 #2a5a1a / 中调 #3a6a28。
LEAF_LIGHT = (0x4a, 0x7a, 0x3a, 255)
LEAF_MID = (0x3a, 0x6a, 0x28, 255)
LEAF_DARK = (0x2a, 0x5a, 0x1a, 255)
# 浆果配色：stage 1 小果（青红未熟）berry_unripe #8a4a3a / berry_unripeLight #a85a48；
#   stage 2 成熟（饱满红）berry_ripe #b81c2a / berry_ripeLight #e84030 / berry_ripeDark #74101a。
BERRY_UNRIPE = (0x8a, 0x4a, 0x3a, 255)
BERRY_UNRIPE_LIGHT = (0xa8, 0x5a, 0x48, 255)
BERRY_RIPE = (0xb8, 0x1c, 0x2a, 255)
BERRY_RIPE_LIGHT = (0xe8, 0x40, 0x30, 255)
BERRY_RIPE_DARK = (0x74, 0x10, 0x1a, 255)


def put(px, x, y, color):
    if 0 <= x < TS and 0 <= y < TS:
        px[x, y] = color


def draw_bush_canopy(px):
    """画灌木枝叶冠层（占满格圆角丛，rows 2..15）。3 阶段共用同一丛轮廓，仅浆果多寡不同。

    丛呈「上窄下宽的圆角灌木」轮廓：顶端 rows 2..4 较窄、中下部 rows 5..15 满宽，底部 row 15 收窄。
    内部用亮 / 中 / 暗 三色叶簇交错表「茂密枝叶体积感」（机制等价 MC 浆果丛叶冠）。
    """
    # 顶端窄冠（rows 2..4，cols 5..10）
    for x in range(5, 11):
        put(px, x, 2, LEAF_MID)
        put(px, x, 3, LEAF_MID)
        put(px, x, 4, LEAF_MID)
    # 中下部满宽冠（rows 5..14，cols 3..12）
    for y in range(5, 15):
        for x in range(3, 13):
            put(px, x, y, LEAF_MID)
    # 底部收窄（row 15，cols 4..11）
    for x in range(4, 12):
        put(px, x, 15, LEAF_DARK)

    # 体积感：左上亮面（受光）、右下暗面（背光）—— 给中调丛铺立体感。
    for y in range(3, 8):  # 左上亮面
        for x in range(4, 9):
            if (x + y) % 2 == 0:
                put(px, x, y, LEAF_LIGHT)
    for y in range(10, 15):  # 右下暗面
        for x in range(8, 13):
            if (x + y) % 3 == 0:
                put(px, x, y, LEAF_DARK)
    # 顶端亮叶点缀（rows 2..4 中部偏亮，表「冠顶受光」）
    put(px, 7, 2, LEAF_LIGHT)
    put(px, 8, 3, LEAF_LIGHT)
    put(px, 6, 4, LEAF_LIGHT)


def draw_berries(px, stage):
    """在丛上点缀浆果。stage 0 → 无果；stage 1 → 2 颗青红小果；stage 2 → 4 颗饱满红浆果簇。"""
    if stage == 0:
        return
    if stage == 1:
        # 2 颗小青红未熟果（散布在丛中部，较小：1-2 像素）
        # 果 1（左中，rows 7..8）
        put(px, 5, 7, BERRY_UNRIPE); put(px, 6, 7, BERRY_UNRIPE_LIGHT)
        put(px, 5, 8, BERRY_UNRIPE_LIGHT); put(px, 6, 8, BERRY_UNRIPE)
        # 果 2（右下，rows 11..12）
        put(px, 9, 11, BERRY_UNRIPE); put(px, 10, 11, BERRY_UNRIPE_LIGHT)
        put(px, 9, 12, BERRY_UNRIPE); put(px, 10, 12, BERRY_UNRIPE)
        return
    # stage 2：4 颗饱满红浆果簇（分布在丛四角，每颗 2x2 带高光 + 暗边）
    berries = [
        # (左上角 x, y) —— 4 颗果的 2x2 块左上角坐标
        (4, 6),    # 左上果
        (9, 6),    # 右上果
        (5, 11),   # 左下果
        (9, 11),   # 右下果
    ]
    for (bx, by) in berries:
        put(px, bx, by, BERRY_RIPE)
        put(px, bx + 1, by, BERRY_RIPE_LIGHT)        # 受光高光（右上）
        put(px, bx, by + 1, BERRY_RIPE)
        put(px, bx + 1, by + 1, BERRY_RIPE_DARK)     # 暗边（右下）


def build_bush(stage):
    """生成一阶段灌木贴图。stage = 0 / 1 / 2。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    draw_bush_canopy(px)
    draw_berries(px, stage)
    # alpha bleed：把枝叶 / 浆果颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_flower.py）。
    img = bleed_alpha(img)
    return img


def main():
    for stage in (0, 1, 2):
        img = build_bush(stage)
        out = os.path.join(SRC, "default_sweet_berry_bush_%d.png" % stage)
        img.save(out)
        print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
