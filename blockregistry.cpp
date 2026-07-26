#include "blockregistry.h"

// 单一数据表：每方块一行 —— 顶/底/侧瓦片序号、是否实体、内部名。
// 行索引 == 方块 id（BlockRegistry::Id）。改方块外观/属性只改这里，全工程生效。
// 数组大小用 Count 钉死，初始值个数与 Count 不符 → 编译失败（防漏行/错位）。
namespace {
struct BlockDef {
    int topTile;    // +Y(Top)
    int bottomTile; // -Y(Bottom)
    int sideTile;   // ±X / ±Z（四个侧面统一）
    bool solid;
    const char *name; // 内部/调试用（通用词）
};

constexpr BlockDef kDefs[int(BlockRegistry::Count)] = {
    /* air    */ {0, 0, 0, false, "air"},
    /* grass  */ {0, 2, 1, true,  "grass"},  // 顶=grass_top 底=dirt 侧=grass_side
    /* dirt   */ {2, 2, 2, true,  "dirt"},
    /* stone  */ {3, 3, 3, true,  "stone"},
    /* cobble */ {5, 5, 5, true,  "cobble"},
    /* log    */ {6, 6, 7, true,  "log"},    // 顶/底=log_top 侧=log_side
    /* planks */ {8, 8, 8, true,  "planks"},
    /* leaves */ {9, 9, 9, true,  "leaves"},
    /* sand   */ {4, 4, 4, true,  "sand"},
};
} // namespace

int BlockRegistry::tileIndex(quint8 blockId, Face face)
{
    if (blockId >= Count) return 0; // 越界 → 兜底（与旧 tileFor 同语义）
    const BlockDef &d = kDefs[blockId];
    switch (face) {
    case Top:    return d.topTile;
    case Bottom: return d.bottomTile;
    default:     return d.sideTile; // ±X / ±Z
    }
}

bool BlockRegistry::isSolid(quint8 blockId)
{
    if (blockId >= Count) return false;
    return kDefs[blockId].solid;
}

const char *BlockRegistry::blockName(quint8 blockId)
{
    if (blockId >= Count) return "unknown";
    return kDefs[blockId].name;
}
