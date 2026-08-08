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
// t155 编辑活跃期太阳步进节流：sunChanged 跨步会触发全部 chunk 全量重建顶点光（绕 dirty，18 mesh）
//   —— 调试 30s 周期下约每 83ms 一次、正常 1200s 周期约每 3.3s 一次。玩家密集破/放时这会「抢帧」
//   （与编辑即时重建争主线程）。故编辑活跃（近 kEditCooldownMs 内有 setBlock）期间**跳过**本 tick 的
//   太阳跨步 + 不发 sunChanged → mesher 不全量重建；编辑冷却过后下一 idle tick 按 newPhase 量化 catch-up
//   （太阳一步跳到当前量化位，影 / 天空太阳 Model 随之刷新）。**昼夜亮度（skyLight）不受影响**——仍随
//   dayPhaseChanged 每 tick 平滑刷，clearColor / DirectionalLight / baseColor 无冻结，只是顶点光方向
//   （影）暂驻。编辑「活跃」由呈现层 QML 经 World::worldChanged → noteEditActivity() 反馈给本时钟（Game
//   层不 include / 不依赖 World；QML 桥接无 C++ 向上依赖，PLAN §2 分层不破）。
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
    // t388 睡觉机制「夜间才能睡」判定：夜间 = 天光乘子 < 0.5（phase ∈ (0.25, 0.75)：黄昏→子夜→黎明）。
    //   纯函数 dayPhase → bool（NOTIFY=dayPhaseChanged，与 skyLight 同信号派生）。
    Q_PROPERTY(bool isNight READ isNight NOTIFY dayPhaseChanged)
    Q_PROPERTY(bool debugFast READ debugFast NOTIFY debugFastChanged)
    Q_PROPERTY(QVector3D sunDir READ sunDir NOTIFY sunChanged)
    Q_PROPERTY(float sunElevation READ sunElevation NOTIFY sunChanged)
    Q_PROPERTY(float sunAzimuth READ sunAzimuth NOTIFY sunChanged)
    // t389 月相（机制等价 MC 1.0 月相 8 周期）：每完整过一个「天周期」(periodSecs) 月相前进一阶，
    //   8 天一轮回（满→盈凸→上弦→蛾眉→新月→残月→下弦→亏凸）。纯函数 dayCount → moonPhase：
    //   dayCount = floor(m_elapsedMs / periodMs)（已过的完整天数），moonPhase = dayCount % 8。
    //   NOTIFY=moonPhaseChanged（仅跨天时发，非每 tick → QML 不无谓刷贴图）。呈现层据本值选
    //   moon_<phase>.png（Main.qml 月 Model）。Game 层时间源派生（只读消费，PLAN §2 分层）。
    Q_PROPERTY(int moonPhase READ moonPhase NOTIFY moonPhaseChanged)
    // t464 游戏时间天数（PLAN §2-F F3 调试叠层「游戏时间：时间 + day count」）：m_elapsedMs 累计流逝
    //   的完整「天周期」数（dayCount = floor(elapsed/period)；同 moonPhase 的基准，但 moonPhase 只 % 8
    //   会把第 N 天压回 0..7 → F3 想看「已过第几天」必须暴露**完整** dayCount）。NOTIFY=dayChanged
    //   （跨天时发，独立于 moonPhaseChanged —— moonPhase 8 天才变，F3 day count 每 1 天一刷需独立信号）。
    //   单调递增（时间单向，PLAN §2-H），供 F3 叠层 + 调试展示。Game 层时间源派生（只读）。
    Q_PROPERTY(qint64 dayCount READ dayCount NOTIFY dayChanged)

