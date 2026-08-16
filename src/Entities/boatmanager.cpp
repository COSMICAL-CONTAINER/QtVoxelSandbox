#include "boatmanager.h"

#include <QtMath>
#include <cmath>
#include <algorithm>

#include "world.h"           // 向下只读 World（blockAt / stateAt / isCollidable）
#include "blockregistry.h"   // BlockRegistry::isIce / Water / Air + iceSlipApproach（Core 层）

BoatManager::BoatManager(QObject *parent) : QObject(parent) {}

void BoatManager::setPlayerCenter(const QVector3D &center)
{
    // t508 玩家中心注入：NaN（菜单 / 暂停 / 未设）→ 不推船；合法坐标 → tick 内据此判玩家 ↔ 船重叠。
    m_playerCenter = center;
    m_playerValid = std::isfinite(center.x()) && std::isfinite(center.y()) && std::isfinite(center.z());
}

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
        // Slab 法 ray-AABB（同 EntityManager::findMobHit）：X 用 kBoatHalfW、Y 用 kBoatHalfH、Z 用 kBoatHalfLen
        //   （t556 矩形碰撞盒匹配船体：X 宽 1.0 / Z 长 1.4 / 高 0.7）。
        const float ext[3] = { kBoatHalfW, kBoatHalfH, kBoatHalfLen };
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
    // t508 已骑乘时允许换船（spec「骑船时右键另一艘船来坐上去」）：命中**另一艘**船（idx != m_riderBoat）
    //   → 直接把 m_riderBoat 切到新船（旧船释放骑乘态，自然浮水）。命中当前骑的船 → no-op（不重复上）。
    float dist = 0.0f;
    const int idx = findBoatHit(origin, dir, maxDist, &dist);
    if (idx < 0) return false;
    if (idx == m_riderBoat) return false; // 命中当前骑的船 → no-op（不重复上）
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
    // t508 下船 Y 防沉底：船 Y 在过渡期（冰→水 / 深水 lerp 中途）可能短暂低于真水面 → 直接用 bp.y 摆玩家
    //   会让玩家脚底沉到水面下深处，下一帧重力 + 水中减速不足以快速上浮 → 用户报「沉底按 shift 下不来」。
    //   取「船 Y」与「该列真水面 Y」的较高者作下船脚底：保证玩家落在水面或之上（下一帧要么站冰 / 地面、
    //   要么入水游泳上浮，绝不卡水底）。水面 Y 由 waterSurfaceY 查（无水返 fallback = bp.y，等同旧行为）。
    const float placeY = world ? std::max(bp.y(), waterSurfaceY(world, bp.x(), bp.z(), bp.y())) : bp.y();
    // 玩家下船摆船侧（+X 侧 1 格）安全位：脚底 Y = placeY（水面附近，下个 tick 重力让其落到支撑面）。
    //   优先 +X 侧；若 +X 侧堵（可碰撞方块）则试 -X / +Z / -Z，取首个非堵方向。
    outPlayerFeet = QVector3D(bp.x() + 1.0f, placeY, bp.z());
    if (world) {
        const float candidates[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto &c : candidates) {
            const float tx = bp.x() + c[0], tz = bp.z() + c[1];
            const int cx = int(std::floor(tx)), cz = int(std::floor(tz));
            const int cy = int(std::floor(placeY));
            // 该列脚位 + 头位格都非可碰撞 → 可站（防下船即卡墙）。
            if (!world->isCollidable(cx, cy, cz) && !world->isCollidable(cx, cy + 1, cz)) {
                outPlayerFeet = QVector3D(tx, placeY, tz);
                break;
            }
        }
    }
    notifyChanged();
    return true;
}

