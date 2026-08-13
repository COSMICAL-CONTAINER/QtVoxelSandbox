#ifndef PLAYERPROGRESS_H
#define PLAYERPROGRESS_H

#include <QObject>
#include <QtQml/qqml.h>
#include <QVariantList>

// 玩家进度系统（Game 层 ViewModel；progress 新系统）。机制等价 MC 1.0 statistics + advancements ——
// 记录玩家在本世界的**累计统计**（玩游戏时间 / 天数 / 挖放次数 / 路程 / 击杀 / 死亡 / 合成 / 拾取）+
// **成就解锁状态**（MC 风格 advancement tree 的现阶段可完成子集）。
//
// 跨世界持久化（同 ChestStore / FurnaceStore 模式）：内容存进 worldstore 的 progress 表（IF NOT EXISTS 幂等
// 补建，schema 版本不 bump —— 纯加表对老库向前兼容，见 worldstore.h kSchemaVersion 注释）。Main.qml.enterWorld
// 调 progress.loadVariant(worldStore.loadProgress()) 整体替换内存（清旧世界残留 + 填本世界进度）；
// saveAndExitToWorldList 经 worldStore.saveProgress(progress.toVariant()) 落盘。
//
// 设计（对齐 ChestStore）：纯存储 + 埋点累加 + 成就判定，不持 World / Renderer（PLAN §2 分层：本层属
// Game/ViewModel，只被 Main.qml / CraftingTableUI.qml / SurvivalInventory.qml 等呈现层路由调埋点；零向上依赖）。
// 埋点方法（Q_INVOKABLE）由各事件源经 QML 桥接调用：
//   - onBlockMined() / onBlockPlaced()：playercontroller 破块完成 / 放块成功末尾。
//   - onMove(float deltaBlocks)：playercontroller 每帧位移增量（节流到 ~0.5s flush emit progressChanged）。
//   - onMobKilled(int mobType)：EntityManager.mobDied → Main.qml Connections 路由。
//   - onDeath()：player 死亡 → Main.qml onDied 路由。
//   - onCraft(int resultId)：InventoryOps.js 合成成功（CraftingTableUI / SurvivalInventory）。
//   - onItemPicked(int itemId)：player.itemPickedUp → Main.qml Connections 路由。
//   - onInventoryOpened()：Main.qml.openInventory / openCraftingTable 等开包函数。
//   - onPlayTimeTick(float dt) / setDayCount(int)：WorldClock.ticked → Main.qml Connections 路由。
//
// 成就解锁逻辑在埋点方法内判定（如 onCraft(SwordWood) → unlock("出击时间")）。unlock 时先查前置依赖：
//   父成就未解锁 → 忽略本次解锁事件（progress-tree 三轮；机制等价 MC 1.0 父成就未达成则子成就解锁不生效）。
//   unlock 成功时：
//   - emit achievementUnlocked(id, name, desc)：供 QML 弹 toast 提示。
//   - emit achievementChanged：供 QML 成就列表刷新。
//
// 暴露给 QML（moc 安全：统计数值走 Q_PROPERTY + NOTIFY；列表数据走 Q_INVOKABLE + revision，同 ChestStore / Hotbar 模式）：
//   - playTimeSecs / daysPlayed / blocksMined / blocksPlaced / distanceTraveled / mobsKilled / deaths /
//     craftsCount / itemsPicked（统计；NOTIFY=progressChanged）。
//   - revision（int，任一统计 / 成就变更自增；成就 / 统计列表 delegate 触碰 revision 取最新值）。
//   - Q_INVOKABLE QVariantList achievements()：[{id,name,desc,unlocked,parentId,parentName,depth,locked}, ...]
//     供 QML 树形成就列表显示（定义序 = 依赖树 DFS 先序：父先于子、同父兄弟相邻 → 连续连线；locked = 父未解锁）。
//   - Q_INVOKABLE QVariantList statsList()：[{name, value}, ...] 供 QML 统计面板显示。
//
// §4 法律 + §9：零 MC 专有名词（成就名用通用词「打开背包」「获得原木」「出击时间」「挖矿时间到」「获得升级」
// 「怪物猎人」「合成台」「获得木材」；不引 MC 专名如 "Minecraft" / "Advancement"）。Shambler / Bones /
// Stalker / Spider 是本工程改名（§9 概念改名表）。
class PlayerProgress : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PlayerProgress)
    // 统计数值（NOTIFY=progressChanged；QML 绑定刷新）。playTimeSecs 用 qreal（秒，浮点累积）；
    //   distanceTraveled 同（格，浮点）；其余整数计数。
    Q_PROPERTY(qreal playTimeSecs READ playTimeSecs NOTIFY progressChanged)
    Q_PROPERTY(int daysPlayed READ daysPlayed NOTIFY progressChanged)
    Q_PROPERTY(int blocksMined READ blocksMined NOTIFY progressChanged)
    Q_PROPERTY(int blocksPlaced READ blocksPlaced NOTIFY progressChanged)
    Q_PROPERTY(qreal distanceTraveled READ distanceTraveled NOTIFY progressChanged)
    Q_PROPERTY(int mobsKilled READ mobsKilled NOTIFY progressChanged)
    Q_PROPERTY(int deaths READ deaths NOTIFY progressChanged)
    Q_PROPERTY(int craftsCount READ craftsCount NOTIFY progressChanged)
    Q_PROPERTY(int itemsPicked READ itemsPicked NOTIFY progressChanged)
    // 内容版本号（任一统计 / 成就写入自增）。QML 列表 delegate 触碰它取最新 achievements() / statsList()
    //   （同 ChestStore revision / Hotbar slotRevision 模式，moc 安全契约）。
    Q_PROPERTY(int revision READ revision NOTIFY progressChanged)

