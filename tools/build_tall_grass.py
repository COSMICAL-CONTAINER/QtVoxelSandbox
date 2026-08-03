#!/usr/bin/env python3
"""生成草丛（TallGrass）cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 草丛 / 蕨类（tall grass / fern）—— cross 模型（两片对角相交平面）上贴本瓦片。
贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「一丛直立草叶」—— 透明底（alpha=0）+ 几条竖向绿色草叶（alpha=255）。mesher 把本瓦片
  贴到 cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5 丢弃透明底 → 仅草叶像素显（机制等价
  MC cutout 草叶）。透明底是关键：若无 alpha（实心底），cross 会显成两片实心绿板（非草丛）。

图案（固定位置 + 确定性散布，无随机源 → 便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 透明底（alpha=0）；
  - 5-7 条竖向草叶（画布 y 自下而上生长），宽 1 像素、高低参差、横向分布在画布中段（留边距）；
  - 草叶配色两档（亮面 / 暗面）拟受光明暗；顶端略浅（嫩叶）、底端略深（老叶 / 阴影）。

输出（覆盖写入 textures/）：
  default_tall_grass.png   （tile 28，草丛 cross 贴图）

依赖：仅 PIL，无外部贴图。与 build_farmland.py / build_water.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def main():
    # 透明底（alpha=0）。RGBA，草叶像素后填 alpha=255。
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()

    def blade(x, top_y, light_rgb, dark_rgb):
        """竖向草叶：列 x 自画布底（y=TS-1）向上长到 top_y（含），上端浅、下端深。
        top_y 越小 → 草叶越高（画布 y 向下，顶在上）。"""
        for y in range(top_y, TS):
            # 顶端 1-2 像素用亮色（嫩叶），其余渐暗到暗色（老叶）。
            t = (y - top_y) / max(1, TS - 1 - top_y)  # 0（顶）..1（底）
            r = int(light_rgb[0] * (1 - t) + dark_rgb[0] * t)
            g = int(light_rgb[1] * (1 - t) + dark_rgb[1] * t)
            b = int(light_rgb[2] * (1 - t) + dark_rgb[2] * t)
            px[x, y] = (r, g, b, 255)

    # 草叶两档配色（草绿）：亮面 #6fae3a（嫩绿受光）/ 暗面 #3a6a1a（深绿阴影）。
    light = (0x6f, 0xae, 0x3a)
    dark = (0x3a, 0x6a, 0x1a)
    # 草叶列 + 顶端 y（参差高度）。列分布于画布中段 [3,12]（留左右边距），顶端 y 参差 [3,8]。
    #   固定位置（无随机源 → 可复现 / CI 可校验）。
    blades = [
        (4, 5),   # 高草叶
        (6, 3),   # 最高草叶（中央）
        (8, 4),
        (10, 6),
        (11, 8),  # 矮草叶
        (7, 7),
    ]
    for (x, top_y) in blades:
        blade(x, top_y, light, dark)

    out = os.path.join(SRC, "default_tall_grass.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
