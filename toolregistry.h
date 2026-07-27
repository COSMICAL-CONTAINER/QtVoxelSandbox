#ifndef TOOLREGISTRY_H
#define TOOLREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString

#include "blockregistry.h" // 方块 id 段（blockMine 按方块 id 查挖掘属性）

// 工具注册表 + 挖掘属性表（单一权威数据源；World 层）。
//
// 与 BlockRegistry 同层、同风格：纯静态数据表，无实例、无 Q_OBJECT。把「工具物品 → 挖掘属性」
// 与「方块 → 硬度 / 采掘要求 / 掉落判定」收敛到此处，供 Game/Physics 层（挖掘系统 t34）只读
// 查询。mesher 不读本表（工具不参与网格化）；Hotbar ViewModel 读 displayName / isTool / toolTier
// （图标段判定）并桥接给 QML。
//
// 物品 id 分段（与 Hotbar::ItemStack 的 id 字段一致）：
//   方块段：0 .. BlockRegistry::Count-1（air / 草 / 土 / 石 / 圆石 / 原木 / 木板 / 树叶 / 沙）。
//   工具段：id >= ToolIdBase（0x100）；当前 3 档镐（木 / 石 / 铁）。
// 工具不可堆叠（Hotbar::maxStackSize(id) 对工具段返回 1，t32 已留段）。
//
// 挖掘模型（spec t33，机制等价 MC 1.0）：
//   - 挖掘耗时 = baseHardness / speedMul（秒）。
//   - speedMul：空手 / 不匹配工具 = 1；匹配工具类型 AND tier >= minToolTier → 按 tier 倍率（2/4/6）。
//   - 掉落判定（canHarvest）：方块不需工具 → 恒掉落；需工具 → 须持匹配类型 AND tier >= minToolTier，
//     否则破后仅 AIR（t35 不发掉落实体）。spec：「不匹配 / 等级不够 → 慢且不掉落，仅 AIR」。
//
// 分层（PLAN §2）：本层属 World，只依赖 Core + BlockRegistry（同层数据），**不**依赖
// Renderer/Physics/QtQuick3D。依赖只向下。
//
// §4 法律 + §9：工具名用通用词（木镐 / 石镐 / 铁镐 ——「镐」为通用工具名，非 MC 专名）；
// 零 MC 专有名词。工具图标在 QML 呈现层自绘原创（ToolIcon.qml 的 Canvas 像素图，§9 override (a)）。
class ToolRegistry
{
public:
    // 工具物品 id（与 Hotbar::ItemStack 的工具段对齐）。工具段基址 0x100，与方块段（0..8）隔开，
    // 防 quint8 截断别名（工具 id > 255 不会与任何方块 id 混淆）。新增工具按序追加并同步 ToolCount。
    enum ToolId : int {
        ToolIdBase   = 0x100,
        PickaxeWood  = 0x100, // 木镐：tier 1，speedMul 2.0
        PickaxeStone = 0x101, // 石镐：tier 2，speedMul 4.0
        PickaxeIron  = 0x102, // 铁镐：tier 3，speedMul 6.0
        ToolCount    = 3,     // 哨兵：已定义工具数（也是合法工具 id 相对 ToolIdBase 的上界）。
    };

    // 工具类型（决定能采掘哪类方块；当前仅镐，未来可扩铲 / 斧）。与 BlockMineDef.harvestTool 对齐。
    enum ToolType : int {
        NoTool  = 0, // 空手 / 非工具
        Pickaxe = 1, // 镐：采掘石类方块（stone / cobble）
    };

    // 工具定义。表行索引 == itemId - ToolIdBase（连续）；详见 toolregistry.cpp kTools。
    struct ToolDef {
        int type;            // ToolType
        int tier;            // 等级（1=木 2=石 3=铁）；决定能否采掘高阶方块 + 速度倍率
        float speedMul;      // 匹配工具时的挖掘速度倍率（>1 → 加速）
        const char *name;    // 内部 / 调试用名（通用词，英文标识符；非面向用户）
        const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词）
    };

    // 方块挖掘属性（每实体方块一行）。air / 越界 → 兜底（baseHardness=0、不掉落）。
    struct BlockMineDef {
        float baseHardness; // 基础硬度（挖掘耗时基准；越大越慢；<=0 表示不可挖掘）
        int harvestTool;    // 采掘所需工具类型（NoTool=空手可采且掉落；Pickaxe=需镐才掉落）
        int minToolTier;    // 采掘所需最低工具等级（harvestTool=NoTool 时忽略，恒 0）
    };

    // 工具判定（id >= ToolIdBase 且在已定义范围内）。越段 / 方块段 → false。
    static bool isTool(int itemId);

    // 取工具定义（type / tier / speedMul / 名）。非工具 id → nullptr。
    static const ToolDef *tool(int itemId);

    // 取方块挖掘属性（按方块 id 查表）。air / 越界 → 兜底行（不可挖掘、不掉落）。
    static const BlockMineDef &blockMine(quint8 blockId);

    // 挖掘速度倍率（spec：挖掘速度 = baseHardness / speedMul）。
    //   空手 / 非工具 / 类型不匹配 / 等级不够 → 1.0（=「慢」基准）；
    //   匹配工具类型 AND tier >= minToolTier → tool.speedMul。
    static float miningSpeedMul(quint8 blockId, int itemId);

    // 挖掘耗时（秒）= baseHardness / miningSpeedMul，地板 0.05s（防空手秒破致 t34 进度抖动 / 除零）。
    // air / 越界（baseHardness<=0）→ 返回 1.0（不会被实际挖掘：raycast 只命中实体方块）。
    static float miningTime(quint8 blockId, int itemId);

    // 是否采掘掉落（破块后是否产出物品实体，供 t35 判定）。
    //   方块不需工具（harvestTool=NoTool）→ 恒 true（空手可采且掉落）；
    //   需工具 → 须持匹配类型 AND tier >= minToolTier，否则 false（破后仅 AIR，不掉落）。
    static bool canHarvest(quint8 blockId, int itemId);

    // 用户可见中文显示名（工具段；PLAN §9 override (b) 通用词）。
    //   PickaxeWood=木镐 PickaxeStone=石镐 PickaxeIron=铁镐。非工具 / 越界 → 空串。
    // 字面量为 UTF-8，由 fromUtf8 解码（与项目既有中文注释 / BlockRegistry::displayName 同源）。
    static QString displayName(int itemId);

private:
    ToolRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // TOOLREGISTRY_H
