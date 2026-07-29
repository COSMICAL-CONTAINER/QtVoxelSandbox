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
// 数据形态（pos / id / 物理态）：每个实体 = {世界坐标 pos, 物品 id, 垂直速度 vy, 是否已落地 resting}。
// 呈现层（Main.qml 的 Repeater）经 count + posAt + itemIdAt 读数据，自发旋转 / 浮动动画（不反向写）。
// 拾取（t36）：PlayerController 每帧扫附近实体 → Hotbar.addStack 成功 → removeAt 销毁该实体。
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
    // itemId<=0（air / 非法）拒（caller 应已过滤；双保险）。
    Q_INVOKABLE void spawnItem(int x, int y, int z, int itemId);

    // 第 i 个实体的世界坐标（呈现层 Repeater delegate 绑它摆位）。越界返回 (0,0,0)。
    Q_INVOKABLE QVector3D posAt(int i) const;
    // 第 i 个实体的物品 id（呈现层据它设 BlockCube.blockId）。越界返回 0。
    Q_INVOKABLE int itemIdAt(int i) const;
    // 销毁第 i 个实体（t36 拾取后调用）。erase-shift（保持其余实体位置 / 索引连续；非 swap-remove，
    // 否则末位 delegate 会瞬移到被拾取位 → 视觉跳变）。越界静默。bump revision → QML Repeater
    // delegate 的 posAt/itemIdAt 绑定（触碰 revision）整列重算，shift 后各 delegate 对齐新数据。
    Q_INVOKABLE void removeAt(int i);

    // t53：第 i 个实体是否已过「新生免拾取期」（spawn 后 kPickupDelayMs 内 false → pickupScan 跳过）。
    // 破块瞬间实体常落在玩家近旁（如脚下方块中心距玩家中心仅 ~1.4 格 < kPickupDist 1.5），若无免拾窗
    // 则下一帧即被 pickupScan 收走、玩家永远看不见实体（用户反馈「仍 auto-collect 入背包」的根因）。
    // 加 0.5s 免拾窗（机制等价 MC block-break 的短暂 pickup delay）让实体先可见、再入背包。越界 /
    // 时钟未启 → true（保守可拾，防卡死、防延迟机制误伤合法拾取）。
    bool isPickupReady(int i) const;

    // t60 掉落物重力：每帧推进所有未落地实体的垂直运动。vy -= g*dt（钳 -kMaxFall），按 dy 下移 pos.y，
    // 下移路径上扫实体所在列（cx = floor(pos.x)、cz = floor(pos.z)）查首个实体方块 → 命中则贴其顶面停下
    // （pos.y = solidCellY + 1 + kRestOffset、vy=0、resting=true）。已 resting 的实体复探支撑格
    // （= floor(pos.y) - 1）仍实体才续落（防下方被挖后悬空）。任一实体 pos / resting 真变 → 末尾 bump
    // revision + emit entitiesChanged（驱动 QML delegate 的 {revision; posAt(index)} 绑定重算 →
    // 呈现位置实时下落；count 不变 → Repeater 不重建 delegate，旋转 / 浮动动画连续不被打断）。
    // world 为 null / 无实体 → 早 return（保守不动作）。**C++ 直调**（PlayerController::tick 每帧调），
    // 非 QML 调 → 不挂 Q_INVOKABLE（避开 moc 对 World* 前向类型的 metatype 处理）。
    void tick(qreal dt, World *world);

signals:
    void entitiesChanged(); // spawn / 未来 remove 触发；驱动 count/revision + QML 绑定刷新

private:
    struct ItemEntity {
        QVector3D pos;
        int itemId;
        qint64 spawnMs = 0; // 生成时刻（m_clock.elapsed()）；t53 isPickupReady 算 age 用
        float vy = 0.0f;        // t60：垂直速度（blocks/s；向下为负）；落地后归 0
        bool resting = false;   // t60：是否已落在实体方块顶面（resting 跳过重力，仅复探支撑格）
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
};

#endif // ITEMENTITYMANAGER_H
