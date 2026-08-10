#!/usr/bin/env python3
"""生成蜘蛛网（Cobweb）cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

t484 废弃矿井结构方块：蜘蛛网是 cross 形广告牌方块（与草丛 / 树苗 / 枯灌木同走 cross 几何段），
机制等价 MC 1.0 蛛网（cobweb / web）—— 废弃矿井巷道间隙的蛛丝装饰，无碰撞可穿过。名称 / 贴图纯
原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「一格蛛网」—— 透明底（alpha=0）+ 灰白蛛丝从中心向 8 个方向放射（拟蜘蛛网的放射
辐 + 同心螺旋环），中心一个亮结点。mesher 把本瓦片贴到 cross 的两片对角双面 quad（俯视成 X 形），
chunk 地形材质 alphaCutoff:0.5 丢弃透明底 → 仅蛛丝像素显（机制等价 MC cutout 蛛网）。透明底是关键：
若无 alpha，cross 会显成两片灰白实心板（非蛛网）。

alpha bleed（同 build_dead_bush.py / build_sapling.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_cobweb.png   （tile 120，蜘蛛网 cross 贴图）

依赖：仅 PIL，无外部贴图。与 build_dead_bush.py / build_tall_grass.py 同风格（程序生成原创像素图）。
"""
import os
import math
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 蛛丝配色（灰白系，拟真实蛛丝的银灰半透感；三层给立体感 + 中心亮结点）。
WEB_LIGHT = (0xf0, 0xf0, 0xe8, 255)  # 蛛丝亮面 / 中心结点（近白）
WEB_MID   = (0xc8, 0xc8, 0xc0, 255)  # 蛛丝主色（银灰）
WEB_DARK  = (0x90, 0x90, 0x88, 255)  # 蛛丝暗面 / 辐条底（暗灰，给蛛丝一点层次）


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    同 build_dead_bush.py 的 bleed_alpha（t454 alpha 边缘修复）：见文件头注释。
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


def bresenham(x0, y0, x1, y1):
    """整数 Bresenham 直线点列（含端点）。返回 [(x,y), ...]。"""
    pts = []
    dx = abs(x1 - x0)
    dy = abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx - dy
    while True:
        pts.append((x0, y0))
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 > -dy:
            err -= dy
            x0 += sx
        if e2 < dx:
            err += dx
            y0 += sy
    return pts


def line(px, x0, y0, x1, y1, color):
    """沿 (x0,y0)-(x1,y1) 画 1px Bresenham 细线（不越界）。"""
    for (x, y) in bresenham(x0, y0, x1, y1):
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = color


def put(px, x, y, color):
    if 0 <= x < TS and 0 <= y < TS:
        px[x, y] = color


def main():
    # 透明底（alpha=0）。
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()

    # 蛛网中心（贴图中心 7.5,7.5 → 取格子 (7,7) 与 (8,8) 之间的中点）。8 方向放射辐条。
    cx, cy = 7.5, 7.5
    # 8 个方向（含对角 + 直边），辐条端点贴近贴图边缘。
    dirs = [
        (15, 15), (15, 8), (15, 1),
        (8, 15),           (8, 1),
        (1, 15),  (1, 8),  (1, 1),
    ]
    # 画 8 条放射辐条（WEB_MID 主色，1px 细线）—— 蛛网的骨架。
    for (ex, ey) in dirs:
        line(px, int(cx), int(cy), ex, ey, WEB_MID)

    # 同心方形环（连接辐条，拟蛛网螺旋环）：在距中心 radius=2/4/6 处画方形环。
    #   环沿辐条间的「阶梯」走（Bresenham 自然产生阶梯方框 → 拟蛛网环）。
    def ring(radius, color):
        # 取 8 个辐条上半径距离的格点，连成方框。
        pts = []
        for (ex, ey) in dirs:
            # 沿辐条从中心走 radius 步（Bresenham 第 radius 个点）。
            bs = bresenham(int(cx), int(cy), ex, ey)
            idx = min(radius, len(bs) - 1)
            pts.append(bs[idx])
        for i in range(len(pts)):
            x0, y0 = pts[i]
            x1, y1 = pts[(i + 1) % len(pts)]
            line(px, x0, y0, x1, y1, color)

    ring(3, WEB_MID)   # 内环
    ring(6, WEB_DARK)  # 外环（暗灰，给蛛网层次）

    # 中心结点（蛛丝汇聚的亮结点）：2×2 WEB_LIGHT 小方块。
    for (x, y) in [(7, 7), (8, 7), (7, 8), (8, 8)]:
        put(px, x, y, WEB_LIGHT)

    # 暗面强调：辐条上几处点 1px WEB_DARK，给 1px 细线一点明暗（蛛丝圆柱质感），不破坏细线感。
    for (ex, ey) in dirs:
        bs = bresenham(int(cx), int(cy), ex, ey)
        if len(bs) > 4:
            x, y = bs[4]
            put(px, x, y, WEB_DARK)

    # alpha bleed：把蛛丝颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_dead_bush.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_cobweb.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
