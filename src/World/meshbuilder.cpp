#include "meshbuilder.h"

#include "blockregistry.h"
#include "chunk.h"         // Chunk::kSize（域常量互钉）
#include "frameprofiler.h" // meshN 族事件计数（自 ChunkGeometry::buildMesh 原位迁入——统计经新路径照走）
#include "voxellight.h"    // kVcMin/kVcMax/kAoFactor + sunShadowColumnTop 泛化缝（PCF 单点）
#include "worldfacade.h"   // 采集面（R20.08 收窄查询：block/state/光照/height/columnTopSurfaceY）

#include <algorithm> // std::clamp / std::min
#include <cmath>     // std::sin / std::fabs（流水 t893 + 静态水涟漪 t391）
#include <cstddef>
#include <vector>    // t178 greedy meshing mask 缓冲（std::vector）

// R20.13 网格算法单一权威（plan §29.3 R20.13 五验收）：本文件即「ChunkGeometry::buildMesh 的
// 网格算法」整体抽出后的落点——PASS1 异形/cross 合批、PASS2 六面 mask + 边界剔除、天光 dayMul
// 顶点色烘焙、AO 三探针、water-lava-ice-waterOnly-lavaOnly-iceOnly 段分流、greedy 合并键、
// FrameProfiler 计数全部在此，**旧路径（ChunkGeometry::buildMesh）委托本实现，禁两份网格逻辑
// 并存**。抽取 = 搬移不改写：除两类机械替换（数据来源：`m_world->` 现查 → `snap.` 快照访问器；
// 输出容器：局部 verts/idx → ChunkMeshData 成员引用）外逐段自 chunkgeometry.cpp 原文迁入，
// 顶点输出逐位一致由矩阵 r2013a 承重（字节比对）。r2008c 反探的「mesher 零 Chunk*」在本文件
// 以更强形态成立：连 World* 都不持（输入只有值语义快照）。

// 域常量互钉（编译期）：快照 pad 域推导的两大前提——chunk 边长一致 + PCF 步进不超过 pad。
// kMaxShadow 增大时必须同步扩 ChunkMeshSnapshot::kPad/kTopDim（否则 PCF 采样落域外 → 恒 -1
// 不遮挡 = 软影截断的行为漂移；编译期把这一耦合钉死）。
static_assert(ChunkMeshSnapshot::kChunk == Chunk::kSize,
              "ChunkMeshSnapshot::kChunk must match Chunk::kSize (R20.13)");
static_assert(VoxelLight::kMaxShadow <= ChunkMeshSnapshot::kPad,
              "PCF kMaxShadow grew past snapshot pad - widen ChunkMeshSnapshot (R20.13)");
// t1056（agent-review-2026-09-16 #9）列顶域 × PCF 半格触达互钉：PCF 每步采样 floor(落点) 与
// floor(落点)+1 两列（半格列），顶点恰在 chunk 右/下缘 origin+kChunk → 前向探测越过 chunk
// 缘 kMaxShadow+1 列，须 ≤ 列顶域在缘外的余量（kTopLo + kTopDim - kChunk = 域前向半宽）；
// 后向探测 = floor 恰落 -kMaxShadow（不下探），域下界须触达。旧 21 宽域缺这两条断言面
//（推导漏 floor 后 +1 半格 → 域差 1 列，#9 病灶本体）——本两条把「kMaxShadow 增大须同步
// 扩 kTopDim/kTopLo」的耦合纳入编译期。列顶域对探测带的**精确覆盖**（last index ≥
// kChunk+kMaxShadow+1 与 kTopLo ≤ -kMaxShadow 两端恰紧）由矩阵 r2030a 运行时推导腿钉：
// 编译期紧式会把 NEG-1 恰红轮的 kTopDim 回退（21）直接变编译错误——阴性轮须红在测试面
// 而非断编译，选型留痕（t1056）。
static_assert(VoxelLight::kMaxShadow + 1 <= ChunkMeshSnapshot::kTopLo + ChunkMeshSnapshot::kTopDim
                  - ChunkMeshSnapshot::kChunk,
              "PCF half-cell probe (kMaxShadow + 1 columns past the chunk edge) must fit the"
              " forward column-top headroom - widen kTopDim with kMaxShadow (t1056 #9)");
static_assert(ChunkMeshSnapshot::kTopLo <= -VoxelLight::kMaxShadow,
              "column-top domain must reach the backward PCF probe (-kMaxShadow; floor never"
              " dips below the exact landing) - widen kTopLo with kMaxShadow (t1056 #9)");

// MeshBuilder::Reason 与 ChunkGeometry::RebuildReason 镜像互钉（值域恒等——适配层 int 转发的
// 语义不变式）。
static_assert(int(MeshBuilder::Reason::Dirty) == 0 && int(MeshBuilder::Reason::Sun) == 1
                  && int(MeshBuilder::Reason::Water) == 2,
              "MeshBuilder::Reason must mirror ChunkGeometry::RebuildReason (R20.13)");

// 6 个面：邻居偏移 dir、外法线 nrm、4 角偏移（从外侧看逆时针，叉积验证 = 外法线）。
// 三角形按 (0,1,2),(0,2,3) 画。UV 按角点位置单独计算（保证侧面「上=草」）。
// （自 chunkgeometry.cpp 逐行迁入——立方面/流体/greedy 三路共用的面表，单一权威。）
struct FaceDef {
    int dir[3];
    float nrm[3];
    float c[4][3];
};
static const FaceDef kFaces[6] = {
    /*+X*/ {{ 1, 0, 0}, { 1, 0, 0}, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}},
    /*-X*/ {{-1, 0, 0}, {-1, 0, 0}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}}},
    /*+Y*/ {{0,  1, 0}, {0,  1, 0}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}},
    /*-Y*/ {{0, -1, 0}, {0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    /*+Z*/ {{0, 0,  1}, {0, 0,  1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
    /*-Z*/ {{0, 0, -1}, {0, 0, -1}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}},
};

// t746 邻面遮挡判定（叶族特例）：叶（Leaves/SpruceLeaves）def.solid=true 仅供碰撞 / 光照遮蔽 / mob 支撑等
//   格逻辑；但叶贴图是 cutout 带孔瓦片（Mask alphaCutoff 0.5，程序 oak ~22% 孔 / pack oak ~33% 孔），
//   mesher 邻面剔除若把叶当实体遮挡，叶与实体邻的接界会**双侧**被剔（叶底面被地面剔、地面顶面被叶剔）
//   → 边界无任何面 → 透过叶面孔洞直通地形内部空洞，直到第一个内界面（洞穴顶）才挡住 —— 穿墙透视级
//   （用户「树叶放地上透视看到底下方块」根因，pack 态 / 程序态同病：孔密度只影响严重度）。
//   机制等价 MC 1.0 fancy leaves 的「非不透明方块」语义：叶不遮挡任何邻面。修后行为——
//     · 邻面（叶下草地顶面 / 树冠内原木面）恢复绘制：透过叶孔看到的是紧贴的邻面表面而非 void；
//     · 叶自身面对实体邻仍剔除（实体满格完全遮挡，画了只会共面 z-fight）；
//     · 叶-叶间面双侧绘制（共面反向法线，背面剔除后各朝各可见 —— 树冠呈叶簇通透感而非空心透视，
//       取舍：冠内面数上升（贪婪合并按 tile+光合并可部分抵消），换透视破绽归零）。
//   t639 教训：不动 isSolid 共享谓词（其消费者是碰撞/光照/支撑链），只在本消费者（mesher）做局部特例。
//   （自 chunkgeometry.cpp 逐行迁入；AO 探针遮挡判据与邻面剔除同谓词单一权威。）
static bool occludesNeighborFace(quint8 nb)
{
    if (!BlockRegistry::isSolid(nb))
        return false;
    return nb != BlockRegistry::Leaves && nb != BlockRegistry::SpruceLeaves; // t746 叶不遮挡邻面
}

// 查表（单一权威：BlockRegistry）。行为与历史硬编码一致：草顶/草侧/草底、其余各面统一。
//   t225 箱子特例：前面（锁面 chest_front）所朝面由 state 决定（放置时朝玩家），其余三侧面 chest_side、
//   顶/底 chest_top。t234 耕地特例：顶面（+Y）恒 farmland_dry(26) 贴图，湿润等级（state 低 2 位）由顶点色
//   暗化体现（darker=wetter，见 buildMesh 内 farmlandHydrBrightMul）；其余面 = dirt（侧/底）。其余方块 state
//   inert（tileFor 退化为 stateless BlockRegistry::tileIndex）。
//   （自 ChunkGeometry::tileFor 逐行迁入——本文件唯一消费方。）
static int tileFor(quint8 block, int face, quint8 state)
{
    // face: 0=+X 1=-X 2=+Y(顶) 3=-Y(底) 4=+Z 5=-Z（须与 BlockRegistry::Face 一致）
    if (block == BlockRegistry::Chest) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（锁面）所朝面
        if (face == frontFace) return d.frontTile;                       // chest_front（锁面）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = chest_top
        return d.sideTile;                                               // 其余三侧面 = chest_side
    }
    // t456 熔炉朝向：前面（炉口 furnace_front）所朝面由 state 决定（放置时朝玩家，同箱子 horizontalFacing 同源
    //   编码）；其余三侧面 furnace_side(13)、顶/底 furnace_top(12)。此前熔炉未在此特判 → 落 BlockRegistry::tileIndex
    //   兜底（前面恒 -Z 固定方向）。chestFrontFace 是「state 低 2 位 → 水平前面 Face」通用解码器（命名历史性，
    //   0=+X 1=-X 2=+Z 3=-Z），chest / furnace 共用（编码同源）；熔炉复用之，不改箱子行为。
    // t494 熔炉燃烧态：state 的 FurnaceStateLitFlag（bit2）= 1 表「冶炼进行中」→ 前面用 furnace_front_on(134)
    //   （拱洞内带火，机制等价 MC 1.0 熔炉燃烧时正面发光）；bit2=0 用 furnace_front(14)（灭）。由 FurnaceUI
    //   冶炼 tick 在点燃/熄火边界经 PlayerController::setFurnaceLit 翻转本位（5 参数 setBlock → 仅 worldChanged
    //   重建 mesh，朝向低 2 位保留）。侧面/顶底不受燃烧态影响（同 furnace_side / furnace_top）。
    if (block == BlockRegistry::Furnace) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（炉口）所朝面
        if (face == frontFace)
            return (state & BlockRegistry::FurnaceStateLitFlag) != 0 ? 134 : 14; // 134=front_on(带火) / 14=front(灭)
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = furnace_top
        return d.sideTile;                                               // 其余三侧面 = furnace_side
    }
    // t486 发射器朝向（同熔炉编码）：前面（排出口 dispenser_front）所朝面由 state bit[1:0] 决定（放置时朝玩家，
    //   同 chest / furnace horizontalFacing 同源编码）；其余三侧面 dispenser_side(126)、顶/底 dispenser_top(125)。
    //   复用 chestFrontFace 解码（0=+X 1=-X 2=+Z 3=-Z）；发射器不改箱子 / 熔炉行为（编码同源）。
    if (block == BlockRegistry::Dispenser) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（排出口）所朝面
        if (face == frontFace) return d.frontTile;                       // dispenser_front（排出口）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = dispenser_top
        return d.sideTile;                                               // 其余三侧面 = dispenser_side
    }
    // t609 投掷器朝向（同发射器 / 熔炉编码）：前面（排出口 dropper_front(139)）所朝面由 state bit[1:0] 决定
    //   （放置时朝玩家，同 chest / furnace / dispenser horizontalFacing 同源编码）；其余三侧面 furnace_side(13)、
    //   顶/底 furnace_top(12)（复用熔炉贴图——机关盒家族石质观感）。复用 chestFrontFace 解码（0=+X 1=-X 2=+Z
    //   3=-Z）；投掷器不改箱子 / 熔炉 / 发射器行为（编码同源）。
    if (block == BlockRegistry::Dropper) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（排出口）所朝面
        if (face == frontFace) return d.frontTile;                       // dropper_front（排出口）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = furnace_top（复用）
        return d.sideTile;                                               // 其余三侧面 = furnace_side（复用）
    }
    // t234/t406 耕地：顶面（+Y）恒 farmland_dry(26)（topTile）；湿润等级 0..3（state 低 2 位）由顶点色暗化
    //   体现（darker=wetter，见下方 farmlandHydrBrightMul），不再切换 dry/wet 两贴图（4 级靠顶点色
    //   连续暗化实现，无需扩图集 + 2 贴图）。侧/底 = dirt(2)。
    if (block == BlockRegistry::Farmland) {
        if (face == int(BlockRegistry::Top))
            return BlockRegistry::def(block).topTile;     // farmland_dry(26) —— 湿润由顶点色暗化体现
        return BlockRegistry::def(block).sideTile;        // 侧/底 = dirt(2)
    }
    // t487/t620 末地传送门「末影祭坛化」per-face + 激活态：本工程无独立祭坛框方块，传送门方块本体兼作
    //   末影祭坛（endframe 化）。侧·底 = endframe_side(140)（灰白细孔框身）恒定；顶面按 state bit0
    //   （EndPortalStateActiveFlag，玩家持末影之眼右键翻）切换 endframe_top(141)（未放之眼：框面 +
    //   中央暗绿凹槽）→ endframe_eye(142)（已放之眼：框面 + 中央之眼亮纹，放之眼后的可见反馈）。
    //   旧 t487 程序星空贴图（end_portal 129 / end_portal_active 130）仍在图集，但已无 BlockDef/tileFor
    //   引用（非 pack 程序回退改走 140..142 的 build_endframe.py 程序贴图）。
    //   t965：顶面态变选择收敛 BlockRegistry::stateTileOverride（查看器形态预览同源单一权威）。
    if (block == BlockRegistry::EndPortal) {
        const int overrideTile = BlockRegistry::stateTileOverride(block, face, state);
        return overrideTile >= 0 ? overrideTile : 140; // 顶 = 141/142（Core 态变权威）；侧/底 = endframe_side
    }
    // t638 ② 南瓜朝向 per-face（同箱子 / 熔炉 / 发射器模式）：前面（刻面 pumpkin_face）所朝面由 state
    //   bit[1:0] 决定（放置时朝玩家，placeBlock 写 horizontalFacing^1；此前南瓜未写 state → 前面恒 -Z 固定
    //   方向）。复用 chestFrontFace 解码（0=+X 1=-X 2=+Z 3=-Z）；其余三侧面 pumpkin_side(117)、顶/底
    //   pumpkin_top(119)。造物（雪傀儡 / 铁傀儡）检测不读南瓜 state → 零影响。旧存档南瓜 state=0 → 前面
    //   +X 兜底（朝向变化可接受，南瓜仅玩家放置）。
    if (block == BlockRegistry::Pumpkin) {
        const BlockRegistry::BlockDef &d = BlockRegistry::def(block);
        const int frontFace = int(BlockRegistry::chestFrontFace(state)); // 前面（刻面）所朝面
        if (face == frontFace) return d.frontTile;                       // pumpkin_face（刻面双眼+锯齿嘴）
        if (face == int(BlockRegistry::Top) || face == int(BlockRegistry::Bottom))
            return d.topTile;                                            // 顶/底 = pumpkin_top
        return d.sideTile;                                               // 其余三侧面 = pumpkin_side
    }
    // t620 红石灯两态全六面换贴图（机制等价 MC 1.0 redstone lamp off/on 两张贴图）：state bit0
    //   （RedstoneLampStateOnFlag，玩家右键翻位）→ on 态全六面 redstone_lamp_on(153)（暖黄亮芯）、
    //   off 态全六面 redstone_lamp_off(152)（灰暗壳，def 默认）。与熔炉 / 传送门不同：红石灯无朝向 /
    //   per-face 语义（六面同图），仅按 state 二选一 → 不读 BlockDef（def 存 off 态 152 作 BlockCube
    //   手持 / 掉落物的无 state 兜底），此处直接二值返回。光照（光 15）由 lightEmission 状态感知版
    //   承担，与贴图切换解耦（同 t494 熔炉 / t569 红石矿的「贴图 + 光照各自读同一 state bit」模式）。
    if (block == BlockRegistry::RedstoneLamp)
        return (state & BlockRegistry::RedstoneLampStateOnFlag) != 0 ? 153 : 152;
    return BlockRegistry::tileIndex(block, BlockRegistry::Face(face));
}

