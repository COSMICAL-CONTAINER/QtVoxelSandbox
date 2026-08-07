#!/usr/bin/env python3
"""生成月相贴图（8 帧 64×64，原创程序生成，§9 override (a)）。

机制等价 MC 1.0 月相（8 相一周期：满→亏凸→下弦→残月→新月→蛾眉→上弦→盈凸），但贴图为本
项目程序生成的原创像素图，**不**拷贝任何 MC 资产。月的位置 / 升落 / 夜间显隐由呈现层（Main.qml
的月 Model）实现；本脚本只产出 8 帧固定相位的月盘贴图，呈现层据 WorldClock.moonPhase 选其一。

相位光照模型（球面投影）：把月视作面向相机的球，前半球任一像点 (x,y) 对应球面法线
n=(x,y,sqrt(1-x²-y²))。光源在相机-月平面内绕轴转 α=2π·phase（约定 phase=0 满月：光来自相机后方
L=(0,0,1) → 全前半球亮；phase=0.5 新月：L=(0,0,-1) → 全暗；quarters L=(±1,0,0) → 左 / 右半亮）。
像点亮的判据 = n·L > 0，L=(sin α, 0, cos α)。亏 / 盈由 α 增大方向自然决定（α∈(0,π) 亮部缩向右 =
盈→？约定见下）。

按 α 递增，8 帧 phase=i/8：
  i=0 α=0    : 满月（全亮）
  i=1 α=π/4  : 盈凸月（大部亮，亮缘在右）
  i=2 α=π/2  : 上弦月（右半亮）
  i=3 α=3π/4 : 蛾眉月（右侧窄亮）
  i=4 α=π    : 新月（全暗）
  i=5 α=5π/4 : 残月（左侧窄亮）
  i=6 α=3π/2 : 下弦月（左半亮）
  i=7 α=7π/4 : 亏凸月（大部亮，亮缘在左）

视觉意图：读作「夜空中的月」——亮部呈暖白米色（带几颗确定性环形山暗斑），暗部呈暗蓝灰
（略亮于夜空 #0b1026，使整盘在任何相位都隐约可见 → 验收「夜有月」即便恰逢新月也不空）。
圆盘半径 ~0.42 贴图单位，留出抗锯齿软边。

依赖：仅 numpy/PIL，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 64          # 贴图边长（像素；与 sun.png 同尺寸）
RADIUS = 0.42    # 月盘半径（贴图单位；留抗锯齿软边）
N_PHASES = 8

# 固定随机种子 → 确定性（同 CI 同图；§9 自绘原创）。
RNG = np.random.default_rng(20260807)

# 环形山暗斑（确定性）：几颗随机位置 / 半径的暗圆，叠在亮部上做细节（非纯平涂）。
CRATERS = []
for _ in range(7):
    # 在盘内随机（极坐标），半径 0.04..0.09。
    rr = RNG.random() * (RADIUS - 0.10)
    th = RNG.random() * 2.0 * np.pi
    CRATERS.append((rr * np.cos(th), rr * np.sin(th), 0.04 + RNG.random() * 0.05))


def moon_frame(phase):
    """单帧月相：返回 (TS,TS,4) RGBA。phase∈[0,1)：0 满、0.5 新。"""
    alpha = 2.0 * np.pi * phase
    Lx, Lz = np.sin(alpha), np.cos(alpha)
    # 像素网格，中心化到 [-0.5,0.5]，单位 = 贴图宽。
    coords = (np.arange(TS) + 0.5) / TS - 0.5
    gx, gy = np.meshgrid(coords, coords, indexing="xy")   # gx 列变化、gy 行变化
    # 注意 PNG 行序：gy 从上到下递增；月无上下之分（光照水平），故无需翻转。
    dist = np.sqrt(gx * gx + gy * gy)
    disk = dist <= RADIUS                                # 盘内掩膜
    # 盘内像点的球面法线（前半球 z>0）；盘外不参与（透明）。
    nx = gx / RADIUS
    ny = gy / RADIUS
    nz2 = 1.0 - nx * nx - ny * ny
    nz = np.sqrt(np.clip(nz2, 0.0, 1.0))
    # 亮判据：n·L > 0（L 光源方向）。
    lit = (nx * Lx + nz * Lz) > 0.0
    lit = np.logical_and(lit, disk)

    # 抗锯齿软边：盘边 ±1px alpha 渐变（避免硬锯齿圆）。
    edge = np.clip((RADIUS - dist) * TS + 0.5, 0.0, 1.0)

    # 亮部底色（暖白米色），暗部底色（暗蓝灰，略亮于夜空使整盘可见）。
    lit_col = np.array([245.0, 238.0, 214.0])   # 暖白米
    dark_col = np.array([34.0, 40.0, 60.0])     # 暗蓝灰（夜空 #0b1026≈(11,16,38)，略亮 → 盘可辨）

    rgba = np.zeros((TS, TS, 4), dtype=np.float64)
    for c in range(3):
        chan = np.where(lit, lit_col[c], dark_col[c])
        # 环形山暗斑（仅亮部）：每颗暗圆把亮度乘 ~0.72。
        for (cx, cy, cr) in CRATERS:
            d = np.sqrt((gx - cx) ** 2 + (gy - cy) ** 2)
            spot = (d < cr) & lit
            chan = np.where(spot, chan * 0.72, chan)
        rgba[..., c] = chan
    rgba[..., 3] = np.where(disk, edge * 255.0, 0.0)   # 盘外全透、盘内按软边 alpha
    return rgba


def save(arr, name):
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, name + ".png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    for i in range(N_PHASES):
        save(moon_frame(i / N_PHASES), "moon_%d" % i)


if __name__ == "__main__":
    main()
