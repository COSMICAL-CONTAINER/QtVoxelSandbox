#ifndef BLOCKREGISTRY_H
#define BLOCKREGISTRY_H

#include <QtGlobal> // quint8
#include <QString>  // displayName() 返回 QString（用户可见中文名）
#include <vector>   // collisionAABBs/selectionAABBs 返回 std::vector<BlockAABB>

// 方块注册表（单一权威数据源；Core 层）。
//
// 把「方块 id → 外观瓦片 / 是否实体 / 中文名 / 硬度 / 采掘要求 / 掉落 / 堆叠上限」全部
// 收敛到**一处** BlockDef 表，取代散落在 chunkgeometry::tileFor、toolregistry::kBlockMine、
// hotbar::kBlockMaxStack 里的硬编码。mesher(Renderer) / worldgen(World) / 挖掘系统(Game) /
// 背包 Hotbar(ViewModel) 都**只读**本表，不得各持副本（PLAN §2：世界数据单一）。
//
// 分层（PLAN §2）：本层属 Core（仅依赖 QtGlobal/QString），**不得**依赖
// Renderer/QtQuick3D/Physics/QtQuick/World/Game。依赖只向下。ToolType 枚举放本层是因为
// 「方块需要何种工具采掘」本质是方块的属性；ToolRegistry(Game) 复用之（向下依赖 Core）。
//
// 方块 id（稳定可引用；worldgen/网格/存档都按 id 引用，勿随意改顺序/插值）：
//   0=air 1=grass 2=dirt 3=stone 4=cobble 5=log 6=planks 7=leaves 8=sand 9=crafting_table
//   10=furnace 11=coal_ore 12=iron_ore 13=torch 14=bedrock ... 21=water 22=chest 23=farmland
//   24=tall_grass（草丛；cross 广告牌方块）。25=wheat_crop（小麦作物；cross + state=生长阶段 0..7）。
// air 恒 solid=false / hardness=0 / 不掉落。方块名用通用词，零 MC 专有名词（PLAN §9）。
class BlockRegistry
{
public:
    // 方块 id（与体素栅格的 quint8 存储对齐；底层类型 quint8 便于直接赋给栅格）。
    enum Id : quint8 {
        Air           = 0,
        Grass         = 1,
        Dirt          = 2,
        Stone         = 3,
        Cobble        = 4,
        Log           = 5,
        Planks        = 6,
        Leaves        = 7,
        Sand          = 8,
        CraftingTable = 9, // 工作台：右键打开 3×3 合成（t50）；机制等价 MC 工作台，名称/贴图原创（§9）。
        Furnace       = 10, // 熔炉：8 圆石围圈合成（t80）；机制等价 MC 熔炉，名称/贴图原创（§9）。
        CoalOre       = 11, // 煤矿石：散布于 stone 区段（t84）；机制等价 MC 煤矿，名称/贴图原创（§9）。
        IronOre       = 12, // 铁矿石：散布于 stone 区段（t84）；机制等价 MC 铁矿，名称/贴图原创（§9）。
        Torch         = 13, // 火把：伪光源方块（t88）；机制等价 MC 火把。solid=false 非实体碰撞、
                           // hardness=0 瞬破、NoTool；掉落自身。**视觉光源走伪光源**（Main.qml 发光
                           // Model + 光晕，NoLighting 高 baseColor 暖色），**非** QtQuick3D PointLight
                           // （lit 材质渲染红线，lessons-learned；真 flood-fill 方块光留 PLAN §M）。
        Bedrock       = 14, // 基岩：世界底层方块（t119）；机制等价 MC 基岩。solid=true 实体碰撞、
                           // **hardness=-1.0**（负值 → ToolRegistry::canMine 自动 false → 生存不可破：
                           // updateMining 内 if(canMine&&progress>=1.0) 守 finishMiningAt；t141 后创造可瞬破，
                           // beginMining 守卫已移除）、dropId=0（破不掉落）、各面同贴图（tile 18）。
                           // worldgen 在 y 0..4 按 hashVoxel 坑洼铺一层（底实顶疏）。
        // ── t134 不完整方块（异形几何段，id ∈ [FirstPartial, LastPartial]）：6 类木制半方块，机制等价 MC 1.0
        //   (id, metadata) 方块模型。solid=false（同 torch：非整立方 → 不挡邻居面剔除，避免相邻整立方
        //   被错误剔除出「洞」；逐形状精确碰撞留后续任务）。各面同贴图=planks(8)、hardness=2.0（木质）、
        //   NoTool（空手可采且掉落）。掉落自身。mesher 经 PartialBlockGeometry::append 按 (id,state) 生成
        //   异形顶点并合批进 chunk mesh。state 编码朝向 / 开合 / 半位（见各枚举注释 + partialblockgeometry.cpp）。
        WoodSlab          = 15, // 木板台阶：半高（上/下半）。state bit0 = 上半(1)/下半(0)。
        WoodStairs        = 16, // 木板楼梯：整步 + 背墙。state[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z（楼梯朝该向开）；bit2=上下倒置（整步在上、背墙在下）。
        WoodFence         = 17, // 木栅栏：中心立柱（0.4 见方 × 1.5 高）+ 四向横档连邻居（t209）。state=0
                                 //   （连接由 mesher 读水平邻居 id 运行期决定，非 state 编码；机制等价 MC栅栏）。
        WoodPressurePlate = 18, // 木板压力板：贴地薄板。state=0。
        WoodDoor          = 19, // 木板门：两格高（下/上格同 id）。state: bit3=上格(1)/下格(0)、bit2=开(1)/合(0)、
                                //   bit[1:0]=朝向 0=+X 1=-X 2=+Z 3=-Z。maxStack=1（单件，不可堆叠）。
        WoodTrapdoor      = 20, // 木板活板门：水平/竖直薄板。state bit0=开(1，竖直贴边)/合(0，水平贴地)，
                                //   bit[2:1]=开时朝向 0=+X 1=-X 2=+Z 3=-Z。
        Water              = 21, // 水（t148/t174）：机制等价 MC 1.0 流水。solid=false（不挡邻居面剔除 → 地形贴着水
                                  //   仍画自己的面）、shape=ShapeNone（**无碰撞** → 玩家穿过；t174 浮力/游泳走
                                  //   PlayerController 水中物理分支，非碰撞）、**hardness=-1.0**（不可挖掘：
                                  //   canMine=false，任何模式/工具不破，防创造秒破；同 bedrock 哨兵语义）、dropId=0
                                  //   （破不掉落）、各面同贴图=water(19) 蓝半透。
                                  //   worldgen 在 waterLevel 以下低洼列填水（h<wl 从 h+1 到 wl）—— 全作**水源**
                                  //   （state=0）。渲染：mesher 把水面剔出独立几何段、材质 opacity=0.7 半透；
                                  //   水-水邻接面互剔（nb==Water 剔除）。不进创造调色板（worldgen 专属；非玩家可放置
                                  //   方块 —— 玩家经铁桶舀/倒水交互，t174）。
                                  //   **t174 水流 state 编码**（复用 chunk m_states 并行数组；与不完整方块 state 同存储）：
                                  //     state 0 = 水源（无限；worldgen 填 / 玩家铁桶倒）
                                  //     state 1..7 = 流水（距水源的蔓延距离；MC 式扩散，最大 7 格水平距离）。
                                  //   World::tickWaterFlow()（t185 重做为增量波前）每 ~0.3s 把波前推进 1 格：水源/流水
                                  //   grounded（下方实体）→ 水平蔓延 state+1（≤7）；悬崖边（下方 air）→ 下落为流水 state=1
                                  //   （**非水源**）；流水失支撑（水源被舀/隔断）→ 逐环蒸发。1 格/tick 流动动画可见、不灌满
                                  //   整个平面。**t197 水位视觉**：state 同时驱动渲染——mesher 水段按 state 降水面高度
                                  //   （水源 1.0 / 流水 (8-level)/8 逐级降），流水(state>0) 各面用 water_flow(23) 贴图，
                                  //   水源(state=0) 用 water(19)。水面高度仅由 mesher 渲染层读取 state 计算，不进 BlockDef
                                  //   （本表方块 id=21 仍各面=19 静水；流水贴图是 mesher 呈现层选择，非方块属性）。
        Chest          = 22, // 箱子（t173）：机制等价 MC 1.0 箱子。solid=true / ShapeFull（整立方实体碰撞、
                                  //   mesher 正常画 6 面顶/侧/前贴图=chest_top/side/front，与工作台 / 熔炉同走整立方
                                  //   渲染路径 —— 非异形，**不**进 PartialBlockGeometry）、hardness=2.5（木制）、NoTool
                                  //   （空手可采且掉落自身）、各面贴图 chest_top(20)/chest_side(21)/chest_front(22)。
                                  //   右键打开 27 槽物品栏 UI（playercontroller 发 chestOpened 信号 → Main.qml 开
                                  //   ChestUI + 盖子开合动画）；**物品内容存 ChestStore（Game 层，按方块坐标键控）**，
                                  //   spec「物品存 chunk state」= 物品随方块存（世界级 block-instance state，非 QML
                                  //   面板本地态，机制等价 MC「箱子内容存于方块」；多箱子各自独立 27 槽）。破块时
                                  //   ChestStore.clearChest 清条目（内容不退回玩家背包，机制等价 MC 破箱掉落本属 1.1+）。
        Farmland       = 23, // 耕地（t234）：机制等价 MC 1.0 耕地。持锄右键泥土/草方块→变耕地（playercontroller
                                  //   useBlock 分支：检手持为 Hoe 工具 + 命中格 Dirt/Grass → setBlock(Farmland, moist)）。
                                  //   solid=true / ShapeFull（整立方 opaque，正常参与邻居面剔除 + 走 culled 立方面渲染，
                                  //   **非**异形 —— 不进 PartialBlockGeometry；与 Chest 同走段后整立方路径）、hardness=0.6
                                  //   （同 grass/dirt 量级，NoTool 空手可采）、dropId=Dirt（破耕地掉泥土，机制等价 MC
                                  //   「耕地破坏返泥土」，非掉耕地自身）、dropCount=1、maxStack=64。各面贴图：顶=farmland_dry(26)
                                  //   或 farmland_wet(27)（mesher 据 state bit0 选，见 FarmlandMoistBit）/ 侧·底=dirt(2)。
                                  //   **碰撞略矮 0.9375（15/16）**：collisionAABBs 对 Farmland 特例返 {0,0,0,1,0.9375,1}
                                  //   （机制等价 MC 耕地碰撞箱矮 1 像素；selectionAABBs 仍走 ShapeFull 整格，选中框不缩，
                                  //   raycast 经 isFullCube=true 走整格命中 —— 三者解耦：碰撞矮、选中满、射线整格，零相互干扰）。
                                  //   state 编码干/湿（见 FarmlandMoistBit），由 playercontroller 耕地时据水源邻近判定写入。
        WheatCrop      = 25, // 小麦作物（t236）：机制等价 MC 1.0 小麦作物（wheat crop）。**cross 形广告牌方块**
                                  //   （与 TallGrass 同走 PartialBlockGeometry 的 cross 几何段 [FirstCross, LastCross]；
                                  //   两片对角相交的双面 quad，alpha 透明底 cutout）。**生长阶段存 chunk state**（state = 阶段
                                  //   0..7；0=刚种下的嫩芽、7=成熟可收割），WorldClock tick 推进成长（world.tickCropGrowth；
                                  //   每株据光强 + 耕地支撑 + 确定性散布概率逐步升阶段）。solid=false（非实体 → 不挡邻居面剔除，
                                  //   同 torch / 草丛）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 作物可踩过 — 踩踏
                                  //   退化耕地留后续任务）、hardness=0（瞬破，同 torch / 草丛）、NoTool（空手可采且掉落）、
                                  //   dropId=0x208（小麦种子，材料段；Core 不依赖 Game 故字面量与 RecipeRegistry::SeedId 同源；
                                  //   **未成熟阶段破块返种子**——成熟阶段破块掉小麦物品 + 额外种子归 t237 收割按 state 判定，
                                  //   本表 dropId 仅作基础兜底）、dropCount=1、maxStack=64。**进创造调色板**（t244 补全：作物
                                  //   经种子种出、非玩家常规放置，但创造页供测试 / 装饰取用；图标取成熟阶段 7 金黄麦穗）。
                                  //   各面贴图=wheat_stage_<state>（tile 29..36，mesher 走 cross 几何段时据 state 选；本表
                                  //   topTile/sideTile 存阶段 0 基底 tile 29，partialblockgeometry 的 WheatCrop case 内 state +
                                  //   基底算出实际阶段贴图）。音色归 GroupGrass（软草音，同草丛）。
        TallGrass      = 24, // 草丛（t235）：机制等价 MC 1.0 草丛 / 蕨类（tall grass / fern）。**cross 形广告牌方块**
                                  //   （两片对角十字相交的 quad，billboard X 形贴图，alpha 透明底 cutout）—— 非 1×1×1 整立方，
                                  //   亦非段内异形方块段（id > LastPartial）。worldgen 在 grass 表层上方确定性散布（placeTallGrass），
                                  //   同 seed 同分布（PLAN §2-K）。solid=false（非实体 → 不挡邻居面剔除 → 相邻地形仍画自己的面；
                                  //   同 torch / 不完整方块语义）、shape=ShapeNone（**无碰撞** → 玩家穿过，机制等价 MC 草丛可踩过）、
                                  //   hardness=0（瞬破，同 torch）、NoTool（空手可采且掉落）、dropId=小麦种子（材料段 0x208，
                                  //   RecipeRegistry::SeedId；Core 不依赖 Game 故用字面量 0x208）、dropCount=1、maxStack=64。
                                  //   各面贴图=tall_grass(28)（green 草叶 + alpha 透明底；mesher 走 cross 几何段，材质 alphaCutoff
                                  //   cutout 透明底）。音色归 GroupGrass（软草音）。**渲染走 PartialBlockGeometry::append 的 cross
                                  //   case**（合批进 chunk mesh，复用顶点色光照；cross 双面双对角 quad）；chunkgeometry 路由进 cross 段
                                  //   [FirstCross, LastCross]、不进立方面 PASS。raycastAABBs 整格命中（ShapeNone 非 torch 兜底）→
                                  //   可瞄准 / 破坏。**进创造调色板**（t244 补全：草丛由 worldgen 散布、非玩家常规放置，
                                  //   但创造页供测试 / 装饰取用，图标走 flat 2D 路径同火把）。
        Count         = 26, // 哨兵：已定义方块数（含 air），也是合法 id 的上界（id < Count）。
    };

