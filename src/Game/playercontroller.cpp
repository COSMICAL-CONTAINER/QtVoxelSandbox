#include "playercontroller.h"
#include "world.h"

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuaternion>

#include <algorithm>
#include <cmath>

PlayerController::PlayerController(QQuickItem *parent) : QQuickItem(parent)
{
    connect(this, &QQuickItem::windowChanged, this, &PlayerController::onWindowChanged);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(16); // ~60Hz：鼠标视角 + 物理
    connect(&m_timer, &QTimer::timeout, this, &PlayerController::tick);
}

void PlayerController::componentComplete()
{
    QQuickItem::componentComplete();
    if (!m_window && (m_window = window()))
        m_window->installEventFilter(this); // 拦截 Esc + 失焦
    m_peakY = m_pos.y(); // 掉落伤害基准：以脚底初始 Y 起（首帧不误判大落差）
    m_clock.start();
    m_evtClock.start();
    m_timer.start();
}

void PlayerController::setWorld(World *w)
{
    if (m_world == w) return;
    m_world = w;
    emit worldChanged();
}

void PlayerController::setHotbar(Hotbar *h)
{
    if (m_hotbar == h) return;
    m_hotbar = h;
    emit hotbarChanged();
}

void PlayerController::setItemEntities(ItemEntityManager *m)
{
    if (m_itemEntities == m) return;
    m_itemEntities = m;
    emit itemEntitiesChanged();
}

void PlayerController::onWindowChanged(QQuickWindow *win)
{
    if (m_window) m_window->removeEventFilter(this);
    m_window = win;
    if (m_window) m_window->installEventFilter(this);
    else {
        if (m_captured) release(); // 窗口销毁 → 安全释放
        cancelMining();            // 防御：清累积挖掘态（避免失窗口后仍在 tick）
    }
}

// ---- 输入 ----
void PlayerController::setKey(int key, bool pressed)
{
    const bool wasDown = m_keys.value(key);
    // 飞行门控（t21）：双击空格切飞仅 Creative（Spectator 常驻 noclip 无需切；Survival canFly()=false，
    // 永不进入此分支 → 双击空格不触发飞行，重力生效）。真实按下（非按住自动重复）→ 双击 ≤300ms 切换。
    if (key == Qt::Key_Space && pressed && !wasDown && m_mode == Creative) {
        const qint64 now = m_evtClock.elapsed();
        if (now - m_lastSpaceMs < 300) {
            m_flying = !m_flying;
            m_lastSpaceMs = -100000; // 防三连误触
            m_vel.setY(0);
            // t51：起飞退出疾跑/蹲下（飞行态 Walk 状态机不适用；spec「疾跑/蹲下仅走路模式」）。
            if (m_moveState != Walk) setMoveState(Walk);
            emit flyingChanged();
        } else {
            m_lastSpaceMs = now;
        }
    }
    // 移动状态机（t51）：双击 W 疾跑 / Shift 按住蹲下。仅走路模式（Survival / Creative-未飞）有效；
    // Spectator 与 Creative-飞态下 Shift 作「下降」用、W 无疾跑语义 → 不触发状态切换。
    // 优先级：Shift（蹲）覆盖 W（疾跑）—— 蹲下时按/松 W 不动蹲态，仅松 Shift 才回 Walk。
    //   双击 W 仅从 Walk 进 Sprint（蹲态不进）；松 W 退 Sprint；松 Shift 退 Crouch。
    //   真实按下（非自动重复）才参与双击检测（与 Space 同；QML 已过滤 autorepeat，此处 wasDown 再保险）。
    if (key == Qt::Key_Shift) {
        const bool canCrouch = (m_mode == Survival || (m_mode == Creative && !m_flying));
        if (pressed && canCrouch) {
            if (m_moveState != Crouch) setMoveState(Crouch);
        } else if (!pressed) {
            if (m_moveState == Crouch) setMoveState(Walk);
        }
    } else if (key == Qt::Key_W) {
        const bool canSprint = (m_mode == Survival || (m_mode == Creative && !m_flying));
        if (pressed && !wasDown && canSprint) {
            const qint64 now = m_evtClock.elapsed();
            if (m_moveState == Walk && (now - m_lastWms < 300)) {
                setMoveState(Sprint);
                m_lastWms = -100000; // 防三连误触（双击成功后立即消费）
            } else {
                m_lastWms = now;
            }
        } else if (!pressed) {
            // 松 W 退出疾跑（spec「松 W 回 Walk」）。蹲态下松 W 不动（Shift 仍按 → 仍蹲）。
            if (m_moveState == Sprint) setMoveState(Walk);
        }
    }
    m_keys.insert(key, pressed);
}

void PlayerController::cycleMode() { setMode(static_cast<Mode>((static_cast<int>(m_mode) + 1) % 3)); }

// F5 相机模式循环（t27）：第一人称 → 第三人称-后 → 第三人称-前 → 回第一人称（0→1→2→0）。
// 仅改标志 + 通知 QML（相机摆位在 Main.qml 据 cameraMode 算 position/eulerRotation）。
void PlayerController::cycleCamera()
{
    m_cameraMode = static_cast<CameraMode>((static_cast<int>(m_cameraMode) + 1) % 3);
    emit cameraModeChanged();
}
void PlayerController::setMode(Mode m)
{
    if (m == m_mode) return;
    m_mode = m;
    m_vel = QVector3D(0, 0, 0); // 切模式清速度，避免半空切走生存被甩飞
    m_onGround = false;
    m_peakY = m_pos.y();        // 重置掉落基准：避免从创造飞行高度切生存时累计陈旧落差
    if (m_flying) { m_flying = false; emit flyingChanged(); } // 进入新模式默认走（不飞）
    // t51：切模式清移动状态机（疾跑/蹲下不跨模式延续；新模式的 Shift/W 上下文不同，从 Walk 起最稳）。
    if (m_moveState != Walk) setMoveState(Walk);
    emit modeChanged();
    emit onGroundChanged();
}

