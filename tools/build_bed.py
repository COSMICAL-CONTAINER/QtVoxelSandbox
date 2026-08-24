#!/usr/bin/env python3
"""生成床方块（bed）各色变体贴图 default_bed_<color>.png（16×16 像素，原创程序自绘，§9 override (a)）。

机制等价 MC 1.0 床（bed）—— head+foot 双格横置（t428），名称 / 贴图全原创、不拷贝任何 MC 资产（PLAN §9 区隔）。
每个色变体一张 16×16 实心贴图：彩色被面底 + 顶部「折边亮带」（表被头折叠起来的那一段，比被面略亮的同色）+
被面横向绗缝条纹（绗缝暗线 + 针脚暗点）+ 边缘暗化。色变用纯色（红 / 橙 / 黄 / 绿 / 青 / 蓝 / 品红 / 黑
+ t455 追加 white/light_blue/lime/pink/gray/light_gray/purple/brown），非 MC 资产；配色调亮 / 调暗派生
折边 / 绗缝 / 阴影，无随机源 → CI 可复现（与 build_wool.py / build_chest.py 同风格）。

t496 贴图重设计（配合 partialblockgeometry.cpp ShapeBed 3D 模型）：
  旧版（t387）贴图顶部画了一整块「枕垫亮带」（rows 1..5），那是给**单格整立方**渲染（六面铺同图，顶面显
  枕垫区）用的。t457 改成 3D 模型后，床垫盒六面都铺这张图 → 顶部枕垫区出现在床垫顶面（与真正的枕头盒
  重复）+ 床垫侧面被压扁拉伸（枕垫区 5 行贴在 1/16 厚的床垫侧面 → 视觉糊成一片）。t496 重设计贴图为
  「纯绗缝被面」（无枕垫区）—— 任何面铺上去都是干净的横向绗缝条纹 + 顶部一道折边亮带（表被头折叠），
  床垫顶 / 侧面都不会再被拉伸糊化。真正的枕头改用独立白色枕头盒（PartialBlockGeometry 用 wool tile 38）
  + 床头板（planks），不再靠贴图表达枕头。

  绗缝布局：贴图整体是「被面」—— 顶部 rows 1..3 一道折边亮带（被头折叠处，略亮同色），其余被面散布
  横向绗缝暗线 + 针脚暗点（表被子绗缝起伏，确定性坐标，无随机）。四边 1px 暗化（床沿 / 阴影）。各面铺
  同图，被面观感一致。

输出（覆盖写入 textures/default_bed_<color>.png，每色一张）。
依赖：仅 PIL。
"""
import os
from PIL import Image, ImageDraw

# t821 床色板与羊毛色板**同源对齐**（用户「床上羊毛色与实际色羊毛不一致」）：旧版 t387 首 8 色
#   （red/orange/yellow/green/cyan/magenta/black）是手挑近似值，与 build_wool.py WOOL_COLORS 逐通道有差
#   （PIL 实测：red +10/+5/+5、orange −22/−25/0、yellow −20/−10/0、green −10/0/−5、cyan −10/−5/−5、
#   magenta −15/−5/−15、black +8/+8/+6；blue 及 t455 追加 8 色 + white 本就同值）→ 同名色床/羊毛并排
#   色感漂移。修：直接 **import build_wool.WOOL_COLORS**（同目录单一权威，改羊毛色板床自动跟随，零第二
#   份色板可漂移）；white 沿用羊毛白 (240,240,238)（= default_wool.png 主体奶白，build_wool.WOOL）。
#   条目顺序仅决定生成循环次序（key=文件名后缀，无索引耦合），保持 t387 前 8 + t455 后 8 的既有排列。
from build_wool import WOOL_COLORS as _WOOL_COLORS

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

BED_COLORS = [
    ("red",         dict(_WOOL_COLORS)["red"]),
    ("orange",      dict(_WOOL_COLORS)["orange"]),
    ("yellow",      dict(_WOOL_COLORS)["yellow"]),
    ("green",       dict(_WOOL_COLORS)["green"]),
    ("cyan",        dict(_WOOL_COLORS)["cyan"]),
    ("blue",        dict(_WOOL_COLORS)["blue"]),
    ("magenta",     dict(_WOOL_COLORS)["magenta"]),
    ("black",       dict(_WOOL_COLORS)["black"]),
    # t455 新增 8 色 + white（与 build_wool.py 同色板，羊毛↔床同色一致）：
    ("white",       (240, 240, 238)),
    ("light_blue",  dict(_WOOL_COLORS)["light_blue"]),
    ("lime",        dict(_WOOL_COLORS)["lime"]),
    ("pink",        dict(_WOOL_COLORS)["pink"]),
    ("gray",        dict(_WOOL_COLORS)["gray"]),
    ("light_gray",  dict(_WOOL_COLORS)["light_gray"]),
    ("purple",      dict(_WOOL_COLORS)["purple"]),
    ("brown",       dict(_WOOL_COLORS)["brown"]),
]


def _shade(rgb, k):
    """对基色 rgb 按 k 缩放（k>1 提亮 / k<1 压暗），钳到 0..255。"""
    return tuple(max(0, min(255, int(round(c * k))) ) for c in rgb)


def make_bed(base):
    """基色 base（RGB tuple）→ 16×16 床贴图（纯绗缝被面 + 顶部折边亮带）。确定性（无随机）。

    t496：重设计为「纯绗缝被面」—— 任何面铺同图都是干净的横向绗缝纹 + 折边亮带，无枕垫区
    （枕头改由独立白色枕头盒表达，床头板改由 planks 盒表达，见 partialblockgeometry.cpp ShapeBed）。
    """
    sheet = base                       # 被面主体
    fold = _shade(base, 1.20)          # 折边亮带（被头折叠处，略亮同色）
    stitch = _shade(base, 0.60)        # 绗缝暗线 / 针脚暗点
    seam = _shade(base, 0.78)          # 折边下沿一道接缝（被头折痕）
    edge = _shade(base, 0.50)          # 边缘暗化（床沿 / 阴影）

    img = Image.new("RGBA", (TS, TS), sheet + (255,))
    px = img.load()

    # 1) 顶部折边亮带：rows 1..3、cols 1..14 填折边色（表被头折叠起来的那一段，略亮 → 立体层次）。
    #    顶部折边对应 3D 模型床垫的「被面朝上一端」—— 各面铺同图时，被面 + 顶部一道折边层次清晰。
    for y in range(1, 4):
        for x in range(1, 15):
            px[x, y] = fold + (255,)
    # 折边下沿一道接缝（被头折痕，表被子折过来的缝合线）。
    for x in range(1, 15):
        px[x, 4] = seam + (255,)

    # 2) 被面横向绗缝条纹：被体（rows 5..14）上每隔 2 行画一条横向绗缝暗线 + 针脚暗点，
    #    表被子绗缝起伏的缝合线（确定性坐标，无随机）。横向条纹让被面有「被子」纹理而非纯色块。
    for y in (6, 9, 12):  # 横向绗缝缝合线
        for x in range(1, 15):
            px[x, y] = stitch + (255,)
    # 针脚暗点（绗缝交叉处的针脚，强化绗缝纹理）。
    stitch_cells = [
        (3, 5), (7, 5), (11, 5),
        (3, 8), (7, 8), (11, 8),
        (3, 11), (7, 11), (11, 11),
        (3, 14), (7, 14), (11, 14),
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
