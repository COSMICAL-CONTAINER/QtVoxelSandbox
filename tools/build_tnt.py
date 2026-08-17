#!/usr/bin/env python3
"""生成 TNT 方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t485 沙漠神殿 TNT 陷阱方块（机制等价 MC 1.0 TNT——可引爆的爆炸物方块）。名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）：红色药柱捆绑外观 + 横向捆带 + 顶部引线，读作「炸药包」。

视觉意图：读作「捆扎的红药柱炸药」——
  - 主体：深红底（火药+染料混合的标志色），细密噪点表药柱粗糙质感。
  - 横向捆带：3 条深棕带（表捆绑药柱的绳/带），强化「成捆炸药」读感。
  - 顶部引线：顶面中央一个亮黄/橙小圆点（引燃点），暗示「点燃即爆」。
  - 侧面贴一张字面「TNT」风格标识（用原创几何色块，非商标字样）。

输出（覆盖写入 textures/）：
  default_tnt.png         （tile 122，TNT 侧/前面：捆带 + 中央标识）
  default_tnt_top.png     （tile 164，TNT 顶面：药柱截面 + 中央引线接口俯视）
  default_tnt_bottom.png  （tile 165，TNT 底面：药柱底板 + 暗捆带，无引线/标识）

t646 per-face（机制等价 MC 1.0 TNT 三面贴图 side/top/bottom）：此前四槽全 122（顶底
也是侧图）；现顶面独立（fuse 圆点俯视）、底面独立（纯药柱底无标记），侧/前面共用侧图。

依赖：仅 PIL/numpy，无外部贴图。与 build_sandstone.py / build_ore.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(485)


def tnt_base():
    """深红药柱底（alpha=255：TNT 不透明整立方，与砂岩/石头同走整立方面路径）。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 176.0  # R
    canvas[..., 1] = 48.0   # G
    canvas[..., 2] = 40.0   # B（深红，火药+染料混合的标志色）
    canvas[..., 3] = 255.0
    return canvas


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def speckle(canvas, rng, density=0.30):
    """撒细密噪点（暗/亮交替），表药柱粗糙质感。确定性（传入 rng）。"""
    dark = np.array([150.0, 38.0, 32.0])
    lite = np.array([200.0, 64.0, 52.0])
    mask = rng.random((TS, TS)) < density
    canvas[mask, 0:3] = dark
    mask2 = rng.random((TS, TS)) < density * 0.6
    canvas[mask2, 0:3] = lite


def draw_side():
    """侧面：深红药柱底 + 3 条横向深棕捆带 + 中央原创几何标识。"""
    c = tnt_base()
    speckle(c, _RNG, density=0.28)
    band = np.array([96.0, 60.0, 32.0])    # 深棕捆带
    band_hi = np.array([128.0, 82.0, 44.0])  # 捆带高光上沿
    # 3 条横向捆带（位置 3 / 8 / 12），表捆绑药柱的带子。
    for y in [3, 8, 12]:
        rect(c, 0, y, TS - 1, y, band)
        rect(c, 0, y - 1, TS - 1, y - 1, band_hi)
    # 中央原创几何标识（3×3 色块组合，非商标字样）：亮黄圆点 + 暗框，读作「危险品标志」。
    mark_bg = np.array([232.0, 196.0, 56.0])   # 亮黄标识底
    mark_fg = np.array([40.0, 28.0, 20.0])     # 暗框/暗心
    # 标识位于捆带之间的中段（rows 5..6 区域的中央 cols 6..9）。
    rect(c, 6, 5, 9, 6, mark_bg)
    px(c, 7, 5, mark_fg)
    px(c, 8, 6, mark_fg)
    # 边缘暗化（表药柱边缘磨损）。
    edge = np.array([120.0, 30.0, 26.0])
    rect(c, 0, 0, TS - 1, 0, edge)
    rect(c, 0, TS - 1, TS - 1, TS - 1, edge)
    rect(c, 0, 0, 0, TS - 1, edge)
    rect(c, TS - 1, 0, TS - 1, TS - 1, edge)
    return c


