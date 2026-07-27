#ifndef PLAYERCONTROLLER_H
#define PLAYERCONTROLLER_H

#include <QCursor>
#include <QElapsedTimer>
#include <QHash>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVector3D>
#include <QtQml/qqml.h>

#include "blockregistry.h" // 方块 id（默认手持方块 / 破放校验）
#include "raycast.h"       // RayHit（射线选体结果）
#include "world.h"         // Q_PROPERTY(World*) 需要 World 完整定义

// 玩家控制器（一个对象全包）：指针锁定式鼠标视角 + WASD/跳/飞 + 三模式物理。
// 继承 QQuickItem 以拿到 QQuickWindow（指针锁定需要 QCursor 居中 warp）。
//
// 鼠标：点击画面 → grab()（隐藏光标 + 居中 + 轮询）；Esc/失焦 → release()（恢复光标 = 暂停）。
// 模式：观察者(noclip 飞) / 创造(碰撞+可飞) / 生存(碰撞+重力+跳一格)。G 循环（Spectator→Creative→Survival）。
// pos 存的是「脚底」（AABB 0.6×1.8×0.6 底中心）；对外 position 是眼睛=脚底+(0,1.62,0)。
class PlayerController : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PlayerController)
    Q_PROPERTY(World *world READ world WRITE setWorld NOTIFY worldChanged)
    Q_PROPERTY(QVector3D position READ position NOTIFY positionChanged) // 眼睛位置（相机绑它）
    Q_PROPERTY(float yaw READ yaw NOTIFY yawChanged)
    Q_PROPERTY(float pitch READ pitch NOTIFY pitchChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(bool captured READ captured NOTIFY capturedChanged)
    Q_PROPERTY(bool onGround READ onGround NOTIFY onGroundChanged)
    Q_PROPERTY(bool flying READ flying NOTIFY flyingChanged)
    // 射线选体（t04）：每帧沿视线 DDA 步进，命中首个实体方块。无命中 / 暂停时 hasHit=false。
    // hitBlock=命中格整数坐标；hitNormal=命中面外法线；hitFaceCenter/hitFaceEuler 供线框 Model 直接摆位。
    Q_PROPERTY(bool hasHit READ hasHit NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitBlock READ hitBlock NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitNormal READ hitNormal NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitFaceCenter READ hitFaceCenter NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitFaceEuler READ hitFaceEuler NOTIFY hitChanged)
    // 当前手持方块（右键放置用它；t06 hotbar 会绑定此属性）。默认 Stone。
    Q_PROPERTY(int selectedBlock READ selectedBlock WRITE setSelectedBlock NOTIFY selectedBlockChanged)
    // 模式行为门控（t21）：由当前模式派生的能力标志（随 modeChanged 通知 QML）。
    // Spectator 禁放破（用户核心诉求：观察者不能破坏/放置）；飞仅 Creative/Spectator 可用。
    Q_PROPERTY(bool canBreak READ canBreak NOTIFY modeChanged)
    Q_PROPERTY(bool canPlace READ canPlace NOTIFY modeChanged)
    Q_PROPERTY(bool canFly READ canFly NOTIFY modeChanged)
    // 掉落伤害事件（t22）：生存模式着地时按落差结算，发出本次应扣 HP（每 HP = 半心）。
    // 不直接持有 PlayerState（保持 Physics/Game→呈现 的单向事件流，分层干净；与 blockBroken
    // 同模式）：呈现层经 Connections 路由到 PlayerState.takeDamage。0 表示无伤害（不路出）。
    // 注：发射正值仅 Survival；Creative 无伤、Spectator noclip 不走重力分支。

public:
    enum Mode { Spectator, Creative, Survival };
    Q_ENUM(Mode)

    explicit PlayerController(QQuickItem *parent = nullptr);

    World *world() const { return m_world; }
    void setWorld(World *w);

    QVector3D position() const { return m_pos + QVector3D(0, kEyeHeight, 0); }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    Mode mode() const { return m_mode; }
    bool captured() const { return m_captured; }
    bool onGround() const { return m_onGround; }
    bool flying() const { return m_flying; }

    bool hasHit() const { return m_hasHit; }
    QVector3D hitBlock() const { return QVector3D(m_hitBx, m_hitBy, m_hitBz); }
    QVector3D hitNormal() const { return QVector3D(m_hitNx, m_hitNy, m_hitNz); }
    QVector3D hitFaceCenter() const; // 命中面中心世界坐标（贴面，略外推防 z-fight）
    QVector3D hitFaceEuler() const;  // 把规范线框（+Z 法线）摆到命中面的欧拉角（度）

    int selectedBlock() const { return m_selectedBlock; }
    void setSelectedBlock(int id);

    // 模式行为门控（t21，PLAN §2-D：模式标志由 PlayerController 持有，输入边缘统一查）。
    // 三模式差异化：Spectator 禁放破 + 可飞；Creative 可放破 + 可飞（双击空格切）；生存可放破 + 禁飞。
    bool canBreak() const { return m_mode != Spectator; } // 观察者不能破块
    bool canPlace() const { return m_mode != Spectator; } // 观察者不能放块
    bool canFly() const   { return m_mode != Survival; }  // 生存走重力+跳，禁飞

    Q_INVOKABLE void setKey(int key, bool pressed);
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void setMode(Mode m);
    Q_INVOKABLE void grab();
    Q_INVOKABLE void release();
    // 破/放（t05）：仅指针捕获时生效；走当前射线命中结果 → World::setBlock。
    Q_INVOKABLE void breakBlock(); // 左键：命中格置 air
    Q_INVOKABLE void placeBlock(); // 右键：命中面相邻空格置 selectedBlock（不覆盖实体 / 不埋玩家）

signals:
    void worldChanged();
    void positionChanged();
    void yawChanged();
    void pitchChanged();
    void modeChanged();
    void capturedChanged();
    void onGroundChanged();
    void flyingChanged();
    void hitChanged();
    void selectedBlockChanged();
    void fallDamageTaken(int hp); // 生存掉落伤害（t22）：着地结算，正值才发；呈现层路由到 PlayerState

protected:
    void componentComplete() override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void onWindowChanged(QQuickWindow *win);
    void tick();

private:
    void pollMouse();
    void step(qreal dt);
    QVector3D wishHoriz() const;
    void moveAxis(int axis, float amount);
    bool aabbHitsSolid() const;
    void setCaptured(bool c);
    QPoint windowCenterGlobal() const;
    QVector3D lookDirection() const;             // 视线方向（与相机 eulerRotation 同源）
    void updateRaycast();                        // 每帧沿视线 DDA，更新命中态
    void clearHit();                             // 暂停/失焦时隐藏线框
    bool overlapsPlayerAABB(int bx, int by, int bz) const; // 放置校验：该格方块是否与玩家 AABB 相交

    World *m_world = nullptr;
    QQuickWindow *m_window = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_evtClock; // 事件时间戳（双击检测；不被 tick restart）

    QVector3D m_pos{8, 14, 8}; // 脚底
    QVector3D m_vel{0, 0, 0};
    float m_yaw = 0, m_pitch = -42;
    Mode m_mode = Spectator;
    bool m_captured = false, m_onGround = false;
    QHash<int, bool> m_keys;
    bool m_flying = false;          // 创造模式飞行子状态（双击空格切换；进创造默认走）
    qint64 m_lastSpaceMs = -100000; // 双击空格检测时间戳
    bool m_spacePrev = false;       // 跳跃边沿触发（长按空格只跳一次）
    int m_selectedBlock = BlockRegistry::Stone; // 当前手持方块（右键放置；默认 Stone，t06 hotbar 绑定）
    float m_peakY = 0.0f;           // 滞空期间最高点 Y（掉落伤害结算基准；componentComplete 设为脚底 Y）

    // 射线选体命中态（整数格坐标 + 整数法线分量；仅变化时 emit hitChanged，避免每帧抖动 QML）
    bool m_hasHit = false;
    qint32 m_hitBx = 0, m_hitBy = 0, m_hitBz = 0;
    qint32 m_hitNx = 0, m_hitNy = 0, m_hitNz = 0;

    static constexpr float kHalfW = 0.3f;      // 宽 0.6
    static constexpr float kHeight = 1.8f;
    static constexpr float kEyeHeight = 1.62f;
    static constexpr float kFly = 8.0f;        // 飞/观察 移速
    static constexpr float kWalk = 4.3f;       // 走 移速
    static constexpr float kGravity = 28.0f;
    static constexpr float kJump = 8.4f;       // 顶点约 1.25 格
    static constexpr float kMaxFall = 78.4f;
    static constexpr float kSens = 0.25f;      // 度/像素
    static constexpr float kDeg = 0.017453292519943295f;
    static constexpr float kReach = 5.0f;      // 射线选体射程（格）
};

#endif // PLAYERCONTROLLER_H
