#ifndef ITEMENTITYMANAGER_H
#define ITEMENTITYMANAGER_H

#include <QObject>
#include <QVector3D>
#include <QElapsedTimer>
#include <QtQml/qqml.h>

#include <vector>

// 方块掉落实体管理器（t35；Entities/Game ViewModel 层）。
//
// 生存模式破坏**可掉落**方块时，在该格生成一个 item entity（旋转 / 浮动的小方块图标），
// 等待 t36 拾取。创造秒破不产出（PlayerController 不发 spawnItem 信号）；生存不可采掘
// （canHarvest=false，如空手破石）也不产出。
//
// 数据形态（pos / id / count / 物理态）：每个实体 = {世界坐标 pos, 物品 id, 数量 count,
// 垂直速度 vy, 是否已落地 resting}。count 字段（t64）支持「整栈丢弃为 1 实体」（如 4 木棒丢出
// 仍为 1 实体 count=4）；拾取时把 count 全数交 Hotbar::addStack，装不下则 entity.count 留余数。
// 呈现层（Main.qml 的 Repeater）经 count + posAt + itemIdAt + countAt 读数据，自发旋转 / 浮动
// 动画（不反向写）；count>1 时 delegate 显数量数字。
// 拾取（t36）：PlayerController 每帧扫附近实体 → Hotbar.addStack 成功 → removeAt 销毁该实体；
//   t64：拾取按 entity.count 全数尝试入背包，余数（背包满）回写 entity.count，entity 保留。
// 丢弃（t36）：PlayerController Q 键 → 经 spawnItem 信号（onSpawnItem 转发）回流入本类 spawnItem。
//
// 实体数量有上限（防溢出，spec「>200 跳过 / 合并」）：达到上限 kCap 时新 spawn 被跳过 +
// 告警，保留已有实体（最简策略；合并 / LRU 推迟）。
//
// t60 掉落物重力：spawn 时实体悬浮在格中央（pos.y = y + 0.5），落地前每帧 tick() 施加重力
// （vy -= g*dt，钳到 -kMaxFall），下移后扫实体所在列查首个实体方块 → 落到其顶面停下（resting=true，
// vy=0）。落地后仍保留旋转 / 浮动动画（呈现层自发，不受 resting 影响）。落地后再检测到下方方块
// 被挖空（支撑格变空气）→ 解除 resting 续落（防悬空）。tick 由 PlayerController::tick() 每帧驱动
// （独立于玩家捕获态 → 菜单 / 暂停时世界照常模拟），传入 World* 做只读 solidity 查询。
//
// 分层（PLAN §2）：本层属 Entities（PLAN §2 分层「Entities: ... 掉落物」位于 Game / Physics 之下、
// World 之上）。t60 引入**向下**只读依赖 World（isSolid），方向合规（PLAN §2「依赖只向下」）；不依赖
// Renderer / Physics / QtQuick3D。spawnItem 触发由 PlayerController（Game/Physics 层）发信号，Main.qml
// 的 Connections 转发到本类 spawnItem() —— 单向事件流，本类不持有 PlayerController（同
// blockBroken→粒子 / fallDamageTaken→PlayerState 模式）。
class World; // 前向声明（t60 tick 只读 World::isSolid；完整定义在 .cpp include）
class ItemEntityManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ItemEntityManager)
    // count：当前实体数（Repeater 作 int model → 生成 0..count-1 delegate）。NOTIFY entitiesChanged
    // 驱动 spawn 后 Repeater 追加新 delegate（不重建已有 → 动画连续不被打断）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：实体集版本号（随 spawn / 未来 remove 自增）。供需要整列重建的消费者「触碰」
    // 绑定作 NOTIFY 触发器（同 Hotbar.slotRevision 模式）；当前 Repeater 直接用 count，预留。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit ItemEntityManager(QObject *parent = nullptr);

    int count() const { return int(m_entities.size()); }
    int revision() const { return m_revision; }
    // t256：当前**活体**实体数（不含已释放的空槽）。F3 draw-call 估算用它（空槽 delegate visible=false
    //   不参与绘制）。spawn 上限判定（kCap）也读它（空槽可复用，不算满）。
    Q_INVOKABLE int liveCount() const { return m_liveCount; }
    // t256：第 i 个槽位是否活体。呈现层 delegate 据它 visible：空槽隐藏（slot 复用保 Repeater count
    //   单调不降、delegate 永不销毁）。越界 → false。pickupScan 也据此跳过空槽。
    Q_INVOKABLE bool aliveAt(int i) const;

    // 在方块格 (x,y,z)（整数坐标）生成一个 itemId 的掉落实体。位置存该格中心
    // (x+0.5, y+0.5, z+0.5)（实体悬浮在格中央）。达到 kCap → 跳过 + qWarning（防溢出）。
    // itemId<=0（air / 非法）拒（caller 应已过滤；双保险）。count 为实体携带数量（t64：整栈
    // 丢弃为 1 实体；缺省 1 = 单件，与历史调用兼容）；count<=0 视作 1，>maxStack 由 caller 分流
    // （本类不查 maxStack —— PlayerController 拾取时把全数交 Hotbar.addStack 自然分流到多槽）。
    Q_INVOKABLE void spawnItem(int x, int y, int z, int itemId, int count = 1);

    // t354 批量 spawn 抑制 entitiesChanged（修 Stalker 爆炸「t320 已批 worldChanged 但仍卡」的复发根因）：
    //   一次爆炸按 kExplosionDropChance(~50%) 对球内每破坏块发 explosionDroppedItem → 呈现层逐个 spawnItem，
    //   而旧 spawnItem **每次** ++revision + emit entitiesChanged → N 个掉落物 = N 次 Repeater model(count) 变更 +
    //   N 轮「全体 delegate 触碰 revision 重算 4 绑定」= O(N²) 绑定重算 + N 次重 3D delegate 即时实例化
    //   （BlockCube / Billboard Canvas）→ 一帧数十 ms（FPS 崩 + 落地前每帧续卡）。t320 只批了 World 层的
    //   worldChanged（N 写 1 emit），漏了本 Game/Item 层的 entitiesChanged —— 即复发根因。
    //   beginBatch/endBatch 把同一次爆炸的 N 个 spawn 收口成末尾 1 次 emit（Repeater 一次补齐 N 个新 delegate、
    //   已存在 delegate 仅重算 1 轮）= O(N)。深度计数可嵌套；非爆炸的常规 spawn（玩家丢弃 / mob 死亡 / 落沙）
    //   不经批（depth=0）→ 立即 emit，行为不变。clearAll 不经批（重置语义，应即时通知）。机制等价 MC 爆炸
    //   一次性结算掉落而非逐块入世界。呈现层用法：第一发爆炸掉落信号 beginBatch、爆炸总结信号（onExplosion，
    //   detonateStalker 末尾恒发）endBatch 收口。
    Q_INVOKABLE void beginBatch(); // 进入批量：notifyChanged 仅标 dirty、不 emit（depth++）
    Q_INVOKABLE void endBatch();   // 退出批量：depth 归 0 且有 dirty → 1 次 emit；非批 / 无 dirty → no-op
    // t354 当前是否在批量区间（depth>0）。呈现层据此判「首发爆炸掉落」开批（仅 begin 一次），避免在 QML 维护
    //   跨 handler 的批量态（批态集中在 C++ m_batchDepth，单一事实源）。
    Q_INVOKABLE bool batchActive() const { return m_batchDepth > 0; }

    // 第 i 个实体的世界坐标（呈现层 Repeater delegate 绑它摆位）。越界返回 (0,0,0)。
    Q_INVOKABLE QVector3D posAt(int i) const;
    // 第 i 个实体的物品 id（呈现层据它设 BlockCube.blockId / 分流到 ToolIcon / MaterialIcon 外观）。
    // 越界返回 0。
    Q_INVOKABLE int itemIdAt(int i) const;
    // 第 i 个实体的数量（t64：呈现层 count>1 时显数字；PlayerController 拾取按它入背包）。越界返回 0。
    Q_INVOKABLE int countAt(int i) const;
    // 把第 i 个实体的数量设为 n（t64：拾取装不下时把余数回写、保留 entity）。n<=0 销毁该实体
    // （余数为 0 = 全拾走）。边界安全（越界静默）。仅 PlayerController::pickupScan 调（拾取路径），
    // 非 QML 调用入口。bump revision 驱动 QML delegate 数量绑定重算。
    Q_INVOKABLE void setCountAt(int i, int n);
    // 销毁第 i 个实体（t36 拾取后调用）。erase-shift（保持其余实体位置 / 索引连续；非 swap-remove，
    // 否则末位 delegate 会瞬移到被拾取位 → 视觉跳变）。越界静默。bump revision → QML Repeater
    // delegate 的 posAt/itemIdAt 绑定（触碰 revision）整列重算，shift 后各 delegate 对齐新数据。
    Q_INVOKABLE void removeAt(int i);
    // t176 存档：清空所有掉落实体（切世界 / 退出存档前调，防上一世界的掉落物残留进新世界）。
    //   t437：改「释放全部活体槽位」而非「清空 vector」——保 slot-reuse 单调不变量（count 不降）。根因同
    //   EntityManager::clearAll：旧 m_entities.clear() 把 count→0，QML itemHost Repeater 随之→0，但 reparent
    //   进 itemHost 的 3D delegate（QQuick3DNode）不进 QQuickRepeater 跟踪表、所有权已转给 itemHost → Repeater
    //   销毁不到 → delegate 永久成孤儿（lessons-learned t170）。每次退存档→再进都把上一世界全部掉落物 delegate
    //   孤儿化 + 新世界从 0 重建 → 跨世界单调累积 → 内存只增不减、FPS 掉到个位数（"退存档再进仍卡"的直接根因；
    //   C++ 审计全 clean，泄漏在 QML 场景图侧；t256 已用 slot-reuse 修「游玩期」却漏了「切世界 clearAll」断点）。
    //   改释放槽位：alive=false + 入 free list + liveCount=0，保留 vector → count 不降 → Repeater 不销毁 delegate
    //   （无孤儿）→ 下次进世界复用既有 delegate（aliveAt 翻 true + revision bump 重绑）。高水位受 kCap(200) 钳制，
    //   有界常驻开销远优于跨世界无界泄漏。仅释放活体槽（幂等）。emit entitiesChanged → QML 据 revision 翻释放槽
    //   delegate visible=false 隐藏（不销毁）。
    Q_INVOKABLE void clearAll() {
        for (size_t i = 0; i < m_entities.size(); ++i)
            if (m_entities[i].alive) releaseSlot(int(i));
        emit entitiesChanged();
    }

    // t53：第 i 个实体是否已过「新生免拾取期」（spawn 后 kPickupDelayMs 内 false → pickupScan 跳过）。
    // 破块瞬间实体常落在玩家近旁（如脚下方块中心距玩家中心仅 ~1.4 格 < kPickupDist 1.5），若无免拾窗
    // 则下一帧即被 pickupScan 收走、玩家永远看不见实体（用户反馈「仍 auto-collect 入背包」的根因）。
    // 加 0.5s 免拾窗（机制等价 MC block-break 的短暂 pickup delay）让实体先可见、再入背包。越界 /
    // 时钟未启 → true（保守可拾，防卡死、防延迟机制误伤合法拾取）。
    bool isPickupReady(int i) const;

    // t60 掉落物重力 / t271 水冲走掉落物：每帧推进所有实体的物理（由 PlayerController::tick 每帧调，
    //   常开、独立于捕获态——菜单/暂停时世界照常模拟）。**C++ 直调**（非 QML 调 → 不挂 Q_INVOKABLE，
    //   避开 moc 对 World* 前向类型的 metatype 处理）。world 为 null / 无实体 → 早 return（保守不动作）。
    //
    //   t60 空气重力：未入水时 vy -= g*dt（钳 -kMaxFall），按 dy 下移 pos.y，下移路径上扫实体所在列
    //   （cx = floor(pos.x)、cz = floor(pos.z)）查首个实体方块 → 命中则贴其顶面停下（pos.y =
    //   solidCellY + 1 + kRestOffset、vy=0、resting=true）。已 resting 的实体复探支撑格仍实体才续落
    //   （防下方被挖后悬空）。任一实体 pos / resting 真变 → 末尾 bump revision + emit entitiesChanged
    //   （驱动 QML delegate 的 {revision; posAt(index)} 绑定重算 → 呈现位置实时；count 不变 → Repeater
    //   不重建 delegate，旋转 / 浮动动画连续不被打断）。
    //
    //   t271 入水（中心格 == Water）：
    //     (a) **浮水面**（buoyancy，非瀑布）：扫该列自中心格向上找「最顶水格」（其上为非水 = 水面），
    //         目标静止 Y = surfCellY + 1 - kItemFloatOffset（中心贴水面、留在水格内防 floor 抖出空气→
    //         下帧误判离水→重力回落的振荡）。中心在目标下方 → 以恒速 kItemRiseSpeed 上浮（机制等价 MC
    //         掉落物水中缓浮）；到水面 → 钳到目标、vy=0。**瀑布例外**（水格下方为空气 = 水柱下落）→ 不上浮，
    //         fall-through 到重力分支随水柱下沉（机制等价 MC 掉落物被瀑布带下；落入下方水池后转浮水）。
    //     (b) **随流移动**（flow push）：仅流水格（state>0；水源 state=0 静止不推，spec）→ 据 4 向邻居
    //         state 梯度推算「离源方向」（state 低于本格的邻居 = 近源 → 推力朝远离它），归一化后 ×
    //         kItemFlowSpeed × dt 直接叠入水平位移（与 PlayerController t211 玩家水流推力同源算法）。
    //         水平位移前查目标格 isCollidable（实体碰撞）→ 撞墙 / 撞半砖该轴不动（per-axis 试探让实体沿墙
    //         滑动而非卡死）。浮水 + 瀑布均施（水柱底部漫流仍横向带）。
    //     **关键修正（t271）**：t60 列扫用 World::isSolid（=非 air，含 Water）会让掉落物停在水面上当成地面；
    //       改为 isSolid && blockAt != Water（水视作穿透，机制等价 t220「水不挡沙」），掉落物由此穿水面
    //       入水 → 下帧中心格变 Water → 转浮水分支上浮（而非粘在水面当着地）。
    //   分层（PLAN §2）：本层属 Entities/Game，向下只读 World（blockAt/stateAt/isSolid/isCollidable），
    //     不依赖 Renderer/Physics/QtQuick3D。
    void tick(qreal dt, World *world);