def draw_top():
    """顶面（t646 tile 164）：深红药柱截面 + 中央亮黄引燃点（引线接口俯视）+ 四角暗化。

    读作「成捆药柱的俯视截面」——侧面捆带的俯视延续为外圈边框暗带；中央亮黄/白小圆
    （fuse dot）表引线接口（顶视看引线插入点）。与侧面标识的亮黄同色语言。
    """
    c = tnt_base()
    speckle(c, _RNG, density=0.25)
    # 外圈边框暗带（侧面边缘暗化的俯视延续 + 捆带勒出的外沿）。
    rim = np.array([140.0, 36.0, 30.0])
    rect(c, 0, 0, TS - 1, 0, rim)
    rect(c, 0, TS - 1, TS - 1, TS - 1, rim)
    rect(c, 0, 0, 0, TS - 1, rim)
    rect(c, TS - 1, 0, TS - 1, TS - 1, rim)
    # 捆带俯视压痕：3 条捆带在顶面的勒痕（与侧面 y=3/8/12 对应 → 顶面 x=3/8/12 竖暗线，
    #   读作「带子绕过顶面」；深棕同侧图捆带色但更暗（顶视受光弱））。
    band_top = np.array([80.0, 50.0, 28.0])
    for x in [3, 8, 12]:
        rect(c, x, 1, x, TS - 2, band_top)
    # 中央引线接口（fuse dot）：亮黄圆点 + 白热高光心 + 暗色勒圈 —— 顶视看引线插口。
    fuse_ring = np.array([96.0, 60.0, 32.0])   # 深棕勒圈（捆带同语言）
    fuse = np.array([232.0, 196.0, 56.0])      # 亮黄引线帽（同侧图标识底色）
    fuse_hot = np.array([252.0, 240.0, 170.0]) # 白热高光心
    rect(c, 6, 6, 9, 9, fuse_ring)             # 外勒圈 4×4
    rect(c, 7, 7, 8, 8, fuse)                  # 亮黄 2×2
    px(c, 7, 7, fuse_hot)                      # 左上高光像素
    # 四角暗化（表药柱捆扎角落磨损）。
    corner = np.array([140.0, 36.0, 30.0])
    rect(c, 1, 1, 2, 2, corner)
    rect(c, TS - 3, 1, TS - 2, 2, corner)
    rect(c, 1, TS - 3, 2, TS - 2, corner)
    rect(c, TS - 3, TS - 3, TS - 2, TS - 2, corner)
    return c


def draw_bottom():
    """底面（t646 tile 165）：深红药柱底板 + 暗捆带延续，无引线/无标识。

    读作「炸药包的底板」——捆带绕过底面成横向暗带（与侧面 y=3/8/12 同位）；底面贴地
    无标记（引线/标识都在顶面与侧面，机制等价 MC TNT 底面纯药柱底）。
    """
    c = tnt_base()
    speckle(c, _RNG, density=0.30)
    # 3 条横向捆带（与侧面同位 y=3/8/12；底面受光最弱 → 无高光上沿，仅暗带）。
    band = np.array([88.0, 54.0, 30.0])  # 深棕（比侧图更暗一格，底面不受光）
    for y in [3, 8, 12]:
        rect(c, 0, y, TS - 1, y, band)
    # 整体边缘暗化（贴地磨损；比侧图暗一档）。
    edge = np.array([112.0, 28.0, 24.0])
    rect(c, 0, 0, TS - 1, 0, edge)
    rect(c, 0, TS - 1, TS - 1, TS - 1, edge)
    rect(c, 0, 0, 0, TS - 1, edge)
    rect(c, TS - 1, 0, TS - 1, TS - 1, edge)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    # t646 per-face：侧/前面 = 侧图（捆带 + 中央标识）；顶 = 引线接口俯视；底 = 纯药柱底。
    #   **先画 side**：default_tnt.png（tile 122）与 t485 版本字节一致（RNG 调用序不变，
    #   side 先消耗固定次数的随机流，top/bottom 在其后取流 → 已有瓦片不漂移）。
    save(draw_side(), "default_tnt")
    save(draw_top(), "default_tnt_top")
    save(draw_bottom(), "default_tnt_bottom")


if __name__ == "__main__":
    main()
