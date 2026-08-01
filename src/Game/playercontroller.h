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
#include "entitymanager.h"      // 统一实体管理器（t95 测试生物 / 玩家推动）
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
    // 统一实体管理器（t95）：PlayerController 经 Q_PROPERTY 持 EntityManager*（同 world/hotbar/itemEntities
    // 模式，QML 注入 peer ViewModel）。每帧调 entityManager.tick(dt, world)（重力 / 地面静止，独立于
    // 捕获态——菜单 / 暂停时实体仍模拟）+ captured 时 resolvePlayerPush（玩家走碰可推动实体，把玩家
    // 位移解析后传给实体）。分层（PLAN §2）：PlayerController 属 Game/Physics，EntityManager 属 Entities，
    // 经 QML 绑定注入（运行期连接、非编译期反向依赖，同 itemEntities 先例）。
    Q_PROPERTY(EntityManager *entityManager READ entityManager WRITE setEntityManager NOTIFY entityManagerChanged)
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
    // 飞行速度倍数（t159）：创造-飞 / 观察者用滚轮调速，默认 1.0（= kFly 基速）；滚轮 ±档，
    //   有效速度 = clamp(kFly * mul, kFlyMin, kFlyMax) ∈ [4, 20] blocks/sec。仅飞态生效（走路态滚轮
    //   仍切 hotbar；spec「WheelHandler 仅 flying 时生效」）。状态属 Game/Physics 层（§2-D 单一输入路径：
    //   滚轮经 QML → adjustFlySpeed），step() 读它算飞移速。spectator 常驻飞亦适用（spec「创造/观察者」）。
    Q_PROPERTY(float flySpeedMul READ flySpeedMul NOTIFY flySpeedMulChanged)
    // 当前有效飞行速度（blocks/sec）= clamp(kFly*mul, kFlyMin, kFlyMax)；F3 报它（t159）。随 mul 变通知。
    Q_PROPERTY(float flySpeed READ flySpeed NOTIFY flySpeedMulChanged)
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
    // 飞行速度滚轮调速（t159）：dir=+1 加速 / -1 减速（对应前滚 / 后滚），每档 kFlyStep。
    //   有效速度 clamp 到 [kFlyMin, kFlyMax]（4..20 blocks/sec）。仅 QML 在飞态调用（走路态滚轮切 hotbar）。
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
    // 中键拾取方块（t37 pick block）：取当前射线命中格的方块 id → 写入 hotbar 当前选中槽（覆盖；
    // 创造源无限 → 满栈，生存 → 单件）。仅指针捕获时生效（与破/放同窗口级 MouseButtonPress 路径）。
    // spec：「无论背包开关」—— captured=true 蕴含背包已关，故等价于「游戏内中键」；命中空气 / 无
    // 世界 / 无 hotbar → 不动作。pick 属「选择」语义（不改栅格），三模式均允许（观察者亦可查方块）。
    Q_INVOKABLE void pickBlock();
    // Q 键丢弃（t36）：从选中槽 takeStack 1 件 → 发 spawnItem（玩家前方 1.5 格）。仅指针捕获时生效
    // （spec）。空手 / 取失败 → 不丢。spawnItem 经 QML Connections 转发到 ItemEntityManager.spawnItem
    // （同破块掉落 t35 路径）；丢弃后实体可被重新拾取（闭环）。
    Q_INVOKABLE void dropHeld();
    // 拖出背包丢弃（t49）：光标手持栈（hotbar.heldBlock/heldCount）整栈丢弃为实体（玩家前方 1.5 格）。
    // 与 dropHeld 的差异：后者取**选中槽** 1 件且仅捕获时；本方法取**光标手持栈**整栈、**不限捕获态**
    // （背包打开时未捕获正是此场景）。t64：spawnItem 传 heldCount → 1 实体携带整栈数量（修「丢 4 木棒
    // 捡回只剩 1」bug；spec 验收「4 木棒丢出捡回仍 4」）。清空 hotbar 光标手持栈（setHeldBlock(0) 同步清
    // count）。空手 → 不丢。经 QML Connections 转发（同 dropHeld）。
    Q_INVOKABLE void dropHeldCursor();
    // t78 重生定位：传回出生点（kSpawn）+ 清速度 / 挖掘态 / 飞行 / 蹲下疾跑（spec「立即重生」的定位部分）。
    //   由呈现层「立即重生」按钮调（与 PlayerState::respawn 配对：本方法管定位/物理态，PlayerState 管
    //   血量/死亡态）。出生点与构造期 m_pos 初值同源（kSpawn）；m_peakY 重置 → respawn 后下落从出生点
    //   起算（同 componentComplete 首帧，不误判陈旧落差）。emit positionChanged → 相机绑定重算跟随。
    Q_INVOKABLE void respawn();