// ---- 指针锁定 ----
void PlayerController::grab()
{
    if (m_captured || !m_window) return;
    setCaptured(true);
    QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor)); // 全局隐藏光标（最可靠；release 配对 restore）
    QCursor::setPos(windowCenterGlobal());                         // 先居中，首次 delta 从中心起算
}

void PlayerController::release()
{
    if (!m_captured) return;
    QGuiApplication::restoreOverrideCursor(); // 恢复光标 = 可点暂停菜单
    m_keys.clear();                           // 丢弃按住的 WASD，防恢复时前冲
    setCaptured(false);
    clearHit();                               // 暂停 → 隐藏线框（未捕获时不选中）
    cancelMining();                           // t34：暂停 / 失焦 → 清累积挖掘态（spec：失焦清零）
    m_leftDown = false;                       // t44：暂停 / 失焦 → 视同松手（切断续挖）
    // t51：暂停 / 失焦时退出疾跑 / 蹲下（恢复时从 Walk 起；避免遗留蹲态卡低视角 / 疾跑余速）。
    if (m_moveState != Walk) setMoveState(Walk);
}

void PlayerController::setCaptured(bool c)
{
    if (m_captured == c) return;
    m_captured = c;
    emit capturedChanged();
}

QPoint PlayerController::windowCenterGlobal() const
{
    if (!m_window) return QCursor::pos();
    return m_window->mapToGlobal(QPoint(m_window->width() / 2, m_window->height() / 2));
}

void PlayerController::pollMouse()
{
    if (!m_window) return;
    const QPoint c = windowCenterGlobal();
    const QPoint p = QCursor::pos();
    const int dx = p.x() - c.x(), dy = p.y() - c.y();
    if (dx || dy) {
        m_yaw -= dx * kSens; // 鼠标左移 → 视角左转
        m_pitch = std::clamp(m_pitch - dy * kSens, -89.0f, 89.0f); // 屏幕Y下→上抬；夹±89防翻转
        emit yawChanged();
        emit pitchChanged();
        emit lookChanged(); // 视线方向随 yaw/pitch 变（第三人称相机偏移绑定刷新，t27）
        QCursor::setPos(c); // 每帧回中 → 永不撞屏边（无限旋转）
    }
}

bool PlayerController::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_window) {
        if (e->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(e);
            if (k->key() == Qt::Key_Escape && m_captured) { release(); return true; }
        } else if (e->type() == QEvent::WindowDeactivate || e->type() == QEvent::FocusOut) {
            if (m_captured) release(); // 切走绝不留「锁住的光标」
        } else if (e->type() == QEvent::MouseButtonPress) {
            // 破/放（t05 + t34）：仅指针捕获时由窗口级事件过滤接管（未捕获时不消费 → 让暂停层 grab）。
            // 走事件过滤而非 MouseArea —— 指针锁定下光标每帧被 warp 回中，MouseArea 依赖的指针
            // 位置不可靠；窗口级 MouseButtonPress 与光标位置无关，最稳。
            // t34：左键按下走 beginMining（创造瞬破 / 生存开始累积），不再单击直破 —— 由 beginMining
            // 内按模式分流（spec：创造单击瞬破 = 单次边缘；生存 = 按住累积）。
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_captured) {
                if (me->button() == Qt::LeftButton)   { beginMining(); return true; }
                if (me->button() == Qt::RightButton)  { placeBlock();  return true; }
                if (me->button() == Qt::MiddleButton) { pickBlock();   return true; } // t37 pick block
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            // t34：左键松开 → 清生存累积进度（创造不进入累积态，endMining 内 no-op）。
            // 仍只在捕获时消费（与 press 对称；未捕获时 release 不应破坏其它层的光标交互）。
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_captured && me->button() == Qt::LeftButton) {
                endMining();
                return true;
            }
        }
    }
    return QQuickItem::eventFilter(o, e);
}

// ---- 主循环 ----
void PlayerController::tick()
{
    const qreal dt = qMin(m_clock.restart() / 1000.0, 0.05); // 钳 50ms，防卡顿后穿墙
    if (!m_captured) {
        cancelMining(); // 暂停（含背包开 / 失焦）：清累积挖掘态（spec：失焦清零）
        // t45：暂停时清行走动画驱动（moveSpeed→0；walkPhase 不动，QML 据此 sin*0=0 → 四肢归中性位）。
        // 仅值真变时发，免每 tick 无谓刷新 QML 绑定。
        if (m_moveSpeed != 0.0f) { m_moveSpeed = 0.0f; emit moveSpeedChanged(); }
        return;
    }
    pollMouse();
    step(dt);
    updateRaycast();   // 沿视线 DDA 选体 → 更新线框命中态
    updateCameraDistance(); // t40：第三人称相机距离钳制（防穿墙）
    updateMining(float(dt)); // t34：累积生存挖掘进度（创造不进入此态；无操作时早 return）
    pickupScan();      // t36：扫附近掉落实体 → Hotbar.addStack → 命中则销毁实体
}

// 视线方向：用与相机相同的欧拉→四元数（QQuaternion::fromEulerAngles(pitch,yaw,0)）旋转
// 相机本地 -Z，保证射线与渲染出的视线**完全同向**（不靠手写 pitch 符号约定，消除方向歧义）。
// pitch=0 时退化为水平前向 (-sin(yaw),0,-cos(yaw))，与 wishHoriz 一致。
QVector3D PlayerController::lookDirection() const
{
    const QQuaternion q = QQuaternion::fromEulerAngles(m_pitch, m_yaw, 0.0f);
    return q.rotatedVector(QVector3D(0.0f, 0.0f, -1.0f));
}

