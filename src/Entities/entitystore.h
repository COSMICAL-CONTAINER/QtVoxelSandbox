#ifndef ENTITYSTORE_H
#define ENTITYSTORE_H

// R20.14 EntityStore（refactor-plan §29.3 R20.14「先迁移掉落物或一个简单实体族，不要同时
// 迁移全部生物」）：掉落物族（ItemEntityManager 所辖）模拟**单一权威**——spawn 物理（弹出
// 初速 / 重力 / 摩擦）/ 就近合并 / LRU 驱逐 / 拾取（部分 / 全数）/ 自然 despawn / 岩浆·火·
// 仙人掌焚毁 / 批量收口（t354）/ 槽位复用（t256）自 ItemEntityManager（src/Game）整体搬移
// 到此（逐行搬移非复制——section18 等价腿族守）。
//
// ── 验收对照（plan §29.3 R20.14 原文四条）──────────────────────────────────────────
// ① 「EntityId 稳定」：store 侧每活体在 acquireSlot 时刻获配 **EntityId**（quint32，自 1
//    单调递增、永不复用）——实体存活期内恒定，跨拾取 / despawn / 槽位复用不回收（后续
//    Event::entityId（quint32，event.h「0 = 非实体域」）与存档域的对接口径）。
//    **槽位下标 ≠ EntityId，双轨语义过渡**（头注立此存照）：槽位复用 LIFO 是 t256/t978
//    既有设计——槽下标在存活期内稳定、释放后 LIFO 复用、count（=槽表长）单调不降，是
//    QML Repeater 消费面（count 属性 + aliveAt/posAt/itemIdAt/countAt 槽下标寻址）的
//    **承重契约，本单迁移逐位保留**；EntityId 是 plan 级稳定 id（事件 / 存档 / 未来跨族
//    统一），与槽位解耦——同一槽位先后两任实体 EntityId 必异。QML 消费面零变化由
//    ItemEntityManager 过渡 Adapter 承接（见其头注）。
// ② 「模拟实体不依赖 QML」：本类非 QObject、零 QtQml/QtQuick include、零信号——纯值
//    语义模拟核心（result.h QObjectFree 编译期钉）；世界读经 World* 参数（向下只读
//    blockAt/stateAt/isCollidable/supportTopYAt，同 ItemEntityManager 旧分层纪律），变更
//    通知经 notifySink 回调缝（std::function）上行——QObject 信号面归 Adapter。
// ③ 「可见实体由 snapshot 决定」：EntityStoreSnapshot（值类型，全值成员自持可独立复制）
//    在每处 notifyChanged 沿 + clearAll 重建（m_visible 缓存，≤kCap 条目，成本登记）——
//    活体可见集（槽位 / EntityId / pos / itemId / count / resting / 附魔 / 名 / 耐久全可见
//    面）以快照为权威表达。**本单最小实现边界**：QML/instancing feeder（BlockDropInstancing
//    等）仍经 Adapter 逐槽读（消费面语义零变化是本单承重墙），feeder 改读快照属渲染管线
//    大迁移 = 后续单（登记非目标，按 plan 原文边界诚实划线）。
// ④ 「旧 EntityManager 可以作为过渡 Adapter」：ItemEntityManager 收敛为过渡 Adapter——
//    全部 Q_INVOKABLE / Q_PROPERTY / entitiesChanged 信号面零变化，方法体逐个委托本类
//    （m_store. 一行委托，不复制逻辑）；渲染家族谓词（isItem3DFamily 等静态权威）留
//    Adapter（渲染域非模拟域，t1027b/t1041d 源钉钉在该文件路径）。
//
// 分层（PLAN §2）：src/Entities——「Entities: 玩家 / 生物 / 掉落物」层（Game/Physics 之下、
// World 之上）；向下只读 World + Core（blockregistry/result），不依赖 Game/Renderer/QML；
// ItemEntityManager（src/Game 呈现 ViewModel 层）向下依赖本类（Game→Entities 合规向下）。
// 放置依据：模拟权威应居 Entities 层（ItemEntityManager 头注既有自述「本层属 Entities」——
// 它只是暂居 Game 的呈现适配器；真权威归位 Entities，Adapter 留 Game 供 QML/feeder 消费）。

#include "blockregistry.h"    // BlockRegistry id/maxStackSize/waterSurfaceFrac/isIce（Core 向下合规）
#include "result.h"           // QObjectFree（值纪律编译期钉，R20.06 起值类型纪律）

#include <QtGlobal>   // quint8 / quint32 / qint64
#include <QElapsedTimer> // 墙钟（拾取延迟 / 5min 寿命真值源；非 QObject 值类型）
#include <QVariantList>  // enchantsAt 元数据返回面（Adapter Q_INVOKABLE 同型转发）
#include <QVector3D>

#include <functional> // notifySink 回调缝（非 QObject 的上行通道）
#include <QString>
#include <vector>

