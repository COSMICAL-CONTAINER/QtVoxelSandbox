#include "toolregistry.h"

#include <algorithm>

// 单一工具数据表：每工具一行（工具段）。改工具属性（speedMul / tier / 名）只改这里。
// 行索引 = itemId - ToolIdBase（isTool 判定后按偏移索引）。
// 表大小用 static_assert 钉死到 ToolCount，漏行 / 错位 → 编译失败。
//
// 注：方块的挖掘属性（hardness / toolType / minToolTier / dropId / maxStack）已统一收敛到
// BlockRegistry::BlockDef（t42）；本类只读查它（BlockRegistry::def / hardness / toolType / ...），
// 不再持 kBlockMine 副本（PLAN §2：世界数据单一）。
namespace {
// 工具段连续表（按 ToolId 枚举顺序；isTool 判定后按偏移索引）。
// type 字段为 BlockRegistry::ToolType（枚举归 Core；与 BlockDef.toolType 同源）。
constexpr ToolRegistry::ToolDef kTools[int(ToolRegistry::ToolCount)] = {
    // maxDurability 取 MC 1.0 经典值：木 59 / 石 131 / 铁 250（同 tier 镐 / 锄 / 斧 / 铲 / 剑共享；spec t263「木头耐久度最低以此类推」）。
    // speedMul：镐 / 斧 / 铲取 MC 1.0 同 tier 倍率（木 2 / 石 4 / 铁 6）—— 匹配方块 toolType 后激活加速。
    //   t265：铁档（6）有意低于未来金（12）/ 钻石（8）档，留头部空间（spec「铁镐削弱，留金/钻石档空间」）；
    //   锄 / 剑恒 1.0（锄专用耕地不挖、剑是武器不挖，二者无方块的 toolType 取它们 → miningSpeedMul 恒返 1.0 等同空手）。
    // harvestLevel（rv56 问题6 新列，tier 后第 2 列）：采掘门槛等级，canHarvest / miningSpeedMul 与 BlockDef.minToolTier
    //   比较用此值。木 / 石 / 铁 / 钻石 = tier；金 = 1（MC 1.0 gold mining level = wood）；铜 = 2（自定同石级）。
    /* PickaxeWood  */ {int(BlockRegistry::Pickaxe), 1, 1, 2.0f,   59, "pickaxe_wood",  "木镐"},
    /* PickaxeStone */ {int(BlockRegistry::Pickaxe), 2, 2, 4.0f,  131, "pickaxe_stone", "石镐"},
    /* PickaxeIron  */ {int(BlockRegistry::Pickaxe), 3, 3, 6.0f,  250, "pickaxe_iron",  "铁镐"},
    /* HoeWood      */ {int(BlockRegistry::Hoe),     1, 1, 1.0f,   59, "hoe_wood",      "木锄"},
    /* HoeStone     */ {int(BlockRegistry::Hoe),     2, 2, 1.0f,  131, "hoe_stone",     "石锄"},
    /* HoeIron      */ {int(BlockRegistry::Hoe),     3, 3, 1.0f,  250, "hoe_iron",      "铁锄"},
    // t264 完整工具集：斧 / 铲 / 剑 × 木 / 石 / 铁。机制等价 MC 1.0 工具集（§9 通用工具名，零 MC 专名）。
    /* AxeWood      */ {int(BlockRegistry::Axe),     1, 1, 2.0f,   59, "axe_wood",      "木斧"},
    /* AxeStone     */ {int(BlockRegistry::Axe),     2, 2, 4.0f,  131, "axe_stone",     "石斧"},
    /* AxeIron      */ {int(BlockRegistry::Axe),     3, 3, 6.0f,  250, "axe_iron",      "铁斧"},
    /* ShovelWood   */ {int(BlockRegistry::Shovel),  1, 1, 2.0f,   59, "shovel_wood",   "木铲"},
    /* ShovelStone  */ {int(BlockRegistry::Shovel),  2, 2, 4.0f,  131, "shovel_stone",  "石铲"},
    /* ShovelIron   */ {int(BlockRegistry::Shovel),  3, 3, 6.0f,  250, "shovel_iron",   "铁铲"},
    /* SwordWood    */ {int(BlockRegistry::Sword),   1, 1, 1.0f,   59, "sword_wood",    "木剑"},
    /* SwordStone   */ {int(BlockRegistry::Sword),   2, 2, 1.0f,  131, "sword_stone",   "石剑"},
    /* SwordIron    */ {int(BlockRegistry::Sword),   3, 3, 1.0f,  250, "sword_iron",    "铁剑"},
    // t304 弓：type=Bow（不参与挖掘 → miningSpeedMul 恒 1.0 等同空手；弓的伤害走拉弓蓄力 + 箭，非 attackDamage）。
    //   maxDurability=384（机制等价 MC 1.0 弓耐久；每次射箭 -1）。tier/speedMul 仅记账占位（语义同剑：无对应
    //   采掘方块）。 displayName「弓」（§9 通用词；非 MC 专名）。
    /* Bow          */ {int(BlockRegistry::Bow),     1, 1, 1.0f,  384, "bow",           "弓"},
    // t300 剪刀（type=Shears=6）：speedMul=2.0 在 Wool.toolType=Shears 时激活（羊毛方块挖掘加速）；
    //   其余方块持剪刀 miningSpeedMul 恒 1.0（类型不匹配，等同空手）。tier/speedMul 仅记账 —— 剪刀的真正用途是
    //   右键剪羊毛（非挖掘）。maxDurability=238（机制等价 MC 1.0 剪刀耐久；每次剪羊毛 -1）。displayName「剪刀」。
    /* Shears       */ {int(BlockRegistry::Shears),  1, 1, 2.0f,  238, "shears",        "剪刀"},
    // t401 钓鱼竿（type=FishingRod=8）：不参与挖掘 → miningSpeedMul 恒 1.0 等同空手。maxDurability=64（机制等价
    //   MC 1.0 钓竿耐久；生存每次成功钓获 -1）。tier/speedMul 仅记账（语义同弓 / 剪刀：无对应采掘方块）。
    //   displayName「钓鱼竿」（§9 通用词；非 MC 专名）。
    /* FishingRod   */ {int(BlockRegistry::FishingRod), 1, 1, 1.0f,  64, "fishing_rod",  "钓鱼竿"},
    // t472 钻石镐：tier 4（最高档）、speedMul 8.0（项目设计预留的钻石档倍率，见上「铁档 6 留金 12 / 钻石 8 空间」；
    //   机制对齐 MC 1.0 钻石镐采掘速度）。maxDurability=1561（MC 1.0 钻石镐耐久，铁 250 之上的最高耐久）。
    //   采掘 Obsidian 的唯一工具（Obsidian.minToolTier=4）。追加在末尾（与 ToolId 枚举同序；不重排保向后兼容）。
    /* PickaxeDiamond */ {int(BlockRegistry::Pickaxe), 4, 4, 8.0f, 1561, "pickaxe_diamond", "钻石镐"},
    // t557 金工具（机制等价 MC 1.0 gold tools：耐久 32 最脆、speedMul 12.0 最快 —— 「快而脆」）。**rv56 问题6 修正：
    //   harvestLevel=1（MC 1.0 gold mining level = wood）**——金镐 tier 5 虽高于钻石 4，但采掘门槛同木镐（挖不动
    //   铁矿 minToolTier=3 / 黑曜石 4，只挖得动木镐能挖的石 / 煤矿）；速度倍率仍 12.0 全工具最高（快但采掘等级低，
    //   机制对齐 MC「金工具快而脆且采掘等级木级」）。剑 speedMul 1.0（武器不挖掘）；锄 1.0（耕地）。
    /* GoldPickaxe   */ {int(BlockRegistry::Pickaxe), 5, 1, 12.0f,   32, "pickaxe_gold",   "金镐"},
    /* GoldAxe       */ {int(BlockRegistry::Axe),     5, 1, 12.0f,   32, "axe_gold",       "金斧"},
    /* GoldShovel    */ {int(BlockRegistry::Shovel),  5, 1, 12.0f,   32, "shovel_gold",    "金铲"},
    /* GoldSword     */ {int(BlockRegistry::Sword),   5, 1,  1.0f,   32, "sword_gold",     "金剑"},
    /* GoldHoe       */ {int(BlockRegistry::Hoe),     5, 1,  1.0f,   32, "hoe_gold",       "金锄"},
    // t557 铜工具（本工程已有材料 CopperIngot，MC 1.0 无铜工具 → 自定：speedMul 5.0 介石 4 / 铁 6 之间、耐久 180 介
    //   石 131 / 铁 250 之间 —— 「介于石与铁之间的金属档」；tier 6；harvestLevel=2 同石级，与「介石铁之间」定位一致）。
    //   剑 speedMul 1.0（武器）；锄 1.0（耕地）。
    /* CopperPickaxe */ {int(BlockRegistry::Pickaxe), 6, 2,  5.0f,  180, "pickaxe_copper", "铜镐"},
    /* CopperAxe     */ {int(BlockRegistry::Axe),     6, 2,  5.0f,  180, "axe_copper",     "铜斧"},
    /* CopperShovel  */ {int(BlockRegistry::Shovel),  6, 2,  5.0f,  180, "shovel_copper",  "铜铲"},
    /* CopperSword   */ {int(BlockRegistry::Sword),   6, 2,  1.0f,  180, "sword_copper",   "铜剑"},
    /* CopperHoe     */ {int(BlockRegistry::Hoe),     6, 2,  1.0f,  180, "hoe_copper",     "铜锄"},
};

// 编译期表大小守卫：ToolCount 变更后未同步本表 → 编译失败（防漏行 / 错位）。
static_assert(int(ToolRegistry::ToolCount) == 29, "kTools 表大小须与 ToolRegistry::ToolCount 一致；新工具需补行");

// t348 引擎工具 id → MC Java 1.0.0 物品数字 id 对齐表（资源包前置；单一权威，与 docs/item-ids.md 工具段
//   「MC 1.0.0」列一致）。行索引 = engineToolId - ToolIdBase（与 kTools 同序）。**不重排枚举**（保存档 / 配方
//   向后兼容）。新增工具须在此补一行（否则越界 -1）。Shears → 359（MC 剪刀 beta 1.7 加入、1.0 存在）。
constexpr int kMcToolId[int(ToolRegistry::ToolCount)] = {
    /* PickaxeWood  */ 270, /* PickaxeStone */ 274, /* PickaxeIron  */ 257, /* HoeWood */ 290,
    /* HoeStone     */ 291, /* HoeIron      */ 292, /* AxeWood      */ 271, /* AxeStone */ 275,
    /* AxeIron      */ 258, /* ShovelWood   */ 269, /* ShovelStone  */ 273, /* ShovelIron */ 256,
    /* SwordWood    */ 272, /* SwordStone   */ 276, /* SwordIron    */ 267, /* Bow */ 261,
    /* Shears       */ 359,
    /* FishingRod   */ 346, // t401 钓竿（MC 1.0 fishing rod）
    /* PickaxeDiamond */ 278, // t472 钻石镐（MC 1.0 diamond_pickaxe）
    // t557 金工具 MC 1.0 对齐（gold_sword 283 / gold_shovel 284 / gold_pickaxe 285 / gold_axe 286 / gold_hoe 294）。
    /* GoldPickaxe   */ 285, /* GoldAxe */ 286, /* GoldShovel */ 284, /* GoldSword */ 283, /* GoldHoe */ 294,
    // t557 铜工具：MC 1.0 无铜工具（铜 1.17+）→ -1（资源包回退引擎自绘 ToolIcon）。
    /* CopperPickaxe */ -1, /* CopperAxe */ -1, /* CopperShovel */ -1, /* CopperSword */ -1, /* CopperHoe */ -1,
};
static_assert(sizeof(kMcToolId) / sizeof(kMcToolId[0]) == int(ToolRegistry::ToolCount),
              "kMcToolId 行数须与 ToolRegistry::ToolCount 一致；新工具需补一行 MC 1.0 对齐值");
} // namespace

