#include "minecartmanager.h"

#include <QtMath>
#include <cmath>
#include <algorithm>
#include <QRandomGenerator> // t735 ① 掉落邻格散布随机取（同船 t711 修法）
#include <unordered_set> // t658 探测轨占用边沿表（m_detectorOccupied）

#include "world.h"           // 向下只读 World（blockAt / stateAt —— 轨与连接位）
#include "blockregistry.h"   // BlockRegistry::Rail / RailConnPx/Nx/Pz/Nz（Core 层）

MinecartManager::MinecartManager(QObject *parent) : QObject(parent) {}

// t658 探测轨占用格坐标打包（x/z 各 21 位有符号偏移、y 10 位 —— 同 playercontroller cellKey 编码族，
//   表内自洽）。m_detectorOccupied 键。
static inline quint64 packRailCell(int x, int y, int z)
{
    return (quint64(quint32(x + 0x100000) & 0x1FFFFFu))
         | (quint64(quint32(z + 0x100000) & 0x1FFFFFu) << 21)
         | (quint64(quint32(y) & 0x3FFu) << 42);
}
static inline void unpackRailCell(quint64 k, int &x, int &y, int &z)
{
    x = int(quint32(k & 0x1FFFFFu)) - 0x100000;
    z = int(quint32((k >> 21) & 0x1FFFFFu)) - 0x100000;
    y = int(quint32(k >> 42)) & 0x3FFu;
}

// t737 车头朝向随行进向更新（-Z 前约定：yaw = atan2(-dirX,-dirZ)，与 spawnCart / pushEmptyCart /
//   tickRiddenCart 末尾同公式同源）。stepCartAlongRail 拐角重选向时同步调用 —— 旧版只在被骑 tick 末尾
//   更新 yaw → **空车**（tickPushedCarts）过弯 dir 已转但车头不转（t737「骑乘与空车都要转」的空车半边；
//   被骑路径末尾重算同公式 → 幂等无害）。
static inline void cartYawFromDir(float dirX, float dirZ, float &outYaw)
{
    float y = std::atan2(-dirX, -dirZ) * 57.2957795f;
    while (y < 0.0f) y += 360.0f;
    outYaw = y;
}

bool MinecartManager::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_carts.size())) return false;
    return m_carts[size_t(i)].alive;
}

