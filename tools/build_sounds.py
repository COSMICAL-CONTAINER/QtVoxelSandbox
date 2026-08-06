#!/usr/bin/env python3
"""原创 SFX 合成（§4 原创 SFX；零 MC 资产）。

程序合成 44100 Hz mono 16-bit PCM WAV（无外部音频资产），机制等价 MC「按方块材质 SoundType
选声」手感（机制对齐，非名词照搬；§9 法律：原创程序合成）。

t328 全面重做：用户反馈「ambient/hurt/collect/hostile 全听不清或沉闷怪异（0-1/10）」。根因诊断：
  1. **音量太低**：旧版多数 clip 内部峰值仅 ~0.13（如 ambient_wind），叠加 AudioManager 各级音量
     系数后实际播放峰值 <0.03 → 几乎听不见。修：所有 clip 经 `finalize()` 做 DC 阻隔 + 峰值归一化
     到目标峰值（默认 0.9）→ 统一满刻度、彻底根治「太轻」。
  2. **音色沉闷**：旧版基频偏低（hurt 90Hz、cow 165Hz、shambler 112Hz）+ 谐波少 + 起声慢 → 听感
     「沉、闷、糊」（低频一团无高频细节）。修：
     - 脚步：~1ms 宽带瞬态 click（高通噪声爆 + 极快指数衰减 τ≈0.8ms）→ 干脆的「踏」撞击，不再
       是拖沓的低频闷音；
     - 牛/羊/猪：改用**共振峰（formant）合成**（锯齿声门源 → 并联 2 极共振器组）→ 真正的元音 /
       动物叫声质感（非旧版纯正弦下扫的「电子蜂鸣」）。cow=moo 低共振峰、sheep=baa 高亮共振峰、
       pig=oink 鼻音共振峰 + 双爆发；
     - Shambler（机制等价僵尸）：多谐波锯齿源 + 低共振峰 + ~24Hz growl AM + rasp 气声 → 粗糙
       下沉的哀嚎（明显可辨、有存在感）；
     - Bones（骷髅）：高频噪声咔哒串 + 偶发空腔 tok → 干脆骨响；
     - Spider：高通 hiss + ~600Hz × 42Hz AM 嗡 → 愤怒虫鸣；
     - Stalker（苦力怕）：引信 hiss + 末段软 boom（一 Clip 内嘶嘶转爆炸）→ 一听即「引信怪物」；
     - hurt/mob_hurt/pickup/place/explosion/tool_break/door：抬升基频、加谐波 / 瞬态，明亮可辨。
  3. **新增 UI click**（ui_click.wav）：热键 / 滚轮切槽时的轻 tick 反馈（AudioManager.playUIClick，
     Main.qml 路由 hotbarVM.selectedSlotChanged）。

确定性合成：每生成器用固定 random.Random(seed)（**不再用 hash()**——Python3 字符串 hash 受
PYTHONHASHSEED 随机盐影响、跨进程不稳定；改显式 int 映射 KIND_SEED），同次运行产出字节一致 WAV。

运行：python tools/build_sounds.py（输出到工程根 sounds/）。
"""
import math
import struct
import wave
import random
from pathlib import Path

SR = 44100  # 采样率（t328 升到 44.1k：更多高频细节、更短瞬态分辨 → 音色清晰；AudioManager 同步设 44100）

# 音类 → 确定性种子偏移（显式 int，不用 hash() 避免跨进程随机盐；见模块 docstring）。
KIND_SEED = {"break": 11, "mining": 23, "step": 37}


def write_wav(path, samples):
    """samples ∈ [-1,1] float → 16-bit PCM mono WAV（先经 finalize() 归一化）。"""
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
    """指数衰减包络（e^{-decay*t}）。decay 越大越短促。"""
    return math.exp(-t * decay)


def finalize(samples, target_peak=0.9):
    """DC 阻隔（一阶高通 τ≈8ms）+ 峰值归一化到 target_peak。

    t328 核心修复：旧版各 clip 内部峰值参差（ambient_wind 仅 ~0.13）→ 播放听不见。归一化后所有 clip
    统一满刻度，由 AudioManager 各级音量系数（m_volume / kind 系数 / ambient base vol）单独控制
    相对响度。DC 阻隔避免低频偏置浪费量化动态范围。
    """
    if not samples:
        return samples
    a = 0.999  # 一阶高通系数（τ≈8ms，去 DC 与极低频偏置，不动可听低频）
    prev_in = 0.0
    prev_out = 0.0
    out = [0.0] * len(samples)
    for i, x in enumerate(samples):
        y = x - prev_in + a * prev_out
        prev_in = x
        prev_out = y
        out[i] = y
    peak = max(1e-6, max(abs(s) for s in out))
    g = target_peak / peak
    return [max(-1.0, min(1.0, s * g)) for s in out]


