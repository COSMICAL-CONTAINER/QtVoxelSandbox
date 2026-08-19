#!/usr/bin/env python3
"""生成 t717 画作贴图 27 张（16/32/64 像素系列，原创自绘抽象像素画，§9 override (a)）。

R19.10 t720/t721 画作系统的贴图前置：Painting 方块（薄板贴墙 partial）按 index 选一张画作。
构图 / 尺寸参考 docs/Default HD 128x Demo 1.8.2.2/ 的 painting/ 目录**文件名与画幅尺寸**（27 张
实测：16×16×8、32×16×5、16×32×2、32×32×6、64×48×2、64×64×4），但**画面内容全部程序原创**
自绘（抽象风景 / 几何像素图案；PLAN §9 红线：不拷贝 pack 画面内容）。文件名沿用 pack 名作内部
key（仅运行期映射索引，非 UI 专名）。

画风语言（统一原创像素画风格）：
  - 有限调色板（天 / 地 / 点睛三段色）+ 色块拼接（无渐变抗锯齿，硬边像素）。
  - 主题分五族随机分配（确定性 seed）：风景（地平线+日月）/ 抽象几何（方块拼贴）/ 像素生灵
    （剪影小生物）/ 静物（瓶罐杯）/ 深空（星点+漩涡）。每张再叠 2px 内框（画框读感）。

输出（覆盖写入 textures/；**不进图集**——t720 画作方块走独立 Texture per-painting，
  pack 运行期映射 paintingSource(index) → painting/<name>.png，miss 回退本程序贴图）：
  default_painting_<name>.png × 27
"""

import os
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")

# 27 张画作：pack painting/ 目录 27 文件逐名镜像（尺寸按盘上 PIL 实测写死；PIL 逐像素绘制）。
#   name 沿用 pack 文件名作内部 key（仅 t720 运行期映射索引，非 UI 专名）。
#   实测尺寸分布：16×16×8 / 32×16×5 / 16×32×2 / 32×32×6 / 64×48×2 / 64×64×3 / 64×32×1。
PAINTINGS = [
    # 16×16 × 8
    ("kebab", 16, 16), ("aztec", 16, 16), ("aztec2", 16, 16),
    ("bomb", 16, 16), ("plant", 16, 16), ("wasteland", 16, 16),
    ("back", 16, 16), ("alban", 16, 16),
    # 32×16 × 5
    ("courbet", 32, 16), ("sea", 32, 16), ("creebet", 32, 16),
    ("sunset", 32, 16), ("pool", 32, 16),
    # 16×32 × 2
    ("graham", 16, 32), ("wanderer", 16, 32),
    # 32×32 × 6
    ("match", 32, 32), ("skull_and_roses", 32, 32), ("stage", 32, 32),
    ("void", 32, 32), ("bust", 32, 32), ("wither", 32, 32),
    # 64×48 × 2
    ("donkey_kong", 64, 48), ("skeleton", 64, 48),
    # 64×64 × 3 + 64×32 × 1（fighters 实测 64×32）
    ("burning_skull", 64, 64), ("pigscene", 64, 64),
    ("pointer", 64, 64), ("fighters", 64, 32),
]

# 主题族调色板（统一画风语言；硬边色块）。
FAMILY = {
    "landscape": {  # 地平线 + 日月
        "sky": (108, 148, 190), "sky2": (146, 186, 218),
        "sun": (244, 214, 120), "ground": (104, 126, 74), "ground2": (76, 96, 54),
    },
    "geo": {        # 抽象几何拼贴
        "bg": (40, 42, 56), "a": (206, 92, 76), "b": (216, 178, 84),
        "c": (86, 150, 122), "d": (146, 106, 168),
    },
    "fauna": {      # 像素生灵剪影
        "bg": (222, 216, 198), "body": (72, 60, 52), "acc": (196, 96, 72),
        "line": (86, 74, 62),
    },
    "still": {      # 静物瓶罐
        "bg": (198, 184, 162), "jar": (118, 148, 112), "jar2": (156, 118, 84),
        "line": (86, 74, 62),
    },
    "space": {      # 深空星点漩涡
        "bg": (22, 24, 44), "neb": (94, 66, 128), "star": (238, 238, 220),
        "core": (216, 186, 96),
    },
}
FRAME = (60, 46, 34)        # 画框内沿深棕
FRAME_LIGHT = (110, 86, 58)  # 画框高光沿


class PxCtx:
    """确定性伪随机（每画独立 seed → 同名重跑同图；无随机源漂移）。"""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self, lo, hi):
        # xorshift32（确定性、无依赖）。
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= (x >> 17)
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return lo + (x % (hi - lo + 1))


