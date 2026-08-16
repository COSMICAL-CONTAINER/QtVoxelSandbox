#include "hotbar.h"
#include "resourcepackmanager.h" // t456 pack item 图标覆盖（blockItemIconSource 静态查询）

#include <QDebug>
#include <QRandomGenerator> // t476 耐久附魔减损耗概率掷骰
#include <QStringList>
#include <algorithm>

namespace {
// 方块 id → 等距立方体图标文件名（顶 + 两侧，统一尺寸），由 tools/build_cube_icons.py
// 从 textures/ 下既有面贴图烘焙而成（复用图集 tile 合成，非 MC 资产；PLAN §2-L）。
// 用立方体而非单面平面贴图：源贴图尺寸不一（16/48 混排）→ 单面图会大小不统一；
// 立方体图标统一画布尺寸 + 顶/侧明暗强化可辨性（grass 顶绿侧褐、log 顶年轮侧树皮…）。
// air / 未知 → 返回 nullptr（无图标）。工具段 id（>=0x100，t33）暂无图标 → 返回 nullptr。
const char *iconFileForBlock(quint8 id)
{
    switch (id) {
    case BlockRegistry::Grass:         return "icon_grass.png";
    case BlockRegistry::Dirt:          return "icon_dirt.png";
    case BlockRegistry::Stone:         return "icon_stone.png";
    case BlockRegistry::Cobble:        return "icon_cobble.png";
    case BlockRegistry::Log:           return "icon_log.png";
    case BlockRegistry::Planks:        return "icon_planks.png";
    case BlockRegistry::Leaves:        return "icon_leaves.png";
    case BlockRegistry::Sand:          return "icon_sand.png";
    case BlockRegistry::CraftingTable: return "icon_crafting_table.png"; // t50/t492 工作台立方体图标（正面为主投影，显正面网格）
    case BlockRegistry::Furnace:       return "icon_furnace.png";        // t80/t492 熔炉立方体图标（正面为主投影，显炉口）
    case BlockRegistry::CoalOre:       return "icon_coal_ore.png";       // t84 煤矿石立方体图标
    case BlockRegistry::IronOre:       return "icon_iron_ore.png";       // t84 铁矿石立方体图标
    case BlockRegistry::DiamondOre:    return "icon_diamond_ore.png";    // t279 钻矿石立方体图标（石头底+青白菱斑晶体）
    // misc 二轮：基岩立方体图标（深灰粗糙岩石纹理；build_cube_icons.py 程序生成原创贴图）。机制等价 MC 创造可取
    //   bedrock 物品（生存不可破，创造可放/取）。default_bedrock.png 是方块面贴图（tile 18），各面同 → 立方体顶+两侧明暗。
    case BlockRegistry::Bedrock:       return "icon_bedrock.png";
    case BlockRegistry::CopperOre:     return "icon_copper_ore.png";     // t308 铜矿石立方体图标（石头底+橙铜斑+孔雀绿锈）
    case BlockRegistry::GoldOre:       return "icon_gold_ore.png";       // t308 金矿石立方体图标（石头底+金黄斑簇）
    case BlockRegistry::LapisOre:      return "icon_lapis_ore.png";      // t471 青金矿石立方体图标（石头底+群青深蓝斑簇+黄铁矿金点）
    case BlockRegistry::RedstoneOre:   return "icon_redstone_ore.png";   // t569 红石矿石立方体图标（石头底+鲜红菱斑矿粒；走过/挖掘点亮微弱红光）
    case BlockRegistry::Torch:         return "icon_torch.png";          // t88 火把立方体图标（伪光源）
    case BlockRegistry::Chest:         return "icon_chest.png";          // t173 箱子立方体图标（顶盖缝+侧铁箍）
    case BlockRegistry::Farmland:      return "icon_farmland.png";       // t234 耕地立方体图标（顶=干态翻耕土+侧泥土）
    case BlockRegistry::Wool:          return "icon_wool.png";           // t300 羊毛立方体图标（奶白绒毛）
    // t455 16 色 wool 其余 15 色变体立方体图标（彩色卷绒纹；build_cube_icons.py 程序生成）。
    case BlockRegistry::WoolOrange:     return "icon_wool_orange.png";     // 橙色羊毛
    case BlockRegistry::WoolMagenta:    return "icon_wool_magenta.png";    // 品红色羊毛
    case BlockRegistry::WoolLightBlue:  return "icon_wool_light_blue.png"; // 浅蓝色羊毛
    case BlockRegistry::WoolYellow:     return "icon_wool_yellow.png";     // 黄色羊毛
    case BlockRegistry::WoolLime:       return "icon_wool_lime.png";       // 柠绿色羊毛
    case BlockRegistry::WoolPink:       return "icon_wool_pink.png";       // 粉红色羊毛
    case BlockRegistry::WoolGray:       return "icon_wool_gray.png";       // 灰色羊毛
    case BlockRegistry::WoolLightGray:  return "icon_wool_light_gray.png"; // 浅灰色羊毛
    case BlockRegistry::WoolCyan:       return "icon_wool_cyan.png";       // 青色羊毛
    case BlockRegistry::WoolPurple:     return "icon_wool_purple.png";     // 紫色羊毛
    case BlockRegistry::WoolBlue:       return "icon_wool_blue.png";       // 蓝色羊毛
    case BlockRegistry::WoolBrown:      return "icon_wool_brown.png";      // 棕色羊毛
    case BlockRegistry::WoolGreen:      return "icon_wool_green.png";      // 绿色羊毛
    case BlockRegistry::WoolRed:        return "icon_wool_red.png";        // 红色羊毛
    case BlockRegistry::WoolBlack:      return "icon_wool_black.png";      // 黑色羊毛
    case BlockRegistry::Sandstone:     return "icon_sandstone.png";      // t394 砂岩立方体图标（顶=压实沙面 / 侧=层理带）
    case BlockRegistry::CutSandstone:  return "icon_cut_sandstone.png"; // t485 切制砂岩立方体图标（各面=暖沙色+内陷矩形装饰边框；金字塔外框装饰变体）
    case BlockRegistry::TntBlock:      return "icon_tnt.png";           // t485 TNT 立方体图标（各面=深红药柱+横向捆带+亮黄标识；沙漠神殿陷阱方块）
    case BlockRegistry::MossyCobble:   return "icon_mossy_cobble.png";  // t486 苔石立方体图标（各面=圆石灰底+暗绿苔藓斑簇；丛林神殿主体）
    case BlockRegistry::Dispenser:     return "icon_dispenser.png";     // t486/t492 发射器立方体图标（正面为主投影，显排出口面板；丛林神殿陷阱机关）
    case BlockRegistry::Dropper:       return "icon_dropper.png";       // t609 投掷器立方体图标（正面为主投影，显小排出口；顶/侧=熔炉石质）
    // t487 要塞结构方块图标：石砖（立方体 3D）/ 石砖台阶（3D 半高盒）/ 石砖楼梯（3D L 阶）。t600 修正：台阶/楼梯
    //   原误走 BLOCKS 满立方投影（三图标同图=全显石砖整块，用户「背包三个都是石砖满一格」）→ 改 render_partial_3d
    //   slab/stairs 形状（同圆石变体流程，fill=default_stone_brick 砖纹）。tools/build_cube_icons.py 程序生成。
    case BlockRegistry::StoneBrick:       return "icon_stone_brick.png";       // 石砖：3D 立方体（顶+两侧砖纹）
    case BlockRegistry::StoneBrickSlab:   return "icon_stone_brick_slab.png";  // 石砖台阶：3D 半高盒（砖纹）
    case BlockRegistry::StoneBrickStairs: return "icon_stone_brick_stairs.png";// 石砖楼梯：3D L 阶（背墙 + 整步，砖纹）
    case BlockRegistry::Cactus:        return "icon_cactus.png";         // t394 仙人掌立方体图标（顶=绿截面环纹 / 侧=棱脊+刺点）
    case BlockRegistry::SnowLayer:    return "icon_snow_layer.png";    // t395 积雪层立方体图标（各面=冷白冰晶噪点）
    case BlockRegistry::SpruceLog:    return "icon_spruce_log.png";    // t395 云杉原木立方体图标（顶=年轮截面 / 侧=深棕树皮）
    case BlockRegistry::Ice:          return "icon_ice.png";            // t468 冰立方体图标（各面=浅蓝反光裂纹）
    case BlockRegistry::PackIce:      return "icon_pack_ice.png";       // t468 浮冰立方体图标（各面=实白细裂纹）
    case BlockRegistry::BlueIce:      return "icon_blue_ice.png";       // t468 蓝冰立方体图标（各面=淡蓝纵向纹路）
    case BlockRegistry::Obsidian:     return "icon_obsidian.png";       // t472 黑曜石立方体图标（各面=深紫黑火山玻璃；流体交互产物，钻石镐采掘）
    case BlockRegistry::EnchantingTable: return "icon_enchanting_table.png"; // t474 附魔台立方体图标（顶=黑曜石+钻石+立书 / 侧=黑曜石+钻石嵌点）
    case BlockRegistry::Bookshelf:    return "icon_bookshelf.png";      // t474 书架立方体图标（各面=木板边框+彩色书脊书列）
    case BlockRegistry::IronBlock:    return "icon_iron_block.png";     // t477 铁块立方体图标（各面=金属灰底+铆钉网格+高光）
    // t620 矿物存储块立方体图标（各面=对应材质存储块贴图；build_cube_icons.py 程序生成）。
    case BlockRegistry::CoalBlock:    return "icon_coal_block.png";     // 煤炭块（各面=近黑煤层压缩块+高光棱线）
    case BlockRegistry::LapisBlock:   return "icon_lapis_block.png";    // 青金石块（各面=深群青底+金点镶面）
    case BlockRegistry::DiamondBlock: return "icon_diamond_block.png";  // 钻石块（各面=浅青底+钻石菱面镶格）
    case BlockRegistry::GoldBlock:    return "icon_gold_block.png";     // 金块（各面=金黄底+镶格高光）
    case BlockRegistry::RedstoneBlock: return "icon_redstone_block.png"; // 红石块（各面=鲜红底+矿粒镶面）
    case BlockRegistry::RedstoneLamp: return "icon_redstone_lamp.png";  // 红石灯（各面=灰暗壳+中央红石芯（off 态））
    case BlockRegistry::Anvil:        return "icon_anvil.png";          // t477 铁砧立方体图标（顶=砧台+砧面+尖角 / 侧=深铁砧身）
    case BlockRegistry::AnvilChipped: return "icon_anvil_chipped.png";  // t477 微损铁砧立方体图标（顶=砧台+细裂纹）
    case BlockRegistry::AnvilDamaged: return "icon_anvil_damaged.png";  // t477 重损铁砧立方体图标（顶=砧台+粗裂纹网+缺角）
    // t387 床方块 8 色变体立方体图标（彩色被面 + 枕垫亮带 + 绗缝针脚；build_cube_icons.py 程序生成）。
    case BlockRegistry::BedRed:        return "icon_bed_red.png";        // 红床（配方产物默认色）
    case BlockRegistry::BedOrange:     return "icon_bed_orange.png";     // 橙床
    case BlockRegistry::BedYellow:     return "icon_bed_yellow.png";     // 黄床
    case BlockRegistry::BedGreen:      return "icon_bed_green.png";      // 绿床
    case BlockRegistry::BedCyan:       return "icon_bed_cyan.png";       // 青床
    case BlockRegistry::BedBlue:       return "icon_bed_blue.png";       // 蓝床
    case BlockRegistry::BedMagenta:    return "icon_bed_magenta.png";    // 品红床
    case BlockRegistry::BedBlack:      return "icon_bed_black.png";      // 黑床
    // t455 床方块补齐 8 色新变体立方体图标（white/light_blue/lime/pink/gray/light_gray/purple/brown；
    //   build_cube_icons.py 程序生成；与同色羊毛视觉一致）。
    case BlockRegistry::BedWhite:      return "icon_bed_white.png";      // 白床
    case BlockRegistry::BedLightBlue:  return "icon_bed_light_blue.png"; // 浅蓝床
    case BlockRegistry::BedLime:       return "icon_bed_lime.png";       // 柠绿床
    case BlockRegistry::BedPink:       return "icon_bed_pink.png";       // 粉红床
    case BlockRegistry::BedGray:       return "icon_bed_gray.png";       // 灰床
    case BlockRegistry::BedLightGray:  return "icon_bed_light_gray.png"; // 浅灰床
    case BlockRegistry::BedPurple:     return "icon_bed_purple.png";     // 紫床
    case BlockRegistry::BedBrown:      return "icon_bed_brown.png";      // 棕床
    // t244 cross 广告牌方块图标：草丛 / 小麦作物在世界内是 cross 形广告牌（透明底 + 像素草叶 / 麦穗），
    //   图标走 flat 2D 平面路径（同火把 icon_torch）—— build_cube_icons.py 的 render_flat_2d 直接放大源
    //   贴图保留 alpha → 「纯草叶 / 麦穗无方块底」。小麦作物图标取成熟阶段 7（金黄麦穗），肉眼一眼可辨。
    case BlockRegistry::TallGrass:     return "icon_tall_grass.png";     // t235 草丛（cross 透明底；绿草叶）
    case BlockRegistry::WheatCrop:     return "icon_wheat_crop.png";     // t236 小麦作物（cross 透明底；取成熟阶段 7 麦穗）
    case BlockRegistry::DeadBush:      return "icon_dead_bush.png";      // t394 枯死的灌木（cross 透明底；棕褐干枝；沙漠装饰）
    case BlockRegistry::LilyPad:       return "icon_lily_pad.png";       // t396 睡莲（cross 路由横向浮叶；透明底 + 绿圆叶 + V 缺口）
    case BlockRegistry::Mushroom:      return "icon_mushroom.png";       // t396 蘑菇（cross 透明底；米色菌柄 + 红底白斑菌盖）
    case BlockRegistry::BrownMushroom: return "icon_brown_mushroom.png"; // t507 白蘑菇 / 棕蘑菇（cross 透明底；米色菌柄 + 棕色菌盖白斑）
    // t397 花 4 色变体 + 甘蔗（cross 透明底；flat 2D 图标，build_cube_icons.py render_flat_2d 放大源贴图保留 alpha）。
    case BlockRegistry::FlowerRed:     return "icon_flower_red.png";     // 红花（cross 透明底；绿茎 + 红花头）
    case BlockRegistry::FlowerYellow:  return "icon_flower_yellow.png";  // 黄花（cross 透明底；绿茎 + 黄花头）
    case BlockRegistry::FlowerBlue:    return "icon_flower_blue.png";    // 蓝花（cross 透明底；绿茎 + 蓝花头）
    case BlockRegistry::FlowerWhite:   return "icon_flower_white.png";   // 白花（cross 透明底；绿茎 + 白花头）
    case BlockRegistry::Sugarcane:     return "icon_sugarcane.png";      // 甘蔗（cross 透明底；绿色节段细茎 + 顶部尖叶）
    // t145/t163(d) 不完整方块图标：6 类木制半方块各走自己的区分图标（tools/build_cube_icons.py 程序生成）。
    //   t163(d) slab/stairs/trapdoor/pressure_plate 升级为 **3D dimetric 立体图标**（render_partial_3d 按
    //   实际形状投影：slab 半高 / stairs L 阶 / trapdoor 薄板 / pressure_plate 更薄更小，顶 + 两侧明暗同
    //   完整方块 cube icon）；door/fence 保留 flat 2D 剪影（高板 / 柱档剪影更直观）。6 类同为木板材质 →
    //   共享木纹观感，但形状各异 → hotbar / 创造调色板肉眼即可辨图，不依赖 displayName 区分。
    case BlockRegistry::WoodSlab:          return "icon_wood_slab.png";          // 木板台阶：3D 半高盒
    case BlockRegistry::WoodStairs:        return "icon_wood_stairs.png";        // 木板楼梯：3D L 阶（背墙 + 整步）
    case BlockRegistry::WoodFence:         return "icon_wood_fence.png";         // 木栅栏：2D 柱档剪影
    case BlockRegistry::WoodDoor:          return "icon_wood_door.png";          // 木板门：2D 高板剪影
    case BlockRegistry::WoodTrapdoor:      return "icon_wood_trapdoor.png";      // 木活板门：3D 薄板
    case BlockRegistry::WoodPressurePlate: return "icon_wood_pressure_plate.png";// 木板压力板：3D 更薄更小
    // t412 圆石变体图标：石质半方块 3D dimetric 立体图标（圆石贴图按实际形状投影，同木制半方块图标流程）。
    case BlockRegistry::CobbleSlab:          return "icon_cobble_slab.png";          // 圆石台阶：3D 半高盒
    case BlockRegistry::CobbleStairs:        return "icon_cobble_stairs.png";        // 圆石楼梯：3D L 阶（背墙 + 整步）
    case BlockRegistry::CobbleFence:         return "icon_cobble_fence.png";         // 圆石墙：3D 立柱 + 横档
    case BlockRegistry::CobblePressurePlate: return "icon_cobble_pressure_plate.png";// 圆石压力板：3D 更薄更小
    // t466 云杉木制品链图标：云杉木板（立方体 3D）/ 云杉台阶（3D 半高盒）/ 云杉栅栏（3D 立柱+横档）/ 云杉门
    //   （3D 两格高薄板）。同橡木木制品图标流程，仅 fill 换 default_spruce_planks（深色木纹）。tools/build_cube_icons.py
    //   程序生成；与橡木木制品图标形状一致但贴图深色 → 肉眼即可辨「云杉」。
    case BlockRegistry::SprucePlanks:        return "icon_spruce_planks.png";        // 云杉木板：3D 立方体（顶+两侧深色木纹）
    case BlockRegistry::SpruceSlab:          return "icon_spruce_slab.png";          // 云杉台阶：3D 半高盒（深色木纹）
    case BlockRegistry::SpruceFence:         return "icon_spruce_fence.png";         // 云杉栅栏：3D 立柱 + 横档（深色木纹）
    case BlockRegistry::SpruceDoor:          return "icon_spruce_door.png";          // 云杉门：3D 两格高薄板（深色木纹）
    case BlockRegistry::Ladder:        return "icon_ladder.png";      // t413/t519 木梯（透明底；两纵轨 + 4 道横梯级；竖直爬行梯；t519 满格版）
    // t482/t483 防御造物方块立方体图标（build_cube_icons.py 程序生成原创像素图）。
    case BlockRegistry::Pumpkin:       return "icon_pumpkin.png";     // 南瓜（顶=橙色瓜顶+短茎 / 侧=橙色瓜棱；造物头部方块）
    case BlockRegistry::Snow:          return "icon_snow.png";        // 雪块（各面=冷白冰晶噪点，同积雪层；雪傀儡身体方块）
    // t484 废弃矿井结构方块图标（build_cube_icons.py flat 2D 透明底；程序生成原创像素图）。
    case BlockRegistry::Cobweb:        return "icon_cobweb.png";      // 蜘蛛网（cross 透明底；灰白蛛丝放射网纹；矿井散布）
    case BlockRegistry::Rail:          return "icon_rail.png";        // 铁轨（flat 透明底；棕色枕木 + 灰铁双轨；贴地薄板）
    default: return nullptr; // air / 未知 / 工具段：无图标（t33 落地工具图标时扩展）
    }
}

// 物品 id 段：方块段 0..BlockRegistry::Count-1；工具段 id>=0x100（t33，不可堆叠）；
// 材料段 id>=0x200（t50 合成产物：木棒等，可堆叠 64）。
constexpr int kToolIdBase     = 0x100;
constexpr int kMaterialIdBase = 0x200; // 与 RecipeRegistry::MaterialIdBase 同源（t50）
// 方块单栈上限走 BlockRegistry::BlockDef.maxStack（t42 单一权威；MC 1.0 方块标准 64）。
// 创造风格默认填充直接读 maxStack(id)，不再本地硬编码 64（改表即同步）。

// id 合法性：air(0) / 方块段 (0,Count) / 工具段 (>=0x100) / 材料段 (>=0x200) / 护甲段 (>=0x300)。越段 id 一律拒
// （防 quint8 截断别名）。护甲段 t345 新增（>= ArmorIdBase，经 >= kToolIdBase 隐式通过；显式注释钉死语义）。
bool isValidItemId(int id)
{
    return id == 0 || (id > 0 && id < int(BlockRegistry::Count)) || id >= kToolIdBase;
}

// t263 把「写入时传入的 durability」归一为合法的工具 / 护甲实例耐久。
//   - 非工具 / 非护甲 id（含 air / 方块 / 材料）→ 恒 0（inert；不带耐久）。
//   - 工具 / 护甲 id + durability<=0（缺省 -1 / ItemStack 默认 0 = 未初始化）→ maxDurability（新实例满耐久）。
//   - 工具 / 护甲 id + durability>0（显式保真）→ clamp 到 (0, maxDurability]（搬运 / 拾取时槽内旧耐久）。
// t448：0 不再视作「1 次耐久」。ItemStack 的 durability 默认值为 0（=「未显式赋值的新工具」），旧版把它归一为 1
//   会导致「用一次就消失」——任何写入路径（合成 / 取件 / 存档 round-trip / 旧档迁移 / 拾取兜底）意外落 0 时，
//   工具实例被当成仅剩 1 点耐久，首次锄地 / 挖掘 / 攻击即触发 damageSelectedItem 归零清槽 → 工具凭空消失。
//   0 与缺省 -1 同义（未初始化 → 新实例满耐久）。耐久真正归零的实例本就不应再进槽——破损在
//   damageSelectedItem / damageArmor 内清槽（写空栈），故「0 耐久实例进槽」只可能是 bug / 脏数据，按新工具满耐久
//   兜底远比「1 次耐久」安全（机制等价 MC「新工具满耐久」，杜绝一次性工具）。
// t345：护甲走 ArmorRegistry::maxDurability（同工具语义；护甲受击 -1，归零破损）。
// 调用者：setStack / mainSetStack / addStack / mainAddStack / addToAny / armorSetStack（统一入口，防各处散写漏归一）。
int normalizeDurability(int id, int durability)
{
    const bool isTool = ToolRegistry::isTool(id);
    const bool isArm  = ArmorRegistry::isArmor(id);
    if (!isTool && !isArm) return 0;
    const int cap = isTool ? ToolRegistry::maxDurability(id) : ArmorRegistry::maxDurability(id);
    if (cap <= 0) return 0; // 防御：表未配耐久（不应发生；maxDurability 对工具 / 护甲恒 >0）
    if (durability <= 0) return cap;    // <=0（缺省 -1 / 默认 0=未初始化）→ 新实例满耐久（t448：杜绝 0→1「用一次就消失」）
    if (durability > cap) return cap;   // 超 max 钳到 max
    return durability;                   // (0, cap] 显式保真（搬运 / 拾取实例耐久）
}

// t475 ItemStack.enchants[4] 与 QVariantList<int>(4) 边界互转 + 比较。QML 边界（InventoryOps / 附魔台）走
//   QVariantList<int> 4 元素，每元素 = EnchantRegistry::pack 值（(id<<8)|level；0 = 空槽）；C++ 内部走 int[4]。
//   列表不足 4 元素按 0 补齐（空附魔）；越界截断（防御）。非可附魔物品的 enchants 恒全 0（inert，写入时 caller
//   传空 list → applyEnchants 清零；读出时 readEnchants 返 4 个 0）。
void applyEnchants(ItemStack &s, const QVariantList &enchants)
{
    for (int i = 0; i < 4; ++i) {
        s.enchants[i] = (i < enchants.size()) ? enchants.at(i).toInt() : 0;
    }
}
QVariantList readEnchants(const ItemStack &s)
{
    return { s.enchants[0], s.enchants[1], s.enchants[2], s.enchants[3] };
}
bool enchantsEqual(const ItemStack &a, const ItemStack &b)
{
    for (int i = 0; i < 4; ++i) if (a.enchants[i] != b.enchants[i]) return false;
    return true;
}
} // namespace

