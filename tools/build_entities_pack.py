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
  - entity_skin_default.png（64×32 MC 皮肤布局）：头(0,0)8×8×8 / body(16,16)8×12×4 /
    armR(40,16)4×12×4 / legR(0,16)4×12×4。蓝裤 + 棕发 + 肤色脸（程序版玩家默认皮肤，
    t731 playerModel 第三人称贴图回退）。
  - entity_skin_alex.png（64×32 同布局变体）：橙发 + 绿衣（细臂语义在本工程 UnitCube 拼装
    模型上无区别，仅贴图配色）。

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
    px = img.load()
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            px[x, y] = c


def speckle(img, rng, x0, y0, x1, y1, c, density_num, density_den):
    """区域内确定性撒点（密度 n/d）。"""
    px = img.load()
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
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


# ── 玩家皮肤（t731 第三人称贴图）──
def draw_skin(variant):
    """variant=default：蓝裤 + 棕发 + 肤色脸；variant=alex：橙发 + 绿衣。"""
    img = canvas()
    rng = Rng(7311 if variant == "default" else 7312)
    if variant == "default":
        skin, skin_hi = (198, 156, 112, 255), (222, 178, 130, 255)
        hair = (58, 40, 26, 255)
        shirt, shirt_d = (56, 132, 178, 255), (40, 100, 142, 255)
        pants, pants_d = (52, 62, 128, 255), (38, 46, 96, 255)
        shoe = (60, 50, 42, 255)
    else:  # alex
        skin, skin_hi = (232, 188, 150, 255), (244, 206, 168, 255)
        hair = (216, 122, 52, 255)
        shirt, shirt_d = (96, 158, 88, 255), (72, 126, 66, 255)
        pants, pants_d = (128, 96, 72, 255), (100, 74, 54, 255)
        shoe = (92, 70, 54, 255)
    # 头 (0,0)8×8×8：顶=发 / 前=脸（发际 + 肤 + 眼）/ 侧=发+肤 / 背=发 / 底=肤。
    paint_box(img, 0, 0, 8, 8, 8, top=hair, bot=skin, side=skin, front=skin, back=hair)
    # 脸细节（前面 (8..16, 8..16)）：发际 2 行 + 双眼（白 + 瞳）+ 嘴影。
    rect(img, 8, 8, 16, 10, hair)
    rect(img, 9, 11, 11, 13, (255, 255, 255, 255))
    rect(img, 13, 11, 15, 13, (255, 255, 255, 255))
    rect(img, 10, 11, 11, 13, (56, 44, 38, 255))
    rect(img, 14, 11, 15, 13, (56, 44, 38, 255))
    rect(img, 11, 14, 13, 15, shade(skin, 0.82))
    rect(img, 8, 15, 16, 16, skin_hi)
    # 侧面发区（头顶到上段）。
    rect(img, 0, 8, 8, 12, hair)
    rect(img, 16, 8, 24, 12, hair)
    rect(img, 24, 8, 32, 16, hair)
    # body (16,16)8×12×4：上衣 + 下摆裤腰。
    paint_box(img, 16, 16, 8, 12, 4, top=shirt, bot=pants_d, side=shirt_d, front=shirt, back=shirt_d)
    rect(img, 16, 27, 40, 32, pants)                       # 侧带下段裤腰
    speckle(img, rng, 16, 16, 40, 28, shirt_d, 1, 8)
    # armR (40,16)4×12×4：袖（上衣色上半）+ 肤（手下半）。
    paint_box(img, 40, 16, 4, 12, 4, top=shirt, bot=skin, side=shirt_d, front=shirt, back=shirt_d)
    rect(img, 40, 23, 56, 32, skin)                        # 侧带下段手
    rect(img, 40, 22, 56, 23, shade(shirt, 0.9))           # 袖口
    # legR (0,16)4×12×4：裤 + 鞋底 2 行。
    paint_box(img, 0, 16, 4, 12, 4, top=pants, bot=shoe, side=pants_d, front=pants, back=pants_d)
    rect(img, 0, 30, 16, 32, shoe)
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
