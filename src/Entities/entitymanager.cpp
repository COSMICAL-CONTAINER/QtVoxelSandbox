#include "entitymanager.h"
#include "world.h" // tick / resolvePlayerPush 只读 World::isSolid（向下依赖；PLAN §2 Entities→World 合规）

#include <QLoggingCategory>
#include <QtMath>    // qFloor
#include <algorithm> // std::clamp, std::min, std::move
#include <cmath>     // std::sqrt

namespace {
Q_LOGGING_CATEGORY(lcEnt, "vo.entity") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤，落 log 可见

// mob AABB footprint 全格扫（t104；仿 player aabbHitsSolid，playercontroller.cpp:761）。
// 给定实体立方体中心 (cx,cy,cz) 与半径 r，扫其 AABB [cx−r,cx+r]×[cy−r,cy+r]×[cz−r,cz+r]「严格覆盖」
// 的所有格子，任一实体方块 → true。「严格重叠」取样（ceil(max)−1 排除仅贴面的方块 → 防卡缝 / 不误判
// 正下方支撑格）。取代 resolvePlayerPush 旧版「只查 mob 中心格」的单格检查：斜推角落时 mob 中心可能
// 仍在空气格但 3/4 身体已入墙 → 旧版不撤回 → 下帧中心才入墙 → 撤回 → 再下帧又被推入 → 反复跳变 =
// jitter（用户感知为 scale 闪烁 + revision 每帧 bump）。全格扫使「mob AABB 任一部分触墙」即撤回 →
// mob 永不入墙 → 无跳变。
bool mobAabbHitsSolid(World *world, float cx, float cy, float cz, float r)
{
    if (!world) return false;
    const float minx = cx - r, maxx = cx + r;
    const float miny = cy - r, maxy = cy + r;
    const float minz = cz - r, maxz = cz + r;
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int y0 = int(std::floor(miny)), y1 = int(std::ceil(maxy)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (world->isSolid(x, y, z)) return true;
    return false;
}
} // namespace

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

// t117 生成下落方块实体：存格中心 + blockId + pushable=false + kind=FallingBlock。bump revision →
// QML Repeater 追加 delegate（BlockCube 贴图渲染，复用地形图集）。达 kCap 跳过 + 告警（防溢出）。
// 重力 tick 下落，着地时 world->setBlockFromEntity 放置 blockId 并移除（链式塌落由 caller 控制）。
void EntityManager::spawnFallingBlock(int x, int y, int z, int blockId)
{
    if (int(m_entities.size()) >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); falling block spawn skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    e.radius = 0.5f;
    e.pushable = false; // 下落方块不被玩家推动（同掉落物变体）
    e.kind = FallingBlock;
    e.blockId = blockId;
    m_entities.push_back(std::move(e));
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcEnt) << "spawned falling block id=" << blockId << "at" << x << y << z
                  << "(total" << m_entities.size() << ")";
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

// t117：FallingBlock 携带的方块 id（着地 setBlock 用；呈现层 BlockCube.blockId 贴图渲染）。
int EntityManager::blockIdAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].blockId;
}

