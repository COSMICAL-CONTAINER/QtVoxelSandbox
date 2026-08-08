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
// spec「实体数量有上限（防溢出）」。t320 cap 行为改 LRU 驱逐：达 kCap 时不再「跳过新 spawn」（玩家视角是
//   破块没掉落 = bug），而是驱逐最老活体（min spawnMs）腾位（机制等价 MC kMaxItemEntities 滑动窗 + LRU
//   驱逐）。爆炸瞬时产数十掉落物 + 已有累积 → 老掉落物让位给新，玩家始终能看到本次破块的产出；被驱逐者
//   走 releaseSlot（同拾取路径，aliveAt=false → delegate 隐藏 + 槽位可复用）。
// t64：count 字段支持整栈丢弃为 1 实体（如 4 木棒丢出仍 1 实体 count=4）；count<=0 视作 1。
void ItemEntityManager::spawnItem(int x, int y, int z, int itemId, int count)
{
    if (itemId <= 0) return; // air / 非法：不产出（PlayerController 仅在 drop=true 时发，已过滤）
    if (count < 1) count = 1; // 缺省 / 非法 → 单件（与历史调用兼容）
    if (m_liveCount >= kCap) {
        // t320 LRU 驱逐最老活体（min spawnMs = max age）腾位。O(N) 扫，N≤kCap(200) 常数级。
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
    acquireSlot(ItemEntity{QVector3D(x + 0.5f, y + 0.5f, z + 0.5f), itemId, count, m_clock.elapsed()}); // t256 slot 复用
    notifyChanged(); // t354：经批量收口（批内不 emit，endBatch 末尾 1 次 emit）
    qCInfo(lcItem) << "spawned item entity id=" << itemId << "count=" << count << "at" << x << y << z
                   << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// t354 批量 emit 收口实现（见 .h beginBatch / notifyChanged 注释）。
void ItemEntityManager::notifyChanged()
{
    ++m_revision;
    if (m_batchDepth <= 0) {
        emit entitiesChanged(); // 非批（常态）：立即通知，行为同旧
    } else {
        m_batchDirty = true; // 批内：仅标 dirty，由 endBatch 末尾 1 次 emit 收口
    }
}

// t354 进入批量：depth++（可嵌套；非爆炸的常规 spawn 不经批 → depth 恒 0）。
void ItemEntityManager::beginBatch() { ++m_batchDepth; }

// t354 退出批量：depth 归 0 且批内有 dirty → 1 次 emit 收口；未 begin / 无 dirty → no-op（防御水中爆炸无掉落
//   → onExplosionDroppedItem 未发 → 无 begin，onExplosion 仍调 endBatch 的情形）。
void ItemEntityManager::endBatch()
{
    if (m_batchDepth <= 0) return;
    if (--m_batchDepth == 0 && m_batchDirty) {
        m_batchDirty = false;
        emit entitiesChanged(); // 1 次 emit 收口 N 个累积变更（修爆炸 O(N²) 绑定风暴 → O(N)）
    }
}

// t256：第 i 个槽位是否活体。空槽 → false（呈现层 delegate visible 隐藏 + pickupScan 跳过）。
bool ItemEntityManager::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].alive;
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
// t256：销毁走 releaseSlot（标空 + 入 free list，不 erase-shift）→ count 单调不降 → Repeater delegate
//   不泄漏（掉落物 spawn/拾取抖动同掉落沙族泄漏，slot 复用根治）。
void ItemEntityManager::setCountAt(int i, int n)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    ItemEntity &e = m_entities[size_t(i)];
    if (!e.alive) return; // t256：空槽防御
    if (e.count == n) return;
    if (n <= 0) {
        // 余数为 0 = 全拾走 → 释放槽位（同 removeAt 路径，但走 setCountAt(0) 调用方语义统一）。
        releaseSlot(i);
        qCInfo(lcItem) << "item entity at index" << i << "consumed fully (live" << m_liveCount << "slots" << m_entities.size() << ")";
    } else {
        e.count = n;
        qCInfo(lcItem) << "item entity at index" << i << "count ->" << n
                       << "(partially picked; remaining in world)";
    }
    notifyChanged(); // t354：经批量收口（内部 ++revision + 按需 emit）
}

