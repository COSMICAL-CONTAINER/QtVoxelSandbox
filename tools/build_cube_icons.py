#!/usr/bin/env python3
"""把每方块的顶面 + 侧面贴图烘焙成等距（2:1 dimetric）立方体图标，供 hotbar / 背包使用。

为什么需要：
  - 源贴图尺寸不一（16×16 与 48×48 混排），用 Image.Pad 会按原始尺寸渲染 → 槽内图标
    大小不统一、48px 的被裁、16px 的偏小。
  - 单面平面贴图使 cobble/log/planks 肉眼难辨；leaves/sand 也读不出体积感。
  - 改为「顶 + 两侧」等距立方体图标后：每格尺寸完全统一（同一画布）；顶/侧明暗差异
    强化 3D 可读性；grass 顶绿侧褐、log 顶年轮侧树皮 等天然可辨。

实现：2:1 dimetric 投影（顶面菱形、两侧平行四边形）。对每输出像素反求 (u,v) 落在哪一面，
NEAREST 采样源贴图（保像素感），4× 超采样后 LANCZOS 降回目标尺寸 → 立方体轮廓抗锯齿、
贴图保持清晰、无面间接缝。顶 1.0 / 右 0.80 / 左 0.62 三档明暗（光自右上）。

资产管线（PLAN §2-L）：复用 textures/ 下既有面贴图合成，非 MC 资产。本脚可复现同一图标。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")

OUT = 64      # 最终图标边长（px）
SS = 4        # 超采样倍数（轮廓抗锯齿）
W = OUT * SS  # 工作画布边长
FACE_RES = 32  # 每面采样前统一缩到此分辨率（保证 8 方块采样密度一致）

# 每方块 → (顶面贴图, 侧面贴图) 文件名（不含扩展名）。来源 textures/，非 MC 资产。
BLOCKS = [
    ("grass",           "default_grass_top", "default_grass_side"),
    ("dirt",            "default_dirt",      "default_dirt"),
    ("stone",           "default_stone",     "default_stone"),
    ("cobble",          "default_cobble",    "default_cobble"),
    ("log",             "default_tree_top",  "default_tree"),
    ("planks",          "default_wood",      "default_wood"),
    ("leaves",          "default_leaves",    "default_leaves"),
    ("sand",            "default_sand",      "default_sand"),
    ("crafting_table",  "default_crafting_table_top", "default_crafting_table_side"),  # t50
    ("furnace",         "default_furnace_top", "default_furnace_side"),  # t80（图标显顶+侧，不显炉口前面）
]

# ---- dimetric 几何（工作画布坐标，y 向下）----
hw = 0.46 * W   # 顶菱形水平半对角线（= 立方体水平半宽）
dv = hw / 2.0   # 顶菱形竖直半对角线（2:1 dimetric）
v = 0.50 * W    # 侧面竖直高度（立方体身高）
cx = W / 2.0
cy = W / 2.0 - v / 2.0  # 使整体竖直居中

# 顶面菱形 4 角 + 底面 3 角（仅画可见三面需要这些）
N = np.array([cx, cy - dv], dtype=np.float64)
E = np.array([cx + hw, cy], dtype=np.float64)
S = np.array([cx, cy + dv], dtype=np.float64)
Wc = np.array([cx - hw, cy], dtype=np.float64)
Ep = np.array([cx + hw, cy + v], dtype=np.float64)
Sp = np.array([cx, cy + dv + v], dtype=np.float64)
Wp = np.array([cx - hw, cy + v], dtype=np.float64)

# 目标画布的 (x,y) 网格（y=row, x=col）
YS, XS = np.mgrid[0:W, 0:W].astype(np.float64)


def load_face(name):
    p = os.path.join(SRC, name + ".png")
    img = Image.open(p).convert("RGBA").resize((FACE_RES, FACE_RES), Image.NEAREST)
    arr = np.asarray(img, dtype=np.float64)  # (FACE_RES, FACE_RES, 4) RGBA
    # 源贴图可能含透明像素（如 leaves 半镂空）→ 立方体会被看穿、读不出体积。
    # 用该面的不透明像素平均色填掉透明像素并强制不透明，保证每方块都是实心立方体图标。
    a = arr[..., 3]
    opaque = a >= 128
    if opaque.all():
        arr[..., 3] = 255.0
        return arr
    if opaque.any():
        fill = arr[opaque][:, 0:3].mean(axis=0)
    else:
        fill = np.array([90.0, 130.0, 50.0])  # 兜底叶绿
    transparent = ~opaque
    arr[transparent, 0:3] = fill
    arr[..., 3] = 255.0
    return arr


def face_uv(o, uax, vax):
    """反求每像素的 (u,v)：前向 P = o + u*uax + v*vax。"""
    det = uax[0] * vax[1] - uax[1] * vax[0]
    dx = XS - o[0]
    dy = YS - o[1]
    u = (dx * vax[1] - dy * vax[0]) / det
    v = (uax[0] * dy - uax[1] * dx) / det
    return u, v


def sample(face, u, v):
    tx = np.clip(np.floor(u * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    ty = np.clip(np.floor(v * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    return face[ty, tx]  # (W, W, 4)


def render(top_name, side_name):
    top = load_face(top_name)
    side = load_face(side_name)
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    # 渲染序：左 → 右 → 顶（顶最后画，顶/侧共边归顶面；左/右共边归右面，无透明缝）
    layers = [
        (0.62, Wc, S - Wc, Wp - Wc, side),  # 左面
        (0.80, E,  S - E,  Ep - E,  side),  # 右面
        (1.00, Wc, N - Wc, S - Wc,  top),  # 顶面
    ]
    for shade, o, uax, vax, face in layers:
        u, v = face_uv(o, uax, vax)
        m = (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)
        col = sample(face, np.clip(u, 0, 1), np.clip(v, 0, 1)).copy()
        col[..., 0:3] *= shade  # 明暗（alpha 不变）
        canvas[m] = col[m]
    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    return img.resize((OUT, OUT), Image.LANCZOS)


def main():
    for out_name, top_name, side_name in BLOCKS:
        img = render(top_name, side_name)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)


if __name__ == "__main__":
    main()
