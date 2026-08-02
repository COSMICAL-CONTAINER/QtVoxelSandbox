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
    snapSpawnToGround(); // t137：世界就绪 → 贴地表（覆盖 kSpawnY=44 兜底，消除出生落差摔伤）
    m_peakY = m_pos.y(); // 掉落伤害基准：以脚底初始 Y 起（首帧不误判大落差；snap 已设则等价）
    m_clock.start();
    m_evtClock.start();
    m_timer.start();
}

void PlayerController::setWorld(World *w)
{
    if (m_world == w) return;
    m_world = w;
    snapSpawnToGround(); // t137：世界注入后贴地表（构造期 m_pos=kSpawnY 兜底，此处覆盖为真实地表）
    emit worldChanged();
}

// t137 出生贴地表：查出生列 (kSpawnX,kSpawnZ) 的 worldgen 地表高度 → 脚底 Y = h+1（站地表方块上方），
//   同步 m_peakY 防误判落差。kSpawnY=44 是高于最高地表(~40)的兜底初值（防卡地形），但玩家从 44 摔到
//   地表（落差 >3）会触发摔伤；本方法在世界就绪后把玩家贴真实地表，消除出生落差。分别在
//   componentComplete / setWorld / respawn 调，确保世界（width/height/seed）定稿后玩家始终贴地表。
//   无世界 → no-op（m_pos 保持 kSpawnY 兜底）。分层（PLAN §2）：只读 World::heightAt（worldgen 地表纯
//   函数，同 generate() 填充用），不改栅格；Game 层向下读 World，无反向依赖。
void PlayerController::snapSpawnToGround()
{
    if (!m_world) return;
    const int h = m_world->heightAt(int(kSpawnX), int(kSpawnZ));
    m_pos.setY(float(h) + 1.0f); // 脚底 = 地表方块顶面（h 为地表方块 y，+1 站其上）
    m_peakY = m_pos.y();         // 掉落伤害基准重置（同 componentComplete / setMode 语义，防陈旧落差）
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

// t95：注入统一实体管理器（QML 绑定）。同 setItemEntities 模式：仅记录指针 + 发信号（QML 注入 peer
// ViewModel，运行期连接、非编译期反向依赖；PLAN §2 分层）。PlayerController 是唯一同时持 World* +
// EntityManager* 的对象，故由 tick 驱动实体的重力 / 推动物理（实体物理态住在 EntityManager 内部数据）。
void PlayerController::setEntityManager(EntityManager *m)
{
    if (m_entityManager == m) return;
    m_entityManager = m;
    emit entityManagerChanged();
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
            if (m_moveState == Walk && (now - m_lastWms < 250)) {
                setMoveState(Sprint);
                m_lastWms = -100000; // 防三连误触（双击成功后立即消费）
            } else if (m_moveState == Walk) {
                // 仅 Walk 态记「双击第一击候选」；Crouch/Sprint 态按 W 不进双击检测、不刷 m_lastWms。
                // 否则蹲态按 W 会留下时间戳，松 Shift 回 Walk 后单按 W（now - 旧戳 < 300）被误判双击 → 误触发疾跑。
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

// t159/t210 飞行速度滚轮调速：dir=+1 加速 / -1 减速（前滚 / 后滚），按有效速度步进 kFlyStep。
//   有效速度 = clamp(kFly * mul, kFlyMin, kFlyMax) ∈ [4,20]；改有效速度再回算 mul（直接钳有效速度更直观，
//   spectator 常驻飞读 mul 算飞移速）。无变化静默（不发信号，免抖动）。
//   t210 起仅 QML 在 spectator 模式调用（创造/生存滚轮切 hotbar）；本方法本身不判模式 —— 输入边界（§2-D）
//   由 QML 把关。
void PlayerController::adjustFlySpeed(int dir)
{
    const float eff = std::clamp(kFly * m_flySpeedMul + float(dir) * kFlyStep, kFlyMin, kFlyMax);
    const float newMul = eff / kFly;
    if (newMul == m_flySpeedMul) return;
    m_flySpeedMul = newMul;
    emit flySpeedMulChanged();
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
    m_lastWms = -100000; // t70：切模式清双击窗口脏残留（防切换后首按 W 被旧戳误判双击 → 误触发疾跑）
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

// t78 重生定位：传回出生点 + 清速度 / 挖掘态 / 飞行 / 蹲下疾跑（spec「立即重生」的物理态复位部分）。
//   血量 / 死亡态由 PlayerState::respawn() 复位（呈现层按钮同时调两者；分层：定位属 Physics、数值属 Game）。
//   m_peakY 重置到出生点 Y → respawn 后下落从出生点起算（同 componentComplete 首帧，不误判陈旧落差致死）。
//   emit positionChanged → 相机绑定（眼位 + 第三人称偏移）重算跟随；feetPosition 同 NOTIFY 一并刷新。
void PlayerController::respawn()
{
    cancelMining();           // 清生存累积挖掘态（裂纹叠层随之隐）
    m_leftDown = false;       // 清左键按下态（防 respawn 后 updateMining 误续挖）
    m_dead = false;           // t175：清死亡态镜像 → pickupScan 恢复（重生后玩家已离开死亡点，可正常拾取）
    // t202：重生回满气泡 + 清三计时器（出生点在水外 → 满气起算；PlayerState::respawn 同步 air 到 maxAir）。
    if (m_air != kMaxAir) { m_air = kMaxAir; emit airUpdated(m_air); }
    m_airTimer = 0.0f;
    m_drownTimer = 0.0f;
    m_airRegenTimer = 0.0f;
    m_pos = QVector3D(kSpawnX, kSpawnY, kSpawnZ); // 回出生列（X/Z；Y 由 snapSpawnToGround 贴地表）
    m_vel = QVector3D(0, 0, 0);
    snapSpawnToGround();      // t137：重生贴地表（消除 kSpawnY 兜底落差；设 m_pos.y + m_peakY）
    if (m_flying) { m_flying = false; emit flyingChanged(); }
    setMoveState(Walk);       // 蹲下 / 疾跑归 Walk（同时复位 AABB 高 / 眼位；无变化静默）
    emit positionChanged();   // 相机 / 第三人称模型跟随刷新
}

// t176 存档加载：恢复玩家位姿 + 模式。清物理瞬态（速度 / 挖掘 / 飞行 / 蹲疾跑）+ m_peakY 重置到存档 Y
//   （防「存档点到首次重力 tick」误判落差摔伤）。mode 序数 → Mode；越界守 0（Spectator，无伤兜底）。
void PlayerController::loadSavedState(float x, float y, float z, float yaw, float pitch, int mode)
{
    cancelMining();
    m_leftDown = false;
    m_dead = false;
    m_pos = QVector3D(x, y, z);
    m_vel = QVector3D(0, 0, 0);
    m_yaw = yaw;
    m_pitch = pitch;
    m_peakY = y; // 重置掉落基准（存档点起算，不误判陈旧落差）
    // t202：存档不持久化 air（瞬态值，机制同速度 / 挖掘态；出水即回满）→ 加载回满气泡 + 清三计时器。
    //   emit airUpdated 强制同步 PlayerState.air（防上一世界溺水残留低气值带进新世界显陈旧气泡条）。
    m_air = kMaxAir;
    emit airUpdated(m_air);
    m_airTimer = 0.0f;
    m_drownTimer = 0.0f;
    m_airRegenTimer = 0.0f;
    if (m_flying) { m_flying = false; emit flyingChanged(); }
    if (m_moveState != Walk) setMoveState(Walk);
    const Mode target = (mode == int(Survival)) ? Survival
                       : (mode == int(Creative)) ? Creative : Spectator;
    if (target != m_mode) { m_mode = target; emit modeChanged(); }
    emit positionChanged();
    emit yawChanged();
    emit pitchChanged();
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
    // t70：同时清双击窗口脏残留（防暂停恢复后首按 W 被旧戳误判双击 → 误触发疾跑）。
    if (m_moveState != Walk) setMoveState(Walk);
    m_lastWms = -100000;
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
// t178：tick() 包一层 CPU 耗时计时（PLAN §4 帧时间切分）。每 tick 累加主线程耗时，每 ~60 tick（≈1s@60Hz）
//   算平均写 m_simMs 并 emit perfChanged → F3 叠层重绑显示「cpu sim: X.XXms」。实体 / 物理逻辑在 tickImpl。
void PlayerController::tick()
{
    QElapsedTimer pt;
    pt.start();
    tickImpl();
    m_simAccumNs += pt.nsecsElapsed();
    if (++m_simTickCount >= 60) { // ≈1s 窗口（timer 16ms ≈ 62.5Hz → 60 tick ≈ 0.96s）
        m_simMs = float(double(m_simAccumNs) / 1e6 / double(m_simTickCount)); // ns → ms 均
        m_simAccumNs = 0;
        m_simTickCount = 0;
        emit perfChanged();
    }
}

void PlayerController::tickImpl()
{
    const qreal dt = qMin(m_clock.restart() / 1000.0, 0.05); // 钳 50ms，防卡顿后穿墙
    // t201 水下蓝滤镜：每 tick 重算眼位水态，翻转才 emit（避免每帧抖 QML 绑定）。放在 !m_captured
    //   早 return 之前 → 暂停 / 背包开 / 失焦时仍刷新（玩家可能停在水里打开背包，蓝雾应持续显）。
    //   仅读 World::blockAt（向下依赖，不改栅格）；无世界时 eyeInWater() 返 false。
    const bool inWater = eyeInWater();
    if (inWater != m_eyeInWater) {
        m_eyeInWater = inWater;
        emit eyeInWaterChanged();
    }
    // t60：掉落物重力（世界模拟，独立于玩家捕获态——菜单 / 暂停时实体仍落到地面）。
    // PlayerController 是唯一同时持 World* + ItemEntityManager* 的对象，故由此驱动；实体物理态
    // （vy / resting）与 pos 同住在 ItemEntityManager 内部数据里（分层：Entities→World 向下只读）。
    if (m_itemEntities && m_world) m_itemEntities->tick(dt, m_world);
    // t95：统一实体（测试生物）重力 + 地面静止，同掉落物常开（菜单 / 暂停时仍模拟）。机制同源
    // （EntityManager::tick 向下只读 World::isSolid）。PlayerController 现亦持 EntityManager* → 由它驱动。
    if (m_entityManager && m_world) m_entityManager->tick(dt, m_world);
    // t92：拾取扫描提到 m_captured 早 return **之前**——打开背包（release→m_captured=false）时
    // 原 pickupScan 落在早 return 之后永不执行，玩家走近掉落物拾不起（仅见实体掉地）。掉落物物理
    // （itemEntities->tick）本就在早 return 前跑（独立于捕获态），拾取与之同级、同样常开才一致。
    // 安全：pickupScan 内自检 m_itemEntities/m_hotbar 非空，t53 新生免拾取窗已在内部（0.5s 后才可拾）。
    pickupScan();
    if (!m_captured) {
        cancelMining(); // 暂停（含背包开 / 失焦）：清累积挖掘态（spec：失焦清零）
        // t45：暂停时清行走动画驱动（moveSpeed→0；walkPhase 不动，QML 据此 sin*0=0 → 四肢归中性位）。
        // t159：同步清 speed（实际水平速度，暂停即 0；F3 报 0 而非陈旧值）。仅值真变时发，免每 tick 抖动。
        if (m_moveSpeed != 0.0f || m_horizSpeed != 0.0f) {
            m_moveSpeed = 0.0f; m_horizSpeed = 0.0f; emit moveSpeedChanged();
        }
        return;
    }
    pollMouse();
    step(dt);
    // t95：玩家推动可推动实体（仅 captured/playing；玩家主动移动后才有位移可传给实体）。在 step() 解析
    // 完玩家与世界碰撞后调 —— 用已贴墙的玩家 AABB 做圆-vs-AABB 推解，把穿透量传给实体（swept 碰撞解析
    // 玩家位移传给实体，spec）。m_height 用当前 AABB 高（蹲下变矮 → 推动区间随之收，与碰撞同源）。
    if (m_entityManager) m_entityManager->resolvePlayerPush(m_pos, kHalfW, m_height, m_world);
    updateRaycast();   // 沿视线 DDA 选体 → 更新线框命中态
    updateCameraDistance(); // t40：第三人称相机距离钳制（防穿墙）
    updateMining(float(dt)); // t34：累积生存挖掘进度（创造不进入此态；无操作时早 return）
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
    // t184：选体射线纳入 Torch（HitTorch）—— 准星瞄火把即命中火把（可显示火把边界框 + 左键直挖），
    //   修正 t157「射线永远穿透火把」致火把不可直挖之缺陷（用户原意「火把可选可挖、空气可穿」）。
    //   Water 仍穿过（保 t165 水下可选中 / 挖实体）；相机距离（updateCameraDistance）走 Default（火把 /
    //   水均穿过，non-solid 不拉近视距），故本处显式传 HitTorch 仅作用于选体。
    const RayHit h = raycastVoxel(*m_world, position(), lookDirection(), kReach, RayFilter::HitTorch);

    // t212 命中点 Y（供 slab 上/下半放置 + 互补半合并判定，placeBlock 读）。lookDirection 已归一、dist 为起点
    //   到命中面欧氏距离（见 raycast.h）→ 命中点 = 眼位 + 视线*dist。**每帧刷新**（不随下方 changed 早退）：
    //   准星在同格内移动时格坐标/法线不变 → changed=false 跳过 emit，但命中点 Y 仍在变，placeBlock 点击瞬间
    //   需读最新值定半位，故须在早退之前写入。无命中 → 0（placeBlock 入口 m_hasHit 守卫已拦，不读此值）。
    m_hitPointY = h.valid ? (position().y() + lookDirection().y() * h.dist) : 0.0f;

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
    m_hitPointY = 0.0f; // t212：与命中态同步清零
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
// 共用门控：未捕获 / 无世界 → 不动作。无命中 → 挥空手（t68，见下）。无窗口 → 不动作。
// 兼容：breakBlock()（旧 Q_INVOKABLE）等价调本方法（创造瞬破 / 生存开始累积）。
void PlayerController::beginMining()
{
    // t44 连续挖掘：记录物理左键按下态。置于所有早 return 之前 —— 即便当前不能开始累积
    // （观察者 / 无命中 / 暂停），按钮按下这一事实仍成立，后续 updateMining 据此 + 新命中自动续挖。
    m_leftDown = true;
    if (!canBreak()) return; // 观察者不能破块（t21）
    if (!m_world || !m_captured) return;
    // t68 挥空手：左键对空气（无命中方块）时仍 emit swingArm（挥臂动画反馈），但**不进入**
    // mining 状态（不破块、不扣耐久、不显裂纹）。这是「动作反馈应独立于是否命中」的通用原则 ——
    // 挥臂是玩家主观动作的呈现，命不命中只决定后续语义（破块 / 未来攻击判定），不应阻塞挥臂反馈。
    // 此分支为后续攻击系统（打怪）铺垫：攻击判定走同一 raycast，但挥臂本身无目标也要触发。
    // 第一 / 二 / 第三人称均消费 swingArm 驱动手臂挥动（见 Main.qml）。
    if (!m_hasHit) {
        emit swingArm();
        return;
    }

    if (m_mode == Creative) {
        // 创造：瞬破（progress 直接 1.0 等价），不掉落。仍发 swingArm（finishMiningAt 末尾发，动作真发生）。
        // 不进入累积态（mining 留 false）→ 不显裂纹叠层（瞬破无需裂纹）。
        // finishMiningAt 内自行取原方块 id（setBlock 前）发 playerMined；此处无需重复读取。
        // t141：删 t119 的创造 canMine 守卫 —— 创造模式下基岩**可秒破**（spec「基岩创造可破」）。原守卫
        //   拦截基岩（hardness<0 → canMine=false）仅给挥臂不破；现移除，基岩与普通方块同走瞬破（仍发
        //   swingArm）。生存基岩仍不可破：updateMining 内 if(canMine(bid)&&progress>=1.0) 守 finishMiningAt。
        finishMiningAt(m_hitBx, m_hitBy, m_hitBz, /*drop=*/false);
        return;
    }

    // Survival：开始累积。重置目标 / 进度 / stage；stage 从 0 起（裂纹首阶立显，反馈即时）。
    m_mining = true;
    m_mineBx = m_hitBx; m_mineBy = m_hitBy; m_mineBz = m_hitBz;
    m_miningProgress = 0.0f;
    m_miningStage = 0;
    m_mineBeat = -1; // t165：挥臂节拍归位（新目标首 beat 0 立挥）
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
    m_mineBeat = -1; // t165：挥臂节拍归位
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
    // t134 门两格破坏联动：破任一格 → 同步清配对格（另一格），防留半截悬空门。配对格据本格 state bit3
    //   （isUpper）判上 / 下：本格上格 → 配对 y-1；本格下格 → 配对 y+1。仅当配对格同为 WoodDoor 才清
    //   （防御：state 不一致时不误清异格）。drop 仅对本格发（配对格静默清，避免双掉落）。
    //   ⚠️ brokenState 必须在 setBlock(Air) **之前**读：4 参数 setBlock 委托 5 参数版以 state=0 写入
    //   （chunk.cpp「id 变更时重置 state=0」契约），之后 stateAt 永返 0 → 配对方向恒算成 y+1 →
    //   破上格（bit3=1 本应 y-1 找下格）时下格不清，留半截悬空门。useBlock 路径在 setBlock 前读 st 故无此坑。
    const quint8 brokenState = (brokenId == BlockRegistry::WoodDoor
                                || brokenId == BlockRegistry::Planks)
        ? m_world->stateAt(x, y, z) : quint8(0);
    m_world->setBlock(x, y, z, BlockRegistry::Air); // → World 发 blockBroken（粒子触发）+ worldChanged（mesh 重建）
    if (brokenId == BlockRegistry::WoodDoor) {
        const int py = ((brokenState & 8) != 0) ? y - 1 : y + 1;
        if (m_world->blockAt(x, py, z) == BlockRegistry::WoodDoor)
            m_world->setBlock(x, py, z, BlockRegistry::Air);
    }
    emit playerMined(x, y, z, int(brokenId), drop); // 破块语义事件（含 drop 标志；当前无消费端，留扩展）
    // t150c/d 火把支撑联动：破块可能挖掉邻接火把的唯一支撑（如破墙→墙上火把悬空）。扫 6 邻火把，
    //   无支撑者（torchHasSupport 返 false）掉落为物品（setBlock(Air) + spawnItem）。
    //   机制等价 MC「火把附着面被移除则火把脱落」。torchHasSupport 查 5 向 solid 邻居（下 / 四侧），
    //   与 placeBlock 预检 + computeTorchOrient 同语义 → 火把有任一 solid 邻居即保留（不会因次要支撑被
    //   破而误掉）。掉落的火把走 setBlock(Air) → World 发 blockBroken + worldChanged → Main.qml 移除伪
    //   光源（removeTorchAt / onWorldChanged 兜底）+ mesh 重建，消除「破支撑后火把悬空残像」（t150c）。
    //   火把非 solid → 不支撑他火把 → 单趟 6 邻扫即足够（无级联，破一块不会链式掉一串）。
    if (m_world) {
        constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto &d : kNb) {
            const int tx = x + d[0], ty = y + d[1], tz = z + d[2];
            if (m_world->blockAt(tx, ty, tz) == BlockRegistry::Torch && !torchHasSupport(tx, ty, tz)) {
                m_world->setBlock(tx, ty, tz, BlockRegistry::Air);
                emit spawnItem(tx, ty, tz, BlockRegistry::dropId(BlockRegistry::Torch),
                               std::max(1, BlockRegistry::dropCount(BlockRegistry::Torch)));
            }
        }
    }
    // t43：生存挖出可掉落方块 → **走实体流**（emit spawnItem），移除 commit a3e9300 的 auto-collect
    // （原直接 addStack）。掉落物落到该格地面、玩家走近 ≤kPickupDist 时经 pickupScan 拾取 → addStack
    // （先选中槽、再空槽，智能堆叠至 maxStack）。满栈不进背包则实体留地面（spec：全满→不拾取）。
    // drop 由 caller 算（生存走 ToolRegistry::canHarvest；创造瞬破 drop=false 不发）。
    // t64：spawnItem 带 count（= BlockRegistry::dropCount；当前表内全 1，留扩展位对齐方块表）。
    if (drop) {
        int dropId = BlockRegistry::dropId(brokenId);
        int dropCount = std::max(1, BlockRegistry::dropCount(brokenId));
        // t206 双半砖（合并态）破块掉 2× WoodSlab（非 1× Planks）：placeBlock 合并时写 Planks +
        //   PlanksFromDoubleSlabBit 标记「源自双半砖」。此处检本 bit → 改掉 2 块半砖（机制等价 MC
        //   「double slab 破坏掉 2 块半砖」）。spawnItem 传 count=2 → 1 实体携 2 件（拾取 addStack 入 2 块）。
        //   常规 Planks（state=0）不进此分支 → 掉 1× Planks 不变。brokenState 已在 setBlock(Air) 前读（t134 时序）。
        if (brokenId == BlockRegistry::Planks && (brokenState & BlockRegistry::PlanksFromDoubleSlabBit)) {
            dropId = BlockRegistry::WoodSlab;
            dropCount = 2;
        }
        emit spawnItem(x, y, z, dropId, dropCount); // t83：传 dropId（Stone→Cobble / 矿石→材料 / 双砖→2×slab），非 brokenId
    }
    emit swingArm();                                // 破块成功 → 第一人称手挥动（t29）
    cancelMining();                                 // 清累积态（裂纹叠层隐藏）
}

// t150d 火把支撑判定：5 向（下 / ±X / ±Z）任一为 solid 方块即有支撑。与 placeBlock 火把放置预检 +
//   Main.qml computeTorchOrient/torchNeighborSolid 同语义（同走 BlockRegistry::isSolid 单一权威）。
//   火把 / 空气等 solid=false 方块不算支撑（防两火把互挂悬空）。无世界 → 视为无支撑（保守掉落）。
bool PlayerController::torchHasSupport(int x, int y, int z) const
{
    if (!m_world) return false;
    return BlockRegistry::isSolid(m_world->blockAt(x, y - 1, z))
        || BlockRegistry::isSolid(m_world->blockAt(x - 1, y, z))
        || BlockRegistry::isSolid(m_world->blockAt(x + 1, y, z))
        || BlockRegistry::isSolid(m_world->blockAt(x, y, z - 1))
        || BlockRegistry::isSolid(m_world->blockAt(x, y, z + 1));
}

// 每 tick 推进生存挖掘进度（t34）+ 连续续挖（t44）。创造不进入此态（beginMining 内瞬破已 return）。
// spec：progress += dt * speed(block, tool)；speed = 1 / miningTime（ToolRegistry 已含 hardness/speedMul）。
//   - 失命中 / 目标已被破（变 air）→ cancelMining。
//   - 目标换（玩家转头）→ 重置 progress（spec：换目标清零），目标格更新。
//   - stage 推进（progress 跨 1/6 阈值）→ 发 miningStateChanged（驱动 QML 切裂纹贴图）+ swingArm（挥动循环）。
//   - progress >= 1.0（且 canMine）→ finishMiningAt（drop = canHarvest）；基岩 canMine=false 永不破（t141）。
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

    // 目标变 air（已被其它途径破 / 越界）→ 取消累积。t141：不再对「不可挖（基岩）」取消 —— 基岩保留在
    //   累积挖掘态推 stage（挥臂 + miningParticle 音）给反馈，仅由下方完成守卫 if(canMine(bid)&&progress
    //   >=1.0) 阻止 finishMiningAt（不破）。原 canMine 取消会把基岩立即清出累积态 → 无任何挖掘反馈。
    //   air / 越界 = 已破 / 无目标，取消合理（blockAt 越界返 Air）。
    const quint8 bid = m_world->blockAt(m_mineBx, m_mineBy, m_mineBz);
    if (int(bid) == int(BlockRegistry::Air)) { cancelMining(); return; }

    // t57：手持物品 id **直接读 hotbar（单一权威）**，不读 m_selectedItem 副本。m_selectedItem 经
    //   Q_PROPERTY 绑定 hotbarVM.selectedItemId（NOTIFY=selectedSlotChanged）刷新，但 QML 绑定重算
    //   可能被引擎延迟到下一事件循环 → 同一 tick 内 m_selectedItem 可能滞后于真实选中槽（拾取 / 切槽
    //   边缘与 mining tick 同帧时）。canHarvest / miningTime 是掉落与速度的判定输入，必须用**当下**
    //   选中槽的真实 id：空手挖需工具方块（石 / 圆石）→ canHarvest=false → 不掉落（spec「仅 AIR」）；
    //   持匹配工具 → 掉落。直读 hotbar 消除绑定滞后窗口，保证空手挖石头永不误掉。
    //   m_hotbar 在 QML 绑定注入（同 world / itemEntities）；无 hotbar（极端）→ 0=空手兜底。
    const int heldItemId = m_hotbar ? m_hotbar->selectedItemId() : 0;

    // 速度：miningTime = hardness / speedMul（ToolRegistry），progress 增量 = dt / miningTime。
    // t141：基岩（hardness<0，canMine=false）现也走到此累积路径给反馈 —— miningTime 对 hardness<=0 走
    //   0.05s 地板（不除 hardness），故恒 >0，无除零；基岩 progress 快速达 1.0 但被完成守卫拦下不破。
    const float miningTime = ToolRegistry::miningTime(bid, heldItemId);
    m_miningProgress += dt / miningTime;

    // t165：不可挖方块（基岩 canMine=false）持续累积给「挥臂」反馈但**不显裂纹**（spec「一直不出现裂纹」）。
    //   可挖方块 progress 到 1.0 → 下方完成守卫 finishMiningAt 破块；不可挖方块 progress 到 1.0 **回绕**
    //   （-=1.0）→ beat 循环 0..5 → 持续跨阶 swingArm（手一直挥），防无界增长溢出。displayed stage：
    //   可挖 = beat（裂纹 0..5），不可挖 = -1（裂纹叠层恒隐）。miningParticle 仅可挖方块迸发（基岩不破无碎屑）。
    const bool mineable = ToolRegistry::canMine(bid);
    if (!mineable && m_miningProgress >= 1.0f) m_miningProgress -= 1.0f; // 基岩 progress 循环（持续挥臂）
    const int beat = std::clamp(int(std::min(m_miningProgress, 1.0f) * 6.0f), 0, 5);
    const int newStage = mineable ? beat : -1; // 不可挖 → 不显裂纹（spec「一直不出现裂纹」）
    if (beat != m_mineBeat) {
        m_mineBeat = beat;
        emit swingArm(); // 跨节拍挥臂（可挖/不可挖均挥；基岩循环 beat → 持续挥动反馈）
        // t165：挖掘击打音对所有被挖方块发（含基岩）—— spec「保持 mining 态挥臂+音」。基岩虽不破，
        //   hold-mine 仍随节拍有挖掘音反馈（机制等价 MC 镐撞基岩响一声）；可挖方块同样发（音统一走本信号）。
        emit miningSound(int(bid));
        // t61：每跨一阶迸发少量碎屑（被挖方块色），驱动「挖的过程中」进度反馈粒子。仅可挖方块（基岩
        //   不破无碎屑，故碎屑仍由 mineable 守卫）。bid 上面已读（当前 tick 目标方块 id，setBlock 前原值），
        //   传呈现层复用破块 emitter。音已由上方 miningSound 统一发出（含基岩），此处不再耦合音。
        if (mineable) emit miningParticle(m_mineBx, m_mineBy, m_mineBz, int(bid));
    }
    if (newStage != m_miningStage) {
        m_miningStage = newStage;
        emit miningStateChanged(); // 切裂纹贴图（可挖）/ 隐藏裂纹（不可挖 stage=-1）
    }
    emit miningProgressChanged();

    // 完成：progress 满 → 破块。drop 走 ToolRegistry::canHarvest（生存可采掘判定）。
    //   空手（heldItemId=0 / 非工具）挖需工具方块（石 / 圆石）→ canHarvest=false → 仅 AIR 不掉落；
    //   泥土 / 草 / 木 / 叶 / 沙（NoTool）→ canHarvest=true → 掉落（spec）。
    // finishMiningAt 内 cancelMining 清 m_mining=false；m_leftDown 不动 → 下一 tick 顶部续挖分支接手。
    // t141：完成守卫加 canMine —— 不可挖方块（基岩 hardness<0）即便 progress 满（miningTime 走 0.05s
    //   地板使 progress 快速达 1.0）也**不**调 finishMiningAt（不破）。spec「if(canMine(bid)&&progress
    //   >=1.0) 守 finishMiningAt 不破」。基岩留累积态持续推 stage 反馈（挥臂 + 挖掘音），但永不破。
    if (ToolRegistry::canMine(bid) && m_miningProgress >= 1.0f) {
        const bool drop = ToolRegistry::canHarvest(bid, heldItemId);
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
// t87：命中格若为熔炉（Furnace）→ 右键打开 FurnaceUI（不放置；发 furnaceOpened），同工作台模式。
void PlayerController::placeBlock()
{
    // t128：放置 CD（200ms = 5 次/秒）防连点放沙等触发多次塌落链溢出。spec「入口 if(now - m_lastPlaceMs
    //   < 200) return」；仅成功放置后刷新 m_lastPlaceMs（下方 setBlock 后）。now 走 m_evtClock（事件
    //   时间戳，与双击检测 m_lastSpaceMs/m_lastWms 同源）。初值 -100000 = 远古 → 首次放置不限。
    const qint64 now = m_evtClock.elapsed();
    if (now - m_lastPlaceMs < 200) return;
    if (!canPlace()) return; // 观察者不能放块
    if (!m_world || !m_captured) return;
    // t174：空桶舀水用独立含水射线（见下方桶分支），水下 / 瞄深水时主射线无实体命中（m_hasHit=false）
    //   亦须可舀，故 !m_hasHit 不在此一刀切拦截。下方工作台/熔炉/门/活版门与放块路径仍需命中 → 局部门控。
    if (m_hasHit) {
    // t50：右键工作台 → 打开 3×3 合成 UI（优先于放置；spec「右键工作台开 3×3」）。
    if (m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::CraftingTable) {
        emit craftingTableOpened();
        return;
    }
    // t87：右键熔炉 → 打开 FurnaceUI 冶炼界面（同工作台模式：优先于放置，无论手持何物右键熔炉即开）。
    if (m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Furnace) {
        emit furnaceOpened();
        return;
    }
    // t173/t179：右键箱子 → 打开 ChestUI 物品栏（同工作台 / 熔炉模式：优先于放置，无论手持何物右键箱子
    //   即开）。发 chestOpened(x,y,z) 携命中格世界坐标 → 呈现层 Connections 打开 ChestUI（释放指针 +
    //   盖子开合动画）；ChestStore 据坐标寻址该箱子的 27 槽。机制等价 MC 右键箱子开物品栏。
    if (m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Chest) {
        emit chestOpened(m_hitBx, m_hitBy, m_hitBz);
        return;
    }
    // t134 右键门 / 活板门 → 翻 state 开合（useBlock 语义；spec「door/trapdoor 右键 useBlock 翻 state 开合」）。
    //   优先于放置（同工作台 / 熔炉模式：右键已放置的门 / 活板门即开合，不另放块）。空手亦可（开合是
    //   「使用」语义，与手持何物无关）。id 不变只 state 变 → World::setBlock 5 参数版走重网格化路径
    //   （发 worldChanged 不发 broken/placed）。门两格同翻（找配对格：据本格 state bit3 判上 / 下）。
    {
        const quint8 hitId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
        if (hitId == BlockRegistry::WoodDoor) {
            const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
            const quint8 flipped = quint8((st & ~4) | (((st & 4) == 0) ? 4 : 0)); // 翻 bit2（开合）
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, hitId, flipped);
            // 配对格（上 / 下）同步翻：本格 isUpper(bit3)=1 → 配对在 y-1；否则 y+1。
            const int py = ((st & 8) != 0) ? m_hitBy - 1 : m_hitBy + 1;
            if (m_world->blockAt(m_hitBx, py, m_hitBz) == BlockRegistry::WoodDoor) {
                const quint8 pst = m_world->stateAt(m_hitBx, py, m_hitBz);
                const quint8 pflipped = quint8((pst & ~4) | (((pst & 4) == 0) ? 4 : 0));
                m_world->setBlock(m_hitBx, py, m_hitBz, BlockRegistry::WoodDoor, pflipped);
            }
            m_lastPlaceMs = now;
            // t152：开合音（门两格同翻只发一次 = 一次动作一次音）。flipped.bit2 = 新的开合态。
            emit doorToggled((flipped & 4) != 0);
            emit swingArm();
            return;
        }
        if (hitId == BlockRegistry::WoodTrapdoor) {
            const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
            const bool willOpen = (st & 1) == 0;
            quint8 ns = quint8(st ^ 1); // 翻 bit0（开合）
            if (willOpen) {
                // 开时记录朝向 = 玩家当前水平朝向（bit[2:1]），合时不清朝向（保留下次开向）。
                ns = quint8((ns & ~6) | quint8((horizontalFacing() & 3) << 1));
            }
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, hitId, ns);
            m_lastPlaceMs = now;
            // t152：开合音（ns.bit0 = 新的开合态；willOpen 已是本次结果，等价 (ns & 1)）。
            emit doorToggled(willOpen);
            emit swingArm();
            return;
        }
    }
    } // t174：m_hasHit 局部门控结束（工作台/熔炉/门/活版门需命中；桶分支与放块路径各自处理命中需求）
    // t174 铁桶 useBlock（spec「右键舀水/倒水交互」）：选空桶 / 装水桶时右键走桶交互，不走方块放置路径
    //   （桶非方块；selectedBlock 经 hotbar 已归 Air，下方 Air 守卫会拦，故在此提前分支）。机制等价 MC 1.0
    //   铁桶：装水桶右键 → 在命中面相邻空气格放置水源（state=0）；空桶右键 → 含水射线命中首个水格舀走。
    //   分层（PLAN §2）：桶交互属 Game/Physics（读射线 + 写 World + 写 Hotbar VM），不改栅格语义（setBlock 入口）。
    // t186 修复(a)「舀水不变桶」：手持物品 id **直读 hotbar**（单一权威，同 updateMining 的 t57 修法），不读
    //   m_selectedItem 副本——后者经 Q_PROPERTY 绑定 hotbarVM.selectedItemId（NOTIFY=selectedSlotChanged）刷新，
    //   QML 绑定重算可能被引擎延迟到下一事件循环 → 同一 tick 内 m_selectedItem 滞后于真实选中槽（拾取 / 切槽
    //   边缘与右键同帧时）→ 桶分支不触发 → 舀水无效。直读 hotbar 消除绑定滞后窗口。
    //   同时：舀水（空桶→装水桶）**所有模式都换桶**（移除旧 `m_mode != Creative` 守卫）——机制等价 MC 1.0
    //   铁桶在创造 / 生存均反映其内容（舀水即满）；旧守卫使创造模式舀水后桶仍空（spec「当前舀水不变桶」）。
    //   倒水方向保留创造不消耗（装水桶右键放水源，创造保持装水桶 = 无限放水；生存→空桶），不属本任务范围。
    const int heldItemId = m_hotbar ? m_hotbar->selectedItemId() : 0;
    if (m_hotbar && (heldItemId == RecipeRegistry::WaterBucketId
                     || heldItemId == RecipeRegistry::BucketEmptyId)) {
        const int slot = m_hotbar->selectedSlot();
        if (heldItemId == RecipeRegistry::WaterBucketId) {
            // 倒水：命中面相邻格放水源。tx/ty/tz 同方块放置（命中面外法线相邻格）。目标须为空气（不覆盖实体）。
            if (!m_hasHit) return; // 未命中 → 无相邻格可放（桶分支已绕过上方 !m_hasHit 门，此处补检）
            const int tx = m_hitBx + m_hitNx, ty = m_hitBy + m_hitNy, tz = m_hitBz + m_hitNz;
            if (m_world->blockAt(tx, ty, tz) == BlockRegistry::Air) {
                m_world->setBlock(tx, ty, tz, BlockRegistry::Water, 0); // state=0 水源（tickWaterFlow 下次波前推进开始 1 格/tick 蔓延）
                if (m_mode != Creative)
                    m_hotbar->setStack(slot, int(RecipeRegistry::BucketEmptyId), 1); // 装水桶 → 空桶（创造不消耗：保持装水桶可无限放水）
                m_lastPlaceMs = now;
                emit swingArm();
            }
            return; // 倒水（无论成功与否）不再走放置路径
        }
        // 空桶舀水（t174 fix）：主射线排除 Water（t165 水下挖掘语义）→ 命中格恒为水后/水下的实体方块，
        //   旧查命中格 == Water 恒 false（死代码）。改为单独跑「含水」射线（RayFilter::HitWater）命中首个水格：
        //     - 水面 / 岸边瞄准水体 → 射线穿空气后命中水格；
        //     - 瞄深水（射程内无实体，仅水）→ 主射线无命中，但含水射线命中水；
        //     - 水下（眼位在水）→ 含水射线起点即水格，视为命中该格（桶舀身处水）。
        //   不依赖上方 !m_hasHit 门（已绕过）→ 三种姿态均可舀。舀走走 setWaterSilent（水流系统静默写入，
        //   不发 blockBroken → 无破块粒子/音，机制等价 MC 舀水无反馈），下一 tickWaterFlow 波前自动逐环衰退邻接流水。
        const RayHit wHit = raycastVoxel(*m_world, position(), lookDirection(), kReach, RayFilter::HitWater);
        // t199 空桶只舀水源：机制等价 MC 1.0 铁桶——仅 state==0（水源）可舀，流水（state 1..7）右键无效。
        //   state 取自 wHit 水格（同格 id+state 经 ChunkManager 路由），水源被舀后邻接流水由下一 tickWaterFlow
        //   波前逐环衰退（机制等价 MC「桶舀源、流自然蒸发」）。流水右键：守卫不满足 → 不写栅格 / 不换桶 / 不挥手。
        if (wHit.valid
            && m_world->blockAt(wHit.bx, wHit.by, wHit.bz) == BlockRegistry::Water
            && m_world->stateAt(wHit.bx, wHit.by, wHit.bz) == 0) { // 仅水源 state==0 可舀（t199）
            m_world->setWaterSilent(wHit.bx, wHit.by, wHit.bz, BlockRegistry::Air, 0); // 舀走（清整格）
            m_hotbar->setStack(slot, int(RecipeRegistry::WaterBucketId), 1); // t186：空桶 → 装水桶（所有模式均换桶；旧 m_mode!=Creative 守卫移除）
            m_lastPlaceMs = now;
            emit swingArm();
        }
        return; // 空桶（舀水成功与否）不再走放置路径
    }
    if (!m_hasHit) return; // t174：放块路径需命中（桶分支已 return；至此为非桶手持方块）
    if (m_selectedBlock == BlockRegistry::Air) return; // 空栈 → 右键不放置（也不挥手，t32）
    const int tx = m_hitBx + m_hitNx, ty = m_hitBy + m_hitNy, tz = m_hitBz + m_hitNz;
    const quint8 idByte = quint8(m_selectedBlock);
    // t146 放置态先算（供「重叠校验」+「实际写入」+「t163(b) 合并判定」复用，逻辑同源）：slab 据命中面 /
    //   玩家俯仰、stairs/door 据玩家水平朝向、fence/pressure_plate/trapdoor 默认 0（trapdoor 默认水平合）。
    //   door 占两格 → 另算上格。
    quint8 placeState = 0;
    if (m_selectedBlock == BlockRegistry::WoodSlab) {
        // t212 命中面检测（spec「瞄已放方块上50%→上半砖；下50%→下半砖」，备注「命中点 y 与 hitCell y 差值判
        //   上下半」）：
        //   命中顶面（ny=+1，方块上方）→ 下半(state=0)：满格之上放下半砖，顶面+0.5 步可走上去（机制等价 MC
        //     顶面放置造台阶）；命中底面（ny=-1，天花板下方）→ 上半(state=1)：倒挂半砖。二者命中点恒在格边界
        //     （fracY=1/0），无「上下半」之分，沿用步进 / 倒挂约定不变。
        //   命中侧面（ny=0，靠墙 / 靠方块侧）→ 据命中点 Y 在该格内的小数位置：上50%(fracY>=0.5)→上半(state=1)，
        //     下50%→下半(state=0)。旧实现用玩家俯仰(m_pitch)判侧面半位，但默认略俯视(pitch=-42)→侧面恒下半，
        //     与「瞄方块上半应放上半砖」直觉相悖（spec 报「现恒下半砖」）；改读命中点 Y 后瞄哪半放哪半。
        if (m_hitNy > 0) placeState = 0;
        else if (m_hitNy < 0) placeState = 1;
        else {
            const float fracY = m_hitPointY - float(m_hitBy); // 命中点在命中格内的 Y 小数分量 [0,1]
            placeState = (fracY >= 0.5f) ? 1 : 0;
        }
    } else if (m_selectedBlock == BlockRegistry::WoodStairs) {
        // t147：state[1:0]=水平朝向；bit2=上下倒置。
        //   t163 朝向修正：朝向 = horizontalFacing **异或 1**（取玩家反向）→ 楼梯「开口」朝玩家侧
        //     （partialblockgeometry「朝 X 开 = 背墙在对侧」：玩家面 +X(0) → 取 -X(1) → 背墙 +X 侧、开口 -X 朝玩家），
        //     玩家前行即可走上台阶（旧取正向 = 开口朝远离玩家 = 背对玩家，得绕行）。
        //   命中底面（天花板下方，m_hitNy<0）→ 倒置（整步在上、背墙在下）；否则正置。镜像 slab 的
        //   「ny<0 → 上半」约定，使「点方块下方」在所有半方块（slab/stairs）统一得到「倒挂」变体。
        placeState = quint8(((horizontalFacing() & 3) ^ 1) | (m_hitNy < 0 ? 4 : 0));
    }
    // t163(b) 同格双半砖合整（spec「同格下半砖上再放下半砖→合并为完整方块阻挡行走」）：
    //   右键 slab 时若点中的就是 slab，且点击面朝向其空半（lower 顶面 ny>0 / upper 底面 ny<0）→ 在同格
    //   补出互补半，合成 Planks 完整方块（满格碰撞 → 阻挡行走；机制等价 MC「double slab = full block」）。
    //   合成前查重叠（满格 Planks 比半砖大 → 重查 overlapsPlayerAABB；玩家在格内则拒合，防自埋）。
    //   侧面点击（ny=0）不合（自然语义是放邻格）；同半 slab（如 lower 上再放 lower）走常规 target 放置。
    //   t206：合并写 Planks + PlanksFromDoubleSlabBit 标记「源自双半砖」→ finishMiningAt 据本 bit 掉 2× WoodSlab
    //   （非 1× Planks；机制等价 MC「double slab 破坏掉 2 块半砖」）。详见 BlockRegistry::PlanksFromDoubleSlabBit 注释。
    if (m_selectedBlock == BlockRegistry::WoodSlab
        && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::WoodSlab) {
        const quint8 hitState = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
        const bool hitUpper = (hitState & 1) != 0;
        // t212 同格互补半合并（spec「同格上半+下半→合并整砖」+ 修「放了上半砖后同格下半砖放不下」/
        //   「瞄上方却放旁边」）：据命中点 Y 判是否点中**空半**——下半砖(filled [0,0.5]) 点其上空半(fracY>=0.5)、
        //   上半砖(filled [0.5,1]) 点其下空半(fracY<0.5) → 合并为 Planks 满格。旧实现仅认顶/底面(ny≠0)合并，
        //   侧面点击不合并 → 右键已有半砖侧面会落到邻格（spec 报「瞄上方却放旁边」）而非补齐同格；且上半砖只能
        //   由底面合并（玩家很难从下方点中）→「放了上半砖后同格下半砖放不下」。改读命中点 Y 后，任意面点中
        //   空半即合并（含侧面）；点中实半则 fall-through 走下方常规邻格放置。
        const float fracY = m_hitPointY - float(m_hitBy);
        const bool merge = hitUpper ? (fracY < 0.5f) : (fracY >= 0.5f);
        if (merge) {
            if (overlapsPlayerAABB(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::Planks, 0)) return;
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::Planks,
                              BlockRegistry::PlanksFromDoubleSlabBit);
            m_lastPlaceMs = now;
            emit swingArm(); // 合成也是一次「放置」动作 → 挥手（t29）
            return;
        }
    }
    // t198 水中可放方块（排开水）：目标格为空气或水均可放置；水（水源 state=0 / 流水 state 1..7）被
    //   方块直接覆盖 → World::setBlock 内 oldId=Water → newId=实体走「放置」分支（仅发 blockPlaced，
    //   不发 blockBroken → 无破块粒子 / 音；水静默消失，机制等价 MC「方块填入水格排开水」）。已有实体
    //   方块 → 拒（不覆盖）。下一 tickWaterFlow 因目标格已非 air → 不再向其扩散 → 邻接流水按失支撑逐环衰退。
    {
        const quint8 tid = m_world->blockAt(tx, ty, tz);
        if (tid != BlockRegistry::Air && tid != BlockRegistry::Water) return; // 已有实体方块 → 不放
    }
    const bool isDoor = (m_selectedBlock == BlockRegistry::WoodDoor);
    const quint8 doorFacing = quint8(horizontalFacing() & 3); // door 朝向（上下格同 facing；上格 +bit3）
    // 与玩家重叠 → 不放（防自埋 / 卡死）。t146：按「将放置方块的实际形状 sub-AABB」判 —— 不完整方块可能
    //   只占半格，玩家在另半格内仍可放；air/torch 无 sub-AABB → 不挡（允许放入玩家格，机制等价 MC）。
    //   door 占两格 → 上下格都查。
    if (overlapsPlayerAABB(tx, ty, tz, idByte, isDoor ? doorFacing : placeState)) return;
    if (isDoor && overlapsPlayerAABB(tx, ty + 1, tz, idByte, quint8(doorFacing | 8))) return;
    // t114 火把放置预检：火把需挂到实体邻居（下 / 四侧之一为实体方块），否则拒绝（机制等价 MC「火把
    // 需要支撑面」—— 平地或墙面）。判定用 BlockRegistry::isSolid（实体方块语义；不挂到空气 / 另一火把
    // / 工作台等非实体方块）。torchHost 据同样语义在运行期推断朝向（下 solid=垂直 / 侧 solid=横插）。
    if (m_selectedBlock == BlockRegistry::Torch) {
        const bool below = BlockRegistry::isSolid(m_world->blockAt(tx, ty - 1, tz));
        const bool px = BlockRegistry::isSolid(m_world->blockAt(tx + 1, ty, tz));
        const bool nx = BlockRegistry::isSolid(m_world->blockAt(tx - 1, ty, tz));
        const bool pz = BlockRegistry::isSolid(m_world->blockAt(tx, ty, tz + 1));
        const bool nz = BlockRegistry::isSolid(m_world->blockAt(tx, ty, tz - 1));
        if (!below && !px && !nx && !pz && !nz) return; // 无任何实体邻居 → 悬空火把，拒绝放置
    }
    // t134 不完整方块放置：door 占两格（下格 + 上格），需上格也为空气；其余单格。走 setBlock 5 参数版
    //   （写 id + state）。state 复用上方算出的 placeState / doorFacing（逻辑同源，无重复推导）。
    if (isDoor) {
        // t208 防半截门（可穿根因 2）：门占两格，上格越世界高度时 World::setBlock 静默返 false（caller 不查）
        //   → 只剩下单格 → 玩家跳跃（顶点 1.25 > 单格 1.0）即可跨过。显式查 ty+1 在界内且为空气才放，
        //   否则整门拒绝（两格都不放），杜绝「上格缺失的单格门」。
        if (ty + 1 >= m_world->height()) return;                       // 上格越世界顶 → 门放不下（拒整门）
        {   // t198：门上格亦允许排开水（与下格同语义，水源 / 流水均可被门替换）。
            const quint8 upId = m_world->blockAt(tx, ty + 1, tz);
            if (upId != BlockRegistry::Air && upId != BlockRegistry::Water) return; // 上格非空（实体）→ 门放不下
        }
        m_world->setBlock(tx, ty, tz, idByte, doorFacing);              // 下格：bit3=0(下格) bit2=0(合) bit[1:0]=朝向
        m_world->setBlock(tx, ty + 1, tz, idByte, quint8(doorFacing | 8)); // 上格：bit3=1
    } else {
        // fence / pressure_plate / trapdoor：placeState=0（trapdoor 默认水平合）。
        m_world->setBlock(tx, ty, tz, idByte, placeState);
    }
    m_lastPlaceMs = now; // 放置成功 → 刷新 CD 计时（t128；now 为入口时间戳，同帧无意义漂移）
    // t125 火把朝向：把玩家点击面外法线随放置事件传出，供呈现层按玩家意图定向（柄嵌所点墙面，
    //   非旧固定优先级误判）。法线为射线命中面外法线（指向玩家侧），值在 placeBlock 入口已由 updateRaycast
    //   确定、此处不变；按值传出无后效依赖（即便下一帧 raycast 改向也不影响本火把）。
    if (m_selectedBlock == BlockRegistry::Torch)
        emit torchPlaced(tx, ty, tz, m_hitNx, m_hitNy, m_hitNz);
    emit swingArm(); // 放块成功 → 第一人称手挥动（t29）
}

// Q 键丢弃（t36）：从选中槽 takeStack 1 件 → 发 spawnItem（玩家前方 1.5 格，count=1）。
// 仅指针捕获时生效（spec：「Q 键（captured 时）」）。取失败（空栈 / 无 hotbar）→ 不丢。
// spawnItem 经 QML Connections 转发到 ItemEntityManager.spawnItem（同破块掉落 t35 路径），
// 丢弃后实体在前方生成 → 可被重新拾取（闭环）。丢弃位置取眼位 + 视线 * 1.5，floor 到格坐标。
// t56：选中槽为空时直接早退（id==0）—— 若用户从背包拾取到光标后关包，光标手持栈（heldBlock）
//   须经 Main.qml::returnHeldToHotbar 在关包时归还进 hotbar（优先选中槽），否则 Q 读空槽不丢。
// t64：Q 键每次只丢 1 件（dropHeld 的语义不变；整栈丢弃走 dropHeldCursor）。
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
    emit spawnItem(int(std::floor(p.x())), int(std::floor(p.y())), int(std::floor(p.z())), id, 1);
}

// 拖出背包丢弃（t49 / t64）：光标手持栈整栈丢弃为**单个实体携带整栈数量**（玩家前方）。不限捕获态
// （背包打开时正是未捕获）。t64 修复：原 emit 仅传 id（count 走默认 1）→ 4 木棒丢出只生 1 实体 count=1，
// 捡回只剩 1（用户：「4 木棒丢出去捡起来只剩 1 个」）。现传 heldCount → 1 实体携带整栈 → 捡回原数。
// 清空 hotbar 光标手持栈（setHeldBlock(0) 同步清 count），再 emit spawnItem。空手 / 无 hotbar → 不丢。
// 位置同 dropHeld：眼位 + 视线 * 1.5，floor 到格坐标（ItemEntityManager 存格中心 = 整数+0.5）。
void PlayerController::dropHeldCursor()
{
    if (!m_hotbar) return;
    const int id = m_hotbar->heldBlock();
    const int cnt = m_hotbar->heldCount();
    if (id == 0 || cnt <= 0) return; // 空手 → 不丢
    m_hotbar->setHeldBlock(0);       // 清空光标手持栈（id=0 同步清 count）
    const QVector3D fwd = lookDirection();
    const QVector3D p = position() + fwd * 1.5f;
    emit spawnItem(int(std::floor(p.x())), int(std::floor(p.y())), int(std::floor(p.z())), id, cnt);
}

// t175 死亡掉落：玩家死亡时把整个背包（hotbar 9 + main 27 + 光标手持栈）全部掉落为物品实体（死亡点
//   = 脚底 m_pos），随后清空背包。每非空栈 → 1 实体携带整栈数量（同 dropHeldCursor 模式）；空栈跳过。
//   ItemEntityManager 无水平速度物理，靠散布到死亡格 3×3 邻域做视觉分离（轮回 9 格 pattern：>9 栈后
//   回中心格可接受重叠）。光标手持栈一并掉落（onDied 已先 returnHeldToHotbar 归还背包合并，此处为
//   防御双保险，held 通常已空）。最后 resetForMode(Survival) 清空 hotbar+main+held + bump revision →
//   QML 同步（slotsChanged/mainSlotsChanged/heldBlockChanged）。死亡只在 Survival 发生（fallDamage /
//   suffocation 均 Survival 路径），故 resetForMode(int(Survival)) 清空语义正确。
//   顺序关键：先 emit spawnItem（创建实体 = 物品移出背包），后 resetForMode 清空（物品不再在背包）→
//   无重复（实体一份 + 背包空）。spawnItem 同步走 QML DirectConnection（同线程）→ 实体即刻生成。
void PlayerController::dropAllItems()
{
    if (!m_hotbar) return;
    m_dead = true; // 标记死亡态 → 抑制 pickupScan（玩家尸体停死亡点，免掉落物被自动捡回空背包）
    // 死亡点 = 玩家脚底整数格（玩家倒下处）；3×3 邻域散布避免全堆同一格中心。
    const int cx = int(std::floor(m_pos.x()));
    const int cy = int(std::floor(m_pos.y()));
    const int cz = int(std::floor(m_pos.z()));
    // 9 格轮回散布 pattern（中心 + 8 邻）。idx 单调递增，>9 栈后回中心格（接受少量重叠）。
    static constexpr int kScatter[9][2] = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    int idx = 0;
    auto dropStack = [&](int id, int count) {
        if (id == 0 || count <= 0) return;          // 空栈跳过
        emit spawnItem(cx + kScatter[idx % 9][0], cy, cz + kScatter[idx % 9][1], id, count);
        ++idx;
    };
    // hotbar 9 槽 → main 27 槽 → 光标手持栈，逐栈掉落。
    for (int i = 0; i < m_hotbar->slotCount(); ++i)
        dropStack(m_hotbar->blockIdAt(i), m_hotbar->countAt(i));
    for (int i = 0; i < m_hotbar->mainCount(); ++i)
        dropStack(m_hotbar->mainBlockIdAt(i), m_hotbar->mainCountAt(i));
    dropStack(m_hotbar->heldBlock(), m_hotbar->heldCount()); // 光标手持栈（onDied 已归还，通常空）
    // 清空整个背包（hotbar + main + held）+ bump revision → QML 同步。仅 Survival 调（死亡仅在 Survival）。
    m_hotbar->resetForMode(int(Survival));
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

// 拾取扫描（t36 / t64 / t97）：每帧扫附近掉落实体 → Hotbar::addToAny（跨 main + hotbar 智能堆叠，
// 先合并 main 同 id → 再 hotbar 同 id → 再空槽 main→hotbar）。
// t97：原走 addStack（只看 hotbar 9 槽），主栏物品拾取进不去主栏、丢弃回栏不合并 → 改 addToAny 让拾取
// 能进主栏（修「主栏不同步」根因）。
// t64：拾取按 entity.count 全数尝试入背包；addToAny 返回未放入数（leftover）：
//   - leftover == 0：全入 → removeAt 销毁实体（spec：拾取后销毁）。
//   - 0 < leftover < entity.count：部分入 → setCountAt(i, leftover) 把余数回写、保留 entity（玩家背包
//     只剩空槽位不足 maxStack 时常见；spec「超 maxStack 分裂」由 addToAny 自然分到多槽，entity 留余）。
//   - leftover == entity.count：背包完全装不下（同 id 槽全满 + 无空槽）→ 不动 entity（spec：全满→不拾取）。
// 距离从玩家 AABB 中心（脚底 + 半高）3D 起算，阈值 kPickupDist；从后往前扫 → erase 不影响前面索引。
// t51：AABB 高用 m_height（蹲下变矮 → 中心随之降低，仍贴近玩家实际占据空间）。
// t53：跳过新生免拾取期内的实体（isPickupReady=false）——破块瞬间实体常在玩家近旁（脚下方块 ~1.4 格
//   < kPickupDist 1.5），若无延迟则下一帧即被收走、玩家永远看不见（疑似 auto-collect 根因）。让实体
//   先可见 0.5s 再开放拾取（机制等价 MC block-break pickup delay）。
void PlayerController::pickupScan()
{
    if (!m_itemEntities || !m_hotbar) return;
    if (m_dead) return; // t175：死亡态不拾取（玩家尸体停死亡点，否则掉落物 0.5s 免拾窗后被自动捡回空背包）
    const QVector3D center = m_pos + QVector3D(0.0f, m_height * 0.5f, 0.0f);
    const float r2 = kPickupDist * kPickupDist;
    for (int i = m_itemEntities->count() - 1; i >= 0; --i) {
        if (!m_itemEntities->isPickupReady(i)) continue; // t53：新生 0.5s 免拾取（让实体先可见再可拾）
        const QVector3D d = m_itemEntities->posAt(i) - center;
        if (d.lengthSquared() > r2) continue;          // 超阈值 → 跳过
        const int id = m_itemEntities->itemIdAt(i);
        const int have = m_itemEntities->countAt(i);    // t64：实体携带数量（整栈丢弃场景）
        if (have <= 0) { m_itemEntities->removeAt(i); continue; } // 防御：count 已为 0 → 销毁
        const int leftover = m_hotbar->addToAny(id, have); // t97：跨 main + hotbar 智能堆叠；按 maxStack 分流
        if (leftover <= 0) {
            m_itemEntities->removeAt(i);                // 全入 → 销毁实体
            // t118：拾取语义事件（驱动 AudioManager.playPickup 拾取音；t120 亦据此驱动手弹跳动画）。
            emit itemPickedUp(id, have);
        } else if (leftover < have) {
            m_itemEntities->setCountAt(i, leftover);    // 部分入 → 余数回写、entity 保留
            // t118：部分拾取也算拾取事件（按实际入栈数计；spec「拾取」语义覆盖「全 / 部分」两路）。
            emit itemPickedUp(id, have - leftover);
        } // else leftover == have：背包完全装不下 → entity 不动（spec：全满→不拾取；不发事件）
    }
}

// t146 放置校验：按「将放置方块的实际形状 sub-AABB」判是否与玩家 AABB 相交（3 轴严格重叠）。
//   id/state = 放置态（placeBlock 算好后传入）。air/torch（shape=ShapeNone）→ collisionAABBs 空 →
//   不相交 → 允许放入玩家格（机制等价 MC「非实体方块可放入玩家所在格」）。t51：AABB 高用 m_height。
bool PlayerController::overlapsPlayerAABB(int bx, int by, int bz, quint8 id, quint8 state) const
{
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(),           maxy = m_pos.y() + m_height;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    const float fx = float(bx), fy = float(by), fz = float(bz);
    for (const BlockRegistry::BlockAABB &a : BlockRegistry::collisionAABBs(id, state)) {
        if (minx < a.maxX + fx && maxx > a.minX + fx &&
            miny < a.maxY + fy && maxy > a.minY + fy &&
            minz < a.maxZ + fz && maxz > a.minZ + fz) return true;
    }
    return false;
}

// t146 玩家 AABB vs 世界碰撞 sub-AABB 重叠测试（3 轴严格重叠）。axis∈{0,1,2} 时记录沿该轴的相交
//   sub-AABB 表面（outMinSurf = 相交盒 min 表面；outMaxSurf = 相交盒 max 表面），供 moveAxis 贴面：
//   向 + 移动贴到 minSurf（玩家 max 边 = 最近进入面）、向 - 移动贴到 maxSurf（玩家 min 边 = 最近阻挡面）。
//   axis<0 仅判命中（aabbHitsSolid 用，不填表面）。取样范围与旧 aabbHitsSolid 同策略。
//   t161 修：maxSurfCap 仅把 bmax<=cap 的块顶计入 maxSurf（向下着地用：cap=pyBefore → 只累计「玩家移动前
//   脚底 ≥ 块顶」的可着陆面，忽略沙等 bury 块），outHasMax 报是否有合格块。默认 cap=+inf、outHasMax=nullptr
//   → 取全部块顶（旧行为），兼容 aabbHitsSolid / 水平轴贴面调用。
bool PlayerController::overlapSubAABBs(int axis, float *outMinSurf, float *outMaxSurf,
                                       bool *outHasMax, float maxSurfCap) const
{
    if (!m_world) return false;
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(),           maxy = m_pos.y() + m_height; // t51：蹲下用 m_height（变矮）
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    // 严格重叠：ceil(max)-1 排除「仅贴面」的方块 → 防卡缝
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    // t209 Y 取样向下扩 1 格（yFloor - 1）：栅栏等「高 AABB」（maxY > 1，探入上格）的方块其 sub-AABB 会从
    //   玩家脚位下一格延伸上来。玩家跳跃（脚位上升到 F+1.x）时，栅栏立柱（cell F，AABB [F, F+1.5]）仍在
    //   Y 区间重叠 [F+1.x, F+1.5]，但 floor(miny)=F+1 → 原 y0=F+1 漏掉 cell F → 玩家跨格横移即穿隧道越栅栏。
    //   扩 1 格后 y0=F 命中栅栏格、3 轴严格重叠测试仍过滤掉「 maxY<=cell 顶」的普通方块（其 AABB 上界 ≤
    //   yFloor ≤ miny → miny < b.maxY=false，无假阳性）。仅 +Y 向上凸出的形状（栅栏 / 未来 fence gate 等）获益。
    const int yFloor = int(std::floor(miny));
    const int y0 = yFloor - 1, y1 = int(std::ceil(maxy)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    bool hit = false, haveMin = false, haveMax = false;
    float minSurf = 0.f, maxSurf = 0.f;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(x, y, z)) {
                    if (!(minx < b.maxX && maxx > b.minX &&
                          miny < b.maxY && maxy > b.minY &&
                          minz < b.maxZ && maxz > b.minZ)) continue; // 3 轴任一仅贴面 / 不重叠 → 跳过
                    hit = true;
                    if (axis < 0) continue; // 仅判命中（aabbHitsSolid）
                    const float bmin = (axis == 0) ? b.minX : (axis == 1) ? b.minY : b.minZ;
                    const float bmax = (axis == 0) ? b.maxX : (axis == 1) ? b.maxY : b.maxZ;
                    if (outMinSurf && (!haveMin || bmin < minSurf)) { minSurf = bmin; haveMin = true; }
                    // t161 修：maxSurf 只累计 bmax<=cap 的块（向下着地时 cap=pyBefore 过滤掉沙等 bury 块顶）
                    if (outMaxSurf && bmax <= maxSurfCap && (!haveMax || bmax > maxSurf)) { maxSurf = bmax; haveMax = true; }
                }
            }
    if (hit) {
        if (outMinSurf && haveMin) *outMinSurf = minSurf;
        if (outMaxSurf && haveMax) *outMaxSurf = maxSurf;
    }
    if (outHasMax) *outHasMax = haveMax; // 区分「有碰撞但无可着陆面=纯 bury」与「有可着陆面」
    return hit;
}

// t134 玩家水平朝向（4 向，据 yaw 推）：前向 = (-sin(yaw), -cos(yaw))（与 wishHoriz/lookDirection 同源）。
//   主轴（|fx| vs |fz|）+ 符号 → 0=+X 1=-X 2=+Z 3=-Z（与 stairs/door/trapdoor state 朝向编码一致）。
int PlayerController::horizontalFacing() const
{
    const float yr = m_yaw * kDeg;
    const float fx = -std::sin(yr), fz = -std::cos(yr);
    if (std::fabs(fx) >= std::fabs(fz)) return fx > 0.0f ? 0 : 1; // +X / -X
    return fz > 0.0f ? 2 : 3;                                     // +Z / -Z
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

// t159 当前有效飞行速度（blocks/sec）= clamp(kFly * flySpeedMul, kFlyMin, kFlyMax)。
//   spectator 与 creative-飞 共用（统一「飞行」单一旋钮）；step 各飞态分支读它算位移。
float PlayerController::flySpeed() const
{
    return std::clamp(kFly * m_flySpeedMul, kFlyMin, kFlyMax);
}

// t159 水下判定：眼位格 == Water。眼位 = position()（脚底 + eyeHeight，与相机同源）。只读 World::blockAt
//   （向下依赖，不改栅格；蹲下眼位降低随之变）。无世界 → false（保守不减速）。
bool PlayerController::eyeInWater() const
{
    if (!m_world) return false;
    const QVector3D eye = position();
    return m_world->blockAt(int(std::floor(eye.x())), int(std::floor(eye.y())), int(std::floor(eye.z())))
           == BlockRegistry::Water;
}

// t174 脚位水中判定：脚底格 == Water（m_pos 整数坐标 → 脚所处方块）。浮力/游泳物理用它（眼位高于水面
//   时仍能游；机制等价 MC「在水中游泳」= 脚或身在水中即可）。只读 World::blockAt；无世界 → false。
bool PlayerController::feetInWater() const
{
    if (!m_world) return false;
    return m_world->blockAt(int(std::floor(m_pos.x())), int(std::floor(m_pos.y())), int(std::floor(m_pos.z())))
           == BlockRegistry::Water;
}

// t159 上报实际水平速度（speed 属性）：= |水平位移| / dt。含撞墙归零、疾跑 / 飞 / 水下倍数 —— 反映
//   玩家**真实**水平移动速率（非意图速度）。值真变（> 0.05 阈值，免帧间噪声刷 QML）才发 moveSpeedChanged
//   （speed 复用此 NOTIFY）。dt<=0 → no-op（极端：首帧 dt 未取）。
void PlayerController::reportHorizSpeed(const QVector3D &posBefore, qreal dt)
{
    if (dt <= 1e-5) return;
    const float dx = m_pos.x() - posBefore.x();
    const float dz = m_pos.z() - posBefore.z();
    const float s = std::sqrt(dx * dx + dz * dz) / float(dt);
    if (std::fabs(s - m_horizSpeed) > 0.05f) {
        m_horizSpeed = s;
        emit moveSpeedChanged();
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
            if (m_world->isCollidable(xx, by, zz)) return true; // t88：火把 non-solid 不挡玩家
    return false;
}

bool PlayerController::aabbHitsSolid() const
{
    // t146：委托 overlapSubAABBs（axis<0 仅判命中）。逐格逐 sub-AABB 测试 → 不完整方块精确碰撞
    //   （下半砖只在 y[0,0.5] 挡玩家，上半空气可穿过）。火把 shape=ShapeNone → 无 sub-AABB → 不挡。
    return overlapSubAABBs(-1, nullptr, nullptr);
}

// 沿单轴移动 amount；碰撞则贴面 + eps + 清该轴速度。无世界则自由移动。
// t51：AABB 高用 m_height（蹲下变矮 → 头顶碰撞 / 着地贴面随之变；顶头贴面按当前高度算）。
// t146：贴面到相交 sub-AABB 的实际表面（非整格边界）—— 落下半砖着地于 y=0.5，撞栅栏柱贴到柱面。
void PlayerController::moveAxis(int axis, float amount)
{
    if (amount == 0) return;
    switch (axis) {
    case 0: m_pos.setX(m_pos.x() + amount); break;
    case 1: m_pos.setY(m_pos.y() + amount); break;
    case 2: m_pos.setZ(m_pos.z() + amount); break;
    }
    if (!m_world) return;
    float minSurf = 0.f, maxSurf = 0.f;
    bool hasMax = false;
    // t161 修：向下（axis==1, amount<0）着地面只取玩家移动前脚底 pyBefore ≥ 块顶 bmax 的块（地面顶），
    //   忽略沙等 pyBefore<bmax 的 bury 块（顶更高）→ maxSurfCap=pyBefore+eps 过滤。否则 overlapSubAABBs 取
    //   所有重叠块 MAX（=沙顶 11）会让 snap 无效、地面托举（顶 10）随之失效，每 tick 重力再下沉穿地坠虚空。
    //   其他方向（X/Z、Y 向上顶头）cap=+inf = 取全部（旧行为）。
    const float pyBefore = m_pos.y() - amount; // 移动前脚底 Y（Y 已按 amount 更新，减回 = 旧值）
    const float cap = (axis == 1 && amount < 0) ? (pyBefore + 1e-3f)
                                                : std::numeric_limits<float>::max();
    if (!overlapSubAABBs(axis, &minSurf, &maxSurf, &hasMax, cap)) return; // 无碰撞 → 自由
    const float eps = 1e-4f;
    switch (axis) {
    case 0:
        // 向 +：玩家 max 边贴到最近 sub-AABB 的 min 面；向 -：玩家 min 边贴到最近 sub-AABB 的 max 面。
        if (amount > 0) m_pos.setX(minSurf - kHalfW - eps);
        else            m_pos.setX(maxSurf + kHalfW + eps);
        m_vel.setX(0); break;
    case 1:
        if (amount > 0) {
            m_pos.setY(minSurf - m_height - eps); // 顶头（按当前高度）
        } else {
            // t161 修：maxSurf 已被 cap=pyBefore 限定为「玩家原本站其顶上」的可着陆最高面（地面顶 10，非沙顶 11）。
            //   hasMax=true → 贴顶面站住（地面托举正常生效，玩家不再逐 tick 下沉穿地）；hasMax=false = 所有重叠
            //   块顶都在 pyBefore 之上（纯 bury：沙落身上 / 侧面卡入且脚下无支撑面）→ 不 snap Y（留格内），交后续
            //   moveAxis(0/2)+extrudeEmbedded 横向推出（用户「向外挤 not 向上」）。被完全包裹（无水平出路）则由
            //   t160 窒息扣血兜底。原 t161「据 inflated maxSurf 全不 snap」会连地面托举一起失效 → 穿地坠虚空。
            if (hasMax) m_pos.setY(maxSurf + eps); // 站到可着陆最高面（地面顶，非沙顶）
            // else 纯 bury：不 snap Y（留格内，待横向推出）
        }
        m_vel.setY(0); break;
    case 2:
        if (amount > 0) m_pos.setZ(minSurf - kHalfW - eps);
        else            m_pos.setZ(maxSurf + kHalfW + eps);
        m_vel.setZ(0); break;
    }
}

// t161 嵌入挤出：见 .h 注释。补充实现要点——
//   1) 嵌入块定位：扫玩家 XZ 中心列 (bx,bz) 在玩家全高 [y0,y1] 各格的 sub-AABB，找到第一个与玩家 AABB
//      真重叠（3 轴严格重叠）的实体格 embY。中心列是玩家正常占据的「空气柱」，凡该柱出现重叠实体 = 有
//      方块在玩家身上 materialize（下落沙着地 / 侧面塞入）= burial；正常贴墙时墙在玩家前导边、中心列仍
//      空气 → embY<0 不触发。
//   2) 逃向选择：4 向邻列（bx±1 / bz±1）在玩家全高全开放（无 collidable）= 可逃；取玩家中心距嵌入块该
//      侧边界最近的一向（最少位移）。把玩家整体推到该侧嵌入块面之外（footprint 完全进入开放邻列）。
//   3) 全包裹（4 向皆堵）→ 无可逃向 → 不动，交 t160 窒息扣血兜底（spec「被完全包裹」语义）。
//   注：仅改 XZ、不动 Y / 速度 → 与 moveAxis(1) 不上抬（fab580e）正交协同：Y 留格内待此处横向清出。
void PlayerController::extrudeEmbedded()
{
    if (!m_world) return;
    const float px = m_pos.x(), pz = m_pos.z();
    const int bx = int(std::floor(px));
    const int bz = int(std::floor(pz));
    const int y0 = int(std::floor(m_pos.y()));
    const int y1 = int(std::floor(m_pos.y() + m_height));
    const float minx = px - kHalfW, maxx = px + kHalfW;
    const float miny = m_pos.y(),        maxy = m_pos.y() + m_height;
    const float minz = pz - kHalfW, maxz = pz + kHalfW;
    // 1) 找嵌入块（中心列上某 Y 格真重叠）。
    int embY = -1;
    for (int y = y0; y <= y1 && embY < 0; ++y) {
        for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(bx, y, bz)) {
            if (minx < b.maxX && maxx > b.minX &&
                miny < b.maxY && maxy > b.minY &&
                minz < b.maxZ && maxz > b.minZ) { embY = y; break; }
        }
    }
    if (embY < 0) return; // 中心列为空气（玩家正常占据）→ 非嵌入，不干预
    // 2) 邻列全高开放判定（玩家能整身进入才算可逃，避免挤进半堵列又被卡）。
    const auto columnClear = [&](int cx, int cz) -> bool {
        for (int y = y0; y <= y1; ++y)
            if (m_world->isCollidable(cx, y, cz)) return false;
        return true;
    };
    // 3) 取最近开放侧（玩家中心距嵌入块该侧边界最近 = 位移最小）。
    const float toPX = (bx + 1) - px, toMX = px - bx; // 到嵌入块 ±X 面距离
    const float toPZ = (bz + 1) - pz, toMZ = pz - bz; // 到嵌入块 ±Z 面距离
    int bestAxis = -1, bestDir = 0; float bestDist = 1e9f;
    const auto consider = [&](int axis, int dir, float d) {
        if (d >= bestDist) return;
        const int cx = bx + (axis == 0 ? dir : 0);
        const int cz = bz + (axis == 2 ? dir : 0);
        if (!columnClear(cx, cz)) return; // 该侧堵 → 跳过
        bestAxis = axis; bestDir = dir; bestDist = d;
    };
    consider(0,  1, toPX); consider(0, -1, toMX);
    consider(2,  1, toPZ); consider(2, -1, toMZ);
    if (bestAxis < 0) return; // 全包裹 → 不挤（t160 窒息兜底）
    const float eps = 1e-3f;
    if (bestAxis == 0)
        m_pos.setX(bestDir > 0 ? (bx + 1) + kHalfW + eps : bx - kHalfW - eps); // 出 ±X 面，footprint 入开放邻列
    else
        m_pos.setZ(bestDir > 0 ? (bz + 1) + kHalfW + eps : bz - kHalfW - eps);
}

void PlayerController::step(qreal dt)
{
    const QVector3D posBefore = m_pos; // t159：出口算实际水平速度（speed 属性）的位移基准
    const QVector3D wish = wishHoriz();
    const bool space = m_keys.value(Qt::Key_Space), shift = m_keys.value(Qt::Key_Shift);
    const bool spaceEdge = space && !m_spacePrev; // 跳跃边沿：长按只跳一次（生存/创造-走路统一）
    m_spacePrev = space;
    // t159 水下速度倍数：眼位在水格 → 水平（及飞垂直）速度 ×kUnderwaterSpeedMul（~0.4）。所有模式统一适用
    //   （走 / 飞 / 观察者进水都变慢，机制等价 MC 水中减速）。每 tick 查一次（blockAt 单查，廉价）。
    const float waterMul = eyeInWater() ? kUnderwaterSpeedMul : 1.0f;

    // 行走动画驱动（t45）：moveSpeed 仅走路模式（Survival / Creative-未飞）按住 WASD 时非零；
    //   Spectator / Creative-飞 → 0（飞行/幽灵态无走步动画，spec 未要求；为未来泳/飞姿留接口）。
    //   walkPhase 仅在走时累加（speed*dt*kStrideRate），2π 回绕；静止不累加 → QML 据此 sin*0=0 中性位。
    //   「按住 WASD 撞墙」时 wish 仍非零（玩家在「尝试」走）→ 腿仍摆，对齐 MC 行为。
    //   t51：moveSpeed 乘 speedMul（疾跑 ×1.3 / 蹲 ×0.4）→ walkPhase 推进速率随状态变（动画频率）。
    //   t159：moveSpeed 不乘 waterMul（动画驱动用「意图」速度；水下减速是位移层，与四肢摆频解耦）。
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
        // t159：飞态统一用 flySpeed()（滚轮可调）× waterMul；spectator 常驻飞亦适用（spec「创造/观察者」）。
        const float fs = flySpeed() * waterMul;
        QVector3D v = wish * fs;
        if (space) v.setY(fs);
        if (shift) v.setY(-fs);
        m_pos += v * dt; // noclip
        reportHorizSpeed(posBefore, dt); // t159：speed 属性上报
        emit positionChanged();
        return;
    }

    if (m_mode == Creative && m_flying) {
        // t159：飞态统一用 flySpeed()（滚轮可调）× waterMul（原水平基 kWalk 改 kFly 基，与 spectator 同旋钮；
        //   默认 mul=1.0 → kFly=8，比旧 kWalk=4.3 快但更贴近 MC 创造飞，且滚轮可在 4..20 调）。
        const float fs = flySpeed() * waterMul;
        QVector3D v = wish * fs;
        if (space) v.setY(fs);
        if (shift) v.setY(-fs);
        // t208 防穿隧道（薄板 / 门）：旧 creative-fly 无子步（unlike walk 路径的 ≤0.4/子步）→ 滚轮加速到 16+
        //   blocks/sec 叠卡顿（dt 钳到 0.05）时单次 moveAxis 位移可达 0.8+，一步跨过薄障碍（门 3/16 板穿隧道
        //   阈值仅 0.7875 = 板厚 0.1875 + 玩家宽 0.6）。门是游戏内最薄的可放置障碍 → 最先被穿（整立方墙阈值
        //   1.6，飞 20×0.05=1.0 也穿不过）。补子步（同 walk：任意轴单步 ≤0.4 格）→ 子步位移 < 玩家宽 0.6 →
        //   必与路径上任何障碍重叠被检出。lessons-learned「dt 钳制 + 子步防穿墙」同源。
        QVector3D delta = v * float(dt);
        const float md = std::max({std::fabs(delta.x()), std::fabs(delta.y()), std::fabs(delta.z())});
        const int sub = std::max(1, int(std::ceil(md / 0.4f)));
        delta /= float(sub);
        for (int i = 0; i < sub; ++i) {
            moveAxis(0, delta.x()); // 碰撞，无重力
            moveAxis(2, delta.z());
            moveAxis(1, delta.y());
        }
        if (m_onGround) { m_onGround = false; emit onGroundChanged(); }
        reportHorizSpeed(posBefore, dt); // t159：speed 属性上报
        emit positionChanged();
        return;
    }

    // 走（生存 / 创造-未飞）：重力 + 跳跃 + 逐轴解算
    // t51：水平速度乘 speedMul（疾跑 ×1.3 / 蹲 ×0.4 / 走 ×1.0）；spec「疾跑移速 +、蹲下 -」。
    //   双击 W 疾跑（setKey 内 m_lastWms 双击窗 ≤250ms 进 Sprint）→ speedMul()=1.3 实际乘入此处水平速度，
    //   走速 4.3 → 疾跑 5.59 blocks/sec（同 MC 1.0 系数）。t159：再乘 waterMul（水下减速）。
    m_vel.setX(wish.x() * kWalk * speedMul() * waterMul);
    m_vel.setZ(wish.z() * kWalk * speedMul() * waterMul);
    // t174 水中浮力 / 游泳（spec「浮力/游泳」）：脚位在水格 → 缓沉（kWaterGravity << kGravity）+ 按住空格
    //   上浮（kSwimUp，连续非边沿）+ 钳最大下沉（防穿水底）。机制等价 MC 1.0 水中：减速 + 浮力 + 空格上浮。
    //   离水（脚位非水）走原重力 + 跳跃（spaceEdge && onGround）。waterMul 已乘水平速度（眼位在水中减速）。
    if (feetInWater()) {
        // t211 水流推动玩家（spec「创造非飞 + 生存，玩家在流水中被水流沿流动方向水平推动」）：
        //   脚位在流水格（state>0；水源 state=0 静止不推，spec）→ 沿「离源方向」叠入水平推力。流向据脚位
        //   4 向邻居 state 梯度推算：state 低于脚位的邻居 = 近源方向 → 推力朝远离它（离源 = 高 state 远源
        //   方向）。梯度加权（footState − ns）使陡降（近源 → 远源跨多级）推得更猛；归一化后 ×kWaterFlowPush
        //   叠入 m_vel.x/z（与玩家 wish 输入相加 → 逆流游净速 = 走速 − 推力，松手则被流走）。无梯度（四面无
        //   更近源邻居，如水源/对称流）→ 不推。仅走路模式生效（Spectator / Creative-飞 已 early return）。
        //   悬崖边落水额外向下带：脚位下方为空气 = 水柱下落（瀑布）→ 下沉上限抬到 kWaterfallSinkMax。
        const int fx = int(std::floor(m_pos.x()));
        const int fy = int(std::floor(m_pos.y()));
        const int fz = int(std::floor(m_pos.z()));
        const quint8 footState = m_world->stateAt(fx, fy, fz);
        if (footState > 0) { // 流水格才推（水源 state=0 不推，spec）
            float gx = 0.0f, gz = 0.0f;
            constexpr int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto &d : dirs) {
                const int nx = fx + d[0], nz = fz + d[1];
                if (m_world->blockAt(nx, fy, nz) == BlockRegistry::Water) {
                    const quint8 ns = m_world->stateAt(nx, fy, nz);
                    if (ns < footState) { // 该邻居更近源 → 推力朝远离它（离源）
                        gx -= float(d[0]) * float(footState - ns);
                        gz -= float(d[1]) * float(footState - ns);
                    }
                }
            }
            const float glen = std::sqrt(gx * gx + gz * gz);
            if (glen > 1e-4f) {
                m_vel.setX(m_vel.x() + (gx / glen) * kWaterFlowPush);
                m_vel.setZ(m_vel.z() + (gz / glen) * kWaterFlowPush);
            }
        }
        // 瀑布（脚位下方空气 = 水柱下落）→ 下沉上限抬高（额外向下带）；普通水仍 kWaterSinkMax。
        const bool waterfall = footState > 0
            && m_world->blockAt(fx, fy - 1, fz) == BlockRegistry::Air;
        const float sinkMax = waterfall ? kWaterfallSinkMax : kWaterSinkMax;
        m_vel.setY(std::clamp(float(m_vel.y() - kWaterGravity * dt), -sinkMax, kSwimUp));
        if (space) m_vel.setY(kSwimUp); // 按住空格 = 游泳上浮（连续；离水后 spaceEdge 跳跃边沿仍由下方分支处理）
    } else {
        m_vel.setY(std::max(float(m_vel.y() - kGravity * dt), -kMaxFall));
        if (spaceEdge && m_onGround) m_vel.setY(kJump);
    }

    // 防穿墙：每子步任意轴移动 ≤0.4 格
    QVector3D delta = m_vel * dt;
    const float md = std::max({std::fabs(delta.x()), std::fabs(delta.y()), std::fabs(delta.z())});
    const int sub = std::max(1, int(std::ceil(md / 0.4f)));
    delta /= float(sub);

    // t58 蹲下「边缘安全」（逐轴后查回滚）：原 t51 实现「整帧目的位置（m_pos + delta*sub）预查 →
    //   通过则整帧放行水平移动」在多轴合成位移 / 子步内 wall snap 下会与 moveAxis 后的真实 m_pos
    //   错位 → 玩家可能落到无支撑格而坠下方块边缘。改为 moveAxis(0) → 立即查脚下（新位置 -Y）有无
    //   支撑方块 → 无则回滚 X 位移；moveAxis(2) → 同样查 → 回滚 Z。判定点 = moveAxis 后的真实 m_pos
    //   （含本子步 wall snap），与 aabbHitsSolid 同源 → 边缘安全与实际碰撞一致。仅蹲态 + 本子步着地
    //   （m_onGround）时启用：走 / 疾跑 / 滞空 / 起跳子步不限制（防走下边缘，不干预跳跃 / 坠落）。
    const bool wasGround = m_onGround;
    m_onGround = false;
    for (int i = 0; i < sub; ++i) {
        const float dy = delta.y();
        moveAxis(1, dy);
        if (dy < 0 && m_vel.y() == 0) m_onGround = true; // 下落被挡 = 着地
        const bool crouchSafe = (m_moveState == Crouch && m_onGround);
        const float prevX = m_pos.x();
        moveAxis(0, delta.x());
        if (crouchSafe && delta.x() != 0.0f && !hasGroundBelowAt(m_pos.x(), m_pos.z()))
            m_pos.setX(prevX); // 蹲下边缘安全：X 移动后脚下无支撑 → 回滚该轴位移
        // t163 auto-step X：被低障碍（≤0.5：下半砖 / 楼梯整步 / 活版门合态）挡住且在地面非蹲 → 试抬升 0.55
        //   走过去（机制等价 MC 自动上半砖 / 楼梯，无需跳）。抬升后顶头或仍走不通 → 还原 Y（不影响正常碰撞）。
        if (delta.x() != 0.0f && m_pos.x() == prevX && m_onGround && m_moveState != Crouch) {
            const float baseY = m_pos.y();
            m_pos.setY(baseY + 0.55f);
            if (!aabbHitsSolid()) {
                const float prevXs = m_pos.x();
                moveAxis(0, delta.x());
                if (m_pos.x() == prevXs) m_pos.setY(baseY); // 抬升仍走不通（高墙）→ 还原
            } else {
                m_pos.setY(baseY); // 抬升顶头（天花板）→ 还原
            }
        }
        const float prevZ = m_pos.z();
        moveAxis(2, delta.z());
        if (crouchSafe && delta.z() != 0.0f && !hasGroundBelowAt(m_pos.x(), m_pos.z()))
            m_pos.setZ(prevZ); // 蹲下边缘安全：Z 移动后脚下无支撑 → 回滚该轴位移
        // t163 auto-step Z（同 X）：低障碍 + 地面 + 非蹲 → 抬升 0.55 走过去，失败还原。
        if (delta.z() != 0.0f && m_pos.z() == prevZ && m_onGround && m_moveState != Crouch) {
            const float baseY = m_pos.y();
            m_pos.setY(baseY + 0.55f);
            if (!aabbHitsSolid()) {
                const float prevZs = m_pos.z();
                moveAxis(2, delta.z());
                if (m_pos.z() == prevZs) m_pos.setY(baseY);
            } else {
                m_pos.setY(baseY);
            }
        }
    }
    // t161 嵌入挤出：逐轴解算后若玩家仍被包裹（下落沙 / 放置方块 materialize 在玩家身上），沿最近开放
    //   水平方向推出（向外 not 向上）。先于地面复探 / 窒息判定 → 挤出成功则该 tick 不误判着地 / 窒息。
    extrudeEmbedded();
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
            // t200 水抵消摔落伤害（机制等价 MC：落入水中免除摔伤）。着地瞬间脚位格 == Water → 水缓冲冲击，
            //   不结算伤害。复用 feetInWater()（脚位 blockAt == Water；水非实体 → 落地必踩在水床底块上方，
            //   floor(m_pos.y) 取水格 → 正确判中；无世界 → false 保守不抵消）。
            if (!feetInWater()) {
                const int dmg = int(std::floor(fall - 3.0f));
                if (dmg > 0) emit fallDamageTaken(dmg); // 呈现层 Connections 路由到 PlayerState.takeDamage
            }
        }
        m_peakY = m_pos.y();
    }

    // t160 窒息（仅 Survival）：眼位（头部）格为实体可碰撞方块（被埋 / 头卡进方块，机制等价 MC 窒息）→
    //   每 kSuffocationInterval 秒扣 1HP（fallDamageTaken 同路径 → PlayerState.takeDamage）+ 发 suffocationPulse
    //   （呈现层红屏闪 + 视角晃动）。创造 / 观察者无伤。脱困（头部出方块）即停累积。蹲下眼位低随之判定点下移。
    if (m_mode == Survival && m_world) {
        const int hx = int(std::floor(m_pos.x()));
        const int hy = int(std::floor(m_pos.y() + m_eyeHeight));
        const int hz = int(std::floor(m_pos.z()));
        if (m_world->isCollidable(hx, hy, hz)) {
            m_suffocationTimer += float(dt);
            if (m_suffocationTimer >= kSuffocationInterval) {
                m_suffocationTimer -= kSuffocationInterval;
                // fallDamageTaken(1) 经既有链 takeDamage→damaged→onDamaged 已驱动 HP 扣减 + 红屏闪 + 视角晃动（t67）。
                emit fallDamageTaken(1);
            }
        } else {
            m_suffocationTimer = 0.0f;
        }
    }

    // t202 气泡 + 溺水（仅 Survival 耗气；Creative/Spectator 不溺水 → 恒满气）。复用 eyeInWater()（眼位
    //   blockAt == Water；同 t201 蓝滤镜判定）。眼位入水：m_airTimer 累加 → 每 kAirInterval 减 1 气泡
    //   （emit airUpdated 驱动 PlayerState.air + 气泡条）；气泡归零后 m_drownTimer 累加 → 每 kDrownInterval
    //   扣 1HP（emit fallDamageTaken(1) 复用 takeDamage→damaged 红闪 / 视角晃链，与窒息同路径）。出水：
    //   m_airRegenTimer 累加 → 每 kAirRegenInterval 回 1 气泡（spec「出水 → 气泡回满后消失」）。非 Survival
    //   强制满气（切回 Survival 从满起算，防陈旧低气值残留）。死亡态（!m_captured 早 return）不进 step →
    //   不耗气；respawn 复位 m_air + 三计时器。
    if (m_world) {
        if (m_mode != Survival) {
            if (m_air != kMaxAir) { m_air = kMaxAir; emit airUpdated(m_air); }
            m_airTimer = 0.0f;
            m_drownTimer = 0.0f;
            m_airRegenTimer = 0.0f;
        } else if (eyeInWater()) {
            m_airRegenTimer = 0.0f;
            if (m_air > 0) {
                m_airTimer += float(dt);
                if (m_airTimer >= kAirInterval) {
                    m_airTimer -= kAirInterval;
                    --m_air;
                    emit airUpdated(m_air);
                }
            } else {
                // 气泡归零 → 溺水扣血（机制等价 MC 1.0：air=0 后每秒 1HP；复用 fallDamageTaken→damaged 链）
                m_drownTimer += float(dt);
                if (m_drownTimer >= kDrownInterval) {
                    m_drownTimer -= kDrownInterval;
                    emit fallDamageTaken(1);
                }
            }
        } else {
            // 出水：气泡逐格回满（满后气泡条消失；计时器同步清）
            m_airTimer = 0.0f;
            m_drownTimer = 0.0f;
            if (m_air < kMaxAir) {
                m_airRegenTimer += float(dt);
                if (m_airRegenTimer >= kAirRegenInterval) {
                    m_airRegenTimer -= kAirRegenInterval;
                    ++m_air;
                    emit airUpdated(m_air);
                }
            } else {
                m_airRegenTimer = 0.0f;
            }
        }
    }

    if (wasGround != m_onGround) emit onGroundChanged();
    reportHorizSpeed(posBefore, dt); // t159：speed 属性上报（位移/dt；含撞墙归零 / 疾跑 / 水下倍数）
    emit positionChanged();
}
