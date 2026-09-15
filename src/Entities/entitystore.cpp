#include "entitystore.h"
#include "world.h" // tick 只读 World::blockAt/stateAt/isCollidable/supportTopYAt（向下依赖；PLAN §2 Entities→World 合规）

#include <QLoggingCategory>

#include <QtMath> // qFloor
#include <cmath>  // std::sqrt / std::exp / std::cos / std::sin / std::fabs
#include <algorithm> // std::min（合并 clamp）

namespace {
Q_LOGGING_CATEGORY(lcItem, "vo.item") // 模块化日志（PLAN §2-F）；类目名沿用 vo.item（log 过滤面零变化）
}

// R20.14 EntityStore 实现：模拟体自 src/Game/itementitymanager.cpp **逐行搬移**（非复制——
// Adapter 收敛为委托后本文件是唯一实现；「网格本体不回流」同款纪律由 r2014d 反探钉承担：
// itementitymanager.{h,cpp} 禁再现合并/物理/槽位机件）。搬移机械替换仅两类：
//   ① acquireSlot 内新增 EntityId 获配一行（验收①，其余逐位不动）；
//   ② notifyChanged 的 emit entitiesChanged() → notify 上行缝（if (m_notify) m_notify()），
//     批量收口（endBatch）与 clearAll 的直发语义逐位保留。
// 行为等价由矩阵 section18 r2014a 孪生腿（store 直驱 vs Adapter 面逐位一致）+
// t1027/t1032/t1039/t1041/t867/t889 既有掉落物腿族全绿常驻承担。

EntityStore::EntityStore()
{
    m_clock.start(); // 拾取延迟判定的墙钟起点（elapsed 单调递增，免 dt 耦合）
}

int EntityStore::acquireSlot(ItemEntity &&e)
{
    e.entityId = m_nextEntityId++; // R20.14 验收①：稳定 EntityId（自 1 单调，永不复用）
    int slot;
    if (!m_freeSlots.empty()) {
        slot = m_freeSlots.back();
        m_freeSlots.pop_back();
        m_entities[size_t(slot)] = std::move(e);
    } else {
        m_entities.push_back(std::move(e));
        slot = int(m_entities.size()) - 1;
    }
    ++m_liveCount;
    if (m_liveCount > m_liveHighWater) m_liveHighWater = m_liveCount;
    return slot;
}

void EntityStore::releaseSlot(int idx)
{
    if (idx < 0 || idx >= int(m_entities.size())) return;
    m_entities[size_t(idx)].alive = false;
    m_freeSlots.push_back(idx);
    --m_liveCount;
}

bool EntityStore::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].alive;
}

bool EntityStore::restingAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].resting;
}

QVector3D EntityStore::posAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    return m_entities[size_t(i)].pos;
}

int EntityStore::itemIdAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].itemId;
}

int EntityStore::countAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].count;
}

QVariantList EntityStore::enchantsAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return { 0, 0, 0, 0 };
    const ItemEntity &e = m_entities[size_t(i)];
    return { e.enchants[0], e.enchants[1], e.enchants[2], e.enchants[3] };
}

QString EntityStore::nameAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QString();
    return m_entities[size_t(i)].name;
}

int EntityStore::durabilityAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return -1;
    return m_entities[size_t(i)].durability;
}

quint32 EntityStore::entityIdAtSlot(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0; // 0 = 非实体域哨兵（对齐 event.h）
    return m_entities[size_t(i)].entityId;
}

// 变更收口单点：++revision + 快照重建（验收③「可见实体由 snapshot 决定」的沿语义——
//   任何变更沿后快照即新，消费方无需手动刷新）+ 按批态上行（非批即 sink，批内仅标脏）。
void EntityStore::notifyChanged()
{
    ++m_revision;
    rebuildVisibleSnapshot();
    if (m_batchDepth <= 0) {
        if (m_notify) m_notify(); // 非批（常态）：立即上行，行为同旧 emit
    } else {
        m_batchDirty = true; // 批内：仅标脏，由 endBatch 末尾 1 次收口
    }
}

