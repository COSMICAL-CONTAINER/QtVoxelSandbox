#!/usr/bin/env python3
"""生成 t717 实体贴图批（64×32 系 MC 实体 UV 布局，原创自绘，§9 override (a)）。

R19.10 t727/t728/t730/t731/t732 的实体贴图前置。**布局**参考 docs/Default HD 128x Demo 1.8.2.2/
entity/ 下同名贴图的盒区像素占用（实测后按 MC 标准 box-UV 布局自绘），像素全部为本项目程序
生成的**原创**绘制（PLAN §9 红线：不拷贝任何包内 PNG；名称按 §9 改名表——Enderman→夜行者
Nightwalker / Blaze→燃烬者 Emberling）。

各贴图（base 64×32 / 64×64；与 mobmodel.cpp 的 g_texW/H + setMobTex texOffs 同族 UV 规则）：
  - entity_nightwalker.png（64×32；MC enderman 布局）：head(0,0)8×8×8 / body(16,16)8×12×4 /
    arm(40,16)4×12×4 / leg(0,16)4×12×4。黑曜石黑体 + 微紫纹理；**脸区留纯净黑**（眼由独立
    eyes 层贴）。
  - entity_nightwalker_eyes.png（64×32 同布局）：全透明 + 头前脸窗两粒紫光眼（机制等价 MC
    enderman_eyes 发光层；pack 映射 enderman_eyes.png）。
  - entity_emberling.png（64×32；MC blaze 布局）：head(0,0)8×8×8（实测 blaze 主盒区 (8,0)-(32,16)）
    + rod 棒条区 (0,16)-(8,32)。黄焰头（焰纹 + 亮核）+ 烟灰棒（暗黄竖纹）。
  - entity_squid.png（64×32；MC squid 布局）：mantle(12,0)24×20 + tentacle(0,20)？—— 实测包
    squid.png base 占用（mantle (12,0)-(36,20) / 触腕区 (0,12)-(56,20)）：深蓝头（斑驳浅肚）+
    触腕条。
  - entity_minecart.png（64×32；MC minecart 布局）：侧帮 (0,4)-(44,20) / 底板 (0,20)-(44,28)。
    铁灰壁（铆钉）+ 木底（棕板条）。
  - entity_enchant_book.png（64×32 整页区）：棕封 + 金边 + 白纸页（t732 附魔台悬浮书重贴图，
    供两页盒各取半区）。
  - entity_skin_default.png / entity_skin_alex.png（t747 升 **128×64 HD**：64×32 MC 皮肤布局 ×2 重绘，
    发丝/眉眼（眼白+虹膜+瞳孔+高光）/布纹织理/缝线/鞋底加密；盒区比例不变 → UV 分数不变。侧脸鬓角按
    模板方向贴脸区前缘，alex 右颊长发盖耳——侧脸前后方向的可视校验标记）：头(0,0) / body(32,32) /
    armR(80,32) / legR(0,32)（均 ×2 基准坐标）。蓝衣棕发（default）/ 橙发绿衣棕裤（alex）。

输出（覆盖写入 textures/；**不进图集**——实体贴图走独立 Texture）：
  entity_nightwalker.png / entity_nightwalker_eyes.png / entity_emberling.png /
  entity_squid.png / entity_minecart.png / entity_enchant_book.png /
  entity_skin_default.png / entity_skin_alex.png

pack 运行期映射 entitySource(kind)（resourcepackmanager）：nightwalker→entity/enderman/
enderman.png(+enderman_eyes.png) / emberling→entity/blaze.png / squid→entity/squid.png /
minecart→entity/minecart.png / enchant_book→entity/enchanting_table_book.png /
skin_default→entity/steve.png / skin_alex→entity/alex.png；miss 回退本程序贴图。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
W, H = 64, 32


class Rng:
    """确定性 xorshift（同名重跑同图）。"""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self, lo, hi):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= (x >> 17)
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return lo + (x % (hi - lo + 1))


def canvas():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def rect(img, x0, y0, x1, y1, c):
    # t747：钳制边界改随 img 实际尺寸（原用模块级 W/H=64×32，会截掉 128×64 HD 皮肤的下半）。
    px = img.load()
    for y in range(max(0, y0), min(img.height, y1)):
        for x in range(max(0, x0), min(img.width, x1)):
            px[x, y] = c


def speckle(img, rng, x0, y0, x1, y1, c, density_num, density_den):
    """区域内确定性撒点（密度 n/d）。t747：钳制随 img 实际尺寸（同 rect）。"""
    px = img.load()
    for y in range(max(0, y0), min(img.height, y1)):
        for x in range(max(0, x0), min(img.width, x1)):
            if px[x, y][3] > 0 and rng.next(1, density_den) <= density_num:
                px[x, y] = c


def shade(c, f):
    return (min(255, int(c[0] * f)), min(255, int(c[1] * f)), min(255, int(c[2] * f)), c[3])


def paint_box(img, u0, v0, w, h, d, top, bot, side, front, back):
    """MC box-UV 六面（顶亮 / 底·背暗 / 侧中 / 前特征面）。"""
    rect(img, u0 + d, v0, u0 + d + w, v0 + d, top)
    rect(img, u0 + d + w, v0, u0 + 2 * d + w, v0 + d, bot)
    rect(img, u0, v0 + d, u0 + d, v0 + d + h, side)
    rect(img, u0 + d, v0 + d, u0 + d + w, v0 + d + h, front)
    rect(img, u0 + d + w, v0 + d, u0 + 2 * d + w, v0 + d + h, side)
    rect(img, u0 + 2 * d + w, v0 + d, u0 + 2 * d + 2 * w, v0 + d + h, back)


# ── 夜行者（Nightwalker；机制等价 MC enderman，§9 改名）──
NW_BODY = (16, 16, 26, 255)      # 黑曜石黑体
NW_BODY2 = (28, 22, 40, 255)     # 微紫纹理（虚空缀点）


def draw_nightwalker():
    img = canvas()
    rng = Rng(7271)
    pal = {"top": (26, 22, 34, 255), "bot": (12, 10, 18, 255), "side": NW_BODY,
           "front": (20, 17, 28, 255), "back": (14, 12, 20, 255)}
    paint_box(img, 0, 0, 8, 8, 8, **pal)          # head
    paint_box(img, 16, 16, 8, 12, 4, **pal)       # body
    paint_box(img, 40, 16, 4, 12, 4, **pal)       # armR（armL 镜像同区）
    paint_box(img, 0, 16, 4, 12, 4, **pal)        # legR（legL 镜像同区）
    speckle(img, rng, 0, 0, 64, 32, NW_BODY2, 1, 14)  # 微紫虚空缀点
    # 头前脸区保持纯净黑（眼独立层贴）。
    rect(img, 8, 8, 16, 16, (18, 15, 26, 255))
    return img


def draw_nightwalker_eyes():
    """眼睛层：全透明 + 头前两粒紫光眼（机制等价 MC enderman_eyes 发光层）。

    眼位对齐 head 前面 (u0+d..u0+d+w, v0+d..v0+d+h) = (8..16, 8..16)：行 11、列 9..11 / 12..14
    （实测 pack enderman_eyes 眼在 row 12 区）。"""
    img = canvas()
    eye = (196, 128, 244, 255)
    eye_hi = (236, 208, 255, 255)
    for x in (9, 10, 12, 13):
        rect(img, x, 11, x + 1, 14, eye)
    rect(img, 10, 12, 11, 13, eye_hi)
    rect(img, 13, 12, 14, 13, eye_hi)
    return img


# ── 燃烬者（Emberling；机制等价 MC blaze，§9 改名）──
def draw_emberling():
    img = canvas()
    rng = Rng(7282)
    # 头部盒区（实测 blaze.png 主盒 (8,0)-(32,16)；头 box-UV (0,0)8×8×8 同族）：
    #   黄焰头 + 焰纹 + 白热亮核。
    head_top = (252, 232, 140, 255)
    head_side = (244, 196, 68, 255)
    head_back = (226, 152, 40, 255)
    paint_box(img, 0, 0, 8, 8, 8, top=head_top, bot=(208, 132, 32, 255),
              side=head_side, front=(250, 214, 96, 255), back=head_back)
    # 焰纹（暗橙竖条 + 亮黄斑点，确定性）。
    speckle(img, rng, 8, 0, 32, 16, (226, 148, 36, 255), 1, 6)
    speckle(img, rng, 8, 0, 32, 16, (254, 244, 176, 255), 1, 10)
    # 白热亮核（头前面上部一簇）。
    rect(img, 10, 9, 14, 12, (255, 250, 210, 255))
    # 烟灰棒区（实测 (0,16)-(8,32)：竖棒条）：暗黄底 + 烟灰竖纹 + 亮橙热点。
    rect(img, 0, 16, 8, 32, (108, 92, 62, 255))
    for x in (1, 4, 7):
        rect(img, x, 16, x + 1, 32, (84, 70, 46, 255))
    speckle(img, rng, 0, 16, 8, 32, (244, 168, 60, 255), 1, 9)
    speckle(img, rng, 0, 16, 8, 32, (168, 146, 108, 255), 1, 7)
    return img


# ── 鱿鱼（Squid）──
def draw_squid():
    img = canvas()
    rng = Rng(7303)
    # 躯干 mantle 区（实测 (12,0)-(36,20)）：深蓝头 + 斑驳 + 浅肚底带。
    rect(img, 12, 0, 36, 20, (34, 62, 108, 255))
    speckle(img, rng, 12, 0, 36, 20, (48, 88, 140, 255), 1, 6)
    speckle(img, rng, 12, 0, 36, 20, (24, 44, 84, 255), 1, 8)
    rect(img, 12, 16, 36, 20, (78, 112, 148, 255))       # 浅肚底带
    # 触腕条区（实测 (0,12)-(56,20) 行带；八条触腕各取 7px 段——mobmodel 触腕盒全脸采样本区）：
    #   深蓝 + 浅斑 + 末端吸盘点。
    for i in range(8):
        tx = i * 7
        rect(img, tx, 12, tx + 7, 20, (30, 56, 100, 255))
        speckle(img, rng, tx, 12, tx + 7, 20, (58, 96, 144, 255), 1, 5)
        rect(img, tx + 2, 18, tx + 4, 20, (86, 120, 156, 255))  # 末端吸盘浅点
    return img


# ── 矿车（Minecart）──
def draw_minecart():
    img = canvas()
    rng = Rng(7324)
    # 侧帮区（实测 (0,4)-(44,20)）：铁灰壁 + 铆钉 + 上沿亮棱。
    rect(img, 0, 4, 44, 20, (156, 158, 164, 255))
    speckle(img, rng, 0, 4, 44, 20, (132, 134, 140, 255), 1, 7)
    rect(img, 0, 4, 44, 6, (198, 200, 206, 255))          # 上沿亮棱（车口卷边）
    for x in range(2, 44, 5):                              # 铆钉列
        rect(img, x, 9, x + 1, 10, (212, 214, 220, 255))
        rect(img, x, 10, x + 1, 11, (112, 114, 120, 255))
    # 底板区（实测 (0,20)-(44,28)）：木底棕板条 + 板缝。
    rect(img, 0, 20, 44, 28, (128, 96, 58, 255))
    for x in range(0, 44, 6):
        rect(img, x, 20, x + 1, 28, (100, 74, 44, 255))    # 竖板缝
    speckle(img, rng, 0, 20, 44, 28, (148, 112, 68, 255), 1, 8)
    return img


# ── 附魔书（Enchant book；t732 附魔台悬浮书重贴图）──
def draw_enchant_book():
    img = canvas()
    rng = Rng(7325)
    # 左右两页各半区（书盒两页各取 [0,32) / [32,64)：左页封面 / 右页纸页——t679 悬浮书两页盒各铺半区）。
    # 左半：棕封 + 金边 + 封面纹章。
    rect(img, 0, 0, 32, 32, (108, 74, 42, 255))
    speckle(img, rng, 0, 0, 32, 32, (92, 62, 34, 255), 1, 6)
    rect(img, 0, 0, 2, 32, (206, 168, 76, 255))           # 金边（书脊侧）
    rect(img, 30, 0, 32, 32, (206, 168, 76, 255))          # 金边（外沿）
    rect(img, 10, 12, 22, 20, (196, 158, 70, 255))         # 封面纹章框
    rect(img, 12, 14, 20, 18, (80, 54, 30, 255))
    # 右半：白纸页 + 符文字线 + 页缘暗化。
    rect(img, 32, 0, 64, 32, (238, 232, 210, 255))
    for y in (5, 8, 11, 14, 17, 20, 23):
        off = (y // 3) % 3
        for dx in range(6 + off * 2):
            rect(img, 34 + dx, y, 35 + dx, y + 1, (128, 118, 138, 255))
    rect(img, 32, 0, 33, 32, (206, 196, 168, 255))         # 纸页书脊侧暗化
    return img


# ── 玩家皮肤（t731 第三人称贴图；t747 升 128×64 高清重绘）──
# 布局 = 64×32 MC 皮肤基准 ×2（盒区坐标全部 ×2，PlayerSkinBox 的 UV 是分数（分母钉 64×32）→ 同分数
# 自动适配，无需改 C++）。细节加密（非最近邻放大）：1px 发丝 / 眉眼（眼白+虹膜+瞳孔+高光）/ 布纹织理 /
# 缝线 / 鞋底。两侧脸区**按模板方向**画鬓角（贴脸区边界的前缘：右脸区 [0,16) 在大 u 端 x→16、左脸区
# [32,48) 在小 u 端 x=32——t747 UV 修正后此处即渲染为脸颊前方），且左右发长不对称（alex 明显、default
# 刘海微差）= 侧脸前后方向的常驻可视校验标记。
SKIN_W, SKIN_H = 128, 64


def _weave(img, x0, y0, x1, y1, base, lo, hi):
    """布纹织理（4px 周期棋盘点：暗/亮经纬线交织；只覆不透明像素）。"""
    px = img.load()
    for y in range(y0, y1):
        for x in range(x0, x1):
            if px[x, y][3] > 0:
                if (x + y) % 4 == 0:
                    px[x, y] = lo
                elif (x + y) % 4 == 2:
                    px[x, y] = hi


def _strands(img, xs, y0, y1, c_a, c_b):
    """1px 波浪发丝列（每 3 行左右各偏 1px，读作发流）。"""
    px = img.load()
    for i, x in enumerate(xs):
        c = c_a if i % 2 == 0 else c_b
        for y in range(y0, y1):
            xx = x + (1 if (y // 3) % 2 == 0 else 0)
            if 0 <= xx < img.width:
                px[xx, y] = c


def draw_skin(variant):
    """variant=default：蓝衣 + 棕发 + 肤色脸；variant=alex：橙发（不对称长发）+ 绿衣 + 棕裤。"""
    img = Image.new("RGBA", (SKIN_W, SKIN_H), (0, 0, 0, 0))
    rng = Rng(7311 if variant == "default" else 7312)
    if variant == "default":
        skin, skin_hi = (198, 156, 112, 255), (222, 178, 130, 255)
        hair, hair_hi, hair_lo = (58, 40, 26, 255), (78, 56, 34, 255), (42, 28, 18, 255)
        shirt, shirt_d = (56, 132, 178, 255), (40, 100, 142, 255)
        pants, pants_d, pants_hi = (52, 62, 128, 255), (38, 46, 96, 255), (66, 78, 150, 255)
        shoe, shoe_hi, sole = (60, 50, 42, 255), (82, 68, 54, 255), (40, 32, 26, 255)
        iris, pupil = (70, 96, 150, 255), (28, 30, 38, 255)
    else:  # alex：橙发不对称（右颊长发盖耳 + 额发偏右）+ 绿衣 + 棕裤 + 灰棕靴
        skin, skin_hi = (232, 188, 150, 255), (244, 206, 168, 255)
        hair, hair_hi, hair_lo = (216, 122, 52, 255), (236, 152, 84, 255), (184, 96, 38, 255)
        shirt, shirt_d = (96, 158, 88, 255), (72, 126, 66, 255)
        pants, pants_d, pants_hi = (128, 96, 72, 255), (100, 74, 54, 255), (144, 110, 82, 255)
        shoe, shoe_hi, sole = (86, 72, 60, 255), (106, 90, 74, 255), (56, 46, 38, 255)
        iris, pupil = (64, 124, 76, 255), (30, 32, 30, 255)
    skin_lo = shade(skin, 0.84)

    # ── 头 (0,0) 16×16×16：条带 y∈[16,32)——右脸区[0,16) 脸区[16,32) 左脸区[32,48) 后区[48,64) ──
    paint_box(img, 0, 0, 16, 16, 16, top=hair, bot=skin_lo, side=skin, front=skin, back=hair)
    # 顶区 [16,32)×[0,16)：前后向发丝 + 分缝线（default 缝在角色左侧 x≈27 / alex 右侧 x≈21——不对称标记）。
    _strands(img, [18, 22, 26, 30], 0, 16, hair_lo, hair_hi)
    rect(img, 27 if variant == "default" else 21, 2, 28 if variant == "default" else 22, 16, hair_lo)
    rect(img, 16, 15, 32, 16, hair_hi)                      # 前缘（贴脸区发际）亮一行
    # 脸区 [16,32)×[16,32)：发际 4 行 + 不对称额发（default：角色左侧(x 大端)多垂 2 行；alex：右侧垂到眉下）。
    rect(img, 16, 16, 32, 20, hair)
    if variant == "default":
        rect(img, 26, 20, 32, 22, hair)                     # 角色左侧额发微垂（不对称标记）
    else:
        rect(img, 16, 20, 26, 22, hair)                     # 角色右侧额发压到眉上（alex 标志性偏分）
    _strands(img, [17, 20, 23, 28, 31], 16, 20, hair_lo, hair_hi)
    # 眉（1 行 2 段；脸区小 u 端 = 角色右侧（t747 模板实测）→ 低 x 段是右眉）。
    rect(img, 17, 22, 23, 23, hair_lo)
    rect(img, 25, 22, 31, 23, hair_lo)
    # 眼（y23-25，各 4 宽：眼白 2 + 虹膜 2，瞳孔靠内侧 = 正视；高光 1px）。
    rect(img, 17, 23, 21, 26, (238, 238, 238, 255))         # 右眼眼白
    rect(img, 19, 23, 21, 25, iris)
    rect(img, 20, 23, 21, 25, pupil)
    rect(img, 17, 23, 18, 24, (252, 252, 252, 255))         # 右眼高光
    rect(img, 27, 23, 31, 26, (238, 238, 238, 255))         # 左眼眼白
    rect(img, 27, 23, 29, 25, iris)
    rect(img, 27, 23, 28, 25, pupil)
    rect(img, 30, 23, 31, 24, (252, 252, 252, 255))         # 左眼高光
    # 鼻（y26-27 中部 6 宽）+ 鼻底阴影。
    rect(img, 21, 26, 27, 28, shade(skin, 0.87))
    rect(img, 21, 27, 22, 28, shade(skin, 0.74))
    rect(img, 26, 27, 27, 28, shade(skin, 0.74))
    # 嘴（y29 唇线 + y30 下唇亮）+ 下巴亮行。
    rect(img, 20, 29, 28, 30, shade(skin, 0.70))
    rect(img, 21, 30, 27, 31, shade(skin, 0.88))
    rect(img, 17, 31, 31, 32, skin_hi)
    # 右脸区 [0,16)：发 + 鬓角贴**前缘（大 u 端 x→16，贴脸区边界）**——t747 UV 修正后此渲染在脸颊
    #   前方（耳朵方向正确性的常驻标记）。alex 右颊长发盖耳（垂到 y28）。
    cheek_hair_end = 24 if variant == "default" else 28
    rect(img, 0, 16, 16, cheek_hair_end, hair)
    rect(img, 0, cheek_hair_end, 16, 32, skin)
    rect(img, 14, 24, 16, 28, hair)                         # 鬓角主体（前缘 2 列）
    rect(img, 15, 28, 16, 30, hair_lo)                      # 鬓角尾梢
    _strands(img, [2, 6, 10], 16, cheek_hair_end, hair_lo, hair_hi)
    # 左脸区 [32,48)：镜像——鬓角贴前缘（小 u 端 x=32）；两变体左颊都短发。
    rect(img, 32, 16, 48, 24, hair)
    rect(img, 32, 24, 48, 32, skin)
    rect(img, 32, 24, 34, 28, hair)
    rect(img, 32, 28, 33, 30, hair_lo)
    _strands(img, [36, 40, 44], 16, 24, hair_lo, hair_hi)
    # 后区 [48,64)：满发 + 波浪发丝。
    _strands(img, [50, 54, 58, 62], 16, 32, hair_lo, hair_hi)
    # 底区（颈）[32,48)×[0,16)：肤色 + 颌底阴影 2 行。
    rect(img, 32, 14, 48, 16, shade(skin, 0.80))

    # ── body (32,32) 16×24×8：条带 y∈[40,64)——右侧[32,40) 前[40,56) 左侧[56,64) 后[64,80) ──
    paint_box(img, 32, 32, 16, 24, 8, top=shirt, bot=pants_d, side=shirt_d, front=shirt, back=shirt_d)
    _weave(img, 32, 40, 80, 54, shirt, shade(shirt, 0.88), shade(shirt, 1.08))   # 上衣布纹
    rect(img, 47, 40, 49, 42, skin)                         # 领口颈肤
    rect(img, 44, 42, 52, 43, shirt_d)                      # 领圈
    rect(img, 71, 40, 72, 54, shirt_d)                      # 背中缝
    if variant != "default":
        rect(img, 64, 40, 80, 52, hair)                     # alex 长发垂背（后区上衣段覆发）
        _strands(img, [66, 70, 74, 78], 40, 52, hair_lo, hair_hi)
        rect(img, 40, 52, 56, 53, shade(shirt, 1.10))       # 下摆亮行
    rect(img, 32, 54, 80, 56, pants_d)                      # 腰带
    rect(img, 47, 54, 49, 56, (168, 150, 90, 255))          # 皮带扣
    rect(img, 32, 56, 80, 64, pants)                        # 下摆裤腰
    speckle(img, rng, 32, 56, 80, 64, pants_d, 1, 8)
    rect(img, 47, 56, 48, 60, pants_d)                      # 门襟线

    # ── armR (80,32) 8×24×8：条带 [80,112)×[40,64)（袖 12 行 + 袖口 2 + 手 10）──
    paint_box(img, 80, 32, 8, 24, 8, top=shirt, bot=skin, side=shirt_d, front=shirt, back=shirt_d)
    _weave(img, 80, 41, 112, 52, shirt, shade(shirt_d, 1.05), shade(shirt, 1.08))
    rect(img, 80, 40, 112, 41, shade(shirt, 0.85))          # 肩缝
    rect(img, 80, 52, 112, 54, shade(shirt, 0.80))          # 袖口
    rect(img, 80, 54, 112, 55, shade(skin, 0.88))           # 袖口投影
    rect(img, 80, 55, 112, 64, skin)                        # 手
    speckle(img, rng, 80, 55, 112, 64, skin_hi, 1, 12)
    rect(img, 88, 57, 96, 59, shade(skin, 0.90))            # 指节（前区）
    for y in (34, 36, 38):                                  # 掌心底区 [96,104)×[32,40)：指缝线
        rect(img, 96, y, 104, y + 1, shade(skin, 0.90))

    # ── legR (0,32) 8×24×8：条带 [0,32)×[40,64)（裤 16 行 + 靴 8 行；大腿段=subV[0,0.5]=y[40,52)）──
    paint_box(img, 0, 32, 8, 24, 8, top=pants, bot=sole, side=pants_d, front=pants, back=pants_d)
    speckle(img, rng, 0, 40, 32, 56, pants_d, 1, 10)
    rect(img, 11, 40, 13, 56, shade(pants, 0.90))           # 前区挺缝线
    rect(img, 8, 46, 16, 48, pants_hi)                      # 膝盖亮部（前区）
    rect(img, 0, 56, 32, 58, shoe_hi)                       # 靴口
    rect(img, 0, 58, 32, 62, shoe)
    for x in range(9, 15, 2):                               # 靴带（前区）
        rect(img, x, 58, x + 1, 59, shoe_hi)
    rect(img, 0, 62, 32, 64, sole)                          # 鞋底
    return img


def save(img, name):
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_nightwalker(), "entity_nightwalker")
    save(draw_nightwalker_eyes(), "entity_nightwalker_eyes")
    save(draw_emberling(), "entity_emberling")
    save(draw_squid(), "entity_squid")
    save(draw_minecart(), "entity_minecart")
    save(draw_enchant_book(), "entity_enchant_book")
    save(draw_skin("default"), "entity_skin_default")
    save(draw_skin("alex"), "entity_skin_alex")


if __name__ == "__main__":
    main()