Hotbar::Hotbar(QObject *parent)
    : QObject(parent)
    // t49：构造期 9 槽全空（创造物品改由调色板点取到光标→放入 hotbar 槽才有；spec point 2「初始全空」）。
    , m_slots(9, ItemStack{0, 0})
    // t97：构造期 27 主栏槽全空（生存空背包起；三菜单共享同一份 VM 数据）。
    , m_mainSlots(27, ItemStack{0, 0})
    // t345：构造期 4 护甲槽全空（玩家初始无装备）。
    , m_armorSlots(4, ItemStack{0, 0})
{
}

void Hotbar::bumpRevision()
{
    ++m_slotRevision;
    emit slotsChanged();
}

// t97：主栏版本号 bump（同 bumpRevision 的主栏版）。mainSetStack / mainAddStack / addToAny 的 main 分支 /
// resetForMode 调 → 三菜单 delegate 触碰 mainRevision 的绑定重算。
void Hotbar::bumpMainRevision()
{
    ++m_mainRevision;
    emit mainSlotsChanged();
}

// t345：护甲版本号 bump（同 bumpRevision 的护甲版）。armorSetStack / damageArmor / resetForMode 调 →
// SurvivalInventory 装备栏 delegate + Main.qml 护甲条 / 减伤绑定重算（totalArmorPoints 是派生值，经此刷新）。
void Hotbar::bumpArmorRevision()
{
    ++m_armorRevision;
    emit armorSlotsChanged();
}

void Hotbar::setSelectedSlot(int slot)
{
    const int n = int(m_slots.size());
    if (n == 0) return;
    if (slot < 0) slot = 0;        // 数字键 / QML 直选：clamp 到合法区间
    if (slot >= n) slot = n - 1;
    if (slot == m_selectedSlot) return;
    m_selectedSlot = slot;
    emit selectedSlotChanged(); // selectedBlockId 从新选中栈派生，随之刷新
}

// 选中栈 id（空栈 / 工具栈 / 材料栈 / 桶→Air）。player.selectedBlock 绑它 → 非方块段槽位右键不放置
// （playercontroller 守 Air；桶走 m_selectedItem 的 useBlock 分支，不走 selectedBlock 放置路径）。
// 工具非方块、不可放置（t33）：选中工具槽时 selectedBlockId 返 Air，同时 HUD 的 player.selectedBlock
// 显 #0（无可放置方块），与「工具用于挖掘、非放置」语义一致。t174：材料段（木棒/煤/铁锭/桶等 id>=Count）
// 亦非方块 → 返 Air（旧行为返回原始 id 会被 BlockCube 截断成 quint8 渲染出垃圾方块，如选中木棒显木板立方）。
int Hotbar::selectedBlockId() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size()))
        return int(BlockRegistry::Air);
    const int id = m_slots[size_t(m_selectedSlot)].id;
    // 仅方块段（0,Count）才是可放置方块；工具 / 材料 / 桶（id>=Count）→ Air（非方块，不可放置）。
    if (id > 0 && id < int(BlockRegistry::Count)) return id;
    return int(BlockRegistry::Air);
}

// 选中栈的**原始** id（t34 工具感知挖掘用）：含工具段，不归一 Air。空栈 / 越界 → 0（=Air，
// ToolRegistry 视作空手挖 → speedMul=1、不掉需工具方块）。player.selectedItem 绑它。
int Hotbar::selectedItemId() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size()))
        return 0;
    return m_slots[size_t(m_selectedSlot)].id;
}

// ── 工具段桥接（t33）── 查 ToolRegistry 单一权威，QML delegate 据此选方块 Image vs ToolIcon Canvas。
bool Hotbar::isTool(int itemId) const { return ToolRegistry::isTool(itemId); }

// ── 材料段判定（t50 / t345）── id >= RecipeRegistry::MaterialIdBase（0x200）。**含护甲段**（0x300..）：
//   isMaterial 在全工程是「非方块非工具 → QML 自绘 MaterialIcon」的**渲染路由谓词**（40+ 处 delegate 据它切
//   方块 Image vs MaterialIcon），护甲同属「非方块非工具」→ 走 MaterialIcon（MaterialIcon.qml 含护甲段分支）。
//   护甲与可堆叠材料的**功能**差异（maxStack=1 / 独立耐久 / 独立名）由各自入口的 isArmor 先行特判保证
//   （maxStackSize / nameForBlock / normalizeDurability 均先查 isArmor），不依赖 isMaterial 排除护甲。
//   故保持单边 >= 简单形式 → 40+ delegate 零改动即自动渲染护甲。
bool Hotbar::isMaterial(int itemId) const
{
    return itemId >= RecipeRegistry::MaterialIdBase;
}

// ── t345 护甲段判定 / 属性桥接（透传 ArmorRegistry；QML delegate + 装备槽校验用）──
bool Hotbar::isArmor(int itemId) const { return ArmorRegistry::isArmor(itemId); }
int Hotbar::armorPiece(int itemId) const { return ArmorRegistry::piece(itemId); }
int Hotbar::armorTier(int itemId) const { return ArmorRegistry::tier(itemId); }
int Hotbar::armorPointsFor(int itemId) const { return ArmorRegistry::armorPoints(itemId); }
int Hotbar::armorMaxDurability(int itemId) const { return ArmorRegistry::maxDurability(itemId); }

// t219 不完整方块段判定（手持 / 掉落贴图分流用）：异形方块在世界内非整立方 → 手持 / 掉落走 dimetric 立体
//   图标（icon_wood_*.png / icon_cobble_*.png，BillboardQuad），非 BlockCube 满格立方。t412 走 BlockRegistry
//   单一权威谓词（[FirstPartial,LastPartial] ∪ 段外圆石变体），避免 QML 与 mesher 路由漂移。
bool Hotbar::isPartialBlock(int itemId) const
{
    if (itemId <= 0 || itemId >= int(BlockRegistry::Count)) return false;
    return BlockRegistry::isPartialBlock(quint8(itemId));
}

// t440 cross 广告牌方块段判定（手持 / 掉落渲染分流用）：cross 植物（草丛 / 作物 / 树苗 / 花 / 蘑菇 / 睡莲 /
//   甘蔗 / 枯灌木 / 木梯）在世界内是双面对角 cross 广告牌（非整立方），手持 / 掉落须走 flat 2D 图标
//   BillboardQuad（icon_*.png + alphaCutoff discard 透明底），非 BlockCube 满格立方——cross 贴图透明底，
//   BlockCube 材质无 alpha 处理会把透明底当不透明 → 渲成黑底方块（用户实测「火把/花/蘑菇/睡莲手持+掉落黑底」）。
//   走 BlockRegistry::isCrossBillboard 单一权威（同 mesher cross 段路由谓词），避免 QML 与 mesher 路由漂移。
//   火把（id 13）在 QML 有独立分支（mesher 火把亦非 cross 段），故不含；供其余 cross 方块统一路由进 flat billboard。
bool Hotbar::isCrossBlock(int itemId) const
{
    if (itemId <= 0 || itemId >= int(BlockRegistry::Count)) return false;
    return BlockRegistry::isCrossBillboard(quint8(itemId));
}

// t496 二轮复盘 床方块段判定（手持 / 掉落渲染分流用）：床（ShapeBed 低 3D 模型）在世界内是双格横置异形
//   （非整立方），手持 / 掉落走 bed 图标 BillboardQuad（icon_bed_<color>.png），非 BlockCube 满格立方。
//   走 BlockRegistry::isBed 单一权威（同 mesher bed 段路由谓词），避免 QML 与 mesher 路由漂移。
bool Hotbar::isBed(int itemId) const
{
    if (itemId <= 0 || itemId >= int(BlockRegistry::Count)) return false;
    return BlockRegistry::isBed(quint8(itemId));
}

int Hotbar::toolTier(int itemId) const
{
    const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId);
    return t ? t->tier : 0; // 非工具 → 0（ToolIcon 兜底木镐配色）
}

// 工具类型（t233 锄加入后，QML 据 type 选镐 vs 锄的 3D 几何 / Canvas 像素图）。
//   返回 BlockRegistry::ToolType（Pickaxe=1 / Hoe=2）；非工具 → 0。
int Hotbar::toolType(int itemId) const
{
    const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId);
    return t ? t->type : 0;
}

QVariantList Hotbar::creativeTools() const
{
    // 创造调色板工具（无限源：拾取时 heldCount=1，工具不可堆叠）。t264 完整工具集：5 类（镐 / 锄 / 斧 / 铲 / 剑）
    //   × 6 档（木 / 石 / 铜 / 铁 / 金 / 钻石），机制等价 MC 1.0 工具集。按「同类分档」分组排列，肉眼易辨。
    //   t587 展示序修正（用户「等级依次是 木头→石头→铜→铁→金→钻石；铜现在排在钻石和金的后面」）：铜
    //   的挖掘定位是「介石 / 铁之间」（harvestLevel=2 同石级），展示位应紧跟石头；tier 数值（铜 6）只驱动
    //   speedMul / 配色（内部记账），与展示序解耦 —— 与铁砧修复材料 / 合成链的「石→铜→铁」递进一致。
    //   t589 钻石补全：斧 / 铲 / 剑 / 锄四件入列（各组钻石位补齐；弓 / 剪刀 / 钓竿三功能件仍排末尾）。
    return {int(ToolRegistry::PickaxeWood),  int(ToolRegistry::PickaxeStone),  int(ToolRegistry::CopperPickaxe), int(ToolRegistry::PickaxeIron), int(ToolRegistry::GoldPickaxe), int(ToolRegistry::PickaxeDiamond),
            int(ToolRegistry::HoeWood),      int(ToolRegistry::HoeStone),      int(ToolRegistry::CopperHoe),     int(ToolRegistry::HoeIron),     int(ToolRegistry::GoldHoe),     int(ToolRegistry::DiamondHoe),
            int(ToolRegistry::AxeWood),      int(ToolRegistry::AxeStone),      int(ToolRegistry::CopperAxe),     int(ToolRegistry::AxeIron),     int(ToolRegistry::GoldAxe),     int(ToolRegistry::DiamondAxe),
            int(ToolRegistry::ShovelWood),   int(ToolRegistry::ShovelStone),   int(ToolRegistry::CopperShovel),  int(ToolRegistry::ShovelIron),  int(ToolRegistry::GoldShovel),  int(ToolRegistry::DiamondShovel),
            int(ToolRegistry::SwordWood),    int(ToolRegistry::SwordStone),    int(ToolRegistry::CopperSword),   int(ToolRegistry::SwordIron),   int(ToolRegistry::GoldSword),   int(ToolRegistry::DiamondSword),
            // t304 弓（远程武器）：归工具段（maxStack=1，有耐久 384），故入 creativeTools（非 creativeMaterials）。
            //   拾取即满耐庋新弓；创造射箭不消耗耐久 / 箭（但仍需背包有箭才射得出，spec「需箭在背包」）。
            int(ToolRegistry::Bow),
            // t300 剪刀（功能性工具）：归工具段（maxStack=1，耐久 238），故入 creativeTools。拾取即满耐久新剪刀；
            //   创造剪羊毛不消耗耐久（同弓 / 镐 创造无限源）。ToolIcon 据 toolType===Shears 自绘剪刀图标。
            int(ToolRegistry::Shears),
            // t401 钓鱼竿（功能性工具）：归工具段（maxStack=1，耐久 64），故入 creativeTools。拾取即满耐久新钓竿；
            //   创造钓鱼不消耗耐久（同弓 / 剪刀 创造无限源）。ToolIcon 据 toolType===FishingRod 自绘钓竿图标。
            int(ToolRegistry::FishingRod)};
}

