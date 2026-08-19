#!/usr/bin/env python3
"""生成 t717 盔甲 layer 贴图（64×32 MC 盔甲 UV 布局，原创自绘，§9 override (a)）。

R19.10 t718/t719 盔甲 3D 显示的贴图前置：玩家模型 / MobModel 叠加护甲薄壳盒体时，按 MC 盔甲
layer UV 布局采样本贴图（机制等价 MC 1.0 armor layer_1 / layer_2 两层贴图——layer_1 = 头盔+胸甲+
护腿（人形外层），layer_2 = 靴（内层））。**布局参考** docs/Default HD 128x Demo 1.8.2.2/ 的
models/armor/<tier>_layer_*.png 构图意图（盒区像素占用实测后按 MC 标准 box-UV 布局自绘），像素
全部为本项目程序生成的**原创**绘制（PLAN §9 红线：不拷贝任何包内 PNG）。

UV 布局（base 64×32，与 mobmodel.cpp 人形 setMobTex texOffs 同族——盔甲盒与身体盒同 UV 规则）：
  layer_1（头盔 + 胸甲 + 护腿）：
    helmet (0,0)8×8×8   —— 顶面 (8,0)-(16,8)；侧带 (0,8)-(32,16)（前面开脸窗 alpha=0）
    body   (16,16)8×12×4 —— 侧带 (16,20)-(40,32)；顶/底 (20,16)/(28,16)
    armR   (40,16)4×12×4 —— 侧带 (40,20)-(56,32)（armL 镜像共用同区）
    legR   (0,16)4×12×4  —— 侧带 (0,20)-(16,32)（legL 镜像共用同区）
  layer_2（靴）：
    bootR  (0,16)4×12×4  —— 侧带 (0,20)-(16,32)
    bootL  (16,16)4×12×4 —— 侧带 (16,20)-(32,32)
  盒区六面明暗：顶亮 / 前中 / 侧中暗 / 背最暗 / 底暗（方向光读感，原创）。

六档配色（与 playerModel.armorBaseColor / MaterialIcon 护甲配色同族色板）：
  leather（皮革棕）/ iron（浅灰）/ copper（铜橙，t718 补——本工程自创档，MC 1.0 无铜甲 → 无 pack
  等价，armorLayerSource 恒 miss 恒走本程序层；与 ToolIcon 铜工具头 #c87850 同族色板）/ gold（金黄）/
  diamond（青）/ chainmail（深灰点孔——链环纹 + 规则透明孔，读作「链甲网眼」）。

输出（覆盖写入 textures/，全部透明底 + 盒区不透明；**不进图集**——实体层贴图走独立 Texture）：
  armor_leather_layer_1.png / armor_leather_layer_2.png
  armor_iron_layer_1.png    / armor_iron_layer_2.png
  armor_copper_layer_1.png  / armor_copper_layer_2.png  # t718 铜档
  armor_gold_layer_1.png    / armor_gold_layer_2.png
  armor_diamond_layer_1.png / armor_diamond_layer_2.png
  armor_chainmail_layer_1.png / armor_chainmail_layer_2.png

依赖：仅 PIL。pack 运行期映射 armorLayerSource(kind, layer) → models/armor/<kind>_layer_<n>.png
（resourcepackmanager；leather 染棕、miss 回退本程序贴图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
W, H = 64, 32  # base 分辨率（MC 盔甲 UV）


def new_canvas():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def rect(img, x0, y0, x1, y1, color):
    """闭开区间填色 [x0,x1) × [y0,y1)（越界自动钳制）。"""
    px = img.load()
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            px[x, y] = color


def erase(img, x0, y0, x1, y1):
    """抠透明（开脸窗 / 链甲孔）。"""
    rect(img, x0, y0, x1, y1, (0, 0, 0, 0))


def shade(c, f):
    """颜色明暗缩放（f>1 提亮 / f<1 压暗），钳 0..255。"""
    return (min(255, int(c[0] * f)), min(255, int(c[1] * f)), min(255, int(c[2] * f)), c[3])


def paint_box(img, u0, v0, w, h, d, pal):
    """按 MC box-UV 六面布局画一个盒（方向明暗：顶亮/前中/侧中暗/背最暗/底暗）。"""
    top, bot = pal["top"], pal["bot"]
    rgt, lft, bck = pal["side"], pal["side"], pal["back"]
    frt = pal["front"]
    # 顶 / 底（v0 起 d 高）。
    rect(img, u0 + d, v0, u0 + d + w, v0 + d, top)
    rect(img, u0 + d + w, v0, u0 + 2 * d + w, v0 + d, bot)
    # 侧带（v0+d 起 h 高）：右 (d 宽) | 前 (w 宽) | 左 (d 宽) | 背 (w 宽)。
    rect(img, u0, v0 + d, u0 + d, v0 + d + h, rgt)
    rect(img, u0 + d, v0 + d, u0 + d + w, v0 + d + h, frt)
    rect(img, u0 + d + w, v0 + d, u0 + 2 * d + w, v0 + d + h, lft)
    rect(img, u0 + 2 * d + w, v0 + d, u0 + 2 * d + 2 * w, v0 + d + h, bck)


# 五档色板（base / 顶亮 / 底·背暗 / 铆钉或缝线点缀）。
TIERS = {
    # 皮革棕：与 playerModel.armorBaseColor 皮革 #8a5a2b 同族。
    "leather": {
        "front": (138, 90, 43, 255), "side": (120, 78, 36, 255),
        "back": (94, 61, 28, 255), "top": (168, 115, 64, 255), "bot": (94, 61, 28, 255),
        "stitch": (196, 140, 82, 255),
    },
    # 铁浅灰：与铁工具头 #d8d8d8 同族。
    "iron": {
        "front": (216, 216, 216, 255), "side": (196, 196, 198, 255),
        "back": (154, 154, 158, 255), "top": (240, 240, 242, 255), "bot": (154, 154, 158, 255),
        "stitch": (250, 250, 250, 255),
    },
    # 铜橙（t718 补，本工程自创档）：与 ToolIcon 铜工具头 #c87850 / MaterialIcon 铜锭同族色板
    #   （暗 #8a4818 / 中 #c87850 / 亮 #e8a088，与 retintCopperTemplate 锚点同源）。
    "copper": {
        "front": (200, 120, 80, 255), "side": (180, 104, 68, 255),
        "back": (138, 72, 24, 255), "top": (232, 160, 136, 255), "bot": (138, 72, 24, 255),
        "stitch": (240, 180, 152, 255),
    },
    # 金金黄：与金轨 GOLD_MID #e8b830 同族。
    "gold": {
        "front": (232, 184, 48, 255), "side": (210, 164, 38, 255),
        "back": (168, 128, 24, 255), "top": (255, 232, 96, 255), "bot": (168, 128, 24, 255),
        "stitch": (255, 246, 160, 255),
    },
    # 钻石青：与护甲钻石 #4ee0c8 同族。
    "diamond": {
        "front": (78, 224, 200, 255), "side": (64, 198, 178, 255),
        "back": (44, 156, 140, 255), "top": (128, 240, 224, 255), "bot": (44, 156, 140, 255),
        "stitch": (200, 252, 244, 255),
    },
    # 链甲深灰：小色差明暗（链环纹靠 stitch 点阵表达 + 透明孔）。
    "chainmail": {
        "front": (74, 74, 82, 255), "side": (66, 66, 74, 255),
        "back": (52, 52, 58, 255), "top": (92, 92, 100, 255), "bot": (52, 52, 58, 255),
        "stitch": (108, 108, 118, 255),
    },
}


def decorate(img, pal, kind):
    """档位专属点缀：金属铆钉 / 皮革缝线 / 链甲网眼孔。"""
    px = img.load()

    def is_opaque(x, y):
        return 0 <= x < W and 0 <= y < H and px[x, y][3] > 0

    if kind == "chainmail":
        # 链甲网眼：规则透明孔（每 4px 网格 1 孔，交错），读作「环间空隙」。
        for y in range(H):
            for x in range(W):
                if px[x, y][3] > 0 and x % 4 == 2 and (y + (x // 4) % 2 * 2) % 4 == 1:
                    px[x, y] = (0, 0, 0, 0)
        # 链环高光点（亮 stitch 错落）。
        for y in range(H):
            for x in range(W):
                if px[x, y][3] > 0 and x % 4 == 0 and y % 4 == 3:
                    px[x, y] = pal["stitch"]
    else:
        # 金属 / 皮革：沿每个已画盒的「侧带上沿 + 下沿」点缀一行铆钉/缝线（视觉收边）。
        #   覆盖 body/arm/leg 侧带（rows 20 与 31 附近）与头盔侧带（row 8/15 附近）。
        for (y0, y1, xs) in ((8, 8, range(0, 32)), (15, 15, range(0, 32)),
                             (20, 20, range(16, 56)), (31, 31, range(0, 56))):
            for x in xs:
                if is_opaque(x, y0) and (x % 3 == 1):
                    px[x, y0] = pal["stitch"]
                if is_opaque(x, y1) and (x % 3 == 2):
                    px[x, y1] = shade(pal["front"], 0.7)


def draw_layer_1(kind):
    """layer_1：头盔 + 胸甲 + 护腿（人形外层）。"""
    pal = TIERS[kind]
    img = new_canvas()
    # 头盔 (0,0)8×8×8 —— 前面开脸窗（露脸：MC 盔甲头盔不遮面）。
    paint_box(img, 0, 0, 8, 8, 8, pal)
    erase(img, 10, 11, 14, 16)   # 脸窗（前面前下区）
    # 胸甲 body (16,16)8×12×4 + 双臂 armR (40,16)4×12×4（armL 镜像同区）。
    paint_box(img, 16, 16, 8, 12, 4, pal)
    paint_box(img, 40, 16, 4, 12, 4, pal)
    # 肩垫凸块（arm 顶面上沿 2px 加亮——护肩读感）。
    rect(img, 44, 16, 52, 18, pal["top"])
    # 护腿 legR (0,16)4×12×4（legL 镜像同区）。
    paint_box(img, 0, 16, 4, 12, 4, pal)
    decorate(img, pal, kind)
    return img


def draw_layer_2(kind):
    """layer_2：靴（左右各一盒；bootL 在 (16,16) 独立区——非镜像，MC layer_2 实测布局）。"""
    pal = TIERS[kind]
    img = new_canvas()
    paint_box(img, 0, 16, 4, 12, 4, pal)    # 右靴
    paint_box(img, 16, 16, 4, 12, 4, pal)   # 左靴
    # 靿口加亮沿（靴筒上缘 1px）。
    rect(img, 4, 20, 12, 21, pal["top"])
    rect(img, 20, 20, 28, 21, pal["top"])
    decorate(img, pal, kind)
    return img


def save(img, name):
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    for kind in ("leather", "iron", "copper", "gold", "diamond", "chainmail"):
        save(draw_layer_1(kind), "armor_%s_layer_1" % kind)
        save(draw_layer_2(kind), "armor_%s_layer_2" % kind)


if __name__ == "__main__":
    main()
