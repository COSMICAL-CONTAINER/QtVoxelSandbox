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
// t252：AABB 的 XZ 用 halfW、Y 用 halfH（cow 0.40×0.50×0.40 等非立方 footprint；旧版单一 r 致 cow
//   垂直范围同 XZ，碰撞感失真）。
bool mobAabbHitsSolid(World *world, float cx, float cy, float cz, float halfW, float halfH)
{
    if (!world) return false;
    const float minx = cx - halfW, maxx = cx + halfW;
    const float miny = cy - halfH, maxy = cy + halfH;
    const float minz = cz - halfW, maxz = cz + halfW;
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int y0 = int(std::floor(miny)), y1 = int(std::ceil(maxy)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                // t333 水视穿透（同 t271 掉落物 / t220 水不挡沙）：World::isSolid 语义=「非 air」含 Water，
                //   会把水当墙 → mob 横向进不了水 + 流水推力被水格自身撤回（t333 根因「怪水上走 + 不被推」）。
                //   水非实体碰撞 → 排除后 mob 可入水游 / 被流水沿流推动，仍撞石头/泥土等真实体方块。
                if (world->blockAt(x, y, z) != BlockRegistry::Water && world->isSolid(x, y, z)) return true;
    return false;
}

// t298 mob 脚位（AABB 底面所在格）是否为水 —— 同玩家 PlayerController::feetInWater 语义（机制等价 MC
//   「脚在水里即受水中物理」）。feetCellY = floor(cy − halfH)（mob 底面格；pos.y 是中心，底面 = 中心−halfH）。
//   只读 World::blockAt；越界（fy<0）/ 无世界 → false。用于 tick 内判「mob 在水中」→ 减速 + 浮力 + 流水推动。
bool mobFeetInWater(World *world, float cx, float cy, float cz, float halfH)
{
    if (!world) return false;
    const int fy = int(std::floor(cy - halfH));
    if (fy < 0) return false;
    return world->blockAt(int(std::floor(cx)), fy, int(std::floor(cz))) == BlockRegistry::Water;
}

// t362 mob 落地支撑复探：footprint XZ 任一列在支撑层 supportY 有实体（非水）方块 → true。
//   取样同 mobAabbHitsSolid（floor(min)..ceil(max)-1，严格覆盖排除仅贴面列）。只读 World。
//   用于替代旧版「仅中心列」支撑复探 —— 见 tick 内 resting 复探注释（修「mob 下 1 格台阶卡死」根因）。
bool mobFootprintHasSupport(World *world, float cx, float cz, int supportY, float halfW)
{
    if (!world || supportY < 0) return false; // 无世界 / 脚位已在 y=0 之下（无支撑层可查）→ 无支撑
    const int x0 = int(std::floor(cx - halfW));
    const int x1 = int(std::ceil(cx + halfW)) - 1;
    const int z0 = int(std::floor(cz - halfW));
    const int z1 = int(std::ceil(cz + halfW)) - 1;
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x)
            // t333 水视穿透（同 mobAabbHitsSolid）：水格不算实体支撑 → 怪不把水面当地面站着。
            if (world->blockAt(x, supportY, z) != BlockRegistry::Water && world->isSolid(x, supportY, z))
                return true;
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
//   t400：实体构造 + 入槽抽到 spawnMobCore（便于繁殖产幼崽复用）；本入口仅包一层 acquire 后立即 emit。
void EntityManager::spawnMobTyped(int x, int y, int z, int mobType, const QString &color, int maxHealth)
{
    const int slot = spawnMobCore(x, y, z, mobType, color, maxHealth);
    if (slot < 0) return; // 达 kCap（spawnMobCore 内已告警）
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcEnt) << "spawned mob type" << mobType << "at" << x << y << z
                  << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// t239 生物基类统一生成核心（spawnMobTyped 实现主体；t400 抽出便于繁殖产幼崽复用）：构造 Entity（碰撞箱 /
//   pos / 血量 / AI 初值 / 护甲随机）+ acquireSlot，返槽索引（达 kCap → -1 + 告警）。不 bump revision / 不 emit
//   （caller 决定 emit 时机：spawnMobTyped 立即 emit；tickBreeding 批量产幼崽后统一一次 emit，避免高频扇出
//   notify 风暴，同 t320/t354 批量收口纪律）。
int EntityManager::spawnMobCore(int x, int y, int z, int mobType, const QString &color, int maxHealth)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); spawnMobCore skipped at" << x << y << z;
        return -1;
    }
    Entity e;
    // t252/t293 碰撞箱按 mobType 设。t293 收紧贴合 MobModel 身体（旧值「大一圈」：被动 0.9 宽 vs 躯干 0.6~0.7、
    //   敌对 0.9 宽 vs MC 0.6 / 身体 0.4~0.8、牛 1.4 高 vs 身体 0.87）。现按「MobModel 实际身体半宽 + 小余量」
    //   取值：敌对 halfW=0.30（机制等价 MC 1.0 僵尸 / 骷髅 / 苦力怕 0.6 宽——手臂略超盒属 MC 风格，hitbox 只包
    //   躯干核心）；被动 halfW=0.40（躯干 0.6~0.7 + 余量，头 / 长身可能略超 Z 盒，同 MC 四足 hitbox 不含吻部）；
    //   牛 halfH 0.70→0.50（身体 0.87 高，旧 1.4 偏大）；其余 halfH 不变（已贴合：人形 1.8 = MC 玩家身高、
    //   spider 0.6）。mobModelYOff = modelLegBottom − halfH 自动随 halfH 调（腿底恒贴 collision 底面，免悬空）。
    //   标识符 / 模型全原创（§9 区隔，不照搬 MC 美术）。hostile 标志据 mobType 设（spawnHostileMob 入口已设，
    //   spawnMobTyped 兜底也判一次）。
    switch (mobType) {
        case MobPig:      e.halfW = 0.40f; e.halfH = 0.45f; break; // 0.8×0.9（躯干 0.7 宽 + 余量；旧 0.9 偏大）
        case MobCow:      e.halfW = 0.40f; e.halfH = 0.50f; break; // 0.8×1.0（躯干 0.64 宽 / 身体 0.87 高；旧 0.9×1.4 偏大一圈）
        case MobSheep:    e.halfW = 0.40f; e.halfH = 0.45f; break; // 0.8×0.9（躯干 0.6 宽 + 余量；旧 0.9 偏大）
        case MobShambler: e.halfW = 0.30f; e.halfH = 0.90f; e.hostile = true; break; // 0.6×1.8（机制等价 MC 僵尸 0.6 宽；旧 0.9 偏大）
        case MobBones:    e.halfW = 0.30f; e.halfH = 0.90f; e.hostile = true; break; // 0.6×1.8（机制等价 MC 骷髅 0.6 宽；旧 0.9 偏大）
        case MobStalker:  e.halfW = 0.30f; e.halfH = 0.90f; e.hostile = true; break; // 0.6×1.8（机制等价 MC 苦力怕 0.6 宽；旧 0.9 偏大一圈；t284）
        case MobSpider:   e.halfW = 0.45f; e.halfH = 0.30f; e.hostile = true; break; // 0.9×0.6 宽矮（躯干 0.8 宽 + 余量；旧 1.1 偏大；快速，t285）
        case MobChicken:  e.halfW = 0.30f; e.halfH = 0.40f; break; // 0.6×0.8 小型鸟（躯干 0.4 宽 / 站立 0.7 高；t398）
        case MobSquid:    e.halfW = 0.40f; e.halfH = 0.45f; break; // 0.8×0.9 水生软体（机制等价 MC 1.0 squid 0.8 宽；t399）
        default:          e.halfW = 0.50f; e.halfH = 0.50f; break; // MobTest / 通用：1×1×1（UnitCube 精确贴合，保 t95 旧路径）
    }
    // pos.y 用 halfH（非旧版固定 +0.5）：spawn 在空气格 y 上方贴地（resting 高度 = y + halfH）→
    //   免首帧 collision 底面嵌入地面再 snap（cow halfH=0.70 时旧 +0.5 会嵌 0.2 进支撑方块）。
    e.pos = QVector3D(x + 0.5f, y + e.halfH, z + 0.5f);
    e.pushable = true;
    e.kind = Mob;
    e.color = color.isEmpty() ? QStringLiteral("#ff5555") : color;
    e.mobType = mobType;
    e.maxHealth = maxHealth > 0 ? maxHealth : kDefaultMaxHealth;
    e.health = e.maxHealth;
    e.dead = false;
    e.hurtFlash = 0.0f;
    e.deathTimer = 0.0f;
    e.deathBurned = false;
    e.yawRad = 0.0f;
    e.wanderTimer = 0.0f; // 0 → tick 首帧选第一次向（避免所有 mob 同步起步）
    e.wanderSpeed = 0.0f;
    e.moveSpeed = 0.0f;
    // t241 行走 / 吃草态初值：相位 0；未吃草；eatCooldown=0 → 羊首次 idle 即可扫描草丛（无需等待）。
    e.walkPhase = 0.0f;
    e.eatTimer = 0.0f;
    e.eatApplied = false;
    e.eatCooldown = 0.0f;
    // t250 环境音初值：半步累加 0；idle 叫声倒计时随机化（[kAmbientMin,kAmbientMax)）防批量 spawn 的 mob
    //   首次叫声同步（同 wanderTimer=0 错峰起步同理）。stepAccum=0。
    e.stepAccum = 0.0f;
    e.aimTimer = 0.0f; // t331 骸骨拉弓瞄准计时（slot 复用防残留；仅 MobBones 用）
    // t398 鸡下蛋计时初值：随机化（kEggLayMin..Max）防批量 spawn 的鸡同步下蛋（同 ambientTimer 错峰模式）。
    //   非 MobChicken 的 mob 保留 0 不触发（tick Mob 分支仅 mobType==MobChicken 推进 eggTimer）。
    e.eggTimer = (mobType == MobChicken)
                 ? (kEggLayMin + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kEggLayMax - kEggLayMin))
                 : 0.0f;
    // t399 鱿鱼喷水推进计时初值：随机化（kSquidSwimIntervalMin..Max）防批量 spawn 的鱿鱼同步喷水（同 ambientTimer
    //   错峰模式）。非 MobSquid 的 mob 保留 0 不触发（tick Mob 分支仅 mobType==MobSquid 推进 swimTimer）。
    e.swimTimer = (mobType == MobSquid)
                  ? (kSquidSwimIntervalMin
                     + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kSquidSwimIntervalMax - kSquidSwimIntervalMin))
                  : 0.0f;
    e.ambientTimer = kAmbientMin
                     + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kAmbientMax - kAmbientMin);
    // t377 mob 随机护甲（仅 Shambler/Bones；spec「~80% no armor, ~20% a random piece/set」）。机制等价 MC 1.0
    //   僵尸/骷髅随机护甲。armorId = 0x300 + tier*4 + piece（与 ArmorRegistry id 段一致；本地常量避免跨层依赖
    //   Game/recipe.h —— Entities 层不向上 include）。tier 0..4（皮革/铁/铜/金/钻石）；piece 0..3（头/胸/腿/靴）。
    //   仅视觉 + spawn 随机（QML delegate 叠 tier 色护甲 Model）；不参与 mob 减伤（spec 仅要求偶遇）。
    e.armorHelmet = e.armorChest = e.armorLegs = e.armorBoots = 0;
    if (mobType == MobShambler || mobType == MobBones) {
        auto *rng = QRandomGenerator::global();
        if (rng->bounded(100) < 20) {                       // ~20% 有护甲
            constexpr int kArmorBase = 0x300;                // ArmorRegistry::ArmorIdBase（同源常量）
            const int tier = int(rng->bounded(5));           // 0..4 材质档
            const int base = kArmorBase + tier * 4;
            if (rng->bounded(2) == 0) {                      // 整套（4 部位全配）
                e.armorHelmet = base + 0;
                e.armorChest  = base + 1;
                e.armorLegs   = base + 2;
                e.armorBoots  = base + 3;
            } else {                                         // 单件（随机一个部位）
                const int piece = int(rng->bounded(4));
                if (piece == 0)      e.armorHelmet = base + 0;
                else if (piece == 1) e.armorChest  = base + 1;
                else if (piece == 2) e.armorLegs   = base + 2;
                else                 e.armorBoots  = base + 3;
            }
        }
    }
    // t400 繁殖态初值：Entity 默认成员初始化已把 loveTimer/breedCooldown/growTimer=0、baby=false，move 入槽时
    //   覆盖槽位旧值（slot 复用防残留 —— 上一任槽位若曾求偶 / 繁殖 / 是幼崽，复用时清回成体默认）。故新生 mob
    //   恒为成体、未求偶、可繁殖；幼崽态由 tickBreeding 产时单独设 baby=true + growTimer。无需在此显式赋。
    return acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）；返槽索引
}

// t117 生成下落方块实体：存格中心 + blockId + pushable=false + kind=FallingBlock。bump revision →
// QML Repeater 追加 delegate（BlockCube 贴图渲染，复用地形图集）。达 kCap 跳过 + 告警（防溢出）。
// 重力 tick 下落，着地时 world->setBlockFromEntity 放置 blockId 并移除（链式塌落由 caller 控制）。
void EntityManager::spawnFallingBlock(int x, int y, int z, int blockId)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); falling block spawn skipped at" << x << y << z;
        return;
    }
    Entity e;
    e.pos = QVector3D(x + 0.5f, y + 0.5f, z + 0.5f);
    e.halfW = 0.5f; // FallingBlock = 1×1×1 立方（同地形方块；t252 halfW/halfH 默认 0.5 显式留档）
    e.halfH = 0.5f;
    e.pushable = false; // 下落方块不被玩家推动（同掉落物变体）
    e.kind = FallingBlock;
    e.blockId = blockId;
    acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcEnt) << "spawned falling block id=" << blockId << "at" << x << y << z
                  << "(live" << m_liveCount << "slots" << m_entities.size() << ")";
}

// t283 生成箭矢投射物：存 origin + 3D 速度 vel（含 vy 抛物）+ kind=Arrow + pushable=false + 寿命。
//   halfW/halfH=0.06（细长杆视觉 + 碰撞最小；箭命中走 point-in-AABB 不读 halfW）。bump revision → QML
//   Repeater 追加 delegate（Arrow 分支细长杆定向 Model）。达 kCap 跳过 + 告警（防溢出）。
void EntityManager::spawnArrow(const QVector3D &origin, const QVector3D &vel)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); arrow spawn skipped at" << origin;
        return;
    }
    Entity e;
    e.pos = origin;
    e.halfW = 0.06f; // 箭细长杆（视觉 + 碰撞最小；命中检测用独立命中盒不读它）
    e.halfH = 0.06f;
    e.pushable = false; // 玩家走碰不推箭（同掉落物 / 下落方块变体）
    e.kind = Arrow;
    e.vx = vel.x(); // Arrow 复用 vx/vy/vz 作 3D 速度（Arrow 不走 Mob 击退衰减分支，无冲突）
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kArrowLifetime;
    acquireSlot(std::move(e)); // t256：slot 复用（保 count 单调不降 → Repeater delegate 不泄漏）
    ++m_revision;
    emit entitiesChanged();
}

// t304 玩家弓射出的箭：与 spawnArrow（骷髅射出，命中玩家 t283）对称，差异在 arrowFromPlayer=true（命中 mob）+
//   arrowDamage 由蓄力决定（1..6）。tick Arrow 分支据 arrowFromPlayer 分流命中目标（true→mob / false→玩家）。
void EntityManager::spawnArrowPlayer(const QVector3D &origin, const QVector3D &vel, int damage)
{
    if (m_liveCount >= kCap) {
        qCWarning(lcEnt) << "entity cap reached (" << kCap << "); player arrow spawn skipped at" << origin;
        return;
    }
    Entity e;
    e.pos = origin;
    e.halfW = 0.06f; // 同 spawnArrow（细长杆视觉 + 碰撞最小）
    e.halfH = 0.06f;
    e.pushable = false;
    e.kind = Arrow;
    e.vx = vel.x();
    e.vy = vel.y();
    e.vz = vel.z();
    e.arrowLife = kArrowLifetime;
    e.arrowFromPlayer = true;                  // 命中 mob（非玩家）
    e.arrowDamage = damage > 0 ? damage : 1;   // 蓄力伤害（防御 ≥1）
    acquireSlot(std::move(e));
    ++m_revision;
    emit entitiesChanged();
}

// t280 敌对生物生成入口：委托 spawnMobTyped 设碰撞箱 / 血量 / 颜色（Shambler 暗绿、Bones 灰白，§9 原创配色）。
//   spawnMobTyped 内 switch 据 mobType 设 hostile=true（兜底）。spec「黑暗刷怪调度」周期 spawn 调用它。
//   mobType 非 Shambler/Bones → 仍生成但非敌对语义（防御；正常 caller 只传这两种）。
void EntityManager::spawnHostileMob(int x, int y, int z, int mobType)
{
    QString color;
    int health = kHostileDefaultHealth;
    if (mobType == MobBones) {
        color = QStringLiteral("#d8d4c4"); // Bones：灰白骨色（机制等价 MC 骷髅；原创配色非照搬）
    } else if (mobType == MobStalker) {
        color = QStringLiteral("#5fa83a"); // Stalker：青绿色（机制等价 MC 苦力怕；原创配色非照搬）
    } else {
        color = QStringLiteral("#4a6a3a"); // Shambler：暗绿腐肉色（机制等价 MC 僵尸；原创配色）
        if (mobType != MobShambler) mobType = MobShambler; // 防御：非 Bones/Stalker 一律按 Shambler
    }
    spawnMobTyped(x, y, z, mobType, color, health);
    // spawnMobTyped 内 switch 已对 Shambler/Bones/Stalker 设 hostile=true；spawnHostileMob 仅收口语义入口。
}