void PlayerController::updateRaycast()
{
    if (!m_world) { clearHit(); return; }
    const RayHit h = raycastVoxel(*m_world, position(), lookDirection(), kReach);

    // 仅在命中态/格坐标/法线真正变化时 emit，避免每帧无谓刷新 QML 绑定。
    const bool changed = (h.valid != m_hasHit)
        || (h.valid && (h.bx != m_hitBx || h.by != m_hitBy || h.bz != m_hitBz
                        || qint32(h.nx) != m_hitNx || qint32(h.ny) != m_hitNy || qint32(h.nz) != m_hitNz));
    if (!changed) return;

    m_hasHit = h.valid;
    m_hitBx = h.bx; m_hitBy = h.by; m_hitBz = h.bz;
    m_hitNx = qint32(h.nx); m_hitNy = qint32(h.ny); m_hitNz = qint32(h.nz);
    emit hitChanged();
}

void PlayerController::clearHit()
{
    if (!m_hasHit) return;
    m_hasHit = false;
    m_hitNx = m_hitNy = m_hitNz = 0;
    emit hitChanged();
}

// 第三人称相机距离钳制（t40）：每帧从眼位沿相机偏移方向 DDA，返回首个实体命中距离（留余量贴在面前）。
//   ThirdPersonBack：相机在玩家身后 → 偏移方向 = -look（射线往身后打）；
//   ThirdPersonFront：相机在玩家身前 → 偏移方向 = +look。
// 复用 raycastVoxel（RayHit.dist = 起点到命中面的欧氏距离）。命中 → dist - kCamMargin（贴面前、防
// z-fight / 近裁面穿插），floor 到 0（墙贴近眼 → 退到眼位）；无命中（含起点嵌实体的退化）→ kCamMax。
// 第一人称恒 0（不偏移，相机贴眼）。仅值真变时发 cameraDistanceChanged——DDA 对同一 (eye,look,世界)
// 输入确定，玩家不动/不转时距离帧间稳定，无抖动。
// 分层（PLAN §2）：本层只读 World（blockAt/isSolid），与选体 raycast 同源；相机摆位仍在 QML 呈现层。
void PlayerController::updateCameraDistance()
{
    if (m_cameraMode == FirstPerson) {
        if (m_cameraDistance != 0.0f) {
            m_cameraDistance = 0.0f;
            emit cameraDistanceChanged();
        }
        return;
    }
    float d = kCamMax;
    if (m_world) {
        const QVector3D look = lookDirection();
        const QVector3D dir = (m_cameraMode == ThirdPersonBack) ? -look : look;
        const RayHit h = raycastVoxel(*m_world, position(), dir, kCamMax);
        if (h.valid) {
            d = h.dist - kCamMargin;
            if (d < 0.0f) d = 0.0f; // 墙贴近眼位 → 退到眼（极端情形，相机近乎第一人称）
        }
    }
    if (d != m_cameraDistance) {
        m_cameraDistance = d;
        emit cameraDistanceChanged();
    }
}

// 命中面中心 = 方块中心 + (0.5 + eps)*法线：贴在该面上（非方块几何中心），
// eps 外推防与方块自身面 z-fight。
QVector3D PlayerController::hitFaceCenter() const
{
    constexpr float eps = 0.01f;
    return QVector3D(m_hitBx + 0.5f, m_hitBy + 0.5f, m_hitBz + 0.5f)
         + QVector3D(m_hitNx, m_hitNy, m_hitNz) * (0.5f + eps);
}

// 把规范线框（XY 平面、+Z 法线）摆到命中面：六种法线各对应一个欧拉角。
QVector3D PlayerController::hitFaceEuler() const
{
    if (m_hitNx > 0) return QVector3D(0, 90, 0);    // +X 面
    if (m_hitNx < 0) return QVector3D(0, -90, 0);   // -X 面
    if (m_hitNy > 0) return QVector3D(90, 0, 0);    // +Y（顶）面
    if (m_hitNy < 0) return QVector3D(-90, 0, 0);   // -Y（底）面
    if (m_hitNz < 0) return QVector3D(0, 180, 0);   // -Z 面
    return QVector3D(0, 0, 0);                      // +Z 面（与规范同向，不转）
}

// ---- 方块编辑（t05 + t34）----
// 编辑入口属 Game/Physics 层：输入 →（已有）射线命中 → World::setBlock。Renderer 不直接改栅格。
void PlayerController::setSelectedBlock(int id)
{
    if (id < 0 || id >= int(BlockRegistry::Count)) return; // 仅 clamp 到合法 id 区间
    if (id == m_selectedBlock) return;
    m_selectedBlock = id;
    emit selectedBlockChanged();
}

// 手持物品原始 id（t34）：方块段直接透传；工具段（>=0x100）合法也接受（挖掘速度查 ToolRegistry
// 用）。非法 / 越段 id 静默拒（与 setSelectedBlock 不同：此处无上界，因工具段 > BlockRegistry::Count）。
void PlayerController::setSelectedItem(int id)
{
    if (id == m_selectedItem) return;
    m_selectedItem = id;
    emit selectedItemChanged();
}

