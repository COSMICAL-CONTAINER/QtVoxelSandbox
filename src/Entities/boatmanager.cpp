#include "boatmanager.h"

#include <QtMath>
#include <QRandomGenerator>
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

bool BoatManager::spawnBoat(int x, int y, int z, int boatType)
{
    if (m_liveCount >= kCap) { qWarning("vo.entities: BoatManager spawnBoat cap reached (%d)", kCap); return false; }
    // review L12：放船点与既有活体船重叠拒绝（防「骑船时在自己脚下放船 → 两船同格叠死」）。船中心间距
    //   < kBoatMinSpawnDist（1.4：略大于船长 1.4 → 两船中心距离至少容一船身，不互相嵌位）→ 拒绝生成
    //   （返 false，caller 据此不扣船物品 —— review rv2-A2 修「被拒放船仍消耗船物品」；玩家瞄准邻近水面
    //   重放即可，机制等价 MC 放船点被占不消耗物品）。旧版无此查：骑乘中放船点常落在骑乘船所在格 →
    //   新船与骑乘船同格叠加，二者推离逻辑 d≈0 无力分开。
    const QVector3D spawnPos(float(x) + 0.5f, float(y) + 1.0f - kBoatDraft, float(z) + 0.5f);
    for (size_t i = 0; i < m_boats.size(); ++i) {
        if (!m_boats[i].alive) continue; // 跳过空槽（slot-reuse 残留位）
        const QVector3D d = m_boats[i].pos - spawnPos;
        if (d.lengthSquared() < kBoatMinSpawnDist * kBoatMinSpawnDist) {
            qWarning("vo.entities: spawnBoat rejected - overlaps existing boat at slot %d", int(i));
            return false;
        }
    }
    Boat b;
    b.pos = spawnPos; // 格中心 + 浮水面（cell 顶 - 吃水）
    b.boatType = (boatType == Spruce) ? Spruce : Oak;
    b.vx = 0.0f; b.vz = 0.0f; b.yaw = 0.0f;
    acquireSlot(std::move(b));
    notifyChanged();
    return true;
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

bool BoatManager::boatFootprintBlocked(World *world, float px, float py, float pz, bool ignoreIce,
                                        bool ignoreLilyPad) const
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
    //   review rev2-C2：ignoreLilyPad=true（仅水档碰岸探测传）同冰豁免逻辑 —— 睡莲浮于水面同层（水面上一格
    //   cell 底 1/16 quad，船中心 Y 落在叶同格），且探测前探边 0.65 > 撞碎扫描边 0.5 → 叶先于撞碎被当岸，
    //   速度被清朝向分量后每帧只重建 ~0.5 < 撞碎阈值 3.0 → 船在叶前楔死、撞碎永不触发。豁免后船保速驶入
    //   叶格 → 高速撞碎（smashLilyPads 在探测前跑）；低速船仍被位移碰撞挡（叶按实体，不传本参）→「快=碎、
    //   慢=挡」两态成立。
    //   t711 五修「冰面可上 / 沙滩不可上不一致」：ignoreIce 同步豁免**与水面同高的任何固体方块**（不止冰）
    //     —— 旧版只豁免冰族：与水面齐平的湿沙滩（沙格顶 == 水面顶）被当岸清速 → 船上不了同层沙滩，而同层
    //     冰面能上（行为不一致）。判据 = 方块格顶 (y+1) ≤ 船中心 Y（探测传 py=船中心−1 → 该层格顶 == 船
    //     中心所在水面顶）→ 与冰同语义「同层可行驶表面」；高出水面的真岸（顶 > 船中心）不豁免（须
    //     beachTimer 冲量爬升登岸，t661「上岸应需速度」语义保持）。机制等价 MC 1.0 船可在与水面齐平的
    //     湿沙滩 / 冰面上行驶（同为船可行驶表面，行为一致）。
    const auto cellBlocked = [world, ignoreIce, ignoreLilyPad, py](int x, int y, int z) {
        if (!world->isCollidable(x, y, z)) return false;
        const quint8 id = world->blockAt(x, y, z);
        // t711 同层可行驶表面豁免：ignoreIce 探测（py=船中心−1）时格顶 ≤ py+1（= 船中心水面顶）的同层
        //   固体（冰 + 同高沙滩）视作可通行；位移碰撞（ignoreIce=false / py=船中心）恒 y+1 > py 不豁免。
        if (ignoreIce && (BlockRegistry::isIce(id) || float(y) + 1.0f <= py + 1.0f + 1e-3f)) return false;
        if (ignoreLilyPad && id == BlockRegistry::LilyPad) return false;
        return true;
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

// t661 四轮 footprint 支撑顶（见 boatmanager.h 注释）：扫 footprint 全部覆盖列（自船中心层向下
//   kBoatSupportScanDepth 格的首个可踩实体），返回**最高**支撑顶（cellY+1）。无支撑 → -1。
//   与 boatFootprintBlocked 同款 footprint 格扫（floor(±半宽/半长)）；水 / 空气非 collidable 自然跳过
//   （船在水面时不进本函数 —— foundWater 走浮水分支；仅无水陆档调用）。
//   t711 五修「睡莲顶飞」：LilyPad 列跳过（无支撑，见 .h 注释）—— 睡莲是水面薄叶非承载面，且其整格
//   顶（cellY+1）被旧版当支撑顶会把碰叶的船瞬抬一整格。
float BoatManager::boatFootprintSupportTop(World *world, float px, float py, float pz) const
{
    if (!world) return -1.0f;
    const int x0 = int(std::floor(px - kBoatHalfW)), x1 = int(std::floor(px + kBoatHalfW));
    const int z0 = int(std::floor(pz - kBoatHalfLen)), z1 = int(std::floor(pz + kBoatHalfLen));
    const int cyBase = int(std::floor(py));
    float topY = -1.0f;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z) {
            for (int y = cyBase; y >= 0 && y >= cyBase - kBoatSupportScanDepth; --y) {
                if (world->isCollidable(x, y, z)) {
                    if (world->blockAt(x, y, z) == BlockRegistry::LilyPad)
                        break; // t711：睡莲薄叶非支撑（该列无支撑；防整格顶顶飞船）
                    const float cellTop = float(y) + 1.0f;
                    if (cellTop > topY) topY = cellTop; // 取全列最高（船搁浅在最高支撑上，不坠缝 / 不卡坑）
                    break; // 该列首个（最高）实体已记，向下不再看
                }
            }
        }
    return topY;
}

float BoatManager::boatFootprintWaterFraction(World *world, float px, float pz, float probeY) const
{
    if (!world) return 0.0f;
    // t630 footprint 水域覆盖采样 + t711 五修「2/3 判定偏严」：
    //   旧版按「覆盖格整格等权」（格数比）：footprint X 1.0×Z 1.4，船中心接近格边界时覆盖格数在 2×3=6 /
    //   2×4=8 / 3×3=9 间跳变 —— 8 格覆盖时浮起需 ≥0.66 → 要 6/8（0.75）格是水，比 2/3 严一整档；9 格时
    //   要 6/9（0.6667）恰好。用户实测「船身 2/3 已在方块（岸）外仍不下水」正是 8 格覆盖态（5/8=0.625 <
    //   0.66 被拒）。改**固定几何采样点**：footprint 内 2(X)×3(Z) = 6 个等分点（X 取 ±0.25、Z 取
    //   -0.45/0/0.45，均在 X±0.5 / Z±0.7 框内）→ 覆盖率恒为 6 点等权，不随船贴格边界跳格数，2/3 = 4/6
    //   稳定成立。列有水 = 自支撑层参考格 probeY 起向上扫 kWaterProbeDepth 内有 Water（覆盖浅 1 格水到
    //   深水）。越界列（blockAt Air 兜底）算非水。
    // t711 边界侧翼：船贴世界边缘（中心被 clamp 到 width-0.5）时部分采样点落界外 → blockAt 返 Air 算
    //   非水 → 覆盖率 < 阈转陆档（防边界处误浮水）；正常开阔水面不受影响。
    const int yStart = int(std::floor(probeY));
    constexpr float kSampX[2] = {-0.25f, 0.25f};
    constexpr float kSampZ[3] = {-0.45f, 0.0f, 0.45f};
    int total = 0, water = 0;
    for (const float ox : kSampX)
        for (const float oz : kSampZ) {
            const int x = int(std::floor(px + ox));
            const int z = int(std::floor(pz + oz));
            ++total;
            for (int y = yStart; y < yStart + kWaterProbeDepth; ++y) {
                if (world->blockAt(x, y, z) == BlockRegistry::Water) { ++water; break; }
            }
        }
    return total > 0 ? float(water) / float(total) : 0.0f;
}

// t630 撞碎荷叶（见 boatmanager.h smashLilyPads 注释）：高速船碾过 footprint 内 LilyPad → 清 Air +
//   emit lilyPadSmashed（呈层掉睡莲物品）。静默写 setWaterSilent（非玩家破块，免粒子/音 spam —— 掉落物由
//   呈层信号侧 spawnItem；同 EntityManager 留雪 / 羊吃草静默写模式）。两层采样（船中心层 + 下一层）与
//   boatFootprintBlocked 一致：浮水船中心 Y = 水面顶 → 叶（浮于水面格底 1/16）常在中心层或下一层。
bool BoatManager::smashLilyPads(World *world, float px, float py, float pz)
{
    if (!world) return false;
    const int x0 = int(std::floor(px - kBoatHalfW)), x1 = int(std::floor(px + kBoatHalfW));
    const int z0 = int(std::floor(pz - kBoatHalfLen)), z1 = int(std::floor(pz + kBoatHalfLen));
    const int cy = int(std::floor(py));
    bool smashed = false;
    for (int x = x0; x <= x1; ++x)
        for (int z = z0; z <= z1; ++z)
            for (int y = cy; y >= cy - 1; --y) {
                if (world->blockAt(x, y, z) == BlockRegistry::LilyPad) {
                    world->setWaterSilent(x, y, z, BlockRegistry::Air, 0);
                    emit lilyPadSmashed(x, y, z);
                    smashed = true;
                }
            }
    return smashed;
}

float BoatManager::waterSurfaceY(World *world, float px, float pz, float fallbackY, bool *outFoundWater) const{
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
                // t630 身体推船旋转（用户：人撞船应有旋转效果，不只平移）：推力作用点 = 玩家接触点相对船心
                //   （−dpx,−dpz = 玩家在船坐标的偏移）。横向偏移（垂直于推开方向）→ 力臂 → 扭矩使船头偏转。
                //   简化力矩模型：yawRate = (力臂 × 推开速度) 的符号分量 —— 取「玩家偏移 × 推开量」的叉积
                //   （2D 叉积 z = rx·pz − rz·px，r = 玩家接触点，p = 推开冲量方向）× kBoatPushTurnRate，
                //   钳 ±kBoatPushTurnMax（度/s）防甩头。玩家正对船心推（偏移近 0）→ 叉积 ≈ 0 不转（对心推 =
                //   纯平移，机制等价 MC 力矩直觉）；玩家偏侧推 → 船头朝推侧偏转（扭矩感）。yaw 为度（QML
                //   eulerRotation.y）。
                if (pushX != 0.0f || pushZ != 0.0f) {
                    const float rx = -dpx, rz = -dpz;      // 玩家接触点相对船心（力臂）
                    const float crossZ = rx * pushZ - rz * pushX; // 2D 叉积（力臂 × 推开量）
                    float yawRate = crossZ * kBoatPushTurnRate;
                    if (yawRate > kBoatPushTurnMax) yawRate = kBoatPushTurnMax;
                    if (yawRate < -kBoatPushTurnMax) yawRate = -kBoatPushTurnMax;
                    if (yawRate != 0.0f) {
                        b.yaw += yawRate * float(dt);
                        // 归一到 [0,360)（QML eulerRotation 绑定常规域，防无限增长精度漂移）。
                        while (b.yaw >= 360.0f) b.yaw -= 360.0f;
                        while (b.yaw < 0.0f) b.yaw += 360.0f;
                    }
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
        //   t630「2/3 支撑阈值」：waterSurfaceY 只看**中心列** —— 岸沿驶入时中心格一入水即判「有水」→ Y 钉
        //     水面把仍压岸的 ≥1/3 船身拽下沉嵌岸块（「一半在水一半卡方块」根因）。改：中心列有水**且**
        //     footprint 水域覆盖率过迟滞双阈（浮起 ≥kBoatWaterFractionRise / 落下 <kBoatWaterFractionFall，
        //     rev2 修：旧单值 0.67 拒 4/6 + 无迟滞抖动）才走浮水；否则走无水陆档（重力贴支撑面）→
        //     船身在岸上滑行直到 2/3 过沿才落水（机制等价 MC 船大部分船身离开岸才落水，不卡岸缝）。
        bool foundWater = false;
        const float surfY = waterSurfaceY(world, b.pos.x(), b.pos.z(), b.pos.y(), &foundWater);
        if (foundWater) {
            const float frac = boatFootprintWaterFraction(world, b.pos.x(), b.pos.z(), b.pos.y() - 1.0f);
            if (b.floating ? frac < kBoatWaterFractionFall : frac < kBoatWaterFractionRise)
                foundWater = false; // 未浮 <浮起阈 / 已浮 <落下阈 → 按陆档处理（不掉进水岸夹缝）
        }
        b.floating = foundWater;
        const float dy = surfY - b.pos.y();
        if (foundWater) {
            if (std::fabs(dy) > 1e-3f) {
                // 恒速上浮 / 下沉（kBoatAccel 作速率），钳到本帧不超过 |dy|（防过冲振荡）。
                const float step = std::clamp(kBoatAccel * float(dt), 0.0f, std::fabs(dy)) * (dy > 0 ? 1.0f : -1.0f);
                b.pos.setY(b.pos.y() + step);
                changed = true;
            }
        } else {
            // 无水重力（t508 二轮复盘；t661 四轮重写支撑段）：footprint 全列最高支撑顶作支撑面（旧版只查
            //   中心列 3 格 —— 中心列在坑上 → 误判无支撑自由落体卡坑「高处落下走不了」；中心列在水上 →
            //   支撑=水底 → restY 远低 → 贴岸的船被拽沉「到海边直接掉下去」。见 boatFootprintSupportTop 注释）。
            //   船底低于中心 kBoatHullBottom（0.2），稳态船中心 Y = 支撑顶 + 0.2（船底贴顶）。
            //   - 船中心 ≤ 稳态 Y（已贴 / 已穿）且高差 ≤ 半格（kBoatBeachSnap）→ 直接贴稳态（防抖动）。
            //   - 稳态比船中心高 0.5~1.25 格（1 格高岸沿）→ 仅 beachTimer>0（冲量登岸，tickRiddenBoat 高速
            //     撞岸时写入）时以 kBoatBeachClimbRate 限速爬升；空船 / 无冲量 → Y 不动（贴岸壁停住，机制
            //     等价 MC 船低速顶岸 = 停）。
            //   - 船中心 > 稳态 Y → 匀速下落（本帧不穿稳态）。
            const float supportTop = boatFootprintSupportTop(world, b.pos.x(), b.pos.y(), b.pos.z());
            if (supportTop >= 0.0f) {
                const float restY = supportTop + kBoatHullBottom;
                if (b.pos.y() <= restY) {
                    if (restY - b.pos.y() <= kBoatBeachSnap) {
                        b.pos.setY(restY); // 小高差贴稳态（防抖动；同高 / 略穿）
                    } else if (b.beachTimer > 0.0f) {
                        // t661 冲量登岸：限速爬上 1 格岸沿（机制等价 MC 1.0 带速的船勉强冲上滩）。
                        b.pos.setY(b.pos.y() + kBoatBeachClimbRate * float(dt));
                        b.beachTimer = std::max(0.0f, b.beachTimer - float(dt)); // 爬升消耗冲量
                    }
                    // else：稳态高出 >kBoatBeachSnap 且无冲量 → 不瞬移不爬（贴岸壁停住）
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
        //   t630 撞碎荷叶：速度 > kBoatLilySmashSpeed 时先碾碎 footprint 内 LilyPad（清 Air + 掉物品），
        //   再走位移碰撞（叶已清 → 不挡；低速叶仍挡，绕行）。
        //   review rev2-C2：高速时位移碰撞**同豁免**睡莲（ignoreLilyPad=fast）—— 撞碎扫描只盖**当前**
        //   footprint，船头要到下一帧才进叶格；若位移碰撞仍把叶当实体，船会被冻在叶格边（速度清零 →
        //   重建每帧 ~0.5 永达不到撞碎阈值 3.0 → 撞碎扫描永远盖不到叶格）。高速豁免 → 船头进叶格（最多
        //   重叠 1 帧，最大帧位移 0.35 < footprint 宽 1.0，不会跳过）→ 下一帧撞碎扫描清叶。低速不豁免 →
        //   叶按实体挡（慢速 = 阻挡绕行，机制等价 MC 慢速船被 lily pad 阻挡）。
        if (b.vx != 0.0f || b.vz != 0.0f) {
            const bool fastBoat = std::sqrt(b.vx * b.vx + b.vz * b.vz) > kBoatLilySmashSpeed;
            if (fastBoat && smashLilyPads(world, b.pos.x(), b.pos.y(), b.pos.z()))
                changed = true;
            const float dx = b.vx * float(dt);
            const float dz = b.vz * float(dt);
            if (dx != 0.0f) {
                const float nx = b.pos.x() + dx;
                if (!boatFootprintBlocked(world, nx, b.pos.y(), b.pos.z(), /*ignoreIce*/ false, /*ignoreLilyPad*/ fastBoat)) b.pos.setX(nx);
                else b.vx = 0.0f;
            }
            if (dz != 0.0f) {
                const float nz = b.pos.z() + dz;
                if (!boatFootprintBlocked(world, b.pos.x(), b.pos.y(), nz, /*ignoreIce*/ false, /*ignoreLilyPad*/ fastBoat)) b.pos.setZ(nz);
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
    //   t630「2/3 支撑阈值」（同 tick 段修法）：中心列有水但 footprint 水域覆盖 < 2/3（岸沿 1/3+ 船身仍
    //   压岸）→ 判无水（陆档重力贴支撑面），修「岸→水骑乘驶入一半卡水一半卡方块」（旧版中心列一入水
    //   即钉水面 Y + 水档推进，半船被拽沉嵌岸块）。船滑行到 2/3 船身过沿才切水档落水，平滑不卡。
    bool foundWater = false;
    const float surfY = waterSurfaceY(world, b.pos.x(), b.pos.z(), b.pos.y(), &foundWater);
    // rev2 迟滞双阈：浮起 ≥ kBoatWaterFractionRise（4/6=0.6667 过）；已浮态落下须 < kBoatWaterFractionFall
    //   （3/6 及以下）；带间（0.5~0.66）维持上一档 —— 防覆盖格数 4↔6 跳变处反复切档 Y 抖动。
    if (foundWater) {
        const float frac = boatFootprintWaterFraction(world, b.pos.x(), b.pos.z(), b.pos.y() - 1.0f);
        if (b.floating ? frac < kBoatWaterFractionFall : frac < kBoatWaterFractionRise)
            foundWater = false; // <浮起阈（未浮）/ <落下阈（已浮）→ 陆档（不掉水岸夹缝）
    }
    b.floating = foundWater;
    const quint8 below = blockBelowBoat(world, b.pos);
    const bool onIce = !foundWater && BlockRegistry::isIce(below);

    // 三档推进参数（机制等价 MC 1.0 船：陆地几乎开不动 / 水中常速 / 冰面最快且惯性大——难操作才对）：
    //   - Water：kBoatSpeed × 1.0（8 blocks/s）+ 接近率 kBoatAccel=4（中等动量）。
    //   - Ice：  kBoatSpeed × 冰类型倍率（t661 四轮：1.4 / 1.7 / 2.0 → 11.2~16 blocks/s，约水面 1.4~2 倍）
    //     + 接近率读 BlockRegistry::iceSlipApproach（t661：冰 4 / 浮冰 2.2 / 蓝冰 1.3 /s，单一权威：越小
    //     越滑 → 松键后长滑行 = 冰面惯性）+ 转向速率 × kBoatIceTurnMul（迟钝 = 难操作）。
    //     t661 四轮调校（用户「冰上速度太快 + 惯性太小」）：旧 2.0/2.4/2.7（16~21.6，过快难控）→ 降速
    //     1.4/1.7/2.0；同时 blockregistry iceSlipApproach 6/3.2/1.9 → 4/2.2/1.3（接近率更小 = 松键滑行
    //     更长，滑行到 10% 初速时间冰 0.58s→0.58s/浮冰 0.72→1.05s/蓝冰 1.21→1.77s）→「快但收得住 +
    //     松手长滑」手感（机制等价 MC 1.0 冰面船：明显快于水 + 大惯性）。
    //   - Land：kBoatSpeed × kBoatLandSpeedMul 0.3（2.4 blocks/s，最慢 —— 比走路 4.3 还慢但能开，
    //     用户要求「直接放陆地上开不会停」即陆地非零速）+ 接近率 kBoatLandAccel=10（高响应：
    //     松手即停、无滑行，贴地挪动手感）。
    float maxSpeed = kBoatSpeed;
    float approach = kBoatAccel;
    float turnRate = kBoatTurnRate;
    if (foundWater) {
        // 水档：基础参数
    } else if (onIce) {
        // t661 四轮冰档倍率下调（用户「冰上速度太快」）：1.4/1.7/2.0（11.2~16 blocks/s，约水面 1.4~2 倍，
        //   机制等价 MC 1.0 冰面船速 ~ 水面 2 倍），配合 iceSlipApproach 调小（blockregistry t661）→ 松键
        //   后长滑行（惯性更大，用户「惯性太小」）。「快但可控 + 松手长滑」的冰面手感。
        if (below == BlockRegistry::Ice)          maxSpeed = kBoatSpeed * 1.4f;
        else if (below == BlockRegistry::PackIce) maxSpeed = kBoatSpeed * 1.7f;
        else                                      maxSpeed = kBoatSpeed * 2.0f; // BlueIce（isIce 已保证）
        // t691：approach 改读 boatIceSlipApproach（船专用，t661 校准 4/2.2/1.3）—— t661 曾借玩家表
        //   iceSlipApproach 调船，连带改变玩家行走冰感；两表分离后各自独立调校不再牵连。
        approach = BlockRegistry::boatIceSlipApproach(below); // 冰面惯性（船专用滑度权威）
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

    // t630 撞碎荷叶（同 tick 段修法）：骑乘船速度 > kBoatLilySmashSpeed 时先碾碎 footprint 内 LilyPad
    //   （清 Air + emit lilyPadSmashed 掉物品），再走下方碰岸探测 / 位移碰撞（叶已清 → 叶不挡船不「撞岸」停）。
    //   低速叶仍挡（绕行，机制等价 MC 慢速船被 lily pad 阻挡）。
    if (world && speed > kBoatLilySmashSpeed) smashLilyPads(world, b.pos.x(), b.pos.y(), b.pos.z());

    // t611 修「撞岸后整船焊死不能动」（用户：撞到岸边应该还能倒退，后面是水）：t584 旧版探到岸（水面同高层
    //   有实心方块）即**双轴速度无条件清零** → 船贴岸后每帧 lerp 刚建起倒退速度就被清掉（清除在位移积分之前）
    //   → 船永不位移 → footprint 永不脱离岸块 → 死锁（撞岸 = 焊死）。修：**只挡朝岸分量** —— 对四个水平方向
    //   （±X / ±Z）分别探「船 footprint 前探一小步（kShoreProbe）后该层是否被挡」，被挡方向 n 的速度分量
    //   v·n > 0 部分清零（朝岸 → 停）；背向分量保留（倒退 / 侧滑离岸仍有效，机制等价 MC 1.0 船顶岸可倒退）。
    //   高速撞岸（speed ≥ kBoatCrashSpeed）仍整船撞毁（掉散件）。陆档 / 冰档不触发本检测（同 t584 语义）。
    //   t661 四轮「上滩冲量」：高速（≥ kBoatBeachSpeed，未达撞毁）撞岸 → 写 beachTimer（无水重力段据此
    //   限速爬上 ≤1 格高的岸沿，机制等价 MC 1.0 带速的船能冲上滩 / 低速撞岸只停住）。每次撞岸沿刷新计时。
    if (foundWater && world) {
        // 四向探测：footprint 中心沿该方向前探 kShoreProbe 后，水面同高层（y-1）是否被挡。
        //   probe 取 0.15（略小于半宽 0.5 / 半长 0.7 的接触裕度）：贴岸（footprint 已触岸）时朝岸侧必命中，
        //   背岸侧（后方是水）不命中 → 背向分量永不清除。
        //   review L10：探测传 ignoreIce=true（冰面豁免，见 boatFootprintBlocked 注释）—— 冰顶与水面同层，
        //   船须能越过冰缘滑上冰面（t611 冰面加速入口）；沙 / 草岸等真岸不豁免（防搁浅语义保持）。
        //   review rev2-C2：同传 ignoreLilyPad=true（睡莲豁免）—— 叶浮水面同层且探测边（0.65）大于撞碎扫描
        //   边（0.5），不豁免则叶先当岸清速 → 速度每帧只重建 ~0.5 永达不到撞碎阈值 3.0 → 船楔死叶前、
        //   撞碎永不触发。豁免后高速船保速驶入叶格撞碎（smashLilyPads 已在探测前跑）；低速船由位移碰撞
        //   （不传本参）挡在叶前（慢速 = 阻挡，机制等价 MC 慢速船被叶阻）。
        const bool blockedPosX = boatFootprintBlocked(world, b.pos.x() + kShoreProbe, b.pos.y() - 1.0f, b.pos.z(), /*ignoreIce*/ true, /*ignoreLilyPad*/ true);
        const bool blockedNegX = boatFootprintBlocked(world, b.pos.x() - kShoreProbe, b.pos.y() - 1.0f, b.pos.z(), /*ignoreIce*/ true, /*ignoreLilyPad*/ true);
        const bool blockedPosZ = boatFootprintBlocked(world, b.pos.x(), b.pos.y() - 1.0f, b.pos.z() + kShoreProbe, /*ignoreIce*/ true, /*ignoreLilyPad*/ true);
        const bool blockedNegZ = boatFootprintBlocked(world, b.pos.x(), b.pos.y() - 1.0f, b.pos.z() - kShoreProbe, /*ignoreIce*/ true, /*ignoreLilyPad*/ true);
        const bool hitShore = (blockedPosX && b.vx > 0.0f) || (blockedNegX && b.vx < 0.0f)
                           || (blockedPosZ && b.vz > 0.0f) || (blockedNegZ && b.vz < 0.0f);
        if (hitShore) {
            if (speed >= kBoatCrashSpeed) {
                outCrashed = true; // 高速撞岸 → 撞毁（掉散件）
            } else if (speed >= kBoatBeachSpeed) {
                // t661 中高速撞岸（未达撞毁）→ 写冲量（后续 fraction 跌破落下阈转陆档时限速爬上 ≤1 格岸沿）。
                //   低速撞岸不写 → 船贴岸壁停住（用户要求「上岸应难 / 需速度」，机制等价 MC 1.0）。
                b.beachTimer = kBoatBeachTimerMax;
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
    //   review rev2-C2：高速（> kBoatLilySmashSpeed）时位移碰撞豁免睡莲（同上方 tick 段修法）—— 撞碎扫描只
    //     盖当前 footprint，船头下一帧才进叶格；位移碰撞若仍把叶当实体 → 船被冻在叶格边、速度清零后每帧
    //     只重建 ~0.5 永达不到撞碎阈值 → 撞碎扫描永远盖不到叶（=「撞碎从未触发」根因）。豁免后船头进叶格
    //     （最大帧位移 ~0.35 < footprint 宽 1.0 不跳格）→ 下一帧撞碎扫描清叶。低速不豁免 → 叶挡（慢 = 阻挡）。
    const bool fastBoat = speed > kBoatLilySmashSpeed;
    const float dx = b.vx * float(dt);
    const float dz = b.vz * float(dt);
    // 世界边界（半宽外扩防船头穿出）：无 world → 不限。t556：X 用 kBoatHalfW / Z 用 kBoatHalfLen（矩形碰撞盒）。
    //   t711 五修「撞边界沉底后松键恢复海面」：边界 clamp 从「无损贴边滑停」改**视同撞墙** —— 旧版只把
    //   nx 钳到 minX/maxX，速度不清零（顶边蠕动），且边缘列 footprint 半数落界外（Air 非水）→ 水覆盖跌破
    //   迟滞阈转陆档 → 支撑顶扫到水底 → 船被重力拽沉到海底（「撞边界沉底」）；沉底后全列皆水、覆盖回升
    //   ≥ 浮起阈又转浮水档 → 船慢慢浮回海面（「松键恢复海面」假象，实为档位来回翻）。现边界命中即按撞墙
    //   处理：高速（≥ kBoatCrashSpeed）→ outCrashed=true（掉散件下船，机制等价 MC 船高速撞硬物损坏 ——
    //   世界边界对船就是一堵硬墙）；低速 → 清该轴速度（贴边停住不蠕动）。修后撞边要么撞毁要么贴边悬浮在
    //   水面（Y 仍走浮水段 —— 覆盖采样点界外算非水，6 点中界内侧 3 点水 + 界外 3 点非水 = 0.5：已浮态
    //   0.5 ≥ 落下阈 0.34 维持浮水，不坠底）。
    const float minX = world ? kBoatHalfW : -1e9f;
    const float maxX = world ? float(world->width()) - kBoatHalfW : 1e9f;
    const float minZ = world ? kBoatHalfLen : -1e9f;
    const float maxZ = world ? float(world->depth()) - kBoatHalfLen : 1e9f;
    // X 轴
    if (dx != 0.0f) {
        float nx = b.pos.x() + dx;
        bool edgeHit = false;
        if (nx < minX) { nx = minX; edgeHit = true; }
        if (nx > maxX) { nx = maxX; edgeHit = true; }
        if (edgeHit) {
            // t711：撞世界边界 = 撞墙（高速撞毁 / 低速停该轴；见上方注释块）。
            if (speed >= kBoatCrashSpeed) { outCrashed = true; b.vx = 0.0f; b.vz = 0.0f; }
            else b.vx = 0.0f;
        } else if (!boatFootprintBlocked(world, nx, b.pos.y(), b.pos.z(), /*ignoreIce*/ false, /*ignoreLilyPad*/ fastBoat)) {
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
        bool edgeHit = false;
        if (nz < minZ) { nz = minZ; edgeHit = true; }
        if (nz > maxZ) { nz = maxZ; edgeHit = true; }
        if (edgeHit) {
            // t711：撞世界边界 = 撞墙（同 X 轴）。
            if (speed >= kBoatCrashSpeed) { outCrashed = true; b.vx = 0.0f; b.vz = 0.0f; }
            else b.vz = 0.0f;
        } else if (!boatFootprintBlocked(world, b.pos.x(), b.pos.y(), nz, /*ignoreIce*/ false, /*ignoreLilyPad*/ fastBoat)) {
            b.pos.setZ(nz);
        } else {
            if (speed >= kBoatCrashSpeed) { outCrashed = true; b.vx = 0.0f; b.vz = 0.0f; }
            else b.vz = 0.0f;
        }
    }

    // 浮水 / 落地（同 tick 段逻辑；t508 二轮复盘）：有水 → Y 钉水面；无水 → 重力落支撑面。
    //   旧版无脑 b.pos.setY(surfY) 在无水时 surfY=fallback=pos.y → 陆地骑船 Y 恒不变（悬空）。改分支同 tick。
    //   t584：foundWater / surfY 在函数头部介质检测段已查（同帧复用，不再二次扫柱）。
    //   t661 四轮：支撑段重写为 footprint 全列最高支撑顶 + 冲量登岸爬升（同 tick 段修法；「从海里直接开上
    //   上岸」= 旧版中心列支撑 + 无条件贴 restY → 岸下瞬间上吸 1.2 格上岸。现高差 >kBoatBeachSnap 须
    //   beachTimer 冲量 + 限速爬升，低速顶岸只停住）。
    if (foundWater) {
        // t661 四轮「冲量登岸」（用户「从海里直接开上岸太容易 / 上岸应需速度」）：高速撞岸写了 beachTimer
        //   的船，若 footprint 支撑顶高出水面 ~1 格（1 格高岸沿），限速把 Y 爬向岸顶稳态（而非钉水面）——
        //   爬过岸顶（Y ≥ 岸块 cellY+1）后位移碰撞所在层为岸上方空气层 → 船滑上滩。低速撞岸无冲量 →
        //   Y 恒钉水面、被岸壁挡停（机制等价 MC 1.0 慢速顶岸停住）。仅 beachTimer>0 时多扫一次支撑（热路径
        //   零开销）；上岸后 fraction 跌破落下阈自然转陆档（下方 else 分支接管贴稳态）。
        bool climbing = false;
        if (b.beachTimer > 0.0f && world) {
            const float supportTop = boatFootprintSupportTop(world, b.pos.x(), b.pos.y(), b.pos.z());
            const float restY = supportTop + kBoatHullBottom;
            if (supportTop >= 0.0f && restY > b.pos.y() + kBoatBeachSnap
                && restY - b.pos.y() <= 1.25f) { // 仅 ≤1 格高岸沿可爬（更高 = 墙，不爬）
                b.pos.setY(std::min(b.pos.y() + kBoatBeachClimbRate * float(dt), restY));
                b.beachTimer = std::max(0.0f, b.beachTimer - float(dt));
                // 爬升中清朝岸残余速度（岸壁挡着，保速无意义；爬上后 wish 重建）。
                b.vx = 0.0f;
                b.vz = 0.0f;
                climbing = true;
            } else {
                // 支撑不构成可登台阶（同层岸 / 深墙）→ 冲量无意义，清零（水面照常钉 surfY）。
                b.beachTimer = 0.0f;
            }
        }
        if (!climbing) b.pos.setY(surfY); // 常规浮水：Y 钉水面（爬升期 Y 已高于 surfY，不回钉）
    } else {
        // 无水重力落地（同 tick 段：footprint 支撑顶 → 贴稳态 / 冲量爬岸 / 下落钳不穿）。
        const float supportTop = world ? boatFootprintSupportTop(world, b.pos.x(), b.pos.y(), b.pos.z()) : -1.0f;
        if (supportTop >= 0.0f) {
            const float restY = supportTop + kBoatHullBottom;
            if (b.pos.y() <= restY) {
                if (restY - b.pos.y() <= kBoatBeachSnap) {
                    b.pos.setY(restY); // 小高差贴稳态（同层岸沿平滑上岸，防抖动）
                } else if (b.beachTimer > 0.0f) {
                    // t661 冲量登岸：限速爬上 1 格岸沿（爬升期间船 Y 越过岸块顶后 moveAxis 不再被挡 →
                    //   玩家继续 W 船滑上滩；冲量耗尽 / 中途松键 → 停在岸壁 = 上岸须带速度）。
                    b.pos.setY(b.pos.y() + kBoatBeachClimbRate * float(dt));
                    b.beachTimer = std::max(0.0f, b.beachTimer - float(dt));
                }
            } else {
                b.pos.setY(std::max(b.pos.y() - kBoatGravity * float(dt), restY)); // 下落钳到不穿支撑
            }
        } else {
            b.pos.setY(b.pos.y() - kBoatGravity * float(dt)); // 无支撑 → 自由下落
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

bool BoatManager::hitBoatFromRay(const QVector3D &origin, const QVector3D &dir, float maxDist, World *world)
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
    // t661 四轮「创造攻击船不掉落船物品」：掉落格从「船中心格」改为「船侧首个非实体水平邻格」（同双半砖
    //   掉落散布模式）。根因非「创造跳过掉落」（hitBoatFromRay / onBoatBroken 全模式同路径）——而是掉落
    //   物生成在船中心格 = 常与**攻击者本人所在格重合**（玩家站船上 / 骑乘中攻击自己脚下的船：骑乘者
    //   m_pos = 船位），0.5s 免拾窗一过 pickupScan 立即吸回 → 用户全程看不到掉落物 =「不掉落」。邻格散布
    //   后实体落在船侧（玩家拾取半径边缘外 / 水面漂浮），肉眼可见可拾取，机制等价 MC 船被攻击掉落在船旁。
    // t711 五修：邻格筛选升级 —— 旧版固定 4 邻**随机**取，船贴岸 / 冰时 4 邻常含实心格 → 掉落物埋进方块里
    //   不可见（观感「不掉落」，创造多在岸边测、生存多在水面测故显模式差异）。现带 world 参优先取**非
    //   collidable** 的邻格（空气 / 水面均落得下）作掉落格；4 邻全实心（船嵌在窄缝）→ 退回船中心格上
    //   一格（y+1，掉落物自重落顶）。world null（防御）→ 旧 4 邻随机行为。
    int dropX = int(std::floor(bp.x())), dropZ = int(std::floor(bp.z()));
    int dropY = int(std::floor(bp.y()));
    static constexpr int kDropNb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    if (world) {
        // 非实心邻格中随机取一（保散布随机观感）；同时要求邻格「上方一格也非实心」防掉进 1 格深坑壁内。
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
            dropY += 1; // 4 邻全实心（窄缝嵌船）→ 掉头顶上一格（自重落顶，不埋）
        }
    } else {
        // 无世界可查（防御路径，当前 caller 恒传 world）：退回旧 4 邻随机（不查实体）。
        const int nb = QRandomGenerator::global()->bounded(4);
        dropX += kDropNb[nb][0];
        dropZ += kDropNb[nb][1];
    }
    emit boatBroken(dropX, dropY, dropZ, bt);
    notifyChanged();
    return true;
}
