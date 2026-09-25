#!/usr/bin/env python3
"""生成酿造台方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t1097 酿造台（BrewingStand；机制等价 MC 1.0 brewing stand——右键开酿造 UI / 燃料驱动酿造 / 瓶原位
变换）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：石底座 + 中柱 + 双臂的「炼金蒸馏台」
读感，双态贴图（idle 静置 / lit 酿造中，lit 由 state bit0 分派——熔炉 frontTile 承载亮态同门）。

视觉意图：读作「炼金蒸馏台」——
  - idle：石灰底座板纹 + 中柱石纹 + 双臂横杆暗槽（三瓶位读感：横杆两端点暗槽 = 左右瓶 / 柱顶 = 顶瓶）。
  - lit：底座提亮 + 柱身中段亮橙「炉心」辉光带 + 双臂端点亮斑（酿造进行中的发热读感）。
  - 固定坐标布点（非随机 → 重跑逐位可复现，PLAN §2-K 同纪律外溢到资产工具）。

输出（覆盖写入 textures/）：
  default_brewing_stand.png      （tile 193，静置态）
  default_brewing_stand_lit.png  （tile 194，酿造进行态）

依赖：仅 PIL/numpy，无外部贴图。与 build_hopper.py / build_repeater.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 色板：石灰石系底座（同圆石 / 酿造台机械族）+ 炉心暖光。
STONE = np.array([118.0, 118.0, 116.0])     # 石灰主面
STONE_HI = np.array([142.0, 142.0, 138.0])  # 受光亮缘
STONE_DK = np.array([86.0, 86.0, 84.0])     # 边框暗带 / 槽缘
STONE_DKR = np.array([58.0, 58.0, 57.0])    # 深槽 / 瓶位暗点
EMBER = np.array([232.0, 122.0, 36.0])      # 炉心亮橙（lit 态）
EMBER_HI = np.array([250.0, 190.0, 70.0])   # 辉光核心亮黄（lit 态）


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0] = rgb[0]
        canvas[y, x, 1] = rgb[1]
        canvas[y, x, 2] = rgb[2]
        canvas[y, x, 3] = 255


def body(lit):
    """共享底纹：石灰面 + 顶缘亮 / 底缘暗 + 边框；lit 态整体微提亮。"""
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    for y in range(TS):
        for x in range(TS):
            base = STONE
            if y <= 1:
                base = STONE_HI
            elif y == TS - 1:
                base = STONE_DK
            elif lit and 6 <= y <= 9:
                base = STONE_HI  # lit 态柱身中段基底提亮（承辉光带）
            px(img, x, y, base)
    for i in range(TS):
        px(img, i, 0, STONE_DK)
        px(img, i, 15, STONE_DK)
        px(img, 0, i, STONE_DK)
        px(img, 15, i, STONE_DK)
    return img


def idle():
    """静置态：底座板纹 + 中柱窄幅 + 双臂横杆暗槽 + 三瓶位暗点。"""
    img = body(False)
    # 中柱窄幅（6..9 列 × 3..12 行）：竖条柱身 + 左右缘影线（柱体读感）。
    for y in range(3, 13):
        for x in range(6, 10):
            c = STONE_HI if x in (6, 7) else STONE_DK
            px(img, x, y, c)
        px(img, 7, y, STONE_HI)
        px(img, 8, y, STONE_DKR)
    # 双臂横杆（3..12 列 × 6..7 行）：横杆体 + 槽缘。
    for x in range(3, 13):
        px(img, x, 6, STONE_DK)
        px(img, x, 7, STONE_HI)
    # 三瓶位暗点（横杆两端 + 柱顶）：深槽点 = 摆瓶位读感。
    for (x, y) in [(3, 5), (12, 5), (7, 2), (8, 2)]:
        px(img, x, y, STONE_DKR)
    # 底座板纹（1..14 行两道水平凿线）。
    for x in range(1, 15):
        px(img, x, 11, STONE_DK)
        px(img, x, 12, STONE_HI)
    return img


def lit():
    """酿造态：底纹提亮 + 柱身炉心辉光带 + 双臂端点亮斑（发热读感）。"""
    img = body(True)
    for y in range(3, 13):
        px(img, 7, y, STONE_HI)
        px(img, 8, y, STONE_DKR)
    for x in range(3, 13):
        px(img, x, 6, STONE_DK)
        px(img, x, 7, STONE_HI)
    # 炉心辉光带（柱身 6..9 列 × 6..9 行）：外圈亮橙 + 内核亮黄。
    for y in range(6, 10):
        for x in range(6, 10):
            edge = x in (6, 9) or y in (6, 9)
            px(img, x, y, EMBER if edge else EMBER_HI)
    # 双臂端点辉斑（横杆两端亮橙 = 瓶受热读感）。
    for (x, y) in [(3, 6), (12, 6), (3, 7), (12, 7)]:
        px(img, x, y, EMBER)
    # 柱顶瓶位亮点。
    for (x, y) in [(7, 2), (8, 2)]:
        px(img, x, y, EMBER_HI)
    return img


def main():
    for name, img in [("default_brewing_stand", idle()),
                      ("default_brewing_stand_lit", lit())]:
        out = Image.fromarray(img.astype(np.uint8), "RGBA")
        out_path = os.path.join(SRC, name + ".png")
        out.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), out.size)


if __name__ == "__main__":
    main()
