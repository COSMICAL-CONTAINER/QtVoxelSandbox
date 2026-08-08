#!/usr/bin/env python3
"""生成枯死的灌木（DeadBush）cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

t394 沙漠群系内容：枯死的灌木是沙漠装饰性 cross 广告牌方块（与草丛 / 树苗同走 cross 几何段），
机制等价 MC 1.0 枯死的灌木（dead bush）—— 沙漠干旱地表的枯枝点缀，无碰撞可踩过。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「一丛干枯的木枝」—— 透明底（alpha=0）+ 几根**细而分明**的棕褐 / 灰褐干枝从底部根簇
呈放射散开（拟旱死灌木的残枝骨架），每根枝顶端留一小簇干枯的种壳 / 枝梢亮点。mesher 把本瓦片贴到
cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5 丢弃透明底 → 仅枯枝像素显（机制等价 MC
cutout 枯灌木）。透明底是关键：若无 alpha，cross 会显成两片实心板（非枯枝）。

t454 重做（修「图标现糊 / 糊块」）：旧实现用 `put(x,y)+put(x+1,y)` 把每根枝画成 **2px 宽横带**，且多根
枝在底部根区**密集重叠** → 经 build_cube_icons.py 的 render_flat_2d 做 4× NEAREST 放大后，每根 2px 枝
变成 8px 宽粗块，且底部根区重叠成「糊块」—— hotbar / 创造调色板图标读作一团棕褐色模糊块，而非清晰的
枯枝线条。世界内 cross 广告牌同样显粗。修法：改用 **Bresenham 1px 细线**画放射枝（细而分明、彼此留白），
仅根簇 + 枝梢种壳做 2~3px 局部加粗（锚点 / 视觉重量）→ 4× 放大后呈 4px 细枝 + 锚点亮点，读作「清晰的
枯木枝线条」。机制与 MC 枯灌木一致（细枝放射骨架），纯原创线条艺术。

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

# 干枝配色（棕褐 / 灰褐，旱死木质；三层给立体感 + 干枯种壳亮点）。沿用 t394 既有色调保持系列一致。
STICK_LIGHT = (0x8a, 0x66, 0x38, 255)  # 棕褐亮面 / 干枯种壳
STICK_MID   = (0x6e, 0x5a, 0x3e, 255)  # 灰褐主枝干（老枯枝主色）
STICK_DARK  = (0x4a, 0x34, 0x1c, 255)  # 棕褐暗面 / 根簇底


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


def polyline(px, pts, color):
    """沿 pts 折线画 1px 细线（每段 Bresenham）。每根枝细而清晰（区别旧 2px 横带）。"""
    for i in range(len(pts) - 1):
        x0, y0 = pts[i]
        x1, y1 = pts[i + 1]
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

    # 根簇（底部中央 2×3 加粗锚点）：固定整丛枯枝的视觉重量，让放射枝有「根」。STICK_DARK 表贴地腐根。
    for (x, y) in [(7, 15), (8, 15), (7, 14), (8, 14), (6, 14), (9, 14), (7, 13)]:
        put(px, x, y, STICK_DARK)

    # 主干（略偏左、顶端向左弧）：从根簇 (7,14) 向上经 (7,9) 弯到 (6,5)→(6,3)。
    #   中段用 STICK_MID 主色，让主干成为放射骨架的中轴（其它枝从其上分叉）。
    polyline(px, [(7, 14), (7, 9), (6, 5), (6, 3)], STICK_MID)

    # 放射分枝（每根 1px 细线，彼此留白、顶端各自分散 → 放大后读作分明枯枝，非重叠糊块）：
    #   B1 右下分支：从主干低位 (7,12) 向右上斜展到 (11,5)，顶端伸到 (12,3)。
    polyline(px, [(7, 12), (9, 9), (11, 5), (12, 3)], STICK_MID)
    #   B2 左下分支：从根上 (7,13) 向左上斜展到 (3,8)，顶端伸到 (2,6)（左侧最宽展开，给整丛宽度）。
    polyline(px, [(7, 13), (5, 11), (3, 8), (2, 6)], STICK_MID)
    #   B3 右上小枝：从主干高位 (6,6) 向右上斜伸到 (9,3)。
    polyline(px, [(6, 6), (8, 4), (9, 3)], STICK_MID)
    #   B4 左上小枝：从主干高位 (6,5) 向左上斜伸到 (3,3)。
    polyline(px, [(6, 5), (4, 4), (3, 3)], STICK_MID)
    #   B5 中左细枝：从主干中段 (7,10) 向左上斜伸到 (4,6)（填补主干与 B2 之间的留白）。
    polyline(px, [(7, 10), (5, 8), (4, 6)], STICK_MID)

    # 干枯种壳 / 枝梢亮点（每根枝顶端 2~3px STICK_LIGHT 小簇）：给枯枝一个「干花 / 种壳」收尾，
    #   亮色提层次、增加可辨识的枝梢节点（旧实现只在顶端零散 1px 亮点，放大后看不出）。
    #   种壳簇 = 顶端点 + 上方一格 + 侧 1px，呈小三角（拟干枯花序）。
    def seed_head(cx, cy):
        put(px, cx, cy, STICK_LIGHT)
        put(px, cx, cy - 1, STICK_LIGHT)
        put(px, cx + 1, cy - 1, STICK_LIGHT)
    seed_head(6, 3)   # 主干顶端
    seed_head(12, 3)  # B1 顶端
    seed_head(2, 6)   # B2 顶端
    seed_head(9, 3)   # B3 顶端
    seed_head(3, 3)   # B4 顶端

    # 暗面强调：主干 + B1/B2 下侧各点 1px STICK_DARK，给 1px 细线一点明暗（圆柱木质感），不破坏细线感。
    for (x, y) in [(7, 12), (7, 11), (6, 7), (5, 12), (4, 10)]:
        put(px, x, y, STICK_DARK)

    # alpha bleed：把枯枝颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_sapling.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_dead_bush.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