class World; // 前向声明（tick 只读 World 查询族；完整定义在 .cpp include）

// ── EntityStoreSnapshot：可见实体快照（验收③；plan §4.2 Snapshot 纪律逐项落位）──────
// 自包含 = 全值成员；不携带 QObject = QObjectFree 钉；不引用可能失效的内存 = 无指针 /
// 无引用成员；对外不可变 = 只读观察面（findSlot 查询）。重建时机 = 每 notifyChanged 沿
// + clearAll（notifyChanged 沿语义见 EntityStore::notifyChanged 注）。
struct EntityStoreEntry
{
    int slot = -1;          // 槽位下标（呈现寻址 = QML Repeater 面；LIFO 复用语义见头注）
    quint32 entityId = 0;   // 稳定 EntityId（验收①；0 = 非实体域哨兵，对齐 event.h）
    QVector3D pos;          // 世界坐标（实体中心）
    int itemId = 0;
    int count = 1;
    bool resting = false;   // 着地态（压力板门控可见面）
    int enchants[4] = { 0, 0, 0, 0 }; // 附魔可见面（光晕渲染）
    QString name;                     // 自定义名（可见面完整性；空 = 注册表默认）
    int durability = -1;              // 实例耐久（-1 = 未初始化）
};

struct EntityStoreSnapshot
{
    qint64 revision = 0;               // 快照对应 store revision（采集时刻定格）
    std::vector<EntityStoreEntry> entries{}; // 恰含活体槽（空槽不入 = 可见集由快照决定）

    // 槽位寻址查询（不可变观察面）；无该槽 / 空槽 → nullptr。
    const EntityStoreEntry *findSlot(int slot) const
    {
        for (const EntityStoreEntry &e : entries)
            if (e.slot == slot)
                return &e;
        return nullptr;
    }

    void clear()
    {
        revision = 0;
        entries.clear();
    }
};

// 值纪律编译期钉（同 WorldSnapshot/Event 先例）：快照可独立复制（拷贝即深拷贝）。
static_assert(QObjectFree<EntityStoreSnapshot>, "EntityStoreSnapshot must not carry QObject (plan §4.2)");
static_assert(QObjectFree<EntityStoreEntry>, "EntityStoreEntry must not carry QObject (plan §4.2)");
static_assert(std::is_copy_constructible_v<EntityStoreSnapshot>
                  && std::is_copy_assignable_v<EntityStoreSnapshot>,
              "EntityStoreSnapshot must be independently copyable (plan §29.3 R20.14)");

// ── EntityStore：掉落物族模拟单一权威（非 QObject 值语义组件）──────────────────────
// 方法语义与 ItemEntityManager 旧实现逐位一致（逐行搬移）——行为等价由矩阵 section18
// r2014a 孪生腿守（store 直驱 vs Adapter 面同操作序列逐位一致）。
class EntityStore
{
public:
    EntityStore();

    // ── 观察面（Adapter Q_INVOKABLE 逐一转发；越界语义与旧实现逐位一致）──
    int count() const { return int(m_entities.size()); }
    int revision() const { return m_revision; }
    int liveCount() const { return m_liveCount; }
    int liveHighWater() const { return m_liveHighWater; }
    bool aliveAt(int i) const;
    bool restingAt(int i) const;
    QVector3D posAt(int i) const;
    int itemIdAt(int i) const;
    int countAt(int i) const;
    QVariantList enchantsAt(int i) const; // QVariantList<int> 4 元素（0 = 空槽）
    QString nameAt(int i) const;
    int durabilityAt(int i) const;
    // 稳定 EntityId 观察（store 级新面；槽无效 / 空槽 → 0 哨兵）。
    quint32 entityIdAtSlot(int i) const;
    // 可见实体快照（验收③权威表达；notify 沿 / clearAll 自动重建，此处只读返回）。
    const EntityStoreSnapshot &snapshot() const { return m_visible; }

    // ── 变更面（模拟权威；物理 / 合并 / 拾取 / despawn / 焚毁）──
    //   缺省参与 Adapter Q_INVOKABLE 面一一对应（直驱消费方同参可用）。
    void spawnItem(int x, int y, int z, int itemId, int count = 1,
                   const QVariantList &enchants = {}, const QString &name = QString(),
                   int durability = -1);
    void spawnItemAt(const QVector3D &pos, int itemId, int count,
                     float dirX, float dirZ, float speed,
                     const QVariantList &enchants = {}, const QString &name = QString(),
                     int durability = -1);
    void spawnItemThrown(const QVector3D &pos, int itemId, int count,
                         float dirX, float dirY, float dirZ, float speed,
                         const QVariantList &enchants = {}, const QString &name = QString(),
                         int durability = -1);
    void setCountAt(int i, int n);  // n<=0 → 释放槽位（全拾走）
    void removeAt(int i);
    // 重置语义：释放全部活体槽 + **无条件**通知（不经批——同旧 clearAll 直 emit）。
    void clearAll();