public:
    explicit PlayerProgress(QObject *parent = nullptr);

    // ── 统计读取（Q_PROPERTY READ）──
    qreal playTimeSecs() const { return m_playTimeSecs; }
    int daysPlayed() const { return m_daysPlayed; }
    int blocksMined() const { return m_blocksMined; }
    int blocksPlaced() const { return m_blocksPlaced; }
    qreal distanceTraveled() const { return m_distanceTraveled; }
    int mobsKilled() const { return m_mobsKilled; }
    int deaths() const { return m_deaths; }
    int craftsCount() const { return m_craftsCount; }
    int itemsPicked() const { return m_itemsPicked; }
    int revision() const { return m_revision; }

    // ── 埋点方法（Q_INVOKABLE；各事件源经 QML 桥接调）──
    // 破块完成（创造瞬破 / 生存累积完成均调；mobType 不区分）。仅累加 blocksMined。
    Q_INVOKABLE void onBlockMined();
    // 放块成功（placeBlock 末尾）。仅累加 blocksPlaced。
    Q_INVOKABLE void onBlockPlaced();
    // 每帧位移增量（水平距离，格）。内部累积到 m_distanceAccum；每 ~0.5s flush 把累积值并入 distanceTraveled
    //   并 emit progressChanged（免每帧抖 QML 绑定，同 playercontroller flowScan 节流模式）。
    Q_INVOKABLE void onMove(float deltaBlocks);
    // 击杀 mob（mobType = EntityManager::MobType；据它判「怪物猎人」成就：敌对 mob Shambler/Bones/Stalker/Spider）。
    Q_INVOKABLE void onMobKilled(int mobType);
    // 玩家死亡（仅 Survival）。累加 deaths。
    Q_INVOKABLE void onDeath();
    // 合成成功（resultId = 产物物品 id）。累加 craftsCount + 据产物判成就：
    //   CraftingTable → 「合成台」；SwordWood → 「出击时间」；PickaxeWood → 「挖矿时间到」；
    //   石质工具（石镐/石剑/石斧/石锄/石铲）→ 「获得升级」。
    Q_INVOKABLE void onCraft(int resultId);
    // 拾取物品（itemId = 物品 id）。累加 itemsPicked + 据物品判成就：Log/SpruceLog → 「获得原木」。
    Q_INVOKABLE void onItemPicked(int itemId);
    // 打开任意背包面板（E 背包 / 工作台 / 熔炉 / 箱子 / 附魔台 / 铁砧）。解锁「打开背包」。
    Q_INVOKABLE void onInventoryOpened();
    // 游戏时间 tick（dt 秒）。累加 playTimeSecs；每 ~0.5s flush emit（节流，免每 tick 抖 QML 绑定）。
    Q_INVOKABLE void onPlayTimeTick(float dt);
    // 设当前天数（WorldClock.dayCount）。仅当 > 当前 daysPlayed 时更新（单调递增；跨天推进）。
    Q_INVOKABLE void setDayCount(int day);

    // ── 列表数据（Q_INVOKABLE；QML delegate 触碰 revision 取最新）──
    // 全部成就 [{id, name, desc, unlocked, parentId, parentName, depth, locked}, ...]（定义序 = 依赖树
    //   DFS 先序：父先于子、同父兄弟相邻）。供 QML 树形成就列表显示。locked = 未解锁 且 父未解锁
    //   （父已解锁但未解锁 → 可解锁，locked=false）。
    Q_INVOKABLE QVariantList achievements() const;
    // 统计列表 [{name, value}, ...]（中文名 + 当前值）。供 QML 统计面板显示。
    Q_INVOKABLE QVariantList statsList() const;
    // 某成就是否已解锁（id = 成就 id 字符串）。供 QML 单项查询。
    Q_INVOKABLE bool isUnlocked(const QString &id) const;

    // ── 持久化（同 ChestStore allChests / loadAll 模式）──
    // 收集全部统计 + 成就为 QVariantMap（{stats:{...}, achievements:{id:bool}}），供 Main.qml 传
    //   worldStore.saveProgress 落盘。
    Q_INVOKABLE QVariantMap toVariant() const;
    // 用存档 QVariantMap（同 toVariant 形状）整体替换内存内容（先清后填；单次 emit progressChanged）。
    //   Main.qml.enterWorld 调：progress.loadVariant(worldStore.loadProgress()) —— 替换语义即「清旧世界残留 +
    //   填本世界进度」。空 map → 重置默认（全 0 统计 + 全未解锁成就）。
    Q_INVOKABLE void loadVariant(const QVariantMap &data);

