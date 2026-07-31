#ifndef WORLDCLOCK_H
#define WORLDCLOCK_H

#include <QObject>
#include <QTimer>
#include <QVector3D>
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
// t123 动态太阳光照（方案②顶点光动态太阳）：
//   太阳方向 / 仰角 / 方位角随 dayPhase 派生（sunDir/sunElevation/sunAzimuth，Q_PROPERTY）。
//   mesher（ChunkGeometry）据此把 faceNormal·sunDir 烘进顶点 color.rgb，配合 heightmap 列投影
//   产生「朝太阳面亮 / 背阳与投影阴影暗」的方向感与阴影。**仍非旋转方向光**——QtQuick3D 的
//   DirectionalLight eulerRotation 固定不变（PLAN §2-H），sunDir 只调制顶点色。
//
//   量化步进（避免每 100ms tick 全量重建 mesh）：sunDir/elev/azim 按 kSunSteps 离散步更新，
//   NOTIFY=sunChanged 仅跨步时发 → mesher 绑 sunChanged 重建顶点光（正常 ~每 16s 一步、
//   调试 ~每 0.4s 一步）。skyLight 不走此信号，仍随 dayPhaseChanged 每 tick 平滑刷
//   （clearColor / DirectionalLight / 地形 baseColor 的昼夜过渡无跳变）。
//
// 暴露给 QML（呈现层只读消费）：
//   - dayPhase   0..1 循环相位（NOTIFY dayPhaseChanged）
//   - skyLight   [0,1] 天光乘子（纯函数 dayPhase→亮度；同 NOTIFY 派生）
//   - debugFast  当前是否处于调试加速周期（HUD 可显提示）
//   - sunDir     单位向量指向太阳（NOTIFY sunChanged，量化步进）
//   - sunElevation / sunAzimuth  太阳仰角 / 方位角（度；NOTIFY sunChanged）
//
// 分层（PLAN §2）：Game 层时间源，只依赖 Core（Qt）。不依赖 Renderer/World。呈现层绑定消费、
//   绝不反向写入时间（无 setTime）—— 时间单向流逝、纯函数派生亮度与太阳方向。
class WorldClock : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WorldClock)
    Q_PROPERTY(float dayPhase READ dayPhase NOTIFY dayPhaseChanged)
    Q_PROPERTY(float skyLight READ skyLight NOTIFY dayPhaseChanged)
    Q_PROPERTY(bool debugFast READ debugFast NOTIFY debugFastChanged)
    Q_PROPERTY(QVector3D sunDir READ sunDir NOTIFY sunChanged)
    Q_PROPERTY(float sunElevation READ sunElevation NOTIFY sunChanged)
    Q_PROPERTY(float sunAzimuth READ sunAzimuth NOTIFY sunChanged)

public:
    explicit WorldClock(QObject *parent = nullptr);

    float dayPhase() const { return m_phase; }
    float skyLight() const;     // 派生：天光乘子（纯函数 dayPhase → 亮度；PLAN §2-H）
    bool debugFast() const { return m_debugFast; }

    // t123 太阳方向（量化步进更新；mesher 据此烘顶点光）。
    QVector3D sunDir() const { return m_sunDir; }
    float sunElevation() const { return m_sunElevDeg; }
    float sunAzimuth() const { return m_sunAzimDeg; }

    // 调试加速键（呈现层按键调）：切 ~30s 周期，便于肉眼快速看一圈昼夜。仅调试便利、
    // 不影响生产节律常量（kDaySecs 不变；切换的是运行期所用周期）。
    Q_INVOKABLE void toggleDebugFast();

signals:
    void dayPhaseChanged();   // 每 tick 发（phase 推进）；skyLight 派生于 phase，同信号刷新
    void debugFastChanged();
    // t123：太阳量化步进跨步时发（驱动 mesher 重建顶点光）。skyLight 不绑此信号（仍随
    //   dayPhaseChanged 平滑刷）。
    void sunChanged();
    // t87 游戏时间 tick：每 kTickMs（100ms）发一次，携带本 tick 推进的秒数（恒 kTickMs/1000=0.1）。
    // 用途：熔炉冶炼等「按游戏时间推进」的系统据此 tick（FurnaceUI.tick）。与昼夜相位解耦——本信号
    // 每 tick 无条件发（不似 dayPhaseChanged 仅 phase 真变才发），保证冶炼节律稳定 10Hz。
    // 单一时间权威：所有按时间推进的子系统都消费本信号，不在 QML 各自起 Timer（PLAN §2：时钟单一）。
    void ticked(qreal deltaSecs);

private:
    void onTick();
    // 周期（秒）：默认 ~20 分钟；调试加速切 ~30s（dev-spec 验收要求）。
    float periodSecs() const;
    // t123：由「量化后的相位」算太阳 elev/azim/dir，写进成员。仅跨步时调（onTick 内判定）。
    void recomputeSun(float quantizedPhase);

    // 100ms（10Hz）：昼夜 lerp 缓慢，10Hz 已视觉平滑且不无谓刷爆 QML 绑定（与 PlayerController
    // 的 16ms 物理 tick 解耦——时钟不需要 60Hz）。
    static constexpr int kTickMs = 100;
    static constexpr float kDaySecs  = 1200.f; // ~20 分钟一周期（MC 1.0；dev-spec）
    static constexpr float kFastSecs = 30.f;   // 调试加速周期（dev-spec）
    // t123 太阳轨道常量：
    //   kSunMaxElevDeg：正午太阳最高仰角（度）。sin(50°)≈0.766 为「满日照」基准（mesher 据此
    //     归一 sunDir.y → intensity∈[0,1]）。
    //   kSunSteps：一周期太阳方向的量化步数。72 → 正常 1200s 周期约每 16.7s 跨一步（重建一次
    //     mesh）、调试 30s 周期约每 0.42s 一步（肉眼可见太阳缓慢移动）。步进而非每 tick 刷 =
    //     把「全量 mesh 重建」从 10Hz 降到 ~0.06Hz（正常）/ ~2.4Hz（调试），代价是顶点光按
    //     步进变化（baseColor 仍平滑补昼夜过渡，故整体明暗无跳变）。
    static constexpr float kSunMaxElevDeg = 50.f;
    static constexpr int   kSunSteps      = 72;

    float m_phase = 0.f;      // 0..1 循环相位（由 m_elapsedMs 派生，避免浮点累积漂移）
    qint64 m_elapsedMs = 0;   // 累计已流逝毫秒（phase = (elapsed mod period) / period）
    bool m_debugFast = false;
    QTimer m_timer;

    // t123 太阳态（量化步进更新；首帧 mesh 未绑前用天顶正午）：
    QVector3D m_sunDir{0.f, 1.f, 0.f};   // 单位向量指向太阳（光来自此向）；默认天顶
    float m_sunElevDeg = kSunMaxElevDeg; // 仰角（度；正=地平线上、负=地平下）
    float m_sunAzimDeg = 0.f;            // 方位角（度；约定 +Z=0/南，绕 +Y 增大）
    int m_sunStep = -1;                  // 上次量化步（-1 = 未初始化 → 首 tick 必发 sunChanged）
};

#endif // WORLDCLOCK_H
