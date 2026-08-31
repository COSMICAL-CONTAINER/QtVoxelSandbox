#ifndef ENCHANTREGISTRY_H
#define ENCHANTREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString
#include <QVariantList>

#include "blockregistry.h" // ToolType（武器/工具判定借 ToolType.Sword/Pickaxe/...）
#include "toolregistry.h"  // 工具段 id → ToolDef（type/tier）
#include "armor.h"         // 护甲段判定（ArmorRegistry::isArmor）

// 附魔注册表 + 附魔选择逻辑（Game 层；机制等价 MC 1.0 附魔系统）。
//
// 与 ToolRegistry / ArmorRegistry 同风格：纯静态数据表，无实例、无 Q_OBJECT。本表持有「附魔 →
// 附魔属性」（类别适用面 / 最大等级 / 权重 / 名）。附魔 id 是本表自有的小整数段（1..20，见 EnchantId 枚举），
// **不**进物品 id 段（附魔是物品的元数据，附在 ItemStack.enchants[4] 上，非独立物品）。
//
// 物品类别（机制等价 MC 1.0「附魔按物品类型分流」）：
//   - Weapon（剑）：锐锋 / 亡灵杀手 / 节肢克星 / 击退 / 燃焰。
//   - Tool（镐 / 锄 / 斧 / 铲）：效率 / 精准采集 / 时运。
//   - Armor（头盔 / 胸甲 / 护腿 / 靴子）：保护 / 火焰保护 / 摔落保护 / 弹射物保护 / 水上亲和。
//   - BowItem（弓，t960）：劲射 / 震击 / 燃箭 / 不竭（弓专属四件）。
//   - RodItem（钓鱼竿，t960）：唤潮 / 缠咬（钓竿专属两件）。
//   - 耐久（Unbreaking）适用**全部五类**（武器 / 工具 / 护甲 / 弓 / 钓竿均可附），用 appliesToMask 位掩码表达。
//   - 剪刀（Shears）无专属附魔 → 不可附魔（categoryForItem 判 None）。
//
// 附魔选择（机制等价 MC 1.0 附魔台「按 offered level + 随机种子从适用池加权抽 1–3 个附魔 + 各自等级」）：
//   selectEnchantsForItem(itemId, offeredLevel, seed) 给定物品 + 提供等级（1..30，来自 t474 书架加成）+
//   每槽随机种子，返回 [{id, level}, ...]（已剔除互斥冲突，如锐锋 / 亡灵杀手 / 节肢克星三选一）。候选池按
//   isApplicableForItem 逐物品精判（t824：镐不出亡灵杀手 / 锄不给选项）。offeredLevel 越高 → 附魔数越多
//   （1→3）+ 单附魔等级越高（趋近 maxLevel）。纯函数（无副作用 / 无 IO），附魔台 UI 点选项槽时调 → 把结果
//   写入目标物品 ItemStack.enchants（Hotbar::enchantSelected）。
//
// t959 书附魔池主类别收窄（用户第五轮口径「书本附魔把很多工具+装甲附魔冲突地混在一起」）：书载体的
//   候选池本 = isApplicableForItem 全过 → 全附魔并集，无任何类别过滤 → 单次施法常出「保护+效率+
//   锐锋」这类**任何单件物品都戴不上**的跨类乱炖。收窄口径 = 施法时先由同一确定性 LCG 定一个**主类别**
//   （武器/工具/装甲/弓/竿五类均匀轮——t959 起三类，t960 弓/竿系入表扩五类（kBookMainCats 扩位）），
//   本轮产物全部从「该类池 ∪ 通用（耐久）」抽 → 单次产物同类成簇；同次产物内 conflictGroup 互斥
//   （保护系四件互斥 / 锐锋+亡灵+节肢三选一 / 采集系三选一）由既有位集抽样保证。
//   跨施法主类别随机轮换 → 全 20 附魔在书池**长期仍都可达**（union 不收窄，只收窄单次产物内部）。
//   直附面（对工具/武器/护甲/弓/竿直接施法）不经此分支 —— 池本就按 isApplicableForItem 逐物品精判
//   （t824），不存在跨类混出。
//
// §4 法律 + §9：附魔名用**通用描述词**（锐锋 / 亡灵杀手 / 节肢克星 / 击退 / 燃焰 / 效率 / 精准采集 / 时运 /
//   耐久 / 保护 / 火焰保护 / 摔落保护 / 弹射物保护 / 水上亲和）—— 非 MC 专名（sharpness / Smite / … 仅为
//   机制等价参考，代码 / 用户可见字串绝不用原名）。机制对齐 MC Java 1.0.0，名词 / 数值原创。
//
// 分层（PLAN §2）：本层属 Game，只依赖 Core（BlockRegistry::ToolType）+ 同层 ToolRegistry / ArmorRegistry
// （判定物品类别），**不**依赖 Renderer/Physics/QtQuick3D。依赖只向下。
class EnchantRegistry
{
public:
    // 物品类别（决定哪些附魔适用）。用位掩码表达「适用面」（耐久适用多类）。t615 细化适用域：
    //   - Weapon（剑）+ 斧：锐锋族三选一互斥（机制等价 MC「斧可附武器系附魔」）；击退 / 燃焰仅剑。
    //   - Tool（镐/斧/铲）：效率全适用；精准采集 = 镐·铲·斧；时运 = 镐·铲（**锄 t824 起判 None 不可附魔**）。
    //   - Armor：保护/火焰保护/弹射物保护全护甲；摔落保护仅靴；水上亲和仅头盔；保护系四者互斥（组 2/3 见 .cpp）。
    //   - BookItem（书，t615 附魔台附书载体）：全附魔池随机（机制等价 MC enchanted book 全池）。
    //   耐久（Unbreaking）适用**全部三类 + 书**（Weapon|Tool|Armor|BookItem）。
    enum Category : int {
        None   = 0,
        Weapon = 1,  // bit0
        Tool   = 2,  // bit1
        Armor  = 4,  // bit2
        // t615 附魔书载体位（bit3）：itemEnchantCategory(BookId) 返 BookItem；selectEnchants 对它取
        //   **t959 主类别成簇池**（施法先随机定主类别，产物 ⊆ 该类 ∪ 通用——见 selectEnchantsForItem），
        //   机制等价 MC「附魔台附书从全池随机」的收窄版（单次产物不跨类混出）。
        BookItem = 8,
        // t960 弓 / 钓鱼竿位（bit4 / bit5；用户第五轮口径「弓/钓竿专属附魔」——专属 = 适用物品门严：
        //   弓四件只上弓、竿两件只上钓竿，书载体全过、其他物品全拒）。旧版两工具类型判 None 不可附魔，
        //   t960 起入池（剪刀仍 None——无专属附魔）。
        BowItem  = 16, // bit4（弓：劲射 / 震击 / 燃箭 / 不竭 + 耐久）
        RodItem  = 32, // bit5（钓鱼竿：唤潮 / 缠咬 + 耐久）
    };

