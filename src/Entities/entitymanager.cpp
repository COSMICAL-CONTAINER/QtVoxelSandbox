#include "entitymanager.h"
#include "world.h" // tick / resolvePlayerPush / aiWander 只读 World::isSolid/blockAt/width/depth（向下依赖；PLAN §2 Entities→World 合规）

#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QtMath>    // qFloor, qRadiansToDegrees
#include <algorithm> // std::clamp, std::min, std::move
#include <cmath>     // std::sqrt, std::sin, std::cos

namespace {
Q_LOGGING_CATEGORY(lcEnt, "vo.entity") // 模块化日志（PLAN §2-F）；未在 main.cpp 过滤，落 log 可见

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
bool mobAabbHitsSolid(World *world, float cx, float cy, float cz, float r)
{
    if (!world) return false;
    const float minx = cx - r, maxx = cx + r;
    const float miny = cy - r, maxy = cy + r;
    const float minz = cz - r, maxz = cz + r;
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int y0 = int(std::floor(miny)), y1 = int(std::ceil(maxy)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (world->isSolid(x, y, z)) return true;
    return false;
}
} // namespace

EntityManager::EntityManager(QObject *parent) : QObject(parent) {}

// 生成默认测试生物：委托 spawnMobTyped（mobType=0、#ff5555、满血）。t239 调试入口（M 键）。
void EntityManager::spawnMob(int x, int y, int z)
{
    spawnMobTyped(x, y, z, 0, QStringLiteral("#ff5555"), kDefaultMaxHealth);
}

// t239 生物基类统一生成入口：满血 + 未死 + AI 初值（wanderTimer=0 → tick 首帧即选第一次向）。
//   达 kCap 跳过 + 告警（防溢出，spec「实体数量有上限」）。bump revision → QML Repeater 追加 delegate。
void EntityManager::spawnMobTyped(int x, int y, int z, int mobType, const QString &color, int maxHealth)
{
    if (int(m_entities.size()) >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); spawnMobTyped skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    e.radius = 0.5f;
    e.pushable = true;
    e.kind = Mob;
    e.color = color.isEmpty() ? QStringLiteral("#ff5555") : color;
    e.mobType = mobType;
    e.maxHealth = maxHealth > 0 ? maxHealth : kDefaultMaxHealth;
    e.health = e.maxHealth;
    e.dead = false;
    e.hurtFlash = 0.0f;
    e.deathTimer = 0.0f;
    e.yawRad = 0.0f;
    e.wanderTimer = 0.0f; // 0 → tick 首帧选第一次向（避免所有 mob 同步起步）
    e.wanderSpeed = 0.0f;
    e.moveSpeed = 0.0f;
    // t241 行走 / 吃草态初值：相位 0；未吃草；eatCooldown=0 → 羊首次 idle 即可扫描草丛（无需等待）。
    e.walkPhase = 0.0f;
    e.eatTimer = 0.0f;
    e.eatApplied = false;
    e.eatCooldown = 0.0f;
    m_entities.push_back(std::move(e));
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcEnt) << "spawned mob type" << mobType << "at" << x << y << z << "(total" << m_entities.size() << ")";
}

// t117 生成下落方块实体：存格中心 + blockId + pushable=false + kind=FallingBlock。bump revision →
// QML Repeater 追加 delegate（BlockCube 贴图渲染，复用地形图集）。达 kCap 跳过 + 告警（防溢出）。
// 重力 tick 下落，着地时 world->setBlockFromEntity 放置 blockId 并移除（链式塌落由 caller 控制）。
void EntityManager::spawnFallingBlock(int x, int y, int z, int blockId)
{
    if (int(m_entities.size()) >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); falling block spawn skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    e.radius = 0.5f;
    e.pushable = false; // 下落方块不被玩家推动（同掉落物变体）
    e.kind = FallingBlock;
    e.blockId = blockId;
    m_entities.push_back(std::move(e));
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcEnt) << "spawned falling block id=" << blockId << "at" << x << y << z
                  << "(total" << m_entities.size() << ")";
}

QVector3D EntityManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    return m_entities[size_t(i)].pos;
}

float EntityManager::radiusAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    return m_entities[size_t(i)].radius;
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
float EntityManager::headPitchAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep || e.eatTimer <= 0.0f) return 0.0f;
    const float p = (kEatDuration - e.eatTimer) / kEatDuration; // 周期内进度 0..1
    return kEatHeadPitch * std::sin(3.14159265f * p);           // sin(πp) 包络：起末 0、中段最深（负=低头）
}

