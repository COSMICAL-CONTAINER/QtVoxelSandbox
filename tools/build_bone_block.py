#!/usr/bin/env python3
"""生成骨块方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t1077 骨块（BoneBlock；机制等价 MC 骨粉 9↔1 压缩存储/装饰方块——9 骨粉 3×3 满铺 ↔ 1 块）。
名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 / 专名）：骨白底面 + 横向骨节环带 + 纵向骨纹细条 +
散点骨孔，读作「压实的骨段柱面」。

视觉意图：读作「骨料压缩块」——
  - 底面：米骨白（同 bone_meal / bone 材料族的米白色系，明度略压暗保立体）。
  - 骨节：每 4px 一道 1px 横向浅灰环带（骨段关节缝），带内偶发断点（非通直机械线）。
  - 骨纹：稀疏 1px 纵向细条（淡灰黄），模拟骨质纵向纤维。
  - 骨孔：3-5 粒 1px 暗灰小孔散布（固定坐标，非随机 → 重跑可复现）。
  - 与羊毛（卷绒纹）/ 石砖（砖纹）/ 石质存储块（镶格暗缝）族均不同纹——肉眼可辨。
  - alpha 恒不透明（255）：整立方六面同贴图，无透明语义。

输出（覆盖写入 textures/）：
  default_bone_block.png  （tile 185，骨块各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_note_block.py 系同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 色板：米骨白主面（基 / 受光 / 暗角）/ 骨节环带（浅灰）/ 骨纹（淡灰黄）/ 骨孔（暗灰）。
BONE = np.array([222.0, 216.0, 198.0])    # 骨白主色（米白偏暖，同骨粉材料族色系）
BONE_HI = np.array([234.0, 229.0, 212.0])  # 受光带（上缘提亮）
BONE_DK = np.array([202.0, 195.0, 176.0])  # 暗角 / 带缘（同色系压暗）
JOINT = np.array([176.0, 168.0, 148.0])    # 骨节环带（浅灰关节缝）
STRIATION = np.array([210.0, 202.0, 178.0])  # 纵向骨纹（淡灰黄纤维线）
PORE = np.array([160.0, 152.0, 134.0])     # 骨孔（暗灰小点）

# 固定布置（非随机 → 重跑逐位可复现，PLAN §2-K 同纪律外溢到资产工具）：
#   骨节环带 y 行（每 4px 一道）+ 各带断点 x（带内留 2-3 个原色断点，非通直机械线）。
JOINT_ROWS = [3, 7, 11, 15]
JOINT_BREAKS = {3: [5, 12], 7: [2, 9], 11: [6, 13], 15: [1, 8]}
# 纵向骨纹 x 列（稀疏 4 列，只画环带之间的骨段段内，不穿节缝）。
STRIATION_COLS = [2, 6, 10, 14]
# 骨孔坐标（固定 4 粒，散布于不同骨段内）。
PORES = [(4, 1), (12, 5), (8, 9), (3, 13)]


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0] = rgb[0]
        canvas[y, x, 1] = rgb[1]
        canvas[y, x, 2] = rgb[2]
        canvas[y, x, 3] = 255


def main():
    img = np.zeros((TS, TS, 4), dtype=np.float64)
    # ① 底面：整面骨白 + 顶部两行受光带 / 底部一行暗角（轻立体，同存储块族明暗语言）。
    for y in range(TS):
        for x in range(TS):
            base = BONE
            if y <= 1:
                base = BONE_HI
            elif y == TS - 1:
                base = BONE_DK
            px(img, x, y, base)
    # ② 骨节环带：每 4px 一道 1px 浅灰横带，带内固定坐标留原色断点（非通直机械线）。
    for row in JOINT_ROWS:
        for x in range(TS):
            if x in JOINT_BREAKS[row]:
                continue
            px(img, x, row, JOINT)
    # ③ 纵向骨纹：稀疏 4 列淡灰黄细条，只画骨段段内（跳过节缝行）。
    for col in STRIATION_COLS:
        for y in range(TS):
            if y in JOINT_ROWS:
                continue
            if (y + col) % 5 == 0:  # 段内断续（纤维非通长）
                continue
            px(img, col, y, STRIATION)
    # ④ 骨孔：固定 4 粒 1px 暗灰点。
    for (x, y) in PORES:
        px(img, x, y, PORE)

    out = Image.fromarray(img.astype(np.uint8), "RGBA")
    out_path = os.path.join(SRC, "default_bone_block.png")
    out.save(out_path)
    print("wrote", os.path.relpath(out_path, HERE), out.size)


if __name__ == "__main__":
    main()