    bool isPickupReady(int i) const;
    void deferWallClocks(qint64 ms);
    // 寿命时钟注入缝（t1029 测试缝纪律：与后续 RNG/时钟注入设计兼容；deferWallClocks 的对偶
    //   ——+ms 顺延只能冻结寿命，无法老化）：把全部活体 spawnMs 前移 ms（寿命与免拾窗同步
    //   过期，等价墙钟真老化）。**生产路径零调用**（矩阵 r2014 despawn 腿专用）。
    void ageLifetimeClock(qint64 ms);
    void tick(qreal dt, World *world);

    // ── 批量收口（t354；depth 可嵌套）──
    void beginBatch() { ++m_batchDepth; }
    // 退出批量：depth 归 0 且批内有 dirty → 1 次通知收口（经 sink）。返回是否发生收口
    // （Adapter 无感——sink 即其 emit；返回值供 store 级测试断言）。
    bool endBatch();
    bool batchActive() const { return m_batchDepth > 0; }

    // ── 通知缝（②模拟不依赖 QML 的上行通道）：Adapter 构造时设
    //    setNotifySink([this]{ emit entitiesChanged(); })——本类零信号零 QObject。
    //    未设（裸 store 测试 / 未来非 Qt 消费方）时通知退化为纯 revision/快照推进。
    using NotifySink = std::function<void()>;
    void setNotifySink(NotifySink sink) { m_notify = std::move(sink); }

private:
    struct ItemEntity {
        QVector3D pos;
        int itemId;
        int count = 1;
        qint64 spawnMs = 0;
        float vy = 0.0f;
        float vx = 0.0f;
        float vz = 0.0f;
        bool resting = false;
        int enchants[4] = { 0, 0, 0, 0 };
        QString name = QString();
        int durability = -1;
        quint32 entityId = 0; // R20.14：acquireSlot 时刻获配（自 1 单调，永不复用——验收①）
        bool alive = true;    // 末位：聚合初始化 {pos,itemId,count,spawnMs} 不显式列 → 默认 true
    };
    std::vector<ItemEntity> m_entities;
    int m_revision = 0;
    int m_batchDepth = 0;
    bool m_batchDirty = false;
    QElapsedTimer m_clock;
    std::vector<int> m_freeSlots; // 已释放可复用槽索引（LIFO；t256/t978 既有设计，QML 面承重）
    int m_liveCount = 0;
    int m_liveHighWater = 0;
    quint32 m_nextEntityId = 1;   // 稳定 EntityId 分配游标（验收①；0 保留哨兵）
    EntityStoreSnapshot m_visible; // 可见实体快照缓存（notify 沿 / clearAll 重建——验收③）
    NotifySink m_notify;           // 变更上行缝（Adapter 的 emit；可空）

    int acquireSlot(ItemEntity &&e); // 获配槽位（LIFO 复用优先）+ EntityId；返回槽下标
    void releaseSlot(int idx);
    void despawnExpired();
    // 变更收口单点：++revision + 快照重建（验收③沿）+ 按批态上行（非批即 sink / 批内标脏）。
    void notifyChanged();
    void rebuildVisibleSnapshot();

    // ── 模拟常量（自 ItemEntityManager 逐值搬移；数值即权威，改值须过等价腿族）──
    static constexpr int kCap = 200;               // 实体数上限（spec：>200 跳过 / 合并）
    static constexpr qint64 kPickupDelayMs = 500;  // 新生免拾取期（ms）
    static constexpr qint64 kDespawnMs = 300000;   // 自然寿命（ms；5 min，机制等价 MC）
    static constexpr float kGravity = 28.0f;       // 重力加速度（blocks/s²）
    static constexpr float kMaxFall = 78.4f;       // 终端下落速度
    static constexpr float kRestOffset = 0.3f;     // 落地中心相对支撑顶面偏移
    static constexpr float kItemRiseSpeed = 2.5f;   // 浮水恒速上浮
    static constexpr float kItemFlowSpeed = 2.0f;   // 流水水平推移
    static constexpr float kItemFloatOffset = 0.05f;// 浮水中心下沉量（防抖出水面）
    static constexpr float kItemPopSpeed = 2.0f;        // 生成弹出速度
    static constexpr float kItemGroundFriction = 6.0f;  // 常规地面摩擦衰减率（1/s）
    static constexpr float kItemIceFriction = 0.4f;     // 冰面摩擦衰减率（1/s）
    static constexpr float kMergeRadius = 2.0f;         // 就近合并半径（格）
};

// 值纪律编译期钉：模拟核心不携带 QObject（验收②「模拟实体不依赖 QML」的类型层形态）。
static_assert(QObjectFree<EntityStore>, "EntityStore must not carry QObject (plan §29.3 R20.14)");

#endif // ENTITYSTORE_H