    // t959 附魔「主类别」（**附魔归属哪类装备**；书附魔池按它成簇收窄）。与上方 Category（**物品**
    //   类别，决定物品能附什么）是两个正交维度：本枚举答「这条附魔属于哪类装备的池」。universal（耐久）
    //   不入任何单类 —— 任何主类别池都可出；none 仅 NoEnchant 占位行。取值连续小整数（非位掩码——
    //   一条附魔只归一类，主类别轮按值均匀取样）。
    enum EnchantCategory : int {
        EnchantCatNone      = 0, // 占位（NoEnchant 行）/ 未归类
        EnchantCatWeapon    = 1, // 武器系：锐锋 / 亡灵杀手 / 节肢克星 / 击退 / 燃焰
        EnchantCatTool      = 2, // 工具系：效率 / 精准采集 / 时运
        EnchantCatArmor     = 3, // 护甲系：保护 / 火焰保护 / 摔落保护 / 弹射物保护 / 水上亲和
        EnchantCatUniversal = 4, // 通用：耐久（武器/工具/护甲/书全适用 → 任何主类别池均可出）
        // t960 弓 / 钓竿系（用户第五轮口径「弓（力量/冲击/火矢/无限类）、钓竿（海之眷顾/饵钓类）」——
        //   名称原创，机制对位）。入 EnchantCategory 枚举 → 书附魔池主类别轮（kBookMainCats，t959 预留的
        //   扩展位）同步扩五类：书施法可轮出弓 / 竿专属附魔（书池长期可达性保持），单次产物仍同类成簇。
        EnchantCatBow       = 5, // 弓系：劲射 / 震击 / 燃箭 / 不竭
        EnchantCatRod       = 6, // 钓竿系：唤潮 / 缠咬
    };

