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
//   独立根线（t637；t752 后仅剩两条，t1000 起三条，t1020 起十条）：「起航」（骑船）/「发射!」（发射器触发）/
//   「隔墙有眼」（t1000 进入要塞结构区域——用户口径「进入到末地要塞的结构里面去了」；标准 MC 同名进度
//   跟随末影之眼，本工程无该物品 → 按进入结构落地：PlayerController tick 内 insideStronghold 边沿 →
//   enteredStronghold 信号 → Main.qml 路由）各自独立根；t1020 追加七条独立根：探索四结构（「地牢探秘」
//   「废矿来客」「沙漠寻踪」「丛林秘境」——World::inside* 区域沿信号，判定权威 = rebuildStructureRegions
//   重推导足迹）+ 生活三条（「轨道骑士」t1048 勘误口径：乘矿车到达距乘车起点单方向 ≥500 米——MC On A
//   Rail 真口径，review0913-A P2-1 / 「愿者上钩」钓鱼首获 / 「移动金库」箱车取物）。
//   t1020 亦扩两条既有埋点判定：onMobKilled 首杀四生物（蜘蛛/骸骨/潜行者/银鱼，挂「怪物猎人」下）/
//   onItemPicked 首煤·首铁·首红石（矿脉首矿，挂挖矿线下）。
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
        //   绕过父检查（仅新解锁事件走前置；同 t637 get_wood 重挂的兼容语义）。计数回放（t619）则走
        //   unlockWithAncestry 链式补前置（review #22：重挂后窄边缘档不再被父前置吞，见其实现头注释）。
        { "time_to_farm",   "crafting_table", "耕种时间到", "合成一把锄头开始耕种",
          int(ToolRegistry::HoeWood) },
        { "farmer",         "time_to_farm",   "农夫",       "收获 10 株成熟作物",
          int(RecipeRegistry::WheatId) },
        // ── 独立根线（t637：各自独立根，不挂主线；t752 后农夫已重挂「耕种时间到」下）──
        { "set_sail",       nullptr,          "起航",       "坐上船开始航行",
          int(RecipeRegistry::OakBoatId) },
        { "dispense",       nullptr,          "发射!",      "让发射器或投掷器弹出物品",
          int(BlockRegistry::Dispenser) },
        // t1000 独立根：「隔墙有眼」——进入要塞结构区域解锁（触发链见文件头独立根线注释；判定权威 =
        //   World::insideStronghold 足迹口径）。iconId = 末地传送门框架 EndPortal（要塞传送门房标志物，
        //   111 家族）。
        { "entered_stronghold", nullptr,      "隔墙有眼",   "发现了藏在地底深处的要塞",
          int(BlockRegistry::EndPortal) },
        // ── t1020 成就树四分支扩展（采矿 / 战斗 / 探索 / 生活；总数 17→31）──
        // 采矿支（首矿链：挂在既有挖矿线下，父先于子 DFS 序）：
        { "get_coal",       "mining_time",    "煤炭!",      "首次获得煤炭",
          int(RecipeRegistry::CoalId) },
        { "get_iron",       "upgrade",        "铁矿!",      "首次获得铁原矿",
          int(RecipeRegistry::IronOreDropId) },
        { "get_redstone",   "get_diamond",    "红石!",      "首次获得红石粉",
          int(RecipeRegistry::RedstoneId) },
        // 战斗支（首杀各生物代表，挂「怪物猎人」下；同事件首杀 → 父先行解锁，同 tick 双解锁）：
        //   t1046 四条首杀为工程发明项（MC 无按种首杀成就）→ 描述尾「（原创）」标注（低-6，功能不删）。
        { "kill_spider",    "monster_hunter", "织网终结者", "首次击杀蜘蛛（原创）",
          int(RecipeRegistry::SpawnEggSpiderId) },
        { "kill_bones",     "monster_hunter", "白骨收藏家", "首次击杀骸骨（原创）",
          int(RecipeRegistry::SpawnEggBonesId) },
        { "kill_stalker",   "monster_hunter", "拆弹专家",   "首次击杀潜行者（原创）",
          int(RecipeRegistry::SpawnEggStalkerId) },
        { "kill_silverfish", "monster_hunter", "石中蛀虫",  "首次击杀银鱼（原创）",
          int(BlockRegistry::StoneBrick) },
        // 探索支（进入四结构区域，各独立根 —— 同「隔墙有眼」t1000 先例，不挂线避免父前置吞沿）：
        //   t1046 四条进结构为工程发明项（MC 无进结构成就）→ 描述尾「（原创）」标注（低-6，功能不删）。
        { "entered_dungeon", nullptr,        "地牢探秘",   "发现了藏在地底的怪物房间（原创）",
          int(BlockRegistry::Spawner) },
        { "entered_mineshaft", nullptr,      "废矿来客",   "发现了废弃的地下矿井（原创）",
          int(BlockRegistry::Planks) },
        { "entered_desert_temple", nullptr,  "沙漠寻踪",   "发现了沙漠中的神殿（原创）",
          int(BlockRegistry::CutSandstone) },
        { "entered_jungle_temple", nullptr,  "丛林秘境",   "发现了丛林深处的神殿（原创）",
          int(BlockRegistry::Lever) },
        // 生活支（载具 / 渔获 / 箱车，各独立根）：
        //   t1048「轨道骑士」勘误为 MC 1.0 On A Rail 真口径：乘矿车到达距**乘车起点**单方向 ≥500 米的
        //   一点（review0913-A P2-1 wiki 引证；t1046 旧「累计 1km」口径两轴皆偏退役）。「移动金库」为
        //   工程发明项 → 尾「（原创）」标注。
        { "ride_minecart",  nullptr,          "轨道骑士",   "乘矿车到达距乘车点 500 米外的位置",
          int(RecipeRegistry::MinecartId) },
        { "first_catch",    nullptr,          "愿者上钩",   "用钓竿钓起一件获物",
          int(RecipeRegistry::RawFishId) },
        { "chest_cart_loot", nullptr,         "移动金库",   "打开装货的矿车取走物品（原创）",
          int(BlockRegistry::Chest) },
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

