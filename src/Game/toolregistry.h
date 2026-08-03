#ifndef TOOLREGISTRY_H
#define TOOLREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString

#include "blockregistry.h" // 方块 id 段 + ToolType + BlockDef（挖掘 / 掉落 / 堆叠 / canMine 走方块表）

// 工具注册表 + 挖掘 / 掉落 / 堆叠判定（单一权威数据源在 BlockRegistry::BlockDef；Game 层）。
//
// 与 BlockRegistry 同风格：纯静态数据表，无实例、无 Q_OBJECT。本表只持有「工具物品 → 工具属性」
// （type / tier / speedMul / 名）；方块的挖掘属性（hardness / toolType / minToolTier / dropId /
// maxStack）**已统一收敛到 BlockRegistry::BlockDef**（t42），本类只读查它，不再持副本（PLAN §2：
// 世界数据单一）。挖掘系统(t34)、背包 Hotbar(t32) 经本类（或直接经 BlockRegistry）只读查询。
//
// 物品 id 分段（与 Hotbar::ItemStack 的 id 字段一致）：
//   方块段：0 .. BlockRegistry::Count-1（air / 草 / 土 / 石 / 圆石 / 原木 / 木板 / 树叶 / 沙）。
//   工具段：id >= ToolIdBase（0x100）；当前 3 档镐（木 / 石 / 铁）+ 3 档锄（木 / 石 / 铁）。
// 工具不可堆叠（Hotbar::maxStackSize(id) 对工具段返回 1，t32 已留段）。
//
// 耐久模型（spec t263，机制等价 MC 1.0 工具耐久）：每工具一份 maxDurability（按 tier：木 < 石 < 铁），
//   每次有效使用（生存挖掘完成 / 锄耕地 / 未来剑攻击）-1，归零即破损（槽位清空、工具消失）。
//   创造模式不消耗（无限源）。耐久值随工具实例走（Hotbar::ItemStack.durability 字段，工具 count 恒 1 →
//   每实例独立耐久；背包内搬运经 setStack 显式传 durability 保真，见 hotbar.h）。
//   maxDurability 取 MC 1.0 经典值：木 59 / 石 131 / 铁 250（同 tier 的镐 / 锄 / 未来剑 / 斧 / 铲共享）。
//
// 挖掘模型（spec t33，机制等价 MC 1.0；硬度 / 采掘要求走 BlockRegistry::BlockDef）：
//   - 挖掘耗时 = hardness / speedMul（秒）。
//   - speedMul：空手 / 不匹配工具 = 1；匹配工具类型 AND tier >= minToolTier → 按 tier 倍率（2/4/6）。
//   - 掉落判定（canHarvest）：方块不需工具 → 恒掉落；需工具 → 须持匹配类型 AND tier >= minToolTier，
//     否则破后仅 AIR（t35 不发掉落实体）。spec：「不匹配 / 等级不够 → 慢且不掉落，仅 AIR」。
//   - 可挖判定（canMine）：实体方块且 hardness > 0（air / 越界 / 基岩=false）。
//
// 锄（type=Hoe）特殊语义：本工程**无任何方块的 toolType 取 Hoe**（耕地是非方块交互、走 useBlock，
// 非「采掘所需工具」），故持锄挖任何方块 miningSpeedMul 恒返 1.0（miningSpeedMul 第一步 `harvestTool
// == NoTool → 1.0` 或类型不匹配 → 1.0），canHarvest 对需工具方块恒 false → 机制等价 MC「锄不影响挖掘」。
// 锄的 tier 仅驱动其未来耕地交互（草→耕地耗时随 tier 缩短等，留后续任务），与挖掘解耦。
//
// 分层（PLAN §2）：本层属 Game，只依赖 Core（BlockRegistry），**不**依赖
// Renderer/Physics/QtQuick3D。依赖只向下。ToolType 枚举归 BlockRegistry（Core），本类复用。
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
        HoeWood      = 0x103, // 木锄：type=Hoe（专用耕地；不参与挖掘速度，speedMul 仅记账=1.0）
        HoeStone     = 0x104, // 石锄：type=Hoe tier 2
        HoeIron      = 0x105, // 铁锄：type=Hoe tier 3
        ToolCount    = 6,     // 哨兵：已定义工具数（也是合法工具 id 相对 ToolIdBase 的上界）。
    };

    // 工具定义。表行索引 == itemId - ToolIdBase（连续）；详见 toolregistry.cpp kTools。
    // type 字段为 BlockRegistry::ToolType（与 BlockDef.toolType 同枚举）——「工具实例的类型」须能
    // 匹配「方块要求的采掘工具类型」，故共用一个枚举（归 Core 层）。
    struct ToolDef {
        int type;            // BlockRegistry::ToolType（Pickaxe / Hoe / NoTool）
        int tier;            // 等级（1=木 2=石 3=铁）；决定能否采掘高阶方块 + 速度倍率（镐）/ 耕地等级（锄）
        float speedMul;      // 匹配工具时的挖掘速度倍率（>1 → 加速）；锄恒 1.0（不参与挖掘，仅记账）
        int maxDurability;   // t263 最大耐久（使用次数上限；木 59 / 石 131 / 铁 250）。归零即破损。
        const char *name;    // 内部 / 调试用名（通用词，英文标识符；非面向用户）
        const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词）
    };

    // 工具判定（id >= ToolIdBase 且在已定义范围内）。越段 / 方块段 → false。
    static bool isTool(int itemId);

    // 取工具定义（type / tier / speedMul / 名）。非工具 id → nullptr。
    static const ToolDef *tool(int itemId);

    // 挖掘速度倍率（spec：挖掘速度 = hardness / speedMul；硬度走 BlockRegistry::BlockDef）。
    //   空手 / 非工具 / 类型不匹配 / 等级不够 → 1.0（=「慢」基准）；
    //   匹配工具类型 AND tier >= minToolTier → tool.speedMul。
    static float miningSpeedMul(quint8 blockId, int itemId);

    // 挖掘耗时（秒）= hardness / miningSpeedMul，地板 0.05s（防空手秒破致 t34 进度抖动 / 除零）。
    //   hardness<=0（火把瞬破 / air 越界）→ 0.05s 地板（air 越界实际不挖：canMine 已排除）。
    static float miningTime(quint8 blockId, int itemId);

    // 是否采掘掉落（破块后是否产出物品实体，供 t35 判定；掉落 id / 数量走 BlockRegistry::BlockDef）。
    //   方块不需工具（toolType=NoTool）→ 恒 true（空手可采且掉落）；
    //   需工具 → 须持匹配类型 AND tier >= minToolTier，否则 false（破后仅 AIR，不掉落）。
    static bool canHarvest(quint8 blockId, int itemId);

    // 方块是否可挖（spec t42）：实存方块（非 air / 非越界）且 hardness >= 0。
    //   - hardness == 0 → 瞬破可挖（如火把 t88）；
    //   - hardness < 0 → 不可挖（留给未来基岩类方块，无需特殊分支）。
    // 注：「实心」（碰撞）与「可挖」正交——火把 non-solid 但可挖。solid 不再作可挖前置。
    static bool canMine(quint8 blockId);

    // 用户可见中文显示名（工具段；PLAN §9 override (b) 通用词）。
    //   PickaxeWood=木镐 PickaxeStone=石镐 PickaxeIron=铁镐
    //   HoeWood=木锄 HoeStone=石锄 HoeIron=铁锄。非工具 / 越界 → 空串。
    // 字面量为 UTF-8，由 fromUtf8 解码（与项目既有中文注释 / BlockRegistry::displayName 同源）。
    static QString displayName(int itemId);

    // t263 工具最大耐久（使用次数上限；MC 1.0 经典值：木 59 / 石 131 / 铁 250，同 tier 共享）。
    //   非工具 / 越界 → 0（无耐久概念）。Hotbar 据本值初始化新工具实例的耐久 + tooltip 显「cur/max」。
    //   机制等价 MC 1.0 工具耐久（机制对齐，非名词照搬）；金 / 钻石档留后续任务（t264 扩工具集时追加 tier）。
    static int maxDurability(int itemId);

private:
    ToolRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // TOOLREGISTRY_H