// 左键按下（t34）：按模式分流。
//   Creative：瞬破（progress 等价 1.0）→ 立即 setBlock(air) + swingArm + playerMined(drop=false)。
//             spec：创造单击瞬破、不掉落。
//   Survival：进入累积态（mining=true，记录目标格，progress=0），由 updateMining 每 tick 推进。
//             spec：按住左键累积进度，进度满才破。
//   Spectator：canBreak()=false → 直接返回（不破 / 不进累积）。
// 共用门控：未捕获 / 无命中 → 不动作。无世界 / 无窗口 → 不动作。
// 兼容：breakBlock()（旧 Q_INVOKABLE）等价调本方法（创造瞬破 / 生存开始累积）。
void PlayerController::beginMining()
{
    // t44 连续挖掘：记录物理左键按下态。置于所有早 return 之前 —— 即便当前不能开始累积
    // （观察者 / 无命中 / 暂停），按钮按下这一事实仍成立，后续 updateMining 据此 + 新命中自动续挖。
    m_leftDown = true;
    if (!canBreak()) return; // 观察者不能破块（t21）
    if (!m_world || !m_captured || !m_hasHit) return;

    if (m_mode == Creative) {
        // 创造：瞬破（progress 直接 1.0 等价），不掉落。仍发 swingArm（动作真发生）。
        // 不进入累积态（mining 留 false）→ 不显裂纹叠层（瞬破无需裂纹）。
        // finishMiningAt 内自行取原方块 id（setBlock 前）发 playerMined；此处无需重复读取。
        finishMiningAt(m_hitBx, m_hitBy, m_hitBz, /*drop=*/false);
        return;
    }

    // Survival：开始累积。重置目标 / 进度 / stage；stage 从 0 起（裂纹首阶立显，反馈即时）。
    m_mining = true;
    m_mineBx = m_hitBx; m_mineBy = m_hitBy; m_mineBz = m_hitBz;
    m_miningProgress = 0.0f;
    m_miningStage = 0;
    emit miningStateChanged();
    emit miningProgressChanged();
    emit swingArm(); // 起手挥动（持续挖掘期间每阶切换再补发，形成挥动循环）
}

// 左键松开（t34）：清累积进度。创造模式下未进入累积态（mining=false）→ no-op。
// t44：同步清 m_leftDown（按钮物理松开）—— 切断连续挖掘的续挖条件（spec：松手清零）。
void PlayerController::endMining()
{
    m_leftDown = false;
    if (!m_mining) return;
    cancelMining();
}

// 清累积挖掘态（松开 / 换目标 / 失焦 / 暂停 / 完成）。无累积时静默（不发信号，免抖动）。
void PlayerController::cancelMining()
{
    if (!m_mining && m_miningStage < 0) return;
    m_mining = false;
    m_miningProgress = 0.0f;
    m_miningStage = -1;
    emit miningStateChanged();
    emit miningProgressChanged();
}

// 完成（progress 满 / 创造瞬破）：写 air + 发 playerMined + swingArm + 清态。
// drop 由 caller 决定（创造=false；生存=ToolRegistry::canHarvest(原方块, 手持物)）。
// 取原方块 id（setBlock 前）作 playerMined 的 blockId 参数（t35 据此 spawn 对应物品）。
void PlayerController::finishMiningAt(int x, int y, int z, bool drop)
{
    if (!m_world) return;
    const quint8 brokenId = m_world->blockAt(x, y, z);
    m_world->setBlock(x, y, z, BlockRegistry::Air); // → World 发 blockBroken（粒子触发）+ worldChanged（mesh 重建）
    emit playerMined(x, y, z, int(brokenId), drop); // 破块语义事件（含 drop 标志；当前无消费端，留扩展）
    // t43：生存挖出可掉落方块 → **走实体流**（emit spawnItem），移除 commit a3e9300 的 auto-collect
    // （原直接 addStack）。掉落物落到该格地面、玩家走近 ≤kPickupDist 时经 pickupScan 拾取 → addStack
    // （先选中槽、再空槽，智能堆叠至 maxStack）。满栈不进背包则实体留地面（spec：全满→不拾取）。
    // drop 由 caller 算（生存走 ToolRegistry::canHarvest；创造瞬破 drop=false 不发）。
    if (drop) {
        emit spawnItem(x, y, z, int(brokenId));
    }
    emit swingArm();                                // 破块成功 → 第一人称手挥动（t29）
    cancelMining();                                 // 清累积态（裂纹叠层隐藏）
}

