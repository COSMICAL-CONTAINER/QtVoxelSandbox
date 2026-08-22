#include "playerprogress.h"

#include <QHash>
#include <QSet>
#include <QVariantList>
#include <functional>

#include <QVariantMap>

#include "blockregistry.h"   // Log/SpruceLog/CraftingTable 方块 id（成就判定 + 图标）
#include "toolregistry.h"    // SwordWood/PickaxeWood/石质工具/Bow id（合成成就判定 + 图标）
#include "recipe.h"          // DiamondId/WheatId/EnchantedBookId/OakBoatId 材料段 id（图标 + 判定）
#include "entitymanager.h"   // MobType（敌对 mob 判「怪物猎人」）

// 成就定义（progress 新系统）。机制等价 MC 1.0 advancement tree 的现阶段可完成子集。
//   §9：成就名用中文通用词，零 MC 专名。定义序 = 依赖树 DFS 先序（父先于子、同父兄弟相邻）：
//   主线（t637 布局重排，用户「打开背包最左根 → 获得原木一条线」）：「打开背包」唯一最左根 →
//   「获得原木」(←打开背包) →「合成台」→「出击时间」→「怪物猎人」→「神射手」(←怪物猎人)；
//   「挖矿时间到」(←合成台) →「获得升级」→「钻石!」→「附魔师」→「书虫」/「铁匠」(←附魔师)；
//   「耕种时间到」(←合成台，t752；与「出击时间」「挖矿时间到」并列的第三分支，首次合成任意材质锄头)
//   →「农夫」(←耕种时间到，t752 由独立根重挂——「做锄头」作前置、「收获 10 作物」为其后继)。
//   独立根线（t637；t752 后仅剩两条）：「起航」（骑船）/「发射!」（发射器触发）各自独立根。
//   父成就未解锁时子成就不解锁（unlock 前置检查）。iconId = 节点图标（QML 树节点显示）。
const QList<PlayerProgress::AchievementDef> &PlayerProgress::achievementDefs()
{
    static const QList<AchievementDef> kDefs = {
        // ── 主线：打开背包 → 获得原木 → 合成台 → 工具 / 战斗 / 挖矿 / 附魔 ──
        { "open_inventory", nullptr,          "打开背包",   "按 E 打开你的背包",
          int(BlockRegistry::Chest) },
        // t637：get_wood 由独立根改为挂 open_inventory 下（用户「打开背包→获得原木一条线」——主线单一
        //   根串成线，机制等价 MC 1.0 "Taking Inventory" 即成就树根）。旧存档已解锁组合不受影响
        //   （loadVariant 直插 m_unlocked 绕过父检查；仅新解锁事件走父前置）。
        { "get_wood",       "open_inventory", "获得原木",   "砍倒一棵树获得原木",
          int(BlockRegistry::Log) },
        { "crafting_table", "get_wood",       "合成台",     "用 4 块木板合成工作台",
          int(BlockRegistry::CraftingTable) },
        // t752 ② 材质放宽：出击/挖矿/耕种三兄弟全走「任意材质」（描述同步去「木」字——判定见 onCraft）。
        { "sword_time",     "crafting_table", "出击时间",   "合成一把剑准备战斗",
          int(ToolRegistry::SwordWood) },
        { "monster_hunter", "sword_time",     "怪物猎人",   "击杀一只敌对怪物",
          int(ToolRegistry::SwordIron) },
        { "sniper",         "monster_hunter", "神射手",     "用箭命中生物 10 次",
          int(ToolRegistry::Bow) },
        { "mining_time",    "crafting_table", "挖矿时间到", "合成一把镐开始挖矿",
          int(ToolRegistry::PickaxeWood) },
        { "upgrade",        "mining_time",    "获得升级",   "升级到石质工具",
          int(ToolRegistry::PickaxeStone) },
        { "get_diamond",    "upgrade",        "钻石!",      "首次获得钻石",
          int(RecipeRegistry::DiamondId) },
        { "enchanter",      "get_diamond",    "附魔师",     "在附魔台完成一次附魔",
          int(BlockRegistry::EnchantingTable) },
        { "bookworm",       "enchanter",      "书虫",       "附出一本附魔书",
          int(RecipeRegistry::EnchantedBookId) },
        { "blacksmith",     "enchanter",      "铁匠",       "用铁砧修复或合并物品",
          int(BlockRegistry::Anvil) },
        // t752 ①「耕种时间到」：首次合成任意材质锄头（与「出击时间」「挖矿时间到」同族，挂合成台下并列）；
        //   「农夫」由独立根（t637）重挂其下。旧存档已解锁 farmer 不受影响——loadVariant 直插 m_unlocked
        //   绕过父检查（仅新解锁事件走前置；同 t637 get_wood 重挂的兼容语义）。
        { "time_to_farm",   "crafting_table", "耕种时间到", "合成一把锄头开始耕种",
          int(ToolRegistry::HoeWood) },
        { "farmer",         "time_to_farm",   "农夫",       "收获 10 株成熟作物",
          int(RecipeRegistry::WheatId) },
        // ── 独立根线（t637：各自独立根，不挂主线；t752 后农夫已重挂「耕种时间到」下）──
        { "set_sail",       nullptr,          "起航",       "坐上船开始航行",
          int(RecipeRegistry::OakBoatId) },
        { "dispense",       nullptr,          "发射!",      "让发射器或投掷器弹出物品",
          int(BlockRegistry::Dispenser) },
    };
    return kDefs;
}

