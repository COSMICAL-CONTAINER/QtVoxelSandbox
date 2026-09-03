#!/usr/bin/env python3
"""生成铁栏杆方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t998 结构新方块包（要塞逐方块还原前置；机制等价 MC 1.0 iron bars——要塞窗棂 / 栏杆的金属薄杆栅格）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：暗铁缝底 + 四亮铁竖条 + 居中横带，
读作「金属栏杆格」。

视觉意图：读作「竖向铁条栅格」——
  - 竖条：四根亮铁竖条（周期 4：x ≡ 0,1 (mod 4) 两列一组，2px 宽）满高贯穿 —— 栏杆主体。
  - 缝底：暗铁缝色（条间空隙），薄杆几何的细面整张压缩采样时读作「铁条间隙」。
  - 横带：y 7..8 两行亮铁横带贯穿全部列 —— 连接横板的贴图语义（与几何横板 y[7/16,9/16] 同高对齐）。
  - **alpha 恒不透明（255）**：薄柱 / 横板各面把整张瓦片压缩采样（pushBox 单位 UV）——若留透明孔，
    2px 细面上会采到透明列致整面消隐（Mask discard）；以暗缝底达成同读感且任意面尺寸稳健。
    （与铁活板门 178「大面真透明」的取舍差异：活板门大面是满格平面、孔语义成立；栏杆全部面皆薄杆。）

输出（覆盖写入 textures/）：
  default_iron_bars.png   （tile 183，铁栏杆各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_stone_brick.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(9983)

# 色板：亮铁条（主 / 高光）/ 暗铁缝底 / 横带亮铁。
BAR = np.array([168.0, 172.0, 178.0])      # 竖条主体（亮铁）
BAR_HI = np.array([188.0, 192.0, 198.0])   # 竖条左列高光（金属反光棱线）
GAP = np.array([52.0, 54.0, 58.0])         # 条间暗缝底（近黑铁影）
BAND = np.array([178.0, 182.0, 188.0])     # 横带（连接横板语义，比竖条略亮）


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def draw_face():
    """铁栏杆面：暗缝底 + 周期 4 亮铁竖条（满高）+ y7..8 横带贯穿。alpha 恒 255。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = GAP[0]
    canvas[..., 1] = GAP[1]
    canvas[..., 2] = GAP[2]
    canvas[..., 3] = 255.0
    # 缝底微噪声（金属影深浅差）。
    g1 = _RNG.random((TS, TS)) < 0.18
    canvas[g1, 0:3] = GAP * 0.82
    # 竖条：x ≡ 0,1 (mod 4) 两列一组（[0,1] [4,5] [8,9] [12,13] 四条 × 满高）。
    #   组内左列（x%4==0）用高光列、右列用主体 —— 每条带一道反光棱线。
    for y in range(TS):
        for x in range(TS):
            if x % 4 == 0:
                px(canvas, x, y, BAR_HI)
            elif x % 4 == 1:
                px(canvas, x, y, BAR)
    # 竖条明暗微噪声（锻打质感；仅压暗不越缝 —— 探针钉「条亮缝暗」带阈值间距）。
    for y in range(TS):
        for x in range(TS):
            if x % 4 < 2 and _RNG.random() < 0.22:
                px(canvas, x, y, BAR * 0.90)
    # 横带：y 7..8 两行亮铁贯穿全部列（连接横板贴图语义；盖写缝底与竖条 —— 通层亮带）。
    for y in (7, 8):
        for x in range(TS):
            px(canvas, x, y, BAND)
    # 边框微暗化（贴图四边 1 像素压暗，拟杆件边缘倒角）。
    edge = GAP * 0.9
    for i in range(TS):
        px(canvas, i, TS - 1, edge)
        px(canvas, TS - 1, i, edge)

    return canvas


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # 铁栏杆各面同贴图（薄杆几何各面统一用 tile 183）。
    save(draw_face(), "default_iron_bars")


if __name__ == "__main__":
    main()
