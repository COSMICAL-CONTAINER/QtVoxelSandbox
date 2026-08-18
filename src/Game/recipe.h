#ifndef RECIPE_H
#define RECIPE_H

#include <QtGlobal> // quint8

#include "blockregistry.h" // 方块 id（原料 / 产物方块段）
#include "toolregistry.h"  // 工具段 id（产物：木镐等；Game 同层，向下依赖 Core）

// 合成配方注册表 + 匹配算法（Game 层；机制等价 MC 1.0 合成）。
//
// 与 BlockRegistry / ToolRegistry 同风格：纯静态数据表，无实例、无 Q_OBJECT。配方表是合成系统
// 的单一权威（UI 呈现层 SurvivalInventory.qml / CraftingTableUI.qml 与未来的冶炼系统都**只读**
// 查它，不各持副本 —— PLAN §2：数据单一）。输入 / 产出物品 id 复用 BlockRegistry（方块段）+
// ToolRegistry（工具段）；本类不另存物品表。
//
// 配方模型（spec t50）：
//   - gridSize：合成格尺寸（2 = 2×2 背包合成栏；3 = 3×3 工作台）。**关键**：2×2 配方（planks /
//     stick / craftingTable）也能在 3×3 工作台里合成；3×3 配方（woodPickaxe）只能在工作台。
//     匹配算法据此自适应（见 match()）。
//   - shapeless：true = 无序（只看原料多重集，位置任意）；false = 有序（位置敏感，按 MC「最小
//     包围盒」规则：输入与配方各自收缩到非空格的最小矩形，逐格比 id）。
//   - pattern[9]：行优先（[0..2]=顶行、[3..5]=中行、[6..8]=底行）；0=空格、>0=原料 id。
//     2×2 配方仅用 [0..3]（顶行 [0,1]、底行 [3,4] —— 注意 2×2 视为左上 2×2 子矩阵）。
//   - outputId / outputCount：一次合成产出物品 id 与数量。
//
// 数量处理（spec「MC 式数量」）：
//   - 点结果槽 → 取 outputCount 件到光标；输入槽每原料格 consume 1 份（count-1，归 0 清 id），
//     **不清空整槽**（剩余留槽，可继续合成）。这与 MC 一致（「2 原木 → 一次 4 板、原料 -1」可连点）。
//   - 每次合成的「原料用量」恒为 1（consumeCount 字段保留扩展位，当前配方全 1）。
//
// 分层（PLAN §2）：本层属 Game，只依赖 Core（BlockRegistry）+ 同层 ToolRegistry，**不**依赖
// Renderer/Physics/QtQuick3D。依赖只向下。合成匹配是纯函数（输入网格 → 匹配配方），无副作用；
// 实际消耗 / 产出由 UI 层（SurvivalInventory.qml / CraftingTableUI.qml）在点结果槽时执行（写本地
// craft 栈 + hotbar 光标手持栈），本类只负责「能不能合 / 合出什么」判定。
//
// §4 法律 + §9：配方名 / 产物名用通用词（木板 / 木棒 / 工作台 / 木镐）；零 MC 专有名词。
class RecipeRegistry
{
public:
    // 合成格尺寸（决定配方在哪种合成台可用：2×2 背包栏 / 3×3 工作台）。
    enum GridSize : int {
        Inventory2x2 = 2,
        Table3x3     = 3,
    };