PlayerProgress::PlayerProgress(QObject *parent) : QObject(parent) {}

// bump revision + emit progressChanged（统计累加 / loadVariant 末尾调）。统一驱动 QML 绑定刷新。
void PlayerProgress::bumpAndEmit()
{
    ++m_revision;
    emit progressChanged();
}

// 解锁成就（id）。已解锁 → no-op（幂等，防重复弹 toast）；父成就未解锁（前置依赖）→ 忽略本次解锁
//   （progress-tree 三轮；机制等价 MC 1.0 父成就未达成则子成就解锁不生效，依赖树真实生效）；
//   首次解锁 → 标记 + 弹 toast + 列表刷新 + revision bump。silent=true（读档回放用）→ 同样走父检查
//   但不弹 toast / 不逐项 emit（loadVariant 末尾单次 emit，免 toast 风暴；review-M5：回放曾直插
//   m_unlocked 绕过父检查 → 「射而未杀 10 次」存档回放即子成就解锁而父仍锁，依赖树破相）。
void PlayerProgress::unlock(const QString &id, bool silent)
{
    if (m_unlocked.contains(id)) return;
    // 前置依赖检查：父成就未解锁 → 忽略本次解锁事件。defs 按 DFS 先序（父定义先于子），直接扫表查父。
    for (const auto &d : achievementDefs()) {
        if (id == QLatin1String(d.id) && d.parentId
            && !m_unlocked.contains(QLatin1String(d.parentId)))
            return;
    }
    m_unlocked.insert(id);
    if (silent) return; // 读档回放：仅标记，不发信号（caller 统一 emit）
    // 查成就定义取 name/desc（toast 文案）；未知 id 防御性兜底。
    QString name = id, desc;
    for (const auto &d : achievementDefs()) {
        if (id == QLatin1String(d.id)) { name = QString::fromUtf8(d.name); desc = QString::fromUtf8(d.desc); break; }
    }
    emit achievementUnlocked(id, name, desc);
    emit achievementChanged();
    bumpAndEmit();
}

void PlayerProgress::onBlockMined()    { ++m_blocksMined;   bumpAndEmit(); }
void PlayerProgress::onBlockPlaced()   { ++m_blocksPlaced;  bumpAndEmit(); }

// 每帧位移增量累加（节流 flush：不自带计时器，借 onPlayTimeTick 的 kFlushInterval 闸门一并并入 distanceTraveled，
//   免每帧抖 QML 绑定，同 playercontroller flowScan 节流模式）。
void PlayerProgress::onMove(float deltaBlocks)
{
    m_distanceAccum += deltaBlocks;
}

