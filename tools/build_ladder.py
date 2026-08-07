#!/usr/bin/env python3
"""生成木梯（Ladder）cross 贴图 + 图标（16×16 瓦片 + 64×64 图标，原创自绘，§9 override (a)）。

t413 垂直爬梯（vertical climb ladder）：木梯是 cross 形广告牌方块（与草丛 / 树苗同走
PartialBlockGeometry 的 cross 几何段，两片对角相交双面 quad），机制等价 MC 1.0 梯子（ladder）——
玩家走进梯格 + 按前即逐格向上爬（竖井用）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「一把梯子」—— 透明底（alpha=0）+ 两根棕色纵轨（左右各一）+ 数根横向梯级
连接两轨。mesher 把本瓦片贴到 cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5 丢弃
透明底 → 仅梯的纵轨 / 横级像素显（机制等价 MC cutout 梯子）。透明底是关键：若无 alpha（实心底），
cross 会显成两片实心板（非梯子）。

alpha bleed（同 build_sapling.py / build_tall_grass.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_ladder.png   （tile 78，木梯 cross 贴图）
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

    # 两根纵轨：左轨 x=4（暗面）、右轨 x=11（亮面），占满全高 0..15。
    rail_lx, rail_rx = 4, 11
    for y in range(0, TS):
        put(rail_lx, y, rail_dark)
        put(rail_lx + 1, y, rail_dark)   # 轨宽 2 像素（暗面）
        put(rail_rx, y, rail_light)
        put(rail_rx - 1, y, rail_light)  # 轨宽 2 像素（亮面）

    # 横向梯级：在 y=2 / 6 / 10 / 14 连接两轨（x rail_lx..rail_rx），顶行 +1 像素高光表受光边。
    for ry in (2, 6, 10, 14):
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