// t374 被动生物群系化类型选取：据群系 id（World::biomeIdAt 编码）按 kPassiveSpawnWeights 加权随机返
//   MobPig/MobCow/MobSheep。机制等价 MC 1.0 群系化被动刷怪池（平原牛羊 / 森林猪富集；非排斥，仅概率差异）。
//   群系 id 越界 → 兜底 Plains（索引 0）。const 只读（仅 RNG 采样，不改实体数据）。
int EntityManager::pickPassiveMobType(int biomeId) const
{
    const int b = (biomeId >= 0 && biomeId < 4) ? biomeId : 0; // 越界兜底 Plains
    const int wCow   = kPassiveSpawnWeights[b][0]; // 列 0 = 牛 MobCow
    const int wSheep = kPassiveSpawnWeights[b][1]; // 列 1 = 羊 MobSheep
    const int wPig   = kPassiveSpawnWeights[b][2]; // 列 2 = 猪 MobPig
    const int wChick = kPassiveSpawnWeights[b][3]; // 列 3 = 鸡 MobChicken（t398）
    const int total  = wCow + wSheep + wPig + wChick;
    auto *rng = QRandomGenerator::global();
    int r = int(rng->bounded(total)); // [0, total)；bounded 返 quint32，同 tickHostileLife pickMob 模式
    if (r < wCow) return MobCow;
    r -= wCow;
    if (r < wSheep) return MobSheep;
    r -= wSheep;
    if (r < wPig) return MobPig;
    return MobChicken;
}

// t280 当前活体敌对生物数（hostile && !dead && kind==Mob）。供 spawn 调度上限判定。
int EntityManager::hostileCount() const
{
    int n = 0;
    for (const Entity &e : m_entities) {
        if (e.alive && e.kind == Mob && e.hostile && !e.dead) ++n;
    }
    return n;
}

// t388 床周敌对判定（sleep 机制「附近有怪物拒绝」）：任一活体敌对 mob 在 center 的 radius 球内 → true。
//   3D 欧氏距离（含 Y，防楼上 / 洞下贴脸的敌对漏判）。const 只读自身数据。
bool EntityManager::hostileNearby(const QVector3D &center, float radius) const
{
    const float r2 = radius * radius;
    for (const Entity &e : m_entities) {
        if (!e.alive || e.kind != Mob || !e.hostile || e.dead) continue;
        const QVector3D d = e.pos - center;
        if (d.lengthSquared() <= r2) return true;
    }
    return false;
}

// t280 第 i 个实体是否敌对（hostile=true 的活体 Mob）。越界 / 非敌对 → false。
bool EntityManager::isHostileAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.alive && e.kind == Mob && e.hostile;
}

// t280 第 i 个 mob 是否正在燃烧（火焰视觉）：t344 火烧态（fireTimer>0，岩浆/火点燃；ALL mobs 含 passive）
//   OR 敌对日光 burning（tickHostileLife 每 tick 重算缓存 Entity.burning）。越界 / 非 Mob → false。
//   QML 据 isBurningAt 显火焰动画（t344：passive 着火亦显火焰 Model）。
bool EntityManager::isBurningAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    // t344：火烧态（fireTimer>0）适用于所有 Mob；日光 burning 仅敌对。二者任一为真即显火焰。
    return e.alive && e.kind == Mob && (e.fireTimer > 0.0f || (e.hostile && e.burning));
}

// t280 黑暗刷怪调度 + 敌对日光燃烧 + 远距消失（详见头文件方法注释）。三职责一方法收口敌对生命周期。
//   分层（PLAN §2）：Entities 层，只读 World（blockAt/isSolid/skyLightAt/blockLightAt/heightAt/width/depth/height）
//   + 自身实体数据；写 EntityManager（spawn / releaseSlot / damageEntity）。world==null → 早 return。
void EntityManager::tickHostileLife(qreal dt, World *world, const QVector3D &playerPos, float skyBrightness)
{
    if (!world) return;
    const int worldW = world->width();
    const int worldD = world->depth();
    const int worldH = world->height();
    bool dirty = false;
    std::vector<int> toRemove; // 远距消失索引（releaseSlot；逆序处理避免索引漂移）

    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive || e.kind != Mob || !e.hostile) continue; // 仅敌对 Mob；passive / FallingBlock 跳过
        if (e.dead) continue; // 尸体走 deathTimer 链（tick 内已处理），不燃烧 / 不远距消失

        // (a) 日光燃烧判定：mob 所在格直接见天（skyLightAt>=15 = 无遮挡）且白天（skyBrightness>门槛）→ 燃烧。
        //   mob 中心格 (sx, sy, sz)：用 body 中心 Y（pos.y）所处方块格（同 tick 的窒息判定取身体高度处格）。
        //   shade（skyLightAt<15，如树叶下 / 屋檐 / 洞口）→ 不燃烧（机制等价 MC 树荫保护敌对）。
        //   夜间（skyBrightness<=门槛）→ 不燃烧（spec「白天燃烧消失」，仅白天）。
        const int sx = qFloor(e.pos.x());
        const int sy = qFloor(e.pos.y());
        const int sz = qFloor(e.pos.z());
        const bool exposedToSun = (sx >= 0 && sz >= 0 && sx < worldW && sz < worldD && sy >= 0 && sy < worldH)
                                  && world->skyLightAt(sx, sy, sz) >= 15;
        // t284：Stalker（苦力怕）非亡灵 → 白天**不**燃烧（机制等价 MC 苦力怕不像僵尸/骷髅那样日光起火；
        //   仅 Shambler/Bones 亡灵类燃烧）。Stalker 仍受远距消失 / spawn 调度约束（hostile=true）。
        // t385：降水（雨/雪/雷）时露天 mob 不燃烧（机制等价 MC 雨天遮日 → 亡灵不燃）—— 见天 + 所在列降水
        //   即视为「无直射日光」。与 fire-timer 灭火（主 tick）互补：日光 burning 由 rain 门控前置、岩浆 / 火点燃
        //   的 fireTimer 由主 tick 雨浇灭。
        const bool rainingHere = exposedToSun && world->isPrecipitatingAt(sx, sz);
        const bool inDaylight = exposedToSun && (skyBrightness > kBurnSkyBrightness)
                                 && e.mobType != MobStalker && !rainingHere;
        if (inDaylight) {
            if (!e.burning) { e.burning = true; dirty = true; } // 翻入燃烧 → bump（QML 显火焰）
            e.burnTimer += float(dt);
            if (e.burnTimer >= kBurnDamageInterval) {
                e.burnTimer -= kBurnDamageInterval;
                damageEntity(idx, 1); // 复用受击链：扣 1HP + 红闪 + （归零时）mobDied 死亡消失
                dirty = true;
            }
        } else {
            if (e.burning) { e.burning = false; dirty = true; } // 出日光 → 停燃烧（QML 隐火焰）
            e.burnTimer = 0.0f; // 不燃烧时清零（机制等价 MC 出日光即停烧；非 MC「燃烧一段时间后才灭」简化）
        }

        // (b) 远距消失：敌对距玩家 > kFarDespawn（且不正在燃烧 —— 燃烧中让 damageEntity 链自然处理消失，
        //   避免火焰视觉被远距消失打断）。releaseSlot 标空槽（slot 复用保 count 单调不降，lessons-learned t256）。
        //   距离用 XZ 主导（玩家与 mob 多在同一高度层；Y 大差不影响「水平远」语义）。
        if (!e.burning) {
            const float dx = e.pos.x() - playerPos.x();
            const float dz = e.pos.z() - playerPos.z();
            if ((dx * dx + dz * dz) > kFarDespawn * kFarDespawn) {
                toRemove.push_back(idx);
                dirty = true;
            }
        }
    }

    // 逆序 releaseSlot（避免索引漂移；release 不 erase，但保持逆序习惯以备 erase 演进）。
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
        releaseSlot(*it);
    }

    // (c) spawn 调度：节流到 kSpawnInterval 秒一次。达 kHostileMobCap 则跳过（passive / FallingBlock 走 kCap
    //   不受此限）。每周期 kSpawnAttempts 次随机选点（地表 / 洞穴），首个合格点 spawn 一个敌对后本周期收手
    //   （慢速堆叠到 cap，机制等价 MC 周期 spawn）。hostileCount 在 releaseSlot 后算（含本 tick 远距消失腾出的槽）。
    m_spawnAccum += float(dt);
    if (m_spawnAccum >= kSpawnInterval) {
        m_spawnAccum = 0.0f;
        if (hostileCount() < kHostileMobCap && m_liveCount < kCap) {
            auto *rng = QRandomGenerator::global();
            const float pfx = playerPos.x();
            const float pfz = playerPos.z();
            for (int attempt = 0; attempt < kSpawnAttempts; ++attempt) {
                // 环内随机选点：角度 [0,2π)、距离 [kSpawnMinDist, kSpawnMaxDist]。
                const float ang = float(rng->bounded(360)) * (0.017453292519943295f);
                const float dist = kSpawnMinDist
                                   + float(rng->bounded(1000)) / 1000.0f * (kSpawnMaxDist - kSpawnMinDist);
                const int cx = int(pfx + std::cos(ang) * dist);
                const int cz = int(pfz + std::sin(ang) * dist);
                if (cx < 0 || cz < 0 || cx >= worldW || cz >= worldD) continue;
                const int surfH = world->heightAt(cx, cz);
                if (surfH < 1) continue; // 列无地表（极端情况）
                // 选地表 or 洞穴（各 50%）：地表贴 surfH+1、洞穴在地表下随机一层。
                int cy = surfH + 1;
                const bool caveSpawn = (rng->bounded(2) != 0);
                if (caveSpawn) {
                    const int caveMax = surfH - 3; // 洞穴最深到 surfH-3（保留表面 3 格地层不动）
                    const int caveMin = 2;         // 不刷基岩层（y<2 接近基岩）
                    if (caveMax <= caveMin) continue;
                    cy = caveMin + rng->bounded(caveMax - caveMin + 1);
                }
                if (cy < 1 || cy >= worldH - 1) continue; // 越界 / 顶格
                // 三条件：目标格 air + 下方 solid（有地板）+ 有效光 < 阈值。
                if (world->blockAt(cx, cy, cz) != BlockRegistry::Air) continue;
                if (!world->isSolid(cx, cy - 1, cz)) continue; // 脚下须有支撑（防悬空刷怪）
                const quint8 skyL = world->skyLightAt(cx, cy, cz);
                const quint8 blkL = world->blockLightAt(cx, cy, cz);
                const float effSkyL = float(skyL) * skyBrightness; // 天光乘昼夜（夜间→0、白天→原值）
                const float effLight = std::max(effSkyL, float(blkL));
                if (effLight >= kSpawnLightThreshold) continue; // spec「light<阈值(7)」
                // 合格点：spawn 一个敌对（Shambler / Bones / Stalker 等概率；t284 加 Stalker）。spawnMobTyped 内
                //   kCap 守卫；达 cap 静默跳过。机制等价 MC 1.0 黑暗刷怪池（僵尸 / 骷髅 / 苦力怕）。
                const int pickMob = rng->bounded(3);
                const int spawnType = (pickMob == 0) ? MobShambler : (pickMob == 1) ? MobBones : MobStalker;
                spawnHostileMob(cx, cy, cz, spawnType);
                qCInfo(lcEnt) << "hostile spawned type" << spawnType
                             << "at" << cx << cy << cz << "effLight=" << effLight
                             << "(hostile" << hostileCount() << "/" << kHostileMobCap << ")";
                break; // 本周期成功 spawn 1 个即收手（慢速堆叠；下个 kSpawnInterval 周期再尝试）
            }
        }
    }

    if (dirty) {
        ++m_revision;
        emit entitiesChanged();
    }
}

// t392 刷怪笼周期刷怪（见头文件方法注释）。机制等价 MC 1.0 刷怪笼：玩家在范围内时周期 spawn 1 敌对 mob。
//   实现策略 —— **按需扫描**：每 kSpawnerInterval 秒扫玩家所在格周围 ±kSpawnerScanRange 立方体找 Spawner 方块，
//   对每个笼判「玩家近 + 笼周敌对 < 本地 cap + 全局敌对 < 全局 cap + 找到合法 spawn 位」四条件全过 → spawn 1 只。
//   不维护 spawner 位置列表 → 破坏即停（blockAt != Spawner 自然跳过）、存档加载后仍能扫到（无 index 维护负担）。
void EntityManager::tickSpawners(qreal dt, World *world, const QVector3D &playerPos)
{
    if (!world) return;
    m_spawnAccumSpawner += float(dt);
    if (m_spawnAccumSpawner < kSpawnerInterval) return;
    m_spawnAccumSpawner = 0.0f;

    // 全局敌对上限（与 tickHostileLife 共享 kHostileMobCap；防刷怪笼 + 黑暗刷怪叠加爆量）。
    const int hostiles = hostileCount();
    if (hostiles >= kHostileMobCap || m_liveCount >= kCap) return;

    // 扫玩家所在格周围 ±kSpawnerScanRange 立方体（限 Y 到 [0, worldHeight)，防越界）。
    const int pcx = int(std::floor(playerPos.x()));
    const int pcy = int(std::floor(playerPos.y()));
    const int pcz = int(std::floor(playerPos.z()));
    const int worldW = world->width();
    const int worldD = world->depth();
    const int worldH = world->height();
    const int x0 = std::max(0, pcx - kSpawnerScanRange);
    const int x1 = std::min(worldW - 1, pcx + kSpawnerScanRange);
    const int y0 = std::max(0, pcy - kSpawnerScanRange);
    const int y1 = std::min(worldH - 1, pcy + kSpawnerScanRange);
    const int z0 = std::max(0, pcz - kSpawnerScanRange);
    const int z1 = std::min(worldD - 1, pcz + kSpawnerScanRange);

    int hostilesRunning = hostiles; // 本周期内已 spawn 的敌对数累加（防一周期刷出多笼 × N）
    bool dirty = false;

    for (int y = y0; y <= y1; ++y) {
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                if (world->blockAt(x, y, z) != BlockRegistry::Spawner) continue;
                // 玩家在范围内（XZ 距离 ≤ kSpawnerPlayerRange）。机制等价 MC 1.0 刷怪笼玩家 16 格内激活。
                const float ddx = float(x) - playerPos.x();
                const float ddz = float(z) - playerPos.z();
                if (ddx * ddx + ddz * ddz > kSpawnerPlayerRange * kSpawnerPlayerRange) continue;

                // 笼周敌对数（kSpawnerMobCheckRadius 球内）：≥ kSpawnerLocalCap 则本笼跳过（防刷爆）。
                const QVector3D spawnerCenter(float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f);
                if (hostileNearby(spawnerCenter, kSpawnerMobCheckRadius)) continue;

                // 全局敌对 cap：本周期已刷够则停（防多笼同周期刷爆）。
                if (hostilesRunning >= kHostileMobCap) break;

                // 找合法 spawn 位（笼 8 水平邻 + 笼同格上方 / 下方共 10 候选；首个「air + 下方 solid」）。
                //   spawnMobTyped 把 (x,y,z) 当格坐标、mob 中心放 (x+0.5, y+0.5, z+0.5)。机制等价 MC 刷怪笼
                //   在笼旁刷怪（笼自身不可站立 → 邻格 spawn）。Y 优先笼同高（玩家走入触发高度）。
                static const int kSpawnDx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
                static const int kSpawnDz[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
                int sx = -1, sy = -1, sz = -1;
                for (int i = 0; i < 8; ++i) {
                    const int cx = x + kSpawnDx[i];
                    const int cz = z + kSpawnDz[i];
                    if (cx < 0 || cz < 0 || cx >= worldW || cz >= worldD) continue;
                    // 优先笼同高（y）、次之 y+1（玩家跳上触发高度）；二者均堵 → 跳过本邻位。
                    for (int cyOff = 0; cyOff <= 1; ++cyOff) {
                        const int cy = y + cyOff;
                        if (cy < 0 || cy >= worldH - 1) continue;        // 须留一格空气在上（mob 占 2 格高）
                        const quint8 here = world->blockAt(cx, cy, cz);
                        const quint8 above = world->blockAt(cx, cy + 1, cz);
                        const quint8 below = world->blockAt(cx, cy - 1, cz);
                        if (here == BlockRegistry::Air && above == BlockRegistry::Air
                            && world->isSolid(cx, cy - 1, cz) && below != BlockRegistry::Water
                            && below != BlockRegistry::Lava) {
                            sx = cx; sy = cy; sz = cz;
                            break;
                        }
                    }
                    if (sx >= 0) break;
                }
                if (sx < 0) continue; // 笼周无合法 spawn 位 → 跳过本笼（下周期再试）

                // spawn 1 只敌对（Shambler / Bones 等概率；机制等价 MC 1.0 刷怪笼等概率随机刷怪）。
                auto *rng = QRandomGenerator::global();
                const int mobType = (rng->bounded(2) == 0) ? int(MobShambler) : int(MobBones);
                spawnHostileMob(sx, sy, sz, mobType);
                ++hostilesRunning;
                dirty = true;
            }
            if (hostilesRunning >= kHostileMobCap) break;
        }
        if (hostilesRunning >= kHostileMobCap) break;
    }

    if (dirty) {
        ++m_revision;
        emit entitiesChanged();
    }
}

// t256：第 i 个槽位是否活体（已分配未释放）。空槽 → false（呈现层 delegate 据它 visible 隐藏）。
bool EntityManager::aliveAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    return m_entities[size_t(i)].alive;
}

QVector3D EntityManager::posAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    return m_entities[size_t(i)].pos;
}

float EntityManager::radiusAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    return m_entities[size_t(i)].halfW;
}

// t252 Y 碰撞半高（QML F3+B hitbox scale.y 读）。越界 → 0。
float EntityManager::halfHeightAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    return m_entities[size_t(i)].halfH;
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

// t283 箭水平朝向（QML eulerRotation.y 定向杆）：据 vx/vz 用 player 同 yaw 约定（dir=(-sin,-cos)）→
//   yaw=atan2(-vx,-vz) 使杆本地 -Z 正对飞行水平方向。非 Arrow / 水平速度 ~0 / 越界 → 0。
float EntityManager::arrowYawAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Arrow) return 0.0f;
    const float h = std::sqrt(e.vx * e.vx + e.vz * e.vz);
    if (h < 1e-4f) return 0.0f;
    return qRadiansToDegrees(std::atan2(-e.vx, -e.vz));
}

// t283 箭俯仰（QML eulerRotation.x 定向杆）：pitch=atan2(vy, 水平速度)，正=上扬、负=下俯（抛物飞行中由正转负）。
//   非 Arrow / 水平速度 ~0 / 越界 → 0。
float EntityManager::arrowPitchAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Arrow) return 0.0f;
    const float h = std::sqrt(e.vx * e.vx + e.vz * e.vz);
    if (h < 1e-4f) return 0.0f;
    return qRadiansToDegrees(std::atan2(e.vy, h));
}