    // 附魔 id（本表自有小整数段 1..20；0 = 无附魔 / 空槽哨兵）。追加新附魔在末尾续号，不重排（ItemStack.
    //   enchants[4] 每槽按 (id<<8)|level 打包，旧存档 / 数据向后兼容）。机制等价 MC 1.0 附魔集（§9 改名）：
    //   - 武器（剑）：锐锋(sharpness) / 亡灵杀手(Smite) / 节肢克星(arthropod-slayer) / 击退(knockback) / 燃焰(fire-aspect)。
    //   - 工具：效率(efficiency) / 精准采集(silk-touch) / 时运(fortune)。
    //   - 通用：耐久(unbreaking) —— 适用武器 / 工具 / 护甲 / 弓 / 钓竿全类（t960 扩弓 / 竿）。
    //   - 护甲：保护(protection) / 火焰保护(fire-protection) / 摔落保护(feather-fall) /
    //     弹射物保护(projectile-protection) / 水上亲和(aqua-affinity)。
    //   - t960 弓专属（用户口径「力量/冲击/火矢/无限类」，名称原创；机制等价 MC bow enchantments）：
    //     劲射(might，箭伤 +1HP/级) / 震击(bow-shock，箭命中击退增强) / 燃箭(bright-draw，命中点燃) /
    //     不竭(never-run，射箭不耗箭——完全无限口径)。
    //   - t960 钓竿专属（用户口径「海之眷顾/饵钓类」）：唤潮(tide-call，咬钩等待期缩短) /
    //     缠咬(bite-call，咬钩判定窗加宽——kBobberBiteWindowSec 基值 1.0s 用户口径钉死不动，附加式加宽)。
    enum EnchantId : int {
        NoEnchant       = 0,
        Sharpness       = 1,  // 锐锋：剑伤害 +0.5*level（Game 层 attackMob 命中时叠加，暴击后乘 1.5；max 5；t476 链 t763 复核）
        UndeadSlay      = 2,  // 亡灵杀手：对亡灵类 mob 伤害 +（机制等价 MC Smite；max 5）
        ArthropodSlay   = 3,  // 节肢克星：对节肢类 mob 伤害 +（机制等价 MC arthropod-slayer；max 5）
        Knockback       = 4,  // 击退：命中 mob 击退距离 +（机制等价 MC knockback；max 2）
        FireAspect      = 5,  // 燃焰：命中 mob 点燃（机制等价 MC fire-aspect；max 2）。t919 账目钉死：不进
                               //   weaponAttackDamage（直伤/攻击面板 0 加成），输出全在 ignite(level×4s) 的 DoT。
        Efficiency      = 6,  // 效率：挖掘速度 +（机制等价 MC efficiency；max 5）
        SilkTouch       = 7,  // 精准采集：掉落方块自身（机制等价 MC silk-touch；max 1）
        Fortune         = 8,  // 时运：掉落倍率 +（机制等价 MC fortune；max 3）
        Unbreaking      = 9,  // 耐久：耐久消耗概率 -（机制等价 MC unbreaking；max 3；适用武器/工具/护甲/弓/钓竿）
        Protection      = 10, // 保护：通用减伤（机制等价 MC protection；max 4）
        FireProtection  = 11, // 火焰保护：火焰伤害减免（机制等价 MC fire-protection；max 4）
        FeatherFall     = 12, // 摔落保护：摔落伤害减免（机制等价 MC feather-fall；max 4）
        ProjectileProt  = 13, // 弹射物保护：弹射物伤害减免（机制等价 MC projectile-protection；max 4）
        AquaAffinity    = 14, // 水上亲和：水下挖掘免除 ×5 耗时惩罚（生效点 playercontroller updateMining；t763 补）
        // ── t960 弓专属四件（appliesToMask = BowItem|BookItem；仅弓；书载体全过）──
        Might           = 15, // 劲射：箭伤害 +1HP/级（生效点 playercontroller endBowDraw 伤害计算；
                              //   EnchantRegistry::bowArrowDamage 单一权威；max 3）
        BowShock        = 16, // 震击：箭命中击退增强（生效点 EntityManager 箭命中 knockback 强度 ×(1+level)；
                              //   EnchantRegistry::bowKnockbackMultiplier 单一权威；max 2）
        BrightDraw      = 17, // 燃箭：箭命中点燃（机制等价 MC flame；生效点 EntityManager 箭命中 ignite，
                              //   时长 EnchantRegistry::bowIgniteSeconds 单一权威；max 1）
        NeverRun        = 18, // 不竭：射箭不耗箭（完全无限口径；仍须背包有 ≥1 箭才可拉弓——beginBowDraw
                              //   门槛不变，仅 endBowDraw 免消耗；max 1）。与「修复系」互斥是惯例，本作无
                              //   修复系附魔 → 组 0 登记；未来入修复系时改互斥组即可
        // ── t960 钓竿专属两件（appliesToMask = RodItem|BookItem；仅钓竿）──
        TideCall        = 19, // 唤潮：咬钩等待期缩短（等待 = 确定性掷骰 × (1−0.2×级)，生效点 EntityManager
                              //   浮标等待掷骰两处；EnchantRegistry::rodWaitScale 单一权威；max 3）
        BiteCall        = 20, // 缠咬：咬钩判定窗加宽（窗口 = kBobberBiteWindowSec(1.0 用户口径钉死) +
                              //   0.5s×级，附加式——基值常量不动；EnchantRegistry::rodBiteWindowExtra
                              //   单一权威；max 3）
        EnchantCount    = 21, // 哨兵：合法附魔 id 上界（1..20；0 = 无附魔）。
    };

