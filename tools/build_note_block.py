#!/usr/bin/env python3
"""生成音符盒方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t1028 音符盒（NoteBlock；机制等价 MC 1.0 note block——右键调音 / 攻击·红石触发发声的木制乐器方块）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：深木框体 + 居中圆形「鼓膜/喇叭盆」 +
中心小音符标记，读作「会发声的木盒」。

视觉意图：读作「木框发声盒」——
  - 框体：四边 3px 深木色框（木板拼缝 + 角部榫点），与工作台 / 箱子同「木质容器」家族观感。
  - 盆面：居中 10px 圆形浅木色盆膜（同心圆两档明暗 → 鼓膜/共振盆的立体感）。
  - 音符：盆心一枚 2×4 原创小音符（符头菱形 + 符干）——「这是个乐器」的一眼语义（纯原创点阵，
    非任何现有 logo/字形）。
  - alpha 恒不透明（255）：整立方六面同贴图，无透明语义。

输出（覆盖写入 textures/）：
  default_note_block.png  （tile 184，音符盒各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_iron_bars.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 色板：深木框（主 / 亮棱 / 拼缝暗线）/ 盆膜（浅木亮 / 中 / 边缘暗）/ 音符（近黑棕）。
WOOD = np.array([96.0, 66.0, 40.0])      # 框体主色（深橡木）
WOOD_HI = np.array([122.0, 88.0, 54.0])  # 框体上/左受光棱线
WOOD_DK = np.array([64.0, 44.0, 26.0])   # 板缝 / 框内缘阴影
MEMB = np.array([196.0, 168.0, 122.0])   # 盆膜主体（浅木鼓膜）
MEMB_IN = np.array([214.0, 188.0, 142.0])  # 盆膜内圈高光
MEMB_ED = np.array([150.0, 122.0, 82.0])   # 盆膜外缘（沉降暗环）
NOTE = np.array([52.0, 36.0, 22.0])      # 音符标记（近黑棕，框族内最深）


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0] = rgb[0]
        canvas[y, x, 1] = rgb[1]
        canvas[y, x, 2] = rgb[2]
        canvas[y, x, 3] = 255


def main():
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    # ① 框体底：整面深木色。
    for y in range(TS):
        for x in range(TS):
            px(img, x, y, WOOD)
    # ② 板缝：x=0/5/10/15 竖缝 + y=0/5/10/15 横缝 → 框面读作「木板拼装」；缝色走暗线。
    for g in (0, 5, 10, 15):
        for i in range(TS):
            px(img, g, i, WOOD_DK)
            px(img, i, g, WOOD_DK)
    # ③ 受光棱线：框外沿上 / 左两线提亮（顶光源惯例，同 build_iron_bars 立体口径）。
    for i in range(TS):
        px(img, i, 0, WOOD_HI)
        px(img, 0, i, WOOD_HI)
    # ④ 角部榫点：四角 2×2 暗点（木盒榫卯语义 + 打断纯网格感）。
    for cx, cy in ((0, 0), (14, 0), (0, 14), (14, 14)):
        for dy in range(2):
            for dx in range(2):
                px(img, cx + dx, cy + dy, WOOD_DK)
    # ⑤ 盆膜：圆心 (7.5, 7.5) 半径 5 的圆 → MEMB；r ≤ 3 内圈 MEMB_IN；r ≥ 4.4 外缘 MEMB_ED。
    for y in range(TS):
        for x in range(TS):
            d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            if d <= 5.0:
                px(img, x, y, MEMB)
                if d <= 3.0:
                    px(img, x, y, MEMB_IN)
                elif d >= 4.4:
                    px(img, x, y, MEMB_ED)
    # ⑥ 音符标记（原创点阵，盆心居中）：符头 3×2 菱块 + 右侧 1×5 符干 + 符干顶 2×1 符尾横旗。
    #    布局（x 5..10 / y 4..11）在 10px 盆膜内居中；点阵手排，与任何现存字形无涉。
    for dx, dy in ((6, 9), (7, 9), (5, 10), (6, 10), (7, 10), (6, 11)):  # 符头（斜置椭圆块）
        px(img, dx, dy, NOTE)
    for dy in range(4, 10):                                             # 符干
        px(img, 8, dy, NOTE)
    for dx in (9, 10):                                                  # 符尾
        px(img, dx, 4, NOTE)
    px(img, 10, 5, NOTE)
    px(img, 9, 5, NOTE)
    # ⑦ alpha 恒不透明（⑥/①..⑤ 已全部显式 255；此处兜底统一）。
    img[:, :, 3] = 255
    out = Image.fromarray(img.astype(np.uint8), "RGBA")
    out.save(os.path.join(SRC, "default_note_block.png"))
    print(f"wrote {os.path.join(SRC, 'default_note_block.png')} ({TS}x{TS})")


if __name__ == "__main__":
    main()
