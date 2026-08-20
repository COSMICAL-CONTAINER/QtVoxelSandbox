#!/usr/bin/env python3
"""t725 生成余烬门「条带」贴图（portalHost delegate 的材质级 flipbook 动画）。

机制等价 MC 1.0 下界传送门 flipbook（nether_portal.png 单列 32 帧），但贴图为本项目程序生成的原创
像素图，**不**拷贝任何 MC 资产（§9 override (a)）。

条带结构（与 Main.qml portalStripTex 三方共用 BlockRegistry::kNetherPortalStripFrames 常量；改帧数必须同步）：
  - portal_strip.png：16 宽 × 512 高 = 1 列 × 32 行（每帧 16×16）。
    门面 = 满格紫色漩涡：多层正弦扰动纹（横向波纹沿纵向流动）+ 中心亮核（门心偏亮的紫白渐变）。
    与火焰的 cutout 透明底不同，门是**满格半透明面**（边缘 alpha 高、纹谷 alpha 略低 → 微透感，
    Blend 混合而非 Mask cutout；机制对标 MC 门面软半透明观感，程序回退取同语义的软 alpha）。

帧序约定（**帧 0 在图像底部、帧内容保持原方向**；同 build_fire.py / build_fluid_strips.py t563 ②
修复后的约定——不 flipud，rows.reverse() 后 vstack）。门路走 QML delegate（portalStripTex），帧区采
UV 全 [0,1] + Texture scaleV=1/N + positionV=k/N（与水 / 岩浆的 chunk-mesh 路不同源：mesher 烘焙
UV v∈[0,1/N]）。

包覆盖：resourcepackmanager 启用包时，以本程序生成条带为底、包内 block/nether_portal.png 帧覆盖
（demo 包实测 16×512 = 32 帧现成 strip，帧数天然与本常量对齐）→ 落盘合成条带。无包时 QML 直接加载
本 qrc 条带。

输出（覆盖写入 textures/）：
  portal_strip.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 帧像素边长（与图集瓦片 kTile=16 同源）

PORTAL_FRAMES = 32  # 与 BlockRegistry::kNetherPortalStripFrames 一致


def build_portal_strip():
    """1 列 × 32 帧。三层正弦波纹相位逐帧推进（0..2π 环）→ 漩涡涌动感。帧 0 在底（同水/岩浆 t563 ②）。"""
    deep = np.array([64.0, 18.0, 112.0])    # 深紫底（暗紫）
    mid = np.array([122.0, 32.0, 178.0])    # 主体紫
    bright = np.array([186.0, 84.0, 232.0]) # 亮紫（波峰）
    core = np.array([236.0, 200.0, 252.0])  # 门心紫白（漩涡核）

    frames = []
    for k in range(PORTAL_FRAMES):
        phase = 2.0 * np.pi * k / PORTAL_FRAMES  # 帧相位（32 帧一整环）
        canvas = np.zeros((TS, TS, 4), dtype=np.float64)
        for y in range(TS):
            for x in range(TS):
                # 以格中心为原点的归一化坐标。
                nx = (x - 7.5) / 7.5
                ny = (y - 7.5) / 7.5
                r = float(np.sqrt(nx * nx + ny * ny))   # 距中心半径（0..~1.41）
                # 三层正弦波纹：半径向波（环纹）+ 两向斜交纹 → 干涉出漩涡质感；相位随帧推进。
                w1 = np.sin(r * 3.5 * np.pi - phase)
                w2 = np.sin((nx * 2.2 + ny * 1.4) * np.pi + phase * 0.7)
                w3 = np.sin((ny * 2.6 - nx * 1.1) * np.pi + phase * 1.3)
                t = (w1 * 0.5 + w2 * 0.3 + w3 * 0.2) / 1.0  # 加权 → [-1,1]
                tv = (t + 1.0) * 0.5                       # 折到 [0,1]（0=暗谷 1=亮峰）
                # 颜色：暗谷深紫 → 中段主体紫 → 波峰亮紫（三段插值）。
                if tv < 0.5:
                    c = deep + (mid - deep) * (tv / 0.5)
                else:
                    c = mid + (bright - mid) * ((tv - 0.5) / 0.5)
                # 门心亮核：中心 r<0.35 区向紫白渐变（越近心越亮）。
                if r < 0.35:
                    c = c + (core - c) * (1.0 - r / 0.35) * 0.8
                # 软 alpha：满格不透明为底、波谷微透（Blend 半透明面；机制对标 MC 门面软半透明观感）。
                a = 168.0 + 56.0 * tv                     # [168, 224]（软渐变，非 0/255 cutout）
                canvas[y, x, 0:3] = c
                canvas[y, x, 3] = a
        frames.append(canvas)
    frames.reverse()                    # 反序：帧 0 落到图像底（PIL 行 496..511），帧 31 在顶（t563 ② 约定）
    grid = np.vstack(frames)            # (512, 16, 4)，行 0 = 帧 31
    img = Image.fromarray(np.clip(grid, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, "portal_strip.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    build_portal_strip()


if __name__ == "__main__":
    main()