bool ToolRegistry::isTool(int itemId)
{
    return itemId >= ToolIdBase && itemId < ToolIdBase + int(ToolCount);
}

const ToolRegistry::ToolDef *ToolRegistry::tool(int itemId)
{
    if (!isTool(itemId)) return nullptr;
    const int idx = itemId - ToolIdBase; // isTool 已钳到 [0, ToolCount)
    return &kTools[idx];
}

bool ToolRegistry::canMine(quint8 blockId)
{
    // 可挖 = 实存方块（非 air / 非越界）AND hardness >= 0。
    //   - air / 越界 → def() 返 air 行（id=Air）→ 排除（d.id == Air）。
    //   - hardness == 0 → 瞬破可挖（如火把 t88；miningTime 走 0.05s 地板 ≈ 瞬）。
    //   - hardness < 0 → 不可挖（留给未来基岩类方块，无需特殊分支）。
    // 注：早先版本要求 isSolid && hardness>0，但「实心」与「可挖」是两个正交概念——火把 non-solid
    //   却应可挖（玩家右键放置、左键瞬破回收）。改为按「实存 + hardness>=0」判定，语义更准。
    const BlockRegistry::BlockDef &d = BlockRegistry::def(blockId);
    return int(d.id) != int(BlockRegistry::Air) && d.hardness >= 0.0f;
}

