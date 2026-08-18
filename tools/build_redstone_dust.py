#!/usr/bin/env python3
"""生成 t656 红石粉导线贴图（16×16 像素，原创自绘，§9 override (a)）。

红石粉导线（RedstoneDust）的贴地薄层贴图：线向（line，瓦片沿 Z 轴延伸——X 向由 mesher UV 旋转复用，
一张两用同铁轨模式）+ 孤立点（dot，无连接时画）两形态 × 断常暗红 / 通电亮红两态 = 四瓦片
（tile 166..169）。机制等价 MC 1.0 redstone dust（线 / 点两形态 + 15 级亮度渐变——v1 简化为两态，
16 级亮度留后续任务）；名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）。

t657 追加：redstone_torch_off（tile 170，红石火把熄灭态 cross —— 深棕柄 + 暗红熄焰头，附着块被供电
反相熄灭的 NOT 门视觉；on 态 tile 161 由 build_rail_family.py 生成）。

视觉意图：
  - dust_line_off（tile 166）：暗红粉线（中央 2px 宽纵线 + 端头渐细收口）——「断电的暗红粉末线」。
  - dust_dot_off  （tile 167）：孤立暗红粉点（中央 4×4 方点）——「无连接的粉堆」。
  - dust_line_on  （tile 168）：亮红粉线 + 白热高光粒——「通电发亮的红线」。
  - dust_dot_on   （tile 169）：亮红粉点 + 白热心——「通电发亮的粉堆」。
  - redstone_torch_off（tile 170）：暗红熄焰火把（深棕柄 + 外暗红 / 内深红焰头，无白热心）。

t692 电力级亮度渐变追加（机制等价 MC 1.0 redstone dust 15 级亮度沿导线衰减的视觉）：在断（off）与
满亮（on）之间补两档中间亮度瓦片 ——
  - dust_line_lvl1（tile 171）：弱电粉线（电力 1-5 档：中暗红 + 稀疏微亮粒）。
  - dust_line_lvl2（tile 172）：半亮粉线（电力 6-10 档：中亮红 + 少量白热粒）。
  - dust_dot_lvl1  （tile 173）：弱电粉点。
  - dust_dot_lvl2  （tile 174）：半亮粉点。
mesher 据 state 低 4 位电力级选档：0 → off（166/167）、1-5 → lvl1、6-10 → lvl2、11-15 → on（168/169）
（4 视觉档 × 衰减 15 格 → 沿线亮度逐格变暗的「电流衰减」读感）。

输出（覆盖写入 textures/）：
  default_dust_line_off.png   （tile 166）
  default_dust_dot_off.png    （tile 167）
  default_dust_line_on.png    （tile 168）
  default_dust_dot_on.png     （tile 169）
  default_redstone_torch_off.png（tile 170；t657 红石火把熄灭态）
  default_dust_line_lvl1.png  （tile 171；t692 弱电线向）
  default_dust_line_lvl2.png  （tile 172；t692 半亮线向）
  default_dust_dot_lvl1.png   （tile 173；t692 弱电点）
  default_dust_dot_lvl2.png   （tile 174；t692 半亮点）

依赖：仅 PIL，无外部贴图。与 build_rail_family.py 同风格（透明底 + alphaCutoff cutout + bleed_alpha
消黑边；程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 断常态配色（暗红粉末 —— 未通电的红石粉）。
DUST_OFF_MID  = (0x9a, 0x1a, 0x12, 255)  # 断电粉主色（暗红）
DUST_OFF_DARK = (0x6a, 0x10, 0x0a, 255)  # 断电粉暗缘（深暗红）

# t692 中间亮度档配色（电力 1-5 弱电 / 6-10 半亮）。
DUST_L1_MID   = (0xc4, 0x24, 0x16, 255)  # 弱电主色（中暗红——亮于 off 主色、暗于 on 主色）
DUST_L1_DARK  = (0x88, 0x14, 0x0c, 255)  # 弱电暗缘
DUST_L1_CORE  = (0xe8, 0x7c, 0x60, 255)  # 弱电微亮粒（暖橙红——无白热、仅微温感）
DUST_L2_MID   = (0xe8, 0x2c, 0x1c, 255)  # 半亮主色（中亮红）
DUST_L2_DARK  = (0xa4, 0x18, 0x10, 255)  # 半亮暗缘
DUST_L2_CORE  = (0xff, 0xa0, 0x88, 255)  # 半亮暖心粒（粉白——弱于 on 的近白白热）

# 通常态配色（亮红粉末 —— 通电发光的红石粉）。
DUST_ON_MID   = (0xff, 0x38, 0x22, 255)  # 通电粉主色（亮红）
DUST_ON_DARK  = (0xc8, 0x20, 0x14, 255)  # 通电粉暗缘（中红）
DUST_ON_CORE  = (0xff, 0xd0, 0xb8, 255)  # 通电白热粒（近白粉红）


def put(px, x, y, color):
    if 0 <= x < TS and 0 <= y < TS:
        px[x, y] = color


# 亮度档配色表（t692 4 视觉档：0=断电 / 1=弱电 1-5 / 2=半亮 6-10 / 3=满亮 11-15）。
#   每档 = (主色, 暗缘, 心粒)；心粒 None = 无心粒（断电）。
LEVELS = [
    (DUST_OFF_MID, DUST_OFF_DARK, None),           # 0 断电
    (DUST_L1_MID,  DUST_L1_DARK,  DUST_L1_CORE),   # 1 弱电
    (DUST_L2_MID,  DUST_L2_DARK,  DUST_L2_CORE),   # 2 半亮
    (DUST_ON_MID,  DUST_ON_DARK,  DUST_ON_CORE),   # 3 满亮
]


def _level(level):
    """亮度档 → (mid, dark, core, core_ys)：core_ys = 心粒出现行（档越高越密；断电恒空）。"""
    mid, dark, core = LEVELS[level]
    if level == 0:
        return mid, dark, core, ()
    if level == 1:   # 弱电：稀疏微亮粒（每 6px 一对）
        return mid, dark, core, (3, 10)
    if level == 2:   # 半亮：少量暖心粒（每 5px）
        return mid, dark, core, (2, 7, 12)
    return mid, dark, core, (2, 6, 10, 13)  # 满亮：白热粒最密（同旧 on 版）


def draw_line(level):
    """线向瓦片：中央 2px 宽纵线（沿 Z 轴贯穿 y 0..15）+ 端头渐细收口 +（档>0）心粒。

    一张两用（同铁轨 121）：X 向连线由 mesher UV 旋转 90° 复用本瓦片（partialblockgeometry
    RedstoneDust case 的角点绕序旋转，同 Rail EW 直轨模式）。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    mid, dark, core, core_ys = _level(level)
    # 主线：x 7..8（2px 宽居中）贯穿 y 1..14（端头各留 1px 给渐细收口）。
    for y in range(1, TS - 1):
        put(px, 7, y, mid)
        put(px, 8, y, mid)
    # 暗缘：主线两侧零星暗色粒（x 6 / x 9 每隔 3px 一粒——撒粉感，非实心边）。
    for y in range(2, TS - 2, 3):
        put(px, 6, y, dark)
        put(px, 9, y, dark)
    # 端头渐细收口：y 0 / y 15 各 1px 单列（粉线端头略窄的「粉末收口」）。
    put(px, 7, 0, dark)
    put(px, 7, TS - 1, dark)
    if core is not None:
        # 心粒（电流在线上流动的「发亮」读感；档越高粒越密越白）。
        for y in core_ys:
            put(px, 7, y, core)
            put(px, 8, y, core)
    return img


