#include "raycast.h"
#include "world.h"

#include <cmath>
#include <limits>

// Amanatides & Woo「快速体素遍历」：每步跳到下一个体素边界（沿主导轴恰好 1 格），
// 故步长 ≤ 1 格（满足 dev-spec t04 要求）。命中面法线 = 进入该体素时所跨轴的 −step。
//
// 参考：A Fast Voxel Traversal Algorithm for Ray Tracing (1987)。整数格坐标按
// World 约定（+Y 朝上，越界=空气）；isSolid 决定阻挡。
RayHit raycastVoxel(const World &world, QVector3D origin, QVector3D dir, float maxDist)
{
    RayHit h;
    if (dir.lengthSquared() < 1e-12f)
        return h; // 零向量：无方向，不命中
    dir.normalize();

    // 当前所在体素（floor 对负坐标亦正确：-2.3 ∈ 体素 -3）
    int x = int(std::floor(origin.x()));
    int y = int(std::floor(origin.y()));
    int z = int(std::floor(origin.z()));

    // 起点已嵌在实体方块内（相机穿模）→ 退化，视为不可命中，避免无意义的高亮
    if (world.isSolid(x, y, z))
        return h;

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
        if (world.isSolid(x, y, z)) {
            h.valid = true;
            h.bx = x; h.by = y; h.bz = z;
            h.nx = float(nx); h.ny = float(ny); h.nz = float(nz);
            h.dist = t; // 命中面距起点欧氏距离（dir 已归一 → t 即距离）；t40 相机钳制复用
            return h;
        }
    }
    return h; // 射程内全为空气
}
