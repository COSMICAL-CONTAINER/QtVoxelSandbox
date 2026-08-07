#!/usr/bin/env python3
"""生成猪 / 牛 / 羊 / 蹒跚者 / 鸡 / 鱿鱼（passive + hostile mob）贴图（16×16 像素，原创程序自绘，§9 override (a)）。

机制等价 MC 1.0 三种被动生物（pig / cow / sheep）+ 鸡（chicken）+ 一种敌对生物（zombie）+ 一种水生被动
生物（squid）—— 名称 / 模型 / 贴图全原创、**不**拷贝任何 MC 资产（PLAN §9 区隔：Zombie→Shambler「蹒跚者」
改名）。本脚本程序生成六种 mob 各一张「全脸」贴图（MobModel 几何的每面都铺同一张贴图，非 MC 式 UV 拆皮）——
简单稳健，配方块化模型比例让六种 mob 肉眼可辨。

视觉意图（每张 16×16，无 alpha 透明底 —— 实心贴图走不透明 PrincipledMaterial）：
  - mob_pig.png      ：粉红皮 + 几个深粉斑点 + 浅腹纹（读作「粉红猪皮」）。
  - mob_cow.png      ：深棕底 + 不规则白斑 + 黑色「角痕」点缀（读作「牛皮斑纹」）。
  - mob_sheep.png    ：奶白羊毛 + 灰阴影卷曲纹（读作「羊毛卷」）。
  - mob_shambler.png ：暗绿腐肉底 + 深绿霉斑 + 棕色腐痕 + 青蓝/赭褐「破布」残片 + 深色缝合痕
                       （读作「不死亡灵腐尸」；机制等价 MC 1.0 僵尸皮肤，§9 改名 + 原创贴图）。
  - mob_chicken.png  ：白羽底 + 棕褐翅尖 / 尾羽斑 + 浅暖黄腹部（读作「白色母鸡羽毛」；t398）。
  - mob_squid.png    ：深褐橘斑软体底 + 浅腹纹 + 暗点（读作「鱿鱼软体皮」；t399）。

图案采用固定位置 + 确定性散布（无随机源 → CI 可复现、与 build_atlas.py / build_tall_grass.py 同风格）。

输出（覆盖写入 textures/）：
  mob_pig.png   /   mob_cow.png   /   mob_sheep.png   /   mob_shambler.png   /   mob_chicken.png   /   mob_squid.png

依赖：仅 PIL，无外部贴图。与 build_farmland.py / build_tall_grass.py / build_chest.py 同风格（程序
生成原创像素图，§9 override (a)）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def fill(img, rgb):
    """整张填一色。"""
    px = img.load()
    for y in range(TS):
        for x in range(TS):
            px[x, y] = rgb


def blot(img, cells, rgb):
    """把指定坐标列表的像素改色（确定性的「斑点」分布，无随机源）。"""
    px = img.load()
    for (x, y) in cells:
        if 0 <= x < TS and 0 <= y < TS:
            px[x, y] = rgb


def make_pig():
    """猪：粉红皮 + 深粉斑点 + 浅腹纹。机制等价 MC 猪皮（非名词照搬）。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0xf0, 0xa8, 0xb0, 255)   # 粉红皮主色 #f0a8b0
    fill(img, base)

    # 深粉斑点（散布于皮面，固定坐标）
    dark = (0xd0, 0x80, 0x88, 255)   # 深粉 #d08088
    blot(img, [
        (3, 4), (4, 4), (3, 5),
        (9, 3), (10, 3), (10, 4),
        (6, 9), (7, 9), (6, 10),
        (12, 10), (13, 10), (12, 11),
        (2, 12), (3, 12),
    ], dark)

    # 浅腹纹（底部 2 行换浅色，拟腹部更亮 —— 仅底排，因每面铺同图，太多条纹会显乱）
    light = (0xf8, 0xc4, 0xc8, 255)  # 浅粉 #f8c4c8
    blot(img, [
        (x, TS - 1) for x in range(2, TS - 2)
    ], light)

    out = os.path.join(SRC, "mob_pig.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_cow():
    """牛：深棕底 + 不规则白斑 + 黑角痕点缀。机制等价 MC 牛皮斑纹（非名词照搬）。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0x5a, 0x40, 0x30, 255)   # 深棕 #5a4030
    fill(img, base)

    # 白色不规则斑块（皮纹典型特征）—— 两簇：左上一团 + 右中一团
    white = (0xf0, 0xe8, 0xd8, 255)  # 米白 #f0e8d8
    blot(img, [
        # 左上团
        (2, 2), (3, 2), (4, 2),
        (2, 3), (3, 3),
        (3, 4),
        # 右中团
        (10, 7), (11, 7), (12, 7),
        (10, 8), (11, 8),
        (11, 9),
        # 右下角小斑
        (13, 12), (13, 13),
    ], white)

    # 黑角痕点缀（深棕上的更深小点，拟皮皱 / 角痕质感）
    black = (0x2a, 0x1c, 0x14, 255)  # 深棕近黑 #2a1c14
    blot(img, [
        (6, 5), (9, 4), (5, 11), (8, 12), (12, 2), (1, 9),
    ], black)

    out = os.path.join(SRC, "mob_cow.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_sheep():
    """羊：奶白羊毛 + 灰阴影卷曲纹（拟羊毛卷）。机制等价 MC 羊毛（非名词照搬）。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0xf5, 0xf0, 0xe8, 255)   # 奶白羊毛 #f5f0e8
    fill(img, base)

    # 灰阴影卷曲点（拟羊毛卷凹凸 —— 散布小灰点，固定坐标）
    shade = (0xd0, 0xc8, 0xc0, 255)  # 浅灰 #d0c8c0
    blot(img, [
        (2, 3), (4, 2), (6, 4), (8, 3), (10, 2), (12, 4), (14, 3),
        (3, 6), (5, 7), (7, 6), (9, 7), (11, 6), (13, 7),
        (2, 9), (4, 10), (6, 9), (8, 10), (10, 9), (12, 10), (14, 9),
        (3, 12), (5, 13), (7, 12), (9, 13), (11, 12), (13, 13),
    ], shade)

    # 深一点的灰纹（少量，提层次）
    deep = (0xb0, 0xa8, 0xa0, 255)   # 中灰 #b0a8a0
    blot(img, [
        (5, 5), (10, 6), (3, 11), (12, 11),
    ], deep)

    out = os.path.join(SRC, "mob_sheep.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_shambler():
    """蹒跩者（Shambler；机制等价 MC 1.0 僵尸，§9 改名 + 原创贴图非照搬）：
    暗绿腐肉底 + 深绿霉斑 + 棕色腐痕 + 青蓝/赭褐「破布」残片 + 深色缝合痕（读作「不死亡灵腐尸」）。
    每面铺同图（同猪牛羊全脸 UV 方案）→ 躯干/头/双臂/双腿各盒都显同一张腐皮纹（含破布残片），
    配人形方块比例 + 前伸双臂姿态 + 红眼 → 肉眼读作「僵尸类不死亡灵」（机制等价，名称/美术全原创）。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0x4a, 0x6a, 0x3a, 255)   # 暗绿腐肉主色 #4a6a3a（与 EntityManager Shambler colorAt 同色）
    fill(img, base)

    # 深绿霉斑（腐肉上的霉变暗区，散布固定坐标）
    mold = (0x2f, 0x4a, 0x24, 255)   # 深绿霉 #2f4a24
    blot(img, [
        (2, 2), (3, 2), (2, 3),
        (7, 1), (8, 1), (8, 2),
        (13, 3), (14, 3), (13, 4),
        (1, 8), (2, 8), (1, 9),
        (6, 7), (7, 7), (6, 8),
        (11, 9), (12, 9), (12, 10),
        (14, 13), (13, 13), (14, 12),
        (4, 13), (5, 13), (4, 14),
    ], mold)

    # 棕色腐痕（干涸血/泥污斑块，拟腐尸溃烂感）
    rot = (0x5a, 0x40, 0x2a, 255)    # 赭褐腐痕 #5a402a
    blot(img, [
        (5, 3), (6, 3), (5, 4),
        (10, 5), (11, 5), (10, 6),
        (3, 11), (4, 11), (3, 12),
        (9, 12), (10, 12), (9, 13),
        (13, 6), (2, 6),
    ], rot)

    # 青蓝破布残片（上衣碎布，机制等价「残破衣物」感，原创配色非 MC 皮肤）
    rag_blue = (0x2e, 0x4a, 0x5a, 255)  # 褪色青蓝 #2e4a5a
    blot(img, [
        (0, 4), (0, 5), (1, 5),
        (15, 7), (15, 8), (14, 8),
        (7, 10), (8, 10), (7, 11),
    ], rag_blue)

    # 赭褐破布残片（下装/裹布碎块，与青蓝碎布区分层次）
    rag_brown = (0x3a, 0x2c, 0x1a, 255)  # 深褐 #3a2c1a
    blot(img, [
        (11, 2), (12, 2),
        (4, 9), (5, 9),
        (13, 11), (13, 12),
        (2, 14), (3, 14),
    ], rag_brown)

    # 深色缝合痕（几道短缝线，拟粗暴缝合的腐皮 —— 不亡灵特征点缀，少量免乱）
    stitch = (0x18, 0x24, 0x14, 255)  # 近黑深绿 #182414
    blot(img, [
        # 一道横向短缝（左中区）
        (4, 6), (5, 6), (6, 6),
        # 一道纵向短缝（右中区）
        (10, 10), (10, 11), (10, 12),
    ], stitch)

    out = os.path.join(SRC, "mob_shambler.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_chicken():
    """鸡（Chicken；机制等价 MC 1.0 鸡，§9 原创贴图非照搬）：
    白色羽毛底 + 棕红色的翅尖 / 尾羽斑（读作「白色母鸡羽毛」）。每面铺同图（同猪牛羊全脸 UV 方案）→
    躯干 / 头 / 尾 / 腿各盒都铺同一张羽毛纹。配方块化小型鸟比例 + 喙 / 鸡冠纯色子 Model → 肉眼读作「鸡」。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0xf5, 0xf0, 0xe4, 255)   # 白羽主色 #f5f0e4（略暖奶白，区别于羊 #f5f0e8 的冷白）
    fill(img, base)

    # 棕红色羽斑（翅膀 / 尾羽深色区，散布于身体侧面，固定坐标）
    brown = (0x8a, 0x5a, 0x32, 255)  # 棕褐 #8a5a32
    blot(img, [
        (2, 5), (3, 5), (2, 6),
        (6, 4), (7, 4), (7, 5),
        (11, 6), (12, 6), (12, 7),
        (13, 4), (14, 4),
        (4, 10), (5, 10), (4, 11),
        (9, 11), (10, 11), (10, 12),
        (13, 12), (14, 12),
    ], brown)

    # 浅暖灰阴影点（羽毛层叠凹凸感，少量免乱）
    shade = (0xd8, 0xd0, 0xc2, 255)  # 浅暖灰 #d8d0c2
    blot(img, [
        (5, 2), (8, 3), (11, 2),
        (3, 8), (6, 8), (9, 9), (12, 9),
        (2, 13), (7, 13), (11, 14),
    ], shade)

    # 底部浅黄（腹部到腿部过渡略偏暖黄，拟鸡腹部绒毛色）
    belly = (0xf0, 0xe0, 0xb8, 255)  # 浅暖黄 #f0e0b8
    blot(img, [
        (x, TS - 1) for x in range(3, TS - 3)
    ], belly)

    out = os.path.join(SRC, "mob_chicken.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_squid():
    """鱿鱼（Squid；机制等价 MC 1.0 squid，§9 原创贴图非照搬）：
    深褐橘斑软体底 + 浅腹纹 + 暗点（读作「鱿鱼软体皮」）。每面铺同图（同猪牛羊全脸 UV 方案）→
    躯干 / 顶端尖 / 触腕各盒都铺同一张软体纹。配方块化水生软体比例 + 黑眼纯色子 Model → 肉眼读作「鱿鱼」。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0x6a, 0x4a, 0x3a, 255)   # 深褐主色 #6a4a3a（与 EntityManager squid 占位色同色，机制等价 squid 软体色）
    fill(img, base)

    # 橘褐斑（软体上的浅色斑驳，散布于身体，固定坐标 —— 拟鱿鱼皮肤斑点 / 色素细胞）
    tan = (0xa8, 0x78, 0x4a, 255)    # 橘褐 #a8784a
    blot(img, [
        (3, 3), (4, 3), (3, 4),
        (8, 2), (9, 2), (9, 3),
        (12, 4), (13, 4), (12, 5),
        (2, 8), (3, 8), (2, 9),
        (6, 7), (7, 7), (6, 8),
        (10, 9), (11, 9), (11, 10),
        (13, 12), (14, 12), (13, 13),
        (4, 12), (5, 12), (4, 13),
    ], tan)

    # 暗点（深色小点，拟皮肤皱褶 / 色素细胞暗斑，提层次）
    dark = (0x3a, 0x28, 0x1a, 255)   # 深褐近黑 #3a281a
    blot(img, [
        (6, 3), (10, 5), (5, 10), (8, 11), (12, 2), (2, 11), (14, 9), (7, 13),
    ], dark)

    # 浅腹纹（底部 2 行换浅色，拟腹部更亮 —— 软体腹面常偏浅）
    light = (0x8a, 0x6a, 0x4a, 255)  # 浅褐 #8a6a4a
    blot(img, [
        (x, TS - 1) for x in range(2, TS - 2)
    ], light)

    out = os.path.join(SRC, "mob_squid.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    make_pig()
    make_cow()
    make_sheep()
    make_shambler()
    make_chicken()
    make_squid()


if __name__ == "__main__":
    main()
