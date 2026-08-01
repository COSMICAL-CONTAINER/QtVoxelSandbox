#!/usr/bin/env python3
"""生成箱子（Chest）方块的顶面 / 侧面 / 前面贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 箱子（玩家右键打开 27 槽物品栏 + 开合盖动画），但贴图为本项目程序生成的
原创像素图，**不**拷贝任何 MC 资产。基底取本工程既有木板（default_wood.png）平均色，
保证与木板族配色一致；叠加暗色铁箍带 + 锁孔 + 盖缝刻线表「带锁木箱」语义。

输出（覆盖写入 textures/）：
  default_chest_top.png   —— 顶面：木板底 + 横向盖缝（lid 与箱体分界）+ 后沿两条铰链 + 前沿锁孔暗印
  default_chest_side.png  —— 侧面：木板底 + 上下两条铁箍带（顶 / 底沿）+ 中段竖向木板缝
  default_chest_front.png —— 前面（-Z，玩家面对的锁面）：木板底 + 横向盖缝（上 1/3）+ 中央锁孔

顶面贴图方向约定（与 chunkgeometry per-face UV + BlockCube 一致）：贴图底边 = 箱子前沿
（-Z，锁所在侧），贴图顶边 = 后沿（+Z，铰链所在侧）。故锁孔暗印画在底边、铰链画在顶边。

可复现：同输入（无随机源）→ 同输出（确定性，便于 CI 校验）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def plank_base():
    """读既有木板贴图，取不透明像素平均色作箱子底色（与木板族配色一致）。"""
    p = os.path.join(SRC, "default_wood.png")
    img = Image.open(p).convert("RGBA").resize((TS, TS), Image.NEAREST)
    arr = np.asarray(img, dtype=np.float64)
    opaque = arr[..., 3] >= 128
    base = arr[opaque][:, 0:3].mean(axis=0) if opaque.any() else np.array([160.0, 120.0, 60.0])
    return base  # (3,) RGB


def new_canvas(base):
    """以 base 色 + 木纹横条铺底（16×16 不透明）。"""
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = base
    c[..., 3] = 255.0
    # 木纹横条：每隔几行略微变暗，模拟木板纹理（原创，非 MC 资产）。
    grain = base * 0.88
    for y in (2, 6, 11, 14):
        c[y, :, 0:3] = grain
    return c


def draw_top(base):
    """顶面 = 木板底 + 横向盖缝（中段水平刻线，表 lid 与箱体分界）+ 后沿两条铰链 + 前沿锁孔暗印。"""
    c = new_canvas(base)
    seam = base * 0.50      # 盖缝暗线（lid / 箱体分界）
    hinge = base * 0.40     # 铰链更暗（金属感深棕）
    lock_mark = base * 0.35 # 锁孔暗印
    # 横向盖缝：贴图中段（rows 7..8）一条横穿暗线 —— 表「盖子合上时的缝」。
    for y in (7, 8):
        c[y, :, 0:3] = seam
    # 后沿铰链（贴图顶边 = +Z 后沿）：左右两块小铰链（rows 0..2, cols 2..4 与 11..13）。
    for (x0, y0) in ((2, 0), (11, 0)):
        for y in range(y0, y0 + 3):
            for x in range(x0, x0 + 3):
                c[y, x, 0:3] = hinge
    # 前沿锁孔暗印（贴图底边 = -Z 前沿）：中下小暗块（rows 12..14, cols 7..9）—— 俯视锁的顶部。
    for y in range(12, 15):
        for x in range(7, 10):
            c[y, x, 0:3] = lock_mark
    return c


def draw_side(base):
    """侧面 = 木板底 + 上下两条铁箍带（顶 / 底沿，表金属加固）+ 中段竖向木板缝。"""
    c = new_canvas(base)
    band = base * 0.45      # 铁箍带暗色（金属感）
    split = base * 0.55     # 竖向板缝
    # 上下两条铁箍带（rows 0..1 顶沿 + rows 14..15 底沿）—— 表箱子上下被金属带箍住。
    for y in (0, 1, 14, 15):
        c[y, :, 0:3] = band
    # 中段竖向木板缝（cols 5 / 10）—— 表侧面由多块木板拼成。
    for x in (5, 10):
        c[:, x, 0:3] = split
    # 盖缝水平线（rows 7..8）与顶面 / 前面同源 —— 侧面也能看到 lid 与箱体的分界缝。
    for y in (7, 8):
        c[y, :, 0:3] = split * 0.9
    return c


def draw_front(base):
    """前面（-Z 锁面）= 木板底 + 横向盖缝（上 1/3）+ 中央锁孔（金属底座 + 钥匙孔）。"""
    c = new_canvas(base)
    seam = base * 0.50      # 盖缝
    plate = base * 0.42     # 锁底座（深棕金属）
    keyhole = base * 0.22   # 钥匙孔（最暗）
    # 横向盖缝（rows 5..6）—— 表「盖子合上时上半为 lid、下半为箱体」的分界。
    for y in (5, 6):
        c[y, :, 0:3] = seam
    # 中央锁底座（rows 8..12, cols 6..9）—— 矩形金属底座。
    for y in range(8, 13):
        for x in range(6, 10):
            c[y, x, 0:3] = plate
    # 钥匙孔（锁底座中央，rows 9..11, col 7..8）—— 圆形钥匙孔暗点。
    for y in range(9, 12):
        for x in range(7, 9):
            c[y, x, 0:3] = keyhole
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    base = plank_base()
    save(draw_top(base), "default_chest_top")
    save(draw_side(base), "default_chest_side")
    save(draw_front(base), "default_chest_front")


if __name__ == "__main__":
    main()