// 创造调色板材料段（t114）：木棒 / 煤炭 / 木炭 / 铁原矿 / 铁锭 / 玻璃（材料段 id >= 0x200，
// RecipeRegistry::*Id 命名常量；与 hotbar.cpp 材料段判定 / MaterialIcon 自绘图标同源）。
// 无限源（拾取时满栈 64；创造不耗）。非方块 → 右键不放置（playercontroller selectedBlock 守 Air），
// 与工具段同属「调色板可取、世界不可放」的非方块物品段。
// t174：追加空铁桶 / 装水铁桶（材料段 0x206/0x207；maxStack=1 不可堆叠 —— creativeMaterials 取件时
//   heldCount=1，同工具段语义）。桶走 useBlock（右键舀 / 倒水），不走放置路径。
QVariantList Hotbar::creativeMaterials() const
{
    return {
        int(RecipeRegistry::StickId),       // 木棒
        int(RecipeRegistry::CoalId),        // 煤炭
        int(RecipeRegistry::CharcoalId),    // 木炭
        int(RecipeRegistry::IronOreDropId), // 铁原矿
        int(RecipeRegistry::IronIngotId),   // 铁锭
        int(RecipeRegistry::GlassId),       // 玻璃
        int(RecipeRegistry::BucketEmptyId), // t174 铁桶（空）
        int(RecipeRegistry::WaterBucketId), // t174 装水铁桶
        int(RecipeRegistry::LavaBucketId),  // t351 装岩浆铁桶（创造调色板补全：平行装水铁桶；右键放岩浆源）
        int(RecipeRegistry::SeedId),        // t235 小麦种子（挖草丛得；种植 → 小麦作物 t236）
        int(RecipeRegistry::WheatId),       // t237 小麦物品（收割成熟作物得；面包原料）
        int(RecipeRegistry::BreadId),       // t238 面包（3 小麦合成；右键食 +5 饥饿）
        // t243 生物蛋（创造模式物品；右键地面 → 生成对应被动生物）。机制等价 MC 1.0 spawn egg；maxStack=64
        //   （走材料段默认），拾取时满栈 64；创造不耗 → 无限生成。MaterialIcon 自绘蛋形 + mob 配色斑点。
        int(RecipeRegistry::SpawnEggPigId),   // 生物蛋（猪）
        int(RecipeRegistry::SpawnEggCowId),   // 生物蛋（牛）
        int(RecipeRegistry::SpawnEggSheepId), // 生物蛋（羊）
        // t287 敌对生物蛋（创造模式物品；右键地面 → 生成敌对生物 Shambler/Bones/Stalker）。
        //   机制等价 MC 1.0 僵尸/骷髅/苦力怕 spawn egg；§9 改名。Spider 蛋留待 t285。
        int(RecipeRegistry::SpawnEggShamblerId), // 生物蛋（蹒跚者）
        int(RecipeRegistry::SpawnEggBonesId),    // 生物蛋（骸骨）
        int(RecipeRegistry::SpawnEggStalkerId),  // 生物蛋（潜行者）
        int(RecipeRegistry::SpawnEggSpiderId),   // 生物蛋（蜘蛛）t285
        int(RecipeRegistry::SpawnEggChickenId),  // t398 生物蛋（鸡）：右键 → 生成鸡
        int(RecipeRegistry::SpawnEggSquidId),    // t399 生物蛋（鱿鱼）：右键 → 生成鱿鱼
        // t244 mob 死亡掉落物（杀猪 / 牛 / 羊产出；机制等价 MC 1.0 被动生物掉落，纯原创自绘 MaterialIcon §9a）：
        //   完成创造调色板一览 —— 生存时由 mob 死亡掉落 / 拾取获得，创造直接取用便于测试 / 装饰。
        //   可堆叠 64（走材料段默认 maxStack）；非方块 → 右键不放置（playercontroller selectedBlock 守 Air）。
        int(RecipeRegistry::RawPorkchopId),   // 生猪排：杀猪掉落（带骨肉排，浅粉红）
        int(RecipeRegistry::RawBeefId),       // 生牛肉：杀牛掉落（深红肉块）
        int(RecipeRegistry::LeatherId),       // 皮革：杀牛掉落（棕黄兽皮）
        int(RecipeRegistry::WoolId),          // 羊毛：杀羊掉落（白色绒毛团）
        int(RecipeRegistry::DiamondId),       // t279 钻石：钻石矿挖掘掉落（需铁镐；机制等价 MC 1.0 钻石）
        // t299 敌对 mob 死亡掉落物（杀骸骨 / 蹒跚者 / 蜘蛛产出；机制等价 MC 1.0 敌对生物掉落，纯原创自绘 MaterialIcon §9a）：
        //   完成创造调色板一览 —— 生存时由敌对 mob 死亡掉落 / 拾取获得，创造直接取用便于测试 / 装饰。
        //   可堆叠 64（走材料段默认 maxStack）；非方块 → 右键不放置（playercontroller selectedBlock 守 Air）。
        int(RecipeRegistry::BoneId),          // 骨头：杀骸骨掉落（米白骨段 + 两端膨节）
        int(RecipeRegistry::RottenFleshId),   // 腐肉：杀蹒跩者掉落（暗红腐块 + 绿斑霉点）
        int(RecipeRegistry::StringId),        // 线：杀蜘蛛掉落（浅色缠绕线团）
        // t304 箭（弓弹药）：材料段 0x21A，可堆叠 64。创造直接取用便于测试弓（仍需背包有箭才射得出）。
        int(RecipeRegistry::ArrowId),         // 箭：弓弹药；铁锭+木棒+线合成 4 件（t304）
        // t305 树苗物品：材料段 0x21B，可堆叠 64。破叶概率掉落（生存）/ 创造直接取用。右键草地 / 泥土种植 →
        //   Sapling 方块（WorldClock tick 推进成长长成完整橡树）。机制等价 MC 1.0 橡树树苗；MaterialIcon 自绘图标。
        int(RecipeRegistry::SaplingItemId),   // 树苗物品：破叶掉落；右键草地/泥土种植（t305）
        // t308 铜/金原矿 + 锭（机制等价 MC 1.0「铜/铁/金矿采下为原矿，须熔炉冶炼成锭」）：
        //   铜矿 / 金矿挖掘掉落原矿（非锭）→ 熔炉冶炼成锭（区别于钻石矿直接掉钻石）。创造直接取用便于测试。
        //   可堆叠 64（走材料段默认）；非方块 → 右键不放置。MaterialIcon 自绘图标（原矿走矿石族八边形 + 金属斑；
        //   锭走水平梯形金属条，铜橙 / 金黄配色区分）。
        int(RecipeRegistry::CopperOreDropId), // 铜原矿：铜矿石挖掘掉落；熔炉冶炼为铜锭
        int(RecipeRegistry::CopperIngotId),   // 铜锭：铜原矿冶炼产物；铜工具 / 装备配方原料（后续任务）
        int(RecipeRegistry::GoldOreDropId),   // 金原矿：金矿石挖掘掉落；熔炉冶炼为金锭
        int(RecipeRegistry::GoldIngotId),     // 金锭：金原矿冶炼产物；金工具 / 装备 / 钟配方原料（后续任务）
        // t344 烤肉（mob 燃烧致死掉落；机制等价 MC 1.0 着火死亡掉熟肉）：生存由「烧死动物」获得，
        //   创造调色板补全便于测试（同生肉）。可堆叠 64；非方块 → 右键不放置。MaterialIcon 自绘褐色烤肉图标。
        int(RecipeRegistry::CookedPorkchopId), // 熟猪排：猪燃烧致死掉落
        int(RecipeRegistry::CookedBeefId),     // 熟牛肉：牛燃烧致死掉落
        int(RecipeRegistry::CookedMuttonId),   // 熟羊肉：羊燃烧致死掉落
        // t393 战利品表专用材料（地牢箱首开填充 + 预留钓鱼 t401）：生存非合成获得（仅地牢战利品），
        //   创造调色板补全便于测试图标 / 装饰取用。可堆叠 64（走材料段默认）；非方块 → 右键不放置。
        //   MaterialIcon 自绘图标（红石=红粉堆 / 马鞍=棕鞍座 / 命名牌=纸签+绳 / 附魔书=书+紫光）。
        int(RecipeRegistry::RedstoneId),        // 红石粉：地牢战利品（机制等价 MC 1.0 redstone dust）
        int(RecipeRegistry::SaddleId),          // 马鞍：地牢稀有战利品（机制等价 MC 1.0 saddle）
        int(RecipeRegistry::NameTagId),         // 命名牌：地牢稀有战利品（机制等价 MC name tag，1.6+）
        int(RecipeRegistry::EnchantedBookId),   // t615 附魔书：附魔台附书产 / 地牢极稀有战利品（真附魔，maxStack=1）
        // t398 鸡相关材料（机制等价 MC 1.0 鸡掉羽毛 + 生鸡肉 + 周期下蛋；生存由杀鸡 / 拾鸡蛋获得，
        //   创造调色板补全便于测试 / 装饰）。可堆叠 64；非方块 → 右键不放置。MaterialIcon 自绘图标。
        int(RecipeRegistry::FeatherId),         // 羽毛：杀鸡掉落
        int(RecipeRegistry::RawChickenId),      // 生鸡肉：杀鸡掉落
        int(RecipeRegistry::CookedChickenId),   // 熟鸡肉：鸡燃烧致死掉落
        int(RecipeRegistry::EggId),             // 蛋：鸡周期性下蛋掉落
        // t400 繁殖食物（机制等价 MC 1.0 胡萝卜 / 马铃薯 —— 猪的繁殖食物）：创造调色板补全便于测试繁殖
        //   （喂成体猪触发求偶 → 同种配对产幼崽）。生存由（未来）种植 / 战利品获得；可堆叠 64；非方块 → 右键喂食。
        int(RecipeRegistry::CarrotId),          // 胡萝卜：猪繁殖食物（喂成体猪 → 求偶）
        int(RecipeRegistry::PotatoId),          // 马铃薯：猪繁殖食物（喂成体猪 → 求偶）
        // t399 鱿鱼相关材料（机制等价 MC 1.0 鱿鱼死亡掉墨囊；生存由杀鱿鱼获得，创造调色板补全便于测试 / 装饰）。
        //   可堆叠 64；非方块 → 右键不放置。MaterialIcon 自绘图标。
        int(RecipeRegistry::InkSacId),          // 墨囊：杀鱿鱼掉落（机制等价 MC 1.0 ink sac）
        // t401 钓鱼获物（机制等价 MC 1.0 raw fish；生存由钓竿拉起咬钩获物获得，创造调色板补全便于测试 / 装饰）。
        //   可堆叠 64；非方块 → 右键不放置。MaterialIcon 自绘鱼形图标。
        int(RecipeRegistry::RawFishId),         // 生鱼：钓竿拉起获物（机制等价 MC 1.0 raw fish；钓鱼常见获物）
        // t447 骨粉（机制等价 MC 1.0 bone meal；生存由骨头合成获得，创造调色板补全便于测试 / 装饰）。
        //   可堆叠 64；非方块 → 右键不走放置，走 useBlock 催熟分支。MaterialIcon 自绘骨粉图标。
        int(RecipeRegistry::BonemealId),        // 骨粉：骨头合成产物；右键未成熟作物催熟一阶段（t447）
        // t467 甜浆果（机制等价 MC 1.0 sweet berries；雪原浆果灌木丛采摘产物 + 食物）。生存由右键成熟浆果丛采摘
        //   得（2-3 浆果 + 丛回阶段 0 重长），创造调色板补全便于测试 / 装饰。可堆叠 64；非方块 → 右键不放置，
        //   走「食用」分支（长按右键累积进食 +2 饥饿）。MaterialIcon 自绘红色浆果簇图标。
        int(RecipeRegistry::SweetBerryId),      // 甜浆果：成熟浆果丛采摘得；可食（右键长按 +2 饥饿，t467）
        // t469 船物品（机制等价 MC 1.0 boat；5 木板 U 形合成；右键水面放船 + 骑乘 + WASD + 冰上加速 + 撞坏掉落）。
        //   生存由合成获得，创造调色板补全便于测试。可堆叠 64；非方块（材料段）→ 右键不走方块放置，走 useBlock
        //   船交互分支（playercontroller placeBlock 船段：先试骑乘命中的船 → mount；否则放船）。MaterialIcon 自绘船图标。
        int(RecipeRegistry::OakBoatId),         // 橡木船：5 橡木木板合成；右键水面放船 + 骑乘
        int(RecipeRegistry::SpruceBoatId),      // 云杉船：5 云杉木板合成；右键水面放船 + 骑乘
        // t471 青金石（机制等价 MC 1.0 lapis lazuli；青金矿石挖掘掉落，附魔台消耗材料）。生存由挖青金矿石获得，
        //   创造调色板补全便于测试 / 装饰。可堆叠 64；非方块 → 右键不放置。MaterialIcon 自绘青金石图标。
        int(RecipeRegistry::LapisId),           // 青金石：青金矿石挖掘掉落；附魔台消耗材料（t474）
        // t473 纸 + 书（机制等价 MC 1.0 甘蔗造纸 → 书；附魔台 / 附魔书 / 书架的核心材料链）。生存由合成获得
        //   （纸=3 甘蔗横排 → 3 纸；书=3 纸 + 1 皮革 → 1 书），创造调色板补全便于测试 / 装饰。可堆叠 64；
        //   非方块 → 右键不放置。MaterialIcon 自绘纸页 / 书本图标。
        int(RecipeRegistry::PaperId),           // 纸：3 甘蔗横排合成；书配方原料（t473）
        int(RecipeRegistry::BookId),            // 书：3 纸 + 1 皮革合成；附魔台 / 附魔书 / 书架材料（t473）
        // t485 火药（机制等价 MC 1.0 gunpowder）：杀潜行者（Stalker）掉落；TNT 合成原料（5 火药 + 4 沙 → 1 TNT）。
        //   创造调色板补全便于测试 TNT 合成 / 引爆。可堆叠 64；非方块 → 右键不放置。MaterialIcon 自绘火药图标。
        int(RecipeRegistry::GunpowderId),       // 火药：杀潜行者掉落；TNT 合成原料
        // t487 末影之眼（机制等价 MC 1.0 ender eye）：要塞宝藏箱战利品 + 创造调色板补全（便于测试激活传送门）。
        //   可堆叠 64；非方块（材料段）→ 右键不走方块放置，走 useBlock 末地传送门激活分支（placeBlock 检测命中
        //   EndPortal + 持末影之眼 → 翻传送门 state bit0 激活）。MaterialIcon 自绘末影之眼图标（绿蓝球体 + 瞳孔）。
        int(RecipeRegistry::EndEyeId),          // 末影之眼：要塞宝藏箱战利品；右键末地传送门激活（t487）
        // t507 木碗 + 蘑菇汤（机制等价 MC 1.0 bowl / mushroom stew）：生存由合成获得（碗=3 木板 V 形 / 蘑菇汤=碗+红+白
        //   蘑菇），创造调色板补全便于测试食用。木碗可堆叠 64；蘑菇汤 maxStack=1（碗装液体食物不可叠，同铁桶族）。
        //   非方块（材料段）→ 右键不放置（蘑菇汤走「食用」分支：长按右键累积进食 +10 饥饿，食完返空碗）。
        int(RecipeRegistry::BowlId),             // 木碗：3 木板 V 形合成；蘑菇汤原料
        int(RecipeRegistry::MushroomStewId),     // 蘑菇汤：碗+红蘑菇+白蘑菇合成；右键食 +10 饥饿（食完返空碗）
        // t505 雪球（机制等价 MC 1.0 snowball；材料段 0x23D）。生存由铲挖雪层 / 雪块 / 雪傀儡死亡掉落获得，
        //   创造调色板补全便于测试抛掷。可堆叠 64；非方块（材料段）→ 右键不走方块放置，走 useBlock 雪球抛掷分支
        //   （playercontroller placeBlock 雪球段：spawnSnowball 玩家抛雪球，砸敌对 mob 0 伤害 + 红闪 + 击退）。
        //   MaterialIcon 自绘雪球图标（drawSnowball 冷白小球，§9 原创）。
        int(RecipeRegistry::SnowballId)         // 雪球：铲挖雪层/雪块/雪傀儡死亡掉落；右键抛掷（砸 mob 红闪+击退）
        ,
        // t567/t568 指南针 + 钟（机制等价 MC 1.0 compass / clock；HUD 信息件：指南针指针指向出生点、钟显示昼夜
        //   相位）。生存由工作台合成获得（4 铁锭/金锭 + 1 红石），创造调色板补全便于测试。可堆叠 64；非方块
        //   （材料段）→ 右键不放置。MaterialIcon 自绘图标（圆表盘 + 磁针 / 金框表盘）。
        int(RecipeRegistry::CompassId),          // 指南针：4 铁锭+1 红石合成；HUD 指针指向出生点（t567）
        int(RecipeRegistry::ClockId)             // 钟：4 金锭+1 红石合成；HUD 显示当前昼夜相位（t568）
    };
}

int Hotbar::blockIdAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return int(BlockRegistry::Air);
    return m_slots[size_t(slot)].id;
}

int Hotbar::countAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return 0;
    return m_slots[size_t(slot)].count;
}