float ToolRegistry::miningSpeedMul(quint8 blockId, int itemId)
{
    const int harvestTool = BlockRegistry::toolType(blockId);
    // 无有效工具的方块（air/leaves/torch/...）：任何手持物均无加成（基准速 1.0）。
    if (harvestTool == BlockRegistry::NoTool) return 1.0f;
    // t565 剑挖蛛网特例（机制等价 MC 1.0 sword ×15 挖 cobweb 近乎瞬破）：Cobweb.toolType=Sword（本工程
    //   唯一取 Sword 的方块）。剑表内 speedMul=1.0（剑不参与常规挖掘）→ 持剑挖蛛网若走通用路径无加成
    //   （4.0s 太慢）。此特例在通用匹配前拦截：持任意剑 + 蛛网 → 返 15.0（miningTime = 4.0/15 ≈ 0.27s，
    //   近乎瞬破手感对齐 MC）。剪刀亦快速（MC scissors ×15）→ 同样返 15.0。非剑非剪刀 → 落回通用路径
    //   （无加成，空手 4.0s 极慢 —— canHarvest 已拦掉落）。
    if (blockId == BlockRegistry::Cobweb) {
        const ToolDef *sw = tool(itemId);
        if (sw && (sw->type == int(BlockRegistry::Sword) || sw->type == int(BlockRegistry::Shears)))
            return 15.0f;
        return 1.0f;
    }
    // 查手持物是否匹配类型（斧 / 铲 / 镐）。
    const ToolDef *t = tool(itemId);
    if (!t) return 1.0f;                       // 空手 / 非工具 → 无加成（慢）
    if (t->type != harvestTool) return 1.0f;   // 工具类型不匹配 → 无加成（慢）
    // t265：requiresTool=true 的方块（石类）额外要求采掘等级>=minToolTier 才给速度加成（等级不够 = 慢）；
    //   requiresTool=false 的方块（木 / 土 / 沙类）任意等级正确类型均给加成（空手也掉落、仅速度受工具影响）。
    //   rv56 问题6：门槛比较从 tier 改 harvestLevel（金 tier 5 但 harvestLevel 1 = 木级门槛 → 金镐对铁矿 / 黑曜石
    //   无加成恒 1.0；速度倍率本身仍走 t->speedMul = 12.0 对低阶石类全工具最快 —— 快但挖不了高阶矿，机制对齐 MC gold）。
    if (BlockRegistry::requiresTool(blockId)
        && t->harvestLevel < BlockRegistry::minToolTier(blockId)) return 1.0f;
    return t->speedMul;                        // 匹配（且达标）→ tier 倍率
}