def draw_dot(level):
    """孤立点瓦片：中央 4×4 方点（无连接的粉堆）+（档≥2）心粒。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    mid, dark, core, _ = _level(level)
    # 方点：x 6..9 × y 6..9（4×4 居中）。
    for y in range(6, 10):
        for x in range(6, 10):
            put(px, x, y, mid)
    # 四角暗缘（粉堆边缘略暗的「堆料」感）。
    put(px, 6, 6, dark)
    put(px, 9, 6, dark)
    put(px, 6, 9, dark)
    put(px, 9, 9, dark)
    if core is not None and level >= 2:
        # 半亮档：中心单点暖粒；满亮档：中央 2×2 白热（粉堆通电发亮）。
        if level == 2:
            put(px, 7, 7, core)
            put(px, 8, 8, core)
        else:
            for y in range(7, 9):
                for x in range(7, 9):
                    put(px, x, y, core)
    return img


def draw_torch_off():
    """t657 红石火把熄灭态 cross（tile 170）：深棕柄 + 暗红熄焰头（无白热心 —— 区别 on 态 161 的
    外暗红 / 内亮红 / 心白热三层焰）。附着块被供电 → 反相熄灭（NOT 门）的视觉承载。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()
    handle = (0x5e, 0x42, 0x24, 255)      # 深棕木柄（同 build_rail_family.draw_redstone_torch 的 handle）
    head_out = (0x70, 0x14, 0x0c, 255)    # 熄焰外层（深暗红）
    head_mid = (0x98, 0x20, 0x14, 255)    # 熄焰中层（暗红——低于 on 态 head_out 的暗度）
    # 木柄（y 6..15，2px 宽居中，与 on 态同布局）。
    for y in range(6, TS):
        put(px, 7, y, handle)
        put(px, 8, y, handle)
    # 熄焰头（y 2..6 方块，仅两层暗红，无白热心）。
    for y in range(2, 7):
        for x in range(6, 10):
            put(px, x, y, head_out)
    for y in range(3, 6):
        for x in range(7, 9):
            put(px, x, y, head_mid)
    return img


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


def save(img, name):
    img = bleed_alpha(img)
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_line(0), "default_dust_line_off")   # 断电线向（tile 166）
    save(draw_dot(0),  "default_dust_dot_off")    # 断电点   （tile 167）
    save(draw_line(3), "default_dust_line_on")    # 通电线向（tile 168）
    save(draw_dot(3),  "default_dust_dot_on")     # 通电点   （tile 169）
    save(draw_torch_off(), "default_redstone_torch_off")  # t657 红石火把熄灭态（tile 170）
    # t692 电力级亮度渐变两中间档（mesher 据电力级选：0→off / 1-5→lvl1 / 6-10→lvl2 / 11-15→on）。
    save(draw_line(1), "default_dust_line_lvl1")  # 弱电线向（tile 171）
    save(draw_line(2), "default_dust_line_lvl2")  # 半亮线向（tile 172）
    save(draw_dot(1),  "default_dust_dot_lvl1")   # 弱电点   （tile 173）
    save(draw_dot(2),  "default_dust_dot_lvl2")   # 半亮点   （tile 174）


if __name__ == "__main__":
    main()