class Resonator:
    """2 极共振器（bandpass，共振峰合成用）。

    y[n] = g·x[n] + 2R·cos(θ)·y[n-1] − R²·y[n-2]，R=e^{−π·bw/fs}（bw 控带宽）、θ=2π·freq/fs。
    g=(1−R) 馈入增益大致归一化共振峰。锯齿声门源经并联共振器组 → 元音 / 动物叫声质感。
    """

    def __init__(self, freq, bw, sr=SR):
        R = math.exp(-math.pi * bw / sr)
        self.a1 = 2.0 * R * math.cos(2.0 * math.pi * freq / sr)
        self.a2 = R * R
        self.g = 1.0 - R
        self.s1 = 0.0
        self.s2 = 0.0

    def process(self, x):
        y = self.g * x + self.a1 * self.s1 - self.a2 * self.s2
        self.s2 = self.s1
        self.s1 = y
        return y


def voiced_formant(f0_fn, formants, dur, attack=0.04, decay_rate=3.0,
                   vib_rate=5.0, vib_depth=0.0, am_rate=0.0, am_depth=0.0,
                   noise_gain=0.0, seed=0):
    """共振峰合成有声音（牛/羊/猪/Shambler 叫声）。

    锯齿声门源（含全部 1/n 谐波）→ 并联共振器组（formants=[(freq,bw),...]）→ 元音 / 动物声质感。
    f0_fn(t_norm→[0,1]) 返回瞬时基频；vib_* 颤音、am_* AM 颤（咩 / growl）、noise_gain 气声（rasp）。
    attack 起声渐入；decay_rate 整体指数衰减。返回未归一化样本（caller 走 finalize）。
    """
    n = int(SR * dur)
    rnd = random.Random(seed)
    res = [Resonator(f, bw) for (f, bw) in formants]
    phase = 0.0
    out = [0.0] * n
    for i in range(n):
        tnorm = i / n if n else 0.0
        ts = i / SR
        f0 = f0_fn(tnorm)
        vib = vib_depth * math.sin(2 * math.pi * vib_rate * ts) if vib_rate else 0.0
        phase += (f0 + vib) / SR
        if phase >= 1.0:
            phase -= math.floor(phase)
        src = 2.0 * phase - 1.0  # 锯齿 -1..1（含全部谐波，喉音 / 声带源）
        y = 0.0
        for r in res:
            y += r.process(src)
        if noise_gain:
            y += rnd.uniform(-1, 1) * noise_gain
        am = 1.0
        if am_depth:
            am = 1.0 - am_depth * 0.5 + am_depth * 0.5 * math.sin(2 * math.pi * am_rate * ts)
        a = min(1.0, ts / attack) if attack else 1.0
        e = a * math.exp(-decay_rate * ts)
        out[i] = y * e * am
    return out


# 材质合成参数（手感差异化的单一权威表）。
# 字段：thunk_freq=敲击基频；transient_gain=起始 ~1ms 宽带 click 权重（脆 / 软）；body_gain=低频重量；
#       crunch_gain=宽带噪声权重；break_decay/mining_decay/step_decay=三类音衰减常数（越大越短促）；
#       energy=相对响度（峰值归一化目标系数，保留材质间手感差）；seed=确定性噪声源。
MATERIALS = {
    "stone":  dict(thunk_freq=210, transient_gain=0.95, body_gain=0.45, crunch_gain=0.55,
                   break_decay=15.0, mining_decay=28.0, step_decay=34.0, energy=1.00, seed=1337),
    "wood":   dict(thunk_freq=170, transient_gain=0.70, body_gain=0.60, crunch_gain=0.25,
                   break_decay=18.0, mining_decay=30.0, step_decay=34.0, energy=0.85, seed=2024),
    "grass":  dict(thunk_freq=120, transient_gain=0.20, body_gain=0.35, crunch_gain=0.55,
                   break_decay=20.0, mining_decay=32.0, step_decay=36.0, energy=0.70, seed=42),
    "sand":   dict(thunk_freq=110, transient_gain=0.55, body_gain=0.15, crunch_gain=0.80,
                   break_decay=22.0, mining_decay=34.0, step_decay=38.0, energy=0.65, seed=99),
    "leaves": dict(thunk_freq=150, transient_gain=0.30, body_gain=0.10, crunch_gain=0.60,
                   break_decay=17.0, mining_decay=28.0, step_decay=32.0, energy=0.55, seed=7),
}


