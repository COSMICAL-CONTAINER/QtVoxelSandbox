#!/usr/bin/env python3
"""生成树苗（Sapling）cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

t305 树叶衰减 + 树苗：树苗是 cross 形广告牌方块（与草丛 / 小麦作物同走 PartialBlockGeometry 的
cross 几何段），机制等价 MC 1.0 橡树树苗（sapling）—— 种在草地 / 泥土上，随时间生长成完整橡树。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「一棵小树苗」—— 透明底（alpha=0）+ 一根棕色短树干 + 顶部几片绿色嫩叶（呈小球状
树冠雏形）。mesher 把本瓦片贴到 cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5 丢弃
透明底 → 仅树苗像素显（机制等价 MC cutout 树苗）。透明底是关键：若无 alpha（实心底），cross 会
显成两片实心板（非树苗）。

t245 alpha bleed（同 build_tall_grass.py）：把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0）
→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_sapling.png   （tile 39，树苗 cross 贴图）

依赖：仅 PIL，无外部贴图。与 build_tall_grass.py / build_wheat.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    同 build_tall_grass.py 的 bleed_alpha（t245 alpha 边缘修复）：见文件头注释。
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
        """安全置像素（越界跳过）。color = (r,g,b,a)。"""
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = color

    # 树干配色（棕色木质，与原木亮面同族）：亮面 #9c7340 / 暗面 #6b4f24。
    trunk_light = (0x9c, 0x73, 0x40, 255)
    trunk_dark = (0x6b, 0x4f, 0x24, 255)
    # 树冠嫩叶配色（鲜绿，比草丛嫩绿略浅 + 偏黄，表「幼嫩树苗叶」）：亮 #6fae3a / 暗 #3a6a1a。
    leaf_light = (0x6f, 0xae, 0x3a, 255)
    leaf_dark = (0x3a, 0x6a, 0x1a, 255)

    # 树干：从画布底（y=15）向上长到 y=7（占下半部分，短树苗主干）。列 x=7/8（中央偏左两列，圆柱明暗）。
    for y in range(7, 16):
        put(7, y, trunk_dark)   # 左暗面
        put(8, y, trunk_light)  # 右亮面

    # 树冠：树干顶部（y=4..8）四周布绿叶，呈小球状树冠雏形。以 (7.5, 6) 为中心散布叶像素。
    #   亮 / 暗叶交错 → 立体感（受光面亮、背光面暗）。
    crown = [
        # (x, y, light?)
        (6, 5, True), (7, 4, True), (8, 4, False), (9, 5, False),
        (5, 6, True), (6, 6, True), (7, 6, False), (8, 6, True), (9, 6, False), (10, 6, False),
        (5, 7, False), (6, 7, True), (9, 7, False), (10, 7, True),
        (6, 8, False), (9, 8, True),
        (7, 3, True), (8, 3, False),  # 顶端两叶（收口）
    ]
    for (x, y, light) in crown:
        put(x, y, leaf_light if light else leaf_dark)

    # alpha bleed：把树干 / 叶颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_tall_grass.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_sapling.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