// 玩家推动解析：对每个 pushable 实体做「玩家 AABB（XZ 矩形）vs 实体圆（XZ，半径=entity.radius）」
// 穿透求解（机制等价 MC 实体碰撞推开：玩家位移解析后传给实体）。
//   1) 垂直区间重叠判定：实体立方体 [pos.y−r, pos.y+r] 与玩家 AABB [feet.y, feet.y+height] 必须重叠
//      才推动（玩家从头顶跳过 / 跨层时不应误推）。
//   2) XZ 穿透：AABB 最近点 cx/cz = clamp(entity 中心, [px−halfW, px+halfW])；d = entity − 最近点。
//      dist < r → 穿透 push = r − dist，沿 d/dist 推出；d≈0（中心在 AABB 内）→ 沿最近面推出（min 四向）。
//   3) 世界碰撞钳制（t104：mob AABB footprint 全格扫，仿 player aabbHitsSolid）：推动后扫 mob 立方体
//      AABB 覆盖的所有格子（非旧版「只查中心格」），任一实体方块 → 撤回该轴推动。X/Z 两轴独立判定
//      （斜推各自检查），保证实体贴墙滑动不穿入；全格扫消除「中心在空气但 3/4 入墙」的 jitter。
//   4) 被推动后按「新 XZ 位置下方一格 isSolid」决定是否解除 resting（t115；仿 player hasGroundBelowAt）：
//      新位置下方仍有支撑 → 保持 resting（平地推动不下沉）；下方变空气（推下阶梯 / 推离支撑面）才
//      resting=false 让重力复探。任一 pos 真变 → dirty，末尾统一 bump revision + emit（驱动 QML
//      {revision; posAt} 绑定重算）。
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

        // 世界碰撞钳制（t104：mob AABB footprint 全格扫，仿 player aabbHitsSolid）：扫 mob 立方体 AABB
        // 覆盖的所有格子（非旧版「只查中心格」），任一实体方块 → 撤回该轴推动（防穿墙）。X/Z 两轴独立
        // 判定，Z 轴参照可能已撤回的 newX（两轴独立但顺序敏感）。旧版单格检查在斜推角落时 mob 中心可能
        // 仍在空气 → 不撤回 → 下帧中心入墙才撤回 → 反复跳变 = jitter；全格扫使任一部分触墙即撤回 → 消除。
        if (world) {
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), r)) newX = e.pos.x();
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, r))     newZ = e.pos.z();
        }

        if (newX != e.pos.x() || newZ != e.pos.z()) {
            e.pos.setX(newX);
            e.pos.setZ(newZ);
            // t115：旧版无条件 e.resting=false → 平地推动后 tick 重力下沉几帧再弹回 restY（下移扫描
            //   只覆盖实体中心高度的空气格、够不到下方支撑格 → 误判无命中 → 自由下落 → 几帧后扫到支撑
            //   才弹回）→ 帧帧 Y 抖（用户感知为缩小 / scale 闪烁）。改为按新 XZ 位置下方一格 isSolid
            //   判定（仿 player hasGroundBelowAt）：新位置仍有支撑 → 保持 resting → 下帧 tick 复探支撑
            //   格仍实体 → 跳过重力 → pos.y 不变 → 无抖；推下阶梯 / 推离支撑面时下方变空气 → 解除 →
            //   重力自然落（防悬空）。支撑判定与 tick 的 resting 复探同公式（同列 floor(pos.y−r)−1）→
            //   保证「此处判保留」必与「tick 下帧判保留」一致 → 永不因二者分歧再抖。
            if (world) {
                const int ncx = qFloor(newX);
                const int ncz = qFloor(newZ);
                const int supportY = qFloor(e.pos.y() - r) - 1; // 实体底面下方一格（与 tick 复探同公式）
                if (supportY < 0 || !world->isSolid(ncx, supportY, ncz))
                    e.resting = false; // 新位置失支撑 → 解除静止让重力复探（推下阶梯 / 推离支撑面）
            } else {
                e.resting = false; // 无世界可查 → 保守解除（world=null 时 tick 早 return，不影响）
            }
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
//
// t117 FallingBlock 分支：无 resting 态（落到底即转为方块 + 移除）。重力 + 列扫同 Mob/Item，命中固体时
//   inline 调 world->setBlockFromEntity(cx, solidCellY+1, cz, blockId) 放置并标记移除。**inline 放置**使
//   同 tick 后续 FallingBlock 的列扫能见到此新固体 → 同柱多块依次堆叠到不同格（不撞同一格）。着地格
//   solidCellY+1 由「自顶向下首个固体」扫描保证为空气。移除用索引收集 + 循环后逆序 erase（保索引有效）。
void EntityManager::tick(qreal dt, World *world)
{
    if (!world || m_entities.empty()) return;
    bool dirty = false;
    std::vector<int> toRemove; // t117：着地 / 跌出底部的 FallingBlock 索引（循环后逆序 erase）

    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        Entity &e = m_entities[size_t(idx)];
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过

        // t117 FallingBlock：重力 + 着地放置 + 移除（无 resting 态；落到底即转为方块）。
        if (e.kind == FallingBlock) {
            e.vy -= kGravity * float(dt);
            if (e.vy < -kMaxFall) e.vy = -kMaxFall;
            const float newY = e.pos.y() + e.vy * float(dt);
            const int topCell = qFloor(e.pos.y());
            int botCell = qFloor(newY);
            if (botCell > topCell) botCell = topCell; // 防浮点噪声致 botCell>topCell
            int solidCellY = -1;
            for (int cy = topCell; cy >= botCell; --cy) {
                if (cy < 0) break; // 越界下方=空气 → 不视作地面
                if (world->isSolid(cx, cy, cz)) { solidCellY = cy; break; }
            }
            if (solidCellY >= 0) {
                // 着地：在支撑方块上方一格放置 blockId + 标记移除。
                world->setBlockFromEntity(cx, solidCellY + 1, cz, quint8(e.blockId));
                toRemove.push_back(idx);
                dirty = true;
            } else if (newY <= 0.0f) {
                // 全列无固体且已跌出世界底部 → 移除（防永久下落；正常世界 y=0 有石头层不触发）。
                toRemove.push_back(idx);
                dirty = true;
            } else if (newY != e.pos.y()) {
                e.pos.setY(newY); // 继续自由下落
                dirty = true;
            }
            continue; // 不走 Mob/Item 的 resting / 落地静止逻辑
        }

        // --- Mob/Item：原有 resting + 重力 + 落地静止逻辑 ---
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
        const float mobNewY = e.pos.y() + e.vy * float(dt);

        // 下移路径自顶向下扫实体所在列，找首个实体方块（防大 dt 穿过薄层；lessons「子步防穿墙」精神）。
        const int mobTopCell = qFloor(e.pos.y()); // 当前中心所在格（一般为空气）
        int mobBotCell = qFloor(mobNewY);
        if (mobBotCell > mobTopCell) mobBotCell = mobTopCell; // 防浮点噪声
        int mobSolidY = -1;
        for (int cy = mobTopCell; cy >= mobBotCell; --cy) {
            if (cy < 0) break; // 越界下方=空气（World 约定）→ 不视作地面，实体继续落
            if (world->isSolid(cx, cy, cz)) { mobSolidY = cy; break; }
        }

        if (mobSolidY >= 0) {
            // 落地：贴支撑方块顶面 + 静止偏移（底面 = mobSolidY+1 = 支撑方块顶）。钳 mobNewY 防穿越。
            const float restY = float(mobSolidY + 1) + kRestOffset;
            if (mobNewY <= restY || e.vy < 0.0f) {
                if (e.pos.y() != restY) { e.pos.setY(restY); dirty = true; }
                if (e.vy != 0.0f) { e.vy = 0.0f; dirty = true; }
                e.resting = true;
            }
        } else if (mobNewY != e.pos.y()) {
            e.pos.setY(mobNewY); // 自由下落（无命中）
            dirty = true;
        }
    }

    // t117：逆序 erase 已着地 / 跌出的 FallingBlock（逆序保未处理索引有效）。
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        m_entities.erase(m_entities.begin() + *it);

    if (dirty || !toRemove.empty()) { ++m_revision; emit entitiesChanged(); }
}
