#!/usr/bin/env python3
"""生成漏斗方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t1080 漏斗（Hopper；机制等价 MC 1.5+ hopper 的机制四语义——收集 / 抽取 / 输出 / 红石锁停）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：暗铁锅体 + 顶面进料箅 + 侧面排料嘴，
读作「给料漏斗机械件」。

视觉意图：读作「金属给料漏斗」——
  - 锅体（side）：暗铁灰底 + 边框暗带 + 两道箍带 + 四角铆钉（同铁栏杆 / 铁块金属族语言）。
  - 顶箅（top）：暗铁框 + 中央栅格孔阵（进料口读感——掉落物从这里被收进容腔）。
  - 排料嘴（front）：锅体 + 中央小圆孔 + 短出料槽（排料口读感——标记输出所朝方向，
    mesher 据漏斗 state 贴到排料口所朝面，同发射器 / 投掷器「前面标记输出方向」模式）。
  - 三张同色板（暗铁系），形状语言区分三面职能；alpha 恒不透明（整立方无透明语义）。
  - 固定坐标布点（非随机 → 重跑逐位可复现，PLAN §2-K 同纪律外溢到资产工具）。

输出（覆盖写入 textures/）：
  default_hopper_top.png   （tile 186，顶面进料箅）
  default_hopper_side.png  （tile 187，锅体侧面/底面）
  default_hopper_front.png （tile 188，排料口面）

依赖：仅 PIL/numpy，无外部贴图。与 build_bone_block.py / build_dispenser.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 色板：暗铁系（同铁块 / 铁砧金属族）。
IRON = np.array([126.0, 130.0, 136.0])     # 铁灰主面
IRON_HI = np.array([148.0, 152.0, 158.0])  # 受光 / 箍带亮线
IRON_DK = np.array([88.0, 91.0, 97.0])     # 边框暗带 / 孔缘
IRON_DKR = np.array([56.0, 59.0, 64.0])    # 栅格孔 / 排料孔（近黑深孔）
RIVET = np.array([164.0, 168.0, 174.0])    # 铆钉亮点


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0] = rgb[0]
        canvas[y, x, 1] = rgb[1]
        canvas[y, x, 2] = rgb[2]
        canvas[y, x, 3] = 255


def base_plate(img):
    """锅体底面：整面铁灰 + 顶两行受光 / 底一行暗角 + 四角铆钉（三张共用基底）。"""
    for y in range(TS):
        for x in range(TS):
            base = IRON
            if y <= 1:
                base = IRON_HI
            elif y == TS - 1:
                base = IRON_DK
            px(img, x, y, base)
    for (x, y) in [(1, 1), (14, 1), (1, 14), (14, 14)]:
        px(img, x, y, RIVET)


def side():
    """锅体（side / 底面）：底面 + 边框暗带 + 两道横向箍带（亮线 + 影线）。"""
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    base_plate(img)
    for x in range(TS):
        px(img, x, 0, IRON_DK)
        px(img, x, 15, IRON_DK)
    for x in range(TS):
        if x == 0 or x == 15:
            px(img, x, 5, IRON_DK)
            px(img, x, 10, IRON_DK)
            continue
        px(img, x, 5, IRON_HI)   # 箍带亮线
        px(img, x, 6, IRON_DK)   # 箍带影线
        px(img, x, 10, IRON_HI)
        px(img, x, 11, IRON_DK)
    return img


def top():
    """顶箅（top）：底面 + 边框 + 中央 3×3 栅格孔阵（进料口读感）。"""
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    base_plate(img)
    for x in range(TS):
        px(img, x, 0, IRON_DK)
        px(img, x, 15, IRON_DK)
        px(img, 0, x, IRON_DK)
        px(img, 15, x, IRON_DK)
    # 中央栅格孔阵：3×3 孔（2px 孔 + 1px 隔条），孔缘一圈暗缘。
    for gy in range(3):
        for gx in range(3):
            ox = 4 + gx * 3
            oy = 4 + gy * 3
            for dy in range(2):
                for dx in range(2):
                    px(img, ox + dx, oy + dy, IRON_DKR)
    return img


def front():
    """排料嘴（front）：锅体 + 中央 4×4 深孔 + 孔下短出料槽（排料口读感，标记输出方向）。"""
    img = side()
    # 中央深孔（4×4，近黑）+ 孔缘暗环。
    for dy in range(6):
        for dx in range(6):
            x, y = 5 + dx, 4 + dy
            if dx == 0 or dx == 5 or dy == 0 or dy == 5:
                px(img, x, y, IRON_DK)
            else:
                px(img, x, y, IRON_DKR)
    # 孔下短出料槽（两道竖亮线 + 底部开口），向下导料读感。
    for y in range(11, 15):
        px(img, 6, y, IRON_HI)
        px(img, 9, y, IRON_HI)
    px(img, 7, 14, IRON_DKR)
    px(img, 8, 14, IRON_DKR)
    return img


def main():
    for name, img in [("default_hopper_top", top()),
                      ("default_hopper_side", side()),
                      ("default_hopper_front", front())]:
        out = Image.fromarray(img.astype(np.uint8), "RGBA")
        out_path = os.path.join(SRC, name + ".png")
        out.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), out.size)


if __name__ == "__main__":
    main()