// t283 实体 3D 速度（F3 / 调试；Arrow 读 vx/vy/vz）。非活体 / 越界 → 零向量。
QVector3D EntityManager::velAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return QVector3D();
    const Entity &e = m_entities[size_t(i)];
    if (!e.alive) return QVector3D();
    return QVector3D(e.vx, e.vy, e.vz);
}

// t323 箭是否已嵌入方块（PlayerController::arrowPickupScan 近距拾取读；飞行中不拾免误拾）。
//   非 Arrow / 越界 → false。
bool EntityManager::isArrowStuckAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.kind == Arrow && e.arrowStuck;
}

// t323 箭是否玩家射出（仅玩家箭嵌入后可拾；骷髅箭防刷不拾，spec「SKELETON 箭不可拾取」）。非 Arrow / 越界 → false。
bool EntityManager::arrowFromPlayerAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    return e.kind == Arrow && e.arrowFromPlayer;
}

// t323 释放槽位（PlayerController 拾取嵌入箭全入背包后销毁箭；同 ItemEntityManager.removeAt 拾取销毁语义）。
//   委托 releaseSlot（标空槽 + 入 free list，保 Repeater count 单调不降，lessons-learned t256）。
void EntityManager::removeEntityAt(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    releaseSlot(i);
    ++m_revision;
    emit entitiesChanged();
}

// t284 Stalker 蓄力膨胀进度（0..1）：仅 mobType==MobStalker 且 fuseTimer>0（正在蓄力）时返
//   clamp(fuseTimer/kFuseTime,0,1)，供 QML delegate 据 it 对 Model 做 scale + baseColor 蓄力发白。非 Stalker /
//   未蓄力（fuseTimer<=0）/ 越界 → 0（模型静态、原配色）。
float EntityManager::inflateAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobStalker || e.fuseTimer <= 0.0f) return 0.0f;
    float p = e.fuseTimer / kFuseTime;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// t331 骸骨拉弓瞄准进度（0..1）：仅 mobType==MobBones 且 aimTimer>0（正在拉弓）时返 clamp(aimTimer/kAimWindup,0,1)，
//   供 QML delegate 据 it 驱动肩枢 Node 抬右臂 + MobBowGeometry 弦后拉。非 Bones / 未瞄准（aimTimer<=0）/ 越界 → 0
//   （模型静态、松弦）。机制等价 MC 1.0 骷髅停步拉弓瞄准。
float EntityManager::drawAmountAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobBones || e.aimTimer <= 0.0f) return 0.0f;
    float p = e.aimTimer / kAimWindup;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    return p;
}

// t239 mob 子类 id（t240 pig/cow/sheep；t242/t243 分流）。越界 → 0。
int EntityManager::mobTypeAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    return m_entities[size_t(i)].mobType;
}

// t377 第 i 个 mob 的护甲物品 id（piece 0=头盔 / 1=胸甲 / 2=护腿 / 3=靴子；0=该部位无护甲）。越界 → 0。
//   仅 Shambler/Bones spawn 时随机分配；QML delegate 据 it 叠 tier 色护甲 Model。
int EntityManager::mobArmorAt(int i, int piece) const
{
    if (i < 0 || i >= int(m_entities.size())) return 0;
    const Entity &e = m_entities[size_t(i)];
    switch (piece) {
    case 0: return e.armorHelmet;
    case 1: return e.armorChest;
    case 2: return e.armorLegs;
    case 3: return e.armorBoots;
    default: return 0;
    }
}

// t300 第 i 只 mob 是否已被剪羊毛（仅 MobSheep 用；其余 mob 永远 false）。越界 → false。
bool EntityManager::shearedAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep) return false; // 仅 sheep 有剪羊毛态
    return e.sheared;
}

// t300 剪羊毛（spec「玩家右键羊 + 持剪刀 → 羊变裸 + 掉羊毛物品」；机制等价 MC 1.0 剪羊毛）。
//   未剪羊毛的活体 sheep → 翻 sheared=true + 设 regrowCooldown（防刚剪完立即吃草长回，spec「加重新长毛冷却」）+
//   emit sheepSheared(坐标) 让呈现层 Connections 转发 ItemEntityManager.spawnItem 生成羊毛物品掉落实体
//   （同 mobDied→spawnItem 模式；单向事件流，分层：Entities 层发语义事件、呈现层只消费）。bump revision
//   → QML delegate 据 shearedAt 翻羊为裸外观。已剪羊毛 / 非 sheep / dead / 越界 → 静默早退（机制等价 MC：
//   剪羊毛只对有毛的活体羊生效，已裸的羊右键无反应）。
void EntityManager::shearSheep(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || e.mobType != MobSheep) return; // 仅 sheep 可剪
    if (e.dead || !e.alive) return;                     // 尸体 / 空槽不可剪
    if (e.sheared) return;                              // 已裸 → 无反应（不重复掉羊毛）
    e.sheared = true;
    e.regrowCooldown = kRegrowCooldown; // 剪完到能吃草方块重新长毛的硬冷却
    // 羊毛掉落在羊当前格（floor(pos)，同 mobDied 坐标约定）→ 呈现层 spawnItem 在该格中心生成掉落实体。
    const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
    qCInfo(lcEnt) << "sheep sheared at slot" << i << "pos" << e.pos << "-> dropped wool at" << dx << dy << dz;
    emit sheepSheared(dx, dy, dz);
    ++m_revision;
    emit entitiesChanged(); // bump → QML delegate 据 shearedAt 翻羊为裸外观
}

// t400 mobType 是否可繁殖被动生物（pig/cow/sheep/chicken 之一）。hostile / MobTest / MobSquid 不可繁殖。
//   feedMob 食物匹配 / 求偶寻偶 / 配对均先据它门控。机制等价 MC 1.0 仅被动 farm 动物可繁殖。
bool EntityManager::isBreedableType(int mobType)
{
    return mobType == MobPig || mobType == MobCow || mobType == MobSheep || mobType == MobChicken;
}

// t400 触发求偶期（spec「喂对应食物 → 求偶」；机制等价 MC 1.0 breeding 的 feed-to-enter-love-mode）。
//   成体可繁殖 mob + 非冷却 + 未在求偶 → 进求偶期（loveTimer=kLoveDuration）+ 返 true。
//   幼崽 / 冷却中 / 已求偶 / 非可繁殖 mob / dead / 越界 → 返 false（caller 不消耗食物）。
//   **食物匹配**由 caller（PlayerController Game 层）判：物品 id 属 RecipeRegistry（Game 层），Entities 层不向上
//   依赖（PLAN §2）。caller 先据 mobTypeAt + 持物判「食物是否匹配该物种」，匹配才调本方法。
//   bump revision + emit → QML delegate 据 inLoveAt 显心（玩家即时见求偶反馈）。同 shearSheep 修改 + emit 模式。
bool EntityManager::enterLoveMode(int i)
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive || e.dead) return false;   // 仅活体 mob 可触发
    if (!isBreedableType(e.mobType)) return false;            // 仅 pig/cow/sheep/chicken 可繁殖
    if (e.baby) return false;                                 // 幼崽未成熟，不可触发求偶
    if (e.breedCooldown > 0.0f) return false;                 // 繁殖冷却中 → 喂食无效（防刷屏；caller 保住食物）
    if (e.loveTimer > 0.0f) return false;                     // 已求偶 → 不重复触发（防一次喂多个叠加）
    e.loveTimer = kLoveDuration; // 进求偶期（tick 内衰减 + 寻偶 AI 把它拉向同类求偶者）
    qCInfo(lcEnt) << "mob slot" << i << "type" << e.mobType << "-> love mode" << kLoveDuration << "s";
    ++m_revision;
    emit entitiesChanged(); // bump → QML 据 inLoveAt 显心
    return true; // caller 据返值消耗 1 食物（生存）
}

// t400 第 i 个 mob 是否处于求偶期（loveTimer>0）。QML delegate 据它显心形 Model（繁殖可观察反馈）。
bool EntityManager::inLoveAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return false;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive) return false;
    return e.loveTimer > 0.0f;
}

// t400 第 i 个 mob 的模型缩放（幼崽 kBabyScale=0.5 / 成体 1.0）。QML delegate Node scale 绑它。
float EntityManager::babyScaleAt(int i) const
{
    if (i < 0 || i >= int(m_entities.size())) return 1.0f;
    const Entity &e = m_entities[size_t(i)];
    if (e.kind != Mob || !e.alive) return 1.0f;
    return e.baby ? kBabyScale : 1.0f;
}

// t400 当前可繁殖被动 mob 数（pig/cow/sheep/chicken 成体 + 幼崽，alive 且非 dead）。供繁殖上限判定。
int EntityManager::passiveBreedableCount() const
{
    int n = 0;
    for (const Entity &e : m_entities) {
        if (!e.alive || e.dead || e.kind != Mob) continue;
        if (isBreedableType(e.mobType)) ++n;
    }
    return n;
}

// t400 最近求偶配偶查找：返最近一只 alive && !dead && !baby && loveTimer>0 && mobType==e.mobType 的 mob 索引
//   （排除 self）；无 → -1。供求偶者设 yaw 朝配偶 → aiWander 行走相遇。O(n) 每 mob 每帧。
int EntityManager::findNearestMate(int idx) const
{
    if (idx < 0 || idx >= int(m_entities.size())) return -1;
    const Entity &self = m_entities[size_t(idx)];
    int best = -1;
    float bestDistSq = 0.0f;
    for (int j = 0; j < int(m_entities.size()); ++j) {
        if (j == idx) continue;
        const Entity &m = m_entities[size_t(j)];
        if (!m.alive || m.dead || m.kind != Mob) continue;
        if (m.baby || m.loveTimer <= 0.0f) continue;     // 仅同样在求偶的成体算配偶
        if (m.mobType != self.mobType) continue;          // 同种
        const float dx = m.pos.x() - self.pos.x();
        const float dz = m.pos.z() - self.pos.z();
        const float d2 = dx * dx + dz * dz;
        if (best < 0 || d2 < bestDistSq) { best = j; bestDistSq = d2; }
    }
    return best;
}

// t400 繁殖 tick（tick 末尾调；spec「同种 2 只喂对应食物 → 生幼崽；种群上限」；机制对齐 MC 1.0 breeding）。
//   (1) 衰减 loveTimer / breedCooldown / 幼崽 growTimer（growTimer 到 0 → baby=false 长大成体）。
//   (2) 求偶期成体配对：同种双方均求偶 + XZ 中心距 ≤ kBreedRange → 产 1 幼崽（spawnMobCore 标 baby + growTimer）
//       + 双方进 kBreedCooldown 冷却 + 退求偶（loveTimer=0）。受 kPassiveMobCap 钳制（达上限不再产）。
//   返是否变更（驱动 tick 末尾 dirty + bump revision + emit）。配对扫描 O(n²)，n≤kCap=64 可忽略。
//   幼崽生成走 acquireSlot（可能 push_back）—— 主实体循环持 Entity& 引用期间不可 push_back（致引用失效），
//   故本段在主循环之外（tick 末尾）做，且配对阶段仅记 pending、统一在末段 spawn（防引用失效 + 批量收口 emit）。
bool EntityManager::tickBreeding(qreal dt)
{
    if (m_entities.empty()) return false;
    bool dirty = false;
    // (1) 衰减所有 mob 的求偶 / 冷却 / 幼崽长大计时。
    for (Entity &e : m_entities) {
        if (!e.alive || e.kind != Mob) continue;
        if (e.loveTimer > 0.0f) {
            e.loveTimer -= float(dt);
            if (e.loveTimer <= 0.0f) { e.loveTimer = 0.0f; dirty = true; } // 退求偶 → 收心（QML 隐心）
        }
        if (e.breedCooldown > 0.0f) {
            e.breedCooldown -= float(dt);
            if (e.breedCooldown < 0.0f) e.breedCooldown = 0.0f;
        }
        if (e.baby) {
            e.growTimer -= float(dt);
            if (e.growTimer <= 0.0f) {
                e.growTimer = 0.0f;
                e.baby = false; // 长大成体（QML babyScaleAt 1.0 → 重缩回正常体型）
                dirty = true;
                qCInfo(lcEnt) << "baby grew up at pos" << e.pos << "type" << e.mobType;
            }
        }
    }
    // (2) 配对：求偶期成体同种相遇 → 产幼崽。先算「本帧还可产几只」= 上限 − 当前可繁殖数（防超 cap）。
    //   配对阶段仅 reset 父母 loveTimer / 设冷却 + 记 pending 幼崽；spawn 推迟到末段（批量 + 避引用失效）。
    struct PendingBaby { float x, y, z; int mobType; QString color; };
    std::vector<PendingBaby> pending;
    int remaining = kPassiveMobCap - passiveBreedableCount();
    const float rangeSq = kBreedRange * kBreedRange;
    const int n = int(m_entities.size());
    for (int idx = 0; idx < n && remaining > 0; ++idx) {
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive || e.dead || e.kind != Mob) continue;
        if (!isBreedableType(e.mobType) || e.baby) continue;
        if (e.loveTimer <= 0.0f || e.breedCooldown > 0.0f) continue; // 须在求偶且非冷却
        // 找最近的同种求偶配偶（j>idx 避免重复配对：idx 配 j 后 j 的 loveTimer 归 0，后续到 j 自动跳过）。
        for (int j = idx + 1; j < n; ++j) {
            Entity &m = m_entities[size_t(j)];
            if (!m.alive || m.dead || m.kind != Mob) continue;
            if (m.mobType != e.mobType || m.baby) continue;
            if (m.loveTimer <= 0.0f || m.breedCooldown > 0.0f) continue;
            const float dx = m.pos.x() - e.pos.x();
            const float dz = m.pos.z() - e.pos.z();
            const float dy = m.pos.y() - e.pos.y();
            if (dx * dx + dz * dz > rangeSq) continue;        // XZ 中心距超 KBreedRange → 未相遇
            if (std::abs(dy) > 2.0f) continue;                 // 垂直跨层不算（防跨地板盲配）
            // 配对成功：双方退求偶 + 进冷却；幼崽生在双方中点（地表上方，重力 tick 贴地）。
            e.loveTimer = 0.0f; e.breedCooldown = kBreedCooldown;
            m.loveTimer = 0.0f; m.breedCooldown = kBreedCooldown;
            const float bx = (e.pos.x() + m.pos.x()) * 0.5f;
            const float by = std::min(e.pos.y(), m.pos.y());
            const float bz = (e.pos.z() + m.pos.z()) * 0.5f;
            pending.push_back({ bx, by, bz, e.mobType, e.color });
            --remaining;
            dirty = true;
            qCInfo(lcEnt) << "breed pair: slots" << idx << "&" << j << "type" << e.mobType
                          << "-> baby pending at" << bx << by << bz
                          << "(passive count toward cap" << (kPassiveMobCap - remaining) << "/" << kPassiveMobCap << ")";
            break; // e 已配对，处理下一个 idx
        }
    }
    // 末段 spawn 幼崽（acquireSlot 可能 push_back —— 此时已脱离主循环的 Entity& 引用，安全）。
    //   一次循环 spawn 多只，tick 末尾统一 bump revision + emit 一次（批量收口，避免 N 只幼崽 N 次 notify 风暴）。
    for (const PendingBaby &b : pending) {
        // spawnMobCore 不 emit；幼崽色继承父代 color（pig/cow/sheep 走 MobModel 不读 color，占位串无妨）。
        const int slot = spawnMobCore(int(std::floor(b.x)), int(std::floor(b.y)), int(std::floor(b.z)),
                                      b.mobType, b.color, 0);
        if (slot >= 0) {
            Entity &baby = m_entities[size_t(slot)];
            baby.baby = true;            // 标幼崽（QML babyScaleAt → 0.5 缩小）
            baby.growTimer = kBabyGrowTime; // 长大倒计时
        }
    }
    return dirty;
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
//   - health≤0 且未 dead → dead=true + deathTimer=kDeathTime（冻结 AI/重力；**不**立即掉落）。
//     t449：mobDied（→ 掉落物）改在 tick 死亡态 deathTimer 归零时 emit（倒地动画播完才掉落），本处仅
//     快照 deathBurned（致死时刻 fireTimer>0）供延迟 emit 携带。dead 期间冻结 AI / 重力 / 攻击。
//   - dead / 非 Mob / 越界 / amount≤0 → 静默早退。
//   bump revision → 驱动 QML health/红闪/死亡绑定刷新（QML 据 deadAt 进入侧倒动画 + 白烟）。
void EntityManager::damageEntity(int i, int amount)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead || amount <= 0) return; // t256：空槽防御（caller 已过滤）

    e.health -= amount;
    if (e.health < 0) e.health = 0;
    e.hurtFlash = kHurtFlashTime;

    if (e.health <= 0) {
        // 死亡：冻结 AI / 重力（dead=true → tick 跳过 aiWander / 重力 / 敌对攻击，仅 deathTimer 倒计时）+
        //   给 QML 播死亡动画窗口（kDeathTime ≈ 500ms：侧倒旋转 + 白烟消散）。**mobDied 延迟到 deathTimer
        //   归零才 emit**（见 tick 死亡态分支）—— 机制等价 MC「血归零 → 倒地动画 → 掉落物」三段过渡，
        //   旧实现「红闪 + 掉落物同帧」太急（t449 修）。
        e.dead = true;
        e.deathTimer = kDeathTime;
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        // t344 burned = 致死时刻是否处于火烧态（fireTimer>0）：着火死亡掉熟肉（被动动物）。仅 fireTimer
        //   触发（日光 burning 仅敌对、不掉肉故不参与 cooked 判定）。t449 快照进 deathBurned 供延迟 emit 携带
        //   （dead 态 fireTimer 冻结，故与 expiry 复算等价；快照更稳）。
        e.deathBurned = e.fireTimer > 0.0f;
        const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
        qCInfo(lcEnt) << "mob" << i << "type" << e.mobType << "entering death at" << dx << dy << dz
                      << (e.deathBurned ? "(burned)" : "")
                      << "-> drops deferred" << kDeathTime << "s (t449 side-fall anim)";
    } else {
        qCInfo(lcEnt) << "mob" << i << "took" << amount << "dmg, health=" << e.health << "/" << e.maxHealth;
    }

    ++m_revision;
    emit entitiesChanged();
}

