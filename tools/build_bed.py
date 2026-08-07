#!/usr/bin/env python3
"""生成床方块（bed）各色变体贴图 default_bed_<color>.png（16×16 像素，原创程序自绘，§9 override (a)）。

机制等价 MC 1.0 床（bed）—— 简化为单格整立方（spec t387「head+foot 双格，或简化单格 if cleaner」→ 取单格），
名称 / 贴图全原创、不拷贝任何 MC 资产（PLAN §9 区隔）。每个色变体一张 16×16 实心贴图：
彩色被面底 + 顶部枕垫亮带（表枕头 / 被头折叠）+ 被面绗缝针脚暗点 + 边缘暗化。各方块六面铺同图
（mesher 走 culled 立方面路径，同 wool / chest）。色变用纯色（红 / 橙 / 黄 / 绿 / 青 / 蓝 / 品红 / 黑），
非 MC 资产；配色调亮 / 调暗派生枕垫 / 阴影，无随机源 → CI 可复现（与 build_wool.py / build_chest.py 同风格）。

输出（覆盖写入 textures/default_bed_<color>.png，每色一张）。
依赖：仅 PIL。
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 床色变体（plain 纯色，§9 区隔；非 MC 资产）。key = 文件名后缀，value = 被面基色 RGB。
# 覆盖暖 / 冷 / 中性：红 / 橙 / 黄 / 绿 / 青 / 蓝 / 品红 / 黑。
BED_COLORS = [
    ("red",     (160,  45,  45)),
    ("orange",  (200,  95,  30)),
    ("yellow",  (190, 170,  40)),
    ("green",    (60, 130,  50)),
    ("cyan",     (55, 130, 140)),
    ("blue",     (55,  70, 165)),
    ("magenta", (170,  70, 150)),
    ("black",    (38,  38,  44)),
]


def _shade(rgb, k):
    """对基色 rgb 按 k 缩放（k>1 提亮 / k<1 压暗），钳到 0..255。"""
    return tuple(max(0, min(255, int(round(c * k))) ) for c in rgb)


def make_bed(base):
    """基色 base（RGB tuple）→ 16×16 床贴图。确定性（无随机）。"""
    sheet = base                       # 被面主体
    pillow = _shade(base, 1.18)        # 枕垫亮带（被头折叠 / 枕头区）
    stitch = _shade(base, 0.62)        # 绗缝针脚暗点
    edge = _shade(base, 0.50)          # 边缘暗化（床沿 / 阴影）

    img = Image.new("RGBA", (TS, TS), sheet + (255,))
    px = img.load()

    # 1) 枕垫亮带：顶部 rows 1..5、cols 2..13 填枕色（表枕头 / 被头折叠；上下各留 1px 边距 + 左右 2px）。
    for y in range(1, 6):
        for x in range(2, 14):
            px[x, y] = pillow + (255,)
    # 枕垫下沿一道暗缝（被头折痕）。
    for x in range(2, 14):
        px[x, 6] = stitch + (255,)

    # 2) 被面绗缝针脚：被体（rows 7..14）上散布暗点，表被面绗缝起伏（确定性坐标，无随机）。
    stitch_cells = [
        (3, 8), (7, 8), (11, 8),
        (5, 10), (9, 10), (13, 10),
        (3, 12), (7, 12), (11, 12),
        (5, 14), (9, 14), (13, 14),
    ]
    for (x, y) in stitch_cells:
        px[x, y] = stitch + (255,)

    # 3) 边缘暗化：贴图四边 1px 压暗（床沿 / 阴影，强化立方体边界）。
    for i in range(TS):
        px[i, 0] = edge + (255,)          # 顶边
        px[i, TS - 1] = edge + (255,)     # 底边
        px[0, i] = edge + (255,)          # 左边
        px[TS - 1, i] = edge + (255,)     # 右边
    return img


def main():
    for key, base in BED_COLORS:
        img = make_bed(base)
        out = os.path.join(SRC, "default_bed_%s.png" % key)
        img.save(out)
        print("wrote", out, img.size)


if __name__ == "__main__":
    main()
