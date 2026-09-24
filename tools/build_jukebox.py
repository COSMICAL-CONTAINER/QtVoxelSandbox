#!/usr/bin/env python3
"""生成唱片机方块的贴图（16×16 像素 ×2，原创自绘，§9 override (a)）。

t1083 唱片机（Jukebox；机制等价 MC 1.0 jukebox——右键放入音乐盘 / 再右键取出 / 播放中吐盘的
木制发声匣）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：木匣体 + 顶面圆盘槽 +
侧面木纹带，读作「会转盘发声的木匣」。

视觉意图：读作「木匣唱盘机」——
  - 顶面（top）：木框拼板面 + 居中圆形深槽（盘仓——音乐盘从这里放进机腹）+ 槽缘一圈亮木
    台阶线（盘嵌进去的凹槽读感）。
  - 侧面（side）：竖板木纹 + 上下两道深色板缝 + 中段一条横向「唱臂搁架」暗带（整机轮廓
    语言与工作台 / 音符盒同族：木质容器 + 功能标记）。
  - 两张同色板（深橡木系，同音符盒 WOOD 族色调），形状语言区分顶 / 侧职能。
  - alpha 恒不透明（255）：整立方六面，无透明语义。
  - 固定坐标布点（非随机 → 重跑逐位可复现，PLAN §2-K 同纪律外溢到资产工具）。

输出（覆盖写入 textures/）：
  default_jukebox_top.png  （tile 189，顶面盘槽）
  default_jukebox_side.png （tile 190，侧面木纹匣体）

依赖：仅 PIL/numpy，无外部贴图。与 build_note_block.py / build_hopper.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 色板：深橡木系（同音符盒框体族）。
WOOD = np.array([96.0, 66.0, 40.0])      # 匣体主面（深橡木）
WOOD_HI = np.array([122.0, 88.0, 54.0])  # 受光棱线 / 槽缘台阶亮线
WOOD_DK = np.array([64.0, 44.0, 26.0])   # 板缝 / 槽内缘阴影
HOLE = np.array([30.0, 20.0, 12.0])      # 盘槽深孔（近黑，机腹开口）
BAND = np.array([74.0, 50.0, 30.0])      # 侧面唱臂搁架暗带


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0] = rgb[0]
        canvas[y, x, 1] = rgb[1]
        canvas[y, x, 2] = rgb[2]
        canvas[y, x, 3] = 255


def wood_base(img):
    """匣体底面：整面深橡木 + 上/左受光棱线 + 四角榫点（两张共用基底）。"""
    for y in range(TS):
        for x in range(TS):
            px(img, x, y, WOOD)
    for i in range(TS):
        px(img, i, 0, WOOD_HI)
        px(img, 0, i, WOOD_HI)
    for cx, cy in ((0, 0), (14, 0), (0, 14), (14, 14)):
        for dy in range(2):
            for dx in range(2):
                px(img, cx + dx, cy + dy, WOOD_DK)


def make_top():
    """顶面：木拼板面 + 板缝十字 + 居中圆盘槽（槽缘亮台阶线 + 深孔）。"""
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    wood_base(img)
    # ① 拼板缝：x=5/10 竖缝 + y=5/10 横缝（读作拼板顶盖，同音符盒框面语言）。
    for g in (5, 10):
        for i in range(TS):
            px(img, g, i, WOOD_DK)
            px(img, i, g, WOOD_DK)
    # ② 盘槽：圆心 (7.5, 7.5)；r≤4.6 深孔 HOLE；r∈(4.6,5.6] 槽缘台阶 WOOD_HI（盘嵌入的凹口亮沿）。
    for y in range(TS):
        for x in range(TS):
            d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            if d <= 4.6:
                px(img, x, y, HOLE)
            elif d <= 5.6:
                px(img, x, y, WOOD_HI)
    # ③ 盘心孔：槽心 1px 中木色亮斑（盘轴读感，打断整片死黑）。
    px(img, 7, 7, WOOD)
    px(img, 8, 7, WOOD)
    px(img, 7, 8, WOOD)
    px(img, 8, 8, WOOD)
    return img


def make_side():
    """侧面：竖板木纹 + 上下板缝 + 中段横向唱臂搁架暗带 + 底沿阴影。"""
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    wood_base(img)
    # ① 竖板缝：x=5/10 两道竖缝（竖拼板读感）。
    for g in (5, 10):
        for i in range(TS):
            px(img, g, i, WOOD_DK)
    # ② 横板缝：y=5 与 y=11 两道（匣体上下围板分界）。
    for i in range(TS):
        px(img, i, 5, WOOD_DK)
        px(img, i, 11, WOOD_DK)
    # ③ 唱臂搁架暗带：y=7..8 横向暗带（中段功能性标记，打断纯板面感）。
    for i in range(TS):
        px(img, i, 7, BAND)
        px(img, i, 8, BAND)
    # ④ 搁架亮沿：y=6 一线受光（带顶高光，同整体顶光源惯例）。
    for i in range(TS):
        px(img, i, 6, WOOD_HI)
    # ⑤ 底沿阴影：最底一行暗线。
    for i in range(TS):
        px(img, i, TS - 1, WOOD_DK)
    return img


def main():
    out_top = make_top()
    out_side = make_side()
    Image.fromarray(out_top.astype(np.uint8), "RGBA").save(
        os.path.join(SRC, "default_jukebox_top.png"))
    Image.fromarray(out_side.astype(np.uint8), "RGBA").save(
        os.path.join(SRC, "default_jukebox_side.png"))
    print("wrote default_jukebox_top.png / default_jukebox_side.png (16x16)")


if __name__ == "__main__":
    main()