// 游戏时间 tick：累加 playTimeSecs；每 kFlushInterval 秒 flush（emit progressChanged + 把累积距离并入）。
void PlayerProgress::onPlayTimeTick(float dt)
{
    m_playTimeSecs += qreal(dt);
    m_playTimeFlushTimer += dt;
    if (m_playTimeFlushTimer >= kFlushInterval) {
        m_playTimeFlushTimer = 0.0f;
        if (m_distanceAccum > 0.0f) {
            m_distanceTraveled += qreal(m_distanceAccum);
            m_distanceAccum = 0.0f;
        }
        bumpAndEmit();
    }
}

// 击杀 mob：累加 + 判「怪物猎人」（敌对 mob Shambler=4/Bones=5/Stalker=6/Spider=7；§9 改名）。
void PlayerProgress::onMobKilled(int mobType)
{
    ++m_mobsKilled;
    using MT = EntityManager::MobType;
    if (mobType == MT::MobShambler || mobType == MT::MobBones
        || mobType == MT::MobStalker || mobType == MT::MobSpider)
        unlock("monster_hunter");
    bumpAndEmit();
}

void PlayerProgress::onDeath() { ++m_deaths; bumpAndEmit(); }

// 合成成功：累加 + 据产物判成就。CraftingTable→「合成台」；t752 ② 材质放宽：任意材质镐→「挖矿时间到」/
//   任意材质剑→「出击时间」/任意材质锄→「耕种时间到」（旧版仅认木系首件 id 等值——玩家直跳石/铁/钻/金/铜
//   档合成时不触发是漏判；改按 ToolDef.type 工具类判，新增材质自动覆盖，免逐一枚举 id 集合）；
//   任一石质工具→「获得升级」。
void PlayerProgress::onCraft(int resultId)
{
    ++m_craftsCount;
    if (resultId == int(BlockRegistry::CraftingTable)) unlock("crafting_table");
    // t752 ②：按工具类型（Pickaxe/Sword/Hoe）判三兄弟成就，全材质（木/石/铁/钻/金/铜）任一均可。
    if (const ToolRegistry::ToolDef *td = ToolRegistry::tool(resultId)) {
        if (td->type == BlockRegistry::Pickaxe) unlock("mining_time");
        if (td->type == BlockRegistry::Sword)   unlock("sword_time");
        if (td->type == BlockRegistry::Hoe)     unlock("time_to_farm");
    }
    using TI = ToolRegistry::ToolId;
    if (resultId == int(TI::PickaxeStone) || resultId == int(TI::HoeStone)
        || resultId == int(TI::AxeStone) || resultId == int(TI::ShovelStone)
        || resultId == int(TI::SwordStone))
        unlock("upgrade");
    bumpAndEmit();
}

// 拾取物品：累加 + Log/SpruceLog→「获得原木」；DiamondId→「钻石!」（t619）。
void PlayerProgress::onItemPicked(int itemId)
{
    ++m_itemsPicked;
    if (itemId == int(BlockRegistry::Log) || itemId == int(BlockRegistry::SpruceLog))
        unlock("get_wood");
    if (itemId == int(RecipeRegistry::DiamondId))
        unlock("get_diamond");
    bumpAndEmit();
}

// 打开任意背包面板 → 解锁「打开背包」。
void PlayerProgress::onInventoryOpened() { unlock("open_inventory"); }

// ── t619 新埋点（树状图成就扩展）──

// 玩家箭命中生物：累加 + 达 kSniperHits（10）解锁「神射手」。
void PlayerProgress::onArrowHitMob()
{
    ++m_arrowsHitMobs;
    if (m_arrowsHitMobs >= kSniperHits) unlock("sniper");
    bumpAndEmit();
}

