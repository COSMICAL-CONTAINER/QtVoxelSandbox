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

t644 `--from-pack`「放置 3D 贴图 → 背包 item 图标」转换工具：
  用户反馈「PNG 放下来 3D 是对的，但背包 item 图标跟放下来的方块不一样」——此前多轮图标用
  程序贴图 / pack front 平面图混搭，而世界内放置态已切 pack 贴图 → 背包与放置观感漂移。
  修：从 pack（gitignored 只读参考，docs/Default HD 128x Demo 1.8.2.2）**读取**方块面贴图，
  按方块**放置形状**（满立方 / 矮盒 / 正面为主 / 门 / 薄板 / 贴地薄片）渲染同款 dimetric 图标，
  **写** icon_*.png 进 textures/（工具产物进 git 合法 —— 与本脚既有程序生成图标同一管线）。
  法务红线：pack PNG 本体只读不拷贝（§9），仅派生图标 PNG 落盘。
"""
import os
import sys
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
    # misc 二轮 基岩立方体图标（各面=default_bedrock；深灰粗糙岩石纹理，同 stone 风格但更暗更斑驳）。
    #   机制等价 MC 创造可取 bedrock 物品（生存不可破；创造可放置/取用），故创造调色板需一图标。
    ("bedrock",         "default_bedrock",   "default_bedrock"),
    ("cobble",          "default_cobble",    "default_cobble"),
    ("log",             "default_tree_top",  "default_tree"),
    ("planks",          "default_wood",      "default_wood"),
    ("leaves",          "default_leaves",    "default_leaves"),
    ("sand",            "default_sand",      "default_sand"),
    # t492 工作台 / 熔炉 / 发射器曾移到 BLOCKS_FRONT（正面为主投影）→ t676 再移 FROM_PACK cube_front
    #   （满立方 dimetric 顶 + 右侧 + 前面三面独立贴图 —— 用户点名「front 方案太扁平」升全立体）。
    ("coal_ore",        "default_coal_ore", "default_coal_ore"),  # t84 煤矿石（各面同贴图）
    ("iron_ore",        "default_iron_ore", "default_iron_ore"),  # t84 铁矿石（各面同贴图）
    ("diamond_ore",     "default_diamond_ore", "default_diamond_ore"),  # t279 钻矿石（各面同贴图）
    ("copper_ore",      "default_copper_ore", "default_copper_ore"),    # t308 铜矿石（各面同贴图=石头底+橙铜斑+孔雀绿锈）
    ("gold_ore",        "default_gold_ore", "default_gold_ore"),        # t308 金矿石（各面同贴图=石头底+金黄斑簇）
    ("lapis_ore",       "default_lapis_ore", "default_lapis_ore"),      # t471 青金矿石（各面同贴图=石头底+群青深蓝斑簇+黄铁矿金点）
    ("redstone_ore",    "default_redstone_ore", "default_redstone_ore"),  # t569 红石矿石（各面同贴图=石头底+鲜红菱斑矿粒；走过/挖掘点亮微弱红光）
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
    # t395 雪原/针叶群系内容：冰（水面冻结）/ 云杉原木（云杉树主干）立方体图标（顶+两侧明暗）。
    #   注：积雪层（snow_layer）不在 BLOCKS —— t525 改薄板图标（1/8 厚度，区别于雪块满格立方），走
    #   render_partial_3d 的 snow_layer shape（见 PARTIALS_3D_SNOW / main），iconSourceForBlock 据
    #   isPartialBlock(SnowLayer)=true 路由到本薄板图标。
    ("ice",             "default_ice",           "default_ice"),             # t395 冰（各面=浅蓝反光裂纹）
    ("pack_ice",        "default_pack_ice",      "default_pack_ice"),        # t468/t495 浮冰（各面=淡蓝白压实冰+细裂纹+反光高光；非白羊毛）
    ("blue_ice",        "default_blue_ice",      "default_blue_ice"),        # t468 蓝冰（各面=淡蓝纵向纹路；最滑冰种）
    ("spruce_log",      "default_spruce_log_top", "default_spruce_log_side"), # t395 云杉原木（顶=年轮截面；侧=深棕树皮）
    # t714 云杉树叶立方体图标：源 default_spruce_leaves（程序深蓝绿针叶 + 透明孔；load_face 填孔保证实心立方）。
    #   机制等价 oak leaves icon_leaves 流程（顶 + 两侧明暗），色调深蓝绿 → 与橡树叶图标肉眼可辨。
    ("spruce_leaves",   "default_spruce_leaves", "default_spruce_leaves"), # t714 云杉树叶（深蓝绿针叶）
    # t466 云杉木板立方体图标（深色木纹；机制等价橡木木板 icon_planks，仅贴图换 spruce_planks）。
    ("spruce_planks",   "default_spruce_planks", "default_spruce_planks"), # t466 云杉木板（各面同贴图=深色木板）
    ("obsidian",        "default_obsidian", "default_obsidian"),  # t472 黑曜石（各面同贴图=深紫黑火山玻璃；流体交互产物）
    # t620 附魔台移出 BLOCKS（满立方投影）→ 下方 one-off 段 render_partial_3d("table")（0.75 矮盒，机制等价
    #   世界内 PartialBlockGeometry [0,0.75] 盒 + e260b2d BlockDef 12/16 高）。满立方图标与世界内矮盒观感
    #   不符（背包里显整块、世界里是矮台）→ review L20 重生成。
    ("bookshelf",       "default_wood", "default_bookshelf"),  # t474 书架（**t620 per-face 改**：顶·底=planks(8) 木板（世界内 BlockDef 顶/底 tile 8）/ 侧=bookshelf(111) 木板边框+书脊书列；图标顶面随之换 default_wood，同 chest/farmland 混面模式；review L20 重生成）
    ("iron_block",      "default_iron_block", "default_iron_block"),  # t477 铁块（各面同贴图=金属灰底+铆钉网格+高光）
    # t620 矿物存储块立方体图标（各面同贴图=对应材质压缩块；机制等价铁块 icon_iron_block 流程）。
    ("coal_block",      "default_coal_block", "default_coal_block"),  # t620 煤炭块（近黑煤层压缩块+高光棱线）
    ("lapis_block",     "default_lapis_block", "default_lapis_block"),  # t620 青金石块（深群青底+黄铁矿金点）
    ("diamond_block",   "default_diamond_block", "default_diamond_block"),  # t620 钻石块（浅青底+青白菱面镶格）
    ("gold_block",      "default_gold_block", "default_gold_block"),  # t620 金块（金黄底+近白金高光）
    ("redstone_block",  "default_redstone_block", "default_redstone_block"),  # t620 红石块（鲜红底+亮红矿粒镶面）
    ("redstone_lamp",   "default_redstone_lamp_off", "default_redstone_lamp_off"),  # t620 红石灯（off 态哑壳+红石芯作图标；on 态是点亮视觉非图标）
    ("anvil",           "default_anvil_top", "default_anvil_base"),  # t477 铁砧（顶=砧台+砧面+尖角 / 侧=深铁砧身+横向分层）
    ("anvil_chipped",   "default_anvil_damaged_1_top", "default_anvil_base"),  # t477 微损铁砧（顶=砧台+细裂纹 / 侧=深铁砧身）
    ("anvil_damaged",   "default_anvil_damaged_2_top", "default_anvil_base"),  # t477 重损铁砧（顶=砧台+粗裂纹网+缺角 / 侧=深铁砧身）
    # t482/t483 防御造物方块立方体图标（build_pumpkin.py 程序生成原创像素图；顶 + 两侧明暗 → 肉眼可辨）。
    #   t675 南瓜移出 BLOCKS（程序贴图 顶+两侧）→ 下方 FROM_PACK 段 cube_front 方案（pack 拼方块：顶 + 刻脸 +
    #   瓜棱，与放置态 per-face 同源）。
    # ("pumpkin",      "default_pumpkin_top",  "default_pumpkin_side"),  # t675 移至 FROM_PACK cube_front（--from-pack 重生成）
    ("snow",            "default_snow",         "default_snow"),          # t482 雪块（各面=冷白冰晶噪点，同积雪层；雪傀儡身体方块）
    # t634 末地传送门（endframe 化 t620）立方体图标（顶=末影祭坛框面+中央暗绿凹槽 / 侧=灰白细孔框身；要塞传送门房祭坛）。
    #   入创造调色板（t634）—— 图标显顶+两侧明暗（同铁矿块流程）。
    ("end_portal",      "default_endframe_top", "default_endframe_side"), # t634 末地传送门（顶=祭坛框面 / 侧=框身）
    # t485 沙漠神殿结构方块立方体图标（build_tnt.py / build_cut_sandstone.py 程序生成原创像素图；顶 + 两侧明暗 → 肉眼可辨）。
    ("tnt",             "default_tnt",          "default_tnt"),           # t485 TNT（各面同贴图=深红药柱+横向捆带+亮黄标识；沙漠神殿 TNT 陷阱方块）
    ("cut_sandstone",   "default_cut_sandstone", "default_cut_sandstone"), # t485 切制砂岩（各面同贴图=暖沙色+内陷矩形装饰边框；金字塔外框装饰变体）
    # t486 丛林神殿结构方块立方体图标（build_mossy_cobble.py / build_dispenser.py 程序生成原创像素图；顶 + 两侧明暗 → 肉眼可辨）。
    ("mossy_cobble",    "default_mossy_cobble",  "default_mossy_cobble"),  # t486 苔石（各面同贴图=圆石灰底+暗绿苔藓斑簇；丛林神殿主体）
    # t676：工作台 / 熔炉 / 发射器 / 投掷器 / TNT 图标走 FROM_PACK cube_front（--from-pack 重生成；本表不再管）。
    # t487 要塞结构方块立方体图标（build_stone_brick.py 程序生成原创像素图；顶 + 两侧明暗 → 肉眼可辨）。
    ("stone_brick",     "default_stone_brick", "default_stone_brick"),  # t487 石砖（各面同贴图=石质灰底+砖块缝纹网格；要塞墙体主体）
    # t760 刷怪笼立方体图标（build_spawner.py 程序生成原创像素图；t760 起贴图为 cutout 栅格——孔透明，
    #   load_face 会把 alpha<128 孔用不透明像素均值色（铁灰）填掉 → 图标仍实心立方，读作铁笼）。
    #   机制等价 MC 1.0 创造背包可取刷怪笼 + 中键复制出带图标的 item（此前 iconFileForBlock 无 case → 空串 → 透明图标）。
    ("spawner",         "default_spawner", "default_spawner"),  # t760 刷怪笼（各面同贴图=铁灰栅栏笼格；地牢/要塞结构方块）
    # t600 石砖台阶/楼梯不再走 BLOCKS（满立方体投影）：三个图标渲染成同一张整砖立方 → 背包里与石砖满格无法区分
    #   （用户「三个都是石砖满一格子的样子」）。改走下方 PARTIALS_3D_STONE_BRICK（slab/stairs 形状投影，砖纹 fill）。
]

# t600 石砖半方块 3D dimetric 立体图标：slab 半高盒 / stairs L 阶（背墙 + 整步），fill 换 default_stone_brick
#   （机制等价圆石变体 PARTIALS_3D_COBBLE 流程）。替代 t487 把台阶/楼梯误放 BLOCKS（满立方投影 → 三图标同图）。
PARTIALS_3D_STONE_BRICK = [
    ("stone_brick_slab",   "slab"),   # t487/t600 石砖台阶：全 footprint 半高盒（砖纹）
    ("stone_brick_stairs", "stairs"), # t487/t600 石砖楼梯：整步 + 背墙 L 阶（砖纹）
]

# t492 「正面有辨识特征」的方块（正面贴图, 顶面贴图, 侧面贴图）—— 走 render_front（正面为主的 dimetric 投影）。
#   t676 工作台 / 熔炉 / 发射器 / 投掷器**全部移出**（用户点名「front 方案太扁平」→ 升 cube per-face 满立方
#   dimetric：顶 + 右侧 + 前面三面独立贴图，见 FROM_PACK cube_front 段）。本表现空 —— 保留表结构 /
#   render_front（t644 render_pack_front 同族）供后续「正面为主」需求复用。
BLOCKS_FRONT = []

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


# t492 「正面为主」dimetric 投影几何（工作画布坐标，y 向下）。与 render() 的顶菱形投影正交：render() 视角
#   俯视立方体（顶菱形 + 左右两侧可见、正面 +Z 背向），适合「各面同贴图 / 顶面是辨识特征」的方块；
#   render_front() 视角近似正视立方体略偏右上（正面 +Z 成主面 + 顶细带 +Y + 右细带 +X），适合「正面有辨识特征」
#   的方块（熔炉炉口 / 工作台网格 / 发射器排出口），让辨识特征贴在正面不被遮挡。
#   深度剪切：世界 -Z（向远）映射为屏幕 (ddx, +ddy 向上)，背面在屏幕右上方 → 右细带（+X 面）与顶细带（+Y 面）
#   暴露在正面右 / 上 → 形成「正面是主面 + 右上 L 形深度带」的 3D 立方体观感（仍是 dimetric 家族，保体积感）。
F_DDX = 0.14 * W   # 深度水平剪切（背面相对正面向右偏）
F_DDY = -0.14 * W  # 深度竖直剪切（背面相对正面向上偏；y 向下故负值=上）
# 正面四角占画布 [f_lo, f_hi]²；留 0.10 边距 + 0.14 深度带 → 整体立方体居中（背面右偏上偏后仍在画布内）。
F_LO = 0.10 * W
F_HI = 0.72 * W


def render_front(front_name, top_name, side_name):
    """正面为主的 dimetric 立方体图标：正面（+Z，主面，贴 front 贴图）+ 顶细带（+Y，贴 top）+ 右细带（+X，贴 side）。

    根因（t492）：render()（顶菱形 + 两侧）的视角下，方块正面（+Z）背向观察者、立体投影遮挡 → 「正面有辨识
    特征」的方块（熔炉炉口 / 工作台网格 / 发射器排出口）的辨识面在图标里完全不可见 → 用户只看到顶 + 无特征
    侧面，读作「普通石块 / 像木板」，且因无正面纵深提示 → 显得「2D / 平」。改正面为主投影后，辨识特征贴在
    主面正面一眼可辨，顶 / 右深度细带保 3D 立体感（与 render() 立方体图标同为 dimetric 家族，槽位观感不割裂）。

    明暗：右 0.70 / 顶 0.85 / 正面 1.00（正面成主面 → 最亮；顶 / 右深度带渐暗保体积感）。与 render()（顶 1.0 /
    右 0.80 / 左 0.62）同为「最亮主面 + 渐暗深度面」的 dimetric 明暗家族，但主面从「顶」改为「正面」以适配正面为主
    投影；保整体调性一致（槽位里与其它立方体图标同属立体明暗家族，观感不割裂）。渲染序：右 → 顶 → 正面（正面最后画、
    最前，盖住顶 / 右与正面共边，无透明缝；同 render()「顶最后画」的共边归属逻辑）。
    """
    front = load_face(front_name)
    top = load_face(top_name)
    side = load_face(side_name)
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    # 正面四角（屏幕坐标，y 向下）：FBL 底左 / FBR 底右 / FTR 顶右 / FTL 顶左。
    FBL = np.array([F_LO, F_HI])
    FBR = np.array([F_HI, F_HI])
    FTR = np.array([F_HI, F_LO])
    FTL = np.array([F_LO, F_LO])
    depth = np.array([F_DDX, F_DDY])
    # 背面四角 = 正面 + 深度剪切。
    BBR = FBR + depth
    BTR = FTR + depth
    BTL = FTL + depth

    def paint(o, uax, vax, face, shade):
        u, v = face_uv(o, uax, vax)
        m = (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)
        col = sample(face, np.clip(u, 0, 1), np.clip(v, 0, 1)).copy()
        col[..., 0:3] *= shade
        canvas[m] = col[m]

    # 右面（+X）：屏幕四角 FBR→BBR→BTR→FTR。o=FBR，u 沿深度（FBR→BBR，front→back），v 沿垂直（FBR→FTR，底→顶）。
    paint(FBR, BBR - FBR, FTR - FBR, side, 0.70)
    # 顶面（+Y）：屏幕四角 FTL→FTR→BTR→BTL。o=FTL，u 沿水平（FTL→FTR，左→右），v 沿深度（FTL→BTL，front→back）。
    paint(FTL, FTR - FTL, BTL - FTL, top, 0.85)
    # 正面（+Z，主面）：屏幕四角 FBL→FBR→FTR→FTL。o=FBL，u 沿水平（FBL→FBR），v 沿垂直（FBL→FTL）。
    paint(FBL, FBR - FBL, FTL - FBL, front, 1.00)
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
    ("brown_mushroom", "default_brown_mushroom"),  # t507 白蘑菇 / 棕蘑菇（透明底 + 米色菌柄 + 棕色菌盖浅黄褐斑；沼泽草地小蘑菇；蘑菇汤原料）
    # t397 多群系装饰植物：花 4 色变体 + 甘蔗（cross 透明底；flat 2D 图标走 render_flat_2d 放大源贴图保留 alpha）。
    ("flower_red",    "default_flower_red"),    # 红花（绿茎 + 红花头）
    ("flower_yellow", "default_flower_yellow"), # 黄花（绿茎 + 黄花头）
    ("flower_blue",   "default_flower_blue"),   # 蓝花（绿茎 + 蓝花头）
    ("flower_white",  "default_flower_white"),  # 白花（绿茎 + 白花头）
    ("sugarcane",     "default_sugarcane"),     # 甘蔗（绿色节段细茎 + 顶部尖叶）
    ("ladder",        "default_ladder"),        # t413/t519 木梯（透明底 + 棕色两纵轨 + 4 道横梯级；竖直爬行梯；t519 满格贴墙贴图）
    ("cobweb",        "default_cobweb"),        # t484 蜘蛛网 cross（透明底 + 灰白蛛丝放射网纹；矿井散布）
    ("rail",          "default_rail"),          # t484 铁轨 flat（透明底 + 棕色枕木 + 灰铁双轨；贴地薄板；t638 ④ 提质版）
    # t638 铁轨家族扩展 + 红石火把 flat 2D 图标（透明底保留 alpha；程序生成原创像素图）。
    ("golden_rail",    "default_golden_rail"),     # 动力铁轨（金轨双线 + 红石连接点；矿车加速）
    ("detector_rail",  "default_detector_rail"),   # 探测铁轨（铁轨 + 石枕 + 探测点；矿车驶过通电视觉）
    ("redstone_torch", "default_redstone_torch"),  # 红石火把（深棕柄 + 亮红焰头；常亮装饰光源 光 7）
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

# t490 手动 TNT 点火机关图标；t662 几何重做（用户「跟压力板一模一样，不行」）→ 换 button / lever 形状投影：
#   - 按钮（wood_button / stone_button）：凸钮单盒（6×2×6 居中，机制等价 MC button）+ 各自底座贴图 fill。
#   - 杠杆（lever）：圆石底座盒（fill_top=default_cobble，机制等价 MC lever cobble base）+ 斜插摆棍两段
#     （fill_side=default_wood 木板棍，机制等价 MC lever stick）—— render_partial_3d lever shape 内
#     盒 0 用 top / 盒 1.. 用 side 贴图。3D dimetric 立体图标使 hotbar / 创造调色板肉眼可辨「按钮 / 拉杆」。
PARTIALS_3D_IGNITER = [
    ("lever",        "lever"),        # 杠杆：底座 cobble + 摆棍 planks（shape 内双 fill 分工）
    ("wood_button",  "button"),       # 木按钮：凸钮单盒（default_wood_button fill，下方 fill 表覆盖）
    ("stone_button", "button"),       # 石按钮：凸钮单盒（default_stone_button fill）
]
# t662 机关 fill 映射（shape → (fill_top, fill_side)）：lever 底座 cobble / 棍 planks；按钮各自底座贴图。
MECH_FILL = {
    "lever":       ("default_cobble", "default_wood"),
    "wood_button": ("default_wood_button", "default_wood_button"),
    "stone_button": ("default_stone_button", "default_stone_button"),
}

# t627 压力板家族扩展图标（stone / iron / gold pressure plate）：pressure_plate shape（同木/圆石压力板流程），
#   fill = 各自独立瓦片（build_pressure_plates.py：石灰/金属铆钉/亮金板面）。材质色一眼可辨「这是哪种板」。
PARTIALS_3D_PLATE_FAMILY = [
    ("stone_pressure_plate", "default_stone_pressure_plate", "default_stone_pressure_plate"),
    ("iron_pressure_plate",  "default_iron_pressure_plate",  "default_iron_pressure_plate"),
    ("gold_pressure_plate",  "default_gold_pressure_plate",  "default_gold_pressure_plate"),
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
    elif shape == "snow_layer":
        # t525 积雪层薄板：全 footprint、1/8 厚（y[0, 1/8]）。机制等价 MC 1.0 snow layer 单层 1/8 格厚。
        #   区别于 slab（半高）—— 更薄 → 手持 / 背包图标一眼辨「这是积雪层薄板」而非雪块满格立方。
        #   fill = default_snow（冷白冰晶噪点，同 worldgen SnowLayer 各面贴图）。
        boxes = [(0.0, 1.0, 0.0, 0.125, 0.0, 1.0)]
        y_min, y_max = 0.0, 0.125
    elif shape == "table":
        # review L20 附魔台矮盒：全 footprint、12/16 高（y[0, 0.75]）。机制等价世界内 PartialBlockGeometry
        #   [0,0.75] 盒（e260b2d BlockDef 附魔台 12/16 高，机制对齐 MC 1.0）。fill 顶=附魔台顶（黑曜石+
        #   钻石+立书）/ 侧=附魔台侧（底面 obsidian 不进图标 —— dimetric 顶+两侧投影本就不画底面）。
        boxes = [(0.0, 1.0, 0.0, 0.75, 0.0, 1.0)]
        y_min, y_max = 0.0, 0.75
    elif shape == "trapdoor":
        boxes = [(0.0, 1.0, 0.0, 0.1875, 0.0, 1.0)]                    # 合态薄板
        y_min, y_max = 0.0, 0.1875
    elif shape == "pressure_plate":
        boxes = [(1.0 / 16.0, 15.0 / 16.0, 0.0, 1.0 / 16.0,
                  1.0 / 16.0, 15.0 / 16.0)]                            # 贴地薄板 + 边距
        y_min, y_max = 0.0, 1.0 / 16.0
    elif shape == "button":
        # t662 按钮凸钮：6×2×6px 量级居中单盒（贴地态 —— 图标取地面放置观感；wall 态几何同款转轴）。
        #   机制等价 MC button 6/16 见方 × 2/16 厚。区别 pressure_plate（15/16 宽薄条）—— 小钮居中、
        #   一眼可辨「这是按钮不是压力板」。
        boxes = [(5.0 / 16.0, 11.0 / 16.0, 0.0, 2.0 / 16.0,
                  5.0 / 16.0, 11.0 / 16.0)]
        y_min, y_max = 0.0, 2.0 / 16.0
    elif shape == "lever":
        # t662 拉杆：圆石底座（6×3×6）+ 斜插摆棍两段阶梯盒（off 摆向，同世界内 mechBoxes 贴地 off 几何）。
        #   机制等价 MC lever = cobble base + stick；棍贴木板材质由 caller 传 fill_side 覆盖（fill_top=底座
        #   cobble / fill_side=棍 planks —— 本 shape 的盒 0 用 top、盒 1.. 用 side 贴图，见下方渲染循环特判）。
        boxes = [
            (5.0 / 16.0, 11.0 / 16.0, 0.0, 3.0 / 16.0, 5.0 / 16.0, 11.0 / 16.0),  # 底座
            (7.0 / 16.0, 9.0 / 16.0, 2.0 / 16.0, 5.0 / 16.0, 4.0 / 16.0, 8.0 / 16.0),  # 棍低段（向 -Z 倾）
            (7.0 / 16.0, 9.0 / 16.0, 4.0 / 16.0, 8.0 / 16.0, 2.0 / 16.0, 6.0 / 16.0),  # 棍高段（远端更高）
        ]
        y_min, y_max = 0.0, 8.0 / 16.0
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

    for bi, (x0, x1, y0, y1, z0, z1) in enumerate(boxes):
        # t662 lever shape：盒 0（底座）贴 fill_top（圆石）、盒 1..（摆棍）贴 fill_side（木板棍）——
        #   两个 fill 各司其职；其余 shape 恒 top/side 同传（既有图标零回归）。
        box_top, box_side = top, side
        if shape == "lever" and bi > 0:
            box_top, box_side = side, side
        _render_box_d(canvas, depth_buf, x0, x1, y0, y1, z0, z1, box_top, box_side, cy_local, scale)

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


# ── t644 从 pack 贴图渲染「放置态一致」图标 ──────────────────────────────────────
# pack 目录（gitignored 只读参考；本工程 demo 包实际布局 assets/minecraft/textures/block）。
PACK_BLOCK = os.path.join(HERE, "..", "docs", "Default HD 128x Demo 1.8.2.2",
                          "assets", "minecraft", "textures", "block")


def load_pack_face(filename, alpha="fill"):
    """读 pack block 贴图 → FACE_RES² RGBA float（与 load_face 同构）。

    alpha 处理两种策略（放置态语义不同，逐块在 FROM_PACK 表选）：
      "fill" —— 透明像素用不透明均值填掉、强制不透明（同 load_face；实心立方体面用）。
      "keep" —— 保留透明（cross / 贴地薄片用；render_flat_pack 直接放大）。
    """
    p = os.path.join(PACK_BLOCK, filename)
    img = Image.open(p).convert("RGBA").resize((FACE_RES, FACE_RES), Image.NEAREST)
    arr = np.asarray(img, dtype=np.float64)
    if alpha == "fill":
        a = arr[..., 3]
        opaque = a >= 128
        if not opaque.all():
            fill = arr[opaque][:, 0:3].mean(axis=0) if opaque.any() \
                else np.array([90.0, 90.0, 90.0])
            arr[~opaque, 0:3] = fill
            arr[..., 3] = 255.0
    return arr


def render_flat_pack(filename):
    """pack 平面 2D 图标（透明底保留 alpha，同 render_flat_2d；铁轨 / 红石火把 cross 族用）。"""
    p = os.path.join(PACK_BLOCK, filename)
    img = Image.open(p).convert("RGBA")
    return img.resize((OUT, OUT), Image.NEAREST)


def render_pack_box(boxes, top, side, front=None, cy_local=None, scale=1.0,
                    side_v0=0.0, side_v1=1.0):
    """pack 贴图版形状渲染：boxes 轴对齐子盒列表 + depth buffer（同 render_partial_3d 的几何管线）。

    与 render_partial_3d 的差异（贴合「放置态」）：
      - 贴图来自 pack（load_pack_face 预载的 float 数组，非 textures/default_*）。
      - front 非空时 +Z 面贴 front（机关盒族正面辨识特征：炉口 / 排出口），否则贴 side。
      - side_v0/side_v1 —— 侧面 V 采样窗口 [v0,v1]（MC 矮模型侧面贴图自带顶部空白带：
        附魔台侧顶 4/16、祭坛侧顶 3/16 空白；引擎放置态 = cropTopBlank 裁空白后整张拉伸。
        图标同样只采样窗口段 → 侧面观感与放置态逐像素同源）。
      - 采样行序：ty = floor((1-v)*(h)) —— v=0 盒底 → 采样贴图末行（贴图底部），v=1 盒顶 → 贴图
        顶部行。即贴图顶行朝上（与引擎 pushBox cv=y、V 轴随 y 增一致 —— 世界 v0 是图集底部）。
        这与既有 _render_box_d（ty=floor(v*h)，贴图顶行朝下）不同：旧程序贴图多为对称纹理
        （木板 / 圆石 / 石砖）翻转不可见，而 pack 非对称侧贴图（门锁孔在底 / 祭坛孔带）必须
        顶行朝上才与放置态一致 —— 故 pack 路径独立实现采样，不动既有已固化图标。
    """
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    depth_buf = np.full((W, W), -np.inf)

    top_face = top
    side_face = side
    front_face = side if front is None else front

    def sample_win(face, u, v, w0, w1):
        """按窗口 [w0,w1] 采样（w 沿贴图行 0=顶 → 末=底）。v=0 盒底 → 窗口底 w1；v=1 盒顶 → 窗口顶 w0。"""
        t = w0 + (1.0 - v) * (w1 - w0)  # v=1→w0（贴图顶）, v=0→w1（贴图底）
        ty = np.clip(np.floor(t * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
        tx = np.clip(np.floor(u * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
        return face[ty, tx]

    def paint(o_s, uax_s, vax_s, depth_fn, face, shade, w0, w1):
        u, vv = face_uv(o_s, uax_s, vax_s)
        m = (u >= 0) & (u <= 1) & (vv >= 0) & (vv <= 1)
        u_cl = np.clip(u, 0, 1)
        v_cl = np.clip(vv, 0, 1)
        depth = depth_fn(u_cl, v_cl)
        col = sample_win(face, u_cl, v_cl, w0, w1).copy()
        col[..., 0:3] *= shade
        draw = m & (depth > depth_buf)
        canvas[draw] = col[draw]
        depth_buf[draw] = depth[draw]

    for (x0, x1, y0, y1, z0, z1) in boxes:
        def face_depth(ox, oy, oz, ux, uy, uz, vx, vy, vz):
            return lambda u, v: (ox + u * ux + v * vx) + (oy + u * uy + v * vy) + (oz + u * uz + v * vz)
        # 顶面 y=y1：u→+x，v→+z（同引擎 +Y cu=x cv=z），满窗采样。
        o_s = project_pt(x0, y1, z0, cy_local, scale)
        paint(o_s,
              project_pt(x1, y1, z0, cy_local, scale) - o_s,
              project_pt(x0, y1, z1, cy_local, scale) - o_s,
              face_depth(x0, y1, z0, x1 - x0, 0, 0, 0, 0, z1 - z0),
              top_face, 1.00, 0.0, 1.0)
        # 右面 x=x1：u→+z，v→+y（引擎 +X cu=z cv=y），侧窗采样。
        o_s = project_pt(x1, y0, z0, cy_local, scale)
        paint(o_s,
              project_pt(x1, y0, z1, cy_local, scale) - o_s,
              project_pt(x1, y1, z0, cy_local, scale) - o_s,
              face_depth(x1, y0, z0, 0, 0, z1 - z0, 0, y1 - y0, 0),
              side_face, 0.80, side_v0, side_v1)
        # 左面 z=z1：u→+x，v→+y（引擎 +Z cu=x cv=y）—— 机关盒族正面（+Z 朝玩家）贴 front。
        o_s = project_pt(x0, y0, z1, cy_local, scale)
        paint(o_s,
              project_pt(x1, y0, z1, cy_local, scale) - o_s,
              project_pt(x0, y1, z1, cy_local, scale) - o_s,
              face_depth(x0, y0, z1, x1 - x0, 0, 0, 0, y1 - y0, 0),
              front_face, 0.62, side_v0, side_v1)

    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    return img.resize((OUT, OUT), Image.LANCZOS)


def load_pack_face_or_none(filename):
    """pack 贴图探测加载（缺文件返 None 供候选链降级；t675 南瓜刻面前贴图用）。"""
    p = os.path.join(PACK_BLOCK, filename)
    if not os.path.exists(p):
        return None
    return load_pack_face(filename)


def pick_pumpkin_face():
    """t675 南瓜前面刻脸贴图选择 —— 复刻 resourcepackmanager tile 118 退化回退链（单一权威）。

    demo 包实测 pumpkin_face_off.png 是 pumpkin_side.png 的逐字节拷贝（懒包复用文件）→ 直接用会在
    图标里画成「三面瓜棱无刻脸」。回退链与引擎运行期一致：face_off 先查退化（与 side 同像素）→
    carved_pumpkin.png（经典刻脸）→ pumpkin_face_on.png（发光刻脸）→ 全退化则 None（图标退化为
    三面同 side 的纯瓜棱立方，与运行期无候选分支语义一致）。
    """
    side = load_pack_face("pumpkin_side.png")
    face = load_pack_face_or_none("pumpkin_face_off.png")
    if face is not None and side is not None and not np.array_equal(face, side):
        return "pumpkin_face_off.png"
    for cand in ("carved_pumpkin.png", "pumpkin_face_on.png"):
        c = load_pack_face_or_none(cand)
        if c is not None and side is not None and not np.array_equal(c, side):
            return cand
    return None


def render_pack_front(front, top, side):
    """pack 版「正面为主」投影（机关盒族：正面辨识特征不被立体投影遮挡；同 render_front 几何 /
    明暗 / 深度剪切常数，仅贴图源换 pack）。用于 t644 重生成 dispenser / dropper —— 放置态
    前面朝玩家，正面为主图标的「所见面 = 放置前面」与放置观感最近。"""
    canvas = np.zeros((W, W, 4), dtype=np.float64)
    FBL = np.array([F_LO, F_HI]); FBR = np.array([F_HI, F_HI])
    FTR = np.array([F_HI, F_LO]); FTL = np.array([F_LO, F_LO])
    depth = np.array([F_DDX, F_DDY])
    BBR = FBR + depth; BTR = FTR + depth; BTL = FTL + depth

    def paint(o, uax, vax, face, shade):
        u, v = face_uv(o, uax, vax)
        m = (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)
        tx = np.clip(np.floor(np.clip(u, 0, 1) * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
        # v=0 屏幕底 → 贴图末行（贴图顶行朝上，同 render_pack_box 行序 —— 与放置态一致）。
        ty = np.clip(np.floor((1.0 - np.clip(v, 0, 1)) * FACE_RES).astype(np.int32), 0, FACE_RES - 1)
        col = face[ty, tx].copy()
        col[..., 0:3] *= shade
        canvas[m] = col[m]

    paint(FBR, BBR - FBR, FTR - FBR, side, 0.70)   # 右面（+X）
    paint(FTL, FTR - FTL, BTL - FTL, top, 0.85)     # 顶面（+Y）
    paint(FBL, FBR - FBL, FTL - FBL, front, 1.00)   # 正面（+Z，主面）
    img = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8), "RGBA")
    return img.resize((OUT, OUT), Image.LANCZOS)


# t644 转换表：icon 名 → 渲染方案。形状 / 贴图窗口与放置态（partialblockgeometry / chunkgeometry
#   per-face + tileFilenameMap 的 pack 覆盖）逐项对齐：
#     cube      — 满立方（顶=top 侧=side，同放置六面 per-face：顶 topTile / 侧 sideTile）。
#     cube_front— 满立方 + 可见面（+Z 左面）贴 front（t675 南瓜：顶=瓜顶带茎 / 右=瓜棱 / 左（+Z 前方）=
#                 刻脸 —— 放置态恒面向玩家（t638 放置朝向），图标「顶 + 刻脸 + 瓜棱」同款经典读感）。
#     front     — 正面为主投影（机关盒族：放置态 frontTile 朝玩家）。
#     table     — 0.75 矮盒（附魔台放置 y[0,0.75]；侧贴图带顶部 4/16 空白 → 侧窗 [0.25,1] = 引擎
#                 cropTopBlank(0.25) 后整张拉伸的等价采样）。
#     frame     — 满立方（祭坛放置态整格；侧贴图顶部 3/16 空白 → 侧窗 [0.1875,1] = cropTopBlank(0.1875)）。
#     door      — 两格高 3/16 薄板（放置态；上盒贴 upper、下盒贴 lower —— per-face bit3）。
#     plate     — 贴地 1/16 薄板（放置态 ShapePlate pushBox xz[1/16,15/16] y[0,1/16]）。
#     flat      — 平面 2D 保留 alpha（cross / 贴地薄片：铁轨 / 红石火把 —— 放置态即 2D quad/cross）。
#   pack 侧贴图空白的行序注意：load_pack_face 不裁剪，靠 side_v0/v1 窗口采样（空白带在贴图顶部）。
FROM_PACK = [
    # t675 南瓜 3D 图标重做（拼方块 cube per-face）：满立方 + 前面（+Z 左面）贴刻脸。顶=pumpkin_top
    #   （瓜顶带茎）/ 右=pumpkin_side（瓜棱）/ 左=刻脸（pick_pumpkin_face 复刻引擎 tile 118 退化回退链
    #   —— face_off 是 side 拷贝时退 carved → face_on）。放置态 t638 恒面向玩家 → 图标三面与放置观感
    #   同源（顶 + 刻脸 + 瓜棱），与 t482 旧版「顶 + 两侧瓜棱」（无刻脸、读不出南瓜）区分。
    #   front="pumpkin_face" 哨兵 → cube_front 分支走 pick_pumpkin_face 候选链（非固定文件名）。
    ("pumpkin", "cube_front", dict(top="pumpkin_top.png", side="pumpkin_side.png",
                                   front="pumpkin_face")),
    # t676 机关盒族五件 3D 立方体图标（用户点名「t644 的 front 方案太扁平」→ 全升 cube per-face）：
    #   满立方 dimetric（顶 + 右侧 + 前面）—— 顶 / 侧 / 前**三面独立贴图**，正面辨识特征（炉口 / 网格 /
    #   排出口 / TNT 标识）贴在可见 +Z 前面（放置态恒面向玩家，t638 朝向）→ 既有体积感（三面明暗）又有
    #   正面特征。与 t537 的 2D pack front 图（纯平面）和 t492/t644 render_pack_front（正面为主 + 细带）
    #   区分 —— 那两版「平」的根因是正面占画面但纵深带太窄 / 纯 2D 无面差。
    #   furnace：顶/侧=furnace 系（MC 1.0 无独立 furnace_side 正面差，侧同图）/ 前=furnace_front（炉口）。
    #   crafting_table：顶=crafting_table_top（网格台面）/ 侧=crafting_table_side / 前=crafting_table_front
    #   （带工具挂件的前图，MC 1.8+ 有独立 front）。dispenser / dropper：顶/侧复用熔炉系（MC 1.0 复用）/
    #   前=*_front_horizontal（排出口）。tnt：顶=tnt_top（引线接口俯视）/ 侧=tnt_side / 前=tnt_side
    #   （MC 1.0 TNT 前面与侧面同图 —— 标识在侧向绕行，dimetric 左面即可见）。
    ("crafting_table", "cube_front", dict(top="crafting_table_top.png",
                                          side="crafting_table_side.png",
                                          front="crafting_table_front.png")),
    ("furnace",        "cube_front", dict(top="furnace_top.png",
                                          side="furnace_side.png",
                                          front="furnace_front.png")),
    ("dispenser",      "cube_front", dict(top="furnace_top.png",
                                          side="furnace_side.png",
                                          front="dispenser_front_horizontal.png")),
    ("dropper",        "cube_front", dict(top="furnace_top.png",
                                          side="furnace_side.png",
                                          front="dropper_front_horizontal.png")),
    ("tnt",            "cube_front", dict(top="tnt_top.png",
                                          side="tnt_side.png",
                                          front="tnt_side.png")),
    # 附魔台：0.75 矮盒（放置态 y[0,0.75]），顶 = enchanting_table_top，侧 = enchanting_table_side
    #   顶部 4/16 空白（引擎合成 cropTopBlank(0.25) → 有效 0.75 整张贴 0.75 高侧面）。
    ("enchanting_table", "table", dict(top="enchanting_table_top.png",
                                       side="enchanting_table_side.png")),
    # 末地祭坛（EndPortal 方块 endframe 化）：放置态整格满立方；顶 = endframe_top（未放之眼态），
    #   侧 = endframe_side 顶部 3/16 空白（cropTopBlank(0.1875)）。
    ("end_portal", "frame", dict(top="endframe_top.png",
                                 side="endframe_side.png")),
    # 书架：满立方；放置态顶/底 = planks(8)→oak_planks、侧 = bookshelf。
    ("bookshelf", "cube", dict(top="oak_planks.png", side="bookshelf.png")),
    # 铁轨族：贴地薄片 2D（放置态 = 一片水平双面 quad 贴底 1/16；icon 走 flat 保留 alpha
    #   —— 与放置观感一致：透明底 + 轨像素）。
    ("rail",         "flat", "rail_normal.png"),   # 直轨（放置态 tile 121）
    ("golden_rail",  "flat", "powered_rail.png"),  # 动力轨（放置态断常 tile 157）
    ("detector_rail","flat", "detector_rail.png"), # 探测轨（放置态断常 tile 158）
    # 红石火把：cross 2D（放置态两片对角双面 quad，cutout）。
    ("redstone_torch", "flat", "redstone_torch_on.png"),
    # 红石灯：满立方 off 态（放置态默认 off；on 是点亮视觉非物品态）。
    ("redstone_lamp", "cube", dict(top="redstone_lamp_off.png", side="redstone_lamp_off.png")),
    # 矿物存储块族：满立方（放置态六面同贴图）。
    ("iron_block",     "cube", dict(top="iron_block.png",     side="iron_block.png")),
    ("coal_block",     "cube", dict(top="coal_block.png",     side="coal_block.png")),
    ("lapis_block",    "cube", dict(top="lapis_block.png",    side="lapis_block.png")),
    ("diamond_block",  "cube", dict(top="diamond_block.png",  side="diamond_block.png")),
    ("gold_block",     "cube", dict(top="gold_block.png",     side="gold_block.png")),
    ("redstone_block", "cube", dict(top="redstone_block.png", side="redstone_block.png")),
    # 门（两格高 3/16 薄板；上盒 upper / 下盒 lower —— 放置态 per-face bit3）。
    ("wood_door",   "door", dict(upper="door_wood_upper.png",   lower="door_wood_lower.png")),
    ("spruce_door", "door", dict(upper="door_spruce_upper.png", lower="door_spruce_lower.png")),
    # t722 铁门：pack door_iron_upper/lower（HD 128px，1.8 老命名）两格高 3/16 薄板，与放置态一致。
    ("iron_door",   "door", dict(upper="door_iron_upper.png",   lower="door_iron_lower.png")),
    # t742 铁活板门图标重做（用户「现像厚铁压力板」）：trapdoor 模式 —— 合态薄板（全 footprint
    #   y[0,3/16]）顶面贴 pack iron_trapdoor（**alpha=keep 保孔洞透明** → 图标即带四孔的活板门造型，
    #   与放置态 cutout 透视一致；旧 partial 模式 fill 把孔填均色 → 实心薄板观感 = 厚铁压力板）+
    #   两侧面贴 iron_block（放置态 t742 薄侧边 = 铁块贴图，图标与放置同源）。
    ("iron_trapdoor", "trapdoor", dict(top="iron_trapdoor.png", side="iron_block.png")),
    # t714 ④木半方块老图标重做（用户「木台阶/栅栏/楼梯等放置贴图对但图标旧」）：wood_slab / wood_stairs /
    #   wood_fence 旧图标是 t163/t169 程序 default_wood（16px 木板）烘的 dimetric——pack 激活时世界放置走
    #   HD oak_planks 而背包还是低清程序木纹 → 观感漂移。本段 partial 模式：pack oak_planks / spruce_planks
    #   按 slab/stairs/fence 真实形状（render_partial_3d 同款子盒 + depth buffer）重烘 → 图标与放置贴图同源。
    #   云杉门（spruce_door）上文已有 door 模式；云杉台阶/栅栏（spruce_slab/spruce_fence）同段接上。
    ("wood_slab",     "partial", dict(fill="oak_planks.png",     shape="slab")),
    ("wood_stairs",   "partial", dict(fill="oak_planks.png",     shape="stairs")),
    ("wood_fence",    "partial", dict(fill="oak_planks.png",     shape="fence")),
    ("spruce_slab",   "partial", dict(fill="spruce_planks.png",  shape="slab")),
    ("spruce_fence",  "partial", dict(fill="spruce_planks.png",  shape="fence")),
    # t714 ③叶图标同步（程序路径）：icon_leaves 旧版是 7/26 烘的旧绿立方；用当前 default_leaves /
    #   default_spruce_leaves（透明孔被 fill 填实心）重烘 → 顶 + 两侧明暗色调与放置贴图一致（pack 激活时
    #   进一步被 blockItemIconSource 的叶 tint 染色 2D 图覆盖）。
    ("leaves",        "partial", dict(fill="oak_leaves.png",     shape="cube")),
    ("spruce_leaves", "partial", dict(fill="spruce_leaves.png",  shape="cube")),
    # 压力板家族（t627 石/铁/金）：贴地 1/16 薄板。demo 包无 plate 专属 PNG（实测缺）→ 用放置
    #   态语义贴图：石板 = smooth_stone（MC 1.0 石压力板贴图即平滑石面）、铁/金 = 各金属块面
    #   （MC 1.0 加权压力板贴图 = 金属块面 + 中央孔，无孔面降级仍可辨材质）。
    ("stone_pressure_plate", "plate", dict(fill="smooth_stone.png")),
    ("iron_pressure_plate",  "plate", dict(fill="iron_block.png")),
    ("gold_pressure_plate",  "plate", dict(fill="gold_block.png")),
]


def _partial_shape_boxes(shape):
    """t714 partial 模式：shape 名 → 轴对齐子盒列表（与 render_partial_3d 同款形状定义，fill 换 pack 贴图）。
    cube = 满立方（叶子图标路径：pack 灰度叶由 caller 乘 tint 后投影）。"""
    if shape == "cube":
        return [(0.0, 1.0, 0.0, 1.0, 0.0, 1.0)]
    if shape == "slab":
        return [(0.0, 1.0, 0.0, 0.5, 0.0, 1.0)]
    if shape == "trapdoor":
        # t723 铁活板门：合态薄板（全 footprint，y[0, 3/16]；与 render_partial_3d trapdoor 同款盒）。
        return [(0.0, 1.0, 0.0, 0.1875, 0.0, 1.0)]
    if shape == "stairs":
        return [(0.0, 1.0, 0.5, 1.0, 0.0, 0.5),   # 背墙
                (0.0, 1.0, 0.0, 0.5, 0.0, 1.0)]  # 整步
    if shape == "fence":
        return [(6.0 / 16.0, 10.0 / 16.0, 0.0, 1.0, 6.0 / 16.0, 10.0 / 16.0),  # 立柱
                (0.0, 1.0, 12.0 / 16.0, 15.0 / 16.0, 6.0 / 16.0, 10.0 / 16.0),  # 上横档
                (0.0, 1.0,  5.0 / 16.0,  8.0 / 16.0, 6.0 / 16.0, 10.0 / 16.0)]  # 下横档
    raise ValueError(f"unknown partial shape {shape}")


def _partial_y_mid(shape):
    """partial 模式竖直居中的 y_mid（与 render_partial_3d scale==1 同式；单盒形状直接 (y0+y1)/2）。"""
    if shape == "cube":
        return 0.5
    if shape == "slab":
        return 0.25
    if shape == "trapdoor":
        return 0.1875 / 2.0  # t723 薄板半高（竖直居中同 slab 公式）
    return 0.5  # stairs / fence（多盒取整体包络中点，同 render_partial_3d）


def run_from_pack():
    """t644 批量转换入口：遍历 FROM_PACK 表，按方案渲染 pack 贴图图标 → textures/icon_<name>.png。
    单块 pack 文件缺失 → 跳过该块（保留现有图标）并打印 SKIP（不中断整批）。
    t742 追加按名过滤：`--from-pack <name> [<name> ...]` 只重生成点名条目（全量批跑会把无关 icon
    一并重写 → git diff 不可控；点名重生成 = 单任务单图标最小改动面）。"""
    only = set(a for a in sys.argv[1:] if a != "--from-pack") or None
    for entry in FROM_PACK:
        name, mode, spec = entry
        if only is not None and name not in only:
            continue
        try:
            if mode == "flat":
                img = render_flat_pack(spec)
            elif mode == "front":
                img = render_pack_front(load_pack_face(spec["front"]),
                                        load_pack_face(spec["top"]),
                                        load_pack_face(spec["side"]))
            elif mode == "cube":
                img = render_pack_box([(0.0, 1.0, 0.0, 1.0, 0.0, 1.0)],
                                      load_pack_face(spec["top"]),
                                      load_pack_face(spec["side"]),
                                      cy_local=W / 2.0 - 0.5 * v)
            elif mode == "cube_front":
                # t675/t676 满立方 + 前面（+Z 可见左面）贴独立 front：南瓜（刻脸候选链）+ 机关盒族五件
                #   （熔炉炉口 / 工作台前图 / 发射器 / 投掷器排出口 / TNT 标识）。front 值 "pumpkin_face"
                #   哨兵走 pick_pumpkin_face 候选链；否则按字面文件名（缺 → None 回退 side）。
                front_name = pick_pumpkin_face() if spec.get("front") == "pumpkin_face" else spec.get("front")
                img = render_pack_box([(0.0, 1.0, 0.0, 1.0, 0.0, 1.0)],
                                      load_pack_face(spec["top"]),
                                      load_pack_face(spec["side"]),
                                      front=load_pack_face(front_name) if front_name else None,
                                      cy_local=W / 2.0 - 0.5 * v)
            elif mode == "table":
                # 0.75 矮盒（附魔台）；侧窗 [0.25,1]（顶部空白带裁除，= cropTopBlank(0.25)）。
                img = render_pack_box([(0.0, 1.0, 0.0, 0.75, 0.0, 1.0)],
                                      load_pack_face(spec["top"]),
                                      load_pack_face(spec["side"]),
                                      cy_local=W / 2.0 - 0.625 * v,
                                      side_v0=0.25, side_v1=1.0)
            elif mode == "frame":
                # 满立方（祭坛放置态整格）；侧窗 [0.1875,1]（顶部空白裁除，= cropTopBlank(0.1875)）。
                img = render_pack_box([(0.0, 1.0, 0.0, 1.0, 0.0, 1.0)],
                                      load_pack_face(spec["top"]),
                                      load_pack_face(spec["side"]),
                                      cy_local=W / 2.0 - 0.5 * v,
                                      side_v0=0.1875, side_v1=1.0)
            elif mode == "door":
                up = load_pack_face(spec["upper"])
                lo = load_pack_face(spec["lower"])
                # 门放置态：两格高 3/16 薄板贴 -Z 边（partialblockgeometry door case 厚 3/16）。
                #   上半 y[1,2] 贴 upper、下半 y[0,1] 贴 lower（per-face bit3 选图）。分两次渲染
                #   各自盒再合成（上盒 cy 抬高 1 格；共享 depth 不必要 —— 两盒屏幕不重叠区
                #   各自采样，直接两次 pass 画在同一画布：先画下半（近），再画上半（远在上）。
                #   scale=0.7 同既有 icon_wood_door 几何常数。
                # t741 修「门图标上下两半中缝断开成空气」（铁 / 木 / 云杉三门同病）：旧 cy 硬编码
                #   0.62W / 0.12W（两基线间距 0.5W）而每半盒的屏幕身高只有 v*scale=0.35W → 中缝
                #   0.15W 空气（64px 图标上 ~10px 断带），且上溢出画布顶 / 下切画布底。改为由
                #   几何常量推导，两步：
                #   ① 无缝拼接 —— 连续投影式 sy = cy + (1-y)*v*s 下，世界 y=1 是两半共享缝：
                #     下半盒顶边（盒 y=1）与上半盒底边（盒 y=0，世界 y=1）共边 ⇔ cy_up = cy_lo - v*s。
                #   ② 整门竖直居中 —— 轮廓最高点 = 上半盒顶菱形 N 角（cy_up - dv*s）、最低点 =
                #     下半盒右下角（cy_lo + v*s + (1+z1)*dv*s），总高 H = 2*v*s + (1+z1)*dv*s，
                #     取轮廓中点 = W/2 反解 cy_up。
                z1 = 3.0 / 16.0
                sc = 0.7  # 两格高整体缩到画布内（同既有门图标比例）
                H = 2.0 * v * sc + (1.0 + z1) * dv * sc  # 整门轮廓总高（两段侧身高 + 顶/底斜差）
                cy_up = W / 2.0 + dv * sc - H / 2.0       # 上半盒基线（居中反解）
                cy_lo = cy_up + v * sc                     # 下半盒基线（无缝缝位）
                img_lo = render_pack_box([(0.0, 1.0, 0.0, 1.0, 0.0, z1)],
                                         lo, lo, cy_local=cy_lo, scale=sc)
                img_up = render_pack_box([(0.0, 1.0, 0.0, 1.0, 0.0, z1)],
                                         up, up, cy_local=cy_up, scale=sc)
                canvas = Image.new("RGBA", (OUT, OUT), (0, 0, 0, 0))
                canvas.alpha_composite(img_up)
                canvas.alpha_composite(img_lo)
                img = canvas
            elif mode == "plate":
                # t662 修「石/铁/金压力板图标只显示上半截卡底」：旧 cy_local = W/2 - 0.5*v*(1/16)*0.5 ≈ W/2
                #   （把薄板投到画布最底缘 → bbox y[48,63] 卡底切半；wood/cobble 走 render_partial_3d 的
                #   y_mid 居中公式故正常）。改用与 render_partial_3d scale==1 相同的 y_mid 公式：
                #   cy = W/2 - (1 - y_mid)*v（y_mid = 薄板半高 1/32）→ 薄板投影竖直居中（同 wood 版观感）。
                fill = load_pack_face(spec["fill"])
                img = render_pack_box([(1.0 / 16.0, 15.0 / 16.0, 0.0, 1.0 / 16.0,
                                        1.0 / 16.0, 15.0 / 16.0)],
                                      fill, fill,
                                      cy_local=W / 2.0 - (1.0 - 1.0 / 32.0) * v)
            elif mode == "trapdoor":
                # t742 铁活板门：合态薄板（y[0,3/16] 全 footprint）；顶面 pack iron_trapdoor 保透明孔
                #   （load_pack_face alpha="keep" —— 四孔在图标上透底，与放置态 cutout 透视一致），
                #   两侧面 iron_block（放置态薄侧边铁皮包边，partialblockgeometry t742 per-face 同源）。
                #   竖直居中同 partial 模式 y_mid 公式（_partial_y_mid("trapdoor")=3/32）。
                img = render_pack_box(_partial_shape_boxes("trapdoor"),
                                      load_pack_face(spec["top"], alpha="keep"),
                                      load_pack_face(spec["side"]),
                                      cy_local=W / 2.0 - (1.0 - _partial_y_mid("trapdoor")) * v)
            elif mode == "partial":
                # t714 ④/③ 木半方块 + 叶图标重做：pack fill 贴图按 slab/stairs/fence/cube 真实形状投影
                #   （render_pack_box 子盒 + depth buffer，同 render_partial_3d 几何但贴图源换 pack）。
                #   竖直居中：y_mid 公式（cube y_mid=0.5 / slab 0.25 / stairs 0.5 / fence 0.5 —— 与
                #   render_partial_3d scale==1 同式）。灰度叶贴图（oak/spruce_leaves）乘叶 tint（与
                #   resourcepackmanager tileTint 同色板）→ 图标色调 = pack 激活时的放置观感。
                boxes = _partial_shape_boxes(spec["shape"])
                fill = load_pack_face(spec["fill"])
                tint = None
                if spec["fill"] in ("oak_leaves.png", "spruce_leaves.png"):
                    tint = ((0x5a, 0x8a, 0x3a) if spec["fill"] == "oak_leaves.png"
                            else (0x3a, 0x6e, 0x55))  # 叶 tint（plains / 云杉深蓝绿；同 tileTint）
                if tint is not None:
                    t = np.array(tint, dtype=np.float64) / 255.0
                    fill[..., 0:3] = (fill[..., 0:3] * t).clip(0, 255)  # 灰度 × tint（alpha 不动）
                y_mid = _partial_y_mid(spec["shape"])
                img = render_pack_box(boxes, fill, fill,
                                      cy_local=W / 2.0 - (1.0 - y_mid) * v)
            else:
                print("SKIP (unknown mode)", name)
                continue
            out_path = os.path.join(SRC, "icon_" + name + ".png")
            img.save(out_path)
            print("wrote", os.path.relpath(out_path, HERE), img.size)
        except FileNotFoundError as e:
            print("SKIP", name, "-", e.filename, "missing in pack")


def main():
    # t644 `--from-pack` 模式：从 pack 面贴图按放置形状渲染图标（batch 表驱动），不跑程序贴图全量重生成
    #   （避免无关 icon 被意外重写 —— 全量 main() 会按当前 textures/default_* 重烘全部图标）。
    if "--from-pack" in sys.argv:
        run_from_pack()
        return
    for out_name, top_name, side_name in BLOCKS:
        if out_name == "torch":
            # 火把走平面 2D 路径（透明底保留 alpha），非立方体投影（见 render_flat_2d 注释）。
            img = render_flat_2d(top_name)
        else:
            img = render(top_name, side_name)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t492 「正面有辨识特征」方块（熔炉 / 工作台 / 发射器）正面为主 dimetric 立体图标（render_front）：
    #   正面（+Z）贴 front 贴图成主面，显炉口 / 网格 / 排出口面板；顶 + 右深度细带保 3D 体积感。
    for out_name, front_name, top_name, side_name in BLOCKS_FRONT:
        img = render_front(front_name, top_name, side_name)
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
    # t600 石砖台阶/楼梯 3D dimetric 立体图标：slab/stairs shape，fill = default_stone_brick（机制等价圆石变体流程）。
    for out_name, shape in PARTIALS_3D_STONE_BRICK:
        img = render_partial_3d(shape, "default_stone_brick", "default_stone_brick")
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t490/t662 手动 TNT 点火机关 3D dimetric 立体图标（button / lever shape + MECH_FILL 贴图；机制等价
    #   世界内 mechBoxes 几何 —— t662 重做前是 pressure_plate 薄板，用户「跟压力板一模一样」）。
    for out_name, shape in PARTIALS_3D_IGNITER:
        fill_top, fill_side = MECH_FILL[out_name]
        img = render_partial_3d(shape, fill_top, fill_side)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t627 压力板家族扩展 3D dimetric 立体图标（pressure_plate shape + 各自独立瓦片 fill；机制等价木/圆石压力板图标流程）。
    for out_name, fill_top, fill_side in PARTIALS_3D_PLATE_FAMILY:
        img = render_partial_3d("pressure_plate", fill_top, fill_side)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # t525 积雪层薄板图标（1/8 厚，机制等价 MC snow layer 单层薄板）：fill = default_snow，shape=snow_layer。
    #   区别于雪块满格立方（icon_snow.png）—— 手持 / 背包 / 掉落实体据 isPartialBlock(SnowLayer)=true 路由到本图标。
    for out_name, fill_top, fill_side in [("snow_layer", "default_snow", "default_snow")]:
        img = render_partial_3d("snow_layer", fill_top, fill_side)
        out_path = os.path.join(SRC, "icon_" + out_name + ".png")
        img.save(out_path)
        print("wrote", os.path.relpath(out_path, HERE), img.size)
    # review L20 附魔台矮盒图标（0.75 高，机制等价世界内 [0,0.75] PartialBlockGeometry 盒）：fill 顶=附魔台顶 /
    #   侧=附魔台侧。e260b2d 把方块改 12/16 矮盒（原满立方）后本图标未重生成（75c8f02 后一直满立方投影，
    #   背包/手持与世界内矮台观感不符）→ 本轮补。表 = render_partial_3d table shape（slab 半高家族，
    #   顶+两侧明暗同 cube icon 家族）。
    for out_name, fill_top, fill_side in [
        ("enchanting_table", "default_enchanting_table_top", "default_enchanting_table_side"),
    ]:
        img = render_partial_3d("table", fill_top, fill_side)
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