    // t133 不完整方块段起止哨兵：id ∈ [FirstPartial, LastPartial] 走 PartialBlockGeometry 异形渲染
    //   （mesher 合批进 chunk mesh，不走 1×1×1 立方面路径）。t134 落地 6 类（WoodSlab=15 ... WoodTrapdoor=20）。
    //   机制等价 MC 1.0 (id, metadata) 方块模型：id ∈ [FirstPartial, LastPartial] 即「异形方块」（非整立方）。
    //   **必须用闭区间**：段后追加的方块虽 id 更大但**非异形** —— Water(21) 走水段、Chest(22) 是整立方
    //   ShapeFull 走 culled 立方面。旧「`b >= FirstPartial` 单边判定」会把这类段后整立方 / 非异形方块误
    //   路由进 PartialBlockGeometry（其 switch 无对应 case → 追加 0 顶点 → 渲染透明）。t194 箱子放置后
    //   透明（Chest 透视格子）即此根因。mesher / 选中框路由一律用 `>= FirstPartial && <= LastPartial`。
    static constexpr int FirstPartial = 15;
    static constexpr int LastPartial  = WoodTrapdoor; // 20（异形段上界；新增异形方块追加时同步右移）

    // t235 cross 广告牌方块段哨兵：id ∈ [FirstCross, LastCross] 走 PartialBlockGeometry 的 cross 几何
    //   （两片对角十字相交的双面 quad，机制等价 MC 草丛 / 花 / 作物的 cross 模型）。与 [FirstPartial, LastPartial]
    //   的「轴对齐盒体异形」**不同类** —— cross 是对角双面平面（非盒组合），故独立成段（避免与 partial 盒体几何混在
    //   同一 switch 误生成）。t235 落地 TallGrass=24；t236 追加 WheatCrop=25（同 cross 模型 + 按 state 阶段选贴图）。
    //   mesher 路由用闭区间 [FirstCross, LastCross]（同 partial 段教训 lessons-learned t194：闭区间防段后整立方误进
    //   cross 路径）。新增 cross 方块（花 / 其它作物）追加时右移 LastCross。
    static constexpr int FirstCross = TallGrass; // 24
    static constexpr int LastCross  = WheatCrop; // 25（cross 段上界；新增 cross 方块追加时同步右移）

