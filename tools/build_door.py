#!/usr/bin/env python3
"""生成门上下半 per-face 贴图（16×16 像素，原创自绘，§9 override (a)）。

t620 门贴图 per-face 接入：机制等价 MC 1.0 门两格高模型——下格 = 门板（整板 + 底部横带 + 锁孔板），
上格 = 门板 + 上部格栅窗（可透视的镂空感用暗色格栅表达，不做真透明——本工程门是满高薄板非镂空）。
PartialBlockGeometry 的 door case 据 state bit3（上/下格）选 tile：kDefs topTile=upper / bottomTile=lower。
名称 / 贴图纯原创自绘（零 MC 资产）；橡木（浅棕）/ 云杉（深冷棕）两族仅色板差异（同 build_spruce.py 模式）。

输出（覆盖写入 textures/）：
  default_door_wood_upper.png     （tile 143，橡木门上半：门板 + 上部格栅窗）
  default_door_wood_lower.png     （tile 144，橡木门下半：门板 + 底部横带 + 锁孔板）
  default_door_spruce_upper.png   （tile 145，云杉门上半：深色门板 + 格栅窗）
  default_door_spruce_lower.png   （tile 146，云杉门下半：深色门板 + 底部横带 + 锁孔板）

依赖：仅 PIL/numpy，无外部贴图。与 build_spruce.py / build_atlas.py 同风格。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 橡木 / 云杉色板（与 build_spruce.py 的 planks 色板同源——门是木板制品，观感与同族木板一致）。
OAK = {
    "board":  np.array([162.0, 128.0, 78.0]),   # 门板底（橡木棕）
    "board2": np.array([148.0, 116.0, 68.0]),   # 门板暗纹（年轮带）
    "grille": np.array([96.0, 74.0, 44.0]),     # 格栅窗格条（暗棕）
    "glass":  np.array([46.0, 62.0, 72.0]),     # 窗洞底（暗青，表玻璃透视暗感）
    "band":   np.array([120.0, 94.0, 56.0]),    # 底部横带（嵌板压条）
    "plate":  np.array([138.0, 108.0, 64.0]),   # 锁孔板（亮一档的嵌板）
    "edge":   np.array([104.0, 82.0, 48.0]),    # 边框暗化
}
SPRUCE = {
    "board":  np.array([108.0, 78.0, 48.0]),    # 门板底（云杉深冷棕）
    "board2": np.array([96.0, 68.0, 40.0]),     # 门板暗纹
    "grille": np.array([64.0, 46.0, 26.0]),     # 格栅窗格条
    "glass":  np.array([34.0, 46.0, 54.0]),     # 窗洞底
    "band":   np.array([82.0, 58.0, 34.0]),     # 底部横带
    "plate":  np.array([94.0, 68.0, 40.0]),     # 锁孔板
    "edge":   np.array([70.0, 50.0, 28.0]),     # 边框暗化
}

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(6202)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def board_base(pal):
    """门板底：木色底 + 竖向年轮暗纹（确定性）+ 左右边框暗化。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0:3] = pal["board"]
    canvas[..., 3] = 255.0
    # 竖向年轮暗纹（3 条不规则竖带；确定性散布）。
    for _ in range(3):
        x0 = int(_RNG.randint(2, TS - 2))
        for y in range(TS):
            px(canvas, min(x0 + (1 if (y + x0) % 5 == 0 else 0), TS - 1), y, pal["board2"])
    # 左右边框暗化（门板侧边收边）。
    for y in range(TS):
        canvas[y, 0, 0:3] = pal["edge"]
        canvas[y, TS - 1, 0:3] = pal["edge"]
    return canvas


def draw_upper(pal):
    """上半：门板 + 上部格栅窗（4×4 格窗：格条 + 窗洞暗青底）+ 顶部边框。"""
    c = board_base(pal)
    # 顶部边框。
    for x in range(TS):
        c[0, x, 0:3] = pal["edge"]
    # 格栅窗区 y[2,9) × x[2,14)（占上半门板的大窗）。
    for y in range(2, 9):
        for x in range(2, 14):
            c[y, x, 0:3] = pal["glass"]
    # 格条：竖条 x=4-5 / 8-9 / 12-13 中取 x∈{5,9,13}，横条 y=3 / y=7（十字格栅）。
    for y in range(2, 9):
        for gx in (5, 9, 13):
            c[y, gx, 0:3] = pal["grille"]
    for x in range(2, 14):
        c[3, x, 0:3] = pal["grille"]
        c[7, x, 0:3] = pal["grille"]
    # 窗框缘（窗区外圈压条，区别窗洞与门板）。
    for x in range(2, 14):
        c[2, x, 0:3] = pal["edge"]
        c[8, x, 0:3] = pal["edge"]
    for y in range(2, 9):
        c[y, 2, 0:3] = pal["edge"]
        c[y, 13, 0:3] = pal["edge"]
    return c


def draw_lower(pal):
    """下半：门板 + 中部锁孔板（亮嵌板 + 暗锁孔）+ 底部横带。"""
    c = board_base(pal)
    # 底部横带 y[12,14)。
    for y in range(12, 14):
        for x in range(TS):
            c[y, x, 0:3] = pal["band"]
    # 锁孔板：中央亮嵌板 x[6,10) × y[6,10)。
    for y in range(6, 10):
        for x in range(6, 10):
            c[y, x, 0:3] = pal["plate"]
    # 锁孔（嵌板中央暗点 + 下方短槽）。
    hole = np.array([52.0, 40.0, 24.0])
    c[7, 7, 0:3] = hole
    c[7, 8, 0:3] = hole
    c[8, 7, 0:3] = hole
    c[8, 8, 0:3] = hole
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_upper(OAK), "default_door_wood_upper")
    save(draw_lower(OAK), "default_door_wood_lower")
    save(draw_upper(SPRUCE), "default_door_spruce_upper")
    save(draw_lower(SPRUCE), "default_door_spruce_lower")


if __name__ == "__main__":
    main()
