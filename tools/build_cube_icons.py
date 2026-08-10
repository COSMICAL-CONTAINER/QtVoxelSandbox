#!/usr/bin/env python3
"""把每方块的顶面 + 侧面贴图烘焙成等距（2:1 dimetric）立方体图标，供 hotbar / 背包使用。

为什么需要：
  - 源贴图尺寸不一（16×16 与 48×48 混排），用 Image.Pad 会按原始尺寸渲染 → 槽内图标
    大小不统一、48px 的被裁、16px 的偏小。
  - 单面平面贴图使 cobble/log/planks 肉眼难辨；leaves/sand 也读不出体积感。
  - 改为「顶 + 两侧」等距立方体图标后：每格尺寸完全统一（同一画布）；顶/侧明暗差异
    强化 3D 可读性；grass 顶绿侧褐、log 顶年轮侧树皮 等天然可辨。

实现：2:1 dimetric 投影（顶面菱形、两侧平行四边形）。对每输出像素反求 (u,v) 落在哪一面，
NEAREST 采样源贴图（保像素感），4× 超采样后 LANCZOS 降回目标尺寸 → 立方体轮廓抗锯齿、
贴图保持清晰、无面间接缝。顶 1.0 / 右 0.80 / 左 0.62 三档明暗（光自右上）。

资产管线（PLAN §2-L）：复用 textures/ 下既有面贴图合成，非 MC 资产。本脚可复现同一图标。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")

OUT = 64      # 最终图标边长（px）
SS = 4        # 超采样倍数（轮廓抗锯齿）
W = OUT * SS  # 工作画布边长
FACE_RES = 32  # 每面采样前统一缩到此分辨率（保证 8 方块采样密度一致）

# 每方块 → (顶面贴图, 侧面贴图) 文件名（不含扩展名）。来源 textures/，非 MC 资产。
BLOCKS = [
    ("grass",           "default_grass_top", "default_grass_side"),
    ("dirt",            "default_dirt",      "default_dirt"),
    ("stone",           "default_stone",     "default_stone"),
    ("cobble",          "default_cobble",    "default_cobble"),
    ("log",             "default_tree_top",  "default_tree"),
    ("planks",          "default_wood",      "default_wood"),
    ("leaves",          "default_leaves",    "default_leaves"),
    ("sand",            "default_sand",      "default_sand"),
    ("crafting_table",  "default_crafting_table_top", "default_crafting_table_side"),  # t50
    ("furnace",         "default_furnace_top", "default_furnace_side"),  # t80（图标显顶+侧，不显炉口前面）
    ("coal_ore",        "default_coal_ore", "default_coal_ore"),  # t84 煤矿石（各面同贴图）
    ("iron_ore",        "default_iron_ore", "default_iron_ore"),  # t84 铁矿石（各面同贴图）
    ("diamond_ore",     "default_diamond_ore", "default_diamond_ore"),  # t279 钻矿石（各面同贴图）
    ("copper_ore",      "default_copper_ore", "default_copper_ore"),    # t308 铜矿石（各面同贴图=石头底+橙铜斑+孔雀绿锈）
    ("gold_ore",        "default_gold_ore", "default_gold_ore"),        # t308 金矿石（各面同贴图=石头底+金黄斑簇）
    ("lapis_ore",       "default_lapis_ore", "default_lapis_ore"),      # t471 青金矿石（各面同贴图=石头底+群青深蓝斑簇+黄铁矿金点）
    ("wool",            "default_wool", "default_wool"),  # t300 羊毛方块（各面同贴图=奶白羊毛卷绒纹）
    # t455 16 色 wool 其余 15 色变体立方体图标（各面同贴图=彩色卷绒纹；build_wool.py 程序生成原创像素图）。
    #   15 色顶 + 两侧明暗 → hotbar / 创造调色板肉眼即可辨色（橙 / 品红 / 浅蓝 / 黄 / 柠绿 / 粉 / 灰 / 浅灰 /
    #   青 / 紫 / 蓝 / 棕 / 绿 / 红 / 黑）。white 复用既有 icon_wool.png，故本段不含 white。
    ("wool_orange",     "default_wool_orange",     "default_wool_orange"),
    ("wool_magenta",    "default_wool_magenta",    "default_wool_magenta"),
    ("wool_light_blue", "default_wool_light_blue", "default_wool_light_blue"),
    ("wool_yellow",     "default_wool_yellow",     "default_wool_yellow"),
    ("wool_lime",       "default_wool_lime",       "default_wool_lime"),
    ("wool_pink",       "default_wool_pink",       "default_wool_pink"),
    ("wool_gray",       "default_wool_gray",       "default_wool_gray"),
    ("wool_light_gray", "default_wool_light_gray", "default_wool_light_gray"),
    ("wool_cyan",       "default_wool_cyan",       "default_wool_cyan"),
    ("wool_purple",     "default_wool_purple",     "default_wool_purple"),
    ("wool_blue",       "default_wool_blue",       "default_wool_blue"),
    ("wool_brown",      "default_wool_brown",      "default_wool_brown"),
    ("wool_green",      "default_wool_green",      "default_wool_green"),
    ("wool_red",        "default_wool_red",        "default_wool_red"),
    ("wool_black",      "default_wool_black",      "default_wool_black"),
    # t387 床方块 8 色变体立方体图标（各面同贴图=彩色被面+枕垫亮带+绗缝针脚；build_bed.py 程序生成原创像素图）。
    #   8 色顶 + 两侧明暗 → hotbar / 创造调色板肉眼即可辨色（红 / 橙 / 黄 / 绿 / 青 / 蓝 / 品红 / 黑）。
    ("bed_red",         "default_bed_red",     "default_bed_red"),
    ("bed_orange",      "default_bed_orange",  "default_bed_orange"),
    ("bed_yellow",      "default_bed_yellow",  "default_bed_yellow"),
    ("bed_green",       "default_bed_green",   "default_bed_green"),
    ("bed_cyan",        "default_bed_cyan",    "default_bed_cyan"),
    ("bed_blue",        "default_bed_blue",    "default_bed_blue"),
    ("bed_magenta",     "default_bed_magenta", "default_bed_magenta"),
    ("bed_black",       "default_bed_black",   "default_bed_black"),
    # t455 床方块补齐 8 色新变体立方体图标（white/light_blue/lime/pink/gray/light_gray/purple/brown；
    #   build_bed.py 程序生成原创像素图；与同色羊毛视觉一致）。
    ("bed_white",       "default_bed_white",       "default_bed_white"),
    ("bed_light_blue",  "default_bed_light_blue",  "default_bed_light_blue"),
    ("bed_lime",        "default_bed_lime",        "default_bed_lime"),
    ("bed_pink",        "default_bed_pink",        "default_bed_pink"),
    ("bed_gray",        "default_bed_gray",        "default_bed_gray"),
    ("bed_light_gray",  "default_bed_light_gray",  "default_bed_light_gray"),
    ("bed_purple",      "default_bed_purple",      "default_bed_purple"),
    ("bed_brown",       "default_bed_brown",       "default_bed_brown"),
    ("torch",           "default_torch", "default_torch"),  # t88 火把（透明底+木柄+火焰；走平面 2D 分支非立方体投影）
    ("chest",           "default_chest_top", "default_chest_side"),  # t173 箱子（顶=盖缝+铰链；侧=铁箍带；图标显顶+侧）
    ("farmland",        "default_farmland_dry", "default_dirt"),  # t234 耕地（顶=干态翻耕土；侧=泥土；图标显顶+侧）
    # t394 沙漠群系内容：砂岩（沙下成岩整立方）/ 仙人掌（接触伤害整立方）立方体图标（顶+两侧明暗）。
    ("sandstone",       "default_sandstone_top", "default_sandstone_side"),  # t394 砂岩（顶=压实沙面；侧=层理带）
    ("cactus",          "default_cactus_top",    "default_cactus_side"),     # t394 仙人掌（顶=绿截面环纹；侧=棱脊+刺点）
    # t395 雪原/针叶群系内容：积雪层（地表覆雪）/ 冰（水面冻结）/ 云杉原木（云杉树主干）立方体图标（顶+两侧明暗）。
    ("snow_layer",      "default_snow",          "default_snow"),            # t395 积雪层（各面=冷白冰晶噪点）
    ("ice",             "default_ice",           "default_ice"),             # t395 冰（各面=浅蓝反光裂纹）
    ("pack_ice",        "default_pack_ice",      "default_pack_ice"),        # t468 浮冰（各面=实白细裂纹；Ice 更滑变种）
    ("blue_ice",        "default_blue_ice",      "default_blue_ice"),        # t468 蓝冰（各面=淡蓝纵向纹路；最滑冰种）
    ("spruce_log",      "default_spruce_log_top", "default_spruce_log_side"), # t395 云杉原木（顶=年轮截面；侧=深棕树皮）
    # t466 云杉木板立方体图标（深色木纹；机制等价橡木木板 icon_planks，仅贴图换 spruce_planks）。
    ("spruce_planks",   "default_spruce_planks", "default_spruce_planks"), # t466 云杉木板（各面同贴图=深色木板）
    ("obsidian",        "default_obsidian", "default_obsidian"),  # t472 黑曜石（各面同贴图=深紫黑火山玻璃；流体交互产物）
    ("enchanting_table", "default_enchanting_table_top", "default_enchanting_table_side"),  # t474 附魔台（顶=黑曜石+钻石+立书 / 侧=黑曜石+钻石嵌点+边缘暗化）
    ("bookshelf",       "default_bookshelf", "default_bookshelf"),  # t474 书架（各面同贴图=木板边框+彩色书脊书列）
    ("iron_block",      "default_iron_block", "default_iron_block"),  # t477 铁块（各面同贴图=金属灰底+铆钉网格+高光）
    ("anvil",           "default_anvil_top", "default_anvil_base"),  # t477 铁砧（顶=砧台+砧面+尖角 / 侧=深铁砧身+横向分层）
    ("anvil_chipped",   "default_anvil_damaged_1_top", "default_anvil_base"),  # t477 微损铁砧（顶=砧台+细裂纹 / 侧=深铁砧身）
    ("anvil_damaged",   "default_anvil_damaged_2_top", "default_anvil_base"),  # t477 重损铁砧（顶=砧台+粗裂纹网+缺角 / 侧=深铁砧身）
    # t482/t483 防御造物方块立方体图标（build_pumpkin.py 程序生成原创像素图；顶 + 两侧明暗 → 肉眼可辨）。
    ("pumpkin",         "default_pumpkin_top",  "default_pumpkin_side"),  # t482 南瓜（顶=橙色瓜顶+短茎 / 侧=橙色瓜棱；造物头部方块）
    ("snow",            "default_snow",         "default_snow"),          # t482 雪块（各面=冷白冰晶噪点，同积雪层；雪傀儡身体方块）
    # t485 沙漠神殿结构方块立方体图标（build_tnt.py / build_cut_sandstone.py 程序生成原创像素图；顶 + 两侧明暗 → 肉眼可辨）。
    ("tnt",             "default_tnt",          "default_tnt"),           # t485 TNT（各面同贴图=深红药柱+横向捆带+亮黄标识；沙漠神殿 TNT 陷阱方块）
    ("cut_sandstone",   "default_cut_sandstone", "default_cut_sandstone"), # t485 切制砂岩（各面同贴图=暖沙色+内陷矩形装饰边框；金字塔外框装饰变体）
]

# ---- dimetric 几何（工作画布坐标，y 向下）----
hw = 0.46 * W   # 顶菱形水平半对角线（= 立方体水平半宽）
dv = hw / 2.0   # 顶菱形竖直半对角线（2:1 dimetric）
v = 0.50 * W    # 侧面竖直高度（立方体身高）
cx = W / 2.0
cy = W / 2.0 - v / 2.0  # 使整体竖直居中

# 顶面菱形 4 角 + 底面 3 角（仅画可见三面需要这些）
N = np.array([cx, cy - dv], dtype=np.float64)
E = np.array([cx + hw, cy], dtype=np.float64)
S = np.array([cx, cy + dv], dtype=np.float64)
Wc = np.array([cx - hw, cy], dtype=np.float64)
Ep = np.array([cx + hw, cy + v], dtype=np.float64)
Sp = np.array([cx, cy + dv + v], dtype=np.float64)
Wp = np.array([cx - hw, cy + v], dtype=np.float64)

# 目标画布的 (x,y) 网格（y=row, x=col）
YS, XS = np.mgrid[0:W, 0:W].astype(np.float64)


def load_face(name):
    p = os.path.join(SRC, name + ".png")
    img = Image.open(p).convert("RGBA").resize((FACE_RES, FACE_RES), Image.NEAREST)
    arr = np.asarray(img, dtype=np.float64)  # (FACE_RES, FACE_RES, 4) RGBA
    # 源贴图可能含透明像素（如 leaves 半镂空）→ 立方体会被看穿、读不出体积。
    # 用该面的不透明像素平均色填掉透明像素并强制不透明，保证每方块都是实心立方体图标。
    a = arr[..., 3]
    opaque = a >= 128
    if opaque.all():
        arr[..., 3] = 255.0
        return arr
    if opaque.any():
        fill = arr[opaque][:, 0:3].mean(axis=0)
    else:
        fill = np.array([90.0, 130.0, 50.0])  # 兜底叶绿
    transparent = ~opaque
    arr[transparent, 0:3] = fill
    arr[..., 3] = 255.0
    return arr


def face_uv(o, uax, vax):
    """反求每像素的 (u,v)：前向 P = o + u*uax + v*vax。"""
    det = uax[0] * vax[1] - uax[1] * vax[0]
    dx = XS - o[0]
    dy = YS - o[1]
    u = (dx * vax[1] - dy * vax[0]) / det
    v = (uax[0] * dy - uax[1] * dx) / det
    return u, v


def sample(face, u, v):
    tx = np.clip(np.floor(u * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    ty = np.clip(np.floor(v * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    return face[ty, tx]  # (W, W, 4)


def render(top_name, side_name):
    top = load_face(top_name)
    side = load_face(side_name)
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    # 渲染序：左 → 右 → 顶（顶最后画，顶/侧共边归顶面；左/右共边归右面，无透明缝）
    layers = [
        (0.62, Wc, S - Wc, Wp - Wc, side),  # 左面
        (0.80, E,  S - E,  Ep - E,  side),  # 右面
        (1.00, Wc, N - Wc, S - Wc,  top),  # 顶面
    ]
    for shade, o, uax, vax, face in layers:
        u, v = face_uv(o, uax, vax)
        m = (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)
        col = sample(face, np.clip(u, 0, 1), np.clip(v, 0, 1)).copy()
        col[..., 0:3] *= shade  # 明暗（alpha 不变）
        canvas[m] = col[m]
    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    return img.resize((OUT, OUT), Image.LANCZOS)


def render_flat_2d(name):
    """平面 2D 图标（异形 / 半透方块如火把）：源贴图透明底直接 NEAREST 放大到 OUT×OUT，保留 alpha。

    根因：火把贴图（default_torch.png）是 16×16 透明底只含火把本体（木柄 + 火焰）。若走立方体投影
    (render)，load_face 会用不透明像素平均色填掉透明像素 → 图标糊成实心方块、且投影本身把整块
    16×16 当立方体面渲染 → 黑底方块。火把游戏内本就是异形（torchHost 木柄 + 火焰小立方，非
    1×1×1 立方体；mesher 对 Torch continue），图标应反映其 2D 形态：直接放大、保留 alpha →
    「纯火把无方块底」。
    """
    p = os.path.join(SRC, name + ".png")
    img = Image.open(p).convert("RGBA")
    return img.resize((OUT, OUT), Image.NEAREST)


# t145 不完整方块 flat 2D 区分图标：6 类木制半方块各走自己的剪影（半高 / L 阶 / 柱档 / 高板 / 方格 / 薄条），
# 木板贴图填充剪影、剪影外透明。与立方体图标共用 OUT×OUT 画布 → 槽位尺寸统一（t15）。
#
# 根因：v1 这 6 类共用 icon_planks 立方体图标 + 仅靠中文显示名区分 → 创造调色板 / hotbar 6 格同图、肉眼
# 无法辨图。spec t145 要求 build_cube_icons 程序生成各异 flat 2D 图标。剪影取自各方块世界内异形几何
# （partialblockgeometry.cpp）的正视投影：slab 半高 / stairs L 阶 / fence 立柱+横档 / door 满高窄板 /
# trapdoor 方格 / pressure_plate 薄条。木板材质观感一致（同为木制半方块），仅剪影不同 → 玩家一眼分辨
# 「这是哪类异形」，配合中文显示名（木板台阶 / 木板楼梯 / …）双重区分。
# t163(d) / t169 不完整方块 3D 立体图标：6 类全部走 dimetric 投影（同完整方块 cube icon 路径）按实际形状
#   缩放 —— slab 半高、stairs L 阶（背墙 + 整步）、trapdoor 薄板、pressure_plate 更薄更小、fence 立柱+横档、
#   door 满高薄板。替代 t145 flat 2D 剪影（v1 同木纹难辨）；3D 顶 + 两侧明暗强化「这是哪类异形」+ 保留木板
#   材质观感。t169 把 door/fence 从 flat 2D 升级为 3D（spec「不完整方块 flat→3D」），与 slab/stairs/trapdoor/
#   pressure_plate 同走 render_partial_3d —— 6 类同为 3D 立体图标，hotbar/创造调色板肉眼即可辨图。
# t244 cross 广告牌方块（透明底，2D 平面图标）：草丛 / 小麦作物在世界内是 cross 形广告牌（非整立方），
#   图标应反映其 2D 形态（直接放大、保留 alpha → 「纯草叶 / 麦穗无方块底」，同火把）。源贴图：
#   default_tall_grass（草丛）/ default_wheat_stage_7（小麦作物取成熟阶段作图标 —— 麦穗金黄，肉眼一眼可辨
#   「这是小麦」而非其它绿茎；与 hotbar.cpp iconFileForBlock / BlockRegistry TallGrass/WheatCrop 注释同源）。
FLAT_2D_CROSS = [
    ("tall_grass", "default_tall_grass"),     # t235 草丛 cross（透明底 + 绿草叶；走 render_flat_2d）
    ("wheat_crop", "default_wheat_stage_7"),  # t236 小麦作物 cross（取成熟阶段 7 麦穗金黄作图标）
    ("dead_bush",  "default_dead_bush"),      # t394 枯死的灌木 cross（透明底 + 棕褐干枝；沙漠装饰）
    ("lily_pad",   "default_lily_pad"),       # t396 睡莲（透明底 + 绿色圆叶 + V 形缺口；沼泽水面浮叶）
    ("mushroom",   "default_mushroom"),       # t396 蘑菇（透明底 + 米色菌柄 + 红底白斑菌盖；沼泽草地小蘑菇）
    # t397 多群系装饰植物：花 4 色变体 + 甘蔗（cross 透明底；flat 2D 图标走 render_flat_2d 放大源贴图保留 alpha）。
    ("flower_red",    "default_flower_red"),    # 红花（绿茎 + 红花头）
    ("flower_yellow", "default_flower_yellow"), # 黄花（绿茎 + 黄花头）
    ("flower_blue",   "default_flower_blue"),   # 蓝花（绿茎 + 蓝花头）
    ("flower_white",  "default_flower_white"),  # 白花（绿茎 + 白花头）
    ("sugarcane",     "default_sugarcane"),     # 甘蔗（绿色节段细茎 + 顶部尖叶）
    ("ladder",        "default_ladder"),        # t413 木梯 cross（透明底 + 棕色两纵轨 + 横梯级；竖直爬行梯）
    ("cobweb",        "default_cobweb"),        # t484 蜘蛛网 cross（透明底 + 灰白蛛丝放射网纹；矿井散布）
    ("rail",          "default_rail"),          # t484 铁轨 flat（透明底 + 棕色枕木 + 灰铁双轨；贴地薄板）
]

PARTIALS_3D = [
    ("wood_slab",           "slab"),           # 木板台阶：全 footprint 半高盒（y[0,0.5]）
    ("wood_stairs",         "stairs"),         # 木板楼梯：整步（y[0,0.5] 全 footprint）+ 背墙（y[0.5,1] 背半 footprint）
    ("wood_trapdoor",       "trapdoor"),       # 木活板门：合态薄板（全 footprint，y[0,0.1875]）
    ("wood_pressure_plate", "pressure_plate"), # 木板压力板：贴地更薄更小（边距 1/16，y[0,1/16]）
    ("wood_fence",          "fence"),          # 木栅栏：中心立柱（细方柱全高）+ 上下两条横档（贯穿 x）
    ("wood_door",           "door"),           # 木板门：满格高、3/16 厚的薄板（贴 -Z 面）
]

# t412 圆石变体（cobble variants）：石质半方块图标。同 shape 几何，仅 fill 换 default_cobble（顶 + 侧同圆石）。
#   render_partial_3d(shape, fill_top, fill_side) 透传 fill；与木制半方块同 3D dimetric 流程。
PARTIALS_3D_COBBLE = [
    ("cobble_slab",           "slab"),
    ("cobble_stairs",         "stairs"),
    ("cobble_pressure_plate", "pressure_plate"),
    ("cobble_fence",          "fence"),
]

# t466 云杉木制品链图标（机制等价橡木木制品，仅 fill 换 default_spruce_planks 深色木纹）。
#   slab / fence / door 同 shape 几何（与 PARTIALS_3D 的木制半方块同流程），fill 透传 spruce_planks。
#   云杉木板（SprucePlanks）走 BLOCKS 立方体路径（上已加），本段只列异形半方块。
PARTIALS_3D_SPRUCE = [
    ("spruce_slab",           "slab"),
    ("spruce_fence",          "fence"),
    ("spruce_door",           "door"),
]


def project_pt(x, y, z, cy_local, scale=1.0):
    """dimetric 投影 unit cube [0,1]^3 → 画布坐标。复用 render() 的 hw/dv/v/cx 几何；cy 可调以按形状竖直居中。
    推导（与 render() 的 N/E/S/Wc 同基准）：sx = cx + (x-z)*hw；sy = cy + (1-y)*v + (x+z-1)*dv。
    scale<1 用于「比单格更大的形状」（如门 2 格高）整体缩到画布内 —— hw/dv/v 同比缩小保 dimetric 比例。"""
    sx = cx + (x - z) * hw * scale
    sy = cy_local + (1.0 - y) * v * scale + (x + z - 1.0) * dv * scale
    return np.array([sx, sy], dtype=np.float64)


def _render_face_depth(canvas, depth_buf,
                       o_w, uax_w, vax_w, o_s, uax_s, vax_s,
                       face_img, shade):
    """平行四边形面：3D 由 (o_w, uax_w, vax_w) 定义（用于深度），屏幕由 (o_s, uax_s, vax_s) 定义（用于光栅化）。
    每像素反求 (u,v)∈[0,1] → 采样 face_img × shade，按深度 x+y+z（大者=离 +inf 视点近=胜）做 depth test。
    替代单盒 painter's algorithm，正确处理 stairs 多盒遮挡（背墙 vs 整步共享 y=0.5 边界、屏幕投影重叠）。"""
    u, vv = face_uv(o_s, uax_s, vax_s)
    m = (u >= 0) & (u <= 1) & (vv >= 0) & (vv <= 1)
    u_c = np.clip(u, 0, 1)
    vv_c = np.clip(vv, 0, 1)
    wx = o_w[0] + u_c * uax_w[0] + vv_c * vax_w[0]
    wy = o_w[1] + u_c * uax_w[1] + vv_c * vax_w[1]
    wz = o_w[2] + u_c * uax_w[2] + vv_c * vax_w[2]
    depth = wx + wy + wz
    col = sample(face_img, u_c, vv_c).copy()
    col[..., 0:3] *= shade
    draw = m & (depth > depth_buf)
    canvas[draw] = col[draw]
    depth_buf[draw] = depth[draw]