    // t236 小麦作物生长阶段：state = 阶段 0..7（0=刚种嫩芽、7=成熟）。mesher（PartialBlockGeometry::append 的
    //   WheatCrop case）据 state 选对应阶段贴图（tile 29..36）；world.tickCropGrowth 据光强 + 耕地支撑 + 确定性
    //   散布概率把未成熟作物的 state 逐步 +1 直到本上界。state 经 m_states 落 SQLite round-trip 保真（存档读回
    //   仍带阶段 → 重载后继续生长 / 收割按阶段判成熟）。唯一消费点：partialblockgeometry 的 WheatCrop case（贴图
    //   选择）+ world tickCropGrowth（成长上界判定）+ t237 收割（state==max 判成熟掉小麦）。mesher / collisionAABBs /
    //   selectionAABBs 不读 wheat state（wheat 走 ShapeNone + cross 几何，state inert 于碰撞/选中）→ 复用 state 作
    //   阶段编码零回归（同 PlanksFromDoubleSlabBit / Farmland state 复用 state 作 marker 的模式）。
    static constexpr quint8 WheatCropStageMax = 7;

    // t206 双半砖（合并态）state 标记：两块互补半砖（上+下）同格合并时，placeBlock 写 Planks(id=6) +
    //   本 bit 标记「源自双半砖」（旧实现写 Planks state=0 → 破块掉 1× Planks，用户报「双半砖挖掉掉 1 全木板」）。
    //   finishMiningAt 检本 bit → 破块掉 2× WoodSlab（机制等价 MC「double slab 破坏掉 2 块半砖」，非 1 块木板）。
    //   **为何标在 Planks 而非 WoodSlab 上**：合并结果在视觉 / 碰撞 / 剔除上本就是「满格整立方」（与 Planks
    //   同走 solid=true 立方面剔除 → 相邻实体面正确剔除，无 z-fight；WoodSlab solid=false 会与邻实体整面 z-fight）。
    //   仅改掉落语义、不动渲染 / 碰撞 / 选中框 → 零回归。state 对 ShapeFull（Planks）本属 inert（mesher /
    //   collisionAABBs / selectionAABBs 均不读 state），此处复用作 marker，**唯一消费点**是 finishMiningAt 掉落判定。
    //   常规放置的 Planks state 恒 0（4 参数 setBlock 默认 / worldgen）→ 不误判为双砖。state 经 m_states 落 SQLite
    //   round-trip 保真（存档读回仍带本 bit → 重载后破块仍掉 2 块半砖）。
    static constexpr quint8 PlanksFromDoubleSlabBit = 0x01; // Planks state bit0 = 源自双半砖合并（仅 Planks 复用）

