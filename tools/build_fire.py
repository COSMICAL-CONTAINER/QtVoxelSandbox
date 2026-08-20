#!/usr/bin/env python3
"""t724 生成火焰「条带」贴图（fireHost delegate 的材质级 flipbook 动画）。

机制等价 MC 1.0 火 flipbook（fire_0/fire_layer 逐层动画），但贴图为本项目程序生成的原创像素图，**不**
拷贝任何 MC 资产（§9 override (a)）。

条带结构（与 Main.qml fireStripTex 三方共用 BlockRegistry::kFireStripFrames 常量；改帧数必须同步）：
  - fire_strip.png：16 宽 × 512 高 = 1 列 × 32 行（每帧 16×16）。
    火焰轮廓 = 下宽上尖的双侧内收锯齿；帧动画 = 纵向循环位移 1px（火舌上舔感，16 帧一周期，32 帧
    = 两周期循环）+ 白炽热点逐帧错位（火花闪烁感）。透明底（alpha=0）—— 火焰是 cutout 式非满格贴图
    （delegate quad 全 [0,1] UV + 材质 Blend 混合，与水 / 岩浆的满格实心底不同）。

帧序约定（**帧 0 在图像底部、帧内容保持原方向**；同 build_fluid_strips.py t563 ② 修复后的约定——
不 flipud，rows.reverse() 后 vstack）。注意：火焰路走 QML delegate（fireStripTex），帧区采 UV 全 [0,1]
+ Texture scaleV=1/N + positionV=k/N（与水 / 岩浆的 chunk-mesh 路不同源：mesher 烘焙 UV v∈[0,1/N]）。

包覆盖：resourcepackmanager 启用包时，以本程序生成条带为底、包内 block/fire_0.png 帧覆盖（demo 包
实测 16×512 = 32 帧现成 strip，帧数天然与本常量对齐）→ 落盘合成条带。无包时 QML 直接加载本 qrc 条带。

输出（覆盖写入 textures/）：
  fire_strip.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 帧像素边长（与图集瓦片 kTile=16 同源）

FIRE_FRAMES = 32  # 与 BlockRegistry::kFireStripFrames 一致


def px(canvas, x, y, rgb, a=255.0):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb
        canvas[y, x, 3] = a


def flame_column(canvas, cx, base_w, top_y, core_rgb, edge_rgb):
    """画一条自底向上的火舌：底部宽 base_w，向上逐行收窄到 top_y。core 在内、edge 镶边。"""
    for y in range(top_y, TS):
        # 行 y 的半宽：自底 (TS-1) 的 base_w/2 线性收窄到顶行的 1px。
        t = (TS - 1 - y) / max(1, TS - 1 - top_y)
        half = max(0.0, base_w / 2.0 * (1.0 - t) + 0.5)
        for x in range(int(cx - half), int(cx + half) + 1):
            inner = abs(x - cx) < max(1.0, half * 0.55)
            px(canvas, x, y, core_rgb if inner else edge_rgb)


def spark(canvas, x, y, white_rgb):
    px(canvas, x, y, white_rgb)


def build_fire_strip():
    """1 列 × 32 帧。每帧纵向循环位移 1px → 火舌上舔感 + 白炽热点错位闪烁。帧 0 在底（同水/岩浆 t563 ②）。"""
    orange = np.array([232.0, 96.0, 16.0])     # 主体橙（外焰）
    deep = np.array([188.0, 44.0, 8.0])        # 深橙红（边缘）
    yellow = np.array([255.0, 202.0, 48.0])    # 焰心黄
    white = np.array([255.0, 246.0, 208.0])    # 白炽热点

    base = np.zeros((TS, TS, 4), dtype=np.float64)  # 透明底（cutout 式）
    # 三条火舌：中主 + 两侧辅，底部宽、顶部尖（错落高度 → 轮廓锯齿感）。
    flame_column(base, 7.0, 10, 2, yellow, orange)   # 主火舌（近中心，高）
    flame_column(base, 3.0, 6, 8, orange, deep)      # 左辅火舌（矮）
    flame_column(base, 12.0, 5, 6, orange, deep)     # 右辅火舌（中）
    # 白炽热点（焰心顶部 + 边缘火星）。
    spark(base, 7, 2, white); spark(base, 8, 3, white)
    spark(base, 3, 8, white); spark(base, 12, 6, white)
    spark(base, 5, 13, white); spark(base, 10, 12, white)

    # 帧动画：纵向循环位移 k px（火舌整体上舔）+ 热点横向抖动（np.roll x 轴 ±1 交替 → 闪烁）。
    frames = []
    for k in range(FIRE_FRAMES):
        f = np.roll(base, k % TS, axis=0)              # 纵向上移（PIL y 向下为正 → roll 负方向 = 上舔；用正 k 后主视觉同 t563「内容保持原方向」）
        if k % 4 == 1 or k % 4 == 2:                   # 每 4 帧中 2 帧横向错位 1px（火花闪烁）
            f = np.roll(f, 1, axis=1)
        frames.append(f)
    frames.reverse()                    # 反序：帧 0 落到图像底（PIL 行 496..511），帧 31 在顶（t563 ② 约定）
    grid = np.vstack(frames)            # (512, 16, 4)，行 0 = 帧 31
    img = Image.fromarray(np.clip(grid, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, "fire_strip.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    build_fire_strip()


if __name__ == "__main__":
    main()