// 每 tick 推进生存挖掘进度（t34）+ 连续续挖（t44）。创造不进入此态（beginMining 内瞬破已 return）。
// spec：progress += dt * speed(block, tool)；speed = 1 / miningTime（ToolRegistry 已含 hardness/speedMul）。
//   - 失命中 / 目标已被破（变 air）→ cancelMining。
//   - 目标换（玩家转头）→ 重置 progress（spec：换目标清零），目标格更新。
//   - stage 推进（progress 跨 1/6 阈值）→ 发 miningStateChanged（驱动 QML 切裂纹贴图）+ swingArm（挥动循环）。
//   - progress >= 1.0 → finishMiningAt（drop = canHarvest）。
// t44 连续挖掘：finishMiningAt 内 cancelMining 清 m_mining，但 m_leftDown 仍 true（左键未松）。下一 tick
//   顶部检测到「未在累积 + 左键仍按 + Survival + 命中新可挖块」→ 自动 beginMining 新目标（progress 归 0
//   继续），不松手连挖。停止条件：松手（m_leftDown=false）/ 视线无命中（m_hasHit=false，含出射程——
//   raycast 限 kReach）/ 新目标不可挖。spec：长按沿一直线连续破多块直到射程外。速度 / 掉落 / 可挖
//   全走 ToolRegistry（t42 方块表）：canMine(可挖) / miningTime(速度) / canHarvest(掉落)。
void PlayerController::updateMining(float dt)
{
    // t44 连续挖掘：左键仍按但当前未累积（刚破完一块 m_mining 被 cancelMining 清 / 或目标刚进入视线）
    // → 若命中可挖块则自动 beginMining 新目标（progress 归 0），无需松手重按。仅 Survival：Creative
    // 单击瞬破不进 updateMining（beginMining 内直接 finishMiningAt return）；Spectator canBreak=false。
    // 用 ToolRegistry::canMine 判可挖（走 BlockDef：实体且 hardness>0），air / 越界 / 不可破坏 → false。
    if (!m_mining && m_leftDown && m_mode == Survival && m_world && m_hasHit) {
        const quint8 hb = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
        if (ToolRegistry::canMine(hb)) {
            beginMining(); // 重新进入累积：m_mining=true / 目标=当前命中 / progress=0 / stage=0
        }
    }

    if (!m_mining) return;
    if (!m_world || !m_hasHit) { cancelMining(); return; }

    // 目标换：清进度（保留 mining=true），更新目标格。stage 同步重置为 0（裂纹首阶）。
    if (m_hitBx != m_mineBx || m_hitBy != m_mineBy || m_hitBz != m_mineBz) {
        m_mineBx = m_hitBx; m_mineBy = m_hitBy; m_mineBz = m_hitBz;
        m_miningProgress = 0.0f;
        if (m_miningStage != 0) {
            m_miningStage = 0;
            emit miningStateChanged();
        }
        emit miningProgressChanged();
    }

    // 目标已被破（其它途径）/ 不可挖 → 取消。可挖判定走 ToolRegistry::canMine（t42 方块表：
    // 实体且 hardness>0；air / 越界 / 基岩=false），取代原 bid==Air 硬比较（spec：可挖走 BlockDef）。
    const quint8 bid = m_world->blockAt(m_mineBx, m_mineBy, m_mineBz);
    if (!ToolRegistry::canMine(bid)) { cancelMining(); return; }

    // 速度：miningTime = hardness / speedMul（ToolRegistry），progress 增量 = dt / miningTime。
    // canMine 已保 hardness>0 → miningTime 经 max(t,0.05) 地板恒 >0，无除零。
    const float miningTime = ToolRegistry::miningTime(bid, m_selectedItem);
    m_miningProgress += dt / miningTime;

    // stage 推进：clamp(progress*6, 0, 5)。每跨一阶发 miningStateChanged（切贴图）+ swingArm（挥动循环）。
    const int newStage = std::clamp(int(m_miningProgress * 6.0f), 0, 5);
    if (newStage != m_miningStage) {
        m_miningStage = newStage;
        emit miningStateChanged();
        emit swingArm();
    }
    emit miningProgressChanged();

    // 完成：progress 满 → 破块。drop 走 ToolRegistry::canHarvest（生存可采掘判定）。
    // finishMiningAt 内 cancelMining 清 m_mining=false；m_leftDown 不动 → 下一 tick 顶部续挖分支接手。
    if (m_miningProgress >= 1.0f) {
        const bool drop = ToolRegistry::canHarvest(bid, m_selectedItem);
        finishMiningAt(m_mineBx, m_mineBy, m_mineBz, drop);
    }
}

// 左键单击（兼容 t05 旧调用 / QML）：等价 beginMining —— 创造瞬破 / 生存开始累积。
// 注：mouseRelease 路径下若仅调用本方法（无对应 endMining），生存会一直累积 —— 故 t34 已把
// MouseButtonRelease 也接入 endMining。直接调本方法的调用者（如有）需自行配对 endMining。
void PlayerController::breakBlock()
{
    beginMining();
}

// 右键：命中面法线方向的相邻空格置当前手持方块。
// 校验：目标格须为空气（不覆盖实体）；且不与玩家 AABB 重叠（防自埋/卡死）。
// 模式门控（t21）：观察者不能放块（用户核心诉求）——在调用 World::setBlock 前拦截。
// t29：放块动作真发生（通过门控 + 目标为空 + 不埋玩家，实际写入）时才发 swingArm；
// 已有方块/重叠被拒不发（仅动作真发生时，spec）。
// t50：命中格若为工作台（CraftingTable）→ 右键打开 3×3 合成 UI（不放置；发 craftingTableOpened）。
// 机制等价 MC 右键工作台。在所有放置校验之前拦截（无论手持何物，右键工作台即开界面）。
void PlayerController::placeBlock()
{
    if (!canPlace()) return; // 观察者不能放块
    if (!m_world || !m_captured || !m_hasHit) return;
    // t50：右键工作台 → 打开 3×3 合成 UI（优先于放置；spec「右键工作台开 3×3」）。
    if (m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::CraftingTable) {
        emit craftingTableOpened();
        return;
    }
    if (m_selectedBlock == BlockRegistry::Air) return; // 空栈 → 右键不放置（也不挥手，t32）
    const int tx = m_hitBx + m_hitNx, ty = m_hitBy + m_hitNy, tz = m_hitBz + m_hitNz;
    if (m_world->blockAt(tx, ty, tz) != BlockRegistry::Air) return; // 已有方块 → 不放
    if (overlapsPlayerAABB(tx, ty, tz)) return;                    // 与玩家重叠 → 不放
    m_world->setBlock(tx, ty, tz, quint8(m_selectedBlock));
    emit swingArm(); // 放块成功 → 第一人称手挥动（t29）
}

