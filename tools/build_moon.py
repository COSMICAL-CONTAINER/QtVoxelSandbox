#!/usr/bin/env python3
"""生成月相贴图（8 帧 64×64，原创程序生成，§9 override (a)）。

机制等价 MC 1.0 月相（8 相一周期：满→亏凸→下弦→残月→新月→蛾眉→上弦→盈凸），但贴图为本
项目程序生成的原创像素图，**不**拷贝任何 MC 资产。月的位置 / 升落 / 夜间显隐由呈现层（Main.qml
的月 Model）实现；本脚本只产出 8 帧固定相位的月盘贴图，呈现层据 WorldClock.moonPhase 选其一。

t570 正方形月亮（用户复盘：「月亮背景灰色 PNG 消不掉 → 改正方形月亮，MC 月亮就是正方形」）：
  旧版画**圆盘**（半径 0.42 贴图单位）+ 暗部 alpha=0 透明，靠 alphaCutoff 剔除盘外像素 —— 但圆盘
  抗锯齿软边的半透像素经 alphaCutoff 混合仍呈灰晕背景，多轮消不掉。根因：圆盘 + 透明边在本工程
  材质管线上先天不稳（半透边缘像素混出夜空色差）。机制等价改法：MC 1.0 的月亮本就是**正方形**
  贴图（whole texture square），故改为满贴图正方形月（64×64 全画布不透明），相位明暗画在方形
  边界内 —— 无透明像素、无 alphaCutoff、无灰背景，一步根除。

t586 二轮复盘（用户「月亮还是圆的 + 背景灰色偏色」）：t570 的方形贴图本身不透明（alpha 全 255），
  但**着色模型**在方形内画了一个内切圆 —— 旧版把方形象点投到球面法线 n=(x,y,sqrt(1-x²-y²))，
  方形四角 |n_xy|≈sqrt(2)·0.5>1 → nz 被钳到 0 → 角上 n·L≈0 → 四角恒判暗 →「暗部」（暗蓝灰
  #22283c）恰好填满方形四角 = 圆外区域，肉眼读作「灰底上的圆月」（暗蓝灰与夜空色不同 → 有偏色）。
  根因：球形光照模型与满画布方形不兼容（球只在单位圆内有定义，方形角落必落暗）。
  修：**放弃球面投影，改平面 terminator 模型**（MC 1.0 方形月的真实画法）：明暗分界线
  （terminator）是**直线**（垂直于光源方向的弦），由每个像素到「垂直于光源方向且过中心的直线」
  的带符号距离决定 —— 距离>0 亮、<0 暗，边界锐利（无中间灰）。四角与圆内同规则 → 四角可以是
  亮的（盈凸相位）→ 整图方形可读、无内切圆痕迹、无灰底。

相位光照模型（平面 terminator，画在正方形内）：把月视作正方形面片，光源在面内绕轴转
  α=2π·phase（约定 phase=0 满月：光来自正后方 → 全亮；phase=0.5 新月：光来自正前 → 全暗）。
  像素亮判据 = 该像素沿光源方向在面片上的投影坐标 s = (x·cosα' + y·sinα')（α' 为相位角）超过
  terminator 偏移 d=cosα（MC 风格近似：terminator 直线 s=d，d 从 +∞(全暗) 到 0(半亮) 到 -∞(全亮)，
  d=0.5·cosα：0.5 ≥ |s|max(0.492) → 满相位恰全亮 / 新相位恰全暗，中间相位 87%/50%/12% 梯度）。按 α 递增 8 帧 phase=i/8：
  i=0 α=0    : 满月（全亮）
  i=1 α=π/4  : 盈凸月（大部亮，亮缘在右）
  i=2 α=π/2  : 上弦月（右半亮）
  i=3 α=3π/4 : 蛾眉月（右侧窄亮）
  i=4 α=π    : 新月（全暗）
  i=5 α=5π/4 : 残月（左侧窄亮）
  i=6 α=3π/2 : 下弦月（左半亮）
  i=7 α=7π/4 : 亏凸月（大部亮，亮缘在左）

视觉意图：读作「夜空中的方月」（机制等价 MC 1.0 正方形月亮）——亮部呈暖白米色（带几颗确定性
环形山暗斑），暗部呈暗蓝灰（略亮于夜空使整盘任何相位都隐约可辨，机制等价 MC 全贴图不透明的
暗部地面）。全画布不透明 → 呈现层材质无需 alphaCutoff（灰背景根除）。

依赖：仅 numpy/PIL，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 64          # 贴图边长（像素；与 sun.png 同尺寸）
N_PHASES = 8

# 固定随机种子 → 确定性（同 CI 同图；§9 自绘原创）。
RNG = np.random.default_rng(20260807)

# 环形山暗斑（确定性）：几颗随机位置 / 半径的暗圆，叠在亮部上做细节（非纯平涂）。
CRATERS = []
for _ in range(7):
    # 在盘内随机（极坐标），半径 0.04..0.09。
    rr = RNG.random() * 0.40
    th = RNG.random() * 2.0 * np.pi
    CRATERS.append((rr * np.cos(th), rr * np.sin(th), 0.04 + RNG.random() * 0.05))


def moon_frame(phase):
    """单帧月相：返回 (TS,TS,4) RGBA（全画布不透明）。phase∈[0,1)：0 满、0.5 新。
    t586：平面 terminator 模型（球面投影在方形四角恒落暗 → 内切圆 + 灰角背景，根除）。
    明暗分界 = 直线：像素坐标 s = 沿「光源在面内的垂直方向」的投影，s > d 亮 / s < d 暗，
    d = 0.5·cosα（0.5 ≥ |s|max=0.492：满相位恰全亮 / 新相位恰全暗；中间相位 87%/50%/12% 梯度）。"""
    alpha = 2.0 * np.pi * phase
    # terminator 直线方向：光源绕面法线转 α → 明暗弦垂直于 (sin α, cos α)。取 s = x·sinα + y·cosα。
    sx = np.sin(alpha)
    sy = np.cos(alpha)
    d = 0.5 * np.cos(alpha)  # terminator 偏移（0.5 ≥ |s|max=0.492：满相位恰全亮 / 新相位恰全暗；中间相位 87%/50%/12% 梯度）
    # 像素网格，中心化到 [-0.5,0.5]，单位 = 贴图宽（正方形满画布，无盘半径裁剪）。
    coords = (np.arange(TS) + 0.5) / TS - 0.5
    gx, gy = np.meshgrid(coords, coords, indexing="xy")   # gx 列变化、gy 行变化
    # 注意 PNG 行序：gy 从上到下递增；月无上下之分（光照水平），故无需翻转。
    s = gx * sx + gy * sy
    lit = s < d  # 平面 terminator：直线分界（s < d 亮；d=+0.5 满月全亮 / d=-0.5 新月全暗），四角与中心同规则（无内切圆）

    # 亮部底色（暖白米色），暗部底色（暗蓝灰，略亮于夜空使整盘可辨 —— 全贴图不透明）。
    lit_col = np.array([245.0, 238.0, 214.0])   # 暖白米
    dark_col = np.array([34.0, 40.0, 60.0])     # 暗蓝灰（夜空 #0b1026≈(11,16,38)，略亮 → 盘可辨）

    rgba = np.zeros((TS, TS, 4), dtype=np.float64)
    for c in range(3):
        chan = np.where(lit, lit_col[c], dark_col[c])
        # 环形山暗斑（仅亮部）：每颗暗圆把亮度乘 ~0.72。
        for (cx, cy, cr) in CRATERS:
            dspot = np.sqrt((gx - cx) ** 2 + (gy - cy) ** 2)
            spot = (dspot < cr) & lit
            chan = np.where(spot, chan * 0.72, chan)
        rgba[..., c] = chan
    rgba[..., 3] = 255.0   # 全画布不透明（t570：正方形月亮，无透明像素 → 无 alphaCutoff / 无灰背景）
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