signals:
    // 任一统计 / 成就变更（埋点累加 / 成就解锁 / loadVariant）。驱动 revision 自增 + QML 列表 / 绑定刷新。
    void progressChanged();
    // 成就解锁（携 id / 中文名 / 描述）。QML Connections 据此弹 toast 提示（仅首次解锁发；已解锁不再发）。
    void achievementUnlocked(const QString &id, const QString &name, const QString &desc);
    // 成就列表变更（解锁 / loadVariant）。QML 成就列表 delegate 据此 + revision 刷新。
    void achievementChanged();

private:
    // 成就定义（id / 父成就 id / 中文名 / 描述）。id 用通用词英文标识符（非 MC 专名）。
    //   parentId：父成就 id（null = 根成就，无前置）。依赖树机制等价 MC 1.0 advancement tree：
    //   子成就仅在父成就已解锁时可解锁（unlock 前置检查）+ QML 面板 locked 态据此判定。
    struct AchievementDef {
        const char *id;
        const char *parentId;   // 父成就 id（null = 根成就）
        const char *name;
        const char *desc;
    };
    // 全部成就定义（定义序 = 依赖树 DFS 先序：父先于子、同父兄弟相邻 → QML 树形渲染连续连线）。
    //   mechanisms 等价 MC 1.0 advancement tree 的现阶段可完成子集。
    static const QList<AchievementDef> &achievementDefs();

    // 解锁成就（id = 成就 id）。已解锁 → no-op（幂等）；父成就未解锁（前置依赖）→ 忽略本次解锁
    //   （机制等价 MC 1.0 父成就未达成则子成就解锁不生效）；首次解锁 → 标记 + emit achievementUnlocked +
    //   achievementChanged + progressChanged（驱动 revision bump）。
    void unlock(const QString &id);
    // 累加统计并 flush（内部辅助：bump revision + emit progressChanged）。
    void bumpAndEmit();

    // 统计数值
    qreal m_playTimeSecs = 0.0;     // 游戏时间（秒，浮点累积）
    int m_daysPlayed = 0;           // 天数（WorldClock.dayCount 单调取值）
    int m_blocksMined = 0;          // 挖掘方块次数
    int m_blocksPlaced = 0;         // 放置方块次数
    qreal m_distanceTraveled = 0.0; // 走过路程（格，浮点累积）
    int m_mobsKilled = 0;           // 击杀 mob 数
    int m_deaths = 0;               // 死亡次数
    int m_craftsCount = 0;          // 合成次数
    int m_itemsPicked = 0;          // 拾取物品次数

    // onMove / onPlayTimeTick 节流累积器（免每帧 emit progressChanged 抖 QML 绑定）。
    float m_distanceAccum = 0.0f;   // 距离累积（格；onPlayTimeTick 达 kFlushInterval 时一并并入 distanceTraveled）
    float m_playTimeFlushTimer = 0.0f; // 游戏时间 flush 计时（秒；达 kFlushInterval 才 emit）

    // 成就解锁状态（id → true）。QString 键；成就数少，性能非热点。
    QSet<QString> m_unlocked;
    int m_revision = 0;

    // 节流间隔（秒）：onMove / onPlayTimeTick 累积到此值才 flush emit progressChanged。
    static constexpr float kFlushInterval = 0.5f;
};

#endif // PLAYERPROGRESS_H
