#!/usr/bin/env python3
"""生成小麦作物（WheatCrop）8 个生长阶段的 cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 小麦作物（wheat crop）—— cross 模型（两片对角相交平面）上贴本瓦片；mesher 据 chunk
state（阶段 0..7）选对应阶段瓦片。贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：8 阶段表达「嫩芽 → 拔高 → 抽穗 → 金黄成熟」的生长过程。透明底（alpha=0）+ 绿色茎叶；
阶段越低茎越短（透明像素占顶部更多）、阶段越高茎越长、末两阶段（6/7）顶部抽穗转金黄。cross 几何
满格高不变（同 MC：作物模型尺寸不变、贴图透明像素表达「未长到的部分」）。

t245 两项修复（用户实测回归）：
  (a) **黑边**（同 build_tall_grass.py）：茎/叶像素与透明底（RGB 0,0,0）相邻 → 图集线性过滤产生暗化边缘
      → alphaCutoff:0.5 保留并显成黑/暗边晕。修法 = alpha bleed（透明像素 RGB 填最近不透明色，alpha=0 不变），
      每阶段贴图生成后统一 bleed。
  (b) **stage 0 与草丛太像**：旧 stage 0 = 5 条竖向绿茎（与草丛同款「成丛竖绿」形态，仅更矮 → 肉眼难辨）。
      重画为**单棵 V/Y 形嫩芽**（种子堆 + 中央短芽 + 两片斜展子叶），形状与草丛（成丛高草叶）明显不同；
      + 小麦作物配色整体调为**橄榄黄绿**（与草丛纯绿进一步区分，机制对齐 MC 小麦作物叶色偏黄）。

图案（固定位置 + 确定性推导，无随机源 → 便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 透明底（alpha=0）；
  - stage 0：V/Y 形单棵嫩芽（特殊绘制 draw_seedling）；stage 1..5：几条竖向茎，高度随阶段递增；
  - 末两阶段（6/7）顶部画金黄麦穗（横向小颗粒）替代绿色顶 → 表达抽穗 / 成熟；
  - 茎配色两档（橄榄亮绿 / 橄榄暗绿）拟受光明暗；穗配色金黄（#e6c34d）/ 暗金（#b8902f）。

输出（覆盖写入 textures/）：
  default_wheat_stage_0.png ... default_wheat_stage_7.png   （tile 29..36）

依赖：仅 PIL，无外部贴图。与 build_tall_grass.py / build_farmland.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 茎列 + 基底顶端 y（画布 y 向下，顶在上；top_y 越小茎越高）。列分布于画布中段 [3,12]（留左右边距）。
# 每条茎的高度随阶段递增；顶端 y = max(基底 - 阶段增量, 上限)。固定位置（无随机源 → 可复现 / CI 可校验）。
#
# t245：STEMS 仅用于 stage 1..7（拔高期 / 抽穗期）。stage 0 走专用 draw_seedling（V/Y 形嫩芽），不再
#   共用此表 —— 旧实现 stage 0 = 同表 5 茎（grow=0）= 5 条矮竖绿茎，与草丛「成丛竖绿」肉眼难辨。
STEMS = [
    # (col, base_top_y) —— base_top_y = stage 1 时该茎顶端 y（最大 y，最矮）；随阶段每 +1 减 1px（向上长）。
    (4, 11),
    (6, 10),   # 中央主茎（最高）
    (8, 11),
    (10, 12),  # 侧茎（较矮）
    (7, 12),
]
# t245 配色调橄榄黄绿（与草丛纯绿 #6fae3a 区分）：小麦作物叶色偏黄绿（机制对齐 MC 小麦作物）。
STEM_LIGHT = (0x96, 0xb0, 0x2a)   # 橄榄亮绿（受光）—— vs 草丛 (0x6f,0xae,0x3a) 偏黄
STEM_DARK = (0x3e, 0x52, 0x12)    # 橄榄暗绿（阴影）
SEED_RGB = (0x4a, 0x36, 0x10)     # 种子堆色（暗棕橄榄，区别于茎叶绿）
EAR_LIGHT = (0xe6, 0xc3, 0x4d)    # 亮金（麦穗受光）
EAR_DARK = (0xb8, 0x90, 0x2f)     # 暗金（麦穗阴影）

# 阶段 → 每茎顶端 y 的减量（px；阶段越高茎越长，顶端 y 越小）。
#   stage 0 走 draw_seedling（不走本函数）；stage 1..5：每阶段茎顶端上移 1px（成长）；
#   stage 6/7：茎已达满高（不再上移），改在顶部抽穗。成熟（7）：穗更密 / 全金黄。抽穗阶段（6）：穗初现、尚带绿。
def stem_top(base_top, stage):
    grow = (stage - 1) if 1 <= stage <= 5 else 4  # stage 1..5 逐 px 长高（stage1=grow0..stage5=grow4）；6/7 不再长
    return max(2, base_top - grow)                # 顶端不低于 y=2（留画布顶边距）


def draw_stem(px, col, top_y):
    """竖向茎：列 col 自画布底（y=TS-1）向上长到 top_y（含），上端浅、下端深。"""
    for y in range(top_y, TS):
        t = (y - top_y) / max(1, TS - 1 - top_y)  # 0（顶）..1（底）
        r = int(STEM_LIGHT[0] * (1 - t) + STEM_DARK[0] * t)
        g = int(STEM_LIGHT[1] * (1 - t) + STEM_DARK[1] * t)
        b = int(STEM_LIGHT[2] * (1 - t) + STEM_DARK[2] * t)
        px[col, y] = (r, g, b, 255)


def draw_ear(px, col, top_y, dense):
    """在茎顶端 y=top_y 及其下 1 格画金黄麦穗（横向小颗粒）。dense=True → 穗更密（成熟 7）。"""
    rows = [top_y, top_y + 1] if dense else [top_y]
    for y in rows:
        for dx in (-1, 0, 1):
            x = col + dx
            if 0 <= x < TS and 0 <= y < TS:
                # 中心列用亮金、两侧用暗金（拟受光）；保持 alpha=255（覆盖茎顶绿色）。
                rgb = EAR_LIGHT if dx == 0 else EAR_DARK
                px[x, y] = (rgb[0], rgb[1], rgb[2], 255)


def draw_seedling(px):
    """t245 stage 0 专用：单棵 V/Y 形嫩芽 —— 种子堆 + 中央短芽 + 两片斜展子叶。

    与草丛（成丛高草叶）形态明显不同：稀疏、矮、V 形展开，肉眼一眼分辨「这是刚种的作物苗」而非
    「矮草丛」。整棵约占画布中下 1/3（y 11..15），留大量上部透明 → 表达「刚发芽、远未长高」。
    配色用小麦作物橄榄黄绿（STEM_LIGHT）+ 暗种子（SEED_RGB），与草丛纯绿进一步区分。

    像素布局（画布 y 向下，0 在顶；15 在底）：
      y=11:  .....#.#........  子叶尖（col 5 / 9）
      y=12:  ......#.#.......  子叶中段（col 6 / 8）
      y=13:  .......#........  中央短芽（col 7）+ 子叶根（col 6 / 8 衔接）
      y=14:  .......#........  中央短芽（col 7）
      y=15:  ......###.......  种子堆（col 6 / 7 / 8，暗棕橄榄）
    """
    # 中央短芽（col 7，y 13..14）+ 芽尖（y 11 亮）。
    px[7, 11] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    px[7, 12] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    px[7, 13] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    px[7, 14] = (STEM_DARK[0], STEM_DARK[1], STEM_DARK[2], 255)
    # 左子叶（向左上斜展）：(6,13) → (6,12) → (5,11)。
    px[6, 13] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    px[6, 12] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    px[5, 11] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    # 右子叶（向右上斜展）：(8,13) → (8,12) → (9,11)。
    px[8, 13] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    px[8, 12] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    px[9, 11] = (STEM_LIGHT[0], STEM_LIGHT[1], STEM_LIGHT[2], 255)
    # 种子堆（底部中央 3 px，暗棕橄榄）。
    for x in (6, 7, 8):
        px[x, 15] = (SEED_RGB[0], SEED_RGB[1], SEED_RGB[2], 255)


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    t245 alpha 边缘修复（同 build_tall_grass.py：见该文件头注释）。迭代膨胀（8 邻域平均）—— 每轮把
    「有已着色邻居（不透明 或 上一轮已 bleed）的透明像素」填为邻居平均色。级联使整个透明区都被就近的
    不透明色覆盖（非仅 1 px 边圈），对线性过滤 / 未来启用 mipmap / 远距宽滤波都鲁棒。alpha=0 像素只改
    RGB、alpha 仍 0 → alphaCutoff 仍丢弃它们（不破坏 cutout 语义），但过滤在 alpha 渐变区不再产生黑边晕。
    """
    img = img.convert("RGBA")
    W, H = img.size
    px = img.load()
    # 用「RGB!=(0,0,0)」判定该像素是否已着色（本族贴图不透明像素恒为橄榄绿/金色，无纯黑 opaque；
    # 透明像素初值 (0,0,0,0)）。由此级联：已 bleed 的像素作下一轮的源。
    for _ in range(max(W, H) * 2):
        changed = False
        snap = [px[x, y] for y in range(H) for x in range(W)]  # 上一轮快照，防同轮连锁污染
        for y in range(H):
            for x in range(W):
                r0, g0, b0, a0 = snap[y * W + x]
                if a0 != 0 or (r0, g0, b0) != (0, 0, 0):
                    continue  # 已不透明 或 已 bleed，跳过
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
    for stage in range(8):
        img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
        px = img.load()
        if stage == 0:
            # t245 stage 0 = V/Y 形单棵嫩芽（与草丛形态区分），不再走 5 茎竖绿。
            draw_seedling(px)
        else:
            ear_stage = stage >= 6            # 阶段 6/7 抽穗
            dense_ear = stage >= 7            # 阶段 7 穗更密（成熟）
            for (col, base_top) in STEMS:
                top_y = stem_top(base_top, stage)
                draw_stem(px, col, top_y)
                if ear_stage:
                    draw_ear(px, col, top_y, dense_ear)
        # t245 alpha bleed：消除 cutout 黑边（橄榄绿 / 金穗色渗进透明底，alpha=0 不变）。
        img = bleed_alpha(img)
        out = os.path.join(SRC, "default_wheat_stage_%d.png" % stage)
        img.save(out)
        print("wrote", os.path.relpath(out, HERE), img.size, "stage", stage)


if __name__ == "__main__":
    main()
