#!/usr/bin/env python3
"""生成南瓜灯（JackOLantern）点亮刻脸贴图（16×16 像素，原创自绘，§9 override (a)）。

t1105 南瓜农作链：南瓜灯 = 南瓜 + 火把无序合成的整立方光源方块（机制等价 MC 1.0 jack o'lantern，
Beta 1.2 起入版、1.0 基准内同料；lightEmission 15 真方块光 flood 光源族）。tile 205（frontTile=点亮刻脸；
顶/侧复用南瓜 tile 117/119）。**点亮感由 lightEmission 15 承担非贴图切换**（南瓜灯无两态——恒亮），
本贴图是「恒亮点亮刻脸」：深橙瓜底 + 亮黄三角眼 / 锯齿嘴 + 内晕光。

输出（覆盖写入 textures/）：
  default_jackolantern_face.png   （tile 205）

依赖：仅 PIL/numpy，一次写入；与 build_pumpkin.py / build_melon.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 南瓜灯色板（南瓜底色系：深橙瓜底 + 点亮孔洞亮黄 + 内晕光；与 build_pumpkin.py 同源橙色系）。
PUMPKIN_BASE = np.array([206.0, 110.0, 28.0])   # 瓜底深橙
PUMPKIN_DARK = np.array([162.0, 80.0, 18.0])    # 瓜棱暗橙（棱带 / 边缘）
GLOW_YELLOW = np.array([252.0, 216.0, 96.0])    # 点亮孔洞亮黄（内焰读感）
GLOW_CORE = np.array([255.0, 244.0, 190.0])     # 孔心白热（最亮点）
GLOW_HALO = np.array([236.0, 172.0, 60.0])      # 孔缘晕光（底色→孔洞过渡）

def draw_face():
    """点亮刻脸：深橙瓜底 + 纵向瓜棱暗纹 + 亮黄三角眼 ×2 + 锯齿嘴 + 孔缘晕光 + 顶缘短茎位。"""
    c = np.zeros((TS, TS, 4), np.float64)
    c[...] = 0
    c[..., 0:3] = PUMPKIN_BASE
    c[..., 3] = 255.0
    # 纵向瓜棱暗纹（4 条棱带 + 波浪暗点，同南瓜侧贴图读感）。
    for x in (2, 6, 10, 14):
        c[x, 0:TS, 0:3] = PUMPKIN_DARK
        c[x - 1, 3, 0:3] = GLOW_HALO  # 棱带左亮纹（波浪点）
        c[x - 1, 9, 0:3] = PUMPKIN_DARK
    # 三角眼 ×2（晕光缘 + 亮黄孔 + 白热孔心）。
    for ex in (3, 10):
        c[ex - 1, 5, 0:3] = GLOW_HALO
        c[ex, 5, 0:3] = GLOW_YELLOW
        c[ex + 1, 5, 0:3] = GLOW_HALO
        c[ex, 6, 0:3] = GLOW_CORE
        c[ex, 7, 0:3] = GLOW_HALO
    # 锯齿嘴（亮黄锯齿带 + 晕光缘）。
    c[3:13, 11, 0:3] = GLOW_YELLOW
    c[4:12, 12, 0:3] = GLOW_YELLOW
    c[5:7, 12, 0:3] = PUMPKIN_BASE   # 锯齿 1（上凹）
    c[9:11, 12, 0:3] = PUMPKIN_BASE  # 锯齿 2（上凹）
    c[4:12, 13, 0:3] = GLOW_HALO     # 嘴底晕光缘
    # 孔心白热点缀（眼 2 点 + 嘴 1 点）。
    c[4, 11, 0:3] = GLOW_CORE
    c[11, 11, 0:3] = GLOW_CORE
    # 边缘暗化（体积感）。
    c[0, 0:TS, 0:3] = PUMPKIN_DARK
    c[TS - 1, 0:TS, 0:3] = PUMPKIN_DARK
    c[0:TS, 0, 0:3] = PUMPKIN_DARK
    c[0:TS, TS - 1, 0:3] = PUMPKIN_DARK
    return c

def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)

def main():
    save(draw_face(), "default_jackolantern_face")

if __name__ == "__main__":
    main()
