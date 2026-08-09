#include "boatmanager.h"

#include <QtMath>
#include <cmath>
#include <algorithm>

#include "world.h"           // 向下只读 World（blockAt / stateAt / isCollidable）
#include "blockregistry.h"   // BlockRegistry::isIce / Water / Air + iceSlipApproach（Core 层）

BoatManager::BoatManager(QObject *parent) : QObject(parent) {}

bool BoatManager::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_boats.size())) return false;
    return m_boats[size_t(i)].alive;
}

void BoatManager::spawnBoat(int x, int y, int z, int boatType)
{
    if (m_liveCount >= kCap) { qWarning("vo.entities: BoatManager spawnBoat cap reached (%d)", kCap); return; }
    Boat b;
    b.pos = QVector3D(float(x) + 0.5f, float(y) + 1.0f - kBoatDraft, float(z) + 0.5f); // 格中心 + 浮水面（cell 顶 - 吃水）
    b.boatType = (boatType == Spruce) ? Spruce : Oak;
    b.vx = 0.0f; b.vz = 0.0f; b.yaw = 0.0f;
    acquireSlot(std::move(b));
    notifyChanged();
}

QVector3D BoatManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_boats.size()) || !m_boats[size_t(i)].alive) return QVector3D(0, 0, 0);
    return m_boats[size_t(i)].pos;
}

int BoatManager::boatTypeAt(int i) const
{
    if (i < 0 || i >= int(m_boats.size()) || !m_boats[size_t(i)].alive) return Oak;
    return m_boats[size_t(i)].boatType;
}

float BoatManager::yawAt(int i) const
{
    if (i < 0 || i >= int(m_boats.size()) || !m_boats[size_t(i)].alive) return 0.0f;
    return m_boats[size_t(i)].yaw;
}