def _render_box_d(canvas, depth_buf, x0, x1, y0, y1, z0, z1, top_face, side_face, cy_local, scale=1.0):
    """渲染轴对齐盒子的 3 个可见面（顶 y=y1 / 右 x=x1 / 左 z=z1）。各面 origin+uax+vax（3D + 屏幕双套）传深度渲染。
    UV 约定与 render() 同：顶面 u=x,v=z；右面 u=z,v=y；左面 u=x,v=y；texture 在各面满铺（slab 侧面贴图被竖向压缩，
    机制等价 MC slab 侧面纹理被压扁）。shade：顶 1.00 / 右 0.80 / 左 0.62（光自右上，与 cube icon 一致）。
    scale 透传 project_pt（用于门等「比单格大」形状缩到画布内）。"""
    # 顶面 y=y1：origin (x0,y1,z0)，u→+x，v→+z。
    o_s = project_pt(x0, y1, z0, cy_local, scale)
    _render_face_depth(canvas, depth_buf,
                       np.array([x0, y1, z0]), np.array([x1 - x0, 0.0, 0.0]), np.array([0.0, 0.0, z1 - z0]),
                       o_s, project_pt(x1, y1, z0, cy_local, scale) - o_s, project_pt(x0, y1, z1, cy_local, scale) - o_s,
                       top_face, 1.00)
    # 右面 x=x1：origin (x1,y0,z0)，u→+z，v→+y。
    o_s = project_pt(x1, y0, z0, cy_local, scale)
    _render_face_depth(canvas, depth_buf,
                       np.array([x1, y0, z0]), np.array([0.0, 0.0, z1 - z0]), np.array([0.0, y1 - y0, 0.0]),
                       o_s, project_pt(x1, y0, z1, cy_local, scale) - o_s, project_pt(x1, y1, z0, cy_local, scale) - o_s,
                       side_face, 0.80)
    # 左面 z=z1：origin (x0,y0,z1)，u→+x，v→+y。
    o_s = project_pt(x0, y0, z1, cy_local, scale)
    _render_face_depth(canvas, depth_buf,
                       np.array([x0, y0, z1]), np.array([x1 - x0, 0.0, 0.0]), np.array([0.0, y1 - y0, 0.0]),
                       o_s, project_pt(x1, y0, z1, cy_local, scale) - o_s, project_pt(x0, y1, z1, cy_local, scale) - o_s,
                       side_face, 0.62)


