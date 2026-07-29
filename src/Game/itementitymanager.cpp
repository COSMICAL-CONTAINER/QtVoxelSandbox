#include "itementitymanager.h"

#include <QLoggingCategory>

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