// t242 攻击射线 vs mob AABB 命中测试（spec「玩家左键攻击生物」前置：选体）。slab-based ray-AABB
//   对每个活体 mob 的 AABB（pos ± radius 的 1×1×1 立方）求交，取最近命中。dir 须归一（caller
//   PlayerController::lookDirection 已归一）。跳过 dead（尸体不可打，防鞭尸重复扣血 / 触发多次掉落）
//   与非 Mob（掉落物 / 下落方块不属攻击目标）。无命中 → -1。
//   分层（PLAN §2）：纯只读自身数据（pos / radius / kind / dead）的几何测试，无向下依赖。
int EntityManager::findMobHit(const QVector3D &origin, const QVector3D &dir, float maxDist, float *outDist) const
{
    int bestIdx = -1;
    float bestDist = maxDist;
    // dir 退化（零向量）→ 无方向，无命中。NaN/Inf 防御同此分支。
    if (!std::isfinite(dir.x()) || !std::isfinite(dir.y()) || !std::isfinite(dir.z())) return -1;
    const float dirLen2 = dir.x()*dir.x() + dir.y()*dir.y() + dir.z()*dir.z();
    if (dirLen2 < 1e-8f) return -1;
    for (size_t i = 0; i < m_entities.size(); ++i) {
        const Entity &e = m_entities[i];
        if (!e.alive || e.kind != Mob || e.dead) continue; // t256：跳过空槽（slot-reuse 残留位）
        // Slab 法 ray-AABB：对每轴 t1 = (min - origin) / dir、t2 = (max - origin) / dir；tmin = max(per-axis near)、
        //   tmax = min(per-axis far)；命中 ⟺ tmax >= tmin && tmax >= 0 && tmin <= maxDist。
        //   dir 分量近 0 时该轴 slab 退化为「整轴在内」约束（origin ∈ [min,max] → -inf..+inf，否则永不命中）。
        //   t252：AABB 非立方 —— X/Z 用 halfW、Y 用 halfH（cow 0.40×0.50×0.40；旧版单一 r 致 hitbox 选体
        //   与实际碰撞箱不符：打牛头 / 牛背高处按 1×1 误判命中、实际碰撞箱更高）。
        const float ext[3] = { e.halfW, e.halfH, e.halfW }; // k=0(X)/2(Z)=halfW、k=1(Y)=halfH
        float tmin = 0.0f, tmax = bestDist; // 起步用 [0, bestDist]，逐轴收紧
        bool hit = true;
        const float p[3] = { e.pos.x(), e.pos.y(), e.pos.z() };
        const float o[3] = { origin.x(), origin.y(), origin.z() };
        const float d[3] = { dir.x(), dir.y(), dir.z() };
        for (int k = 0; k < 3; ++k) {
            const float ek = ext[k];
            const float mn = p[k] - ek, mx = p[k] + ek;
            if (std::abs(d[k]) < 1e-8f) {
                // 射线平行该轴：origin 必须落在 slab 内
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
        // 起点已在 AABB 内（tmin<0）→ 取 tmax 不行（负方向出表面）；攻击视为贴脸命中 dist=0。
        const float dist = tmin >= 0.0f ? tmin : 0.0f;
        if (dist < bestDist) { bestDist = dist; bestIdx = int(i); }
    }
    if (bestIdx >= 0 && outDist) *outDist = bestDist;
    return bestIdx;
}

// t249 受击击退（spec「受击往攻击方向小跳击退」；机制等价 MC 1.0 knockback：受击实体沿攻击方向被推开 +
//   小幅上弹）。caller（PlayerController::attackMob）传玩家→mob 水平方向向量 (dirX,dirZ)；本方法归一后
//   设 vx/vz = kKnockbackHoriz×方向（水平冲量）+ vy = kKnockbackUp（小跳垂直冲量，向上）+ 解除 resting
//   （让 tick 重力分支接手上跳→减速→下落→着地；不解除则 resting 早 return 跳过垂直运动 = 不弹起）。
//   方向归一防御：零向量 / 非有限（NaN/Inf，caller 误传）→ 用实体当前 yaw 朝向兜底（-sin,-cos，同 aiWander
//   约定）避免零冲量。非 Mob（掉落物 / 下落方块）/ dead（尸体不被推，同 resolvePlayerPush）/ 越界 → 早退。
//   bump revision + emit → 驱动 QML {revision; posAt} 位置绑定重算（击退位移可见）。knockback 与 damageEntity
//   分离：扣血走 damageEntity，位移冲量走本方法，各自 bump revision（attackMob 内顺序调用，二者都生效）。
void EntityManager::knockback(int i, float dirX, float dirZ)
{
    if (i < 0 || i >= int(m_entities.size())) return;
    Entity &e = m_entities[size_t(i)];
    if (!e.alive || e.kind != Mob || e.dead) return; // t256：空槽防御（caller 已过滤）

    // 归一方向（caller 应已传合理向量，此处防御零 / 非有限）。len 非有限或近 0 → yaw 朝向兜底。
    float len = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (!(std::isfinite(len) && len > 1e-3f)) {
        dirX = -std::sin(e.yawRad);
        dirZ = -std::cos(e.yawRad);
        len = 1.0f;
    }
    dirX /= len;
    dirZ /= len;

    e.vx = dirX * kKnockbackHoriz;
    e.vz = dirZ * kKnockbackHoriz;
    e.vy = kKnockbackUp;   // 小跳垂直速度（向上为正；tick 重力分支接手）
    e.resting = false;     // 解除静止 → tick 处理上跳 + 下落 + 着地（否则 resting continue 跳过）
    ++m_revision;
    emit entitiesChanged();
    qCInfo(lcEnt) << "mob" << i << "knockback dir=(" << dirX << dirZ << ") horiz=" << kKnockbackHoriz;
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
bool EntityManager::aiWander(Entity &e, float dt, World *world, float worldW, float worldD, float speedScale)
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
    //   t298：spd = wanderSpeed × speedScale（水中 speedScale=kWaterSpeedMul<1 → 位移 + 腿摆频率同步降）。
    const float spd = e.wanderSpeed * speedScale;
    const float dx = -std::sin(e.yawRad) * spd * dt;
    const float dz = -std::cos(e.yawRad) * spd * dt;
    const float ehw = e.halfW; // XZ 半宽（边界 clamp + 圆碰撞）
    const float ehh = e.halfH; // Y 半高（footprint 格扫 Y 范围）

    // X 轴：世界边界 clamp（mob XZ 半宽 ehw → 中心不越 [ehw, world-ehw]）+ 方块碰撞撤回。
    float newX = e.pos.x() + dx;
    if (newX < ehw) newX = ehw;
    if (newX > worldW - ehw) newX = worldW - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();

    // Z 轴：用已更新的 X + 同样边界 clamp / 方块碰撞撤回（两轴顺序敏感，Z 参照可能已撤回的 newX）。
    float newZ = e.pos.z() + dz;
    if (newZ < ehw) newZ = ehw;
    if (newZ > worldD - ehw) newZ = worldD - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();

    bool moved = false;
    if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
    if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }

    e.moveSpeed = moved ? spd : 0.0f; // 撞墙 → 腿停（moveSpeed=0），t241 腿摆频率随它（t298 水中 spd 已含减速）

    // 撞墙（两轴都未动）→ 缩短 timer 下帧大概率换向离开墙角（避免一直顶墙原地不动）。
    if (!moved) e.wanderTimer = std::min(e.wanderTimer, 0.2f);

    return moved;
}

// t399 鱿鱼水生 AI（详见头文件 aiSquid 注释）。机制对齐 MC 1.0 squid：水里周期喷水推进（上浮 + 水平漂游）+
//   通用重力缓沉 → 节律性游动；离水搁浅走 aiWander 慢爬。分层（PLAN §2）：只读 World::blockAt（mobFeetInWater
//   脚位水格判，同文件静态助手）+ 自身数据；写 EntityManager 自身（pos / vy / yawRad / swimTimer）。无向上依赖。
bool EntityManager::aiSquid(Entity &e, float dt, World *world, float worldW, float worldD, float speedScale)
{
    // 离水（搁浅）：委托 aiWander 陆地慢爬（机制等价 MC squid 上岸后笨拙挪动；不复用喷水推进）。speedScale 透传
    //   （搁浅态不在水中 → speedScale=1.0 正常走速；若恰在浅水边沿 speedScale<1 亦无妨，慢爬更符合搁浅笨拙感）。
    if (!mobFeetInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH)) {
        return aiWander(e, dt, world, worldW, worldD, speedScale);
    }

    // 水中：swimTimer 倒计时到 → 喷水推进（vy 上冲量 + 随机换向漂游方向）+ 重置随机周期。
    e.swimTimer -= dt;
    if (e.swimTimer <= 0.0f) {
        e.swimTimer = kSquidSwimIntervalMin
                      + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kSquidSwimIntervalMax - kSquidSwimIntervalMin);
        // 喷水推进上冲量（正=向上）。其下通用重力段以 kWaterGravity 缓沉把它减速到 0 再反向 → 上浮→缓沉 bobbing。
        e.vy = kSquidSwimUp;
        // 随机换向（水平漂游方向）；与 player / aiWander 同 yaw 约定：dir = (-sin,0,-cos)。
        e.yawRad = float(QRandomGenerator::global()->bounded(62832)) / 10000.0f;
    }

    // 水平漂游（沿 yaw 慢速漂移；kSquidSwimSpeed 不受 speedScale 影响 —— 物种游速特征，叠加通用减速会过慢）。
    //   逐轴（X 后 Z）边界 clamp + mobAabbHitsSolid 撤回防穿墙（同 aiWander 移动模式；水中漂游遇实体方块亦撤回）。
    const float spd = kSquidSwimSpeed;
    const float dx = -std::sin(e.yawRad) * spd * dt;
    const float dz = -std::cos(e.yawRad) * spd * dt;
    const float ehw = e.halfW;
    const float ehh = e.halfH;
    float newX = e.pos.x() + dx;
    if (newX < ehw) newX = ehw;
    if (newX > worldW - ehw) newX = worldW - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
    float newZ = e.pos.z() + dz;
    if (newZ < ehw) newZ = ehw;
    if (newZ > worldD - ehw) newZ = worldD - ehw;
    if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();

    bool moved = false;
    if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
    if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
    // moveSpeed 恒取漂游速（水中持续漂移 → 触腕摆动画常驻；区别于 aiWander 的 idle/行走二态）。
    e.moveSpeed = spd;
    return moved;
}

// t281 敌对生物 AI（detect→pathfind→attack；详见头文件 aiHostile 注释）。机制对齐 MC 1.0 僵尸 / 骷髅近战 AI。
//   简化 A* = 贪心方向（直线朝玩家）+ 1 格墙越障跳；非完整 A*（每帧多 mob 跑 A* 开销过大，近战 mob 直线 + 跳够用，
//   平地 / 1 格台阶 / 树根 / 矮墙均能通过；复杂洞穴几何会卡墙，作为基类可接受，留给后续寻路增强）。
//   分层（PLAN §2）：只读 World::isSolid + 自身数据；attack 走语义信号 mobAttackedPlayer（呈现层路由 PlayerState）。
bool EntityManager::aiHostile(Entity &e, float dt, World *world, const QVector3D &playerPos,
                              float worldW, float worldD, float speedScale)
{
    // 攻击冷却递减（不论追踪与否；自然走完，复击不卡陈旧值）。钳到 0。
    if (e.attackCooldown > 0.0f) {
        e.attackCooldown -= dt;
        if (e.attackCooldown < 0.0f) e.attackCooldown = 0.0f;
    }

    // 玩家相对位置（XZ 距离 + 垂直差）。playerPos = 玩家脚位；e.pos = mob 中心。
    const float dx = playerPos.x() - e.pos.x();
    const float dz = playerPos.z() - e.pos.z();
    const float dy = playerPos.y() - e.pos.y();
    const float distXZ = std::sqrt(dx * dx + dz * dz);

    // (1) detect：XZ 距离 <= kDetectRange → 进入 / 刷新追踪（chaseTimer 重置记忆期）。脱离则记忆期内续追，过期放弃。
    if (distXZ <= kDetectRange) {
        e.chasing = true;
        e.chaseTimer = kChaseMemory;
    } else if (e.chasing) {
        e.chaseTimer -= dt;
        if (e.chaseTimer <= 0.0f) { e.chaseTimer = 0.0f; e.chasing = false; }
    }

    // (2) 非追踪 → 回退到 wander（随机游荡，同 passive；mobType 非 sheep 故不吃草分支，纯游荡）。
    if (!e.chasing) {
        return aiWander(e, dt, world, worldW, worldD, speedScale); // t298 透传水中减速
    }

    // (3) 追踪：朝玩家走 + 越障跳。yaw 朝玩家（与 aiWander / player 同 yaw 约定：dir = (-sin,0,-cos)，
    //   使 QML eulerRotation.y=yawDeg 模型 -Z 正对玩家）→ yawRad = atan2(-dx, -dz)。
    if (distXZ > 1e-4f) {
        e.yawRad = std::atan2(-dx, -dz);
    }
    e.wanderSpeed = kChaseSpeed; // 供 walkPhase 动画频率 + 语义（行走态；raw 值，水中减速不写入此字段避免下游二次缩放）
    // t298 水中减速：chaseSpd = kChaseSpeed × speedScale（位移 + moveSpeed 用 it；wanderSpeed 保 raw）。
    const float chaseSpd = kChaseSpeed * speedScale;

    // 越障跳：resting（贴地）+ 前方脚位格是 1 格墙（实体）+ 墙顶两格空气（可落 + 头可容，mob ~1.8 高）→ 跳。
    //   不跳的情况：前方无墙（平地直走）/ 墙 ≥2 格（跳不过，正确不跳避免原地蹦）/ 已在空中（resting=false 跳过）。
    //   fdx/fdz = 朝向单位向量；前方格取脚位 +0.6 格偏移（mob 半宽 0.45 + 余量，确保落在墙格而非自身列）。
    if (e.resting && world) {
        const float fdx = -std::sin(e.yawRad);
        const float fdz = -std::cos(e.yawRad);
        const int fy = qFloor(e.pos.y() - e.halfH);          // 脚位格（mob 底面所在格）
        const int fx = qFloor(e.pos.x() + fdx * 0.6f);
        const int fz = qFloor(e.pos.z() + fdz * 0.6f);
        if (fy >= 0
            && world->isSolid(fx, fy, fz)                    // 前方脚位是墙（1 格障碍）
            && !world->isSolid(fx, fy + 1, fz)                // 墙顶可落（mob 翻上去后脚位）
            && !world->isSolid(fx, fy + 2, fz)) {             // 头位可容（mob ~1.8 高，再上方须空气）
            e.vy = kJumpSpeed;
            e.resting = false; // 解除静止 → 本 tick 后段重力分支处理上跳（vy 正）→ 减速 → 下落 → 着地
        }
    }

    // 水平移动（朝玩家，逐轴 AABB 碰撞撤回；复用 aiWander 的边界 clamp + mobAabbHitsSolid 全格扫模式 → 贴墙滑动不穿入）。
    bool moved = false;
    if (distXZ > 1e-4f) {
        const float ehw = e.halfW; // t252 XZ 半宽（边界 clamp + 碰撞）
        const float ehh = e.halfH; // t252 Y 半高（footprint 格扫）
        const float nx = dx / distXZ;
        const float nz = dz / distXZ;
        // X 轴：世界边界 clamp + 方块碰撞撤回。
        float newX = e.pos.x() + nx * chaseSpd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        // Z 轴：参照可能已撤回的 newX（两轴顺序敏感）。
        float newZ = e.pos.z() + nz * chaseSpd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
    }
    e.moveSpeed = moved ? chaseSpd : 0.0f; // 撞墙 → 腿停（moveSpeed=0），但仍在追踪，下帧重试 / 已跳（t298 含水中减速）

    // (4) attack：XZ <= kAttackRange + 垂直同层（|dy|<=kAttackVertRange）+ 单 mob 冷却到 + t321 全局节流到 →
    //   emit mobAttackedPlayer + 重置两者。垂直门控防跨层隔空打（玩家在 mob 头顶 / 脚下不命中）。emit 走语义信号，
    //   呈现层据 Survival 门控应用伤害。t321 节流门控防多 mob 围攻同帧齐抽（详见 kPlayerHitThrottle 注释）——
    //   m_playerHitCooldown>0（其它 mob 刚命中过）→ 本次 attack 不触发（mob 视觉仍挥击但无伤害），等节流过。
    //   t296 击退方向 = (玩家 − mob) XZ 归一（把玩家推开 mob）；distXZ 极小（贴脸重合）→ 朝 mob 朝向兜底（yaw 约定
    //     dir=(-sin,-cos)），防零向量。dx/dz/distXZ 已在 (1) 前算好。
    if (distXZ <= kAttackRange && std::abs(dy) <= kAttackVertRange
        && e.attackCooldown <= 0.0f && m_playerHitCooldown <= 0.0f) {
        e.attackCooldown = kAttackCooldown;
        m_playerHitCooldown = kPlayerHitThrottle; // t321 串行化玩家受击（围攻 mob 轮替出手）
        float kbX, kbZ;
        if (distXZ > 1e-3f) { kbX = dx / distXZ; kbZ = dz / distXZ; }
        else { kbX = -std::sin(e.yawRad); kbZ = -std::cos(e.yawRad); } // 兜底：朝 mob 面朝方向（= 推开）
        emit mobAttackedPlayer(kAttackDamage, e.mobType, kbX, kbZ);
        qCInfo(lcEnt) << "hostile mob" << e.mobType << "attacked player for" << kAttackDamage << "HP";
    }

    return moved;
}

