#include "worldclock.h"

#include <cmath>

WorldClock::WorldClock(QObject *parent) : QObject(parent)
{
    // 100ms 节奏推进相位（见 kTickMs 注释）。PreciseTimer 不必要——昼夜 lerp 对 10ms 抖动不敏感，
    // 用默认 CoarseTimer 反而更省电（与 60Hz 物理 tick 区隔）。
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &WorldClock::onTick);
    m_timer.start();
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
    // 新 elapsed = 旧 phase × 新周期；保持视觉连续。
    m_elapsedMs = qint64(m_phase * periodSecs() * 1000.f);
    emit debugFastChanged();
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
    // t87：游戏时间 tick（每 100ms 无条件发，携带本 tick 秒数）。熔炉等按时间推进的子系统消费。
    // 放在末尾发，保证消费者读到的 phase / skyLight 已是本 tick 最新值。
    emit ticked(qreal(kTickMs) / 1000.0);
}
