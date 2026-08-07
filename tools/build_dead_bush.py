#!/usr/bin/env python3
"""生成枯死的灌木（DeadBush）cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

t394 沙漠群系内容：枯死的灌木是沙漠装饰性 cross 广告牌方块（与草丛 / 树苗同走 cross 几何段），
机制等价 MC 1.0 枯死的灌木（dead bush）—— 沙漠干旱地表的枯枝点缀，无碰撞可踩过。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「一丛干枯的木枝」—— 透明底（alpha=0）+ 几根棕褐 / 灰褐干枝从底部向上呈放射散开
（拟旱死灌木的残枝）。mesher 把本瓦片贴到 cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5
丢弃透明底 → 仅枯枝像素显（机制等价 MC cutout 枯灌木）。透明底是关键：若无 alpha，cross 会显成两片
实心板（非枯枝）。

alpha bleed（同 build_sapling.py / build_tall_grass.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_dead_bush.png   （tile 56，枯死的灌木 cross 贴图）

依赖：仅 PIL，无外部贴图。与 build_sapling.py / build_tall_grass.py 同风格（程序生成原创像素图）。
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

    # 干枝配色（棕褐 / 灰褐，旱死木质；亮面 / 暗面交错给立体感）。
    stick_light = (0x8a, 0x66, 0x38, 255)  # 棕褐亮面
    stick_dark = (0x5c, 0x42, 0x24, 255)   # 棕褐暗面
    stick_grey = (0x6e, 0x5a, 0x3e, 255)   # 灰褐（老枯枝）

    # 主根簇从画布底（y=15）中央 (7.5, 15) 向上放射散开数根干枝（折线）。
    #   每根枝由若干段组成（from bottom 向上 / 向侧），段坐标手调呈自然放射。
    def branch(pts, color):
        for (x, y) in pts:
            put(x, y, color)
            # 枝略加粗（1px 邻居）让 cross 缩放后不致太细。
            put(x + 1, y, color)

    # 中央直立主枝（y 15 → 6），亮 / 暗双列给圆柱感。
    for y in range(6, 16):
        put(7, y, stick_dark)
        put(8, y, stick_light)
    # 顶端分叉两小枝。
    put(7, 5, stick_light); put(6, 5, stick_dark); put(8, 5, stick_light)
    put(6, 4, stick_dark); put(9, 4, stick_light)

    # 左倾枝（从底 (6,15) 向左上到 (3,8)）。
    branch([(6, 15), (6, 14), (5, 13), (5, 12), (4, 11), (4, 10), (3, 9), (3, 8)], stick_grey)
    put(3, 7, stick_light); put(2, 7, stick_dark)

    # 右倾枝（从底 (9,15) 向右上到 (12,9)）。
    branch([(9, 15), (9, 14), (10, 13), (10, 12), (11, 11), (11, 10), (12, 9)], stick_grey)
    put(12, 8, stick_light); put(13, 8, stick_dark)

    # 左下短匍匐枝（底 (5,15) 向左）。
    branch([(5, 15), (4, 15), (3, 15)], stick_dark)
    # 右下短匍匐枝。
    branch([(10, 15), (11, 15), (12, 15)], stick_dark)

    # alpha bleed：把枯枝颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_sapling.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_dead_bush.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
