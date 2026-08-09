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
//   工具段：id >= ToolIdBase（0x100）；5 类工具（镐 / 斧 / 铲 / 剑 / 锄）× 3 档材质（木 / 石 / 铁）= 15 件。
// 工具不可堆叠（Hotbar::maxStackSize(id) 对工具段返回 1，t32 已留段）。
//
// 耐久模型（spec t263，机制等价 MC 1.0 工具耐久）：每工具一份 maxDurability（按 tier：木 < 石 < 铁），
//   每次有效使用（生存挖掘完成 / 锄耕地 / 剑攻击 / 斧伐木 / 铲掘土）-1，归零即破损（槽位清空、工具消失）。
//   创造模式不消耗（无限源）。耐久值随工具实例走（Hotbar::ItemStack.durability 字段，工具 count 恒 1 →
//   每实例独立耐久；背包内搬运经 setStack 显式传 durability 保真，见 hotbar.h）。
//   maxDurability 取 MC 1.0 经典值：木 59 / 石 131 / 铁 250（同 tier 的镐 / 锄 / 剑 / 斧 / 铲共享）。
//
// 挖掘模型（spec t33 + t265 工具速度效果，机制等价 MC 1.0；硬度 / 工具类型 / 掉落门槛走 BlockRegistry::BlockDef）：
//   - 挖掘耗时 = hardness / speedMul（秒）。
//   - speedMul（t265）：空手 / 类型不匹配 = 1（基准速）；匹配工具类型 → 按 tier 倍率（2/4/6）。
//     **掉落门槛（requiresTool）与速度解耦**：requiresTool=true 的方块（石类）须 tier>=minToolTier 才给速度加成
//     且才掉落；requiresTool=false 的方块（木 / 土 / 沙类）任意等级的正确类型工具均给速度加成，且空手也掉落。
//   - 掉落判定（canHarvest，t265 重构）：requiresTool=false → 恒掉落（空手可采）；requiresTool=true → 须持匹配
//     类型 AND tier >= minToolTier，否则破后仅 AIR（t35 不发掉落实体）。spec：「不匹配 / 等级不够 → 慢且不掉落，仅 AIR」
//     （仅对 requiresTool=true 的石类方块；木 / 土 / 沙空手慢挖仍掉落）。
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
    // t265 徒手 / 非武器工具攻击伤害（HP；MC 1.0 fist=1HP=半心）。剑走 attackDamage() 返 tier 倍率（4/5/6），
    //   其余（空手 / 镐 / 斧 / 铲 / 锄）统一本值。attackMob 据本值 ×暴击算最终伤害。
    static constexpr int kFistDamage = 1;

    // 工具物品 id（与 Hotbar::ItemStack 的工具段对齐）。工具段基址 0x100，与方块段（0..8）隔开，
    // 防 quint8 截断别名（工具 id > 255 不会与任何方块 id 混淆）。新增工具按序追加并同步 ToolCount。
    // t264 完整工具集：5 类（镐 / 锄 / 斧 / 铲 / 剑）× 3 档（木 / 石 / 铁）= 15 件，机制等价 MC 1.0 工具集。
    enum ToolId : int {
        ToolIdBase   = 0x100,
        PickaxeWood  = 0x100, // 木镐：tier 1，speedMul 2.0
        PickaxeStone = 0x101, // 石镐：tier 2，speedMul 4.0
        PickaxeIron  = 0x102, // 铁镐：tier 3，speedMul 6.0
        HoeWood      = 0x103, // 木锄：type=Hoe（专用耕地；不参与挖掘速度，speedMul 仅记账=1.0）
        HoeStone     = 0x104, // 石锄：type=Hoe tier 2
        HoeIron      = 0x105, // 铁锄：type=Hoe tier 3
        AxeWood      = 0x106, // 木斧：type=Axe tier 1，speedMul 2.0（伐木加速；t265 落实方块 toolType→Axe 后激活）
        AxeStone     = 0x107, // 石斧：type=Axe tier 2，speedMul 4.0
        AxeIron      = 0x108, // 铁斧：type=Axe tier 3，speedMul 6.0
        ShovelWood   = 0x109, // 木铲：type=Shovel tier 1，speedMul 2.0（掘土沙加速；t265 落实方块 toolType→Shovel 后激活）
        ShovelStone  = 0x10A, // 石铲：type=Shovel tier 2，speedMul 4.0
        ShovelIron   = 0x10B, // 铁铲：type=Shovel tier 3，speedMul 6.0
        SwordWood    = 0x10C, // 木剑：type=Sword tier 1，speedMul 1.0（不参与挖掘；剑攻击伤害归 t265）
        SwordStone   = 0x10D, // 石剑：type=Sword tier 2，speedMul 1.0
        SwordIron    = 0x10E, // 铁剑：type=Sword tier 3，speedMul 1.0
        // t304 弓（远程武器）：type=Bow（BlockRegistry::Bow=7；不参与挖掘速度，miningSpeedMul 恒 1.0 等同空手）。
        //   tier=1 / speedMul=1.0 仅为记账占位（弓的「速度」概念走拉弓蓄力，非挖掘 speedMul）。maxDurability=384
        //   （机制等价 MC 1.0 弓耐久；每次射箭 -1，生存模式消耗）。弓近战 = 徒手（attackDamage 返 kFistDamage），
        //   真实伤害由箭 + 蓄力决定（PlayerController bow draw/fire + EntityManager Arrow 命中）。不可堆叠（工具段
        //   maxStack=1）。ToolIcon / 手持 3D（BowGeometry）/ tooltip 据 toolType===Bow 分流到弓形渲染。
        Bow          = 0x10F, // 弓：type=Bow tier 1，speedMul 1.0（不参与挖掘）；右键长按拉弓 → 松开射箭（t304）
        // t300 剪刀（功能性工具）：type=Shears（BlockRegistry::Shears=6；专用剪羊毛 + 给羊毛方块挖掘速度加成）。
        //   tier=1 / speedMul=2.0（仅 Wool.toolType=Shears 时激活 → 羊毛方块挖掘加速；其余方块持剪刀 miningSpeedMul
        //   恒 1.0 等同空手，机制等价 MC 1.0 剪刀不擅长挖普通方块）。maxDurability=238（机制等价 MC 1.0 剪刀耐久；
        //   每次剪羊毛 -1，生存模式消耗）。**不可堆叠**（工具段 maxStack=1）。ToolIcon 据 toolType===Shears 自绘剪刀图标
        //   （两片交叉刀刃 + 中央枢轴 + 弹性弧环）；剪刀的真正价值在剪羊毛（playercontroller placeBlock shears 分支
        //   → EntityManager shearSheep），非挖掘。displayName「剪刀」（§9 通用词；非 MC 专名）。
        Shears       = 0x110, // 剪刀：type=Shears tier 1，speedMul 2.0（仅羊毛方块激活）；右键羊 → 剪羊毛（t300）
        // t401 钓鱼竿（功能性工具）：type=FishingRod（BlockRegistry::FishingRod=8；不参与挖掘速度，miningSpeedMul 恒
        //   1.0 等同空手，机制等价 MC 1.0 钓竿不影响挖掘）。tier=1 / speedMul=1.0 仅为记账占位（钓竿的「速度」概念
        //   走抛竿 → 咬钩 → 拉起时序，非挖掘 speedMul）。maxDurability=64（机制等价 MC 1.0 钓竿耐久；生存每次成功
        //   钓获 -1，创造不消耗）。钓竿近战 = 徒手（attackDamage 返 kFistDamage）。**不可堆叠**（工具段 maxStack=1）。
        //   ToolIcon 据 toolType===FishingRod 自绘钓竿图标（长杆 + 钓线 + 浮标）；右键抛竿 / 拉起由 PlayerController
        //   useFishingRod 驱动（机制等价 MC 1.0 钓鱼）。displayName「钓鱼竿」（§9 通用词；非 MC 专名）。
        FishingRod   = 0x111, // 钓鱼竿：type=FishingRod tier 1，speedMul 1.0（不参与挖掘）；右键抛浮标入水 → 拉起获物（t401）
        // t472 钻石镐（diamond pickaxe）：tier 4（铁档之上的最高档），speedMul 8.0（机制等价 MC 1.0 钻石镐
        //   —— 采掘黑曜石 Obsidian 的唯一工具：Obsidian.minToolTier=4，仅 tier>=4 的镐给速度加成且掉落；
        //   木 / 石 / 铁镐 tier<4 → miningSpeedMul 恒 1.0（慢）+ canHarvest false（破后仅 AIR，spec「lower-tier
        //   pick -> NO drop」））。maxDurability=1561（MC 1.0 钻石镐耐久）。**追加在末尾 0x112 而非插在铁镐后**：
        //   不重排既有枚举（保存档 / 配方向后兼容 —— 工具段 id 落 player_state JSON + 配方 outputId，重排会破坏
        //   旧存档与配方表），故钻石镐脱离「每类 3 档 contiguous」布局（ToolIcon.qml 的 itemIdFromTypeTier 对
        //   tier 4 特例映射到 0x112）。配方 = 3 钻石（RecipeRegistry::DiamondId）+ 2 木棒（仅工作台，机制等价
        //   MC 钻石镐配方）。displayName「钻石镐」（§9 通用词；非 MC 专名）。
        PickaxeDiamond = 0x112, // 钻石镐：type=Pickaxe tier 4，speedMul 8.0；采掘 Obsidian 的唯一工具（t472）
        ToolCount    = 19,    // 哨兵：已定义工具数（也是合法工具 id 相对 ToolIdBase 的上界）。
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
    //   空手 / 非工具 / 类型不匹配 → 1.0（基准速）；
    //   匹配工具类型 → tool.speedMul（tier 倍率 2/4/6）。t265：requiresTool=true 的方块（石类）额外要求
    //   tier>=minToolTier 才给加成（等级不够 = 慢，spec「等级不够 → 慢」）；requiresTool=false 的方块（木 / 土 / 沙）
    //   任意等级正确类型均给加成。
    static float miningSpeedMul(quint8 blockId, int itemId);

    // 挖掘耗时（秒）= hardness / miningSpeedMul，地板 0.05s（防空手秒破致 t34 进度抖动 / 除零）。
    //   hardness<=0（火把瞬破 / air 越界）→ 0.05s 地板（air 越界实际不挖：canMine 已排除）。
    static float miningTime(quint8 blockId, int itemId);

    // 是否采掘掉落（破块后是否产出物品实体，供 t35 判定；掉落 id / 数量走 BlockRegistry::BlockDef）。
    //   t265：requiresTool=false（木 / 土 / 沙类）→ 恒 true（空手可采且掉落，速度受工具影响但产物不依赖工具）；
    //   requiresTool=true（石类）→ 须持匹配类型 AND tier >= minToolTier，否则 false（破后仅 AIR，不掉落）。
    static bool canHarvest(quint8 blockId, int itemId);

    // t265 持物品攻击伤害（HP；spec「剑→加攻击伤害」）。机制等价 MC 1.0 武器伤害：
    //   - 剑（type=Sword）：tier 倍率 —— 木 4 / 石 5 / 铁 6（MC 1.0 sword damage）。
    //   - 其它（空手 / 镐 / 斧 / 铲 / 锄）：kFistDamage=1（MC 1.0 徒手 / 非武器工具伤害，剑是唯一武器）。
    //   暴击（玩家滞空下落挥击）由 caller 按 base*3/2 算（attackMob）；本方法只返基础伤害。
    //   非工具 / 越界 → kFistDamage（空手兜底）。玩家可用工具段 id 查（hotbar.selectedItemId）。
    static int attackDamage(int itemId);

    // 方块是否可挖（spec t42）：实存方块（非 air / 非越界）且 hardness >= 0。
    //   - hardness == 0 → 瞬破可挖（如火把 t88）；
    //   - hardness < 0 → 不可挖（留给未来基岩类方块，无需特殊分支）。
    // 注：「实心」（碰撞）与「可挖」正交——火把 non-solid 但可挖。solid 不再作可挖前置。
    static bool canMine(quint8 blockId);

    // 用户可见中文显示名（工具段；PLAN §9 override (b) 通用词）。
    //   PickaxeWood=木镐 PickaxeStone=石镐 PickaxeIron=铁镐
    //   HoeWood=木锄 HoeStone=石锄 HoeIron=铁锄
    //   AxeWood=木斧 AxeStone=石斧 AxeIron=铁斧
    //   ShovelWood=木铲 ShovelStone=石铲 ShovelIron=铁铲
    //   SwordWood=木剑 SwordStone=石剑 SwordIron=铁剑。非工具 / 越界 → 空串。
    // 字面量为 UTF-8，由 fromUtf8 解码（与项目既有中文注释 / BlockRegistry::displayName 同源）。
    static QString displayName(int itemId);

    // t263 工具最大耐久（使用次数上限；MC 1.0 经典值：木 59 / 石 131 / 铁 250，同 tier 共享）。
    //   非工具 / 越界 → 0（无耐久概念）。Hotbar 据本值初始化新工具实例的耐久 + tooltip 显「cur/max」。
    //   机制等价 MC 1.0 工具耐久（机制对齐，非名词照搬）；金 / 钻石档留后续任务（扩 tier 时追加）。
    static int maxDurability(int itemId);

    // t348 引擎工具 id → MC Java 1.0.0 物品数字 id 的**对齐映射**（资源包加载前置；与 docs/item-ids.md 工具段
    //   「MC 1.0.0」列一致）。**不重排 ToolId 枚举**（保存档 / 配方向后兼容）——工具段 id（0x100..）落 player_state
    //   JSON（背包）+ 配方 outputId，重排会破坏旧存档与配方表；故用「映射层」对齐。未来资源包加载器据此把引擎工具
    //   翻译成 MC 1.0 items.png 的贴图槽。剪刀（Shears，MC 物品 id 359，beta 1.7 加入、1.0 存在）亦在此映射。
    //   越界 / 非工具 id → -1（资源包回退引擎自绘 ToolIcon）。分层：本表属 Game，向下依赖 Core（BlockRegistry::ToolType）。
    static int mcToolId(int engineToolId);

private:
    ToolRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // TOOLREGISTRY_H
