#include "itementitymanager.h"
#include "world.h" // t60/t271 tick 只读 World::blockAt/stateAt/isSolid/isCollidable（向下依赖；PLAN §2 Entities→World 合规）

#include <QLoggingCategory>
#include <QtMath> // qFloor
#include <cmath>  // t271 std::sqrt（流水梯度归一化）

namespace {
Q_LOGGING_CATEGORY(lcItem, "vo.item") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤，落 log 可见
}

ItemEntityManager::ItemEntityManager(QObject *parent) : QObject(parent)
{
    m_clock.start(); // t53：拾取延迟判定的墙钟起点（elapsed 单调递增，免 dt 耦合）
}

// 生成掉落实体：存格中心坐标 + id + count，bump 版本号发 entitiesChanged → QML Repeater 追加 delegate。
// spec「实体数量有上限（防溢出）」：达 kCap 跳过 + 告警（保留已有；最简防溢出策略）。
// t64：count 字段支持整栈丢弃为 1 实体（如 4 木棒丢出仍 1 实体 count=4）；count<=0 视作 1。
void ItemEntityManager::spawnItem(int x, int y, int z, int itemId, int count)
{
    if (itemId <= 0) return; // air / 非法：不产出（PlayerController 仅在 drop=true 时发，已过滤）
    if (int(m_entities.size()) >= kCap) {
        qCWarning(lcItem) << "item entity cap reached (" << kCap << "); spawn skipped at" << x << y << z;
        return;
    }
    if (count < 1) count = 1; // 缺省 / 非法 → 单件（与历史调用兼容）
    m_entities.push_back(ItemEntity{QVector3D(x + 0.5f, y + 0.5f, z + 0.5f), itemId, count, m_clock.elapsed()});
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcItem) << "spawned item entity id=" << itemId << "count=" << count << "at" << x << y << z
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

// t64：实体携带数量。呈现层据 count>1 显数量数字；PlayerController 拾取按它入背包。
int ItemEntityManager::countAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].count;
}

// t64：拾取装不下时把余数回写、保留 entity（dropHeldCursor 整栈丢弃后部分拾取的回退路径）。
// n<=0 销毁该实体（余数为 0 = 全拾走 → removeAt 语义）。bump revision 驱动 QML 数量绑定重算。
void ItemEntityManager::setCountAt(int i, int n)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    ItemEntity &e = m_entities[size_t(i)];
    if (e.count == n) return;
    if (n <= 0) {
        // 余数为 0 = 全拾走 → 销毁实体（同 removeAt 路径，但走 setCountAt(0) 调用方语义统一）。
        m_entities.erase(m_entities.begin() + i);
        qCInfo(lcItem) << "item entity at index" << i << "consumed fully (remaining" << m_entities.size() << ")";
    } else {
        e.count = n;
        qCInfo(lcItem) << "item entity at index" << i << "count ->" << n
                       << "(partially picked; remaining in world)";
    }
    ++m_revision;
    emit entitiesChanged();
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