def render_partial_3d(shape, fill_top="default_wood", fill_side="default_wood"):
    """异形方块 dimetric 立体图标：木板贴图按实际形状投影 —— slab 半高 / stairs L 阶 / trapdoor 薄板 /
    pressure_plate 更薄更小。1~2 个轴对齐子盒 + depth buffer（x+y+z 大者胜）解多盒遮挡；顶/两侧明暗同
    cube icon。形状竖直居中（cy_local 据 y_mid 推）使小尺寸 icon 不贴画布底。"""
    top = load_face(fill_top)
    side = load_face(fill_side)
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    depth_buf = np.full((W, W), -np.inf)

    # dimetric 投影缩放：默认 1.0（形状 ≤ 1 格高，沿用既有 hw/dv/v 几何）；门 2 格高 → 0.65 缩进画布。
    #   下方各 shape 分支可覆写。scale==1 时 cy_local 走 y_mid 公式（与既有 5 类图标像素一致、零回归）；
    #   scale!=1 时按实际投影 bbox 中心居中（门 x/z 不对称，y_mid 公式会偏中心）。
    scale = 1.0

    if shape == "slab":
        boxes = [(0.0, 1.0, 0.0, 0.5, 0.0, 1.0)]                       # 全 footprint 半高
        y_min, y_max = 0.0, 0.5
    elif shape == "trapdoor":
        boxes = [(0.0, 1.0, 0.0, 0.1875, 0.0, 1.0)]                    # 合态薄板
        y_min, y_max = 0.0, 0.1875
    elif shape == "pressure_plate":
        boxes = [(1.0 / 16.0, 15.0 / 16.0, 0.0, 1.0 / 16.0,
                  1.0 / 16.0, 15.0 / 16.0)]                            # 贴地薄板 + 边距
        y_min, y_max = 0.0, 1.0 / 16.0
    elif shape == "stairs":
        # 整步（全 footprint 半高）+ 背墙（背半 footprint 上半）。渲染序无关（depth buffer 解决遮挡）；
        #   背墙 z[0,0.5] = 背半（z 小 = 背，对应 N 角侧），整步 z[0,1] = 全 footprint。
        boxes = [
            (0.0, 1.0, 0.5, 1.0, 0.0, 0.5),  # 背墙
            (0.0, 1.0, 0.0, 0.5, 0.0, 1.0),  # 整步
        ]
        y_min, y_max = 0.0, 1.0
    elif shape == "fence":
        # 木栅栏（t169 由 flat 2D 升级为 3D，机制对齐 partialblockgeometry.cpp 异形几何）：
        #   中心立柱（4/16 方柱全高，xz 都居中 6..10）+ 两条横档（贯穿 x 0..1、z 同立柱；上位 12..15/16、
        #   下位 5..8/16）。横档略凸出立柱两侧 → 栅栏观感（与 v1 flat 2D 剪影柱档同语义）。depth buffer
        #   解决立柱与横档的相交遮挡（共享 xz 中心区）。
        boxes = [
            (6.0 / 16.0, 10.0 / 16.0, 0.0, 1.0, 6.0 / 16.0, 10.0 / 16.0),  # 立柱
            (0.0, 1.0, 12.0 / 16.0, 15.0 / 16.0, 6.0 / 16.0, 10.0 / 16.0),  # 上横档
            (0.0, 1.0,  5.0 / 16.0,  8.0 / 16.0, 6.0 / 16.0, 10.0 / 16.0),  # 下横档
        ]
        y_min, y_max = 0.0, 1.0
    elif shape == "door":
        # t207 门 UI 图标两格高：门在世界里是两格高方块（WoodDoor 下/上格同 id，
        #   partialblockgeometry.cpp 每格各画满高薄板 → 合起来即 y[0,2] 的 1×2×3/16 薄板）。
        #   图标按真实形状投影 y[0,2]（修正 t169 把门只画成 1 格高 → 与「门是两格高方块」语义不符、
        #   且与立方体图标等高难辨「这是 1 格的门」）。scale=0.7 把 2 格高的屏幕投影缩进画布（2 格高 × 0.7
        #   ≈ 1.4 格立方体的屏幕高度，但因门是窄板宽度仅 ~3/16 格 → 整体呈高窄「门」剪影，~89% 画布高、
        #   ~36% 画布宽，一眼分辨「这是 2 格高的门」）。dimetric 视角下见 +Z 门面（1×2 大面）+ 顶面
        #   （1×3/16 薄边）+ 右面（3/16×2 薄边），与立方体图标（满格厚 1）对比即可分辨。
        boxes = [
            (0.0, 1.0, 0.0, 2.0, 0.0, 3.0 / 16.0),  # 门板（贴 -Z 面，3/16 厚，2 格高）
        ]
        y_min, y_max = 0.0, 2.0
        scale = 0.7
    else:
        img = Image.fromarray(canvas.astype(np.uint8), "RGBA")
        return img.resize((OUT, OUT), Image.LANCZOS)

    # 形状竖直居中：cy_local 使形状的屏幕中心落在画布中心 W/2。
    #   scale==1：沿用 y_mid 公式（与既有 5 类图标像素一致、零回归 —— 公式由 project_pt sy 反解，
    #     对 x/z 对称形状精确、对 fence 等略偏但已固化进既有图标）。
    #   scale!=1（门）：按实际投影 bbox 中心居中 —— 投影所有盒子 8 角（cy_local=0 基线）求 sy 中点，
    #     cy_local = W/2 − sy_mid（门 2 格高 + x/z 不对称，y_mid 公式会偏中心 → 用精确 bbox）。
    if scale == 1.0:
        y_mid = (y_min + y_max) / 2.0
        cy_local = W / 2.0 - (1.0 - y_mid) * v
    else:
        sys_baseline = []
        for (x0, x1, y0, y1, z0, z1) in boxes:
            for px in (x0, x1):
                for py in (y0, y1):
                    for pz in (z0, z1):
                        sys_baseline.append((1.0 - py) * v * scale + (px + pz - 1.0) * dv * scale)
        sy_mid_baseline = (min(sys_baseline) + max(sys_baseline)) / 2.0
        cy_local = W / 2.0 - sy_mid_baseline

    for (x0, x1, y0, y1, z0, z1) in boxes:
        _render_box_d(canvas, depth_buf, x0, x1, y0, y1, z0, z1, top, side, cy_local, scale)

    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    return img.resize((OUT, OUT), Image.LANCZOS)


