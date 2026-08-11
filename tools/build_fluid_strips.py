#!/usr/bin/env python3
"""t489 生成水/岩浆「条带」贴图（材质级 flipbook 动画，替代 t222/t223 重建式水动画）。

机制等价 MC 1.0 流体动画（静水/流水/岩浆 flipbook），但贴图为本项目程序生成的原创像素图，**不**拷贝
任何 MC 资产（§9 override (a)）。

条带结构（与 chunkgeometry UV 烘焙 + Main.qml positionV 动画三方共用 BlockRegistry::kWaterStripFrames /
kLavaStripFrames 常量；改帧数必须三方同步）：
  - water_strip.png：32 宽 × 512 高 = 2 列（左=静水 still / 右=流水 flow）× 32 行（每帧 16×16）。
    静水列：横向波纹每帧下移 1px（16 帧一周期，32 帧 = 两周期循环）→ 水面呈「波纹下流动」感。
    流水列：左上→右下斜向条纹每帧沿 (+1,+1) 流动方向移 1px → 斜纹呈「向右下流动」感（机制等价 MC
    flowing_water flipbook）。两列各自独立动画，材质 positionV 同步驱动（静水/流水同帧索引）。
  - lava_strip.png：16 宽 × 256 高 = 1 列 × 16 行（每帧 16×16）。岩浆鼓泡每帧位移 + 白炽热点脉冲
    → 岩浆面呈「鼓泡翻涌」感（机制等价 MC lava flipbook）。

帧序约定（**帧 0 在图像底部**）：mesher 烘焙面 UV v∈[0,1/N]（帧 0 区，v=0=图像底）；材质 positionV=k/N
向上采样到帧 k（QtQuick3D Texture.positionV 在 6.11 已替代旧 vOffset，positive positionV 上移采样 → 帧 k
在 v∈[k/N,(k+1)/N]）。PIL y=0 在顶，故帧 k 占 PIL 行 [H-(k+1)*16, H-k*16-1]（帧 0 在底 16 行）。

动画节拍由 Main.qml Timer 驱动（水 ~150ms/帧、岩浆 ~250ms/帧），纯材质参数变化 → 零 mesh 重建
（修 t222/t223「水 2s 一次全量重建水段 261 段/次」mesh 重建风暴的回归）。

包覆盖：resourcepackmanager 启用包时（settings.json resourcePackEnabled=true），以本程序生成条带为
底、包内 water_still/water_flow/lava_still 帧覆盖对应列/行（缩放到 16×16）→ 落盘合成条带；包内帧不足
（如 lava_still 仅 20 帧 vs 16 帧常量）取前 N 帧后末尾补齐（循环）。无包时 QML 直接加载本 qrc 条带。

输出（覆盖写入 textures/）：
  water_strip.png
  lava_strip.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 帧像素边长（与图集瓦片 kTile=16 同源）

WATER_FRAMES = 32  # 与 BlockRegistry::kWaterStripFrames 一致
LAVA_FRAMES = 16   # 与 BlockRegistry::kLavaStripFrames 一致


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def hline(canvas, x0, x1, y, rgb):
    if not (0 <= y < TS):
        return
    for x in range(max(0, x0), min(TS, x1 + 1)):
        px(canvas, x, y, rgb)


def diag(canvas, x0, y0, length, rgb):
    for k in range(length):
        px(canvas, x0 + k, y0 + k, rgb)


def disc(canvas, cx, cy, r, rgb):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px(canvas, x, y, rgb)


def roll_y(canvas, dy):
    """纵向循环位移 dy 行（向下为正；TS 周期）——每帧把图案整体下移 dy px 实现「流动」感。"""
    return np.roll(canvas, dy, axis=0)


# ===== 水 =====
def water_base():
    """中蓝实心底（alpha=255：透明度由材质 opacity=0.7 控制）。"""
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0] = 58.0; c[..., 1] = 110.0; c[..., 2] = 165.0; c[..., 3] = 255.0
    return c


def draw_water_still(c):
    """横向波纹（亮/暗细带 + 高光点），与 default_water.png 同色系。"""
    light = np.array([110.0, 170.0, 215.0])
    dark = np.array([40.0, 84.0, 132.0])
    hi = np.array([195.0, 220.0, 240.0])
    hline(c, 1, 4, 2, light); hline(c, 9, 12, 2, dark)
    hline(c, 3, 6, 5, dark); hline(c, 10, 14, 5, light)
    hline(c, 0, 3, 8, light); hline(c, 7, 11, 8, dark); hline(c, 12, 15, 8, light)
    hline(c, 2, 5, 11, dark); hline(c, 9, 13, 11, light)
    hline(c, 4, 8, 14, light); hline(c, 11, 15, 14, dark)
    for (x, y) in [(2, 2), (11, 5), (1, 8), (12, 11), (6, 14)]:
        px(c, x, y, hi)


def draw_water_flow(c):
    """左上→右下斜向条纹（亮/暗短带 + 高光点），与 default_water_flow.png 同色系。"""
    light = np.array([110.0, 170.0, 215.0])
    dark = np.array([40.0, 84.0, 132.0])
    hi = np.array([195.0, 220.0, 240.0])
    diag(c, 1, 0, 4, light); diag(c, 0, 2, 3, dark)
    diag(c, 9, 1, 5, light); diag(c, 3, 4, 4, dark)
    diag(c, 11, 3, 4, light); diag(c, 6, 6, 5, dark)
    diag(c, 0, 7, 4, light); diag(c, 12, 7, 3, dark)
    diag(c, 4, 10, 5, light); diag(c, 10, 10, 4, dark)
    diag(c, 1, 12, 4, light); diag(c, 8, 12, 4, dark)
    for (x, y) in [(3, 2), (10, 4), (7, 7), (12, 9), (5, 12)]:
        px(c, x, y, hi)


def build_water_strip():
    """2 列 × 32 帧（静水 | 流水）。每帧纵向循环位移 1px → 流动感（16 帧一周期）。"""
    still0 = water_base(); draw_water_still(still0)
    flow0 = water_base(); draw_water_flow(flow0)
    # 帧序：帧 0 在底。组装为 numpy，再按「帧 k 放 PIL 行 [H-(k+1)*16, H-k*16]」翻转。
    still_frames = [roll_y(still0, k % TS) for k in range(WATER_FRAMES)]  # k=0..15 下移 k px（周期 16）
    flow_frames = [roll_y(flow0, k % TS) for k in range(WATER_FRAMES)]
    # 拼成 (WATER_FRAMES 行 × 2 列) 的 numpy，行 0 = 帧 0（待翻转到图像底）。
    rows = []
    for k in range(WATER_FRAMES):
        row = np.hstack([still_frames[k], flow_frames[k]])  # (16, 32, 4)
        rows.append(row)
    grid = np.vstack(rows)            # (512, 32, 4)，行 0 = 帧 0
    grid = np.flipud(grid)            # 翻转：帧 0 落到图像底（PIL 行 496..511）
    img = Image.fromarray(np.clip(grid, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, "water_strip.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


# ===== 岩浆 =====
def lava_base():
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0] = 168.0; c[..., 1] = 50.0; c[..., 2] = 26.0; c[..., 3] = 255.0
    return c


def draw_lava(c):
    hot = np.array([255.0, 196.0, 60.0])
    crust = np.array([120.0, 28.0, 16.0])
    white = np.array([255.0, 240.0, 200.0])
    disc(c, 3, 3, 2, hot); disc(c, 12, 4, 1, hot); disc(c, 8, 7, 2, hot)
    disc(c, 2, 11, 1, hot); disc(c, 13, 12, 2, hot); disc(c, 7, 13, 1, hot)
    disc(c, 6, 2, 1, crust); disc(c, 11, 9, 1, crust); disc(c, 4, 8, 1, crust)
    disc(c, 14, 7, 1, crust); disc(c, 9, 11, 1, crust)
    for (x, y) in [(3, 3), (8, 7), (13, 12), (12, 4)]:
        px(c, x, y, white)


def build_lava_strip():
    """1 列 × 16 帧。每帧纵向循环位移 1px → 鼓泡翻涌感。"""
    lava0 = lava_base(); draw_lava(lava0)
    frames = [roll_y(lava0, k % TS) for k in range(LAVA_FRAMES)]
    grid = np.vstack(frames)            # (256, 16, 4)，行 0 = 帧 0
    grid = np.flipud(grid)              # 帧 0 落到图像底
    img = Image.fromarray(np.clip(grid, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, "lava_strip.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    build_water_strip()
    build_lava_strip()


if __name__ == "__main__":
    main()
