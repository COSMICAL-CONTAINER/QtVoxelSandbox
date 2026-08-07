#include "xporbmanager.h"

#include <QLoggingCategory>
#include <cmath> // std::sqrt

namespace {
Q_LOGGING_CATEGORY(lcXp, "vo.xp") // 模块化日志（PLAN §2-F）
}

XpOrbManager::XpOrbManager(QObject *parent) : QObject(parent)
{
    m_clock.start(); // 寿命判定的墙钟起点（elapsed 单调递增）
}

// 生成经验球（见 .h 头注；slot-reuse + LRU 驱逐达 cap）。
void XpOrbManager::spawnOrb(int x, int y, int z, int amount)
{
    if (amount <= 0) return; // 空球无意义（caller 应已过滤；双保险）
    if (m_liveCount >= kCap) {
        // LRU 驱逐最老活体腾位（同 ItemEntityManager spawnItem 模式）。
        int oldest = -1; qint64 oldestMs = 0;
        for (int i = 0; i < int(m_orbs.size()); ++i) {
            if (!m_orbs[size_t(i)].alive) continue;
            if (oldest < 0 || m_orbs[size_t(i)].spawnMs < oldestMs) {
                oldest = i; oldestMs = m_orbs[size_t(i)].spawnMs;
            }
        }
        if (oldest >= 0) {
            releaseSlot(oldest);
            qCWarning(lcXp) << "xp orb cap reached (" << kCap << "); evicted oldest at slot" << oldest;
        }
    }
    acquireSlot(Orb{QVector3D(x + 0.5f, y + 0.5f, z + 0.5f), amount, m_clock.elapsed()});
    notifyChanged();
    qCInfo(lcXp) << "spawned xp orb amount=" << amount << "at" << x << y << z
                 << "(live" << m_liveCount << "slots" << m_orbs.size() << ")";
}

bool XpOrbManager::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_orbs.size())) return false;
    return m_orbs[size_t(i)].alive;
}

QVector3D XpOrbManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_orbs.size())) return QVector3D();
    return m_orbs[size_t(i)].pos;
}

int XpOrbManager::amountAt(int i) const
{
    if (i < 0 || i >= int(m_orbs.size())) return 0;
    return m_orbs[size_t(i)].amount;
}

// 磁吸 + 拾取（见 .h 头注）。无 World 依赖：经验球是纯磁吸实体，只向玩家中心飞。
// 单帧最大位移 = kMaxSpeed*0.05 ≈ 0.45 格（dt 钳 50ms）→ 不会穿玩家；拾取判据用 3D 距离。
void XpOrbManager::tick(qreal dt, const QVector3D &playerCenter)
{
    // 寿命到期驱逐先于磁吸（独立于玩家位 —— 菜单 / 暂停时球照常老化消失）。
    despawnExpired();
    if (m_orbs.empty()) return;
    bool dirty = false;
    for (int i = 0; i < int(m_orbs.size()); ++i) {
        if (!m_orbs[size_t(i)].alive) continue; // 跳过已释放空槽
        Orb &o = m_orbs[size_t(i)];
        const QVector3D toPlayer = playerCenter - o.pos;
        const float dist = std::sqrt(toPlayer.lengthSquared());

        // 拾取：进吸附半径即吸收（释放槽位 + emit xpPickedUp 让呈现层路由 PlayerState.addXp）。
        if (dist < kPickupRange) {
            const int amt = o.amount;
            releaseSlot(i);
            dirty = true;
            emit xpPickedUp(amt); // 语义事件（呈现层路由 PlayerState + 拾取音）
            qCInfo(lcXp) << "xp orb at index" << i << "picked up amount=" << amt
                         << "(live" << m_liveCount << "slots" << m_orbs.size() << ")";
            continue;
        }
        // 磁吸：在磁吸半径内朝玩家加速飞（越近越快，机制等价 MC 经验球磁吸）。
        if (dist < kMagnetRange && dist > 1e-3f) {
            const QVector3D dir = toPlayer / dist; // 单位方向
            // t∈[0,1]：远端（dist=kMagnetRange）=0 → kMinSpeed；近端（dist→0）=1 → kMaxSpeed。
            const float t = 1.0f - (dist / kMagnetRange);
            const float speed = kMinSpeed + t * (kMaxSpeed - kMinSpeed);
            o.pos += dir * speed * float(dt);
            dirty = true;
        }
        // 否则（dist >= kMagnetRange）：静止悬浮，呈现层自发上下浮动动画（不在此处理）。
    }
    if (dirty) notifyChanged();
}

// 寿命到期驱逐（见头注释）。每帧 tick 起始调，先于磁吸。
void XpOrbManager::despawnExpired()
{
    if (m_orbs.empty()) return;
    const qint64 now = m_clock.elapsed();
    bool dirty = false;
    for (int i = 0; i < int(m_orbs.size()); ++i) {
        if (!m_orbs[size_t(i)].alive) continue;
        if (now - m_orbs[size_t(i)].spawnMs > kDespawnMs) {
            releaseSlot(i);
            dirty = true;
            qCInfo(lcXp) << "xp orb at index" << i << "despawned after" << kDespawnMs
                         << "ms (lifetime; live" << m_liveCount << "slots" << m_orbs.size() << ")";
        }
    }
    if (dirty) notifyChanged();
}
