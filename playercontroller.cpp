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

void PlayerController::onWindowChanged(QQuickWindow *win)
{
    if (m_window) m_window->removeEventFilter(this);
    m_window = win;
    if (m_window) m_window->installEventFilter(this);
    else if (m_captured) release(); // 窗口销毁 → 安全释放
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
            emit flyingChanged();
        } else {
            m_lastSpaceMs = now;
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
            // 破/放（t05）：仅指针捕获时由窗口级事件过滤接管（未捕获时不消费 → 让暂停层 grab）。
            // 走事件过滤而非 MouseArea —— 指针锁定下光标每帧被 warp 回中，MouseArea 依赖的指针
            // 位置不可靠；窗口级 MouseButtonPress 与光标位置无关，最稳。
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_captured) {
                if (me->button() == Qt::LeftButton)  { breakBlock(); return true; }
                if (me->button() == Qt::RightButton) { placeBlock();  return true; }
            }
        }
    }
    return QQuickItem::eventFilter(o, e);
}

// ---- 主循环 ----
void PlayerController::tick()
{
    const qreal dt = qMin(m_clock.restart() / 1000.0, 0.05); // 钳 50ms，防卡顿后穿墙
    if (!m_captured) return;                                  // 暂停：冻结
    pollMouse();
    step(dt);
    updateRaycast(); // 沿视线 DDA 选体 → 更新线框命中态
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

// ---- 方块编辑（t05）----
// 编辑入口属 Game/Physics 层：输入 →（已有）射线命中 → World::setBlock。Renderer 不直接改栅格。
void PlayerController::setSelectedBlock(int id)
{
    if (id < 0 || id >= int(BlockRegistry::Count)) return; // 仅 clamp 到合法 id 区间
    if (id == m_selectedBlock) return;
    m_selectedBlock = id;
    emit selectedBlockChanged();
}

// 左键：命中格置 air。要求已捕获指针且有命中（未捕获 = 暂停，不破坏）。
// 模式门控（t21）：观察者不能破块（用户核心诉求）——在调用 World::setBlock 前拦截。
// t29：破块动作真发生（通过门控且有命中→射线只命中实体方块，必破）时发 swingArm，
// 呈现层启动第一人称手挥动。未命中/观察者不发（仅动作真发生时，spec）。
void PlayerController::breakBlock()
{
    if (!canBreak()) return; // 观察者不能破块
    if (!m_world || !m_captured || !m_hasHit) return;
    m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::Air);
    emit swingArm(); // 破块成功 → 第一人称手挥动（t29）
}

// 右键：命中面法线方向的相邻空格置当前手持方块。
// 校验：目标格须为空气（不覆盖实体）；且不与玩家 AABB 重叠（防自埋/卡死）。
// 模式门控（t21）：观察者不能放块（用户核心诉求）——在调用 World::setBlock 前拦截。
// t29：放块动作真发生（通过门控 + 目标为空 + 不埋玩家，实际写入）时才发 swingArm；
// 已有方块/重叠被拒不发（仅动作真发生时，spec）。
void PlayerController::placeBlock()
{
    if (!canPlace()) return; // 观察者不能放块
    if (!m_world || !m_captured || !m_hasHit) return;
    if (m_selectedBlock == BlockRegistry::Air) return; // 空栈 → 右键不放置（也不挥手，t32）
    const int tx = m_hitBx + m_hitNx, ty = m_hitBy + m_hitNy, tz = m_hitBz + m_hitNz;
    if (m_world->blockAt(tx, ty, tz) != BlockRegistry::Air) return; // 已有方块 → 不放
    if (overlapsPlayerAABB(tx, ty, tz)) return;                    // 与玩家重叠 → 不放
    m_world->setBlock(tx, ty, tz, quint8(m_selectedBlock));
    emit swingArm(); // 放块成功 → 第一人称手挥动（t29）
}

// 方块格 [bx,bx+1]×[by,by+1]×[bz,bz+1] 与玩家 AABB 是否相交（严格重叠；仅贴面不算）。
bool PlayerController::overlapsPlayerAABB(int bx, int by, int bz) const
{
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(),           maxy = m_pos.y() + kHeight;
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

bool PlayerController::aabbHitsSolid() const
{
    if (!m_world) return false;
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(), maxy = m_pos.y() + kHeight;
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
    const float miny = m_pos.y(), maxy = m_pos.y() + kHeight;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    const float eps = 1e-4f;
    switch (axis) {
    case 0:
        if (amount > 0) m_pos.setX(std::floor(maxx) - kHalfW - eps);
        else            m_pos.setX(std::floor(minx) + 1.f + kHalfW + eps);
        m_vel.setX(0); break;
    case 1:
        if (amount > 0) m_pos.setY(std::floor(maxy) - kHeight - eps); // 顶头
        else            m_pos.setY(std::floor(miny) + 1.f + eps);     // 着地
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
    m_vel.setX(wish.x() * kWalk);
    m_vel.setZ(wish.z() * kWalk);
    m_vel.setY(std::max(float(m_vel.y() - kGravity * dt), -kMaxFall));
    if (spaceEdge && m_onGround) m_vel.setY(kJump);

    // 防穿墙：每子步任意轴移动 ≤0.4 格
    QVector3D delta = m_vel * dt;
    const float md = std::max({std::fabs(delta.x()), std::fabs(delta.y()), std::fabs(delta.z())});
    const int sub = std::max(1, int(std::ceil(md / 0.4f)));
    delta /= float(sub);

    const bool wasGround = m_onGround;
    m_onGround = false;
    for (int i = 0; i < sub; ++i) {
        const float dy = delta.y();
        moveAxis(1, dy);
        if (dy < 0 && m_vel.y() == 0) m_onGround = true; // 下落被挡 = 着地
        moveAxis(0, delta.x());
        moveAxis(2, delta.z());
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