    // t234 耕地干/湿 state 标记：bit0 = 湿润（1，邻近水源 → 顶面贴 farmland_wet，深色湿润土）/ 干燥（0，farmland_dry，
    //   浅色干土）。由 playercontroller 耕地时据「水源邻近判定」（isFarmlandMoist 扫描半径 4 水平 + 同/下一层
    //   有无 Water）写入 setBlock(Farmland, moist)。**唯一消费点**：ChunkGeometry::tileFor（mesher 据 bit0 选顶面
    //   干/湿贴图）。collisionAABBs / selectionAABBs / raycastAABBs 不读 farmland state（farmland 走 ShapeFull +
    //   collision 特例，state inert 于碰撞/选中）→ 复用 state 作干湿编码零回归（同 PlanksFromDoubleSlabBit /
    //   torch state 复用 state 作 marker 的模式）。state 经 m_states 落 SQLite round-trip 保真（存档读回仍带干湿态）。
    //   注：干/湿仅由耕地时的水源邻近快照决定（机制等价 MC「耕地后即时据邻水判湿润」）；动态补水（雨/后放水）
    //   属后续任务（需 random tick 系统），本任务不做。
    static constexpr quint8 FarmlandMoistBit = 0x01; // Farmland state bit0 = 湿润（仅 Farmland 复用）

    // 面索引（与 Renderer 的 kFaces 顺序一致，是 World/Renderer 共享的轴向约定）：
    //   0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z
    enum Face : int {
        PosX   = 0,
        NegX   = 1,
        Top    = 2, // +Y
        Bottom = 3, // -Y
        PosZ   = 4,
        NegZ   = 5,
    };

    // 工具类型（决定哪类工具给方块挖掘**速度加成**；BlockDef.toolType 与 ToolRegistry::ToolDef.type 共用同一枚举）。
    // 放 Core 层是因为它属「方块的采掘属性」。当前 5 档：镐（石类）/ 斧（木类）/ 铲（土沙草类）/
    // 剑（攻击，不参与挖掘速度）/ 锄（专用耕地，不参与挖掘）。机制等价 MC 1.0 工具类型分流。
    // **toolType 只决定速度加成，掉落是否依赖工具看 BlockDef.requiresTool**（t265 解耦）：木 / 土 / 沙类
    //   方块 toolType=Axe/Shovel 但 requiresTool=false → 空手仍掉落、仅速度无加成；石类 toolType=Pickaxe
    //   且 requiresTool=true → 空手不掉落且无加成。
    enum ToolType : int {
        NoTool  = 0, // 无有效工具：任何手持物均无速度加成（基准速 1.0）；空手可采且掉落
        Pickaxe = 1, // 镐：石类方块加速（stone / cobble / furnace / coal_ore / iron_ore；requiresTool=true 需镐才掉落）
        Hoe     = 2, // 锄：专用耕地（右键草/泥土→耕地，机制留后续任务）。**不参与挖掘速度**——
                     //   本工程无任何方块的 BlockDef.toolType 取 Hoe（耕地是非方块语义、走 useBlock 交互，
                     //   非「采掘所需工具」），故 ToolRegistry::miningSpeedMul 对持锄挖任何方块恒返 1.0
                     //   （等同空手），机制等价 MC 1.0「锄不影响挖掘」。
        Axe     = 3, // 斧：木类方块加速（原木 / 木板 / 工作台 / 箱 / 木台阶 / 楼梯 / 栅栏 / 压力板 / 门 / 活版门；
                     //   requiresTool=false → 空手也掉落，仅速度受斧影响）。t265 落实方块 toolType→Axe 映射。
        Shovel  = 4, // 铲：土沙草类方块加速（沙 / 泥土 / 草方块 / 耕地；requiresTool=false → 空手也掉落）。
                     //   t265 落实方块 toolType→Shovel 映射。砾（gravel）方块待后续追加。
        Sword   = 5, // 剑：攻击伤害加成（ToolRegistry::attackDamage，t265 落实），**不参与挖掘速度**——
                     //   本工程无任何方块的 toolType 取 Sword（剑是武器、非采掘工具），miningSpeedMul 恒 1.0。
                     //   机制等价 MC 1.0「剑不加速挖掘（蛛网除外），其价值在攻击伤害」。
    };