bool BoatManager::boatFootprintBlocked(World *world, float px, float py, float pz, bool ignoreIce) const
{
    if (!world) return false;
    // 扫船 footprint（X [−kBoatHalfW,+kBoatHalfW] × Z [−kBoatHalfLen,+kBoatHalfLen] 的矩形，t556 匹配船体
    //   X 宽 1.0 / Z 长 1.4）覆盖的所有格，Y 取船中心所在格 + 其上一格（船舱占两格高，防只查单格漏矮墙）。
    //   任一格可碰撞 → 挡船。
    //   review L10：ignoreIce=true（仅水档碰岸探测传）把冰族（Ice/PackIce/BlueIce，isIce 单一权威）视作
    //   可通行 —— 冰顶与水面顶同层（船中心 Y = 水面顶 = 冰顶），旧探测把同层冰面当岸 → 朝冰速度分量被
    //   清零，船停在冰缘前 ~0.15 格永远上不了冰（t611 冰面加速只能把船直接放到冰上才生效）。探测豁免后
    //   船可从水面滑上同层冰面；实际位移碰撞（下方不传本参的 boatFootprintBlocked 调用）冰仍按实体挡
    //   （船不嵌冰）；真岸（沙 / 草 / 石岸顶通常高出水面 ≥1 格 → y-1 层 + cy 层仍命中）不受影响，防搁浅
    //   语义保持。
    const auto cellBlocked = [world, ignoreIce](int x, int y, int z) {
        return world->isCollidable(x, y, z)
            && !(ignoreIce && BlockRegistry::isIce(world->blockAt(x, y, z)));
    };
    const int x0 = int(std::floor(px - kBoatHalfW)), x1 = int(std::floor(px + kBoatHalfW));
    const int z0 = int(std::floor(pz - kBoatHalfLen)), z1 = int(std::floor(pz + kBoatHalfLen));
    const int cy = int(std::floor(py));
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            if (cellBlocked(x, cy, z)) return true;
            if (cellBlocked(x, cy + 1, z)) return true;
        }
    return false;
}

float BoatManager::waterSurfaceY(World *world, float px, float pz, float fallbackY, bool *outFoundWater) const
{
    if (outFoundWater) *outFoundWater = false;
    if (!world) return fallbackY;
    const int cx = int(std::floor(px)), cz = int(std::floor(pz));
    // 自船中心格向上扫找最顶水格（其上为非水 = 水面）；目标 Y = 顶水格 cell 顶（y+1）− 吃水。
    //   没水（船在陆地 / 空中）→ 返 fallbackY（不浮，tick 由重力 / 玩家放置决定其 Y）。
    //   t508：扫描深度从 4 格扩到 32 格 —— 旧版仅扫 startY..startY+3，船若被放到深水中（pos.y 在水底附近）
    //     只能向上找 4 格内的水，到不了真水面 → 船卡在水柱中途（用户报「船下沉」的真因之一）。
    int topWaterY = -1;
    const int startY = int(std::floor(fallbackY));
    for (int y = startY; y < startY + 32 && y < 256; ++y) {
        if (world->blockAt(cx, y, cz) == BlockRegistry::Water) topWaterY = y;
        else if (topWaterY >= 0) break; // 已过水面（水 → 非水），停在最顶水格
    }
    // 起点格本身或其下也可能是水（船略没入 / 船在高处如冰面边缘向水面过渡）→ 向下补扫找水柱。
    //   t508：向下扫描深度从 4 格扩到 16 格 —— 旧版仅扫 startY-1..startY-4，船从冰面（高位）骑到水面（水面
    //     比冰面低 5+ 格）时向下 4 格够不到水面 → topWaterY 恒 -1 → 返 fallbackY（高位）→ tickRiddenBoat 把船
    //     Y 钉在高位悬空 / 或玩家下马后船被重力拽下穿过水柱沉底（用户报「船从冰上走下水直接沉底」真因）。
    //   向下扫到首个水格后，再向上爬到该水柱的最顶水格（= 真水面）：首个水格可能是水柱中段，必须爬顶。
    if (topWaterY < 0) {
        int firstWaterY = -1;
        for (int y = startY - 1; y >= 0 && y >= startY - 16; --y) {
            // boat 三轮「放冰上掉到冰下面」（用户报②）：向下扫水柱时碰到**可碰撞实体方块**（冰 / 沙 / 岩 /
            // 玻璃等）→ 该实体把下方水封住，船不可能浮在这封水之上 → 停（无水）。旧版下扫穿过冰面继续找水：
            //   冰（若架在水上，如冻湖）下方找到水 → 误判浮水 → 浮水 lerp / tickRiddenBoat 把船拽穿冰面沉到
            //   冰下水里。现遇首个实块即断 → 冰上船返 fallback（无水）→ 走重力落地分支贴冰顶（同陆地）。
            if (world->isCollidable(cx, y, cz)) break;
            if (world->blockAt(cx, y, cz) == BlockRegistry::Water) { firstWaterY = y; break; }
        }
        if (firstWaterY >= 0) {
            topWaterY = firstWaterY;
            for (int y = firstWaterY + 1; y < 256; ++y) {
                if (world->blockAt(cx, y, cz) == BlockRegistry::Water) topWaterY = y;
                else break; // 水柱顶（水 → 非水）
            }
        }
    }
    if (topWaterY < 0) return fallbackY;
    if (outFoundWater) *outFoundWater = true;
    return float(topWaterY) + 1.0f - kBoatDraft;
}

