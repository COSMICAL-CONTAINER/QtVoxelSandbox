#!/usr/bin/env python3
"""生成 t638 铁轨家族扩展贴图（16×16 像素，原创自绘，§9 override (a)）。

t638 方块细节批：动力铁轨（GoldenRail）/ 探测铁轨（DetectorRail）的贴地薄板贴图 + 探测轨通电
变体 + 红石火把 cross 贴图。与 build_rail.py（普通铁轨）同风格（透明底 + 枕木 + 双轨，alphaCutoff
cutout），机制等价 MC 1.0 powered rail / detector rail / redstone torch；名称 / 贴图纯原创自绘
（§9 区隔，零 MC 资产 / 专名）。

视觉意图：
  - golden_rail（tile 157，断常态）：金轨双线（暖金黄 + 高光）+ 深棕枕木 + 轨中红石连接点（暗红
    像素嵌在两轨间——「断电」暗态）。读作「金色的动力轨」。
  - rail_golden_on（tile 159，通变态）：同布局，红石点改亮红 + 轨亮一档（本工程动力轨恒断电——
    无红石信号，本贴图留图集备用，mesher 不消费）。
  - detector_rail（tile 158，断常态）：灰铁轨 + 石枕（浅灰）+ 轨中暗红探测点。
  - rail_detector_on（tile 160，通电视觉）：同布局，探测点亮红闪红（矿车驶过 state bit4 → mesher
    换本贴图——机制等价 MC 1.0 detector rail 通电换贴图）。
  - redstone_torch（tile 161，cross）：透明底 + 深棕木柄 + 亮红焰头（常亮 on 态装饰光源 光 7）。

t638 ④ 普通铁轨（default_rail.png）质量提升：同脚本顺带重画普通直轨——更粗的双轨（3px 宽：高光 +
主色 + 暗缘）+ 更密的枕木（5 根）+ 轨距对称收窄（x 5..10）→ 读作「真铁轨」而非细线（用户「铁轨
贴图丑」——程序回退图分辨率细节提升；pack 激活时被 rail_normal.png 覆盖不受影响）。

输出（覆盖写入 textures/）：
  default_rail.png              （tile 121，普通直轨 NS——t638 ④ 重画提质）
  default_golden_rail.png       （tile 157，动力轨断常态）
  default_rail_golden_on.png    （tile 159，动力轨通变态——留图集备用）
  default_detector_rail.png     （tile 158，探测轨断常态）
  default_rail_detector_on.png  （tile 160，探测轨通电视觉）
  default_redstone_torch.png    （tile 161，红石火把 cross 常亮态）

依赖：仅 PIL，无外部贴图。与 build_rail.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 配色（普通铁轨提质版）。
WOOD_MID   = (0x6e, 0x4a, 0x28, 255)  # 枕木主色（深棕）
WOOD_DARK  = (0x4a, 0x30, 0x18, 255)  # 枕木暗面
RAIL_LIGHT = (0xd8, 0xd8, 0xd8, 255)  # 铁轨高光（t638 提亮一档：c0→d8，金属反光更醒）
RAIL_MID   = (0x9a, 0x9a, 0x9a, 255)  # 铁轨主色
RAIL_DARK  = (0x5a, 0x5a, 0x5a, 255)  # 铁轨暗缘

# 动力轨配色（金轨 + 红石连接点）。
GOLD_LIGHT = (0xff, 0xe8, 0x60, 255)  # 金轨高光（近白金）
GOLD_MID   = (0xe8, 0xb8, 0x30, 255)  # 金轨主色（暖金黄）
GOLD_DARK  = (0xa8, 0x80, 0x18, 255)  # 金轨暗缘
RS_OFF     = (0x70, 0x18, 0x10, 255)  # 红石连接点断常态（暗红）
RS_ON      = (0xff, 0x40, 0x30, 255)  # 红石点通变态（亮红）

# 探测轨配色（铁轨 + 石枕 + 探测点）。
STONE_TIE  = (0x8a, 0x8a, 0x8e, 255)  # 石枕（浅灰，区别普通木枕）
STONE_DARK = (0x6a, 0x6a, 0x6e, 255)  # 石枕暗面
DT_OFF     = (0x80, 0x20, 0x18, 255)  # 探测点断常态（暗红）
DT_ON      = (0xff, 0x58, 0x38, 255)  # 探测点通电视觉（亮红橙）


def put(px, x, y, color):
    if 0 <= x < TS and 0 <= y < TS:
        px[x, y] = color


def hline(px, x0, x1, y, color):
    for x in range(min(x0, x1), max(x0, x1) + 1):
        put(px, x, y, color)


def vline(px, x, y0, y1, color):
    for y in range(min(y0, y1), max(y0, y1) + 1):
        put(px, x, y, color)


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），消除 cutout 黑边（同 build_rail.py）。"""
    img = img.convert("RGBA")
    W, H = img.size
    px = img.load()
    for _ in range(max(W, H) * 2):
        changed = False
        snap = [px[x, y] for y in range(H) for x in range(W)]
        for y in range(H):
            for x in range(W):
                r0, g0, b0, a0 = snap[y * W + x]
                if a0 != 0 or (r0, g0, b0) != (0, 0, 0):
                    continue
                ar = ag = ab = 0
                n = 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dx == 0 and dy == 0:
                            continue
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < W and 0 <= ny < H:
                            r, g, b, a = snap[ny * W + nx]
                            if a != 0 or (r, g, b) != (0, 0, 0):
                                ar += r; ag += g; ab += b; n += 1
                if n > 0:
                    px[x, y] = (ar // n, ag // n, ab // n, 0)
                    changed = True
        if not changed:
            break
    return img


def draw_ties(px, ys, tie_mid, tie_dark):
    """枕木（横向木/石条）：每根 3px 高（y, y+1 主色、y+2 暗缘），跨 x 1..14。"""
    for ty in ys:
        hline(px, 1, 14, ty, tie_mid)
        hline(px, 1, 14, ty + 1, tie_mid)
        hline(px, 1, 14, ty + 2, tie_dark)


def draw_rail_pair(px, xs, lite, mid, dark):
    """双轨（纵向）：每条轨 3px 宽（x=高光、x+1=主色、x+2=暗缘），贯穿 y 0..15（t638 提质：2px→3px）。"""
    for rx in xs:
        vline(px, rx, 0, 15, lite)
        vline(px, rx + 1, 0, 15, mid)
        vline(px, rx + 2, 0, 15, dark)


def draw_rail_normal():
    """t638 ④ 普通直轨提质版：3px 粗双轨（x=4..6 / x=9..11，轨距对称收窄）+ 5 根密枕木。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    # 5 根枕木（y=1/4/7/10/13 —— 更密：旧 4 根 → 5 根，节距 3px 更像真实轨枕）。
    draw_ties(px, [1, 4, 7, 10, 13], WOOD_MID, WOOD_DARK)
    # 3px 双轨（x=4..6 / x=9..11 —— 对称轨距 x 中心 7.5）。
    draw_rail_pair(px, [4, 9], RAIL_LIGHT, RAIL_MID, RAIL_DARK)
    return img


def draw_golden(on_state):
    """动力轨：金轨双线 + 深棕枕木 + 轨间红石连接点（断暗红 / 通亮红）。直线 NS（EW 由 mesher UV 旋转）。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    draw_ties(px, [1, 4, 7, 10, 13], WOOD_MID, WOOD_DARK)
    draw_rail_pair(px, [4, 9], GOLD_LIGHT if on_state else GOLD_MID,
                   GOLD_MID, GOLD_DARK)
    # 红石连接点：两轨之间（x=7..8）每隔 3px 一对竖向短点（y=2/5/8/11/14 各 1px 高 + 中央纵向连线感——
    #   保持轻量：只在枕木间隙中点放点，读作「轨面嵌着红石」）。
    rs = RS_ON if on_state else RS_OFF
    for y in (2, 5, 8, 11, 14):
        put(px, 7, y, rs)
        put(px, 8, y, rs)
    return img


def draw_detector(on_state):
    """探测轨：铁轨 + 石枕（浅灰，区别木枕）+ 轨间探测点（断暗红 / 通亮红橙）。直线 NS。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    draw_ties(px, [1, 4, 7, 10, 13], STONE_TIE, STONE_DARK)
    draw_rail_pair(px, [4, 9], RAIL_LIGHT, RAIL_MID, RAIL_DARK)
    # 探测点：两轨间（x=7..8）——通态更醒（2px 宽 × 每 2px 一点成列；断态轻量（只在枕木上沿））。
    dt = DT_ON if on_state else DT_OFF
    if on_state:
        for y in (1, 2, 5, 6, 9, 10, 13, 14):
            put(px, 7, y, dt)
            put(px, 8, y, dt)
    else:
        for y in (2, 5, 8, 11, 14):
            put(px, 7, y, dt)
            put(px, 8, y, dt)
    return img


def draw_redstone_torch():
    """红石火把（tile 161，cross 贴图）：透明底 + 深棕木柄（中央竖条 x 7..8，y 6..15）+ 亮红焰头
    （柄顶方块 x 6..9 × y 2..6——外暗红 / 内亮红 / 心白热）。机制等价 MC 1.0 红石火把常亮 on 态。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    handle = (0x5e, 0x42, 0x24, 255)      # 深棕木柄（同 torchHandle #6b4f24 近似族）
    head_out = (0xd8, 0x30, 0x20, 255)    # 焰头外层（暗红）
    head_mid = (0xff, 0x50, 0x38, 255)    # 焰头中层（亮红）
    head_core = (0xff, 0xd8, 0xc8, 255)   # 焰心（白热）
    # 木柄（y 6..15，2px 宽居中）。
    for y in range(6, TS):
        put(px, 7, y, handle)
        put(px, 8, y, handle)
    # 焰头（y 2..6 方块，外中内三层）。
    for y in range(2, 7):
        for x in range(6, 10):
            put(px, x, y, head_out)
    for y in range(3, 6):
        for x in range(7, 9):
            put(px, x, y, head_mid)
    put(px, 7, 4, head_core)
    put(px, 8, 4, head_core)
    return img


def save(img, name):
    img = bleed_alpha(img)
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_rail_normal(), "default_rail")            # t638 ④ 普通直轨提质（tile 121）
    save(draw_golden(False), "default_golden_rail")     # 动力轨断常（tile 157）
    save(draw_golden(True), "default_rail_golden_on")   # 动力轨通变（tile 159；留图集备用）
    save(draw_detector(False), "default_detector_rail") # 探测轨断常（tile 158）
    save(draw_detector(True), "default_rail_detector_on")  # 探测轨通电视觉（tile 160）
    save(draw_redstone_torch(), "default_redstone_torch")  # 红石火把 cross（tile 161；常亮光 7）


if __name__ == "__main__":
    main()
