#!/usr/bin/env python3
"""生成耕地（Farmland）方块的顶面贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 耕地（持锄右键泥土/草方块→耕地；干/湿两态由水源邻近判定湿润），但贴图为本项目
程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「翻耕过的农田土」——比泥土更深、更规整，带纵向犁沟纹（耕地被犁过的痕迹）。
  - dry（farmland_dry，tile 26）：浅棕色翻耕干土（比泥土略深、偏灰褐）+ 纵向（南北向）犁沟纹（深色细沟）；
  - wet（farmland_wet，tile 27）：深棕色湿润翻耕土（饱和更深、偏暗，似被水浸润）+ 同犁沟纹（更深）。
  dry/wet 仅色深区别（机制等价 MC 耕地干/湿仅顶面色深异），mesher 据 Farmland state bit0 选 26/27。

图案（固定位置色块 + 确定性散布，无随机源 → 便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 基底：dry 灰褐 / wet 深棕（实心 alpha=255，耕地为不透明方块，半透由材质不参与）；
  - 纵向犁沟：数条南北向（沿 Z 轴、画布 y）深色细带，刻意等距（人工犁沟的规整感，区别于自然泥土的随机斑驳）；
  - 翻耕土块：少量亮暗散点（拟翻起的土块明暗），dry 偏亮、wet 偏暗。

输出（覆盖写入 textures/）：
  default_farmland_dry.png   （tile 26，干态顶面）
  default_farmland_wet.png   （tile 27，湿态顶面）

依赖：仅 PIL/numpy，无外部贴图。与 build_water.py / build_bedrock.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def blank(base_rgb):
    """单色实心底（alpha=255：耕地不透明）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = base_rgb[0]
    canvas[..., 1] = base_rgb[1]
    canvas[..., 2] = base_rgb[2]
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    """单像素写入（越界忽略）。"""
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def vline(canvas, x, y0, y1, rgb):
    """纵向（画布 y）线段（含端点）—— 犁沟沿画布 y（世界 Z）方向。"""
    if not (0 <= x < TS):
        return
    for y in range(max(0, y0), min(TS, y1 + 1)):
        px(canvas, x, y, rgb)


def draw_farmland(canvas, wet):
    """翻耕农田土：色基底 + 纵向犁沟（深色细带）+ 少量翻耕土块散点。

    wet=True（湿态）整体更深更饱和（似水浸润）；wet=False（干态）偏灰褐（干土）。
    犁沟纹位置 dry/wet 相同（干/湿仅色深异，机制对齐 MC）；犁沟色 wet 更深。
    """
    if wet:
        furrow = np.array([90.0, 58.0, 36.0])    # 深棕犁沟（湿润深土沟）
        clod_light = np.array([110.0, 74.0, 46.0])  # 亮土块（翻起的湿土）
        clod_dark = np.array([70.0, 44.0, 28.0])    # 暗土块
    else:
        furrow = np.array([122.0, 92.0, 62.0])   # 灰褐犁沟（干土沟）
        clod_light = np.array([150.0, 120.0, 86.0])  # 亮干土块
        clod_dark = np.array([108.0, 82.0, 56.0])    # 暗干土块

    # 纵向犁沟（南北向、画布 y 全长）：每隔 3-4 像素一条，规整等距（人工翻耕痕迹）。
    #   沟位：x=1, 5, 9, 13（4 条均匀犁沟，覆盖 16 像素宽，与 MC 耕地犁沟纹密度量级一致）。
    for x in (1, 5, 9, 13):
        vline(canvas, x, 0, TS - 1, furrow)

    # 翻耕土块散点（少量明暗点，拟翻起的土块；刻意避开犁沟列 → 落在沟间垄上）。
    #   垄位列：x=3, 7, 11；散点位置确定性（无随机源）。
    clod_pts = [
        (3, 2, clod_light), (7, 3, clod_dark), (11, 2, clod_light),
        (3, 6, clod_dark), (7, 7, clod_light), (11, 6, clod_dark),
        (3, 10, clod_light), (7, 11, clod_dark), (11, 10, clod_light),
        (3, 14, clod_dark), (7, 15, clod_light), (11, 14, clod_dark),
    ]
    for (x, y, rgb) in clod_pts:
        px(canvas, x, y, rgb)


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # dry（farmland_dry，tile 26）：浅灰褐干土基底。
    dry = blank(np.array([138.0, 108.0, 76.0]))  # 灰褐干土（比泥土 default_dirt 略深、偏灰）
    draw_farmland(dry, wet=False)
    save(dry, "default_farmland_dry")
    # wet（farmland_wet，tile 27）：深棕湿土基底。
    wet = blank(np.array([100.0, 66.0, 40.0]))   # 深棕湿土（饱和深，似水浸润）
    draw_farmland(wet, wet=True)
    save(wet, "default_farmland_wet")


if __name__ == "__main__":
    main()