def synth_material(name, kind):
    """按材质 + 音类（break/mining/step）合成一段样本（t328 重做）。

    起始 ~1ms 宽带瞬态 click（高通噪声爆 + τ≈0.8ms 极快衰减）→ 干脆的撞击「踏 / 啪」，区别旧版
    拖沓低频闷音；其后接 tonal thunk + crunch 尾。能量系数 kind_energy：break 1.0 / mining 0.7 /
    step 0.55（相对响度进 finalize 目标峰值；AudioManager 另有 kind 系数）。
    """
    m = MATERIALS[name]
    if kind == "break":
        dur, decay, kind_energy = 0.26, m["break_decay"], 1.0
    elif kind == "mining":
        dur, decay, kind_energy = 0.11, m["mining_decay"], 0.7
    else:  # step
        dur, decay, kind_energy = 0.09, m["step_decay"], 0.55
    n = int(SR * dur)
    rnd = random.Random(m["seed"] + KIND_SEED[kind])  # 确定性序列（显式 int，不用 hash）
    out = [0.0] * n
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.60  # 一阶高通（起始 click 用，去低频闷感留脆亮瞬态）
    for i in range(n):
        t = i / SR
        # 起始 ~1ms 宽带瞬态 click（干脆撞击「踏」）—— 高通噪声爆 × 极快指数衰减
        click_env = math.exp(-t / 0.0008)  # τ≈0.8ms（~1ms 瞬态，符合 spec「footstep=wideband noise ~1ms transient」）
        w1 = rnd.uniform(-1, 1)
        hpv = hp_a * (hp_prev_out + w1 - hp_prev_in)
        hp_prev_in = w1
        hp_prev_out = hpv
        click = hpv * m["transient_gain"] * click_env
        # tonal body（敲击基频回响）
        thunk = math.sin(2 * math.pi * m["thunk_freq"] * t)
        # 宽带 crunch 尾（材质颗粒感）
        crunch = rnd.uniform(-1, 1)
        # 低频重量（统一基线）
        body = math.sin(2 * math.pi * 65 * t)
        e = env_exp(t, decay)
        s = (click
             + thunk * m["body_gain"]
             + crunch * m["crunch_gain"]
             + body * 0.22) * e
        out[i] = s
    return finalize(out, target_peak=0.9 * m["energy"])  # 保留材质间相对响度（leaves < stone）


def gen_place():
    """放块音（t328）：明亮 plonk —— 较高基频 ~180Hz 上扫到 240Hz + 二次谐波 + 起始短 click。
    放置不分材质；与 break（破坏）/ pickup（拾取）音色明显区分（更圆润 / 上扬）。"""
    dur = 0.16
    n = int(SR * dur)
    rnd = random.Random(7)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        tnorm = i / n
        # 基频上扫 180→240Hz（放物「落定」的上扬感）
        f = 180.0 + 60.0 * tnorm
        tone = math.sin(2 * math.pi * f * t)
        harm = math.sin(2 * math.pi * 2 * f * t) * 0.30  # 二次谐波（圆润不空）
        click = rnd.uniform(-1, 1) * 0.35 * env_exp(t, 80.0)  # 起始极短瞬态
        e = env_exp(t, 24.0)
        out[i] = (tone * 0.7 + harm + click) * e
    return finalize(out)


def gen_pickup():
    """拾取 / 收集音（t328）：明亮上扬双音（A4 440Hz → E5 659Hz 正弦 + 三次谐波），轻快「叮」。
    机制等价 MC 拾取反馈；与 place（圆润落定）/ break（颗粒破坏）明显区分。"""
    dur = 0.15
    n = int(SR * dur)
    rnd = random.Random(20260731)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        # 两段频率：前半 440Hz（A4），后半 659Hz（E5）—— 上扬轻快「叮-叮」
        f = 440.0 if t < dur * 0.45 else 659.0
        tone = math.sin(2 * math.pi * f * t)
        harm = math.sin(2 * math.pi * 3 * f * t) * 0.10  # 三次谐波（金属铃质感）
        transient = rnd.uniform(-1, 1) * 0.12 * env_exp(t, 60.0)
        e = env_exp(t, 18.0)
        out[i] = (tone * 0.75 + harm + transient) * e
    return finalize(out)


def gen_ui_click():
    """UI 反馈 click（t328 新增）：热键 / 滚轮切槽时的轻 tick —— 极短高通噪声爆 + 微小 1200Hz 谐，
    ~0.05s。机制等价 MC 物品栏切换 tick 反馈（§9 原创）。AudioManager.playUIClick 触发，
    Main.qml 路由 hotbarVM.selectedSlotChanged。短 SFX（~0.05s），轻而不刺。"""
    dur = 0.05
    n = int(SR * dur)
    rnd = random.Random(328328)
    out = [0.0] * n
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.7
    for i in range(n):
        t = i / SR
        w = rnd.uniform(-1, 1)
        hpv = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = hpv
        click = hpv * 0.7 * env_exp(t, 120.0)  # 极快衰减（脆 tick）
        tone = math.sin(2 * math.pi * 1200.0 * t) * 0.15 * env_exp(t, 90.0)  # 微小高谐（木 / 塑按键感）
        out[i] = click + tone
    return finalize(out, target_peak=0.55)  # UI 反馈偏低（不抢前景 SFX）


def gen_door_open():
    """开门音：木质嘎吱上扬 + 起始短扣响（门闩松脱）~0.22s。机制等价 MC 木门开启声（§9 原创）。"""
    dur = 0.22
    n = int(SR * dur)
    rnd = random.Random(310152)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        f = 220.0 + 160.0 * (t / dur)  # 嘎吱基频缓升（门轴转动 pitch bend）
        creak = math.sin(2 * math.pi * f * t)
        grit = rnd.uniform(-1, 1) * 0.22 * math.sin(2 * math.pi * 2800 * t)  # 高频摩擦
        click_env = math.exp(-((t - 0.0) ** 2) / (2 * 0.010 ** 2))
        click = rnd.uniform(-1, 1) * 0.40 * click_env
        e = env_exp(t, 11.0)
        out[i] = (creak * 0.55 + grit + click) * e
    return finalize(out)


