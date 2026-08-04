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
   - door_open.wav：开门 —— 木质嘎吱上扬 + 起始短扣响（门闩松脱）~0.22s（t152）。
   - door_close.wav：关门 —— 低频闷击（门框撞上）+ 末尾扣响（门闩扣合）~0.20s（t152）。
   - hurt.wav：玩家受伤（t177）—— 低频闷击 + 略不和谐中频（呻吟感）+ 起始宽带冲击 ~0.22s；不分
     材质（玩家自身受伤，非方块）。机制等价 MC 玩家受伤声（§9 原创）。PlayerState.damaged →
     AudioManager.playHurt 触发；连击 seek 重发不堆叠（同其他单件）。
   - mob_hurt.wav：生物受击（t248 专属受击音）—— 区别于玩家 hurt 的「闷哼」：更短促的软冲击 + 一段
     下扫中频「 creature yelp 」（拟生物被打一声叫），~0.18s。机制等价 MC 生物受击声（§9 原创，零 MC
     资产）。PlayerController.mobAttacked → AudioManager.playMobHurt 触发（替代旧复用 hurt.wav 的路径，
     spec「受击音换专属 mob 受伤声」）。
   - ambient_wind.wav：环境音 / 风声床（t177）—— 长循环风声（~8s，单极点低通白噪 + 双 LFO 慢颤
     调幅 + 首末 50ms 三角窗淡化保循环无缝）。机制等价 MC 的环境 / 风声氛围床（§9 原创）。
     AudioManager 用 ma_sound 设 looping，startAmbient/stopAmbient 控制开关（playing 态开 / 退菜单停），
     setAmbientLevel 据昼夜调强度（夜间更静谧）。
   - water_flow.wav：潺潺流水声（t269 重做：旧版合成的极慢 AM 起伏 + 单一低频 sustained body 听感像
     海浪，用户判「不像流水」；改潺潺流水 / 潺潺溪流）。长循环 ~8s，三层混合：低频水量床（单极点
     低通白噪）+ 中频「流水过石」沙沙 body（带通白噪）+ 高频细流 / 飞溅 hiss（高通白噪）；多重
     不规则 AM（3 个速率 ~0.7/1.1/2.3Hz + 随机相位 sin 叠加，非旧版潮汐式规则慢呼吸）；密集「咕嘟」
     气泡瞬态（每 0.08-0.25s 一个、随机基频 250-700Hz + 上扫拟冒泡上浮）—— 潺潺流水的标志性颗粒感。
     首末 50ms 三角窗淡化保循环无缝。机制等价 MC 近流水环境音（§9 原创，零 MC 资产）。AudioManager 用
     ma_sound 设 looping，startWaterFlow/stopWaterFlow 控开关（PlayerController 近流水 proximity 扫描驱动），
     setWaterFlowLevel 据玩家到最近流水格距离调音量（近强远弱）。
   - water_step.wav：水中走路声（t269）—— 玩家脚位在水中迈步时播（低频闷浊「咚」水下 body + 起始
     「咕嘟」气泡 plop 瞬态，拟脚入水搅动），~0.16s。机制等价 MC 水中走路声（§9 原创，零 MC 资产）。
     不分材质（水中听感统一闷浊），单件 clip；Main.qml onWalkPhaseChanged 据玩家 feetInWater 分流到
     playWaterStep（替代按材质的 playStep）。
   - mob_idle.wav / mob_idle_pig.wav / mob_idle_cow.wav / mob_idle_sheep.wav：生物环境 idle 叫声（t250
     牛叫/羊叫/猪叫，周期偶发；玩家听者范围内由 EntityManager 周期 emit mobAmbient 触发 →
     AudioManager.playMobAmbient 据 mobType 选播）。机制等价 MC 1.0 被动生物偶发 idle call（§9 原创，
     零 MC 资产；不照搬任何 MC 生物音效，仅机制对齐「周期 idle 叫声」）。
     - mob_idle（通用）：测试生物 / 未知子类的中性短 chirp（~0.18s）。
     - mob_idle_pig（猪哼）：低中频鼻音 grunt —— 双短爆发（拟「哼哼」两声），基频 ~190Hz + 二次谐波，
       ~0.32s。
     - mob_idle_cow（牛哞）：长低频下扫 moo —— 基频 ~165→105Hz 缓降 + 慢 attack + 轻颤音，~0.62s。
     - mob_idle_sheep（羊咩）：带 AM 颤音的 bleat —— ~380Hz 载波 × ~11Hz 振幅调制（拟「咩-咩」颤抖），
       ~0.45s。

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