// Q 键丢弃（t36）：从选中槽 takeStack 1 件 → 发 spawnItem（玩家前方 1.5 格）。
// 仅指针捕获时生效（spec：「Q 键（captured 时）」）。取失败（空栈 / 无 hotbar）→ 不丢。
// spawnItem 经 QML Connections 转发到 ItemEntityManager.spawnItem（同破块掉落 t35 路径），
// 丢弃后实体在前方生成 → 可被重新拾取（闭环）。丢弃位置取眼位 + 视线 * 1.5，floor 到格坐标。
void PlayerController::dropHeld()
{
    if (!m_captured) return;        // spec：仅捕获时
    if (!m_hotbar) return;
    const int id = m_hotbar->selectedItemId();
    if (id == 0) return;            // 空手 → 不丢
    const int took = m_hotbar->takeStack(m_hotbar->selectedSlot(), 1);
    if (took <= 0) return;          // 取失败（空栈）→ 不丢
    // 眼位前方 1.5 格，floor 到整数格（ItemEntityManager 存格中心 = 整数+0.5）。
    const QVector3D fwd = lookDirection();
    const QVector3D p = position() + fwd * 1.5f;
    emit spawnItem(int(std::floor(p.x())), int(std::floor(p.y())), int(std::floor(p.z())), id);
}

// 拖出背包丢弃（t49）：光标手持栈整栈丢弃为实体（玩家前方）。不限捕获态（背包打开时正是未捕获）。
// 清空 hotbar 光标手持栈（setHeldBlock(0) 同步清 count），再 emit spawnItem。空手 / 无 hotbar → 不丢。
// 位置同 dropHeld：眼位 + 视线 * 1.5，floor 到格坐标（ItemEntityManager 存格中心 = 整数+0.5）。
void PlayerController::dropHeldCursor()
{
    if (!m_hotbar) return;
    const int id = m_hotbar->heldBlock();
    if (id == 0 || m_hotbar->heldCount() <= 0) return; // 空手 → 不丢
    m_hotbar->setHeldBlock(0);                         // 清空光标手持栈（id=0 同步清 count）
    const QVector3D fwd = lookDirection();
    const QVector3D p = position() + fwd * 1.5f;
    emit spawnItem(int(std::floor(p.x())), int(std::floor(p.y())), int(std::floor(p.z())), id);
}

// 中键拾取方块（t37 pick block）：取当前射线命中格的方块 id → 写入 hotbar 当前选中槽（覆盖；
// 仅指针捕获时生效（与破/放同窗口级事件过滤路径；spec）。命中空气 / 无命中 / 无世界 / 无 hotbar → 不动作。
// 数量按模式分（spec 示意 {id,1}；此处对齐模式语义：创造源无限 → 满栈，与 resetForMode 创造默认一致、
// 不把既有 64 回退成 1 造成视觉突兀；生存有限背包 → 单件）。pick 属「选择」不改栅格，三模式均允许。
// 走 Hotbar::setStack 直接覆盖选中槽（Hotbar 内部校验范围 + id 合法性 + count 上限）。
void PlayerController::pickBlock()
{
    if (!m_captured || !m_hasHit || !m_world || !m_hotbar) return;
    const quint8 id = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
    if (id == BlockRegistry::Air) return; // 命中空气 → 无可拾取
    const int count = (m_mode == Creative) ? m_hotbar->maxStackSize(int(id)) : 1;
    m_hotbar->setStack(m_hotbar->selectedSlot(), int(id), count);
}

// 拾取扫描（t36）：每帧扫附近掉落实体 → Hotbar.addStack（先选中槽、再空槽，「入手」语义）。
// addStack 返 0（全放入）→ ItemEntityManager.removeAt 销毁实体（spec：拾取后销毁）；
// 返 >0（背包满）→ 不拾取（entity 留；spec：全满 → 不拾取）。距离从玩家 AABB 中心（脚底 + 半高）
// 3D 起算，阈值 kPickupDist；从后往前扫 → erase 不影响前面索引（安全边删边迭代）。
// t51：AABB 高用 m_height（蹲下变矮 → 中心随之降低，仍贴近玩家实际占据空间）。
// t53：跳过新生免拾取期内的实体（isPickupReady=false）——破块瞬间实体常在玩家近旁（脚下方块 ~1.4 格
//   < kPickupDist 1.5），若无延迟则下一帧即被收走、玩家永远看不见（疑似 auto-collect 根因）。让实体
//   先可见 0.5s 再开放拾取（机制等价 MC block-break pickup delay）。
void PlayerController::pickupScan()
{
    if (!m_itemEntities || !m_hotbar) return;
    const QVector3D center = m_pos + QVector3D(0.0f, m_height * 0.5f, 0.0f);
    const float r2 = kPickupDist * kPickupDist;
    for (int i = m_itemEntities->count() - 1; i >= 0; --i) {
        if (!m_itemEntities->isPickupReady(i)) continue; // t53：新生 0.5s 免拾取（让实体先可见再可拾）
        const QVector3D d = m_itemEntities->posAt(i) - center;
        if (d.lengthSquared() > r2) continue;          // 超阈值 → 跳过
        const int id = m_itemEntities->itemIdAt(i);
        const int leftover = m_hotbar->addStack(id, 1); // 先选中槽（空/同 id）→ 再空槽
        if (leftover == 0) m_itemEntities->removeAt(i); // 全入 → 销毁实体；背包满则留（leftover>0）
    }
}

// 方块格 [bx,bx+1]×[by,by+1]×[bz,bz+1] 与玩家 AABB 是否相交（严格重叠；仅贴面不算）。
// t51：AABB 高用 m_height（蹲下变矮 → 放置校验随之放宽，玩家头顶不再误判阻挡）。
bool PlayerController::overlapsPlayerAABB(int bx, int by, int bz) const
{
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(),           maxy = m_pos.y() + m_height;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    return minx < float(bx + 1) && maxx > float(bx)
        && miny < float(by + 1) && maxy > float(by)
        && minz < float(bz + 1) && maxz > float(bz);
}