// review #22（出生点/进度组，2026-08-23）：计数回放链式补前置。背景：t752 把 farmer 由独立根重挂
//   「耕种时间到」（time_to_farm）下 → loadVariant 的 t619 计数回放 unlock("farmer", silent) 会撞上
//   unlock 的父前置检查（playerprogress.cpp unlock 头段）——「cropsHarvested≥10 但锄头线未解锁」的旧档
//   回放被静默吞（窄边缘：正常生存档收获必经锄头耕地 → time_to_farm 已解锁、回放本不吞；被吞档 = 创造
//   放耕地后切生存收割等旁路）。修法取 Review 建议的「链式补前置」而非注释接受：本工程 worldgen 不生成
//   耕地 / 作物，收获统计达阈 ⟹ 必经「开背包→获得原木→合成台→合成锄头」全链（旁路仅创造类，成就对创造
//   本就廉价，同调色板口径接受），祖先事实已达成，自根向下补挂不虚发；且逐级解锁保树形一致（不出「子亮
//   父锁」破相——review-M5 直插修补的同款原则）。sniper 回放**不**走本链：10 次箭命中不蕴含首杀敌对怪
//   （可全射被动生物），其父前置吞没与实时 unlock 行为一致（非 t752 重挂引入的回归面），保持现状。
void PlayerProgress::unlockWithAncestry(const QString &id, bool silent)
{
    // 沿 parentId 上溯收集祖先链（defs DFS 先序：父定义先于子，直接扫表逐级查父）。guard 上限 = 表长，
    //   防 defs 数据错误成环时死循环（正常树深 < 表长）。
    QStringList chain;
    QString cur = id;
    const auto &defs = achievementDefs();
    for (int guard = 0; guard <= defs.size(); ++guard) {
        const char *parent = nullptr;
        for (const auto &d : defs) {
            if (cur == QLatin1String(d.id)) { parent = d.parentId; break; }
        }
        chain.prepend(cur);
        if (!parent) break; // 到根
        cur = QLatin1String(parent);
    }
    for (const QString &step : chain)
        unlock(step, silent); // 根先解锁 → 每级前置恒过；已解锁级幂等 no-op（unlock 首行早退）
}

void PlayerProgress::onBlockMined()    { ++m_blocksMined;   bumpAndEmit(); }
void PlayerProgress::onBlockPlaced()   { ++m_blocksPlaced;  bumpAndEmit(); }

// 每帧位移增量累加（节流 flush：不自带计时器，借 onPlayTimeTick 的 kFlushInterval 闸门一并并入 distanceTraveled，
//   免每帧抖 QML 绑定，同 playercontroller flowScan 节流模式）。
void PlayerProgress::onMove(float deltaBlocks)
{
    m_distanceAccum += deltaBlocks;
}