// t406 耕地湿润度 → +Y 顶面顶点色暗化系数（darker=wetter；机制等价 MC 耕地越湿顶面越深）。
//   level 0（干）×1.00 不暗化 → 浅色 farmland_dry 贴图本色；level 3（最湿，邻水 dist 1）×0.46 → 明显深暗。
//   仅作用于顶点色（vc 已含天光/方光/软影）→ 既保光照信息又叠加湿润暗化。湿等级进 greedy 合并键
//   （MaskEntry.hydr）→ 不同湿润度的耕地顶面不共面误并（各保留各自暗化）。
//   （自 ChunkGeometry::farmlandHydrBrightMul 逐行迁入。）
static float farmlandHydrBrightMul(quint8 hydr)
{
    static constexpr float kTbl[4] = { 1.00f, 0.82f, 0.64f, 0.46f };
    return (hydr <= BlockRegistry::FarmlandHydrationMax) ? kTbl[hydr] : kTbl[0];
}

// ── 稠密快照采集（验收②生产侧；契约见 meshbuilder.h 头注）────────────────────────────
// 16×16×H（pad 域 20×20×H）列扫一次 + 21×21 列顶查，全部经 WorldFacade 收窄查询面（R20.08
// 纪律：新代码世界读统一走 Facade）。采集成本登记：4 数组 ×~38.4k 格查 + 441 列顶查 ≈15 万次
// 查询/次——旧路径同量级查询散布在网格循环内（每格被自身 + 6 邻重复读），此处前移为一次性
// 顺序扫（总量同阶、位置集中；worker meshing 输入形态预演）。
ChunkMeshSnapshot captureChunkMeshSnapshot(const WorldFacade &world, int cx, int cz,
                                           const ChunkMeshBakeParams &bake)
{
    ChunkMeshSnapshot s;
    s.originX = cx * ChunkMeshSnapshot::kChunk;
    s.originZ = cz * ChunkMeshSnapshot::kChunk;
    s.height = world.height();
    s.dayMul = bake.dayMul;
    s.sunDir = bake.sunDir;
    s.shadowsEnabled = bake.shadowsEnabled;
    s.aoEnabled = bake.aoEnabled;
    s.greedyMeshing = bake.greedyMeshing;
    s.waterOnly = bake.waterOnly;
    s.lavaOnly = bake.lavaOnly;
    s.glassOnly = bake.glassOnly;
    s.iceOnly = bake.iceOnly;
    s.cutoutOnly = bake.cutoutOnly;
    s.cutoutFolded = bake.cutoutFolded;

    const int H = s.height > 0 ? s.height : 0;
    const size_t cells = size_t(ChunkMeshSnapshot::kDim) * size_t(ChunkMeshSnapshot::kDim)
        * size_t(H);
    s.blocks.assign(cells, 0);
    s.states.assign(cells, 0);
    s.skyLight.assign(cells, 0);
    s.blockLight.assign(cells, 0);
    for (int ly = 0; ly < H; ++ly) {
        for (int lz = -ChunkMeshSnapshot::kPad; lz < ChunkMeshSnapshot::kChunk + ChunkMeshSnapshot::kPad; ++lz) {
            for (int lx = -ChunkMeshSnapshot::kPad; lx < ChunkMeshSnapshot::kChunk + ChunkMeshSnapshot::kPad; ++lx) {
                const int wx = s.originX + lx, wz = s.originZ + lz;
                const size_t i = ChunkMeshSnapshot::cellIndex(lx, ly, lz);
                s.blocks[i] = world.blockAt(wx, ly, wz);
                s.states[i] = world.stateAt(wx, ly, wz);
                s.skyLight[i] = world.skyLightAt(wx, ly, wz);
                s.blockLight[i] = world.blockLightAt(wx, ly, wz);
            }
        }
    }
    // 列顶域（PCF 采样源；采集值含「空列/越界 → -1 不遮挡」语义——accessor 域外同值兜底）。
    s.columnTop.assign(size_t(ChunkMeshSnapshot::kTopDim) * size_t(ChunkMeshSnapshot::kTopDim), -1.0f);
    for (int lz = ChunkMeshSnapshot::kTopLo; lz < ChunkMeshSnapshot::kTopLo + ChunkMeshSnapshot::kTopDim; ++lz) {
        for (int lx = ChunkMeshSnapshot::kTopLo; lx < ChunkMeshSnapshot::kTopLo + ChunkMeshSnapshot::kTopDim; ++lx) {
            s.columnTop[ChunkMeshSnapshot::topIndex(lx, lz)]
                = world.columnTopSurfaceY(s.originX + lx, s.originZ + lz);
        }
    }
    return s;
}

