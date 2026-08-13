#include "worldclock.h"

#include <cmath>

WorldClock::WorldClock(QObject *parent) : QObject(parent)
{
    // 100ms 节奏推进相位（见 kTickMs 注释）。PreciseTimer 不必要——昼夜 lerp 对 10ms 抖动不敏感，
    // 用默认 CoarseTimer 反而更省电（与 60Hz 物理 tick 区隔）。
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &WorldClock::onTick);
    m_timer.start();
    // t123：首帧前用默认太阳态（天顶正午）兜底；首 tick 即据 phase 量化更新 + 发 sunChanged。
    recomputeSun(0.f);
}

float WorldClock::periodSecs() const
{
    return m_debugFast ? kFastSecs : kDaySecs;
}

// 天光乘子（PLAN §2-H）：纯函数 dayPhase → [0,1]。余弦曲线保证 noon=1、midnight=0、
// dawn/dusk=0.5 的平滑过渡（无跳变）。QML 端再据 floor（#0b1026 / 0.25）做昼↔夜 lerp。
float WorldClock::skyLight() const
{
    // 2π·phase：phase 0..1 映射到一整个余弦周期。
    const float c = 0.5f + 0.5f * std::cos(2.f * float(M_PI) * m_phase);
    return std::clamp(c, 0.f, 1.f);
}

void WorldClock::toggleDebugFast()
{
    m_debugFast = !m_debugFast;
    // 切周期时按比例调整 elapsed，使 phase 不突变（否则 1200s→30s 会让画面瞬间昼夜翻转）。
    // 新 elapsed = (dayCount + phase) × 新周期：同时保 phase（昼夜亮度连续）与 dayCount
    //   （t389 月相不跳 —— 否则 elapsed 仅按小数 phase 重建会让 dayCount 归 0、月相回满）。
    const qint64 day = m_dayCount >= 0 ? m_dayCount : 0;
    m_elapsedMs = qint64((float(day) + m_phase) * periodSecs() * 1000.f);
    emit debugFastChanged();
}

// t388 睡觉跳清晨：把时间向前快进到下一黎明（phase 0.75）。只**加** m_elapsedMs（PLAN §2-H 时间单向）。
//   isNight 下当前 phase ∈ (0.25, 0.75)，故通常 cur < target 直接补到 0.75；cur 已过 0.75（防御）则绕到
//   下一周期 0.75。即时重派生 phase + 太阳方向 + emit（同 onTick 派生路径，但即时而非等下一 tick）。
void WorldClock::skipToDawn()
{
    const float target = 0.75f;
    const float cur = m_phase;
    const float addFrac = (cur <= target) ? (target - cur) : ((1.0f - cur) + target);
    m_elapsedMs += qint64(addFrac * periodSecs() * 1000.f);
    // 重派生 phase（与 onTick 同取模路径，防浮点漂移）。
    const float periodMs = periodSecs() * 1000.f;
    qint64 wrapped = m_elapsedMs;
    if (periodMs > 0.f) {
        const qint64 p = qint64(periodMs);
        if (p > 0) wrapped = m_elapsedMs % p;
    }
    m_phase = (periodMs > 0.f) ? float(wrapped) / periodMs : 0.f;
    emit dayPhaseChanged(); // skyLight / isNight 派生于 phase，同信号刷新（QML 昼夜亮度即刻跳清晨）
    // 太阳方向即时跳到清晨量化位（mesher 据此重建顶点光 / 影；同 onTick 的量化逻辑）。
    if (kSunSteps > 0) {
        int step = int(std::floor(m_phase * float(kSunSteps)));
        if (step >= kSunSteps) step = kSunSteps - 1;
        m_sunStep = step;
        recomputeSun(float(step) / float(kSunSteps));
        emit sunChanged();
    }
}

