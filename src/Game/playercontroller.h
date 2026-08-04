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

#include <limits>

#include "blockregistry.h"      // 方块 id（默认手持方块 / 破放校验）
#include "entitymanager.h"      // 统一实体管理器（t95 测试生物 / 玩家推动）
#include "hotbar.h"             // Hotbar VM（t36 拾取 addStack / 丢弃 takeStack）
#include "itementitymanager.h"  // 掉落实体管理器（t36 拾取扫描 / removeAt）
#include "raycast.h"            // RayHit（射线选体结果）
#include "toolregistry.h"       // 工具感知挖掘速度 / 掉落判定（t34）
#include "world.h"              // Q_PROPERTY(World*) 需要 World 完整定义
#include "worldclock.h"         // t280 黑暗刷怪：读 skyLight（昼夜乘子）驱动 EntityManager::tickHostileLife

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
    // 统一实体管理器（t95）：PlayerController 经 Q_PROPERTY 持 EntityManager*（同 world/hotbar/itemEntities
    // 模式，QML 注入 peer ViewModel）。每帧调 entityManager.tick(dt, world)（重力 / 地面静止，独立于
    // 捕获态——菜单 / 暂停时实体仍模拟）+ captured 时 resolvePlayerPush（玩家走碰可推动实体，把玩家
    // 位移解析后传给实体）。分层（PLAN §2）：PlayerController 属 Game/Physics，EntityManager 属 Entities，
    // 经 QML 绑定注入（运行期连接、非编译期反向依赖，同 itemEntities 先例）。
    Q_PROPERTY(EntityManager *entityManager READ entityManager WRITE setEntityManager NOTIFY entityManagerChanged)
    // t280 黑暗刷怪：PlayerController 经 Q_PROPERTY 持 WorldClock*（同 world/hotbar/itemEntities/entityManager
    //   模式，QML 注入 peer ViewModel）。读 worldClock->skyLight()（[0,1] 昼夜乘子）每 tick 传给
    //   EntityManager::tickHostileLife（驱动 spawn 光判定 + 白天燃烧）。Game→World 向下依赖合规（WorldClock
    //   属 World 层）。null 时跳过敌对生命周期（无昼夜 → 无 spawn / 无燃烧，安全降级）。
    Q_PROPERTY(WorldClock *worldClock READ worldClock WRITE setWorldClock NOTIFY worldClockChanged)
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
    // 第三人称模型动画驱动（t45）：moveSpeed = 当前行走速度（仅走路模式非零，供 QML 驱动腿/臂摆动频率）；
    //   walkPhase = 行走相位（弧度，走时按 speed*dt*kStrideRate 累加、2π 回绕；静止不累加 → sin*0=0 中性位）。
    //   仅 Survival / Creative-未飞 推进；Spectator / Creative-飞 = 0（飞行/幽灵态无走步动画，spec 未要求）。
    //   分层（PLAN §2）：动画驱动数据（速度 + 相位）由 Game/Physics 层 tick 算出，QML 呈现层只读消费、
    //   绝不反向写（同 blockBroken→粒子 / swingArm→手挥动 模式）。腿/臂的实际欧拉角算在 QML（呈现层）。
    Q_PROPERTY(float moveSpeed READ moveSpeed NOTIFY moveSpeedChanged)
    // 实际水平速度（blocks/sec，t159）：= 位移/dt 的水平标量（含撞墙归零、疾跑 / 飞 / 水下倍数）。
    //   与 moveSpeed（走路动画意图速度，飞 / 观察者=0）的区别：speed 是**真实有效水平速度**，F3 叠层
    //   报它（spec「F3 加 speed 行 = 水平速度标量」）。复用 moveSpeedChanged 作 NOTIFY（spec：NOTIFY
    //   moveSpeedChanged）—— 两者同在 step() 出口刷新，QML 绑定 speed / moveSpeed 一并重算。
    Q_PROPERTY(float speed READ speed NOTIFY moveSpeedChanged)
    Q_PROPERTY(float walkPhase READ walkPhase NOTIFY walkPhaseChanged)
    // 移动状态机（t51）：双击 W 进 Sprint（水平移速 ×1.3、四肢摆动幅度 ×1.4）；Shift 按住进 Crouch
    //   （×0.4、AABB 高 1.8→1.5、相机随之降低、且「边缘安全」= 蹲下时若脚下将无支撑则不水平移动）。
    //   Walk 为默认。仅走路模式（Survival / Creative-未飞）有效：飞/Spectator 恒 Walk（Shift 在飞态作
    //   「下降」用，不触发蹲；W 无疾跑语义）。状态驱动模型动画频率（speedMul 已乘入 moveSpeed →
    //   walkPhase 推进速率随之变）与幅度（QML 据 moveState 缩放四肢摆角，接 t45）。
    //   分层（PLAN §2）：状态属 Game/Physics 层、由输入边缘统一推进（§2-D）；呈现层只读消费。
    Q_PROPERTY(MoveState moveState READ moveState NOTIFY moveStateChanged)
    // 飞行速度倍数（t159/t210）：观察者（spectator）用滚轮调速，默认 1.0（= kFly 基速）；滚轮 ±档，
    //   有效速度 = clamp(kFly * mul, kFlyMin, kFlyMax) ∈ [4, 20] blocks/sec。t210 起滚轮语义按模式分流：
    //   spectator 滚轮=调速、创造/生存滚轮=切 hotbar（创造飞态亦走 hotbar，mul 保持默认 1.0 不由滚轮改）。
    //   step() 仍读 mul 算飞移速（创造-飞 / spectator 常驻飞均用）。状态属 Game/Physics 层（§2-D 单一输入路径：
    //   滚轮经 QML → adjustFlySpeed）。
    Q_PROPERTY(float flySpeedMul READ flySpeedMul NOTIFY flySpeedMulChanged)
    // 当前有效飞行速度（blocks/sec）= clamp(kFly*mul, kFlyMin, kFlyMax)；F3 报它（t159）。随 mul 变通知。
    Q_PROPERTY(float flySpeed READ flySpeed NOTIFY flySpeedMulChanged)
    // t178 帧时间切分（PLAN §4 验收「写死帧时间切分 CPU/GPU ms」）：每帧测 tick() 主线程 CPU 耗时（物理 /
    //   射线 / 实体 / 挖掘 / 拾取），~1s 滚动平均暴露给 F3 叠层。QtQuick3D 路径无公开 GPU 计时 / 逐帧 draw-call
    //   查询 → F3 另用 frameMs(=1000/fps) + 估算 draw-call 数补足「CPU/GPU/draw-call 预算」（spec：不得伪造，
    //   估算值明确标注 ≈）。GPU 真计时待自研 RHI 迁移（QRhiGpuTimer）。状态属 Game/Physics 层（§2-D）。
    Q_PROPERTY(float simMs READ simMs NOTIFY perfChanged)
    // 射线选体（t04）：每帧沿视线 DDA 步进，命中首个实体方块。无命中 / 暂停时 hasHit=false。
    // hitBlock=命中格整数坐标；hitNormal=命中面外法线；hitFaceCenter/hitFaceEuler 供线框 Model 直接摆位。
    Q_PROPERTY(bool hasHit READ hasHit NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitBlock READ hitBlock NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitNormal READ hitNormal NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitFaceCenter READ hitFaceCenter NOTIFY hitChanged)
    Q_PROPERTY(QVector3D hitFaceEuler READ hitFaceEuler NOTIFY hitChanged)
    // t253 攻击单体选中（spec「近距两 mob 只打一个 / 射线最近命中」）：每帧沿视线 findMobHit 选出的
    //   **单个**活体 mob 索引（nearest-along-ray；非 AoE 全打），供 QML 目标框高亮（呈现层只读）。
    //   -1 = 无目标（瞄准未命中 mob / 方块挡在 mob 前 / 无实体管理器 / 暂停）。仅 captured 时刷新；
    //   beginMining 攻击路径仍即时调 findMobHit（点击瞬间最新视线），不依赖本缓存。
    Q_PROPERTY(int targetedMob READ targetedMob NOTIFY targetedMobChanged)
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
    // 持续进食态（t267）：手持面包时**按住**右键累积进食进度（机制等价 MC 1.0 长按右键食面包 ~1.6s）。
    //   eating = 是否正在进食；eatingProgress = 0..1。呈现层据此驱动 viewModelHand 下沉 + 抖动动画；
    //   进食屑粒由 eatingParticle 信号驱动（嘴部迸发，同 miningParticle 模式）。spec「单击即食→改长按右键」。
    //   松开 / 换槽（持物不再是面包）/ 失焦 / 暂停 → 清零（未完成不消耗）。完成（progress>=1）→ 消耗 1 面包。
    Q_PROPERTY(bool eating READ eating NOTIFY eatingStateChanged)
    Q_PROPERTY(float eatingProgress READ eatingProgress NOTIFY eatingProgressChanged)
    // t304 弓拉弓态（手持弓右键长按蓄力）：bowDrawing = 正在拉弓；bowDrawProgress = 0..1 蓄力进度
    //   （0=刚起拉、1=满弓）。呈现层（Main.qml viewModelHand）据此把手 / 弓后拉（拉弓动画）；松开 endBowDraw
    //   据 progress 算箭速 / 伤害并射出。spec「长按右键拉弓动画 → 松开射箭」。仅持弓右键进入（eventFilter
    //   RightButton press 据 selectedItemId==Bow 分流）；松开 / 换槽 / 失焦 / 暂停 → 清零（cancelBowDraw）。
    Q_PROPERTY(bool bowDrawing READ bowDrawing NOTIFY bowDrawChanged)
    Q_PROPERTY(float bowDrawProgress READ bowDrawProgress NOTIFY bowDrawChanged)
    // 模式行为门控（t21）：由当前模式派生的能力标志（随 modeChanged 通知 QML）。
    // Spectator 禁放破（用户核心诉求：观察者不能破坏/放置）；飞仅 Creative/Spectator 可用。
    Q_PROPERTY(bool canBreak READ canBreak NOTIFY modeChanged)
    Q_PROPERTY(bool canPlace READ canPlace NOTIFY modeChanged)
    Q_PROPERTY(bool canFly READ canFly NOTIFY modeChanged)
    // t201 水下蓝滤镜：眼位（= position()，脚底+eyeHeight）所在格 == Water 时为真。QML 据此显全屏浅蓝
    //   半透叠层（机制等价 MC 水下视野蓝雾），1/2/3 人称统一（基于眼位 blockAt，与相机模式无关）。tickImpl
    //   每 tick 重算并缓存到 m_eyeInWater；状态翻转才发 eyeInWaterChanged（避免每帧抖 QML 绑定）。只读
    //   World::blockAt（向下依赖）；无世界 → false。speed-mul 减速（t159）直接调同名 const 方法读实时值。
    Q_PROPERTY(bool eyeInWater READ eyeInWater NOTIFY eyeInWaterChanged)
    // t269 脚位水中态（驱动水中走路声分流）：脚底格 == Water 时为真（涉水 / 浸没均真）。与 eyeInWater 的差异：
    //   eyeInWater 仅眼位在水（完全潜没）；feetInWater 覆盖「眼在水面上但脚在水里」的涉水走步（机制等价
    //   MC 涉水 / 游泳时步声变水声）。tickImpl 每 tick 重算并缓存到 m_feetInWater；状态翻转才发
    //   feetInWaterChanged（避免每帧抖 QML 绑定，同 eyeInWater 模式）。Main.qml onWalkPhaseChanged 据它分流
    //   playWaterStep（水中）vs playStep（陆地）。只读 World::blockAt（向下依赖）；无世界 → false。
    Q_PROPERTY(bool feetInWater READ feetInWater NOTIFY feetInWaterChanged)
    // t223 近流水 proximity 水流声强度（0..1）：玩家到最近**流动水**格（Water 且 state>0；静止水源 state=0
    //   不算 —— MC 近大片静海无流水声、近瀑布 / 玩家倒水流才有）的距离映射，近=1 / 远→0、范围外=0。
    //   tickImpl 节流扫邻近盒（~每 0.25s）算最近流水格距离 → level = clamp(1 - dist/kFlowSoundRadius, 0, 1)。
    //   Main.qml Connections 据此 start/stop AudioManager 水流声 + setWaterFlowLevel（level=0 → stopWaterFlow）。
    //   仅 playing 流动水存在时 >0；菜单态 player.release 后扫描仍跑但通常无流水格 → 0 → 自动停。只读 World
    //   （blockAt/stateAt 向下依赖）；无世界 → 0。值真变（>epsilon 或 0<->非0 翻转）才 emit，免每 scan 抖 QML。
    Q_PROPERTY(float flowSoundLevel READ flowSoundLevel NOTIFY flowSoundLevelChanged)
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
    // 移动状态机（t51）：Walk=默认走；Sprint=双击 W 疾跑；Crouch=按住 Shift 蹲下。
    //   仅 Walk↔Sprint↔Crouch 三态；详见 moveState 属性注释。
    enum MoveState { Walk, Sprint, Crouch };
    Q_ENUM(MoveState)

    explicit PlayerController(QQuickItem *parent = nullptr);

    World *world() const { return m_world; }
    void setWorld(World *w);
    Hotbar *hotbar() const { return m_hotbar; }
    void setHotbar(Hotbar *h);
    ItemEntityManager *itemEntities() const { return m_itemEntities; }
    void setItemEntities(ItemEntityManager *m);
    EntityManager *entityManager() const { return m_entityManager; }
    void setEntityManager(EntityManager *m);
    WorldClock *worldClock() const { return m_worldClock; }
    void setWorldClock(WorldClock *c);

    QVector3D position() const { return m_pos + QVector3D(0, m_eyeHeight, 0); }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    Mode mode() const { return m_mode; }
    MoveState moveState() const { return m_moveState; } // 当前移动态（t51；驱动 QML 摆幅 + 速度因子）
    CameraMode cameraMode() const { return m_cameraMode; }
    QVector3D feetPosition() const { return m_pos; }          // 脚底位置（= m_pos；第三人称玩家模型绑它）
    QVector3D lookVector() const { return lookDirection(); }  // 视线方向（第三人称相机沿视线偏移绑它）
    float cameraDistance() const { return m_cameraDistance; } // 第三人称相机距离（钳制后；t40）
    bool captured() const { return m_captured; }
    bool onGround() const { return m_onGround; }
    bool flying() const { return m_flying; }
    float moveSpeed() const { return m_moveSpeed; } // 当前行走速度（仅走路模式非零；t45 QML 腿/臂摆频）
    float speed() const { return m_horizSpeed; }    // 实际水平速度（blocks/sec；F3 报它；t159）
    float walkPhase() const { return m_walkPhase; } // 行走相位（弧度；走时累加、2π 回绕；t45 QML sin() 算摆角）
    float flySpeedMul() const { return m_flySpeedMul; } // 飞行速度倍数（默认 1.0；t159 滚轮调速）
    float flySpeed() const;                              // 有效飞行速度 = clamp(kFly*mul,4,20)；F3 报它（t159）
    // t178：上次 1s 窗口的 tick() CPU 耗时平均（ms）；F3 帧时间切分用。
    float simMs() const { return m_simMs; }

    bool hasHit() const { return m_hasHit; }
    QVector3D hitBlock() const { return QVector3D(m_hitBx, m_hitBy, m_hitBz); }
    QVector3D hitNormal() const { return QVector3D(m_hitNx, m_hitNy, m_hitNz); }
    QVector3D hitFaceCenter() const; // 命中面中心世界坐标（贴面，略外推防 z-fight）
    QVector3D hitFaceEuler() const;  // 把规范线框（+Z 法线）摆到命中面的欧拉角（度）
    // t253：当前准星瞄准的**单个**目标 mob 索引（findMobHit 最近活体；updateRaycast 每帧刷新）。
    //   -1 = 无目标。供 QML 目标框高亮（呈现层只读 EntityManager.posAt/radiusAt/halfHeightAt）。
    int targetedMob() const { return m_targetedMob; }

    int selectedBlock() const { return m_selectedBlock; }
    void setSelectedBlock(int id);
    int selectedItem() const { return m_selectedItem; }
    void setSelectedItem(int id);

    bool mining() const { return m_mining; }
    float miningProgress() const { return m_miningProgress; }
    int miningStage() const { return m_miningStage; }
    QVector3D miningBlock() const { return QVector3D(m_mineBx, m_mineBy, m_mineBz); }
    bool eating() const { return m_eating; }          // t267 进食态（驱动 viewModelHand 下沉 + 抖动）
    float eatingProgress() const { return m_eatingProgress; } // t267 进食进度 0..1
    // t304 弓拉弓态（Q_PROPERTY：bowDrawing / bowDrawProgress）。仅持弓右键蓄力时为真。
    bool bowDrawing() const { return m_bowDrawing; }
    float bowDrawProgress() const; // 蓄力进度 0..1（钳到 [0,1]；满弓=1）

    // 模式行为门控（t21，PLAN §2-D：模式标志由 PlayerController 持有，输入边缘统一查）。
    // 三模式差异化：Spectator 禁放破 + 可飞；Creative 可放破 + 可飞（双击空格切）；生存可放破 + 禁飞。
    bool canBreak() const { return m_mode != Spectator; } // 观察者不能破块
    bool canPlace() const { return m_mode != Spectator; } // 观察者不能放块
    bool canFly() const   { return m_mode != Survival; }  // 生存走重力+跳，禁飞

    // t201 水下判定（Q_PROPERTY eyeInWater READ）：眼位格 == Water。step() 读它乘水下速度倍数（t159）；
    //   QML 读它驱动水下蓝滤镜叠层。const 只读 World::blockAt（向下依赖）；眼位 = position()（脚底+eyeHeight）；
    //   无世界 → false。定义在 .cpp。
    bool eyeInWater() const;
    // t269 脚位水中判定（Q_PROPERTY feetInWater READ）：脚底格 == Water（涉水 / 浸没均真）。step() 内复用它
    //   做浮力 / 游泳 / 水流推动物理；QML 读它驱动水中走路声分流（playWaterStep vs playStep）。const 只读
    //   World::blockAt（向下依赖）；无世界 → false。定义在 .cpp。
    bool feetInWater() const;
    // t223 近流水 proximity 水流声强度（Q_PROPERTY flowSoundLevel READ）：玩家到最近流水格的距离映射 [0,1]。
    //   无世界 / 无近流水 → 0。定义在 .cpp。
    float flowSoundLevel() const;

    Q_INVOKABLE void setKey(int key, bool pressed);
    Q_INVOKABLE void cycleMode();
    Q_INVOKABLE void setMode(Mode m);
    Q_INVOKABLE void cycleCamera(); // F5 相机模式循环（0→1→2→0，t27）
    // 飞行速度滚轮调速（t159/t210）：dir=+1 加速 / -1 减速（对应前滚 / 后滚），每档 kFlyStep。
    //   有效速度 clamp 到 [kFlyMin, kFlyMax]（4..20 blocks/sec）。t210 起仅 QML 在 spectator 模式调用
    //   （创造 / 生存滚轮切 hotbar）。
    Q_INVOKABLE void adjustFlySpeed(int dir);
    Q_INVOKABLE void grab();
    Q_INVOKABLE void release();
    // 破/放（t05）：仅指针捕获时生效；走当前射线命中结果 → World::setBlock。
    //   breakBlock()（兼容 t05 单击）：创造=瞬破；生存=开始累积（同 beginMining）。
    //   beginMining() / endMining()（t34）：左键按下 / 松开的事件边缘；生存累积进度。
    Q_INVOKABLE void breakBlock(); // 左键：命中格置 air
    Q_INVOKABLE void placeBlock(); // 右键：命中面相邻空格置 selectedBlock（不覆盖实体 / 不埋玩家）
    Q_INVOKABLE void beginMining(); // 左键按下：创造瞬破 / 生存开始累积进度（t34）
    Q_INVOKABLE void endMining();   // 左键松开：清生存累积进度（t34）
    // 持续进食（t267）：手持面包时**按住**右键累积进食进度（机制等价 MC 1.0 长按右键食面包 ~1.6s）。
    //   beginEating() = 右键按下边缘（eventFilter 据持物 == BreadId 分流，面包不进 placeBlock；spec「非单击」）；
    //   endEating() = 右键松开边缘（清累积进度，未完成不消耗）。完成时 finishEating 消耗 1 面包 + 恢复饥饿。
    Q_INVOKABLE void beginEating(); // 右键按下（手持面包）：开始累积进食进度（t267）
    Q_INVOKABLE void endEating();   // 右键松开：清累积进食进度（未完成不消耗，t267）
    // t304 弓拉弓 / 射箭：手持弓右键按下 → beginBowDraw（开始蓄力，m_bowDrawTime 累加）；右键松开 → endBowDraw
    //   （据蓄力射箭：消耗箭 + spawnArrowPlayer + 弓耐久 -1）。spec「长按右键拉弓 → 松开射箭」。
    //   beginBowDraw：仅持弓 + 可放置（非观察者，沿用 placeBlock 入口门控语义）+ 已捕获时进入；无需命中
    //   （弓瞄准走视线方向，不依赖射线命中实体方块）。蓄力期间 step() 减速（kBowSlowMul，spec「拉弓减速」）。
    //   endBowDraw：蓄力 < kBowMinChargeRatio / 无箭 → 不射（仅 cancel）；满足 → 算速度 / 伤害射出。创造不耗箭 / 耐久。
    Q_INVOKABLE void beginBowDraw();
    Q_INVOKABLE void endBowDraw();
    // 中键拾取方块（t37 pick block）：取当前射线命中格的方块 id → 装入 hotbar。仅指针捕获时生效
    // （与破/放同窗口级 MouseButtonPress 路径）。
    // spec：「无论背包开关」—— captured=true 蕴含背包已关，故等价于「游戏内中键」；命中空气 / 无
    // 世界 / 无 hotbar → 不动作。t288：pick-block 仅 Creative（创造式复制方块能力；生存无之 →
    // 不动作），Spectator 亦禁（与 canBreak/canPlace 同「观察者不交互」语义，不改栅格但属选择作弊）。
    // t291：优先「切槽」——hotbar 9 槽已有同 id 方块时切到该槽（setSelectedSlot，不动栈 / 数量）；
    // 仅 hotbar 全无该方块时才覆盖当前选中槽（setStack 满栈）。机制等价 MC 1.0 pick-block。
    Q_INVOKABLE void pickBlock();
    // t296 玩家受击击退（机制等价 MC 1.0 玩家被僵尸 / 箭 / 苦力怕爆炸击退 —— 命中后向被攻击方向小弹 + 水平推）。
    //   EntityManager.mobAttackedPlayer(amount, mobType, kbX, kbZ) 携「欲推开玩家的水平单位方向」，Main.qml
    //   Connections 据它调本方法。仅 Survival 生效（Creative/Spectator 无敌不弹；mobAttackedPlayer 经 t290 门控
    //   本就只在 Survival 发，此处再守防御）+ 非死亡 + 已捕获（菜单态不弹）。kbX/kbZ 由 caller 归一（EntityManager
    //   内已归一并兜底零向量）；本方法再防御性归一一次。击退写入独立冲量累加器 m_knockback（玩家 m_vel.x/z 每
    //   tick 被 wish 输入覆盖，无法存击退），step() 走路路径每帧衰减 + 重力 + 叠入位移。分层（PLAN §2）：
    //   Game/Physics 层持击退态；方向由 Entities 层（mob 位置 / 箭速）经语义信号向下传（Game→Physics 同层）。
    Q_INVOKABLE void applyHitKnockback(float dirX, float dirZ);
    // Q 键丢弃（t36）：从选中槽 takeStack 1 件 → 发 spawnItem（玩家前方 1.5 格）。仅指针捕获时生效
    // （spec）。空手 / 取失败 → 不丢。spawnItem 经 QML Connections 转发到 ItemEntityManager.spawnItem
    // （同破块掉落 t35 路径）；丢弃后实体可被重新拾取（闭环）。
    Q_INVOKABLE void dropHeld();
    // t229 Ctrl+Q 第一人称丢弃整栈：与 dropHeld（Q=丢 1 件）同源（取**选中槽**），差异在**丢整栈**而非 1 件。
    //   仅指针捕获时生效（同 dropHeld；背包打开时未捕获正是此场景，整栈丢弃走背包悬停槽路径）。
    //   空栈 / 取失败 → 不丢。spawnItem 携整栈数量（1 实体带整栈，同 dropHeldCursor 模式，修「丢 4 木棒
    //   捡回只剩 1」类 bug）。经 QML Connections 转发（同 dropHeld / dropHeldCursor）。
    Q_INVOKABLE void dropHeldStack();
    // t229 背包悬停槽丢弃原语：通用「在玩家前方 1.5 格丢弃指定 id/count 实体」。与 dropHeld（取选中槽）/
    //   dropHeldCursor（取光标手持栈）的差异：本方法**不读/改任何槽**，纯粹按给定 id/count 在玩家前方
    //   spawnItem ——槽位的读改由 UI 层（InventoryOps.readSlot/writeSlot，按组路由 main/hotbar/craft/in/
    //   fuel/out/chest）完成，本方法只负责实体生成 + 位置（Game/Physics 层语义，PLAN §2 分层：物理位置与
    //   实体事件在 Game 层，槽操作在 VM/UI 层）。这样背包任意组的悬停槽丢弃（Q=1件 / Ctrl+Q=整栈）都走
    //   同一原语，UI 层据组分发读写、算 1/全 栈量后调本方法。不限捕获态（背包打开时未捕获正是此场景）。
    //   id==0 / count<=0 → 不丢。经 QML Connections 转发（同 spawnItem 模式）。
    Q_INVOKABLE void dropItemAtFront(int itemId, int count);
    // 拖出背包丢弃（t49）：光标手持栈（hotbar.heldBlock/heldCount）整栈丢弃为实体（玩家前方 1.5 格）。
    // 与 dropHeld 的差异：后者取**选中槽** 1 件且仅捕获时；本方法取**光标手持栈**整栈、**不限捕获态**
    // （背包打开时未捕获正是此场景）。t64：spawnItem 传 heldCount → 1 实体携带整栈数量（修「丢 4 木棒
    // 捡回只剩 1」bug；spec 验收「4 木棒丢出捡回仍 4」）。清空 hotbar 光标手持栈（setHeldBlock(0) 同步清
    // count）。空手 → 不丢。经 QML Connections 转发（同 dropHeld）。
    Q_INVOKABLE void dropHeldCursor();
    // t228 右键拖出背包丢弃 1 件：与 dropHeldCursor 同源（光标手持栈 → 玩家前方 1.5 格实体），差异在**只丢 1 件**、
    //   余数留光标（右键 = 逐个丢弃，对齐 MC「右键拖出 = 每次丢 1」；左键整栈走 dropHeldCursor）。count 归 0 时连 id
    //   一起清（保持「id==0 ⟺ count==0」空栈不变式）。空手 / count<=0 → 不丢。spawnItem count=1。不限捕获态
    //   （背包打开时未捕获正是此场景，同 dropHeldCursor）。经 QML Connections 转发（同 dropHeld / dropHeldCursor）。
    Q_INVOKABLE void dropHeldCursorOne();
    // t175 死亡掉落：玩家死亡时把整个背包（hotbar 9 + main 27 + 光标手持栈）全部掉落为物品实体（**死亡点**
    //   = 玩家倒下时的脚底 m_pos，非出生点）+ 清空背包。每非空栈 → 1 实体携带整栈数量（同 dropHeldCursor
    //   模式，经 spawnItem 信号 → Main.qml 转发到 ItemEntityManager.spawnItem，单向事件流）。栈散布到死亡格
    //   3×3 邻域（ItemEntityManager 无水平速度，靠位置散布做视觉分离 / 便于玩家走回死亡点分别拾取）。
    //   清空走 resetForMode(Survival)（hotbar + main + held 全清 + bump revision → QML 同步）。
    //   由呈现层 onDied 路由调用（在 returnHeldToHotbar 之后：先把光标手持栈归还背包合并，再统一掉落；
    //   held 此时已空，本方法的 held 分支为防御双保险）。死亡只在 Survival 发生（fallDamage/suffocation 均
    //   Survival 路径），故 resetForMode(Survival) 清空语义正确。
    //   分层（PLAN §2）：Game/Physics 层发语义事件 + 操作自身持有的 Hotbar，呈现层只路由（不反向写数值）。
    Q_INVOKABLE void dropAllItems();
    // t78 重生定位：传回出生点（kSpawn）+ 清速度 / 挖掘态 / 飞行 / 蹲下疾跑（spec「立即重生」的定位部分）。
    //   由呈现层「立即重生」按钮调（与 PlayerState::respawn 配对：本方法管定位/物理态，PlayerState 管
    //   血量/死亡态）。出生点与构造期 m_pos 初值同源（kSpawn）；m_peakY 重置 → respawn 后下落从出生点
    //   起算（同 componentComplete 首帧，不误判陈旧落差）。emit positionChanged → 相机绑定重算跟随。
    Q_INVOKABLE void respawn();
    // t176 存档加载：从存档恢复玩家位姿 + 模式（spec「玩家态 pos/.../模式」）。一次设 m_pos/m_yaw/m_pitch/
    //   m_mode + 清速度 / 挖掘 / 飞行 / 蹲疾跑 + 重置 m_peakY（防存档点与首次重力结算间误判落差）。emit
    //   positionChanged/yawChanged/pitchChanged/modeChanged → 相机 / 第三人称模型绑定刷新。与 respawn 的差异：
    //   respawn 回固定出生点，本方法回存档任意点（玩家上次保存位置）。mode 取 Mode 序数（0/1/2）。
    Q_INVOKABLE void loadSavedState(float x, float y, float z, float yaw, float pitch, int mode);
    // t238 设饥饿值（存档加载用；与 PlayerState.setHunger 配对）：clamp 到 [0, kMaxHunger]；同步本类的
    //   Physics 层饥饿累积器 m_hunger + emit hungerUpdated（让 Main.qml 路由到 playerState.setHunger 把
    //   Game 层显值与 Physics 层值对齐——存档只持久化 playerState.hunger，本方法把同一值灌回 Physics 层
    //   镜像，使两层数据一致、 depletion 从存档值起算）。无变化（值 == 当前）静默。
    Q_INVOKABLE void setHunger(int value);