// 收获成熟作物：累加 + 达 kFarmerHarvests（10）解锁「农夫」。
void PlayerProgress::onCropHarvested()
{
    ++m_cropsHarvested;
    if (m_cropsHarvested >= kFarmerHarvests) unlock("farmer");
    bumpAndEmit();
}

// 骑上船 → 「起航」（幂等 unlock）。
void PlayerProgress::onBoatBoarded() { unlock("set_sail"); }

// 发射器/投掷器弹出物品 → 「发射!」。
void PlayerProgress::onDispensed() { unlock("dispense"); }

// 附魔台附魔成功 → 「附魔师」。
void PlayerProgress::onEnchanted() { unlock("enchanter"); }

// 附书产附魔书 → 「书虫」。
void PlayerProgress::onEnchantedBookObtained() { unlock("bookworm"); }

// 铁砧成功操作 → 「铁匠」。
void PlayerProgress::onAnvilUsed() { unlock("blacksmith"); }

// 设当前天数（WorldClock.dayCount 单调）。仅当 > 当前 daysPlayed 时更新。
void PlayerProgress::setDayCount(int day)
{
    if (day > m_daysPlayed) { m_daysPlayed = day; bumpAndEmit(); }
}

// 全部成就 [{id,name,desc,unlocked,parentId,parentName,depth,locked,col,row,iconId}, ...]（定义序 = 依赖树
//   DFS 先序）。parentName = 父成就中文名（locked 态提示）；depth = 沿 parentId 链上溯层数（0=根成就）；
//   locked = 未解锁 且 父未解锁（父已解锁但未解锁 → 可解锁，locked=false，QML 显 ○）。
//   t619 树状图布局字段：col = depth（依赖层级，root=0 在左）；row = 同列内垂直序（t752 ③ 起 qreal，可带
//   .5 半行）—— 递归子树布局：叶子占 1 行、父 = 首末子女中心的中点（对称对齐；多子树的父的孙辈互不重叠），
//   x=col×列距、y=row×行距（QML 摆位；QML 侧 visibleTree 复刻同一算法，见 Main.qml）。
//   iconId = 节点图标物品 id（QML 据 isTool/isMaterial 路由 ToolIcon/MaterialIcon/方块 Image）。
QVariantList PlayerProgress::achievements() const
{
    const auto &defs = achievementDefs();
    // 查某成就 id 的中文名（父名提示用）；未知 id 防御性返原名。
    auto findName = [&defs](const QString &pid) {
        for (const auto &d : defs)
            if (pid == QLatin1String(d.id)) return QString::fromUtf8(d.name);
        return pid;
    };
    // 沿 parentId 链上溯计深度（defs 父先于子；未命中即止，防御性防死循环）。
    auto depthOf = [&defs](const QString &pid) {
        int depth = 0;
        QString cur = pid;
        while (!cur.isEmpty()) {
            QString next;
            for (const auto &d : defs) {
                if (cur == QLatin1String(d.id)) {
                    next = d.parentId ? QString::fromUtf8(d.parentId) : QString();
                    break;
                }
            }
            cur = next;
            ++depth;
        }
        return depth;
    };

    // ── t619 树状布局（递归子树垂直分配）：row = 同列内序 ──
    // 叶子子树占 1 行；父节点 row = 首末子女中心的中点（居中对齐子女）；各根子树垂直堆叠不重叠。
    // defs 父先于子（DFS 先序）→ 按定义序递归即可（子必在父之后定义）。
    QHash<QString, qreal> rowOf;        // id → 已分配 row（-1 = 未分配；t752 ③ 升格 qreal——偶数子女跨度
                                        //   的精确中点带 .5，QML 摆位按 real 计算天然兼容）
    for (const auto &d : defs) rowOf.insert(QString::fromUtf8(d.id), -1.0);
    int nextRow = 0;                    // 全局行计数器（跨根连续堆叠）
    // 递归分配 id 的子树行；返回子树占的行数。
    std::function<int(const QString &)> layoutSubtree = [&](const QString &id) -> int {
        // 收集直接子女（定义序）。
        QStringList children;
        for (const auto &d : defs)
            if (d.parentId && id == QString::fromUtf8(d.parentId))
                children.append(QString::fromUtf8(d.id));
        if (children.isEmpty()) {
            rowOf[id] = qreal(nextRow++); // 叶子占 1 行
            return 1;
        }
        int total = 0;
        for (const auto &c : children) total += layoutSubtree(c);
        // t752 ③ 布局对齐修复：父 row = 首末子女「中心」的中点 (c1+cn)/2，替代旧式「子树叶子跨度中点」
        //   startRow+(total-1)/2（整除截断）。旧式在子女子树行数不均时把父拉向大子树——例（旧树形）
        //   合成台下「出击」子树 1 行 +「挖矿」子树 2 行（附魔师展开双分支），旧式父 = 0+(3-1)/2 = 1
        //   而挖矿链中心 1.5 → 父距上路 1.0 行、距下路中心仅 0.5 行（用户「下方占比多、更贴近」，
        //   上下并列路线不等距）。中点式对任意子女组合恒等距对称；且中点 ∈ [c1, cn] ⊆ 自身子树行区间，
        //   不会侵入兄弟子树（叶子行分配 nextRow 顺序不变，total 语义照旧）。
        rowOf[id] = (rowOf[children.first()] + rowOf[children.last()]) / 2.0;
        return total;
    };
    // 按定义序对每个根（parentId 空）起布局。
    for (const auto &d : defs)
        if (!d.parentId) layoutSubtree(QString::fromUtf8(d.id));

    QVariantList out;
    for (const auto &d : defs) {
        const QString id = QString::fromUtf8(d.id);
        const QString parentId = d.parentId ? QString::fromUtf8(d.parentId) : QString();
        const bool unlocked = m_unlocked.contains(id);
        const bool parentUnlocked = parentId.isEmpty() || m_unlocked.contains(parentId);
        const int depth = parentId.isEmpty() ? 0 : depthOf(parentId);
        QVariantMap item;
        item["id"] = id;
        item["name"] = QString::fromUtf8(d.name);
        item["desc"] = QString::fromUtf8(d.desc);
        item["unlocked"] = unlocked;
        item["parentId"] = parentId;
        item["parentName"] = parentId.isEmpty() ? QString() : findName(parentId);
        item["depth"] = depth;
        item["locked"] = !unlocked && !parentId.isEmpty() && !parentUnlocked;
        item["col"] = depth;                          // 列 = 依赖层级
        item["row"] = rowOf.value(id, 0);             // 同列内垂直序（未分配防御 0）
        item["iconId"] = d.iconId;                    // 图标物品 id
        out.append(item);
    }
    return out;
}

