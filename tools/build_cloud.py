#!/usr/bin/env python3
"""生成云层贴图（64×64 像素，原创程序生成，§9 override (a)）。

机制等价 MC 1.0 高空云层（一片缓慢漂移的扁平云盖，昼白夜灰），但贴图为本项目程序生成的
原创像素图，**不**拷贝任何 MC 资产。云的「形状 / 漂移 / 昼夜变色」全部由呈现层（Main.qml
的云 Model）实现；本脚本只产出一份**可无缝平铺**的云密度图（白 RGB + alpha 表云密度）。

视觉意图：读作「自然云盖」——半透明白色团块（云）散布在透明天空（缝）里，边界柔但可辨
（非纯噪声麻点、非硬方块）。昼夜由 Main.qml baseColor 乘灰阶（昼白、夜灰暗），故贴图本身
只存「云密度」（alpha）+ 中性白 RGB（昼 baseColor=白 → 显白；夜 baseColor=灰 → 显灰暗）。

无缝平铺（关键）：云 Model 是一张覆盖整片天空的大平面，纹理经 scaleU/V=N 重复铺贴，且漂移
靠「平面位置随时间偏移、按一个 tile 宽取模回绕」实现 —— 这要求贴图**自身四边可无缝拼接**
（否则回绕点显一道接缝）。故噪声场用**周期化 value noise**：在一张可回绕的粗网格上取随机
值，按 bilinear 插值（坐标取模回绕），多 octave 叠加 → 任意 tile 边界处左右/上下严格连续。
固定随机种子 → 确定性（同 CI 同图）。

图案（确定性、无外部资产）：
  - 两 octave 周期化 value noise（粗网格 8×8 + 细网格 16×16）叠加 → 自然的云团大小不一；
  - smoothstep 阈值化（中段陡、两端缓）→ 云团有清晰轮廓但边缘柔（非硬方块、非麻点）；
  - RGB 恒白（255），alpha = 密度（云心 ~230、边缘 ~0）。

输出（覆盖写入 textures/）：
  cloud.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 64  # 贴图边长（像素；云覆盖大片天空，比 16 格方块贴图大以保细节）

# 固定随机种子 → 确定性（同 CI 同图；§9 自绘原创）。
RNG = np.random.default_rng(20260807)


def _periodic_value_noise(grid_n):
    """周期化 value noise：在 grid_n×grid_n 的可回绕粗网格上取 [0,1] 随机值，
    按 bilinear 插值到 TS×TS（采样坐标取模 grid_n → 任意边界处严格连续 → 无缝平铺）。"""
    # 粗网格随机值（最后一行/列 = 第一行/列，保证 wrap 时连续）。
    g = RNG.random((grid_n, grid_n))
    # 双线性插值：把 TS 个采样点映射到 [0, grid_n) 连续坐标。
    ys = (np.arange(TS) * grid_n / TS) % grid_n
    xs = (np.arange(TS) * grid_n / TS) % grid_n
    x0 = np.floor(xs).astype(int) % grid_n
    x1 = (x0 + 1) % grid_n
    y0 = np.floor(ys).astype(int) % grid_n
    y1 = (y0 + 1) % grid_n
    fx = (xs - np.floor(xs))[None, :]            # (1, TS)
    fy = (ys - np.floor(ys))[:, None]            # (TS, 1)
    # smoothstep 缓和插值权重 → 减弱 bilinear 的「菱形」人工感。
    fx = fx * fx * (3.0 - 2.0 * fx)
    fy = fy * fy * (3.0 - 2.0 * fy)
    v00 = g[np.ix_(y0, x0)]
    v01 = g[np.ix_(y0, x1)]
    v10 = g[np.ix_(y1, x0)]
    v11 = g[np.ix_(y1, x1)]
    top = v00 + (v01 - v00) * fx
    bot = v10 + (v11 - v10) * fx
    return top + (bot - top) * fy                 # (TS, TS) ∈ [0,1]


def draw_cloud():
    """云密度场：两 octave 周期化 value noise 叠加 → smoothstep 阈值化 → 云团 + 柔边。"""
    # 粗 octave（大云团）权重高、细 octave（碎云）权重低。
    field = 0.65 * _periodic_value_noise(8) + 0.35 * _periodic_value_noise(16)
    # 归一化到 [0,1]（两 octave 和最大 ~1.0，按实际 min/max 拉满）。
    lo, hi = float(field.min()), float(field.max())
    field = (field - lo) / (hi - lo + 1e-9)

    # smoothstep 阈值化：阈值 0.42 以下 → 缝（alpha→0），以上 → 云（alpha→1），
    # 过渡带 ~0.18 → 边缘柔但可辨（非硬 cutout、非麻点噪声）。
    t = (field - 0.42) / 0.18
    alpha = np.clip(t, 0.0, 1.0)
    alpha = alpha * alpha * (3.0 - 2.0 * alpha)    # smoothstep
    alpha = alpha * 235.0                          # 云心 ~235（留 ~0.08 余量给材质 opacity 二次调）

    # RGB 恒白（昼夜变色由 Main.qml baseColor 乘灰阶实现，贴图只存密度）。
    rgba = np.zeros((TS, TS, 4), dtype=np.float64)
    rgba[..., 0] = 255.0
    rgba[..., 1] = 255.0
    rgba[..., 2] = 255.0
    rgba[..., 3] = alpha
    return rgba


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    save(draw_cloud(), "cloud")


if __name__ == "__main__":
    main()
