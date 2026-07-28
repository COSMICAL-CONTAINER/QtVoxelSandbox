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

#include "blockregistry.h"      // 方块 id（默认手持方块 / 破放校验）
#include "hotbar.h"             // Hotbar VM（t36 拾取 addStack / 丢弃 takeStack）
#include "itementitymanager.h"  // 掉落实体管理器（t36 拾取扫描 / removeAt）
#include "raycast.h"            // RayHit（射线选体结果）
#include "toolregistry.h"       // 工具感知挖掘速度 / 掉落判定（t34）
#include "world.h"              // Q_PROPERTY(World*) 需要 World 完整定义

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
    // 拾取 / 丢弃（t36）：PlayerController 经 Q_PROPERTY 持 Hotbar* / ItemEntityManager*（同 world 模式，
    // QML 注入 peer ViewModel）。拾取 = 每帧扫附近掉落实体 → Hotbar.addStack（先选中槽、再空槽）→
    // addStack 返 0（全入）时 ItemEntityManager.removeAt 销毁实体；丢弃 = Q 键 Hotbar.takeStack 1 件 →
    // 发 spawnItem（玩家前方，经 QML Connections 回流 ItemEntityManager.spawnItem）。
    // 分层（PLAN §2）：PlayerController 属 Game/Physics，Hotbar/ItemEntityManager 属 ViewModel/Entities，
    // 经 QML 绑定注入（运行期连接、非编译期反向依赖；同 world 属性先例）。
    Q_PROPERTY(Hotbar *hotbar READ hotbar WRITE setHotbar NOTIFY hotbarChanged)
    Q_PROPERTY(ItemEntityManager *itemEntities READ itemEntities WRITE setItemEntities NOTIFY itemEntitiesChanged)
    Q_PROPERTY(QVector3D position READ position NOTIFY positionChanged) // 眼睛位置（相机绑它）
    Q_PROPERTY(float yaw READ yaw NOTIFY yawChanged)
    Q_PROPERTY(float pitch READ pitch NOTIFY pitchChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    // 相机模式（t27）：F5 循环 第一人称→第三人称-后→第三人称-前→第一人称。模式标志属 PlayerController，
    // 相机摆位属 QML 呈现层（按 cameraMode 算 position/eulerRotation）。feetPosition=脚底 m_pos
    // （第三人称玩家模型绑它，t28）；lookVector=视线方向（第三人称相机沿视线偏移绑它）。
    Q_PROPERTY(CameraMode cameraMode READ cameraMode NOTIFY cameraModeChanged)
    Q_PROPERTY(QVector3D feetPosition READ feetPosition NOTIFY positionChanged)
    Q_PROPERTY(QVector3D lookVector READ lookVector NOTIFY lookChanged)
    // 第三人称相机距离钳制（t40）：每帧从眼位沿相机偏移方向（ThirdPersonBack=−look，Front=+look）
    // DDA 步进（max=kCamMax=3.5），返回首个实体命中距离（留 kCamMargin 余量贴在面前）；无命中=kCamMax。
    // Main.qml 相机 position 用 ±cameraDistance 偏移 → 相机贴墙不穿入。第一人称恒 0（不偏移）。
    // 复用 raycastVoxel（RayHit.dist 已暴露命中距离）。仅值真变时发 cameraDistanceChanged（DDA 对同输入
    // 确定 → 玩家不动/不转时距离稳定，无抖动）。
    Q_PROPERTY(float cameraDistance READ cameraDistance NOTIFY cameraDistanceChanged)
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
    // 当前手持物品的**原始 id**（t34 工具感知挖掘用）：方块段直接透传；工具段（>=0x100）由
    // hotbar 暴露的 selectedItemId 绑定而来。与 selectedBlock 的差异：选中工具槽时 selectedBlock
    // →Air（右键不放置），但 selectedItem 仍是工具 id → 挖掘速度按工具 tier 算（t33 表）。
    Q_PROPERTY(int selectedItem READ selectedItem WRITE setSelectedItem NOTIFY selectedItemChanged)
    // 持续挖掘态（t34）：生存模式按住左键累积进度；创造模式按下即瞬破（不进入此态）。
    //   mining = 是否正在累积（生存）；miningProgress = 0..1；miningStage = -1（无裂纹）/0..5
    //   （裂纹叠层阶，按进度 0/20/40/60/80/100% 切）；miningBlock = 当前目标格整数坐标。
    //   呈现层（Main.qml 的裂纹叠层 Model）据此绑定：visible=miningStage>=0；baseColorMap 切
    //   crack_{stage}.png；position=miningBlock+0.5。spec：换目标 / 松开 / 失焦 / 暂停 → 清零。
    Q_PROPERTY(bool mining READ mining NOTIFY miningStateChanged)
    Q_PROPERTY(float miningProgress READ miningProgress NOTIFY miningProgressChanged)
    Q_PROPERTY(int miningStage READ miningStage NOTIFY miningStateChanged)
    Q_PROPERTY(QVector3D miningBlock READ miningBlock NOTIFY miningStateChanged)
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
    // 相机视角（t27）：F5 循环 0→1→2→0。相机摆位在 QML（PerspectiveCamera）按此模式算。
    enum CameraMode { FirstPerson, ThirdPersonBack, ThirdPersonFront };
    Q_ENUM(CameraMode)

    explicit PlayerController(QQuickItem *parent = nullptr);

    World *world() const { return m_world; }
    void setWorld(World *w);
    Hotbar *hotbar() const { return m_hotbar; }
    void setHotbar(Hotbar *h);
    ItemEntityManager *itemEntities() const { return m_itemEntities; }
    void setItemEntities(ItemEntityManager *m);

    QVector3D position() const { return m_pos + QVector3D(0, kEyeHeight, 0); }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    Mode mode() const { return m_mode; }
    CameraMode cameraMode() const { return m_cameraMode; }
    QVector3D feetPosition() const { return m_pos; }          // 脚底位置（= m_pos；第三人称玩家模型绑它）
    QVector3D lookVector() const { return lookDirection(); }  // 视线方向（第三人称相机沿视线偏移绑它）
    float cameraDistance() const { return m_cameraDistance; } // 第三人称相机距离（钳制后；t40）
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
    int selectedItem() const { return m_selectedItem; }
    void setSelectedItem(int id);

    bool mining() const { return m_mining; }
    float miningProgress() const { return m_miningProgress; }
    int miningStage() const { return m_miningStage; }
    QVector3D miningBlock() const { return QVector3D(m_mineBx, m_mineBy, m_mineBz); }

    // 模式行为门控（t21，PLAN §2-D：模式标志由 PlayerController 持有，输入边缘统一查）。
    // 三模式差异化：Spectator 禁放破 + 可飞；Creative 可放破 + 可飞（双击空格切）；生存可放破 + 禁飞。
    bool canBreak() const { return m_mode != Spectator; } // 观察者不能破块
    bool canPlace() const { return m_mode != Spectator; } // 观察者不能放块
    bool canFly() const   { return m_mode != Survival; }  // 生存走重力+跳，禁飞

    Q_INVOKABLE void setKey(int key, bool pressed);
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void setMode(Mode m);
    Q_INVOKABLE void cycleCamera(); // F5 相机模式循环（0→1→2→0，t27）
    Q_INVOKABLE void grab();
    Q_INVOKABLE void release();
    // 破/放（t05）：仅指针捕获时生效；走当前射线命中结果 → World::setBlock。
    //   breakBlock()（兼容 t05 单击）：创造=瞬破；生存=开始累积（同 beginMining）。
    //   beginMining() / endMining()（t34）：左键按下 / 松开的事件边缘；生存累积进度。
    Q_INVOKABLE void breakBlock(); // 左键：命中格置 air
    Q_INVOKABLE void placeBlock(); // 右键：命中面相邻空格置 selectedBlock（不覆盖实体 / 不埋玩家）
    Q_INVOKABLE void beginMining(); // 左键按下：创造瞬破 / 生存开始累积进度（t34）
    Q_INVOKABLE void endMining();   // 左键松开：清生存累积进度（t34）
    // 中键拾取方块（t37 pick block）：取当前射线命中格的方块 id → 写入 hotbar 当前选中槽（覆盖；
    // 创造源无限 → 满栈，生存 → 单件）。仅指针捕获时生效（与破/放同窗口级 MouseButtonPress 路径）。
    // spec：「无论背包开关」—— captured=true 蕴含背包已关，故等价于「游戏内中键」；命中空气 / 无
    // 世界 / 无 hotbar → 不动作。pick 属「选择」语义（不改栅格），三模式均允许（观察者亦可查方块）。
    Q_INVOKABLE void pickBlock();
    // Q 键丢弃（t36）：从选中槽 takeStack 1 件 → 发 spawnItem（玩家前方 1.5 格）。仅指针捕获时生效
    // （spec）。空手 / 取失败 → 不丢。spawnItem 经 QML Connections 转发到 ItemEntityManager.spawnItem
    // （同破块掉落 t35 路径）；丢弃后实体可被重新拾取（闭环）。
    Q_INVOKABLE void dropHeld();

signals:
    void worldChanged();
    void hotbarChanged();
    void itemEntitiesChanged();
    void positionChanged();
    void yawChanged();
    void pitchChanged();
    void modeChanged();
    void cameraModeChanged();
    void lookChanged();
    void cameraDistanceChanged(); // 第三人称相机距离变（t40；值真变才发，免抖动）
    void capturedChanged();
    void onGroundChanged();
    void flyingChanged();
    void hitChanged();
    void selectedBlockChanged();
    void selectedItemChanged(); // 手持原始 id 变（含工具段切换；驱动 t34 速度重算）
    void miningStateChanged();  // mining / miningStage / miningBlock 三者同变（一次性发，少抖动）
    void miningProgressChanged(); // 进度连续变化（HUD 可选显示百分比；高频，独立信号）
    // 玩家挖掘产出（t34 → t35）：生存破块时按 ToolRegistry::canHarvest 判 drop；创造 drop=false
    // （瞬破不掉落，spec）。t35 据此 spawn item entity；当前任务仅发信号、消费端未接（无副作用）。
    // blockBroken（World 已发）仍触发粒子；本信号额外区分「玩家挖」+「是否掉落」（创造 / 不可采掘
    // 不掉）。坐标 + 原方块 id。
    void playerMined(int x, int y, int z, int blockId, bool drop);
    // 方块掉落实体（t35）：生存破块且 ToolRegistry::canHarvest 判定掉落（drop=true）时发；
    // 创造瞬破（drop=false）/ 不可采掘（canHarvest=false，如空手破石）不发。坐标 = 被破格整数
    // 坐标，id = 原方块 id。Main.qml Connections 转发到 ItemEntityManager.spawnItem 生成实体。
    // 分层（PLAN §2）：Game/Physics 层发语义事件，呈现层 / ViewModel 只消费（同 blockBroken→粒子）。
    void spawnItem(int x, int y, int z, int blockId);
    void fallDamageTaken(int hp); // 生存掉落伤害（t22）：着地结算，正值才发；呈现层路由到 PlayerState
    // 第一人称手挥动（t29）：breakBlock/placeBlock 在通过模式门控 + 动作真发生后发（观察者不发；
    // 未命中/放置被拒也不发）。同 blockBroken 模式——Game/Physics 层发语义事件，呈现层 Connections
    // 消费启动手臂挥动动画（PLAN §2 分层：手 viewmodel 属呈现层，绝不反向写）。
    void swingArm();

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
    void updateCameraDistance();                 // 每帧算第三人称相机距离（钳制防穿墙，t40）
    void clearHit();                             // 暂停/失焦时隐藏线框
    bool overlapsPlayerAABB(int bx, int by, int bz) const; // 放置校验：该格方块是否与玩家 AABB 相交
    // 持续挖掘（t34）：每 tick 累积进度 / 检目标变更 / 完成时破块。由 tick() 调（captured 时）。
    void updateMining(float dt);
    // 清掉累积态（松开 / 换目标 / 失焦 / 完成）。无变化时静默（不发信号）。
    void cancelMining();
    // 完成（progress 满）：写 air + 发 playerMined + 清态。drop 由 caller 算（生存走 ToolRegistry）。
    void finishMiningAt(int x, int y, int z, bool drop);
    // 拾取扫描（t36）：每帧扫附近掉落实体 → Hotbar.addStack（先选中槽、再空槽）。addStack 返 0
    // （全入）→ ItemEntityManager.removeAt 销毁实体；返 >0（背包满）→ 不拾取（entity 留）。
    // 距离从玩家 AABB 中心（脚底 + 半高）3D 起算，阈值 kPickupDist；从后往前扫便于 erase。
    void pickupScan();

    World *m_world = nullptr;
    Hotbar *m_hotbar = nullptr;                  // 拾取 addStack / 丢弃 takeStack 的栈操作目标（Q_PROPERTY 绑定）
    ItemEntityManager *m_itemEntities = nullptr; // 拾取扫描数据源 + removeAt 销毁（Q_PROPERTY 绑定）
    QQuickWindow *m_window = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_evtClock; // 事件时间戳（双击检测；不被 tick restart）

    QVector3D m_pos{8, 14, 8}; // 脚底
    QVector3D m_vel{0, 0, 0};
    float m_yaw = 0, m_pitch = -42;
    Mode m_mode = Spectator;
    CameraMode m_cameraMode = FirstPerson; // F5 相机模式（默认第一人称，t27）
    float m_cameraDistance = 0.0f;         // 第三人称相机距离（钳制后；FirstPerson 恒 0；t40）
    bool m_captured = false, m_onGround = false;
    QHash<int, bool> m_keys;
    bool m_flying = false;          // 创造模式飞行子状态（双击空格切换；进创造默认走）
    qint64 m_lastSpaceMs = -100000; // 双击空格检测时间戳
    bool m_spacePrev = false;       // 跳跃边沿触发（长按空格只跳一次）
    int m_selectedBlock = BlockRegistry::Stone; // 当前手持方块（右键放置；默认 Stone，t06 hotbar 绑定）
    int m_selectedItem = BlockRegistry::Stone;  // 手持物品原始 id（含工具段；t34 挖掘速度用，绑定 hotbar.selectedItemId）
    float m_peakY = 0.0f;           // 滞空期间最高点 Y（掉落伤害结算基准；componentComplete 设为脚底 Y）

    // 持续挖掘态（t34）：仅 Survival 进入累积（Creative 单击瞬破不进入）；progress 0..1；
    // stage = clamp(progress*6, 0, 5)，-1 = 无累积（裂纹叠层隐藏）。mineBx/y/z = 目标格整数坐标。
    bool m_mining = false;
    float m_miningProgress = 0.0f;
    int m_miningStage = -1;
    qint32 m_mineBx = 0, m_mineBy = 0, m_mineBz = 0;
    // 物理左键按下态（t44 连续挖掘）：与 m_mining（=「正在某目标上累积进度」）分离 —— finishMiningAt
    // 破完一块后 cancelMining 清 m_mining，但左键可能仍按住。m_leftDown 仅由 press 边缘（beginMining）
    // 置 true、release 边缘（endMining）/ 暂停失焦（release）置 false；finishMiningAt / cancelMining
    // 不动它。updateMining 顶部据此 + 新命中 → 自动 beginMining 下一块（progress 归 0），不松手连挖。
    bool m_leftDown = false;

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
    static constexpr float kPickupDist = 1.5f; // 拾取距离阈值（格；玩家 AABB 中心起算，spec ~1.2 量级）
    static constexpr float kCamMax = 3.5f;     // 第三人称相机最大距离（格；t40，与 Main.qml 默认 d 对齐）
    static constexpr float kCamMargin = 0.1f;  // 相机贴命中面前的余量（防卡面 z-fight / 近裁面穿插；t40）
};

#endif // PLAYERCONTROLLER_H
