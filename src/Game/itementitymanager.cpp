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

// ── t1027 掉落物渲染家族谓词（单一权威；实现见头注释）──
bool ItemEntityManager::isItem3DFamily(int itemId)
{
    // 家族表自 Main.qml isItem3DFamily（t880 建 / t925 扩 / t965 订正铁门 135）逐 id 收编，值不变：
    //   火把 13；活板门 20/136；台阶 15/87/58/109；雪层 44；草丛 24；附魔台 94；枯灌木 43；小麦 25；
    //   栅栏 17/60/88；门 19/135/89；蘑菇 115/48；蛛网 102；红石火把 129；拉杆 112；按钮 113/114。
    switch (itemId) {
    case 13: case 20: case 136: case 15: case 87:
    case 58: case 109: case 44: case 24: case 94:
    case 43: case 25:
    case 17: case 60: case 88:
    case 19: case 135: case 89:
    case 115: case 48:
    case 102: case 129:
    case 112: case 113: case 114:
        return true;
    default:
        return false;
    }
}

bool ItemEntityManager::isPlainCubeDrop(int itemId)
{
    if (itemId <= 0 || itemId >= int(BlockRegistry::Count)) return false; // air / 越界 / 工具·材料段（见 static_assert）
    if (BlockRegistry::isPartialBlock(quint8(itemId))) return false;      // 异形段（台阶 16 / 半砖…）→ billboard / 3D 族
    if (BlockRegistry::isCrossBillboard(quint8(itemId))) return false;    // cross 段（花 / 树苗…）→ flat billboard
    if (BlockRegistry::isBed(quint8(itemId))) return false;               // 床段（含 8 色扩展床）→ bed 图标 billboard
    return !isItem3DFamily(itemId);                                       // 3D 形状族（火把 / 门 / 栅栏…）排除
}