// t239 mob 子类 id（t240 pig/cow/sheep；t242/t243 分流）。越界 → 0。
int EntityManager::mobTypeAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].mobType;
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
//   - health≤0 且未 dead → dead=true + deathTimer=kDeathTime + emit mobDied（坐标 floor(pos) + mobType；
//     t242 据它掉落猪排/皮革/羊毛）。dead 期间冻结 AI / 重力。
//   - dead / 非 Mob / 越界 / amount≤0 → 静默早退。
//   bump revision → 驱动 QML health/红闪/死亡绑定刷新。
void EntityManager::damageEntity(int i, int amount)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.dead || amount <= 0) return;

    e.health -= amount;
    if (e.health < 0) e.health = 0;
    e.hurtFlash = kHurtFlashTime;

    if (e.health <= 0) {
        // 死亡：冻结 AI / 重力（dead=true → tick 跳过 aiWander / 重力，仅 deathTimer 倒计时）+ 给 QML
        //   死亡动画窗口（kDeathTime）+ emit mobDied 让 t242 掉落。坐标用 floor(pos)（与 spawnItem 整数
        //   入口一致；t242 转发 ItemEntityManager.spawnItem）。
        e.dead = true;
        e.deathTimer = kDeathTime;
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
        qCInfo(lcEnt) << "mob" << i << "type" << e.mobType << "died at" << dx << dy << dz;
        emit mobDied(dx, dy, dz, e.mobType);
    } else {
        qCInfo(lcEnt) << "mob" << i << "took" << amount << "dmg, health=" << e.health << "/" << e.maxHealth;
    }

    ++m_revision;
    emit entitiesChanged();
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
bool EntityManager::aiWander(Entity &e, float dt, World *world, float worldW, float worldD)
{
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
    const float dx = -std::sin(e.yawRad) * e.wanderSpeed * dt;
    const float dz = -std::cos(e.yawRad) * e.wanderSpeed * dt;
    const float r = e.radius;

    // X 轴：世界边界 clamp（mob 半宽 0.5 → 中心不越 [0.5, world-0.5]）+ 方块碰撞撤回。
    float newX = e.pos.x() + dx;
    if (newX < 0.5f) newX = 0.5f;
    if (newX > worldW - 0.5f) newX = worldW - 0.5f;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), r)) newX = e.pos.x();

    // Z 轴：用已更新的 X + 同样边界 clamp / 方块碰撞撤回（两轴顺序敏感，Z 参照可能已撤回的 newX）。
    float newZ = e.pos.z() + dz;
    if (newZ < 0.5f) newZ = 0.5f;
    if (newZ > worldD - 0.5f) newZ = worldD - 0.5f;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, r)) newZ = e.pos.z();

    bool moved = false;
    if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
    if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }

    e.moveSpeed = moved ? e.wanderSpeed : 0.0f; // 撞墙 → 腿停（moveSpeed=0），t241 腿摆频率随它

    // 撞墙（两轴都未动）→ 缩短 timer 下帧大概率换向离开墙角（避免一直顶墙原地不动）。
    if (!moved) e.wanderTimer = std::min(e.wanderTimer, 0.2f);

    return moved;
}