    // 附魔定义。表行索引 == enchantId（连续 1..14；详见 enchantregistry.cpp kEnchants）。
    struct EnchantDef {
        int id;              // EnchantId
        int homeCategory;    // t959 附魔主类别（EnchantCategory；书附魔池按它成簇收窄 + 探针数据钉）
        int appliesToMask;   // Category 位掩码（适用物品类别并集；耐久 = Weapon|Tool|Armor）
        int maxLevel;        // 最大等级（1..5）
        int weight;          // 权重（越大越常被选中；机制等价 MC 附魔 rarity weight）
        int exclusiveGroup;  // 互斥组（同组附魔不能共存于同一物品；0 = 无互斥）。锐锋/亡灵杀手/节肢克星 = 组 1。
        const char *name;    // 内部 / 调试用名（英文标识符；非面向用户）
        const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词）
    };

    // 附魔判定（id 在 [1, EnchantCount) 内）。
    static bool isEnchant(int enchantId);
    // 取附魔定义。非附魔 id → nullptr。
    static const EnchantDef *enchant(int enchantId);

    // 附魔是否适用给定物品类别（appliesToMask & catMask ≠ 0）。
    static bool isApplicable(int enchantId, int catMask);
    // t959 附魔主类别查询（EnchantCategory 枚举值；非附魔 → EnchantCatNone）。书附魔池成簇的分流键 +
    //   探针逐 id 数据钉读数。
    static int homeCategory(int enchantId);
    // t959 冲突组号查询（exclusiveGroup 单值形式；0 = 无互斥）。与 conflictsWith(id, other) 同一数据源
    //   （逐附魔读组号 vs 成对判互斥）—— 探针数据钉 / 冲突提示文案用。
    static int conflictGroup(int enchantId);
    // t615 附魔是否适用**具体物品**（据 ToolRegistry 类型 / ArmorRegistry 部位精判，非仅大类）：
    //   - 锐锋族（1/2/3）：剑 + 斧（Axe）。
    //   - 击退（4）/ 燃焰（5）：仅剑。
    //   - 效率（6）：镐 / 斧 / 铲（锄 t824 起 categoryForItem=None 不可附魔）。
    //   - 精准采集（7）：镐 / 铲 / 斧。时运（8）：镐 / 铲。
    //   - 耐久（9）：全部工具 + 全部护甲 + 弓 + 钓鱼竿（t960 扩弓 / 竿）。
    //   - 保护（10）/ 火焰保护（11）/ 弹射物保护（13）：全护甲。摔落保护（12）：仅靴。水上亲和（14）：仅头盔。
    //   - t960 弓四件（15-18）：仅弓。竿两件（19/20）：仅钓鱼竿（「专属」= 适用物品门严，书载体全过）。
    //   dev-plan §附魔设计表逐条核对（t475 只按大类判定 → 本方法为铁砧敲附魔书的**逐条适用过滤**权威）。
    static bool isApplicableForItem(int enchantId, int itemId);
    // t615 冲突组查询：两附魔是否互斥（同 exclusiveGroup 非 0 且相同；同 id 不算冲突 —— 等级合并走另路）。
    //   冲突组（dev-plan §3 表）：组 1 = 锐锋/亡灵杀手/节肢克星（伤害系）；组 2 = 效率/精准采集/时运
    //   （采集系）；组 3 = 保护/火焰保护/摔落保护/弹射物保护（保护系四互斥）。供铁砧敲附魔判定
    //   「书上附魔与目标已有附魔互斥 → 不上（红字冲突）」+ 附魔台随机池排除冲突。
    static bool conflictsWith(int enchantId, int otherEnchantId);
    // 最大等级。非附魔 → 0。
    static int maxLevel(int enchantId);
    // 权重。非附魔 → 0。
    static int weight(int enchantId);
    // 用户可见中文显示名（PLAN §9 override (b) 通用词）。非附魔 → 空串。
    static QString displayName(int enchantId);

