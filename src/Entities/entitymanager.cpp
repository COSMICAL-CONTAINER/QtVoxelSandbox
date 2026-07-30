#include "entitymanager.h"
#include "world.h" // tick / resolvePlayerPush 只读 World::isSolid（向下依赖；PLAN §2 Entities→World 合规）

#include <QLoggingCategory>
#include <QtMath>    // qFloor
#include <algorithm> // std::clamp, std::min, std::move
#include <cmath>     // std::sqrt

namespace {
Q_LOGGING_CATEGORY(lcEnt, "vo.entity") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤，落 log 可见
}

EntityManager::EntityManager(QObject *parent) : QObject(parent) {}

// 生成测试生物：存格中心坐标 + 醒目纯色 + pushable，bump revision → QML Repeater 追加 delegate。
// 达 kCap 跳过 + 告警（防溢出，spec「实体数量有上限」）。
void EntityManager::spawnMob(int x, int y, int z)
{
    if (int(m_entities.size()) >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); spawn skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    e.radius = 0.5f;
    e.pushable = true;
    e.kind = Mob;
    e.color = QStringLiteral("#ff5555");
    m_entities.push_back(std::move(e));
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcEnt) << "spawned mob at" << x << y << z << "(total" << m_entities.size() << ")";
}

QVector3D EntityManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    return m_entities[size_t(i)].pos;
}

float EntityManager::radiusAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    return m_entities[size_t(i)].radius;
}

bool EntityManager::pushableAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].pushable;
}

int EntityManager::kindAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return Mob;
    return m_entities[size_t(i)].kind;
}

QString EntityManager::colorAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QStringLiteral("#ff5555");
    return m_entities[size_t(i)].color;
}

// 玩家推动解析：对每个 pushable 实体做「玩家 AABB（XZ 矩形）vs 实体圆（XZ，半径=entity.radius）」
// 穿透求解（机制等价 MC 实体碰撞推开：玩家位移解析后传给实体）。
//   1) 垂直区间重叠判定：实体立方体 [pos.y−r, pos.y+r] 与玩家 AABB [feet.y, feet.y+height] 必须重叠
//      才推动（玩家从头顶跳过 / 跨层时不应误推）。
//   2) XZ 穿透：AABB 最近点 cx/cz = clamp(entity 中心, [px−halfW, px+halfW])；d = entity − 最近点。
//      dist < r → 穿透 push = r − dist，沿 d/dist 推出；d≈0（中心在 AABB 内）→ 沿最近面推出（min 四向）。
//   3) 世界碰撞钳制：新位置实体中心所在格（身体中段 Y）若为实体方块 → 撤回该轴推动（防把实体推进墙）。
//      X/Z 两轴独立判定（斜推时各自检查），保证实体贴墙滑动不穿入。
//   4) 被推动 → resting=false（解除静止，让重力复探支撑：可能被推下阶梯 → 自然落）。任一 pos 真变 →
//      dirty，末尾统一 bump revision + emit（驱动 QML {revision; posAt} 绑定重算）。
void EntityManager::resolvePlayerPush(const QVector3D &playerFeet, float halfW, float height, World *world)
{
    if (m_entities.empty()) return;
    const float px = playerFeet.x(), pz = playerFeet.z();
    const float pminY = playerFeet.y(), pmaxY = playerFeet.y() + height;
    bool dirty = false;
    for (auto &e : m_entities) {
        if (!e.pushable) continue; // 掉落物等非推动实体跳过（统一基类预留）

        const float r = e.radius;
        // 垂直区间重叠判定（实体为立方体：[pos.y−r, pos.y+r]）。
        if (e.pos.y() + r <= pminY || e.pos.y() - r >= pmaxY) continue;

        // XZ 平面 AABB-vs-Circle 穿透求解。
        const float cx = std::clamp(e.pos.x(), px - halfW, px + halfW);
        const float cz = std::clamp(e.pos.z(), pz - halfW, pz + halfW);
        const float dx = e.pos.x() - cx;
        const float dz = e.pos.z() - cz;
        const float dist2 = dx * dx + dz * dz;
        if (dist2 >= r * r) continue; // 无 XZ 穿透

        float newX = e.pos.x();
        float newZ = e.pos.z();
        const float dist = std::sqrt(dist2);
        if (dist > 1e-5f) {
            const float push = r - dist;
            newX = e.pos.x() + dx / dist * push;
            newZ = e.pos.z() + dz / dist * push;
        } else {
            // 实体中心在玩家 AABB 内：沿最近面推出（min 四向距离 → 最短穿透方向）。
            const float toMinX = e.pos.x() - (px - halfW);
            const float toMaxX = (px + halfW) - e.pos.x();
            const float toMinZ = e.pos.z() - (pz - halfW);
            const float toMaxZ = (pz + halfW) - e.pos.z();
            const float m = std::min({toMinX, toMaxX, toMinZ, toMaxZ});
            if (m == toMinX)      newX = px - halfW - r;
            else if (m == toMaxX) newX = px + halfW + r;
            else if (m == toMinZ) newZ = pz - halfW - r;
            else                  newZ = pz + halfW + r;
        }

        // 世界碰撞钳制：实体身体中段 Y 所在格若为实体方块 → 撤回该轴推动（防穿墙）。X/Z 独立判定。
        if (world) {
            const int by = qFloor(e.pos.y()); // 实体中心所在格（身体中段；resting 时为支撑方块上方空气格）
            const int oldCellX = qFloor(e.pos.x());
            const int oldCellZ = qFloor(e.pos.z());
            const int newCellX = qFloor(newX);
            if (newCellX != oldCellX && world->isSolid(newCellX, by, oldCellZ)) newX = e.pos.x();
            const int newCellZ = qFloor(newZ);
            const int refCellX = qFloor(newX); // 用可能已撤回的 newX 重算参照列（斜推两轴独立）
            if (newCellZ != oldCellZ && world->isSolid(refCellX, by, newCellZ)) newZ = e.pos.z();
        }

        if (newX != e.pos.x() || newZ != e.pos.z()) {
            e.pos.setX(newX);
            e.pos.setZ(newZ);
            e.resting = false; // 被推动后可能离支撑面 → 解除静止，让重力复探（防悬空 / 阶梯推落）
            dirty = true;
        }
    }
    if (dirty) { ++m_revision; emit entitiesChanged(); }
}