quint8 BoatManager::blockBelowBoat(World *world, const QVector3D &boatPos) const
{
    if (!world) return BlockRegistry::Air;
    // t508 从船中心格向下扫（含本格）找首个「可踩实体方块」（isCollidable），作为船的支撑面：
    //   - 船在冰面（pos.y 落在 Ice 格内，floor = Ice 格）：本格即 Ice → 直接返回 Ice / PackIce / BlueIce。
    //   - 船在水面（pos.y 落在 Water 格内，floor = Water 格，水非 collidable）：跳过水继续向下，到水底实体
    //     （沙 / 石）→ 返水底（非冰，无加速，正确）。
    //   旧版固定「floor(pos.y) − 1」跳过了船中心格 → 永远读到水底 / 冰下，冰面加速从未生效（t508 修）。
    const int cx = int(std::floor(boatPos.x()));
    const int cyBase = int(std::floor(boatPos.y()));
    const int cz = int(std::floor(boatPos.z()));
    for (int y = cyBase; y >= 0 && y >= cyBase - 3; --y) {
        const quint8 id = world->blockAt(cx, y, cz);
        if (world->isCollidable(cx, y, cz)) return id;
    }
    return BlockRegistry::Air;
}

void BoatManager::tick(qreal dt, World *world)
{
    if (!world || m_boats.empty()) return;
    bool changed = false;
    for (size_t i = 0; i < m_boats.size(); ++i) {
        Boat &b = m_boats[i];
        if (!b.alive) continue;
        // t508 二轮复盘：被骑船的物理（浮水 / 落地 / 操控位移 / 摩擦）由 step() 调 tickRiddenBoat 全权推进；
        //   本 tick 跳过被骑船，避免同帧 tick + tickRiddenBoat 双跑（浮水被覆盖做无用功、重力双落、摩擦误衰减
        //   wish 速度）。仅未骑的船在本 tick 跑浮水 + 玩家推开 + 残留惯性摩擦（空船物理）。
        if (int(i) == m_riderBoat) continue;
        // t508 玩家推船（pushable；机制等价 MC 1.0 船可被实体推开）：玩家 AABB 与船 footprint（水平圆 /
        // 方框）重叠 → 把船沿「玩家中心 → 船中心」水平方向推开（接触分离），并给船一小段水平速度（玩家
        //   走开后船继续滑一小段，机制等价 MC 玩家撞船船被弹开 + 滑行）。船 y 上贴近水面（浮水段统一管），推力只改 XZ。
        //   注：被骑船已在上文 continue 跳过本循环体，此处都是未骑船（玩家正是骑乘者不可能同时「推」骑乘船）。
        if (m_playerValid) {
            const float dpx = b.pos.x() - m_playerCenter.x();
            const float dpz = b.pos.z() - m_playerCenter.z();
            // t556 矩形碰撞盒：接触判定按轴分开 —— 玩家半宽 0.3 + 船半宽/半长（X: kBoatHalfW / Z: kBoatHalfLen）
            //   为各轴接触阈值；玩家中心落入「船 AABB 外扩玩家半宽」的矩形 → 重叠 → 推。
            const float cX = 0.3f + kBoatHalfW;
            const float cZ = 0.3f + kBoatHalfLen;
            if (std::fabs(dpx) < cX && std::fabs(dpz) < cZ) {
                // 接触分离量 = 把船推到该轴「刚好接触距离之外」（防 AABB 持续穿叠）；沿玩家 → 船方向分开。
                const float pushX = dpx != 0.0f ? (dpx > 0.0f ? (cX - dpx) : (-cX - dpx)) : 0.0f;
                const float pushZ = dpz != 0.0f ? (dpz > 0.0f ? (cZ - dpz) : (-cZ - dpz)) : 0.0f;
                // 逐轴试推（撞可碰撞方块则该轴不推，防把船推进墙里）。
                if (pushX != 0.0f && !boatFootprintBlocked(world, b.pos.x() + pushX, b.pos.y(), b.pos.z()))
                    b.pos.setX(b.pos.x() + pushX);
                if (pushZ != 0.0f && !boatFootprintBlocked(world, b.pos.x(), b.pos.y(), b.pos.z() + pushZ))
                    b.pos.setZ(b.pos.z() + pushZ);
                // 给船一小段水平速度（玩家走开后船继续滑）。t556：冲量 0.08 + 摩擦 5.0 → 滑行 <0.01 格、肉眼不动。
                const float nlen = std::sqrt(dpx * dpx + dpz * dpz);
                if (nlen > 1e-6f) {
                    b.vx += (dpx / nlen) * kBoatPushImpulse;
                    b.vz += (dpz / nlen) * kBoatPushImpulse;
                }
                changed = true;
            }
        }
        // 浮水 / 落地（t508 二轮复盘）：查本列水面 Y（waterSurfaceY 内扫水柱并回报是否找到水）。
        //   - 有水：pos.y 向水面 lerp（恒速上浮 / 下沉到水面），机制等价 MC 船浮水稳态。
        //   - 无水（陆地 / 冰面 / 空中）：加常速重力让船落向支撑面（boatFootprintBlocked 挡实块即停）。
        //     修「放陆地悬空半格」（用户报③）+「陆地卡住沉底」（⑥：旧版无水时 Y 不变、卡在放船点；现落地贴
        //     支撑面，骑乘下船摆位才正常）+「放水上飞到水下」（②：旧版 hasWater 用船中心格 == Water 判，但船浮
        //     水面时中心格是水上空气 → 误判无水 → 重力拽船下沉；改用 waterSurfaceY 的扫柱结论判有无水，准）。
        bool foundWater = false;
        const float surfY = waterSurfaceY(world, b.pos.x(), b.pos.z(), b.pos.y(), &foundWater);
        const float dy = surfY - b.pos.y();
        if (foundWater) {
            if (std::fabs(dy) > 1e-3f) {
                // 恒速上浮 / 下沉（kBoatAccel 作速率），钳到本帧不超过 |dy|（防过冲振荡）。
                const float step = std::clamp(kBoatAccel * float(dt), 0.0f, std::fabs(dy)) * (dy > 0 ? 1.0f : -1.0f);
                b.pos.setY(b.pos.y() + step);
                changed = true;
            }
        } else {
            // 无水重力（t508 二轮复盘）：找船 footprint 下方首个可碰撞方块作支撑面（其顶 = cy+1）。
            //   船底低于中心 kBoatHullBottom（0.2），稳态船中心 Y = 支撑顶 + 0.2（船底贴顶）。
            //   - 船中心 < 稳态 Y（已穿 / 接触支撑顶）→ 直接贴稳态 Y（防下落穿过 + 防落地后每帧抖动：旧版
            //     「落进实块格 → 贴格顶」因 dt 步进使船在「格顶」与「下一格」间反复横跳 0.2+ → 可见抖动）。
            //   - 船中心 > 稳态 Y → 匀加速下落（但本帧下落不穿稳态 Y，钳到稳态 Y）。
            //   扫描深度 3 格（船底 + 其下 2 格，防只查紧邻格漏台阶级差）。
            const int bcx = int(std::floor(b.pos.x())), bcz = int(std::floor(b.pos.z()));
            int supportCell = -1;
            for (int y = int(std::floor(b.pos.y())); y >= 0 && y >= int(std::floor(b.pos.y())) - 3; --y) {
                if (world->isCollidable(bcx, y, bcz)) { supportCell = y; break; }
            }
            if (supportCell >= 0) {
                // 有支撑：稳态 Y = 支撑顶 + 船底偏移；落不穿。
                const float restY = float(supportCell) + 1.0f + kBoatHullBottom;
                if (b.pos.y() <= restY) {
                    b.pos.setY(restY); // 贴稳态（防抖动）
                } else {
                    const float gy = b.pos.y() - kBoatGravity * float(dt);
                    b.pos.setY(std::max(gy, restY)); // 下落但钳到不穿支撑
                }
            } else {
                // 无支撑（虚空 / 高空）→ 自由下落。
                b.pos.setY(b.pos.y() - kBoatGravity * float(dt));
            }
            changed = true;
        }
        // 水平速度积分位移 + 逐轴碰撞（空船被推 / 残留惯性滑行；被骑船的操控位移由 tickRiddenBoat 推进）。
        if (b.vx != 0.0f || b.vz != 0.0f) {
            const float dx = b.vx * float(dt);
            const float dz = b.vz * float(dt);
            if (dx != 0.0f) {
                const float nx = b.pos.x() + dx;
                if (!boatFootprintBlocked(world, nx, b.pos.y(), b.pos.z())) b.pos.setX(nx);
                else b.vx = 0.0f;
            }
            if (dz != 0.0f) {
                const float nz = b.pos.z() + dz;
                if (!boatFootprintBlocked(world, b.pos.x(), b.pos.y(), nz)) b.pos.setZ(nz);
                else b.vz = 0.0f;
            }
            changed = true;
        }
        // 水平速度摩擦衰减（空船渐停）。
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

    // t584 三档介质检测（重写）：先查船本列水柱（waterSurfaceY 扫柱结论 foundWater —— 船「浮在水中」
    //   = Water 档，即使水很浅、脚下是水底沙 / 石也是水档：介质档必须与浮水结论同源，否则浅水船读到
    //   水底实体 = 陆档，档位与浮力自相矛盾）；无水（陆地 / 冰面 / 空中）再看支撑面方块（blockBelowBoat
    //   首个可踩实体）：冰族（Ice/PackIce/BlueIce，isIce 单一权威）= Ice 档；其余 = Land 档。
    //   前向介质不单独取样（档位取船中心列即可 —— 介质切换瞬态由 lerp 平滑；「前方是岸」的探测归
    //   下方水中碰岸停段，由 footprint 前向覆盖）。
    bool foundWater = false;
    const float surfY = waterSurfaceY(world, b.pos.x(), b.pos.z(), b.pos.y(), &foundWater);
    const quint8 below = blockBelowBoat(world, b.pos);
    const bool onIce = !foundWater && BlockRegistry::isIce(below);

    // 三档推进参数（机制等价 MC 1.0 船：陆地几乎开不动 / 水中常速 / 冰面最快且惯性大——难操作才对）：
    //   - Water：kBoatSpeed × 1.0（8 blocks/s）+ 接近率 kBoatAccel=4（中等动量）。
    //   - Ice：  kBoatSpeed × 冰类型倍率（t611：Ice 2.0 / PackIce 2.4 / BlueIce 2.7 → 16~21.6 blocks/s，
    //     约水面 2~2.7 倍）+ 接近率读 BlockRegistry::iceSlipApproach（t611：冰 6 / 浮冰 3.2 / 蓝冰 1.9 /s，
    //     单一权威：越小越滑 → 松键后长滑行 = 冰面惯性；转向速率 × kBoatIceTurnMul（迟钝 = 难操作）。
    //   - Land：kBoatSpeed × kBoatLandSpeedMul 0.3（2.4 blocks/s，最慢 —— 比走路 4.3 还慢但能开，
    //     用户要求「直接放陆地上开不会停」即陆地非零速）+ 接近率 kBoatLandAccel=10（高响应：
    //     松手即停、无滑行，贴地挪动手感）。
    float maxSpeed = kBoatSpeed;
    float approach = kBoatAccel;
    float turnRate = kBoatTurnRate;
    if (foundWater) {
        // 水档：基础参数
    } else if (onIce) {
        // t611 冰档倍率微升（用户「冰上的惯性再大一点」）：1.8/2.2/2.5 → 2.0/2.4/2.7（16~21.6 blocks/s），
        //   配合 iceSlipApproach 调小（blockregistry t611）→ 冰上「更快 + 更收不住」。
        if (below == BlockRegistry::Ice)          maxSpeed = kBoatSpeed * 2.0f;
        else if (below == BlockRegistry::PackIce) maxSpeed = kBoatSpeed * 2.4f;
        else                                      maxSpeed = kBoatSpeed * 2.7f; // BlueIce（isIce 已保证）
        approach = BlockRegistry::iceSlipApproach(below); // 冰面惯性（滑度单一权威）
        turnRate = kBoatTurnRate * kBoatIceTurnMul;       // 转向迟钝（难操作）
    } else {
        maxSpeed = kBoatSpeed * kBoatLandSpeedMul;
        approach = kBoatLandAccel;
    }
    const float targetVx = wishX * maxSpeed;
    const float targetVz = wishZ * maxSpeed;
    // 船速向目标 lerp（动量；approach 接近率 → 冰上接近率低 = 加速慢 + 松键滑行远（惯性），机制等价 MC 船动量）。
    {
        const float alpha = 1.0f - std::exp(-approach * float(dt));
        b.vx += (targetVx - b.vx) * alpha;
        b.vz += (targetVz - b.vz) * alpha;
    }

    // 船头转向意图方向（wish 非零 → 平滑转向 atan2(wish)，机制等价 MC 船头随操控缓转；冰面 turnRate 打折）。
    {
        const float wlen = std::sqrt(wishX * wishX + wishZ * wishZ);
        if (wlen > 1e-3f) {
            const float targetYaw = std::atan2(-wishX, -wishZ) * 57.2957795f; // 弧度→度（180/π；与 player yaw 约定 -Z 前）
            // 最短角差转向（绕 ±180 取小）。
            float d = targetYaw - b.yaw;
            while (d > 180.0f) d -= 360.0f;
            while (d < -180.0f) d += 360.0f;
            const float maxTurn = turnRate * float(dt);
            b.yaw += std::clamp(d, -maxTurn, maxTurn);
        }
    }

    const float speed = std::sqrt(b.vx * b.vx + b.vz * b.vz);

    // t611 修「撞岸后整船焊死不能动」（用户：撞到岸边应该还能倒退，后面是水）：t584 旧版探到岸（水面同高层
    //   有实心方块）即**双轴速度无条件清零** → 船贴岸后每帧 lerp 刚建起倒退速度就被清掉（清除在位移积分之前）
    //   → 船永不位移 → footprint 永不脱离岸块 → 死锁（撞岸 = 焊死）。修：**只挡朝岸分量** —— 对四个水平方向
    //   （±X / ±Z）分别探「船 footprint 前探一小步（kShoreProbe）后该层是否被挡」，被挡方向 n 的速度分量
    //   v·n > 0 部分清零（朝岸 → 停）；背向分量保留（倒退 / 侧滑离岸仍有效，机制等价 MC 1.0 船顶岸可倒退）。
    //   高速撞岸（speed ≥ kBoatCrashSpeed）仍整船撞毁（掉散件）。陆档 / 冰档不触发本检测（同 t584 语义）。
    if (foundWater && world) {
        // 四向探测：footprint 中心沿该方向前探 kShoreProbe 后，水面同高层（y-1）是否被挡。
        //   probe 取 0.15（略小于半宽 0.5 / 半长 0.7 的接触裕度）：贴岸（footprint 已触岸）时朝岸侧必命中，
        //   背岸侧（后方是水）不命中 → 背向分量永不清除。
        //   review L10：探测传 ignoreIce=true（冰面豁免，见 boatFootprintBlocked 注释）—— 冰顶与水面同层，
        //   船须能越过冰缘滑上冰面（t611 冰面加速入口）；沙 / 草岸等真岸不豁免（防搁浅语义保持）。
        const bool blockedPosX = boatFootprintBlocked(world, b.pos.x() + kShoreProbe, b.pos.y() - 1.0f, b.pos.z(), /*ignoreIce*/ true);
        const bool blockedNegX = boatFootprintBlocked(world, b.pos.x() - kShoreProbe, b.pos.y() - 1.0f, b.pos.z(), /*ignoreIce*/ true);
        const bool blockedPosZ = boatFootprintBlocked(world, b.pos.x(), b.pos.y() - 1.0f, b.pos.z() + kShoreProbe, /*ignoreIce*/ true);
        const bool blockedNegZ = boatFootprintBlocked(world, b.pos.x(), b.pos.y() - 1.0f, b.pos.z() - kShoreProbe, /*ignoreIce*/ true);
        const bool hitShore = (blockedPosX && b.vx > 0.0f) || (blockedNegX && b.vx < 0.0f)
                           || (blockedPosZ && b.vz > 0.0f) || (blockedNegZ && b.vz < 0.0f);
        if (hitShore) {
            if (speed >= kBoatCrashSpeed) {
                outCrashed = true; // 高速撞岸 → 撞毁（掉散件）
            }
            // 只清朝岸分量（被挡方向上 v·n>0 的部分）；背向 / 正交分量保留（倒退可走）。
            if (blockedPosX && b.vx > 0.0f) b.vx = 0.0f;
            if (blockedNegX && b.vx < 0.0f) b.vx = 0.0f;
            if (blockedPosZ && b.vz > 0.0f) b.vz = 0.0f;
            if (blockedNegZ && b.vz < 0.0f) b.vz = 0.0f;
        }
    }

    // 逐轴（X 后 Z）积分位移 + 碰撞：撞可碰撞方块则该轴不动；高速撞（speed>kBoatCrashSpeed）→ 撞毁。
    //   t508 二轮复盘修「能开出虚空」（用户报⑧）：船 XZ 位移只查 boatFootprintBlocked（世界内实块），
    //     但世界边缘外 blockAt 返 Air → isCollidable=false → 不挡船 → 船可开出世界边界进虚空。加世界边界
    //     clamp：船 footprint（±kBoatHalfW）必须落在 [0,width]×[0,depth] 内（半宽留 1 格缓冲防骑跨边界卡 delegate）。
    const float dx = b.vx * float(dt);
    const float dz = b.vz * float(dt);
    // 世界边界（半宽外扩防船头穿出）：无 world → 不限。t556：X 用 kBoatHalfW / Z 用 kBoatHalfLen（矩形碰撞盒）。
    const float minX = world ? kBoatHalfW : -1e9f;
    const float maxX = world ? float(world->width()) - kBoatHalfW : 1e9f;
    const float minZ = world ? kBoatHalfLen : -1e9f;
    const float maxZ = world ? float(world->depth()) - kBoatHalfLen : 1e9f;
    // X 轴
    if (dx != 0.0f) {
        float nx = b.pos.x() + dx;
        if (nx < minX) nx = minX;
        if (nx > maxX) nx = maxX;
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
        float nz = b.pos.z() + dz;
        if (nz < minZ) nz = minZ;
        if (nz > maxZ) nz = maxZ;
        if (!boatFootprintBlocked(world, b.pos.x(), b.pos.y(), nz)) {
            b.pos.setZ(nz);
        } else {
            if (speed >= kBoatCrashSpeed) { outCrashed = true; b.vx = 0.0f; b.vz = 0.0f; }
            else b.vz = 0.0f;
        }
    }

    // 浮水 / 落地（同 tick 段逻辑；t508 二轮复盘）：有水 → Y 钉水面；无水 → 重力落支撑面。
    //   旧版无脑 b.pos.setY(surfY) 在无水时 surfY=fallback=pos.y → 陆地骑船 Y 恒不变（悬空）。改分支同 tick。
    //   t584：foundWater / surfY 在函数头部介质检测段已查（同帧复用，不再二次扫柱）。
    if (foundWater) {
        b.pos.setY(surfY);
    } else {
        // 无水重力落地（同 tick 段：扫支撑面 → 贴稳态 restY 防抖动）。
        const int bcx = int(std::floor(b.pos.x())), bcz = int(std::floor(b.pos.z()));
        int supportCell = -1;
        if (world) {
            for (int y = int(std::floor(b.pos.y())); y >= 0 && y >= int(std::floor(b.pos.y())) - 3; --y) {
                if (world->isCollidable(bcx, y, bcz)) { supportCell = y; break; }
            }
        }
        if (supportCell >= 0) {
            const float restY = float(supportCell) + 1.0f + kBoatHullBottom;
            if (b.pos.y() <= restY) b.pos.setY(restY);
            else b.pos.setY(std::max(b.pos.y() - kBoatGravity * float(dt), restY));
        } else {
            b.pos.setY(b.pos.y() - kBoatGravity * float(dt));
        }
    }

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
    // t535 撞坏掉木板 + 木棍（非完整船；机制等价 MC 1.0 船高速撞毁 → 3 木板 + 2 木棍）：发 boatWrecked（区别
    //   boatBroken = 挖船掉完整船）。格坐标取船中心所在格；boatType 暂不影响（两变体撞坏都掉木板 + 木棍）。
    emit boatWrecked(int(std::floor(bp.x())), int(std::floor(bp.y())), int(std::floor(bp.z())), bt);
    notifyChanged();
}

bool BoatManager::hitBoatFromRay(const QVector3D &origin, const QVector3D &dir, float maxDist)
{
    float dist = 0.0f;
    const int idx = findBoatHit(origin, dir, maxDist, &dist);
    if (idx < 0 || idx >= int(m_boats.size()) || !m_boats[size_t(idx)].alive) return false;
    // t508 挖船：移除该船 + 清骑乘态（若挖的是被骑的船）+ emit boatBroken → 呈层 spawnItem 掉船物品
    //   （机制等价 MC 1.0 攻击船 → 船破坏掉船物品）。格坐标取船中心所在格；boatType 决定掉哪种船物品。
    const QVector3D bp = m_boats[size_t(idx)].pos;
    const int bt = m_boats[size_t(idx)].boatType;
    if (idx == m_riderBoat) m_riderBoat = -1; // 挖骑乘中的船 → 玩家自然下马
    releaseSlot(idx);
    emit boatBroken(int(std::floor(bp.x())), int(std::floor(bp.y())), int(std::floor(bp.z())), bt);
    notifyChanged();
    return true;
}