    // 音效材质分组（t118）：决定破 / 挖 / 走音色按方块材质分流（石 / 木 / 草 / 沙 / 叶 5 组 +
    // 兜底 Default）。放 Core 层是因为「方块敲起来听感是哪类材质」本质是方块的属性（与 hardness /
    // toolType 同性质），由 BlockRegistry 单一权威给出，AudioManager（Core/Platform）只读查询、
    // 不另持映射副本（PLAN §2：世界数据单一）。groupFor() 是 id → MaterialGroup 的纯函数。
    //   - Stone：stone / cobble / furnace / coal_ore / iron_ore（石质，硬、脆、明亮敲击）
    //   - Wood：log / planks / crafting_table（木质，中空闷击）
    //   - Grass：grass / dirt（软土 / 草皮，柔垫）
    //   - Sand：sand（颗粒沙响）
    //   - Leaves：leaves（叶沙沙）
    //   - Default：air / torch / 未知（用 Stone 兜底音色；多数无 mining 音路径）
    enum MaterialGroup : int {
        GroupStone   = 0,
        GroupWood    = 1,
        GroupGrass   = 2,
        GroupSand    = 3,
        GroupLeaves  = 4,
        GroupDefault = 5, // 哨兵：合法组上界（实际播放时 GroupDefault → 复用 GroupStone 兜底）
    };

    // t146 方块碰撞 / 选中形状（决定 collisionAABBs/selectionAABBs）。机制等价 MC「方块 VoxelShape」：
    //   完整立方（常规方块）走 ShapeFull；air/torch 走 ShapeNone（无碰撞 sub-AABB，torch 选中框由
    //   Main.qml isTorch 分支特殊定向、不走 selectionAABBs 几何）；不完整方块段（id >= FirstPartial）
    //   各 shape 复用 partialblockgeometry.cpp 的 state 解码生成对应子 AABB（slab/stairs/fence/plate/
    //   door/trapdoor），使「碰撞/选中形状」与「渲染形状」同源 —— 改一处 state 编码须同步另一处。
    //   分层：本枚举属 Core（仅数据属性），不依赖 Renderer/Mesher。
    enum Shape : int {
        ShapeNone     = 0, // air / torch：无碰撞 sub-AABB（torch 不挡玩家；选中框由 Main.qml isTorch 分支）
        ShapeFull     = 1, // 常规整立方：collision/selection = {0,0,0,1,1,1}
        ShapeSlab     = 2, // 木板台阶：半高（state bit0=上半 → {0,0.5,0,1,1,1}；下半 → {0,0,0,1,0.5,1}）
        ShapeStairs   = 3, // 木板楼梯：整步 + 背墙（朝向 state[1:0]；bit2=倒置 → 整步/背墙 y 区间垂直镜像）
        ShapeFence    = 4, // 木栅栏：中心立柱 {0.3,0,0.3,0.7,1.5,0.7}（t209 1.5 高不可越；横档纯视觉不进碰撞）
        ShapePlate    = 5, // 木板压力板：贴地薄板 {0.0625,0,0.0625,0.9375,0.0625,0.9375}
        ShapeDoor     = 6, // 木板门：满高薄板（state 朝向/开合；上下半由所在格的 y 自然分，bit3 标识上下）
        ShapeTrapdoor = 7, // 木活板门：合=水平薄板 / 开=竖直薄板（state bit0 开合 + bit[2:1] 朝向）
    };

    // t146 方块子碰撞/选中盒（**cell-local [0,1]^3 AABB**；世界坐标由 caller + (bx,by,bz) 偏移）。
    //   min/max 各轴，min <= max。完整方块单盒 {0,0,0,1,1,1}；异形方块可能多盒（stairs = 下步 + 背墙）。
    //   纯数据结构（POD），可值拷贝进 std::vector 返回。Core 层定义，World/Physics 只读消费。
    struct BlockAABB {
        float minX, minY, minZ;
        float maxX, maxY, maxZ;
    };

    // 方块定义（每方块一项；单一权威数据源）。改方块任何属性只改 kDefs 一行，全工程生效。
    // 行索引 == 方块 id（air 行同时作越界 / 不可挖掘 / 不掉落兜底）。
    struct BlockDef {
        int id;              // 方块 id（= 行索引；自描述，便于日志 / 调试对照）
        int topTile;         // +Y(Top) 图集瓦片序号
        int bottomTile;      // -Y(Bottom) 瓦片序号
        int sideTile;        // +X / -X / +Z（三个侧面统一）瓦片序号
        int frontTile;       // -Z(NegZ「前面」) 瓦片序号（熔炉炉口等有朝向的方块用）；多数 == sideTile
        bool solid;          // 实体（参与碰撞 / culled 面剔除）；air=false。t146 注：solid 仅作 **mesher
                             //   邻居面剔除** 的依据（不完整方块 solid=false → 不挡邻居整面，避免相邻整立方
                             //   被误剔出洞）。**碰撞**改走 shape（solid=false 的不完整方块仍有碰撞 sub-AABB）。
        Shape shape;         // t146 碰撞/选中形状（Shape 枚举）。决定 collisionAABBs/selectionAABBs。
        float hardness;      // 基础硬度（挖掘耗时基准；<=0 → 不可挖掘，canMine=false）
        int toolType;        // 「有效工具」类型（ToolType；决定哪类工具给挖掘**速度加成**，t265：斧→木 / 铲→土沙草 / 镐→石）。
                             //   NoTool=无任何工具给加成（空手即基准速）。**与 requiresTool 正交**：toolType 只管速度，
                             //   能否掉落看 requiresTool（木 / 土 / 沙虽 toolType=Axe/Shovel 但 requiresTool=false → 空手仍掉落）。
        int minToolTier;     // 采掘所需最低工具等级（仅 requiresTool=true 时才作「掉落 + 速度」的等级门槛；
                             //   requiresTool=false 时忽略，任意等级的正确类型工具均给速度加成）
        bool requiresTool;   // t265 「掉落是否需要匹配工具」：true=必须持 toolType 且 tier>=minToolTier 才掉落
                             //   （石 / 圆石 / 矿石 / 熔炉等石类）；false=空手也掉落（木 / 土 / 沙 / 草 —— 速度受工具
                             //   影响，但产物不依赖工具，机制等价 MC 1.0「不需工具方块空手可采且掉落」）。
        int dropId;          // 破坏后掉落物品 id（<=0 → 不掉落；stone→cobble 等「冶炼转化」在此表达）
        int dropCount;       // 掉落数量（生存破块产出物品实体数；创造秒破不掉落，由 caller 判）
        int maxStack;        // 单栈最大堆叠（Hotbar / 背包上限；air=0；工具段另走 ToolRegistry，恒 1）
        const char *name;    // 内部 / 调试用名（通用词，英文标识符；非面向用户）
        const char *display; // 用户可见中文显示名（UTF-8；PLAN §9 override (b) 通用描述词；air=空串）
    };

