#!/usr/bin/env python3
"""声效生成（t381：Kenney CC0 录制 + 程序合成混合；零 MC 资产）。

t381（RECURRENCE of t366）：脚步 / 破坏 / 挖掘 / 拾取(collect) / 放 / UI / 门 / 受伤 改用 Kenney.nl CC0
录制素材（tools/cc0_audio/*.ogg，CC0 1.0 Universal，专业母带、一听即辨，根治前两轮合成的「闷 / 像没声」）；
动物 / 敌对 idle 叫声 改加性声门源共振峰合成（去嗡嗡，详见下方 imports 后的 t381 注释块 + voiced_formant）。
其余（环境风 / 流水 / 岩浆 / 爆炸 / 工具破损）保留程序合成（t366 已修白噪 / 氛围长音 / 特定瞬态，合成合适）。

程序合成部分生成 44100 Hz mono 16-bit PCM WAV；CC0 部分由 vendored ogg 经 soundfile+scipy 转换（44100 mono
+ DC 阻隔 + 峰值归一）。机制等价 MC「按方块材质 SoundType 选声」手感（机制对齐，非名词照搬；§9：CC0 + 原创，
零 MC 资产）。

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

import soundfile as sf
from scipy.signal import resample_poly

# t381：声效质量真修（RECURRENCE of t366）。前两轮（t328 加共振峰 / t366 删白噪层）后实测仍不合格：
#   脚步「闷」、动物叫「像没声」。频谱诊断（实测 sounds/*.wav）定位两个**真根因**（非音量、非路由——
#   Main.qml 路由已全、AudioManager 音量系数合理）：
#   1. 脚步声 80% 能量 >2kHz（step_stone centroid≈10kHz）= 几乎纯高频噪声 tick，无低频「踏」体 → 读作「闷/糊」。
#   2. 动物叫用**原始锯齿源**（-6dB/oct，谐波过亮过齐）经共振峰 → 电子蜂鸣质感，听不出是动物。
#
#   修法（任务首选 CC0，否则大改合成）：
#   - 脚步 / 破 / 挖 / 拾取(collect) / 放 / UI / 门 / 受伤 → 改用 **Kenney.nl CC0** 录制素材
#     （tools/cc0_audio/*.ogg，CC0 1.0 Universal，零 MC 资产；专业母带，一听即辨）。load_cc0 读 ogg →
#     44100 mono → DC 阻隔 + 峰值归一 → 写 wav。材质间用不同 Kenney 表面（concrete/wood/grass/snow/carpet
#     脚步；Plate/Wood/Soft/Plank 破坏）区分音色。
#   - 动物 / 敌对 idle 叫声 → 改 voiced_formant 的**声门源**：原始锯齿 → 加性谐波合成（1/n^tilt，tilt≈1.3 =
#     -8dB/oct 谱倾斜，比锯齿 -6dB 更接近真声带激励，去嗡嗡感）+ **低通化气声**（noise_gain 现走一阶低通 →
#       呼吸气声而非嘶嘶白噪）+ 4-5 共振峰 + 更自然的基频轮廓 / 颤音 / AM。机制等价 MC 生物偶发 idle call
#       （§9 原创；零 MC 资产；PLAN §9 区隔改名 shambler/bones/stalker/spider）。
#   - 环境风 / 流水 / 岩浆 / 爆炸 / 工具破损 → 保留合成（t366 已修白噪 / 这些是氛围长音 / 特定瞬态，合成合适）。
#
# 依赖（仅重生成时需，构建不跑本脚本——sounds/*.wav 已入仓）：numpy / soundfile(含 libsndfile 解 ogg) / scipy。
CC0_DIR = Path(__file__).resolve().parent / "cc0_audio"  # vendored Kenney CC0 ogg 源（committed）

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


def load_cc0(name, target_peak=0.85):
    """t381：加载 vendored Kenney CC0 ogg → 44100 Hz mono float 样本（DC 阻隔 + 峰值归一）。

    Kenney.nl 全资产 CC0 1.0 Universal（无需署名、零 MC 资产）。专业母带的录制 / 设计音效，一听即辨
    （脚步「踏」、破坏「碎」、拾取「叮」），远胜前两轮程序合成的「闷 / 像没声」。立体声 → mono（均值），
    非 44100 → scipy 重采样（与 AudioManager kSampleRate=44100 一致，避免 miniaudio 二次重采样丢高频）。
    finalize 复用同一 DC 阻隔 + 峰值归一管线，统一各 clip 播放电平（由 AudioManager 各级音量系数控相对响度）。
    """
    path = CC0_DIR / (name + ".ogg")
    data, sr = sf.read(str(path), always_2d=False, dtype="float64")  # ndarray
    if data.ndim > 1:
        data = data.mean(axis=1)  # 立体声 → mono（声道均值）
    if sr != SR:
        data = resample_poly(data, SR, sr)  # 多项式重采样（抗混叠；48000→44100 等）
    return finalize(data.tolist(), target_peak=target_peak)


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
                   noise_gain=0.0, tilt=1.3, nharmonics=24, seed=0):
    """共振峰合成有声音（牛/羊/猪/Shambler 叫声）。

    t381 真修：前两轮用**原始锯齿源**（谐波 1/n = -6dB/oct，过亮过齐 → 电子蜂鸣，听不出动物）。改**加性
    声门源**：∑ (1/n^tilt)·sin(2π·n·phase)，tilt≈1.3 → -8dB/oct 谱倾斜，更接近真声带体积速度激励（去嗡嗡感、
    更「肉」）。谐波幅度按 1/n^tilt 预算并归一（tilt / 谐波数变不改变源能量 → 各 clip finalize 后电平一致）。
    noise_gain 现走**一阶低通**（呼吸气声，非嘶嘶白噪）→ 真实气声 / rasp。f0_fn 返回瞬时基频；vib_* 颤音、
    am_* AM 颤（咩 / growl）。attack 起声渐入；decay_rate 整体指数衰减。返回未归一化样本（caller 走 finalize）。
    """
    n = int(SR * dur)
    rnd = random.Random(seed)
    res = [Resonator(f, bw) for (f, bw) in formants]
    # 加性声门源谐波幅度（1/n^tilt，预归一化能量）；nharmonics 覆盖到 ~f0*nharmonics Hz（足够共振峰合成）。
    hamp = [1.0 / ((k + 1) ** tilt) for k in range(nharmonics)]
    hnorm = 1.0 / max(1e-9, sum(hamp))
    hamp = [h * hnorm for h in hamp]
    phase = 0.0
    # 气声一阶低通状态（低截止 → 呼吸气声而非嘶嘶）。
    asp_state = 0.0
    asp_a = 0.06
    out = [0.0] * n
    for i in range(n):
        tnorm = i / n if n else 0.0
        ts = i / SR
        f0 = f0_fn(tnorm)
        vib = vib_depth * math.sin(2 * math.pi * vib_rate * ts) if vib_rate else 0.0
        phase += (f0 + vib) / SR
        if phase >= 1.0:
            phase -= math.floor(phase)
        # 加性声门源（去锯齿嗡嗡感；-tilt·6 dB/oct 谱倾斜）。
        src = 0.0
        ph2pi = 2.0 * math.pi * phase
        for k in range(nharmonics):
            src += hamp[k] * math.sin(ph2pi * (k + 1))
        y = 0.0
        for r in res:
            y += r.process(src)
        if noise_gain:
            w = rnd.uniform(-1, 1)
            asp_state = asp_a * w + (1.0 - asp_a) * asp_state
            y += asp_state * noise_gain
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


# t381：脚步 / 破坏 / 挖掘改用 Kenney CC0 录制素材（专业母带、一听即辨）。材质 → Kenney 表面映射：
#   脚步 surface：石头=混凝土 / 木=木地板 / 草=草地 / 沙=雪(软碎) / 叶=地毯(软)。各表面音色不同 →
#     踩不同方块听感可辨（spec「playStep 按 group 选」的音色差异）。
#   破坏 impact：石头=Plate重击 / 木=Wood中击 / 草=Soft重击 / 沙=Soft中击 / 叶=Plank中击（脆空腔）。
#   挖掘 mining：统一用 impactMining（镐击石，挖掘声与材质音色差异小；统一亦更连贯）。
STEP_CC0 = {"stone": "step_concrete", "wood": "step_wood", "grass": "step_grass",
            "sand": "step_snow", "leaves": "step_carpet"}
# t520：草/泥土破坏声修正。旧 grass→impact_soft_heavy（软体重击）经 finalize 峰值归一化后频谱重心仅 ~87Hz
#   （实测 break_grass.wav）—— 几乎纯次低频、扬声器难以重放、人耳近不可闻，故「挖草/泥土没声音」（用户报）。
#   而同 CC0 库的 step_grass（草地脚步录制）centroid ~254Hz、明显可辨（已作 grass step 用）。故 grass 破坏改用
#   step_grass 源（真实草地表面音、清晰可闻），与 step 同源但破坏路径 target_peak 更高（0.92 满刻度近前，
#   破坏是强反馈事件宜响于脚步）。sand/leaves 各用其专属 impact 源（软击 / 板击），音色区分保留。
BREAK_CC0 = {"stone": "impact_plate_heavy", "wood": "impact_wood_med", "grass": "step_grass",
             "sand": "impact_soft_med", "leaves": "impact_plank_med"}


def synth_material(name, kind):
    """按材质 + 音类（break/mining/step）取一段样本（t381 改 Kenney CC0）。

    前两轮程序合成脚步 80% 能量 >2kHz = 纯高频噪声 tick（实测 centroid≈10kHz）→「闷 / 糊」；破坏声同理偏
    高频无体。改用 Kenney CC0 录制音效：脚步「踏」有低频体 + 脆瞬态、破坏「碎」有冲击体，一听即辨。
    target_peak：脚步 0.78（频繁、宜稍弱）；破坏按材质 energy（石响叶轻，保留相对响度）；挖掘 0.80。
    """
    if kind == "step":
        return load_cc0(STEP_CC0[name], target_peak=0.78)
    if kind == "break":
        # t520：grass 破坏声用 step_grass 源（centroid ~254Hz 可辨），但 step_grass 内部峰值偏弱（脚步录制）。
        #   故 grass 破坏提到满刻度 0.95（破坏是强反馈事件、须明显可闻），不再压 MATERIALS["grass"]["energy"]
        #   （0.70 会把已偏弱的草地声进一步压到难辨）。其余材质仍按 energy 比控相对响度。
        if name == "grass":
            return load_cc0(BREAK_CC0[name], target_peak=0.95)
        return load_cc0(BREAK_CC0[name], target_peak=0.90 * MATERIALS[name]["energy"])
    # mining
    return load_cc0("impact_mining", target_peak=0.80)


def gen_place():
    """放块音（t381 改 Kenney CC0）：轻 plate 落定冲击（impactPlate_light，方块着地的干脆轻击）。
    放置不分材质；与 break（重碎）/ pickup（叮）音色区分（更轻、更短）。"""
    return load_cc0("impact_plate_light", target_peak=0.85)


def gen_pickup():
    """拾取 / 收集音（t381 改 Kenney CC0）：明亮确认「叮」（confirmation，正反馈上扬音）。
    机制等价 MC 拾取反馈；一听即「收进背包」。AudioManager.playPickup 触发。"""
    return load_cc0("confirm", target_peak=0.85)


def gen_ui_click():
    """UI 反馈 click（t381 改 Kenney CC0）：极短按键 tick（click，热键 / 滚轮切槽反馈）。
    机制等价 MC 物品栏切换 tick 反馈（§9 原创合成已替换为 CC0 录制，更干脆）。AudioManager.playUIClick 触发，
    Main.qml 路由 hotbarVM.selectedSlotChanged。target_peak=0.55（UI 反馈偏低，不抢前景 SFX）。"""
    return load_cc0("click", target_peak=0.55)


def gen_door_open():
    """开门音（t381 改 Kenney CC0 rpg doorOpen）：木质门轴开启声（嘎吱 + 扣响），一听即「开门」。
    机制等价 MC 木门开启声（§9；零 MC 资产）。"""
    return load_cc0("door_open", target_peak=0.85)


def gen_door_close():
    """关门音（t381 改 Kenney CC0 rpg doorClose）：木质门框合上声（闷击 + 门闩扣合），一听即「关门」。
    机制等价 MC 木门关闭声（§9；零 MC 资产）。"""
    return load_cc0("door_close", target_peak=0.85)


def gen_hurt():
    """玩家受伤音（t381 改 Kenney CC0）：重击冲击（impactPunch_heavy，挨打的沉闷体击 + 瞬态）。
    一听即「挨打」，远胜前两轮合成 grunt（中频蜂鸣感）。机制等价 MC 玩家受伤声（§9；零 MC 资产）。
    PlayerState.damaged → AudioManager.playHurt 触发。"""
    return load_cc0("impact_punch_heavy", target_peak=0.90)


def gen_mob_hurt():
    """生物受击音（t381 改 Kenney CC0）：中等拳击冲击（impactPunch_medium，被动生物被打的体击）。
    与玩家 hurt 区分（稍轻）。机制等价 MC 生物受击声（§9；零 MC 资产）。敌对（4-7）受击复用各自 idle
    clip（见 playMobHurt），本 clip 仅被动（0-3）/ 通用路径用。"""
    return load_cc0("impact_punch_med", target_peak=0.85)


def gen_ambient_wind():
    """环境音 / 风声床（t366 真正修复白噪）。

    RECURRENCE 真因：t328 为「让风声更亮」在此加了高通白噪「gust」层（权重 0.40）+ finalize 满刻度归一化。
    该层在频谱上占 ~53% 能量（>2kHz）且本 clip 是**进游戏即自动启动的 8s 循环 looping 声**，结果 = 持续满幅
    宽带嘶嘶（电视雪花 / 雨声白噪），掩盖所有前景 SFX（脚步 / 动物叫「听不清 / 像没声」）。这正是「t328 之后更糟」
    的来源——修音量没用，因为问题层是宽带噪声本身。

    真正修复：**删除 gust 高通层**，仅留两级级联一阶低通（陡降、高频几乎无）+ 双慢 LFO AM（自然起伏）+
    首末 80ms 淡化（循环无缝）。频谱集中在 <300Hz、高频能量 <8%（旧 53%）→ 柔和低频背景风、非静态噪声。
    AudioManager kAmbientBaseVol 进一步压到背景级。机制等价 MC 环境风声床（§9 原创）。

    教训（写入 lessons-learned）：**长循环 ambient 床永远不要含宽带高通噪声层**——低通化的低频风声才是「风」，
    高通宽带层在任何音量下都是静态噪声，且因其 looping 自动启动会持续淹没前景。"""
    dur = 8.0
    n = int(SR * dur)
    rnd = random.Random(51501)
    # 两级级联一阶低通（a=0.990 → a=0.985）：截止数十至百 Hz、陡降，高频几乎为零 → 柔和低频「风」body。
    a1 = 0.990
    s1 = 0.0
    bed = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        s1 = a1 * s1 + (1.0 - a1) * w
        bed[i] = s1
    a2 = 0.985
    s2 = 0.0
    for i in range(n):
        s2 = a2 * s2 + (1.0 - a2) * bed[i]
        bed[i] = s2
    bed_inv = 1.0 / max(1e-6, max(abs(s) for s in bed))
    # 双慢 LFO 调幅（自然风势起伏；0.13Hz + 0.07Hz 拟阵风）
    lfo1 = 2 * math.pi * 0.13
    lfo2 = 2 * math.pi * 0.07
    fade_n = int(SR * 0.08)  # 80ms 首末淡化（循环无缝）
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        lfo = 0.55 + 0.30 * math.sin(lfo1 * t) + 0.15 * math.sin(lfo2 * t)
        s = bed[i] * bed_inv * lfo
        if i < fade_n:
            s *= i / fade_n
        elif i > n - fade_n:
            s *= (n - 1 - i) / fade_n
        out[i] = s
    return finalize(out, target_peak=0.7)  # 低于满刻度 → 柔和背景级（非前景 SFX 量级）


def gen_water_flow():
    """潺潺流水声（t366 重做可辨）。

    t328 版「中频流水过石」层用微分器（low-pass 后做差分）实现 —— 微分本质是高通，结果整段 ~85% 能量 >2kHz =
    嘶嘶静态噪声，听不出「流水」。t366 修复：中频层改**中低通**（非微分）→ 中频柔和沙沙而非高频嘶嘶；降 hiss 权重
    （0.25→0.10）；提低频水量床权重；保留密集「咕嘟」气泡瞬态（音高可辨的咕嘟 = 流水的辨识特征）。
    首末 50ms 淡化无缝。~8s。机制等价 MC 近流水环境音（§9 原创）。"""
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
    # 中频柔和流水沙沙：中低通（非微分高通）→ 中频颗粒而非高频嘶嘶
    mid_lp_a = 0.20
    mid_state = 0.0
    mid = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        mid_state = mid_lp_a * w + (1.0 - mid_lp_a) * mid_state
        mid[i] = mid_state
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
        s = (bed[i] * bed_inv * 0.45 + mid[i] * mid_inv * 0.35 + hiss[i] * hiss_inv * 0.10) * am
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
    """通用生物 idle 叫声（t381 改 voiced_formant）：中性短 chirp —— 加性声门源经共振峰（F1=600/F2=1400/
    F3=2500）→ 自然 creature 叫声质感（非旧版纯正弦电子音）。基频 420→360Hz 缓降 + 气声。~0.20s。供测试生物 /
    未知 mobType 兜底（机制等价 MC 生物偶发 idle call；§9 原创）。"""
    samples = voiced_formant(
        f0_fn=lambda tn: 420.0 - 60.0 * tn,
        formants=[(600, 120), (1400, 180), (2500, 250)],
        dur=0.20, attack=0.012, decay_rate=4.0,
        noise_gain=0.05, tilt=1.3, seed=250070)
    return finalize(samples)


def gen_mob_idle_pig():
    """猪哼 idle（t381 加性声门源重做）：鼻音 grunt —— 共振峰合成（加性声门源 + 鼻音共振峰 F1=500/F2=1500/
    F3=2600/F4=3500，高 F2 = 鼻音色彩）× 双窄高斯爆发（拟「哼哼」两声），基频 ~180Hz，~0.35s。
    机制等价 MC 猪偶发 grunt（§9 原创）。"""
    dur = 0.35
    bursts = [0.18, 0.62]
    samples = voiced_formant(
        f0_fn=lambda tn: 180.0,
        formants=[(500, 100), (1500, 150), (2600, 250), (3500, 350)],
        dur=dur, attack=0.012, decay_rate=1.2,
        noise_gain=0.06, tilt=1.3, seed=250071)
    n = len(samples)
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
    """牛哞 idle（t381 加性声门源重做）：长 moo —— 共振峰合成（锯齿源已换加性声门源 → 不嗡嗡）+
    低共振峰 F1=400/F2=850/F3=2400/F4=3300 = 「ooo」元音 + 基频 150→95Hz 缓降（真牛哞下沉轮廓）+
    ~5Hz vibrato + 呼吸气声。~0.62s。机制等价 MC 牛偶发 moo（§9 原创）。"""
    samples = voiced_formant(
        f0_fn=lambda tn: 150.0 - 55.0 * tn,
        formants=[(400, 90), (850, 120), (2400, 200), (3300, 300)],
        dur=0.62, attack=0.08, decay_rate=3.0,
        vib_rate=5.0, vib_depth=4.0,  # ~5Hz vibrato（声带轻颤）
        noise_gain=0.10, tilt=1.4, seed=250072)
    return finalize(samples)


def gen_mob_idle_sheep():
    """羊咩 idle（t381 加性声门源重做）：bleat —— 共振峰合成（加性声门源 + 高亮共振峰 F1=720/F2=1300/
    F3=2700/F4=3500 = 「aaa」元音）+ 基频 ~360Hz × ~12Hz AM 颤（咩-咩颤抖）+ 气声。~0.45s。
    机制等价 MC 羊偶发 baa（§9 原创）。比牛更高 / 更亮（高共振峰 + 高基频 + AM 颤）。"""
    samples = voiced_formant(
        f0_fn=lambda tn: 360.0,
        formants=[(720, 120), (1300, 170), (2700, 230), (3500, 350)],
        dur=0.45, attack=0.025, decay_rate=5.0,
        am_rate=12.0, am_depth=0.7,  # ~12Hz AM 颤（咩-咩颤抖）
        noise_gain=0.08, tilt=1.2, seed=250073)
    return finalize(samples)


def gen_mob_idle_shambler():
    """敌对 Shambler（机制等价僵尸）idle 哀嚎（t381 加性声门源重做）：加性声门源（去嗡嗡）+ 低宽共振峰
    （F1=480/F2=1050/F3=2500/F4=3200 = 亡灵喉音）+ 基频 155→95Hz 下沉 + ~24Hz growl AM（粗颤吼）+
    rasp 气声（亡灵破损）。~0.60s。机制等价 MC 敌对生物偶发 idle call（§9 原创；PLAN §9 区隔改名 shambler）。
    比牛哞更粗糙 / 多谐波 / 颤吼。"""
    samples = voiced_formant(
        f0_fn=lambda tn: 155.0 - 60.0 * tn,  # 基频下沉（呻吟下沉）
        formants=[(480, 140), (1050, 220), (2500, 320), (3200, 400)],  # 低 + 宽共振峰（亡灵喉音）
        dur=0.60, attack=0.05, decay_rate=2.5,
        am_rate=24.0, am_depth=0.55,  # ~24Hz growl AM（粗颤吼）
        vib_rate=5.0, vib_depth=3.0,
        noise_gain=0.14,  # rasp 气声（亡灵破损）
        tilt=1.5, seed=294074)
    return finalize(samples)


def gen_mob_idle_bones():
    """敌对 Bones（机制等价骷髅）idle 骨头咔哒（t366 重做可辨）。

    t328 版以高通噪声咔哒为主（频谱 ~71% >2kHz）= 纯噪声，听不出「骨头」。t366 修复：每下咔哒改为**空心木块 tok**
    （基频 600-1100Hz 正弦 + 二次谐，拟骨头相击的脆空腔音）为主、噪声 tick 降为陪衬；6-10 个不规则间隔。干脆、带
    音高的敲击串，一听即「骨头咔哒」。~0.30s。机制等价 MC 骷髅 idle（§9 原创；PLAN §9 区隔改名 bones，非 MC 专名）。"""
    dur = 0.30
    n = int(SR * dur)
    rnd = random.Random(294075)
    clicks = []
    tc = 0.02
    while tc < dur - 0.02:
        # 每下咔哒：时间 / 振幅 / 极窄高斯宽度 / 空心 tok 基频（带音高的木块音）
        clicks.append((tc, rnd.uniform(0.50, 0.90), rnd.uniform(0.005, 0.010), rnd.uniform(600.0, 1100.0)))
        tc += rnd.uniform(0.022, 0.045)
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        s = 0.0
        for ct, ca, cw, cf in clicks:
            dtc = t - ct
            if -0.02 < dtc < cw * 4.0:
                env = math.exp(-(dtc ** 2) / (2 * cw * cw))
                tok = math.sin(2 * math.pi * cf * dtc) + 0.4 * math.sin(2 * math.pi * 2 * cf * dtc)  # 空心木块 tok
                tick = rnd.uniform(-1, 1) * 0.18  # 极小噪声 tick（骨头相击的颗粒质感）
                s += ca * env * (tok * 0.6 + tick)
        out[i] = s * 0.9
    return finalize(out)


def gen_mob_idle_stalker():
    """敌对 Stalker（机制等价苦力怕）idle（t381 真修可辨）。

    真根因（实测定位）：t366 引信段含高通 hiss 层，crest factor 高 → finalize 峰值归一化被 hiss 尖峰锁定 →
    哨音基频(2.5-3kHz)被压成噪声底、实测该带仅 ~10%，听感偏嘶嘶而非引信哨（权重再小也无效，因按峰归一化）。
    t381 真修：删引信段 hiss，纯上升哨音（2500→3400Hz 基频主导 + 弱二次谐）× 半正弦 swell；末段软 boom 保留
    （其宽带 crack 峰 < 哨音峰，归一化不压哨音）。一听即「引信燃灼尖啸 → 软爆」。~0.42s。机制等价 MC 苦力怕 idle
    （§9 原创；PLAN §9 区隔改名 stalker，非 MC 专名）。idle 软 boom 远弱于 explosion.wav（ambient 暗示，非真爆炸）。"""
    dur = 0.42
    n = int(SR * dur)
    rnd = random.Random(294076)
    out = [0.0] * n
    boom_t = dur * 0.62  # 引信段结束、boom 段起点
    for i in range(n):
        t = i / SR
        s = 0.0
        if t < boom_t:
            # 引信段：上升哨音（基频主导）× 半正弦 swell（删 hiss → 峰归一化不再被 spiky hiss 锁定）
            tn = t / boom_t
            f_w = 2500.0 + 900.0 * tn
            whistle = math.sin(2 * math.pi * f_w * t) + 0.2 * math.sin(2 * math.pi * 2 * f_w * t)
            swell = math.sin(math.pi * tn)
            s = whistle * swell
        else:
            # 末段软 boom（低频闷击 + 宽带爆裂，快速衰减；crack 峰 < 哨音峰 → 不压哨音）
            dt = t - boom_t
            body = (math.sin(2 * math.pi * 70.0 * dt) * 0.45 + math.sin(2 * math.pi * 110.0 * dt) * 0.25)
            crack = rnd.uniform(-1, 1) * 0.40 * env_exp(dt, 30.0)
            s = (body + crack) * env_exp(dt, 12.0) * 0.8
        out[i] = s
    return finalize(out)


def gen_mob_idle_spider():
    """敌对 Spider（机制等价蜘蛛）idle 愤怒虫嗡（t381 真修可辨）。

    真根因（实测定位）：旧版 swell 包络用 `tn = i / dur`（dur 是秒=0.40），swell=sin(π·i/0.40) 每样本推进
    π/0.40≈7.85 rad → **离散折叠（混叠）到 fs/4=11025Hz**，样本呈 `0,val,0,val` 交替 = 纯 11kHz 啸叫，完全淹没
    buzz（实测谱峰全在 10-12kHz、buzz 载波区≈0%）→ 听感「刺耳高频 / 像没声」。t366 只调 hiss 权重没碰这个真因。
    t381 真修：swell 改 `tn = i / n`（归一化 0..1 → 真半正弦包络 0→1→0，无混叠）+ 删 hiss 层（其 spiky 高 crest
    也会被峰值归一化放大），纯多谐 buzz（600/1200/1800/2400Hz 昆虫振翅多谐）× 深 42Hz AM（0.2..1.0）× 半正弦 swell。
    一听即「愤怒虫嗡」。~0.40s。机制等价 MC 蜘蛛 idle（§9 原创；PLAN §9 区隔改名 spider，非 MC 专名）。"""
    dur = 0.40
    n = int(SR * dur)
    out = [0.0] * n
    for i in range(n):
        tn = i / n  # 归一化 0..1（旧版 i/dur 是秒 → swell 混叠到 fs/4=11kHz，淹没 buzz）
        ts = i / SR
        # 多谐 buzz（昆虫振翅嗡；4 谐波丰富质感）× 深 42Hz AM（振翅嗡嗡 0.2..1.0）
        buzz = (math.sin(2 * math.pi * 600.0 * ts)
                + 0.5 * math.sin(2 * math.pi * 1200.0 * ts)
                + 0.3 * math.sin(2 * math.pi * 1800.0 * ts)
                + 0.15 * math.sin(2 * math.pi * 2400.0 * ts))
        am = 0.2 + 0.8 * math.sin(2 * math.pi * 42.0 * ts)
        swell = math.sin(math.pi * tn)
        out[i] = buzz * am * swell
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


def gen_thunder():
    """雷声（t386）：起始尖「咔」爆（宽带瞬态 ~25ms）+ 深沉低频轰鸣长尾（~2.6s 双正弦 38/62Hz body + FM 颤
    滚动）+ 缓慢衰减的低通噪声 rumble。机制等价 MC 远雷 / 雷击轰隆声（§9 原创；零 MC 资产）。比 explosion 更低、
    更长（雷声是低频长尾持续轰隆，而非爆炸的短爆裂）。由 World::lightningStruck → Main.qml 路由 playThunder 触发。"""
    dur = 2.6
    n = int(SR * dur)
    rnd = random.Random(386386)
    # 低通噪声 rumble 床（极低截止 → 持续低频隆隆，拟云层放电的空气振动）。
    bed_state = 0.0
    bed_a = 0.018
    bed = [0.0] * n
    for i in range(n):
        w = rnd.uniform(-1, 1)
        bed_state = bed_a * w + (1.0 - bed_a) * bed_state
        bed[i] = bed_state
    bed_inv = 1.0 / max(1e-6, max(abs(s) for s in bed))
    out = [0.0] * n
    for i in range(n):
        t = i / SR
        # 起始尖爆：极短宽带瞬态（拟雷击放电瞬间的「咔」爆，迅速衰减）。
        crack = rnd.uniform(-1, 1) * 0.55 * env_exp(t, 60.0)
        # 低频 body：双正弦（38/62Hz）+ FM 颤动（拟雷声滚动 / 多次回响叠加）。
        fm = 12.0 * math.sin(2 * math.pi * 4.5 * t)
        body = (math.sin(2 * math.pi * 38.0 * t + fm) * 0.40
                + math.sin(2 * math.pi * 62.0 * t) * 0.22)
        rumble = bed[i] * bed_inv * 0.28
        # 长衰减包络（雷声持续轰隆 ~2.6s 渐弱）+ 极短 attack（瞬态后立即满）。
        e = env_exp(t, 1.05)
        attack = min(1.0, t / 0.006)
        out[i] = (crack + body + rumble) * e * attack
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
                      ("thunder", gen_thunder),
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