    // 材料段基址（合成产物中「既非方块也非工具、可堆叠」的材料物品 id 区间，>= 0x200）。
    // 当前成员：木棒(0x200) / 煤炭(0x201) / 铁原矿(0x202) / 铁锭(0x203)。与 Hotbar 的材料段判定
    // （id >= 0x200）同源；改一处须同步另一处（.cpp 有 static_assert 对齐）。
    // 矿石→材料掉落由 BlockRegistry::BlockDef.dropId 引用（Core 层不依赖 Game，故 blockregistry.cpp
    // 用字面量 0x201/0x202；本处给字面量命名，全工程通过 RecipeRegistry::CoalId 等引用）。
    static constexpr int MaterialIdBase = 0x200;
    static constexpr int StickId        = 0x200; // 木棒：4 件产出（stick 配方）+ 木镐配方原料
    static constexpr int CoalId         = 0x201; // 煤炭：煤矿石挖掘掉落（BlockRegistry::CoalOre.dropId）；可作燃料 / 未来冶炼原料
    static constexpr int IronOreDropId  = 0x202; // 铁原矿：铁矿石挖掘掉落（BlockRegistry::IronOre.dropId）；熔炉冶炼为铁锭
    static constexpr int IronIngotId    = 0x203; // 铁锭：铁原矿冶炼产物；石镐 / 铁镐配方原料
    // t87 冶炼产物（SmeltingRegistry 用；与 Coal/铁系列同属材料段 >= 0x200，可堆叠 64）：
    static constexpr int GlassId        = 0x204; // 玻璃：沙子冶炼产物（spec 可选项）
    static constexpr int CharcoalId     = 0x205; // 木炭：原木冶炼产物（spec 可选项；与煤等价燃料）
    // t174 铁桶（功能性物品，复用材料段 id 区间；但**不可堆叠** —— Hotbar::maxStackSize 对这两个 id 返回 1）。
    //   机制等价 MC 1.0 铁桶：空桶可合成（3 铁锭 V 形）+ 右键舀水（命中水 → 装水铁桶 + 水源消失）/
    //   倒水（命中面相邻空气格 → 放置水源 + 桶变空）。属材料段（id >= 0x200）便于 isMaterial 分流到 MaterialIcon
    //   自绘桶图标（BucketEmptyId 画空桶 / WaterBucketId 画装水桶），但 maxStack=1 与工具段同语义（单件）。
    //   与 ToolRegistry::isTool（[0x100,0x103)）互斥 —— 桶非工具，不影响挖掘速度 / 掉落判定。
    static constexpr int BucketEmptyId  = 0x206; // 铁桶（空）：3 铁锭 V 形合成；右键水舀水 → WaterBucketId
    static constexpr int WaterBucketId  = 0x207; // 装水铁桶：空桶舀水得；右键倒出水水源 → BucketEmptyId
    // t343 装岩浆铁桶（LavaBucketId）：材料段 0x220。机制等价 MC 1.0 岩浆桶（lava bucket）——空桶舀岩浆源得；
    //   右键倒出岩浆源 → BucketEmptyId（生存消耗 / 创造不耗，同装水桶模式）。**不可堆叠**（maxStack=1，Hotbar::
    //   maxStackSize 特判，同空 / 装水桶）。非方块（材料段）→ 右键不走放置，走 useBlock 桶交互分支（同装水桶）。
    //   **无合成配方**（同装水桶——桶类内容由舀取获得，非合成）；MaterialIcon 自绘装岩浆桶图标（桶身 + 橙红岩浆）。
    //   MC 1.0 对齐 id 327（lava bucket）；与 docs/item-ids.md 材料段「MC 1.0.0」列一致（单一权威）。
    static constexpr int LavaBucketId   = 0x220; // 装岩浆铁桶：空桶舀岩浆源得；右键倒岩浆源 → 空桶（t343）
    // t235 小麦种子：材料段 0x208。挖草丛（TallGrass）掉落（BlockRegistry::TallGrass.dropId=0x208）；
    //   机制等价 MC 1.0「挖草丛掉小麦种子」。可堆叠 64；创造调色板可取用。**种植**（右键耕地→种小麦作物）
    //   归 t236（WheatCrop 方块 + 生长阶段）；本任务仅注册物品（可持 / 可掉 / 创造可取）。
    //   t237：收割小麦作物（WheatCrop）时也掉落种子（未成熟作物 / 成熟作物的额外种子）。
    static constexpr int SeedId         = 0x208; // 小麦种子：挖草丛掉落；种植 → 小麦作物（t236）；收割返种（t237）
    // t237 小麦物品：材料段 0x209。收割**成熟**小麦作物（WheatCrop，state==WheatCropStageMax）掉落
    //   （机制等价 MC 1.0「成熟小麦收割掉小麦物品」）。可堆叠 64；创造调色板补全归 t244。
    //   小麦物品是非方块 / 非工具材料（同种子 / 木棒 / 煤），走 MaterialIcon 自绘图标 + 本地通用名。
    //   下游消费：t238 面包配方（3 小麦 → 1 面包；面包为后续任务，本任务仅注册小麦物品使其可持 / 可掉 / 可堆叠）。
    static constexpr int WheatId        = 0x209; // 小麦物品：收割成熟小麦作物掉落；面包原料（t238）
    // t238 面包：材料段 0x20A。3 小麦合成（仅工作台 3×3 横排）；右键食 → 恢复饱食度（PlayerController
    //   useItem 分支 +5 hunger；机制等价 MC 1.0 面包 +5 hunger / 2.5 鼓腿）。可堆叠 64；非方块（材料段）
    //   → 右键不放置，走「食用」分支（同桶 / 种子：在 selectedBlock Air 守卫之前分流）。MaterialIcon 自绘
    //   面包块图标。创造调色板补全归 t244；本任务仅注册使其可合 / 可食 / 可堆叠。
    static constexpr int BreadId        = 0x20A; // 面包：3 小麦合成；右键食 → +5 饥饿（t238）
    // t242 mob 死亡掉落（材料段 0x20B..0x20E；机制等价 MC 1.0 被动生物掉落物；非方块、可堆叠 64）：
    //   杀猪掉生猪排 / 杀牛掉生牛肉 + 皮革 / 杀羊掉羊毛。EntityManager mobDied 信号 → Main.qml 据本 id 调
    //   ItemEntityManager.spawnItem 生成掉落实体（同 spawnItem 模式；PLAN §2 分层：Entities 层发语义事件、
    //   呈现层只消费）。MaterialIcon 自绘图标 + 进创造调色板均由 t244 完成（Hotbar::creativeMaterials 拾取即满栈 64，
    //   供测试 / 装饰直接取用；生存时由 mob 死亡掉落 / 拾取获得）。名称 / 模型全原创（§9 区隔，零 MC 资产 / 专名）。
    static constexpr int RawPorkchopId  = 0x20B; // 生猪排：杀猪掉落（机制等价 MC 1.0 raw porkchop）
    static constexpr int RawBeefId      = 0x20C; // 生牛肉：杀牛掉落（机制等价 MC 1.0 raw beef）
    static constexpr int LeatherId      = 0x20D; // 皮革：杀牛掉落（机制等价 MC 1.0 leather）
    static constexpr int WoolId         = 0x20E; // 羊毛：杀羊掉落（机制等价 MC 1.0 wool）
    // t243 生物蛋（spawn eggs）：材料段 0x20F..0x211。创造模式物品，右键地面 → EntityManager::spawnMobTyped
    //   生成对应被动生物（猪 / 牛 / 羊；机制等价 MC 1.0 spawn egg）。三种蛋各映射一种 mobType（与
    //   EntityManager::MobType MobPig/Cow/Sheep 同值）；PlayerController::placeBlock 据本 id 分流走「使用」分支
    //   （同桶 / 锄 / 种子：非方块材料段 → 在 selectedBlock Air 守卫之前分流，不走方块放置路径）。可堆叠 64
    //   （机制等价 MC 1.0 spawn egg maxStack 64；走材料段默认 maxStack=64，无需特例）。生存消耗 1 蛋 / 创造不耗
    //   （同种子 / 面包模式）。MaterialIcon 自绘蛋形图标（壳 + mob 配色斑点）；创造调色板补全归 t244。名称 /
    //   图标全原创（§9 区隔，零 MC 资产 / 专名）。
    static constexpr int SpawnEggPigId   = 0x20F; // 生物蛋（猪）：右键地面 → 生成猪（MobPig）
    static constexpr int SpawnEggCowId   = 0x210; // 生物蛋（牛）：右键地面 → 生成牛（MobCow）
    static constexpr int SpawnEggSheepId = 0x211; // 生物蛋（羊）：右键地面 → 生成羊（MobSheep）
    // t279 钻石：材料段 0x212。**钻石矿挖掘掉落**（BlockRegistry::DiamondOre.dropId=0x212；机制等价 MC 1.0「钻石矿
    //   需铁镐采掘、掉钻石」，非冶炼产物 —— 钻石矿直接掉钻石材料，区别于铁原矿需冶炼成铁锭）。可堆叠 64；非方块
    //   → 右键不放置。MaterialIcon 自绘钻石晶体图标（青白多面切割宝石，§9a）。创造调色板补全便于测试 / 装饰取用。
    //   t279 仅注册物品（可持 / 可掉 / 创造可取）；钻石工具（钻石镐 tier 4 等）配方属后续任务，本轮不做。
    static constexpr int DiamondId       = 0x212; // 钻石：钻石矿挖掘掉落（BlockRegistry::DiamondOre.dropId）；可堆叠 64
    // t287 敌对生物蛋（spawn eggs）：材料段 0x213..0x215。创造模式物品，右键地面 → spawnMobTyped 生成敌对生物
    //   （Shambler/Bones/Stalker；机制等价 MC 1.0 僵尸/骷髅/苦力怕 spawn egg）。§9 改名（Zombie→Shambler「蹒跚者」、
    //   Skeleton→Bones「骸骨」、Creeper→Stalker「潜行者」），仅机制对齐。Spider 蛋留待 t285 蜘蛛实现后补。
    static constexpr int SpawnEggShamblerId = 0x213; // 生物蛋（蹒跚者）：右键 → 生成 Shambler（敌对近战，MobShambler）
    static constexpr int SpawnEggBonesId    = 0x214; // 生物蛋（骸骨）：右键 → 生成 Bones（敌对远程射箭，MobBones）
    static constexpr int SpawnEggStalkerId  = 0x215; // 生物蛋（潜行者）：右键 → 生成 Stalker（敌对爆炸，MobStalker）
    static constexpr int SpawnEggSpiderId   = 0x216; // t285 生物蛋（蜘蛛）：右键 → 生成 Spider（敌对快速，MobSpider）
    // t299 敌对 mob 死亡掉落（材料段 0x217..0x219；机制等价 MC 1.0 敌对生物掉落物；非方块、可堆叠 64）：
    //   杀骸骨（Bones）掉骨头 / 杀蹒跚者（Shambler）掉腐肉 / 杀蜘蛛（Spider）掉线。EntityManager mobDied 信号 →
    //   Main.qml 据本 id 调 ItemEntityManager.spawnItem 生成掉落实体（同 t242 被动掉落模式；PLAN §2 分层：Entities
    //   层发语义事件、呈现层只消费）。MaterialIcon 自绘图标 + 进创造调色板（Hotbar::creativeMaterials 拾取即满栈 64，
    //   供测试 / 装饰直接取用；生存时由 mob 死亡掉落 / 拾取获得）。名称 / 图标全原创（§9 区隔，零 MC 资产 / 专名）。
    //   **弓 + 箭掉落**（spec「骸骨→弓(带耐久)+箭+骨头」）归 t301（骷髅持弓）/ t304（弓箭物品系统）：那两任务注册弓 /
    //   箭物品 + 持久耐久 + 拉弓射箭机制；本任务仅落骸骨的**材料**掉落（骨头），弓 / 箭留 t301 / t304（避免半成品弓
    //   无图标 / 无用法的中间态）。
    static constexpr int BoneId       = 0x217; // 骨头：杀骸骨（MobBones）掉落（机制等价 MC 1.0 骨头；可堆叠 64）
    static constexpr int RottenFleshId= 0x218; // 腐肉：杀蹒跚者（MobShambler）掉落（机制等价 MC 1.0 腐肉；可堆叠 64）
    static constexpr int StringId     = 0x219; // 线：杀蜘蛛（MobSpider）掉落（机制等价 MC 1.0 线；弓 / 钓竿原料，t304 弓配方用）
    // t304 箭（弓弹药）：材料段 0x21A。可堆叠 64；非方块（材料段）→ 右键不放置。弓右键蓄力松开射出箭实体
    //   （复用 t283 Arrow 实体 + EntityManager::spawnArrowPlayer）；命中 mob 伤害（蓄力越高伤害越高，1..6 HP）。
    //   MaterialIcon 自绘箭头 + 杆 + 箭羽图标。创造调色板可取用（同木棒 / 铁锭等材料）；生存由合成获得
    //   （铁锭 + 木棒 + 线 → 4 箭，机制等价 MC 1.0 箭配方燧石+棒+羽毛的本地化替代——本工程无燧石 / 羽毛，
    //   用铁锭代箭头、线代羽毛）。名称 / 图标全原创（§9 区隔）。
    static constexpr int ArrowId      = 0x21A; // 箭：弓弹药；铁锭+木棒+线合成 4 件；弓射出（t304）
    // t305 树苗物品：材料段 0x21B。**树叶衰减 / 玩家破叶掉落**（playercontroller dropLeafDrops：破叶概率掉树苗物品
    //   + 木棒，机制等价 MC 1.0 破叶 5% 掉树苗）。可堆叠 64；非方块（材料段）→ 走 useBlock 种植（同种子模式：
    //   持树苗物品右键草地 / 泥土 → 在其上方一格种下 Sapling 方块，WorldClock tick 推进成长长成完整橡树）。
    //   MaterialIcon 自绘树苗图标（棕色短树干 + 绿色嫩叶小球冠）。创造调色板可取用（creativeMaterials）。
    //   破 Sapling 方块亦掉本物品（BlockRegistry::Sapling.dropId=0x21B），玩家可回收再种。名称 / 图标全原创（§9 区隔）。
    static constexpr int SaplingItemId = 0x21B; // 树苗物品：破叶概率掉落；右键草地/泥土种植 → Sapling 方块（t305）
    // t308 铜/金原矿 + 锭（机制等价 MC 1.0「铜/铁/金矿采下为原矿，须熔炉冶炼成锭」）：
    //   铜矿 / 金矿挖掘掉落**原矿**（非锭，区别于钻石矿直接掉钻石——钻石是宝石无需冶炼）；原矿经熔炉冶炼成锭。
    //   铁原矿(0x202)→铁锭(0x203) 既已存在；本任务补铜 / 金两条对称链。可堆叠 64；非方块（材料段）→ 右键不放置。
    //   MaterialIcon 自绘图标（原矿走矿石族八边形 + 金属斑；锭走水平梯形金属条，配色按金属区分）。
    //   下游消费：t308 仅注册物品（可持 / 可掉 / 创造可取 / 可冶炼）；铜/金工具 / 装备配方属后续任务（钻石工具前留位）。
    static constexpr int CopperOreDropId = 0x21C; // 铜原矿：铜矿石挖掘掉落（BlockRegistry::CopperOre.dropId）；熔炉冶炼为铜锭
    static constexpr int CopperIngotId   = 0x21D; // 铜锭：铜原矿冶炼产物；铜工具 / 装备配方原料（后续任务）
    static constexpr int GoldOreDropId   = 0x21E; // 金原矿：金矿石挖掘掉落（BlockRegistry::GoldOre.dropId）；熔炉冶炼为金锭
    static constexpr int GoldIngotId     = 0x21F; // 金锭：金原矿冶炼产物；金工具 / 装备 / 钟配方原料（后续任务）
    // t344 烤肉（mob 燃烧致死掉落；机制等价 MC 1.0「着火死亡的动物掉熟肉」）：被动生物在燃烧态（fireTimer>0，
    //   触碰岩浆 / 火点燃）下死亡时，EntityManager mobDied 信号带 burned=true → Main.qml onMobDied 据 mobType
    //   把「生肉掉落」替换为本段熟肉掉落（猪→熟猪排 / 牛→熟牛肉 / 羊→熟羊肉；皮革 / 羊毛等非肉掉落不变）。
    //   机制等价 MC 1.0 cooked porkchop / cooked beef / cooked mutton；纯原创自绘 MaterialIcon 图标（§9a）。
    //   可堆叠 64（走材料段默认）；非方块 → 右键不放置。生存由「烧死动物」获得（替代熔炉烤肉，t344 范围），
    //   创造调色板补全便于测试（同生肉）。无 MC 1.0 等价映射（mutton / 熟肉段 1.0 物品表无对应，1.8+ 才有 →
    //   mcMaterialId 返 -1，资源包回退引擎自绘 MaterialIcon）。
    static constexpr int CookedPorkchopId = 0x221; // 熟猪排：猪燃烧致死掉落（机制等价 MC 1.0 cooked porkchop）
    static constexpr int CookedBeefId     = 0x222; // 熟牛肉：牛燃烧致死掉落（机制等价 MC 1.0 cooked beef / steak）
    static constexpr int CookedMuttonId   = 0x223; // 熟羊肉：羊燃烧致死掉落（机制等价 MC cooked mutton；§9 区隔改名）
    // t393 战利品表（loot table）专用材料段物品（地牢箱 + 渔获共用 LootTable）。机制等价 MC 1.0 dungeon chest loot
    //   里的稀有件 / 红石粉等；本任务仅注册使其「可持 / 可掉 / 可堆叠 / 有名 / 有图标」，无合成配方（地牢战利品
    //   非合成获得，同桶类）。可堆叠 64（走材料段默认）；非方块（材料段）→ 右键不放置。名称 / 图标全原创自绘（§9a）。
    //   MC 1.0 对齐：红石粉=331 / 马鞍=329；命名牌（1.6+）/ 附魔书（1.4+）1.0 不存在 → mcMaterialId 返 -1（资源包回退）。
    static constexpr int RedstoneId      = 0x224; // 红石粉：地牢战利品（机制等价 MC 1.0 redstone dust）；可堆叠 64
    static constexpr int SaddleId        = 0x225; // 马鞍：地牢稀有战利品（机制等价 MC 1.0 saddle）；可堆叠 64（简化）
    static constexpr int NameTagId       = 0x226; // 命名牌：地牢稀有战利品（机制等价 MC name tag，1.6+）
    static constexpr int EnchantedBookId = 0x227; // 附魔书：附魔台附书产 + 地牢极稀有战利品（机制等价 MC enchanted book；t615 真附魔——enchants 元数据携带附魔列表，maxStack=1）
    // t398 鸡（chicken）相关材料段物品（0x228..0x22C；机制等价 MC 1.0 鸡掉羽毛 + 生鸡肉 + 周期下蛋）：
    //   杀鸡掉羽毛 + 生鸡肉；鸡燃烧致死 → 生鸡肉替换为熟鸡肉（机制等价 MC 1.0 着火死亡掉熟肉，同猪/牛/羊）。
    //   鸡周期性下蛋（EGG 物品掉落，机制等价 MC 1.0 鸡 5-10 分钟下一枚蛋）。可堆叠 64；非方块（材料段）→ 右键不放置。
    //   MaterialIcon 自绘图标（羽毛 / 肉排 / 蛋），创造调色板补全（同生肉 / 熟肉）。名称 / 图标全原创（§9 区隔，零 MC 资产）。
    //   **生物蛋（鸡）** SpawnEggChickenId：创造模式物品，右键地面 → EntityManager::spawnMobTyped 生成 MobChicken。
    static constexpr int FeatherId         = 0x228; // 羽毛：杀鸡掉落（机制等价 MC 1.0 feather；箭配方原料预留）
    static constexpr int RawChickenId      = 0x229; // 生鸡肉：杀鸡掉落（机制等价 MC 1.0 raw chicken）
    static constexpr int CookedChickenId   = 0x22A; // 熟鸡肉：鸡燃烧致死掉落（机制等价 MC 1.0 cooked chicken）
    static constexpr int EggId             = 0x22B; // 蛋：鸡周期性下蛋掉落（机制等价 MC 1.0 egg）；可堆叠 16（§9 简化走材料段默认 64）
    static constexpr int SpawnEggChickenId = 0x22C; // 生物蛋（鸡）：右键地面 → 生成鸡（MobChicken）
    // t399 鱿鱼（squid）相关材料段物品（0x22D..0x22E；机制等价 MC 1.0 鱿鱼水里游 + 死亡掉墨囊）：
    //   杀鱿鱼掉墨囊（1-3）；墨囊非食物 / 非燃料（§9 简化预留，未来染料 / 书与笔原料）。可堆叠 64；非方块（材料段）
    //   → 右键不放置。MaterialIcon 自绘图标（墨囊黑水滴），创造调色板补全。名称 / 图标全原创（§9 区隔，零 MC 资产）。
    //   **生物蛋（鱿鱼）** SpawnEggSquidId：创造模式物品，右键地面 → EntityManager::spawnMobTyped 生成 MobSquid。
    static constexpr int InkSacId          = 0x22D; // 墨囊：杀鱿鱼掉落（机制等价 MC 1.0 ink sac；染料 / 书与笔原料预留）
    static constexpr int SpawnEggSquidId   = 0x22E; // 生物蛋（鱿鱼）：右键地面 → 生成鱿鱼（MobSquid）
    // t400 繁殖食物（材料段 0x22F/0x230；机制等价 MC 1.0 胡萝卜 / 马铃薯 —— 猪的繁殖食物）。可堆叠 64；非方块
    //   （材料段）→ 右键走 useBlock「喂食」分支（PlayerController placeBlock 食物分支 → EntityManager::feedMob）：
    //   命中**成体猪** → 触发求偶期（同种配对产幼崽）。命中非猪 / 幼崽 / 无命中 → 不消耗（无其他 useBlock 用途）。
    //   MaterialIcon 自绘图标（胡萝卜橙红锥根 + 绿缨 / 马铃薯棕黄椭圆块茎），创造调色板补全便于测试繁殖。
    //   名称 / 图标全原创（§9 区隔，零 MC 资产 / 专名）。与小麦（牛 / 羊食物）/ 种子（鸡食物）一并构成 4 种被动
    //   生物的繁殖食物表（EntityManager::feedMob 据 mobType 判食物匹配）。
    static constexpr int CarrotId          = 0x22F; // 胡萝卜：猪繁殖食物（喂成体猪 → 求偶；机制等价 MC 1.0 carrot）
    static constexpr int PotatoId          = 0x230; // 马铃薯：猪繁殖食物（喂成体猪 → 求偶；机制等价 MC 1.0 potato）
    // t669 毒马铃薯（材料段 0x241）：机制等价 MC 1.0 poisonous potato（1.4.2+ 食物）。**马铃薯的坏变种**——
    //   薯皮泛绿（未成熟龙葵碱「solanine」致毒，机制等价 MC 毒马铃薯 「wolfsbane」 60% 概率食物中毒）。
    //   可食：恢复 +kPotatoHunger 饥饿但 **60% 概率中毒**（m_poisonTimer 8s：每秒 -1 饥饿 + -1 HP，
    //   PlayerController finishEating 掷骰 → tickImpl 推进，机制等价 MC 毒马铃薯食后中毒扣血）。
    //   获得途径：创造调色板直接取用（t669；destructive-test 用）+ 未来马铃薯作物收割概率掉落可挂 loottable。
    //   可堆叠 64（材料段默认）；非方块（材料段）→ 右键走「食用」useBlock 分支（同面包 / 甜浆果模式）。
    //   MaterialIcon 自绘「绿皮毒薯」图标（drawPoisonPotato，薯皮泛绿 + 暗斑，§9a 区隔原创）。
    //   pack 映射：itemFilenameMap 0x241 → poisonous_potato.png（demo 包有 poisonous_potato.png 则接，缺则自绘）。
    //   mcMaterialId 无映射越界（>0x240）→ 资源包回退引擎自绘（同 SnowballId 越界模式）。
    static constexpr int PoisonousPotatoId = 0x241; // 毒马铃薯：薯皮泛绿；食后 60% 中毒（每秒 -1 饥饿 -1 HP，8s）
    // t401 生鱼（钓鱼获物；机制等价 MC 1.0 raw fish / cod）。材料段 0x231。钓竿抛浮标入水 → 等咬钩 → 拉起按
    //   LootTable::fishingPool 抽获，本 id 是「鱼」类获物（高权重）。可堆叠 64；非方块（材料段）→ 右键不放置。
    //   MaterialIcon 自绘鱼形图标（银蓝鱼身 + 尾鳍 + 眼）；创造调色板补全便于测试。名称 / 图标全原创（§9a 区隔）。
    //   无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎自绘）。
    static constexpr int RawFishId         = 0x231; // 生鱼：钓竿拉起获物（机制等价 MC 1.0 raw fish；钓鱼常见获物）
    // t447 骨粉（bone meal）：材料段 0x232。**骨头合成产物**（1 骨头 → 3 骨粉，无序 2×2 / 3×3，机制等价 MC 1.0
    //   bone→3 bone meal）。可堆叠 64；非方块（材料段）→ 右键不放置，走 useBlock「催熟」分支（同桶 / 种子：在
    //   selectedBlock Air 守卫之前分流）：右键命中**未成熟作物**（小麦 / 胡萝卜 / 马铃薯，state<WheatCropStageMax）
    //   → 作物 state+1（即时催熟一阶段，机制等价 MC 1.0 骨粉右键作物 +1 age）。生存消耗 1 骨粉 / 创造不耗。
    //   MaterialIcon 自绘骨粉图标（米白粉末堆 + 几粒骨碎，§9a 区隔原创）。创造调色板补全便于测试 / 装饰取用。
    //   无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎自绘）。
    static constexpr int BonemealId        = 0x232; // 骨粉：骨头合成产物；右键未成熟作物 → 催熟一阶段（t447）
    // t467 甜浆果（sweet berry）：材料段 0x233。机制等价 MC 1.0 sweet berries——雪原浆果灌木丛（SweetBerryBush）的
    //   采摘产物 + 可食食物。可堆叠 64；非方块（材料段）→ 右键不放置，走 useBlock「食用」分支（playercontroller
    //   beginEating/finishEating，长按右键累积进食进度满后消耗 1 浆果 + 恢复饥饿 kSweetBerryHungerAmount=2，机制等价
    //   MC 1.0 甜浆果 +2 hunger）。**获得途径**：右键**成熟**浆果丛（SweetBerryBush state==SweetBerryBushStageMax）→
    //   2-3 浆果 + 丛回阶段 0 重新长（playercontroller useBlock 采摘分支，5 参数 setBlock 降阶段 id 不变只 state 变 +
    //   worldChanged 重建 mesh，同 t447 骨粉模式）；破丛亦掉 1 浆果（BlockDef.dropId 兜底）。创造调色板补全便于测试。
    //   MaterialIcon 自绘浆果图标（红色圆润浆果簇 + 绿色花萼，§9a 区隔原创）。无 MC 1.0 mcMaterialId 映射
    //   （sweet berries id 477 为 1.14+；id 越表界 → -1 → 资源包回退引擎自绘 MaterialIcon）。
    static constexpr int SweetBerryId      = 0x233; // 甜浆果：成熟浆果丛采摘得；可食（右键长按 +2 饥饿，t467）
    // t469 船物品（boat）：材料段 0x234/0x235。机制等价 MC 1.0 boat（5 木板 U 形合成；右键水面放置；浮水 + 可骑乘
    //   WASD 操控；冰上加速；高速撞硬墙损坏掉落船物品）。**橡木船 OakBoatId（浅色）/ 云杉船 SpruceBoatId（深色）**
    //   仅贴图 / 配色不同，机制完全同构（对应橡木 / 云杉木板合成，机制等价 MC 1.0 oak/spruce boat）。可堆叠 64；
    //   非方块（材料段）→ 右键不走方块放置，走 useBlock 船交互分支（playercontroller placeBlock 船段：先试骑乘命中的
    //   船实体 findBoatHit → mount；否则持船物品 → 在水面放置船实体 BoatManager.spawnBoat；生存消耗 1 / 创造不耗）。
    //   船实体是 Entities 层 BoatManager 的新实体类型（浮水 + 骑乘 + WASD + 冰上加速 + 撞坏掉落），呈现层 Main.qml
    //   boatHost Repeater 渲染船 Model（橡木 / 云杉贴图，NoLighting）。MaterialIcon 自绘船图标（俯视船形，橡木浅 /
    //   云杉深）。无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎
    //   自绘 MaterialIcon）。§9 区隔：仅机制对齐 MC 1.0 boat，名称 / 模型 / 贴图全原创。
    static constexpr int OakBoatId    = 0x234; // 橡木船：5 橡木木板 U 形合成；右键水面放置 + 骑乘（机制等价 MC 1.0 oak boat）
    static constexpr int SpruceBoatId = 0x235; // 云杉船：5 云杉木板 U 形合成；右键水面放置 + 骑乘（机制等价 MC 1.0 spruce boat）
    // t471 青金石（lapis）：材料段 0x236。机制等价 MC 1.0 青金石（lapis lazuli）—— **青金矿石挖掘掉落**
    //   （BlockRegistry::LapisOre.dropId=0x236；机制等价 MC 1.0「青金矿采下直接掉青金石物品」，区别于铁/铜/金
    //   矿掉原矿须冶炼）。可堆叠 64；非方块（材料段）→ 右键不放置。MaterialIcon 自绘青金石图标（深群青蓝
    //   多面晶体 + 黄铁矿金点，§9a 区隔原创）。**附魔台每次附魔消耗 1-3 个 + 经验等级**（附魔前置材料，
    //   消耗机制归 t474 附魔台）。创造调色板补全便于测试 / 装饰取用。无 MC 1.0 mcMaterialId 映射
    //   （id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎自绘 MaterialIcon）。
    static constexpr int LapisId        = 0x236; // 青金石：青金矿石挖掘掉落（BlockRegistry::LapisOre.dropId）；附魔台消耗材料（t474）
    // t473 纸 + 书（paper / book；机制等价 MC 1.0 甘蔗造纸 → 书，附魔台 / 附魔书 / 书架的核心材料链）：
    //   纸 PaperId：材料段 0x237。机制等价 MC 1.0 paper——**3 甘蔗横排合成 → 3 纸**（recipe.cpp paper 配方）。
    //   原料甘蔗 = BlockRegistry::Sugarcane（破甘蔗掉自身方块，可入合成格）。可堆叠 64；非方块（材料段）→ 右键不放置。
    //   书 BookId：材料段 0x238。机制等价 MC 1.0 book——**3 纸 + 1 皮革合成 → 1 书**（recipe.cpp book 配方，2×2
    //   左上 P P / P L）。皮革来源：杀牛 / 杀猪掉落（t242 LeatherId=0x20D；t473 扩到猪也掉）。可堆叠 64；非方块
    //   → 右键不放置。下游消费：附魔台（t474）/ 附魔书 / 书架（书架方块配方原料，后续任务）。MaterialIcon 自绘
    //   图标（纸=米黄纸页 / 书=合上的书带封面书脊，§9a 区隔原创）。创造调色板补全便于测试 / 装饰取用。
    //   无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎自绘
    //   MaterialIcon；与 carrot / potato / bonemeal / lapis 等近期材料段物品同模式，不扩 kMcMaterialId 表）。
    static constexpr int PaperId        = 0x237; // 纸：3 甘蔗横排合成 → 3 件；书配方原料（t473）
    static constexpr int BookId         = 0x238; // 书：3 纸 + 1 皮革合成 → 1 件；附魔台 / 附魔书 / 书架材料（t473）
    // t485 火药（gunpowder）：材料段 0x239。机制等价 MC 1.0 gunpowder——潜行者（Stalker，机制等价 MC 苦力怕）
    //   死亡掉落 + TNT 合成原料（5 火药 + 4 沙 → 1 TNT）。可堆叠 64；非方块 → 右键不放置。
    //   无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎自绘
    //   MaterialIcon；与 paper / book / lapis 等近期材料段物品同模式，不扩 kMcMaterialId 表）。
    static constexpr int GunpowderId    = 0x239; // 火药：杀潜行者掉落；TNT 合成原料（5 火药 + 4 沙 → 1 TNT，t485）
    // t487 末影之眼（EndEye）：材料段 0x23A。机制等价 MC 1.0 末影之眼（ender eye）—— 持本物品右键末地传送门
    //   （EndPortal 方块）→ placeBlock useBlock 分支翻传送门 state bit0（激活态）+ qInfo 日志（末地预热占位：
    //   仅激活效果 + 日志，不实现末地维度，spec「实际传送末地可推迟为占位/告警」）。可堆叠 64；非方块（材料段）
    //   → 右键不走放置，走 useBlock 末地传送门激活分支（同桶 / 食物：在 selectedBlock Air 守卫之前分流）。
    //   MaterialIcon 自绘末影之眼图标（绿蓝球体 + 中心瞳孔）。**获得途径**：要塞（Stronghold）宝藏箱战利品
    //   （LootTable::strongholdChestPool，t487）—— 末影之眼是要塞稀有掉落，玩家探索要塞开箱获得用于激活传送门。
    //   创造调色板补全（hotbar creativeMaterials，便于测试激活）。无合成配方（同末影珍珠 / 烈焰粉，本工程无
    //   下界 / 末影人故不作合成链；机制等价 MC 末影之眼 = 末影珍珠 + 烈焰粉合成，但前置生物留后续）。
    //   无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎自绘）。
    static constexpr int EndEyeId       = 0x23A; // 末影之眼：要塞宝藏箱战利品；右键末地传送门激活（占位，t487）
    // t507 木碗 + 蘑菇汤（机制等价 MC 1.0 bowl / mushroom stew；名称 / 图标全原创自绘 §9a 区隔）：
    //   木碗 BowlId：材料段 0x23B。**4 木板菱形合成 → 1 木碗**（recipe.cpp bowl 配方，2×2：3 木板左上 / 右上 / 左下，
    //   右下空 —— MC 1.0 木碗 V 形合成的本地化）。可堆叠 64；非方块（材料段）→ 右键不放置。MaterialIcon 自绘
    //   木碗图标（米色木质浅碗俯视）。下游消费：蘑菇汤配方原料（碗 + 红蘑菇 + 白蘑菇 → 1 蘑菇汤）。创造调色板补全
    //   便于测试。无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退）。
    //   蘑菇汤 MushroomStewId：材料段 0x23C。**碗 + 红蘑菇 + 白蘑菇合成 → 1 蘑菇汤**（recipe.cpp stew 配方，无序
    //   shapeless 2×2 / 3×3：三原料各 1 件）。可堆叠 **1**（Hotbar::maxStackSize 特判，同铁桶 —— MC 1.0 蘑菇汤
    //   maxStack 1，碗装液体食物不可叠；机制等价非数值复刻）；非方块（材料段）→ 右键不放置，走「食用」分支
    //   （playercontroller beginEating / finishEating，长按右键累积进食进度满后消耗 1 蘑菇汤 + 恢复饥饿
    //   kMushroomStewHungerAmount=10，机制等价 MC 1.0 蘑菇汤 +10 hunger / 5 鼓腿 —— 同 MC 量级最高档食物之一）。
    //   MaterialIcon 自绘蘑菇汤图标（木碗 + 红 / 白蘑菇块 + 汤面）。创造调色板补全。食用后**返空碗**（finishEating
    //   内特判：消耗 1 蘑菇汤 + 给回 1 空碗物品，机制等价 MC 1.0「喝完汤碗留下」；同 MC 量级非精确复刻）。
    //   无 MC 1.0 mcMaterialId 映射（id 越表界 → -1 → 资源包回退引擎自绘 MaterialIcon）。
    static constexpr int BowlId          = 0x23B; // 木碗：4 木板合成；蘑菇汤配方原料（t507）
    static constexpr int MushroomStewId  = 0x23C; // 蘑菇汤：碗+红蘑菇+白蘑菇合成；右键食 +10 饥饿（t507）
    // t510 雪球（snowball）：材料段 0x23D。机制等价 MC 1.0 雪球物品——**雪傀儡死亡掉落** 0-15 个（mechanic-
    //   equivalent MC snow golem death drops 0-15 snowballs；呈现层 onMobDied MobSnowGolem 分支据 Math.random
    //   0-15 spawnItem 本 id，同被动 mob 多件独立 spawnItem 模式）。可堆叠 64；非方块（材料段）→ 右键不放置。
    //   无 MC 1.0 mcMaterialId 映射（id > SpawnEggSquidId=0x22E，越 kMcMaterialId 表界 → -1 → 资源包回退引擎自绘
    //   MaterialIcon）。图标走 MaterialIcon drawSnowball（程序生成冷白小球，§9 原创）。
    static constexpr int SnowballId      = 0x23D; // 雪球：雪傀儡死亡掉落 0-15 个（t510）；可堆叠 64
    // t565 矿车（minecart）：材料段 0x23E。机制等价 MC 1.0 minecart——**5 铁锭 U 形合成 → 1 矿车**
    //   （recipe.cpp minecart 配方，机制等价 MC 1.0 minecraft 配方 5 iron ingot）。右键铁轨放置矿车实体
    //   （MinecartManager，Entities 层）；矿车沿轨行驶（WASD 驱动 / 上车右键 / Shift 下车）；左键挖矿车 →
    //   掉矿车物品。可堆叠 1（载具，同船 maxStack 1？—— 船可 64，矿车对齐 MC 1.0 maxStack 1？MC 1.0 矿车
    //   实际 maxStack 1（非堆叠载具）→ 本工程取 1）。非方块（材料段）→ 右键走矿车交互分支（playercontroller
    //   placeBlock minecart 段，同船模式）。图标走 MaterialIcon drawMinecart（程序生成矿车斗形，§9 原创）。
    static constexpr int MinecartId      = 0x23E; // 矿车：5 铁锭合成；右键铁轨放置 + 骑乘行驶（t565）
    // t567 指南针（compass）：材料段 0x23F。机制等价 MC 1.0 compass——**4 铁锭十字 + 中心 1 红石合成 → 1 指南针**
    //   （recipe.cpp compass 配方；机制等价 MC 1.0 compass 配方 4 iron ingot + 1 redstone 十字）。可堆叠 64；
    //   非方块（材料段）→ 右键不放置。作用：HUD 显示指针指向**出生点**（PlayerController.spawnPoint Q_PROPERTY；
    //   出生点 = 世界生成第一个区块中心列 kSpawn(80,80)，玩家出生格，非全 0）。指针旋转动画留接口（用户后给）。
    //   MaterialIcon 自绘指南针图标（圆表盘 + 红黑双磁针，§9 原创）。无 MC 1.0 mcMaterialId 映射（id > 表界
    //   0x22E → -1 → 资源包回退引擎自绘；机制等价但映射段未扩，同 paper/book/lapis 近期材料段模式）。
    static constexpr int CompassId       = 0x23F; // 指南针：4 铁锭 + 1 红石合成；HUD 指针指向出生点（t567）
    // t568 钟（clock）：材料段 0x240。机制等价 MC 1.0 clock——**4 金锭十字 + 中心 1 红石合成 → 1 钟**
    //   （recipe.cpp clock 配方；机制等价 MC 1.0 clock 配方 4 gold ingot + 1 redstone 十字）。可堆叠 64；
    //   非方块（材料段）→ 右键不放置。作用：HUD 显示当前昼夜相位（时辰文字 + 昼夜小图；WorldClock.dayPhase
    //   派生，同 F3 time 行口径）。指针 / 表盘旋转动画留接口（用户后给 PNG 素材，D 接口）。MaterialIcon
    //   自绘钟图标（金框圆表盘 + 指针，§9 原创）。无 MC 1.0 mcMaterialId 映射（id > 表界 → -1 → 回退自绘）。
    static constexpr int ClockId         = 0x240; // 钟：4 金锭 + 1 红石合成；HUD 显示当前昼夜相位（t568）
    // t345 护甲段（ArmorIdBase=0x300）：5 套材质（皮革 / 铁 / 铜 / 金 / 钻石）× 4 部位（头盔 / 胸甲 / 护腿 / 靴子）= 20 件。
    //   spec t345「recipe.h（Armor ids）」—— id 段定义在此（单一权威），护甲属性（护甲值 / 耐久 / 名）由
    //   ArmorRegistry（src/Game/armor.*，同层 Game）持有。机制等价 MC 1.0 护甲系统；§9 改名（零 MC 专名）。
    //   **不可堆叠**（Hotbar::maxStackSize 对护甲段返 1，同工具段语义 —— 每件独立耐久）。与材料段（0x200..）/
    //   工具段（0x100..）三段互斥：Hotbar::isMaterial 对护甲段返 false（防误判为可堆叠材料）；图标渲染走
    //   MaterialIcon（护甲段 case，因护甲「非方块非工具 → QML 自绘」同材料段）。
    //   行序 = tier 主序 × piece 次序（皮革 4 → 铁 4 → 铜 4 → 金 4 → 钻石 4）；与 armor.cpp kArmors 表行序严格
    //   一致（static_assert 钉死）。合成产物 = 各材质锭 / 皮革（材料段）经工作台 3×3 有序配方（recipe.cpp）。
    //   皮革来源：杀牛掉落 LeatherId（0x20D，t242）→ 皮革护甲配方原料（spec「LEATHER comes from cow drops」）。
    static constexpr int ArmorIdBase      = 0x300;
    static constexpr int LeatherHelmet     = 0x300; // 皮革头盔（护甲 1 / 耐久 55）
    static constexpr int LeatherChestplate = 0x301; // 皮革胸甲（护甲 3 / 耐久 80）
    static constexpr int LeatherLeggings   = 0x302; // 皮革护腿（护甲 2 / 耐久 75）
    static constexpr int LeatherBoots      = 0x303; // 皮革靴子（护甲 1 / 耐久 65）
    static constexpr int IronHelmet        = 0x304; // 铁头盔（护甲 2 / 耐久 165）
    static constexpr int IronChestplate    = 0x305; // 铁胸甲（护甲 6 / 耐久 240）
    static constexpr int IronLeggings      = 0x306; // 铁护腿（护甲 5 / 耐久 225）
    static constexpr int IronBoots         = 0x307; // 铁靴子（护甲 2 / 耐久 195）
    static constexpr int CopperHelmet      = 0x308; // 铜头盔（护甲 2 / 耐久 110；MC 1.0 无铜护甲，自定）
    static constexpr int CopperChestplate  = 0x309; // 铜胸甲（护甲 4 / 耐久 160）
    static constexpr int CopperLeggings    = 0x30A; // 铜护腿（护甲 3 / 耐久 150）
    static constexpr int CopperBoots       = 0x30B; // 铜靴子（护甲 1 / 耐久 130）
    static constexpr int GoldHelmet        = 0x30C; // 金头盔（护甲 2 / 耐久 77）
    static constexpr int GoldChestplate    = 0x30D; // 金胸甲（护甲 5 / 耐久 112）
    static constexpr int GoldLeggings      = 0x30E; // 金护腿（护甲 3 / 耐久 105）
    static constexpr int GoldBoots         = 0x30F; // 金靴子（护甲 1 / 耐久 96）
    static constexpr int DiamondHelmet     = 0x310; // 钻石头盔（护甲 3 / 耐久 363）
    static constexpr int DiamondChestplate = 0x311; // 钻石胸甲（护甲 8 / 耐久 528）
    static constexpr int DiamondLeggings   = 0x312; // 钻石护腿（护甲 6 / 耐久 495）
    static constexpr int DiamondBoots      = 0x313; // 钻石靴子（护甲 3 / 耐久 429）
    static constexpr int ArmorCount    = 20,       // 哨兵：已定义护甲数（合法护甲 id 相对 ArmorIdBase 的上界）。
                                  ArmorIdEnd = ArmorIdBase + ArmorCount; // 0x314（护甲段上界，不含）。

