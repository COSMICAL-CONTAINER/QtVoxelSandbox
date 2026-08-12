#ifndef XPORBMANAGER_H
#define XPORBMANAGER_H

#include <QObject>
#include <QVector3D>
#include <QElapsedTimer>
#include <QtQml/qqml.h>

#include <vector>

// 经验球（XP orb）实体管理器（t402；Entities 层）。
//
// 机制等价 MC 1.0 经验球：杀怪 / 冶炼产出 → 在该位置 spawn 一个发光小球实体 →
// 玩家靠近时**磁吸**飞向玩家（auto-flies toward the player）→ 进吸附半径即拾取，
// 把球携带的 XP 数量经语义信号交给呈现层路由到 PlayerState.addXp（playerState xp 累积）。
//
// 与 ItemEntityManager（掉落物）同族但**不读 World**：经验球是纯磁吸实体（不落
// 重力、不被流冲、不入背包），spawn 后只向玩家飞。故 tick 只需玩家中心位（playerCenter，
// 由 PlayerController::tick 传 m_pos + 半高），无 World* 依赖 → 分层更轻（仅 Core/Qt +
// QVector3D 数学，PLAN §2 Entities 层最瘦形态）。
//
// 拾取链（PLAN §2 单向事件流，同 spawnItem→掉落物 / fallDamageTaken→PlayerState 模式）：
//   XpOrbManager.tick 内吸附命中 → releaseSlot（销毁球）+ emit xpPickedUp(amount) →
//   Main.qml Connections 路由到 PlayerState.addXp + 拾取音。XpOrbManager 不持 PlayerState
//   （保持单向事件流、分层干净；同 PlayerController 不持 PlayerState 的先例）。
//
// 来源（spec t402）：
//   (a) 杀怪 —— Main.qml onMobDied 据 mobType 调 spawnOrb（敌对/被动各异：敌对掉 XP、
//       被动 0；机制等价 MC 1.0 杀怪掉经验）。
//   (b) 冶炼产出取走 —— FurnaceUI 检测输出槽减少 → emit xpAwarded → Main.qml 在玩家
//       附近 spawnOrb（铁锭 > 木炭，机制等价 MC 冶炼产经验；具体 XP 走 SmeltingRegistry::smeltXpReward）。
//
// 槽位复用（slot-reuse，同 ItemEntityManager / EntityManager t256 模式）：移除改 releaseSlot
//   标空 + 入 free list，不 erase-shift → count（= Repeater model）单调不降 → QML Repeater
//   不需销毁 3D delegate（lessons-learned t170/t256：reparent 后的 3D delegate count 减小
//   不销毁 → 高频 spawn/拾取抖动致 delegate 累积泄漏；slot 复用根治）。空槽 aliveAt=false →
//   delegate visible=false 隐藏；复用时 revision bump 重显重绑。高水位受 kCap 钳制。
class XpOrbManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(XpOrbManager)
    // count：当前槽位数（含已释放空槽；Repeater 作 int model）。NOTIFY entitiesChanged 驱动
    //   spawn 后 Repeater 追加新 delegate（不重建已有 → 动画连续不被打断）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：实体集版本号（随 spawn / 拾取 / 磁吸位移 自增）。供「触碰」绑定作 NOTIFY 触发器
    //   （同 ItemEntityManager.revision 模式）—— posAt/amountAt 是 Q_INVOKABLE 不被 NOTIFY 自动
    //   跟踪，需 { revision; posAt(i) } 显式建依赖，磁吸 / 拾取后绑定才重算。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit XpOrbManager(QObject *parent = nullptr);

    int count() const { return int(m_orbs.size()); }
    int revision() const { return m_revision; }
    // 当前**活体**球数（不含已释放空槽）。F3 draw-call 估算用它（空槽 delegate 隐藏不绘制）。
    Q_INVOKABLE int liveCount() const { return m_liveCount; }
    // 第 i 个槽位是否活体（呈现层 delegate visible 绑它：空槽隐藏）。越界 → false。
    Q_INVOKABLE bool aliveAt(int i) const;

    // 在方块格 (x,y,z)（整数坐标）生成一个携带 amount 点 XP 的经验球。位置存该格中心
    //   (x+0.5, y+0.5, z+0.5)。amount<=0 拒绝（无意义的空球）。达 kCap → 驱逐最老活体腾位
    //   （LRU，同 ItemEntityManager 模式）。bump revision + emit entitiesChanged → QML Repeater
    //   追加 delegate。
    Q_INVOKABLE void spawnOrb(int x, int y, int z, int amount);

    // 第 i 个球的世界坐标（呈现层 delegate 摆位绑它）。越界返回 (0,0,0)。
    Q_INVOKABLE QVector3D posAt(int i) const;
    // 第 i 个球携带的 XP 数量（呈现层据此调色 / 大小：大球更显眼）。越界返回 0。
    Q_INVOKABLE int amountAt(int i) const;

    // 存档 / 切世界：清空所有球（防上一世界经验球残留进新世界）。t437：改「释放全部活体槽位」而非
    //   「清空 vector」——保 slot-reuse 单调不变量（count 不降）。根因同 ItemEntityManager/EntityManager：
    //   旧 m_orbs.clear() 把 count→0，QML xpOrbHost Repeater 随之→0，但 reparent 进 xpOrbHost 的 3D delegate
    //   （QQuick3DNode）不进 QQuickRepeater 跟踪表 → Repeater 销毁不到 → delegate 永久成孤儿（lessons-learned
    //   t170），退存档→再进单调累积 → 内存只增不减、FPS 掉到个位数（"退存档再进仍卡"的直接根因；C++ 审计全
    //   clean，泄漏在 QML 场景图侧；t256 slot-reuse 修了「游玩期」却漏了「切世界 clearAll」断点）。改释放槽位：
    //   alive=false + 入 free list + liveCount=0，保留 vector → count 不降 → Repeater 不销毁 delegate（无孤儿）→
    //   下次进世界复用既有 delegate（aliveAt 翻 true + revision bump 重绑）。高水位受 kCap(64) 钳制，有界常驻开销
    //   远优于跨世界无界泄漏。仅释放活体槽（幂等）。emit entitiesChanged → QML 据 revision 翻释放槽 delegate
    //   visible=false 隐藏（不销毁）。
    Q_INVOKABLE void clearAll() {
        for (size_t i = 0; i < m_orbs.size(); ++i)
            if (m_orbs[i].alive) releaseSlot(int(i));
        emit entitiesChanged();
    }
    // t492 跨世界 delegate 泄漏修复：真正清空 vector（count→0）。clearAll 只标 alive=false（delegate 隐藏不销毁，
    //   保 count 单调修 t170），但高水位（爆炸杀多怪产大量经验球等）后这些 delegate 永久驻留场景图，重进世界仍
    //   在（仅隐藏）→ 持续吃场景图开销（同 itemHost/mobHost 族）。hardReset 让 count→0；但 reparent 的 3D delegate
    //   仍不被 Repeater 销毁（t170），故 caller（QML world-exit）须配套手动 destroy xpOrbHost 的 delegate 子节点
    //   （见 Main.qml clearEntDelegates）。**仅 world-exit / enterWorld 清旧段调**（非游玩期，无并发 spawn）。游玩期
    //   的拾取 / 寿命到期仍走 releaseSlot（保 t256 slot-reuse 不变量）。
    Q_INVOKABLE void hardReset() {
        m_orbs.clear(); m_freeSlots.clear(); m_liveCount = 0;
        ++m_revision; emit entitiesChanged();
    }

    // 磁吸 + 拾取（C++ 直调；PlayerController::tick 每帧调，常开、独立于捕获态——菜单 / 暂停时
    //   球仍向玩家飞 / 仍可拾取，世界模拟连续，同 ItemEntityManager::tick）。playerCenter = 玩家
    //   AABB 中心（脚底 + 半高，caller 传）。无球 → 早 return。
    //   每个活体球：3D 距玩家中心 < kPickupRange → releaseSlot + emit xpPickedUp(amount)（呈现层
    //   路由 PlayerState.addXp + 拾取音）；< kMagnetRange → 朝玩家加速飞（越近越快，机制等价 MC
    //   经验球磁吸）；否则静止悬浮（呈现层自发上下浮动动画）。任一球 pos 变 / 销毁 → 末尾
    //   bump revision + emit entitiesChanged（驱动 QML 位置绑定重算）。
    //   寿命到期驱逐先于磁吸（同 ItemEntityManager despawnExpired：漏拾的球 5min 后消失，防累积）。
    void tick(qreal dt, const QVector3D &playerCenter);