    // 物品类别（单一权威：据 item id 查 ToolRegistry / ArmorRegistry；决定哪些附魔适用）。
    //   - 护甲段（ArmorRegistry::isArmor）→ Armor。
    //   - 工具段（ToolRegistry::isTool）→ 据 ToolDef.type：Sword→Weapon / Pickaxe·Axe·Shovel→Tool /
    //     **Hoe→None（t824：锄 MC 1.0 无适用附魔 → 不可附魔——附魔台槽 0 拒入 / 铁砧书合并拒）** /
    //     **Bow→BowItem / FishingRod→RodItem（t960：弓 / 钓竿专属附魔入池 → 可附魔；剪刀 Shears 仍 None）**。
    //   - t615 书（RecipeRegistry::BookId）→ BookItem（附魔台附书载体：t959 起主类别成簇池 → 产附魔书）。
    //   - 方块段 / 材料段（含附魔书物品本身——书不可再附）/ 越界 → None（不可附魔）。
    // 返回 Category 位值（None / Weapon / Tool / Armor / BookItem / BowItem / RodItem）；非位掩码叠加（单类别）。
    static int categoryForItem(int itemId);

    // t824 附魔选择（按**具体物品**过滤；附魔台三档选项池单一权威）。机制等价 MC 1.0 附魔台。纯函数：
    //   给定物品 id + 提供等级 + 随机种子 → 返回 [{id: int, level: int}, ...]（QVariantMap list；已剔互斥
    //   冲突、已剔重复、等级钳到 maxLevel）。
    //   候选池 = isApplicableForItem(enchantId, itemId) 逐条精判（**非旧版大类 mask 门**——旧 selectEnchants
    //   按 Category 过滤令镐 / 铲可出亡灵杀手（mask=Weapon|Tool 含 Tool 位）、胸甲可出摔落保护、锄可出效率，
    //   即用户报「镐子附上亡灵杀手」根因）。对齐 t763/t798 适用表：镐/铲→效率·耐久·时运·精准；斧→+锐锋族
    //   （无时运）；剑→锐锋族·击退·燃焰·耐久；护甲按部位（靴+摔落保护 / 头盔+水上亲和）；
    //   书（BookId）→ **t959 主类别成簇池**（施法先随机定主类别，产物 ⊆ 该类 ∪ 通用——附魔台附书 /
    //   战利品附魔书同入口同收窄；t960 起主类别轮扩五类：武器/工具/护甲/弓/竿——弓 / 竿专属附魔经书
    //   施法可达）；锄 / 剪刀 → 空 list（不给选项；弓 / 钓竿 t960 起各给专属池 + 耐久）。
    //   offeredLevel 1..30（来自 t474 书架加成映射到三槽）；seed 任意 int（同槽同 seed 同结果，重投换 seed
    //   换选项）。附魔数 = 1..3（offeredLevel 越高越多）；单附魔等级随 offeredLevel 趋 maxLevel。
    static QVariantList selectEnchantsForItem(int itemId, int offeredLevel, int seed);