    // 配方定义（每条一行；单一权威）。改配方任何属性只改 kRecipes 一行，全工程生效。
    struct Recipe {
        int gridSize;        // 2 或 3（合成台尺寸；2×2 配方亦可在 3×3 工作台合成）
        bool shapeless;      // true = 无序（多重集匹配）；false = 有序（最小包围盒逐格匹配）
        int pattern[9];      // 行优先；0=空格 / >0=原料 id；2×2 配方仅 [0,1,3,4] 有效
        int outputId;        // 产物物品 id（方块段或工具段）
        int outputCount;     // 一次合成产出数量
        int consumeCount;    // 每原料格每次合成消耗数（当前全 1；留扩展位）
        const char *name;    // 内部 / 调试用名（通用词；非面向用户）
    };

    // 在给定输入网格中找匹配配方。
    //   grid     = 行优先 id 数组（0=空格 / >0=物品 id）；
    //   gridSize = 输入合成台尺寸（2 = 背包 2×2 / 3 = 工作台 3×3）。
    // 返回首个匹配的 Recipe*（无匹配 → nullptr）。匹配规则：
    //   - 仅尝试 gridSize <= 输入尺寸的配方（2×2 配方在 3×3 输入里也能合；3×3 配方在 2×2 输入里不能）。
    //   - shapeless：输入非空格的多重集 == 配方 pattern 非空格的多重集（与位置无关）。
    //   - shaped：输入收缩到最小非空包围盒、配方 pattern（在自身 gridSize 上）收缩到最小非空包围盒；
    //     二者包围盒尺寸相同且逐格 id 相同 → 匹配（MC「最小包围盒」规则，允许图案在网格内任意平移）。
    // 空输入（全 0）→ 恒 nullptr（无配方匹配空网格）。
    static const Recipe *match(const int *grid, int gridSize);

