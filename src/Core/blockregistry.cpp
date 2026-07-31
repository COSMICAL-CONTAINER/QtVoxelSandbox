#include "blockregistry.h"

// 单一数据表：每方块一行 —— 外观 / 实体 / 挖掘 / 掉落 / 堆叠 / 名 全部集中（t42）。
// 行索引 == 方块 id（BlockRegistry::Id）。改方块任何属性只改这里，全工程生效（挖掘 / 掉落 /
// 背包 / mesher 全部走本表的只读查询）。数组大小用 Count 钉死，行序 == Id 枚举序，
// 初始值个数与 Count 不符 → 编译失败（防漏行 / 错位）。
//
// 字段填值依据（机制等价 MC 1.0，对齐机制而非精确数值；PLAN §9 通用名）：
//   - tile 序号：与 tools/build_atlas.py 打包顺序严格一致（一个偏差即渗色 / 错贴）。
//     grass 顶=grass_top(0) 底=dirt(2) 侧=grass_side(1)；log 顶/底=log_top(6) 侧=log_side(7)。
//     crafting_table 顶=crafting_table_top(10) 底=planks(8) 侧=crafting_table_side(11)（t50）。
//     furnace 顶=furnace_top(12) 底=furnace_top(12) 侧=furnace_side(13) 前(-Z)=furnace_front(14)（t80）。
//     frontTile 仅「有朝向」方块用（熔炉炉口）；其余方块 frontTile == sideTile，-Z 面与其它侧面无差异。
//   - hardness：grass/dirt/sand≈0.5~0.6；stone 1.5 / cobble 2.0（需镐）；log/planks 2.0；leaves 0.2；
//     crafting_table 2.5（木制，同 MC 工作台量级）；furnace 1.5（同石头，需镐；spec t80「同石头」）；
//     bedrock=-1.0（负值 → ToolRegistry::canMine 自动 false：t141 后生存不可破——updateMining 守 finishMiningAt；创造可瞬破，t141 移除 beginMining 守卫）。
//   - toolType：本工程仅有镐 → 石类（stone/cobble/furnace/coal_ore/iron_ore）需 Pickaxe；其余 NoTool（空手采且掉落）。
//   - minToolTier：coal_ore=1（木镐可挖、掉煤材料）；iron_ore=2（需石镐，木镐挖了不掉落，机制等价 MC 铁矿）。
//   - dropId：方块自掉（id==自身），唯独 stone→cobble（MC：原石采下变圆石）；矿石→材料段 id（>=0x200，
//     由 RecipeRegistry::MaterialIdBase 契约定址；Core 层不依赖 Game，故此处用字面量 0x201=Coal /
//     0x202=IronOreDrop，t85 在 recipe.h 给这两个 id 命名 + 加图标/中文名）；air 不掉。
//   - maxStack：MC 1.0 方块标准 64；air=0（不可拾取 / 不可放置）。
namespace {
constexpr BlockRegistry::BlockDef kDefs[int(BlockRegistry::Count)] = {
    /* air            */ {int(BlockRegistry::Air),           0,  0, 0,  0,  false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0,                            0, 0,  0, "air",            ""},
    /* grass          */ {int(BlockRegistry::Grass),         0,  2, 1,  1,  true,  BlockRegistry::ShapeFull,     0.6f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::Grass),         1, 64, "grass",          "草方块"}, // 顶=grass_top 底=dirt 侧=grass_side
    /* dirt           */ {int(BlockRegistry::Dirt),          2,  2, 2,  2,  true,  BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::Dirt),          1, 64, "dirt",           "泥土"},
    /* stone          */ {int(BlockRegistry::Stone),         3,  3, 3,  3,  true,  BlockRegistry::ShapeFull,     1.5f, int(BlockRegistry::Pickaxe), 1, int(BlockRegistry::Cobble),        1, 64, "stone",          "石头"}, // 需木镐+ 采掘；掉圆石（原石→圆石）
    /* cobble         */ {int(BlockRegistry::Cobble),        5,  5, 5,  5,  true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::Pickaxe), 1, int(BlockRegistry::Cobble),        1, 64, "cobble",         "圆石"},
    /* log            */ {int(BlockRegistry::Log),           6,  6, 7,  7,  true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::Log),            1, 64, "log",            "橡木原木"}, // 顶/底=log_top 侧=log_side
    /* planks         */ {int(BlockRegistry::Planks),        8,  8, 8,  8,  true,  BlockRegistry::ShapeFull,     2.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::Planks),         1, 64, "planks",         "橡木木板"},
    /* leaves         */ {int(BlockRegistry::Leaves),        9,  9, 9,  9,  true,  BlockRegistry::ShapeFull,     0.2f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::Leaves),         1, 64, "leaves",         "橡树树叶"},
    /* sand           */ {int(BlockRegistry::Sand),          4,  4, 4,  4,  true,  BlockRegistry::ShapeFull,     0.5f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::Sand),           1, 64, "sand",           "沙子"},
    /* crafting_table */ {int(BlockRegistry::CraftingTable), 10, 8, 11, 11, true,  BlockRegistry::ShapeFull,     2.5f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::CraftingTable),  1, 64, "crafting_table", "工作台"}, // 顶=crafting_table_top(10) 底=planks(8) 侧=crafting_table_side(11)（t50）
    /* furnace        */ {int(BlockRegistry::Furnace),       12, 12, 13, 14, true,  BlockRegistry::ShapeFull,     1.5f, int(BlockRegistry::Pickaxe), 1, int(BlockRegistry::Furnace),        1, 64, "furnace",        "熔炉"}, // 顶=furnace_top(12) 底=furnace_top(12) 侧=furnace_side(13) 前(-Z)=furnace_front(14)（t80）；8 圆石围圈合成；同石头（需镐）
    /* coal_ore       */ {int(BlockRegistry::CoalOre),       15, 15, 15, 15, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 1, 0x201,                              1, 64, "coal_ore",       "煤矿石"}, // 各面=coal_ore(15)（t84）；散布于 stone 区段；木镐可挖；掉煤材料(0x201，t85 命名)
    /* iron_ore       */ {int(BlockRegistry::IronOre),       16, 16, 16, 16, true,  BlockRegistry::ShapeFull,     3.0f, int(BlockRegistry::Pickaxe), 2, 0x202,                              1, 64, "iron_ore",       "铁矿石"}, // 各面=iron_ore(16)（t84）；散布于 stone 区段；**需石镐**（minTier2，木镐挖不掉落）；掉铁原矿材料(0x202，t85 命名)
    /* torch          */ {int(BlockRegistry::Torch),         17, 17, 17, 17, false, BlockRegistry::ShapeNone,     0.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::Torch),         1, 64, "torch",          "火把"}, // 各面=torch(17)（t88）；solid=false 非实体碰撞、hardness=0 瞬破、NoTool；掉自身；伪光源（Main.qml 发光 Model，非 PointLight）
    /* bedrock        */ {int(BlockRegistry::Bedrock),       18, 18, 18, 18, true,  BlockRegistry::ShapeFull,    -1.0f, int(BlockRegistry::Pickaxe), 0,                            0, 0, 64, "bedrock",        "基岩"}, // 各面=bedrock(18)（t119）；solid=true 实体碰撞、**hardness=-1.0**（负值 → ToolRegistry::canMine 自动 false，任何模式/工具不可破，防创造秒破底层）、dropId=0 不掉落；worldgen y 0..4 坑洼层
    // ── t134 不完整方块（异形段 id >= FirstPartial=15）：6 类木制半方块。各面贴图=planks(8)、
    //   solid=false（非整立方 → 不挡邻居面剔除，避免相邻整立方被误剔出洞；**碰撞走 shape 子 AABB**，t146）、
    //   hardness=2.0（木质）、NoTool（空手可采且掉落）、dropId=自身、dropCount=1。mesher 经
    //   PartialBlockGeometry::append 按 (id,state) 生成异形顶点。maxStack：door=1（单件不可堆叠），其余 64。
    /* wood_slab      */ {int(BlockRegistry::WoodSlab),          8,  8, 8,  8, false, BlockRegistry::ShapeSlab,     2.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::WoodSlab),          1, 64, "wood_slab",          "木板台阶"}, // state bit0=上半(1)/下半(0)；半高 0.5
    /* wood_stairs    */ {int(BlockRegistry::WoodStairs),        8,  8, 8,  8, false, BlockRegistry::ShapeStairs,   2.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::WoodStairs),        1, 64, "wood_stairs",        "木板楼梯"}, // state[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z；bit2=上下倒置
    /* wood_fence     */ {int(BlockRegistry::WoodFence),         8,  8, 8,  8, false, BlockRegistry::ShapeFence,    2.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::WoodFence),         1, 64, "wood_fence",         "木栅栏"}, // 中心立柱 0.4 见方；state=0
    /* wood_pressure_plate */ {int(BlockRegistry::WoodPressurePlate), 8, 8, 8, 8, false, BlockRegistry::ShapePlate, 2.0f, int(BlockRegistry::NoTool), 0, int(BlockRegistry::WoodPressurePlate), 1, 64, "wood_pressure_plate", "木板压力板"}, // 贴地薄板；state=0
    /* wood_door      */ {int(BlockRegistry::WoodDoor),          8,  8, 8,  8, false, BlockRegistry::ShapeDoor,     2.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::WoodDoor),          1,  1, "wood_door",          "木板门"}, // 两格高；maxStack=1；state bit3=上格 bit2=开 bit[1:0]=朝向
    /* wood_trapdoor  */ {int(BlockRegistry::WoodTrapdoor),      8,  8, 8,  8, false, BlockRegistry::ShapeTrapdoor, 2.0f, int(BlockRegistry::NoTool),  0, int(BlockRegistry::WoodTrapdoor),      1, 64, "wood_trapdoor",      "木活板门"}, // state bit0=开/合 bit[2:1]=开时朝向
};

