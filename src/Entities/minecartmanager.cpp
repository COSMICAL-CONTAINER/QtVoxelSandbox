#include "minecartmanager.h"

#include <QtMath>
#include <cmath>
#include <algorithm>

#include "world.h"           // 向下只读 World（blockAt / stateAt —— 轨与连接位）
#include "blockregistry.h"   // BlockRegistry::Rail / RailConnPx/Nx/Pz/Nz（Core 层）

MinecartManager::MinecartManager(QObject *parent) : QObject(parent) {}

bool MinecartManager::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_carts.size())) return false;
    return m_carts[size_t(i)].alive;
}

void MinecartManager::spawnCart(int x, int y, int z)
{
    if (m_liveCount >= kCap) { qWarning("vo.entities: MinecartManager spawnCart cap reached (%d)", kCap); return; }
    Cart c;
    // 矿车中心 = 轨格中心、轨面上 kCartRideH（轨格 cell 顶 = y+1；轨薄板 y=1/16 之上 → 车底贴轨面）。
    c.pos = QVector3D(float(x) + 0.5f, float(y) + 1.0f + kCartRideH, float(z) + 0.5f);
    // 初始行进方向默认 +Z（视觉初值；首次 tickRiddenCart 据 wish + 轨连接位重选向，未骑的矿车不动）。
    c.dirX = 0.0f; c.dirZ = 1.0f;
    c.speed = 0.0f;
    c.yaw = 180.0f; // +Z 行进的车头朝向（-Z 前约定下 yaw=180）
    acquireSlot(std::move(c));
    notifyChanged();
}

QVector3D MinecartManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_carts.size()) || !m_carts[size_t(i)].alive) return QVector3D(0, 0, 0);
    return m_carts[size_t(i)].pos;
}

float MinecartManager::yawAt(int i) const
{
    if (i < 0 || i >= int(m_carts.size()) || !m_carts[size_t(i)].alive) return 0.0f;
    return m_carts[size_t(i)].yaw;
}

