#!/usr/bin/env python3
"""生成星空贴图（256×256，原创程序生成，§9 override (a)）。

机制等价 MC 1.0 夜空星点（散布在透明天穹上的小白 / 蓝亮点），但贴图为本项目程序生成的原创
像素图，**不**拷贝任何 MC 资产。星的「显隐 / 夜间淡入」由呈现层（Main.qml 的天穹 Model opacity）
实现；本脚本只产出一份透明背景 + 散布星点的贴图，铺到一张绕相机的天穹球上。

天穹几何（SkyDome）：UV 球，u 绕赤道一圈（纹理左右对接）、v 由南极到北极。球的两极 UV 收 pin，
星点贴在极附近 (v∈[0,0.05]∪[0.95,1]) 会被挤压成一团 → 故本脚本避开极区布星，星只在
v∈[0.08,0.92] 散布（天穹侧带，恰是夜空中抬头最常看到的区域）。

视觉意图：读作「自然星空」——大小不一的亮星（少数较大较亮、多数细小）散布，颜色偏白带
少量冷蓝 / 暖黄点缀（非纯白麻点），密度适中（抬头仰望不糊屏、不空）。固定随机种子 → 确定性
（同 CI 同图）。纹理**左右可无缝对接**（u=0 与 u=1 接缝处星点对齐）：每颗星同时在镜像 u 位置
复制一份，使天穹绕一圈接缝无可见裂缝。

依赖：仅 numpy/PIL，无外部贴图。
"""
import os
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 256            # 贴图边长（像素；星点小，256 足够细节又不爆显存）
N_STARS = 170       # 星数（密度适中：抬头不空、不糊屏）
V_MIN, V_MAX = 0.08, 0.92   # 避开球极 pinch 区（仅天穹侧带布星）

# 固定随机种子 → 确定性（同 CI 同图；§9 自绘原创）。
RNG = np.random.default_rng(20260807)

# 星色：多数白、少量冷蓝、少量暖黄（非纯白麻点）。
STAR_COLORS = [
    (255, 255, 255),
    (255, 255, 255),
    (255, 255, 255),
    (210, 226, 255),   # 冷蓝白
    (255, 244, 214),   # 暖黄白
]


def draw_stars():
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    def stamp(u, v, r, col, bri):
        """在 (u,v)∈[0,1] 处盖一颗半径 r 的星，亮度 bri∈[0,1] 调 alpha。"""
        px = u * TS
        py = v * TS
        a = int(255 * bri)
        if r <= 1.0:
            # 单像素星：直接画点（含微亮邻像素模拟辉光）。
            draw.point((px, py), fill=(col[0], col[1], col[2], a))
        else:
            # 多像素星：核心实心 + 外圈半透软边。
            x0, y0 = px - r, py - r
            x1, y1 = px + r, py + r
            draw.ellipse((x0, y0, x1, y1), fill=(col[0], col[1], col[2], a))
            # 辉光环（更暗的稍大圆）。
            draw.ellipse((x0 - 1, y0 - 1, x1 + 1, y1 + 1),
                         fill=(col[0], col[1], col[2], int(a * 0.30)))

    for _ in range(N_STARS):
        u = RNG.random()
        v = V_MIN + RNG.random() * (V_MAX - V_MIN)
        # 半径分布：大多 0.6..1.2（细小），少数 1.3..2.2（亮星）。
        if RNG.random() < 0.18:
            r = 1.3 + RNG.random() * 0.9
            bri = 0.75 + RNG.random() * 0.25
        else:
            r = 0.6 + RNG.random() * 0.6
            bri = 0.45 + RNG.random() * 0.45
        col = STAR_COLORS[int(RNG.random() * len(STAR_COLORS))]
        stamp(u, v, r, col, bri)
        # 左右无缝对接：在镜像 u（u±1 取模）处再盖同一颗星，使 u=0/1 接缝处不出现半颗星裂缝。
        stamp(u + 1.0, v, r, col, bri)
        stamp(u - 1.0, v, r, col, bri)

    return np.array(img)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_stars(), "stars")


if __name__ == "__main__":
    main()