public:
    explicit WorldClock(QObject *parent = nullptr);

    float dayPhase() const { return m_phase; }
    float skyLight() const;     // 派生：天光乘子（纯函数 dayPhase → 亮度；PLAN §2-H）
    // t388 夜间判定（sleep 机制用）：天光乘子 < 0.5（黄昏→子夜→黎明的暗半周期）。纯函数 dayPhase → bool。
    bool isNight() const { return skyLight() < 0.5f; }
    bool debugFast() const { return m_debugFast; }

    // t123 太阳方向（量化步进更新；mesher 据此烘顶点光）。
    QVector3D sunDir() const { return m_sunDir; }
    float sunElevation() const { return m_sunElevDeg; }
    float sunAzimuth() const { return m_sunAzimDeg; }
    // t389 月相（0..7；纯函数 dayCount%8，跨天时 onTick 更新 + emit moonPhaseChanged）。
    int moonPhase() const { return m_moonPhase; }
    // t464 完整天数（floor(elapsed/period)；跨天时更新 + emit dayChanged）。F3 叠层「day count」读它。
    //   m_dayCount 初值 -1（哨兵，保首 tick 必 emit）；clamp 到 ≥0 暴露给 QML（首 tick 前的极短窗口不显 -1）。
    qint64 dayCount() const { return m_dayCount < 0 ? 0 : m_dayCount; }

    // 调试加速键（呈现层按键调）：切 ~30s 周期，便于肉眼快速看一圈昼夜。仅调试便利、
    // 不影响生产节律常量（kDaySecs 不变；切换的是运行期所用周期）。
    Q_INVOKABLE void toggleDebugFast();

    // t388 睡觉机制「跳清晨」（夜间右键床完成后由 PlayerController 调）：把时间**向前**快进到下一清晨
    //   （phase 0.75 = 黎明）。PLAN §2-H 不变量 = 天光亮度乘子 lerp + **时间单向流逝** —— 本方法只**向前加**
    //   m_elapsedMs（绝不回退），是 Game 层「睡觉快进」事件（机制等价 MC 1.0 全员睡觉跳清晨），非呈现层反向
    //   写时间（与既有「无 setTime / 时间单向」精神一致：时间仍只增不减，只是离散跳跃一次）。即时重派生
    //   phase + 太阳方向并 emit（不等下一 100ms tick），使昼夜亮度 / 顶点光同步跳到清晨。
    Q_INVOKABLE void skipToDawn();

    // t155 编辑活跃期反馈入口（呈现层 QML 经 World::worldChanged 调）：记录「最近一次编辑」时间戳，
    //   供 onTick 判定编辑活跃期（近 kEditCooldownMs 内有编辑）→ 跳过太阳跨步全量重建，避免抢帧。
    //   分层（PLAN §2）：Game 层时间源，不 include / 不依赖 World；编辑信号由 QML 桥接转发（无 C++ 向上依赖）。
    Q_INVOKABLE void noteEditActivity();