// 编译期表大小守卫：Count 变更后未同步本表 → 编译失败（防漏行 / 错位）。
static_assert(sizeof(kDefs) / sizeof(kDefs[0]) == int(BlockRegistry::Count),
              "kDefs 行数须与 BlockRegistry::Count 一致；新方块需补 BlockDef 行");
} // namespace

const BlockRegistry::BlockDef &BlockRegistry::def(quint8 blockId)
{
    if (int(blockId) >= int(Count)) return kDefs[int(Air)]; // 越界 → air 兜底（不可挖掘、不掉落、不实体）
    return kDefs[int(blockId)];
}

int BlockRegistry::tileIndex(quint8 blockId, Face face)
{
    const BlockDef &d = def(blockId); // 越界 → air（topTile=0，与旧 tileFor 兜底同语义）
    switch (face) {
    case Top:    return d.topTile;
    case Bottom: return d.bottomTile;
    case NegZ:   return d.frontTile; // -Z 面 = 「前面」（熔炉炉口朝 -Z；其余方块 == sideTile）
    default:     return d.sideTile;  // +X / -X / +Z
    }
}

bool BlockRegistry::isSolid(quint8 blockId)      { return def(blockId).solid; }
BlockRegistry::Shape BlockRegistry::shape(quint8 blockId) { return def(blockId).shape; }
float BlockRegistry::hardness(quint8 blockId)    { return def(blockId).hardness; }
int   BlockRegistry::toolType(quint8 blockId)    { return def(blockId).toolType; }
int   BlockRegistry::minToolTier(quint8 blockId) { return def(blockId).minToolTier; }
int   BlockRegistry::dropId(quint8 blockId)      { return def(blockId).dropId; }
int   BlockRegistry::dropCount(quint8 blockId)   { return def(blockId).dropCount; }
int   BlockRegistry::maxStack(quint8 blockId)    { return def(blockId).maxStack; }