// t263 每槽工具剩余耐久（非工具 / 空槽 = 0）。tooltip 显「cur/max」+ InventoryOps.readSlot 搬运保真读它。
int Hotbar::durabilityAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return 0;
    return m_slots[size_t(slot)].durability;
}

// 拷一份槽内容给 QML 作 Repeater model（QVariantList<int>：物品 id）。slotRevision 触碰的 model
// 绑定在 slotsChanged 后重算 → 返回新数组 → Repeater 整列重建。
QVariantList Hotbar::slotList() const
{
    QVariantList v;
    v.reserve(int(m_slots.size()));
    for (const ItemStack &s : m_slots) v.append(s.id);
    return v;
}

// 每栈 count（与 slotList 平行）。QML 数量 Text 触碰 slotRevision 刷新（同 model 绑定机制）。
QVariantList Hotbar::countList() const
{
    QVariantList v;
    v.reserve(int(m_slots.size()));
    for (const ItemStack &s : m_slots) v.append(s.count);
    return v;
}

// 创造背包网格用：所有可放置方块 id（实体方块，air 除外）。id 取自 BlockRegistry（单一权威）。
// t50：追加工作台；t80：追加熔炉；t84：追加煤矿/铁矿石；t88：追加火把（伪光源方块，可在创造调色板
// 直接取用，便于测试放置 + 发光精灵效果）。t134：追加 6 类木制半方块（slab/stairs/fence/pressure_plate/
// door/trapdoor），可在创造直接取用测试异形放置 / 开合。t244：追加 cross 广告牌方块（草丛 / 小麦作物），
// 完成创造调色板一览（spec「加入所有新方块/物品」—— 草丛由 worldgen 散布、小麦作物经种子种出，
// 但创造调色板供玩家直接取用测试放置 / 渲染，与 MC 创造页含草丛 / 作物同语义）。
QVariantList Hotbar::creativeBlocks() const
{
    return { int(BlockRegistry::Grass),         int(BlockRegistry::Dirt),  int(BlockRegistry::Stone),
             int(BlockRegistry::Cobble),        int(BlockRegistry::Log),   int(BlockRegistry::Planks),
             int(BlockRegistry::Leaves),        int(BlockRegistry::Sand),
             int(BlockRegistry::CraftingTable), int(BlockRegistry::Furnace),
             int(BlockRegistry::CoalOre),       int(BlockRegistry::IronOre),
             int(BlockRegistry::DiamondOre),                                   // t279 钻矿石（散布于 stone 深层 y∈[5,40]、需铁镐采掘；掉钻石材料）
             int(BlockRegistry::CopperOre),                                    // t308 铜矿石（散布于 stone 浅中层 y∈[5,45]、需石镐采掘；掉铜原矿→熔炉烧铜锭）
             int(BlockRegistry::GoldOre),                                      // t308 金矿石（散布于 stone 深层 y∈[5,25]、需铁镐采掘；掉金原矿→熔炉烧金锭）
             int(BlockRegistry::LapisOre),                                     // t471 青金矿石（散布于 stone 深层 y∈[5,31]、需石镐采掘；掉青金石物品）
             int(BlockRegistry::RedstoneOre),                                  // t569 红石矿石（散布于 stone 最深层 y∈[5,16]、需铁镐采掘；掉 4 红石粉；走过/挖掘点亮微弱红光）
             int(BlockRegistry::Bedrock),                                      // misc 二轮 基岩（机制等价 MC 创造可取 bedrock 物品；生存不可破 hardness=-1，创造可放置/取用，便于建筑/测试底层封顶）
             int(BlockRegistry::Torch),
             int(BlockRegistry::Chest),                                    // t173 箱子（右键开 27 槽）
             int(BlockRegistry::Farmland),                                 // t234 耕地（持锄右键泥土/草得；干/湿两态）
             // t134 木制半方块：
             int(BlockRegistry::WoodSlab),          int(BlockRegistry::WoodStairs),
             int(BlockRegistry::WoodFence),         int(BlockRegistry::WoodPressurePlate),
             int(BlockRegistry::WoodDoor),          int(BlockRegistry::WoodTrapdoor),
             // t412 圆石变体（cobble variants）：石质半方块（台阶 / 楼梯 / 墙 / 压力板），复用异形方块系统 + 圆石贴图。
             int(BlockRegistry::CobbleSlab),        int(BlockRegistry::CobbleStairs),
             int(BlockRegistry::CobbleFence),       int(BlockRegistry::CobblePressurePlate),
             int(BlockRegistry::Ladder),                                     // t413 木梯（竖直爬行梯；玩家入格+按前向上爬；可放置）
             // t244 cross 广告牌方块（透明底 cutout；与火把同走非整立方渲染）：
             int(BlockRegistry::TallGrass),                                     // 草丛（worldgen 散布 / 杀草掉种子）
             int(BlockRegistry::WheatCrop),                                    // 小麦作物（state=阶段；种 0..7，图标显成熟态）
             int(BlockRegistry::Wool),                                        // t300 羊毛方块（剪羊毛 / 杀羊掉落；可放置）
             // t455 16 色 wool 其余 15 色变体（机制等价 MC 1.0 羊毛 16 色；创造调色板每色独立取用 + 右键放置）。
             int(BlockRegistry::WoolOrange), int(BlockRegistry::WoolMagenta), int(BlockRegistry::WoolLightBlue),
             int(BlockRegistry::WoolYellow),  int(BlockRegistry::WoolLime),   int(BlockRegistry::WoolPink),
             int(BlockRegistry::WoolGray),    int(BlockRegistry::WoolLightGray), int(BlockRegistry::WoolCyan),
             int(BlockRegistry::WoolPurple),  int(BlockRegistry::WoolBlue),   int(BlockRegistry::WoolBrown),
             int(BlockRegistry::WoolGreen),   int(BlockRegistry::WoolRed),    int(BlockRegistry::WoolBlack),
             // t394 沙漠群系内容：砂岩（沙下成岩）/ 仙人掌（接触伤害）/ 枯死的灌木（cross 装饰）。机制等价 MC 1.0
             //   沙漠三件套（sandstone / cactus / dead bush），名称 / 贴图原创自绘 §9a。
             int(BlockRegistry::Sandstone),                                   // 砂岩（沙下成岩；需镐采掘；可放置）
             int(BlockRegistry::Cactus),                                      // 仙人掌（接触伤害；放沙/仙人掌上）
             int(BlockRegistry::DeadBush),                                    // 枯死的灌木（cross 装饰；放沙上）
             // t395 雪原/针叶群系内容：积雪层（地表覆雪）/ 云杉原木（云杉树主干）。机制等价 MC 1.0 寒冷群系三件套
             //   （snow / spruce log），名称 / 贴图原创自绘 §9a。冰（Ice）由 worldgen 冻结水面获得（同 water / lava
             //   属系统获得），不进创造调色板。
             int(BlockRegistry::SnowLayer),                                  // 积雪层（雪原地表覆雪；铲加速；可放置）
             int(BlockRegistry::SpruceLog),                                  // 云杉原木（云杉树主干；斧加速；可放置）
             // t468 冰族（Ice / PackIce / BlueIce）：透明整立方（iceOnly 段 Blend 半透）+ 冰上低摩擦滑动。Ice 由 worldgen
             //   冻结水面 / tickIceFreeze 获得，PackIce/BlueIce 创造调色板取用测试 / 装饰（滑动速度递增 Ice<PackIce<BlueIce）。
             int(BlockRegistry::Ice),                                        // 冰（雪原水面冻结；半透；冰上滑行）
             int(BlockRegistry::PackIce),                                    // 浮冰（更滑变种；半透；可放置）
             int(BlockRegistry::BlueIce),                                    // 蓝冰（最滑变种；半透；可放置）
             // t466 云杉木制品链（机制等价 MC 1.0 spruce 木制品；名称 / 贴图原创自绘 §9a）。复用既有木制品机制，
             //   仅换 id + 贴图（深色木纹 spruce_planks 区别橡木 planks）。云杉原木→4 云杉木板；云杉木板→台阶/栅栏/门。
             int(BlockRegistry::SprucePlanks),                               // 云杉木板（深色木纹整立方；配方：云杉原木→4）
             int(BlockRegistry::SpruceSlab),                                 // 云杉台阶（半高；配方：3 云杉木板→6）
             int(BlockRegistry::SpruceFence),                                // 云杉栅栏（立柱+横档；配方：云杉木板+棒→3）
             int(BlockRegistry::SpruceDoor),                                 // 云杉门（两格高；配方：3 云杉木板→1）
             // t396 沼泽群系内容：睡莲（水面浮叶）/ 蘑菇（草地小蘑菇）。机制等价 MC 1.0 沼泽植物
             //   （lily pad / mushroom），名称/贴图原创自绘 §9a。cross 路由（alphaCutoff cutout 透明底）。
             int(BlockRegistry::LilyPad),                                    // 睡莲（沼泽水面浮叶；横向浮叶 cross 路由；可放置）
             int(BlockRegistry::Mushroom),                                   // 蘑菇（沼泽草地小蘑菇；cross 装饰；可放置）
             int(BlockRegistry::BrownMushroom),                              // t507 白蘑菇 / 棕蘑菇（沼泽草地小蘑菇；cross 装饰；蘑菇汤原料；可放置）
             // t397 多群系装饰植物：花 4 色变体 + 甘蔗（机制等价 MC 1.0 花 / 甘蔗；名称 / 贴图原创自绘 §9a）。
             //   cross 路由（alphaCutoff cutout 透明底）；每色花独立 id 便于创造调色板取用 + 右键放置。
             int(BlockRegistry::FlowerRed),    int(BlockRegistry::FlowerYellow), // 红花 / 黄花（worldgen 散布 / 各群系花点缀）
             int(BlockRegistry::FlowerBlue),   int(BlockRegistry::FlowerWhite),  // 蓝花 / 白花
             int(BlockRegistry::Sugarcane),                                   // 甘蔗（水边生长；1..3 高叠柱；可放置）
             // t387 床方块 8 色变体（简化单格整立方；机制等价 MC 1.0 床。配方 planks+wool → 红床；其余色创造直接取用）。
             int(BlockRegistry::BedRed),    int(BlockRegistry::BedOrange),
             int(BlockRegistry::BedYellow), int(BlockRegistry::BedGreen),
             int(BlockRegistry::BedCyan),   int(BlockRegistry::BedBlue),
             int(BlockRegistry::BedMagenta),int(BlockRegistry::BedBlack),
             // t455 16 色床补齐 8 色新变体（机制等价 MC 1.0 床 16 色；配方 = 3 同色羊毛 + 3 木板 → 该色床）。
             int(BlockRegistry::BedWhite),    int(BlockRegistry::BedLightBlue),
             int(BlockRegistry::BedLime),     int(BlockRegistry::BedPink),
             int(BlockRegistry::BedGray),     int(BlockRegistry::BedLightGray),
             int(BlockRegistry::BedPurple),   int(BlockRegistry::BedBrown),
             // t474 附魔链两件方块（机制等价 MC 1.0 enchanting table / bookshelf；右键附魔台开 UI / 书架作加成源）。
             int(BlockRegistry::EnchantingTable),                              // 附魔台（右键开附魔 UI；配方书+钻石+黑曜石）
             int(BlockRegistry::Bookshelf),                                   // 书架（合成产物；附魔台加成来源；配方木板+书）
             // t477 铁块 + 铁砧（机制等价 MC 1.0 iron block / anvil；右键铁砧开铁砧 UI）。
             int(BlockRegistry::IronBlock),                                   // 铁块（9 铁锭合成存储方块；铁砧配方前置）
             int(BlockRegistry::Anvil),                                       // 铁砧（右键开铁砧 UI 修复/合并/重命名；配方 3 铁块+4 铁锭；微损/重损不进调色板）
             // t482/t483 防御造物方块（机制等价 MC 1.0 雪傀儡 / 铁傀儡搭建材料；南瓜放好 + 下方排列 → 造物）。
             int(BlockRegistry::Pumpkin),                                     // 南瓜（造物头部方块；雪傀儡/铁傀儡搭建触发物）
             int(BlockRegistry::Snow),                                        // 雪块（造物身体方块；雪傀儡 = 南瓜 + 雪块×2 竖直）
             // t484 废弃矿井结构方块（机制等价 MC 1.0 废弃矿井 mineshaft 的蛛网 / 铁轨；worldgen 散布 / 创造取用）。
             int(BlockRegistry::Cobweb),                                      // 蜘蛛网（cross 形蛛网；矿井散布；无碰撞瞬破掉线）
             int(BlockRegistry::Rail),                                        // 铁轨（贴地薄板 flat；矿井木地板散布；配方 6 铁锭+1 木棒→16）
             // t485 沙漠神殿结构方块（机制等价 MC 1.0 沙漠神殿 desert temple 的 TNT / 切制砂岩；worldgen 散布 / 创造取用）。
             int(BlockRegistry::CutSandstone),                                // 切制砂岩（装饰砂岩变体；金字塔外框；可放置）
             int(BlockRegistry::TntBlock),                                   // TNT（可引爆爆炸物；沙漠神殿陷阱；配方 5 火药+4 沙→1）
             // t486 丛林神殿结构方块（机制等价 MC 1.0 丛林神殿 jungle temple 的苔石 / 发射器；worldgen 散布 / 创造取用）。
             int(BlockRegistry::MossyCobble),                                // 苔石（长苔圆石变体；丛林神殿主体；可放置）
             int(BlockRegistry::Dispenser),                                  // 发射器（踩压力板触发的射箭机关；丛林神殿陷阱；可放置 / 自建机关）
             // t609 投掷器（机制等价 MC 1.0 dropper——全部物品弹出掉落物的机关盒；7 圆石合成；DispenserStore 9 槽共用）。
             int(BlockRegistry::Dropper),                                    // 投掷器（踩压力板触发弹出全部物品；可放置 / 自建机关）
             // t620 矿物存储块（机制等价 MC 1.0 coal/lapis/diamond/gold/redstone block；9 材料↔1 块 双向配方。
             //   铁块 IronBlock 已在上方既存列表；本段补其余五种）。
             int(BlockRegistry::CoalBlock),                                  // 煤炭块（9 煤↔1 块；燃料 800s=80 件；木镐采掘）
             int(BlockRegistry::LapisBlock),                                 // 青金石块（9 青金石↔1 块；石镐采掘；附魔材料压缩存储）
             int(BlockRegistry::DiamondBlock),                               // 钻石块（9 钻石↔1 块；铁镐采掘）
             int(BlockRegistry::GoldBlock),                                  // 金块（9 金锭↔1 块；铁镐采掘）
             int(BlockRegistry::RedstoneBlock),                              // 红石块（9 红石粉↔1 块；铁镐采掘）
             // t620 红石灯（机制等价 MC 1.0 redstone lamp；右键开关的可放置光源方块——on 态光 15 + 亮贴图）。
             int(BlockRegistry::RedstoneLamp),                               // 红石灯（右键开关光源；配方 4 红石+1 玻璃）
             // t487 要塞结构方块（机制等价 MC 1.0 要塞 stronghold 的石砖 / 石砖台阶 / 石砖楼梯；worldgen 散布 / 创造取用）。
             int(BlockRegistry::StoneBrick),                                 // 石砖（石质整立方 + 砖纹；要塞墙体主体；可放置）
             int(BlockRegistry::StoneBrickSlab),                             // 石砖台阶（半高；复用 ShapeSlab 几何 + 石砖贴图；可放置）
             int(BlockRegistry::StoneBrickStairs) };                         // 石砖楼梯（整步+背墙；复用 ShapeStairs 几何 + 石砖贴图；可放置）
}

QString Hotbar::iconSourceAt(int slot) const
{
    return iconSourceForBlock(blockIdAt(slot));
}

QString Hotbar::iconSourceForBlock(int blockId) const
{
    // 方块段才返回 PNG 路径；工具段（>=0x100，t33）/ 材料段（>=0x200，t50 木棒）图标由 QML 自绘
    // （ToolIcon / 材料图标 Canvas，§9a）→ 返空串，调用方据 isTool / isMaterial 切到对应自绘 delegate。
    // 越界先判再 cast，防 quint8 截断别名。
    if (blockId <= 0 || blockId >= int(BlockRegistry::Count)) return QString();
    // t456 pack item 图标覆盖：pack 启用且该方块在「方块→pack item/前贴图」映射内（现含 CraftingTable=9 /
    //   Furnace=10 双候选（item/<name>.png 优先、block/<name>_front.png 兜底，t537 回退到 t492 二轮的 2D pack 图，
    //   用户否决 t518 的 3D「一坨」）+ 16 色床 {32..39,78..85}→bed.png + 木梯 Ladder=62→ladder.png；
    //   LapisOre=93 刻意不在映射，与其它矿石一致走 3D 立方体图标，t493 二轮复盘撤销）、包内有 PNG 时，返 pack
    //   的 file:// URL（2D 物品图标改用 pack item 贴图，机制等价 MC item icon）；pack 关 / 无映射 / 包内缺 →
    //   落下方程序生成 icon_<block>.png（含工作台 / 熔炉的 3D 立方体图标）。仅 2D 物品图标路径
    //   （hotbar/背包/光标）消费；3D 手持立方 / 掉落物走 BlockCube+voxelAtlas 另一路径，不调本函数，故不受影响。
    const QString packSrc = ResourcePackManager::blockItemIconSource(blockId);
    if (!packSrc.isEmpty())
        return packSrc;
    const char *file = iconFileForBlock(quint8(blockId));
    if (!file) return QString();
    return QStringLiteral("qrc:/textures/") + QString::fromLatin1(file);
}