// t283 骷髅弓箭手 AI（detect→keep-distance→shoot；详见头文件 aiArcher 注释）。机制对齐 MC 1.0 骷髅射手。
//   分层（PLAN §2）：只读 World::isSolid + 自身数据；shoot 走 spawnArrow（箭实体），命中由 Arrow tick 分支
//   发 mobAttackedPlayer 语义信号让呈现层路由 PlayerState（同 aiHostile attack 模式）。
bool EntityManager::aiArcher(Entity &e, float dt, World *world, const QVector3D &playerPos,
                             float worldW, float worldD, float speedScale)
{
    // 攻击（射箭）冷却递减（不论追踪与否；自然走完，复射不卡陈旧值）。钳到 0。
    if (e.attackCooldown > 0.0f) {
        e.attackCooldown -= dt;
        if (e.attackCooldown < 0.0f) e.attackCooldown = 0.0f;
    }

    const float dx = playerPos.x() - e.pos.x();
    const float dz = playerPos.z() - e.pos.z();
    const float dy = playerPos.y() - e.pos.y();
    const float distXZ = std::sqrt(dx * dx + dz * dz);

    // (1) detect + chase memory（同 aiHostile）：进入 kDetectRange → 追踪 + 刷新记忆；脱离后记忆期内续追。
    if (distXZ <= kDetectRange) {
        e.chasing = true;
        e.chaseTimer = kChaseMemory;
    } else if (e.chasing) {
        e.chaseTimer -= dt;
        if (e.chaseTimer <= 0.0f) { e.chaseTimer = 0.0f; e.chasing = false; }
    }
    if (!e.chasing) {
        // 非追踪 → 回退 wander（随机游荡；mobType 非 sheep 不吃草，纯游荡）。
        return aiWander(e, dt, world, worldW, worldD, speedScale); // t298 透传水中减速
    }

    // (2) 朝向玩家（射箭方向 + 行走方向；dir=(-sin,0,-cos) 同 player/aiHostile yaw 约定）。
    if (distXZ > 1e-4f) e.yawRad = std::atan2(-dx, -dz);
    e.wanderSpeed = kChaseSpeed; // 供 walkPhase 腿摆频率 + 语义（行走态；raw 值，水中减速不写入避免二次缩放）
    // t298 水中减速：chaseSpd = kChaseSpeed × speedScale（位移 + moveSpeed 用 it）。
    // t331 拉弓减速：aimTimer>0（正拉弓瞄准）→ draw=aimTimer/kAimWindup ∈[0,1]，位移再 ×(1−draw) → 拉弓期渐减速
    //   到停（SLOWS + pauses to aim；满拉 draw=1 → chaseSpd=0 停步射）。draw 亦暴露给 QML 抬臂/弦后拉（drawAmountAt）。
    float draw = e.aimTimer > 0.0f ? e.aimTimer / kAimWindup : 0.0f;
    if (draw > 1.0f) draw = 1.0f;
    const float chaseSpd = kChaseSpeed * speedScale * (1.0f - draw);

    // (3) 保持距离：近于 kArcherKeepMin → 朝远离走；远于 kArcherKeepMax → 朝玩家走；其间 → 原地（仅朝向）。
    //   moveDirX/Z = 水平移动单位向量（朝向「期望远离 / 接近」方向）。wantMove=false 表示在保持带内 → 不水平位移。
    float moveDirX = 0.0f, moveDirZ = 0.0f;
    bool wantMove = false;
    if (distXZ > 1e-4f) {
        if (distXZ < kArcherKeepMin) {
            // 太近：朝远离玩家方向（moveDir = (e - player) / dist = (-dx,-dz)/dist）。
            moveDirX = -dx / distXZ;
            moveDirZ = -dz / distXZ;
            wantMove = true;
        } else if (distXZ > kArcherKeepMax) {
            // 太远：朝玩家方向。
            moveDirX = dx / distXZ;
            moveDirZ = dz / distXZ;
            wantMove = true;
        }
    }

    // 越障跳（仅在要水平移动 + 贴地时；同 aiHostile 越障：前方 1 格墙 + 墙顶 2 格空气 → 跳）。
    //   前方格取 moveDir 方向 0.7 格偏移（mob 半宽 0.45 + 余量，落在前方格而非自身列）。
    if (wantMove && e.resting && world) {
        const int fy = qFloor(e.pos.y() - e.halfH);                 // 脚位格
        const int fx = qFloor(e.pos.x() + moveDirX * 0.7f);
        const int fz = qFloor(e.pos.z() + moveDirZ * 0.7f);
        if (fy >= 0
            && world->isSolid(fx, fy, fz)                            // 前方脚位是 1 格墙
            && !world->isSolid(fx, fy + 1, fz)                       // 墙顶可落
            && !world->isSolid(fx, fy + 2, fz)) {                    // 头位可容（mob ~1.8 高）
            e.vy = kJumpSpeed;
            e.resting = false;
        }
    }

    // (4) 水平移动（朝 moveDir，逐轴 AABB 碰撞撤回；复用 aiHostile / aiWander 边界 clamp + 全格扫模式）。
    bool moved = false;
    if (wantMove) {
        const float ehw = e.halfW;
        const float ehh = e.halfH;
        float newX = e.pos.x() + moveDirX * chaseSpd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + moveDirZ * chaseSpd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
    }
    e.moveSpeed = moved ? chaseSpd : 0.0f; // 撞墙 / 保持带内 → 腿停（walkPhase 冻结；t298 含水中减速）

    // (5) shoot：射程内 + 垂直同层 + 视线清 + 冷却到 → t331 先累加 aimTimer（拉弓瞄准），满 kAimWindup 才射 +
    //   重置冷却（防每帧连发）。脱射程 / 视线断 / 冷却中 → 清 aimTimer（中止拉弓，下帧 draw 归 0 → 全速）。
    if (distXZ <= kArcherShootRange && std::abs(dy) <= kShootVertRange && e.attackCooldown <= 0.0f) {
        const QVector3D origin(e.pos.x(), e.pos.y() + e.halfH * 0.5f, e.pos.z());
        const QVector3D target(playerPos.x(), playerPos.y() + 0.9f, playerPos.z()); // 玩家上身（眼位 ~ 脚+1.62）
        if (lineOfSightClear(world, origin, target)) {
            e.aimTimer += float(dt);
            if (e.aimTimer >= kAimWindup) {
                fireArrow(e, target);
                e.attackCooldown = kShootCooldown;
                e.aimTimer = 0.0f;
                qCInfo(lcEnt) << "archer (Bones) fired arrow; dist=" << distXZ;
            }
        } else {
            e.aimTimer = 0.0f; // 视线断 → 中止拉弓
        }
    } else {
        e.aimTimer = 0.0f; // 脱射程 / 垂直跨层 / 冷却中 → 不拉弓
    }

    return moved;
}

// t283 视线清查（aiArcher shoot 前调，防穿墙盲射）：从 from 到 to 沿连线 0.5 格步进采样，任一采样点所在格
//   isSolid → 视线被挡返 false。0.5 格步进足以抓 1 格墙。分层：只读 World::isSolid，不向下加依赖。
bool EntityManager::lineOfSightClear(World *world, const QVector3D &from, const QVector3D &to) const
{
    if (!world) return false; // 无世界可查 → 保守不射（caller 安全）
    const QVector3D d = to - from;
    const float len = d.length();
    if (len < 0.5f) return true;                                  // 起终点同格 → 视线清
    const float step = 0.5f;
    for (float t = step; t < len; t += step) {
        const QVector3D p = from + d * (t / len);
        const int bx = qFloor(p.x()), by = qFloor(p.y()), bz = qFloor(p.z());
        if (by >= 0 && world->isSolid(bx, by, bz)) return false;  // 中途被实体方块挡 → 视线不清
    }
    return true;
}

// t283 朝 target 解抛物初速并射箭（aiArcher shoot 段调；详见头文件 fireArrow 注释）。
void EntityManager::fireArrow(const Entity &shooter, const QVector3D &target)
{
    // origin = shooter 中心高度 + 朝 target 前移 0.5 格（避免贴墙时箭 spawn 入墙即被 tick 判方块命中）。
    const float dx0 = target.x() - shooter.pos.x();
    const float dz0 = target.z() - shooter.pos.z();
    const float horiz0 = std::sqrt(dx0 * dx0 + dz0 * dz0);
    if (horiz0 < 0.01f) return; // 退化（同格）→ 安全早退（防除零）
    QVector3D origin(shooter.pos.x() + dx0 / horiz0 * 0.5f,
                     shooter.pos.y() + shooter.halfH * 0.5f,
                     shooter.pos.z() + dz0 / horiz0 * 0.5f);
    const float dx = target.x() - origin.x();
    const float dy = target.y() - origin.y();
    const float dz = target.z() - origin.z();
    const float horiz = std::sqrt(dx * dx + dz * dz);
    if (horiz < 0.01f) return;
    const float vH = kArrowSpeed;
    const float t = horiz / vH;                                  // 飞行时间（水平距 / 水平速度）
    // 抛物解：dy = vy·t − 0.5·g·t² → vy = (dy + 0.5·g·t²)/t（命中 target 高度的初速）。
    float vy = (dy + 0.5f * kGravity * t * t) / t;
    if (vy > kArrowMaxVert) vy = kArrowMaxVert;                  // 钳极端弧
    if (vy < -kArrowMaxVert) vy = -kArrowMaxVert;
    float vx = (dx / horiz) * vH;
    float vz = (dz / horiz) * vH;
    // MC 骷髅非 100% 精准 → 三轴 ±kArrowSpread 随机抖动（spread ≪ vH 不改飞行时间量级）。
    auto rnd = []() {
        return (float(QRandomGenerator::global()->bounded(2001)) - 1000.0f) / 1000.0f; // [-1,1]
    };
    vx += rnd() * kArrowSpread;
    vz += rnd() * kArrowSpread;
    vy += rnd() * kArrowSpread;
    spawnArrow(origin, QVector3D(vx, vy, vz));
}

// t284 Stalker（潜行者；机制等价 MC 1.0 苦力怕）AI（detect→chase→fuse→detonate；详见头文件 aiStalker 注释）。
//   机制对齐 MC 苦力怕：缓慢逼近 → 近距蓄力（站立膨胀）→ 引爆；玩家逃远熄火。
//   分层（PLAN §2）：只读 World::isSolid/blockAt + 自身数据；爆炸破坏方块走 setWaterSilent、伤害玩家走
//   mobAttackedPlayer 语义信号（呈现层路由 PlayerState），同 aiHostile attack / aiArcher shoot 模式。
bool EntityManager::aiStalker(Entity &e, float dt, World *world, const QVector3D &playerPos,
                              float worldW, float worldD, float speedScale)
{
    const float dx = playerPos.x() - e.pos.x();
    const float dz = playerPos.z() - e.pos.z();
    const float distXZ = std::sqrt(dx * dx + dz * dz);

    // (1) detect + chase memory（同 aiHostile / aiArcher）：进入 kDetectRange → 追踪 + 刷新记忆；脱离后记忆期内续追。
    if (distXZ <= kDetectRange) {
        e.chasing = true;
        e.chaseTimer = kChaseMemory;
    } else if (e.chasing) {
        e.chaseTimer -= dt;
        if (e.chaseTimer <= 0.0f) { e.chaseTimer = 0.0f; e.chasing = false; }
    }

    // (4) fuse：追踪态下据距离蓄力 / 熄火。蓄力中（fuseTimer>0）→ 站立不动（机制等价 MC 苦力怕近距嘶嘶蓄力停步）。
    //   distXZ<=kFuseRange → 累加 fuseTimer（蓄力）；离开蓄力区即泄压（defuse）：中距（kFuseRange<dist<=kDefuseRange）
    //   渐退回常态（非累积，反复进出不强制引爆）；逃远（>kDefuseRange）即归零。非追踪态 → 强制熄火（防脱离后仍蓄力）。
    if (e.chasing) {
        if (distXZ <= kFuseRange) {
            e.fuseTimer += float(dt);
        } else if (distXZ > kDefuseRange) {
            e.fuseTimer = 0.0f;
        } else {
            e.fuseTimer -= float(dt);
            if (e.fuseTimer < 0.0f) e.fuseTimer = 0.0f;
        }
    } else {
        e.fuseTimer = 0.0f;
    }

    // 朝向玩家（追踪时；射箭方向 + 行走方向。dir=(-sin,0,-cos) 同 player/aiHostile yaw 约定）。
    if (e.chasing && distXZ > 1e-4f) e.yawRad = std::atan2(-dx, -dz);

    // (5) detonate：蓄力满 → 引爆（破坏方块 + 伤害玩家 + emit explosion）+ 标 exploded。caller（tick Mob 分支）
    //   据 exploded 当帧 releaseSlot 移除。detonate 后不再移动 → 直接返 false（moved=false）。
    if (e.fuseTimer >= kFuseTime) {
        detonateStalker(e, world, playerPos);
        e.moveSpeed = 0.0f;
        e.wanderSpeed = 0.0f;
        return false;
    }

    // 蓄力中：站立不动（腿停），但仍朝玩家（yaw 已更新）。moveSpeed=0 → walkPhase 冻结。
    if (e.fuseTimer > 0.0f) {
        e.wanderSpeed = 0.0f;
        e.moveSpeed = 0.0f;
        return false;
    }

    // 非追踪 → 回退 wander（随机游荡；mobType 非 sheep 不吃草，纯游荡）。
    if (!e.chasing) {
        return aiWander(e, dt, world, worldW, worldD, speedScale); // t298 透传水中减速
    }

    // (3) 追踪但未蓄力：缓慢朝玩家走（kStalkerChaseSpeed）+ 越障跳（同 aiHostile）。
    e.wanderSpeed = kStalkerChaseSpeed; // 供 walkPhase 腿摆频率 + 语义（行走态；raw 值，水中减速不写入避免二次缩放）
    // t298 水中减速：stalkerSpd = kStalkerChaseSpeed × speedScale（位移 + moveSpeed 用 it）。
    const float stalkerSpd = kStalkerChaseSpeed * speedScale;
    if (e.resting && world) {
        const float fdx = -std::sin(e.yawRad);
        const float fdz = -std::cos(e.yawRad);
        const int fy = qFloor(e.pos.y() - e.halfH);
        const int fx = qFloor(e.pos.x() + fdx * 0.6f);
        const int fz = qFloor(e.pos.z() + fdz * 0.6f);
        if (fy >= 0
            && world->isSolid(fx, fy, fz)
            && !world->isSolid(fx, fy + 1, fz)
            && !world->isSolid(fx, fy + 2, fz)) {
            e.vy = kJumpSpeed;
            e.resting = false;
        }
    }
    bool moved = false;
    if (distXZ > 1e-4f) {
        const float ehw = e.halfW;
        const float ehh = e.halfH;
        const float nx = dx / distXZ;
        const float nz = dz / distXZ;
        float newX = e.pos.x() + nx * stalkerSpd * dt;
        if (newX < ehw) newX = ehw;
        if (newX > worldW - ehw) newX = worldW - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
        float newZ = e.pos.z() + nz * stalkerSpd * dt;
        if (newZ < ehw) newZ = ehw;
        if (newZ > worldD - ehw) newZ = worldD - ehw;
        if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
        if (newX != e.pos.x()) { e.pos.setX(newX); moved = true; }
        if (newZ != e.pos.z()) { e.pos.setZ(newZ); moved = true; }
    }
    e.moveSpeed = moved ? stalkerSpd : 0.0f; // t298 含水中减速
    return moved;
}

