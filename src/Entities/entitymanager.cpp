#include "entitymanager.h"
#include "world.h" // tick / resolvePlayerPush / aiWander 只读 World::isSolid/blockAt/width/depth（向下依赖；PLAN §2 Entities→World 合规）
#include "minecartmanager.h" // t811 tickVehicleRiding 读矿车座位/位置（同层互调，Entities 内；无环 — 载具管理器不反指本类）
#include "boatmanager.h"     // t811 tickVehicleRiding 读船座位/位置（同上）
#include "frameprofiler.h" // t500 perf：mob 桶子分解探针（mobLoop/mobAI/mobPhys/mobHostile/mobSpawn）

#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QtMath>    // qFloor, qRadiansToDegrees
#include <algorithm> // std::clamp, std::min, std::move
#include <cmath>     // std::sqrt, std::sin, std::cos
#include <cstring>   // std::memcpy（slotFingerprint 浮点位混合）

namespace {
Q_LOGGING_CATEGORY(lcEnt, "vo.entity") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤，落 log 可见

// t642 作物格判定（WheatCrop / CarrotCrop / PotatoCrop）：cross 形非实体植物（ShapeNone，无碰撞盒）。
//   t865 起 mob 的碰撞 / 支撑 / 越障三谓词已收口 World::isCollidable（枚举豁免退役），本判定仍被两处
//   消费：①耕作踩踏判定（mob 踩过作物格减速 / t642 慢走语义）；②下落沙落作物格穿透变掉落物（沙不落
//   在无碰撞植物上）。纯 id 表查询，与碰撞权威解耦。
bool isCropBlock(int blockId)
{
    return blockId == BlockRegistry::WheatCrop
        || blockId == BlockRegistry::CarrotCrop
        || blockId == BlockRegistry::PotatoCrop;
}

// t642 → t865 越障跳判据「前方脚位是墙」收口（单一权威 = World::isCollidable）：
//   无碰撞格（轨 / 火把 / 草丛 / 花 / 树苗 / 作物 / 火 —— ShapeNone 无碰撞盒族）**不是墙** → mob 直接
//   走过不跳（机制等价 MC 怪跨过草丛 / 花不跳踩；t642 作物豁免与 t803 火焰豁免的同族收口 —— 枚举式
//   豁免漏了草丛/花 = 用户报「僵尸遇草丛跳过去」的根因；改碰撞权威后枚举表退役，新增无碰撞方块零漏）。
//   水保留旧口径（isSolid 恒 true → 照旧当沟壑跳过，t642 明示非彼任务范围的行为不动）；review26 #3：
//   岩浆同列 ditch-jump——两流体皆 ShapeNone 无碰撞，岩浆落 isCollidable 分支恒 false = 不当沟壑 →
//   被动生物 / 跟随狼猫径直走进岩浆殉死（旧版岩浆恒跳；t865 收口时行为翻转且与水的保留口径自相矛盾）。
//   碰撞实体（含半砖 / 楼梯 / 压力板 / 门 / 栅栏等薄形碰撞体）仍按墙跳（per-cell 粒度，旧语义）——
//   例外（review26 #1）：feetY ≥ 0 且前方格碰撞真顶 ≤ 脚位 + 容差 = 与脚下同高的矮支撑（mob 已站其
//   同类真顶上：下半砖地面 / 耕地 / 压力板走廊），非墙不跳（否则 t865 落定链把脚位 snap 进矮支撑格
//   内部后 fy=脚位格恒命中自身同类 → 全程兔跳）。真顶高于脚位（走进矮墙 / 上台阶）仍照旧跳。
//   feetY = 调用点 mob 当前脚位世界 Y（e.pos.y()−e.halfH）；默认 -1 = 不启用高度判（按整格口径）。
bool isJumpObstacle(World *world, int x, int y, int z, float feetY)
{
    if (!world || y < 0) return false;
    const quint8 bid = world->blockAt(x, y, z);
    if (bid == BlockRegistry::Water || bid == BlockRegistry::Lava)
        return true; // 水 / 岩浆当沟壑跳过（t642 口径保留 + review26 #3 岩浆同列）
    if (!world->isCollidable(x, y, z)) return false;
    if (feetY >= 0.0f) {
        const float top = world->supportTopYAt(x, y, z);
        if (top >= 0.0f && top <= feetY + 1e-3f) return false; // 正站其顶的同高矮支撑 → 非墙
    }
    return true;
}

// ── t789 羊自然毛色（机制等价 MC 1.0 自然刷出羊的毛色分布；近似权重表，万分位整数便于单源求和）──
//   下标 = 羊毛 16 色标准序：白 0 / 粉 6 / 灰 7 / 浅灰 8 / 棕 12 / 黑 15。权重 = 白 8184 + 黑 500 +
//   灰 500 + 浅灰 500 + 棕 300 + 粉 16 = 10000（≈ 白 81.8% / 黑·灰·浅灰各 5% / 棕 3% / 粉 0.164%，
//   dev-plan t789 锚点值归一化；MC 原文白 81.836-81.875% 两口径均在此容差内）。**只有这 6 色自然出现**
//   （其余 10 色是染料链 / 创造调色板的事），spawnMobCore 对 MobSheep 按 it 加权随机——一处收口全部
//   生成路径（进世界散布 / 刷怪笼被动 / 生物蛋 / 繁殖幼崽——幼崽随后被 tickBreeding 覆写为父代色）。
struct SheepWoolWeight { int index; int weight; };
constexpr SheepWoolWeight kSheepNaturalWeights[] = {
    { 0, 8184 }, { 15, 500 }, { 7, 500 }, { 8, 500 }, { 12, 300 }, { 6, 16 },
};
constexpr int kSheepNaturalWeightTotal = 10000; // Σweights（表改值须同步）

// t832 抽出「自然权重掷一次羊毛色」（spawnMobCore 生成 + 染色羊长回重掷共用同一权威；逐段扣减同
//   pickPassiveMobType 模式）。返 0..15 自然色下标（兜底白——表合计恰 10000 必命中，防表改漏悬空）。
int rollNaturalSheepWool()
{
    auto *rng = QRandomGenerator::global();
    int r = int(rng->bounded(kSheepNaturalWeightTotal));
    for (const SheepWoolWeight &w : kSheepNaturalWeights) {
        if (r < w.weight) return w.index;
        r -= w.weight;
    }
    return 0;
}

// t789 羊毛 16 色 tint 色板（毛层贴图乘色，白 → #ffffff 恒等不着色）。同源链：tools/build_wool.py
//   WOOL_COLORS（羊毛方块贴图程序生成色板）→ ResourceBrowser.qml woolPalette（t751 图鉴变体预览）→
//   本表（游戏内毛层 tint）三处同值镜像——浏览器预览色 = 游戏内羊观感色。矩阵探针 t789 直调
//   sheepWoolTintForIndex 钉死契约（漂移即 FAIL）。下标序 = 染料/羊毛标准序（白/橙/品红/淡蓝/黄/柠绿/
//   粉/灰/浅灰/青/紫/蓝/棕/绿/红/黑）。
constexpr const char *kSheepWoolTints[16] = {
    "#ffffff", "#de781e", "#b94ba5", "#4696d2", "#d2b428", "#5faf2d",
    "#e191af", "#464650", "#9b9ba0", "#418791", "#823ca5", "#3746a5",
    "#734b2d", "#468237", "#962828", "#1e1e26",
};


// t670 白天寻阴凉（机制等价 MC 亡灵日间主动找树荫/洞口躲避日光）：在世界里找 (sx,sy,sz) 周围半径 kRadius 内
//   最近（XZ 距离取小）的「遮荫可站列」。(x, z) 列候选：某脚位层 y（±1 内）下方实体（站得住）+ 身体格空气
//   （mob 1.8 高占两格）+ 身体格 skyLight < kThresh（遮荫，燃烧判定是 skyLightAt>=15，14 留边缘余量）。
//   返 true 并输出列坐标（bestX/bestZ）。扫描盒子含垂直三/四层，覆盖树叶下 / 屋檐 / 洞口等常见阴凉。
//   无任何遮荫格 → false（caller 保持追踪玩家，白昼照烧，MC 僵尸无遮荫即烧死）。
bool findShadeTarget(World *world, int sx, int sy, int sz, int radius, int kThresh, int *outX, int *outZ)
{
    if (!world) return false;
    int bestX = -1, bestZ = -1, bestD2 = INT_MAX;
    const int h = world->height();
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = sx + dx, z = sz + dz;
            if (x < 0 || z < 0 || x >= world->width() || z >= world->depth()) continue;
            for (int y = sy - 1; y <= sy + 2; ++y) {
                if (y < 1 || y + 2 >= h) continue; // 身体两格 y+1/y+2 需在界内（t690：1.8 高 mob 占两格 ——
                                                     //   旧版只界检 y+1 → y+2 越界读贴边缓存 / 头顶格无校验）
                if (!world->isSolid(x, y, z)) continue;               // 下方支撑（脚位层实体，可站立）
                if (world->blockAt(x, y + 1, z) != BlockRegistry::Air) continue; // 身体格空气（可容身）
                if (world->blockAt(x, y + 2, z) != BlockRegistry::Air) continue; // t690：头部格净空（旧版漏检 ——
                                                     //   1 格高气袋内 mob 楔入天花板卡死；1.8 高须两格净空，同
                                                     //   spawnCellFitsHostile 双格校验口径）
                if (world->skyLightAt(x, y + 1, z) >= kThresh) continue;          // 遮荫（无直射日光）
                const int dd = dx * dx + dz * dz;
                if (bestX < 0 || dd < bestD2) { bestX = x; bestZ = z; bestD2 = dd; }
                break; // 该列有任一可站遮荫层即算候选（同列其它层不更近）
            }
        }
    }
    if (bestX >= 0) { *outX = bestX; *outZ = bestZ; return true; }
    return false;
}

// t642 刷怪 AABB 适配校验（防生成即嵌墙）：敌对 mob（Shambler/Bones/Stalker halfH=0.90，高 1.8）的 AABB
//   [y, y+1.8] 占 y、y+1 两格。仅查目标格 air 不够 —— 头顶格（y+1）为实体（1 格高洞穴气袋 / 地表树冠压顶 /
//   岩架下凹）时 mob 生成即嵌进天花板 = 卡死 + 窒息（用户「晚上僵尸生成卡在方块里」根因）。要求 y、y+1
//   两格均为 air（同刷怪笼候选校验的 here==Air && above==Air 约定）。XZ 半宽 0.30 < 0.5 → 水平 footprint
//   不出本格，无需查邻列。调用点：tickHostileLife 黑暗刷怪；刷怪笼自带等价双格校验无需换。
bool spawnCellFitsHostile(World *world, int x, int y, int z)
{
    if (!world) return false;
    if (y < 1 || y + 1 >= world->height()) return false; // 两格须在界内（防越界 blockAt）
    return world->blockAt(x, y, z) == BlockRegistry::Air
        && world->blockAt(x, y + 1, z) == BlockRegistry::Air;
}

// mob AABB footprint 全格扫（t104；仿 player aabbHitsSolid，playercontroller.cpp:761）。
// 给定实体立方体中心 (cx,cy,cz) 与半径 r，扫其 AABB [cx−r,cx+r]×[cy−r,cy+r]×[cz−r,cz+r]「严格覆盖」
// 的所有格子，任一实体方块 → true。「严格重叠」取样（ceil(max)−1 排除仅贴面的方块 → 防卡缝 / 不误判
// 正下方支撑格）。取代 resolvePlayerPush 旧版「只查 mob 中心格」的单格检查：斜推角落时 mob 中心可能
// 仍在空气格但 3/4 身体已入墙 → 旧版不撤回 → 下帧中心才入墙 → 撤回 → 再下帧又被推入 → 反复跳变 =
// jitter（用户感知为 scale 闪烁 + revision 每帧 bump）。全格扫使「mob AABB 任一部分触墙」即撤回 →
// mob 永不入墙 → 无跳变。
//
// t239 复用于 AI wander 水平碰撞（同 player move-and-resolve 逐轴撤回）：mob 按 yaw 行走时，逐轴
// （X 后 Z）试探新位置 → 任一部分触墙即撤回该轴 → mob 贴墙滑动不穿入。Y 范围用 mob 当前 pos.y
// （resting 后稳定，扫到的是身体高度处的墙，非脚下地面）。
// t252：AABB 的 XZ 用 halfW、Y 用 halfH（cow 0.40×0.50×0.40 等非立方 footprint；旧版单一 r 致 cow
//   垂直范围同 XZ，碰撞感失真）。
bool mobAabbHitsSolid(World *world, float cx, float cy, float cz, float halfW, float halfH)
{
    if (!world) return false;
    const float minx = cx - halfW, maxx = cx + halfW;
    const float miny = cy - halfH, maxy = cy + halfH;
    const float minz = cz - halfW, maxz = cz + halfW;
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int y0 = int(std::floor(miny)), y1 = int(std::ceil(maxy)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const quint8 bid = world->blockAt(x, y, z);
                // t629 薄雪层视穿透（review D1-a 收紧）：SnowLayer 是贴 cell 底的 1/8..1.0 薄板（snowLayerHeight
                //   单一权威），全格 isSolid 把它当整墙 → mob 无法走进任何含雪层格（雪原 worldgen 雪层会把 mob 围死原地；
                //   雪傀儡被自己铺的脚印围死）。机制等价 MC mob 跨步薄雪层（玩家侧本就有 auto-step 跨 1/8）→
                //   水平碰撞豁免；垂直仍由下方落地扫描按层真实高度承接（mobSupportTopY 配套，两处成对）。
                //   **仅豁免薄层（≤0.5）**：8 级中的高 4 级（5/8..8/8）视觉与碰撞上已是矮墙 / 满格 —— MC 1.0 中
                //   厚雪层（≥1/2）对实体是真实障碍（mob 跳不上 / 不穿）。旧无条件豁免让 8/8 满格雪层（塌落叠层
                //   setSnowLayerMerge 可叠出 state=7）被 mob 直线穿墙。垂直落地扫描不受影响（mobSupportTopY
                //   按层真顶承接，非豁免路径）。雪层有碰撞 → 不在下方的无碰撞豁免里，须显式薄层分支。
                if (bid == BlockRegistry::SnowLayer
                    && BlockRegistry::snowLayerHeight(world->stateAt(x, y, z)) <= 0.5f)
                    continue;
                // t865 无碰撞格整体视穿透（单一权威 = World::isCollidable，与 mobSupportTopY 落地承接 /
                //   玩家 collisionAABBsAt 同源）：轨 / 火把 / 草丛 / 花 / 树苗 / 作物 / 火 / 水（ShapeNone 无
                //   碰撞盒族）不再挡 mob 横向移动 —— 旧版 isSolid（非 air 实存）把它们当整墙，枚举豁免表
                //   （t642 作物 / t803 火 / t333 水）漏了轨与火把（mob 被轨列挡住 → 越障跳翻上轨 → 悬浮轨上
                //   一格的另一路径）。含掉落沙 / 击退 / 流水推动等所有 mobAabbHitsSolid 消费路径。碰撞实体
                //   （半砖 / 门 / 活板门 / 压力板等）当墙挡的语义见下方脚位格豁免（review26 #1）。
                if (!world->isCollidable(x, y, z)) continue;
                // review26 #1（t865 半改态收尾）：脚位格薄支撑豁免。t865 落定链把 feet(miny) snap 到矮
                //   碰撞支撑**真顶**（下半砖 +0.5 / 耕地 +0.9375 / 压力板 1/16 / 附魔台 0.75 / 合活板门
                //   0.1875 …）后脚位落进支撑格内部 → y0=floor(feet)=支撑格自身 → 该格 isCollidable 恒
                //   true → 任何水平试探恒命中 → 逐轴撤回 → mob 原地冻结（wander/chase 全灭；t865 只给
                //   SnowLayer≤0.5 留豁免，其余矮碰撞体全中招——本条把雪层豁免推广为通用「正站其顶」豁免）。
                //   判据：y==y0（仅脚位格——上方身体格的真顶恒 > miny，天然不满足）且碰撞真顶
                //   （supportTopYAt 单一权威）≤ 脚位 + 1e-3 容差（盒顶不高于脚底 = AABB 无严格重叠，与
                //   XZ 侧 ceil(max)-1 贴面排除同原则）→ 视穿透。走向矮墙时 feet−y=0 < 盒高 → 真顶 > miny
                //   → 不豁免，「碰撞实体仍当墙挡」旧语义不变（只豁免已站上去的）。
                if (y == y0) {
                    const float top = world->supportTopYAt(x, y, z);
                    if (top >= 0.0f && top <= miny + 1e-3f) continue;
                }
                return true;
            }
    return false;
}

// t298 mob 脚位（AABB 底面所在格）是否为水 —— 同玩家 PlayerController::feetInWater 语义（机制等价 MC
//   「脚在水里即受水中物理」）。feetCellY = floor(cy − halfH)（mob 底面格；pos.y 是中心，底面 = 中心−halfH）。
//   只读 World::blockAt；越界（fy<0）/ 无世界 → false。用于 tick 内判「mob 在水中」→ 减速 + 浮力 + 流水推动。
bool mobFeetInWater(World *world, float cx, float cy, float cz, float halfH)
{
    if (!world) return false;
    const int fy = int(std::floor(cy - halfH));
    if (fy < 0) return false;
    return world->blockAt(int(std::floor(cx)), fy, int(std::floor(cz))) == BlockRegistry::Water;
}

// t979 mob 口鼻线淹没判定（单一权威：t828 溺水 + t923 浮面「会淹才游」两处共用本谓词，旧式为两处
//   各写的同式整格布尔）。水是分档液面（state 0=水源满格 1.0 / 流水 (8-state)/8 逐级降，见
//   BlockRegistry::Water 的 t174/t197 state 编码——1/8 档=state 7、1/4 档=state 6）；旧整格
//   blockAt==Water 布尔判把「脚踝水」误当「没顶」：矮 mob 口鼻线 pos.y+halfH·0.8 抬不到 1 格
//   （猪 halfH=0.45 → 口鼻线在脚位上方 0.81），站在 1/8 水洼里口鼻线仍落在脚位水格 → 呼吸耗尽
//   溺亡（用户实测「脚踝深水、猪一直往上跳还是淹死」根因；「往上跳」= 同一误判驱动的 t923 浮力
//   在浅水空跳，不脱困）。本谓词改**口鼻线 vs 格内实际水面高度**连续比较：口鼻所在格非水 → 口鼻
//   在空气中（不算淹没）；是水 → 格内液面（格底 + 满格 1.0 / (8-state)/8）≥ 口鼻线才算淹没。
//   满格水源液面=格顶 → 与旧整格判在源块水完全等价（既有 t828/t923 探针行为不变）；仅浅水流水档
//   修正误判。玩家侧核对（t202 eyeInWater 同为整格判）：玩家眼高 1.62 > 1 格，站立时眼位格恒在
//   脚位格上一格，1/8、1/4 水洼结构性不可达眼位格 → 不同病，不顺手修（差异登记）。分层（PLAN §2）：
//   纯只读 World::blockAt + stateAt，无写。t980 鱿鱼离水判据与本谓词同族（线 vs 水面的反向锚）。
bool mobSnoutSubmergedInWater(World *world, float cx, float cy, float cz, float halfH)
{
    if (!world) return false;
    const float mouthY = cy + halfH * 0.8f; // 口鼻线（旧 t828/t923 头位采样式原样保留——采样权威不变）
    const int my = int(std::floor(mouthY));
    if (my < 0) return false;
    const int mx = int(std::floor(cx));
    const int mz = int(std::floor(cz));
    if (world->blockAt(mx, my, mz) != BlockRegistry::Water) return false; // 口鼻格非水 → 呼吸自由
    const quint8 st = world->stateAt(mx, my, mz);                          // 越界返 0 → 下方按满格算（保守没顶）
    const float surfaceY = float(my) + (st == 0 ? 1.0f : float(8 - st) / 8.0f); // 水源满格 / 流水降档
    return surfaceY >= mouthY; // 液面没过口鼻线才算淹没（浅水档液面低于口鼻线 → 负）
}

// t980 鱿鱼离水搁浅判定（t979 mobSnoutSubmergedInWater 的**同族反向**谓词——同一「采样线 vs 格内
//   液面」比较式，一对两向用：空气呼吸者锚口鼻线（线在液面下 = 淹 → 溺水计时），水生者锚体底线
//   （线在液面上 = 离水 → 搁浅掉血）；不造第二套平行判据）。体底线 = pos.y−halfH（AABB 底面，同
//   mobFeetInWater 脚位线）：体底格非水 → 身体最低点已在空气中 → 搁浅；是水 → 格内液面低于体底线
//   （浅水档托不住悬空的体底）→ 同样搁浅。与 mobFeetInWater 的分工：后者是**运动语义**（脚格沾水
//   即水中物理/减速，t298/t399 既有口径，1/8 浅水档也照走水中物理），本谓词是**生存语义**（身体
//   最低点离水才算「离水搁浅」——鱿鱼半浸在水洼里不干渴，机制等价 MC 1.0 squid 触水即存活的宽口径）。
//   玩家侧无对应（玩家不因离水掉血）。无世界 → false（与溺水侧同保守向：宁可漏判不可误伤）。分层
//   （PLAN §2）：纯只读 World::blockAt + stateAt。
bool mobBodyAboveWaterSurface(World *world, float cx, float cy, float cz, float halfH)
{
    if (!world) return false;
    const float bellyY = cy - halfH; // 体底线（AABB 底面）
    const int by = int(std::floor(bellyY));
    if (by < 0) return false; // 世界底之下查无水格（blockAt 越界返 Air）→ 不误判搁浅（保守向）
    const int bx = int(std::floor(cx));
    const int bz = int(std::floor(cz));
    if (world->blockAt(bx, by, bz) != BlockRegistry::Water) return true; // 体底格非水 → 离水
    const quint8 st = world->stateAt(bx, by, bz);
    const float surfaceY = float(by) + (st == 0 ? 1.0f : float(8 - st) / 8.0f);
    return surfaceY < bellyY; // 液面低于体底线（如 1/8 档托不住悬空体底）→ 仍算离水
}

// t980 鱿鱼搁浅常量（本文件局部——仅 aiSquid 搁浅挣扎分支 + tick 鱿鱼窒息块消费，数值口径随两处
//   注释走，不进头文件公共契约面）：
//   - kSquidFlopInterval=1.6s 挣扎周期（每周期随机换挣扎向；前段蠕动后段摊歇）。
//   - kSquidStruggleBurst=0.6s 蠕动窗（周期前段；后 1.0s 摊歇——间歇感 = 非 kWalkSpeed 连续步态）。
//   - kSquidStruggleSpeed=0.5 blocks/s 蠕动速（= kWalkSpeed 一半：陆上挣扎缓动，慢于水中漂游 0.8）。
//   - kSquidStrandGraceSeconds=1.0s 离水宽限（防推挤跨水面 / 翻身瞬间的单帧误检尖峰；之后每
//     kMobDrownInterval(1s) 扣 1HP 至死——10HP 鱿鱼离水 ~11s 死，节奏与 mob 溺水 1HP/s 同族）。
static constexpr float kSquidFlopInterval       = 1.6f;
static constexpr float kSquidStruggleBurst      = 0.6f;
static constexpr float kSquidStruggleSpeed      = 0.5f;
static constexpr float kSquidStrandGraceSeconds = 1.0f;

// t362 mob 落地支撑复探：footprint XZ 任一列在支撑层 supportY 有可站立支撑 → true。
//   取样同 mobAabbHitsSolid（floor(min)..ceil(max)-1，严格覆盖排除仅贴面列）。只读 World。
//   用于替代旧版「仅中心列」支撑复探 —— 见 tick 内 resting 复探注释（修「mob 下 1 格台阶卡死」根因）。
//   t865：支撑语义收口 World::isCollidable（有碰撞 sub-AABB = 可站立，与 mobSupportTopY 落地承接 /
//   玩家 collisionAABBsAt 同源）—— 旧版 isSolid（非 air）把轨 / 火把 / 花草 / 作物 / 火都当「有支撑」，
//   与 mobSupportTopY 的穿透（-1）不一致 → 停在无碰撞格上方的复探保 resting 悬空。水 / 火 / 无碰撞
//   格不再算支撑（t333/t803 旧显式豁免由碰撞权威统一覆盖）；雪层 / 薄板 / 半砖有碰撞 → 照常支撑。
bool mobFootprintHasSupport(World *world, float cx, float cz, int supportY, float halfW)
{
    if (!world || supportY < 0) return false; // 无世界 / 脚位已在 y=0 之下（无支撑层可查）→ 无支撑
    const int x0 = int(std::floor(cx - halfW));
    const int x1 = int(std::ceil(cx + halfW)) - 1;
    const int z0 = int(std::floor(cz - halfW));
    const int z1 = int(std::ceil(cz + halfW)) - 1;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            if (world->isCollidable(x, supportY, z))
                return true;
    return false;
}
// t629 列支撑顶面高度（世界 Y；无效支撑返回 -1）：t865 起收口 World::supportTopYAt（碰撞 sub-AABB 真顶
//   单一权威 —— 整立方 cell+1 / SnowLayer 按 state 真顶 / 下半砖 +0.5 / 压力板 +1/16 / 无碰撞族（轨 /
//   火把 / 花草 / 作物 / 火 / 水）-1 穿透到下方真支撑）。供 mob 落地扫描按真实层高贴面 —— 修「落在 1/8
//   薄雪层上被整格顶起悬空一格格」（t629）与「落在轨列上被满格顶起悬浮一格」（t865，同一病根族：
//   支撑判定把非整格当满格）。与玩家侧 collisionAABBsAt sub-AABB 精度对齐（t575 模式的 mob 侧落地版）。
float mobSupportTopY(World *world, int x, int y, int z)
{
    return world ? world->supportTopYAt(x, y, z) : -1.0f;
}
} // namespace

EntityManager::EntityManager(QObject *parent) : QObject(parent)
{
    m_clock.start(); // 任务（弓箭 60s despawn）：墙钟计时器（arrowSpawnMs / tick 硬上限用）
}

// t935 perf 槽位可见态指纹（= Main.qml mobHost delegate 绑定读到的 At() 访问器底层字段集的散列）。
//   差分语义：指纹不变 ⇔ 该槽全部可见字段值不变 ⇔ delegate 绑定重求值只会读回相同值 → 跳过（零 QML
//   绑定求值成本）。**字段清单契约**：Main.qml delegate 每读一个新的 entityManager.xxxAt(index) 访问器，
//   其底层字段必须加入本清单，否则该字段变化不 bump → delegate 停更（视觉冻结直到下次别的字段变化）。
//   逐字段 ↔ 访问器对照（2026-08 盘点，新增访问器同步改两侧）：
//     alive→aliveAt；pos→posAt；halfW/halfH→radiusAt/halfHeightAt；pushable→pushableAt；kind→kindAt；
//     blockId/blockState→blockIdAt/blockStateAt；primed/fuse→isPrimedAt/fuseProgressAt；color→colorAt；
//     vx/vy/vz→arrowYawAt/arrowPitchAt（箭朝向；mob 击退衰减期顺带 bump 无害）；
//     mobType/maxHealth/health/dead/hurtFlash→mobTypeAt/maxHealthAt/healthAt/deadAt/hurtFlashAt；
//     burning/fireTimer→isBurningAt；slowTimer→isSlowedAt；yawRad→yawAt；walkPhase→walkPhaseAt；
//     eatTimer→headPitchAt（羊吃草低头包络）；sheared/sheepWool→shearedAt/sheepWoolTintAt；
//     snowGolemSheared→snowGolemShearedAt；loveTimer/tameHeartTimer→inLoveAt；baby→babyScaleAt；
//     wolfTamed/wolfSitting→wolfTamedAt/wolfSittingAt；ocelotTamed/ocelotSitting/ocelotVariant→…At；
//     fuseTimer→inflateAt（潜行者蓄力膨胀）；aimTimer→drawAmountAt（骸骨拉弓）；
//     enraged/rageTimer→enragedAt/nightwalkerRageProgressAt；golemWindup→golemAttackPoseAt；
//     enderEyeShatter→shatteringAt；armorHelmet/Chest/Legs/Boots→mobArmorAt。
//   **刻意排除**的字段（每 tick 恒变但零视觉 → 入指纹会让静置 mob 每 emit 空转 bump，正是本修复要消灭的
//   浪费）：AI/节流计时器（wanderTimer/ambientTimer/aiAccum/hostileAccum/stepAccum/losCacheTimer/
//   shadeRescanTimer/teleportCooldown…）、经济计时器（eggTimer/swimTimer/growTimer/regrowCooldown/
//   breedCooldown）、内部快照（spawnSerial/deathBurned/deathBaby/deathSheared/deathTimer）、
//   arrowLife/enderEyeDistLeft/bobber 族（寿命/驱动计时，呈现走 player.fishing 镜像非本 delegate）。
//   其中「值翻转才可见」的计时器（growTimer→baby、regrowCooldown→sheared…）以对应布尔/值字段入指纹，
//   翻转帧必 bump，语义无损。
//   碰撞概率口径：64 位 FNV-1a + boost 式混合，47 槽 × ~20 emit/s 连玩一年 ≈ 3e9 次比对，生日碰撞
//   ~2e-10；即便命中，失败模式 = 单槽错过一帧中间态（下次真变化即恢复），非崩溃/永久冻结，可接受。
quint64 EntityManager::slotFingerprint(size_t i) const
{
    const Entity &e = m_entities[i];
    auto mix = [](quint64 &h, quint64 v) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    auto mixB = [&mix](quint64 &h, bool b) { mix(h, b ? 1 : 0); };
    auto mixI = [&mix](quint64 &h, int v) { mix(h, quint64(quint32(v))); };
    // 浮点按位混合（位级稳定 ≠ 数值相等；呈现层只消费数值，位同 ⇒ 值同，方向安全）。
    auto mixF = [&mix](quint64 &h, float f) { quint32 b = 0; std::memcpy(&b, &f, sizeof(b)); mix(h, b); };
    quint64 h = 1469598103934665603ULL; // FNV offset basis
    mixB(h, e.alive);
    mixF(h, e.pos.x()); mixF(h, e.pos.y()); mixF(h, e.pos.z());
    mixF(h, e.halfW); mixF(h, e.halfH); mixB(h, e.pushable);
    mixI(h, e.kind); mixI(h, e.blockId); mixI(h, e.blockState);
    mixB(h, e.primed); mixF(h, e.fuse);
    mix(h, quint64(qHash(e.color))); // QString → 64 位域
    mixF(h, e.vx); mixF(h, e.vy); mixF(h, e.vz);
    mixI(h, e.mobType); mixI(h, e.maxHealth); mixI(h, e.health);
    mixB(h, e.dead); mixF(h, e.hurtFlash);
    mixB(h, e.burning); mixF(h, e.fireTimer); mixF(h, e.slowTimer);
    mixF(h, e.yawRad); mixF(h, e.walkPhase); mixF(h, e.eatTimer);
    mixB(h, e.sheared); mixI(h, e.sheepWool); mixB(h, e.snowGolemSheared);
    mixF(h, e.loveTimer); mixF(h, e.tameHeartTimer); mixB(h, e.baby);
    mixB(h, e.wolfTamed); mixB(h, e.wolfSitting);
    mixB(h, e.ocelotTamed); mixB(h, e.ocelotSitting); mixI(h, e.ocelotVariant);
    mixF(h, e.fuseTimer); mixF(h, e.aimTimer);
    mixB(h, e.enraged); mixF(h, e.rageTimer); mixF(h, e.golemWindup); mixF(h, e.enderEyeShatter);
    mixI(h, e.armorHelmet); mixI(h, e.armorChest); mixI(h, e.armorLegs); mixI(h, e.armorBoots);
    return h;
}

// t935 槽位监视器惰性入口（QML delegate 建立时取一次）。新槽首次询问即建；已建的恒返回同一实例
//   （槽复用换任不换 monitor —— delegate 的绑定依赖挂在该实例 revision 上，换实例 = 依赖重挂丢一拍）。
QObject *EntityManager::slotMonitorAt(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return nullptr;
    if (int(m_slotMonitors.size()) < int(m_entities.size()))
        m_slotMonitors.resize(m_entities.size(), nullptr); // 只增不减（同 count 单调；delegate 引用稳定）
    if (!m_slotMonitors[size_t(i)])
        m_slotMonitors[size_t(i)] = new EntitySlotMonitor(this); // 父对象托管；GUI 线程专用
    return m_slotMonitors[size_t(i)];
}

// t935 统一 notify 漏斗（头文件声明处注释）。全部原「++m_revision; emit entitiesChanged();」对收口到此。
void EntityManager::notifyEntitiesChanged()
{
    ++m_revision;
    refreshSlotMonitors();
    emit entitiesChanged();
}

// t935 指纹差分 → 按槽 bump。每次 notify 全槽扫一遍（47 槽 × ~45 字段混合 ≈ 微秒级，对照被消掉的
//   N×50 QML 绑定重求值可忽略）。计数三键（F3 mob 行量化面）：mobEmitN = emit 次数；mobBumpN = 实际
//   bump 槽数（= 真被刷新的 delegate 数）；mobFanN = 旧口径全扇出槽数和（count × emit，修复前实付成本
//   的口径）—— bump << fan 即收口生效（静置 / 空槽 / 跨世界高水位槽全部落在差里）。
void EntityManager::refreshSlotMonitors()
{
    const int n = int(m_entities.size());
    if (int(m_slotFp.size()) < n) {
        m_slotFp.resize(n, ~quint64(0)); // 哨兵必 ≠ 首算指纹 → 新槽首 notify 必 bump（delegate 首刷不丢）
        m_slotMonitors.resize(n, nullptr);
    }
    int bumped = 0;
    for (int i = 0; i < n; ++i) {
        const quint64 fp = slotFingerprint(size_t(i));
        if (fp == m_slotFp[size_t(i)]) continue; // 可见态未变 → 不 bump → 本槽 delegate 绑定零重求值
        m_slotFp[size_t(i)] = fp;
        ++bumped;
        if (EntitySlotMonitor *m = m_slotMonitors[size_t(i)])
            m->bump(); // nullptr = 该槽尚无 delegate（monitor 未被索取）→ 无观众无需 bump
    }
    FrameProfiler::instance()->count("mobEmitN");
    FrameProfiler::instance()->addCount("mobBumpN", bumped);
    FrameProfiler::instance()->addCount("mobFanN", n);
}

// 生成默认测试生物：委托 spawnMobTyped（mobType=0、#ff5555、满血）。t239 调试入口（M 键）。
void EntityManager::spawnMob(int x, int y, int z)
{
    spawnMobTyped(x, y, z, 0, QStringLiteral("#ff5555"), kDefaultMaxHealth);
}

// t239 生物基类统一生成入口：满血 + 未死 + AI 初值（wanderTimer=0 → tick 首帧即选第一次向）。
//   达 kCap 跳过 + 告警（防溢出，spec「实体数量有上限」）。bump revision → QML Repeater 追加 delegate。
//   t400：实体构造 + 入槽抽到 spawnMobCore（便于繁殖产幼崽复用）；本入口仅包一层 acquire 后立即 emit。
int EntityManager::spawnMobTyped(int x, int y, int z, int mobType, const QString &color, int maxHealth)
{
    const int slot = spawnMobCore(x, y, z, mobType, color, maxHealth);
    if (slot < 0) return -1; // 达 kCap（spawnMobCore 内已告警）
    notifyEntitiesChanged();
    qCInfo(lcEnt) << "spawned mob type" << mobType << "at" << x << y << z
                  << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
    return slot;
}

// t529 spawnMobTyped 的「带生成时朝向」变体（见头文件注释）。
int EntityManager::spawnMobTypedYaw(int x, int y, int z, int mobType, const QString &color, int maxHealth, float yawRad)
{
    const int slot = spawnMobCore(x, y, z, mobType, color, maxHealth);
    if (slot < 0) return -1; // 达 kCap（spawnMobCore 内已告警）
    m_entities[size_t(slot)].yawRad = yawRad; // 生成时固定朝向（emit 前设，QML 首帧即读到正确 yaw）
    notifyEntitiesChanged();
    qCInfo(lcEnt) << "spawned mob type" << mobType << "at" << x << y << z
                  << "yaw" << yawRad << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
    return slot;
}

// t239 生物基类统一生成核心（spawnMobTyped 实现主体；t400 抽出便于繁殖产幼崽复用）：构造 Entity（碰撞箱 /
//   pos / 血量 / AI 初值 / 护甲随机）+ acquireSlot，返槽索引（达 kCap → -1 + 告警）。不 bump revision / 不 emit
//   （caller 决定 emit 时机：spawnMobTyped 立即 emit；tickBreeding 批量产幼崽后统一一次 emit，避免高频扇出
//   notify 风暴，同 t320/t354 批量收口纪律）。

// t1025 碰撞盒单一权威（原 spawnMobCore 内联 switch 本体抽出；spawnMobCore 生成 + tickBreeding 幼崽长大
//   还原两处共用）。按显式传入的 mobType 设 halfW/halfH + hostile 标志（**勿改读 e.mobType**——spawnMobCore
//   调用点 e.mobType 尚未赋值，首跑实锤全 mob 落默认盒/hostile=0 的级联红）。t252/t293 收紧贴合 MobModel
//   身体（旧值「大一圈」：被动 0.9 宽 vs 躯干 0.6~0.7、敌对 0.9 宽 vs MC 0.6 / 身体 0.4~0.8、牛 1.4 高 vs
//   身体 0.87）。现按「MobModel 实际身体半宽 + 小余量」取值：敌对 halfW=0.30（机制等价 MC 1.0 僵尸 / 骷髅 /
//   苦力怕 0.6 宽——手臂略超盒属 MC 风格，hitbox 只包躯干核心）；被动 halfW=0.40（躯干 0.6~0.7 + 余量，头 /
//   长身可能略超 Z 盒，同 MC 四足 hitbox 不含吻部）；牛 halfH 0.70→0.50（身体 0.87 高，旧 1.4 偏大）；其余
//   halfH 不变（已贴合：人形 1.8 = MC 玩家身高、spider 0.6）。标识符 / 模型全原创（§9 区隔，不照搬 MC 美术）。
//   hostile 标志据 mobType 设（spawnHostileMob 入口已设，spawnMobTyped 兜底也判一次——重赋幂等，长大还原
//   路径复用零行为差）。
void EntityManager::applyMobCollisionBox(int mobType, Entity &e)
{
    switch (mobType) {
        case MobPig:      e.halfW = 0.40f; e.halfH = 0.45f; break; // 0.8×0.9（躯干 0.7 宽 + 余量；旧 0.9 偏大）
        case MobCow:      e.halfW = 0.40f; e.halfH = 0.50f; break; // 0.8×1.0（躯干 0.64 宽 / 身体 0.87 高；旧 0.9×1.4 偏大一圈）
        case MobSheep:    e.halfW = 0.40f; e.halfH = 0.45f; break; // 0.8×0.9（躯干 0.6 宽 + 余量；旧 0.9 偏大）
        case MobShambler: e.halfW = 0.30f; e.halfH = 0.90f; e.hostile = true; break; // 0.6×1.8（机制等价 MC 僵尸 0.6 宽；旧 0.9 偏大）
        case MobBones:    e.halfW = 0.30f; e.halfH = 0.90f; e.hostile = true; break; // 0.6×1.8（机制等价 MC 骷髅 0.6 宽；旧 0.9 偏大）
        case MobStalker:  e.halfW = 0.30f; e.halfH = 0.90f; e.hostile = true; break; // 0.6×1.8（机制等价 MC 苦力怕 0.6 宽；旧 1.1 偏大一圈；t284）
        case MobSpider:   e.halfW = 0.45f; e.halfH = 0.30f; e.hostile = true; break; // 0.9×0.6 宽矮（躯干 0.8 宽 + 余量；旧 1.1 偏大；快速，t285）
        case MobChicken:  e.halfW = 0.30f; e.halfH = 0.40f; break; // 0.6×0.8 小型鸟（躯干 0.4 宽 / 站立 0.7 高；t398）
        case MobSquid:    e.halfW = 0.40f; e.halfH = 0.45f; break; // 0.8×0.9 水生软体（机制等价 MC 1.0 squid 0.8 宽；t399）
        case MobWolf:     e.halfW = 0.35f; e.halfH = 0.45f; break; // 0.7×0.9 犬科（细长躯干 0.36 宽 + 余量；略小于猪；t480）
        case MobOcelot:   e.halfW = 0.30f; e.halfH = 0.35f; break; // 0.6×0.7 猫科（细长躯干 + 长尾，紧凑小体型；t481）
        // t482/t483 防御造物（机制等价 MC 1.0 雪傀儡 / 铁傀儡；neutral non-hostile —— 不参与黑暗刷怪 / 日光燃烧 /
        //   远距消失，生命周期同 passive）。碰撞箱按「南瓜头 + 方块身」整体量：雪傀儡 ~0.7 宽 × 2 雪块+头 3 格高
        //   （取 halfW=0.35 / halfH=0.90，贴合「柱身 + 头」）；铁傀儡 ~1.2 宽 × 2 铁块+头 3 格高（取 halfW=0.60 /
        //   halfH=1.20，重装体型）。hostile 默认 false（造物不攻击玩家）。
        case MobSnowGolem: e.halfW = 0.35f; e.halfH = 0.90f; break; // 0.7×1.8 雪傀儡（柱身 2 雪块 + 南瓜头；t482）
        case MobIronGolem: e.halfW = 0.60f; e.halfH = 1.20f; break; // 1.2×2.4 铁傀儡（T 形铁块 + 南瓜头；t483）
        case MobSilverfish: e.halfW = 0.22f; e.halfH = 0.15f; e.hostile = true; break; // 0.44×0.30 小型虫（机制等价 MC 1.0 银鱼 0.43×0.18 宽矮；要塞刷怪笼刷出，t487）
        case MobNightwalker: e.halfW = 0.35f; e.halfH = 1.40f; e.hostile = true; break; // 0.7×2.9 三格高细长人形（机制等价 MC 1.0 末影人 3 格高；怕水/瞪视激怒/弹射免疫/近战传送，t727）
        case MobEmberling: e.halfW = 0.50f; e.halfH = 0.60f; e.hostile = true; break; // 1.0×1.2 悬浮单头（机制等价 MC 1.0 烈焰人；浮空漂移 + 远程火球 + 火免疫，t728）
        // t952 小蹒跚者（MobBabyShambler）：0.5×0.9 幼体盒（<1 格高口径 halfH=0.45<0.5；机制等价 MC 幼体
        //   僵尸 0.3×0.95 的量级收紧——本工程盒沿用 0.5 宽步进；移速 / 伤害倍率在 aiHostile 分支读常量）。
        case MobBabyShambler: e.halfW = 0.25f; e.halfH = 0.45f; e.hostile = true; break;
        // t1012③ 洞穴蜘蛛（MobCaveSpider）：蜘蛛同族 **0.7× 小体型**盒（机制等价 MC 1.0 cave spider
        //   0.7×0.7×0.7 缩比——spider 0.9×0.6 × 0.7 → 0.64 宽（halfW 0.32）/ 0.42 高（halfH 0.21）；
        //   halfH 0.21 恰 = MobModel 蜘蛛腿底 0.30 × 0.7 → QML mobModelYOff=0 腿底贴地零偏移）。
        case MobCaveSpider: e.halfW = 0.32f; e.halfH = 0.21f; e.hostile = true; break;
        default:          e.halfW = 0.50f; e.halfH = 0.50f; break; // MobTest / 通用：1×1×1（UnitCube 精确贴合，保 t95 旧路径）
    }
}

int EntityManager::spawnMobCore(int x, int y, int z, int mobType, const QString &color, int maxHealth)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); spawnMobCore skipped at" << x << y << z;
        return -1;
    }
    Entity e;
    applyMobCollisionBox(mobType, e); // t1025 碰撞盒单一权威（原内联 switch 本体抽出；显式传参——e.mobType 此处尚未赋值）
    // pos.y 用 halfH（非旧版固定 +0.5）：spawn 在空气格 y 上方贴地（resting 高度 = y + halfH）→
    //   免首帧 collision 底面嵌入地面再 snap（cow halfH=0.70 时旧 +0.5 会嵌 0.2 进支撑方块）。
    //   t728 燃烬者（Emberling）：+kEmberlingHoverOffset 抬升 → spawn 即悬空 ~0.4 格（机制等价 MC 烈焰人飞浮）。
    e.pos = QVector3D(x + 0.5f, y + e.halfH + (mobType == MobEmberling ? kEmberlingHoverOffset : 0.0f), z + 0.5f);
    e.pushable = true;
    e.kind = Mob;
    e.color = color.isEmpty() ? QStringLiteral("#ff5555") : color;
    e.mobType = mobType;
    e.maxHealth = maxHealth > 0 ? maxHealth : kDefaultMaxHealth;
    e.health = e.maxHealth;
    e.dead = false;
    e.hurtFlash = 0.0f;
    e.deathTimer = 0.0f;
    e.deathBurned = false;
    e.deathSheared = false; // review #32：槽复用防残留（同 deathBurned/deathBaby 防御重置）
    e.yawRad = 0.0f;
    e.wanderTimer = 0.0f; // 0 → tick 首帧选第一次向（避免所有 mob 同步起步）
    e.wanderSpeed = 0.0f;
    e.moveSpeed = 0.0f;
    // t241 行走 / 吃草态初值：相位 0；未吃草；eatCooldown=0 → 羊首次 idle 即可扫描草丛（无需等待）。
    e.walkPhase = 0.0f;
    e.eatTimer = 0.0f;
    e.eatApplied = false;
    e.eatCooldown = 0.0f;
    // t250 环境音初值：半步累加 0；idle 叫声倒计时随机化（[kAmbientMin,kAmbientMax)）防批量 spawn 的 mob
    //   首次叫声同步（同 wanderTimer=0 错峰起步同理）。stepAccum=0。
    e.stepAccum = 0.0f;
    e.aimTimer = 0.0f; // t331 骸骨拉弓瞄准计时（slot 复用防残留；仅 MobBones 用）
    // t398 鸡下蛋计时初值：随机化（kEggLayMin..Max）防批量 spawn 的鸡同步下蛋（同 ambientTimer 错峰模式）。
    //   非 MobChicken 的 mob 保留 0 不触发（tick Mob 分支仅 mobType==MobChicken 推进 eggTimer）。
    e.eggTimer = (mobType == MobChicken)
                 ? (kEggLayMin + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kEggLayMax - kEggLayMin))
                 : 0.0f;
    // t399 鱿鱼喷水推进计时初值：随机化（kSquidSwimIntervalMin..Max）防批量 spawn 的鱿鱼同步喷水（同 ambientTimer
    //   错峰模式）。非 MobSquid 的 mob 保留 0 不触发（tick Mob 分支仅 mobType==MobSquid 推进 swimTimer）。
    e.swimTimer = (mobType == MobSquid)
                  ? (kSquidSwimIntervalMin
                     + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kSquidSwimIntervalMax - kSquidSwimIntervalMin))
                  : 0.0f;
    // t728 燃烬者喷火球冷却初值：随机化（[kEmberlingFireIntervalMin,Max)）防批量 spawn 的燃烬者同步喷火（同
    //   ambientTimer 错峰模式）。非 MobEmberling 的 mob 保留 0 不触发（tick Mob 分支仅 mobType==MobEmberling 推进）。
    e.fireCooldown = (mobType == MobEmberling)
                     ? (kEmberlingFireIntervalMin
                        + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kEmberlingFireIntervalMax - kEmberlingFireIntervalMin))
                     : 0.0f;
    e.ambientTimer = kAmbientMin
                     + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kAmbientMax - kAmbientMin);
    // t789 羊自然毛色：spawn 时按 kSheepNaturalWeights 加权随机（白 ~81.8% 主导 + 黑/灰/浅灰 5% + 棕 3% +
    //   粉 0.164%，机制等价 MC 1.0 自然刷羊色分布）。所有生成路径（进世界散布 / 刷怪笼被动 spawnPassiveMob /
    //   生物蛋 / 繁殖幼崽）都经本核心 → 一处收口；幼崽的随机色随后被 tickBreeding 覆写为父代色（继承语义
    //   同 ocelotVariant 先例）。加权随机同 pickPassiveMobType 的「逐段扣减」模式（bounded 返 quint32）。
    if (mobType == MobSheep) {
        e.sheepWool = rollNaturalSheepWool(); // t832 抽出共用（染羊长回重掷同权威）
        e.sheepWoolDyed = false;              // 自然生成非染色（槽复用防残留）
    }
    // t377 mob 随机护甲（仅 Shambler/Bones；spec「~80% no armor, ~20% a random piece/set」）。机制等价 MC 1.0
    //   僵尸/骷髅随机护甲。armorId = 0x300 + tier*4 + piece（与 ArmorRegistry id 段一致；本地常量避免跨层依赖
    //   Game/recipe.h —— Entities 层不向上 include）。tier 0..4（皮革/铁/铜/金/钻石）；piece 0..3（头/胸/腿/靴）。
    //   仅视觉 + spawn 随机（QML delegate 叠 tier 色护甲 Model）；不参与 mob 减伤（spec 仅要求偶遇）。
    e.armorHelmet = e.armorChest = e.armorLegs = e.armorBoots = 0;
    if (mobType == MobShambler || mobType == MobBones || mobType == MobBabyShambler) { // t952 小蹒跚者入随机甲白名单（机制等价 MC 幼体僵尸可带甲生成；delegate 有对应甲壳渲染）
        auto *rng = QRandomGenerator::global();
        if (rng->bounded(100) < 20) {                       // ~20% 有护甲
            constexpr int kArmorBase = 0x300;                // ArmorRegistry::ArmorIdBase（同源常量）
            const int tier = int(rng->bounded(5));           // 0..4 材质档
            const int base = kArmorBase + tier * 4;
            if (rng->bounded(2) == 0) {                      // 整套（4 部位全配）
                e.armorHelmet = base + 0;
                e.armorChest  = base + 1;
                e.armorLegs   = base + 2;
                e.armorBoots  = base + 3;
            } else {                                         // 单件（随机一个部位）
                const int piece = int(rng->bounded(4));
                if (piece == 0)      e.armorHelmet = base + 0;
                else if (piece == 1) e.armorChest  = base + 1;
                else if (piece == 2) e.armorLegs   = base + 2;
                else                 e.armorBoots  = base + 3;
            }
        }
    }
    // t400 繁殖态初值：Entity 默认成员初始化已把 loveTimer/breedCooldown/growTimer=0、baby=false，move 入槽时
    //   覆盖槽位旧值（slot 复用防残留 —— 上一任槽位若曾求偶 / 繁殖 / 是幼崽，复用时清回成体默认）。故新生 mob
    //   恒为成体、未求偶、可繁殖；幼崽态由 tickBreeding 产时单独设 baby=true + growTimer。无需在此显式赋。
    // t952 小鸡骑士：mobType==MobBabyShambler 的生成在 acquireSlot 后掷组合骰（全生成路径单一权威入口，
    //   黑暗刷怪 / 生物蛋 / 刷怪笼均经此）。同样**不 bump / 不 emit**（caller 统一发，组合出的小鸡同帧可见）。
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）；返槽索引
    if (slot >= 0 && mobType == MobBabyShambler) tryFormChickenJockey(slot);
    return slot;
}

// t952 小鸡骑士组合（见 .h 注释）：生成时概率把小蹒跚者与小鸡合并为「小鸡骑士」（小鸡驮小僵尸）。
//   机制等价 MC 幼体僵尸骑小鸡（chicken jockey）生成语义；概率口径 kChickenJockeyChance=5%（运行时可缝写
//   供探针端钉）。组合 = 同格再 spawn 一只 MobChicken（spawnMobCore 直入；槽满静默跳过 → 小僵尸独立生成）
//   + 挂双向链 + 骑手即时钉载具顶。只生成时组合，分离后不再合并（dev-plan 登记取舍）。
void EntityManager::tryFormChickenJockey(int babySlot)
{
    if (babySlot < 0 || babySlot >= int(m_entities.size())) return;
    // 掷骰：>=1 恒组合（探针上端钉，跳掷骰语义同 t950 setEquipmentPickupChance 口径）。
    if (m_chickenJockeyChance < 1.0
        && float(QRandomGenerator::global()->bounded(1000)) / 1000.0f >= float(m_chickenJockeyChance)) {
        return;
    }
    // 读骑手位（值拷贝，不持 Entity& 跨 spawnMobCore —— 其内 acquireSlot 可能 push_back 致引用悬空，
    //   lessons-learned t690/t400 同因先例）。
    const QVector3D babyPos = m_entities[size_t(babySlot)].pos;
    const float babyHalfH = m_entities[size_t(babySlot)].halfH;
    const int cx = qFloor(babyPos.x());
    const int cy = qFloor(babyPos.y() - babyHalfH); // 脚位格（spawnMobCore pos = 格底 + halfH → 反推格 y）
    const int cz = qFloor(babyPos.z());
    // 同格载具小鸡（占位配色同被动鸡路径——鸡走 MobModel + 贴图不读 color）。达 kCap → -1 静默跳过。
    const int chickenSlot = spawnMobCore(cx, cy, cz, MobChicken, QStringLiteral("#f5f0e4"), kDefaultMaxHealth);
    if (chickenSlot < 0 || chickenSlot == babySlot) return; // 槽满 / 自指（防御）：放弃组合，小僵尸独立
    m_entities[size_t(babySlot)].rideMob = chickenSlot;
    m_entities[size_t(chickenSlot)].mobRider = babySlot;
    // 骑手即时钉载具顶（spawnMobTyped 的 emit 在本调用之后 → QML 首帧即见骑士姿态，无穿模首帧）。
    const float mountTop = m_entities[size_t(chickenSlot)].pos.y() + m_entities[size_t(chickenSlot)].halfH;
    m_entities[size_t(babySlot)].pos = QVector3D(m_entities[size_t(chickenSlot)].pos.x(),
                                                 mountTop + m_entities[size_t(babySlot)].halfH,
                                                 m_entities[size_t(chickenSlot)].pos.z());
    m_entities[size_t(babySlot)].resting = true;
    m_entities[size_t(babySlot)].vy = 0.0f;
    qCInfo(lcEnt) << "chicken jockey formed: baby shambler slot" << babySlot
                  << "riding chicken slot" << chickenSlot << "at" << cx << cy << cz;
}

// t117 生成下落方块实体：存格中心 + blockId + pushable=false + kind=FallingBlock。bump revision →
// QML Repeater 追加 delegate（BlockCube 贴图渲染，复用地形图集）。达 kCap 跳过 + 告警（防溢出）。
// 重力 tick 下落，着地时 world->setBlockFromEntity 放置 blockId 并移除（链式塌落由 caller 控制）。
void EntityManager::spawnFallingBlock(int x, int y, int z, int blockId)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); falling block spawn skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    e.halfW = 0.5f; // FallingBlock = 1×1×1 立方（同地形方块；t252 halfW/halfH 默认 0.5 显式留档）
    e.halfH = 0.5f;
    e.pushable = false; // 下落方块不被玩家推动（同掉落物变体）
    e.kind = FallingBlock;
    e.blockId = blockId;
    e.fallStartY = e.pos.y(); // t794 铁砧砸伤落差基准（spawn 时刻中心 Y；沙/砾等族不读，无害写入）
    acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    qCInfo(lcEnt) << "spawned falling block id=" << blockId << "at" << x << y << z
                  << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// t527 携带 state 的下落方块实体（积雪层专用；见 .h 头注释）：照搬 spawnFallingBlock（位置 / halfW / halfH /
//   kind=FallingBlock / pushable=false），额外写 e.blockState = state（着地 setBlockFromEntity(...,state) 写回雪层
//   保留层数；呈现层 blockStateAt 缩放薄板高度）。达 kCap 跳过 + 告警（防溢出，同 spawnFallingBlock）。
void EntityManager::spawnFallingBlockState(int x, int y, int z, int blockId, int state)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); falling block(state) spawn skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    e.halfW = 0.5f; // FallingBlock = 1×1×1 立方（着地 / 碰撞；同 spawnFallingBlock）
    e.halfH = 0.5f;
    e.pushable = false;
    e.kind = FallingBlock;
    e.blockId = blockId;
    e.blockState = state; // t527：携带 state（积雪层层数 metadata；仅 SnowLayer 用）
    e.fallStartY = e.pos.y(); // t794 铁砧砸伤落差基准（同 spawnFallingBlock；SnowLayer 不读无害）
    acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    qCInfo(lcEnt) << "spawned falling block id=" << blockId << "state=" << state << "at" << x << y << z
                  << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// t283 生成箭矢投射物：存 origin + 3D 速度 vel（含 vy 抛物）+ kind=Arrow + pushable=false + 寿命。
//   halfW/halfH=0.06（细长杆视觉 + 碰撞最小；箭命中走 point-in-AABB 不读 halfW）。bump revision → QML
//   Repeater 追加 delegate（Arrow 分支细长杆定向 Model）。达 kCap 跳过 + 告警（防溢出）。
//   t480：返新箭槽索引（fireArrow 用它设 arrowShooter —— 骷髅箭命中玩家时驯服狼反击发射者）；达 kCap → -1。
int EntityManager::spawnArrow(const QVector3D &origin, const QVector3D &vel)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); arrow spawn skipped at" << origin;
        return -1;
    }
    Entity e;
    e.pos = origin;
    e.halfW = 0.06f; // 箭细长杆（视觉 + 碰撞最小；命中检测用独立命中盒不读它）
    e.halfH = 0.06f;
    e.pushable = false; // 玩家走碰不推箭（同掉落物 / 下落方块变体）
    e.kind = Arrow;
    e.vx = vel.x(); // Arrow 复用 vx/vy/vz 作 3D 速度（Arrow 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kArrowLifetime;
    e.arrowSpawnMs = m_clock.elapsed(); // 任务（60s despawn）：spawn 墙钟（tick 硬上限用）
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    return slot;
}

// t304 玩家弓射出的箭：与 spawnArrow（骷髅射出，命中玩家 t283）对称，差异在 arrowFromPlayer=true（命中 mob）+
//   arrowDamage 由蓄力决定（1..6）。tick Arrow 分支据 arrowFromPlayer 分流命中目标（true→mob / false→玩家）。
void EntityManager::spawnArrowPlayer(const QVector3D &origin, const QVector3D &vel, int damage,
                                     float kbMul, float igniteSec)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); player arrow spawn skipped at" << origin;
        return;
    }
    Entity e;
    e.pos = origin;
    e.halfW = 0.06f; // 同 spawnArrow（细长杆视觉 + 碰撞最小）
    e.halfH = 0.06f;
    e.pushable = false;
    e.kind = Arrow;
    e.vx = vel.x();
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kArrowLifetime;
    e.arrowSpawnMs = m_clock.elapsed(); // 任务（60s despawn）：spawn 墙钟（tick 硬上限用）
    e.arrowFromPlayer = true;                  // 命中 mob（非玩家）
    e.arrowDamage = damage > 0 ? damage : 1;   // 蓄力伤害（防御 ≥1）
    // t960 弓附魔载荷（Game 层算好传入）：击退强度 = 基线 × 震击倍率（kbMul≤0 防御按基线）；
    //   点燃时长直存（0 = 不点燃）。DMI 缺省 + 缺省参数 → 旧调用点（发射器 / 探针）行为逐字不变。
    e.arrowKbStrength = kArrowKnockbackStrength * (kbMul > 0.0f ? kbMul : 1.0f);
    e.arrowIgniteSec  = igniteSec > 0.0f ? igniteSec : 0.0f;
    acquireSlot(std::move(e));
    notifyEntitiesChanged();
}

// t482/t505 生成雪球投射物（雪傀儡 aiSnowGolem 远程攻击 / t505 玩家右键抛掷）：存 origin + 3D 速度 vel（含 vy 抛物）
//   + kind=Snowball + pushable=false + 寿命 + **命中伤害 damage**（按发射者分流，见头文件注释）。halfW/halfH=0.10
//   （白色小球视觉 + 碰撞最小；命中检测走点-in-AABB 不读 halfW）。bump revision → QML Repeater 追加 delegate
//   （Snowball 分支白色小球定向 Model）。达 kCap → 跳过 + 告警（防溢出）。返新雪球槽索引（调试用）；达 kCap → -1。
int EntityManager::spawnSnowball(const QVector3D &origin, const QVector3D &vel, int damage, int thrower)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); snowball spawn skipped at" << origin;
        return -1;
    }
    Entity e;
    e.pos = origin;
    e.halfW = 0.10f; // 雪球小圆球视觉 + 碰撞最小
    e.halfH = 0.10f;
    e.pushable = false; // 玩家走碰不推（同箭 / 掉落物变体）
    e.kind = Snowball;
    e.vx = vel.x(); // 复用 vx/vy/vz 作 3D 速度（Snowball 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kSnowballLifetime;
    e.snowballDamage = damage;   // t505 按发射者分流（golem=kSnowballDamage / player=0；命中分支读它）
    e.snowballThrower = thrower; // t553 发射者槽索引（fireSnowball=雪傀儡 idx / 玩家=-1；命中分支排除自身）
    // rv-low-batch1 发射者代际快照（修槽复用误排除）：thrower 槽当前任的 spawnSerial 一并记下 → 命中排除
    //   同时比对 slot+serial，槽复用换任后不再误排除新生物。越界防御（thrower 无效 / 玩家 -1）→ 0 不影响。
    if (thrower >= 0 && thrower < int(m_entities.size()))
        e.snowballThrowerSerial = m_entities[size_t(thrower)].spawnSerial;
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    return slot;
}
// t583 生成鸡蛋投射物（玩家右键投掷 / 发射器弹射；见头文件注释）：存 origin + 3D 速度 vel（含 vy 抛物）+
//   kind=Egg + pushable=false + 寿命。halfW/halfH=0.10（卵形小体视觉 + 碰撞最小；命中检测走点-in-AABB）。
//   bump revision → QML Repeater 追加 delegate（Egg 分支奶白卵形 Model）。达 kCap → 跳过 + 告警（防溢出）。
//   返新鸡蛋槽索引（调试用）；达 kCap → -1。
int EntityManager::spawnEgg(const QVector3D &origin, const QVector3D &vel)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); egg spawn skipped at" << origin;
        return -1;
    }
    Entity e;
    e.pos = origin;
    e.halfW = 0.10f; // 鸡蛋卵形小体视觉 + 碰撞最小
    e.halfH = 0.10f;
    e.pushable = false; // 玩家走碰不推（同箭 / 雪球）
    e.kind = Egg;
    e.vx = vel.x(); // 复用 vx/vy/vz 作 3D 速度（Egg 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kEggLifetime;
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    return slot;
}

// t728 生成火球投射物（燃烬者 aiEmberling 远程攻击；见头文件注释）：存 origin + 3D 速度 vel（blocks/s，直线弹道，
//   重力 ~0）+ kind=Fireball + pushable=false + 寿命。halfW/halfH=0.15（橙黄火球小体视觉 + 碰撞最小；命中检测走
//   点-in-AABB 不读 halfW）。entity.mobType 设 MobEmberling（火球命中玩家时 mobAttackedPlayer 携它在 QML 映射死因
//   尾追 Emberling）。bump revision → QML Repeater 追加 delegate（Fireball 分支橙黄自发光小球 Model）。达 kCap →
//   跳过 + 告警（防溢出）。返新火球槽索引（调试用）；达 kCap → -1。
int EntityManager::spawnFireball(const QVector3D &origin, const QVector3D &vel, int igniteChancePct)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); fireball spawn skipped at" << origin;
        return -1;
    }
    Entity e;
    e.pos = origin;
    e.halfW = 0.15f; // 橙黄火球小体视觉 + 碰撞最小
    e.halfH = 0.15f;
    e.pushable = false; // 玩家走碰不推（同箭 / 雪球）
    e.kind = Fireball;
    e.mobType = MobEmberling; // 命中玩家 mobAttackedPlayer 携它 → QML 映射死因 Emberling
    e.vx = vel.x(); // 复用 vx/vy/vz 作 3D 速度（Fireball 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kFireballLifetime;
    e.fireballIgnitePct = igniteChancePct; // t891② per-entity 撞击点燃概率（默认 20；玩家烈焰弹 100）
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    return slot;
}

// 审查修 B8（t724-t729 复盘）：统一生成 AI 段记入 pending 的火球 / 箭 —— aiEmberling / aiArcher 在 tick
//   主实体循环持 Entity& 引用期间直接 spawn，acquireSlot 无空闲槽时 push_back → vector 扩容使循环内全部
//   引用悬空（堆损坏级 UB；违反 tickBreeding 前注释的「主循环不可 push_back」不变量，t400 繁殖同因先例）。
//   现改为：AI 只记请求（发射者槽 + spawnSerial 代际 + 弹道参数），本方法在主循环结束后被 tick 调用 ——
//   此时不再持有任何 Entity&，且逐项重新按索引取引用（前一项 spawn 扩容不影响后一项重取）→ 安全。
//   发射者双查（slot + serial + 活体校验）：主循环内发射者可能已死 / 槽被复用 → 弃射（死者不补射，机制
//   等价 MC 死亡瞬间不出手）。火球在此写入 fireballShooter/Serial（审查修 B1 的命中排除数据源）。
void EntityManager::flushPendingShots()
{
    for (const PendingFireball &fb : m_pendingFireballs) {
        if (fb.shooterIdx < 0 || fb.shooterIdx >= int(m_entities.size())) continue;
        const Entity &sh = m_entities[size_t(fb.shooterIdx)];
        if (!sh.alive || sh.kind != Mob || sh.dead || sh.spawnSerial != fb.shooterSerial) continue; // 死者/槽复用 → 弃射
        const int slot = spawnFireball(fb.origin, fb.vel);
        if (slot >= 0 && slot < int(m_entities.size())) {
            Entity &fbEnt = m_entities[size_t(slot)]; // push_back 后按新索引重取（安全）
            fbEnt.fireballShooter = fb.shooterIdx;
            fbEnt.fireballShooterSerial = fb.shooterSerial;
        }
    }
    m_pendingFireballs.clear();
    for (const PendingArrow &ar : m_pendingArrows) {
        if (ar.shooterIdx < 0 || ar.shooterIdx >= int(m_entities.size())) continue;
        Entity &sh = m_entities[size_t(ar.shooterIdx)]; // 每项重取（前一项 spawn 可能扩容）
        if (!sh.alive || sh.kind != Mob || sh.dead || sh.spawnSerial != ar.shooterSerial) continue;
        fireArrow(ar.shooterIdx, sh, ar.target); // 内部 spawnArrow 后仅按索引访问（fireArrow 尾段安全）
    }
    m_pendingArrows.clear();
}
// t729 生成暗渊之眼投射物（玩家右键 EndEyeId 掷出；见头文件注释）：存 origin + 3D 速度 vel（blocks/s，初速方向
//   由 Game 层算；t757 两段式接管后 vel 仅作初速，tick 内转向平滑修正）+ kind=EnderEye + pushable=false +
//   halfW/halfH=0.16（小绿瞳珠小体视觉 + 碰撞最小；命中检测走距离判定不读 halfW）。entity.enderEyeDistLeft =
//   随机 [kEnderEyeDistMin,Max]（10..16）剩余飞行距离 → tick 递减归零结算；enderEyeShatter=0（飞行态）；
//   enderEyeCruiseY = origin.y()+kEnderEyeClimbHeight（t757 远段巡航高度）。vx/vy/vz 复用 3D 速度（如
//   spawnFireball）。bump revision → QML Repeater 追加 delegate（EnderEye 分支小绿瞳珠 Model）。达 kCap →
//   跳过 + 告警（防溢出）。返新槽索引（调试用）；达 kCap → -1。
int EntityManager::spawnEnderEye(const QVector3D &origin, const QVector3D &vel)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); ender eye spawn skipped at" << origin;
        return -1;
    }
    Entity e;
    e.pos = origin;
    e.halfW = kEnderEyeHalfDim; // 小绿瞳珠小体视觉 + 碰撞最小
    e.halfH = kEnderEyeHalfDim;
    e.pushable = false; // 玩家走碰不推（同箭 / 雪球 / 火球）
    e.kind = EnderEye;
    e.vx = vel.x(); // 复用 vx/vy/vz 作 3D 速度（EnderEye 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    // 判定飞行距离随机带（机制等价 MC 末影之眼飞行一段后落地/碎裂；玩家据此逐步逼近要塞）。
    e.enderEyeDistLeft = kEnderEyeDistMin
        + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kEnderEyeDistMax - kEnderEyeDistMin);
    e.enderEyeShatter = 0.0f; // 飞行态（非碎裂）
    // t757 远段巡航高度：掷出眼位 + kEnderEyeClimbHeight（spawn 定死不随地形变 —— 眼睛无方块碰撞，
    //   平飞穿山可接受；换算依据是「玩家上方的指示高度」而非地表，故以掷出点为基准最直观）。
    //   审查 #1 回归补回：t758 插入 spawnEnderPearl 时本赋值被 diff 吞掉 → 字段全工程无写入点（只剩
    //   头文件默认 0.0f）→ tick 远段 gap 恒负、爬升分量恒 0，升空巡航整体死码。矩阵测试有断言防线。
    e.enderEyeCruiseY = origin.y() + kEnderEyeClimbHeight;
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    return slot;
}
// t758 生成暗渊珠投射物（玩家右键 EnderPearlId 掷出；见头文件注释）：存 origin + 3D 速度 vel（含 vy 抛物）+
//   kind=EnderPearl + pushable=false + 寿命。halfW/halfH=kEnderPearlHalfDim（深绿小珠视觉 + 碰撞最小；命中
//   判定走点格不读它）。bump revision → QML Repeater 追加 delegate（EnderPearl 分支深绿小珠 Model）。达 kCap →
//   跳过 + 告警（防溢出）。返新珠槽索引（调试用）；达 kCap → -1。
int EntityManager::spawnEnderPearl(const QVector3D &origin, const QVector3D &vel)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); ender pearl spawn skipped at" << origin;
        return -1;
    }
    Entity e;
    e.pos = origin;
    e.halfW = kEnderPearlHalfDim; // 深绿小珠视觉 + 碰撞最小
    e.halfH = kEnderPearlHalfDim;
    e.pushable = false; // 玩家走碰不推（同箭 / 雪球 / 眼）
    e.kind = EnderPearl;
    e.vx = vel.x(); // 复用 vx/vy/vz 作 3D 速度（EnderPearl 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kEnderPearlLifetime; // 寿命兜底「命中」（悬空到期视作落点结算传送）
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    return slot;
}

// t836 生成钓鱼浮标投射物（玩家右键甩竿；见头文件注释）：存 origin + 3D 速度 vel（含 vy 抛物）+ kind=Bobber +
//   pushable=false + 寿命 + 甩竿序号 castSerial（确定性等待掷骰错峰源；鱼跑重掷时实体内自增）。halfW/halfH=
//   kBobberHalfDim（小浮标视觉 + 碰撞最小；命中判定走点格不读它）。渲染不走 mobHost Repeater（Main.qml 的
//   player.fishing 专属 delegate 绑 PlayerController 镜像的 bobberPosition），但 revision 照 bump（Game 层镜像
//   读 posAt 需要数据新鲜度；Repeater 内无 Bobber 分支 → 无 delegate 开销）。达 kCap → 跳过 + 告警（防溢出）。
//   返浮标槽索引（Game 层 m_bobberEntityIdx 跟踪）；达 kCap → -1。
int EntityManager::spawnBobber(const QVector3D &origin, const QVector3D &vel, quint32 castSerial,
                               float waitScale, float biteWindowExtra)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); bobber spawn skipped at" << origin;
        return -1;
    }
    Entity e;
    e.pos = origin;
    e.halfW = kBobberHalfDim; // 小浮标视觉 + 碰撞最小
    e.halfH = kBobberHalfDim;
    e.pushable = false; // 玩家走碰不推（同箭 / 雪球 / 珠）
    e.kind = Bobber;
    e.vx = vel.x(); // 复用 vx/vy/vz 作 Flying 段 3D 速度（Bobber 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kBobberLifetime; // 寿命次级镜像（dt 递减；墙钟 kBobberLifetimeMs 为真值源，review25 #14）
    e.arrowSpawnMs = m_clock.elapsed(); // review25 #14：寿命墙钟起点（同箭 60s despawn 先例——dt 钳 50ms 卡顿
                                        //   下 arrowLife 只慢不快（挂机浮标滞留 >180s），墙钟不依赖 dt 必然到期）
    e.bobberState = kBobberStFlying;
    e.bobberSerial = castSerial;   // 掷骰序号（等待值在落水 settle 时算）
    // t960 钓竿附魔载荷（Game 层算好传入）：等待期倍率存实体（落水首掷 / 鱼跑重掷两处掷骰同乘）；
    //   判定窗 = 基线常量 + 附加秒（kBobberBiteWindowSec 基值 1.0 用户口径钉死不动，附加式加宽）。
    //   缺省参数（1.0 / 0）→ 旧调用点行为逐字不变。
    e.bobberWaitScale  = waitScale > 0.0f ? waitScale : 1.0f;
    e.bobberBiteWindow = kBobberBiteWindowSec + (biteWindowExtra > 0.0f ? biteWindowExtra : 0.0f);
    const int slot = acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    return slot;
}

// t836 确定性等待掷骰（纯函数；矩阵探针直调锁 [5,30] 两端恰可达：h=0 → 5.00s / h=2500 → 30.00s）。
float EntityManager::bobberWaitSeconds(quint32 h)
{
    // % 2501 → [0,2500] → ×0.01 → [0,25.00]s 加在 5s 基线上 → [5.00, 30.00]s（MC 1.0 wiki 口径区间）。
    return kBobberWaitMinSec + float(h % 2501u) * 0.01f;
}

// t836 收竿拉拽（见 .h 头注释）：钩住 mob 收竿时把 mob 拉向玩家——水平速度 = speed × 归一(玩家-mob) 方向 +
//   上抛 vy = upSpeed（t882：由 Game 层按距离 / 收杆角度调制传入，基值对齐旧 kBobberHookPullUp 观感）+
//   解除 resting（重力分支接手上抛→减速→下落→着地）。不伤害（钩中 0 伤害，MC 口径；不调 damageEntity
//   → 无红闪无扣血，纯位移冲量）。非 Mob / dead / 越界 / 零距（玩家与 mob 重合）→ 静默早退 / yaw 兜底方向。
bool EntityManager::pullMobToward(int mobIdx, const QVector3D &towardPos, float speed, float upSpeed)
{
    // 返 bool（R19.13 终审 B-L2）：拉拽实际生效才 true——目标 dead（死亡动画 0.5s 窗内、浮标 tick 尚未跑
    //   脱钩验证）/ 非 Mob / 越界早退返 false，caller 据此不扣钓竿耐久（对垂死 mob 收竿 = 空收，无获物无消耗）。
    if (mobIdx < 0 || mobIdx >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(mobIdx)];
    if (!e.alive || e.kind != Mob || e.dead) return false;
    float dx = towardPos.x() - e.pos.x();
    float dz = towardPos.z() - e.pos.z();
    float len = std::sqrt(dx * dx + dz * dz);
    if (!(std::isfinite(len) && len > 1e-3f)) {
        // 玩家与 mob 水平重合 → 用 mob 朝向兜底（同 knockback 零向量防御；拉拽方向退化不致 NaN）。
        dx = -std::sin(e.yawRad);
        dz = -std::cos(e.yawRad);
        len = 1.0f;
    }
    dx /= len;
    dz /= len;
    e.vx = dx * speed;
    e.vz = dz * speed;
    e.vy = upSpeed;           // t882 上抛分量（距离 / 角度调制后的传入值；拉离地面 + 空中拽飞弧高来源）
    e.resting = false;        // 解除静止 → tick 重力分支处理上抛→减速→下落→着地
    // t970 拉拽摔伤豁免置位（消费点 = tick Mob 落地沿）：本次抛物线自身的落地不结算摔伤——用户口径
    //   「拉拽是玩家动作，不该顺带摔死目标」。一次性语义：豁免在第一个落地沿无条件消费（t690 着地沿
    //   无条件清窗同型），拉拽之后的自体坠落（被推下 / 自行走下）照常结算，**非常驻免摔**（字段语义
    //   详见 Entity.fallExemptOnce 注释）。
    e.fallExemptOnce = true;
    notifyEntitiesChanged();
    qCInfo(lcEnt) << "mob" << mobIdx << "hook-pulled toward player speed=" << speed << "up=" << upSpeed;
    return true;
}

// t836 浮标咬钩态查询（Game 层收竿结算读；越界 / 非活体 Bobber → false，同 aliveAt 越界安全语义）。
bool EntityManager::bobberHasBiteAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Bobber) return false;
    return e.bobberHasBite;
}

// t836 浮标已钩 mob 槽索引查询（收竿拉拽目标；越界 / 非活体 Bobber / 未钩 → -1）。
int EntityManager::bobberHookedMobAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return -1;
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Bobber) return -1;
    return e.bobberState == kBobberStHooked ? e.bobberHookedIdx : -1;
}

// t884 浮标水中浮定态查询（Game 层镜像 bobberInWater 用；见 .h 头注释）：越界 / 非活体 Bobber → false。
bool EntityManager::bobberInWaterAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Bobber) return false;
    return e.bobberState == kBobberStWater;
}

// t971 鱼粒子预告窗查询（临近咬钩 N 秒前瞻，见 .h 头注释）：Water 态等待阶段剩余 ≤ kBobberParticleLeadSec。
//   纯读查询（不改任何字段——tick 本体零改动，等待 / 窗口倒计时既有时序即权威源）。
bool EntityManager::bobberApproachAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Bobber) return false;
    return e.bobberState == kBobberStWater && !e.bobberHasBite
           && e.bobberBiteTimer <= kBobberParticleLeadSec;
}

// t889 暂停期墙钟顺延（语义见 .h 声明处头注释）：活体槽 arrowSpawnMs 整体 +ms。ms<=0 早退（幂等防御）。
//   只动墙钟字段，不动位置 / 速度 / bobber 计时（那些走 dt，tick 停即冻结），无 revision bump（纯寿命
//   簿记，无呈现变化）。
void EntityManager::deferWallClocks(qint64 ms)
{
    if (ms <= 0) return;
    for (Entity &e : m_entities) {
        if (e.alive) e.arrowSpawnMs += ms;
    }
}

//   spawnMobTyped 内 switch 据 mobType 设 hostile=true（兜底）。spec「黑暗刷怪调度」周期 spawn 调用它。
//   mobType 非 Shambler/Bones → 仍生成但非敌对语义（防御；正常 caller 只传这两种）。
void EntityManager::spawnHostileMob(int x, int y, int z, int mobType)
{
    QString color;
    int health = kHostileDefaultHealth;
    if (mobType == MobBones) {
        color = QStringLiteral("#d8d4c4"); // Bones：灰白骨色（机制等价 MC 骷髅；原创配色非照搬）
    } else if (mobType == MobStalker) {
        color = QStringLiteral("#5fa83a"); // Stalker：青绿色（机制等价 MC 苦力怕；原创配色非照搬）
    } else if (mobType == MobSpider) {
        // t787 修（探针暴露的 t786 潜伏缺口）：Spider 不在本色表 → 旧防御分支把它改写成 Shambler ——
        //   地牢 worldgen 蜘蛛笼（SpawnerStateSpider）经 tickSpawners 刷出的是僵尸非蜘蛛（t786 探针只测
        //   僵尸/骷髅两极性未覆盖）。补分支保型；色值同实体 delegate 暗黑红（playercontroller 蛋分支同板）。
        color = QStringLiteral("#2a1a1a");
    } else if (mobType == MobSilverfish) {
        color = QStringLiteral("#c8c2b8"); // Silverfish：灰白甲壳色（机制等价 MC 银鱼；原创配色，t487）
    } else if (mobType == MobCaveSpider) {
        // t1012③ 洞穴蜘蛛：暗蓝染体色（机制等价 MC cave spider 蓝黑体色；占位串——渲染走 MobModel 蜘蛛
        //   共享几何 + QML delegate 0.7× 蓝染 tint 不读 color，文档锚同 Spider 家族模式）。
        color = QStringLiteral("#1c3a52");
    } else if (mobType == MobNightwalker) {
        color = QStringLiteral("#2a1f2a"); // Nightwalker：暗紫黑体色（机制等价 MC 末影人暗黑体型；原创配色，t727）
    } else if (mobType == MobEmberling) {
        color = QStringLiteral("#e8b030"); // Emberling：橙黄焰色（机制等价 MC 烈焰人黄色焰体；原创配色，t728）
    } else if (mobType == MobBabyShambler) {
        color = QStringLiteral("#5a7a42"); // t952 小蹒跚者：亮一档的黄绿幼体色（区别成体暗绿 #4a6a3a；走专属贴图不读 color，占位串同族文档锚）
    } else {
        color = QStringLiteral("#4a6a3a"); // Shambler：暗绿腐肉色（机制等价 MC 僵尸；原创配色）
        if (mobType != MobShambler) mobType = MobShambler; // 防御：非七敌对型一律按 Shambler
    }
    spawnMobTyped(x, y, z, mobType, color, health);
    // spawnMobTyped 内 switch 已对 Shambler/Bones/Stalker/Spider/Silverfish/Nightwalker/Emberling 设
    //   hostile=true；spawnHostileMob 仅收口语义入口。
}

// t787 被动生物生成入口（spawnHostileMob 的被动镜像，见头文件注释）：配色与 PlayerController 蛋分支 /
//   Main.qml 进世界散布（t374）同板（pig #f0a8b0 / cow #5a4030 / sheep #f5f0e8 / chicken #f5f0e4 / squid
//   #6a4a3a / wolf #c8ccd4 / ocelot #e8c890 —— 纯占位串，被动型走 MobModel + 贴图不读 color；文档锚对齐）。
//   血量 kDefaultMaxHealth（=10，MC 1.0 猪/牛/羊 5 心，同散布/蛋路径）；spawnMobCore 按型设 hostile=false。
void EntityManager::spawnPassiveMob(int x, int y, int z, int mobType)
{
    QString color;
    switch (mobType) {
    case MobCow:      color = QStringLiteral("#5a4030"); break;
    case MobSheep:    color = QStringLiteral("#f5f0e8"); break;
    case MobChicken:  color = QStringLiteral("#f5f0e4"); break;
    case MobSquid:    color = QStringLiteral("#6a4a3a"); break;
    case MobWolf:     color = QStringLiteral("#c8ccd4"); break;
    case MobOcelot:   color = QStringLiteral("#e8c890"); break;
    case MobPig:      color = QStringLiteral("#f0a8b0"); break;
    default:
        color = QStringLiteral("#f0a8b0"); // 猪（兜底同型防御）
        if (mobType != MobPig) mobType = MobPig; // 防御：非被动七型一律按 Pig（敌对型应走 spawnHostileMob）
        break;
    }
    spawnMobTyped(x, y, z, mobType, color, kDefaultMaxHealth);
}

// t374 被动生物群系化类型选取：据群系 id（World::biomeIdAt 编码）按 kPassiveSpawnWeights 加权随机返
//   MobPig/MobCow/MobSheep。机制等价 MC 1.0 群系化被动刷怪池（平原牛羊 / 森林猪富集；非排斥，仅概率差异）。
//   群系 id 越界 → 兜底 Plains（索引 0）。const 只读（仅 RNG 采样，不改实体数据）。
int EntityManager::pickPassiveMobType(int biomeId) const
{
    const int b = (biomeId >= 0 && biomeId < 4) ? biomeId : 0; // 越界兜底 Plains
    const int wCow   = kPassiveSpawnWeights[b][0]; // 列 0 = 牛 MobCow
    const int wSheep = kPassiveSpawnWeights[b][1]; // 列 1 = 羊 MobSheep
    const int wPig   = kPassiveSpawnWeights[b][2]; // 列 2 = 猪 MobPig
    const int wChick = kPassiveSpawnWeights[b][3]; // 列 3 = 鸡 MobChicken（t398）
    const int total  = wCow + wSheep + wPig + wChick;
    auto *rng = QRandomGenerator::global();
    int r = int(rng->bounded(total)); // [0, total)；bounded 返 quint32，同 tickHostileLife pickMob 模式
    if (r < wCow) return MobCow;
    r -= wCow;
    if (r < wSheep) return MobSheep;
    r -= wSheep;
    if (r < wPig) return MobPig;
    return MobChicken;
}

// t786 刷怪笼 state → 笼内 mob 类型解码（见头文件注释；规则三段：type 位 / 旧 bit0 银鱼 / 兜底 Shambler）。
//   纯函数于入参：编码在 Core 层 BlockRegistry::spawnerStateForMob（数值契约），本端是**唯一**解码源
//   （tickSpawners 与 QML spawnerHost delegate 共用，防两套各写漂移——t785 单一权威教训）。
int EntityManager::spawnerMobTypeForState(int state) const
{
    const int typeBits = (int(quint8(state)) & int(BlockRegistry::SpawnerStateMobMask))
                         >> int(BlockRegistry::SpawnerStateMobShift);
    if (typeBits != 0) {
        // t786 笼带显式类型 + t787 蛋改型扩表：白名单 = 13 蛋型（pig/cow/sheep/shambler/bones/stalker/
        //   spider/chicken/squid/nightwalker/emberling/wolf/ocelot —— 生物蛋右键刷怪笼写入，单一权威
        //   RecipeRegistry::mobTypeForSpawnEgg）+ 无蛋的 Silverfish（要塞 worldgen 专属）。枚举漂移 / 手改
        //   存档的非法值（哨兵 MobTest/Tnt/Anvil、golem、>18 越界）回退 Shambler。
        switch (typeBits) {
        case MobShambler:
        case MobBones:
        case MobStalker:
        case MobSpider:
        case MobSilverfish:
        case MobPig:
        case MobCow:
        case MobSheep:
        case MobChicken:
        case MobSquid:
        case MobWolf:
        case MobOcelot:
        case MobNightwalker:
        case MobEmberling:
        case MobBabyShambler: // t952 小蹒跚者蛋（0x25D）右键刷怪笼改型（组合骰在 spawnMobCore 末段照掷）
        case MobCaveSpider:   // t1012③ 洞穴蜘蛛笼（SpawnerStateCaveSpider=0x28；worldgen pieceSpiderRoom 转正写入）
            return typeBits;
        default:
            return MobShambler;
        }
    }
    // type 位零：t487 时代旧笼。bit0=1 → 旧要塞银鱼笼；bit0=0 → 旧地牢笼（旧版 Shambler/Bones 随机刷，
    //   无从恢复原始序列 → 确定性回退最常见型 Shambler，刻意不回退 Silverfish 防旧地牢笼全变银鱼）。
    if ((int(quint8(state)) & int(BlockRegistry::SpawnerStateSilverfishFlag)) != 0)
        return MobSilverfish;
    return MobShambler;
}

// t280 当前活体敌对生物数（hostile && !dead && kind==Mob）。供 spawn 调度上限判定。
int EntityManager::hostileCount() const
{
    int n = 0;
    for (const Entity &e : m_entities) {
        if (e.alive && e.kind == Mob && e.hostile && !e.dead) ++n;
    }
    return n;
}

// t562 区域敌对计数（per-area cap）：活体敌对 mob 中 XZ 水平距离 ≤ radius 的数量。O(n)（n≤kCap 可忽略）。
//   供黑暗刷怪 / 刷怪笼调度判「玩家周边区域是否已饱和」（达 kHostileLocalCap 停刷）。const 只读。
int EntityManager::hostileCountNear(const QVector3D &center, float radius) const
{
    const float r2 = radius * radius;
    int n = 0;
    for (const Entity &e : m_entities) {
        if (!e.alive || e.kind != Mob || !e.hostile || e.dead) continue;
        const float dx = e.pos.x() - center.x();
        const float dz = e.pos.z() - center.z();
        if (dx * dx + dz * dz <= r2) ++n;
    }
    return n;
}

// t388 床周敌对判定（sleep 机制「附近有怪物拒绝」）：任一活体敌对 mob 在 center 的 radius 球内 → true。
//   3D 欧氏距离（含 Y，防楼上 / 洞下贴脸的敌对漏判）。const 只读自身数据。
bool EntityManager::hostileNearby(const QVector3D &center, float radius) const
{
    const float r2 = radius * radius;
    for (const Entity &e : m_entities) {
        if (!e.alive || e.kind != Mob || !e.hostile || e.dead) continue;
        const QVector3D d = e.pos - center;
        if (d.lengthSquared() <= r2) return true;
    }
    return false;
}

// t787 同型邻域计数（见头文件注释）：活体且未死、mobType 匹配的 Mob 在 3D 球内计数（3D 判定同
//   hostileNearby 的 lengthSquared；区别于 hostileCountNear 的 XZ 水平距离）。供 tickSpawners 被动笼
//   上限判定（机制等价 MC 1.0 刷怪笼「同类 6 只内才刷」按型判）。
int EntityManager::mobTypeCountNear(const QVector3D &center, float radius, int mobType) const
{
    const float r2 = radius * radius;
    int n = 0;
    for (const Entity &e : m_entities) {
        if (!e.alive || e.kind != Mob || e.dead || e.mobType != mobType) continue;
        const QVector3D d = e.pos - center;
        if (d.lengthSquared() <= r2) ++n;
    }
    return n;
}

// t973 生物格放方块检测（头文件注释详述口径）：格 AABB [bx,bx+1]×[by,by+1]×[bz,bz+1] 与任一活体 mob
//   碰撞盒严格相交 → true。排除面（alive/kind==Mob/!dead）与放置预检消费口径对齐：濒死 / 死亡动画帧
//   不拦放置；非 Mob kind 实体（掉落物 / 箭 / 浮标 / 引燃 TNT 等）不拦——「物品可以盖」。骑乘组合两盒
//   各自独立命中（挂载位骑手盒在载具上方，逐实体遍历天然覆盖，无需特判）。
bool EntityManager::mobOccupiesCell(int bx, int by, int bz) const
{
    for (const Entity &e : m_entities) {
        if (!e.alive || e.kind != Mob || e.dead) continue;
        const float ex0 = e.pos.x() - e.halfW, ex1 = e.pos.x() + e.halfW;
        const float ey0 = e.pos.y() - e.halfH, ey1 = e.pos.y() + e.halfH;
        const float ez0 = e.pos.z() - e.halfW, ez1 = e.pos.z() + e.halfW;
        // 严格 <（边界相触不算重叠）：脚底正好站在格顶 / 身侧贴邻格壁的 mob 不拦邻格放置。
        if (ex0 < bx + 1 && bx < ex1 && ey0 < by + 1 && by < ey1
            && ez0 < bz + 1 && bz < ez1)
            return true;
    }
    return false;
}

// t280 第 i 个实体是否敌对（hostile=true 的活体 Mob）。越界 / 非敌对 → false。
bool EntityManager::isHostileAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.alive && e.kind == Mob && e.hostile;
}

// t280 第 i 个 mob 是否正在燃烧（火焰视觉）：t344 火烧态（fireTimer>0，岩浆/火点燃；ALL mobs 含 passive）
//   OR 敌对日光 burning（tickHostileLife 每 tick 重算缓存 Entity.burning）。越界 / 非 Mob → false。
//   QML 据 isBurningAt 显火焰动画（t344：passive 着火亦显火焰 Model）。
bool EntityManager::isBurningAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    // t344：火烧态（fireTimer>0）适用于所有 Mob；日光 burning 仅敌对。二者任一为真即显火焰。
    return e.alive && e.kind == Mob && (e.fireTimer > 0.0f || (e.hostile && e.burning));
}

// t482 第 i 个 mob 是否被雪球减速（slowTimer>0；雪傀儡雪球命中后短暂减速）。QML isSlowedAt 显蓝调。
bool EntityManager::isSlowedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.alive && e.kind == Mob && e.slowTimer > 0.0f;
}

// t729 第 i 个实体是否「暗渊之眼碎裂态」（enderEyeShatter>0）。QML EnderEye delegate 据它翻 to 缩小 + 淡出 +
//   玻璃碎裂粒子动画（shatteringAt=true 的窗口内）。越界 / 非 EnderEye / 非碎裂 → false（同 aliveAt 越界安全）。
bool EntityManager::shatteringAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.alive && e.kind == EnderEye && e.enderEyeShatter > 0.0f;
}

// 审查 #1 回归探针（头文件注释详述动机）：读第 i 个暗渊之眼的远段巡航高度。离线矩阵测试 spawn 后断言
//   == origin.y()+kEnderEyeClimbHeight，防「插入新 spawn 函数被 diff 吞赋值 → 巡航死码」静默回归复刻。
float EntityManager::enderEyeCruiseYAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    return (e.alive && e.kind == EnderEye) ? e.enderEyeCruiseY : 0.0f;
}

// t951 灼烧级日光暴露采样单一权威（契约见头文件声明）。从 tickHostileLife 内联判定提炼——燃烧扣血与
//   白天阴影 AI（寻影 / 持影 / 玩家暴晒判定）共用同一采样，禁第二套光照判定（复用即正确）。门序按开销
//   递增：界内（防御）→ 见天 skyLightAt（数组读）→ 白天 → 降水 isPrecipitatingAt（内含 biome 4×fbm 噪声，
//   t500 节流先例的贵门殿后）→ 水豁免。豁免态一律 false = 视同安全。
bool EntityManager::sunBurnExposureAt(World *world, float px, float py, float pz, float halfH, float skyBrightness)
{
    if (!world) return false;
    const int sx = qFloor(px), sy = qFloor(py), sz = qFloor(pz);
    if (sx < 0 || sz < 0 || sx >= world->width() || sz >= world->depth()
        || sy < 0 || sy >= world->height()) return false;
    if (world->skyLightAt(sx, sy, sz) < 15) return false;                  // 有遮挡（树荫 / 屋檐 / 洞口，t280 同列采样）
    if (skyBrightness <= kBurnSkyBrightness) return false;                 // 夜间 / 晨昏（spec「白天燃烧」门）
    if (world->isPrecipitatingAt(sx, sz)) return false;                    // 降水遮日（t385：雨/雪/雷皆豁免）
    if (mobFeetInWater(world, px, py, pz, halfH)) return false;            // 入水不燃（t561①：脚位水格）
    if (world->blockAt(sx, sy, sz) == BlockRegistry::Water) return false;  // t561①：身体中心格水（深水悬浮）
    return true;
}

// t951 亡灵日光白名单单一权威（契约见头文件声明；审查修 B6 的名单提炼——新敌对默认不晒燃，显式加白才燃）。
//   t952 小蹒跚者同入白名单（亡灵幼体同族晒燃 + t951 白天阴影 AI 避光行为随之生效——骑乘中由骑手 AI
//   驱动载具避光，机制等价 MC 幼体僵尸日间燃烧）。
bool EntityManager::undeadBurnsInDaylight(int mobType)
{
    return mobType == MobShambler || mobType == MobBones || mobType == MobBabyShambler;
}

// review0830 #26 亡灵族谓词单一权威（契约见头文件声明）：attackMob 亡灵杀手的目标门由 t476 裸清单
//   （MobShambler || MobBones，t952 漏幼体 = 对幼体无加成且 t961 显示面 (+M) 劈叉）收口至此。
//   与 undeadBurnsInDaylight 语义有意分立：本门 =「亡灵族」，头盔免烧豁免**不进**本门（戴盔亡灵
//   不烧但仍吃亡灵杀手对族加成）。
bool EntityManager::isUndeadFamily(int mobType)
{
    return mobType == MobShambler || mobType == MobBones || mobType == MobBabyShambler;
}

// t280 黑暗刷怪调度 + 敌对日光燃烧 + 远距消失（详见头文件方法注释）。三职责一方法收口敌对生命周期。
//   分层（PLAN §2）：Entities 层，只读 World（blockAt/isSolid/skyLightAt/blockLightAt/heightAt/width/depth/height）
//   + 自身实体数据；写 EntityManager（spawn / releaseSlot / damageEntity）。world==null → 早 return。
void EntityManager::tickHostileLife(qreal dt, World *world, const QVector3D &playerPos, float skyBrightness)
{
    if (!world) return;
    FrameProfiler::Scope profHostile("mobHostile"); // t500 perf：mob 桶子分解（黑暗刷怪 / 燃烧 / 远距消失）
    const int worldW = world->width();
    const int worldD = world->depth();
    const int worldH = world->height();
    bool dirty = false;
    std::vector<int> toRemove; // 远距消失索引（releaseSlot；逆序处理避免索引漂移）

    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive || e.kind != Mob || !e.hostile) continue; // 仅敌对 Mob；passive / FallingBlock 跳过
        if (e.dead) continue; // 尸体走 deathTimer 链（tick 内已处理），不燃烧 / 不远距消失

        // t500 perf：每 hostile 每 kAiTickInterval 帧才跑一次燃烧 / 远距消失扫描（错峰 idx，同 tick 内 aiTick
        //   判定一致 —— m_tickPhase 同帧两处用同式 → 同一组 mob 在 tick / tickHostileLife 同步节流）。skyLightAt +
        //   isPrecipitatingAt（biomeAt 内含 4 次 fbm × 4 阶噪声）是 hostile 每 mob 每帧主开销，节流后平均削 1/N。
        //   burnTimer 据 hostileAccum 累积 aiDt 推进 → 平均燃烧扣血速率不变（kBurnDamageInterval=1s 量级，
        //   节流到 15Hz 误差 <100ms 不可察觉）。spawn 调度（段 c）已有 kSpawnInterval=2s 独立节流不受影响。
        e.hostileAccum += float(dt);
        const bool aiTick = ((m_tickPhase + quint32(idx)) % quint32(kAiTickInterval)) == 0;
        if (!aiTick) continue;
        const float aiDt = e.hostileAccum;
        e.hostileAccum = 0.0f;

        // (a) 日光燃烧判定：mob 所在格直接见天（skyLightAt>=15 = 无遮挡）且白天（skyBrightness>门槛）→ 燃烧。
        //   mob 中心格：用 body 中心 Y（pos.y）所处方块格（同 tick 的窒息判定取身体高度处格）。
        //   shade（skyLightAt<15，如树叶下 / 屋檐 / 洞口）→ 不燃烧（机制等价 MC 树荫保护敌对）。
        //   夜间（skyBrightness<=门槛）→ 不燃烧（spec「白天燃烧消失」，仅白天）。
        //   t284：Stalker（苦力怕）非亡灵不燃烧（亡灵白名单）；t385：降水露天不燃烧；t561①：水覆盖不烧；
        //   t561③：头盔免疫（mob 侧语义留在调用点）；审查修 B6：白名单单一权威防新敌对漏排。
        //   t951：判定本体提炼为静态成员 sunBurnExposureAt（灼烧级日光采样单一权威——t951 白天阴影 AI 与
        //   本处共用同一采样，禁第二套光照判定），亡灵白名单提炼为 undeadBurnsInDaylight（白天阴影 AI 同门
        //   ——只有会晒燃的亡灵才做避光行为）。各豁免门语义逐字保留在谓词内（见其定义注释）。
        const bool inDaylight = sunBurnExposureAt(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH, skyBrightness)
                                && undeadBurnsInDaylight(e.mobType) && e.armorHelmet == 0;
        if (inDaylight) {
            if (!e.burning) { e.burning = true; dirty = true; } // 翻入燃烧 → bump（QML 显火焰）
            e.burnTimer += aiDt;
            if (e.burnTimer >= kBurnDamageInterval) {
                e.burnTimer -= kBurnDamageInterval;
                damageEntity(idx, 1); // 复用受击链：扣 1HP + 红闪 + （归零时）mobDied 死亡消失
                dirty = true;
            }
        } else {
            if (e.burning) { e.burning = false; dirty = true; } // 出日光 → 停燃烧（QML 隐火焰）
            e.burnTimer = 0.0f; // 不燃烧时清零（机制等价 MC 出日光即停烧；非 MC「燃烧一段时间后才灭」简化）
        }

        // (b) 远距消失：敌对距玩家 > kFarDespawn（且不正在燃烧 —— 燃烧中让 damageEntity 链自然处理消失，
        //   避免火焰视觉被远距消失打断）。releaseSlot 标空槽（slot 复用保 count 单调不降，lessons-learned t256）。
        //   距离用 XZ 主导（玩家与 mob 多在同一高度层；Y 大差不影响「水平远」语义）。
        if (!e.burning) {
            const float dx = e.pos.x() - playerPos.x();
            const float dz = e.pos.z() - playerPos.z();
            if ((dx * dx + dz * dz) > kFarDespawn * kFarDespawn) {
                toRemove.push_back(idx);
                dirty = true;
            }
        }
    }

    // 逆序 releaseSlot（避免索引漂移；release 不 erase，但保持逆序习惯以备 erase 演进）。
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
        releaseSlot(*it);
    }

    // (c) spawn 调度：节流到 kSpawnInterval 秒一次。达 kHostileMobCap 则跳过（passive / FallingBlock 走 kCap
    //   不受此限）。每周期 kSpawnAttempts 次随机选点（地表 / 洞穴），首个合格点 spawn 一个敌对后本周期收手
    //   （慢速堆叠到 cap，机制等价 MC 周期 spawn）。hostileCount 在 releaseSlot 后算（含本 tick 远距消失腾出的槽）。
    m_spawnAccum += float(dt);
    if (m_spawnAccum >= kSpawnInterval) {
        m_spawnAccum = 0.0f;
        // t562：全局 cap（kHostileMobCap / kCap）+ **区域 cap**（kHostileLocalCap within kHostileAreaRadius）——
        //   玩家周边敌对已饱和（≥12 只）→ 本周期停刷（「每区块/区域 mob 上限，达上限停刷」）。区域计数在
        //   releaseSlot 后算（含本 tick 远距消失腾出的槽）。全局 cap 依旧兜底（30 全图）。
        if (hostileCount() < kHostileMobCap && m_liveCount < kCap
            && hostileCountNear(playerPos, kHostileAreaRadius) < kHostileLocalCap) {
            auto *rng = QRandomGenerator::global();
            const float pfx = playerPos.x();
            const float pfz = playerPos.z();
            for (int attempt = 0; attempt < kSpawnAttempts; ++attempt) {
                // 环内随机选点：角度 [0,2π)、距离 [kSpawnMinDist, kSpawnMaxDist]。
                const float ang = float(rng->bounded(360)) * (0.017453292519943295f);
                const float dist = kSpawnMinDist
                                   + float(rng->bounded(1000)) / 1000.0f * (kSpawnMaxDist - kSpawnMinDist);
                const int cx = int(pfx + std::cos(ang) * dist);
                const int cz = int(pfz + std::sin(ang) * dist);
                if (cx < 0 || cz < 0 || cx >= worldW || cz >= worldD) continue;
                const int surfH = world->heightAt(cx, cz);
                if (surfH < 1) continue; // 列无地表（极端情况）
                // 选地表 or 洞穴（各 50%）：地表贴 surfH+1、洞穴在地表下随机一层。
                int cy = surfH + 1;
                const bool caveSpawn = (rng->bounded(2) != 0);
                if (caveSpawn) {
                    const int caveMax = surfH - 3; // 洞穴最深到 surfH-3（保留表面 3 格地层不动）
                    const int caveMin = 2;         // 不刷基岩层（y<2 接近基岩）
                    if (caveMax <= caveMin) continue;
                    cy = caveMin + rng->bounded(caveMax - caveMin + 1);
                }
                if (cy < 1 || cy >= worldH - 1) continue; // 越界 / 顶格
                // t642 AABB 适配校验：目标格 + 头顶格（敌对 mob 高 1.8 占两格）均须 air。旧版仅查目标格
                //   air → 1 格高洞穴气袋 / 地表树冠压顶（surfH+2 是树叶）下照样刷 → mob 生成即嵌方块卡死
                //   （用户「晚上僵尸生成卡在方块里」根因）。spawnCellFitsHostile 内含双格 + 界内校验。
                if (!spawnCellFitsHostile(world, cx, cy, cz)) continue;
                // review26 #19：脚下支撑收口 isCollidable（t865 单一权威）——旧 isSolid（非 air）把花草 /
                //   轨 / 火把 / 作物 / 火当「可站立」，刷在其上的 mob 下帧失支撑再坠落。
                if (!world->isCollidable(cx, cy - 1, cz)) continue; // 脚下须有碰撞支撑（防悬空 / 花草刷怪）
                const quint8 skyL = world->skyLightAt(cx, cy, cz);
                const quint8 blkL = world->blockLightAt(cx, cy, cz);
                const float effSkyL = float(skyL) * skyBrightness; // 天光乘昼夜（夜间→0、白天→原值）
                const float effLight = std::max(effSkyL, float(blkL));
                if (effLight >= kSpawnLightThreshold) continue; // spec「light<阈值(7)」
                // 合格点：spawn 一个敌对（Shambler / Bones / Stalker / Nightwalker；t284 加 Stalker；
                //   t727 加 Nightwalker 夜行者）。t830 燃烬者（Emberling）**摘除主世界自然刷新**（R19.13：
                //   烈焰人语义属下界，当前只有主世界 → 不该自然刷；生物蛋 / 刷怪笼改型等手动刷保留——
                //   spawnHostileMob / 被动笼路由不经本表）。下界更新后再恢复入池。份额：Shambler 2/5、
                //   其余各 1/5（原 6 份表去掉 Emberling 后归一，机制等价 MC 1.0 主世界黑暗刷怪池不含烈焰人）。
                const int pickMob = rng->bounded(5); // 5 份：Shambler 2/5、Bones/Stalker/Nightwalker 各 1/5
                const int spawnType = (pickMob == 0 || pickMob == 1) ? MobShambler
                                    : (pickMob == 2) ? MobBones
                                    : (pickMob == 3) ? MobStalker : MobNightwalker;
                // t952 幼体翻变：选中蹒跚者 → kBabyShamblerSpawnChance（5%）翻成小蹒跚者（机制等价 MC
                //   小僵尸稀有自然生成；再经 spawnMobCore 的 kChickenJockeyChance 掷小鸡骑士组合骰）。
                int finalSpawnType = spawnType;
                if (spawnType == MobShambler
                    && float(rng->bounded(1000)) / 1000.0f < kBabyShamblerSpawnChance) {
                    finalSpawnType = MobBabyShambler;
                }
                spawnHostileMob(cx, cy, cz, finalSpawnType);
                qCInfo(lcEnt) << "hostile spawned type" << finalSpawnType
                             << "at" << cx << cy << cz << "effLight=" << effLight
                             << "(hostile" << hostileCount() << "/" << kHostileMobCap << ")";
                break; // 本周期成功 spawn 1 个即收手（慢速堆叠；下个 kSpawnInterval 周期再尝试）
            }
        }
    }

    if (dirty) {
        notifyEntitiesChanged();
    }
}

// t392 刷怪笼周期刷怪（见头文件方法注释）。机制等价 MC 1.0 刷怪笼：玩家在范围内时周期 spawn 笼型 mob。
//   实现策略 —— **按需扫描**：每 kSpawnerInterval 秒扫玩家所在格周围 ±kSpawnerScanRange 立方体找 Spawner 方块，
//   对每个笼按笼型分流闸门（review #31：被动笼 = 同型 local cap + 总 cap；敌对笼 = 全局敌对 cap + 区域
//   cap + 笼周敌对 local cap）+ 找到合法 spawn 位 → spawn 1 只。不维护 spawner 位置列表 → 破坏即停
//   （blockAt != Spawner 自然跳过）、存档加载后仍能扫到（无 index 维护负担）。
void EntityManager::tickSpawners(qreal dt, World *world, const QVector3D &playerPos)
{
    if (!world) return;
    FrameProfiler::Scope profSpawn("mobSpawn"); // t500 perf：mob 桶子分解（刷怪笼周期扫描）
    m_spawnAccumSpawner += float(dt);
    if (m_spawnAccumSpawner < kSpawnerInterval) return;
    m_spawnAccumSpawner = 0.0f;

    // review #31（Review 2026-08-23 低危）：入口只留**总 cap**（kCap，对被动 / 敌对笼共同生效）。旧入口的
    //   全局敌对 cap + 玩家周边敌对区域 cap 早退对被动笼同样生效 → 夜里敌对满 30 时猪 / 羊笼全停。机制
    //   等价 MC 1.0：spawner 不受 ambient hostile cap 约束（笼自有 local cap）→ 敌对闸门（全局 + 区域）全部
    //   下沉到敌对笼分支（被动笼仅留同型 local cap）。入口计数 hostileCount 保留作敌对预算种子。
    if (m_liveCount >= kCap)
        return;

    // 扫玩家所在格周围 ±kSpawnerScanRange 立方体（限 Y 到 [0, worldHeight)，防越界）。
    const int pcx = int(std::floor(playerPos.x()));
    const int pcy = int(std::floor(playerPos.y()));
    const int pcz = int(std::floor(playerPos.z()));
    const int worldW = world->width();
    const int worldD = world->depth();
    const int worldH = world->height();
    const int x0 = std::max(0, pcx - kSpawnerScanRange);
    const int x1 = std::min(worldW - 1, pcx + kSpawnerScanRange);
    const int y0 = std::max(0, pcy - kSpawnerScanRange);
    const int y1 = std::min(worldH - 1, pcy + kSpawnerScanRange);
    const int z0 = std::max(0, pcz - kSpawnerScanRange);
    const int z1 = std::min(worldD - 1, pcz + kSpawnerScanRange);

    int hostilesRunning = hostileCount(); // 本周期内已存在的敌对数（敌对笼全局预算；被动笼不挤占、不读它）
    bool hostileAreaCapped = false;       // review #31：玩家周边敌对区域 cap（惰性一次，首个敌对笼时算——原入口
                                          //   早退条件之一，现仅敌对笼分支生效；被动笼不再被它压制）
    bool dirty = false;

    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (world->blockAt(x, y, z) != BlockRegistry::Spawner) continue;
                // 玩家在范围内（XZ 距离 ≤ kSpawnerPlayerRange）。机制等价 MC 1.0 刷怪笼玩家 16 格内激活。
                const float ddx = float(x) - playerPos.x();
                const float ddz = float(z) - playerPos.z();
                if (ddx * ddx + ddz * ddz > kSpawnerPlayerRange * kSpawnerPlayerRange) continue;

                // 笼类型先解码（t786 类型化刷怪笼；spawnerMobTypeForState 是唯一解码源——type 位=
                //   worldgen placeDungeons 加权随机 / placeStronghold 银鱼 / 创造放置默认 Shambler / t787
                //   生物蛋右键改型全蛋表；type 位零的旧存档笼按 bit0 分流银鱼/Shambler，见该函数注释）。
                //   机制等价 MC 1.0 刷怪笼刷**笼内类型**的怪。类型先于闸门 / 找位解码：被动 / 敌对走不同
                //   闸门组（review #31）且鱿鱼走水格找位（review #30）。
                const int mobType = spawnerMobTypeForState(int(world->stateAt(x, y, z)));
                bool passiveType = false;
                switch (mobType) {
                case MobPig:
                case MobCow:
                case MobSheep:
                case MobChicken:
                case MobSquid:
                case MobWolf:
                case MobOcelot:
                    passiveType = true;
                    break;
                default:
                    break; // 敌对七型（Shambler/Bones/Stalker/Spider/Silverfish/Nightwalker/Emberling）
                }

                // 闸门按笼型分流（review #31：MC spawner 不受 ambient hostile cap 约束——被动笼仅留同型
                //   local cap；敌对笼保留三重闸门 = 全局 cap + 区域 cap + 笼周敌对计数）。
                const QVector3D spawnerCenter(float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f);
                if (passiveType) {
                    // 被动笼：笼周**同型**计数 ≥ kSpawnerLocalCap 则跳过（t787 判据；hostileNearby 只数
                    //   敌对、对被动笼恒 false，旧入口 / 旧笼周敌对闸门全撤——夜里敌对满额时被动笼照刷）。
                    if (mobTypeCountNear(spawnerCenter, kSpawnerMobCheckRadius, mobType) >= kSpawnerLocalCap)
                        continue;
                } else {
                    // 敌对笼：全局敌对 cap（本周期已刷够 → 跳过本笼继续扫，不再 break 整个扫描——被动笼
                    //   在敌对预算耗尽后仍须能刷，review #31）+ 玩家周边区域 cap（t562，惰性一次）+ 笼周
                    //   敌对数（kSpawnerMobCheckRadius 球内 ≥ kSpawnerLocalCap → 跳过防刷爆）。
                    if (hostilesRunning >= kHostileMobCap) continue;
                    if (!hostileAreaCapped)
                        hostileAreaCapped =
                            hostileCountNear(playerPos, kHostileAreaRadius) >= kHostileLocalCap;
                    if (hostileAreaCapped) continue;
                    if (hostileNearby(spawnerCenter, kSpawnerMobCheckRadius)) continue;
                }

                // 找合法 spawn 位（笼 8 水平邻 + 笼同格上方 / 下方共 10 候选；review #30 起按笼型分流谓词）。
                //   spawnMobTyped 把 (x,y,z) 当格坐标、mob 中心放 (x+0.5, y+0.5, z+0.5)。机制等价 MC 刷怪笼
                //   在笼旁刷怪（笼自身不可站立 → 邻格 spawn）。Y 优先笼同高（玩家走入触发高度）。
                //   鱿鱼（review #30）：水生 mob 复用「空气 + 固体底」陆生谓词 → 全刷陆上慢爬（「搁浅鱿鱼」）。
                //   改单独水格谓词：本格 + 上格均 Water（身体浸没；水柱即可，无需固体底——MC 鱿鱼笼旁水体
                //   刷鱿鱼）。其余型保持「air + 上 air + 下 solid + 下非 Water/Lava」原谓词。
                static const int kSpawnDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
                static const int kSpawnDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
                const bool wantWater = (mobType == MobSquid);
                int sx = -1, sy = -1, sz = -1;
                for (int i = 0; i < 8; ++i) {
                    const int cx = x + kSpawnDx[i];
                    const int cz = z + kSpawnDz[i];
                    if (cx < 0 || cz < 0 || cx >= worldW || cz >= worldD) continue;
                    // 优先笼同高（y）、次之 y+1（玩家跳上触发高度）；二者均堵 → 跳过本邻位。
                    for (int cyOff = 0; cyOff <= 1; ++cyOff) {
                        const int cy = y + cyOff;
                        if (cy < 0 || cy >= worldH - 1) continue;        // 须留一格空间在上（mob 占 2 格高/水柱同）
                        const quint8 here = world->blockAt(cx, cy, cz);
                        const quint8 above = world->blockAt(cx, cy + 1, cz);
                        // 跨批高危 #1（09-06 review）：Cobweb 格豁免——t1012② 满网蛛网走廊把笼周 8 邻 +
                        //   上格全填 Cobweb（非 Air），陆生谓词「here/above == Air」恒假 → 洞穴蜘蛛笼
                        //   （唯一自然来源）永久零刷。机制等价 MC：cave spider 在网窝里照常刷（蛛网无碰撞，
                        //   不阻生成）。取「豁免」而非「刷出后清网」：清网会破 t786「笼周 ≥8 网 = 矿井蛛笼」
                        //   分流口径（刷几代网就没了），豁免零 worldgen 面。仅豁 Cobweb（火把 / 草 / 轨等
                        //   其余非实体格维持 Air-only 口径不变，review26 #19 支撑收口不受扰）。
                        const bool hereOk = here == BlockRegistry::Air || here == BlockRegistry::Cobweb;
                        const bool aboveOk = above == BlockRegistry::Air || above == BlockRegistry::Cobweb;
                        const bool okCell = wantWater
                            ? (here == BlockRegistry::Water && above == BlockRegistry::Water)
                            : (hereOk && aboveOk
                               // review26 #19：支撑收口 isCollidable（t865 单一权威）——水 / 岩浆本就
                               //   ShapeNone 无碰撞（旧显式排除随之免费包含），花草 / 轨 / 火把不再当支撑。
                               && world->isCollidable(cx, cy - 1, cz));
                        if (okCell) {
                            sx = cx; sy = cy; sz = cz;
                            break;
                        }
                    }
                    if (sx >= 0) break;
                }
                if (sx < 0) continue; // 笼周无合法 spawn 位 → 跳过本笼（下周期再试）

                // spawn 1 只 —— t787 路由：敌对型走 spawnHostileMob（hostile 语义 + 敌对默认血量 + 计入
                //   hostilesRunning 预算，同旧）；被动型（蛋改型写入）走 spawnPassiveMob —— 上限判据「笼周
                //   同型计数 < kSpawnerLocalCap」（hostileNearby 只数敌对、对被动笼恒 false → 不换判据会
                //   无限刷），不计入敌对预算（被动型不挤占敌对 cap；review #31 起也不再被敌对闸门压制）。
                if (passiveType) {
                    spawnPassiveMob(sx, sy, sz, mobType);
                    dirty = true;
                } else {
                    spawnHostileMob(sx, sy, sz, mobType);
                    ++hostilesRunning;
                    dirty = true;
                }
            }
        }
    }

    if (dirty) {
        notifyEntitiesChanged();
    }
}

// t256：第 i 个槽位是否活体（已分配未释放）。空槽 → false（呈现层 delegate 据它 visible 隐藏）。
bool EntityManager::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].alive;
}

// t1007：当前存活 PrimedTnt 数（kind==FallingBlock && primed 的活体槽扫描；n≤kCap=64 常数级，F3 10Hz
//   读取零压力）。t1005/t1006 关单数据面（见头文件注释）：用户「永续闪烁 TNT」现场 primed>0 = 引擎实体
//   残留（fuse 链真在 tick，查 t1005 布线形态）；primed==0 而画面仍闪 = 呈现层 delegate 冻结。
int EntityManager::primedCount() const
{
    int n = 0;
    for (const Entity &e : m_entities)
        if (e.alive && e.kind == FallingBlock && e.primed) ++n;
    return n;
}


QVector3D EntityManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    return m_entities[size_t(i)].pos;
}

float EntityManager::radiusAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    return m_entities[size_t(i)].halfW;
}

// t252 Y 碰撞半高（QML F3+B hitbox scale.y 读）。越界 → 0。
float EntityManager::halfHeightAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    return m_entities[size_t(i)].halfH;
}

bool EntityManager::pushableAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].pushable;
}

int EntityManager::kindAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return Mob;
    return m_entities[size_t(i)].kind;
}

QString EntityManager::colorAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QStringLiteral("#ff5555");
    return m_entities[size_t(i)].color;
}

// t117：FallingBlock 携带的方块 id（着地 setBlock 用；呈现层 BlockCube.blockId 贴图渲染）。
int EntityManager::blockIdAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].blockId;
}

// t527：FallingBlock 携带的方块 state（积雪层层数 metadata；仅 SnowLayer 用，其余 0）。呈现层据它缩放薄板高度。
int EntityManager::blockStateAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].blockState;
}

// t239 mob 朝向度数（QML eulerRotation.y）。与 player.yaw 同约定：dir = (-sin(yaw),0,-cos(yaw))，
//   QML eulerRotation.y = yawDeg 使模型本地 -Z（前）正对行走方向。非 Mob / 越界 → 0。
float EntityManager::yawAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob) return 0.0f;
    return qRadiansToDegrees(e.yawRad);
}

// t239 mob 当前水平速度（t241 腿摆动画频率读）。行走非零、idle/撞墙/死亡=0。非 Mob / 越界 → 0。
float EntityManager::moveSpeedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob) return 0.0f;
    return e.moveSpeed;
}

// t241 行走动画相位（QML 驱动 MobModel 腿摆）。非 Mob / 越界 → 0。
float EntityManager::walkPhaseAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob) return 0.0f;
    return e.walkPhase;
}

// t241 羊头部俯仰（QML 驱动 MobModel 头俯仰）：仅 mobType==MobSheep 且吃草周期内返 sin(πp) 包络
//   （p = 周期内进度 0..1；中段最深 = kEatHeadPitch、起末归 0）；其余 → 0（头不转）。
//   t1047 O-4：惊逃期返 0 头回正——tick 侧惊逃打断吃草只冻结 eatTimer（低头包络会停在半途），
//   panicTimer>0 期间呈现面归 0（惊逃羊抬头疾走，惊逃窗尽后包络从冻结位续走）。
float EntityManager::headPitchAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep || e.eatTimer <= 0.0f) return 0.0f;
    if (e.panicTimer > 0.0f) return 0.0f; // t1047 O-4 惊逃期头回正（返 0 条件追加 panicTimer 门）
    const float p = (kEatDuration - e.eatTimer) / kEatDuration; // 周期内进度 0..1
    return kEatHeadPitch * std::sin(3.14159265f * p);           // sin(πp) 包络：起末 0、中段最深（负=低头）
}

// t283 箭水平朝向（QML eulerRotation.y 定向杆）：据 vx/vz 用 player 同 yaw 约定（dir=(-sin,-cos)）→
//   yaw=atan2(-vx,-vz) 使杆本地 -Z 正对飞行水平方向。非 Arrow / 水平速度 ~0 / 越界 → 0。
float EntityManager::arrowYawAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Arrow) return 0.0f;
    const float h = std::sqrt(e.vx * e.vx + e.vz * e.vz);
    if (h < 1e-4f) return 0.0f;
    return qRadiansToDegrees(std::atan2(-e.vx, -e.vz));
}

// t283 箭俯仰（QML eulerRotation.x 定向杆）：pitch=atan2(vy, 水平速度)，正=上扬、负=下俯（抛物飞行中由正转负）。
//   非 Arrow / 水平速度 ~0 / 越界 → 0。
float EntityManager::arrowPitchAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Arrow) return 0.0f;
    const float h = std::sqrt(e.vx * e.vx + e.vz * e.vz);
    if (h < 1e-4f) return 0.0f;
    return qRadiansToDegrees(std::atan2(e.vy, h));
}

// t283 实体 3D 速度（F3 / 调试；Arrow 读 vx/vy/vz）。非活体 / 越界 → 零向量。
QVector3D EntityManager::velAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive) return QVector3D();
    return QVector3D(e.vx, e.vy, e.vz);
}

// t323 箭是否已嵌入方块（PlayerController::arrowPickupScan 近距拾取读；飞行中不拾免误拾）。
//   非 Arrow / 越界 → false。
bool EntityManager::isArrowStuckAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.kind == Arrow && e.arrowStuck;
}

// t323 箭是否玩家射出（仅玩家箭嵌入后可拾；骷髅箭防刷不拾，spec「SKELETON 箭不可拾取」）。非 Arrow / 越界 → false。
bool EntityManager::arrowFromPlayerAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.kind == Arrow && e.arrowFromPlayer;
}

// t604 箭已存活墙钟 ms（spawn 起算）。非 Arrow / 非活体 / 越界 → 0（调用方视 0 = 不可拾 / 刚生成）。
qint64 EntityManager::arrowAgeMsAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Arrow) return 0;
    return m_clock.elapsed() - e.arrowSpawnMs;
}

// t323 释放槽位（PlayerController 拾取嵌入箭全入背包后销毁箭；同 ItemEntityManager.removeAt 拾取销毁语义）。
//   委托 releaseSlot（标空槽 + 入 free list，保 Repeater count 单调不降，lessons-learned t256）。
void EntityManager::removeEntityAt(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    releaseSlot(i);
    notifyEntitiesChanged();
}

// t284 Stalker 蓄力膨胀进度（0..1）：仅 mobType==MobStalker 且 fuseTimer>0（正在蓄力）时返
//   clamp(fuseTimer/kFuseTime,0,1)，供 QML delegate 据 it 对 Model 做 scale + baseColor 蓄力发白。非 Stalker /
//   未蓄力（fuseTimer<=0）/ 越界 → 0（模型静态、原配色）。
float EntityManager::inflateAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobStalker || e.fuseTimer <= 0.0f) return 0.0f;
    float p = e.fuseTimer / kFuseTime;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// t804 打火石点燃 Stalker（见头文件注释）：置不可逆短引信态。仅翻自身数据（flintIgnited），蓄力推进 /
//   引爆 / 嘶声全由 aiStalker 的 flintIgnited 分支接管（同帧不引爆——点燃到爆炸 ≥1 AI tick，机制等价
//   MC 点燃后 ~1.5s 引信窗口，玩家可后退逃离伤害半径）。
bool EntityManager::igniteStalkerFlint(int idx)
{
    if (idx < 0 || idx >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(idx)];
    if (!e.alive || e.kind != Mob || e.dead) return false;       // 空槽 / 非 mob / 尸体 → no-op
    if (e.mobType != MobStalker) return false;                   // 仅 Stalker 可点燃（MC 仅苦力怕）
    if (!e.flintIgnited) {
        e.flintIgnited = true;                                    // 首次点燃 → 置位（幂等：已点燃返 true 不重复置）
        // 蓄力进度保留（近距蓄力中被点燃 → 从当前 fuseTimer 续走，≤kFuseTime 内引爆）；不重置 chasing /
        //   猫状态——aiStalker 的 flintIgnited 分支短路它们，残留态随爆炸销毁。
    }
    return true;
}

// t331 骸骨拉弓瞄准进度（0..1）：仅 mobType==MobBones 且 aimTimer>0（正在拉弓）时返 clamp(aimTimer/kAimWindup,0,1)，
//   供 QML delegate 据 it 驱动肩枢 Node 抬右臂 + MobBowGeometry 弦后拉。非 Bones / 未瞄准（aimTimer<=0）/ 越界 → 0
//   （模型静态、松弦）。机制等价 MC 1.0 骷髅停步拉弓瞄准。
float EntityManager::drawAmountAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobBones || e.aimTimer <= 0.0f) return 0.0f;
    float p = e.aimTimer / kAimWindup;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// t635 铁傀儡攻击蓄力进度（0..1；见头文件注释）：仅 MobIronGolem 蓄力期返 windup 归一。
float EntityManager::golemAttackPoseAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobIronGolem || e.golemWindup <= 0.0f) return 0.0f;
    float p = e.golemWindup / kGolemWindup;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// t727 夜行者激怒态（enraged=true）：QML delegate 据 it 播张嘴 + 上下颤抖动画 + 身体微抖。仅 MobNightwalker
//   且已激怒返 true；非 Nightwalker / 未激怒 / 越界 → false。
bool EntityManager::enragedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.kind == Mob && e.mobType == MobNightwalker && e.enraged;
}

// t727 夜行者激怒后「瞬移到玩家背后」进度（0..1；见头文件注释）：enraged 后 rageTimer/NightwalkerRageTeleportDelay。
float EntityManager::nightwalkerRageProgressAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobNightwalker || !e.enraged) return 0.0f;
    float p = e.rageTimer / kNightwalkerRageTeleportDelay;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// t727 玩家视线状态注入（PlayerController 每 tick 调）：存眼睛位置 + 归一视线方向供夜行者瞪视激怒判定。
void EntityManager::setPlayerSight(const QVector3D &eyePos, const QVector3D &lookDir)
{
    m_playerEye = eyePos;
    const float len = lookDir.length();
    if (len > 1e-5f) {
        m_playerLook = lookDir / len; // 归一（点积对方向敏感，非归一会错判眼对眼）
        m_playerSightValid = true;
    } else {
        m_playerLook = QVector3D(0, 0, -1); // 退化零向量 → 默认 -Z 并标无效（不误判瞪视）
        m_playerSightValid = false;
    }
}

// t635 铁傀儡反击锁定（PlayerController::attackMob 目标是铁傀儡时调；见头文件注释）。
void EntityManager::setGolemRetaliate(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead || e.mobType != MobIronGolem) return;
    if (!e.golemAngry) {
        e.golemAngry = true;
        qCInfo(lcEnt) << "iron golem" << i << "retaliating against player";
    }
    e.golemAngryTimer = kGolemAngryMemory;
    e.golemWindup = 0.0f; // 已在蓄力中被再打 → 重蓄（打断当前拳，重新抬臂；反击记忆刷新）
}

// t1042 被动型受击惊逃（PlayerController::attackMob 命中后调；见头文件注释）。类型门收口在此：
//   被动五型（pig/cow/sheep/chicken + 未驯服豹猫）置 panicTimer，其余（敌对 / 造物 / 驯服狼 /
//   野狼成体——后者由 setWolfProvoked 反击——驯服猫 / **狼全族**）静默 no-op。机制等价 MC 1.0：
//   被动受击只惊逃（panic ~8s）永不反击；豹猫被打只逃不反击。t1047 O-3（review0912 #3，常设裁决
//   「一切按原版」）：MC 狼**无 PanicGoal**——成体被打经 setWolfProvoked 敌对反击、幼崽零 panic
//   （旧「幼崽不反击只惊逃」wolfBaby 分支摘除：狼整体退出惊逃集，驯服狼本就豁免）。
void EntityManager::setPanicFlee(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead) return;
    const bool passiveFleeType = e.mobType == MobPig || e.mobType == MobCow || e.mobType == MobSheep
        || e.mobType == MobChicken || (e.mobType == MobOcelot && !e.ocelotTamed);
    if (!passiveFleeType) return; // t1047 O-3：狼退出惊逃集（含幼崽；旧 wolfBaby 分支摘除）
    e.panicTimer = kPanicDuration;
    qCInfo(lcEnt) << "mob" << i << "type" << e.mobType << "panic-flees for" << kPanicDuration << "s";
}

// t1042 野狼被打敌对反击锁定（PlayerController::attackMob 目标是未驯服成体狼时调；见头文件注释）。
//   chasing=玩家走 aiHostile 先例进入写点（chasing=true + chaseTimer=kChaseMemory）；维持 / 超时 / 超距
//   清收在 aiWolf 未驯服分支（同 hostile 收口）。驯服狼 / 狼幼崽 / 非 wolf / dead / 越界 → 静默 no-op。
void EntityManager::setWolfProvoked(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead || e.mobType != MobWolf) return;
    if (e.wolfTamed || e.baby) return; // t1031 驯服狼豁免维持 + t1047 O-3 幼崽零反击零 panic（狼无 PanicGoal）
    e.chasing = true;
    e.chaseTimer = kChaseMemory;
    qCInfo(lcEnt) << "wild wolf" << i << "provoked: retaliates against player";
}

// t1042 惊逃剩余秒数读口（矩阵探针钉「时长 ~8s 登记面」+ 超时回落腿；非 Mob / 越界 → 0）。
float EntityManager::panicTimerAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob) return 0.0f;
    return e.panicTimer;
}

// t239 mob 子类 id（t240 pig/cow/sheep；t242/t243 分流）。越界 → 0。
int EntityManager::mobTypeAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].mobType;
}

// t811 骑乘读口（头注释见 .h；越界 / 非 Mob 安全默认 -1）。
int EntityManager::rideCartAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size()) || m_entities[size_t(i)].kind != Mob) return -1;
    return m_entities[size_t(i)].rideCart;
}

int EntityManager::rideBoatAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size()) || m_entities[size_t(i)].kind != Mob) return -1;
    return m_entities[size_t(i)].rideBoat;
}

// t952 mob-on-mob 骑乘双向链读口（-1 = 无链；矩阵探针钉骑士组合 / 分离腿用）。越界 / 非 Mob → -1。
int EntityManager::rideMobAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size()) || m_entities[size_t(i)].kind != Mob) return -1;
    return m_entities[size_t(i)].rideMob;
}

int EntityManager::mobRiderAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size()) || m_entities[size_t(i)].kind != Mob) return -1;
    return m_entities[size_t(i)].mobRider;
}

// t952 小鸡骑士组合概率缝写（见 .h 声明注释）：钳 [0,1]（负值/越界钳边界，防调用方误传生成恒独立 / 恒组合）。
void EntityManager::setChickenJockeyChance(qreal chance)
{
    chance = std::clamp(chance, 0.0, 1.0);
    if (chance == m_chickenJockeyChance) return;
    m_chickenJockeyChance = chance;
    qCInfo(lcEnt) << "chicken jockey chance set to" << m_chickenJockeyChance;
}

// t1029 wander 冻结测试缝写（见 .h 声明注释）：headless 矩阵探针（P-t882/P-t927/P-t960 钓鱼 / 射箭族）
//   把甩钩 / 量测窗内 mob 的 wander RNG 噪声源确定性化——冻结期 aiWander 顶部早退（RNG 流不推进），
//   tick 物理（重力 / 水推 / 击退 / 拉拽）照常。产品路径零调用（缺省 false 行为不变，同
//   m_chickenJockeyChance / setBreedTimings 缝先例）。不 bump revision / 不 emit：纯 AI 输入门控，
//   无直接呈现面（位移面变化由后续 tick 的既有 dirty 链路自然携带）。
void EntityManager::setWanderFrozen(bool frozen)
{
    if (frozen == m_wanderFrozen) return;
    m_wanderFrozen = frozen;
    qCInfo(lcEnt) << "wander frozen set to" << m_wanderFrozen;
}

// t1031 驯服概率测试缝写（见 .h 声明注释）：roll >= 0 接管 tameWolf 的驯服样本（千分比 → <0.33 驯中），
//   -1 恢复全局 RNG 掷骰。生产路径零调用（P-t1031 探针专用，同 setWanderFrozen / setChickenJockeyChance
//   缝先例）。不 bump revision / 不 emit：纯下次驯服尝试的输入门控，无直接呈现面。
void EntityManager::setTameRollOverride(int roll)
{
    if (roll == m_tameRollOverride) return;
    m_tameRollOverride = roll;
    qCInfo(lcEnt) << "tame roll override set to" << m_tameRollOverride;
}

// t377 第 i 个 mob 的护甲物品 id（piece 0=头盔 / 1=胸甲 / 2=护腿 / 3=靴子；0=该部位无护甲）。越界 → 0。
//   仅 Shambler/Bones spawn 时随机分配；QML delegate 据 it 叠 layer 贴图护甲壳（t719 ArmorLayerBox）。
int EntityManager::mobArmorAt(int i, int piece) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    const Entity &e = m_entities[size_t(i)];
    switch (piece) {
    case 0: return e.armorHelmet;
    case 1: return e.armorChest;
    case 2: return e.armorLegs;
    case 3: return e.armorBoots;
    default: return 0;
    }
}

// t719 人形 mob 穿甲调试入口（见头文件注释；dev-plan t719 降级路径——mob 拾取装备 AI 未实现，用
//   /mobarmor 命令驱动通用 layer 渲染器，不強做拾取）。armorId 段与 spawn 随机护甲同源本地常量
//   （Entities 层不向上 include Game/recipe.h，PLAN §2）。
bool EntityManager::setMobArmorSet(int i, int tier)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.dead) return false;
    // 仅人形 mob（Shambler/Bones——delegate 有 ArmorLayerBox 护甲壳的两种）；其余静默早退。
    //   t952 小蹒跚者同门（幼体人形，delegate 有 baby 尺码甲壳）。
    if (e.mobType != MobShambler && e.mobType != MobBones && e.mobType != MobBabyShambler) return false;
    if (tier < 0 || tier > 4) {
        // 越界 → 清空四部位（脱甲）。
        e.armorHelmet = e.armorChest = e.armorLegs = e.armorBoots = 0;
    } else {
        constexpr int kArmorBase = 0x300; // ArmorRegistry::ArmorIdBase（同源常量）
        const int base = kArmorBase + tier * 4;
        e.armorHelmet = base + 0;
        e.armorChest  = base + 1;
        e.armorLegs   = base + 2;
        e.armorBoots  = base + 3;
    }
    notifyEntitiesChanged(); // QML 护甲壳 armId 绑定刷新
    return true;
}

// t950 mob 装备拾取穿着链 · 部位护甲写入口（见头文件注释）：Game 层扫描比较「更好才换」后调——
//   写入前旧 id 快照返回（0=原空），caller 据它把换下装备掉回地面（MC equip-swap 语义）。零注册表
//   依赖（Entities 不向上 include Game）；视觉 = t377/t719 既有链随 revision 刷新。
int EntityManager::equipMobArmorPiece(int i, int piece, int armorId)
{
    if (i < 0 || i >= int(m_entities.size())) return -1;
    if (piece < 0 || piece > 3) return -1;
    if (armorId < 0) return -1;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.dead) return -1;
    if (e.mobType != MobShambler && e.mobType != MobBones && e.mobType != MobBabyShambler) return -1; // 仅人形三种（同 setMobArmorSet 门；t952 加小蹒跚者）
    int old;
    switch (piece) {
    case 0:  old = e.armorHelmet; e.armorHelmet = armorId; break;
    case 1:  old = e.armorChest;  e.armorChest  = armorId; break;
    case 2:  old = e.armorLegs;   e.armorLegs   = armorId; break;
    default: old = e.armorBoots;  e.armorBoots  = armorId; break;
    }
    notifyEntitiesChanged(); // QML 护甲壳 armId 绑定刷新（同 setMobArmorSet）
    return old;
}

// t950 mob 手持武器槽读取（0=空手；数据登记面，暂无 QML 消费端）。越界 / 非 Mob → 0（同 mobArmorAt 语义）。
int EntityManager::mobHeldItemAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    const Entity &e = m_entities[size_t(i)];
    return e.kind == Mob ? e.heldItemId : 0;
}

// t950 mob 手持武器槽写入（Game 层比较「更好武器」后调）：快照返回旧 id（0=原空手）/ -1 拒绝。
//   手持武器**不加成** mob 攻击力（AI 常量口径，用户未要伤害面——登记取舍）；bump revision 备未来
//   手持物渲染消费端接入。
int EntityManager::equipMobHeldItem(int i, int itemId)
{
    if (i < 0 || i >= int(m_entities.size())) return -1;
    if (itemId < 0) return -1;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.dead) return -1;
    if (e.mobType != MobShambler && e.mobType != MobBones && e.mobType != MobBabyShambler) return -1; // 仅人形三种（t950 武器槽门；t952 加小蹒跚者）
    const int old = e.heldItemId;
    e.heldItemId = itemId;
    notifyEntitiesChanged();
    return old;
}

// t300 第 i 只 mob 是否已被剪羊毛（仅 MobSheep 用；其余 mob 永远 false）。越界 → false。
bool EntityManager::shearedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep) return false; // 仅 sheep 有剪羊毛态
    return e.sheared;
}

// t300 剪羊毛（spec「玩家右键羊 + 持剪刀 → 羊变裸 + 掉羊毛物品」；机制等价 MC 1.0 剪羊毛）。
//   未剪羊毛的活体 sheep → 翻 sheared=true + 设 regrowCooldown（防刚剪完立即吃草长回，spec「加重新长毛冷却」）+
//   emit sheepSheared(坐标, 毛色下标) 让呈现层 Connections 转发 ItemEntityManager.spawnItem 生成**对应色**
//   羊毛掉落实体（t834 起统一方块段：白→Wool 方块 27 / 有色→羊毛方块 63..77，机制等价 MC 剪彩色羊得对应色羊毛；
//   同 mobDied→spawnItem 模式；单向事件流，分层：Entities 层发语义事件、呈现层只消费）。bump revision
//   → QML delegate 据 shearedAt 翻羊为裸外观。已剪羊毛 / 非 sheep / dead / 越界 → 静默早退（机制等价 MC：
//   剪羊毛只对有毛的活体羊生效，已裸的羊右键无反应）。
void EntityManager::shearSheep(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep) return; // 仅 sheep 可剪
    if (e.dead || !e.alive) return;                     // 尸体 / 空槽不可剪
    if (e.sheared) return;                              // 已裸 → 无反应（不重复掉羊毛）
    e.sheared = true;
    e.regrowCooldown = kRegrowCooldown; // 剪完到能吃草方块重新长毛的硬冷却
    // 羊毛掉落在羊当前格（floor(pos)，同 mobDied 坐标约定）→ 呈现层 spawnItem 在该格中心生成掉落实体。
    const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
    qCInfo(lcEnt) << "sheep sheared at slot" << i << "pos" << e.pos << "woolIndex" << e.sheepWool
                  << "-> dropped wool at" << dx << dy << dz;
    emit sheepSheared(dx, dy, dz, e.sheepWool); // t789：携毛色下标（呈现层据此选对应色羊毛掉落 id）
    notifyEntitiesChanged(); // bump → QML delegate 据 shearedAt 翻羊为裸外观
}

// t832 染料染羊（spec「手持染料对羊右键 → 羊染成对应色」；机制等价 MC 1.0 染羊 + 一次性语义）。染即长毛
//   （已剪裸羊 sheared 翻 false——染料染在皮肤上重新长出染色的毛，QML 毛层 tint 据 sheepWoolAt 即时切色）；
//   sheepWoolDyed=true 记「当前色是染的」→ 剪毛掉该染色（shearSheep 载荷）+ 吃草长回时重掷自然权重恢复
//   自然原色（tick 长毛分支消费标记）。非 sheep / dead / 越界 / woolIndex 越界 → false（caller 不消耗染料）。
bool EntityManager::dyeSheep(int i, int woolIndex)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    if (woolIndex < 0 || woolIndex > 15) return false; // 羊毛 16 色标准序外 → 拒（防御手改存档类输入）
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep) return false; // 仅 sheep 可染
    if (e.dead || !e.alive) return false;                     // 尸体 / 空槽不可染
    e.sheepWool = woolIndex;
    e.sheepWoolDyed = true; // 长回重掷自然色的依据（自然生成 false / 染色 true）
    e.sheared = false;      // 染即长毛（染料涂在毛上，观感立即显染色毛层）
    e.regrowCooldown = 0.0f; // 新长的毛不吃草重掷（剪后才会走长回重掷链）
    qCInfo(lcEnt) << "sheep dyed at slot" << i << "woolIndex" << woolIndex;
    notifyEntitiesChanged(); // bump → QML 毛层 tint / 毛茸外观刷新
    return true;            // caller 据返值消耗 1 染料（生存）
}

// t831 手持食物喂食回血（spec「手持食物右键已驯服个体 → 喂食回血」；机制等价 MC 1.0 受伤驯服狼吃肉回血
//   优先于繁殖）。已驯服（狼 wolfTamed / 猫 ocelotTamed）+ 血量低于上限 → health 回 amount（钳上限）+ 返
//   true；满血 → false（caller 改走幼崽加速 / 求偶繁殖分支，不消耗）。未驯服 / dead / 越界 → false。
bool EntityManager::healTamedPet(int i, int amount)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    if (amount <= 0) return false; // 防御（负治疗无意义）
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.dead || !e.alive) return false;
    const bool tamed = (e.mobType == MobWolf && e.wolfTamed)
                       || (e.mobType == MobOcelot && e.ocelotTamed);
    if (!tamed) return false;              // 仅驯服个体可喂食回血
    if (e.health >= e.maxHealth) return false; // 满血 → 不回（caller 走繁殖分支）
    e.health = std::min(e.maxHealth, e.health + amount);
    qCInfo(lcEnt) << "tamed pet healed at slot" << i << "->" << e.health << "/" << e.maxHealth;
    notifyEntitiesChanged(); // bump → QML 心条刷新（尾巴角度据 healthAt 同步翘起）
    return true;            // caller 据返值消耗 1 食物（生存）
}

// t789 第 i 只羊的羊毛色下标（0..15；仅 MobSheep 用）。非 sheep / 越界 → 0（白，QML 毛层 tint 恒等）。
int EntityManager::sheepWoolAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep) return 0; // 仅 sheep 有毛色
    return e.sheepWool;
}

// t789 第 i 只羊的毛层 tint 色（QML 毛茸 Model baseColor 乘色；白 → 恒等不着色，兼容旧观感）。委托
//   sheepWoolTintForIndex（色板单一权威 kSheepWoolTints）。非 sheep / 越界 → 白。
QColor EntityManager::sheepWoolTintAt(int i) const
{
    return sheepWoolTintForIndex(sheepWoolAt(i));
}

// t789 羊毛色下标 → tint 色（sheepWoolTintAt 的按值入口；矩阵测试直调钉「游戏内 tint = 浏览器 woolPalette
//   = build_wool.py 色板」契约）。下标越界 → 钳 [0,15]（负下标落白、超界落黑，防御手改存档类输入）。
QColor EntityManager::sheepWoolTintForIndex(int woolIndex) const
{
    const int idx = std::clamp(woolIndex, 0, 15);
    return QColor(QLatin1String(kSheepWoolTints[idx]));
}

// t510 雪傀儡剪南瓜头（spec「玩家持剪刀右键雪傀儡 → 南瓜掉落 + 雪傀儡变无头 derpy 形态」；机制等价 MC 1.0
//   剪刀剪雪傀儡南瓜头）。仅 mobType==MobSnowGolem && 未剪南瓜头 && 活体可剪 → 翻 snowGolemSheared=true +
//   emit snowGolemSheared（坐标 = golem 当前格 floor(pos)）→ 呈现层 spawnItem(100=Pumpkin, 1) 掉南瓜方块。
//   bump revision → QML delegate 据 snowGolemShearedAt 切换为无头 derpy 形态（隐藏南瓜头 Model，保留眼/嘴贴
//   原头位漂浮，机制等价 MC 1.0「剪后变无头形态带眼不死的 derpy 版」）。已剪 / 非 SnowGolem / dead / 越界 → 静默。
bool EntityManager::snowGolemShearedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSnowGolem) return false;
    return e.snowGolemSheared;
}

void EntityManager::shearSnowGolem(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSnowGolem) return; // 仅 SnowGolem 可剪南瓜头
    if (e.dead || !e.alive) return;                         // 尸体 / 空槽不可剪
    if (e.snowGolemSheared) return;                         // 已无头 → 无反应（不重复掉南瓜）
    e.snowGolemSheared = true;
    // 南瓜方块掉落在 golem 当前格（floor(pos)，同 mobDied / sheepSheared 坐标约定）→ 呈现层 spawnItem 在该格
    //   中心生成掉落实体（item id = BlockRegistry::Pumpkin=100，方块 id 即物品 id，可放置回）。
    const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
    qCInfo(lcEnt) << "snow golem sheared at slot" << i << "pos" << e.pos << "-> dropped pumpkin at" << dx << dy << dz;
    emit snowGolemSheared(dx, dy, dz);
    notifyEntitiesChanged(); // bump → QML delegate 据 snowGolemShearedAt 翻为无头 derpy 外观
}

// t400 mobType 是否可繁殖被动生物（pig/cow/sheep/chicken 之一；t480 加 MobWolf；t481 加 MobOcelot）。hostile /
//   MobTest / MobSquid 不可繁殖。feedMob 食物匹配 / 求偶寻偶 / 配对均先据它门控。机制等价 MC 1.0 仅被动 farm
//   动物可繁殖；狼/猫的「仅驯服可繁殖」门控在 enterLoveMode / feedBaby 内（isBreedableType 是类型级门，
//   驯服是实例级门）。
bool EntityManager::isBreedableType(int mobType)
{
    return mobType == MobPig || mobType == MobCow || mobType == MobSheep || mobType == MobChicken
           || mobType == MobWolf || mobType == MobOcelot;
}

// t400 触发求偶期（spec「喂对应食物 → 求偶」；机制等价 MC 1.0 breeding 的 feed-to-enter-love-mode）。
//   成体可繁殖 mob + 非冷却 + 未在求偶 → 进求偶期（loveTimer=kLoveDuration）+ 返 true。
//   幼崽 / 冷却中 / 已求偶 / 非可繁殖 mob / dead / 越界 → 返 false（caller 不消耗食物）。
//   **食物匹配**由 caller（PlayerController Game 层）判：物品 id 属 RecipeRegistry（Game 层），Entities 层不向上
//   依赖（PLAN §2）。caller 先据 mobTypeAt + 持物判「食物是否匹配该物种」，匹配才调本方法。
//   bump revision + emit → QML delegate 据 inLoveAt 显心（玩家即时见求偶反馈）。同 shearSheep 修改 + emit 模式。
bool EntityManager::enterLoveMode(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive || e.dead) return false;   // 仅活体 mob 可触发
    if (!isBreedableType(e.mobType)) return false;            // 仅 pig/cow/sheep/chicken/wolf/ocelot 可繁殖
    if (e.mobType == MobWolf && !e.wolfTamed) return false;   // t480：仅**驯服狼**可繁殖（野狼喂肉无求偶，机制等价 MC）
    if (e.mobType == MobOcelot && !e.ocelotTamed) return false; // t481：仅**驯服猫**可繁殖（野豹猫喂鱼无求偶，机制等价 MC）
    if (e.baby) return false;                                 // 幼崽未成熟，不可触发求偶
    if (e.breedCooldown > 0.0f) return false;                 // 繁殖冷却中 → 喂食无效（防刷屏；caller 保住食物）
    if (e.loveTimer > 0.0f) return false;                     // 已求偶 → 不重复触发（防一次喂多个叠加）
    e.loveTimer = kLoveDuration; // 进求偶期（tick 内衰减 + 寻偶 AI 把它拉向同类求偶者）
    qCInfo(lcEnt) << "mob slot" << i << "type" << e.mobType << "-> love mode" << kLoveDuration << "s";
    notifyEntitiesChanged(); // bump → QML 据 inLoveAt 显心
    return true; // caller 据返值消耗 1 食物（生存）
}

// t479 幼崽喂食加速成长（spec「喂幼崽对应繁殖食物 → 加速长大」；机制等价 MC 1.0 喂幼崽减 ~10% 剩余成长时间）。
//   第 i 个幼崽可繁殖 mob → growTimer 减 kBabyFeedGrow（≈kBabyGrowTime 的 10%）+ 返 true。**食物匹配**由 caller
//   判（同 enterLoveMode：物品 id 属 RecipeRegistry Game 层，Entities 层不向上依赖，PLAN §2）。非幼崽 / 非可繁殖
//   mob / dead / 越界 → 返 false（caller 不消耗食物）。不查 breedCooldown（幼崽无繁殖冷却；喂食只加速成长，同 MC
//   与 enterLoveMode 的冷却守卫无关）。growTimer clamp 到 0（到 0 → 下 tick tickBreeding 自然长大，无需本方法翻
//   baby —— 延迟 ≤1 帧，观感无差）。bump revision + emit → 喂食是状态变更，通知纪律同 enterLoveMode / shearSheep。
bool EntityManager::feedBaby(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive || e.dead) return false;   // 仅活体 mob 可喂
    if (!isBreedableType(e.mobType)) return false;            // 仅 pig/cow/sheep/chicken/wolf/ocelot 可繁殖
    if (e.mobType == MobWolf && !e.wolfTamed) return false;   // t480：仅**驯服狼**幼崽可喂（野狼幼崽不驯）
    if (e.mobType == MobOcelot && !e.ocelotTamed) return false; // t481：仅**驯服猫**幼崽可喂（野豹猫幼崽不驯）
    if (!e.baby) return false;                                // 非幼崽 → 走成体求偶路径（enterLoveMode）
    e.growTimer -= kBabyFeedGrow;
    if (e.growTimer < 0.0f) e.growTimer = 0.0f;               // clamp 0（防负成长；到 0 即下 tick 长大）
    qCInfo(lcEnt) << "baby fed at slot" << i << "type" << e.mobType
                  << "-> growTimer reduced to" << e.growTimer << "s left";
    notifyEntitiesChanged();
    return true; // caller 据返值消耗 1 食物（生存）
}

// t1025 繁殖计时测试缝（见 .h 注释）：写运行期冷却 / 成长秒数（负值 clamp 0）。产品路径零调用（缺省 =
//   MC 口径常量 kBreedCooldown=300 / kBabyGrowTime=1200）；探针缝调短跑「冷却门 / 成长到点」行为腿。
//   不 bump revision / 不 emit：两值只被「此后新设的计时器」读取，不直接呈现（同 m_chickenJockeyChance
//   缝不通知先例）。
void EntityManager::setBreedTimings(float breedCooldownSec, float babyGrowSec)
{
    m_breedCooldownSec = std::max(0.0f, breedCooldownSec);
    m_babyGrowSec      = std::max(0.0f, babyGrowSec);
    qCInfo(lcEnt) << "breed timings set: cooldown" << m_breedCooldownSec << "s / baby grow" << m_babyGrowSec << "s";
}

// t1025 食物引诱门控写入（见 .h 注释）：mobType 越界静默 no-op（防御；m_foodLure 容量 = 枚举尾 +1）。
//   不 bump revision / 不 emit：门控是 AI 输入（下一 AI tick 生效），无直接呈现面。
void EntityManager::setFoodLure(int mobType, bool active)
{
    if (mobType < 0 || mobType >= kMobTypeCount) return;
    m_foodLure[mobType] = active;
}

// t1025 引诱门控读取（探针断言面 + AI 引诱段同源）。越界 → false。
bool EntityManager::foodLureAt(int mobType) const
{
    if (mobType < 0 || mobType >= kMobTypeCount) return false;
    return m_foodLure[mobType];
}

// t1025 幼崽成长剩余秒数（探针断言面）。非 mob / 越界 → 0。
float EntityManager::growTimerAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob) return 0.0f;
    return e.growTimer;
}

// t1025 繁殖冷却剩余秒数（探针断言面）。非 mob / 越界 → 0。
float EntityManager::breedCooldownAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob) return 0.0f;
    return e.breedCooldown;
}

// t480 第 i 只 mob 是否已驯服狼（wolfTamed=true）。仅 MobWolf 用；其余 mob 恒 false。越界 → false。
bool EntityManager::wolfTamedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobWolf) return false; // 仅 wolf 有驯服态
    return e.wolfTamed;
}

// t480 第 i 只驯服狼是否坐着（wolfSitting=true；留守）。仅驯服狼用（未驯服 / 非 wolf 恒 false）。越界 → false。
bool EntityManager::wolfSittingAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobWolf || !e.wolfTamed) return false; // 仅驯服狼有坐态
    return e.wolfSitting;
}

// t480 骨头驯服（spec「右键概率驯服 ~33%」；机制等价 MC 1.0 狼 33% 驯服概率 + 失败骨头仍消耗）。
//   未驯服活体狼 → ~kWolfTameChance 概率驯服（wolfTamed=true + 清野狼追踪态 chasing 残留 → aiWolf
//   转跟随/防御态）+ bump revision（QML 切狼外观 / 行为态 + 爱心沿）+ 返 true；未中 → 返 false（caller
//   照常消耗骨头；**失败反馈登记简化**：仅日志，MC 失败冒烟粒子不做——探针只钉状态面，见 dev-plan t1031）。
//   已驯服 / 非 wolf / dead / 越界 → 返 false（caller 不消耗）。Q_INVOKABLE 兼调试 + PlayerController 骨头
//   分支双入口。t1031 测试缝：m_tameRollOverride >= 0 时驯服样本取缝值千分比（(roll%1000)/1000.0），不
//   消费全局 RNG——P-t1031 钉必成(0)/必败(999)两端；缺省 -1 = 照常 QRandomGenerator::global 掷骰（生产
//   路径零调用即零改动，同 m_wanderFrozen 缝先例）。
bool EntityManager::tameWolf(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobWolf) return false; // 仅 wolf 可驯
    if (e.dead || !e.alive) return false;                    // 尸体 / 空槽不可驯
    if (e.wolfTamed) return false;                           // 已驯服 → 不重复（caller 不消耗骨头）
    const double roll = m_tameRollOverride >= 0
        ? double(m_tameRollOverride % 1000) / 1000.0 // t1031 缝接管（同值恒同果，探针确定性）
        : QRandomGenerator::global()->generateDouble();
    if (roll >= double(kWolfTameChance)) {
        qCInfo(lcEnt) << "tame attempt failed (slot" << i << ") - bone consumed, wolf stays wild";
        return false; // ~67% 失败（骨头仍消耗，机制等价 MC 喂骨无论成败都耗）
    }
    e.wolfTamed = true;
    e.chasing = false;     // 清野狼敌对追踪残留（驯服即停攻玩家，防下帧 aiWolf 仍追咬）
    e.chaseTimer = 0.0f;
    e.attackCooldown = 0.0f; // 清咬击冷却（驯服后无攻击语义残留）
    e.tameHeartTimer = kTameHeartDuration; // t831 驯服成功爱心（QML 心形经 inLoveAt 显；tickBreeding 衰减）
    qCInfo(lcEnt) << "wolf tamed at slot" << i << "pos" << e.pos;
    notifyEntitiesChanged(); // bump → QML 据 wolfTamedAt 切狼行为态
    return true; // caller 据返值消耗 1 骨头（生存）
}

// t480 坐/站切换（spec「驯服狼右键坐 → 再右键站」；机制等价 MC 1.0 驯服狼右键坐/站命令）。
//   已驯服活体狼 → 翻转 wolfSitting + bump revision（QML 切坐姿 / 站姿；aiWolf 切留守 / 跟随）。
//   未驯服 / 非 wolf / dead / 越界 → 静默 no-op（野狼右键无反应）。Q_INVOKABLE 兼调试 + PlayerController 骨头分支双入口。
void EntityManager::toggleWolfSit(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobWolf) return; // 仅 wolf 可命令坐/站
    if (e.dead || !e.alive) return;                    // 尸体 / 空槽不可命令
    if (!e.wolfTamed) return;                          // 未驯服 → 右键无反应（机制等价 MC 野狼不可命令）
    e.wolfSitting = !e.wolfSitting;
    qCInfo(lcEnt) << "wolf slot" << i << (e.wolfSitting ? "sitting (stay)" : "standing (follow)");
    notifyEntitiesChanged(); // bump → QML 据 wolfSittingAt 切坐姿/站姿
}

// t923 仇恨传递：敌对攻击驯服狼 → 狼群反击攻击者（m_wolfTarget 共享防御目标；调自 骷髅箭命中狼 /
//   燃烬者火球命中狼 两处 mob→mob 伤害点）。见头文件注释（豹猫不护主 = MC 1.0 猫不攻击怪物，钉死）。
void EntityManager::wolfRetaliateAgainst(int victimIdx, int attackerIdx)
{
    if (victimIdx < 0 || attackerIdx < 0 || victimIdx == attackerIdx) return;
    if (victimIdx >= int(m_entities.size()) || attackerIdx >= int(m_entities.size())) return;
    const Entity &v = m_entities[size_t(victimIdx)];
    if (v.kind != Mob || v.mobType != MobWolf || !v.wolfTamed || !v.alive || v.dead) return; // 仅活体驯服狼
    const Entity &a = m_entities[size_t(attackerIdx)];
    if (!a.alive || a.kind != Mob || a.dead) return; // 攻击者须活体 mob（爆炸者已自毁 → caller 不调）
    if (m_wolfTarget == attackerIdx) return; // 已在反击它（防重复日志刷屏）
    m_wolfTarget = attackerIdx;
    qCInfo(lcEnt) << "tamed wolf" << victimIdx << "harmed by mob" << attackerIdx
                  << "- pack retaliates (MC 1.0 wolf defend-self)";
}

// t948 敌对 mob 仇恨注册**单一入口**（狼主动咬击命中处调；机制等价 MC 1.0 revenge target——生物被活体
//   mob 打伤 → 仇恨目标记为攻击者）。与 t923 wolfRetaliateAgainst（狼被打 → 狼群反击）互补：狼**主动
//   攻击**面也要注册被咬者对狼的仇恨（t923 反击注册面核），敌对被咬后下个 AI tick 起转火追咬 / 转火
//   射击本狼（消费侧 resolveAggroTarget，aiHostile / aiArcher 入口顶部）。受害者门：仅带仇恨 AI 的敌对型
//   {Shambler / Spider / Silverfish（同落 aiHostile else 分发面）/ Bones / BabyShambler（t952 幼体，
//   review0830 #13 补门——幼体同走 aiHostile 三条追击路径）}——Stalker（spec 只锁玩家不对
//   生物自爆，t712 同界）/ Nightwalker、Emberling（独立 AI）/ 被动七型（无仇恨系统，逃跑链不受影响）
//   → 静默 no-op；攻击者门：活体 mob。slot+serial 双快照（同骷髅箭 arrowShooter+serial 槽复用防线）：
//   攻击者槽复用换任后 serial 不匹配 → 消费侧判空落回玩家路径。重复注册覆盖旧仇恨（最新攻击者优先）。
//   review0830 #13 长期方向（登记）：此枚举门宜改「是否具备仇恨 AI 能力」判定（mobType → 走
//   aiHostile/aiArcher 的能力表单一权威），新敌对类型免再随类型清单漂移（#26 同族教训）。
void EntityManager::mobAggroAgainst(int victimIdx, int attackerIdx)
{
    if (victimIdx < 0 || attackerIdx < 0 || victimIdx == attackerIdx) return;
    if (victimIdx >= int(m_entities.size()) || attackerIdx >= int(m_entities.size())) return;
    Entity &v = m_entities[size_t(victimIdx)];
    if (!v.alive || v.kind != Mob || v.dead) return; // 尸体 / 空槽不注册（咬击致死同帧已是 dead → no-op）
    switch (v.mobType) {
    case MobShambler: case MobSpider: case MobCaveSpider: case MobSilverfish: case MobBones:
    case MobBabyShambler: break; // 仇恨 AI 消费面（t952 幼体扩段，review0830 #13；t1012③ 洞穴蜘蛛同走 aiHostile 面入列）
    default: return; // Stalker / Nightwalker / Emberling / 被动型无此系统（入口门，见上）
    }
    const Entity &a = m_entities[size_t(attackerIdx)];
    if (!a.alive || a.kind != Mob || a.dead) return; // 攻击者须活体 mob（同 wolfRetaliateAgainst 门）
    v.aggroIdx = attackerIdx;
    v.aggroSerial = a.spawnSerial;
    qCInfo(lcEnt) << "hostile mob" << victimIdx << "aggroed on mob" << attackerIdx
                  << "- revenge target set (MC 1.0 attacked-by-wolf)";
}

// t948 敌对 mob 仇恨目标消费侧校验（aiHostile / aiArcher 入口顶部调；单一消费点）。见头文件注释：
//   死亡 / 非 mob / 槽复用（spawnSerial ≠ 快照）→ 清 -1/0 返 -1（caller 落回常规目标路径）；仇恨不设
//   时限不设距离（MC revenge target 持续到目标死亡），出侦测范围由 caller 分支门控（驯服狼瞬移回主人
//   身边 → 本 tick 落回玩家路径，仇恨保留；狼回侦测范围即恢复追狼）。无仇恨 → -1。
int EntityManager::resolveAggroTarget(Entity &e)
{
    if (e.aggroIdx < 0) return -1;
    if (e.aggroIdx >= int(m_entities.size())) {
        e.aggroIdx = -1;
        e.aggroSerial = 0;
        return -1;
    }
    const Entity &t = m_entities[size_t(e.aggroIdx)];
    if (!t.alive || t.kind != Mob || t.dead || t.spawnSerial != e.aggroSerial) {
        e.aggroIdx = -1;
        e.aggroSerial = 0; // 攻击者死亡 / 槽复用换任 → 回落（同 m_wolfTarget 死亡回落先例，t480）
        return -1;
    }
    return e.aggroIdx;
}

// t481 第 i 只 mob 是否已驯服猫（ocelotTamed=true）。仅 MobOcelot 用；其余 mob 恒 false。越界 → false。
bool EntityManager::ocelotTamedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobOcelot) return false; // 仅 ocelot 有驯服态
    return e.ocelotTamed;
}

// t481 第 i 只驯服猫是否坐着（ocelotSitting=true；留守）。仅驯服猫用（未驯服 / 非 ocelot 恒 false）。越界 → false。
bool EntityManager::ocelotSittingAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobOcelot || !e.ocelotTamed) return false; // 仅驯服猫有坐态
    return e.ocelotSitting;
}

// t481 第 i 只驯服猫的毛色变体（0..2）。仅 MobOcelot 用；未驯服 / 非 ocelot → 0（走豹猫贴图不读变体）。越界 → 0。
int EntityManager::ocelotVariantAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobOcelot) return 0; // 仅 ocelot 有毛色变体
    return e.ocelotVariant;
}

// t481 生鱼驯服（spec「生鱼驯服 → 变猫（3 毛色变体随机）」；机制等价 MC 1.0 豹猫生鱼驯服 ~1/3）。
//   未驯服活体豹猫 → ~kOcelotTameChance 概率驯服（ocelotTamed=true + 随机毛色变体 0..2 + 清敌对追踪残留）+
//   bump revision（QML 收豹猫外观、转猫外观 + 跟随态）+ 返 true；未中（~2/3）→ 返 false（**生鱼仍消耗**，
//   机制等价 MC 喂鱼无论成败都耗）。已驯服 / 非 ocelot / dead / 越界 → 返 false（caller 不消耗生鱼）。
//   Q_INVOKABLE 兼调试 + PlayerController 生鱼分支双入口。
bool EntityManager::tameOcelot(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobOcelot) return false; // 仅 ocelot 可驯
    if (e.dead || !e.alive) return false;                      // 尸体 / 空槽不可驯
    if (e.ocelotTamed) return false;                           // 已驯服 → 不重复（caller 不消耗生鱼）
    if (QRandomGenerator::global()->generateDouble() >= double(kOcelotTameChance)) {
        qCInfo(lcEnt) << "ocelot tame attempt failed (slot" << i << ") - raw fish consumed, ocelot stays wild";
        return false; // ~2/3 失败（生鱼仍消耗，机制等价 MC 喂鱼无论成败都耗）
    }
    e.ocelotTamed = true;
    e.ocelotVariant = int(QRandomGenerator::global()->bounded(3)); // 随机毛色变体 0..2（棕虎斑 / 姜黄 / 奶油；t963 前变体 0 为全黑档）
    e.chasing = false;     // 清野豹猫残留追踪态（驯服即转跟随，防下帧误走敌对分支）
    e.chaseTimer = 0.0f;
    e.tameHeartTimer = kTameHeartDuration; // t831 驯服成功爱心（QML 心形经 inLoveAt 显；tickBreeding 衰减）
    qCInfo(lcEnt) << "ocelot tamed at slot" << i << "pos" << e.pos << "variant" << e.ocelotVariant;
    notifyEntitiesChanged(); // bump → QML 据 ocelotTamedAt 切猫外观 / 跟随态
    return true; // caller 据返值消耗 1 生鱼（生存）
}

// t481 坐/站切换（spec「驯服猫坐/站（同狼模式）」；机制等价 MC 1.0 驯服猫右键坐/站）。
//   已驯服活体猫 → 翻转 ocelotSitting + bump revision（QML 切坐姿 / 站姿；aiOcelot 切留守 / 跟随）。
//   未驯服 / 非 ocelot / dead / 越界 → 静默 no-op（野豹猫右键无反应）。Q_INVOKABLE 兼调试 + PlayerController 空手分支双入口。
void EntityManager::toggleOcelotSit(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobOcelot) return; // 仅 ocelot 可命令坐/站
    if (e.dead || !e.alive) return;                      // 尸体 / 空槽不可命令
    if (!e.ocelotTamed) return;                          // 未驯服 → 右键无反应（机制等价 MC 野豹猫不可命令）
    e.ocelotSitting = !e.ocelotSitting;
    qCInfo(lcEnt) << "ocelot slot" << i << (e.ocelotSitting ? "sitting (stay)" : "standing (follow)");
    notifyEntitiesChanged(); // bump → QML 据 ocelotSittingAt 切坐姿/站姿
}

// t400 第 i 个 mob 是否处于求偶期（loveTimer>0）。QML delegate 据它显心形 Model（繁殖可观察反馈）。
bool EntityManager::inLoveAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive) return false;
    return e.loveTimer > 0.0f || e.tameHeartTimer > 0.0f; // t831：求偶或驯服爱心期均显心（呈现合一）
}

// t400 第 i 个 mob 的模型缩放（幼崽 kBabyScale=0.5 / 成体 1.0）。QML delegate Node scale 绑它。
float EntityManager::babyScaleAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 1.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive) return 1.0f;
    return e.baby ? kBabyScale : 1.0f;
}

// t479 第 i 个 mob 是否幼崽（baby=true）。PlayerController 喂食分流（幼崽 → feedBaby 加速成长 / 成体 →
//   enterLoveMode 求偶）+ 呈现层守卫读。非 Mob / 越界 → false。
bool EntityManager::isBabyAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive) return false;
    return e.baby;
}

// t400 当前可繁殖被动 mob 数（pig/cow/sheep/chicken 成体 + 幼崽 + t480 驯服狼 + t481 驯服猫，alive 且非 dead）。供繁殖上限判定。
//   t480：未驯服狼不计入（野狼不可繁殖，参与上限会虚占 kPassiveMobCap 名额 → 人为压低 farm 种群上限）。
//   t481：未驯服豹猫同理不计入（野豹猫不可繁殖）。
int EntityManager::passiveBreedableCount() const
{
    int n = 0;
    for (const Entity &e : m_entities) {
        if (!e.alive || e.dead || e.kind != Mob) continue;
        if (!isBreedableType(e.mobType)) continue;
        if (e.mobType == MobWolf && !e.wolfTamed) continue; // 仅驯服狼计入（野狼不可繁殖）
        if (e.mobType == MobOcelot && !e.ocelotTamed) continue; // t481 仅驯服猫计入（野豹猫不可繁殖）
        ++n;
    }
    return n;
}

// t400 最近求偶配偶查找：返最近一只 alive && !dead && !baby && loveTimer>0 && mobType==e.mobType 的 mob 索引
//   （排除 self）；无 → -1。供求偶者设 yaw 朝配偶 → aiWander 行走相遇。O(n) 每 mob 每帧。
int EntityManager::findNearestMate(int idx) const
{
    if (idx < 0 || idx >= int(m_entities.size())) return -1;
    const Entity &self = m_entities[size_t(idx)];
    int best = -1;
    float bestDistSq = 0.0f;
    for (int j = 0; j < int(m_entities.size()); ++j) {
        if (j == idx) continue;
        const Entity &m = m_entities[size_t(j)];
        if (!m.alive || m.dead || m.kind != Mob) continue;
        if (m.baby || m.loveTimer <= 0.0f) continue;     // 仅同样在求偶的成体算配偶
        if (m.mobType != self.mobType) continue;          // 同种
        const float dx = m.pos.x() - self.pos.x();
        const float dz = m.pos.z() - self.pos.z();
        const float d2 = dx * dx + dz * dz;
        if (best < 0 || d2 < bestDistSq) { best = j; bestDistSq = d2; }
    }
    return best;
}

// t1025 最近成年同种查找（见 .h 注释）：幼崽跟随 AI 调。门 = alive && !dead && kind==Mob && 同种 && !baby
//   （成年）；XZ 距 ≤ kBabyFollowRange（MC 幼畜认亲感知量级，超半径不跨图认亲）。O(n) 每 mob 每帧。
int EntityManager::findNearestAdultSameType(int idx) const
{
    if (idx < 0 || idx >= int(m_entities.size())) return -1;
    const Entity &self = m_entities[size_t(idx)];
    int best = -1;
    float bestDistSq = kBabyFollowRange * kBabyFollowRange;
    for (int j = 0; j < int(m_entities.size()); ++j) {
        if (j == idx) continue;
        const Entity &m = m_entities[size_t(j)];
        if (!m.alive || m.dead || m.kind != Mob) continue;
        if (m.baby || m.mobType != self.mobType) continue; // 同种**成年**（幼崽不互认，机制等价 MC 跟随成体）
        const float dx = m.pos.x() - self.pos.x();
        const float dz = m.pos.z() - self.pos.z();
        const float d2 = dx * dx + dz * dz;
        if (d2 <= bestDistSq) { best = j; bestDistSq = d2; }
    }
    return best;
}

// t481 最近豹猫/猫查找（aiStalker 驱赶调）：返距 pos 在 range 内最近一只 alive && !dead && kind==Mob &&
//   mobType==MobOcelot 的 mob 索引；无 → -1。XZ 距离（Stalker 逃离是水平行为，Y 不参与）。O(n) 每 Stalker
//   每 AI tick，n≤kCap=64 可忽略。const 只读自身数据。
int EntityManager::nearestOcelot(const QVector3D &pos, float range) const
{
    const float r2 = range * range;
    int best = -1;
    float bestDistSq = 0.0f;
    for (int j = 0; j < int(m_entities.size()); ++j) {
        const Entity &m = m_entities[size_t(j)];
        if (!m.alive || m.dead || m.kind != Mob) continue;
        if (m.mobType != MobOcelot) continue;
        const float dx = m.pos.x() - pos.x();
        const float dz = m.pos.z() - pos.z();
        const float d2 = dx * dx + dz * dz;
        if (d2 > r2) continue;
        if (best < 0 || d2 < bestDistSq) { best = j; bestDistSq = d2; }
    }
    return best;
}

// t400 繁殖 tick（tick 末尾调；spec「同种 2 只喂对应食物 → 生幼崽；种群上限」；机制对齐 MC 1.0 breeding）。
//   (1) 衰减 loveTimer / breedCooldown / 幼崽 growTimer（growTimer 到 0 → baby=false 长大成体）。
//   (2) 求偶期成体配对：同种双方均求偶 + XZ 中心距 ≤ kBreedRange → 产 1 幼崽（spawnMobCore 标 baby + growTimer）
//       + 双方进 kBreedCooldown 冷却 + 退求偶（loveTimer=0）。受 kPassiveMobCap 钳制（达上限不再产）。
//   返是否变更（驱动 tick 末尾 dirty + bump revision + emit）。配对扫描 O(n²)，n≤kCap=64 可忽略。
//   幼崽生成走 acquireSlot（可能 push_back）—— 主实体循环持 Entity& 引用期间不可 push_back（致引用失效），
//   故本段在主循环之外（tick 末尾）做，且配对阶段仅记 pending、统一在末段 spawn（防引用失效 + 批量收口 emit）。
bool EntityManager::tickBreeding(qreal dt)
{
    if (m_entities.empty()) return false;
    bool dirty = false;
    // (1) 衰减所有 mob 的求偶 / 冷却 / 幼崽长大计时。
    for (Entity &e : m_entities) {
        if (!e.alive || e.kind != Mob) continue;
        if (e.loveTimer > 0.0f) {
            e.loveTimer -= float(dt);
            if (e.loveTimer <= 0.0f) { e.loveTimer = 0.0f; dirty = true; } // 退求偶 → 收心（QML 隐心）
        }
        // t831 驯服爱心衰减（tameWolf / tameOcelot 成功置 kTameHeartDuration；与求偶 loveTimer 分离——仅呈现态，
        //   不触发寻偶 AI / 配对）。归零收心（inLoveAt 转 false → QML 隐心形）。
        if (e.tameHeartTimer > 0.0f) {
            e.tameHeartTimer -= float(dt);
            if (e.tameHeartTimer <= 0.0f) { e.tameHeartTimer = 0.0f; dirty = true; } // 爱心到期 → 收心
        }
        if (e.breedCooldown > 0.0f) {
            e.breedCooldown -= float(dt);
            if (e.breedCooldown < 0.0f) e.breedCooldown = 0.0f;
        }
        if (e.baby) {
            e.growTimer -= float(dt);
                if (e.growTimer <= 0.0f) {
                    e.growTimer = 0.0f;
                    e.baby = false; // 长大成体（QML babyScaleAt 1.0 → 重缩回正常体型）
                    // t1025 成长还原（幼崽缩放基建的逆操作）：按类型还原成体碰撞盒（applyMobCollisionBox 单一
                    //   权威）+ pos.y 重锚（盒底恒贴地 —— halfH 变高 Δ，collision 中心同步抬 Δ，防还原盒嵌入
                    //   地面卡住移动）。t1046 血量还原面退役（parity 台账低-1，用户裁决「一切按原版」）：幼崽
                    //   血量=成体上限满血（MC Baby 血量口径），本就 10/10 长大无 maxHealth/health 可还原——
                    //   t1025 旧「×2 还原」是产崽减半的逆，减半已随低-1 清偿一并移除，此处不再触血量。
                    const float babyHalfH = e.halfH;
                    applyMobCollisionBox(e.mobType, e);
                    e.pos.setY(e.pos.y() + (e.halfH - babyHalfH));
                dirty = true;
                qCInfo(lcEnt) << "baby grew up at pos" << e.pos << "type" << e.mobType;
            }
        }
    }
    // (2) 配对：求偶期成体同种相遇 → 产幼崽。先算「本帧还可产几只」= 上限 − 当前可繁殖数（防超 cap）。
    //   配对阶段仅 reset 父母 loveTimer / 设冷却 + 记 pending 幼崽；spawn 推迟到末段（批量 + 避引用失效）。
    //   t480：PendingBaby 带 tamed —— 狼幼崽继承父代驯服态（配对仅驯服狼进求偶 → 恒 tamed=true；保留字段
    //   传递语义，未来若野狼可配对则各按父代）。
    //   t481：PendingBaby 带 variant —— 猫幼崽继承父代毛色变体（配对仅驯服猫进求偶 → 恒 tamed=true；
    //   variant 取 e.ocelotVariant = 配对循环首个父母的变体，机制等价 MC 幼猫继承其一父母毛色）。
    //   t789：PendingBaby 带 wool —— 羊幼崽继承父代羊毛色（自然色权重只管 spawn；繁殖链色稳定，机制等价
    //   MC 幼畜继承父母毛色——本工程取首个父母色，同 ocelotVariant 继承先例，不做混色）。
    struct PendingBaby { float x, y, z; int mobType; QString color; bool tamed; int variant = 0; int wool = 0; };
    std::vector<PendingBaby> pending;
    int remaining = kPassiveMobCap - passiveBreedableCount();
    const float rangeSq = kBreedRange * kBreedRange;
    const int n = int(m_entities.size());
    for (int idx = 0; idx < n && remaining > 0; ++idx) {
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive || e.dead || e.kind != Mob) continue;
        if (!isBreedableType(e.mobType) || e.baby) continue;
        if (e.loveTimer <= 0.0f || e.breedCooldown > 0.0f) continue; // 须在求偶且非冷却
        // 找最近的同种求偶配偶（j>idx 避免重复配对：idx 配 j 后 j 的 loveTimer 归 0，后续到 j 自动跳过）。
        for (int j = idx + 1; j < n; ++j) {
            Entity &m = m_entities[size_t(j)];
            if (!m.alive || m.dead || m.kind != Mob) continue;
            if (m.mobType != e.mobType || m.baby) continue;
            if (m.loveTimer <= 0.0f || m.breedCooldown > 0.0f) continue;
            const float dx = m.pos.x() - e.pos.x();
            const float dz = m.pos.z() - e.pos.z();
            const float dy = m.pos.y() - e.pos.y();
            if (dx * dx + dz * dz > rangeSq) continue;        // XZ 中心距超 KBreedRange → 未相遇
            if (std::abs(dy) > 2.0f) continue;                 // 垂直跨层不算（防跨地板盲配）
            // 配对成功：双方退求偶 + 进冷却（t1025：运行期值 m_breedCooldownSec，缺省 = MC 5 min 口径）；
            //   幼崽生在双方中点（地表上方，重力 tick 贴地）。
            e.loveTimer = 0.0f; e.breedCooldown = m_breedCooldownSec;
            m.loveTimer = 0.0f; m.breedCooldown = m_breedCooldownSec;
            const float bx = (e.pos.x() + m.pos.x()) * 0.5f;
            const float by = std::min(e.pos.y(), m.pos.y());
            const float bz = (e.pos.z() + m.pos.z()) * 0.5f;
            pending.push_back({ bx, by, bz, e.mobType, e.color, e.wolfTamed, e.ocelotVariant, e.sheepWool });
            --remaining;
            dirty = true;
            qCInfo(lcEnt) << "breed pair: slots" << idx << "&" << j << "type" << e.mobType
                          << "-> baby pending at" << bx << by << bz
                          << "(passive count toward cap" << (kPassiveMobCap - remaining) << "/" << kPassiveMobCap << ")";
            break; // e 已配对，处理下一个 idx
        }
    }
    // 末段 spawn 幼崽（acquireSlot 可能 push_back —— 此时已脱离主循环的 Entity& 引用，安全）。
    //   一次循环 spawn 多只，tick 末尾统一 bump revision + emit 一次（批量收口，避免 N 只幼崽 N 次 notify 风暴）。
    for (const PendingBaby &b : pending) {
        // spawnMobCore 不 emit；幼崽色继承父代 color（pig/cow/sheep 走 MobModel 不读 color，占位串无妨）。
        const int slot = spawnMobCore(int(std::floor(b.x)), int(std::floor(b.y)), int(std::floor(b.z)),
                                      b.mobType, b.color, 0);
        if (slot >= 0) {
            Entity &baby = m_entities[size_t(slot)];
            baby.baby = true;            // 标幼崽（QML babyScaleAt → 0.5 缩小）
            baby.growTimer = m_babyGrowSec; // 长大倒计时（t1025：运行期值，缺省 = MC 20 min 口径）
            // t1025 幼崽缩放基建对齐（t952 小蹒跚者同款「物理盒即缩放」机制，非 t400 旧版仅 QML 视觉缩）：
            //   碰撞盒 ×kBabyScale（halfW/halfH 同倍）。t1046 血量口径改写（parity 台账低-1，用户裁决
            //   「一切按原版」）：MC Baby 血量 = 成体上限满血（非减半）——maxHealth/health 保持
            //   spawnMobCore 的满血语义原样（10/10），旧「maxHealth/2」减半与长大「×2 还原」双双退役；
            //   pos.y 贴 collision 底面重锚（spawnMobCore 按成体盒 pos.y = 格底 + halfH；缩盒后改
            //   格底 + 幼体 halfH，免首帧嵌地）。
            const float adultHalfH = baby.halfH;
            baby.halfW *= kBabyScale;
            baby.halfH *= kBabyScale;
            baby.health = baby.maxHealth;
            baby.pos.setY(baby.pos.y() - (adultHalfH - baby.halfH));
            if (b.mobType == MobWolf)
                baby.wolfTamed = b.tamed; // t480：狼幼崽继承父代驯服态（配对仅驯服狼 → 恒 true；驯服幼崽跟随主人）
            if (b.mobType == MobOcelot) {
                // t481：猫幼崽继承父代驯服态 + 毛色变体（配对仅驯服猫 → 恒 tamed=true；变体随父代，QML 据
                //   ocelotVariantAt 选 3 色猫贴图 → 幼猫毛色与父母一致，机制等价 MC 幼猫继承父母毛色）。
                baby.ocelotTamed = b.tamed;
                baby.ocelotVariant = b.variant;
            }
            if (b.mobType == MobSheep) {
                // t789：羊幼崽继承父代羊毛色（覆写 spawnMobCore 的自然随机色——繁殖产色不走权重表，
                //   机制等价 MC 幼畜毛色随父母；幼崽剪/杀掉对应色与成体同链）。
                baby.sheepWool = b.wool;
            }
        }
    }
    return dirty;
}

// t239 mob 当前血量（呈现层心条 / 攻击反馈）。非 Mob / 越界 → 0。
int EntityManager::healthAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].health;
}

// t239 mob 血量上限。非 Mob / 越界 → 0。
int EntityManager::maxHealthAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].maxHealth;
}

// t239 mob 死亡态（QML 播死亡动画 / 心条清空）。非 Mob / 越界 → false。
bool EntityManager::deadAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].dead;
}

// t239 mob 受击红闪剩余比 0..1（= hurtFlash / kHurtFlashTime；>0 → QML baseColor 红）。非 Mob / 越界 → 0。
float EntityManager::hurtFlashAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.hurtFlash <= 0.0f) return 0.0f;
    return e.hurtFlash / kHurtFlashTime;
}

// t239 受击（Q_INVOKABLE 兼调试 + t242 攻击路径双入口）：第 i 个 mob 受 amount 伤害。
//   - clamp health 到 [0, maxHealth]；hurtFlash = kHurtFlashTime（QML 红闪）。
//   - health≤0 且未 dead → dead=true + deathTimer=kDeathTime（冻结 AI/重力；**不**立即掉落）。
//     t449：mobDied（→ 掉落物）改在 tick 死亡态 deathTimer 归零时 emit（倒地动画播完才掉落），本处仅
//     快照 deathBurned（致死时刻 fireTimer>0）供延迟 emit 携带。dead 期间冻结 AI / 重力 / 攻击。
//   - dead / 非 Mob / 越界 / amount≤0 → 静默早退。
//   bump revision → 驱动 QML health/红闪/死亡绑定刷新（QML 据 deadAt 进入侧倒动画 + 白烟）。
void EntityManager::damageEntity(int i, int amount)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead || amount <= 0) return; // t256：空槽防御（caller 已过滤）

    e.health -= amount;
    if (e.health < 0) e.health = 0;
    e.hurtFlash = kHurtFlashTime;

    if (e.health <= 0) {
        // 死亡：冻结 AI / 重力（dead=true → tick 跳过 aiWander / 重力 / 敌对攻击，仅 deathTimer 倒计时）+
        //   给 QML 播死亡动画窗口（kDeathTime ≈ 500ms：侧倒旋转 + 白烟消散）。**mobDied 延迟到 deathTimer
        //   归零才 emit**（见 tick 死亡态分支）—— 机制等价 MC「血归零 → 倒地动画 → 掉落物」三段过渡，
        //   旧实现「红闪 + 掉落物同帧」太急（t449 修）。
        e.dead = true;
        e.deathTimer = kDeathTime;
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        // review27 #12：死亡翻转即归零行走相位（死亡态在 tick 主循环 continue 早退，恒不达下方 t897 ②
        //   归零块——不在此清则行走中被击杀的 mob 尸体腿冻结半步相位）。stepAccum 同清（防残留半步
        //   累加在（不可能的）复活瞬发鬼脚步；登乘写链同款）。
        e.walkPhase = 0.0f;
        e.stepAccum = 0.0f;
        // t344 burned = 致死时刻是否处于火烧态（fireTimer>0）：着火死亡掉熟肉（被动动物）。仅 fireTimer
        //   触发（日光 burning 仅敌对、不掉肉故不参与 cooked 判定）。t449 快照进 deathBurned 供延迟 emit 携带
        //   （dead 态 fireTimer 冻结，故与 expiry 复算等价；快照更稳）。
        e.deathBurned = e.fireTimer > 0.0f;
        // t479 幼崽死亡快照：致死瞬间 e.baby（同 deathBurned 快照模式）—— mobDied 延迟到 deathTimer 归零才 emit，
        //   期间 tickBreeding 仍衰减 growTimer（dead 态不冻结），幼崽可能在 0.5s 窗口内长大 → 延迟读 e.baby 会漏判。
        e.deathBaby = e.baby;
        // review #32 剪毛死亡快照：致死瞬间 e.sheared（同 deathBaby 模式）—— 呈现层据 mobDied 第 8 参跳过
        //   剪毛羊的羊毛掉落（机制等价 MC 1.0 剪过毛的羊死时无毛可掉；t300 注释曾声称此语义但信号未携带）。
        e.deathSheared = e.sheared;
        const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
        qCInfo(lcEnt) << "mob" << i << "type" << e.mobType << "entering death at" << dx << dy << dz
                      << (e.deathBurned ? "(burned)" : "")
                      << "-> drops deferred" << kDeathTime << "s (t449 side-fall anim)";
    } else {
        qCInfo(lcEnt) << "mob" << i << "took" << amount << "dmg, health=" << e.health << "/" << e.maxHealth;
    }

    notifyEntitiesChanged();
}

// t242 攻击射线 vs mob AABB 命中测试（spec「玩家左键攻击生物」前置：选体）。slab-based ray-AABB
//   对每个活体 mob 的 AABB（pos ± radius 的 1×1×1 立方）求交，取最近命中。dir 须归一（caller
//   PlayerController::lookDirection 已归一）。跳过 dead（尸体不可打，防鞭尸重复扣血 / 触发多次掉落）
//   与非 Mob（掉落物 / 下落方块不属攻击目标）。无命中 → -1。
//   分层（PLAN §2）：纯只读自身数据（pos / radius / kind / dead）的几何测试，无向下依赖。
int EntityManager::findMobHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const
{
    int bestIdx = -1;
    float bestDist = maxDist;
    // dir 退化（零向量）→ 无方向，无命中。NaN/Inf 防御同此分支。
    if (!std::isfinite(dir.x()) || !std::isfinite(dir.y()) || !std::isfinite(dir.z())) return -1;
    const float dirLen2 = dir.x()*dir.x() + dir.y()*dir.y() + dir.z()*dir.z();
    if (dirLen2 < 1e-8f) return -1;
    for (size_t i = 0; i < m_entities.size(); ++i) {
        const Entity &e = m_entities[i];
        if (!e.alive || e.kind != Mob || e.dead) continue; // t256：跳过空槽（slot-reuse 残留位）
        // Slab 法 ray-AABB：对每轴 t1 = (min - origin) / dir、t2 = (max - origin) / dir；tmin = max(per-axis near)、
        //   tmax = min(per-axis far)；命中 ⟺ tmax >= tmin && tmax >= 0 && tmin <= maxDist。
        //   dir 分量近 0 时该轴 slab 退化为「整轴在内」约束（origin ∈ [min,max] → -inf..+inf，否则永不命中）。
        //   t252：AABB 非立方 —— X/Z 用 halfW、Y 用 halfH（cow 0.40×0.50×0.40；旧版单一 r 致 hitbox 选体
        //   与实际碰撞箱不符：打牛头 / 牛背高处按 1×1 误判命中、实际碰撞箱更高）。
        const float ext[3] = { e.halfW, e.halfH, e.halfW }; // k=0(X)/2(Z)=halfW、k=1(Y)=halfH
        float tmin = 0.0f, tmax = bestDist; // 起步用 [0, bestDist]，逐轴收紧
        bool hit = true;
        const float p[3] = { e.pos.x(), e.pos.y(), e.pos.z() };
        const float o[3] = { origin.x(), origin.y(), origin.z() };
        const float d[3] = { dir.x(), dir.y(), dir.z() };
        for (int k = 0; k < 3; ++k) {
            const float ek = ext[k];
            const float mn = p[k] - ek, mx = p[k] + ek;
            if (std::abs(d[k]) < 1e-8f) {
                // 射线平行该轴：origin 必须落在 slab 内
                if (o[k] < mn || o[k] > mx) { hit = false; break; }
                continue;
            }
            float t1 = (mn - o[k]) / d[k];
            float t2 = (mx - o[k]) / d[k];
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) { hit = false; break; }
        }
        if (!hit) continue;
        // 起点已在 AABB 内（tmin<0）→ 取 tmax 不行（负方向出表面）；攻击视为贴脸命中 dist=0。
        const float dist = tmin >= 0.0f ? tmin : 0.0f;
        if (dist < bestDist) { bestDist = dist; bestIdx = int(i); }
    }
    if (bestIdx >= 0 && outDist) *outDist = bestDist;
    return bestIdx;
}

// t249 受击击退（spec「受击往攻击方向小跳击退」；机制等价 MC 1.0 knockback：受击实体沿攻击方向被推开 +
//   小幅上弹）。caller（PlayerController::attackMob）传玩家→mob 水平方向向量 (dirX,dirZ)；本方法归一后
//   设 vx/vz = kKnockbackHoriz×方向（水平冲量）+ vy = kKnockbackUp（小跳垂直冲量，向上）+ 解除 resting
//   （让 tick 重力分支接手上跳→减速→下落→着地；不解除则 resting 早 return 跳过垂直运动 = 不弹起）。
//   方向归一防御：零向量 / 非有限（NaN/Inf，caller 误传）→ 用实体当前 yaw 朝向兜底（-sin,-cos，同 aiWander
//   约定）避免零冲量。非 Mob（掉落物 / 下落方块）/ dead（尸体不被推，同 resolvePlayerPush）/ 越界 → 早退。
//   bump revision + emit → 驱动 QML {revision; posAt} 位置绑定重算（击退位移可见）。knockback 与 damageEntity
//   分离：扣血走 damageEntity，位移冲量走本方法，各自 bump revision（attackMob 内顺序调用，二者都生效）。
void EntityManager::knockback(int i, float dirX, float dirZ, float strength)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead) return; // t256：空槽防御（caller 已过滤）

    // 归一方向（caller 应已传合理向量，此处防御零 / 非有限）。len 非有限或近 0 → yaw 朝向兜底。
    float len = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (!(std::isfinite(len) && len > 1e-3f)) {
        dirX = -std::sin(e.yawRad);
        dirZ = -std::cos(e.yawRad);
        len = 1.0f;
    }
    dirX /= len;
    dirZ /= len;
    // t476 strength 缺省 1.0；玩家「击退」附魔命中时传 >1 拉大冲量（kKnockbackHoriz × strength）。
    const float horiz = kKnockbackHoriz * std::max(0.0f, strength);

    e.vx = dirX * horiz;
    e.vz = dirZ * horiz;
    e.vy = kKnockbackUp;   // 小跳垂直速度（向上为正；tick 重力分支接手）
    e.resting = false;     // 解除静止 → tick 处理上跳 + 下落 + 着地（否则 resting continue 跳过）
    notifyEntitiesChanged();
    qCInfo(lcEnt) << "mob" << i << "knockback dir=(" << dirX << dirZ << ") horiz=" << horiz;
}

// t476 点燃 mob（玩家「燃焰」命中触发；机制等价 MC fire-aspect ignite on hit）。把 fireTimer 刷到至少
//   duration 秒（取 max，不覆盖更长已有燃烧；duration<=0 早退）。fireTimer>0 → tick 火烧分支按既有时序扣血 +
//   isBurningAt 显火焰 + 致死掉熟肉（mobDied burned）。非 Mob / dead / 越界 → 静默早退。bump revision。
void EntityManager::ignite(int i, float duration)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead || duration <= 0.0f) return;
    if (e.fireTimer < duration) {
        e.fireTimer = duration;
        notifyEntitiesChanged();
        qCInfo(lcEnt) << "mob" << i << "ignited duration=" << duration;
    }
}

// t239 AI wander 自主移动（机制等价 MC passive mob「随机选向 + 时间片游荡 / 停驻」循环）。
//   - 时间片倒计时 wanderTimer；到 0 选新朝向 yawRad∈[0,2π) + 随机决定 idle（~25% speed=0 停驻）/ 行走
//     （speed=kWalkSpeed），重置 timer∈[kWanderMin, kWanderMax]。非确定性（生物 AI 非世界生成，不涉 §2-K）。
//   - idle（speed=0）→ moveSpeed=0 不位移（腿停）。
//   - 行走：按 yaw 算水平位移（dir = (-sin,0,-cos)，与 player 同 yaw 约定 → QML eulerRotation.y=yawDeg
//     使模型 -Z 正对行走方向）；逐轴（X 后 Z）世界边界 clamp（[0.5, world-0.5] 防 mob 走出世界坠虚空）
//     + 方块碰撞撤回（mobAabbHitsSolid 全格扫，仿 player move-and-resolve 贴墙滑动不穿入）。
//   - 撞墙（两轴都未动）→ 缩短 wanderTimer（≤0.2s）下帧大概率换向离开墙角，避免一直顶墙。
//   返回是否真位移（驱动 dirty + moveSpeed）。moveSpeed = 行走速度（撞墙/idle=0）供 t241 腿摆。
bool EntityManager::aiWander(Entity &e, float dt, World *world, float worldW, float worldD, float speedScale)
{
    // t1029 wander 冻结测试缝（setWanderFrozen 置位；缺省 false 不进本分支 = 生产路径零改动）：
    //   早退在时间片推进 / RNG 选向 / 速度写入**之前**——wanderTimer 保持、QRandomGenerator 全局流
    //   不消费（后续随机路径不受本缝扰动）、yawRad / wanderSpeed / 位置全部保持；仅 moveSpeed 归零
    //   （腿停，walkPhase 不推进）。tick 的重力 / 水推 / 击退 / 拉拽等物理分支在 aiWander 之外，冻结
    //   ≠ 悬停（P-t1029 行为腿钉「冻结中空中生成仍落地」）。解冻后 wanderTimer 多为生成初值 0 →
    //   下帧立即重掷选向，无缝恢复原噪声路径。
    if (m_wanderFrozen) {
        e.moveSpeed = 0.0f;
        return false;
    }

    // 时间片倒计时 → 选新向（随机 yaw + idle/行走 + 重置 timer）。
    e.wanderTimer -= dt;
    if (e.wanderTimer <= 0.0f) {
        // yawRad ∈ [0, 2π)：bounded(62832) 返 [0, 62831]，/10000 → [0, 6.2831] ≈ [0, 2π)。
        e.yawRad = float(QRandomGenerator::global()->bounded(62832)) / 10000.0f;
        // wanderTimer ∈ [kWanderMin, kWanderMax)：bounded(1000)/1000 ∈ [0, 0.999]。
        e.wanderTimer = kWanderMin
                        + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kWanderMax - kWanderMin);
        // idle（~kIdleChance 概率 speed=0 停驻）/ 行走（kWalkSpeed）。
        e.wanderSpeed = (float(QRandomGenerator::global()->bounded(100)) / 100.0f < kIdleChance)
                            ? 0.0f : kWalkSpeed;
    }

    if (e.wanderSpeed <= 0.0f) {
        e.moveSpeed = 0.0f; // idle：不动（腿停）
        return false;
    }

    // 行走：按 yaw 算水平位移（dir = (-sin,0,-cos)，与 player wishHoriz 同 yaw 约定）。
    //   t298：spd = wanderSpeed × speedScale（水中 speedScale=kWaterSpeedMul<1 → 位移 + 腿摆频率同步降）。
    const float spd = e.wanderSpeed * speedScale;
    const float dx = -std::sin(e.yawRad) * spd * dt;
    const float dz = -std::cos(e.yawRad) * spd * dt;
    const float ehw = e.halfW; // XZ 半宽（边界 clamp + 圆碰撞）
    const float ehh = e.halfH; // Y 半高（footprint 格扫 Y 范围）

    // X 轴：世界边界 clamp（mob XZ 半宽 ehw → 中心不越 [ehw, world-ehw]）+ 方块碰撞撤回。
    float newX = e.pos.x() + dx;
    if (newX < ehw) newX = ehw;
    if (newX > worldW - ehw) newX = worldW - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();

    // Z 轴：用已更新的 X + 同样边界 clamp / 方块碰撞撤回（两轴顺序敏感，Z 参照可能已撤回的 newX）。
    float newZ = e.pos.z() + dz;
    if (newZ < ehw) newZ = ehw;
    if (newZ > worldD - ehw) newZ = worldD - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();

    bool moved = false;
    if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
    if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }

    e.moveSpeed = moved ? spd : 0.0f; // 撞墙 → 腿停（moveSpeed=0），t241 腿摆频率随它（t298 水中 spd 已含减速）

    // 撞墙（两轴都未动）→ 缩短 timer 下帧大概率换向离开墙角（避免一直顶墙原地不动）。
    if (!moved) e.wanderTimer = std::min(e.wanderTimer, 0.2f);

    return moved;
}

// t1042 被动型惊逃移动（牛/羊/猪/鸡/豹猫共用机制；t1047 O-3 起狼不入惊逃集）：受击沿 setPanicFlee 置
//   panicTimer = kPanicDuration 后，各 AI 分支（通用被动链 / aiOcelot 未驯服分支 / aiWolf 幼崽门序位）
//   在 panicTimer>0 时调本方法。机制等价 MC 1.0 panic：背离玩家疾走 ~8s，期间不游走选向 / 不寻偶 / 不引诱 / 不幼随 / 不吃草
//   （优先级置顶由 caller 门序保证——通用链中本分支在 love/lure/baby 之前，eating 门读 panicTimer 豁免）。
//   实现：每 AI tick 衰减 panicTimer + 钉 yaw=离玩家（dir=(-sin,0,-cos) 约定下背离方向 = atan2(pdx,pdz)，
//   与「朝向玩家」的 atan2(-pdx,-pdz) 反号）+ 强制疾走 kPanicSpeed + 短置 wanderTimer=0.4（防 aiWander 本帧
//   重新随机选向，同 t1025 引诱/寻偶钉法），位移交 aiWander（复用逐轴碰撞撤回 + 边界 clamp + 撞墙缩短 timer
//   换向 + t1029 冻结缝语义——wander 冻结时惊逃同样冻结不位移）。返回是否真位移（驱动 dirty + moveSpeed）。
bool EntityManager::aiPanicFlee(Entity &e, float dt, World *world, float worldW, float worldD,
                                float speedScale, const QVector3D &playerPos)
{
    if (e.panicTimer <= 0.0f) return false;
    e.panicTimer -= dt;
    if (e.panicTimer < 0.0f) e.panicTimer = 0.0f;
    const float pdx = playerPos.x() - e.pos.x();
    const float pdz = playerPos.z() - e.pos.z();
    if (pdx * pdx + pdz * pdz > 1e-8f)
        e.yawRad = std::atan2(pdx, pdz); // 背离玩家（同 (-sin,-cos) yaw 约定；重合时保当前朝向）
    e.wanderSpeed = kPanicSpeed; // 疾走（2×kWalkSpeed；MC panic 加速游离量级）
    e.wanderTimer = 0.4f;        // 防 aiWander 本帧重新随机选向（同 t1025 引诱钉法）
    return aiWander(e, dt, world, worldW, worldD, speedScale);
}

// t399 鱿鱼水生 AI（详见头文件 aiSquid 注释）。机制对齐 MC 1.0 squid：水里周期喷水推进（上浮 + 水平漂游）+
//   通用重力缓沉 → 节律性游动；离水搁浅走 t980 挣扎缓动（间歇低速蠕动，非行走步态；离水掉血在 tick
//   窒息块）。分层（PLAN §2）：只读 World::blockAt（mobFeetInWater 脚位水格判，同文件静态助手）+ 自身
//   数据；写 EntityManager 自身（pos / vy / yawRad / swimTimer / moveSpeed）。无向上依赖。
bool EntityManager::aiSquid(Entity &e, float dt, World *world, float worldW, float worldD, float speedScale)
{
    // 离水（搁浅）——t980 挣扎缓动（替代旧 aiWander 委托：鱿鱼无腿，陆上不该有正常行走摆动步态）。
    //   swimTimer 复用为挣扎周期相位计（水中分支独占于搁浅分支，互斥无冲突）：每 kSquidFlopInterval
    //   周期随机换一次挣扎向，周期前 kSquidStruggleBurst 段以 kSquidStruggleSpeed 低速蠕动（moveSpeed=
    //   蠕动速 → walkPhase 低速间歇推进 = 触手拖拽式小摆，非 kWalkSpeed 连续步摆），后段摊歇（moveSpeed=0）。
    //   speedScale 透传（水中减速语义不在搁浅态出现 → 通常 1.0；恰在浅水边沿 <1 亦无妨，更笨拙）。
    //   逐轴（X 后 Z）边界 clamp + mobAabbHitsSolid 撤回防穿墙（同 aiWander 位移模式）。
    if (!mobFeetInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH)) {
        e.swimTimer -= dt;
        if (e.swimTimer <= 0.0f) {
            e.swimTimer += kSquidFlopInterval; // 周期回绕（相位连续；搁浅入水再出时相位带旧值无害——重进
                                               //   搁浅首周期可能短一截，观感即「刚落地的急促扑腾」）
            e.yawRad = float(QRandomGenerator::global()->bounded(62832)) / 10000.0f; // 随机挣扎向
        }
        const float flopPhase = kSquidFlopInterval - e.swimTimer;   // 周期内相位 [0, interval)
        const bool struggling = flopPhase < kSquidStruggleBurst;    // 前段蠕动、后段摊歇
        const float spd = struggling ? kSquidStruggleSpeed * speedScale : 0.0f;
        const float dx = -std::sin(e.yawRad) * spd * dt;
        const float dz = -std::cos(e.yawRad) * spd * dt;
        const float ehw = e.halfW;
        const float ehh = e.halfH;
        float newX = e.pos.x() + dx;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + dz;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        bool moved = false;
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
        // 蠕动段 moveSpeed=低速值（walkPhase 间歇慢推）；摊歇/撞墙=0（触手停）。非 kWalkSpeed 值域
        //   （0.5 < 1.0）本身即「非行走步态」的判别面（矩阵 P-t980 钉 moveSpeed 上界 + 间歇占比）。
        e.moveSpeed = moved ? spd : 0.0f;
        return moved;
    }

    // 水中：swimTimer 倒计时到 → 喷水推进（vy 上冲量 + 随机换向漂游方向）+ 重置随机周期。
    e.swimTimer -= dt;
    if (e.swimTimer <= 0.0f) {
        e.swimTimer = kSquidSwimIntervalMin
                      + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kSquidSwimIntervalMax - kSquidSwimIntervalMin);
        // 喷水推进上冲量（正=向上）。其下通用重力段以 kWaterGravity 缓沉把它减速到 0 再反向 → 上浮→缓沉 bobbing。
        e.vy = kSquidSwimUp;
        // 随机换向（水平漂游方向）；与 player / aiWander 同 yaw 约定：dir = (-sin,0,-cos)。
        e.yawRad = float(QRandomGenerator::global()->bounded(62832)) / 10000.0f;
    }

    // 水平漂游（沿 yaw 慢速漂移；kSquidSwimSpeed 不受 speedScale 影响 —— 物种游速特征，叠加通用减速会过慢）。
    //   逐轴（X 后 Z）边界 clamp + mobAabbHitsSolid 撤回防穿墙（同 aiWander 移动模式；水中漂游遇实体方块亦撤回）。
    const float spd = kSquidSwimSpeed;
    const float dx = -std::sin(e.yawRad) * spd * dt;
    const float dz = -std::cos(e.yawRad) * spd * dt;
    const float ehw = e.halfW;
    const float ehh = e.halfH;
    float newX = e.pos.x() + dx;
    if (newX < ehw) newX = ehw;
    if (newX > worldW - ehw) newX = worldW - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
    float newZ = e.pos.z() + dz;
    if (newZ < ehw) newZ = ehw;
    if (newZ > worldD - ehw) newZ = worldD - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();

    bool moved = false;
    if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
    if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
    // moveSpeed 恒取漂游速（水中持续漂移 → 触腕摆动画常驻；区别于 aiWander 的 idle/行走二态）。
    e.moveSpeed = spd;
    return moved;
}

// t480 狼 AI（详见头文件 aiWolf 注释）。机制对齐 MC 1.0 狼三态：
//   (1) 未驯服 → 中立方 + 受击反击（t1031 收口：不主动攻击玩家；t1042 反击面：被打 → setWolfProvoked 置
//       chasing=玩家 → 追咬玩家，超时/超距清除同 hostile 收口；幼崽不反击只惊逃）。
//   (2) 驯服 + 坐 → 留守（不移动不攻击）。
//   (3) 驯服 + 站 → 跟随主人 + 防御（追击咬击 m_wolfTarget 目标 mob）；求偶期优先寻偶。
//       t947 ① 跟随门：观察者（playerSpectator）不跟随（走近/瞬移全停 → 回退 wander）；③ chase 越障跳
//       改 aiHostile 同款主动判定（不等撞停、无距离短路边 —— 详见 chase lambda 内注释）。
//   返是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。分层（PLAN §2）：只读 World::isSolid + 自身数据；
//   咬玩家走 mobAttackedPlayer 语义信号（呈现层路由 PlayerState）、咬 mob 走 damageEntity（同层受击链）。
bool EntityManager::aiWolf(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                           float worldW, float worldD, float speedScale, bool playerTargetable,
                           bool playerSpectator)
{
    // 坐：留守 —— 不移动不攻击（跟随主人回来时仍坐原地；机制等价 MC 坐狼）。moveSpeed 清零 → walkPhase 冻结。
    if (e.wolfSitting) {
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        return false;
    }

    if (e.wolfAttackCooldown > 0.0f) {
        e.wolfAttackCooldown -= dt;
        if (e.wolfAttackCooldown < 0.0f) e.wolfAttackCooldown = 0.0f;
    }

    // 水平追击移动 lambda（复用 aiHostile 逐轴 AABB 撤回 + 世界边界 clamp 模式）：朝 (tx,tz) 以 spd 走，
    //   返是否真位移。tdy = 目标脚位高差（t988：异层目标在咬击带内也须探跳爬升，见下门注）。
    //   捕获 e/dt/world/worldW/worldD（本函数内唯一移动路径；四处复用免内联副本）。
    auto chase = [&](float tx, float tz, float spd, float distXZ, float tdy) -> bool {
        if (distXZ <= 1e-4f) { e.moveSpeed = 0.0f; return false; } // 目标重合 → 不位移（避免除零）
        const float ehw = e.halfW; // XZ 半宽（边界 clamp + 碰撞）
        const float ehh = e.halfH; // Y 半高（footprint 格扫）
        const float nx = (tx - e.pos.x()) / distXZ;
        const float nz = (tz - e.pos.z()) / distXZ;
        float newX = e.pos.x() + nx * spd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + nz * spd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        bool moved = false;
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
        e.moveSpeed = moved ? spd : 0.0f; // 撞墙 → 腿停（t241 腿摆频率随它）
        // t947 ③ 越障跳重构（用户第五轮「攻击被方块挡住不会跳」）——t923 版把整段门在 `!moved && distXZ > 0.6f`
        //   上，两个门各漏一类攻击场景：①斜向滑墙时单轴仍可滑 → moved 恒 true，永不探跳（狼贴着墙往目标
        //   z 向溜，纵向抖动步长恒非零 → 等不到「撞全停」那一拍）；②隔墙贴脸目标（如矮台阶上的目标）XZ 距
        //   可 ≤0.6 被距离门短路。两型表现都是「顶墙干瞪眼」。改回 aiHostile 同款**主动判定**（3351-3373
        //   先例）：每个 AI tick、resting 且朝目标移动就探前方脚位格（isJumpObstacle 单一权威 —— 作物/矮
        //   支撑豁免沿用），是 1 格墙 + 墙顶两格净空 → kJumpSpeed + t670 朝目标水平滑流。判定独立于 moved
        //   （不等撞停）也独立于 distXZ（贴脸障碍照跳；无墙时 isJumpObstacle 恒 false 不会原地蹦）。
        //   review0830 #14：攻击距离内不探跳（distXZ ≤ kAttackRange 咬击带内）——贴脸可咬时前方矮障碍
        //   不再触发瞬态起跳（边咬边跳 1-2 跳）；跳探只服务「接近被挡」的越障，进入咬击带即无需越障。
        //   跟随/寻偶路径不受影响（kFollowMinDist 2.5 > kAttackRange，chase 调用时 distXZ 恒 > 1.6）。
        //   t988 门收窄（用户「遇到要跳跃才能上的格子不会跳、卡在那里」）：#14 的压跳语义 = 「贴脸
        //   **同层可咬**时不跳」——异层目标（|tdy| > 0.5，1 格台阶上/下的目标）在咬带内也必须探跳：
        //   旧门把这类目标一并压死 = 狼贴台面 0.8-1.6 处隔台阶卡死（咬带内既不跳、血隔台扣但狼永不上台，
        //   用户观感「不会跳、卡在那里」）。同层目标（|tdy| ≤ 0.5）带内照旧压跳（#14 行为钉 rig 保持绿）。
        if (e.resting && world && (distXZ > kAttackRange || std::abs(tdy) > 0.5f)) {
            const float fdx = -std::sin(e.yawRad);
            const float fdz = -std::cos(e.yawRad);
            const int fy = qFloor(e.pos.y() - e.halfH);          // 脚位格（mob 底面所在格）
            const int fx = qFloor(e.pos.x() + fdx * 0.6f);
            const int fz = qFloor(e.pos.z() + fdz * 0.6f);
            if (fy >= 0
                && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH) // 作物可穿越不跳（t642 同款）
                && !world->isSolid(fx, fy + 1, fz)                // 墙顶可落
                && !world->isSolid(fx, fy + 2, fz)) {             // 头位可容（跳峰 1.25 + 身高 0.9 → 两格口径，同 aiHostile）
                e.vy = kJumpSpeed;
                e.resting = false;
                e.jumpGX = ((tx - e.pos.x()) / distXZ) * spd; // t670 越障跳水平滑流（朝目标）
                e.jumpGZ = ((tz - e.pos.z()) / distXZ) * spd;
            }
        }
        // t923 ② 泳跃（保留「被挡才跃」触发语义，与上面的主动探跳分工）：水中（脚位格 Water 且未 resting）
        //   且位移被挡 → vy=kJumpSpeed 跃出水面贴 1 格岸（出水弧线 ≥1 格 → 贴岸跃上）。距离门随 t947 ③ 一并
        //   摘除（主人贴岸俯视时 XZ 可 ≤0.6，距离门会把最后一步上岸也拦死）；重复触发无害（出水后脚位离水
        //   即停，弧线落岸/回落再试）。
        if (!moved && world && !e.resting && distXZ > 1e-4f) {
            const int fy = qFloor(e.pos.y() - e.halfH);
            if (fy >= 0
                && world->blockAt(qFloor(e.pos.x()), fy, qFloor(e.pos.z())) == BlockRegistry::Water)
                e.vy = kJumpSpeed; // 泳跃（出水弧线 ≥1 格 → 贴岸跃上）
        }
        return moved;
    };

    // t1042 狼幼崽惊逃置顶 → t1047 O-3 改口径：MC 狼无 PanicGoal（成体被打经 setWolfProvoked 反击 /
    //   幼崽零 panic）→ 狼整体退出惊逃集（setPanicFlee 单一登记权威不再收狼）。本门序位保留为共用
    //   移动器的消费位（狼侧 panicTimer 恒 0 → 不可达；防御未来登记面变化绕过优先级序）。幼崽落回：
    //   生产面狼幼崽恒驯服（t480 父代继承）→ 恒走跟随语义；野幼崽恒游荡。
    //   反击面由 setWolfProvoked 的 baby 门登记兜底（chasing 永不为幼崽置位，无论驯服与否）。
    if (e.baby && e.panicTimer > 0.0f)
        return aiPanicFlee(e, dt, world, worldW, worldD, speedScale, playerPos);

    // (1) 未驯服：野狼两态（t1031 中立收口 + t1042 反击面；机制等价 MC 1.0 原版：野狼**中立**，不主动
    //     攻击玩家，被玩家攻击才敌对反击）。旧 t480 主动敌对分支（kWolfDetectRange 侦测 → 见人就咬）随
    //     t1031 退役**不复活**——chasing 只能由受击沿（setWolfProvoked）置位，侦测带内仅**续期**已有反击
    //     记忆（见下），未挑逗的野狼照旧纯游荡。
    if (!e.wolfTamed) {
        // t290 同源门（aiIronGolem `golemAngry && playerTargetable` 先例）：创造/观察者不可锁定 → 不反击
        //   并清反击态（同 tick hostile 收口清 chasing/chaseTimer 模式，防模式切换后残留追击）。
        if (!playerTargetable) {
            if (e.chasing) { e.chasing = false; e.chaseTimer = 0.0f; }
            return aiWander(e, dt, world, worldW, worldD, speedScale);
        }
        const float wdx = playerPos.x() - e.pos.x();
        const float wdz = playerPos.z() - e.pos.z();
        const float wdy = playerPos.y() - e.pos.y();
        const float distPlayer = std::sqrt(wdx * wdx + wdz * wdz);
        // 反击记忆维持（同 aiHostile (1) 收口语义倒置：进入只由受击沿写点置位；带内续期、带外衰减、
        //   归零弃追回游荡）。
        if (e.chasing) {
            if (distPlayer <= kDetectRange) {
                e.chaseTimer = kChaseMemory;
            } else {
                e.chaseTimer -= dt;
                if (e.chaseTimer <= 0.0f) { e.chaseTimer = 0.0f; e.chasing = false; }
            }
        }
        if (!e.chasing)
            return aiWander(e, dt, world, worldW, worldD, speedScale);
        // t1042 反击追咬：追击玩家（复用 t923 chase 体系——逐轴碰撞撤回 + 越障跳 + 泳跃同款）+ 近距咬击
        //   （狼咬击常量 kWolf*；t321 全局节流收口同 aiHostile——多狼围攻与敌对围攻共用玩家受击节拍）。
        //   咬击走 mobAttackedPlayer 语义信号（呈现层路由 PlayerState，Survival 门控在呈现侧，同 t290）。
        if (distPlayer > 1e-4f) e.yawRad = std::atan2(-wdx, -wdz);
        const bool moved = chase(playerPos.x(), playerPos.z(), kWolfChaseSpeed * speedScale, distPlayer, wdy);
        if (distPlayer <= kAttackRange && std::abs(wdy) <= kAttackVertRange
            && e.wolfAttackCooldown <= 0.0f && m_playerHitCooldown <= 0.0f) {
            e.wolfAttackCooldown = kWolfAttackCooldown;
            m_playerHitCooldown = kPlayerHitThrottle;
            float kbX, kbZ;
            if (distPlayer > 1e-3f) { kbX = wdx / distPlayer; kbZ = wdz / distPlayer; }
            else { kbX = -std::sin(e.yawRad); kbZ = -std::cos(e.yawRad); } // 兜底：朝狼面朝方向（=推开玩家）
            emit mobAttackedPlayer(kWolfAttackDamage, e.mobType, kbX, kbZ);
            qCInfo(lcEnt) << "wild wolf" << idx << "retaliated against player for"
                          << kWolfAttackDamage << "HP";
        }
        return moved;
    }

    // (2)(3) 驯服 + 站。
    // t400 求偶优先（机制等价 MC 求偶者走向配偶）：驯服狼在求偶期 → 覆盖跟随/防御，主动走向最近同种求偶配偶
    //   （进入配对距离后由 tickBreeding 产幼崽）；无配偶（仅一方求偶）→ 照常跟随。
    if (e.loveTimer > 0.0f && !e.baby) {
        const int mate = findNearestMate(idx);
        if (mate >= 0) {
            const Entity &mp = m_entities[size_t(mate)];
            const float mdx = mp.pos.x() - e.pos.x();
            const float mdz = mp.pos.z() - e.pos.z();
            const float md = std::sqrt(mdx * mdx + mdz * mdz);
            if (md > 1e-4f) e.yawRad = std::atan2(-mdx, -mdz);
            return chase(mp.pos.x(), mp.pos.z(), kWolfChaseSpeed * speedScale, md,
                         mp.pos.y() - e.pos.y());
        }
    }

    // 防御目标校验（目标死亡 / 释放槽 / 自身 → 清除；slot-reuse 索引稳定但槽可被新 mob 复用 → 每 AI tick 复核）。
    if (m_wolfTarget >= 0 && m_wolfTarget < int(m_entities.size())) {
        const Entity &t = m_entities[size_t(m_wolfTarget)];
        if (m_wolfTarget == idx || !t.alive || t.kind != Mob || t.dead) m_wolfTarget = -1;
    }
    if (m_wolfTarget >= 0) {
        const Entity &t = m_entities[size_t(m_wolfTarget)];
        const float dx = t.pos.x() - e.pos.x();
        const float dz = t.pos.z() - e.pos.z();
        const float dy = t.pos.y() - e.pos.y();
        const float distXZ = std::sqrt(dx * dx + dz * dz);
        if (distXZ > 1e-4f) e.yawRad = std::atan2(-dx, -dz);
        const bool moved = chase(t.pos.x(), t.pos.z(), kWolfChaseSpeed * speedScale, distXZ, dy);
        // 近距咬击目标 mob（damageEntity 复用受击链：扣血 + 红闪 + 归零 mobDied 死亡掉落；冷却门控防连抽）。
        if (distXZ <= kAttackRange && std::abs(dy) <= kAttackVertRange && e.wolfAttackCooldown <= 0.0f) {
            e.wolfAttackCooldown = kWolfAttackCooldown;
            damageEntity(m_wolfTarget, kWolfAttackDamage);
            // t948 仇恨转移注册（单一入口 mobAggroAgainst）：狼**主动咬击**命中也要注册被咬者对狼的仇恨
            //   （t923 反击注册面核——不只狼被打了才反击；与上方 aiHostile 命中玩家处的 m_wolfTarget 注册
            //   互补成双向面）。受害者门在入口内：敌对仇恨型 {Shambler/Spider/Silverfish/Bones} 记 slot+
            //   serial 快照（下个 AI tick 起 aiHostile/aiArcher 转火追咬/射击本狼）；被动受害者（主人误标
            //   羊/猪等）静默 no-op（无仇恨系统，逃跑链不变）。若咬击致死同帧受害者已 dead → 入口 no-op。
            mobAggroAgainst(m_wolfTarget, idx);
            qCInfo(lcEnt) << "tamed wolf" << idx << "bit mob" << m_wolfTarget
                          << "for" << kWolfAttackDamage << "HP";
        }
        return moved;
    }

    // t947 ① 跟随门（用户口径：创造/生存跟随、观察者模式不跟随）：观察者 → 跳过整个跟随段（走近 + 过远
    //   瞬移补位一并停 —— 瞬移是跟随段的防掉队机制，跟随停则瞬移同停），回退 aiWander（同 t290 不可锁定
    //   回退游荡先例：无跟随语义时的默认行为态）。门放在防御/寻偶分支之后 —— 防御追击（m_wolfTarget）与
    //   求偶寻偶是 mob-mob 语义，不随主人模式翻转（观察者下狼被打仍反击）。创造/生存（false）不进此分支，
    //   跟随行为零变化。
    if (playerSpectator)
        return aiWander(e, dt, world, worldW, worldD, speedScale);

    // 无防御目标 → 跟随主人：distXZ > kFollowMinDist 走近（kFollowMinDist 内停步贴近）；过远 kWolfTeleportDist
    //   瞬移到主人附近安全位（防跟随永久掉队 —— 狼速 3.5 < 玩家 4.3；机制等价 MC 狼距主人过远传送）。
    const float fdx = playerPos.x() - e.pos.x();
    const float fdz = playerPos.z() - e.pos.z();
    const float followDist = std::sqrt(fdx * fdx + fdz * fdz);
    if (followDist > kWolfTeleportDist) {
        auto *rng = QRandomGenerator::global();
        for (int attempt = 0; attempt < 8; ++attempt) {
            const float ang = float(rng->bounded(62832)) / 10000.0f; // [0, 2π)
            const float rad = 2.0f + float(rng->bounded(100)) / 100.0f * 3.0f; // [2, 5) 格环
            const int tx = qFloor(playerPos.x() + std::cos(ang) * rad);
            const int tz = qFloor(playerPos.z() + std::sin(ang) * rad);
            if (tx < 0 || tz < 0 || tx >= int(worldW) || tz >= int(worldD)) continue;
            // 自主人高度向上 1 格起向下扫 5 格，找「本格 air + 下方实体」（防瞬移进墙 / 悬空 / 天花板）。
            for (int y = qFloor(playerPos.y()) + 1; y >= std::max(0, qFloor(playerPos.y()) - 4); --y) {
                // review26 #19：落点支撑收口 isCollidable（t865 单一权威）——旧 isSolid 把花草 / 水 / 轨当
                //   「安全位」，宠物瞬移落花草下帧坠落、落水则瞬进水（t878 阈值 24→12 放大暴露频率）。
                if (world->blockAt(tx, y, tz) == BlockRegistry::Air && world->isCollidable(tx, y - 1, tz)) {
                    e.pos = QVector3D(float(tx) + 0.5f, float(y) + e.halfH, float(tz) + 0.5f);
                    e.vy = 0.0f;
                    e.resting = true; // 落安全位 → 贴地（下帧 resting 复探支撑；pos 变化须返 true 驱动 dirty）
                    return true; // 瞬移 = 位置变更（tick 据返值标 dirty → 末尾 bump revision 刷新 QML）
                }
            }
        }
    }
    if (followDist > 1e-4f) e.yawRad = std::atan2(-fdx, -fdz); // 跟随期间朝主人
    if (followDist > kFollowMinDist)
        return chase(playerPos.x(), playerPos.z(), kWolfChaseSpeed * speedScale, followDist,
                     playerPos.y() - e.pos.y());
    e.wanderSpeed = 0.0f;
    e.moveSpeed = 0.0f; // 已到位（贴近主人）→ 停步（腿停）
    return false;
}

// t481 豹猫/猫 AI（详见头文件 aiOcelot 注释）。机制对齐 MC 1.0 豹猫/猫三态：
//   (1) 未驯服 → 被动游荡（丛林野豹猫；不攻击玩家不敌对，纯 aiWander）。
//   (2) 驯服 + 坐 → 留守（不移动不跟随，机制等价 MC 坐猫）。
//   (3) 驯服 + 站 → 跟随主人（走近 / 停步 / 过远瞬移）；求偶期优先寻偶。
//   猫**不防御**（机制等价 MC 1.0 猫不攻击怪物 —— 驱赶 Stalker 由 aiStalker 侧对猫/豹猫临近时逃离实现）。
//   review0830 #5 跟随门：playerSpectator=true（观察者）→ 跳过跟随段（走近 + 过远瞬移一并停——瞬移是
//   跟随段的防掉队机制），回退 aiWander；创造/生存照常跟随。与 aiWolf 的门**同一字面量门形**（矩阵
//   sync pin 钉 count≥2），门位同构：求偶分支之后（mob-mob 语义不随主人模式翻转）、跟随段之前。
//   返是否真位移（驱动 dirty + moveSpeed + walkPhase 腿摆）。分层（PLAN §2）：只读 World::isSolid + 自身数据。
bool EntityManager::aiOcelot(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                             float worldW, float worldD, float speedScale, bool playerSpectator)
{
    // (1) 未驯服：被动游荡（丛林野豹猫；不攻击不敌对。驯服前的野生形态，机制等价 MC 1.0 野豹猫）。
    //     t1042 受击惊逃：被打（setPanicFlee）→ panicTimer>0 期间背离玩家疾走（MC 原版口径：豹猫被打
    //     只逃不反击）；驯服猫不受惊逃（跟随语义不变，跟随门/坐留守照旧）。
    if (!e.ocelotTamed) {
        if (e.panicTimer > 0.0f)
            return aiPanicFlee(e, dt, world, worldW, worldD, speedScale, playerPos);
        return aiWander(e, dt, world, worldW, worldD, speedScale);
    }

    // (2) 坐：留守 —— 不移动（跟随主人回来时仍坐原地；机制等价 MC 坐猫）。moveSpeed 清零 → walkPhase 冻结。
    if (e.ocelotSitting) {
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        return false;
    }

    // 水平追击移动 lambda（复用 aiWolf chase 模式）：朝 (tx,tz) 以 spd 走，返是否真位移。
    //   捕获 e/dt/world/worldW/worldD（本函数内唯一移动路径；求偶 / 跟随两处复用免两份内联副本）。
    auto chase = [&](float tx, float tz, float spd, float distXZ) -> bool {
        if (distXZ <= 1e-4f) { e.moveSpeed = 0.0f; return false; } // 目标重合 → 不位移（避免除零）
        const float ehw = e.halfW; // XZ 半宽（边界 clamp + 碰撞）
        const float ehh = e.halfH; // Y 半高（footprint 格扫）
        const float nx = (tx - e.pos.x()) / distXZ;
        const float nz = (tz - e.pos.z()) / distXZ;
        float newX = e.pos.x() + nx * spd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + nz * spd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        bool moved = false;
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
        e.moveSpeed = moved ? spd : 0.0f; // 撞墙 → 腿停（t241 腿摆频率随它）
        return moved;
    };

    // 求偶优先（机制等价 MC 求偶者走向配偶）：驯服猫在求偶期 → 覆盖跟随，主动走向最近同种求偶配偶
    //   （进入配对距离后由 tickBreeding 产幼崽）；无配偶（仅一方求偶）→ 照常跟随。
    if (e.loveTimer > 0.0f && !e.baby) {
        const int mate = findNearestMate(idx);
        if (mate >= 0) {
            const Entity &mp = m_entities[size_t(mate)];
            const float mdx = mp.pos.x() - e.pos.x();
            const float mdz = mp.pos.z() - e.pos.z();
            const float md = std::sqrt(mdx * mdx + mdz * mdz);
            if (md > 1e-4f) e.yawRad = std::atan2(-mdx, -mdz);
            return chase(mp.pos.x(), mp.pos.z(), kOcelotFollowSpeed * speedScale, md);
        }
    }

    // review0830 #5 跟随门（与 aiWolf 同款门同字面量——对称 sync pin 消费端）：观察者主人 → 跳过整个
    //   跟随段（走近 + 过远瞬移补位一并停），回退 aiWander（同 t290 不可锁定回退游荡先例）。门在求偶
    //   分支之后：求偶寻偶是 mob-mob 语义，不随主人模式翻转（观察者下猫仍寻偶，同狼防御仍反击）。
    //   创造/生存（false）不进此分支，跟随行为零变化。
    if (playerSpectator)
        return aiWander(e, dt, world, worldW, worldD, speedScale);

    // 跟随主人：distXZ > kFollowMinDist 走近（kFollowMinDist 内停步贴近）；过远 kOcelotTeleportDist 瞬移到主人
    //   附近安全位（防跟随永久掉队 —— 机制等价 MC 猫距主人过远传送；同狼 aiWolf 瞬移模式）。
    const float fdx = playerPos.x() - e.pos.x();
    const float fdz = playerPos.z() - e.pos.z();
    const float followDist = std::sqrt(fdx * fdx + fdz * fdz);
    if (followDist > kOcelotTeleportDist) {
        auto *rng = QRandomGenerator::global();
        for (int attempt = 0; attempt < 8; ++attempt) {
            const float ang = float(rng->bounded(62832)) / 10000.0f; // [0, 2π)
            const float rad = 2.0f + float(rng->bounded(100)) / 100.0f * 3.0f; // [2, 5) 格环
            const int tx = qFloor(playerPos.x() + std::cos(ang) * rad);
            const int tz = qFloor(playerPos.z() + std::sin(ang) * rad);
            if (tx < 0 || tz < 0 || tx >= int(worldW) || tz >= int(worldD)) continue;
            // 自主人高度向上 1 格起向下扫 5 格，找「本格 air + 下方实体」（防瞬移进墙 / 悬空 / 天花板）。
            for (int y = qFloor(playerPos.y()) + 1; y >= std::max(0, qFloor(playerPos.y()) - 4); --y) {
                // review26 #19：落点支撑收口 isCollidable（t865 单一权威）——旧 isSolid 把花草 / 水 / 轨当
                //   「安全位」，宠物瞬移落花草下帧坠落、落水则瞬进水（t878 阈值 24→12 放大暴露频率）。
                if (world->blockAt(tx, y, tz) == BlockRegistry::Air && world->isCollidable(tx, y - 1, tz)) {
                    e.pos = QVector3D(float(tx) + 0.5f, float(y) + e.halfH, float(tz) + 0.5f);
                    e.vy = 0.0f;
                    e.resting = true; // 落安全位 → 贴地（下帧 resting 复探支撑；pos 变化须返 true 驱动 dirty）
                    return true; // 瞬移 = 位置变更（tick 据返值标 dirty → 末尾 bump revision 刷新 QML）
                }
            }
        }
    }
    if (followDist > 1e-4f) e.yawRad = std::atan2(-fdx, -fdz); // 跟随期间朝主人
    if (followDist > kFollowMinDist)
        return chase(playerPos.x(), playerPos.z(), kOcelotFollowSpeed * speedScale, followDist);
    e.wanderSpeed = 0.0f;
    e.moveSpeed = 0.0f; // 已到位（贴近主人）→ 停步（腿停）
    return false;
}

// t482/t483 最近**敌对** mob 查找（雪傀儡抛雪球 / 铁傀儡追击攻击调）：返距 pos 在 range 内最近一只
//   alive && !dead && kind==Mob && hostile 的 mob 索引；无 → -1。**只打敌对**（passive / 玩家 / 造物自身不攻击，
//   机制等价 MC 防御造物只打怪物）。O(n) 每 golem 每 AI tick，n≤kCap=64 可忽略。const 只读自身数据。
int EntityManager::nearestHostile(const QVector3D &pos, float range) const
{
    int best = -1;
    float bestD2 = range * range;
    for (int i = 0; i < int(m_entities.size()); ++i) {
        const Entity &m = m_entities[size_t(i)];
        if (!m.alive || m.kind != Mob || !m.hostile || m.dead) continue; // 仅活体敌对 mob
        const float dx = m.pos.x() - pos.x();
        const float dy = m.pos.y() - pos.y();
        const float dz = m.pos.z() - pos.z();
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

// t712 批「敌对 mob 主动攻击铁傀儡」：最近活体铁傀儡查找（见头文件注释；同 nearestHostile 模式，
//   谓词换 mobType==MobIronGolem）。
int EntityManager::nearestIronGolem(const QVector3D &pos, float range) const
{
    int best = -1;
    float bestD2 = range * range;
    for (int i = 0; i < int(m_entities.size()); ++i) {
        const Entity &m = m_entities[size_t(i)];
        if (!m.alive || m.kind != Mob || m.dead || m.mobType != MobIronGolem) continue; // 仅活体铁傀儡
        const float dx = m.pos.x() - pos.x();
        const float dy = m.pos.y() - pos.y();
        const float dz = m.pos.z() - pos.z();
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    return best;
}

// t482 朝 target 解抛物初速并抛雪球（aiSnowGolem 远程攻击调；详见头文件 fireSnowball 注释）。
//   与 fireArrow（骷髅射箭）同数学：origin = shooter 中心 + 朝 target 前移 0.5 格（防贴墙 spawn 入墙即没）；
//   水平速度固定 kSnowballSpeed → 飞行时间 t=d/vH；据 target 高度差反解 vy；三轴 ±kSnowballSpread 抖动。
void EntityManager::fireSnowball(int idx, const Entity &shooter, const QVector3D &target)
{
    Q_UNUSED(idx)
    const float dx0 = target.x() - shooter.pos.x();
    const float dz0 = target.z() - shooter.pos.z();
    const float horiz0 = std::sqrt(dx0 * dx0 + dz0 * dz0);
    if (horiz0 < 0.01f) return; // 退化（同格）→ 安全早退（防除零）
    // origin = shooter 中心 + 朝 target 前移 0.5 格（雪傀儡口鼻高度 ~ 中心上方，避免贴墙 spawn 入墙即没）。
    QVector3D origin(shooter.pos.x() + dx0 / horiz0 * 0.5f,
                     shooter.pos.y() + shooter.halfH * 0.5f,
                     shooter.pos.z() + dz0 / horiz0 * 0.5f);
    const float dx = target.x() - origin.x();
    const float dy = target.y() - origin.y();
    const float dz = target.z() - origin.z();
    const float horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz < 0.01f) return;
    const float vH = kSnowballSpeed;
    const float t = horiz / vH; // 飞行时间（水平距 / 水平速度）
    float vy = (dy + 0.5f * kGravity * t * t) / t; // 抛物解（命中 target 高度的初速）
    if (vy > kSnowballMaxVert) vy = kSnowballMaxVert;
    if (vy < -kSnowballMaxVert) vy = -kSnowballMaxVert;
    float vx = (dx / horiz) * vH;
    float vz = (dz / horiz) * vH;
    // 非 100% 精准（雪傀儡抛掷有散布）：三轴 ±kSnowballSpread 随机抖动（spread ≪ vH 不改飞行时间量级）。
    auto rnd = []() {
        return (float(QRandomGenerator::global()->bounded(2001)) - 1000.0f) / 1000.0f; // [-1,1]
    };
    vx += rnd() * kSnowballSpread;
    vz += rnd() * kSnowballSpread;
    vy += rnd() * kSnowballSpread;
    spawnSnowball(origin, QVector3D(vx, vy, vz), kSnowballDamage, idx); // t505 golem 雪球保留敌对伤害；t553 idx = 发射者（命中排除自身）
}

// t482 雪傀儡 AI（详见头文件 aiSnowGolem 注释）。机制对齐 MC 1.0 雪傀儡：游荡 + 抛雪球打敌对 + 行走留雪 +
//   热/雨/水融化。neutral non-hostile（hostile=false → 不参与黑暗刷怪 / 燃烧 / 远距消失）。
//   分层（PLAN §2）：只读 World（blockAt/isSolid/biomeIdAt/isPrecipitatingAt）+ 自身数据；写自身（pos /
//   attackCooldown / moveSpeed）+ damageEntity（同层）+ 向下静默写 World（setWaterSilent 雪层）。
//   t510 改融化语义：旧版「热群系/降水 → kSnowMeltDamage=100 一击致死」（用户报「沙漠召唤即死」），spec 要
//     「慢慢扣血到 0 才死」。改：热群系 / 入水 / 降水 → 累加 meltAccum，达 kSnowMeltInterval 扣 kSnowMeltDamage=1 HP
//     （每秒 1HP，满血 4 → ~4s 融化死亡，机制等价 MC 1.0 持续热伤害而非即死）。新增「入水扣血」分支（脚位 /
//     身体格在水 → 同热伤害路径，机制等价 MC 雪傀儡入水融化）。
bool EntityManager::aiSnowGolem(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                                float worldW, float worldD, float speedScale)
{
    bool dirty = false;
    // (1) 热伤害累积（机制等价 MC 雪傀儡在热群系 / 入水 / 雨天持续受热伤害直至融化死亡，**非即死**）：
    //    所在格群系为**沙漠**（热）或**雨 / 雷雨降水**或**脚位/身体格在水**（入水融化）→ meltAccum 累加 dt，
    //    达 kSnowMeltInterval → damageEntity(1 HP) + 重置 meltAccum。只读 World::biomeIdAt / weatherStateAt /
    //    blockAt（向下依赖）。mobFeetInWater 判脚位水格（同 aiSquid / 通用 mob 水物理）。
    //    **t552 雪不融雪傀儡**：旧版用 isPrecipitatingAt（= weatherStateAt != Clear）判降水 —— 雪天气 / 雪原 /
    //    山地群系的雪也触发融化（用户报「没打他莫名倒下死掉」）。雪傀儡是雪造的，**雪天不受伤**（见下方代码）。
    //    **die 不立即 return**（旧版 damageEntity 大伤害后 return）—— 改慢扣血后 mob 仍存活 N 秒，须继续走
    //    后续分支（留雪 / 抛雪球 / 游荡）直至血 0 自然死亡（damageEntity 致死时置 dead → tick 死亡态分支接管）。
    if (world) {
        const int gx = qFloor(e.pos.x()), gz = qFloor(e.pos.z());
        const bool hotBiome = (world->biomeIdAt(gx, gz) == 2);        // 2 = Desert（热群系）
        // t552 修「没打他莫名倒下死掉」：旧版用 isPrecipitatingAt（= weatherStateAt != Clear）判降水 —— 它把
        //   **雪**（Snow weather / 雪原 / 山地群系恒 Snow）也当融化源。机制等价 MC 1.0：雪傀儡是**雪造的**，
        //   只在**液态水降水（雨 / 雷雨）**/ 入水 / 热群系才融化；**雪天不受伤**（雪原 / 雪天气正是它的主场）。
        //   改用 weatherStateAt 精确判雨 / 雷雨两类（Weather 编码 1=Rain / 3=Thunder；2=Snow 排除）。
        const int localWeather = world->weatherStateAt(gx, gz);
        const bool raining = (localWeather == 1 || localWeather == 3); // 雨 / 雷雨（液态水降水；Snow=2 不融）
        const bool inWater = mobFeetInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH); // 入水融化
        if (hotBiome || raining || inWater) {
            e.meltAccum += float(dt);
            if (e.meltAccum >= kSnowMeltInterval) {
                e.meltAccum = 0.0f;
                damageEntity(idx, kSnowMeltDamage); // 慢扣血 1HP（满血 4 → ~4s 融化死亡）
                dirty = true;
            }
        } else {
            e.meltAccum = 0.0f; // 离开热/水/雨 → 累积清零（防跨段累积，机制等价 MC 离开热源不再受伤）
        }
    }
    // t529 复盘 ②：t499 二轮曾在此（aiWander 之前）+ 之后各放一段「玩家在 kSnowGolemFaceRange 内 → yawRad 朝玩家」
    //   的持续覆盖（spec 当时「雪傀儡应朝玩家」）。用户反馈「一直固定朝向玩家」不自然（t499 二轮改过头）→ t529
    //   移除持续覆盖，改「生成时固定朝、平时 aiWander 随机朝向」（见 (4) 后注释）。playerPos 参数保留为 caller
    //   签名兼容（tick Mob 分支统一传 listener），但本函数体不再读它（Q_UNUSED 标注防 -Wextra 未用参数警告）。
    Q_UNUSED(playerPos)
    // (2) 行走留雪层（机制等价 MC 1.0 雪傀儡走过留雪脚印 + 雪层被铲后立即重生可无限刷雪球）：
    //    t629 改**放脚下所在格**（golem 正走过 / 正站的格 —— 用户「只在离它最近的一格持续生成」；MC 1.0 雪傀儡
    //    在其所在格铺薄雪层）。旧「放身后格 floor(pos−dir)」偏移根因：yaw 是模型朝向非位移方向（aiWander 随机
    //    选向 1-4s 一换，转身瞬间「身后格」跳到从未走过的格）→ 脚印乱偏；且与 AABB 无关联，斜走时算出的格
    //    既不在足迹上也不在附近。脚下格 = footprint 覆盖格中**离 golem 中心 XZ 最近**的一格（halfW=0.35 →
    //    通常 1 格，跨格取最近 = 「最近的一格」），铺在其脚位空气格（支撑面上方）。
    //    **嵌入自洽**：SnowLayer 贴格底 1/8；放置后落地扫描（t629 mobSupportTopY 按 snowLayerHeight 真顶承接）
    //    自然把 golem 抬到层顶 —— 层顶仅 +1/8，视觉「踩进薄雪」而非旧「悬空一格格 / 整格顶起」。t529 旧
    //    「放脚下打架」的根因正是落地扫描按满格顶承接（mobSolidY+1 恒满格）→ 真顶修复后脚下铺雪自洽。
    //    下方支撑用 isSolid（SnowLayer 非 air → isSolid 含它 → 雪层顶也算实体支撑，脚印可铺在已有雪层
    //    旁边的层顶面上；注意**不会叠层**——写入守卫要求脚下格 blockAt==Air，已有雪层（state=0）的格跳过，
    //    层数增高只发生在塌落合并路径 setSnowLayerMerge（FallingBlock(SnowLayer) 落在既有层上），本 AI 恒铺
    //    1/8 薄层 state=0）。
    //    setWaterSilent 静默写（非玩家破/放 → 免粒子/音/掉落噪音，同羊吃草消耗草丛模式）。
    //    节流：仅当脚下格 blockAt==Air 才写（已铺雪 / 已有方块 → 跳过），无每帧开销。
    if (world) {
        const int fx0 = qFloor(e.pos.x() - e.halfW), fx1 = qFloor(e.pos.x() + e.halfW);
        const int fz0 = qFloor(e.pos.z() - e.halfW), fz1 = qFloor(e.pos.z() + e.halfW);
        const int footY = qFloor(e.pos.y() - e.halfH); // 脚位格（AABB 底面所在格；铺在其空气格里）
        int bx = qFloor(e.pos.x()), bz = qFloor(e.pos.z());
        if (fx1 > fx0 || fz1 > fz0) { // 跨格：取离中心最近格
            float bestD2 = 1e9f;
            for (int gx = fx0; gx <= fx1; ++gx)
                for (int gz = fz0; gz <= fz1; ++gz) {
                    const float ddx = (float(gx) + 0.5f) - e.pos.x();
                    const float ddz = (float(gz) + 0.5f) - e.pos.z();
                    const float d2 = ddx * ddx + ddz * ddz;
                    if (d2 < bestD2) { bestD2 = d2; bx = gx; bz = gz; }
                }
        }
        // 脚下格在界内 + 为空气 + 下方有实体面支撑（SnowLayer isCollidable → 可叠层；水 ShapeNone → 水面不
        //   铺）→ 铺薄雪层（state=0）。审查修 #19（Review 2026-08-23 低危）：旧用 isSolid，t766 铁砧
        //   solid=false 后雪傀儡立铁砧不铺雪；改 R1 口径（a890bfa，同 torchSupportBlock 公式）isCollidable ∨
        //   isFullCube —— 铁砧 ShapeFull 恢复铺雪（isCollidable 的 state 参仅 shape 族判定内部 Q_UNUSED，
        //   blockAt 无 state 传 0 安全）。
        if (footY >= 0 && footY < world->height()
            && world->blockAt(bx, footY, bz) == BlockRegistry::Air
            && (BlockRegistry::isCollidable(world->blockAt(bx, footY - 1, bz), quint8(0))
                || BlockRegistry::isFullCube(world->blockAt(bx, footY - 1, bz)))) {
            world->setWaterSilent(bx, footY, bz, BlockRegistry::SnowLayer, 0);
        }
    }
    // (3) 游荡（同 passive：随机选向 + 时间片）。
    if (aiWander(e, dt, world, worldW, worldD, speedScale)) dirty = true;
    // (4) 远程雪球攻击（机制等价 MC 雪傀儡抛雪球打怪物）：节流（kSnowGolemThrowInterval）扫最近敌对 mob →
    //    fireSnowball（抛物弹丸，命中敌对低伤害 1HP + 减速 kSnowSlowDuration）。只打敌对（nearestHostile 守卫）。
    //    **t558 发球前先面向敌对**：yawRad 朝目标（模型 -Z 正对敌对）再发球 → 修用户报「现在往脑门后面发」
    //    （旧版此块在 aiWander 之前：aiWander 随机选向后，雪球从身侧/身后方向飞出、与模型朝向无关）。
    //    **后置于 aiWander**（lessons t499「面向目标的 yaw 赋值须在 aiWander 之后」）—— aiWander 在
    //    wanderTimer 到期时会把 yawRad 随机化；放其后保「发球帧的视觉朝向 = 目标方向」不被本帧随机选向覆盖。
    e.attackCooldown -= dt;
    if (e.attackCooldown <= 0.0f) {
        const int target = nearestHostile(e.pos, kSnowGolemAttackRange);
        if (target >= 0) {
            const Entity &te = m_entities[size_t(target)];
            const float tdx = te.pos.x() - e.pos.x();
            const float tdz = te.pos.z() - e.pos.z();
            if (tdx * tdx + tdz * tdz > 1e-6f) {
                e.yawRad = std::atan2(-tdx, -tdz); // 面向敌对（dir=(-sin,-cos) 约定，模型 -Z 正对目标）
                dirty = true; // yaw 变 → QML eulerRotation 刷新（发球帧可见转向目标）
            }
            fireSnowball(idx, e, te.pos);
            e.attackCooldown = kSnowGolemThrowInterval;
        }
    }
    // t529 复盘 ②「平时随机朝向」：移除 t499 二轮「玩家在 kSnowGolemFaceRange 内 → yawRad 朝玩家」的持续覆盖。
    //   用户反馈「雪傀儡一直固定朝向玩家」（t499 二轮改过头 —— 持续覆盖使造物视觉朝向恒朝玩家，机制不自然）。
    //   spec t529 明确「生成时固定朝，平时 aiWander 随机朝向」：移除持续覆盖后，yawRad 由 aiWander 在 wanderTimer
    //   到期时随机选向（造物自由游荡，机制等价 MC 造物随机朝向）；生成时 yawRad 初值（spawnMobCore 默认 0）即
    //   「生成时固定朝」（首帧固定，aiWander 首次到期才随机改）。南瓜头刻面眼/嘴正脸只在偶发面向玩家时见到
    //   （机制等价 MC 造物非恒面向玩家）。t558 例外：**发球那一帧** yaw 强制朝敌对（见上 (4)），平时仍随机。
    return dirty;
}

// t483 铁傀儡 AI（详见头文件 aiIronGolem 注释）。机制对齐 MC 1.0 铁傀儡：游荡 + 追击打敌对 + 重拳击退。
//   neutral non-hostile（hostile=false → 不参与黑暗刷怪 / 燃烧 / 远距消失）。**只打敌对**（nearestHostile）。
//   t635 反击玩家分支（golemAngry）：玩家打了它（setGolemRetaliate）→ 优先级**高于敌对 mob 目标**——追击
//   玩家（kIronGolemWalkSpeed）→ 近距（kIronGolemAttackRange）蓄力（kGolemWindup 秒抬臂动画，站立不动）
//   → 蓄满重拳：mobAttackedPlayer(kGolemPlayerDamage) + golemLaunchedPlayer（上抛 ~4.6 格 → 摔落伤害）+
//   重置攻击冷却。玩家脱离 kIronGolemDetectRange / 反击记忆（kGolemAngryMemory）到期 → 平息回常规逻辑。
//   分层（PLAN §2）：只读 World::isSolid + 自身数据；攻击敌对走 damageEntity / knockback（同层），攻击玩家
//   走语义信号 mobAttackedPlayer / golemLaunchedPlayer（呈现层路由 PlayerState / PlayerController）。
//   t663 三修：① 双腿 walkPhase 摆动（MobModel golem 几何 addBoxRot 化）；② 追击（敌对 mob + 反击玩家两
//   分支）加越障跳（前方 1 格墙 + 上方空气 → kJumpSpeed，同 aiHostile；golem 2.4 高 → 查 3 格上）；③ 对
//   mob 重拳改**蓄力 windup**（kGolemWindup 抬臂动画）→ 蓄满命中（对齐反击玩家的攻击节奏 + 动画可见）。
bool EntityManager::aiIronGolem(int idx, Entity &e, float dt, World *world, float worldW, float worldD,
                                float speedScale, const QVector3D &playerPos, bool playerTargetable)
{
    Q_UNUSED(idx) // 铁傀儡攻击目标（target）走 damageEntity/knockback，不读自身 idx（区别于 aiSnowGolem 融化用 idx）
    bool dirty = false;
    e.attackCooldown -= dt; // 重拳冷却递减（不论追踪与否；自然走完）
    // t635 反击记忆倒计时：脱离接触 / 玩家不可锁定（死亡 / 非生存）期间递减，到期平息（golemAngry=false）。
    if (e.golemAngry) {
        e.golemAngryTimer -= dt;
        if (e.golemAngryTimer <= 0.0f) {
            e.golemAngry = false;
            e.golemWindup = 0.0f;
            qCInfo(lcEnt) << "iron golem retaliation expired";
        }
    }
    // ── t635 反击玩家（优先于敌对 mob 目标；玩家打它 → 锁定追击 → 近距蓄力重拳上抛）──
    if (e.golemAngry && playerTargetable) {
        const float pdx = playerPos.x() - e.pos.x();
        const float pdz = playerPos.z() - e.pos.z();
        const float distXZ = std::sqrt(pdx * pdx + pdz * pdz);
        if (distXZ > 1e-4f) e.yawRad = std::atan2(-pdx, -pdz); // 朝玩家（同 yaw 约定 dir=(-sin,-cos)）
        // 脱离侦测范围 → 平息（记忆计继续走，回常规逻辑）。
        if (distXZ > kIronGolemDetectRange) {
            e.golemAngry = false;
            e.golemWindup = 0.0f;
        } else if (distXZ <= kIronGolemAttackRange) {
            // 近距：蓄力（站立不动 + 抬臂动画驱动 golemWindup → golemAttackPoseAt）→ 蓄满重拳。
            e.wanderSpeed = 0.0f;
            e.moveSpeed = 0.0f;
            if (e.attackCooldown <= 0.0f) {
                if (e.golemWindup <= 0.0f) {
                    e.golemWindup = dt; // 起蓄（从本帧已流逝 dt 开始累积 0→kGolemWindup）
                } else {
                    e.golemWindup += dt;
                    if (e.golemWindup >= kGolemWindup) {
                        // 蓄满重拳：大伤害 + 上抛（golemLaunchedPlayer → 玩家 vy=+16 抛起 ~4.6 格摔伤）。
                        e.golemWindup = 0.0f;
                        e.attackCooldown = kIronGolemAttackCooldown;
                        float kbX = 1.0f, kbZ = 0.0f;
                        if (distXZ > 1e-3f) { kbX = pdx / distXZ; kbZ = pdz / distXZ; }
                        else { kbX = -std::sin(e.yawRad); kbZ = -std::cos(e.yawRad); } // 兜底：朝 golem 面朝方向推
                        emit golemLaunchedPlayer(kbX, kbZ);
                        emit mobAttackedPlayer(kGolemPlayerDamage, int(MobIronGolem), kbX, kbZ);
                        qCInfo(lcEnt) << "iron golem heavy punch hit player (launch)";
                    }
                }
            }
            return true; // 蓄力期 revision 须 bump（attackPose 动画绑定刷新，即使站立不动）
        } else {
            // 追击玩家（同下敌对追击的逐轴 AABB 撤回 + 边界 clamp 模式）。
            e.wanderSpeed = kIronGolemWalkSpeed;
            const float spd = kIronGolemWalkSpeed * speedScale;
            // t663 ② 越障跳（同下敌对追击分支；玩家跳 1 格台阶 → golem 也跳，防卡死在台阶下空挥）。
            if (e.resting && world) {
                const float fdx = -std::sin(e.yawRad);
                const float fdz = -std::cos(e.yawRad);
                const int fy = qFloor(e.pos.y() - e.halfH);
                const int fx = qFloor(e.pos.x() + fdx * 0.66f);
                const int fz = qFloor(e.pos.z() + fdz * 0.66f);
                if (fy >= 0
                    && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH)
                    && !world->isSolid(fx, fy + 1, fz)
                    && !world->isSolid(fx, fy + 2, fz)
                    && !world->isSolid(fx, fy + 3, fz)) {
                    e.vy = kJumpSpeed;
                    e.resting = false;
                    // t691：跳起置水平滑流（同 t670 aiHostile 越障跳）——AI 移动节流（每 kAiTickInterval 帧
                    //   一次），翻墙水平前进只有「身体高过墙顶 + 恰逢 AI 帧」才发生 → golem 仍卡台阶下只蹦
                    //   不上；滑流让跳起后每帧朝玩家漂移，高过墙顶即爬升越障。
                    e.jumpGX = (distXZ > 1e-4f ? pdx / distXZ : -std::sin(e.yawRad)) * spd;
                    e.jumpGZ = (distXZ > 1e-4f ? pdz / distXZ : -std::cos(e.yawRad)) * spd;
                }
            }
            const float ehw = e.halfW, ehh = e.halfH;
            const float nx = pdx / distXZ, nz = pdz / distXZ;
            float newX = e.pos.x() + nx * spd * float(dt);
            if (newX < ehw) newX = ehw;
            if (newX > worldW - ehw) newX = worldW - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
            float newZ = e.pos.z() + nz * spd * float(dt);
            if (newZ < ehw) newZ = ehw;
            if (newZ > worldD - ehw) newZ = worldD - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
            bool moved = false;
            if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
            if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
            e.moveSpeed = moved ? spd : 0.0f;
            return moved;
        }
    }
    const int target = nearestHostile(e.pos, kIronGolemDetectRange);
    if (target < 0 && e.golemWindup > 0.0f) {
        // t691：目标消失（死亡 / 消失 / 被移除）→ 清蓄力。否则 golemWindup 残留部分累积值 → 下次刷出
        //   敌对目标的**第一拳**从陈旧蓄力直接蓄满（无前摇瞬发，观感「隔空秒锤」）。golemAngry 分支在
        //   平息两处已各自清（上方），本处覆盖敌对目标路径的目标消失沿。
        e.golemWindup = 0.0f;
    }
    if (target >= 0) {
        const Entity &t = m_entities[size_t(target)];
        const float tdx = t.pos.x() - e.pos.x();
        const float tdz = t.pos.z() - e.pos.z();
        const float distXZ = std::sqrt(tdx * tdx + tdz * tdz);
        if (distXZ > 1e-4f) e.yawRad = std::atan2(-tdx, -tdz); // 朝目标（同 yaw 约定 dir=(-sin,-cos)）
        if (distXZ <= kIronGolemAttackRange) {
            // t663 ③ 对 mob 重拳也走蓄力抬臂（与 t635 反击玩家分支同款 windup → attackPose 动画）：
            //   近距站立蓄力 kGolemWindup 秒（golemWindup 累积 → golemAttackPoseAt 驱动 QML 抬臂）→ 蓄满
            //   重拳命中（高伤害 + 击退，机制等价 MC 铁傀儡对怪上勾拳前摇）。此前直接瞬发伤害 → 用户观感
            //   「打怪没有动画」。冷却未到 → 站立等待（站立不动，不重复起蓄防动画抖动）。
            e.wanderSpeed = 0.0f;
            e.moveSpeed = 0.0f; // 攻击时站立（重拳沉步）
            if (e.attackCooldown <= 0.0f) {
                if (e.golemWindup <= 0.0f) {
                    e.golemWindup = dt; // 起蓄（本帧已流逝 dt 起步累积 0→kGolemWindup）
                } else {
                    e.golemWindup += dt;
                    if (e.golemWindup >= kGolemWindup) {
                        // 蓄满重拳：高伤害 + 击退（沿 golem→mob 方向，机制等价 MC 铁傀儡重拳 + 击退）。
                        e.golemWindup = 0.0f;
                        e.attackCooldown = kIronGolemAttackCooldown;
                        // 目标可能蓄力期间死亡 / 被移除 → 重读槽位校验（idx 越界 / 非 Mob / dead → 放弃本拳）。
                        if (target < int(m_entities.size())) {
                            const Entity &t2 = m_entities[size_t(target)];
                            float kx = 1.0f, kz = 0.0f;
                            if (distXZ > 1e-3f) { kx = tdx / distXZ; kz = tdz / distXZ; }
                            else { kx = -std::sin(e.yawRad); kz = -std::cos(e.yawRad); } // 兜底：朝 golem 面朝方向
                            damageEntity(target, kIronGolemAttackDamage);
                            knockback(target, kx, kz, kIronGolemKnockbackStrength);
                            qCInfo(lcEnt) << "iron golem heavy punch hit mob" << target;
                        }
                    }
                }
            }
            return true; // 蓄力期 revision 须 bump（attackPose 动画绑定刷新，即使站立不动）
        } else {
            // 追击：朝目标走（kIronGolemWalkSpeed，缓慢；逐轴 AABB 撤回 + 边界 clamp，同 aiHostile 追踪移动）。
            e.wanderSpeed = kIronGolemWalkSpeed;
            const float spd = kIronGolemWalkSpeed * speedScale; // t298 水中减速透传
            // t663 ② 越障跳（机制等价 aiHostile：前方脚位是 1 格墙 + 墙顶两格空气 → 跳；台阶卡死根因）。
            //   前方格取脚位 +0.66 偏移（golem 半宽 0.60 + 余量，落在墙格而非自身列）；头位查 3 格上
            //   （golem 2.4 高，翻 1 格墙需上两格皆空气）。多查一层（fy+3）防 2 格身位嵌顶。
            if (e.resting && world) {
                const float fdx = -std::sin(e.yawRad);
                const float fdz = -std::cos(e.yawRad);
                const int fy = qFloor(e.pos.y() - e.halfH);          // 脚位格（mob 底面所在格）
                const int fx = qFloor(e.pos.x() + fdx * 0.66f);
                const int fz = qFloor(e.pos.z() + fdz * 0.66f);
                if (fy >= 0
                    && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH) // 前方脚位是墙（作物格排除，可穿越不跳）
                    && !world->isSolid(fx, fy + 1, fz)                // 墙顶可落（翻上去后脚位）
                    && !world->isSolid(fx, fy + 2, fz)                // 头位可容（golem 2.4 高 → 再上方两格须空气）
                    && !world->isSolid(fx, fy + 3, fz)) {
                    e.vy = kJumpSpeed;
                    e.resting = false; // 解除静止 → 重力分支处理上跳（同 aiHostile 越障跳）
                    // t691：水平滑流（同上反击玩家分支 / t670 aiHostile）——节流 AI 帧外不前进 → 卡台阶下。
                    e.jumpGX = (distXZ > 1e-4f ? tdx / distXZ : -std::sin(e.yawRad)) * spd;
                    e.jumpGZ = (distXZ > 1e-4f ? tdz / distXZ : -std::cos(e.yawRad)) * spd;
                }
            }
            const float ehw = e.halfW, ehh = e.halfH;
            const float nx = tdx / distXZ, nz = tdz / distXZ;
            float newX = e.pos.x() + nx * spd * float(dt);
            if (newX < ehw) newX = ehw;
            if (newX > worldW - ehw) newX = worldW - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
            float newZ = e.pos.z() + nz * spd * float(dt);
            if (newZ < ehw) newZ = ehw;
            if (newZ > worldD - ehw) newZ = worldD - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
            bool moved = false;
            if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
            if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
            e.moveSpeed = moved ? spd : 0.0f; // 撞墙 → 腿停（t241 腿摆频率随它）
            dirty = dirty || moved;
        }
        return dirty;
    }
    // 无敌对目标 → 游荡（同 passive）。
    if (aiWander(e, dt, world, worldW, worldD, speedScale)) dirty = true;
    return dirty;
}

// t1044 蜘蛛爬墙步（MC 原版口径：蜘蛛沿实体方块面垂直爬墙，parity-ledger 裁-2；详见头文件
//   aiSpiderWallClimb 注释）。AI tick 级脉冲（aiHostile 三条追击分支水平位移双轴皆撤回时调）：
//   被阻轴贴面前探列（halfW+0.25，越过 AABB 前沿一个 AI 步长）脚位层或身体层有可碰撞方块
//   （isCollidable 与撤回判定同源）→ vy = kSpiderClimbSpeed + resting 解除 → tick 重力分支积分上升。
//   vy 正值窗（≥kAiTickInterval 帧内 0.61 残速）不触发落地扫描的 vy<0 snap 回弹 → 攀爬单调。
//   天花板由 review25 #4 上浮钳制承接。
//   能力门（阴性轮敏感靶）：仅 MobSpider / MobCaveSpider（t1012③ 同族；MC cave spider 同样爬墙）。
bool EntityManager::aiSpiderWallClimb(Entity &e, World *world, bool blockedX, float nx, bool blockedZ, float nz)
{
    if (!world) return false;
    const bool isClimber = (e.mobType == MobSpider) || (e.mobType == MobCaveSpider); // 阴性轮：false && 前缀摘门
    if (!isClimber) return false;
    const int fy = qFloor(e.pos.y() - e.halfH); // 脚位格（mob AABB 底面所在格，同越障跳口径）
    if (fy < 0 || fy + 1 >= world->height()) return false;
    // 逐被阻轴探可爬面：前探 halfW+0.25（越过 AABB 前沿 0.25 —— 撤回停位点的前沿距墙面的缝隙可达
    // 一个 AI 步长 ≈0.18（逐轴撤回不贴面），固定 0.6 偏移在 halfW=0.45 时临界漏探（t1044 阳轮首跑
    // 实证：蜘蛛停位 +0.6 差 0.004 落回自身列 → 恰不触发）；+0.25 余量对任意 mob 停位稳定命中前柱），
    // 脚位层或身体层任一可碰撞 = 面存在。无被阻轴面（悬空/贴柱角无面）→ 不攀（caller 保持腿停重试）。
    auto faceAhead = [world, fy, halfW = e.halfW](float px, float pz) {
        const int fx = qFloor(px), fz = qFloor(pz);
        return world->isCollidable(fx, fy, fz) || world->isCollidable(fx, fy + 1, fz);
    };
    bool face = false;
    if (blockedX && nx != 0.0f)
        face = faceAhead(e.pos.x() + (nx > 0.0f ? 1.0f : -1.0f) * (e.halfW + 0.25f), e.pos.z());
    if (!face && blockedZ && nz != 0.0f)
        face = faceAhead(e.pos.x(), e.pos.z() + (nz > 0.0f ? 1.0f : -1.0f) * (e.halfW + 0.25f));
    if (!face) return false;
    // 攀爬脉冲：vy 置爬升速 + 解除静止（tick 重力积分上升；vy 正值不触发落地扫描回弹）。
    e.vy = kSpiderClimbSpeed;
    e.resting = false;
    return true;
}

// t281 敌对生物 AI（detect→pathfind→attack；详见头文件 aiHostile 注释）。机制对齐 MC 1.0 僵尸 / 骷髅近战 AI。
//   简化 A* = 贪心方向（直线朝玩家）+ 1 格墙越障跳；非完整 A*（每帧多 mob 跑 A* 开销过大，近战 mob 直线 + 跳够用，
//   平地 / 1 格台阶 / 树根 / 矮墙均能通过；复杂洞穴几何会卡墙，作为基类可接受，留给后续寻路增强）。
//   分层（PLAN §2）：只读 World::isSolid + 自身数据；attack 走语义信号 mobAttackedPlayer（呈现层路由 PlayerState）。
//   t480 idx = 本 mob 槽索引：近战攻击命中玩家时注册驯服狼防御目标（m_wolfTarget = idx，机制等价 MC 驯服狼
//   攻击咬伤主人的怪物）。
bool EntityManager::aiHostile(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                              float worldW, float worldD, float speedScale, float skyBrightness)
{
    // 攻击冷却递减（不论追踪与否；自然走完，复击不卡陈旧值）。钳到 0。
    if (e.attackCooldown > 0.0f) {
        e.attackCooldown -= dt;
        if (e.attackCooldown < 0.0f) e.attackCooldown = 0.0f;
    }

    // t952 小蹒跚者参数分支（快速低伤口径，常量注释即契约）：追击基准速 ×kBabyShamblerChaseSpeedMul
    //   （本函数内全部 kChaseSpeed 消费点统一经 chaseBase 缩放——仇恨狼 / 铁傀儡 / 玩家三条追击路径同参数，
    //   防单点漏改漂移），近战伤害 kBabyShamblerAttackDamage（成体一半档；三条伤害出口同换）。
    //   成体 Shambler/Spider/Silverfish 走原值零变化（mul=1、dmg=kAttackDamage）。
    // t1012③ 洞穴蜘蛛参数分支：追击速与蜘蛛同速（mul=1，MC cave spider 移速 ≡ spider）、近战
    //   kCaveSpiderAttackDamage=2（MC 简单/普通难度 cave spider 2 = 低一档；**毒伤不在引擎侧扣血**——
    //   mobAttackedPlayer 携 mobType → 呈现层 onMobAttackedPlayer 据型挂 applyStatusEffect(EffectPoison)
    //   中毒 DoT，走 t669/t715 既有 m_poisonTimer 链，不新造平行伤害系统）。
    const bool isBabyShambler = (e.mobType == MobBabyShambler);
    const bool isCaveSpider = (e.mobType == MobCaveSpider);
    const float chaseBase = kChaseSpeed * (isBabyShambler ? kBabyShamblerChaseSpeedMul : 1.0f);
    const int meleeDamage = isBabyShambler ? kBabyShamblerAttackDamage
                          : (isCaveSpider ? kCaveSpiderAttackDamage : kAttackDamage);

    // t948 仇恨转移（狼咬敌对 → 被咬者转火攻击咬它的驯服狼；机制等价 MC 1.0 revenge target——被咬敌对
    //   改追攻击狼而非玩家）：注册单一入口 mobAggroAgainst（狼咬击命中处调），本分支消费。目标优先级：
    //   仇恨目标（个人即时仇恨）> golem 视线目标（下方 t712 分支）> 玩家。目标死亡 / 槽复用 →
    //   resolveAggroTarget 清仇恨落回常规路径（同 m_wolfTarget 死亡回落先例）；目标出侦测范围（驯服狼
    //   瞬移回主人身边）→ 本 tick 不转火、落回下方玩家路径，仇恨保留（狼回侦测范围即恢复追狼）。
    //   近距命中调 wolfRetaliateAgainst：敌对近战打驯服狼也注册狼群反击（t923 反击面接线，同骷髅箭先例；
    //   咬击互咬循环下 m_wolfTarget 已是该 mob → 入口去重 no-op，多狼包场景亦正确）。Stalker /
    //   Nightwalker / Emberling 走独立 AI 不进本函数（t712 同界）；t290 门控照旧（玩家不可锁定 → 敌对
    //   AI 整体回退游荡）。追击 / 攻击体例全部照抄下方 golem 分支（逐轴 AABB 撤回 + 越障跳 + 垂直同层）。
    {
        const int aggroIdx = resolveAggroTarget(e);
        if (aggroIdx >= 0) {
            const Entity &t = m_entities[size_t(aggroIdx)];
            const float adx = t.pos.x() - e.pos.x();
            const float adz = t.pos.z() - e.pos.z();
            const float ady = t.pos.y() - e.pos.y();
            const float aDist = std::sqrt(adx * adx + adz * adz);
            if (aDist <= kDetectRange) {
                // 朝仇恨目标（同 golem 分支 yaw 约定 dir=(-sin,-cos)）。
                if (aDist > 1e-4f) e.yawRad = std::atan2(-adx, -adz);
                // 近距攻击（垂直同层门控同玩家路径；冷却到）：damageEntity + knockback（复用受击链）。
                if (aDist <= kAttackRange && std::abs(ady) <= kAttackVertRange && e.attackCooldown <= 0.0f) {
                    // 蓄力期目标可能死亡 / 被移除 → 重读槽位校验（同 golem 分支模式）。
                    if (aggroIdx < int(m_entities.size())) {
                        const Entity &t2 = m_entities[size_t(aggroIdx)];
                        if (t2.alive && t2.kind == Mob && !t2.dead) {
                            e.attackCooldown = kAttackCooldown;
                            float kx = 1.0f, kz = 0.0f;
                            if (aDist > 1e-3f) { kx = adx / aDist; kz = adz / aDist; }
                            else { kx = -std::sin(e.yawRad); kz = -std::cos(e.yawRad); }
                            damageEntity(aggroIdx, meleeDamage);
                            knockback(aggroIdx, kx, kz, 1.0f);
                            // t923 面完整：敌对近战命中驯服狼也注册狼群反击（同骷髅箭接线先例；互咬循环
                            //   下 m_wolfTarget 已是该 mob → 入口去重 no-op，多狼包亦正确）。
                            wolfRetaliateAgainst(aggroIdx, idx);
                            qCInfo(lcEnt) << "hostile mob" << e.mobType
                                          << "attacked aggro target" << aggroIdx;
                        }
                    }
                    e.wanderSpeed = 0.0f;
                    e.moveSpeed = 0.0f;
                    return false; // 贴身攻击 / 攻击瞬间：腿停（同 golem 分支贴身语义）
                }
                if (aDist > kAttackRange) {
                    // 追击仇恨目标：越障跳 + 水平移动（复用 golem 分支逐轴 AABB 撤回 + 边界 clamp 模式）。
                    e.wanderSpeed = chaseBase;
                    const float chaseSpd = chaseBase * speedScale;
                    if (e.resting && world && aDist > 1e-4f) {
                        const float fdx = -std::sin(e.yawRad);
                        const float fdz = -std::cos(e.yawRad);
                        const int fy = qFloor(e.pos.y() - e.halfH);
                        const int fx = qFloor(e.pos.x() + fdx * 0.6f);
                        const int fz = qFloor(e.pos.z() + fdz * 0.6f);
                        if (fy >= 0
                            && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH)
                            && !world->isSolid(fx, fy + 1, fz)
                            && !world->isSolid(fx, fy + 2, fz)) {
                            e.vy = kJumpSpeed;
                            e.resting = false;
                            e.jumpGX = (adx / aDist) * chaseSpd; // t670 越障跳水平滑流（朝仇恨目标）
                            e.jumpGZ = (adz / aDist) * chaseSpd;
                        }
                    }
                    const float ehw = e.halfW, ehh = e.halfH;
                    const float nx = adx / aDist, nz = adz / aDist;
                    float newX = e.pos.x() + nx * chaseSpd * dt;
                    if (newX < ehw) newX = ehw;
                    if (newX > worldW - ehw) newX = worldW - ehw;
                    if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                    float newZ = e.pos.z() + nz * chaseSpd * dt;
                    if (newZ < ehw) newZ = ehw;
                    if (newZ > worldD - ehw) newZ = worldD - ehw;
                    if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                    const bool blockedX = (newX == e.pos.x()) && std::abs(nx) > 1e-4f; // 撤回=被阻（赋值前判）
                    const bool blockedZ = (newZ == e.pos.z()) && std::abs(nz) > 1e-4f;
                    bool moved = false;
                    if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
                    if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
                    // t1044 蜘蛛爬墙：水平位移被实体方块面挡死 → 贴面攀爬脉冲（MC 口径；仅 Spider 家族门内生效）。
                    if (!moved && aiSpiderWallClimb(e, world, blockedX, nx, blockedZ, nz)) moved = true;
                    e.moveSpeed = moved ? chaseSpd : 0.0f;
                    return moved;
                }
                // 已贴身（攻击冷却中）：站立（腿停）。
                e.wanderSpeed = 0.0f;
                e.moveSpeed = 0.0f;
                return false;
            }
            // 出侦测范围：落回下方常规目标路径（仇恨保留 —— resolveAggroTarget 未清）。
        }
    }

    // t712 批「敌对 mob 主动攻击铁傀儡」（机制等价 MC 1.0 僵尸 / 蜘蛛见铁傀儡即转火攻击防御造物）：
    //   侦测范围内有活体铁傀儡 → **优先锁定它**（高于玩家目标；MC 敌对近战对造物天然敌意）。走同款
    //   detect→chase→attack 流程但目标换 mob：朝 golem 走（kChaseSpeed）+ 近距（kAttackRange）冷却到 →
    //   damageEntity + knockback（复用受击链，红闪 / 归零 mobDied 掉落照常）。golem 不在范围（死亡 / 远离）
    //   → 落回下方常规玩家目标路径（chasing 记忆自然衔接）。
    //   注：本分支对 Shambler（蹒跚者）/ Spider（蜘蛛）生效 —— MC 1.0 两族都攻击铁傀儡；Stalker（潜行者）
    //   走独立 aiStalker（只锁玩家，不对造物自爆，spec 明确）；Bones（骷髅弓箭手）走 aiArcher 独立分流。
    const int golemIdx = nearestIronGolem(e.pos, kDetectRange);
    if (golemIdx >= 0) {
        const Entity &g = m_entities[size_t(golemIdx)];
        const float gdx = g.pos.x() - e.pos.x();
        const float gdz = g.pos.z() - e.pos.z();
        const float gdy = g.pos.y() - e.pos.y();
        const float gDist = std::sqrt(gdx * gdx + gdz * gdz);
        // 朝 golem（同 yaw 约定 dir=(-sin,0,-cos)）。
        if (gDist > 1e-4f) e.yawRad = std::atan2(-gdx, -gdz);
        // 近距攻击（垂直同层门控同玩家路径；冷却到）：damageEntity + knockback（朝 golem 推开）。
        if (gDist <= kAttackRange && std::abs(gdy) <= kAttackVertRange && e.attackCooldown <= 0.0f) {
            // 蓄力期间目标可能死亡 / 被移除 → 重读槽位校验（同 aiIronGolem 对 mob 重拳模式）。
            if (golemIdx < int(m_entities.size())) {
                const Entity &g2 = m_entities[size_t(golemIdx)];
                if (g2.alive && g2.kind == Mob && !g2.dead && g2.mobType == MobIronGolem) {
                    e.attackCooldown = kAttackCooldown;
                    float kx = 1.0f, kz = 0.0f;
                    if (gDist > 1e-3f) { kx = gdx / gDist; kz = gdz / gDist; }
                    else { kx = -std::sin(e.yawRad); kz = -std::cos(e.yawRad); }
                    damageEntity(golemIdx, meleeDamage);
                    knockback(golemIdx, kx, kz, 1.0f);
                    qCInfo(lcEnt) << "hostile mob" << e.mobType << "attacked iron golem" << golemIdx;
                }
            }
        }
        if (gDist > kAttackRange) {
            // 追击 golem：越障跳 + 水平移动（复用 aiHostile 追玩家同款逐轴 AABB 撤回 + 边界 clamp）。
            e.wanderSpeed = chaseBase;
            const float chaseSpd = chaseBase * speedScale;
            if (e.resting && world && gDist > 1e-4f) {
                const float fdx = -std::sin(e.yawRad);
                const float fdz = -std::cos(e.yawRad);
                const int fy = qFloor(e.pos.y() - e.halfH);
                const int fx = qFloor(e.pos.x() + fdx * 0.6f);
                const int fz = qFloor(e.pos.z() + fdz * 0.6f);
                if (fy >= 0
                    && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH)
                    && !world->isSolid(fx, fy + 1, fz)
                    && !world->isSolid(fx, fy + 2, fz)) {
                    e.vy = kJumpSpeed;
                    e.resting = false;
                    e.jumpGX = (gdx / gDist) * chaseSpd; // t670 越障跳水平滑流（朝 golem 方向）
                    e.jumpGZ = (gdz / gDist) * chaseSpd;
                }
            }
            const float ehw = e.halfW, ehh = e.halfH;
            const float nx = gdx / gDist, nz = gdz / gDist;
            float newX = e.pos.x() + nx * chaseSpd * dt;
            if (newX < ehw) newX = ehw;
            if (newX > worldW - ehw) newX = worldW - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
            float newZ = e.pos.z() + nz * chaseSpd * dt;
            if (newZ < ehw) newZ = ehw;
            if (newZ > worldD - ehw) newZ = worldD - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
            const bool blockedX = (newX == e.pos.x()) && std::abs(nx) > 1e-4f; // 撤回=被阻（赋值前判）
            const bool blockedZ = (newZ == e.pos.z()) && std::abs(nz) > 1e-4f;
            bool moved = false;
            if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
            if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
            // t1044 蜘蛛爬墙：水平位移被实体方块面挡死 → 贴面攀爬脉冲（MC 口径；仅 Spider 家族门内生效）。
            if (!moved && aiSpiderWallClimb(e, world, blockedX, nx, blockedZ, nz)) moved = true;
            e.moveSpeed = moved ? chaseSpd : 0.0f;
            return moved;
        }
        // 已贴身：站立攻击（腿停）。
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        return false;
    }

    // 玩家相对位置（XZ 距离 + 垂直差）。playerPos = 玩家脚位；e.pos = mob 中心。
    const float dx = playerPos.x() - e.pos.x();
    const float dz = playerPos.z() - e.pos.z();
    const float dy = playerPos.y() - e.pos.y();
    const float distPlayer = std::sqrt(dx * dx + dz * dz);

    // (1) detect：XZ 距离 <= kDetectRange → 进入 / 刷新追踪（chaseTimer 重置记忆期）。脱离则记忆期内续追，过期放弃。
    if (distPlayer <= kDetectRange) {
        e.chasing = true;
        e.chaseTimer = kChaseMemory;
    } else if (e.chasing) {
        e.chaseTimer -= dt;
        if (e.chaseTimer <= 0.0f) { e.chaseTimer = 0.0f; e.chasing = false; }
    }

    // (2) 非追踪 → 回退到 wander（随机游荡，同 passive；mobType 非 sheep 故不吃草分支，纯游荡）。
    //   review0830 #17 登记：白天避光仅罩追击路径 —— 本早退在 (2b) 阴影机之前，白天游荡（玩家脱战）
    //   的亡灵原地照烧不避光（t670 旧版同位，非回归；显式取舍：避光是交战走位策略，游荡态不加装）。
    if (!e.chasing) {
        return aiWander(e, dt, world, worldW, worldD, speedScale); // t298 透传水中减速
    }

    // (2b) t951 白天阴影优先状态机（机制等价 MC 亡灵日间避光；用户口径「白天优先找阴凉保命；等玩家进
    //   阴影才发起攻击」）。抽搐根因（t670 版）：寻影由 e.burning 驱动——进影即停燃 → burning 翻 false →
    //   下方 else 立即清阴凉目标回追玩家 → 一步出影复燃 → 又寻影……追击与避光向量逐 AI tick 交替占优 =
    //   「来回转向走出/退回阴影」。t951 双状态 + 迟滞：
    //     • 暴晒（sunBurnExposureAt 真值——先于燃烧发生，保命不等点燃）→ 移动目标 = 就近遮荫格（t670
    //       findShadeTarget 机制沿用：目标缓存 + kShadeRescanInterval 节流重扫；找不到 → 兜底维持追玩家——
    //       沙漠 / 雪原等无遮蔽 biome 照旧，白昼照烧胜过原地等死，登记取舍）。
    //     • 遮蔽（自身不暴露）且玩家仍暴晒 → 持影停驻 + 压攻击（等玩家进阴影才发起攻击；不追出影）。
    //     • 遮蔽且玩家亦安全（入影 / 入水 / 降水 / 夜间豁免——同一谓词豁免=视同安全）→ 照常追击攻击
    //       （双方都在安全态，追击不会把自己送进日光；玩家逃出遮蔽即回到持影等待）。
    //     • 迟滞（kShadeHoldSeconds，Entity.shadeHoldTimer）：遮蔽即刷新；暴晒衰减；>0 期间「视作遮蔽」
    //       维持当前决策——交战中踏出光影边界 / 天光传播瞬态读数不触发立即 180° 折返（MC 僵尸日间交战
    //       也不秒退）。「追击→寻影」翻向最少隔一个迟滞窗 = 抽搐的结构性解药。
    //   白名单 undeadBurnsInDaylight：只有会晒燃的亡灵做避光（Spider/Silverfish 走本函数但零波及）；
    //   review0830 #6：盔免烧豁免（armorHelmet==0）同式入门 —— 戴盔亡灵「不烧」就也不寻影不压攻击
    //   （免烧者无保命动机；t950 捡盔链放大面随燃烧门同口径闭合）。
    //   夜间（skyBrightness<=kBurnSkyBrightness）整段旁路 → 夜间行为与 t951 前逐位一致。
    //   只罩玩家目标路径：上方仇恨狼 / 铁傀儡转火分支不入场（mob-mob 战斗语义不随日光翻转，t947/t948）。
    float mx = dx, mz = dz; // 移动目标向量（默认朝玩家）
    float mdist = distPlayer;
    bool attackSuppressed = false; // t951：持影等待期压攻击（玩家暴晒未入影 → 不发起攻击）
    const bool dayShadeAi = skyBrightness > kBurnSkyBrightness && undeadBurnsInDaylight(e.mobType)
                            && e.armorHelmet == 0; // review0830 #6：盔免烧豁免同式入门（与燃烧调用点同式）
    if (dayShadeAi) {
        const bool selfExposed = sunBurnExposureAt(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH, skyBrightness);
        if (!selfExposed) {
            e.shadeHoldTimer = kShadeHoldSeconds; // 遮蔽 → 迟滞窗常满（真遮蔽，非残窗）
            e.seekingShade = false;               // 已安全 → 清旧寻影目标（下次暴晒重新就近搜索）
        } else if (e.shadeHoldTimer > 0.0f) {
            e.shadeHoldTimer -= dt;               // 暴晒 → 旧窗衰减（窗内维持当前追击决策，不立即折返）
        }
        const bool sheltered = !selfExposed || e.shadeHoldTimer > 0.0f; // 迟滞后的「视作遮蔽」判定
        if (!sheltered) {
            // 暴晒且迟滞窗尽 → 寻影优先（t670 机制：目标缓存 + 节流重扫；找不到 → 兜底追玩家照旧）。
            e.shadeRescanTimer -= dt;
            if (!e.seekingShade || e.shadeRescanTimer <= 0.0f) {
                int tx = -1, tz = -1;
                if (findShadeTarget(world, qFloor(e.pos.x()), qFloor(e.pos.y()), qFloor(e.pos.z()),
                                    kShadeScanRadius, kShadeSkyLight, &tx, &tz)) {
                    e.seekingShade = true;
                    e.shadeTx = tx; e.shadeTz = tz;
                } else {
                    e.seekingShade = false; // 没搜到遮荫 → 维持追玩家（白昼照烧胜过原地等死；无遮蔽 biome 兜底口径）
                }
                e.shadeRescanTimer = kShadeRescanInterval;
            }
            if (e.seekingShade) {
                const float sx = (float(e.shadeTx) + 0.5f) - e.pos.x();
                const float sz = (float(e.shadeTz) + 0.5f) - e.pos.z();
                mx = sx; mz = sz; mdist = std::sqrt(sx * sx + sz * sz);
                // 入影停驻不在本分支判：!sheltered 蕴含 selfExposed（本格仍暴晒）——自身格翻为不暴露的
                //   下一 AI tick 即走上方 !selfExposed 路径停驻（同谓词单源 = t690「停驻前验证自身格」
                //   升级版；入水/降水=安全同燃烧豁免口径）。迟滞一拍（≤0.07s）的入影过冲可忽略。
            }
        } else if (!selfExposed) {
            // 真遮蔽（非迟滞残窗）→ 持影决策：玩家暴晒 → 停驻 + 压攻击（等玩家进阴影）；玩家安全 →
            //   mx/mz 保持朝玩家（照常追击攻击）。玩家暴露判定同谓词（玩家不燃烧，「暴晒」只是持影等待
            //   的对象）；玩家脚位换算身体中心 +0.9（同 aiArcher 射击目标上身字面量先例）。
            const bool playerExposed = sunBurnExposureAt(world, playerPos.x(), playerPos.y() + 0.9f,
                                                         playerPos.z(), 0.9f, skyBrightness);
            if (playerExposed) {
                mx = 0.0f; mz = 0.0f; mdist = 0.0f;
                attackSuppressed = true;
            }
        }
        // selfExposed && 迟滞残窗：维持默认朝玩家的 mx/mz（交战承诺）；窗尽 → 下个 AI tick 走寻影分支。
    }

    // (3) 追踪：朝移动目标（阴凉方向 / 玩家方向）走 + 越障跳。yaw 朝目标（与 aiWander / player 同 yaw 约定：
    //   dir = (-sin,0,-cos)，使 QML eulerRotation.y=yawDeg 模型 -Z 正对目标）→ yawRad = atan2(-mx, -mz)。
    if (mdist > 1e-4f) {
        e.yawRad = std::atan2(-mx, -mz);
    }
    e.wanderSpeed = chaseBase; // 供 walkPhase 动画频率 + 语义（行走态；raw 值，水中减速不写入此字段避免下游二次缩放）
    // t298 水中减速：chaseSpd = chaseBase × speedScale（位移 + moveSpeed 用 it；wanderSpeed 保 raw；t952 小蹒跚者 chaseBase 含 ×1.4 快速倍率）。
    const float chaseSpd = chaseBase * speedScale;

    // 越障跳：resting（贴地）+ 前方脚位格是 1 格墙（实体）+ 墙顶两格空气（可落 + 头可容，mob ~1.8 高）→ 跳。
    //   不跳的情况：前方无墙（平地直走）/ 墙 ≥2 格（跳不过，正确不跳避免原地蹦）/ 已在空中（resting=false 跳过）。
    //   fdx/fdz = 朝向单位向量；前方格取脚位 +0.6 格偏移（mob 半宽 0.45 + 余量，确保落在墙格而非自身列）。
    //   t642 前方格是作物 → 不跳（isJumpObstacle 排除作物：作物可穿越，mob 应慢走穿过农田而非跳踩作物）。
    //   t670 跳起同时置水平滑流（jumpGX/jumpGZ = 目标方向 × 追击速度）：AI 移动是节流的（每 kAiTickInterval 帧
    //   一次），翻墙的水平前进只有「身体高过墙顶 + 恰逢 AI 帧」才发生 → 玩家高一格时僵尸常被卡墙下（只蹦不上）；
    //   滑流让 mob 跳起后每帧向前漂移，身体一高过墙顶即持续爬升越障 → 落到墙顶（机制等价 MC 跳 1 格台阶）。
    if (e.resting && world && mdist > 1e-4f) {
        const float fdx = -std::sin(e.yawRad);
        const float fdz = -std::cos(e.yawRad);
        const int fy = qFloor(e.pos.y() - e.halfH);          // 脚位格（mob 底面所在格）
        const int fx = qFloor(e.pos.x() + fdx * 0.6f);
        const int fz = qFloor(e.pos.z() + fdz * 0.6f);
        if (fy >= 0
            && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH) // t642 前方脚位是墙（作物格排除，可穿越不跳）
            && !world->isSolid(fx, fy + 1, fz)                // 墙顶可落（mob 翻上去后脚位）
            && !world->isSolid(fx, fy + 2, fz)) {             // 头位可容（mob ~1.8 高，再上方须空气）
            e.vy = kJumpSpeed;
            e.resting = false; // 解除静止 → 本 tick 后段重力分支处理上跳（vy 正）→ 减速 → 下落 → 着地
            e.jumpGX = (mx / mdist) * chaseSpd; // t670 越障跳水平滑流（朝目标方向持续漂移）
            e.jumpGZ = (mz / mdist) * chaseSpd;
        }
    }

    // 水平移动（朝移动目标，逐轴 AABB 碰撞撤回；复用 aiWander 的边界 clamp + mobAabbHitsSolid 全格扫模式 → 贴墙滑动不穿入）。
    bool moved = false;
    if (mdist > 1e-4f) {
        const float ehw = e.halfW; // t252 XZ 半宽（边界 clamp + 碰撞）
        const float ehh = e.halfH; // t252 Y 半高（footprint 格扫）
        const float nx = mx / mdist;
        const float nz = mz / mdist;
        // X 轴：世界边界 clamp + 方块碰撞撤回。
        float newX = e.pos.x() + nx * chaseSpd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        // Z 轴：参照可能已撤回的 newX（两轴顺序敏感）。
        float newZ = e.pos.z() + nz * chaseSpd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        const bool blockedX = (newX == e.pos.x()) && std::abs(nx) > 1e-4f; // 撤回=被阻（赋值前判）
        const bool blockedZ = (newZ == e.pos.z()) && std::abs(nz) > 1e-4f;
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
        // t1044 蜘蛛爬墙：水平位移被实体方块面挡死 → 贴面攀爬脉冲（MC 口径；仅 Spider 家族门内生效）。
        //   爬到墙顶（脚位过顶）水平试探自然解锁 → 越檐平移（登记简化：无独立檐机制）。
        if (!moved && aiSpiderWallClimb(e, world, blockedX, nx, blockedZ, nz)) moved = true;
    }
    e.moveSpeed = moved ? chaseSpd : 0.0f; // 撞墙 → 腿停（moveSpeed=0），但仍在追踪，下帧重试 / 已跳（t298 含水中减速）

    // (4) attack：XZ <= kAttackRange + 垂直同层（|dy|<=kAttackVertRange）+ 单 mob 冷却到 + t321 全局节流到 →
    //   emit mobAttackedPlayer + 重置两者。垂直门控防跨层隔空打（玩家在 mob 头顶 / 脚下不命中）。emit 走语义信号，
    //   呈现层据 Survival 门控应用伤害。t321 节流门控防多 mob 围攻同帧齐抽（详见 kPlayerHitThrottle 注释）——
    //   m_playerHitCooldown>0（其它 mob 刚命中过）→ 本次 attack 不触发（mob 视觉仍挥击但无伤害），等节流过。
    //   t296 击退方向 = (玩家 − mob) XZ 归一（把玩家推开 mob）；distPlayer 极小（贴脸重合）→ 朝 mob 朝向兜底（yaw 约定
    //     dir=(-sin,-cos)），防零向量。dx/dz/distPlayer 已在 (1) 前算好。t951：寻影途中（暴晒态）玩家贴身
    //     仍攻击（t670 近战本能——攻击不解位不致振荡）；持影等待期由 attackSuppressed 压门（见上）。
    if (distPlayer <= kAttackRange && std::abs(dy) <= kAttackVertRange
        && e.attackCooldown <= 0.0f && m_playerHitCooldown <= 0.0f
        && !attackSuppressed) { // t951：持影等待期压攻击（玩家暴晒未入影 → 不发起攻击；夜间恒 false 零变化）
        e.attackCooldown = kAttackCooldown;
        m_playerHitCooldown = kPlayerHitThrottle; // t321 串行化玩家受击（围攻 mob 轮替出手）
        float kbX, kbZ;
        if (distPlayer > 1e-3f) { kbX = dx / distPlayer; kbZ = dz / distPlayer; }
        else { kbX = -std::sin(e.yawRad); kbZ = -std::cos(e.yawRad); } // 兜底：朝 mob 面朝方向（= 推开）
        // t480 主人受击 → 驯服狼攻击本敌对（防御目标 = 咬伤主人的 mob；机制等价 MC 驯服狼报复攻击者）。
        m_wolfTarget = idx;
        emit mobAttackedPlayer(meleeDamage, e.mobType, kbX, kbZ);
        qCInfo(lcEnt) << "hostile mob" << e.mobType << "attacked player for" << meleeDamage << "HP";
    }

    return moved;
}

// t283 骷髅弓箭手 AI（detect→keep-distance→shoot；详见头文件 aiArcher 注释）。机制对齐 MC 1.0 骷髅射手。
//   分层（PLAN §2）：只读 World::isSolid + 自身数据；shoot 走 spawnArrow（箭实体），命中由 Arrow tick 分支
//   发 mobAttackedPlayer 语义信号让呈现层路由 PlayerState（同 aiHostile attack 模式）。
//   t480 idx = 本 mob 槽索引：传给 fireArrow 设箭 arrowShooter（箭命中玩家 → 驯服狼反击发射者）。
bool EntityManager::aiArcher(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                             float worldW, float worldD, float speedScale, float skyBrightness)
{
    // 攻击（射箭）冷却递减（不论追踪与否；自然走完，复射不卡陈旧值）。钳到 0。
    if (e.attackCooldown > 0.0f) {
        e.attackCooldown -= dt;
        if (e.attackCooldown < 0.0f) e.attackCooldown = 0.0f;
    }

    // t948 仇恨转移（骷髅弓手分支；机制等价 MC 1.0 骷髅被狼咬 → 反击目标=狼 → 保持距离射击它）：
    //   注册单一入口 mobAggroAgainst（狼咬击命中处调），本分支消费（目标优先级同 aiHostile：仇恨 >
    //   golem 视线 > 玩家）。侦测范围内有仇恨狼 → 保持距离带（kArcherKeepMin/Max）+ 拉弓瞄准 →
    //   fireArrow 射狼上身；箭命中狼由 tick Arrow 分支的 t923 扩展结算名单（狼在滤网内）结算伤害 +
    //   wolfRetaliateAgainst（狼群反击射手；咬击互咬循环下 m_wolfTarget 已是该骷髅 → 去重 no-op）。
    //   狼出侦测范围（瞬移回主人身边）→ 落回下方 golem/玩家路径，仇恨保留。保持距离 / 越障跳 / 视线
    //   清查 / 拉弓节律全部照抄下方 golem 分支同款；死亡 / 槽复用回落由 resolveAggroTarget 统一处理。
    {
        const int aggroIdx = resolveAggroTarget(e);
        if (aggroIdx >= 0 && aggroIdx < int(m_entities.size())) {
            const Entity &t = m_entities[size_t(aggroIdx)];
            const float tdx = t.pos.x() - e.pos.x();
            const float tdz = t.pos.z() - e.pos.z();
            const float tdy = t.pos.y() - e.pos.y();
            const float tDist = std::sqrt(tdx * tdx + tdz * tdz);
            if (tDist <= kDetectRange) {
                if (tDist > 1e-4f) e.yawRad = std::atan2(-tdx, -tdz); // 朝仇恨狼
                e.wanderSpeed = kChaseSpeed;
                float draw = e.aimTimer > 0.0f ? e.aimTimer / kAimWindup : 0.0f;
                if (draw > 1.0f) draw = 1.0f;
                const float chaseSpd = kChaseSpeed * speedScale * (1.0f - draw);
                // 保持距离（同玩家 / golem 路径：近退远进）。
                float moveDirX = 0.0f, moveDirZ = 0.0f;
                bool wantMove = false;
                if (tDist > 1e-4f) {
                    if (tDist < kArcherKeepMin) { moveDirX = -tdx / tDist; moveDirZ = -tdz / tDist; wantMove = true; }
                    else if (tDist > kArcherKeepMax) { moveDirX = tdx / tDist; moveDirZ = tdz / tDist; wantMove = true; }
                }
                // 越障跳（同玩家路径模式，朝移动方向）。
                if (wantMove && e.resting && world) {
                    const int fy = qFloor(e.pos.y() - e.halfH);
                    const int fx = qFloor(e.pos.x() + moveDirX * 0.7f);
                    const int fz = qFloor(e.pos.z() + moveDirZ * 0.7f);
                    if (fy >= 0
                        && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH)
                        && !world->isSolid(fx, fy + 1, fz)
                        && !world->isSolid(fx, fy + 2, fz)) {
                        e.vy = kJumpSpeed;
                        e.resting = false;
                        e.jumpGX = moveDirX * chaseSpd;
                        e.jumpGZ = moveDirZ * chaseSpd;
                    }
                }
                bool moved = false;
                if (wantMove) {
                    const float ehw = e.halfW, ehh = e.halfH;
                    float newX = e.pos.x() + moveDirX * chaseSpd * dt;
                    if (newX < ehw) newX = ehw;
                    if (newX > worldW - ehw) newX = worldW - ehw;
                    if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                    float newZ = e.pos.z() + moveDirZ * chaseSpd * dt;
                    if (newZ < ehw) newZ = ehw;
                    if (newZ > worldD - ehw) newZ = worldD - ehw;
                    if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                    if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
                    if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
                }
                e.moveSpeed = moved ? chaseSpd : 0.0f;
                // 射箭（同 golem 路径：射程 + 垂直同层 + 冷却到 + 视线清 → 拉弓 kAimWindup 满 → fireArrow）。
                if (tDist <= kArcherShootRange && std::abs(tdy) <= kShootVertRange && e.attackCooldown <= 0.0f) {
                    const QVector3D origin(e.pos.x(), e.pos.y() + e.halfH * 0.5f, e.pos.z());
                    const QVector3D target(t.pos.x(), t.pos.y() + t.halfH * 0.6f, t.pos.z()); // 狼上身
                    e.losCacheTimer -= float(dt);
                    if (e.losCacheTimer <= 0.0f) {
                        e.losClear = lineOfSightClear(world, origin, target);
                        e.losCacheTimer = kLosCacheInterval;
                    }
                    if (e.losClear) {
                        e.aimTimer += float(dt);
                        if (e.aimTimer >= kAimWindup) {
                            // 审查修 B8（t724-t729 复盘）：主循环持 Entity& 期间不直接 fireArrow（pending
                            //   模式同 golem 分支）→ tick 主循环外 flushPendingShots 统一射出。
                            m_pendingArrows.push_back({ target, idx, e.spawnSerial });
                            e.attackCooldown = kShootCooldown;
                            e.aimTimer = 0.0f;
                            qCInfo(lcEnt) << "archer (Bones) fired arrow at aggro wolf" << aggroIdx
                                          << "dist=" << tDist;
                        }
                    } else {
                        e.aimTimer = 0.0f;
                    }
                } else {
                    e.aimTimer = 0.0f;
                }
                return moved;
            }
            // 出侦测范围：落回下方 golem/玩家路径（仇恨保留 —— resolveAggroTarget 未清）。
        }
    }

    // t712 批「敌对 mob 主动攻击铁傀儡」（骷髅弓箭手分支；机制等价 MC 1.0 骷髅见铁傀儡转火射它）：
    //   侦测范围内有活体铁傀儡 → 优先锁定：保持距离（kArcherKeepMin/Max）+ 拉弓瞄准（kAimWindup）→
    //   fireArrow 射 golem 上身。箭命中 mob 由 tick Arrow 分支的骷髅箭 mob 命中（t712 同批加）结算。
    //   golem 不在范围 → 落回下方常规玩家路径。视线清查（lineOfSightClear）同玩家射箭路径复用。
    {
        const int golemIdx = nearestIronGolem(e.pos, kDetectRange);
        if (golemIdx >= 0 && golemIdx < int(m_entities.size())) {
            const Entity &g = m_entities[size_t(golemIdx)];
            const float gdx = g.pos.x() - e.pos.x();
            const float gdz = g.pos.z() - e.pos.z();
            const float gdy = g.pos.y() - e.pos.y();
            const float gDist = std::sqrt(gdx * gdx + gdz * gdz);
            if (gDist > 1e-4f) e.yawRad = std::atan2(-gdx, -gdz); // 朝 golem
            e.wanderSpeed = kChaseSpeed;
            float draw = e.aimTimer > 0.0f ? e.aimTimer / kAimWindup : 0.0f;
            if (draw > 1.0f) draw = 1.0f;
            const float chaseSpd = kChaseSpeed * speedScale * (1.0f - draw);
            // 保持距离（同玩家路径：近退远进）。
            float moveDirX = 0.0f, moveDirZ = 0.0f;
            bool wantMove = false;
            if (gDist > 1e-4f) {
                if (gDist < kArcherKeepMin) { moveDirX = -gdx / gDist; moveDirZ = -gdz / gDist; wantMove = true; }
                else if (gDist > kArcherKeepMax) { moveDirX = gdx / gDist; moveDirZ = gdz / gDist; wantMove = true; }
            }
            // 越障跳（同玩家路径模式，朝移动方向）。
            if (wantMove && e.resting && world) {
                const int fy = qFloor(e.pos.y() - e.halfH);
                const int fx = qFloor(e.pos.x() + moveDirX * 0.7f);
                const int fz = qFloor(e.pos.z() + moveDirZ * 0.7f);
                if (fy >= 0
                    && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH)
                    && !world->isSolid(fx, fy + 1, fz)
                    && !world->isSolid(fx, fy + 2, fz)) {
                    e.vy = kJumpSpeed;
                    e.resting = false;
                    e.jumpGX = moveDirX * chaseSpd;
                    e.jumpGZ = moveDirZ * chaseSpd;
                }
            }
            bool moved = false;
            if (wantMove) {
                const float ehw = e.halfW, ehh = e.halfH;
                float newX = e.pos.x() + moveDirX * chaseSpd * dt;
                if (newX < ehw) newX = ehw;
                if (newX > worldW - ehw) newX = worldW - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                float newZ = e.pos.z() + moveDirZ * chaseSpd * dt;
                if (newZ < ehw) newZ = ehw;
                if (newZ > worldD - ehw) newZ = worldD - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
                if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
            }
            e.moveSpeed = moved ? chaseSpd : 0.0f;
            // 射箭（同玩家路径：射程 + 垂直同层 + 冷却到 + 视线清 → 拉弓 kAimWindup 满 → fireArrow）。
            if (gDist <= kArcherShootRange && std::abs(gdy) <= kShootVertRange && e.attackCooldown <= 0.0f) {
                const QVector3D origin(e.pos.x(), e.pos.y() + e.halfH * 0.5f, e.pos.z());
                const QVector3D target(g.pos.x(), g.pos.y() + g.halfH * 0.6f, g.pos.z()); // golem 上身（2.4 高）
                e.losCacheTimer -= float(dt);
                if (e.losCacheTimer <= 0.0f) {
                    e.losClear = lineOfSightClear(world, origin, target);
                    e.losCacheTimer = kLosCacheInterval;
                }
                if (e.losClear) {
                    e.aimTimer += float(dt);
                    if (e.aimTimer >= kAimWindup) {
                        // 审查修 B8（t724-t729 复盘）：主循环持 Entity& 期间不直接 fireArrow（spawnArrow 可能
                        //   push_back → 引用悬空 UB）→ 记 pending，tick 主循环外 flushPendingShots 统一射出。
                        m_pendingArrows.push_back({ target, idx, e.spawnSerial });
                        e.attackCooldown = kShootCooldown;
                        e.aimTimer = 0.0f;
                        qCInfo(lcEnt) << "archer (Bones) fired arrow at iron golem" << golemIdx << "dist=" << gDist;
                    }
                } else {
                    e.aimTimer = 0.0f;
                }
            } else {
                e.aimTimer = 0.0f;
            }
            return moved;
        }
    }

    const float dx = playerPos.x() - e.pos.x();
    const float dz = playerPos.z() - e.pos.z();
    const float dy = playerPos.y() - e.pos.y();
    const float distXZ = std::sqrt(dx * dx + dz * dz);

    // (1) detect + chase memory（同 aiHostile）：进入 kDetectRange → 追踪 + 刷新记忆；脱离后记忆期内续追。
    if (distXZ <= kDetectRange) {
        e.chasing = true;
        e.chaseTimer = kChaseMemory;
    } else if (e.chasing) {
        e.chaseTimer -= dt;
        if (e.chaseTimer <= 0.0f) { e.chaseTimer = 0.0f; e.chasing = false; }
    }
    if (!e.chasing) {
        // 非追踪 → 回退 wander（随机游荡；mobType 非 sheep 不吃草，纯游荡）。
        //   review0830 #17 登记：白天避光仅罩追击路径 —— 本早退在 (2b) 阴影机之前，白天游荡的骸骨
        //   原地照烧不避光（t670 旧版同位，显式取舍同 aiHostile）。
        return aiWander(e, dt, world, worldW, worldD, speedScale); // t298 透传水中减速
    }

    // (2) 朝向玩家（射箭方向 + 行走方向；dir=(-sin,0,-cos) 同 player/aiHostile yaw 约定）。
    if (distXZ > 1e-4f) e.yawRad = std::atan2(-dx, -dz);
    e.wanderSpeed = kChaseSpeed; // 供 walkPhase 腿摆频率 + 语义（行走态；raw 值，水中减速不写入避免二次缩放）
    // t298 水中减速：chaseSpd = kChaseSpeed × speedScale（位移 + moveSpeed 用 it）。
    // t331 拉弓减速：aimTimer>0（正拉弓瞄准）→ draw=aimTimer/kAimWindup ∈[0,1]，位移再 ×(1−draw) → 拉弓期渐减速
    //   到停（SLOWS + pauses to aim；满拉 draw=1 → chaseSpd=0 停步射）。draw 亦暴露给 QML 抬臂/弦后拉（drawAmountAt）。
    float draw = e.aimTimer > 0.0f ? e.aimTimer / kAimWindup : 0.0f;
    if (draw > 1.0f) draw = 1.0f;
    const float chaseSpd = kChaseSpeed * speedScale * (1.0f - draw);

    // (2b) t951 白天阴影优先状态机（骸骨弓手版；迟滞与谓词同 aiHostile (2b)，契约见该处 + 头文件注释）。
    //   差异面 = 远程攻击不解位：射击不因玩家暴晒压门（「骷髅可在阴影内射箭」——无需接近即无需玩家入影；
    //   射界判定 lineOfSightClear 照旧），白天机器只管**走位**：
    //     • 暴晒且迟滞窗尽 → 寻影优先（覆盖保持距离带——保命高于走位）；入影即停驻。
    //     • 真遮蔽 → 移动候选落点暴晒则弃选（保持带照常运作，但任何一步踏进灼烧日光即放弃该 tick 位移
    //       =「走位不出阴影」；弓手因此可贴在影内对露天玩家照射而自己不再挨晒）。
    //   白名单 / 夜间旁路 / 只罩玩家路径（仇恨狼 / 铁傀儡分支不入场）同 aiHostile。review0830 #6：
    //   盔免烧豁免（armorHelmet==0）同式入门 —— 戴盔骸骨不烧即不避光（候选落点暴晒弃选闸随
    //   dayShadeAi 一并关闭，走位不再滞留阴影）。
    bool seekActive = false;    // 本 tick 以寻影为移动目标（覆盖保持带）
    float seekDirX = 0.0f, seekDirZ = 0.0f;
    const bool dayShadeAi = skyBrightness > kBurnSkyBrightness && undeadBurnsInDaylight(e.mobType)
                            && e.armorHelmet == 0; // review0830 #6：盔免烧豁免同式入门（与燃烧调用点同式）
    if (dayShadeAi) {
        const bool selfExposed = sunBurnExposureAt(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH, skyBrightness);
        if (!selfExposed) {
            e.shadeHoldTimer = kShadeHoldSeconds; // 遮蔽 → 迟滞窗常满
            e.seekingShade = false;               // 已安全 → 清旧寻影目标
        } else if (e.shadeHoldTimer > 0.0f) {
            e.shadeHoldTimer -= dt;               // 暴晒 → 旧窗衰减（走位决策不逐 tick 翻转）
        }
        const bool sheltered = !selfExposed || e.shadeHoldTimer > 0.0f;
        if (!sheltered) {
            e.shadeRescanTimer -= dt;
            if (!e.seekingShade || e.shadeRescanTimer <= 0.0f) {
                int tx = -1, tz = -1;
                if (findShadeTarget(world, qFloor(e.pos.x()), qFloor(e.pos.y()), qFloor(e.pos.z()),
                                    kShadeScanRadius, kShadeSkyLight, &tx, &tz)) {
                    e.seekingShade = true;
                    e.shadeTx = tx; e.shadeTz = tz;
                } else {
                    e.seekingShade = false; // 没搜到遮荫 → 兜底照常保持距离带走位（白昼照烧，登记取舍同 aiHostile）
                }
                e.shadeRescanTimer = kShadeRescanInterval;
            }
            if (e.seekingShade) {
                seekDirX = (float(e.shadeTx) + 0.5f) - e.pos.x();
                seekDirZ = (float(e.shadeTz) + 0.5f) - e.pos.z();
                const float sd = std::sqrt(seekDirX * seekDirX + seekDirZ * seekDirZ);
                if (sd > 1e-4f) { seekDirX /= sd; seekDirZ /= sd; seekActive = true; } // 单位向量 + 寻影态
            }
        }
    }

    // (3) 保持距离：近于 kArcherKeepMin → 朝远离走；远于 kArcherKeepMax → 朝玩家走；其间 → 原地（仅朝向）。
    //   moveDirX/Z = 水平移动单位向量（朝向「期望远离 / 接近」方向）。wantMove=false 表示在保持带内 → 不水平位移。
    float moveDirX = 0.0f, moveDirZ = 0.0f;
    bool wantMove = false;
    if (distXZ > 1e-4f) {
        if (distXZ < kArcherKeepMin) {
            // 太近：朝远离玩家方向（moveDir = (e - player) / dist = (-dx,-dz)/dist）。
            moveDirX = -dx / distXZ;
            moveDirZ = -dz / distXZ;
            wantMove = true;
        } else if (distXZ > kArcherKeepMax) {
            // 太远：朝玩家方向。
            moveDirX = dx / distXZ;
            moveDirZ = dz / distXZ;
            wantMove = true;
        }
    }
    // t951 白天寻影优先覆盖保持带（暴晒且迟滞窗尽 → 保命高于走位）；真遮蔽态的保持带候选落点若在灼烧
    //   日光下 → 弃选该 tick 位移（「走位不出阴影」——一步踏进即被候选闸拦回，弓手滞留影内）。寻影态
    //   不设候选闸（本身就在逃离日光，中间格暴露是路径必然）。白天机器旁路（夜间/非亡灵）时两闸恒不触发。
    if (seekActive) {
        moveDirX = seekDirX;
        moveDirZ = seekDirZ;
        wantMove = true;
    } else if (wantMove && dayShadeAi
               && sunBurnExposureAt(world, e.pos.x() + moveDirX * chaseSpd * dt, e.pos.y(),
                                    e.pos.z() + moveDirZ * chaseSpd * dt, e.halfH, skyBrightness)) {
        wantMove = false; // t951 候选落点暴晒 → 弃选（原地滞留影内，走位不出阴影）
    }

    // 越障跳（仅在要水平移动 + 贴地时；同 aiHostile 越障：前方 1 格墙 + 墙顶 2 格空气 → 跳）。
    //   前方格取 moveDir 方向 0.7 格偏移（mob 半宽 0.45 + 余量，落在前方格而非自身列）。
    //   t642 前方格是作物 → 不跳（isJumpObstacle 排除作物；作物可穿越，同 aiHostile）。
    //   t670 跳起同时置水平滑流（同 aiHostile：翻 1 格墙时持续向前漂移，身体高过墙顶即爬升越障）。
    if (wantMove && e.resting && world) {
        const int fy = qFloor(e.pos.y() - e.halfH);                 // 脚位格
        const int fx = qFloor(e.pos.x() + moveDirX * 0.7f);
        const int fz = qFloor(e.pos.z() + moveDirZ * 0.7f);
        if (fy >= 0
            && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH)    // t642 前方脚位是墙（作物格排除）
            && !world->isSolid(fx, fy + 1, fz)                       // 墙顶可落
            && !world->isSolid(fx, fy + 2, fz)) {                    // 头位可容（mob ~1.8 高）
            e.vy = kJumpSpeed;
            e.resting = false;
            e.jumpGX = moveDirX * chaseSpd; // t670 越障跳水平滑流（朝移动方向）
            e.jumpGZ = moveDirZ * chaseSpd;
        }
    }

    // (4) 水平移动（朝 moveDir，逐轴 AABB 碰撞撤回；复用 aiHostile / aiWander 边界 clamp + 全格扫模式）。
    bool moved = false;
    if (wantMove) {
        const float ehw = e.halfW;
        const float ehh = e.halfH;
        float newX = e.pos.x() + moveDirX * chaseSpd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + moveDirZ * chaseSpd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
    }
    e.moveSpeed = moved ? chaseSpd : 0.0f; // 撞墙 / 保持带内 → 腿停（walkPhase 冻结；t298 含水中减速）

    // (5) shoot：射程内 + 垂直同层 + 视线清 + 冷却到 → t331 先累加 aimTimer（拉弓瞄准），满 kAimWindup 才射 +
    //   重置冷却（防每帧连发）。脱射程 / 视线断 / 冷却中 → 清 aimTimer（中止拉弓，下帧 draw 归 0 → 全速）。
    //   t500 perf：lineOfSightClear（32 步 isSolid march）降到每 kLosCacheInterval 秒复查一次 → chasing 时
    //     march 频率降 ~87%。复查用 losCacheTimer 倒计时（aiDt 推进）；缓存结果 losClear 供间隔内复用。
    //     误差：玩家躲墙后 ≤0.5s 内弓手可能仍判视线清多发一箭（MC 骷髅亦有反应延迟，可接受）。
    if (distXZ <= kArcherShootRange && std::abs(dy) <= kShootVertRange && e.attackCooldown <= 0.0f) {
        const QVector3D origin(e.pos.x(), e.pos.y() + e.halfH * 0.5f, e.pos.z());
        const QVector3D target(playerPos.x(), playerPos.y() + 0.9f, playerPos.z()); // 玩家上身（眼位 ~ 脚+1.62）
        e.losCacheTimer -= float(dt);
        if (e.losCacheTimer <= 0.0f) {
            e.losClear = lineOfSightClear(world, origin, target);
            e.losCacheTimer = kLosCacheInterval;
        }
        if (e.losClear) {
            e.aimTimer += float(dt);
            if (e.aimTimer >= kAimWindup) {
                // 审查修 B8（t724-t729 复盘）：主循环持 Entity& 期间不直接 fireArrow（spawnArrow 可能
                //   push_back → 引用悬空 UB，t283 起既有隐患）→ 记 pending，tick 主循环外 flushPendingShots
                //   统一射出（t480 idx = 发射者槽，箭 arrowShooter 由 fireArrow 内写入不变）。
                m_pendingArrows.push_back({ target, idx, e.spawnSerial });
                e.attackCooldown = kShootCooldown;
                e.aimTimer = 0.0f;
                qCInfo(lcEnt) << "archer (Bones) fired arrow; dist=" << distXZ;
            }
        } else {
            e.aimTimer = 0.0f; // 视线断 → 中止拉弓
        }
    } else {
        e.aimTimer = 0.0f; // 脱射程 / 垂直跨层 / 冷却中 → 不拉弓
    }

    return moved;
}

// t283 视线清查（aiArcher shoot 前调，防穿墙盲射）：从 from 到 to 沿连线 0.5 格步进采样，任一采样点所在格
//   isSolid → 视线被挡返 false。0.5 格步进足以抓 1 格墙。分层：只读 World::isSolid，不向下加依赖。
bool EntityManager::lineOfSightClear(World *world, const QVector3D &from, const QVector3D &to) const
{
    if (!world) return false; // 无世界可查 → 保守不射（caller 安全）
    const QVector3D d = to - from;
    const float len = d.length();
    if (len < 0.5f) return true;                                  // 起终点同格 → 视线清
    const float step = 0.5f;
    for (float t = step; t < len; t += step) {
        const QVector3D p = from + d * (t / len);
        const int bx = qFloor(p.x()), by = qFloor(p.y()), bz = qFloor(p.z());
        if (by >= 0 && world->isSolid(bx, by, bz)) return false;  // 中途被实体方块挡 → 视线不清
    }
    return true;
}

// t283 朝 target 解抛物初速并射箭（aiArcher shoot 段调；详见头文件 fireArrow 注释）。
//   t480 shooterIdx = 发射者（骸骨）槽索引：spawnArrow 返槽后写 arrowShooter —— 箭命中玩家时驯服狼据此
//   反击发射者（主人受击 → 狼攻击射箭的骸骨）。
void EntityManager::fireArrow(int shooterIdx, const Entity &shooter, const QVector3D &target)
{
    // origin = shooter 中心高度 + 朝 target 前移 0.5 格（避免贴墙时箭 spawn 入墙即被 tick 判方块命中）。
    const float dx0 = target.x() - shooter.pos.x();
    const float dz0 = target.z() - shooter.pos.z();
    const float horiz0 = std::sqrt(dx0 * dx0 + dz0 * dz0);
    if (horiz0 < 0.01f) return; // 退化（同格）→ 安全早退（防除零）
    QVector3D origin(shooter.pos.x() + dx0 / horiz0 * 0.5f,
                     shooter.pos.y() + shooter.halfH * 0.5f,
                     shooter.pos.z() + dz0 / horiz0 * 0.5f);
    const float dx = target.x() - origin.x();
    const float dy = target.y() - origin.y();
    const float dz = target.z() - origin.z();
    const float horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz < 0.01f) return;
    const float vH = kArrowSpeed;
    const float t = horiz / vH;                                  // 飞行时间（水平距 / 水平速度）
    // 抛物解：dy = vy·t − 0.5·g·t² → vy = (dy + 0.5·g·t²)/t（命中 target 高度的初速）。
    float vy = (dy + 0.5f * kGravity * t * t) / t;
    if (vy > kArrowMaxVert) vy = kArrowMaxVert;                  // 钳极端弧
    if (vy < -kArrowMaxVert) vy = -kArrowMaxVert;
    float vx = (dx / horiz) * vH;
    float vz = (dz / horiz) * vH;
    // MC 骷髅非 100% 精准 → 三轴 ±kArrowSpread 随机抖动（spread ≪ vH 不改飞行时间量级）。
    auto rnd = []() {
        return (float(QRandomGenerator::global()->bounded(2001)) - 1000.0f) / 1000.0f; // [-1,1]
    };
    vx += rnd() * kArrowSpread;
    vz += rnd() * kArrowSpread;
    vy += rnd() * kArrowSpread;
    const int arrowSlot = spawnArrow(origin, QVector3D(vx, vy, vz));
    if (arrowSlot >= 0 && arrowSlot < int(m_entities.size()))
        m_entities[size_t(arrowSlot)].arrowShooter = shooterIdx; // t480 箭记发射者（驯服狼防御用）
}

// t284 Stalker（潜行者；机制等价 MC 1.0 苦力怕）AI（detect→chase→fuse→detonate；详见头文件 aiStalker 注释）。
//   机制对齐 MC 苦力怕：缓慢逼近 → 近距蓄力（站立膨胀）→ 引爆；玩家逃远熄火。
//   分层（PLAN §2）：只读 World::isSolid/blockAt + 自身数据；爆炸破坏方块走 setWaterSilent、伤害玩家走
//   mobAttackedPlayer 语义信号（呈现层路由 PlayerState），同 aiHostile attack / aiArcher shoot 模式。
//   t480 idx = 本 mob 槽索引：传给 detonateStalker 注册驯服狼防御目标（爆炸伤玩家 → 狼反击 Stalker）。
bool EntityManager::aiStalker(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                              float worldW, float worldD, float speedScale)
{
    const float dx = playerPos.x() - e.pos.x();
    const float dz = playerPos.z() - e.pos.z();
    const float distXZ = std::sqrt(dx * dx + dz * dz);
    Q_UNUSED(distXZ); // t804：flintIgnited 短路路径不读距离（不可逆引信与玩家远近无关）；下方常规链照用

    // t804 打火石点燃（不可逆短引信，机制等价 MC 1.0 flint and steel 点燃苦力怕）：flintIgnited → 无视
    //   追踪 / 距离 / 猫**原地站立蓄力至爆**——玩家逃开（远于 kDefuseRange）/ 猫靠近 / !playerTargetable
    //   都不熄火（区别于近距蓄力链的 defuse / 猫断 / 模式清零，机制等价 MC 点燃后的苦力怕必然爆炸）。
    //   **优先于猫驱赶与追踪判定短路**（那些链都会清 fuseTimer，与不可逆语义冲突）。蓄力进度沿
    //   fuseTimer（近距蓄力中被点燃 → 续走不重置）；inflateAt / QML 膨胀动画 / stalkerFuseLit 嘶声
    //   全复用既有链（tick Mob 分支 flintIgnited 亦每帧 bump revision 驱动绑定刷新）。
    if (e.flintIgnited) {
        if (e.fuseTimer <= 0.0f)
            emit stalkerFuseLit(qFloor(e.pos.x()), qFloor(e.pos.z())); // t616 点燃嘶声（0→正 沿发一次）
        e.fuseTimer += float(dt);
        if (e.fuseTimer >= kFuseTime) {
            detonateStalker(idx, e, world, playerPos); // t480 idx = 本 mob 槽（爆炸伤玩家 → 驯服狼防御目标）
            e.moveSpeed = 0.0f;
            e.wanderSpeed = 0.0f;
            return false;
        }
        e.wanderSpeed = 0.0f; // 蓄力站立（同近距蓄力语义：腿停 moveSpeed=0）
        e.moveSpeed = 0.0f;
        return false;
    }

    // (1) detect + chase memory（同 aiHostile / aiArcher）：进入 kDetectRange → 追踪 + 刷新记忆；脱离后记忆期内续追。
    if (distXZ <= kDetectRange) {
        e.chasing = true;
        e.chaseTimer = kChaseMemory;
    } else if (e.chasing) {
        e.chaseTimer -= dt;
        if (e.chaseTimer <= 0.0f) { e.chaseTimer = 0.0f; e.chasing = false; }
    }

    // t481 豹猫/猫驱赶 Stalker（spec「驱赶 Stalker：近距 Stalker 逃走远离玩家/猫」；机制等价 MC 1.0 苦力怕
    //   被豹猫/猫吓跑）：距本 Stalker kStalkerFleeRange 内有活体豹猫/猫（未驯服豹猫与驯服猫均驱赶，机制等价
    //   MC 1.0 豹猫与猫都吓苦力怕）→ **逃离**：沿背离最近猫方向走 + 熄火（fuseTimer 归零，不蓄力不爆炸）+
    //   优先于追踪 / 蓄力 / 游荡（近猫即逃，不管玩家是否在近旁）。猫离开范围 → 下 AI tick 恢复原行为
    //   （chasing 记忆仍在，恢复追玩家 / 蓄力）。移动逐轴 AABB 撤回 + 边界 clamp（同下方追踪移动模式）。
    const int catIdx = nearestOcelot(e.pos, kStalkerFleeRange);
    if (catIdx >= 0) {
        const Entity &cat = m_entities[size_t(catIdx)];
        const float cdx = e.pos.x() - cat.pos.x(); // 背离猫方向（e − cat）XZ
        const float cdz = e.pos.z() - cat.pos.z();
        const float cdist = std::sqrt(cdx * cdx + cdz * cdz);
        if (cdist > 1e-4f) e.yawRad = std::atan2(-cdx, -cdz); // 同 yaw 约定 dir=(-sin,-cos) → 移动向量 = (e−cat)/d = 背离猫
        e.fuseTimer = 0.0f; // 近猫熄火（不蓄力不爆炸，机制等价 MC 苦力怕被猫吓跑后熄灭）
        e.wanderSpeed = kStalkerFleeSpeed; // 供 walkPhase 腿摆频率 + 语义（逃离行走态；raw 值，水中减速不写入避免二次缩放）
        const float fleeSpd = kStalkerFleeSpeed * speedScale; // t298 水中减速（逃离时落水亦减速，同其他移动路径）
        bool moved = false;
        if (cdist > 1e-4f) {
            const float ehw = e.halfW;
            const float ehh = e.halfH;
            const float nx = cdx / cdist; // 背离猫单位向量
            const float nz = cdz / cdist;
            float newX = e.pos.x() + nx * fleeSpd * float(dt);
            if (newX < ehw) newX = ehw;
            if (newX > worldW - ehw) newX = worldW - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
            float newZ = e.pos.z() + nz * fleeSpd * float(dt);
            if (newZ < ehw) newZ = ehw;
            if (newZ > worldD - ehw) newZ = worldD - ehw;
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
            if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
            if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
        }
        e.moveSpeed = moved ? fleeSpd : 0.0f; // t298 含水中减速
        return moved;
    }

    // (4) fuse：追踪态下据距离蓄力 / 熄火。蓄力中（fuseTimer>0）→ 站立不动（机制等价 MC 苦力怕近距嘶嘶蓄力停步）。
    //   distXZ<=kFuseRange → 累加 fuseTimer（蓄力）；离开蓄力区即泄压（defuse）：中距（kFuseRange<dist<=kDefuseRange）
    //   渐退回常态（非累积，反复进出不强制引爆）；逃远（>kDefuseRange）即归零。非追踪态 → 强制熄火（防脱离后仍蓄力）。
    //   t616：fuseTimer 从 0 翻正（首次点燃）时 emit stalkerFuseLit 一次（呈现层播嘶嘶声；蓄力期间不重复）。
    if (e.chasing) {
        if (distXZ <= kFuseRange) {
            if (e.fuseTimer <= 0.0f)
                emit stalkerFuseLit(qFloor(e.pos.x()), qFloor(e.pos.z())); // t616 点燃嘶声（0→正 沿发一次）
            e.fuseTimer += float(dt);
        } else if (distXZ > kDefuseRange) {
            e.fuseTimer = 0.0f;
        } else {
            e.fuseTimer -= float(dt);
            if (e.fuseTimer < 0.0f) e.fuseTimer = 0.0f;
        }
    } else {
        e.fuseTimer = 0.0f;
    }

    // 朝向玩家（追踪时；射箭方向 + 行走方向。dir=(-sin,0,-cos) 同 player/aiHostile yaw 约定）。
    if (e.chasing && distXZ > 1e-4f) e.yawRad = std::atan2(-dx, -dz);

    // (5) detonate：蓄力满 → 引爆（破坏方块 + 伤害玩家 + emit explosion）+ 标 exploded。caller（tick Mob 分支）
    //   据 exploded 当帧 releaseSlot 移除。detonate 后不再移动 → 直接返 false（moved=false）。
    if (e.fuseTimer >= kFuseTime) {
        detonateStalker(idx, e, world, playerPos); // t480 idx = 本 mob 槽（爆炸伤玩家 → 驯服狼防御目标）
        e.moveSpeed = 0.0f;
        e.wanderSpeed = 0.0f;
        return false;
    }

    // 蓄力中：站立不动（腿停），但仍朝玩家（yaw 已更新）。moveSpeed=0 → walkPhase 冻结。
    if (e.fuseTimer > 0.0f) {
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        return false;
    }

    // 非追踪 → 回退 wander（随机游荡；mobType 非 sheep 不吃草，纯游荡）。
    if (!e.chasing) {
        return aiWander(e, dt, world, worldW, worldD, speedScale); // t298 透传水中减速
    }

    // (3) 追踪但未蓄力：缓慢朝玩家走（kStalkerChaseSpeed）+ 越障跳（同 aiHostile）。
    e.wanderSpeed = kStalkerChaseSpeed; // 供 walkPhase 腿摆频率 + 语义（行走态；raw 值，水中减速不写入避免二次缩放）
    // t298 水中减速：stalkerSpd = kStalkerChaseSpeed × speedScale（位移 + moveSpeed 用 it）。
    const float stalkerSpd = kStalkerChaseSpeed * speedScale;
    if (e.resting && world) {
        const float fdx = -std::sin(e.yawRad);
        const float fdz = -std::cos(e.yawRad);
        const int fy = qFloor(e.pos.y() - e.halfH);
        const int fx = qFloor(e.pos.x() + fdx * 0.6f);
        const int fz = qFloor(e.pos.z() + fdz * 0.6f);
        if (fy >= 0
            && isJumpObstacle(world, fx, fy, fz, e.pos.y() - e.halfH)   // t642 前方脚位是墙（作物格排除，可穿越不跳）
            && !world->isSolid(fx, fy + 1, fz)
            && !world->isSolid(fx, fy + 2, fz)) {
            e.vy = kJumpSpeed;
            e.resting = false;
            // t670 越障跳水平滑流（同 aiHostile / aiArcher：翻 1 格墙时持续向前漂移爬升越障）。
            if (distXZ > 1e-4f) {
                e.jumpGX = (dx / distXZ) * stalkerSpd;
                e.jumpGZ = (dz / distXZ) * stalkerSpd;
            } else {
                e.jumpGX = fdx * stalkerSpd;
                e.jumpGZ = fdz * stalkerSpd;
            }
        }
    }
    bool moved = false;
    if (distXZ > 1e-4f) {
        const float ehw = e.halfW;
        const float ehh = e.halfH;
        const float nx = dx / distXZ;
        const float nz = dz / distXZ;
        float newX = e.pos.x() + nx * stalkerSpd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + nz * stalkerSpd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
    }
    e.moveSpeed = moved ? stalkerSpd : 0.0f; // t298 含水中减速
    return moved;
}

// t727 夜行者 AI（tick 内 hostile mob == MobNightwalker 分支调；详见头文件注释）。机制等价 MC 1.0 末影人。
//   分层同 aiHostile：只读 World::blockAt/isSolid + 自身数据；伤害走 damageEntity / mobAttackedPlayer 语义信号；
//   瞬移只改自身数据（world 只读找落点），无向下写栅格。§9 区隔，零 MC 专名。
bool EntityManager::aiNightwalker(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                                  float worldW, float worldD, float speedScale)
{
    // 计时器递减（不论状态）
    if (e.teleportCooldown > 0.0f) {
        e.teleportCooldown -= dt;
        if (e.teleportCooldown < 0.0f) e.teleportCooldown = 0.0f;
    }
    if (e.attackCooldown > 0.0f) {
        e.attackCooldown -= dt;
        if (e.attackCooldown < 0.0f) e.attackCooldown = 0.0f;
    }

    bool moved = false;

    // ---- (1) 怕水（机制等价 MC 末影人怕水）：身体中心格 / 脚位格 == Water → 扣血 + 瞬移逃离 ----
    //   body 中心格（深水悬浮）+ 脚位格（浅水站立齐腰）任一水 → 判定「碰到水」。timeSlice dt（AI 节流帧传
    //   累积 aiDt）→ 平均每秒扣 1HP（同火烧 burning 模式）。
    //   t829③ 碰水即伤 + 瞬移逃离（用户「当前水中平安无事」的真正根因）：旧版「累计连续满 1s 才扣」在
    //   瞬移先发（kNightwalkerTeleportCooldown 0.6s 冷却、每 aiTick 试跳）的节奏下永远凑不满 1 秒 —— 瞬移
    //   把夜行者带离水域、离水分支又把累积器清零 → 水伤恒死代码。改 MC 语义「末影人碰水即受伤并传送离开」：
    //   **首个接触 tick 即扣 1HP**；其后每持续接触满 kNightwalkerWaterDamageTick 再扣一次（持续浸泡才连续
    //   掉血；瞬移逃离成功即中断）。离水重置 → 每次独立接触都是「首触即伤」。
    const int fx = qFloor(e.pos.x()), fz = qFloor(e.pos.z());
    const int bodyY = qFloor(e.pos.y());                 // mob 中心格（头身中段）
    const int feetY = qFloor(e.pos.y() - e.halfH);       // 脚位格（AABB 底面）
    const bool inWater = (bodyY >= 0 && world->blockAt(fx, bodyY, fz) == BlockRegistry::Water)
                      || (feetY >= 0 && world->blockAt(fx, feetY, fz) == BlockRegistry::Water);
    if (inWater) {
        if (e.waterDamageAccum <= 0.0f) {
            damageEntity(idx, 1); // 首触即伤（红闪 + 归零 mobDied；复用受击链）
            e.waterDamageAccum = kNightwalkerWaterDamageTick; // 距下一次水伤的倒计时
            if (e.dead) return false; // 水伤致死 → 本帧不再动（尸体走死亡动画）
        } else {
            e.waterDamageAccum -= dt; // 持续接触计时（瞬移逃出即被离水分支重置）
            if (e.waterDamageAccum <= 0.0f) {
                damageEntity(idx, 1); // 持续浸泡：每满 kNightwalkerWaterDamageTick 再扣 1HP
                e.waterDamageAccum = kNightwalkerWaterDamageTick;
                if (e.dead) return false;
            }
        }
        if (e.teleportCooldown <= 0.0f
            && teleportEntity(idx, e, world, kNightwalkerTeleportMin, kNightwalkerTeleportMax)) {
            moved = true; // teleportEntity 内已置 cooldown + 清 enraged（逃离打断激怒）
        }
    } else {
        e.waterDamageAccum = 0.0f;
    }

    // ---- (3) 激怒时序（enraged=true）：原地发怒 → 满 delay 瞬移到玩家背后 → 蓄力重拳 ----
    if (e.enraged) {
        e.rageTimer += dt;
        if (e.rageTimer >= kNightwalkerRageTeleportDelay) {
            if (teleportBehindPlayer(idx, e, world, m_playerEye, m_playerLook)) {
                // 瞬移背后成功：清激怒 → 进蓄力段。windupTimer 置极小正触发值（>0 才进入段 (4)；0 会被段 (4)
                //   的 `windupTimer > 0` 守卫跳过 → 永不蓄力出拳的死分支）。段 (4) 首帧 += aiDt 从触发值起累。
                e.enraged = false;
                e.rageTimer = 0.0f;
                e.windupTimer = 1e-6f;
                e.attackCooldown = 0.0f; // 确保蓄力满即可出拳（不被陈旧冷却卡住）
                return true;
            } else {
                // 背后无安全落点（被围 / 贴墙）：放弃本次瞬移，退回 idling（不清 rageTimer 但清 enraged）；
                //   teleportBehindPlayer 内部已清 enraged（防卡在激怒态白吼）。
                e.enraged = false;
                e.rageTimer = 0.0f;
            }
        }
        // 激怒早期（未到瞬移时刻）或瞬移失败：原地发怒不动（QML 据 enragedAt/rageProgressAt 播张嘴+颤抖动画）
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        return false;
    }

    // ---- (4) 蓄力段（windupTimer>0）：瞬移背后后等 kNightwalkerWindup 0.5s 出重拳 ----
    if (e.windupTimer > 0.0f) {
        e.windupTimer += dt;
        if (e.windupTimer >= kNightwalkerWindup) {
            e.windupTimer = 0.0f;
            // 重拳（heavy punch）：近战攻击玩家，伤害 kNightwalkerAttackDamage（spec「重拳 5-6」→ 6 HP）。
            //   复用 aiHostile attack 段：XZ <= kAttackRange + 垂直同层 + 全局节流（t321 串行化，防多 mob 齐抽）。
            const float dx = playerPos.x() - e.pos.x();
            const float dz = playerPos.z() - e.pos.z();
            const float dy = playerPos.y() - e.pos.y();
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist <= kAttackRange && std::abs(dy) <= kAttackVertRange && m_playerHitCooldown <= 0.0f) {
                e.attackCooldown = kAttackCooldown;
                m_playerHitCooldown = kPlayerHitThrottle;
                float kbX, kbZ;
                if (dist > 1e-3f) { kbX = dx / dist; kbZ = dz / dist; }
                else { kbX = -std::sin(e.yawRad); kbZ = -std::cos(e.yawRad); }
                m_wolfTarget = idx; // 重拳伤玩家 → 驯服狼反击本夜行者（同 aiHostile 模式）
                emit mobAttackedPlayer(kNightwalkerAttackDamage, e.mobType, kbX, kbZ);
                qCInfo(lcEnt) << "nightwalker" << idx << "heavy punch player for"
                              << kNightwalkerAttackDamage << "HP";
            }
        }
        // 蓄力期站立不动（等待出拳；windupTimer 未到 0 前原地）
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        return false;
    }

    // ---- (2) 瞪视激怒（enraged=false）：玩家视线眼对眼 + 距 < 32 + 它面朝玩家，持续 2s → 激怒 ----
    //   仅最近一只可激怒（nearestEnragableNightwalker 守卫，防多只齐怒 —— 只能凝视最近那只）。
    if (!e.enraged && m_playerSightValid) {
        const int nearest = nearestEnragableNightwalker(m_playerEye, kNightwalkerStareRange);
        if (nearest == idx) {
            // 玩家视线朝夜行者（头顶点）的点积（眼对眼）：>0.99 = 准星大致正中头颅。
            const QVector3D head = QVector3D(e.pos.x(), e.pos.y() + e.halfH * 0.72f, e.pos.z());
            const QVector3D toHead = head - m_playerEye;
            const float hl = toHead.length();
            float dot = 0.0f;
            if (hl > 1e-4f) {
                const QVector3D u = toHead / hl;
                dot = QVector3D::dotProduct(m_playerLook, u);
            }
            // 水平距离（stare range 用 XZ）：
            const float pdx = e.pos.x() - m_playerEye.x();
            const float pdz = e.pos.z() - m_playerEye.z();
            const float hDist = std::sqrt(pdx * pdx + pdz * pdz);
            // 它面朝玩家（近似：玩家在 mob 前半球，前方锥 dot>0.3；机制等价 MC「对视」需正脸朝向）：
            const float fwdX = -std::sin(e.yawRad), fwdZ = -std::cos(e.yawRad); // mob 前向 dir=(-sin,-cos)
            float mobDot = 0.0f;
            const float toP = hl > 1e-4f ? hl : 1.0f;
            mobDot = (fwdX * (e.pos.x() - m_playerEye.x()) + fwdZ * (e.pos.z() - m_playerEye.z())) / toP;
            if (dot > 0.99f && hDist < kNightwalkerStareRange && mobDot > 0.3f) {
                e.enrageTimer += dt; // 持续瞪视累积
            } else {
                e.enrageTimer = 0.0f; // 视线移开 / 转脸 → 归零
            }
            if (e.enrageTimer >= kNightwalkerStareTime) {
                e.enraged = true;
                e.rageTimer = 0.0f;
                e.windupTimer = 0.0f;
                qCInfo(lcEnt) << "nightwalker" << idx << "ENRAGED by player stare";
            }
        } else {
            e.enrageTimer = 0.0f; // 非最近 / 已有更近的可激怒者 → 不累积
        }
    }

    // ---- idling（非激怒 / 非蓄力 / 不在水）：游荡（aiWander，机制等价 MC 末影人平时中性游荡，不主动追玩家；
    //   瞪视是挑衅入口。速度含 speedScale 水中减速但 Nightwalker 怕水不入水，纯兜底）。----
    return aiWander(e, dt, world, worldW, worldD, speedScale);
}

// t727 最近「可被激怒」的夜行者查找（瞪视激怒守卫；见头文件注释）。只读自身数据，const。
int EntityManager::nearestEnragableNightwalker(const QVector3D &playerEye, float range) const
{
    int best = -1;
    float bestD = range * range;
    for (size_t mi = 0; mi < m_entities.size(); ++mi) {
        const Entity &m = m_entities[mi];
        if (!m.alive || m.kind != Mob || m.dead || m.mobType != MobNightwalker || m.enraged) continue;
        const float dx = m.pos.x() - playerEye.x();
        const float dz = m.pos.z() - playerEye.z();
        const float d2 = dx * dx + dz * dz;
        if (d2 < bestD) { bestD = d2; best = int(mi); }
    }
    return best;
}

// t727 落点是否被夜行者占用（瞬移「非实体」守卫；见头文件注释）。排除 self。const 只读。
bool EntityManager::nightwalkerSpotFree(World *world, float cx, float cy, float cz, float halfW, float halfH,
                                        int selfIdx) const
{
    Q_UNUSED(world);
    for (size_t mi = 0; mi < m_entities.size(); ++mi) {
        if (int(mi) == selfIdx) continue;
        const Entity &m = m_entities[mi];
        if (!m.alive || m.kind != Mob || m.dead) continue;
        if (std::abs(m.pos.x() - cx) < (m.halfW + halfW)
            && std::abs(m.pos.z() - cz) < (m.halfW + halfW)
            && std::abs(m.pos.y() - cy) < (m.halfH + halfH)) {
            return false; // 与其它活体 mob AABB 重叠 → 占位
        }
    }
    return true;
}

// t727 通用瞬移（怕水 / 弹射物 / 近战 dodge 逃逸共用；见头文件注释）。随机 [minDist,maxDist] 水平距离 + 随机
//   方向，迭代找「落点格非水 + 下方 solid 支撑 + AABB 非实体占用」空位。找到 → 移动 + 置 cooldown；仇恨
//   清理由 clearAggro 参数门控（review26 #8）：默认 true（近战 dodge / 水逃逸——近身交互 / 水伤打断激怒是
//   既有设计）；弹射物闪避（箭链 t829① / 浮标 t883）传 false——机制等价 MC 1.0 末影人被投射物闪避**不
//   解除仇恨**：浮标 0 伤害 / 永不钩定 / 回收零耐久，若闪避清 enraged+windupTimer 则成免费无限远程「净化」
//   （激怒→甩竿→打断前摇→循环）；箭虽是消耗品 + 失败兜底有伤害，同样只位移不清仇恨（MC 口径一致）。
bool EntityManager::teleportEntity(int idx, Entity &e, World *world, float minDist, float maxDist,
                                   bool clearAggro)
{
    if (!world) return false;
    auto *rng = QRandomGenerator::global();
    const float wW = float(world->width()), wD = float(world->depth());
    const float loX = e.halfW, hiX = wW - e.halfW, loZ = e.halfW, hiZ = wD - e.halfW;
    for (int attempt = 0; attempt < kNightwalkerTeleportAttempts; ++attempt) {
        const float dist = minDist + rng->bounded(maxDist - minDist); // [min,max)
        const float ang = float(rng->bounded(6283)) / 1000.0f;        // [0, 6.283) rad
        const float nx = std::cos(ang), nz = std::sin(ang);
        const float txF = e.pos.x() + nx * dist;
        const float tzF = e.pos.z() + nz * dist;
        if (txF < loX || txF > hiX || tzF < loZ || tzF > hiZ) continue; // 越界重试
        const int tx = qFloor(txF), tz = qFloor(tzF);
        // 找下方 solid 支撑（从当前高度稍上往下扫，跳进低洼 / 上平台自适应）
        const int startY = qFloor(e.pos.y()) + 1;
        int groundY = -1;
        for (int y = startY; y >= 0; --y) {
            if (world->isSolid(tx, y, tz)) { groundY = y; break; }
        }
        if (groundY < 0) continue; // 悬空（深坑）→ 重试
        // 落点立位（feet 在 groundY+1）：mob 中心 y = groundY+1 + halfH
        const float cy = float(groundY + 1) + e.halfH;
        const int bodyY = qFloor(cy);
        if (bodyY < 0) continue;
        if (world->blockAt(tx, bodyY, tz) == BlockRegistry::Water) continue;     // 身体中段水 → 重试（怕水）
        if (world->blockAt(tx, qFloor(cy - e.halfH), tz) == BlockRegistry::Water) continue; // 脚位水
        // 审查修 B10（t724-t729 复盘）：复查补脚位格 isSolid —— 下方支撑扫描自 startY 向下，若首格即实体
        //   （groundY == startY），脚位格 groundY+1 = startY+1 从未被扫过（下扫不经过）且高个 mob（halfH≥1）
        //   的脚位 < bodyY 也不在 bodyY/bodyY+1 复查内 → 窄情形瞬移落点脚嵌方块（靠窒息扣血兜底）。
        const int footY = qFloor(cy - e.halfH);
        if (world->isSolid(tx, footY, tz) || world->isSolid(tx, bodyY, tz) || world->isSolid(tx, bodyY + 1, tz)) continue; // 脚位 / 立位 / 头位被占
        if (!nightwalkerSpotFree(world, txF, cy, tzF, e.halfW, e.halfH, idx)) continue;    // 撞 mob
        // 落定
        e.pos = QVector3D(txF, cy, tzF);
        e.resting = true;
        e.vy = 0.0f; e.vx = 0.0f; e.vz = 0.0f;
        e.teleportCooldown = kNightwalkerTeleportCooldown;
        if (clearAggro) { e.enraged = false; e.rageTimer = 0.0f; e.windupTimer = 0.0f; } // review26 #8：仅位移的调用方不清
        return true;
    }
    return false; // 全试失败（被围困 / 地形不允许）→ 原地（不移动）
}

// t727 瞬移到玩家背后（激怒满 rageTimer 后调；见头文件注释）。落点 = 玩家眼睛 − 玩家 lookDir×1.6，往下扫贴
//   地支撑；占位 / 水 → 近距随机兜底。
bool EntityManager::teleportBehindPlayer(int idx, Entity &e, World *world, const QVector3D &playerEye,
                                         const QVector3D &playerLook)
{
    if (!world) return false;
    const QVector3D behind = playerEye - playerLook * 1.6f;
    const int tx = qFloor(behind.x()), tz = qFloor(behind.z());
    if (tx < 0 || tz < 0 || tx >= world->width() || tz >= world->depth())
        return teleportEntity(idx, e, world, 1.0f, 6.0f); // 越界 → 近距随机兜底
    const int startY = qFloor(playerEye.y()) + 2;
    int groundY = -1;
    for (int y = startY; y >= 0; --y) {
        if (world->isSolid(tx, y, tz)) { groundY = y; break; }
    }
    if (groundY < 0) return teleportEntity(idx, e, world, 1.0f, 6.0f); // 背后悬空 → 兜底
    const float cy = float(groundY + 1) + e.halfH;
    const int bodyY = qFloor(cy);
    if (world->blockAt(tx, bodyY, tz) == BlockRegistry::Water) return teleportEntity(idx, e, world, 1.0f, 6.0f);
    if (world->isSolid(tx, bodyY, tz) || world->isSolid(tx, bodyY + 1, tz))
        return teleportEntity(idx, e, world, 1.0f, 6.0f);
    if (!nightwalkerSpotFree(world, behind.x(), cy, behind.z(), e.halfW, e.halfH, idx))
        return teleportEntity(idx, e, world, 1.0f, 6.0f);
    // 落定 + 面朝玩家
    e.pos = QVector3D(behind.x(), cy, behind.z());
    const float pdx = playerEye.x() - e.pos.x();
    const float pdz = playerEye.z() - e.pos.z();
    e.yawRad = std::atan2(-pdx, -pdz);
    e.resting = true;
    e.vy = 0.0f; e.vx = 0.0f; e.vz = 0.0f;
    e.teleportCooldown = kNightwalkerTeleportCooldown;
    e.enraged = false; e.rageTimer = 0.0f; e.windupTimer = 0.0f;
    return true;
}

// t727 夜行者近战命中瞬移躲避（PlayerController::attackMob 调；见头文件注释）。调用即躲（30% 概率由 caller 掷）。
//   review27 #5：clearAggro 透传 teleportEntity（默认 true = 近战闪避打断激怒原语义；雪球 / 蛋投掷物链传
//   false——0 伤害投掷物不得成免费远程「净化」，箭 / 浮标 review26 #8 同口径）。
bool EntityManager::nightwalkerDodge(int i, World *world, bool clearAggro)
{
    if (!world) return false;
    if (i < 0 || i >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead || e.mobType != MobNightwalker) return false;
    if (e.teleportCooldown > 0.0f) return false; // 冷却内不连躲（防 spam）
    if (teleportEntity(i, e, world, kNightwalkerTeleportMin, kNightwalkerTeleportMax, clearAggro)) {
        qCInfo(lcEnt) << "nightwalker" << i << "dodged melee via teleport";
        return true;
    }
    return false;
}

// t728 燃烬者 AI（tick 内 hostile mob 且 mobType==MobEmberling 分支调，替代 aiHostile）。机制等价 MC 1.0
//   烈焰人（Blaze）；§9 区隔改名 + 原创模型/贴图。分层同 aiHostile（只读 World + 自身数据 + 语义信号）：
//   悬浮单头游走 + 远程火球，无近战（怕贴脸）。行为按 XZ 水平距 distXZ 分三段：
//   (1) 距 > kEmberlingBackoffDist(3) 且 <= kEmberlingAttackMax(16)：缓慢漂向玩家（hover 慢速 ~1.5 blocks/s，
//       sin 上下浮动由 QML 动画驱动，本 AI 只设水平漂移）+ 周期性喷火球 —— fireCooldown 倒减，归零且在
//       [kEmberlingAttackMin(6), max(16)] 区间 → 朝玩家当前位置直线喷发（spawnFireball，~8 blocks/s）；喷完
//       随机 2.5-4s 冷却。
//   (2) 距 <= kEmberlingBackoffDist(3)：玩家贴脸（怕近战）→ 后退背离玩家漂移。
//   (3) 距 > kEmberlingAttackMax(16)：不追击，轻微 idle 漂移（hover 原地缓慢随机向）。
//   return 是否真位移（驱动 dirty → QML 位置绑定）。idx = 本 mob 槽索引（命中排除 / 驯服狼防御目标用）。
//   火力免疫在 tick 火烧分支另判（不在这里）。
bool EntityManager::aiEmberling(int idx, Entity &e, float dt, World *world, const QVector3D &playerPos,
                                float worldW, float worldD, float speedScale)
{
    // 喷火球冷却递减（不论状态；自然走完，复喷不卡陈旧值）。钳到 0。
    if (e.fireCooldown > 0.0f) {
        e.fireCooldown -= dt;
        if (e.fireCooldown < 0.0f) e.fireCooldown = 0.0f;
    }

    // 玩家相对位置（XZ 水平距）。
    const float dx = playerPos.x() - e.pos.x();
    const float dz = playerPos.z() - e.pos.z();
    const float distXZ = std::sqrt(dx * dx + dz * dz);

    // ---- 水平漂移方向（按距分三段）----
    float mvX = 0.0f, mvZ = 0.0f; // 归一化水平移动单位向量
    bool moving = false;
    if (distXZ > kEmberlingAttackMax) {
        // 段 (3) 远距：不追击 → idle 缓慢随机向漂移（hover 原地游走感）。wanderTimer 到 → 换随机向。
        e.wanderTimer -= dt;
        if (e.wanderTimer <= 0.0f) {
            e.wanderTimer = 1.5f + float(QRandomGenerator::global()->bounded(2000)) / 1000.0f; // 1.5-3.5s
            e.yawRad = float(QRandomGenerator::global()->bounded(6283)) / 1000.0f; // [0, 2π) 随机向
        }
        mvX = -std::sin(e.yawRad); // 沿 yaw 前进（dir=(-sin,0,-cos) 约定）
        mvZ = -std::cos(e.yawRad);
        moving = true;
    } else if (distXZ <= kEmberlingBackoffDist && distXZ > 1e-3f) {
        // 段 (2) 贴脸：后退背离玩家漂移（怕近战；机制等价 MC 烈焰人近战保持距离后退）。
        mvX = -dx / distXZ;
        mvZ = -dz / distXZ;
        moving = true;
    } else if (distXZ > kEmberlingBackoffDist) {
        // 段 (1) 射程（3 < distXZ <= 16）：漂向玩家 + 面朝玩家。
        mvX = dx / distXZ;
        mvZ = dz / distXZ;
        moving = true;
        if (distXZ > 1e-4f) e.yawRad = std::atan2(-mvX, -mvZ);
        // 周期喷火球（仅 [kEmberlingAttackMin, max] 射程区间；fireCooldown 归零触发；喷完随机 [2.5,4]s 冷却）。
        if (e.fireCooldown <= 0.0f && distXZ >= kEmberlingAttackMin) {
            const QVector3D origin(e.pos.x(), e.pos.y(), e.pos.z());
            const QVector3D to(playerPos.x() - origin.x(), playerPos.y() + 0.9f - origin.y(),
                               playerPos.z() - origin.z()); // 朝玩家中心高度（脚位 + 0.9）
            const float tl = to.length();
            if (tl > 1e-3f) {
                const QVector3D dir = to / tl;
                // 审查修 B1（t724-t729 复盘）：出生点沿射向前移出自身命中盒（halfW + 0.5，同玩家掷眼
                //   eye+look*0.5 防贴墙思路）—— 旧版火球生在自身 AABB 正中心，mob 命中盒外扩
                //   kFireballHitHalfW 后首帧仍在发射者盒内。与命中循环 fireballShooter 排除双保险
                //   （机制等价 MC 投射物不命中发射者自身）。
                // 审查修 L15：前移出生点若落进实体格（燃烬者贴墙朝玩家喷）→ 火球首帧 isSolid 方块命中
                //   即消失（视觉「白喷一下」）。生成前与命中判定同口径 isSolid 预检：实体格 → 回退用
                //   发射者中心（燃烬者自身必在空气格 —— 漂移逐轴 AABB 碰撞撤回保证；盒内生成的 mob 自伤
                //   由 fireballShooter 排除兜住，B1 不回归）。
                QVector3D spawnOrigin = origin + dir * (e.halfW + 0.5f);
                if (world->isSolid(qFloor(spawnOrigin.x()), qFloor(spawnOrigin.y()), qFloor(spawnOrigin.z())))
                    spawnOrigin = origin; // 前移点入墙 → 回退发射者中心
                const QVector3D vel = dir * kEmberlingFireballSpeed;
                // 审查修 B8（t724-t729 复盘）：主循环持 Entity& 期间不直接 spawn（acquireSlot 无空槽时
                //   push_back → vector 扩容使循环内引用悬空 UB，t400 繁殖同因先例）→ 记 pending，tick
                //   主循环外 flushPendingShots 统一生成（发射者槽 + 代际由 flush 写入火球供 B1 排除）。
                m_pendingFireballs.push_back({ spawnOrigin, vel, idx, e.spawnSerial });
                qCInfo(lcEnt) << "emberling" << idx << "spit fireball at player dist=" << distXZ;
            }
            e.fireCooldown = kEmberlingFireIntervalMin
                + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f
                * (kEmberlingFireIntervalMax - kEmberlingFireIntervalMin);
        }
    }
    // distXZ 在 (0, 3] 与 (16, ...] 之外（含 3<distXZ<=6 段）已覆盖；玩家同格（distXZ≈0）→ 不水平移动。

    // ---- 水平漂移应用（逐轴 AABB 碰撞撤回 + 边界 clamp，同 aiHostile 追击模式）----
    bool moved = false;
    if (moving) {
        const float hoverSpd = kEmberlingSpeed * speedScale;
        const float ehw = e.halfW, ehh = e.halfH;
        float newX = e.pos.x() + mvX * hoverSpd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + mvZ * hoverSpd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
        e.moveSpeed = moved ? hoverSpd : 0.0f;
    } else {
        e.moveSpeed = 0.0f;
    }
    e.wanderSpeed = moving ? kEmberlingSpeed : 0.0f;
    // 悬浮垂直（hover）保持：Emberling 不落回地表 —— 垂直悬浮由 tick resting / 落地段 kEmberlingHoverOffset
    //   保持（见那里），本 AI 只设水平漂移；上下 sin 浮动由 QML 动画驱动（呈现层）。
    return moved;
}

// t738 爆炸失撑火把掉落（Stalker detonateStalker / TNT detonateTntSphere 两爆炸路径共用；头文件注释
//   详述语义）。实现同 PlayerController::dropUnsupportedTorchesAround 的判定（火把族 state 解码唯一附着
//   格 → 非 solid 即掉）。**反馈通道现状（review25 #16 如实登记，替代 t738 原设想的静默通道叙事）**：
//   destroySphereSilent 每清一格已先在 World::recheckAttachmentsAfterClear ② 内用同判把 6 邻失撑火把
//   掉落（blockBroken + blockDroppedAsItem——每火把一组破块粒子 / 音 / worldEditRev++，t738 头注释原要
//   避免的 spam；有界：每爆炸每火把恰一次，接受现状——给 recheck 加系统事件旁路会分裂两套掉落口径，
//   得不偿失）→ 本函数复扫时 blockAt 已 Air → setWaterSilent + explosionDroppedItem 通道仅在 recheck
//   不及的残余格生效（防御性双保险）。功能正确（双掉被「先清者留 Air、后扫者判跳」挡住）。去重：首扫
//   清格后 blockAt=Air → 破坏列表邻格复扫不再命中火把族 → 不双掉。越界格 blockAt 返 Air 天然跳过。
void EntityManager::dropUnsupportedTorchesAfterBlast(const std::vector<World::DestroyedVoxel> &destroyed,
                                                     World *world)
{
    if (!world) return;
    for (const World::DestroyedVoxel &d : destroyed)
        dropUnsupportedTorchesAroundCell(d.x, d.y, d.z, world); // 审查 #5：收口单格版（逻辑不变，水下路径复用）
}

// t739 爆炸失撑红石粉掉落（Stalker detonateStalker / TNT detonateTntSphere 两爆炸路径共用；头文件注释
//   详述语义）。实现同 PlayerController::dropUnsupportedDustAbove 的判定（正上方粉 + isDustSupport 支撑
//   复检），差异仅在写入口与掉落通道：爆炸是系统事件 → setWaterSilent 静默清（无破块粒子 / 音 spam，
//   同球形破坏口径）+ 恒发 explosionDroppedItem（呈现层 spawnItem，机制等价 MC 爆炸震落粉成物品）。
//   setWaterSilent 已挂 notePowerWrite → 清粉格 + 邻粉入电力脏集，下 tick 红石重算断失效段信号。
void EntityManager::dropUnsupportedDustAfterBlast(const std::vector<World::DestroyedVoxel> &destroyed,
                                                  World *world)
{
    if (!world) return;
    for (const World::DestroyedVoxel &d : destroyed)
        dropUnsupportedDustAboveCell(d.x, d.y, d.z, world); // 审查 #5：收口单格版（逻辑不变，水下路径复用）
}

// t744① 单格版：扫 (x,y,z) 的 6 邻机关族（Lever / WoodButton / StoneButton），state bit[3:1] 解码唯一
//   附着格（mechAttachOffset），非完整立方（isFullCube，与放置预检同源）→ setWaterSilent 清 + 恒发
//   explosionDroppedItem（爆炸系统事件的失撑掉落通道，同 t738/t739 口径：静默清免破块粒子/音 spam、
//   支撑脱落是必然事件不走概率门）。机制等价 MC「机关附着面被移除即脱落」（玩家挖掘版 =
//   PlayerController::dropUnsupportedMechAround，判定同源；本处差异仅写入口与掉落信号——爆炸是
//   系统事件）。机关无碰撞不撑他机关 → 单趟扫即足够（无级联）；清后格 blockAt=Air → 重复扫不双掉。
void EntityManager::dropUnsupportedMechAroundCell(int x, int y, int z, World *world)
{
    if (!world) return;
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &n : kNb) {
        const int tx = x + n[0], ty = y + n[1], tz = z + n[2];
        const quint8 tb = world->blockAt(tx, ty, tz);
        if (tb != BlockRegistry::Lever && tb != BlockRegistry::WoodButton
            && tb != BlockRegistry::StoneButton) continue;
        int ax, ay, az;
        BlockRegistry::mechAttachOffset(world->stateAt(tx, ty, tz), ax, ay, az);
        if (BlockRegistry::isFullCube(world->blockAt(tx + ax, ty + ay, tz + az))) continue; // 支撑幸存 → 保留
        world->setWaterSilent(tx, ty, tz, BlockRegistry::Air, 0); // 静默清（mesh 重建走 worldChanged）
        emit explosionDroppedItem(tx, ty, tz, BlockRegistry::dropId(tb)); // 脱落恒掉（不走概率门）
    }
}

// 审查 #5 单格版（火把族；头文件注释详述）：AfterBlast 逐破坏格 + 水下链式引燃的 clearBlockSilent 后
//   共用。判定与收口前完全一致（state 解码唯一附着格 → 非 solid 即掉；去重：清后 Air 复扫不双掉）。
void EntityManager::dropUnsupportedTorchesAroundCell(int x, int y, int z, World *world)
{
    if (!world) return;
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &n : kNb) {
        const int tx = x + n[0], ty = y + n[1], tz = z + n[2];
        const quint8 tb = world->blockAt(tx, ty, tz);
        if (tb != BlockRegistry::Torch && tb != BlockRegistry::RedstoneTorch) continue;
        int ax, ay, az;
        BlockRegistry::torchAttachOffset(world->stateAt(tx, ty, tz), ax, ay, az);
        if (BlockRegistry::torchSupportBlock(world->blockAt(tx + ax, ty + ay, tz + az), world->stateAt(tx + ax, ty + ay, tz + az))) continue; // 支撑幸存 → 保留（审查修 R1：与放置预检同口径 isCollidable∨isFullCube——旧读 isSolid 会把火把贴刷怪笼（solid=false）误判失撑）
        world->setWaterSilent(tx, ty, tz, BlockRegistry::Air, 0); // 静默清（mesh 重建走 worldChanged）
        emit explosionDroppedItem(tx, ty, tz, BlockRegistry::dropId(tb)); // 脱落恒掉（不走概率门）
    }
}

// 审查 #5 单格版（红石粉；头文件注释详述）：AfterBlast 逐破坏格 + 水下链式引燃共用。判定与收口前完全
//   一致（正上方粉 + isDustSupport 支撑复检；清后 Air 复扫不双掉）。setWaterSilent 已挂 notePowerWrite
//   → 清粉格 + 邻粉入电力脏集，下 tick 红石重算断失效段信号。
void EntityManager::dropUnsupportedDustAboveCell(int x, int y, int z, World *world)
{
    if (!world) return;
    const int ty = y + 1;
    if (ty >= world->height()) return;
    if (!BlockRegistry::isRedstoneDust(world->blockAt(x, ty, z))) return;
    if (BlockRegistry::isDustSupport(world->blockAt(x, y, z), world->stateAt(x, y, z)))
        return; // 支撑幸存（如上半砖在球外 / 双半砖合并）→ 粉保留
    world->setWaterSilent(x, ty, z, BlockRegistry::Air, 0); // 静默清（mesh 重建走 worldChanged）
    emit explosionDroppedItem(x, ty, z, BlockRegistry::dropId(BlockRegistry::RedstoneDust)); // 恒掉
}

// t744① 爆炸失撑机关掉落（头文件注释详述语义）：destroyed 每破坏格调单格版扫 6 邻。覆盖两类失撑源：
//   ① 支撑块被炸掉、机关在球外幸存（按钮贴的墙被炸）；② 支撑格是被链式引燃的 TNT（oldId==TntBlock
//   的破坏格——TNT 转实体后其上的按钮/拉杆立即脱落，机制等价 MC 链式引燃的 TNT 不再是有效附着面）。
void EntityManager::dropUnsupportedMechAfterBlast(const std::vector<World::DestroyedVoxel> &destroyed,
                                                  World *world)
{
    if (!world) return;
    for (const World::DestroyedVoxel &d : destroyed)
        dropUnsupportedMechAroundCell(d.x, d.y, d.z, world);
}

// t774 爆炸伤害 mob（detonateStalker / detonateTntSphere 共用主体；见头文件 damageMobsFromExplosion 注释）。
//   用户报告：「TNT 爆炸之后对生物没有伤害？只有对玩家才有伤害」——旧爆炸路径只 emit mobAttackedPlayer 伤
//   玩家，球内 mob 零伤。本方法补 mob 侧：距离衰减受伤（同玩家侧公式/常量）+ 击退（同量级）+ 死亡走
//   damageEntity 既有 mobDied 掉落链。同层直调（无迭代器失效：damageEntity/knockback 不增删槽）。
void EntityManager::damageMobsFromExplosion(float ex, float ey, float ez, int skipIdx)
{
    if (kExplosionRadius <= 0.0f) return;
    for (int i = 0; i < int(m_entities.size()); ++i) {
        Entity &m = m_entities[size_t(i)];
        if (!m.alive || m.kind != Mob || m.dead || i == skipIdx) continue; // 尸体/非 Mob（掉落物/primed TNT）/自爆源不吃爆炸伤
        // mob 身体中心（e.pos）到爆心 3D 距离（同玩家侧 playerPos.y()+0.9 身体中心采样口径）。
        const float dx = m.pos.x() - ex;
        const float dy = m.pos.y() - ey;
        const float dz = m.pos.z() - ez;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > kExplosionRadius) continue; // 半径外 → 0 伤（同玩家侧）
        int dmg = int(std::round(float(kExplosionDamageMax) * (1.0f - dist / kExplosionRadius)));
        if (dmg < 1) dmg = 1; // 半径内 → 至少 1HP（同玩家侧）
        // 击退方向 = (mob − 爆心) XZ 归一（推离爆心，同玩家侧 applyHitKnockback 方向语义）；退化（正上/正下）→ +X 兜底。
        float kbX = 1.0f, kbZ = 0.0f;
        const float phlen = std::sqrt(dx * dx + dz * dz);
        if (phlen > 1e-3f) { kbX = dx / phlen; kbZ = dz / phlen; }
        damageEntity(i, dmg);                                                    // 扣血 + 红闪 + 归零 dead（→ mobDied 掉落链）
        knockback(i, kbX, kbZ, kExplosionMobKnockbackStrength);                   // 击退冲量（对齐玩家侧爆炸击退 ~6 b/s）
        qCInfo(lcEnt) << "explosion hit mob" << i << "type" << m.mobType
                      << "dmg" << dmg << "dist" << double(dist);
    }
}

// t284 Stalker 爆炸（aiStalker fuse 满时调；详见头文件 detonateStalker 注释）。机制等价 MC 苦力怕球形爆炸。
//   分层（PLAN §2）：向下写 World（setWaterSilent 破坏方块 + worldChanged 重建 mesh）+ 发语义信号
//   （explosion 音/视反馈、mobAttackedPlayer 伤害玩家）；只读 World::blockAt 判定破坏目标。无向上依赖。
//   t480 idx = 本 mob 槽索引：爆炸伤害玩家（dmg>0）时注册驯服狼防御目标（m_wolfTarget = idx）。
void EntityManager::detonateStalker(int idx, Entity &e, World *world, const QVector3D &playerPos)
{
    const float ex = e.pos.x();
    const float ey = e.pos.y();
    const float ez = e.pos.z();
    const int cx0 = qFloor(ex);
    const int cy0 = qFloor(ey);
    const int cz0 = qFloor(ez);

    // (a) 球形破坏方块：以爆炸中心格为原点、ceil(kExplosionRadius) 为半径的立方盒内逐格，距中心 <= 半径才破坏。
    //   跳过 Air（无操作）/ Bedrock（不可破坏）/ Water（不抽干，机制等价 MC 爆炸不毁水体）。走 setWaterSilent
    //   （直写 + worldChanged 重建 mesh，**不**发 blockBroken → 免球形内每块破块粒子 / 音 spam —— 爆炸的音 / 视
    //   反馈由下方 explosion 信号单一入口驱动）。每块 O(1)；半径 3 → 7³=343 格 worst case，可接受（一次性事件）。
    //   t297 水中不破坏方块：水吸收爆炸（机制等价 MC 爆炸射线遇液体即灭 → 不毁地形），整段球形破坏跳过。
    //     玩家伤害 / 音 / 视反馈照发（水中爆炸仍伤玩家、仍有爆炸声 / 迸发，仅地形无损，机制等价 MC）。
    //   t319 修 t297 漏判（水中仍毁地形）：e.pos.y 是 mob 身体【中心】（halfH=0.9 → 身体 1.8 高），t297
    //     只查 blockAt(cx0, cy0=floor(pos.y), cz0) 中心格 —— Stalker 在水中受浮力上浮（头出水面）时中心格
    //     常落在水面之上的空气格 → originInWater=false → 球形破坏照跑（bug）。改【沿身体 Y 列】逐格查水
    //     （feet=floor(pos.y−halfH) .. head=floor(pos.y+halfH−ε)，XZ 取中心格）—— 复用同文件 mobFeetInWater /
    //     tick 水中浮力判定（mobInWater）的同一脚位语义；任一身体格 == Water 即视为水中爆炸。陆地（身体列
    //     全程无水）originInWater=false → 照常破坏，行为不变。
    bool originInWater = false;
    if (world) {
        const int fy = qFloor(ey - e.halfH);             // 脚位格（AABB 底面所在格；同 mobFeetInWater）
        const int hy = qFloor(ey + e.halfH - 1e-3f);     // 头位格（ε 防 AABB 顶恰整数误取上方空气格）
        for (int by = fy; by <= hy && !originInWater; ++by) {
            if (by < 0) continue;                         // Y<0 越界 blockAt 返 Air，跳过免无谓查（同 mobFeetInWater 早返）
            if (world->blockAt(cx0, by, cz0) == BlockRegistry::Water) originInWater = true;
        }
    }
    if (world && !originInWater) {
        // t320 批量破坏：球内所有破坏块走 World::destroySphereSilent 一次收口（N 写 + 1 次 refloodBox +
        //   1 次 emit worldChanged + 1 次 clearAllDirty），替代旧逐块 setWaterSilent 的「重建风暴」
        //   （每块 1× emit + 1× recomputeLightAround → 数十次 mesh 重建请求 / 数十次光场重 flood → 一帧数百 ms）。
        //   返回被破坏块（坐标 + 原 id），caller 据原 id 派生掉落物。
        const auto destroyed = world->destroySphereSilent(cx0, cy0, cz0, kExplosionRadius);
        // t297 爆炸掉落（~50% / 破坏块）：取 BlockRegistry::dropId（Stone→Cobble 等，同玩家挖掘掉落，非原方块 id）
        //   → 概率门控（kExplosionDropChance）→ emit explosionDroppedItem → 呈现层转发 ItemEntityManager.spawnItem
        //   在该格生成掉落实体（机制等价 MC 爆炸把被毁方块弹成物品）。dropId<=0（如 leaves 默认 0 / 矿石须冶炼类）
        //   → 不掉。用 QRandomGenerator（玩家交互掉落的随机性，非 worldgen 确定性范畴 §2-K）。
        for (const World::DestroyedVoxel &d : destroyed) {
            // t494 爬行者爆炸引燃 TNT（用户「爬行者爆炸也可以点燃 TNT，链式反应一个原理」）：destroyed 内
            //   oldId==TntBlock → spawnPrimedTnt（短引信 kChainFuseSec，同 TNT 链式口径）→ 连锁传播。**不**走爆炸
            //   掉落（TNT 被炸引燃而非掉成物品，同 TNT detonateTntSphere 语义）。机制等价 MC 苦力怕爆炸引燃邻接 TNT。
            if (d.oldId == BlockRegistry::TntBlock) {
                const float jit = kPrimedTntFuseJitterSec > 0.0f
                                  ? float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * kPrimedTntFuseJitterSec : 0.0f;
                spawnPrimedTnt(d.x, d.y, d.z, kChainFuseSec + jit); // 短引信（快连锁）
                continue; // 引燃完毕，跳过掉落（TNT 不掉物品）
            }
            const int dropItemId = BlockRegistry::dropId(d.oldId);
            if (dropItemId > 0 && QRandomGenerator::global()->generateDouble() < kExplosionDropChance)
                emit explosionDroppedItem(d.x, d.y, d.z, dropItemId);
        }
        // t738 爆炸失撑火把掉落：支撑块被炸掉、火把本体在球外幸存 → 脱落为掉落物（不悬空残留）。
        dropUnsupportedTorchesAfterBlast(destroyed, world);
        // t739 爆炸失撑红石粉掉落：支撑块被炸掉、粉本体在球外幸存 → 脱落为红石粉物品（不浮空残留；
        //   激活态照样掉，失效段电力即时重算断信号）。
        dropUnsupportedDustAfterBlast(destroyed, world);
        // t744① 爆炸失撑机关掉落：按钮/拉杆的支撑块被炸（或支撑格是被链式引燃转实体的 TNT）→
        //   机关脱落为掉落物（不悬空残留；含 destroyed 内 TntBlock 格 6 邻扫描）。
        dropUnsupportedMechAfterBlast(destroyed, world);
    }

    // (b) 距离衰减伤害玩家：以玩家身体中心（脚位 + ~0.9，机制等价 MC 玩家受击采样身体中部）到爆炸中心计 3D 距离；
    //   半径内 → dmg = round(kExplosionDamageMax·(1 − dist/radius))，至少 1（贴脸必死、远距可存活，机制等价 MC
    //   苦力怕爆炸伤害随距离衰减）。半径外 → 0 不发。emit mobAttackedPlayer → 呈现层仅 Survival 应用（Creative /
    //   Spectator 无伤跳过，机制等价 MC 创造 / 观察者无敌）。
    int dmg = 0;
    if (kExplosionRadius > 0.0f) {
        const float pdx = playerPos.x() - ex;
        const float pdy = (playerPos.y() + 0.9f) - ey;
        const float pdz = playerPos.z() - ez;
        const float pd = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
        if (pd <= kExplosionRadius) {
            dmg = int(std::round(float(kExplosionDamageMax) * (1.0f - pd / kExplosionRadius)));
            if (dmg < 1) dmg = 1; // 半径内 → 至少 1HP（机制等价 MC 爆炸半径内必有伤害）
        }
    }
    // t296 爆炸击退方向 = (玩家 − 爆炸中心) XZ 归一（把玩家炸离 Stalker；机制等价 MC 苦力怕爆炸把玩家推飞）。
    //   用玩家脚位 − 爆炸中心水平向量；退化（玩家恰在爆炸中心正上 / 下）→ 朝 +X 兜底。
    if (dmg > 0) {
        float kbX = 0.0f, kbZ = 0.0f;
        const float dxh = playerPos.x() - ex;
        const float dzh = playerPos.z() - ez;
        const float phlen = std::sqrt(dxh * dxh + dzh * dzh);
        if (phlen > 1e-3f) { kbX = dxh / phlen; kbZ = dzh / phlen; }
        else { kbX = 1.0f; } // 兜底：水平重合 → 朝 +X 推（任意非零向）
        // t480 主人受炸 → 驯服狼攻击本 Stalker（防御目标 = 爆炸伤主的潜行者）。
        m_wolfTarget = idx;
        emit mobAttackedPlayer(dmg, int(MobStalker), kbX, kbZ);
    }

    // (b2) t774 爆炸伤害 mob：半径内活体 mob 同公式距离衰减受伤 + 击退 + 死亡走 mobDied 掉落链（跳过自爆源
    //   idx 本体——已 exploded 待当帧移除，不吃自己的爆炸）。玩家链 (b) 原样保留（防双伤）。水中爆炸照样伤
    //   （originInWater 只跳地形破坏，同玩家侧口径）。爆心 = e.pos（mob 身体中心，与破坏球心 cx0 格心近似）。
    damageMobsFromExplosion(ex, ey, ez, idx);

    // (c) 爆炸音 / 视反馈（单一入口）：emit explosion（呈现层 Connections → AudioManager.playExplosion +
    //   BlockParticles.burstExplosion）。坐标 = 爆炸中心格（粒子在中心迸发；机制等价 MC 爆炸声/光在爆炸点）。
    emit explosion(cx0, cy0, cz0);

    // (d) 标记本实体本帧已引爆 → tick Mob 分支据此当帧 releaseSlot 移除（爆炸即除，不再模拟）。
    e.exploded = true;
    qCInfo(lcEnt) << "Stalker detonated at" << cx0 << cy0 << cz0 << "player dmg" << dmg
                  << (originInWater ? "(in water: no terrain damage)" : "");
}

// t485 TNT 方块爆炸（见 entitymanager.h 头注释；机制等价 MC 1.0 TNT 爆炸）。与 detonateStalker 同源球形破坏 +
//   距离衰减伤玩家 + explosion 音/视，差异：无 mob 实体（TNT 是方块）→ 无 exploded / releaseSlot / 驯服狼目标。
//   playercontroller tick 扫玩家 footprint 格（压力板下垫 TNT）触发本方法。破坏方块走 destroySphereSilent 一次收口
//   （N 写 1 emit，同 Stalker t320 批量模式），破坏块按 kExplosionDropChance 概率 emit explosionDroppedItem（掉落物，
//   同 Stalker t297）。水中不破坏的守卫留简化（TNT 陷阱在密室内不在水中）。
// t490 连锁引燃（spec 验收核心）：本 TNT 引爆时，球内其它 TNT 方块应被引燃（转 PrimedTnt 延时引爆）→ 链式引爆全部。
//   实现委托 detonateTntSphere（公共主体）—— destroyed 列表里 oldId==TntBlock 的格子 spawnPrimedTnt（fuse=Jitter
//   随机错峰）→ 各 PrimedTnt fuse 到 0 再次 detonatePrimedTnt 引爆其球内 TNT，递归连锁。踩沙漠神殿压力板 →
//   3×3 TNT 连锁全爆（大坑 + 战利品箱暴露）。
void EntityManager::detonateTntBlock(int x, int y, int z, World *world, const QVector3D &playerPos)
{
    detonateTntSphere(x, y, z, world, playerPos);
}

// t490 TNT 球形爆炸公共主体（detonateTntBlock 方块路径 + detonatePrimedTnt 实体路径共用；机制等价 MC 1.0 TNT 爆炸
//   + 连锁引燃）。① destroySphereSilent 球形破坏；② 爆炸掉落；③ 链式引燃（destroyed 内 TntBlock 格 → spawnPrimedTnt）；
//   ④ 距离衰减伤玩家 + 击退；⑤ emit explosion（音/视单一入口）。
void EntityManager::detonateTntSphere(int cx, int cy, int cz, World *world, const QVector3D &playerPos)
{
    // t494 水中爆炸守卫（用户「水下 TNT 不破坏方块但能引燃」；机制等价 MC 爆炸射线遇液体即灭 → 不毁地形，同 Stalker
    //   originInWater）：爆炸中心（TNT 格）在液体（Water / Lava）中 → **跳过地形破坏**（不 destroySphereSilent），但
    //   **链式引燃仍生效**（扫球内 TNT 方块 → spawnPrimedTnt，水下 TNT 照常连锁）。玩家伤害 / 音 / 视反馈照发
    //   （水中爆炸仍伤玩家 / 有声光）。origin 判定：查 TNT 所在格 blockAt == Water/Lava（primed TNT 落水后中心在
    //   水格内 → 命中；也覆盖铁桶倒水淹 TNT 后引爆的方块路径）。
    bool originInWater = false;
    if (world && cx >= 0 && cz >= 0 && cx < world->width() && cz < world->depth() && cy >= 0 && cy < world->height()) {
        const quint8 o = world->blockAt(cx, cy, cz);
        originInWater = (o == BlockRegistry::Water || o == BlockRegistry::Lava);
    }

    // (a) 球形破坏方块（仅陆地）：destroySphereSilent 一次收口（跳过 Air / Bedrock / Water / Obsidian，同 Stalker t320）。
    //   返回被破坏块（坐标 + 原 id），caller 据原 id 派生掉落物 + 链式引燃。水中 → 跳过（不毁地形）。
    std::vector<World::DestroyedVoxel> destroyed;
    if (world && !originInWater) destroyed = world->destroySphereSilent(cx, cy, cz, kExplosionRadius);

    // (b) 爆炸掉落（~50% / 破坏块，同 Stalker）：取 BlockRegistry::dropId → 概率门控 → emit explosionDroppedItem。
    //   **链式引燃**（spec t490 验收核心）：destroyed 内 oldId==TntBlock 的格子 → spawnPrimedTnt（fuse=Jitter 随机
    //   错峰）→ 各 PrimedTnt fuse 到 0 再次 detonatePrimedTnt 引爆其球内 TNT，递归连锁引爆全部。
    //   顺序：先掉落 / 引燃（据 destroyed 列表，TNT 方块已被 destroySphereSilent 清为 Air 故不重复破坏），再伤玩家 + 音视。
    //   水中：destroyed 空 → 需**另扫球内 TNT** 完成链式引燃（水下 TNT 连锁不断）。
    for (const World::DestroyedVoxel &d : destroyed) {
        // t493 恢复爆炸**链式引燃**（用户明确要链式传递）：爆炸破坏的 TNT 方块 → spawnPrimedTnt（引燃态实体，
        //   fuse=短引信 kChainFuseSec + jitter 随机错峰 → 快速连锁推进）→ 各 PrimedTnt fuse 到 0 再次引爆 → 递归
        //   连锁传播。机制等价 MC TNT 连锁（链式引燃的 TNT 比手点更短引信，快连锁观感）。**不**走爆炸掉落（TNT
        //   被炸引燃而非掉落成物品，避免「TNT 被炸成掉落物」错乱）。
        if (d.oldId == BlockRegistry::TntBlock) {
            const float jit = kPrimedTntFuseJitterSec > 0.0f
                              ? float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * kPrimedTntFuseJitterSec : 0.0f;
            spawnPrimedTnt(d.x, d.y, d.z, kChainFuseSec + jit); // 短引信（快连锁）
            continue; // 引燃完毕，跳过掉落（TNT 不掉物品）
        }
        const int dropItemId = BlockRegistry::dropId(d.oldId);
        if (dropItemId > 0 && QRandomGenerator::global()->generateDouble() < kExplosionDropChance)
            emit explosionDroppedItem(d.x, d.y, d.z, dropItemId);
    }
    // t738 爆炸失撑火把掉落：支撑块被炸掉、火把本体在球外幸存 → 脱落为掉落物（同 Stalker 路径）。
    dropUnsupportedTorchesAfterBlast(destroyed, world);
    // t739 爆炸失撑红石粉掉落：支撑块被炸掉、粉本体在球外幸存 → 脱落为红石粉物品（同 Stalker 路径）。
    dropUnsupportedDustAfterBlast(destroyed, world);
    // t744① 爆炸失撑机关掉落（同 Stalker 路径）：支撑块被炸 / 支撑格是被链式引燃转实体的 TNT →
    //   按钮/拉杆脱落为掉落物（destroyed 含 TntBlock 格 → 其上机关一并扫掉）。
    dropUnsupportedMechAfterBlast(destroyed, world);
    // 水中链式：destroyed 空（未破坏地形）→ 扫球内 TNT 方块单独引燃（水下 TNT 连锁不断，机制等价 MC 水中 TNT
    //   连锁；不破坏其它方块 / 无掉落）。立方盒 [cx±R, cy±R, cz±R] 逐格距中心 ≤ R 判，同 destroySphereSilent 口径。
    if (originInWater && world) {
        const int r = int(std::ceil(kExplosionRadius));
        for (int dx = -r; dx <= r; ++dx)
            for (int dy = -r; dy <= r; ++dy)
                for (int dz = -r; dz <= r; ++dz) {
                    const int x = cx + dx, y = cy + dy, z = cz + dz;
                    if (x < 0 || y < 0 || z < 0 || x >= world->width() || y >= world->height() || z >= world->depth())
                        continue;
                    const float d2 = float(dx * dx + dy * dy + dz * dz);
                    if (d2 > kExplosionRadius * kExplosionRadius) continue; // 距中心 > 半径 → 跳过
                    if (world->blockAt(x, y, z) != BlockRegistry::TntBlock) continue;
                    const float jit = kPrimedTntFuseJitterSec > 0.0f
                                      ? float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * kPrimedTntFuseJitterSec : 0.0f;
                    world->clearBlockSilent(x, y, z); // 移除 TNT 方块（水中爆炸不毁地形，但 TNT 被引燃转实体）
                    spawnPrimedTnt(x, y, z, kChainFuseSec + jit); // 短引信（水下快连锁）
                    // t744① 水下链式引燃的机关失撑掉落：本路径 destroyed 为空（不毁地形）→ AfterBlast 扫不到，
                    //   但 clearBlockSilent 清 TNT 格同样使其上/旁的按钮/拉杆失撑 → 单格版显式补扫（同语义）。
                    dropUnsupportedMechAroundCell(x, y, z, world);
                    // 审查 #5：同格失撑的火把（贴 TNT 墙）/ 粉（铺 TNT 顶）一并单格补扫——悬空火把残留
                    //   还会照旧供电（powerSourceLevel 只读格子 id），水下链式路径与玩家侧三处点火对齐。
                    dropUnsupportedTorchesAroundCell(x, y, z, world);
                    dropUnsupportedDustAboveCell(x, y, z, world);
                }
    }

    // (c) 距离衰减伤害玩家（同 Stalker：身体中心到爆炸中心 3D 距离 → dmg=round(max·(1−dist/radius))，至少 1）。
    //   TNT 爆炸中心 = TNT 格中心（cx+0.5, cy+0.5, cz+0.5），机制等价 MC TNT 爆炸。半径外 → 0 不发。
    int dmg = 0;
    if (kExplosionRadius > 0.0f) {
        const float ex = float(cx) + 0.5f, ey = float(cy) + 0.5f, ez = float(cz) + 0.5f;
        const float pdx = playerPos.x() - ex;
        const float pdy = (playerPos.y() + 0.9f) - ey;
        const float pdz = playerPos.z() - ez;
        const float pd = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
        if (pd <= kExplosionRadius) {
            dmg = int(std::round(float(kExplosionDamageMax) * (1.0f - pd / kExplosionRadius)));
            if (dmg < 1) dmg = 1; // 半径内 → 至少 1HP
        }
        // 击退方向 = (玩家 − 爆炸中心) XZ 归一（同 Stalker t296）。
        if (dmg > 0) {
            float kbX = 0.0f, kbZ = 0.0f;
            const float dxh = playerPos.x() - ex;
            const float dzh = playerPos.z() - ez;
            const float phlen = std::sqrt(dxh * dxh + dzh * dzh);
            if (phlen > 1e-3f) { kbX = dxh / phlen; kbZ = dzh / phlen; }
            else { kbX = 1.0f; } // 兜底：水平重合 → 朝 +X 推
            // t494 死因区分：TNT 爆炸传 MobTnt 哨兵（非 MobStalker）→ 呈现层映射 PlayerState::Tnt（「被 TNT 炸死」），
            //   区别于潜行者自爆（MobStalker → 「被潜行者炸飞」）。受击音色 / 护甲附魔保护族（t476）按死因分派。
            emit mobAttackedPlayer(dmg, int(MobTnt), kbX, kbZ);
        }
    }

    // (c1b) t774 爆炸伤害 mob（同 Stalker (b2)；TNT 无自爆 mob 源 → skipIdx=-1）：半径内活体 mob 距离衰减受伤 +
    //   击退 + 死亡走 mobDied 掉落链。爆心 = TNT 格中心（cx+0.5, cy+0.5, cz+0.5，同 (c) 玩家侧口径）。玩家链
    //   (c) 原样保留（防双伤）。水中爆炸照样伤（同玩家侧）。
    damageMobsFromExplosion(float(cx) + 0.5f, float(cy) + 0.5f, float(cz) + 0.5f, -1);

    // (c2) t494 爆炸推动 primed TNT 实体（用户「TNT 可爆炸推动点燃的 TNT 飞起」；机制等价 MC 爆炸把邻接 primed
    //   TNT 推开）：扫描活体 primed TNT，中心距爆炸中心（ex,ey,ez）≤ kExplosionRadius → 施加「远离爆炸中心」的
    //   水平冲量（强度随距离衰减）+ 上抛 vy。vx/vz 由 primed tick 积分（见 FallingBlock primed 分支水平段）。
    //   O(n) n≤kCap=64 可忽略。e.resting 解除（primed 无 resting 标志——primed 分支直接积分 vx/vz 无需解 resting）。
    {
        const float ex = float(cx) + 0.5f, ey = float(cy) + 0.5f, ez = float(cz) + 0.5f;
        for (int i = 0; i < int(m_entities.size()); ++i) {
            Entity &t = m_entities[size_t(i)];
            if (!t.alive || t.kind != FallingBlock || !t.primed) continue; // 仅活体 primed TNT
            const float dx = t.pos.x() - ex, dy = t.pos.y() - ey, dz = t.pos.z() - ez;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist <= 0.001f) continue; // 爆炸中心自身（正在引爆的那个，本 tick 已 releaseSlot）
            if (dist > kExplosionRadius) continue;
            const float strength = kExplosionPushSpeed * (1.0f - dist / kExplosionRadius); // 距离衰减冲量
            t.vx += dx / dist * strength; // 远离爆炸中心水平分量
            t.vz += dz / dist * strength;
            t.vy += kExplosionUpSpeed * (1.0f - dist / kExplosionRadius); // 上抛（距离衰减）
            // vx/vz 非零 → primed tick 水平积分（见 FallingBlock primed 分支）；tick 每帧都跑，下帧自然生效。
        }
    }

    // (d) 爆炸音 / 视反馈（单一入口，同 Stalker：呈现层 onExplosion → playExplosion + burstExplosion）。
    emit explosion(cx, cy, cz);
    qCInfo(lcEnt) << "TNT detonated at" << cx << cy << cz << "player dmg" << dmg
                  << "chain tnt primed:" << [destroyed]() {
                         int n = 0; for (const auto &d : destroyed) if (d.oldId == BlockRegistry::TntBlock) ++n; return n;
                     }();
}

// t490 生成 PrimedTnt（引燃态 TNT 实体；见 entitymanager.h 头注释）。复用 FallingBlock kind + blockId=TntBlock
//   + primed=true + fuseTicks。位置存 (x+0.5, y+0.5, z+0.5)。halfW/halfH=0（玩家碰撞跳过 → 可穿过）+ pushable=false
//   + 不查占用（同格可叠多个）。tick FallingBlock 分支据 primed 走 fuse 倒计 → detonatePrimedTnt 引爆。
//   t856：velX/velZ 非零 → 写入 e.vx/e.vz（primed tick 水平积分段 t494 消费）——发射器弹出路径的定向初速；
//   默认 0 → 既有机关点火 / 电力点火 / 链式路径零水平位移行为不变。
void EntityManager::spawnPrimedTnt(int x, int y, int z, float fuseSec, float velX, float velZ)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); primed TNT spawn skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    // t490 非完整方块 + 可穿透 + 可堆叠：halfW/halfH=0 → resolvePlayerPush / mobAabbHitsSolid 等碰撞检测零半宽
    //   → 玩家 / mob AABB 永不与 PrimedTnt 重叠 → 可穿过；spawn 不查占用 → 同格可叠多个 PrimedTnt（各自独立引爆）。
    e.halfW = 0.0f;
    e.halfH = 0.0f;
    e.pushable = false;       // 不被玩家推动（同掉落物 / FallingBlock）
    e.kind = FallingBlock;    // 复用 FallingBlock（重力 + tick FallingBlock 分支）
    e.blockId = BlockRegistry::TntBlock; // 携带 TNT 方块 id（QML delegate BlockCube 据它取 TNT 贴图）
    e.primed = true;          // 标 PrimedTnt（tick 走 fuse 倒计而非着地放置）
    e.fuse = (fuseSec > 0.0f) ? fuseSec : kPrimedTntFuseSec; // 引信（caller 传链式错峰 / 默认 5s）
    if (velX != 0.0f || velZ != 0.0f) { // t856 定向初速（默认 0 跳过 → 既有路径行为不变）
        e.vx = velX;
        e.vz = velZ;
    }
    acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    notifyEntitiesChanged();
    qCInfo(lcEnt) << "spawned primed TNT at" << x << y << z << "fuse" << e.fuse << "s"
                  << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// t490 引爆 PrimedTnt（见 entitymanager.h 头注释；机制等价 MC 1.0 TNT 爆炸）。委托 detonateTntSphere（公共主体，
//   球形破坏 + 链式引燃 + 衰减伤玩家 + explosion 音/视）+ releaseSlot 移除实体。idx = PrimedTnt 槽索引。
void EntityManager::detonatePrimedTnt(int idx, World *world, const QVector3D &playerPos)
{
    if (idx < 0 || idx >= int(m_entities.size())) return;
    const Entity &e = m_entities[size_t(idx)];
    if (!e.alive || !e.primed) return; // 非活体 / 非 PrimedTnt → 静默（防误调）
    const int cx = qFloor(e.pos.x()), cy = qFloor(e.pos.y()), cz = qFloor(e.pos.z());
    releaseSlot(idx); // 先释放槽（爆炸即除，不再模拟；releaseSlot 不 erase 保 count 单调，同 t256）
    detonateTntSphere(cx, cy, cz, world, playerPos); // 球形爆炸 + 链式引燃 + 伤玩家 + 音视
    notifyEntitiesChanged(); // releaseSlot 改了槽位 → bump revision 让 QML delegate 隐藏空槽（aliveAt 翻 false）
}

// t490 第 i 个实体是否 PrimedTnt（kind==FallingBlock && primed && alive）。QML delegate 据它对 FallingBlock 叠白闪脉冲。
bool EntityManager::isPrimedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.alive && e.kind == FallingBlock && e.primed;
}

// t490 第 i 个 PrimedTnt 的引信进度（0..1，1=刚点燃、0=即将引爆）。QML delegate 据它驱动白闪脉冲频率（频率随 fuse
//   减少加速，机制等价 MC TNT 引信将尽时闪烁加快）。越界 / 非 primed → 0。
float EntityManager::fuseProgressAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive || !e.primed || e.fuse <= 0.0f) return 0.0f;
    // 进度 = 当前 fuse / 初始 fuse（kPrimedTntFuseSec 为基准；链式 fuse 略长故 progress 可能 >1，clamp 到 1）。
    float p = e.fuse / kPrimedTntFuseSec;
    return p > 1.0f ? 1.0f : p;
}

// t241/t897 羊吃草：检测 / 消耗**脚下草方块**（机制等价 MC 1.0 羊低头吃草方块：Grass → Dirt）。
//   t897 ① 语义收紧（用户「吃草动画只在脚下草方块触发」）：旧目标 = 身前 reach 0.7 一格的**草丛**
//   （TallGrass；消耗草丛 + 其下草方块变泥土）→ ①纯草方块地表（无草丛的草原 / 人造平台）羊永不
//   开吃草动画；②吃不吃取决于朝向（正对草丛才吃）观感怪。改目标 = **自身列脚下方块**（AABB 底面
//   下一格 = 支撑格，与 t300 重新长毛链 / 脚步声同列口径）== Grass —— 草丛（TallGrass）不再参与
//   吃草语义（kEatReach 前向外推随之退役）。consume=true：草方块 → 泥土（静默写 setWaterSilent，
//   非玩家破块 → 不发 broken/placed——但 t903 给 setWaterSilent 挂了「同格植物连带清」钩子：羊吃的
//   Grass 上方有 TallGrass 时会正常爆粒子 + 掉种子（t903 有意设计，顺带收割高草），「完全免粒子 /
//   掉落」只对无附着裸草方块成立）。consume=false：仅检测（决定是否开吃草周期）。越界 / 非
//   草方块 → 安全返 false（blockAt 越界返 Air ≠ Grass；pos 已被物理边界 clamp，自身列恒在界内）。
//   review27 #18① 腾空不开吃：groundY = floor(pos.y − halfH) − 1 的垂直窗口在腾空时放宽 ~1 格（小跳 /
//   下落 / 水面缓沉都落在窗内）——旧版腾空羊可开吃并在 0.5s 后空中消耗 Grass→Dirt。吃草是落地行为：
//   resting（已落实体支撑面）才检测 / 消耗（检测与消耗同门，杜绝「落地开吃后离地仍消耗」半态）。
bool EntityManager::sheepEatGrass(Entity &e, World *world, float worldW, float worldD,
                                  bool consume)
{
    if (!world) return false;
    Q_UNUSED(worldW);
    Q_UNUSED(worldD); // 脚下自身列（pos 被边界 clamp 恒在界内）；保留签名兼容两处调用点
    if (!e.resting) return false; // review27 #18①：腾空（未落实体支撑面）→ 不检测不消耗（见头注释）
    // 脚下方块（自身列）：bodyY = AABB 底面所在格；支撑格 = bodyY − 1（草方块）。
    const int cx = qFloor(e.pos.x());
    const int cz = qFloor(e.pos.z());
    const int bodyY = qFloor(e.pos.y() - e.halfH); // t252: e.halfH（底面 y）
    const int groundY = bodyY - 1;
    if (groundY < 0) return false;

    if (world->blockAt(cx, groundY, cz) != BlockRegistry::Grass) return false; // 脚下非草方块 → 不吃

    if (consume) {
        // 草方块 → 泥土（静默写；机制等价 MC 1.0 羊吃草后草地变泥土）。
        world->setWaterSilent(cx, groundY, cz, BlockRegistry::Dirt, 0);
        qCInfo(lcEnt) << "sheep ate grass block at" << cx << groundY << cz << "(-> dirt)";
    }
    return true;
}

// 玩家推动解析：对每个 pushable 实体做「玩家 AABB（XZ 矩形）vs 实体圆（XZ，半径=entity.halfW）」
// 穿透求解（机制等价 MC 实体碰撞推开：玩家位移解析后传给实体）。
//   1) 垂直区间重叠判定：实体立方体 [pos.y−r, pos.y+r] 与玩家 AABB [feet.y, feet.y+height] 必须重叠
//      才推动（玩家从头顶跳过 / 跨层时不应误推）。
//   2) XZ 穿透：AABB 最近点 cx/cz = clamp(entity 中心, [px−halfW, px+halfW])；d = entity − 最近点。
//      dist < r → 穿透 push = r − dist，沿 d/dist 推出；d≈0（中心在 AABB 内）→ 沿最近面推出（min 四向）。
//   3) 世界碰撞钳制（t104：mob AABB footprint 全格扫，仿 player aabbHitsSolid）：推动后扫 mob 立方体
//      AABB 覆盖的所有格子（非旧版「只查中心格」），任一实体方块 → 撤回该轴推动。X/Z 两轴独立判定
//      （斜推各自检查），保证实体贴墙滑动不穿入；全格扫消除「中心在空气但 3/4 入墙」的 jitter。
//   4) 被推动后按「新 XZ 位置下方一格 isSolid」决定是否解除 resting（t115；仿 player hasGroundBelowAt）：
//      新位置下方仍有支撑 → 保持 resting（平地推动不下沉）；下方变空气（推下阶梯 / 推离支撑面）才
//      resting=false 让重力复探。任一 pos 真变 → dirty，末尾统一 bump revision + emit（驱动 QML
//      {revision; posAt} 绑定重算）。
//   t239：dead mob 跳过（尸体不被推）。
void EntityManager::resolvePlayerPush(const QVector3D &playerFeet, float halfW, float height, World *world)
{
    if (m_entities.empty()) return;
    const float px = playerFeet.x(), pz = playerFeet.z();
    const float pminY = playerFeet.y(), pmaxY = playerFeet.y() + height;
    bool dirty = false;
    for (auto &e : m_entities) {
        if (!e.alive || !e.pushable || e.dead) continue; // t256 空槽 + 掉落物等非推动 + t239 dead mob 跳过
        // t811 载具骑乘态跳过：mob 位置钉载具座位（tickVehicleRiding 权威），玩家推挤会把钉位实体推出
        //   车斗 → 视觉脱离 + 下帧钉回的反复拉扯；骑乘期玩家从旁走过不应扰动乘员（同 dead 不推语义）。
        //   t952 mob-on-mob 挂载组合同口径：骑士被驮 / 载具驮人，位置由 tickMobMounts 钉位权威接管，推挤
        //   任一侧都会被对侧钉位拉回（反复拉扯），故成对豁免。
        if ((e.rideCart >= 0 && m_cartMgr) || (e.rideBoat >= 0 && m_boatMgr)
            || e.rideMob >= 0 || e.mobRider >= 0) continue;

        const float ehw = e.halfW; // 实体 XZ 半宽（圆碰撞半径）
        const float ehh = e.halfH; // 实体 Y 半高（垂直区间）
        // 垂直区间重叠判定（实体 AABB：[pos.y−ehh, pos.y+ehh] vs 玩家 [feet.y, feet.y+height]）。
        //   t252：Y 用 halfH（非旧版共用 radius；cow halfH=0.70 → 推动判定区对齐实际碰撞箱高度）。
        if (e.pos.y() + ehh <= pminY || e.pos.y() - ehh >= pmaxY) continue;

        // XZ 平面 AABB-vs-Circle 穿透求解。
        const float cx = std::clamp(e.pos.x(), px - halfW, px + halfW);
        const float cz = std::clamp(e.pos.z(), pz - halfW, pz + halfW);
        const float dx = e.pos.x() - cx;
        const float dz = e.pos.z() - cz;
        const float dist2 = dx * dx + dz * dz;
        if (dist2 >= ehw * ehw) continue; // 无 XZ 穿透

        float newX = e.pos.x();
        float newZ = e.pos.z();
        const float dist = std::sqrt(dist2);
        if (dist > 1e-5f) {
            const float push = ehw - dist;
            newX = e.pos.x() + dx / dist * push;
            newZ = e.pos.z() + dz / dist * push;
        } else {
            // 实体中心在玩家 AABB 内：沿最近面推出（min 四向距离 → 最短穿透方向）。
            const float toMinX = e.pos.x() - (px - halfW);
            const float toMaxX = (px + halfW) - e.pos.x();
            const float toMinZ = e.pos.z() - (pz - halfW);
            const float toMaxZ = (pz + halfW) - e.pos.z();
            const float m = std::min({toMinX, toMaxX, toMinZ, toMaxZ});
            if (m == toMinX)      newX = px - halfW - ehw;
            else if (m == toMaxX) newX = px + halfW + ehw;
            else if (m == toMinZ) newZ = pz - halfW - ehw;
            else                  newZ = pz + halfW + ehw;
        }

        // 世界碰撞钳制（t104：mob AABB footprint 全格扫，仿 player aabbHitsSolid）：扫 mob 立方体 AABB
        // 覆盖的所有格子（非旧版「只查中心格」），任一实体方块 → 撤回该轴推动（防穿墙）。X/Z 两轴独立
        // 判定，Z 轴参照可能已撤回的 newX（两轴独立但顺序敏感）。旧版单格检查在斜推角落时 mob 中心可能
        // 仍在空气 → 不撤回 → 下帧中心入墙才撤回 → 反复跳变 = jitter；全格扫使任一部分触墙即撤回 → 消除。
        if (world) {
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh))     newZ = e.pos.z();
        }

        if (newX != e.pos.x() || newZ != e.pos.z()) {
            e.pos.setX(newX);
            e.pos.setZ(newZ);
            // t115：旧版无条件 e.resting=false → 平地推动后 tick 重力下沉几帧再弹回 restY（下移扫描
            //   只覆盖实体中心高度的空气格、够不到下方支撑格 → 误判无命中 → 自由下落 → 几帧后扫到支撑
            //   才弹回）→ 帧帧 Y 抖（用户感知为缩小 / scale 闪烁）。改为按新 XZ 位置下方一格 isSolid
            //   判定（仿 player hasGroundBelowAt）：新位置仍有支撑 → 保持 resting → 下帧 tick 复探支撑
            //   格仍实体 → 跳过重力 → pos.y 不变 → 无抖；推下阶梯 / 推离支撑面时下方变空气 → 解除 →
            //   重力自然落（防悬空）。支撑判定与 tick 的 resting 复探同公式（同列 floor(pos.y−r)−1）→
            //   保证「此处判保留」必与「tick 下帧判保留」一致 → 永不因二者分歧再抖。
            if (world) {
                const int ncx = qFloor(newX);
                const int ncz = qFloor(newZ);
                const int supportY = qFloor(e.pos.y() - ehh) - 1; // 实体底面下方一格（与 tick 复探同公式）
                if (supportY < 0 || !world->isSolid(ncx, supportY, ncz))
                    e.resting = false; // 新位置失支撑 → 解除静止让重力复探（推下阶梯 / 推离支撑面）
            } else {
                e.resting = false; // 无世界可查 → 保守解除（world=null 时 tick 早 return，不影响）
            }
            dirty = true;
        }
    }
    if (dirty) notifyEntitiesChanged();
}

namespace {
// ── t811 载具骑乘常量（tickVehicleRiding 用；机制口径见 .h tickVehicleRiding 头注释）──
// 矿车座位底板偏移：mob 脚底相对矿车中心下移量 = 车底板面（坐车斗内脚踩板面）。与 PlayerController.step
//   骑车分支的 kCartSeatDrop 同值同义（MinecartManager private 跨类不可读 → 本层同值命名常量，同
//   playercontroller.cpp kCartSeatDrop 先例；改须两处 + Main.qml 车斗底板 piece 三处同步）。
const float kEmCartSeatFloorDrop = 0.3125f;
// 登乘判定距离（XZ 平面，格）：矿车 0.8 / 船 1.0（船体更长更宽 → 阈值放宽）。垂直容差 kEmBoardDy 防
//   楼上 / 桥下误登（隔层载具不吸人）。
const float kEmBoardCartDist = 0.8f;
const float kEmBoardBoatDist = 1.0f;
const float kEmBoardDy = 1.5f;
// 船双座侧向偏移（格）：座位 0 = 船右舷 +0.3 / 座位 1 = 左舷 −0.3（沿船 right 向；两乘员并肩不重叠）。
const float kEmBoatSeatSide = 0.3f;
} // namespace

// t811 载具管理器注入（头注释见 .h；幂等指针写）。
void EntityManager::setVehicleManagers(MinecartManager *carts, BoatManager *boats)
{
    m_cartMgr = carts;
    m_boatMgr = boats;
}

// t811 骑乘收口 pass（头注释见 .h）：座位对账 → 骑乘钉位/自释放 → 登乘扫描。纯自身 + 只读载具管理器。
void EntityManager::tickVehicleRiding()
{
    if (m_entities.empty()) return;
    if (!m_cartMgr && !m_boatMgr) return; // 无载具场景（未注入 / 矩阵测试）：mob 照常 AI，不乘不冻

    // Pass A 座位对账（载具侧 → mob 侧）：座位指向的 mob 已死 / 已释放 / 槽复用换任（kind 不再 Mob /
    //   反向链断）→ 清座。防「幽灵乘客」占座拒载（mob 死亡动画期 alive=true 但 dead → 座立即让出可再接客，
    //   尸体本身不走 AI 不受影响）。mob 侧 rideX 字段届时由其自身死亡 / 释放路径自然失效（dead 分支冻结 +
    //   槽复用 DMI 清回）。
    if (m_cartMgr) {
        const int n = m_cartMgr->count();
        for (int i = 0; i < n; ++i) {
            const int p = m_cartMgr->mobPassengerAt(i); // 空槽 → -1 天然跳过
            if (p < 0) continue;
            const bool linkOk = p < int(m_entities.size()) && m_entities[size_t(p)].alive
                                && m_entities[size_t(p)].kind == Mob && !m_entities[size_t(p)].dead
                                && m_entities[size_t(p)].rideCart == i;
            if (!linkOk) m_cartMgr->clearMobPassenger(i);
        }
    }
    if (m_boatMgr) {
        const int n = m_boatMgr->count();
        for (int i = 0; i < n; ++i) {
            for (int s = 0; s < 2; ++s) {
                const int p = m_boatMgr->mobPassengerAt(i, s);
                if (p < 0) continue;
                const bool linkOk = p < int(m_entities.size()) && m_entities[size_t(p)].alive
                                    && m_entities[size_t(p)].kind == Mob && !m_entities[size_t(p)].dead
                                    && m_entities[size_t(p)].rideBoat == i;
                if (!linkOk) m_boatMgr->clearMobPassenger(i, s);
            }
        }
    }

    bool dirty = false;
    // review26 #10：本 pass 内是否有乘客钉位值真变（= 载客矿车 / 船本帧移动，钉位公式算出新位）。
    //   只统计「跟车钉位」——登乘 / 自释放等一次性状态变化仍走 20Hz 相位门（无逐帧刷新需求）。
    bool pinMoved = false;
    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive || e.kind != Mob || e.dead) continue; // 空槽 / 非 mob / 尸体不参与钉位与登乘

        // Pass B 矿车骑乘：反向链完好 → 钉车座位（车动它动）；链断（车被挖 / clearAll / 槽复用）→ 自释放
        //   原地恢复 AI（下车唯一路径 = 载具被破坏；释放位 = 最后钉位 ≈ 车消失处）。
        if (e.rideCart >= 0) {
            const int ci = e.rideCart;
            if (m_cartMgr && ci < m_cartMgr->count() && m_cartMgr->aliveAt(ci)
                && m_cartMgr->mobPassengerAt(ci) == idx) {
                const QVector3D cp = m_cartMgr->posAt(ci);
                const QVector3D pin(cp.x(), cp.y() - kEmCartSeatFloorDrop + e.halfH, cp.z());
                if (e.pos != pin) { e.pos = pin; dirty = true; pinMoved = true; }
            } else {
                e.rideCart = -1;
                e.resting = false; // 解除静止让重力复探支撑面：钉位 Y（车座位）常高于地面 → 不解除则复探
                                   //   支撑仍实体 → 悬在座位高度不落地（t115 推动解除 resting 同因）。
                dirty = true;
            }
            continue;
        }

        // Pass B 船骑乘：同上，座位 0/1 沿船 right 向侧偏（yaw 度 → right = (cosθ,0,−sinθ)，-Z 前约定下
        //   θ=0 面北右 = +X；mob 中心 = 脚底（船心）+ halfH）。
        if (e.rideBoat >= 0) {
            const int bi = e.rideBoat;
            if (m_boatMgr && bi < m_boatMgr->count() && m_boatMgr->aliveAt(bi)
                && m_boatMgr->mobPassengerAt(bi, e.rideBoatSeat) == idx) {
                const QVector3D bp = m_boatMgr->posAt(bi);
                const float yawRad = qDegreesToRadians(m_boatMgr->yawAt(bi));
                const float side = (e.rideBoatSeat == 0) ? kEmBoatSeatSide : -kEmBoatSeatSide;
                const QVector3D pin(bp.x() + std::cos(yawRad) * side,
                                    bp.y() + e.halfH,
                                    bp.z() - std::sin(yawRad) * side);
                if (e.pos != pin) { e.pos = pin; dirty = true; pinMoved = true; }
            } else {
                e.rideBoat = -1;
                e.resting = false; // 同矿车自释放：重力复探支撑面（船座位高于地面 / 水面落点由重力 + 浮力接手）。
                dirty = true;
            }
            continue;
        }

        // Pass C 登乘扫描（非骑乘 mob）：最近可乘载具。矿车 1 座（生物占 / 玩家骑均满）；船总乘员限 2
        //   （玩家占 1 座时只剩 1 生物座）。两类都在近旁优先矿车（先扫）—— 机制口径简单可预期。
        //   t952：mob-on-mob 挂载组合（骑士 / 载具任一）不参与载具登乘——骑士组合是独立骑乘体系（双向链
        //   挂 Entity 槽），混乘载具会让两套钉位权威打架（矿车钉位 vs 挂载钉位同帧互拉）。
        if (e.rideMob >= 0 || e.mobRider >= 0) continue;
        if (m_cartMgr) {
            const int n = m_cartMgr->count();
            int best = -1;
            float bestD2 = kEmBoardCartDist * kEmBoardCartDist;
            for (int i = 0; i < n; ++i) {
                if (!m_cartMgr->aliveAt(i)) continue;
                if (m_cartMgr->mobPassengerAt(i) >= 0) continue;    // 生物已占座 → 满
                if (m_cartMgr->ridingIndex() == i) continue;        // 玩家正骑 → 满（乘员总数限 1）
                // 跨批遗留 #4（09-06 review 中 #4）：箱子矿车拒载生物——对齐玩家侧 tryMount 拒箱车
                //   （minecartmanager.cpp「箱子矿车不可骑」守卫）：内容容器语义，蜘蛛钉坐进车斗模型
                //   视觉穿插。读口 chestAt 已有（呈现层同用），一行守卫即可。
                if (m_cartMgr->chestAt(i)) continue;
                const QVector3D d = m_cartMgr->posAt(i) - e.pos;
                if (std::abs(d.y()) > kEmBoardDy) continue;
                const float dxz2 = d.x() * d.x() + d.z() * d.z();
                if (dxz2 < bestD2) { bestD2 = dxz2; best = i; }
            }
            if (best >= 0) {
                m_cartMgr->seatMob(best, idx);
                e.rideCart = best;
                e.rideBoat = -1;
                // 姿态锁定：清行走驱动 + 残余动量（击退 / 越障滑流 / 垂直速度），骑乘态从静止位开始钉。
                //   review27 #12：walkPhase/stepAccum 同处归零——骑乘态在 tick 主循环 continue 早退，恒
                //   不达下方 t897 ② 归零块；不在此清则行走中被放上车的 mob 腿冻结半步相位（t897 ② 自称
                //   修掉的观感 bug 在骑乘态残留）。resting 不动（钉位跳过 tick 物理段，不读它）。
                e.moveSpeed = 0.0f;
                e.walkPhase = 0.0f;
                e.stepAccum = 0.0f;
                e.vx = 0.0f; e.vz = 0.0f; e.vy = 0.0f;
                e.jumpGX = 0.0f; e.jumpGZ = 0.0f;
                dirty = true;
                continue;
            }
        }
        if (m_boatMgr) {
            const int n = m_boatMgr->count();
            int best = -1, bestSeat = 0;
            float bestD2 = kEmBoardBoatDist * kEmBoardBoatDist;
            for (int i = 0; i < n; ++i) {
                if (!m_boatMgr->aliveAt(i)) continue;
                const int playerSeats = (m_boatMgr->ridingIndex() == i) ? 1 : 0;
                if (playerSeats + m_boatMgr->mobSeatCount(i) >= 2) continue; // 满员（玩家+1生物 / 2生物）
                const int freeSeat = (m_boatMgr->mobPassengerAt(i, 0) < 0) ? 0 : 1;
                const QVector3D d = m_boatMgr->posAt(i) - e.pos;
                if (std::abs(d.y()) > kEmBoardDy) continue;
                const float dxz2 = d.x() * d.x() + d.z() * d.z();
                if (dxz2 < bestD2) { bestD2 = dxz2; best = i; bestSeat = freeSeat; }
            }
            if (best >= 0) {
                m_boatMgr->seatMob(best, bestSeat, idx);
                e.rideBoat = best;
                e.rideBoatSeat = bestSeat;
                e.rideCart = -1;
                // review27 #12：同矿车登乘——walkPhase/stepAccum 归零（骑乘态早退不达 t897 ② 归零块）。
                e.moveSpeed = 0.0f;
                e.walkPhase = 0.0f;
                e.stepAccum = 0.0f;
                e.vx = 0.0f; e.vz = 0.0f; e.vy = 0.0f;
                e.jumpGX = 0.0f; e.jumpGZ = 0.0f;
                dirty = true;
                continue;
            }
        }
    }
    // review25 #3：dirty 只置 m_pendingEmit，绝不在此直发 entitiesChanged——本 pass 每帧被调两次
    //   （playercontroller mob 桶 tick 后常开 + step() 后补钉），乘客跟车时每帧都 dirty，直发 = 最坏
    //   每帧 2 次全量 revision+emit（激活全体实体 delegate revision 绑定 + 行走 MobModel 全几何重建
    //   ——t500 已修的 22ms/帧卡顿模式复发）。复用 EntityManager::tick 末尾的 kEmitEveryN（~20Hz）
    //   收口：本帧两个调用点都在 tick 收口之后跑（playercontroller 内 tick → tickVehicleRiding 序）
    //   → pending 由**下一帧** tick 的相位门接住（≤kEmitEveryN 帧 ≈ 50ms 延迟，钉位呈现层无感）；
    //   m_pendingEmit 持续脏确保变更不丢。
    if (dirty) m_pendingEmit = true;
    // review26 #10 乘客钉位帧同步发射：钉位值真变（载客车 / 船本帧移动）→ bump rideRevision + 发
    //   ridersChanged。取舍（review 三选一，此处取「乘客单开小名单每帧 emit」的变体——专用 revision
    //   而非复用 entitiesChanged）：①「bump 矿车侧 revision」按字面不解——mob delegate 不观察
    //   carts.entitiesChanged，bump 车侧 revision 对乘客 delegate 零刷新；让 mob 绑定改触 carts.revision
    //   则任何车（含玩家骑乘 / 空车）移动都激活全体 mob position 绑定，触发面比「载客载具移动」宽。
    //   ② 本实现：发射条件恰 = 载客载具移动帧（静止 / 无乘客零发射），QML 侧仅 position 一条绑定触碰
    //   rideRevision（t500 卡顿主因的 ~12 条 revision 绑定 + MobModel 几何重建面不被触碰；乘客
    //   walkPhase 冻结、非乘客重采样到相同 / 静息值 = no-op）→ 乘客与车 delegate 同帧 60Hz 刷新，玩家
    //   骑乘观感不受影响（车侧 notifyChanged 节奏未动）。调序注：本 pass 每帧两调（mob 桶内 #1 在 step
    //   前、step 后 #2）——车在 step 内推进 → 只有 #2 观察到钉位变化 → 稳态恰每帧一次发射；船由
    //   BoatManager::tick 常开推进 → #1 或 #2 恰一处观察到，同样 ≤1 次 / 帧。
    if (pinMoved) { ++m_rideRevision; emit ridersChanged(); }
}

// t952 mob-on-mob 挂载 pass 实现（契约见 .h 声明注释；tick 末尾调）。三段：① 双向对账（死亡 / 槽复用 /
//   链断 → 解除挂载，被骑乘者落地恢复独立、载具恢复自主漫步）② 骑手 AI（kAiTickInterval 节拍消费冻结
//   分支累积的 aiAccum；玩家可锁定 → aiHostile，观察者 → aiWander）③ 钉位（载具 XZ ← 骑手 XZ、骑手 Y ←
//   载具顶 + 清骑手垂直态；载具 moveSpeed ← 骑手速度驱动小鸡腿摆、骑手 moveSpeed 清零——被驮不迈腿）。
//   钉位值真变帧 bump m_rideRevision + emit ridersChanged（review26 #10 专用通道：mob delegate position
//   绑定独占触碰，骑士组合 60Hz 同帧刷新；非乘客单帧重采样读到相同值 = 廉价 no-op）。
bool EntityManager::tickMobMounts(World *world, const QVector3D &playerPos, float worldW,
                                  float worldD, bool playerTargetable, float skyBrightness)
{
    if (m_entities.empty()) return false;
    bool dirty = false;
    bool pinMoved = false;
    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive || e.kind != Mob || e.dead) continue; // 空槽 / 非 mob / 尸体不参与（尸僵期对账已解除挂载）

        // ② 骑手 AI（被骑乘者；主循环已冻结其 AI/物理，节拍在此消费）。放对账**之后**：链断帧 AI 不跑
        //   （对账已在上方/下方解除），防骑死载具的骑手同帧再以挂载参数出手。
        if (e.rideMob >= 0) {
            // ① 骑手侧对账：载具失效（死 / 释放 / 槽复用换任 / 反向链断）→ 自解除落地（resting=false 让
            //   重力复探支撑面，从载具顶自然坠地——同 rideCart 自释放解除 resting 先例）。
            const int mi = e.rideMob;
            const bool mountOk = mi < int(m_entities.size()) && m_entities[size_t(mi)].alive
                                 && m_entities[size_t(mi)].kind == Mob && !m_entities[size_t(mi)].dead
                                 && m_entities[size_t(mi)].mobRider == idx;
            if (!mountOk) {
                e.rideMob = -1;
                e.resting = false; // 解除静止 → 重力接管：从载具顶落回地面（「小鸡被杀 → 小僵尸落地独立」）
                e.moveSpeed = 0.0f;
                e.walkPhase = 0.0f;
                e.stepAccum = 0.0f;
                dirty = true;
                qCInfo(lcEnt) << "jockey rider" << idx << "dismounted (mount slot" << mi << "invalid)";
                continue; // 本帧 AI / 钉位都不跑（下 tick 起主循环正常自由体）
            }
            // AI 节拍：同主循环 kAiTickInterval 错峰公式（m_tickPhase 本帧 tick 头部已 ++）。
            const bool aiTick = ((m_tickPhase + quint32(idx)) % quint32(kAiTickInterval)) == 0;
            const float aiDt = aiTick ? e.aiAccum : 0.0f;
            if (aiTick) e.aiAccum = 0.0f;
            if (aiDt > 0.0f) {
                // t290 门控同口径：玩家可锁定 → aiHostile（小蹒跚者快速低伤追击）；观察者 → aiWander
                //   （敌对回退游荡，位移经钉位仍驱动载具）。aiHostile 返 moved 已写入 moveSpeed，不消费。
                if (playerTargetable) {
                    aiHostile(idx, e, aiDt, world, playerPos, worldW, worldD, 1.0f, skyBrightness);
                } else if (world) {
                    aiWander(e, aiDt, world, worldW, worldD, 1.0f);
                }
                dirty = true;
            }
            // ③ 钉位：载具 XZ ← 骑手 XZ（骑手 AI 权威），骑手 Y ← 载具顶（垂直权威归本 pass）。
            Entity &mount = m_entities[size_t(mi)];
            const float riderSpeed = e.moveSpeed;
            if (mount.pos.x() != e.pos.x() || mount.pos.z() != e.pos.z()) {
                mount.pos.setX(e.pos.x());
                mount.pos.setZ(e.pos.z());
                pinMoved = true;
                dirty = true;
            }
            const QVector3D pin(e.pos.x(), mount.pos.y() + mount.halfH + e.halfH, e.pos.z());
            if (e.pos != pin) { e.pos = pin; pinMoved = true; dirty = true; }
            // t1008② 越障跳意图转移（「小僵尸的追击移动驱动小鸡」延伸到跳跃轴）：骑手 aiHostile
            //   越障跳门在骑乘态照样点火（钉位恒 resting → 门可达，review0830 #19 已实证），但点火后
            //   的 vy/滑流在旧链钉位段被一并清掉 = 骑士组合结构上跳不过 1 格台阶（实机「小僵尸追玩家
            //   跳不过 1 格障碍」复现根因：小鸡骑士被台面卡死——骑手 AABB 压在台面格内被逐轴撤回，
            //   每 AI tick 重探跳重清零，30s+ 原地踏步）。修法 = 骑手本拍点了跳（vy == kJumpSpeed，
            //   越障跳门专属初速——击退 kKnockbackUp 更小不会误触）且载具尚贴地（mount.resting）→
            //   载具以同款 kJumpSpeed 起跳：载具物理（重力 / 落地扫描）主循环照跑 → 整体升到台面顶，
            //   骑手 AABB 随钉位 Y 抬过台面后 AI 水平推进恢复 → 钉位把载具 XZ 拖上台（落地扫描落台顶）
            //   = 组合翻越台阶。载具已在空中 → 不重点火（防逐 AI tick 重置 vy = 组合火箭升空）。
            //   骑手 vy/滑流照旧清零（主循环不积分骑手垂直态，上升由钉位 Y 随载具顶承接）。
            if (e.vy >= kJumpSpeed && mount.resting) {
                mount.vy = kJumpSpeed;
                mount.resting = false;                 // 解除静止 → 主循环重力分支接管载具上跳
                dirty = true;
            }
            if (e.vy != 0.0f) e.vy = 0.0f;             // 挂载期垂直速度清零（主循环冻结不积分，防解除残留）
            // review0830 #19：钉位段顺手清越障跳水平滑流（与清 vy 同段）——骑乘期骑手 AI（钉位恒
            //   resting → aiHostile 越障跳分支可达）可设 jumpGX/jumpGZ，挂载态主循环早退不应用滑流
            //   （陈旧值存活）；不在此清则解除骑乘（载具死）后下落期尾段把陈旧滑流施出 = 朝墙漂移。
            if (e.jumpGX != 0.0f || e.jumpGZ != 0.0f) { e.jumpGX = 0.0f; e.jumpGZ = 0.0f; dirty = true; }
            if (!e.resting) e.resting = true;          // 钉位态视作「支撑」（无物理语义，仅状态一致；review0901 #31 清编辑残留重复行，矩阵钉本语句恰一处）
            mount.moveSpeed = riderSpeed;              // 小鸡腿随骑士移动摆动（走相块下帧消费）
            e.moveSpeed = 0.0f;                        // 被驮不迈腿（同矿车乘客 walkPhase 冻结口径）
            continue;
        }

        // ① 载具侧对账（无主动位移）：骑手失效 → 清反向链（小鸡下 tick 恢复自主 wander，本分支零动作）。
        if (e.mobRider >= 0) {
            const int ri = e.mobRider;
            const bool riderOk = ri < int(m_entities.size()) && m_entities[size_t(ri)].alive
                                 && m_entities[size_t(ri)].kind == Mob && !m_entities[size_t(ri)].dead
                                 && m_entities[size_t(ri)].rideMob == idx;
            if (!riderOk) {
                e.mobRider = -1;
                dirty = true;
                qCInfo(lcEnt) << "chicken" << idx << "rider released (rider slot" << ri << "invalid)";
            }
        }
    }
    if (pinMoved) { ++m_rideRevision; emit ridersChanged(); }
    return dirty;
}

// 重力 + AI wander + 地面静止（机制同 ItemEntityManager::tick；向下只读 World::isSolid/blockAt）。
//   FallingBlock（t117/t220）：重力 + 着地放置 / 变掉落物 + 移除（无 resting 态）。
//   Mob（t239）：
//     - dead：冻结 AI/重力，仅 deathTimer 倒计时 + hurtFlash 衰减；deathTimer≤0 → 移除（给 QML 死亡动画窗口）。
//     - 非 dead：aiWander（水平 AI 行走 + 碰撞）+ hurtFlash 衰减 + 原有 resting/重力/垂直（共用 Mob/Item）。
//   Mob/Item 原有逻辑：
//     1) 已 resting：复探支撑格（实体底面下方一格 = floor(pos.y − r) − 1）仍实体 → 保持静止；
//        失支撑 → 解除 resting 续落（防挖空悬空；亦承接 resolvePlayerPush / aiWander 把实体推/走离原支撑面）。
//     2) 未 resting：vy -= g*dt（钳 -kMaxFall），按 dy 下移；下移路径 [floor(newY), floor(pos.y)] 自顶向下
//        扫实体所在列首个实体方块 → 命中则贴其顶面（solidCellY+1+halfH）停下、vy=0、resting=true。
//        越界（cy<0）查 isSolid 返 false → 不误判虚空地面，实体继续落。
//     3) pos / resting 任一真变 → dirty，末尾统一 bump revision + emit（驱动 QML {revision; posAt} 绑定重算）。
//   void-loss 兜底：Mob 跌出世界底部（pos.y<0，如被推出边界外无支撑）→ 标记移除（防永久下落）。
//
// 移除用索引收集 + 循环后逆序 erase（保索引有效）。
void EntityManager::tick(qreal dt, World *world, const QVector3D &listener,
                        float listenerHalfW, float listenerHeight, bool playerTargetable,
                        bool playerSpectator, float skyBrightness)
{
    if (!world || m_entities.empty()) return;
    FrameProfiler::Scope profLoop("mobLoop"); // t500 perf：mob 桶子分解（EntityManager::tick 整段）
    const float worldW = float(world->width());
    const float worldD = float(world->depth());
    bool dirty = false;
    std::vector<int> toRemove; // FallingBlock 着地 / 跌出 + t239 mob deathTimer 到 / void-loss 索引（逆序 erase）
    // t997 爆炸波分期：本 tick 的 primed TNT 引爆预算（每 tick() 调用重置一次）。同帧到期簇 ≤ 预算照旧
    //   全爆；超额者引信重挂 kDetonationWaveRegroupSec 下 tick 再爆（钳制点见下方 FallingBlock primed 分支；
    //   常量契约见 entitymanager.h 注释：总破坏量不变、链式 1.2s 错峰引信语义不动、只影响同帧到期簇）。
    int tntDetonationBudget = kMaxTntDetonationsPerTick;
    // t500 perf：tick 节拍 +1（每 60Hz tick 一次）；mob AI / 环境扫描错峰节流据它判本帧哪些 mob 跑重活。
    ++m_tickPhase;
    // t500 perf mob 子桶手动计时：mob-loop 内逐实体 aiTick 段（火烧 / 仙人掌 / AI 决策移动）累 aiNs；
    //   mobPhys（每帧段：重力 / resting / flow / knockback / 音频 / walkPhase）不单独累 —— 在 report 派生出
    //   mobLoop − mobAI（见函数末尾 mobAI 桶注释）。手动 nowNs 比 RAII Scope 更轻（每实体 2× nowNs vs 2×
    //   Scope 构造析构含 map add）且能跨 continue。
    qint64 aiNs = 0;
    // t905 perf mob 桶细分（phys = loop − ai 的 10.26ms/帧黑盒解剖）：mobHead / mobTail 两手动桶，靠
    //   「下一实体迭代起点结清上一实体尾段」的跨迭代记账（循环内 continue 太多，逐段 RAII 包裹不可行）：
    //   - segId=0（head）：迭代起点 → aiT0。覆盖非 Mob kind 分支（箭 / 雪球 / 落体 …）+ Mob 入块前置
    //     （dead 倒计时 / 骑乘冻结 / 免疫清零）+ 空槽跳过。
    //   - ai 段：aiT0 → aiNs 结算行（既有 aiNs 计时，不动）。
    //   - segId=2（tail）：ai 结算行 → 下一迭代起点 / 循环尾。覆盖流推 / 红闪 / 环境音 / 走相 / 击退 /
    //     滑流 / 窒息（节流帧）+ resting 复探 / 重力 / 落地扫描 + 循环尾（releaseSlot / flushPendingShots /
    //     tickBreeding / emit）。
    //   每活体迭代 2-3 次 nowNs（~25ns/次），空槽 1 次 —— 开销 << 观测对象（μs 级）。
    qint64 headNs = 0, tailNs = 0;
    qint64 segT0 = 0;  // 当前段起点（0 = 无未结清段）
    int segId = -1;    // 0=head 1=ai（块内 continue 由结清点入 aiNs）2=tail
    // t905 结清当前未结段（下一迭代起点 / 循环尾两处调）。
    auto settleSeg = [&]() {
        const qint64 now = FrameProfiler::nowNs();
        if (segId == 0) headNs += now - segT0;
        else if (segId == 1) aiNs += now - segT0;
        else if (segId == 2) tailNs += now - segT0;
        segT0 = 0;
        segId = -1;
    };

    // t321 玩家受击全局节流倒计时（每帧扣一次，非每实体；防多 mob 围攻秒杀，详见 kPlayerHitThrottle 注释）。
    if (m_playerHitCooldown > 0.0f) {
        m_playerHitCooldown -= float(dt);
        if (m_playerHitCooldown < 0.0f) m_playerHitCooldown = 0.0f;
    }

    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        // t905 mob 细分：先结清上一实体的未结段（head / ai / tail），本实体开 head 段。空槽 continue 亦计入
        //   head（它们本就是 head 成本：47 槽里 33 空槽的逐槽跳过）。
        settleSeg();
        {
            const qint64 now = FrameProfiler::nowNs();
            segT0 = now;
            segId = 0;
        }
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive) continue; // t256：跳过已释放的空槽（slot-reuse 残留位；不参与物理 / AI）

        // --- Arrow（t283 骷髅弓箭手箭矢）：抛物 + 方块命中 / 玩家命中 / 寿命 / 越界 → 移除（不走 Mob AI / resting）---
        if (e.kind == Arrow) {
            // 任务（弓箭 60s 必 despawn）：**硬墙钟上限** —— 任何箭（玩家 / 骷髅 / 飞行 / 嵌入）自 spawn 起
            //   60s 必 despawn（机制等价 MC 1.0 箭 60s 消失）。这是 arrowLife dt-累加 despawn（飞行 5s / 嵌入 60s）
            //   的真值源 + 安全网：dt 累加在低帧率 / dt=0 / t500 节流帧时会滞后或漂移，墙钟不依赖 dt → 必然移除，
            //   杜绝用户报告「骷髅弓手射出的箭插墙 / 落地不消失」。放在 stuck 分支之前，对飞行 + 嵌入态统一生效。
            if (m_clock.elapsed() - e.arrowSpawnMs >= kArrowDespawnMs) {
                toRemove.push_back(idx);
                dirty = true;
                continue;
            }
            // t323 嵌入态（命中方块后冻结物理）：仅 despawn 倒计时；玩家箭的近距拾取由
            //   PlayerController::arrowPickupScan 处理（嵌入箭仍渲染：kind=Arrow + pos 钉面 + vel 定向不变）。
            if (e.arrowStuck) {
                e.arrowLife -= float(dt);
                if (e.arrowLife <= 0.0f) { toRemove.push_back(idx); dirty = true; } // ~60s despawn
                continue;
            }
            e.arrowLife -= float(dt);
            // 抛物：重力改 vy（与世界重力同值 → 弧自然）。不复用 Mob 终端下落钳（箭可高速上扬）。
            e.vy -= kGravity * float(dt);
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt);
            bool remove = false;
            bool hitPlayer = false;
            int hitPlayerDmg = 0; // t324 命中玩家造成的伤害（骷髅箭=kArrowDamage / 自身箭=arrowDamage；日志用）

            // 寿命到 → 移除（飞行未命中兜底，防永久滞留堆积）。
            if (e.arrowLife <= 0.0f) remove = true;

            // 方块命中（t323 嵌入而非移除）：新位置所在格命中 → 箭钉入射面（半嵌可见、定向飞行方向），
            //   arrowStuck=true 冻结物理 + arrowLife 重置 kStuckArrowLifetime（~60s despawn）。vx/vy/vz 保留供
            //   arrowYawAt/arrowPitchAt 定向（嵌入箭仍朝命中飞行方向）。玩家箭嵌入后可拾
            //   （PlayerController::arrowPickupScan）；骷髅箭嵌入不拾（防刷，spec）。
            //   review M7：命中判据由「该格存在方块」（World::isSolid 语义=非 air —— 压力板 / 火把 / 水全算墙。
            //   丛林神殿陷阱的压力板正对发射器口：箭 origin 在板格界面上、14 格/s 首 tick 穿入板格 0.23 →
            //   isSolid 见板 → 箭嵌在出膛 0.2 格处，永远到不了玩家）改为「箭尖点实际落入该格某碰撞 sub-AABB
            //   内」（world->collisionAABBsAt 点在盒内，与玩家碰撞 / t575 窒息判定同源 —— 玩家移动也按 sub-AABB
            //   精确碰撞，非整格）：压力板（1/16 薄板仅占格底 y[0,0.0625]，箭在格半高飞过）/ 火把（ShapeNone
            //   无碰撞盒）/ 水（无盒）不再挡箭 —— 陷阱箭越过板命中踩板玩家；箭穿水下沉（机制对齐 MC 箭入水
            //   不停在水面）。完整立方 / 半砖 / 楼梯 / 栅栏等碰撞盒覆盖通路区域 → 仍正常嵌入（半砖上半飞行的
            //   箭正确掠过其上沿，比旧整格判更贴形）。
            if (!remove) {
                const int bx = qFloor(next.x()), by = qFloor(next.y()), bz = qFloor(next.z());
                bool hitBlock = false;
                if (by >= 0) {
                    // t859：out-param 栈上小缓冲（零堆分配；旧按值版每查两次 vector 分配）。
                    BlockRegistry::BlockAABB boxes[BlockRegistry::kMaxAABBsPerCell];
                    const int n = world->collisionAABBsAt(bx, by, bz, boxes, BlockRegistry::kMaxAABBsPerCell);
                    for (int i = 0; i < n; ++i) {
                        const BlockRegistry::BlockAABB &b = boxes[i];
                        if (next.x() > b.minX && next.x() < b.maxX
                            && next.y() > b.minY && next.y() < b.maxY
                            && next.z() > b.minZ && next.z() < b.maxZ) { hitBlock = true; break; }
                    }
                }
                if (hitBlock) {
                    // 入射方向（归一；速度 ~0 退化 → 向下兜底）。沿 dir 把箭尖压入面、杆尾露面外（半嵌）。
                    QVector3D v(e.vx, e.vy, e.vz);
                    const float vlen = v.length();
                    const QVector3D dir = vlen > 1e-3f ? v / vlen : QVector3D(0.0f, -1.0f, 0.0f);
                    // 入射面 = |v| 主轴上「箭来源侧」的 block 边界面（vx>0 → 来源 -X 侧 → x=bx 面；余类推），
                    //   其余两轴用 next 坐标（命中点在该面上的投影）。非主轴坐标可能略入块，但箭细长沿 dir 定向无碍。
                    float fx = next.x(), fy = next.y(), fz = next.z();
                    const float ax = std::abs(e.vx), ay = std::abs(e.vy), az = std::abs(e.vz);
                    if (ax >= ay && ax >= az) fx = e.vx > 0.0f ? float(bx) : float(bx + 1);
                    else if (ay >= az)        fy = e.vy > 0.0f ? float(by) : float(by + 1);
                    else                       fz = e.vz > 0.0f ? float(bz) : float(bz + 1);
                    e.pos = QVector3D(fx, fy, fz) - dir * kArrowEmbed; // 心在面外、尖入面内（半嵌可见）
                    e.arrowStuck = true;
                    e.arrowLife = kStuckArrowLifetime;
                    dirty = true;
                    continue; // 嵌入态：跳过玩家 / mob 命中 + 越界兜底（已钉面；下帧由顶部 arrowStuck 分支 despawn）
                }
            }

            // 玩家命中：箭（点）是否落在玩家 AABB 外扩命中盒内。玩家 AABB = listener（脚位）[−hw,+hw]×
            //   [0,height]×[−hw,+hw]；XZ/Z 外扩 kArrowHitHalfW，Y 上下各外扩 kArrowHitHalfW（提升近距命中率）。
            //   命中 → 发 mobAttackedPlayer(kArrowDamage, MobBones)（呈现层据 Survival 门控应用伤害，同近战
            //   aiHostile attack 路径）+ 移除箭。Creative/Spectator 玩家经 onMobAttackedPlayer 跳过 takeDamage。
            //   t290 观察者交互门控：playerTargetable=false（创造/观察者）→ 箭直接穿过玩家不判定命中（机制等价
            //   MC 1.0 创造/观察者无敌 —— 既不射（aiArcher 不射击非生存玩家）也不被命中；防 Survival→模式切换
            //   后半空中的箭仍戳到刚转无敌的玩家）。
            //   t304 玩家射出的箭（arrowFromPlayer=true）默认不判玩家命中 —— 玩家箭只打 mob（下方分支），不会误伤
            //   玩家自己（机制等价 MC 1.0 玩家箭不伤玩家）。
            //   t324 自身箭下落自伤例外：玩家射出的箭飞行 kArrowSelfArmDelay（发射者忽略窗口）后「武装」—— 下落砸中
            //   玩家时也扣 arrowDamage HP（机制等价 MC 1.0 玩家可被自己朝天射落的箭砸伤）。窗口防贴脸出膛误伤（箭
            //   spawn 在玩家外扩命中盒内，未武装前穿过不触发）。骷髅箭（arrowFromPlayer=false）恒命中玩家。
            //   t321 全局节流门控：m_playerHitCooldown>0（玩家刚被任一 mob 命中，节流无敌帧内）→ 跳过玩家命中判定
            //   （箭穿过玩家不触发伤害、不移除，继续飞行），与 aiHostile attack 节流一致 —— 防多弓手齐射秒杀玩家。
            const float flightTime = kArrowLifetime - e.arrowLife; // 已飞行秒数（arrowLife 从 kArrowLifetime 递减）
            const bool selfArmed = !e.arrowFromPlayer || flightTime >= kArrowSelfArmDelay;
            if (!remove && playerTargetable && selfArmed && m_playerHitCooldown <= 0.0f) {
                const float px = listener.x(), py = listener.y(), pz = listener.z();
                const float ex = px - listenerHalfW - kArrowHitHalfW;
                const float ey = py - kArrowHitHalfW;
                const float ez = pz - listenerHalfW - kArrowHitHalfW;
                if (next.x() >= ex && next.x() <= px + listenerHalfW + kArrowHitHalfW
                    && next.y() >= ey && next.y() <= py + listenerHeight + kArrowHitHalfW
                    && next.z() >= ez && next.z() <= pz + listenerHalfW + kArrowHitHalfW) {
                    // t296 箭击退方向 = 箭飞行速度 (vx,vz) 归一（沿箭去向推玩家；机制等价 MC 箭动量传递）。
                    //   退化（箭水平速 ~0，近乎垂直下落）→ 用「玩家 − 箭」水平向量兜底（把玩家推离着箭点）。
                    float kbX = 0.0f, kbZ = 0.0f;
                    const float vlen = std::sqrt(e.vx * e.vx + e.vz * e.vz);
                    if (vlen > 1e-3f) { kbX = e.vx / vlen; kbZ = e.vz / vlen; }
                    else {
                        const float tx = px - e.pos.x(), tz = pz - e.pos.z();
                        const float tlen = std::sqrt(tx * tx + tz * tz);
                        if (tlen > 1e-3f) { kbX = tx / tlen; kbZ = tz / tlen; }
                        else { kbX = 1.0f; }
                    }
                    // t324 命中伤害 / 死因来源分流：骷髅箭（arrowFromPlayer=false）= kArrowDamage / MobBones（t283 旧路径）；
                    //   玩家自身箭（arrowFromPlayer=true）= arrowDamage（蓄力 1..6）/ mobType=-1（无 mob 来源 → 呈现层
                    //   死因映射兜底 Generic，机制等价 MC「被自己的箭砸死」无特定凶手）。
                    const int dmg = e.arrowFromPlayer ? e.arrowDamage : kArrowDamage;
                    const int srcMobType = e.arrowFromPlayer ? -1 : int(MobBones);
                    // t480 骷髅箭命中玩家 → 驯服狼防御目标 = 射箭的骸骨（arrowShooter 由 fireArrow 记发射者槽）。
                    if (!e.arrowFromPlayer && e.arrowShooter >= 0 && e.arrowShooter < int(m_entities.size()))
                        m_wolfTarget = e.arrowShooter;
                    emit mobAttackedPlayer(dmg, srcMobType, kbX, kbZ);
                    m_playerHitCooldown = kPlayerHitThrottle; // t321 串行化玩家受击（多弓手轮替命中）
                    hitPlayer = true;
                    hitPlayerDmg = dmg;
                    remove = true;
                }
            }

            // t304 玩家箭命中 mob（spec「抛物+伤害 mobs」）：玩家射出的箭（arrowFromPlayer=true）沿飞行逐帧
            //   测点是否落在任一活体 mob 的 AABB（外扩 kArrowHitHalfW 提升近距命中率）内。命中首个最近 mob
            //   → damageEntity（扣 arrowDamage HP + 红闪 + 归零 mobDied 死亡掉落，复用受击链）+ emit mobAttacked
            //   （呈现层 playMobHurt；同近战 attackMob 路径）+ 移除箭。跳过非 alive / 非 Mob / dead（尸体） /
            //   Arrow / Item（掉落物）实体。mob AABB 用每实体 halfW/halfH（t252 拆分 XZ/Y，按 mobType 贴合身体）。
            //   机制等价 MC 1.0 玩家弓箭打怪（命中首个、伤害由蓄力决定、箭命中即消失）。
            if (!remove && e.arrowFromPlayer) {
                for (int mi = 0; mi < int(m_entities.size()); ++mi) {
                    const Entity &m = m_entities[size_t(mi)];
                    if (!m.alive || m.kind != Mob || m.dead) continue; // 跳过空槽 / 非 mob / 尸体
                    const float ex2 = m.pos.x() - m.halfW - kArrowHitHalfW;
                    const float ey2 = m.pos.y() - m.halfH - kArrowHitHalfW;
                    const float ez2 = m.pos.z() - m.halfW - kArrowHitHalfW;
                    if (next.x() >= ex2 && next.x() <= m.pos.x() + m.halfW + kArrowHitHalfW
                        && next.y() >= ey2 && next.y() <= m.pos.y() + m.halfH + kArrowHitHalfW
                        && next.z() >= ez2 && next.z() <= m.pos.z() + m.halfW + kArrowHitHalfW) {
                        // t727 夜行者弹射物免疫（spec「弓箭…攻击不到会瞬移」；机制等价 MC 末影人远程免疫）。
                        //   t829① race 修复：命中即**强制瞬移**（绕过 teleportCooldown——旧版冷却内 arrow 命中只
                        //   nightwalkerDodge 早退 = 箭无声穿身零反馈，用户「有时不瞬移直接穿过」）；瞬移落定则
                        //   **箭一并移除**（remove=true，机制等价 MC 末影人对投射物的「弹开并消耗」——不再保留
                        //   穿透原站位继续飞的箭，杜绝 dodge 后箭继续命中其后 mob / 落地可拾的间接收益）。瞬移
                        //   全试失败（被围 / 地形不允许，teleportEntity 返 false）→ 退化为普通命中（扣血 + 击退 +
                        //   箭消失）——「命中必有结算」，两者都不再出现零反馈穿身。近战路径 nightwalkerDodge 的
                        //   冷却门 + caller 30% 掷骰保留不变（近战节奏另管）。
                        if (m.mobType == MobNightwalker) {
                            Entity &nm = m_entities[size_t(mi)];
                            // R19.13 终审 B-L1：m 是槽引用，teleportEntity 改的就是同一对象——日志若比
                            //   m_entities[mi].pos != m.pos 是同对象自比较恒 false（瞬移成功也记 forced hit）。
                            //   瞬移前快照旧位，日志改比快照（真实判据：位置变了 = 闪避成功）。
                            const QVector3D oldPos = nm.pos;
                            if (!teleportEntity(mi, nm, world, kNightwalkerTeleportMin, kNightwalkerTeleportMax,
                                                /*clearAggro=*/false)) { // review26 #8：投射物闪避只位移不清仇恨（MC 1.0 口径）
                                // 瞬移失败兜底：普通命中（伤害 / 击退 / 音 / 移除，同下常规分支语义）。
                                damageEntity(mi, e.arrowDamage);
                                // t960 燃箭（瞬移失败兜底同点燃——「普通命中」语义对齐下方常规分支）。
                                if (e.arrowIgniteSec > 0.0f) ignite(mi, e.arrowIgniteSec);
                                const float fhx = e.vx, fhz = e.vz;
                                const float flen = std::sqrt(fhx * fhx + fhz * fhz);
                                // t960 震击：击退强度用箭实体携行值（spawnArrowPlayer 写 = 基线 × 震击倍率；
                                //   0 防御兜底基线——玩家箭 spawn 后恒 >0，理论不可达）。
                                if (flen > 1e-3f)
                                    knockback(mi, fhx / flen, fhz / flen,
                                              e.arrowKbStrength > 0.0f ? e.arrowKbStrength : kArrowKnockbackStrength);
                                emit arrowHitMob(m.mobType);
                            }
                            qCInfo(lcEnt) << "player arrow deflected by nightwalker" << mi
                                          << (m_entities[size_t(mi)].pos != oldPos ? "(teleport dodge)" : "(forced hit)");
                            remove = true; // 箭命中夜行者即消耗（瞬移闪避或普通命中皆移除，t829①）
                            break;        // 本帧不再判定其它 mob
                        }
                        damageEntity(mi, e.arrowDamage); // 扣血 + 红闪 + 归零 mobDied（内含 dead/越界/amount 守）
                        // t553 箭命中击退（机制对齐 MC 1.0 箭命中推开生物；用户「雪球应像箭一样击退」的参照 ——
                        //   本工程箭此前也不击退，一并补上使投射物行为一致）。方向 = 箭水平速度归一化（箭 → mob）；
                        //   强度 kArrowKnockbackStrength。箭已嵌入 / 贴脸慢速时 vx≈vz≈0 → knockback 内 yaw 兜底。
                        // t960 震击：强度改用箭实体携行值（spawnArrowPlayer 写 = 基线 × 震击倍率；Game 层由
                        //   EnchantRegistry::bowKnockbackMultiplier 出倍率——Entities 不读 Game，分层铁律）。
                        // t960 燃箭：命中点燃（时长 Game 层 bowIgniteSeconds 出、spawn 写实体；damageEntity
                        //   **之后** ignite——t919 燃焰同序：致死击 ignite 内 dead 守卫早退，尸体不燃）。
                        if (e.arrowIgniteSec > 0.0f) ignite(mi, e.arrowIgniteSec);
                        const float ahx = e.vx, ahz = e.vz;
                        float alen = std::sqrt(ahx * ahx + ahz * ahz);
                        if (alen > 1e-3f)
                            knockback(mi, ahx / alen, ahz / alen,
                                      e.arrowKbStrength > 0.0f ? e.arrowKbStrength : kArrowKnockbackStrength);
                        emit arrowHitMob(m.mobType); // t304 命中音（呈现层 playMobHurt，同近战 attackMob→mobAttacked）
                        qCInfo(lcEnt) << "player arrow hit mob" << mi << "for" << e.arrowDamage << "HP";
                        remove = true;
                        break; // 命中首个即止（箭消失，不穿透）
                    }
                }
            }
            // t712 批「敌对 mob 主动攻击铁傀儡」配套：骷髅箭（arrowFromPlayer=false）命中**铁傀儡**（aiArcher
            //   golem 分支射出的目标）→ damageEntity(kArrowDamage) + 击退 + 移除箭。仅铁傀儡（骷髅不会朝其它
            //   mob 射箭 —— aiArcher golem 分支只在 nearestIronGolem 命中时开火；范围限定防骷髅误伤羊群 /
            //   队友改变既有生态）。AABB 外扩同玩家箭（kArrowHitHalfW）；伤害固定 kArrowDamage（同命中玩家）。
            //   t923 扩到**狼**：箭道上的狼身体挡箭（机制等价 MC 1.0 箭不穿透生物体——宠物挡箭实战场景）；
            //   命中驯服狼 → 仇恨传递反击射手（wolfRetaliateAgainst）。其余 mob 仍穿透（生态稳定不扩）。
            if (!remove && !e.arrowFromPlayer) {
                for (int mi = 0; mi < int(m_entities.size()); ++mi) {
                    const Entity &m = m_entities[size_t(mi)];
                    if (!m.alive || m.kind != Mob || m.dead
                        || (m.mobType != MobIronGolem && m.mobType != MobWolf)) continue; // 仅活体铁傀儡 / 狼
                    const float ex2 = m.pos.x() - m.halfW - kArrowHitHalfW;
                    const float ey2 = m.pos.y() - m.halfH - kArrowHitHalfW;
                    const float ez2 = m.pos.z() - m.halfW - kArrowHitHalfW;
                    if (next.x() >= ex2 && next.x() <= m.pos.x() + m.halfW + kArrowHitHalfW
                        && next.y() >= ey2 && next.y() <= m.pos.y() + m.halfH + kArrowHitHalfW
                        && next.z() >= ez2 && next.z() <= m.pos.z() + m.halfW + kArrowHitHalfW) {
                        damageEntity(mi, kArrowDamage);
                        const float ahx = e.vx, ahz = e.vz;
                        float alen = std::sqrt(ahx * ahx + ahz * ahz);
                        if (alen > 1e-3f) knockback(mi, ahx / alen, ahz / alen, kArrowKnockbackStrength);
                        // t923 仇恨传递：骷髅箭命中驯服狼（挡箭的宠物）→ 狼群反击射手（arrowShooter = 发射
                        //   骸骨槽，同命中玩家注册 m_wolfTarget 的 t480 先例；受害者/攻击者校验在入口内）。
                        if (m.mobType == MobWolf && m.wolfTamed
                            && e.arrowShooter >= 0 && e.arrowShooter < int(m_entities.size()))
                            wolfRetaliateAgainst(mi, e.arrowShooter);
                        qCInfo(lcEnt) << "skeleton arrow hit mob" << mi << "for" << kArrowDamage << "HP";
                        remove = true;
                        break;
                    }
                }
            }

            // 越界兜底（飞出世界 XZ 边界 / 跌出底部）→ 移除（防永久飞行堆积）。
            if (!remove) {
                if (next.x() < 0.0f || next.z() < 0.0f
                    || next.x() > worldW || next.z() > worldD || next.y() < 0.0f) {
                    remove = true;
                }
            }

            if (remove) {
                toRemove.push_back(idx);
                dirty = true;
                if (hitPlayer) qCInfo(lcEnt) << "arrow hit player for" << hitPlayerDmg << "HP";
            } else {
                e.pos = next; // 继续飞行
                dirty = true;
            }
            continue; // Arrow 不走 Mob AI / resting / 击退衰减
        }

        // --- Snowball（t482 雪傀儡抛雪球）：抛物 + 敌对 mob 命中（低伤害 + 减速）+ 方块命中移除 + 寿命兜底 ---
        if (e.kind == Snowball) {
            e.arrowLife -= float(dt); // 复用 arrowLife 作寿命倒计时
            e.vy -= kGravity * float(dt); // 抛物：重力改 vy（与世界重力同值 → 弧自然）
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt);
            bool remove = false;
            bool hitBlock = false; // t505 命中方块 → 破碎粒子（区别寿命到 / 越界，仅方块 / mob 命中迸雪沫）
            // 寿命到 → 移除（飞行未命中兜底，防永久滞留堆积）。
            if (e.arrowLife <= 0.0f) remove = true;
            // mob 命中（t553 **先于方块判定**）：雪球（点）是否落在任一**活体 mob** 的 AABB（外扩 kSnowballHitHalfW
            //   提升近距命中率）内。命中首个 → **击退所有 mob + 减速**（机制对标 MC 1.0：雪球打任意生物都击退，
            //   含猪牛羊等被动 —— 旧版「只打敌对」致玩家丢向被动生物毫无反应，用户报「雪球打生物不击退」）+ 伤害
            //   按发射者分流（t505：golem 雪球 damageEntity(kSnowballDamage) 扣血；玩家雪球 damage=0 → 不扣血但设
            //   hurtFlash 红闪）+ 移除雪球。**方块判定放后**（先 mob 后方块：贴墙 mob 的命中点同时落入身后墙块时，
            //   不应被「先撞墙」吞掉 → mob 优先命中；远离墙的飞行路径与旧版一致）。跳过非 alive / 非 Mob / dead。
            if (!remove) {
                for (int mi = 0; mi < int(m_entities.size()); ++mi) {
                    const Entity &m = m_entities[size_t(mi)];
                    if (!m.alive || m.kind != Mob || m.dead) continue; // 所有活体 mob（含被动；玩家非 Mob 穿过）
                    if (mi == e.snowballThrower && m_entities[size_t(mi)].spawnSerial == e.snowballThrowerSerial)
                        continue; // t553 排除发射者自身（防雪球首帧误击自己）。rv-low-batch1 加代际比对：槽复用
                                  //   换任（新生物进驻同槽）后 serial 不同 → 不再误排除新生物（修「雪球打不到某格生物」）
                    const float ex2 = m.pos.x() - m.halfW - kSnowballHitHalfW;
                    const float ey2 = m.pos.y() - m.halfH - kSnowballHitHalfW;
                    const float ez2 = m.pos.z() - m.halfW - kSnowballHitHalfW;
                    if (next.x() >= ex2 && next.x() <= m.pos.x() + m.halfW + kSnowballHitHalfW
                        && next.y() >= ey2 && next.y() <= m.pos.y() + m.halfH + kSnowballHitHalfW
                        && next.z() >= ez2 && next.z() <= m.pos.z() + m.halfW + kSnowballHitHalfW) {
                        // t727 夜行者弹射物免疫（spec「雪球…攻击不到会瞬移」）：命中夜行者 → 不击退 / 不减速 / 不落定
                        //   （雪球穿过），夜行者瞬移躲避。仅夜行者免疫（其余 mob 走下方既有击退 / 减速 / 伤害分流）。
                        //   review27 #5：clearAggro=false——雪球 0 伤害 4 雪块可无限复购，清仇恨 = 免费远程「净化」
                        //   + 打断攻击前摇 exploit；闪避只位移，enraged/rageTimer/windupTimer 保持（箭 / 浮标
                        //   review26 #8 同口径，MC 1.0 投射物闪避不解仇恨）。
                        if (m.mobType == MobNightwalker) {
                            nightwalkerDodge(mi, world, /*clearAggro=*/false);
                            break; // 穿过（不落定），本帧不再判定其它 mob（夜行者已跳走）
                        }
                        // t505 按发射者分流伤害：golem(damage>0) → 扣血走 damageEntity（红闪 + 归零 mobDied 掉落）；
                        //   player(damage==0) → damageEntity 因 amount<=0 早退不扣血，改手动设 hurtFlash 触发红闪。
                        //   仅敌对扣血 / 红闪（被动生物 0 伤害 0 红闪，机制对标 MC 雪球不伤友好生物）；击退 / 减速
                        //   对所有 mob 生效。
                        if (m.hostile && e.snowballDamage > 0) {
                            damageEntity(mi, e.snowballDamage); // golem 雪球：敌对扣血 + 红闪（复用受击链）
                        } else if (m.hostile) {
                            // 玩家雪球打敌对：0 伤害但触发红闪（damageEntity 守 amount<=0 不闪，手动设 hurtFlash）。
                            Entity &tm = m_entities[size_t(mi)];
                            if (tm.alive && tm.kind == Mob && !tm.dead) {
                                tm.hurtFlash = kHurtFlashTime; // 红闪（QML hurtFlashAt>0 → baseColor 红；t935 起由
                                // 下方 dirty → tick 末 notify 的槽位指纹差分捕获 hurtFlash 变化 → bump 本槽监视器）
                            }
                        }
                        // 击退（t553 加大到 kSnowballKnockbackStrength=2.0：追尾敌对 mob 也被明显推开；t505 玩家雪球
                        //   机制对标 MC 雪球击退；golem 雪球也叠加）—— 方向 = 雪球水平速度归一化（雪球 → mob）。
                        const float hvx = e.vx, hvz = e.vz;
                        float hlen = std::sqrt(hvx * hvx + hvz * hvz);
                        if (hlen > 1e-3f) knockback(mi, hvx / hlen, hvz / hlen, kSnowballKnockbackStrength);
                        m_entities[size_t(mi)].slowTimer = kSnowSlowDuration; // 轻微减速（QML isSlowedAt 蓝调）
                        qCInfo(lcEnt) << "snowball hit mob" << mi << "hostile=" << int(m.hostile)
                                      << "damage=" << e.snowballDamage
                                      << "+slow" << kSnowSlowDuration << "s";
                        remove = true;
                        break; // 命中首个即止（雪球消失，不穿透）
                    }
                }
            }
            // 方块命中 → 移除（雪球砸方块即碎，机制等价 MC 雪球撞方块碎裂；不产生方块变化）。mob 命中已早退，
            //   贴墙 mob 不会先撞墙。仅未命 mob 的飞行 / 落地雪球走此。
            if (!remove) {
                const int bx = qFloor(next.x()), by = qFloor(next.y()), bz = qFloor(next.z());
                if (by >= 0 && world->isSolid(bx, by, bz)) { remove = true; hitBlock = true; }
            }
            // 越界兜底（飞出世界 XZ 边界 / 跌出底部）→ 移除（防永久飞行堆积）。
            if (!remove) {
                if (next.x() < 0.0f || next.z() < 0.0f
                    || next.x() > worldW || next.z() > worldD || next.y() < 0.0f) {
                    remove = true;
                }
            }
            // t505 方块命中 → emit snowballBreak 让呈现层迸发雪沫粒子（命中点 = next，雪球碎裂处）。mob 命中不迸发
            //   （雪球贴 mob 消失，机制对标 MC 雪球打 mob 不碎裂成雪沫）。寿命到 / 越界移除不迸发（无命中点）。
            if (remove && hitBlock) {
                emit snowballBreak(next.x(), next.y(), next.z());
            }
            if (remove) {
                toRemove.push_back(idx);
                dirty = true;
            } else {
                e.pos = next; // 继续飞行
                dirty = true;
            }
            continue; // Snowball 不走 Mob AI / resting / 击退衰减
        }

        // --- Egg（t583 鸡蛋投掷物）：抛物 + 活体 mob / 方块命中即碎（1/8 孵小鸡）+ 寿命兜底 ---
        //   机制等价 MC 1.0 egg：0 伤害投掷物（命中 mob 不伤只击退，t608 统一手持 / 发射器口径）+ 命中处
        //   1/8 概率孵 1 只小鸡（用户「丢出来可以砸出来小鸡」）。判定序同雪球：先 mob 后方块（贴墙 mob
        //   不被撞墙吞掉）。
        if (e.kind == Egg) {
            e.arrowLife -= float(dt); // 复用 arrowLife 作寿命倒计时
            e.vy -= kGravity * float(dt); // 抛物：重力改 vy（与世界重力同值 → 弧自然）
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt);
            bool remove = false;
            // 寿命到 → 移除（飞行未命中兜底，防永久滞留堆积；不碎裂不孵化）。
            if (e.arrowLife <= 0.0f) remove = true;
            // mob 命中（先于方块判定，同雪球）：鸡蛋（点）落入任一活体 mob 的 AABB（外扩 kEggHitHalfW）→
            //   碎裂移除 + 0 伤害击退（t608 用户口径「和雪球一个逻辑，没有伤害只有击退」——沿投掷方向
            //   knockback 同雪球 kSnowballKnockbackStrength，不扣血不红闪不减速；机制等价 MC 1.0 蛋投掷物
            //   命中生物仅击退不伤）。孵化分支在下方统一命中处理（命中点 = next，含 mob 命中）。
            if (!remove) {
                for (int mi = 0; mi < int(m_entities.size()); ++mi) {
                    const Entity &m = m_entities[size_t(mi)];
                    if (!m.alive || m.kind != Mob || m.dead) continue; // 所有活体 mob；玩家非 Mob 穿过
                    const float ex2 = m.pos.x() - m.halfW - kEggHitHalfW;
                    const float ey2 = m.pos.y() - m.halfH - kEggHitHalfW;
                    const float ez2 = m.pos.z() - m.halfW - kEggHitHalfW;
                    if (next.x() >= ex2 && next.x() <= m.pos.x() + m.halfW + kEggHitHalfW
                        && next.y() >= ey2 && next.y() <= m.pos.y() + m.halfH + kEggHitHalfW
                        && next.z() >= ez2 && next.z() <= m.pos.z() + m.halfW + kEggHitHalfW) {
                        // t727 夜行者弹射物免疫（spec「鸡蛋…攻击不到会瞬移」）：命中夜行者 → 不击退 / 不落定（蛋穿
                        //   过），夜行者瞬移躲避。仅夜行者免疫（其余 mob 走下方既有击退）。
                        //   review27 #5：clearAggro=false——鸡蛋同雪球是 0 伤害无限复购投掷物，清仇恨 = 免费远程
                        //   「净化」exploit；闪避只位移（箭 / 浮标 / 雪球全族同口径）。
                        if (m.mobType == MobNightwalker) {
                            nightwalkerDodge(mi, world, /*clearAggro=*/false);
                            break; // 穿过（不落定），本帧不再判定其它 mob（夜行者已跳走）
                        }
                        // 击退：方向 = 鸡蛋水平速度归一化（鸡蛋 → mob），同雪球命中分支模式（慢速退化由
                        //   knockback 内 yaw 兜底）。不 damageEntity（0 伤害语义，机制对标 MC 蛋投掷物）。
                        const float evx = e.vx, evz = e.vz;
                        float elen = std::sqrt(evx * evx + evz * evz);
                        if (elen > 1e-3f) knockback(mi, evx / elen, evz / elen, kSnowballKnockbackStrength);
                        qCInfo(lcEnt) << "egg hit mob" << mi << "(0 damage, knockback only)";
                        remove = true;
                        break; // 命中首个即止（鸡蛋消失，不穿透）
                    }
                }
            }
            // 方块命中 → 碎裂移除（机制等价 MC 鸡蛋砸方块碎裂）。mob 命中已早退。
            if (!remove) {
                const int bx = qFloor(next.x()), by = qFloor(next.y()), bz = qFloor(next.z());
                if (by >= 0 && world->isSolid(bx, by, bz)) remove = true;
            }
            // 越界兜底（飞出世界 XZ 边界 / 跌出底部）→ 移除（防永久飞行堆积；不碎裂不孵化）。
            if (!remove) {
                if (next.x() < 0.0f || next.z() < 0.0f
                    || next.x() > worldW || next.z() > worldD || next.y() < 0.0f) {
                    remove = true;
                }
            }
            // 命中（mob / 方块）→ emit eggBreak 迸蛋壳碎屑粒子 + 1/8 概率孵化小鸡（机制等价 MC 1.0 egg
            //   1/8 出鸡；用户核心诉求）。寿命到 / 越界不触发（无命中点）。
            if (remove && e.arrowLife > 0.0f && next.y() >= 0.0f
                && next.x() >= 0.0f && next.z() >= 0.0f
                && next.x() <= worldW && next.z() <= worldD) {
                emit eggBreak(next.x(), next.y(), next.z());
                if (float(QRandomGenerator::global()->bounded(int(kEggHatchDenominator) * 1000)) < 1000.0f) {
                    // 孵化位 review L8：旧版取命中点 next 所在格（floor）—— next 已穿入实体格，鸡蛋砸 ≥2 格
                    //   厚墙时该格在墙**内部** → 小鸡生在墙里（卡死 + 窒息）。改为上一帧位置 e.pos（蛋碎前
                    //   所在的空气侧格）：贴墙命中 → e.pos 仍在其前面的空气格 → 小鸡生在墙外的地面上，
                    //   spawnMobCore 放格中心 + halfH，重力 tick 贴地表（地面弹 / 砸地面时 e.pos = 飞行末格，
                    //   小鸡落在命中面上方，落地行为保持）。spawnMobCore 不 emit（本 tick 末尾统一 emit）；
                    //   spawn 可 acquireSlot push_back → vector 扩容后本循环内的 Entity& e 悬垂 —— 终审修
                    //   L1（B8 同型残留）：e.pos **先拷局部 hatchPos**，spawn 与下方日志一律读局部量（旧代码
                    //   在 spawn 之后再读 e.pos 打日志 = 悬垂引用读，UB；旧注释声称「后续只读局部 next/idx」
                    //   与事实不符，一并订正），同 tickBreeding 主循环外 spawn 的安全纪律。
                    const QVector3D hatchPos = e.pos;
                    const int slot = spawnMobCore(qFloor(hatchPos.x()), qFloor(hatchPos.y()), qFloor(hatchPos.z()),
                                                  MobChicken, QStringLiteral("#f5f0e4"), 0);
                    if (slot >= 0) {
                        // 幼崽态（复用 t400 繁殖幼崽机制：baby=true → QML babyScaleAt 0.5 缩小 + growTimer
                        //   到 0 长大成体，机制等价 MC 鸡蛋孵出的是小鸡非成年鸡）。maxHealth 传 0 →
                        //   spawnMobCore 内部用 kDefaultMaxHealth（同 spawn egg 路径）。
                        m_entities[size_t(slot)].baby = true;
                        m_entities[size_t(slot)].growTimer = m_babyGrowSec;
                        // t1025 幼崽缩放基建对齐（同 tickBreeding 产崽段：物理盒 ×kBabyScale +
                        //   pos.y 贴底重锚——蛋孵小鸡与繁殖幼崽同款幼体形态，单点语义不分叉）。
                        //   t1046 血量口径改写（parity 台账低-1）：MC Baby 血量 = 成体上限满血，
                        //   旧「maxHealth/2 减半」退役；health 保持 spawnMobCore 满血语义。
                        {
                            Entity &hatch = m_entities[size_t(slot)];
                            const float adultHalfH = hatch.halfH;
                            hatch.halfW *= kBabyScale;
                            hatch.halfH *= kBabyScale;
                            hatch.health = hatch.maxHealth;
                            hatch.pos.setY(hatch.pos.y() - (adultHalfH - hatch.halfH));
                        }
                        qCInfo(lcEnt) << "egg hatched baby chicken at" << hatchPos;
                    }
                }
            }
            if (remove) {
                toRemove.push_back(idx);
                dirty = true;
            } else {
                e.pos = next; // 继续飞行
                dirty = true;
            }
            continue; // Egg 不走 Mob AI / resting / 击退衰减
        }

        // --- Fireball（t728 燃烬者火球）：直线弹道（重力 0）+ 方块命中（消失 + 20% 点燃邻可燃）+ 玩家命中
        //   （伤害 5 + 着火）+ mob 命中（伤害 5 + 着火）+ 寿命 / 越界兜底 ---
        //   机制等价 MC 1.0 烈焰人火球：直线飞行（无重力）、命中生物扣血 + 点燃、命中方块消失。判定序同箭 /
        //   雪球：先 mob / 玩家后方块（贴墙目标不被先撞墙吞掉，但玩家判定在 mob 前 —— 火球朝玩家飞，命中玩家
        //   优先于命中玩家身后 mob）。
        if (e.kind == Fireball) {
            e.arrowLife -= float(dt); // 复用 arrowLife 作寿命倒计时
            // 直线弹道：重力 0（火球不抛物，机制等价 MC 烈焰人火球直线惯性飞行）；速度恒定不变。
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt);
            bool remove = false;
            bool hitBlock = false;
            // 寿命到 → 移除（飞行未命中兜底，防永久滞留堆积；不引爆不点燃）。
            if (e.arrowLife <= 0.0f) remove = true;
            // **玩家命中**（先于方块 / mob 判定：火球朝玩家飞，命中点优先归玩家）：火球（点）落点是否在玩家
            //   AABB（外扩 kFireballHitHalfW；玩家半宽/半高用 tick 传入的 listenerHalfW/listenerHeight + listener
            //   （玩家脚位）近似）内。命中 → 伤害 kEmberlingFireballDamage=5（mobAttackedPlayer 携 MobEmberling →
            //   死因尾追 Emberling）+ emit emberFireballHitPlayer()（呈现层 ignite 玩家）+ 移除火球。受击全局节流
            //   （m_playerHitCooldown）串行化（同箭命中玩家模式，防连发灼烧叠加瞬死）。
            //   t891②：**玩家侧火球不判玩家命中**（fireballShooter==-1 = 无 mob 发射者 → 玩家烈焰弹）。出生点
            //   在眼位前 0.5、恰在玩家外扩命中盒（halfW 0.3 + 0.3 = 0.6）内 → 低头 / 俯角发射首帧必自击
            //   5HP + 点燃；MC 投射物对所有者豁免同语义。Emberling 喷火恒走 flushPendingShots 写 shooter>=0
            //   → 本豁免对其零影响。
            if (!remove && e.fireballShooter >= 0 && playerTargetable && m_playerHitCooldown <= 0.0f) {
                const float px = listener.x(), py = listener.y(), pz = listener.z();
                const float pEx = px - listenerHalfW - kFireballHitHalfW;
                const float pEy = py - kFireballHitHalfW;
                const float pEz = pz - listenerHalfW - kFireballHitHalfW;
                if (next.x() >= pEx && next.x() <= px + listenerHalfW + kFireballHitHalfW
                    && next.y() >= pEy && next.y() <= py + listenerHeight + kFireballHitHalfW
                    && next.z() >= pEz && next.z() <= pz + listenerHalfW + kFireballHitHalfW) {
                    m_playerHitCooldown = kPlayerHitThrottle;
                    float kbX, kbZ;
                    if (e.vx * e.vx + e.vz * e.vz > 1e-3f) {
                        const float hl = std::sqrt(e.vx * e.vx + e.vz * e.vz);
                        kbX = e.vx / hl; kbZ = e.vz / hl;
                    } else { kbX = 0.0f; kbZ = 1.0f; }
                    emit mobAttackedPlayer(kEmberlingFireballDamage, e.mobType, kbX, kbZ); // 自身 mobType=MobEmberling
                    emit emberFireballHitPlayer(); // 着火呈现（Main.qml → 点燃玩家）
                    qCInfo(lcEnt) << "emberling fireball hit player for" << kEmberlingFireballDamage << "HP";
                    remove = true;
                }
            }
            // **mob 命中**（未命玩家且未 remove）：火球（点）落入任一活体 mob 的 AABB（外扩 kFireballHitHalfW）
            //   → damageEntity(kEmberlingFireballDamage) 扣血 + 设 fireTimer 着火（t724 点燃判据先例）+ 移除火球。
            //   跳过非 alive / 非 Mob / dead。命中首个即止（火球消失，不穿透）。
            if (!remove) {
                for (int mi = 0; mi < int(m_entities.size()); ++mi) {
                    const Entity &m = m_entities[size_t(mi)];
                    if (!m.alive || m.kind != Mob || m.dead) continue;
                    // 审查修 B1（t724-t729 复盘）：跳过发射者（slot + serial 双查，同雪球 t553 先例）——
                    //   火球出生在发射者外扩命中盒内，不排除则首帧判中发射者自己（自伤 5HP + 点燃，约 4 发自杀）。
                    if (mi == e.fireballShooter && m.spawnSerial == e.fireballShooterSerial) continue;
                    const float ex2 = m.pos.x() - m.halfW - kFireballHitHalfW;
                    const float ey2 = m.pos.y() - m.halfH - kFireballHitHalfW;
                    const float ez2 = m.pos.z() - m.halfW - kFireballHitHalfW;
                    if (next.x() >= ex2 && next.x() <= m.pos.x() + m.halfW + kFireballHitHalfW
                        && next.y() >= ey2 && next.y() <= m.pos.y() + m.halfH + kFireballHitHalfW
                        && next.z() >= ez2 && next.z() <= m.pos.z() + m.halfW + kFireballHitHalfW) {
                        damageEntity(mi, kEmberlingFireballDamage); // 扣血 + 红闪 + 归零 mobDied（复用受击链）
                        // t923 仇恨传递：火球命中驯服狼 → 狼群反击发射者（slot+serial 双查防槽复用误绑，
                        //   同上方发射者排除的成对契约；受害者/攻击者活体校验在 wolfRetaliateAgainst 内）。
                        if (m.mobType == MobWolf && e.fireballShooter >= 0
                            && e.fireballShooter < int(m_entities.size())
                            && m_entities[size_t(e.fireballShooter)].spawnSerial == e.fireballShooterSerial)
                            wolfRetaliateAgainst(mi, e.fireballShooter);
                        // t453 点燃目标（火伤）：fireTimer 刷新到 kFireDuration（t724 点燃判据先例；若目标自身火
                        //   免疫如另一燃烬者无实际火伤，但仍设 fireTimer —— 免疫由火烧分支跳过不伤）。
                        Entity &tm = m_entities[size_t(mi)];
                        // 审查修 B2（t724-t729 复盘）：火免疫者（燃烬者）不再设 fireTimer —— 免疫分支跳过
                        //   衰减后设了永不归零 = 永久火焰特效（isBurningAt 据 fireTimer>0 显焰）。
                        if (tm.alive && tm.kind == Mob && !tm.dead && tm.mobType != MobEmberling)
                            tm.fireTimer = std::max(tm.fireTimer, kFireDuration);
                        qCInfo(lcEnt) << "emberling fireball hit mob" << mi << "for"
                                      << kEmberlingFireballDamage << "HP";
                        remove = true;
                        break;
                    }
                }
            }
            // 方块命中 → 消失 + 点燃掷骰（Emberling ~20% / 玩家烈焰弹 100% = 撞击必生火——per-entity
            //   fireballIgnitePct，t891②；机制等价 MC 1.0 火球命中方块消失，可燃则燃）。mob / 玩家命中已
            //   早退，贴墙目标不会先撞墙。仅未命目标的飞行火球走此。
            if (!remove) {
                const int bx = qFloor(next.x()), by = qFloor(next.y()), bz = qFloor(next.z());
                // 审查修 B12（t724-t729 复盘）：命中判定并入水格 —— 旧版只判 isSolid（水非 solid）→ 火球
                //   水下飞行不消失；入水即熄（remove，不参与点燃，机制等价 MC 1.0 火球遇水熄灭）。
                const bool hitWater = (by >= 0 && world->blockAt(bx, by, bz) == BlockRegistry::Water);
                if (by >= 0 && (world->isSolid(bx, by, bz) || hitWater)) {
                    remove = true;
                    hitBlock = true;
                    // t891② 点燃改**打火石同源口径**（机制对齐 MC 火弹落地放火 + t843 直燃语义；掷骰读
                    //   per-entity fireballIgnitePct——Emberling 保持 ~20% 概率感、玩家烈焰弹 100% 必生火）：
                    //   ① 命中格**可燃** → igniteFlammableAt 单一入口直燃进燃烧态（栅格 id 不变 + 面火
                    //      overlay + 计时烧毁 + 同态蔓延；湿燃料防火带 / 门整扇湿判收口在入口内，与
                    //      打火石 / tickFire 蔓延三入口同口径）；
                    //   ② 非可燃 → 火球**来向格**（e.pos 所在格 = 撞面前最后的空气侧格）为 Air 时置立地火
                    //      （setBlock 5 参数 → blockPlaced → Main.qml fireHost 挂 delegate，审查修 B3 同款；
                    //      ==Air 门拦含水 / 被占格）。旧「命中格正上方置火」（顶面延烧启发式）被 ① 取代
                    //      ——打墙面时火贴墙燃（MC 观感）而非浮上墙顶。
                    //   审查修 B12：水命中不点燃（熄灭）；落火目标格 == Air 门不变。
                    if (!hitWater
                        && QRandomGenerator::global()->bounded(100) < e.fireballIgnitePct) {
                        if (!world->igniteFlammableAt(bx, by, bz)) {
                            int px = qFloor(e.pos.x()), py = qFloor(e.pos.y()), pz = qFloor(e.pos.z());
                            // review27 #14①：玩家朝脚下近距直射（直击豁免外）撞非可燃块时，来向格 ==
                            //   玩家自身格 → 立地火把发射者自己点着（火系统按 AABB 接触点燃）。玩家侧火球
                            //   （fireballShooter==-1）来向格与玩家 AABB 相交 → 偏移到首个「Air 且不与玩家
                            //   AABB 相交」的邻格再落火（火球水平来向一格优先 = 火「溅」过撞击点，四向兜底）；
                            //   无合格邻格 → 不落火（宁缺勿自燃）。燃烬者火球（shooter>=0）不偏移——落火
                            //   贴玩家脚边是敌意投射物的合法后果（MC 语义），只收口「自己射自己」。
                            bool dropFire = true;
                            if (e.fireballShooter < 0) {
                                const float plMinX = listener.x() - listenerHalfW, plMaxX = listener.x() + listenerHalfW;
                                const float plMinY = listener.y(), plMaxY = listener.y() + listenerHeight;
                                const float plMinZ = listener.z() - listenerHalfW, plMaxZ = listener.z() + listenerHalfW;
                                const auto cellHitsPlayer = [&](int cx, int cy, int cz) {
                                    return float(cx) < plMaxX && float(cx + 1) > plMinX
                                        && float(cy) < plMaxY && float(cy + 1) > plMinY
                                        && float(cz) < plMaxZ && float(cz + 1) > plMinZ;
                                };
                                if (cellHitsPlayer(px, py, pz)) {
                                    int ox = 0, oz = 0; // 水平来向一格（垂直下射 hvx/hvz≈0 → 四向兜底）
                                    if (e.vx * e.vx + e.vz * e.vz > 1e-4f) {
                                        if (qAbs(e.vx) >= qAbs(e.vz)) { ox = e.vx > 0.0f ? 1 : -1; }
                                        else { oz = e.vz > 0.0f ? 1 : -1; }
                                    }
                                    const int cand[5][2] = { {ox, oz}, {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
                                    dropFire = false;
                                    for (const auto &c : cand) {
                                        const int qx = px + c[0], qz = pz + c[1];
                                        if ((qx == px && qz == pz) || qx < 0 || qz < 0
                                            || qx >= world->width() || qz >= world->depth())
                                            continue;
                                        if (!cellHitsPlayer(qx, py, qz)
                                            && world->blockAt(qx, py, qz) == BlockRegistry::Air) {
                                            px = qx;
                                            pz = qz;
                                            dropFire = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (dropFire && py >= 0 && py < world->height()
                                && world->blockAt(px, py, pz) == BlockRegistry::Air) {
                                // 来向格是空气 → 落火（撞面外的空气侧；t724 火系统承接蔓延）。
                                // review27 #14③：先试下界门框（与打火石 playercontroller 路径同源口径——
                                //   tryIgniteNetherPortal 成门 → 开口整面 NetherPortal 不落火；烈焰弹撞
                                //   黑曜石门框内腔点火与打火石等效，替代此前只在打火石链生效的门检测）。
                                if (world->tryIgniteNetherPortal(px, py, pz)) {
                                    qCInfo(lcEnt) << "fireball lit nether portal at" << px << py << pz;
                                } else {
                                    world->setBlock(px, py, pz, BlockRegistry::Fire, 0);
                                    qCInfo(lcEnt) << "fireball ignited at" << px << py << pz;
                                }
                            }
                        } else {
                            qCInfo(lcEnt) << "fireball ignited flammable block at" << bx << by << bz;
                        }
                    }
                }
            }
            // 越界兜底（飞出世界 XZ 边界 / 跌出底部）→ 移除（防永久飞行堆积）。
            if (!remove) {
                if (next.x() < 0.0f || next.z() < 0.0f
                    || next.x() > worldW || next.z() > worldD || next.y() < 0.0f) {
                    remove = true;
                }
            }
            if (remove) {
                toRemove.push_back(idx);
                dirty = true;
            } else {
                e.pos = next; // 继续直线飞行
                dirty = true;
            }
            continue; // Fireball 不走 Mob AI / resting / 击退衰减
        }

        // --- EnderEye（t729 暗渊之眼投射物；t757 两段式定位）：寻路要塞 + 飞距结算（变掉落物 / 碎裂无掉落）---
        //   机制等价 MC 1.0 末影之眼 ender eye：右键掷出 → 寻路要塞（t757 远段升空平飞指示 / 近段下探逼近，
        //   速度 ~kEnderEyeSpeed=4，玩家可侧身看它飞）→ 飞行一段后判定：80% 变**掉落物实体**（emit
        //   enderEyeBecameItem，可捡回）、20% 碎裂（缩小淡出 + 玻璃碎裂粒子，无掉落）。本分支两态：飞行态
        //   （enderEyeShatter==0，两段转向 + 位移 + distLeft 递减）/ 碎裂态（enderEyeShatter>0，仅倒计，QML
        //   delegate 播动画；归零释放槽无掉落）。无重力（非抛物）、无方块碰撞（机制等价 MC 末影之眼透过地形
        //   感应要塞；豆腐脑如穿墙亦可接受，距离短）。
        if (e.kind == EnderEye) {
            if (e.enderEyeShatter > 0.0f) {
                // 碎裂态：倒计时（QML delegate 据 shatteringAt 播缩小淡出 + 玻璃碎裂粒子动画），归零 → 释放槽
                //   （无掉落物，机制等价 MC 末影之眼破裂无回收）。
                e.enderEyeShatter -= float(dt);
                dirty = true;
                if (e.enderEyeShatter <= 0.0f) {
                    toRemove.push_back(idx);
                    dirty = true;
                }
                continue; // 碎裂态不走飞行 / Mob AI / resting
            }
            // 飞行态（t757 两段式）：每 tick 先按段算「目标方向」，再把当前速度方向向它指数趋近（平滑转向），
            //   然后恒速 kEnderEyeSpeed 位移 + 剩余距离递减（按速度模长；两段通用不动）。
            //   远段（与传送门水平距离 > kEnderEyeNearDist=50）：目标 = 水平朝要塞 + 未达巡航高度
            //   （enderEyeCruiseY = 掷出眼位 + 8）时带爬升分量（缺口 >3 格满爬 1.0，与水平 1:1 ≈ 45°；临近
            //   线性收敛到 0 → 平飞），垂直分量恒 >= 0 —— **绝不向下钻地**。旧版直线朝地下传送门中心钻，远处
            //   玩家看不出方向指示还容易把眼丢进地形里；本段让眼升到玩家上空朝要塞平飞 = 空中方向指示。
            //   平飞穿山可接受（眼睛本就无方块碰撞，机制等价「透过地形感应要塞」）。
            //   近段（≤ 50）：目标 = 传送门中心方向（可下探逼近结构 —— t729 寻路语义保留，无缝衔接远段）。
            //   无要塞（世界未建 / 空存档）：不算目标方向 → 保持 spawn 初速直线飞（Game 层初速已含 +0.25 略升
            //   偏置，兜底不崩，同旧版行为；kEnderEyeRiseOff 由此只在初速里生效，tick 不再叠加）。
            if (world->hasStronghold()) {
                const float pdx = float(world->strongholdPortalX()) + 0.5f - e.pos.x();
                const float pdz = float(world->strongholdPortalZ()) + 0.5f - e.pos.z();
                const float horizDist = std::sqrt(pdx * pdx + pdz * pdz);
                QVector3D desired;
                if (horizDist > kEnderEyeNearDist) {
                    // 远段：水平单位向量 + 爬升分量（缺口 >3 满爬 / 0..3 线性收敛 / 负值截 0 —— 绝不向下）
                    const float gap = e.enderEyeCruiseY - e.pos.y();
                    const float climb = gap > 3.0f ? 1.0f : std::max(gap, 0.0f) / 3.0f;
                    desired = QVector3D(pdx / horizDist, climb, pdz / horizDist).normalized();
                } else {
                    // 近段：直线朝传送门中心（portalY 取格坐标同 t729 语义，可下探）
                    desired = QVector3D(pdx,
                                        float(world->strongholdPortalY()) - e.pos.y(),
                                        pdz);
                    const float dl = desired.length();
                    if (dl > 1e-3f) desired = desired / dl;
                }
                // 指数趋近（帧率无关 blend = 1 − exp(−kEnderEyeTurnRate·dt)）：50 格阈值切换 / 初速与目标方向
                //   的偏差都被平滑成圆弧（阈值邻域来回穿越时方向连续，无硬折角）。归一化后回写恒定模长
                //   kEnderEyeSpeed —— 速度大小恒定，只转向（distLeft 按模长递减不受影响）。
                QVector3D curDir(e.vx, e.vy, e.vz);
                const float cl = curDir.length();
                if (cl > 1e-4f) curDir = curDir / cl; else curDir = desired;
                const float blend = 1.0f - std::exp(-kEnderEyeTurnRate * float(dt));
                QVector3D nd = curDir + (desired - curDir) * blend;
                const float nl = nd.length();
                if (nl > 1e-4f) {
                    nd = nd / nl * kEnderEyeSpeed;
                    e.vx = nd.x();
                    e.vy = nd.y();
                    e.vz = nd.z();
                }
            }
            const float spd = std::sqrt(e.vx * e.vx + e.vy * e.vy + e.vz * e.vz);
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt); // t757：独立 rise 偏置移除（爬升并入远段方向），直线位移不变
            e.pos = next;
            if (spd > 1e-4f) e.enderEyeDistLeft -= spd * float(dt);
            dirty = true;
            // 判定结算：剩余飞行距离归零 → 掷判定（80% 掉落物 / 20% 碎裂）。
            if (e.enderEyeDistLeft <= 0.0f) {
                if (QRandomGenerator::global()->bounded(100) < kEnderEyeDropChance) {
                    // 80% → 变**掉落物实体**（机制等价 MC 末影之眼落地变掉落物可回收；emit 语义事件，呈现层转发
                    //   ItemEntityManager.spawnItem 生成 item 实体，玩家走近可捡回 —— 反复使用逐步逼近要塞）。
                    // 审查修 B11（t724-t729 复盘）：眼睛无方块碰撞（设计上穿墙），结算点可能在实体格内（掉落物
                    //   卡方块永不可拾）或岩浆 / 火格（被 t724 焚毁逻辑吞掉，玩家白耗一只眼）→ 从当前位置向下
                    //   找最近「非实体且非岩浆 / 火」格再落物（穿出墙底 / 落到地表）；全柱无可落格 → 原位兜底
                    //   （极深实心柱罕见；blockAt 越界返 Air 安全）。
                    int dropX = qFloor(e.pos.x());
                    int dropY = qFloor(e.pos.y());
                    const int dropZ = qFloor(e.pos.z());
                    bool settled = false;
                    for (int yy = dropY; yy >= 0; --yy) {
                        const quint8 b = world->blockAt(dropX, yy, dropZ);
                        if (!world->isSolid(dropX, yy, dropZ)
                            && b != BlockRegistry::Lava && b != BlockRegistry::Fire) {
                            dropY = yy;
                            settled = true;
                            break;
                        }
                    }
                    if (!settled) dropY = qFloor(e.pos.y()); // 全柱无空位 → 原位置（同旧行为兜底）
                    emit enderEyeBecameItem(dropX, dropY, dropZ);
                    toRemove.push_back(idx);
                    dirty = true;
                } else {
                    // 20% → 碎裂：进入碎裂态（QML delegate 播缩小淡出 + 玻璃碎裂粒子，无掉落物）。
                    e.enderEyeShatter = kEnderEyeShatterTime;
                    dirty = true;
                }
            }
            continue; // EnderEye 不走 Mob AI / resting / 击退衰减
        }

        // --- EnderPearl（t758 暗渊珠投掷物；t835 五项修）：轻重力抛物 + 任意接触命中 + 液体缓沉 + 出界/虚空不传 ---
        //   机制等价 MC 1.0 ender pearl：右键掷出受重力抛物飞行（t835④ 珠专属轻重力 kEnderPearlGravity=12，
        //   MC 投掷物 0.03/tick²=12 vs 世界 28），接触即结算 —— emit enderPearlLanded(落点格) → 呈现层路由
        //   PlayerController.applyEnderPearlTeleport（安全落点扫描 + 瞬移玩家 + 传送伤害，机制语义收口在
        //   Game 层）。**不做 mob 命中**（取舍：珍珠只传送掷出者自己，撞 mob 穿过 —— MC 对 mob 命中亦仅传送
        //   掷者，伤害分支 v1 不做，同头文件注释）。t835 三项 tick 侧改动：
        //   ① 有形接触必传送：命中判据从 collisionAABBsAt 点在盒内（t762 引入）放宽为**本格任意方块实存**
        //   （5 id 豁免 + review25 #5 补无碰撞植物族豁免，见命中处注释——本工程 Rail/Torch 是 ShapeNone
        //   无碰撞盒但属 t835 有意命中的具形方块，格级接触判据保持）。
        //   旧点在盒判据漏两族 —— (a) 铁轨/火把等 ShapeNone 无碰撞盒方块：珠点穿过该格不命中，落进下方
        //   支撑格 → 落点格=支撑格内部 → Game 层立位扫描把非整格方块当实心全列 abort（「落铁轨不传送」的
        //   根因，t803 isSolid 实体侧消费者逐一豁免的同族反向）；(b) 压力板/台阶等薄碰撞盒：珠点一 tick
        //   跨过薄盒带（薄板厚 0.06 << 速度 0.4 格/tick）点不在盒内 → 同 (a) 穿透落到下方格。格级判据按
        //   「格内有没有东西」判接触 —— 机制等价 MC 1.0 珍珠对任何具形方块（含轨道、薄板）都算落地、
        //   对无碰撞格（空气 / 液体② / 门面 / 火 / 植物族）穿过（植物穿过与箭的「空碰撞盒即穿」同义；
        //   箭是点在盒内、珍珠按格接触，差异是 t835 防薄盒穿透漏传送的有意取舍）。
        //   ② 液体缓沉：珠所在格为 Water/Lava → 不立即传送，重力把 vy 压到缓沉终速（−kEnderPearlWaterSink/
        //   LavaSink）+ 水平强阻尼（kEnderPearlLiquidDrag）→ 缓慢沉到液体底（底面方块接触 = ① 判据）才结算
        //   传送。寿命倒计暂停（深水柱缓沉可超 8s，防到期把掷出者半水传送）。机制等价 MC 投掷物入液强阻尼
        //   缓沉；岩浆更粘 → 终速更慢。岩浆接触同样必传送（①）—— 传送本身**不附带点燃**（MC 1.0 珍珠传送
        //   无着火；「传送 5 格内着火」是 1.x 后期机制，不做）。传送后玩家若立于岩浆格，走既有岩浆接触伤害
        //   链（现状保持）。
        //   ③ 虚空/出界不传送：一路无接触落出世界底（y<0）/ 飞出 XZ 边界 → 静默移除**不传送**（防把玩家传
        //   到界外/虚空不可玩位置；珍珠白耗。机制取舍：MC 珍珠入虚空同样有去无回）。寿命兜底（悬空到期）
        //   仍传送（B11 落点列向下找支撑）。
        if (e.kind == EnderPearl) {
            // ② 液体缓沉态判定按**当前格**（本 tick 起点所在格）：进入液体格的下一 tick 起接管物理。
            const quint8 curId = world->blockAt(qFloor(e.pos.x()), qFloor(e.pos.y()), qFloor(e.pos.z()));
            const bool inLiquid = (curId == BlockRegistry::Water || curId == BlockRegistry::Lava);
            if (inLiquid) {
                // 缓沉：重力压 vy 到 −sink 终速 + 水平指数阻尼（~0.2s 基本停 → 竖直缓沉）；寿命暂停（见②）。
                const float sink = (curId == BlockRegistry::Lava) ? kEnderPearlLavaSink : kEnderPearlWaterSink;
                e.vy = qMax(e.vy - kEnderPearlGravity * float(dt), -sink);
                const float dragMul = qMax(0.0f, 1.0f - kEnderPearlLiquidDrag * float(dt));
                e.vx *= dragMul;
                e.vz *= dragMul;
            } else {
                e.arrowLife -= float(dt); // 寿命倒计（复用 arrowLife；仅空中递减，液体缓沉期暂停）
                e.vy -= kEnderPearlGravity * float(dt); // ④ 轻重力抛物（12 vs 世界 28，MC 投掷物同源）
            }
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt);
            bool remove = false;
            bool landed = false; // 命中结算（接触 / 寿命兜底）→ emit enderPearlLanded（传送掷出者）
            // 寿命兜底命中：悬空到期视作落点结算（落点列向下找支撑传送，防极端上抛珍珠永久滞留堆积）。
            if (e.arrowLife <= 0.0f) { remove = true; landed = true; }
            // ① 有形接触命中 → 即结算（撞地 / 撞墙 / 踩铁轨/薄板：落点 = 接触格，Game 层扫描从该格起向下
            //   找碰撞支撑 → 立位）。review25 #5 取**修法①**（豁免表补无碰撞植物族）：review 建议的修法②
            //   「本格有碰撞盒才命中」经核不成立——本工程 Rail / Torch 都是 ShapeNone 无碰撞盒（t835 探针
            //   钉死「落轨格 / 落火把格必传送」，格级接触是 t835 对「点在薄盒内一 tick 跨过即漏」的有意
            //   判据），改盒存在性判据会砸 t835 基线。取①：t835「本格任意方块实存」判据保留，豁免表在
            //   原 5 id（Air / Water·Lava ② / NetherPortal / Fire）基础上补**无碰撞植物族**——着生植物
            //   （isGroundPlant：草丛 / 花×4 / 蘑菇×2）+ 枯灌木 + 树苗 + 作物（isCropBlock：小麦 / 胡萝卜 /
            //   马铃薯）+ 浆果丛 + 甘蔗，全族 ShapeNone 无碰撞、MC 1.0 投掷物 raytrace 穿过、本工程箭亦按
            //   空碰撞盒穿过 → 珍珠平抛弧线下降段不再被草丛截断在数格内（review25 #5 反噬 t835「更远
            //   投掷」目标的病根）。铁轨 / 火把 / 压力板 / 台阶等仍按格命中（t835 语义不动）。
            if (!remove) {
                const int bx = qFloor(next.x()), by = qFloor(next.y()), bz = qFloor(next.z());
                const quint8 hitId = world->blockAt(bx, by, bz);
                const bool plantPass = BlockRegistry::isGroundPlant(hitId)
                                       || hitId == BlockRegistry::DeadBush
                                       || hitId == BlockRegistry::Sapling
                                       || isCropBlock(hitId)
                                       || hitId == BlockRegistry::SweetBerryBush
                                       || hitId == BlockRegistry::Sugarcane;
                if (hitId != BlockRegistry::Air && hitId != BlockRegistry::Water
                    && hitId != BlockRegistry::Lava && hitId != BlockRegistry::NetherPortal
                    && hitId != BlockRegistry::Fire && !plantPass) {
                    remove = true;
                    landed = true;
                }
            }
            // 越界兜底（t835③：飞出世界 XZ 边界 / 跌出底部 = 虚空直落）→ 静默移除**不传送**（防把玩家传到
            //   界外 / 虚空不可玩位置；珍珠白耗。机制取舍：MC 珍珠入虚空同样有去无回）。
            if (!remove) {
                if (next.x() < 0.0f || next.z() < 0.0f
                    || next.x() > worldW || next.z() > worldD || next.y() < 0.0f) {
                    remove = true;
                }
            }
            // 命中结算 → emit 落点格（floor(next)，整数格约定同 enderEyeBecameItem；呈现层路由传送）。
            //   审查修 L2：寿命到期分支（上方）先于越界检查置 landed=true → 到期与出界同 tick 时越界检查被
            //   !remove 短路，仍会 emit 越界坐标把掷出者传到界外 / 虚空。emit 前补界内校验（与越界兜底同
            //   口径）：落点出界 → 只移除不传送（珍珠白耗，语义与「越界不传送」注释一致）。
            if (remove && landed
                && next.x() >= 0.0f && next.z() >= 0.0f
                && next.x() <= worldW && next.z() <= worldD && next.y() >= 0.0f) {
                emit enderPearlLanded(qFloor(next.x()), qFloor(next.y()), qFloor(next.z()));
            }
            if (remove) {
                toRemove.push_back(idx);
                dirty = true;
            } else {
                e.pos = next; // 继续飞行
                dirty = true;
            }
            continue; // EnderPearl 不走 Mob AI / resting / 击退衰减
        }

        // --- Bobber（t836 钓鱼浮标投射物）：轻重力抛物 + 飞行段钩 mob + 落水浮定待咬 / 落陆静止 + 出界消散 ---
        //   机制等价 MC 1.0 fishing bobber：右键甩竿抛出（Game 层沿视线初速），四态机（kBobberSt*）：
        //     Flying：轻重力 kBobberGravity 抛物（投掷物家族 12 vs 世界 28，同暗渊珠）→ ① 飞行段与 mob AABB
        //       （外扩 kBobberHookHitPad）相交 → Hooked 钉在 mob 身上（**不伤害**；玩家不可钩自己——玩家不是
        //       EntityManager 实体天然排除；已钩 mob 被新浮标命中 = 换绑，旧浮标脱钩转 Flying 下落——spec「已钩
        //       新浮标重钩=换绑」；**t883 夜行者例外**：命中夜行者 → 强制瞬移闪避（同箭链 t829①）且永不钩定——
        //       钩不住夜行者族，浮标穿过继续飞）；② next 格是 Water → 浮定水面（浮力平衡半浸：XZ 收格心、Y = 液面 −
        //       kBobberFloatDip；液面按水 state 折算，源 7/8 / 流 (8−s)/8——waterSurfaceFrac 单一权威（t892 降位
        //   后口径，见下方 t892 行；review27 #24 勘误：旧注「源=1.0」是降位前数据））+ 掷确定性
        //       等待（hashVoxel(seed ^ 盐 ^ 甩竿序号)，PLAN §2-K 禁随机源，t791 骨粉同模式）→ Water 态；
        //       ③ 实体方块接触（豁免族同 t835 珍珠：Air/水/岩浆/门面/火）→ 贴命中面静止（Ground，不推进 next
        //       ——浮标停在接触面前一位置，MC 浮标砸哪停哪的近似）；review25 #13 本接触门**前置到 ① 钩 mob
        //       之前**（实体格命中即 Ground 不钩——防隔墙钩，见 Flying 段注释）；④ 岩浆格 → 同 Ground 静止（岩浆面浮住；
        //       MC 口径岩浆里无鱼可钓，不进入等待机）；⑤ 出界（XZ 越界 / y<0 虚空）→ 消散移除（Game 层
        //       updateFishing 镜像检测 alive 失效 → 自动收竿态）。
        //     Water：bobberBiteTimer 两阶段（等待 →0 咬钩 → 窗口 →0 逃走重掷，见 Entity 字段注释）；咬钩沿 /
        //       逃走沿各 emit bobberBit / bobberEscaped（呈现层水花粒子）；重掷 = 序号 ++（新一轮确定性值）。
        //       水格被排干 / 填方（blockAt != Water）→ 转 Flying 零速下落（自然落到下方支撑转 Ground）。
        //     Ground：静止（不再钩 mob——**只在飞行段钩**，spec 明示取舍：MC 语义近似，落地静止浮标是死线
        //       不是渔具；收竿 = 空收无消耗回收）。review25 #12：贴靠格节流复查（kBobberGroundRecheckEvery
        //       tick 一查）——支撑被挖 / 被爆 → 转 Flying 零速下落（与 Water 排水路径对称，不再悬空滞留）。
        //     Hooked：pos 钉 mob 身上每帧跟随（mob 中心 + 0.55×半高的体侧）；mob 死 / 移除 / 槽复用换任
        //       （spawnSerial 比对，snowballThrowerSerial 双查先例）→ 脱钩转 Flying 零速下落。
        //   寿命墙钟 kBobberLifetimeMs 真值源 + arrowLife（dt 递减）次级镜像 → 到 0 消散（挂机兜底，review25 #14
        //   同箭 60s despawn 先例；等待 / 咬钩窗口计时保持 dt——确定性掷骰的 tick 语义依赖，见分支头注释）。
        if (e.kind == Bobber) {
            // review25 #14 寿命墙钟真值源（同箭 kArrowDespawnMs 先例 / ItemEntityManager 拾取延迟 m_clock 模式）：
            //   dt 在调用方钳 50ms，真实帧耗时超限时 dt 累计只慢不快 → 旧版 arrowLife 递减在卡顿下漂移（挂机
            //   浮标滞留 >180s）；墙钟不依赖 dt 必然到期。下方 arrowLife dt 递减保留为次级镜像（先到先收，
            //   正常路径二者同窗到期）。**等待 / 咬钩窗口计时刻意保持 dt**：bobberWaitSeconds 掷骰的 tick 语义
            //   是确定性探针的依赖（t836 b ±1 tick 行为级直证），且窗口变慢方向对玩家有利——取舍见 review25 #14。
            if (m_clock.elapsed() - e.arrowSpawnMs >= kBobberLifetimeMs) {
                toRemove.push_back(idx);
                dirty = true;
                continue;
            }
            e.arrowLife -= float(dt);
            if (e.arrowLife <= 0.0f) { toRemove.push_back(idx); dirty = true; continue; }

            if (e.bobberState == kBobberStHooked) {
                // 钉 mob 跟随；目标失效（死 / 移除 / 槽复用换任）→ 脱钩转 Flying 零速下落。
                const bool validTarget = e.bobberHookedIdx >= 0 && e.bobberHookedIdx < int(m_entities.size());
                if (validTarget) {
                    const Entity &m = m_entities[size_t(e.bobberHookedIdx)];
                    if (m.alive && m.kind == Mob && !m.dead && m.spawnSerial == e.bobberHookedSerial) {
                        e.pos = QVector3D(m.pos.x(), m.pos.y() + m.halfH * 0.55f, m.pos.z());
                        dirty = true;
                        continue; // 保持 Hooked（不走下方 Flying 物理）
                    }
                }
                e.bobberState = kBobberStFlying;
                e.bobberHookedIdx = -1;
                e.bobberHookedSerial = 0;
                e.vx = e.vy = e.vz = 0.0f;
                dirty = true;
                continue; // 脱钩本帧冻结，下帧起 Flying 下落
            }

            if (e.bobberState == kBobberStWater) {
                const int wbx = qFloor(e.pos.x()), wby = qFloor(e.pos.y()), wbz = qFloor(e.pos.z());
                if (world->blockAt(wbx, wby, wbz) != BlockRegistry::Water) {
                    // 水没了（被舀 / 填方 / 流走）→ 浮标失浮转 Flying 零速下落。
                    e.bobberState = kBobberStFlying;
                    e.bobberHasBite = false;
                    e.bobberBiteTimer = 0.0f;
                    e.vx = e.vy = e.vz = 0.0f;
                    dirty = true;
                    continue;
                }
                e.bobberBiteTimer -= float(dt);
                if (e.bobberBiteTimer > 0.0f) continue; // 等待 / 窗口计时中
                if (!e.bobberHasBite) {
                    // 等待到点 → 咬钩（进入窗口；呈现层水花 + 浮标下沉视觉由 hasBite 镜像驱动）。
                    //   t960 缠咬：窗长 = spawn 时写定的 e.bobberBiteWindow（基线 kBobberBiteWindowSec +
                    //   0.5s×级附加；基值常量用户口径钉死不动）；0 防御兜底基线（spawn 后恒 >0，理论不可达）。
                    e.bobberHasBite = true;
                    e.bobberBiteTimer = e.bobberBiteWindow > 0.0f ? e.bobberBiteWindow : kBobberBiteWindowSec;
                    emit bobberBit(e.pos.x(), e.pos.y(), e.pos.z());
                    dirty = true;
                } else {
                    // 窗口过期 → 鱼跑了：小水花提示 + 重掷新确定性等待（序号 ++ → 新值）。
                    //   t960 唤潮：重掷等待同乘 e.bobberWaitScale（spawn 写定，逐竿恒定——每轮等待都缩短）。
                    e.bobberHasBite = false;
                    ++e.bobberSerial;
                    e.bobberBiteTimer = bobberWaitSeconds(world->hashVoxel(
                        int(quint32(world->seed()) ^ kBobberWaitHashSalt ^ e.bobberSerial), wbx, wby, wbz))
                        * e.bobberWaitScale;
                    emit bobberEscaped(e.pos.x(), e.pos.y(), e.pos.z());
                    dirty = true;
                }
                continue; // Water 不走 Flying 物理
            }

            if (e.bobberState == kBobberStGround) {
                // review25 #12 贴靠格节流复查（每 kBobberGroundRecheckEvery tick 一查，与 Water 态每 tick
                //   复查 blockAt!=Water 对称的降频版——Ground 是静置态，多浮标场景省格查询）：支撑被挖 / 被
                //   爆 / 被浇成水 → 转 Flying 零速下落（自然落到下方支撑转 Ground / 入水转 Water；旧版恒
                //   continue → 挖掉贴靠方块后浮标悬空滞留至 180s 寿命兜底）。贴靠格 = 进态时快照（bobberGround*
                //   命中实体格 / 岩浆格）；支撑仍在（含岩浆面——岩浆格是有效贴靠）→ 继续静止。Fire 同 Air 处理
                //   （火格对本投射物是穿透豁免族——贴靠格烧穿成火 = 无面可贴，下落）。
                if (m_tickPhase % kBobberGroundRecheckEvery == 0) {
                    const quint8 sup = world->blockAt(e.bobberGroundCellX, e.bobberGroundCellY,
                                                      e.bobberGroundCellZ);
                    if (sup == BlockRegistry::Air || sup == BlockRegistry::Water
                        || sup == BlockRegistry::Fire) {
                        e.bobberState = kBobberStFlying;
                        e.vx = e.vy = e.vz = 0.0f;
                        dirty = true;
                        continue; // 本帧冻结，下帧起 Flying 下落（同脱钩 / 排水路径口径）
                    }
                }
                continue; // 陆上静止（等收竿回收；寿命在分支头递减）
            }

            // --- Flying：抛物 + 钩定 / 落水 / 接触 / 出界 ---
            e.vy -= kBobberGravity * float(dt);
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt);
            const int bx = qFloor(next.x()), by = qFloor(next.y()), bz = qFloor(next.z());
            const quint8 nid = world->blockAt(bx, by, bz);

            // ⑤ 实体接触**前置到钩 mob 之前**（review25 #13 从原「钩 mob → 出界 → 落水 → 岩浆 → 接触」序尾
            //   提前）：next 已进入非豁免实体格（Air / 水 / 岩浆 / 门面 / 火穿过——豁免族与下方 ③④ 分流同源，
            //   条件 ≡ 原 ⑤ 在 ③④ continue 之后的有效口径，纯重排无判据变化）→ 贴面静止**且不钩 mob**。旧序
            //   先用 next 点测 mob AABB 再查方块碰撞 → 浮标 next 跨入墙格且已进墙后 mob 外扩命中盒（步长 >0.85
            //   格 / 贴壁 mob 命中盒伸入墙格）时被**隔墙钩住**，Hooked 钉位 + 收竿拉拽可把 mob 拉过墙；实体格
            //   命中门先行 = 钩定只发生在可穿透格（近似视线门：实体方块挡在中间就不钩）。水 / 岩浆 / 出界仍在
            //   其后各自分支（水中生物照旧飞行段可钩——t836 语义不变，仅实体格命中让位）。贴靠格快照进
            //   bobberGround*（Ground 态节流复查用，review25 #12）。
            if (nid != BlockRegistry::Air && nid != BlockRegistry::Water && nid != BlockRegistry::Lava
                && nid != BlockRegistry::NetherPortal && nid != BlockRegistry::Fire) {
                e.bobberState = kBobberStGround;
                e.bobberGroundCellX = qint16(bx);
                e.bobberGroundCellY = qint16(by);
                e.bobberGroundCellZ = qint16(bz);
                e.vx = e.vy = e.vz = 0.0f;
                dirty = true;
                continue; // 贴命中面前一位置静止（不推进 next——浮标停在接触面前，MC 浮标砸哪停哪的近似）
            }

            // ① 飞行段钩 mob（点 vs AABB 外扩；取首个命中）。**只在 Flying 段**（Ground 静止浮标不钩，见头注释）。
            {
                int hit = -1;
                for (size_t j = 0; j < m_entities.size(); ++j) {
                    const Entity &m = m_entities[j];
                    if (!m.alive || m.kind != Mob || m.dead) continue;
                    const float pad = m.halfW + kBobberHookHitPad;
                    if (std::abs(next.x() - m.pos.x()) > pad || std::abs(next.z() - m.pos.z()) > pad) continue;
                    if (next.y() < m.pos.y() - m.halfH - kBobberHookHitPad
                        || next.y() > m.pos.y() + m.halfH + kBobberHookHitPad) continue;
                    // t883 夜行者对鱼钩=投射物：**强制瞬移闪避**（同箭链 t829①——绕 teleportCooldown，防
                    //   「命中零反馈穿身」）且**绝不钩定**（钩不住夜行者族；机制等价 MC 1.0 末影人对投射物
                    //   远程免疫的鱼钩口径——spec「鱼钩对夜行者算投射物→瞬移闪避（同箭链）」）。闪避成功 →
                    //   浮标穿过原站位继续飞（软线不消耗不落定，区别于箭的 remove=true——钩是线不是命中体）；
                    //   瞬移全试失败（被围/地形不允许）→ 免疫依旧（穿过），下帧原位重试。
                    if (m.mobType == MobNightwalker) {
                        Entity &nm = m_entities[j];
                        const QVector3D oldPos = nm.pos; // 快照旧位（同箭分支 B-L1：teleportEntity 改同一对象）
                        const bool dodged = teleportEntity(int(j), nm, world,
                                                           kNightwalkerTeleportMin, kNightwalkerTeleportMax,
                                                           /*clearAggro=*/false); // review26 #8：0 伤害 0 消耗的浮标不得成免费远程「净化」——闪避只位移，enraged/rageTimer/windupTimer 保持（MC 1.0 末影人被投射物闪避不解除仇恨；箭链 t829① 同口径）
                        qCInfo(lcEnt) << "bobber deflected by nightwalker" << int(j)
                                      << (dodged ? "(teleport dodge)" : "(teleport failed; immune pass-through)")
                                      << "from" << oldPos;
                        break; // 本帧不钩（hit 保持 -1）——浮标继续飞行（下方出界/落水/岩浆/推进分支照走）
                    }
                    hit = int(j);
                    break;
                }
                if (hit >= 0) {
                    // 换绑：同 mob 已被其它浮标钩住 → 旧浮标脱钩转 Flying 零速下落（spec「已钩新浮标重钩=换绑」）。
                    for (size_t j = 0; j < m_entities.size(); ++j) {
                        Entity &o = m_entities[j];
                        if (int(j) == idx || !o.alive || o.kind != Bobber) continue;
                        if (o.bobberState == kBobberStHooked && o.bobberHookedIdx == hit) {
                            o.bobberState = kBobberStFlying;
                            o.bobberHookedIdx = -1;
                            o.bobberHookedSerial = 0;
                            o.vx = o.vy = o.vz = 0.0f;
                        }
                    }
                    const Entity &m = m_entities[size_t(hit)];
                    e.bobberState = kBobberStHooked;
                    e.bobberHookedIdx = hit;
                    e.bobberHookedSerial = m.spawnSerial; // 代际快照（槽复用换任检测）
                    e.pos = QVector3D(m.pos.x(), m.pos.y() + m.halfH * 0.55f, m.pos.z());
                    dirty = true;
                    continue;
                }
            }

            // ② 出界（XZ 越界 / 虚空直落）→ 消散（Game 层镜像检测自动收竿；浮标白耗，MC 口径甩飞了就没了）。
            if (next.x() < 0.0f || next.z() < 0.0f || next.x() > worldW || next.z() > worldD || next.y() < 0.0f) {
                toRemove.push_back(idx);
                dirty = true;
                continue;
            }

            // ③ 落水 → 浮定水面（浮力平衡半浸）+ 掷确定性等待（首掷用甩竿序号）。
            if (nid == BlockRegistry::Water) {
                const quint8 wst = world->stateAt(bx, by, bz);
                // t892 液面走 BlockRegistry::waterSurfaceFrac 单一权威（源 7/8 / 流 (8−s)/8）——与 mesher
                //   renderTop 同口径（浮标浮定高度随可视水面同步降，不再各写一份 1.0）。
                const float surf = BlockRegistry::waterSurfaceFrac(wst);
                e.pos = QVector3D(float(bx) + 0.5f, float(by) + surf - kBobberFloatDip, float(bz) + 0.5f);
                e.vx = e.vy = e.vz = 0.0f;
                e.bobberState = kBobberStWater;
                e.bobberHasBite = false;
                // t960 唤潮：首掷等待同乘 e.bobberWaitScale（spawn 写定；基线 1.0 → 旧行为逐字不变）。
                e.bobberBiteTimer = bobberWaitSeconds(world->hashVoxel(
                    int(quint32(world->seed()) ^ kBobberWaitHashSalt ^ e.bobberSerial), bx, by, bz))
                    * e.bobberWaitScale;
                emit bobberSplashed(e.pos.x(), e.pos.y(), e.pos.z()); // t884 ① 入水水花（浮定沿；呈现层 burstWaterCast）
                dirty = true;
                continue;
            }

            // ④ 岩浆面浮住（MC 口径岩浆无鱼可钓——不进等待机，按静止处理；不沉不烧，浮标是软木不是可燃物）。
            //   贴靠格快照 = 岩浆格（Ground 复查按「岩浆仍是有效贴靠」判，review25 #12）。
            if (nid == BlockRegistry::Lava) {
                e.bobberState = kBobberStGround;
                e.bobberGroundCellX = qint16(bx);
                e.bobberGroundCellY = qint16(by);
                e.bobberGroundCellZ = qint16(bz);
                e.vx = e.vy = e.vz = 0.0f;
                dirty = true;
                continue;
            }

            e.pos = next; // 空中继续飞行（非豁免实体接触已在前置 ⑤ 收口，此处仅剩 Air / 门面 / Fire 穿过格）
            dirty = true;
            continue; // Bobber 不走 Mob AI / resting / 击退衰减
        }

        // --- FallingBlock（t117/t220）：重力 + 着地放置 / 变掉落物 + 移除（无 resting 态；落到底即转为方块或掉落物）---
        //   t490 PrimedTnt（引燃态 TNT）：复用 FallingBlock kind 但 primed=true → 不走着地放置路径，改走 fuse
        //   倒计（dt 递减）→ 到 0 调 detonatePrimedTnt（球形破坏 + 链式引燃 + 伤玩家 + 音视）+ 移除。仍受重力
        //   （机制等价 MC primed TNT 受重力下落）；着地（下方非 air/水）停在下落支撑面顶但 **不放置方块**（保持
        //   「引燃态非完整方块可穿透」语义，spec），fuse 继续倒计直到引爆。listener = 玩家脚位（tick 签名传，由
        //   PlayerController::m_pos 喂入），detonatePrimedTnt 用它算距离衰减伤害 + 击退方向（同 detonateStalker）。
        if (e.kind == FallingBlock && e.primed) {
            // (1) fuse 倒计：每帧 -= dt（机制等价 MC primed TNT 80 tick fuse ~4s；本工程 ~5s）。到 0 → 引爆。
            e.fuse -= float(dt);
            if (e.fuse <= 0.0f) {
                // t997 爆炸波分期：单 tick 引爆预算钳制——同帧到期簇 ≤ kMaxTntDetonationsPerTick 照旧
                //   全爆；超额者引信重挂 kDetonationWaveRegroupSec 下 tick 再爆（波次推进）。总破坏量
                //   不变（分期不许吞爆炸）；链式引燃语义不动（链式 TNT 本就带 1.2s 错峰引信 kChainFuseSec
                //   几乎不同帧扎堆，预算只钳「同引信同帧到期簇」——同帧 N 爆 = N× 联合盒 reflood + N×
                //   worldChanged QML 扇出同帧叠加，用户 15FPS 帧的渲染/同步侧放大源）。
                if (tntDetonationBudget > 0) {
                    --tntDetonationBudget;
                    detonatePrimedTnt(idx, world, listener); // 球形破坏 + 链式引燃 + 衰减伤玩家 + explosion 音/视
                    // detonatePrimedTnt 内已 releaseSlot(idx) + bump revision/emit；这里直接 continue 跳过下方重力段
                    //   （实体已除不再模拟）。不 push toRemove（detonatePrimedTnt 已释放槽）。
                    continue;
                }
                // 预算耗尽 → 引信重挂，下 tick 归队再爆（落下方重力段照常模拟；分支末统一 dirty=true
                //   bump revision —— fuse 变了，QML 白闪频率随 fuseProgressAt 同步）。
                e.fuse = kDetonationWaveRegroupSec;
            }
            // (2) 重力下落（复用沙子物理；机制等价 MC primed TNT 受重力）。着地判定同沙子：下落路径扫首个实体
            //   方块 → 贴其顶面停下（vy=0），但 **不放置方块 / 不变掉落物**（primed 实体保持引燃态继续倒计）。
            const int cx = qFloor(e.pos.x());
            const int cz = qFloor(e.pos.z());
            if (cx < 0 || cz < 0) {
                // 列坐标非法（飞出世界 XZ 边界）→ 仍继续倒计（下一帧位置可能合法）；不动 pos（防写入非法列）。
                dirty = true; // fuse 变了 → bump revision（QML 据 fuseProgressAt 加速白闪）
                continue;
            }
            e.vy -= kGravity * float(dt);
            if (e.vy < -kMaxFall) e.vy = -kMaxFall;
            // t494 水平积分（爆炸推动 primed TNT）：vx/vz 由 detonateTntSphere 施加速度冲量 → 此处积分 X/Z 位移 +
            //   摩擦衰减（exp(-rate·dt)）。无冲量（vx=vz=0）→ 无位移无衰减，行为不变。飞起后摩擦停下（机制等价 MC
            //   爆炸推开 TNT 的抛物线）。不查碰撞（primed halfW=0 可穿透，机制等价 MC primed TNT 被推穿过实体）。
            if (e.vx != 0.0f || e.vz != 0.0f) {
                e.pos.setX(e.pos.x() + e.vx * float(dt));
                e.pos.setZ(e.pos.z() + e.vz * float(dt));
                const float f = std::exp(-kExplosionEntityFriction * float(dt));
                e.vx *= f; e.vz *= f;
                dirty = true; // 位置变 → bump revision（QML 摆位重算）
            }
            const float newY = e.pos.y() + e.vy * float(dt);
            // t493 修「primed TNT 上下震荡」：旧 topCell=qFloor(pos.y) 扫 TNT 自己所在格（已被 clearBlockSilent 清成
            //   Air，pos.y=10.5 → floor=10 = TNT 已移除的格）→ 永远找不到支撑 → 每帧重力拉下又弹回 restY → 上下抖动。
            //   改从**模型底面正下方**一格开始扫：TNT 视觉占 pos.y±0.5，底面 pos.y-0.5，正下方格 = floor(pos.y-0.5)。
            //   pos.y=10.5 → floor(10.0)=10 仍是自己格 —— 需再下移 1 格 = floor(pos.y)-1 = 9 = 支撑格。下落时同理：
            //   TNT 中心在某 cell 内，支撑是它下方第一个实体格。故扫描范围 [floor(pos.y)-1 .. floor(newY)-1]。
            const int topCell = qFloor(e.pos.y()) - 1; // 模型底面正下方的格（支撑判定起点；TNT 占 pos.y±0.5）
            int botCell = qFloor(newY) - 1;            // 下落目标底面下方的格
            if (botCell > topCell) botCell = topCell; // 防浮点噪声致 botCell>topCell
            int supportCellY = -1; // 首个支撑方块（primed TNT 停在其顶面，不放置方块）
            quint8 supportId = 0;  // 支撑方块 id（据它算顶面高度 topOffset；全立方 1.0 / 下半砖 0.5 等）
            for (int cy = topCell; cy >= botCell; --cy) {
                if (cy < 0) break; // 越界下方=空气 → 不视作地面
                const quint8 b = world->blockAt(cx, cy, cz);
                if (b == BlockRegistry::Air || b == BlockRegistry::Water) continue; // 穿透（同沙子）
                // 任一实体方块都作支撑（完整立方 / 半砖 / 压力板 / 火把…），据其碰撞 AABB 顶面算落点
                //   （机制等价 MC primed TNT 落半砖上坐半砖顶、落火把上停火把顶）。
                supportCellY = cy; supportId = b; break;
            }
            if (supportCellY >= 0) {
                // 着地：停在支撑方块**顶面**上方半格（TNT 视觉高 1.0 → 中心 = 支撑顶面 + 0.5）。
                //   t494b 修「落半砖仍悬空」：旧实现从 topOffset=1.0 起「只更新更大的」→ 下半砖 maxY=0.5 < 1.0
                //   永不命中 → topOffset 恒 1.0 → 落半砖中心停在 cy+1.5 仍悬空。改从 **0 起取最大 maxY**（全立方
                //   1.0 / 下半砖 0.5 / 压力板 ~0.0625 / 上半砖 1.0 / stairs 多盒取最高）→ restY = supportCellY +
                //   topOffset + 0.5（TNT 底面贴支撑顶面）。halfH=0 可穿透 + 不放置方块（引燃态）。
                float topOffset = 0.0f;
                // t859：out-param 栈上小缓冲（零堆分配；旧按值版每查两次 vector 分配）。
                BlockRegistry::BlockAABB supBoxes[BlockRegistry::kMaxAABBsPerCell];
                const int nSup = BlockRegistry::collisionAABBsInto(
                        supportId, world->stateAt(cx, supportCellY, cz),
                        supBoxes, BlockRegistry::kMaxAABBsPerCell);
                for (int i = 0; i < nSup; ++i)
                    if (supBoxes[i].maxY > topOffset) topOffset = supBoxes[i].maxY; // 取最大顶面（stairs 多盒取最高）
                if (nSup == 0) topOffset = 1.0f; // 兜底（不应发生）
                const float restY = float(supportCellY) + topOffset + 0.5f; // 支撑顶面 + TNT 半高
                if (e.pos.y() != restY) { e.pos.setY(restY); e.vy = 0.0f; dirty = true; }
            } else if (newY <= 0.0f) {
                // 全列无支撑且已跌出世界底部 → 静默移除（防永久下落；正常世界 y=0 有石头层不触发）。
                toRemove.push_back(idx);
                dirty = true;
            } else if (newY != e.pos.y()) {
                e.pos.setY(newY); // 继续自由下落（穿过 air / 水）
                dirty = true;
            }
            dirty = true; // fuse 每帧变 → bump revision（QML 白闪脉冲频率随 fuseProgressAt 加速）
            continue; // 不走 Mob 的 AI / resting / 重力逻辑，也不走下方普通 FallingBlock 着地放置路径
        }

        if (e.kind == FallingBlock) {
            const int cx = qFloor(e.pos.x());
            const int cz = qFloor(e.pos.z());
            if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过
            e.vy -= kGravity * float(dt);
            if (e.vy < -kMaxFall) e.vy = -kMaxFall;
            const float newY = e.pos.y() + e.vy * float(dt);
            const int topCell = qFloor(e.pos.y());
            int botCell = qFloor(newY);
            if (botCell > topCell) botCell = topCell; // 防浮点噪声致 botCell>topCell
            int supportCellY = -1; // 首个完整立方支撑（着地放置在 cy+1）
            int dropCellY = -1;    // 首个不完整方块（沙变掉落物；掉落点 = 该格上方 cy+1）
            for (int cy = topCell; cy >= botCell; --cy) {
                if (cy < 0) break; // 越界下方=空气 → 不视作地面
                const quint8 b = world->blockAt(cx, cy, cz);
                if (b == BlockRegistry::Air || b == BlockRegistry::Water) continue; // 穿透（t220 水不挡沙）
                if (BlockRegistry::isFullCube(b)) { supportCellY = cy; break; } // 完整立方 → 着地支撑
                dropCellY = cy; break; // 不完整方块（火把 / 半砖 / ...）→ 沙失撑变掉落物
            }
            // t794 下落铁砧砸伤（机制等价 MC 1.0 anvil crush；**先伤后落** —— 本检查在着地还原方块之前跑，
            //   生物不挡下落体，铁砧穿过它落到下方方块格还原）：携带 Anvil 族 id 时，用「本 tick 扫掠盒」做
            //   重叠测试 —— XZ = 铁砧砸伤足印 [pos±kAnvilCrushHalfW]（review #29：12/16 视觉宽的一半 0.375，
            //   非 FallingBlock halfW 0.5 满格——贴格边站的无视觉接触不再误伤），Y = [min(pos.y,newY)−halfH,
            //   pos.y+halfH]（本帧下落扫过的整段，防高速下落一帧跨过薄身 mob 漏检）。命中目标族：全体活体
            //   mob（damageEntity 扣血 + 红闪 + 归零 mobDied 链）+ 玩家（listener 脚位 AABB [±listenerHalfW]×
            //   [0,listenerHeight]；仅 playerTargetable —— 创造/观察者无敌跳过，机制等价 MC；发 mobAttackedPlayer
            //   携 MobAnvil 哨兵 → 呈现层映射死因 DeathCause::Anvil + 护甲减伤链，同 MobTnt 爆炸先例；击退 =
            //   玩家−铁砧 水平归一推离落点）。伤害 = 落差函数（kAnvil* 常量注释见 .h：dmg=(floor(落差)−1)×2，
            //   落差 = fallStartY − 扫掠底中心，2 格起伤 / 每多 1 格 +1♥ / 上限 40HP）；dmg≤0（落差不足）不结算，
            //   继续下落累积。review #28：**按目标记录已结算** —— 命中即把本落体 spawnSerial 写进目标（mob 侧
            //   Entity.anvilCrushSerial / 玩家侧 m_playerAnvilCrushSerial），同 serial 再压到同目标跳过 → 每
            //   实体每次下落只伤一次（真语义；旧 t794 落体侧一次性拍只结算首个命中拍——先穿玩家后落猪身则
            //   猪免伤、台阶两猪只伤上面那只）。铁砧继续下落，后续帧压到**未结算**目标照常结算。
            // review-r1913-final B-M1：封**外层落体引用 e** 的悬垂窗口。下方砸伤段 damageEntity 的
            //   entitiesChanged emit 若经任一 QML handler 同步 spawn → acquireSlot push_back → vector
            //   realloc → e 悬垂；review24 #28 换序只封了 mob 引用 m，e 在 mob 循环逐迭代的 AABB 复检、
            //   玩家段与着地 / 塌落段仍被解引用（:5238 孵化先例同款已知模式）。修：进砸伤段前值快照本落体
            //   的判伤 / 还原字段（emit 链不改本落体数据 → 快照 == 搬家后的活值，逐位相同），其后 mob 循环 /
            //   玩家段 / 着地段全读快照；继续下落分支有写回 → 该分支内重取引用再写。非铁砧落体无 emit
            //   窗口，快照 == 活值，行为零变。
            const QVector3D aPos = e.pos;
            const quint8 aBlockId = quint8(e.blockId);
            const quint8 aBlockState = quint8(e.blockState);
            const quint32 aSerial = e.spawnSerial;
            const float aHalfH = e.halfH;
            if (BlockRegistry::isAnvil(aBlockId)) {
                const float sweepLow = std::min(newY, aPos.y());       // 本帧扫掠底中心
                const float fallDist = e.fallStartY - sweepLow;         // 落差（格）
                const int dmg = std::min(kAnvilCrushDamageCap,
                                         std::max(0, (int(qFloor(fallDist)) - 1) * 2));
                if (dmg > 0 && fallDist >= kAnvilMinFallBlocks) {
                    for (int mi = 0; mi < int(m_entities.size()); ++mi) {
                        Entity &m = m_entities[size_t(mi)];
                        if (!m.alive || m.kind != Mob || m.dead) continue; // 空槽 / 非 mob / 尸体
                        if (m.anvilCrushSerial == aSerial) continue; // 本落体已结算过它（review #28）
                        // AABB 重叠（XZ 分离 / Y 分离逐轴早退；铁砧砸伤半宽 kAnvilCrushHalfW、mob 用各自 halfW/halfH）
                        if (std::abs(m.pos.x() - aPos.x()) >= kAnvilCrushHalfW + m.halfW) continue;
                        if (std::abs(m.pos.z() - aPos.z()) >= kAnvilCrushHalfW + m.halfW) continue;
                        if (m.pos.y() + m.halfH <= sweepLow - aHalfH
                            || m.pos.y() - m.halfH >= aPos.y() + aHalfH) continue;
                        // review24 低危收尾（#28 残口）：**先记账后扣血**——damageEntity 今日只 emit 不增删
                        //   槽，但未来 QML handler 若同步 spawn 会 push_back realloc → 上面取的 `Entity &m`
                        //   悬垂 UB；记账在前则「本落体已结算」事实先落盘（damageEntity 早退 / 命中 handler
                        //   均无悬垂窗）。语义等价：旧版记账本就在 damage 无条件之后（不区分 damage 是否
                        //   实际生效），换序不改变任何可达状态。
                        m.anvilCrushSerial = aSerial; // 按目标记账：本落体对它已结算（review #28）
                        damageEntity(mi, dmg); // 扣血 + 红闪 + 归零 mobDied（复用受击链）
                    }
                    if (playerTargetable && m_playerAnvilCrushSerial != aSerial) {
                        const float px = listener.x(), py = listener.y(), pz = listener.z();
                        if (px + listenerHalfW > aPos.x() - kAnvilCrushHalfW
                            && px - listenerHalfW < aPos.x() + kAnvilCrushHalfW
                            && pz + listenerHalfW > aPos.z() - kAnvilCrushHalfW
                            && pz - listenerHalfW < aPos.z() + kAnvilCrushHalfW
                            && py + listenerHeight > sweepLow - aHalfH
                            && py < aPos.y() + aHalfH) {
                            // 击退方向 = 玩家 − 铁砧 水平归一（推离落点；退化 → (1,0) 兜底同箭模式）
                            float kbX = px - aPos.x(), kbZ = pz - aPos.z();
                            const float klen = std::sqrt(kbX * kbX + kbZ * kbZ);
                            if (klen > 1e-3f) { kbX /= klen; kbZ /= klen; }
                            else { kbX = 1.0f; kbZ = 0.0f; }
                            emit mobAttackedPlayer(dmg, int(MobAnvil), kbX, kbZ);
                            m_playerAnvilCrushSerial = aSerial; // 玩家侧同记账（review #28）
                        }
                    }
                }
            }
            if (supportCellY >= 0) {
                // 着地：在支撑方块上方一格放置 blockId（覆盖空气 / 水；t220 沙落水填堵水格）+ 标记移除。
                //   t527：积雪层（blockId==SnowLayer）着地走 5 参数 setBlockFromEntity 带 state（保留层数 metadata）；
                //   其余 FallingBlock（沙/圆石等）走 4 参数 state=0。
                if (aBlockId == BlockRegistry::SnowLayer)
                    world->setBlockFromEntity(cx, supportCellY + 1, cz, aBlockId, aBlockState);
                else
                    world->setBlockFromEntity(cx, supportCellY + 1, cz, aBlockId);
                // t794 铁砧着地：**恒还原铁砧方块**（不改损坏阶段 / 不变掉落物 —— 机制等价 MC 1.0 铁砧落地
                //   不碎成物品，最多砸坏自己进下一阶段；本工程简化不做阶段推进，落地还原原阶段）+ 发
                //   fallingBlockLanded（呈现层播重铁落地音）。
                if (BlockRegistry::isAnvil(aBlockId))
                    emit fallingBlockLanded(cx, supportCellY + 1, cz, int(aBlockId));
                toRemove.push_back(idx);
                dirty = true;
            } else if (dropCellY >= 0) {
                // 沙遇不完整方块失撑 → 变掉落物（掉落点 = 不完整方块上方一格 = 沙应停位）。发信号由呈现层
                //   转发 ItemEntityManager.spawnItem（同 spawnItem 模式；分层：Entities 层发语义事件，呈现层
                //   只消费）。不放置方块、不动原不完整方块（仅完整立方可支撑沙，机制等价 MC 沙落火把碎成掉落物）。
                // rv-low-batch1 修「塌落雪层落半砖 / 落另一雪层上层数丢失」：旧实现 SnowLayer 与沙同走本分支
                //   emit fallingBlockDropped(...,SnowLayer) → 呈现层 spawnItem(SnowLayer,1) = 只掉 1 份（多层数
                //   metadata 丢失，且掉的是可放置的 SnowLayer 物品而非雪球）。特判（机制对标 MC 1.0 雪层塌落）：
                //   - 目标格是**另一雪层**（同为 SnowLayer）→ 不掉落，改为**叠层合并**：新层数 = min(下方层数 +
                //     携带层数, 8)。下方未满层（合并后 ≤ 8）→ 写回下方格 state（保留层数放置）；溢出部分按层数掉
                //     雪球（溢出层数 × 1 雪球，同铲挖 state+1 语义）。
                //   - 其它不完整方块（半砖 / 火把等）→ 按 state+1 掉雪球（同玩家铲挖：每层 1 雪球；空手沙规则不适用
                //     —— 塌落是系统事件非挖掘，直接给产出）。发 snowLayerCollapseDropped（itemId=SnowballId 字面
                //     0x23D，count=层数）由呈现层 spawnItem（同 fallingBlockDropped 模式）。
                if (aBlockId == BlockRegistry::SnowLayer) {
                    const quint8 below = world->blockAt(cx, dropCellY, cz);
                    if (below == BlockRegistry::SnowLayer) {
                        // 叠层合并：下方 state + 携带 state+1 层（state 0..7 = 1..8 层）；clamp 到 8。
                        const int belowLayers = int(world->stateAt(cx, dropCellY, cz)) + 1;
                        const int carryLayers = int(aBlockState) + 1;
                        const int total = std::min(belowLayers + carryLayers,
                                                   int(BlockRegistry::SnowLayerStageMax) + 1);
                        // 写回合并层数（-1 回 state 编码）。setBlockFromEntity occ 守卫会拒（下方非 air）→ 用
                        //   setWaterSilent 语义写入？不行 —— setWaterSilent 是水流系统接口。此处需「覆盖既有
                        //   SnowLayer 的 state」：走 World 的 5 参数 setBlockFromEntity 不行（occ 拒非空格），
                        //   故由 World 提供 setSnowLayerMerge（见 world.h；仅 Entities 层塌落合并调）。
                        world->setSnowLayerMerge(cx, dropCellY, cz, quint8(total - 1));
                        // 溢出层数（belowLayers + carryLayers > 8）→ 掉雪球（每溢出层 1 个）。
                        const int overflow = (belowLayers + carryLayers) - (int(BlockRegistry::SnowLayerStageMax) + 1);
                        if (overflow > 0)
                            emit snowLayerCollapseDropped(cx, dropCellY + 1, cz, 0x23D, overflow);
                    } else {
                        // 其它不完整方块（半砖 / 火把…）→ 全部层数掉雪球（state+1 个，同铲挖语义）。
                        const int layers = std::min(int(aBlockState) + 1, int(BlockRegistry::SnowLayerStageMax) + 1);
                        emit snowLayerCollapseDropped(cx, dropCellY + 1, cz, 0x23D, layers);
                    }
                    toRemove.push_back(idx);
                    dirty = true;
                } else if (BlockRegistry::isAnvil(aBlockId)) {
                    // t794 铁砧落不完整方块（火把 / 半砖 / 雪层…）上：**还原铁砧方块**于该方块上方一格 ——
                    //   与沙 t220「落部分方块碎成掉落物」语义分叉（机制等价 MC 1.0 铁砧落任何实体上都还原
                    //   方块，不碎成物品；不改损坏阶段，落地恒还原原阶段。浮在部分方块上方不二次坍落 ——
                    //   setBlockFromEntity 直写不经 checkGravityBlockOnEdit，破掉那格支撑才会再落，无死循环）。
                    //   occ 守卫内（该格 air/水，列扫保证；被占罕见时静默消失，同沙兜底）+ 落地音事件。
                    world->setBlockFromEntity(cx, dropCellY + 1, cz, aBlockId);
                    emit fallingBlockLanded(cx, dropCellY + 1, cz, int(aBlockId));
                    toRemove.push_back(idx);
                    dirty = true;
                } else {
                    emit fallingBlockDropped(cx, dropCellY + 1, cz, int(aBlockId));
                    toRemove.push_back(idx);
                    dirty = true;
                }
            } else if (newY <= 0.0f) {
                // 全列无支撑且已跌出世界底部 → 移除（防永久下落；正常世界 y=0 有石头层不触发）。
                toRemove.push_back(idx);
                dirty = true;
            } else {
                // B-M1：本分支写回实体（唯一写点）→ 上方铁砧砸伤段的 emit 可能已 realloc，**重取引用**再
                //   读写（快照管只读面、重取管写回；非铁砧路径无 emit 窗口，重取 == 原引用，零行为差）。
                Entity &eNow = m_entities[size_t(idx)];
                if (newY != eNow.pos.y()) {
                    eNow.pos.setY(newY); // 继续自由下落（穿过 air / 水）
                    dirty = true;
                }
            }
            continue; // 不走 Mob 的 AI / resting / 重力逻辑
        }

        // --- Mob（t239）---
        if (e.kind == Mob) {
            if (e.dead) {
                // t905 状态直方图：dead 尸体（deathTimer 倒计时帧）。
                FrameProfiler::instance()->count("mobStDead");
                // 死亡态：冻结 AI / 重力 / 敌对攻击，仅 deathTimer 倒计时（给 QML 播侧倒动画 + 白烟窗口）+
                //   hurtFlash 衰减（让 killing blow 的红闪自然褪去）。deathTimer≤0 → emit mobDied（掉落物，
                //   t449 延迟到此刻）+ 标记移除（releaseSlot）。
                e.deathTimer -= float(dt);
                if (e.deathTimer <= 0.0f) {
                    // t449 死亡过渡结束才掉落（机制等价 MC 倒地动画后产掉落物）：mobDied 在 damageEntity 致死
                    //   瞬间不再 emit，改在此 emit —— 侧倒 + 白烟已播完 kDeathTime → 此刻掉落物自然弹出。
                    //   坐标 floor(pos) 与 spawnItem 整数格入口一致（dead 态 pos 冻结，与致死瞬间同位）。
                    //   t479 wasBaby = 致死瞬间快照（deathBaby）—— 幼崽死亡不掉落（呈现层 onMobDied 守卫跳战利品 +
                    //   XP）；0.5s 死亡动画窗口内 growTimer 可能到 0 长大，快照保「致死时是幼崽」语义（同 deathBurned）。
                    //   t789 woolIndex = 羊毛色下标（仅 MobSheep 有意义；呈现层羊分支据此掉对应色羊毛）。
                    //   review #32 sheared = 致死瞬间快照（deathSheared）—— 剪毛羊死亡不掉羊毛的呈现层守卫依据。
                    const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
                    emit mobDied(dx, dy, dz, e.mobType, e.deathBurned, e.deathBaby,
                                 e.mobType == MobSheep ? e.sheepWool : 0,
                                 e.mobType == MobSheep && e.deathSheared);
                    toRemove.push_back(idx);
                    dirty = true;
                }
                if (e.hurtFlash > 0.0f) {
                    e.hurtFlash -= float(dt);
                    if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; } // 红闪结束 → bump 让 QML 翻回 baseColor
                }
                continue; // dead：不走 AI / 重力
            }

            // t811 载具骑乘态冻结：mob 坐上矿车/船后 AI / 物理 / 环境判定**全停**（不动不漂、姿态锁定 ——
            //   登乘时已清 moveSpeed + walkPhase/stepAccum 归零（review27 #12：腿回中立位，非冻结半步
            //   相位）。位置钉载具座位由 tickVehicleRiding 负责
            //   （PlayerController 在载具物理之后调 → 同帧随车不滞后）。掉血 / 死亡照常：damageEntity 是
            //   外部路径（玩家攻击 / 箭 / 火）不经本循环，dead 翻 true 后走上文死亡分支 → 尸体 / 掉落留在
            //   载具处（spec「骑乘中被箭射死 → 尸体/掉落在载具处释放」）。仅保 hurtFlash 衰减（受击红闪
            //   自然褪去）。dead 优先于本守卫（上文先判）。守卫只看 rideX 字段：对账释放（车被挖）发生在
            //   tickVehicleRiding → 下一 tick 本守卫自然放行（mob 恢复 AI），至多冻结一帧可忽略。
            if ((e.rideCart >= 0 && m_cartMgr) || (e.rideBoat >= 0 && m_boatMgr)) {
                // t905 状态直方图：骑乘冻结（早退帧，AI/物理全停）。
                FrameProfiler::instance()->count("mobStRide");
                if (e.hurtFlash > 0.0f) {
                    e.hurtFlash -= float(dt);
                    if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; }
                }
                continue; // 骑乘态：AI / 重力 / resting / 击退 / jumpG / 流推 / 火 / 仙人掌 / 窒息全跳（防漂移）
            }
            // t952 mob-on-mob 被骑乘态（小鸡骑士的骑手；同 t811 冻结口径的挂载变体）：主循环 AI / 物理 /
            //   环境判定全停（**垂直物理必停**——重力 + 落地扫描的 `vy<0 → snap 支撑顶` 会把骑手从载具背上
            //   拽到地面穿模，位置 Y 权威归 tickMobMounts 钉载具顶），但 aiAccum 照累积——骑手 AI 不死，在
            //   tickMobMounts 挂载 pass 内以同一 kAiTickInterval 节奏消费（AI 位移驱动载具，「小僵尸的追击
            //   移动驱动小鸡」）。掉血 / 死亡照常（外部 damageEntity 路径）；击退速度在挂载态不应用（vx/vz
            //   留存不施位移，解除挂载后自然衰减——与矿车乘客击退不生效同口径，登记取舍）。对账解除（载具
            //   死 / 槽复用）发生在 tickMobMounts → 下一 tick 本守卫自然放行（落地恢复独立 AI）。
            if (e.rideMob >= 0) {
                // t905 状态直方图：同骑乘冻结桶（挂载乘客物理全停）。
                FrameProfiler::instance()->count("mobStRide");
                e.aiAccum += float(dt); // 骑手 AI 节拍累积（tickMobMounts 按 aiTick 帧消费）
                if (e.hurtFlash > 0.0f) {
                    e.hurtFlash -= float(dt);
                    if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; }
                }
                continue; // 挂载态：重力 / resting / 击退应用 / 流推 / 溺水 / 走路声全跳（AI 走挂载 pass）
            }
            // t905 状态直方图：活体自由态（AI + 每帧物理照跑）。resting=静置（非 aiTick 帧 continue 早退），
            //   非 resting=下落/水中（重力 + 落地扫描每帧跑）—— stF 高 = 振荡 / 悬空 mob 主吃尾段的判据。
            FrameProfiler::instance()->count(e.resting ? "mobStRest" : "mobStFall");

            // t500 perf：mob AI / 环境扫描错峰节流 —— 每 kAiTickInterval 帧（按 idx 错峰）跑一次「火烧 / 仙人掌 /
            //   AI 决策 + 移动 / 窒息」重活，传 aiDt = 自上帧起的累积 dt → AI 速度 / 火伤 / 仙人掌扎伤 / 窒息 /
            //   吃草推进「每秒平均速率」与原每帧路径一致（aiDt = N·dt 抵消 N 倍节流）。物理（重力 / resting /
            //   推动 / 击退）+ 受击红闪 + 走路声 + 环境音 + 水流推动仍每帧跑（连续体感 + 即时反馈）。
            //   mob 桶瓶颈（用户实测 24.99ms/f）由每 mob 每帧 ~50 blockAt（mobAabbHitsSolid×2 全格扫 + 仙人掌
            //   10 邻接 + 视线 raycast）× 60 槽 = 数千 blockAt/帧 构成；错峰节流后单帧平均 1/N mob 跑重活 →
            //   削到目标 <5ms/f。机制等价 MC 1.0 mob AI 节流（mob think 每 4-5 tick，非每 tick 全员跑）。
            e.aiAccum += float(dt);
            const bool aiTick = ((m_tickPhase + quint32(idx)) % quint32(kAiTickInterval)) == 0;
            const float aiDt = aiTick ? e.aiAccum : 0.0f;
            if (aiTick) e.aiAccum = 0.0f;
            // t500 fix：speedScale 降到 aiTick 块内（AI 函数 + flow push 用它）。非 aiTick 帧默认 1.0 → flow push
            //   的 `if (speedScale < 1.0f)` 自然跳过（水中 flow push 每 N 帧跑一次、N× 步长，平均推力不变）。
            //   省 1 blockAt/mob/非-aiTick-帧（60 槽 × 3/4 帧 ≈ 45 blockAt/帧）。
            float speedScale = 1.0f;

            // 审查修 L16：火免疫（燃烬者）残留 fireTimer 清零**提到 aiTick 节流块外**（每帧 O(1) 一次比较
            //   + 罕见置零）—— 旧版清零在下方 4 帧节流块内的免疫分支，外部 ignite() 路径（火格点燃 / 命中
            //   火焰 / 玩家侧点燃）设的 fireTimer 最长挂 ~4 帧（~66ms）才被清 → QML isBurningAt 短闪火焰
            //   残留。每帧清零后外部点燃即时压制（免疫 = 不伤 + 不显焰）；火烧系统主体仍在节流块内（B2
            //   语义不变，仅清零时机提前）。
            if (kEmberlingFireResistImmunity && e.mobType == MobEmberling && e.fireTimer > 0.0f) {
                e.fireTimer = 0.0f;
                e.fireDamageTimer = 0.0f;
                dirty = true; // 熄火 → bump（QML 收火焰）
            }

            // t500 perf mob 子桶：mobAI 计时 —— 包裹整个 aiTick 块（火烧 / 仙人掌 / AI 决策移动 / 吃草）。
            //   非 aiTick 帧此块全跳过 → aiNs 不累（mobAI≈0）；aiTick 帧 aiNs 累入 mobAI 桶。
            // t905 细分：aiT0 同时是 head 段的结清点（迭代起点→此处的 headNs 入账），ai 段开启。
            const qint64 aiT0 = FrameProfiler::nowNs();
            if (segId == 0 && segT0 > 0) headNs += aiT0 - segT0;
            segT0 = aiT0;
            segId = 1; // ai 段进行中：块内 continue（如火伤致死早退）由下一迭代起点 / 循环尾结清入 aiNs
            if (aiTick) {
                speedScale = mobFeetInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH)
                             ? kWaterSpeedMul : 1.0f;
                // t482 雪球减速（slowTimer>0）：水平移动 ×kSnowSlowMul（叠加水中减速）。只对被雪球命中的 mob
                //   生效（雪傀儡 / 铁傀儡自身 slowTimer 恒 0 不触发）。减速期 QML isSlowedAt 显蓝调。
                if (e.slowTimer > 0.0f) speedScale *= kSnowSlowMul;
                // t642 作物格减速（脚位格是作物）：水平移动 ×kCropSlowMul（叠加水中/雪球减速）。机制等价
                //   MC 怪在耕地作物上慢走 —— mob 穿越 / 追击穿越农田时被作物绊慢（玩家有防护性减速观感，
                //   「踩作物但不白嫖穿行」）。脚位 = floor(pos.y−halfH)（mob AABB 底面格），同 mobFeetInWater 语义。
                if (world) {
                    const int cfy = qFloor(e.pos.y() - e.halfH);
                    if (cfy >= 0
                        && isCropBlock(world->blockAt(qFloor(e.pos.x()), cfy, qFloor(e.pos.z()))))
                        speedScale *= kCropSlowMul;
                }
            // t344 火烧系统（岩浆 / 火点燃；ALL mobs 含 passive；机制等价 MC 1.0 实体触岩浆着火 + 火伤 + 熄灭）。
            //   分两段：
            //   (1) 岩浆接触点燃：mob 脚位格 floor(pos.y−halfH) 或身体中心格 floor(pos.y) 任一 == Lava → 刷新
            //       fireTimer = kFireDuration（落入岩浆湖 / 踩岩浆流即着火；机制等价 MC 实体进岩浆着火）。
            //       仍在岩浆中重置火伤累积（机制等价 MC：岩浆内持续重燃，fireTimer 不递减）。
            //   (2) 火烧推进：fireTimer>0 → 离开岩浆后递减；每 kFireDamageInterval 扣 1HP（复用 damageEntity 受击链：
            //       扣血 + 红闪 + 归零 mobDied 带 burned=true → 熟肉掉落）+ 掷随机提前熄灭（kFireExtinguishChance）。
            //       fireTimer 自然归零即熄（定时双保险）。passive 与敌对均走本段（日光 burning 仍由 tickHostileLife
            //       独立管，二者在 isBurningAt 合取显火焰）。只读 World::blockAt（向下依赖）。
            //   t728 火力免疫（Emberling；机制等价 MC 1.0 烈焰人免疫火伤）：燃烬者跳过整段火烧系统 —— 岩浆 / 火
            //   格不点燃、已有 fireTimer 不推进扣血（惰性 → 不伤），且不受外部 ignite（fire 点燃）影响。火免疫
            //   故段内全部逻辑对其 no-op（不点燃 / 不扣火伤 / 岩浆不点燃），isBurningAt 亦不显火焰（fireTimer 恒 0）。
            //   审查修 B2（t724-t729 复盘）：免疫者残留 fireTimer（外部路径所设）的清零已**提到节流块外每帧
            //   执行**（见上方 L16 段注释）—— 旧版在本节流块内跳过「递减 + 随机熄灭」却无人清 → 永久火焰 /
            //   4 帧残留；kEmberlingFireResistImmunity 常量在彼处与下方守卫两处消费（审查修 B13 一并）。
            const bool emberlingFireImmune = kEmberlingFireResistImmunity && e.mobType == MobEmberling;
            if (!emberlingFireImmune) {
                const int fx = qFloor(e.pos.x());
                const int fz = qFloor(e.pos.z());
                const int footY = qFloor(e.pos.y() - e.halfH); // 脚位（AABB 底面）格
                const int bodyY = qFloor(e.pos.y());           // 身体中心格
                // 容差皮 ≫1e-4 snap 缝、≪半宽（玩家侧判据族同值；review27 #10 提升到火段共享——下方
                //   AABB 扫描 / 站顶分支 / t843 中心列快速路径三处同值同口径）。
                constexpr float kTouchSkin = 0.002f;
                bool touchingLava = false;
                // t724：火焰格（Fire）并入点燃判定 —— mob 脚位 / 身体格 == Fire 同样持续点燃（机制等价
                //   MC 1.0 实体站火 / 穿火着火；与岩浆共用 t344 火烧推进链：余焰扣血 / 随机熄灭 / 熟肉掉落）。
                if (footY >= 0 && world->blockAt(fx, footY, fz) == BlockRegistry::Lava) touchingLava = true;
                if (!touchingLava && bodyY >= 0 && world->blockAt(fx, bodyY, fz) == BlockRegistry::Lava)
                    touchingLava = true;
                if (footY >= 0 && world->blockAt(fx, footY, fz) == BlockRegistry::Fire) touchingLava = true;
                if (!touchingLava && bodyY >= 0 && world->blockAt(fx, bodyY, fz) == BlockRegistry::Fire)
                    touchingLava = true;
                // t888 接触语义对齐 MC（t890 复核收口，玩家侧 step 同批）：旧三格判定只查 mob **中心列**
                //   → 贴燃烧方块侧壁走的 mob（身体在邻格、中心不含燃烧格）永不点燃。修法 = 满格 AABB 接触
                //   扫描（玩家侧 kTouchSkin 容差皮先例）：mob AABB（halfW/halfH）与 Fire/燃烧格满格盒重叠
                //   即接触；扫自身 XZ 覆盖格 + 正交 4 邻（侧壁贴走时燃烧格在邻格）。岩浆保持中心列判定不动
                //   （流体接触语义 + 泡岩浆独立伤害链，防隔墙误燃）。
                if (!touchingLava) {
                    const float mMinX = e.pos.x() - e.halfW, mMaxX = e.pos.x() + e.halfW;
                    const float mMinZ = e.pos.z() - e.halfW, mMaxZ = e.pos.z() + e.halfW;
                    const float mMinY = e.pos.y() - e.halfH, mMaxY = e.pos.y() + e.halfH;
                    const int xLo = qFloor(mMinX), xHi = qFloor(mMaxX);
                    const int zLo = qFloor(mMinZ), zHi = qFloor(mMaxZ);
                    const int yHi = qFloor(mMaxY);
                    static constexpr int kNb4[5][2] = {{0,0},{1,0},{-1,0},{0,1},{0,-1}};
                    for (int yy = footY; yy <= yHi && !touchingLava; ++yy) {
                        if (yy < 0) continue;
                        for (int cx = xLo; cx <= xHi && !touchingLava; ++cx) {
                            for (int cz = zLo; cz <= zHi && !touchingLava; ++cz) {
                                for (const auto &o : kNb4) {
                                    const int nx = cx + o[0], nz = cz + o[1];
                                    if (nx < 0 || nz < 0 || nx >= int(worldW) || nz >= int(worldD)
                                        || yy >= world->height()) continue;
                                    const quint8 nid = world->blockAt(nx, yy, nz);
                                    const bool fireSrc = nid == BlockRegistry::Fire
                                                         || world->isBurningAt(nx, yy, nz);
                                    if (!fireSrc) continue;
                                    const float cMinX = float(nx), cMaxX = float(nx) + 1.0f;
                                    const float cMinZ = float(nz), cMaxZ = float(nz) + 1.0f;
                                    const float cMinY = float(yy), cMaxY = float(yy) + 1.0f;
                                    if (mMinX <= cMaxX + kTouchSkin && mMaxX >= cMinX - kTouchSkin
                                        && mMinZ <= cMaxZ + kTouchSkin && mMaxZ >= cMinZ - kTouchSkin
                                        && mMinY < cMaxY && mMaxY > cMinY) {
                                        touchingLava = true; break; // 复用火烧链（变量名沿 t344）
                                    }
                                }
                            }
                        }
                    }
                    // 站顶分支（review27 #10，玩家侧 review27 #3 同构——口径统一 / 行为对称）：mob 站燃烧
                    //   方块顶面——主扫描 yy 从 footY 起恒不覆盖 footY-1 支撑格（Y 严格界定的固有边界），
                    //   脚下支撑层按 XZ 足印覆盖列 + **Y 界定**（脚底贴支撑面 ±容差皮才算；腾空跳越 /
                    //   下落掠过不误燃，与玩家侧跳越不点燃对称）复探。部分悬站（足印盖到燃块列而中心列
                    //   不在其上）从此同玩家口径点燃——旧版只有 t843 中心列行覆盖站顶，行为不对称。
                    if (!touchingLava && footY - 1 >= 0 && footY - 1 < world->height()) {
                        const int supY = footY - 1;
                        for (int cx = xLo; cx <= xHi && !touchingLava; ++cx) {
                            if (cx < 0 || cx >= int(worldW)) continue;
                            for (int cz = zLo; cz <= zHi && !touchingLava; ++cz) {
                                if (cz < 0 || cz >= int(worldD)) continue;
                                const quint8 nid = world->blockAt(cx, supY, cz);
                                if ((nid == BlockRegistry::Fire || world->isBurningAt(cx, supY, cz))
                                    && mMinY <= float(footY) + kTouchSkin) {
                                    touchingLava = true; // 支撑面即火源 = 接触（站燃块顶必点燃）
                                }
                            }
                        }
                    }
                }
                // t843：燃烧中的可燃方块并入接触点燃（World::isBurningAt 侧表真值）。三格判定现为**中心列
                //   快速路径**（主扫描含 foot/body 自身格；站顶分支含脚下一格的足印覆盖列 ⊃ 中心列）——
                //   命中即短路省全扫（语义等价，双保险）。review27 #10：脚下一格行补与站顶分支同款 Y 界定
                //   （玩家侧 review27 #3 口径：脚底贴支撑面才算）——不补则 mob 跳越燃块顶经中心列复活误燃。
                if (footY - 1 >= 0 && e.pos.y() - e.halfH <= float(footY) + kTouchSkin
                    && world->isBurningAt(fx, footY - 1, fz)) touchingLava = true;
                if (!touchingLava && footY >= 0 && world->isBurningAt(fx, footY, fz)) touchingLava = true;
                if (!touchingLava && bodyY >= 0 && world->isBurningAt(fx, bodyY, fz)) touchingLava = true;
                if (touchingLava) {
                    if (e.fireTimer < kFireDuration) { e.fireTimer = kFireDuration; dirty = true; } // 翻入着火 → bump（QML 显火焰）
                    // t803：**不再清零 e.fireDamageTimer**（对齐玩家侧 t351 修复）。旧版在火 / 岩浆内每 AI tick
                    //   把火伤累积器归零 → 累积永达不到 kFireDamageInterval → **mob 泡在火里反而不扣血**（同玩家
                    //   t351「伤害时有时无」的 mob 侧镜像；点燃视觉有、周期火伤无）。现只刷 fireTimer（保持续燃
                    //   = 离开前不熄），火伤累积器照常推进 → 站火 / 余焰均按 kFireDamageInterval 稳定扣血。
                }
                // t385 雨灭 mob 火（spec「雨灭 mob 火」）：mob 直接见天（skyLightAt>=15 = 头顶无遮挡）且所在列
                //   正降水（雨/雪/雷，群系解析；沙漠不降水）→ 立即灭火。机制等价 MC 雨水浇灭着火实体。
                //   仅露天生效（树下/屋内不淋雨，火不灭）。与日光 burning 独立（fireTimer 适用于所有 Mob）。
                //   review27 #11：判据抽到 World::rainExtinguishesAt（玩家 / mob 单一权威，玩家侧同批补
                //   雨灭 / 水灭路径——此前玩家着火必烧满 8s 无提前止损）。
                if (e.fireTimer > 0.0f && world->rainExtinguishesAt(fx, bodyY, fz)) {
                    e.fireTimer = 0.0f;
                    e.fireDamageTimer = 0.0f;
                    dirty = true; // 熄火 → bump（QML 收火焰）
                }
                if (e.fireTimer > 0.0f) {
                    if (!touchingLava) e.fireTimer -= float(aiDt);
                    e.fireDamageTimer += float(aiDt);
                    if (e.fireDamageTimer >= kFireDamageInterval) {
                        e.fireDamageTimer -= kFireDamageInterval;
                        // 先掷随机提前熄灭（t888 起恒 0 = MC 常态火不自灭；雨灭走上方独立路径）。
                        // review27 #17①：if constexpr 按常量门控——kFireExtinguishChance == 0 时掷骰与
                        //   永假分支整体编译期剔除（旧版每 mob 每火伤脉冲白耗一次全局 RNG）。**常量回改
                        //   非零前必读**：t888 节奏标定按「必烧满 kFireDuration」校准，复活掷骰 = 实际火伤
                        //   期望低于标定，须同步重标（若接 Peaceful 难度再复用本口——锚点保留）。
                        bool earlyExtinguished = false;
                        if constexpr (kFireExtinguishChance > 0.0f) {
                            earlyExtinguished = QRandomGenerator::global()->generateDouble()
                                                < double(kFireExtinguishChance);
                            if (earlyExtinguished) {
                                e.fireTimer = 0.0f;
                                e.fireDamageTimer = 0.0f;
                                dirty = true; // 熄火 → bump（QML 收火焰）
                            }
                        }
                        if (!earlyExtinguished && !e.dead) { // 防御：damageEntity 可能本帧已死
                            damageEntity(idx, 1); // 火伤 1HP（复用受击链；归零 mobDied 带 burned=true）
                            dirty = true;
                        }
                    }
                    // 定时熄灭：fireTimer 自然归零（离开岩浆后持续 kFireDuration 秒即灭）。
                    if (e.fireTimer <= 0.0f) {
                        e.fireTimer = 0.0f;
                        e.fireDamageTimer = 0.0f;
                        dirty = true;
                    }
                }
            } // /t728 火免疫：非 Emberling 走火烧系统；Emberling 跳过（不点燃不扣火伤；残留火清零已提级
              //    到节流块外每帧执行 —— 审查修 L16，见上方）
            // 火伤可能本帧致死（damageEntity 置 dead）→ 本帧不再走 AI / 重力（同上方 dead 分支语义，防死尸位移）。
            if (e.dead) continue;

            // t394 仙人掌接触伤害（spec「contact damages entities that touch it」；机制等价 MC 1.0 仙人掌触碰即伤）。
            //   接触判定：mob 脚位 / 身体格（floor(pos±halfH)）及其水平 4 邻 + 脚下格，任一 == Cactus 即接触
            //   （覆盖「撞其侧」+「站其顶」；Cactus 是实体整立方 → mob 碰撞停在邻格，故脚 / 身体格本身恒非 Cactus，
            //   须查邻接格 / 脚下格）。每 kCactusDamageInterval(0.5s) 扣 1HP（复用 damageEntity 受击链：扣血 + 红闪 +
            //   归零 mobDied 死亡掉落）。离开即重置累积器（机制等价 MC：接触才扣，离开即停）。只读 World::blockAt
            //   （向下依赖）。
            {
                const int fx = qFloor(e.pos.x());
                const int fz = qFloor(e.pos.z());
                const int footY = qFloor(e.pos.y() - e.halfH); // 脚位（AABB 底面）格
                const int bodyY = qFloor(e.pos.y());           // 身体中心格
                auto cactusCell = [&](int xx, int yy, int zz) -> bool {
                    return yy >= 0 && yy < world->height()
                           && world->blockAt(xx, yy, zz) == BlockRegistry::Cactus;
                };
                bool touch = false;
                for (int yy : {footY, bodyY}) {
                    if (cactusCell(fx,     yy, fz) || cactusCell(fx + 1, yy, fz)
                        || cactusCell(fx - 1, yy, fz) || cactusCell(fx, yy, fz + 1)
                        || cactusCell(fx, yy, fz - 1)) { touch = true; break; }
                }
                if (!touch && cactusCell(fx, footY - 1, fz)) touch = true; // 站在仙人掌顶
                if (touch) {
                    e.cactusDamageTimer += float(aiDt);
                    if (e.cactusDamageTimer >= kCactusDamageInterval) {
                        e.cactusDamageTimer = 0.0f;
                        if (!e.dead) { damageEntity(idx, 1); dirty = true; } // 仙人掌扎伤 1HP（复用受击链）
                    }
                } else {
                    e.cactusDamageTimer = 0.0f; // 离开即重置
                }
            }
            // 仙人掌扎伤可能本帧致死 → 本帧不再走 AI / 重力（同上方 dead 分支语义，防死尸位移）。
            if (e.dead) continue;

            // t828 mob 水下窒息（spec「生物水下有呼吸时间，太久不浮上来掉血」；机制等价玩家 t202 溺水——
            //   15s 呼吸耗尽后 1HP/s，节奏同玩家 10 气泡×1.5s + kDrownInterval 1s）。头部判定 = t979
            //   mobSnoutSubmergedInWater 单一权威（口鼻线 vs 格内实际水面高度——矮 mob 站 1/8、1/4 浅水
            //   档不再误判没顶；满格水源与旧整格判完全等价，见谓词头注释）。**鱿鱼豁免**（水生不溺水，
            //   机制等价 MC 1.0 squid；t980 在 else 分支给鱿鱼离水搁浅掉血——同一判据族两向用）。节流帧用
            //   累积 aiDt（t500：状态累积器必须累积 dt，平均速率与每帧一致）。口鼻出水 → 双计时器清零
            //   （呼吸恢复，机制等价玩家出水气泡回满）。掉血走 damageEntity 受击链（红闪 + 归零 mobDied
            //   掉落）；致死本帧 continue 由下方 AI 段前的 dead 分支兜（同火/仙人掌语义）。
            if (e.mobType != MobSquid) {
                const bool dHeadInWater = mobSnoutSubmergedInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH);
                if (dHeadInWater) {
                    e.mobAirTimer += float(aiDt);
                    if (e.mobAirTimer >= kMobBreathSeconds) {
                        e.mobDrownTimer += float(aiDt);
                        if (e.mobDrownTimer >= kMobDrownInterval) {
                            e.mobDrownTimer -= kMobDrownInterval;
                            if (!e.dead) {
                                damageEntity(idx, 1); // 溺水 1HP/s（复用受击链）
                                dirty = true;
                            }
                        }
                    }
                } else if (e.mobAirTimer > 0.0f || e.mobDrownTimer > 0.0f) {
                    e.mobAirTimer = 0.0f;
                    e.mobDrownTimer = 0.0f; // 口鼻出水 → 呼吸恢复（清累积，下次浸水重新计 15s）
                }
                if (e.dead) continue; // 溺死本帧不再走 AI / 重力（同火伤 / 仙人掌语义，防死尸位移）
            } else {
                // t980 鱿鱼离水搁浅窒息（MC 1.0 语义：水生者离水持续受伤至死——「鱿鱼上岸会死」）。
                //   判据 = mobBodyAboveWaterSurface（t979 淹没谓词同族反向：体底线离水，半浸水洼不算）。
                //   节奏：kSquidStrandGraceSeconds(1s) 宽限 → 每 kMobDrownInterval(1s) 扣 1HP（复用
                //   受击链 + mobDied 掉落链）。计时器复用 mobAirTimer（干渴累积）+ mobDrownTimer（扣血
                //   累积）——上方溺水块对鱿鱼整体跳过，两字段对其独占无冲突；回水即双清（止血，机制
                //   等价入水立刻脱险）。
                const bool stranded = mobBodyAboveWaterSurface(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH);
                if (stranded) {
                    e.mobAirTimer += float(aiDt);
                    if (e.mobAirTimer >= kSquidStrandGraceSeconds) {
                        e.mobDrownTimer += float(aiDt);
                        if (e.mobDrownTimer >= kMobDrownInterval) {
                            e.mobDrownTimer -= kMobDrownInterval;
                            if (!e.dead) {
                                damageEntity(idx, 1); // 搁浅窒息 1HP/s（复用受击链）
                                dirty = true;
                            }
                        }
                    }
                } else if (e.mobAirTimer > 0.0f || e.mobDrownTimer > 0.0f) {
                    e.mobAirTimer = 0.0f;
                    e.mobDrownTimer = 0.0f; // 回水 → 计时清零（立即止血）
                }
                if (e.dead) continue; // 搁浅致死本帧不再走 AI / 重力（同火/仙人掌/溺水语义，防死尸位移）
            }

            // t239 AI wander 自主移动（水平）：随机选向 + 时间片 + 逐轴 AABB 碰撞。位移 → dirty（驱动 QML 位置绑定）。
            // t241 羊吃草门控：eatTimer>0（吃草周期内）→ 跳过 wander + 强制 idle 站立（腿停 + 头俯仰），仅推进
            //   吃草周期；否则走 AI wander，并据 idle + 扫描冷却决定是否开吃草周期。
            // t298 怪物受水流影响：脚位在水格 → 水平减速（speedScale=kWaterSpeedMul）。透传给各 AI 函数缩放位移；
            //   浮力缓沉 + 流水推动在下方 Mob 分支末段（flow push）+ 共享垂直段（buoyancy）处理。
            // t500 perf：speedScale 已提到 aiTick 守卫之外（每帧算，供水流推动用）；此处不重复。
            const bool isSheep = (e.mobType == MobSheep);
            // t1042 惊逃优先级置顶：惊逃中的羊不进吃草周期（受击打断吃草 → 背离玩家疾走；周期剩余
            //   eatTimer 冻结保留，惊逃结束后无食草冷却地自然续吃——MC panic 压过一切常规行为）。
            const bool eating = isSheep && e.eatTimer > 0.0f && e.panicTimer <= 0.0f;
            if (eating) {
                // 吃草周期：推进计时；到 apply 阈值时消耗脚下草方块（Grass→Dirt，t897 ①）；周期内强制 idle。
                // t500 perf：节流帧用 aiDt（累积值）推进 → 平均速率与原每帧路径一致。
                e.eatTimer -= float(aiDt);
                const float eatElapsed = kEatDuration - e.eatTimer;
                if (!e.eatApplied && eatElapsed >= kEatApplyAt) {
                    // 即时消耗（apply 在周期中段、近 sin(πp) 包络峰 = 头最低时嚼）。consume=true 写栅格。
                    sheepEatGrass(e, world, worldW, worldD, /*consume=*/true);
                    e.eatApplied = true;
                }
                if (e.eatTimer <= 0.0f) {
                    e.eatTimer = 0.0f;
                    e.eatApplied = false;
                    e.eatCooldown = kEatCooldown; // 吃完一棵后冷却（防连续吃完一片）
                }
                e.wanderSpeed = 0.0f;
                e.moveSpeed = 0.0f; // 站立吃草 → 腿停（walkPhase 由 t897 ② 静止归零链回正）
                dirty = true;       // headPitch 随 eatTimer 变 → 每帧 bump 让 QML 头俯仰绑定刷新
            } else if (e.hostile) {
                // t290 观察者交互门控：玩家不可锁定（创造/观察者）→ 敌对 Mob 不 detect/chase/attack/shoot，
                //   回退 wander（机制等价 MC 1.0 创造/观察者无敌且不被仇恨）。清残留追踪态（chasing/chaseTimer）
                //   + Stalker 熄火（fuseTimer 归零）防 Survival→Creative/Spectator 切换后 mob 仍贴脸/蓄力。
                //   playerTargetable 由 PlayerController 传（mode==Survival）。变值即回退 wander → dirty 若真移动。
                //   t804 例外：**打火石点燃的 Stalker 不走此门**（flintIgnited 不可逆——切观察者已点燃的引信
                //   照常烧完，机制等价 MC 点燃后的苦力怕不因玩家切模式而熄火）→ 落回 mobType 分发链照跑 aiStalker。
                if (!playerTargetable && !e.flintIgnited) {
                    if (e.chasing || e.fuseTimer > 0.0f) {
                        e.chasing = false;
                        e.chaseTimer = 0.0f;
                        e.fuseTimer = 0.0f;
                        dirty = true; // chasing/fuse 翻转 → bump 让 QML 收回追踪高亮 / 蓄力膨胀
                    }
                    if (aiWander(e, float(aiDt), world, worldW, worldD, speedScale)) dirty = true;
                } else if (e.mobType == MobBones) {
                    // t281/t283/t284 敌对 AI：替代 wander。listener = 玩家脚位（tick 参数）。
                    //   t281 Shambler（僵尸）→ aiHostile（detect→pathfind→melee attack）。
                    //   t283 Bones（骷髅弓箭手）→ aiArcher（detect→keep-distance→shoot 远程射箭）。
                    //   t284 Stalker（潜行者/苦力怕）→ aiStalker（detect→chase→fuse→detonate 近距自爆）。
                    //   非追踪回退到 wander（在 aiHostile / aiArcher / aiStalker 内）。
                    if (aiArcher(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale, skyBrightness)) dirty = true;
                    // t331 拉弓期（chasing）每帧 bump revision → QML drawAmountAt 绑定刷新（驱动抬臂 + 弦后拉）；
                    //   即使 aiArcher 返 moved=false（拉弓减速到停），aimTimer 仍在变 → 须 dirty（同 Stalker inflate 模式）。
                    if (e.chasing) dirty = true;
                } else if (e.mobType == MobStalker) {
                    if (aiStalker(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale)) dirty = true;
                    // 蓄力期（chasing）每帧 bump revision → QML inflateAt 绑定刷新（驱动膨胀动画 + 蓄力发白）；
                    //   即使 aiStalker 返 moved=false（蓄力站立不动），inflate 仍在变 → 须 dirty。熄火（fuseTimer→0）
                    //   亦在 chasing 态内 → 一并刷新让 QML 收回膨胀。
                    //   t804：打火石点燃（flintIgnited，玩家可能在远处未追踪）fuseTimer 同样每帧变 → 一并 bump
                    //   （膨胀 / 发白动画照播，不依赖 chasing）。
                    if (e.chasing || e.flintIgnited) dirty = true;
                    // 引爆当帧移除：detonateStalker 置 exploded=true → 跳过后续重力 / resting（尸体即除）。
                    if (e.exploded) { toRemove.push_back(idx); continue; }
                } else if (e.mobType == MobNightwalker) {
                    // t727 Nightwalker（夜行者；机制等价 MC 1.0 末影人）：独立 AI —— 平时近战追击（类 aiHostile），
                    //   叠加瞪视激怒（enraged）+ 瞬移背后重拳 + 怕水扣血瞬移。aiNightwalker 内部据 enraged 分时序。
                    if (aiNightwalker(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale)) dirty = true;
                    // 激怒时序（rageTimer/windupTimer/enrageTimer）+ 怕水水伤累积在 aiNightwalker 内推进；与 Bones 拉弓 /
                    //   Stalker 蓄力同模式 —— 即使未移动（激怒原地发抖 / 蓄力站立），状态仍在变 → 每帧 bump 让 QML
                    //   激怒动画绑定刷新。
                    if (e.mobType == MobNightwalker && (e.enraged || e.enrageTimer > 0.0f)) dirty = true;
                } else if (e.mobType == MobEmberling) {
                    // t728 Emberling（燃烬者；机制等价 MC 1.0 烈焰人）：独立悬浮 AI —— 漂向玩家 + 远程喷火球 +
                    //   贴脸后退。aiEmberling 内部推进 fireCooldown（喷发节律）+ 水平漂移；hover 上下浮动由 QML
                    //   sin 动画驱动（本 AI 只设水平漂移）。火力免疫在火烧分支已跳过（不在此）。
                    if (aiEmberling(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale)) dirty = true;
                    // 火球冷却 / 漂移状态在 aiEmberling 内推进；无独立每帧显示态需 bump（浮动画由 QML Animation
                    //   驱动非 revision 绑定，无需 term）。悬浮移动已由 dirty=true 覆盖（revision → QML position 绑定）。
                } else {
                    if (aiHostile(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale, skyBrightness)) dirty = true;
                }
            } else {
                // 非吃草：扫描冷却倒数（仅羊）；AI wander；羊 idle 且冷却到 → 扫前方草丛决定是否开吃。
                //   t399 鱿鱼（mobType==MobSquid）走 aiSquid（水里喷水游动；非 aiWander），且无吃草分支（非羊）。
                //   t480 狼（mobType==MobWolf）走 aiWolf（未驯服敌对玩家 / 驯服跟随 + 防御 / 坐留守 / 求偶寻偶；
                //     替代 aiWander + 吃草分支，狼非羊无吃草语义）。
                //   t481 豹猫/猫（mobType==MobOcelot）走 aiOcelot（未驯服游荡 / 驯服跟随 + 坐留守 / 求偶寻偶；
                //     替代 aiWander + 吃草分支，猫非羊无吃草语义）。
                //   t482/t483 防御造物（mobType==MobSnowGolem/MobIronGolem）走各自 AI（抛雪球 / 大力攻击敌对），
                //     无吃草 / 求偶 / 繁殖语义（造物不可繁殖）→ 进各自分支早退，不落 aiWander + 吃草 / 求偶段。
                if (e.mobType == MobSnowGolem) {
                    // t529：listener（玩家脚位）保留传 aiSnowGolem（caller 签名兼容），但 t529 已移除「朝玩家」逻辑
                    //   （spec 改「平时 aiWander 随机朝向」），函数体内 Q_UNUSED(playerPos) 不再读它。
                    if (aiSnowGolem(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale)) dirty = true;
                    // 雪傀儡融化（damageEntity 大伤害）可能本帧致死 → 本帧不再走后续逻辑（同仙人掌 / 火伤致死守卫）。
                    if (e.dead) continue;
                } else if (e.mobType == MobIronGolem) {
                    // t635：透传玩家位置 + 可锁定态（反击玩家分支用；非 angry 时两参不读，行为不变）。
                    if (aiIronGolem(idx, e, float(aiDt), world, worldW, worldD, speedScale, listener, playerTargetable)) dirty = true;
                    // t635 蓄力期（golemWindup>0）每帧 bump revision → QML golemAttackPoseAt 绑定刷新（抬臂动画）；
                    //   蓄力站立不动（moved=false）亦须刷新（同 Bones 拉弓 / Stalker 蓄力膨胀模式）。
                    if (e.golemWindup > 0.0f) dirty = true;
                } else if (e.mobType == MobSquid) {
                    if (aiSquid(e, float(aiDt), world, worldW, worldD, speedScale)) dirty = true;
                } else if (e.mobType == MobWolf) {
                    // t947 ① 跟随门通道：playerSpectator 透传 aiWolf（观察者不跟随；PlayerController 按
                    //   mode==Spectator 派生，同 playerTargetable 向下派生 bool 通道）。
                    if (aiWolf(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale,
                               playerTargetable, playerSpectator))
                        dirty = true;
                } else if (e.mobType == MobOcelot) {
                    // review0830 #5：playerSpectator 透传 aiOcelot（观察者不跟随 + 不瞬移——与 aiWolf
                    //   同款门同字面量；PlayerController 按 mode==Spectator 派生，同 aiWolf 通道）。
                    if (aiOcelot(idx, e, float(aiDt), world, listener, worldW, worldD, speedScale, playerSpectator)) dirty = true;
                } else if (e.mobRider >= 0) {
                    // t952 骑士载具 AI 挂起（小鸡被小僵尸驮乘期）：自身 wander / 求偶 / 坐站全停——移动由
                    //   tickMobMounts 钉位跟随骑手（「小僵尸的追击移动驱动小鸡」，dev-plan 最稳刀口径，分离后
                    //   恢复本分支之外的自主 AI）。物理（重力 / 支撑）与下蛋周期在共享尾段照跑（钉位只接管
                    //   XZ，Y 由小鸡自身落地扫描贴地 → 骑手随之起伏；骑乘中的鸡照常周期下蛋，机制等价 MC）。
                    //   moveSpeed 不在此清零：钉位 pass 每帧写入骑手速度驱动 walkPhase（小鸡腿随移动摆动）。
                } else {
                // t1042 被动型惊逃置顶门（MC panic 优先级最高：惊逃期间不寻偶 / 不引诱 / 不幼随 / 不游走
                //   选向 / 不吃草扫描——见上 eating 门）：panicTimer>0 → 走 aiPanicFlee（内含 aiWander 位移）
                //   并跳过下方选向块与公共 aiWander（防同帧双移）。
                if (e.panicTimer > 0.0f) {
                    if (aiPanicFlee(e, float(aiDt), world, worldW, worldD, speedScale, listener)) dirty = true;
                } else {
                // t400 求偶寻偶（spec「喂食 → 求偶 → 同种配对」；机制等价 MC 1.0 love mode 寻偶）：成体可繁殖 mob
                //   在求偶期（loveTimer>0）→ 覆盖 wander 的随机选向，把 yaw 钉向最近同种求偶配偶 + 强制行走 +
                //   短置 wanderTimer（防 aiWander 本帧重新随机选向）→ aiWander 沿该 yaw 行走靠近配偶，使两求偶者
                //   主动相遇进入配对距离（tickBreeding 末段配对产幼崽）。无配偶（仅一方求偶）/ 非求偶 → 走原 wander。
                //   寻偶仅设 yaw/speed/timer（不改 pos），实际位移交 aiWander（复用其逐轴碰撞撤回 + 边界 clamp，
                //   防穿墙 / 出界）。squid loveTimer 恒 0（不可繁殖）→ 本块对其 no-op。
                if (e.loveTimer > 0.0f && !e.baby && isBreedableType(e.mobType)) {
                    const int mate = findNearestMate(idx);
                    if (mate >= 0) {
                        const Entity &mp = m_entities[size_t(mate)];
                        const float mdx = mp.pos.x() - e.pos.x();
                        const float mdz = mp.pos.z() - e.pos.z();
                        // 朝配偶（dir=(-sin,0,-cos) 约定：yaw=atan2(-Δx,-Δz) 使模型 -Z 正对配偶）。
                        e.yawRad = std::atan2(-mdx, -mdz);
                        e.wanderSpeed = kWalkSpeed; // 强制行走（覆盖 idle 可能）
                        e.wanderTimer = 0.4f;       // 防 aiWander 本帧重新随机选向（0.4s > 一帧 dt，下帧再重设）
                    }
                }
                // t1025 食物引诱（spec「MC 动物被手持繁殖食物的玩家吸引」）：玩家手持该物种繁殖食物
                //   （PlayerController 据持物经 setFoodLure 写门控——食物映射在 Game 层，Entities 只收 bool，
                //   PLAN §2 分层）且玩家在 kFoodLureRange 内 → yaw 钉向玩家 + 强制行走（寻偶优先级更高：
                //   上块已在求偶期钉向配偶，本块以 loveTimer<=0 门控互斥；同寻偶仅设 yaw/speed/timer，
                //   位移交 aiWander）。贴身（≤ kFoodLureStopDist）不再钉向（停步防推挤玩家；两引诱同种
                //   相遇仍 < kBreedRange → 持食物聚拢动物即可配对，同 MC 手法）。门控表全 false（空手 /
                //   非食物）→ 本块 no-op，动物照常 wander。review0909 #7：补观察者门 !playerSpectator
                //   （同 aiWolf/aiOcelot 跟随门先例——观察者不可交互、非攻击目标，不应吸引动物围观；
                //   变量在本 tick 作用域已可用，同 t947① 透传通道）。
                else if (!playerSpectator && e.loveTimer <= 0.0f && e.mobType >= 0
                         && e.mobType < kMobTypeCount && m_foodLure[e.mobType]) {
                    const float pdx = listener.x() - e.pos.x();
                    const float pdz = listener.z() - e.pos.z();
                    const float pd2 = pdx * pdx + pdz * pdz;
                    if (pd2 > kFoodLureStopDist * kFoodLureStopDist
                        && pd2 <= kFoodLureRange * kFoodLureRange) {
                        e.yawRad = std::atan2(-pdx, -pdz); // 朝玩家（同 -sin/-cos yaw 约定）
                        e.wanderSpeed = kWalkSpeed;        // 强制行走走向玩家
                        e.wanderTimer = 0.4f;              // 防 aiWander 本帧重新随机选向
                    }
                }
                // t1025 幼崽跟随父母（spec「幼崽跟随最近的成年同种」；机制等价 MC 幼畜跟随成体）：幼崽在
                //   kBabyFollowRange 内认最近成年同种为亲 → yaw 钉向 + 强制行走，贴身（≤ kBabyFollowStopDist）
                //   停步。引诱优先（上块：玩家持食物时幼崽同成体一样被吸引，同 MC）；无成年同种（独苗）→
                //   照常 wander。仅设 yaw/speed/timer，位移交 aiWander（复用逐轴碰撞撤回 + 边界 clamp）。
                else if (e.baby && isBreedableType(e.mobType)) {
                    const int parent = findNearestAdultSameType(idx);
                    if (parent >= 0) {
                        const Entity &pp = m_entities[size_t(parent)];
                        const float pdx = pp.pos.x() - e.pos.x();
                        const float pdz = pp.pos.z() - e.pos.z();
                        const float pd2 = pdx * pdx + pdz * pdz;
                        if (pd2 > kBabyFollowStopDist * kBabyFollowStopDist) {
                            e.yawRad = std::atan2(-pdx, -pdz); // 朝父母（同 -sin/-cos yaw 约定）
                            e.wanderSpeed = kWalkSpeed;        // 强制行走跟随
                            e.wanderTimer = 0.4f;              // 防 aiWander 本帧重新随机选向
                        }
                    }
                }
                if (isSheep && e.eatCooldown > 0.0f) e.eatCooldown -= float(aiDt);
                if (aiWander(e, float(aiDt), world, worldW, worldD, speedScale)) dirty = true;
                } // t1042 惊逃豁免段收口（求偶/引诱/幼随选向 + 公共 aiWander 仅非惊逃帧走；惊逃帧上方 aiPanicFlee 已自含位移）
                if (isSheep && e.eatCooldown <= 0.0f && e.wanderSpeed <= 0.0f) {
                    // idle 且扫描冷却到：脚下是草方块 → 开吃草周期（headPitch 动画 + 中段消耗）；无 → 重置短冷却再等。
                    //   t897 ①：目标 = 脚下草方块（旧「身前草丛」收紧——纯草地无草丛也吃、不吃不吃在看朝向）。
                    if (sheepEatGrass(e, world, worldW, worldD, /*consume=*/false)) {
                        e.eatTimer = kEatDuration; // 进入周期（apply 阈值时才真正消耗，保低头→嚼→抬头 时序）
                        e.eatApplied = false;
                        dirty = true;
                    } else {
                        e.eatCooldown = kEatScanInterval; // 空扫描 → 节流冷却
                    }
                }
                // t300 剪羊毛后的羊吃草方块重新长毛（spec「裸羊站在草方块上偶尔吃它 → 长毛 + 草方块变泥土」；
                //   机制等价 MC 1.0「羊吃草方块重新长毛」）。仅 sheared=true 的羊走本分支（未剪羊毛的羊
                //   sheared=false 默认状态 → 跳过，无需重新长毛）。regrowCooldown 由 shearSheep 设 kRegrowCooldown
                //   防刚剪完即长回；冷却到 + 站在草方块上（脚下方块 id==Grass）→ 翻 sheared=false（重新长毛）+
                //   脚下草方块→泥土（setWaterSilent 静默写，同 sheepEatGrass 的草方块→泥土，非玩家破块 → 不发
                //   broken/placed，免粒子/音/掉落噪音）。扫描用 kRegrowScanInterval 节流（每秒扫一次脚下方块，
                //   足够肉眼可见的「重新长毛」事件，不每帧扫 blockAt 省开销）。
                //   **不要求 idle**（行走中亦可触发，机制等价 MC 1.0 羊走过草方块即可能吃 —— 重新长毛是吃草方块的
                //   派生效果，非主动行为，不强制站立）；spec「偶尔」表概率性，节流扫描间隔本身就是「偶尔」语义。
                if (isSheep && e.sheared) {
                    if (e.regrowCooldown > 0.0f) {
                        e.regrowCooldown -= float(aiDt);
                    } else {
                        // 冷却到：扫脚下方块（AABB 底面下一格 = 支撑格）。是 Grass → 重新长毛 + 草方块→泥土。
                        const int gx = qFloor(e.pos.x());
                        const int gy = qFloor(e.pos.y() - e.halfH) - 1; // 脚位格（AABB 底面）下一格 = 支撑方块
                        const int gz = qFloor(e.pos.z());
                        if (gy >= 0 && world->blockAt(gx, gy, gz) == BlockRegistry::Grass) {
                            world->setWaterSilent(gx, gy, gz, BlockRegistry::Dirt, 0); // 草方块→泥土（静默写）
                            e.sheared = false;       // 重新长毛（QML 据 shearedAt 翻回毛茸外观）
                            // t832 染色羊长回恢复自然原色（一次性语义）：染料染的毛剪掉后，长回的是**自然色**
                            //   ——重掷 kSheepNaturalWeights（t789 权重表单一权威，同 spawn 生成路径）+ 清染色
                            //   标记。未染的羊（sheepWoolDyed=false）长回保持原色不重掷（自然羊剪后长回同色）。
                            if (e.sheepWoolDyed) {
                                e.sheepWool = rollNaturalSheepWool();
                                e.sheepWoolDyed = false;
                                qCInfo(lcEnt) << "dyed sheep regrew natural wool at" << e.pos
                                              << "color index" << e.sheepWool;
                            }
                            e.regrowCooldown = 0.0f; // 未剪羊毛不再推进（下次剪切重置）
                            dirty = true;            // bump → QML 翻外观
                            qCInfo(lcEnt) << "sheep regrew wool at" << e.pos
                                          << "(grass block at" << gx << gy << gz << "-> dirt)";
                        } else {
                            // 站在非草方块上：保持冷却到 0 但不立即长毛；下次扫描间隔（kRegrowScanInterval）
                            //   再查（防每帧扫）。设短冷却节流。
                            e.regrowCooldown = kRegrowScanInterval;
                        }
                    }
                }
                } // 非 squid 的 passive（sheep 吃草 / 通用 wander）；squid 走上面 aiSquid 分支
            }
            } // /aiTick（t500 perf：火烧 / 仙人掌 / AI / 吃草 节流到此；下方物理 + 音频每帧跑）
            // t905 细分：ai 段在此结算（既有 aiNs 累计不动），同时开启 tail 段（ai 后每帧物理 → 下一迭代
            //   起点 / 循环尾）。fire 烧死 continue（下方 e.dead 早退）等本块后的 continue 路径都落在 tail。
            {
                const qint64 aiEnd = FrameProfiler::nowNs();
                aiNs += aiEnd - aiT0; // t500 mob 子桶：mobAI 累入（含 aiTick 跳过的近零开销）
                segT0 = aiEnd;
                segId = 2;
            }

            // t298 流水推动 mob（机制等价玩家 t211：脚位在流水格 state>0 → 沿「离源方向」叠入水平位移）。
            //   流向据脚位 4 向邻居 state 梯度推算：state 低于脚位的邻居 = 近源方向 → 推力朝远离它（离源）。
            //   梯度加权（footState − ns）使陡降（近源 → 远源跨多级）推得更猛；归一化后 ×kWaterFlowPush 叠入位移
            //   （与 AI 行走位移相加 → 逆流净速 = 走速 − 推力，松手则被流走）。逐轴 mobAabbHitsSolid 撤回防穿墙 +
            //   世界边界 clamp（同 aiWander / knockback 位移模式）。仅 speedScale<1（脚位在水格）时执行（无水零开销）。
            //   水源 state=0 不推（无梯度）；四面无更低 state 邻居（对称流）→ glen≈0 不推。
            if (speedScale < 1.0f) {
                const int wfx = qFloor(e.pos.x());
                const int wfy = qFloor(e.pos.y() - e.halfH); // 脚位（AABB 底面）格
                const int wfz = qFloor(e.pos.z());
                if (wfy >= 0 && world->blockAt(wfx, wfy, wfz) == BlockRegistry::Water) {
                    const quint8 footState = world->stateAt(wfx, wfy, wfz);
                    if (footState > 0) { // 流水格才推（水源 state=0 静止不推，同玩家 t211）
                        float gx = 0.0f, gz = 0.0f;
                        constexpr int wdirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                        for (const auto &wd : wdirs) {
                            const int nx = wfx + wd[0], nz = wfz + wd[1];
                            if (world->blockAt(nx, wfy, nz) == BlockRegistry::Water) {
                                const quint8 ns = world->stateAt(nx, wfy, nz);
                                if (ns < footState) { // 该邻居更近源 → 推力朝远离它（离源）
                                    gx -= float(wd[0]) * float(footState - ns);
                                    gz -= float(wd[1]) * float(footState - ns);
                                }
                            }
                        }
                        const float glen = std::sqrt(gx * gx + gz * gz);
                        if (glen > 1e-4f) {
                            const float ehw = e.halfW;
                            const float ehh = e.halfH;
                            const float pushX = (gx / glen) * kWaterFlowPush;
                            const float pushZ = (gz / glen) * kWaterFlowPush;
                            float newX = e.pos.x() + pushX * float(dt);
                            if (newX < ehw) newX = ehw;
                            if (newX > worldW - ehw) newX = worldW - ehw;
                            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                            float newZ = e.pos.z() + pushZ * float(dt);
                            if (newZ < ehw) newZ = ehw;
                            if (newZ > worldD - ehw) newZ = worldD - ehw;
                            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                            if (newX != e.pos.x()) { e.pos.setX(newX); dirty = true; }
                            if (newZ != e.pos.z()) { e.pos.setZ(newZ); dirty = true; }
                        }
                    }
                }
            }

            // 受击红闪衰减（非 dead Mob）：仅在跨过 0 时 bump（红闪期间 colorAt 恒红无需每帧 bump；结束翻回 baseColor）。
            if (e.hurtFlash > 0.0f) {
                e.hurtFlash -= float(dt);
                if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; }
            }

            // t482 雪球减速衰减（slowTimer>0 → 缓慢）：每帧递减，跨 0 时 bump revision → QML isSlowedAt 翻回
            //   蓝调消失（减速期间持续蓝调无需每帧 bump，结束即清回 baseColor）。
            if (e.slowTimer > 0.0f) {
                e.slowTimer -= float(dt);
                if (e.slowTimer <= 0.0f) { e.slowTimer = 0.0f; dirty = true; }
            }

            // t250 环境音 proximity 门控：仅听者 kAudioRange 半径内的活体 mob 才 emit idle/step 叫声（远场静默，
            //   防多 mob 同步吵闹 + 无意义远场音频）。每 mob 每帧算一次（XZ 主导，Y 纳入避免垂直堆叠 mob 全响）。
            const float adx = e.pos.x() - listener.x();
            const float ady = e.pos.y() - listener.y();
            const float adz = e.pos.z() - listener.z();
            const bool inAudioRange = (adx * adx + ady * ady + adz * adz) <= kAudioRange * kAudioRange;

            // t250 mob idle 叫声（牛叫/羊叫/猪叫）：ambientTimer 周期倒计时 → 0 时听者范围内 emit mobAmbient
            //   （mobType 供 AudioManager 选 mob_idle clip）+ 重置随机周期（错峰，防多 mob 同步叫）。
            //   机制等价 MC 1.0 被动生物偶发 idle call；不论 idle/行走/吃草，活体 mob 周期性偶发叫。
            e.ambientTimer -= float(dt);
            if (e.ambientTimer <= 0.0f) {
                e.ambientTimer = kAmbientMin
                                 + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kAmbientMax - kAmbientMin);
                if (inAudioRange) emit mobAmbient(e.mobType);
            }

            // t398 鸡下蛋（spec「periodically lays an EGG item」）：仅 mobType==MobChicken 推进 eggTimer（其余 mob
            //   eggTimer=0 早退）。周期到 → emit chickenLaidEgg(floor(pos))（呈现层据它 spawnItem 生成蛋物品掉落，
            //   同 mobDied→spawnItem 模式）+ 重置随机周期（kEggLayMin..Max，机制等价 MC 1.0 鸡 5-10 分钟下一枚蛋）。
            //   不受 idle/行走态门控（活体鸡无论静止 / 游荡均周期下蛋，机制等价 MC 鸡下蛋独立于行为）。坐标取
            //   floor(pos) 与 spawnItem 整数格约定一致（蛋落在鸡身旁）。无 listener 范围门控 —— 蛋是物品实体非音频，
            //   远场鸡下蛋亦须生成（玩家走近即可见）。
            if (e.mobType == MobChicken) {
                e.eggTimer -= float(dt);
                if (e.eggTimer <= 0.0f) {
                    e.eggTimer = kEggLayMin
                                 + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kEggLayMax - kEggLayMin);
                    emit chickenLaidEgg(qFloor(e.pos.x()), qFloor(e.pos.y() - e.halfH), qFloor(e.pos.z()));
                }
            }

            // t241 行走动画相位推进：moveSpeed>0（行走 / 被推）→ walkPhase 前进（fmod 2π，QML 据它驱动腿摆）；
            //   **t897 ②：静止（idle / 吃草 / 撞墙）→ walkPhase 归零**（腿回中立位）—— 旧「冻结于
            //   上次相位」让停步的 mob 腿卡在半步中间（用户「静止卡住」）；归零 = QML walkPhase 0 的腿摆角 0
            //   （四腿站直，机制等价 MC 停步回正）。非零才清（已 0 的稳态 mob 零额外 dirty）。
            //   review27 #12：**骑乘 / 死亡态恒不达本块**（主循环 continue 早退）——两者在登乘写链 /
            //   死亡翻转处各自归零（walkPhase=stepAccum=0），本块只覆盖活体自由态静止。
            //   t250 mob 走路声：相位推进量同步累加进 stepAccum，每半步（π=一次脚落）听者范围内 emit mobStep
            //   （mobType + 脚下方块 id 供 AudioManager 按材质组选 step clip）。半步语义同 player QML 端
            //   「Δphase≥π 播一次脚步音」，搬进 C++ 避逐 mob 追踪 walkPhase（多 mob 在 QML 追踪不现实）。
            //   归零时 stepAccum 同步清（防残留半步累加在下次起步瞬发一声鬼脚步）。
            if (e.moveSpeed > 0.0f) {
                const float advance = e.moveSpeed * float(dt) * kWalkFreq;
                e.walkPhase = std::fmod(e.walkPhase + advance, 6.2831853f);
                e.stepAccum += advance;
                if (e.stepAccum >= kStepHalfStride) {
                    e.stepAccum -= kStepHalfStride;
                    if (inAudioRange) {
                        // 脚下方块 id（材质组判定用；与 resting 复探同列格 = 底面下方一格）。越界 / air → 0
                        //   → GroupDefault 兜底 Stone step（同 player 脚步音越界处理；仍响）。
                        const int sfx = qFloor(e.pos.x());
                        const int sfz = qFloor(e.pos.z());
                        const int sfy = qFloor(e.pos.y() - e.halfH) - 1;
                        const quint8 sid = (sfy >= 0) ? world->blockAt(sfx, sfy, sfz) : quint8(BlockRegistry::Air);
                        emit mobStep(e.mobType, int(sid));
                    }
                }
                dirty = true;
            } else if (e.walkPhase != 0.0f) {
                e.walkPhase = 0.0f;  // t897 ② 静止归零（腿回中立位，不再冻结半步）
                e.stepAccum = 0.0f;
                dirty = true;
            }

            // t249 击退水平位移应用（vx/vz 衰减 + 逐轴碰撞位移）。knockback() 受击瞬间设 vx/vz，本处每 tick 把
            //   速度转位移（叠加在 aiWander 移动之上 → 击退期间 AI 仍走，二者位移相加，同 MC「既有动量又有击退」）
            //   + 指数衰减（vx *= 1 - kKnockbackDrag*dt，~0.5s 基本停）。逐轴（X 后 Z）mobAabbHitsSolid 撤回防穿墙
            //   （同 aiWander / resolvePlayerPush）；世界边界 clamp（防击退出世界）。速度衰减到可忽略 → 清零（防永
            //   久微小漂移 / 每帧 dirty 抖动）。仅 vx/vz 任一非零时执行（无击退的 mob 零开销跳过）。
            if (std::abs(e.vx) > 1e-4f || std::abs(e.vz) > 1e-4f) {
                const float ehw = e.halfW; // t252 XZ 半宽（边界 clamp + 碰撞）
                const float ehh = e.halfH; // t252 Y 半高（footprint 格扫）
                float newX = e.pos.x() + e.vx * float(dt);
                if (newX < ehw) newX = ehw;
                if (newX > worldW - ehw) newX = worldW - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                float newZ = e.pos.z() + e.vz * float(dt);
                if (newZ < ehw) newZ = ehw;
                if (newZ > worldD - ehw) newZ = worldD - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                if (newX != e.pos.x()) { e.pos.setX(newX); dirty = true; }
                if (newZ != e.pos.z()) { e.pos.setZ(newZ); dirty = true; }
                const float decay = std::max(0.0f, 1.0f - kKnockbackDrag * float(dt));
                e.vx *= decay;
                e.vz *= decay;
                if (std::abs(e.vx) < 0.05f) e.vx = 0.0f; // 衰减到可忽略 → 清零
                if (std::abs(e.vz) < 0.05f) e.vz = 0.0f;
            }

            // t670 越障跳水平滑流应用（jumpGX/jumpGZ；aiHostile/aiArcher/aiStalker 跳起时置，着地清零）：
            //   每帧把滑流转位移（同击退路径：逐轴 mobAabbHitsSolid 撤回 + 世界边界 clamp）。滑流让 mob 跳起后
            //   持续向前漂移 —— 低空时被墙顶碰撞撤回（身体还压在墙里），身体一高过墙顶即漂移通过 → 爬到墙顶，
            //   修「玩家高一格僵尸被卡在台阶下只蹦不上」（t670）。着地（resting 恢复）在下方 resting 分支清零。
            //   与击退（vx/vz）独立叠加（跳起瞬间被击退 → 两段位移相加，同 AI 移动 + 击退共存语义）。
            if (std::abs(e.jumpGX) > 1e-4f || std::abs(e.jumpGZ) > 1e-4f) {
                const float ehw = e.halfW;
                const float ehh = e.halfH;
                float newX = e.pos.x() + e.jumpGX * float(dt);
                if (newX < ehw) newX = ehw;
                if (newX > worldW - ehw) newX = worldW - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                float newZ = e.pos.z() + e.jumpGZ * float(dt);
                if (newZ < ehw) newZ = ehw;
                if (newZ > worldD - ehw) newZ = worldD - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                if (newX != e.pos.x()) { e.pos.setX(newX); dirty = true; }
                if (newZ != e.pos.z()) { e.pos.setZ(newZ); dirty = true; }
            }

            // t254 窒息（机制同玩家 t160 的「眼位嵌实体方块 → 每 1s 扣 1HP」）：mob 头部（AABB 顶格）嵌入实体
            //   可碰撞方块（被沙 / 方块埋住）→ 累加 suffocationTimer，每 kSuffocationInterval 秒扣 1HP（复用
            //   damageEntity → hurtFlash 红闪 / mobDied 死亡掉落链，同玩家 fallDamageTaken(1)→takeDamage）。
            //   头部出方块即停累积（脱困即停伤）。dead mob 已在上方早退 continue 跳过（尸体不再窒息）。
            //   头部格 = floor(pos.y + halfH − ε)；ε 防 AABB 顶恰整数误取上方空气格（漏判窒息 → 沙埋不死）。
            //   用 isCollidable（非 isSolid）：火把 / 开门 / 半砖等非完整碰撞方块不致窒息（同玩家 t160 用
            //   isCollidable 判定；仅「头部被完整碰撞方块包裹」才窒息，机制等价 MC 头卡进 solid block）。
            //   注：damageEntity 内 bump revision + emit，本 tick 末尾仍再 emit 一次（同帧多次 emit 无副作用，
            //   QML 绑定合并到下次事件循环求值）。
            //   t500 perf：与火烧 / 仙人掌同走 aiTick 节流 —— 每 mob 每 kAiTickInterval 帧扫一次头部格，
            //   suffocationTimer 用 aiDt 累积 → 平均窒息扣血速率不变（kSuffocationInterval=1s 量级，节流到 15Hz
            //   误差 <100ms 不可察觉）。每帧跑会浪费每 mob 1 isCollidable（60 槽 = 60 次 / 帧）。
            //   t629 判据收紧「头部格 collidable」→「头部**点**落入该格某 sub-AABB 内」（对齐玩家 t575 眼位
            //   sub-AABB 精判）：旧判据按整格判，1.9 高 mob 站在薄雪层（cell 底 1/8 板）上时头部点在其所在格的
            //   空气区（>7/8 高度处），但该格若恰是可碰撞 SnowLayer / 半砖 → 整格判嵌 → 每秒扣 1HP（用户报
            //   雪傀儡「白天阴凉处也一直扣血」真因 —— 不是太阳，是薄层整格窒息误判）。点在 sub-AABB 内才真嵌
            //   （与碰撞同源，partial 块精确；同玩家 t575 修法）。
            if (aiTick) {
                const float hx = e.pos.x(), hy = e.pos.y() + e.halfH - 1e-3f, hz = e.pos.z();
                const int sx = qFloor(hx);
                const int sz = qFloor(hz);
                const int sy = qFloor(hy);
                bool embedded = false;
                if (sy >= 0 && world->isCollidable(sx, sy, sz)) {
                    // t859：out-param 栈上小缓冲（零堆分配；mob 窒息判定 aiTick 节流帧集中跑）。
                    BlockRegistry::BlockAABB boxes[BlockRegistry::kMaxAABBsPerCell];
                    const int n = world->collisionAABBsAt(sx, sy, sz, boxes, BlockRegistry::kMaxAABBsPerCell);
                    for (int i = 0; i < n; ++i) {
                        const BlockRegistry::BlockAABB &b = boxes[i];
                        if (hx > b.minX && hx < b.maxX && hy > b.minY && hy < b.maxY
                            && hz > b.minZ && hz < b.maxZ) { embedded = true; break; }
                    }
                }
                if (embedded) {
                    e.suffocationTimer += float(aiDt);
                    if (e.suffocationTimer >= kSuffocationInterval) {
                        e.suffocationTimer -= kSuffocationInterval;
                        damageEntity(idx, 1); // 复用受击链：扣 1HP + 红闪 + （归零时）死亡掉落（内含 dead/越界/amount 守）
                        dirty = true;
                    }
                    // t642 卡方块自恢复（轻量兜底）：mob 头部嵌入可碰撞方块（被沙埋 / 方块压身 / 生成残留）时，
                    //   尝试把 mob 移到最近空位（AABB 无碰撞 + 下方有支撑）。主修是刷怪 AABB 适配校验
                    //   （spawnCellFitsHostile 防生成即嵌）；本兜底覆盖「运行期嵌入」—— 沙落下凝固在 mob 身上 /
                    //   爆炸破块后 mob 卡缝 / 旧存档残留等（这些路径不经过刷怪校验）。机制等价 MC 实体被埋时的
                    //   推出脱困。节流：每 kStuckEscapeCooldown 秒最多尝试一次（失败静默下周期再试，防反复空扫）。
                    //   扫描代价：仅真嵌入（罕见态）时 5×5×3 候选 × mobAabbHitsSolid，低频低耗。
                    if (e.stuckEscapeTimer <= 0.0f) {
                        e.stuckEscapeTimer = kStuckEscapeCooldown;
                        const int bx = qFloor(e.pos.x()), by = qFloor(e.pos.y()), bz = qFloor(e.pos.z());
                        const float ehw = e.halfW, ehh = e.halfH;
                        bool escaped = false;
                        // 按 Y 从高到低、水平由近到远扫候选脚格；首个「AABB 无碰撞 + 下方有支撑」→ 移入。
                        //   Y 优先向上（被埋场景头顶更可能近地表）；水平 ±1 环覆盖侧向洞口（洞穴 / 房间）。
                        for (int dy = 2; dy >= -2 && !escaped; --dy) {
                            const int cy = by + dy;
                            for (int dx = -1; dx <= 1 && !escaped; ++dx) {
                                for (int dz = -1; dz <= 1 && !escaped; ++dz) {
                                    if (dx == 0 && dz == 0 && dy == 0) continue; // 当前位已嵌 → 跳过
                                    const float nx = float(bx + dx) + 0.5f;
                                    const float nz = float(bz + dz) + 0.5f;
                                    const float ny = float(cy) + ehh; // 脚位贴候选格底
                                    if (mobAabbHitsSolid(world, nx, ny, nz, ehw, ehh)) continue;
                                    if (!mobFootprintHasSupport(world, nx, nz, cy - 1, ehw)) continue;
                                    e.pos = QVector3D(nx, ny, nz);
                                    e.vy = 0.0f;
                                    e.resting = true;
                                    escaped = true;
                                    dirty = true;
                                    qCInfo(lcEnt) << "mob" << idx << "stuck-escape to"
                                                 << nx << ny << nz;
                                    break;
                                }
                            }
                        }
                    } else {
                        e.stuckEscapeTimer -= float(aiDt);
                    }
                } else {
                    e.suffocationTimer = 0.0f; // 头部出方块 → 停累积（脱困即停伤）
                }
            }
        }

        // --- Mob（非 dead）/ Item：原有 resting + 重力 + 垂直运动（cx/cz 在 AI 行走后重算）---
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过

        // t500 fix perf：resting 复探 + mobInWater 降到 aiTick——resting mob 每 N 帧查一次支撑（mobAabbFootprint
        //   全足迹扫 1-4 blockAt）足够；非 aiTick 帧 continue 跳过重力（resting mob 不下落，无需重力 / 水分流）。
        //   失支撑后（aiTick 翻 resting=false）重力仍每帧跑保平滑下落。重算 aiTick（Mob 块内局部变量不跨块；
        //   同公式同 m_tickPhase → 同一组 mob 在 tick 内两处一致）。
        //   mobInWater（1 blockAt）延迟到此处——仅非 resting mob 走重力时需水分流；resting mob 已 continue 免查。
        const bool mobAiTick = (e.kind == Mob)
                               && ((m_tickPhase + quint32(idx)) % quint32(kAiTickInterval)) == 0;
        if (e.resting) {
            if (!mobAiTick) continue; // 非 aiTick：信上次复探结果，保 resting，跳过重力 + mobInWater（省 blockAt）
            // t670 越障跳滑流着地兜底清（防某条路径漏清后 resting mob 持续漂移）。
            e.jumpGX = 0.0f;
            e.jumpGZ = 0.0f;
            // t828 鱿鱼浮力打破 resting：水中的鱿鱼有持续净上涌（下方重力分流 +kSquidBuoyancy），不应贴底
            //   静置——但支撑复探对「水底固体」恒真 → 无此打破则下方浮力分支永不可达（resting continue 先于
            //   重力），鱿鱼仍贴底。脚位格在水 → 翻 resting=false 走浮力上升（头出水面落回普通重力 bobbing）。
            // t923 泛化到全部陆栖 mob（浮面）：**口鼻线淹没**（t979 mobSnoutSubmergedInWater 单一权威——
            //   溺水/浮面/resting 破除三消费点同源；旧整格判在 1/8、1/4 浅水档误破 resting → 每 aiTick
            //   翻转振荡 dirty 抖动，新谓词浅水不破）→ 打破 resting 走下方泳浮分支升到水面（旧版仅鱿鱼有
            //   浮力，其余 mob 缓沉贴底 → 头长浸水 15s 溺亡 = 驯服狼困水淹死根因）。浅水跋涉（口鼻线在
            //   水面上）**不打破**——贴底站立继续走（无 resting 翻转振荡 / dirty 抖动）。鱿鱼保持 t828 原
            //   脚位判据（水生恒浮，包括浅水格）。
            if (e.kind == Mob) {
                const bool deepWater = (e.mobType == MobSquid)
                    ? (qFloor(e.pos.y() - e.halfH) >= 0
                       && world->blockAt(cx, qFloor(e.pos.y() - e.halfH), cz) == BlockRegistry::Water)
                    : mobSnoutSubmergedInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH);
                if (deepWater) {
                    e.resting = false;
                    dirty = true;
                }
            }
            if (e.resting) {
            // t970 坠落基准保鲜：贴地期间 fallPeakY 恒跟当前脚位（= 下一次腾空的起算点）。每 aiTick 重置
            //   （节流面同支撑复探）；防「陈旧高基准」伪摔伤——落差只从本次腾空起累计（滞空 max 刷新见
            //   下方重力段；落地沿结算消费后亦复位到落点）。
            e.fallPeakY = e.pos.y() - e.halfH;
            // aiTick：复探支撑。
            // t362 改「footprint 任一列有支撑」（旧版仅中心列 cx/cz）：mob 走下 1 格台阶时，中心先越过台阶沿、
            //   但后半 footprint 仍压在更高支撑块上。旧版即判失支撑 → 重力把整格 snap 下沉到低地 → 此时 trailing
            //   边仍压在高块列 → 落地后水平移动被 mobAabbHitsSolid 判 trailing 腿卡进身后高块 → 每帧撤回 →
            //   永久卡死（用户「下台阶卡住变活靶」）。改 footprint 后：只要还有任一列压在更高支撑上就保 resting，
            //   悬出台阶沿继续前行；直到 trailing 边也越过台阶沿（footprint 全离支撑）才下沉 → 落低地时 trailing
            //   已不在高块列 → 腿不卡、干净步下（机制等价 MC mob 越过台阶沿后才自动步下 1 格）。
            // perf FP-robust：restY = mobSolidY+1+halfH（落地时设）。pos.y - halfH 应恰为整数 mobSolidY+1，但
            //   halfH 非 2 的幂时（pig/sheep 0.45、敌对 0.9、spider 0.3）float 运算有 ~1 ULP 残差 → pos.y-halfH
            //   可能落在 mobSolidY+0.9999 → floor 取 mobSolidY → supportY=mobSolidY-1（支撑格下方一格）。
            //   厚地面下邻格也是实体 → 误判不暴露；但**薄地板**（1 格厚天花板 / 生成的结构顶）下邻格是空气 →
            //   误判失支撑 → resting 翻 false → 重力下落 1 帧 → 落回同位 resting=true → 下个 aiTick 又翻 false
            //   = 周期振荡（每 aiTick 一次重力 + dirty bump + emit，驱动 QML 全 delegate 绑定重算 = 持续卡顿源）。
            //   加 0.01f（>> 1 ULP ~1e-5、<< 1.0 格）把任何向下残差推回整数之上 → supportY 稳定 = mobSolidY。
            //   仅在 resting 复探生效（pos.y 已 snap 到 restY，feet 恒 ≈ 整数）；下落中 mob 不走此分支。
            // t629 薄层支撑复探：mob 站在 SnowLayer 顶（restY = cell+1/8+halfH，feet = cell+0.125 非整数）时
            //   旧「supportY = floor(feet+0.01)−1 = cell−1」查到薄层**下方**的空气格 → 判失支撑 → resting 翻
            //   false → 重力下落 1 帧 → 落回同位 resting=true → 周期振荡（每 aiTick 一次重力 + emit，卡顿源）。
            //   改「候选支撑层取 feet 所在格及其下一格」两格复探：任一层有支撑即保 resting（footprint 任一列）。
            //   两格覆盖：满格地面（feet 整数 → supportY 恒 cell，同旧公式）+ 薄层顶（feet 在层格内 → 层格自身
            //   命中）。SnowLayer solid=true（isSolid 非 air）→ mobFootprintHasSupport 原样命中，无需改谓词。
            const int feetCell = qFloor(e.pos.y() - e.halfH + 0.01f);
            if (mobFootprintHasSupport(world, e.pos.x(), e.pos.z(), feetCell - 1, e.halfW)
                || mobFootprintHasSupport(world, e.pos.x(), e.pos.z(), feetCell, e.halfW)) {
                // review D1-b 走入薄雪层的贴合：mob 从满格地面（feet=cell+1）走上积雪区（层真顶 cell+1/8）时，
                //   上面支撑复探命中保 resting（对），但旧版不再贴面 → 脚悬在层顶上方 ~0.9 格（t629 只修了下落
                //   路径的落地扫描，行走路径不走重力分支贴面）。同「下 1 格台阶」语义：resting 期间对**中心列
                //   支撑**（走下台阶时 footprint 还压着身后高块，取中心列 = mob 前脚方向）取 mobSupportTopY 真顶，
                //   当前脚位高于真顶超过容差（1e-3，防 ULP 抖动 / 满格地面恒等早退）→ 下贴真顶（等价自动步下
                //   1/8 级小台阶，机制同玩家 auto-step 的反向）。只下贴不上抬（支撑变高的情况由失支撑→重力→
                //   落地扫描路径处理，不在此分支）。footprint 后续列仍高于中心列时 mobAabbHitsSolid 已挡
                //   （台阶沿卡死由 t362 footprint 保 resting 覆盖 —— 本处仅移 Y 不移 XZ）。
                float supportTop = -1.0f;
                for (int cy = feetCell + 1; cy >= feetCell - 1; --cy) {
                    if (cy < 0) break;
                    const float top = mobSupportTopY(world, cx, cy, cz);
                    if (top >= 0.0f && top <= e.pos.y() - e.halfH + 0.01f) { supportTop = top; break; }
                    // cy 高于脚位的层（top > feet）不算当前支撑（mob 站进它下方 = 嵌入，由窒息 / 挤出兜底）
                }
                if (supportTop >= 0.0f) {
                    // t728 燃烬者悬浮：restY 加 kEmberlingHoverOffset 抬升（不断回退到贴地）。悬浮 mob 中心底
                    //   面距支撑面恒悬空 ~0.4 格（视觉，机制等价 MC 烈焰人飞浮）；上下 sin 浮动由 QML 动画驱动。
                    const float restY = supportTop + e.halfH
                                        + (e.mobType == MobEmberling ? kEmberlingHoverOffset : 0.0f);
                    if (restY < e.pos.y() - 1e-3f) { e.pos.setY(restY); dirty = true; }
                }
                continue; // 仍实体 → 保持静止
            }
            e.resting = false; // 支撑消失 → 续落（vy 已 0，从静止重新加速）
            dirty = true;
            }
        }

        // t298 水中浮力判定：仅 Mob kind（vestigial Item 不涉水物理）。脚位（AABB 底面）格 == Water → mobInWater。
        //   feetCellY = floor(pos.y − halfH)（mob 底面格；pos.y 是中心）。用于下方重力分流（缓沉 vs 自由落体）。
        //   仅非 resting mob 到此（resting 已 continue）→ 每帧仅对下落中的 mob 算（省 resting mob 的 1 blockAt/帧）。
        const int mobFeetY = qFloor(e.pos.y() - e.halfH);
        const bool mobInWater = (e.kind == Mob) && mobFeetY >= 0
                                 && world->blockAt(cx, mobFeetY, cz) == BlockRegistry::Water;

        // 重力 + 下移（vy 向下为负）。t298：mob 在水中 → 缓沉（kWaterGravity << kGravity）+ 钳最大下沉
        //   （kWaterSinkMax << kMaxFall，防加速穿水底）；机制等价玩家 t174 水中浮力（mobs 不按空格故无上浮，
        //   仅被动缓沉到水底 resting）。离水走原重力 + 终端下落。
        if (mobInWater) {
            if (e.kind == Mob && e.mobType == MobSquid) {
                // t828 水生浮力（spec「水生生物默认上浮不沉底」；机制等价 MC 1.0 squid 中性浮力）：鱿鱼水中
                //   重力反转为净浮力加速度（+kSquidBuoyancy，向上）+ 上浮速度上限钳制（kSquidRiseMax，防喷水
                //   脉冲叠加把鱿鱼顶出水面过高）→ 持续轻浮上涌；头出水面后 mobInWater=false 落回普通重力拉回
                //   → 在水面下小幅 bobbing 悬停（不再缓沉贴底爬行）。喷水脉冲（aiSquid vy=kSquidSwimUp）被
                //   钳制后仍保持「上涌快、回沉慢」的节律游动感。
                e.vy += kSquidBuoyancy * float(dt);
                if (e.vy > kSquidRiseMax) e.vy = kSquidRiseMax;
            } else if (mobSnoutSubmergedInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH)) {
                // t923 ② 陆栖 mob 主动浮面（口鼻线淹没 = t979 mobSnoutSubmergedInWater 单一权威——
                //   「会淹才游」与 t828 溺水判据同源同谓词，源块水行为与旧整格判等价、浅水流水档不再
                //   误触发空跳；机制等价 MC 1.0 生物水中游到水面）：净浮力上涌 + rise 钳制 → 口鼻出水后
                //   本分支不触发、落回下方缓沉 → 水面贴平 bobbing 悬停（呼吸恢复不溺亡 + 追击水平照常）。
                //   驯服狼困水淹死链（缓沉贴底 → 口鼻浸 15s → 1HP/s → 死）由此断开；上岸最后一步由
                //   aiWolf chase 泳跃（vy=kJumpSpeed 直设）承接——钳制只作用于**浮力累积**（vy < riseMax
                //   才加），泳跃的高速 vy 原样穿过（否则跃出速度被夹回 1.2 = 跃不出水面）。
                if (e.vy < kMobSwimRiseMax)
                    e.vy = std::min(e.vy + kMobSwimBuoyancy * float(dt), kMobSwimRiseMax);
            } else {
                // t298 浅水跋涉缓沉（头未没水：仅脚位浸水）：贴底站立继续走（原行为，无浮面振荡）。
                e.vy -= kWaterGravity * float(dt);
                if (e.vy < -kWaterSinkMax) e.vy = -kWaterSinkMax;
            }
        } else {
            e.vy -= kGravity * float(dt);
            if (e.vy < -kMaxFall) e.vy = -kMaxFall;
        }
        const float mobNewY = e.pos.y() + e.vy * float(dt);

        // t970 滞空坠落基准：每帧 max 刷新历史最高脚位（上升段累计弧顶；落地沿据此结算摔伤，见落地分支）。
        //   取积分前的当前位（弧顶由逐帧离散采样近似，粒度 vy·dt ≤ 1 格级阈值下可忽略）。
        const float fallFeetNow = e.pos.y() - e.halfH;
        if (fallFeetNow > e.fallPeakY) e.fallPeakY = fallFeetNow;

        // review25 #4 上浮天花板碰撞（vy>0 向上分支——垂直积分段原只为下落设计）：t828 持续浮力
        //   （鱿鱼 kSquidBuoyancy）使 vy 恒正，自由上移分支（下方 else 分支）原无任何向上阻挡检查 →
        //   头顶穿入固体格（冰面 / 封顶水池）后**中心**落入固体格的那一帧，落地扫描 mobSupportTopY
        //   命中该格、restY=格顶+halfH 高于当前位置 → mobNewY<=restY 成立把整段 setY(restY) 抬到方块
        //   顶上（穿顶 / snap 上顶，冰湖场景必现）。对称修：vy>0 时对头顶格（头位 = 中心+halfH，自
        //   floor(pos.y+halfH) 扫到 floor(mobNewY+halfH)，与落地扫描同用中心列 cx,cz）取碰撞 sub-AABB
        //   的**最低盒底**为天花板面；头顶将穿入 → 贴顶悬停（钳 pos.y=ceilBottom-halfH、vy=0，浮力
        //   下帧再积再钳 = 稳定贴顶）。口径与落定分支对称复用 collisionAABBsAt（ShapeNone 无盒族——
        //   草丛/火/水/门面——天然穿过，薄盒/异形盒按真形阻挡）。钳住后跳过下方落定扫描（本帧位移
        //   已定，扫描的 restY 判定对向上钳制无意义）。
        bool ceilingClamped = false;
        if (e.vy > 0.0f) {
            const float headTopNew = mobNewY + e.halfH;
            const int headCellFrom = qFloor(e.pos.y() + e.halfH);
            const int headCellTo = qFloor(headTopNew);
            float ceilBottom = -1.0f;
            // t859：out-param 栈上小缓冲（零堆分配；mob 上浮天花板钳制每 mob 每帧查）。
            BlockRegistry::BlockAABB boxes[BlockRegistry::kMaxAABBsPerCell];
            for (int cy = headCellFrom; cy <= headCellTo && ceilBottom < 0.0f; ++cy) {
                if (cy < 0) continue;
                const int n = world->collisionAABBsAt(cx, cy, cz, boxes, BlockRegistry::kMaxAABBsPerCell);
                for (int i = 0; i < n; ++i) {
                    if (ceilBottom < 0.0f || boxes[i].minY < ceilBottom) ceilBottom = boxes[i].minY;
                }
            }
            if (ceilBottom >= 0.0f && headTopNew > ceilBottom) {
                const float clampY = ceilBottom - e.halfH;
                if (e.pos.y() != clampY) { e.pos.setY(clampY); dirty = true; }
                if (e.vy != 0.0f) { e.vy = 0.0f; dirty = true; }
                ceilingClamped = true;
            }
        }

        // 下移路径自顶向下扫实体所在列，找首个实体方块（防大 dt 穿过薄层；lessons「子步防穿墙」精神）。
        const int mobTopCell = qFloor(e.pos.y()); // 当前中心所在格（一般为空气）
        int mobBotCell = qFloor(mobNewY);
        if (mobBotCell > mobTopCell) mobBotCell = mobTopCell; // 防浮点噪声
        // t629：扫列改为记「首个可支撑列的**真顶面 Y**」（mobSupportTopY）——SnowLayer 按 snowLayerHeight
        //   取薄层真顶（1/8..1.0）、完整方块取 cell+1、水/air 返 -1 跳过（t333 水穿透语义不变）。修「mob 落在
        //   1/8 薄雪层上被整格顶起悬空一格格」（用户报雪傀儡「卡在空中悬浮在积雪层上一格」）：旧 mobSolidY+1
        //   恒按满格顶承接，落在薄层上中心被抬到层顶+halfH ≈ 层上方 0.9 格 = 视觉悬空。restTopY<0 = 本段
        //   无支撑（含只扫到水）→ 自由下落（原 mobSolidY<0 路径）。
        if (!ceilingClamped) {
        float restTopY = -1.0f;
        int restCellY = -1; // t1045 落点支撑格 Y（踩踏耕地判读用；-1 = 无支撑）
        for (int cy = mobTopCell; cy >= mobBotCell; --cy) {
            if (cy < 0) break; // 越界下方=空气（World 约定）→ 不视作地面，实体继续落
            const float top = mobSupportTopY(world, cx, cy, cz);
            if (top >= 0.0f) { restTopY = top; restCellY = cy; break; }
        }

        if (restTopY >= 0.0f) {
            // 落地：贴支撑面真顶 + 静止偏移（底面 = restTopY = 支撑顶；中心 = 顶 + halfH）。
            //   t252：kRestOffset → e.halfH（per-mob 半高；cow halfH=0.70 → 比 1×1 高 0.2，固定 0.5 无法表达）。
            //   t728 燃烬者：+kEmberlingHoverOffset 抬升 → 落地也停悬浮高度（不贴地，机制等价 MC 烈焰人飞浮）。
            const float restY = restTopY + e.halfH
                                + (e.mobType == MobEmberling ? kEmberlingHoverOffset : 0.0f);
            if (mobNewY <= restY || e.vy < 0.0f) {
                if (e.pos.y() != restY) { e.pos.setY(restY); dirty = true; }
                if (e.vy != 0.0f) { e.vy = 0.0f; dirty = true; }
                e.resting = true;
                e.jumpGX = 0.0f; // t670 越障跳滑流着地即停（防落地后继续漂移 / 推入墙）
                e.jumpGZ = 0.0f;
                // t970 mob 落地摔伤结算（机制等价 MC 1.0 生物摔落；玩家同式见 PlayerController t22 段）：
                //   落差 = 滞空最高脚位 − 落点脚位（restTopY；峰值/落点同为 pos.y−halfH 脚位口径，
                //   Emberling 悬浮偏移两端一致抵消）。① 拉拽一次性豁免**无条件消费**（t690 着地沿无条件
                //   清窗同型：伤害判定读消费前值；钓竿拉拽抛物线的落地由此免摔，豁免不跨坠落存活）；
                //   ② 落点脚位格 Water → 水缓冲豁免（机制等价玩家 t200；判定走下方 t298 mobFeetInWater
                //   单一权威——浸没沉底时脚位格即水，同 mobSupportTopY「水非支撑」的沉底口径）；
                //   ③ fall > kMobFallSafeBlocks → dmg = floor(fall − 阈值)，damageEntity 既有受击链承载
                //   （红闪 + 归零 mobDied 掉落）。基准/豁免在结算前就地复位（下一程腾空从落点起算；
                //   damageEntity 会 emit → 容器引用写入必须先于该调用，悬垂窗口纪律）。
                const float fallDist = e.fallPeakY - restTopY; // 落差（格）
                const bool pullExempt = e.fallExemptOnce;      // t970 拉拽一次性豁免（先取值后清）
                e.fallExemptOnce = false;                      // 着地沿无条件消费
                e.fallPeakY = restTopY;                        // 基准复位到落点
                // 水缓冲判定走 t298 既有单一权威（pos 已 snap 到 restY → 脚位格 = floor(restTopY)）。
                const bool feetInWater = mobFeetInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH);
                if (!pullExempt && !feetInWater && fallDist > kMobFallSafeBlocks) {
                    damageEntity(idx, int(std::floor(fallDist - kMobFallSafeBlocks)));
                    dirty = true;
                }
                // t1045 mob 踩踏耕地（parity 裁-3：MC onFallenUpon 对一切实体生效——mobGriefing 缺省
                //   true 且工程无 gamerule 系统 → 全 mob 通行，登记；Java modern 的 0.512 尺寸豁免门不
                //   入（Beta/1.0 基准无此门），登记）。落点支撑格 Farmland → World::farmlandTrampleRoll
                //   单一掷骰（与玩家同公式同缝；概率地板 fall ≤ 0.5 恒不踩——resting 期间不走本分支、
                //   行走并入小落差自然短路）。命中 → setBlockSilent 回 Dirt（湿润 state 随 id 消失）+
                //   发 farmlandTrampledByMob → PlayerController 收事件清苗 + dropCropDrops 单一权威弹落
                //   （掉落表不出 Entities 层，分层口径）。
                if (restCellY >= 0
                    && world->blockAt(cx, restCellY, cz) == BlockRegistry::Farmland
                    && world->farmlandTrampleRoll(fallDist)) {
                    world->setBlockSilent(cx, restCellY, cz, BlockRegistry::Dirt, 0);
                    emit farmlandTrampledByMob(cx, restCellY, cz);
                }
            }
        } else if (mobNewY != e.pos.y()) {
            e.pos.setY(mobNewY); // 自由下落（无命中）
            dirty = true;
        }
        } // /!ceilingClamped（review25 #4：上浮贴顶帧跳过落定扫描——mobNewY 已被钳制替代）

        // t239 void-loss 兜底：Mob 跌出世界底部（pos.y<0，如被推/走离边界外无支撑）→ 标记移除（防永久下落）。
        if (e.kind == Mob && e.pos.y() < 0.0f) { toRemove.push_back(idx); dirty = true; }
    }
    // t905 mob 细分：实体循环结束即结清最后一段（releaseSlot 起为独立 loopTail 段 —— emit entitiesChanged
    //   的 QML delegate 扇出（47 槽 × ~12 revision 绑定 + 行走 mob 的 MobModel 几何重建）与逐实体物理是
    //   两个不同成本中心，混在一个 tail 桶会互相掩盖）。
    settleSeg();
    const qint64 loopTailT0 = FrameProfiler::nowNs();

    // t256：移除的实体（着地 / 跌出的 FallingBlock + deathTimer 到 / void-loss 的 Mob）改 releaseSlot（标
    //   alive=false + 入 free list）替代 erase-shift —— 保 m_entities.size()（=count 属性 = QML Repeater
    //   model）单调不降 → Repeater 不需销毁 reparent 的 3D delegate → 消除掉落沙 spawn/land 抖动致 delegate
    //   泄漏。release 不 shift 索引，顺序无关（逆序仅为保留与旧 erase 路径一致的可读性）。
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        releaseSlot(*it);

    // 审查修 B8（t724-t729 复盘）：主循环外统一生成 AI 段 pending 的火球 / 箭（aiEmberling / aiArcher 记入；
    //   循环内直接 spawn 会因 acquireSlot push_back 使 Entity& 悬空 —— 此处不再持引用，逐项重取安全）。
    //   在 toRemove 释放之后 → 本帧已判死的发射者不出手（flush 内再校验 alive/serial 双保险）。
    flushPendingShots();

    // t400 繁殖 tick（主实体循环之外 —— 幼崽生成 acquireSlot 可能 push_back，主循环持 Entity& 引用期间不可 push_back
    //   致其失效）：衰减求偶 / 冷却 / 幼崽长大计时 + 求偶配对产幼崽（受 kPassiveMobCap 钳制）。dirty 合入本 tick
    //   末尾统一一次 bump + emit（批量收口，避免 N 幼崽 N 次 notify 风暴，同 t320/t354 纪律）。
    if (tickBreeding(dt)) dirty = true;

    // t952 mob-on-mob 挂载 pass（小鸡骑士）：骑手 AI 消费 + 双向对账 + 骑手钉载具顶。主循环之外（骑手
    //   AI 经由冻结分支累积的 aiAccum 在此消费；对账可能读多槽一致状态）。变更合入本 tick 末尾统一 emit。
    if (tickMobMounts(world, listener, worldW, worldD, playerTargetable, skyBrightness)) dirty = true;

    // perf：节流 entitiesChanged emit。mob 每帧 wander/gravity 致 dirty 几乎每帧 → 旧版每帧 ++revision+emit 触发
    //   全体 delegate（count × ~12 revision 绑定）NOTIFY 激活 + 行走 mob 的 MobModel 全几何 rebuild+GPU 重上传
    //   = mob 卡顿主因（用户实测 mob 22ms 恒定 + ~65ms QML，与视距无关；前几轮 AI/blockAt 节流无效因瓶颈在此）。
    //   改：dirty/toRemove 只置 m_pendingEmit；每 kEmitEveryN 帧（~20Hz）才 notify 一次 → NOTIFY 激活 +
    //   MobModel 重建频率降 3×。mob 位置/腿/外观 20Hz 刷新（缓慢生物视觉够），spawn/despawn ≤3 帧延迟。配合
    //   MobModel::setWalkPhase 量化（腿姿 12 步/cycle），双重削减每帧 mob 渲染开销。m_pendingEmit 持续脏确保不丢更新。
    //   t935 二段收口：节流把 emit 从 60Hz 降到 ~20Hz，但**单次 emit 仍激活全部 N 槽 delegate**（用户实测 ltail
    //   10.59ms：TNT 炸沙坑后槽高水位 47，空槽 / 静置 mob 的绑定重求值全是白算）。notify → refreshSlotMonitors
    //   槽位指纹差分只 bump 可见态真变的槽 → 扇出 =「bump 槽 × ~50 绑定」而非「count 槽 × ~50」（量化读数 =
    //   F3 mob 行 emit/bump/fan 三计数）。dirty 含非可见字段翻转（resting 等）时 notify 照发但零 bump（指纹
    //   差分吸收），成本退化为全槽指纹扫描（微秒级）。
    if (dirty || !toRemove.empty()) m_pendingEmit = true;
    if (m_pendingEmit && (m_tickPhase % quint32(kEmitEveryN) == 0)) {
        m_pendingEmit = false;
        notifyEntitiesChanged();
    }
    // t905 mob 细分：三桶 + loopTail（releaseSlot / flushPendingShots / tickBreeding / emit 扇出）入账
    //   （mobHead / mobAI / mobTail / mobLoopTail 在 FrameProfiler report 的 mob 行细分展示）。
    FrameProfiler::instance()->add("mobHead", headNs);
    FrameProfiler::instance()->add("mobTail", tailNs);
    FrameProfiler::instance()->add("mobLoopTail", FrameProfiler::nowNs() - loopTailT0);
    // t500 perf mob 子桶：mobAI 累入（mobPhys = mobLoop − mobAI 在 report 派生）。
    FrameProfiler::instance()->add("mobAI", aiNs);
}
