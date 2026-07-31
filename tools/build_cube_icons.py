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
    ("coal_ore",        "default_coal_ore", "default_coal_ore"),  # t84 煤矿石（各面同贴图）
    ("iron_ore",        "default_iron_ore", "default_iron_ore"),  # t84 铁矿石（各面同贴图）
    ("torch",           "default_torch", "default_torch"),  # t88 火把（透明底+木柄+火焰；走平面 2D 分支非立方体投影）
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


def render_flat_2d(name):
    """平面 2D 图标（异形 / 半透方块如火把）：源贴图透明底直接 NEAREST 放大到 OUT×OUT，保留 alpha。

    根因：火把贴图（default_torch.png）是 16×16 透明底只含火把本体（木柄 + 火焰）。若走立方体投影
    (render)，load_face 会用不透明像素平均色填掉透明像素 → 图标糊成实心方块、且投影本身把整块
    16×16 当立方体面渲染 → 黑底方块。火把游戏内本就是异形（torchHost 木柄 + 火焰小立方，非
    1×1×1 立方体；mesher 对 Torch continue），图标应反映其 2D 形态：直接放大、保留 alpha →
    「纯火把无方块底」。
    """
    p = os.path.join(SRC, name + ".png")
    img = Image.open(p).convert("RGBA")
    return img.resize((OUT, OUT), Image.NEAREST)


# t145 不完整方块 flat 2D 区分图标：6 类木制半方块各走自己的剪影（半高 / L 阶 / 柱档 / 高板 / 方格 / 薄条），
# 木板贴图填充剪影、剪影外透明。与立方体图标共用 OUT×OUT 画布 → 槽位尺寸统一（t15）。
#
# 根因：v1 这 6 类共用 icon_planks 立方体图标 + 仅靠中文显示名区分 → 创造调色板 / hotbar 6 格同图、肉眼
# 无法辨图。spec t145 要求 build_cube_icons 程序生成各异 flat 2D 图标。剪影取自各方块世界内异形几何
# （partialblockgeometry.cpp）的正视投影：slab 半高 / stairs L 阶 / fence 立柱+横档 / door 满高窄板 /
# trapdoor 方格 / pressure_plate 薄条。木板材质观感一致（同为木制半方块），仅剪影不同 → 玩家一眼分辨
# 「这是哪类异形」，配合中文显示名（木板台阶 / 木板楼梯 / …）双重区分。
PARTIALS = [
    ("wood_slab",           "slab"),           # 木板台阶：下半矩形（半高）
    ("wood_stairs",         "stairs"),         # 木板楼梯：下半整 + 左上 quarter（L 阶）
    ("wood_fence",          "fence"),          # 木栅栏：中心立柱 + 上下两条横档（柱档）
    ("wood_door",           "door"),           # 木板门：满高窄竖板 + 把手（高板）
    ("wood_trapdoor",       "trapdoor"),       # 木活板门：满格 + 2×2 网格纹（方格）
    ("wood_pressure_plate", "pressure_plate"), # 木板压力板：中部细横条（薄）
]


