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

    // 在方块格 (x,y,z)（整数坐标）生成一个 itemId 的掉落实体。位置存该格中心
    // (x+0.5, y+0.5, z+0.5)（实体悬浮在格中央）。达到 kCap → 跳过 + qWarning（防溢出）。
    // itemId<=0（air / 非法）拒（caller 应已过滤；双保险）。count 为实体携带数量（t64：整栈
    // 丢弃为 1 实体；缺省 1 = 单件，与历史调用兼容）；count<=0 视作 1，>maxStack 由 caller 分流
    // （本类不查 maxStack —— PlayerController 拾取时把全数交 Hotbar.addStack 自然分流到多槽）。
    Q_INVOKABLE void spawnItem(int x, int y, int z, int itemId, int count = 1);

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
    // t176 存档：清空所有掉落实体（切世界 / 退出存档前调，防上一世界的掉落物残留进新世界）。emit
    //   entitiesChanged → count=0 → QML Repeater 清空 delegate。
    Q_INVOKABLE void clearAll() { m_entities.clear(); emit entitiesChanged(); }

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
        bool resting = false;// t60：是否已落在实体方块顶面（resting 跳过重力，仅复探支撑格）
    };
    std::vector<ItemEntity> m_entities;
    int m_revision = 0;
    QElapsedTimer m_clock; // 构造时 start()；spawn 记 elapsed、拾取算 age（墙钟，暂停期照常流逝无残留锁）

    static constexpr int kCap = 200;             // 实体数上限（spec：>200 跳过 / 合并）
    static constexpr qint64 kPickupDelayMs = 500; // 新生免拾取期（ms；t53 让实体先可见再可拾）
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
};

#endif // ITEMENTITYMANAGER_H