    // t795 附魔台书架门槛公式（书架数 → 可选档位 / 提供等级；单一权威，QML 经 Hotbar 包装调用）：
    //   - tierForBookshelves(bookshelves)：书架数 → 最高可选档位（1 档恒开；≥5 → 2 档；≥10 → 3 档）。
    //     **与游戏模式无关**（函数无模式参数 —— 创造模式同样须书架达标才有高档，t795 收口；t694 的 QML
    //     creativeMode 直通 3 档旁路删除，公式收编至此防两处漂移）。
    //   - offeredLevelFor(bookshelves, tierIdx)：tierIdx 0..2 → 提供等级 floor(bs*20*(tier+1)/33)+(tier+1)，
    //     钳 [1,30]。锚点：bs=0 → [1,2,3]；bs=4 → 3 档 10；bs=14 → 3 档 28；bs=15 → [10,20,30] —— 顶格 30
    //     仅满 15 书架可达（书架数上限封顶；对 bs 单调不减）。tierIdx 越界钳回 [0,2]、bs 钳 [0,15]（防御）。
    static int tierForBookshelves(int bookshelves);
    static int offeredLevelFor(int bookshelves, int tierIdx);

    // 打包 / 拆包附魔到 ItemStack.enchants[4] 每槽的 int（(enchantId<<8)|level；0 = 空槽）。
    //   供 Hotbar 内部读写用（QML 边界走 QVariantList<int> 4 元素，每元素 = 本 pack 值）。
    static int pack(int enchantId, int level);
    static int packEnchantId(int packed);
    static int packLevel(int packed);

    // t476 在物品 4 槽附魔元数据中查指定附魔的等级（0 = 无）。供 Game 层在挖掘 / 攻击 / 受击 calc point
    //   读手持 / 装备物品的附魔等级并据此改伤害 / 速度 / 掉落（机制等价 MC「读 item enchantments 算附魔效果」）。
    //   enchants 指向 4 个 packed int（同 ItemStack.enchants[4] 布局；0=空槽）。非附魔 id / 空指针 / 越界 → 0。
    //   同一附魔不会重复（selectEnchants 已剔重复），故取首个匹配槽的 level 即可。
    static int findLevel(const int *enchants, int enchantId);

    // 等级 → 罗马数字后缀字符串（如 level=3 → "III"；level=1 → "I"）。供 tooltip / 附魔台显示「锐锋 III」。
    //   level<=0 → 空串；level 1..5 → I/II/III/IV/V；>5 → 阿拉伯数字（防御）。
    static QString levelSuffix(int level);

    // t825 手持武器攻击伤害（**显示与实战同源单一权威**）：ToolRegistry::attackDamage 基础 + 锐锋 ×0.5/级
    //   （目标无关部分）。PlayerController::attackMob 以此为起点（再叠亡灵 / 节肢对族加成 → 暴击 ×1.5 →
    //   下限 1）；全部 tooltip 攻击行经 Hotbar::displayAttackDamage 取整显示 round(本值)。修「附锋利后显示
    //   仍基础值」类公式漂移：改加成系数只改这里，战斗与九处 UI 显示同步变。enchants 空指针 → 仅基础。
    static float weaponAttackDamage(int itemId, const int *enchants);