void EntityStore::rebuildVisibleSnapshot()
{
    m_visible.revision = m_revision;
    m_visible.entries.clear();
    m_visible.entries.reserve(m_entities.size());
    for (int i = 0; i < int(m_entities.size()); ++i) {
        const ItemEntity &e = m_entities[size_t(i)];
        if (!e.alive) continue; // 空槽不入快照 = 可见集恰为活体集
        EntityStoreEntry v;
        v.slot = i;
        v.entityId = e.entityId;
        v.pos = e.pos;
        v.itemId = e.itemId;
        v.count = e.count;
        v.resting = e.resting;
        v.enchants[0] = e.enchants[0];
        v.enchants[1] = e.enchants[1];
        v.enchants[2] = e.enchants[2];
        v.enchants[3] = e.enchants[3];
        v.name = e.name;
        v.durability = e.durability;
        m_visible.entries.push_back(std::move(v));
    }
}

// beginBatch 头内 inline（++m_batchDepth）；endBatch 实现在此（收口面带返回值）。
bool EntityStore::endBatch()
{
    if (m_batchDepth <= 0) return false;
    if (--m_batchDepth == 0 && m_batchDirty) {
        m_batchDirty = false;
        if (m_notify) m_notify(); // 1 次收口 N 个累积变更（修爆炸 O(N²) 绑定风暴 → O(N)）
        return true;
    }
    return false;
}

void EntityStore::clearAll()
{
    for (size_t i = 0; i < m_entities.size(); ++i)
        if (m_entities[i].alive) releaseSlot(int(i));
    rebuildVisibleSnapshot(); // 重置沿同步快照（验收③）
    if (m_notify) m_notify(); // 重置语义：无条件通知（同旧 clearAll 直 emit 恒发，不经批）
}

bool EntityStore::isPickupReady(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return true;
    return (m_clock.elapsed() - m_entities[size_t(i)].spawnMs) >= kPickupDelayMs;
}

void EntityStore::deferWallClocks(qint64 ms)
{
    if (ms <= 0) return;
    for (auto &e : m_entities) {
        if (e.alive) e.spawnMs += ms;
    }
}

// 寿命时钟注入缝（见头注）：spawnMs 前移 = 墙钟老化（生产路径零调用，矩阵 despawn 腿专用）。
void EntityStore::ageLifetimeClock(qint64 ms)
{
    if (ms <= 0) return;
    for (auto &e : m_entities) {
        if (e.alive) e.spawnMs -= ms;
    }
}