void MinecartManager::spawnCart(int x, int y, int z, World *world)
{
    if (m_liveCount >= kCap) { qWarning("vo.entities: MinecartManager spawnCart cap reached (%d)", kCap); return; }
    Cart c;
    // t734 ① 贴轨修真：轨面基准 = 轨格 cell 底 + 薄板 1/16（mesher yr 常量），**非 cell 顶**。旧版
    //   y+1.0+kCartRideH 把「格底薄板」当「格顶」→ 矿车悬浮约一整格（primed TNT / 雪傀儡 restY 基准
    //   同族错：渲染面贴格底、物理从格顶叠）。轨上：车底（渲染底板下沿 = 中心 −0.15）贴轨板顶 →
    //   中心 = y + rise + kCartRideH；非轨格（t734 放宽地面放置）：底贴 cell 底静止（kCartGroundH）。
    const bool onRail = world && BlockRegistry::isRail(world->blockAt(x, y, z));
    c.pos = QVector3D(float(x) + 0.5f,
                      float(y) + (onRail ? kCartRideH : kCartGroundH),
                      float(z) + 0.5f);
    // t708 ② 初始朝向沿轨延伸（不再固定 +Z）：据目标轨格连接位定轴 —— X 轴连接 → 沿 X（单端取该延伸向、
    //   对向取 +X）；仅 Z 连接 → 沿 Z（单端取该向、对向取 +Z）；孤轨（0 连接）→ 默认 +Z（旧行为兜底）。
    //   车头由 tick 停驻重选向 / 玩家 S 反推（负速倒行）按 wish 重定向 —— 「双方向」由推 / 倒行机制承担。
    if (onRail) {
        const quint8 st = world->stateAt(x, y, z);
        const quint8 con = quint8(st & 0x0F);
        const bool cpx = (con & BlockRegistry::RailConnPx) != 0;
        const bool cnx = (con & BlockRegistry::RailConnNx) != 0;
        const bool cpz = (con & BlockRegistry::RailConnPz) != 0;
        const bool cnz = (con & BlockRegistry::RailConnNz) != 0;
        if (cpx || cnx) { c.dirX = (cnx && !cpx) ? -1.0f : 1.0f; c.dirZ = 0.0f; } // 单端向该延伸；对向 +X
        else if (cpz || cnz) { c.dirX = 0.0f; c.dirZ = (cnz && !cpz) ? -1.0f : 1.0f; } // 单端向该向；对向 +Z
        const int nConn = int(cpx) + int(cnx) + int(cpz) + int(cnz);
        const bool isCorner = (nConn == 2 && ((cpx || cnx) && (cpz || cnz)));
        // t708 ① 贴轨面（放置不悬空）：放置格中心（fx=fz=0.5）的坡面高 = 本轴两种向的 +1 坡抬升折半
        //   （同 mesher riseAtX/riseAtZ 与 tickRiddenCart 钉轨面公式 → 帧 0 即贴轨面；拐角 / 十字无坡）。
        if (!isCorner && nConn < 3) {
            const bool ew = (cpx || cnx) || (nConn == 0 && (st & BlockRegistry::RailAxisEWFlag) != 0);
            const auto dlt = [&](int dx, int dz) {
                return BlockRegistry::railProbeDelta(
                    { world->blockAt(x + dx, y, z + dz),
                      world->blockAt(x + dx, y + 1, z + dz),
                      world->blockAt(x + dx, y - 1, z + dz) });
            };
            float rise = 0.0f;
            if (ew) {
                const int dpx = dlt(1, 0);  if (dpx > 0) rise += float(dpx) * 0.5f;
                const int dnx = dlt(-1, 0); if (dnx > 0) rise += float(dnx) * 0.5f;
            } else {
                const int dpz = dlt(0, 1);  if (dpz > 0) rise += float(dpz) * 0.5f;
                const int dnz = dlt(0, -1); if (dnz > 0) rise += float(dnz) * 0.5f;
            }
            c.pos.setY(float(y) + rise + kCartRideH); // t734：cell 底 + 坡面高（去掉旧 +1.0 格顶基准）
        }
        // 朝向 yaw 与 dir 同一公式（-Z 前 = 0 约定；见 tickRiddenCart yaw 更新注释）。
        c.yaw = std::atan2(-c.dirX, -c.dirZ) * 57.2957795f;
        while (c.yaw < 0.0f) c.yaw += 360.0f;
    } else {
        // t734 非轨格放置（地面静止车）/ 无世界兜底：默认 +Z 朝向（静态 —— 推进侧无轨守卫保证不动）。
        c.dirX = 0.0f; c.dirZ = 1.0f;
        c.yaw = 180.0f; // +Z 行进的车头朝向（-Z 前约定下 yaw=180）
    }
    c.speed = 0.0f;
    c.hp = kCartHitPoints; // t735 ② 生存耐久满血（创造 instantBreak 不看它；槽复用整结构覆盖无残留）
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

// t735 ② 剩余耐久读口（呈现层 delegate 绑它驱动受击摇晃；见头注释）。
int MinecartManager::hpAt(int i) const
{
    if (i < 0 || i >= int(m_carts.size()) || !m_carts[size_t(i)].alive) return 0;
    return m_carts[size_t(i)].hp;
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

// 复审 #4：列内向下扫最近可达轨格（实现见头注释；pinCartY / pickTrackStep / tickRiddenCart / 探测轨
//   占用四点共用单一权威）。遮挡判定 World::isCollidable（Core isCollidable 单一权威：轨 / 火把 / 水 /
//   花草 ShapeNone 恒 false 不遮挡；LilyPad 特例可踩碰撞当遮挡 —— 矿车压睡莲列本就非法，可接受）。
int MinecartManager::scanRailColumn(World *world, int cx, int topY, int cz) const
{
    if (!world) return -1;
    for (int y = topY; y >= topY - 2 && y >= 0; --y) {
        if (BlockRegistry::isRail(world->blockAt(cx, y, cz))) return y;
        if (world->isCollidable(cx, y, cz)) break; // 实心遮挡 → 更下方轨不可达（隔板供电 / 假支撑拒）
    }
    return -1;
}

bool MinecartManager::pickTrackStep(World *world, const QVector3D &cartPos, float wantX, float wantZ,
                                    int &outDx, int &outDz) const
{
    if (!world) return false;
    // 矿车所在轨格 = 所在列向下扫的最近轨格（t708：不再用 floor(pos.y)-1 —— 坡格顶 rise≥0.7 时
    //   pos.y = 轨Y+1+rise+0.30 → floor(pos.y)-1 = 轨**上方**空气层 → isRail 判不在轨上 → 拐角重选向 /
    //   S 倒行 / 推空车在坡顶 30% 段全部失联。与 tickRiddenCart 钉轨面 / t691 前置钉定同「列内向下扫、
    //   容差 2」语义 → 坡顶 / 坡脚 / 平轨一律解析到真轨格）。轨判定 isRail 家族（普通 / 动力 / 探测）。
    //   复审 #4：扫描收口到 scanRailColumn（含实心遮挡断扫）。
    const int rx = int(std::floor(cartPos.x()));
    const int rz = int(std::floor(cartPos.z()));
    const int ry = scanRailColumn(world, rx, int(std::floor(cartPos.y())), rz);
    if (ry < 0) return false; // 不在轨上（防御；含实心遮挡 → 列内轨不可达）
    const quint8 con = world->stateAt(rx, ry, rz);
    // 4 向连接位（RailConnPx/Nx/Pz/Nz）→ 位移向量；选与 (wantX,wantZ) 点积最大且**非反向**（dot ≥ 0）者
    //   （拐角 dot=0 自动选中 —— 行进 +Z 时拐角连接 +X 点积 0 > 反向 -Z 的 -1 → 自动转弯）。反向连接
    //   （dot=-1，来路）不返回：死端轨若返回来路连接 → 跨格即 180° 掉头、按 W 全速倒退 → 两端永久振荡；
    //   改为返回 false 让「轨尽头 → 停」分支接管，停下后 tickRiddenCart 末尾的 -dir 重选分支再实现
    //   「按 W 蓄力反推回」（机制等价 MC 矿车在尽头轨停下后可反向推回）。
    //   t737 注：拐角形态（恰 1 X + 1 Z 臂）的「连接位 → 两臂走向」与贴图象限映射同源单一权威
    //   BlockRegistry::railCornerArms（Core）—— 本通用点积环在拐角格的选中结果与该表逐格等价
    //   （轴对齐行进进拐角 → 出口恒 = 垂直臂 dot=0；矩阵测试环线探针对两者一致性逐拐角断言），
    //   贴图与物理不再各查各表（t737 前贴图像限自查表镜像错位的教训）。
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
    // 邻格确为铁轨（连接位应与之一致；防御 —— 连接位 stale 时兜底直查）。t638 家族判定。
    if (!BlockRegistry::isRail(world->blockAt(rx + kDirs[best].dx, ry, rz + kDirs[best].dz))) {
        // 连接位失真：直接按邻格实查重选（首查四邻中与 want 点积最大、非反向且为铁轨者）。
        int fb = -1; float fDot = -1.0f;
        for (int i = 0; i < 4; ++i) {
            if (!BlockRegistry::isRail(world->blockAt(rx + kDirs[i].dx, ry, rz + kDirs[i].dz))) continue;
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

// t708 钉轨面（共享 helper；实现见头注释）：矿车所在列向下扫最近轨格 → Y = cell 底 + 坡面高 + kCartRideH
//   （t734 基准修真：轨板贴 cell 底 +1/16，去掉旧 +1.0 格顶叠加）。返钉到的轨格 Y（-1 = 列内无轨）。
//   复审 #4：列扫描收口到 scanRailColumn（含实心遮挡断扫 —— 地面车隔着实心地板不再钉到地板下的轨）。
int MinecartManager::pinCartY(Cart &c, World *world)
{
    if (!world) return -1;
    const int bcx = int(std::floor(c.pos.x()));
    const int bcz = int(std::floor(c.pos.z()));
    const int y = scanRailColumn(world, bcx, int(std::floor(c.pos.y())), bcz);
    if (y < 0) return -1;
    const float fx = c.pos.x() - float(bcx); // [0,1) cell 内横向位置
    const float fz = c.pos.z() - float(bcz);
    const auto dlt = [&](int dx, int dz) {
        return BlockRegistry::railProbeDelta(
            { world->blockAt(bcx + dx, y, bcz + dz),
              world->blockAt(bcx + dx, y + 1, bcz + dz),
              world->blockAt(bcx + dx, y - 1, bcz + dz) });
    };
    // t691 轴判定（mesher 同源）：连接位低 4 位定行进轴；EW（±X 连接）→ riseAtX、NS → riseAtZ；
    //   0 连接读 RailAxisEWFlag（bit5 轴偏好）。拐角 / 十字无坡（mesher 同判）。
    const quint8 rst = world->stateAt(bcx, y, bcz);
    const quint8 con = quint8(rst & 0x0F);
    const bool cpx = (con & BlockRegistry::RailConnPx) != 0;
    const bool cnx = (con & BlockRegistry::RailConnNx) != 0;
    const bool cpz = (con & BlockRegistry::RailConnPz) != 0;
    const bool cnz = (con & BlockRegistry::RailConnNz) != 0;
    const int nConn = int(cpx) + int(cnx) + int(cpz) + int(cnz);
    const bool isCorner = (nConn == 2 && ((cpx || cnx) && (cpz || cnz)));
    const bool ew = (cpx || cnx) || (nConn == 0 && (rst & BlockRegistry::RailAxisEWFlag) != 0);
    float rise = 0.0f;
    if (isCorner) {
        // t709 坡臂拐角：mesher 拐角 quad 沿臂侧整边抬升（armLift）→ 矿车在格中心取四角抬升的
        //   双线性中心（坡底拐弯曲面过弯不跳变；平地拐角四角全 0 → rise 0 语义不变）。
        const int dpx = dlt(1, 0), dnx = dlt(-1, 0), dpz = dlt(0, 1), dnz = dlt(0, -1);
        const float eE = dpx > 0 ? float(dpx) : 0.0f, eW = dnx > 0 ? float(dnx) : 0.0f;
        const float eS = dpz > 0 ? float(dpz) : 0.0f, eN = dnz > 0 ? float(dnz) : 0.0f;
        rise = 0.25f * (eW + eE + eN + eS);
    } else if (nConn < 3) { // 直轨才有坡（拐角 / 十字无坡，mesher 同判）
        if (ew) {
            const int dpx = dlt(1, 0);
            const int dnx = dlt(-1, 0);
            if (dpx > 0 && dnx > 0) rise += 2.0f * std::fabs(fx - 0.5f); // t710 V 形凹谷：两端 +1、谷心 0（与 mesher 两半 quad 同曲面；railProbe 只回 ±1 → 无多档）
            else {
                if (dpx > 0) rise += float(dpx) * fx;
                if (dnx > 0) rise += float(dnx) * (1.0f - fx);
            }
        } else {
            const int dpz = dlt(0, 1);
            const int dnz = dlt(0, -1);
            if (dpz > 0 && dnz > 0) rise += 2.0f * std::fabs(fz - 0.5f); // t710 V 形凹谷（Z 向）
            else {
                if (dpz > 0) rise += float(dpz) * fz;
                if (dnz > 0) rise += float(dnz) * (1.0f - fz);
            }
        }
    }
    c.pos.setY(float(y) + rise + kCartRideH); // t734：轨板贴 cell 底（mesher yr=1/16），去掉旧 +1.0 格顶基准
    return y;
}

// t708 沿轨推进（共享：被骑 / 空车被推同一物理；实现见头注释）。负速 = 倒行（沿 -dir，车头保持原朝向）。
void MinecartManager::stepCartAlongRail(Cart &c, World *world, float dt)
{
    if (!world) { c.speed = 0.0f; return; }
    const float step = c.speed * float(dt);
    if (std::fabs(step) < 1e-5f) return;
    // 带符号行进单位向量：正行（speed>0）沿 +dir；倒行（speed<0）沿 -dir（车头 dir 不翻转 —— 保持朝向
    //   车底退行；跨格重选向只影响行进方向，正行转弯才更新 dir）。
    const int sgn = (step > 0.0f) ? 1 : -1;
    float tx = c.dirX * float(sgn);
    float tz = c.dirZ * float(sgn);
    // t734 起步先验：矿车停在格心（到心重选失败 / 停驻死区 / spawn / 被推起步都在格心）时，起步前先从
    //   当前列重选行进向 —— 死端 / 离轨即刻停零位移（旧版起步段不验 → 被骑输入每帧把清零速度再 lerp
    //   起步，一格一格把车「蠕行」推离轨道）。格心判定用 1e-4 容差（到心落位 / spawn 均写精确 .5 值；
    //   摩擦停车在段中非格心 → 跳过先验，其段已由上一格心的重选验证过）。拐角格心起步：重选结果即
    //   出口向（自动掰向，同到心重选语义）。
    {
        const float ccx = std::floor(c.pos.x()) + 0.5f;
        const float ccz = std::floor(c.pos.z()) + 0.5f;
        if (std::fabs(c.pos.x() - ccx) < 1e-4f && std::fabs(c.pos.z() - ccz) < 1e-4f) {
            int vdx = 0, vdz = 0;
            if (!pickTrackStep(world, c.pos, tx, tz, vdx, vdz)) { c.speed = 0.0f; return; }
            if (sgn > 0) { c.dirX = float(vdx); c.dirZ = float(vdz); cartYawFromDir(c.dirX, c.dirZ, c.yaw); } // t737：起步重选向（含拐角格起步）→ 车头同步
            tx = float(vdx); tz = float(vdz);
        }
    }
    float remain = std::fabs(step);
    int guard = 0;
    while (remain > 1e-5f && guard++ < 16) { // 子步循环（单帧跨多格；16 子步上限（boost×卡顿尖峰 dt 余量））
        // t734 段终点重写 = 行进向上**前方最近的格心**（行进轴 floor/ceil 取 k+0.5，另一轴不动）。
        //   旧版「当前格心 + 行进向」在 16ms tick 下步长 ~0.06-0.21 < 段长下界 0.5 → `remain < segLen`
        //   恒真 → 跨格分支（连接重选 / 拐角转弯 / 尽头停）在稳定帧率下是**死代码**、仅 dt 卡顿尖峰偶发
        //   触发 → 矿车沿初始 dir 直线冲出轨道悬浮滑到地图边界（t734 用户报「离轨仍可移动」根因）。
        //   改「前方最近格心」后每跨一个格心必经一次到心重选，任意步长都被轨连接位约束（帧率无关）。
        float nextCx = c.pos.x(), nextCz = c.pos.z();
        if (tx > 0.5f)       nextCx = std::floor(c.pos.x() + 0.5f) + 0.5f;
        else if (tx < -0.5f) nextCx = std::ceil(c.pos.x() - 0.5f) - 0.5f;
        else if (tz > 0.5f)  nextCz = std::floor(c.pos.z() + 0.5f) + 0.5f;
        else if (tz < -0.5f) nextCz = std::ceil(c.pos.z() - 0.5f) - 0.5f;
        const float segLen = std::fabs(nextCx - c.pos.x()) + std::fabs(nextCz - c.pos.z()); // 轴向 → 曼哈顿 = 欧氏
        // 注：segLen→0（pos 距格心 <1e-5 的 FP 残差）不在此停 —— 循环条件保证 remain>1e-5 ≥ segLen，
        //   自然落到心分支：钉到格心 + 到心重选（ε 残差被吸收，不误停车）。
        if (remain < segLen) {
            // 段内推进：沿行进 tx/tz 归一位移（带符号 dir）。段起点必经上一格心重选验证 → 段内位移
            //   恒在已验证的轨列上。
            c.pos.setX(c.pos.x() + tx * remain);
            c.pos.setZ(c.pos.z() + tz * remain);
            remain = 0.0f;
        } else {
            // 到心落位 → 重选下一连接向（拐角在此转弯；want = 行进向，dot≥0 滤反向）。无可用连接
            //   （死端 / 离轨）→ 停在格心、零溢出（t734：格心即验证点，越过格心的位移必先经验证）。
            c.pos.setX(nextCx);
            c.pos.setZ(nextCz);
            remain -= segLen;
            int ndx = 0, ndz = 0;
            if (!pickTrackStep(world, c.pos, tx, tz, ndx, ndz)) {
                c.speed = 0.0f; // 轨尽头 → 停（速度清零；正行 W 再推也停，须反推 / 上轨延伸）
                break;
            }
            if (sgn > 0) { c.dirX = float(ndx); c.dirZ = float(ndz); cartYawFromDir(c.dirX, c.dirZ, c.yaw); } // t737：正行跨格 → 车头同步新连接向（空车过弯也转 —— 骑乘 tick 末尾另有同公式重算，幂等）
            tx = float(ndx); tz = float(ndz); // 行进方向继续（倒行沿新连接向退行 —— 拐角倒车自动过弯）
        }
    }
}

// t708 ③ 空矿车被推后的滑行（PlayerController.step 每帧调，骑乘分支之外）：扫全部未被骑的活体矿车，
//   静置（speed≈0）不动（无被骑路径的 slopeDownAuto 起步闸门 —— 空车不自动溜坡，机制保守）；有速度
//   （pushEmptyCart 推动 / t735 碰撞获速 / 推入下坡段顺坡溜）→ 下坡不衰减（重力抵消摩擦 → 顺坡滑）、
//   平 / 上坡磨擦渐停 → 共享 stepCartAlongRail 沿轨推进（出轨 / 死端自动停：无轨不前进）→ 钉轨面（坡面
//   贴地）。t735 ④ 语义变更：**通电动力轨上摩擦不衰减、加速到 boost 档**（撞击获速的空车在动力段保持
//   前进直到离开；见循环内注释）。t736：探测轨占用标记统一在本 tick 末尾重扫（updateDetectorRailOccupancy
//   —— 被骑 / 空车 / 停驶全车种，见其头注释；被骑路径 tickRiddenCart 不再自标）。
//   任一车本帧水平位移 → 发一次 entitiesChanged（revision 触碰驱动 QML 位置刷新；无位移不发防每帧空转）。
void MinecartManager::tickPushedCarts(qreal dt, World *world)
{
    if (!world || dt <= 0.0) return;
    if (m_carts.empty()) return; // 无矿车 → 零开销（非骑乘帧常见路径；空向量 ⇔ 从未有过车 ⇔ 占用表必空，无需收边沿）
    bool moved = false;
    for (size_t i = 0; i < m_carts.size(); ++i) {
        Cart &c = m_carts[i];
        if (!c.alive) continue;
        if (int(i) == m_riderCart) continue; // 被骑的走 tickRiddenCart 专属物理（boost / 停驻重选向；占用统一在帧首 pass）
        if (std::fabs(c.speed) < 1e-3f) continue; // 静置空车不自动起步
        // 滑行物理：行进侧邻轨高度差判定（同 tickRiddenCart 坡道重力公式；空车无输入 → 只做滑行不抬速）。
        //   下坡（δ=-1）→ 重力抵摩擦 → 本帧不衰减（顺坡滑）；平 / 上坡 / 无轨（INT_MIN）→ 磨擦渐停。
        const float bx = c.pos.x(), bz = c.pos.z(); // 水平位移检测基准
        const int ry = pinCartY(c, world);
        if (ry < 0) { c.speed = 0.0f; continue; } // 无轨列 → 防御停
        const int gs = (c.speed >= 0.0f) ? 1 : -1;
        const int cx = int(std::floor(c.pos.x()));
        const int cz = int(std::floor(c.pos.z()));
        const int ndx = int(c.dirX) * gs, ndz = int(c.dirZ) * gs;
        const int slope = BlockRegistry::railProbeDelta(
            { world->blockAt(cx + ndx, ry, cz + ndz),
              world->blockAt(cx + ndx, ry + 1, cz + ndz),
              world->blockAt(cx + ndx, ry - 1, cz + ndz) });
        // t735 ④ 动力段保持（**语义变更**：t708 旧注释「空车不吃动力轨 boost」被本任务推翻 —— 碰撞 / 玩家
        //   推动获速的空车驶上通电动力轨（GoldenRail + GoldenRailStateOnFlag，同被骑路径 t658 通电判定）
        //   应保持前进直到离开动力段）：摩擦不衰减，反被 lerp 加速到 boost 档（沿当前 speed 符号 —— 动力
        //   轨不改向，只沿车既行方向供能）。静置空车（|speed|<1e-3 早退闸门）在动力轨上不被弹射起步 ——
        //   弹射语义只属被骑路径（防玩家放下的车自己跑掉）。
        const bool railPowered = world->blockAt(cx, ry, cz) == BlockRegistry::GoldenRail
            && (world->stateAt(cx, ry, cz) & BlockRegistry::GoldenRailStateOnFlag) != 0;
        if (railPowered) {
            const float targetV = (c.speed >= 0.0f ? 1.0f : -1.0f) * kCartBoostSpeed;
            const float alpha = 1.0f - std::exp(-kCartAccel * float(dt));
            c.speed += (targetV - c.speed) * alpha;
        } else if (slope == INT_MIN || slope >= 0) { // 平 / 上坡：摩擦衰减（帧率无关 exp 衰减）
            const float alpha = 1.0f - std::exp(-kCartFriction * float(dt));
            c.speed -= c.speed * alpha;
            if (std::fabs(c.speed) < 0.02f) { c.speed = 0.0f; continue; } // 磨擦停稳（死区）
        } // 下坡（slope<0）→ 不衰减（顺坡滑）；动力段已先行接管（boost 12.8 > 溜坡 10，语义不冲突）
        stepCartAlongRail(c, world, float(dt));
        pinCartY(c, world); // step 不碰 Y → 给新格重新钉坡面（下坡贴地滑 / 平轨贴面）
        const float dx = c.pos.x() - bx, dz = c.pos.z() - bz;
        if (dx * dx + dz * dz > 1e-6f) moved = true;
    }
    if (moved) notifyChanged();
    // t736 探测轨占用统一重扫：置于循环**之后** —— 空车已推进到本帧终位（被骑车在骑乘分支由先行的
    //   tickRiddenCart 推进），占用按全车种最新位置判定，无跨帧陈旧。PlayerController 骑乘 / 非骑乘两
    //   分支每帧都调本 tick → 占用（含离开沿清位）在帧级收口：车被挖毁 / clearAll 后本 pass 以「上一帧
    //   占用 − 本帧占用」自动清位断电（旧版被骑车被毁时 tickRiddenCart 早退不再收边沿 → 探测轨永久带电
    //   的泄漏一并修复）。
    updateDetectorRailOccupancy(world);
}

// t735 ③ 矿车↔矿车碰撞（实现见头注释）：两两水平 AABB 相交 → (a) 沿各自轨轴的位置去穿插（垂直轨轴
//   分量不位移 —— 防把车顶出轨道列被 pinCartY 判死）+ (b) 一维沿轨向近似动量传递（接近速度 ×
//   kCartMomentumTransfer 投影到各自轨轴加减；**注明简化**：等质量非弹性碰撞的简化模型，不做能量守恒
//   精确解）。
//   复审 #3（2026-08-22）：(a)/(b) 旧版不读世界直接改 pos/speed —— 轨道合法性只在 stepCartAlongRail
//   格心重选时校验，去穿插位移（每帧最多 ~0.46 格）与 0.85×closing 获速可把车送进无轨列 → 下一帧
//   pinCartY 返 -1 只清速度不回落 Y → 悬浮永久死车（死端追尾稳定复现）。修：
//   (a) clampShift —— 位移越过当前格边界时以 scanRailColumn（pinCartY 同语义，自车当前 Y 向下）探测
//       目标列有轨，无轨钳制在当前格边界内（ε 内缩防 FP 压线）；本列已无轨（离轨 / 地面车）位移恒 0
//       （同 pushEmptyCart「无轨推不动」语义）。目标列有轨但当前 Y 扫描窗够不到（坡顶上格 + 车 Y 尚停
//       坡脚低处）同样拒 —— 保证位移后位置下一帧 pinCartY 必能解析，位移永不制造「离轨悬空」态。
//   (b) impulseDirOk —— 受冲量方向（A 沿 -n 顶退 / B 沿 +n 推离）须有合法轨连接（pickTrackStep），
//       死端车（仅剩来路连接）/ 离轨车不吃冲量（正常追尾 / 对撞 / 坡道 / 交叉的正向连接不受影响）。
//   任一车 speed/pos 被改 → notifyChanged。
void MinecartManager::resolveCartCollisions(World *world)
{
    if (m_carts.size() < 2) return; // <2 车 → 无对可撞（零开销）
    // 复审 #3 (b) 冲量方向守卫：车沿 pushDir 有合法轨连接（pickTrackStep：列内有轨 + 非反向连接）才
    //   允许改速；无世界（防御路径）恒 false（无轨约束可验即不动车）。
    const auto impulseDirOk = [&](const Cart &c, float wx, float wz) -> bool {
        if (!world) return false;
        int pdx = 0, pdz = 0;
        return pickTrackStep(world, c.pos, wx, wz, pdx, pdz);
    };
    // 复审 #3 (a) 位移边界守卫：去穿插位移 s（沿轨轴带符号标量）若使车越过当前格边界，先以 pinCartY
    //   同语义列扫描（自车当前 Y 向下 + 实心遮挡断扫）探测目标列有轨；无轨 → 钳制在当前格边界内。
    //   返回允许执行的位移标量（0 = 不动；负 = 把已越线的半格拉回边界内）。
    const auto clampShift = [&](const Cart &c, float s) -> float {
        if (!world) return 0.0f;
        if (std::fabs(s) < 1e-6f) return s;
        const bool axisX = std::fabs(c.dirX) > 0.5f;
        const float d = axisX ? c.dirX : c.dirZ; // 轨轴分量（轨向四向 → 恒 ±1）
        const float cur = axisX ? c.pos.x() : c.pos.z();
        const int cellCur = int(std::floor(cur));
        const int cellNxt = int(std::floor(cur + d * s));
        if (cellNxt == cellCur) return s; // 段内位移（未跨格）→ 放行
        const int cx = int(std::floor(c.pos.x()));
        const int cz = int(std::floor(c.pos.z()));
        const int topY = int(std::floor(c.pos.y()));
        if (scanRailColumn(world, cx, topY, cz) < 0) return 0.0f; // 本列无轨（离轨 / 地面车）→ 推不动
        const int step = (cellNxt > cellCur) ? 1 : -1;
        const int tx = axisX ? cx + step : cx;
        const int tz = axisX ? cz : cz + step;
        if (scanRailColumn(world, tx, topY, tz) >= 0) return s; // 目标列有轨（当前 Y 可达）→ 放行
        const float bound = (step > 0) ? float(cellCur + 1) - 1e-3f : float(cellCur) + 1e-3f;
        return (bound - cur) / d; // 钳到边界内（d=±1 → 同号同模换算）
    };
    bool changed = false;
    for (size_t i = 0; i < m_carts.size(); ++i) {
        Cart &a = m_carts[i];
        if (!a.alive) continue;
        for (size_t j = i + 1; j < m_carts.size(); ++j) {
            Cart &b = m_carts[j];
            if (!b.alive) continue;
            // Y 层筛（不同高度的轨道层互不相交）：两车中心 Y 差 ≥ 2×半高 → 跳过。
            if (std::fabs(a.pos.y() - b.pos.y()) >= 2.0f * kCartHalfH) continue;
            // 水平中心连线 n（A→B 单位向量，即「碰撞法线」）。两车同格心（dist≈0；spawn 每格一车，理论
            //   不并置，防御）→ 用 A 行进向兜底定轴。
            float nx = b.pos.x() - a.pos.x();
            float nz = b.pos.z() - a.pos.z();
            float dist = std::sqrt(nx * nx + nz * nz);
            if (dist < 1e-4f) { nx = a.dirX; nz = a.dirZ; dist = 1.0f; }
            else { nx /= dist; nz /= dist; }
            if (dist >= kCartCollideSep) continue; // 未重叠
            // (b) 动量传递（仅互相逼近 closing>0 时）：各车速度矢量（dir×speed）沿 n 投影 → 接近速度；
            //   冲量 = closing × kCartMomentumTransfer，沿 n 加给 B、减给 A，再投影回**各自轨轴**
            //   （dot(n,dir) ∈ {0, ±0.707, ±1}）：同轨追尾 dot=±1 全额传递（后车减速、前车沿 n 被推走 ——
            //   负速=沿 -dir 倒行，与车头朝向无关地「被撞开」）；交叉轨道 dot≈±0.707 吃部分冲量；完全垂直
            //   dot=0 不吃（车沿原轨穿过道口，不飞出轨道）。对撞同式自然得「双双减速 / 轻微反弹」。
            const float aDot = a.dirX * nx + a.dirZ * nz;
            const float bDot = b.dirX * nx + b.dirZ * nz;
            const float closing = a.speed * aDot - b.speed * bDot; // >0 = A 沿 n 逼近 B
            if (closing > 0.05f) {
                // 复审 #3 (b)：A 的受冲向 = -n（顶退）、B 的 = +n（推离），各自须有合法轨连接才吃冲量
                //   （死端车不被撞进无轨列；守卫未过者速度原样保留 —— 另一侧的减速分量照常生效）。
                const float impulse = closing * kCartMomentumTransfer;
                if (impulseDirOk(b, nx, nz)) b.speed += impulse * bDot;
                if (impulseDirOk(a, -nx, -nz)) a.speed -= impulse * aDot;
                changed = true;
            }
            // (a) 位置去穿插（穿透 >5cm 才推，防贴轨停驻两车的 FP 微抖抖动）：各沿自身轨轴推开半穿透量
            //   （-n 在 A 轨轴上的投影定 A 的位移符号与大小；投影 0 = 交叉轨道不位移，只交换冲量）。
            const float pen = kCartCollideSep - dist;
            if (pen > 0.05f) {
                // 复审 #3 (a)：位移过 clampShift 守卫（越界无轨 → 钳当前格边界内；离轨车不动）。
                const float gA = clampShift(a, -aDot * (pen * 0.5f)); // A 沿 -n（背离 B）在其轨轴上的分量
                const float gB = clampShift(b,  bDot * (pen * 0.5f)); // B 沿 +n（背离 A）在其轨轴上的分量
                a.pos.setX(a.pos.x() + a.dirX * gA);
                a.pos.setZ(a.pos.z() + a.dirZ * gA);
                b.pos.setX(b.pos.x() + b.dirX * gB);
                b.pos.setZ(b.pos.z() + b.dirZ * gB);
                changed = true;
            }
        }
    }
    if (changed) notifyChanged();
}

// t735 ③ 行进矿车轻推玩家（实现见头注释）：|speed|>0.1 的活体车与玩家轴对齐 AABB 三轴相交 → 玩家沿车
//   行进向（dir×sign(speed)，负速=倒行推 -dir）获 kCartBumpSpeed 冲量（caller 写 m_knockback 击退通道，
//   复用其防穿墙子步与指数衰减；不做伤害）；车按 kCartBumpDrag 指数掉速（有阻力不挡停）。静置车不推人。
void MinecartManager::resolvePlayerPush(World *world, const QVector3D &playerFeet, float playerHalfW,
                                        float playerHeight, qreal dt, float &outPushX, float &outPushZ)
{
    outPushX = 0.0f;
    outPushZ = 0.0f;
    if (!world || dt <= 0.0) return;
    bool touched = false;
    for (size_t i = 0; i < m_carts.size(); ++i) {
        Cart &c = m_carts[i];
        if (!c.alive) continue;
        if (std::fabs(c.speed) < 0.1f) continue; // 静置车不推人（玩家推车由 pushEmptyCart 承担）
        // 轴对齐 AABB 相交（简化：不旋转盒，X 半宽 kCartHalfW / Z 半长 kCartHalfL 对轨向四向车取外接）。
        if (std::fabs(playerFeet.x() - c.pos.x()) >= kCartHalfW + playerHalfW) continue;
        if (std::fabs(playerFeet.z() - c.pos.z()) >= kCartHalfL + playerHalfW) continue;
        // Y 相交：玩家 [feet.y, feet.y+height] vs 车 [pos.y-kCartHalfH, pos.y+kCartHalfH]。
        if (playerFeet.y() >= c.pos.y() + kCartHalfH) continue;
        if (playerFeet.y() + playerHeight <= c.pos.y() - kCartHalfH) continue;
        const float sgn = (c.speed >= 0.0f) ? 1.0f : -1.0f; // 负速 = 倒行（推向 -dir）
        outPushX += c.dirX * sgn * kCartBumpSpeed;
        outPushZ += c.dirZ * sgn * kCartBumpSpeed;
        // 车碾过玩家掉速（帧率无关 exp 衰减；动力段 / 下坡会再供能 —— 推开玩家后继续走）。
        const float alpha = 1.0f - std::exp(-kCartBumpDrag * float(dt));
        c.speed -= c.speed * alpha;
        touched = true;
    }
    if (touched) notifyChanged(); // 车速被改 → revision 触碰（下一帧推进照常刷新位置）
}

// t708 ④ 空车被玩家推动（PlayerController.step 走路 / 飞 / 观察者分支统一调；实现见头注释）：玩家脚底
//   水平 AABB（±kCartPushReach）与静止空矿车重叠 + wish 沿轨轴有分量 → 把矿车沿「与 wish 点积最大的轨
//   连接向」推走（speed = kCartPushSpeed，车头转向该连接向 → 之后 tickPushedCarts 磨擦渐停 / 下坡顺坡滑）。
//   已滑行的车（speed≠0）不二次推（防静止站位无限叠速）；被骑的车不推。pickTrackStep 滤 dot<0 / 无轨 →
//   纯反向或无沿 wish 的可走连接 → 不推（wish 垂直于轨轴推不动 —— 机制等价 MC 推静止矿车须沿轨轴）。
bool MinecartManager::pushEmptyCart(World *world, const QVector3D &playerFeet, float wishX, float wishZ)
{
    if (!world) return false;
    const float wishLen = std::sqrt(wishX * wishX + wishZ * wishZ);
    if (wishLen < 1e-3f) return false; // 无输入 → 不推
    const float nwx = wishX / wishLen, nwz = wishZ / wishLen;
    bool pushed = false;
    for (size_t i = 0; i < m_carts.size(); ++i) {
        Cart &c = m_carts[i];
        if (!c.alive) continue;
        if (int(i) == m_riderCart) continue;      // 被骑的车不推（骑乘语义专属）
        if (std::fabs(c.speed) > 1e-3f) continue; // 已滑行的车不二次推（防静止站位无限叠速）
        const float dx = std::fabs(playerFeet.x() - c.pos.x());
        const float dz = std::fabs(playerFeet.z() - c.pos.z());
        if (dx > kCartPushReach || dz > kCartPushReach) continue; // 玩家未实际贴住车
        int ndx = 0, ndz = 0;
        if (!pickTrackStep(world, c.pos, nwx, nwz, ndx, ndz)) continue; // 无沿 wish 的可走连接 → 推不动
        c.dirX = float(ndx);
        c.dirZ = float(ndz);
        c.speed = kCartPushSpeed;
        c.yaw = std::atan2(-c.dirX, -c.dirZ) * 57.2957795f; // 车头转向推入向（-Z 前约定，同 tickRiddenCart）
        while (c.yaw < 0.0f) c.yaw += 360.0f;
        pushed = true;
    }
    if (pushed) notifyChanged(); // 推动即发；后续滑行帧由 tickPushedCarts 持续发位置刷新
    return pushed;
}

void MinecartManager::tickRiddenCart(qreal dt, World *world, float wishX, float wishZ, QVector3D &outCartPos)
{
    if (m_riderCart < 0 || m_riderCart >= int(m_carts.size())) { outCartPos = QVector3D(); return; }
    Cart &c = m_carts[size_t(m_riderCart)];
    if (!c.alive) { outCartPos = QVector3D(); return; }

    // t736：探测轨占用 / 离开沿不再在本函数处理 —— 统一移到 tickPushedCarts 末尾的
    //   updateDetectorRailOccupancy（全车种帧级收口；PlayerController 骑乘分支在调完本函数后必调
    //   tickPushedCarts）。旧版在本函数清占用表 + 只标被骑车 → 骑乘帧邻轨空车占用每帧被误判离开沿
    //   （清位）又由别处重置 → 电力抖动；且空车路径（无人骑乘）完全不触发探测轨。

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

    // t691 轨格层前置钉定：boost / 坡道重力两处旧用 floor(pos.y)-1 派生轨层 —— 坡格上 pos.y = 轨Y+1+
    //   rise+0.30，rise≥0.7 时 floor-1 取到轨**上方**层（约 30% 位置错层 → boost 判定 / 坡修正读空气 →
    //   间歇性失效）。改为与下方钉轨面循环同源的「本列向下扫最近轨格」，全 tick 用同一 pinnedY。
    //   复审 #4：扫描收口到 scanRailColumn（含实心遮挡断扫，与 pinCartY / 探测轨占用同语义）。
    const int railX = int(std::floor(c.pos.x()));
    const int railZ = int(std::floor(c.pos.z()));
    const int railY = world ? scanRailColumn(world, railX, int(std::floor(c.pos.y())), railZ)
                            : -1; // -1 = 列内无轨

    // t734 ③ 离轨静止（防「矿车不在铁轨上仍可被骑着一路悬浮滑到地图边界」）：列内无轨（railY<0）→
    //   钉死速度、跳过全部输入物理与推进 —— 不重选向 / 不 lerp 起步 / 不坡道溜 / 不位移。旧版速度照常
    //   按输入 lerp 起步 + 段内推进不验轨 → 离轨车仍全速直线滑行。地面放置（t734 放宽）/ 轨被拆 / 冲出
    //   轨端的矿车由此统一静止：可 Shift 下车（dismount 不经本函数）+ 左键拾取（hitCartFromRay）。
    if (railY < 0) {
        c.speed = 0.0f;
        outCartPos = c.pos;
        notifyChanged();
        // t736：离轨车的探测轨占用由 tickPushedCarts 末尾的统一 pass 以「上一帧占用 − 本帧占用」收边沿
        //   （本帧被骑车不在任何探测轨上 → 不记占用 → 离开沿自动清位断电）。
        return;
    }

    // 目标速度：wish 在当前行进方向上的投影（前推 / 后拉；无输入 → 0 摩擦滑行渐停）。
    const float wishLen = std::sqrt(wishX * wishX + wishZ * wishZ);
    float proj = 0.0f;
    if (wishLen > 1e-3f) {
        // wish 归一后与 dir 点积 → 前进 / 后退意图 [-1,1]（侧向分量投影 0 → 不侧移，轨约束）。
        proj = (wishX / wishLen) * c.dirX + (wishZ / wishLen) * c.dirZ;
    }
    // t638 ⑤ / t658 修 动力轨加速（spec「动力轨 = 矿车经过提速」，机制等价 MC 1.0 powered rail boost）：
    //   **通电才加速**（t658 前恒 boost —— 无红石系统时代的简化；红石电力系统 v1 落地后改为读轨 state 的
    //   GoldenRailStateOnFlag（bit4，World::tickRedstone 电力重算置 / 清）。机制等价 MC：断电动力轨 = 普通轨
    //   （仅承载不加速），通电动力轨才 boost / 弹射）。矿车当前所在轨格为 GoldenRail 且通电 → 投影改写：
    //   (a) 有前进输入 → 上限提升（proj × boost/kCart → 目标速达 kCartBoostSpeed）；(b) 无输入 → 弹射档
    //   0.35（机制等价 MC 动力轨是「发射器」：停着的矿车驶上动力轨即被弹射向前，无需玩家踩 W）；(c) 反踩
    //   刹车（proj<0）→ 不改写（玩家减速意图优先，动力不反向推）。轨格判定同 pickTrackStep（中心下一格）。
    //   t691：轨层读前置钉定的 railY（非 floor(pos.y)-1）。
    {
        const quint8 gb = (world && railY >= 0) ? world->blockAt(railX, railY, railZ) : quint8(BlockRegistry::Air);
        const bool railPowered = (gb == BlockRegistry::GoldenRail)
            && (world->stateAt(railX, railY, railZ) & BlockRegistry::GoldenRailStateOnFlag) != 0; // t658 通电位
        if (railPowered) {
            if (proj > 1e-3f) {
                proj = proj * (kCartBoostSpeed / kCartSpeed); // 有输入 → 上限提升（proj≤1 → ≤boost）
            } else if (proj > -1e-3f) {
                proj = 0.35f; // 无输入 → 弹射档（机制等价 MC 动力轨弹射停着的矿车）
            } // proj < -0.001（反踩刹车）→ 不改写：玩家减速意图优先
        }
    }
    // t667 坡道重力（机制等价 MC 矿车上坡减速 / 下坡自加速、静止车在下坡上溜车）：
    //   以「行进方向上的邻轨高度差」（railProbeDelta：+1 上坡 / -1 下坡 / 0 平 / INT_MIN 无轨）修正目标
    //   速度 —— 下坡（δ<0）→ 目标抬高到 kCartSlopeDownSpeed（无输入也溜车）；上坡（δ>0）→ 目标按
    //   kCartUphillMul 收窄（须玩家输入推力才能爬）。行进侧按 speed 符号取（负速 = 朝 -dir 走：反向推进、
    //   倒行下坡同样加速倒溜）。静止车在下坡上 → 下坡标志放行下方 movement 闸门起步溜。渲染几何的坡面
    //   高度与矿车 Y 同读 railProbeDelta（钉轨面段），两者粒度一致。t691：轨层读前置钉定 railY。
    float targetV = proj * kCartSpeed;
    bool slopeDownAuto = false; // 静止车下坡起步溜的闸门标志（speed==0 且行进侧下坡 → 允许移动）
    if (world && railY >= 0) {
        const int gs = (c.speed >= 0.0f) ? 1 : -1;
        const int ndx = int(c.dirX) * gs, ndz = int(c.dirZ) * gs;
        const int slope = BlockRegistry::railProbeDelta(
            { world->blockAt(railX + ndx, railY, railZ + ndz),
              world->blockAt(railX + ndx, railY + 1, railZ + ndz),
              world->blockAt(railX + ndx, railY - 1, railZ + ndz) });
        if (slope != INT_MIN && slope < 0) { // t684：INT_MIN（该向无轨 / 死端）必须排除 —— 否则
            //   停在死端 / 孤轨上的静止车把「无轨」当「下坡」→ slopeDownAuto 放行起步闸门 → 自动
            //   冲出轨端悬空一格（速度永远非零 + 推进段指向无轨方向）。只有真下坡（邻轨低 1）才溜车。
            //   t708 ⑤ 负速倒行对称：行进侧下坡（gs<0 = 倒在下坡上）→ 目标往 **-kCartSlopeDownSpeed**
            //   方向抬（倒溜加速），不把倒退意图掰成正向（旧版 max 会把「倒行下坡」改成正推）。
            slopeDownAuto = true;
            targetV = (gs > 0) ? std::max(targetV, kCartSlopeDownSpeed)
                               : std::min(targetV, -kCartSlopeDownSpeed);
        }
        else if (slope > 0) targetV *= kCartUphillMul; // 上坡减速（正倒行同款收窄）
    }
    // 速度 lerp 接近目标（加速 / 摩擦统一：目标 0 时按 kCartFriction 衰减；目标 ±速时按 kCartAccel 接近）。
    {
        const float rate = (std::fabs(targetV) > 1e-3f) ? kCartAccel : kCartFriction;
        const float alpha = 1.0f - std::exp(-rate * float(dt));
        c.speed += (targetV - c.speed) * alpha;
        if (std::fabs(c.speed) < 0.02f) c.speed = 0.0f; // 死区归零（防微速漂移）
    }

    // 沿轨推进：以「格中心到格中心」的插值段推进（跨格时重选连接向 → 拐角自动转弯）。
    //   段 = 当前格中心 → 沿 dirX/dirZ 的下一轨格中心；到段末（进入新格中心）时重选下一连接向。
    //   t667：下坡起步（speed==0 且行进侧下坡）也进本闸门 —— 静止车在坡上受力溜车，无需玩家输入。
    //   t708 ⑤：S 后退 —— proj<0 → 目标速负 → lerp 出负速 → stepCartAlongRail 负速段沿 -dir 倒行
    //   （旧版推进循环只看正 remain，负速不进循环 = S 只减速不后退，须转身向 W 才动 —— 实测症状根因）。
    if (c.speed != 0.0f || slopeDownAuto) {
        // 静停下坡起步：补一个初始化速度（direct target = kCartSlopeDownSpeed；不给 0 起步死区吃掉）。
        if (c.speed == 0.0f) c.speed = kCartSlopeDownSpeed * 0.05f;
        // 共享沿轨推进（正行跨格重选连接向 / 负速倒行 / 出轨停）。
        stepCartAlongRail(c, world, float(dt));
        // Y 钉轨面（t667 坡道感知）：矿车所在列向下找轨格（中心格或下一格）→ pos.y = 轨格 cell 顶 + **坡面高** +
        //   kCartRideH。坡面高 = cell 内横向位置上的邻轨抬升叠加（只抬 δ>0 —— 高端平铺、低端画坡；与
        //   PartialBlockGeometry Rail case 的 riseAtX/riseAtZ 同公式同语义、同读 railProbeDelta → 渲染坡面
        //   与矿车高度严格一致，无「贴贴图坡了车没坡」）。跨格推进期间按像素级横向位置连续升降（下坡顺滑 /
        //   上坡爬升），不再跨格跳变。t638：轨判定扩 isRail 家族（普通 / 动力 / 探测轨同钉轨面）。
        //   **t691：rise 只叠本轴**（与 mesher 严格镜像）：mesher 对直轨只读行进轴的 riseAtX（EW）或
        //   riseAtZ（NS）——旧版矿车把 4 向 delta 全叠 → 垂直邻线（邻列同层轨的 ±X/±Z 探针命中）凭空抬车
        //   0.5，且坡后 30% 位置（rise≥0.7）floor(pos.y)-1 的三处派生（boost 判定 / 坡道重力 / 探测轨）取错
        //   轨层。轴取法与 mesher 同源：state 连接位（cpx||cnx → X 轴；0 连接读 RailAxisEWFlag 轴偏好）。
        if (world) {
            // t708：钉轨面提取为共享 helper pinCartY（空车被推 tickPushedCarts 同一 Y 钉定）。
            //   t736：探测轨占用判定不再在此做 —— 统一移 tickPushedCarts 末尾 updateDetectorRailOccupancy
            //   （全车种帧级收口；此处只保留 Y 钉定物理）。
            pinCartY(c, world);
        }
        // 车头朝向（-Z 前约定，同 PlayerController horizontalFacing / 相机 yaw）：yaw = atan2(-dirX,-dirZ)
        //   → dir=(0,-1) → 0°；(0,1) → 180°；(-1,0) → 90°；(1,0) → 270°（车头本地 -Z 经 R_y(yaw) 旋转后
        //   指向 dir；与 QML cartRoot eulerRotation.y 直连，dir 与渲染朝向严格一致）。负速倒行时 dir 不变
        //   → 车头保持原朝向（车底朝后退行），跨格重选向（正行转弯）才更新。
        c.yaw = std::atan2(-c.dirX, -c.dirZ) * 57.2957795f;
        while (c.yaw < 0.0f) c.yaw += 360.0f;
    }
    // t680 ③ 停稳（speed==0 且非下坡起步）被骑车的探测轨占用：t736 起同由统一 pass 收口（占用是「位置」
    //   语义而非「移动」语义，机制等价 MC 探测轨上静止矿车恒供电 —— 统一 pass 遍历全部活体车不看速度，
    //   停驶车照常占用，语义保留）。

    outCartPos = c.pos;
    notifyChanged();
}

// t736 探测轨占用统一重扫（每帧一次，tickPushedCarts 末尾调；头注释见 minecartmanager.h）：快照上一帧
//   占用表 → 清空重建：遍历全部活体矿车（被骑 / 空车 / 停驶车全车种 —— 机制等价 MC 1.0 detector rail
//   对任何矿车输出信号，t658 旧版只标被骑路径），列内向下扫最近轨格（同 pinCartY / 旧停驶分支的「列内
//   最近一轨」扫描语义，窗口 ±2 覆盖坡顶 rise 造成的 pos.y 抬层）为探测轨 → **幂等**置 state bit4
//   （DetectorRailStateOnFlag；已置不重写 —— 车驻轨期间每帧零 state 写，setWaterSilent 只在置位 / 清位
//   沿各发生一次）+ 记占用键。末尾 updateDetectorRailEdges 以「上一帧占用 − 本帧占用」清离开沿断电。
void MinecartManager::updateDetectorRailOccupancy(World *world)
{
    if (!world) return;
    const std::unordered_set<quint64> prev = m_detectorOccupied;
    m_detectorOccupied.clear(); // 重建前清空（漏清则集合只增不减 →「离开沿」永不触发 → 一次通电永久带电）
    for (size_t i = 0; i < m_carts.size(); ++i) {
        const Cart &c = m_carts[i];
        if (!c.alive) continue; // 车被挖毁 / clearAll 释放 → 不记占用 → 下一帧离开沿自动断电
        const int bcx = int(std::floor(c.pos.x()));
        const int bcz = int(std::floor(c.pos.z()));
        // 复审 #4：列扫描收口 scanRailColumn（含实心遮挡断扫）—— 地面静止车隔着实心地板不再点亮
        //   下方探测轨隧道（MC 探测轨只响应压在自己身上的车；坡顶场景格与轨之间只隔空气不受影响）。
        const int y = scanRailColumn(world, bcx, int(std::floor(c.pos.y())), bcz);
        if (y >= 0 && world->blockAt(bcx, y, bcz) == BlockRegistry::DetectorRail) {
            const quint8 ds = world->stateAt(bcx, y, bcz);
            if ((ds & BlockRegistry::DetectorRailStateOnFlag) == 0) {
                // 置位沿：setWaterSilent（静默 + notePowerWrite → m_powerDirty → tickRedstone 把
                //   bit4 读作电源 15 向 6 邻供能，world.cpp powerSourceLevel；不发 blockPlaced）。
                world->setWaterSilent(bcx, y, bcz, BlockRegistry::DetectorRail,
                                      quint8(ds | BlockRegistry::DetectorRailStateOnFlag));
            }
            m_detectorOccupied.insert(packRailCell(bcx, y, bcz)); // 本帧占用（下帧边沿比较基线）
        }
    }
    updateDetectorRailEdges(world, prev);
}

// t658 探测轨占用边沿收尾（t736 起由 updateDetectorRailOccupancy 末尾调；prev = 本帧开头快照的上一帧
//   占用表，cur = 本帧重建的占用表（m_detectorOccupied））：上一帧占用、本帧不再占用的探测轨 → 清
//   state bit4（DetectorRailStateOnFlag）断电（机制等价 MC 1.0 detector rail 矿车离开即断；t638「压过
//   后保持亮」占位语义由电力系统取代）。经 setWaterSilent 静默写（notePowerWrite → 电力重算把下游接收器
//   断电）。prev 空 → 零开销早退（无探测轨场景每帧仅一次判空）。表键与 powerTnt 信号等均世界坐标打包。
void MinecartManager::updateDetectorRailEdges(World *world,
                                              const std::unordered_set<quint64> &prev)
{
    if (prev.empty()) return; // 上一帧无占用 → 无离开沿
    if (!world) return;
    for (const quint64 k : prev) {
        if (m_detectorOccupied.count(k)) continue; // 本帧仍占用 → 非离开沿
        int x, y, z;
        unpackRailCell(k, x, y, z);
        const quint8 b = world->blockAt(x, y, z);
        if (b != BlockRegistry::DetectorRail) continue; // 轨被拆（setWaterSilent 已触发电力重算）→ 跳过
        const quint8 st = world->stateAt(x, y, z);
        if ((st & BlockRegistry::DetectorRailStateOnFlag) != 0) {
            world->setWaterSilent(x, y, z, BlockRegistry::DetectorRail,
                                  quint8(st & quint8(~BlockRegistry::DetectorRailStateOnFlag)));
        }
    }
}

bool MinecartManager::hitCartFromRay(const QVector3D &origin, const QVector3D &dir, float maxDist,
                                     World *world, bool instantBreak)
{
    float dist = 0.0f;
    const int idx = findCartHit(origin, dir, maxDist, &dist);
    if (idx < 0 || idx >= int(m_carts.size()) || !m_carts[size_t(idx)].alive) return false;
    Cart &c = m_carts[size_t(idx)];
    // t735 ② 生存耐久：非最后一击 → 只扣血 + 受击摇晃（notifyChanged bump revision → 呈层 hpAt 绑定
    //   重算 → delegate onCartHpChanged 触发摇晃动画），不掉落不摧毁。创造（instantBreak，caller 按
    //   m_mode==Creative 传）跳过耐久直接摧毁（同方块创造单击瞬破语义）。连击节奏由 caller 的
    //   kAttackCooldown（0.5s）门控 → kCartHitPoints 击约 1.5s 打毁一辆。
    if (!instantBreak && c.hp > 1) {
        c.hp -= 1;
        notifyChanged();
        return true;
    }
    // 摧毁（生存最后一击 / 创造瞬破）：移除该矿车 + 清骑乘态（若挖的是被骑的矿车）+ emit cartBroken
    //   （→ 呈层 spawnItem 掉 MinecartId，可重放；机制等价 MC 1.0 攻击矿车 → 矿车破坏掉矿车物品）。
    const QVector3D cp = c.pos;
    if (idx == m_riderCart) m_riderCart = -1; // 挖骑乘中的矿车 → 玩家自然下车
    releaseSlot(idx);
    // t735 ① 掉落格散布（同船 t711 修法）：旧版掉「矿车中心格」—— 轨格非实心、玩家可与车同格 / 紧邻，
    //   掉落物常落在攻击者本人 kPickupDist 1.5 半径内 → 0.5s 免拾窗一过被 pickupScan 立即吸回（背包静默
    //   +1）→ 用户全程看不到掉落物 = 观感「不掉落」（根因是掉落点选格与攻击者重合，非创造模式跳过掉落
    //   —— hitCartFromRay 全模式同路径；同船 t661 排查结论的矿车族）。改掉「首个非实心水平邻格」随机
    //   一格（邻格上方一格也须非实心，防掉进 1 格深坑壁内）；4 邻全实心（窄缝嵌车）→ 掉车中心格上一格
    //   （y+1，自重落顶不埋）。world 空（防御路径）→ 保留旧中心格行为。
    int dropX = int(std::floor(cp.x())), dropY = int(std::floor(cp.y())), dropZ = int(std::floor(cp.z()));
    if (world) {
        static constexpr int kDropNb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        int cand[4] = {-1, -1, -1, -1};
        int nCand = 0;
        for (int n = 0; n < 4; ++n) {
            const int nx = dropX + kDropNb[n][0], nz = dropZ + kDropNb[n][1];
            if (!world->isCollidable(nx, dropY, nz) && !world->isCollidable(nx, dropY + 1, nz))
                cand[nCand++] = n;
        }
        if (nCand > 0) {
            const int nb = cand[QRandomGenerator::global()->bounded(nCand)];
            dropX += kDropNb[nb][0];
            dropZ += kDropNb[nb][1];
        } else {
            dropY += 1; // 4 邻全实心（窄缝嵌车）→ 掉头顶上一格（自重落顶，不埋）
        }
    }
    emit cartBroken(dropX, dropY, dropZ);
    notifyChanged();
    return true;
}
