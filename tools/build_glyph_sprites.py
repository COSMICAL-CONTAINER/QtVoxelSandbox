#!/usr/bin/env python3
"""生成符文字形图集 glyphs.png（64×64，4×4 格 × 16px/格，原创程序生成，§9 override (a)）。

t765 书架→附魔台「文字/符文粒子」的贴图源：16 个原创符文字形拼成 4×4 图集，QML 粒子经
Texture.scaleU/V=0.25 + positionU/V 选格（机制等价 MC 1.0 附魔台 glyph 粒子的未知字符图集，
但字形为本项目程序生成的原创笔画，**不**拷贝任何 MC 资产）。

设计要点：
- 透明底 + 近白 (#f0eeff) 笔画 → 材质 baseColor 染色相乘后即得各色符文（紫 / 青 / 白系调色）。
- 字形 = 3×3 点阵（格内像素坐标 {3,8,13}²）上的竖笔 + 斜笔 + 点饰，角形笔画读作「符文 /
  古字符」而非涂鸦；每格 3-5 笔，随机但固定种子 → 确定性（同 CI 同图）。
- 笔画全部收在 [3,13]² 内 → 距格边 ≥2px 透明边距：QML 按 0.25 子区采样时 Linear 过滤的
  边缘纹素混色只发生在透明边圈，字形永不渗色到邻格（图集防渗色，半纹素内缩同族原则）。

依赖：仅 PIL。
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
CELL = 16          # 单字形格边长（px）
GRID = 4           # 4×4 图集 → 16 个字形
TS = CELL * GRID   # 64
INK = (240, 238, 255, 255)   # 近白笔画（baseColor 相乘染色）

# 固定随机种子 → 确定性（同 CI 同图；§9 自绘原创）。t765 专用种子。
import random
RNG = random.Random(20260822)

LATTICE = [3, 8, 13]   # 3×3 点阵的像素坐标（[3,13] 收拢 → 格边 ≥2px 透明边距防渗色）


def draw_glyph(draw, ox, oy):
    """在 (ox,oy) 起的 16px 格内画一个原创符文：1-2 竖笔 + 2-3 斜/横笔 + 0-1 点饰。"""
    pts = [(x, y) for x in LATTICE for y in LATTICE]
    used = set()

    def seg(a, b):
        if a == b:
            return
        key = tuple(sorted([a, b]))
        if key in used:
            return
        used.add(key)
        draw.line([(ox + a[0], oy + a[1]), (ox + b[0], oy + b[1])], fill=INK, width=1)

    # 1-2 根竖笔（符文骨架：runic 竖柱观感的主干）。
    stems = 1 if RNG.random() < 0.6 else 2
    stem_xs = RNG.sample(LATTICE, stems)
    for sx in stem_xs:
        y0, y1 = sorted(RNG.sample(LATTICE, 2))
        seg((sx, y0), (sx, y1))
        # 从竖笔中点伸出的枝（斜笔为主 → 角形字符感）。
        mid = (sx, y0 if RNG.random() < 0.5 else y1)
        bx = RNG.choice([v for v in LATTICE if v != sx])
        by = RNG.choice(LATTICE)
        seg(mid, (bx, by))
    # 1-2 根自由斜/横笔（补足 3-5 笔的字形密度）。
    for _ in range(1 + int(RNG.random() * 2)):
        seg(RNG.choice(pts), RNG.choice(pts))
    # 0-1 个点饰（2×2 短点，古字符的间隔点读感；仍收在 [3,12] 内）。
    if RNG.random() < 0.7:
        dx, dy = RNG.choice(LATTICE), RNG.choice(LATTICE)
        draw.rectangle([ox + dx - 1, oy + dy - 1, ox + dx, oy + dy], fill=INK)
    return len(used)


def main():
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    strokes = []
    for row in range(GRID):
        for col in range(GRID):
            strokes.append(draw_glyph(draw, col * CELL, row * CELL))
    # 自检：每格至少 1 笔（空格会在图集里留洞 → QML 随机选到空白字形 = 隐形粒子）。
    assert all(s >= 1 for s in strokes), "empty glyph cell"
    # 自检：格边 2px 边圈全透明（防渗色前提，随生成器一起锁死防回归）。
    px = img.load()
    for row in range(GRID):
        for col in range(GRID):
            for e in range(CELL):
                for (ex, ey) in ((e, 0), (e, 1), (e, CELL - 2), (e, CELL - 1),
                                 (0, e), (1, e), (CELL - 2, e), (CELL - 1, e)):
                    assert px[col * CELL + ex, row * CELL + ey][3] == 0, "glyph ink in bleed margin"
    out = os.path.join(SRC, "glyphs.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size, "strokes/cell:", strokes)


if __name__ == "__main__":
    main()
