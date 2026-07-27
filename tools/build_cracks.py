#!/usr/bin/env python3
"""生成 6 阶「裂纹叠层」贴图（16×16，透明底 + 黑裂纹），供 t34 挖掘系统使用。

每阶在前一阶基础上**累积**更多裂纹（嵌套递进，机制对齐 MC 1.0 的 destroy_stage_*.png）：
挖掘进度越深 → 裂纹越密 → 进度满则破。Main.qml 的裂纹叠层 Model 据玩家 miningStage
（0..5）切换 baseColorMap 到对应 PNG。

资产管线（PLAN §2-L）：纯程序生成（numpy + PIL 随机游走画线），**非 MC 资产**。
固定 seed → 同输入产同图，构建可复现（PLAN §2-K 精神）。
"""
import os
import random
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")

SIZE = 16           # 与既有贴图同分辨率（像素感一致）
N_STAGES = 6        # 0..5（对应进度 0% / 20% / 40% / 60% / 80% / 100%）
SEED = 1337         # 固定 → 可复现


def gen_crack(rng):
    """从某条边出发向内随机游走，返回一条裂纹的折线点列。"""
    edge = rng.randint(0, 3)
    if edge == 0:      # 顶边
        x, y = rng.randint(2, SIZE - 3), 0
        ddx, ddy = rng.choice([-1, 0, 1]), 1
    elif edge == 1:    # 右边
        x, y = SIZE - 1, rng.randint(2, SIZE - 3)
        ddx, ddy = -1, rng.choice([-1, 0, 1])
    elif edge == 2:    # 底边
        x, y = rng.randint(2, SIZE - 3), SIZE - 1
        ddx, ddy = rng.choice([-1, 0, 1]), -1
    else:              # 左边
        x, y = 0, rng.randint(2, SIZE - 3)
        ddx, ddy = 1, rng.choice([-1, 0, 1])

    pts = [(x, y)]
    for _ in range(rng.randint(4, 7)):
        x = max(0, min(SIZE - 1, x + ddx + rng.randint(-1, 1)))
        y = max(0, min(SIZE - 1, y + ddy + rng.randint(-1, 1)))
        pts.append((x, y))
    return pts


def main():
    rng = random.Random(SEED)
    accumulated = []   # 已积累的裂纹（每阶复用并追加，呈现「裂纹渐密」）

    for stage in range(N_STAGES):
        # 每阶新增 1~2 条；累积，使视觉上「裂纹越挖越密」。
        n_new = 1 if stage == 0 else 2
        for _ in range(n_new):
            accumulated.append(gen_crack(rng))

        img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        # 半透明黑裂纹（alpha=220）：既显眼又不会完全遮住方块本色。
        for pts in accumulated:
            draw.line(pts, fill=(0, 0, 0, 220), width=1)
        out = os.path.join(SRC, "crack_{}.png".format(stage))
        img.save(out)
        print("wrote", os.path.relpath(out, HERE), img.size)


if __name__ == "__main__":
    main()