signals:
    void worldChanged();
    void hotbarChanged();
    void itemEntitiesChanged();
    void entityManagerChanged();
    void worldClockChanged();
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
    void eyeInWaterChanged(); // t201 眼位水态翻转（驱动水下蓝滤镜叠层显隐；值真变才发，免每帧抖 QML 绑定）
    void feetInWaterChanged(); // t269 脚位水态翻转（驱动水中走路声分流；值真变才发，免每帧抖 QML 绑定）
    void flowSoundLevelChanged(); // t223 近流水 proximity 强度变（驱动 AudioManager 水流声 start/stop/setLevel）
    void moveSpeedChanged();  // 行走速度变（t45；驱动 QML walkBlend 切换 + 摆频）。speed 属性亦复用本信号（t159）。
    void flySpeedMulChanged(); // 飞行速度倍数变（t159 滚轮调速；驱动 F3 报当前有效飞速）
    void walkPhaseChanged();  // 行走相位推进（t45；走时每 tick 发，QML 据 sin() 算四肢欧拉角）
    void perfChanged();       // t178：~1s 窗口 tick() CPU 耗时平均刷新（驱动 F3 帧时间切分重绑）
    void moveStateChanged();  // 移动态切（Walk/Sprint/Crouch；t51；驱动 QML 摆幅 + 速率因子）
    void hitChanged();
    // t253 攻击单体选中：准星瞄准的目标 mob 索引变（updateRaycast 每帧刷新；驱动 QML 目标框显隐 / 跟随）。
    //   仅值真变（命中新 mob / 失瞄 / 暂停清零）才发，免每帧抖 QML 绑定。分层（PLAN §2）：Game 层暴露
    //   选中态，呈现层只读消费（同 hasHit→线框 模式）。
    void targetedMobChanged();
    void selectedBlockChanged();
    void selectedItemChanged(); // 手持原始 id 变（含工具段切换；驱动 t34 速度重算）
    void miningStateChanged();  // mining / miningStage / miningBlock 三者同变（一次性发，少抖动）
    void miningProgressChanged(); // 进度连续变化（HUD 可选显示百分比；高频，独立信号）
    // 挖掘过程碎屑粒子（t61）：生存累积挖掘时每跨一阶（progress 推进触发 stage 1..5 切换）发一次，
    // 携被挖方块坐标 + id（呈现层据此迸发少量对应色碎屑，复用破块 emitter / 重力）。破块完成时
    // 的「+30% 大迸发」仍走 World 的 blockBroken → burstBreak（已在此任务内 +30%），本信号仅驱动
    // 「挖的过程中」的进度反馈粒子。创造瞬破不进累积态 → 不发；仅 Survival 推进 stage 时发。
    // t165：本信号**只驱动碎屑**（仅可挖方块 —— 基岩不破无碎屑）；挖掘音改由下方 miningSound 统一发。
    // 分层（PLAN §2）：Game/Physics 层发语义事件，呈现层只消费（同 blockBroken→粒子 / swingArm 模式）。
    void miningParticle(int x, int y, int z, int blockId);
    // 挖掘击打音（t165）：生存累积挖掘每跨一节拍（progress 推进触发 beat 切换）发一次，携被挖方块 id。
    //   **所有**被挖方块（含不可挖基岩）均发 —— spec「生存基岩可持续挖 ... 保持 mining 态挥臂+音」要求
    //   基岩 hold-mine 也持续有节奏的挖掘音反馈（机制等价 MC 镐撞基岩响一声）。呈现层 Connections 接
    //   AudioManager.playMining（按 id 材质组选 clip）。与 miningParticle 解耦：音走本信号（含基岩），
    //   碎屑走 miningParticle（仅可挖）。创造瞬破不进累积态 → 不发。分层同 miningParticle。
    void miningSound(int blockId);
    // t267 进食态翻转（开始 / 结束进食）：驱动 QML viewModelHand 下沉 + 抖动动画启停。
    //   分层（PLAN §2）：Game/Physics 层发语义事件，呈现层只消费（同 miningStateChanged 模式）。
    void eatingStateChanged();
    // t267 进食进度连续变化（0..1）：高频独立信号（同 miningProgressChanged；当前仅驱动 beat 推进，
    //   留 hook 供未来 HUD 进度条 / 抖动频率绑定）。值真变才发语义；此处每推进 tick 发（驱动 beat 跨阶判定）。
    void eatingProgressChanged();
    // t304 弓拉弓态翻转 / 蓄力进度变（驱动 viewModelHand 拉 / 弓动画启停 + 进度跟随）。
    void bowDrawChanged();
    // t267 进食屑粒（持面包按住右键累积进食时每跨一节拍发一次）：携嘴部世界坐标（= 玩家眼位 position()，
    //   float 坐标非方块格 —— 进食屑粒从玩家嘴部迸发而非方块中心）。呈现层 Connections 转发到
    //   BlockParticles.burstEat 迸发少量屑粒（机制等价 MC 进食屑粒）。分层同 miningParticle。
    void eatingParticle(float x, float y, float z);
    // 拾取掉落实体（t118 / t120）：pickupScan 把实体入背包（addToAny 成功入栈，无论全 / 部分）时发；
    // id = 物品 id、count = 本次实际拾取数（have - leftover；spec「拾取后销毁」的「拾取」语义事件）。
    // 全满装不下（leftover == have）不发（无拾取发生）。t118 据此 → AudioManager.playPickup（拾取音）；
    // t120 后续亦据此驱动手弹跳动画（同一事件多消费者，分层干净）。分层（PLAN §2）：Game 层发语义
    // 事件，呈现层 / 音频层只消费（同 miningParticle / swingArm 模式）。
    void itemPickedUp(int itemId, int count);
    // 玩家挖掘产出（t34 → t35）：生存破块时按 ToolRegistry::canHarvest 判 drop；创造 drop=false
    // （瞬破不掉落，spec）。t35 据此 spawn item entity；当前任务仅发信号、消费端未接（无副作用）。
    // blockBroken（World 已发）仍触发粒子；本信号额外区分「玩家挖」+「是否掉落」（创造 / 不可采掘
    // 不掉）。坐标 + 原方块 id。
    void playerMined(int x, int y, int z, int blockId, bool drop);
    // 方块掉落实体（t35）：生存破块且 ToolRegistry::canHarvest 判定掉落（drop=true）时发；
    // 创造瞬破（drop=false）/ 不可采掘（canHarvest=false，如空手破石）不发。坐标 = 被破格整数
    // 坐标，id = 原方块 id，count = 掉落数量（走 BlockRegistry::dropCount；t64）。Main.qml Connections
    // 转发到 ItemEntityManager.spawnItem 生成实体。分层（PLAN §2）：Game/Physics 层发语义事件，
    // 呈现层 / ViewModel 只消费（同 blockBroken→粒子）。
    void spawnItem(int x, int y, int z, int blockId, int count);
    void fallDamageTaken(int hp); // 生存掉落伤害（t22）：着地结算，正值才发；呈现层路由到 PlayerState（t160 窒息 / t202 溺水复用此路径）
    // t238 饥饿回血（仅 Survival，饱腹态）：饥饿充足（>= kRegenHungerThreshold）且未满血时，每
    //   kHungerRegenInterval 秒发本信号携 1HP → 呈现层 Connections 路由到 PlayerState.heal（与 fallDamageTaken
    //   → takeDamage 反向配对：扣血走 fallDamageTaken、回血走 healed；同 airUpdated→setAir 模式）。
    //   机制等价 MC 1.0 hunger≥18 自动回血，让「食面包」真有生存收益（否则系统只能「不饿死」无法回血）。
    void healed(int hp);
    // t202 气泡值更新（仅 Survival）：眼位入水逐格减 / 出水逐格回满时发，携**新的 air 值**（0..10）。
    //   呈现层 Connections 路由到 PlayerState.setAir（同 fallDamageTaken→takeDamage 模式：Physics 层算
    //   时序、Game 层持显值、呈现层路由）。溺水扣血复用 fallDamageTaken(1)（→ takeDamage → damaged 红
    //   闪 / 视角晃，与窒息同链）。值真变（减 / 增一格气泡）才发，免每 tick 抖 QML 绑定。
    void airUpdated(int air);
    // t238 饥饿值更新（仅 Survival 推进；食用面包在所有模式都发）：Physics 层 m_hunger 推进 / 食用恢复
    //   时发，携新的 hunger 值（0..maxHunger）。呈现层 Connections 路由到 PlayerState.setHunger（同 airUpdated
    //   → setAir 模式：Physics 层算时序 / 食用事件、Game 层持显值、呈现层路由）。值真变才发，免每 tick 抖
    //   QML 绑定。饥饿归零后扣血复用 fallDamageTaken(1)（→ takeDamage → damaged 红闪 / 视角晃，与窒息 /
    //   溺水同链，spec「到 0→开始扣血，复用 takeDamage 链」）。
    void hungerUpdated(int hunger);
    // 第一人称手挥动（t29）：breakBlock/placeBlock 在通过模式门控 + 动作真发生后发（观察者不发；
    // 未命中/放置被拒也不发）。同 blockBroken 模式——Game/Physics 层发语义事件，呈现层 Connections
    // 消费启动手臂挥动动画（PLAN §2 分层：手 viewmodel 属呈现层，绝不反向写）。
    void swingArm();
    // 右键工作台（t50）：placeBlock 检测到命中格为 CraftingTable → 发本信号（不放置）→ 呈现层
    // Connections 打开 3×3 工作台 UI（释放指针 / 关包归还合成栏）。机制等价 MC 右键工作台开合成界面。
    // 分层（PLAN §2）：Game/Physics 层发语义事件，呈现层只消费（同 spawnItem / swingArm 模式）。
    void craftingTableOpened();
    // 右键熔炉（t87）：placeBlock 检测到命中格为 Furnace → 发本信号（不放置）→ 呈现层 Connections
    // 打开 FurnaceUI（释放指针）。机制等价 MC 右键熔炉开冶炼界面。同 craftingTableOpened 模式：
    // Game/Physics 层发语义事件，呈现层只消费（PLAN §2 分层）。熔炉槽状态 / 冶炼 tick 由 FurnaceUI
    // 自持（面板常驻、visible 切换；WorldClock.ticked 驱动 tick）。
    void furnaceOpened();
    // 右键箱子（t173）：placeBlock 检测到命中格为 Chest → 发本信号（不放置；携命中格世界坐标）→ 呈现层
    //   Connections 打开 ChestUI（释放指针 + 启动盖子开合动画）。机制等价 MC 右键箱子开物品栏。同
    //   craftingTableOpened / furnaceOpened 模式：Game/Physics 层发语义事件（携坐标供 ChestStore 寻址该
    //   箱子的 27 槽），呈现层只消费（PLAN §2 分层）。坐标 = 玩家所点箱子格的整数世界坐标。
    void chestOpened(int x, int y, int z);
    // 火把放置（t125 朝向修正）：placeBlock 成功放置 Torch 后发，携带玩家点击面的外法线（指向玩家侧，
    //   = m_hitNx/Ny/Nz）。呈现层（torchHost）据此把火把定向为「柄嵌玩家所点墙面」——替代旧 recomputeOrient
    //   固定优先级（下>-X>+X>-Z>+Z）：旧逻辑在「墙+地并存」（墙插火把下方恰有地面）时误判垂直立柱，
    //   违背玩家点击墙面的意图。法线为原始几何量，定向串（up/px/nx/pz/nz）由呈现层推导（PLAN §2 分层：
    //   Game 层只发几何语义事件，呈现决定如何画；同 swingArm / spawnItem 模式）。
    void torchPlaced(int x, int y, int z, int nx, int ny, int nz);
    // 门 / 活版门开合（t152）：右键 useBlock（命中格为门/活版门）翻开合态后发，open=true=开（→ playDoorOpen）、
    //   false=关（→ playDoorClose）。机制等价 MC 右键门/活版门开关声。与 swingArm 同发（开合也是一次「使用」
    //   动作，挥手），呈现层 Connections 路由 open 到 AudioManager.playDoor*（音频层只消费，PLAN §2 分层）。
    //   spec「useBlock 发 doorToggled(open) 信号 → Main.qml 路由」。门两格同翻时只发一次（玩家点的是其中一格，
    //   配对格被动跟随；一次开合动作 = 一次音）。
    void doorToggled(bool open);
    // t242 玩家攻击 mob（spec「玩家左键攻击生物→受伤音效」）：beginMining 在通过模式门控后、破块前
    //   先做 findMobHit；命中活体 mob 且（无方块命中 OR mob 比方块更近）→ 走攻击路径（damageEntity +
    //   swingArm）替代破块，并发本信号。呈现层 Connections 路由到 AudioManager.playMobHurt（t248 专属 mob
    //   受击声 mob_hurt.wav，替代旧复用 hurt.wav 的玩家受伤声——spec「受击音换专属 mob 受伤声」）。
    //   同 swingArm / blockBroken 模式：Game/Physics 层发语义事件，呈现 / 音频层只消费（PLAN §2 分层）。
    //   t249 crit=true 表示本次为暴击（玩家跳劈下落中命中）：呈现层可据此播放更强的受击音 / 暴击粒子
    //   （spec 仅要求 +50% 伤害已由 attackMob 内 baseDmg*3/2 落地；反馈音 / 视觉属可选增强，留 hook）。
    //   t295 mob 受击音效 + 敌对专属：mobType = 被攻击 mob 的子类 id（MobTest/Pig/Cow/Sheep 0-3 被动；
    //     Shambler/Bones/Stalker/Spider 4-7 敌对）。attackMob 命中后从 EntityManager::mobTypeAt 取该值随本信号
    //     下传，呈现层据此路由到 AudioManager.playMobHurt(mobType) —— 被动用通用 creature yelp（mob_hurt.wav）、
    //     敌对各走专属音（复用其 ambient idle clip：Shambler 哀嚎 / Bones 骨头敲击 / Spider 蜘蛛嘶 / Stalker 嘶嘶；
    //     机制等价 MC 敌对受击声与其 idle 叫声同族；Stalker 爆炸专属音走 playExplosion 的 detonation 路径）。
    //     spec t295「敌对各:骨头敲击/蜘蛛嘶(近)/僵尸哀嚎/苦力怕爆炸声」。mobType 由 Game/Physics 层（持
    //     EntityManager）取得，向下经语义信号传呈现层，不反查（PLAN §2 分层）。
    void mobAttacked(int mobType, bool crit);

