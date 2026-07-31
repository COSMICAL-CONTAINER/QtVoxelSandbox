#!/usr/bin/env python3
"""生成火把（Torch）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 火把（小光源方块，solid=false 非实体碰撞、hardness=0 瞬破），但贴图为本项目
程序生成的原创像素图，**不**拷贝任何 MC 资产。

火把在游戏中由异形渲染（Main.qml torchHost：木柄 + 火焰小立方，t114），mesher 对 Torch 特例
continue（不画 1×1×1 立方面，见 chunkgeometry.cpp），故贴图不参与世界内立方体面渲染。贴图采用
**全透明底**（alpha=0），只画火把本体（木柄 + 火焰）；背包/hotbar 图标走平面 2D 放大
（build_cube_icons.py torch 分支保留 alpha），呈现「纯火把无方块底」。t88 早期「实心暗底防 Mask
blend 渲成黑块」的前提（火把当 1×1×1 立方体渲染）已随 t114 异形化失效，故回退到透明底。

图案（固定，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 底色：全透明（alpha=0）；
  - 木柄：垂直棕色矩形（#6b4a2a）居中、下半段；
  - 火焰：木柄上方黄→橙→暖白三段渐变焰心（外橙 #ff8a1a、中黄 #ffd23c、心 #fff4c4）。

输出（覆盖写入 textures/）：
  default_torch.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank():
    """全透明底（alpha=0）。火把本体由 draw_torch 写入的像素经 px() 单独置 alpha=255。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 3] = 0.0  # A（全透明底；RGB 留 0，透明像素的 RGB 不被采样故无所谓）
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。同时置 alpha=255（火把本体不透明，与全透明底区分）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb
        canvas[y, x, 3] = 255.0


def rect(canvas, x0, y0, x1, y1, rgb):
    """实心矩形（含端点）。"""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def draw_torch(canvas):
    """木柄（垂直棕色）+ 火焰（黄橙暖白渐变焰心）。"""
    # 木柄：columns 7-8、rows 6-13（下半段，留上方空间给火焰）
    wood = np.array([107.0, 74.0, 42.0])    # 棕色木柄
    wood_dark = np.array([74.0, 50.0, 28.0])  # 暗棕（侧阴影）
    rect(canvas, 7, 6, 8, 13, wood)
    px(canvas, 7, 6, wood_dark)              # 左上角暗化（拟光源右上）
    px(canvas, 7, 13, wood_dark)             # 底部暗化（接地）

    # 火焰：木柄顶部（rows 1-5）。外橙 → 中黄 → 暖白心，自下而上收窄成焰形。
    flame_outer = np.array([255.0, 138.0, 26.0])   # 外焰橙
    flame_mid   = np.array([255.0, 210.0, 60.0])   # 中焰黄
    flame_core  = np.array([255.0, 244.0, 196.0])  # 焰心暖白

    # 第 5 行（紧贴木柄顶）：宽底 5..10
    for x in range(5, 11):
        px(canvas, x, 5, flame_outer)
    px(canvas, 7, 5, flame_mid)
    px(canvas, 8, 5, flame_mid)

    # 第 4 行：6..9
    for x in range(6, 10):
        px(canvas, x, 4, flame_outer)
    px(canvas, 7, 4, flame_mid)
    px(canvas, 8, 4, flame_mid)

    # 第 3 行：7..8（开始收窄）
    px(canvas, 7, 3, flame_outer)
    px(canvas, 8, 3, flame_outer)
    px(canvas, 7, 3, flame_mid)

    # 第 2 行：焰尖
    px(canvas, 7, 2, flame_mid)
    px(canvas, 8, 2, flame_outer)

    # 第 1 行：顶端焰心高光
    px(canvas, 7, 1, flame_core)

    # 木柄顶部接焰处暖化（火焰照亮木柄顶）
    px(canvas, 7, 6, np.array([138.0, 92.0, 50.0]))
    px(canvas, 8, 6, np.array([138.0, 92.0, 50.0]))


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    c = blank()
    draw_torch(c)
    save(c, "default_torch")


if __name__ == "__main__":
    main()
