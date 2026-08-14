#!/usr/bin/env python3
"""生成铁轨（Rail）贴地薄板贴图（16×16 像素，原创自绘，§9 override (a)）。

t484 废弃矿井结构方块：铁轨是贴地薄板 flat 方块（mesher 走 PartialBlockGeometry 的 Rail case 画一片
水平双面 quad 贴 cell 底部，与睡莲横向浮叶同源几何），机制等价 MC 1.0 铁轨（rail）—— 废弃矿井木地板
上的铁轨铺设。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

t565 铁轨转弯 / 连接：追加两张变体贴图 ——
  default_rail_corner.png（tile 136）：直轨从底边（南，PIL y=15）竖直进入、向左（西，PIL x=0）弯出的
    90° 拐角轨。其余三个拐角方向由 mesher 换 UV 旋转 / 镜像映射（一张贴图四用，图集省瓦片）。
  default_rail_cross.png（tile 137）：十字交叉轨（南北 + 东西两向轨叠交，中央共用枕木块）。
  机制等价 MC 1.0 rail 的 corner / crossing 形态（本项目无坡道 / 动力铁轨，仅普通铁轨三种形态）。

视觉意图：读作「一段直铁轨」—— 透明底（alpha=0）+ 棕色枕木（横向木条）+ 灰铁双轨（纵向平行线）。
mesher 把本瓦片贴到 Rail 水平 quad（铺满 cell footprint），chunk 地形材质 alphaCutoff:0.5 丢弃透明底
→ 仅枕木 + 双轨像素显（机制等价 MC cutout 铁轨）。透明底是关键：若无 alpha，铁轨会显成一块实心板
（非枕木 + 轨）。

alpha bleed（同 build_dead_bush.py / build_cobweb.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_rail.png          （tile 121，铁轨直轨 NS 贴图）
  default_rail_corner.png   （tile 136，铁轨 90° 拐角贴图）
  default_rail_cross.png    （tile 137，铁轨十字交叉贴图）

依赖：仅 PIL，无外部贴图。与 build_dead_bush.py / build_cobweb.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 配色（棕色枕木 + 灰铁双轨 + 暗影 / 亮高光，拟真实铁轨的木 + 金属质感）。
WOOD_MID  = (0x6e, 0x4a, 0x28, 255)  # 枕木主色（深棕，老朽木质）
WOOD_DARK = (0x4a, 0x30, 0x18, 255)  # 枕木暗面（缝隙阴影）
RAIL_MID  = (0x8a, 0x8a, 0x8a, 255)  # 铁轨主色（中灰铁）
RAIL_DARK = (0x5a, 0x5a, 0x5a, 255)  # 铁轨暗面（轨底阴影）
RAIL_LIGHT = (0xc0, 0xc0, 0xc0, 255)  # 铁轨高光（金属反光）


def put(px, x, y, color):
    if 0 <= x < TS and 0 <= y < TS:
        px[x, y] = color


def hline(px, x0, x1, y, color):
    """水平 1px 线（含两端）。"""
    for x in range(min(x0, x1), max(x0, x1) + 1):
        put(px, x, y, color)


def vline(px, x, y0, y1, color):
    """竖直 1px 线（含两端）。"""
    for y in range(min(y0, y1), max(y0, y1) + 1):
        put(px, x, y, color)


def hspan(px, x0, x1, y, color, thickness=1):
    """水平等厚条（thickness 像素高，含两端）。"""
    for t in range(thickness):
        hline(px, x0, x1, y + t, color)


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


def main():
    # 透明底（alpha=0）。
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()

    # 枕木（横向木条）：4 根枕木均匀分布在贴图竖直方向（y=2/6/9/13），每根 3px 高（y, y+1）。
    #   枕木跨满贴图宽度（x 1..14，留 1px 边隙显「分离的枕木」感）。WOOD_MID 主色 + WOOD_DARK 底阴影。
    tie_ys = [2, 6, 9, 13]
    for ty in tie_ys:
        hline(px, 1, 14, ty, WOOD_MID)
        hline(px, 1, 14, ty + 1, WOOD_MID)
        hline(px, 1, 14, ty + 2, WOOD_DARK)  # 底阴影线（给枕木一点厚度感）

    # 铁轨（纵向双轨）：两条平行灰铁线在 x=4 / x=11，贯穿贴图竖直方向（y 0..15）。
    #   每条轨 2px 宽（x, x+1）：RAIL_MID 主色 + RAIL_LIGHT 高光（x 列）+ RAIL_DARK 轨底（x+1 列下沿）。
    for rx in [4, 11]:
        vline(px, rx, 0, 15, RAIL_LIGHT)   # 高光列（金属反光，最亮）
        vline(px, rx + 1, 0, 15, RAIL_MID)  # 主色列（中灰铁）

    # 轨底暗影（每根轨 x+1 列的枕木行下沿加深 RAIL_DARK）：增强轨压在枕木上的层次。
    for ty in tie_ys:
        put(px, 5, ty + 2, RAIL_DARK)
        put(px, 12, ty + 2, RAIL_DARK)

    # alpha bleed：把枕木 / 铁轨颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_cobweb.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_rail.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)

    # ── t565 铁轨 90° 拐角贴图（tile 136）──
    # 语义：双轨从贴图**底边**（南，PIL y=15）竖直进入，到中部（y=8 附近）向**左**（西，PIL x=0）弯出。
    #   其余三个拐角方向（南→东 / 北→西 / 北→东）由 mesher 换 UV 旋转 / 镜像映射（一张贴图四用）。
    #   轨线 = 两段 2px 宽的 L 形折线（外轨与内轨错半格 —— 真实铁轨拐角外轨半径大、内轨半径小）。
    cimg = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    cpx = cimg.load()
    # 纵段（南入口 → 中部）：双轨 x=4 / x=11，y 8..15（半格长）。
    for rx in [4, 11]:
        vline(cpx, rx, 8, 15, RAIL_LIGHT)
        vline(cpx, rx + 1, 8, 15, RAIL_MID)
    # 横段（中部 → 西出口）：双轨 y=4 / y=11，x 0..8（半格长）。两轨的横段 y 取纵段 x 的同值 → 拐角
    #   处两轨自然衔接（x=4 纵轨拐成 y=4 横轨、x=11 拐成 y=11）。外角（y=11 轨）在 (11..) 内侧补圆角暗色。
    for ry in [4, 11]:
        hline(cpx, 0, 8, ry, RAIL_LIGHT)
        hline(cpx, 0, 8, ry + 1, RAIL_MID)
    # 拐角衔接点（(4,4) / (11,11) 两轨转角）填轨色块，消除 L 形折线缺口。
    for c in [(4, 4), (11, 11)]:
        put(cpx, c[0], c[1], RAIL_LIGHT)
        put(cpx, c[0] + 1, c[1], RAIL_MID)
        put(cpx, c[0], c[1] + 1, RAIL_MID)
    # 拐角枕木：L 形木条 —— 纵段两根（y=12..14 横跨 x 1..14 保留入口段感）+ 横段两根（x=1..3 竖条）。
    #   简化：纵段入口一根满宽枕木（y=13）+ 横段出口一根满高枕木（x=2，y 1..14）+ 中央转角一块方枕木。
    hspan(cpx, 1, 14, 13, WOOD_MID, 2)                 # 南入口枕木（横，2px）
    for y in range(1, 15):
        put(cpx, 2, y, WOOD_MID)
        put(cpx, 3, y, WOOD_DARK)                       # 西出口枕木（竖，暗边）
    for yy in range(6, 10):                             # 中央转角方枕木（4×4）
        for xx in range(6, 10):
            put(cpx, xx, yy, WOOD_MID if xx < 8 else WOOD_DARK)
    cimg = bleed_alpha(cimg)
    out = os.path.join(SRC, "default_rail_corner.png")
    cimg.save(out)
    print("wrote", os.path.relpath(out, HERE), cimg.size)

    # ── t565 铁轨十字交叉贴图（tile 137）──
    # 语义：南北直轨 + 东西直轨十字叠交（三向 / 四向连接共用），中央共用一块方枕木。
    ximg = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    xpx = ximg.load()
    # 南北双轨（同 tile 121 布局：x=4 / x=11 纵贯）。
    for rx in [4, 11]:
        vline(xpx, rx, 0, 15, RAIL_LIGHT)
        vline(xpx, rx + 1, 0, 15, RAIL_MID)
    # 东西双轨（y=4 / y=11 横贯）。
    for ry in [4, 11]:
        hline(xpx, 0, 15, ry, RAIL_LIGHT)
        hline(xpx, 0, 15, ry + 1, RAIL_MID)
    # 枕木：南北向两根（y=2 / y=13）+ 东西向两根（x=2 / x=13）+ 中央方枕木（6..9 方块）。
    hspan(xpx, 1, 14, 2, WOOD_MID, 2)
    hspan(xpx, 1, 14, 13, WOOD_MID, 2)
    for y0 in [1, 12]:
        for y in range(y0, y0 + 2):
            hline(xpx, 2, 3, y, WOOD_MID)
            hline(xpx, 13, 14, y, WOOD_DARK)
    for yy in range(6, 10):
        for xx in range(6, 10):
            put(xpx, xx, yy, WOOD_MID if xx < 8 else WOOD_DARK)
    ximg = bleed_alpha(ximg)
    out = os.path.join(SRC, "default_rail_cross.png")
    ximg.save(out)
    print("wrote", os.path.relpath(out, HERE), ximg.size)


if __name__ == "__main__":
    main()