def gen_door_close():
    """关门音：低频闷击（门框撞上）+ 末尾扣响（门闩扣合）~0.20s。机制等价 MC 木门关闭声（§9 原创）。"""
    dur = 0.20
    n = int(SR * dur)
    rnd = random.Random(310153)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        thunk = math.sin(2 * math.pi * 110 * t)
        body = math.sin(2 * math.pi * 60 * t) * 0.30
        center = dur * 0.7
        click_env = math.exp(-((t - center) ** 2) / (2 * 0.006 ** 2))
        latch = rnd.uniform(-1, 1) * 0.45 * click_env
        e = env_exp(t, 14.0)
        out[i] = (thunk * 0.6 + body + latch) * e
    return finalize(out)


def gen_hurt():
    """玩家受伤音（t328）：中频「呃」grunt —— 抬升基频（220Hz 略下沉 + 440Hz 谐）+ 起始宽带冲击。
    旧版 90Hz 太沉闷；新版基频拉到中频 + 谐波 → 像「挨打闷哼」而非低频蜂鸣。~0.20s。机制等价 MC
    玩家受伤声（§9 原创）。PlayerState.damaged → AudioManager.playHurt 触发。"""
    dur = 0.20
    n = int(SR * dur)
    rnd = random.Random(778877)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        tnorm = i / n
        # 中频 grunt：220Hz 略下沉到 170Hz（挨打闷哼），加二次 + 三次谐（声带质感）
        f0 = 220.0 - 50.0 * tnorm
        tone = math.sin(2 * math.pi * f0 * t)
        h2 = math.sin(2 * math.pi * 2 * f0 * t) * 0.25
        h3 = math.sin(2 * math.pi * 3 * f0 * t) * 0.12
        # 起始宽带冲击（瞬态「啪」）
        impact = rnd.uniform(-1, 1) * 0.45 * env_exp(t, 45.0)
        e = env_exp(t, 12.0)
        out[i] = (tone * 0.55 + h2 + h3 + impact) * e
    return finalize(out)


def gen_mob_hurt():
    """生物受击音（t328）：creature yelp —— 下扫中频（560→320Hz，带 FM 颤）+ 软 thunk + 摩擦瞬态。
    与玩家 hurt 区分（更高 / 更亮 / 更短叫）。~0.18s。机制等价 MC 生物受击声（§9 原创）。"""
    dur = 0.18
    n = int(SR * dur)
    rnd = random.Random(812482)
    out = [0.0] * n
    for i in range(n):
        tnorm = i / n
        ts = i / SR
        thunk = math.sin(2 * math.pi * 160.0 * ts) * 0.35  # 软 thunk（小 creature 体腔）
        f_yelp = 560.0 - 240.0 * tnorm  # 下扫 yelp（被打一声短叫）
        fm = 35.0 * math.sin(2 * math.pi * 70.0 * ts)  # FM 颤
        yelp = math.sin(2 * math.pi * f_yelp * ts + fm) * 0.40
        grit = rnd.uniform(-1, 1) * 0.16 * math.sin(2 * math.pi * 2400.0 * ts)
        e = env_exp(ts, 15.0)
        attack = min(1.0, ts / 0.012)  # 12ms attack（被打 → 楞一下 → 叫）
        out[i] = (thunk + yelp + grit) * e * attack
    return finalize(out)


def gen_ambient_wind():
    """环境音 / 风声床（t328 重做）：长循环风声 —— 单极点低通白噪（低频起伏「风」body）+ 高通白噪
    （gust 高频细颗粒）+ 双 LFO 慢颤调幅 + 首末 80ms 三角窗淡化（循环无缝），~8s。
    t328 关键：旧版 base_amp=0.13 → 峰值 ~0.13 → 播放几乎听不见；finalize 归一化到满刻度 + 加高频 gust
    层 → 明显可闻的风声（AudioManager kAmbientBaseVol 再控制背景级）。机制等价 MC 环境 / 风声床（§9）。"""
    dur = 8.0
    n = int(SR * dur)
    rnd = random.Random(51501)
    # 低频水量床（单极点低通 → 低频起伏的「风」body）
    a = 0.985
    bed = [0.0] * n
    state = 0.0
    for i in range(n):
        w = rnd.uniform(-1, 1)
        state = a * state + (1.0 - a) * w
        bed[i] = state
    bed_inv = 1.0 / max(1e-6, max(abs(s) for s in bed))
    # 高频 gust（高通白噪 → 风的细颗粒 / 嘶嘶层，旧版缺、致风声太闷）
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.8
    gust = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        v = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = v
        gust[i] = v
    gust_inv = 1.0 / max(1e-6, max(abs(s) for s in gust))
    # 双 LFO 慢颤调幅（自然不规则起伏）
    lfo1 = 2 * math.pi * 0.30
    lfo2 = 2 * math.pi * 0.11
    fade_n = int(SR * 0.08)  # 80ms 首末淡化（循环无缝）
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        lfo = 0.55 + 0.30 * math.sin(lfo1 * t) + 0.15 * math.sin(lfo2 * t)
        s = (bed[i] * bed_inv * 0.65 + gust[i] * gust_inv * 0.40) * lfo
        if i < fade_n:
            s *= i / fade_n
        elif i > n - fade_n:
            s *= (n - 1 - i) / fade_n
        out[i] = s
    return finalize(out)  # 满刻度（AudioManager kAmbientBaseVol 控制背景级）