// 游戏时间 tick：累加 playTimeSecs；每 kFlushInterval 秒 flush（emit progressChanged + 把累积距离
//   （走过路程 + t1046 矿车里程两路）并入各自统计）。
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
        if (m_minecartAccum > 0.0f) {
            m_minecartTravelBlocks += qreal(m_minecartAccum);
            m_minecartAccum = 0.0f;
        }
        bumpAndEmit();
    }
}

// 击杀 mob：累加 + 判「怪物猎人」（敌对 mob Shambler=4/Bones=5/Stalker=6/Spider=7；§9 改名）。
// t1020 首杀各生物：蜘蛛 / 骸骨 / 潜行者 / 银鱼（洞穴蜘蛛计入蜘蛛系 —— 同族，不设独立成就）。父
//   「怪物猎人」同事件先行解锁（首杀即怪猎 + 对应首杀，同 tick 双解锁；infoToast 单槽 → 后者覆盖
//   前者文案，两项均入树 —— MC advancement 链同观感，登记）。
void PlayerProgress::onMobKilled(int mobType)
{
    ++m_mobsKilled;
    using MT = EntityManager::MobType;
    if (mobType == MT::MobShambler || mobType == MT::MobBones
        || mobType == MT::MobStalker || mobType == MT::MobSpider
        || mobType == MT::MobCaveSpider) // t1012③ 洞穴蜘蛛计入「怪物猎人」（敌对型同列）
        unlock("monster_hunter");
    if (mobType == MT::MobSpider || mobType == MT::MobCaveSpider)
        unlock("kill_spider");
    if (mobType == MT::MobBones)
        unlock("kill_bones");
    if (mobType == MT::MobStalker)
        unlock("kill_stalker");
    if (mobType == MT::MobSilverfish)
        unlock("kill_silverfish");
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
// t1020 矿脉首矿：CoalId→「煤炭!」（挂「挖矿时间到」，木镐可挖）/ IronOreDropId→「铁矿!」（挂
//   「获得升级」，石镐可挖）/ RedstoneId→「红石!」（挂「钻石!」，红石矿需铁镐采掘同进度位阶）。
void PlayerProgress::onItemPicked(int itemId)
{
    ++m_itemsPicked;
    if (itemId == int(BlockRegistry::Log) || itemId == int(BlockRegistry::SpruceLog))
        unlock("get_wood");
    if (itemId == int(RecipeRegistry::DiamondId))
        unlock("get_diamond");
    if (itemId == int(RecipeRegistry::CoalId))
        unlock("get_coal");
    if (itemId == int(RecipeRegistry::IronOreDropId))
        unlock("get_iron");
    if (itemId == int(RecipeRegistry::RedstoneId))
        unlock("get_redstone");
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

// 进入要塞结构区域 → 「隔墙有眼」（t1000；player.enteredStronghold 一次性边沿信号经 Main.qml 路由）。
//   unlock 幂等：重复进出的重复沿 / 读档重进的重放沿均早退，不重发 toast。
void PlayerProgress::onEnteredStronghold() { unlock("entered_stronghold"); }

// 附魔台附魔成功 → 「附魔师」。
void PlayerProgress::onEnchanted() { unlock("enchanter"); }

// 附书产附魔书 → 「书虫」。
void PlayerProgress::onEnchantedBookObtained() { unlock("bookworm"); }

// 铁砧成功操作 → 「铁匠」。
void PlayerProgress::onAnvilUsed() { unlock("blacksmith"); }

// ── t1020 新埋点（成就树四分支扩展）──

// 进入结构区域 → 对应探索成就（kind 数值契约 = World::StructureKind：0 地牢 / 1 废弃矿井 /
//   2 沙漠神殿 / 3 丛林神殿；本层不持 World（PLAN §2 Game/ViewModel 零向上依赖），数值契约注释
//   绑定同 BlockRegistry 字面量先例）。unlock 幂等：重复进出的重复沿 / 读档重进的重放沿均早退，
//   不重发 toast（同 onEnteredStronghold）。越界 kind 防御忽略。
void PlayerProgress::onStructureEntered(int kind)
{
    switch (kind) {
    case 0: unlock("entered_dungeon"); break;        // World::StructureDungeon
    case 1: unlock("entered_mineshaft"); break;      // World::StructureMineshaft
    case 2: unlock("entered_desert_temple"); break;  // World::StructureDesertTemple
    case 3: unlock("entered_jungle_temple"); break;  // World::StructureJungleTemple
    default: break;                                  // 越界防御（契约外 kind 忽略）
    }
}

// t1048 乘矿车径向位移埋点（头注释见 .h；R19.23 批次 review A P2-1 勘误 = MC 1.0 On A Rail 真口径）：
//   delta 只喂节流累积器（矿车里程统计 display-only，负 / 零增量防御忽略，t1046 语义保留）；径向判定
//   每帧绝对位置采样：起点→当前位置水平距离平方 ≥ kMinecartRideGoal² 即解锁——平方比较免开方且恰阈
//   缝精确，回折 / 绕圈累计不缩径向（与累计制的分野，探针 t1046e 钉死）。unlock 幂等（越阈续乘 /
//   回折的重复判定早退，不重发 toast）；起点未记录 → 只累积不判定。
void PlayerProgress::onMinecartMoved(float deltaBlocks, qreal x, qreal z)
{
    if (deltaBlocks > 0.0f)
        m_minecartAccum += deltaBlocks; // 里程统计只计正向增量（负 / 零防御忽略）
    if (!m_minecartRideActive) return;  // 起点未记录（未骑上先喂点防御）→ 不判径向
    const qreal dx = x - m_minecartRideOrigin.x();
    const qreal dz = z - m_minecartRideOrigin.y();
    if (dx * dx + dz * dz >= kMinecartRideGoal * kMinecartRideGoal)
        unlock("ride_minecart");
}

// t1048 骑乘起点记录（Main.qml ridingCart 上升沿调；口径与登记见 .h 头注释）。
void PlayerProgress::onMinecartRideStarted(qreal x, qreal z)
{
    m_minecartRideOrigin = QPointF(x, z);
    m_minecartRideActive = true;
}

// 钓竿收竿获物 → 「愿者上钩」（幂等 unlock；获物实体由 Game 层直调生成，本埋点仅成就口径）。
void PlayerProgress::onFishCaught() { unlock("first_catch"); }

// 打开箱子矿车 → 「移动金库」（幂等 unlock；t1013 链：矿井标记箱转正的箱车，首开填充矿井池）。
void PlayerProgress::onChestCartOpened() { unlock("chest_cart_loot"); }

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
    add("矿车里程",   QString("%1 格").arg(int(m_minecartTravelBlocks))); // t1046
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
    stats["minecartTravelBlocks"] = m_minecartTravelBlocks; // t1046
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
    m_arrowsHitMobs = 0; m_cropsHarvested = 0; m_minecartTravelBlocks = 0.0;
    m_distanceAccum = 0.0f; m_minecartAccum = 0.0f; m_playTimeFlushTimer = 0.0f; m_unlocked.clear();
    // t1048 径向判定窗一并清（跨世界换档残留清零；径向起点不入档，未达阈档续玩由下次骑乘重开窗补判）。
    m_minecartRideOrigin = QPointF(0.0, 0.0); m_minecartRideActive = false;

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
        m_minecartTravelBlocks = stats.value("minecartTravelBlocks", 0.0).toReal(); // t1046（旧档缺 → 0）
    }
    const QVariantMap ach = data.value("achievements").toMap();
    for (auto it = ach.begin(); it != ach.end(); ++it)
        if (it.value().toBool()) m_unlocked.insert(it.key());

    // t619：读档后按既有统计回放「计数达阈值」型成就判定（旧档可能已满足但当时无该成就定义）。
    //   走 unlock(silent) 同一条前置依赖检查路径（父未解锁则忽略，机制等价 MC 1.0；review-M5）：
    //   合法存档（父已解锁）照常恢复；「计数够但父未解锁」的存档回放不再让子成就越级解锁。
    //   review #22：farmer 改走 unlockWithAncestry（t752 重挂后窄边缘档被父前置静默吞 → 链式补前置，
    //   见其实现头注释）；sniper 保持原 unlock（计数不蕴含父，吞没与实时行为一致）。
    if (m_arrowsHitMobs >= kSniperHits) unlock(QStringLiteral("sniper"), /*silent=*/true);
    if (m_cropsHarvested >= kFarmerHarvests) unlockWithAncestry(QStringLiteral("farmer"), /*silent=*/true);

    emit achievementChanged();
    bumpAndEmit();
}
