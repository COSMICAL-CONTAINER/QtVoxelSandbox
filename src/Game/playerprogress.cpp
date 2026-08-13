#include "playerprogress.h"

#include <QSet>
#include <QVariantMap>

#include "blockregistry.h"   // Log/SpruceLog/CraftingTable 方块 id（成就判定）
#include "toolregistry.h"    // SwordWood/PickaxeWood/石质工具 id（合成成就判定）
#include "entitymanager.h"   // MobType（敌对 mob 判「怪物猎人」）

// 成就定义（progress 新系统）。机制等价 MC 1.0 advancement tree 的现阶段可完成子集。
//   §9：成就名用中文通用词，零 MC 专名。定义序 = QML 列表显示序（从「打开背包」入门 → 装备升级 → 战斗）。
const QList<PlayerProgress::AchievementDef> &PlayerProgress::achievementDefs()
{
    static const QList<AchievementDef> kDefs = {
        { "open_inventory", "打开背包",   "按 E 打开你的背包" },
        { "get_wood",       "获得原木",   "砍倒一棵树获得原木" },
        { "crafting_table", "合成台",     "用 4 块木板合成工作台" },
        { "sword_time",     "出击时间",   "合成一把木剑准备战斗" },
        { "mining_time",    "挖矿时间到", "合成一把木镐开始挖矿" },
        { "upgrade",        "获得升级",   "升级到石质工具" },
        { "monster_hunter", "怪物猎人",   "击杀一只敌对怪物" },
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

// 解锁成就（id）。已解锁 → no-op（幂等，防重复弹 toast）；首次解锁 → 标记 + 弹 toast + 列表刷新 + revision bump。
void PlayerProgress::unlock(const QString &id)
{
    if (m_unlocked.contains(id)) return;
    m_unlocked.insert(id);
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

// 合成成功：累加 + 据产物判成就。CraftingTable→「合成台」；SwordWood→「出击时间」；
//   PickaxeWood→「挖矿时间到」；任一石质工具→「获得升级」。
void PlayerProgress::onCraft(int resultId)
{
    ++m_craftsCount;
    if (resultId == int(BlockRegistry::CraftingTable)) unlock("crafting_table");
    if (resultId == int(ToolRegistry::SwordWood))      unlock("sword_time");
    if (resultId == int(ToolRegistry::PickaxeWood))    unlock("mining_time");
    using TI = ToolRegistry::ToolId;
    if (resultId == int(TI::PickaxeStone) || resultId == int(TI::HoeStone)
        || resultId == int(TI::AxeStone) || resultId == int(TI::ShovelStone)
        || resultId == int(TI::SwordStone))
        unlock("upgrade");
    bumpAndEmit();
}

// 拾取物品：累加 + Log/SpruceLog→「获得原木」。
void PlayerProgress::onItemPicked(int itemId)
{
    ++m_itemsPicked;
    if (itemId == int(BlockRegistry::Log) || itemId == int(BlockRegistry::SpruceLog))
        unlock("get_wood");
    bumpAndEmit();
}

// 打开任意背包面板 → 解锁「打开背包」。
void PlayerProgress::onInventoryOpened() { unlock("open_inventory"); }

// 设当前天数（WorldClock.dayCount 单调）。仅当 > 当前 daysPlayed 时更新。
void PlayerProgress::setDayCount(int day)
{
    if (day > m_daysPlayed) { m_daysPlayed = day; bumpAndEmit(); }
}

// 全部成就 [{id,name,desc,unlocked}, ...]（定义序）。
QVariantList PlayerProgress::achievements() const
{
    QVariantList out;
    for (const auto &d : achievementDefs()) {
        QVariantMap item;
        item["id"] = QString::fromUtf8(d.id);
        item["name"] = QString::fromUtf8(d.name);
        item["desc"] = QString::fromUtf8(d.desc);
        item["unlocked"] = m_unlocked.contains(QString::fromUtf8(d.id));
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
    }
    const QVariantMap ach = data.value("achievements").toMap();
    for (auto it = ach.begin(); it != ach.end(); ++it)
        if (it.value().toBool()) m_unlocked.insert(it.key());

    emit achievementChanged();
    bumpAndEmit();
}
