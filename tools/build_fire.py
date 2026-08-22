#!/usr/bin/env python3
"""t724/t803 生成火焰「条带」贴图（fireHost delegate 的材质级 flipbook 动画）。

机制等价 MC 1.0 火 flipbook（fire_0/fire_layer 逐层动画），但贴图为本项目程序生成的原创像素图，**不**
拷贝任何 MC 资产（§9 override (a)）。

条带结构（与 Main.qml fireStripTex 三方共用 BlockRegistry::kFireStripFrames 常量；改帧数必须同步）：
  - fire_strip.png：16 宽 × 512 高 = 1 列 × 32 行（每帧 16×16）。
    火焰轮廓 = 下宽上尖的双侧内收锯齿；透明底（alpha=0）—— 火焰是 cutout 式非满格贴图（delegate quad
    全 [0,1] UV + 材质 Mask cutout，与水 / 岩浆的满格实心底不同）。

**t803 帧动画语义（修用户「火从格子上方往下播放」错位）**：每帧是**自成一体**的火焰形（火舌高度 /
宽度 / 热点位置随帧抖动 = 原地闪烁），**绝不做帧间纵向循环位移（np.roll axis=0）**。旧版每帧把基准火
整体 roll k px → 翻书播放时火焰内容逐帧平移穿过格子（顶出格顶 / 底部穿入），观感「火从格子上方往下
播放」。MC 真 fire_0 帧也是原地逐帧变形（实测 demo 包 16×512 条带帧间非位移关系），对齐该语义。
帧 k 的火舌参数 = 确定性位混叠散列（帧序号喂 Knuth 乘数）→ 同一次生成结果恒定（无随机种子漂移）。

帧序约定（**帧 0 在图像底部、帧内容保持原方向**；同 build_fluid_strips.py t563 ② 修复后的约定——
不 flipud，rows.reverse() 后 vstack）。火焰路走 QML delegate（fireStripTex），帧区采 UV 全 [0,1]
+ Texture scaleV=1/N + positionV=k/N（与水 / 岩浆的 chunk-mesh 路不同源：mesher 烘焙 UV v∈[0,1/N]）。

包覆盖：resourcepackmanager 启用包时，以本程序生成条带为底、包内 block/fire_0.png 帧覆盖（demo 包
实测 16×512 = 32 帧现成 strip，帧数天然与本常量对齐）→ 落盘合成条带。无包时 QML 直接加载本 qrc 条带。

输出（覆盖写入 textures/）：
  fire_strip.png

依赖：仅 PIL/numpy，无外部贴图。
"""
import os
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "textures")
TS = 16  # 帧像素边长（与图集瓦片 kTile=16 同源）

FIRE_FRAMES = 32  # 与 BlockRegistry::kFireStripFrames 一致


def px(canvas, x, y, rgb, a=255.0):
    if 0 <= x < TS and 0 <= y < TS:
        canvas[y, x, 0:3] = rgb
        canvas[y, x, 3] = a


def flame_column(canvas, cx, base_w, top_y, core_rgb, edge_rgb):
    """画一条自底向上的火舌：底部宽 base_w，向上逐行收窄到 top_y。core 在内、edge 镶边。"""
    for y in range(top_y, TS):
        # 行 y 的半宽：自底 (TS-1) 的 base_w/2 线性收窄到顶行的 1px。
        t = (TS - 1 - y) / max(1, TS - 1 - top_y)
        half = max(0.0, base_w / 2.0 * (1.0 - t) + 0.5)
        for x in range(int(cx - half), int(cx + half) + 1):
            inner = abs(x - cx) < max(1.0, half * 0.55)
            px(canvas, x, y, core_rgb if inner else edge_rgb)


def spark(canvas, x, y, white_rgb):
    """单像素白炽热点（焰心顶 / 边缘火星）。"""
    px(canvas, x, y, white_rgb)


def frame_rand(k, salt):
    """帧 k 的确定性伪随机 ∈ [0,1)：Knuth 乘数位混叠散列（同散布确定性思路——同帧序号恒同值，无种子）。"""
    h = (k * 2654435761 + salt * 40503 + 0x9E3779B9) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65536.0


def build_fire_strip():
    """1 列 × 32 帧。每帧原地闪烁（火舌高度 / 底宽 / 热点位随帧抖动，无整体位移）；帧 0 在底（t563 ②）。"""
    orange = np.array([232.0, 96.0, 16.0])     # 主体橙（外焰）
    deep = np.array([188.0, 44.0, 8.0])        # 深橙红（边缘）
    yellow = np.array([255.0, 202.0, 48.0])    # 焰心黄
    white = np.array([255.0, 246.0, 208.0])    # 白炽热点

    frames = []
    for k in range(FIRE_FRAMES):
        canvas = np.zeros((TS, TS, 4), dtype=np.float64)  # 透明底（cutout 式）
        # 三条火舌（中主 + 两侧辅）：每帧高度 ±2px / 底宽 ±1px 抖动 → 火舌舔动但根部位移不超过 1px
        # （原地闪烁，非滚动）。top_y 下限 1：主焰尖恒在格内（不顶出帧）。
        main_top = max(1, 2 + int(frame_rand(k, 1) * 3))        # 主火舌尖 2..4
        left_top = max(5, 7 + int(frame_rand(k, 2) * 3))        # 左辅火舌尖 7..9
        right_top = max(4, 5 + int(frame_rand(k, 3) * 3))       # 右辅火舌尖 5..7
        flame_column(canvas, 7.0, 9 + int(frame_rand(k, 4) * 3), main_top, yellow, orange)
        flame_column(canvas, 3.0, 5 + int(frame_rand(k, 5) * 2), left_top, orange, deep)
        flame_column(canvas, 12.0, 4 + int(frame_rand(k, 6) * 2), right_top, orange, deep)
        # 白炽热点（焰心顶部 + 边缘火星）：位置随帧在 ±1px 邻域抖动（火花闪烁；不做整体横移）。
        spark(canvas, 7 + int(frame_rand(k, 7) * 3) - 1, main_top + int(frame_rand(k, 8) * 2), white)
        spark(canvas, 3 + int(frame_rand(k, 9) * 3) - 1, left_top + int(frame_rand(k, 10) * 2), white)
        spark(canvas, 12 + int(frame_rand(k, 11) * 3) - 1, right_top + int(frame_rand(k, 12) * 2), white)
        spark(canvas, 5 + int(frame_rand(k, 13) * 3) - 1, 13, white)
        spark(canvas, 10 + int(frame_rand(k, 14) * 3) - 1, 12, white)
        frames.append(canvas)
    frames.reverse()                    # 反序：帧 0 落到图像底（PIL 行 496..511），帧 31 在顶（t563 ② 约定）
    grid = np.vstack(frames)            # (512, 16, 4)，行 0 = 帧 31
    img = Image.fromarray(np.clip(grid, 0, 255).astype(np.uint8), "RGBA")
    out = os.path.join(SRC, "fire_strip.png")
    img.save(out)
    print("wrote", os.path.relpath(out, HERE), img.size)


def main():
    build_fire_strip()


if __name__ == "__main__":
    main()