// 销毁第 i 个实体（t36 拾取消费）。t256：改 releaseSlot（标空 + 入 free list）替代 erase-shift —— 保
//   count 单调不降 → Repeater 不需销毁 reparent 的 3D delegate → 消除 spawn/拾取抖动致 delegate 泄漏。
//   release 不 shift 索引（slot 稳定），bump revision 驱动 delegate 的 {revision; posAt/...} 绑定重算
//   （空槽 aliveAt=false → delegate visible=false 隐藏；复用时 visible=true + 数据重绑）。
void ItemEntityManager::removeAt(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    if (!m_entities[size_t(i)].alive) return; // t256：空槽防御（重复 remove 安全）
    releaseSlot(i);
    notifyChanged(); // t354：经批量收口（内部 ++revision + 按需 emit）
    qCInfo(lcItem) << "picked up item entity at index" << i
                   << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
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
    // t320 寿命到期驱逐先于物理（独立于 world —— 菜单 / 暂停时世界不模拟但掉落物照常老化消失）。
    despawnExpired();
    if (!world || m_entities.empty()) return;
    bool dirty = false;
    for (auto &e : m_entities) {
        if (!e.alive) continue; // t256：跳过已释放的空槽（slot-reuse 残留位；不参与物理）
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过（防越界误判）

        const int cy = qFloor(e.pos.y());
        // t343 掉落物丢入岩浆被摧毁（spec「Q 键物品丢岩浆→摧毁」）：实体中心格 == Lava → 焚毁释放该槽。
        //   机制等价 MC 1.0 掉落物接触岩浆 / 火即消失。releaseSlot 标 alive=false（slot-reuse，同拾取路径），
        //   本迭代即结束（continue）；末尾 dirty=true 触发 emit entitiesChanged → QML delegate 隐藏。
        if (cy >= 0 && world->blockAt(cx, cy, cz) == BlockRegistry::Lava) {
            const int idx = int(&e - &m_entities.front());
            releaseSlot(idx);
            dirty = true;
            continue;
        }
        // t445 ⑤ 掉落物落到 / 触碰仙人掌被摧毁（spec「Q 丢物落到仙人掌→被顶掉/销毁」）：实体中心下方一格 ==
        //   Cactus（即落在仙人掌顶上 / 贴其侧下落）→ 摧毁释放该槽。机制等价 MC 1.0 掉落物接触仙人掌即消失
        //   （MC 仙人掌摧毁触碰它的物品实体）。releaseSlot 标 alive=false（slot-reuse，同岩浆 / 拾取路径），
        //   本迭代即结束（continue）；末尾 dirty=true 触发 emit entitiesChanged → QML delegate 隐藏。
        if (cy - 1 >= 0 && world->blockAt(cx, cy - 1, cz) == BlockRegistry::Cactus) {
            const int idx = int(&e - &m_entities.front());
            releaseSlot(idx);
            dirty = true;
            continue;
        }
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
    if (dirty) notifyChanged(); // t354：经批量收口（内部 ++revision + 按需 emit）
}

// t320 自然寿命驱逐（见头文件 despawnExpired 注释）。每帧 tick 起始调，先于物理。
void ItemEntityManager::despawnExpired()
{
    if (m_entities.empty()) return;
    const qint64 now = m_clock.elapsed();
    bool dirty = false;
    for (int i = 0; i < int(m_entities.size()); ++i) {
        if (!m_entities[size_t(i)].alive) continue; // t256：跳过已释放的空槽
        if (now - m_entities[size_t(i)].spawnMs > kDespawnMs) {
            releaseSlot(i); // 同拾取路径（alive=false + 入 free list + --liveCount）
            dirty = true;
            qCInfo(lcItem) << "item entity at index" << i << "despawned after" << kDespawnMs
                           << "ms (lifetime; live" << m_liveCount << "slots" << m_entities.size() << ")";
        }
    }
    if (dirty) notifyChanged(); // t354：经批量收口（内部 ++revision + 按需 emit）
}