    // 取方块定义（const 引用）。air / 越界 → 返回 air 行（不可挖掘、不掉落、不实体）。
    // 供 ToolRegistry（挖掘 / 掉落判定）与 Hotbar（maxStack）等只读查询，避免各持副本。
    static const BlockDef &def(quint8 blockId);

    // 给定方块 id 与面，返回**图集瓦片序号**（须与 tools/build_atlas.py 打包顺序一致）。
    // 越界/未知 id 返回 0（兜底，与旧 tileFor 同语义）。
    //
    // 瓦片顺序（一个偏差即渗色/错贴）：
    //   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
    //   5=cobble 6=log_top 7=log_side 8=planks 9=leaves
    //   10=crafting_table_top 11=crafting_table_side
    //   12=furnace_top 13=furnace_side 14=furnace_front（t80；炉口朝 -Z）
    //   15=coal_ore 16=iron_ore（t84；矿石各面同贴图）
    //   17=torch（t88；6 面同贴图，近黑底+火焰图案）
    //   18=bedrock（t119；6 面同贴图，深灰斑驳不可破坏底岩）
    //   19=water（t148；6 面同贴图，蓝半透——纹理本身不透明，半透由材质 opacity=0.7 实现）
    //   20=chest_top / 21=chest_side / 22=chest_front（t173；箱子顶=盖缝+铰链、侧=铁箍带、前=锁孔）
    //   23=water_flow（t197；**流水专用**贴图，不绑定任何方块 id——Water 方块 def 仍各面=19 静水；
    //      mesher 在水段按 cell 的 state 选 19(水源)/23(流水)。属渲染层呈现选择，非方块属性）。
    //   24=water_2（t223 静水动画第二帧；mesher 在水段据 waterAnimPhase 在 19/24 间慢速切换 → 静水荡漾感）。
    //   25=water_flow_2（t223 流水动画第二帧；mesher 在水段据 waterAnimPhase 在 23/25 间切换 → 斜纹流动动势）。
    //   26=farmland_dry（t234 耕地顶面干态；浅色翻耕干土，纵向犁沟纹）。
    //   27=farmland_wet（t234 耕地顶面湿态；深色湿润翻耕土，同犁沟纹 + 深色；mesher 据 Farmland state bit0
    //      选 26(干)/27(湿)；Farmland 方块 def topTile=26，tileFor 特例覆盖）。
    //   28=tall_grass（t235 草丛 cross 贴图；green 草叶 + alpha 透明底；cross 几何段材质 alphaCutoff cutout 透明底）。
    //   29..36=wheat_stage_0..7（t236 小麦作物 8 个生长阶段贴图；cross 几何段，alpha 透明底 cutout。mesher 在
    //      PartialBlockGeometry::append 的 WheatCrop case 内据 state 选 tile = 29 + stage；WheatCrop 方块 def 各面
    //      = 29（基底阶段 0），阶段贴图选择是 mesher 呈现层据 state 决定，非 BlockDef 字段——同 Water 流水贴图模式）。
    // 图集由 tools/build_atlas.py 打包全部 37 瓦片；mesher / BlockCube 都读本常量算每瓦片 UV
    //   宽 1/AtlasTileCount —— **单一权威**，与 build_atlas.py 的 TILES 长度严格对齐。
    // -Z 面（NegZ「前面」）走 frontTile（熔炉炉口；其余方块 frontTile == sideTile，无视觉差异）。
    static int tileIndex(quint8 blockId, Face face);

    // 图集瓦片总数（atlas.png 横排瓦片数 = 最大 tile 序号 + 1；当前 26）。
    //   **单一权威**：mesher(chunkgeometry) 与 BlockCube（第一/第三人称手持 + 掉落/下落实体）
    //   都读本常量算每瓦片 UV 子区宽 1/AtlasTileCount。消除「mesher 与 BlockCube 各持一份魔数、
    //   加新瓦片后忘记同步一份」的复发 bug 类——历史已踩 3 次（t54: 10→12、t148: 12→20、t173: 20→23
    //   漏改 BlockCube → 手持/掉落物贴图「杂交」：UV 按 1/20 算偏宽、tile t 采到 [t/20,(t+1)/20] 而真实
    //   瓦片在 [t/23,(t+1)/23] → 泥土采到半块石头、树叶采到木板，肉眼「不是实际方块」）。
    //   .cpp 内 static_assert 守卫：kDefs 任一 tile 字段 >= AtlasTileCount → 编译失败（防 tile 越界）。
    //   新增瓦片时同步改本常量 + tools/build_atlas.py 的 TILES（两处须一致）。
    static constexpr int AtlasTileCount = 37;