    // 产物是否能放入光标手持栈（spec「MC 式数量」前置判定，供 UI 决定是否合成）：
    // 光标空 OR（同 id 且 heldCount + outputCount <= maxStack）→ true。maxStack 走 Hotbar::maxStackSize
    // （方块 64 / 工具 1）；此处由 caller 传 maxStack 解耦（本类不依赖 Hotbar）。
    static bool canTake(const Recipe &r, int heldId, int heldCount, int maxStack);

    // t348 引擎材料段 id → MC Java 1.0.0 物品数字 id 的**对齐映射**（资源包加载前置；与 docs/item-ids.md 材料 /
    //   mob 掉落 / 生物蛋段「MC 1.0.0」列一致）。覆盖整个材料段 [MaterialIdBase, EnchantedBookId] = 0x200..0x227
    //   （含材料 / mob 死亡掉落 / 生物蛋 / 熟肉 / 战利品表物品五子集——五者在 MC 1.0 都是「物品」，统一在 items.png）。**不重排常量**
    //   （保存档 / 配方 / 掉落表向后兼容——材料段 id 落 player_state JSON（背包）+ 配方 / BlockDef.dropId，重排会
    //   破坏旧存档与既存数据）；故用「映射层」对齐。无 MC 1.0 等价（铜 / 金原矿与锭 1.17+、铁 / 金原矿 1.0 直接掉
    //   矿石方块）→ -1（资源包回退引擎自绘 MaterialIcon）。**生物蛋**：MC 1.0 是单一 id 383（spawn egg）+ metadata
    //   分 mob 变体，故引擎全部 spawn_egg（猪 / 牛 / 羊 / 蹒跚者 / 骸骨 / 潜行者 / 蜘蛛）→ 383。越界 → -1。
    static int mcMaterialId(int engineMaterialId);

private:
    RecipeRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // RECIPE_H
