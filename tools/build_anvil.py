#!/usr/bin/env python3
"""生成铁块（IronBlock）+ 铁砧（Anvil）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 iron block / anvil，但贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。
铁砧在 MC 1.0 是异形（低体 + 上方砧台 + 尖角），本工程简化为整立方（机制等价非视觉对齐 §4），
故贴图用「俯视砧台轮廓」表达铁砧特征（顶面）+「深铁砧身横向分层」表达侧/底面。

五件贴图（覆盖写入 textures/）：
  - default_iron_block.png         铁块各面：金属灰底 + 铆钉网格 + 高光（9 铁锭存储方块）
  - default_anvil_top.png          铁砧顶面（完好）：深铁砧台 + 宽砧面 + 尖角 + 边缘暗化
  - default_anvil_base.png         铁砧侧/底/前面（三损坏阶段共享）：深铁砧身 + 横向分层暗带
  - default_anvil_damaged_1_top.png 铁砧顶面（微损）：砧台 + 一条细裂纹
  - default_anvil_damaged_2_top.png 铁砧顶面（重损）：砧台 + 粗裂纹 + 缺角

色块位置固定（固定种子噪声，确定性）→ 同输入同输出（便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
依赖：numpy / Pillow。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 铁系配色（原创，§9 区隔；机制等价 MC iron/anvil 金属灰族，数值自定）
IRON_DARK = np.array([58.0, 58.0, 66.0])     # 深铁（阴影 / 边缘）
IRON_BASE = np.array([92.0, 92.0, 100.0])    # 铁基色（砧身 / 铁块底）
IRON_FACE = np.array([128.0, 128.0, 138.0])  # 砧面（亮铁，反光顶面）
IRON_HI = np.array([168.0, 168.0, 180.0])    # 高光（金属反光）
RIVET = np.array([120.0, 120.0, 130.0])      # 铆钉点
CRACK = np.array([20.0, 18.0, 24.0])         # 裂纹（近黑深紫）


def blank(color):
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0:3] = color
    canvas[..., 3] = 255.0
    return canvas


def put(canvas, x, y, color):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = color


def hline(canvas, x0, x1, y, color):
    for x in range(x0, x1 + 1):
        put(canvas, x, y, color)


def vline(canvas, x, y0, y1, color):
    for y in range(y0, y1 + 1):
        put(canvas, x, y, color)


def rect(canvas, x0, y0, x1, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(canvas, x, y, color)


def speckle(canvas, count, color, seed):
    """固定种子散布 count 个 1px 斑点（表金属质感噪点；确定性）。"""
    rng = np.random.default_rng(seed)
    for _ in range(count):
        y, x = int(rng.integers(0, TS)), int(rng.integers(0, TS))
        put(canvas, x, y, color)


def edge_darken(canvas, band, color):
    """顶 / 底各 band 行暗化（表分层 / 边缘阴影）。"""
    for y in range(band):
        for x in range(TS):
            put(canvas, x, y, color)
            put(canvas, x, TS - 1 - y, color)


def save(canvas, name):
    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    path = os.path.join(SRC, name + ".png")
    img.save(path)
    print("wrote", os.path.relpath(path, HERE), img.size)


# ── 铁块（iron_block）：金属灰底 + 4×4 铆钉网格 + 高光斜带 ──
def make_iron_block():
    c = blank(IRON_BASE)
    speckle(c, 26, IRON_DARK, seed=101)   # 深铁斑驳（金属粗糙感）
    speckle(c, 14, IRON_HI, seed=102)     # 高光散点（金属反光）
    # 4×4 铆钉网格（每 4px 一个，表「9 锭压实成块」的金属拼接感）
    for gy in range(4):
        for gx in range(4):
            put(c, 2 + gx * 4, 2 + gy * 4, RIVET)
            put(c, 3 + gx * 4, 2 + gy * 4, IRON_DARK)
    # 顶边 + 左边高光（表受光面）
    hline(c, 1, TS - 2, 0, IRON_HI)
    vline(c, 0, 1, TS - 2, IRON_HI)
    # 底边 + 右边暗化（表背光面）
    hline(c, 1, TS - 2, TS - 1, IRON_DARK)
    vline(c, TS - 1, 1, TS - 2, IRON_DARK)
    return c


# ── 铁砧顶面基底（俯视砧台）：深铁砧台 + 宽砧面 + 尖角（horn 伸向 +X）──
def anvil_top_base():
    c = blank(IRON_DARK)            # 砧台深铁底
    speckle(c, 18, IRON_BASE, seed=211)
    # 砧面（亮铁宽矩形，占中段）：俯视铁砧的「宽顶面」
    rect(c, 2, 3, 11, 12, IRON_FACE)
    # 砧面边缘暗化（表砧面下沉感）
    rect(c, 2, 3, 11, 3, IRON_BASE)      # 顶边暗
    rect(c, 2, 12, 11, 12, IRON_BASE)    # 底边暗
    vline(c, 2, 3, 12, IRON_BASE)        # 左边暗
    # 尖角（horn）：从砧面 +X 侧伸出逐渐收窄的尖（表铁砧标志性尖角）
    hline(c, 12, 13, 6, IRON_FACE)
    hline(c, 12, 13, 7, IRON_FACE)
    hline(c, 13, 14, 8, IRON_FACE)
    put(c, 14, 7, IRON_FACE)
    # 砧面高光（受光斜带，表金属反光）
    hline(c, 4, 10, 5, IRON_HI)
    put(c, 3, 6, IRON_HI)
    put(c, 11, 6, IRON_HI)
    # 砧台四角暗化（表砧台边缘阴影）
    edge_darken(c, 1, IRON_DARK)
    return c


# ── 铁砧侧 / 底 / 前面（砧身，三损坏阶段共享）：深铁 + 横向分层暗带 ──
def make_anvil_base():
    c = blank(IRON_DARK)
    speckle(c, 22, IRON_BASE, seed=311)
    speckle(c, 10, IRON_HI, seed=312)
    # 上段（砧面下方颈缩）：亮铁窄带（表砧面底部反光）
    hline(c, 1, TS - 2, 1, IRON_FACE)
    hline(c, 1, TS - 2, 2, IRON_BASE)
    # 中段（砧身主体）：深铁 + 两条横向暗带（表砧身分段 / 锻造接缝）
    hline(c, 0, TS - 1, 6, IRON_DARK)
    hline(c, 0, TS - 1, 7, IRON_DARK)
    hline(c, 0, TS - 1, 9, IRON_DARK)
    # 下段（底座）：亮铁宽带（表砧底宽座反光）
    for y in range(11, TS):
        hline(c, 0, TS - 1, y, IRON_FACE if y >= 13 else IRON_BASE)
    # 左 / 右边暗化（表圆柱感）
    vline(c, 0, 0, TS - 1, IRON_DARK)
    vline(c, TS - 1, 0, TS - 1, IRON_DARK)
    return c


def draw_crack(c, points, color=CRACK):
    """沿 points 列表画 1px 裂纹折线。"""
    for (x, y) in points:
        put(c, x, y, color)


def make_anvil_top_damaged1():
    """微损顶面：砧台 + 一条细斜裂纹（从砧面右下到砧台左下）。"""
    c = anvil_top_base()
    draw_crack(c, [(10, 11), (9, 12), (8, 13), (7, 14)])
    put(c, 9, 11, CRACK)
    return c


def make_anvil_top_damaged2():
    """重损顶面：砧台 + 粗裂纹网 + 尖角缺角（更严重损坏）。"""
    c = anvil_top_base()
    # 主粗裂纹（双线加粗）
    draw_crack(c, [(9, 4), (8, 5), (7, 6), (6, 7), (5, 8), (4, 9), (3, 10)])
    draw_crack(c, [(9, 5), (8, 6), (7, 7), (6, 8), (5, 9), (4, 10)])
    # 分叉裂纹
    draw_crack(c, [(7, 6), (7, 7), (8, 8)])
    draw_crack(c, [(5, 8), (6, 9), (6, 10)])
    # 尖角缺角（horn 末端崩缺）
    put(c, 14, 7, IRON_DARK)
    put(c, 13, 8, IRON_DARK)
    put(c, 13, 7, CRACK)
    return c


def main():
    save(make_iron_block(), "default_iron_block")
    save(anvil_top_base(), "default_anvil_top")
    save(make_anvil_base(), "default_anvil_base")
    save(make_anvil_top_damaged1(), "default_anvil_damaged_1_top")
    save(make_anvil_top_damaged2(), "default_anvil_damaged_2_top")


if __name__ == "__main__":
    main()