def gen_water_flow():
    """潺潺流水声（t328 保留 t269 三层 + 密集气泡设计，finalize 归一化）。长循环 ~8s：低频水量床 +
    中频「流水过石」沙沙 + 高频细流 hiss + 多重不规则 AM + 密集「咕嘟」气泡瞬态；首末 50ms 淡化无缝。
    机制等价 MC 近流水环境音（§9 原创）。"""
    dur = 8.0
    n = int(SR * dur)
    rnd = random.Random(70269)
    bed_lp_a = 0.06
    bed_state = 0.0
    bed = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        bed_state = bed_lp_a * w + (1.0 - bed_lp_a) * bed_state
        bed[i] = bed_state
    bed_inv = 1.0 / max(1e-6, max(abs(s) for s in bed))
    mid_lp_a = 0.25
    mid_lp_state = 0.0
    mid_raw = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        mid_lp_state = mid_lp_a * w + (1.0 - mid_lp_a) * mid_lp_state
        mid_raw[i] = mid_lp_state
    mid = [0.0] * n
    prev = 0.0
    for i in range(n):
        v = mid_raw[i] - prev
        prev = mid_raw[i]
        mid[i] = v
    mid_inv = 1.0 / max(1e-6, max(abs(s) for s in mid))
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.85
    hiss = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        v = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = v
        hiss[i] = v
    hiss_inv = 1.0 / max(1e-6, max(abs(s) for s in hiss))
    lfo_a = 2 * math.pi * 1.1
    lfo_b = 2 * math.pi * 2.3
    lfo_c = 2 * math.pi * 0.7
    ph_a = rnd.uniform(0, 2 * math.pi)
    ph_b = rnd.uniform(0, 2 * math.pi)
    ph_c = rnd.uniform(0, 2 * math.pi)
    bubbles = []
    t_b = 0.05
    while t_b < dur - 0.05:
        bubbles.append((t_b, rnd.uniform(250.0, 700.0), rnd.uniform(0.06, 0.13),
                        rnd.uniform(0.008, 0.020), rnd.uniform(0.5, 2.0)))
        t_b += rnd.uniform(0.08, 0.25)
    fade_n = int(SR * 0.05)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        am = 0.4 + 0.2 * math.sin(lfo_a * t + ph_a) + 0.2 * math.sin(lfo_b * t + ph_b) + 0.2 * math.sin(lfo_c * t + ph_c)
        if am < 0.05:
            am = 0.05
        s = (bed[i] * bed_inv * 0.30 + mid[i] * mid_inv * 0.55 + hiss[i] * hiss_inv * 0.25) * am
        for bt, bf, ba, bw, bs in bubbles:
            dtb = t - bt
            if -0.02 < dtb < bw * 4.0:
                env = math.exp(-(dtb ** 2) / (2 * bw * bw))
                f = bf + bs * 100.0 * dtb
                s += ba * env * math.sin(2 * math.pi * f * dtb)
        if i < fade_n:
            s *= i / fade_n
        elif i > n - fade_n:
            s *= (n - 1 - i) / fade_n
        out[i] = s
    return finalize(out)


def gen_water_step():
    """水中走路声：低频闷浊「咚」（水下 muffling）+ 起始「咕嘟」气泡 plop，~0.16s。机制等价 MC 水中
    走路声（§9 原创）。不分材质（水中听感统一闷浊），单件 clip。"""
    dur = 0.16
    n = int(SR * dur)
    rnd = random.Random(269269)
    out = [0.0] * n
    lp_state = 0.0
    lp_a = 0.12
    for i in range(n):
        t = i / SR
        thunk = math.sin(2 * math.pi * 100.0 * t) * 0.45
        w = rnd.uniform(-1, 1)
        lp_state = lp_a * w + (1.0 - lp_a) * lp_state
        murk = lp_state * 0.35
        plop_env = math.exp(-((t - 0.0) ** 2) / (2 * 0.012 ** 2))
        plop = math.sin(2 * math.pi * (280.0 + 200.0 * t) * t) * 0.35 * plop_env
        e = env_exp(t, 16.0)
        out[i] = (thunk + murk + plop) * e
    return finalize(out)


def gen_mob_idle_generic():
    """通用生物 idle 叫声：中性短 chirp —— 基频 ~440Hz 正弦 + 二次谐波 + 软 attack + 快衰减，~0.18s。
    供测试生物 / 未知 mobType 兜底（机制等价 MC 生物偶发 idle call；§9 原创）。"""
    dur = 0.18
    n = int(SR * dur)
    rnd = random.Random(250070)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        tone = math.sin(2 * math.pi * 440.0 * t) * 0.50
        harm = math.sin(2 * math.pi * 880.0 * t) * 0.15
        grit = rnd.uniform(-1, 1) * 0.12 * math.sin(2 * math.pi * 2600.0 * t)
        attack = min(1.0, t / 0.010)
        e = env_exp(t, 16.0)
        out[i] = (tone + harm + grit) * e * attack
    return finalize(out)