def _shape_mask(shape):
    """工作画布 W×W（y 向下，归一坐标 [0,1]）内返回该异形的 bool 剪影。

    坐标约定与 partialblockgeometry.cpp 的局部体素坐标一致（y 向上 0..1），但画布 y 向下 →
    画布 y=0.5..1.0 对应世界下半格（脚侧），符合「半砖贴地」的视觉直觉。
    """
    xn = XS / W
    yn = YS / W
    if shape == "slab":
        # 半砖：下半矩形（画布 y 0.5..1.0 全宽）= 世界下半格贴地。
        return (yn >= 0.5) & (yn <= 1.0)
    if shape == "stairs":
        # 楼梯 L 阶：下半整（y 0.5..1.0 全宽）∪ 左上 quarter（y 0..0.5 ∧ x 0..0.5）。
        # 剪影像 MC 楼梯正视：低处满、高处只剩一侧背墙 → 阶梯轮廓。
        lower = (yn >= 0.5) & (yn <= 1.0)
        upper_left = (yn < 0.5) & (xn < 0.5)
        return lower | upper_left
    if shape == "fence":
        # 栅栏：中心立柱（x 0.40..0.60 全高）∪ 两条横档（y 0.20..0.34 / 0.66..0.80 全宽，
        #   横档比立柱略宽 → 柱档凸出两侧的栅栏观感）。
        post = (xn >= 0.40) & (xn <= 0.60)
        bar1 = (yn >= 0.20) & (yn <= 0.34)
        bar2 = (yn >= 0.66) & (yn <= 0.80)
        return post | bar1 | bar2
    if shape == "door":
        # 门：满高窄竖板（x 0.22..0.78，y 0.03..0.98；上下留窄缝显「门框内」）。
        return (xn >= 0.22) & (xn <= 0.78) & (yn >= 0.03) & (yn <= 0.98)
    if shape == "trapdoor":
        # 活版门：满格（2×2 方格纹在 render_partial_2d 内叠加，区分于满格木板立方体图标）。
        return np.ones_like(xn, dtype=bool)
    if shape == "pressure_plate":
        # 压力板：中部细横条（y 0.42..0.58，x 0.08..0.92；最薄的一类，一眼可辨）。
        return (yn >= 0.42) & (yn <= 0.58) & (xn >= 0.08) & (xn <= 0.92)
    return np.zeros_like(xn, dtype=bool)