protected:
    void componentComplete() override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void onWindowChanged(QQuickWindow *win);
    void tick();

private:
    // t178：tick() 包一层计时（累加主线程 CPU 耗时，~60 tick 算 1s 平均 → m_simMs → emit perfChanged），
    //   实体逻辑放 tickImpl（原 tick body）。tick 仍是 QTimer slot（连接不变）。
    void tickImpl();
    void pollMouse();
    void step(qreal dt);
    // t223 近流水 proximity 扫描：在玩家眼位周围 kFlowSoundRadius 盒内查最近**流动水**格（Water 且 state>0；
    //   静水水源 state=0 不算），返回 [0,1] 强度（1=贴脸 / 0=范围外或无流水）。节流由 tickImpl 累加 dt 控制
    //   （kFlowScanInterval）。只读 World::blockAt/stateAt（向下依赖）；无世界 → 0。
    float scanFlowSoundLevel() const;
    QVector3D wishHoriz() const;
    void moveAxis(int axis, float amount);
    bool aabbHitsSolid() const;
    void setCaptured(bool c);
    QPoint windowCenterGlobal() const;
    QVector3D lookDirection() const;             // 视线方向（与相机 eulerRotation 同源）
    void updateRaycast();                        // 每帧沿视线 DDA，更新命中态
    void updateCameraDistance();                 // 每帧算第三人称相机距离（钳制防穿墙，t40）
    void clearHit();                             // 暂停/失焦时隐藏线框
    // t146 放置校验：按「将放置方块的实际形状 sub-AABB」判是否与玩家 AABB 相交（不完整方块可能只占
    //   半格，玩家在另半格内仍可放；air/torch 无 sub-AABB → 不挡）。id/state = 放置态（slab 据命中面、
    //   stairs/door 据朝向）。与碰撞（overlapSubAABBs）共用 collisionAABBs 单一权威。
    bool overlapsPlayerAABB(int bx, int by, int bz, quint8 id, quint8 state) const;
    // t146 玩家 AABB vs 世界碰撞 sub-AABB 重叠测试（3 轴严格重叠）。axis∈{0,1,2} 时记录沿该轴的相交
    //   sub-AABB 表面（outMinSurf/outMaxSurf）供 moveAxis 贴面；axis<0 仅判命中（aabbHitsSolid 用）。
    //   取样范围与旧 aabbHitsSolid 同策略（floor(min)..ceil(max)-1，严格重叠排除仅贴面）。
    //   t161 修：maxSurfCap 仅把 bmax<=cap 的块顶计入 maxSurf（向下着地用 cap=pyBefore → 只取「玩家原本站
    //   其顶上」的可着陆面，忽略沙等 bury 块；默认 +inf=取全部，旧行为）。outHasMax=true 当至少有一个
    //   合格 bmax 计入（区分「有碰撞但无可着陆面=纯 bury」与「有可着陆面」）；默认 nullptr 兼容旧调用。
    bool overlapSubAABBs(int axis, float *outMinSurf, float *outMaxSurf,
                         bool *outHasMax = nullptr,
                         float maxSurfCap = std::numeric_limits<float>::max()) const;
    // t161 嵌入挤出：玩家被下落沙 / 放置方块「包裹」时，沿最近开放水平方向把玩家推出（向外 not 向上，
    //   无需按键）。判据用「玩家 XZ 中心所在列 + AABB 真重叠」→ 仅 burial 触发；正常贴墙 / 站立时中心
    //   列为玩家占据的空气 → 不触发。4 向皆堵（全包裹）→ 不挤（交 t160 窒息扣血兜底）。配合 moveAxis(1)
    //   嵌入不上抬（fab580e）共同兑现 spec「挖沙柱前行不上爬；被覆盖向外（未堵侧）挤出 not 向上」。
    void extrudeEmbedded();
    // t258 被埋锁定检测（spec「被埋→锁定不能动，只能挖出脱困」）：玩家被实体方块完全包围（中心列嵌入
    //   + 四向水平邻列全高皆堵）→ 无水平出路 = 锁定态。step() 据此在 moveAxis 之前先验门控（velocity 清零
    //   → delta=0 → moveAxis 全 amount==0 早退），根本不给 moveAxis 的 snap 把玩家推穿相邻块的机会
    //   （穿出 bug 根因：嵌入态下 snap 到「被嵌块表面」可推向反方向 / 穿入邻块 = 前后左右穿出 / 坠出基岩外，
    //   机制等价观察者 noclip；Y 轴已由 moveAxis(1) 纯 bury 回退防坠，水平在此先验门控）。与 extrudeEmbedded
    //   的「全包裹→不挤」条件同源，但 extrudeEmbedded 在 moveAxis 之后跑（事后挤出，有开放侧才推），
    //   本方法在 moveAxis 之前跑（先验锁定）。挖掘（raycast→beginMining）不经位移 → 不受锁定影响，玩家
    //   仍可挖出卡住的方块脱困（spec「只能挖出脱困」）。只读 World（向下依赖）。
    bool isLockedBuried() const;
    // 移动状态速率因子（t51）：Sprint×1.3 / Crouch×0.4 / Walk×1.0。仅走路模式水平速度乘此值
    //   （飞 / 观察者 noclip 恒 1，状态机不进入 Sprint/Crouch）。同时驱动 moveSpeed 报告 → walkPhase 频率。
    float speedMul() const;
    // t174 脚位水中判定（已上移为 public Q_PROPERTY feetInWater，t269）：玩家脚底格 == Water（m_pos 整数坐标）。
    //   浮力 / 游泳 / 水流推动物理用它（眼位高于水面时仍能游，机制等价 MC「在水中游泳」= 脚或身在水中即可）。
    // t159 上报实际水平速度（speed 属性）：据 step 出口位移 / dt 算水平标量，值真变（> 阈值）才发
    //   moveSpeedChanged（speed 复用此 NOTIFY）。各飞 / 走出口前调一次。dt<=0 → no-op。
    void reportHorizSpeed(const QVector3D &posBefore, qreal dt);
    // 切移动态（t51）：同步更新 AABB 高 / 眼位（蹲下 1.5/相机随之降低；站起 1.8/1.62）。无变化静默。
    void setMoveState(MoveState s);
    // 蹲下「边缘安全」（t51）：给定水平位置 (x,z) 在当前脚位下方是否有支撑方块（脚底 0.05 处那一格
    //   在 AABB footprint 内任一列实体即算支撑）。step() 据此判定「蹲下时若水平移动后脚下将无方块
    //   则不水平移动」（防走下方块边缘）。仅读 World（isSolid），与碰撞同层。
    bool hasGroundBelowAt(float x, float z) const;
    // t134 玩家水平朝向（据 yaw 推 4 向）：前向 = (-sin(yaw), -cos(yaw))（与 wishHoriz/lookDirection 同源）。
    //   返回 0=+X 1=-X 2=+Z 3=-Z（与不完整方块 state 朝向编码一致：stairs/door/trapdoor 均用此编码）。
    //   供 placeBlock 放 stairs/door 时定朝向、useBlock 开 trapdoor 时定开向。
    int horizontalFacing() const;
    // t234 耕地湿润判定（spec「水源邻近判定湿润」）：给定耕地格 (x,y,z)，扫描水平半径 kFarmlandWaterRadius
    //   格 + 本层 / 下一层（y / y-1，机制等价 MC 耕地被同高或低 1 层的水滋润）有无 Water 方块。命中 → 湿润
    //   （state bit0=1，mesher 顶面贴 farmland_wet），否则干燥（farmland_dry）。仅耕地（placeBlock 锄头分支）
    //   时调一次（快照判定，非动态补水）。只读 World::blockAt（向下依赖）；无世界 → false（干态兜底）。
    bool isFarmlandMoist(int x, int y, int z) const;
    // 持续挖掘（t34）：每 tick 累积进度 / 检目标变更 / 完成时破块。由 tick() 调（captured 时）。
    void updateMining(float dt);
    // 清掉累积态（松开 / 换目标 / 失焦 / 完成）。无变化时静默（不发信号）。
    void cancelMining();
    // t267 持续进食：每 tick 累积进度 / 检持物变更 / 跨节拍发屑粒 / 完成时消耗面包。由 tick() 调（captured 时）。
    //   机制等价 MC 1.0 长按右键食面包：progress 增量 = dt / kEatDuration（~1.6s 满）。
    void updateEating(float dt);
    // t267 清进食累积态（松开 / 换槽 / 失焦 / 完成）。无变化时静默（不发信号，免抖动 QML 绑定）。
    void cancelEating();
    // t264 清弓拉弓累积态（松开射出后 / 换槽（持物不再是弓）/ 失焦 / 暂停）。无拉弓态时静默（不发信号）。
    void cancelBowDraw();
    // t304 在背包（hotbar 9 + main 27）查首格含箭（ArrowId）的 (group,index)，找不到返 {false,0,0}。
    //   group=0 hotbar / 1 main。供 fireArrow 判「需箭在背包」（spec）+ 生存消耗 1 箭定位槽。
    struct ArrowSlot { bool found; int group; int index; };
    ArrowSlot findArrowInInventory() const;
    // t267 完成（progress 满）：消耗 1 面包 + 恢复饥饿（kBreadHungerAmount）+ 清态。Survival 消耗 / Creative 不耗。
    void finishEating();
    // 完成（progress 满）：写 air + 发 playerMined + 清态。drop 由 caller 算（生存走 ToolRegistry）。
    void finishMiningAt(int x, int y, int z, bool drop);
    // t214 破块后扫 6 邻火把：若其**附着格**（state 编码，BlockRegistry::torchAttachOffset）已非 solid
    //   （含本格刚被置 Air）→ 火把直接掉落为物品（不「粘」到附近其它 solid 邻居）。机制等价 MC「火把
    //   附着面被移除即脱落」。火把非 solid → 不撑他火把 → 单趟扫即足够（无级联）。
    void dropUnsupportedTorchesAround(int x, int y, int z);
    // t247 草丛 / 小麦作物掉落产出（玩家破块 / 失撑共用）：把 WheatCrop（按 state 判成熟，t237 收割：
    //   成熟掉 1 小麦物品 + 1-2 种子、未成熟仅 1 种子）/ TallGrass（1/kTallGrassSeedDropDenom 概率掉种，
    //   t246）的 spawnItem 计算收敛到此 → finishMiningAt 与 dropUnsupportedCropsAround 共用，**失撑掉落
    //   与玩家破块产出同源**，零分支漂移。id 非两者 → no-op（caller 误调防御）。state 须为 setBlock(Air)
    //   前快照（t134 时序：setBlock 委托 5 参数版以 state=0 写入，之后 stateAt 永返 0 → WheatCrop 成熟
    //   判定失效，须先读）。分层同 spawnItem（Game/Physics 发语义事件，呈现层 / ViewModel 只消费）。
    void dropCropDrops(int x, int y, int z, quint8 id, quint8 state);
    // t305 树叶掉落产出（玩家破叶专用）：破 Leaves 时按概率掉树苗物品（SaplingItemId）+ 木棒（StickId）。
    //   机制等价 MC 1.0 破叶掉落（5% 树苗 / 2% 木棒；本工程对齐概率）。自然衰减（decayLeavesAround）不掉落
    //   （spec「树叶消失」），仅玩家破叶走本分支。两物品散布到破格 + 非实体水平邻格做视觉分离（同 WheatCrop
    //   / 双半砖模式）。概率走 QRandomGenerator（玩家交互掉落随机性，非 worldgen 确定性范畴 §2-K）。同
    //   dropCropDrops 模式：特例掉落在通用 BlockDef 表之上提前分流（Leaves.dropId=0 兜底，本分支覆盖）。
    void dropLeafDrops(int x, int y, int z);
    // t247 草丛 / 小麦作物失撑掉落（spec「挖底方块→其上草方块/小麦应掉落（现悬空）；草根+作物须依附
    //   下方实体方块」）：破块后查**正上方格**，若为 TallGrass / WheatCrop（其唯一支撑 = 下方实体方块，
    //   刚被破为 Air）→ 作物直接掉落为对应产出（setBlock(Air) + dropCropDrops），不再悬空。机制等价 MC
    //   「草丛 / 作物下方方块移除即脱落」。同 dropUnsupportedTorchesAround 模式：仅玩家破块触发（blockBroken
    //   链）；TallGrass / WheatCrop 非 solid 且不作他物支撑 → 单趟向上扫即足够（无级联，破一块不会链式
    //   掉一串）。掉落产出与玩家破块同源（dropCropDrops 共用，成熟小麦失撑仍掉小麦 + 种子）。
    void dropUnsupportedCropsAround(int x, int y, int z);
    // t242 攻击 mob（spec「玩家左键攻击生物」）：damageEntity(entityIndex, dmg) + swingArm +
    //   emit mobAttacked。t265 伤害改走 ToolRegistry::attackDamage(手持物)（剑木4/石5/铁6、空手/工具=1 HP）。
    //   由 beginMining 在 mob 优先于方块时调。mob 由 caller 选定（findMobHit 已返最近活体索引）。
    //   EntityManager.damageEntity 内已做边界 / dead / 非 Mob 守卫 + bump revision + emit mobDied（死亡掉落）。
    void attackMob(int entityIndex);
    // 拾取扫描（t36）：每帧扫附近掉落实体 → Hotbar.addStack（先选中槽、再空槽）。addStack 返 0
    // （全入）→ ItemEntityManager.removeAt 销毁实体；返 >0（背包满）→ 不拾取（entity 留）。
    // 距离从玩家 AABB 中心（脚底 + 半高）3D 起算，阈值 kPickupDist；从后往前扫便于 erase。
    void pickupScan();
    // t137 出生贴地表：查出生列 (kSpawnX,kSpawnZ) 的 worldgen 地表高度 → 把脚底 Y 设为 h+1（站地表方块
    //   上方）+ 同步 m_peakY 防误判落差。kSpawnY=44 是高于最高地表(~40)的兜底初值（防卡地形），但玩家从
    //   44 摔到地表（落差 >3）会触发摔伤；本方法在世界就绪后把玩家贴真实地表，消除出生落差。分别在
    //   componentComplete / setWorld / respawn 调，确保世界（width/height/seed）定稿后玩家始终贴地表。
    //   无世界 → no-op（m_pos 保持 kSpawnY 兜底）。只读 World::heightAt（向下依赖，不改栅格）。
    void snapSpawnToGround();

    World *m_world = nullptr;
    Hotbar *m_hotbar = nullptr;                  // 拾取 addStack / 丢弃 takeStack 的栈操作目标（Q_PROPERTY 绑定）
    ItemEntityManager *m_itemEntities = nullptr; // 拾取扫描数据源 + removeAt 销毁（Q_PROPERTY 绑定）
    EntityManager *m_entityManager = nullptr;    // 统一实体（t95 测试生物）：重力 tick + 玩家推动（Q_PROPERTY 绑定）
    WorldClock *m_worldClock = nullptr;          // t280 黑暗刷怪：读 skyLight 驱动敌对 spawn / 燃烧（Q_PROPERTY 绑定）
    QQuickWindow *m_window = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_evtClock; // 事件时间戳（双击检测；不被 tick restart）
    // t178 帧时间切分：累加每 tick 的主线程 CPU 耗时（ns），每 ~60 tick（≈1s@60Hz）算平均写 m_simMs + emit。
    float m_simMs = 0.0f;
    qint64 m_simAccumNs = 0;
    int m_simTickCount = 0;

    // 出生点（t78 重生定位）：与构造期 m_pos 初值同源，respawn() 传回此处。脚底中心坐标。
    // 必须声明在 m_pos 之前（m_pos 默认成员初始化器引用本常量；C++ 不允许前向引用）。
    // t119：世界高度 16→64、地表重定标到 16..40 → 出生 Y 抬到 44（高于最高地表 40，玩家落 / 浮于地表之上，
    //   不会卡进地形）。默认 Spectator 模式无重力（漂浮）；切重力模式时 setMode 重置 m_peakY 防误判落差。
    // t276：出生点跟随大世界网格居中——世界边长 = worldChunksPerSide×16（默认 10×16=160）→ 居中 = 80。
    //   X/Z 取「边长/2」使玩家落在世界中心而非贴角（与 Main.qml worldChunksPerSide 单一权威对齐；改网格
    //   尺寸时同步改此处）。Y 不随边长变（地表高度由 heightAt 决定，kSpawnY 仅兜底）。
    static constexpr float kSpawnX = 80.0f; // t276：大世界(160×160)居中（原 40 = 5×5/80×80 中心）
    static constexpr float kSpawnY = 44.0f;
    static constexpr float kSpawnZ = 80.0f; // t276：大世界(160×160)居中
    QVector3D m_pos{kSpawnX, kSpawnY, kSpawnZ}; // 脚底（= 出生点；respawn 传回此处，t78）
    QVector3D m_vel{0, 0, 0};
    float m_yaw = 0, m_pitch = -42;
    Mode m_mode = Spectator;
    CameraMode m_cameraMode = FirstPerson; // F5 相机模式（默认第一人称，t27）
    float m_cameraDistance = 0.0f;         // 第三人称相机距离（钳制后；FirstPerson 恒 0；t40）
    bool m_captured = false, m_onGround = false;
    QHash<int, bool> m_keys;
    bool m_flying = false;          // 创造模式飞行子状态（双击空格切换；进创造默认走）
    float m_moveSpeed = 0.0f;       // 当前行走速度（仅走路模式非零；驱动 QML 腿/臂摆动频率，t45）
    float m_horizSpeed = 0.0f;      // 实际水平速度（blocks/sec；F3 报它；位移/dt 算；t159）
    float m_walkPhase = 0.0f;       // 行走相位（弧度；走时累加、2π 回绕；供 QML sin() 算四肢摆角，t45）
    float m_flySpeedMul = 1.0f;     // 飞行速度倍数（默认 1.0；滚轮 ±档调速；有效 = clamp(kFly*mul,4,20)；t159）
    MoveState m_moveState = Walk;   // 移动态（t51；Walk/Sprint/Crouch；仅走路模式有效，飞/Spectator 恒 Walk）
    float m_height = kHeight;       // 当前 AABB 高（蹲下变 kCrouchHeight=1.5；默认 kHeight=1.8；t51）
    float m_eyeHeight = kEyeHeight; // 当前眼位（蹲下降低到 kCrouchEye；相机 position 据此 → 蹲下相机降低，t51）
    qint64 m_lastSpaceMs = -100000; // 双击空格检测时间戳
    qint64 m_lastWms = -100000;     // W 双击检测时间戳（疾跑触发；t51）
    // 放置节流时间戳（t128）：上次成功放置的 m_evtClock 时间戳。placeBlock 入口据此判 200ms CD
    //   （5 次/秒），防连点放沙等触发多次塌落链溢出（spec t128）。仅成功放置后刷新；初值 -100000
    //   = 远古 → 首次放置不受限。与 m_lastSpaceMs/m_lastWms 同走 m_evtClock（事件时间戳，不被 tick restart）。
    qint64 m_lastPlaceMs = -100000;
    bool m_spacePrev = false;       // 跳跃边沿触发（长按空格只跳一次）
    int m_selectedBlock = BlockRegistry::Stone; // 当前手持方块（右键放置；默认 Stone，t06 hotbar 绑定）
    int m_selectedItem = BlockRegistry::Stone;  // 手持物品原始 id（含工具段；t34 挖掘速度用，绑定 hotbar.selectedItemId）
    float m_peakY = 0.0f;           // 滞空期间最高点 Y（掉落伤害结算基准；componentComplete 设为脚底 Y）
    float m_suffocationTimer = 0.0f; // t160 窒息累积计时（身体嵌实体方块时累加，每 kSuffocationInterval 秒一脉冲）
    // t202 气泡 + 溺水计时（仅 Survival）：眼位入水 m_airTimer 累加 → 每 kAirInterval 减 1 气泡；
    //   气泡归零后 m_drownTimer 累加 → 每 kDrownInterval 扣 1HP（复用 fallDamageTaken→damaged 链）；
    //   出水 m_airRegenTimer 累加 → 每 kAirRegenInterval 回 1 气泡。m_air 是 Physics 层累积器（权威），
    //   PlayerState.air 是 Game 层显值镜像（经 airUpdated 同步）。respawn / loadSavedState 复位 m_air + 三计时器。
    int m_air = kMaxAir;          // 当前气泡（0..kMaxAir；构造满，同 PlayerState::kMaxAir）
    float m_airTimer = 0.0f;      // 入水减气累积
    float m_drownTimer = 0.0f;    // 气泡归零后溺水扣血累积
    float m_airRegenTimer = 0.0f; // 出水回气累积
    // t238 饥饿系统（仅 Survival 推进；食用在所有模式都生效）。m_hunger 是 Physics 层累积器（权威），
    //   PlayerState.hunger 是 Game 层显值镜像（经 hungerUpdated 同步）。depletion 由 step(dt) 推进：
    //   m_hungerDepleteAccum 按「idle / walk / sprint」速率累加 dt，每满 1.0 扣 1 饥饿（运动加速消耗，
    //   spec「饥饿随时间/运动掉落」）。m_starveTimer 在 m_hunger==0 时累加 → 每 kStarveInterval 扣 1HP
    //   （复用 fallDamageTaken→takeDamage→damaged 链，spec「到 0→开始扣血」）。m_regenTimer 在 m_hunger
    //   充足（>= kRegenHungerThreshold）且未满血时累加 → 每 kHungerRegenInterval 回 1HP（机制等价 MC 1.0
    //   饥饿回血；让「食面包」能真回血，否则系统只能「不饿死」无法回血）。respawn / loadSavedState 复位
    //   m_hunger + 三计时器。非 Survival（Creative/Spectator）m_hunger 锁满 + 计时器归零。
    int m_hunger = kMaxHunger;            // 当前饥饿（0..kMaxHunger；构造满，同 PlayerState::kMaxHunger）
    float m_hungerDepleteAccum = 0.0f;    // 饥饿消耗累积（按 dt × 速率累加，每满 1.0 扣 1）
    float m_starveTimer = 0.0f;           // 饥饿归零后扣血累积（每 kStarveInterval 扣 1HP）
    float m_regenTimer = 0.0f;            // 高饥饿时回血累积（每 kHungerRegenInterval 回 1HP）
    bool m_eyeInWater = false;       // t201 眼位水态缓存（tickImpl 每 tick 重算对比，翻转才 emit eyeInWaterChanged）
    bool m_feetInWater = false;      // t269 脚位水态缓存（tickImpl 每 tick 重算对比，翻转才 emit feetInWaterChanged）
    // t223 近流水 proximity 水流声：m_flowSoundLevel = 最近流水格距离映射 [0,1]（tickImpl 节流扫描更新）；
    //   m_flowScanTimer 累加 dt 到 kFlowScanInterval 才重扫（~0.25s，省扫描开销）。值真变才 emit。
    float m_flowSoundLevel = 0.0f;
    float m_flowScanTimer = 0.0f;
    bool m_dead = false;             // t175 死亡态镜像（dropAllItems 置 true / respawn 置 false）：抑制死亡后
                                     //   pickupScan（玩家尸体停死亡点，否则 0.5s 免拾窗过后掉落物被自动捡回空背包）

    // 持续挖掘态（t34）：仅 Survival 进入累积（Creative 单击瞬破不进入）；progress 0..1；
    // stage = clamp(progress*6, 0, 5)，-1 = 无累积（裂纹叠层隐藏）。mineBx/y/z = 目标格整数坐标。
    bool m_mining = false;
    float m_miningProgress = 0.0f;
    int m_miningStage = -1;
    int m_mineBeat = -1; // t165 挖掘挥臂节拍（progress*6 跨阶驱动 swingArm；基岩 progress 循环 → 持续挥臂）
    // t231 不可挖方块（基岩）挖掘音节流时间戳（m_evtClock；不被 tick restart）。基岩 miningTime 走 0.05s
    //   地板 → progress 每 tick 跨多 beat → beat 变化触发 miningSound 每 ~16ms 连发（远快于普通挖掘的
    //   几百 ms 节奏）。spec「改与普通挖掘同节奏（几百 ms 间隔）」：仅对不可挖方块按本时间戳节流到
    //   kMineSoundThrottleMs；可挖方块仍每 beat 发（其 miningTime/6 节奏本就 ≥ 此节流，行为不变）。
    //   初值 -100000 = 远古 → 每次新挖掘会话首 beat 不受限。beginMining / cancelMining 同 m_mineBeat 归位。
    qint64 m_lastMineSoundMs = -100000;
    qint32 m_mineBx = 0, m_mineBy = 0, m_mineBz = 0;
    // 物理左键按下态（t44 连续挖掘）：与 m_mining（=「正在某目标上累积进度」）分离 —— finishMiningAt
    // 破完一块后 cancelMining 清 m_mining，但左键可能仍按住。m_leftDown 仅由 press 边缘（beginMining）
    // 置 true、release 边缘（endMining）/ 暂停失焦（release）置 false；finishMiningAt / cancelMining
    // 不动它。updateMining 顶部据此 + 新命中 → 自动 beginMining 下一块（progress 归 0），不松手连挖。
    bool m_leftDown = false;
    // t248 攻击冷却剩余秒数（>0 时 attackMob 早退不扣血）：tickImpl 每帧递减；attackMob 成功命中后置
    //   kAttackCooldown。修长按左键每 tick 重触 beginMining 致 mob 瞬秒（见 kAttackCooldown 注释）。
    float m_attackCooldown = 0.0f;
    // t296 玩家受击击退冲量累加器（XZ 水平推 + Y 小跳弧）。与 m_vel 分离 —— 玩家 m_vel.x/z 每 tick 被 wish
    //   输入覆盖（走路 / 疾跑 / 水中），击退若写入 m_vel 一帧即被覆盖 → 看不见。故独立累加：applyHitKnockback
    //   设本向量（水平 = dir×kHitKnockbackHoriz / 垂直 = kHitKnockbackUp 小跳）；step() 走路路径每帧水平指数衰减
    //   + 垂直受重力 → 叠入 delta = (m_vel + m_knockback)*dt；衰减殆尽（L1 < 阈）→ 整体清零。仅走路模式积分
    //   （Spectator / Creative-飞 noclip 早 return 不至此 → setMode 切走时清零，防陈旧冲量残留）。着地（onGround
    //   且向下分量 <0）清 Y 分量（小跳被地面吸收，同 m_vel.y 着地归零）。
    QVector3D m_knockback{0, 0, 0};
    // t267 持续进食态（手持面包按住右键累积，机制等价 MC 1.0 长按右键食面包 ~1.6s）。仅持面包时进入
    //   （eventFilter RightButton press 据持物 == BreadId 分流调 beginEating，面包不进 placeBlock）。
    //   progress 0..1；eatBeat = clamp(progress*kEatBeats,0,kEatBeats) 跨阶驱动 eatingParticle（屑粒迸发）。
    //   完成（progress>=1）→ finishEating 消耗 1 面包 + 恢复饥饿 + 清 m_eating；m_rightDown 不动 → 连食。
    bool m_eating = false;
    float m_eatingProgress = 0.0f;
    int m_eatBeat = -1;
    // t267 物理右键按下态（与 m_eating =「正在累积进食」分离，同 m_leftDown/m_mining 解耦模式）：
    //   finishEating 消耗后 cancelEating 清 m_eating，但右键可能仍按住。m_rightDown 仅由 press 边缘
    //   （beginEating）置 true、release 边缘（endEating）/ 暂停失焦（release）置 false；finishEating /
    //   cancelEating 不动它。updateEating 顶部据此 + 仍持面包 → 自动 beginEating 下一件（不松手连食）。
    bool m_rightDown = false;
    // t304 弓拉弓蓄力态（手持弓右键按住累积，机制等价 MC 1.0 长按右键拉弓 ~1s 满弓）。仅持弓时进入
    //   （eventFilter RightButton press 据持物 == Bow 分流调 beginBowDraw，弓不进 placeBlock）。
    //   m_bowDrawTime 累加 dt（钳 kBowFullCharge）；bowDrawProgress = clamp(m_bowDrawTime/kBowFullCharge,0,1)。
    //   松开（endBowDraw）据蓄力射箭 + 清 m_bowDrawing；换槽 / 失焦 / 暂停 → cancelBowDraw。蓄力期间 step()
    //   水平速度 ×kBowSlowMul（spec「拉弓减速」，与蹲下叠加）。
    bool m_bowDrawing = false;
    float m_bowDrawTime = 0.0f;

    // 射线选体命中态（整数格坐标 + 整数法线分量；仅变化时 emit hitChanged，避免每帧抖动 QML）
    bool m_hasHit = false;
    qint32 m_hitBx = 0, m_hitBy = 0, m_hitBz = 0;
    qint32 m_hitNx = 0, m_hitNy = 0, m_hitNz = 0;
    // t212 命中点世界 Y（眼位 + 视线*dist；updateRaycast 每帧刷新，不随 changed 早退——同格内移动格坐标/
    //   法线不变但命中点 Y 仍在变，placeBlock 点击瞬间需读最新值定 slab 上/下半与互补合并）。无命中=0。
    float m_hitPointY = 0.0f;
    // t242 命中方块的视线距离（眼位 → 命中面欧氏距离；updateRaycast 每帧刷新，不随 changed 早退——
    //   beginMining 攻击判定读它与 findMobHit 返回距离比，挑更近的目标优先攻击 / 破块）。无命中=kReach
    //   （=「无穷远」语义，让 mob 命中永远优先于「无方块命中」）。初值 5.0f 与 kReach 同值（kReach 声明
    //   在后，依代码库约定默认成员初始化器不前向引用，故用字面量；值变更须同步 kReach）。
    float m_hitDist = 5.0f;
    // t253 攻击单体选中：准星瞄准的**单个**目标 mob 的 EntityManager 索引（findMobHit 最近活体）。
    //   updateRaycast 每帧刷新（不随 hit changed 早退——同 m_hitPointY/m_hitDist；准星扫过 mob 时即便
    //   背后方块格未变，目标 mob 仍在切换，须每帧重算）。mob 须不晚于命中方块（mobDist<=m_hitDist）才算
    //   目标（方块挡 mob 前 → 非目标）。仅值真变才 emit targetedMobChanged。-1 = 无目标。beginMining 攻击
    //   仍即时调 findMobHit（点击瞬间最新视线），不读此缓存。
    int m_targetedMob = -1;

    static constexpr float kHalfW = 0.3f;      // 宽 0.6
    static constexpr float kHeight = 1.8f;
    static constexpr float kEyeHeight = 1.62f;
    static constexpr float kCrouchHeight = 1.5f; // 蹲下 AABB 高（spec t51：1.8→1.5）
    static constexpr float kCrouchEye = 1.35f;   // 蹲下眼位（相机随之降低；≈ MC 蹲/站比例）
    // t259 碰撞皮肤（collision skin）：overlapSubAABBs 逐块重叠判定用「向内缩 skin」的有效 AABB。
    //   落地 / 贴墙 snap 为防抖动留了 eps=1e-4 间隙，使身体实际占据 = surface + eps + height，
    //   于是蹲下（1.5）的头顶会以 eps 量探入「精确 1.5 格」通道的天花板（上半砖 / 整砖+下半砖），
    //   严格 `maxy > b.minY` 判碰撞 → 玩家卡在通道口进不去（与 MC 1.0 蹲下可过 1.5 缺口相违）。
    //   skin（1e-3 = 1mm ≈ 10× snap eps）吸收这类浮点漂移与贴面误差，使 1.5 AABB 能通过 1.5 通道；
    //   远小于 1 格且小于最薄方块（压力板 1/16=0.0625），视觉不可见、不漏检真实嵌入。cell 采样仍走
    //   完整 AABB（不漏采贴面格），仅逐块判定用缩皮 AABB —— 是「防抖动 snap」与「精确通行」的解耦。
    static constexpr float kCollisionSkin = 1e-3f;
    static constexpr float kFly = 8.0f;        // 飞/观察 移速
    static constexpr float kFlyMin = 4.0f;     // 飞行最低速度（blocks/sec；t159 滚轮调速下限）
    static constexpr float kFlyMax = 20.0f;    // 飞行最高速度（blocks/sec；t159 滚轮调速上限）
    static constexpr float kFlyStep = 1.0f;    // 滚轮每档有效飞行速度步进（blocks/sec；t159）
    static constexpr float kUnderwaterSpeedMul = 0.4f; // 水下速度倍数（眼位在水格；t159，用户可后续调）
    // t174 水中浮力 / 游泳常量（机制等价 MC 1.0 水中物理：减速 + 浮力 + 按空格上浮）：
    //   kWaterGravity：水中等效重力（远小于 kGravity=28 → 缓沉；接近 MC「水中下落被阻尼」）。
    //   kSwimUp：按住空格上浮速度（恒定向上，机制等价 MC 按空格游泳上浮）。
    //   kWaterSinkMax：水中最大下沉速度（钳制，防加速到穿水底；远小于 kMaxFall=78.4）。
    //   水平减速复用 kUnderwaterSpeedMul（waterMul，眼位在水中时已乘入；脚位在水面以上时走正常水平速度）。
    static constexpr float kWaterGravity = 6.0f;  // 水中重力（缓沉；≈ kGravity×0.21）
    static constexpr float kSwimUp       = 4.5f;  // 按空格游泳上浮速度（blocks/sec）
    static constexpr float kWaterSinkMax = 3.0f;  // 水中最大下沉速度（钳制）
    // t223 近流水 proximity 水流声（spec「近流动水一定范围持续水流声 ambience loop」）：
    //   kFlowSoundRadius：扫描盒半径（格）= 水流声可闻范围；玩家到最近流水格距离 ≥ 此 → level=0（无声）。
    //     8 格 ≈ MC 近流水可闻距离量级（机制对齐，非精确数值复刻）。
    //   kFlowScanInterval：proximity 重扫间隔（秒）。tickImpl 累加 dt 到此值才扫一次（省扫描开销，~4 次/秒
    //     足够跟手；扫描盒约 (2R+1)³ 子集 ~几千次 blockAt/stateAt，每次 O(1) 数组索引，~亚毫秒级）。
    static constexpr float kFlowSoundRadius = 8.0f;   // 水流声可闻半径（格）
    static constexpr float kFlowScanInterval = 0.25f; // proximity 重扫间隔（秒）
    // t234 耕地水源邻近判定半径（机制等价 MC 1.0 farmland hydration：水同高或低 1 层、水平 4 格内即滋润）。
    //   isFarmlandMoist 扫 (2R+1)×2×(2R+1) 盒（y / y-1 两层 × 水平 ±4）；MC 取 4，本工程对齐。只读 blockAt。
    static constexpr int kFarmlandWaterRadius = 4;
    // t246 草丛挖掉掉种子的概率分母（机制等价 MC 1.0 挖草丛 1/8=12.5% 掉小麦种子；可在本常量调）。finishMiningAt
    //   破 TallGrass 时以 1/kTallGrassSeedDropDenom 概率 emit spawnItem(SeedId,1)，否则不掉落（dropId/dropCount
    //   表恒返 1 种子是「基础兜底」，概率门控在本特例分支之上覆盖通用 drop 路径，同 WheatCrop/Planks 特例模式）。
    //   这是玩家交互掉落的随机性（QRandomGenerator），非 worldgen 确定性范畴 §2-K —— 机制等价 MC 草丛掉种随机，
    //   同 WheatCrop 收割种子 1-2 随机。羊吃草（entitymanager::sheepEatGrass）走静默 setWaterSilent 不发掉落，
    //   与本玩家破块路径互不影响。
    static constexpr int kTallGrassSeedDropDenom = 8; // 1/8 ≈ 12.5%（MC 1.0 草丛掉种概率）
    // t305 树叶掉落概率（机制等价 MC 1.0 破叶掉落）：finishMiningAt 破 Leaves 时按本概率掉树苗物品 / 木棒。
    //   kLeafSaplingDropPct=5（5% 掉 1 树苗，MC 1.0 橡树叶 5% 掉树苗）；kLeafStickDropPct=2（2% 掉 1 木棒，
    //   MC 1.0 树叶 2% 掉木棒）。两次独立判定（可同时掉树苗 + 木棒）。玩家交互掉落的随机性（QRandomGenerator），
    //   非 worldgen 确定性范畴 §2-K。自然衰减（decayLeavesAround）不走本路径（无掉落，spec「树叶消失」）。
    static constexpr int kLeafSaplingDropPct = 5; // 5%（MC 1.0 橡树叶掉树苗概率）
    static constexpr int kLeafStickDropPct   = 2; // 2%（MC 1.0 树叶掉木棒概率）
    // t211 水流推动玩家（机制等价 MC 1.0 流水冲走实体）：
    //   kWaterFlowPush：流水水平推力速度（blocks/sec；脚位在流水格 state>0 时沿离源方向叠入水平速度）。
    //     低于 kWalk(4.3) → 玩家仍可逆流游（净速 ≈ 走速 − 推力），但松手会被流走。spec「创造非飞 + 生存」。
    //   t270 流水推力增强：原值 2.5 太弱 —— 浮水按空格时几乎感觉不到水流携带（drift 仅 2.5 blocks/sec，
    //     远低于走速 4.3）。提升至 4.0 使水流明显持续外推（idle drift 4.0；逆流走净速 0.3 仍可行、疾跑 1.59、
    //     游出水面脱困路径不变）。仍 < kWalk 故玩家可逆流（MC 对齐：流水可逆、但费力）；机制（方向梯度 + 每 tick
    //     叠入 m_vel.x/z + 仅走路模式）不变，只调幅值。穿墙安全：4.0×0.05=0.2 < 0.4 子步阈（同向叠走速 8.3×
    //     0.05=0.415 → sub=2，子步循环自适应任意速度，无穿隧道风险）。
    //   kWaterfallSinkMax：悬崖边落水（瀑布）额外向下带的最大下沉速度（blocks/sec；高于 kWaterSinkMax=3
    //     使玩家在瀑布中下沉更快，但仍远慢于自由落体 kMaxFall=78.4）。脚位下方为空气 = 水柱下落时启用。
    //   仅走路模式（Survival / Creative-未飞）生效 —— Spectator / Creative-飞 early return，不进此分支
    //   （spec「飞行 / 观察者态不生效」）。
    static constexpr float kWaterFlowPush    = 4.0f; // 流水水平推力（blocks/sec；t270 由 2.5 增强）
    static constexpr float kWaterfallSinkMax = 8.0f; // 瀑布最大下沉（blocks/sec）
    static constexpr float kWalk = 4.3f;       // 走 移速
    static constexpr float kGravity = 28.0f;
    static constexpr float kJump = 8.4f;       // 顶点约 1.25 格
    static constexpr float kMaxFall = 78.4f;
    // t296 玩家受击击退常量（机制对齐 MC 1.0 玩家被击退量级；与 EntityManager mob 击退 kKnockbackHoriz=4.5 /
    //   kKnockbackUp=4.5 / kKnockbackDrag=4.0 同族，玩家侧略强使「被打」反馈明显）：
    //   - kHitKnockbackHoriz：受击水平初速（blocks/s）。略高于玩家走速 4.3 + mob 击退 → 一击把玩家推 ~1.3 格
    //     （v0/drag ≈ 6/4.5），玩家可逆推反击但不致被风筝到追不上。
    //   - kHitKnockbackUp：受击小跳垂直初速（blocks/s，向上）。峰值 v²/(2g)=17.64/56≈0.32 格 = 小弹起（机制等价
    //     MC 受击小幅上弹），重力 28 拉回走原 tick 着地路径。
    //   - kHitKnockbackDrag：水平衰减率（1/s）。时间常数 1/drag≈0.22s → ~0.5s 衰到 ~10%、~1s 近停；总位移 ≈ v0/drag。
    static constexpr float kHitKnockbackHoriz = 6.0f;  // 受击水平初速（blocks/s）
    static constexpr float kHitKnockbackUp    = 4.2f;  // 受击小跳垂直初速（blocks/s；峰值 ~0.32 格）
    static constexpr float kHitKnockbackDrag  = 4.5f;  // 受击水平衰减率（1/s；~0.5s 基本停下）
    static constexpr float kSuffocationInterval = 1.0f; // t160 窒息扣血间隔（秒；每秒 1HP，机制等价 MC 窒息 1/秒）
    // t202 气泡 + 溺水时序（机制等价 MC 1.0：10 气泡 ≈ 15s 入水耗尽，归零后每秒 1HP）。
    //   kAirInterval：眼位入水时每减 1 气泡的间隔（1.5s → 10 气泡 = 15s 才耗尽，同 MC 1.0 air=300 tick@20tps）。
    //   kDrownInterval：air 归零后每扣 1HP 的间隔（1s，同 MC 溺水 1HP/s + 复用 fallDamageTaken→damaged 红闪 / 晃链）。
    //   kAirRegenInterval：出水后每回 1 气泡的间隔（0.3s → 从 0 回满 ≈ 3s，比耗尽快避免反复溺水）。
    static constexpr float kAirInterval = 1.5f;
    static constexpr float kDrownInterval = 1.0f;
    static constexpr float kAirRegenInterval = 0.3f;
    static constexpr int kMaxAir = 10; // t202 气泡上限（须与 PlayerState::kMaxAir 一致；满气泡起算）
    // t238 饥饿系统时序（机制对齐 MC 1.0 饥饿：满饥饿 ~ 几分钟运动耗尽；归零后周期扣血；充足时缓慢回血）。
    //   kMaxHunger：饥饿上限（须与 PlayerState::kMaxHunger=20 一致；10 鼓腿 × 2 点）。
    //   kBreadHungerAmount：食面包一次恢复的饥饿值（5 = 2.5 鼓腿；机制等价 MC 面包 +5 hunger）。
    //   kHungerIdleRate / kHungerWalkRate / kHungerSprintRate：每秒饥饿消耗率（累积到 1.0 扣 1）。
    //     idle 极慢（站立 / 漂浮 ~ 25 分钟耗尽）；walk 中速（走 ~5 分钟）；sprint 快（疾跑 ~2.5 分钟）。
    //     非线性 & MC 1.0 量级一致（疾跑 ≈ 走 × 2、idle 远低于走）。m_horizSpeed > 0.1 即视为移动；
    //     moveState==Sprint 用 sprint 率，否则 walk 率；不动用 idle 率。
    //   kStarveInterval：饥饿归零后每扣 1HP 的间隔（4s，机制等价 MC 1.0 饥饿伤害 1HP/4s）。
    //   kHungerRegenInterval：饥饿充足时每回 1HP 的间隔（4s，机制等价 MC 1.0 饱腹回血 1HP/4s）。
    //   kRegenHungerThreshold：回血所需饥饿下限（18 = 9 鼓腿；机制等价 MC 1.0 hunger≥18 才回血）。
    static constexpr int kMaxHunger = 20;
    static constexpr int kBreadHungerAmount = 5;
    // t267 长按右键进食时序（机制对齐 MC 1.0：按住右键 ~1.6s 食完一件面包）。
    //   kEatDuration：食一件面包的累积时长（秒）；progress 增量 = dt / kEatDuration。32 ticks ≈ 1.6s
    //     （MC 1.0 食物进食 32 ticks；机制对齐，非精确数值复刻）。
    //   kEatBeats：进食屑粒节拍数 —— progress [0,1] 等分 kEatBeats 段，每跨一段（eatBeat 自增）发一次
    //     eatingParticle（嘴部屑粒迸发）+ QML 抖动循环。4 段 ≈ 每 0.4s 一拍（节奏感清晰，屑粒不爆量）。
    static constexpr float kEatDuration = 1.6f;
    static constexpr int kEatBeats = 4;
    static constexpr float kHungerIdleRate   = 0.013f; // ~1 饥饿 / 75s ≈ 25min 耗尽（满→空）
    static constexpr float kHungerWalkRate   = 0.067f; // ~1 饥饿 / 15s ≈ 5min 走路耗尽
    static constexpr float kHungerSprintRate = 0.133f; // ~1 饥饿 / 7.5s ≈ 2.5min 疾跑耗尽
    static constexpr float kStarveInterval = 4.0f;
    static constexpr float kHungerRegenInterval = 4.0f;
    static constexpr int kRegenHungerThreshold = 18;
    static constexpr float kSens = 0.25f;      // 度/像素
    static constexpr float kStrideRate = 2.2f; // 步频系数（rad/米）：speed*dt*kStrideRate = walkPhase 增量（t45）
    static constexpr float kDeg = 0.017453292519943295f;
    static constexpr float kReach = 5.0f;      // 射线选体射程（格）
    static constexpr float kPickupDist = 1.5f; // 拾取距离阈值（格；玩家 AABB 中心起算，spec ~1.2 量级）
    // t265 玩家攻击伤害改走 ToolRegistry::attackDamage（手持物驱动）。机制等价 MC 1.0：
    //   - 剑（type=Sword）：木 4 / 石 5 / 铁 6 HP（每档 +1）；
    //   - 空手 / 镐 / 斧 / 铲 / 锄：ToolRegistry::kFistDamage=1 HP（MC 1.0 徒手）。
    //   旧 t242 固定 kAttackDamage=4（不分工具）已由 t265 替换为按手持物查表（spec「剑→加攻击伤害」）。
    //   创造模式亦走此伤害（但创造可瞬破方块优先 → 实际打 mob 走 findMobHit 同路径）。
    // t265 剑攻击消耗耐久（机制等价 MC「剑每次命中 -1 耐久」）：attackMob 命中后 Survival 模式调
    //   hotbar.damageSelectedItem（对非工具 / 空手静默 no-op；耐久归零自动清槽）。
    // t248 攻击冷却（秒）：mob 受击后 0.5s 内同玩家对其的伤害被压制（attackMob 早退、不扣血 / 不发信号）。
    //   修「1 击即死」根因：survival 长按左键时 updateMining 续挖分支会每 tick 重调 beginMining → 攻击分支
    //   每帧打一下 mob（mob 后方射程内有方块即触发），10HP/4dmg=3 击在 ~50ms 内打完 = 体感瞬秒。冷却把
    //   「长按连击」压成「每 0.5s 一次伤害」→ 3 击需 ~1.5s = 用户体感「空手需多击」（spec 验收）。机制等价
    //   MC 攻击冷却（间隔门控，非每帧扣血）。值取 0.5s ≈ MC 剑基础冷却时长。单次 click 边沿（press）首击总
    //   生效（cooldown 初值 0）；连击 / 长按的后续 tick 在冷却内被吞。
    static constexpr float kAttackCooldown = 0.5f;
    // t231 不可挖方块（基岩）hold-mine 挖掘音节流间隔（ms；m_evtClock 时间戳差）。基岩 progress 因 miningTime
    //   走 0.05s 地板每 tick 跨多 beat → miningSound 每 ~16ms 连发；spec「改与普通挖掘同节奏（几百 ms 间隔）」。
    //   250ms ≈ 典型可挖方块（如手挖石头 miningTime≈1.5s）beat 变化的间隔量级（miningTime/6），机制对齐
    //   MC 镐撞基岩的击打节拍。仅作用于不可挖方块；可挖方块 miningTime/6 节奏本就 ≥ 此值，不触发节流。
    static constexpr qint64 kMineSoundThrottleMs = 250;
    // t304 弓拉弓 / 射箭常量（机制对齐 MC 1.0 弓：~1s 满弓、蓄力越高箭越快越痛、拉弓减速、需箭在背包；
    //   数值为本工程小世界量身调，非 MC 精确复刻 —— PLAN §4「机制对标」非数值 1:1）。
    //   - kBowFullCharge：满弓蓄力时长（秒）。MC 1.0 弓拉满 ~20 tick=1s；取 1.0s（玩家有反应时间决定何时松）。
    //   - kBowMinChargeRatio：可射箭的最低蓄力比（< 此松开 = 取消不射，机制等价 MC「未拉足松开箭无力 / 不射」）。
    //   - kBowMinSpeed / kBowMaxSpeed：箭水平速度（blocks/s；蓄力 lerp。min≈12 让短蓄力箭仍飞几格、max≈26 满弓
    //     快速直线）。Arrow 重力 = kGravity=28（与骷髅箭 / 世界同源）→ 抛物弧自然。
    //   - kBowMinDamage / kBowMaxDamage：箭命中伤害（HP；蓄力 lerp。min=1=半心、max=6=3 心，机制等价 MC 1.0 弓
    //     伤害量级）。Hotbar::bowArrowMaxDamage 据本 max 显 tooltip「攻击 1-6」。
    //   - kBowSlowMul：拉弓时水平速度倍数（spec「拉弓减速」；与蹲下 ×0.4 叠加 = 蹲下拉弓更慢）。仅走路模式生效。
    static constexpr float kBowFullCharge    = 1.0f;   // 满弓蓄力时长（秒；MC 1.0 弓 ~1s 拉满）
    static constexpr float kBowMinChargeRatio = 0.15f; // 可射箭最低蓄力比（< 此松开 = 取消）
    static constexpr float kBowMinSpeed      = 12.0f;  // 短蓄力箭水平速度（blocks/s）
    static constexpr float kBowMaxSpeed      = 26.0f;  // 满弓箭水平速度（blocks/s）
    static constexpr int   kBowMinDamage     = 1;      // 短蓄力箭命中伤害（HP）
    static constexpr int   kBowMaxDamage     = 6;      // 满弓箭命中伤害（HP；Hotbar::bowArrowMaxDamage 同源）
    static constexpr float kBowSlowMul       = 0.5f;   // 拉弓时水平速度倍数（spec「拉弓减速」）
    static constexpr float kCamMax = 3.5f;     // 第三人称相机最大距离（格；t40，与 Main.qml 默认 d 对齐）
    static constexpr float kCamMargin = 0.1f;  // 相机贴命中面前的余量（防卡面 z-fight / 近裁面穿插；t40）
};

#endif // PLAYERCONTROLLER_H