def gen_mob_idle_pig():
    """猪哼 idle（t328 共振峰）：鼻音 grunt —— 共振峰合成（锯齿源 + 鼻音共振峰 F1=500/F2=1500/F3=2600）
    × 双窄高斯爆发（拟「哼哼」两声），基频 ~170Hz，~0.35s。机制等价 MC 猪偶发 grunt（§9 原创）。"""
    dur = 0.35
    bursts = [0.18, 0.62]
    samples = voiced_formant(
        f0_fn=lambda tn: 170.0,
        formants=[(500, 90), (1500, 130), (2600, 200)],  # 鼻音共振峰（高 F2 = 鼻音色彩）
        dur=dur, attack=0.012, decay_rate=1.2,
        noise_gain=0.08, seed=250071)
    n = len(samples)
    rnd = random.Random(2500719)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        env = 0.0
        for b in bursts:
            env += math.exp(-((t - b * dur) ** 2) / (2 * 0.045 ** 2))
        env = min(1.0, env)
        out[i] = samples[i] * env * 1.4
    return finalize(out)


def gen_mob_idle_cow():
    """牛哞 idle（t328 共振峰）：长 moo —— 共振峰合成（锯齿源 + 低共振峰 F1=350/F2=850/F3=2400 = 「ooo」
    元音）+ 基频 200→130Hz 缓降 + ~5Hz vibrato + 慢 attack，~0.62s。机制等价 MC 牛偶发 moo（§9 原创）。
    旧版纯 165Hz 正弦下扫 = 电子蜂鸣；共振峰合成后是真正「哞」的元音质感。"""
    samples = voiced_formant(
        f0_fn=lambda tn: 200.0 - 70.0 * tn,  # 基频缓降（哞的下沉轮廓）
        formants=[(350, 80), (850, 110), (2400, 180)],  # 「ooo」低共振峰
        dur=0.62, attack=0.08, decay_rate=3.0,
        vib_rate=5.0, vib_depth=4.0,  # ~5Hz vibrato（声带轻颤）
        noise_gain=0.05, seed=250072)
    return finalize(samples)


def gen_mob_idle_sheep():
    """羊咩 idle（t328 共振峰）：bleat —— 共振峰合成（锯齿源 + 高亮共振峰 F1=720/F2=1300/F3=2700 = 「aaa」
    元音）+ 基频 ~360Hz × ~12Hz AM 颤（咩的颤抖）+ 中 attack，~0.45s。机制等价 MC 羊偶发 baa（§9 原创）。
    比牛更高 / 更亮（高共振峰 + 高基频 + AM 颤）。"""
    samples = voiced_formant(
        f0_fn=lambda tn: 360.0,
        formants=[(720, 110), (1300, 160), (2700, 220)],  # 「aaa」高亮共振峰
        dur=0.45, attack=0.025, decay_rate=5.0,
        am_rate=12.0, am_depth=0.7,  # ~12Hz AM 颤（咩-咩颤抖）
        noise_gain=0.06, seed=250073)
    return finalize(samples)


def gen_mob_idle_shambler():
    """敌对 Shambler（机制等价僵尸）idle 哀嚎（t328 共振峰重做）：多谐波锯齿源（含全部 1/n 谐波 → 粗糙
    喉音）+ 低共振峰（F1=480/F2=1050/F3=2500）+ 基频 160→105Hz 下沉 + ~24Hz growl AM（粗颤吼）+
    rasp 气声（亡灵破损）+ 慢 attack，~0.60s。机制等价 MC 敌对生物偶发 idle call（§9 原创；PLAN §9
    区隔改名 shambler，非 MC 专名）。比牛哞更粗糙 / 多谐波 / 颤吼（明显可辨、有存在感）。"""
    samples = voiced_formant(
        f0_fn=lambda tn: 160.0 - 55.0 * tn,  # 基频下沉（呻吟下沉）
        formants=[(480, 130), (1050, 200), (2500, 320)],  # 低 + 宽共振峰（亡灵喉音）
        dur=0.60, attack=0.05, decay_rate=2.5,
        am_rate=24.0, am_depth=0.55,  # ~24Hz growl AM（粗颤吼）
        vib_rate=5.0, vib_depth=3.0,
        noise_gain=0.12,  # rasp 气声（亡灵破损）
        seed=294074)
    return finalize(samples)


def gen_mob_idle_bones():
    """敌对 Bones（机制等价骷髅）idle 骨头咔哒（t328）：高频噪声咔哒串 + 偶发空腔 1000Hz tok。
    不规则分布 6-10 个短 click（高通噪声爆 × 极窄高斯包络 ~8ms），干脆干 percussive 无 sustain，
    ~0.30s。机制等价 MC 敌对生物偶发 idle call（§9 原创；PLAN §9 区隔改名 bones，非 MC 专名）。"""
    dur = 0.30
    n = int(SR * dur)
    rnd = random.Random(294075)
    clicks = []
    tc = 0.02
    while tc < dur - 0.02:
        clicks.append((tc, rnd.uniform(0.30, 0.55), rnd.uniform(0.006, 0.012), rnd.uniform(0, 2 * math.pi)))
        tc += rnd.uniform(0.022, 0.045)
    out = [0.0] * n
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.85
    for i in range(n):
        t = i / SR
        w = rnd.uniform(-1, 1)
        v = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = v
        noise = v
        s = 0.0
        for ct, ca, cw, cph in clicks:
            dtc = t - ct
            if -0.02 < dtc < cw * 4.0:
                env = math.exp(-(dtc ** 2) / (2 * cw * cw))
                s += ca * env * (0.7 * noise + 0.3 * math.sin(2 * math.pi * 1000.0 * dtc + cph))
        out[i] = s * 0.9
    return finalize(out)


