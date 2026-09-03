#!/usr/bin/env python3
"""生成苔石砖方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t998 结构新方块包（要塞逐方块还原前置；机制等价 MC 1.0 stone brick metadata 1 mossy——要塞潮湿墙段的
风化石砖变体）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：石砖底 + 暗绿苔斑簇，读作「长苔的石砖」。

视觉意图：读作「长了苔藓的石砖」——
  - 主体：**与 default_stone_brick.png 同 RNG 基底**（同 seed 487 + 同调用序，像素级一致）——
    「同一块砖、不同风化」的变体叙事；苔斑以外区域与石砖逐像素同色。
  - 苔藓：散布暗绿斑簇（不规则团块，同 build_mossy_cobble.py 苔语言），覆盖约 25-35% 表面
    （石砖变体比苔石 40% 略收敛——砖面平整积水少，苔聚在缝线边角）。

输出（覆盖写入 textures/）：
  default_mossy_stone_brick.png   （tile 181，苔石砖各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_stone_brick.py / build_mossy_cobble.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机。**基底流 seed=487 与 build_stone_brick.py 完全一致 + 调用序一致**（先 m1 后 m2 两轮
# 0.20 双色噪声，此后不再动用本流）→ 苔石砖 / 裂纹石砖 / 石砖三张的砖底逐像素同源（探针钉此契约）。
_RNG = np.random.RandomState(487)
# 苔藓叠加流（独立 seed，避免扰动基底流 → 基底与石砖保持逐像素一致）。
_MOSS_RNG = np.random.RandomState(9981)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def stone_brick_base():
    """石砖基底：**逐字克隆 build_stone_brick.py draw_face**（同 seed 同调用序 → 输出与
    default_stone_brick.png 像素级一致；两脚本一处改砖底须同步，探针钉基底一致性）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    # 石质灰底（中灰，同 stone 美感）。
    canvas[..., 0] = 122.0
    canvas[..., 1] = 122.0
    canvas[..., 2] = 122.0
    canvas[..., 3] = 255.0
    # 砖块明暗双色噪声（表石面粗糙的明暗差异）。
    lite = np.array([142.0, 142.0, 142.0])
    dark = np.array([98.0, 98.0, 98.0])
    m1 = _RNG.random((TS, TS)) < 0.20
    canvas[m1, 0:3] = lite
    m2 = _RNG.random((TS, TS)) < 0.20
    canvas[m2, 0:3] = dark

    # 深灰砖缝颜色（砂浆勾缝，明显深于砖面）。
    seam = np.array([58.0, 58.0, 58.0])

    # 横向砖缝：第 0 / 8 行整行（把贴图分上下两行砖）。
    for x in range(TS):
        px(canvas, x, 0, seam)
        px(canvas, x, 8, seam)

    # 竖向砖缝（错缝 —— 上行砖缝在 x=8，下行砖缝在 x=4 与 x=12）。
    for y in range(1, 8):
        px(canvas, 8, y, seam)
    for y in range(9, TS):
        px(canvas, 4, y, seam)
        px(canvas, 12, y, seam)

    # 边框暗化（贴图四边 1 像素暗化，拟砖块边缘磨损）。
    edge = np.array([78.0, 78.0, 78.0])
    for i in range(TS):
        px(canvas, i, TS - 1, edge)
        px(canvas, TS - 1, i, edge)

    return canvas


def moss_splotch(canvas, cx_, cy_, rad, rgb):
    """在 (cx_,cy_) 画一团不规则苔藓斑簇（半径 rad 的不规则填充；同 build_mossy_cobble.py 苔语言）。"""
    for dy in range(-rad, rad + 1):
        for dx in range(-rad, rad + 1):
            if dx * dx + dy * dy <= rad * rad + _MOSS_RNG.randint(-2, 3):
                # 苔藓斑簇内部颜色微抖（深 / 浅绿交替 → 苔藓质感）。
                vary = np.array([rgb[0] + _MOSS_RNG.randint(-12, 13),
                                 rgb[1] + _MOSS_RNG.randint(-10, 16),
                                 rgb[2] + _MOSS_RNG.randint(-10, 11)])
                px(canvas, cx_ + dx, cy_ + dy, vary)


def draw_face():
    """苔石砖面：石砖基底（与石砖逐像素同源）+ 散布暗绿苔斑簇（覆盖约 25-35%）+ 缝线边角加密。"""
    c = stone_brick_base()
    # 暗绿苔藓主色（潮湿苔藓；同 build_mossy_cobble.py 色板）。
    moss = np.array([62.0, 94.0, 48.0])
    # 散布 7 团苔藓斑簇（半径 1-2）→ 覆盖约 25-35% 表面。
    for _ in range(7):
        mx = int(_MOSS_RNG.randint(0, TS))
        my = int(_MOSS_RNG.randint(0, TS))
        rad = int(_MOSS_RNG.randint(1, 3))
        moss_splotch(c, mx, my, rad, moss)
    # 缝线交点 / 边角加密苔藓（砖缝积水潮湿地带苔更密：四角 + 两条横缝中点）。
    for cx_, cy_ in [(1, 1), (TS - 2, 1), (1, TS - 2), (TS - 2, TS - 2), (8, 0), (4, 8), (12, 8)]:
        moss_splotch(c, cx_, cy_, 1, moss)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # 苔石砖各面同贴图（mesher 整立方路径 6 面统一用 tile 181）。
    save(draw_face(), "default_mossy_stone_brick")


if __name__ == "__main__":
    main()