QVector3D PlayerController::wishHoriz() const
{
    const float yr = m_yaw * kDeg;
    const float fx = -std::sin(yr), fz = -std::cos(yr); // 前
    const float rx = std::cos(yr), rz = -std::sin(yr);  // 右
    float dx = 0, dz = 0;
    if (m_keys.value(Qt::Key_W)) { dx += fx; dz += fz; }
    if (m_keys.value(Qt::Key_S)) { dx -= fx; dz -= fz; }
    if (m_keys.value(Qt::Key_D)) { dx += rx; dz += rz; }
    if (m_keys.value(Qt::Key_A)) { dx -= rx; dz -= rz; }
    QVector3D w(dx, 0, dz);
    if (w.lengthSquared() > 0) w.normalize();
    return w;
}

// 移动状态速率因子（t51）：Sprint×1.3 / Crouch×0.4 / Walk×1.0。
//   仅走路模式（Survival / Creative-未飞）的水平速度乘此值；飞 / Spectator 恒 Walk（speedMul=1）。
//   spec「疾跑移速 ×1.3、蹲下 ×0.4」。同时驱动 moveSpeed 报告 → walkPhase 推进频率 → 四肢摆频。
float PlayerController::speedMul() const
{
    switch (m_moveState) {
    case Sprint: return 1.3f;
    case Crouch: return 0.4f;
    default:     return 1.0f; // Walk
    }
}

// 切移动态（t51）：同步更新 AABB 高 / 眼位（蹲下变矮、相机随之降低）。无变化静默。
//   蹲下：m_height=kCrouchHeight(1.5) / m_eyeHeight=kCrouchEye(1.35)；
//   站起（Walk/Sprint）：m_height=kHeight(1.8) / m_eyeHeight=kEyeHeight(1.62)。
//   相机 position 读 m_eyeHeight → 蹲下相机自动降低（无需 QML 额外处理）。
void PlayerController::setMoveState(MoveState s)
{
    if (s == m_moveState) return;
    m_moveState = s;
    if (s == Crouch) {
        m_height = kCrouchHeight;
        m_eyeHeight = kCrouchEye;
    } else {
        m_height = kHeight;
        m_eyeHeight = kEyeHeight;
    }
    emit moveStateChanged();
    emit positionChanged(); // 眼位随蹲 / 站变 → 相机 position 绑定刷新（蹲下相机降低）
}

// 蹲下「边缘安全」支撑判定（t51）：给定水平位置 (x,z)，当前脚位 m_pos.y() 下方 0.05 处那一格
//   在 AABB footprint（kHalfW 宽）内任一列实体即算「有支撑」。step() 据此判定蹲下时是否允许
//   水平移动（无支撑 → 不移动，防走下边缘）。仅读 World（isSolid），与碰撞同层。
//   注：脚底 y = 站立方块顶面（整数 + eps）；其下一格 = floor(y - 0.05)。仅着地时调用（见 step）。
bool PlayerController::hasGroundBelowAt(float x, float z) const
{
    if (!m_world) return false;
    const float checkY = m_pos.y() - 0.05f;          // 脚底略下 → 支撑方块所在格
    const int by = int(std::floor(checkY));
    const float minx = x - kHalfW, maxx = x + kHalfW;
    const float minz = z - kHalfW, maxz = z + kHalfW;
    // 严格覆盖（与 aabbHitsSolid 同取样策略）：AABB footprint 内所有可能列
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int zz = z0; zz <= z1; ++zz)
        for (int xx = x0; xx <= x1; ++xx)
            if (m_world->isSolid(xx, by, zz)) return true;
    return false;
}