def gen_mob_idle_stalker():
    """敌对 Stalker（机制等价苦力怕）idle（t328）：引信 hiss + 末段软 boom（一 Clip 内嘶嘶转爆炸）。
    前段高通白噪 hiss + ~3200Hz 略升哨音（引信燃灼尖啸）+ 半正弦 swell；末段 ~0.10s 软 boom（低频闷击
    + 宽带爆裂，暗示爆炸），~0.42s。机制等价 MC 敌对生物偶发 idle call（§9 原创；PLAN §9 区隔改名 stalker，
    非_MC 专名）。idle 软 boom 远弱于 explosion.wav（ambient 暗示，非真爆炸）。"""
    dur = 0.42
    n = int(SR * dur)
    rnd = random.Random(294076)
    out = [0.0] * n
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.80
    boom_t = dur * 0.62  # 嘶嘶段结束、boom 段起点
    for i in range(n):
        t = i / SR
        w = rnd.uniform(-1, 1)
        v = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = v
        hiss = v
        s = 0.0
        if t < boom_t:
            # 引信嘶嘶段：高通 hiss + 略升哨音 × 半正弦 swell
            tn = t / boom_t
            f_w = 3000.0 + 400.0 * tn
            whistle = math.sin(2 * math.pi * f_w * t) * 0.14
            swell = math.sin(math.pi * tn)
            s = (hiss * 0.55 + whistle) * swell
        else:
            # 末段软 boom（低频闷击 + 宽带爆裂，快速衰减；ambient 暗示非真爆炸）
            dt = t - boom_t
            body = (math.sin(2 * math.pi * 70.0 * dt) * 0.45 + math.sin(2 * math.pi * 110.0 * dt) * 0.25)
            crack = rnd.uniform(-1, 1) * 0.40 * env_exp(dt, 30.0)
            s = (body + crack) * env_exp(dt, 12.0) * 0.8
        out[i] = s
    return finalize(out)


def gen_mob_idle_spider():
    """敌对 Spider（机制等价蜘蛛）idle 愤怒嘶嗡（t328）：高通白噪 hiss + ~600Hz 载波 × ~42Hz 攻击性 AM
    （振翅嗡）+ 半正弦 swell，~0.40s。比 stalker 更持续 / 带嗡。机制等价 MC 敌对生物偶发 idle call
    （§9 原创；PLAN §9 区隔改名 spider，非 MC 专名）。"""
    dur = 0.40
    n = int(SR * dur)
    rnd = random.Random(294077)
    out = [0.0] * n
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.78
    for i in range(n):
        t = i / dur
        ts = i / SR
        w = rnd.uniform(-1, 1)
        v = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = v
        hiss = v
        buzz_carrier = math.sin(2 * math.pi * 600.0 * ts) * 0.22
        am = 0.5 + 0.5 * math.sin(2 * math.pi * 42.0 * ts)
        swell = math.sin(math.pi * t)
        out[i] = (hiss * 0.45 + buzz_carrier * am) * swell
    return finalize(out)


def gen_tool_break():
    """工具破损音（t328）：干脆「啪嗒」断裂 —— 起始极短宽带 crack + 较高基频 snap（620Hz，区别 hurt
    低闷）+ 弱低频 thunk + 末尾碎屑沙沙，~0.20s。机制等价 MC 工具耐久耗尽破损声（§9 原创）。"""
    dur = 0.20
    n = int(SR * dur)
    rnd = random.Random(315315)
    out = [0.0] * n
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.82
    for i in range(n):
        t = i / SR
        crack_env = math.exp(-((t - 0.0) ** 2) / (2 * 0.004 ** 2))
        crack = rnd.uniform(-1, 1) * 0.60 * crack_env
        snap = math.sin(2 * math.pi * 620.0 * t) * 0.42 * env_exp(t, 32.0)
        thunk = math.sin(2 * math.pi * 120.0 * t) * 0.20 * env_exp(t, 20.0)
        w = rnd.uniform(-1, 1)
        v = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = v
        debris_env = env_exp(t - 0.06, 16.0) if t > 0.06 else 0.0
        debris = v * 0.22 * debris_env
        out[i] = crack + snap + thunk + debris
    return finalize(out)