// t241 羊吃草：检测 / 消耗 entity 前方一格草丛（机制等价 MC 羊吃草：草丛消失 + 其下草方块变泥土）。
//   目标列 = 沿 yaw 朝向 reach=0.7 前方（头部前方）；草丛格 y = 身体格（floor(pos.y − radius)，草丛生于地
//   表上方一格 = 羊身体所在格）；其下地表格 = bodyY − 1（草方块 Grass）。OOB → 安全返 false（blockAt 越界
//   返 air ≠ TallGrass，setWaterSilent 越界返 false，均不副作用）。
//   consume=true：草丛→Air（静默写，setWaterSilent 通用入口；非玩家破块 → 不发 broken/placed → 免粒子 / 音 /
//     掉落噪音，同水流蔓延 / 作物生长模式）；其下若 Grass → Dirt（机制等价 MC 草地变泥土）。
//   consume=false：仅检测（决定是否开吃草周期）。返回前方是否找到草丛。
bool EntityManager::sheepEatGrass(Entity &e, World *world, float worldW, float worldD,
                                  bool consume)
{
    if (!world) return false;
    // 前方列坐标（沿 yaw 朝向 reach 距离）：dir = (-sin, 0, -cos)（与 aiWander / player wishHoriz 同约定）。
    const float fx = e.pos.x() - std::sin(e.yawRad) * kEatReach;
    const float fz = e.pos.z() - std::cos(e.yawRad) * kEatReach;
    const int cx = qFloor(fx);
    const int cz = qFloor(fz);
    // 身体格 y（草丛生于地表上方一格 = 此格）；地表格 = bodyY − 1（草方块）。
    const int bodyY = qFloor(e.pos.y() - e.radius);
    const int groundY = bodyY - 1;
    // 越界 / 非法 y → 安全返 false（blockAt 亦返 air，但显式守避免 floor 滚动到负域读 chunk 边界外）。
    if (cx < 0 || cz < 0 || cx >= int(worldW) || cz >= int(worldD)) return false;
    if (bodyY < 1 || groundY < 0) return false;

    if (world->blockAt(cx, bodyY, cz) != BlockRegistry::TallGrass) return false; // 前方非草丛 → 不吃

    if (consume) {
        // 草丛→空气（静默写；setWaterSilent 是 World 的通用静默 state 写入口 —— 名字历史遗留 water-first，
        //   实现支持任意 id+state，已由 tickCropGrowth 复用写入小麦作物 state）。
        world->setWaterSilent(cx, bodyY, cz, BlockRegistry::Air, 0);
        // 其下草方块→泥土（机制等价 MC 羊吃草后草地变泥土；非草方块不动）。
        if (world->blockAt(cx, groundY, cz) == BlockRegistry::Grass)
            world->setWaterSilent(cx, groundY, cz, BlockRegistry::Dirt, 0);
        qCInfo(lcEnt) << "sheep ate tall grass at" << cx << bodyY << cz
                      << "(grass block below -> dirt)";
    }
    return true;
}

