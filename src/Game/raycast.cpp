#include "raycast.h"
#include "world.h"
#include "blockregistry.h" // Torch / Water 是否挡射线由 filter 决定（见 RayFilter；BlockRegistry 取 id）

#include <cmath>
#include <limits>

// Amanatides & Woo「快速体素遍历」：每步跳到下一个体素边界（沿主导轴恰好 1 格），
// 故步长 ≤ 1 格（满足 dev-spec t04 要求）。命中面法线 = 进入该体素时所跨轴的 −step。
//
// 参考：A Fast Voxel Traversal Algorithm for Ray Tracing (1987)。整数格坐标按
// World 约定（+Y 朝上，越界=空气）；阻挡谓词见 blocksRay（按 filter 切换 Torch / Water）。
//
// 同一份 DDA 被多种语义复用（选体 / 相机距离 / 铁桶舀水），它们对「Torch / Water 是否挡射线」
// 需求不同，故用 filter 标志位独立切换（详见 raycast.h RayFilter 注释）：
//   - 选体（HitTorch）：火把挡（t184，可选中 / 直挖）、水穿过（t165 水下可挖实体）。
//   - 相机距离（Default）：火把 / 水均穿过（皆 non-solid，相机不应被拉近视距，保 t40）。
//   - 铁桶（HitWater）：水挡（命中首个水格舀水）、火把穿过。
RayHit raycastVoxel(const World &world, QVector3D origin, QVector3D dir, float maxDist, unsigned filter)
{
    RayHit h;
    if (dir.lengthSquared() < 1e-12f)
        return h; // 零向量：无方向，不命中
    dir.normalize();

    // 射线「阻挡」谓词：空气恒穿过；实体方块（非 Torch / Water）恒挡；Torch / Water 由 filter 决定。
    //   不复用 World::isSolid（语义=blockAt!=0 会把 Torch / Water 当 solid；且选体要 Torch 挡、相机要
    //   Torch 穿，需按 filter 区分），改读 blockAt + filter 显式判定（单一权威 + 模式可切换）。
    auto blocksRay = [&world, filter](int cx, int cy, int cz) {
        const quint8 b = world.blockAt(cx, cy, cz);
        if (b == quint8(0)) return false; // 空气：永远穿过
        // Torch / Water 是否挡射线由 filter 决定（不同射线模式语义不同，见 RayFilter 注释）。
        if (b == BlockRegistry::Torch && !(filter & RayFilter::HitTorch)) return false;
        if (b == BlockRegistry::Water && !(filter & RayFilter::HitWater)) return false;
        return true;
    };

    // 当前所在体素（floor 对负坐标亦正确：-2.3 ∈ 体素 -3）
    int x = int(std::floor(origin.x()));
    int y = int(std::floor(origin.y()));
    int z = int(std::floor(origin.z()));

    // 起点已嵌在「该模式视为阻挡」的格内 → 退化，视为不可命中（相机穿模进实体方块，避免无意义高亮）。
    //   t174 例外：HitWater 模式下起点在水格属正常（玩家在水中游泳），视该水格为首个命中 ——
    //   桶舀「身处水」（眼位在水时含水射线起点即水格，否则判退化会漏掉水下舀水）。无外法线
    //   （射线未跨面进入此格，nx/ny/nz 保持 0；桶舀水只读格坐标不读法线，无碍）。
    //   t184：HitTorch 模式起点在火把格（玩家眼位恰入火把格 —— 火把 non-solid 可走入）亦判退化（不命中），
    //   避免选中「贴脸火把」+ 无意义高亮；玩家退后半步即可正常选中该火把（火把通常贴墙，眼位入其格
    //   是边缘瞬时态，退化无碍主流程）。
    if (blocksRay(x, y, z)) {
        if ((filter & RayFilter::HitWater) && world.blockAt(x, y, z) == BlockRegistry::Water) {
            h.valid = true;
            h.bx = x; h.by = y; h.bz = z;
            return h; // 起点即水格 → 命中该格（dist=0；法线 0）
        }
        return h; // 起点嵌实体方块（相机穿模）/ HitTorch 起点在火把格 → 退化，不可命中
    }

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
            h.valid = true;
            h.bx = x; h.by = y; h.bz = z;
            h.nx = float(nx); h.ny = float(ny); h.nz = float(nz);
            h.dist = t; // 命中面距起点欧氏距离（dir 已归一 → t 即距离）；t40 相机钳制复用
            return h;
        }
    }
    return h; // 射程内全为空气 / 被该模式视为穿过的方块
}
