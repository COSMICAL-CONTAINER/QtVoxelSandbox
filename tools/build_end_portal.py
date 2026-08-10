#!/usr/bin/env python3
"""生成末地传送门方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t487 要塞传送门房中央方块（机制等价 MC 1.0 end portal——末地传送门平面；§9 区隔：末地为通用描述词，
机制对齐非 MC 专名照搬）。名称 / 贴图纯原创自绘（零 MC 资产）。

视觉意图：读作「星空中的绿色传送门旋涡」——
  - 主体：深紫黑星空底（黑紫底 + 散布星点）。
  - 中心：亮绿色旋涡（同心方框环纹，从中心亮绿渐到外圈深绿）。
  - 末激活版（end_portal）旋涡暗；激活版（end_portal_active）旋涡亮 + 中心高光（mesher 据 state bit0 切换）。

输出（覆盖写入 textures/）：
  default_end_portal.png          （tile 129，末地传送门未激活态各面同贴图）
  default_end_portal_active.png   （tile 130，末地传送门激活态各面同贴图 —— 旋涡更亮 + 中心高光）

依赖：仅 PIL/numpy，无外部贴图。与 build_obsidian.py / build_enchanting_table.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(4872)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def starfield_base():
    """深紫黑星空底 + 散布星点。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    # 深紫黑底（虚空夜空）。
    canvas[..., 0] = 14.0
    canvas[..., 1] = 8.0
    canvas[..., 2] = 22.0
    canvas[..., 3] = 255.0
    # 散布星点（亮紫白点，确定性散布）。
    star = np.array([180.0, 170.0, 210.0])
    star_dim = np.array([90.0, 80.0, 110.0])
    for _ in range(20):
        sx = int(_RNG.randint(0, TS))
        sy = int(_RNG.randint(0, TS))
        px(canvas, sx, sy, star if _RNG.random() < 0.4 else star_dim)
    return canvas


def draw_face(active):
    """末地传送门面：深紫黑星空底 + 中心亮绿旋涡（active 控制旋涡亮度 + 中心高光）。

    active=False（tile 129 未激活）：旋涡暗绿，中心无高光。
    active=True（tile 130 激活）：旋涡亮绿 + 中心白绿高光。
    """
    c = starfield_base()
    # 旋涡颜色（激活态明显更亮）。
    if active:
        ring_bright = np.array([90.0, 230.0, 130.0])    # 亮绿（激活）
        ring_mid = np.array([40.0, 160.0, 80.0])
        core = np.array([220.0, 255.0, 220.0])          # 中心白绿高光
    else:
        ring_bright = np.array([40.0, 110.0, 60.0])     # 暗绿（未激活）
        ring_mid = np.array([22.0, 60.0, 32.0])
        core = np.array([50.0, 90.0, 50.0])             # 中心暗绿（无高光）
    # 同心方框环纹（旋涡）：中心 2×2 = core，外扩两圈 ring。
    cx, cy = TS // 2, TS // 2
    # 中心 2×2 core（旋涡最内）。
    for dy in range(-1, 1):
        for dx in range(-1, 1):
            px(c, cx + dx, cy + dy, core)
    # 内圈环（围绕 core 的一圈亮 ring_bright）。
    for dy in range(-2, 2):
        for dx in range(-2, 2):
            if abs(dx) == 2 or abs(dy) == 2 or abs(dx) == 1 and abs(dy) == 1 and not (abs(dx) <= 1 and abs(dy) <= 1 and abs(dx) + abs(dy) <= 1):
                # 仅画环边缘（避免覆盖 core）。
                if not (abs(dx) < 2 and abs(dy) < 2):
                    px(c, cx + dx, cy + dy, ring_bright)
    # 外圈环（ring_mid，旋涡外缘渐暗）。
    for dy in range(-3, 3):
        for dx in range(-3, 3):
            if abs(dx) == 3 or abs(dy) == 3:
                # 仅画最外环边缘（距中心 3 格的方框）。
                if abs(dx) <= 3 and abs(dy) <= 3:
                    px(c, cx + dx, cy + dy, ring_mid)
    # 旋涡触手（从外圈向四角延伸的暗绿短线，拟旋涡动感）。
    tendril = ring_mid
    for i in range(3):
        px(c, cx + 3 + i, cy + 3 + i, tendril)
        px(c, cx - 3 - i, cy - 3 - i, tendril)
        px(c, cx + 3 + i, cy - 3 - i, tendril)
        px(c, cx - 3 - i, cy + 3 + i, tendril)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # 末地传送门各面同贴图（mesher 整立方路径 6 面统一用 tile 129/130 据 state bit0 选）。
    save(draw_face(active=False), "default_end_portal")
    save(draw_face(active=True), "default_end_portal_active")


if __name__ == "__main__":
    main()
