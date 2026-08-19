#!/usr/bin/env python3
"""生成 t717 铁门 / 铁活板门贴图（16×16 像素，原创自绘，§9 override (a)）。

R19.10 t722/t723 铁门（IronDoor 上下两格）+ 铁活板门（IronTrapdoor）的贴图前置。机制等价
MC 1.0 iron door / iron trapdoor（仅红石驱动开合）；名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产）。
与 build_door.py（木门）同构图语言：上半 = 门板 + 格栅窗（窗洞真透明——铁门走 cutout 段，
pack 侧 door_iron_upper.png 自带 4 孔窗实测）；下半 = 门板 + 底部横带 + 锁孔板。

视觉意图（铁灰金属门，区别木门暖棕）：
  - door_iron_upper（tile 176）：铁灰门板 + 铆钉列 + **下窗**（2×2 孔格栅——pack 实测窗区在
    中下部）；窗洞 alpha=0 真透明（cutout）。
  - door_iron_lower（tile 177）：铁灰门板 + 铆钉 + 底部横带 + 中央锁孔板（暗孔）。
  - iron_trapdoor（tile 178）：铁灰格子板——四角铆钉 + 十字格条 + 中部栅格孔（pack 实测
    两列 3+3 孔；透明孔 cutout 透视）。

输出（覆盖写入 textures/）：
  default_door_iron_upper.png  （tile 176，铁门上半：门板 + 下部 2×2 格栅窗）
  default_door_iron_lower.png  （tile 177，铁门下半：门板 + 锁孔板 + 底部横带）
  default_iron_trapdoor.png    （tile 178，铁活板门：格子板 + 栅格孔）

依赖：仅 PIL/numpy，无外部贴图。与 build_door.py（木门）/ build_lever_button.py 同风格。
pack 运行期映射 tileFilenameMap {176→door_iron_upper.png / 177→door_iron_lower.png /
178→iron_trapdoor.png}（demo 包实测 door_iron_*（HD 128px）与 iron_trapdoor.png 都在）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 铁金属色板（与 default_iron_block.png / build_anvil.py 铁色同族）。
IRON = {
    "plate":  np.array([176.0, 178.0, 182.0]),   # 门板底（铁灰亮面）
    "plate2": np.array([156.0, 158.0, 164.0]),   # 门板暗纹（轧制竖纹）
    "edge":   np.array([104.0, 106.0, 112.0]),   # 边框暗化（收边）
    "rivet":  np.array([216.0, 218.0, 224.0]),   # 铆钉（高光亮点）
    "band":   np.array([132.0, 134.0, 140.0]),   # 底部横带（压条）
    "lock":   np.array([188.0, 190.0, 196.0]),   # 锁孔板（亮一档嵌板）
    "hole":   np.array([40.0, 42.0, 48.0]),      # 锁孔（暗孔）
}

# 确定性伪随机（同 seed 同图案；与 build_door.py 模式一致）。
_RNG = np.random.RandomState(7172)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def plate_base():
    """铁门板底：铁灰 + 竖向轧制暗纹（确定性）+ 左右边框暗化。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0:3] = IRON["plate"]
    canvas[..., 3] = 255.0
    for _ in range(4):
        x0 = int(_RNG.randint(2, TS - 2))
        for y in range(TS):
            px(canvas, min(x0 + (1 if (y + x0) % 6 == 0 else 0), TS - 1), y, IRON["plate2"])
    for y in range(TS):
        canvas[y, 0, 0:3] = IRON["edge"]
        canvas[y, TS - 1, 0:3] = IRON["edge"]
    return canvas


def rivet_col(canvas, x, ys):
    """一列铆钉（x 固定，ys 各一粒 1px 亮点 + 下邻 1px 暗晕）。"""
    for y in ys:
        px(canvas, x, y, IRON["rivet"])
        px(canvas, x, y + 1, IRON["band"])


