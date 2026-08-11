#!/usr/bin/env python3
"""生成熔炉（Furnace）方块的顶面 / 侧面 / 前面贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 熔炉（8 圆石围圈合成），但贴图为本项目程序生成的原创像素图，**不**拷贝任何
MC 资产。基底取本工程既有圆石（default_cobble.png）平均色，保证与「石族」配色一致、视觉上
读得出「石制熔炉」；三面用不同的暗色装饰区分朝向：
  - 顶面：中央暗色方孔（熔炉顶口）+ 浅色边框；
  - 侧面：横向砖纹分隔线（无开口，纯侧壁）；
  - 前面（朝 -Z）：下部圆拱形炉口（深色拱洞 + 浅色拱框），是熔炉最具辨识度的一面。
  - 前面点燃态（furnace_front_on）：拱洞内填亮黄橙火焰（机制等价 MC 1.0 熔炉燃烧时正面发光）；
    燃烧时（有燃料 + 有输入）mesher 据熔炉 state 的点燃 bit 选本贴图，否则用 furnace_front（灭）。

输出（覆盖写入 textures/）：
  default_furnace_top.png        —— 顶面：圆石底 + 中央方孔 + 边框
  default_furnace_side.png       —— 侧面：圆石底 + 横向砖纹
  default_furnace_front.png      —— 前面（灭）：圆石底 + 下部圆拱炉口（暗洞）
  default_furnace_front_on.png   —— 前面（点燃）：圆石底 + 下部圆拱炉口 + 亮黄橙火焰（t494）

可复现：同输入（无随机源）→ 同输出（确定性，便于 CI 校验）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def cobble_base():
    """读既有圆石贴图，取不透明像素平均色作熔炉底色（与石族配色一致；熔炉由圆石合成）。"""
    p = os.path.join(SRC, "default_cobble.png")
    img = Image.open(p).convert("RGBA").resize((TS, TS), Image.NEAREST)
    arr = np.asarray(img, dtype=np.float64)
    opaque = arr[..., 3] >= 128
    base = arr[opaque][:, 0:3].mean(axis=0) if opaque.any() else np.array([128.0, 128.0, 128.0])
    return base  # (3,) RGB


def new_canvas(base):
    """以 base 色 + 固定微噪点纹理铺底（16×16 不透明，确定性无随机源）。"""
    c = np.zeros((TS, TS, 4), dtype=np.float64)
    c[..., 0:3] = base
    c[..., 3] = 255.0
    speckle = base * 0.90
    lite = np.clip(base * 1.08, 0, 255)
    for y in range(TS):
        for x in range(TS):
            if (x + y) % 3 == 0:
                c[y, x, 0:3] = speckle
            elif (x * 2 + y) % 5 == 0:
                c[y, x, 0:3] = lite
    return c


def draw_top(base):
    """顶面 = 圆石底 + 中央 6×6 暗色方孔（熔炉顶口）+ 浅色边框。"""
    c = new_canvas(base)
    rim = np.clip(base * 1.15, 0, 255)   # 顶口浅色边框
    hole = base * 0.30                    # 顶口暗洞
    x0, x1 = 5, 10                        # 方孔 x 范围 [5,10]
    y0, y1 = 5, 10                        # 方孔 y 范围 [5,10]
    for x in range(x0 - 1, x1 + 2):
        c[y0 - 1, x, 0:3] = rim
        c[y1 + 1, x, 0:3] = rim
    for y in range(y0 - 1, y1 + 2):
        c[y, x0 - 1, 0:3] = rim
        c[y, x1 + 1, 0:3] = rim
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            c[y, x, 0:3] = hole
    return c


def draw_side(base):
    """侧面 = 圆石底 + 2 条横向砖纹分隔线（无开口，纯侧壁；与前面区分）。"""
    c = new_canvas(base)
    line = base * 0.62   # 砖纹暗线
    for y in (5, 10):
        c[y, :, 0:3] = line
    return c


# 拱洞形状（draw_front 用）：返回某 y 行的拱洞 x 范围 (x_lo, x_hi)（含端点）；
# 顶部窄、向下逐层加宽成圆拱，y>=8 满宽。行 y 不在拱洞内 → None。
#   y=6: 顶弧（仅 x=7,8）
#   y=7: (6,9)
#   y=8..11: (5,10) 满宽
def arch_xs(y):
    if y == 6:
        return (7, 8)
    if y == 7:
        return (6, 9)
    if 8 <= y <= 11:
        return (5, 10)
    return None


def draw_front(base):
    """前面（朝 -Z）= 圆石底 + 下部圆拱形炉口（深色拱洞 + 浅色拱框）。
    拱洞由 arch_xs() 定义；拱框为拱洞外缘 1 圈浅色（不覆盖洞内像素）。
    """
    c = new_canvas(base)
    frame = np.clip(base * 1.18, 0, 255)  # 拱框浅色
    mouth = base * 0.22                    # 炉口暗洞（接近黑）

    def in_arch(x, y):
        xs = arch_xs(y)
        return xs is not None and xs[0] <= x <= xs[1]

    # 先填拱洞暗色
    for y in range(6, 12):
        xs = arch_xs(y)
        if xs is None:
            continue
        for x in range(xs[0], xs[1] + 1):
            c[y, x, 0:3] = mouth
    # 拱框：遍历拱洞外接区域 (x 4..11, y 5..12)，非拱洞内的像素描浅色边框。
    for y in range(5, 13):
        for x in range(4, 12):
            if in_arch(x, y):
                continue
            # 仅当该格「紧邻」拱洞（上下左右有一格在拱洞内）才描框，避免把整个外接矩形填实。
            nb = (in_arch(x - 1, y) or in_arch(x + 1, y) or
                  in_arch(x, y - 1) or in_arch(x, y + 1))
            if nb:
                c[y, x, 0:3] = frame
    return c


def draw_front_on(base):
    """前面点燃态（furnace_front_on，t494）= 同 draw_front 的圆石底 + 拱框，但拱洞内填亮黄橙火焰
    （机制等价 MC 1.0 熔炉燃烧时正面炉口发光）。火焰由「暖色填拱洞 + 几粒亮白热点 + 顶部偏橙底偏红」
    组成（无随机源 → 确定性）。拱框同灭态（视觉一致，仅洞内从暗变亮火）。
    """
    c = new_canvas(base)
    frame = np.clip(base * 1.18, 0, 255)  # 拱框浅色（同灭态）
    # 火焰色阶：底（拱洞底，y=11）深红橙 → 中橙 → 上（拱顶，y=6..7）亮黄白（火焰下红上黄）。
    fire_low = np.array([214.0, 78.0, 28.0])    # 深红橙（火焰底部）
    fire_mid = np.array([248.0, 158.0, 40.0])   # 中橙（火焰中部）
    fire_hi = np.array([252.0, 224.0, 110.0])   # 亮黄（火焰上部 / 高温区）
    fire_peek = np.array([255.0, 250.0, 210.0])  # 白炽热点（最亮）

    def in_arch(x, y):
        xs = arch_xs(y)
        return xs is not None and xs[0] <= x <= xs[1]

    # 填拱洞火焰：y 越小（拱顶）越亮黄、y 越大（拱底）越深红橙。
    for y in range(6, 12):
        xs = arch_xs(y)
        if xs is None:
            continue
        for x in range(xs[0], xs[1] + 1):
            if y >= 10:
                c[y, x, 0:3] = fire_low    # 拱底两层深红橙
            elif y >= 8:
                c[y, x, 0:3] = fire_mid    # 拱中部中橙
            else:
                c[y, x, 0:3] = fire_hi     # 拱顶亮黄
    # 几粒白炽热点散布在火焰中上部（高温反光，确定性固定位置）。
    c[7, 7, 0:3] = fire_peek
    c[8, 8, 0:3] = fire_peek
    c[6, 8, 0:3] = fire_peek
    c[9, 6, 0:3] = fire_peek
    # 拱框（同灭态）：非拱洞内、紧邻拱洞的像素描浅色边框。
    for y in range(5, 13):
        for x in range(4, 12):
            if in_arch(x, y):
                continue
            nb = (in_arch(x - 1, y) or in_arch(x + 1, y) or
                  in_arch(x, y - 1) or in_arch(x, y + 1))
            if nb:
                c[y, x, 0:3] = frame
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    base = cobble_base()
    save(draw_top(base), "default_furnace_top")
    save(draw_side(base), "default_furnace_side")
    save(draw_front(base), "default_furnace_front")
    save(draw_front_on(base), "default_furnace_front_on")  # t494 点燃态炉口（亮黄橙火焰）


if __name__ == "__main__":
    main()
