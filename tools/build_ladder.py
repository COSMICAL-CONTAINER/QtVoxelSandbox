#!/usr/bin/env python3
"""生成木梯（Ladder）cross 贴图 + 图标（16×16 瓦片 + 64×64 图标，原创自绘，§9 override (a)）。

t413 垂直爬梯（vertical climb ladder）：机制等价 MC 1.0 梯子（ladder）—— 玩家走进梯格 + 按前即
逐格向上爬（竖井用）。t501 把几何从两片对角 cross 改为「单片贴墙 quad」（贴完整立方方块侧面、
state 编码贴墙方向）。mesher 把本瓦片整张铺满该贴墙 quad；chunk cutout 段材质 alphaCutoff:0.5
丢弃透明底 → 仅梯的纵轨 / 横级像素显（机制等价 MC cutout 梯子）。透明底是关键：若无 alpha（实心
底），贴墙 quad 会显成一片实心板（非梯子）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

t519 满格梯子贴图（修「放下形状上下部分太宽粗糙」）：旧版纵轨居中 x=4/5,10/11（仅瓦片中央 8/16
宽 + 两侧各 4/16 透明留白）→ 贴墙 quad 把整张贴图铺满 cell [0,1] face 时，梯子只显在格中心半宽、
两侧大块透明 → 观感「格中央的小梯图标、粗糙上下宽」（用户反馈「t501 换了贴图但几何没改」实指
贴图比例未满格 → 视觉效果仍粗糙）。t519 改纵轨贴到瓦片两侧（x=2/3 与 12/13）+ 横梯级满铺轨间
+ 4 道梯级等距覆盖全高 → 整张贴图「满格读作一把梯子」，铺满 face 后梯子铺满整格宽，读作「贴墙的
一把梯子」（机制等价 MC 1.0 ladder 贴图：轨靠瓦片边 + rung 满轨间，整张无大块透明留白）。

视觉意图：读作「一把梯子」—— 透明底（alpha=0）+ 两根棕色纵轨（左右各一、贴瓦片两侧）+ 4 道横向
梯级满铺轨间。

alpha bleed（同 build_sapling.py / build_tall_grass.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_ladder.png   （tile 78，木梯贴墙贴图；t519 满格版）
  icon_ladder.png      （64×64，hotbar / 创造调色板图标；flat 2D 放大源贴图保留 alpha，同 cross 植物图标）

依赖：仅 PIL，无外部贴图。与 build_sapling.py / build_tall_grass.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16      # 贴图边长（像素）
ICON = 64    # 图标边长（像素；同 build_cube_icons.py 的 OUT）


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    同 build_sapling.py / build_tall_grass.py 的 bleed_alpha（alpha 边缘修复）：见文件头注释。
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

    # 木质配色（与原木 / 树苗同族棕色）：纵轨亮 / 暗面，横梯级中等棕。
    rail_light = (0x9c, 0x73, 0x40, 255)   # 纵轨亮面（右侧）
    rail_dark  = (0x6b, 0x4f, 0x24, 255)   # 纵轨暗面（左侧）
    rung_mid   = (0x82, 0x60, 0x32, 255)   # 横梯级（介于亮 / 暗面，表横向受力件）
    rung_hi    = (0xb0, 0x86, 0x4a, 255)   # 横梯级顶高光（受光边）

    # t519 MC 风格满格梯子贴图：纵轨贴到瓦片两侧（x=2/3 与 x=12/13，2px 宽各）+ 4 道横向梯级满铺轨间，
    #   使贴图「整张读作一把梯子」铺满 cell（机制等价 MC 1.0 ladder 贴图：轨靠边 + rung 满轨间，整张无大块
    #   透明留白）。旧版轨居中 x=4/5,10/11（仅瓦片中央 8/16 宽）→ 单片贴墙 quad 把整张贴图铺满 [0,1] face 时
    #   梯子只显在格中心半宽、两侧大块透明 → 观感「小图标贴墙、粗糙上下宽」（用户 t519 反馈）。
    #   满格版轨靠瓦片边 → 整张贴图铺满 face 后梯子铺满整格宽，读作「贴墙的一把梯子」而非「格中央的小梯图标」。
    # 两根纵轨：左轨 x=2-3（暗面）、右轨 x=12-13（亮面），占满全高 0..15。
    rail_lx, rail_rx = 2, 13
    for y in range(0, TS):
        put(rail_lx,     y, rail_dark)
        put(rail_lx + 1, y, rail_dark)   # 左轨宽 2 像素（暗面）
        put(rail_rx,     y, rail_light)
        put(rail_rx - 1, y, rail_light)  # 右轨宽 2 像素（亮面）

    # 横向梯级：在 y=1 / 5 / 9 / 13 满铺连接两轨（x rail_lx..rail_rx），顶行 +1 像素高光表受光边。
    #   4 道梯级等距分布（间距 4px），覆盖全高 → 顶/底各留 1-2px 给轨端，整张无大块空区。
    for ry in (1, 5, 9, 13):
        for x in range(rail_lx, rail_rx + 1):
            put(x, ry, rung_mid)
        for x in range(rail_lx + 1, rail_rx):  # 梯级上沿高光（跳过与轨重叠的两端像素）
            put(x, ry - 1, rung_hi)

    # alpha bleed：把纵轨 / 梯级颜色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_sapling.py）。
    img = bleed_alpha(img)

    tex_out = os.path.join(SRC, "default_ladder.png")
    img.save(tex_out)
    print("wrote", os.path.relpath(tex_out, HERE), img.size)

    # 图标：flat 2D 放大源贴图保留 alpha（同 build_cube_icons.py render_flat_2d，cross 透明底方块图标路径）。
    icon = img.resize((ICON, ICON), Image.NEAREST)
    icon_out = os.path.join(SRC, "icon_ladder.png")
    icon.save(icon_out)
    print("wrote", os.path.relpath(icon_out, HERE), icon.size)


if __name__ == "__main__":
    main()