def draw_upper():
    """铁门上半：门板 + 下部 2×2 格栅窗（真透明）+ 铆钉列 + 顶部边框。

    窗区按 pack door_iron_upper.png 实测构图意图（窗洞集中在中下段、上下各留门板带）：
    行带 [9,11) / [12,14)，列带 [4,7) / [9,12)，中央十字格条（竖 x=8 不开孔）分四孔。
    pack 实测（door_iron_upper HD 版）窗形为 4 孔（2 列×若干行、列间隔条），此处取 2×2
    四孔与木门格栅语言统一；格条颜色用压条暗铁。"""
    c = plate_base()
    # 顶部边框。
    for x in range(TS):
        c[0, x, 0:3] = IRON["edge"]
    # 上部门板带的横向压条（y=3 分隔上板带与窗区）。
    for x in range(1, TS - 1):
        c[3, x, 0:3] = IRON["band"]
    # 窗区（2×2 孔，真透明 alpha=0——铁门走 cutout 段 discard）。
    for y in (9, 10, 12, 13):
        for x in range(4, 7):
            c[y, x, 0:3] = IRON["hole"]; c[y, x, 3] = 0.0
        for x in range(9, 12):
            c[y, x, 0:3] = IRON["hole"]; c[y, x, 3] = 0.0
    # 窗框缘（窗区外圈压条，不透明）。
    for x in range(3, 13):
        c[8, x, 0:3] = IRON["edge"]
        c[14, x, 0:3] = IRON["edge"]
    for y in range(8, 15):
        c[y, 3, 0:3] = IRON["edge"]
        c[y, 12, 0:3] = IRON["edge"]
    # 铆钉列（门板两侧带 x=2/13，上板带 y=1..6 每隔 2px）。
    rivet_col(c, 2, [1, 4, 6])
    rivet_col(c, 13, [1, 4, 6])
    return c


def draw_lower():
    """铁门下半：门板 + 中央锁孔板（亮嵌板 + 暗锁孔）+ 底部横带 + 铆钉。"""
    c = plate_base()
    # 底部横带 y[12,14)。
    for y in range(12, 14):
        for x in range(TS):
            c[y, x, 0:3] = IRON["band"]
    # 锁孔板：中央亮嵌板 x[6,10) × y[5,10)。
    for y in range(5, 10):
        for x in range(6, 10):
            c[y, x, 0:3] = IRON["lock"]
    # 锁孔（嵌板中央暗点 + 下方短槽）。
    c[6, 7, 0:3] = IRON["hole"]; c[6, 8, 0:3] = IRON["hole"]
    c[7, 7, 0:3] = IRON["hole"]; c[7, 8, 0:3] = IRON["hole"]
    c[8, 7, 0:3] = IRON["hole"]
    # 铆钉列（门板两侧带，上段 y=1..4；锁孔板下方 y=10）。
    rivet_col(c, 2, [1, 3, 5, 7, 9])
    rivet_col(c, 13, [1, 3, 5, 7, 9])
    return c


def draw_trapdoor():
    """铁活板门：铁灰格子板——边框 + 十字格条 + 中部两列栅格孔（真透明，pack 实测构图意图：
    边框实心、内部 2 列×3 行孔阵）+ 四角铆钉。"""
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = IRON["plate"]
    c[..., 3] = 255.0
    # 细轧纹（轻量，区别纯平）。
    mask = _RNG.random((TS, TS)) < 0.10
    c[mask, 0:3] = IRON["plate2"]
    # 外框（2px 压条 + 外缘暗化）。
    for i in range(TS):
        for k in (0, 1):
            c[k, i, 0:3] = IRON["edge"] if k == 0 else IRON["band"]
            c[TS - 1 - k, i, 0:3] = IRON["edge"] if k == 0 else IRON["band"]
            c[i, k, 0:3] = IRON["edge"] if k == 0 else IRON["band"]
            c[i, TS - 1 - k, 0:3] = IRON["edge"] if k == 0 else IRON["band"]
    # 中部栅格孔（2 列 × 3 行，真透明——pack iron_trapdoor.png 实测孔阵位）。
    for y in (5, 8, 11):
        for x in (4, 5, 10, 11):
            c[y, x, 0:3] = IRON["hole"]
            c[y, x, 3] = 0.0
    # 中央十字格条（孔阵之间的实心格，压条色）。
    for y in range(4, 13):
        for x in (7, 8):
            c[y, x, 0:3] = IRON["band"]
    for x in range(4, 12):
        for y in (6, 7, 9, 10):
            c[y, x, 0:3] = IRON["band"]
            c[y, x, 3] = 255.0
    # 四角铆钉（框角内侧 1px 亮点 + 暗晕）。
    for (rx, ry) in ((3, 3), (TS - 4, 3), (3, TS - 4), (TS - 4, TS - 4)):
        c[ry, rx, 0:3] = IRON["rivet"]
        c[ry + 1, rx, 0:3] = IRON["band"]
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_upper(), "default_door_iron_upper")     # tile 176
    save(draw_lower(), "default_door_iron_lower")     # tile 177
    save(draw_trapdoor(), "default_iron_trapdoor")    # tile 178


if __name__ == "__main__":
    main()