QString Hotbar::nameAt(int slot) const
{
    // t477 优先返回自定义名（铁砧重命名）；无自定义名 → 走注册表默认名 displayName。
    if (slot >= 0 && slot < int(m_slots.size())) {
        const QString &cn = m_slots[size_t(slot)].customName;
        if (!cn.isEmpty()) return cn;
    }
    return nameForBlock(blockIdAt(slot));
}

QString Hotbar::nameForBlock(int blockId) const
{
    // 走单一权威：方块段→BlockRegistry::displayName；工具段→ToolRegistry::displayName（t33）；
    // 材料段（t50 木棒 / t85 煤炭·铁原矿·铁锭）→ 本地通用名（材料段无注册表，名简单且少，就近返回）。
    // air / 越界 → 空串。PLAN §9：UI 不另存方块 / 工具名副本。
    if (blockId <= 0) return QString();
    // t345 护甲段（>= ArmorIdBase，在材料段之上）：走 ArmorRegistry::displayName（皮革头盔 / 铁胸甲 …）。
    //   须在材料段判定之前（护甲段 id 也 >= kMaterialIdBase，否则会落材料段兜底空串）。
    if (ArmorRegistry::isArmor(blockId)) return ArmorRegistry::displayName(blockId);
    if (blockId >= kMaterialIdBase) {
        // 材料段：木棒 / 煤炭 / 铁原矿 / 铁锭（id 取自 RecipeRegistry 常量，与 blockregistry.cpp 矿石
        // dropId 字面量同源）。任一漏返 → 空串（兜底，UI 不显名但不崩）。
        if (blockId == RecipeRegistry::StickId)       return QStringLiteral("木棒");
        if (blockId == RecipeRegistry::CoalId)        return QStringLiteral("煤炭");
        if (blockId == RecipeRegistry::IronOreDropId) return QStringLiteral("铁原矿");
        if (blockId == RecipeRegistry::IronIngotId)   return QStringLiteral("铁锭");
        // t87 冶炼产物（spec 可选）：沙子→玻璃、原木→木炭。
        if (blockId == RecipeRegistry::GlassId)       return QStringLiteral("玻璃");
        if (blockId == RecipeRegistry::CharcoalId)    return QStringLiteral("木炭");
        // t174 铁桶（材料段 0x206/0x207，maxStack=1 不可堆叠）：空桶可合成 / 装水桶由舀水得。
        if (blockId == RecipeRegistry::BucketEmptyId) return QStringLiteral("铁桶");
        if (blockId == RecipeRegistry::WaterBucketId) return QStringLiteral("装水铁桶");
        if (blockId == RecipeRegistry::LavaBucketId) return QStringLiteral("装岩浆铁桶"); // t343 空桶舀岩浆源得
        if (blockId == RecipeRegistry::SeedId)        return QStringLiteral("小麦种子"); // t235
        if (blockId == RecipeRegistry::WheatId)       return QStringLiteral("小麦");     // t237 收割成熟小麦作物掉落
        if (blockId == RecipeRegistry::BreadId)       return QStringLiteral("面包");     // t238 3 小麦合成；右键食 +5 饥饿
        // t242 mob 死亡掉落物：杀猪 / 牛 / 羊产出（机制等价 MC 1.0 被动生物掉落，名称用通用词、零 MC 专名 §9）。
        if (blockId == RecipeRegistry::RawPorkchopId) return QStringLiteral("生猪排");
        if (blockId == RecipeRegistry::RawBeefId)     return QStringLiteral("生牛肉");
        if (blockId == RecipeRegistry::LeatherId)     return QStringLiteral("皮革");
        if (blockId == RecipeRegistry::WoolId)        return QStringLiteral("羊毛");
        // t243 生物蛋：创造模式物品，右键地面 → 生成对应被动生物（机制等价 MC 1.0 spawn egg，零 MC 专名 §9）。
        if (blockId == RecipeRegistry::SpawnEggPigId)   return QStringLiteral("生物蛋（猪）");
        if (blockId == RecipeRegistry::SpawnEggCowId)   return QStringLiteral("生物蛋（牛）");
        if (blockId == RecipeRegistry::SpawnEggSheepId) return QStringLiteral("生物蛋（羊）");
        if (blockId == RecipeRegistry::SpawnEggShamblerId) return QStringLiteral("生物蛋（蹒跚者）");
        if (blockId == RecipeRegistry::SpawnEggBonesId)    return QStringLiteral("生物蛋（骷髅弓箭手）"); // t594：骸骨→骷髅弓箭手（骷髅是通用词；MobBones 标识符不动）
        if (blockId == RecipeRegistry::SpawnEggStalkerId)  return QStringLiteral("生物蛋（潜行者）");
        if (blockId == RecipeRegistry::SpawnEggSpiderId)   return QStringLiteral("生物蛋（蜘蛛）");
        if (blockId == RecipeRegistry::DiamondId)       return QStringLiteral("钻石"); // t279 钻石矿挖掘掉落
        // t471 青金石：青金矿石挖掘掉落（需石镐；机制等价 MC 1.0 青金石）；附魔台消耗材料（t474）。
        if (blockId == RecipeRegistry::LapisId)         return QStringLiteral("青金石"); // 青金矿石挖掘掉落
        // t473 纸 + 书（机制等价 MC 1.0 paper / book；甘蔗造纸 → 书，附魔台 / 附魔书 / 书架的核心材料链）。零 MC 专名（§9）。
        if (blockId == RecipeRegistry::PaperId)         return QStringLiteral("纸");     // 3 甘蔗横排合成；书配方原料
        if (blockId == RecipeRegistry::BookId)          return QStringLiteral("书");     // 3 纸 + 1 皮革合成；附魔台 / 附魔书 / 书架材料
        // t485 火药（机制等价 MC 1.0 gunpowder）：杀潜行者（Stalker，机制等价 MC 苦力怕）掉落；TNT 合成原料（5 火药 + 4 沙 → 1 TNT）。
        if (blockId == RecipeRegistry::GunpowderId)   return QStringLiteral("火药"); // 杀潜行者掉落；TNT 合成原料
        // t487 末影之眼（机制等价 MC 1.0 ender eye）：要塞宝藏箱战利品；右键末地传送门激活（末地预热占位）。
        //   名称用通用词「末影之眼」、零 MC 专名（§9 区隔）。
        if (blockId == RecipeRegistry::EndEyeId)      return QStringLiteral("末影之眼"); // 要塞宝藏箱战利品；激活末地传送门
        // t567 指南针（材料段 0x23F；4 铁锭 + 1 红石合成；HUD 指针指向出生点）。零 MC 专名（§9）。
        if (blockId == RecipeRegistry::CompassId)     return QStringLiteral("指南针"); // 4 铁锭+1 红石合成；HUD 指针指向出生点
        // t568 钟（材料段 0x240；4 金锭 + 1 红石合成；HUD 显示当前昼夜相位）。零 MC 专名（§9）。
        if (blockId == RecipeRegistry::ClockId)       return QStringLiteral("钟");     // 4 金锭+1 红石合成；HUD 显示当前时间
        // t507 木碗 + 蘑菇汤（机制等价 MC 1.0 bowl / mushroom stew；零 MC 专名 §9）。
        if (blockId == RecipeRegistry::BowlId)         return QStringLiteral("木碗");     // 4 木板合成；蘑菇汤原料
        if (blockId == RecipeRegistry::MushroomStewId) return QStringLiteral("蘑菇汤");   // 碗+红蘑菇+白蘑菇合成；右键食 +10 饥饿（食完返空碗）
        // t299 敌对 mob 死亡掉落物：杀骸骨 / 蹒跚者 / 蜘蛛产出（机制等价 MC 1.0 敌对生物掉落，名称用通用词、零 MC 专名 §9）。
        if (blockId == RecipeRegistry::BoneId)        return QStringLiteral("骨头"); // 杀骸骨掉落
        if (blockId == RecipeRegistry::RottenFleshId) return QStringLiteral("腐肉"); // 杀蹒跚者掉落
        if (blockId == RecipeRegistry::StringId)      return QStringLiteral("线");   // 杀蜘蛛掉落（弓 / 钓竿原料）
        if (blockId == RecipeRegistry::ArrowId)       return QStringLiteral("箭");   // t304 弓弹药
        if (blockId == RecipeRegistry::SaplingItemId) return QStringLiteral("橡树树苗"); // t305 破叶掉落；种植 → 树
        // t308 铜/金原矿 + 锭（机制等价 MC 1.0「铜/铁/金矿采下为原矿，须熔炉冶炼成锭」）：
        if (blockId == RecipeRegistry::CopperOreDropId) return QStringLiteral("铜原矿"); // 铜矿石挖掘掉落；熔炉烧铜锭
        if (blockId == RecipeRegistry::CopperIngotId)   return QStringLiteral("铜锭");   // 铜原矿冶炼产物
        if (blockId == RecipeRegistry::GoldOreDropId)   return QStringLiteral("金原矿"); // 金矿石挖掘掉落；熔炉烧金锭
        if (blockId == RecipeRegistry::GoldIngotId)     return QStringLiteral("金锭");   // 金原矿冶炼产物
        // t344 烤肉（mob 燃烧致死掉落；机制等价 MC 1.0 cooked porkchop / beef / mutton）：与生肉 (RawPorkchopId/
        //   RawBeefId) 配对的熟肉段，进 creativeMaterials 故须有名 —— 旧版漏返 → 空串 → 创造调色板无 hover tooltip。
        if (blockId == RecipeRegistry::CookedPorkchopId) return QStringLiteral("熟猪排"); // 猪燃烧致死掉落
        if (blockId == RecipeRegistry::CookedBeefId)     return QStringLiteral("熟牛肉"); // 牛燃烧致死掉落
        if (blockId == RecipeRegistry::CookedMuttonId)   return QStringLiteral("熟羊肉"); // 羊燃烧致死掉落
        // t393 战利品表专用材料（地牢箱 / 渔获）：红石粉 / 马鞍 / 命名牌 / 附魔书占位。零 MC 专名（§9）。
        if (blockId == RecipeRegistry::RedstoneId)      return QStringLiteral("红石粉"); // 地牢战利品（机制等价 MC 1.0 redstone dust）
        if (blockId == RecipeRegistry::SaddleId)        return QStringLiteral("马鞍");   // 地牢稀有战利品（机制等价 MC 1.0 saddle）
        if (blockId == RecipeRegistry::NameTagId)       return QStringLiteral("命名牌"); // 地牢稀有战利品（机制等价 MC name tag）
        if (blockId == RecipeRegistry::EnchantedBookId) return QStringLiteral("附魔书"); // t615 真附魔书：附魔台附书产 + 地牢战利品；enchants 元数据携带附魔列表
        // t398 鸡相关材料（机制等价 MC 1.0 鸡掉羽毛 + 生鸡肉 + 周期下蛋；零 MC 专名 §9）。
        if (blockId == RecipeRegistry::FeatherId)         return QStringLiteral("羽毛");     // 杀鸡掉落
        if (blockId == RecipeRegistry::RawChickenId)      return QStringLiteral("生鸡肉");   // 杀鸡掉落
        if (blockId == RecipeRegistry::CookedChickenId)   return QStringLiteral("熟鸡肉");   // 鸡燃烧致死掉落
        if (blockId == RecipeRegistry::EggId)             return QStringLiteral("蛋");       // 鸡周期性下蛋掉落
        if (blockId == RecipeRegistry::SpawnEggChickenId) return QStringLiteral("生物蛋（鸡）"); // 右键地面 → 生成鸡
        // t400 繁殖食物（机制等价 MC 1.0 胡萝卜 / 马铃薯 —— 猪的繁殖食物）。零 MC 专名（§9）。
        if (blockId == RecipeRegistry::CarrotId)          return QStringLiteral("胡萝卜");   // 猪繁殖食物（喂成体猪 → 求偶）
        if (blockId == RecipeRegistry::PotatoId)          return QStringLiteral("马铃薯");   // 猪繁殖食物（喂成体猪 → 求偶）
        // t399 鱿鱼相关（机制等价 MC 1.0 鱿鱼；§9 区隔改名 + 原创名）。
        if (blockId == RecipeRegistry::InkSacId)        return QStringLiteral("墨囊");       // 杀鱿鱼掉落
        if (blockId == RecipeRegistry::SpawnEggSquidId) return QStringLiteral("生物蛋（鱿鱼）"); // 右键地面 → 生成鱿鱼
        // t401 钓鱼获物（机制等价 MC 1.0 raw fish；钓竿拉起咬钩获物）。
        if (blockId == RecipeRegistry::RawFishId)       return QStringLiteral("生鱼");       // 钓鱼常见获物
        // t447 骨粉（机制等价 MC 1.0 bone meal；骨头合成产物，右键未成熟作物催熟一阶段）。
        if (blockId == RecipeRegistry::BonemealId)      return QStringLiteral("骨粉");       // 骨头合成产物；右键作物催熟
        // t467 甜浆果（机制等价 MC 1.0 sweet berries；雪原浆果灌木丛采摘产物 + 食物）。零 MC 专名（§9）。
        if (blockId == RecipeRegistry::SweetBerryId)    return QStringLiteral("甜浆果");     // 成熟浆果丛采摘得；可食（+2 饥饿）
        // t469 船物品（机制等价 MC 1.0 boat；5 木板 U 形合成；右键水面放置 + 骑乘）。零 MC 专名（§9）。
        if (blockId == RecipeRegistry::OakBoatId)       return QStringLiteral("橡木船");     // 5 橡木木板合成；右键水面放船 + 骑乘
        if (blockId == RecipeRegistry::SpruceBoatId)    return QStringLiteral("云杉船");     // 5 云杉木板合成；右键水面放船 + 骑乘
        return QString();
    }
    if (ToolRegistry::isTool(blockId)) return ToolRegistry::displayName(blockId);
    if (blockId >= int(BlockRegistry::Count)) return QString();
    return BlockRegistry::displayName(quint8(blockId));
}

void Hotbar::scroll(int delta)
{
    const int n = int(m_slots.size());
    if (n == 0) return;
    // 仅取方向（±1），对 n 取模环绕 —— 防止触控板一次吐大 delta 跳多格。
    int s = (m_selectedSlot + (delta > 0 ? 1 : -1)) % n;
    if (s < 0) s += n;
    setSelectedSlot(s);
}

// ── 栈操作 ──

// 直接写入栈。air/非法 id/count<=0 → 清空该槽（id=0,count=0）；否则 count 钳到 maxStackSize(id)。
//   durability（t263）：经 normalizeDurability 归一（-1=自动满 / >=0=保真 clamp）。工具 count 恒 1。
//   enchants（t475）：经 applyEnchants 写入 target.enchants[4]（列表不足 4 元素按 0 补齐 = 清空）。
void Hotbar::setStack(int slot, int id, int count, int durability, const QVariantList &enchants)
{
    if (slot < 0 || slot >= int(m_slots.size())) return;
    if (!isValidItemId(id)) return;
    ItemStack target;
    if (id != 0 && count > 0) {
        const int cap = maxStackSize(id);
        target = ItemStack{id, std::min(count, cap), normalizeDurability(id, durability)};
        applyEnchants(target, enchants); // t475 仅非空栈写附魔（空栈全 0）
    } // else: 空栈（id=0 或 count<=0 → 清空；enchants 全 0）
    const ItemStack &cur = m_slots[size_t(slot)];
    if (cur.id == target.id && cur.count == target.count && cur.durability == target.durability
        && enchantsEqual(cur, target)) return; // t475 附魔也进相等判定（防「同 id 同耐久但附魔变」被误跳过）
    m_slots[size_t(slot)] = target;
    qInfo().noquote() << "[inv] setStack slot=" << slot << "id=" << target.id << "count=" << target.count
                      << "dur=" << target.durability;
    bumpRevision();
    // 改当前选中槽 → 手持方块（selectedBlockId）也变了。selectedBlockId 的 NOTIFY 只能挂一个
    // 信号（selectedSlotChanged），故此处补发它，让消费者（player.selectedBlock 绑定 / HUD 手持名）
    // 刷新；非选中槽变更不发，避免无谓重算。
    if (slot == m_selectedSlot) emit selectedSlotChanged();
}

// 兼容旧调用（t18）：单件写入。等同 setStack(slot, id, id==0?0:1)。
void Hotbar::setSlotBlock(int slot, int blockId)
{
    setStack(slot, blockId, blockId == 0 ? 0 : 1);
}

