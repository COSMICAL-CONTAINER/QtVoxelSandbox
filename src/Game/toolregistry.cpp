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
    /* PickaxeWood  */ {int(BlockRegistry::Pickaxe), 1, 2.0f, "pickaxe_wood",  "木镐"},
    /* PickaxeStone */ {int(BlockRegistry::Pickaxe), 2, 4.0f, "pickaxe_stone", "石镐"},
    /* PickaxeIron  */ {int(BlockRegistry::Pickaxe), 3, 6.0f, "pickaxe_iron",  "铁镐"},
};

// 编译期表大小守卫：ToolCount 变更后未同步本表 → 编译失败（防漏行 / 错位）。
static_assert(int(ToolRegistry::ToolCount) == 3, "kTools 表大小须与 ToolRegistry::ToolCount 一致；新工具需补行");
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
    // 可挖 = 实体方块 AND hardness > 0。air / 越界 → def() 返 air（solid=false / hardness=0）→ false。
    // 本工程无基岩；若将来加不可破坏实体方块，把 hardness 设 <=0 即被本判定排除（无需特殊分支）。
    return BlockRegistry::isSolid(blockId) && BlockRegistry::hardness(blockId) > 0.0f;
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
    if (hardness <= 0.0f) return 1.0f; // air / 越界：返回非零（防 t34 进度异常；实际不可挖掘）
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