def gen_door_open():
    """开门音（t152）：木质嘎吱上扬 + 起始短扣响（门闩松脱）~0.22s。
    机制等价 MC 木门开启声（原创程序合成，§9 法律：零 MC 资产）。门/活板门右键开合时由
    PlayerController::doorToggled(open=true) → AudioManager::playDoorOpen 触发。
    """
    dur = 0.22
    n = int(SR * dur)
    random.seed(310152)
    out = []
    for i in range(n):
        t = i / SR
        # 嘎吱：基频从 ~180 缓升到 ~320Hz（门轴转动摩擦的 pitch bend）
        f = 180.0 + 140.0 * (t / dur)
        creak = math.sin(2 * math.pi * f * t) * 0.5
        # 高频摩擦噪声（嘎吱质感）
        grit = random.uniform(-1, 1) * 0.18 * math.sin(2 * math.pi * 2400 * t)
        # 起始扣响（门闩松脱瞬态）—— 起点窄峰
        click_env = math.exp(-((t - 0.0) ** 2) / (2 * 0.010 ** 2))
        click = random.uniform(-1, 1) * 0.40 * click_env
        e = env_exp(t, 11.0)
        s = (creak + grit + click) * e
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_door_close():
    """关门音（t152）：低频闷击（门框撞上）+ 末尾扣响（门闩扣合）~0.20s。
    机制等价 MC 木门关闭声（原创程序合成，§9 法律：零 MC 资产）。doorToggled(open=false) 触发。
    """
    dur = 0.20
    n = int(SR * dur)
    random.seed(310153)
    out = []
    for i in range(n):
        t = i / SR
        # 低频闷击（门撞门框）+ 低频重量感
        thunk = math.sin(2 * math.pi * 95 * t) * 0.6
        body = math.sin(2 * math.pi * 55 * t) * 0.25
        # 末尾扣响（门闩扣合）—— 在 t≈dur*0.7 附近爆发的窄高斯峰
        center = dur * 0.7
        click_env = math.exp(-((t - center) ** 2) / (2 * 0.006 ** 2))
        latch = random.uniform(-1, 1) * 0.45 * click_env
        e = env_exp(t, 14.0)
        s = (thunk + body + latch) * e
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_hurt():
    """受伤音（t177）：玩家自身受伤声 —— 低频闷击（身体受击的「闷」）+ 略不和谐中频（呻吟 / 闷哼感，
    165Hz 略偏离基频泛音）+ 起始宽带冲击（瞬态「啪」），~0.22s。
    机制等价 MC 玩家受伤声（原创程序合成，§9 法律：零 MC 资产）。由 PlayerState.damaged → Main.qml 路由
    到 AudioManager.playHurt 触发（掉落伤害等；连击 seek 重发不堆叠，同其他单件）。
    """
    dur = 0.22
    n = int(SR * dur)
    random.seed(778877)
    out = []
    for i in range(n):
        t = i / SR
        # 低频冲击（身体受击的「闷」）
        thunk = math.sin(2 * math.pi * 90 * t) * 0.55
        # 略不和谐中频（呻吟 / 闷哼感；与 thunk 基频不成整数比 → 略紧绷）
        groan = math.sin(2 * math.pi * 165 * t) * 0.30
        # 起始宽带冲击（瞬态「啪」，快速衰减）
        impact = random.uniform(-1, 1) * 0.45 * env_exp(t, 45.0)
        e = env_exp(t, 12.0)
        s = (thunk + groan + impact) * e
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_mob_hurt():
    """生物受击音（t248 专属受击音）：与玩家 hurt.wav 区分 —— 更短促的软冲击（creature 被打的「啪」，
    但比玩家 hurt 轻柔、低频分量少）+ 一段下扫中频「 creature yelp / 短叫」（拟生物一声叫），~0.18s。
    机制等价 MC 生物受击声（原创程序合成，§9 法律：零 MC 资产；不照搬任何 MC 生物音效）。
    由 PlayerController::mobAttacked → Main.qml 路由到 AudioManager.playMobHurt 触发（替代旧复用 hurt.wav
    的路径）。与 hurt 单件同模式（连击 seek 重发不堆叠）。

    音色差异化（vs gen_hurt）：
      - hurt = 低频闷击 90Hz + 不和谐中频 165Hz groan + 宽带冲击 → 人味「闷哼 / 呃」。
      - mob_hurt = 更高的软 thunk 140Hz（小体型 creature 体腔）+ 下扫 yelp（500→300Hz，带 FM 颤，
        拟「吱 / 叫」）+ 较轻的摩擦瞬态 → 生物味「短叫」。整体更短（0.18 vs 0.22）、更亮。
    """
    dur = 0.18
    n = int(SR * dur)
    random.seed(812482)
    out = []
    for i in range(n):
        t = i / dur  # 归一化时间 0..1（yelp pitch bend 用）
        ts = i / SR
        # 软冲击：较高基频的 thunk（小 creature 体腔，比玩家 hurt 高），衰减快
        thunk = math.sin(2 * math.pi * 140.0 * ts) * 0.40
        # 下扫 yelp：频率从 ~500Hz 滑到 ~300Hz（被打一声短叫），加少量 FM 颤（拟声带颤）
        f_yelp = 500.0 - 200.0 * t
        fm = 30.0 * math.sin(2 * math.pi * 70.0 * ts)  # FM 颤（→ 略「颤音」质感）
        yelp = math.sin(2 * math.pi * f_yelp * ts + fm) * 0.38
        # 较轻的摩擦瞬态（生物皮毛 / 羽毛摩擦感，比 hurt 宽带冲击弱）
        grit = random.uniform(-1, 1) * 0.18 * math.sin(2 * math.pi * 2200.0 * ts)
        # 整体指数衰减；起始段加一个极短attack 包络让 yelp 起声稍顿（拟「被打 → 楞一下 → 叫」）
        e = env_exp(ts, 15.0)
        attack = min(1.0, ts / 0.012)  # 12ms attack（yelp 不在最尖的 0 点满幅）
        s = (thunk + yelp + grit) * e * attack
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_ambient_wind():
    """环境音 / 风声床（t177）：长循环风声 —— 单极点低通白噪（→ 低频起伏的「风」body）+ 双 LFO 慢速
    调幅（0.30Hz + 0.11Hz 叠加 → 自然不规则起伏）+ 首末 50ms 三角窗淡化（保循环无缝、无边界咔哒），
    ~8s。机制等价 MC 的环境 / 风声氛围床（原创程序合成，§9 法律：零 MC 资产）。
    AudioManager 用 ma_sound_set_looping(true) 设循环，startAmbient / stopAmbient 控制开关（playing 态开 /
    退菜单停），setAmbientLevel 据昼夜调强度（夜间更静谧）。整体幅度偏低（base_amp=0.13，背景氛围不抢前景）。
    """
    dur = 8.0
    n = int(SR * dur)
    random.seed(51501)
    # 单极点低通：state = a*state + (1-a)*w → 把白噪压成低频起伏（a 越接近 1 越平滑 / 低频）
    a = 0.985
    state = 0.0
    raw = [0.0] * n
    for i in range(n):
        w = random.uniform(-1, 1)
        state = a * state + (1.0 - a) * w
        raw[i] = state
    # 低通后幅度大幅衰减 → 归一化回 [-1,1]，再乘 base 调到背景级
    peak = max(1e-6, max(abs(s) for s in raw))
    inv = 1.0 / peak
    # 双 LFO 慢颤调幅（白天 / 风强的自然不规则起伏）
    lfo1 = 2 * math.pi * 0.30
    lfo2 = 2 * math.pi * 0.11
    fade_n = int(SR * 0.05)  # 50ms 首末淡化（循环无缝）
    base_amp = 0.13          # 风声整体偏低（背景氛围，不抢前景）
    out = []
    for i in range(n):
        t = i / SR
        lfo = 0.5 + 0.3 * math.sin(lfo1 * t) + 0.2 * math.sin(lfo2 * t)
        amp = base_amp * lfo
        if i < fade_n:
            amp *= i / fade_n
        elif i > n - fade_n:
            amp *= (n - 1 - i) / fade_n
        out.append(max(-1.0, min(1.0, raw[i] * inv * amp)))
    return out


