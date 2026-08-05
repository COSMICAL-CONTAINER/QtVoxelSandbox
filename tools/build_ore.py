#!/usr/bin/env python3
"""生成矿石（CoalOre / IronOre / DiamondOre / CopperOre / GoldOre）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 矿石（嵌于 stone 区段、需镐采掘），但贴图为本项目程序生成的原创像素图，
**不**拷贝任何 MC 资产。基底取本工程既有石头（default_stone.png）逐像素副本，保证与
「石族」纹理一致（视觉上读得出「石头里嵌着矿」）；矿石用固定位置的色块簇点缀：

  - 煤矿（coal_ore）：石头底 + 多簇近黑斑块（煤层外露，配少量高光暗化边）；
  - 铁矿（iron_ore）：石头底 + 多簇棕橙斑点（铁锈色，配浅色高光）；
  - 钻矿（diamond_ore）：石头底 + 多簇青白菱斑（机制等价 MC 钻石矿，名称/贴图原创），
    高光偏冷白、底阴影偏深青，散布表「嵌于岩的晶体」；
  - 铜矿（copper_ore，t308）：石头底 + 多簇橙铜斑点 + 少量孔雀绿锈（机制等价 MC 1.0 铜矿，
    名称/贴图原创）。橙铜主色 + 绿锈副色显「铜氧化」，区别于铁的纯棕橙；
  - 金矿（gold_ore，t308）：石头底 + 多簇金黄斑点（机制等价 MC 1.0 金矿，名称/贴图原创）。
    暖金高光 + 深金阴影显「贵金属反光」，最亮最暖的矿石族一眼可辨。

色块位置固定（无随机源）→ 同输入同输出（确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
斑块布局刻意打散、不对称，避免「网格化 / 重复纹理」的人工感。

输出（覆盖写入 textures/）：
  default_coal_ore.png
  default_iron_ore.png
  default_diamond_ore.png
  default_copper_ore.png
  default_gold_ore.png

依赖：本脚本须先有 textures/default_stone.png（既有 CC0/原创资产）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def stone_base():
    """读既有石头贴图原样作底（逐像素副本 → 矿石嵌于真实石头纹理中，族内一致）。"""
    p = os.path.join(SRC, "default_stone.png")
    img = Image.open(p).convert("RGBA").resize((TS, TS), Image.NEAREST)
    return np.asarray(img, dtype=np.float64).copy()


def paint_blobs(canvas, centers, radius, color, highlight=None):
    """在 canvas 上画若干圆形色块（半径 radius 的菱形/十字邻域，像素硬边）。
    centers = [(x, y), ...]；color = (r, g, b)；highlight 可选高光色（画在色块左上偏移一格）。
    """
    for (cx, cy) in centers:
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if abs(dx) + abs(dy) > radius:
                    continue  # 菱形邻域（曼哈顿距离 ≤ radius）
                x, y = cx + dx, cy + dy
                if 0 <= x < TS and 0 <= y < TS:
                    canvas[y, x, 0:3] = color
        if highlight is not None:
            # 左上偏移一格画高光（拟光源从左上）
            hx, hy = cx - 1, cy - 1
            if 0 <= hx < TS and 0 <= hy < TS:
                canvas[hy, hx, 0:3] = highlight


def draw_coal(canvas):
    """煤矿：石头底 + 多簇近黑煤层斑块（散布，刻意不对称；少量高光暗化）。"""
    coal_dark = np.array([28.0, 28.0, 30.0])      # 近黑（煤层外露）
    coal_hi = np.array([70.0, 70.0, 74.0])        # 高光（边缘反光）
    # 簇心位置（固定；打散不对称，避开边角溢出）
    centers = [(3, 4), (10, 3), (12, 9), (5, 11), (8, 7)]
    paint_blobs(canvas, centers, radius=1, color=coal_dark, highlight=coal_hi)
    return canvas


def draw_iron(canvas):
    """铁矿：石头底 + 多簇棕橙铁锈斑点（散布；浅色高光显金属反光）。"""
    iron_base = np.array([196.0, 144.0, 92.0])    # 棕橙（铁锈外露）
    iron_hi = np.array([238.0, 196.0, 140.0])     # 高光（金属反光）
    centers = [(4, 3), (11, 4), (3, 10), (12, 11), (8, 8), (7, 12)]
    paint_blobs(canvas, centers, radius=1, color=iron_base, highlight=iron_hi)
    return canvas


def draw_diamond(canvas):
    """钻矿（diamond_ore）：石头底 + 多簇青白菱斑晶体（散布；冷白高光 + 深青阴影显晶体反光）。
    机制等价 MC 1.0 钻石矿，名称/贴图全原创（§9）。比煤/铁矿斑块更亮、更冷，肉眼即读「晶体矿」。"""
    dia_base = np.array([110.0, 214.0, 214.0])    # 青白（晶体外露，偏冷）
    dia_hi = np.array([208.0, 246.0, 246.0])      # 高光（晶体反光，近白青）
    centers = [(3, 5), (11, 3), (13, 10), (5, 11), (8, 8), (10, 13)]
    paint_blobs(canvas, centers, radius=1, color=dia_base, highlight=dia_hi)
    return canvas


def draw_copper(canvas):
    """铜矿（copper_ore，t308）：石头底 + 多簇橙铜斑点 + 少量孔雀绿锈（散布；暖橙高光显「铜氧化」）。
    机制等价 MC 1.0 铜矿，名称/贴图全原创（§9）。橙铜主色比铁的棕橙更鲜亮、更偏橘；副色孔雀绿（铜锈）
    点缀显「铜氧化」—— 与铁的纯棕橙铁锈区分（铁无绿锈）。"""
    cop_base = np.array([214.0, 128.0, 56.0])     # 橙铜（铜外露，鲜亮橘橙，比铁更亮更橘）
    cop_hi = np.array([248.0, 178.0, 96.0])       # 高光（铜反光，暖亮橙）
    patina = np.array([72.0, 168.0, 138.0])       # 孔雀绿（铜氧化锈，副色点缀；铁无此色 → 区分铜/铁）
    # 橙铜簇（散布；刻意不对称，避开边角溢出）
    centers = [(4, 4), (11, 3), (3, 11), (12, 10), (8, 7), (10, 13)]
    paint_blobs(canvas, centers, radius=1, color=cop_base, highlight=cop_hi)
    # 孔雀绿锈点（少量散布，表「铜氧化」—— 铜的身份证，与铁的纯棕橙铁锈区分）
    canvas[6, 9, 0:3] = patina
    canvas[9, 13, 0:3] = patina
    canvas[13, 5, 0:3] = patina
    return canvas


def draw_gold(canvas):
    """金矿（gold_ore，t308）：石头底 + 多簇金黄斑点（散布；暖金高光 + 深金阴影显「贵金属反光」）。
    机制等价 MC 1.0 金矿，名称/贴图全原创（§9）。金黄最亮最暖的矿石族一眼可辨（区别于铜的橙、铁的棕橙）。"""
    gold_base = np.array([244.0, 198.0, 48.0])    # 金黄（金外露，饱和暖金）
    gold_hi = np.array([252.0, 232.0, 120.0])     # 高光（金反光，近白金）
    centers = [(4, 3), (11, 4), (3, 10), (12, 11), (8, 8), (10, 13), (6, 13)]
    paint_blobs(canvas, centers, radius=1, color=gold_base, highlight=gold_hi)
    return canvas


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_coal(stone_base()), "default_coal_ore")
    save(draw_iron(stone_base()), "default_iron_ore")
    save(draw_diamond(stone_base()), "default_diamond_ore")
    save(draw_copper(stone_base()), "default_copper_ore")
    save(draw_gold(stone_base()), "default_gold_ore")


if __name__ == "__main__":
    main()
