#!/usr/bin/env python3
"""生成红石中继器方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t1095 红石中继器（Repeater；机制等价 MC 1.0 repeater 的延迟四档 / 二极管整流 / 输出强充能 15）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：石质底板 + 双焰标 + 中缝滑标槽，
读作「信号中继机关件」——石基座上两枚红石焰标标记信号入 / 出轴，中缝滑标按延迟档滑动（滑标
几何在 mesher 摆位，贴图只提供底板与焰标观感）。

视觉意图：读作「石质信号中继器」——
  - 熄态（off）：石灰底板 + 暗红双焰标 + 中缝暗槽（信号断，焰标无光）。
  - 亮态（on）：同构底板略提亮 + 焰标亮红发光感 + 中缝滑标亮线（信号通，输出端有光）。
  - 两张同布局（逐像素位置一致，仅色相 / 亮度分态）→ mesher 按输出位切瓦时无布局跳变。
  - alpha 恒不透明（贴地薄板整面采样，透明孔会在 2/16 薄面上消隐——铁栏杆 t998 同教训）。
  - 固定坐标布点（非随机 → 重跑逐位可复现，PLAN §2-K 同纪律外溢到资产工具）。

输出（覆盖写入 textures/）：
  default_repeater_off.png （tile 191，熄态底板）
  default_repeater_on.png  （tile 192，亮态底板）

依赖：仅 PIL/numpy，无外部贴图。与 build_hopper.py / build_note_block.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 色板：石质底板（同圆石 / 石压力板石族语言）+ 红石焰标（暗红熄 / 亮红通）。
STONE = np.array([118.0, 118.0, 118.0])     # 底板主面
STONE_HI = np.array([138.0, 138.0, 138.0])  # 受光亮带（板顶两行）
STONE_DK = np.array([88.0, 88.0, 90.0])     # 边框暗带 / 中缝槽
FLAME_OFF = np.array([96.0, 28.0, 24.0])    # 焰标熄态（暗红，无光感）
FLAME_OFF_DK = np.array([64.0, 18.0, 16.0]) # 焰标熄态描边
FLAME_ON = np.array([232.0, 58.0, 40.0])    # 焰标亮态（亮红发光感）
FLAME_ON_DK = np.array([150.0, 30.0, 22.0]) # 焰标亮态描边
SLOT_OFF = np.array([52.0, 52.0, 56.0])     # 中缝滑标槽（熄态近黑）
SLOT_ON = np.array([255.0, 120.0, 96.0])    # 中缝滑标亮线（亮态信号感）


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0] = rgb[0]
        canvas[y, x, 1] = rgb[1]
        canvas[y, x, 2] = rgb[2]
        canvas[y, x, 3] = 255


def base_plate(img, brighten):
    """底板：石灰主面 + 顶两行受光 / 底一行暗边 + 四角铆钉式暗点（两张同布局）。"""
    for y in range(TS):
        for x in range(TS):
            base = STONE
            if y <= 1:
                base = STONE_HI
            elif y == TS - 1:
                base = STONE_DK
            if brighten and base is STONE:
                base = base + np.array([10.0, 10.0, 10.0])
            px(img, x, y, base)
    for (x, y) in [(1, 1), (14, 1), (1, 14), (14, 14)]:
        px(img, x, y, STONE_DK)


def torch_mark(img, cx, lit):
    """焰标：3×3 红石焰标 + 1px 描边（cx = 焰标中心列）。lit 选亮 / 熄色板。"""
    body = FLAME_ON if lit else FLAME_OFF
    edge = FLAME_ON_DK if lit else FLAME_OFF_DK
    for dy in range(-2, 2):
        for dx in range(-1, 2):
            x, y = cx + dx, 8 + dy
            is_edge = (dx == -1 or dx == 1 or dy == -2 or dy == 1)
            px(img, x, y, edge if is_edge else body)
    # 焰头顶光点（亮态白热芯 / 熄态暗芯）。
    px(img, cx, 7, np.array([255.0, 200.0, 180.0]) if lit else np.array([40.0, 12.0, 10.0]))


def slot_track(img, lit):
    """中缝滑标槽（x 7..8 竖带 y 3..12）：近黑槽底；亮态加滑标亮线段（信号感）。"""
    for y in range(3, 13):
        px(img, 7, y, SLOT_OFF)
        px(img, 8, y, SLOT_OFF)
    if lit:
        for y in range(5, 9):  # 滑标亮线（中段亮橙红）
            px(img, 7, y, SLOT_ON)
            px(img, 8, y, SLOT_ON)


def make(lit):
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    base_plate(img, lit)
    # 边框一圈暗带（同漏斗锅体框线语言）。
    for i in range(TS):
        px(img, i, 0, STONE_DK)
        px(img, i, 15, STONE_DK)
        px(img, 0, i, STONE_DK)
        px(img, 15, i, STONE_DK)
    slot_track(img, lit)
    # 双焰标：入端 x=3 / 出端 x=12（沿 +X 信号轴——贴图固定 +X 语义，mesher 按 state 朝向旋转不在此处理，
    #   与压力板 / 铁轨「贴图固定、几何承载朝向」同口径：中继器朝向由双焰标几何摆位表达）。
    torch_mark(img, 3, lit)
    torch_mark(img, 12, lit)
    return img


def main():
    for name, lit in [("default_repeater_off", False), ("default_repeater_on", True)]:
        out = Image.fromarray(make(lit).astype(np.uint8), "RGBA")
        out_path = os.path.join(SRC, name + ".png")
        out.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), out.size)


if __name__ == "__main__":
    main()