// 智能堆叠放入（t36 拾取消费）。优先序（t74 重排）：
//   0) 所有同 id 未满槽合并（含选中槽）——合并优先于空槽开新。
//   1) 空槽开新栈（选中槽优先，再按索引序）。
// 返回未放入数（0=全入；>0=背包满，caller 据此判「不拾取，entity 留」）。
//
// t74 根因：旧序把「选中槽空→开新栈」排在「合并同 id 槽」前 → 选中槽空时直接开新栈，
// 不查别处已有同 id 未满槽。例：第1槽草(未满) + 第2槽(选中,空) 挖草 → 草应进第1槽却进第2槽，
// 形成同物分散两栈的反直觉结果。新序：合并全程优先于开新，避免「同物分散」。
int Hotbar::addStack(int id, int n, int durability, const QVariantList &enchants)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    // t263 工具段（cap==1）：不可堆叠 → 同 id 槽合并分支永不命中（已有同 id 槽必 count==1==cap）。
    //   故工具只走「空槽开新栈」分支，写入 normalizeDurability 归一的耐久。方块 / 材料段 durability 恒 0（inert）。
    //   t475 enchants 仅工具 / 护甲（cap==1）有意义：随空槽开新栈的实例写入；可堆叠物品合并路径不写附魔（恒 0）。
    const int dur = normalizeDurability(id, durability);
    int remaining = n;
    bool changed = false;

    // 0) 先扫所有已有同 id 未满槽合并（含选中槽）。合并优先于空槽开新（t74）。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_slots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; changed = true;
        }
    }
    // 1) 空槽开新栈：选中槽优先（「入手」语义：选中空时优先入手），再按索引序补其余空槽。
    //    t475 工具 / 护甲（cap==1）空槽开新栈时写入其实例附魔（enchants）；可堆叠物品附魔恒 0（applyEnchants
    //      仍调，写入 0 = 无附魔，行为不变）。
    if (remaining > 0 && m_selectedSlot >= 0 && m_selectedSlot < int(m_slots.size())) {
        ItemStack &sel = m_slots[size_t(m_selectedSlot)];
        if (sel.id == 0) {
            const int add = std::min(cap, remaining);
            sel = ItemStack{id, add, dur}; applyEnchants(sel, enchants); remaining -= add; changed = true;
        }
    }
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        if (int(i) == m_selectedSlot) continue;
        ItemStack &s = m_slots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add, dur}; applyEnchants(s, enchants); remaining -= add; changed = true;
        }
    }
    if (changed) {
        bumpRevision();
        // 选中槽可能被填入新物品（id 变化）→ selectedBlockId 刷新。addStack 拾取场景难预判命中哪槽，
        // 无条件补发（罕见操作，开销可忽）。
        emit selectedSlotChanged();
    }
    qInfo().noquote() << "[inv] addStack id=" << id << " n=" << n << " remaining=" << remaining
                      << " slots=[" << m_slots[0].id << m_slots[1].id << m_slots[2].id << m_slots[3].id
                      << m_slots[4].id << m_slots[5].id << m_slots[6].id << m_slots[7].id << m_slots[8].id << "]";
    return remaining;
}

// ── t97 主栏 VM 栈操作（27 槽，三菜单共享）──

int Hotbar::mainBlockIdAt(int slot) const
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return int(BlockRegistry::Air);
    return m_mainSlots[size_t(slot)].id;
}

int Hotbar::mainCountAt(int slot) const
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return 0;
    return m_mainSlots[size_t(slot)].count;
}

// t263 主栏槽工具剩余耐久（同 durabilityAt 的主栏版；非工具 / 空槽 = 0）。
int Hotbar::mainDurabilityAt(int slot) const
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return 0;
    return m_mainSlots[size_t(slot)].durability;
}

// 直接写入主栏栈（背包点击放置 / 互换 / 拖拽均分写回主栏用）。校验同 setStack；air/非法 id/count<=0 → 清空。
// 主栏槽与 selectedSlot 无关（不驱动 selectedBlockId），故无 selectedSlotChanged 补发。
//   durability（t263）：同 setStack（normalizeDurability 归一）。
//   enchants（t475）：同 setStack（applyEnchants 写入；空栈 / 非可附魔 → 全 0）。
void Hotbar::mainSetStack(int slot, int id, int count, int durability, const QVariantList &enchants)
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return;
    if (!isValidItemId(id)) return;
    ItemStack target;
    if (id != 0 && count > 0) {
        const int cap = maxStackSize(id);
        target = ItemStack{id, std::min(count, cap), normalizeDurability(id, durability)};
        applyEnchants(target, enchants);
    }
    const ItemStack &cur = m_mainSlots[size_t(slot)];
    if (cur.id == target.id && cur.count == target.count && cur.durability == target.durability
        && enchantsEqual(cur, target)) return;
    m_mainSlots[size_t(slot)] = target;
    qInfo().noquote() << "[inv] mainSetStack slot=" << slot << "id=" << target.id << "count=" << target.count
                      << "dur=" << target.durability;
    bumpMainRevision();
}

// 智能堆叠放入主栏（同 id 合并 → 空槽开新）。返回未放入数。仅主栏范围（hotbar 由 addStack / addToAny 管）。
//   durability（t263）：同 addStack（工具不可堆叠 → 只走空槽开新，写入归一耐久）。
//   enchants（t475）：同 addStack（工具 / 护甲空槽开新栈写实例附魔）。
int Hotbar::mainAddStack(int id, int n, int durability, const QVariantList &enchants)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    const int dur = normalizeDurability(id, durability);
    int remaining = n;
    bool changed = false;
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; changed = true;
        }
    }
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add, dur}; applyEnchants(s, enchants); remaining -= add; changed = true;
        }
    }
    if (changed) bumpMainRevision();
    return remaining;
}

// 跨 main + hotbar 的智能堆叠（拾取 / 丢弃回栏合并）。优先序（spec t109）：
//   0) main 同 id 未满槽合并（合并全程优先于开新，避免同物分散两栈；spec「main 同 id 合并已在先」）
//   1) hotbar 同 id 未满槽合并
//   2) 空槽开新栈：**hotbar 优先 → main**
// 返回未放入数。returnHeldToHotbar（关包归还光标）+ pickupScan（世界拾取）改调它 → 拾取 / 丢弃回栏能
// 合并进主栏同 id（修「主栏不同步、丢弃回栏不合并」根因；旧 addStack 只看 hotbar 9 槽）。
//
// t109 根因：旧序「main 空 → hotbar 空」让空手拾取先塞主栏空槽，玩家挖块却要翻主栏找 → 违直觉。
// 拾取应优先落入可直接看见的 hotbar（MC 行为同此），故交换两空槽循环：hotbar 空优先于 main 空。
// main 同 id 合并仍先于 hotbar 同 id（已存在栈就地补满优于跨栏开新，保持「同物不分散」）。
int Hotbar::addToAny(int id, int n, int durability, const QVariantList &enchants)
{
    if (!isValidItemId(id) || id == 0 || n <= 0) return std::max(0, n);
    const int cap = maxStackSize(id);
    // t263 工具段（cap==1）：同 id 合并分支永不命中（已有同 id 槽必满）→ 只走空槽开新；dur 归一。
    //   t475 enchants 仅工具 / 护甲（cap==1）有意义：空槽开新栈写其实例附魔；可堆叠物品合并 / 开新附魔恒 0。
    const int dur = normalizeDurability(id, durability);
    int remaining = n;
    bool mainChanged = false, slotChanged = false;

    // 0) main 同 id 未满槽合并。
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; mainChanged = true;
        }
    }
    // 1) hotbar 同 id 未满槽合并。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_slots[i];
        if (s.id == id && s.count < cap) {
            const int add = std::min(cap - s.count, remaining);
            s.count += add; remaining -= add; slotChanged = true;
        }
    }
    // 2) 空槽开新栈：hotbar 优先 → main（t109：拾取优先 hotbar，玩家挖块直接落在可见栏）。
    for (size_t i = 0; i < m_slots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_slots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add, dur}; applyEnchants(s, enchants); remaining -= add; slotChanged = true;
        }
    }
    for (size_t i = 0; i < m_mainSlots.size(); ++i) {
        if (remaining <= 0) break;
        ItemStack &s = m_mainSlots[i];
        if (s.id == 0) {
            const int add = std::min(cap, remaining);
            s = ItemStack{id, add, dur}; applyEnchants(s, enchants); remaining -= add; mainChanged = true;
        }
    }
    if (mainChanged) bumpMainRevision();
    if (slotChanged) {
        bumpRevision();
        // 选中槽可能被填入新物品（id 变化）→ selectedBlockId 刷新；与 addStack 同理无条件补发。
        emit selectedSlotChanged();
    }
    return remaining;
}

// ── t345 护甲槽 VM（4 槽：头 / 胸 / 腿 / 脚；玩家自身装备，非物品流）──

// 4 装备槽护甲值之和（0..20）。Q_PROPERTY 暴露 → Main.qml 护甲条 + 减伤比例算。
// 空槽 / 非护甲（防御性，不应发生 —— armorSetStack 已守只接护甲）不计。
int Hotbar::totalArmorPoints() const
{
    int total = 0;
    for (const ItemStack &s : m_armorSlots) {
        if (s.id != 0 && ArmorRegistry::isArmor(s.id)) total += ArmorRegistry::armorPoints(s.id);
    }
    return total;
}

int Hotbar::armorBlockIdAt(int slot) const
{
    if (slot < 0 || slot >= int(m_armorSlots.size())) return 0;
    return m_armorSlots[size_t(slot)].id;
}

int Hotbar::armorCountAt(int slot) const
{
    if (slot < 0 || slot >= int(m_armorSlots.size())) return 0;
    return m_armorSlots[size_t(slot)].count;
}

int Hotbar::armorDurabilityAt(int slot) const
{
    if (slot < 0 || slot >= int(m_armorSlots.size())) return 0;
    return m_armorSlots[size_t(slot)].durability;
}

// 直接写入装备槽。slot 范围守；id 须为护甲段（或 0=清空）+ 部位须匹配该槽（头盔槽只接头盔，MC 行为）；
//   count 钳 1（护甲不可堆叠）；durability 经 normalizeDurability 归一。非护甲 / 部位不符 → no-op。
//   id==0 或 count<=0 → 清空该槽（脱下）。部位匹配：slot 索引 == ArmorRegistry::piece(id)。
//   enchants（t475）：护甲可附魔（保护族 / 耐久 / 水上亲和）；装备 / 脱下搬运时透传实例附魔保真。
void Hotbar::armorSetStack(int slot, int id, int count, int durability, const QVariantList &enchants)
{
    if (slot < 0 || slot >= int(m_armorSlots.size())) return;
    if (id == 0 || count <= 0) {
        // 清空（脱下）。
        if (m_armorSlots[size_t(slot)].id == 0) return; // 已空 → 不发信号
        m_armorSlots[size_t(slot)] = ItemStack{0, 0, 0};
        bumpArmorRevision();
        return;
    }
    if (!ArmorRegistry::isArmor(id)) return;            // 非护甲 → 拒（装备槽只接护甲）
    if (ArmorRegistry::piece(id) != slot) return;       // 部位不符 → 拒（头盔不进胸甲槽）
    const int dur = normalizeDurability(id, durability);
    ItemStack ns{id, 1, dur};                           // 护甲不可堆叠 → count 恒 1
    applyEnchants(ns, enchants);                        // t475 写附魔元数据
    m_armorSlots[size_t(slot)] = ns;
    bumpArmorRevision();
}

// 创造调色板护甲段（5 套 × 4 部位 = 20 件；拾取即满耐久单件，供测试 / 直接装备）。Inventory 创造调色板补全用。
QVariantList Hotbar::creativeArmor() const
{
    QVariantList list;
    for (int i = 0; i < int(RecipeRegistry::ArmorCount); ++i)
        list << (int(RecipeRegistry::ArmorIdBase) + i);
    return list;
}

// 受击时每件装备 -1 耐久（spec「DURABILITY degrades on hits」）；归零 → 清空该槽（破损消失）+ emit armorBroken。
//   由 Main.qml 在 takeDamage 路由后调（每受一次击 4 件各 -1，机制等价 MC 护甲耐久损耗）。空槽 / 满耐久不动。
void Hotbar::damageArmor()
{
    bool changed = false;
    for (ItemStack &s : m_armorSlots) {
        if (s.id == 0 || !ArmorRegistry::isArmor(s.id)) continue;
        // t476 耐久附魔：每次受击消耗有 100/(level+1)% 概率被该件护甲忽略（机制等价 MC unbreaking 减损耗概率）。
        const int unb = EnchantRegistry::findLevel(s.enchants, EnchantRegistry::Unbreaking);
        if (unb > 0 && QRandomGenerator::global()->bounded(100) < (100 / (unb + 1)))
            continue; // 本件本次跳过耐久损耗
        if (s.durability <= 1) {
            // 归零 → 护甲破损：清空槽（机制等价 MC「护甲耐久耗尽即消失」）。
            const int brokenId = s.id;
            s = ItemStack{0, 0, 0};
            emit armorBroken(brokenId); // 呈现层播破损音（复用 toolBreak 路径）
            changed = true;
        } else {
            --s.durability;
            changed = true;
        }
    }
    if (changed) bumpArmorRevision();
}

// t377 在世界中右键手持护甲 → 装备 / 互换（spec t377「held armor RIGHT-CLICK = equip/swap」）。
//   选中槽护甲 → 装到对应部位槽；该槽原旧件 → 换回选中槽（手持）。机制等价 MC 1.0 右键装备护甲
//   （空槽装备、占用槽互换）。护甲不可堆叠（count 恒 1）；耐久随实例保真搬运（normalizeDurability 经
//   setStack / armorSetStack 归一）。非护甲选中 → false（caller 回退常规右键 placeBlock）。
bool Hotbar::equipSelectedArmor()
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return false;
    const ItemStack &sel = m_slots[size_t(m_selectedSlot)];
    if (!ArmorRegistry::isArmor(sel.id)) return false;            // 选中非护甲 → 非本路径
    const int piece = ArmorRegistry::piece(sel.id);
    if (piece < 0 || piece >= int(m_armorSlots.size())) return false;
    // 快照新旧护甲（耐久 + 附魔随实例保真搬运；t475）。
    const int newId = sel.id, newDur = sel.durability;
    const QVariantList newEnch = readEnchants(sel);
    const int oldId = m_armorSlots[size_t(piece)].id;
    const int oldDur = m_armorSlots[size_t(piece)].durability;
    const QVariantList oldEnch = readEnchants(m_armorSlots[size_t(piece)]);
    qInfo().noquote() << "[inv] equipSelectedArmor slot=" << m_selectedSlot << "id=" << newId
                      << "->piece=" << piece << " oldId=" << oldId;
    // 清选中槽（取出手持护甲）。
    setStack(m_selectedSlot, 0, 0, 0);
    // 装备新护甲到对应部位槽（armorSetStack 守部位匹配，piece 一致必过；附魔随实例入装备槽）。
    armorSetStack(piece, newId, 1, newDur, newEnch);
    // 旧护甲换回手持选中槽（已空 → 单件入；附魔随实例回背包）；无旧件 → 选中槽留空（手持清空）。
    if (oldId != 0) setStack(m_selectedSlot, oldId, 1, oldDur, oldEnch);
    return true;
}

// 从 slot 取最多 n 件。返回实际取走数；栈空则 id 归 0（保持空栈不变式）。
int Hotbar::takeStack(int slot, int n)
{
    if (slot < 0 || slot >= int(m_slots.size()) || n <= 0) return 0;
    ItemStack &s = m_slots[size_t(slot)];
    if (s.id == 0 || s.count <= 0) return 0;
    const int take = std::min(s.count, n);
    s.count -= take;
    if (s.count <= 0) s = ItemStack{0, 0};
    bumpRevision();
    if (slot == m_selectedSlot) emit selectedSlotChanged(); // 选中栈可能因取空而变 Air
    return take;
}

int Hotbar::maxStackSize(int id) const
{
    // t174 铁桶（材料段 0x206/0x207）：不可堆叠（机制等价 MC 1.0 桶单件；空 / 装水桶均 1）。
    //   须在通用材料段判定**之前**特判（否则落 64）。桶是非堆叠功能性物品（同工具段语义），仅因归材料段
    //   才在此分流。与 isMaterial 不冲突（MaterialIcon 仍画桶图标）。
    if (id == RecipeRegistry::BucketEmptyId || id == RecipeRegistry::WaterBucketId
        || id == RecipeRegistry::LavaBucketId) return 1;
    // t615 附魔书（EnchantedBookId=0x227）：**不可堆叠**（maxStack=1，机制等价 MC 1.0 enchanted book——
    //   每本携带独立附魔列表（enchants 元数据），两本内容不同不可叠；铁砧「两本合并」走 activeOp=combine
    //   而非堆叠）。须在通用材料段判定**之前**特判（否则落 64 → 两本不同附魔的书叠一槽会丢一本的附魔）。
    if (id == RecipeRegistry::EnchantedBookId) return 1;
    // t507 蘑菇汤（MushroomStewId，材料段 0x23C）：不可堆叠（机制等价 MC 1.0 蘑菇汤 maxStack 1 —— 碗装液体
    //   食物不可叠；同铁桶族）。须在通用材料段判定**之前**特判（否则落 64）。食用后返空碗（finishEating 特判）。
    if (id == RecipeRegistry::MushroomStewId) return 1;
    // t345 护甲段（>= ArmorIdBase）：不可堆叠（每件独立耐久，同工具段语义）。
    if (ArmorRegistry::isArmor(id)) return 1;
    if (id >= kMaterialIdBase) return 64; // 材料段（t50 木棒等）：可堆叠 64（MC 标准）
    if (id >= kToolIdBase) return 1;      // 工具段（t33）：不可堆叠
    if (id <= 0 || id >= int(BlockRegistry::Count)) return 0; // air / 越界：不可堆叠（无意义）
    // 方块段走 BlockRegistry::BlockDef.maxStack（t42 单一权威；旧硬编码 64 迁移到表查，行为不变）。
    return BlockRegistry::maxStack(quint8(id));
}

// t263 工具最大耐久（透传 ToolRegistry::maxDurability；非工具 → 0）。QML tooltip 显 max + 创造取件初始化用。
int Hotbar::toolMaxDurability(int id) const
{
    return ToolRegistry::maxDurability(id);
}

// t304 弓箭最大伤害（满蓄力命中 HP；spec「弓伤害 tooltip」）。与 PlayerController::kBowMaxDamage 同源（命名常量
//   在 Physics 层私有，此处给 QML 友好的访问器）。改一处须同步 PlayerController 弓蓄力伤害上界。
int Hotbar::bowArrowMaxDamage() const
{
    return 6; // 满蓄力箭命中 6 HP（3 心）；蓄力 1..6 HP（机制等价 MC 1.0 弓伤害量级）。
}

