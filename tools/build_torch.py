#!/usr/bin/env python3
"""生成火把（Torch）方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 火把（小光源方块，solid=false 非实体碰撞、hardness=0 瞬破），但贴图为本项目
程序生成的原创像素图，**不**拷贝任何 MC 资产。

本工程 mesher 只产立方体几何（无 cross-billboard 特殊形），火把被当作「6 面同贴图的 1×1×1
立方体」渲染。故贴图须在 16×16 内自含一个完整可读的火把图案（木柄 + 火焰），让任一面都读
得出「这是火把」；底色取近黑暖棕（非透明）——透明底在 PrincipledMaterial 默认 Mask blend 下
会被当不透明渲染成黑块（lessons-learned「裂纹叠层」同族坑），故走实心暗底。

图案（固定，无随机源 → 确定性，便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 底色：近黑暖棕（#1a120a），读作「未点亮 / 焦炭」暗块；
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
    """近黑暖棕底（实心 alpha=255；防 Mask blend 渲成黑块，lessons-learned）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 26.0   # R
    canvas[..., 1] = 18.0   # G
    canvas[..., 2] = 10.0   # B
    canvas[..., 3] = 255.0  # A（实心）
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


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
