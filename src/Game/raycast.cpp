#include "raycast.h"
#include "world.h"
#include "blockregistry.h" // Torch / Water / Ladder 是否挡射线由 filter 决定；t213 isFullCube / raycastAABBs 做
                           // 命中点 vs sub-AABB 精确测试（不完整方块/火把/木梯空气部分穿过命中后方块）。

#include <algorithm> // std::min / std::swap
#include <cmath>
#include <limits>
#include <vector>

// Amanatides & Woo「快速体素遍历」：每步跳到下一个体素边界（沿主导轴恰好 1 格），
// 故步长 ≤ 1 格（满足 dev-spec t04 要求）。命中面法线 = 进入该体素时所跨轴的 −step。
//
// 参考：A Fast Voxel Traversal Algorithm for Ray Tracing (1987)。整数格坐标按
// World 约定（+Y 朝上，越界=空气）；阻挡谓词见 blocksRay（按 filter 切换 Torch / Water）。
//
// 同一份 DDA 被多种语义复用（选体 / 相机距离 / 铁桶舀水），它们对「Torch / Water / Ladder / 不完整方块
// 是否挡射线」需求不同，故用 filter 标志位独立切换（详见 raycast.h RayFilter 注释）：
//   - 选体（HitTorch|HitLadder）：火把 / 木梯挡（t184/t501，可选中 / 直挖）、水穿过（t165 水下可挖实体）。
//   - 相机距离（HitPartial，t605）：不完整方块按碰撞 sub-AABB 精确命中；火把 / 水 / 木梯穿过（皆 non-solid，
//     相机不应被拉近视距，保 t40）。起点在不完整方块格的**空气部分**（如 1.5 格通道天花板上半砖的下半格）
//     时穿过继续命中后方实体（否则退化返 invalid → 相机当「无墙」取满距 kCamMax 直穿，见下方起点分流注释）。
//   - 铁桶（HitWater）：水挡（命中首个水格舀水）、火把 / 木梯穿过。
namespace {
// t213 射线 vs cell-local sub-AABB（世界坐标 = cell + local）精确命中测试。
//   选体射线进入含不完整方块 / 火把的体素后，须命中其中某个 sub-AABB 才算选中（空气部分穿过命中后方块）。
//   slab 法逐轴求交：每轴算「过 min 面参数 t1 / 过 max 面参数 t2」，进 = min(t1,t2)、出 = max(t1,t2)；
//   三轴取**进的 max** = 实际进入 t（最后碰的面 = 进面），其轴 + 法线符号（过 min 面→法线 −轴、过 max 面→+轴）
//   即命中面外法线。dir 须已归一化（caller normalize）。
//
//   tEnterCell / tExitCell：射线在当前体素内的参数段（命中 t 须落此段内）。sub-AABB 先 clamp 到 [0,1]^3
//   cell-local 再测——超格部分（如栅栏立柱 maxY=1.5 探入上格 0.5）不属本格段，clamp 后天然只测本格内部分。
//
//   cellNx/Ny/Nz：射线进入此格时的 cell-entry 面法线（DDA 给），作**起点嵌 sub-AABB** 时的兜底法线
//   （slab 算出 tmin<0 表示起点在盒内 → 命中点 = tEnterCell，法线退化用进格面；此情形仅起点格可能发生）。
//
//   多 sub-AABB（如 stairs 下步+背墙）取最近命中。无命中 → false。命中 → outT（沿归一 dir 欧氏距离）+
//   outNx/Ny/Nz（命中面外法线，±1 单轴；起点嵌盒退化时 = cellEntryNormal）。
bool rayHitBoxes(const QVector3D &origin, const QVector3D &dir,
                 int cx, int cy, int cz,
                 const std::vector<BlockRegistry::BlockAABB> &boxes,
                 float tEnterCell, float tExitCell,
                 int cellNx, int cellNy, int cellNz,
                 float &outT, int &outNx, int &outNy, int &outNz)
{
    const float inf = std::numeric_limits<float>::infinity();
    const float eps = 1e-20f; // 方向分量「平行」阈值（dir 归一 → 分量域 [-1,1]）
    float bestT = inf;
    int bnX = cellNx, bnY = cellNy, bnZ = cellNz; // 兜底法线（起点嵌盒时用）
    bool found = false;
    for (const BlockRegistry::BlockAABB &bb : boxes) {
        // clamp 到 [0,1]^3 cell-local（超格部分不属本格段，如栅栏 1.5 高 → 本格内只到 1.0）。
        const float mnx = std::max(0.f, bb.minX), mxx = std::min(1.f, bb.maxX);
        const float mny = std::max(0.f, bb.minY), mxy = std::min(1.f, bb.maxY);
        const float mnz = std::max(0.f, bb.minZ), mxz = std::min(1.f, bb.maxZ);
        if (mnx >= mxx || mny >= mxy || mnz >= mxz) continue; // 零体积 / 退化盒
        // 世界坐标 AABB（cell + local）。
        const float ax0 = cx + mnx, ax1 = cx + mxx;
        const float ay0 = cy + mny, ay1 = cy + mxy;
        const float az0 = cz + mnz, az1 = cz + mxz;
        float tmin = -inf, tmax = inf; // 此盒相交段 [tmin, tmax]
        int nX = 0, nY = 0, nZ = 0;    // 进面法线（最后定 tmin 那轴的符号）

        // X 轴
        if (std::fabs(dir.x()) < eps) {
            if (origin.x() < ax0 || origin.x() > ax1) continue; // 平行且在外 → 此盒不中
        } else {
            const float inv = 1.f / dir.x();
            float t1 = (ax0 - origin.x()) * inv; // 过 min 面参数
            float t2 = (ax1 - origin.x()) * inv; // 过 max 面参数
            int s1 = -1, s2 = +1;                // t1=min面(法线−X) / t2=max面(法线+X)
            if (t1 > t2) { std::swap(t1, t2); std::swap(s1, s2); } // 进=min(t1,t2)，法线取对应符号
            if (t1 > tmin) { tmin = t1; nX = s1; nY = 0; nZ = 0; } // 此轴成为「最后进的轴」→ 进面在此轴
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) continue; // 三轴投影无重叠 → 此盒不中
        }
        // Y 轴
        if (std::fabs(dir.y()) < eps) {
            if (origin.y() < ay0 || origin.y() > ay1) continue;
        } else {
            const float inv = 1.f / dir.y();
            float t1 = (ay0 - origin.y()) * inv, t2 = (ay1 - origin.y()) * inv;
            int s1 = -1, s2 = +1;
            if (t1 > t2) { std::swap(t1, t2); std::swap(s1, s2); }
            if (t1 > tmin) { tmin = t1; nX = 0; nY = s1; nZ = 0; }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) continue;
        }
        // Z 轴
        if (std::fabs(dir.z()) < eps) {
            if (origin.z() < az0 || origin.z() > az1) continue;
        } else {
            const float inv = 1.f / dir.z();
            float t1 = (az0 - origin.z()) * inv, t2 = (az1 - origin.z()) * inv;
            int s1 = -1, s2 = +1;
            if (t1 > t2) { std::swap(t1, t2); std::swap(s1, s2); }
            if (t1 > tmin) { tmin = t1; nX = 0; nY = 0; nZ = s1; }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) continue;
        }
        // [tmin, tmax] = 射线与此 sub-AABB 的相交段。限定到当前体素段 [tEnterCell, tExitCell]。
        if (tmin > tExitCell || tmax < tEnterCell) continue; // 此盒不在此格段（超格部分已 clamp，一般不触发）
        float thit = tmin;
        int hx = nX, hy = nY, hz = nZ;
        if (thit < tEnterCell) {
            // 起点已在此 sub-AABB 内（slab tmin < 进格点；仅起点格可能）→ 命中点 = 进格点，法线用进格面。
            thit = tEnterCell;
            hx = cellNx; hy = cellNy; hz = cellNz;
        }
        if (thit < bestT) { bestT = thit; bnX = hx; bnY = hy; bnZ = hz; found = true; }
    }
    if (found) { outT = bestT; outNx = bnX; outNy = bnY; outNz = bnZ; }
    return found;
}
} // namespace

