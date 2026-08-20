#!/usr/bin/env python3
"""生成猪 / 牛 / 羊 / 蹒跚者 / 鸡 / 鱿鱼 / 狼 / 豹猫 / 银鱼（passive + hostile mob）贴图（16×16 像素，原创程序自绘，§9 override (a)）。

机制等价 MC 1.0 三种被动生物（pig / cow / sheep）+ 鸡（chicken）+ 一种敌对生物（zombie）+ 一种水生被动
生物（squid）+ 一种犬科驯服生物（wolf）+ 一种猫科驯服生物（ocelot/cat）+ 一种小型虫类敌对生物（silverfish）
—— 名称 / 模型 / 贴图全原创、**不**拷贝任何 MC 资产（PLAN §9 区隔：Zombie→Shambler「蹒跚者」改名）。
本脚本程序生成十二种 mob 各一张「全脸」贴图（MobModel 几何的每面都铺同一张贴图，非 MC 式 UV 拆皮）
——简单稳健，配方块化模型比例让十二种 mob 肉眼可辨。

视觉意图（每张 16×16，无 alpha 透明底 —— 实心贴图走不透明 PrincipledMaterial）：
  - mob_pig.png      ：粉红皮 + 几个深粉斑点 + 浅腹纹（读作「粉红猪皮」）。
  - mob_cow.png      ：深棕底 + 不规则白斑 + 黑色「角痕」点缀（读作「牛皮斑纹」）。
  - mob_sheep.png    ：奶白羊毛 + 灰阴影卷曲纹（读作「羊毛卷」）。
  - mob_shambler.png ：暗绿腐肉底 + 深绿霉斑 + 棕色腐痕 + 青蓝/赭褐「破布」残片 + 深色缝合痕
                       （读作「不死亡灵腐尸」；机制等价 MC 1.0 僵尸皮肤，§9 改名 + 原创贴图）。
  - mob_chicken.png  ：白羽底 + 棕褐翅尖 / 尾羽斑 + 浅暖黄腹部（读作「白色母鸡羽毛」；t398）。
  - mob_squid.png    ：深褐橘斑软体底 + 浅腹纹 + 暗点（读作「鱿鱼软体皮」；t399）。
  - mob_wolf.png     ：灰狼毛皮底 + 深灰背脊 / 侧纹 + 浅灰腹纹（读作「灰狼皮毛」；t480）。
  - mob_ocelot.png   ：斑点橙棕底 + 深棕圆斑 + 浅奶黄腹纹（读作「丛林豹猫斑点皮」；t481 未驯服形态）。
  - mob_cat_black.png ：乌黑底 + 深灰高光纹 + 暗灰腹（读作「黑猫」；t481 驯服毛色变体 0）。
  - mob_cat_ginger.png：姜黄底 + 深橙横纹（虎斑）+ 浅奶黄腹（读作「姜黄虎斑猫」；t481 驯服毛色变体 1）。
  - mob_cat_cream.png ：奶油底 + 深褐面部/耳尖/尾尖深色点 + 浅白腹（读作「奶油暹罗猫」；t481 驯服毛色变体 2）。
  - mob_silverfish.png：灰白甲壳底 + 深灰体节横纹 + 暗头斑（读作「银灰多节小虫」；t487 要塞银鱼）。

图案采用固定位置 + 确定性散布（无随机源 → CI 可复现、与 build_atlas.py / build_tall_grass.py 同风格）。

输出（覆盖写入 textures/）：
  mob_pig.png / mob_cow.png / mob_sheep.png / mob_shambler.png / mob_chicken.png / mob_squid.png / mob_wolf.png /
  mob_ocelot.png / mob_cat_black.png / mob_cat_ginger.png / mob_cat_cream.png / mob_silverfish.png \
  mob_nightwalker.png / mob_nightwalker_eyes.png / mob_fireball.png

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


def make_wolf():
    """狼（Wolf；机制等价 MC 1.0 狼，§9 原创贴图非照搬）：
    灰狼毛皮底 + 深灰背脊 / 侧纹 + 浅灰腹纹（读作「灰狼皮毛」）。每面铺同图（同猪牛羊全脸 UV 方案）→
    躯干 / 头 / 耳 / 腿各盒都铺同一张灰狼纹。配方块化犬科比例 + 立耳 + 尾巴（QML 独立旋转 Model）→ 肉眼读作「狼」。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0x8a, 0x8a, 0x84, 255)   # 灰狼毛主色 #8a8a84（暖灰，非纯灰 / 非 MC 狼灰精确色）
    fill(img, base)

    # 深灰背脊 / 侧纹（狼背典型的深灰纵带 + 侧斑，散布固定坐标）
    dark = (0x5a, 0x58, 0x52, 255)   # 深灰 #5a5852
    blot(img, [
        # 背脊纵带（中列，拟狼背深灰脊线）
        (7, 1), (8, 1), (7, 2), (8, 2), (7, 3), (8, 3),
        (7, 4), (8, 4), (7, 5), (8, 5),
        # 侧斑（两肩 / 两胯的深灰斑，拟狼侧身纹）
        (3, 6), (4, 6), (3, 7),
        (12, 7), (13, 7), (12, 8),
        (5, 11), (6, 11), (10, 12), (11, 12),
    ], dark)

    # 浅灰腹纹（底部 2 行换浅色，拟狼腹 / 喉部浅毛）
    light = (0xc8, 0xc8, 0xc0, 255)  # 浅灰 #c8c8c0
    blot(img, [
        (x, TS - 1) for x in range(2, TS - 2)
    ], light)

    out = os.path.join(SRC, "mob_wolf.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_ocelot():
    """豹猫（Ocelot；机制等价 MC 1.0 豹猫，§9 原创贴图非照搬）：
    斑点橙棕底 + 深棕圆斑 + 浅奶黄腹纹（读作「丛林豹猫斑点皮」）。每面铺同图（同猪牛羊全脸 UV 方案）→
    躯干 / 头 / 耳 / 长尾 / 腿各盒都铺同一张斑点纹。配方块化猫科比例 + 尖耳 + 长尾 → 肉眼读作「豹猫」。
    未驯服形态（驯服变猫后由 QML 据 ocelotVariantAt 切 mob_cat_* 贴图）。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0xc8, 0x92, 0x4a, 255)   # 斑点橙棕主色 #c8924a（暖橙棕，非 MC 豹猫精确色）
    fill(img, base)

    # 深棕圆斑（豹猫标志性斑纹 —— 散布于皮面，固定坐标）
    spot = (0x5a, 0x3a, 0x1a, 255)   # 深棕 #5a3a1a
    blot(img, [
        (3, 3), (4, 3), (3, 4),
        (8, 2), (9, 2), (8, 3),
        (12, 4), (13, 4), (12, 5),
        (2, 8), (3, 8), (2, 9),
        (6, 7), (7, 7), (6, 8),
        (10, 9), (11, 9), (11, 10),
        (13, 12), (14, 12), (13, 13),
        (4, 12), (5, 12), (4, 13),
    ], spot)

    # 深棕细条斑（豹猫沿脊 / 侧身的纵向条点，提层次，少量免乱）
    strip = (0x7a, 0x52, 0x2a, 255)  # 中棕 #7a522a
    blot(img, [
        (7, 5), (8, 5),
        (5, 10), (6, 10),
        (10, 6), (10, 7),
        (1, 12), (14, 10),
    ], strip)

    # 浅奶黄腹纹（底部 2 行换浅色，拟豹猫腹 / 喉部浅毛）
    light = (0xf0, 0xe0, 0xc0, 255)  # 奶黄 #f0e0c0
    blot(img, [
        (x, TS - 1) for x in range(2, TS - 2)
    ], light)

    out = os.path.join(SRC, "mob_ocelot.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_cat_black():
    """黑猫（Cat 毛色变体 0；机制等价 MC 1.0 驯服猫变体，§9 原创贴图非照搬）：
    乌黑底 + 深灰高光纹 + 暗灰腹（读作「黑猫」）。每面铺同图（同全脸 UV 方案）→ 驯服豹猫转猫后据
    ocelotVariantAt==0 切本贴图。黑色底上深灰纹让纯黑方块模型有毛皮质感（非死黑平面）。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0x1a, 0x18, 0x18, 255)   # 乌黑主色 #1a1818（近黑，非 MC 猫精确色）
    fill(img, base)

    # 深灰高光纹（黑猫毛皮在光下的深灰光泽纹 —— 散布于皮面，固定坐标）
    sheen = (0x38, 0x36, 0x34, 255)  # 深灰 #383634
    blot(img, [
        (2, 3), (3, 3), (4, 3),
        (8, 2), (9, 2),
        (12, 4), (13, 4),
        (3, 8), (4, 8), (5, 8),
        (7, 7), (8, 7),
        (11, 9), (12, 9),
        (13, 12), (14, 12),
        (5, 13), (6, 13),
    ], sheen)

    # 暗灰腹纹（底部 2 行换略浅色，拟黑猫腹毛在暗处稍亮）
    belly = (0x2e, 0x2c, 0x2a, 255)  # 暗灰 #2e2c2a
    blot(img, [
        (x, TS - 1) for x in range(2, TS - 2)
    ], belly)

    out = os.path.join(SRC, "mob_cat_black.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_cat_ginger():
    """姜黄虎斑猫（Cat 毛色变体 1；机制等价 MC 1.0 驯服猫变体，§9 原创贴图非照搬）：
    姜黄底 + 深橙横纹（虎斑）+ 浅奶黄腹（读作「姜黄虎斑猫」）。每面铺同图（同全脸 UV 方案）→ 驯服豹猫转猫后
    据 ocelotVariantAt==1 切本贴图。横向条带拟经典虎斑猫纹（非 MC 精确纹样）。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0xe0, 0x8a, 0x3a, 255)   # 姜黄主色 #e08a3a（暖姜橙，非 MC 猫精确色）
    fill(img, base)

    # 深橙横纹（虎斑 —— 横向条带逐行错位，固定坐标；拟猫身横纹）
    tabby = (0xa8, 0x5a, 0x22, 255)  # 深橙 #a85a22
    blot(img, [
        # 横纹带（每行一条短横纹，逐行错位 → 读作虎斑）
        (2, 2), (3, 2), (4, 2),
        (10, 3), (11, 3), (12, 3),
        (4, 5), (5, 5), (6, 5),
        (11, 6), (12, 6), (13, 6),
        (2, 8), (3, 8), (4, 8),
        (9, 9), (10, 9), (11, 9),
        (5, 11), (6, 11), (7, 11),
        (12, 12), (13, 12),
        (3, 13), (4, 13),
    ], tabby)

    # 浅奶黄腹纹（底部 2 行换浅色，拟虎斑猫腹 / 喉部浅毛）
    light = (0xf0, 0xd0, 0xa0, 255)  # 奶黄 #f0d0a0
    blot(img, [
        (x, TS - 1) for x in range(2, TS - 2)
    ], light)

    out = os.path.join(SRC, "mob_cat_ginger.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_cat_cream():
    """奶油暹罗猫（Cat 毛色变体 2；机制等价 MC 1.0 驯服猫变体，§9 原创贴图非照搬）：
    奶油底 + 深褐面部 / 耳尖 / 尾尖深色点 + 浅白腹（读作「奶油暹罗猫」—— 浅色身体 + 深色远端点的花色）。
    每面铺同图（同全脸 UV 方案）→ 驯服豹猫转猫后据 ocelotVariantAt==2 切本贴图。深色点分布在贴图四角 /
    中央模拟面 / 尾端深点（全脸铺同图时四角 = 耳 / 尾尖位、底部 = 腹，读作暹罗花色）。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0xe8, 0xd8, 0xb8, 255)   # 奶油底 #e8d8b8（浅暖奶油，非 MC 猫精确色）
    fill(img, base)

    # 深褐点（面部 / 耳尖 / 尾尖 —— 暹罗花色标志性深色远端；贴图四角 + 中央上缘 + 底缘两点）
    point = (0x4a, 0x32, 0x20, 255)  # 深褐 #4a3220
    blot(img, [
        # 四角（耳 / 尾尖端）
        (1, 1), (2, 1), (1, 2),
        (14, 1), (13, 1), (14, 2),
        (1, 14), (2, 14), (1, 13),
        (14, 14), (13, 14), (14, 13),
        # 中央上缘（面部深点带）
        (6, 3), (7, 3), (8, 3), (9, 3),
        # 底缘两点（尾尖深点）
        (6, 12), (7, 12), (9, 12), (10, 12),
    ], point)

    # 浅褐柔晕（深点周围的过渡柔晕，免生硬）
    soft = (0xbe, 0xa0, 0x78, 255)   # 浅褐 #bea078
    blot(img, [
        (3, 2), (13, 2), (3, 13), (13, 13),
        (5, 4), (10, 4), (5, 11), (10, 11),
    ], soft)

    # 浅白腹纹（底部 2 行换近白色，拟暹罗猫腹 / 喉部浅毛）
    light = (0xf8, 0xf0, 0xe0, 255)  # 近白 #f8f0e0
    blot(img, [
        (x, TS - 1) for x in range(3, TS - 3)
    ], light)

    out = os.path.join(SRC, "mob_cat_cream.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_silverfish():
    """银鱼（Silverfish；机制等价 MC 1.0 银鱼——小型虫类敌对生物，§9 改名 + 原创贴图非照搬）：
    灰白甲壳底 + 深灰体节横纹 + 暗头斑（读作「小型银灰色多节虫」）。每面铺同图（同全脸 UV 方案）→
    MobModel 银鱼几何（小躯干 + 前伸小头 + 多对短腿）各盒都显同一张甲壳纹，配小体型 + 灰白色 → 肉眼
    读作「银灰色小虫」（机制等价 MC 银鱼，名称/美术全原创）。横向体节纹拟昆虫分节体表（非 MC 精确纹样）。
    """
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0xc8, 0xc2, 0xb8, 255)   # 灰白甲壳主色 #c8c2b8（银灰，非 MC 银鱼精确色）
    fill(img, base)

    # 深灰体节横纹（昆虫分节体表 —— 横向条带逐行错位，固定坐标）
    seg = (0x88, 0x82, 0x78, 255)    # 深灰 #888278
    blot(img, [
        # 体节横纹（每行一条短横纹，逐行错位 → 读作多节体段）
        (2, 3), (3, 3), (4, 3), (5, 3),
        (10, 4), (11, 4), (12, 4), (13, 4),
        (3, 6), (4, 6), (5, 6), (6, 6),
        (9, 7), (10, 7), (11, 7), (12, 7),
        (2, 9), (3, 9), (4, 9),
        (11, 10), (12, 10), (13, 10),
        (4, 12), (5, 12), (6, 12),
        (10, 13), (11, 13),
    ], seg)

    # 暗头斑（前部小深色块 —— 拟虫头部稍深，固定坐标位于贴图上缘中央）
    head = (0x5a, 0x54, 0x4a, 255)   # 暗灰 #5a544a
    blot(img, [
        (7, 1), (8, 1), (7, 2), (8, 2),
    ], head)

    # 亮高光点（甲壳反光 —— 少量亮像素提金属银灰质感）
    sheen = (0xe8, 0xe4, 0xdc, 255)  # 近白 #e8e4dc
    blot(img, [
        (6, 5), (11, 8), (3, 11), (12, 12),
    ], sheen)

    out = os.path.join(SRC, "mob_silverfish.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_nightwalker():
    """夜行者（Nightwalker；机制等价 MC 1.0 末影人 Enderman，§9 改名 + 原创贴图）：暗紫黑细长人形体色。
    暗紫黑底 #2a1f2a（与 EntityManager Nightwalker colorAt 同色）+ 深紫灰斑 + 近黑纵向「黑雾」条纹 + 几处
    暗荧光紫点（读作「暗黑瘦长黑影」—— 末影人观感，原创配色非照搬 MC 皮肤）。每面铺同图（同全脸 UV 方案）；
    配 MobModel 细长人形比例 + 独立发光眼层（Main.qml 补）→ 肉眼读作「神秘暗影夜行者」。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    base = (0x2a, 0x1f, 0x2a, 255)   # 暗紫黑主色 #2a1f2a（与 EntityManager colorAt 同色）
    fill(img, base)
    # 深紫灰斑（暗影皮肤上的暗淡色块，散布固定坐标）
    patch = (0x1e, 0x16, 0x22, 255)  # 深紫灰 #1e1622
    blot(img, [
        (2, 2), (3, 2), (2, 3),
        (7, 4), (8, 4), (8, 5),
        (12, 2), (13, 2), (13, 3),
        (4, 8), (5, 8), (4, 9),
        (10, 7), (11, 7), (11, 8),
        (3, 12), (4, 12), (5, 12),
        (11, 11), (12, 11), (12, 12),
        (14, 5), (1, 6),
    ], patch)
    # 近黑纵向「黑雾」条纹（末影人黑雾散逸观感；少量纵向线免乱）
    soot = (0x14, 0x0f, 0x18, 255)   # 近黑紫 #140f18
    blot(img, [
        (8, 1), (8, 2), (8, 3), (8, 4), (8, 6), (8, 7),
        (3, 5), (3, 6), (3, 7), (3, 9),
        (12, 8), (12, 9), (12, 10), (12, 13),
    ], soot)
    # 暗荧光紫点（残影微光点缀，暗示神秘的暗影生物）
    glow = (0x6a, 0x4a, 0x8a, 255)  # 暗紫荧光 #6a4a8a
    blot(img, [
        (6, 6), (9, 11), (14, 14), (1, 13),
    ], glow)
    out = os.path.join(SRC, "mob_nightwalker.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_nightwalker_eyes():
    """夜行者眼睛发光层（t727 独立眼贴图）：透明底 + 两枚亮紫白平行四边形眼（QML 顶层自发光小盒铺这张，
    读作「竖瞳紫白魅眼」—— 末影人眼睛观感，原创配色非照搬）。透明底 → 叠加在 body 上只显眼；无 alpha 裁
    切（Main.qml eye Model 用小盒 UV 全脸铺这张，仅眼睛区着紫白、其余透明不遮脸）。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))  # 全透明底
    eye = (0xe8, 0xdc, 0xff, 255)  # 亮紫白 #e8dcff（发光眼）
    # 两枚竖眼（末影人特征竖瞳）：左（x 4-6）、右（x 9-11），y 5-10 微斜
    blot(img, [
        (4, 5), (5, 5), (6, 5),
        (3, 6), (4, 6), (5, 6), (6, 6), (7, 6),
        (4, 7), (5, 7), (6, 7),
        (4, 8), (5, 8), (6, 8),
        (3, 9), (4, 9), (5, 9), (6, 9), (7, 9),
        (4, 10), (5, 10), (6, 10),
        # 右眼（镜像）
        (12, 5), (11, 5), (10, 5),
        (13, 6), (12, 6), (11, 6), (10, 6), (9, 6),
        (12, 7), (11, 7), (10, 7),
        (12, 8), (11, 8), (10, 8),
        (13, 9), (12, 9), (11, 9), (10, 9), (9, 9),
        (12, 10), (11, 10), (10, 10),
    ], eye)
    out = os.path.join(SRC, "mob_nightwalker_eyes.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def make_fireball():
    """燃烬者火球贴图（t728；机制等价 MC 1.0 烈焰人火球 blaze fireball）：16×16 橙黄自发光火球 ——
    外圈深橙焰 + 中圈亮橙黄焰 + 中心白热核（读作「跳动燃烧的火球」；与 Main.qml 火球 delegate 的三层
    配色外橙 #ff8a1a / 中黄 #ffd23c / 白核 #fff4c4 同源，原创程序自绘非照搬 MC 资产）。透明四角 → 圆润
    球感而非方形贴片。走全脸 UV（每面铺同图）；无随机源、确定性散布（同其它 make_* 风格）。"""
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))  # 透明底（圆润火球，四角透空）
    blaze = (0xff, 0x8a, 0x1a, 255)   # 外圈深橙 #ff8a1a
    glow = (0xff, 0xd2, 0x3c, 255)    # 中圈亮橙黄 #ffd23c
    core = (0xff, 0xf4, 0xc4, 255)    # 白热核 #fff4c4
    # 中心白热核（3×2 亮心）
    blot(img, [
        (7, 8), (8, 8), (6, 8), (9, 8),
        (7, 7), (8, 7), (7, 9), (8, 9),
    ], core)
    # 中圈橙黄焰（围绕白核一圈，略外扩）
    blot(img, [
        (7, 6), (8, 6), (5, 7), (10, 7), (5, 8), (10, 8), (5, 9), (10, 9),
        (7, 10), (8, 10), (6, 6), (9, 6), (6, 10), (9, 10),
    ], glow)
    # 外圈深橙焰（再外扩一层环形边缘，读作「火球外焰」）
    blot(img, [
        (4, 7), (11, 7), (4, 8), (11, 8), (4, 9), (11, 9),
        (5, 5), (6, 5), (9, 5), (10, 5),
        (5, 11), (6, 11), (9, 11), (10, 11),
        (7, 5), (8, 5), (7, 11), (8, 11),
    ], blaze)
    out = os.path.join(SRC, "mob_fireball.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    make_pig()
    make_cow()
    make_sheep()
    make_shambler()
    make_chicken()
    make_squid()
    make_wolf()
    make_ocelot()
    make_cat_black()
    make_cat_ginger()
    make_cat_cream()
    make_silverfish()
    make_nightwalker()
    make_nightwalker_eyes()
    make_fireball()


if __name__ == "__main__":
    main()