signals:
    void dayPhaseChanged();   // 每 tick 发（phase 推进）；skyLight 派生于 phase，同信号刷新
    void debugFastChanged();
    // t123：太阳量化步进跨步时发（驱动 mesher 重建顶点光）。skyLight 不绑此信号（仍随
    //   dayPhaseChanged 平滑刷）。
    void sunChanged();
    // t464 跨天时发（驱动 F3 day count 刷新）；与 moonPhaseChanged 同 onTick 跨天分支，但独立信号
    //   —— moonPhase 8 天才变，day count 每 1 天一刷需独立 NOTIFY，否则 F3 day count 7 天不刷新。
    void dayChanged();
    // t389：跨天时发（驱动月 Model 切 moon_<phase>.png 贴图）；非每 tick → 仅 8 次周期切换刷新。
    void moonPhaseChanged();
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
    // t155：编辑活跃期判定 ——「最近一次 noteEditActivity() 至今 < kEditCooldownMs」即为活跃。
    //   onTick 跨步判定时读 m_elapsedMs（本 tick 已推进后的当前值），与编辑时间戳同基准。
    bool editingActive() const;

    // 100ms（10Hz）：昼夜 lerp 缓慢，10Hz 已视觉平滑且不无谓刷爆 QML 绑定（与 PlayerController
    // 的 16ms 物理 tick 解耦——时钟不需要 60Hz）。
    static constexpr int kTickMs = 100;
    static constexpr float kDaySecs  = 1200.f; // ~20 分钟一周期（MC 1.0；dev-spec）
    static constexpr float kFastSecs = 30.f;   // 调试加速周期（dev-spec）
    // t123 太阳轨道常量：
    //   kSunMaxElevDeg：正午太阳最高仰角（度）。sin(50°)≈0.766 为「满日照」基准（mesher 据此
    //     归一 sunDir.y → intensity∈[0,1]）。
    //   kSunSteps：一周期太阳方向的量化步数。t135 72→360：调试 30s 周期约每 0.083s 跨一步
    //     （原 72 步每 0.42s 一步 → 影子跳跃明显），360 步使影子随太阳移动更连续平滑；正常
    //     1200s 周期则约每 3.3s 一步（每步太阳移 ~1° → 影位移 <0.1 格，肉眼无感）。
    //     t153 PCF 软影进一步柔化跨步过渡：影因子是 sunDir 的连续 0..1 函数（2×2 邻列平均 + 门附近
    //     kSunFade 淡入），小步进 ΔsunDir → 小 Δ影，跨步无突变。步进而非每 tick 刷 = 把「全量 mesh
    //     重建」从 10Hz 降到 ~步数/周期 Hz，代价是顶点光按步进变化（baseColor 仍平滑补昼夜过渡）。
    static constexpr float kSunMaxElevDeg = 50.f;
    static constexpr int   kSunSteps      = 360;
    // t155 编辑活跃期窗口（毫秒）：近此窗口内有 setBlock（worldChanged）即视为「编辑活跃」→ 太阳跨步
    //   节流跳过。取 1500ms：单次破/放冻结随后 1 个太阳跨步、连续编辑期间持续冻结、玩家停手 >1.5s
    //   即恢复（影 / 天空太阳 catch-up）。值偏小 → 编辑间隙仍偶发抢帧；偏大 → 停手后影滞后明显。1.5s
    //   在「编辑密集段不卡」与「停手影即时恢复」间取平衡。
    static constexpr int kEditCooldownMs = 1500;

    float m_phase = 0.f;      // 0..1 循环相位（由 m_elapsedMs 派生，避免浮点累积漂移）
    qint64 m_elapsedMs = 0;   // 累计已流逝毫秒（phase = (elapsed mod period) / period）
    bool m_debugFast = false;
    // t389 月相态：m_dayCount = floor(elapsed/period)（已过完整天数），m_moonPhase = dayCount%8。
    //   初值 m_dayCount=-1 → 首 tick 必算 + emit（保证呈现层首帧即拿到正确 phase，非靠默认 0 兜底）。
    qint64 m_dayCount = -1;
    int m_moonPhase = 0;
    QTimer m_timer;
    // t155 最近一次 noteEditActivity() 时的 m_elapsedMs 快照（编辑活跃期判定基准）。初值 = -(cooldown+1)
    //   → 启动首 tick 即「不活跃」（diff = m_elapsedMs - 初值 ≥ cooldown），避免世界 / 尺寸初始化期的
    //   worldChanged 把首个太阳跨步误冻结（启动太阳本在天顶，冻结亦无视觉差，但语义求稳）。
    qint64 m_lastEditElapsedMs = -qint64(kEditCooldownMs) - 1;

    // t123 太阳态（量化步进更新；首帧 mesh 未绑前用天顶正午）：
    QVector3D m_sunDir{0.f, 1.f, 0.f};   // 单位向量指向太阳（光来自此向）；默认天顶
    float m_sunElevDeg = kSunMaxElevDeg; // 仰角（度；正=地平线上、负=地平下）
    float m_sunAzimDeg = 0.f;            // 方位角（度；约定 +Z=0/南，绕 +Y 增大）
    int m_sunStep = -1;                  // 上次量化步（-1 = 未初始化 → 首 tick 必发 sunChanged）
};

#endif // WORLDCLOCK_H
