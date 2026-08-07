#!/usr/bin/env python3
"""生成甘蔗（Sugarcane）cross 贴图（16x16 像素，原创自绘，§9 override (a)）。

t397 多群系装饰植物：甘蔗是水边生长的可叠高细茎植物 cross 广告牌方块（与花 / 草丛同走 cross
几何段），机制等价 MC 1.0 sugar cane / reeds。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「一束细茎甘蔗」—— 透明底（alpha=0）+ 一根绿色节段细茎（占画布竖向近满高）+
顶部尖叶。机制等价 MC 甘蔗细茎观感（贴图 alpha 表达「细于整立方」，cross 几何满格不变）。
worldgen placeSugarcane 在水边列散布 1..3 格高甘蔗柱，每格独立走 cross case 各画满高细茎 -> 视觉如
细茎柱（机制等价 MC 甘蔗 1..3 格柱）。mesher 把本瓦片贴到 cross 的两片对角双面 quad；chunk 地形
材质 alphaCutoff:0.5 丢弃透明底 -> 仅茎像素显（机制等价 MC cutout 甘蔗）。

节段：每隔几格一道横向深色节（机制等价 MC 甘蔗节段纹理），表「分节甘蔗茎」。

alpha bleed（同 build_flower.py / build_sapling.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）-> 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_sugarcane.png   （tile 67，甘蔗 cross 贴图）

依赖：仅 PIL，无外部贴图。与 build_flower.py / build_sapling.py 同风格（程序生成原创像素图）。
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


def main():
    # 透明底（alpha=0）。
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()

    def put(x, y, color):
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = color

    # 茎配色（黄绿色，比草丛略偏黄 + 浅，表「甘蔗嫩茎」）：亮面 #b8c24a / 暗面 #7a8a2a。
    stem_light = (0xb8, 0xc2, 0x4a, 255)
    stem_dark = (0x7a, 0x8a, 0x2a, 255)
    # 节段配色（深绿环纹，机制等价 MC 甘蔗节段）：节环 #4a6a1a。
    node = (0x4a, 0x6a, 0x1a, 255)
    # 尖叶配色（亮黄绿，顶部嫩叶）：亮 #c4d25a / 暗 #8a9a3a。
    tip_light = (0xc4, 0xd2, 0x5a, 255)
    tip_dark = (0x8a, 0x9a, 0x3a, 255)

    # 主茎：竖向 3 列宽（x=6/7/8，中央偏窄呈圆柱明暗），从画布底（y=15）到顶部（y=4），占竖向近满高。
    #   细茎观感（仅占画布中部 3 列，两侧透明 → cutout 后显成细茎柱，机制等价 MC 甘蔗「细于整立方」）。
    for y in range(4, 16):
        put(6, y, stem_dark)
        put(7, y, stem_light)
        put(8, y, stem_dark)

    # 节段环纹（每隔 ~4 格一道深色节，机制等价 MC 甘蔗节段分节）：在 y=12 / y=8 / y=4 处加深绿环。
    for ny in (12, 8, 4):
        for x in (6, 7, 8):
            put(x, ny, node)

    # 顶部尖叶（机制等价 MC 甘蔗顶部嫩叶尖）：以 (7, 3) 为中心向上散布几片尖叶像素。
    #   亮 / 暗双色给立体感。
    tips = [
        (7, 2, True), (8, 2, False),    # 顶两叶
        (6, 3, False), (7, 3, True), (8, 3, False),  # 中层三叶
        (5, 4, True), (9, 4, False),    # 下层两叶（与茎顶接）
    ]
    for (x, y, light) in tips:
        put(x, y, tip_light if light else tip_dark)

    # alpha bleed：把茎 / 叶颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_flower.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_sugarcane.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
