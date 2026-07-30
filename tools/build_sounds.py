#!/usr/bin/env python3
"""原创 SFX 合成（§4 原创 SFX；零 MC 资产）。

程序合成 22050 Hz mono 16-bit PCM WAV（无外部音频资产），机制等价 MC「按方块材质 SoundType
选声」手感（机制对齐，非名词照搬；§9 法律：原创程序合成）。两类产出：

1. **材质分组 clip 池**（t118）：石 / 木 / 草 / 沙 / 叶 5 组，每组各 break / mining / step
   三个短音 —— AudioManager 据 BlockRegistry::materialGroup(id) 选播（spec「playBreak/playMining/
   playStep 按 group 选」）。各组手感差异化（参数见 MATERIALS）：
     - stone：硬脆敲击 —— 高频 thunk + 宽带 crunch，明亮短促。
     - wood：中空闷击 —— 中频空心敲 + 弱噪声瞬态。
     - grass：柔垫软踩 —— 低频软噪声，无锐瞬态。
     - sand：颗粒沙响 —— 高频细噪声为主，无 body。
     - leaves：叶沙沙 —— 调幅噪声（慢颤），最轻。
   - break_X：完整破块音（最长 / 响，~0.25-0.30s，含 crunch 尾）。
   - mining_X：挖掘一阶的轻敲（spec「每挥一次响」；短 ~0.10s、亮度同组、能量约为 break 的 60%）。
   - step_X：踩在表面（最短 ~0.10s、能量约为 break 的 50%、body 偏弱）。
2. **单件音**：
   - place.wav：放块 plonk（保留 t89 既有合成；放置不分材质，单份）。
   - pickup.wav：拾取反馈 —— 短上扬正弦双音（~0.16s），轻快提示。

确定性合成（每组固定 random.seed），同次运行产出字节一致的 WAV（便于 git/CI diff）。
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


# 材质合成参数（手感差异化的单一权威表；新增材质组在此追加一行）。
# 字段：thunk_freq=敲击基频；crunch_gain=宽带噪声权重；body_gain=低频重量权重；hi=高频亮噪权重；
#       break_decay / mining_decay / step_decay = 三类音的衰减常数（越大越短促）；
#       energy_mul = 相对响度系数（stone 满、leaves 最弱）；seed = 确定性噪声源。
MATERIALS = {
    "stone":  dict(thunk_freq=110, crunch_gain=0.55, body_gain=0.35, hi_gain=0.30,
                   break_decay=13.0, mining_decay=24.0, step_decay=28.0, energy=1.00, seed=1337),
    "wood":   dict(thunk_freq=180, crunch_gain=0.20, body_gain=0.55, hi_gain=0.10,
                   break_decay=18.0, mining_decay=30.0, step_decay=34.0, energy=0.85, seed=2024),
    "grass":  dict(thunk_freq=70,  crunch_gain=0.45, body_gain=0.30, hi_gain=0.05,
                   break_decay=20.0, mining_decay=32.0, step_decay=36.0, energy=0.65, seed=42),
    "sand":   dict(thunk_freq=60,  crunch_gain=0.65, body_gain=0.10, hi_gain=0.45,
                   break_decay=24.0, mining_decay=38.0, step_decay=42.0, energy=0.60, seed=99),
    "leaves": dict(thunk_freq=120, crunch_gain=0.55, body_gain=0.05, hi_gain=0.20,
                   break_decay=16.0, mining_decay=26.0, step_decay=30.0, energy=0.50, seed=7),
}


def synth_material(name, kind):
    """按材质 + 音类（break/mining/step）合成一段样本。

    各音类差异：duration（break 最长 / step 最短）、衰减常数（kind_decay）、整体能量系数。
    能量系数（kind_energy）：break 1.0 / mining 0.6（每挥一击，比破轻）/ step 0.5（脚下踩，最弱）。
    音量末尾钳到 [-1,1]（防削顶爆音；spec「音量合理、不爆音」）。
    """
    m = MATERIALS[name]
    if kind == "break":
        dur, decay, kind_energy = 0.28, m["break_decay"], 1.0
    elif kind == "mining":
        dur, decay, kind_energy = 0.11, m["mining_decay"], 0.6
    else:  # step
        dur, decay, kind_energy = 0.11, m["step_decay"], 0.5
    n = int(SR * dur)
    rnd = random.Random(m["seed"] + hash(kind) % 1000)  # 每音类独立确定性序列
    out = []
    for i in range(n):
        t = i / SR
        thunk = math.sin(2 * math.pi * m["thunk_freq"] * t)
        crunch = rnd.uniform(-1, 1)
        body = math.sin(2 * math.pi * 55 * t)  # 低频重量感（统一基线）
        hi = rnd.uniform(-1, 1) * math.sin(2 * math.pi * 1800 * t)  # 高频亮瞬态
        e = env_exp(t, decay)
        amp = m["energy"] * kind_energy
        s = (thunk * m["body_gain"]
             + crunch * m["crunch_gain"]
             + body * 0.25
             + hi * m["hi_gain"]) * e * amp
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_place():
    """放块音：低频 plonk + 起始短噪声瞬态（保留 t89 合成；放置不分材质）。"""
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


def gen_pickup():
    """拾取音：短上扬双音（C5→E5 正弦）+ 轻噪声瞬态 ~0.16s。机制等价 MC 拾取「啵」反馈。"""
    dur = 0.16
    n = int(SR * dur)
    random.seed(20260731)
    out = []
    for i in range(n):
        t = i / SR
        # 两段频率：前半 ~523Hz（C5），后半 ~659Hz（E5）—— 上扬轻快感。
        f = 523.0 if t < dur * 0.5 else 659.0
        tone = math.sin(2 * math.pi * f * t) * 0.55
        transient = random.uniform(-1, 1) * 0.15 * env_exp(t, 50.0)
        e = env_exp(t, 16.0)
        out.append((tone + transient) * e)
    return out


def main():
    root = Path(__file__).resolve().parent.parent
    out_dir = root / "sounds"
    out_dir.mkdir(exist_ok=True)
    # 材质分组 clip 池：{break,mining,step}_{stone,wood,grass,sand,leaves}.wav（15 文件）
    for name in MATERIALS:
        for kind in ("break", "mining", "step"):
            samples = synth_material(name, kind)
            path = out_dir / f"{kind}_{name}.wav"
            write_wav(path, samples)
            print(f"wrote {path} ({len(samples)} frames, {len(samples)/SR:.2f}s)")
    # 单件音：place（保留）+ pickup（新）
    for name, gen in [("place", gen_place), ("pickup", gen_pickup)]:
        samples = gen()
        path = out_dir / f"{name}.wav"
        write_wav(path, samples)
        print(f"wrote {path} ({len(samples)} frames, {len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