float ToolRegistry::miningTime(quint8 blockId, int itemId)
{
    const float hardness = BlockRegistry::hardness(blockId);
    // hardness<=0（火把瞬破 / air 越界）：走 0.05s 地板。air / 越界实际不会被挖（canMine 已排除），
    // 故此分支仅火把等 hardness=0 方块命中 → ≈ 瞬破（spec t88「hardness 0 瞬破」）。
    if (hardness <= 0.0f) return 0.05f;
    const float mul = miningSpeedMul(blockId, itemId);
    // 挖掘耗时 = hardness / speedMul（spec）。mul >= 1.0 → 耗时 <= hardness。
    const float t = hardness / std::max(mul, 0.0001f);
    return std::max(t, 0.05f); // 地板 0.05s 防秒破致 t34 进度抖动
}

bool ToolRegistry::canHarvest(quint8 blockId, int itemId)
{
    // t265：掉落门槛走 requiresTool（与 toolType 速度加成解耦）。
    //   requiresTool=false（木 / 土 / 沙类）→ 空手可采且掉落（速度受工具影响，但产物不依赖工具）；
    //   requiresTool=true（石类）→ 须持匹配工具类型 AND 采掘等级（harvestLevel，rv56）>=minToolTier 才掉落。
    //   rv56 问题6：金工具 harvestLevel=1（MC gold mining level = wood）→ 金镐破黑曜石 / 铁矿不掉落（仅 AIR），
    //   与速度加成路径同源门槛（miningSpeedMul 同改 harvestLevel），「无加成慢挖 + 不掉落」两判定一致。
    if (!BlockRegistry::requiresTool(blockId)) return true; // 不需工具 → 恒掉落
    const int harvestTool = BlockRegistry::toolType(blockId);
    const ToolDef *t = tool(itemId);
    if (!t) return false;                      // 需工具但空手 → 不掉落（spec：仅 AIR）
    return t->type == harvestTool && t->harvestLevel >= BlockRegistry::minToolTier(blockId); // 类型 + 采掘等级双达标才掉落
}