// t263 消耗选中槽工具 1 点耐久（playercontroller 生存挖掘完成 / 锄耕地调用）。创造由 caller 不调（不消耗）。
//   非工具 / 空槽 → no-op。耐久 >1 → -1 + bumpRevision（tooltip / HUD 刷新）。归零 → 清空槽（工具破损消失）
//   + bumpRevision + 补发 selectedSlotChanged（selectedBlockId 可能因栈空而变 Air）。无返回值（caller 不据之分支）。
void Hotbar::damageSelectedItem()
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return;
    ItemStack &s = m_slots[size_t(m_selectedSlot)];
    if (!ToolRegistry::isTool(s.id) || s.count <= 0) return; // 非工具 / 空槽 → no-op
    // t476 耐久附魔：每次消耗有 100/(level+1)% 概率被忽略（机制等价 MC unbreaking 减损耗概率；工具 / 武器均适用）。
    const int unb = EnchantRegistry::findLevel(s.enchants, EnchantRegistry::Unbreaking);
    if (unb > 0 && QRandomGenerator::global()->bounded(100) < (100 / (unb + 1)))
        return; // 本次跳过耐久损耗
    if (s.durability <= 1) {
        // 归零 → 工具破损：清空槽（机制等价 MC「工具耐久耗尽即消失」，不掉落破损残骸）。
        qInfo().noquote() << "[inv] tool broken slot=" << m_selectedSlot << "id=" << s.id;
        const int brokenId = s.id;
        s = ItemStack{0, 0, 0};
        bumpRevision();
        emit selectedSlotChanged(); // 选中栈变空 → selectedBlockId → Air；player.selectedBlock 刷新
        emit toolBroken(brokenId);  // t315 破损音（Main.qml Connections → AudioManager.playToolBreak）
        return;
    }
    s.durability -= 1;
    qInfo().noquote() << "[inv] tool damage slot=" << m_selectedSlot << "id=" << s.id << "dur=" << s.durability;
    bumpRevision(); // tooltip 「cur/max」+（未来）HUD 耐久条刷新
}

// t474 跨槽材料消耗（附魔台扣青金石）：扫 hotbar + 主栏，凑足 n 件 id 即扣（按槽逐个 takeStack）；
//   凑不足 → 回滚已扣（恢复原态）+ 返 false。成功 → bumpRevision + 返 true。
//   机制等价 MC 附魔台从背包任意位置扣青金石。id<=0 / n<=0 → 返 true（防御；caller 应保证 id/n 合法）。
bool Hotbar::consumeMaterial(int id, int n)
{
    if (id <= 0 || n <= 0) return true; // 防御：无消耗视为成功
    // 先扫一遍算总量，不足直接返 false（不开始扣，免回滚复杂度）。
    int total = 0;
    for (const ItemStack &s : m_slots)        if (s.id == id) total += s.count;
    for (const ItemStack &s : m_mainSlots)    if (s.id == id) total += s.count;
    if (total < n) return false; // 余额不足 → 拒绝（caller 不应推进附魔）
    // 凑足 → 逐槽扣（先 hotbar 后 main；同槽累扣到 count 归 0 即清 id，takeStack 已处理）。
    int remaining = n;
    for (size_t i = 0; i < m_slots.size() && remaining > 0; ++i) {
        if (m_slots[i].id != id || m_slots[i].count <= 0) continue;
        const int took = takeStack(int(i), remaining);
        remaining -= took;
    }
    for (size_t i = 0; i < m_mainSlots.size() && remaining > 0; ++i) {
        if (m_mainSlots[i].id != id || m_mainSlots[i].count <= 0) continue;
        // takeStack 仅作用于 hotbar（slot 索引）；主栏直接写（同 mainSetStack 路径）。
        const int have = m_mainSlots[i].count;
        const int take = std::min(have, remaining);
        if (take >= have) m_mainSlots[i] = ItemStack{0, 0, 0};
        else              m_mainSlots[i].count -= take;
        remaining -= take;
    }
    bumpRevision(); // 驱动 QML 槽显示 + 数量刷新（青金石堆减少；hotbar 段）
    bumpMainRevision(); // t97 主栏也参与（青金石可能在主栏；主栏段 NOTIFY）
    return remaining == 0; // 恒 true（前已判 total>=n），防御性返回
}

// t474 跨槽材料计数（附魔 UI「青金石 N」显示 + 点附魔前置判定）：扫 hotbar + 主栏累加同 id 数量。
//   只读，不改槽态。id<=0 → 0。
int Hotbar::materialCount(int id) const
{
    if (id <= 0) return 0;
    int total = 0;
    for (const ItemStack &s : m_slots)     if (s.id == id) total += s.count;
    for (const ItemStack &s : m_mainSlots) if (s.id == id) total += s.count;
    return total;
}

// t50 合成桥接：QML 不能直接调 C++ 静态 RecipeRegistry，经 VM 透传（VM 属 Game 同层，向下查 RecipeRegistry）。
// slotIds 为 QVariantList<int>（行优先 id，0=空格）；gridSize = 2 / 3。返回匹配配方的 QVariantMap
// （outputId / outputCount / consumeCount）或空 Map（无匹配 / 输入尺寸非法）。
QVariantMap Hotbar::recipeMatch(const QVariantList &slotIds, int gridSize) const
{
    QVariantMap empty;
    if (gridSize < 2 || gridSize > 3) return empty;
    const int n = gridSize * gridSize;
    if (slotIds.size() < n) return empty;
    // 提取 id 到栈缓冲（RecipeRegistry::match 取 const int*；2×2/3×3 最多 9 格）。
    int grid[9] = {0};
    for (int i = 0; i < n; ++i) {
        bool ok = false;
        const int id = slotIds.at(i).toInt(&ok);
        grid[i] = ok ? id : 0;
    }
    const RecipeRegistry::Recipe *r = RecipeRegistry::match(grid, gridSize);
    if (!r) return empty;
    QVariantMap m;
    m.insert(QStringLiteral("outputId"), r->outputId);
    m.insert(QStringLiteral("outputCount"), r->outputCount);
    m.insert(QStringLiteral("consumeCount"), r->consumeCount);
    return m;
}

// t50：产物能否放入光标（空 / 同 id 且累加不超 maxStack）。透传 RecipeRegistry::canTake。
bool Hotbar::recipeCanTake(int outId, int outCount, int heldId, int heldCount, int maxStack) const
{
    // 构造临时 Recipe 走 canTake（canTake 只读 outputId/outputCount，其余字段无关）。
    RecipeRegistry::Recipe r{};
    r.outputId = outId;
    r.outputCount = outCount;
    return RecipeRegistry::canTake(r, heldId, heldCount, maxStack);
}

// t87 冶炼 / 燃料桥接：透传 SmeltingRegistry 静态查询给 QML（FurnaceUI 的 tick / 槽校验消费）。
// 返回 int（产物 id / 燃烧秒数；0 = 不可冶炼 / 不可燃），QML 友好且与 recipeMatch 的整数语义一致。
int Hotbar::smeltResult(int inputId) const
{
    return SmeltingRegistry::smeltResult(inputId);
}

int Hotbar::fuelBurnSeconds(int fuelId) const
{
    return int(SmeltingRegistry::fuelBurnSeconds(fuelId));
}

// t402 冶炼 XP 桥接：透传 SmeltingRegistry::smeltXpReward（FurnaceUI 据产物 id 查单件 XP）。
int Hotbar::smeltXpReward(int outputId) const
{
    return SmeltingRegistry::smeltXpReward(outputId);
}

// 显式重置槽内容（清空 9 hotbar + 27 主栏 + 光标手持物）。t49 引入时由 Main.qml::onModeChanged 在每次
//   模式切换自动调用；t171 取消该自动调用 —— cycleMode 切模式**保留物品**（用户诉求「创造↔生存切换不清空
//   背包」）。本方法保留为显式重置 API（供未来「清空背包」按钮等场景），不再被模式切换触发。
//   mode 沿用 PlayerController::Mode 序数：1=Creative / 2=Survival → 清空；0=Spectator → 不动（观察者
//   hotbar 隐藏，清空无意义；保留数据以便切回创造 / 生存时仍在）。主栏 27 槽同清（VM 共享，不随面板销毁）。
void Hotbar::resetForMode(int mode)
{
    if (mode == 1 || mode == 2) {
        // Creative / Survival：全空（创造源=调色板无限拾取；生存=空背包起，采集/拾取由 t34-t36 填入）。
        for (ItemStack &s : m_slots) s = ItemStack{0, 0};
        for (ItemStack &s : m_mainSlots) s = ItemStack{0, 0};
        // t345：护甲槽一并清空（重生 / 切模式重置时脱下所有装备）。
        for (ItemStack &s : m_armorSlots) s = ItemStack{0, 0};
    }
    // mode==0（Spectator）：不动（hotbar 隐藏，槽内容无意义）。
    m_heldStack = ItemStack{};
    bumpRevision();
    bumpMainRevision();
    bumpArmorRevision();               // t345 护甲槽可能清空 → 装备栏 / 护甲条 / 减伤刷新
    emit heldBlockChanged();           // 手持物被清空 → 浮动图标隐
    emit selectedSlotChanged();        // selectedBlockId 可能因栈变空而变 Air
}

// 光标手持物 id。setHeldBlock(0) 同步清 count + durability；setHeldBlock(非0) 时若 count 为 0 补 1
// （防「有 id 无 count」中间态——QML 拾取整栈时会紧接 setHeldCount 覆盖为真实数量）。
// t263：切到新工具 id 时自动填 maxDurability（创造调色板取件=满耐久场景）；pickup-from-slot 路径
//   在 setHeldBlock 后紧接 setHeldDurability(slot 旧值) 覆盖为槽内实例耐久（InventoryOps.readSlot 透传）。
//   切到非工具 / 非护甲 id 时 durability 归 0（inert）；工具 / 护甲走 normalizeDurability 填满耐久（t263 / t345）。
//   同 id 不变则早退（不动 durability，防覆盖 caller 已设的值）。
void Hotbar::setHeldBlock(int id)
{
    if (!isValidItemId(id)) return;
    if (id == m_heldStack.id) return;
    if (id == 0) {
        m_heldStack = ItemStack{0, 0};
    } else {
        m_heldStack.id = id;
        if (m_heldStack.count <= 0) m_heldStack.count = 1;
        // 新工具 / 护甲实例默认满耐久（创造取件 / 合成产物兜底）；caller 显式覆盖走 setHeldDurability。
        m_heldStack.durability = normalizeDurability(id, -1);
        // t475 切换到新 id → 清空手持附魔（新实例无附魔；caller 显式覆盖走 setHeldEnchants 保真搬运）。
        for (int i = 0; i < 4; ++i) m_heldStack.enchants[i] = 0;
    }
    qInfo().noquote() << "[inv] setHeldBlock -> id=" << id << " count=" << m_heldStack.count
                      << "dur=" << m_heldStack.durability;
    emit heldBlockChanged();
}

void Hotbar::setHeldCount(int n)
{
    if (n < 0) n = 0;
    if (n == m_heldStack.count) return;
    m_heldStack.count = n;
    emit heldBlockChanged();
}

// t263 手持工具耐久 setter：clamp 到 [0, maxDurability(id)]；非工具 id 写入静默归 0（inert）。
//   pickup-from-slot 路径在 setHeldBlock 后调本方法覆盖默认满耐久为槽内实例耐久（保真搬运）。
void Hotbar::setHeldDurability(int d)
{
    const int normalized = normalizeDurability(m_heldStack.id, d);
    if (normalized == m_heldStack.durability) return;
    m_heldStack.durability = normalized;
    emit heldBlockChanged();
}

// t314 `/give` 调试聊天命令（spec t314）：解析 "/give <id> [count] [durability]" → addStack 放入背包 →
//   返回聊天回显文案。debug 命令，**无视游戏模式**（创造 / 生存 / 观察者都可调；不属玩法经济）。
//   args 形如 "3 64" / "<swordId> 1 100" / "8" / ""。物品 id 校验：必须落在方块段 / 工具段 / 材料段任一
//   **已注册**区间（nameForBlock 返非空 = 已注册名）。count 缺省 1、durability 缺省 -1（自动：工具满耐久、
//   非工具 0）。越段 / 未注册 / count<1 / durability<1 → 不改背包，返错误文案。
//   分层：本方法是 ViewModel 入口（聊天经 QML sendChat 路由），向下调本类 addStack + nameForBlock，
//   不依赖 World / Renderer。物品名走 nameForBlock（§9 通用词，零 MC 专名）。
QString Hotbar::give(const QString &args)
{
    const QStringList parts = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return QStringLiteral("用法: /give <id> [count] [durability]");
    }
    // 第一段：物品 id（方块段 / 工具段 / 材料段任一）。非整数 / 越段 / 未注册 → 未知物品。
    bool okId = false;
    const int id = parts.at(0).toInt(&okId);
    if (!okId || !isValidItemId(id)) {
        return QStringLiteral("未知物品 id: %1").arg(parts.at(0));
    }
    // isValidItemId 只判 id 段（方块段 / 工具段 / 材料段基址），不保证「已注册」—— 材料段 >= 0x200
    //   区间内仍有未分配 id（如 0x220）。nameForBlock 返空 = 未注册名 → 视作未知物品（debug 命令拒
    //   写入未注册 id 防误触 / 静默造数据）。
    const QString name = nameForBlock(id);
    if (name.isEmpty()) {
        return QStringLiteral("未知物品 id: %1").arg(parts.at(0));
    }
    // 第二段：count（缺省 1）。非整数 / <1 → 用法错（不允许给 0 或负数）。
    int count = 1;
    if (parts.size() >= 2) {
        bool okCount = false;
        const int parsed = parts.at(1).toInt(&okCount);
        if (!okCount || parsed < 1) {
            return QStringLiteral("用法: /give <id> [count] [durability]");
        }
        count = parsed;
    }
    // 第三段：durability（缺省 -1 = 自动：工具满耐久 / 非工具 0）。显式指定时须 >=1（normalizeDurability
    //   会 clamp 到 [1, maxDurability]）；非工具 id 写 durability 会被 normalizeDurability 归 0 inert。
    int durability = -1;
    if (parts.size() >= 3) {
        bool okDur = false;
        const int parsed = parts.at(2).toInt(&okDur);
        if (!okDur || parsed < 1) {
            return QStringLiteral("用法: /give <id> [count] [durability]");
        }
        durability = parsed;
    }
    // 多余参数（>=4 段）忽略（容忍用户多敲空格 / 后续参数；机制等价 MC /give 容错）。
    // addStack 内部按 maxStackSize 钳制（工具段 cap=1 → 每槽单件；材料 / 方块段 cap=64）。返回未放入数
    //   （背包满时 >0）。actual = count - remaining = 实际入背包数。
    const int remaining = addStack(id, count, durability);
    const int actual = count - remaining;
    if (actual <= 0) {
        return QStringLiteral("给予玩家 %1 ×%2 失败：背包已满").arg(name).arg(count);
    }
    // t346 输出文案：成功「给予玩家 <名> ×<n>」；显式耐久（第三段已给）+ 工具 → 耐久变体「给予玩家耐久为
    //   <dur> 的 <名> ×<n>」。<dur> = normalizeDurability clamp 后实际写入值；非工具显式耐久 inert（归 0）
    //   → 不显耐久变体（避免「耐久为 0」误导）。
    QString msg;
    if (durability > 0 && isTool(id)) {
        msg = QStringLiteral("给予玩家耐久为 %1 的 %2 ×%3")
                  .arg(normalizeDurability(id, durability)).arg(name).arg(actual);
    } else {
        msg = QStringLiteral("给予玩家 %1 ×%2").arg(name).arg(actual);
    }
    if (remaining > 0) msg += QStringLiteral("（背包已满，弃 %1）").arg(remaining);
    return msg;
}

// ── t475 附魔桥接 + 槽位附魔元数据 + 附魔选中槽 ──

int Hotbar::itemEnchantCategory(int itemId) const
{
    return EnchantRegistry::categoryForItem(itemId);
}

bool Hotbar::isEnchantable(int itemId) const
{
    return EnchantRegistry::categoryForItem(itemId) != EnchantRegistry::None;
}

QString Hotbar::enchantDisplayName(int enchantId) const
{
    return EnchantRegistry::displayName(enchantId);
}

int Hotbar::enchantMaxLevel(int enchantId) const
{
    return EnchantRegistry::maxLevel(enchantId);
}

QString Hotbar::enchantLevelText(int level) const
{
    return EnchantRegistry::levelSuffix(level);
}

QVariantList Hotbar::selectEnchantsPreview(int category, int offeredLevel, int seed) const
{
    return EnchantRegistry::selectEnchants(category, offeredLevel, seed);
}

// t615 附魔适用 / 冲突精判（透传 EnchantRegistry；铁砧敲附魔书逐条过滤，详见 .h 注释）。
bool Hotbar::enchantApplicableTo(int enchantId, int itemId) const
{
    return EnchantRegistry::isApplicableForItem(enchantId, itemId);
}

bool Hotbar::enchantConflictsWith(int enchantIdA, int enchantIdB) const
{
    return EnchantRegistry::conflictsWith(enchantIdA, enchantIdB);
}