int BoatManager::findBoatHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const
{
    int bestIdx = -1;
    float bestDist = maxDist;
    if (!std::isfinite(dir.x()) || !std::isfinite(dir.y()) || !std::isfinite(dir.z())) return -1;
    const float dirLen2 = dir.x()*dir.x() + dir.y()*dir.y() + dir.z()*dir.z();
    if (dirLen2 < 1e-8f) return -1;
    for (size_t i = 0; i < m_boats.size(); ++i) {
        const Boat &b = m_boats[i];
        if (!b.alive) continue; // 跳过空槽（slot-reuse 残留位）
        // Slab 法 ray-AABB（同 EntityManager::findMobHit）：X/Z 用 kBoatHalfW、Y 用 kBoatHalfH。
        const float ext[3] = { kBoatHalfW, kBoatHalfH, kBoatHalfW };
        float tmin = 0.0f, tmax = bestDist;
        bool hit = true;
        const float p[3] = { b.pos.x(), b.pos.y(), b.pos.z() };
        const float o[3] = { origin.x(), origin.y(), origin.z() };
        const float d[3] = { dir.x(), dir.y(), dir.z() };
        for (int k = 0; k < 3; ++k) {
            const float mn = p[k] - ext[k], mx = p[k] + ext[k];
            if (std::abs(d[k]) < 1e-8f) {
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
        const float dist = tmin >= 0.0f ? tmin : 0.0f;
        if (dist < bestDist) { bestDist = dist; bestIdx = int(i); }
    }
    if (bestIdx >= 0 && outDist) *outDist = bestDist;
    return bestIdx;
}

bool BoatManager::tryMount(const QVector3D &origin, const QVector3D &dir, float maxDist)
{
    if (m_riderBoat >= 0) return false; // 已骑乘 → 不重复上
    float dist = 0.0f;
    const int idx = findBoatHit(origin, dir, maxDist, &dist);
    if (idx < 0) return false;
    m_riderBoat = idx;
    notifyChanged();
    return true;
}

bool BoatManager::dismount(World *world, QVector3D &outPlayerFeet)
{
    if (m_riderBoat < 0) return false;
    const int idx = m_riderBoat;
    m_riderBoat = -1;
    if (idx < 0 || idx >= int(m_boats.size()) || !m_boats[size_t(idx)].alive) { notifyChanged(); return true; }
    const QVector3D bp = m_boats[size_t(idx)].pos;
    // 玩家下船摆船侧（+X 侧 1 格）安全位：脚底 Y = 船中心 Y（水面附近，下个 tick 重力让其落到支撑面）。
    //   优先 +X 侧；若 +X 侧堵（可碰撞方块）则试 -X / +Z / -Z，取首个非堵方向。
    outPlayerFeet = QVector3D(bp.x() + 1.0f, bp.y(), bp.z());
    if (world) {
        const float candidates[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &c : candidates) {
            const float tx = bp.x() + c[0], tz = bp.z() + c[1];
            const int cx = int(std::floor(tx)), cz = int(std::floor(tz));
            const int cy = int(std::floor(bp.y()));
            // 该列脚位 + 头位格都非可碰撞 → 可站（防下船即卡墙）。
            if (!world->isCollidable(cx, cy, cz) && !world->isCollidable(cx, cy + 1, cz)) {
                outPlayerFeet = QVector3D(tx, bp.y(), tz);
                break;
            }
        }
    }
    notifyChanged();
    return true;
}

bool BoatManager::boatFootprintBlocked(World *world, float px, float py, float pz) const
{
    if (!world) return false;
    // 扫船 footprint（[−half, +half]）覆盖的所有格（X/Z 向 floor(min)..floor(max)），Y 取船中心所在格 +
    // 其上一格（船舱占两格高，防只查单格漏矮墙）。任一格可碰撞 → 挡船。
    const int x0 = int(std::floor(px - kBoatHalfW)), x1 = int(std::floor(px + kBoatHalfW));
    const int z0 = int(std::floor(pz - kBoatHalfW)), z1 = int(std::floor(pz + kBoatHalfW));
    const int cy = int(std::floor(py));
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            if (world->isCollidable(x, cy, z)) return true;
            if (world->isCollidable(x, cy + 1, z)) return true;
        }
    return false;
}

float BoatManager::waterSurfaceY(World *world, float px, float pz, float fallbackY) const
{
    if (!world) return fallbackY;
    const int cx = int(std::floor(px)), cz = int(std::floor(pz));
    // 自船中心格向上扫找最顶水格（其上为非水 = 水面）；目标 Y = 顶水格 cell 顶（y+1）− 吃水。
    //   没水（船在陆地 / 空中）→ 返 fallbackY（不浮，tick 由重力 / 玩家放置决定其 Y）。
    int topWaterY = -1;
    const int startY = int(std::floor(fallbackY));
    for (int y = startY; y < startY + 4 && y < 256; ++y) {
        if (world->blockAt(cx, y, cz) == BlockRegistry::Water) topWaterY = y;
        else if (topWaterY >= 0) break; // 已过水面（水 → 非水），停在最顶水格
    }
    // 起点格本身或其下也可能是水（船略没入）→ 向下补扫 1 格覆盖「船中心在水面下一点」。
    if (topWaterY < 0 && startY - 1 >= 0 && world->blockAt(cx, startY - 1, cz) == BlockRegistry::Water)
        topWaterY = startY - 1;
    if (topWaterY < 0) return fallbackY;
    return float(topWaterY) + 1.0f - kBoatDraft;
}

quint8 BoatManager::blockBelowBoat(World *world, const QVector3D &boatPos) const
{
    if (!world) return BlockRegistry::Air;
    // 船「脚下」格 = 船中心 Y − 1 的格（船浮水面，水面格下方是水底 / 冰）。判冰面加速读它。
    return world->blockAt(int(std::floor(boatPos.x())),
                          int(std::floor(boatPos.y())) - 1,
                          int(std::floor(boatPos.z())));
}

void BoatManager::tick(qreal dt, World *world)
{
    if (!world || m_boats.empty()) return;
    bool changed = false;
    for (size_t i = 0; i < m_boats.size(); ++i) {
        Boat &b = m_boats[i];
        if (!b.alive) continue;
        // 浮水：pos.y 向水面 lerp（恒速接近，机制等价 MC 船浮水稳态）。
        const float surfY = waterSurfaceY(world, b.pos.x(), b.pos.z(), b.pos.y());
        const float dy = surfY - b.pos.y();
        if (std::fabs(dy) > 1e-3f) {
            // 恒速上浮 / 下沉（kBoatAccel 作速率），钳到本帧不超过 |dy|（防过冲振荡）。
            const float step = std::clamp(kBoatAccel * float(dt), 0.0f, std::fabs(dy)) * (dy > 0 ? 1.0f : -1.0f);
            b.pos.setY(b.pos.y() + step);
            changed = true;
        }
        // 水平速度摩擦衰减（空船渐停；被骑船的操控位移由 tickRiddenBoat 推进，此处衰减其残留惯性）。
        if (b.vx != 0.0f || b.vz != 0.0f) {
            const float decay = std::max(0.0f, 1.0f - kBoatFriction * float(dt));
            b.vx *= decay; b.vz *= decay;
            if (std::fabs(b.vx) < 0.01f) b.vx = 0.0f;
            if (std::fabs(b.vz) < 0.01f) b.vz = 0.0f;
            changed = true;
        }
    }
    if (changed) notifyChanged();
}

void BoatManager::tickRiddenBoat(qreal dt, World *world, float wishX, float wishZ,
                                 QVector3D &outBoatPos, bool &outCrashed)
{
    outCrashed = false;
    if (m_riderBoat < 0 || m_riderBoat >= int(m_boats.size())) { outBoatPos = QVector3D(); return; }
    Boat &b = m_boats[size_t(m_riderBoat)];
    if (!b.alive) { outBoatPos = QVector3D(); return; }

    // 目标速度 = wish × kBoatSpeed × iceMul（冰面加速，复用 BlockRegistry::isIce + 按类型递增倍率）。
    //   冰面倍率：Ice 1.5 / PackIce 2.0 / BlueIce 2.5（蓝冰最快，机制等价 MC 蓝冰船速最高）。非冰 → 1.0。
    float iceMul = 1.0f;
    if (world) {
        const quint8 below = blockBelowBoat(world, b.pos);
        if (below == BlockRegistry::Ice)          iceMul = 1.5f;
        else if (below == BlockRegistry::PackIce) iceMul = 2.0f;
        else if (below == BlockRegistry::BlueIce) iceMul = 2.5f;
    }
    const float maxSpeed = kBoatSpeed * iceMul;
    const float targetVx = wishX * maxSpeed;
    const float targetVz = wishZ * maxSpeed;
    // 船速向目标 lerp（动量；kBoatAccel 接近率 → 冰上叠加速时惯性明显，机制等价 MC 船动量）。
    {
        const float alpha = 1.0f - std::exp(-kBoatAccel * float(dt));
        b.vx += (targetVx - b.vx) * alpha;
        b.vz += (targetVz - b.vz) * alpha;
    }

    // 船头转向意图方向（wish 非零 → 平滑转向 atan2(wish)，机制等价 MC 船头随操控缓转）。
    {
        const float wlen = std::sqrt(wishX * wishX + wishZ * wishZ);
        if (wlen > 1e-3f) {
            const float targetYaw = std::atan2(-wishX, -wishZ) * 57.2957795f; // 弧度→度（180/π；与 player yaw 约定 -Z 前）
            // 最短角差转向（绕 ±180 取小）。
            float d = targetYaw - b.yaw;
            while (d > 180.0f) d -= 360.0f;
            while (d < -180.0f) d += 360.0f;
            const float maxTurn = kBoatTurnRate * float(dt);
            b.yaw += std::clamp(d, -maxTurn, maxTurn);
        }
    }

    // 逐轴（X 后 Z）积分位移 + 碰撞：撞可碰撞方块则该轴不动；高速撞（speed>kBoatCrashSpeed）→ 撞毁。
    const float speed = std::sqrt(b.vx * b.vx + b.vz * b.vz);
    const float dx = b.vx * float(dt);
    const float dz = b.vz * float(dt);
    // X 轴
    if (dx != 0.0f) {
        const float nx = b.pos.x() + dx;
        if (!boatFootprintBlocked(world, nx, b.pos.y(), b.pos.z())) {
            b.pos.setX(nx);
        } else {
            // 撞墙：高速 → 撞毁；低速 → 只停该轴。
            if (speed >= kBoatCrashSpeed) { outCrashed = true; b.vx = 0.0f; b.vz = 0.0f; }
            else b.vx = 0.0f;
        }
    }
    // Z 轴
    if (dz != 0.0f && !outCrashed) {
        const float nz = b.pos.z() + dz;
        if (!boatFootprintBlocked(world, b.pos.x(), b.pos.y(), nz)) {
            b.pos.setZ(nz);
        } else {
            if (speed >= kBoatCrashSpeed) { outCrashed = true; b.vx = 0.0f; b.vz = 0.0f; }
            else b.vz = 0.0f;
        }
    }

    // 浮水（同 tick：Y 钉水面），与操控位移（XZ）同帧合。
    const float surfY = waterSurfaceY(world, b.pos.x(), b.pos.z(), b.pos.y());
    b.pos.setY(surfY);

    outBoatPos = b.pos;
    notifyChanged();
}

void BoatManager::breakRiddenBoat()
{
    if (m_riderBoat < 0 || m_riderBoat >= int(m_boats.size())) return;
    const int idx = m_riderBoat;
    m_riderBoat = -1;
    if (!m_boats[size_t(idx)].alive) { notifyChanged(); return; }
    const QVector3D bp = m_boats[size_t(idx)].pos;
    const int bt = m_boats[size_t(idx)].boatType;
    releaseSlot(idx);
    // 掉船物品（呈层据信号 spawnItem）：格坐标取船中心所在格。
    emit boatBroken(int(std::floor(bp.x())), int(std::floor(bp.y())), int(std::floor(bp.z())), bt);
    notifyChanged();
}