RayHit raycastVoxel(const World &world, QVector3D origin, QVector3D dir, float maxDist, unsigned filter)
{
    RayHit h;
    if (dir.lengthSquared() < 1e-12f)
        return h; // 零向量：无方向，不命中
    dir.normalize();

    // 射线「阻挡」谓词：空气恒穿过；实体方块（非 Torch / Water / Ladder）恒挡；Torch / Water / Ladder 由 filter 决定。
    //   不复用 World::isSolid（语义=blockAt!=0 会把 Torch / Water 当 solid；且选体要 Torch 挡、相机要
    //   Torch 穿，需按 filter 区分），改读 blockAt + filter 显式判定（单一权威 + 模式可切换）。
    auto blocksRay = [&world, filter](int cx, int cy, int cz) {
        const quint8 b = world.blockAt(cx, cy, cz);
        if (b == quint8(0)) return false; // 空气：永远穿过
        // Torch / Water / Lava / Ladder 是否挡射线由 filter 决定（不同射线模式语义不同，见 RayFilter 注释）。
        if (b == BlockRegistry::Torch && !(filter & RayFilter::HitTorch)) return false;
        if (b == BlockRegistry::Water && !(filter & RayFilter::HitWater)) return false;
        if (b == BlockRegistry::Lava  && !(filter & RayFilter::HitLava))  return false; // t343 岩浆（铁桶舀）
        if (b == BlockRegistry::Ladder && !(filter & RayFilter::HitLadder)) return false; // t501 木梯（选体可拆）
        return true;
    };

    // 当前所在体素（floor 对负坐标亦正确：-2.3 ∈ 体素 -3）
    int x = int(std::floor(origin.x()));
    int y = int(std::floor(origin.y()));
    int z = int(std::floor(origin.z()));

    const int stepX = (dir.x() > 0.f) ? 1 : (dir.x() < 0.f ? -1 : 0);
    const int stepY = (dir.y() > 0.f) ? 1 : (dir.y() < 0.f ? -1 : 0);
    const int stepZ = (dir.z() > 0.f) ? 1 : (dir.z() < 0.f ? -1 : 0);

    const float inf = std::numeric_limits<float>::infinity();
    // tMax：沿射线到下一个体素边界的参数距离；tDelta：穿越一格的参数增量。
    // 方向分量为 0 的轴永不被选中（tMax=tDelta=∞）。
    auto initTMax = [](float o, float d, int s) -> float {
        if (s == 0) return std::numeric_limits<float>::infinity();
        const float boundary = (s > 0) ? float(std::floor(o) + 1) : float(std::floor(o));
        return (boundary - o) / d;
    };
    float tMaxX = initTMax(origin.x(), dir.x(), stepX);
    float tMaxY = initTMax(origin.y(), dir.y(), stepY);
    float tMaxZ = initTMax(origin.z(), dir.z(), stepZ);
    const float tDeltaX = (stepX != 0) ? std::fabs(1.0f / dir.x()) : inf;
    const float tDeltaY = (stepY != 0) ? std::fabs(1.0f / dir.y()) : inf;
    const float tDeltaZ = (stepZ != 0) ? std::fabs(1.0f / dir.z()) : inf;

    // t213 命中决策：射线进入「该 filter 视为阻挡」的格后，是否选中 + 命中点/法线。
    //   - 完整立方（isFullCube）/ 水 → 整格命中（射线进格即中，等同旧行为；水经 HitWater 命中整格舀水）。
    //   - 不完整方块 / 火把 + **选体模式**（HitTorch）/ 木梯 + 选体模式（HitLadder）→ 命中点 vs sub-AABB 精确测试
    //     （空气部分穿过命中后方块）。非选体模式（相机 Default / 桶 HitWater / HitLava）对不完整方块维持整格阻挡
    //     （旧行为，防相机穿半砖 / 桶射线行为变）。
    //   hitFull=true 时用 DDA 进格面法线 + 进格 t（cellEntryN 全格命中）；否则 sub-AABB 命中（法线由 slab 算）。
    //   返回 true=已命中（h 已填，caller return）；false=此格空气部分穿过（caller 继续 DDA 步进）。
    auto tryHitCell = [&](int cx, int cy, int cz, float tEnterCell, float tExitCell,
                          int cellNx, int cellNy, int cellNz) -> bool {
        const quint8 b = world.blockAt(cx, cy, cz);
        // 选体模式 = 命中方块的精确 sub-AABB 测试生效（火把 / 木梯等 non-solid 方块的「准星完全落在视觉面才命中」
        //   语义）：HitTorch 覆盖火把、HitLadder 覆盖木梯。二者均可经 updateRaycast 同时启用。
        // t605 相机模式（HitPartial）：**所有不完整方块**（半砖 / 雪层 / 压力板 / 栅栏 / 楼梯 / 门 / 活版门…）走
        //   sub-AABB 精确测试 —— 相机距离以「实体 sub-AABB 面」为准（与玩家碰撞 collisionAABBs 同源 shapeBoxes）：
        //   1.5 格通道（下半砖地 + 上半砖顶）蹲行时相机沿偏移方向的空隙恰是 0.5+1.5+0.5=2.5 格薄缝，整格阻挡会把
        //   相机无谓钳近（半砖格的空气半格相机实际可通过）；Torch/Water/Ladder 无对应 Hit* 标志仍先穿（non-solid
        //   不拉近视距，保 t40）。
        const bool preciseMode = (filter & (RayFilter::HitTorch | RayFilter::HitLadder
                                            | RayFilter::HitPartial)) != 0;
        // 完整立方 → 整格命中；水（仅 HitWater 模式进此分支）→ 整格命中舀水；岩浆（仅 HitLava 模式）→ 整格命中舀岩浆；
        //   非选体·非相机模式（桶 HitWater / HitLava）对不完整方块亦整格阻挡（旧行为，防桶射线行为变）。
        const bool fullCell = BlockRegistry::isFullCube(b) || b == BlockRegistry::Water
                              || b == BlockRegistry::Lava || !preciseMode;
        if (fullCell) {
            h.valid = true;
            h.bx = cx; h.by = cy; h.bz = cz;
            h.nx = float(cellNx); h.ny = float(cellNy); h.nz = float(cellNz);
            h.dist = tEnterCell; // DDA 进格面距起点欧氏距离（dir 已归一 → t 即距离）；t40 相机钳制复用
            return true;
        }
        // 选体 / 相机（t605）模式 + 不完整方块 / 火把 / 木梯 → 命中点 vs sub-AABB 精确测试。
        const std::vector<BlockRegistry::BlockAABB> boxes =
            BlockRegistry::raycastAABBs(b, world.stateAt(cx, cy, cz));
        float subT; int snx, sny, snz;
        if (rayHitBoxes(origin, dir, cx, cy, cz, boxes, tEnterCell, tExitCell,
                        cellNx, cellNy, cellNz, subT, snx, sny, snz)) {
            if (subT > maxDist) return false; // 命中点已超射程 → 视为未命中（caller 继续 → 下一格 t>maxDist 退出）
            h.valid = true;
            h.bx = cx; h.by = cy; h.bz = cz;
            h.nx = float(snx); h.ny = float(sny); h.nz = float(snz);
            h.dist = subT;
            return true;
        }
        return false; // 射线穿过此格空气部分（未中任何 sub-AABB）→ 继续 DDA
    };

    // 起点已嵌在「该模式视为阻挡」的格内 → 退化判定（相机穿模进实体方块，避免无意义高亮）。
    //   t174 例外：HitWater 模式下起点在水格属正常（玩家在水中游泳），视该水格为首个命中 ——
    //   桶舀「身处水」（眼位在水时含水射线起点即水格，否则判退化会漏掉水下舀水）。无外法线
    //   （射线未跨面进入此格，nx/ny/nz 保持 0；桶舀水只读格坐标不读法线，无碍）。
    //   t213：选体模式下起点在**不完整方块 / 火把 / 木梯**格内 → 不一刀切退化：眼位在空气部分（sub-AABB 外）时
    //   射线应继续命中后方方块（修「贴脸火把/半砖挡选后方块」）；仅眼位在 sub-AABB 内（真嵌入实体）才退化。
    //   完整立方 / 非选体·非相机模式沿用旧「起点嵌阻挡格 → 退化」语义。
    //   t605：**相机距离（HitPartial）同样分流**——1.5 格通道蹲行时眼位落在天花板（上半砖/雪层等 sub-AABB 方块）
    //   格的空气部分，旧版在此退化返 invalid → updateCameraDistance 把 invalid 当「无墙」取满距 3.5 → 相机
    //   穿墙查看（用户 bug：恰只在 1.5 格通道触发，因仅此场景眼位才会位于 sub-AABB 方块格的空气段）。眼位在
    //   空气部分 → 穿过继续命中后方实体 / 命中本格 sub-AABB；仅眼位真嵌 sub-AABB（相机已在实体内，无处可退）
    //   才退化。桶模式（HitWater/HitLava）已在上方整格分支返回，不进此路径，行为不变。
    if (blocksRay(x, y, z)) {
        if ((filter & RayFilter::HitWater) && world.blockAt(x, y, z) == BlockRegistry::Water) {
            h.valid = true;
            h.bx = x; h.by = y; h.bz = z;
            return h; // 起点即水格 → 命中该格（dist=0；法线 0）
        }
        // t343：HitLava 模式下起点在岩浆格属正常（玩家在岩浆中），视该岩浆格为首个命中（同 HitWater 水下舀水）。
        if ((filter & RayFilter::HitLava) && world.blockAt(x, y, z) == BlockRegistry::Lava) {
            h.valid = true;
            h.bx = x; h.by = y; h.bz = z;
            return h; // 起点即岩浆格 → 命中该格（dist=0；法线 0）
        }
        const quint8 startB = world.blockAt(x, y, z);
        // t605：起点格「不完整方块（sub-AABB 实体 + 空气部分）」的分流扩到相机模式（HitPartial）：
        //   选体（HitTorch|HitLadder，t213 贴脸半砖/火把）与相机（HitPartial，t605 1.5 格通道天花板半砖）都须
        //   眼位在空气部分 → 继续命中（本格 sub-AABB 或后方实体）；仅真嵌入 sub-AABB 才退化。
        const bool startPartial = (filter & (RayFilter::HitTorch | RayFilter::HitLadder | RayFilter::HitPartial)) != 0
                                  && !BlockRegistry::isFullCube(startB)
                                  && startB != BlockRegistry::Water
                                  && startB != BlockRegistry::Lava;
        if (!startPartial)
            return h; // 完整立方 / 非选体·非相机模式起点嵌阻挡格 → 退化（相机穿模 / 贴脸火把旧语义）
        // 选体 / 相机（t605）模式 + 起点在不完整方块/火把/木梯格：sub-AABB 测试（段 = 起点格 [0, tExitStartCell]）。
        const float tExitStart = std::min(std::min(tMaxX, tMaxY), tMaxZ);
        const std::vector<BlockRegistry::BlockAABB> boxes =
            BlockRegistry::raycastAABBs(startB, world.stateAt(x, y, z));
        float subT; int snx, sny, snz;
        if (rayHitBoxes(origin, dir, x, y, z, boxes, 0.f, tExitStart, 0, 0, 0, subT, snx, sny, snz)) {
            if (snx == 0 && sny == 0 && snz == 0)
                return h; // 眼位在 sub-AABB 内（真嵌入实体）→ 退化（无意义高亮）
            // 眼位在空气部分、射线在此格内命中 sub-AABB（如眼在下半砖格上半空气、低头瞄到下半砖）→ 合法命中。
            if (subT <= maxDist) {
                h.valid = true;
                h.bx = x; h.by = y; h.bz = z;
                h.nx = float(snx); h.ny = float(sny); h.nz = float(snz);
                h.dist = subT;
                return h;
            }
        }
        // 眼位在空气部分且此格内未中 sub-AABB → 射线穿出起点格继续 DDA（不退化）。
    }

    float t = 0.0f;
    while (t <= maxDist) {
        // 选 tMax 最小的轴推进，记录所跨轴 → 命中面法线 = −step（射线从该面进入新体素）
        int nx = 0, ny = 0, nz = 0;
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) { x += stepX; t = tMaxX; tMaxX += tDeltaX; nx = -stepX; }
            else               { z += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; nz = -stepZ; }
        } else {
            if (tMaxY < tMaxZ) { y += stepY; t = tMaxY; tMaxY += tDeltaY; ny = -stepY; }
            else               { z += stepZ; t = tMaxZ; tMaxZ += tDeltaZ; nz = -stepZ; }
        }
        if (t > maxDist)
            break; // 下一格已在射程外
        if (blocksRay(x, y, z)) {
            // t213：当前体素段 [t, tExitCell]（tExitCell = 下一个格边界 = min(tMax...)，已推进到下一边界）。
            const float tExitCell = std::min(std::min(tMaxX, tMaxY), tMaxZ);
            if (tryHitCell(x, y, z, t, tExitCell, nx, ny, nz))
                return h;
            // 此格空气部分穿过 → 继续 DDA 步进（不 return）。
        }
    }
    return h; // 射程内全为空气 / 被该模式视为穿过的方块
}