signals:
    void worldChanged();
    void hotbarChanged();
    void itemEntitiesChanged();
    void entityManagerChanged();
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
    void moveSpeedChanged();  // 行走速度变（t45；驱动 QML walkBlend 切换 + 摆频）。speed 属性亦复用本信号（t159）。
    void flySpeedMulChanged(); // 飞行速度倍数变（t159 滚轮调速；驱动 F3 报当前有效飞速）
    void walkPhaseChanged();  // 行走相位推进（t45；走时每 tick 发，QML 据 sin() 算四肢欧拉角）
    void moveStateChanged();  // 移动态切（Walk/Sprint/Crouch；t51；驱动 QML 摆幅 + 速率因子）
    void hitChanged();
    void selectedBlockChanged();
    void selectedItemChanged(); // 手持原始 id 变（含工具段切换；驱动 t34 速度重算）
    void miningStateChanged();  // mining / miningStage / miningBlock 三者同变（一次性发，少抖动）
    void miningProgressChanged(); // 进度连续变化（HUD 可选显示百分比；高频，独立信号）
    // 挖掘过程碎屑粒子（t61）：生存累积挖掘时每跨一阶（progress 推进触发 stage 1..5 切换）发一次，
    // 携被挖方块坐标 + id（呈现层据此迸发少量对应色碎屑，复用破块 emitter / 重力）。破块完成时
    // 的「+30% 大迸发」仍走 World 的 blockBroken → burstBreak（已在此任务内 +30%），本信号仅驱动
    // 「挖的过程中」的进度反馈粒子。创造瞬破不进累积态 → 不发；仅 Survival 推进 stage 时发。
    // 分层（PLAN §2）：Game/Physics 层发语义事件，呈现层只消费（同 blockBroken→粒子 / swingArm 模式）。
    void miningParticle(int x, int y, int z, int blockId);
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
    void fallDamageTaken(int hp); // 生存掉落伤害（t22）：着地结算，正值才发；呈现层路由到 PlayerState
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
    // t146 放置校验：按「将放置方块的实际形状 sub-AABB」判是否与玩家 AABB 相交（不完整方块可能只占
    //   半格，玩家在另半格内仍可放；air/torch 无 sub-AABB → 不挡）。id/state = 放置态（slab 据命中面、
    //   stairs/door 据朝向）。与碰撞（overlapSubAABBs）共用 collisionAABBs 单一权威。
    bool overlapsPlayerAABB(int bx, int by, int bz, quint8 id, quint8 state) const;
    // t146 玩家 AABB vs 世界碰撞 sub-AABB 重叠测试（3 轴严格重叠）。axis∈{0,1,2} 时记录沿该轴的相交
    //   sub-AABB 表面（outMinSurf/outMaxSurf）供 moveAxis 贴面；axis<0 仅判命中（aabbHitsSolid 用）。
    //   取样范围与旧 aabbHitsSolid 同策略（floor(min)..ceil(max)-1，严格重叠排除仅贴面）。
    bool overlapSubAABBs(int axis, float *outMinSurf, float *outMaxSurf) const;
    // 移动状态速率因子（t51）：Sprint×1.3 / Crouch×0.4 / Walk×1.0。仅走路模式水平速度乘此值
    //   （飞 / 观察者 noclip 恒 1，状态机不进入 Sprint/Crouch）。同时驱动 moveSpeed 报告 → walkPhase 频率。
    float speedMul() const;
    // t159 水下判定：玩家眼位格 == Water。step 据此把水平（及飞垂直）速度乘 kUnderwaterSpeedMul（~0.4）。
    //   只读 World::blockAt（向下依赖）；眼位 = position()（脚底 + eyeHeight）。无世界 → false。
    bool eyeInWater() const;
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
    // 持续挖掘（t34）：每 tick 累积进度 / 检目标变更 / 完成时破块。由 tick() 调（captured 时）。
    void updateMining(float dt);
    // 清掉累积态（松开 / 换目标 / 失焦 / 完成）。无变化时静默（不发信号）。
    void cancelMining();
    // 完成（progress 满）：写 air + 发 playerMined + 清态。drop 由 caller 算（生存走 ToolRegistry）。
    void finishMiningAt(int x, int y, int z, bool drop);
    // t150d 火把支撑判定：火把在 (x,y,z) 是否仍有实体支撑（下 / 四侧任一为 solid）。与 placeBlock 的
    //   火把放置预检 + Main.qml computeTorchOrient 同语义（5 向任一 solid = 有支撑）。无支撑的火把应掉落。
    bool torchHasSupport(int x, int y, int z) const;
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
    QQuickWindow *m_window = nullptr;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QElapsedTimer m_evtClock; // 事件时间戳（双击检测；不被 tick restart）

    // 出生点（t78 重生定位）：与构造期 m_pos 初值同源，respawn() 传回此处。脚底中心坐标。
    // 必须声明在 m_pos 之前（m_pos 默认成员初始化器引用本常量；C++ 不允许前向引用）。
    // t119：世界高度 16→64、地表重定标到 16..40 → 出生 Y 抬到 44（高于最高地表 40，玩家落 / 浮于地表之上，
    //   不会卡进地形）。默认 Spectator 模式无重力（漂浮）；切重力模式时 setMode 重置 m_peakY 防误判落差。
    static constexpr float kSpawnX = 8.0f;
    static constexpr float kSpawnY = 44.0f;
    static constexpr float kSpawnZ = 8.0f;
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
    static constexpr float kCrouchHeight = 1.5f; // 蹲下 AABB 高（spec t51：1.8→1.5）
    static constexpr float kCrouchEye = 1.35f;   // 蹲下眼位（相机随之降低；≈ MC 蹲/站比例）
    static constexpr float kFly = 8.0f;        // 飞/观察 移速
    static constexpr float kFlyMin = 4.0f;     // 飞行最低速度（blocks/sec；t159 滚轮调速下限）
    static constexpr float kFlyMax = 20.0f;    // 飞行最高速度（blocks/sec；t159 滚轮调速上限）
    static constexpr float kFlyStep = 1.0f;    // 滚轮每档有效飞行速度步进（blocks/sec；t159）
    static constexpr float kUnderwaterSpeedMul = 0.4f; // 水下速度倍数（眼位在水格；t159，用户可后续调）
    static constexpr float kWalk = 4.3f;       // 走 移速
    static constexpr float kGravity = 28.0f;
    static constexpr float kJump = 8.4f;       // 顶点约 1.25 格
    static constexpr float kMaxFall = 78.4f;
    static constexpr float kSens = 0.25f;      // 度/像素
    static constexpr float kStrideRate = 2.2f; // 步频系数（rad/米）：speed*dt*kStrideRate = walkPhase 增量（t45）
    static constexpr float kDeg = 0.017453292519943295f;
    static constexpr float kReach = 5.0f;      // 射线选体射程（格）
    static constexpr float kPickupDist = 1.5f; // 拾取距离阈值（格；玩家 AABB 中心起算，spec ~1.2 量级）
    static constexpr float kCamMax = 3.5f;     // 第三人称相机最大距离（格；t40，与 Main.qml 默认 d 对齐）
    static constexpr float kCamMargin = 0.1f;  // 相机贴命中面前的余量（防卡面 z-fight / 近裁面穿插；t40）
};

#endif // PLAYERCONTROLLER_H