int MinecartManager::findCartHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const
{
    int bestIdx = -1;
    float bestDist = maxDist;
    if (!std::isfinite(dir.x()) || !std::isfinite(dir.y()) || !std::isfinite(dir.z())) return -1;
    const float dirLen2 = dir.x()*dir.x() + dir.y()*dir.y() + dir.z()*dir.z();
    if (dirLen2 < 1e-8f) return -1;
    for (size_t i = 0; i < m_carts.size(); ++i) {
        const Cart &c = m_carts[i];
        if (!c.alive) continue; // 跳过空槽（slot-reuse 残留位）
        // Slab 法 ray-AABB（同 BoatManager::findBoatHit / EntityManager::findMobHit）：矩形盒半宽
        //   X=kCartHalfW / Y=kCartHalfH / Z=kCartHalfL（朝向仅绕 Y 旋转不影响 AABB 对轴近似 —— 取车斗外接）。
        const float ext[3] = { kCartHalfW, kCartHalfH, kCartHalfL };
        float tmin = 0.0f, tmax = bestDist;
        bool hit = true;
        const float p[3] = { c.pos.x(), c.pos.y(), c.pos.z() };
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

bool MinecartManager::tryMount(const QVector3D &origin, const QVector3D &dir, float maxDist)
{
    float dist = 0.0f;
    const int idx = findCartHit(origin, dir, maxDist, &dist);
    if (idx < 0) return false;
    if (idx == m_riderCart) return false; // 命中当前骑的矿车 → no-op（不重复上）
    m_riderCart = idx;
    notifyChanged();
    return true;
}

bool MinecartManager::dismount(World *world, QVector3D &outPlayerFeet)
{
    if (m_riderCart < 0) return false;
    const int idx = m_riderCart;
    m_riderCart = -1;
    if (idx < 0 || idx >= int(m_carts.size()) || !m_carts[size_t(idx)].alive) { notifyChanged(); return true; }
    const QVector3D cp = m_carts[size_t(idx)].pos;
    // 下车摆矿车侧（四向试首个非堵方向；脚底 Y = 矿车中心同高 —— 下一帧重力落到支撑面）。同 BoatManager
    //   dismount 的安全位模式（船侧摆位），矿车轨道场景格多为轨 / 空气 → 四向常通。
    outPlayerFeet = QVector3D(cp.x() + 1.0f, cp.y(), cp.z());
    if (world) {
        const float candidates[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &cd : candidates) {
            const float tx = cp.x() + cd[0], tz = cp.z() + cd[1];
            const int cx = int(std::floor(tx)), cz = int(std::floor(tz));
            const int cy = int(std::floor(cp.y()));
            // 该列脚位 + 头位格都非可碰撞 → 可站（防下车即卡墙）。
            if (!world->isCollidable(cx, cy, cz) && !world->isCollidable(cx, cy + 1, cz)) {
                outPlayerFeet = QVector3D(tx, cp.y(), tz);
                break;
            }
        }
    }
    notifyChanged();
    return true;
}

bool MinecartManager::pickTrackStep(World *world, const QVector3D &cartPos, float wantX, float wantZ,
                                    int &outDx, int &outDz) const
{
    if (!world) return false;
    // 矿车所在轨格（pos 为格中心 → floor 得格坐标）。
    const int rx = int(std::floor(cartPos.x()));
    const int ry = int(std::floor(cartPos.y())) - 1; // 轨格在矿车中心下一格（中心 = 轨顶 + kCartRideH → floor 减 1）
    const int rz = int(std::floor(cartPos.z()));
    if (world->blockAt(rx, ry, rz) != BlockRegistry::Rail) return false; // 不在轨上（防御）
    const quint8 con = world->stateAt(rx, ry, rz);
    // 4 向连接位（RailConnPx/Nx/Pz/Nz）→ 位移向量；选与 (wantX,wantZ) 点积最大且**非反向**（dot ≥ 0）者
    //   （拐角 dot=0 自动选中 —— 行进 +Z 时拐角连接 +X 点积 0 > 反向 -Z 的 -1 → 自动转弯）。反向连接
    //   （dot=-1，来路）不返回：死端轨若返回来路连接 → 跨格即 180° 掉头、按 W 全速倒退 → 两端永久振荡；
    //   改为返回 false 让「轨尽头 → 停」分支接管，停下后 tickRiddenCart 末尾的 -dir 重选分支再实现
    //   「按 W 蓄力反推回」（机制等价 MC 矿车在尽头轨停下后可反向推回）。
    struct Dir { int dx, dz; quint8 bit; };
    static const Dir kDirs[4] = {
        { 1,  0, BlockRegistry::RailConnPx},
        {-1,  0, BlockRegistry::RailConnNx},
        { 0,  1, BlockRegistry::RailConnPz},
        { 0, -1, BlockRegistry::RailConnNz},
    };
    int best = -1;
    float bestDot = -1.0f; // dot ≥ 0 才候选（0 = 拐角；-1 反向被 dot<0 过滤）
    for (int i = 0; i < 4; ++i) {
        if ((con & kDirs[i].bit) == 0) continue;
        const float dot = float(kDirs[i].dx) * wantX + float(kDirs[i].dz) * wantZ;
        if (dot < 0.0f) continue; // 反向连接（来路）不选 → 死端轨（仅剩来路）→ false 停
        if (dot > bestDot) { bestDot = dot; best = i; }
    }
    if (best < 0) return false; // 无非反向连接（轨尽头 / 仅来路）→ 停
    // 邻格确为 Rail（连接位应与之一致；防御 —— 连接位 stale 时兜底直查）。
    if (world->blockAt(rx + kDirs[best].dx, ry, rz + kDirs[best].dz) != BlockRegistry::Rail) {
        // 连接位失真：直接按邻格实查重选（首查四邻中与 want 点积最大、非反向且为 Rail 者）。
        int fb = -1; float fDot = -1.0f;
        for (int i = 0; i < 4; ++i) {
            if (world->blockAt(rx + kDirs[i].dx, ry, rz + kDirs[i].dz) != BlockRegistry::Rail) continue;
            const float dot = float(kDirs[i].dx) * wantX + float(kDirs[i].dz) * wantZ;
            if (dot < 0.0f) continue; // 反向连接不选（同上：死端返回 false 走停车分支）
            if (dot > fDot) { fDot = dot; fb = i; }
        }
        if (fb < 0) return false;
        best = fb;
    }
    outDx = kDirs[best].dx;
    outDz = kDirs[best].dz;
    return true;
}

void MinecartManager::tickRiddenCart(qreal dt, World *world, float wishX, float wishZ, QVector3D &outCartPos)
{
    if (m_riderCart < 0 || m_riderCart >= int(m_carts.size())) { outCartPos = QVector3D(); return; }
    Cart &c = m_carts[size_t(m_riderCart)];
    if (!c.alive) { outCartPos = QVector3D(); return; }

    // 停驻重选向（speed==0 且有输入）：按 wish 直接从轨连接位选向（pickTrackStep 内滤 dot<0 反向连接
    //   → 面向死端壁的 wish 无可用连接 → 保持停驻不动，不掉头不振荡）。选出的向与 wish 点积 ≥0 → 下方
    //   proj ≥0 → 输入即刻沿新向正推。蓄力重推语义：死端轨停稳后面向来路按 W/S → 来路连接 dot=+1 被选中
    //   → 反推回程（机制等价 MC 矿车在尽头轨停下后可反向推回）。
    if (c.speed == 0.0f && (std::fabs(wishX) > 1e-3f || std::fabs(wishZ) > 1e-3f)) {
        int ndx = 0, ndz = 0;
        if (pickTrackStep(world, c.pos, wishX, wishZ, ndx, ndz)) {
            c.dirX = float(ndx);
            c.dirZ = float(ndz);
        }
    }

    // 目标速度：wish 在当前行进方向上的投影（前推 / 后拉；无输入 → 0 摩擦滑行渐停）。
    const float wishLen = std::sqrt(wishX * wishX + wishZ * wishZ);
    float proj = 0.0f;
    if (wishLen > 1e-3f) {
        // wish 归一后与 dir 点积 → 前进 / 后退意图 [-1,1]（侧向分量投影 0 → 不侧移，轨约束）。
        proj = (wishX / wishLen) * c.dirX + (wishZ / wishLen) * c.dirZ;
    }
    const float targetV = proj * kCartSpeed;
    // 速度 lerp 接近目标（加速 / 摩擦统一：目标 0 时按 kCartFriction 衰减；目标 ±速时按 kCartAccel 接近）。
    {
        const float rate = (std::fabs(targetV) > 1e-3f) ? kCartAccel : kCartFriction;
        const float alpha = 1.0f - std::exp(-rate * float(dt));
        c.speed += (targetV - c.speed) * alpha;
        if (std::fabs(c.speed) < 0.02f) c.speed = 0.0f; // 死区归零（防微速漂移）
    }

    // 沿轨推进：以「格中心到格中心」的插值段推进（跨格时重选连接向 → 拐角自动转弯）。
    //   段 = 当前格中心 → 沿 dirX/dirZ 的下一轨格中心；到段末（进入新格中心）时重选下一连接向。
    if (c.speed != 0.0f) {
        const float step = c.speed * float(dt);
        float remain = step;
        int guard = 0;
        while (remain > 1e-5f && guard++ < 8) { // 子步循环（单帧跨多格；8 子步上限防死循环）
            // 当前段终点 = 当前所在格中心 + dir（沿行进向的下一格中心边界）。简化推进模型：
            //   目标格中心 = floor(pos) + 0.5 + dir*1.0（沿 dir 的邻格中心）。
            const int cxc = int(std::floor(c.pos.x()));
            const int czc = int(std::floor(c.pos.z()));
            const float nextCx = float(cxc) + 0.5f + c.dirX;
            const float nextCz = float(czc) + 0.5f + c.dirZ;
            const float ddx = nextCx - c.pos.x();
            const float ddz = nextCz - c.pos.z();
            const float segLen = std::fabs(ddx) + std::fabs(ddz); // 轴向 → 曼哈顿 = 欧氏
            if (segLen <= 1e-5f) { c.speed = 0.0f; break; }      // 已在格中心（理论不达，防御）
            if (remain < segLen) {
                // 段内推进：沿 dir 归一位移（ddx/ddz 恒轴对齐 → 归一 = dir）。
                c.pos.setX(c.pos.x() + c.dirX * remain);
                c.pos.setZ(c.pos.z() + c.dirZ * remain);
                remain = 0.0f;
            } else {
                // 跨到下一格中心：先落位，再重选下一连接向（拐角在此转弯 —— 新 dir = 与当前 dir 点积
                //   最大的连接向；反向连接点积 -1 仅在末端无前进连接时选中 = 允许推回）。
                c.pos.setX(nextCx);
                c.pos.setZ(nextCz);
                remain -= segLen;
                int ndx = 0, ndz = 0;
                if (!pickTrackStep(world, c.pos, c.dirX, c.dirZ, ndx, ndz)) {
                    c.speed = 0.0f; // 轨尽头 → 停（速度清零；W 再推也停 —— 须下车上轨延伸或反推）
                    break;
                }
                c.dirX = float(ndx);
                c.dirZ = float(ndz);
            }
        }
        // Y 钉轨面：矿车所在列向下找轨格（中心格或下一格）→ pos.y = 轨格 cell 顶 + kCartRideH。
        if (world) {
            const int bcx = int(std::floor(c.pos.x()));
            const int bcz = int(std::floor(c.pos.z()));
            const int bcy = int(std::floor(c.pos.y()));
            for (int y = bcy; y >= bcy - 2 && y >= 0; --y) {
                if (world->blockAt(bcx, y, bcz) == BlockRegistry::Rail) {
                    c.pos.setY(float(y) + 1.0f + kCartRideH);
                    break;
                }
            }
        }
        // 车头朝向（-Z 前约定：dir=(0,-1) → yaw 0；(1,0) → yaw 90；(0,1) → yaw 180；(-1,0) → yaw 270）。
        c.yaw = std::atan2(-c.dirX, -c.dirZ) * 57.2957795f;
        while (c.yaw < 0.0f) c.yaw += 360.0f;
    }

    outCartPos = c.pos;
    notifyChanged();
}

bool MinecartManager::hitCartFromRay(const QVector3D &origin, const QVector3D &dir, float maxDist)
{
    float dist = 0.0f;
    const int idx = findCartHit(origin, dir, maxDist, &dist);
    if (idx < 0 || idx >= int(m_carts.size()) || !m_carts[size_t(idx)].alive) return false;
    // 挖矿车：移除该矿车 + 清骑乘态（若挖的是被骑的矿车）+ emit cartBroken → 呈层 spawnItem 掉矿车物品
    //   （机制等价 MC 1.0 攻击矿车 → 矿车破坏掉矿车物品）。格坐标取矿车中心所在格。
    const QVector3D cp = m_carts[size_t(idx)].pos;
    if (idx == m_riderCart) m_riderCart = -1; // 挖骑乘中的矿车 → 玩家自然下车
    releaseSlot(idx);
    emit cartBroken(int(std::floor(cp.x())), int(std::floor(cp.y())), int(std::floor(cp.z())));
    notifyChanged();
    return true;
}
