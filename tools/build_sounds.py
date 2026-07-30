#!/usr/bin/env python3
"""原创 SFX 合成（§4 原创 SFX；零 MC 资产）。

合成 3 个短音效 WAV（22050 Hz mono 16-bit PCM），程序合成（无外部音频资产），
机制等价 MC 破/放/脚步的「干脆 / 闷响 / 柔垫」手感：
  - break.wav：破块音 —— 低频 thunk（~90Hz 正弦）+ 宽带噪声 crunch，混合后指数衰减 ~0.28s。
  - place.wav：放块音 —— 低频 plonk（~130Hz 正弦快速衰减）+ 起始短噪声瞬态 ~0.18s。
  - step.wav：脚步音 —— 柔和噪声脉冲 + 低频重量感，快衰减 ~0.12s。

确定性合成（固定 random.seed），同次运行产出字节一致的 WAV（便于 git/CI diff）。
运行：python tools/build_sounds.py（输出到工程根 sounds/）。
"""
import math
import struct
import wave
import random
from pathlib import Path

SR = 22050  # 采样率（SFX 足够；文件小）


def write_wav(path, samples):
    """samples ∈ [-1,1] float → 16-bit PCM mono WAV。"""
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)  # 16-bit
        w.setframerate(SR)
        frames = b"".join(
            struct.pack("<h", max(-32768, min(32767, int(s * 32767))))
            for s in samples
        )
        w.writeframes(frames)


def env_exp(t, decay):
    """指数衰减包络（e^{-decay*t}）。"""
    return math.exp(-t * decay)


def gen_break():
    """破块音：低频 thunk + 宽带噪声 crunch，混合后指数衰减。"""
    dur = 0.28
    n = int(SR * dur)
    random.seed(1337)  # 确定性（非运行期随机源；§4 法律合规：原创程序合成）
    out = []
    for i in range(n):
        t = i / SR
        thunk = math.sin(2 * math.pi * 90 * t) * 0.5
        crunch = random.uniform(-1, 1) * 0.7
        e = env_exp(t, 14.0)
        out.append((thunk * 0.6 + crunch * 0.5) * e)
    return out


def gen_place():
    """放块音：低频 plonk + 起始短噪声瞬态。"""
    dur = 0.18
    n = int(SR * dur)
    random.seed(7)
    out = []
    for i in range(n):
        t = i / SR
        plonk = math.sin(2 * math.pi * 130 * t) * 0.7
        transient = random.uniform(-1, 1) * 0.4 * env_exp(t, 60.0)  # 极短瞬态
        e = env_exp(t, 22.0)
        out.append((plonk + transient) * e)
    return out


def gen_step():
    """脚步音：柔和噪声脉冲 + 低频重量感，快衰减。"""
    dur = 0.12
    n = int(SR * dur)
    random.seed(42)
    out = []
    for i in range(n):
        t = i / SR
        noise = random.uniform(-1, 1) * 0.5
        body = math.sin(2 * math.pi * 70 * t) * 0.3  # 低频重量感
        e = env_exp(t, 30.0)
        out.append((noise + body) * e)
    return out


def main():
    root = Path(__file__).resolve().parent.parent
    out_dir = root / "sounds"
    out_dir.mkdir(exist_ok=True)
    for name, gen in [("break", gen_break), ("place", gen_place), ("step", gen_step)]:
        samples = gen()
        path = out_dir / f"{name}.wav"
        write_wav(path, samples)
        print(f"wrote {path} ({len(samples)} frames, {len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