signals:
    void entitiesChanged(); // spawn / 未来 remove 触发；驱动 count/revision + QML 绑定刷新

private:
    struct ItemEntity {
        QVector3D pos;
        int itemId;
        int count = 1;       // t64：实体携带数量（整栈丢弃为 1 实体；拾取按此数入背包）
        qint64 spawnMs = 0;  // 生成时刻（m_clock.elapsed()）；t53 isPickupReady 算 age 用
        float vy = 0.0f;     // t60：垂直速度（blocks/s；向下为负）；落地后归 0
        // t468 水平速度（blocks/s）：掉落物生成时带初始「弹出」水平速度（机制等价 MC 破块 / 丢弃物品弹出），
        //   每 tick 积分位移 + 摩擦衰减。冰面摩擦极低 → 持续滑动（spec「冰上丢弃物品会一直滑动往前」）；
        //   常规地面摩擦高 → 快速停下。放 resting 之前（alive 仍须末位，保聚合初始化 tail-default）。
        float vx = 0.0f;
        float vz = 0.0f;
        bool resting = false;// t60：是否已落在实体方块顶面（resting 跳过重力，仅复探支撑格）
        // t256：槽位占用标志（slot-reuse 模型，同 EntityManager::Entity::alive）。true = 活体；false = 已释放
        //   空槽（待复用）。放末位：spawnItem 的聚合初始化 {pos,itemId,count,spawnMs} 不显式列 alive →
        //   取默认 true（C++ 聚合初始化尾字段缺省即 default member init）。掉落物被拾取（removeAt /
        //   setCountAt(0)）后 releaseSlot → alive=false；下次 spawnItem 复用空槽。tick / pickupScan 跳过
        //   空槽；呈现层 delegate visible:aliveAt(index)。
        bool alive = true;
    };
    std::vector<ItemEntity> m_entities;
    int m_revision = 0;
    // t354 批量 spawn 抑制 emit（见 beginBatch 注释）：depth>0 时 notifyChanged 只标 dirty 不 emit；
    //   endBatch 归 0 且 dirty → 1 次 emit 收口 N 个累积变更。非批（depth=0）时 notifyChanged 立即 emit，行为同旧。
    int m_batchDepth = 0;
    bool m_batchDirty = false;
    QElapsedTimer m_clock; // 构造时 start()；spawn 记 elapsed、拾取算 age（墙钟，暂停期照常流逝无残留锁）

    // t256 slot-reuse（修掉落沙衍生掉落物 delegate 泄漏；机制同 EntityManager，详见其注释）：实体移除
    //   （拾取 / setCountAt(0)）不再 erase-shift，而 releaseSlot 标 alive=false + 入 free list；下次 spawn
    //   复用空槽。m_entities.size()（=count 属性 = QML Repeater model）单调不降 → Repeater 不需销毁 reparent
    //   的 3D delegate（lessons-learned t170：reparent 后的 3D delegate count 减小不销毁 → 掉落物 spawn/拾取
    //   抖动致 delegate 累积泄漏）。沙落不完整方块变掉落物（fallingBlockDropped → spawnItem）+ 生存挖掘产出
    //   均频繁 spawn/拾取，同族泄漏；slot 复用根治。高水位受 kCap(200) 钳制，与既有峰值并发同量级。
    std::vector<int> m_freeSlots; // 已释放可复用的槽索引（LIFO）
    int m_liveCount = 0;          // 活体实体数（= m_entities.size() − 空槽数）；spawn 上限 + F3 draw 估算读它

    int acquireSlot(ItemEntity &&e)
    {
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
        return slot;
    }
    void releaseSlot(int idx)
    {
        if (idx < 0 || idx >= int(m_entities.size())) return;
        m_entities[size_t(idx)].alive = false;
        m_freeSlots.push_back(idx);
        --m_liveCount;
    }
    // t320 自然寿命到期驱逐（每帧 tick 调，独立于 world —— 菜单 / 暂停时仍消失）。扫所有活体，age（m_clock.elapsed()
    //   − spawnMs）> kDespawnMs → releaseSlot（同拾取路径，aliveAt=false → delegate 隐藏 + 槽位可复用）。任一驱逐
    //   → bump revision + emit entitiesChanged（驱动 QML 隐藏对应 delegate）。空集合 / 无到期 → no-op。
    void despawnExpired();
    // t354 批量 emit 收口（见 beginBatch 注释）：实体集每次变更（spawn / setCount / remove / tick dirty /
    //   despawn dirty）统一走此。++revision 恒做（delegate 触碰 revision 取最新值）；emit 仅在非批（depth<=0）时发，
    //   批内仅标 dirty、由 endBatch 末尾 1 次 emit 收口。clearAll 走独立直 emit（重置语义、不经批）。
    void notifyChanged();

    static constexpr int kCap = 200;             // 实体数上限（spec：>200 跳过 / 合并）
    static constexpr qint64 kPickupDelayMs = 500; // 新生免拾取期（ms；t53 让实体先可见再可拾）
    // t320 掉落物自然寿命（ms；机制等价 MC 1.0 掉落物 5 分钟后消失）。tick() 内扫所有活体，age > kDespawnMs
    //   → releaseSlot 移除（同拾取路径，aliveAt=false → delegate 隐藏 + 槽位可复用）。修爆炸后掉落物累积致
    //   FPS / 内存崩塌：单次爆炸可产出数十个掉落物，多次爆炸 + 无自然消失 → 累积到 kCap(200) 高水位 →
    //   数百 3D delegate 长期占驻 QQuick3D 场景图（slot-reuse 模型下 count 单调不降，达到 200 后永不回落）。
    //   5 min 寿命让活跃掉落物在玩家走开 / 漏拾后自然退场，常态 active count 维持低个位到几十，远低于 cap。
    static constexpr qint64 kDespawnMs = 300000;  // 掉落物寿命（ms；5 min，机制等价 MC 掉落物 5 min 消失）
    // t60 物理常量（与 PlayerController 同值：保持世界重力手感一致；lessons-learned「重力/跳跃常量」）。
    static constexpr float kGravity = 28.0f;  // 重力加速度（blocks/s²）
    static constexpr float kMaxFall = 78.4f;  // 终端下落速度（blocks/s；防无限加速）
    // 落地后实体中心相对支撑方块顶面的静止偏移（格）。图标 Model scale 0.3（半高 0.15）→ 中心高于顶面
    // 0.3 时图标底贴顶面 +0.15、留余量给浮动动画（bobY 0..0.15）不穿地；半透外壳 scale 0.45 略大无碍。
    static constexpr float kRestOffset = 0.3f;
    // t271 水冲走掉落物（spec「item 掉落物入水→浮水面 + 随流移动」）常量：
    //   kItemRiseSpeed：水中浮力上浮速度（blocks/s；恒速，机制等价 MC 掉落物水中缓浮——不取加速度模型
    //     是为避免「深水→陡升→水面→急刹」的机械感，恒速上升视觉更接近 MC 静稳上浮）。
    //   kItemFlowSpeed：流水水平推移速度（blocks/s；流水格 state>0 沿离源方向直接叠入水平位移）。比玩家
    //     kWaterFlowPush（4.0，每 tick 叠入速度且玩家有移动阻尼）取小——掉落物无水平阻尼，直接积分位移，
    //     小值保「被流走可见但不火箭」。
    //   kItemFloatOffset：浮水静止时中心距水面（cell 顶）的下沉量（格）；留中心在水格内防 floor 抖出
    //     空气 → 下一帧误判离水 → 重力回落的振荡（中心贴近水面、略没入水中，机制等价 MC 掉落物贴水面浮）。
    static constexpr float kItemRiseSpeed   = 2.5f;
    static constexpr float kItemFlowSpeed   = 2.0f;
    static constexpr float kItemFloatOffset = 0.05f;
    // t468 掉落物水平弹出 + 冰面滑动常量（spec「冰上丢弃物品会一直滑动往前」）：
    //   kItemPopSpeed：生成时初始水平弹出速度（blocks/s；机制等价 MC 破块 / 丢弃物品弹出方向随机、幅值小）。
    //     确定性哈希（位置 + itemId）给每件一个固定方向 + 幅值抖动 → 同一掉落可复现，非运行期随机源。
    //   kItemGroundFriction：常规地面水平摩擦衰减率（1/s；exp(-rate*dt) 衰减；6 → ~0.5s 基本停下）。
    //   kItemIceFriction：冰面水平摩擦衰减率（1/s；0.4 → 滑行 ~数秒，机制等价 MC 冰上物品长滑）。
    //     冰上摩擦远低于常规地面 → 用户肉眼「冰上丢弃物品会一直滑动往前」。
    static constexpr float kItemPopSpeed      = 2.0f;
    static constexpr float kItemGroundFriction = 6.0f;
    static constexpr float kItemIceFriction    = 0.4f;
};

#endif // ITEMENTITYMANAGER_H
