#include "toolregistry.h"

#include <algorithm>

// 单一数据表：每工具一行（工具段）+ 每方块一行（挖掘属性）。改工具 / 硬度 / 采掘要求只改这里。
// 行索引：工具表 = itemId - ToolIdBase；方块表 = 方块 id（与 BlockRegistry::Id 对齐）。
// 表大小用 static_assert 钉死到 ToolCount / BlockRegistry::Count，漏行 / 错位 → 编译失败。
namespace {
// 工具段连续表（按 ToolId 枚举顺序；isTool 判定后按偏移索引）。
constexpr ToolRegistry::ToolDef kTools[int(ToolRegistry::ToolCount)] = {
    /* PickaxeWood  */ {int(ToolRegistry::Pickaxe), 1, 2.0f, "pickaxe_wood",  "木镐"},
    /* PickaxeStone */ {int(ToolRegistry::Pickaxe), 2, 4.0f, "pickaxe_stone", "石镐"},
    /* PickaxeIron  */ {int(ToolRegistry::Pickaxe), 3, 6.0f, "pickaxe_iron",  "铁镐"},
};

// 每方块的挖掘属性（air 也占一行；baseHardness=0 → miningTime 兜底，不会被实际挖掘）。
// MC 1.0 hardness 参考（取近似值，对齐机制而非精确数值）：
//   grass / dirt / sand 0.5~0.6（铲首选；本工程无铲 → 空手可采、掉落）；
//   stone 1.5 / cobble 2.0（需木镐+ 才掉落，否则破后仅 AIR）；
//   log / planks 2.0（斧首选；本工程无斧 → 空手可采、掉落）；leaves 0.2（剪首选；空手可采、掉落）。
// 本工程仅有镐 → 石类（stone / cobble）harvestTool=Pickaxe；其余 NoTool（空手采、掉落）。
constexpr ToolRegistry::BlockMineDef kBlockMine[int(BlockRegistry::Count)] = {
    /* air    */ {0.0f, int(ToolRegistry::NoTool),  0}, // 不可挖掘（air 非实体；兜底）
    /* grass  */ {0.6f, int(ToolRegistry::NoTool),  0}, // 空手可采、掉落
    /* dirt   */ {0.5f, int(ToolRegistry::NoTool),  0},
    /* stone  */ {1.5f, int(ToolRegistry::Pickaxe), 1}, // 需木镐+ 采掘才掉落（圆石）
    /* cobble */ {2.0f, int(ToolRegistry::Pickaxe), 1},
    /* log    */ {2.0f, int(ToolRegistry::NoTool),  0}, // 空手可采、掉落（斧首选，无斧→空手）
    /* planks */ {2.0f, int(ToolRegistry::NoTool),  0},
    /* leaves */ {0.2f, int(ToolRegistry::NoTool),  0},
    /* sand   */ {0.5f, int(ToolRegistry::NoTool),  0},
};

// 越界 / 未知方块兜底（不可挖掘、无掉落）。blockMine 返回它的 const 引用（避免悬空）。
constexpr ToolRegistry::BlockMineDef kUnknownMine{0.0f, int(ToolRegistry::NoTool), 0};

// 编译期表大小守卫：BlockRegistry::Count 或 ToolCount 变更后未同步本表 → 编译失败（防漏行 / 错位）。
static_assert(int(BlockRegistry::Count) == 9, "kBlockMine 表大小须与 BlockRegistry::Count 一致；新方块需补挖掘属性行");
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

const ToolRegistry::BlockMineDef &ToolRegistry::blockMine(quint8 blockId)
{
    if (int(blockId) >= int(BlockRegistry::Count)) return kUnknownMine;
    return kBlockMine[int(blockId)];
}

float ToolRegistry::miningSpeedMul(quint8 blockId, int itemId)
{
    const BlockMineDef &bm = blockMine(blockId);
    // 不需工具的方块：任何手持物均无加成（本工程无铲 / 斧 → 持镐挖土同空手，MC 一致）。
    if (bm.harvestTool == NoTool) return 1.0f;
    // 需工具：查手持物是否匹配类型且等级达标（spec：匹配 type AND tier>=minToolTier → 按 tier 倍率）。
    const ToolDef *t = tool(itemId);
    if (!t) return 1.0f;                              // 空手 / 非工具 → 无加成（慢）
    if (t->type != bm.harvestTool) return 1.0f;       // 工具类型不匹配 → 无加成（慢）
    if (t->tier < bm.minToolTier) return 1.0f;        // 等级不够 → 无加成（慢，spec「不匹配 / 等级不够 → 慢」）
    return t->speedMul;                               // 匹配且达标 → tier 倍率
}

float ToolRegistry::miningTime(quint8 blockId, int itemId)
{
    const BlockMineDef &bm = blockMine(blockId);
    if (bm.baseHardness <= 0.0f) return 1.0f; // air / 越界：返回非零（防 t34 进度异常；实际不可挖掘）
    const float mul = miningSpeedMul(blockId, itemId);
    // 挖掘耗时 = baseHardness / speedMul（spec）。mul >= 1.0 → 耗时 <= baseHardness。
    const float t = bm.baseHardness / std::max(mul, 0.0001f);
    return std::max(t, 0.05f); // 地板 0.05s 防秒破致 t34 进度抖动
}

bool ToolRegistry::canHarvest(quint8 blockId, int itemId)
{
    const BlockMineDef &bm = blockMine(blockId);
    if (bm.harvestTool == NoTool) return true; // 空手可采 → 恒掉落
    const ToolDef *t = tool(itemId);
    if (!t) return false;                      // 需工具但空手 → 不掉落（spec：仅 AIR）
    return t->type == bm.harvestTool && t->tier >= bm.minToolTier; // 类型 + 等级双达标才掉落
}

QString ToolRegistry::displayName(int itemId)
{
    const ToolDef *t = tool(itemId);
    if (!t) return QString(); // 非工具 / 越界 → 空串（兜底）
    // 源文件 UTF-8；MinGW GCC 默认 input/exec charset = UTF-8，字面量为 UTF-8 字节，fromUtf8 正确解码
    // （与 BlockRegistry::displayName 同源；跨编译器稳健靠「UTF-8 字面量 + fromUtf8」）。
    return QString::fromUtf8(t->display);
}
