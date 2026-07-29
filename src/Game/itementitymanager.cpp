#include "itementitymanager.h"
#include "world.h" // t60 tick 只读 World::isSolid（向下依赖；PLAN §2 Entities→World 合规）

#include <QLoggingCategory>
#include <QtMath> // qFloor

namespace {
Q_LOGGING_CATEGORY(lcItem, "vo.item") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤，落 log 可见
}

ItemEntityManager::ItemEntityManager(QObject *parent) : QObject(parent)
{
    m_clock.start(); // t53：拾取延迟判定的墙钟起点（elapsed 单调递增，免 dt 耦合）
}

// 生成掉落实体：存格中心坐标 + id，bump 版本号发 entitiesChanged → QML Repeater 追加 delegate。
// spec「实体数量有上限（防溢出）」：达 kCap 跳过 + 告警（保留已有；最简防溢出策略）。
void ItemEntityManager::spawnItem(int x, int y, int z, int itemId)
{
    if (itemId <= 0) return; // air / 非法：不产出（PlayerController 仅在 drop=true 时发，已过滤）
    if (int(m_entities.size()) >= kCap) {
        qCWarning(lcItem) << "item entity cap reached (" << kCap << "); spawn skipped at" << x << y << z;
        return;
    }
    m_entities.push_back(ItemEntity{QVector3D(x + 0.5f, y + 0.5f, z + 0.5f), itemId, m_clock.elapsed()});
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcItem) << "spawned item entity id=" << itemId << "at" << x << y << z
                   << "(total" << m_entities.size() << ")";
}

QVector3D ItemEntityManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    return m_entities[size_t(i)].pos;
}

int ItemEntityManager::itemIdAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].itemId;
}

// 销毁第 i 个实体（t36 拾取消费）。erase-shift：其后元素前移、size--，保持位置 / 索引连续。
// bump revision 驱动 QML Repeater delegate 的 posAt/itemIdAt 绑定（触碰 revision）重算 →
// shift 后 delegate[k] 对齐新的 entity[k] 数据。count-- 同时让 Repeater 移除末位多余 delegate。
void ItemEntityManager::removeAt(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    m_entities.erase(m_entities.begin() + i);
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcItem) << "picked up item entity at index" << i << "(remaining" << m_entities.size() << ")";
}

// t53：第 i 个实体是否已过新生免拾取期（spawn 后 kPickupDelayMs）。
// 破块瞬间实体常在玩家近旁（如脚下方块中心距玩家中心 ~1.4 < kPickupDist 1.5）→ pickupScan 下一帧即收走，
// 玩家永远看不到实体（用户反馈「仍 auto-collect 入背包」的根因——非 finishMiningAt 残留 addStack，
// 而是 pickupScan 即时拾取的副作用）。加 0.5s 免拾窗让实体先可见再可拾（机制等价 MC block-break pickup
// delay）。越界 / 时钟未启 → true（保守可拾，防延迟机制误伤合法拾取 / 卡死）。
bool ItemEntityManager::isPickupReady(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return true;
    return (m_clock.elapsed() - m_entities[size_t(i)].spawnMs) >= kPickupDelayMs;
}

// t60 掉落物重力（每帧由 PlayerController::tick 调）。对每个实体：
//   1) 已 resting：复探支撑格（cellY = floor(pos.y) - 1，即静止中心下方那一格）仍实体 → 保持静止；
//      否则（下方被挖空）解除 resting 续落（防悬空；机制等价 MC 掉落物在支撑消失后重新下落）。
//   2) 未 resting：vy -= g*dt（钳 -kMaxFall），按 dy 下移；下移路径 [floor(newY), floor(pos.y)] 自顶向下
//      扫实体所在列首个实体方块 → 命中则贴其顶面（solidCellY+1+kRestOffset）停下、vy=0、resting=true。
//      越界（cy<0）查 world.isSolid 返 false（World 约定越界=空气）→ 不会误判「虚空地面」，实体继续落。
//   3) pos / resting 任一真变 → dirty=true，末尾统一 bump revision + emit entitiesChanged（驱动 QML
//      {revision; posAt(index)} 绑定重算；count 不变 → Repeater 不重建 delegate，动画连续）。
// 单帧最大下移 = kMaxFall*0.05 ≈ 3.9 格（dt 钳 50ms）→ 列扫 ≤4 格，cheap；≤200 实体全程 O(数百)。
void ItemEntityManager::tick(qreal dt, World *world)
{
    if (!world || m_entities.empty()) return;
    bool dirty = false;
    for (auto &e : m_entities) {
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过（防 isSolid 越界误判）

        // 已落地：复探支撑格是否仍实体。失支撑 → 解除 resting 续落（防挖空后悬空）。
        if (e.resting) {
            const int supportY = qFloor(e.pos.y()) - 1; // 静止中心下方那一格（= 支撑方块 cellY）
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
            // 落地：贴支撑方块顶面 + 静止偏移。钳 newY 防穿越（newY 可能已低于顶面）。
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