def gen_explosion():
    """爆炸音（t328）：低频 body（55+85Hz 双正弦 → 厚「轰」）+ 中频咆哮（130Hz + FM 颤）+ 起始宽带
    爆裂瞬态 + 尾音低频余响，~0.50s。机制等价 MC 爆炸声（§9 原创）。Stalker 自爆走此 clip。"""
    dur = 0.50
    n = int(SR * dur)
    rnd = random.Random(284284)
    out = [0.0] * n
    lp_state = 0.0
    lp_a = 0.08
    for i in range(n):
        t = i / SR
        body = math.sin(2 * math.pi * 55.0 * t) * 0.45 + math.sin(2 * math.pi * 85.0 * t) * 0.30
        fm = 18.0 * math.sin(2 * math.pi * 9.0 * t)
        roar = math.sin(2 * math.pi * 130.0 * t + fm) * 0.22
        crack = rnd.uniform(-1, 1) * 0.55 * env_exp(t, 30.0)
        w = rnd.uniform(-1, 1)
        lp_state = lp_a * w + (1.0 - lp_a) * lp_state
        rumble = lp_state * 0.30
        e = env_exp(t, 5.0)
        attack = min(1.0, t / 0.004)
        out[i] = (body + roar + crack + rumble) * e * attack
    return finalize(out)


def gen_lava():
    """岩浆低频 rumble + 气泡（t343）。长循环 ~8s：低频隆隆床（拟地壳深处的持续低吼）+ 中频「咕嘟」气泡瞬态
    （拟岩浆表面鼓泡破裂）+ 偶发深爆裂（拟大块结皮塌陷）。首末 50ms 淡化无缝。机制等价 MC 近岩浆环境音（§9 原创）。"""
    dur = 8.0
    n = int(SR * dur)
    rnd = random.Random(51317)
    # 低频隆隆床：极低截止的噪声（拟持续低频轰鸣）。
    bed_lp_a = 0.012
    bed_state = 0.0
    bed = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        bed_state = bed_lp_a * w + (1.0 - bed_lp_a) * bed_state
        bed[i] = bed_state
    bed_inv = 1.0 / max(1e-6, max(abs(s) for s in bed))
    # 中频气泡瞬态（咕嘟）：不规则间隔、低频正弦脉冲 + 指数衰减包络。
    bubbles = []
    t_b = 0.08
    while t_b < dur - 0.08:
        bubbles.append((t_b, rnd.uniform(90.0, 220.0), rnd.uniform(0.10, 0.20),
                        rnd.uniform(0.025, 0.060), rnd.uniform(-40.0, 40.0)))
        t_b += rnd.uniform(0.10, 0.30)
    # 偶发深爆裂（结皮塌陷）：稀疏、更低频、更长包络。
    cracks = []
    t_c = 0.5
    while t_c < dur - 0.5:
        cracks.append((t_c, rnd.uniform(50.0, 90.0), rnd.uniform(0.18, 0.28), rnd.uniform(0.08, 0.14)))
        t_c += rnd.uniform(1.5, 3.5)
    fade_n = int(SR * 0.05)
    out = [0.0] * n
    # 低频床慢 AM（拟岩浆活动起伏，非恒定轰鸣）。
    lfo_a = 2 * math.pi * 0.5
    lfo_b = 2 * math.pi * 1.3
    ph_a = rnd.uniform(0, 2 * math.pi)
    ph_b = rnd.uniform(0, 2 * math.pi)
    for i in range(n):
        t = i / SR
        am = 0.6 + 0.2 * math.sin(lfo_a * t + ph_a) + 0.2 * math.sin(lfo_b * t + ph_b)
        if am < 0.1:
            am = 0.1
        s = bed[i] * bed_inv * 0.55 * am
        for bt, bf, ba, bw, bs in bubbles:
            dtb = t - bt
            if -0.02 < dtb < bw * 4.0:
                env = math.exp(-(dtb ** 2) / (2 * bw * bw))
                f = bf + bs * 20.0 * dtb
                s += ba * env * math.sin(2 * math.pi * f * dtb)
        for ct, cf, ca, cw in cracks:
            dtc = t - ct
            if -0.02 < dtc < cw * 4.0:
                env = math.exp(-(dtc ** 2) / (2 * cw * cw))
                s += ca * env * math.sin(2 * math.pi * cf * dtc)
        if i < fade_n:
            s *= i / fade_n
        elif i > n - fade_n:
            s *= (n - 1 - i) / fade_n
        out[i] = s
    return finalize(out)


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
    # 单件音（t328 重做合成 + 新增 ui_click）：
    for name, gen in [("place", gen_place), ("pickup", gen_pickup),
                      ("ui_click", gen_ui_click),
                      ("door_open", gen_door_open), ("door_close", gen_door_close),
                      ("hurt", gen_hurt), ("mob_hurt", gen_mob_hurt),
                      ("explosion", gen_explosion),
                      ("tool_break", gen_tool_break),
                      ("ambient_wind", gen_ambient_wind),
                      ("water_flow", gen_water_flow),
                      ("water_step", gen_water_step),
                      ("lava", gen_lava),
                      ("mob_idle", gen_mob_idle_generic),
                      ("mob_idle_pig", gen_mob_idle_pig),
                      ("mob_idle_cow", gen_mob_idle_cow),
                      ("mob_idle_sheep", gen_mob_idle_sheep),
                      ("mob_idle_shambler", gen_mob_idle_shambler),
                      ("mob_idle_bones", gen_mob_idle_bones),
                      ("mob_idle_stalker", gen_mob_idle_stalker),
                      ("mob_idle_spider", gen_mob_idle_spider)]:
        samples = gen()
        path = out_dir / f"{name}.wav"
        write_wav(path, samples)
        print(f"wrote {path} ({len(samples)} frames, {len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
