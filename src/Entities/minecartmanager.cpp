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
        // t769 放置即贴坡：初始俯仰按轨面几何取（坡格中心 ±0.25 采样 → 1:1 坡恰 45°；平格 / 拐角 0）——
        //   停驻车（speed==0 不进 tick 推进）放置在坡上即刻平行轨面，无需先行驶一段。
        updateCartPitch(c, world, y);
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

// t769 俯仰读口（呈现层 delegate eulerRotation.x 直连；见头注释）。
float MinecartManager::pitchAt(int i) const
{
    if (i < 0 || i >= int(m_carts.size()) || !m_carts[size_t(i)].alive) return 0.0f;
    return m_carts[size_t(i)].pitch;
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
    // t811 满员拒载：矿车乘员总数限 1（玩家 XOR 生物）—— 生物已占座 → 拒玩家上（返 false 不改态；
    //   机制等价 t811 spec「矿车限 1 只」总数口径：玩家占了 mob 不上，mob 占了玩家也不上）。
    if (idx >= 0 && idx < int(m_carts.size()) && m_carts[size_t(idx)].mobPassenger >= 0) return false;
    m_riderCart = idx;
    notifyChanged();
    return true;
}

// ── t811 生物乘客座位（mob 自动登乘；头注释见 .h。越界 / 空槽安全默认，无 QML 消费不 bump revision）──
int MinecartManager::mobPassengerAt(int i) const
{
    if (i < 0 || i >= int(m_carts.size()) || !m_carts[size_t(i)].alive) return -1;
    return m_carts[size_t(i)].mobPassenger;
}

void MinecartManager::seatMob(int i, int mobIdx)
{
    if (i < 0 || i >= int(m_carts.size())) return;
    m_carts[size_t(i)].mobPassenger = mobIdx;
}