def render_partial_2d(shape, fill_name="default_wood"):
    """异形方块 flat 2D 图标：木板贴图填充剪影、剪影外透明；剪影边缘暗化描边强化小尺寸可辨性。

    与立方体图标 (render) 共用 W×W 超采样画布 + LANCZOS 降采样 → 剪影轮廓抗锯齿、与立方体图标
    尺寸完全统一。木板贴图按画布坐标线性平铺（整图共享一张木纹 → 6 类材质观感一致）。
    """
    fill = load_face(fill_name)  # FACE_RES×FACE_RES，已强制不透明（leaves 等透明源的统一处理）
    mask = _shape_mask(shape)

    # 木板贴图按画布归一坐标线性采样（u,v in [0,1] → FACE_RES 索引）。
    xn = XS / W
    yn = YS / W
    tx = np.clip(np.floor(xn * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    ty = np.clip(np.floor(yn * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    col = fill[ty, tx].copy()
    canvas[mask] = col[mask]

    # 剪影内 1px（超采样空间）暗化描边：把 mask 零填充一圈后做 4 邻域「全在内」判定，
    # 差集 = 内边缘带（canvas 外视作非 mask → 贴画布边的形状边也被描边）。SS× 下采样后呈细暗轮廓。
    padded = np.zeros((W + 2, W + 2), dtype=bool)
    padded[1:-1, 1:-1] = mask
    nb = padded[:-2, 1:-1] & padded[2:, 1:-1] & padded[1:-1, :-2] & padded[1:-1, 2:]
    edge = mask & ~nb
    canvas[edge, 0:3] *= 0.55

    # 活版门叠加 2×2 方格纹（外框 + 中十字），把「满格」从木板立方体图标里区分出来。
    if shape == "trapdoor":
        border = (xn < 0.06) | (xn > 0.94) | (yn < 0.06) | (yn > 0.94)
        mid = (np.abs(xn - 0.5) < 0.035) | (np.abs(yn - 0.5) < 0.035)
        canvas[border | mid, 0:3] *= 0.55
    # 门加把手（右侧中段小暗点），强化「门」语义。
    if shape == "door":
        knob = (xn > 0.66) & (xn < 0.72) & (yn > 0.48) & (yn < 0.56)
        canvas[knob, 0:3] *= 0.45

    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    return img.resize((OUT, OUT), Image.LANCZOS)


def main():
    for out_name, top_name, side_name in BLOCKS:
        if out_name == "torch":
            # 火把走平面 2D 路径（透明底保留 alpha），非立方体投影（见 render_flat_2d 注释）。
            img = render_flat_2d(top_name)
        else:
            img = render(top_name, side_name)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t145/t163(d)/t169 不完整方块 3D dimetric 立体图标：6 类木制半方块全部走 render_partial_3d
    #   （按实际形状投影，3D 顶+两侧明暗）。t169 把 door/fence 从 flat 2D 升级为 3D —— 6 类同为立体图标。
    for out_name, shape in PARTIALS_3D:
        img = render_partial_3d(shape)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t412 圆石变体 3D dimetric 立体图标：同 shape 几何，fill 换 default_cobble（机制等价木制半方块图标流程）。
    for out_name, shape in PARTIALS_3D_COBBLE:
        img = render_partial_3d(shape, "default_cobble", "default_cobble")
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t466 云杉木制品链 3D dimetric 立体图标：同 shape 几何，fill 换 default_spruce_planks（机制等价木制半方块图标流程）。
    for out_name, shape in PARTIALS_3D_SPRUCE:
        img = render_partial_3d(shape, "default_spruce_planks", "default_spruce_planks")
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t244 cross 广告牌方块 flat 2D 图标：草丛 / 小麦作物（透明底 + 草叶 / 麦穗像素）。
    for out_name, src_name in FLAT_2D_CROSS:
        img = render_flat_2d(src_name)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)


if __name__ == "__main__":
    main()
