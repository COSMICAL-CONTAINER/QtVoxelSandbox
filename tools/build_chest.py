#!/usr/bin/env python3
"""生成箱子（Chest）方块的顶面 / 侧面 / 前面贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 箱子（玩家右键打开 27 槽物品栏 + 开合盖动画），但贴图为本项目程序生成的
原创像素图，**不**拷贝任何 MC 资产。基底取本工程既有木板（default_wood.png）平均色，
保证与木板族配色一致。

设计风格（t195 重做）：MC 1.0 简洁风。前版贴图叠了多条横向木纹 + 铰链 + 锁印 + 钥匙孔，
观感「像工作台一样太繁」——多条横向暗线拼出网格感，远看与工作台混淆。本次精简到三要素：
  1. 木板底色（纯色，**不**再叠横向木纹横条 —— 那是产生「像工作台」网格感的元凶）；
  2. 暗色边框（1px 木框结构，表箱子木质边框）；
  3. 极简冷调铁件（铰链 / 角箍 / 锁扣）—— 冷调灰与暖色木板框区分，一眼读出「金属件」。

三面分工：
  default_chest_top   —— 顶面：木板底 + 暗框 + 后沿两铰链 + 前沿锁扣顶（俯视锁顶）
  default_chest_side  —— 侧面：木板底 + 暗框 + 水平盖缝（lid/箱体分界）+ 四角铁箍角包
  default_chest_front —— 前面（-Z 锁面）：木板底 + 暗框 + 水平盖缝 + 中央铁锁扣（底座+钥匙孔）

顶面贴图方向约定（与 chunkgeometry per-face UV + BlockCube 一致）：贴图底边 = 箱子前沿
（-Z，锁所在侧），贴图顶边 = 后沿（+Z，铰链所在侧）。故锁俯视画在底边、铰链画在顶边。
竖直面（侧 / 前）：贴图顶边 = 方块顶，贴图底边 = 方块底；盖缝在贴图上 1/3（lid 约占上 1/3）。

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
    """以 base 色纯色铺底（16×16 不透明）。

    极简关键：**不**再叠横向木纹横条。前版在此函数里每隔几行画一条暗线（rows 2/6/11/14）
    模拟木板纹理，但多条横向暗线在 16×16 上拼出网格感，远看与工作台（2×2 网格 + 工具）混淆，
    被判「像工作台一样太繁」。箱子结构由暗框 + 铁件提供，底色保持纯色即可清晰可辨为木箱。
    """
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = base
    c[..., 3] = 255.0
    return c


def iron_color(base):
    """冷调金属灰（铁件色）。

    取木板灰度后降亮 + 微冷偏蓝（B 通道略高于 R/G），与暖色木板框（base*0.52，暖棕）区分，
    一眼读出「这是金属件」而非「又一处暗木」。冷/暖对照是简洁风中表「不同材质」的廉价手段。
    """
    gray = float(base.mean())
    return np.array([gray * 0.50, gray * 0.50, gray * 0.58], dtype=np.float64)


def draw_frame(c, color):
    """1px 暗色边框（箱子木质边框结构，绕整面一周）。"""
    c[0, :, 0:3] = color
    c[TS - 1, :, 0:3] = color
    c[:, 0, 0:3] = color
    c[:, TS - 1, 0:3] = color


def fill_block(c, x0, x1, y0, y1, color):
    """在 [x0,x1) × [y0,y1) 矩形填 color（闭开区间，便于传 2×2/3×2 等小块）。"""
    c[y0:y1, x0:x1, 0:3] = color


def draw_top(base):
    """顶面 = 木板底 + 暗框 + 后沿两铰链 + 前沿锁扣顶（俯视锁顶）。

    俯视一眼即辨「带盖木箱」：后沿两个小铁铰链（盖子在此处开合）+ 前沿中央铁块（锁的顶面）。
    中段纯木板底，不叠任何纹理（极简）。
    """
    c = new_canvas(base)
    iron = iron_color(base)
    draw_frame(c, base * 0.52)            # 暗框（木框结构）
    # 后沿铰链（贴图顶边 = +Z 后沿，盖子铰链所在侧）：左右两块小铁件（cols 2..5 与 11..14，rows 1..3）。
    fill_block(c, 2, 5, 1, 3, iron)
    fill_block(c, 11, 14, 1, 3, iron)
    # 前沿锁扣顶（贴图底边 = -Z 前沿，锁正上方俯视）：中央铁块（cols 6..10, rows 13..15）。
    fill_block(c, 6, 10, 13, 15, iron)
    return c


def draw_side(base):
    """侧面 = 木板底 + 暗框 + 水平盖缝（lid/箱体分界）+ 四角铁箍角包。

    铁箍角包（四角 2×2 铁块）= 箱子四角被金属箍包住，表「铁箍木箱」语义（用户诉求「铁箍锁扣」）。
    水平盖缝在 row 5（贴图上 ~1/3），与前面盖缝同高，环绕箱子一圈表 lid 与箱体分界。
    """
    c = new_canvas(base)
    iron = iron_color(base)
    draw_frame(c, base * 0.52)            # 暗框
    seam = base * 0.42                    # 盖缝暗线（暖棕，比框更深）
    c[5, :, 0:3] = seam                   # 水平盖缝（贯穿整行）
    # 四角铁箍角包（2×2）：(1,1) / (13,1) / (1,13) / (13,13)，紧贴暗框内侧。
    for (x0, y0) in ((1, 1), (TS - 3, 1), (1, TS - 3), (TS - 3, TS - 3)):
        fill_block(c, x0, x0 + 2, y0, y0 + 2, iron)
    return c


def draw_front(base):
    """前面（-Z 锁面）= 木板底 + 暗框 + 水平盖缝 + 中央铁锁扣（底座 + 钥匙孔）。

    锁扣是箱子最不可误认的标志：从盖缝（rows 5..6）向下垂的铁底座（rows 5..11, cols 6..10）
    + 中央钥匙孔暗点。机制等价 MC 1.0 箱子正面锁扣挂在 lid 与箱体接缝正中。
    """
    c = new_canvas(base)
    iron = iron_color(base)
    iron_dark = base * 0.16               # 钥匙孔（近黑，最暗）
    draw_frame(c, base * 0.52)            # 暗框
    seam = base * 0.42
    # 水平盖缝（rows 5..6）：lid（上）与箱体（下）分界。
    c[5, :, 0:3] = seam
    c[6, :, 0:3] = seam
    # 中央铁锁扣底座：从盖缝向下垂（rows 5..11, cols 6..10）—— 挂在缝上的锁扣。
    fill_block(c, 6, 10, 5, 12, iron)
    # 钥匙孔（锁扣中央暗点，rows 8..10, cols 7..9）。
    fill_block(c, 7, 9, 8, 11, iron_dark)
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