signals:
    void entitiesChanged(); // spawn / 拾取 / 磁吸位移 触发；驱动 count/revision + QML 绑定刷新
    // 拾取语义事件（tick 内吸附命中时发）：amount = 本球携带的 XP 数量。呈现层（Main.qml）
    //   Connections 路由到 PlayerState.addXp + AudioManager.playPickup（拾取音，复用掉落物拾取声）。
    //   分层（PLAN §2）：Entities 层发语义事件，呈现层只消费（同 spawnItem / fallDamageTaken 模式）。
    void xpPickedUp(int amount);

private:
    struct Orb {
        QVector3D pos;
        int amount = 1;          // 携带的 XP 数量（拾取时交 PlayerState.addXp）
        qint64 spawnMs = 0;      // 生成时刻（m_clock.elapsed()）；despawnExpired 算 age 用
        // 槽位占用标志（slot-reuse，同 ItemEntityManager::ItemEntity::alive）。true = 活体；
        //   false = 已释放空槽（在 m_freeSlots 中待复用）。放末位：聚合初始化 {pos,amount,spawnMs}
        //   不显式列 alive → 取默认 true（lessons-learned t256：alive 放末尾免聚合初始化错位）。
        bool alive = true;
    };
    std::vector<Orb> m_orbs;
    int m_revision = 0;
    QElapsedTimer m_clock; // 构造时 start()；spawn 记 elapsed、despawn 算 age（墙钟）

    // slot-reuse（同 ItemEntityManager / EntityManager t256 模式）：拾取 / despawn 改 releaseSlot
    //   标空 + 入 free list，不 erase-shift → count 单调不降 → Repeater 不需销毁 reparent 的 3D delegate。
    std::vector<int> m_freeSlots; // 已释放可复用的槽索引（LIFO）
    int m_liveCount = 0;          // 活体球数（= m_orbs.size() − 空槽数）

    int acquireSlot(Orb &&o)
    {
        int slot;
        if (!m_freeSlots.empty()) {
            slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            m_orbs[size_t(slot)] = std::move(o);
        } else {
            m_orbs.push_back(std::move(o));
            slot = int(m_orbs.size()) - 1;
        }
        ++m_liveCount;
        return slot;
    }
    void releaseSlot(int idx)
    {
        if (idx < 0 || idx >= int(m_orbs.size())) return;
        m_orbs[size_t(idx)].alive = false;
        m_freeSlots.push_back(idx);
        --m_liveCount;
    }
    void notifyChanged() { ++m_revision; emit entitiesChanged(); }
    // 寿命到期驱逐（每帧 tick 起始调，独立于玩家位 —— 菜单 / 暂停时球照常老化消失）。
    void despawnExpired();

    static constexpr int kCap = 64;                  // 球数上限（防溢出；经验球瞬时、并发低）
    static constexpr qint64 kDespawnMs = 300000;     // 球寿命（ms；5min，同掉落物；漏拾后退场）
    static constexpr float kMagnetRange = 6.0f;      // 磁吸触发半径（blocks；< 此朝玩家飞）
    static constexpr float kPickupRange = 0.9f;      // 拾取半径（blocks；< 此即吸附拾取）
    static constexpr float kMinSpeed = 2.5f;         // 磁吸远端速度（blocks/s）
    static constexpr float kMaxSpeed = 9.0f;         // 磁吸近端速度（blocks/s；越近越快）
};

#endif // XPORBMANAGER_H