// t284 Stalker 爆炸（aiStalker fuse 满时调；详见头文件 detonateStalker 注释）。机制等价 MC 苦力怕球形爆炸。
//   分层（PLAN §2）：向下写 World（setWaterSilent 破坏方块 + worldChanged 重建 mesh）+ 发语义信号
//   （explosion 音/视反馈、mobAttackedPlayer 伤害玩家）；只读 World::blockAt 判定破坏目标。无向上依赖。
void EntityManager::detonateStalker(Entity &e, World *world, const QVector3D &playerPos)
{
    const float ex = e.pos.x();
    const float ey = e.pos.y();
    const float ez = e.pos.z();
    const int cx0 = qFloor(ex);
    const int cy0 = qFloor(ey);
    const int cz0 = qFloor(ez);

    // (a) 球形破坏方块：以爆炸中心格为原点、ceil(kExplosionRadius) 为半径的立方盒内逐格，距中心 <= 半径才破坏。
    //   跳过 Air（无操作）/ Bedrock（不可破坏）/ Water（不抽干，机制等价 MC 爆炸不毁水体）。走 setWaterSilent
    //   （直写 + worldChanged 重建 mesh，**不**发 blockBroken → 免球形内每块破块粒子 / 音 spam —— 爆炸的音 / 视
    //   反馈由下方 explosion 信号单一入口驱动）。每块 O(1)；半径 3 → 7³=343 格 worst case，可接受（一次性事件）。
    //   t297 水中不破坏方块：水吸收爆炸（机制等价 MC 爆炸射线遇液体即灭 → 不毁地形），整段球形破坏跳过。
    //     玩家伤害 / 音 / 视反馈照发（水中爆炸仍伤玩家、仍有爆炸声 / 迸发，仅地形无损，机制等价 MC）。
    //   t319 修 t297 漏判（水中仍毁地形）：e.pos.y 是 mob 身体【中心】（halfH=0.9 → 身体 1.8 高），t297
    //     只查 blockAt(cx0, cy0=floor(pos.y), cz0) 中心格 —— Stalker 在水中受浮力上浮（头出水面）时中心格
    //     常落在水面之上的空气格 → originInWater=false → 球形破坏照跑（bug）。改【沿身体 Y 列】逐格查水
    //     （feet=floor(pos.y−halfH) .. head=floor(pos.y+halfH−ε)，XZ 取中心格）—— 复用同文件 mobFeetInWater /
    //     tick 水中浮力判定（mobInWater）的同一脚位语义；任一身体格 == Water 即视为水中爆炸。陆地（身体列
    //     全程无水）originInWater=false → 照常破坏，行为不变。
    bool originInWater = false;
    if (world) {
        const int fy = qFloor(ey - e.halfH);             // 脚位格（AABB 底面所在格；同 mobFeetInWater）
        const int hy = qFloor(ey + e.halfH - 1e-3f);     // 头位格（ε 防 AABB 顶恰整数误取上方空气格）
        for (int by = fy; by <= hy && !originInWater; ++by) {
            if (by < 0) continue;                         // Y<0 越界 blockAt 返 Air，跳过免无谓查（同 mobFeetInWater 早返）
            if (world->blockAt(cx0, by, cz0) == BlockRegistry::Water) originInWater = true;
        }
    }
    if (world && !originInWater) {
        // t320 批量破坏：球内所有破坏块走 World::destroySphereSilent 一次收口（N 写 + 1 次 refloodBox +
        //   1 次 emit worldChanged + 1 次 clearAllDirty），替代旧逐块 setWaterSilent 的「重建风暴」
        //   （每块 1× emit + 1× recomputeLightAround → 数十次 mesh 重建请求 / 数十次光场重 flood → 一帧数百 ms）。
        //   返回被破坏块（坐标 + 原 id），caller 据原 id 派生掉落物。
        const auto destroyed = world->destroySphereSilent(cx0, cy0, cz0, kExplosionRadius);
        // t297 爆炸掉落（~50% / 破坏块）：取 BlockRegistry::dropId（Stone→Cobble 等，同玩家挖掘掉落，非原方块 id）
        //   → 概率门控（kExplosionDropChance）→ emit explosionDroppedItem → 呈现层转发 ItemEntityManager.spawnItem
        //   在该格生成掉落实体（机制等价 MC 爆炸把被毁方块弹成物品）。dropId<=0（如 leaves 默认 0 / 矿石须冶炼类）
        //   → 不掉。用 QRandomGenerator（玩家交互掉落的随机性，非 worldgen 确定性范畴 §2-K）。
        for (const World::DestroyedVoxel &d : destroyed) {
            const int dropItemId = BlockRegistry::dropId(d.oldId);
            if (dropItemId > 0 && QRandomGenerator::global()->generateDouble() < kExplosionDropChance)
                emit explosionDroppedItem(d.x, d.y, d.z, dropItemId);
        }
    }

    // (b) 距离衰减伤害玩家：以玩家身体中心（脚位 + ~0.9，机制等价 MC 玩家受击采样身体中部）到爆炸中心计 3D 距离；
    //   半径内 → dmg = round(kExplosionDamageMax·(1 − dist/radius))，至少 1（贴脸必死、远距可存活，机制等价 MC
    //   苦力怕爆炸伤害随距离衰减）。半径外 → 0 不发。emit mobAttackedPlayer → 呈现层仅 Survival 应用（Creative /
    //   Spectator 无伤跳过，机制等价 MC 创造 / 观察者无敌）。
    int dmg = 0;
    if (kExplosionRadius > 0.0f) {
        const float pdx = playerPos.x() - ex;
        const float pdy = (playerPos.y() + 0.9f) - ey;
        const float pdz = playerPos.z() - ez;
        const float pd = std::sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
        if (pd <= kExplosionRadius) {
            dmg = int(std::round(float(kExplosionDamageMax) * (1.0f - pd / kExplosionRadius)));
            if (dmg < 1) dmg = 1; // 半径内 → 至少 1HP（机制等价 MC 爆炸半径内必有伤害）
        }
    }
    // t296 爆炸击退方向 = (玩家 − 爆炸中心) XZ 归一（把玩家炸离 Stalker；机制等价 MC 苦力怕爆炸把玩家推飞）。
    //   用玩家脚位 − 爆炸中心水平向量；退化（玩家恰在爆炸中心正上 / 下）→ 朝 +X 兜底。
    if (dmg > 0) {
        float kbX = 0.0f, kbZ = 0.0f;
        const float dxh = playerPos.x() - ex;
        const float dzh = playerPos.z() - ez;
        const float phlen = std::sqrt(dxh * dxh + dzh * dzh);
        if (phlen > 1e-3f) { kbX = dxh / phlen; kbZ = dzh / phlen; }
        else { kbX = 1.0f; } // 兜底：水平重合 → 朝 +X 推（任意非零向）
        emit mobAttackedPlayer(dmg, int(MobStalker), kbX, kbZ);
    }

    // (c) 爆炸音 / 视反馈（单一入口）：emit explosion（呈现层 Connections → AudioManager.playExplosion +
    //   BlockParticles.burstExplosion）。坐标 = 爆炸中心格（粒子在中心迸发；机制等价 MC 爆炸声/光在爆炸点）。
    emit explosion(cx0, cy0, cz0);

    // (d) 标记本实体本帧已引爆 → tick Mob 分支据此当帧 releaseSlot 移除（爆炸即除，不再模拟）。
    e.exploded = true;
    qCInfo(lcEnt) << "Stalker detonated at" << cx0 << cy0 << cz0 << "player dmg" << dmg
                  << (originInWater ? "(in water: no terrain damage)" : "");
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
    const int bodyY = qFloor(e.pos.y() - e.halfH); // t252: e.radius → e.halfH（底面 y）
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

// 玩家推动解析：对每个 pushable 实体做「玩家 AABB（XZ 矩形）vs 实体圆（XZ，半径=entity.halfW）」
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
        if (!e.alive || !e.pushable || e.dead) continue; // t256 空槽 + 掉落物等非推动 + t239 dead mob 跳过

        const float ehw = e.halfW; // 实体 XZ 半宽（圆碰撞半径）
        const float ehh = e.halfH; // 实体 Y 半高（垂直区间）
        // 垂直区间重叠判定（实体 AABB：[pos.y−ehh, pos.y+ehh] vs 玩家 [feet.y, feet.y+height]）。
        //   t252：Y 用 halfH（非旧版共用 radius；cow halfH=0.70 → 推动判定区对齐实际碰撞箱高度）。
        if (e.pos.y() + ehh <= pminY || e.pos.y() - ehh >= pmaxY) continue;

        // XZ 平面 AABB-vs-Circle 穿透求解。
        const float cx = std::clamp(e.pos.x(), px - halfW, px + halfW);
        const float cz = std::clamp(e.pos.z(), pz - halfW, pz + halfW);
        const float dx = e.pos.x() - cx;
        const float dz = e.pos.z() - cz;
        const float dist2 = dx * dx + dz * dz;
        if (dist2 >= ehw * ehw) continue; // 无 XZ 穿透

        float newX = e.pos.x();
        float newZ = e.pos.z();
        const float dist = std::sqrt(dist2);
        if (dist > 1e-5f) {
            const float push = ehw - dist;
            newX = e.pos.x() + dx / dist * push;
            newZ = e.pos.z() + dz / dist * push;
        } else {
            // 实体中心在玩家 AABB 内：沿最近面推出（min 四向距离 → 最短穿透方向）。
            const float toMinX = e.pos.x() - (px - halfW);
            const float toMaxX = (px + halfW) - e.pos.x();
            const float toMinZ = e.pos.z() - (pz - halfW);
            const float toMaxZ = (pz + halfW) - e.pos.z();
            const float m = std::min({toMinX, toMaxX, toMinZ, toMaxZ});
            if (m == toMinX)      newX = px - halfW - ehw;
            else if (m == toMaxX) newX = px + halfW + ehw;
            else if (m == toMinZ) newZ = pz - halfW - ehw;
            else                  newZ = pz + halfW + ehw;
        }

        // 世界碰撞钳制（t104：mob AABB footprint 全格扫，仿 player aabbHitsSolid）：扫 mob 立方体 AABB
        // 覆盖的所有格子（非旧版「只查中心格」），任一实体方块 → 撤回该轴推动（防穿墙）。X/Z 两轴独立
        // 判定，Z 轴参照可能已撤回的 newX（两轴独立但顺序敏感）。旧版单格检查在斜推角落时 mob 中心可能
        // 仍在空气 → 不撤回 → 下帧中心入墙才撤回 → 反复跳变 = jitter；全格扫使任一部分触墙即撤回 → 消除。
        if (world) {
            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh))     newZ = e.pos.z();
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
                const int supportY = qFloor(e.pos.y() - ehh) - 1; // 实体底面下方一格（与 tick 复探同公式）
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
//        扫实体所在列首个实体方块 → 命中则贴其顶面（solidCellY+1+halfH）停下、vy=0、resting=true。
//        越界（cy<0）查 isSolid 返 false → 不误判虚空地面，实体继续落。
//     3) pos / resting 任一真变 → dirty，末尾统一 bump revision + emit（驱动 QML {revision; posAt} 绑定重算）。
//   void-loss 兜底：Mob 跌出世界底部（pos.y<0，如被推出边界外无支撑）→ 标记移除（防永久下落）。
//
// 移除用索引收集 + 循环后逆序 erase（保索引有效）。
void EntityManager::tick(qreal dt, World *world, const QVector3D &listener,
                        float listenerHalfW, float listenerHeight, bool playerTargetable)
{
    if (!world || m_entities.empty()) return;
    const float worldW = float(world->width());
    const float worldD = float(world->depth());
    bool dirty = false;
    std::vector<int> toRemove; // FallingBlock 着地 / 跌出 + t239 mob deathTimer 到 / void-loss 索引（逆序 erase）

    // t321 玩家受击全局节流倒计时（每帧扣一次，非每实体；防多 mob 围攻秒杀，详见 kPlayerHitThrottle 注释）。
    if (m_playerHitCooldown > 0.0f) {
        m_playerHitCooldown -= float(dt);
        if (m_playerHitCooldown < 0.0f) m_playerHitCooldown = 0.0f;
    }

    for (int idx = 0; idx < int(m_entities.size()); ++idx) {
        Entity &e = m_entities[size_t(idx)];
        if (!e.alive) continue; // t256：跳过已释放的空槽（slot-reuse 残留位；不参与物理 / AI）

        // --- Arrow（t283 骷髅弓箭手箭矢）：抛物 + 方块命中 / 玩家命中 / 寿命 / 越界 → 移除（不走 Mob AI / resting）---
        if (e.kind == Arrow) {
            // t323 嵌入态（命中方块后冻结物理）：仅 despawn 倒计时；玩家箭的近距拾取由
            //   PlayerController::arrowPickupScan 处理（嵌入箭仍渲染：kind=Arrow + pos 钉面 + vel 定向不变）。
            if (e.arrowStuck) {
                e.arrowLife -= float(dt);
                if (e.arrowLife <= 0.0f) { toRemove.push_back(idx); dirty = true; } // ~60s despawn
                continue;
            }
            e.arrowLife -= float(dt);
            // 抛物：重力改 vy（与世界重力同值 → 弧自然）。不复用 Mob 终端下落钳（箭可高速上扬）。
            e.vy -= kGravity * float(dt);
            const QVector3D next = e.pos + QVector3D(e.vx, e.vy, e.vz) * float(dt);
            bool remove = false;
            bool hitPlayer = false;
            int hitPlayerDmg = 0; // t324 命中玩家造成的伤害（骷髅箭=kArrowDamage / 自身箭=arrowDamage；日志用）

            // 寿命到 → 移除（飞行未命中兜底，防永久滞留堆积）。
            if (e.arrowLife <= 0.0f) remove = true;

            // 方块命中（t323 嵌入而非移除）：新位置所在格 isSolid → 箭钉入射面（半嵌可见、定向飞行方向），
            //   arrowStuck=true 冻结物理 + arrowLife 重置 kStuckArrowLifetime（~60s despawn）。vx/vy/vz 保留供
            //   arrowYawAt/arrowPitchAt 定向（嵌入箭仍朝命中飞行方向）。火把 / 半砖等非 solid → 穿透（机制可接受）。
            //   玩家箭嵌入后可拾（PlayerController::arrowPickupScan）；骷髅箭嵌入不拾（防刷，spec）。
            if (!remove) {
                const int bx = qFloor(next.x()), by = qFloor(next.y()), bz = qFloor(next.z());
                if (by >= 0 && world->isSolid(bx, by, bz)) {
                    // 入射方向（归一；速度 ~0 退化 → 向下兜底）。沿 dir 把箭尖压入面、杆尾露面外（半嵌）。
                    QVector3D v(e.vx, e.vy, e.vz);
                    const float vlen = v.length();
                    const QVector3D dir = vlen > 1e-3f ? v / vlen : QVector3D(0.0f, -1.0f, 0.0f);
                    // 入射面 = |v| 主轴上「箭来源侧」的 block 边界面（vx>0 → 来源 -X 侧 → x=bx 面；余类推），
                    //   其余两轴用 next 坐标（命中点在该面上的投影）。非主轴坐标可能略入块，但箭细长沿 dir 定向无碍。
                    float fx = next.x(), fy = next.y(), fz = next.z();
                    const float ax = std::abs(e.vx), ay = std::abs(e.vy), az = std::abs(e.vz);
                    if (ax >= ay && ax >= az) fx = e.vx > 0.0f ? float(bx) : float(bx + 1);
                    else if (ay >= az)        fy = e.vy > 0.0f ? float(by) : float(by + 1);
                    else                       fz = e.vz > 0.0f ? float(bz) : float(bz + 1);
                    e.pos = QVector3D(fx, fy, fz) - dir * kArrowEmbed; // 心在面外、尖入面内（半嵌可见）
                    e.arrowStuck = true;
                    e.arrowLife = kStuckArrowLifetime;
                    dirty = true;
                    continue; // 嵌入态：跳过玩家 / mob 命中 + 越界兜底（已钉面；下帧由顶部 arrowStuck 分支 despawn）
                }
            }

            // 玩家命中：箭（点）是否落在玩家 AABB 外扩命中盒内。玩家 AABB = listener（脚位）[−hw,+hw]×
            //   [0,height]×[−hw,+hw]；XZ/Z 外扩 kArrowHitHalfW，Y 上下各外扩 kArrowHitHalfW（提升近距命中率）。
            //   命中 → 发 mobAttackedPlayer(kArrowDamage, MobBones)（呈现层据 Survival 门控应用伤害，同近战
            //   aiHostile attack 路径）+ 移除箭。Creative/Spectator 玩家经 onMobAttackedPlayer 跳过 takeDamage。
            //   t290 观察者交互门控：playerTargetable=false（创造/观察者）→ 箭直接穿过玩家不判定命中（机制等价
            //   MC 1.0 创造/观察者无敌 —— 既不射（aiArcher 不射击非生存玩家）也不被命中；防 Survival→模式切换
            //   后半空中的箭仍戳到刚转无敌的玩家）。
            //   t304 玩家射出的箭（arrowFromPlayer=true）默认不判玩家命中 —— 玩家箭只打 mob（下方分支），不会误伤
            //   玩家自己（机制等价 MC 1.0 玩家箭不伤玩家）。
            //   t324 自身箭下落自伤例外：玩家射出的箭飞行 kArrowSelfArmDelay（发射者忽略窗口）后「武装」—— 下落砸中
            //   玩家时也扣 arrowDamage HP（机制等价 MC 1.0 玩家可被自己朝天射落的箭砸伤）。窗口防贴脸出膛误伤（箭
            //   spawn 在玩家外扩命中盒内，未武装前穿过不触发）。骷髅箭（arrowFromPlayer=false）恒命中玩家。
            //   t321 全局节流门控：m_playerHitCooldown>0（玩家刚被任一 mob 命中，节流无敌帧内）→ 跳过玩家命中判定
            //   （箭穿过玩家不触发伤害、不移除，继续飞行），与 aiHostile attack 节流一致 —— 防多弓手齐射秒杀玩家。
            const float flightTime = kArrowLifetime - e.arrowLife; // 已飞行秒数（arrowLife 从 kArrowLifetime 递减）
            const bool selfArmed = !e.arrowFromPlayer || flightTime >= kArrowSelfArmDelay;
            if (!remove && playerTargetable && selfArmed && m_playerHitCooldown <= 0.0f) {
                const float px = listener.x(), py = listener.y(), pz = listener.z();
                const float ex = px - listenerHalfW - kArrowHitHalfW;
                const float ey = py - kArrowHitHalfW;
                const float ez = pz - listenerHalfW - kArrowHitHalfW;
                if (next.x() >= ex && next.x() <= px + listenerHalfW + kArrowHitHalfW
                    && next.y() >= ey && next.y() <= py + listenerHeight + kArrowHitHalfW
                    && next.z() >= ez && next.z() <= pz + listenerHalfW + kArrowHitHalfW) {
                    // t296 箭击退方向 = 箭飞行速度 (vx,vz) 归一（沿箭去向推玩家；机制等价 MC 箭动量传递）。
                    //   退化（箭水平速 ~0，近乎垂直下落）→ 用「玩家 − 箭」水平向量兜底（把玩家推离着箭点）。
                    float kbX = 0.0f, kbZ = 0.0f;
                    const float vlen = std::sqrt(e.vx * e.vx + e.vz * e.vz);
                    if (vlen > 1e-3f) { kbX = e.vx / vlen; kbZ = e.vz / vlen; }
                    else {
                        const float tx = px - e.pos.x(), tz = pz - e.pos.z();
                        const float tlen = std::sqrt(tx * tx + tz * tz);
                        if (tlen > 1e-3f) { kbX = tx / tlen; kbZ = tz / tlen; }
                        else { kbX = 1.0f; }
                    }
                    // t324 命中伤害 / 死因来源分流：骷髅箭（arrowFromPlayer=false）= kArrowDamage / MobBones（t283 旧路径）；
                    //   玩家自身箭（arrowFromPlayer=true）= arrowDamage（蓄力 1..6）/ mobType=-1（无 mob 来源 → 呈现层
                    //   死因映射兜底 Generic，机制等价 MC「被自己的箭砸死」无特定凶手）。
                    const int dmg = e.arrowFromPlayer ? e.arrowDamage : kArrowDamage;
                    const int srcMobType = e.arrowFromPlayer ? -1 : int(MobBones);
                    emit mobAttackedPlayer(dmg, srcMobType, kbX, kbZ);
                    m_playerHitCooldown = kPlayerHitThrottle; // t321 串行化玩家受击（多弓手轮替命中）
                    hitPlayer = true;
                    hitPlayerDmg = dmg;
                    remove = true;
                }
            }

            // t304 玩家箭命中 mob（spec「抛物+伤害 mobs」）：玩家射出的箭（arrowFromPlayer=true）沿飞行逐帧
            //   测点是否落在任一活体 mob 的 AABB（外扩 kArrowHitHalfW 提升近距命中率）内。命中首个最近 mob
            //   → damageEntity（扣 arrowDamage HP + 红闪 + 归零 mobDied 死亡掉落，复用受击链）+ emit mobAttacked
            //   （呈现层 playMobHurt；同近战 attackMob 路径）+ 移除箭。跳过非 alive / 非 Mob / dead（尸体） /
            //   Arrow / Item（掉落物）实体。mob AABB 用每实体 halfW/halfH（t252 拆分 XZ/Y，按 mobType 贴合身体）。
            //   机制等价 MC 1.0 玩家弓箭打怪（命中首个、伤害由蓄力决定、箭命中即消失）。
            if (!remove && e.arrowFromPlayer) {
                for (int mi = 0; mi < int(m_entities.size()); ++mi) {
                    const Entity &m = m_entities[size_t(mi)];
                    if (!m.alive || m.kind != Mob || m.dead) continue; // 跳过空槽 / 非 mob / 尸体
                    const float ex2 = m.pos.x() - m.halfW - kArrowHitHalfW;
                    const float ey2 = m.pos.y() - m.halfH - kArrowHitHalfW;
                    const float ez2 = m.pos.z() - m.halfW - kArrowHitHalfW;
                    if (next.x() >= ex2 && next.x() <= m.pos.x() + m.halfW + kArrowHitHalfW
                        && next.y() >= ey2 && next.y() <= m.pos.y() + m.halfH + kArrowHitHalfW
                        && next.z() >= ez2 && next.z() <= m.pos.z() + m.halfW + kArrowHitHalfW) {
                        damageEntity(mi, e.arrowDamage); // 扣血 + 红闪 + 归零 mobDied（内含 dead/越界/amount 守）
                        emit arrowHitMob(m.mobType); // t304 命中音（呈现层 playMobHurt，同近战 attackMob→mobAttacked）
                        qCInfo(lcEnt) << "player arrow hit mob" << mi << "for" << e.arrowDamage << "HP";
                        remove = true;
                        break; // 命中首个即止（箭消失，不穿透）
                    }
                }
            }

            // 越界兜底（飞出世界 XZ 边界 / 跌出底部）→ 移除（防永久飞行堆积）。
            if (!remove) {
                if (next.x() < 0.0f || next.z() < 0.0f
                    || next.x() > worldW || next.z() > worldD || next.y() < 0.0f) {
                    remove = true;
                }
            }

            if (remove) {
                toRemove.push_back(idx);
                dirty = true;
                if (hitPlayer) qCInfo(lcEnt) << "arrow hit player for" << hitPlayerDmg << "HP";
            } else {
                e.pos = next; // 继续飞行
                dirty = true;
            }
            continue; // Arrow 不走 Mob AI / resting / 击退衰减
        }

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
                // 死亡态：冻结 AI / 重力 / 敌对攻击，仅 deathTimer 倒计时（给 QML 播侧倒动画 + 白烟窗口）+
                //   hurtFlash 衰减（让 killing blow 的红闪自然褪去）。deathTimer≤0 → emit mobDied（掉落物，
                //   t449 延迟到此刻）+ 标记移除（releaseSlot）。
                e.deathTimer -= float(dt);
                if (e.deathTimer <= 0.0f) {
                    // t449 死亡过渡结束才掉落（机制等价 MC 倒地动画后产掉落物）：mobDied 在 damageEntity 致死
                    //   瞬间不再 emit，改在此 emit —— 侧倒 + 白烟已播完 kDeathTime → 此刻掉落物自然弹出。
                    //   坐标 floor(pos) 与 spawnItem 整数格入口一致（dead 态 pos 冻结，与致死瞬间同位）。
                    const int dx = qFloor(e.pos.x()), dy = qFloor(e.pos.y()), dz = qFloor(e.pos.z());
                    emit mobDied(dx, dy, dz, e.mobType, e.deathBurned);
                    toRemove.push_back(idx);
                    dirty = true;
                }
                if (e.hurtFlash > 0.0f) {
                    e.hurtFlash -= float(dt);
                    if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; } // 红闪结束 → bump 让 QML 翻回 baseColor
                }
                continue; // dead：不走 AI / 重力
            }

            // t344 火烧系统（岩浆 / 火点燃；ALL mobs 含 passive；机制等价 MC 1.0 实体触岩浆着火 + 火伤 + 熄灭）。
            //   分两段：
            //   (1) 岩浆接触点燃：mob 脚位格 floor(pos.y−halfH) 或身体中心格 floor(pos.y) 任一 == Lava → 刷新
            //       fireTimer = kFireDuration（落入岩浆湖 / 踩岩浆流即着火；机制等价 MC 实体进岩浆着火）。
            //       仍在岩浆中重置火伤累积（机制等价 MC：岩浆内持续重燃，fireTimer 不递减）。
            //   (2) 火烧推进：fireTimer>0 → 离开岩浆后递减；每 kFireDamageInterval 扣 1HP（复用 damageEntity 受击链：
            //       扣血 + 红闪 + 归零 mobDied 带 burned=true → 熟肉掉落）+ 掷随机提前熄灭（kFireExtinguishChance）。
            //       fireTimer 自然归零即熄（定时双保险）。passive 与敌对均走本段（日光 burning 仍由 tickHostileLife
            //       独立管，二者在 isBurningAt 合取显火焰）。只读 World::blockAt（向下依赖）。
            {
                const int fx = qFloor(e.pos.x());
                const int fz = qFloor(e.pos.z());
                const int footY = qFloor(e.pos.y() - e.halfH); // 脚位（AABB 底面）格
                const int bodyY = qFloor(e.pos.y());           // 身体中心格
                bool touchingLava = false;
                if (footY >= 0 && world->blockAt(fx, footY, fz) == BlockRegistry::Lava) touchingLava = true;
                if (!touchingLava && bodyY >= 0 && world->blockAt(fx, bodyY, fz) == BlockRegistry::Lava)
                    touchingLava = true;
                if (touchingLava) {
                    if (e.fireTimer < kFireDuration) { e.fireTimer = kFireDuration; dirty = true; } // 翻入着火 → bump（QML 显火焰）
                    e.fireDamageTimer = 0.0f; // 岩浆内重置火伤累积（持续重燃）
                }
                // t385 雨灭 mob 火（spec「雨灭 mob 火」）：mob 直接见天（skyLightAt>=15 = 头顶无遮挡）且所在列
                //   正降水（雨/雪/雷，群系解析；沙漠不降水）→ 立即灭火。机制等价 MC 雨水浇灭着火实体。
                //   仅露天生效（树下/屋内不淋雨，火不灭）。与日光 burning 独立（fireTimer 适用于所有 Mob）。
                if (e.fireTimer > 0.0f) {
                    const bool mobSkyExposed = (fx >= 0 && fz >= 0 && fx < int(worldW) && fz < int(worldD)
                                                && bodyY >= 0 && bodyY < world->height()
                                                && world->skyLightAt(fx, bodyY, fz) >= 15);
                    if (mobSkyExposed && world->isPrecipitatingAt(fx, fz)) {
                        e.fireTimer = 0.0f;
                        e.fireDamageTimer = 0.0f;
                        dirty = true; // 熄火 → bump（QML 收火焰）
                    }
                }
                if (e.fireTimer > 0.0f) {
                    if (!touchingLava) e.fireTimer -= float(dt);
                    e.fireDamageTimer += float(dt);
                    if (e.fireDamageTimer >= kFireDamageInterval) {
                        e.fireDamageTimer -= kFireDamageInterval;
                        // 先掷随机提前熄灭（机制等价 MC 火 random extinguish）；不熄才扣 1HP 火伤。
                        if (QRandomGenerator::global()->generateDouble() < double(kFireExtinguishChance)) {
                            e.fireTimer = 0.0f;
                            e.fireDamageTimer = 0.0f;
                            dirty = true; // 熄火 → bump（QML 收火焰）
                        } else if (!e.dead) { // 防御：damageEntity 可能本帧已死
                            damageEntity(idx, 1); // 火伤 1HP（复用受击链；归零 mobDied 带 burned=true）
                            dirty = true;
                        }
                    }
                    // 定时熄灭：fireTimer 自然归零（离开岩浆后持续 kFireDuration 秒即灭）。
                    if (e.fireTimer <= 0.0f) {
                        e.fireTimer = 0.0f;
                        e.fireDamageTimer = 0.0f;
                        dirty = true;
                    }
                }
            }
            // 火伤可能本帧致死（damageEntity 置 dead）→ 本帧不再走 AI / 重力（同上方 dead 分支语义，防死尸位移）。
            if (e.dead) continue;

            // t394 仙人掌接触伤害（spec「contact damages entities that touch it」；机制等价 MC 1.0 仙人掌触碰即伤）。
            //   接触判定：mob 脚位 / 身体格（floor(pos±halfH)）及其水平 4 邻 + 脚下格，任一 == Cactus 即接触
            //   （覆盖「撞其侧」+「站其顶」；Cactus 是实体整立方 → mob 碰撞停在邻格，故脚 / 身体格本身恒非 Cactus，
            //   须查邻接格 / 脚下格）。每 kCactusDamageInterval(0.5s) 扣 1HP（复用 damageEntity 受击链：扣血 + 红闪 +
            //   归零 mobDied 死亡掉落）。离开即重置累积器（机制等价 MC：接触才扣，离开即停）。只读 World::blockAt
            //   （向下依赖）。
            {
                const int fx = qFloor(e.pos.x());
                const int fz = qFloor(e.pos.z());
                const int footY = qFloor(e.pos.y() - e.halfH); // 脚位（AABB 底面）格
                const int bodyY = qFloor(e.pos.y());           // 身体中心格
                auto cactusCell = [&](int xx, int yy, int zz) -> bool {
                    return yy >= 0 && yy < world->height()
                           && world->blockAt(xx, yy, zz) == BlockRegistry::Cactus;
                };
                bool touch = false;
                for (int yy : {footY, bodyY}) {
                    if (cactusCell(fx,     yy, fz) || cactusCell(fx + 1, yy, fz)
                        || cactusCell(fx - 1, yy, fz) || cactusCell(fx, yy, fz + 1)
                        || cactusCell(fx, yy, fz - 1)) { touch = true; break; }
                }
                if (!touch && cactusCell(fx, footY - 1, fz)) touch = true; // 站在仙人掌顶
                if (touch) {
                    e.cactusDamageTimer += float(dt);
                    if (e.cactusDamageTimer >= kCactusDamageInterval) {
                        e.cactusDamageTimer = 0.0f;
                        if (!e.dead) { damageEntity(idx, 1); dirty = true; } // 仙人掌扎伤 1HP（复用受击链）
                    }
                } else {
                    e.cactusDamageTimer = 0.0f; // 离开即重置
                }
            }
            // 仙人掌扎伤可能本帧致死 → 本帧不再走 AI / 重力（同上方 dead 分支语义，防死尸位移）。
            if (e.dead) continue;

            // t239 AI wander 自主移动（水平）：随机选向 + 时间片 + 逐轴 AABB 碰撞。位移 → dirty（驱动 QML 位置绑定）。
            // t241 羊吃草门控：eatTimer>0（吃草周期内）→ 跳过 wander + 强制 idle 站立（腿停 + 头俯仰），仅推进
            //   吃草周期；否则走 AI wander，并据 idle + 扫描冷却决定是否开吃草周期。
            // t298 怪物受水流影响：脚位在水格 → 水平减速（speedScale=kWaterSpeedMul）。透传给各 AI 函数缩放位移；
            //   浮力缓沉 + 流水推动在下方 Mob 分支末段（flow push）+ 共享垂直段（buoyancy）处理。
            const float speedScale = mobFeetInWater(world, e.pos.x(), e.pos.y(), e.pos.z(), e.halfH)
                                     ? kWaterSpeedMul : 1.0f;
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
            } else if (e.hostile) {
                // t290 观察者交互门控：玩家不可锁定（创造/观察者）→ 敌对 Mob 不 detect/chase/attack/shoot，
                //   回退 wander（机制等价 MC 1.0 创造/观察者无敌且不被仇恨）。清残留追踪态（chasing/chaseTimer）
                //   + Stalker 熄火（fuseTimer 归零）防 Survival→Creative/Spectator 切换后 mob 仍贴脸/蓄力。
                //   playerTargetable 由 PlayerController 传（mode==Survival）。变值即回退 wander → dirty 若真移动。
                if (!playerTargetable) {
                    if (e.chasing || e.fuseTimer > 0.0f) {
                        e.chasing = false;
                        e.chaseTimer = 0.0f;
                        e.fuseTimer = 0.0f;
                        dirty = true; // chasing/fuse 翻转 → bump 让 QML 收回追踪高亮 / 蓄力膨胀
                    }
                    if (aiWander(e, float(dt), world, worldW, worldD, speedScale)) dirty = true;
                } else if (e.mobType == MobBones) {
                    // t281/t283/t284 敌对 AI：替代 wander。listener = 玩家脚位（tick 参数）。
                    //   t281 Shambler（僵尸）→ aiHostile（detect→pathfind→melee attack）。
                    //   t283 Bones（骷髅弓箭手）→ aiArcher（detect→keep-distance→shoot 远程射箭）。
                    //   t284 Stalker（潜行者/苦力怕）→ aiStalker（detect→chase→fuse→detonate 近距自爆）。
                    //   非追踪回退到 wander（在 aiHostile / aiArcher / aiStalker 内）。
                    if (aiArcher(e, float(dt), world, listener, worldW, worldD, speedScale)) dirty = true;
                    // t331 拉弓期（chasing）每帧 bump revision → QML drawAmountAt 绑定刷新（驱动抬臂 + 弦后拉）；
                    //   即使 aiArcher 返 moved=false（拉弓减速到停），aimTimer 仍在变 → 须 dirty（同 Stalker inflate 模式）。
                    if (e.chasing) dirty = true;
                } else if (e.mobType == MobStalker) {
                    if (aiStalker(e, float(dt), world, listener, worldW, worldD, speedScale)) dirty = true;
                    // 蓄力期（chasing）每帧 bump revision → QML inflateAt 绑定刷新（驱动膨胀动画 + 蓄力发白）；
                    //   即使 aiStalker 返 moved=false（蓄力站立不动），inflate 仍在变 → 须 dirty。熄火（fuseTimer→0）
                    //   亦在 chasing 态内 → 一并刷新让 QML 收回膨胀。
                    if (e.chasing) dirty = true;
                    // 引爆当帧移除：detonateStalker 置 exploded=true → 跳过后续重力 / resting（尸体即除）。
                    if (e.exploded) { toRemove.push_back(idx); continue; }
                } else {
                    if (aiHostile(e, float(dt), world, listener, worldW, worldD, speedScale)) dirty = true;
                }
            } else {
                // 非吃草：扫描冷却倒数（仅羊）；AI wander；羊 idle 且冷却到 → 扫前方草丛决定是否开吃。
                //   t399 鱿鱼（mobType==MobSquid）走 aiSquid（水里喷水游动；非 aiWander），且无吃草分支（非羊）。
                if (e.mobType == MobSquid) {
                    if (aiSquid(e, float(dt), world, worldW, worldD, speedScale)) dirty = true;
                } else {
                // t400 求偶寻偶（spec「喂食 → 求偶 → 同种配对」；机制等价 MC 1.0 love mode 寻偶）：成体可繁殖 mob
                //   在求偶期（loveTimer>0）→ 覆盖 wander 的随机选向，把 yaw 钉向最近同种求偶配偶 + 强制行走 +
                //   短置 wanderTimer（防 aiWander 本帧重新随机选向）→ aiWander 沿该 yaw 行走靠近配偶，使两求偶者
                //   主动相遇进入配对距离（tickBreeding 末段配对产幼崽）。无配偶（仅一方求偶）/ 非求偶 → 走原 wander。
                //   寻偶仅设 yaw/speed/timer（不改 pos），实际位移交 aiWander（复用其逐轴碰撞撤回 + 边界 clamp，
                //   防穿墙 / 出界）。squid loveTimer 恒 0（不可繁殖）→ 本块对其 no-op。
                if (e.loveTimer > 0.0f && !e.baby && isBreedableType(e.mobType)) {
                    const int mate = findNearestMate(idx);
                    if (mate >= 0) {
                        const Entity &mp = m_entities[size_t(mate)];
                        const float mdx = mp.pos.x() - e.pos.x();
                        const float mdz = mp.pos.z() - e.pos.z();
                        // 朝配偶（dir=(-sin,0,-cos) 约定：yaw=atan2(-Δx,-Δz) 使模型 -Z 正对配偶）。
                        e.yawRad = std::atan2(-mdx, -mdz);
                        e.wanderSpeed = kWalkSpeed; // 强制行走（覆盖 idle 可能）
                        e.wanderTimer = 0.4f;       // 防 aiWander 本帧重新随机选向（0.4s > 一帧 dt，下帧再重设）
                    }
                }
                if (isSheep && e.eatCooldown > 0.0f) e.eatCooldown -= float(dt);
                if (aiWander(e, float(dt), world, worldW, worldD, speedScale)) dirty = true;
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
                // t300 剪羊毛后的羊吃草方块重新长毛（spec「裸羊站在草方块上偶尔吃它 → 长毛 + 草方块变泥土」；
                //   机制等价 MC 1.0「羊吃草方块重新长毛」）。仅 sheared=true 的羊走本分支（未剪羊毛的羊
                //   sheared=false 默认状态 → 跳过，无需重新长毛）。regrowCooldown 由 shearSheep 设 kRegrowCooldown
                //   防刚剪完即长回；冷却到 + 站在草方块上（脚下方块 id==Grass）→ 翻 sheared=false（重新长毛）+
                //   脚下草方块→泥土（setWaterSilent 静默写，同 sheepEatGrass 的草方块→泥土，非玩家破块 → 不发
                //   broken/placed，免粒子/音/掉落噪音）。扫描用 kRegrowScanInterval 节流（每秒扫一次脚下方块，
                //   足够肉眼可见的「重新长毛」事件，不每帧扫 blockAt 省开销）。
                //   **不要求 idle**（行走中亦可触发，机制等价 MC 1.0 羊走过草方块即可能吃 —— 重新长毛是吃草方块的
                //   派生效果，非主动行为，不强制站立）；spec「偶尔」表概率性，节流扫描间隔本身就是「偶尔」语义。
                if (isSheep && e.sheared) {
                    if (e.regrowCooldown > 0.0f) {
                        e.regrowCooldown -= float(dt);
                    } else {
                        // 冷却到：扫脚下方块（AABB 底面下一格 = 支撑格）。是 Grass → 重新长毛 + 草方块→泥土。
                        const int gx = qFloor(e.pos.x());
                        const int gy = qFloor(e.pos.y() - e.halfH) - 1; // 脚位格（AABB 底面）下一格 = 支撑方块
                        const int gz = qFloor(e.pos.z());
                        if (gy >= 0 && world->blockAt(gx, gy, gz) == BlockRegistry::Grass) {
                            world->setWaterSilent(gx, gy, gz, BlockRegistry::Dirt, 0); // 草方块→泥土（静默写）
                            e.sheared = false;       // 重新长毛（QML 据 shearedAt 翻回毛茸外观）
                            e.regrowCooldown = 0.0f; // 未剪羊毛不再推进（下次剪切重置）
                            dirty = true;            // bump → QML 翻外观
                            qCInfo(lcEnt) << "sheep regrew wool at" << e.pos
                                          << "(grass block at" << gx << gy << gz << "-> dirt)";
                        } else {
                            // 站在非草方块上：保持冷却到 0 但不立即长毛；下次扫描间隔（kRegrowScanInterval）
                            //   再查（防每帧扫）。设短冷却节流。
                            e.regrowCooldown = kRegrowScanInterval;
                        }
                    }
                }
                } // 非 squid 的 passive（sheep 吃草 / 通用 wander）；squid 走上面 aiSquid 分支
            }

            // t298 流水推动 mob（机制等价玩家 t211：脚位在流水格 state>0 → 沿「离源方向」叠入水平位移）。
            //   流向据脚位 4 向邻居 state 梯度推算：state 低于脚位的邻居 = 近源方向 → 推力朝远离它（离源）。
            //   梯度加权（footState − ns）使陡降（近源 → 远源跨多级）推得更猛；归一化后 ×kWaterFlowPush 叠入位移
            //   （与 AI 行走位移相加 → 逆流净速 = 走速 − 推力，松手则被流走）。逐轴 mobAabbHitsSolid 撤回防穿墙 +
            //   世界边界 clamp（同 aiWander / knockback 位移模式）。仅 speedScale<1（脚位在水格）时执行（无水零开销）。
            //   水源 state=0 不推（无梯度）；四面无更低 state 邻居（对称流）→ glen≈0 不推。
            if (speedScale < 1.0f) {
                const int wfx = qFloor(e.pos.x());
                const int wfy = qFloor(e.pos.y() - e.halfH); // 脚位（AABB 底面）格
                const int wfz = qFloor(e.pos.z());
                if (wfy >= 0 && world->blockAt(wfx, wfy, wfz) == BlockRegistry::Water) {
                    const quint8 footState = world->stateAt(wfx, wfy, wfz);
                    if (footState > 0) { // 流水格才推（水源 state=0 静止不推，同玩家 t211）
                        float gx = 0.0f, gz = 0.0f;
                        constexpr int wdirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                        for (const auto &wd : wdirs) {
                            const int nx = wfx + wd[0], nz = wfz + wd[1];
                            if (world->blockAt(nx, wfy, nz) == BlockRegistry::Water) {
                                const quint8 ns = world->stateAt(nx, wfy, nz);
                                if (ns < footState) { // 该邻居更近源 → 推力朝远离它（离源）
                                    gx -= float(wd[0]) * float(footState - ns);
                                    gz -= float(wd[1]) * float(footState - ns);
                                }
                            }
                        }
                        const float glen = std::sqrt(gx * gx + gz * gz);
                        if (glen > 1e-4f) {
                            const float ehw = e.halfW;
                            const float ehh = e.halfH;
                            const float pushX = (gx / glen) * kWaterFlowPush;
                            const float pushZ = (gz / glen) * kWaterFlowPush;
                            float newX = e.pos.x() + pushX * float(dt);
                            if (newX < ehw) newX = ehw;
                            if (newX > worldW - ehw) newX = worldW - ehw;
                            if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                            float newZ = e.pos.z() + pushZ * float(dt);
                            if (newZ < ehw) newZ = ehw;
                            if (newZ > worldD - ehw) newZ = worldD - ehw;
                            if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                            if (newX != e.pos.x()) { e.pos.setX(newX); dirty = true; }
                            if (newZ != e.pos.z()) { e.pos.setZ(newZ); dirty = true; }
                        }
                    }
                }
            }

            // 受击红闪衰减（非 dead Mob）：仅在跨过 0 时 bump（红闪期间 colorAt 恒红无需每帧 bump；结束翻回 baseColor）。
            if (e.hurtFlash > 0.0f) {
                e.hurtFlash -= float(dt);
                if (e.hurtFlash <= 0.0f) { e.hurtFlash = 0.0f; dirty = true; }
            }

            // t250 环境音 proximity 门控：仅听者 kAudioRange 半径内的活体 mob 才 emit idle/step 叫声（远场静默，
            //   防多 mob 同步吵闹 + 无意义远场音频）。每 mob 每帧算一次（XZ 主导，Y 纳入避免垂直堆叠 mob 全响）。
            const float adx = e.pos.x() - listener.x();
            const float ady = e.pos.y() - listener.y();
            const float adz = e.pos.z() - listener.z();
            const bool inAudioRange = (adx * adx + ady * ady + adz * adz) <= kAudioRange * kAudioRange;

            // t250 mob idle 叫声（牛叫/羊叫/猪叫）：ambientTimer 周期倒计时 → 0 时听者范围内 emit mobAmbient
            //   （mobType 供 AudioManager 选 mob_idle clip）+ 重置随机周期（错峰，防多 mob 同步叫）。
            //   机制等价 MC 1.0 被动生物偶发 idle call；不论 idle/行走/吃草，活体 mob 周期性偶发叫。
            e.ambientTimer -= float(dt);
            if (e.ambientTimer <= 0.0f) {
                e.ambientTimer = kAmbientMin
                                 + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kAmbientMax - kAmbientMin);
                if (inAudioRange) emit mobAmbient(e.mobType);
            }

            // t398 鸡下蛋（spec「periodically lays an EGG item」）：仅 mobType==MobChicken 推进 eggTimer（其余 mob
            //   eggTimer=0 早退）。周期到 → emit chickenLaidEgg(floor(pos))（呈现层据它 spawnItem 生成蛋物品掉落，
            //   同 mobDied→spawnItem 模式）+ 重置随机周期（kEggLayMin..Max，机制等价 MC 1.0 鸡 5-10 分钟下一枚蛋）。
            //   不受 idle/行走态门控（活体鸡无论静止 / 游荡均周期下蛋，机制等价 MC 鸡下蛋独立于行为）。坐标取
            //   floor(pos) 与 spawnItem 整数格约定一致（蛋落在鸡身旁）。无 listener 范围门控 —— 蛋是物品实体非音频，
            //   远场鸡下蛋亦须生成（玩家走近即可见）。
            if (e.mobType == MobChicken) {
                e.eggTimer -= float(dt);
                if (e.eggTimer <= 0.0f) {
                    e.eggTimer = kEggLayMin
                                 + float(QRandomGenerator::global()->bounded(1000)) / 1000.0f * (kEggLayMax - kEggLayMin);
                    emit chickenLaidEgg(qFloor(e.pos.x()), qFloor(e.pos.y() - e.halfH), qFloor(e.pos.z()));
                }
            }

            // t241 行走动画相位推进：moveSpeed>0（行走 / 被推）→ walkPhase 前进（fmod 2π，QML 据它驱动腿摆）；
            //   idle / 吃草 / 撞墙 → 冻结（moveSpeed=0 不进，腿停于上次相位）。每推进帧 bump dirty 让绑定刷新。
            //   t250 mob 走路声：相位推进量同步累加进 stepAccum，每半步（π=一次脚落）听者范围内 emit mobStep
            //   （mobType + 脚下方块 id 供 AudioManager 按材质组选 step clip）。半步语义同 player QML 端
            //   「Δphase≥π 播一次脚步音」，搬进 C++ 避逐 mob 追踪 walkPhase（多 mob 在 QML 追踪不现实）。
            if (e.moveSpeed > 0.0f) {
                const float advance = e.moveSpeed * float(dt) * kWalkFreq;
                e.walkPhase = std::fmod(e.walkPhase + advance, 6.2831853f);
                e.stepAccum += advance;
                if (e.stepAccum >= kStepHalfStride) {
                    e.stepAccum -= kStepHalfStride;
                    if (inAudioRange) {
                        // 脚下方块 id（材质组判定用；与 resting 复探同列格 = 底面下方一格）。越界 / air → 0
                        //   → GroupDefault 兜底 Stone step（同 player 脚步音越界处理；仍响）。
                        const int sfx = qFloor(e.pos.x());
                        const int sfz = qFloor(e.pos.z());
                        const int sfy = qFloor(e.pos.y() - e.halfH) - 1;
                        const quint8 sid = (sfy >= 0) ? world->blockAt(sfx, sfy, sfz) : quint8(BlockRegistry::Air);
                        emit mobStep(e.mobType, int(sid));
                    }
                }
                dirty = true;
            }

            // t249 击退水平位移应用（vx/vz 衰减 + 逐轴碰撞位移）。knockback() 受击瞬间设 vx/vz，本处每 tick 把
            //   速度转位移（叠加在 aiWander 移动之上 → 击退期间 AI 仍走，二者位移相加，同 MC「既有动量又有击退」）
            //   + 指数衰减（vx *= 1 - kKnockbackDrag*dt，~0.5s 基本停）。逐轴（X 后 Z）mobAabbHitsSolid 撤回防穿墙
            //   （同 aiWander / resolvePlayerPush）；世界边界 clamp（防击退出世界）。速度衰减到可忽略 → 清零（防永
            //   久微小漂移 / 每帧 dirty 抖动）。仅 vx/vz 任一非零时执行（无击退的 mob 零开销跳过）。
            if (std::abs(e.vx) > 1e-4f || std::abs(e.vz) > 1e-4f) {
                const float ehw = e.halfW; // t252 XZ 半宽（边界 clamp + 碰撞）
                const float ehh = e.halfH; // t252 Y 半高（footprint 格扫）
                float newX = e.pos.x() + e.vx * float(dt);
                if (newX < ehw) newX = ehw;
                if (newX > worldW - ehw) newX = worldW - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), e.pos.z(), ehw, ehh)) newX = e.pos.x();
                float newZ = e.pos.z() + e.vz * float(dt);
                if (newZ < ehw) newZ = ehw;
                if (newZ > worldD - ehw) newZ = worldD - ehw;
                if (mobAabbHitsSolid(world, newX, e.pos.y(), newZ, ehw, ehh)) newZ = e.pos.z();
                if (newX != e.pos.x()) { e.pos.setX(newX); dirty = true; }
                if (newZ != e.pos.z()) { e.pos.setZ(newZ); dirty = true; }
                const float decay = std::max(0.0f, 1.0f - kKnockbackDrag * float(dt));
                e.vx *= decay;
                e.vz *= decay;
                if (std::abs(e.vx) < 0.05f) e.vx = 0.0f; // 衰减到可忽略 → 清零
                if (std::abs(e.vz) < 0.05f) e.vz = 0.0f;
            }

            // t254 窒息（机制同玩家 t160 的「眼位嵌实体方块 → 每 1s 扣 1HP」）：mob 头部（AABB 顶格）嵌入实体
            //   可碰撞方块（被沙 / 方块埋住）→ 累加 suffocationTimer，每 kSuffocationInterval 秒扣 1HP（复用
            //   damageEntity → hurtFlash 红闪 / mobDied 死亡掉落链，同玩家 fallDamageTaken(1)→takeDamage）。
            //   头部出方块即停累积（脱困即停伤）。dead mob 已在上方早退 continue 跳过（尸体不再窒息）。
            //   头部格 = floor(pos.y + halfH − ε)；ε 防 AABB 顶恰整数误取上方空气格（漏判窒息 → 沙埋不死）。
            //   用 isCollidable（非 isSolid）：火把 / 开门 / 半砖等非完整碰撞方块不致窒息（同玩家 t160 用
            //   isCollidable 判定；仅「头部被完整碰撞方块包裹」才窒息，机制等价 MC 头卡进 solid block）。
            //   注：damageEntity 内 bump revision + emit，本 tick 末尾仍再 emit 一次（同帧多次 emit 无副作用，
            //   QML 绑定合并到下次事件循环求值）。
            {
                const int sx = qFloor(e.pos.x());
                const int sz = qFloor(e.pos.z());
                const int sy = qFloor(e.pos.y() + e.halfH - 1e-3f);
                if (sy >= 0 && world->isCollidable(sx, sy, sz)) {
                    e.suffocationTimer += float(dt);
                    if (e.suffocationTimer >= kSuffocationInterval) {
                        e.suffocationTimer -= kSuffocationInterval;
                        damageEntity(idx, 1); // 复用受击链：扣 1HP + 红闪 + （归零时）死亡掉落（内含 dead/越界/amount 守）
                        dirty = true;
                    }
                } else {
                    e.suffocationTimer = 0.0f; // 头部出方块 → 停累积（脱困即停伤）
                }
            }
        }

        // --- Mob（非 dead）/ Item：原有 resting + 重力 + 垂直运动（cx/cz 在 AI 行走后重算）---
        const int cx = qFloor(e.pos.x());
        const int cz = qFloor(e.pos.z());
        if (cx < 0 || cz < 0) continue; // 列坐标非法（实体飞出世界 XZ 边界）→ 跳过

        // t298 水中浮力判定：仅 Mob kind（vestigial Item 不涉水物理）。脚位（AABB 底面）格 == Water → mobInWater。
        //   feetCellY = floor(pos.y − halfH)（mob 底面格；pos.y 是中心）。用于下方重力分流（缓沉 vs 自由落体）。
        const int mobFeetY = qFloor(e.pos.y() - e.halfH);
        const bool mobInWater = (e.kind == Mob) && mobFeetY >= 0
                                 && world->blockAt(cx, mobFeetY, cz) == BlockRegistry::Water;

        // 已落地：复探支撑是否仍实体。失支撑 → 续落。
        // t362 改「footprint 任一列有支撑」（旧版仅中心列 cx/cz）：mob 走下 1 格台阶时，中心先越过台阶沿、
        //   但后半 footprint 仍压在更高支撑块上。旧版即判失支撑 → 重力把整格 snap 下沉到低地 → 此时 trailing
        //   边仍压在高块列 → 落地后水平移动被 mobAabbHitsSolid 判 trailing 腿卡进身后高块 → 每帧撤回 →
        //   永久卡死（用户「下台阶卡住变活靶」）。改 footprint 后：只要还有任一列压在更高支撑上就保 resting，
        //   悬出台阶沿继续前行；直到 trailing 边也越过台阶沿（footprint 全离支撑）才下沉 → 落低地时 trailing
        //   已不在高块列 → 腿不卡、干净步下（机制等价 MC mob 越过台阶沿后才自动步下 1 格）。
        if (e.resting) {
            const int supportY = mobFeetY - 1; // 实体底面下方那一格（= 支撑方块 cellY）
            if (mobFootprintHasSupport(world, e.pos.x(), e.pos.z(), supportY, e.halfW)) continue; // 仍实体 → 保持静止
            e.resting = false; // 支撑消失 → 续落（vy 已 0，从静止重新加速）
            dirty = true;
        }

        // 重力 + 下移（vy 向下为负）。t298：mob 在水中 → 缓沉（kWaterGravity << kGravity）+ 钳最大下沉
        //   （kWaterSinkMax << kMaxFall，防加速穿水底）；机制等价玩家 t174 水中浮力（mobs 不按空格故无上浮，
        //   仅被动缓沉到水底 resting）。离水走原重力 + 终端下落。
        if (mobInWater) {
            e.vy -= kWaterGravity * float(dt);
            if (e.vy < -kWaterSinkMax) e.vy = -kWaterSinkMax;
        } else {
            e.vy -= kGravity * float(dt);
            if (e.vy < -kMaxFall) e.vy = -kMaxFall;
        }
        const float mobNewY = e.pos.y() + e.vy * float(dt);

        // 下移路径自顶向下扫实体所在列，找首个实体方块（防大 dt 穿过薄层；lessons「子步防穿墙」精神）。
        const int mobTopCell = qFloor(e.pos.y()); // 当前中心所在格（一般为空气）
        int mobBotCell = qFloor(mobNewY);
        if (mobBotCell > mobTopCell) mobBotCell = mobTopCell; // 防浮点噪声
        int mobSolidY = -1;
        for (int cy = mobTopCell; cy >= mobBotCell; --cy) {
            if (cy < 0) break; // 越界下方=空气（World 约定）→ 不视作地面，实体继续落
            // t333 水视穿透：水格不计地面 → mob 穿水面入水（落水后转 mobInWater 浮/减速/流水推动分支，
            //   同 t271 掉落物穿水）。否则水格被 isSolid 当地面 → mob 粘在水面当着地（怪水上走）。
            if (world->blockAt(cx, cy, cz) != BlockRegistry::Water
                && world->isSolid(cx, cy, cz)) { mobSolidY = cy; break; }
        }

        if (mobSolidY >= 0) {
            // 落地：贴支撑方块顶面 + 静止偏移（底面 = mobSolidY+1 = 支撑方块顶；中心 = 顶 + halfH）。
            //   t252：kRestOffset → e.halfH（per-mob 半高；cow halfH=0.70 → 比 1×1 高 0.2，固定 0.5 无法表达）。
            const float restY = float(mobSolidY + 1) + e.halfH;
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

    // t256：移除的实体（着地 / 跌出的 FallingBlock + deathTimer 到 / void-loss 的 Mob）改 releaseSlot（标
    //   alive=false + 入 free list）替代 erase-shift —— 保 m_entities.size()（=count 属性 = QML Repeater
    //   model）单调不降 → Repeater 不需销毁 reparent 的 3D delegate → 消除掉落沙 spawn/land 抖动致 delegate
    //   泄漏。release 不 shift 索引，顺序无关（逆序仅为保留与旧 erase 路径一致的可读性）。
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
        releaseSlot(*it);

    // t400 繁殖 tick（主实体循环之外 —— 幼崽生成 acquireSlot 可能 push_back，主循环持 Entity& 引用期间不可 push_back
    //   致其失效）：衰减求偶 / 冷却 / 幼崽长大计时 + 求偶配对产幼崽（受 kPassiveMobCap 钳制）。dirty 合入本 tick
    //   末尾统一一次 bump + emit（批量收口，避免 N 幼崽 N 次 notify 风暴，同 t320/t354 纪律）。
    if (tickBreeding(dt)) dirty = true;

    if (dirty || !toRemove.empty()) { ++m_revision; emit entitiesChanged(); }
}
