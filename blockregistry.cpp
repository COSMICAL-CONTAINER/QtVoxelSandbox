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
    const char *name;    // 内部/调试用（通用词，英文标识符；非面向用户）
    const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词；air=空串）
};

constexpr BlockDef kDefs[int(BlockRegistry::Count)] = {
    /* air    */ {0, 0, 0, false, "air",    ""},
    /* grass  */ {0, 2, 1, true,  "grass",  "草方块"}, // 顶=grass_top 底=dirt 侧=grass_side
    /* dirt   */ {2, 2, 2, true,  "dirt",   "泥土"},
    /* stone  */ {3, 3, 3, true,  "stone",  "石头"},
    /* cobble */ {5, 5, 5, true,  "cobble", "圆石"},
    /* log    */ {6, 6, 7, true,  "log",    "橡木原木"}, // 顶/底=log_top 侧=log_side
    /* planks */ {8, 8, 8, true,  "planks", "橡木木板"},
    /* leaves */ {9, 9, 9, true,  "leaves", "橡树树叶"},
    /* sand   */ {4, 4, 4, true,  "sand",   "沙子"},
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

QString BlockRegistry::displayName(quint8 blockId)
{
    if (blockId >= Count) return QString(); // 越界 → 空串（兜底）
    // 源文件 UTF-8 编码；MinGW GCC 默认 input/exec charset = UTF-8，故 const char* 字面量为
    // UTF-8 字节，fromUtf8 正确解码（与项目既有中文注释同源；跨编译器稳健靠「UTF-8 字面量 + fromUtf8」）。
    return QString::fromUtf8(kDefs[blockId].display);
}