def gen_water_flow():
    """潺潺流水声（t269 重做：旧版像海浪 → 改潺潺流水 / 溪流声）。

    听感诊断与对症（关键）：
      - **旧版像海浪的根因**：极慢 AM（0.27Hz / 0.13Hz = 周期 3.7s / 7.7s）+ 单极点低通产生的单一
        低频 sustained body → 听感是「潮起潮落的缓慢呼吸」= 海浪，而非流水。
      - **潺潺流水的听感要素**：中高频为主（细流过石的沙沙）、快速**不规则**起伏（非潮汐式规则慢呼吸）、
        密集小气泡 / 涟漪瞬态（潺潺的「颗粒感」）。

    合成（~8s 长循环，三层混合 + 密集气泡 + 不规则 AM）：
      1. 低频水量床（单极点低通白噪，幅度低 —— 仅作「水量」基础底，不抢前景）；
      2. 中频「流水过石」沙沙 body（带通白噪：低通去高频嘶嘶 + 差分高通去极低频）；
      3. 高频细流 / 飞溅 hiss（高通白噪，亮的细水颗粒）；
      4. 多重不规则 AM：3 个速率（~0.7 / 1.1 / 2.3Hz）+ 随机相位 sin 叠加 → 不规则起伏（非规则慢呼吸）；
      5. 密集「咕嘟」气泡瞬态（每 0.08-0.25s 一个、随机基频 250-700Hz + 上扫拟冒泡上浮）；
      6. 首末 50ms 三角窗淡化保循环无缝。

    机制等价 MC 近流水环境音（§9 原创，零 MC 资产）。AudioManager 用 ma_sound_set_looping(true) 设循环，
    startWaterFlow / stopWaterFlow 控开关（PlayerController 近流水 proximity 扫描驱动），setWaterFlowLevel
    据玩家到最近流水格距离调音量（近强远弱 → 0 时 caller stopWaterFlow）。整体幅度偏低（base_amp=0.16，
    背景氛围不抢前景；与环境风声同床共存、不互相盖过）。
    """
    dur = 8.0
    n = int(SR * dur)
    random.seed(70269)  # t269 重做新种子（新音色；确定性，同次运行字节一致）

    # ---- 1. 低频水量床（单极点低通）----
    bed_lp_a = 0.06
    bed_state = 0.0
    bed = [0.0] * n
    for i in range(n):
        w = random.uniform(-1, 1)
        bed_state = bed_lp_a * w + (1.0 - bed_lp_a) * bed_state
        bed[i] = bed_state
    bed_inv = 1.0 / max(1e-6, max(abs(s) for s in bed))

    # ---- 2. 中频 body（带通 = 低通 + 差分高通）----
    mid_lp_a = 0.25                      # 单极点低通（压高频嘶嘶，留中频沙沙）
    mid_lp_state = 0.0
    mid_raw = [0.0] * n
    for i in range(n):
        w = random.uniform(-1, 1)
        mid_lp_state = mid_lp_a * w + (1.0 - mid_lp_a) * mid_lp_state
        mid_raw[i] = mid_lp_state
    mid = [0.0] * n                       # 差分 = 高通（去极低频，留中频起伏）
    prev = 0.0
    for i in range(n):
        v = mid_raw[i] - prev
        prev = mid_raw[i]
        mid[i] = v
    mid_inv = 1.0 / max(1e-6, max(abs(s) for s in mid))

    # ---- 3. 高频 hiss（高通白噪：细流 / 飞溅的亮颗粒）----
    hp_prev_in = 0.0
    hp_prev_out = 0.0
    hp_a = 0.85                           # 越接近 1 越保留高频
    hiss = [0.0] * n
    for i in range(n):
        w = random.uniform(-1, 1)
        v = hp_a * (hp_prev_out + w - hp_prev_in)
        hp_prev_in = w
        hp_prev_out = v
        hiss[i] = v
    hiss_inv = 1.0 / max(1e-6, max(abs(s) for s in hiss))

    # ---- 4. 多重不规则 AM（3 速率 + 随机相位；非规则慢呼吸）----
    lfo_a = 2 * math.pi * 1.1
    lfo_b = 2 * math.pi * 2.3
    lfo_c = 2 * math.pi * 0.7
    ph_a = random.uniform(0, 2 * math.pi)
    ph_b = random.uniform(0, 2 * math.pi)
    ph_c = random.uniform(0, 2 * math.pi)

    # ---- 5. 密集气泡瞬态（每 0.08-0.25s 一个；潺潺的「咕嘟」颗粒感）----
    bubbles = []
    t_b = 0.05
    while t_b < dur - 0.05:
        bubbles.append((t_b,
                        random.uniform(250.0, 700.0),   # 气泡基频
                        random.uniform(0.06, 0.13),      # 振幅
                        random.uniform(0.008, 0.020),    # 高斯宽
                        random.uniform(0.5, 2.0)))       # 频率上扫系数（拟气泡上浮变调）
        t_b += random.uniform(0.08, 0.25)

    fade_n = int(SR * 0.05)
    base_amp = 0.16
    out = []
    for i in range(n):
        t = i / SR
        # 不规则 AM（均值 ~0.6、保底 0.05 不归零 → 水量持续）
        am = 0.4 + 0.2 * math.sin(lfo_a * t + ph_a) \
                 + 0.2 * math.sin(lfo_b * t + ph_b) \
                 + 0.2 * math.sin(lfo_c * t + ph_c)
        if am < 0.05:
            am = 0.05
        # 三层混合：bed（低频水量）+ mid（中频沙沙主体）+ hiss（高频细流）
        s = (bed[i] * bed_inv * 0.30
             + mid[i] * mid_inv * 0.55
             + hiss[i] * hiss_inv * 0.25) * base_amp * am
        # 叠加气泡瞬态（窄高斯包络短正弦 + 频率上扫）
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
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_water_step():
    """水中走路声（t269）：玩家脚位在水中迈步时播 —— 低频闷浊「咚」（水下 muffling）+ 起始「咕嘟」气泡
    plop 瞬态（脚入水搅动），~0.16s。机制等价 MC 水中走路声（§9 原创，零 MC 资产）。AudioManager.playWaterStep
    触发（Main.qml onWalkPhaseChanged 据玩家 feetInWater 分流，替代按材质的 playStep）。

    vs 普通 step_X（按材质）：水下步声不按材质（水中听感统一闷浊），单件 clip；音量在 AudioManager 内
    略低于普通 step（水下传播衰减 + 不抢水流声前景）。
    """
    dur = 0.16
    n = int(SR * dur)
    random.seed(269269)
    out = []
    lp_state = 0.0
    lp_a = 0.12  # 低通系数（闷浊搅动感）
    for i in range(n):
        t = i / SR
        # 低频 thunk（脚踩水的「咚」，水下 body）
        thunk = math.sin(2 * math.pi * 90.0 * t) * 0.45
        # 低通宽带噪声（水下的闷浊搅动感）
        w = random.uniform(-1, 1)
        lp_state = lp_a * w + (1.0 - lp_a) * lp_state
        murk = lp_state * 0.35
        # 起始「咕嘟」气泡 plop（脚入水瞬态）：~260Hz 起的上扫短正弦，窄高斯包络在 t≈0
        plop_env = math.exp(-((t - 0.0) ** 2) / (2 * 0.012 ** 2))
        plop = math.sin(2 * math.pi * (260.0 + 200.0 * t) * t) * 0.35 * plop_env
        e = env_exp(t, 16.0)
        s = (thunk + murk + plop) * e
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_mob_idle_generic():
    """通用生物 idle 叫声（t250）：中性短 chirp —— 单音基频 ~420Hz 正弦 + 二次谐波 + 起始软 attack +
    快指数衰减，~0.18s。供 MobTest（测试生物）/ 未知 mobType 兜底（机制等价 MC 生物偶发 idle call；
    §9 原创，零 MC 资产）。EntityManager 周期 emit mobAmbient(mobType=0) → AudioManager.playMobAmbient 触发。
    """
    dur = 0.18
    n = int(SR * dur)
    random.seed(250070)
    out = []
    for i in range(n):
        t = i / SR
        tone = math.sin(2 * math.pi * 420.0 * t) * 0.50
        harm = math.sin(2 * math.pi * 840.0 * t) * 0.15        # 二次谐波（让 chirp 不至于纯音过空）
        grit = random.uniform(-1, 1) * 0.12 * math.sin(2 * math.pi * 2600.0 * t)  # 轻摩擦瞬态
        attack = min(1.0, t / 0.010)                            # 10ms attack（起声不刺）
        e = env_exp(t, 16.0)
        s = (tone + harm + grit) * e * attack
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_mob_idle_pig():
    """猪哼 idle 叫声（t250）：低中频鼻音 grunt —— 两段短爆发（拟「哼哼」），每段基频 ~190Hz 正弦 +
    二次谐波（鼻音色彩）+ 弱宽带 grit，各 ~0.11s、间隔 ~0.05s，~0.32s。机制等价 MC 猪偶发 grunt
    （§9 原创，零 MC 资产；不照搬任何 MC 生物音效）。mobAmbient(mobType=1) → playMobAmbient 触发。
    """
    dur = 0.32
    n = int(SR * dur)
    random.seed(250071)
    # 两段爆发的中心（归一化时间 [0,1]）：第一段 0.15、第二段 0.65。
    bursts = [0.15, 0.65]
    out = []
    for i in range(n):
        t = i / SR
        # 累加两段窄高斯包络（各 ~0.11s 宽）
        env = 0.0
        for b in bursts:
            env += math.exp(-((t - b * dur) ** 2) / (2 * 0.030 ** 2))
        env = min(1.0, env)
        tone = math.sin(2 * math.pi * 190.0 * t) * 0.55
        harm = math.sin(2 * math.pi * 380.0 * t) * 0.20          # 二次谐波（鼻音色彩）
        grit = random.uniform(-1, 1) * 0.16                      # 弱宽带 grit（哼的气流感）
        e = env
        attack = min(1.0, t / 0.008)
        s = (tone + harm + grit) * e * attack * 0.9
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_mob_idle_cow():
    """牛哞 idle 叫声（t250）：长低频下扫 moo —— 基频从 ~165Hz 缓降到 ~105Hz（哞的下沉感）+ 二次谐波 +
    慢 attack（起声渐入）+ 轻颤音（~6Hz FM）+ 末尾慢衰减，~0.62s。机制等价 MC 牛偶发 moo（§9 原创，
    零 MC 资产）。mobAmbient(mobType=2) → playMobAmbient 触发。
    """
    dur = 0.62
    n = int(SR * dur)
    random.seed(250072)
    out = []
    for i in range(n):
        t = i / dur      # 归一化 0..1（pitch bend 用）
        ts = i / SR
        # 基频下扫 165→105Hz（哞的下沉轮廓）
        f0 = 165.0 - 60.0 * t
        fm = 6.0 * math.sin(2 * math.pi * 6.0 * ts)             # ~6Hz FM 颤（声带轻颤）
        tone = math.sin(2 * math.pi * f0 * ts + fm) * 0.50
        harm = math.sin(2 * math.pi * 2.0 * f0 * ts) * 0.16      # 二次谐波（mellow 的体腔感）
        body = math.sin(2 * math.pi * 70.0 * ts) * 0.18          # 低频重量（大体型）
        grit = random.uniform(-1, 1) * 0.08                      # 极弱气声
        # 慢 attack（0.08s 起声渐入）+ 慢衰减
        attack = min(1.0, ts / 0.08)
        e = attack * env_exp(ts, 3.5)
        s = (tone + harm + body + grit) * e
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_mob_idle_sheep():
    """羊咩 idle 叫声（t250）：带 AM 颤音的 bleat —— ~380Hz 载波 × ~11Hz 振幅调制（拟「咩-咩」颤抖）+
    二次谐波 + 中 attack + 中衰减，~0.45s。机制等价 MC 羊偶发 baa（§9 原创，零 MC 资产）。
    mobAmbient(mobType=3) → playMobAmbient 触发。
    """
    dur = 0.45
    n = int(SR * dur)
    random.seed(250073)
    out = []
    for i in range(n):
        t = i / SR
        ts = i / SR
        carrier = math.sin(2 * math.pi * 380.0 * ts) * 0.48
        harm = math.sin(2 * math.pi * 760.0 * ts) * 0.14          # 二次谐波
        grit = random.uniform(-1, 1) * 0.10
        # ~11Hz AM 颤（0.5±0.5 → 0..1 包络，让载波忽强忽弱 = 咩的颤抖）
        trem = 0.5 + 0.5 * math.sin(2 * math.pi * 11.0 * ts)
        attack = min(1.0, ts / 0.020)
        e = attack * env_exp(ts, 5.5)
        s = (carrier + harm + grit) * e * trem * 0.85
        out.append(max(-1.0, min(1.0, s)))
    return out


