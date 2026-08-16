#!/usr/bin/env python3
"""生成矿物存储块 + 红石灯的贴图（16×16 像素，原创自绘，§9 override (a)）。

t620 矿物存储块（机制等价 MC 1.0 coal/lapis/diamond/gold/redstone block；铁块
default_iron_block.png 已由 build_anvil.py t477 生成，本脚本补其余五种）——
9 材料 ↔ 1 块 双向配方的「压缩存储」方块，六面同贴图（机制等价 MC 1.0 存储块
六面一张图）。红石灯（redstone_lamp_off/on）两态贴图：off = 灰暗壳 + 中央红石
芯；on = 暖黄亮芯（发光态）。名称 / 贴图全原创自绘（§9 区隔，零 MC 资产）。

视觉意图（机制等价非美术复刻）：「切割整齐的矿物压缩块」—— 底色 = 对应材料
主色，加 4×4 镶格暗缝 + 左上高光 + 右下暗边读出「整块切割面」；红石块额外撒
矿粒亮红点（同矿石族斑点语言）；红石灯 off 是「哑壳」（暗黄褐 + 红石芯暗纹）、
on 是「亮芯」（暖黄自发光感 + 中心白热）。

色块位置固定（无随机源；矿粒用固定种子 RNG）→ 同输入同输出（确定性，与
build_atlas.py 顺序对齐）。

输出（覆盖写入 textures/）：
  default_coal_block.png
  default_lapis_block.png
  default_diamond_block.png
  default_gold_block.png
  default_redstone_block.png
  default_redstone_lamp_off.png
  default_redstone_lamp_on.png

依赖：仅 PIL/numpy。与 build_ore.py / build_anvil.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

_RNG = np.random.RandomState(6200)


def base_canvas(r, g, b):
    """主色实心底（alpha=255：存储块不透明整立方，走整立方面路径）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = r
    canvas[..., 1] = g
    canvas[..., 2] = b
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def draw_block(base_rgb, hi_rgb, lo_rgb, speckle_rgb=None, speckle_hi=None, speckle_n=0):
    """矿物存储块通用绘制：主色底 + 细噪 + 4×4 镶格暗缝 + 左上高光带 + 右下暗边。

    base_rgb  = 主色（材料本色）
    hi_rgb    = 左上高光（受光棱）
    lo_rgb    = 右下暗边 + 镶格缝（背光棱 / 切割缝）
    speckle_* = 可选矿粒点缀（同材料矿石族的斑块语言；红石块用——块面撒亮红矿粒
                读作「红石压缩块」而非纯色板）
    """
    c = base_canvas(*base_rgb)
    # 细噪（拟矿物颗粒感；固定种子 → 确定性）。
    mask = _RNG.random((TS, TS)) < 0.22
    noise = np.array(base_rgb, dtype=np.float64) * 0.86
    c[mask, 0:3] = noise
    # 4×4 镶格暗缝（切割面接缝；格距 4px，缝走 x/y ≡ 0 mod 4 的暗线）。
    for i in range(0, TS, 4):
        for t in range(TS):
            px(c, i, t, lo_rgb)       # 竖缝
            px(c, t, i, lo_rgb)       # 横缝
    # 左上高光带（受光棱亮线；沿镶格顶 / 左边内缩 1px）。
    hi_soft = (np.array(hi_rgb, dtype=np.float64) + np.array(base_rgb)) / 2.0
    for i in range(1, TS, 4):
        for t in range(1, TS):
            px(c, i, t, hi_soft)      # 竖棱内亮线
            px(c, t, i, hi_soft)      # 横棱内亮线
    # 外框高光（左 / 上边）+ 暗边（右 / 下边）—— 读出整块受左上光。
    for t in range(TS):
        px(c, 0, t, hi_rgb)
        px(c, t, 0, hi_rgb)
        px(c, TS - 1, t, lo_rgb)
        px(c, t, TS - 1, lo_rgb)
    # 可选矿粒点缀（固定位置散布；区别镶格缝的「材料斑块」语言）。
    if speckle_rgb is not None:
        centers = [(3, 3), (10, 2), (13, 10), (2, 11), (7, 7), (11, 13)]
        for (cx, cy) in centers[:speckle_n]:
            px(c, cx, cy, speckle_rgb)
            if speckle_hi is not None:
                px(c, cx - 1, cy - 1, speckle_hi)
    return c


