#ifndef WORLDCLOCK_H
#define WORLDCLOCK_H

#include <QObject>
#include <QTimer>
#include <QtQml/qqml.h>

// 世界时钟（Game 层）：MC 1.0 风格昼夜节律的**单一权威时间源**。
//
// PLAN §2 不变量 H：昼夜 = **天光亮度乘子** lerp（**非**旋转方向光）。本类只暴露**纯函数**
// 时间→亮度的输入（dayPhase 0..1 循环 + 派生 skyLight 乘子），呈现层（Main.qml 的
// SceneEnvironment.clearColor 与 DirectionalLight.brightness）据此 lerp 昼↔夜。**绝不**在内部
// 旋转任何光源方向（DirectionalLight 的 eulerRotation 由 QML 固定不变）。
//
// 节律：
//   - 默认 ~20 分钟（1200s）一周期（对齐 MC 1.0；dev-spec t09）。
//   - 调试加速：toggleDebugFast() 把周期压到 ~30s（便于肉眼快速看一圈昼夜），生产节律常量不变。
//
// dayPhase 约定（与 skyLight 纯函数配套）：
//   - 0.0 = 正午（skyLight=1，最亮） → 0.25 = 黄昏 → 0.5 = 子夜（skyLight=0，最暗）
//   - 0.75 = 黎明 → 1.0 回正午。循环连续、无跳变。
//   skyLight = 0.5 + 0.5*cos(2π·phase) ∈ [0,1]；MC「夜间仍可见」语义由 QML 端的「夜间 floor」
//   承担（clearColor 最低 #0b1026、brightness 最低 0.25，非纯黑），故 skyLight 自身可取全 [0,1]。
//
// 暴露给 QML（呈现层只读消费）：
//   - dayPhase   0..1 循环相位（NOTIFY dayPhaseChanged）
//   - skyLight   [0,1] 天光乘子（纯函数 dayPhase→亮度；同 NOTIFY 派生）
//   - debugFast  当前是否处于调试加速周期（HUD 可显提示）
//
// 分层（PLAN §2）：Game 层时间源，只依赖 Core（Qt）。不依赖 Renderer/World。呈现层绑定消费、
//   绝不反向写入时间（无 setTime）—— 时间单向流逝、纯函数派生亮度。
class WorldClock : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldClock)
    Q_PROPERTY(float dayPhase READ dayPhase NOTIFY dayPhaseChanged)
    Q_PROPERTY(float skyLight READ skyLight NOTIFY dayPhaseChanged)
    Q_PROPERTY(bool debugFast READ debugFast NOTIFY debugFastChanged)

public:
    explicit WorldClock(QObject *parent = nullptr);

    float dayPhase() const { return m_phase; }
    float skyLight() const;     // 派生：天光乘子（纯函数 dayPhase → 亮度；PLAN §2-H）
    bool debugFast() const { return m_debugFast; }

    // 调试加速键（呈现层按键调）：切 ~30s 周期，便于肉眼快速看一圈昼夜。仅调试便利、
    // 不影响生产节律常量（kDaySecs 不变；切换的是运行期所用周期）。
    Q_INVOKABLE void toggleDebugFast();

signals:
    void dayPhaseChanged();   // 每 tick 发（phase 推进）；skyLight 派生于 phase，同信号刷新
    void debugFastChanged();
    // t87 游戏时间 tick：每 kTickMs（100ms）发一次，携带本 tick 推进的秒数（恒 kTickMs/1000=0.1）。
    // 用途：熔炉冶炼等「按游戏时间推进」的系统据此 tick（FurnaceUI.tick）。与昼夜相位解耦——本信号
    // 每 tick 无条件发（不似 dayPhaseChanged 仅 phase 真变才发），保证冶炼节律稳定 10Hz。
    // 单一时间权威：所有按时间推进的子系统都消费本信号，不在 QML 各自起 Timer（PLAN §2：时钟单一）。
    void ticked(qreal deltaSecs);

private:
    void onTick();
    // 周期（秒）：默认 ~20 分钟；调试加速切 ~30s（dev-spec 验收要求）。
    float periodSecs() const;

    // 100ms（10Hz）：昼夜 lerp 缓慢，10Hz 已视觉平滑且不无谓刷爆 QML 绑定（与 PlayerController
    // 的 16ms 物理 tick 解耦——时钟不需要 60Hz）。
    static constexpr int kTickMs = 100;
    static constexpr float kDaySecs  = 1200.f; // ~20 分钟一周期（MC 1.0；dev-spec）
    static constexpr float kFastSecs = 30.f;   // 调试加速周期（dev-spec）

    float m_phase = 0.f;      // 0..1 循环相位（由 m_elapsedMs 派生，避免浮点累积漂移）
    qint64 m_elapsedMs = 0;   // 累计已流逝毫秒（phase = (elapsed mod period) / period）
    bool m_debugFast = false;
    QTimer m_timer;
};

#endif // WORLDCLOCK_H