def gen_explosion():
    """爆炸音（t284 Stalker/苦力怕自爆）：低频闷击（爆炸的「轰」body）+ 起始宽带爆裂瞬态（冲击破空的「砰」）
    + 较长尾音（低频余响 + 衰减噪声，拟爆炸后的轰鸣回响），~0.5s。机制等价 MC 爆炸声（原创程序合成，§9 法律：
    零 MC 资产）。由 EntityManager::explosion → AudioManager.playExplosion 触发（爆炸的单一音/视入口）。

    音色设计（vs gen_hurt / gen_door_close）：
      - 低频 body 极重（55Hz + 80Hz 双正弦 → 大能量低频「轰」；爆炸比关门/受伤的体量大得多）；
      - 起始宽带爆裂（白噪 × 极快衰减 → 破空「砰」瞬态，比 hurt 的冲击强且宽）；
      - 中频咆哮（~120Hz 略不和谐 + 慢 FM 颤 → 爆炸气浪的「吼」感）；
      - 尾音较长（衰减常数 5.0 → ~0.5s 渐弱，比 hurt 0.22s / door 0.20s 长得多 = 爆炸的余响）。
    """
    dur = 0.50
    n = int(SR * dur)
    random.seed(284284)
    out = []
    lp_state = 0.0  # 低通态（尾音低频余响用）
    lp_a = 0.08
    for i in range(n):
        t = i / SR
        # 低频 body：双正弦（55 + 80Hz）→ 厚重「轰」
        body = (math.sin(2 * math.pi * 55.0 * t) * 0.45
                + math.sin(2 * math.pi * 80.0 * t) * 0.30)
        # 中频咆哮：~120Hz + 慢 FM 颤（气浪的「吼」）
        fm = 18.0 * math.sin(2 * math.pi * 9.0 * t)
        roar = math.sin(2 * math.pi * 120.0 * t + fm) * 0.22
        # 起始宽带爆裂瞬态（破空「砰」）—— 极快衰减
        crack = random.uniform(-1, 1) * 0.55 * env_exp(t, 30.0)
        # 尾音低频余响：单极点低通白噪 → 低频起伏的「轰隆」尾
        w = random.uniform(-1, 1)
        lp_state = lp_a * w + (1.0 - lp_a) * lp_state
        rumble = lp_state * 0.30
        # 整体指数衰减（衰减常数 5.0 → ~0.5s 渐弱；爆炸余响长）
        e = env_exp(t, 5.0)
        # 起始极短 attack（爆炸瞬态起声不顿）
        attack = min(1.0, t / 0.004)
        s = (body + roar + crack + rumble) * e * attack
        out.append(max(-1.0, min(1.0, s)))
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
    # 单件音：place（保留）+ pickup + door_open / door_close（t152 门/活板门开关声）
    # + hurt / mob_hurt（t177 玩家受伤 + t248 生物受击专属）+ ambient_wind（t177 环境风声床）
    # + water_flow（t223 近流水水流声床）
    for name, gen in [("place", gen_place), ("pickup", gen_pickup),
                      ("door_open", gen_door_open), ("door_close", gen_door_close),
                      ("hurt", gen_hurt), ("mob_hurt", gen_mob_hurt),
                      ("explosion", gen_explosion),
                      ("ambient_wind", gen_ambient_wind),
                      ("water_flow", gen_water_flow),
                      ("water_step", gen_water_step),
                      ("mob_idle", gen_mob_idle_generic),
                      ("mob_idle_pig", gen_mob_idle_pig),
                      ("mob_idle_cow", gen_mob_idle_cow),
                      ("mob_idle_sheep", gen_mob_idle_sheep)]:
        samples = gen()
        path = out_dir / f"{name}.wav"
        write_wav(path, samples)
        print(f"wrote {path} ({len(samples)} frames, {len(samples)/SR:.2f}s)")


if __name__ == "__main__":
    main()