def _shape_mask(shape):
    """工作画布 W×W（y 向下，归一坐标 [0,1]）内返回该异形的 bool 剪影。

    坐标约定与 partialblockgeometry.cpp 的局部体素坐标一致（y 向上 0..1），但画布 y 向下 →
    画布 y=0.5..1.0 对应世界下半格（脚侧），符合「半砖贴地」的视觉直觉。
    """
    xn = XS / W
    yn = YS / W
    if shape == "slab":
        # 半砖：下半矩形（画布 y 0.5..1.0 全宽）= 世界下半格贴地。
        return (yn >= 0.5) & (yn <= 1.0)
    if shape == "stairs":
        # 楼梯 L 阶：下半整（y 0.5..1.0 全宽）∪ 左上 quarter（y 0..0.5 ∧ x 0..0.5）。
        # 剪影像 MC 楼梯正视：低处满、高处只剩一侧背墙 → 阶梯轮廓。
        lower = (yn >= 0.5) & (yn <= 1.0)
        upper_left = (yn < 0.5) & (xn < 0.5)
        return lower | upper_left
    if shape == "fence":
        # 栅栏：中心立柱（x 0.40..0.60 全高）∪ 两条横档（y 0.20..0.34 / 0.66..0.80 全宽，
        #   横档比立柱略宽 → 柱档凸出两侧的栅栏观感）。
        post = (xn >= 0.40) & (xn <= 0.60)
        bar1 = (yn >= 0.20) & (yn <= 0.34)
        bar2 = (yn >= 0.66) & (yn <= 0.80)
        return post | bar1 | bar2
    if shape == "door":
        # 门：满高窄竖板（x 0.22..0.78，y 0.03..0.98；上下留窄缝显「门框内」）。
        return (xn >= 0.22) & (xn <= 0.78) & (yn >= 0.03) & (yn <= 0.98)
    if shape == "trapdoor":
        # 活版门：满格（2×2 方格纹在 render_partial_2d 内叠加，区分于满格木板立方体图标）。
        return np.ones_like(xn, dtype=bool)
    if shape == "pressure_plate":
        # 压力板：中部细横条（y 0.42..0.58，x 0.08..0.92；最薄的一类，一眼可辨）。
        return (yn >= 0.42) & (yn <= 0.58) & (xn >= 0.08) & (xn <= 0.92)
    return np.zeros_like(xn, dtype=bool)


def render_partial_2d(shape, fill_name="default_wood"):
    """异形方块 flat 2D 图标：木板贴图填充剪影、剪影外透明；剪影边缘暗化描边强化小尺寸可辨性。

    与立方体图标 (render) 共用 W×W 超采样画布 + LANCZOS 降采样 → 剪影轮廓抗锯齿、与立方体图标
    尺寸完全统一。木板贴图按画布坐标线性平铺（整图共享一张木纹 → 6 类材质观感一致）。
    """
    fill = load_face(fill_name)  # FACE_RES×FACE_RES，已强制不透明（leaves 等透明源的统一处理）
    mask = _shape_mask(shape)

    # 木板贴图按画布归一坐标线性采样（u,v in [0,1] → FACE_RES 索引）。
    xn = XS / W
    yn = YS / W
    tx = np.clip(np.floor(xn * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    ty = np.clip(np.floor(yn * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    col = fill[ty, tx].copy()
    canvas[mask] = col[mask]

    # 剪影内 1px（超采样空间）暗化描边：把 mask 零填充一圈后做 4 邻域「全在内」判定，
    # 差集 = 内边缘带（canvas 外视作非 mask → 贴画布边的形状边也被描边）。SS× 下采样后呈细暗轮廓。
    padded = np.zeros((W + 2, W + 2), dtype=bool)
    padded[1:-1, 1:-1] = mask
    nb = padded[:-2, 1:-1] & padded[2:, 1:-1] & padded[1:-1, :-2] & padded[1:-1, 2:]
    edge = mask & ~nb
    canvas[edge, 0:3] *= 0.55

    # 活版门叠加 2×2 方格纹（外框 + 中十字），把「满格」从木板立方体图标里区分出来。
    if shape == "trapdoor":
        border = (xn < 0.06) | (xn > 0.94) | (yn < 0.06) | (yn > 0.94)
        mid = (np.abs(xn - 0.5) < 0.035) | (np.abs(yn - 0.5) < 0.035)
        canvas[border | mid, 0:3] *= 0.55
    # 门加把手（右侧中段小暗点），强化「门」语义。
    if shape == "door":
        knob = (xn > 0.66) & (xn < 0.72) & (yn > 0.48) & (yn < 0.56)
        canvas[knob, 0:3] *= 0.45

    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    return img.resize((OUT, OUT), Image.LANCZOS)


def main():
    for out_name, top_name, side_name in BLOCKS:
        if out_name == "torch":
            # 火把走平面 2D 路径（透明底保留 alpha），非立方体投影（见 render_flat_2d 注释）。
            img = render_flat_2d(top_name)
        else:
            img = render(top_name, side_name)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t145 不完整方块 flat 2D 区分图标（6 类木制半方块）。
    for out_name, shape in PARTIALS:
        img = render_partial_2d(shape)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)


if __name__ == "__main__":
    main()
