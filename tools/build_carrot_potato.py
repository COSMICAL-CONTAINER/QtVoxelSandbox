#!/usr/bin/env python3
"""生成胡萝卜 / 马铃薯作物（CarrotCrop / PotatoCrop）各 4 个生长阶段的 cross 贴图（16×16 像素，原创自绘，
§9 override (a)）。

机制等价 MC 1.0 carrot / potato 作物 —— cross 模型（两片对角相交平面）上贴本瓦片；mesher 据 chunk state
（age 0..7）选 tile = 基底 + state/2（4 视觉阶段，每阶段覆盖 2 个年龄：age 0-1→stage0、2-3→stage1、
4-5→stage2、6-7→stage3）。贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图（4 阶段表达「嫩芽 → 拔高 → 叶丛 → 成熟（根部露出土）」）：
  - 透明底（alpha=0）+ 绿色茎叶；阶段越低茎越短、阶段越高茎越长 / 叶越密；
  - 末阶段（stage 3 = 成熟）底部画出作物地下部「露出土」：胡萝卜 = 橙红锥根 / 马铃薯 = 棕黄椭圆块茎。
  - cross 几何满格高不变（同 MC / 小麦：作物模型尺寸不变、贴图透明像素表达「未长到的部分」）。

两种作物形态区分（肉眼可辨「这是胡萝卜 / 那是马铃薯」）：
  - 胡萝卜：茎细而高、呈羽状（多细裂叶），配色橄榄黄绿（与小麦橄榄绿再偏黄区分）；
  - 马铃薯：茎粗而矮、呈阔卵叶丛（少而宽的叶片），配色深草绿。

输出（覆盖写入 textures/）：
  default_carrot_crop_0.png ... default_carrot_crop_3.png   （tile 69..72）
  default_potato_crop_0.png ... default_potato_crop_3.png   （tile 73..76）

依赖：仅 PIL，无外部贴图。与 build_wheat.py / build_tall_grass.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# ── 配色 ──
# 胡萝卜茎叶：橄榄黄绿（比小麦 #96b02a 更偏黄，与小麦区分；机制对齐 MC 胡萝卜叶色偏黄绿）。
CARROT_LIGHT = (0x9a, 0xb8, 0x2a)
CARROT_DARK = (0x4a, 0x5e, 0x14)
CARROT_ROOT = (0xe0, 0x7a, 0x1e)   # 橙红胡萝卜根（露出土部分）
# 马铃薯茎叶：深草绿（比小麦 / 胡萝卜更深更蓝绿，阔叶观感）。
POTATO_LIGHT = (0x4e, 0x8a, 0x32)
POTATO_DARK = (0x2a, 0x52, 0x18)
POTATO_TUBER = (0xb8, 0x92, 0x3a)  # 棕黄马铃薯块茎（露出土部分）
SEED_RGB = (0x4a, 0x36, 0x10)      # 种子堆色（暗棕橄榄）


def draw_seedling(px, light, dark):
    """stage 0（age 0-1）专用：单棵 V/Y 形嫩芽 —— 种子堆 + 中央短芽 + 两片斜展子叶。

    与草丛 / 小麦 stage 0 同款「刚发芽」形态（稀疏、矮、V 形展开），肉眼一眼分辨「这是刚种的作物苗」。
    """
    # 中央短芽（col 7，y 12..13）。
    px[7, 12] = (light[0], light[1], light[2], 255)
    px[7, 13] = (light[0], light[1], light[2], 255)
    px[7, 14] = (dark[0], dark[1], dark[2], 255)
    # 左子叶（向左上斜展）。
    px[6, 13] = (light[0], light[1], light[2], 255)
    px[6, 12] = (light[0], light[1], light[2], 255)
    px[5, 11] = (light[0], light[1], light[2], 255)
    # 右子叶（向右上斜展）。
    px[8, 13] = (light[0], light[1], light[2], 255)
    px[8, 12] = (light[0], light[1], light[2], 255)
    px[9, 11] = (light[0], light[1], light[2], 255)
    # 种子堆（底部中央 3 px，暗棕橄榄）。
    for x in (6, 7, 8):
        px[x, 15] = (SEED_RGB[0], SEED_RGB[1], SEED_RGB[2], 255)


def draw_stems(px, stage, stems, light, dark):
    """stage 1..3：若干竖向茎，高度随阶段递增；顶端浅、下端深。

    stems = [(col, base_top_y), ...]：base_top_y = stage 1 时该茎顶端 y（最大 y = 最矮）；
    每阶段 +1 顶端 y 减 1px（向上长），到 stage 3 不再长高（满格）。
    """
    grow = (stage - 1) if stage <= 3 else 2  # stage 1=grow0、2=grow1、3=grow2
    for (col, base_top) in stems:
        top_y = max(2, base_top - grow)
        for y in range(top_y, TS):
            t = (y - top_y) / max(1, TS - 1 - top_y)  # 0（顶）..1（底）
            r = int(light[0] * (1 - t) + dark[0] * t)
            g = int(light[1] * (1 - t) + dark[1] * t)
            b = int(light[2] * (1 - t) + dark[2] * t)
            px[col, y] = (r, g, b, 255)


def draw_leaves(px, stage, leaves, light, dark):
    """stage 2..3：在茎中段两侧画阔叶 / 羽状裂叶（stage 3 比 stage 2 更密）。

    leaves = [(col, y, side), ...]：side = -1 左 / +1 右。stage 2 画稀疏、stage 3 画密集（追加更多叶）。
    """
    for (col, y, side) in leaves:
        x = col + side
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = (light[0], light[1], light[2], 255)
        # 叶尖（再外一格，仅 stage 3 加密时显暗色叶尖）
        if stage >= 3:
            x2 = col + side * 2
            if 0 <= x2 < TS and 0 <= y < TS:
                px[x2, y] = (dark[0], dark[1], dark[2], 255)


def draw_mature_base(px, root_rgb, kind):
    """stage 3（成熟）底部画作物地下部「露出土」：胡萝卜橙红锥根 / 马铃薯棕黄椭圆块茎。

    mechanism 等价 MC 1.0 成熟作物根部露出地表的视觉（玩家肉眼知「可收割」）。kind = "carrot"（锥根）/ "potato"（块茎）。
    """
    if kind == "carrot":
        # 橙红锥根：底部中央逐行收窄（y 12..15），形似胡萝卜倒锥。
        #   y=12: col 6..8（3 px）/ y=13: 6..8 / y=14: 7（尖）/ y=15: 7（尖）。
        for x in (6, 7, 8):
            px[x, 12] = (root_rgb[0], root_rgb[1], root_rgb[2], 255)
            px[x, 13] = (root_rgb[0], root_rgb[1], root_rgb[2], 255)
        px[7, 14] = (root_rgb[0], root_rgb[1], root_rgb[2], 255)
        px[7, 15] = (root_rgb[0], root_rgb[1], root_rgb[2], 255)
    else:  # potato
        # 棕黄椭圆块茎：底部中央偏右一团（y 13..15），形似马铃薯块茎露出土。
        #   y=13: col 6..9（4 px）/ y=14: 6..9 / y=15: 7..8（底收窄）。
        for x in (6, 7, 8, 9):
            px[x, 13] = (root_rgb[0], root_rgb[1], root_rgb[2], 255)
            px[x, 14] = (root_rgb[0], root_rgb[1], root_rgb[2], 255)
        for x in (7, 8):
            px[x, 15] = (root_rgb[0], root_rgb[1], root_rgb[2], 255)


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    t245 alpha 边缘修复（同 build_wheat.py / build_tall_grass.py）：cutout 透明底 + 线性过滤会产生黑边晕，
    bleed 把透明像素 RGB 填为就近的不透明色（alpha 仍 0 → alphaCutoff 仍丢弃），消除暗边。
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


# 胡萝卜茎位（细而高，多列呈羽状；base_top_y = stage 1 顶端）。列分布于画布中段 [4,11]。
CARROT_STEMS = [
    (4, 12), (6, 10), (7, 9), (8, 10), (10, 12), (5, 11), (9, 11),
]
# 胡萝卜羽状裂叶（stage 2..3 在茎两侧画细裂叶）：(col, y, side)。
CARROT_LEAVES = [
    (6, 7, -1), (6, 8, +1), (8, 7, +1), (8, 8, -1),
    (7, 5, -1), (7, 5, +1), (5, 9, -1), (9, 9, +1),
    (7, 6, -1), (7, 6, +1), (6, 9, +1), (8, 9, -1),
]
# 马铃薯茎位（粗而矮，少列阔叶；base_top_y 比胡萝卜略矮）。
POTATO_STEMS = [
    (5, 11), (6, 10), (7, 10), (8, 10), (9, 11),
]
# 马铃薯阔卵叶（stage 2..3 在茎两侧画宽叶）：(col, y, side)。
POTATO_LEAVES = [
    (6, 7, -1), (6, 7, +1), (8, 7, -1), (8, 7, +1),
    (7, 5, -1), (7, 5, +1), (5, 9, -1), (9, 9, +1),
    (7, 6, -1), (7, 6, +1), (6, 9, +1), (8, 9, -1),
]


def draw_crop(px, stage, light, dark, root_rgb, kind, stems, leaves):
    """画一种作物的某个阶段。stage 0..3。"""
    if stage == 0:
        draw_seedling(px, light, dark)
    else:
        draw_stems(px, stage, stems, light, dark)
        if stage >= 2:
            draw_leaves(px, stage, leaves, light, dark)
        if stage >= 3:
            draw_mature_base(px, root_rgb, kind)


def main():
    for stage in range(4):
        # 胡萝卜
        img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
        draw_crop(img.load(), stage, CARROT_LIGHT, CARROT_DARK, CARROT_ROOT,
                  "carrot", CARROT_STEMS, CARROT_LEAVES)
        img = bleed_alpha(img)
        out = os.path.join(SRC, "default_carrot_crop_%d.png" % stage)
        img.save(out)
        print("wrote", os.path.relpath(out, HERE), img.size, "stage", stage)
        # 马铃薯
        img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
        draw_crop(img.load(), stage, POTATO_LIGHT, POTATO_DARK, POTATO_TUBER,
                  "potato", POTATO_STEMS, POTATO_LEAVES)
        img = bleed_alpha(img)
        out = os.path.join(SRC, "default_potato_crop_%d.png" % stage)
        img.save(out)
        print("wrote", os.path.relpath(out, HERE), img.size, "stage", stage)


if __name__ == "__main__":
    main()