bool PlayerController::aabbHitsSolid() const
{
    if (!m_world) return false;
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(), maxy = m_pos.y() + m_height; // t51：蹲下用 m_height（变矮）
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    // 严格重叠：ceil(max)-1 排除「仅贴面」的方块 → 防卡缝
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int y0 = int(std::floor(miny)), y1 = int(std::ceil(maxy)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (m_world->isSolid(x, y, z)) return true;
    return false;
}

// 沿单轴移动 amount；碰撞则贴面 + eps + 清该轴速度。无世界则自由移动。
// t51：AABB 高用 m_height（蹲下变矮 → 头顶碰撞 / 着地贴面随之变；顶头贴面按当前高度算）。
void PlayerController::moveAxis(int axis, float amount)
{
    if (amount == 0) return;
    switch (axis) {
    case 0: m_pos.setX(m_pos.x() + amount); break;
    case 1: m_pos.setY(m_pos.y() + amount); break;
    case 2: m_pos.setZ(m_pos.z() + amount); break;
    }
    if (!m_world || !aabbHitsSolid()) return;

    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(), maxy = m_pos.y() + m_height;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    const float eps = 1e-4f;
    switch (axis) {
    case 0:
        if (amount > 0) m_pos.setX(std::floor(maxx) - kHalfW - eps);
        else            m_pos.setX(std::floor(minx) + 1.f + kHalfW + eps);
        m_vel.setX(0); break;
    case 1:
        if (amount > 0) m_pos.setY(std::floor(maxy) - m_height - eps); // 顶头（按当前高度）
        else            m_pos.setY(std::floor(miny) + 1.f + eps);      // 着地
        m_vel.setY(0); break;
    case 2:
        if (amount > 0) m_pos.setZ(std::floor(maxz) - kHalfW - eps);
        else            m_pos.setZ(std::floor(minz) + 1.f + kHalfW + eps);
        m_vel.setZ(0); break;
    }
}

void PlayerController::step(qreal dt)
{
    const QVector3D wish = wishHoriz();
    const bool space = m_keys.value(Qt::Key_Space), shift = m_keys.value(Qt::Key_Shift);
    const bool spaceEdge = space && !m_spacePrev; // 跳跃边沿：长按只跳一次（生存/创造-走路统一）
    m_spacePrev = space;

    // 行走动画驱动（t45）：moveSpeed 仅走路模式（Survival / Creative-未飞）按住 WASD 时非零；
    //   Spectator / Creative-飞 → 0（飞行/幽灵态无走步动画，spec 未要求；为未来泳/飞姿留接口）。
    //   walkPhase 仅在走时累加（speed*dt*kStrideRate），2π 回绕；静止不累加 → QML 据此 sin*0=0 中性位。
    //   「按住 WASD 撞墙」时 wish 仍非零（玩家在「尝试」走）→ 腿仍摆，对齐 MC 行为。
    //   t51：moveSpeed 乘 speedMul（疾跑 ×1.3 / 蹲 ×0.4）→ walkPhase 推进速率随状态变（动画频率）。
    //   分层：动画驱动数据由 Game/Physics tick 算出，QML 呈现层只读消费（同 swingArm 模式）。
    {
        const bool moving = wish.lengthSquared() > 0.001f; // wish 已 normalize：非零即有 WASD 输入
        const float walk = (moving && (m_mode == Survival || (m_mode == Creative && !m_flying)))
                         ? kWalk * speedMul() : 0.0f;
        if (walk != m_moveSpeed) { m_moveSpeed = walk; emit moveSpeedChanged(); }
        // 相位推进（仅走时；走时每 tick 发 walkPhaseChanged 供 QML 重算四肢欧拉角，~60Hz）。
        if (m_moveSpeed > 0.1f) {
            m_walkPhase += m_moveSpeed * float(dt) * kStrideRate;
            if (m_walkPhase >= 6.28318530718f) m_walkPhase -= 6.28318530718f; // 2π 回绕
            emit walkPhaseChanged();
        }
    }

    if (m_mode == Spectator) {
        QVector3D v = wish * kFly;
        if (space) v.setY(kFly);
        if (shift) v.setY(-kFly);
        m_pos += v * dt; // noclip
        emit positionChanged();
        return;
    }

    if (m_mode == Creative && m_flying) {
        QVector3D v = wish * kWalk;
        if (space) v.setY(kFly);
        if (shift) v.setY(-kFly);
        moveAxis(0, v.x() * dt); // 碰撞，无重力
        moveAxis(2, v.z() * dt);
        moveAxis(1, v.y() * dt);
        if (m_onGround) { m_onGround = false; emit onGroundChanged(); }
        emit positionChanged();
        return;
    }

    // 走（生存 / 创造-未飞）：重力 + 跳跃 + 逐轴解算
    // t51：水平速度乘 speedMul（疾跑 ×1.3 / 蹲 ×0.4 / 走 ×1.0）；spec「疾跑移速 +、蹲下 -」。
    m_vel.setX(wish.x() * kWalk * speedMul());
    m_vel.setZ(wish.z() * kWalk * speedMul());
    m_vel.setY(std::max(float(m_vel.y() - kGravity * dt), -kMaxFall));
    if (spaceEdge && m_onGround) m_vel.setY(kJump);

    // 防穿墙：每子步任意轴移动 ≤0.4 格
    QVector3D delta = m_vel * dt;
    const float md = std::max({std::fabs(delta.x()), std::fabs(delta.y()), std::fabs(delta.z())});
    const int sub = std::max(1, int(std::ceil(md / 0.4f)));
    delta /= float(sub);

    // t51 蹲下「边缘安全」：蹲下且着地时，若整帧水平位移的目的位置脚下无支撑方块（hasGroundBelowAt），
    //   则跳过本帧水平移动（仅重力 / 跳跃仍生效）→ 蹲下不会从方块边缘走下（spec「防走下边缘」）。
    //   检查目的位置 = 当前 m_pos + delta*sub（一帧位移 << 1 格 → 等价「贴边即停」）。MC 行为对齐。
    //   仅蹲态 + 着地触发；走 / 疾跑 / 滞空不限制（走下边缘正常掉落）。
    const bool crouchEdgeBlock = (m_moveState == Crouch && m_onGround
                                  && (delta.x() != 0.0f || delta.z() != 0.0f)
                                  && !hasGroundBelowAt(m_pos.x() + delta.x() * float(sub),
                                                        m_pos.z() + delta.z() * float(sub)));

    const bool wasGround = m_onGround;
    m_onGround = false;
    for (int i = 0; i < sub; ++i) {
        const float dy = delta.y();
        moveAxis(1, dy);
        if (dy < 0 && m_vel.y() == 0) m_onGround = true; // 下落被挡 = 着地
        if (!crouchEdgeBlock) {
            moveAxis(0, delta.x());
            moveAxis(2, delta.z());
        }
    }
    // 稳健地面复探：脚底下方 0.05 有实体即算着地
    const float oy = m_pos.y();
    m_pos.setY(oy - 0.05f);
    if (aabbHitsSolid()) m_onGround = true;
    m_pos.setY(oy);
    if (m_onGround && m_vel.y() < 0) m_vel.setY(0);

    // 掉落伤害（t22，仅 Survival）：滞空期间记录最高点 m_peakY，着地瞬间按 MC 1.0 公式
    // floor(落差-3) 结算（fall>3 才伤，每整格 1 HP = 半心）。上 tick 已着地 → 重置基准；
    // 上升（跳跃）更新峰；本 tick 刚着地 → 结算并复位。
    if (wasGround) m_peakY = m_pos.y();
    else if (m_pos.y() > m_peakY) m_peakY = m_pos.y();
    if (!wasGround && m_onGround) {
        const float fall = m_peakY - m_pos.y();
        if (m_mode == Survival && fall > 3.0f) {
            const int dmg = int(std::floor(fall - 3.0f));
            if (dmg > 0) emit fallDamageTaken(dmg); // 呈现层 Connections 路由到 PlayerState.takeDamage
        }
        m_peakY = m_pos.y();
    }

    if (wasGround != m_onGround) emit onGroundChanged();
    emit positionChanged();
}