const char *BlockRegistry::blockName(quint8 blockId)
{
    if (int(blockId) >= int(Count)) return "unknown"; // 与旧契约一致：越界 → "unknown"（非 air.name）
    return kDefs[int(blockId)].name;
}

QString BlockRegistry::displayName(quint8 blockId)
{
    if (int(blockId) >= int(Count)) return QString(); // 越界 → 空串（兜底）
    // 源文件 UTF-8 编码；MinGW GCC 默认 input/exec charset = UTF-8，故 const char* 字面量为
    // UTF-8 字节，fromUtf8 正确解码（与项目既有中文注释同源；跨编译器稳健靠「UTF-8 字面量 + fromUtf8」）。
    return QString::fromUtf8(kDefs[int(blockId)].display);
}

// t146 方块形状 → cell-local [0,1]^3 子 AABB（**state 解码镜像 partialblockgeometry.cpp**，使碰撞/选中
//   形状与渲染形状同源：改一处 state 编码须同步另一处）。异形方块拆 1~2 个轴对齐子盒，与 mesher 的
//   pushBox 几何一一对应（slab 半高 / stairs 下步+背墙 / fence 中心柱 / plate 薄板 / door 薄板 / trapdoor
//   合=水平 / 开=竖直）。air/torch → 空；ShapeFull → 单盒 {0,0,0,1,1,1}。
namespace {
// 朝向 state[1:0] → 朝该向「开」（板在对侧半）。复用于 door / stairs 的薄板半 footprint 推导。
//   与 partialblockgeometry.cpp 同编码：0=+X 1=-X 2=+Z 3=-Z。
struct Half { float a0, a1; }; // 沿某轴的半 footprint 区间（0..0.5 或 0.5..1）
Half facingWall(int facing) {
    switch (facing & 3) {
    case 0: return {0.0f, 0.5f};  // 朝 +X 开 → 板在 -X 半
    case 1: return {0.5f, 1.0f};  // 朝 -X 开 → 板在 +X 半
    default: return {0.0f, 0.0f}; // +Z/-Z 向（X 轴全 footprint）
    }
}
Half facingWallZ(int facing) {
    switch (facing & 3) {
    case 2: return {0.0f, 0.5f};  // 朝 +Z 开 → 板在 -Z 半
    case 3: return {0.5f, 1.0f};  // 朝 -Z 开 → 板在 +Z 半
    default: return {0.0f, 0.0f}; // +X/-X 向（Z 轴全 footprint）
    }
}
std::vector<BlockRegistry::BlockAABB> shapeBoxes(BlockRegistry::Shape sh, quint8 state)
{
    std::vector<BlockRegistry::BlockAABB> out;
    switch (sh) {
    case BlockRegistry::ShapeFull:
        out.push_back({0, 0, 0, 1, 1, 1});
        return out;
    case BlockRegistry::ShapeNone:
        return out; // air / torch：无碰撞 sub-AABB
    case BlockRegistry::ShapeSlab: {
        const bool upper = (state & 1) != 0;
        out.push_back(upper ? BlockRegistry::BlockAABB{0, 0.5f, 0, 1, 1, 1}
                            : BlockRegistry::BlockAABB{0, 0, 0, 1, 0.5f, 1});
        return out;
    }
    case BlockRegistry::ShapeStairs: {
        // 整步（全 footprint 半高）+ 背墙（朝向对侧半 footprint 的另半高）。
        //   state[1:0]=朝向；t147 bit2=倒置 → 整步/背墙 y 区间垂直镜像（与 partialblockgeometry.cpp 同编码）。
        const bool inverted = (state & 4) != 0;
        const float stepY0 = inverted ? 0.5f : 0.0f, stepY1 = inverted ? 1.0f : 0.5f;
        const float wallY0 = inverted ? 0.0f : 0.5f, wallY1 = inverted ? 0.5f : 1.0f;
        out.push_back({0, stepY0, 0, 1, stepY1, 1});
        const Half hx = facingWall(int(state));
        const Half hz = facingWallZ(int(state));
        out.push_back({hx.a0, wallY0, hz.a0, hx.a1, wallY1, hz.a1});
        return out;
    }
    case BlockRegistry::ShapeFence:
        out.push_back({0.3f, 0, 0.3f, 0.7f, 1, 0.7f}); // 中心立柱 0.4 见方（与 mesher 同）
        return out;
    case BlockRegistry::ShapePlate:
        out.push_back({0.0625f, 0, 0.0625f, 0.9375f, 0.0625f, 0.9375f}); // 贴地薄板 1/16 厚
        return out;
    case BlockRegistry::ShapeDoor: {
        // 合：薄板贴朝向边（厚 3/16）；开：板旋 90° 贴邻边。满高（y 0..1）。
        const int facing = state & 3;
        const bool open = (state & 4) != 0;
        const float t0 = 0.8125f, t1 = 1.0f, s0 = 0.0f, s1 = 0.1875f; // 厚 3/16
        float bx0 = 0, bx1 = 1, bz0 = 0, bz1 = 1;
        if (!open) {
            switch (facing) {
            case 0: bx0 = t0; bx1 = t1; break; // 朝 +X → 板贴 +X 边
            case 1: bx0 = s0; bx1 = s1; break; // 朝 -X → 板贴 -X 边
            case 2: bz0 = t0; bz1 = t1; break; // 朝 +Z → 板贴 +Z 边
            case 3: bz0 = s0; bz1 = s1; break; // 朝 -Z → 板贴 -Z 边
            }
        } else {
            switch (facing) {
            case 0: bz0 = t0; bz1 = t1; break; // 原 +X → 旋到 +Z 边
            case 1: bz0 = s0; bz1 = s1; break; // 原 -X → 旋到 -Z 边
            case 2: bx0 = t0; bx1 = t1; break; // 原 +Z → 旋到 +X 边
            case 3: bx0 = s0; bx1 = s1; break; // 原 -Z → 旋到 -X 边
            }
        }
        out.push_back({bx0, 0, bz0, bx1, 1, bz1});
        return out;
    }
    case BlockRegistry::ShapeTrapdoor: {
        const bool open = (state & 1) != 0;
        if (!open) {
            out.push_back({0, 0, 0, 1, 0.1875f, 1}); // 合：水平薄板贴地
        } else {
            const int facing = (state >> 1) & 3;
            const float t0 = 0.8125f, t1 = 1.0f, s0 = 0.0f, s1 = 0.1875f;
            float bx0 = 0, bx1 = 1, bz0 = 0, bz1 = 1;
            switch (facing) {
            case 0: bx0 = t0; bx1 = t1; break; // +X 边
            case 1: bx0 = s0; bx1 = s1; break; // -X 边
            case 2: bz0 = t0; bz1 = t1; break; // +Z 边
            case 3: bz0 = s0; bz1 = s1; break; // -Z 边
            }
            out.push_back({bx0, 0, bz0, bx1, 1, bz1}); // 开：竖直薄板贴边
        }
        return out;
    }
    }
    return out; // 未知 shape → 空（兜底）
}
} // namespace