// 玩家推动解析：对每个 pushable 实体做「玩家 AABB（XZ 矩形）vs 实体圆（XZ，半径=entity.radius）」
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
        if (!e.pushable || e.dead) continue; // 掉落物等非推动 + t239 dead mob 跳过

        const float r = e.radius;
        // 垂直区间重叠判定（实体为立方体：[pos.y−r, pos.y+r]）。
        if (e.pos.y() + r <= pminY || e.pos.y() - r >= pmaxY) continue;

        // XZ 平面 AABB-vs-Circle 穿透求解。
        const float cx = std::clamp(e.pos.x(), px - halfW, px + halfW);
        const float cz = std::clamp(e.pos.z(), pz - halfW, pz + halfW);
        const float dx = e.pos.x() - cx;
        const float dz = e.pos.z() - cz;
        const float dist2 = dx * dx + dz * dz;
        if (dist2 >= r * r) continue; // 无 XZ 穿透

        float newX = e.pos.x();
        float newZ = e.pos.z();
        const float dist = std::sqrt(dist2);
        if (dist > 1e-5f) {
            const float push = r - dist;
            newX = e.pos.x() + dx / dist * push;
            newZ = e.pos.z() + dz / dist * push;
        } else {
            // 实体中心在玩家 AABB 内：沿最近面推出（min 四向距离 → 最短穿透方向）。
            const float toMinX = e.pos.x() - (px - halfW);
            const float toMaxX = (px + halfW) - e.pos.x();
            const float toMinZ = e.pos.z() - (pz - halfW);
            const float toMaxZ = (pz + halfW) - e.pos.z();
            const float m = std::min({toMinX, toMaxX, toMinZ, toMaxZ});
            if (m == toMinX)      newX = px - halfW - r;
            else if (m == toMaxX) newX = px + halfW + r;
            else if (m == toMinZ) newZ = pz - halfW - r;
            else                  newZ = pz + halfW + r;
        }

        // 世界碰撞钳制（t104：mob AABB footprint 全格扫，仿 player aabbHitsSolid）：扫 mob 立方体 AABB
        // 覆盖的所有格子（非旧版「只查中心格」），任一实体方块 → 撤回该轴推动（防穿墙）。X/Z 两轴独立
        // 判定，Z 轴参照可能已撤回的 newX（两轴独立但顺序敏感）。旧版单格检查在斜推角落时 mob 中心可能
        // 仍在空气 → 不撤回 → 下帧中心入墙才撤回 → 反复跳变 = jitter；全格扫使任一部分触墙即撤回 → 消除。
        if (world) {
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), r)) newX = e.pos.x();
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, r))     newZ = e.pos.z();
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
                const int supportY = qFloor(e.pos.y() - r) - 1; // 实体底面下方一格（与 tick 复探同公式）
                if (supportY < 0 || !world->isSolid(ncx, supportY, ncz))
                    e.resting = false; // 新位置失支撑 → 解除静止让重力复探（推下阶梯 / 推离支撑面）
            } else {
                e.resting = false; // 无世界可查 → 保守解除（world=null 时 tick 早 return，不影响）
            }
            dirty = true;
        }
    }
    if (dirty) { ++m_revision; emit entitiesChanged(); }
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
//        扫实体所在列首个实体方块 → 命中则贴其顶面（solidCellY+1+kRestOffset）停下、vy=0、resting=true。
//        越界（cy<0）查 isSolid 返 false → 不误判虚空地面，实体继续落。
//     3) pos / resting 任一真变 → dirty，末尾统一 bump revision + emit（驱动 QML {revision; posAt} 绑定重算）。
//   void-loss 兜底：Mob 跌出世界底部（pos.y<0，如被推出边界外无支撑）→ 标记移除（防永久下落）。
//
// 移除用索引收集 + 循环后逆序 erase（保索引有效）。
void EntityManager::tick(qreal dt, World *world)
{
    if (!world || m_entities.empty()) return;
    const float worldW = float(world->width());
    const float worldD = float(world->depth());
    bool dirty = false;
    std::vector<int> toRemove; // FallingBlock 着地 / 跌出 + t239 mob deathTimer 到 / void-loss 索引（逆序 erase）

    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        Entity &e = m_entities[size_t(idx)];

        // --- FallingBlock（t117/t220）：重力 + 着地放置 / 变掉落物 + 移除（无 resting 态；落到底即转为方块或掉落物）---
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
            if (supportCellY >= 0) {
                // 着地：在支撑方块上方一格放置 blockId（覆盖空气 / 水；t220 沙落水填堵水格）+ 标记移除。
                world->setBlockFromEntity(cx, supportCellY + 1, cz, quint8(e.blockId));
                toRemove.push_back(idx);
                dirty = true;
            } else if (dropCellY >= 0) {
                // 沙遇不完整方块失撑 → 变掉落物（掉落点 = 不完整方块上方一格 = 沙应停位）。发信号由呈现层
                //   转发 ItemEntityManager.spawnItem（同 spawnItem 模式；分层：Entities 层发语义事件，呈现层
                //   只消费）。不放置方块、不动原不完整方块（仅完整立方可支撑沙，机制等价 MC 沙落火把碎成掉落物）。
                emit fallingBlockDropped(cx, dropCellY + 1, cz, e.blockId);
                toRemove.push_back(idx);
                dirty = true;
            } else if (newY <= 0.0f) {
                // 全列无支撑且已跌出世界底部 → 移除（防永久下落；正常世界 y=0 有石头层不触发）。
                toRemove.push_back(idx);
                dirty = true;
            } else if (newY != e.pos.y()) {
                e.pos.setY(newY); // 继续自由下落（穿过 air / 水）
                dirty = true;
            }
            continue; // 不走 Mob 的 AI / resting / 重力逻辑
        }

        // --- Mob（t239）---
        if (e.kind == Mob) {
            if (e.dead) {
                // 死亡态：冻结 AI / 重力，仅 deathTimer 倒计时（给 QML 播死亡动画窗口）+ hurtFlash 衰减
                //   （让 killing blow 的红闪自然褪去）。deathTimer≤0 → 标记移除（逆序 erase）。
                e.deathTimer -= float(dt);
                if (e.deathTimer <= 0.0f) { toRemove.push_back(idx); dirty = true; }
                if (e.hurtFlash > 0.0f) {
                    e.hurtFlash -= float(dt);
                    if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; } // 红闪结束 → bump 让 QML 翻回 baseColor
                }
                continue; // dead：不走 AI / 重力
            }

            // t239 AI wander 自主移动（水平）：随机选向 + 时间片 + 逐轴 AABB 碰撞。位移 → dirty（驱动 QML 位置绑定）。
            // t241 羊吃草门控：eatTimer>0（吃草周期内）→ 跳过 wander + 强制 idle 站立（腿停 + 头俯仰），仅推进
            //   吃草周期；否则走 AI wander，并据 idle + 扫描冷却决定是否开吃草周期。
            const bool isSheep = (e.mobType == MobSheep);
            const bool eating = isSheep && e.eatTimer > 0.0f;
            if (eating) {
                // 吃草周期：推进计时；到 apply 阈值时消耗前方草丛（草丛→空气 + 下草→泥土）；周期内强制 idle。
                e.eatTimer -= float(dt);
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
                e.moveSpeed = 0.0f; // 站立吃草 → 腿停（walkPhase 冻结于上次值）
                dirty = true;       // headPitch 随 eatTimer 变 → 每帧 bump 让 QML 头俯仰绑定刷新
            } else {
                // 非吃草：扫描冷却倒数（仅羊）；AI wander；羊 idle 且冷却到 → 扫前方草丛决定是否开吃。
                if (isSheep && e.eatCooldown > 0.0f) e.eatCooldown -= float(dt);
                if (aiWander(e, float(dt), world, worldW, worldD)) dirty = true;
                if (isSheep && e.eatCooldown <= 0.0f && e.wanderSpeed <= 0.0f) {
                    // idle 且扫描冷却到：前方有草丛 → 开吃草周期（headPitch 动画 + 中段消耗）；无 → 重置短冷却再等。
                    if (sheepEatGrass(e, world, worldW, worldD, /*consume=*/false)) {
                        e.eatTimer = kEatDuration; // 进入周期（apply 阈值时才真正消耗，保低头→嚼→抬头 时序）
                        e.eatApplied = false;
                        dirty = true;
                    } else {
                        e.eatCooldown = kEatScanInterval; // 空扫描 → 节流冷却
                    }
                }
            }

            // 受击红闪衰减（非 dead Mob）：仅在跨过 0 时 bump（红闪期间 colorAt 恒红无需每帧 bump；结束翻回 baseColor）。
            if (e.hurtFlash > 0.0f) {
                e.hurtFlash -= float(dt);
                if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; }
            }

            // t241 行走动画相位推进：moveSpeed>0（行走 / 被推）→ walkPhase 前进（fmod 2π，QML 据它驱动腿摆）；
            //   idle / 吃草 / 撞墙 → 冻结（moveSpeed=0 不进，腿停于上次相位）。每推进帧 bump dirty 让绑定刷新。
            if (e.moveSpeed > 0.0f) {
                e.walkPhase = std::fmod(e.walkPhase + e.moveSpeed * float(dt) * kWalkFreq, 6.2831853f);
                dirty = true;
            }
        }

        // --- Mob（非 dead）/ Item：原有 resting + 重力 + 垂直运动（cx/cz 在 AI 行走后重算）---
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过

        // 已落地：复探支撑格是否仍实体。失支撑 → 续落。
        if (e.resting) {
            const int supportY = qFloor(e.pos.y() - e.radius) - 1; // 实体底面下方那一格（= 支撑方块 cellY）
            if (supportY >= 0 && world->isSolid(cx, supportY, cz)) continue; // 仍实体 → 保持静止
            e.resting = false; // 支撑消失 → 续落（vy 已 0，从静止重新加速）
            dirty = true;
        }

        // 重力 + 下移（vy 向下为负）。
        e.vy -= kGravity * float(dt);
        if (e.vy < -kMaxFall) e.vy = -kMaxFall;
        const float mobNewY = e.pos.y() + e.vy * float(dt);

        // 下移路径自顶向下扫实体所在列，找首个实体方块（防大 dt 穿过薄层；lessons「子步防穿墙」精神）。
        const int mobTopCell = qFloor(e.pos.y()); // 当前中心所在格（一般为空气）
        int mobBotCell = qFloor(mobNewY);
        if (mobBotCell > mobTopCell) mobBotCell = mobTopCell; // 防浮点噪声
        int mobSolidY = -1;
        for (int cy = mobTopCell; cy >= mobBotCell; --cy) {
            if (cy < 0) break; // 越界下方=空气（World 约定）→ 不视作地面，实体继续落
            if (world->isSolid(cx, cy, cz)) { mobSolidY = cy; break; }
        }

        if (mobSolidY >= 0) {
            // 落地：贴支撑方块顶面 + 静止偏移（底面 = mobSolidY+1 = 支撑方块顶）。钳 mobNewY 防穿越。
            const float restY = float(mobSolidY + 1) + kRestOffset;
            if (mobNewY <= restY || e.vy < 0.0f) {
                if (e.pos.y() != restY) { e.pos.setY(restY); dirty = true; }
                if (e.vy != 0.0f) { e.vy = 0.0f; dirty = true; }
                e.resting = true;
            }
        } else if (mobNewY != e.pos.y()) {
            e.pos.setY(mobNewY); // 自由下落（无命中）
            dirty = true;
        }

        // t239 void-loss 兜底：Mob 跌出世界底部（pos.y<0，如被推/走离边界外无支撑）→ 标记移除（防永久下落）。
        if (e.kind == Mob && e.pos.y() < 0.0f) { toRemove.push_back(idx); dirty = true; }
    }

    // 逆序 erase 已着地 / 跌出的 FallingBlock + deathTimer 到 / void-loss 的 Mob（逆序保未处理索引有效）。
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        m_entities.erase(m_entities.begin() + *it);

    if (dirty || !toRemove.empty()) { ++m_revision; emit entitiesChanged(); }
}