// misc 二轮 `/time` 指令内部统一应用：把目标 phase + day 写进 m_elapsedMs，重派生并 emit 全套信号
//   （phase / day / moonPhase / 太阳量化步）。由 setPhase/setDay/addPhase 共用。允许任意设/加（含回退）。
void WorldClock::applyTime(float phase, qint64 day)
{
    const float periodMs = periodSecs() * 1000.f;
    phase = phase - std::floor(phase);            // 归一化到 [0,1)
    if (day < 0) day = 0;
    m_elapsedMs = qint64((float(day) + phase) * periodMs);
    m_phase = phase;
    m_dayCount = day;
    emit dayPhaseChanged();
    emit dayChanged();
    const int moon = int(((day % 8) + 8) % 8);
    if (moon != m_moonPhase) { m_moonPhase = moon; emit moonPhaseChanged(); }
    if (kSunSteps > 0) {
        int step = int(std::floor(phase * float(kSunSteps)));
        if (step >= kSunSteps) step = kSunSteps - 1;
        m_sunStep = step;
        recomputeSun(float(step) / float(kSunSteps));
        emit sunChanged();
    }
}

// misc 二轮 `/time set <phase>`：设昼夜相位，保当前 dayCount（月相不变）。
void WorldClock::setPhase(float phase)
{
    applyTime(phase, m_dayCount >= 0 ? m_dayCount : 0);
}

// misc 二轮 `/time set <n>d`：设第几天（影响月相），保当前 phase。
void WorldClock::setDay(int day)
{
    applyTime(m_phase, day);
}

// misc 二轮 `/time add <n>`：当前 phase 加 delta（可跨天 / 可回退），dayCount 随跨天递增。
void WorldClock::addPhase(float delta)
{
    applyTime(m_phase + delta, m_dayCount >= 0 ? m_dayCount : 0);
}

// t155 编辑活跃期反馈：呈现层 QML 在 World::worldChanged 时调本方法，把「最近编辑」时间戳记为当前
//   m_elapsedMs（与 onTick 跨步判定同基准）。onTick 据此判 editingActive() → 编辑活跃期跳过太阳跨步。
//   仅记时间戳，不改时间本身（无 setTime，PLAN §2 时间单向流逝）。
void WorldClock::noteEditActivity()
{
    m_lastEditElapsedMs = m_elapsedMs;
}

// t155 编辑活跃期判定：「最近编辑至今 < kEditCooldownMs」即活跃。读 m_elapsedMs（onTick 跨步判定时
//   已是本 tick 推进后的当前值）。初值哨兵使启动期判不活跃（见 m_lastEditElapsedMs 注释）。
bool WorldClock::editingActive() const
{
    return m_elapsedMs - m_lastEditElapsedMs < qint64(kEditCooldownMs);
}

// t123：由相位算太阳位置。轨道角 psi = 2π·phase（0=正午）：
//   elev = maxElev·cos(psi)   —— 正午 +maxElev（最高）、黄昏/黎明 0°、子夜 -maxElev（地平下）。
//   水平绕转角 = psi（1 周期 1 圈）：正午 sunDir 朝 +Z+Y（约定 +Z=南）、黄昏 +X、子夜 -Z-下、黎明 -X。
//   sunDir = (cos(elev)·sin(psi), sin(elev), cos(elev)·cos(psi))，单位向量指向太阳。
//   方位角 azim（度）= atan2(sx, sz) 映射 [0,360)，仅供 F3/调试展示（不参与光照计算）。
// PLAN §2-H 不变量：昼夜是「亮度乘子 lerp」非旋转方向光——此处算的 sunDir 仅用于顶点光的
//   方向调制（mesher 烘 faceNormal·sunDir 进 color.rgb），**不**旋转 QtQuick3D 的 DirectionalLight。
void WorldClock::recomputeSun(float phase)
{
    const float psi = 2.f * float(M_PI) * phase;
    const float elevDeg = kSunMaxElevDeg * std::cos(psi);
    const float er = elevDeg * float(M_PI) / 180.f;
    const float ce = std::cos(er);   // cos(elev) = 太阳水平投影长
    const float se = std::sin(er);   // sin(elev) = 太阳垂直分量（正=地平线上）
    m_sunDir = QVector3D(ce * std::sin(psi), se, ce * std::cos(psi));
    m_sunElevDeg = elevDeg;
    // 方位角（度）：atan2(x, z) → [0,360)。+Z=0°（南）、+X=90°（西，黄昏）、-Z=180°（北，子夜）、-X=270°（东，黎明）。
    float azim = std::atan2(m_sunDir.x(), m_sunDir.z()) * 180.f / float(M_PI);
    if (azim < 0.f) azim += 360.f;
    m_sunAzimDeg = azim;
}