def draw(name, w, h, idx):
    """按主题族画一张（family 由 index 确定性轮转；内容坐标由 PxCtx 驱动）。"""
    family_name = ["landscape", "geo", "fauna", "still", "space"][idx % 5]
    pal = FAMILY[family_name]
    rng = PxCtx(idx * 2654435761 + sum(ord(c) for c in name))
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()

    # 内框（2px 画框沿 + 1px 高光）：框内才是画面。
    for y in range(h):
        for x in range(w):
            if x < 2 or x >= w - 2 or y < 2 or y >= h - 2:
                px[x, y] = FRAME + (255,)
            elif x == 2 or y == 2:
                px[x, y] = FRAME_LIGHT + (255,)
    x0, y0, x1, y1 = 3, 3, w - 3, h - 3  # 画面区（闭开）

    if family_name == "landscape":
        hzn = rng.next(y0 + (y1 - y0) // 3, y0 + 2 * (y1 - y0) // 3)
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[x, y] = (pal["sky"] if y < hzn - 1 else
                            pal["sky2"] if y == hzn - 1 else
                            pal["ground"] if y < hzn + (y1 - hzn) // 2 else
                            pal["ground2"]) + (255,)
        # 日 / 月（圆盘 + 光晕点）。
        cx, cy, r = rng.next(x0 + 3, x1 - 4), rng.next(y0 + 2, max(y0 + 3, hzn - 3)), 2
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= r * r and x0 <= cx + dx < x1 and y0 <= cy + dy < y1:
                    px[cx + dx, cy + dy] = pal["sun"] + (255,)
        # 地面竖物（树 / 塔 剪影 ×3）。
        for i in range(3):
            tx = rng.next(x0, x1 - 2)
            th = rng.next(2, max(3, (y1 - hzn) - 1))
            for t in range(th):
                if hzn + t < y1:
                    px[tx, hzn + t] = pal["ground2"] + (255,)
                    if tx + 1 < x1:
                        px[tx + 1, hzn + t] = pal["ground2"] + (255,)
    elif family_name == "geo":
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[x, y] = pal["bg"] + (255,)
        # 色块拼贴（随机矩形 ×6，颜色轮转 a/b/c/d）。
        keys = ["a", "b", "c", "d"]
        for i in range(6):
            bw, bh = rng.next(2, max(3, (x1 - x0) // 3)), rng.next(2, max(3, (y1 - y0) // 3))
            bx, by = rng.next(x0, max(x0, x1 - bw)), rng.next(y0, max(y0, y1 - bh))
            c = pal[keys[i % 4]] + (255,)
            for y in range(by, min(by + bh, y1)):
                for x in range(bx, min(bx + bw, x1)):
                    px[x, y] = c
    elif family_name == "fauna":
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[x, y] = pal["bg"] + (255,)
        # 小生灵剪影：圆身 + 立耳 + 尾（像素块堆叠，位置随机）。
        bx, by = rng.next(x0 + 1, max(x0 + 1, x1 - 5)), rng.next(y0 + 1, max(y0 + 1, y1 - 5))
        bw = min(4, x1 - bx)
        bh = min(3, y1 - by - 1)
        for y in range(by, by + bh):
            for x in range(bx, bx + bw):
                px[x, y] = pal["body"] + (255,)
        if bx - 1 >= x0:
            px[bx - 1, by] = pal["body"] + (255,)      # 尾
        if bx + 1 < x1:
            px[bx + 1, by - 1] = pal["acc"] + (255,)   # 耳 / 冠点缀
        # 地线（脚下 1px）。
        for x in range(x0, x1):
            if by + bh < y1:
                px[x, by + bh] = pal["line"] + (255,)
    elif family_name == "still":
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[x, y] = pal["bg"] + (255,)
        # 两只瓶罐（高低错落：细颈圆腹 / 方罐）。
        jx = rng.next(x0 + 1, max(x0 + 1, (x0 + x1) // 2 - 2))
        for y in range(y0 + 3, y1 - 1):
            for x in range(jx, min(jx + 3, x1)):
                px[x, y] = pal["jar"] + (255,)
        kx = rng.next((x0 + x1) // 2, max((x0 + x1) // 2, x1 - 4))
        for y in range(y0 + 5, y1 - 1):
            for x in range(kx, min(kx + 4, x1)):
                px[x, y] = pal["jar2"] + (255,)
        # 台面线。
        for x in range(x0, x1):
            px[x, y1 - 1] = pal["line"] + (255,)
    else:  # space
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[x, y] = pal["bg"] + (255,)
        # 星漩：中心亮核 + 螺旋星点 + 星云弧块。
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        px[cx, cy] = pal["core"] + (255,)
        step = 0
        for t in range((x1 - x0) * (y1 - y0) // 6 + 8):
            step += 1
            ang = step * 0.7
            rad = 1.0 + step * 0.55
            sx, sy = int(cx + rad * __import__("math").cos(ang)), int(cy + rad * 0.7 * __import__("math").sin(ang))
            if x0 <= sx < x1 and y0 <= sy < y1:
                px[sx, sy] = (pal["neb"] if step % 3 else pal["star"]) + (255,)
    return img


def main():
    for i, (name, w, h) in enumerate(PAINTINGS):
        img = draw(name, w, h, i)
        out = os.path.join(SRC, "default_painting_%s.png" % name)
        img.save(out)
    print("wrote %d paintings -> %s" % (len(PAINTINGS), os.path.relpath(SRC, HERE)))


if __name__ == "__main__":
    main()