// 统计列表 [{name, value}, ...]（中文名 + 当前值格式化）。
QVariantList PlayerProgress::statsList() const
{
    // 游戏时间格式化：秒 → 「Xh Ym」/「Ym Zs」/「Zs」。
    auto fmtTime = [](qreal secs) {
        int s = int(secs);
        if (s >= 3600) return QString("%1h %2m").arg(s / 3600).arg((s % 3600) / 60);
        if (s >= 60)   return QString("%1m %2s").arg(s / 60).arg(s % 60);
        return QString("%1s").arg(s);
    };
    QVariantList out;
    auto add = [&](const QString &n, const QString &v) {
        QVariantMap m; m["name"] = n; m["value"] = v; out.append(m);
    };
    add("游戏时间",   fmtTime(m_playTimeSecs));
    add("游玩天数",   QString::number(m_daysPlayed));
    add("挖掘方块",   QString::number(m_blocksMined));
    add("放置方块",   QString::number(m_blocksPlaced));
    add("走过路程",   QString("%1 格").arg(int(m_distanceTraveled)));
    add("击杀怪物",   QString::number(m_mobsKilled));
    add("死亡次数",   QString::number(m_deaths));
    add("合成次数",   QString::number(m_craftsCount));
    add("拾取物品",   QString::number(m_itemsPicked));
    add("箭中生物",   QString::number(m_arrowsHitMobs));   // t619
    add("收获作物",   QString::number(m_cropsHarvested));  // t619
    return out;
}

