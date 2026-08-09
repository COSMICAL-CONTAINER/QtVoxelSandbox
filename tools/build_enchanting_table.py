#!/usr/bin/env python3
"""生成附魔台（EnchantingTable）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 附魔台（enchanting table），但贴图为本项目程序生成的原创像素图，
**不**拷贝任何 MC 资产。两件贴图：

  - default_enchanting_table_top.png（顶面）：黑曜石深紫黑底 + 钻石青白菱斑四角嵌点 +
    中央一本「立书」轮廓（书脊+书页暗化带），表「附魔台 = 黑曜石基座 + 顶上立书」
    的 MC 1.0 标志性轮廓（简化为俯视：书脊居中纵贯、两侧书页）。
  - default_enchanting_table_side.png（侧面 / 底面 / 前面）：黑曜石深紫黑底 + 钻石青白
    菱斑四角嵌点 + 边缘暗化带（顶部 / 底部各一道暗边，表「分层石质基座」）。

色块位置固定（无随机源）→ 同输入同输出（确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
不对称布局避免「网格化 / 重复纹理」的人工感。

输出（覆盖写入 textures/）：
  default_enchanting_table_top.png
  default_enchanting_table_side.png

依赖：无（纯合成像素图，不读既有贴图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def base():
    """黑曜石深紫黑底（同 build_obsidian.py 配色：深紫黑火山玻璃）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    # 深紫黑底（与 obsidian 同源色，表「黑曜石基座」）
    canvas[..., 0:3] = np.array([18.0, 12.0, 26.0])
    canvas[..., 3] = 255.0
    # 加少量深紫斑驳（同 obsidian 纹理嵌点，表「火山玻璃质感」）
    rng = np.random.default_rng(42)  # 固定种子 → 确定性
    for _ in range(18):
        y, x = int(rng.integers(0, TS)), int(rng.integers(0, TS))
        canvas[y, x, 0:3] = np.array([42.0, 22.0, 56.0])  # 紫红嵌点
    for _ in range(8):
        y, x = int(rng.integers(0, TS)), int(rng.integers(0, TS))
        canvas[y, x, 0:3] = np.array([68.0, 50.0, 88.0])  # 品紫微反光
    return canvas


def paint_blobs(canvas, centers, color):
    """在 canvas 上画若干 1×1 像素点（钻石嵌点，硬边像素感）。"""
    for (cx, cy) in centers:
        if 0 <= cx < TS and 0 <= cy < TS:
            canvas[cy, cx, 0:3] = color


def draw_top(canvas):
    """顶面：黑曜石底 + 钻石青白菱斑四角 + 中央立书轮廓（书脊纵贯 + 两侧书页）。

    MC 1.0 附魔台顶面标志性特征 = 黑曜石基座上摊开一本「书」（俯视为中央书脊 + 两侧书页）。
    本工程简化为像素图：中央纵贯暗带（书脊）+ 两侧亮带（书页）+ 上下书缘暗化。
    """
    # 钻石青白四角嵌点（与钻石矿 diamond_ore 同源色，表「钻石装饰」）
    diamond = np.array([110.0, 214.0, 214.0])
    paint_blobs(canvas, [(1, 1), (14, 1), (1, 14), (14, 14)], diamond)
    # 中央立书（俯视：书脊居中纵贯 + 两侧书页亮带）
    book_spine = np.array([58.0, 38.0, 26.0])      # 深棕（书脊皮革）
    book_page = np.array([208.0, 196.0, 158.0])    # 米黄（书页）
    book_edge = np.array([28.0, 18.0, 12.0])       # 暗棕（书缘暗化）
    # 书页两侧（米黄亮带，col 4..6 / 9..11）
    for y in range(2, TS - 2):
        canvas[y, 4, 0:3] = book_page
        canvas[y, 5, 0:3] = book_page
        canvas[y, 10, 0:3] = book_page
        canvas[y, 11, 0:3] = book_page
    # 书脊居中（深棕纵贯，col 7..8）
    for y in range(2, TS - 2):
        canvas[y, 7, 0:3] = book_spine
        canvas[y, 8, 0:3] = book_spine
    # 书缘暗化（顶 / 底各一行暗棕，表「书合拢的边缘」）
    for x in range(4, 12):
        canvas[2, x, 0:3] = book_edge
        canvas[TS - 3, x, 0:3] = book_edge
    return canvas


def draw_side(canvas):
    """侧面 / 底面 / 前面：黑曜石底 + 钻石青白菱斑四角 + 顶 / 底边缘暗化带（表「分层石质基座」）。"""
    # 钻石青白四角嵌点
    diamond = np.array([110.0, 214.0, 214.0])
    paint_blobs(canvas, [(1, 1), (14, 1), (1, 14), (14, 14)], diamond)
    # 顶 / 底边缘暗化带（深紫黑，表「分层基座」）
    edge = np.array([10.0, 6.0, 16.0])
    for x in range(TS):
        canvas[0, x, 0:3] = edge
        canvas[1, x, 0:3] = edge
        canvas[TS - 1, x, 0:3] = edge
        canvas[TS - 2, x, 0:3] = edge
    return canvas


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_top(base()), "default_enchanting_table_top")
    save(draw_side(base()), "default_enchanting_table_side")


if __name__ == "__main__":
    main()