void WorldClock::onTick()
{
    m_elapsedMs += kTickMs;
    const float periodMs = periodSecs() * 1000.f;
    // 浮点取模：用 qint64 计整数圈数再减回，避免 fmod 精度漂移（千小时运行后仍稳）。
    qint64 wrapped = m_elapsedMs;
    if (periodMs > 0.f) {
        const qint64 p = qint64(periodMs);
        if (p > 0) wrapped = m_elapsedMs % p;
    }
    const float newPhase = (periodMs > 0.f) ? float(wrapped) / periodMs : 0.f;
    if (newPhase != m_phase) {
        m_phase = newPhase;
        emit dayPhaseChanged(); // skyLight 派生于 phase，QML 同信号刷新
    }
    // t389 月相：跨「完整一天」(elapsed 跨过 period 整数倍) 时前进一阶、8 周期轮回。
    //   dayCount = floor(elapsed/period)（用 wrapped 除外的累计 elapsed，而非 wrapped——wrapped 已抹掉
    //   整圈，会把「第 N 天」误判为第 0 天；月相应随绝对流逝天数单调推进）。仅跨天时 emit（非每 tick）。
    //   toggleDebugFast 会按比例改 elapsed 保 phase 连续 → dayCount 不突变 → 无须此处特殊处理。
    if (periodMs > 0.f) {
        const qint64 day = m_elapsedMs / qint64(periodMs);
        if (day != m_dayCount) {
            m_dayCount = day;
            emit dayChanged();   // t464：day count 每跨 1 天刷新（F3 叠层读；独立于 moonPhase 的 8 天周期）
            const int phase = int(((day % 8) + 8) % 8);   // %8 轮回；+8 防 day 为负（哨兵初值 -1 → 0）
            if (phase != m_moonPhase) {
                m_moonPhase = phase;
                emit moonPhaseChanged();   // QML 月 Model 据 moonPhase 切 moon_<phase>.png
            }
        }
    }
    // t123：太阳量化步进。按 newPhase 量化到 kSunSteps 步；跨步才重算 sunDir/elev/azim + 发
    //   sunChanged（mesher 绑此信号重建顶点光）。量化把 mesh 重建从 10Hz 降到 ~kSunSteps/period Hz。
    // t155 编辑活跃期节流：玩家近 kEditCooldownMs 内有 setBlock（编辑活跃）→ 跳过本 tick 的太阳跨步
    //   （不重算 sunDir / 不发 sunChanged → mesher 不全量重建 18 mesh，避免与编辑即时重建争帧）。编辑
    //   冷却过后下一 idle tick：step 已按 newPhase 推进数步，`step != m_sunStep` 成立 → 一次 catch-up
    //   跨步（太阳方向一步跳到当前量化位，影 / 天空太阳随之刷新）。昼夜亮度（skyLight）不受此节流影响
    //   —— 仍随 dayPhaseChanged 每 tick 平滑刷（clearColor / DirectionalLight / baseColor 无冻结）。
    //   编辑信号由呈现层 QML 经 World::worldChanged → noteEditActivity() 桥接（Game 层不依赖 World）。
    if (kSunSteps > 0) {
        int step = int(std::floor(newPhase * float(kSunSteps)));
        if (step >= kSunSteps) step = kSunSteps - 1; // phase==1.0 边界保护（floor 可能取到 kSunSteps）
        if (step != m_sunStep && !editingActive()) {
            m_sunStep = step;
            recomputeSun(float(step) / float(kSunSteps));
            emit sunChanged();
        }
    }
    // t87：游戏时间 tick（每 100ms 无条件发，携带本 tick 秒数）。熔炉等按时间推进的子系统消费。
    // 放在末尾发，保证消费者读到的 phase / skyLight / sunDir 已是本 tick 最新值。
    emit ticked(qreal(kTickMs) / 1000.0);
}