// t60 掉落物重力 / t271 水冲走掉落物（每帧由 PlayerController::tick 调）。对每个实体先判中心格是否
//   为 Water，分流「浮水 + 随流」与「空气重力」两条路径。详见 .h 头注（分层 / 机制 / 关键修正）。
// 单帧最大下移 = kMaxFall*0.05 ≈ 3.9 格（dt 钳 50ms）→ 列扫 ≤4 格，cheap；≤200 实体全程 O(数百)。
void ItemEntityManager::tick(qreal dt, World *world)
{
    if (!world || m_entities.empty()) return;
    bool dirty = false;
    for (auto &e : m_entities) {
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过（防越界误判）

        const int cy = qFloor(e.pos.y());
        const bool inWater = (cy >= 0 && world->blockAt(cx, cy, cz) == BlockRegistry::Water);
        // t271 瀑布：水格下方为空气 = 水柱下落 → 不上浮（随水柱下沉，落入下方水池后转浮水）。
        const bool waterfall = inWater
            && (cy - 1 < 0 || world->blockAt(cx, cy - 1, cz) == BlockRegistry::Air);

        if (inWater) {
            // 水中 → 非着地（浮 / 随水柱下沉，resting 恒 false）。
            if (e.resting) { e.resting = false; dirty = true; }

            // (a) 浮水面（非瀑布）：扫列向上找最顶水格（其上非水 = 水面），恒速上浮到水面。
            if (!waterfall) {
                int surfCellY = cy;
                // blockAt 对 y>=height 返 0(空气) → 循环到世界顶自然停；128 为硬上限防异常长水柱。
                for (int i = 0; i < 128; ++i) {
                    if (world->blockAt(cx, surfCellY + 1, cz) != BlockRegistry::Water) break;
                    ++surfCellY;
                }
                const float restY = float(surfCellY + 1) - kItemFloatOffset; // 中心贴水面、留水格内
                if (e.pos.y() < restY - 1e-3f) {
                    e.vy = kItemRiseSpeed; // 恒速上浮（机制等价 MC 掉落物水中缓浮）
                    float newY = e.pos.y() + e.vy * float(dt);
                    if (newY > restY) newY = restY; // 到水面钳住（防上冲出空气格→振荡）
                    e.pos.setY(newY);
                    if (newY >= restY - 1e-3f) e.vy = 0.0f; // 抵达水面 → 静止
                    dirty = true;
                } else if (e.vy != 0.0f || e.pos.y() != restY) {
                    e.vy = 0.0f;     // 在水面：静止（呈现层 bobY 动画给视觉浮动，物理稳）
                    e.pos.setY(restY);
                    dirty = true;
                }
            }

            // (b) 随流移动（浮水 + 瀑布均施）：流水 state>0 才推（水源 state=0 静止，spec）。
            //   4 向邻居 state 梯度 → 离源方向（与 PlayerController t211 玩家水流推力同源算法）。
            const quint8 cellState = world->stateAt(cx, cy, cz);
            if (cellState > 0) {
                float gx = 0.0f, gz = 0.0f;
                constexpr int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto &d : dirs) {
                    const int nx = cx + d[0], nz = cz + d[1];
                    if (world->blockAt(nx, cy, nz) == BlockRegistry::Water) {
                        const quint8 ns = world->stateAt(nx, cy, nz);
                        if (ns < cellState) { // 该邻居更近源 → 推力朝远离它（离源 = 高 state 远源方向）
                            gx -= float(d[0]) * float(cellState - ns);
                            gz -= float(d[1]) * float(cellState - ns);
                        }
                    }
                }
                const float glen = std::sqrt(gx * gx + gz * gz);
                if (glen > 1e-4f) {
                    const float dvx = (gx / glen) * kItemFlowSpeed * float(dt);
                    const float dvz = (gz / glen) * kItemFlowSpeed * float(dt);
                    // per-axis 试探 + isCollidable 查（撞墙 / 撞半砖该轴不动，沿墙滑动而非卡死）。
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
            // 瀑布：fall through 到重力分支（水穿透列扫 → 随水柱下沉）
        }

        // === 重力分支（空气 + 瀑布）：t60 原逻辑，列扫已修正「水穿透」 ===
        // 已落地：复探支撑格（cellY = floor(pos.y) - 1，即静止中心下方那一格）。水不再算支撑
        //   （isSolid && blockAt != Water）→ 水填满下方时解除 resting 续落 / 下帧转浮水分支。
        if (e.resting) {
            const int supportY = qFloor(e.pos.y()) - 1; // 静止中心下方那一格（= 支撑方块 cellY）
            // 三目两支统一为 quint8（blockAt 返回 quint8，false 支显式强转枚举避 -Wextra 枚举/非枚举混用告警）。
            const quint8 sb = (supportY >= 0) ? world->blockAt(cx, supportY, cz) : quint8(BlockRegistry::Air);
            if (sb != BlockRegistry::Water && world->isSolid(cx, supportY, cz)) continue; // 仍实体 → 保持静止
            e.resting = false; // 支撑消失（被挖 / 被水填）→ 续落（vy 已 0，从静止重新加速）
            dirty = true;
        }

        // 重力 + 下移（vy 向下为负）。
        e.vy -= kGravity * float(dt);
        if (e.vy < -kMaxFall) e.vy = -kMaxFall;
        const float newY = e.pos.y() + e.vy * float(dt);

        // 下移路径自顶向下扫实体所在列首个实体方块（防大 dt 穿过薄层；lessons「子步防穿墙」精神）。
        //   t271 关键修正：水视作穿透（isSolid && blockAt != Water）→ 掉落物穿水面入水，下帧转浮水分支
        //   （机制等价 t220「水不挡沙」），而非粘在水面当着地。
        const int topCell = qFloor(e.pos.y()); // 当前中心所在格（一般为空气）
        int botCell = qFloor(newY);
        if (botCell > topCell) botCell = topCell; // 防浮点噪声致 botCell>topCell（vy≈0 时 newY 微高于 pos.y）
        int solidCellY = -1;
        for (int scy = topCell; scy >= botCell; --scy) {
            if (scy < 0) break; // 越界下方=空气（World 约定）→ 不视作地面，实体继续落
            const quint8 b = world->blockAt(cx, scy, cz);
            if (b != BlockRegistry::Water && world->isSolid(cx, scy, cz)) { solidCellY = scy; break; }
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