    // 方块是否实体（参与碰撞 / culled 面剔除）。air 恒 false；torch 亦 false（非实体、不挡邻居面）；
    // 其余填表 solid=true。越界/未知 id 返回 false。mesher 邻居面剔除走本谓词（单一权威），
    //   切勿在渲染层另写 `!= 0`（会把 torch 当 solid → 误剔邻居面 → 透明 bug，见 t130）。
    static bool isSolid(quint8 blockId);
    // t146 方块碰撞/选中形状（BlockDef.shape；越界 → air 行 = ShapeNone）。
    static Shape shape(quint8 blockId);
    // 方块是否「有碰撞 sub-AABB」（考虑开合态）：决定 collisionAABBs 是否非空 + World::isCollidable
    //   预判。单一权威：isCollidable 与 collisionAABBs 共用，保证「预判」与「精确碰撞」对开合态同源。
    //   - 门（ShapeDoor）：**恒 true** —— 门板无论开合都实存于某一边（合=贴朝向边、开=旋 90° 贴铰链侧
    //     邻边，几何见 shapeBoxes 的 ShapeDoor 分支 + partialblockgeometry WoodDoor 渲染同源），玩家撞
    //     门板那两面被挡、门板切线方向（含开门后的「门洞」方向）可穿过。t261 修「开门四向全通」：旧名
    //     isCollidableWhenClosed 对开门返 false → collisionAABBs 空 → 门板凭空消失、四向皆通；改恒 true 后
    //     collisionAABBs 返回 shapeBoxes 算出的「旋后贴边」panel AABB，开门仍挡铰链那一面（机制等价
    //     MC 门打开后门板贴墙仍挡一面、仅门洞方向可过）。函数随之从 isCollidableWhenClosed 改名 isCollidable
    //     （「开态通」对门已不成立，旧名误导）。
    //   - 活版门（ShapeTrapdoor）：bit0=开(1) → false（开=竖直贴边，玩家穿过，机制等价 MC 活版门打开可过）；
    //     合(0) → true（水平贴地挡）。
    //   - 其余有碰撞形状（Full/Slab/Stairs/Fence/Plate）→ true（无开合概念，恒挡）。
    //   - air / torch / water（ShapeNone）→ false。
    //   越界 → false（air 兜底）。
    static bool isCollidable(quint8 blockId, quint8 state);
    // t146 方块碰撞 sub-AABB（cell-local [0,1]^3；复用 partialblockgeometry state 解码 → 与渲染形状同源）。
    //   玩家碰撞迭代玩家 AABB 覆盖的格子，逐 sub-AABB（+ 格偏移到世界坐标）做 3 轴重叠测试。
    //   air/torch → 空；常规整立方 → 单盒 {0,0,0,1,1,1}；异形 → 形状对应的多盒（stairs 2 盒 等）。
    //   越界 / air 行 → 空。机制等价 MC「方块 VoxelShape」（机制对齐，非名词照搬）。
    static std::vector<BlockAABB> collisionAABBs(quint8 blockId, quint8 state);
    // t146 方块选中框 sub-AABB（cell-local [0,1]^3；选中框线框按此画棱）。当前与 collisionAABBs 同数据
    //   （异形方块 VoxelShape 与 outline shape 多数一致）；分离接口备将来分歧（如某些方块选中框略放宽）。
    //   Main.qml 的 SelectionWireBoxes 几何据本方法画每个 sub-AABB 的 12 棱（贴合实际形状，非全格）。
    static std::vector<BlockAABB> selectionAABBs(quint8 blockId, quint8 state);

    // t213 方块是否占满整格立方（shape == ShapeFull）。R18d 三任务共用基础谓词（dev-plan t213/t220/t226）：
    //   - t213 射线穿「不完整方块的空气部分」命中后方块（isFullCube → 射线进格即中；否则须命中 sub-AABB）；
    //   - t220 沙子下落遇不完整方块 → 变掉落物（仅完整方块可支撑沙）；
    //   - t226 箱子上方完整方块 → 阻挡开盖（不完整方块上方可开）。
    //   water/torch/air（ShapeNone）→ false；不完整方块段（ShapeSlab/...）→ false；常规整立方
    //   （grass/stone/log/planks/.../chest 等）→ true。机制等价 MC「方块是否完整立方」。
    static bool isFullCube(quint8 blockId);

    // t213 射线命中 sub-AABB（cell-local [0,1]^3）：射线进入含该方块的体素后，**须命中其中某个 sub-AABB**
    //   才算选中——不完整方块 / 火把的「空气部分」让射线穿过命中后方块（修「挖半砖背后的方块却撸掉了
    //   半砖/火把」，命中点是否落在该方块 sub-AABB 内）。与 selectionAABBs 的差异：
    //   - 完整立方（ShapeFull）→ 单盒 {0,0,0,1,1,1}（射线进格即中，等同旧行为）；
    //   - 不完整方块段（ShapeSlab/...）→ 同 selectionAABBs（实体 sub 形状；空气部分穿过）；
    //   - 火把（ShapeNone，selectionAABBs 空 → 选中框由 Main.qml isTorch 分支特殊定向）→ 此处给一个贴火把
    //     视觉范围的中央小立柱盒（瞄柄/焰才命中，格角落空气穿过）；
    //   - air/water → 单盒 {0,0,0,1,1,1}（water 经 HitWater 命中整格舀水；air 不进本路径，兜底）。
    //   火把朝向需邻居上下文（由呈现层 QML 持 + 邻居推导），Core 层无 World → 此处取覆盖所有朝向焰/柄的
    //   保守中央区（焰恒在格中央偏上）。raycast 用本方法 + stateAt 做命中点 vs sub-AABB 精确测试。
    static std::vector<BlockAABB> raycastAABBs(quint8 blockId, quint8 state);