// t265 持物品攻击伤害（spec「剑→加攻击伤害」，机制等价 MC 1.0 武器伤害）。
//   剑（type=Sword）：tier 倍率 —— 木 4 / 石 5 / 铁 6（MC 1.0 sword damage；每档 +1，留金 / 钻石档空间）。
//   其它（空手 / 镐 / 斧 / 铲 / 锄）：kFistDamage=1（MC 1.0 徒手伤害；剑是唯一武器，余工具不擅长攻击）。
//   暴击由 caller（attackMob）按 base*3/2 算，本方法只返基础伤害。
int ToolRegistry::attackDamage(int itemId)
{
    const ToolDef *t = tool(itemId);
    if (!t) return kFistDamage; // 空手 / 非工具 → 徒手伤害
    if (t->type == int(BlockRegistry::Sword)) {
        // MC 1.0 剑伤害：木 4 / 石 5 / 铁 6 / 金 4（同木剑脆弱）/ 钻石 7（HP；1HP=半心）。
        //   每档 +1 的递增仅木→石→铁；金剑因「快而脆」伤害同木剑（MC 1.0 gold sword = 4）。
        switch (t->tier) {
        case 1: return 4; // 木剑（2 心）
        case 2: return 5; // 石剑（2.5 心）
        case 3: return 6; // 铁剑（3 心）
        case 4: return 7; // 钻石剑（3.5 心；钻石剑本任务未加，防御 / 未来档）
        case 5: return 4; // 金剑（2 心；机制等价 MC 1.0 gold sword 同木剑伤害）
        case 6: return 5; // 铜剑（2.5 心；本工程自定 = 石剑级，介于木 / 铁之间）
        default: return 4; // 防御：未知 tier 兜底木剑
        }
    }
    return kFistDamage; // 镐 / 斧 / 铲 / 锄：徒手伤害（非武器，MC 工具攻击伤害低，本工程统一=徒手）
}

QString ToolRegistry::displayName(int itemId)
{
    const ToolDef *t = tool(itemId);
    if (!t) return QString(); // 非工具 / 越界 → 空串（兜底）
    // 源文件 UTF-8；MinGW GCC 默认 input/exec charset = UTF-8，字面量为 UTF-8 字节，fromUtf8 正确解码
    // （与 BlockRegistry::displayName 同源；跨编译器稳健靠「UTF-8 字面量 + fromUtf8」）。
    return QString::fromUtf8(t->display);
}

int ToolRegistry::maxDurability(int itemId)
{
    const ToolDef *t = tool(itemId);
    if (!t) return 0; // 非工具 / 越界 → 0（无耐久概念；Hotbar 据本值区分工具 vs 非工具）
    return t->maxDurability;
}

// t348 引擎工具 id → MC Java 1.0.0 物品数字 id（资源包前置；见 kMcToolId 表注释）。越界 / 非工具 → -1
//   （资源包回退引擎自绘 ToolIcon）。
int ToolRegistry::mcToolId(int engineToolId)
{
    if (!isTool(engineToolId)) return -1;
    return kMcToolId[engineToolId - ToolIdBase];
}
