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
    // maxDurability 取 MC 1.0 经典值：木 59 / 石 131 / 铁 250（同 tier 镐 / 锄共享；spec t263「木头耐久度最低以此类推」）。
    /* PickaxeWood  */ {int(BlockRegistry::Pickaxe), 1, 2.0f,   59, "pickaxe_wood",  "木镐"},
    /* PickaxeStone */ {int(BlockRegistry::Pickaxe), 2, 4.0f,  131, "pickaxe_stone", "石镐"},
    /* PickaxeIron  */ {int(BlockRegistry::Pickaxe), 3, 6.0f,  250, "pickaxe_iron",  "铁镐"},
    /* HoeWood      */ {int(BlockRegistry::Hoe),     1, 1.0f,   59, "hoe_wood",      "木锄"},
    /* HoeStone     */ {int(BlockRegistry::Hoe),     2, 1.0f,  131, "hoe_stone",     "石锄"},
    /* HoeIron      */ {int(BlockRegistry::Hoe),     3, 1.0f,  250, "hoe_iron",      "铁锄"},
};

// 编译期表大小守卫：ToolCount 变更后未同步本表 → 编译失败（防漏行 / 错位）。
static_assert(int(ToolRegistry::ToolCount) == 6, "kTools 表大小须与 ToolRegistry::ToolCount 一致；新工具需补行");
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
    // 不需工具的方块：任何手持物均无加成（本工程无铲 / 斧 → 持镐挖土同空手，MC 一致）。
    if (harvestTool == BlockRegistry::NoTool) return 1.0f;
    // 需工具：查手持物是否匹配类型且等级达标（spec：匹配 type AND tier>=minToolTier → 按 tier 倍率）。
    const ToolDef *t = tool(itemId);
    if (!t) return 1.0f;                                          // 空手 / 非工具 → 无加成（慢）
    if (t->type != harvestTool) return 1.0f;                      // 工具类型不匹配 → 无加成（慢）
    if (t->tier < BlockRegistry::minToolTier(blockId)) return 1.0f; // 等级不够 → 无加成（慢，spec「不匹配 / 等级不够 → 慢」）
    return t->speedMul;                                           // 匹配且达标 → tier 倍率
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
    const int harvestTool = BlockRegistry::toolType(blockId);
    if (harvestTool == BlockRegistry::NoTool) return true; // 空手可采 → 恒掉落
    const ToolDef *t = tool(itemId);
    if (!t) return false;                      // 需工具但空手 → 不掉落（spec：仅 AIR）
    return t->type == harvestTool && t->tier >= BlockRegistry::minToolTier(blockId); // 类型 + 等级双达标才掉落
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
