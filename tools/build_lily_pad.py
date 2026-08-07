#!/usr/bin/env python3
"""生成睡莲（LilyPad）cross 路由的横向浮叶贴图（16×16 像素，原创自绘，§9 override (a)）。

t396 沼泽群系内容：睡莲是沼泽浅水水面的浮叶方块（mesher 经 PartialBlockGeometry::append 的 LilyPad
case 画一片水平双面 quad 贴 cell 底部 → 浮于水面）。机制等价 MC 1.0 lily pad（沼泽浅水水面圆叶）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

视觉意图：读作「一片浮水圆叶」—— 透明底（alpha=0）+ 一片绿色圆 / 椭圆叶（带经典 V 形缺口，
拟睡莲叶的叶柄缺口）。mesher 把本瓦片贴到水平 quad；chunk 地形材质 alphaCutoff:0.5 丢弃透明底
→ 仅圆叶像素显（机制等价 MC cutout 睡莲）。透明底是关键：若无 alpha，浮叶会显成整张实心板。

alpha bleed（同 build_dead_bush.py / build_sapling.py）：把透明像素的 RGB 用最近不透明邻居颜色填上
（alpha 保持 0）→ 线性过滤边缘不再产生黑边晕。

输出（覆盖写入 textures/）：
  default_lily_pad.png   （tile 61，睡莲浮叶贴图）

依赖：仅 PIL，无外部贴图。与 build_sapling.py / build_dead_bush.py 同风格（程序生成原创像素图）。
"""
import os
import math
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

    # 叶配色（沼泽睡莲深绿，偏暗黄绿表「水面浮叶」）：亮面 / 暗面 + 叶脉。
    leaf_light = (0x4f, 0x8a, 0x2f, 255)  # 黄绿亮面
    leaf_dark = (0x2f, 0x5a, 0x18, 255)   # 深绿暗面
    vein = (0x6a, 0xa8, 0x3a, 255)        # 叶脉亮线
    edge = (0x26, 0x47, 0x12, 255)        # 叶缘暗化

    def put(x, y, color):
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = color

    # 圆叶：以 (7.5, 7.5) 为中心、半径 ~7 的圆盘，向右下开一个 V 形缺口（叶柄缺口）。
    cx, cy = 7.5, 7.5
    R = 6.8
    for y in range(TS):
        for x in range(TS):
            dx = x - cx
            dy = y - cy
            d = math.sqrt(dx * dx + dy * dy)
            if d > R:
                continue
            # V 形缺口：第一象限（dx>0, dy<0，即右上）靠近 +X 轴方向开一个扇形缺口（叶柄入口）。
            #   缺口角约从 -10° 到 +20°（以 +X 轴为 0°，y 向下故 -atan2(dy,dx)）。
            if dx > 0:
                ang = math.degrees(math.atan2(-dy, dx))  # 屏幕坐标 y 向下 → 反号取数学角
                if -12.0 <= ang <= 22.0 and d > 2.2:
                    continue  # 缺口（透明）
            # 像素填色：距中心略远（叶缘）暗化；叶面亮 / 暗按象限分（受光面亮、背光面暗）。
            is_edge = d > R - 1.2
            # 右下半（dy>0）亮面、左上半（dy<0）暗面，给立体感。
            base = leaf_light if dy > -0.5 else leaf_dark
            color = edge if is_edge else base
            put(x, y, color)

    # 叶脉：从缺口根部（圆心偏右上）放射几条亮线（拟睡莲放射叶脉）。
    for ang_deg in (-50.0, -20.0, 20.0, 60.0, 110.0, 160.0):
        ang = math.radians(ang_deg)
        for r in range(1, 6):
            vx = int(round(cx + math.cos(ang) * r))
            vy = int(round(cy - math.sin(ang) * r))
            put(vx, vy, vein)

    # alpha bleed：把叶色渗进透明底（alpha=0 不变），消除 cutout 黑边（同 build_sapling.py）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_lily_pad.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