// t590 附魔列表文本（tooltip 显示「物品有什么附魔」）：输入 4 槽 packed int（同 ItemStack.enchants[4] 布局，
//   即 enchantsAt / mainEnchantsAt / armorEnchantsAt 返回格式）→ 逐槽拆包 id/level → 「锐锋 III」「效率 II」
//   … 以换行连接；无附魔 / 非附魔 id → 跳过该槽。工具 / 护甲 tooltip 附魔行显示用（PLAN §9：附魔名走注册表
//   原创中文通用词，非 MC 专名；等级罗马数字由 EnchantRegistry::levelSuffix 出）。
QString Hotbar::enchantListText(const QVariantList &packed) const
{
    QString out;
    const int n = std::min(int(packed.size()), 4);
    for (int i = 0; i < n; ++i) {
        const int p = packed.at(i).toInt();
        if (p <= 0) continue;
        const int id = EnchantRegistry::packEnchantId(p);
        if (!EnchantRegistry::isEnchant(id)) continue;
        if (!out.isEmpty()) out += QLatin1Char('\n');
        out += EnchantRegistry::displayName(id);
        const QString suffix = EnchantRegistry::levelSuffix(EnchantRegistry::packLevel(p));
        if (!suffix.isEmpty()) out += QLatin1Char(' ') + suffix;
    }
    return out;
}

// 光标手持物附魔（QVariantList<int> 4 元素，每 = pack 值；空手 / 非可附魔 → 4 个 0）。
QVariantList Hotbar::heldEnchants() const
{
    return readEnchants(m_heldStack);
}

// 手持物附魔 setter：clamp 写入 m_heldStack.enchants[4]（列表不足 4 按 0 补齐 = 清空）。
//   pickup-from-slot 路径在 setHeldBlock 后调本方法覆盖默认空附魔为槽内实例附魔（保真搬运）。
void Hotbar::setHeldEnchants(const QVariantList &enchants)
{
    ItemStack snapshot = m_heldStack;
    applyEnchants(snapshot, enchants);
    if (enchantsEqual(snapshot, m_heldStack)) return; // 无变化 → 不发信号
    for (int i = 0; i < 4; ++i) m_heldStack.enchants[i] = snapshot.enchants[i];
    emit heldBlockChanged();
}

QVariantList Hotbar::enchantsAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return {0, 0, 0, 0};
    return readEnchants(m_slots[size_t(slot)]);
}

QVariantList Hotbar::mainEnchantsAt(int slot) const
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return {0, 0, 0, 0};
    return readEnchants(m_mainSlots[size_t(slot)]);
}

QVariantList Hotbar::armorEnchantsAt(int slot) const
{
    if (slot < 0 || slot >= int(m_armorSlots.size())) return {0, 0, 0, 0};
    return readEnchants(m_armorSlots[size_t(slot)]);
}

// ── t477 自定义名读写（铁砧重命名）── 同 durabilityAt 模式（越界 → 空串）。
QString Hotbar::customNameAt(int slot) const
{
    if (slot < 0 || slot >= int(m_slots.size())) return QString();
    return m_slots[size_t(slot)].customName;
}
QString Hotbar::mainCustomNameAt(int slot) const
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return QString();
    return m_mainSlots[size_t(slot)].customName;
}
void Hotbar::setCustomName(int slot, const QString &name)
{
    if (slot < 0 || slot >= int(m_slots.size())) return;
    ItemStack &s = m_slots[size_t(slot)];
    if (s.isEmpty()) return;              // 空槽不写名
    s.customName = name.trimmed();        // 去首尾空白（防误输入全空格）
    ++m_slotRevision; emit slotsChanged();
    if (slot == m_selectedSlot) emit selectedSlotChanged(); // 驱动 nameAt / HUD 名刷新
}
void Hotbar::mainSetCustomName(int slot, const QString &name)
{
    if (slot < 0 || slot >= int(m_mainSlots.size())) return;
    ItemStack &s = m_mainSlots[size_t(slot)];
    if (s.isEmpty()) return;
    s.customName = name.trimmed();
    emit mainSlotsChanged();
}

// ── t477 铁砧三功能内部辅助 ──

// 工具 / 护甲的最大耐久（供修复算 max 上限）；非可修复物品 → 0。单一权威：工具走 ToolRegistry、护甲走
//   ArmorRegistry（二者 maxDurability 各持；本 helper 按段分流，避免 caller 自写两套判定）。
static int repairMaxDurability(int itemId)
{
    if (ToolRegistry::isTool(itemId))    return ToolRegistry::maxDurability(itemId);
    if (ArmorRegistry::isArmor(itemId))  return ArmorRegistry::maxDurability(itemId);
    return 0;
}

// 在 hotbar（除选中槽）+ 主栏找一件同 id 第二件（count>0）。找到 → 返回 true 并经 outGroup/outIndex 写
//   位置（'h'=hotbar / 'm'=main）；调用方据之取栈引用。找不到 → false。excludeSel = 跳过的 hotbar 槽
//   （铁砧操作目标 = 选中槽，第二件不能是它自己）。
static bool findSecondSameId(const std::vector<ItemStack> &hotSlots,
                             const std::vector<ItemStack> &mainSlots, int id, int excludeSel,
                             char &outGroup, int &outIndex)
{
    for (size_t i = 0; i < hotSlots.size(); ++i) {
        if (int(i) == excludeSel) continue;
        if (hotSlots[i].id == id && hotSlots[i].count > 0) { outGroup = 'h'; outIndex = int(i); return true; }
    }
    for (size_t i = 0; i < mainSlots.size(); ++i) {
        if (mainSlots[i].id == id && mainSlots[i].count > 0) { outGroup = 'm'; outIndex = int(i); return true; }
    }
    return false;
}

// 取组:下标 对应栈引用（'h'=hotbar / 'm'=main）。范围已由 findSecondSameId 保证。
static ItemStack &secondRef(std::vector<ItemStack> &hotSlots, std::vector<ItemStack> &mainSlots,
                            char group, int index)
{
    return group == 'h' ? hotSlots[size_t(index)] : mainSlots[size_t(index)];
}

bool Hotbar::anvilCanRepairSelected() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return false;
    const ItemStack &sel = m_slots[size_t(m_selectedSlot)];
    if (sel.isEmpty()) return false;
    if (repairMaxDurability(sel.id) <= 0) return false; // 非工具 / 护甲不可修复
    char g = 0; int idx = 0;
    return findSecondSameId(m_slots, m_mainSlots, sel.id, m_selectedSlot, g, idx);
}

bool Hotbar::anvilDoRepairSelected()
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return false;
    ItemStack &sel = m_slots[size_t(m_selectedSlot)];
    if (sel.isEmpty()) return false;
    const int maxD = repairMaxDurability(sel.id);
    if (maxD <= 0) return false;
    char g = 0; int idx = 0;
    if (!findSecondSameId(m_slots, m_mainSlots, sel.id, m_selectedSlot, g, idx)) return false;
    ItemStack &second = secondRef(m_slots, m_mainSlots, g, idx);
    // 机制等价 MC 1.0 铁砧修复：合并耐久 + 12% max 加成，clamp 到 max。
    const int repaired = std::min(maxD, sel.durability + second.durability + maxD / 8);
    sel.durability = repaired;
    // 消耗 1 件第二件（工具 / 护甲 count 恒 1 → 取 1 即清空该槽）。
    if (second.count > 1) second.count -= 1;
    else                  second = ItemStack{0, 0, 0};
    ++m_slotRevision; emit slotsChanged();
    if (g == 'm') emit mainSlotsChanged();
    return true;
}

bool Hotbar::anvilCanMergeEnchantsSelected() const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return false;
    const ItemStack &sel = m_slots[size_t(m_selectedSlot)];
    if (sel.isEmpty()) return false;
    if (EnchantRegistry::categoryForItem(sel.id) == EnchantRegistry::None) return false; // 非可附魔
    char g = 0; int idx = 0;
    if (!findSecondSameId(m_slots, m_mainSlots, sel.id, m_selectedSlot, g, idx)) return false;
    const ItemStack &second = (g == 'h') ? m_slots[size_t(idx)] : m_mainSlots[size_t(idx)];
    // 第二件须带至少一个附魔。
    for (int i = 0; i < 4; ++i) if (second.enchants[i] != 0) return true;
    return false;
}

bool Hotbar::anvilDoMergeEnchantsSelected()
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return false;
    ItemStack &sel = m_slots[size_t(m_selectedSlot)];
    if (sel.isEmpty()) return false;
    if (EnchantRegistry::categoryForItem(sel.id) == EnchantRegistry::None) return false;
    char g = 0; int idx = 0;
    if (!findSecondSameId(m_slots, m_mainSlots, sel.id, m_selectedSlot, g, idx)) return false;
    ItemStack &second = secondRef(m_slots, m_mainSlots, g, idx);
    bool merged = false;
    // 逐附魔：第二件每个附魔，选中件已有同 id → 取 max 等级；否则写入首个空槽（≤4）。
    //   机制等价 MC 1.0 铁砧附魔合并（同附魔取高等级、新附魔加入）。
    for (int i = 0; i < 4; ++i) {
        const int packed = second.enchants[i];
        if (packed == 0) continue;
        const int eid = EnchantRegistry::packEnchantId(packed);
        const int lvl = EnchantRegistry::packLevel(packed);
        int slot = -1;
        for (int j = 0; j < 4; ++j) {
            if (EnchantRegistry::packEnchantId(sel.enchants[j]) == eid) { slot = j; break; }
        }
        if (slot >= 0) {
            const int cur = EnchantRegistry::packLevel(sel.enchants[slot]);
            sel.enchants[slot] = EnchantRegistry::pack(eid, std::max(cur, lvl));
            merged = true;
        } else {
            for (int j = 0; j < 4; ++j) {
                if (sel.enchants[j] == 0) { sel.enchants[j] = EnchantRegistry::pack(eid, lvl); merged = true; break; }
            }
        }
    }
    if (!merged) return false; // 第二件无附魔（理论 anvilCanMerge 已守；防御）
    // 消耗第二件（count 1 → 清空）。
    if (second.count > 1) second.count -= 1;
    else                  second = ItemStack{0, 0, 0};
    ++m_slotRevision; emit slotsChanged();
    if (g == 'm') emit mainSlotsChanged();
    return true;
}

bool Hotbar::anvilDoRenameSelected(const QString &name)
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return false;
    ItemStack &sel = m_slots[size_t(m_selectedSlot)];
    if (sel.isEmpty()) return false;
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) return false;
    sel.customName = trimmed;
    ++m_slotRevision; emit slotsChanged();
    emit selectedSlotChanged(); // 驱动 nameAt / HUD 名刷新（显重命名）
    return true;
}

// ── t550 铁砧二轮重做：修复材料映射（纯查询，QML 判预览 / 消耗；机制等价 MC 1.0 铁砧修复材料）──
//   木档工具→木板 / 石档→圆石 / 铁档→铁锭 / 钻石档→钻石；弓→线、剪刀→铁锭、钓竿→线（MC 1.0 无剪刀/钓竿
//   修复 — 本工程给合理映射便于铁砧修复全工具集）；护甲→同材质锭（皮革护甲→皮革 / 铁→铁锭 / 铜→铜锭 /
//   金→金锭 / 钻→钻石）。不可修复物品（方块 / 材料段 / 弓剪竿以外工具段…）→ 0。
int Hotbar::anvilRepairMaterial(int itemId) const
{
    if (const ToolRegistry::ToolDef *t = ToolRegistry::tool(itemId)) {
        switch (t->type) {
        case BlockRegistry::Pickaxe:
        case BlockRegistry::Hoe:
        case BlockRegistry::Axe:
        case BlockRegistry::Shovel:
        case BlockRegistry::Sword:
            switch (t->tier) {
            case 1:  return int(BlockRegistry::Planks);        // 木档 → 木板
            case 2:  return int(BlockRegistry::Cobble);        // 石档 → 圆石
            case 3:  return int(RecipeRegistry::IronIngotId);  // 铁档 → 铁锭
            case 4:  return int(RecipeRegistry::DiamondId);    // 钻石档 → 钻石
            case 5:  return int(RecipeRegistry::GoldIngotId);  // t557 金档 → 金锭
            case 6:  return int(RecipeRegistry::CopperIngotId); // t557 铜档 → 铜锭
            default: return 0;
            }
        case BlockRegistry::Bow:          return int(RecipeRegistry::StringId);       // 弓 → 线
        case BlockRegistry::Shears:       return int(RecipeRegistry::IronIngotId);    // 剪刀 → 铁锭
        case BlockRegistry::FishingRod:   return int(RecipeRegistry::StringId);       // 钓竿 → 线
        default:                          return 0;
        }
    }
    if (ArmorRegistry::isArmor(itemId)) {
        switch (ArmorRegistry::tier(itemId)) {
        case ArmorRegistry::Leather:  return int(RecipeRegistry::LeatherId);
        case ArmorRegistry::Iron:     return int(RecipeRegistry::IronIngotId);
        case ArmorRegistry::Copper:   return int(RecipeRegistry::CopperIngotId);
        case ArmorRegistry::Gold:     return int(RecipeRegistry::GoldIngotId);
        case ArmorRegistry::Diamond:  return int(RecipeRegistry::DiamondId);
        default:                      return 0;
        }
    }
    return 0;
}

bool Hotbar::anvilCanRepairMaterial(int itemId, int materialId) const
{
    return anvilRepairMaterial(itemId) == materialId;
}

// t476 选中槽物品附魔等级（供 Game 层 attack / mining calc point 直读）。空槽 / 越界 / 非可附魔 → 0。
int Hotbar::selectedItemEnchantLevel(int enchantId) const
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return 0;
    return EnchantRegistry::findLevel(m_slots[size_t(m_selectedSlot)].enchants, enchantId);
}

// t476 4 装备槽某附魔等级之和（耐久 / 保护族 EPF 累加用；空槽不计）。
int Hotbar::armorEnchantLevelSum(int enchantId) const
{
    int sum = 0;
    for (const ItemStack &s : m_armorSlots) {
        if (s.id == 0) continue;
        sum += EnchantRegistry::findLevel(s.enchants, enchantId);
    }
    return sum;
}

// t476 受击减伤 EPF（机制等价 MC 1.0 Enchantment Protection Factor）。PlayerState::DeathCause 序数：
//   Generic=0/Fall=1/Suffocation=2/Drowning=3/Starvation=4/Shambler=5/Bones=6/Spider=7/Stalker=8/Fire=9/Cactus=10。
//   通用 Protection（每级 1 EPF）对所有来源生效；专项保护（每级 2 EPF）仅对匹配来源生效：
//     Fall→摔落保护 / Fire→火焰保护 / Bones(骷髅箭)→弹射物保护 / Stalker(爆炸)→火焰保护（MC 火焰保护亦减爆炸）。
//   EPF 总和交 caller cap（≤0.85 减伤比）+ 换算减伤比例；本方法只汇总 EPF 原始值。
int Hotbar::armorProtectionFactor(int cause) const
{
    int epf = armorEnchantLevelSum(EnchantRegistry::Protection); // 通用：1 EPF/级
    switch (cause) {
    case 1:  epf += armorEnchantLevelSum(EnchantRegistry::FeatherFall) * 2; break;    // Fall
    case 9:  epf += armorEnchantLevelSum(EnchantRegistry::FireProtection) * 2; break; // Fire
    case 6:  epf += armorEnchantLevelSum(EnchantRegistry::ProjectileProt) * 2; break; // Bones = 骷髅箭（弹射物）
    case 8:  epf += armorEnchantLevelSum(EnchantRegistry::FireProtection) * 2; break; // Stalker 爆炸（火焰保护亦减爆炸）
    case 11: epf += armorEnchantLevelSum(EnchantRegistry::FireProtection) * 2; break; // t494 Tnt 爆炸（同 Stalker：火焰保护亦减爆炸）
    default: break;
    }
    return epf;
}

// t475 附魔选中槽物品（附魔台点选项槽 → 写附魔元数据）。机制等价 MC 1.0 附魔台点槽即附魔。
//   选中槽空 / 非可附魔 / 已有附魔 → no-op（返 false；UI 应已门控，MC 1.0 不允许重复附魔已附魔物品）。
//   否则 selectEnchants(category, offeredLevel, seed) → 清空旧 enchants[4] + 填新（最多 4 个）。
//   不改 id / count / durability（附魔是叠加元数据，非替换物品）。
bool Hotbar::enchantSelected(int offeredLevel, int seed)
{
    if (m_selectedSlot < 0 || m_selectedSlot >= int(m_slots.size())) return false;
    ItemStack &s = m_slots[size_t(m_selectedSlot)];
    if (s.id == 0 || s.count <= 0) return false;
    const int category = EnchantRegistry::categoryForItem(s.id);
    if (category == EnchantRegistry::None) return false;
    // MC 1.0：已附魔物品不能再进附魔台（防重复附魔刷属性）。
    bool hasEnchant = false;
    for (int i = 0; i < 4; ++i) if (s.enchants[i] != 0) { hasEnchant = true; break; }
    if (hasEnchant) return false;

    const QVariantList picks = EnchantRegistry::selectEnchants(category, offeredLevel, seed);
    // 清空旧 + 填新（selectEnchants 已 ≤ 4 个 + 已剔互斥 / 重复）。
    for (int i = 0; i < 4; ++i) s.enchants[i] = 0;
    for (int i = 0; i < int(picks.size()) && i < 4; ++i) {
        const QVariantMap m = picks.at(i).toMap();
        const int eid = m.value(QStringLiteral("id")).toInt();
        const int lvl = m.value(QStringLiteral("level")).toInt();
        s.enchants[i] = EnchantRegistry::pack(eid, lvl);
    }
    qInfo().noquote() << "[inv] enchantSelected slot=" << m_selectedSlot << "id=" << s.id
                      << "offered=" << offeredLevel << "seed=" << seed << "count=" << picks.size();
    bumpRevision();
    emit selectedSlotChanged(); // 选中槽附魔变 → 附魔台 / tooltip 显示刷新
    return true;
}