// ── MeshBuilder::build（网格算法本体；自 ChunkGeometry::buildMesh 逐段搬移）──────────────
ChunkMeshData MeshBuilder::build(const ChunkMeshSnapshot &snap, Reason reason)
{
    // perf：事件计数（meshN + 按重建原因细分 meshNdirty/meshNsun/meshNwater）——自
    //   ChunkGeometry::buildMesh 原位迁入，统计经新路径照走（r2013c 穿透腿）。读报告：
    //   `mesh Xms (Yreb)` 后附 `[Ndirty d Nsun s Nwater w]`，占比最大者即风暴主源。
    FrameProfiler::instance()->count("meshN");
    if (reason == Reason::Dirty) FrameProfiler::instance()->count("meshNdirty");
    else if (reason == Reason::Sun) FrameProfiler::instance()->count("meshNsun");
    else FrameProfiler::instance()->count("meshNwater");

    ChunkMeshData mesh;
    QVector<Vtx> &verts = mesh.vertices;
    QVector<quint32> &idx = mesh.indices;

    // 列顶 PCF 软影（快照域采样）：VoxelLight::sunShadowColumnTop 泛化缝——数学体（门/淡入/
    //   步进/2×2 PCF）与 World 路径（BlockCube 掉落沙）单点共用，这里只把「列顶从哪来」换成
    //   快照列顶域。lambda 沿用旧名 sunShadowAt → 迁入后的调用点逐字不变。
    const auto sunShadowAt = [&](float wx, float wy, float wz) -> float {
        return VoxelLight::sunShadowColumnTop(snap.sunDir, snap.shadowsEnabled,
                                              [&snap](int x, int z) { return snap.columnTopAt(x, z); },
                                              wx, wy, wz);
    };

    const int H = snap.height; // 世界高（快照元数据；0 = 空快照 → 本体跳过 → 空 mesh）
    constexpr int S = Chunk::kSize; // 16（X、Z chunk 边长）
    const int originX = snap.originX, originZ = snap.originZ; // chunk 世界起点

    if (snap.valid()) {
        verts.reserve(4096);
        idx.reserve(8192);
    }

    // 图集瓦片横排：20 瓦片（320×16）。BlockRegistry 为各方块定义 0..19 序号，
    // 与 tools/build_atlas.py 打包顺序严格一致（一个偏差即渗色/错贴）：
    //   0=grass_top 1=grass_side 2=dirt 3=stone 4=sand
    //   5=cobble 6=log_top 7=log_side 8=planks 9=leaves
    //   10=crafting_table_top 11=crafting_table_side（t50）
    //   12=furnace_top 13=furnace_side 14=furnace_front（t80）
    //   15=coal_ore 16=iron_ore（t84；矿石各面同贴图）
    //   17=torch（t88；6 面同贴图）
    //   18=bedrock（t119；6 面同贴图，深灰斑驳底岩）
    //   19=water（t148；6 面同贴图，蓝；纹理不透明，半透由水材质 opacity=0.7 实现）
    //   20=chest_top / 21=chest_side / 22=chest_front（t173；箱子方块各面贴图）
    // 半纹素内缩防渗色（线性采样跨瓦片）。N 读 BlockRegistry::AtlasTileCount（单一权威，
    //   与 BlockCube / build_atlas.py 同源——消除「两处各持魔数、加瓦片漏改一份」回归类，见 t182）。
    //   t668 HD 图集：半纹素 = 0.5px 折算成归一化 UV = 0.5/(N × kAtlasTilePx)；垂直维同理 0.5/kAtlasTilePx
    //   （图集高 = 1 瓦片）。旧 16px 时内缩 1/32 瓦片宽，64px 后缩到 1/128（瓦片更密 → 内缩更小才不裁掉
    //   有效像素；**必须与 kAtlasTilePx 同步**，改一漏一 → 采到跨瓦片渗色或瓦片边缘被裁）。
    constexpr int N = BlockRegistry::AtlasTileCount;
    constexpr float tileW = 1.0f / N;
    constexpr float hx = 0.5f / (N * BlockRegistry::kAtlasTilePx);
    constexpr float hy = 0.5f / BlockRegistry::kAtlasTilePx;
    const float v0 = 0.0f + hy, v1 = 1.0f - hy;

    // t151 真光场 + t153 PCF 软影顶点色（PLAN §2-H / §M，替代 t123 方向太阳 faceVc）：
    //   光场基底 = 邻格（面所朝向的空气格）的 max(sky, block)/15，由 World 的 BFS flood-fill 算出（存 chunk
    //   第三数组），mesher 只读采样。t153 在此基底上叠 PCF 软影：天光分量再乘 (1 - sunShadowAt)，把「太阳
    //   被邻近高地遮挡」处压暗（heightmap 正交深度图沿 sunDir 步进、2×2 PCF 软过渡）；火把方光（block）
    //   不受影（取 max 保留）。
    //   kVcMin / kVcMax 取自 voxellight.h（VoxelLight::kVcMin/kVcMax）—— mesher 与 BlockCube（t257 掉落沙）
    //   共用同一顶点色钳制曲线，保证「掉落沙与地形同亮度」（修暗处挖底沙变亮根因）。
    //
    // PLAN §2-H 夜间火把发光修复（R19 B6）：昼夜天光乘子 dayMul **只乘天光分量**、**绝不乘方块光分量**。
    //   公式 vc = clamp(max(sky/15 × (1 - 软影) × dayMul, block/15), kVcMin, kVcMax)。机制：方块光（火把/熔炉
    //   flood-fill）时间不变，昼夜只调制天光；火把光池（block/15≈0.93）在任何 dayMul 下都全亮 → 夜间火把发光
    //   （修旧版「夜间火把被压到 0.37」根因：旧 dayMul 留在 QML baseColor，会同时压暗 block 通道）。地形材质
    //   baseColor 改白（不再承担昼夜）；dayMul 由 setDayMul 注入（量化门控重烘，非 10Hz 全量重建）。
    constexpr float kVcMin = VoxelLight::kVcMin; // 暗部地板最低亮度（洞穴/阴影最低，仍远低于火把光池 0.93 保持对比）
    constexpr float kVcMax = VoxelLight::kVcMax;
    const float dayMul = snap.dayMul; // 本帧烘光的昼夜天光乘子（只乘 sky 项，保 block 项时间不变）

    // t1023 AO 角点遮挡因子（地形段逐格 culled 路径专属；snap.aoEnabled=true 才被调用）：
    //   经典 MC AO——面邻格（空气侧）为基，沿面内两轴 ±1 的两侧格 + 对角格共 3 探针，遮挡判据 =
    //   occludesNeighborFace（满格不透明立方，叶不遮——与邻面剔除同一谓词单一权威），因子曲线 =
    //   VoxelLight::kAoFactor（双侧同遮钳 3，防薄墙 / 凹角三重压黑）。探针走 snap.blockAtWorld
    //   （快照 pad 域覆盖探针 ±2 最大触达；域外 = 空气 = 无遮挡，与旧跨 chunk 路由/越界语义一致）。
    const auto aoCorner = [&snap](int nax, int nay, int naz,
                                 int axisU, int axisV, int signU, int signV) -> float {
        const int s1[3] = { nax + (axisU == 0 ? signU : 0),
                            nay + (axisU == 1 ? signU : 0),
                            naz + (axisU == 2 ? signU : 0) };
        const int s2[3] = { nax + (axisV == 0 ? signV : 0),
                            nay + (axisV == 1 ? signV : 0),
                            naz + (axisV == 2 ? signV : 0) };
        const int kc[3] = { s1[0] + (axisV == 0 ? signV : 0),
                            s1[1] + (axisV == 1 ? signV : 0),
                            s1[2] + (axisV == 2 ? signV : 0) };
        const bool o1 = occludesNeighborFace(snap.blockAtWorld(s1[0], s1[1], s1[2]));
        const bool o2 = occludesNeighborFace(snap.blockAtWorld(s2[0], s2[1], s2[2]));
        const bool oc = occludesNeighborFace(snap.blockAtWorld(kc[0], kc[1], kc[2]));
        return VoxelLight::aoCornerFactor(o1, o2, oc);
    };

    if (snap.valid()) {
        // ---- PASS 1：不完整方块（异形）合批进同一 chunk mesh（t133 PartialBlockGeometry）----
        //   **terrain 段独有**（水段无 partial；waterOnly 守卫防水段 ChunkGeometry 重复渲染异形方块）。
        //   每 cell 仅一次 append。独立于 PASS 2 的面 mask——否则 6 面 mask 各扫一次会 6× 重复 append。
        //   torch / 整立方 / 水不进此 pass。光照上下文（cellLight）按本格光场 + 本格中心 PCF 软影算
        //   （同 t151/t153 异形约定），打包进 PartialLightCtx 传入。
        if (!snap.waterOnly && !snap.lavaOnly && !snap.glassOnly && !snap.iceOnly) for (int ly = 0; ly < H; ++ly) { // t343/t405/t468：岩浆/玻璃/冰段只画对应立方面，跳过 partial/cross（PASS 1）
            for (int lz = 0; lz < S; ++lz) {
                for (int lx = 0; lx < S; ++lx) {
                    const int wx = originX + lx, wz = originZ + lz;
                    const quint8 b = snap.blockAtWorld(wx, ly, wz);
                    if (b == 0) continue;
                    if (b == BlockRegistry::Water) continue;       // 水走 PASS 2 立方面（水段）
                    if (b == BlockRegistry::Lava) continue;        // t343 岩浆走 PASS 2 立方面（岩浆段，独立材质）
                    if (b == BlockRegistry::Torch) continue;       // 火把走 torchHost（QML Model）
                    if (b == BlockRegistry::Painting) continue;    // t720 画作走 paintingHost（QML delegate，贴图不进图集）——非 partial 非 cross，双 PASS 均跳过
                    if (b == BlockRegistry::Fire) continue;        // t724 火焰走 fireHost（QML delegate 两片对角交叉双面 quad + fire_strip 翻书）——非 partial 非 cross，双 PASS 均跳过
                    if (b == BlockRegistry::NetherPortal) continue; // t725 余烬门走 portalHost（QML delegate 竖直平面 quad + portal_strip 翻书）——非 partial 非 cross，双 PASS 均跳过
                    if (b == BlockRegistry::Spawner) continue;    // t760 刷怪笼走 spawnerHost（QML delegate：BlockCube cutout 铁笼壳 + 笼内旋转迷你蠹虫）——整笼 delegate 渲染，双 PASS 均跳过
                    // t194：必须闭区间 [FirstPartial, LastPartial]。段后整立方（Chest=22）虽 id 更大但非异形
                    //   （ShapeFull，走 PASS 2 立方面）。旧单边 `b >= FirstPartial` 把 Chest 误路由进 PartialBlockGeometry
                    //   （switch 无 case → 0 顶点 → 放置后透明透视格子）。Water/Torch 在上方已显式 continue。
                    // t235：cross 广告牌方块段 [FirstCross, LastCross]（草丛）亦进此 pass（pushCross 生成对角双面
                    //   quad）。与 partial 盒体段并列、闭区间判定（同 t194 教训）。
                    // t305：cross 路由改用 isCrossBillboard 谓词（连续段 ∪ {Sapling}）—— Sapling(28) id 不在
                    //   [FirstCross,LastCross]=[24,25] 连续段内（DiamondOre/Wool 夹中间且非 cross），故并入谓词。
                    // t412：partial 路由改用 isPartialBlock 谓词（[FirstPartial,LastPartial] ∪ 段外圆石变体）——
                    //   CobbleSlab/Stairs/Fence/PressurePlate id(58..61) 不在连续段内（中间夹大量非异形方块），
                    //   故并入谓词（同 isCrossBillboard 段外 cross 模式）。
                    const bool isPartialX = BlockRegistry::isPartialBlock(b)
                                            || b == BlockRegistry::Farmland // t408 耕地矮盒经 PartialBlockGeometry 渲染（露 1/16 唇）
                                            || b == BlockRegistry::Cactus   // t445 仙人掌 0.8 细柱经 PartialBlockGeometry 渲染（非满格）
                                            || b == BlockRegistry::SnowLayer // t505 积雪层薄板经 PartialBlockGeometry 渲染（state 高度 1/8..1.0；非满格）
                                            || b == BlockRegistry::EnchantingTable // t620 附魔台 0.75 矮盒经 PartialBlockGeometry 渲染（非满格）
                                            || BlockRegistry::isAnvil(b)     // t766 铁砧三盒异形（基座+腰柱+砧台）经 PartialBlockGeometry 渲染（非满格；isAnvil 覆盖三阶段 id）
                                            || BlockRegistry::isBed(b);     // t457 床低 3D 模型经 PartialBlockGeometry 渲染（非整立方）
                    const bool isCrossX   = BlockRegistry::isCrossBillboard(b);
                    // t638 ① 木门镂空窗：门上半格栅窗贴图带 alpha（pack door_wood_upper.png 窗格真透明 /
                    //   程序贴图 t638 改窗洞 alpha=0）→ 门须走 **cutout 段**（alphaMode:Mask 材质——alpha<0.5
                    //   像素 discard）才能透视窗后（terrain 段不透明材质会把透明窗画成黑 / 暗色板）。门盒体
                    //   pushBox 几何不变（非 cross），仅路由到 cutout 段渲染（isDoor 门族全格两半都走 cutout——
                    //   下半门板不透明贴图 cutout 无副作用（alpha 全 255 不 discard），保同一方块单段渲染）。
                    const bool isDoorX = BlockRegistry::isDoor(b);
                    // t723 铁活板门栅格孔：iron_trapdoor(178) 贴图两列栅格孔真透明（同门窗 t638① 语义）→
                    //   走 cutout 段（alphaMode:Mask）透视孔后。t879② 木活板门大面贴图改 180 四镂空板
                    //   （孔 alpha=0 真透明）→ 同族入 cutout 段（两活板门同段渲染，分族注释退役；其余
                    //   partial 盒体贴图不透明仍留 terrain 段零回归）。
                    const bool isCutoutTrapX = (b == BlockRegistry::IronTrapdoor
                                                || b == BlockRegistry::WoodTrapdoor);
                    // t326 cross cutout 分流（**t860 R19.14 已折叠**）：历史上 cross（草丛/作物/树苗）+
                    //   门（t638 窗格 alpha）+ 活板门（t723 栅格孔）拆独立 cutout 段 + Mask 材质，因彼时
                    //   terrain 段材质还是 Opaque（opacity=1 无 alphaMode → alpha 被忽略 → 透明底显实心板）。
                    //   t442 起 terrain 段材质已带 alphaMode:Mask + alphaCutoff:0.5（leaves 透明间隙硬丢弃），
                    //   与 cutout 段材质（t439 起 Mask）**逐字相同** → 独立段无存在必要。t860 折叠：terrain 段
                    //   不再跳过 cross/door/trapdoor（并入本段 mesh，同材质同光照管线同 Mask 深度写 pass，
                    //   逐像素等价），QML 停建 cutout 段 Model（每 chunk 6 段 → 5 段，600 Model 满配 → 500）。
                    //   **降级杠杆 = cutoutFolded 显式开关（review28 #4）**：false 时本段退回 t860 前跳过
                    //   清单（cross/门/活板门让位 QML 恢复的独立 cutout 段；两段同发 = 同几何同材质逐顶点
                    //   重合 z-fighting——旧注释「QML 只恢复 createObject 一行即回 6 段」正是漏了本半边）。
                    //   Main.qml window.cutoutSegmentRestored 单开关联动本属性 / cutout 段实例化 /
                    //   segmentsPerChunk，恢复 = 翻一个属性（chunk 构建期置位），不可能只恢复一半。
                    if (snap.cutoutOnly) {
                        if (!isCrossX && !isDoorX && !isCutoutTrapX) continue;  // cutout 段：仅 cross + 门 + 活板门（铁 723 栅格孔 / 木 t879 四镂空板，alpha cutout 透视）
                    } else if (!snap.cutoutFolded) {
                        // 恢复态（cutoutFolded=false）：t860 前跳过清单逐字回归——cross/门/活板门走独立
                        //   cutout 段，本段仅 partial 盒体（立方面照走 PASS 2）。
                        if (isCrossX) continue;
                        if (isDoorX) continue;
                        if (isCutoutTrapX) continue;
                        if (!isPartialX) continue;
                    } else {
                        // t860 折叠态（默认）：partial 盒体 + cross + 门 + 活板门全收（唯 PASS 2 立方面
                        //   跳过清单不变——cross/door/trapdoor 本就只走 PASS 1，无双重发射）。
                        if (!isPartialX && !isCrossX && !isDoorX && !isCutoutTrapX) continue;
                    }
                    const quint8 cSky = snap.skyLightAt(wx, ly, wz);
                    const quint8 cBlock = snap.blockLightAt(wx, ly, wz);
                    const float cShadow = sunShadowAt(float(wx) + 0.5f, float(ly) + 0.5f, float(wz) + 0.5f);
                    // PLAN §2-H：dayMul 只乘天光分量（cross 本格光场），block 项保留 → 夜间火把附近 cross 仍亮。
                    const float cellLight = std::clamp(
                        std::max((cSky / 15.0f) * (1.0f - cShadow) * dayMul, cBlock / 15.0f), kVcMin, kVcMax);
                    const quint8 st = snap.stateAtWorld(wx, ly, wz);
                    // t360 异形方块光照：cross 用本格光场（cellLight）；pushBox 各面用「面所朝邻格」flood 光
                    //   （修合活版门/下半砖顶面自影：旧采被本格 lightOpacity 压暗的本格值）。6 向邻格 sky/block
                    //   + 各面代表点（面中心世界位）PCF → clamp(max(sky*(1-sh), block))。顺序同 kBoxFaces
                    //   [+X,-X,+Y,-Y,+Z,-Z]，与 partialblockgeometry pushBox 取 face[fi] 一一对应。
                    struct FaceSrc { int dx, dy, dz; float px, py, pz; };
                    const FaceSrc fs[6] = {
                        { 1, 0, 0, float(wx + 1),     float(ly) + 0.5f, float(wz) + 0.5f}, // +X
                        {-1, 0, 0, float(wx),         float(ly) + 0.5f, float(wz) + 0.5f}, // -X
                        { 0, 1, 0, float(wx) + 0.5f,  float(ly + 1),   float(wz) + 0.5f}, // +Y
                        { 0,-1, 0, float(wx) + 0.5f,  float(ly),       float(wz) + 0.5f}, // -Y
                        { 0, 0, 1, float(wx) + 0.5f,  float(ly) + 0.5f, float(wz + 1)},   // +Z
                        { 0, 0,-1, float(wx) + 0.5f,  float(ly) + 0.5f, float(wz)},       // -Z
                    };
                    PartialLightCtx lctx;
                    lctx.light = cellLight;
                    for (int i = 0; i < 6; ++i) {
                        const int nx = wx + fs[i].dx, ny = ly + fs[i].dy, nz = wz + fs[i].dz;
                        const float nbSkyF = snap.skyLightAt(nx, ny, nz) / 15.0f;
                        const float nbBlockF = snap.blockLightAt(nx, ny, nz) / 15.0f;
                        const float sh = sunShadowAt(fs[i].px, fs[i].py, fs[i].pz);
                        // PLAN §2-H：dayMul 只乘天光分量（异形盒体各面光场），block 项保留。
                        lctx.face[i] = std::clamp(std::max(nbSkyF * (1.0f - sh) * dayMul, nbBlockF), kVcMin, kVcMax);
                    }
                    // t408 耕地从 PASS 2 立方面迁到 PASS 1 矮盒（PartialBlockGeometry），原 PASS 2 顶面湿润暗化
                    //   （farmlandHydrBrightMul，darker=wetter）改在此预乘 lctx.face[+Y(=2)]，使矮盒顶面顶点色仍随
                    //   state 低 2 位湿润等级渐暗（机制不变，仅消费点迁移）。
                    if (b == BlockRegistry::Farmland)
                        lctx.face[2] *= farmlandHydrBrightMul(quint8(st & BlockRegistry::FarmlandHydrationMask));
                    // t209 栅栏连接：查 4 向水平邻居 id（跨 chunk 经 blockAtWorld 路由，边界邻居正确）。
                    //   仅 fence 读本上下文；其余异形方块忽略。边界格破/放已标邻 chunk 脏（ChunkManager::setBlock
                    //   在 lx/lz 贴边时标邻接脏）→ 跨 chunk 栅栏连接随邻居重网格化自动更新。
                    //   t667 铁轨族（普通 / 动力 / 探测）：追加填 4 向三高（同 / 上 / 下）邻轨高度差
                    //   （railProbeDelta 单一权威，同 World::recomputeRailConnections 的探针形态）→
                    //   PartialBlockGeometry Rail case 直轨段据此把 quad 端边抬高画坡度（低端画坡高端平铺）。
                    PartialNeighborCtx nctx{
                        snap.blockAtWorld(wx + 1, ly, wz),
                        snap.blockAtWorld(wx - 1, ly, wz),
                        snap.blockAtWorld(wx, ly, wz + 1),
                        snap.blockAtWorld(wx, ly, wz - 1),
                    };
                    if (BlockRegistry::isRail(b)) {
                        const auto dprobe = [&](int dx, int dz) {
                            return BlockRegistry::railProbeDelta(
                                { snap.blockAtWorld(wx + dx, ly, wz + dz),
                                  snap.blockAtWorld(wx + dx, ly + 1, wz + dz),
                                  snap.blockAtWorld(wx + dx, ly - 1, wz + dz) });
                        };
                        nctx.railDeltaPx = dprobe(1, 0);
                        nctx.railDeltaNx = dprobe(-1, 0);
                        nctx.railDeltaPz = dprobe(0, 1);
                        nctx.railDeltaNz = dprobe(0, -1);
                    }
                    // t702 红石粉爬墙探针：该向水平邻的上 / 下一格是粉 → +1 / -1（World 连接位 /
                    //   电力互通同判据，渲染据 >0 把该向线臂画成上坡斜段；同铁轨 dprobe 三高探针模式）。
                    if (b == BlockRegistry::RedstoneDust) {
                        const auto dclimb = [&](int dx, int dz) {
                            if (BlockRegistry::isRedstoneDust(snap.blockAtWorld(wx + dx, ly + 1, wz + dz))) return 1;
                            if (BlockRegistry::isRedstoneDust(snap.blockAtWorld(wx + dx, ly - 1, wz + dz))) return -1;
                            return 0;
                        };
                        nctx.dustClimbPx = dclimb(1, 0);
                        nctx.dustClimbNx = dclimb(-1, 0);
                        nctx.dustClimbPz = dclimb(0, 1);
                        nctx.dustClimbNz = dclimb(0, -1);
                    }
                    // review27 #15① 睡莲叶高上下文：填同列下一格 id/state（仅 LilyPad 用——叶 quad 高度读
                    //   下方水格液面 waterSurfaceFrac，partialblockgeometry LilyPad case 消费；其余异形
                    //   方块忽略，零成本）。
                    if (b == BlockRegistry::LilyPad) {
                        nctx.belowId = snap.blockAtWorld(wx, ly - 1, wz);
                        nctx.belowState = snap.stateAtWorld(wx, ly - 1, wz);
                    }
                    PartialBlockGeometry::append(verts, idx, lx, ly, lz, b, st,
                                                 lctx, nctx, tileW, hx, hy, v0, v1);
                }
            }
        }

        // ---- PASS 2：立方面网格化（terrain 段 culled/greedy；水段 t197 变高水面专用路径）----
        //   t326：cutout 段（cutoutOnly）只发 cross 顶点（PASS 1），无水 / 无整立方面 → 跳过 PASS 2。
        if (snap.cutoutOnly) {
            // cutout 段：cross 仅在 PASS 1（pushCross 双面 quad）；无立方面、无水。空分支，跳过下方三路。
        } else if (snap.waterOnly || snap.lavaOnly) {
            // t197 水位视觉 / t351 岩浆分层视觉：流体段（水 / 岩浆）不再画满格立方，而是按 cell 的 state(level)
            //   降液面高度 + 流体用独立贴图，呈现 MC 式逐格衰减流动（修「所有流体格同高满液位 → 看着静止/全平」）。
            //   t351：岩浆段复用此变高路径（旧岩浆段走 culled/greedy 满格立方 → 岩浆面全平，无分层；现 lavaOnly
            //   进本分支按 state 降液面，机制等价水的分层流动）。参数差异由下方 fluidId/maxLevel/各 tile 区分。
            //
            //   水面高度：水源(state=0) 满高 1.0（用 water=19 静水贴图）；流水(state=1..7) 水面 = (8-level)/8
            //   逐级降（用 water_flow=23 流水贴图）。机制等价 MC 1.0 流水 8 级衰减（机制对齐，非精确数值复刻）。
            //
            //   面剔除（关键：水位感知，决定本格水面与邻居水面间的暴露带 / 瀑布阶梯）：
            //     · 顶面(+Y)：上方为空气 → 画在 myTop（水面）；上方为实体/水 → 剔除。
            //     · 底面(-Y)：下方为空气 → 画在 0；下方为实体/水 → 剔除（流水悬空下方可见底）。
            //     · 侧面(±X/±Z)：邻实体 → 剔除（地形挡）；邻空气 → 画本格满侧（自 0 至 myTop）；
            //       邻水 → 比较水面：邻居水面 >= 本格 → 整面剔除；邻居水面 < 本格 → 画本格水面到邻居水面
            //       之间的暴露垂直带（瀑布阶梯感 —— 两格水面落差处露出高格的侧壁）。
            //   顶点色光照沿用 t151 真光场 + t153 PCF 软影（同 terrain culled 约定：邻格 sky/block + per-vertex 软影）。
            //
            //   分层（PLAN §2）：本段属 Renderer（mesher），只读快照（block/state/光照/列顶）—— 水面高度是
            //   mesher 据 state 的纯渲染层计算，不写栅格、不进 BlockDef（Water 方块 def 仍各面=19 静水；流水
            //   贴图选择是呈现层决定，非方块属性）。
            // t351 流体参数化（水 / 岩浆共用此变高路径，差异仅 id / 贴图 / 最大 level）：
            //   fluidId：本段画哪种流体（水段=Water / 岩浆段=Lava）。
            //   maxLevel：液面衰减分级。水 8 级（state 1..7 → 液面 (8-s)/8）；岩浆 4 级（state 1..3 → 液面 (4-s)/4）。
            const quint8 fluidId  = snap.lavaOnly ? BlockRegistry::Lava  : BlockRegistry::Water;
            const int maxLevel    = snap.lavaOnly ? 4 : 8;              // 源(0) + 流(1..maxLevel-1)
            // t489 材质级 flipbook：水/岩浆段不再采样共享图集（tile 19/23/42），改采样**独立条带纹理**
            //   （waterStrip: 2 列×32 帧 / lavaStrip: 1 列×16 帧）。面 UV 烘焙为「单帧区域」约定：
            //     - u：水双列——水源(st==0) 采左列（静水）、流水(st>0) 采右列（流水）；岩浆单列。
            //     - v ∈ [0, 1/N]：帧 0 区（v=0=条带底）。帧切换由材质 positionV 动画（QtQuick3D Texture 6.11 已把
            //       vOffset 更名 positionV；positive positionV 上移采样 → 帧 k 在 v∈[k/N,(k+1)/N]）驱动——
            //       **mesh 一次性构建、动画纯材质参数，零 buildMesh**（修 t222/t223「水 2s 一次全量重建水段 261 段/次」
            //       的 mesh 重建风暴回归；F3 [w]/[s] reb 不回升）。静水/流水同帧索引同步动画（机制等价 MC 1.0
            //       still/flow 同步 flipbook）。
            //   t391 水面顶面空间涟漪（brightMul/alphaMul）仍按 phase 0 烘死（材质动画接管时间维度的荡漾，
            //   空间维度的反光起伏保留在顶点色）。tiles 19/23/24/25/42（图集水/岩浆瓦片）保留供 BlockCube
            //   手持 / 掉落路径消费；水/岩浆**段** mesh 不再引用图集瓦片。
            //   帧数 N / 帧像素 16 与 blockregistry kWaterStripFrames/kLavaStripFrames/kFluidStripFramePx 同源单一权威
            //   （条带构建方 resourcepackmanager/build_fluid_strips.py 与此处 UV 烘焙 + Main.qml positionV 步长三方共用）。
            const int stripFrames = snap.lavaOnly ? BlockRegistry::kLavaStripFrames : BlockRegistry::kWaterStripFrames;
            const float frameH = 1.0f / float(stripFrames);                       // 单帧高（帧 0 区 v∈[0,1/N]）
            // 半像素内缩防帧间 / 列间渗色（线性采样越过帧/列边界采到相邻帧/列）。
            //   **关键**：内缩量是「半纹素」，纹素的归一化 V 尺寸 = 1/条带总高 = 1/(N×帧高px)，不是 1/帧高px
            //   ——早期写成 0.5/帧高px 会恰好等于半帧高（N=16 时 0.5/16 = 0.03125 = (1/16)/2），把帧 0 区
            //   [0,1/N] 内缩成单点 → 面四顶点 v 全相同 → V 维坍缩（采单行纹素）。正确：hys = 0.5/(N×帧高px)。
            //   水条带高 32×16=512 → hys=0.5/512；岩浆条带高 16×16=256 → hys=0.5/256。
            //   hxs：水双列条带宽 32px → 0.5/32；岩浆单列 16px → 0.5/16。
            const float stripPxH = float(stripFrames * BlockRegistry::kFluidStripFramePx); // 条带总高（像素）
            const float hys = 0.5f / stripPxH;
            const float hxs = snap.lavaOnly ? (0.5f / float(BlockRegistry::kFluidStripFramePx))
                                         : (0.5f / float(2 * BlockRegistry::kFluidStripFramePx));
            const float stripV0 = hys, stripV1 = frameH - hys;                    // v 子区（帧 0 区，内缩）
            // state → 液面高度（cell-local Y，0..1）。**t892 水走 BlockRegistry::waterSurfaceFrac 单一权威**：
            //   水源(st==0)=7/8（表面比方块顶低 2 像素，机制等价 MC 1.0 静水 14/16；连带耕地 15/16 顶不再
            //   被满格水漫过——用户「耕地比水还低」透视错乱随之消失）；流(st>0)=(8−min(s,7))/8（口径不变）。
            //   岩浆（snap.lavaOnly，maxLevel=4）保持本地折算：源满格 1.0 / 流 (4−s)/4（岩浆无 t892 降位语义）。
            auto surfH = [maxLevel](quint8 state) -> float {
                if (maxLevel == 8) return BlockRegistry::waterSurfaceFrac(state); // 水：单一权威（源 7/8）
                if (state == 0) return 1.0f;
                const int s = (int(state) > maxLevel - 1) ? (maxLevel - 1) : int(state);
                return (float(maxLevel) - float(s)) / float(maxLevel);
            };
            // t350 renderTop：流体格的**实际渲染**顶高（含竖向柱连续性修正）。**t892 起判序反转：先查正上方
            //   同种流体再折液面**——水源(7/8)也要参与柱连续（深水湖内部源格上方仍是水 → 满块 1.0，仅
            //   柱顶格露空气位降 7/8）；流(st>0) 的 slab 高 = surfH(state)，上方同种流体（竖向柱 / 下落流
            //   中段）→ 1.0（满块）。
            //   修「竖向堆叠流格间露出空气带」：流 slab 仅占 cell 下部，上方留空；两流格上下堆叠时，下格侧壁止于
            //   其 slab 顶、上格侧壁起于本 cell 底 → slab 顶与本 cell 底之间一段无侧壁 → 透视见空气带。被上方同种
            //   流体覆盖的流格属柱内 → 渲染满高，侧壁贯通相邻格 → 柱连续无缝。顶格（上方 air）保 slab 液面高。
            //   blockAtWorld 越界返 Air → 顶格不触发满高修正。t351：流体判定由硬编码 Water 改为 fluidId（水 / 岩浆通用）。
            auto renderTop = [&](quint8 state, int ax, int ay, int az) -> float {
                if (snap.blockAtWorld(ax, ay + 1, az) == fluidId) return 1.0f; // 上方有同种流体 → 柱内满块（t892 起先判）
                return surfH(state);
            };
            for (int ly = 0; ly < H; ++ly) {
                for (int lz = 0; lz < S; ++lz) {
                    for (int lx = 0; lx < S; ++lx) {
                        const int wx = originX + lx, wz = originZ + lz;
                        if (snap.blockAtWorld(wx, ly, wz) != fluidId) continue;
                        const quint8 st = snap.stateAtWorld(wx, ly, wz);
                        float myTop = renderTop(st, wx, ly, wz); // t350：上方有水 → 满高（柱连续无缝）
                        // t639⑤ 耕地邻面水面 cap：水源 / 流格水平 4 向邻格 == Farmland（耕地矮盒顶
                        //   15/16=0.9375）→ 本格液面 cap 到 15/16，消除「满高 1.0 水面邻耕地凸出 1/16」
                        //   观感破绽（水不漫过耕地顶，接缝齐平）。**t892 起对暴露液面为 no-op**（水源已降
                        //   7/8 < 15/16，「耕地比水还低」透视错乱随水位下降根治）；仅剩柱内满块（上方同种
                        //   流体 → renderTop 1.0）邻耕地的情形仍被本 cap 压平 —— 保留防柱内格回归。
                        if (snap.blockAtWorld(wx + 1, ly, wz) == BlockRegistry::Farmland
                            || snap.blockAtWorld(wx - 1, ly, wz) == BlockRegistry::Farmland
                            || snap.blockAtWorld(wx, ly, wz + 1) == BlockRegistry::Farmland
                            || snap.blockAtWorld(wx, ly, wz - 1) == BlockRegistry::Farmland)
                            myTop = std::min(myTop, 15.0f / 16.0f);
                        // t489：条带 UV（替代图集 tile UV）。水双列：源(st==0)→左列静水、流(st>0)→右列流水；
                        //   岩浆单列。u ∈ 列宽 [colX, colX+0.5/1.0]，v ∈ [0,1/N]（帧 0 区，内缩）。详见上方 t489 注释。
                        const float colL = snap.lavaOnly ? 0.0f : ((st == 0) ? 0.0f : 0.5f); // 左列(still) / 右列(flow)
                        const float colW = snap.lavaOnly ? 1.0f : 0.5f;                      // 单列宽（岩浆整宽 / 水半宽）
                        const float u0 = colL + hxs, u1 = colL + colW - hxs;
                        // t893 流向四向动画匹配：流水条带（右列）图案随帧沿 −v 方向移动（build_fluid_strips
                        //   roll_y 下移 + t563 帧内容保向）→ 把「−v 方向」映射到本格**离源流向** D 即观感顺流。
                        //   流向判定与 ItemEntityManager 掉落物随流 / PlayerController t211 玩家水流推力同源
                        //   算法：4 向水邻居 state 梯度（低 state = 近源 → 流向背它），量化到主轴四向。
                        //   静止源（st==0 左列）/ 岩浆 / 孤立流格（无更低 state 邻居，不可判）→ 无向恒等
                        //   （spec「静止面无向」）。仅重排/翻转角点坐标：u 仍锁列窗 [u0,u1]、v 仍锁帧 0 子区
                        //   → positionV 翻书不受扰，零 mesh 重建语义不变。
                        int flowDir = 0; // 0=无向 / 1=+X / 2=−X / 3=+Z / 4=−Z
                        if (!snap.lavaOnly && st > 0) {
                            float fgx = 0.0f, fgz = 0.0f;
                            constexpr int flowDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                            for (const auto &fd : flowDirs) {
                                const int fnx = wx + fd[0], fnz = wz + fd[1];
                                if (snap.blockAtWorld(fnx, ly, fnz) == fluidId) {
                                    const quint8 fns = snap.stateAtWorld(fnx, ly, fnz);
                                    if (fns < st) { // 该邻居更近源 → 流向朝远离它
                                        fgx -= float(fd[0]) * float(st - fns);
                                        fgz -= float(fd[1]) * float(st - fns);
                                    }
                                }
                            }
                            if (std::fabs(fgx) > 1e-4f || std::fabs(fgz) > 1e-4f) {
                                if (std::fabs(fgx) >= std::fabs(fgz)) flowDir = fgx > 0.0f ? 1 : 2;
                                else flowDir = fgz > 0.0f ? 3 : 4;
                            }
                        }
                        for (int f = 0; f < 6; ++f) {
                            const FaceDef &F = kFaces[f];
                            const int nwx = wx + F.dir[0], nwy = ly + F.dir[1], nwz = wz + F.dir[2];
                            const quint8 nb = snap.blockAtWorld(nwx, nwy, nwz);
                            // t563 ③：邻接**异种流体**（水↔岩浆）→ 剔本面。水/岩浆各占独立透明 mesh 段
                            //   （水 opacity 0.7 / 岩浆 0.95，均透明 pass），在分界面**同一平面**各画一张侧壁
                            //   （本段流体朝对方：isSolid(对方)=false 不剔除、≠fluidId 不剔除 → 落 else 画满侧；
                            //   对方段朝本段同理）→ 两张共面半透明面 → z-fighting 逐帧闪烁（用户「水岩浆混合
                            //   闪烁」）。剔本面（对方段也不画）→ 分界面无共面 → 不闪烁；两流体体积在分界处
                            //   相接（交互凝固 obsidian/stone/cobble 由 tick 处理，此处仅解决渲染闪烁）。
                            const bool nbOtherFluid = (nb == BlockRegistry::Lava || nb == BlockRegistry::Water)
                                                      && nb != fluidId;
                            // 决定本面是否画 + 画的垂直区间 [yLo, yHi]（cell-local）。
                            //   水平面(±Y)：yLo=yHi（单层）；侧面(±X/±Z)：[yLo,yHi] 可能是部分带。
                            float yLo = 0.0f, yHi = myTop;
                            if (F.dir[1] != 0) {
                                // 顶/底面：邻(上/下)为实体或水 → 剔除；为空气 → 画在水面 / 底。
                                // t746 叶邻不剔（occludesNeighborFace）：树叶盖在水面上时，水面若被剔，
                                //   叶孔直通水体内部 → 同地形透视病；画回水面后叶孔下见水表面。
                                if (occludesNeighborFace(nb)) continue;
                                if (nb == fluidId) continue;
                                if (nbOtherFluid) continue; // t563 ③：异种流体上下邻 → 剔共面（防 z-fight 闪烁）
                                yLo = yHi = (F.dir[1] > 0) ? myTop : 0.0f; // 顶在 myTop / 底在 0
                            } else {
                                // 侧面：邻实体剔除；邻空气画满侧 [0,myTop]；邻水按水面差画暴露带。
                                // t222：流水（state>0，降水面 myTop<1）邻实体方块时**不整面剔除**——画 [0,myTop]
                                //   满侧保持流水贴图可见。修「流水格被占（玩家放方块 / 自然地形邻接）→水面贴图
                                //   消失/透明、透视见底」：透明水材质（opacity 0.7）下侧壁封闭水体体积，从水面斜
                                //   透视不再穿透到背后的实体方块 / 水底（水体「满」而非「空壳」），机制等价 MC
                                //   流水贴着实体方块显侧壁。流水格被占（t198 setBlock 覆盖水→实体）后邻接流水 N
                                //   朝新实体面不再被 `isSolid→continue` 抹掉其 water_flow 侧壁贴图。
                                //   水源（state=0，t892 起液面 7/8）邻实体仍**剔除**：本面只有从实体内部
                                //   才可见（不可达），画了只在实体面上叠一层半透水色（z-fight / 渗色观感），
                                //   无视觉收益且会把所有水-地形接缝染蓝；7/8 上方露出的 1/8 空段由实体自身
                                //   满高侧面覆盖，无透视洞。
                                if (occludesNeighborFace(nb)) { // t746 叶邻不剔（叶孔后应见水侧壁而非 void）
                                    if (st == 0) continue; // 水源满高：邻实体完全遮挡 → 剔除（原行为）
                                    // 流水降水面：画 [0,myTop] 满侧（yLo=0,yHi=myTop 已是默认）保持贴图可见
                                } else if (nbOtherFluid) {
                                    continue; // t563 ③：异种流体水平邻 → 剔共面（防 z-fight 闪烁）
                                } else if (nb == fluidId) {
                                    const float nbrTop = renderTop(snap.stateAtWorld(nwx, nwy, nwz), nwx, nwy, nwz);
                                    if (nbrTop >= myTop - 1e-4f) continue;      // 邻居水面 >= 本格 → 整面剔除
                                    yLo = nbrTop;                                // 邻居更低 → 画邻居水面到本格水面间暴露带
                                    yHi = myTop;
                                } // else 邻空气：yLo=0, yHi=myTop（满侧）
                            }
                            // 光照（同 terrain culled：面所朝邻格的天光/方光 + per-vertex PCF 软影）。
                            const float nbSkyF = snap.skyLightAt(nwx, nwy, nwz) / 15.0f;
                            const float nbBlockF = snap.blockLightAt(nwx, nwy, nwz) / 15.0f;
                            const quint32 base = quint32(verts.size());
                            for (int cc = 0; cc < 4; ++cc) {
                                const float dx = F.c[cc][0], dy = F.c[cc][1], dz = F.c[cc][2];
                                // 顶点 Y：水平面四角同高（yHi）；侧面按角点 dy(0/1) 映射到 [yLo,yHi]。
                                const float yy = (F.dir[1] != 0) ? yHi : (dy ? yHi : yLo);
                                const float shadow = sunShadowAt(float(wx) + dx, float(ly) + yy, float(wz) + dz);
                                // PLAN §2-H：dayMul 只乘天光分量（水/岩浆变高面），block 项保留 → 夜间火把照亮的水面仍可见。
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow) * dayMul, nbBlockF),
                                                            kVcMin, kVcMax);
                                // UV：同 terrain culled 规则（±X cu=dz,cv=dy；±Z cu=dx,cv=dy；±Y cu=dx,cv=dz）。
                                //   侧面 cv=dy(0/1) → 贴图垂直方向 0..1 映射到 [yLo,yHi] 区间（部分带/矮水面
                                //   会让贴图竖向压缩/拉伸，图集 CLAMP 无法 REPEAT —— 已知图集路径权衡，见
                                //   lessons-learned greedy meshing 条）。
                                float cu, cv;
                                if (f == 0 || f == 1) { cu = dz; cv = dy; }       // ±X
                                else if (f == 4 || f == 5) { cu = dx; cv = dy; }  // ±Z
                                else { cu = dx; cv = dz; }                        // ±Y
                                // t893 流向旋转：把「−v 方向」（图案随帧移动向）映射到流向 D → 观感顺流。
                                //   顶/底面（±Y）：cv 轴 ≡ −D；侧面仅当流向有**沿墙水平分量**才把动画轴横置
                                //   （±X 墙看 ±Z 流、±Z 墙看 ±X 流），否则保持竖直向下（瀑布 / 正交流贴墙
                                //   「往下淌」，t563 语义）。cu 取被替换轴（u 方向不影响动画向，正交即可）。
                                if (flowDir != 0) {
                                    if (f == 2 || f == 3) {          // ±Y 面
                                        if (flowDir == 3) cv = 1.0f - dz;      // D=+Z：−v ≡ +Z
                                        else if (flowDir == 4) { /* D=−Z：cv=dz 恒等 */ }
                                        else if (flowDir == 1) { cv = 1.0f - dx; cu = dz; } // D=+X
                                        else              { cv = dx;        cu = dz; }     // D=−X
                                    } else if (f == 0 || f == 1) {    // ±X 墙（沿墙水平轴 = Z）
                                        if (flowDir == 3) { cv = 1.0f - dz; cu = dy; }      // D=+Z
                                        else if (flowDir == 4) { cv = dz; cu = dy; }        // D=−Z
                                    } else {                          // ±Z 墙（沿墙水平轴 = X）
                                        if (flowDir == 1) { cv = 1.0f - dx; cu = dy; }      // D=+X
                                        else if (flowDir == 2) { cv = dx; cu = dy; }        // D=−X
                                    }
                                }
                                // t391 水面波动/透明度润色（spec「水面有波动质感、非死板」）：仅水段顶面（+Y，f==2）
                                //   叠加一层**空间正弦涟漪**——每顶点据世界角点 (wx+dx, wz+dz) 算 sin（k=1.1，
                                //   周期 ~5.7 格，对角涟漪）；相邻 cell 共享同一角点 → 涟漪跨格连续（非逐格跳变）。
                                //   tXXX：涟漪相位**烘死为 phase 0**（静态水）——旧 flipbook 的 ±π 相位翻转
                                //   （切帧时亮/暗波带互换 = 「闪烁」感）需随 waterAnimPhase 重网格化，已随翻页
                                //   一并废弃；保留烘死的涟漪 → 水面仍有明暗波带质感、非全平死板，但不再随时间
                                //   翻转（静态水视觉损失可接受，见 tXXX 验收）。仅水（fluidId==Water，!snap.lavaOnly）；
                                //   岩浆段不参与（浓稠近不透、无涟漪语义）。
                                //   亮度 ±12%（反光起伏）+ vertex.a [0.85,1.0]（材质 opacity 0.7 × vertex.a → 有效
                                //   alpha [0.595,0.7]，透射起伏）；侧面/底面 brightMul=alphaMul=1（原行为）。
                                float brightMul = 1.0f, alphaMul = 1.0f;
                                if (!snap.lavaOnly && f == 2) { // +Y 顶面（水面）
                                    const float sarg = float(wx + dx + wz + dz) * 1.1f;
                                    const float wave = std::sin(sarg); // tXXX 静态水：相位 0（不再随 phase 翻转）
                                    brightMul = 1.0f + 0.12f * wave;                 // [0.88, 1.12] 反光起伏
                                    alphaMul = 0.85f + 0.15f * (0.5f + 0.5f * wave);   // [0.85, 1.00] 透射起伏
                                }
                                Vtx v;
                                v.x = float(lx) + dx; v.y = float(ly) + yy; v.z = float(lz) + dz; // 局部坐标
                                v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                                v.u = u0 + cu * (u1 - u0);
                                v.v = stripV0 + cv * (stripV1 - stripV0); // t489：条带帧 0 区（v∈[0,1/N]）；帧切换由材质 positionV 驱动
                                // t151 光场 × t153 PCF 软影顶点色；t391 水面顶面（+Y）再乘涟漪亮/透射因子。
                                v.r = vc * brightMul; v.g = vc * brightMul; v.b = vc * brightMul;
                                v.a = alphaMul; // 侧面/底面 = 1.0；水面顶面 = [0.85,1.0] 涟漪透射
                                verts.append(v);
                            }
                            idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
                            idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
                        }
                    }
                }
            }
        } else if (snap.greedyMeshing) {
            // t178 贪婪网格化（greedy meshing，PLAN §4 性能打磨）：按 6 面方向逐「层」建 2D mask，合并同
            //   (tile, 邻格天光, 邻格方光) 的共面连续格为单个矩形 → 顶点 / 三角 / 索引数大幅下降（平坦地面
            //   16×16=256 quad → 1 quad）。F3 叠层据此可观测 meshing 吞吐改善（PLAN §2-F）。
            //
            //   合并键含光照（tile + 邻格 sky + 邻格 block）→ 仅**均匀照明**区合并（保光照保真：合并 quad
            //   四角共享同一邻格光值，内部不被误暗；火把 / 墙边光照变化处不合并，贴图也保持逐格清晰）。
            //   PCF 软影仍 per-vertex（同 t153：合并 quad 四角各自 sunShadowAt → 影边光栅化平滑过渡）。
            //   贴图在合并 quad 上**拉伸**铺满（图集路径权衡：逐格平铺需纹理数组 = 自研 RHI 路径，已记录
            //   为推迟偏差 dev-plan 1/2 / PLAN §2-I）。greedyMeshing=false 可回退逐格 culled（清晰贴图）。
            struct MaskEntry { bool valid = false; int tile = 0; quint8 sky = 0; quint8 block = 0; quint8 hydr = 0; }; // t406 hydr = Farmland +Y 顶面湿润等级（进合并键防不同湿润度共面误并）
            std::vector<MaskEntry> mask;
            // 各面 UV 轴映射（须与历史逐格 culled 的 cu/cv 规则一致：±X cu=Z,cv=Y；±Y cu=X,cv=Z；±Z cu=X,cv=Y）。
            static const int kUVAxes[6][2] = {
                {2, 1}, {2, 1}, // ±X
                {0, 2}, {0, 2}, // ±Y
                {0, 1}, {0, 1}, // ±Z
            };
            for (int f = 0; f < 6; ++f) {
                const FaceDef &F = kFaces[f];
                // 法线轴（normalAxis）+ 两 in-plane 轴（axisA 带 merge 宽 w、axisB 带 merge 高 h）。
                const int normalAxis = (F.dir[0] != 0) ? 0 : (F.dir[1] != 0) ? 1 : 2;
                const int axisA = (normalAxis + 1) % 3;
                const int axisB = (normalAxis + 2) % 3;
                const int sizeN = (normalAxis == 1) ? H : S; // 沿法线轴的层数
                const int sizeA = (axisA == 1) ? H : S;
                const int sizeB = (axisB == 1) ? H : S;
                if (sizeA <= 0 || sizeB <= 0 || sizeN <= 0) continue;
                mask.assign(sizeA * sizeB, MaskEntry{});

                for (int n = 0; n < sizeN; ++n) {
                    // 建 mask：逐 (a,b) 判本格（normalAxis 层 = n）在面 f 是否出可见面。
                    for (int a = 0; a < sizeA; ++a) {
                        for (int b = 0; b < sizeB; ++b) {
                            int lc[3] = {0, 0, 0};
                            lc[normalAxis] = n; lc[axisA] = a; lc[axisB] = b;
                            const int lx = lc[0], ly = lc[1], lz = lc[2];
                            const int wx = originX + lx, wz = originZ + lz;
                            MaskEntry &e = mask[a * sizeB + b];
                            e.valid = false;
                            const quint8 blk = snap.blockAtWorld(wx, ly, wz);
                            if (blk == 0) continue;
                            const bool isWater = (blk == BlockRegistry::Water);
                            const bool isLava  = (blk == BlockRegistry::Lava);
                            const bool isGlass = (blk == BlockRegistry::Glass);
                            const bool isIceBlk = BlockRegistry::isIce(blk); // t468 冰族（Ice/PackIce/BlueIce）
                            // t343/t405/t468 段分流：岩浆段只画 Lava；水段只画 Water；玻璃段只画 Glass；冰段只画冰族；
                            //   地形段跳过流体+玻璃+冰族（各自独立段渲染）。
                            if (snap.iceOnly)   { if (!isIceBlk) continue; }
                            else if (snap.glassOnly) { if (!isGlass) continue; }
                            else if (snap.lavaOnly) { if (!isLava) continue; }
                            else if (snap.waterOnly) { if (!isWater) continue; }
                            else { if (isWater || isLava || isGlass || isIceBlk) continue; }       // 地形段跳流体 + 玻璃 + 冰族
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Torch) continue;
                            if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isPartialBlock(blk)) continue; // t412 异形已在 PASS 1（含段外圆石变体）；段后整立方（Chest）正常进立方面
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Farmland) continue; // t408 耕地矮盒已在 PASS 1；不进整立方面（否则满格立方覆盖矮盒唇）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isCrossBillboard(blk)) continue; // t235/t305 cross（草丛/作物/树苗）已在 PASS 1；不进立方面
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Cactus) continue; // t445 仙人掌 0.8 细柱已在 PASS 1；不进整立方面（否则满格立方覆盖细柱）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::SnowLayer) continue; // t505 积雪层薄板已在 PASS 1；不进整立方面（否则满格立方覆盖薄板）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isBed(blk)) continue; // t457 床低 3D 模型已在 PASS 1；不进整立方面（否则满格立方覆盖低床）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::EnchantingTable) continue; // t620 附魔台 0.75 矮盒已在 PASS 1；不进整立方面（否则满格立方覆盖矮盒）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isAnvil(blk)) continue; // t766 铁砧三盒异形已在 PASS 1；不进整立方面（否则满格立方覆盖三盒造型，退回「上下各一半」观感）
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Painting) continue; // t720 画作渲染走 paintingHost QML delegate（贴图不进图集）；立方面路径会把画格画成 tile 0 草顶立方
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Fire) continue; // t724 火焰渲染走 fireHost QML delegate（fire_strip 翻书条带不进图集）；立方面路径会把火格画成 tile 0 草顶立方
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::NetherPortal) continue; // t725 余烬门渲染走 portalHost QML delegate（portal_strip 翻书不进图集）；立方面路径会把门格画成 tile 0 草顶立方
                            if (!isWater && !isLava && !isGlass && !isIceBlk && blk == BlockRegistry::Spawner) continue; // t760 刷怪笼渲染走 spawnerHost QML delegate（BlockCube cutout 笼壳 + 旋转迷你蠹虫）；terrain 立方是 opaque 整壳会完全遮住笼内迷你蠹虫
                            const quint8 nb = snap.blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (occludesNeighborFace(nb)) continue;        // t746 邻居实体 → 剔除（跨 chunk 路由正确）；叶邻不剔（防叶孔透视 void）
                            if (isWater && nb == BlockRegistry::Water) continue; // 水-水面互剔
                            if (isLava && nb == BlockRegistry::Lava) continue;   // t343 岩浆-岩浆面互剔
                            // t405 玻璃-玻璃面互剔（Glass solid=false → isSolid(nb) 不剔除玻璃邻；显式剔除避免两玻璃共面重复绘制）。
                            if (isGlass && nb == BlockRegistry::Glass) continue;
                            // t468 冰-冰面互剔（冰族 solid=false → isSolid(nb) 不剔除冰邻；显式剔除避免两冰共面重复绘制，同 glass 模式）。
                            if (isIceBlk && BlockRegistry::isIce(nb)) continue;
                            const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                            e.valid = true;
                            // t225 箱子前面朝向由 state 决定（其余方块 state inert）→ mask tile 含 state，
                            //   不同朝向的相邻箱子在前侧面自然不合并（侧/顶/底面仍同 tile 可合并）。
                            const quint8 st = snap.stateAtWorld(wx, ly, wz);
                            e.tile = tileFor(blk, f, st);
                            e.sky = snap.skyLightAt(ax, ay, az);
                            e.block = snap.blockLightAt(ax, ay, az);
                            // t406 耕地湿润等级进合并键：仅 Farmland +Y 顶面带等级（侧/底 + 非耕地 = 0），
                            //   → 不同湿润度的耕地顶面不共面误并（各保留各自顶点色暗化，darker=wetter 清晰可辨）。
                            e.hydr = (blk == BlockRegistry::Farmland && f == int(BlockRegistry::Top))
                                     ? quint8(st & BlockRegistry::FarmlandHydrationMask) : quint8(0);
                        }
                    }
                    // 贪婪合并 (a,b) 平面 → 矩形（先沿 axisA 扩宽 w，再沿 axisB 扩高 h）。
                    for (int a = 0; a < sizeA; ++a) {
                        for (int b = 0; b < sizeB; ++b) {
                            const MaskEntry cur = mask[a * sizeB + b]; // 值拷贝（合并键固定；后续清格不影响）
                            if (!cur.valid) continue;
                            const int curTile = cur.tile;
                            const quint8 curSky = cur.sky, curBlock = cur.block;
                            const quint8 curHydr = cur.hydr; // t406 耕地湿润等级（进合并键）
                            auto same = [&](int aa, int bb) {
                                const MaskEntry &o = mask[aa * sizeB + bb];
                                return o.valid && o.tile == curTile && o.sky == curSky
                                       && o.block == curBlock && o.hydr == curHydr;
                            };
                            int w = 1;
                            while (a + w < sizeA && same(a + w, b)) ++w;
                            int h = 1;
                            while (b + h < sizeB) {
                                bool ok = true;
                                for (int k = 0; k < w; ++k)
                                    if (!same(a + k, b + h)) { ok = false; break; }
                                if (!ok) break;
                                ++h;
                            }
                            // 发射矩形 [a,a+w) × [b,b+h) @ 层 n，面 f：4 顶点 + 2 三角。
                            const float u0 = curTile * tileW + hx, u1 = (curTile + 1) * tileW - hx;
                            const float nbSkyF = curSky / 15.0f;
                            const float nbBlockF = curBlock / 15.0f;
                            // t406 耕地 +Y 顶面湿润暗化（curHydr>0 仅耕地顶面；其余面 / 非耕地 = 1.0 不影响）。
                            const float brightMul = farmlandHydrBrightMul(curHydr);
                            const int cuAxis = kUVAxes[f][0], cvAxis = kUVAxes[f][1];
                            const quint32 base = quint32(verts.size());
                            for (int cc = 0; cc < 4; ++cc) {
                                int cl[3];
                                cl[normalAxis] = n + int(F.c[cc][normalAxis]);             // 面贴 n 或 n+1 侧
                                cl[axisA] = a + (F.c[cc][axisA] ? w : 0);                  // in-plane A：起 or 起+宽
                                cl[axisB] = b + (F.c[cc][axisB] ? h : 0);                  // in-plane B：起 or 起+高
                                // per-vertex PCF 软影（世界位 = chunk 原点 + 局部角点；同 t153）。
                                const float shadow = sunShadowAt(float(originX + cl[0]),
                                                                 float(cl[1]),
                                                                 float(originZ + cl[2]));
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow) * dayMul, nbBlockF),
                                                            kVcMin, kVcMax);
                                // UV 在合并 quad 上拉伸铺满（cu/cv 取角点分量 0/1，纹理随几何 span 拉伸）。
                                const float cu = F.c[cc][cuAxis] ? 1.0f : 0.0f;
                                const float cv = F.c[cc][cvAxis] ? 1.0f : 0.0f;
                                Vtx v;
                                v.x = float(cl[0]); v.y = float(cl[1]); v.z = float(cl[2]); // chunk 局部坐标
                                v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                                v.u = u0 + cu * (u1 - u0);
                                v.v = v0 + cv * (v1 - v0);
                                v.r = vc * brightMul; v.g = vc * brightMul; v.b = vc * brightMul; v.a = 1.0f; // t406 brightMul = 耕地湿润暗化（非耕地 = 1.0）
                                verts.append(v);
                            }
                            idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
                            idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
                            // 清已合并格（防后续重叠发射）。
                            for (int da = 0; da < w; ++da)
                                for (int db = 0; db < h; ++db)
                                    mask[(a + da) * sizeB + (b + db)].valid = false;
                        }
                    }
                }
            }
        } else {
            // 逐格 culled meshing（fallback，greedyMeshing=false）：每可见面 4 顶点 + 6 索引（贴图逐格清晰）。
            for (int ly = 0; ly < H; ++ly) {
                for (int lz = 0; lz < S; ++lz) {
                    for (int lx = 0; lx < S; ++lx) {
                        const int wx = originX + lx, wz = originZ + lz;
                        const quint8 b = snap.blockAtWorld(wx, ly, wz);
                        if (b == 0) continue;
                        const bool isWater = (b == BlockRegistry::Water);
                        const bool isLava  = (b == BlockRegistry::Lava);
                        const bool isGlass = (b == BlockRegistry::Glass);
                        const bool isIceBlk = BlockRegistry::isIce(b); // t468 冰族（Ice/PackIce/BlueIce）
                        // t343/t405/t468 段分流：岩浆段只画 Lava；水段只画 Water；玻璃段只画 Glass；冰段只画冰族；
                        //   地形段跳过流体+玻璃+冰族（各自独立段渲染）。
                        if (snap.iceOnly)   { if (!isIceBlk) continue; }
                        else if (snap.glassOnly) { if (!isGlass) continue; }
                        else if (snap.lavaOnly) { if (!isLava) continue; }
                        else if (snap.waterOnly) { if (!isWater) continue; }
                        else { if (isWater || isLava || isGlass || isIceBlk) continue; }
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Torch) continue;
                        if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isPartialBlock(b)) continue; // t412 异形已在 PASS 1（含段外圆石变体）；段后整立方（Chest）正常进立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Farmland) continue; // t408 耕地矮盒已在 PASS 1；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isCrossBillboard(b)) continue; // t235/t305 cross（草丛/作物/树苗）已在 PASS 1；不进立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Cactus) continue; // t445 仙人掌 0.8 细柱已在 PASS 1；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::SnowLayer) continue; // t505/t510 二轮复盘：积雪层薄板已在 PASS 1；不进整立方面（否则满格立方覆盖薄板，雪层显完整方块）
                        if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isBed(b)) continue; // t457 床低 3D 模型已在 PASS 1；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::EnchantingTable) continue; // t620 附魔台 0.75 矮盒已在 PASS 1
                        if (!isWater && !isLava && !isGlass && !isIceBlk && BlockRegistry::isAnvil(b)) continue; // t766 铁砧三盒异形已在 PASS 1；不进整立方面（greedy 同 culled 路径双保险）
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Painting) continue; // t720 画作渲染走 paintingHost QML delegate（贴图不进图集）；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Fire) continue; // t724 火焰渲染走 fireHost QML delegate（fire_strip 翻书不进图集）；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::NetherPortal) continue; // t725 余烬门渲染走 portalHost QML delegate（portal_strip 翻书不进图集）；不进整立方面
                        if (!isWater && !isLava && !isGlass && !isIceBlk && b == BlockRegistry::Spawner) continue; // t760 刷怪笼渲染走 spawnerHost QML delegate（BlockCube cutout 笼壳 + 旋转迷你蠹虫）；不进整立方面
                        for (int f = 0; f < 6; ++f) {
                            const FaceDef &F = kFaces[f];
                            const quint8 nb = snap.blockAtWorld(wx + F.dir[0], ly + F.dir[1], wz + F.dir[2]);
                            if (occludesNeighborFace(nb)) continue; // t746 叶邻不剔（同 greedy 路防叶孔透视 void）
                            if (isWater && nb == BlockRegistry::Water) continue;
                            if (isLava && nb == BlockRegistry::Lava) continue;
                            // t405 玻璃-玻璃面互剔（Glass solid=false → isSolid(nb) 不剔除玻璃邻；显式剔除避免两玻璃共面重复绘制）。
                            if (isGlass && nb == BlockRegistry::Glass) continue;
                            // t468 冰-冰面互剔（冰族 solid=false → isSolid(nb) 不剔除冰邻；显式剔除避免两冰共面重复绘制，同 glass 模式）。
                            if (isIceBlk && BlockRegistry::isIce(nb)) continue;
                            const quint8 st = snap.stateAtWorld(wx, ly, wz); // t225/t406 箱子前面朝向 / 耕地湿润由 state 决定
                            const int t = tileFor(b, f, st); // t225 箱子前面朝向由 state 决定
                            const float u0 = t * tileW + hx, u1 = (t + 1) * tileW - hx;
                            const int ax = wx + F.dir[0], ay = ly + F.dir[1], az = wz + F.dir[2];
                            const float nbSkyF = snap.skyLightAt(ax, ay, az) / 15.0f;
                            const float nbBlockF = snap.blockLightAt(ax, ay, az) / 15.0f;
                            // t406 耕地 +Y 顶面湿润暗化（darker=wetter；仅 Farmland 顶面带等级，其余 = 1.0）。
                            const float brightMul = (b == BlockRegistry::Farmland && f == int(BlockRegistry::Top))
                                ? farmlandHydrBrightMul(quint8(st & BlockRegistry::FarmlandHydrationMask)) : 1.0f;
                            // t1023 AO 面内两轴（±X 面→Y/Z；±Y 面→X/Z；±Z 面→X/Y）——角点侧/对角探针偏移用。
                            const int aoAxisU = (f <= 1) ? 1 : 0;
                            const int aoAxisV = (f <= 1) ? 2 : (f <= 3 ? 2 : 1);
                            const quint32 base = quint32(verts.size());
                            for (int cc = 0; cc < 4; ++cc) {
                                const float dx = F.c[cc][0], dy = F.c[cc][1], dz = F.c[cc][2];
                                const float shadow = sunShadowAt(float(wx) + dx, float(ly) + dy, float(wz) + dz);
                                // PLAN §2-H：dayMul 只乘天光分量（立方面），block 项保留 → 夜间火把/熔炉光照亮的方块面仍全亮。
                                const float vc = std::clamp(std::max(nbSkyF * (1.0f - shadow) * dayMul, nbBlockF),
                                                            kVcMin, kVcMax);
                                // t1023 AO 接触阴影（aoEnabled 默认关 → 因子恒 1.0，三探针零开销旁路）：
                                //   角点实体遮挡 → kAoFactor 曲线乘顶点色。乘在光场钳制**后**（AO 是几何暗角
                                //   非光场分量，允许低过 kVcMin 地板——暗角即压暗语义）。
                                const float vcl = vc * (snap.aoEnabled
                                    ? aoCorner(ax, ay, az, aoAxisU, aoAxisV,
                                               F.c[cc][aoAxisU] ? 1 : -1,
                                               F.c[cc][aoAxisV] ? 1 : -1)
                                    : 1.0f);
                                float cu, cv;
                                if (f == 0 || f == 1) { cu = dz; cv = dy; }       // ±X
                                else if (f == 4 || f == 5) { cu = dx; cv = dy; }  // ±Z
                                else { cu = dx; cv = dz; }                        // ±Y
                                Vtx v;
                                v.x = float(lx) + dx; v.y = float(ly) + dy; v.z = float(lz) + dz; // 局部坐标
                                v.nx = F.nrm[0]; v.ny = F.nrm[1]; v.nz = F.nrm[2];
                                v.u = u0 + cu * (u1 - u0);
                                v.v = v0 + cv * (v1 - v0);
                                v.r = vcl * brightMul; v.g = vcl * brightMul; v.b = vcl * brightMul; v.a = 1.0f; // t151 光场 × t153 PCF 软影顶点色 × t406 耕地湿润暗化（非耕地 brightMul=1.0）× t1023 AO 接触阴影（默认关 = 恒 1.0）
                                verts.append(v);
                            }
                            idx.append(base + 0); idx.append(base + 1); idx.append(base + 2);
                            idx.append(base + 0); idx.append(base + 2); idx.append(base + 3);
                        }
                    }
                }
            }
        }
    }

    // 网格统计自持（t10 F3 叠层口径）：顶点 / 三角面数随 owning 输出一并交付（验收③「统计自持」）。
    mesh.vertexCount = int(verts.size());
    mesh.triangleCount = int(idx.size() / 3);
    return mesh;
}
