#!/usr/bin/env python3
"""生成青金石「空缺风格」轮廓图标 icon_lapis_outline.png（t957，48×48 RGBA，原创程序生成 §9a）。

t957 附魔台青金石槽空位占位图标返修（用户 8-28 口径「青金石轮廓图标不像」）：旧占位是 QML
Canvas 手绘八边形描边 + 「青金」小字（EnchantingTableUI.qml EnchantInputSlot），辨识度差。
本脚本两段式离线生成（构建期一次生成零运行时成本）：

  ① 物化源图 icon_lapis_item.png —— 青金石物品像素艺术此前只以 QML Canvas 代码存在
     （src/ui/MaterialIcon.qml 的 drawLapis，24×24 设计网格）；本段按**同一像素规格**（逐条
     R(col,row,w,h) 调用 + 同源十六进制色板，SCALE 倍放大）物化为 PNG，作为边缘提取的输入。
     drawLapis 与本表 RECTS 互为镜像：改一处必须同步另一处（注释即契约）。
  ② 边缘提取 —— 读 icon_lapis_item.png 像素 → 预乘亮度图（L_eff = 亮度 × alpha/255，透明底
     恒 0）→ Sobel 3×3 梯度幅值（同时抓轮廓环与内部切面棱线：相邻色块亮度差 Δ 在边界两侧
     各产生约 3Δ 的梯度幅值）→ 幅值 > EDGE_GRADIENT_THRESHOLD 判为边缘像素。
  ③ 空缺风格输出 icon_lapis_outline.png —— 边缘像素 = 近黑不透明（「边缘变黑」）；非边缘
     实心像素 = 原色 × INTERIOR_DARKEN + INTERIOR_ALPHA 半透（内部镂空感，附魔台槽位幽灵
     图标的经典样式）；背景恒全透明。

消费点：src/ui/EnchantingTableUI.qml EnchantInputSlot（showLapisOutline 空槽占位 Image，
qrc:/textures/icon_lapis_outline.png）。仅处理本工程自带程序像素图（§9a 自绘），无任何
MC 资产。依赖：仅 PIL。运行：python tools/build_lapis_outline.py（重跑恒确定性同图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TEX_DIR = os.path.join(HERE, "..", "textures")
SRC_PNG = os.path.join(TEX_DIR, "icon_lapis_item.png")      # ① 源图（物化的青金石物品像素图）
OUT_PNG = os.path.join(TEX_DIR, "icon_lapis_outline.png")   # ③ 空缺风格轮廓图标

# ── ① 源像素规格（与 src/ui/MaterialIcon.qml drawLapis 逐条镜像：R(col, row, w, h, color)）──
SCALE = 2                       # 24 设计网格 × 2 → 48×48 输出（30×30 槽内平滑缩小显示）
# 色板与 drawLapis 同源：face 群青主体 / light 顶面高光菱 / dark 底影+切面暗棱 / spark 高光闪点 /
# edge 外轮廓暗边 / pyrite 黄铁矿金点（青金石矿物特征，边缘提取后成小金点轮廓 → 身份保留）。
PALETTE = {
    "face":   (0x22, 0x3a, 0xa0),
    "light":  (0x60, 0x82, 0xdc),
    "dark":   (0x14, 0x2a, 0x6a),
    "spark":  (0xa0, 0xc0, 0xff),
    "edge":   (0x0c, 0x1a, 0x4a),
    "pyrite": (0xda, 0xb2, 0x38),
}
# 与 drawLapis 完全同序同值的矩形表（改任一处须同步另一处）。
RECTS = [
    (8, 6, 8, 1, "face"),
    (6, 7, 12, 1, "face"),
    (5, 8, 14, 8, "face"),
    (6, 16, 12, 1, "face"),
    (8, 17, 8, 1, "face"),
    (8, 6, 8, 1, "light"),                       # 顶面高光菱
    (7, 7, 2, 1, "light"), (15, 7, 2, 1, "light"),
    (11, 8, 2, 1, "light"),
    (6, 16, 12, 1, "dark"),                      # 底阴影
    (8, 17, 8, 1, "dark"),
    (13, 14, 3, 1, "dark"),
    (10, 9, 1, 2, "dark"), (14, 9, 1, 2, "dark"),    # 切面棱线
    (8, 11, 2, 1, "dark"), (15, 11, 2, 1, "dark"),
    (9, 13, 1, 1, "dark"), (14, 13, 1, 1, "dark"),
    (9, 9, 1, 1, "spark"),                       # 高光闪点
    (7, 6, 1, 1, "edge"), (16, 6, 1, 1, "edge"),     # 外轮廓暗边（角顶收口）
    (5, 8, 1, 2, "edge"), (18, 8, 1, 2, "edge"),
    (7, 12, 1, 1, "pyrite"),                     # 黄铁矿金点 ×3（青金石身份）
    (13, 10, 1, 1, "pyrite"),
    (11, 14, 1, 1, "pyrite"),
]

# ── ②/③ 边缘提取与空缺风格参数（可调常量，注释即契约）──
EDGE_GRADIENT_THRESHOLD = 36.0  # Sobel 幅值阈值：最小有义色差 Δ≈12（dark/edge 色差 16 → 幅值 ~48），
                                #   平坦内部 Δ=0 恒不过线；调低 → 轮廓变密（噪点棱线）、调高 → 只剩外环。
EDGE_COLOR = (12, 12, 20, 255)  # 边缘 = 近黑不透明（「边缘变黑」；略带蓝黑避免死黑生硬）
INTERIOR_DARKEN = 0.45          # 非边缘实心像素的原色压暗系数（空缺感的「残色」浓度）
INTERIOR_ALPHA = 80             # 非边缘实心像素的统一半透 alpha/255 ≈ 0.31（内部镂空感）


def build_source() -> Image.Image:
    """① 物化 drawLapis 同规格像素图为 SRC_PNG（写盘 → 供②按「读 png 像素」流程消费）。"""
    size = 24 * SCALE
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = im.load()
    for (c, r, w, h, name) in RECTS:
        rgb = PALETTE[name]
        for y in range(r * SCALE, (r + h) * SCALE):
            for x in range(c * SCALE, (c + w) * SCALE):
                px[x, y] = (rgb[0], rgb[1], rgb[2], 255)
    im.save(SRC_PNG)
    return im


def extract_outline(src: Image.Image) -> Image.Image:
    """②③ 边缘提取（预乘亮度 Sobel）→ 空缺风格输出。"""
    w, h = src.size
    spx = src.load()
    # 预乘亮度图：L_eff = 亮度(RGB) × alpha/255。透明底恒 0 → 物体轮廓环与内部色块棱线
    #   在同一张梯度图上同权呈现（alpha 突变即亮度突变，无需第二套轮廓判定）。
    lum = [[0.0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = spx[x, y]
            lum[y][x] = (0.299 * r + 0.587 * g + 0.114 * b) * (a / 255.0)
    # Sobel 3×3（域外按 0 = 透明延拓；源图四周留透明边距，实际无截断影响）。
    KX = ((-1, 0, 1), (-2, 0, 2), (-1, 0, 1))
    KY = ((-1, -2, -1), (0, 0, 0), (1, 2, 1))

    def grad(x, y, k):
        s = 0.0
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                yy, xx = y + dy, x + dx
                v = lum[yy][xx] if (0 <= yy < h and 0 <= xx < w) else 0.0
                s += v * k[dy + 1][dx + 1]
        return s

    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    opx = out.load()
    for y in range(h):
        for x in range(w):
            _, _, _, a = spx[x, y]
            if a == 0:
                continue                      # 背景恒全透明
            gx, gy = grad(x, y, KX), grad(x, y, KY)
            mag = (gx * gx + gy * gy) ** 0.5
            if mag > EDGE_GRADIENT_THRESHOLD:
                opx[x, y] = EDGE_COLOR        # 边缘 → 近黑不透明
            else:
                r, g, b, _ = spx[x, y]        # 内部 → 原色压暗 + 半透（空缺感残色）
                opx[x, y] = (int(r * INTERIOR_DARKEN), int(g * INTERIOR_DARKEN),
                             int(b * INTERIOR_DARKEN), INTERIOR_ALPHA)
    return out


def ascii_preview(im: Image.Image, title: str) -> None:
    """控制台 ASCII 预览（人工目检用：. 透明 / - 淡内部 / # 黑边）。"""
    print("==", title, im.size)
    w, h = im.size
    for y in range(h):
        row = ""
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            if a == 0:
                row += "."
            elif a < 200:
                row += "-"                    # 半透内部残色
            else:
                row += "#"                    # 近黑边缘
        print(row)


def main() -> None:
    src = build_source()
    print("wrote", os.path.normpath(SRC_PNG))
    outline = extract_outline(src)
    outline.save(OUT_PNG)
    print("wrote", os.path.normpath(OUT_PNG))
    ascii_preview(outline, OUT_PNG)


if __name__ == "__main__":
    main()
