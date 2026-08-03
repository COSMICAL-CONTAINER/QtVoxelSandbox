#!/usr/bin/env python3
"""生成小麦作物（WheatCrop）8 个生长阶段的 cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 小麦作物（wheat crop）—— cross 模型（两片对角相交平面）上贴本瓦片；mesher 据 chunk
state（阶段 0..7）选对应阶段瓦片。贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：8 阶段表达「嫩芽 → 拔高 → 抽穗 → 金黄成熟」的生长过程。透明底（alpha=0）+ 绿色茎叶；
阶段越低茎越短（透明像素占顶部更多）、阶段越高茎越高、末两阶段（6/7）顶部抽穗转金黄。cross 几何
满格高不变（同 MC：作物模型尺寸不变、贴图透明像素表达「未长到的部分」）。

图案（固定位置 + 确定性推导，无随机源 → 便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 透明底（alpha=0）；
  - 几条竖向茎（列分布于画布中段），高度随阶段递增（stage 0: 4px → stage 5: ~12px）；
  - 末两阶段（6/7）顶部画金黄麦穗（横向小颗粒）替代绿色顶 → 表达抽穗 / 成熟；
  - 茎配色两档（亮绿 / 暗绿）拟受光明暗；穗配色金黄（#e6c34d）/ 暗金（#b8902f）。

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
STEMS = [
    # (col, base_top_y) —— base_top_y = 阶段 0 时该茎顶端 y（最大 y，最矮）；随阶段每 +1 减 1px（向上长）。
    (4, 11),
    (6, 10),   # 中央主茎（最高）
    (8, 11),
    (10, 12),  # 侧茎（较矮）
    (7, 12),
]
STEM_LIGHT = (0x6f, 0xb0, 0x2a)   # 亮绿（受光）
STEM_DARK = (0x3c, 0x6a, 0x14)    # 暗绿（阴影）
EAR_LIGHT = (0xe6, 0xc3, 0x4d)    # 亮金（麦穗受光）
EAR_DARK = (0xb8, 0x90, 0x2f)     # 暗金（麦穗阴影）

# 阶段 → 每茎顶端 y 的减量（px；阶段越高茎越长，顶端 y 越小）。
#   阶段 0..5：每阶段茎顶端上移 1px（成长）；阶段 6/7：茎已达满高（不再上移），改在顶部抽穗。
#   成熟（7）：穗更密 / 全金黄。抽穗阶段（6）：穗初现、尚带绿。
def stem_top(base_top, stage):
    grow = stage if stage <= 5 else 5  # 阶段 0..5 逐 px 长高；6/7 不再长（转入抽穗）
    return max(2, base_top - grow)     # 顶端不低于 y=2（留画布顶边距）


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


def main():
    for stage in range(8):
        img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
        px = img.load()
        ear_stage = stage >= 6            # 阶段 6/7 抽穗
        dense_ear = stage >= 7            # 阶段 7 穗更密（成熟）
        for (col, base_top) in STEMS:
            top_y = stem_top(base_top, stage)
            draw_stem(px, col, top_y)
            if ear_stage:
                draw_ear(px, col, top_y, dense_ear)
        out = os.path.join(SRC, "default_wheat_stage_%d.png" % stage)
        img.save(out)
        print("wrote", os.path.relpath(out, HERE), img.size, "stage", stage)


if __name__ == "__main__":
    main()