// 当前 collision 与 selection 同数据（异形方块 VoxelShape 与 outline shape 多数一致；机制等价 MC）。
//   分离接口备将来分歧（如某些方块选中框略放宽 / 碰撞略收紧）。两者都走 shapeBoxes —— 改形状只改一处。
std::vector<BlockRegistry::BlockAABB> BlockRegistry::collisionAABBs(quint8 blockId, quint8 state)
{
    return shapeBoxes(def(blockId).shape, state);
}
std::vector<BlockRegistry::BlockAABB> BlockRegistry::selectionAABBs(quint8 blockId, quint8 state)
{
    return shapeBoxes(def(blockId).shape, state);
}

// 音效材质分组（t118）：id → MaterialGroup 纯函数（按 BlockRegistry::Id 枚举值分支，单一权威）。// AudioManager 据此选 break / mining / step 音色；越界 / air / torch / 未知 → GroupDefault
// （AudioManager 内部用 GroupStone 兜底播放，避免缺组静默）。
// 机制等价 MC「方块 → SoundType」（机制对齐，非名词照搬）。新方块追加时按材质归入对应组或补新组。
BlockRegistry::MaterialGroup BlockRegistry::materialGroup(quint8 blockId)
{
    switch (blockId) {
    case Stone: case Cobble: case Furnace: case CoalOre: case IronOre:
        return GroupStone;
    case Log: case Planks: case CraftingTable:
    case WoodSlab: case WoodStairs: case WoodFence:
    case WoodPressurePlate: case WoodDoor: case WoodTrapdoor: // t134 木制半方块 → 木质音色
        return GroupWood;
    case Grass: case Dirt:
        return GroupGrass;
    case Sand:
        return GroupSand;
    case Leaves:
        return GroupLeaves;
    default:
        return GroupDefault; // air / torch / 越界 / 未知 → 兜底（AudioManager 复用 Stone 音色）
    }
}