    // t214 火把附着方向（存 chunk state，低 3 位编码）：火把放置时记录其所「贴」的唯一支撑邻居方向，
    //   破该邻居即掉落（不「粘」到附近其它 solid 邻居）。机制等价 MC「火把附着面被移除即脱落」。
    //   编码与 Main.qml orientFromNormal/torchNeighborSolid 同源约定（QML orient 串 ↔ 本枚举）：
    //     up↔TorchFloor / px↔TorchOnNX / nx↔TorchOnPX / pz↔TorchOnNZ / nz↔TorchOnPZ。
    //   state 经 m_states 落 SQLite round-trip 保真（存档读回仍带附着方向）。旧存档 / worldgen 火把
    //   state=0 → TorchFloor（贴地）兜底，行为对齐「地面火把」（不会误判为无支撑而掉落）。
    //   唯一消费点：PlayerController::finishMiningAt（破块后扫邻火把，附着格失撑即掉）。mesher /
    //   collisionAABBs / selectionAABBs 均不读 torch state（torch 走 ShapeNone + Main.qml isTorch 分支），
    //   故复用 state 作附着编码零回归（同 PlanksFromDoubleSlabBit 复用 state 作 marker 的模式）。
    enum TorchAttach : quint8 {
        TorchFloor = 0, // 支撑 = 下方 (y-1)：玩家点中顶面放置（QML "up"，立柱）
        TorchOnNX  = 1, // 支撑 = -X 邻：玩家点中 +X 面放置（QML "px"，柄伸 +X 嵌 -X 墙）
        TorchOnPX  = 2, // 支撑 = +X 邻：玩家点中 -X 面放置（QML "nx"，柄伸 -X 嵌 +X 墙）
        TorchOnNZ  = 3, // 支撑 = -Z 邻：玩家点中 +Z 面放置（QML "pz"，柄伸 +Z 嵌 -Z 墙）
        TorchOnPZ  = 4, // 支撑 = +Z 邻：玩家点中 -Z 面放置（QML "nz"，柄伸 -Z 嵌 +Z 墙）
    };
    // 由放置命中面外法线（指向玩家侧）推火把附着方向。torch target = hitBlock + normal，故 normal +X
    //   → 火把在 hitBlock 的 +X 侧 → 其支撑 = 火把的 -X 邻 = hitBlock（TorchOnNX）。ny>0 → TorchFloor。
    //   无法线（不应发生）→ TorchFloor 兜底。placeBlock 据此写 state；与 torchPlaced 信号传出的命中面
    //   法线（QML 据之算 prefOrient）同源 → C++ 附着判定与 QML 渲染朝向放置时一致。
    static TorchAttach torchOrientFromNormal(int nx, int ny, int nz);
    // 火把支撑邻居相对偏移 (dx,dy,dz)（state 解码）：TorchFloor→(0,-1,0)；OnNX→(-1,0,0)；OnPX→(+1,0,0)；
    //   OnNZ→(0,0,-1)；OnPZ→(0,0,+1)。越界 state 值 → TorchFloor 兜底。finishMiningAt 据此定位唯一附着格。
    static void torchAttachOffset(quint8 state, int &dx, int &dy, int &dz);

    // t225 箱子朝向（存 chunk state，低 2 位编码水平朝向）：放置时记录箱子「前面（锁面，chest_front 贴图）」
    //   朝哪一侧，mesher 据此把 chest_front 贴到对应面（其余三侧面 chest_side、顶/底 chest_top）。机制等价
    //   MC 1.0 箱子放置时锁面朝向玩家。编码与 horizontalFacing 同源（0=+X 1=-X 2=+Z 3=-Z）= 箱子前面所朝方向。
    //   state 经 m_states 落 SQLite round-trip 保真（旧存档箱子 state=0 → 前面 +X 兜底；箱子仅玩家放置、
    //   罕见旧存档，朝向变化可接受）。**唯一消费点**：ChunkGeometry::tileFor（mesher 据 state 选前面贴图）。
    //   collisionAABBs / selectionAABBs 不读 chest state（chest 走 ShapeFull 整立方）→ 复用 state 作朝向编码
    //   零回归（同 PlanksFromDoubleSlabBit / torch state 复用 state 作 marker 的模式）。BlockCube 手持 / 掉落
    //   走 stateless tileIndex（前面恒 -Z，旧默认）→ 物品图标外观不变。
    // chestFrontFace(state)：返回前面所朝的 Face（PosX/NegX/PosZ/NegZ）；低 2 位解码，越界位 → NegZ 兜底。
    static Face chestFrontFace(quint8 state);

    // 挖掘 / 掉落 / 堆叠属性访问器（t42 集中表查；越界 → air 行默认：hardness=0 / NoTool / 不掉落 / maxStack=0）。
    static float hardness(quint8 blockId);    // 基础硬度
    static int toolType(quint8 blockId);      // 有效工具类型（ToolType；给挖掘速度加成的工具类）
    static int minToolTier(quint8 blockId);   // 最低工具等级（仅 requiresTool=true 时作门槛）
    static bool requiresTool(quint8 blockId); // t265 掉落是否需要匹配工具（true=石类需镐才掉；false=木土沙空手也掉）
    static int dropId(quint8 blockId);        // 破坏后掉落物品 id（<=0=不掉落）
    static int dropCount(quint8 blockId);     // 掉落数量
    static int maxStack(quint8 blockId);      // 单栈最大堆叠

    // 内部/调试用方块名（**非**面向用户字串；通用词）。越界/未知 id 返回 "unknown"。
    static const char *blockName(quint8 blockId);

    // 用户可见的**中文**显示名（PLAN §9 override (b)：通用描述词）。
    //   air → 空串；越界/未知 id → 空串（兜底）。
    // 与 blockName()（内部英文标识符）分离：本方法供 HUD/背包等面向用户文本消费；
    // 字面量为 UTF-8，由 fromUtf8 解码（与项目既有中文注释同源）。
    //   grass=草方块 dirt=泥土 stone=石头 cobble=圆石 log=橡木原木
    //   planks=橡木木板 leaves=橡树树叶 sand=沙子 crafting_table=工作台 furnace=熔炉
    //   coal_ore=煤矿石 iron_ore=铁矿石 bedrock=基岩 ... farmland=耕地
    static QString displayName(quint8 blockId);

    // 音效材质分组（t118）：id → MaterialGroup 纯函数。AudioManager 据此选 break / mining / step
    // 音色（spec「按方块材质分组 clip 池」）。越界 / 未知 → GroupDefault（AudioManager 内部兜底
    // 复用 GroupStone 音色）。机制等价 MC「按方块材质 SoundType 选声」（机制对齐，非名词照搬）。
    static MaterialGroup materialGroup(quint8 blockId);

private:
    BlockRegistry() = delete; // 纯静态数据表，无实例。
};

#endif // BLOCKREGISTRY_H