void MinecartManager::clearMobPassenger(int i)
{
    if (i < 0 || i >= int(m_carts.size())) return;
    m_carts[size_t(i)].mobPassenger = -1;
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

// 复审 #4 / 复审 #2（2026-08-23）：列内向下扫最近可达轨格。**严格版**（实心遮挡即断）自复审 #2 起仅
//   探测轨占用一处消费 —— 隔板供电防线（地面车隔着实心地板点亮下方探测轨隧道由此拒）。骑乘族消费端
//   （pinCartY / pickTrackStep / tickRiddenCart 前置钉定 / railSurfaceYAt / clampShift）走下方宽容版。
//   遮挡判定 World::isCollidable（Core isCollidable 单一权威：轨 / 火把 / 水 / 花草 ShapeNone 恒 false
//   不遮挡；LilyPad 特例可踩碰撞当遮挡 —— 矿车压睡莲列本就非法，可接受）。
int MinecartManager::scanRailColumn(World *world, int cx, int topY, int cz) const
{
    if (!world) return -1;
    for (int y = topY; y >= topY - 2 && y >= 0; --y) {
        if (BlockRegistry::isRail(world->blockAt(cx, y, cz))) return y;
        if (world->isCollidable(cx, y, cz)) break; // 实心遮挡 → 更下方轨不可达（隔板供电拒）
    }
    return -1;
}

// 复审 #2（2026-08-23）宽容版列扫描（骑乘族专用；语义见头注释）：与严格版唯一差异在「扫描顶格 = 实心」
//   的一类 —— 坡道低顶净空（1 格隧道 / 紧凑螺旋 / 多层轨道下层坡段，t775 确立玩法）里车位居上：
//   rise>0.55 时 floor(pos.y) = 轨Y+1 恰是天花板实心格，严格断扫 → 坡上死车 / 俯仰清零 / 采样失联
//   （复审 #2 三症状同根因）。宽容规则：实心格正下方是轨 且 「轨Y + railRiseAt(fx,fz) + kCartRideH」
//   与 pos.y 一致（≤kRideScanTol）→ 判该实心是「紧贴轨上沿的天花板」而非隔板，放行返回该轨。
//   一致性校验承重（af9ec8e「地面车隔地板钉到下方轨」的回归防线全靠它）：
//   - 地面车：pos.y = 地板Y + kCartGroundH(0.3875)，对地板下平轨骑乘高差 = 1 + 0.3875 − 0.45 = 0.9375
//     >> 0.5 → 恒拒（隔板语义保留；例外仅地板下恰为 rise>~0.94 坡段 —— 轨面已贴近地板顶，判可达与
//     渲染面一致，见 kRideScanTol 注释）；
//   - 轨上车：pos.y 正是 pinCartY 用同一公式钉出 → 帧首扫描（fx 未变）差恰 0；stepCartAlongRail 子步
//     中 / 俯仰 ±0.25 采样点的差 ≤ railRiseAt 值域 [0,1]（容差覆盖）。
//   注：实心格下方是**空气**再下方才是轨（隔板 + 空隙 + 隧道轨）仍拒 —— 与隔板不可区分，低顶下坡采样
//   前探偶发失联由 updateCartPitch 的归 0 兜底（纯呈现，<2 帧窗口）。
int MinecartManager::scanRailColumnRiding(World *world, int cx, int cz, int topY, float fx, float fz, float refY) const
{
    if (!world) return -1;
    for (int y = topY; y >= topY - 2 && y >= 0; --y) {
        if (BlockRegistry::isRail(world->blockAt(cx, y, cz))) return y;
        if (world->isCollidable(cx, y, cz)) {
            // 宽容窗口：实心正下一格是轨 + 骑乘高一致 → 低顶净空放行；否则同严格版断扫。
            if (y - 1 >= 0 && BlockRegistry::isRail(world->blockAt(cx, y - 1, cz))) {
                const float h = float(y - 1) + railRiseAt(world, cx, y - 1, cz, fx, fz) + kCartRideH;
                if (std::fabs(refY - h) <= kRideScanTol) return y - 1;
            }
            return -1;
        }
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
    //   复审 #4：扫描收口到 scanRailColumn（含实心遮挡断扫）。复审 #2：骑乘族改宽容版 scanRailColumnRiding
    //   （低顶净空坡段车位居上时 floor(pos.y)=实心天花板 → 严格断扫会把坡中格心重选判死 → 坡上死车）。
    const int rx = int(std::floor(cartPos.x()));
    const int rz = int(std::floor(cartPos.z()));
    const int ry = scanRailColumnRiding(world, rx, rz, int(std::floor(cartPos.y())),
                                        cartPos.x() - float(rx), cartPos.z() - float(rz), cartPos.y());
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
    //   t769 坡道修真：存在性按**三高探针**（同 railConnections 的 anyRail / railProbeDelta：同层 / 上 / 下
    //   任一为轨即有轨）—— 坡连接的邻轨在 ry±1（轨格层与连接位同源三高探针写入），旧版只查同层 ry →
    //   上坡在坡脚格心 / 下坡在坡顶格心的重选一律误判「连接位失真」，且下方兜底重选也只查同层 → 返
    //   false → 矿车在坡段两端的格心停死（用户报「上不去也下不去」根因；矩阵测试 P12b 复现）。
    const auto railAt3 = [&](int dx, int dz) {
        return BlockRegistry::isRail(world->blockAt(rx + dx, ry, rz + dz))
            || BlockRegistry::isRail(world->blockAt(rx + dx, ry + 1, rz + dz))
            || BlockRegistry::isRail(world->blockAt(rx + dx, ry - 1, rz + dz));
    };
    if (!railAt3(kDirs[best].dx, kDirs[best].dz)) {
        // 连接位失真：直接按邻格实查重选（首查四邻中与 want 点积最大、非反向且为铁轨者 —— 同三高探针）。
        int fb = -1; float fDot = -1.0f;
        for (int i = 0; i < 4; ++i) {
            if (!railAt3(kDirs[i].dx, kDirs[i].dz)) continue;
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
//   复审 #2：骑乘 Y 钉定改宽容版（低顶净空坡段不判死车；地面车隔板拒由宽容版一致性校验承担，语义同旧）。
//   t769：坡面高计算抽到 railRiseAt（Y 钉定 / 俯仰采样共用同一张面）。
int MinecartManager::pinCartY(Cart &c, World *world)
{
    if (!world) return -1;
    const int bcx = int(std::floor(c.pos.x()));
    const int bcz = int(std::floor(c.pos.z()));
    const int y = scanRailColumnRiding(world, bcx, bcz, int(std::floor(c.pos.y())),
                                       c.pos.x() - float(bcx), c.pos.z() - float(bcz), c.pos.y());
    if (y < 0) return -1;
    const float fx = c.pos.x() - float(bcx); // [0,1) cell 内横向位置
    const float fz = c.pos.z() - float(bcz);
    const float rise = railRiseAt(world, bcx, y, bcz, fx, fz);
    c.pos.setY(float(y) + rise + kCartRideH); // t734：轨板贴 cell 底（mesher yr=1/16），去掉旧 +1.0 格顶基准
    return y;
}

// t769 轨格内坡面高（从 pinCartY 抽出的纯查询；头注释见 minecartmanager.h）：连接位定行进轴（mesher
//   同源：EW（±X 连接）→ riseAtX、NS → riseAtZ；0 连接读 RailAxisEWFlag 轴偏好），格内 (fx,fz) 上邻轨
//   抬升叠加（只抬 δ>0）。t691 轴判定的原注释语义保留：mesher 对直轨只读本轴 rise —— 垂直邻线探针命中
//   不抬车。拐角 / 十字无坡（mesher 同判）；拐角取 armLift 四角抬升的双线性插值（复审 #3，见分支内
//   注释）；V 形凹谷（t710 两端皆 +1）取 2|轴-0.5|。
float MinecartManager::railRiseAt(World *world, int bcx, int y, int bcz, float fx, float fz)
{
    const auto dlt = [&](int dx, int dz) {
        return BlockRegistry::railProbeDelta(
            { world->blockAt(bcx + dx, y, bcz + dz),
              world->blockAt(bcx + dx, y + 1, bcz + dz),
              world->blockAt(bcx + dx, y - 1, bcz + dz) });
    };
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
        // t709 坡臂拐角 + 复审 #3（2026-08-23）双轴插值：mesher 拐角 quad 角点高 = 该角触及的两侧臂抬升
        //   之和（partialblockgeometry.cpp armLift：(0,0)=eW+eN / (1,0)=eE+eN / (1,1)=eE+eS / (0,1)=eW+eS，
        //   双臂齐抬的峰角取和）。旧版取四边均值**常数** → 与相邻直臂格的线性坡面在格边界不连续（复审 #3：
        //   俯仰采样窗跨界 atan2(-0.75,0.5)≈-56° 车头瞬甩 + 同帧 Y 钉面跳降 0.75）。改四角双线性后拐角内
        //   随 (fx,fz) 连续、边界值与直臂 riseAtX/riseAtZ 严格衔接（渲染面 / 矿车 Y / 俯仰采样同一张面）；
        //   平地拐角四角全 0 → rise 0 语义不变。
        const int dpx = dlt(1, 0), dnx = dlt(-1, 0), dpz = dlt(0, 1), dnz = dlt(0, -1);
        const float eE = dpx > 0 ? float(dpx) : 0.0f, eW = dnx > 0 ? float(dnx) : 0.0f;
        const float eS = dpz > 0 ? float(dpz) : 0.0f, eN = dnz > 0 ? float(dnz) : 0.0f;
        const float h00 = eW + eN, h10 = eE + eN, h11 = eE + eS, h01 = eW + eS;
        rise = (h00 * (1.0f - fx) + h10 * fx) * (1.0f - fz)
             + (h01 * (1.0f - fx) + h11 * fx) * fz;
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
    return rise;
}

// t769 轨道面高度采样（头注释见 minecartmanager.h；俯仰角计算用 —— 采到的正是 pinCartY 所钉的同一张面）。
//   复审 #2：列扫描改宽容版 —— 低顶净空坡段（天花板贴轨上沿）采样列同样从实心格起步，严格断扫会把
//   坡中段俯仰采样整段判失联 → 45° 爬坡车头一帧跳回水平（复审 #2 症状②）。refY = 车当前 pos.y：采样点
//   距车心 ≤0.25 → 两点轨面差 ≤坡度×0.25 ≤0.5（kRideScanTol 容差窗内）。
bool MinecartManager::railSurfaceYAt(World *world, float sx, float sz, int topY, float refY, float &outY) const
{
    if (!world) return false;
    const int cx = int(std::floor(sx));
    const int cz = int(std::floor(sz));
    const int y = scanRailColumnRiding(world, cx, cz, topY, sx - float(cx), sz - float(cz), refY);
    if (y < 0) return false;
    outY = float(y) + railRiseAt(world, cx, y, cz, sx - float(cx), sz - float(cz));
    return true;
}

// t863① 坡上失速反溜起步（头注释见 .h）：车头向 ±kCartPitchProbe 两点轨面采样高差判「车头朝上坡面」。
// 采样列扫描窗 [railY+1, railY-1]（采样点距车心 ≤0.25 → 至多邻列，坡步进 ±1 恒覆盖）；任一端失联
//（死端前探 / 拐角）→ 不起步（保守停驻，机制等价 MC 坡顶平段停）。
bool MinecartManager::tryStallSlideback(Cart &c, World *world, int railY)
{
    if (!world || railY < 0) return false;
    float hF = 0.0f, hB = 0.0f;
    if (!railSurfaceYAt(world, c.pos.x() + c.dirX * kCartPitchProbe,
                        c.pos.z() + c.dirZ * kCartPitchProbe, railY + 1, c.pos.y(), hF))
        return false;
    if (!railSurfaceYAt(world, c.pos.x() - c.dirX * kCartPitchProbe,
                        c.pos.z() - c.dirZ * kCartPitchProbe, railY + 1, c.pos.y(), hB))
        return false;
    if (hF - hB <= 0.05f) return false;     // 平面 / 车头朝下坡（后者由 slopeDownAuto 既有起步溜承担）
    c.speed = -kCartStallKick;              // 反溜起步（沿 -dir 倒行；下坡侧重力目标接管加速）
    return true;
}

// t863② 地面 / 薄支撑真顶探测（头注释见 .h）：t865/t867 同族 —— World::supportTopYAt 碰撞真顶单一权威。
float MinecartManager::groundSupportTopWithin(World *world, float x, float z,
                                              int topCellY, float refBottom) const
{
    if (!world) return -1.0f;
    const int cx = int(std::floor(x)), cz = int(std::floor(z));
    for (int y = topCellY; y >= topCellY - 1 && y >= 0; --y) {
        const float top = world->supportTopYAt(cx, y, cz);
        if (top >= 0.0f && top <= refBottom + 0.05f) return top; // 脚下 / 齐平的支撑面（更高 = 嵌入非支撑）
    }
    return -1.0f;
}

// t863③ 自由物理 tick（出轨 / 坠落态；头注释见 .h）。被骑（tickRiddenCart 首段分流）与空车
//（tickPushedCarts 循环首段分流）共用：水平 dir×speed 平抛 + 重力 + 落地（轨面重挂 / 地面真顶贴面）。
void MinecartManager::tickDerailedCart(int idx, Cart &c, World *world, float dt)
{
    if (!world) { c.speed = 0.0f; return; }
    // ── 垂直：重力 + 落地扫描（自底扫落径，防大 dt 穿薄层；lessons「子步防穿墙」精神）──
    c.fallVy -= kCartFallGravity * float(dt);
    if (c.fallVy < -kCartFallMax) c.fallVy = -kCartFallMax;
    const float newY = c.pos.y() + c.fallVy * float(dt);
    const float botFrom = c.pos.y() - kCartGroundH; // 本帧前车底
    const float botTo = newY - kCartGroundH;
    const int cx = int(std::floor(c.pos.x())), cz = int(std::floor(c.pos.z()));
    bool landed = false; // 本帧落定地面（摩擦适用；轨面重挂则直接退出本函数）
    for (int cy = int(std::floor(botFrom)); cy >= int(std::floor(botTo)); --cy) {
        if (cy < 0) break;
        // (a) 轨面重挂：本帧前车底**严格在轨面上方**（> +0.01 余量 —— 站轨被推离〔④〕的车 bot ≈ 轨面
        //     不满足，不误挂）且本帧落径穿过骑乘面 → 重挂轨道态（速度 / 朝向保留，stepCartAlongRail
        //     下一格心重选连接向；机制等价 MC 1.0 飞出的矿车落回轨道继续跑）。
        if (BlockRegistry::isRail(world->blockAt(cx, cy, cz))) {
            const float rideTop = float(cy)
                + railRiseAt(world, cx, cy, cz, c.pos.x() - float(cx), c.pos.z() - float(cz))
                + kCartRideH;
            if (botFrom > rideTop + 0.01f && botTo <= rideTop) {
                c.derailed = false;
                c.fallVy = 0.0f;
                c.pos.setY(rideTop);
                updateCartPitch(c, world, cy);
                return;
            }
            continue; // 轨格但未下穿骑乘面（同格站轨）→ 不当地面支撑（supportTopYAt 轨 -1 同义），继续下扫
        }
        // (b) 地面真顶：落径穿过 → 贴面落定（速度保留滑行，摩擦下方统一施）。
        const float top = world->supportTopYAt(cx, cy, cz);
        if (top >= 0.0f && botTo <= top) {
            c.pos.setY(top + kCartGroundH);
            c.fallVy = 0.0f;
            landed = true;
            break;
        }
    }
    if (!landed) {
        c.pos.setY(newY); // 自由下落继续
        // 虚空：跌出世界底部 → 无掉落移除（mob void-loss 同款兜底；防永久下落每帧刷 emit）。
        if (c.pos.y() < 0.0f) { destroyCartTail(idx, world, /*survivalDrop=*/false); return; }
    }
    // ── 水平：dir×speed 积分 + 撞可碰撞格清零（车身格 = 中心下一格；撞墙清速度顺墙停）──
    //   t907 子步化（spec「任何弹射不得越实体墙」）：旧版一步欧拉（nx = pos + dir·speed·dt）只验
    //   **落点格** —— dt 尖峰（16ms 定时器饿死 / 调试断点 / 帧尖，t904 实测 tick 可被饿到 1/3）下
    //   挤压弹射车（boost 上界 12.8 blocks/s）单步可跨 >1 格，1 格厚墙恰被「跳过」（落点在墙后开格
    //   → isCollidable(false) → 整步放行 = 飞出牢笼）。改 ≤0.45 格子步循环（<半格，恒逐格墙检；
    //   子步数上界 = 步长 / 0.45 向上取整，常态 1 子步零开销），撞墙清速立即终止。
    if (std::fabs(c.speed) > 1e-4f) {
        constexpr float kDerailStepMax = 0.45f; // 子步上限（<半格：单子步不可能跨过 1 格厚墙）
        float remainH = std::fabs(c.speed) * float(dt);
        const float sgnH = (c.speed >= 0.0f) ? 1.0f : -1.0f;
        const int byc = int(std::floor(c.pos.y() - 0.1f)); // 车身腰位格（撞半格高台阶也挡）
        while (remainH > 1e-5f) {
            const float stepH = std::min(remainH, kDerailStepMax);
            remainH -= stepH;
            const float nx = c.pos.x() + c.dirX * sgnH * stepH;
            const float nz = c.pos.z() + c.dirZ * sgnH * stepH;
            if (world->isCollidable(int(std::floor(nx)), byc, cz)) { c.speed = 0.0f; break; }
            c.pos.setX(nx);
            if (world->isCollidable(int(std::floor(c.pos.x())), byc, int(std::floor(nz)))) { c.speed = 0.0f; break; }
            c.pos.setZ(nz);
        }
    }
    // 落地 / 接地摩擦（每 tick 落定 snap 重置 fallVy → 接地恒走 landed 路径）：exp 衰减 + 死区停驻。
    if (landed && std::fabs(c.speed) > 1e-4f) {
        const float alpha = 1.0f - std::exp(-kCartFriction * float(dt));
        c.speed -= c.speed * alpha;
        if (std::fabs(c.speed) < 0.02f) c.speed = 0.0f;
    }
}

// t769 车身俯仰刷新（坡道平行轨面，纯呈现 —— 不反馈物理；头注释见 minecartmanager.h）：以车心为基准、
//   沿车头向（dir，负速倒行车头不变 → 俯仰跟车头不跟行进）±kCartPitchProbe 两点采样轨面高 →
//   pitch = atan2(前-后, 2·probe)（正 = 车头上扬 —— 与 EntityManager::arrowPitchAt 同约定，QML
//   eulerRotation.x 直连）。采样窗语义：坡中段窗全落坡格 → 1:1 坡恰 ±45°；跨段折缝窗横跨两段 → 过渡带
//   内线性（轨面 H 分段线性且连续 → 俯仰随位置连续，无阶跃、与车速 / 帧率无关、无需时间平滑）；车头
//   翻转（S 反推 / 停驻重选向）→ 窗对调 → 符号自动翻转。平轨 / 拐角两采样等高 → 0。采样列无可达轨
//   （死端前探 / 离轨防御）→ 归 0（水平摆）。railY = 车所在列轨层：采样列扫描窗 [railY+1, railY-1]
//   覆盖坡步进 ±1（采样点距车心 ≤0.25 → 至多邻列）。
//   复审 #3：真坡面上界 = 1:1 坡 ±45°（V 谷窗内 ≤~39° / 拐角双线性窗内 ≤45°）→ 钳 kCartPitchMaxDeg 防
//   病态采样窗（探针跨界断层 / 拐角跨边界）冒 ±56° 级幻象俯仰（车头视觉猛甩）；纯呈现护栏，不改判据。
void MinecartManager::updateCartPitch(Cart &c, World *world, int railY)
{
    if (!world || railY < 0) { c.pitch = 0.0f; return; }
    float hF = 0.0f, hB = 0.0f;
    const bool okF = railSurfaceYAt(world, c.pos.x() + c.dirX * kCartPitchProbe,
                                    c.pos.z() + c.dirZ * kCartPitchProbe, railY + 1, c.pos.y(), hF);
    const bool okB = railSurfaceYAt(world, c.pos.x() - c.dirX * kCartPitchProbe,
                                    c.pos.z() - c.dirZ * kCartPitchProbe, railY + 1, c.pos.y(), hB);
    if (!okF || !okB) { c.pitch = 0.0f; return; }
    const float deg = qRadiansToDegrees(std::atan2(hF - hB, 2.0f * kCartPitchProbe));
    c.pitch = std::max(-kCartPitchMaxDeg, std::min(kCartPitchMaxDeg, deg));
}

// t708 沿轨推进（共享：被骑 / 空车被推同一物理；实现见头注释）。负速 = 倒行（沿 -dir，车头保持原朝向）。
//   t863③ 轨端飞出：死端（到心 / 起步重选失败）时 |speed| ≥ kCartLaunchMinSpeed → 转 derailed 自由物理
//   （水平 dir×speed 平抛保持 + 重力下坠，不自动暂停；机制等价 MC 1.0 矿车冲出轨道末端飞行）；
//   速度不足 → 停驻（原「轨尽头停」语义保留）。
void MinecartManager::stepCartAlongRail(Cart &c, World *world, float dt)
{
    if (!world) { c.speed = 0.0f; return; }
    const auto deadEnd = [&](float tvx, float tvz) {
        // t863③ 坡顶飞出（spec「坡顶前端无轨 + 速度够 → 飞出做平抛，速度不足才停驻」）：**坡顶死端**（本
        //   格后邻轨低一格 = 刚爬升到顶，行进向前端无轨）+ |speed| ≥ kCartLaunchMinSpeed → 出轨平抛
        //   （水平 dir×speed 保持 + 重力下坠，不自动暂停；机制等价 MC 1.0 矿车冲出爬坡顶端飞行）。平轨
        //   死端 / 速度不足 → 停驻（原「轨尽头停」语义保留 —— 车站死端接住矿车的既有玩法面，t769/t811
        //   等死端停驻探针不动；平端交互由 t863④ 玩家推离承担）。
        if (std::fabs(c.speed) >= kCartLaunchMinSpeed) {
            const int bcx = int(std::floor(c.pos.x())), bcz = int(std::floor(c.pos.z()));
            const int bry = scanRailColumnRiding(world, bcx, bcz, int(std::floor(c.pos.y())),
                                                 c.pos.x() - float(bcx), c.pos.z() - float(bcz),
                                                 c.pos.y());
            if (bry >= 0) {
                // 后邻（行进反侧）三高探针：轨低一格（delta -1）= 爬升到顶的死端。
                const int back = BlockRegistry::railProbeDelta(
                    { world->blockAt(bcx - int(tvx), bry, bcz - int(tvz)),
                      world->blockAt(bcx - int(tvx), bry + 1, bcz - int(tvz)),
                      world->blockAt(bcx - int(tvx), bry - 1, bcz - int(tvz)) });
                if (back == -1) {
                    c.derailed = true;  // 坡顶飞出（下一 tick 起 tickDerailedCart 自由物理）
                    c.fallVy = 0.0f;
                    c.pitch = 0.0f;     // 自由飞行无轨面俯仰（落地重挂 / 贴面后重算）
                    return;
                }
            }
        }
        c.speed = 0.0f;     // 平死端 / 速度不足 → 停驻
    };
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
            if (!pickTrackStep(world, c.pos, tx, tz, vdx, vdz)) { deadEnd(tx, tz); return; }
            // t770 ①：起步重选向结果**双符号持久化**（旧版 sgn<0 不写回 dir → 倒退起步过弯只改本帧局部
            //   tx/tz，下一帧 travel 仍按旧轴横切出轨）：正行 dir=新臂（t737 车头同步语义）；倒行 dir=
            //   **新臂取反**（车头回指弯道 = 倒车出弯的正确头向；travel = dir×sgn = 新臂，下一帧重算不回退）。
            if (sgn > 0) { c.dirX = float(vdx);  c.dirZ = float(vdz); }
            else         { c.dirX = -float(vdx); c.dirZ = -float(vdz); }
            cartYawFromDir(c.dirX, c.dirZ, c.yaw);
            tx = float(vdx); tz = float(vdz);
        }
    }
    float remain = std::fabs(step);
    int guard = 0;
    // 复审 #23：本 tick 贴轨收敛预算（格）。旧「一次钉回」在段中重选向后把 ~0.5 格横向偏移瞬时吃掉 →
    //   被骑时玩家视点同步横跳一次（旧探针只验收终态测不出瞬移）。限速后每 tick 最多收
    //   kCartCenterSnapPerTick，0.5 格偏移 ~5 tick（83ms）渐进钉回。取舍：另一修法「改 dir 同时钉 pos」
    //   只是同一瞬移换个时点，不解决跳变，弃。预算制不影响正常行驶（恒在 .5 上 → 不消费）与收敛正确性
    //   （预算只限速、方向不变；行进轴的到心落位仍精确钉格心，仅垂直轴渐进）。
    float snapBudget = kCartCenterSnapPerTick;
    while (remain > 1e-5f && guard++ < 16) { // 子步循环（单帧跨多格；16 子步上限（boost×卡顿尖峰 dt 余量））
        // t770 ② 弯道格内强制贴轨约束（与速度 / 方向 / 步长无关的几何连续约束）+ 复审 #23 限速：把行进向
        //   的**垂直轴向**所在格中心线（floor+0.5）收敛（非一次钉回，见上方预算注释）。轨格模型 = 格心
        //   折线（直段沿轴中心线、拐角过格心转直角）→ 矿车合法位置集合 = 这条折线；但方向重选并非只在
        //   到心时刻发生 —— 停驻重选向（tickRiddenCart 速度死区归零帧）/ 被推起步（pushEmptyCart）都可能
        //   在**段中非心位**把 dir 掰向新轴（弯道格内 wish 略偏即选中出口臂）→ 车沿平行偏移线行驶（用户
        //   报「慢速前进未触发旋转即脱轨」的几何根因；倒退经 ①持久化后同样受益）。每子步向中心线收敛：
        //   正常行驶恒在 .5 上 → no-op 零开销；偏移只可能来自上述重选（量 <0.5 格，且不出本格 → 不改变
        //   floor 列解析），渐进钉回根除脱轨且不瞬移。90° 瞬转本身允许（MC 亦近似瞬转），不允许的是位置
        //   脱离中心线 —— 本段就是「位置必须连续贴轨」的执行点。
        {
            const bool axisX = std::fabs(tx) > 0.5f; // 行进轴 X → 收敛垂直轴 Z，反之收敛 X
            const float cur = axisX ? c.pos.z() : c.pos.x();
            const float want = std::floor(cur) + 0.5f;
            const float d = want - cur;
            const float m = std::min(std::fabs(d), snapBudget);
            if (m > 0.0f) {
                const float v = cur + (d >= 0.0f ? m : -m);
                if (axisX) c.pos.setZ(v); else c.pos.setX(v);
                snapBudget -= m;
            }
        }
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
                deadEnd(tx, tz); // t863③：坡顶 + 速度足飞出；否则停驻（原「轨尽头 → 停」）
                break;
            }
            // t770 ①：到心重选结果双符号持久化（同上方起步先验）：正行 dir=新臂（t737 车头同步）；倒行
            //   dir=新臂取反（车头回指弯道 = 倒车出弯头向；travel = dir×sgn = 新臂）。旧版 sgn<0 只改本
            //   子步循环的局部 tx/tz、不写回 dir → 倒退过弯后**下一帧** travel = 旧 dir×sgn 沿旧轴横切 ——
            //   弯道格心一过，车垂直于轨道滑进无轨列，到心重选 false → 停在轨外一格悬空（用户报「倒退
            //   大概率脱轨」根因；任意倒退速度都触发，慢速下肉眼全程可见）。直格上臂=行进向 → dir=新臂
            //   取反 = 原 dir，幂等无翻转；仅拐角处头向随新臂旋转 90°（位置由 ② 钉在中心线上，几何连续）。
            if (sgn > 0) { c.dirX = float(ndx);  c.dirZ = float(ndz); }
            else         { c.dirX = -float(ndx); c.dirZ = -float(ndz); }
            cartYawFromDir(c.dirX, c.dirZ, c.yaw);
            tx = float(ndx); tz = float(ndz); // 行进方向继续（正行沿新臂 / 倒行沿新臂退行 —— 拐角双向自动过弯）
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
    // t866② 环境摧毁检查（帧级收口：骑乘 / 非骑乘帧 PlayerController 都必调本 tick → 全车种仙人掌 /
    //   岩浆 / 虚空检查在推进前结算 —— 碰仙人掌的车本帧即毁 + 掉落，不再推进物理）。
    checkCartEnvironment(world);
    if (m_liveCount <= 0) { // 环境检查可能已毁完全部车 → 占用表按「上一帧 − 本帧」收离开沿后早退
        updateDetectorRailOccupancy(world);
        return;
    }
    bool moved = false;
    for (size_t i = 0; i < m_carts.size(); ++i) {
        Cart &c = m_carts[i];
        if (!c.alive) continue;
        if (int(i) == m_riderCart) continue; // 被骑的走 tickRiddenCart 专属物理（boost / 停驻重选向；占用统一在帧首 pass）
        // t863③ 出轨 / 坠落自由物理（轨端飞出 / 支撑被挖 / 轨末端被推离）：平抛 + 落地重挂 / 贴面。
        if (c.derailed) {
            const float bx = c.pos.x(), bz = c.pos.z(), by = c.pos.y();
            tickDerailedCart(int(i), c, world, float(dt));
            if (!c.alive || std::fabs(c.pos.x() - bx) > 1e-4f || std::fabs(c.pos.z() - bz) > 1e-4f
                || std::fabs(c.pos.y() - by) > 1e-4f)
                moved = true;
            continue;
        }
        // t863② 停驻 / 滑行统一支撑复探：列内有轨 → pinCartY 钉面（幂等，fx 不变同值）；无轨 → 地面真顶
        //   贴面（World::supportTopYAt，t865/t867 同族）或失支撑转 derailed 坠落（挖掉下方轨 / 支撑的
        //   矿车受重力转下落，机制等价 MC；地面静止车恒贴面保 t734 放宽放置语义）。
        const int ry = pinCartY(c, world);
        if (ry < 0) {
            const float bot = c.pos.y() - kCartGroundH;
            const float gtop = groundSupportTopWithin(world, c.pos.x(), c.pos.z(),
                                                      int(std::floor(bot)), bot);
            if (gtop >= 0.0f) {
                if (std::fabs(c.pos.y() - (gtop + kCartGroundH)) > 1e-4f) {
                    c.pos.setY(gtop + kCartGroundH); // 支撑变矮 / 换薄体 → 下贴新真顶
                    moved = true;
                }
            } else {
                c.derailed = true; // 失支撑 → 坠落（本帧起自由物理）
                c.fallVy = 0.0f;
                c.speed = 0.0f;
                moved = true;
                continue;
            }
        }
        if (std::fabs(c.speed) < 1e-3f) continue; // 静置空车不自动起步
        // 滑行物理：行进侧邻轨高度差判定（同 tickRiddenCart 坡道重力公式；空车无输入 → 只做滑行不抬速）。
        //   下坡（δ=-1）→ 重力抵摩擦 → 本帧不衰减（顺坡滑）；平 / 上坡 / 无轨（INT_MIN）→ 磨擦渐停。
        const float bx = c.pos.x(), bz = c.pos.z(); // 水平位移检测基准（ry 已在上方支撑复探段取得）
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
            if (std::fabs(c.speed) < 0.02f) {
                c.speed = 0.0f;
                // t863① 坡上失速反溜：车头朝上坡面 → 反溜起步（沿 -dir 倒行滑回坡脚，不悬停半空；
                //   机制等价 MC 1.0 矿车上坡失速滑回）。平面 / 采样失联 → 照旧停驻（t708 保守闸门保留）。
                if (!tryStallSlideback(c, world, ry)) continue; // 磨擦停稳（死区）
            }
        } // 下坡（slope<0）→ 不衰减（顺坡滑）；动力段已先行接管（boost 12.8 > 溜坡 10，语义不冲突）
        stepCartAlongRail(c, world, float(dt));
        // step 不碰 Y → 给新格重新钉坡面（下坡贴地滑 / 平轨贴面）；t769 返回值带出新轨层 → 俯仰随坡刷新。
        updateCartPitch(c, world, pinCartY(c, world));
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
//       死端车（仅剩来路连接）不吃冲量（正常追尾 / 对撞 / 坡道 / 交叉的正向连接不受影响）；derailed
//       出轨车例外放行（review26 #14：无轨约束可验的自由体可被撞滑，见守卫内注释；t734 存量地面车
//       —— 无轨且未挂 derailed 标 —— 仍不吃冲量）。
//   任一车 speed/pos 被改 → notifyChanged。
void MinecartManager::resolveCartCollisions(World *world)
{
    if (m_carts.size() < 2) return; // <2 车 → 无对可撞（零开销）
    // 复审 #3 (b) 冲量方向守卫：车沿 pushDir 有合法轨连接（pickTrackStep：列内有轨 + 非反向连接）才
    //   允许改速；无世界（防御路径）恒 false（无轨约束可验即不动车）。
    //   review26 #14：derailed 态（出轨自由物理）无轨约束可验 → 恒放行 —— 冲量沿其自身 dir 轴生效
    //   （bDot 投影，行进车撞出轨车 = 出轨车被撞滑，tickDerailedCart 水平积分 + 摩擦自然承接）；旧版
    //   出轨车在车-车碰撞中是不可推动的幽灵障碍（行进车每帧被顶回、出轨车纹丝不动，动力轨也推不过去）。
    const auto impulseDirOk = [&](const Cart &c, float wx, float wz) -> bool {
        if (!world) return false;
        if (c.derailed) return true;
        int pdx = 0, pdz = 0;
        return pickTrackStep(world, c.pos, wx, wz, pdx, pdz);
    };
    // 复审 #3 (a) 位移边界守卫：去穿插位移 s（沿轨轴带符号标量）若使车越过当前格边界，先以 pinCartY
    //   同语义列扫描（宽容版：自车当前 Y 向下 + 低顶净空放行 / 隔板拒）探测目标列有轨；无轨 → 钳制在
    //   当前格边界内。返回允许执行的位移标量（0 = 不动；负 = 把已越线的半格拉回边界内）。
    //   review26 #14：本列无轨且 derailed → 自由体墙检（同 tickDerailedCart 水平积分的车身腰位格
    //   isCollidable）：目标格敞开 → 放行（被撞滑出去）；堵 → 钳当前格边界内（贴墙停不穿墙）。
    //   非 derailed 的无轨车（t734 地面车存量）维持恒 0（「无轨推不动」旧语义，本修不扩面）。
    //   **t907 实体墙硬约束**（spec「解析结果不得写车入实体格；任何弹射不得越实体墙」）：轨列探测
    //   之外再叠两道闸——
    //   · 同层闸：目标列解析到的轨层必须与自车当前轨层**同层**（colRailY 相等）。宽容扫描（复审 #2）
    //     的容差窗在「邻列下隧轨坡面 rise 抬到接近本层」场景可解析到隔层轨（|ΔY|=1 经 rise 一致性
    //     放行）—— 旧版只验「目标列有轨」就全额放行位移，把车写进墙列、下一帧 pinCartY 同一容差
    //     把它钉到墙内 / 墙后轨上（密闭单格互挤「飞出牢笼」的写位半边）。同层闸只拦「一次去穿插
    //     位移换层」—— 真实的坡道跨格行驶（层变化）走 stepCartAlongRail 的逐格心重选（轨连接验证），
    //     不受本闸影响；碰撞微推（≤半穿透量 0.49）本就不该换层。
    //   · 腰位闸：同层放行后仍验**落点中心格**（腰位 Y = floor(pos.y-0.1)，同自由体墙检口径）非实体
    //     —— 同层 + 宽容窗（top 格实心、top-1 是轨）放行的低顶坡段，落点腰位格恰是贴轨天花板实心格，
    //     去穿插把车心写进实体格（旧版无条件放行）。钳边界内保「解析永不把车心移入实体格」硬不变量。
    //   两闸都不过 → 钳当前格边界内（同旧钳制公式）。
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
        // 复审 #2：宽容版（与 pinCartY 严格同语义 —— 低顶净空坡上的去穿插不被隔板断扫误拒；
        //   refY/格内坐标按位移落点取：下一帧 pinCartY 将以该落点解析）。t907：返回值从「有轨」
        //   布尔改为**轨层 Y**（-1 无轨）—— 同层闸消费。
        const auto colRailY = [&](int colX, int colZ, float wx, float wz) {
            return scanRailColumnRiding(world, colX, colZ, topY,
                                        wx - float(colX), wz - float(colZ), c.pos.y());
        };
        const int rySelf = colRailY(cx, cz, c.pos.x(), c.pos.z());
        if (rySelf < 0) {
            if (!c.derailed) return 0.0f; // 本列无轨且非出轨自由体（t734 地面车）→ 推不动
            const int stepF = (cellNxt > cellCur) ? 1 : -1;
            const int byc = int(std::floor(c.pos.y() - 0.1f)); // 车身腰位格（同 tickDerailedCart 撞墙判据）
            if (!world->isCollidable(axisX ? cx + stepF : cx, byc,
                                     axisX ? cz : cz + stepF))
                return s; // 自由体：目标格敞开 → 放行（撞滑）
            const float boundF = (stepF > 0) ? float(cellCur + 1) - 1e-3f : float(cellCur) + 1e-3f;
            return (boundF - cur) / d; // 堵 → 钳当前格边界内（贴墙停）
        }
        const int step = (cellNxt > cellCur) ? 1 : -1;
        const int tx = axisX ? cx + step : cx;
        const int tz = axisX ? cz : cz + step;
        const float landX = c.pos.x() + (axisX ? d * s : 0.0f);
        const float landZ = c.pos.z() + (axisX ? 0.0f : d * s);
        // t907 同层闸 + 腰位闸（见函数头 t907 注释）。同层 + 腰位非实体 → 放行（平轨跨格微推 /
        //   同层轨列推进的常规路径）；其余一律钳边界内。
        if (colRailY(tx, tz, landX, landZ) == rySelf) {
            const int byc = int(std::floor(c.pos.y() - 0.1f));
            if (!world->isCollidable(int(std::floor(landX)), byc, int(std::floor(landZ))))
                return s; // 目标列同层有轨（当前 Y 可达）且落点腰位非实体 → 放行
        }
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
            // 审查修 L3（交叉道口顶停堵线）：轨轴点积 axisDot（轨向恒四向单位向量 → ∈ {0,±1}；0 = 两车
            //   所在轨列互相垂直 = 交叉道口 / 垂直对穿）。旧版 (a) 去穿插把半穿透量投影到各自轨轴 ——
            //   道口场景 n ≈ 驶来车的轨轴 → aDot=±1 全额反推、bDot≈0 停车不动 → 驶向「停着车的道口」的
            //   车在 ~1 格外被逐帧顶退、永久滞留堵死一条线路（与头注释「交叉轨道互不挤位」承诺相反；
            //   同因 (b) 的冲量减速也把驶来车「吸」停在道口前）。修（= 报告建议的 axisDot² 缩放在四向
            //   轨下的 0/1 离散实现）：轨轴垂直对整体跳过 (a)+(b) → 道口两车互穿通过、各走各轨；同轨
            //   追尾 / 对撞（|axisDot|=1 → 轴平方=1）行为不变。
            const float axisDot = a.dirX * b.dirX + a.dirZ * b.dirZ;
            if (std::fabs(axisDot) <= 0.5f) continue; // 交叉道口：不挤位不传冲量（见上）
            // (b) 动量传递（仅互相逼近 closing>0 时）：各车速度矢量（dir×speed）沿 n 投影 → 接近速度；
            //   冲量 = closing × kCartMomentumTransfer，沿 n 加给 B、减给 A，再投影回**各自轨轴**
            //   （同轨对 dot(n,dir) 恒 ±1 全额传递：后车减速、前车沿 n 被推走 —— 负速=沿 -dir 倒行，
            //   与车头朝向无关地「被撞开」；交叉轨道对已被上方 L3 守卫跳过，不进此段）。对撞同式自然
            //   得「双双减速 / 轻微反弹」。
            const float aDot = a.dirX * nx + a.dirZ * nz;
            const float bDot = b.dirX * nx + b.dirZ * nz;
            const float closing = a.speed * aDot - b.speed * bDot; // >0 = A 沿 n 逼近 B
            if (closing > 0.05f) {
                // 复审 #3 (b)：A 的受冲向 = -n（顶退）、B 的 = +n（推离），各自须有合法轨连接才吃冲量
                //   （死端车不被撞进无轨列；守卫未过者速度原样保留 —— 另一侧的减速分量照常生效）。
                //   t907 冲量钳制：碰撞获速上限 = kCartBoostSpeed（全引擎矿车速度上界 —— boost 档）。
                //   旧版冲量与既有速度线性叠加（后车 8 + 前车吃 0.85×closing 可冲破上界），高速弹射
                //   放大帧率尖峰下 derailed 水平积分的单步步长（穿墙风险面，见 tickDerailedCart 子步
                //   化注释）；钳到上界把「任何弹射」的能量面收敛回既有常量域。
                const float impulse = closing * kCartMomentumTransfer;
                if (impulseDirOk(b, nx, nz)) {
                    b.speed += impulse * bDot;
                    b.speed = std::max(-kCartBoostSpeed, std::min(kCartBoostSpeed, b.speed));
                }
                if (impulseDirOk(a, -nx, -nz)) {
                    a.speed -= impulse * aDot;
                    a.speed = std::max(-kCartBoostSpeed, std::min(kCartBoostSpeed, a.speed));
                }
                changed = true;
            }
            // (a) 位置去穿插（穿透 >5cm 才推，防贴轨停驻两车的 FP 微抖抖动）：各沿自身轨轴推开半穿透量
            //   （-n 在 A 轨轴上的投影定 A 的位移符号与大小；交叉轨道对已在上方 L3 守卫跳过 —— 本段只
            //   处理同轨对，投影恒全额，不再依赖「投影 0 不位移」兜底）。
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
//   水平 AABB（±kCartPushReach）与静止空矿车重叠 → 把矿车沿轨推**离玩家**。t809 修选向：旧版把 wish 直接
//   当选向向量 → 玩家长按 W 连推（视点/输入不随拐角转）时，车过拐角后停在与 wish 垂直的臂上，两臂点积
//   同为 0 平局 → 按 kDirs 枚举序（Px 先于 Nx、Pz 先于 Nz）破平局：拐角出口朝枚举序败者（-X / -Z）时选中
//   **指回拐角**的臂 → 车滑回拐角、到心重选（运动向）又把车送回来路 → 推一下退一格的往返振荡 = 用户报
//   「推到拐弯处推不动了」。修 = 三级合成选向向量（身体推开语义，机制等价 MC 玩家撞静止矿车 → 车沿轨
//   被推离玩家身体）：
//     ① away（车心 − 玩家脚底，水平归一，权重 1.0）—— 推开主方向（玩家在车哪侧，车就往对侧轨臂走；
//        远离向与轨轴垂直时点积 0，自然退给下两级）；
//     ② wish（权重 0.5）—— 输入意图次之（玩家面朝 + 按键方向；直段与 away 同向叠加）；
//     ③ dir（权重 0.25）—— 运动连续性兜底（前 tick 行进向；玩家贴车同位（away≈0）且松向输入时仍沿
//        原行进向续推，不因合成向量归零而卡停）。
//   权重比保证 away 主导（1.0 > 0.5+0.25 合计的任一分量单独翻转不足以越过 away 与另一级的同向和）。
//   已滑行的车（speed≠0）不二次推（防静止站位无限叠速）；被骑的车不推。pickTrackStep 滤 dot<0 / 无轨 →
//   纯反向或无可走连接 → 不推（机制等价 MC 推静止矿车须沿轨轴有可达连接）。
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
        // t809 三级合成选向（见函数头注释）：away（推开主向）+ wish（输入意图）+ dir（运动连续兜底）。
        //   away 归一（玩家贴车同位 → 水平距 <1e-4 → away 置 0，退给 wish/dir 两级）。
        float ax = c.pos.x() - playerFeet.x();
        float az = c.pos.z() - playerFeet.z();
        const float al = std::sqrt(ax * ax + az * az);
        if (al > 1e-4f) { ax /= al; az /= al; } else { ax = 0.0f; az = 0.0f; }
        const float selX = ax + 0.5f * nwx + 0.25f * c.dirX;
        const float selZ = az + 0.5f * nwz + 0.25f * c.dirZ;
        int ndx = 0, ndz = 0;
        if (!pickTrackStep(world, c.pos, selX, selZ, ndx, ndz)) {
            // t863④ 轨末端推离（spec「轨末端静止车可被玩家推离轨道进入自由物理」）：合成推向前方主轴
            //   无轨（真轨端）或本就出轨 / 地面车 → 自由物理推（derailed 水平推出，下方有地面则贴地滑
            //   行渐停、无地面坠落）。轨上但前方主轴**有轨**（三高探针）→ 是反向推（dot<0 滤）或拐角
            //   选向问题 → 不推离（旧语义防振荡）。主轴 = 合成向量绝对值更大的一轴（轨向四向对齐）。
            const int pcx = int(std::floor(c.pos.x())), pcz = int(std::floor(c.pos.z()));
            const int pry = scanRailColumnRiding(world, pcx, pcz, int(std::floor(c.pos.y())),
                                                 c.pos.x() - float(pcx), c.pos.z() - float(pcz),
                                                 c.pos.y());
            int ax = 0, az = 0;
            if (std::fabs(selX) >= std::fabs(selZ)) ax = (selX >= 0.0f) ? 1 : -1;
            else                                     az = (selZ >= 0.0f) ? 1 : -1;
            if (pry >= 0) {
                const auto railAhead = BlockRegistry::isRail(world->blockAt(pcx + ax, pry, pcz + az))
                    || BlockRegistry::isRail(world->blockAt(pcx + ax, pry + 1, pcz + az))
                    || BlockRegistry::isRail(world->blockAt(pcx + ax, pry - 1, pcz + az));
                if (railAhead) continue; // 前方有轨：非轨端（反向 / 拐角），不推离
            }
            c.dirX = float(ax);
            c.dirZ = float(az);
            c.speed = kCartPushSpeed;
            c.derailed = true; // 出轨自由物理（本帧起平抛 / 贴地滑行；站轨 1/16 落差由落地扫描承接）
            c.fallVy = 0.0f;
            c.pitch = 0.0f;    // review26 #12：推离即离轨面 —— 带走坡面俯仰会让平抛车保持倾斜（tickDerailedCart
                               //   只在轨面重挂时刷 pitch，落地贴面永不刷）→ 与 deadEnd 飞出分支对称清 0（水平摆）
            cartYawFromDir(c.dirX, c.dirZ, c.yaw);
            pushed = true;
            continue;
        }
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

    // t863③ 出轨 / 坠落自由物理（轨端飞出 / 支撑被挖）：被骑车平抛坠落，玩家经 outCartPos 随车
    //   （同帧同步座位，不掉队）；落地重挂轨道 / 贴面后恢复骑乘轨物理。输入（wish）飞行中不消费
    //   （机制等价 MC 矿车空中无操控）。
    if (c.derailed) {
        tickDerailedCart(m_riderCart, c, world, float(dt));
        outCartPos = c.pos;
        notifyChanged();
        return;
    }

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
    //   复审 #4：扫描收口到 scanRailColumn。复审 #2：骑乘前置钉定改宽容版（低顶净空坡段不误判离轨 →
    //   坡上死车；地面车隔板拒由一致性校验承担，与 pinCartY 同语义）。
    const int railX = int(std::floor(c.pos.x()));
    const int railZ = int(std::floor(c.pos.z()));
    const int railY = world ? scanRailColumnRiding(world, railX, railZ, int(std::floor(c.pos.y())),
                                                   c.pos.x() - float(railX), c.pos.z() - float(railZ),
                                                   c.pos.y())
                            : -1; // -1 = 列内无轨

    // t734 ③ 离轨静止（防「矿车不在铁轨上仍可被骑着一路悬浮滑到地图边界」）：列内无轨（railY<0）→
    //   t863② 起分两支：地面真顶有支撑 → 钉死速度停驻（t734 放宽放置的地面静止语义保留 —— 可 Shift
    //   下车 + 左键拾取，不重选向 / 不 lerp 起步 / 不位移）；失支撑（下方轨 / 支撑被挖）→ 转坠落自由
    //   物理（受重力转下落，玩家随车；机制等价 MC 矿车失去支撑坠落）。
    if (railY < 0) {
        const float bot = c.pos.y() - kCartGroundH;
        const float gtop = groundSupportTopWithin(world, c.pos.x(), c.pos.z(),
                                                  int(std::floor(bot)), bot);
        if (gtop >= 0.0f) {
            c.speed = 0.0f;
            if (std::fabs(c.pos.y() - (gtop + kCartGroundH)) > 1e-4f) c.pos.setY(gtop + kCartGroundH);
        } else {
            c.derailed = true;
            c.fallVy = 0.0f;
            tickDerailedCart(m_riderCart, c, world, float(dt));
        }
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
    // t638 ⑤ / t658 修 / t810 修 动力轨加速（spec「动力轨 = 矿车经过提速」，机制等价 MC 1.0 powered rail
    //   boost）：**通电才加速**（t658 前恒 boost —— 无红石系统时代的简化；红石电力系统 v1 落地后改为读轨
    //   state 的 GoldenRailStateOnFlag（bit4，World::tickRedstone 电力重算置 / 清）。机制等价 MC：断电动力轨
    //   = 普通轨（仅承载不加速），通电动力轨才 boost / 弹射）。矿车当前所在轨格为 GoldenRail 且通电 → 目标
    //   速改写（t810 修：旧版改写的是 **proj 幅度**（proj>0 → proj×boost/kCart → 目标 = proj×boost；proj≈0
    //   → 弹射档 0.35 → 目标 2.8）→ 过弯后玩家视点/输入向未跟上新行进向的窗口（wish⊥dir → proj≈0）动力段
    //   被弹射档 2.8 接管，boost 12.8 以 ~3 格/s²一路拉垮到爬行速，下一拐角再砍一刀 = 用户报「骑乘过弯速度
    //   骤减、两条动力轨喂入也救不回」；空车路径（tickPushedCarts t735 ④）按**运动符号**全额 boost 无此症
    //   ——同场同轨空车匀速圈跑的对照即根因定位）。修后直接改写 targetV 三分支（机制等价 MC：动力轨供能
    //   看**车速方向**不看玩家视角）：
    //   (a) 无输入（proj≈0）且车在动 → 沿当前 speed 符号全额 boost（对齐空车路径 t735 ④；视点滞后窗口
    //       不再掉速 —— 过弯接近匀速）；
    //   (b) 有前进输入（proj>0）→ 全 boost 档（不再按 proj 幅度打折 —— 视线偏 30° 也不衰减；proj=1 时与
    //       旧版同值，纯放宽）；
    //   (c) 反踩刹车（proj<0）→ 不改写（玩家减速意图优先，动力不反向推、也不与刹车角力）；
    //   (d) 无输入且停驻 → 弹射档 0.35×kCartSpeed（t658 语义保留：机制等价 MC 动力轨是「发射器」，停着的
    //       矿车驶上动力轨即被弹射向前，无需玩家踩 W）。
    //   轨格判定同 pickTrackStep（中心下一格）；t691：轨层读前置钉定的 railY（非 floor(pos.y)-1）。
    float targetV = proj * kCartSpeed;
    {
        const quint8 gb = (world && railY >= 0) ? world->blockAt(railX, railY, railZ) : quint8(BlockRegistry::Air);
        const bool railPowered = (gb == BlockRegistry::GoldenRail)
            && (world->stateAt(railX, railY, railZ) & BlockRegistry::GoldenRailStateOnFlag) != 0; // t658 通电位
        if (railPowered) {
            if (proj > 1e-3f) {
                targetV = kCartBoostSpeed; // (b) 前进输入 → 全 boost 档
            } else if (proj < -1e-3f) {
                // (c) 反踩刹车 → 不改写：玩家减速意图优先
            } else if (std::fabs(c.speed) > 1e-3f) {
                targetV = (c.speed >= 0.0f ? 1.0f : -1.0f) * kCartBoostSpeed; // (a) 沿当前运动向全 boost
            } else {
                targetV = 0.35f * kCartSpeed; // (d) 停驻弹射档（机制等价 MC 动力轨弹射停着的矿车）
            }
        }
    }
    // t667 坡道重力（机制等价 MC 矿车上坡减速 / 下坡自加速、静止车在下坡上溜车）：
    //   以「行进方向上的邻轨高度差」（railProbeDelta：+1 上坡 / -1 下坡 / 0 平 / INT_MIN 无轨）修正目标
    //   速度 —— 下坡（δ<0）→ 目标抬高到 kCartSlopeDownSpeed（无输入也溜车）；上坡（δ>0）→ 目标按
    //   kCartUphillMul 收窄（须玩家输入推力才能爬）。行进侧按 speed 符号取（负速 = 朝 -dir 走：反向推进、
    //   倒行下坡同样加速倒溜）。静止车在下坡上 → 下坡标志放行下方 movement 闸门起步溜。渲染几何的坡面
    //   高度与矿车 Y 同读 railProbeDelta（钉轨面段），两者粒度一致。t691：轨层读前置钉定 railY。
    //   t810：targetV 声明上移至动力轨段前（该段先改写、坡道段在其上继续叠加）。
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
        if (std::fabs(c.speed) < 0.02f) {
            c.speed = 0.0f; // 死区归零（防微速漂移）
            // t863① 坡上失速反溜：停驻点在坡面（车头朝上坡）→ 反溜起步滑回坡脚，不悬停半空（机制等价
            //   MC 1.0）。W 持续时 targetV>0 速度恒正不进死区 → 不与爬坡输入打架；S 刹停 / 松键滑停自然
            //   接入。平面 / 采样失联 → 照旧停驻。
            tryStallSlideback(c, world, railY);
        }
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
            //   t769：钉轨返回值带出本帧轨层 → 俯仰随坡刷新（采样与 Y 钉定同一张面，见 updateCartPitch）。
            const int pinnedY = pinCartY(c, world);
            updateCartPitch(c, world, pinnedY);
        }
        // 车头朝向（-Z 前约定，同 PlayerController horizontalFacing / 相机 yaw）：yaw = atan2(-dirX,-dirZ)
        //   → dir=(0,-1) → 0°；(0,1) → 180°；(-1,0) → 90°；(1,0) → 270°（车头本地 -Z 经 R_y(yaw) 旋转后
        //   指向 dir；与 QML cartRoot eulerRotation.y 直连，dir 与渲染朝向严格一致）。负速倒行时 dir 不变
        //   → 车头保持原朝向（车底朝后退行）；跨格到心重选向才更新 dir —— t770 ① 起正倒行都更新（倒行
        //   = 新臂取反，见 stepCartAlongRail 注释；直格幂等、拐角头随新臂旋转 90°，位置恒被钉在中心线）。
        c.yaw = std::atan2(-c.dirX, -c.dirZ) * 57.2957795f;
        while (c.yaw < 0.0f) c.yaw += 360.0f;
    } else if (world) {
        // t769 停驻帧俯仰收敛：移动帧在上方 pinCartY 后已刷新；停驻（speed==0 且非下坡起步溜）被骑车
        //   停在坡上时保持贴合坡面（轨层用本 tick 前置钉定的 railY —— 车未动，列不变）。
        updateCartPitch(c, world, railY);
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
        // 复审 #4：列扫描收口 scanRailColumn 严格版（复审 #2 起唯一严格消费端 —— 骑乘族已迁宽容版）：
        //   含实心遮挡断扫 —— 地面静止车隔着实心地板不再点亮下方探测轨隧道（MC 探测轨只响应压在自己
        //   身上的车；坡顶场景格与轨之间只隔空气不受影响）。已知边缘（复审 #2 处方接受）：低顶净空坡段
        //   rise>0.55 处 floor(pos.y)=天花板实心格 → 严格断扫不点亮该段探测轨（隔板供电防线优先；平轨
        //   天花板不受影响 —— pos.y 未跨轨层，首扫格即轨）。
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
    // 摧毁（生存最后一击 / 创造瞬破）：destroyCartTail（t866② 起与仙人掌 / 岩浆环境摧毁共用尾部 ——
    //   清骑乘态 + 释放槽 + 生存掉落散布 + emit cartBroken；t767 创造瞬破 survivalDrop=false 无掉落）。
    return destroyCartTail(idx, world, /*survivalDrop=*/!instantBreak);
}

// t735 ① 掉落格散布（destroyCartTail 用；头注释见 .h）：掉「首个非实心水平邻格」随机一格（邻格上方
//   一格也须非实心，防掉进 1 格深坑壁内）；4 邻全实心（窄缝嵌车）→ 掉车中心格上一格（自重落顶不埋）；
//   world 空（防御路径）→ 保留中心格。随机取 QRandomGenerator（t735① 同船 t711 修法先例）。
void MinecartManager::scatterDropCell(const QVector3D &cp, World *world,
                                      int &dropX, int &dropY, int &dropZ)
{
    dropX = int(std::floor(cp.x()));
    dropY = int(std::floor(cp.y()));
    dropZ = int(std::floor(cp.z()));
    if (!world) return;
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

// t866② 击毁通用尾部（头注释见 .h）：清玩家骑乘态 + 释放槽 + 生存掉落（散布格 emit cartBroken → 呈层
//   spawnItem 掉 MinecartId）。生物乘员由对账链自动释放（tickVehicleRiding Pass A/B：座位指空槽 / 反向
//   链断 → mob 自恢复 AI 原地 + resting 解除重力落地 = 「乘员自动下来」）。
bool MinecartManager::destroyCartTail(int idx, World *world, bool survivalDrop)
{
    if (idx < 0 || idx >= int(m_carts.size()) || !m_carts[size_t(idx)].alive) return false;
    const QVector3D cp = m_carts[size_t(idx)].pos;
    if (idx == m_riderCart) m_riderCart = -1; // 挖 / 毁骑乘中的矿车 → 玩家自然下车（重力接手）
    releaseSlot(idx);
    if (survivalDrop) {
        int dropX = 0, dropY = 0, dropZ = 0;
        scatterDropCell(cp, world, dropX, dropY, dropZ);
        emit cartBroken(dropX, dropY, dropZ);
    }
    notifyChanged();
    return true;
}

// t866② 环境摧毁检查（头注释见 .h）：全部活体矿车 AABB 覆盖格扫仙人掌 / 岩浆 → destroyCartTail（生存
//   掉落语义：碰仙人掌掉矿车物品；岩浆同样 emit —— 掉落物落进岩浆格由 ItemEntityManager 头部瞬毁判
//   定接管，净效果 = 毁无物，机制等价 MC）。虚空（pos.y<0）→ 无掉落移除（mob void-loss 同款兜底）。
void MinecartManager::checkCartEnvironment(World *world)
{
    if (!world) return;
    for (size_t i = 0; i < m_carts.size(); ++i) {
        const Cart &c = m_carts[i];
        if (!c.alive) continue;
        if (c.pos.y() < 0.0f) { // 虚空：跌出世界底部 → 移除（防永久下落刷 emit）
            destroyCartTail(int(i), world, /*survivalDrop=*/false);
            continue;
        }
        // AABB 覆盖格（对轴外接 + **朝向定向**：行进轴 = 斗长轴 kCartHalfL（1.0 长），垂直轴 = 斗宽
        //   kCartHalfW —— 旧版 X 恒半宽 0.45 把 +X 行进的车前沿截短半格 → 轨端推入仙人掌格的接触
        //   永不触发；定向后车头探入前方格 = 真接触。仙人掌碰撞盒 0.1 内缩 → 邻格车不共享格，共享格
        //   即接触）。
        const bool axisX = std::fabs(c.dirX) > 0.5f;
        const float ex = axisX ? kCartHalfL : kCartHalfW;
        const float ez = axisX ? kCartHalfW : kCartHalfL;
        const int x0 = int(std::floor(c.pos.x() - ex)), x1 = int(std::floor(c.pos.x() + ex));
        const int y0 = int(std::floor(c.pos.y() - kCartHalfH)), y1 = int(std::floor(c.pos.y() + kCartHalfH));
        const int z0 = int(std::floor(c.pos.z() - ez)), z1 = int(std::floor(c.pos.z() + ez));
        bool hostile = false;
        for (int y = y0; y <= y1 && !hostile; ++y)
            for (int z = z0; z <= z1 && !hostile; ++z)
                for (int x = x0; x <= x1 && !hostile; ++x) {
                    const quint8 b = world->blockAt(x, y, z);
                    if (b == BlockRegistry::Cactus || b == BlockRegistry::Lava) hostile = true;
                }
        if (hostile) destroyCartTail(int(i), world, /*survivalDrop=*/true);
    }
}
