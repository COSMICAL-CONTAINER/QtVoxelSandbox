#!/usr/bin/env python3
"""t715 状态效果图标（poison 中毒 / slowness 缓慢 / fire 着火）。

HUD 右上角状态效果栏用（64×64 透明底）。构图 / 配色**意图**参考 docs/Default HD 128x Demo 1.8.2.2/
assets/minecraft/textures/mob_effect/ 下同名图（poison=绿色药水瓶、slowness=深灰蓝重物），但像素全部
为本项目程序生成的**原创**绘制（PLAN §9 红线：不拷贝任何包内 PNG；本脚本只在 16×16 网格上程序化
作画后 4× NEAREST 放大，与 build_cube_icons.py render_flat_2d 同管线风格）。

- poison：圆身药水瓶（软木塞 + 玻璃瓶颈 + 绿色毒液渐变 + 左上高光）。
- slowness：厚重靴子侧影（深灰蓝分三层明暗 + 靴口上沿亮边，读作「沉重 / 迈不开步」）。
- fire：泪滴火苗（外焰红橙→亮橙渐变 + 黄色内核 + 白高光点）。

依赖：仅 PIL。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")

ART = 16   # 原创像素画网格（与项目贴图同分辨率，保像素感）
OUT = 64   # 输出边长（4× NEAREST 放大，与 icon_* 惯例一致）


def new_canvas():
    return [[(0, 0, 0, 0)] * ART for _ in range(ART)]


def put(px, x, y, c):
    """整数格着色（越界忽略）。"""
    if 0 <= x < ART and 0 <= y < ART:
        px[y][x] = c


def finish(px, name):
    img = Image.new("RGBA", (ART, ART))
    for y in range(ART):
        for x in range(ART):
            img.putpixel((x, y), px[y][x])
    img = img.resize((OUT, OUT), Image.NEAREST)
    out = os.path.join(SRC, name)
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def draw_poison():
    """绿色药水瓶：软木塞 + 玻璃瓶颈 + 圆瓶身毒液渐变 + 高光。

    构图意图参考 mob_effect/poison.png（绿色药水），像素原创。
    """
    px = new_canvas()
    # 颜色带
    CORK_D = (110, 79, 46, 255)     # 塞深棕
    CORK_L = (142, 105, 64, 255)    # 塞浅棕（上沿受光）
    NECK = (205, 224, 214, 235)     # 玻璃颈（半透浅色）
    EDGE = (46, 84, 28, 255)        # 瓶身描边深绿
    LIQ = [                          # 毒液自上而下渐亮（y 越大越亮）
        (74, 138, 44, 255),
        (84, 156, 50, 255),
        (96, 176, 58, 255),
        (110, 196, 68, 255),
    ]
    HILITE = (232, 246, 226, 255)   # 左上高光
    # 软木塞（y0..3，x5..10；上两行浅、下两行深）
    for y in range(0, 2):
        for x in range(5, 11):
            put(px, x, y, CORK_L)
    for y in range(2, 4):
        for x in range(5, 11):
            put(px, x, y, CORK_D)
    # 瓶颈（y4..5，x6..9 玻璃）
    for y in range(4, 6):
        for x in range(6, 10):
            put(px, x, y, NECK)
    # 圆瓶身（y6..14）：椭圆 cx=7.5, cy=10, rx=4.6, ry=4.4；描边 1px、内部液面 y8 起装液
    cx, cy, rx, ry = 7.5, 10.0, 4.6, 4.4
    for y in range(6, 15):
        for x in range(2, 14):
            dy = (y + 0.5 - cy) / ry
            dx = (x + 0.5 - cx) / rx
            d = dx * dx + dy * dy
            if d <= 1.0:
                # 液体渐变：按 y 分四档（液面 y8 以上为空瓶玻璃色）
                if y < 8:
                    put(px, x, y, NECK)
                else:
                    liq = LIQ[min(3, (y - 8) // 2)]
                    put(px, x, y, liq)
            elif d <= 1.45:
                put(px, x, y, EDGE)  # 描边环带
    # 液面横线（y8 一行深绿分界）
    for x in range(4, 12):
        if px[8][x][3] > 0:
            put(px, x, 8, (58, 108, 34, 255))
    # 左上高光（瓶身内左上弧 2×3）
    for yy, xx in ((7, 4), (7, 5), (8, 4), (8, 5), (9, 4)):
        put(px, xx, yy, HILITE)
    # 液内深色气泡（原创细节：两粒暗绿圆点）
    put(px, 7, 11, (52, 100, 32, 255))
    put(px, 9, 12, (52, 100, 32, 255))
    finish(px, "icon_effect_poison.png")


def draw_slowness():
    """厚重靴子侧影：深灰蓝三层明暗（上亮沿 / 中灰 / 底最暗）+ 靴跟。

    构图意图参考 mob_effect/slowness.png（深灰蓝重物剪影），像素原创。
    """
    px = new_canvas()
    TOP = (120, 120, 133, 255)   # 靴口上沿亮灰蓝
    MID = (76, 76, 92, 255)      # 靴筒主色
    DARK = (40, 40, 51, 255)     # 靴底 / 阴影最暗
    EDGE = (18, 18, 26, 255)     # 轮廓近黑
    # 靴筒（y2..9，x4..9）
    for y in range(2, 10):
        for x in range(4, 10):
            c = TOP if y == 2 else MID
            put(px, x, y, c)
    # 靴筒左右轮廓
    for y in range(2, 10):
        put(px, 3, y, EDGE)
        put(px, 10, y, EDGE)
    # 靴口上沿轮廓
    for x in range(3, 11):
        put(px, x, 1, EDGE)
    # 脚掌前伸（y10..14，x4..13）
    for y in range(10, 15):
        for x in range(4, 14):
            c = MID if y < 13 else DARK
            put(px, x, y, c)
    # 靴头轮廓（右端）
    for y in range(10, 15):
        put(px, 13, y, EDGE)
    # 靴底两层：y13 中间过渡 DARK 上一档、y14 最暗底缘
    for x in range(4, 14):
        put(px, x, 13, (56, 56, 68, 255))
        put(px, x, 14, EDGE)
    # 鞋跟块（左端 x4..6 在 y11..12 压深，读作后跟）
    for y in range(11, 13):
        for x in range(4, 7):
            put(px, x, y, DARK)
    # 靴筒竖向褶皱两道（压重感）
    for y in range(4, 9):
        put(px, 6, y, (60, 60, 74, 255))
        put(px, 8, y, (60, 60, 74, 255))
    # 上方两道「下压」短线（靴口正上方 x5 / x8 各一竖点，示意重压下沉）
    put(px, 5, 0, MID)
    put(px, 8, 0, MID)
    finish(px, "icon_effect_slowness.png")


def draw_fire():
    """泪滴火苗：外焰红橙→亮橙（底亮顶暗）、黄芯、白高光。

    构图意图参考 mob_effect 火系图标（火苗剪影 + 暖色渐变），像素原创。
    """
    px = new_canvas()
    CX, TOP_Y, BOT_Y = 7.5, 1.0, 14.0
    H = BOT_Y - TOP_Y

    def half_w(t):
        """火苗半宽（0=顶/底尖，中部最宽）：正弦骨架 + 顶部更快收窄（泪滴感）。"""
        import math
        base = math.sin(math.pi * t)
        # 顶部（t<0.35）额外收窄 → 尖顶；底部（t>0.85）略收 → 圆底
        if t < 0.35:
            base *= t / 0.35
        return base * 5.6

    for y in range(ART):
        t = (y - TOP_Y) / H
        if t < 0.0 or t > 1.0:
            continue
        w = half_w(t)
        if w < 0.35:
            # 顶/底尖单像素列（顶亮黄尖 / 底橙尖）
            put(px, int(CX), y, (249, 233, 121, 255) if t < 0.3 else (224, 123, 31, 255))
            continue
        x0, x1 = int(CX - w + 0.5), int(CX + w)
        for x in range(x0, x1 + 1):
            dx = abs(x + 0.5 - CX) / max(w, 0.001)
            # 外焰颜色随高度渐变：底红橙 → 上亮橙；边缘更暗
            if t > 0.72:
                outer = (216, 96, 28, 255)   # 底部红橙
            elif t > 0.4:
                outer = (232, 140, 36, 255)  # 中部橙
            else:
                outer = (244, 178, 56, 255)  # 上部亮橙
            if dx > 0.86:
                outer = (198, 82, 24, 255)   # 边缘暗橙
            c = outer
            # 内核（黄芯）：同形缩放 0.52，中心略上移
            tx = (t - 0.06) / 0.88
            if 0.0 <= tx <= 1.0:
                wi = half_w(tx) * 0.52
                if wi >= 0.35 and abs(x + 0.5 - CX) <= wi:
                    c = (249, 226, 88, 255)   # 黄芯
                    if dx < 0.3:
                        c = (252, 240, 140, 255)  # 芯心更亮
            put(px, x, y, c)
    # 白高光点（内核左上）
    put(px, 6, 7, (255, 252, 224, 255))
    put(px, 6, 8, (255, 252, 224, 255))
    put(px, 5, 7, (255, 252, 224, 255))
    finish(px, "icon_effect_fire.png")


if __name__ == "__main__":
    draw_poison()
    draw_slowness()
    draw_fire()
