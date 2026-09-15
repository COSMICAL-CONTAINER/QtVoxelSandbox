#ifndef ITEMENTITYMANAGER_H
#define ITEMENTITYMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QVector3D>
#include <QtQml/qqml.h>

#include "entitystore.h"     // R20.14：掉落物模拟单一权威（src/Entities，向下合规）
#include "blockregistry.h" // t1027 家族谓词用 BlockRegistry::Count/isPartialBlock/isCrossBillboard/isBed（Game→Core 向下合规）

// 方块掉落实体管理器（t35；呈现 ViewModel 层）。
//
// ── R20.14 EntityStore 过渡 Adapter（plan §29.3 R20.14 验收④；头注立此存照）──────────
// 本类自 R20.14 起收敛为 **过渡 Adapter**：掉落物模拟（spawn 物理 / 就近合并 / LRU 驱逐 /
// 拾取 / 自然 despawn / 岩浆·火·仙人掌焚毁 / 批量收口 / 槽位复用 / EntityId 分配）的单一
// 权威在 src/Entities/entitystore.h（EntityStore，非 QObject 值语义组件）——本类**逐方法
// 一行委托，不复制逻辑**（禁两份模拟逻辑并存，r2014d 反探钉：合并/物理/槽位机件禁回流
// 本文件）。迁移纪律：
//   · QML / instancing feeder 消费面（Q_PROPERTY count/revision、entitiesChanged 信号、
//     全部 Q_INVOKABLE 与 C++ 直调面：aliveAt/count/posAt/itemIdAt/countAt/restingAt/
//     enchantsAt/nameAt/durabilityAt/setCountAt/removeAt/clearAll/isPickupReady/
//     deferWallClocks/tick/spawn 三入口/beginBatch-endBatch）**语义零变化**——本单承重墙
//     （P-t1027a/b/c、t1032、t1039、t1041 腿族全绿 = 通过线）。
//   · notify 沿：EntityStore 变更收口经 notifySink 回调上行 → 本类 emit entitiesChanged()
//     （含 t354 批量收口与 clearAll 无条件直发语义，逐位保留）。
//   · EntityId 语义：槽位下标（QML Repeater 寻址，t256/t978 LIFO 复用）与 store 级稳定
//     EntityId 双轨——前者 QML 承重契约零变化，后者见 entitystore.h 头注（验收①）。
//   · 渲染家族谓词（isItem3DFamily / isPlainCubeDrop / isTool3DDrop / isIconBillboardDrop /
//     isBlockIconBillboardDrop）留本类：渲染域单一权威（QML delegate 排除侧与 C++ feeder
//     收纳侧同源），非模拟域；t1027b/t1041d 源钉钉在本文件路径，随迁移同址保留。
//
// 既有行为注（t36 拾取 / t64 整栈与余数 / t53 免拾窗 / t60 重力 / t256 槽位复用 / t271 浮水
// 随流 / t320 LRU+寿命 / t343-t445 焚毁 / t354 批量 / t490fix 合并 / t608-t609 定向弹出 /
// t590-t647 元数据保真 / t867 薄支撑）全量随模拟体迁至 EntityStore（entitystore.h/.cpp
// 头注与 git 历史归档），此处不重复。
class World; // 前向声明（tick 只读 World 查询族）
class ItemEntityManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ItemEntityManager)
    // count：当前槽表长（Repeater 作 int model → 生成 0..count-1 delegate；t256 起单调不降）。
    // NOTIFY entitiesChanged 驱动 spawn 后 Repeater 追加新 delegate（不重建已有 → 动画连续）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：实体集版本号（变更收口沿自增；批量区间内也逐次自增，仅 emit 被收口）。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit ItemEntityManager(QObject *parent = nullptr);

    int count() const { return m_store.count(); }
    int revision() const { return m_store.revision(); }
    // t256：当前**活体**实体数（不含已释放空槽）。F3 draw-call 估算与 kCap 满载判定读它。
    Q_INVOKABLE int liveCount() const { return m_store.liveCount(); }
    // t1007：本会话活体高水位（clearAll 不重置；跨重载判读面——语义见 EntityStore）。
    Q_INVOKABLE int liveHighWater() const { return m_store.liveHighWater(); }
    // t256：第 i 个槽位是否活体（空槽 → false；呈现层 delegate visible + pickupScan 跳过）。
    Q_INVOKABLE bool aliveAt(int i) const { return m_store.aliveAt(i); }

    // ── t1027（R19.21）掉落物渲染家族谓词（单一权威；instancing 治理首批）──
    //   「QML delegate 排除侧」与「C++ feeder 收纳侧」（BlockDropInstancing）必须对同族判定逐位一致，
    //   否则双渲（共面 z-fight）或丢渲。谓词落 C++ 静态，QML 的 isItem3DFamily 函数退化薄委托
    //   （Main.qml t880 字面量表原样收编，值不变）。
    // 3D 形状族（t880 建 / t925 扩 / t965 订正）：火把 13；活板门 20/136；台阶 15/87/58/109；雪层 44；
    //   草丛 24；附魔台 94；枯灌木 43；小麦 25；栅栏 17/60/88；门 19/135/89；蘑菇 115/48；蛛网 102；
    //   红石火把 129；拉杆 112；按钮 113/114。改族只改此处 + ItemShapeGeometry 几何 case 表。
    Q_INVOKABLE static bool isItem3DFamily(int itemId);
    // 方块整立方掉落族（t1027 首批 instancing 治理面）：合法方块段内、非异形（partial）/ cross / 床 /
    //   3D 家族 → BlockCube 满格立方渲染路由。工具（0x100+）/ 材料（0x200+）段经 id < BlockRegistry::Count
    //   上界自然排除（下 static_assert 钉死该前提；段界挪动须回改本谓词）。
    Q_INVOKABLE static bool isPlainCubeDrop(int itemId);
    // ── t1041（R19.23）批 4 收官三谓词（单一权威；与 isItem3DFamily / isPlainCubeDrop 同纪律——
    //    QML delegate 排除侧与 C++ feeder 收纳侧（ToolDropInstancing / BillboardDropInstancing）
    //    必须对同族判定逐位一致，否则双渲或丢渲）。族表对齐 Main.qml 掉落 delegate 分支条件逐字。──
    // 工具 3D 族：工具段 ∧ toolType ∈ {Pickaxe=1, Hoe=2, Axe=3, Shovel=4, Sword=5, Bow=7}（五类几何 + 弓，
    //   Main.qml 六个 3D 分支的 C++ 镜像）。剪刀 6 / 打火石 9 走图标 billboard 族；钓鱼竿 8 掉落 delegate
    //   **无分支**（t803 只补了打火石）——如实维持既有行为不入任何族（t1041 登记，非本单范围）。
    Q_INVOKABLE static bool isTool3DDrop(int itemId);
    // 工具/材料 billboard 图标族：剪刀/打火石（工具段 ∧ toolType ∈ {Shears=6, FlintSteel=9}，ToolIcon
    //   sourceItem）∨ 材料段（id ≥ RecipeRegistry::MaterialIdBase，**单边 >= 含护甲段**——Hotbar::isMaterial
    //   逐字同判，MaterialIcon sourceItem）。两支同池（BillboardQuad + per-id Canvas 图标贴图）。
    Q_INVOKABLE static bool isIconBillboardDrop(int itemId);
    // 异形 billboard 族：partial / cross / 床段 ∧ 非 3D 族（Main.qml billboard delegate visible 链
    //   (isPartialBlock ∨ isCrossBlock ∨ isBed) ∧ !isItem3DFamily 的 C++ 镜像；BillboardQuad +
    //   per-id 图标 PNG（iconSourceForBlock）。楼梯 16 在族（partial 非 3D），火把 13 不在（3D 族）。
    Q_INVOKABLE static bool isBlockIconBillboardDrop(int itemId);
    // t743：第 i 个槽位实体是否**已着地**（压力板掉落物触发门控）。空槽 / 越界 → false。
    Q_INVOKABLE bool restingAt(int i) const { return m_store.restingAt(i); }

    // 在方块格 (x,y,z) 生成一个 itemId 的掉落实体（存格中心；t490fix 就近合并；t320 LRU；
    //   t64 count 语义；t590/t622/review D2-c 元数据保真）。达到 kCap → LRU 驱逐最老。
    Q_INVOKABLE void spawnItem(int x, int y, int z, int itemId, int count = 1, const QVariantList &enchants = {}, const QString &name = QString(), int durability = -1)
    {
        m_store.spawnItem(x, y, z, itemId, count, enchants, name, durability);
    }
    // t608 定点定向弹出（发射器排出口统一口径）：位置精确到排出口 + 弹出方向 = 发射朝向；
    //   其余（合并 / LRU / 免拾窗 / 物理）与 spawnItem 同链。
    Q_INVOKABLE void spawnItemAt(const QVector3D &pos, int itemId, int count,
                                 float dirX, float dirZ, float speed,
                                 const QVariantList &enchants = {}, const QString &name = QString(),
                                 int durability = -1)
    {
        m_store.spawnItemAt(pos, itemId, count, dirX, dirZ, speed, enchants, name, durability);
    }
    // t609 带俯仰的定点定向弹出（Q 丢弃修正；PlayerController C++ 直调，非 Q_INVOKABLE）。
    void spawnItemThrown(const QVector3D &pos, int itemId, int count,
                         float dirX, float dirY, float dirZ, float speed,
                         const QVariantList &enchants = {}, const QString &name = QString(),
                         int durability = -1)
    {
        m_store.spawnItemThrown(pos, itemId, count, dirX, dirY, dirZ, speed, enchants, name, durability);
    }

    // t354 批量 spawn 抑制 entitiesChanged（修爆炸 O(N²) 绑定风暴 → O(N)；深度可嵌套；
    //   非爆炸常规 spawn 不经批 → 立即 emit）。clearAll 不经批（重置语义，无条件通知）。
    Q_INVOKABLE void beginBatch() { m_store.beginBatch(); }
    Q_INVOKABLE void endBatch() { m_store.endBatch(); }
    Q_INVOKABLE bool batchActive() const { return m_store.batchActive(); }

    // 呈现层 Repeater delegate 绑定读族（越界安全语义与旧实现逐位一致）。
    Q_INVOKABLE QVector3D posAt(int i) const { return m_store.posAt(i); }
    Q_INVOKABLE int itemIdAt(int i) const { return m_store.itemIdAt(i); }
    Q_INVOKABLE int countAt(int i) const { return m_store.countAt(i); }
    Q_INVOKABLE QVariantList enchantsAt(int i) const { return m_store.enchantsAt(i); }
    Q_INVOKABLE QString nameAt(int i) const { return m_store.nameAt(i); }
    Q_INVOKABLE int durabilityAt(int i) const { return m_store.durabilityAt(i); }
    // t64：拾取余数回写（n<=0 → 全拾走销毁）。仅 PlayerController::pickupScan 调。
    Q_INVOKABLE void setCountAt(int i, int n) { m_store.setCountAt(i, n); }
    // 销毁第 i 个实体（t36 拾取后调用；t256 起释放槽位非 erase-shift——count 单调不降，
    //   Repeater delegate 不泄漏）。
    Q_INVOKABLE void removeAt(int i) { m_store.removeAt(i); }
    // t176/t437：清空所有掉落实体（切世界 / 退出存档前调）——释放全部活体槽 + **无条件**
    //   emit entitiesChanged（重置语义；保 slot-reuse 单调不变量防 delegate 孤儿泄漏）。
    Q_INVOKABLE void clearAll() { m_store.clearAll(); }

    // t53：第 i 个实体是否已过新生免拾取期（0.5s；越界 / 时钟未启 → true 保守可拾）。
    bool isPickupReady(int i) const { return m_store.isPickupReady(i); }

    // t889 暂停期墙钟顺延（硬暂停复跑时由 PlayerController::setWorldRunning 调）：活体槽
    //   spawnMs 整体 +ms；ms<=0 幂等早退；纯寿命簿记无 revision bump。
    void deferWallClocks(qint64 ms) { m_store.deferWallClocks(ms); }
    // R20.14 寿命时钟注入缝（ageLifetimeClock，deferWallClocks 对偶；t1029 缝纪律）——
    //   生产路径零调用，矩阵 despawn 腿经 Adapter 面同步驱动孪生（与 store 直驱同参）。
    void ageLifetimeClock(qint64 ms) { m_store.ageLifetimeClock(ms); }

    // t60 掉落物重力 / t271 水冲走 / t343-t445 焚毁 / t320 寿命驱逐（每帧由
    //   PlayerController::tick 调；C++ 直调非 Q_INVOKABLE——避开 moc 对 World* 前向类型的
    //   metatype 处理）。world null / 无实体 → 早退。物理语义见 EntityStore::tick。
    void tick(qreal dt, World *world) { m_store.tick(dt, world); }

    // R20.14：可见实体快照观察（验收③；EntityStore 在 notify 沿 / clearAll 重建——呈现面
    //   消费仍走上述逐槽读口零变化，本访问器供快照权威的测试 / 未来消费方）。
    const EntityStoreSnapshot &snapshot() const { return m_store.snapshot(); }
    // R20.14：store 级稳定 EntityId 观察（验收①；槽无效 / 空槽 → 0 哨兵）。
    quint32 entityIdAtSlot(int i) const { return m_store.entityIdAtSlot(i); }

signals:
    void entitiesChanged(); // spawn / remove / 物理脏 / despawn / clearAll 触发（经 store notifySink 上行）
    // （t804 itemBurned 火焚烟粒子信号已随 t844 需求反转退役：入火改瞬灭无动画无烟，与岩浆同款语义。）

private:
    // 模拟单一权威（唯一数据成员——Adapter 零自有模拟状态）。
    EntityStore m_store;
};

// t1027：isPlainCubeDrop 用 id < Count 上界排除工具（0x100+）/ 材料（0x200+）段——方块枚举一旦膨胀
//   越过工具段下界，该排除静默失效（工具 id 会被误收进整立方族）→ 编译期钉死前提。
static_assert(int(BlockRegistry::Count) <= 0x100,
              "t1027: BlockRegistry::Count must stay below the 0x100 tool segment - "
              "isPlainCubeDrop excludes tools/materials via the Count bound alone");

#endif // ITEMENTITYMANAGER_H
