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
// 数据形态（纯持有，无物理 / 渲染）：每个实体 = {世界坐标 pos, 物品 id}。呈现层（Main.qml
// 的 Repeater）经 count + posAt + itemIdAt 读数据，自发旋转 / 浮动动画（不反向写）。
// 拾取（t36）：PlayerController 每帧扫附近实体 → Hotbar.addStack 成功 → removeAt 销毁该实体。
// 丢弃（t36）：PlayerController Q 键 → 经 spawnItem 信号（onSpawnItem 转发）回流入本类 spawnItem。
//
// 实体数量有上限（防溢出，spec「>200 跳过 / 合并」）：达到上限 kCap 时新 spawn 被跳过 +
// 告警，保留已有实体（最简策略；合并 / LRU 推迟）。
//
// 分层（PLAN §2）：本层属 ViewModel，只依赖 Core（QtGlobal / QVector3D），**不**依赖
// Renderer / Physics / QtQuick3D / World。触发由 PlayerController（Game/Physics 层）发
// spawnItem 信号，Main.qml 的 Connections 转发到本类 spawnItem() —— 单向事件流，本类
// 不持有 PlayerController（保持分层干净，同 blockBroken→粒子 / fallDamageTaken→PlayerState 模式）。
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

signals:
    void entitiesChanged(); // spawn / 未来 remove 触发；驱动 count/revision + QML 绑定刷新

private:
    struct ItemEntity {
        QVector3D pos;
        int itemId;
        qint64 spawnMs = 0; // 生成时刻（m_clock.elapsed()）；t53 isPickupReady 算 age 用
    };
    std::vector<ItemEntity> m_entities;
    int m_revision = 0;
    QElapsedTimer m_clock; // 构造时 start()；spawn 记 elapsed、拾取算 age（墙钟，暂停期照常流逝无残留锁）

    static constexpr int kCap = 200;             // 实体数上限（spec：>200 跳过 / 合并）
    static constexpr qint64 kPickupDelayMs = 500; // 新生免拾取期（ms；t53 让实体先可见再可拾）
};

#endif // ITEMENTITYMANAGER_H
