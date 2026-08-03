#!/usr/bin/env python3
"""生成草丛（TallGrass）cross 贴图（16×16 像素，原创自绘，§9 override (a)）。

机制等价 MC 1.0 草丛 / 蕨类（tall grass / fern）—— cross 模型（两片对角相交平面）上贴本瓦片。
贴图为本项目程序生成的原创像素图，**不**拷贝任何 MC 资产。

视觉意图：读作「一丛直立草叶」—— 透明底（alpha=0）+ 几条竖向绿色草叶（alpha=255）。mesher 把本瓦片
  贴到 cross 的两片对角双面 quad；chunk 地形材质 alphaCutoff:0.5 丢弃透明底 → 仅草叶像素显（机制等价
  MC cutout 草叶）。透明底是关键：若无 alpha（实心底），cross 会显成两片实心绿板（非草丛）。

t245 alpha 边缘修复（黑边根因 + 修法）：草叶像素（绿, alpha=255）与透明底像素（RGB=0,0,0, alpha=0）
  相邻时，图集线性过滤（min/mag Linear）在两者间插值 → 边缘采样得（暗化绿, alpha≈128）；材质
  alphaCutoff:0.5 保留 alpha≥0.5 的像素并当不透明渲染 → 这些「暗化绿」边缘显成黑/暗边晕（用户实测
  「草丛两交叉平面有黑边」）。修法 = alpha bleed：把每个透明像素的 RGB 用最近不透明像素颜色填上
  （alpha 保持 0）→ 边缘采样在 alpha 渐变时 RGB 保持草绿色 → cutout 边缘干净（机制等价 MC cutout
  贴图生成时的 alpha bleed / "padding" 处理）。alpha=0 不被丢弃契约不变（仅 RGB 改色，alpha 仍 0）。

图案（固定位置 + 确定性散布，无随机源 → 便于 CI 校验 & 与 build_atlas.py 顺序对齐）：
  - 透明底（alpha=0）；
  - 5-7 条竖向草叶（画布 y 自下而上生长），宽 1 像素、高低参差、横向分布在画布中段（留边距）；
  - 草叶配色两档（亮面 / 暗面）拟受光明暗；顶端略浅（嫩叶）、底端略深（老叶 / 阴影）。

输出（覆盖写入 textures/）：
  default_tall_grass.png   （tile 28，草丛 cross 贴图）

依赖：仅 PIL，无外部贴图。与 build_farmland.py / build_water.py 同风格（程序生成原创像素图）。
"""
import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）


def bleed_alpha(img):
    """把透明像素的 RGB 用最近不透明邻居颜色填上（alpha 保持 0），并级联填满整个透明区。

    t245 alpha 边缘修复：见文件头注释。迭代膨胀（8 邻域平均）—— 每轮把「有已着色邻居（不透明 或
    上一轮已 bleed）的透明像素」填为邻居平均色。级联使整个透明区都被就近的不透明色覆盖（非仅 1 px
    边圈），对线性过滤 / 未来启用 mipmap / 远距宽滤波都鲁棒。alpha=0 像素只改 RGB、alpha 仍 0 →
    alphaCutoff 仍丢弃它们（不破坏 cutout 语义），但过滤在 alpha 渐变区不再产生黑边晕。
    """
    img = img.convert("RGBA")
    W, H = img.size
    px = img.load()
    # 用「RGB!=(0,0,0)」判定该像素是否已着色（本族贴图不透明像素恒为绿/金色，无纯黑 opaque 像素；
    # 透明像素初值 (0,0,0,0)）。由此级联：已 bleed 的像素作下一轮的源。
    for _ in range(max(W, H) * 2):
        changed = False
        snap = [px[x, y] for y in range(H) for x in range(W)]  # 上一轮快照，防同轮连锁污染
        for y in range(H):
            for x in range(W):
                r0, g0, b0, a0 = snap[y * W + x]
                if a0 != 0 or (r0, g0, b0) != (0, 0, 0):
                    continue  # 已不透明 或 已 bleed，跳过
                ar = ag = ab = 0
                n = 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if dx == 0 and dy == 0:
                            continue
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < W and 0 <= ny < H:
                            r, g, b, a = snap[ny * W + nx]
                            if a != 0 or (r, g, b) != (0, 0, 0):
                                ar += r; ag += g; ab += b; n += 1
                if n > 0:
                    px[x, y] = (ar // n, ag // n, ab // n, 0)
                    changed = True
        if not changed:
            break
    return img


def main():
    # 透明底（alpha=0）。RGBA，草叶像素后填 alpha=255。
    img = Image.new("RGBA", (TS, TS), (0, 0, 0, 0))
    px = img.load()

    def blade(x, top_y, light_rgb, dark_rgb):
        """竖向草叶：列 x 自画布底（y=TS-1）向上长到 top_y（含），上端浅、下端深。
        top_y 越小 → 草叶越高（画布 y 向下，顶在上）。"""
        for y in range(top_y, TS):
            # 顶端 1-2 像素用亮色（嫩叶），其余渐暗到暗色（老叶）。
            t = (y - top_y) / max(1, TS - 1 - top_y)  # 0（顶）..1（底）
            r = int(light_rgb[0] * (1 - t) + dark_rgb[0] * t)
            g = int(light_rgb[1] * (1 - t) + dark_rgb[1] * t)
            b = int(light_rgb[2] * (1 - t) + dark_rgb[2] * t)
            px[x, y] = (r, g, b, 255)

    # 草叶两档配色（草绿）：亮面 #6fae3a（嫩绿受光）/ 暗面 #3a6a1a（深绿阴影）。
    light = (0x6f, 0xae, 0x3a)
    dark = (0x3a, 0x6a, 0x1a)
    # 草叶列 + 顶端 y（参差高度）。列分布于画布中段 [3,12]（留左右边距），顶端 y 参差 [3,8]。
    #   固定位置（无随机源 → 可复现 / CI 可校验）。
    blades = [
        (4, 5),   # 高草叶
        (6, 3),   # 最高草叶（中央）
        (8, 4),
        (10, 6),
        (11, 8),  # 矮草叶
        (7, 7),
    ]
    for (x, top_y) in blades:
        blade(x, top_y, light, dark)

    # t245 alpha bleed：把草绿色渗进透明底（alpha=0 不变），消除 cutout 黑边（见文件头注释）。
    img = bleed_alpha(img)

    out = os.path.join(SRC, "default_tall_grass.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