bool PlayerProgress::isUnlocked(const QString &id) const { return m_unlocked.contains(id); }

// 收集全部统计 + 成就为 QVariantMap（落盘形状）。
QVariantMap PlayerProgress::toVariant() const
{
    QVariantMap stats;
    stats["playTimeSecs"] = m_playTimeSecs;
    stats["daysPlayed"] = m_daysPlayed;
    stats["blocksMined"] = m_blocksMined;
    stats["blocksPlaced"] = m_blocksPlaced;
    stats["distanceTraveled"] = m_distanceTraveled;
    stats["mobsKilled"] = m_mobsKilled;
    stats["deaths"] = m_deaths;
    stats["craftsCount"] = m_craftsCount;
    stats["itemsPicked"] = m_itemsPicked;
    stats["arrowsHitMobs"] = m_arrowsHitMobs;     // t619
    stats["cropsHarvested"] = m_cropsHarvested;   // t619
    QVariantMap ach;
    for (const auto &d : achievementDefs())
        ach[QString::fromUtf8(d.id)] = m_unlocked.contains(QString::fromUtf8(d.id));
    QVariantMap out;
    out["stats"] = stats;
    out["achievements"] = ach;
    return out;
}

// 用存档 QVariantMap 整体替换内存（先清后填；单次 emit）。空 map → 重置默认。
void PlayerProgress::loadVariant(const QVariantMap &data)
{
    m_playTimeSecs = 0.0; m_daysPlayed = 0; m_blocksMined = 0; m_blocksPlaced = 0;
    m_distanceTraveled = 0.0; m_mobsKilled = 0; m_deaths = 0; m_craftsCount = 0; m_itemsPicked = 0;
    m_arrowsHitMobs = 0; m_cropsHarvested = 0;
    m_distanceAccum = 0.0f; m_playTimeFlushTimer = 0.0f; m_unlocked.clear();

    const QVariantMap stats = data.value("stats").toMap();
    if (!stats.isEmpty()) {
        m_playTimeSecs = stats.value("playTimeSecs", 0.0).toReal();
        m_daysPlayed = stats.value("daysPlayed", 0).toInt();
        m_blocksMined = stats.value("blocksMined", 0).toInt();
        m_blocksPlaced = stats.value("blocksPlaced", 0).toInt();
        m_distanceTraveled = stats.value("distanceTraveled", 0.0).toReal();
        m_mobsKilled = stats.value("mobsKilled", 0).toInt();
        m_deaths = stats.value("deaths", 0).toInt();
        m_craftsCount = stats.value("craftsCount", 0).toInt();
        m_itemsPicked = stats.value("itemsPicked", 0).toInt();
        m_arrowsHitMobs = stats.value("arrowsHitMobs", 0).toInt();     // t619（旧档缺 → 0）
        m_cropsHarvested = stats.value("cropsHarvested", 0).toInt();   // t619（旧档缺 → 0）
    }
    const QVariantMap ach = data.value("achievements").toMap();
    for (auto it = ach.begin(); it != ach.end(); ++it)
        if (it.value().toBool()) m_unlocked.insert(it.key());

    // t619：读档后按既有统计回放「计数达阈值」型成就判定（旧档可能已满足但当时无该成就定义）。
    //   走 unlock(silent) 同一条前置依赖检查路径（父未解锁则忽略，机制等价 MC 1.0；review-M5）：
    //   合法存档（父已解锁）照常恢复；「计数够但父未解锁」的存档回放不再让子成就越级解锁。
    if (m_arrowsHitMobs >= kSniperHits) unlock(QStringLiteral("sniper"), /*silent=*/true);
    if (m_cropsHarvested >= kFarmerHarvests) unlock(QStringLiteral("farmer"), /*silent=*/true);

    emit achievementChanged();
    bumpAndEmit();
}