// 重力 + 地面静止（机制同 ItemEntityManager::tick；向下只读 World::isSolid）。
//   1) 已 resting：复探支撑格（实体底面下方一格 = floor(pos.y − r) − 1）仍实体 → 保持静止；
//      失支撑 → 解除 resting 续落（防挖空悬空；亦承接 resolvePlayerPush 把实体推离原支撑面的情形）。
//   2) 未 resting：vy -= g*dt（钳 -kMaxFall），按 dy 下移；下移路径 [floor(newY), floor(pos.y)] 自顶向下
//      扫实体所在列首个实体方块 → 命中则贴其顶面（solidCellY+1+kRestOffset）停下、vy=0、resting=true。
//      越界（cy<0）查 isSolid 返 false → 不误判虚空地面，实体继续落。
//   3) pos / resting 任一真变 → dirty，末尾统一 bump revision + emit（驱动 QML {revision; posAt} 绑定重算）。
void EntityManager::tick(qreal dt, World *world)
{
    if (!world || m_entities.empty()) return;
    bool dirty = false;
    for (auto &e : m_entities) {
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过

        // 已落地：复探支撑格是否仍实体。失支撑 → 续落。
        if (e.resting) {
            const int supportY = qFloor(e.pos.y() - e.radius) - 1; // 实体底面下方那一格（= 支撑方块 cellY）
            if (supportY >= 0 && world->isSolid(cx, supportY, cz)) continue; // 仍实体 → 保持静止
            e.resting = false; // 支撑消失 → 续落（vy 已 0，从静止重新加速）
            dirty = true;
        }

        // 重力 + 下移（vy 向下为负）。
        e.vy -= kGravity * float(dt);
        if (e.vy < -kMaxFall) e.vy = -kMaxFall;
        const float newY = e.pos.y() + e.vy * float(dt);

        // 下移路径自顶向下扫实体所在列，找首个实体方块（防大 dt 穿过薄层；lessons「子步防穿墙」精神）。
        const int topCell = qFloor(e.pos.y()); // 当前中心所在格（一般为空气）
        int botCell = qFloor(newY);
        if (botCell > topCell) botCell = topCell; // 防浮点噪声致 botCell>topCell（vy≈0 时 newY 微高于 pos.y）
        int solidCellY = -1;
        for (int cy = topCell; cy >= botCell; --cy) {
            if (cy < 0) break; // 越界下方=空气（World 约定）→ 不视作地面，实体继续落
            if (world->isSolid(cx, cy, cz)) { solidCellY = cy; break; }
        }

        if (solidCellY >= 0) {
            // 落地：贴支撑方块顶面 + 静止偏移（底面 = solidCellY+1 = 支撑方块顶）。钳 newY 防穿越。
            const float restY = float(solidCellY + 1) + kRestOffset;
            if (newY <= restY || e.vy < 0.0f) {
                if (e.pos.y() != restY) { e.pos.setY(restY); dirty = true; }
                if (e.vy != 0.0f) { e.vy = 0.0f; dirty = true; }
                e.resting = true;
            }
        } else if (newY != e.pos.y()) {
            e.pos.setY(newY); // 自由下落（无命中）
            dirty = true;
        }
    }
    if (dirty) { ++m_revision; emit entitiesChanged(); }
}
