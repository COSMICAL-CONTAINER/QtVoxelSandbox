#!/usr/bin/env python3
"""生成手动 TNT 点火机关方块的贴图（16×16 像素，原创自绘，§9 override (a)）。

t490 TNT 连锁爆炸 + 引燃机制（机制等价 MC 1.0 lever / wooden button / stone button——无红石系统，
故用「右键激活 → 点燃水平四邻 TNT」简化为单次脉冲触发）。名称 / 贴图纯原创自绘（§9 区隔，零 MC 资产 /
专名）：木质 / 石质底座 + 中央凸起圆钮 / 竖直扳柄，读作「手动点火机关」。

视觉意图：读作「可扳动 / 按下的点火机关」——
  - 杠杆（lever）：木质底座 + 中央竖直扳柄 + 顶部圆柄头（杠杆特征）。
  - 木按钮（button_wood）：木质底座 + 中央凸起小圆钮（按钮特征）。
  - 石按钮（button_stone）：石质底座 + 中央凸起小圆钮（按钮特征，石质纹理区别木）。

输出（覆盖写入 textures/）：
  default_lever.png        （tile 131，杠杆各面同贴图）
  default_wood_button.png  （tile 132，木按钮各面同贴图）
  default_stone_button.png （tile 133，石按钮各面同贴图）

依赖：仅 PIL/numpy，无外部贴图。与 build_dispenser.py / build_mossy_cobble.py 同风格（程序生成原创像素图）。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 贴图边长（像素）

# 确定性伪随机（同 seed 同图案；便于 CI 校验 & 与 build_atlas.py 顺序对齐）。
_RNG = np.random.RandomState(4901)


def px(canvas, x, y, rgb):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb


def rect(canvas, x0, y0, x1, y1, rgb):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(canvas, x, y, rgb)


def wood_base():
    """木质底座（同橡木木板族棕色木纹）+ 细密噪点。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 140.0  # R
    canvas[..., 1] = 108.0  # G
    canvas[..., 2] = 64.0   # B（橡木棕底）
    canvas[..., 3] = 255.0
    lite = np.array([168.0, 130.0, 78.0])
    dark = np.array([104.0, 78.0, 44.0])
    m1 = _RNG.random((TS, TS)) < 0.22
    canvas[m1, 0:3] = lite
    m2 = _RNG.random((TS, TS)) < 0.22
    canvas[m2, 0:3] = dark
    return canvas


def stone_base():
    """石质底座（同圆石族灰色石质）+ 细密噪点。"""
    canvas = np.zeros((TS, TS, 4), dtype=np.float64)
    canvas[..., 0] = 118.0
    canvas[..., 1] = 118.0
    canvas[..., 2] = 118.0  # 中灰石质底
    canvas[..., 3] = 255.0
    lite = np.array([142.0, 142.0, 142.0])
    dark = np.array([96.0, 96.0, 96.0])
    m1 = _RNG.random((TS, TS)) < 0.20
    canvas[m1, 0:3] = lite
    m2 = _RNG.random((TS, TS)) < 0.20
    canvas[m2, 0:3] = dark
    return canvas


def draw_lever():
    """杠杆：木质底座 + 中央竖直扳柄 + 顶部圆柄头（杠杆特征）。"""
    c = wood_base()
    base_dark = np.array([88.0, 66.0, 36.0])    # 底座边框暗木
    handle = np.array([60.0, 44.0, 24.0])       # 扳柄深棕（金属 / 木质杆）
    handle_hi = np.array([120.0, 92.0, 52.0])   # 扳柄高光侧
    knob = np.array([180.0, 150.0, 80.0])       # 顶部圆柄头（亮木球）
    knob_hi = np.array([220.0, 195.0, 130.0])   # 圆柄头高光
    # 底座四周边框暗带（1 px，机关基座感）。
    rect(c, 0, 0, TS - 1, 0, base_dark)
    rect(c, 0, TS - 1, TS - 1, TS - 1, base_dark)
    rect(c, 0, 0, 0, TS - 1, base_dark)
    rect(c, TS - 1, 0, TS - 1, TS - 1, base_dark)
    # 中央竖直扳柄（2×6 杆，自底座中段向上立起）+ 左侧高光边。
    rect(c, 7, 5, 8, 10, handle)
    px(c, 7, 5, handle_hi)  # 杆顶左角高光
    rect(c, 6, 5, 6, 10, handle_hi)  # 杆左侧高光竖条（光自左 → 立体杆感）
    # 顶部圆柄头（3×3 亮球，杆顶之上）+ 高光点。
    rect(c, 6, 3, 8, 5, knob)
    px(c, 6, 3, knob_hi)
    px(c, 7, 3, knob_hi)
    # 底座中央铰链点（扳柄根部小暗点）。
    px(c, 7, 11, handle)
    px(c, 8, 11, handle)
    return c


def draw_button_wood():
    """木按钮：木质底座 + 中央凸起小圆钮（按钮特征）。"""
    c = wood_base()
    base_dark = np.array([88.0, 66.0, 36.0])    # 底座边框暗木
    knob = np.array([176.0, 138.0, 82.0])       # 中央圆钮（凸起木钮，略亮于底）
    knob_hi = np.array([210.0, 172.0, 110.0])   # 圆钮高光
    knob_dark = np.array([120.0, 90.0, 50.0])   # 圆钮底沿阴影
    # 底座四周边框暗带。
    rect(c, 0, 0, TS - 1, 0, base_dark)
    rect(c, 0, TS - 1, TS - 1, TS - 1, base_dark)
    rect(c, 0, 0, 0, TS - 1, base_dark)
    rect(c, TS - 1, 0, TS - 1, TS - 1, base_dark)
    # 中央凸起圆钮（4×4，居中）+ 顶沿高光 + 底沿阴影（立体凸钮）。
    rect(c, 6, 6, 9, 9, knob)
    rect(c, 6, 6, 9, 6, knob_hi)   # 钮顶沿高光（光自上 → 凸起上沿反光）
    rect(c, 6, 9, 9, 9, knob_dark) # 钮底沿阴影（凸起下沿暗）
    return c


def draw_button_stone():
    """石按钮：石质底座 + 中央凸起小圆钮（按钮特征，石质纹理区别木）。"""
    c = stone_base()
    base_dark = np.array([78.0, 78.0, 78.0])    # 底座边框暗石
    knob = np.array([152.0, 152.0, 152.0])      # 中央圆钮（凸起石钮，略亮于底）
    knob_hi = np.array([180.0, 180.0, 180.0])   # 圆钮高光
    knob_dark = np.array([104.0, 104.0, 104.0]) # 圆钮底沿阴影
    # 底座四周边框暗带。
    rect(c, 0, 0, TS - 1, 0, base_dark)
    rect(c, 0, TS - 1, TS - 1, TS - 1, base_dark)
    rect(c, 0, 0, 0, TS - 1, base_dark)
    rect(c, TS - 1, 0, TS - 1, TS - 1, base_dark)
    # 中央凸起圆钮（4×4，居中）+ 顶沿高光 + 底沿阴影（立体凸钮）。
    rect(c, 6, 6, 9, 9, knob)
    rect(c, 6, 6, 9, 6, knob_hi)
    rect(c, 6, 9, 9, 9, knob_dark)
    return c


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_lever(), "default_lever")
    save(draw_button_wood(), "default_wood_button")
    save(draw_button_stone(), "default_stone_button")


if __name__ == "__main__":
    main()