// 生成掉落实体：存格中心坐标 + id + count，bump 版本号发 entitiesChanged → QML Repeater 追加 delegate。
// spec「实体数量有上限（防溢出）」。t320 cap 行为改 LRU 驱逐：达 kCap 时不再「跳过新 spawn」（玩家视角是
//   破块没掉落 = bug），而是驱逐最老活体（min spawnMs）腾位（机制等价 MC kMaxItemEntities 滑动窗 + LRU
//   驱逐）。爆炸瞬时产数十掉落物 + 已有累积 → 老掉落物让位给新，玩家始终能看到本次破块的产出；被驱逐者
//   走 releaseSlot（同拾取路径，aliveAt=false → delegate 隐藏 + 槽位可复用）。
// t64：count 字段支持整栈丢弃为 1 实体（如 4 木棒丢出仍 1 实体 count=4）；count<=0 视作 1。
// t590：enchants 是 QVariantList<int> 4 元素（每 = EnchantRegistry::pack 值；缺省空 = 无附魔）。工具 / 护甲
//   丢弃时传其实例附魔 → 实体携带 → 拾取回填（防「附魔工具丢出再捡变普通」）。可堆叠物品（合并路径）恒 0。
// t622：name 是自定义名（铁砧重命名产物丢弃传其实例名 → 实体携带 → 拾取回填，防「改名物品丢出再捡丢名」）。
// review D2-c：durability 是实例耐久（破箱掉落的磨损工具经 Main.qml 携 slotDurabilityAt 传入 → 实体携带 →
//   拾取回填 addToAny 第 3 参，防「破箱捡回满耐久」免费修复；缺省 -1 = 未初始化 → 归一满耐久，20+ 既有
//   emit / 直调点零改动）。
void ItemEntityManager::spawnItem(int x, int y, int z, int itemId, int count, const QVariantList &enchants, const QString &name, int durability)
{
    if (itemId <= 0) return; // air / 非法：不产出（PlayerController 仅在 drop=true 时发，已过滤）
    if (count < 1) count = 1; // 缺省 / 非法 → 单件（与历史调用兼容）

    // t490fix 就近合并（机制等价 MC 1.0 同 itemId 掉落物在近邻合并为 1 实体）：spawn 前扫现有活体，找同
    //   itemId 且 pos 距 (x+0.5,y+0.5,z+0.5) ≤ kMergeRadius 的第一个 → count 累加（clamp maxStack）。合并方向
    //   新 spawn 往已有实体合（不动已有 pos 避免视觉跳变）。count 用 e.count 直接改 + notifyChanged（同
    //   setCountAt 语义；批内仅标 dirty → t354 爆炸批合并正确生效）。maxStack<=1（工具 / 不可堆叠）→ 跳过合并。
    //   爆炸场景（t354 beginBatch/endBatch）：第 2 个掉落物 spawn 时能找到第 1 个并合并 → 批内生效。
    //   review rev2-C5：**带名实体不参与合并**（双向）—— 铁砧可给整栈可堆叠物改名（anvilRename 对 maxStack>1
    //   物品写 customName），合并会把带名实例吸进无名堆（名静默丢失，正是 t622 要防的）。双向守卫：传入 name
    //   非空 → 不并入任何已有实体；已有实体带名（e.name 非空）→ 不接收任何并入。带名实体各自独立落体，
    //   拾取走 addToAny 的带名不合并守卫（见 hotbar.cpp 同批修法）。
    {
        const int cap = BlockRegistry::maxStackSize(itemId); // Core 层全 id 段堆叠上限（掉落物合并用）
        const bool namedDrop = !name.trimmed().isEmpty();     // rev2-C5：带名丢弃（改名产物）不合并
        if (cap > 1 && !namedDrop && !m_entities.empty()) {
            const QVector3D center(x + 0.5f, y + 0.5f, z + 0.5f);
            const float r2 = kMergeRadius * kMergeRadius; // 平方距离比较（免 sqrt）
            for (size_t i = 0; i < m_entities.size(); ++i) {
                ItemEntity &e = m_entities[i];
                if (!e.alive || e.itemId != itemId) continue; // 空槽 / 不同物品 → 跳过
                if (!e.name.trimmed().isEmpty()) continue;    // rev2-C5：已有实体带名 → 不接收并入
                const QVector3D d = e.pos - center;
                if (d.x() * d.x() + d.y() * d.y() + d.z() * d.z() > r2) continue; // 超半径 → 跳过
                // 命中可合并实体：clamp 到 cap，溢出余数走新 spawn（保掉落实体 count 不超 maxStack）。
                if (e.count < cap) {
                    const int add = std::min(cap - e.count, count);
                    e.count += add;
                    count -= add; // 余数（>0 → 走新 spawn；==0 → 完全合并完，return）
                    notifyChanged(); // t354：经批量收口（批内仅标 dirty；非批立即 emit）
                    qCInfo(lcItem) << "merged item entity id=" << itemId << "into slot" << int(i)
                                   << "count ->" << e.count << "(at" << x << y << z << ")";
                    if (count <= 0) return; // 完全合并 → 不新 spawn（少一个 delegate = 用户「掉落物太多」的诉求）
                }
                continue; // 爆炸批量合并优化：不 break（旧 break 仅合第一个就停），继续扫其余活体找下一个可合并的
                //   （count<cap 的）。爆炸同帧 N 个同 id 掉落物同格 spawn 时，优先填满已有实体（同格多件聚拢）
                //   而非急着新 spawn。第一个满了（count==cap）也继续找第二个、第三个可合并的，直到 count 用完
                //   或扫完。最坏遍历 O(n)，n≤kCap=200 常数级可接受。
            }
        }
    }

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
    const int slot = acquireSlot(ItemEntity{QVector3D(x + 0.5f, y + 0.5f, z + 0.5f), itemId, count, m_clock.elapsed()}); // t256 slot 复用
    // t590 附魔写入：QVariantList<int> → int[4]（不足按 0 补齐 = 无附魔；越界截断防御）。可堆叠物品（方块 /
    //   材料段）恒全 0（inert）；工具 / 护甲丢弃保实例附魔（拾取回填用）。
    {
        ItemEntity &e = m_entities[size_t(slot)];
        for (int i = 0; i < 4; ++i)
            e.enchants[i] = (i < enchants.size()) ? enchants.at(i).toInt() : 0;
        e.name = name.trimmed(); // t622 实例名（空串 = 注册表默认名；拾取回填用）
        e.durability = durability; // review D2-c 实例耐久（-1 = 未初始化；拾取回填用）
    }
    // t468 初始水平弹出速度（机制等价 MC 破块 / 丢弃物品弹出）：确定性哈希（位置 + itemId）给每件一个固定方向
    //   + 幅值抖动 → 同一掉落可复现。冰面摩擦极低 → 弹出后持续滑动（spec「冰上丢弃物品会一直滑动往前」）；
    //   常规地面摩擦高 → 快速停下。取 acquireSlot 写入的实体引用设 vx/vz。
    {
        ItemEntity &e = m_entities[size_t(slot)];
        // 简单整数哈希（非 worldgen 确定性范畴 —— 掉落弹出是即时反馈，确定性仅为可复现，无 PLAN §2-K 约束）。
        quint32 h = quint32(x) * 73856093u ^ quint32(y) * 19349663u ^ quint32(z) * 83492791u ^ quint32(itemId) * 2654435761u;
        const float angle = float(h % 3600u) * (3.14159265358979f / 1800.0f); // 0.1° 精度方向（全圆）
        const float mag = kItemPopSpeed * (0.7f + 0.6f * float((h >> 11) % 100u) / 100.0f); // 幅值抖动 0.7×..1.3×
        e.vx = std::cos(angle) * mag;
        e.vz = std::sin(angle) * mag;
    }
    notifyChanged(); // t354：经批量收口（批内不 emit，endBatch 末尾 1 次 emit）
    qCInfo(lcItem) << "spawned item entity id=" << itemId << "count=" << count << "at" << x << y << z
                   << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// t608 定点定向弹出（见 .h 头注释）：发射器排出口统一口径。与 spawnItem 的差异仅在 ① 精确浮点位置
//   （排出口面中心，非格中心）+ ② 弹出方向 = 发射朝向（dir 归一化 × speed，非哈希随机）。合并 / LRU /
//   附魔 / 免拾窗 / 物理（重力 + 摩擦）全部同链 —— 物理在 tick 内按 vx/vz 积分，本入口只负责生成时
//   写入初速。机制等价 MC 1.0 发射器把物品从排出口朝朝向弹出。
void ItemEntityManager::spawnItemAt(const QVector3D &pos, int itemId, int count,
                                    float dirX, float dirZ, float speed,
                                    const QVariantList &enchants, const QString &name, int durability)
{
    if (itemId <= 0) return; // air / 非法：不产出（同 spawnItem 守卫）
    if (count < 1) count = 1;

    // 就近合并（同 spawnItem 语义）：排出口附近已有同 id 掉落物（如连续踩板弹出多件）→ 合并，少 delegate。
    //   review rev2-C5：带名（双向）不参与合并（同 spawnItem 段注释——铁砧可给整栈可堆叠物改名，合并丢名）。
    {
        const int cap = BlockRegistry::maxStackSize(itemId);
        const bool namedDrop = !name.trimmed().isEmpty();
        if (cap > 1 && !namedDrop && !m_entities.empty()) {
            const float r2 = kMergeRadius * kMergeRadius;
            for (size_t i = 0; i < m_entities.size(); ++i) {
                ItemEntity &e = m_entities[i];
                if (!e.alive || e.itemId != itemId) continue;
                if (!e.name.trimmed().isEmpty()) continue;    // rev2-C5：已有实体带名 → 不接收并入
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
    const int slot = acquireSlot(ItemEntity{pos, itemId, count, m_clock.elapsed()});
    {
        ItemEntity &e = m_entities[size_t(slot)];
        for (int i = 0; i < 4; ++i)
            e.enchants[i] = (i < enchants.size()) ? enchants.at(i).toInt() : 0;
        e.name = name.trimmed(); // t622 实例名（拾取回填用）
        e.durability = durability; // t647 实例耐久（-1 = 未初始化；拾取回填用）
    }
    // 定向弹出初速：dir 归一化 × speed（退化全 0 → 不设初速，原地落地）。vy=0（水平弹出 + 重力抛物，
    //   机制等价 MC 发射器弹物品的短抛物线）。
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

// t609 带俯仰的定点定向弹出（Q 丢弃修正，见 .h 头注释）：与 spawnItemAt 完全同链（合并 / LRU / 附魔 /
//   免拾窗 / 重力 + 摩擦），差异仅 ① 初速三维归一化（含 Y 分量：仰视上抛 / 俯视下压）② 日志标签。
//   机制等价 MC 玩家主动丢弃物品：从眼位沿视线扔出（非哈希随机全圆弹出）。
void ItemEntityManager::spawnItemThrown(const QVector3D &pos, int itemId, int count,
                                        float dirX, float dirY, float dirZ, float speed,
                                        const QVariantList &enchants, const QString &name, int durability)
{
    if (itemId <= 0) return; // air / 非法：不产出（同 spawnItem 守卫）
    if (count < 1) count = 1;

    // 就近合并（同 spawnItemAt 语义）：眼位前方已有同 id 掉落物（如连按 Q）→ 合并，少 delegate。
    //   review rev2-C5：带名（双向）不参与合并（同 spawnItem 段注释——铁砧可给整栈可堆叠物改名，合并丢名）。
    {
        const int cap = BlockRegistry::maxStackSize(itemId);
        const bool namedDrop = !name.trimmed().isEmpty();
        if (cap > 1 && !namedDrop && !m_entities.empty()) {
            const float r2 = kMergeRadius * kMergeRadius;
            for (size_t i = 0; i < m_entities.size(); ++i) {
                ItemEntity &e = m_entities[i];
                if (!e.alive || e.itemId != itemId) continue;
                if (!e.name.trimmed().isEmpty()) continue;    // rev2-C5：已有实体带名 → 不接收并入
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
    const int slot = acquireSlot(ItemEntity{pos, itemId, count, m_clock.elapsed()});
    {
        ItemEntity &e = m_entities[size_t(slot)];
        for (int i = 0; i < 4; ++i)
            e.enchants[i] = (i < enchants.size()) ? enchants.at(i).toInt() : 0;
        e.name = name.trimmed(); // t622 实例名（拾取回填用）
        e.durability = durability; // t647 实例耐久（-1 = 未初始化；拾取回填用）
    }
    // 定向投掷初速（三维）：dir 归一化 × speed（含 Y 分量——仰视上抛 / 俯视下压；重力在 tick 内继续作用成
    //   抛物线）。退化全 0 → 不设初速（原地落下）。
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

// t743 掉落物着地态查询（见 .h 注释）：压力板掉落物触发门控用（着地 = 与支撑面接触）。
bool ItemEntityManager::restingAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].resting;
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

// t590 实体附魔元数据（QVariantList<int> 4 元素，每 = EnchantRegistry::pack 值；0 = 空槽）。呈现层据它给
//   掉落物紫光晕（附魔工具落地显光晕）+ PlayerController 拾取回填（防附魔丢失）。越界 / 空槽 → {0,0,0,0}。
QVariantList ItemEntityManager::enchantsAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return {0, 0, 0, 0};
    const ItemEntity &e = m_entities[size_t(i)];
    return { e.enchants[0], e.enchants[1], e.enchants[2], e.enchants[3] };
}

// t622 实体自定义名（铁砧重命名产物丢弃保真；空串 = 注册表默认名）。越界 / 空槽 → 空串。
QString ItemEntityManager::nameAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QString();
    return m_entities[size_t(i)].name;
}

// review D2-c 实体实例耐久（-1 = 未初始化 → 拾取端 addToAny 归一满耐久；>0 = 显式保真，破箱掉落的
//   磨损工具丢出再捡耐久不复原）。越界 / 空槽 → -1（同未初始化语义，拾取端归一满耐久）。
int ItemEntityManager::durabilityAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return -1;
    return m_entities[size_t(i)].durability;
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

// t889 暂停期墙钟顺延（语义见 .h 声明处头注释）：活体槽 spawnMs 整体 +ms。ms<=0 早退（幂等防御）。
void ItemEntityManager::deferWallClocks(qint64 ms)
{
    if (ms <= 0) return;
    for (auto &e : m_entities) {
        if (e.alive) e.spawnMs += ms;
    }
}

// t60 掉落物重力 / t271 水冲走掉落物（每帧由 PlayerController::tick 调）。对每个实体先判中心格是否
//   为 Water，分流「浮水 + 随流」与「空气重力」两条路径。详见 .h 头注（分层 / 机制 / 关键修正）。
// 单帧最大下移 = kMaxFall*0.05 ≈ 3.9 格（dt 钳 50ms）→ 列扫 ≤4 格，cheap；≤200 实体全程 O(数百)。
void ItemEntityManager::tick(qreal dt, World *world)
{
    // t320 寿命到期驱逐先于物理。t889 前注释「菜单/暂停时照常老化消失」语义已退役 —— 硬暂停
    //   （ESC）期本 tick 不被 PlayerController 调用 + 复跑时 deferWallClocks 顺延 spawnMs → 掉落物
    //   暂停期不老化（机制等价 MC Java 单机 ESC 全冻结）；GUI 面板开（软档）时 tick 照跑、照常老化。
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
        // t844 掉落物入火**瞬灭**（需求反转，覆盖 t804③ 的 0.8s 点燃窗；机制等价 MC 1.0 掉落物接触火
        //   即刻消失，与上方岩浆瞬毁 t343 同款语义——无销毁动画、无烟粒子、无抢回窗）。实体中心格 ==
        //   Fire → releaseSlot 释放该槽（slot-reuse，同岩浆路径），本迭代即结束（continue）；末尾
        //   dirty=true 触发 emit entitiesChanged → QML delegate 隐藏。旧 fireBurn 倒计 / itemBurned
        //   烟粒子信号随本语义一并退役（呈现层 Connections 已同步移除）。
        //   **语义边界（燃烧方块格不烧掉落物）**：本判定只认立地火格（blockAt == Fire）；燃烧态方块格
        //   （m_burningCells 侧表瞬态，栅格 id 不变）不在此列——燃烧是「方块本身着火」（面火 overlay 贴
        //   其表面），非「火占据该格」，掉落物落在燃块顶面 = 落在实体面上，照常物理，不被焚毁。MC 同款：
        //   物品须进入 fire 格才烧，站在着火的木板上不掉耐久。
        if (cy >= 0 && world->blockAt(cx, cy, cz) == BlockRegistry::Fire) {
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
        // review26 #2：resting 且复探仍有支撑 → 跳过重力分支但保走水平积分（见下方 resting 块）。
        bool groundedRest = false;

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
                // t892：水面 = 顶水格 + BlockRegistry::waterSurfaceFrac（单一权威，源 7/8）− 下沉量 ——
                //   掉落物浮定高度随可视静水位同步降（旧口径 surfCellY+1 满格会让物品悬在降位水面上方）。
                const float restY = float(surfCellY)
                    + BlockRegistry::waterSurfaceFrac(world->stateAt(cx, surfCellY, cz))
                    - kItemFloatOffset; // 中心贴水面、留水格内
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

        // === 重力分支（空气 + 瀑布）：t60 原逻辑，t867 起列扫改支撑真顶（World::supportTopYAt 单一权威）===
        // 已落地：复探支撑（中心格及其下一格两格窗 —— t867 薄支撑中心落进支撑格内：板上静止中心 =
        //   板顶 1/16 + kRestOffset(0.3) → floor(pos.y()) 恰 = 板格自身；满格支撑则中心在上一格 → 下探一格。
        //   任一格有碰撞支撑（真顶 ≥0；水/火/无碰撞族 -1 = 穿透语义由落地扫描同源承担）→ 保持静止。
        //   旧版 isSolid（非 air）把压力板 / 轨 / 薄板当满格支撑 + 恒「下一格」单窗 → 与新落地高度不一致
        //   会振荡，本窗随落地公式同源对齐。水填满下方 → 解除 resting 续落 / 下帧转浮水分支。
        if (e.resting) {
            // 两格窗 + 下贴：取窗口内首个「真顶 ≤ 当前底（pos.y − kRestOffset + 容差）」的支撑 ——
            //   支撑还在但变矮（薄支撑被拆、下方换成矮盒）→ 下贴新真顶（对齐 mob 侧 review D1-b 行走贴面
            //   语义：t867 拆板后物品从板面高滑落到基座顶，不悬 1/16）；真顶高于当前底（支撑上方长出
            //   更高体）不算当前支撑、不动。窗口内无任何可站支撑 → 解除 resting 续落（防挖空悬空）。
            const int feetCell = qFloor(e.pos.y());
            float snapTop = -1.0f;
            for (int scy = feetCell; scy >= feetCell - 1; --scy) {
                if (scy < 0) break;
                const float top = world->supportTopYAt(cx, scy, cz);
                if (top >= 0.0f && top <= e.pos.y() - kRestOffset + 0.05f) { snapTop = top; break; }
            }
            if (snapTop >= 0.0f) {
                const float restY = snapTop + kRestOffset;
                if (restY < e.pos.y() - 1e-3f) { e.pos.setY(restY); dirty = true; } // 下贴矮了的支撑面
                // review26 #2（下半）：有支撑的 resting 物品不再整段跳过 —— 残余水平速度仍走下方水平积分
                //   + 支撑面摩擦（下方 t468 摩擦注释声明的设计行为：常规地面 ~0.5s 停、冰面数秒长滑；
                //   旧 continue 让着地后滑动只剩落定那一拍 ≈ 「物品被钉在落点」的另一半根因，与上方
                //   薄支撑脚位格豁免合成完整修法）。仅跳过重力分支（resting 无垂直运动）。
                groundedRest = true;
            } else {
                e.resting = false; // 支撑消失（被挖 / 被水填 / 换无碰撞格）→ 续落（vy 已 0，从静止重新加速）
                dirty = true;
            }
        }

        if (!groundedRest) {
        // 重力 + 下移（vy 向下为负）。
        e.vy -= kGravity * float(dt);
        if (e.vy < -kMaxFall) e.vy = -kMaxFall;
        const float newY = e.pos.y() + e.vy * float(dt);

        // 下移路径自顶向下扫实体所在列首个支撑真顶（防大 dt 穿过薄层；lessons「子步防穿墙」精神）。
        //   t271 关键修正：水视作穿透 → 掉落物穿水面入水，下帧转浮水分支（机制等价 t220「水不挡沙」）。
        //   t804：火同水穿透 → 掉落物落进火格（中心在火格内 → tick 头部火焚判定接管），而非骑在火格顶面。
        //   t867：收口 World::supportTopYAt（碰撞 sub-AABB 真顶单一权威）—— 压力板 / 睡莲等薄板真顶承接
        //   （板上掉落物紧贴板面，不再悬上方一格；用户报「压力板掉落物贴板」），轨 / 火把 / 花草无碰撞族
        //   -1 穿透到下方真支撑；岩浆（ShapeNone）不再被 isSolid 当落点 → 掉落物落入岩浆格由头部瞬毁
        //   判定接管（旧 isSolid 把岩浆当整格 → 高处落入的物品骑在岩浆面上不焚毁的隐性缺陷一并修复）。
        const int topCell = qFloor(e.pos.y()); // 当前中心所在格（一般为空气）
        int botCell = qFloor(newY);
        if (botCell > topCell) botCell = topCell; // 防浮点噪声致 botCell>topCell（vy≈0 时 newY 微高于 pos.y）
        float supportTop = -1.0f;
        for (int scy = topCell; scy >= botCell; --scy) {
            if (scy < 0) break; // 越界下方=空气（World 约定）→ 不视作地面，实体继续落
            const float top = world->supportTopYAt(cx, scy, cz);
            if (top >= 0.0f) { supportTop = top; break; }
        }

        if (supportTop >= 0.0f) {
            // 落地：贴支撑真顶 + 静止偏移。钳 newY 防穿越（newY 可能已低于顶面）。
            const float restY = supportTop + kRestOffset;
            if (newY <= restY || e.vy < 0.0f) {
                if (e.pos.y() != restY) { e.pos.setY(restY); dirty = true; }
                if (e.vy != 0.0f) { e.vy = 0.0f; dirty = true; }
                e.resting = true;
            }
        } else if (newY != e.pos.y()) {
            e.pos.setY(newY); // 自由下落（无命中）
            dirty = true;
        }
        } // !groundedRest（resting 有支撑：免重力，仅水平积分 + 摩擦）

        // t468 水平速度积分 + 摩擦（spec「冰上丢弃物品会一直滑动往前」）。仅非水实体（水实体由 flow drift 处理
        //   水平；瀑布实体 inWater=true 也跳过）。生成时带初始弹出 vx/vz（机制等价 MC 破块 / 丢弃弹出），每 tick
        //   积分位移 + per-axis isCollidable 查（撞墙该轴不动，沿墙滑）。着地时按支撑面摩擦衰减：冰面（isIce）
        //   kItemIceFriction 极低 → 持续滑动数秒；常规地面 kItemGroundFriction 高 → ~0.5s 停下。空中不衰减
        //   （保留水平动量，机制等价 MC 物品空中弧线）。停止阈值 0.05 防微观抖动永久微移。
        if (!inWater && (std::fabs(e.vx) > 1e-4f || std::fabs(e.vz) > 1e-4f)) {
            const int hcy = qFloor(e.pos.y()); // 当前中心所在格（水平碰撞用，垂直已解算）
            // review26 #2（t867 半改态收尾）：薄支撑脚位格豁免（mob 侧 mobAabbHitsSolid 豁免的物品同根）。
            //   静息中心 = 真顶 + kRestOffset(0.3)，顶面 <0.7 的薄支撑（压力板 1/16 / 睡莲 / 合活板门
            //   0.1875 / 下半砖 0.5 / 床 0.31）中心落进支撑格内部 → hcy = 支撑格自身 → isCollidable 恒
            //   true → 带初始弹出速度也滑不动（物品被「钉」在落点，冰面滑摩擦特性在这些支撑上同失效；
            //   旧版 restY 恒 cell+1+0.3 → hcy=上方空气格可滑）。判据与 resting 复探同源（上方两格窗）：
            //   目标格碰撞真顶（supportTopYAt 单一权威）≤ 当前底（pos.y−kRestOffset+0.05 容差）= 正站
            //   其顶 → 不挡；走向满格墙仍挡（真顶 > 底），沿墙滑动语义不变。
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
                // t867：支撑面格与 resting 复探同源两格窗（薄支撑中心落进支撑格内 → 支撑格 = 自身格；
                //   满格支撑 → 下一格），取首个有碰撞真顶者作摩擦面判定（冰面滑 / 常规磨）。
                const int fc = qFloor(e.pos.y());
                int supportY = fc - 1;
                if (fc >= 0
                    && world->supportTopYAt(qFloor(e.pos.x()), fc, qFloor(e.pos.z())) >= 0.0f)
                    supportY = fc;
                // 三目两支统一 quint8（blockAt 返 quint8，false 支显式强转枚举避 -Wextra 枚举/非枚举混用告警）。
                const quint8 sb = (supportY >= 0) ? world->blockAt(qFloor(e.pos.x()), supportY, qFloor(e.pos.z()))
                                                   : quint8(BlockRegistry::Air);
                const float fric = BlockRegistry::isIce(sb) ? kItemIceFriction : kItemGroundFriction;
                const float decay = std::exp(-fric * float(dt)); // 帧率无关指数衰减
                e.vx *= decay;
                e.vz *= decay;
                if (std::fabs(e.vx) < 0.05f) e.vx = 0.0f; // 停止阈值（防微观抖动永久微移）
                if (std::fabs(e.vz) < 0.05f) e.vz = 0.0f;
            }
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
