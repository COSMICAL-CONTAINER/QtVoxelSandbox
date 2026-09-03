#!/usr/bin/env python3
"""生成裂纹石砖方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t998 结构新方块包（要塞逐方块还原前置；机制等价 MC 1.0 stone brick metadata 2 cracked——要塞承重段
开裂破损的石砖变体）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：石砖底 + 暗色裂纹线，
读作「裂开的石砖」。

视觉意图：读作「开裂的石砖」——
  - 主体：**与 default_stone_brick.png 同 RNG 基底**（同 seed 487 + 同调用序，像素级一致）——
    「同一块砖、不同破损」的变体叙事；裂纹以外区域与石砖逐像素同色。
  - 裂纹：两道深灰近黑裂纹折线（随机游走生成，跨砖缝贯穿——比砖缝 58 灰更深的 30 近黑，
    与砂浆勾缝可辨），伴 2-3 条 1px 短枝（分叉碎裂感）。

输出（覆盖写入 textures/）：
  default_cracked_stone_brick.png   （tile 182，裂纹石砖各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_stone_brick.py / build_mossy_stone_brick.py 同风格（程序生成原创像素图）。
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
# 裂纹叠加流（独立 seed，避免扰动基底流 → 基底与石砖保持逐像素一致）。
_CRACK_RNG = np.random.RandomState(9982)

# 裂纹色（近黑深灰——明显深于砖缝 58 灰，读作「裂」而非「缝」）。
CRACK = np.array([30.0, 30.0, 30.0])


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


def crack_walk(canvas, x0, y0, steps, dxy):
    """裂纹随机游走：从 (x0,y0) 走 steps 步，每步沿主导方向 dxy（(1,0) 横裂 / (0,1) 竖裂）
    前进 1px，30% 概率向垂直方向抖 1px（折线感）。越界即止。"""
    x, y = x0, y0
    for _ in range(steps):
        px(canvas, x, y, CRACK)
        if _CRACK_RNG.random() < 0.30:
            # 垂直抖动 1px（裂纹折线感）。
            if dxy[0] != 0:
                y += 1 if _CRACK_RNG.random() < 0.5 else -1
            else:
                x += 1 if _CRACK_RNG.random() < 0.5 else -1
        x += dxy[0]
        y += dxy[1]
        if not (0 <= x < TS and 0 <= y < TS):
            break


def draw_face():
    """裂纹石砖面：石砖基底（与石砖逐像素同源）+ 两道贯穿裂纹折线 + 短分叉枝。"""
    c = stone_brick_base()
    # 主裂纹 A：上行砖面内横向贯穿（y≈4，x 1..14 折线）。
    crack_walk(c, 1, 4, 13, (1, 0))
    # 主裂纹 B：下行砖面内斜向贯穿（从 (10,9) 往左下）。
    crack_walk(c, 10, 9, 9, (-1, 1))
    # 短分叉枝（自两道主裂纹中部各发 1-2 条 1px 竖枝 → 碎裂感）。
    crack_walk(c, 6, 4, 3, (0, 1))
    crack_walk(c, 5, 10, 2, (0, 1))
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # 裂纹石砖各面同贴图（mesher 整立方路径 6 面统一用 tile 182）。
    save(draw_face(), "default_cracked_stone_brick")


if __name__ == "__main__":
    main()