    // t961 杀手系对族伤害加成（**显示与实战同源单一权威**）。familyAttackBonusFor(enchants, id) =
    //   单支权威（亡灵杀手 / 节肢克星之一）的每级 2.5 倍——**每级 2.5 这个数值只活本函数**，实战
    //   （PlayerController::attackMob，t476 链；族门在调用侧：蹒跚者 / 骸骨 / 幼体取亡灵支、蜘蛛取节肢
    //   支，交叉恒 0 即族门不外泄）与显示合计面共用这一个字面量（review0830 #25 收口实战/显示双写
    //   漂移面，同 weaponAttackDamage 的单一权威纪律）；非杀手系 id → 0。
    //   familyAttackBonus(enchants) = 显示面合计 = 两支相加（互斥组 1 保证两杀手不共存于同一物品 →
    //   至多一支非零，合计 == 实际可生效的那支）。tooltip 攻击行括号「(+M)」经
    //   Hotbar::displayFamilyBonusText 取整显示。enchants 空指针 → 0。
    static float familyAttackBonusFor(const int *enchants, int enchantId);
    static float familyAttackBonus(const int *enchants);

    // t826 击退附魔强度（单一权威）：strength = 1 + 3.0*level（无附魔 1.0 / I 4.0 / II 7.0）。
    //   EntityManager::knockback 水平总位移 ≈ kKnockbackHoriz×strength/kKnockbackDrag = 1.125 格×strength
    //   → 无附魔 ~1.1 格 / I ~4.5 格 / II ~7.9 格（MC 1.0 量级：I 明显推离 / II 飞出 ~8 格）。旧版
    //   1+0.5*级 令 II 仅 ~2.3 格（基线 1.1 格 +50%/级）—— 与游荡抖动同量级 → 用户实测「附了没感觉」。
    static float knockbackStrength(int level);

    // ── t960 弓 / 钓竿专属附魔效果公式（单一权威；数值自定 + 注释「用户口径：弓四件+竿两件专属」）──
    //   五个纯函数 = 效果接线点（playercontroller / EntityManager）与探针数值腿共读的唯一权威，
    //   改数值只改这里（同 weaponAttackDamage / knockbackStrength 的单一权威纪律）。
    // bowArrowDamage(baseDamage, mightLevel)：劲射箭伤（HP）。base + 1×级（I +1 / II +2 / III +3；
    //   满弓 6 → III 9）。负级防御钳 0。
    static int bowArrowDamage(int baseDamage, int mightLevel);
    // bowKnockbackMultiplier(level)：震击箭击退倍率（乘在箭基线击退强度上）。1 + 1.0×级
    //   （I ×2 / II ×3；对基线 ~1.1 格位移 → I ~2.2 格 / II ~3.3 格，明显但不夸张）。负级钳 0 → 1.0。
    static float bowKnockbackMultiplier(int level);
    // bowIgniteSeconds(level)：燃箭点燃时长（秒）。级 ≥1 → 5.0s（机制等价 MC flame 一击 5s 量级；
    //   max 1 故无需分级）；级 0 → 0（无点燃）。命中点 EntityManager 箭命中 ignite（damageEntity 之后——
    //   t919 燃焰同序：致死击 ignite 内 dead 守卫早退，尸体不燃）。
    static float bowIgniteSeconds(int level);
    // rodWaitScale(level)：唤潮等待期倍率（乘在确定性等待掷骰上）。1 − 0.2×级（I ×0.8 / II ×0.6 /
    //   III ×0.4 → 等待 [5,30]s → III [2,12]s）；级钳 [0,3]。两处掷骰点（落水首掷 / 鱼跑重掷）同乘。
    static float rodWaitScale(int level);
    // rodBiteWindowExtra(level)：缠咬判定窗附加量（秒，加在 kBobberBiteWindowSec=1.0 基值上——基值
    //   用户口径钉死不动，附加式加宽）。0.5×级（I +0.5s / III +1.5s → 窗 [1.5, 2.5]s）；级钳 [0,3]。
    static float rodBiteWindowExtra(int level);

private:
    EnchantRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // ENCHANTREGISTRY_H