def draw_lamp(on):
    """红石灯两态（off=哑壳 / on=亮芯）：机制等价 MC 1.0 redstone lamp off/on 两张贴图。

    off = 暗黄褐壳 + 中央暗红红石芯纹（「熄灭的灯」）；
    on  = 暖黄亮壳 + 中心白热核（「点亮的灯」—— 自发光感由亮色阶梯表达：
          外围暖琥珀 → 中层亮黄 → 中心近白）。六面同图（无朝向语义）。
    """
    if on:
        shell = (214.0, 168.0, 66.0)     # 暖琥珀壳（亮态外围）
        mid = (250.0, 218.0, 108.0)      # 亮黄中层
        core = (255.0, 248.0, 214.0)     # 白热核（近白暖）
        seam = (176.0, 126.0, 40.0)      # 镶格缝（暖深）
    else:
        shell = (96.0, 82.0, 56.0)       # 暗黄褐壳（熄灭态哑色）
        mid = (128.0, 106.0, 68.0)       # 中层暖灰
        core = (158.0, 44.0, 40.0)       # 中央暗红红石芯纹（熄灭的红石粉读感）
        seam = (62.0, 52.0, 36.0)        # 镶格缝（深哑）
    c = base_canvas(*shell)
    # 细噪（拟石质颗粒；固定种子确定性）。
    mask = _RNG.random((TS, TS)) < 0.20
    noise = np.array(shell, dtype=np.float64) * 0.88
    c[mask, 0:3] = noise
    # 4×4 镶格缝（同存储块族语言——灯壳也是「切割拼装」件）。
    for i in range(0, TS, 4):
        for t in range(TS):
            px(c, i, t, seam)
            px(c, t, i, seam)
    # 中央 6×6 芯区：外圈 mid → 中心 core（off 是暗红芯纹 / on 是白热核）。
    cx0, cy0 = 5, 5
    for y in range(cx0, cx0 + 6):
        for x in range(cy0, cy0 + 6):
            edge = x in (cx0, cx0 + 5) or y in (cy0, cy0 + 5)
            px(c, x, y, mid if edge else core)
    # 中心 2×2 强化（on 的白热最亮 / off 的红芯最暗红）。
    px(c, 7, 7, core)
    px(c, 8, 7, core)
    px(c, 7, 8, core)
    px(c, 8, 8, core)
    # 外框：左上微亮 / 右下微暗（整块受左上光，同存储块）。
    hi = tuple(min(255.0, v * 1.18) for v in shell)
    lo = tuple(v * 0.78 for v in shell)
    for t in range(TS):
        px(c, 0, t, hi)
        px(c, t, 0, hi)
        px(c, TS - 1, t, lo)
        px(c, t, TS - 1, lo)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # 煤炭块：近黑煤层压缩块（同煤矿近黑主色 + 高光棱线读出「整块切割煤」）。
    save(draw_block((34.0, 34.0, 38.0), (78.0, 78.0, 84.0), (16.0, 16.0, 18.0)), "default_coal_block")
    # 青金石块：深群青底 + 少量黄铁矿金点（同青金矿石族「深蓝 + 金点」语言）。
    save(draw_block((38.0, 64.0, 168.0), (108.0, 142.0, 232.0), (20.0, 36.0, 104.0),
                    speckle_rgb=(218.0, 178.0, 56.0), speckle_hi=(244.0, 214.0, 96.0), speckle_n=4),
         "default_lapis_block")
    # 钻石块：浅青底 + 青白钻石菱面镶格（同钻矿石族冷白高光语言）。
    save(draw_block((148.0, 204.0, 204.0), (226.0, 248.0, 248.0), (96.0, 148.0, 152.0),
                    speckle_rgb=(226.0, 248.0, 248.0), speckle_hi=(250.0, 254.0, 254.0), speckle_n=5),
         "default_diamond_block")
    # 金块：金黄底 + 近白金高光（同金矿石族「最亮最暖」语言）。
    save(draw_block((240.0, 192.0, 48.0), (254.0, 236.0, 132.0), (176.0, 130.0, 22.0)),
         "default_gold_block")
    # 红石块：鲜红底 + 亮红矿粒点缀（同红石矿石族「鲜红矿粒」语言）。
    save(draw_block((196.0, 38.0, 38.0), (244.0, 118.0, 106.0), (132.0, 20.0, 20.0),
                    speckle_rgb=(248.0, 128.0, 116.0), speckle_hi=(255.0, 188.0, 176.0), speckle_n=6),
         "default_redstone_block")
    # 红石灯两态（off 哑壳 + 暗红芯 / on 亮壳 + 白热核）。
    save(draw_lamp(False), "default_redstone_lamp_off")
    save(draw_lamp(True), "default_redstone_lamp_on")


if __name__ == "__main__":
    main()