// 生成掉落实体（spawnItem / spawnItemAt / spawnItemThrown 三入口同链：就近合并 → LRU
//   驱逐 → 获配槽位 + EntityId → 元数据写入 → 初速 → 收口）。语义逐行同旧（t490fix 合并 /
//   t320 LRU / rev2-C5 带名不合并双向守卫 / t590-t647 元数据保真），详注归档见
//   git 历史 src/Game/itementitymanager.cpp；本文件只留结构注。
void EntityStore::spawnItem(int x, int y, int z, int itemId, int count, const QVariantList &enchants, const QString &name, int durability)
{
    if (itemId <= 0) return; // air / 非法：不产出
    if (count < 1) count = 1;

    // t490fix 就近合并 + rev2-C5 带名（双向）不合并。
    {
        const int cap = BlockRegistry::maxStackSize(itemId);
        const bool namedDrop = !name.trimmed().isEmpty();
        if (cap > 1 && !namedDrop && !m_entities.empty()) {
            const QVector3D center(x + 0.5f, y + 0.5f, z + 0.5f);
            const float r2 = kMergeRadius * kMergeRadius;
            for (size_t i = 0; i < m_entities.size(); ++i) {
                ItemEntity &e = m_entities[i];
                if (!e.alive || e.itemId != itemId) continue;
                if (!e.name.trimmed().isEmpty()) continue; // rev2-C5：已有实体带名 → 不接收并入
                const QVector3D d = e.pos - center;
                if (d.x() * d.x() + d.y() * d.y() + d.z() * d.z() > r2) continue;
                if (e.count < cap) {
                    const int add = std::min(cap - e.count, count);
                    e.count += add;
                    count -= add;
                    notifyChanged();
                    qCInfo(lcItem) << "merged item entity id=" << itemId << "into slot" << int(i)
                                   << "count ->" << e.count << "(at" << x << y << z << ")";
                    if (count <= 0) return;
                }
                continue; // 批量合并优化：填满一个继续找下一个可合并（旧注释归档）
            }
        }
    }

    // t320 LRU 驱逐最老活体腾位。
    if (m_liveCount >= kCap) {
        int oldest = -1; qint64 oldestMs = 0;
        for (int i = 0; i < int(m_entities.size()); ++i) {
            if (!m_entities[size_t(i)].alive) continue;
            if (oldest < 0 || m_entities[size_t(i)].spawnMs < oldestMs) {
                oldest = i; oldestMs = m_entities[size_t(i)].spawnMs;
            }
        }
        if (oldest >= 0) {
            releaseSlot(oldest);
            qCWarning(lcItem) << "item entity cap reached (" << kCap << "); evicted oldest at slot" << oldest;
        }
    }
    const int slot = acquireSlot(ItemEntity{ QVector3D(x + 0.5f, y + 0.5f, z + 0.5f), itemId, count, m_clock.elapsed() });
    {
        ItemEntity &e = m_entities[size_t(slot)];
        for (int i = 0; i < 4; ++i)
            e.enchants[i] = (i < enchants.size()) ? enchants.at(i).toInt() : 0;
        e.name = name.trimmed();
        e.durability = durability;
    }
    // t468 初始水平弹出速度（确定性哈希：位置 + itemId → 同掉落可复现）。
    {
        ItemEntity &e = m_entities[size_t(slot)];
        quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u ^ quint32(z) * 83492791u ^ quint32(itemId) * 2654435761u;
        const float angle = float(h % 3600u) * (3.14159265358979f / 1800.0f);
        const float mag = kItemPopSpeed * (0.7f + 0.6f * float((h >> 11) % 100u) / 100.0f);
        e.vx = std::cos(angle) * mag;
        e.vz = std::sin(angle) * mag;
    }
    notifyChanged();
    qCInfo(lcItem) << "spawned item entity id=" << itemId << "count=" << count << "at" << x << y << z
                   << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

void EntityStore::spawnItemAt(const QVector3D &pos, int itemId, int count,
                              float dirX, float dirZ, float speed,
                              const QVariantList &enchants, const QString &name, int durability)
{
    if (itemId <= 0) return;
    if (count < 1) count = 1;

    // 就近合并（同 spawnItem 语义；rev2-C5 带名不合并）。
    {
        const int cap = BlockRegistry::maxStackSize(itemId);
        const bool namedDrop = !name.trimmed().isEmpty();
        if (cap > 1 && !namedDrop && !m_entities.empty()) {
            const float r2 = kMergeRadius * kMergeRadius;
            for (size_t i = 0; i < m_entities.size(); ++i) {
                ItemEntity &e = m_entities[i];
                if (!e.alive || e.itemId != itemId) continue;
                if (!e.name.trimmed().isEmpty()) continue;
                const QVector3D d = e.pos - pos;
                if (d.x() * d.x() + d.y() * d.y() + d.z() * d.z() > r2) continue;
                if (e.count < cap) {
                    const int add = std::min(cap - e.count, count);
                    e.count += add;
                    count -= add;
                    notifyChanged();
                    qCInfo(lcItem) << "merged item entity id=" << itemId << "into slot" << int(i)
                                   << "count ->" << e.count << "(dispenser pop at" << pos << ")";
                    if (count <= 0) return;
                }
                continue;
            }
        }
    }

    // LRU 驱逐（同 spawnItem）。
    if (m_liveCount >= kCap) {
        int oldest = -1; qint64 oldestMs = 0;
        for (int i = 0; i < int(m_entities.size()); ++i) {
            if (!m_entities[size_t(i)].alive) continue;
            if (oldest < 0 || m_entities[size_t(i)].spawnMs < oldestMs) {
                oldest = i; oldestMs = m_entities[size_t(i)].spawnMs;
            }
        }
        if (oldest >= 0) {
            releaseSlot(oldest);
            qCWarning(lcItem) << "item entity cap reached (" << kCap << "); evicted oldest at slot" << oldest;
        }
    }
    const int slot = acquireSlot(ItemEntity{ pos, itemId, count, m_clock.elapsed() });
    {
        ItemEntity &e = m_entities[size_t(slot)];
        for (int i = 0; i < 4; ++i)
            e.enchants[i] = (i < enchants.size()) ? enchants.at(i).toInt() : 0;
        e.name = name.trimmed();
        e.durability = durability;
    }
    // 定向弹出初速（t608 排出口口径；退化全 0 → 原地落地）。
    {
        ItemEntity &e = m_entities[size_t(slot)];
        const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
        if (len > 1e-4f && speed > 0.0f) {
            e.vx = (dirX / len) * speed;
            e.vz = (dirZ / len) * speed;
        }
    }
    notifyChanged();
    qCInfo(lcItem) << "spawned item entity (dispenser pop) id=" << itemId << "count=" << count << "at" << pos
                   << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

void EntityStore::spawnItemThrown(const QVector3D &pos, int itemId, int count,
                                  float dirX, float dirY, float dirZ, float speed,
                                  const QVariantList &enchants, const QString &name, int durability)
{
    if (itemId <= 0) return;
    if (count < 1) count = 1;

    // 就近合并（同 spawnItemAt 语义；rev2-C5 带名不合并）。
    {
        const int cap = BlockRegistry::maxStackSize(itemId);
        const bool namedDrop = !name.trimmed().isEmpty();
        if (cap > 1 && !namedDrop && !m_entities.empty()) {
            const float r2 = kMergeRadius * kMergeRadius;
            for (size_t i = 0; i < m_entities.size(); ++i) {
                ItemEntity &e = m_entities[i];
                if (!e.alive || e.itemId != itemId) continue;
                if (!e.name.trimmed().isEmpty()) continue;
                const QVector3D d = e.pos - pos;
                if (d.x() * d.x() + d.y() * d.y() + d.z() * d.z() > r2) continue;
                if (e.count < cap) {
                    const int add = std::min(cap - e.count, count);
                    e.count += add;
                    count -= add;
                    notifyChanged();
                    qCInfo(lcItem) << "merged item entity id=" << itemId << "into slot" << int(i)
                                   << "count ->" << e.count << "(thrown at" << pos << ")";
                    if (count <= 0) return;
                }
                continue;
            }
        }
    }

    // LRU 驱逐（同 spawnItem）。
    if (m_liveCount >= kCap) {
        int oldest = -1; qint64 oldestMs = 0;
        for (int i = 0; i < int(m_entities.size()); ++i) {
            if (!m_entities[size_t(i)].alive) continue;
            if (oldest < 0 || m_entities[size_t(i)].spawnMs < oldestMs) {
                oldest = i; oldestMs = m_entities[size_t(i)].spawnMs;
            }
        }
        if (oldest >= 0) {
            releaseSlot(oldest);
            qCWarning(lcItem) << "item entity cap reached (" << kCap << "); evicted oldest at slot" << oldest;
        }
    }
    const int slot = acquireSlot(ItemEntity{ pos, itemId, count, m_clock.elapsed() });
    {
        ItemEntity &e = m_entities[size_t(slot)];
        for (int i = 0; i < 4; ++i)
            e.enchants[i] = (i < enchants.size()) ? enchants.at(i).toInt() : 0;
        e.name = name.trimmed();
        e.durability = durability;
    }
    // 定向投掷初速（t609 三维；退化全 0 → 原地落下）。
    {
        ItemEntity &e = m_entities[size_t(slot)];
        const float len = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
        if (len > 1e-4f && speed > 0.0f) {
            e.vx = (dirX / len) * speed;
            e.vy = (dirY / len) * speed;
            e.vz = (dirZ / len) * speed;
        }
    }
    notifyChanged();
    qCInfo(lcItem) << "spawned item entity (thrown) id=" << itemId << "count=" << count << "at" << pos
                   << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

void EntityStore::setCountAt(int i, int n)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    ItemEntity &e = m_entities[size_t(i)];
    if (!e.alive) return;
    if (e.count == n) return;
    if (n <= 0) {
        releaseSlot(i);
        qCInfo(lcItem) << "item entity at index" << i << "consumed fully (live" << m_liveCount << "slots" << m_entities.size() << ")";
    } else {
        e.count = n;
        qCInfo(lcItem) << "item entity at index" << i << "count ->" << n
                       << "(partially picked; remaining in world)";
    }
    notifyChanged();
}

void EntityStore::removeAt(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    if (!m_entities[size_t(i)].alive) return;
    releaseSlot(i);
    notifyChanged();
    qCInfo(lcItem) << "picked up item entity at index" << i
                   << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// tick：t60 重力 / t271 浮水随流 / t343-t445 焚毁 / t867 薄支撑 / t320-t889 寿命与暂停顺延。
//   体逐行同旧（详注归档 git 历史 src/Game/itementitymanager.cpp）；world null → 仅寿命驱逐后早退。
void EntityStore::tick(qreal dt, World *world)
{
    // 寿命到期驱逐先于物理（t889：硬暂停期 deferWallClocks 顺延 → 暂停期不老化）。
    despawnExpired();
    if (!world || m_entities.empty()) return;
    bool dirty = false;
    for (auto &e : m_entities) {
        if (!e.alive) continue;
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue;

        const int cy = qFloor(e.pos.y());
        // t343 岩浆焚毁（中心格 == Lava → 瞬灭释放槽位）。
        if (cy >= 0 && world->blockAt(cx, cy, cz) == BlockRegistry::Lava) {
            const int idx = int(&e - &m_entities.front());
            releaseSlot(idx);
            dirty = true;
            continue;
        }
        // t844 入火瞬灭（语义边界：燃烧方块格不烧掉落物，只认立地火格——详注归档）。
        if (cy >= 0 && world->blockAt(cx, cy, cz) == BlockRegistry::Fire) {
            const int idx = int(&e - &m_entities.front());
            releaseSlot(idx);
            dirty = true;
            continue;
        }
        // t445 ⑤ 仙人掌摧毁（中心下方一格 == Cactus）。
        if (cy - 1 >= 0 && world->blockAt(cx, cy - 1, cz) == BlockRegistry::Cactus) {
            const int idx = int(&e - &m_entities.front());
            releaseSlot(idx);
            dirty = true;
            continue;
        }
        const bool inWater = (cy >= 0 && world->blockAt(cx, cy, cz) == BlockRegistry::Water);
        // t271 瀑布：水格下方为空气 = 水柱下落 → 不上浮。
        const bool waterfall = inWater
            && (cy - 1 < 0 || world->blockAt(cx, cy - 1, cz) == BlockRegistry::Air);
        bool groundedRest = false;

        if (inWater) {
            if (e.resting) { e.resting = false; dirty = true; }

            // (a) 浮水面（非瀑布）：恒速上浮到 t892 液面（waterSurfaceFrac 单一权威）。
            if (!waterfall) {
                int surfCellY = cy;
                for (int i = 0; i < 128; ++i) {
                    if (world->blockAt(cx, surfCellY + 1, cz) != BlockRegistry::Water) break;
                    ++surfCellY;
                }
                const float restY = float(surfCellY)
                    + BlockRegistry::waterSurfaceFrac(world->stateAt(cx, surfCellY, cz))
                    - kItemFloatOffset;
                if (e.pos.y() < restY - 1e-3f) {
                    e.vy = kItemRiseSpeed;
                    float newY = e.pos.y() + e.vy * float(dt);
                    if (newY > restY) newY = restY;
                    e.pos.setY(newY);
                    if (newY >= restY - 1e-3f) e.vy = 0.0f;
                    dirty = true;
                } else if (e.vy != 0.0f || e.pos.y() != restY) {
                    e.vy = 0.0f;
                    e.pos.setY(restY);
                    dirty = true;
                }
            }

            // (b) 随流移动（state 梯度离源方向 + per-axis isCollidable 试探）。
            const quint8 cellState = world->stateAt(cx, cy, cz);
            if (cellState > 0) {
                float gx = 0.0f, gz = 0.0f;
                constexpr int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (const auto &d : dirs) {
                    const int nx = cx + d[0], nz = cz + d[1];
                    if (world->blockAt(nx, cy, nz) == BlockRegistry::Water) {
                        const quint8 ns = world->stateAt(nx, cy, nz);
                        if (ns < cellState) {
                            gx -= float(d[0]) * float(cellState - ns);
                            gz -= float(d[1]) * float(cellState - ns);
                        }
                    }
                }
                const float glen = std::sqrt(gx * gx + gz * gz);
                if (glen > 1e-4f) {
                    const float dvx = (gx / glen) * kItemFlowSpeed * float(dt);
                    const float dvz = (gz / glen) * kItemFlowSpeed * float(dt);
                    float px = e.pos.x();
                    float pz = e.pos.z();
                    const float tryX = px + dvx;
                    if (!world->isCollidable(qFloor(tryX), cy, cz)) px = tryX;
                    const float tryZ = pz + dvz;
                    if (!world->isCollidable(qFloor(px), cy, qFloor(tryZ))) pz = tryZ;
                    if (px != e.pos.x() || pz != e.pos.z()) {
                        e.pos.setX(px); e.pos.setZ(pz); dirty = true;
                    }
                }
            }

            if (!waterfall) continue; // 浮水已完整处理 → 跳过重力分支
        }

        // === 重力分支（空气 + 瀑布；t867 起支撑真顶单一权威）===
        if (e.resting) {
            // 两格窗复探支撑（薄支撑中心落进支撑格内；变矮下贴、消失续落）。
            const int feetCell = qFloor(e.pos.y());
            float snapTop = -1.0f;
            for (int scy = feetCell; scy >= feetCell - 1; --scy) {
                if (scy < 0) break;
                const float top = world->supportTopYAt(cx, scy, cz);
                if (top >= 0.0f && top <= e.pos.y() - kRestOffset + 0.05f) { snapTop = top; break; }
            }
            if (snapTop >= 0.0f) {
                const float restY = snapTop + kRestOffset;
                if (restY < e.pos.y() - 1e-3f) { e.pos.setY(restY); dirty = true; }
                groundedRest = true; // review26 #2：残余水平速度仍走积分 + 摩擦
            } else {
                e.resting = false;
                dirty = true;
            }
        }

        if (!groundedRest) {
            e.vy -= kGravity * float(dt);
            if (e.vy < -kMaxFall) e.vy = -kMaxFall;
            const float newY = e.pos.y() + e.vy * float(dt);

            // 下移路径自顶向下扫列首个支撑真顶（水/火穿透；岩浆落格由头部焚毁接管）。
            const int topCell = qFloor(e.pos.y());
            int botCell = qFloor(newY);
            if (botCell > topCell) botCell = topCell;
            float supportTop = -1.0f;
            for (int scy = topCell; scy >= botCell; --scy) {
                if (scy < 0) break;
                const float top = world->supportTopYAt(cx, scy, cz);
                if (top >= 0.0f) { supportTop = top; break; }
            }

            if (supportTop >= 0.0f) {
                const float restY = supportTop + kRestOffset;
                if (newY <= restY || e.vy < 0.0f) {
                    if (e.pos.y() != restY) { e.pos.setY(restY); dirty = true; }
                    if (e.vy != 0.0f) { e.vy = 0.0f; dirty = true; }
                    e.resting = true;
                }
            } else if (newY != e.pos.y()) {
                e.pos.setY(newY);
                dirty = true;
            }
        } // !groundedRest

        // t468 水平积分 + 支撑面摩擦（冰面长滑 / 常规快停；薄支撑脚位格豁免——t867 同源）。
        if (!inWater && (std::fabs(e.vx) > 1e-4f || std::fabs(e.vz) > 1e-4f)) {
            const int hcy = qFloor(e.pos.y());
            const float hFeetY = e.pos.y() - kRestOffset;
            auto hBlocked = [&](int bx, int bz) {
                if (!world->isCollidable(bx, hcy, bz)) return false;
                const float top = world->supportTopYAt(bx, hcy, bz);
                return !(top >= 0.0f && top <= hFeetY + 0.05f);
            };
            float px = e.pos.x(), pz = e.pos.z();
            const float tryX = px + e.vx * float(dt);
            if (!hBlocked(qFloor(tryX), cz)) px = tryX;
            const float tryZ = pz + e.vz * float(dt);
            if (!hBlocked(qFloor(px), qFloor(tryZ))) pz = tryZ;
            if (px != e.pos.x() || pz != e.pos.z()) { e.pos.setX(px); e.pos.setZ(pz); dirty = true; }
            if (e.resting) {
                const int fc = qFloor(e.pos.y());
                int supportY = fc - 1;
                if (fc >= 0
                    && world->supportTopYAt(qFloor(e.pos.x()), fc, qFloor(e.pos.z())) >= 0.0f)
                    supportY = fc;
                const quint8 sb = (supportY >= 0) ? world->blockAt(qFloor(e.pos.x()), supportY, qFloor(e.pos.z()))
                                                  : quint8(BlockRegistry::Air);
                const float fric = BlockRegistry::isIce(sb) ? kItemIceFriction : kItemGroundFriction;
                const float decay = std::exp(-fric * float(dt));
                e.vx *= decay;
                e.vz *= decay;
                if (std::fabs(e.vx) < 0.05f) e.vx = 0.0f;
                if (std::fabs(e.vz) < 0.05f) e.vz = 0.0f;
            }
        }
    }
    if (dirty) notifyChanged();
}

// 自然寿命驱逐（每帧 tick 起始调；age > kDespawnMs → releaseSlot）。
void EntityStore::despawnExpired()
{
    if (m_entities.empty()) return;
    const qint64 now = m_clock.elapsed();
    bool dirty = false;
    for (int i = 0; i < int(m_entities.size()); ++i) {
        if (!m_entities[size_t(i)].alive) continue;
        if (now - m_entities[size_t(i)].spawnMs > kDespawnMs) {
            releaseSlot(i);
            dirty = true;
            qCInfo(lcItem) << "item entity at index" << i << "despawned after" << kDespawnMs
                           << "ms (lifetime; live" << m_liveCount << "slots" << m_entities.size() << ")";
        }
    }
    if (dirty) notifyChanged();
}
