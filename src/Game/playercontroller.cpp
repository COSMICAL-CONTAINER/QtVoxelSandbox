#include "playercontroller.h"
#include "playerstate.h" // t311 DeathCause 枚举（致死来源区分：Fall/Suffocation/Drowning/Starvation）
#include "loottable.h"   // t401 钓鱼获物池（fishingPool / roll）；同层 Game，向下依赖 Core
#include "world.h"

#include "frameprofiler.h" // perf：帧时间分解探针（tickImpl 各阶段 Scope + 1s 窗口 flush）

#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuaternion>
#include <QRandomGenerator> // t237 收割种子随机量（1-2）；玩家交互掉落的随机性，非 worldgen 确定性范畴
#include <QVariantMap>       // t715 状态效果快照（activeEffectsChanged 逐项 map 组装）

#include <algorithm>
#include <cmath>
#include <vector> // t720/t721 画作：连通域 BFS 收集（std::vector<Cell>）

// t476 时运附魔适用方块判定：矿石类（破块掉对应材料物品，掉落数量受时运加成）。机制等价 MC fortune 仅对
//   矿石 / 部分方块生效。本工程矿石段：煤 / 铁 / 钻 / 铜 / 金 / 青金 / 红石（破块掉冶炼材料，掉落数 ×时运有意义）。
//   非矿石（草 / 石 / 圆石等掉落数恒 1 的方块）时运不放大 —— MC 时运对石 / 圆石无效，避免无限石刷。
namespace {
bool isFortuneOre(quint8 blockId)
{
    switch (blockId) {
    case BlockRegistry::CoalOre:
    case BlockRegistry::IronOre:
    case BlockRegistry::DiamondOre:
    case BlockRegistry::CopperOre:
    case BlockRegistry::GoldOre:
    case BlockRegistry::LapisOre:
    case BlockRegistry::RedstoneOre: // t569 红石矿（掉 4 粉，时运放大最有感；机制等价 MC fortune 对红石生效）
        return true;
    default:
        return false;
    }
}

// t480 狼肉食判定（单一权威）：该物品 id 是否为狼繁殖食物（生/熟肉，机制等价 MC 1.0 狼吃生/熟肉繁殖）。
//   RecipeRegistry id（Game 层）；含生/熟猪排、生/熟牛肉、生/熟鸡肉、熟羊肉（羊肉仅熟变体，燃烧致死掉落）。
//   未列入的肉（如生鱼 RawFishId）不喂狼 —— 机制等价 MC 1.0 狼不吃鱼。PlayerController 肉食喂狼分支据它门控。
bool isWolfMeatItem(int itemId)
{
    return itemId == RecipeRegistry::RawPorkchopId
        || itemId == RecipeRegistry::RawBeefId
        || itemId == RecipeRegistry::RawChickenId
        || itemId == RecipeRegistry::CookedPorkchopId
        || itemId == RecipeRegistry::CookedBeefId
        || itemId == RecipeRegistry::CookedMuttonId
        || itemId == RecipeRegistry::CookedChickenId;
}
} // namespace

// t467 食物饥饿恢复量（单一权威）：返回 itemId 作为食物一次恢复的饥饿值；非食物 → 0。
//   面包（BreadId）= kBreadHungerAmount(5)、甜浆果（SweetBerryId）= kSweetBerryHungerAmount(2)、
//   蘑菇汤（MushroomStewId）= kMushroomStewHungerAmount(10)。
//   t513 胡萝卜（CarrotId）= 3、土豆（PotatoId）= 1（机制等价 MC 1.0 胡萝卜 +3 / 生土豆 +1 hunger）。
//   t513 生/熟肉：生猪排/生牛肉 +3、生鸡肉/生鱼 +2、熟猪排/熟牛肉 +8、熟鸡肉/熟羊肉 +6（机制等价 MC 1.0 各肉类）。
//   机制等价 MC 1.0 各食物恢复不同饥饿。新增食物只改本方法一处（避免各处硬编码）。
int PlayerController::foodHungerAmount(int itemId)
{
    if (itemId == RecipeRegistry::BreadId)        return kBreadHungerAmount;
    if (itemId == RecipeRegistry::SweetBerryId)   return kSweetBerryHungerAmount;
    if (itemId == RecipeRegistry::MushroomStewId) return kMushroomStewHungerAmount; // t507 蘑菇汤 +10 饥饿
    // t513 胡萝卜 / 土豆（机制等价 MC 1.0 carrot +3 / potato +1 hunger）。数值简化：烤土豆另算 MC +5
    //   本工程无烤土豆物品故土豆取生食量 1。新增可食作物只在此追加一行（单一权威）。
    if (itemId == RecipeRegistry::CarrotId)       return 3; // 胡萝卜 +3 饥饿（机制等价 MC 1.0 carrot）
    if (itemId == RecipeRegistry::PotatoId)       return 1; // 生土豆 +1 饥饿（机制等价 MC 1.0 raw potato）
    // t669 毒马铃薯：机制等价 MC 1.0 poisonous potato —— 可食 +2 饥饿，但 60% 概率触发食物中毒
    //   （finishEating 掷骰 → m_poisonTimer 8s：每秒 -1 饥饿 + -1 HP）。只算饥饿值（中毒副作用在进食完成分支）。
    if (itemId == RecipeRegistry::PoisonousPotatoId) return kPoisonousPotatoHunger; // 毒马铃薯 +2 饥饿（60% 中毒）
    // t513 生/熟肉（机制等价 MC 1.0 各肉类恢复量；spec「生猪肉/生牛肉/熟肉都吃不了」修）。数值与 MC 1.0 一致：
    //   生肉低（猪/牛 +3、鸡 +2、鱼 +2），熟肉高（猪/牛 +8、鸡/羊 +6）。生鸡肉 MC 另带 30% 食物中毒 ——
    //   本工程无 status-effect 系统故仅取饥饿值（中毒暂未实现，待后续任务加状态系统时补）。
    //   熟羊肉（CookedMuttonId）仅熟变体（羊燃烧致死掉落，无生羊肉物品）。生鱼（RawFishId）= MC 1.0 raw fish +2。
    if (itemId == RecipeRegistry::RawPorkchopId)  return 3; // 生猪排 +3 hunger（MC 1.0 raw porkchop）
    if (itemId == RecipeRegistry::RawBeefId)      return 3; // 生牛肉 +3 hunger（MC 1.0 raw beef）
    if (itemId == RecipeRegistry::RawChickenId)   return 2; // 生鸡肉 +2 hunger（MC raw chicken；未实现 30% 中毒）
    if (itemId == RecipeRegistry::RawFishId)      return 2; // 生鱼 +2 hunger（MC 1.0 raw fish）
    if (itemId == RecipeRegistry::CookedPorkchopId) return 8; // 熟猪排 +8 hunger（MC 1.0 cooked porkchop）
    if (itemId == RecipeRegistry::CookedBeefId)     return 8; // 熟牛肉 +8 hunger（MC 1.0 cooked beef / steak）
    if (itemId == RecipeRegistry::CookedMuttonId)   return 6; // 熟羊肉 +6 hunger（MC cooked mutton）
    if (itemId == RecipeRegistry::CookedChickenId)  return 6; // 熟鸡肉 +6 hunger（MC 1.0 cooked chicken）
    return 0;
}

PlayerController::PlayerController(QQuickItem *parent) : QQuickItem(parent)
{
    connect(this, &QQuickItem::windowChanged, this, &PlayerController::onWindowChanged);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(16); // ~60Hz：鼠标视角 + 物理
    connect(&m_timer, &QTimer::timeout, this, &PlayerController::tick);
}

void PlayerController::componentComplete()
{
    QQuickItem::componentComplete();
    if (!m_window && (m_window = window()))
        m_window->installEventFilter(this); // 拦截 Esc + 失焦
    snapSpawnToGround(); // t137：世界就绪 → 贴地表（覆盖 kSpawnY=80 兜底，消除出生落差摔伤）
    m_peakY = m_pos.y(); // 掉落伤害基准：以脚底初始 Y 起（首帧不误判大落差；snap 已设则等价）
    m_clock.start();
    m_evtClock.start();
    m_timer.start();
}

void PlayerController::setWorld(World *w)
{
    if (m_world == w) return;
    m_world = w;
    m_dispenserCooldowns.clear(); // t486：换世界清发射器冷却（防跨世界同坐标串扰；冷却键按世界坐标打包）
    m_redstoneLitCells.clear();   // t569：换世界清红石矿点亮表（防跨世界同坐标串扰；键按世界坐标打包）
    m_platePressedCells.clear();  // t627：换世界清压力板触发态（防跨世界同坐标串扰；键按世界坐标打包）
    m_plateJustPressed.clear();   // t627：同上（沿表生命周期一帧，但换世界须一并清防陈旧沿触发）
    m_buttonRecoverCells.clear(); // t628：换世界清按钮自动复位表（防跨世界同坐标串扰；键按世界坐标打包，同 m_dispenserCooldowns）
    m_dispenserPoweredCells.clear(); // t689：换世界清机器电力基线集（防跨世界同坐标串扰；键同冷却编码）
    snapSpawnToGround(); // t137：世界注入后贴地表（构造期 m_pos=kSpawnY 兜底，此处覆盖为真实地表）
    emit worldChanged();
}

// t137 出生贴地表：查出生列 (kSpawnX,kSpawnZ) 的 worldgen 地表高度 → 脚底 Y = h+1（站地表方块上方），
//   同步 m_peakY 防误判落差。kSpawnY=80 是高于最高地表(~71，t307 后 hills 顶)的兜底初值（防卡地形），
//   但玩家从 80 摔到地表（落差 >3）会触发摔伤；本方法在世界就绪后把玩家贴真实地表，消除出生落差。分别
//   在 componentComplete / setWorld / respawn 调，确保世界（width/height/seed）定稿后玩家始终贴地表。
//   无世界 → no-op（m_pos 保持 kSpawnY 兜底）。分层（PLAN §2）：只读 World::heightAt（worldgen 地表纯
//   函数，同 generate() 填充用），不改栅格；Game 层向下读 World，无反向依赖。
void PlayerController::snapSpawnToGround()
{
    if (!m_world) return;
    // t388：按当前重生点列贴地表（m_spawnPos）。世界就绪时 m_spawnPos==kSpawn（初值，行为不变）；夜间睡床后
    //   m_spawnPos=床位，重生贴床顶（床是 solid 地表方块 → heightAt 返床 y，+1 站其上）。
    const int h = m_world->heightAt(int(m_spawnPos.x()), int(m_spawnPos.z()));
    m_pos.setY(float(h) + 1.0f); // 脚底 = 地表方块顶面（h 为地表方块 y，+1 站其上）
    m_peakY = m_pos.y();         // 掉落伤害基准重置（同 componentComplete / setMode 语义，防陈旧落差）
}

void PlayerController::setHotbar(Hotbar *h)
{
    if (m_hotbar == h) return;
    m_hotbar = h;
    emit hotbarChanged();
}

void PlayerController::setItemEntities(ItemEntityManager *m)
{
    if (m_itemEntities == m) return;
    m_itemEntities = m;
    emit itemEntitiesChanged();
}

// t95：注入统一实体管理器（QML 绑定）。同 setItemEntities 模式：仅记录指针 + 发信号（QML 注入 peer
// ViewModel，运行期连接、非编译期反向依赖；PLAN §2 分层）。PlayerController 是唯一同时持 World* +
// EntityManager* 的对象，故由 tick 驱动实体的重力 / 推动物理（实体物理态住在 EntityManager 内部数据）。
void PlayerController::setEntityManager(EntityManager *m)
{
    if (m_entityManager == m) return;
    m_entityManager = m;
    emit entityManagerChanged();
}

// t280 黑暗刷怪：注入 WorldClock（同 world/hotbar/itemEntities/entityManager 模式）。
//   读 m_worldClock->skyLight() 传给 EntityManager::tickHostileLife。null 时敌对生命周期早退（无昼夜 → 无 spawn）。
void PlayerController::setWorldClock(WorldClock *c)
{
    if (m_worldClock == c) return;
    m_worldClock = c;
    emit worldClockChanged();
}

// t402 注入经验球管理器（同 setItemEntities / setEntityManager 模式）：仅记录指针 + 发信号
//   （QML 注入 peer ViewModel，运行期连接、非编译期反向依赖；PLAN §2 分层）。tick 驱动见 tickImpl。
void PlayerController::setXpOrbManager(XpOrbManager *m)
{
    if (m_xpOrbManager == m) return;
    m_xpOrbManager = m;
    emit xpOrbManagerChanged();
}

// t469 船管理器注入（同 setXpOrbManager 模式）。
void PlayerController::setBoatManager(BoatManager *m)
{
    if (m_boatManager == m) return;
    m_boatManager = m;
    emit boatManagerChanged();
}

// t565 矿车管理器注入（同 setBoatManager 模式）。
void PlayerController::setMinecartManager(MinecartManager *m)
{
    if (m_minecartManager == m) return;
    m_minecartManager = m;
    emit minecartManagerChanged();
}

// t579 发射器内容存储注入（同 setMinecartManager 模式）。踩压力板触发发射器时读 per-block 9 槽
//   取首个可用槽内容物发射 + 扣库存；null 时发射器触发降级（无内容不发射，神殿陷阱默认箭路径不受影响）。
void PlayerController::setDispenserStore(DispenserStore *s)
{
    if (m_dispenserStore == s) return;
    m_dispenserStore = s;
    emit dispenserStoreChanged();
}

void PlayerController::onWindowChanged(QQuickWindow *win)
{
    if (m_window) m_window->removeEventFilter(this);
    m_window = win;
    if (m_window) m_window->installEventFilter(this);
    else {
        if (m_captured) release(); // 窗口销毁 → 安全释放
        cancelMining();            // 防御：清累积挖掘态（避免失窗口后仍在 tick）
    }
}

// ---- 输入 ----
void PlayerController::setKey(int key, bool pressed)
{
    // t655 死亡态输入闸门：m_dead 期间拒收一切游戏键（WASD / 跳 / 蹲 / 疾跑双击 / 双击空格切飞全部
    //   停摆；spec「死亡态锁移动/攻击/背包键，只接受重生按钮与聊天」）。QML keyInput 层有同款守卫（先
    //   拦），此处 C++ 侧兜底 —— 任何漏网透传路径（未来新增键位 / 面板）都不至于让尸体走动。release
    //   侧不拦：respawn 已 m_keys.clear 兜底，且正常松键透传无害（值只会被清向 false）。
    if (m_dead && pressed) return;
    const bool wasDown = m_keys.value(key);
    // 飞行门控（t21）：双击空格切飞仅 Creative（Spectator 常驻 noclip 无需切；Survival canFly()=false，
    // 永不进入此分支 → 双击空格不触发飞行，重力生效）。真实按下（非按住自动重复）→ 双击 ≤300ms 切换。
    if (key == Qt::Key_Space && pressed && !wasDown && m_mode == Creative) {
        const qint64 now = m_evtClock.elapsed();
        if (now - m_lastSpaceMs < 300) {
            m_flying = !m_flying;
            m_lastSpaceMs = -100000; // 防三连误触
            m_vel.setY(0);
            // t51：起飞退出疾跑/蹲下（飞行态 Walk 状态机不适用；spec「疾跑/蹲下仅走路模式」）。
            if (m_moveState != Walk) setMoveState(Walk);
            emit flyingChanged();
        } else {
            m_lastSpaceMs = now;
        }
    }
    // 移动状态机（t51）：双击 W 疾跑 / Shift 按住蹲下。仅走路模式（Survival / Creative-未飞）有效；
    // Spectator 与 Creative-飞态下 Shift 作「下降」用、W 无疾跑语义 → 不触发状态切换。
    // 优先级：Shift（蹲）覆盖 W（疾跑）—— 蹲下时按/松 W 不动蹲态，仅松 Shift 才回 Walk。
    //   双击 W 仅从 Walk 进 Sprint（蹲态不进）；松 W 退 Sprint；松 Shift 退 Crouch。
    //   真实按下（非自动重复）才参与双击检测（与 Space 同；QML 已过滤 autorepeat，此处 wasDown 再保险）。
    if (key == Qt::Key_Shift) {
        const bool canCrouch = (m_mode == Survival || (m_mode == Creative && !m_flying));
        if (pressed && canCrouch) {
            m_autoCrouch = false; // t559：主动按下 shift → 回到「用户主动蹲」（清自动蹲标记，保留正常蹲语义）
            if (m_moveState != Crouch) setMoveState(Crouch);
        } else if (!pressed) {
            // t559/t575 松 shift 站起：统一走 setMoveState 集中站起闸门 —— 头顶有站起空间（canStandUp）
            //   才真正站起；不足（1.5 格通道 / 低天花板）时闸门拒绝切换并标 m_autoCrouch（自动保持蹲），
            //   由 step 内每 tick 复探自动站。修「通道里松 shift 直接站 + 被挤出/穿墙」：不站就不会把
            //   1.8 AABB 塞进 1.5 通道（不嵌入 → extrudeEmbedded 不推）。
            if (m_moveState == Crouch) setMoveState(Walk);
        }
    } else if (key == Qt::Key_W) {
        const bool canSprint = (m_mode == Survival || (m_mode == Creative && !m_flying));
        if (pressed && !wasDown && canSprint) {
            const qint64 now = m_evtClock.elapsed();
            if (m_moveState == Walk && (now - m_lastWms < 250)) {
                setMoveState(Sprint);
                m_lastWms = -100000; // 防三连误触（双击成功后立即消费）
            } else if (m_moveState == Walk) {
                // 仅 Walk 态记「双击第一击候选」；Crouch/Sprint 态按 W 不进双击检测、不刷 m_lastWms。
                // 否则蹲态按 W 会留下时间戳，松 Shift 回 Walk 后单按 W（now - 旧戳 < 300）被误判双击 → 误触发疾跑。
                m_lastWms = now;
            }
        } else if (!pressed) {
            // 松 W 退出疾跑（spec「松 W 回 Walk」）。蹲态下松 W 不动（Shift 仍按 → 仍蹲）。
            if (m_moveState == Sprint) setMoveState(Walk);
        }
    }
    m_keys.insert(key, pressed);
}

// t655 死亡态不切模式（死亡屏下按 G 不该换观察者「逃出」死亡态；死亡闸门族同 grab/setKey/beginMining）。
void PlayerController::cycleMode() { if (m_dead) return; setMode(static_cast<Mode>((static_cast<int>(m_mode) + 1) % 3)); }

// F5 相机模式循环（t27）：第一人称 → 第三人称-后 → 第三人称-前 → 回第一人称（0→1→2→0）。
// 仅改标志 + 通知 QML（相机摆位在 Main.qml 据 cameraMode 算 position/eulerRotation）。
// t655 死亡态不循环（死亡屏下视角固定；同 cycleMode 死亡闸门族）。
void PlayerController::cycleCamera()
{
    if (m_dead) return;
    m_cameraMode = static_cast<CameraMode>((static_cast<int>(m_cameraMode) + 1) % 3);
    emit cameraModeChanged();
}

// t159/t210 飞行速度滚轮调速：dir=+1 加速 / -1 减速（前滚 / 后滚），按有效速度步进 kFlyStep。
//   有效速度 = clamp(kFly * mul, kFlyMin, kFlyMax) ∈ [4,20]；改有效速度再回算 mul（直接钳有效速度更直观，
//   spectator 常驻飞读 mul 算飞移速）。无变化静默（不发信号，免抖动）。
//   t210 起仅 QML 在 spectator 模式调用（创造/生存滚轮切 hotbar）；本方法本身不判模式 —— 输入边界（§2-D）
//   由 QML 把关。
void PlayerController::adjustFlySpeed(int dir)
{
    const float eff = std::clamp(kFly * m_flySpeedMul + float(dir) * kFlyStep, kFlyMin, kFlyMax);
    const float newMul = eff / kFly;
    if (newMul == m_flySpeedMul) return;
    m_flySpeedMul = newMul;
    emit flySpeedMulChanged();
}
void PlayerController::setMode(Mode m)
{
    if (m == m_mode) return;
    m_mode = m;
    m_vel = QVector3D(0, 0, 0); // 切模式清速度，避免半空切走生存被甩飞
    m_knockback = QVector3D(0, 0, 0); // t296 切模式清受击击退冲量（防陈旧冲量跨模式残留；noclip 模式不积分它）
    m_onGround = false;
    m_peakY = m_pos.y();        // 重置掉落基准：避免从创造飞行高度切生存时累计陈旧落差
    if (m_flying) { m_flying = false; emit flyingChanged(); } // 进入新模式默认走（不飞）
    // t51：切模式清移动状态机（疾跑/蹲下不跨模式延续；新模式的 Shift/W 上下文不同，从 Walk 起最稳）。
    if (m_moveState != Walk) setMoveState(Walk);
    m_lastWms = -100000; // t70：切模式清双击窗口脏残留（防切换后首按 W 被旧戳误判双击 → 误触发疾跑）
    emit modeChanged();
    emit onGroundChanged();
}

// ---- 指针锁定 ----
void PlayerController::grab()
{
    // t655 死亡态不捕获（单一闸门，最先判）：死亡时 onDied 已 release 交光标给死亡屏按钮；若任何呈现层
    //   路径（历史 bug：死亡屏下按 E 开背包再关包 → closeInventory 内 grab）在死亡态抢回 captured，
    //   tickImpl 恢复全路径 = 尸体走动 / 攻击 / 挖掘，且 PlayerState.takeDamage 对 dead 早退 → 永久无敌。
    //   在此拒绝 = 断根：死亡态下 captured 恒 false，物理 / 攻击 / 挖掘输入整层停摆，重生（respawn 清
    //   m_dead）后恢复。
    if (m_dead) return;
    if (m_captured || !m_window) return;
    setCaptured(true);
    QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor)); // 全局隐藏光标（最可靠；release 配对 restore）
    QCursor::setPos(windowCenterGlobal());                         // 先居中，首次 delta 从中心起算
}

// t78 重生定位：传回出生点 + 清速度 / 挖掘态 / 飞行 / 蹲下疾跑（spec「立即重生」的物理态复位部分）。
//   血量 / 死亡态由 PlayerState::respawn() 复位（呈现层按钮同时调两者；分层：定位属 Physics、数值属 Game）。
//   m_peakY 重置到出生点 Y → respawn 后下落从出生点起算（同 componentComplete 首帧，不误判陈旧落差致死）。
//   emit positionChanged → 相机绑定（眼位 + 第三人称偏移）重算跟随；feetPosition 同 NOTIFY 一并刷新。
void PlayerController::respawn()
{
    cancelMining();           // 清生存累积挖掘态（裂纹叠层随之隐）
    cancelEating();           // t267：清进食累积态（防 respawn 后 updateEating 误续食）
    cancelBowDraw();          // t304：清弓拉弓态（防 respawn 后误续拉）
    cancelFishing();          // t401：清钓鱼态（防 respawn 后误续钓；浮标作废）
    cancelSleep();            // t388：清睡觉 fade 态（重生即醒；防 respawn 后 updateSleep 误续睡）
    // t469：重生下船（清骑乘态；玩家离死亡点的船，船保留在世界）。outFeet 不用（重生用 m_spawnPos）。
    if (m_boatManager && m_boatManager->ridingIndex() >= 0) {
        QVector3D dummyFeet; m_boatManager->dismount(m_world, dummyFeet);
    }
    // t565：重生下矿车（清骑乘态；矿车保留在世界）。outFeet 不用（重生用 m_spawnPos）。
    if (m_minecartManager && m_minecartManager->ridingIndex() >= 0) {
        QVector3D dummyFeet; m_minecartManager->dismount(m_world, dummyFeet);
    }
    m_leftDown = false;       // 清左键按下态（防 respawn 后 updateMining 误续挖）
    m_rightDown = false;      // t267：清右键按下态（防 respawn 后 updateEating 误续食）
    m_dead = false;           // t175：清死亡态镜像 → pickupScan 恢复（重生后玩家已离开死亡点，可正常拾取）
    m_golemLaunchTimer = 0.0f; // t655 重生清击飞归属窗口（新生命周期不继承死亡前的击飞态）
    // t202：重生回满气泡 + 清三计时器（出生点在水外 → 满气起算；PlayerState::respawn 同步 air 到 maxAir）。
    if (m_air != kMaxAir) { m_air = kMaxAir; emit airUpdated(m_air); }
    m_airTimer = 0.0f;
    m_drownTimer = 0.0f;
    m_airRegenTimer = 0.0f;
    // t238：重生回满饥饿 + 清三计时器（PlayerState::respawn 同步 hunger 到 maxHunger）。
    if (m_hunger != kMaxHunger) { m_hunger = kMaxHunger; emit hungerUpdated(m_hunger); }
    m_hungerDepleteAccum = 0.0f;
    m_starveTimer = 0.0f;
    m_regenTimer = 0.0f;
    // t344：重生清火烧态（出生点在岩浆外 → 不带火重生；翻 m_burning 才 emit，防陈旧火焰叠层残留）。
    m_fireTimer = 0.0f;
    m_fireDmgTimer = 0.0f;
    if (m_burning) { m_burning = false; emit burningChanged(); }
    // t690：重生清毒态（同火烧 —— 机制等价 MC 死亡 / 重生清除全部状态效果；漏清则死亡前的 8s 毒跨
    //   重生继续每秒扣饥饿 / 扣血，「满血重生后不明掉血」）。
    m_poisonTimer = 0.0f;
    m_poisonDmgAccum = 0.0f;
    // t715：重生清缓慢态 + 效果快照缓存（下一 tick 广播空列表 → PlayerState 容器同步清空，效果栏隐）。
    m_slowTimer = 0.0f;
    m_slowLevel = 0;
    m_lastEffectSigCache.clear();
    m_pos = m_spawnPos; // t388：回当前重生点（初值=kSpawn；睡床后=床位）。Y 由 snapSpawnToGround 贴地表。
    m_vel = QVector3D(0, 0, 0);
    m_knockback = QVector3D(0, 0, 0); // t296：清受击击退冲量（重生不继承死亡点的击退）
    snapSpawnToGround();      // t137：重生贴地表（消除 kSpawnY 兜底落差；设 m_pos.y + m_peakY）
    if (m_flying) { m_flying = false; emit flyingChanged(); }
    setMoveState(Walk, true); // 蹲下 / 疾跑归 Walk（重生强制站：位置已摆出生点，闸门无意义）；同时复位 AABB 高 / 眼位
    emit positionChanged();   // 相机 / 第三人称模型跟随刷新
}

// t176 存档加载：恢复玩家位姿 + 模式。清物理瞬态（速度 / 挖掘 / 飞行 / 蹲疾跑）+ m_peakY 重置到存档 Y
//   （防「存档点到首次重力 tick」误判落差摔伤）。mode 序数 → Mode；越界守 0（Spectator，无伤兜底）。
void PlayerController::loadSavedState(float x, float y, float z, float yaw, float pitch, int mode)
{
    cancelMining();
    cancelEating(); // t267：清进食累积态（防加载后 updateEating 误续食）
    cancelBowDraw(); // t304：清弓拉弓态（防加载后误续拉）
    cancelFishing(); // t401：清钓鱼态（存档不持久化钓鱼；防加载后误续钓）
    cancelSleep();   // t388：清睡觉 fade 态（存档不持久化睡觉；防加载后误续睡）
    m_leftDown = false;
    m_rightDown = false; // t267：清右键按下态（防加载后 updateEating 误续食）
    m_dead = false;
    m_golemLaunchTimer = 0.0f; // t655 存档加载清击飞归属窗口（瞬态值，不跨世界；同 m_knockback 归零语义）
    m_pos = QVector3D(x, y, z);
    m_vel = QVector3D(0, 0, 0);
    m_knockback = QVector3D(0, 0, 0); // t296：清受击击退冲量（瞬态值，存档不持久化；防上一世界残留）
    m_yaw = yaw;
    m_pitch = pitch;
    m_peakY = y; // 重置掉落基准（存档点起算，不误判陈旧落差）
    // t202：存档不持久化 air（瞬态值，机制同速度 / 挖掘态；出水即回满）→ 加载回满气泡 + 清三计时器。
    //   emit airUpdated 强制同步 PlayerState.air（防上一世界溺水残留低气值带进新世界显陈旧气泡条）。
    m_air = kMaxAir;
    emit airUpdated(m_air);
    m_airTimer = 0.0f;
    m_drownTimer = 0.0f;
    m_airRegenTimer = 0.0f;
    // t238：饥饿计时器随存档加载归零（避免上一世界残留扣血 / 回血累积跨世界串入）。m_hunger 本身不在此
    //   复位 —— 由 Main.qml::applyPlayerState 经 player.setHunger(data.hunger) 单独灌入存档值（存档持久化
    //   playerState.hunger；spec「到 0→扣血」从存档值起算）。setHunger 不被本方法调，故此处清计时器即可。
    m_hungerDepleteAccum = 0.0f;
    m_starveTimer = 0.0f;
    m_regenTimer = 0.0f;
    // t344：存档加载清火烧态（瞬态值，不持久化；防上一世界火残留带进新世界显陈旧火焰叠层）。
    m_fireTimer = 0.0f;
    m_fireDmgTimer = 0.0f;
    if (m_burning) { m_burning = false; emit burningChanged(); }
    // t690：存档加载清毒态（同火烧瞬态语义 —— 中毒不持久化，防上一世界残留毒跨世界每秒扣饥饿 / 扣血）。
    m_poisonTimer = 0.0f;
    m_poisonDmgAccum = 0.0f;
    // t715：存档加载清缓慢态 + 效果快照缓存（效果不持久化；下一 tick 广播空列表清 PlayerState 容器）。
    m_slowTimer = 0.0f;
    m_slowLevel = 0;
    m_lastEffectSigCache.clear();
    if (m_flying) { m_flying = false; emit flyingChanged(); }
    if (m_moveState != Walk) setMoveState(Walk, true); // t574/t575 存档加载强制站（位姿已灌新位，闸门无意义）
    const Mode target = (mode == int(Survival)) ? Survival
                       : (mode == int(Creative)) ? Creative : Spectator;
    if (target != m_mode) { m_mode = target; emit modeChanged(); }
    emit positionChanged();
    emit yawChanged();
    emit pitchChanged();
}

// r2-B1 存档加载机关态收尾（Main.qml enterWorld 在 applyPlayerState 后调；见头注释）。读档复用同一
//   theWorld / player 对象 → setWorld 不触发（m_world 指针未变）→ 机关瞬态表按世界坐标打包的键会残留
//   上一局数据（同坐标串扰）。在此统一清 + 置压力板沿一次性抑制标记（首个 updatePressurePlates tick
//   只建基线不产沿）。分层（PLAN §2）：只清本类瞬态表 + 置标记，不写栅格。
void PlayerController::finishWorldLoad()
{
    m_dispenserCooldowns.clear();
    m_dispenserPoweredCells.clear(); // t689：机器电力基线集同清（读档残留基线会吞掉通电沿——存档通电机器首沿不触发）
    m_redstoneLitCells.clear();
    m_platePressedCells.clear();
    m_plateJustPressed.clear();
    m_buttonRecoverCells.clear();
    m_plateBaselineSkipNext = true; // 首 tick 建基线不产沿（防读档踩板误触发陷阱）
}

void PlayerController::release()
{
    if (!m_captured) return;
    QGuiApplication::restoreOverrideCursor(); // 恢复光标 = 可点暂停菜单
    m_keys.clear();                           // 丢弃按住的 WASD，防恢复时前冲
    setCaptured(false);
    clearHit();                               // 暂停 → 隐藏线框（未捕获时不选中）
    cancelMining();                           // t34：暂停 / 失焦 → 清累积挖掘态（spec：失焦清零）
    cancelEating();                           // t267：暂停 / 失焦 → 清进食累积态（spec：失焦清零）
    cancelBowDraw();                          // t304：暂停 / 失焦 → 清弓拉弓态（spec：失焦清零）
    cancelFishing();                          // t401：暂停 / 失焦 → 收浮标（spec：失焦清零）
    cancelSleep();                            // t388：暂停 / 失焦 → 中断睡觉 fade（未跳清晨即醒）
    m_leftDown = false;                       // t44：暂停 / 失焦 → 视同松手（切断续挖）
    m_rightDown = false;                      // t267：暂停 / 失焦 → 视同松手（切断连食）
    // t51：暂停 / 失焦时退出疾跑 / 蹲下（恢复时从 Walk 起；避免遗留蹲态卡低视角 / 疾跑余速）。
    // t574：蹲态下开背包（release）经 setMoveState 站起闸门 —— 头顶不足（1.5 格通道）时拒绝站起并标
    //   m_autoCrouch（关包 grab 后仍保持蹲，头不卡方块；走出低顶区才自动站）。暂停叠层同理不破蹲约束。
    // t70：同时清双击窗口脏残留（防暂停恢复后首按 W 被旧戳误判双击 → 误触发疾跑）。
    if (m_moveState != Walk) setMoveState(Walk);
    m_lastWms = -100000;
}

void PlayerController::setCaptured(bool c)
{
    if (m_captured == c) return;
    m_captured = c;
    emit capturedChanged();
}

QPoint PlayerController::windowCenterGlobal() const
{
    if (!m_window) return QCursor::pos();
    return m_window->mapToGlobal(QPoint(m_window->width() / 2, m_window->height() / 2));
}

void PlayerController::pollMouse()
{
    if (!m_window) return;
    const QPoint c = windowCenterGlobal();
    const QPoint p = QCursor::pos();
    const int dx = p.x() - c.x(), dy = p.y() - c.y();
    if (dx || dy) {
        m_yaw -= dx * kSens; // 鼠标左移 → 视角左转
        m_pitch = std::clamp(m_pitch - dy * kSens, -89.0f, 89.0f); // 屏幕Y下→上抬；夹±89防翻转
        emit yawChanged();
        emit pitchChanged();
        emit lookChanged(); // 视线方向随 yaw/pitch 变（第三人称相机偏移绑定刷新，t27）
        QCursor::setPos(c); // 每帧回中 → 永不撞屏边（无限旋转）
    }
}

bool PlayerController::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_window) {
        if (e->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(e);
            if (k->key() == Qt::Key_Escape && m_captured) { release(); return true; }
        } else if (e->type() == QEvent::WindowDeactivate || e->type() == QEvent::FocusOut) {
            if (m_captured) release(); // 切走绝不留「锁住的光标」
        } else if (e->type() == QEvent::MouseButtonPress) {
            // 破/放（t05 + t34）：仅指针捕获时由窗口级事件过滤接管（未捕获时不消费 → 让暂停层 grab）。
            // 走事件过滤而非 MouseArea —— 指针锁定下光标每帧被 warp 回中，MouseArea 依赖的指针
            // 位置不可靠；窗口级 MouseButtonPress 与光标位置无关，最稳。
            // t34：左键按下走 beginMining（创造瞬破 / 生存开始累积），不再单击直破 —— 由 beginMining
            // 内按模式分流（spec：创造单击瞬破 = 单次边缘；生存 = 按住累积）。
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_captured) {
                // t457 睡觉 Settled 阶段（全黑 + 起床按钮）：左键 / 右键点击 → 平滑起床（wakeUpFromBed，非瞬切）。
                //   spec「中间显起床按钮→按则立即醒」。屏幕全黑光标隐藏 → 玩家任意点击即醒（按钮居中作视觉提示）。
                //   非 Settled 阶段（Lying 渐黑 / Waking 渐显）点击忽略（防误打断过渡动画）；受惊醒走 wakeUp 瞬切。
                if (m_sleeping && m_sleepPhase == kSleepPhaseSettled
                    && (me->button() == Qt::LeftButton || me->button() == Qt::RightButton)) {
                    wakeUpFromBed();
                    return true;
                }
                if (me->button() == Qt::LeftButton)   { beginMining(); return true; }
                // t267：手持面包 → 右键**按住**进食（不再单击即食；spec「单击即食→改长按右键」）。
                //   持物判据直读 hotbar（单一权威，同 updateMining / placeBlock 的 t57/t186 修法，免 QML
                //   绑定滞后窗口）。面包走 beginEating 累积进度路径，不进 placeBlock（placeBlock 内面包
                //   分支已移除）。其它持物（方块 / 桶 / 锄 / 种子 / 蛋 / 工具）仍走 placeBlock 单击路径。
                if (me->button() == Qt::RightButton)  {
                    const int heldForEat = m_hotbar ? m_hotbar->selectedItemId() : 0;
                    // t514 甜浆果种植优先（spec「持甜浆果右键草地 / 泥土 → 种植浆果丛」）：甜浆果既是食物又是种植材料。
                    //   MC 1.0 「使用方块」优先于「使用物品」—— 右键**命中草地 / 泥土**时走种植（placeBlock 路由到浆果丛
                    //   种植分支），不进进食；瞄空气 / 非草地泥土（如石头 / 沙）→ 回退进食（持浆果按住右键吃）。判据复用
                    //   m_hasHit（上一帧 updateRaycast 结果）+ 读命中格 id。种植物品非方块 → placeBlock 内 selectedBlock 归
                    //   Air，由 SweetBerryId 分流分支落地 SweetBerryBush（不走方块放置主路径）。
                    if (heldForEat == RecipeRegistry::SweetBerryId && m_world && m_hasHit) {
                        const quint8 hbId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
                        if (hbId == BlockRegistry::Grass || hbId == BlockRegistry::Dirt) {
                            placeBlock(); // → SweetBerryId 分支种植 SweetBerryBush state=0
                            return true;
                        }
                    }
                    // t639① 胡萝卜/马铃薯种植优先（spec「手持胡萝卜/马铃薯右键耕地 → 种植」）：胡萝卜/马铃薯
                    //   既是食物又是种植材料（MC 1.0 胡萝卜/马铃薯物品本身即种子）。**在进食拦截之前**分流——
                    //   右键命中耕地时走种植（placeBlock 路由到 useBlock 的 kCropSeeds 种植分支），不进进食；
                    //   瞄空气 / 非耕地 → 回退进食（持胡萝卜/马铃薯按住右键吃）。判据复用 m_hasHit（上一帧
                    //   updateRaycast 结果）+ 读命中格 id == Farmland（同 t514 甜浆果「使用方块优先于使用物品」模式）。
                    //   种植物品非方块 → placeBlock 内 selectedBlock 归 Air，由 kCropSeeds 分流分支落地
                    //   CarrotCrop/PotatoCrop（不走方块放置主路径）。
                    if ((heldForEat == RecipeRegistry::CarrotId || heldForEat == RecipeRegistry::PotatoId)
                        && m_world && m_hasHit
                        && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Farmland) {
                        placeBlock(); // → kCropSeeds 种植分支（胡萝卜 / 马铃薯）
                        return true;
                    }
                    // t267：手持食物（面包 / 甜浆果）→ 右键**按住**进食（不再单击即食；spec「单击即食→改长按右键」）。
                    //   t467：经 foodHungerAmount 单一权威判「是否食物」，新增食物只改本判定一处（避免各处硬编码 BreadId）。
                    if (foodHungerAmount(heldForEat) > 0) { beginEating(); return true; }
                    // t304 手持弓 → 右键长按拉弓（不进 placeBlock；弓非方块，selectedBlock 已守 Air）。机制等价
                    //   MC 1.0 右键拉弓。持物判据直读 hotbar（单一权威，免 QML 绑定滞后窗口，同面包 / 桶修法）。
                    if (heldForEat == int(ToolRegistry::Bow)) { beginBowDraw(); return true; }
                    // t401 手持钓鱼竿 → 右键抛 / 拉切换（不进 placeBlock；钓竿非方块，selectedBlock 已守 Air）。机制
                    //   等价 MC 1.0 右键钓竿抛 / 收（单次切换，非长按）。持物判据直读 hotbar（单一权威，同面包 / 弓修法）。
                    if (heldForEat == int(ToolRegistry::FishingRod)) { useFishingRod(); return true; }
                    // t377 手持护甲 → 右键装备 / 互换（不进 placeBlock；护甲非方块，selectedBlock 已守 Air）。
                    //   equipSelectedArmor 自判 isArmor，非护甲返 false → 回退 placeBlock。机制等价 MC 1.0 右键装备。
                    if (m_hotbar && m_hotbar->equipSelectedArmor()) return true;
                    placeBlock();
                    return true;
                }
                if (me->button() == Qt::MiddleButton) { pickBlock();   return true; } // t37 pick block
            }
        } else if (e->type() == QEvent::MouseButtonRelease) {
            // t34：左键松开 → 清生存累积进度（创造不进入累积态，endMining 内 no-op）。
            // t267：右键松开 → 清进食累积进度（未完成不消耗；非进食态 endEating 内 no-op）。
            // 仍只在捕获时消费（与 press 对称；未捕获时 release 不应破坏其它层的光标交互）。
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_captured && me->button() == Qt::LeftButton) {
                endMining();
                return true;
            }
            if (m_captured && me->button() == Qt::RightButton) {
                // t267 面包松开清进食；t304 弓松开射箭（二者持物互斥，安全都调）。
                endEating();
                endBowDraw();
                return true;
            }
        }
    }
    return QQuickItem::eventFilter(o, e);
}

// ---- 主循环 ----
// t178：tick() 包一层 CPU 耗时计时（PLAN §4 帧时间切分）。每 tick 累加主线程耗时，每 ~60 tick（≈1s@60Hz）
//   算平均写 m_simMs 并 emit perfChanged → F3 叠层重绑显示「cpu sim: X.XXms」。实体 / 物理逻辑在 tickImpl。
void PlayerController::tick()
{
    QElapsedTimer pt;
    pt.start();
    tickImpl();
    m_simAccumNs += pt.nsecsElapsed();
    if (++m_simTickCount >= 60) { // ≈1s 窗口（timer 16ms ≈ 62.5Hz → 60 tick ≈ 0.96s）
        m_simMs = float(double(m_simAccumNs) / 1e6 / double(m_simTickCount)); // ns → ms 均
        m_simAccumNs = 0;
        m_simTickCount = 0;
        emit perfChanged();
        // perf：每 ~1s flush 帧时间分解（FrameProfiler 各桶 → 报告 → F3 / 日志）。
        FrameProfiler::instance()->flush();
    }
}

void PlayerController::tickImpl()
{
    const qreal dt = qMin(m_clock.restart() / 1000.0, 0.05); // 钳 50ms，防卡顿后穿墙
    // perf：本帧计入 FrameProfiler 窗口（flush 时除以它得逐帧 ms）。
    FrameProfiler::instance()->tickFrame();
    // perf「env」桶：每帧环境探测（攻击冷却 + 眼/脚位水岩浆态 + 近流水/岩浆 proximity 扫描）。
    { FrameProfiler::Scope profEnv("env");
    // t248 攻击冷却递减（独立于捕获态 —— 菜单 / 背包开时也应自然走完，复击不卡陈旧值）。钳到 0。
    if (m_attackCooldown > 0.0f) {
        m_attackCooldown -= float(dt);
        if (m_attackCooldown < 0.0f) m_attackCooldown = 0.0f;
    }
    // t655 击飞摔死归属窗口递减（独立于捕获态 —— 上抛滞空期间开背包 / 暂停不影响窗口流逝；窗口语义
    //   =「从上抛起 5s」，非游戏输入态）。钳到 0。
    if (m_golemLaunchTimer > 0.0f) {
        m_golemLaunchTimer -= float(dt);
        if (m_golemLaunchTimer < 0.0f) m_golemLaunchTimer = 0.0f;
    }
    // t201 水下蓝滤镜：每 tick 重算眼位水态，翻转才 emit（避免每帧抖 QML 绑定）。放在 !m_captured
    //   早 return 之前 → 暂停 / 背包开 / 失焦时仍刷新（玩家可能停在水里打开背包，蓝雾应持续显）。
    //   仅读 World::blockAt（向下依赖，不改栅格）；无世界时 eyeInWater() 返 false。
    const bool inWater = eyeInWater();
    if (inWater != m_eyeInWater) {
        m_eyeInWater = inWater;
        emit eyeInWaterChanged();
    }
    // t351 眼位岩浆态（驱动岩浆橙雾叠层）：每 tick 重算，翻转才 emit（同 eyeInWater 模式）。放在 !m_captured
    //   早 return 之前 → 暂停 / 背包开 / 失焦时仍刷新。仅读 World::blockAt（向下依赖）；无世界时 eyeInLava() 返 false。
    const bool inLava = eyeInLava();
    if (inLava != m_eyeInLava) {
        m_eyeInLava = inLava;
        emit eyeInLavaChanged();
    }
    // t269 脚位水态（驱动水中走路声分流）：每 tick 重算，翻转才 emit（同 eyeInWater 模式）。放在 !m_captured
    //   早 return 之前 → 暂停 / 背包开 / 失焦时仍刷新（玩家停水里开背包，关包后迈步仍应水声）。仅读 World::blockAt
    //   （向下依赖）；无世界时 feetInWater() 返 false。
    const bool finWater = feetInWater();
    if (finWater != m_feetInWater) {
        m_feetInWater = finWater;
        emit feetInWaterChanged();
    }
    // t223 近流水 proximity 水流声：节流扫描（每 kFlowScanInterval 秒一次）算最近流水格距离 → level。
    //   放在 !m_captured 早 return 之前 → 暂停 / 背包开时仍刷新（玩家停流水旁开背包，水流声应持续）；
    //   退出世界 / 回菜单由 Main.qml 显式 stopWaterFlow（同 stopAmbient），且离开流水范围 level→0 自动停。
    m_flowScanTimer += float(dt);
    if (m_flowScanTimer >= kFlowScanInterval) {
        m_flowScanTimer = 0.0f;
        const float level = scanFlowSoundLevel();
        // 值真变才 emit（>epsilon 或 0<->非0 翻转）；免每 scan 抖 QML 绑定 + AudioManager 无谓 setLevel。
        if (qAbs(level - m_flowSoundLevel) > 0.02f
            || (level <= 0.0f) != (m_flowSoundLevel <= 0.0f)) {
            m_flowSoundLevel = level;
            emit flowSoundLevelChanged();
        }
        // t343 近岩浆 proximity 岩浆流声：同 scan 节流算最近岩浆格（源 / 流皆算——岩浆湖多为源，rumble 应近任何岩浆
        //   响起）距离 → level。Main.qml Connections 据此 start/stop AudioManager 岩浆声 + setLavaFlowLevel。
        const float llevel = scanLavaSoundLevel();
        if (qAbs(llevel - m_lavaSoundLevel) > 0.02f
            || (llevel <= 0.0f) != (m_lavaSoundLevel <= 0.0f)) {
            m_lavaSoundLevel = llevel;
            emit lavaSoundLevelChanged();
        }
    }
    } // /profEnv
    // perf「item/xp/boat/mob」桶：实体 tick（每帧常开，与捕获态无关 —— 菜单 / 暂停时仍推进）。
    //   高水位 slot-reuse（ItemEntityManager kCap=200 / EntityManager 64 / XpOrb 64）→ 即便活体少，
    //   vector::size() 停在高水位，每帧遍历所有槽跳空 —— 「段数无关 + 每帧固定」开销头号怀疑项。
    { FrameProfiler::Scope s("item");
    // t60：掉落物重力（世界模拟，独立于玩家捕获态——菜单 / 暂停时实体仍落到地面）。
    // PlayerController 是唯一同时持 World* + ItemEntityManager* 的对象，故由此驱动；实体物理态
    // （vy / resting）与 pos 同住在 ItemEntityManager 内部数据里（分层：Entities→World 向下只读）。
    if (m_itemEntities && m_world) m_itemEntities->tick(dt, m_world);
    } // /profItem
    { FrameProfiler::Scope s("xp");
    // t402 经验球磁吸 + 拾取（同掉落物 tick 常开 —— 菜单 / 暂停时球仍向玩家飞，世界模拟连续）。
    //   playerCenter = 玩家 AABB 中心（脚底 m_pos + 半高），磁吸 / 拾取都相对此点。无 World 依赖
    //   （经验球是纯磁吸实体）。拾取经 XpOrbManager::xpPickedUp 语义信号 → 呈现层路由 PlayerState.addXp。
    //   t641 死亡门控：死亡态（m_dead=true，dropAllItems 置位，早于 onDied 里 spawnOrb）不 tick →
    //   onDied 掉在死亡点的经验球不被**尸体**瞬间吸走（尸体停死亡点，球在磁吸半径 6 内 1s 内飞到被吸 →
    //   addXp 把 t443 致死分支刚清空的 XP 条又填回来 = 用户「死后复活经验条没清空」根因；与
    //   pickupScan 的 m_dead 门控同族 —— 玩家尸体不自动捡东西）。复生后（respawn 清 m_dead）tick 恢复，
    //   玩家走回死亡点可回收该球（机制等价 MC 死亡掉经验、走回拾取）。死亡期间球位置冻结（不磁吸 /
    //   不拾取 / 不老化），复生后 despawnExpired 按墙钟补齐寿命，无累积。
    if (!m_dead && m_xpOrbManager) m_xpOrbManager->tick(dt, m_pos + QVector3D(0.0f, m_height * 0.5f, 0.0f));
    } // /profXp
    { FrameProfiler::Scope s("boat");
    // t469 船浮水 tick（常开，独立于捕获态——菜单 / 暂停时船仍浮水 / 衰减，世界模拟连续，同掉落物 / mob tick）。
    //   被骑船的「操控位移」由 step() 骑乘分支的 tickRiddenBoat 推进（骑乘期 WASD 驱动）；本 tick 只做浮水 +
    //   空船摩擦衰减。PlayerController 是唯一同时持 World* + BoatManager* 的对象，故由此驱动（同 itemEntities / entityManager）。
    //   t508 pushable：每帧把玩家 AABB 中心（脚底 + 半高）灌进 BoatManager，tick 内据此判玩家 ↔ 船重叠 → 推船。
    if (m_boatManager && m_world) {
        m_boatManager->setPlayerCenter(m_pos + QVector3D(0.0f, m_height * 0.5f, 0.0f));
        m_boatManager->tick(dt, m_world);
    }
    } // /profBoat
    { FrameProfiler::Scope s("mob");
    // t95：统一实体（测试生物）重力 + 地面静止，同掉落物常开（菜单 / 暂停时仍模拟）。机制同源
    // （EntityManager::tick 向下只读 World::isSolid）。PlayerController 现亦持 EntityManager* → 由它驱动。
    //   t250：传 m_pos 作听者位置，门控 mob idle/step 叫声（近 mob 才发声；菜单态 m_pos 仍有效）。
    //   t283：传 kHalfW + m_height（当前 AABB 半宽 / 高，蹲下随 m_height 缩小）→ EntityManager Arrow 分支
    //   据它判箭命中玩家 AABB（蹲下命中盒正确收缩）。
    //   t290 观察者交互门控：传 playerTargetable = (m_mode == Survival) → 敌对 Mob 仅仇生存玩家（创造/观察者
    //   不被 detect/chase/attack/shoot；箭亦不命中）。Game 层持玩家模式并据此派生 bool 向下传（PLAN §2 向下依赖）。
    if (m_entityManager && m_world)
        m_entityManager->tick(dt, m_world, m_pos, kHalfW, m_height, m_mode == Survival);
    // t280 黑暗刷怪调度 + 敌对日光燃烧 + 远距消失（详见 EntityManager::tickHostileLife 头注释）。独立于玩家
    //   捕获态（菜单 / 暂停时仍推进 —— 夜晚照样刷怪、白天照样燃烧，世界模拟连续；同 entityManager.tick）。
    //   skyLight 取自 m_worldClock（Q_PROPERTY 注入；[0,1] 昼夜乘子）。m_worldClock=null → 跳过（无昼夜 → 无 spawn）。
    if (m_entityManager && m_world && m_worldClock)
        m_entityManager->tickHostileLife(dt, m_world, m_pos, m_worldClock->skyLight());
    // t392 刷怪笼周期刷怪（同 tickHostileLife 同级常开 —— 玩家在范围内时刷怪笼照样刷，世界模拟连续；
    //   菜单 / 暂停时仍推进，独立于捕获态）。无 m_worldClock 依赖（刷怪笼无视光照 / 昼夜，地牢天然黑暗）。
    if (m_entityManager && m_world)
        m_entityManager->tickSpawners(dt, m_world, m_pos);
    } // /profMob
    { FrameProfiler::Scope s("pickup");
    // t92：拾取扫描提到 m_captured 早 return **之前**——打开背包（release→m_captured=false）时
    // 原 pickupScan 落在早 return 之后永不执行，玩家走近掉落物拾不起（仅见实体掉地）。掉落物物理
    // （itemEntities->tick）本就在早 return 前跑（独立于捕获态），拾取与之同级、同样常开才一致。
    // 安全：pickupScan 内自检 m_itemEntities/m_hotbar 非空，t53 新生免拾取窗已在内部（0.5s 后才可拾）。
    pickupScan();
    // t323 嵌入箭近距拾取（与掉落物拾取同级常开：背包开 / 失焦时玩家仍可走近拾嵌入箭）。内自检
    //   m_entityManager/m_hotbar 非空 + 死亡 / 观察者门控，常开安全（同 pickupScan）。
    arrowPickupScan();
    // t627 压力板触发态更新（边沿触发单一权威）：先算本 tick 踩下沿集（玩家/mob/掉落物按权重压板 +
    //   踩下视觉 state bit），再供下方 scanTntTraps / scanDispenserTraps 消费（两者只对沿触发——踩一次
    //   fire 一次，持续踩着不重复；走开回位再踩再触发）。与拾取同级常开（掉落物落板即触发，独立于
    //   捕获态）。内自检；无板场景两表恒空（零开销）。
    updatePressurePlates();
    // t485 TNT 陷阱触发（踩压力板沿引爆）：只处理 updatePressurePlates 算出的本 tick 踩下沿。
    scanTntTraps();
    // t486 发射器陷阱触发（踩压力板沿 → 邻接发射器/投掷器 fire 一次）：与 TNT 陷阱同级常开；只处理本
    //   tick 踩下沿；per-dispenser 冷却防抖。dt 用于冷却递减。
    scanDispenserTraps(dt);
    // t569 红石矿石点亮触发（玩家走近红石矿即微弱红光，机制等价 MC 触发发光）：与拾取同级常开（玩家走过
    //   红石矿旁即触发，独立于捕获态）。内自检 + 死亡门控；点亮表到期自熄。dt 用于倒计时递减。
    scanRedstoneOre(dt);
    // t628 按钮自动复位（右键按下 ~1s 后弹回）：与拾取 / 红石矿同级常开（菜单 / 暂停时按钮仍按时弹回，
    //   世界模拟连续）。内自检；无按钮场景表恒空（零开销）。dt 用于倒计时递减。
    updateButtonRecovery(dt);
    } // /profPickup
    if (!m_captured) {
        cancelMining(); // 暂停（含背包开 / 失焦）：清累积挖掘态（spec：失焦清零）
        cancelEating(); // t267：暂停 / 失焦 → 清进食累积态（spec：失焦清零，未完成不消耗）
        cancelBowDraw(); // t304：暂停 / 失焦 → 清弓拉弓态（spec：失焦清零，未射出不消耗箭 / 耐久）
        cancelFishing(); // t401：暂停 / 失焦 → 收浮标（spec：失焦清零；浮标 / 倒计时作废）
        cancelSleep();   // t388：暂停 / 失焦 → 中断睡觉 fade（未跳清晨即醒）
        // t253：暂停 / 背包开时清 mob 目标框（updateRaycast 仅 captured 时跑 → 不清则残留旧目标）。
        if (m_targetedMob >= 0) { m_targetedMob = -1; emit targetedMobChanged(); }
        // t45：暂停时清行走动画驱动（moveSpeed→0；walkPhase 不动，QML 据此 sin*0=0 → 四肢归中性位）。
        // t159：同步清 speed（实际水平速度，暂停即 0；F3 报 0 而非陈旧值）。仅值真变时发，免每 tick 抖动。
        if (m_moveSpeed != 0.0f || m_horizSpeed != 0.0f) {
            m_moveSpeed = 0.0f; m_horizSpeed = 0.0f; emit moveSpeedChanged();
        }
        return;
    }
    // t457 睡觉期间冻结玩家（不移动 / 不挖 / 不吃 / 不射 / 不钓）——仅推进 fade 状态机。玩家整个睡觉序列
    //   保持 captured（不释放指针 → 光标隐藏，但屏幕渐黑不可见），点击唤醒经 eventFilter 接管（wakeUpFromBed）。
    //   跳过 step() → 无重力 / 无位移 = 玩家定格在床边（机制等价 MC 躺床期间玩家静止）；重力缺失 1-3s 无影响
    //   （玩家本就立于地面）。updateSleep 跑完即 return，避免下方 pollMouse/updateMining 等干扰睡眠过渡。
    if (m_sleeping) {
        { FrameProfiler::Scope profInput("input"); updateSleep(float(dt)); }
        return;
    }
    { FrameProfiler::Scope profPhys("phys");
    pollMouse();
    step(dt);
    // t95：玩家推动可推动实体（仅 captured/playing；玩家主动移动后才有位移可传给实体）。在 step() 解析
    // 完玩家与世界碰撞后调 —— 用已贴墙的玩家 AABB 做圆-vs-AABB 推解，把穿透量传给实体（swept 碰撞解析
    // 玩家位移传给实体，spec）。m_height 用当前 AABB 高（蹲下变矮 → 推动区间随之收，与碰撞同源）。
    if (m_entityManager) m_entityManager->resolvePlayerPush(m_pos, kHalfW, m_height, m_world);
    } // /profPhys
    { FrameProfiler::Scope profRay("ray");
    updateRaycast();   // 沿视线 DDA 选体 → 更新线框命中态
    updateCameraDistance(); // t40：第三人称相机距离钳制（防穿墙）
    } // /profRay
    { FrameProfiler::Scope profInput("input");
    updateMining(float(dt)); // t34：累积生存挖掘进度（创造不进入此态；无操作时早 return）
    updateEating(float(dt)); // t267：累积进食进度（持面包按住右键时；其它情况早 return）
    // t304 弓拉弓蓄力：持弓按住右键时累加 m_bowDrawTime（钳 kBowFullCharge）。换槽（持物不再是弓）→ cancel。
    //   仅 captured 时跑（pause 早 return 之前已清）。每 tick emit bowDrawChanged → QML 拉弓动画跟随进度。
    if (m_bowDrawing) {
        if (!m_hotbar || m_hotbar->selectedItemId() != int(ToolRegistry::Bow)) { cancelBowDraw(); }
        else {
            m_bowDrawTime = std::min(m_bowDrawTime + float(dt), kBowFullCharge);
            emit bowDrawChanged();
        }
    }
    updateFishing(float(dt)); // t401：累积咬钩倒计时 / 咬钩窗口（持钓竿抛出后；其它情况早 return）
    } // /profInput
}

// 视线方向：用与相机相同的欧拉→四元数（QQuaternion::fromEulerAngles(pitch,yaw,0)）旋转
// 相机本地 -Z，保证射线与渲染出的视线**完全同向**（不靠手写 pitch 符号约定，消除方向歧义）。
// pitch=0 时退化为水平前向 (-sin(yaw),0,-cos(yaw))，与 wishHoriz 一致。
QVector3D PlayerController::lookDirection() const
{
    const QQuaternion q = QQuaternion::fromEulerAngles(m_pitch, m_yaw, 0.0f);
    return q.rotatedVector(QVector3D(0.0f, 0.0f, -1.0f));
}

void PlayerController::updateRaycast()
{
    if (!m_world) { clearHit(); return; }
    // t184：选体射线纳入 Torch（HitTorch）—— 准星瞄火把即命中火把（可显示火把边界框 + 左键直挖），
    //   修正 t157「射线永远穿透火把」致火把不可直挖之缺陷（用户原意「火把可选可挖、空气可穿」）。
    //   Water 仍穿过（保 t165 水下可选中 / 挖实体）；相机距离（updateCameraDistance）走 HitPartial
    //   （t605：不完整方块按 sub-AABB 精确命中；火把 / 水均仍穿，non-solid 不拉近视距），故本处显式传
    //   HitTorch 仅作用于选体。
    // t501：同时纳入 Ladder（HitLadder）—— 机制同火把：木梯默认穿（玩家爬梯时准星瞄后方 / 邻格方块应
    //   选中方块本体，spec「爬梯时挖掘优先选中梯子 → 应像火把不优先选中、可透视穿过」）；仅当准星完全
    //   落在木梯视觉面（贴墙薄 quad 的精确 sub-AABB）时才命中木梯本身（可拆梯）。两标志位独立，故火把 / 木梯
    //   均在选体模式下「可选中、空气穿过」，与相机距离 / 桶射线的 Default / HitWater 互不干扰。
    const RayHit h = raycastVoxel(*m_world, position(), lookDirection(), kReach,
                                  RayFilter::HitTorch | RayFilter::HitLadder);

    // t212 命中点 Y（供 slab 上/下半放置 + 互补半合并判定，placeBlock 读）。lookDirection 已归一、dist 为起点
    //   到命中面欧氏距离（见 raycast.h）→ 命中点 = 眼位 + 视线*dist。**每帧刷新**（不随下方 changed 早退）：
    //   准星在同格内移动时格坐标/法线不变 → changed=false 跳过 emit，但命中点 Y 仍在变，placeBlock 点击瞬间
    //   需读最新值定半位，故须在早退之前写入。无命中 → 0（placeBlock 入口 m_hasHit 守卫已拦，不读此值）。
    m_hitPointY = h.valid ? (position().y() + lookDirection().y() * h.dist) : 0.0f;
    // t242：缓存命中距离（beginMining 攻击判定读它与 mob 命中距离比）。不随 changed 早退——同格内移动
    //   格坐标 / 法线不变但命中距离仍在变，点击瞬间需读最新值。无命中 = kReach（「无穷远」语义）。
    m_hitDist = h.valid ? h.dist : kReach;

    // t253 攻击单体选中：每帧缓存准星瞄准的**单个**最近活体 mob（findMobHit 已返单点最近，非 AoE 全打）。
    //   供 QML 目标框高亮（呈现层只读）。mob 须不晚于命中方块（mobDist<=m_hitDist）才算目标——方块挡在
    //   mob 前时该 mob 不算（机制等价 MC「方块遮挡视线 → mob 不可被瞄」）。不随 changed 早退：准星扫过 mob
    //   时背景方块格可能未变（changed=false）但目标 mob 已切换，须每帧重算。beginMining 攻击仍即时调
    //   findMobHit（点击瞬间最新视线），不读此缓存。
    int newTarget = -1;
    if (m_entityManager) {
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(position(), lookDirection(), kReach, &mobDist);
        if (mobIdx >= 0 && mobDist <= m_hitDist) newTarget = mobIdx;
    }
    if (newTarget != m_targetedMob) {
        m_targetedMob = newTarget;
        emit targetedMobChanged();
    }

    // 仅在命中态/格坐标/法线真正变化时 emit，避免每帧无谓刷新 QML 绑定。
    const bool changed = (h.valid != m_hasHit)
        || (h.valid && (h.bx != m_hitBx || h.by != m_hitBy || h.bz != m_hitBz
                        || qint32(h.nx) != m_hitNx || qint32(h.ny) != m_hitNy || qint32(h.nz) != m_hitNz));
    if (!changed) return;

    m_hasHit = h.valid;
    m_hitBx = h.bx; m_hitBy = h.by; m_hitBz = h.bz;
    m_hitNx = qint32(h.nx); m_hitNy = qint32(h.ny); m_hitNz = qint32(h.nz);
    emit hitChanged();
}

void PlayerController::clearHit()
{
    // t253：清目标 mob（无世界 / 退出 → 不应残留旧目标框；置前于 m_hasHit 早退——无方块命中但瞄着 mob
    //   时 m_hasHit 可能为 false，仍须清 mob 目标）。
    if (m_targetedMob >= 0) { m_targetedMob = -1; emit targetedMobChanged(); }
    if (!m_hasHit) return;
    m_hasHit = false;
    m_hitNx = m_hitNy = m_hitNz = 0;
    m_hitPointY = 0.0f; // t212：与命中态同步清零
    m_hitDist = kReach; // t242：与命中态同步重置为「无穷远」
    emit hitChanged();
}

// 第三人称相机距离钳制（t40）：每帧从眼位沿相机偏移方向 DDA，返回首个实体命中距离（留余量贴在面前）。
//   ThirdPersonBack：相机在玩家身后 → 偏移方向 = -look（射线往身后打）；
//   ThirdPersonFront：相机在玩家身前 → 偏移方向 = +look。
// 复用 raycastVoxel（RayHit.dist = 起点到命中面的欧氏距离）。命中 → dist - kCamMargin（贴面前、防
// z-fight / 近裁面穿插），floor 到 0（墙贴近眼 → 退到眼位）；无命中（含起点嵌实体的退化）→ kCamMax。
// t605：filter 由 Default 改 HitPartial —— 不完整方块（半砖 / 雪层 / 楼梯…）按碰撞 sub-AABB 精确命中，
//   修 1.5 格通道蹲行切第三人称相机穿墙：眼位落在天花板上半砖格的空气段（sub-AABB 外）时，Default 对该格
//   「进格即挡」→ 起点退化返 invalid → 相机取满距 3.5 穿墙查看。HitPartial 下起点格空气段穿过继续命中（本格
//   sub-AABB 或后方实体），相机正确钳到通道口外；三轴 3D 向量探测本就覆盖（射线沿偏移方向归一后 x/y/z 同测）。
//   Torch / Water / Ladder 仍穿（HitPartial 不含其 Hit* 标志，non-solid 不拉近视距，保 t40）。
// 第一人称恒 0（不偏移，相机贴眼）。仅值真变时发 cameraDistanceChanged——DDA 对同一 (eye,look,世界)
// 输入确定，玩家不动/不转时距离帧间稳定，无抖动。
// 分层（PLAN §2）：本层只读 World（blockAt），与选体 raycast 同源；相机摆位仍在 QML 呈现层。
void PlayerController::updateCameraDistance()
{
    if (m_cameraMode == FirstPerson) {
        if (m_cameraDistance != 0.0f) {
            m_cameraDistance = 0.0f;
            emit cameraDistanceChanged();
        }
        return;
    }
    float d = kCamMax;
    if (m_world) {
        const QVector3D look = lookDirection();
        const QVector3D dir = (m_cameraMode == ThirdPersonBack) ? -look : look;
        const RayHit h = raycastVoxel(*m_world, position(), dir, kCamMax, RayFilter::HitPartial);
        if (h.valid) {
            d = h.dist - kCamMargin;
            if (d < 0.0f) d = 0.0f; // 墙贴近眼位 → 退到眼（极端情形，相机近乎第一人称）
        }
    }
    if (d != m_cameraDistance) {
        m_cameraDistance = d;
        emit cameraDistanceChanged();
    }
}

// 命中面中心 = 方块中心 + (0.5 + eps)*法线：贴在该面上（非方块几何中心），
// eps 外推防与方块自身面 z-fight。
QVector3D PlayerController::hitFaceCenter() const
{
    constexpr float eps = 0.01f;
    return QVector3D(m_hitBx + 0.5f, m_hitBy + 0.5f, m_hitBz + 0.5f)
         + QVector3D(m_hitNx, m_hitNy, m_hitNz) * (0.5f + eps);
}

// 把规范线框（XY 平面、+Z 法线）摆到命中面：六种法线各对应一个欧拉角。
QVector3D PlayerController::hitFaceEuler() const
{
    if (m_hitNx > 0) return QVector3D(0, 90, 0);    // +X 面
    if (m_hitNx < 0) return QVector3D(0, -90, 0);   // -X 面
    if (m_hitNy > 0) return QVector3D(90, 0, 0);    // +Y（顶）面
    if (m_hitNy < 0) return QVector3D(-90, 0, 0);   // -Y（底）面
    if (m_hitNz < 0) return QVector3D(0, 180, 0);   // -Z 面
    return QVector3D(0, 0, 0);                      // +Z 面（与规范同向，不转）
}

// ---- 方块编辑（t05 + t34）----
// 编辑入口属 Game/Physics 层：输入 →（已有）射线命中 → World::setBlock。Renderer 不直接改栅格。
void PlayerController::setSelectedBlock(int id)
{
    if (id < 0 || id >= int(BlockRegistry::Count)) return; // 仅 clamp 到合法 id 区间
    if (id == m_selectedBlock) return;
    m_selectedBlock = id;
    emit selectedBlockChanged();
}

// 手持物品原始 id（t34）：方块段直接透传；工具段（>=0x100）合法也接受（挖掘速度查 ToolRegistry
// 用）。非法 / 越段 id 静默拒（与 setSelectedBlock 不同：此处无上界，因工具段 > BlockRegistry::Count）。
void PlayerController::setSelectedItem(int id)
{
    if (id == m_selectedItem) return;
    m_selectedItem = id;
    emit selectedItemChanged();
}

// t238 设饥饿值（存档加载用）。clamp 到 [0, kMaxHunger]；同步本类 Physics 层 m_hunger + emit hungerUpdated
//   让 Main.qml 路由到 playerState.setHunger（Game 层显值与 Physics 层值对齐：存档只持久化 playerState.hunger，
//   本方法把同一值灌回 Physics 层镜像）。无变化静默。供 Main.qml::applyPlayerState 在 playerState.setHunger
//   之后配对调用（两层数据一致、depletion 从存档值起算）。
void PlayerController::setHunger(int value)
{
    const int nv = std::clamp(value, 0, int(kMaxHunger));
    if (nv != m_hunger) { m_hunger = nv; emit hungerUpdated(m_hunger); }
}

// 左键按下（t34）：按模式分流。
//   Creative：瞬破（progress 等价 1.0）→ 立即 setBlock(air) + swingArm + playerMined(drop=false)。
//             spec：创造单击瞬破、不掉落。
//   Survival：进入累积态（mining=true，记录目标格，progress=0），由 updateMining 每 tick 推进。
//             spec：按住左键累积进度，进度满才破。
//   Spectator：canBreak()=false → 直接返回（不破 / 不进累积）。
// 共用门控：未捕获 / 无世界 → 不动作。无命中 → 挥空手（t68，见下）。无窗口 → 不动作。
// 兼容：breakBlock()（旧 Q_INVOKABLE）等价调本方法（创造瞬破 / 生存开始累积）。
void PlayerController::beginMining()
{
    // t655 死亡态输入闸门：尸体不攻击 / 不挖（含 mob / 船 / 矿车攻击分流与挥空手反馈）。置于
    //   m_leftDown 记录之前 —— 死亡态连「按钮按住」这一事实都不留（防 respawn 后 updateMining 据陈旧
    //   m_leftDown 自动续挖）。正常路径：死亡时 onDied → release 已把 m_leftDown 清 false + 暂停早 return
    //   里 cancelMining；本闸门是双保险（QML 若在死亡态重 grab 被新闸拦，此处兜攻击入口）。
    if (m_dead) return;
    // t44 连续挖掘：记录物理左键按下态。置于所有早 return 之前 —— 即便当前不能开始累积
    // （观察者 / 无命中 / 暂停），按钮按下这一事实仍成立，后续 updateMining 据此 + 新命中自动续挖。
    m_leftDown = true;
    if (!canBreak()) return; // 观察者不能破块（t21）
    if (!m_world || !m_captured) return;
    // t242 攻击 mob（spec「玩家左键攻击生物」）：在通过模式门控后、破块 / 挥空手前先做 ray-mob 命中
    //   测试。命中活体 mob 且（无方块命中 OR mob 比方块更近）→ 走攻击路径（damageEntity + swingArm
    //   + emit mobAttacked → 呈现层 playHurt），不进破块分支。机制等价 MC 1.0「准星瞄生物左键攻击」，
    //   生物比背后方块更近时优先打生物。任一不满足（无 mob 命中 / 方块更近）→ 落回原破块 / 挥空手路径。
    if (m_entityManager) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        if (mobIdx >= 0 && mobDist <= m_hitDist) {
            attackMob(mobIdx);
            return;
        }
    }
    // t508 挖船掉落（spec「攻击 / 挖船 → 船实体消失 + 掉落船物品」；机制等价 MC 1.0 攻击船 → 船破坏掉船物品）。
    //   独立跑 boat 命中射线（findBoatHit，同 mob 攻击路径模式）：命中活体船且（无方块命中 OR 船比方块更近）
    //   → hitBoatFromRay 移除船 + emit boatBroken（→ 呈层 spawnItem 掉船物品）+ swingArm，不进破块分支。
    //   任一不满足（无船命中 / 方块更近）→ 落回破块 / 挥空手路径。挖的是被骑的船 → 玩家自然下马（hitBoatFromRay 内清骑乘态）。
    //   t248 攻击冷却门控（kAttackCooldown）：防长按左键 updateMining 续挖分支每 tick 重触 beginMining → 瞬秒多船。
    if (m_boatManager && m_attackCooldown <= 0.0f) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float boatDist = 0.0f;
        const int boatIdx = m_boatManager->findBoatHit(eye, look, kReach, &boatDist);
        if (boatIdx >= 0 && boatDist <= m_hitDist) {
            m_boatManager->hitBoatFromRay(eye, look, kReach, m_world); // t711：传 world 选非实心邻格掉落（防埋）
            m_attackCooldown = kAttackCooldown; // 挖船也走攻击冷却（防长按瞬秒多船）
            emit swingArm();
            return;
        }
    }
    // t565 挖矿车掉落（机制等价 MC 1.0 攻击矿车 → 矿车破坏掉矿车物品；同挖船 hitBoatFromRay 分流模式）。
    //   独立跑 cart 命中射线：命中活体矿车且（无方块命中 OR 矿车比方块更近）→ hitCartFromRay 移除矿车 +
    //   emit cartBroken（→ 呈层 spawnItem 掉矿车物品）+ swingArm，不进破块分支。挖的是被骑的矿车 → 玩家自然
    //   下车（hitCartFromRay 内清骑乘态）。攻击冷却门控同船（防长按瞬秒多车）。
    if (m_minecartManager && m_attackCooldown <= 0.0f) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float cartDist = 0.0f;
        const int cartIdx = m_minecartManager->findCartHit(eye, look, kReach, &cartDist);
        if (cartIdx >= 0 && cartDist <= m_hitDist) {
            m_minecartManager->hitCartFromRay(eye, look, kReach);
            m_attackCooldown = kAttackCooldown; // 挖矿车也走攻击冷却
            emit swingArm();
            return;
        }
    }
    // t68 挥空手：左键对空气（无命中方块）时仍 emit swingArm（挥臂动画反馈），但**不进入**
    // mining 状态（不破块、不扣耐久、不显裂纹）。这是「动作反馈应独立于是否命中」的通用原则 ——
    // 挥臂是玩家主观动作的呈现，命不命中只决定后续语义（破块 / 未来攻击判定），不应阻塞挥臂反馈。
    // 此分支为后续攻击系统（打怪）铺垫：攻击判定走同一 raycast，但挥臂本身无目标也要触发。
    // 第一 / 二 / 第三人称均消费 swingArm 驱动手臂挥动（见 Main.qml）。
    if (!m_hasHit) {
        emit swingArm();
        return;
    }

    if (m_mode == Creative) {
        // 创造：瞬破（progress 直接 1.0 等价），不掉落。仍发 swingArm（finishMiningAt 末尾发，动作真发生）。
        // 不进入累积态（mining 留 false）→ 不显裂纹叠层（瞬破无需裂纹）。
        // finishMiningAt 内自行取原方块 id（setBlock 前）发 playerMined；此处无需重复读取。
        // t141：删 t119 的创造 canMine 守卫 —— 创造模式下基岩**可秒破**（spec「基岩创造可破」）。原守卫
        //   拦截基岩（hardness<0 → canMine=false）仅给挥臂不破；现移除，基岩与普通方块同走瞬破（仍发
        //   swingArm）。生存基岩仍不可破：updateMining 内 if(canMine(bid)&&progress>=1.0) 守 finishMiningAt。
        finishMiningAt(m_hitBx, m_hitBy, m_hitBz, /*drop=*/false);
        return;
    }

    // Survival：开始累积。重置目标 / 进度 / stage；stage 从 0 起（裂纹首阶立显，反馈即时）。
    m_mining = true;
    m_mineBx = m_hitBx; m_mineBy = m_hitBy; m_mineBz = m_hitBz;
    m_miningProgress = 0.0f;
    m_miningStage = 0;
    m_mineBeat = -1; // t165：挥臂节拍归位（新目标首 beat 0 立挥）
    m_lastMineSoundMs = -100000; // t231：音节流归位（新会话首 beat 不受节流限制）
    emit miningStateChanged();
    emit miningProgressChanged();
    emit swingArm(); // 起手挥动（持续挖掘期间每阶切换再补发，形成挥动循环）
}

// 左键松开（t34）：清累积进度。创造模式下未进入累积态（mining=false）→ no-op。
// t44：同步清 m_leftDown（按钮物理松开）—— 切断连续挖掘的续挖条件（spec：松手清零）。
void PlayerController::endMining()
{
    m_leftDown = false;
    if (!m_mining) return;
    cancelMining();
}

// 清累积挖掘态（松开 / 换目标 / 失焦 / 暂停 / 完成）。无累积时静默（不发信号，免抖动）。
void PlayerController::cancelMining()
{
    if (!m_mining && m_miningStage < 0) return;
    m_mining = false;
    m_miningProgress = 0.0f;
    m_miningStage = -1;
    m_mineBeat = -1; // t165：挥臂节拍归位
    m_lastMineSoundMs = -100000; // t231：音节流归位（下次挖掘会话首 beat 不受限）
    emit miningStateChanged();
    emit miningProgressChanged();
}

// 完成（progress 满 / 创造瞬破）：写 air + 发 playerMined + swingArm + 清态。
// drop 由 caller 决定（创造=false；生存=ToolRegistry::canHarvest(原方块, 手持物)）。
// 取原方块 id（setBlock 前）作 playerMined 的 blockId 参数（t35 据此 spawn 对应物品）。
void PlayerController::finishMiningAt(int x, int y, int z, bool drop)
{
    if (!m_world) return;
    const quint8 brokenId = m_world->blockAt(x, y, z);
    // t134 门两格破坏联动：破任一格 → 同步清配对格（另一格），防留半截悬空门。配对格据本格 state bit3
    //   （isUpper）判上 / 下：本格上格 → 配对 y-1；本格下格 → 配对 y+1。仅当配对格同为 WoodDoor 才清
    //   （防御：state 不一致时不误清异格）。drop 仅对本格发（配对格静默清，避免双掉落）。
    //   ⚠️ brokenState 必须在 setBlock(Air) **之前**读：4 参数 setBlock 委托 5 参数版以 state=0 写入
    //   （chunk.cpp「id 变更时重置 state=0」契约），之后 stateAt 永返 0 → 配对方向恒算成 y+1 →
    //   破上格（bit3=1 本应 y-1 找下格）时下格不清，留半截悬空门。useBlock 路径在 setBlock 前读 st 故无此坑。
    //   t237：WheatCrop 同理须读旧 state（state==WheatCropStageMax 判成熟 → 掉小麦 vs 仅种子），setBlock 后
    //   state 重置为 0 → 永判未成熟 → 成熟作物收割不掉小麦（同族 lessons-learned t134「先快照再改 id」坑）。
    const quint8 brokenState = (BlockRegistry::isDoor(brokenId)
                                || BlockRegistry::isBed(brokenId)
                                || brokenId == BlockRegistry::Planks
                                || brokenId == BlockRegistry::SprucePlanks
                                || brokenId == BlockRegistry::WheatCrop
                                || brokenId == BlockRegistry::CarrotCrop
                                || brokenId == BlockRegistry::PotatoCrop
                                || brokenId == BlockRegistry::SnowLayer // t505 雪层按 state 掉 (state+1) 雪球
                                || brokenId == BlockRegistry::Painting) // t721 画：state 带 face/index（连通域移除用）
        ? m_world->stateAt(x, y, z) : quint8(0);
    // t506 冰（Ice）生存挖掘 → 生成水方块（机制等价 MC 1.0 冰破成水）：精准采集（SilkTouch）→ 走通用 silk 分支
    //   掉 Ice 自身（line ~1007），不在此处理；非精准采集 → 破冰格置水源（Water state=0）而非 Air。PackIce /
    //   BlueIce 非精准 dropId=0 不掉、且不生水（MC 1.0 浮冰 / 蓝冰破坏无水）→ 仅 Ice 走生水分支。Water setBlock
    //   发 blockBroken(Ice→Water 的 oldId=Ice) + worldChanged（mesh 重建，iceOnly 段消失 / waterOnly 段冒出）+
    //   poke 水流节流（让水源即刻外溢，机制等价 MC「冰破成水后水会流动」）。创造 drop=false 仍生水（机制等价 MC
    //   创造破冰亦成水 —— 冰的「破成水」是方块本身属性，与是否掉落无关）。
    const bool iceSilkTouch = (brokenId == BlockRegistry::Ice && m_hotbar
                               && m_hotbar->selectedItemEnchantLevel(EnchantRegistry::SilkTouch) > 0);
    const quint8 replaceId = (brokenId == BlockRegistry::Ice && !iceSilkTouch)
                             ? BlockRegistry::Water : BlockRegistry::Air;
    m_world->setBlock(x, y, z, replaceId, quint8(0)); // → World 发 blockBroken（粒子触发）+ worldChanged（mesh 重建）
    // t665 怪物蛋（石砖形；机制等价 MC 1.0 silverfish stone）破坏 → **生成 Silverfish 敌对 mob 替代掉落**
    //   （MC「挖怪物蛋必出蠹虫」；dropId=0 不掉方块）。生存 + 创造均触发（机制等价 MC：创造挖怪物蛋同样
    //   出虫——方块属性而非掉落）。生成位 = 破格中心（同生物蛋 useBlock 语义：spawnMobTyped 以格坐标 +0.5
    //   存中心；重力 tick 贴地）。mobType = EntityManager::MobSilverfish（14，敌对小虫）。
    //   分层（PLAN §2）：生成属 Game/Physics（调 EntityManager），不写栅格（setBlock 已破）。
    if (m_entityManager && brokenId == BlockRegistry::MonsterEgg)
        m_entityManager->spawnMobTyped(x, y, z, EntityManager::MobSilverfish, QStringLiteral("#c8c2b8"), 0);
    // t721 画作连通域移除（机制等价 MC 1.0 破画：整张画消失 + 只掉 1 个 painting 物品，非逐格掉）：
    //   破坏任一画格 → removePaintingAt 按 brokenState 的 face flood-fill 同面 Painting 连通域（本格已被
    //   上方 setBlock 清 Air；余格 BFS 收集后 setWaterSilent 静默清 —— 多格画逐格 blockBroken 会刷成
    //   粒子/音风暴）。掉落受 drop 标志门控（主动破坏仅生存掉，t571①语义）；锚格 index 由连通域内读出
    //   （破坏中间格时锚格仍在域内）。之后**不再进通用掉落链**（drop 分支的通用 dropId 路径会再掉一件
    //   → return 前拦截；本格 setBlock 已发一次 blockBroken 粒子/音）。
    if (brokenId == BlockRegistry::Painting) {
        const int face = (brokenState & BlockRegistry::PaintingStateFaceMask)
                         >> BlockRegistry::PaintingStateFaceShift;
        removePaintingAt(x, y, z, face, /*drop=*/drop);
        // 画作无支撑依赖其它画（画格连通域已随 removePaintingAt 全清）→ 无需再扫邻；但破的画格本身
        // 背后的墙仍在（画破不动墙），跳过 dropUnsupportedPaintingsAround（无意义扫描）。
        emit playerMined(x, y, z, int(brokenId), drop);
        if (m_mode == Survival && m_hotbar) m_hotbar->damageSelectedItem(); // 同通用路径：生存破块工具 -1 耐久
        emit swingArm();
        cancelMining();
        return; // 画特判收口：不走通用掉落 / 级联链
    }
    // t134/t466 门两格破坏联动（统一经 isDoor 谓词覆盖 WoodDoor + SpruceDoor）：破任一格 → 同步清配对格
    //   （另一格），防留半截悬空门。配对格据本格 state bit3（isUpper）判上 / 下：本格上格 → 配对 y-1；
    //   本格下格 → 配对 y+1。仅当配对格**同为门**（isDoor）才清（防御：state 不一致时不误清异格；不同材质门
    //   不互为配对 —— 云杉门配对格不会误清橡木门，反之亦然，因配对格须 isDoor 但其 id 不强制 == 本格 id，
    //   实践中门放置仅产生同 id 配对故安全）。drop 仅对本格发（配对格静默清，避免双掉落）。
    if (BlockRegistry::isDoor(brokenId)) {
        const int py = ((brokenState & 8) != 0) ? y - 1 : y + 1;
        if (BlockRegistry::isDoor(m_world->blockAt(x, py, z)))
            m_world->setBlock(x, py, z, BlockRegistry::Air);
    }
    // t428 床双格破坏联动：破任一半 → 静默清配对格（另一半），防留半截悬空床（同门破坏联动模式）。
    //   drop 仅对本格发（配对格 setBlock(Air) 虽发 blockBroken 但不走 spawnItem → 1 床物体只掉 1 件）。
    //   brokenState 须在 setBlock(Air) 前 capture（同门 t134 lessons-learned：id 变更重置 state=0）。
    if (BlockRegistry::isBed(brokenId)) {
        int pdx = 0, pdz = 0;
        BlockRegistry::bedPartnerOffset(brokenState, pdx, pdz);
        if (BlockRegistry::isBed(m_world->blockAt(x + pdx, y, z + pdz)))
            m_world->setBlock(x + pdx, y, z + pdz, BlockRegistry::Air);
    }
    emit playerMined(x, y, z, int(brokenId), drop); // 破块语义事件（含 drop 标志；当前无消费端，留扩展）
    // t214 火把失支撑立即掉落：破块后扫 6 邻火把，其**附着格**（state 编码）若已非 solid（含本格刚被置
    //   Air）→ 火把直接掉落为物品。机制等价 MC「火把附着面被移除即脱落，不重新粘到附近其它可支撑方块」。
    //   旧实现 torchHasSupport 查 5 向任一 solid → 墙火把破墙后仍被地面「粘」住不掉（用户报「火把不掉」）。
    //   改读 state 编码的唯一附着邻居：仅当该邻居失撑才掉。掉落走 setBlock(Air) → World 发 blockBroken +
    //   worldChanged → Main.qml 移除伪光源（removeTorchAt / onWorldChanged 兜底）+ mesh 重建。火把非
    //   solid → 不撑他火把 → 单趟 6 邻扫即足够（无级联，破一块不会链式掉一串）。
    //   t571 标注【自然失撑掉落：恒发（含创造）】—— 破坏支撑方块是因、火把脱落是果，非玩家直破火把本体。
    dropUnsupportedTorchesAround(x, y, z);
    // t501 木梯失支撑立即掉落：破块后扫 6 邻木梯，其**支撑墙**（state 编码）若已非完整立方（含本格刚被置
    //   Air）→ 木梯直接掉落为物品。机制等价 MC「梯子贴墙被移除即脱落」（同火把失撑语义）。木梯非 solid
    //   → 不撑他木梯 → 单趟 6 邻扫即足够（无级联）。
    //   t571 标注【自然失撑掉落：恒发（含创造）】。
    dropUnsupportedLaddersAround(x, y, z);
    // t662 机关方块（拉杆 / 木按钮 / 石按钮）失支撑立即掉落：破块后扫 6 邻机关，其**附着格**（state 编码）
    //   若已非完整立方（含本格刚被置 Air）→ 机关直接掉落为物品。机制等价 MC「机关附着面被移除即脱落」
    //   （同火把 / 木梯失撑语义）。t571 标注【自然失撑掉落：恒发（含创造）】。
    dropUnsupportedMechAround(x, y, z);
    // t721 画作支撑墙失撑掉落：破块后扫 4 水平邻的画，其支撑墙格 == 本破块格 → 整张画掉落 1 件
    //   （removePaintingAt 连通域移除，drop=true 恒发含创造）。机制等价 MC「画后面的墙被挖 → 画掉落」
    //   （同火把 / 木梯失撑语义）。t571 标注【自然失撑掉落：恒发（含创造）】。
    dropUnsupportedPaintingsAround(x, y, z);
    // t247 草丛 / 小麦作物失撑掉落：破块后其正上方的草丛 / 小麦作物（唯一支撑 = 本格，刚被破为 Air）
    //   直接掉落（同火把失撑语义）。brokenState 已在 setBlock(Air) 前读（WheatCrop 在上 / 普通块 = 0），
    //   但本方法在上方格单独读 cstate（上方作物自身的 state），与 brokenState 无关。
    //   t571 标注【自然失撑掉落：恒发（含创造）】。
    dropUnsupportedCropsAround(x, y, z);
    // t305/t325 树叶衰减（spec「挖光一棵树所有原木→树叶消失」，t325 渐进化）：玩家破原木 → 触发 World 扫破块点
    //   周围树叶，失撑叶（4 格切比雪夫距离内无原木）**入渐进衰减队列**（非瞬时清）；队列由 tickLeafDecay 每
    //   窗按散布概率逐叶渐退 ~10-30s（机制等价 MC 1.0 叶衰 random-tick 渐退）。**不依赖 drop 标志**（创造瞬破
    //   drop=false 亦触发 —— 衰减是结构后果，非掉落；机制等价 MC 创造破原木后叶子照衰）。仅原木触发（破叶 /
    //   破其它方块不衰）。分层：本处（Game/Physics）调 World::decayLeavesAround（World 层方法），向下合法。
    if ((brokenId == BlockRegistry::Log || brokenId == BlockRegistry::SpruceLog) && m_world)
        m_world->decayLeavesAround(x, y, z);
    // t43：生存挖出可掉落方块 → **走实体流**（emit spawnItem），移除 commit a3e9300 的 auto-collect
    // （原直接 addStack）。掉落物落到该格地面、玩家走近 ≤kPickupDist 时经 pickupScan 拾取 → addStack
    // （先选中槽、再空槽，智能堆叠至 maxStack）。满栈不进背包则实体留地面（spec：全满→不拾取）。
    // drop 由 caller 算（生存走 ToolRegistry::canHarvest；创造瞬破 drop=false 不发）。
    // t64：spawnItem 带 count（= BlockRegistry::dropCount；当前表内全 1，留扩展位对齐方块表）。
    // ── t571 掉落语义标注【主动破坏掉落：仅生存】：本 if (drop) 块内全部路径（通用 dropId / 作物 / 叶 /
    //    雪层 / 雪块 / 双半砖 / silk / fortune）都是「被玩家点击破坏的那一格本体」的掉落 → 一律受 drop 标志
    //    门控（创造 drop=false 全部跳过，零掉落）。与之相对的「自然失撑掉落」（下方方法族 + 末尾甘蔗/仙人掌
    //    级联上方格）恒发（含创造），见各自标注。
    if (drop) {
        int dropId = BlockRegistry::dropId(brokenId);
        int dropCount = std::max(1, BlockRegistry::dropCount(brokenId));
        // t247 草丛 / 小麦作物掉落产出收敛到 dropCropDrops（玩家破块 / 失撑共用同一 spawnItem 逻辑）：
        //   - WheatCrop：按 state 判成熟（t237 收割 —— 成熟掉 1 小麦物品 + 1-2 种子 / 未成熟仅 1 种子）；
        //   - TallGrass：1/kTallGrassSeedDropDenom 概率掉种（t246）。
        //   同 PlanksFromDoubleSlabBit 双半砖模式：特殊掉落在通用 BlockDef 表之上提前分流，特例 else 走通用 dropId/dropCount。
        //   brokenState 已在 setBlock(Air) 前读（t134 时序：WheatCrop 在 snapshot 条件内，成熟判定可靠）。
        if (brokenId == BlockRegistry::WheatCrop || brokenId == BlockRegistry::TallGrass
            || brokenId == BlockRegistry::CarrotCrop || brokenId == BlockRegistry::PotatoCrop) {
            // t619 progress 成就埋点（review-L2 上移到此）：**生存玩家直破成熟作物**（任一种）→
            //   cropHarvested 语义事件（「农夫」累计）。仅在 drop=true（生存 canHarvest 通过）的主动
            //   破坏路径发：创造瞬破（drop=false）与破支撑块的失撑级联（dropUnsupportedCropsAround）
            //   不算「收获」—— 否则挖作物下方一格即可刷计数。成熟判定用 setBlock 前快照 brokenState。
            if ((brokenId == BlockRegistry::WheatCrop || brokenId == BlockRegistry::CarrotCrop
                 || brokenId == BlockRegistry::PotatoCrop)
                && brokenState >= BlockRegistry::WheatCropStageMax)
                emit cropHarvested();
            dropCropDrops(x, y, z, brokenId, brokenState);
        } else if (brokenId == BlockRegistry::Leaves || brokenId == BlockRegistry::SpruceLeaves) {
            // t305 玩家破叶 → 概率掉树苗物品 + 木棒（机制等价 MC 1.0 破叶 5% 树苗 / 2% 木棒）。Leaves.dropId=0
            //   （表兜底无自掉），本特例分支覆盖通用 drop 路径（同 WheatCrop/TallGrass/双半砖模式）。自然衰减
            //   （decayLeavesAround）不走此（无掉落）。brokenState 不影响叶掉落（无 state 派生 —— PersistentLeafBit
            //   仅控衰减，破叶掉落同）。t714：云杉叶同分流（同族叶机制，掉同一橡树树苗——本工程树苗仅橡树一种）。
            dropLeafDrops(x, y, z);
        } else if (brokenId == BlockRegistry::SnowLayer) {
            // t505 雪层铲挖掉雪球（机制等价 MC 1.0 snow layer 铲挖掉 (layer+1) 雪球；空手不掉落由 canHarvest
            //   requiresTool=true 守卫，到 drop=true 路径即已持铲）。每层雪球数 = state+1（state 0..7 → 1..8 雪球，
            //   机制等价 MC 雪层掉雪球按层数；brokenState 已在 setBlock(Air) 前 capture，stateAt 读旧层数可靠）。
            //   走 spawnItem 一次 emit count=(state+1) 件（同甘蔗 / 仙人掌级联外的单格多件，1 实体携多层雪球，
            //   拾取 addStack 一次入多层；机制对标 MC 雪层挖出多雪球）。SnowLayer.dropId=SnowballId(0x23D) 表兜底，
            //   本分支按 state 精确放大 count 覆盖通用 dropId/dropCount（同 WheatCrop/TallGrass 按 state 掉落模式）。
            const int layers = std::min(int(brokenState) + 1, int(BlockRegistry::SnowLayerStageMax) + 1); // clamp 1..8
            emit spawnItem(x, y, z, RecipeRegistry::SnowballId, layers);
        } else if (brokenId == BlockRegistry::Snow) {
            // t505 雪块铲挖掉 2-3 雪球（spec「雪块被铲子挖掉应掉 2-3 个雪球」；机制简化对标 MC 1.0 雪块掉 4 雪球 ——
            //   用户报告要求 2-3 故取随机 2..3）。空手不掉落由 canHarvest requiresTool=true 守卫（到 drop=true 路径
            //   即已持铲）。Snow.dropId=SnowballId / dropCount=4 表兜底，本分支按随机 2..3 覆盖通用 dropCount
            //   （同 SnowLayer 按 state 覆盖模式）。走 spawnItem 一次 emit count 件（1 实体携多雪球，拾取 addStack
            //   一次入多件）。
            const int snowballCount = 2 + int(QRandomGenerator::global()->bounded(2)); // 随机 2..3（bounded(2) → 0..1）
            emit spawnItem(x, y, z, RecipeRegistry::SnowballId, snowballCount);
        } else if ((brokenState & BlockRegistry::DoubleSlabMarkerBit)
                   && BlockRegistry::fullBlockSlabDrop(brokenId) != 0) {
            // t215/t412 双半砖（合并态）破块掉 2× 对应半砖为**2 个独立物品实体**（非 1 个 count=2 栈）：
            //   placeBlock 合并时写满格整立方（Planks / Cobble）+ DoubleSlabMarkerBit 标记「源自双半砖」。此处检
            //   本 bit → 改掉 2 块对应半砖（WoodSlab / CobbleSlab）。机制等价 MC「double slab 破坏掉 2 块半砖，各自
            //   为独立掉落物」。原实现 emit 1 次 count=2 → 1 实体携 2 件（拾取 addStack 一次入 2）；改 emit 2 次
            //   count=1 → 2 实体各携 1 件（拾取各入 1）。两实体散布到破格 + 1 个非实体水平邻格做视觉分离（机制
            //   等价 MC 方块掉落的水平散布；ItemEntityManager spawnItem 仅整数格坐标存格中心，故以邻格区分，且选
            //   非实体邻格避免实体被重力弹到墙顶偏离破块）。无可用邻格则两实体同破格（仍 2 实体，拾取各入 1）。
            //   常规 Planks / Cobble（state=0）不进此分支 → 掉 1× 满砖不变。brokenState 已在 setBlock(Air) 前读
            //   （t134 时序：4 参数 setBlock 委托 5 参数版以 state=0 写入，之后 stateAt 永返 0）。
            dropId = BlockRegistry::fullBlockSlabDrop(brokenId);
            constexpr int kHoriz[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            int sx = x, sz = z;
            for (const auto &o : kHoriz) {
                if (!BlockRegistry::isSolid(m_world->blockAt(x + o[0], y, z + o[1]))) { sx = x + o[0]; sz = z + o[1]; break; }
            }
            emit spawnItem(x, y, z, dropId, 1);
            emit spawnItem(sx, y, sz, dropId, 1);
        } else {
            // t476 工具附魔改通用掉落（机制等价 MC 1.0 silk-touch / fortune）：
            //   - 精准采集 SilkTouch：掉方块**自身**（brokenId，count 1）而非 dropId（石→石非圆石 / 草→草非土 /
            //     矿石→矿石方块非材料 / 刷怪笼→刷怪笼）。仅 Tool 类工具（镐等）可带 silk；空手无 silk → 走原 dropId。
            //   - 时运 Fortune：对矿石（isFortuneOre）掉落数 ×(1..1+level)（机制等价 MC fortune ore multiplier）。
            //     非矿石（石 / 圆石等）时运不放大（MC 时运对石无效）。silk 与 fortune 互斥（silk 优先：掉 1 块自身）。
            int finalDropId = dropId;
            int finalDropCount = dropCount;
            if (m_hotbar) {
                const int silkLvl = m_hotbar->selectedItemEnchantLevel(EnchantRegistry::SilkTouch);
                const int fortuneLvl = m_hotbar->selectedItemEnchantLevel(EnchantRegistry::Fortune);
                if (silkLvl > 0) {
                    finalDropId = int(brokenId);
                    finalDropCount = 1;
                } else if (fortuneLvl > 0 && isFortuneOre(brokenId)) {
                    // 矿石掉落数 ×(1 + [0, level])：时运 I 1-2× / II 1-3× / III 1-4×（MC 风格区间放大）。
                    finalDropCount = dropCount * (1 + QRandomGenerator::global()->bounded(0, fortuneLvl + 1));
                }
            }
            emit spawnItem(x, y, z, finalDropId, finalDropCount); // t83：传 finalDropId（silk→brokenId / 余→dropId）
        }
    }
    // t418 垂直植物级联掉落（机制等价 MC 甘蔗 / 仙人掌柱：破任一格 → 其上整柱坍落）：当被破格为 Sugarcane /
    //   Cactus 时，自破格正上一格起向上逐格破同型块（setBlock Air → blockBroken 粒子/音 + worldChanged 重建）。
    //   停于首个异型格；越界 blockAt 返 Air ≠ brokenId → 循环自然终止（无 OOB 风险）。
    // ── t571 掉落语义二分（主动 vs 自然）：
    //   ① 主动破坏掉落（被玩家点击破坏的那一格本身的掉落物）：**仅生存**（drop 标志门控，走上方通用 drop 路径
    //      line ~1141 spawnItem）。创造瞬破 drop=false → 本格零掉落（机制等价 MC 创造破块无掉落；t547 曾让本格
    //      恒掉（含创造补发），t571 修正 —— 创造打甘蔗/仙人掌任一格，被破坏格本体不掉）。
    //   ② 自然失撑掉落（破掉支撑后**上方失撑格**的级联掉落）：**恒发（含创造）** —— 本循环逐格 spawnItem。
    //      机制等价 MC 创造打掉甘蔗中间格：该格不掉，但其上失撑整柱照掉；与 World 侧 dropSugarcaneColumn /
    //      dropCactusColumn（挖沙 → 整柱坍落为掉落物，模式无关恒掉）同语义 —— t547② 报的「创造破柱直接消失」
    //      修的是②；t571 报的「创造本格也掉」错在①（本格补发越权）。
    //   不递归（植物柱仅靠下方支撑，破上方不连累下方）。brokenId 已在 setBlock(Air) 前读（同 t134 时序坑）。
    //   每格 setBlock Air 各自发 blockBroken + 标脏，无需额外 worldEdited/dirty 串联。Sugarcane/Cactus 非
    //   Log/Leaves/Torch/Crop → 级联格不走 leaf-decay / torch/crop 失撑分支（本就无副作用）。
    if (brokenId == BlockRegistry::Sugarcane || brokenId == BlockRegistry::Cactus) {
        const int cascadeDropId = BlockRegistry::dropId(brokenId);
        const int cascadeDropCount = std::max(1, BlockRegistry::dropCount(brokenId));
        for (int cy = y + 1; m_world->blockAt(x, cy, z) == brokenId; ++cy) {
            m_world->setBlock(x, cy, z, BlockRegistry::Air);
            emit spawnItem(x, cy, z, cascadeDropId, cascadeDropCount); // t571②：上方失撑格级联掉落恒发（含创造）
        }
    }
    // t263 生存挖掘完成 → 持有工具消耗 1 点耐久（机制等价 MC「每破 1 块工具 -1 耐久」）。创造 drop=false
    //   路径不调本方法（beginMining 内瞬破 return，m_mode!=Survival 守卫挡）；生存可挖方块无论是否掉落
    //   （canHarvest）均消耗（破需工具但无产出仍磨损工具，机制等价 MC）。damageSelectedItem 对非工具 / 空
    //   槽静默 no-op；耐久归零自动清槽（工具破损消失）。空手破块无工具 → 不消耗（无耐久概念）。
    if (m_mode == Survival && m_hotbar) m_hotbar->damageSelectedItem();
    emit swingArm();                                // 破块成功 → 第一人称手挥动（t29）
    cancelMining();                                 // 清累积态（裂纹叠层隐藏）
}

// t214 破块后扫 (x,y,z) 的 6 邻火把：解码每火把 state 的附着方向（BlockRegistry::torchAttachOffset）
//   定位其**唯一支撑格**，该格已非 solid（含刚被置 Air 的本破块）→ 火把直接掉落为物品（setBlock(Air) +
//   spawnItem）。机制等价 MC「火把附着面被移除即脱落」。火把非 solid → 不撑他火把 → 单趟扫即足够
//   （无级联）。与 placeBlock 火把预检（5 向任一 solid 即可放）正交：放置允许多支撑，但**掉落只看
//   state 记录的那一个附着面**——故破墙后即便火把下方仍有地面，也因「墙是它的附着面」而掉落（不粘地）。
void PlayerController::dropUnsupportedTorchesAround(int x, int y, int z)
{
    if (!m_world) return;
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &d : kNb) {
        const int tx = x + d[0], ty = y + d[1], tz = z + d[2];
        const quint8 tb = m_world->blockAt(tx, ty, tz);
        // t638 ⑥：红石火把同火把失撑掉落（state 同 torchAttachOffset 编码——placeBlock t638 并入 Torch
        //   分支写 state，附着语义一致；破支撑邻即掉，机制等价 MC 红石火把附着面移除脱落）。
        if (tb != BlockRegistry::Torch && tb != BlockRegistry::RedstoneTorch) continue;
        int ax, ay, az;
        BlockRegistry::torchAttachOffset(m_world->stateAt(tx, ty, tz), ax, ay, az);
        const int sx = tx + ax, sy = ty + ay, sz = tz + az;
        if (BlockRegistry::isSolid(m_world->blockAt(sx, sy, sz))) continue; // 附着格仍 solid → 火把保留
        m_world->setBlock(tx, ty, tz, BlockRegistry::Air); // → World 发 blockBroken + worldChanged → 清伪光源 + 重建
        emit spawnItem(tx, ty, tz, BlockRegistry::dropId(tb),
                       std::max(1, BlockRegistry::dropCount(tb)));
    }
}

// t501 破块后扫 (x,y,z) 的 6 邻木梯：解码每木梯 state 的支撑墙方向（BlockRegistry::ladderSupportOffset）
//   定位其**唯一支撑墙格**，该格已非完整立方（含刚被置 Air 的本破块）→ 木梯直接掉落为物品（setBlock(Air) +
//   spawnItem）。机制等价 MC「梯子贴墙被移除即脱落，不重新粘到附近其它可支撑方块」（同火把失撑语义）。
//   木梯非 solid → 不撑他木梯 → 单趟扫即足够（无级联）。与 placeBlock 木梯预检（须完整立方侧面支撑）正交：
//   放置守「完整立方」，掉落看「state 记录的唯一支撑墙」—— 故破墙后即便木梯下方仍有地面，也因「墙是它的
//   唯一支撑」而掉落（不粘地，与火把行为一致）。
void PlayerController::dropUnsupportedLaddersAround(int x, int y, int z)
{
    if (!m_world) return;
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &d : kNb) {
        const int tx = x + d[0], ty = y + d[1], tz = z + d[2];
        if (m_world->blockAt(tx, ty, tz) != BlockRegistry::Ladder) continue;
        int ax, az;
        BlockRegistry::ladderSupportOffset(m_world->stateAt(tx, ty, tz), ax, az);
        const int sx = tx + ax, sy = ty, sz = tz + az;
        if (BlockRegistry::isFullCube(m_world->blockAt(sx, sy, sz))) continue; // 支撑墙仍完整立方 → 木梯保留
        m_world->setBlock(tx, ty, tz, BlockRegistry::Air); // → World 发 blockBroken + worldChanged → mesh 重建
        emit spawnItem(tx, ty, tz, BlockRegistry::dropId(BlockRegistry::Ladder),
                       std::max(1, BlockRegistry::dropCount(BlockRegistry::Ladder)));
    }
}

// t662 破块后扫 (x,y,z) 的 6 邻机关方块（Lever / WoodButton / StoneButton）：解码每机关 state 的附着面
//   （BlockRegistry::mechAttachOffset）定位其**唯一支撑格**，该格已非完整立方（含刚被置 Air 的本破块）
//   → 机关直接掉落为物品（setBlock(Air) + spawnItem）。机制等价 MC「按钮 / 拉杆的附着面被移除即脱落，
//   不重新粘到附近其它可支撑方块」（同火把 t214 / 木梯 t501 失撑语义）。机关无碰撞不撑他机关 → 单趟扫
//   即足够（无级联）。附着守卫与放置预检同源（放置须 isFullCube 支撑面 → 掉落看同一格是否仍完整立方）。
//   t571 标注【自然失撑掉落：恒发（含创造）】。
void PlayerController::dropUnsupportedMechAround(int x, int y, int z)
{
    if (!m_world) return;
    constexpr int kNb[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (const auto &d : kNb) {
        const int tx = x + d[0], ty = y + d[1], tz = z + d[2];
        const quint8 tb = m_world->blockAt(tx, ty, tz);
        if (tb != BlockRegistry::Lever && tb != BlockRegistry::WoodButton
            && tb != BlockRegistry::StoneButton) continue;
        int ax, ay, az;
        BlockRegistry::mechAttachOffset(m_world->stateAt(tx, ty, tz), ax, ay, az);
        const int sx = tx + ax, sy = ty + ay, sz = tz + az;
        if (BlockRegistry::isFullCube(m_world->blockAt(sx, sy, sz))) continue; // 支撑仍完整立方 → 机关保留
        m_world->setBlock(tx, ty, tz, BlockRegistry::Air); // → World 发 blockBroken + worldChanged → mesh 重建
        emit spawnItem(tx, ty, tz, BlockRegistry::dropId(tb),
                       std::max(1, BlockRegistry::dropCount(tb)));
    }
}

// t247 草丛 / 小麦作物掉落产出（玩家破块 / 失撑共用，见 playercontroller.h 头注释）。
//   WheatCrop：按 state 判成熟（t237）—— 成熟(state>=WheatCropStageMax)掉 1× 小麦物品(WheatId) + 1-2× 种子
//   （SeedId，可再种）/ 未成熟仅 1× 种子。两实体散布到破格 + 非实体水平邻格做视觉分离。
//   t407 CarrotCrop/PotatoCrop：成熟掉 1-4× 对应物品（CarrotId/PotatoId，机制等价 MC 1.0「成熟作物掉 1-4」；
//   MC carrot/potato 物品本身即种子 + 产物，故不再额外掉种子 —— 区别于小麦的种子 + 麦粒双产物）/ 未成熟仅 1×。
//   TallGrass：1/kTallGrassSeedDropDenom 概率掉 1× 种子（t246，BlockDef.dropId/dropCount 恒返 1 种子作基础兜底，
//   本分支概率门控覆盖通用 drop 路径）。种子 1-2 / 概率均走 QRandomGenerator（玩家交互掉落的随机性，非 worldgen
//   确定性范畴 §2-K）。失撑调用同走此逻辑 → 成熟作物失撑仍掉产物（机制等价 MC「作物被任何方式移除都掉产物」）。
void PlayerController::dropCropDrops(int x, int y, int z, quint8 id, quint8 state)
{
    if (!m_world) return;
    // t619 progress 成就埋点已上移到 finishMiningAt 的 drop 分支（review-L2）：原在此分支顶部发
    //   cropHarvested → 调用方两路（finishMiningAt 主动破坏 + dropUnsupportedCropsAround 失撑级联）
    //   无差别计数，且创造瞬破（drop=false 路径外的失撑扫描）也计入 → 「挖支撑块刷农夫」。现仅
    //   **生存模式玩家直破成熟作物**（真正的收割动作）发埋点；失撑级联 / 创造破坏不发（产物照掉，
    //   但不算「收获」语义事件）。本方法保持纯掉落计算（玩家破块 / 失撑共用同源 spawnItem）。
    if (id == BlockRegistry::WheatCrop) {
        const bool mature = state >= BlockRegistry::WheatCropStageMax;
        const int wheatCount = mature ? 1 : 0;
        const int seedCount  = mature ? QRandomGenerator::global()->bounded(1, 3) : 1; // 成熟 1-2 / 未成熟 1
        constexpr int kHoriz[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        int sx = x, sz = z;
        for (const auto &o : kHoriz) {
            if (!BlockRegistry::isSolid(m_world->blockAt(x + o[0], y, z + o[1]))) { sx = x + o[0]; sz = z + o[1]; break; }
        }
        if (wheatCount > 0)
            emit spawnItem(x, y, z, RecipeRegistry::WheatId, wheatCount);
        emit spawnItem(sx, y, sz, RecipeRegistry::SeedId, seedCount);
    } else if (id == BlockRegistry::CarrotCrop || id == BlockRegistry::PotatoCrop) {
        // t407 胡萝卜/马铃薯收割：成熟掉 1-4× 对应物品（机制等价 MC 1.0 成熟作物 1-4）；未成熟仅 1×。
        //   产出物即种子（玩家可再种），机制对齐 MC carrot/potato「种 1 收 1-4」。未成熟仅返 1 个（基础兜底，
        //   同 BlockDef.dropId=CarrotId/PotatoId）。
        const bool mature = state >= BlockRegistry::WheatCropStageMax; // 三种作物共享阶段上界（blockregistry.h 注释）
        const int cropItemId = (id == BlockRegistry::CarrotCrop) ? RecipeRegistry::CarrotId : RecipeRegistry::PotatoId;
        const int count = mature ? QRandomGenerator::global()->bounded(1, 5) : 1; // 成熟 1-4 / 未成熟 1
        emit spawnItem(x, y, z, cropItemId, count);
    } else if (id == BlockRegistry::TallGrass) {
        const int dropId = BlockRegistry::dropId(id);
        const int dropCount = std::max(1, BlockRegistry::dropCount(id));
        if (QRandomGenerator::global()->bounded(kTallGrassSeedDropDenom) == 0)
            emit spawnItem(x, y, z, dropId, dropCount);
    }
    // 其余 id → no-op（caller 误调防御，不误发 spawnItem）。
}

// t247 草丛 / 小麦作物失撑掉落（见 playercontroller.h 头注释）：破块后查正上方格，若为 TallGrass / WheatCrop
//   → 其唯一支撑（下方实体方块）已被破为 Air → 直接掉落。掉落产出走 dropCropDrops（与玩家破块同源）。
//   state 须在 setBlock(Air) 前读（t134 时序）。setBlock → World 发 blockBroken(crop) + worldChanged → 粒子 + mesh 重建。
void PlayerController::dropUnsupportedCropsAround(int x, int y, int z)
{
    if (!m_world) return;
    const int cx = x, cy = y + 1, cz = z; // 正上方格：唯一支撑 = 本格（刚被破为 Air）
    const quint8 cid = m_world->blockAt(cx, cy, cz);
    if (cid != BlockRegistry::TallGrass && cid != BlockRegistry::WheatCrop
        && cid != BlockRegistry::CarrotCrop && cid != BlockRegistry::PotatoCrop) return;
    const quint8 cstate = m_world->stateAt(cx, cy, cz); // setBlock(Air) 前快照（WheatCrop 成熟判定）
    m_world->setBlock(cx, cy, cz, BlockRegistry::Air);  // → World 发 blockBroken(crop) + worldChanged → 粒子 + mesh 重建
    dropCropDrops(cx, cy, cz, cid, cstate);            // 失撑掉落产出与玩家破块同源
}

// t305 树叶掉落（见 playercontroller.h 头注释）。机制等价 MC 1.0 破叶掉落：t379 调高后 10% 树苗物品 / 8% 木棒。
//   两次独立判定（可同时掉树苗 + 木棒）。两物品散布到破格 + 非实体水平邻格做视觉分离（同 WheatCrop / 双半砖
//   模式：ItemEntityManager spawnItem 仅整数格坐标存格中心，故以邻格区分）。无邻格则同破格（仍两实体）。
void PlayerController::dropLeafDrops(int x, int y, int z)
{
    if (!m_world) return;
    // 找一个非实体水平邻格做第二物品的散布位（视觉分离）；无则同破格。
    constexpr int kHoriz[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    int sx = x, sz = z;
    for (const auto &o : kHoriz) {
        if (!BlockRegistry::isSolid(m_world->blockAt(x + o[0], y, z + o[1]))) { sx = x + o[0]; sz = z + o[1]; break; }
    }
    // 树苗物品（10%）：独立判定。
    if (QRandomGenerator::global()->bounded(100) < kLeafSaplingDropPct)
        emit spawnItem(x, y, z, RecipeRegistry::SaplingItemId, 1);
    // 木棒（8%）：独立判定（可与树苗同时掉）；落到散布邻格做视觉分离。
    if (QRandomGenerator::global()->bounded(100) < kLeafStickDropPct)
        emit spawnItem(sx, y, sz, RecipeRegistry::StickId, 1);
}

// t720 画作放置主体（见 playercontroller.h 头注释；placeBlock 画物品分支调）。机制等价 MC 1.0 painting：
// 命中墙面 → 锚格（左上）起测最大可用矩形 → 等权随机一张放得下的画 → 多格写入（锚格带 index 全信息）。
bool PlayerController::tryPlacePainting(int face)
{
    if (!m_world) return false;
    // 墙面外法线（face 编码 = horizontalFacing 同源 4 向：0=+X 1=-X 2=+Z 3=-Z）与观察者右向 u。
    int nx = 0, nz = 0, ux = 0, uz = 0;
    switch (face & 3) {
    case 0: nx =  1; nz =  0; ux =  0; uz = -1; break;
    case 1: nx = -1; nz =  0; ux =  0; uz =  1; break;
    case 2: nx =  0; nz =  1; ux =  1; uz =  0; break;
    default: nx = 0; nz = -1; ux = -1; uz =  0; break;
    }
    // 墙格 = 命中格；锚格 = 墙格 + 法线（画层 = 墙外侧一格）。锚格即「左上角」（向下 = -Y，向右 = u）。
    const int ax = m_hitBx + nx, ay = m_hitBy, az = m_hitBz + nz;
    // 锚格越界守卫（y 越界 blockAt 亦返 Air，但下方 wall 探测统一走 isSolid 越界安全；提前挡防写越界）。
    if (ay < 0 || ay >= m_world->height()) return false;
    // 画层格合法性：本格 Air 且其正后方墙格（-法线）solid（贴墙格逐格独立判支撑 —— 墙面缺口 / 窗上不放）。
    const auto cellOk = [&](int px, int py, int pz) -> bool {
        if (py < 0 || py >= m_world->height()) return false;
        if (m_world->blockAt(px, py, pz) != BlockRegistry::Air) return false;
        return BlockRegistry::isSolid(m_world->blockAt(px - nx, py, pz - nz));
    };
    if (!cellOk(ax, ay, az)) return false; // 锚格被占 / 墙非实体 → 放不下
    // ① 向右（u 向）贪心扩宽：连续 cellOk 的格数即 maxW（上限 4 = 最大画宽 4×4 格）。
    int maxW = 1;
    while (maxW < 4 && cellOk(ax + ux * maxW, ay, az + uz * maxW)) ++maxW;
    // ② 向下（-Y）逐行扩高：每行须整行 maxW 列全部 cellOk（更窄画的列集是子集 → 子集行亦成立）。
    int maxH = 1;
    while (maxH < 4) {
        const int ry = ay - maxH;
        bool rowOk = true;
        for (int k = 0; k < maxW; ++k) {
            if (!cellOk(ax + ux * k, ry, az + uz * k)) { rowOk = false; break; }
        }
        if (!rowOk) break;
        ++maxH;
    }
    // ③ 等权随机选一张 w≤maxW && h≤maxH 的画（简化 MC「先随机尺寸再随机该尺寸画」为全部合格画等权）。
    int candidates[BlockRegistry::PaintingCount];
    int nCand = 0;
    for (int i = 0; i < BlockRegistry::PaintingCount; ++i) {
        int w = 1, h = 1;
        BlockRegistry::paintingSize(i, w, h);
        if (w <= maxW && h <= maxH) candidates[nCand++] = i;
    }
    if (nCand == 0) return false; // 无合格画（maxW≥1 && maxH≥1 恒有 1×1，理论到不了；防御）
    const int index = candidates[int(QRandomGenerator::global()->bounded(quint32(nCand)))];
    // ④ 多格写入：锚格 setBlock（发 blockPlaced → 呈现层 paintingHost 加 delegate）；其余格 setWaterSilent
    //   （静默：一画只一次放置反馈 / 一次音，机制等价 MC「放 1 画 = 1 物品 = 一次动作」，同床 head 静默写模式）。
    int w = 1, h = 1;
    BlockRegistry::paintingSize(index, w, h);
    const quint8 faceBits = quint8((face & 3) << BlockRegistry::PaintingStateFaceShift);
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            const int px = ax + ux * dx, py = ay - dy, pz = az + uz * dx;
            if (dx == 0 && dy == 0) {
                // 锚格（左上）：bit7=锚 + face + index（完整信息；破坏 / 渲染都从锚格读）。
                m_world->setBlock(px, py, pz, BlockRegistry::Painting,
                                  quint8(BlockRegistry::PaintingStateAnchorFlag | faceBits
                                         | quint8(index & BlockRegistry::PaintingStateIndexMask)));
            } else {
                // 非锚格：仅 face（bit7=0）—— 身份由锚格连通域承载。
                m_world->setWaterSilent(px, py, pz, BlockRegistry::Painting, faceBits);
            }
        }
    }
    return true;
}

// t721 画作移除主体（见 playercontroller.h 头注释；直挖 + 失撑共用）。flood-fill 同 face 的 Painting
// 连通域（±u 水平 / ±Y 垂直 —— 画恒占一个垂直于法线的格子平面，u = 观察者右向）= 整张画的格子集。
void PlayerController::removePaintingAt(int px, int py, int pz, int face, bool drop)
{
    if (!m_world) return;
    int ux = 0, uz = 0;
    BlockRegistry::paintingRightOffset(face, ux, uz);
    const quint8 faceBits = quint8((face & 3) << BlockRegistry::PaintingStateFaceShift);
    // BFS 收集连通域（种子 (px,py,pz) 允许已被清 Air —— 直挖路径主破坏格先走 setBlock；邻格侧种子恒是画）。
    struct Cell { int x, y, z; };
    std::vector<Cell> cells;
    std::vector<Cell> frontier{{px, py, pz}};
    int anchorIndex = -1;
    while (!frontier.empty()) {
        const Cell c = frontier.back();
        frontier.pop_back();
        bool seen = false;
        for (const Cell &s : cells) {
            if (s.x == c.x && s.y == c.y && s.z == c.z) { seen = true; break; }
        }
        if (seen) continue;
        const quint8 bid = m_world->blockAt(c.x, c.y, c.z);
        const bool isSeed = (c.x == px && c.y == py && c.z == pz);
        if (bid != BlockRegistry::Painting && !isSeed) continue;        // 非画格 → 不入域
        if (bid == BlockRegistry::Painting
            && quint8(m_world->stateAt(c.x, c.y, c.z) & BlockRegistry::PaintingStateFaceMask) != faceBits)
            continue;                                                    // 异面画（共格平面对墙）→ 不连
        cells.push_back(c);
        if (bid == BlockRegistry::Painting
            && (m_world->stateAt(c.x, c.y, c.z) & BlockRegistry::PaintingStateAnchorFlag)) {
            anchorIndex = m_world->stateAt(c.x, c.y, c.z) & BlockRegistry::PaintingStateIndexMask;
        }
        // 4 向扩展（画面平面内：±u 水平 + ±Y 垂直）。
        frontier.push_back({c.x + ux, c.y, c.z + uz});
        frontier.push_back({c.x - ux, c.y, c.z - uz});
        frontier.push_back({c.x, c.y + 1, c.z});
        frontier.push_back({c.x, c.y - 1, c.z});
    }
    // 掉落：整张画只 1 件 PaintingId（机制等价 MC 破画掉 1 个 painting item）。落点 = 种子格（玩家瞄的格）。
    if (drop)
        emit spawnItem(px, py, pz, RecipeRegistry::PaintingId, 1);
    // 清域：setWaterSilent 静默清（不发 blockBroken —— 多格画的逐格粒子/音会刷成风暴；主破坏格由 caller
    //   finishMiningAt 顶部的 setBlock 已清 + 已发一次事件；失撑路径种子格也在此静默清，同火把失撑模式）。
    //   worldChanged 仍逐格发 → mesh / 呈现层 paintingHost 清孤儿（onWorldChanged 校验）。
    for (const Cell &c : cells) {
        if (c.x == px && c.y == py && c.z == pz && m_world->blockAt(c.x, c.y, c.z) != BlockRegistry::Painting)
            continue; // 种子已被 caller 清（防御双清）
        m_world->setWaterSilent(c.x, c.y, c.z, BlockRegistry::Air, 0);
    }
}

// t721 画作支撑墙失撑掉落（见 playercontroller.h 头注释；finishMiningAt 破块后扫 4 水平邻）。
//   画格的支撑墙 = 画格 - 法线（paintingWallOffset）—— 墙格被破（刚置 Air / Water）→ 该画整张掉落。
//   drop=true 恒发（含创造；t571 自然失撑掉落语义）。
void PlayerController::dropUnsupportedPaintingsAround(int x, int y, int z)
{
    if (!m_world) return;
    constexpr int kHoriz[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (const auto &o : kHoriz) {
        const int px = x + o[0], py = y, pz = z + o[1];
        if (m_world->blockAt(px, py, pz) != BlockRegistry::Painting) continue;
        const quint8 st = m_world->stateAt(px, py, pz);
        const int face = (st & BlockRegistry::PaintingStateFaceMask) >> BlockRegistry::PaintingStateFaceShift;
        int wx = 0, wz = 0;
        BlockRegistry::paintingWallOffset(face, wx, wz);
        // 支撑墙格 == 刚破的格（blockAt 已非 solid —— 直接比坐标，破格可能已置 Air/水等）→ 失撑掉落。
        if (px + wx == x && pz + wz == z)
            removePaintingAt(px, py, pz, face, /*drop=*/true);
    }
}

// t242 攻击 mob（spec「玩家左键攻击生物→受伤音效 + 身体红闪 + 扣血」）：damageEntity 扣血 + 设
//   hurtFlash（EntityManager 内已驱动 QML 红闪绑定刷新）+ swingArm（挥臂反馈）+ emit mobAttacked（→
//   呈现层 playMobHurt，t248 专属 mob 受击声，非玩家 hurt）。
//   t265 伤害改走 ToolRegistry::attackDamage(手持物)：剑木4/石5/铁6、空手与其它工具=kFistDamage(1)。
//   mob health≤0 时 damageEntity 内 emit mobDied → Main.qml Connections 据.mobType 转发到
//   ItemEntityManager.spawnItem 生成猪排 / 皮革 / 牛肉 / 羊毛掉落。索引由 caller findMobHit 选定（最近活体）；
//   二次边界 / dead / 非 Mob 守卫在 damageEntity 内。无 entityManager 时 caller 已早退不会到此处（防御）。
// t248 攻击冷却（kAttackCooldown）：m_attackCooldown>0 时早退（不扣血 / 不挥臂 / 不发信号）。修「长按左键
//   每帧重触 beginMining → mob 瞬秒」——cooldown 把长按连击压成每 0.5s 一次伤害。单次 click 边沿（press）
//   首击 cooldown=0 总生效；后续 tick 在冷却内被吞。成功命中后置 kAttackCooldown。
// t249 暴击 + 击退（spec「受击往攻击方向小跳击退；玩家跳起攻击=暴击 +50% 伤害，research MC crit」）：
//   - 暴击判定（机制等价 MC 1.0 crit）：玩家「滞空下落」态（!onGround && m_vel.y()<0）→ dmg=baseDmg*3/2，
//     否则 baseDmg（baseDmg=ToolRegistry::attackDamage(手持物)）。MC 1.0 crit 精确条件 = falling（向下速度）
//     && !onGround && 不在梯子 / 水；本工程无梯子、水中减速但不下落 → 近似为「滞空下落」即可（spec「research
//     MC crit 计算」）。注意：起跳上升期（vy>0）与地面（onGround）都**不**算暴击 —— 须「跳起后下落途中挥击」
//     才触发，对齐 MC 手感。
//   - 击退方向（spec「往攻击方向」= 玩家→mob 水平方向）：取两 XZ 中心差；重合（玩家骑在 mob 上）→ 用视线
//     水平方向兜底。EntityManager.knockback 内再归一 + 零向量防御。knockback 给 mob 水平冲量 + 小跳垂直冲量。
//   damageEntity 先于 knockback：若本次为击杀（health→0→dead），knockback 内 dead 守卫早退（尸体不弹，
//     deathTimer 死亡动画原地播放）；存活则两者都生效（扣血 + 弹开 + 小跳）。
void PlayerController::attackMob(int entityIndex)
{
    if (m_dead) return;              // t655 死亡态输入闸门：尸体不攻击（beginMining 已拦，此处兜 Q_INVOKABLE 直调）
    if (!m_entityManager) return;
    if (m_attackCooldown > 0.0f) return; // t248 冷却内不扣血（连击 / 长按续触被吞）
    // t249 暴击判定：滞空（!onGround）且下落（vy<0）→ +50% 伤害。
    const bool crit = (!m_onGround && m_vel.y() < 0.0f);
    // t265 攻击伤害按手持物查表（spec「剑→加攻击伤害」）：剑 tier 倍率（木4/石5/铁6），余 = kFistDamage(1)。
    //   旧 t242 固定 kAttackDamage=4 已替换为 ToolRegistry::attackDamage（机制等价 MC 1.0 武器 vs 徒手）。
    //   直读 hotbar.selectedItemId（单一权威，同 updateMining 持物判定，免 QML 绑定滞后窗口）；无 hotbar → 0=空手。
    const int heldItemId = m_hotbar ? m_hotbar->selectedItemId() : 0;
    const int mobType = m_entityManager->mobTypeAt(entityIndex);
    // t476 武器附魔伤害加成（机制等价 MC 1.0 剑附魔：锐锋通用 + 亡灵 / 节肢对族）。在暴击乘算**之前**叠入 base，
    //   即暴击对「base + 附魔加成」整体 ×1.5（与 MC crit 对总伤乘算一致）。base 用 float 累加附魔加成后取整。
    //   - 锐锋 Sharpness：+0.5*level HP（spec「0.5*level per hit」）。
    //   - 亡灵杀手 UndeadSlay：对亡灵族（Shambler 蹒跚者 / Bones 骸骨）+2.5*level HP。
    //   - 节肢克星 ArthropodSlay：对节肢族（Spider 蜘蛛）+2.5*level HP。
    //   互斥组 1 已保证锐锋 / 亡灵 / 节肢三选一（selectEnchants 剔冲突）→ 同一武器至多一类伤害加成生效。
    float dmg = float(ToolRegistry::attackDamage(heldItemId));
    if (m_hotbar) {
        const int sharp = m_hotbar->selectedItemEnchantLevel(EnchantRegistry::Sharpness);
        if (sharp > 0) dmg += 0.5f * float(sharp);
        const bool undead = (mobType == int(EntityManager::MobShambler) || mobType == int(EntityManager::MobBones));
        const bool arthropod = (mobType == int(EntityManager::MobSpider));
        if (undead)     dmg += 2.5f * float(m_hotbar->selectedItemEnchantLevel(EnchantRegistry::UndeadSlay));
        if (arthropod)  dmg += 2.5f * float(m_hotbar->selectedItemEnchantLevel(EnchantRegistry::ArthropodSlay));
    }
    if (crit) dmg *= 1.5f;                      // 暴击 = base × 1.5（含附魔加成）
    const int dmgInt = std::max(1, int(std::round(dmg))); // 至少 1 HP（防御：负 / 零兜底）
    // t249 击退方向：玩家脚底 → mob 中心 的水平向量（未归一，knockback 内归一）。
    const QVector3D mobPos = m_entityManager->posAt(entityIndex);
    float kbx = mobPos.x() - m_pos.x();
    float kbz = mobPos.z() - m_pos.z();
    if (std::sqrt(kbx * kbx + kbz * kbz) < 1e-3f) {
        // 玩家与 mob XZ 重合（骑上 / 正上方）→ 用视线水平方向兜底，避免零向量无击退。
        const QVector3D look = lookDirection();
        kbx = look.x();
        kbz = look.z();
    }
    // t476 击退附魔：每级 +50% 击退冲量（机制等价 MC knockback 附魔 +击退距离）。knockback 内归一方向 × strength。
    const int kbLvl = m_hotbar ? m_hotbar->selectedItemEnchantLevel(EnchantRegistry::Knockback) : 0;
    const float kbStrength = 1.0f + 0.5f * float(kbLvl);
    m_entityManager->damageEntity(entityIndex, dmgInt);
    m_entityManager->knockback(entityIndex, kbx, kbz, kbStrength);
    // t476 燃焰附魔：命中即点燃 mob（机制等价 MC fire-aspect ignite on hit）。fireTimer = level*4s；tick 火烧分支扣血 +
    //   致死掉熟肉。先 damageEntity 再 ignite：若本次击杀（health→0→dead），ignite 内 dead 守卫早退（尸体不燃）。
    const int fireLvl = m_hotbar ? m_hotbar->selectedItemEnchantLevel(EnchantRegistry::FireAspect) : 0;
    if (fireLvl > 0) m_entityManager->ignite(entityIndex, float(fireLvl) * 4.0f);
    // t480 主人攻击 → 驯服狼防御目标 = 本 mob（setWolfTarget 记共享目标：所有驯服且站立的狼追击它，机制等价
    //   MC 1.0 驯服狼攻击主人攻击的怪物）。Game→Entities 向下依赖（同 damageEntity / knockback / ignite 直调）。
    m_entityManager->setWolfTarget(entityIndex);
    // t635 玩家打铁傀儡 → 反击锁定（setGolemRetaliate：golemAngry=true + 刷新记忆 → aiIronGolem 追击玩家 +
    //   近距蓄力重拳上抛，机制等价 MC 1.0 铁傀儡被打反击）。非铁傀儡静默 no-op。同 setWolfTarget 向下依赖模式。
    m_entityManager->setGolemRetaliate(entityIndex);
    emit swingArm();
    // t295 mob 受击音效 + 敌对专属：随 mobAttacked 下传被攻击 mob 的 mobType，供呈现层据它路由到
    //   AudioManager.playMobHurt(mobType) —— 被动走通用 creature yelp、敌对各走专属音（哀嚎/骨头敲击/蜘蛛嘶/嘶嘶）。
    //   mobTypeAt 越界（理论不可达：entityIndex 由 findMobHit 选定的活体 mob）→ 兜底 0（通用 mob_hurt）。
    emit mobAttacked(mobType, crit);
    // t265 剑 / 工具攻击消耗耐久（机制等价 MC「工具每次命中 mob -1 耐久」）。仅 Survival（创造无限源不消耗）；
    //   damageSelectedItem 对空手 / 非工具静默 no-op，耐久归零自动清槽（工具破损消失）。同 finishMiningAt 的耐久消耗模式。
    //   t476 耐久附魔：damageSelectedItem 内按 Unbreaking 等级掷骰跳过部分损耗。
    if (m_mode == Survival && m_hotbar) m_hotbar->damageSelectedItem();
    m_attackCooldown = kAttackCooldown;
}

// t296 玩家受击击退（见头文件 applyHitKnockback 注释；机制等价 MC 1.0 玩家被击退）。由 Main.qml 的
//   EntityManager.mobAttackedPlayer Connections 调（仅 Survival —— 创造 / 观察者无敌不弹，mobAttackedPlayer 经
//   t290 门控本就只在 Survival 发）。水平击退把 (dirX,dirZ) 单位方向 × kHitKnockbackHoriz 写入独立冲量 m_knockback 的 XZ
//   （m_vel.x/z 每 tick 被 wish 输入覆盖，无法存击退，故独立累加 + step() 指数衰减）；垂直小跳 kHitKnockbackUp 直接写入
//   m_vel.y（max，不抵消进行中的跳跃 / 上浮）：m_vel.y 不被 wish 覆盖、由 step() 单一重力积分 + 着地归零自然走完弧线，
//   无需独立冲量。旧版把垂直也放 m_knockback.y 并在 step() 每 tick 再施重力 → 与 m_vel.y 自身重力叠加 = 双重力，水平击退
//   （XZ ~1s 衰减）未衰减完时积分块仍每 tick 跑、把已着地清零的 m_knockback.y 反复拉负 → 叠入 delta.y 把玩家向下拽 →
//   其后的跳跃（m_vel.y=kJump）/ 水中上浮（m_vel.y=kSwimUp）有效向上速度被双重力吃掉、峰值腰斩 = 用户实测「被怪打后
//   跳不起来 / 水里跳不上一格」，~1s 后水平击退衰减完才恢复。改走 m_vel.y 根治（见 step() 击退积分注释）。
//   防御：归一输入（caller 已归一，此处再守）；非 Survival / 死亡 / 未捕获（菜单态）/ 零方向 → 静默早退。
void PlayerController::applyHitKnockback(float dirX, float dirZ)
{
    if (m_mode != Survival) return;      // 创造 / 观察者无敌（防御；mobAttackedPlayer 本就 Survival-only）
    if (m_dead || !m_captured) return;   // 死亡 / 菜单态不弹（防 respawn 后陈旧信号或暂停中被推）
    float len = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (!(std::isfinite(len) && len > 1e-3f)) return; // 零 / 非有限方向 → 无击退（不弹）
    dirX /= len;
    dirZ /= len;
    m_knockback.setX(dirX * kHitKnockbackHoriz);
    m_knockback.setZ(dirZ * kHitKnockbackHoriz);
    // 垂直小跳直接给 m_vel.y（max：不抵消进行中更高的跳跃 / 上浮；向下坠落被转成上弹 = MC 受击小弹）。
    //   m_vel.y 由 step() 单一重力积分 + 着地归零自然走完弧线（不经 m_knockback.y → 无双重力，见上方注释）。
    m_vel.setY(std::max(m_vel.y(), kHitKnockbackUp));
    qInfo("player hit-knockback dir=(%.3f,%.3f) horiz=%.1f", dirX, dirZ, kHitKnockbackHoriz);
}

// t635 铁傀儡重拳上抛（见头文件 applyGolemLaunch 注释；机制等价 MC 1.0 铁傀儡把玩家抛上天）。由 Main.qml 的
//   EntityManager.golemLaunchedPlayer Connections 调（仅 Survival —— mobAttackedPlayer 同帧发的伤害已门控，
//   上抛与伤害同源同门；Creative / Spectator 无敌不抛）。同 applyHitKnockback 的 m_vel.y 直写模式（无双重力），
//   但垂直冲量用大值 kGolemLaunchVy（16 → 峰值 ~4.6 格，落地落差 >3 格必触发摔落伤害，用户口径「4 格以上摔伤」）。
//   防御：归一输入；非 Survival / 死亡 / 未捕获 / 零方向 → 静默早退（同 applyHitKnockback）。
void PlayerController::applyGolemLaunch(float dirX, float dirZ)
{
    if (m_mode != Survival) return;      // 创造 / 观察者无敌（防御；golemLaunchedPlayer 本就 Survival-only）
    if (m_dead || !m_captured) return;   // 死亡 / 菜单态不弹（防 respawn 后陈旧信号或暂停中被推）
    float len = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (!(std::isfinite(len) && len > 1e-3f)) return;
    dirX /= len;
    dirZ /= len;
    m_knockback.setX(dirX * kHitKnockbackHoriz); // 水平分量同受击击退（走开一点再落下，视觉「打飞」）
    m_knockback.setZ(dirZ * kHitKnockbackHoriz);
    m_vel.setY(std::max(m_vel.y(), kGolemLaunchVy)); // 大垂直冲量直写 m_vel.y（峰值 16²/(2·28)≈4.6 格）
    // t655 击飞摔死归属窗口起算（5s，从上抛起算）：窗口内着地摔伤改发 GolemLaunchFall 死因（「被铁傀儡
    //   击飞摔死」），窗口外 / 普通坠落仍 Fall。落地结算摔伤那一刻清零（见 step 着地分支）—— 一次上抛只
    //   归属第一次落地，后续弹跳 / 再坠落不误归属。
    m_golemLaunchTimer = kGolemLaunchAttributionWindow;
    qInfo("player golem-launched dir=(%.3f,%.3f) vy=%.1f", dirX, dirZ, kGolemLaunchVy);
}

// t477 铁砧损坏推进（AnvilUI 每次成功操作后调）。机制等价 MC 1.0 铁砧 12% 概率损坏 —— 本工程取 ~1/3
//   （更易观察损坏链，便于测试 3 阶段 + 碎裂；机制对标非数值 1:1）。据当前阶段经 BlockRegistry::anvilNextStage
//   推进：完好→微损 / 微损→重损 / 重损→Air（碎裂移除）。setBlock 触发 worldChanged（mesh 重建显新顶面贴图）；
//   重损→Air 由 World setBlock 发 blockBroken（破块粒子 / 音，机制等价 MC 铁砧用坏碎裂）。
void PlayerController::damageAnvil(int x, int y, int z)
{
    if (!m_world) return;
    const quint8 cur = m_world->blockAt(x, y, z);
    if (!BlockRegistry::isAnvil(cur)) return;     // 非铁砧（已被破 / 替换）→ no-op
    // ~1/3 概率损坏（bounded(3) ∈ {0,1,2}；==0 即损坏）。
    if (QRandomGenerator::global()->bounded(3) != 0) return;
    const quint8 next = BlockRegistry::anvilNextStage(cur);
    if (next == cur) return;                       // 非铁砧兜底（anvilNextStage 对非铁砧返原 id）
    m_world->setBlock(x, y, z, next);              // 推进阶段（或重损→Air 碎裂；World 发 blockBroken + worldChanged）
}

// t494 熔炉燃烧态切换：翻转 Furnace 格 state 的 FurnaceStateLitFlag（bit2）。FurnaceUI 冶炼 tick 在
//   点燃（有燃料 + 可冶炼输入）/ 熄火（燃料烧尽 / 输入断）边界调本方法 → mesher 据本位切前面贴图
//   14(灭)/134(front_on 带火)。走 5 参数 setBlock 保留低 2 位朝向（id 不变 → 只发 worldChanged 重建 mesh、
//   不发 broken/placed）。已是目标态 / 非 Furnace / 越界 → no-op（避免无谓 worldChanged 刷重建）。
void PlayerController::setFurnaceLit(int x, int y, int z, bool lit)
{
    if (!m_world) return;
    const quint8 cur = m_world->blockAt(x, y, z);
    if (cur != BlockRegistry::Furnace) return;     // 非熔炉（已被破 / 替换）→ no-op
    const quint8 oldState = m_world->stateAt(x, y, z);
    const quint8 newState = lit ? quint8(oldState | BlockRegistry::FurnaceStateLitFlag)
                                : quint8(oldState & quint8(~BlockRegistry::FurnaceStateLitFlag));
    if (newState == oldState) return;              // 已是目标态 → no-op（避免无谓 worldChanged）
    m_world->setBlock(x, y, z, cur, newState);     // id 不变 → 仅 worldChanged 重建 mesh（保留朝向低 2 位）
}

// 每 tick 推进生存挖掘进度（t34）+ 连续续挖（t44）。创造不进入此态（beginMining 内瞬破已 return）。
// spec：progress += dt * speed(block, tool)；speed = 1 / miningTime（ToolRegistry 已含 hardness/speedMul）。
//   - 失命中 / 目标已被破（变 air）→ cancelMining。
//   - 目标换（玩家转头）→ 重置 progress（spec：换目标清零），目标格更新。
//   - stage 推进（progress 跨 1/6 阈值）→ 发 miningStateChanged（驱动 QML 切裂纹贴图）+ swingArm（挥动循环）。
//   - progress >= 1.0（且 canMine）→ finishMiningAt（drop = canHarvest）；基岩 canMine=false 永不破（t141）。
// t44 连续挖掘：finishMiningAt 内 cancelMining 清 m_mining，但 m_leftDown 仍 true（左键未松）。下一 tick
//   顶部检测到「未在累积 + 左键仍按 + Survival + 命中新可挖块」→ 自动 beginMining 新目标（progress 归 0
//   继续），不松手连挖。停止条件：松手（m_leftDown=false）/ 视线无命中（m_hasHit=false，含出射程——
//   raycast 限 kReach）/ 新目标不可挖。spec：长按沿一直线连续破多块直到射程外。速度 / 掉落 / 可挖
//   全走 ToolRegistry（t42 方块表）：canMine(可挖) / miningTime(速度) / canHarvest(掉落)。
void PlayerController::updateMining(float dt)
{
    // t44 连续挖掘：左键仍按但当前未累积（刚破完一块 m_mining 被 cancelMining 清 / 或目标刚进入视线）
    // → 若命中可挖块则自动 beginMining 新目标（progress 归 0），无需松手重按。仅 Survival：Creative
    // 单击瞬破不进 updateMining（beginMining 内直接 finishMiningAt return）；Spectator canBreak=false。
    // 用 ToolRegistry::canMine 判可挖（走 BlockDef：实体且 hardness>0），air / 越界 / 不可破坏 → false。
    if (!m_mining && m_leftDown && m_mode == Survival && m_world && m_hasHit) {
        const quint8 hb = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
        if (ToolRegistry::canMine(hb)) {
            beginMining(); // 重新进入累积：m_mining=true / 目标=当前命中 / progress=0 / stage=0
        }
    }

    if (!m_mining) return;
    if (!m_world || !m_hasHit) { cancelMining(); return; }

    // 目标换：清进度（保留 mining=true），更新目标格。stage 同步重置为 0（裂纹首阶）。
    if (m_hitBx != m_mineBx || m_hitBy != m_mineBy || m_hitBz != m_mineBz) {
        m_mineBx = m_hitBx; m_mineBy = m_hitBy; m_mineBz = m_hitBz;
        m_miningProgress = 0.0f;
        if (m_miningStage != 0) {
            m_miningStage = 0;
            emit miningStateChanged();
        }
        emit miningProgressChanged();
    }

    // 目标变 air（已被其它途径破 / 越界）→ 取消累积。t141：不再对「不可挖（基岩）」取消 —— 基岩保留在
    //   累积挖掘态推 stage（挥臂 + miningParticle 音）给反馈，仅由下方完成守卫 if(canMine(bid)&&progress
    //   >=1.0) 阻止 finishMiningAt（不破）。原 canMine 取消会把基岩立即清出累积态 → 无任何挖掘反馈。
    //   air / 越界 = 已破 / 无目标，取消合理（blockAt 越界返 Air）。
    const quint8 bid = m_world->blockAt(m_mineBx, m_mineBy, m_mineBz);
    if (int(bid) == int(BlockRegistry::Air)) { cancelMining(); return; }

    // t569 红石矿石挖掘触发点亮：正在挖的目标是红石矿 → 置亮（微弱阴沉红光泛出，机制等价 MC「挖掘 /
    //   触碰红石矿发光」）。每 tick 调（已是亮态时 setRedstoneOreLit 内 no-op，零额外 worldChanged）；走到
    //   点亮表续时同 scanRedstoneOre（玩家持续挖 → 光不闪断，松手 / 破块后倒计时自熄）。
    if (BlockRegistry::isRedstoneOre(bid)) {
        setRedstoneOreLit(m_mineBx, m_mineBy, m_mineBz, true);
        const quint64 key = (quint64(quint32(m_mineBx + 0x100000) & 0x1FFFFFu))
                          | (quint64(quint32(m_mineBz + 0x100000) & 0x1FFFFFu) << 21)
                          | (quint64(quint32(m_mineBy) & 0x3FFu) << 42);
        m_redstoneLitCells.insert(key, BlockRegistry::RedstoneOreLitSeconds);
    }

    // t57：手持物品 id **直接读 hotbar（单一权威）**，不读 m_selectedItem 副本。m_selectedItem 经
    //   Q_PROPERTY 绑定 hotbarVM.selectedItemId（NOTIFY=selectedSlotChanged）刷新，但 QML 绑定重算
    //   可能被引擎延迟到下一事件循环 → 同一 tick 内 m_selectedItem 可能滞后于真实选中槽（拾取 / 切槽
    //   边缘与 mining tick 同帧时）。canHarvest / miningTime 是掉落与速度的判定输入，必须用**当下**
    //   选中槽的真实 id：空手挖需工具方块（石 / 圆石）→ canHarvest=false → 不掉落（spec「仅 AIR」）；
    //   持匹配工具 → 掉落。直读 hotbar 消除绑定滞后窗口，保证空手挖石头永不误掉。
    //   m_hotbar 在 QML 绑定注入（同 world / itemEntities）；无 hotbar（极端）→ 0=空手兜底。
    const int heldItemId = m_hotbar ? m_hotbar->selectedItemId() : 0;

    // 速度：miningTime = hardness / speedMul（ToolRegistry），progress 增量 = dt / miningTime。
    // t141：基岩（hardness<0，canMine=false）现也走到此累积路径给反馈 —— miningTime 对 hardness<=0 走
    //   0.05s 地板（不除 hardness），故恒 >0，无除零；基岩 progress 快速达 1.0 但被完成守卫拦下不破。
    const float miningTime = ToolRegistry::miningTime(bid, heldItemId);
    // t476 效率附魔：挖掘速度 ×(1+level)（机制等价 MC efficiency mining speedup）。把 miningTime ÷ 此倍率
    //   → progress 增量同比放大（每级约缩短一半挖掘时间，明显可测）。effMul >= 1（level 0 = 1.0 无加成）。
    const int effLvl = m_hotbar ? m_hotbar->selectedItemEnchantLevel(EnchantRegistry::Efficiency) : 0;
    const float effMul = 1.0f + float(effLvl);
    m_miningProgress += (dt * effMul) / miningTime;

    // t165：不可挖方块（基岩 canMine=false）持续累积给「挥臂」反馈但**不显裂纹**（spec「一直不出现裂纹」）。
    //   可挖方块 progress 到 1.0 → 下方完成守卫 finishMiningAt 破块；不可挖方块 progress 到 1.0 **回绕**
    //   （-=1.0）→ beat 循环 0..5 → 持续跨阶 swingArm（手一直挥），防无界增长溢出。displayed stage：
    //   可挖 = beat（裂纹 0..5），不可挖 = -1（裂纹叠层恒隐）。miningParticle 仅可挖方块迸发（基岩不破无碎屑）。
    const bool mineable = ToolRegistry::canMine(bid);
    if (!mineable && m_miningProgress >= 1.0f) m_miningProgress -= 1.0f; // 基岩 progress 循环（持续挥臂）
    const int beat = std::clamp(int(std::min(m_miningProgress, 1.0f) * 6.0f), 0, 5);
    const int newStage = mineable ? beat : -1; // 不可挖 → 不显裂纹（spec「一直不出现裂纹」）
    if (beat != m_mineBeat) {
        m_mineBeat = beat;
        emit swingArm(); // 跨节拍挥臂（可挖/不可挖均挥；基岩循环 beat → 持续挥动反馈）
        // t165：挖掘击打音对所有被挖方块发（含基岩）—— spec「保持 mining 态挥臂+音」。基岩虽不破，
        //   hold-mine 仍随节拍有挖掘音反馈（机制等价 MC 镐撞基岩响一声）；可挖方块同样发（音统一走本信号）。
        // t231 不可挖方块音节流：基岩 miningTime 走 0.05s 地板 → progress 每 tick 跨多 beat → 本分支每 ~16ms
        //   进一次 → miningSound 每 ~16ms 连发（远快于普通挖掘的几百 ms 节奏）。spec「改与普通挖掘同节奏
        //   （几百 ms 间隔）」：仅对不可挖方块按 m_evtClock 节流到 kMineSoundThrottleMs；可挖方块的 miningTime/6
        //   节奏本就 ≥ 此节流（如手挖石头 ≈250ms/beat），无节流影响、行为不变。初值 -100000 = 远古 → 每次
        //   新挖掘会话首 beat 不受限（beginMining/cancelMining 已归位 m_lastMineSoundMs）。
        const bool emitSound = mineable
            || (m_evtClock.elapsed() - m_lastMineSoundMs >= kMineSoundThrottleMs);
        if (emitSound) {
            if (!mineable) m_lastMineSoundMs = m_evtClock.elapsed();
            emit miningSound(int(bid));
        }
        // t61：每跨一阶迸发少量碎屑（被挖方块色），驱动「挖的过程中」进度反馈粒子。仅可挖方块（基岩
        //   不破无碎屑，故碎屑仍由 mineable 守卫）。bid 上面已读（当前 tick 目标方块 id，setBlock 前原值），
        //   传呈现层复用破块 emitter。音已由上方 miningSound 统一发出（含基岩），此处不再耦合音。
        if (mineable) emit miningParticle(m_mineBx, m_mineBy, m_mineBz, int(bid));
    }
    if (newStage != m_miningStage) {
        m_miningStage = newStage;
        emit miningStateChanged(); // 切裂纹贴图（可挖）/ 隐藏裂纹（不可挖 stage=-1）
    }
    emit miningProgressChanged();

    // 完成：progress 满 → 破块。drop 走 ToolRegistry::canHarvest（生存可采掘判定）。
    //   空手（heldItemId=0 / 非工具）挖需工具方块（石 / 圆石）→ canHarvest=false → 仅 AIR 不掉落；
    //   泥土 / 草 / 木 / 叶 / 沙（NoTool）→ canHarvest=true → 掉落（spec）。
    // finishMiningAt 内 cancelMining 清 m_mining=false；m_leftDown 不动 → 下一 tick 顶部续挖分支接手。
    // t141：完成守卫加 canMine —— 不可挖方块（基岩 hardness<0）即便 progress 满（miningTime 走 0.05s
    //   地板使 progress 快速达 1.0）也**不**调 finishMiningAt（不破）。spec「if(canMine(bid)&&progress
    //   >=1.0) 守 finishMiningAt 不破」。基岩留累积态持续推 stage 反馈（挥臂 + 挖掘音），但永不破。
    if (ToolRegistry::canMine(bid) && m_miningProgress >= 1.0f) {
        const bool drop = ToolRegistry::canHarvest(bid, heldItemId);
        finishMiningAt(m_mineBx, m_mineBy, m_mineBz, drop);
    }
}

// 左键单击（兼容 t05 旧调用 / QML）：等价 beginMining —— 创造瞬破 / 生存开始累积。
// 注：mouseRelease 路径下若仅调用本方法（无对应 endMining），生存会一直累积 —— 故 t34 已把
// MouseButtonRelease 也接入 endMining。直接调本方法的调用者（如有）需自行配对 endMining。
void PlayerController::breakBlock()
{
    beginMining();
}

// t267 右键按下（手持面包）：开始累积进食进度。机制等价 MC 1.0 长按右键食面包（~1.6s 满，kEatDuration）。
//   分流：eventFilter 在 RightButton press 时据持物（hotbar.selectedItemId == BreadId）调本方法而非
//   placeBlock（面包不再单击即食；spec「单击即食→改长按右键」）。模式门控（t21）：观察者不能进食
//   （沿用 placeBlock 入口 canPlace() 守卫；食用是「使用」语义，spectator 不交互）。无需命中（食用是玩家
//   主观动作，不依赖视线命中实体方块，同 t238 旧面包分支语义）。持物变更（press→begin 间切槽）→ 不进。
//   分层（PLAN §2）：进食属 Game/Physics（读持物 + 推进进度 + 写 Hotbar VM + 发语义事件），不改栅格语义。
void PlayerController::beginEating()
{
    if (m_dead) return; // t655 死亡态输入闸门：尸体不进食（连 m_rightDown 都不记，防 respawn 续食）
    // 记录物理右键按下态（置于所有早 return 之前 —— 同 beginMining 的 m_leftDown 模式：即便当前不能开始
    //   进食（观察者 / 未持面包 / 暂停），按钮按下这一事实仍成立，后续 updateEating 据此 + 持面包自动续食）。
    m_rightDown = true;
    if (!canPlace()) return; // 观察者不能进食（沿用 placeBlock 入口门控）
    if (!m_hotbar || !m_captured) return;
    // t467：经 foodHungerAmount 单一权威判「持物是否食物」（面包 / 甜浆果）；非食物 → 不进（仍记 m_rightDown）。
    if (foodHungerAmount(m_hotbar->selectedItemId()) == 0) return;
    // t513 吃完冷却：上一件食物食完的冷却期（m_eatCooldown>0）内 → 不进新一轮累积（按住右键不连食）。
    //   m_rightDown 已记（即便冷却内按下，按钮按下事实成立 → 冷却到 0 后 updateEating 连食分支接手）。
    if (m_eatCooldown > 0.0f) return;
    m_eating = true;
    m_eatingProgress = 0.0f;
    m_eatBeat = -1; // 首拍 0 立即触发屑粒（进食开始的反馈即时）
    emit eatingStateChanged();
    emit eatingProgressChanged();
}

// t267 右键松开：清进食累积进度（未完成不消耗）。非进食态（m_eating=false）→ 仅清 m_rightDown，no-op。
void PlayerController::endEating()
{
    m_rightDown = false;
    if (!m_eating) return;
    cancelEating();
}

// t267 清进食累积态（松开 / 换槽 / 失焦 / 完成）。无变化时静默（不发信号，免抖动 QML 绑定）。
//   t513：亦清 m_eatCooldown（松开右键即放弃连食 → 冷却态随之作废；下次按下重走完整累积）。
void PlayerController::cancelEating()
{
    if (!m_eating) { m_eatCooldown = 0.0f; return; }
    m_eating = false;
    m_eatingProgress = 0.0f;
    m_eatBeat = -1;
    m_eatCooldown = 0.0f; // t513 清冷却（松手 / 换槽即重置连食门）
    emit eatingStateChanged();
    emit eatingProgressChanged();
}

// t267 完成（progress 满）：消耗 1 面包 + 恢复饥饿 + 清态。机制等价 MC 1.0 食面包 +5 hunger。
//   Survival：消耗 1 面包（takeStack 选中槽 1 件）。Creative：饥饿锁满 → +5 被 clamp 截断无变化，且**不消耗**
//   （创造调色板无限源，机制等价 MC 创造食不消耗；同 t238 旧面包分支 + 种子种植创造不耗）。饥饿恢复语义
//   与 t238 旧面包分支一致（clamp；Survival 真增 / Creative 锁满静默），仅触发方式改：单击 → 长按累积满。
//   无条件 emit swingArm（进食完成一次「使用」动作的挥手反馈）+ 刷 m_lastPlaceMs（防 placeBlock 入口 200ms CD
//   与进食完成同帧后立即放块冲突）。t513：finishEating 末据 m_rightDown 决定 —— 仍按 → 置 m_eatCooldown
//   + 保持 m_eating=true（冷却期手持动画持续显示，progress 暂停；冷却到 0 后 updateEating 自动恢复累积连食下一件）；
//   已松 → cancelEating 干净清态（单次进食完成不显示冷却动画）。
void PlayerController::finishEating()
{
    if (!m_hotbar) { cancelEating(); return; }
    // t467：经 foodHungerAmount 取当前食物恢复量（面包 +5 / 甜浆果 +2 / 蘑菇汤 +10；clamp；Survival 真增 / Creative 锁满静默）。
    const int eatenId = m_hotbar->selectedItemId();
    const int amount = foodHungerAmount(eatenId);
    const int nv = std::clamp(m_hunger + amount, 0, int(kMaxHunger));
    if (nv != m_hunger) { m_hunger = nv; emit hungerUpdated(m_hunger); } // 呈现层 → PlayerState.setHunger
    if (m_mode == Survival)
        m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 件食物（创造不耗）
    // t507 蘑菇汤食完返空碗（机制等价 MC 1.0 mushroom_stew 喝完碗留下）：消耗 1 蘑菇汤 + 给回 1 空碗物品。
    //   addStack 智能堆叠（同 id 合并 → 入空槽；木碗 maxStack=64 可堆叠故多数情况并入既有碗槽）。生存 / 创造均给回
    //   （创造不消耗蘑菇汤但碗仍累积，机制等价 MC 创造喝汤也留碗 —— 创造 maxStack=1 蘑菇汤槽恒单件故不影响主流程）。
    if (eatenId == RecipeRegistry::MushroomStewId)
        m_hotbar->addStack(RecipeRegistry::BowlId, 1);
    // t669 毒薯食物中毒：食毒马铃薯 60% 概率起效（机制等价 MC 1.0 poisonous potato 60% 中毒；仅 Survival 结算，
    //   Creative/Spectator 无敌不着毒，同火 / 窒息模式）。起效 → m_poisonTimer=kPoisonDuration（tickImpl 每秒
    //   -1 饥饿 + -1 HP）；期间再食另一毒薯重置时长（可叠续）。中毒视觉复用既有链（fallDamageTaken → damaged
    //   红闪 / 视角晃；hungerUpdated → 鼓腿凹）。
    if (eatenId == RecipeRegistry::PoisonousPotatoId && m_mode == Survival
        && QRandomGenerator::global()->bounded(100) < kPoisonChancePct) {
        m_poisonTimer = kPoisonDuration;
        m_poisonDmgAccum = 0.0f;
    }
    m_lastPlaceMs = m_evtClock.elapsed();
    emit swingArm(); // 进食完成挥手（一次「使用」动作）
    // t513 吃完冷却：置 m_eatCooldown（机制等价 MC 1.0 进食冷却 ~1s）。**不调 cancelEating** —— 保持 m_eating=true
    //   使进食手持动画（手落下 + 嚼动）在冷却期持续显示（spec「手持动画在冷却期显示」），仅 progress 归 0 暂停累积。
    //   冷却期 updateEating 顶部递减 + 不累积；冷却到 0 后 progress 重新从 0 累积（连食下一件）。若右键已松开
    //   （m_rightDown=false）→ updateEating 连食分支不触发（仍持食物但未按）→ m_eating 残留为 true → 顶部持物变更
    //   检查 / 暂停清。为防此悬挂态，finishEating 末据 m_rightDown 决定：仍按 → 留 m_eating 显示冷却动画；
    //   已松 → cancelEating 干净清态（吃一件食完即松手 = 单次进食，不显示冷却动画）。
    m_eatingProgress = 0.0f;
    m_eatBeat = -1; // 冷却结束新一轮累积时首拍 0 立即触发屑粒
    if (m_rightDown) {
        // 仍按住 → 进冷却连食：保持 m_eating=true + 设冷却（updateEating 顶部递减 + 冷却到 0 恢复累积）。
        m_eatCooldown = kEatCooldown;
        emit eatingProgressChanged(); // progress 归 0（HUD / beat 判据刷新；eating 态未变不发 eatingStateChanged）
    } else {
        // 已松手 → 单次进食完成：cancelEating 干净清态（清 m_eating + 冷却，发 eatingStateChanged 停动画）。
        cancelEating();
    }
}

// t267 持续进食：每 tick 累积进度 / 检持物变更 / 跨节拍发屑粒 / 完成时消耗面包。由 tick() 调（captured 时）。
//   机制等价 MC 1.0 长按右键食面包：progress 增量 = dt / kEatDuration（~1.6s 满）。
//   连食（同 t44 连续挖掘族）：finishEating 消耗后保持 m_eating=true + 设 m_eatCooldown（t513），右键仍按住
//   （m_rightDown）→ 冷却期 progress 暂停（手持动画持续），冷却到 0 后自动恢复累积连食下一件（不松手连食，
//   机制等价 MC 按住右键连食多件但件间有 ~1s 冷却）。持物变（切槽 / 面包耗尽后未松手）→ 取消进食（同挖掘
//   目标变更清进度，spec「换槽清零」）。
//   t513 冷却期：m_eating=true 但 m_eatCooldown>0 → 不累积 progress、不发屑粒（嚼动暂停视觉化冷却），仅递减冷却。
void PlayerController::updateEating(float dt)
{
    // t513 冷却递减（独立于 m_eating —— 即便暂停 / 失焦后冷却亦应自然走完，避免恢复后卡陈旧冷却值；同 m_attackCooldown）。
    if (m_eatCooldown > 0.0f) {
        m_eatCooldown -= dt;
        if (m_eatCooldown < 0.0f) m_eatCooldown = 0.0f;
    }

    // 连食：右键仍按但当前未进食（刚吃完一件被 cancelEating 清 / 或持食物后按下时 beginEating 因某早 return
    //   未进）→ 若仍持食物 + 可进食 + **不在冷却期** → 自动 beginEating（progress 归 0）。仅非 spectator。
    //   t513：beginEating 内已守 m_eatCooldown>0 早退，故冷却期不会误启；此处条件不变（冷却态 m_eating 保持 true
    //   不进此分支，仅在 finishEating 走 cancelEating 即单次进食后仍按时此分支接手，且 beginEating 自判冷却挡）。
    if (!m_eating && m_rightDown && canPlace() && m_hotbar
        && foodHungerAmount(m_hotbar->selectedItemId()) > 0) {
        beginEating();
    }

    if (!m_eating) return;
    if (!m_hotbar || !m_captured) { cancelEating(); return; }
    // 持物变（切槽 / 食物耗尽换非食物）→ 取消进食（同挖掘目标变更清进度）。t467 经 foodHungerAmount 单一权威判食物。
    if (foodHungerAmount(m_hotbar->selectedItemId()) == 0) { cancelEating(); return; }

    // t513 冷却期：progress 暂停（不累积、不发屑粒），仅保留 m_eating=true 使手持动画持续显示冷却态。
    //   冷却到 0 后下一 tick 自然恢复下方累积路径。progress 已在 finishEating 归 0 + eatBeat=-1，冷却结束首轮
    //   累积使 beat 0 != -1 立即发屑粒（新一轮进食的反馈即时）。
    if (m_eatCooldown > 0.0f) return;

    m_eatingProgress += dt / kEatDuration;
    // 跨节拍屑粒：progress×kEatBeats 跨阶时发 eatingParticle（嘴部 = 玩家眼位 position()）。
    //   beat 从 -1 起 → 首拍 0 在进食开始后首个 tick 立即触发（反馈即时）；之后每 ~0.4s 一拍（kEatBeats=4）。
    //   屑粒不爆量：单拍 burst 少量（BlockParticles.burstEat 内 burst(3,60)），kEatBeats 段封顶总迸发数。
    //   t513：携当前食物 id → QML 据此按食物取屑粒色（替换旧固定面包色占位）。
    const int beat = std::clamp(int(m_eatingProgress * float(kEatBeats)), 0, kEatBeats);
    if (beat != m_eatBeat) {
        m_eatBeat = beat;
        const QVector3D mouth = position(); // 眼位 ≈ 嘴部（屑粒从嘴迸发）
        emit eatingParticle(mouth.x(), mouth.y(), mouth.z(), m_hotbar->selectedItemId());
    }
    emit eatingProgressChanged();

    // 完成：progress 满 → 消耗 1 食物 + 恢复饥饿。finishEating 设冷却（仍按时）或 cancelEating（已松时）；
    //   m_rightDown 不动 → 仍按时冷却到 0 后恢复累积（连食下一件）/ 已松即停。
    if (m_eatingProgress >= 1.0f) {
        finishEating();
    }
}

// t304 弓蓄力进度（Q_PROPERTY bowDrawProgress READ）：钳到 [0,1]（满弓=1）。无拉弓态 → 0。
float PlayerController::bowDrawProgress() const
{
    if (!m_bowDrawing || kBowFullCharge <= 0.0f) return 0.0f;
    return std::clamp(m_bowDrawTime / kBowFullCharge, 0.0f, 1.0f);
}

// t304 右键按下（手持弓）：开始拉弓蓄力。机制等价 MC 1.0 长按右键拉弓（~1s 满弓，kBowFullCharge）。
//   分流：eventFilter RightButton press 据持物（hotbar.selectedItemId == Bow）调本方法而非 placeBlock（弓非方块，
//   selectedBlock 已守 Air）。模式门控：观察者不能拉弓（沿用 placeBlock 入口 canPlace() 守卫；射箭是「使用」
//   语义，spectator 不交互）。无需命中（弓瞄准走视线方向，不依赖射线命中实体方块）。持物变更（press→begin 间
//   切槽）→ 不进。蓄力期间 step() 水平速度 ×kBowSlowMul（spec「拉弓减速」）。
// t322 生存拉弓须背包有箭（机制等价 MC 1.0 生存弓无箭不可拉）；创造射箭免费（不查箭）。
void PlayerController::beginBowDraw()
{
    if (m_dead) return; // t655 死亡态输入闸门：尸体不拉弓
    // 记录物理右键按下态已在 eventFilter（endBowDraw 据松开边缘触发，不依赖此）；m_rightDown 由面包路径管理，
    //   弓与面包持物互斥不复用。置于所有早 return 之前的是 m_rightDown（面包路径），此处弓路径独立。
    if (!canPlace()) return; // 观察者不能拉弓（沿用 placeBlock 入口门控）
    if (!m_hotbar || !m_captured) return;
    if (m_hotbar->selectedItemId() != int(ToolRegistry::Bow)) return; // 持物非弓 → 不进
    // t322：生存须背包有箭才可拉弓（机制等价 MC 1.0 生存弓无箭不可拉；创造射箭免费不查）。
    if (m_mode == Survival && !findArrowInInventory().found) return;
    if (m_bowDrawing) return; // 已在拉弓 → 不重置（防重复 press 抖动重置进度）
    m_bowDrawing = true;
    m_bowDrawTime = 0.0f;
    emit bowDrawChanged();
}

// t304 右键松开：据蓄力射箭 + 清拉弓态。蓄力 < kBowMinChargeRatio / 无箭（生存）→ 不射（仅 cancel）。
//   射箭：算蓄力比 → 箭速（lerp min..max）+ 伤害（lerp min..max）；vel = 视线方向 × 箭速（pitch 已含 → 抛物由
//   Arrow tick 重力生成）；origin = 眼位 + 视线 × 0.6（防贴墙 spawn 入墙即没）。生存消耗 1 箭 + 弓 -1 耐久
//   （创造不耗）。射出后 swingArm（射箭挥手反馈）。
// t322：生存须背包有箭才可射（每发消耗 1，机制等价 MC 1.0）；创造射箭免费（不查箭 / 不消耗 / 不损耐久）。
void PlayerController::endBowDraw()
{
    if (!m_bowDrawing) return; // 非拉弓态（面包松开 / 误触）→ no-op
    const float prog = bowDrawProgress();
    cancelBowDraw(); // 清拉弓态（无论射否；松开即结束蓄力）
    // 蓄力不足 → 取消（不射，机制等价 MC「未拉足松开箭无力」）。
    if (prog < kBowMinChargeRatio) return;
    if (!m_world || !m_entityManager) return;
    // t322：生存须背包有箭才可射 + 定位消耗槽；创造免费（不查箭，无限源射）。
    ArrowSlot ar{false, 0, 0};
    if (m_mode == Survival) {
        if (!m_hotbar) return;
        ar = findArrowInInventory();
        if (!ar.found) return;
    }
    // 箭速 / 伤害按蓄力 lerp（charge² 让满弓手感更利：短蓄力箭慢弱、满弓快强）。
    const float charge = std::clamp(prog, 0.0f, 1.0f);
    const float speed = std::lerp(kBowMinSpeed, kBowMaxSpeed, charge * charge);
    const int damage = kBowMinDamage + int(std::round(charge * float(kBowMaxDamage - kBowMinDamage)));
    const QVector3D look = lookDirection();
    const QVector3D origin = position() + look * 0.6f; // 眼位 + 视线前移 0.6（防贴墙入墙）
    m_entityManager->spawnArrowPlayer(origin, look * speed, damage);
    // 生存消耗 1 箭 + 弓 -1 耐久；创造不耗（无限源）。
    if (m_mode == Survival) {
        if (ar.group == 0) {
            m_hotbar->takeStack(ar.index, 1);
        } else {
            // main 段无 takeStack：手读 count - 1 写回（归 0 清 id，保持「id==0 ⟺ count==0」不变式）。
            const int nc = m_hotbar->mainCountAt(ar.index) - 1;
            m_hotbar->mainSetStack(ar.index, nc > 0 ? RecipeRegistry::ArrowId : 0, nc > 0 ? nc : 0);
        }
        m_hotbar->damageSelectedItem(); // 弓 -1 耐久（归零自动清槽）
    }
    emit swingArm(); // 射箭挥手反馈（一次「使用」动作）
}

// t304 清弓拉弓累积态（松开射出后 / 换槽（持物不再是弓）/ 失焦 / 暂停）。无拉弓态时静默（不发信号）。
void PlayerController::cancelBowDraw()
{
    if (!m_bowDrawing) return;
    m_bowDrawing = false;
    m_bowDrawTime = 0.0f;
    emit bowDrawChanged();
}

// t401 钓鱼竿抛 / 拉切换（手持钓竿右键按下边缘触发；机制等价 MC 1.0 右键钓竿抛 / 收）。单次切换非长按：
//   未钓 → 抛浮标入水（视线 DDA HitWater 命中首个水格 → 浮标落水面；命中非水 / 无水 → 不抛）；已钓 → 拉起
//   （咬钩则按 LootTable::fishingPool 抽一件获物落为掉落实体 + 生存钓竿 -1 耐久，否则空收）。
void PlayerController::useFishingRod()
{
    if (m_dead) return; // t655 死亡态输入闸门：尸体不抛竿
    if (m_fishing) {
        // 拉起：快照咬钩态 / 浮标位（cancelFishing 会清），再据咬钩决定获物 / 空收。
        const bool bite = m_hasBite;
        const QVector3D bp = m_bobberPos;
        cancelFishing(); // 收浮标（无论获物否；拉起即结束）
        emit swingArm(); // 拉起挥手反馈（一次「使用」动作）
        if (!bite) return; // 未咬钩 → 空收（无获物 / 不损耐久）
        if (!m_world) return;
        // 咬钩 → 按 fishingPool 抽一件获物（roll 1 次；RNG 用运行期随机 → 每次拉起不同）。
        const auto &pool = LootTable::fishingPool();
        const quint32 seed = QRandomGenerator::global()->generate();
        const std::vector<LootTable::Stack> stacks = LootTable::roll(pool, 1, seed);
        if (!stacks.empty() && stacks[0].itemId != 0 && stacks[0].count > 0) {
            // 获物落为掉落实体（浮标整数格；Main.qml Connections 转发到 ItemEntityManager.spawnItem）。
            const int bx = int(std::floor(bp.x()));
            const int by = int(std::floor(bp.y()));
            const int bz = int(std::floor(bp.z()));
            emit fishCaught(stacks[0].itemId, stacks[0].count, bx, by, bz);
            // 生存钓竿 -1 耐久（归零自动清槽，同弓 / 镐）；创造不消耗（无限源）。
            if (m_mode == Survival && m_hotbar) m_hotbar->damageSelectedItem();
        }
        return;
    }
    // 抛竿：沿视线 DDA（HitWater）找首个水格；无世界 / 无水命中（墙挡在前 / 射程内无水）→ 不抛。
    if (!m_world) return;
    const RayHit hit = raycastVoxel(*m_world, position(), lookDirection(), kFishCastRange, RayFilter::HitWater);
    if (!hit.valid) return;
    if (m_world->blockAt(hit.bx, hit.by, hit.bz) != quint8(BlockRegistry::Water)) return; // 命中非水 → 不抛
    // 浮标落水格顶面（略下沉表「浮在水面」）；m_biteTimer 随机咬钩倒计时（kFishBiteMin..Max 秒）。
    m_bobberPos = QVector3D(float(hit.bx) + 0.5f, float(hit.by) + 0.875f, float(hit.bz) + 0.5f);
    m_fishing = true;
    m_hasBite = false;
    m_biteTimer = kFishBiteMin + float(QRandomGenerator::global()->generateDouble()) * (kFishBiteMax - kFishBiteMin);
    emit fishingChanged();
    emit swingArm(); // 抛竿挥手反馈
}

// t401 持续钓鱼：每 tick 累积咬钩倒计时。换槽（持物不再是钓竿）→ cancel。机制等价 MC 1.0 抛竿后等若干秒咬钩。
//   m_biteTimer 两阶段复用：>0 = 等待咬钩倒计时（→0 即咬钩，重置为 kFishBiteWindow）；咬钩后 >0 = 咬钩窗口
//   （→0 即窗口过期、鱼跑了 → cancelFishing 空收）。仅 captured 时跑（pause 早 return 之前已 cancelFishing）。
void PlayerController::updateFishing(float dt)
{
    if (!m_fishing) return;
    // 换槽（持物不再是钓竿）→ 收浮标（机制等价 MC 切物品即收竿）。
    if (!m_hotbar || m_hotbar->selectedItemId() != int(ToolRegistry::FishingRod)) { cancelFishing(); return; }
    m_biteTimer -= dt;
    if (m_biteTimer > 0.0f) return;
    if (!m_hasBite) {
        // 等待阶段到点 → 咬钩：进入咬钩窗口（m_biteTimer 重置为窗口时长，m_hasBite=true）。
        m_hasBite = true;
        m_biteTimer = kFishBiteWindow;
        emit fishingChanged(); // 呈现层据 hasBite 让浮标下沉 / 抖动
    } else {
        // 咬钩窗口过期（玩家未及时拉）→ 鱼跑了 → 空收。
        cancelFishing();
    }
}

// t401 清钓鱼态（拉起后 / 换槽（持物不再是钓竿）/ 失焦 / 暂停 / 重生）。无钓鱼态时静默（不发信号）。
void PlayerController::cancelFishing()
{
    if (!m_fishing) return;
    m_fishing = false;
    m_hasBite = false;
    m_biteTimer = 0.0f;
    emit fishingChanged();
}

// t457 自动跳清晨（Settled 阶段计时满 / 内部调）：worldClock.skipToDawn + 设 m_spawnPos=床位 + 进 Waking 阶段
//   （fade-in 平滑回显清晨场景）。与 wakeUpFromBed（按钮，不跳清晨）共用 Waking fade-in，差异仅在 skipToDawn + spawn。
void PlayerController::sleepAdvanceToDawn()
{
    // 跳清晨（WorldClock 时间向前快进到黎明 phase；PLAN §2-H 时间单向，只加 m_elapsedMs）。
    if (m_worldClock) m_worldClock->skipToDawn();
    // 设重生点 = 床位（床格中心 + 上方 1.0 = 玩家站床顶；respawn 时 snapSpawnToGround 再贴地表兜底）。
    m_spawnPos = QVector3D(float(m_sleepBx) + 0.5f, float(m_sleepBy) + 1.0f, float(m_sleepBz) + 0.5f);
    emit spawnPointChanged();   // t567 HUD 指南针指针重算（出生点 → 床位）
    m_sleepPhase = kSleepPhaseWaking;
    m_sleepPhaseTimer = 0.0f;
    // 离开 Settled（隐藏起床按钮）；fade/lie 保持满值（Waking 内从 1 渐降到 0）。
    if (m_sleepSettled) { m_sleepSettled = false; emit sleepSettledChanged(); }
}

// t388/t457 尝试在命中床 (bx,by,bz) 入睡（placeBlock useBlock 床分支调）。机制等价 MC 1.0 床：夜间 + 床周无怪物才睡。
void PlayerController::trySleepAt(int bx, int by, int bz)
{
    if (m_sleeping) return; // 睡觉序列进行中再右键无效（防重入）
    // 夜间判定（WorldClock.isNight 纯函数；无 worldClock → 当非夜间拒绝，安全降级）。
    if (!m_worldClock || !m_worldClock->isNight()) {
        emit sleepRefused(QStringLiteral("只有在夜晚才能睡觉"));
        return;
    }
    // 床周敌对判定（EntityManager.hostileNearby；球心=床格中心，机制等价 MC 床周 8 格内有敌对即不能睡）。
    if (m_entityManager) {
        const QVector3D bedCenter(float(bx) + 0.5f, float(by) + 0.5f, float(bz) + 0.5f);
        if (m_entityManager->hostileNearby(bedCenter, kSleepMonsterRadius)) {
            emit sleepRefused(QStringLiteral("附近有怪物，无法入睡"));
            return;
        }
    }
    // 通过 → 进 Lying 阶段（玩家相机降低 + 渐黑过渡；spec「右键床→玩家躺下→渐黑过渡 ~1s fade」）。
    m_sleeping = true;
    m_sleepPhase = kSleepPhaseLying;
    m_sleepPhaseTimer = 0.0f;
    m_sleepFade = 0.0f;
    m_sleepLie = 0.0f;
    m_sleepSettled = false;
    m_sleepBx = bx; m_sleepBy = by; m_sleepBz = bz;
    emit sleepingChanged();
}

// t388/t457 持续睡觉三阶段状态机（tickImpl 调）：Lying（躺下渐黑）→ Settled（全黑显起床按钮，自动跳清晨）
//   → Waking（fade 回显）。sleepFade/Lie/Settled 据阶段 + 计时派生，值真变才 emit（驱动 QML 黑叠层 + 相机躺姿 + 按钮）。
void PlayerController::updateSleep(float dt)
{
    if (!m_sleeping) return;
    m_sleepPhaseTimer += float(dt);

    // 计算本 tick 阶段派生的目标 fade / lie / settled（与旧值比对，真变才 emit，免每帧抖 QML 绑定）。
    float fade = m_sleepFade, lie = m_sleepLie;
    bool settled = m_sleepSettled;

    const auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };

    if (m_sleepPhase == kSleepPhaseLying) {
        // Lying：fade/lie 0→1（躺下 + 渐黑，spec「~1s fade」）。
        const float t = clamp01(m_sleepPhaseTimer / kSleepLieDur);
        fade = t; lie = t; settled = false;
        if (m_sleepPhaseTimer >= kSleepLieDur) {
            // 进 Settled（全黑 + 显起床按钮）。
            m_sleepPhase = kSleepPhaseSettled;
            m_sleepPhaseTimer = 0.0f;
            fade = 1.0f; lie = 1.0f; settled = true;
        }
    } else if (m_sleepPhase == kSleepPhaseSettled) {
        // Settled：全黑 + 起床按钮；玩家不按则计时满自动跳清晨（spec「不按则度过夜晚（跳到黎明）」）。
        fade = 1.0f; lie = 1.0f; settled = true;
        if (m_sleepPhaseTimer >= kSleepSettleDur) {
            sleepAdvanceToDawn(); // skipToDawn + spawn + 进 Waking（fade/lie 在 Waking 内从 1 降到 0）
            fade = 1.0f; lie = 1.0f; settled = false; // sleepAdvanceToDawn 已发 sleepSettledChanged
        }
    } else if (m_sleepPhase == kSleepPhaseWaking) {
        // Waking：fade/lie 1→0（平滑回显清晨 / 当前场景，spec「非瞬醒」）。
        const float t = clamp01(m_sleepPhaseTimer / kSleepWakeDur);
        fade = 1.0f - t; lie = 1.0f - t; settled = false;
        if (m_sleepPhaseTimer >= kSleepWakeDur) {
            // fade 回显完毕 → 结束整个睡觉序列。
            cancelSleep(); // 瞬切 None + sleeping=false（fade 已 0，黑叠层随 sleeping=false 隐藏无跳变）
            return;
        }
    }

    // 派生值真变才 emit（免每 tick 抖 QML 绑定；同 miningProgressChanged 高频但值稳态时静默）。
    if (fade != m_sleepFade) { m_sleepFade = fade; emit sleepFadeChanged(); }
    if (lie != m_sleepLie)   { m_sleepLie = lie;   emit sleepLieChanged(); }
    if (settled != m_sleepSettled) { m_sleepSettled = settled; emit sleepSettledChanged(); }
}

// t388/t457 清整个睡觉态（受惊醒 wakeUp / 失焦 / 暂停 / 重生 / Waking 完成）：瞬切 None + sleeping=false。
//   非睡觉态静默（不发信号，免抖动 QML 绑定）。中断式瞬醒（紧急打断，黑叠层随 sleeping=false 立即隐藏）。
void PlayerController::cancelSleep()
{
    if (!m_sleeping) return;
    m_sleeping = false;
    m_sleepPhase = kSleepPhaseNone;
    m_sleepPhaseTimer = 0.0f;
    m_sleepFade = 0.0f;
    m_sleepLie = 0.0f;
    const bool wasSettled = m_sleepSettled;
    m_sleepSettled = false;
    emit sleepingChanged(); // 驱动 QML 黑叠层 / 相机躺姿 / 起床按钮全部随 sleeping=false 复位
    if (wasSettled) emit sleepSettledChanged();
}

// t388 受惊醒（Q_INVOKABLE；呈现层 Connections 据玩家受击 fallDamageTaken / mobAttackedPlayer 路由调）。
//   spec「受击即醒」。受击是紧急打断 → 瞬切（cancelSleep），区别于 wakeUpFromBed 的平滑 fade-in 醒。
//   非睡觉态静默（cancelSleep 自守）。
void PlayerController::wakeUp()
{
    cancelSleep();
}

// t457 平滑起床（Q_INVOKABLE；QML「起床」按钮调）：从 Settled 阶段平滑过渡到 Waking（fade 1→0 渐显），不跳清晨
//   （玩家选择立即醒 = 仍处夜晚，spec「按则立即醒」）。非 Settled 阶段调无效（防重入 / 防误触）。
void PlayerController::wakeUpFromBed()
{
    if (!m_sleeping || m_sleepPhase != kSleepPhaseSettled) return;
    m_sleepPhase = kSleepPhaseWaking;
    m_sleepPhaseTimer = 0.0f;
    if (m_sleepSettled) { m_sleepSettled = false; emit sleepSettledChanged(); }
    // fade/lie 保持满值（Waking 内从 1 渐降到 0）；不调 skipToDawn / 不设 spawn（区别于 sleepAdvanceToDawn）。
}

// t715 施加状态效果（/effect 命令入口；机制见头注释）：转交对应时序源，tickImpl 统一推进 + 快照广播。
//   seconds<=0 → 清该效果（同一入口双语义，机制等价 MC /effect <type> 0 清除）。仅 Survival 生效（无敌模式
//   不吃效果，同火 / 中毒门控；非 Survival 调用静默丢弃 —— 命令回显由 QML 侧照常输出）。
void PlayerController::applyStatusEffect(int effect, float seconds, int level)
{
    if (m_mode != Survival) return;
    const float secs = seconds < 0.0f ? 0.0f : seconds;
    const int lvl = level < 1 ? 1 : level;
    switch (effect) {
    case PlayerState::EffectPoison:
        // 中毒：复用 m_poisonTimer（毒马铃薯同源；伤害周期 / 递减全走既有 tickImpl 分支，保持行为不回归）。
        //   伤害累积器同步重置（新一次中毒起算）。
        m_poisonTimer = secs;
        m_poisonDmgAccum = 0.0f;
        break;
    case PlayerState::EffectSlowness:
        // 缓慢：m_slowTimer（t715 新时序源；step() 走路分支 ×kSlowSpeedMul）。
        m_slowTimer = secs;
        m_slowLevel = (secs > 0.0f) ? lvl : 0;
        break;
    case PlayerState::EffectFire:
        // 着火：复用 m_fireTimer（岩浆 / 火点燃同源；火烧伤害 / 随机熄灭 / burningChanged 全走既有分支）。
        //   m_fireDmgTimer 不动（t351：泡岩浆伤害稳定语义 —— 外部施加只刷 fireTimer）。
        m_fireTimer = secs;
        if (secs <= 0.0f) m_fireDmgTimer = 0.0f;
        break;
    default:
        break; // EffectNone / 未知 → 忽略
    }
}

// t715 清全部状态效果（/effect clear；重生 / 存档加载同源调内部字段清，见各处注释）。
void PlayerController::clearStatusEffects()
{
    m_poisonTimer = 0.0f;
    m_poisonDmgAccum = 0.0f;
    m_slowTimer = 0.0f;
    m_slowLevel = 0;
    m_fireTimer = 0.0f;
    m_fireDmgTimer = 0.0f;
    if (m_burning) { m_burning = false; emit burningChanged(); } // t344 火焰叠层同步隐
}

// t715 组装当前活跃效果列表（固定序 Poison / Slowness / Fire；每项 {type, seconds, level}，seconds 取
//   ceil（剩余整秒，向上取整：HUD 显「还剩 N 秒」在 N.0..N.9 期间显 N+1 直到真正跨过整秒边界））。
//   快照在 tickImpl 末与 m_lastEffectSigCache 深比较，真变才 emit activeEffectsChanged（呈现层路由
//   PlayerState.setActiveEffects）—— 每效果每秒最多 1 次 emit，无每帧抖动。
QVariantList PlayerController::buildActiveEffects() const
{
    QVariantList list;
    if (m_mode == Survival) {
        if (m_poisonTimer > 0.0f) {
            QVariantMap m;
            m.insert(QStringLiteral("type"), int(PlayerState::EffectPoison));
            m.insert(QStringLiteral("seconds"), int(std::ceil(m_poisonTimer)));
            m.insert(QStringLiteral("level"), 1);
            list.append(m);
        }
        if (m_slowTimer > 0.0f) {
            QVariantMap m;
            m.insert(QStringLiteral("type"), int(PlayerState::EffectSlowness));
            m.insert(QStringLiteral("seconds"), int(std::ceil(m_slowTimer)));
            m.insert(QStringLiteral("level"), m_slowLevel > 0 ? m_slowLevel : 1);
            list.append(m);
        }
        if (m_fireTimer > 0.0f) {
            QVariantMap m;
            m.insert(QStringLiteral("type"), int(PlayerState::EffectFire));
            m.insert(QStringLiteral("seconds"), int(std::ceil(m_fireTimer)));
            m.insert(QStringLiteral("level"), 1);
            list.append(m);
        }
    }
    return list;
}

// t304 在背包（hotbar 9 + main 27）查首格含箭（ArrowId）。优先 hotbar（入手语义同拾取），再 main。
//   返 {found, group(0=hotbar/1=main), index}。找不到 found=false。供 endBowDraw 判「需箭」+ 生存消耗定位槽。
PlayerController::ArrowSlot PlayerController::findArrowInInventory() const
{
    if (!m_hotbar) return {false, 0, 0};
    for (int i = 0; i < m_hotbar->slotCount(); ++i) {
        if (m_hotbar->blockIdAt(i) == RecipeRegistry::ArrowId)
            return {true, 0, i};
    }
    for (int i = 0; i < m_hotbar->mainCount(); ++i) {
        if (m_hotbar->mainBlockIdAt(i) == RecipeRegistry::ArrowId)
            return {true, 1, i};
    }
    return {false, 0, 0};
}

// 右键：命中面法线方向的相邻空格置当前手持方块。
// 校验：目标格须为空气（不覆盖实体）；且不与玩家 AABB 重叠（防自埋/卡死）。
// 模式门控（t21）：观察者不能放块（用户核心诉求）——在调用 World::setBlock 前拦截。
// t29：放块动作真发生（通过门控 + 目标为空 + 不埋玩家，实际写入）时才发 swingArm；
// 已有方块/重叠被拒不发（仅动作真发生时，spec）。
// t50：命中格若为工作台（CraftingTable）→ 右键打开 3×3 合成 UI（不放置；发 craftingTableOpened）。
// 机制等价 MC 右键工作台。在所有放置校验之前拦截（无论手持何物，右键工作台即开界面）。
// t87：命中格若为熔炉（Furnace）→ 右键打开 FurnaceUI（不放置；发 furnaceOpened），同工作台模式。
void PlayerController::placeBlock()
{
    if (m_dead) return; // t655 死亡态输入闸门：尸体不放块 / 不进食 / 不开容器（右键全路径拒）
    // t128：放置 CD（200ms = 5 次/秒）防连点放沙等触发多次塌落链溢出。spec「入口 if(now - m_lastPlaceMs
    //   < 200) return」；仅成功放置后刷新 m_lastPlaceMs（下方 setBlock 后）。now 走 m_evtClock（事件
    //   时间戳，与双击检测 m_lastSpaceMs/m_lastWms 同源）。初值 -100000 = 远古 → 首次放置不限。
    const qint64 now = m_evtClock.elapsed();
    if (now - m_lastPlaceMs < 200) return;
    if (!canPlace()) return; // 观察者不能放块
    if (!m_world || !m_captured) return;
    // t174：空桶舀水用独立含水射线（见下方桶分支），水下 / 瞄深水时主射线无实体命中（m_hasHit=false）
    //   亦须可舀，故 !m_hasHit 不在此一刀切拦截。下方工作台/熔炉/门/活版门与放块路径仍需命中 → 局部门控。
    if (m_hasHit) {
    // t523 sneak(shift)+右键功能方块 → 放方块而非开界面（用户「想在熔炉上面放方块，shift+右键直接放
    //   方块，而不是右键打开熔炉界面」）。机制等价 MC 1.0：按住 shift 右键**容器 / 功能 UI 方块**（工作台 /
    //   熔炉 / 箱子 / 附魔台 / 铁砧）跳过「打开界面」useBlock，改走放置路径（把选中方块放在该功能方块朝玩家
    //   的面相邻空格）。判据 = Key_Shift 原始按下态（m_keys，§2-D 单一输入路径），覆盖所有模式（生存蹲 / 创造
    //   飞态 shift 下降 / 创造走），非 m_moveState==Crouch（后者飞态不进蹲 → 飞态 shift+右键会失效，与 MC 不符）。
    //   只绕过「开界面」类 useBlock（工作台 / 熔炉 / 箱子 / 附魔台 / 铁砧）；门 / 活版门 / 床 / 机关 / 浆果丛 / 末地门 /
    //   传送门等其它 useBlock **不绕过**（机制等价 MC shift 右键门仍开门、床仍睡 —— 这些非「容器 UI」语义，
    //   shift 不改变其交互）。空手 sneak+右键功能方块 → 下方 m_selectedBlock==Air 守卫拦（不放置不挥手），
    //   机制等价 MC 空手 shift 右键箱子无效应。
    const bool sneakPlace = m_keys.value(Qt::Key_Shift);
    // t50：右键工作台 → 打开 3×3 合成 UI（优先于放置；spec「右键工作台开 3×3」）。
    if (!sneakPlace && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::CraftingTable) {
        emit craftingTableOpened();
        return;
    }
    // t87：右键熔炉 → 打开 FurnaceUI 冶炼界面（同工作台模式：优先于放置，无论手持何物右键熔炉即开）。
    if (!sneakPlace && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Furnace) {
        emit furnaceOpened(m_hitBx, m_hitBy, m_hitBz);
        return;
    }
    // t173/t179：右键箱子 → 打开 ChestUI 物品栏（同工作台 / 熔炉模式：优先于放置，无论手持何物右键箱子
    //   即开）。发 chestOpened(x,y,z) 携命中格世界坐标 → 呈现层 Connections 打开 ChestUI（释放指针 +
    //   盖子开合动画）；ChestStore 据坐标寻址该箱子的 27 槽。机制等价 MC 右键箱子开物品栏。
    if (!sneakPlace && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Chest) {
        emit chestOpened(m_hitBx, m_hitBy, m_hitBz);
        return;
    }
    // t474：右键附魔台 → 打开 EnchantingUI 附魔界面（同工作台 / 熔炉 / 箱子模式：优先于放置，无论手持
    //   何物右键附魔台即开）。发 enchantingTableOpened(x,y,z) 携命中格世界坐标 → 呈现层 Connections 打开
    //   EnchantingTableUI（释放指针）；UI 据坐标查 World.countBookshelvesAround 算书架加成 → 提升可选
    //   附魔等级上限（机制等价 MC 1.0 附魔台书架 power）。空手亦可（开界面是「使用」语义，与手持何物无关）。
    if (!sneakPlace && BlockRegistry::isEnchantingTable(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {
        emit enchantingTableOpened(m_hitBx, m_hitBy, m_hitBz);
        return;
    }
    // t477：右键铁砧 → 打开 AnvilUI 铁砧界面（同工作台 / 熔炉 / 箱子 / 附魔台模式：优先于放置，无论手持
    //   何物右键铁砧即开）。发 anvilOpened(x,y,z) 携命中格世界坐标 → 呈现层 Connections 打开 AnvilUI（释放
    //   指针）；UI 据坐标调 damageAnvil 推进铁砧损坏阶段。机制等价 MC 右键铁砧开铁砧界面。空手亦可（开界面
    //   是「使用」语义，与手持何物无关）。isAnvil 覆盖完好 / 微损 / 重损三阶段（任一皆可开 UI）。
    if (!sneakPlace && BlockRegistry::isAnvil(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {
        emit anvilOpened(m_hitBx, m_hitBy, m_hitBz);
        return;
    }
    // t517：右键发射器 → 打开 DispenserUI 物品栏界面（同工作台 / 熔炉 / 箱子 / 附魔台 / 铁砧模式：优先于放置，
    //   无论手持何物右键发射器即开）。发 dispenserOpened(x,y,z) 携命中格世界坐标 → 呈现层 Connections 打开
    //   DispenserUI（释放指针）。机制等价 MC 右键发射器开物品栏。空手亦可（开界面是「使用」语义，与手持何物
    //   无关）。与踩压力板触发的射箭陷阱（scanDispenserTraps）路径互不干扰（后者机关自动触发）。
    //   t609：投掷器（Dropper）同开本界面——机制等价 MC 1.0 投掷器 9 槽 UI；内容存复用 DispenserStore（按坐标
    //   键控，发射器 / 投掷器共用同一 store 不冲突）。isDropperUiBlock = 发射器 ∪ 投掷器（谓词在 QML 侧据
    //   blockId 判标题；C++ 侧同一信号、同一 UI 路径）。
    if (!sneakPlace && BlockRegistry::isDispenser(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {
        emit dispenserOpened(m_hitBx, m_hitBy, m_hitBz);
        return;
    }
    if (!sneakPlace && BlockRegistry::isDropper(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {
        emit dispenserOpened(m_hitBx, m_hitBy, m_hitBz); // t609 投掷器共用发射器 UI / store（标题由 QML 按 id 判）
        return;
    }
    // t387/t388 右键床 → 尝试睡觉（useBlock 语义；优先于放置，同工作台 / 箱子模式：右键已放置的床即睡，不另放块）。
    //   空手亦可（睡是「使用」语义，与手持何物无关）。命中格为任一床色变体（BlockRegistry::isBed）→ trySleepAt：
    //   夜间 + 床周无怪物 → 进 fade 态（完成后跳清晨 + 设重生点）；白天 / 附近有怪物 → emit sleepRefused 文案。
    if (BlockRegistry::isBed(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {
        trySleepAt(m_hitBx, m_hitBy, m_hitBz);
        return;
    }
    // t134 右键门 / 活板门 → 翻 state 开合（useBlock 语义；spec「door/trapdoor 右键 useBlock 翻 state 开合」）。
    //   优先于放置（同工作台 / 熔炉模式：右键已放置的门 / 活板门即开合，不另放块）。空手亦可（开合是
    //   「使用」语义，与手持何物无关）。id 不变只 state 变 → World::setBlock 5 参数版走重网格化路径
    //   （发 worldChanged 不发 broken/placed）。门两格同翻（找配对格：据本格 state bit3 判上 / 下）。
    {
        const quint8 hitId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
        // t134/t466 右键门翻 state 开合（统一经 isDoor 谓词覆盖 WoodDoor + SpruceDoor）。门两格同翻（找配对格：
        //   据本格 state bit3 判上 / 下；配对格须同为门 isDoor）。配对格写入用其自身 id（配对格 id 与本格一致 ——
        //   门放置仅产生同 id 配对），故读配对格 blockAt 而非硬编码 WoodDoor。
        if (BlockRegistry::isDoor(hitId)) {
            const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
            const quint8 flipped = quint8((st & ~4) | (((st & 4) == 0) ? 4 : 0)); // 翻 bit2（开合）
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, hitId, flipped);
            // 配对格（上 / 下）同步翻：本格 isUpper(bit3)=1 → 配对在 y-1；否则 y+1。
            const int py = ((st & 8) != 0) ? m_hitBy - 1 : m_hitBy + 1;
            const quint8 partnerId = m_world->blockAt(m_hitBx, py, m_hitBz);
            if (BlockRegistry::isDoor(partnerId)) {
                const quint8 pst = m_world->stateAt(m_hitBx, py, m_hitBz);
                const quint8 pflipped = quint8((pst & ~4) | (((pst & 4) == 0) ? 4 : 0));
                m_world->setBlock(m_hitBx, py, m_hitBz, partnerId, pflipped);
            }
            m_lastPlaceMs = now;
            // t152：开合音（门两格同翻只发一次 = 一次动作一次音）。flipped.bit2 = 新的开合态。
            emit doorToggled((flipped & 4) != 0);
            emit swingArm();
            return;
        }
        if (hitId == BlockRegistry::WoodTrapdoor) {
            const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
            const bool willOpen = (st & 1) == 0;
            quint8 ns = quint8(st ^ 1); // 翻 bit0（开合）
            if (willOpen) {
                // 开时记录朝向 = 玩家当前水平朝向（bit[2:1]），合时不清朝向（保留下次开向）。
                ns = quint8((ns & ~6) | quint8((horizontalFacing() & 3) << 1));
            }
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, hitId, ns);
            m_lastPlaceMs = now;
            // t152：开合音（ns.bit0 = 新的开合态；willOpen 已是本次结果，等价 (ns & 1)）。
            emit doorToggled(willOpen);
            emit swingArm();
            return;
        }
    }
    // t467 雪原浆果灌木丛采摘 useBlock（spec「成熟右键采摘得 2-3 浆果、丛回阶段 0 重新长」；机制等价 MC 1.0
    //   sweet berry bush 右键成熟丛采摘）。右键**成熟**浆果丛（SweetBerryBush state==SweetBerryBushStageMax）→
    //   spawnItem 掉落 2-3 甜浆果（RecipeRegistry::SweetBerryId；count = 2 + hash&1）+ 5 参数 setBlock 把丛降回
    //   阶段 0（id 不变只 state 变 → 不发 broken/placed，发 worldChanged 重建 mesh 同 t447 骨粉模式）+ 挥手。
    //   优先于放置（同工作台 / 门：右键成熟丛即采，不另放块），空手亦可（采摘是「使用」语义，与手持何物无关）。
    //   非成熟丛（state<max）右键 → 不采（机制等价 MC 右键未成熟丛无效应），fall-through 到下方放块路径。spectator
    //   已被入口 canPlace() 守卫拦截；Creative / Survival 均可采（创造采得浆果仅作装饰，丛仍降阶段 0 重长）。
    //   分层（PLAN §2）：采摘属 Game/Physics（读射线命中 + 写 World + 发 spawnItem 语义事件），不改 setBlock 语义。
    if (m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::SweetBerryBush) {
        const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz); // 采摘前快照（==max 才采）
        if (st >= BlockRegistry::SweetBerryBushStageMax) {
            // 2-3 浆果（QRandomGenerator 玩家交互掉落的随机性，非 worldgen 确定性范畴 §2-K，同 dropCropDrops）。
            const int berryCount = 2 + int(QRandomGenerator::global()->bounded(0, 2)); // 2 或 3
            // 散布到破格 + 非实体水平邻格做视觉分离（同 dropCropDrops 小麦种子模式）。
            constexpr int kHoriz[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            int sx = m_hitBx, sz = m_hitBz;
            for (const auto &o : kHoriz) {
                if (!BlockRegistry::isSolid(m_world->blockAt(m_hitBx + o[0], m_hitBy, m_hitBz + o[1]))) {
                    sx = m_hitBx + o[0]; sz = m_hitBz + o[1]; break;
                }
            }
            emit spawnItem(sx, m_hitBy, sz, RecipeRegistry::SweetBerryId, berryCount);
            // 丛降回阶段 0（5 参数 setBlock：id 不变 + state=0 → 不发 broken/placed，发 worldChanged 重建 mesh）。
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::SweetBerryBush, 0);
            m_lastPlaceMs = now;
            emit swingArm(); // 采摘也是一次「使用」动作 → 挥手（t29）
            return; // 采摘成功 → 不再走放置路径
        }
    }
    // t487/t664 末影之眼激活末地传送门（t664 正确形态：12 格**末地传送门框架**（EndPortal=111）环 +
    //   t664 门面（EndPortalSurface=131）薄星平面；机制等价 MC 1.0 持末影之眼右键各框架放眼激活）。
    //   右键命中格为末地传送门框架 + 手持末影之眼物品（EndEyeId）→ 翻 state bit0（EndPortalStateActiveFlag，
    //   mesher 切激活贴图 endframe_eye）+ qInfo 日志。激活后检查该框架所属环：12 框架**全部激活** →
    //   World::tryOpenEndPortal 在 3×3 内圈生成门面（薄黑色星平面，光 15，通往另一宇宙观感；末地维度仍
    //   留占位）。生存消耗 1 末影之眼（创造不耗，同桶 / 食物 / 种子模式）。优先于放置（右键框架即激活，
    //   不另放块）。
    //   分层（PLAN §2）：激活属 Game/Physics（读射线命中 + 写 World state + 调 World 打开门面 + 写 Hotbar VM）。
    if (BlockRegistry::isEndPortal(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))
        && m_hotbar && m_hotbar->selectedItemId() == RecipeRegistry::EndEyeId) {
        const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
        if ((st & BlockRegistry::EndPortalStateActiveFlag) == 0) { // 仅未激活时激活（防重复激活刷消耗）
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::EndPortal,
                              quint8(st | BlockRegistry::EndPortalStateActiveFlag));
            if (m_mode == Survival)
                m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 末影之眼（创造不耗 → 无限激活）
            // t664 开环：激活的框架属于哪个环（环中心 ∈ 本格 ±2 水平，3×3 内圈中心与框架相距 ≤2）。
            //   对每个候选中心尝试打开 —— tryOpenEndPortal 内部验 12 框架全激活才开，未满则 no-op。
            for (int cx = m_hitBx - 2; cx <= m_hitBx + 2; ++cx) {
                for (int cz = m_hitBz - 2; cz <= m_hitBz + 2; ++cz) {
                    if (m_world->tryOpenEndPortal(cx, m_hitBy, cz)) {
                        qInfo() << "end portal opened at" << cx << m_hitBy << cz
                                << "(end dimension deferred - placeholder)"; // 末地预热占位（门面已开，维度留后续）
                    }
                }
            }
            m_lastPlaceMs = now;
            emit swingArm();
            qInfo() << "end portal frame activated at" << m_hitBx << m_hitBy << m_hitBz
                    << "(end dimension deferred - placeholder)"; // 末地预热占位（日志，不实现末地维度）
        }
        return; // 已是激活态 → 右键无效应（不重复消耗 / 不放置），机制等价 MC 已激活框架无法再插眼
    }
    // t490 手动 TNT 点火机关（spec「右键 lever / 木按钮 / 石按钮 → 点燃水平四邻 TNT」；机制等价 MC 1.0 lever /
    //   button 红石点火源——本项目无红石，故把「激活脉冲」直接绑在右键动作）。右键命中的机关方块（Lever /
    //   WoodButton / StoneButton）→ 翻 state bit0（扳柄 / 按下态）+ 点燃其 **水平四邻** 的 TNT 方块（移除 TNT 方块
    //   + spawnPrimedTnt 延时引爆）+ 挥手。空手亦可（激活机关是「使用」语义，与手持何物无关）。优先于放置（右键
    //   机关即激活，不另放块），机制等价 MC 1.0 杠杆 / 按下激活红石脉冲点火 TNT。
    //   isManualIgniter 覆盖 Lever / WoodButton / StoneButton 三类机关（单一权威谓词，避免三处硬编码 id 判定漂移）。
    //   点燃 = 移除 TNT 方块（clearBlockSilent 点火专用静默清 + worldChanged 重建 mesh，不发 broken/placed → 免粒子 / 音
    //   spam；clearBlockSilent 绕过 setBlockFromEntity 的 occ 守卫——TNT 是实体方块，occ 守卫会拒写）+ spawnPrimedTnt（默认 fuse ~5s）→ 引爆时链式引燃邻接 TNT。
    //   **t628 边沿触发语义**（对齐 t627 压力板边沿；用户「按钮触发一次自动恢复；拉杆拉开持续激活——扳上沿
    //   fire 一次，扳下沿不 fire」）：
    //   - 仅翻到 **激活沿**（新态 bit0=1）才触发邻接 TNT / 发射器 / 投掷器；扳回下沿只翻视觉不触发。
    //     拉杆：off→on 沿 fire 一次（之后保持 on——持续激活语义由 state 承载，大红石系统的前置）；on→off
    //     只翻位不 fire。
    //   - 按钮（WoodButton/StoneButton）：按下（off→on）即激活沿 → fire 一次 + 写 m_buttonRecoverCells
    //     （kButtonRecoverSeconds 后 updateButtonRecovery 自动清 bit0 弹回）；**按下期间（bit0=1）再右键无效应**
    //     （机制等价 MC 按钮弹回前不可再按——否则旧「翻位」会把按钮提前弹起且白触发一次）。
    //   - t628 发射器 / 投掷器触发：激活沿上扫 **6 邻**（同 TNT 点火同圈）为 Dispenser/Dropper → fireDispenserAt
    //     一次（per-dispenser 冷却防抖；方向 = 机器 state 朝向，与机关方位无关——t608 单一方向源）。
    //   分层（PLAN §2）：点火属 Game/Physics（读射线命中 + 写 World state + 调 EntityManager.spawnPrimedTnt），向下依赖。
    if (BlockRegistry::isManualIgniter(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {
        const quint8 hitId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
        const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
        const bool isButton = BlockRegistry::isWoodButton(hitId) || BlockRegistry::isStoneButton(hitId);
        // t628 按钮「弹回前不可再按」：bit0=1（仍在按下窗 / 表内）→ 右键无效应（不翻位 / 不触发 / 不挥手）。
        if (isButton && (st & 1)) return;
        // 翻 state bit0（激活态：lever 扳柄 / button 按下）→ mesher 切按下视觉（t628：按钮压半高 / 拉杆高光）。
        const quint8 ns = quint8(st ^ 1);
        m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, hitId, ns);
        // t628 按钮自动复位：按下沿写 kButtonRecoverSeconds 倒计时（updateButtonRecovery 到期清 bit0 弹回）。
        //   拉杆不入表（保持扳开直到再右键）。打包键与 updatePressurePlates 的 cellKey 同编码（本表内自洽）。
        if (isButton) {
            const quint64 key = (quint64(quint32(m_hitBx + 0x100000) & 0x1FFFFFu))
                              | (quint64(quint32(m_hitBz + 0x100000) & 0x1FFFFFu) << 21)
                              | (quint64(quint32(m_hitBy) & 0x3FFu) << 42);
            m_buttonRecoverCells.insert(key, kButtonRecoverSeconds);
        }
        // t628 激活沿（ns bit0=1）才触发；扳回下沿（ns bit0=0）只翻视觉。
        if ((ns & 1) != 0 && m_entityManager) {
            // t492：点燃机关的 **6 邻**（4 水平 + 上 + 下）的 TNT 方块（同压力板 6 邻模式；用户要求）。逐邻查 TNT → 移除 + spawnPrimedTnt。
            //   t628：同一 6 邻圈内 Dispenser/Dropper → fireDispenserAt 一次（踩板沿 / 扳机关沿共用同一机器触发语义）。
            static constexpr int kDirs6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            for (const auto &d : kDirs6) {
                const int tx = m_hitBx + d[0], ty = m_hitBy + d[1], tz = m_hitBz + d[2];
                const quint8 tb = m_world->blockAt(tx, ty, tz);
                if (BlockRegistry::isTnt(tb)) {
                    m_world->clearBlockSilent(tx, ty, tz); // 移除 TNT 方块（点火专用静默清，绕过 occ 守卫）
                    m_entityManager->spawnPrimedTnt(tx, ty, tz); // 点燃（默认 fuse；爆炸不链式）
                } else if (BlockRegistry::isDispenser(tb) || BlockRegistry::isDropper(tb)) {
                    fireDispenserAt(tx, ty, tz, tb); // t628 发射器/投掷器：per-dispenser 冷却 + state 朝向发射
                }
            }
        }
        m_lastPlaceMs = now;
        emit swingArm(); // 扳柄 / 按下是一次「使用」动作 → 挥手（t29）
        return; // 机关激活 → 不再走放置路径
    }
    // t492 Bug B：删除「右键 TNT 本体直接点燃」分支（原 2097-2108）。spec 要求 TNT 只能经机关点燃：
    //   ① 右键机关四邻（上方 isManualIgniter 分支）+ ② 踩压力板四邻（scanTntTraps）。右键 TNT 本体不再点燃
    //   → 走普通放块路径（空手无效应 / 持物在 TNT 旁正常放块）。机制等价 MC 1.0 徒手不能点燃 TNT（本项目无
    //   打火石，故仅机关可激活）。两处机关点燃路径保留（isManualIgniter 四邻 + scanTntTraps 压力板四邻）。
    //   **t658 变更**：原「红石灯右键直接开关」分支（t620 无红石时代的简化）已删除 —— 红石电力系统 v1 落地
    //   后红石灯由**电力驱动亮灭**（World::tickRedstone 检出通断翻 state bit0，机制等价 MC 1.0 redstone
    //   lamp 受信号驱动；MC 红石灯不可右键交互）。右键红石灯无效应（fall-through 到放置 / 空手无动作）。
    } // t174：m_hasHit 局部门控结束（工作台/熔炉/门/活版门需命中；桶分支与放块路径各自处理命中需求）
    // t174 铁桶 useBlock（spec「右键舀水/倒水交互」）：选空桶 / 装水桶时右键走桶交互，不走方块放置路径
    //   （桶非方块；selectedBlock 经 hotbar 已归 Air，下方 Air 守卫会拦，故在此提前分支）。机制等价 MC 1.0
    //   铁桶：装水桶右键 → 在命中面相邻空气格放置水源（state=0）；空桶右键 → 含水射线命中首个水格舀走。
    //   分层（PLAN §2）：桶交互属 Game/Physics（读射线 + 写 World + 写 Hotbar VM），不改栅格语义（setBlock 入口）。
    // t186 修复(a)「舀水不变桶」：手持物品 id **直读 hotbar**（单一权威，同 updateMining 的 t57 修法），不读
    //   m_selectedItem 副本——后者经 Q_PROPERTY 绑定 hotbarVM.selectedItemId（NOTIFY=selectedSlotChanged）刷新，
    //   QML 绑定重算可能被引擎延迟到下一事件循环 → 同一 tick 内 m_selectedItem 滞后于真实选中槽（拾取 / 切槽
    //   边缘与右键同帧时）→ 桶分支不触发 → 舀水无效。直读 hotbar 消除绑定滞后窗口。
    //   同时：舀水（空桶→装水桶）**所有模式都换桶**（移除旧 `m_mode != Creative` 守卫）——机制等价 MC 1.0
    //   铁桶在创造 / 生存均反映其内容（舀水即满）；旧守卫使创造模式舀水后桶仍空（spec「当前舀水不变桶」）。
    //   倒水方向保留创造不消耗（装水桶右键放水源，创造保持装水桶 = 无限放水；生存→空桶），不属本任务范围。
    const int heldItemId = m_hotbar ? m_hotbar->selectedItemId() : 0;
    if (m_hotbar && (heldItemId == RecipeRegistry::WaterBucketId
                     || heldItemId == RecipeRegistry::LavaBucketId
                     || heldItemId == RecipeRegistry::BucketEmptyId)) {
        const int slot = m_hotbar->selectedSlot();
        if (heldItemId == RecipeRegistry::WaterBucketId || heldItemId == RecipeRegistry::LavaBucketId) {
            // 倒流体（水 / 岩浆）：命中面相邻格放对应源（state=0）。tx/ty/tz 同方块放置（命中面外法线相邻格）。
            //   目标须为空气或**该流体的流态**（流水 / 流岩浆 state>0 → 源覆盖升源；机制等价 MC 桶倒入流态格使其变满源）。
            //   已是源（state==0）→ 视为「已满」不重复放。实体方块仍不覆盖（保 t05 放置语义）。
            if (!m_hasHit) return; // 未命中 → 无相邻格可放（桶分支已绕过上方 !m_hasHit 门，此处补检）
            const int tx = m_hitBx + m_hitNx, ty = m_hitBy + m_hitNy, tz = m_hitBz + m_hitNz;
            const quint8 tgt = m_world->blockAt(tx, ty, tz);
            const BlockRegistry::Id fluidId = (heldItemId == RecipeRegistry::WaterBucketId)
                ? BlockRegistry::Water : BlockRegistry::Lava;
            const bool tgtFlowing = (tgt == fluidId && m_world->stateAt(tx, ty, tz) != 0); // 流态（state>0），非源
            if (tgt == BlockRegistry::Air || tgtFlowing) {
                // state=0 源。setBlock 在 world.cpp 内对「放水 / 放岩浆」会 poke 对应流 tick 节流计数（t273/t343）→
                //   下一 tick（~100ms）立即把波前推进 1 格（机制等价 MC 倒流体即刻外溢）。Air→流体发 blockPlaced；
                //   流态→源 id 不变只 state 变（不发 placed，是升源动作），均 worldChanged 重建对应段 mesh。
                m_world->setBlock(tx, ty, tz, fluidId, 0);
                if (m_mode != Creative)
                    m_hotbar->setStack(slot, int(RecipeRegistry::BucketEmptyId), 1); // 装流体桶 → 空桶（创造不消耗：保持装流体桶可无限放）
                m_lastPlaceMs = now;
                emit swingArm();
            }
            return; // 倒流体（无论成功与否）不再走放置路径
        }
        // 空桶舀流体（t174 水/t343 岩浆）：主射线排除 Water/Lava → 命中格恒为流体后/下的实体方块，旧查命中格恒 false
        //   （死代码）。改为单独跑「含流体」射线（HitWater / HitLava）命中首个流体格（水面/岩浆面/岸边/深水/水下/岩浆中
        //   均可舀）。舀走走 setWaterSilent（通用静默写入，不发 blockBroken → 无破块粒子/音，机制等价 MC 舀流体无反馈）。
        // t199 仅舀源（state==0）：机制等价 MC 1.0 铁桶——流水 / 流岩浆（state>0）右键无效。先试水（HitWater）→ 装水桶；
        //   水未中再试岩浆（HitLava）→ 装岩浆桶（t343）。两者皆未中 → 不写栅格 / 不换桶 / 不挥手。
        const RayHit wHit = raycastVoxel(*m_world, position(), lookDirection(), kReach, RayFilter::HitWater);
        if (wHit.valid
            && m_world->blockAt(wHit.bx, wHit.by, wHit.bz) == BlockRegistry::Water
            && m_world->stateAt(wHit.bx, wHit.by, wHit.bz) == 0) { // 仅水源 state==0 可舀（t199）
            m_world->setWaterSilent(wHit.bx, wHit.by, wHit.bz, BlockRegistry::Air, 0); // 舀走（清整格）
            m_hotbar->setStack(slot, int(RecipeRegistry::WaterBucketId), 1); // 空桶 → 装水桶
            m_lastPlaceMs = now;
            emit swingArm();
            return;
        }
        // t343 空桶舀岩浆：含岩浆射线（HitLava）命中首个岩浆格；仅岩浆源 state==0 可舀（同 t199 水源语义）。
        const RayHit lHit = raycastVoxel(*m_world, position(), lookDirection(), kReach, RayFilter::HitLava);
        if (lHit.valid
            && m_world->blockAt(lHit.bx, lHit.by, lHit.bz) == BlockRegistry::Lava
            && m_world->stateAt(lHit.bx, lHit.by, lHit.bz) == 0) { // 仅岩浆源 state==0 可舀
            m_world->setWaterSilent(lHit.bx, lHit.by, lHit.bz, BlockRegistry::Air, 0); // 舀走（setWaterSilent 通用静默写，支持 Lava id）
            m_hotbar->setStack(slot, int(RecipeRegistry::LavaBucketId), 1); // 空桶 → 装岩浆桶
            m_lastPlaceMs = now;
            emit swingArm();
        }
        return; // 空桶（舀水 / 舀岩浆成功与否）不再走放置路径
    }
    // t656 红石粉导线放置（机制等价 MC 1.0 redstone dust：**红石粉物品本身就是导线** —— 右键放置成
    //   RedstoneDust 方块，不另立物品 id / 不设合成配方（MC 1.0 红石粉由采矿获得；本工程 RedstoneOre
    //   掉 4 粉 + 地牢战利品 t393））。手持红石粉（RedstoneId 0x224）右键命中**实体方块的顶面**
    //   （ny>0，机制等价 MC 粉尘只能铺在方块顶面——贴侧 / 悬空拒）→ 命中面相邻格若是空气 / 水则铺粉
    //   （RedstoneDust state=0 断电态；World notePowerWrite 首次重算即点亮）。生存消耗 1 粉 / 创造不耗。
    //   破坏粉尘掉回 1 粉（BlockDef dropId=0x224）→ 「放置↔破坏」无损循环。红石粉非方块 → selectedBlock
    //   归 Air，须在 `m_selectedBlock == Air` 守卫之前分流（同桶 / 玻璃分支模式）。分层（PLAN §2）：
    //   放置属 Game/Physics（读射线命中 + 写 World + 写 Hotbar VM）。
    if (m_hotbar && m_world && heldItemId == RecipeRegistry::RedstoneId) {
        if (m_hasHit && m_hitNy > 0) { // 仅顶面（贴侧 / 悬空拒，机制等价 MC 粉尘铺面）
            const int tx = m_hitBx, ty = m_hitBy + 1, tz = m_hitBz; // 粉铺在命中方块正上方一格
            if (ty < m_world->height()) {
                const quint8 tgt = m_world->blockAt(tx, ty, tz);
                // t702 放置守卫（镜像 t666 铁轨守卫）：粉只能铺在**完整立方顶面**——
                //   ① 目标格已是粉 → 拒（机制等价 MC 粉不可叠粉；旧版 tgt 允许 Water/Air，但命中「粉的顶面」
                //      时射线把粉当实体命中（raycastAABBs 粉薄板命中盒）→ ty = 粉格 +1 且下方是粉 → 旧守卫
                //      拦不住「粉上放粉」的悬空二层粉（用户实测「红石粉可以在红石粉上面放」）。
                //   ② 目标格正下方须 isFullCube（粉贴地薄层须满顶面支撑——同铁轨 / 雪层 t554 支撑语义；
                //      耕地 / 半砖 / 楼梯顶不铺，粉上也不满足 → ② 同时兜死 ① 的漏网路径）。
                const bool belowFull = ty - 1 >= 0
                    && BlockRegistry::isFullCube(m_world->blockAt(tx, ty - 1, tz));
                if (!BlockRegistry::isRedstoneDust(tgt)
                    && (tgt == BlockRegistry::Air || tgt == BlockRegistry::Water)
                    && belowFull) {
                    m_world->setBlock(tx, ty, tz, BlockRegistry::RedstoneDust, 0); // state=0（断电；World 首次重算点亮）
                    if (m_mode != Creative)
                        m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 粉（创造不耗）
                    m_lastPlaceMs = now;
                    emit swingArm(); // 铺粉也是一次「放置」动作 → 挥手（t29）
                }
            }
        }
        return; // 红石粉（铺粉成功 / 命中非顶面 / 未命中）均不再走方块放置路径
    }
    // t505 雪球抛掷（spec「雪球可丢弃发射：右键发射（仿箭实体），砸到怪物不扣血但红色受击动画 + 少量击退」；
    //   机制对标 MC 1.0 玩家抛雪球）：手持雪球（SnowballId，材料段）右键 → spawnSnowball 从眼位沿视线方向以
    //   kSnowballSpeed 抛出（抛物弹丸，机制对标箭但更简单 —— 无蓄力，右键即抛）。**玩家雪球 damage=0**（机制
    //   对标 MC 1.0 玩家雪球打 mob 0 伤害，只触发红闪 + 击退，由 spawnSnowball 的 damage 参数传入；雪傀儡雪球
    //   仍走 fireSnowball 传 kSnowballDamage 保留敌对伤害）。
    //   **不要求 m_hasHit**（瞄准的是抛物弹道非方块命中格）；雪球非方块（材料段）→ selectedBlock 归 Air，须在
    //   `m_selectedBlock == Air` 守卫之前分流（同桶 / 蛋 / 剪刀 / 食物分支模式）。spectator 已被入口 canPlace() 守卫
    //   拦截；Creative / Survival 均可抛。生存消耗 1 雪球 / 创造不耗（无限抛）。分层：抛掷属 Game/Physics（读视线 +
    //   调 EntityManager），不改栅格语义。
    if (m_hotbar && m_world && m_entityManager && heldItemId == RecipeRegistry::SnowballId) {
        // origin = 眼位 + 视线前移 0.5（防贴墙 spawn 入墙即被 tick 判方块命中，同 spawnArrowPlayer 模式）。
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        const QVector3D origin = eye + look * 0.5f;
        // vel = 视线方向 × kPlayerSnowballSpeed（水平 + 略向上弧线，机制对标 MC 雪球抛物）。速度取 12（略快于
        //   雪傀儡的 10，玩家主动抛掷更有力；机制对标非精确复刻）。EntityManager::kSnowballSpeed 是 private 不能
        //   跨层读，故本层自定常量（同 spawnArrowPlayer 自定箭速模式）。
        constexpr float kPlayerSnowballSpeed = 12.0f; // 玩家抛雪球速度（blocks/s）
        const QVector3D vel = look * kPlayerSnowballSpeed;
        m_entityManager->spawnSnowball(origin, vel, 0); // 玩家雪球 damage=0（红闪 + 击退，无血量伤害）
        if (m_mode != Creative)
            m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 雪球（创造不耗）
        m_lastPlaceMs = now;
        emit swingArm(); // 抛雪球也是一次「使用」动作 → 挥手（t29）
        return; // 雪球（抛出成功）不再走方块放置路径
    }
    // t583 鸡蛋投掷（用户「鸡蛋还不能投掷出来，应该可以丢出来然后可以砸出来小鸡」；机制等价 MC 1.0 egg
    //   投掷）：手持鸡蛋（EggId，材料段）右键 → spawnEgg 从眼位沿视线方向以 kPlayerEggSpeed 抛出（抛物弹丸，
    //   完全同雪球 t505 模式 —— 无蓄力右键即抛）。命中（方块 / mob）→ 碎裂 + **1/8 概率在命中处孵 1 只小鸡**
    //   （Egg tick 分支内 spawnMobCore + baby 态，机制等价 MC 1.0 鸡蛋砸出小鸡）。
    //   **不要求 m_hasHit**（瞄准的是抛物弹道非方块命中格）；鸡蛋非方块（材料段）→ selectedBlock 归 Air，须在
    //   `m_selectedBlock == Air` 守卫之前分流（同雪球 / 桶 / 生物蛋分支模式）。spectator 已被入口 canPlace()
    //   守卫拦截；Creative / Survival 均可抛。生存消耗 1 鸡蛋 / 创造不耗（无限抛）。分层：抛掷属 Game/Physics
    //   （读视线 + 调 EntityManager），不改栅格语义。
    if (m_hotbar && m_world && m_entityManager && heldItemId == RecipeRegistry::EggId) {
        // origin = 眼位 + 视线前移 0.5（防贴墙 spawn 入墙即被 tick 判方块命中，同雪球模式）。
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        const QVector3D origin = eye + look * 0.5f;
        // vel = 视线方向 × kPlayerEggSpeed。速度取 12（同 kPlayerSnowballSpeed —— 鸡蛋与雪球同为轻抛物弹丸，
        //   机制等价 MC 1.0 蛋 / 雪球投掷速度一致）。本地常量（同 kPlayerSnowballSpeed 模式，Entities 层速度
        //   常量不跨层读）。
        constexpr float kPlayerEggSpeed = 12.0f; // 玩家抛鸡蛋速度（blocks/s）
        const QVector3D vel = look * kPlayerEggSpeed;
        m_entityManager->spawnEgg(origin, vel);
        if (m_mode != Creative)
            m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 鸡蛋（创造不耗）
        m_lastPlaceMs = now;
        emit swingArm(); // 抛鸡蛋也是一次「使用」动作 → 挥手（t29）
        return; // 鸡蛋（抛出成功）不再走方块放置路径
    }
    // t400 繁殖喂食 useBlock（spec「喂对应食物 → 求偶 → 同种配对产幼崽」；机制等价 MC 1.0 breeding）：
    //   手持繁殖食物（小麦 WheatId / 胡萝卜 CarrotId / 马铃薯 PotatoId / 种子 SeedId）右键 → 在主选体射线之外
    //   **独立**跑一条「mob 命中射线」（findMobHit，同剪刀剪羊 / 攻击路径）；命中可繁殖 mob 且食物匹配该物种
    //   （牛/羊=小麦、猪=胡萝卜·马铃薯、鸡=种子）→ EntityManager::enterLoveMode（设 loveTimer + emit，成体求偶）+
    //   生存消耗 1 食物 / 创造不耗 + 挥手。命中非 mob / 食物不匹配 / 无命中 → 未喂。
    //   **t479 幼崽特例**：命中**幼崽**（isBabyAt）且食物匹配 → 不走求偶（幼崽不可繁殖，enterLoveMode 守卫返 false），
    //   改 EntityManager::feedBaby 加速成长（growTimer 减 kBabyFeedGrow ≈ kBabyGrowTime 的 10%，机制等价 MC 喂幼崽
    //   加速长大）—— 同样消耗食物 + 挥手。冷却中 / 已求偶的成体 → 未喂。
    //   **不要求 m_hasHit**（命中方块）：喂食瞄准的是 mob（实体），不依赖方块命中格；瞄准悬空 mob 亦可喂
    //   （同剪刀剪羊模式）。食物非方块（材料段）→ selectedBlock 归 Air，须在 `m_selectedBlock == Air` 守卫之前
    //   分流（同桶 / 锄 / 蛋 / 剪刀分支模式）。spectator 已被入口 canPlace() 守卫拦截；Creative / Survival 均可喂。
    //   **种子特例**：种子既是鸡的食物又是种植物品（t236 右键耕地种小麦）→ 未命中鸡（或命中但非鸡 / 非成体 /
    //   冷却中）时**不 return**，fall-through 到下方种子种植分支（右键耕地仍可种植）；其余 3 种食物（小麦 / 胡
    //   萝卜 / 马铃薯）无其他 useBlock 用途 → 未喂即 return（不放置方块）。食物匹配判定在 Game 层（本处）做：
    //   物品 id 属 RecipeRegistry（Game 层），Entities 层的 enterLoveMode 不向上依赖（PLAN §2 分层）。
    if (m_hotbar && m_world && m_entityManager
        && (heldItemId == RecipeRegistry::WheatId
            || heldItemId == RecipeRegistry::CarrotId
            || heldItemId == RecipeRegistry::PotatoId
            || heldItemId == RecipeRegistry::SeedId)) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        bool fed = false;
        if (mobIdx >= 0) {
            const int mt = m_entityManager->mobTypeAt(mobIdx);
            // 食物匹配（Game 层判定，RecipeRegistry id + EntityManager mobType）：
            //   牛/羊 → 小麦；猪 → 胡萝卜 / 马铃薯；鸡 → 种子。机制等价 MC 1.0 各动物对应繁殖食物。
            bool match = false;
            if (mt == EntityManager::MobCow || mt == EntityManager::MobSheep)
                match = (heldItemId == RecipeRegistry::WheatId);
            else if (mt == EntityManager::MobPig)
                match = (heldItemId == RecipeRegistry::CarrotId || heldItemId == RecipeRegistry::PotatoId);
            else if (mt == EntityManager::MobChicken)
                match = (heldItemId == RecipeRegistry::SeedId);
            // enterLoveMode 内含成体 / 冷却 / 已求偶 / 可繁殖 mob 守卫；返 true 才算喂成功（消耗食物）。
            // t479 幼崽喂食分流（机制等价 MC 1.0 喂幼崽加速长大）：isBabyAt 判「是否幼崽」→ 幼崽走 feedBaby
            //   （growTimer 减 kBabyFeedGrow 加速成长，不触发求偶）；成体走 enterLoveMode（求偶）。二者互斥：
            //   幼崽不可求偶（enterLoveMode 守卫返 false）、成体无成长可加（feedBaby 守卫返 false）。
            if (match) {
                if (m_entityManager->isBabyAt(mobIdx))
                    fed = m_entityManager->feedBaby(mobIdx);
                else
                    fed = m_entityManager->enterLoveMode(mobIdx);
            }
        }
        if (fed) {
            if (m_mode != Creative)
                m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 食物（创造不耗 → 无限喂）
            m_lastPlaceMs = now;
            emit swingArm(); // 喂食也是一次「使用」动作 → 挥手（t29）
            return; // 喂食成功 → 不再走放置 / 种植路径
        }
        // 未喂（无 mob / 食物不匹配 / 幼崽 / 冷却 / 已求偶）：
        //   种子 fall-through 到下方种植分支（右键耕地仍可种小麦）；其余 3 种食物无其他用途 → return（不放置）。
        //   **t447 ①**：胡萝卜 / 马铃薯物品**既是食物又是种子**（机制等价 MC 1.0 carrot/potato「种一个长成
        //   作物」）→ 未喂时亦须 fall-through 到下方种植分支（右键耕地种对应作物），不能 return。旧 `!= SeedId`
        //   一刀切让胡萝卜 / 马铃薯命中非猪 / 无命中时直接 return → 永远种不下（用户报「胡萝卜 / 马铃薯种不了」）。
        //   小麦（WheatId）不是种子（小麦物品纯食物，种小麦用 SeedId）→ 仍 return（无种植用途）。
        if (heldItemId != RecipeRegistry::SeedId
            && heldItemId != RecipeRegistry::CarrotId
            && heldItemId != RecipeRegistry::PotatoId) return;
    }
    // t480 骨头驯狼 useBlock（spec「右键狼概率驯服 ~33% → 驯服后右键坐/站切换」；机制等价 MC 1.0 狼骨头驯服）：
    //   手持骨头（BoneId，材料段非方块）右键 → 在主选体射线之外**独立**跑一条「mob 命中射线」（findMobHit，
    //   同剪刀剪羊 / 喂食路径）。命中狼（mobType==MobWolf）：
    //   - 未驯服 → EntityManager::tameWolf（~33% 概率驯服；**无论成败骨头都消耗**，机制等价 MC 喂骨 ——
    //     生存 takeStack 1 骨头、创造不耗）。
    //   - 已驯服 → EntityManager::toggleWolfSit（坐/站切换，不消耗骨头）。
    //   命中非狼 / 无命中 → return（骨头无其他 useBlock 用途，不放置方块）。**不要求 m_hasHit**（瞄的是 mob
    //   实体，同剪刀 / 喂食模式）。骨头非方块（材料段）→ selectedBlock 归 Air，须在 `m_selectedBlock == Air`
    //   守卫之前分流（同桶 / 锄 / 蛋分支模式）。spectator 已被入口 canPlace() 守卫拦截。
    //   分层（PLAN §2）：驯服属 Game/Physics（读射线 + 调 EntityManager），不改栅格语义（setBlock 入口）。
    if (m_hotbar && m_world && m_entityManager && heldItemId == RecipeRegistry::BoneId) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        if (mobIdx >= 0 && m_entityManager->mobTypeAt(mobIdx) == EntityManager::MobWolf) {
            if (!m_entityManager->wolfTamedAt(mobIdx)) {
                // 驯服尝试（~33% 概率；成功/失败骨头都消耗，机制等价 MC 喂骨无论成败都耗）。
                m_entityManager->tameWolf(mobIdx);
                if (m_mode != Creative)
                    m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 骨头（创造不耗 → 无限驯）
            } else {
                m_entityManager->toggleWolfSit(mobIdx); // 已驯服 → 坐/站切换（不消耗骨头）
            }
            m_lastPlaceMs = now;
            emit swingArm(); // 喂骨 / 命令坐站都是一次「使用」动作 → 挥手（t29）
        }
        return; // 骨头（驯服成功失败 / 切换坐站 / 未命中狼）均不再走方块放置路径
    }
    // t480 狼肉食繁殖 useBlock（spec「喂驯服狼生/熟肉 → love mode 产幼崽」；机制等价 MC 1.0 喂驯服狼肉繁殖）：
    //   手持生/熟肉（RawPorkchop/RawBeef/RawChicken/CookedPorkchop/CookedBeef/CookedMutton/CookedChicken，材料段
    //   非方块）右键 → 独立 mob 命中射线（同喂食路径）→ 命中**已驯服**狼 → 幼崽 feedBaby 加速成长 / 成体
    //   enterLoveMode 求偶（二者互斥分流同 t479；食物匹配 = 生/熟肉，Game 层 isWolfMeatItem 判）。喂成功 → 生存
    //   消耗 1 肉 + 挥手；未喂（无狼 / 未驯服 / 幼崽 / 冷却 / 已求偶）→ return（肉无其他 useBlock 用途，不放置）。
    //   肉非方块 → 须在 `m_selectedBlock == Air` 守卫之前分流（同骨头 / 桶 / 蛋分支模式）。spectator 已被入口
    //   canPlace() 守卫拦截。分层（PLAN §2）：喂食属 Game/Physics（读射线 + 调 EntityManager），不改栅格语义。
    if (m_hotbar && m_world && m_entityManager && isWolfMeatItem(heldItemId)) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        bool fed = false;
        if (mobIdx >= 0 && m_entityManager->mobTypeAt(mobIdx) == EntityManager::MobWolf
            && m_entityManager->wolfTamedAt(mobIdx)) {
            // t479 幼崽喂食分流（机制等价 MC 喂幼崽加速长大 / 喂成体进求偶）：幼崽 → feedBaby；成体 → enterLoveMode。
            if (m_entityManager->isBabyAt(mobIdx))
                fed = m_entityManager->feedBaby(mobIdx);
            else
                fed = m_entityManager->enterLoveMode(mobIdx);
        }
        if (fed) {
            if (m_mode != Creative)
                m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 肉（创造不耗 → 无限喂）
            m_lastPlaceMs = now;
            emit swingArm(); // 喂食也是一次「使用」动作 → 挥手（t29）
        }
        return; // 肉（喂成功 / 未喂）均不再走放置路径
    }
    // t481 生鱼驯服/繁殖豹猫 useBlock（spec「生鱼驯服 → 变猫（3 毛色变体随机）；繁殖：生鱼喂食触发」；机制等价
    //   MC 1.0 豹猫生鱼驯服 + 驯服猫生鱼繁殖）：手持生鱼（RawFishId，材料段非方块）右键 → 独立 mob 命中射线
    //   （同喂食/骨头路径）→ 命中豹猫（mobType==MobOcelot）：
    //   - 未驯服 → EntityManager::tameOcelot（~kOcelotTameChance 概率驯服变猫 + 随机毛色；**无论成败生鱼都消耗**，
    //     机制等价 MC 喂鱼无论成败都耗）。
    //   - 已驯服 → 幼崽 feedBaby 加速成长 / 成体 enterLoveMode 求偶（二者互斥分流同 t479；食物匹配 = 生鱼，
    //     Game 层 RawFishId 判）。繁殖产幼崽复用 t400 框架（MobOcelot 入 isBreedableType + tamed 守卫）。
    //   喂成功 → 生存消耗 1 生鱼 + 挥手；未喂（无豹猫 / 冷却 / 已求偶）→ return（生鱼无其他 useBlock 用途，
    //   不放置）。生鱼非方块 → 须在 `m_selectedBlock == Air` 守卫之前分流（同骨头 / 肉 / 桶分支模式）。spectator
    //   已被入口 canPlace() 守卫拦截。分层（PLAN §2）：喂食属 Game/Physics（读射线 + 调 EntityManager），不改栅格语义。
    if (m_hotbar && m_world && m_entityManager && heldItemId == RecipeRegistry::RawFishId) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        bool fed = false;
        if (mobIdx >= 0 && m_entityManager->mobTypeAt(mobIdx) == EntityManager::MobOcelot) {
            if (!m_entityManager->ocelotTamedAt(mobIdx)) {
                // 驯服尝试（~1/3 概率；成功/失败生鱼都消耗，机制等价 MC 喂鱼无论成败都耗）。tameOcelot 内部
                //   选随机毛色变体 0..2（黑 / 姜黄 / 奶油）→ 变猫；返 false 仅表示本次未驯中（生鱼照耗）。
                m_entityManager->tameOcelot(mobIdx);
                fed = true; // 喂鱼动作发生（无论驯中与否）→ 消耗生鱼
            } else {
                // t479 幼崽喂食分流（机制等价 MC 喂幼崽加速长大 / 喂成体进求偶）：幼崽 → feedBaby；成体 → enterLoveMode。
                if (m_entityManager->isBabyAt(mobIdx))
                    fed = m_entityManager->feedBaby(mobIdx);
                else
                    fed = m_entityManager->enterLoveMode(mobIdx);
            }
        }
        if (fed) {
            if (m_mode != Creative)
                m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 生鱼（创造不耗 → 无限喂）
            m_lastPlaceMs = now;
            emit swingArm(); // 喂食也是一次「使用」动作 → 挥手（t29）
        }
        return; // 生鱼（驯服成功失败 / 繁殖 / 未命中豹猫）均不再走放置路径
    }
    // t481 空手右键驯服猫 → 坐/站切换（spec「坐/站（同狼模式）」；机制等价 MC 1.0 右键驯服猫坐/站命令）：
    //   空手（heldItemId==0，selectedBlock 归 Air）右键 → 独立 mob 命中射线（同喂食/骨头路径）→ 命中**已驯服**
    //   猫（mobType==MobOcelot && ocelotTamed）→ toggleOcelotSit（坐/站翻转；不消耗物品）。命中未驯服豹猫 /
    //   非 ocelot / 无命中 → 无操作（fall-through 到下方 `m_selectedBlock == Air` 守卫返回，不放置；野豹猫右键
    //   无反应，机制等价 MC 只有驯服猫可命令坐/站）。空手 selectedBlock 恒 Air → 须在 Air 守卫之前分流（同
    //   骨头 / 肉 / 桶 / 生鱼分支模式）。spectator 已被入口 canPlace() 守卫拦截。
    if (m_hotbar && m_world && m_entityManager && heldItemId == 0) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        if (mobIdx >= 0 && m_entityManager->mobTypeAt(mobIdx) == EntityManager::MobOcelot
            && m_entityManager->ocelotTamedAt(mobIdx)) {
            m_entityManager->toggleOcelotSit(mobIdx); // 已驯服猫 → 坐/站切换（不消耗物品）
            m_lastPlaceMs = now;
            emit swingArm(); // 命令坐/站也是一次「使用」动作 → 挥手（t29）
            return; // 坐/站切换 → 不再走放置路径
        }
    }
    // t234 锄头 useBlock（spec「持锄右键泥土/草方块→变耕地」）：手持为 Hoe 类工具（木/石/铁锄）+ 命中格为
    //   Dirt/Grass → 该格转 Farmland（湿润等级由 World::farmlandHydrationLevel 据水源邻近判定写 state 低 2 位；
    //   t406 4 级湿润，darker=wetter）。机制等价 MC 1.0 锄耕地（机制对齐，非名词照搬）。锄非方块（工具段 id>=0x100）
    //   → selectedBlock 归 Air，须在下方 `m_selectedBlock == Air` 守卫之前分流（同桶分支模式）。命中非泥土/草
    //   （如石头/沙）→ 不耕不挥（锄对非可耕地无效应，机制等价 MC 锄石头无反应）。spectator 已被入口 canPlace()
    //   守卫拦截（耕地经 placeBlock 入口，沿用既有放置门控；spectator 不交互）；Creative / Survival 均可耕。tier
    //   当前仅记账（不驱动耕地耗时，spec t233「tier 驱动未来耕地等级，留后续任务」）→ 任何 tier 锄一键成耕地。
    //   分层（PLAN §2）：耕地属 Game/Physics（读射线命中 + 写 World + 读水源邻近），不改 setBlock 语义。
    if (m_hotbar && m_world) {
        const ToolRegistry::ToolDef *const td = ToolRegistry::tool(heldItemId);
        if (td && td->type == int(BlockRegistry::Hoe)) {
            if (m_hasHit) {
                const quint8 hitId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
                if (hitId == BlockRegistry::Dirt || hitId == BlockRegistry::Grass) {
                    // t406 湿润等级 0..3（World::farmlandHydrationLevel 据水源切比雪夫半径 4 算出；近水→湿，darker=wetter）。
                    const int hydr = m_world->farmlandHydrationLevel(m_hitBx, m_hitBy, m_hitBz);
                    // setBlock(id, state)：5 参数版写 Farmland + 湿润等级 state（id 变 Dirt/Grass→Farmland，
                    //   走写入路径发 blockBroken(Dirt/Grass) + blockPlaced(Farmland) + worldChanged → 即时重建 mesh；
                    //   粒子/音由呈现层按事件消费）。swingArm 驱动挥锄动画。
                    m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::Farmland, quint8(hydr));
                    // t263 锄耕地消耗 1 点耐久（机制等价 MC「锄每耕 1 格 -1 耐久」）。Survival 限定（创造不耗）；
                    //   damageSelectedItem 对当前选中槽的锄 -1，归零自动清槽（锄破损消失）。
                    if (m_mode == Survival) m_hotbar->damageSelectedItem();
                    m_lastPlaceMs = now;
                    emit swingArm();
                }
            }
            return; // 锄（耕地成功 / 命中非可耕地 / 未命中）均不再走方块放置路径
        }
    }
    // t236 小麦种子种植（spec「种子右键耕地→种小麦作物」）：手持种子（SeedId，材料段非方块）右键命中耕地 →
    //   在耕地正上方空气格种小麦作物（WheatCrop，state=阶段 0 刚种）。机制等价 MC 1.0 种植。种子非方块 →
    //   selectedBlock 经 hotbar 归 Air，须在下方 `m_selectedBlock == Air` 守卫之前分流（同桶 / 锄分支模式）。
    //   命中非耕地（如石头 / 草 / 已有作物）→ 不种不挥（机制等价 MC 种子只能种在耕地）。spectator 已被入口
    //   canPlace() 守卫拦截；Creative / Survival 均可种。生存消耗 1 种子（创造不耗 → 无限种）。
    //   分层（PLAN §2）：种植属 Game/Physics（读射线命中 + 写 World + 写 Hotbar VM），不改 setBlock 语义。
    //   t407：胡萝卜（CarrotId）/马铃薯（PotatoId）物品同理 —— 右键耕地种对应作物方块（MC 1.0 carrot/potato
    //   物品本身即种子 + 产物，机制等价 MC「种一个胡萝卜长成胡萝卜作物」）。三者共一分支：物品→作物映射表，
    //   统一种植流程（耕地正上方空气格 + state=0 + 生存消耗 1 / 创造不耗）。
    static const struct CropSeed { int itemId; quint8 cropBlockId; } kCropSeeds[] = {
        { RecipeRegistry::SeedId,  BlockRegistry::WheatCrop  },
        { RecipeRegistry::CarrotId, BlockRegistry::CarrotCrop },
        { RecipeRegistry::PotatoId, BlockRegistry::PotatoCrop },
    };
    for (const CropSeed &cs : kCropSeeds) {
        if (heldItemId == cs.itemId) {
            if (m_hotbar && m_world && m_hasHit
                && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::Farmland) {
                const int wx = m_hitBx, wy = m_hitBy + 1, wz = m_hitBz; // 作物种在耕地正上方一格
                // 目标须在界内 + 为空气（不覆盖实体 / 已种作物 / 草丛）。越界 setBlock 静默返 false → 提前挡防误耗种子。
                if (wy < m_world->height() && m_world->blockAt(wx, wy, wz) == BlockRegistry::Air) {
                    m_world->setBlock(wx, wy, wz, cs.cropBlockId, 0); // state=0 阶段 0（刚种；tickCropGrowth 逐步推进）
                    if (m_mode != Creative)
                        m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 种子（创造不耗）
                    m_lastPlaceMs = now;
                    emit swingArm(); // 种植也是一次「放置」动作 → 挥手（t29）
                }
            }
            return; // 作物种子（种植成功 / 命中非耕地 / 未命中）均不再走方块放置路径
        }
    }
    // t447 ④ 骨粉催熟（spec「骨粉右键作物→催熟一阶段」）：手持骨粉（BonemealId，材料段非方块）右键命中**未成熟**
    //   作物（小麦 / 胡萝卜 / 马铃薯，state<WheatCropStageMax）→ 作物 state+1（即时催熟一阶段）。机制等价 MC 1.0
    //   骨粉右键作物 +1 age。骨粉非方块 → selectedBlock 经 hotbar 归 Air，须在下方 `m_selectedBlock == Air` 守卫
    //   之前分流（同桶 / 锄 / 种子 / 树苗 / 玻璃分支模式）。命中非作物 / 已成熟（state==max）→ 不催不挥（机制等价
    //   MC 骨粉对成熟作物 / 非作物无效应；本工程简化：骨粉仅催作物，不催草 / 树苗）。spectator 已被入口 canPlace()
    //   守卫拦截；Creative / Survival 均可催。生存消耗 1 骨粉（创造不耗）。分层（PLAN §2）：催熟属 Game/Physics
    //   （读射线命中 + 写 World + 写 Hotbar VM），不改 setBlock 语义。
    //   写入走 5 参数 setBlock（保留 crop id + 写 state+1）：id 不变 → 不发 broken/placed；发 worldChanged → 作物
    //   mesh 重建（阶段贴图更新）。机制等价 MC 骨粉即时 +1 age（不走 tick 等待）。
    if (m_hotbar && m_world && heldItemId == RecipeRegistry::BonemealId) {
        if (m_hasHit) {
            const quint8 hitId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
            if (hitId == BlockRegistry::WheatCrop || hitId == BlockRegistry::CarrotCrop
                || hitId == BlockRegistry::PotatoCrop) {
                const quint8 st = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz); // 催熟前快照（< max 才催）
                if (st < BlockRegistry::WheatCropStageMax) { // 三种作物共享阶段上界（blockregistry.h 注释）
                    m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, hitId, quint8(st + 1)); // id 不变 + state+1
                    if (m_mode != Creative)
                        m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 骨粉（创造不耗）
                    m_lastPlaceMs = now;
                    emit swingArm(); // 催熟也是一次「使用」动作 → 挥手（t29）
                }
            }
        }
        return; // 骨粉（催熟成功 / 命中非作物 / 已成熟 / 未命中）均不再走方块放置路径
    }
    // t305 树苗种植（spec「树苗种植→长大成完整树」）：手持树苗物品（SaplingItemId，材料段非方块）右键命中
    //   草地 / 泥土 → 在命中格正上方空气格种下 Sapling 方块（机制等价 MC 1.0 树苗种植）。树苗物品非方块 →
    //   selectedBlock 经 hotbar 归 Air，须在下方 `m_selectedBlock == Air` 守卫之前分流（同桶 / 锄 / 种子模式）。
    //   命中非草地 / 泥土（如石头 / 沙 / 已有方块）→ 不种不挥（机制等价 MC 树苗只能种在草地 / 泥土）。spectator
    //   已被入口 canPlace() 守卫拦截；Creative / Survival 均可种。生存消耗 1 树苗（创造不耗 → 无限种）。
    //   分层（PLAN §2）：种植属 Game/Physics（读射线命中 + 写 World + 写 Hotbar VM），不改 setBlock 语义。
    //   生长由 World::tickSaplingGrowth（WorldClock tick 驱动）推进，本处仅落地树苗方块。
    if (m_hotbar && m_world && heldItemId == RecipeRegistry::SaplingItemId) {
        if (m_hasHit) {
            const quint8 hitId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
            if (hitId == BlockRegistry::Grass || hitId == BlockRegistry::Dirt) {
                const int wx = m_hitBx, wy = m_hitBy + 1, wz = m_hitBz; // 树苗种在命中格正上方一格
                // 目标须在界内 + 为空气（不覆盖实体 / 已种树苗 / 草丛 / 水）。越界 setBlock 静默返 false → 提前挡防误耗树苗。
                if (wy < m_world->height() && m_world->blockAt(wx, wy, wz) == BlockRegistry::Air) {
                    m_world->setBlock(wx, wy, wz, BlockRegistry::Sapling, 0); // state=0（worldgen 树苗态；生长由 tick 推进）
                    if (m_mode != Creative)
                        m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 树苗（创造不耗）
                    m_lastPlaceMs = now;
                    emit swingArm(); // 种植也是一次「放置」动作 → 挥手（t29）
                }
            }
        }
        return; // 树苗（种植成功 / 命中非草地泥土 / 未命中）均不再走方块放置路径
    }
    // t514 甜浆果丛种植（spec「甜浆果物品右键草地 / 泥土 → 种植甜浆果丛」）：手持甜浆果物品（SweetBerryId，材料段
    //   非方块）右键命中草地 / 泥土 → 在命中格正上方空气格种下 SweetBerryBush 方块 state=0（机制等价 MC 1.0 浆果丛种植）。
    //   甜浆果物品非方块 → selectedBlock 经 hotbar 归 Air，须在下方 `m_selectedBlock == Air` 守卫之前分流（同树苗 /
    //   作物种子 / 玻璃分支模式）。命中非草地 / 泥土（如石头 / 沙 / 雪层 / 已有方块）→ 不种不挥（机制等价 MC 浆果丛
    //   只能种在草地 / 泥土等透光地面）。spectator 已被入口 canPlace() 守卫拦截；Creative / Survival 均可种。生存消耗
    //   1 浆果（创造不耗 → 无限种）。**state=0**：与 worldgen placeSweetBerryBushes 散布阶段 1..2 区分 —— 玩家种下的是
    //   阶段 0 嫩丛（无果、需生长）；生长由 SweetBerryBush 自身 tick（worldgen 丛的生长路径）推进，本处仅落地 state=0 丛。
    //   分流前置：甜浆果既是食物又是种植材料。eventFilter 已对持甜浆果 + 命中草地 / 泥土的情况优先分流到 placeBlock
    //   （本分支种植），其余（瞄空气 / 非草地泥土）走 beginEating 进食（机制等价 MC 1.0「使用方块优先于使用物品」：
    //   右键草地种丛、右键空气 / 非地面吃浆果）。故持甜浆果右键草地 → 此分支落地 SweetBerryBush state=0。
    if (m_hotbar && m_world && heldItemId == RecipeRegistry::SweetBerryId) {
        if (m_hasHit) {
            const quint8 hitId = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
            if (hitId == BlockRegistry::Grass || hitId == BlockRegistry::Dirt) {
                const int wx = m_hitBx, wy = m_hitBy + 1, wz = m_hitBz; // 浆果丛种在命中格正上方一格
                // 目标须在界内 + 为空气（不覆盖实体 / 已有丛 / 草丛 / 水）。越界 setBlock 静默返 false → 提前挡防误耗浆果。
                if (wy < m_world->height() && m_world->blockAt(wx, wy, wz) == BlockRegistry::Air) {
                    m_world->setBlock(wx, wy, wz, BlockRegistry::SweetBerryBush, 0); // state=0（玩家种下嫩丛；生长由 tick 推进）
                    if (m_mode != Creative)
                        m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 浆果（创造不耗）
                    m_lastPlaceMs = now;
                    emit swingArm(); // 种植也是一次「放置」动作 → 挥手（t29）
                }
            }
        }
        return; // 甜浆果（种植成功 / 命中非草地泥土 / 未命中）均不再走方块放置路径
    }
    // t405 玻璃物品放置（spec「沙子冶炼产物玻璃 → 可放置为透明玻璃方块」）：手持玻璃物品（GlassId，材料段非方块）
    //   右键命中实体方块 → 在命中面相邻空气格（或水/岩浆格，替换流体）放置 Glass 方块（透明整立方）。机制等价
    //   MC 1.0 玻璃（glass：可放置的透明方块）。玻璃物品非方块 → selectedBlock 经 hotbar 归 Air，须在下方
    //   `m_selectedBlock == Air` 守卫之前分流（同桶 / 锄 / 种子 / 树苗分支模式）。**须命中**（须目标面定位放置点）。
    //   spectator 已被入口 canPlace() 守卫拦截；Creative / Survival 均可放。生存消耗 1 玻璃物品（创造不耗）。
    //   分层（PLAN §2）：放置属 Game/Physics（读射线命中 + 写 World + 写 Hotbar VM），不改 setBlock 语义。
    //   玻璃方块渲染透明（glassOnly 段半透材质）由 Renderer 呈现层负责，本处仅落地方块 id。
    if (m_hotbar && m_world && heldItemId == RecipeRegistry::GlassId) {
        if (m_hasHit) {
            const int tx = m_hitBx + m_hitNx, ty = m_hitBy + m_hitNy, tz = m_hitBz + m_hitNz;
            const quint8 tgt = m_world->blockAt(tx, ty, tz);
            // 目标须为空气或流体（水/岩浆可被玻璃替换，同方块放置语义）；实体方块不覆盖（保 t05 放置语义）。
            if (tgt == BlockRegistry::Air || tgt == BlockRegistry::Water || tgt == BlockRegistry::Lava) {
                m_world->setBlock(tx, ty, tz, BlockRegistry::Glass, 0); // state=0（玻璃无 state 语义）
                if (m_mode != Creative)
                    m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 玻璃物品（创造不耗）
                m_lastPlaceMs = now;
                emit swingArm(); // 放置也是一次「放置」动作 → 挥手（t29）
            }
        }
        return; // 玻璃物品（放置成功 / 未命中 / 目标被占）均不再走方块放置路径
    }
    // t267：面包已从 placeBlock 移除 —— 改由 eventFilter RightButton press 据持物 == BreadId 分流到
    //   beginEating（长按累积进食进度，~1.6s 满后 finishEating 消耗 + 恢复饥饿）。spec「单击即食→改长按右键」。
    //   旧单次右键食一件的分支已删（避免与长按路径并存导致单击仍即食）。饥饿恢复 + Survival 消耗 / Creative
    //   不耗的语义见 finishEating（同 t238 旧分支语义，仅触发方式改：单击 → 长按累积满）。
    // t720 画作放置（机制等价 MC 1.0 painting）：手持画作物品（PaintingId，材料段 0x242）右键**墙侧面**
    //   （命中面法线水平 ny==0）→ 对该面测最大可用矩形（锚格=命中面相邻格，向「观察者右」u 向贪心扩宽 /
    //   向下逐行扫，每格须 Air 且墙格 solid），随机选一张 w≤maxW && h≤maxH 的画作（27 张等权，简化 MC
    //   「先随机尺寸再随机画」）→ 锚格 setBlock(Painting, 0x80|face<<5|index) + 其余格 setWaterSilent
    //   (Painting, face<<5)（静默写免多次 blockPlaced / 音；state 编码见 blockregistry.h Painting 行）。
    //   **放不下（非侧面 / 无合格画 / 锚格被占）→ no-op 物品不消耗**（spec：挥臂但不消耗，机制等价 MC
    //   右键使用物品恒挥手）。生存放置成功消耗 1 画 / 创造不耗。画物品非方块（材料段）→ selectedBlock 归
    //   Air，须在 `m_selectedBlock == Air` 守卫之前分流（同桶 / 船 / 玻璃模式）。spectator 已被入口
    //   canPlace() 守卫拦截。分层（PLAN §2）：放置属 Game/Physics（读射线命中 + 写 World + 写 Hotbar VM），
    //   不改栅格语义（setBlock 入口）。渲染走呈现层 paintingHost（Main.qml，贴图不进图集）。
    if (m_hotbar && m_world && heldItemId == RecipeRegistry::PaintingId) {
        bool placed = false;
        if (m_hasHit && m_hitNy == 0 && m_hitNx != 0) {
            // 命中 ±X 侧面：法线 (±1,0,0) → face 0=+X / 1=-X（horizontalFacing 同源编码）。
            placed = tryPlacePainting((m_hitNx > 0) ? 0 : 1);
        } else if (m_hasHit && m_hitNy == 0 && m_hitNz != 0) {
            // 命中 ±Z 侧面：face 2=+Z / 3=-Z。
            placed = tryPlacePainting((m_hitNz > 0) ? 2 : 3);
        }
        if (placed && m_mode != Creative)
            m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 画（创造不耗）
        m_lastPlaceMs = now;
        emit swingArm(); // 使用画作是一次「使用」动作 → 挥手（放不下也挥，机制等价 MC 使用物品；不消耗）
        return; // 画作（放置成功 / 非侧面 / 放不下 / 未命中）均不再走方块放置路径
    }
    // t243 生物蛋 useBlock（spec「右键地面→生成对应生物」）：手持生物蛋（猪 / 牛 / 羊，材料段 0x20F..0x211）
    //   右键命中实体方块 → 在命中面相邻格生成对应 mob（EntityManager::spawnMobTyped）。机制等价 MC 1.0 spawn
    //   egg（机制对齐，非名词照搬）。蛋非方块（材料段）→ selectedBlock 经 hotbar 归 Air，须在下方
    //   `m_selectedBlock == Air` 守卫之前分流（同桶 / 锄 / 种子 / 面包分支模式）。**须命中**（spec「右键地面」——
    //   蛋需目标面定位生成点；瞄空气不生成）。spectator 已被入口 canPlace() 守卫拦截；Creative / Survival 均可用。
    //   生存消耗 1 蛋（创造不耗 → 无限生成，机制等价 MC 创造 spawn egg 不消耗）。
    //   生成位 = 命中面相邻格 (m_hitBx+nx, m_hitBy+ny, m_hitBz+nz)（同方块放置 tx/ty/tz 约定）→ 右键方块顶面在
    //   其上方一格生成、右键侧壁在玩家侧空气格生成；mob 半径 0.5、pos 存格中心 → spawnMobTyped 把 mob 放到该
    //   格中心，重力 tick 把它贴到地表（生成位高于地表时下落，机制等价 MC spawn egg 落地）。mobType / 占位配色
    //   据蛋 id 选（pig/cow/sheep 走 MobModel + 贴图，color 仅 mobType 0 测试路径读，传占位串即可）。
    //   分层（PLAN §2）：生成属 Game/Physics（读射线命中 + 调 EntityManager），不改栅格语义（setBlock 入口）。
    if (m_hotbar && m_world && m_entityManager
        && (heldItemId == RecipeRegistry::SpawnEggPigId
            || heldItemId == RecipeRegistry::SpawnEggCowId
            || heldItemId == RecipeRegistry::SpawnEggSheepId
            || heldItemId == RecipeRegistry::SpawnEggShamblerId
            || heldItemId == RecipeRegistry::SpawnEggBonesId
            || heldItemId == RecipeRegistry::SpawnEggStalkerId
            || heldItemId == RecipeRegistry::SpawnEggSpiderId
            || heldItemId == RecipeRegistry::SpawnEggChickenId
            || heldItemId == RecipeRegistry::SpawnEggSquidId)) {
        if (m_hasHit) {
            int mobType = 0;
            QString color; // 占位串（pig/cow/sheep 走 MobModel + 贴图，不读 color）
            if (heldItemId == RecipeRegistry::SpawnEggPigId) {
                mobType = EntityManager::MobPig;   color = QStringLiteral("#f0a8b0");
            } else if (heldItemId == RecipeRegistry::SpawnEggCowId) {
                mobType = EntityManager::MobCow;   color = QStringLiteral("#5a4030");
            } else if (heldItemId == RecipeRegistry::SpawnEggSheepId) {
                mobType = EntityManager::MobSheep; color = QStringLiteral("#f5f0e8");
            } else if (heldItemId == RecipeRegistry::SpawnEggShamblerId) {
                mobType = EntityManager::MobShambler; color = QStringLiteral("#4a6a3a"); // t287 敌对：暗绿腐肉（机制等价僵尸）
            } else if (heldItemId == RecipeRegistry::SpawnEggBonesId) {
                mobType = EntityManager::MobBones;    color = QStringLiteral("#d8d8d0"); // t287 敌对：灰白骨（机制等价骷髅）
            } else if (heldItemId == RecipeRegistry::SpawnEggStalkerId) {
                mobType = EntityManager::MobStalker;  color = QStringLiteral("#3a5a3a"); // t287 敌对：暗绿（机制等价苦力怕）
            } else if (heldItemId == RecipeRegistry::SpawnEggChickenId) {
                mobType = EntityManager::MobChicken;  color = QStringLiteral("#f5f0e4"); // t398 鸡：白羽（机制等价鸡；走 MobModel + 贴图）
            } else if (heldItemId == RecipeRegistry::SpawnEggSquidId) {
                mobType = EntityManager::MobSquid;    color = QStringLiteral("#6a4a3a"); // t399 鱿鱼：深褐橘斑（机制等价 squid；走 MobModel + 贴图）
            } else { // SpawnEggSpiderId
                mobType = EntityManager::MobSpider;   color = QStringLiteral("#2a1a1a"); // t285 敌对：暗黑（机制等价蜘蛛）
            }
            // 生成位 = 命中面相邻格（同方块放置；右键顶面 → 上方一格、右键侧壁 → 玩家侧空气格）。
            //   maxHealth 传 0 → spawnMobTyped 内部用 kDefaultMaxHealth（=10，MC 1.0 猪/牛/羊 5 心）；
            //   避开访问 EntityManager 私有常量（分层：Game 层不读 Entities 实现细节，仅传语义意图「默认血量」）。
            const int sx = m_hitBx + m_hitNx, sy = m_hitBy + m_hitNy, sz = m_hitBz + m_hitNz;
            m_entityManager->spawnMobTyped(sx, sy, sz, mobType, color, 0);
            if (m_mode != Creative)
                m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 蛋（创造不耗）
            m_lastPlaceMs = now;
            emit swingArm(); // 使用蛋也是一次「使用」动作 → 挥手（t29）
        }
        return; // 生物蛋（生成成功 / 未命中）均不再走方块放置路径
    }
    // t300 剪刀 useBlock（spec「玩家右键羊 + 持剪刀 → 羊变裸 + 掉羊毛物品」；机制等价 MC 1.0 剪羊毛）：
    //   手持剪刀（ToolRegistry::Shears，工具段 0x110）右键 → 在主选体射线之外**独立**跑一条「mob 命中射线」
    //   （findMobHit，同 beginMining 攻击路径）；命中**未剪羊毛的活体 sheep**（mobType==MobSheep 且 !sheared）
    //   → EntityManager::shearSheep（翻 sheared=true + 设 regrowCooldown + emit sheepSheared 让呈现层 spawnItem
    //   生成羊毛掉落）。命中非 sheep / 已剪羊毛 / 非 mob / 无命中 → 走原方块放置路径（剪刀不消耗 / 不放置，
    //   机制等价 MC 1.0：剪刀对非羊右键无反应）。
    //   **不要求 m_hasHit**（命中方块）：剪刀剪羊瞄准的是 mob（实体），不依赖方块命中格；瞄准悬空羊（无方块
    //   命中）亦应可剪 → 故本分支在 !m_hasHit 守卫之前。剪刀非方块（工具段 id>=0x100）→ selectedBlock 归 Air，
    //   须在 `m_selectedBlock == Air` 守卫之前分流（同桶 / 锄 / 蛋分支模式）。spectator 已被入口 canPlace() 守卫拦截；
    //   Creative / Survival 均可剪。生存模式消耗剪刀 1 耐久（机制等价 MC「剪刀每剪一羊 -1 耐久」；创造不耗）。
    //   分层（PLAN §2）：剪羊毛属 Game/Physics（读射线 + 调 EntityManager），不改栅格语义（setBlock 入口）。
    if (m_hotbar && m_world && m_entityManager && heldItemId == int(ToolRegistry::Shears)) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        // t510 扩展：剪刀剪羊（t300）OR 剪雪傀儡南瓜头（t510；spec「玩家持剪刀右键雪傀儡 → 南瓜掉落 + 雪傀儡
        //   变无头 derpy 形态」；机制等价 MC 1.0 剪刀剪雪傀儡南瓜头）。命中 mob 后据 mobType 分流：
        //   - MobSheep 且 !shearedAt → shearSheep（翻裸 + 掉羊毛）
        //   - MobSnowGolem 且 !snowGolemShearedAt → shearSnowGolem（翻无头 + 掉南瓜方块；呈现层 spawnItem Pumpkin=100）
        //   命中非羊非雪傀儡 / 已剪 / 无命中 → 走原方块放置路径。生存消耗剪刀 1 耐久（创造不耗，同剪羊毛）。
        if (mobIdx >= 0
            && m_entityManager->mobTypeAt(mobIdx) == EntityManager::MobSheep
            && !m_entityManager->shearedAt(mobIdx)) {
            m_entityManager->shearSheep(mobIdx);
            // 生存模式消耗剪刀 1 耐久（机制等价 MC 剪刀每剪一羊 -1；创造无限源不耗）。damageSelectedItem 对
            //   空手 / 非工具静默 no-op，归零自动清槽（剪刀破损消失）。同 attackMob / finishMiningAt 耐久消耗模式。
            if (m_mode == Survival) m_hotbar->damageSelectedItem();
            m_lastPlaceMs = now;
            emit swingArm(); // 剪羊毛也是一次「使用」动作 → 挥手（t29）
        } else if (mobIdx >= 0
                   && m_entityManager->mobTypeAt(mobIdx) == EntityManager::MobSnowGolem
                   && !m_entityManager->snowGolemShearedAt(mobIdx)) {
            m_entityManager->shearSnowGolem(mobIdx);
            // 生存模式消耗剪刀 1 耐久（同剪羊毛，机制等价 MC 剪刀每剪 -1 耐久；创造无限源不耗）。
            if (m_mode == Survival) m_hotbar->damageSelectedItem();
            m_lastPlaceMs = now;
            emit swingArm(); // 剪南瓜头也是一次「使用」动作 → 挥手（t29）
        }
        return; // 剪刀（剪羊/剪南瓜头成功 / 命中非羊非雪傀儡 / 已剪 / 无命中）均不再走方块放置路径
    }
    // t469 船交互（spec「右键船→骑乘；持船物品右键水面→放船」；机制等价 MC 1.0 boat）。两分支：
    //   (a) 骑乘（优先于放船）：跑独立 boat 命中射线（tryMount 内 findBoatHit，同剪刀剪羊 / 喂食的实体射线模式），
    //       命中船 → 上车。**不要求 m_hasHit**（瞄的是船实体非方块；悬空船亦可上，同剪刀 / 喂食）。已骑乘 → 不重复上。
    //   (b) 放船：手持船物品（OakBoat/SpruceBoat）+ 命中（需目标面定位放船点）→ 在命中水格 / 命中面相邻格放船
    //       （spawnBoat）；生存消耗 1 / 创造不耗。船物品非方块（材料段）→ selectedBlock 归 Air，须在 m_selectedBlock
    //       ==Air 守卫之前分流（同桶 / 蛋 / 剪刀模式）。
    //   spectator 已被入口 canPlace() 拦截；Creative / Survival 均可。分层：船交互属 Game/Physics（读射线 + 调
    //   BoatManager），不改栅格语义（setBlock 入口）。
    if (m_boatManager) {
        // (a) 骑乘 / 换船：命中船 → 上车（即便手持船物品也不另放，机制等价 MC 右键船优先上车）。
        //   t508 换船：已骑乘时命中**另一艘**船 → tryMount 内部切到新船（旧船释放骑乘态自然浮水，spec
        //   「骑船时右键另一艘船来坐上去」）；命中当前骑的船 / 未命中 → 返 false（落回放船路径）。
        //   tryMount 现允许在 ridingIndex>=0 时调（换船），故去掉了旧「ridingIndex() < 0」守卫。
        //   rv-low-batch2 骑乘互斥：骑矿车时不得再上船（旧版两边 rider 同时置位 → step 船分支先命中 → 矿车
        //   rider 残留成幽灵骑乘态）。守卫：骑矿车中 → 跳过上船（先 shift 下车才能换乘，机制等价 MC 同一
        //   时刻只能骑一个载具）。
        if (!(m_minecartManager && m_minecartManager->ridingIndex() >= 0)
            && m_boatManager->tryMount(position(), lookDirection(), kReach)) {
            m_lastPlaceMs = now;
            emit swingArm();
            return;
        }
        // (b) 放船：手持船物品 → 命中定位水面 / 支撑面 → 放船。
        //   t611 放行「骑船时放船」（用户：坐船时应还能放船 / 方块）：旧 `ridingIndex() < 0` 守卫使骑乘中持船
        //   右键凭空 no-op（连挥手臂没有）→ 用户读作「坐船被禁交互」。改放行 —— 骑乘中放船与 MC 1.0 一致
        //   （坐 A 船放 B 船、转身右键 B 船换骑均可；spawnBoat 后玩家仍骑 A，不自动换乘）。放船点与既有船
        //   重叠（<1.4 格，含骑乘船自身）由 spawnBoat 内部拒绝生成（review L12，防两船同格叠加）—— 落点被
        //   占时本次挥手不产生新船，瞄准邻近水面重放即可。
        //   boat 三轮「不能直接放水上」（用户报③）：旧条件含 m_hasHit → 瞄水面时主选体射线穿水命中水底实块
        //   （m_hasHit=true）但深水（>8 格）向上扫水面被 8 格封顶 → 船放水柱中途；水底超出射程（kReach=5）则
        //   m_hasHit=false → 放船分支直接不执行 → 两种情形都「不能直接放水上」。修：(a) 命中实块路径向上扫
        //   水面去 8 格封顶（扫到世界顶，深水一步定位真水面）；(b) 无命中路径跑独立水射线（RayFilter::HitWater，
        //   同钓竿抛竿模式）找视线首个水格 → 放水面。放陆地 / 冰面行为保持（命中实块路径 t508 兜底）。
        if (m_hotbar
            && (heldItemId == RecipeRegistry::OakBoatId || heldItemId == RecipeRegistry::SpruceBoatId)) {
            int tx = m_hitBx, ty = m_hitBy, tz = m_hitBz;
            bool boatTargetReady = false;
            if (m_hasHit) {
                // 目标格定位（t508 修「船下沉」根因之一）：主选体射线**不挡水**（t165 / lessons-learned：Water 在
                //   blocksRay 排除清单），故瞄水面时射线穿水命中**水底实块**（m_hitBy = 水底格 ≠ 水面格）。
                //   旧逻辑拿「水底格 + 命中面法线」算放船点 → 深水时船被放到水柱中途（远低于水面）→ 浮水 lerp
                //   速率（kBoatAccel）有限 → 船「慢慢上浮」肉眼读作「船下沉」。修：自命中面相邻格向上扫，
                //   找水柱的**最顶水格**（连续 Water 段的顶端 = 真水面），把船放到该水面格 → pos.y = 水面 + 1 − 吃水，
                //   即刻稳定浮在水面。瞄陆地 / 冰面（命中非水、向上无水）→ 用命中面相邻格（陆地 / 冰面放船，
                //   tick 无水重力落地停放置点）。
                if (m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) != BlockRegistry::Water) {
                    // 起点 = 命中面相邻格（瞄岸边 / 冰面时该格常即邻水的 Air / Water 格）。
                    const int sx = m_hitBx + m_hitNx, sz = m_hitBz + m_hitNz;
                    int sy = m_hitBy + m_hitNy;
                    // 向上找**最顶水格**（连续 Water 段的顶端 = 真水面）：扫到非水停，记最后一个 Water 格。
                    //   boat 三轮：去掉旧 8 格封顶（深水 >8 格时起点到水面之间全是水 → 8 格内扫不到非水停，
                    //   记的是水柱中途格 → 船放中途慢慢上浮）→ 扫到世界顶，深水一步定位真水面。无水（陆地 /
                    //   冰面）→ waterY<0 走兜底。
                    int waterY = -1;
                    for (int y = sy; y < m_world->height(); ++y) {
                        if (m_world->blockAt(sx, y, sz) == BlockRegistry::Water) waterY = y;
                        else if (waterY >= 0) break; // 已过水面（水 → 非水），停在最顶水格
                    }
                    // 起点 (sx,sy) 已在水面之上（sy 是 Air，其下 sy-1 是水）→ waterY = sy-1。
                    if (waterY < 0 && sy - 1 >= 0 && m_world->blockAt(sx, sy - 1, sz) == BlockRegistry::Water)
                        waterY = sy - 1;
                    if (waterY >= 0) {
                        tx = sx; ty = waterY; tz = sz; // 船放水面格（spawnBoat pos.y = waterY+1 = 水面顶）
                    } else {
                        // t508 二轮复盘修「放陆地悬空半格」（用户报③）：旧版 ty=sy（命中面相邻 Air 格），spawnBoat
                        //   pos.y = sy+1 = 支撑面顶 +1（船悬空 1 格），tick 无重力时永远悬着；即便有重力，spawn 瞬间到
                        //   落地之间肉眼能见「掉一格」。改：让 ty = 命中实块格（支撑面），spawnBoat pos.y = 实块格+1 =
                        //   支撑面顶 → spawn 即刻贴地，无悬空 / 无掉落闪烁。仅当命中实块（可踩 / 可碰撞）才这样取；
                        //   命中非实（如命中空气边缘 —— 罕见，瞄半砖下沿等）回退 sy 旧逻辑。
                        const bool hitSolid = m_world->isCollidable(m_hitBx, m_hitBy, m_hitBz)
                                              || BlockRegistry::isSolid(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz));
                        tx = sx; tz = sz;
                        ty = hitSolid ? m_hitBy : sy; // 命中实块 → 船中心 = 实块顶（spawnBoat +1）；否则旧 sy 行为
                    }
                }
                boatTargetReady = true;
            } else if (m_world) {
                // boat 三轮「不能直接放水上」（无命中路径）：瞄水面但水底实块超出射程（深水 / 水面上方视角）
                //   → 主射线无命中。跑独立水射线（RayFilter::HitWater，同钓竿抛竿 useFishingRod 模式）找视线
                //   首个水格 → 放该水面格（spawnBoat pos.y = waterY+1 = 水面顶）。命中非水（墙挡前 / 射程内无水）
                //   → 不放船（落回下方 !m_hasHit return，不放置不挥手）。
                const RayHit wHit = raycastVoxel(*m_world, position(), lookDirection(), kReach, RayFilter::HitWater);
                if (wHit.valid && m_world->blockAt(wHit.bx, wHit.by, wHit.bz) == BlockRegistry::Water) {
                    tx = wHit.bx; ty = wHit.by; tz = wHit.bz;
                    boatTargetReady = true;
                }
            }
            if (boatTargetReady) {
                const int boatType = (heldItemId == RecipeRegistry::SpruceBoatId)
                    ? BoatManager::Spruce : BoatManager::Oak;
                // review rv2-A2：仅 spawnBoat 真生成才消耗船物品（被拒 = 落点与既有船重叠 / 达 cap → 物品
                //   保留，玩家瞄准邻近水面重放）。旧版无条件扣 → 下船后在脚下重放船「物品没了、船也没出」。
                if (m_boatManager->spawnBoat(tx, ty, tz, boatType)) {
                    if (m_mode != Creative)
                        m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 船（创造不耗 → 无限放）
                    m_lastPlaceMs = now;
                    emit swingArm();
                }
                return;
            }
        }
    }
    // t565 矿车交互（spec「右键铁轨放矿车 / 右键矿车骑乘 / WASD 沿轨行驶」；机制等价 MC 1.0 minecart）。
    //   两分支（同船交互模式）：(a) 骑乘（优先）：跑独立矿车命中射线（tryMount 内 findCartHit）命中矿车 →
    //   上车（不要求 m_hasHit —— 瞄的是实体非方块）。(b) 放矿车：手持矿车物品（MinecartId）+ 命中方块是
    //   Rail → 在该轨格生成矿车（spawnCart，pos = 格中心轨面上）；生存消耗 1 / 创造不耗。矿车物品非方块
    //   （材料段）→ selectedBlock 归 Air，须在 m_selectedBlock==Air 守卫之前分流（同船 / 桶 / 蛋模式）。
    //   命中方块非 Rail → 不放（矿车只能放轨上，机制等价 MC 矿车须置于铁轨）。
    if (m_minecartManager) {
        // (a) 骑乘：命中矿车 → 上车（即便手持矿车物品也不另放，机制等价 MC 右键矿车优先上车）。
        //   rv-low-batch2 骑乘互斥：骑船时不得再上矿车（旧版两 rider 同时置位成幽灵骑乘态）。守卫：骑船中
        //   → 跳过上矿车（先 shift 下船才能换乘，与船侧守卫对称，机制等价 MC 同一时刻只能骑一个载具）。
        if (!(m_boatManager && m_boatManager->ridingIndex() >= 0)
            && m_minecartManager->tryMount(position(), lookDirection(), kReach)) {
            m_lastPlaceMs = now;
            emit swingArm();
            return;
        }
        // (b) 放矿车：手持矿车物品 + 未骑乘 + 命中方块为铁轨（t638 家族——普通 / 动力 / 探测轨均可放车）。
        if (m_minecartManager->ridingIndex() < 0 && m_hotbar
            && heldItemId == RecipeRegistry::MinecartId && m_hasHit
            && BlockRegistry::isRail(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) {
            m_minecartManager->spawnCart(m_hitBx, m_hitBy, m_hitBz, m_world);
            if (m_mode != Creative)
                m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 矿车（创造不耗）
            m_lastPlaceMs = now;
            emit swingArm();
            return;
        }
    }
    // t661 睡莲深水放置（dev-plan t661「只能放浅水（鼠标须指到水下方块）」修）：瞄深水（水底实块超出射程 /
    //    高俯角只见水面）时主选体射线无命中（m_hasHit=false）→ 旧版在此直接 return 睡莲永远放不了。补：
    //    持睡莲 + 无命中 → 跑独立水射线（RayFilter::HitWater，同桶舀水 / 放船无命中路径模式）找视线首个
    //    水格 → 从该格向上爬到水面之上首个非水格（同下方命中路径的睡莲 climb）→ 直接落位（跳过下方需要
    //    m_hasHit 的 tx/ty/tz 推导，写守卫复用下方 LilyPad 预检同款：下方须静水源 + 目标非睡莲）。
    //    分层（PLAN §2）：睡莲放置属 Game/Physics（读射线 + 写 World），不改栅格语义。
    if (!m_hasHit && m_world && m_hotbar && m_selectedBlock == BlockRegistry::LilyPad) {
        const RayHit wHit = raycastVoxel(*m_world, position(), lookDirection(), kReach, RayFilter::HitWater);
        if (wHit.valid && m_world->blockAt(wHit.bx, wHit.by, wHit.bz) == BlockRegistry::Water) {
            int lilyY = wHit.by;
            while (lilyY < m_world->height() && m_world->blockAt(wHit.bx, lilyY + 1, wHit.bz) == BlockRegistry::Water)
                ++lilyY; // 自首个水格爬到最顶水格（其上一格 = 水面 air 格 = 放置目标）
            const int tyAbove = lilyY + 1;
            if (tyAbove < m_world->height()
                && m_world->blockAt(wHit.bx, tyAbove, wHit.bz) == BlockRegistry::Air
                && m_world->stateAt(wHit.bx, lilyY, wHit.bz) == 0) { // 下方须静水源（流水面上不放，同 ④ 守卫）
                m_world->setBlock(wHit.bx, tyAbove, wHit.bz, BlockRegistry::LilyPad, 0);
                if (m_mode != Creative)
                    m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // 生存消耗 1 睡莲（创造不耗）
                m_lastPlaceMs = now;
                emit swingArm();
            }
        }
        return; // 睡莲（放置成功 / 水未中 / 非静水）均不再走方块放置路径
    }
    if (!m_hasHit) return; // t174：放块路径需命中（桶分支已 return；至此为非桶手持方块）
    if (m_selectedBlock == BlockRegistry::Air) return; // 空栈 → 右键不放置（也不挥手，t32）
    const int tx = m_hitBx + m_hitNx, tz = m_hitBz + m_hitNz;
    int ty = m_hitBy + m_hitNy;
    // t417 睡莲浮水面：选体射线走 HitTorch（updateRaycast）—— 水不挡射线 → 瞄水面时射线**穿过水**命中水底
    //   实块（如沼泽 1 格深水下的泥土），命中面 +Y → 放置目标格 ty = 水底+1 = **水源格**本身。睡莲几何贴格底
    //   （PartialBlockGeometry cellLocalY 1/16）→ 渲染在该水格底部 → 视觉沉到水下（spec「睡莲被放到水下方」）。
    //   修：睡莲须浮水面 → 目标格为水时逐格上爬到水面之上首个非水格（air），睡莲落该 air 格、几何贴其底 =
    //   水面 + 1/16 → 浮于水面（机制等价 MC 1.0 lily pad 浮水面非沉底）。worldgen 睡莲（placeSwampFlora）已置于
    //   水格上方一格 → 本修正仅补玩家放置路径（射线穿水落水格）。深水（海）逐格爬到海平面 air 格亦成立。
    if (m_selectedBlock == BlockRegistry::LilyPad) {
        while (ty < m_world->height() && m_world->blockAt(tx, ty, tz) == BlockRegistry::Water)
            ++ty;
    }
    const quint8 idByte = quint8(m_selectedBlock);
    // t146 放置态先算（供「重叠校验」+「实际写入」+「t163(b) 合并判定」复用，逻辑同源）：slab 据命中面 /
    //   玩家俯仰、stairs/door 据玩家水平朝向、fence/pressure_plate/trapdoor 默认 0（trapdoor 默认水平合）。
    //   door 占两格 → 另算上格。
    quint8 placeState = 0;
    if (BlockRegistry::isSlab(quint8(m_selectedBlock))) { // t412 木 / 石台阶共用同一放置态（命中面 / 命中点 Y 判上下半）
        // t212 命中面检测（spec「瞄已放方块上50%→上半砖；下50%→下半砖」，备注「命中点 y 与 hitCell y 差值判
        //   上下半」）：
        //   命中顶面（ny=+1，方块上方）→ 下半(state=0)：满格之上放下半砖，顶面+0.5 步可走上去（机制等价 MC
        //     顶面放置造台阶）；命中底面（ny=-1，天花板下方）→ 上半(state=1)：倒挂半砖。二者命中点恒在格边界
        //     （fracY=1/0），无「上下半」之分，沿用步进 / 倒挂约定不变。
        //   命中侧面（ny=0，靠墙 / 靠方块侧）→ 据命中点 Y 在该格内的小数位置：上50%(fracY>=0.5)→上半(state=1)，
        //     下50%→下半(state=0)。旧实现用玩家俯仰(m_pitch)判侧面半位，但默认略俯视(pitch=-42)→侧面恒下半，
        //     与「瞄方块上半应放上半砖」直觉相悖（spec 报「现恒下半砖」）；改读命中点 Y 后瞄哪半放哪半。
        if (m_hitNy > 0) placeState = 0;
        else if (m_hitNy < 0) placeState = 1;
        else {
            const float fracY = m_hitPointY - float(m_hitBy); // 命中点在命中格内的 Y 小数分量 [0,1]
            placeState = (fracY >= 0.5f) ? 1 : 0;
        }
    } else if (BlockRegistry::isStairs(quint8(m_selectedBlock))) { // t412 木 / 石楼梯共用朝向 / 倒置编码
        // t147：state[1:0]=水平朝向；bit2=上下倒置。
        //   t163 朝向修正：朝向 = horizontalFacing **异或 1**（取玩家反向）→ 楼梯「开口」朝玩家侧
        //     （partialblockgeometry「朝 X 开 = 背墙在对侧」：玩家面 +X(0) → 取 -X(1) → 背墙 +X 侧、开口 -X 朝玩家），
        //     玩家前行即可走上台阶（旧取正向 = 开口朝远离玩家 = 背对玩家，得绕行）。
        //   命中底面（天花板下方，m_hitNy<0）→ 倒置（整步在上、背墙在下）；否则正置。镜像 slab 的
        //   「ny<0 → 上半」约定，使「点方块下方」在所有半方块（slab/stairs）统一得到「倒挂」变体。
        placeState = quint8(((horizontalFacing() & 3) ^ 1) | (m_hitNy < 0 ? 4 : 0));
    } else if (m_selectedBlock == BlockRegistry::Torch || m_selectedBlock == BlockRegistry::RedstoneTorch) {
        // t214 火把附着方向写入 state（供 finishMiningAt 失撑掉落判定 + 存档 round-trip）。由命中面外法线
        //   推导（同 torchPlaced 信号传出的法线 → QML prefOrient，两路同源 → C++ 附着判定与 QML 渲染朝向
        //   放置时一致）。torch 走 ShapeNone → collisionAABBs/selectionAABBs/mesher 均不读 state，复用 state
        //   作附着编码零回归。
        // t638 ⑥ 红石火把并入：同 Torch 分支（cross 形网格渲染不读 attach state，但失撑掉落判定
        //   finishMiningAt 读——破支撑邻即掉，机制等价 MC 红石火把附着语义；torchOrientFromNormal 通用）。
        placeState = quint8(BlockRegistry::torchOrientFromNormal(m_hitNx, m_hitNy, m_hitNz));
    } else if (m_selectedBlock == BlockRegistry::Ladder) {
        // t501 木梯贴墙方向写入 state（供 mesher 单片贴墙 quad 摆位 + finishMiningAt 失撑掉落）。由命中面外
        //   法线推导所贴墙面水平方向（同 torch state 同源模式）。Ladder 走 ShapeNone → collision/selection
        //   不读 state，复用 state 作贴墙编码零回归（同 torch / chest 模式）。placeState 此处先粗赋（4 向），
        //   放置预检段再据 ladderFaceFromNormal（ny≠0 返 -1 = 非侧面）+ isFullCube（完整立方支撑面）守卫。
        //   placeState 不变（int→quint8 截断为 0..3 合法值；若预检段判拒则 placeBlock return 不写入）。
        const int lf = BlockRegistry::ladderFaceFromNormal(m_hitNx, m_hitNy, m_hitNz);
        placeState = quint8((lf < 0) ? 0 : lf); // 顶/底面 → 临时 0（预检段会拒）
    } else if (m_selectedBlock == BlockRegistry::Lever
               || m_selectedBlock == BlockRegistry::WoodButton
               || m_selectedBlock == BlockRegistry::StoneButton) {
        // t662 机关附着面写入 state bit[3:1]（贴地 / 四向贴墙 —— 放置吸附命中面，供 mesher mechBoxes 摆位
        //   + raycastAABBs 选中 + 失撑掉落解码；bit0 激活态留给 t628 右键翻位，互不干扰）。由命中面外法线
        //   推导（mechAttachFromNormal；点底面 ny<0 返 -1 = 天花板挂装 v1 不支持）。若 -1 → placeState 临时 0
        //   （下方机关放置预检段会拒，不写入）。
        const int ma = BlockRegistry::mechAttachFromNormal(m_hitNx, m_hitNy, m_hitNz);
        placeState = quint8(((ma < 0) ? BlockRegistry::MechAttachFloor : ma)
                            << BlockRegistry::MechAttachShift);
    } else if (m_selectedBlock == BlockRegistry::Chest) {
        // t225 箱子前面（锁面）朝玩家侧：state = horizontalFacing ^ 1（玩家朝向的反向 = 箱子前面所朝方向，
        //   机制等价 MC 1.0 箱子放置锁面朝玩家）。编码与 horizontalFacing 同源（0=+X 1=-X 2=+Z 3=-Z）；
        //   mesher 据 state 把 chest_front 贴到对应面，QML 盖子铰链摆在前面背侧（锁面相对）。
        placeState = quint8((horizontalFacing() & 3) ^ 1);
    } else if (m_selectedBlock == BlockRegistry::Furnace) {
        // t456 熔炉前面（炉口 furnace_front）朝玩家侧：state = horizontalFacing ^ 1（同箱子编码；机制等价 MC 1.0
        //   熔炉放置炉口朝玩家）。mesher 据 state 把 furnace_front 贴到对应面，其余侧面 furnace_side、顶/底
        //   furnace_top。此前熔炉无 placeState 分支 → state 恒 0 → 前面固定（tileFor 落 tileIndex 兜底恒 -Z），
        //   不随玩家朝向。
        placeState = quint8((horizontalFacing() & 3) ^ 1);
    } else if (m_selectedBlock == BlockRegistry::Dispenser) {
        // t486/t608 发射器前面（排出口 dispenser_front）朝玩家侧：state = horizontalFacing ^ 1（同箱子 / 熔炉
        //   编码；机制等价 MC 1.0 发射器放置排出口朝玩家，用户原话「和之前的熔炉一样放下来永远面朝玩家」）。
        //   mesher 据 state 把 dispenser_front 贴到对应面；**t608 起 state 是发射方向的唯一源** ——
        //   scanDispenserTraps 触发时据 state（chestFrontFace 解码）算朝向外向发射，压力板方位不参与。
        placeState = quint8((horizontalFacing() & 3) ^ 1);
    } else if (m_selectedBlock == BlockRegistry::Dropper) {
        // t609 投掷器前面（排出口 dropper_front）朝玩家侧：state = horizontalFacing ^ 1（同发射器 / 熔炉 / 箱子
        //   编码；机制等价 MC 1.0 投掷器放置排出口朝玩家）。mesher 据 state 把 dropper_front 贴到对应面；
        //   scanDispenserTraps 触发时据 state 解出弹出口外向（同发射器 t608 方向语义，单一方向源）。
        placeState = quint8((horizontalFacing() & 3) ^ 1);
    } else if (m_selectedBlock == BlockRegistry::Pumpkin) {
        // t638 ② 南瓜前面（刻面 pumpkin_face）朝玩家侧：state = horizontalFacing ^ 1（同箱子 / 熔炉 / 发射器
        //   编码；机制等价 MC 1.0 刻面南瓜放置时脸朝玩家——此前南瓜 placeBlock 未写 state → 恒 state=0
        //   → 前面恒 +X 固定方向，不随玩家朝向）。mesher（ChunkGeometry::tileFor）据 state 把 pumpkin_face
        //   贴到对应面（复用 chestFrontFace 解码）。造物（雪傀儡 / 铁傀儡）检测不读南瓜 state → 零影响。
        placeState = quint8((horizontalFacing() & 3) ^ 1);
    } else if (BlockRegistry::isRail(quint8(m_selectedBlock))) {
        // t666 铁轨放置轴向 = 玩家面向方位轴（spec：「面向 ±Z → NS 直轨；±X → EW 直轨」；MC 实际按放置
        //   上下文定轴，面向简化是接受的近似，见 blockregistry.h RailConn* 头注释）。无邻轨孤轨据此定轴
        //   （state bit5 RailAxisEWFlag：置=EW / 清=NS；0 连接时 mesher 读本位选直轨方向）。有邻轨的新轨
        //   连接（沿轴扩展 / 拐角）由 World::checkRailOnEdit 在放置后按 t666 规则集重算覆盖低 4 位（bit5
        //   随轴守恒写回）—— 故此处只需写初始轴偏好，无需预读邻居。
        const int hf = horizontalFacing() & 3; // 0=+X 1=-X 2=+Z 3=-Z（同 door/stairs 朝向编码）
        placeState = (hf < 2) ? BlockRegistry::RailAxisEWFlag : quint8(0);
    } else if (m_selectedBlock == BlockRegistry::Leaves || m_selectedBlock == BlockRegistry::SpruceLeaves) {
        // t305 玩家放置的树叶标 PersistentLeafBit（持久，不参与自然衰减）—— 机制等价 MC 1.0「玩家放置的树叶
        //   不衰减」。worldgen 叶 state=0（衰减候选）；玩家叶 state=本 bit → decayLeavesAround 跳过 → 创造建筑
        //   用的悬空叶不被清。mesher / collision / 选中均不读 leaves state（ShapeFull + culled 立方面）→ 零回归。
        //   t714：云杉叶同语义（同族叶机制）。
        placeState = BlockRegistry::PersistentLeafBit;
    }
    // t163(b) 同格双半砖合整（spec「同格下半砖上再放下半砖→合并为完整方块阻挡行走」）：
    //   右键 slab 时若点中的就是**同种** slab（木 / 石各自合并，不同材质不合 —— 机制等价 MC double slab 须同材质），
    //   且点击面朝向其空半（lower 顶面 ny>0 / upper 底面 ny<0）→ 在同格补出互补半，合成满格整立方（WoodSlab→Planks /
    //   CobbleSlab→Cobble；满格碰撞 → 阻挡行走；机制等价 MC「double slab = full block」）。合成前查重叠（满格比半砖大 →
    //   重查 overlapsPlayerAABB；玩家在格内则拒合，防自埋）。侧面点击（ny=0）不合（自然语义是放邻格）；同半 slab
    //   （如 lower 上再放 lower）走常规 target 放置。
    //   t206/t412：合并写 slabFullBlock(selected) + DoubleSlabMarkerBit 标记「源自双半砖」→ finishMiningAt 据本 bit
    //   掉 2× 对应半砖（非 1× 满砖；机制等价 MC「double slab 破坏掉 2 块半砖」）。详见 BlockRegistry::DoubleSlabMarkerBit。
    if (BlockRegistry::isSlab(quint8(m_selectedBlock))
        && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == m_selectedBlock) {
        const quint8 hitState = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
        const bool hitUpper = (hitState & 1) != 0;
        // t212 同格互补半合并（spec「同格上半+下半→合并整砖」+ 修「放了上半砖后同格下半砖放不下」/
        //   「瞄上方却放旁边」）：据命中点 Y 判是否点中**空半**——下半砖(filled [0,0.5]) 点其上空半(fracY>=0.5)、
        //   上半砖(filled [0.5,1]) 点其下空半(fracY<0.5) → 合并为满格。旧实现仅认顶/底面(ny≠0)合并，
        //   侧面点击不合并 → 右键已有半砖侧面会落到邻格（spec 报「瞄上方却放旁边」）而非补齐同格；且上半砖只能
        //   由底面合并（玩家很难从下方点中）→「放了上半砖后同格下半砖放不下」。改读命中点 Y 后，任意面点中
        //   空半即合并（含侧面）；点中实半则 fall-through 走下方常规邻格放置。
        const float fracY = m_hitPointY - float(m_hitBy);
        // t361 上半砖底面（空半的边界平面）需可靠合并：选体射线走 sub-AABB 精确测试（raycast.cpp），上半砖
        //   sub-AABB 自 Y=0.5 起 → 点其底面 fracY 恒 ≈0.5（非 <0.5），旧 hitUpper?(fracY<0.5) 永不成立 →
        //   点上半砖底面落到下方邻格（放上半砖）而非同格合并（spec 报「须点邻居底面」）。空半无几何体，玩家
        //   从空半侧只能命中其边界平面（上半砖底面 m_hitNy<0 / 下半砖顶面 m_hitNy>0）→ 补法线判定免 fracY==0.5
        //   浮点抖动：点中空半边界平面即「想填空半」→ 合并。
        const bool merge = hitUpper ? (m_hitNy < 0 || fracY < 0.5f) : (m_hitNy > 0 || fracY >= 0.5f);
        if (merge) {
            const quint8 fullId = BlockRegistry::slabFullBlock(quint8(m_selectedBlock));
            if (overlapsPlayerAABB(m_hitBx, m_hitBy, m_hitBz, fullId, 0)) return;
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, fullId,
                              BlockRegistry::DoubleSlabMarkerBit);
            // t669 放置消耗收口：生存放置由 C++ 侧统一消耗 1 件（原由 QML onBlockPlaced blanket takeStack
            //   承担，但那会误扣非放置类 setBlock（锄/踩踏）；合并成功 = 一次放置动作 → 消耗 1 半砖）。
            if (m_mode == Survival) m_hotbar->takeStack(m_hotbar->selectedSlot(), 1);
            m_lastPlaceMs = now;
            emit swingArm(); // 合成也是一次「放置」动作 → 挥手（t29）
            return;
        }
    }
    // t262 邻格互补半砖合并（spec「角落下半砖上沿邻墙侧面放上半砖」）：命中实体方块（墙 / 满砖）的面、
    //   目标格已是**同种互补半**半砖时 → 把目标格合并为整砖（满格 + 双半砖标记）。场景：墙角下半砖上，玩家瞄
    //   邻墙侧面（上半高度）想补上半砖（非顶面）。旧实现：目标格非 air/water → 下方排开水守卫直接 return
    //   （spec 报「现不行」）。MC 语义：目标格已有同种互补半砖时合并为 double slab = full block（同 t163(b) 同格
    //   合并，只是命中格从「slab」换成「邻接实体方块」——命中点 Y 仍据邻墙侧面 fracY 正确算出想放的半位）。
    //   到此说明上面 t163(b) 同格合并未触发（命中格非同种 slab，或点中 slab 实半 fall-through）→ 检**目标格**。
    //   同半（如目标下半 + 新下半）不合（几何重叠）；异种（木 vs 石）不合（不同 double slab 材质）→ 不合，fall-through。
    if (BlockRegistry::isSlab(quint8(m_selectedBlock))) {
        const quint8 tId = m_world->blockAt(tx, ty, tz);
        if (tId == m_selectedBlock) { // 同种半砖 → 互补半可合并（异种不合）
            const quint8 tState = m_world->stateAt(tx, ty, tz);
            const bool tUpper = (tState & 1) != 0;
            const bool newUpper = (placeState & 1) != 0; // placeState 已据命中面 / 命中点 Y 算好（见上方 slab 分支）
            if (newUpper != tUpper) { // 互补半 → 合并目标格为整砖
                const quint8 fullId = BlockRegistry::slabFullBlock(quint8(m_selectedBlock));
                if (overlapsPlayerAABB(tx, ty, tz, fullId, 0)) return; // 合成满砖前查自埋（同 t163b）
                m_world->setBlock(tx, ty, tz, fullId,
                                  BlockRegistry::DoubleSlabMarkerBit);
                // t669 放置消耗收口（同 t163(b) 同格合并：合并 = 一次放置动作 → 生存消耗 1 半砖）。
                if (m_mode == Survival) m_hotbar->takeStack(m_hotbar->selectedSlot(), 1);
                m_lastPlaceMs = now;
                emit swingArm(); // 合成也是一次「放置」动作 → 挥手（t29）
                return;
            }
        }
    }
    // t505 积雪层堆叠（机制等价 MC 1.0 snow layer 同格堆叠 8 层）：玩家持 SnowLayer 右键**已存在的 SnowLayer**
    //   → 层数 +1（state +1，clamp 到 SnowLayerStageMax=7；高度 (state+1)/8 递增，机制等价 MC 雪层逐层堆高到满格）。
    //   命中格（m_hitBx/y/bz）= 玩家点击的现有雪层；在其上「补雪」而非落到邻格。同格满 8 层（state=7 = 满格高 1.0）
    //   时不再叠加（继续堆会变满格，机制对标 MC 雪层 8 层封顶）。合成也是「放置」→ 挥手；走 5 参数 setBlock
    //   （id 不变只 state 变 → 仅 worldChanged 重建 mesh、不发 broken/placed；同甘蔗采摘降阶段模式）。
    //   预检 overlapsPlayerAABB：更高层雪层碰撞盒更高，可能嵌入玩家 → 合成前查自埋（同 slab 合成满砖）。
    //   **区别 slab 合并**（两半合成整砖、清空再写满格 + marker）：雪层堆叠保留 SnowLayer id，仅 state+1。
    if (m_selectedBlock == BlockRegistry::SnowLayer
        && m_world->blockAt(m_hitBx, m_hitBy, m_hitBz) == BlockRegistry::SnowLayer) {
        const quint8 curState = m_world->stateAt(m_hitBx, m_hitBy, m_hitBz);
        if (curState < BlockRegistry::SnowLayerStageMax) {
            const quint8 newState = quint8(curState + 1);
            if (overlapsPlayerAABB(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::SnowLayer, newState)) return;
            m_world->setBlock(m_hitBx, m_hitBy, m_hitBz, BlockRegistry::SnowLayer, newState);
            m_lastPlaceMs = now;
            emit swingArm();
            return;
        }
        // state 已满（7 = 满格高 1.0）→ 不再叠加，落回常规放置（在邻格放新雪层 state=0）。
    }
    // t554 积雪层放置预检：只能放在**完整方块顶面**（机制等价 MC 1.0 雪层不可贴侧壁 / 悬空放）。
    //   根因：雪层走通用放置路径（无支撑预检）→ 玩家点树干 / 墙体**侧面**时 target 落水平邻格、其下方
    //   为空气 → 雪层悬空贴在树侧。修 = 目标格正下方 (tx,ty-1,tz) 须为完整立方（isFullCube）：
    //     · 顶面放置（ny>0）→ target = 命中方块正上方 → 下方 = 命中方块（完整 → 允）。
    //     · 侧壁放置（ny==0）→ target = 水平邻格 → 下方通常为 Air → 拒（不挥）。
    //   满 8 层雪层（state=SnowLayerStageMax，高 1.0 ≈ 满格雪块）亦算有效支撑：堆叠分支满层后回落
    //   常规放置（点满层雪柱顶 → target 下方即满层雪层），机制等价 MC 8 层雪顶可另起新雪层。
    //   雪层自身不满层（state<7）不算支撑（isFullCube(SnowLayer)=false → 拒在薄雪层邻格悬空放新层；
    //   同格叠加走上方堆叠分支，不经本判）。
    if (m_selectedBlock == BlockRegistry::SnowLayer) {
        const quint8 below = m_world->blockAt(tx, ty - 1, tz);
        const bool belowSupport = BlockRegistry::isFullCube(below)
            || (below == BlockRegistry::SnowLayer
                && m_world->stateAt(tx, ty - 1, tz) == BlockRegistry::SnowLayerStageMax);
        if (!belowSupport) return; // 下方非完整立方 / 非满层雪 → 悬空 / 侧放 → 拒（不挥）
    }
    // t198 水中可放方块（排开水）/ t351 岩浆同理（排开岩浆）：目标格为空气 / 水 / 岩浆均可放置；流体被
    //   方块直接覆盖 → World::setBlock 内 oldId=Water/Lava → newId=实体走「放置」分支（仅发 blockPlaced，
    //   不发 blockBroken → 无破块粒子 / 音；流体静默消失，机制等价 MC「方块填入流体格排开流体」）。已有实体
    //   方块 → 拒（不覆盖）。下一 tickWaterFlow/tickLavaFlow 因目标格已非 air → 不再向其扩散 → 邻接流按失撑衰退。
    {
        const quint8 tid = m_world->blockAt(tx, ty, tz);
        if (tid != BlockRegistry::Air && tid != BlockRegistry::Water
            && tid != BlockRegistry::Lava) return; // 已有实体方块 → 不放
    }
    // t669③ 耕地上方放方块 → 耕地变泥土（MC 规则：方块落在耕地上 → 耕地无法保持湿润支撑，回土；机制等价
    //   MC 1.0 在耕地上放方块即回土）。目标格下方 (tx,ty-1,tz) 若为 Farmland → 静默写回 Dirt（setBlockSilent：
    //   回土是系统变化非玩家破/放 → 不发 blockPlaced/broken，免粒子/音/误扣选中槽；worldChanged 重建 mesh）。
    //   种子种植走上方 crop 专用分支（种在耕地上方一格、目标格即空气上方，never 到此），不触发回土；
    //   slab / 压力板 / 火把等非满格方块放置亦回土（机制等价 MC「任何方块放耕地顶 → 回土」）。
    if (ty - 1 >= 0 && m_world->blockAt(tx, ty - 1, tz) == BlockRegistry::Farmland)
        m_world->setBlockSilent(tx, ty - 1, tz, BlockRegistry::Dirt, 0);
    const bool isDoor = BlockRegistry::isDoor(m_selectedBlock); // t466 统一经 isDoor 谓词覆盖 WoodDoor + SpruceDoor
    const quint8 doorFacing = quint8(horizontalFacing() & 3); // door 朝向（上下格同 facing；上格 +bit3）
    // t428/t496 床双格（head+foot 横置，如门但水平相邻）：foot 落命中面相邻格 (tx,ty,tz)，head 落 foot 的「玩家
    //   朝向同向」水平邻格（spec t496：床脚落在放置处、床头朝远离玩家 —— 区别于熔炉 / 箱子「前面朝玩家」语义，
    //   床的 head 在玩家前方、foot 在玩家脚下）。state 编码 bit[1:0]=head→foot 方向、bit3=head(1)/foot(0)（同 door
    //   复用 bit3 标半格）。bedPartnerOffset 解码约定：stored bit[1:0] = head→foot 方向（foot 所在侧）。故放置时存
    //   bedFacing = horizontalFacing ^ 1（玩家前向的反向 = 玩家脚下指向身后 = head→foot 方向），则 foot→head = -front
    //   = 玩家前向 → head 落在玩家前方（远离玩家），符合 spec。
    //   （旧版存 bedFacing = horizontalFacing 未取反 → head→foot = 玩家前向 → head 落玩家身后 = 床头朝向玩家脚侧，
    //   即用户复盘「朝 +X 放时床头指向玩家脚侧」的根因。修：放床时 state 取 horizontalFacing ^ 1，同 chest/furnace
    //   放置 state 取 ^ 1 的同源手法，但语义不同 —— 那里是「前面朝玩家」，这里是「head 远离玩家」。）
    const bool isBed = BlockRegistry::isBed(idByte);
    const quint8 bedFacing = quint8((horizontalFacing() & 3) ^ 1); // head→foot 方向（玩家前向反向）
    int hdx = 0, hdz = 0;
    BlockRegistry::bedPartnerOffset(bedFacing, hdx, hdz); // foot → 配对 head 偏移（= -front = 玩家前向）
    // misc 二轮 压力板放置放宽（spec「压力板能浮空放在侧边方块」）：压力板允许放在**所点方块的顶面 OR 侧面**
    //   （贴侧墙浮空），不强制下方实体。机制对齐 MC 压力板可贴墙放。当前命中面：
    //     · m_hitNy > 0（顶面）→ 落命中方块正上方格（常规，下方=命中方块实体）。
    //     · m_hitNy == 0（侧面）→ 落命中方块水平邻格（贴墙浮空，下方可能是 Air）。
    //   侧面放置时放宽 overlapsPlayerAABB（薄板贴 cell 底、几乎不占玩家躯干体积；玩家贴墙站时其 AABB 跨入侧格
    //   → 严格重叠会拒，但薄板本身不实质阻挡 → 放行贴墙侧放）。仅压力板分支放宽，其它方块放置逻辑不动。
    const bool isPlate = BlockRegistry::isPressurePlate(m_selectedBlock);
    const bool plateSidePlace = isPlate && (m_hitNy == 0); // 贴墙侧放（浮空，下方无实体亦允）
    // 与玩家重叠 → 不放（防自埋 / 卡死）。t146：按「将放置方块的实际形状 sub-AABB」判 —— 不完整方块可能
    //   只占半格，玩家在另半格内仍可放；air/torch 无 sub-AABB → 不挡（允许放入玩家格，机制等价 MC）。
    //   door 占两格 → 上下格都查；bed 占两格 → foot + head 两格都查（bed ShapeFull 与 state 无关，state 任意）。
    if (overlapsPlayerAABB(tx, ty, tz, idByte, isDoor ? doorFacing : placeState)
        && !plateSidePlace) return; // misc 二轮：压力板贴墙侧放豁免玩家重叠（薄板不实质阻挡；其它方块仍守）
    if (isDoor && overlapsPlayerAABB(tx, ty + 1, tz, idByte, quint8(doorFacing | 8))) return;
    if (isBed && overlapsPlayerAABB(tx + hdx, ty, tz + hdz, idByte, quint8(bedFacing | 8))) return;
    // t114 火把放置预检：火把需挂到实体邻居（下 / 四侧之一为实体方块），否则拒绝（机制等价 MC「火把
    // 需要支撑面」—— 平地或墙面）。判定用 BlockRegistry::isSolid（实体方块语义；不挂到空气 / 另一火把
    // / 工作台等非实体方块）。torchHost 据同样语义在运行期推断朝向（下 solid=垂直 / 侧 solid=横插）。
    // t638 ⑥ 红石火把并入火把预检（机制等价 MC 红石火把同火把须支撑面；cross 形网格渲染贴图自带火把
    //   剪影不依赖邻居推断朝向，但支撑语义 / 失撑掉落（finishMiningAt 破支撑邻即掉）与火把一致）。
    if (m_selectedBlock == BlockRegistry::Torch || m_selectedBlock == BlockRegistry::RedstoneTorch) {
        const bool below = BlockRegistry::isSolid(m_world->blockAt(tx, ty - 1, tz));
        const bool px = BlockRegistry::isSolid(m_world->blockAt(tx + 1, ty, tz));
        const bool nx = BlockRegistry::isSolid(m_world->blockAt(tx - 1, ty, tz));
        const bool pz = BlockRegistry::isSolid(m_world->blockAt(tx, ty, tz + 1));
        const bool nz = BlockRegistry::isSolid(m_world->blockAt(tx, ty, tz - 1));
        if (!below && !px && !nx && !pz && !nz) return; // 无任何实体邻居 → 悬空火把，拒绝放置
    }
    // t638 ③ 轨上放轨拒绝（spec「铁轨不能放在铁轨上」）+ t667 完整立方顶面支撑：
    //   铁轨只能放在**完整方块顶面**（机制等价 MC rails 须全支撑块）。两重守卫（覆盖全部放置路径）：
    //   ① 目标格已是铁轨族（普通 / 动力 / 探测）→ 拒（不挥）。常规邻格放置下目标格通常非轨（瞄轨面 →
    //     目标 = 轨上格 / 轨旁格），但 t638 选中框薄板化后贴轨平扫的落格可能邻轨、以及流体排开分支的组
    //     合下仍可能同格 → 显式拒（机制等价 MC 1.0 轨不可叠轨 / 同格互斥）。轨道连接 state 由
    //     World::checkRailOnEdit 在放置后自动重算（家族互连）。
    //   ② 目标格正下方 (tx,ty-1,tz) 须完整立方（isFullCube，同雪层 t554 支撑判定）→ 以下路径全被覆盖：
    //     · 瞄铁轨顶面放轨（目标 = 轨上方 air 格、下方 = 轨）→ 拒 —— 修 t638 拒绝漏洞「轨上放轨仍可行」；
    //     · 悬空 / 侧壁放轨（下方 Air / 非完整）→ 拒（不挥）；
    //     · 耕地（0.9375 ≠ 满立方）→ 拒；薄雪层 / 半砖 / 楼梯等非完整立方顶面 → 拒。
    //     （简化：「完整立方顶面 only」；MC 允许 rails 放 top-half slab 顶 —— 本工程统一不收，文档化。）
    if (BlockRegistry::isRail(quint8(m_selectedBlock))) {
        if (BlockRegistry::isRail(m_world->blockAt(tx, ty, tz))) return; // ① 同格已有轨 → 拒
        const quint8 below = m_world->blockAt(tx, ty - 1, tz);
        if (!BlockRegistry::isFullCube(below)) return; // ② 下方非完整立方支撑 → 拒（不挥）
    }
    // t501 木梯放置预检（spec「须完整方块侧支撑」）：木梯贴**完整立方方块的侧面**（机制等价 MC 1.0 ladder
    //   须贴实体方块面）。两重守卫：
    //   ① 必须侧面贴墙（命中面法线 ny==0）—— 顶/底面非合法贴墙方向，玩家点顶/底放梯 → 拒（不挥）。法线
    //     方向同时决定 placeState（ladderFaceFromNormal）。
    //   ② 命中方块必须是**完整立方**（isFullCube）—— 草丛/门/活版门/栅栏/火把等不完整或非实体方块的侧不
    //     可贴（机制等价 MC「梯子须贴完整方块面」，spec 明确点名草/门/活版门不完整方块侧不可放）。半砖、
    //     楼梯等异形方块亦不完整（isFullCube=false）→ 拒。
    //   命中方块 = (m_hitBx, m_hitBy, m_hitBz)（玩家射线命中格），非放置目标格 (tx,ty,tz)。木梯贴命中方块的
    //   「玩家侧」面 → 命中方块即其支撑墙。
    if (m_selectedBlock == BlockRegistry::Ladder) {
        const int lf = BlockRegistry::ladderFaceFromNormal(m_hitNx, m_hitNy, m_hitNz);
        if (lf < 0) return; // ① 非侧面（顶/底面）→ 拒
        const quint8 hitBlock = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
        if (!BlockRegistry::isFullCube(hitBlock)) return; // ② 非完整立方支撑 → 拒
    }
    // t662 机关方块（Lever / WoodButton / StoneButton）放置预检（机制等价 MC 1.0 lever/button 须贴完整方块面）：
    //   ① 命中面外法线合法（顶面贴地 / 四向侧面贴墙；底面 ny<0 = 天花板挂装 v1 不支持 → 拒）；
    //   ② 命中方块（= 唯一支撑）须完整立方（isFullCube，同木梯 t501 守卫——机关贴墙/贴地都须实体面，
    //     草丛 / 门 / 半砖等不完整方块面拒挂）。附着编码 placeState 已在上方 placeBlock 段算好写入。
    if (m_selectedBlock == BlockRegistry::Lever
        || m_selectedBlock == BlockRegistry::WoodButton
        || m_selectedBlock == BlockRegistry::StoneButton) {
        if (BlockRegistry::mechAttachFromNormal(m_hitNx, m_hitNy, m_hitNz) < 0) return; // 底面 → 拒
        if (!BlockRegistry::isFullCube(m_world->blockAt(m_hitBx, m_hitBy, m_hitBz))) return; // 非完整支撑 → 拒
    }
    // t394/t445 仙人掌放置预检：（1）仅可放在沙子或仙人掌正上方（机制等价 MC 1.0 仙人掌须沙地 / 仙人掌支撑）。
    //   目标格的下方须为 Sand 或 Cactus；否则拒绝放置（不挥）。命中方块顶面放置 → target 下方 = 命中方块
    //   （须沙 / 仙人掌）；命中侧壁放置 → target 下方 = 空气 / 其它 → 拒（侧壁悬空不能放仙人掌，机制等价 MC）。
    //   （2）t445 ④ 水平 4 邻须无方块（机制等价 MC 1.0 仙人掌不可邻接任何方块 —— 邻接即被扎破掉落）：
    //   目标格水平 4 邻任一非 Air → 拒（不挥）。World setBlock 放块路径另做反应式验证（邻接仙人掌即整柱掉落），
    //   覆盖「放置后邻接方块出现」（如落沙落旁、玩家放沙旁）的非玩家放置路径。
    if (m_selectedBlock == BlockRegistry::Cactus) {
        const quint8 below = m_world->blockAt(tx, ty - 1, tz);
        if (below != BlockRegistry::Sand && below != BlockRegistry::Cactus) return;
        if (m_world->blockAt(tx + 1, ty, tz) != BlockRegistry::Air
            || m_world->blockAt(tx - 1, ty, tz) != BlockRegistry::Air
            || m_world->blockAt(tx, ty, tz + 1) != BlockRegistry::Air
            || m_world->blockAt(tx, ty, tz - 1) != BlockRegistry::Air) return;
    }
    // t394 枯死的灌木放置预检：仅可放在沙子正上方（机制等价 MC 1.0 dead bush 生于沙地）。
    //   目标格的下方须为 Sand；否则拒绝放置（不挥）。与种子 / 树苗「须草地 / 泥土」同支撑语义。
    if (m_selectedBlock == BlockRegistry::DeadBush) {
        if (m_world->blockAt(tx, ty - 1, tz) != BlockRegistry::Sand) return;
    }
    // t397 花放置预检：仅可放在草地 / 泥土 / 耕地正上方（机制等价 MC 1.0 花生于草地 / 泥土）。
    //   目标格的下方须为 Grass / Dirt / Farmland；否则拒绝放置（不挥）。与树苗「须草地 / 泥土」同支撑语义。
    if (BlockRegistry::isFlower(m_selectedBlock)) {
        const quint8 below = m_world->blockAt(tx, ty - 1, tz);
        if (below != BlockRegistry::Grass && below != BlockRegistry::Dirt
            && below != BlockRegistry::Farmland) return;
    }
    // t507 蘑菇放置预检（红 Mushroom / 白 BrownMushroom）：仅可放在草地 / 泥土正上方（机制等价 MC 1.0 蘑菇
    //   生于草地 / 泥土 / 阴暗处，本工程不强制光照判定）。目标格下方须为 Grass / Dirt；否则拒（不挥）。
    //   与花同支撑语义（蘑菇族与花共用 cross 几何 + 失撑掉落校验，但放置支撑更宽：MC 蘑菇亦可生于石头 / 倒木
    //   等阴暗面，本工程简化仅草地 / 泥土）。经 isMushroom 单一权威谓词覆盖红 / 白两蘑菇（同 isFlower 段模式）。
    if (BlockRegistry::isMushroom(m_selectedBlock)) {
        const quint8 below = m_world->blockAt(tx, ty - 1, tz);
        if (below != BlockRegistry::Grass && below != BlockRegistry::Dirt) return;
    }
    // t397/t423/t547 甘蔗放置预检：
    //   （1）目标格须为**空气** —— 不能种在水里（t547③「能种在水里面不对」根因：主选体射线**不挡水**（t165）→ 瞄
    //     水面时射线穿水命中水底实块 → 目标格 ty 落**水格**本身，旧判定漏查目标格类型 → 甘蔗种进水；修 = 目标非
    //     空气即拒，机制等价 MC 甘蔗不可生于水中）。
    //   （2）仅可放在草地 / 泥土 / 沙地 / 甘蔗正上方（机制等价 MC 1.0 sugar cane 须草地 / 沙地 / 甘蔗支撑）。
    //   （3）t423 须邻水：**柱基**（叠甘蔗时沿柱下走到首个非甘蔗支撑格 = 沙/草/土基）或其下一层的水平 4 邻任一为
    //     Water 才可放（t547①「放不下第三格」根因：旧判定只查 ty-1/ty-2，叠到第 3 格时两层都是甘蔗、远离水面 →
    //     邻水恒假 → 第 3 格永远放不下。修 = 沿柱下走到柱基再查邻水，与 worldgen placeSugarcane 的 surfaceY /
    //     surfaceY-1 双层查水同语义 → 玩家可在海岸沙顶补种 / 叠高，远水陆地 / 沙漠内陆拒）。
    //   （4）t547① 放置高度上限：现有柱高 + 1 ≤ 3（机制等价 MC 甘蔗最高 3 格；叠到 3 后第 4 格拒）。
    //   水格水平 4 邻走 blockAt（越界安全返回，同火把预检）。
    if (m_selectedBlock == BlockRegistry::Sugarcane) {
        const quint8 tid = m_world->blockAt(tx, ty, tz);
        if (tid != BlockRegistry::Air) return; // ① 目标须空气（不能种进水 / 岩浆 / 实体）
        const quint8 below = m_world->blockAt(tx, ty - 1, tz);
        if (below != BlockRegistry::Grass && below != BlockRegistry::Dirt
            && below != BlockRegistry::Sand && below != BlockRegistry::Sugarcane) return;
        // ② 柱基定位 + 高度统计：自 ty-1 沿甘蔗柱向下走到首个非甘蔗格（= 沙/草/土基）。
        //   columnHeight = 现有甘蔗格数（含 ty-1）；放置后总量 = columnHeight+1，≤3 才允（最高 3 格）。
        int baseY = ty - 1;
        int columnHeight = 0;
        while (baseY >= 0 && m_world->blockAt(tx, baseY, tz) == BlockRegistry::Sugarcane) {
            ++columnHeight;
            --baseY;
        }
        constexpr int kSugarcanePlaceMaxHeight = 3; // 玩家放置最高 3 格（机制等价 MC 甘蔗 max 3）
        if (columnHeight >= kSugarcanePlaceMaxHeight) return; // 已达上限 → 拒（不挥）
        // ③ 邻水门：柱基 baseY / 下一层 baseY-1 的水平 4 邻任一为 Water → 允；否则拒。
        //   单格直放（below=沙/草/土，baseY=ty-1）→ 等价旧 ty-1/ty-2 双层查水；叠放 → 回落到柱基查水。
        const auto waterAdj = [&](int yy) -> bool {
            return m_world->blockAt(tx + 1, yy, tz)     == BlockRegistry::Water
                || m_world->blockAt(tx - 1, yy, tz)     == BlockRegistry::Water
                || m_world->blockAt(tx,     yy, tz + 1) == BlockRegistry::Water
                || m_world->blockAt(tx,     yy, tz - 1) == BlockRegistry::Water;
        };
        if (!waterAdj(baseY) && !waterAdj(baseY - 1)) return; // 柱基两层均不邻水 → 拒（不挥）
    }
    // t444 睡莲放置预检（spec「仅静止水面可放 / 地上流水不可 / 不可叠放」）：
    //   放置目标格 ty 经上方 climb（见入口段）已是水面之上首个非水格（air）。机制等价 MC 1.0 lily pad 仅可放于
    //   静水源水面、不可叠放。
    //   ④ 仅静止水面：目标格正下方（ty-1 = 水面格）须为水源 Water state==0。流水(state>0) / 地面（下方非水） /
    //      悬空（无下方）→ 拒（不挥）。worldgen 睡莲（placeSwampFlora）仅放水源面，玩家放置须同守。
    //   ⑥ 不可叠放：目标格已有睡莲 → 拒（机制等价 MC lily pad 不可堆叠，防叠柱）。瞄水面时射线穿水、climb 落到
    //      已有睡莲格；瞄睡莲顶时 ty 落其上空气格但下方非水（是睡莲）→ ④ 即拒。本条对「同格已有睡莲」显式补刀。
    if (m_selectedBlock == BlockRegistry::LilyPad) {
        if (ty >= m_world->height() || ty - 1 < 0) return;              // 全高水柱无表面气格 / y=0 无下方水 → 拒
        const quint8 belowId = m_world->blockAt(tx, ty - 1, tz);
        const quint8 belowState = m_world->stateAt(tx, ty - 1, tz);
        if (belowId != BlockRegistry::Water || belowState != 0) return;     // ④ 非静水源 → 拒
        if (m_world->blockAt(tx, ty, tz) == BlockRegistry::LilyPad) return; // ⑥ 已有睡莲 → 拒（叠放）
    }
    // t134 不完整方块放置：door 占两格（下格 + 上格），需上格也为空气；其余单格。走 setBlock 5 参数版
    //   （写 id + state）。state 复用上方算出的 placeState / doorFacing（逻辑同源，无重复推导）。
    if (isDoor) {
        // t208 防半截门（可穿根因 2）：门占两格，上格越世界高度时 World::setBlock 静默返 false（caller 不查）
        //   → 只剩下单格 → 玩家跳跃（顶点 1.25 > 单格 1.0）即可跨过。显式查 ty+1 在界内且为空气才放，
        //   否则整门拒绝（两格都不放），杜绝「上格缺失的单格门」。
        if (ty + 1 >= m_world->height()) return;                       // 上格越世界顶 → 门放不下（拒整门）
        {   // t198/t351：门上格亦允许排开水/岩浆（与下格同语义，源 / 流均可被门替换）。
            const quint8 upId = m_world->blockAt(tx, ty + 1, tz);
            if (upId != BlockRegistry::Air && upId != BlockRegistry::Water
                && upId != BlockRegistry::Lava) return; // 上格非空（实体）→ 门放不下
        }
        m_world->setBlock(tx, ty, tz, idByte, doorFacing);              // 下格：bit3=0(下格) bit2=0(合) bit[1:0]=朝向
        m_world->setBlock(tx, ty + 1, tz, idByte, quint8(doorFacing | 8)); // 上格：bit3=1
        // t669 放置消耗收口：生存放置由 C++ 侧统一消耗 1 件（门两格同写只耗 1 扇；原由 QML onBlockPlaced
        //   blanket takeStack 承担，但那会误扣非放置类 setBlock（锄/踩踏）→ 收口到放置动作本体）。
        if (m_mode == Survival) m_hotbar->takeStack(m_hotbar->selectedSlot(), 1);
    } else if (isBed) {
        // t428 床 head 格须在界内且为 air/water/lava（同 door 上格预检，机制等价 MC 床需两格空位），否则整床
        //   拒绝（两格都不放，防半截床）。foot 经 setBlock 写（发 blockPlaced → 放置音 / 粒子），head 经
        //   setWaterSilent 静默写（重建 mesh + 重光照，但**不**发 blockPlaced → 1 床物体只耗 1 件、只一次放置
        //   反馈；机制等价 MC「放 1 床 = 1 物品 = 一次动作」）。door 靠 maxStack=1 碰巧避双耗，bed maxStack=64
        //   故必须用静默写第二格（setWaterSilent 是通用静默 state 写入口，非仅水流）。t669 放置消耗收口：
        //   survival 消耗由 C++ 统一处理（foot 之后 1 次，同 door / regular 分支）。
        const int hx = tx + hdx, hz = tz + hdz;
        if (hx < 0 || hx >= m_world->width() || hz < 0 || hz >= m_world->depth()) return; // head 越界 → 整床拒
        const quint8 headOcc = m_world->blockAt(hx, ty, hz);
        if (headOcc != BlockRegistry::Air && headOcc != BlockRegistry::Water
            && headOcc != BlockRegistry::Lava) return; // head 格非空 → 床放不下
        m_world->setBlock(tx, ty, tz, idByte, bedFacing);                  // foot: bit3=0 bit[1:0]=朝向
        m_world->setWaterSilent(hx, ty, hz, idByte, quint8(bedFacing | 8)); // head: bit3=1（静默，免双耗）
        if (m_mode == Survival) m_hotbar->takeStack(m_hotbar->selectedSlot(), 1); // t669 消耗收口
    } else {
        // fence / pressure_plate / trapdoor：placeState=0（trapdoor 默认水平合）。
        m_world->setBlock(tx, ty, tz, idByte, placeState);
        // t669 放置消耗收口：常规方块放置（泥土 / 木板 / 火把 / 南瓜…）的 survival 消耗由 C++ 统一处理
        //   （原 QML onBlockPlaced blanket takeStack 承担——但那会误扣锄地/踩踏等非放置类 setBlock）。
        if (m_mode == Survival) m_hotbar->takeStack(m_hotbar->selectedSlot(), 1);
    }
    // t482/t483 防御造物生成（机制等价 MC 1.0 雪傀儡 / 铁傀儡搭建）：玩家放置**南瓜**后检测下方排列，
    //   命中 → 生成对应防御造物（spawnMobTyped 计入实体槽 kCap）+ 静默移除结构方块（setWaterSilent —— 非玩家
    //   破块不发 broken → 免破块粒子/音噪音；结构方块雪/铁是玩家先放好的，南瓜刚放已发 blockPlaced → 生存已
    //   消耗 1 南瓜）。spec「南瓜 + 雪块×2 竖直 → 雪傀儡 / 铁块×4 T 形 + 南瓜 → 铁傀儡」。分层（PLAN §2）：
    //   Game/Physics 层（读 World + 写 World setWaterSilent + 调 EntityManager spawn），不改 setBlock 语义。
    //   **支持 T 形 X / Z 双向**（玩家从任一侧搭底排均能触发，机制等价 MC 铁傀儡 T 形不锁向）。
    // t509 诊断可观测性：t483 检测逻辑经穷尽静态复核**结构正确**（标准 MC 1.0 T 形：南瓜在中柱顶正上方，
    //   下方连续 2 铁块 = 中柱顶 + 底排中间，底排两端任一水平向各 1 铁块 → rowX||rowZ 双向覆盖）；雪傀儡同结构
    //   （少底排）用户报可造 → 检测链路（m_entityManager 绑定 / setWaterSilent / spawnMobTyped）全通。故 t483
    //   失效最可能根因是「玩家实际搭建与检测假设不符」（底排朝向偏 / 南瓜放偏一格 / 少一个铁块）或 stale 构建，
    //   而原检测**静默失败**（不命中无任何反馈）→ 用户「摆了不生成」无法定位。修法：加诊断 qInfo 让运行期可观测
    //   （lessons「先 run + 加诊断确认当前态，禁照旧报告打补丁」）—— ① 南瓜下方命中铁柱但底排不全 → 打底排
    //   4 邻格 id + rowX/rowZ；② 南瓜下方非铁柱/雪柱 → 打 below1/below2 id；用户跑一次 logs/voxelsandbox.log
    //   生成逻辑（rowX||rowZ → 移除 5 块 + spawn）不变。
    //   t509r 运行时根因排查：上轮诊断加完后，所有捕获的 logs/voxelsandbox.log **无任何 golem 行**——说明玩家
    //   放南瓜时 placeBlock 根本没到本段（旧外层守卫 ty>=2 静默跳过 + 命中前 return）。修法：① 入口无条件打
    //   「pumpkin placed」一行（确认 placeBlock 真到达 + 记录南瓜落点 tx/ty/tz + 玩家位）——只要玩家成功放南瓜
    //   就必有此行，缺则证明放南瓜本身被拒（overlaps / 选中非南瓜 / 模式门控）；② 去掉 ty>=2 外层静默守卫
    //   （blockAt 越界安全返 Air，ty-1/ty-2 越界当作「非雪/铁」自然 miss，不再整段跳过）；③ miss 分支打全 3×3
    //   邻域（ty-1 与 ty-2 两层）+ 玩家朝向，用户跑一次即可看清算「南瓜放偏 / 底排朝向偏 / 少一块」。
    if (idByte == BlockRegistry::Pumpkin && m_world && m_entityManager) {
        // t509r ① 无条件入口日志：南瓜放置事件本身（含落点 + 玩家水平朝向 horizontalFacing）。用户日志若缺本行
        //   → 南瓜放置被拒（placeBlock 早 return：overlapsPlayerAABB / 选中槽非南瓜 / canPlace 观察者门控）。
        qInfo("pumpkin placed at %d %d %d (facing=%d, playerY=%.2f)",
              tx, ty, tz, int(horizontalFacing() & 3), double(m_pos.y()));
        const quint8 below1 = (ty - 1 >= 0) ? m_world->blockAt(tx, ty - 1, tz) : quint8(BlockRegistry::Air);
        const quint8 below2 = (ty - 2 >= 0) ? m_world->blockAt(tx, ty - 2, tz) : quint8(BlockRegistry::Air);
        // (a) 雪傀儡：南瓜下方两格均为雪块（南瓜 + 雪块×2 竖直）→ 生成 MobSnowGolem + 移除 3 块。
        //     生成位置 feet = 南瓜下方两格（雪块底）；spawnMobTypedYaw 把 pos 设为 (x+0.5, feet + halfH, z+0.5)。
        //     t529 spec「生成时固定朝，平时随机」：生成时 yaw 朝玩家（atan2(-dx,-dz)），让玩家初次见到南瓜脸正脸
        //       （防御造物刚造出时面朝造主）；之后 aiWander 随机选向（造物自由游荡，不恒面向玩家）。
        if (below1 == BlockRegistry::Snow && below2 == BlockRegistry::Snow) {
            m_world->setWaterSilent(tx, ty, tz, BlockRegistry::Air, 0);     // 南瓜（刚放）
            m_world->setWaterSilent(tx, ty - 1, tz, BlockRegistry::Air, 0); // 雪块 1
            m_world->setWaterSilent(tx, ty - 2, tz, BlockRegistry::Air, 0); // 雪块 2
            // golem 中心 = feet 格中心 (tx+0.5, tz+0.5)；玩家相对 golem 方向 dx/dz → yaw 使模型 -Z 正对玩家。
            const float sgDx = m_pos.x() - (float(tx) + 0.5f);
            const float sgDz = m_pos.z() - (float(tz) + 0.5f);
            m_entityManager->spawnMobTypedYaw(tx, ty - 2, tz, EntityManager::MobSnowGolem,
                                              QStringLiteral("#f0f4f8"), 4, std::atan2(-sgDx, -sgDz));
            qInfo("snow golem built at %d %d %d", tx, ty, tz);
        }
        // (b) 铁傀儡：南瓜下方两层中柱为铁块（stem 铁柱），且其中**一层**为 3 块横梁 T（crossbar：中柱 + 水平
        //     两端各 1 铁块，X 向或 Z 向双向）→ 生成 MobIronGolem + 移除 5 块（南瓜 + 4 铁块）。
        //
        // t509 二轮复盘：**接受两种 T 形朝向**。日志实锤玩家实际摆法是「crossbar 在上(stem 下)、stem 立柱在下」，
        //   而原 probe 只认「stem 在上 / crossbar 在下」单种 → 玩家合法摆法永不匹配、铁傀儡不生成。根因不是逻辑
        //   错而是「检测假设锁死单一朝向」。MC 标准约定 crossbar 在地面、stem 在上、南瓜顶；但用户验收要求其摆法
        //   （stem 下 / crossbar 上）也能生成，故放宽为**两种朝向任一命中即生成**（南瓜正下方两格中柱都为铁块是
        //   共同前提，区别只是「哪一层是 3 块横梁、哪一层是单 stem 块」）。两朝向消耗结构相同（南瓜 + 2 中柱铁块
        //   + 2 横梁端铁块 = 5 块），生成位置同取最底层（ty-2）为脚格（spawnMobTyped 内置 pos=脚+halfH）。
        //   - 朝向 A（stem 上 / crossbar 下，原 MC 标准）：crossbar 在 y-2（中柱 + 两端），stem 在 y-1（仅中柱）。
        //   - 朝向 B（stem 下 / crossbar 上，玩家实际）：crossbar 在 y-1（中柱 + 两端），stem 在 y-2（仅中柱）。
        else if (below1 == BlockRegistry::IronBlock && below2 == BlockRegistry::IronBlock) {
            // crossbarAt(y): 该层是否为 3 块横梁 T（中柱已知铁块 + 水平任一向两端各 1 铁块）。返回方向 outX
            //   （true=X 向横梁 / false=Z 向横梁）；若两端都不全则返回 false 且 *hit=false。
            auto crossbarAt = [&](int y, bool *hit, bool *outX) {
                const bool rx = (m_world->blockAt(tx - 1, y, tz) == BlockRegistry::IronBlock
                                 && m_world->blockAt(tx + 1, y, tz) == BlockRegistry::IronBlock);
                const bool rz = (m_world->blockAt(tx, y, tz - 1) == BlockRegistry::IronBlock
                                 && m_world->blockAt(tx, y, tz + 1) == BlockRegistry::IronBlock);
                *hit = (rx || rz);
                *outX = rx;
            };
            bool crossTopHit = false, crossTopX = false;     // crossbar 在 y-1（朝向 B）
            crossbarAt(ty - 1, &crossTopHit, &crossTopX);
            bool crossBotHit = false, crossBotX = false;     // crossbar 在 y-2（朝向 A，原行为）
            crossbarAt(ty - 2, &crossBotHit, &crossBotX);
            // t509 诊断：记录两层各自的横梁命中态 + 4 邻格 id，定位「横梁端缺一块 / 摆成单列非 T」。
            qInfo("iron golem probe at %d %d %d: cross@-1=%d(X=%d) cross@-2=%d(X=%d) |"
                  " @-1 -X=%d +X=%d -Z=%d +Z=%d | @-2 -X=%d +X=%d -Z=%d +Z=%d",
                  tx, ty, tz, int(crossTopHit), int(crossTopX), int(crossBotHit), int(crossBotX),
                  int(m_world->blockAt(tx - 1, ty - 1, tz)), int(m_world->blockAt(tx + 1, ty - 1, tz)),
                  int(m_world->blockAt(tx, ty - 1, tz - 1)), int(m_world->blockAt(tx, ty - 1, tz + 1)),
                  int(m_world->blockAt(tx - 1, ty - 2, tz)), int(m_world->blockAt(tx + 1, ty - 2, tz)),
                  int(m_world->blockAt(tx, ty - 2, tz - 1)), int(m_world->blockAt(tx, ty - 2, tz + 1)));
            if (crossBotHit || crossTopHit) {
                // 选定命中朝向：优先朝向 A（crossbar 在 y-2，原 MC 标准），否则朝向 B（crossbar 在 y-1）。
                const bool orientB = !crossBotHit;            // true=朝向 B（crossbar 在 y-1）
                const int crossY = orientB ? (ty - 1) : (ty - 2);
                const bool rowX = orientB ? crossTopX : crossBotX;
                m_world->setWaterSilent(tx, ty, tz, BlockRegistry::Air, 0);        // 南瓜
                m_world->setWaterSilent(tx, ty - 1, tz, BlockRegistry::Air, 0);    // 中柱铁块（stem 顶 / crossbar 顶）
                m_world->setWaterSilent(tx, ty - 2, tz, BlockRegistry::Air, 0);    // 中柱铁块（stem 底 / crossbar 底）
                // crossbar 横梁两端（仅 crossY 层；另一层是单 stem 中柱，两端为空气，移除中柱已含）。
                //   code review B1（2026-08-13）：玩家若摆非标准「双横梁」（两层都是 3 块 = 7 铁块），朝向 A 优先
                //   只清 crossY=ty-2 层两端，ty-1 层两端 2 块残留。**符合 MC 行为**（MC 铁傀儡严格只消耗标准 5 块：
                //   南瓜 + 2 中柱 stem + 1 层 crossbar 两端）；玩家多放的 2 块是溢出，MC 也不清。故保留现状。
                if (rowX) {
                    m_world->setWaterSilent(tx - 1, crossY, tz, BlockRegistry::Air, 0);
                    m_world->setWaterSilent(tx + 1, crossY, tz, BlockRegistry::Air, 0);
                } else {
                    m_world->setWaterSilent(tx, crossY, tz - 1, BlockRegistry::Air, 0);
                    m_world->setWaterSilent(tx, crossY, tz + 1, BlockRegistry::Air, 0);
                }
                // 生成铁傀儡：脚格取结构最底层（ty-2），spawnMobTypedYaw 内置 pos=(x+0.5, 脚+halfH, z+0.5)。
                //   t529 spec「生成时固定朝，平时随机」：生成时 yaw 朝玩家（atan2(-dx,-dz)），让玩家初次见南瓜脸正脸
                //   （同雪傀儡路径）；之后 aiWander / aiIronGolem 随机 / 朝敌对定向（造物自由行为）。
                const float igDx = m_pos.x() - (float(tx) + 0.5f);
                const float igDz = m_pos.z() - (float(tz) + 0.5f);
                m_entityManager->spawnMobTypedYaw(tx, ty - 2, tz, EntityManager::MobIronGolem,
                                                  QStringLiteral("#c8c8d0"), 100, std::atan2(-igDx, -igDz));
                qInfo("iron golem built at %d %d %d (orient=%s, crossY=%d, rowX=%d)",
                      tx, ty, tz, orientB ? "B(cross@-1)" : "A(cross@-2)", crossY, int(rowX));
            }
        }
        // t509r ③ miss 全邻域诊断：南瓜放好但下方非雪柱 / 非铁柱 → 打下方两层 + 底排 4 邻 id + 玩家朝向，
        //   精确定位「南瓜放偏一格（below1 非 iron = 瞄了 stem 侧面落旁格）/ 底排朝向偏 / 少一块铁」。
        //   （仅当两柱都没命中才打，避免与上面命中分支重复。）
        else {
            qInfo("golem build miss at %d %d %d: below1=%d below2=%d | row@-2: -X=%d +X=%d -Z=%d +Z=%d | facing=%d"
                  " (need IronBlock/IronBlock column + 2-wide row, OR Snow/Snow)",
                  tx, ty, tz, int(below1), int(below2),
                  int(m_world->blockAt(tx - 1, ty - 2, tz)),
                  int(m_world->blockAt(tx + 1, ty - 2, tz)),
                  int(m_world->blockAt(tx, ty - 2, tz - 1)),
                  int(m_world->blockAt(tx, ty - 2, tz + 1)),
                  int(horizontalFacing() & 3));
        }
    }
    m_lastPlaceMs = now; // 放置成功 → 刷新 CD 计时（t128；now 为入口时间戳，同帧无意义漂移）
    // t125 火把朝向：把玩家点击面外法线随放置事件传出，供呈现层按玩家意图定向（柄嵌所点墙面，
    //   非旧固定优先级误判）。法线为射线命中面外法线（指向玩家侧），值在 placeBlock 入口已由 updateRaycast
    //   确定、此处不变；按值传出无后效依赖（即便下一帧 raycast 改向也不影响本火把）。
    if (m_selectedBlock == BlockRegistry::Torch)
        emit torchPlaced(tx, ty, tz, m_hitNx, m_hitNy, m_hitNz);
    emit swingArm(); // 放块成功 → 第一人称手挥动（t29）
    emit blockPlaced(); // progress 统计：放置方块 +1
}

// Q 键丢弃（t36）：从选中槽 takeStack 1 件 → 生成掉落实体（count=1）。仅指针捕获时生效（spec：「Q 键
//   （captured 时）」）。取失败（空栈 / 无 hotbar）→ 不丢。
// t56：选中槽为空时直接早退（id==0）—— 若用户从背包拾取到光标后关包，光标手持栈（heldBlock）
//   须经 Main.qml::returnHeldToHotbar 在关包时归还进 hotbar（优先选中槽），否则 Q 读空槽不丢。
// t64：Q 键每次只丢 1 件（dropHeld 的语义不变；整栈丢弃走 dropHeldCursor）。
// t609 丢弃方向修正：生成位 = 眼位 + 视线 × kDropForwardOffset（0.3 格，略出身体表面），初速 = 视线 ×
//   kDropThrowSpeed（6 格/s，含俯仰分量：仰视上抛 / 俯视下压），无随机左右散布——走 spawnItemThrown（C++
//   直调同 dispenseFromDispenser 的 spawnItemAt 模式）。旧版「眼位 + 视线 × 1.5 floor 到格中心 + spawnItem
//   哈希随机全圆弹出」→ 用户报「直接从鼠标所指向的地方喷出来而且还是左右喷的」（1.5 格已近准星命中点 + 随机
//   全圆方向 → 左右乱飞）。机制等价 MC 玩家把物品从身体沿视线方向扔出。m_itemEntities 未注入 → 回退旧
//   spawnItem 信号路径（QML 转发，兼容防御）。
void PlayerController::dropHeld()
{
    if (m_dead) return;              // t655 死亡态输入闸门：尸体不丢物（背包已被 dropAllItems 清空，防御）
    if (!m_captured) return;        // spec：仅捕获时
    if (!m_hotbar) return;
    const int id = m_hotbar->selectedItemId();
    if (id == 0) return;            // 空手 → 不丢
    const QVariantList ench = m_hotbar->enchantsAt(m_hotbar->selectedSlot()); // t590 附魔随实体走（先读再 takeStack 清槽）
    const QString name = m_hotbar->customNameAt(m_hotbar->selectedSlot());    // t622 实例名随实体走（同先读再清）
    const int dur = m_hotbar->durabilityAt(m_hotbar->selectedSlot());         // t647 实例耐久随实体走（磨损工具 Q 丢再捡不复原）
    const int took = m_hotbar->takeStack(m_hotbar->selectedSlot(), 1);
    if (took <= 0) return;          // 取失败（空栈）→ 不丢
    throwItemInLook(id, 1, ench, name, dur);   // t609 眼位沿视线丢出
}

// t609 主动丢弃统一原语：从眼位 + 视线 × kDropForwardOffset 生成掉落实体，初速 = 视线 × kDropThrowSpeed
//   （含俯仰，无随机散布）。dropHeld / dropHeldStack / dropItemAtFront / dropHeldCursor / dropHeldCursorOne
//   五个主动丢弃路径共用（死亡掉落 dropAllItems 不走此——死亡散布保留 MC「喷一地」口径）。m_itemEntities
//   未注入（异常配置）→ 回退旧 spawnItem 信号路径（QML 转发到 itemEntities.spawnItem，格中心 + 随机弹出）。
//   t622 name：自定义名随实体走（改名物品丢弃保真；拾取回填见 pickupScan）。
//   t647 durability：实例耐久随实体走（磨损工具丢弃保真；-1 = 未初始化 → 拾取端归一满耐久）。
void PlayerController::throwItemInLook(int itemId, int count, const QVariantList &enchants, const QString &name, int durability)
{
    const QVector3D fwd = lookDirection();
    if (m_itemEntities) {
        const QVector3D p = position() + fwd * kDropForwardOffset;
        m_itemEntities->spawnItemThrown(p, itemId, count, fwd.x(), fwd.y(), fwd.z(), kDropThrowSpeed, enchants, name, durability);
        return;
    }
    // 回退：旧信号路径（眼位 + 视线 × 1.5 floor 到整数格；ItemEntityManager 存格中心 = 整数+0.5）。
    const QVector3D p = position() + fwd * 1.5f;
    emit spawnItem(int(std::floor(p.x())), int(std::floor(p.y())), int(std::floor(p.z())), itemId, count, enchants, name, durability);
}

// t229 Ctrl+Q 第一人称丢弃整栈（spec「第一人称 Ctrl+Q=丢整栈（手持槽）」）：与 dropHeld（Q=丢 1 件）
//   同源（取**选中槽**），差异在 takeStack 传「整栈数量」而非 1 —— 1 实体携带整栈数量（同 dropHeldCursor
//   模式，避免「丢 4 件生 4 实体」爆量）。仅指针捕获时生效（同 dropHeld）。空栈 / 取失败 → 不丢。
//   t609：丢弃位置 / 初速同 dropHeld（眼位沿视线丢出，throwItemInLook 统一原语）。
void PlayerController::dropHeldStack()
{
    if (m_dead) return;              // t655 死亡态输入闸门：尸体不丢物（同 dropHeld）
    if (!m_captured) return;        // spec：仅捕获时（同 dropHeld）
    if (!m_hotbar) return;
    const int slot = m_hotbar->selectedSlot();
    const int id = m_hotbar->blockIdAt(slot);
    if (id == 0) return;            // 空手 → 不丢
    const QVariantList ench = m_hotbar->enchantsAt(slot); // t590 附魔随实体走（先读再 takeStack 清槽）
    const QString name = m_hotbar->customNameAt(slot);    // t622 实例名随实体走（同先读再清）
    const int dur = m_hotbar->durabilityAt(slot);         // t647 实例耐久随实体走（磨损工具整栈丢再捡不复原）
    const int cnt = m_hotbar->countAt(slot);
    if (cnt <= 0) return;
    const int took = m_hotbar->takeStack(slot, cnt); // 取整栈（takeStack 返回实际取走数）
    if (took <= 0) return;
    throwItemInLook(id, took, ench, name, dur); // t609 眼位沿视线丢出（1 实体携整栈）
}

// t229 背包悬停槽丢弃原语（spec「背包内悬停槽 Q=丢 1 / Ctrl+Q=丢整栈。适用所有背包面板」）：按给定
//   (itemId, count) 生成掉落实体。**不读/改任何槽** —— 槽的读改由 UI 层（InventoryOps
//   readSlot/writeSlot，按 hoveredSlotKey 的组分发）完成，本方法只做实体生成 + 位置（Game/Physics 层语义，
//   PLAN §2 分层：物理位置/实体事件在 Game 层，槽操作在 VM/UI 层）。id==0 / count<=0 → 不丢。
//   不限捕获态（背包打开时未捕获正是此场景，同 dropHeldCursor）。
//   t590 enchants：UI 层把 hovered 槽的物品附魔传入 → 实体携带（拾取回填 + 掉落紫光晕）。
//   t622 name：UI 层把 hovered 槽的物品实例名传入 → 实体携带（拾取回填，防改名物品丢名）。
//   t650 durability：实例耐久（缺省 -1 = 新实例满耐久）→ 实体携带（同 dropHeld* t647 批；防磨损工具
//   关包满包余量丢弃再捡回满耐久）。t609：位置 / 初速同 dropHeld（眼位沿视线丢出，throwItemInLook 统一原语）。
void PlayerController::dropItemAtFront(int itemId, int count, const QVariantList &enchants, const QString &name, int durability)
{
    if (itemId == 0 || count <= 0) return; // 空手 / 非正数 → 不丢
    throwItemInLook(itemId, count, enchants, name, durability); // t609 眼位沿视线丢出；t650 耐久随实体
}

// 拖出背包丢弃（t49 / t64）：光标手持栈整栈丢弃为**单个实体携带整栈数量**。不限捕获态
// （背包打开时正是未捕获）。t64 修复：原 emit 仅传 id（count 走默认 1）→ 4 木棒丢出只生 1 实体 count=1，
// 捡回只剩 1（用户：「4 木棒丢出去捡起来只剩 1 个」）。现传 heldCount → 1 实体携带整栈 → 捡回原数。
// 清空 hotbar 光标手持栈（setHeldBlock(0) 同步清 count），再生成实体。空手 / 无 hotbar → 不丢。
// t590：光标手持附魔随实体走（heldEnchants 读先于 setHeldBlock(0) 清栈）→ 拾取回填 + 掉落紫光晕。
// t609：位置 / 初速同 dropHeld（眼位沿视线丢出，throwItemInLook 统一原语）。
void PlayerController::dropHeldCursor()
{
    if (!m_hotbar) return;
    const int id = m_hotbar->heldBlock();
    const int cnt = m_hotbar->heldCount();
    if (id == 0 || cnt <= 0) return; // 空手 → 不丢
    const QVariantList ench = m_hotbar->heldEnchants(); // t590 附魔随实例走（先读再清栈）
    const QString name = m_hotbar->heldCustomName();    // t622 实例名随实例走（先读再清栈）
    const int dur = m_hotbar->heldDurability();         // t647 实例耐久随实例走（先读再清栈）
    m_hotbar->setHeldBlock(0);       // 清空光标手持栈（id=0 同步清 count）
    throwItemInLook(id, cnt, ench, name, dur);  // t609 眼位沿视线丢出
}

// t228 右键拖出背包丢弃 1 件（spec「右键=逐个」）：光标手持栈取 1 件 → 生成掉落实体(count=1)，余数留光标。
//   与 dropHeldCursor 的差异：后者清空整栈；本方法只 -1 count（count 归 0 时连 id 一起清，保空栈不变式）。
//   空手 / count<=0 → 不丢。t609：位置 / 初速同 dropHeldCursor（眼位沿视线丢出，throwItemInLook 统一原语）。
void PlayerController::dropHeldCursorOne()
{
    if (!m_hotbar) return;
    const int id = m_hotbar->heldBlock();
    const int cnt = m_hotbar->heldCount();
    if (id == 0 || cnt <= 0) return;        // 空手 → 不丢
    // 取 1 件：余数 >0 则 count-1（id 不变）；归 0 则 setHeldBlock(0) 连 id 一起清（保空栈不变式）。
    const QVariantList ench = m_hotbar->heldEnchants(); // t590 先读附魔再清栈（setHeldBlock(0) 会清附魔）
    const QString name = m_hotbar->heldCustomName();    // t622 先读实例名再清栈（setHeldBlock(0) 会清名）
    const int dur = m_hotbar->heldDurability();         // t647 先读实例耐久再清栈（setHeldBlock(0) 会重置耐久）
    if (cnt <= 1) m_hotbar->setHeldBlock(0);
    else          m_hotbar->setHeldCount(cnt - 1);
    // t590 附魔随实体走（余数留光标的附魔不变，实体带走 1 件的附魔）。t609 眼位沿视线丢出。
    throwItemInLook(id, 1, ench, name, dur);
}

// t175 死亡掉落：玩家死亡时把整个背包（hotbar 9 + main 27 + 光标手持栈）全部掉落为物品实体（死亡点
//   = 脚底 m_pos），随后清空背包。每非空栈 → 1 实体携带整栈数量（同 dropHeldCursor 模式）；空栈跳过。
//   ItemEntityManager 无水平速度物理，靠散布到死亡格 3×3 邻域做视觉分离（轮回 9 格 pattern：>9 栈后
//   回中心格可接受重叠）。光标手持栈一并掉落（onDied 已先 returnHeldToHotbar 归还背包合并，此处为
//   防御双保险，held 通常已空）。最后 resetForMode(Survival) 清空 hotbar+main+held + bump revision →
//   QML 同步（slotsChanged/mainSlotsChanged/heldBlockChanged）。死亡只在 Survival 发生（fallDamage /
//   suffocation 均 Survival 路径），故 resetForMode(int(Survival)) 清空语义正确。
//   顺序关键：先 emit spawnItem（创建实体 = 物品移出背包），后 resetForMode 清空（物品不再在背包）→
//   无重复（实体一份 + 背包空）。spawnItem 同步走 QML DirectConnection（同线程）→ 实体即刻生成。
void PlayerController::dropAllItems()
{
    if (!m_hotbar) return;
    m_dead = true; // 标记死亡态 → 抑制 pickupScan（玩家尸体停死亡点，免掉落物被自动捡回空背包）
    // 死亡点 = 玩家脚底整数格（玩家倒下处）；3×3 邻域散布避免全堆同一格中心。
    const int cx = int(std::floor(m_pos.x()));
    const int cy = int(std::floor(m_pos.y()));
    const int cz = int(std::floor(m_pos.z()));
    // 9 格轮回散布 pattern（中心 + 8 邻）。idx 单调递增，>9 栈后回中心格（接受少量重叠）。
    static constexpr int kScatter[9][2] = {
        {0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    int idx = 0;
    // t590：附魔随死亡掉落实体走（工具 / 护甲丢出再捡保附魔；拾取回填见 pickupScan）。
    //   t622：实例名同走（改名物品死亡掉落再捡不丢名）。
    //   t686：实例耐久同走（磨损工具死亡掉落再捡不再「免费回满」——旧版漏传第 8 参 → 实体
    //   durability=-1 → 拾取 normalizeDurability 按缺省归满耐久；破箱掉落路径已带耐久，本处对齐）。
    auto dropStack = [&](int id, int count, const QVariantList &ench, const QString &name, int dur) {
        if (id == 0 || count <= 0) return;          // 空栈跳过
        emit spawnItem(cx + kScatter[idx % 9][0], cy, cz + kScatter[idx % 9][1], id, count, ench, name, dur);
        ++idx;
    };
    // hotbar 9 槽 → main 27 槽 → 光标手持栈，逐栈掉落。
    for (int i = 0; i < m_hotbar->slotCount(); ++i)
        dropStack(m_hotbar->blockIdAt(i), m_hotbar->countAt(i), m_hotbar->enchantsAt(i), m_hotbar->customNameAt(i), m_hotbar->durabilityAt(i));
    for (int i = 0; i < m_hotbar->mainCount(); ++i)
        dropStack(m_hotbar->mainBlockIdAt(i), m_hotbar->mainCountAt(i), m_hotbar->mainEnchantsAt(i), m_hotbar->mainCustomNameAt(i), m_hotbar->mainDurabilityAt(i));
    dropStack(m_hotbar->heldBlock(), m_hotbar->heldCount(), m_hotbar->heldEnchants(), m_hotbar->heldCustomName(), m_hotbar->heldDurability()); // 光标手持栈（onDied 已归还，通常空）
    // 清空整个背包（hotbar + main + held）+ bump revision → QML 同步。仅 Survival 调（死亡仅在 Survival）。
    m_hotbar->resetForMode(int(Survival));
}

// 中键拾取方块（t37 pick block）：取当前射线命中格的方块 id → 写入 hotbar 当前选中槽（覆盖；
// 仅指针捕获时生效（与破/放同窗口级事件过滤路径；spec）。命中空气 / 无命中 / 无世界 / 无 hotbar → 不动作。
// t288：pick-block 仅 Creative——创造模式有「凭空中键复制方块」的能力（源无限 → 满栈）；生存有限背包
// 无此能力（按中键不动作），Spectator 亦禁（与 canBreak/canPlace 同「观察者不交互」语义）。pick 虽不改
// 栅格，但属「凭空获得方块」的选择作弊，故与破/放同走模式门控，不放宽到全模式。
// t291：优先「切槽」——hotbar 已有同方块时切到该槽（不动栈），仅 hotbar 全无该方块时才 setStack 复制入
// 当前选中槽（Hotbar 内部校验范围 + id 合法性 + count 上限）。
// t653②：中键指向 mob 优先于方块 —— 独立跑 mob 命中射线（findMobHit，同攻击路径模式），命中活体 mob
//   且比方块更近（或无方块命中）→ 取该 mob 的**生物蛋**（mobType→egg id 映射）走同款「切槽/复制」语义
//   （机制等价 MC 创造中键 pick mob → spawn egg）。无蛋的 mob（狼/傀儡/蠹虫未造蛋物品）→ 落回方块分支。
void PlayerController::pickBlock()
{
    if (m_dead) return; // t655 死亡态输入闸门：尸体不中键复制（防御；死亡仅 Survival，创造路径理论不达）
    if (m_mode != Creative) return; // t288：仅创造模式可中键复制方块
    if (!m_captured || !m_world || !m_hotbar) return;
    // t653② mob 优先：独立 mob 命中射线（不依赖 m_hasHit —— 瞄悬空 mob 无方块命中也应可 pick）。
    //   命中比方块更近（或无方块命中）才走蛋路径；否则 mob 被方块遮挡 → 落回方块分支。
    if (m_entityManager) {
        const QVector3D eye = position();
        const QVector3D look = lookDirection();
        float mobDist = 0.0f;
        const int mobIdx = m_entityManager->findMobHit(eye, look, kReach, &mobDist);
        if (mobIdx >= 0 && (!m_hasHit || mobDist <= m_hitDist)) {
            const int eggId = mobTypeEggId(m_entityManager->mobTypeAt(mobIdx));
            if (eggId != 0) {
                pickIdToHotbar(eggId);
                return;
            }
            // 无蛋 mob（狼/雪傀儡/铁傀儡/蠹虫——蛋物品未造）→ 落回方块分支（中键仍 pick 背后方块）。
        }
    }
    if (!m_hasHit) return;
    const quint8 id = m_world->blockAt(m_hitBx, m_hitBy, m_hitBz);
    if (id == BlockRegistry::Air) return; // 命中空气 → 无可拾取
    pickIdToHotbar(int(id));
}

// t653② 抽出「切槽 / 复制入槽」主体（方块与生物蛋共用；t291/t453 语义原样迁移）：
//   hotbar 已有同 id → 切槽（不动栈）；全无 → 复制满栈入空槽优先（当前选中槽空则直写；满背包回退替换）。
void PlayerController::pickIdToHotbar(int id)
{
    const int n = m_hotbar->slotCount();
    for (int s = 0; s < n; ++s) {
        if (m_hotbar->blockIdAt(s) == id) { // blockIdAt 返原始 id；空槽=Air=0，id≠Air 不误匹配
            m_hotbar->setSelectedSlot(s);
            return;
        }
    }
    // t453 复制 → 空槽优先（修「手持有方块中键另一块丢失原手持」）：hotbar 全无该 id 时，复制入**空槽**
    //   而非替换当前选中槽。当前选中槽为空 → 直接写入它（无内容损失、无需切槽）；当前选中槽非空（手持有
    //   方块）→ 另找空槽复制 + 切到该槽（手持 = 新方块，原选中槽内容保留，机制等价 MC 1.0 pick-block
    //   「取目标方块到手」不破坏既有栈）；仅满背包（无空槽）才回退替换当前选中槽（不可避免）。创造源无限
    //   故写满栈（maxStackSize），与旧「覆盖选中槽」的 setStack 数量语义一致。
    int target = m_hotbar->selectedSlot();
    if (m_hotbar->blockIdAt(target) != int(BlockRegistry::Air)) {
        // 当前选中槽非空 → 找空槽保护原手持内容
        target = -1;
        for (int s = 0; s < n; ++s) {
            if (m_hotbar->blockIdAt(s) == int(BlockRegistry::Air)) { target = s; break; }
        }
        if (target < 0) target = m_hotbar->selectedSlot(); // 满背包 → 回退替换当前
    }
    m_hotbar->setStack(target, id, m_hotbar->maxStackSize(id));
    m_hotbar->setSelectedSlot(target); // 切到复制入的槽（手持 = 新方块；同值时 setSelectedSlot 内部早退）
}

// t653② mobType → 生物蛋物品 id 映射（机制等价 MC 创造中键 pick mob → 对应 spawn egg；零 MC 专名 §9）。
//   仅 9 种有蛋物品的 mob 有映射（t243/t287/t285/t398/t399）；狼/雪傀儡/铁傀儡/蠹虫无蛋物品 → 0（caller
//   落回方块分支）。蛋 id 与 RecipeRegistry 命名常量同源（此处字面量与 playercontroller 生物蛋右键生成
//   分支的 id 判定同表）。
int PlayerController::mobTypeEggId(int mobType) const
{
    switch (mobType) {
    case EntityManager::MobPig:      return RecipeRegistry::SpawnEggPigId;
    case EntityManager::MobCow:      return RecipeRegistry::SpawnEggCowId;
    case EntityManager::MobSheep:    return RecipeRegistry::SpawnEggSheepId;
    case EntityManager::MobShambler: return RecipeRegistry::SpawnEggShamblerId;
    case EntityManager::MobBones:    return RecipeRegistry::SpawnEggBonesId;
    case EntityManager::MobStalker:  return RecipeRegistry::SpawnEggStalkerId;
    case EntityManager::MobSpider:   return RecipeRegistry::SpawnEggSpiderId;
    case EntityManager::MobChicken:  return RecipeRegistry::SpawnEggChickenId;
    case EntityManager::MobSquid:    return RecipeRegistry::SpawnEggSquidId;
    default: return 0; // 无蛋物品的 mob（Test/Wolf/Golem/Silverfish/Tnt 哨兵）
    }
}

// 拾取扫描（t36 / t64 / t97）：每帧扫附近掉落实体 → Hotbar::addToAny（跨 main + hotbar 智能堆叠，
// 先合并 main 同 id → 再 hotbar 同 id → 再空槽 main→hotbar）。
// t97：原走 addStack（只看 hotbar 9 槽），主栏物品拾取进不去主栏、丢弃回栏不合并 → 改 addToAny 让拾取
// 能进主栏（修「主栏不同步」根因）。
// t64：拾取按 entity.count 全数尝试入背包；addToAny 返回未放入数（leftover）：
//   - leftover == 0：全入 → removeAt 销毁实体（spec：拾取后销毁）。
//   - 0 < leftover < entity.count：部分入 → setCountAt(i, leftover) 把余数回写、保留 entity（玩家背包
//     只剩空槽位不足 maxStack 时常见；spec「超 maxStack 分裂」由 addToAny 自然分到多槽，entity 留余）。
//   - leftover == entity.count：背包完全装不下（同 id 槽全满 + 无空槽）→ 不动 entity（spec：全满→不拾取）。
// 距离从玩家 AABB 中心（脚底 + 半高）3D 起算，阈值 kPickupDist；从后往前扫 → erase 不影响前面索引。
// t51：AABB 高用 m_height（蹲下变矮 → 中心随之降低，仍贴近玩家实际占据空间）。
// t53：跳过新生免拾取期内的实体（isPickupReady=false）——破块瞬间实体常在玩家近旁（脚下方块 ~1.4 格
//   < kPickupDist 1.5），若无延迟则下一帧即被收走、玩家永远看不见（疑似 auto-collect 根因）。让实体
//   先可见 0.5s 再开放拾取（机制等价 MC block-break pickup delay）。
void PlayerController::pickupScan()
{
    if (!m_itemEntities || !m_hotbar) return;
    if (m_dead) return; // t175：死亡态不拾取（玩家尸体停死亡点，否则掉落物 0.5s 免拾窗后被自动捡回空背包）
    // t290 观察者交互门控：观察者不应放/破/捡任何东西（与 canBreak/canPlace 同「观察者不交互栅格/物品」语义；
    // spec「观察者能捡物品 = 错」）。门控在物理拾取入口（而非各 addStack 调用点）—— 单点守卫，覆盖率最高。
    // Creative/Survival 正常拾取（创造背包可持有物品；生存拾取是核心玩法）。
    if (m_mode == Spectator) return;
    const QVector3D center = m_pos + QVector3D(0.0f, m_height * 0.5f, 0.0f);
    const float r2 = kPickupDist * kPickupDist;
    for (int i = m_itemEntities->count() - 1; i >= 0; --i) {
        if (!m_itemEntities->aliveAt(i)) continue;       // t256：跳过已释放的空槽（slot-reuse；count 含空槽）
        if (!m_itemEntities->isPickupReady(i)) continue; // t53：新生 0.5s 免拾取（让实体先可见再可拾）
        const QVector3D d = m_itemEntities->posAt(i) - center;
        if (d.lengthSquared() > r2) continue;          // 超阈值 → 跳过
        const int id = m_itemEntities->itemIdAt(i);
        const int have = m_itemEntities->countAt(i);    // t64：实体携带数量（整栈丢弃场景）
        if (have <= 0) { m_itemEntities->removeAt(i); continue; } // 防御：count 已为 0 → 销毁
        const int leftover = m_hotbar->addToAny(id, have, m_itemEntities->durabilityAt(i), m_itemEntities->enchantsAt(i), m_itemEntities->nameAt(i)); // t97：跨 main + hotbar 智能堆叠；按 maxStack 分流。t590：实体附魔随拾取回填（防「附魔工具丢出再捡变普通」）。t622：实例名同回填（防「改名物品丢出再捡丢名」）。review D2-c：实例耐久同回填（-1 = 未初始化 → 归一满耐久；>0 = 保真，破箱掉落的磨损工具捡回不复原）
        if (leftover <= 0) {
            m_itemEntities->removeAt(i);                // 全入 → 销毁实体
            // t118：拾取语义事件（驱动 AudioManager.playPickup 拾取音；t120 亦据此驱动手弹跳动画）。
            emit itemPickedUp(id, have);
        } else if (leftover < have) {
            m_itemEntities->setCountAt(i, leftover);    // 部分入 → 余数回写、entity 保留
            // t118：部分拾取也算拾取事件（按实际入栈数计；spec「拾取」语义覆盖「全 / 部分」两路）。
            emit itemPickedUp(id, have - leftover);
        } // else leftover == have：背包完全装不下 → entity 不动（spec：全满→不拾取；不发事件）
    }
}

// t323 嵌入箭近距拾取（spec「玩家箭嵌入方块后走近自动拾 +1 箭」）：扫 EntityManager 中「玩家射出且已嵌入
//   方块」的箭，玩家 AABB 中心 3D 距 ≤ kPickupDist → addToAny(ArrowId,1) 全入则销毁嵌入箭 + emit itemPickedUp
//   （拾取音 / 手弹跳，复用掉落物拾取语义事件）。骷髅箭（arrowFromPlayer=false）不拾（防刷箭，spec「SKELETON
//   箭不可拾取」）；飞行中箭（未嵌入）不拾（免误拾）。门控同 pickupScan（无 entityManager/hotbar / 死亡 / 观察者 → 早退）。
// t604 拾取延迟：箭自 spawn 起 kArrowPickupDelayMs 内**不可拾**（机制等价 MC 1.0 箭拾取延迟）。根因：贴脸 /
//   近距射墙时箭嵌入点就在拾取半径（1.5 格）内 → 下一帧 scan 即 +1 拾回 → 「射出扣 1、瞬间又回 1」，用户观感
//   即「不是射出就消耗」（实际扣减在射出瞬间 endBowDraw，被秒拾回吞掉视觉反馈）。加延迟后近距射出的箭在墙上
//   停 ~1s 才可走近拾回，消耗语义可感知；远距射出本就飞出拾取半径，不受影响。
void PlayerController::arrowPickupScan()
{
    if (!m_entityManager || !m_hotbar) return;
    if (m_dead) return;            // 同 pickupScan：死亡态不拾取
    if (m_mode == Spectator) return; // t290 观察者不交互（不拾取，同 pickupScan 门控）
    const QVector3D center = m_pos + QVector3D(0.0f, m_height * 0.5f, 0.0f);
    const float r2 = kPickupDist * kPickupDist;
    for (int i = m_entityManager->count() - 1; i >= 0; --i) {
        if (!m_entityManager->aliveAt(i)) continue;                    // 跳过空槽（slot-reuse）
        if (m_entityManager->kindAt(i) != int(EntityManager::Arrow)) continue; // 仅箭
        if (!m_entityManager->isArrowStuckAt(i)) continue;             // 仅嵌入箭（飞行中不拾）
        if (!m_entityManager->arrowFromPlayerAt(i)) continue;          // 仅玩家箭（骷髅箭防刷不拾）
        if (m_entityManager->arrowAgeMsAt(i) < kArrowPickupDelayMs) continue; // t604 拾取延迟（刚射出的不秒拾回）
        const QVector3D d = m_entityManager->posAt(i) - center;
        if (d.lengthSquared() > r2) continue;                          // 超阈值 → 跳过
        const int leftover = m_hotbar->addToAny(int(RecipeRegistry::ArrowId), 1);
        if (leftover <= 0) { // 全入 → 销毁嵌入箭（满则留，spec「满→不拾」）
            m_entityManager->removeEntityAt(i);
            emit itemPickedUp(int(RecipeRegistry::ArrowId), 1); // 拾取音 / 手弹跳（同掉落物拾取）
        }
    }
}

// t485/t490 TNT 陷阱触发（见 playercontroller.h 头注释）。**t627 边沿化**：不再扫玩家 footprint——改读
//   updatePressurePlates 算出的本 tick 踩下沿集（m_plateJustPressed，含玩家/mob/掉落物三类触发源）：沿上
//   的压力板，其 **6 邻**（4 水平 + 上 + 下）有 TntBlock → 点燃（移除 TNT 方块 + spawnPrimedTnt，引燃态实体
//   延时引爆触发连锁）。机制等价 MC 1.0 压力板经红石点燃邻接 TNT（本项目无红石，故「踩板即点燃邻 TNT」
//   直接绑在踩下沿上）。踩一次只点燃一次（持续踩着无新沿 → 不再点燃）；离开再踩 → 新沿 → 再点燃。
//   点燃 TNT 块转 PrimedTnt（非直接引爆）→ fuse 倒计 ~5s → 引爆 → detonateTntSphere 内链式引燃邻接 TNT
//   → 连锁全爆。
void PlayerController::scanTntTraps()
{
    if (!m_entityManager || !m_world) return;
    if (m_dead) return; // 死亡态不触发（同 pickupScan / arrowPickupScan 门控）
    // t627：只处理本 tick 的踩下沿（边沿触发单一权威表；空表零开销早退——无板场景每 tick 仅一次 QSet 判空）。
    if (m_plateJustPressed.isEmpty()) return;
    for (const quint64 key : m_plateJustPressed) {
        // 解包格坐标（x = 低 21 位有符号偏移、z = 中 21 位、y = 高位；打包见 updatePressurePlates）。
        const int bx = int(quint32(key & 0x1FFFFFu)) - 0x100000;
        const int bz = int(quint32((key >> 21) & 0x1FFFFFu)) - 0x100000;
        const int by = int(quint32(key >> 42)) & 0x3FFu;
        if (!BlockRegistry::isPressurePlate(m_world->blockAt(bx, by, bz))) continue; // 板已被破（沿表陈旧）→ 跳过
        // 压力板 **6 邻**（4 水平 + 上 + 下）有 TNT → 点燃（移除 TNT 方块 + spawnPrimedTnt 延时引爆）。
        //   t492 改 6 邻（用户要求「水平四方向 + 上下两个方向」）；t493 删旧路径(a)「压力板下垫 TNT 直接引爆」
        //   （应**点燃**非瞬爆）；t493 恢复爆炸链式（用户要）→ 点燃后可连锁传播。命中首个 → break（板级单点
        //   足矣：引爆时 detonateTntSphere 链式引燃邻接 TNT，多点同燃只改时序不改结果）。
        //   r2-B4：删旧函数级 return（首板命中即弃本 tick 其余沿 → 同帧踩两块板只触发一块，另一块沿已进
        //   基线**永久丢失**——须离开重踩才有新沿）。改为每板独立处理：沿表里每块板都点燃各自 6 邻首个 TNT。
        static constexpr int kDirs6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto &d : kDirs6) {
            const int tx = bx + d[0], ty = by + d[1], tz = bz + d[2];
            if (BlockRegistry::isTnt(m_world->blockAt(tx, ty, tz))) {
                m_world->clearBlockSilent(tx, ty, tz); // 移除 TNT 方块（点火专用静默清，绕过 occ 守卫）
                m_entityManager->spawnPrimedTnt(tx, ty, tz); // 点燃（默认 fuse；爆炸链式传播）
                break; // 本板单点点燃（链式引爆覆盖其余邻 TNT）
            }
        }
    }
}

// t627 压力板触发态更新（边沿触发单一权威；见 playercontroller.h 头注释）。每 tick 先跑，产出：
//   m_plateJustPressed（本 tick 踩下沿集）供 scanTntTraps / scanDispenserTraps 消费；
//   m_platePressedCells（当前被压下的板全集）作下 tick 边沿比较基线；
//   踩下视觉（state bit0 压半高）沿上置位 / 离开沿清位（5 参数 setBlock，id 不变 → 仅 worldChanged）。
void PlayerController::updatePressurePlates()
{
    m_plateJustPressed.clear();
    if (!m_world) { m_platePressedCells.clear(); return; }
    // 打包格坐标键：x/z 各 21 位有符号偏移（±1M 格余量）、y 10 位（0..1023；世界 H≤64 足够）。
    //   与 scanRedstoneOre 的 cellKey 同编码（两处独立打包子程序——键只在本表内自洽，不跨表比较）。
    const auto cellKey = [](int x, int y, int z) -> quint64 {
        return (quint64(quint32(x + 0x100000) & 0x1FFFFFu))
             | (quint64(quint32(z + 0x100000) & 0x1FFFFFu) << 21)
             | (quint64(quint32(y) & 0x3FFu) << 42);
    };
    // 收集本 tick 被压下的压力板格（触发源：玩家 footprint / mob feet / 掉落物 feet，各经权重判定）。
    QSet<quint64> triggered;
    const auto addIfPlate = [&](int x, int y, int z, bool byItem) {
        const quint8 plate = m_world->blockAt(x, y, z);
        if (BlockRegistry::pressurePlateAccepts(plate, byItem))
            triggered.insert(cellKey(x, y, z));
    };
    // (1) 玩家 footprint（非观察者——观察者无碰撞不压板，机制等价 MC spectator 不触发机关；死亡态同）。
    //   采样同旧 scanTntTraps：脚位 cellY + AABB 覆盖 X/Z（半宽 kHalfW）。
    if (!m_dead && m_mode != Spectator) {
        const int feetY = int(std::floor(m_pos.y()));
        const int x0 = int(std::floor(m_pos.x() - kHalfW));
        const int x1 = int(std::floor(m_pos.x() + kHalfW));
        const int z0 = int(std::floor(m_pos.z() - kHalfW));
        const int z1 = int(std::floor(m_pos.z() + kHalfW));
        for (int bx = x0; bx <= x1; ++bx)
            for (int bz = z0; bz <= z1; ++bz)
                addIfPlate(bx, feetY, bz, /*byItem=*/false);
    }
    // (2) mob（feet 格 = floor(pos - halfH + 0.01)——pos 是 mob 中心，减半高到脚底；+0.01 吸收落地 snap 的
    //   -eps 残差防 floor 落低一格（同 lessons「resting 复探 FP 容差」模式）。仅 kind==Mob 活体（箭/雪球/
    //   下落方块/PrimedTnt 非生物级重量——下落沙落到板上不应触发，机制等价 MC 落沙压板仅在着地成方块后）。
    if (m_entityManager) {
        const int n = m_entityManager->count();
        for (int i = 0; i < n; ++i) {
            if (!m_entityManager->aliveAt(i)) continue;               // 跳过空槽（slot-reuse）
            if (m_entityManager->kindAt(i) != int(EntityManager::Mob)) continue; // 仅生物级实体
            const QVector3D p = m_entityManager->posAt(i);
            const float halfH = m_entityManager->halfHeightAt(i);
            addIfPlate(int(std::floor(p.x())), int(std::floor(p.y() - halfH + 0.01f)),
                       int(std::floor(p.z())), /*byItem=*/false);
        }
    }
    // (3) 掉落物（feet 格 = floor(pos)——ItemEntityManager pos 即实体中心，restY = 支撑顶 + 小偏移 → floor
    //   落在压力板格）。金板（轻质）仅由此源触发——「丢东西到板上触发机关」的红石前置玩法。
    if (m_itemEntities) {
        const int n = m_itemEntities->count();
        for (int i = 0; i < n; ++i) {
            if (!m_itemEntities->aliveAt(i)) continue;                 // 跳过空槽（slot-reuse）
            const QVector3D p = m_itemEntities->posAt(i);
            addIfPlate(int(std::floor(p.x())), int(std::floor(p.y())), int(std::floor(p.z())), /*byItem=*/true);
        }
    }
    // 边沿检测 + 踩下视觉（state bit0）：新进集合 = 踩下沿（m_plateJustPressed + 置位）；移出集合 = 离开沿
    //   （清位）。置/清位走 5 参数 setBlock（id 不变只 state 变 → 仅 worldChanged 重建 mesh，同门开合），
    //   已是目标态则 no-op（防无谓 worldChanged 刷重建）。
    //   r2-B1 读档后首 tick（m_plateBaselineSkipNext，finishWorldLoad 置）：基线已被清空 → 本 tick 进集合的板
    //   全是「存档时已压下」的既存态（沿已消费过），只置压下视觉 + 建基线，**不产沿**（防误触发 TNT / 发射器）。
    //   标记消费即清（严格一 tick，不留抑制窗）。
    const bool baselineSkip = m_plateBaselineSkipNext;
    m_plateBaselineSkipNext = false;
    for (const quint64 key : triggered) {
        if (m_platePressedCells.contains(key)) continue; // 上 tick 已压下 → 非沿
        if (!baselineSkip) m_plateJustPressed.insert(key); // 读档首 tick 不产沿（见上）
        const int x = int(quint32(key & 0x1FFFFFu)) - 0x100000;
        const int z = int(quint32((key >> 21) & 0x1FFFFFu)) - 0x100000;
        const int y = int(quint32(key >> 42)) & 0x3FFu;
        const quint8 plate = m_world->blockAt(x, y, z);
        const quint8 st = m_world->stateAt(x, y, z);
        if ((st & BlockRegistry::PressurePlateStatePressedFlag) == 0)
            m_world->setBlock(x, y, z, plate, quint8(st | BlockRegistry::PressurePlateStatePressedFlag));
    }
    for (const quint64 key : m_platePressedCells) {
        if (triggered.contains(key)) continue; // 本 tick 仍压下 → 非离开沿
        const int x = int(quint32(key & 0x1FFFFFu)) - 0x100000;
        const int z = int(quint32((key >> 21) & 0x1FFFFFu)) - 0x100000;
        const int y = int(quint32(key >> 42)) & 0x3FFu;
        const quint8 plate = m_world->blockAt(x, y, z);
        // r2-B3(i) 离开沿清位前守卫 id：表键可能与栅格解耦一帧以上（板被破后同格放了**其它用 state bit0
        //   的方块**——红石灯 / 门半 / 拉杆 / 探测铁轨）。不查 id → 陈旧键把新方块的 bit0 误清（灯灭 / 门半
        //   编码错乱）。仅该格仍是压力板才清位（同 updateButtonRecovery 到期清位的守卫模式）。
        if (!BlockRegistry::isPressurePlate(plate)) continue; // 板已被破 / 被替换 → 仅让键自然出表
        const quint8 st = m_world->stateAt(x, y, z);
        if ((st & BlockRegistry::PressurePlateStatePressedFlag) != 0)
            m_world->setBlock(x, y, z, plate, quint8(st & quint8(~BlockRegistry::PressurePlateStatePressedFlag)));
    }
    m_platePressedCells = triggered;
}
// t486 发射器陷阱触发（见 playercontroller.h 头注释）。**t627 边沿化**：不再扫玩家 footprint——改读
//   updatePressurePlates 算出的本 tick 踩下沿集（m_plateJustPressed，含玩家/mob/掉落物三类触发源）：沿上
//   的压力板，其 4 水平邻格（同 Y）之一为发射器 / 投掷器 → 触发该机器一次（fireDispenserAt）。
//   踩一次喷一次；持续踩着无新沿 → 不再喷（修「一直踩着往里放东西会一直喷」）；走开回位再踩 → 再喷。
//   机制等价 MC 1.0 发射器陷阱（无红石系统，用「踩板沿直接触发」简化）。
//   **t608 发射方向 = 发射器 state 朝向外向**（同熔炉 / 箱子 chestFrontFace 编码 0=+X 1=-X 2=+Z 3=-Z）：
//   发射器放置时排出口面朝玩家（placeState = horizontalFacing ^ 1），之后**恒朝该方向发射**，与压力板在
//   哪一侧无关。压力板仅作**触发器**，不再是方向源；方向唯一源 = state。旧存档 state=0 → 朝 +X 兜底。
//   **t579 通用化**：发射器有 per-block 库存（DispenserStore 9 槽，玩家右键 UI 放入）→ 走 dispenseFromDispenser
//   按内容物分派（箭 / 雪球 / 剑 / 掉落物）+ 扣库存；无库存（神殿陷阱发射器，worldgen 填充不进 store）保持旧行为
//   （默认射箭，t608 起与库存路径统一用 spawnArrowPlayer 玩家友方箭语义：命中 mob + 可拾取）。
void PlayerController::scanDispenserTraps(float dt)
{
    if (!m_entityManager || !m_world) return;
    if (m_dead) return; // 死亡态不触发（同 scanTntTraps 门控）

    // 递减 per-dispenser 冷却（每 tick）；到期移除。无发射器陷阱场景 m_dispenserCooldowns 恒空（零开销）。
    if (!m_dispenserCooldowns.isEmpty()) {
        for (auto it = m_dispenserCooldowns.begin(); it != m_dispenserCooldowns.end(); ) {
            it.value() -= dt;
            if (it.value() <= 0.0f) it = m_dispenserCooldowns.erase(it);
            else ++it;
        }
    }

    // t627：只处理本 tick 的踩下沿（边沿触发单一权威表；空表零开销早退）。
    for (const quint64 pkey : m_plateJustPressed) {
        // 解包压力板格坐标（x = 低 21 位有符号偏移、z = 中 21 位、y = 高位；打包见 updatePressurePlates）。
        const int bx = int(quint32(pkey & 0x1FFFFFu)) - 0x100000;
        const int bz = int(quint32((pkey >> 21) & 0x1FFFFFu)) - 0x100000;
        const int by = int(quint32(pkey >> 42)) & 0x3FFu;
        if (!BlockRegistry::isPressurePlate(m_world->blockAt(bx, by, bz))) continue; // 板已被破 → 跳过
        // t659 压力板的 **6 邻**（4 水平 + 上 + 下）查发射器 / 投掷器（任意一侧邻接压力板即触发 ——
        //   t608 方向由发射器自身 state 决定，与板在哪侧无关；板只是触发器。t609：投掷器同触发同冷却
        //   ——机制等价 MC 1.0 dropper 与 dispenser 同属触发机关，仅内容物出口分派不同）。
        //   **t659 修「板正上方机器触发不了」**：旧版只扫 4 水平邻（同 Y）—— 板**直接放机器顶上**时
        //   机器在板的 dy=-1（发射器在下 / 板在上），水平邻恒 miss → 用户实测「压力板放在发射器正上方
        //   触发不了」。改为 6 邻（同 scanTntTraps 的 kDirs6 圈，含上下）→ 板上 / 板下机器均触发。
        //   （红石电力路径（板 bit0 → tickRedstone → 接收器）同样覆盖此场景 —— 本直接路径是「与既有机关
        //   触发并存」的直连语义；两路径共用 per-dispenser 冷却闸，无双发。）
        static constexpr int kDirs6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (const auto &d : kDirs6) {
            const int dx = bx + d[0], dy = by + d[1], dz = bz + d[2];
            const quint8 db = m_world->blockAt(dx, dy, dz);
            if (!BlockRegistry::isDispenser(db) && !BlockRegistry::isDropper(db)) continue; // 非发射器/投掷器 → 跳过
            // r2-B4：删旧函数级 return（同 scanTntTraps——同帧踩两板只触发一台机器，另一板沿永久丢失）。
            //   每板独立触发邻接机器；**同一台机器同 tick 被两块板触发**由 fireDispenserAt 的 per-dispenser
            //   冷却闸挡住（首次触发写冷却 → 第二次调用 contains 命中返 false 不动作，无双重发射）。
            fireDispenserAt(dx, dy, dz, db);
        }
    }
}

// t628 按钮自动复位（见 m_buttonRecoverCells / updateButtonRecovery 头注释）：每 tick 递减按下倒计时；
//   到期该格仍是按钮（isWoodButton/isStoneButton）且 bit0 置位 → 清 bit0（5 参数 setBlock，id 不变只 state 变
//   → 仅 worldChanged 重建 mesh，按钮弹回视觉）+ 移除表项；该格已非按钮 / bit0 已清（被破 / 被替换）→ 仅移除
//   表项（不写——防陈旧键误写新方块，同 scanRedstoneOre 到期自熄的守卫模式）。无世界 → no-op。
//   门控：常开（独立于捕获态 / 死亡——按钮弹回是世界模拟，不受玩家状态影响，同掉落物 tick）。
void PlayerController::updateButtonRecovery(float dt)
{
    if (!m_world) return;
    if (m_buttonRecoverCells.isEmpty()) return; // 无按下按钮场景零开销（每 tick 仅一次判空）
    for (auto it = m_buttonRecoverCells.begin(); it != m_buttonRecoverCells.end(); ) {
        it.value() -= dt;
        if (it.value() > 0.0f) { ++it; continue; }
        // 到期：解包格坐标（x = 低 21 位有符号偏移、z = 中 21 位、y = 高位；打包见 placeBlock isManualIgniter 分支）。
        const quint64 key = it.key();
        const int bx = int(quint32(key & 0x1FFFFFu)) - 0x100000;
        const int bz = int(quint32((key >> 21) & 0x1FFFFFu)) - 0x100000;
        const int by = int(quint32(key >> 42)) & 0x3FFu;
        const quint8 b = m_world->blockAt(bx, by, bz);
        if (BlockRegistry::isWoodButton(b) || BlockRegistry::isStoneButton(b)) {
            const quint8 st = m_world->stateAt(bx, by, bz);
            if (st & 1)
                m_world->setBlock(bx, by, bz, b, quint8(st & quint8(~1))); // 清 bit0 弹回（id 不变 → 仅 worldChanged）
        }
        it = m_buttonRecoverCells.erase(it); // 该格已非按钮（被破 / 替换）→ 仅移除表项
    }
}

// t627/t628 发射器 / 投掷器单次触发（scanDispenserTraps 踩板沿 + t628 拉杆/按钮右键激活共用）。
//   (dx,dy,dz) = 机器格坐标、db = 机器方块 id（Dispenser / Dropper）。per-dispenser 冷却（m_dispenserCooldowns，
//   按列坐标键 (x,z) 打包——发射器每柱唯一故 (x,z) 足以定位；Y 不进键防高位重叠）内 → 返 false 不动作；
//   触发成功（含神殿陷阱 fallback 射箭）→ 写冷却 + 返 true。方向 = 机器 state 朝向外向（chestFrontFace 解码；
//   state=0 旧存档 → +X 兜底）。库存路径（t579/t607/t609）与 fallback 语义同旧 scanDispenserTraps 逐字保留。
bool PlayerController::fireDispenserAt(int dx, int dy, int dz, quint8 db)
{
    constexpr float kDispenserCooldown = 2.0f; // 每机器触发间隔（秒；防抖：板沿 + 拉杆沿都过本闸，机制等价 MC 发射器触发间隔）
    const quint64 key = (quint64(quint32(dx)) << 32) | quint64(quint32(dz));
    if (m_dispenserCooldowns.contains(key)) return false; // 该机器冷却中 → 不动作
    // t608 发射方向 = 发射器 state 朝向外向（chestFrontFace 解码 → 轴向单位向量；单一方向源）。
    const quint8 dispState = m_world->stateAt(dx, dy, dz);
    float fdx = 0.0f, fdz = 0.0f;
    switch (BlockRegistry::chestFrontFace(dispState)) {
    case BlockRegistry::PosX: fdx = 1.0f; break;
    case BlockRegistry::NegX: fdx = -1.0f; break;
    case BlockRegistry::PosZ: fdz = 1.0f; break;
    default:                   fdz = -1.0f; break; // NegZ
    }
    const QVector3D dir(fdx, 0.0f, fdz);
    // t579：有 per-block 库存 → 按内容物分派 + 扣库存。**t607 修身份判定**：有 store 条目
    //   （hasDispenser，含全空）= 玩家库存发射器（放置注册 / UI 写入自建 / 存档加载）——库存空
    //   （含最后一个投掷物用完清零）触发**无动作**（陷阱解除）；无条目 = 神殿陷阱发射器（worldgen
    //   不写 store）→ 保持旧行为 fallback 默认箭。t609：投掷器同走 dispenseFromDispenser（store 共用）
    //   ——投掷器分支无 fallback 箭（worldgen 不生成投掷器陷阱，无条目即无内容 → 无动作）。
    const bool tracked = m_dispenserStore && m_dispenserStore->hasDispenser(dx, dy, dz);
    bool fired = false;
    if (tracked)
        fired = dispenseFromDispenser(dx, dy, dz, dir, db);
    if (!fired && !tracked && BlockRegistry::isDispenser(db)) {
        // 神殿陷阱路径（无库存）：默认射箭。从发射器格中心 + 朝向外向前移 0.5（出排出口，防贴墙
        //   spawn 入墙即被 tick 判方块命中，同 fireArrow），水平速度朝 state 朝向，Y 取发射器格
        //   中心高（dy+0.5）。vy=0（近距离水平射；重力会让箭略下沉，走廊内仍命中）。
        //   **陷阱箭须命中玩家**：spawnArrow（arrowFromPlayer=false）——陷阱语义是伤害踩板玩家，
        //   与库存路径（t608 spawnArrowPlayer：命中 mob + 可拾取）**刻意相反**（自伤武装窗口 +
        //   无限箭农场两个理由，见 t607/t608）。伤害走 spawnArrow 内部 kArrowDamage（=2）。
        const QVector3D origin(dx + 0.5f + fdx * 0.5f,
                               float(dy) + 0.5f,
                               dz + 0.5f + fdz * 0.5f);
        const QVector3D vel(fdx * kDispenserArrowSpeed, 0.0f,
                            fdz * kDispenserArrowSpeed);
        m_entityManager->spawnArrow(origin, vel);
    }
    m_dispenserCooldowns.insert(key, kDispenserCooldown); // 写冷却
    return true;
}

// t656/t658 红石电力触发 TNT（World::tickRedstone 通电上升沿 → powerTntTriggered → Main.qml 转发到本
//   入口；见 playercontroller.h 头注释）。机制同 t490 右键机关点火：clearBlockSilent 静默清 TNT 方块
//   （不发 broken/placed 免粒子音 spam；绕过 setBlockFromEntity 的 occ 守卫——TNT 是实体方块）+
//   spawnPrimedTnt 生引燃态实体（默认 fuse ~5s；引爆时 detonateTntSphere 链式引燃邻接 TNT）。
//   该格已非 TNT（信号与栅格解耦一帧以上——同 tick 内既有路径已清）→ no-op。
void PlayerController::firePowerTnt(int x, int y, int z)
{
    if (!m_world || !m_entityManager) return;
    if (!BlockRegistry::isTnt(m_world->blockAt(x, y, z))) return; // 已非 TNT → no-op（防双触发）
    m_world->clearBlockSilent(x, y, z);             // 移除 TNT 方块（点火专用静默清）
    m_entityManager->spawnPrimedTnt(x, y, z);       // 点燃（默认 fuse；爆炸链式传播）
}

// t658 红石电力触发发射器 / 投掷器（powerDispenserTriggered → Main.qml 转发；见头注释）。该格仍是
//   发射器 / 投掷器 → fireDispenserAt（per-dispenser 冷却 / state 朝向 / 库存分派全复用既有机关触发链）。
//   **t689 真上升沿门控**：信号 = 「本机器电力态被复算触达」（稳定通电下每电力活动 tick 都会发——World
//   侧不再判沿）。本端维护 m_dispenserPoweredCells 基线集：isReceivingPower 现读 + 上基线比较，仅
//   unpowered→powered 转换才 fire（机制等价 MC 发射器通电沿触发一次；稳定通电不连发，修「拉杆保持扳开
//   每 2s（冷却）连发到库存空」）。断电触达（降沿）→ 仅清基线（下次再通电才触发）。
void PlayerController::fireDispenserAtQml(int x, int y, int z)
{
    if (!m_world) return;
    const quint8 b = m_world->blockAt(x, y, z);
    if (!BlockRegistry::isDispenser(b) && !BlockRegistry::isDropper(b)) return; // 已非机器 → no-op
    const quint64 key = (quint64(quint32(x)) << 32) | quint64(quint32(z)); // 同 fireDispenserAt 冷却键编码（x<<32|z）
    const bool powered = m_world->isReceivingPower(x, y, z); // 现读电力态（信号不携态，消费端自查）
    if (!powered) { m_dispenserPoweredCells.remove(key); return; } // 降沿：只清基线，不 fire
    if (m_dispenserPoweredCells.contains(key)) return;             // 稳定通电（基线已有）→ 非沿，不 fire
    m_dispenserPoweredCells.insert(key);                           // 记上升沿基线
    fireDispenserAt(x, y, z, b); // per-dispenser 冷却闸（同机器同 tick 双路径触发只 fire 一次）
}

// t579/t580/t608/t609 发射器 / 投掷器内容物弹出（见 playercontroller.h 头注释）。读 DispenserStore 首个可用槽 →
//   按物品分派 → 扣 1 库存（setSlot 写回，count-1 归 0 清槽）。发射位（t608 统一排出口）= 发射器格中心 + 朝向外向 ×0.5
//   （出排出口面中心；防贴墙 spawn 入墙即被 tick 判方块命中，同 fireArrow / 神殿箭路径）—— 箭 / 雪球 / 鸡蛋 /
//   掉落物全部分支用同一 origin（用户「投掷出物品和雪球这些应同一个口出来」）。
//   **t609 投掷器分支**（blockId 为 Dropper）：**全部物品**一律 spawnItemAt 定向弹出掉落物（机制等价 MC 1.0
//   dropper「只投不射」——无箭 / 雪球 / 剑弹丸分派，箭也是普通掉落物实体弹出）。
//   发射器分派（t608 口径统一）：箭 → spawnArrowPlayer（arrowFromPlayer=true：命中 mob 伤害 + 嵌入可拾取）；雪球 →
//   spawnSnowball damage=0（与玩家手抛一致：0 伤 + 红闪 + 击退 + 减速）；鸡蛋 → spawnEgg（0 伤 + 命中碎裂 +
//   1/8 孵小鸡 + 击退）；剑类（ToolRegistry type==Sword）弹射：掉落物实体定向弹出 + 发射方向 3 格内命中活体
//   mob → damageEntity(attackDamage) 一次 + 沿发射方向击退（机制等价 MC 发射器弹射武器命中伤害；红闪 / 死亡
//   掉落走 damageEntity 内既有链）；其余物品 → spawnItemAt 定点定向弹出掉落物（排出口 + 朝向初速 + 0.5s 免拾窗
//   + t468 弹出水平速度抛物）。
bool PlayerController::dispenseFromDispenser(int x, int y, int z, const QVector3D &dir, quint8 blockId)
{
    if (!m_dispenserStore || !m_entityManager) return false;
    // 取首个可用槽（id>0 且 count>0；机制等价 MC 发射器按槽序取首个可用）。
    //   t647：一并快照该槽的实例元数据（附魔 / 名 / 耐久）—— 弹出的掉落物携带（拾取回填保真，
    //   修「附魔工具放进发射器弹出来变普通」；附魔书 / 改名物品经发射器不丢实例数据）。
    int slot = -1, itemId = 0, count = 0;
    QVariantList slotEnch;
    QString slotName;
    int slotDur = -1;
    for (int i = 0; i < DispenserStore::kSlotsPerDispenser; ++i) {
        const int id = m_dispenserStore->slotIdAt(x, y, z, i);
        const int c = m_dispenserStore->slotCountAt(x, y, z, i);
        if (id > 0 && c > 0) {
            slot = i; itemId = id; count = c;
            slotEnch = m_dispenserStore->slotEnchantsAt(x, y, z, i);
            slotName = m_dispenserStore->slotNameAt(x, y, z, i);
            slotDur = m_dispenserStore->slotDurabilityAt(x, y, z, i);
            break;
        }
    }
    if (slot < 0) return false; // 库存空 → caller fallback（神殿默认箭；投掷器 caller 无 fallback → 无动作）

    // 发射位：发射器格中心 + 朝向前移 0.5（出排出口；防 spawn 入墙即命中）。
    const QVector3D origin(float(x) + 0.5f + dir.x() * 0.5f,
                           float(y) + 0.5f,
                           float(z) + 0.5f + dir.z() * 0.5f);

    if (BlockRegistry::isDropper(blockId)) {
        // t609 投掷器分支：**全部物品**一律弹出掉落物实体（机制等价 MC 1.0 dropper——只投不射，箭 / 雪球 /
        //   剑等都不走弹丸 / 伤害分派，一律 spawnItemAt 从排出口沿朝向定向弹出，落地成可拾取掉落物）。
        //   速度取 kDropperPopSpeed（略低于发射器 kDispenserPopSpeed——轻量出口的温和弹出，机制等价 MC
        //   dropper 弹出距离短）。t647：实例元数据随实体走（slotEnch / slotName —— 弹出的附魔 / 改名物品
        //   拾取回填保真；旧版丢元数据）。m_itemEntities 未注入 → 回退 spawnItem 信号路径（同 throwItemInLook
        //   回退模式，防「扣了库存却无实体」静默吞物品——review L4）。
        if (m_itemEntities)
            m_itemEntities->spawnItemAt(origin, itemId, 1, dir.x(), dir.z(), kDropperPopSpeed, slotEnch, slotName, slotDur);
        else
            emit spawnItem(int(std::floor(origin.x())), int(std::floor(origin.y())),
                           int(std::floor(origin.z())), itemId, 1, slotEnch, slotName);
    } else if (itemId == RecipeRegistry::ArrowId) {
        // t608 箭 → **玩家友方箭**（spawnArrowPlayer：arrowFromPlayer=true 语义）：命中 **mob**（damageEntity +
        //   击退 + 红闪，机制等价 MC 1.0 发射器箭可打生物）且嵌入方块后**可被玩家拾取**（arrowPickupScan 只拾
        //   arrowFromPlayer=true 的嵌入箭 → +1 箭物品，机制等价 MC 1.0 发射器箭可拾）。旧版 spawnArrow(false)
        //   命中**玩家**（骷髅箭语义）+ 不可拾 —— 与用户「发射器箭应可拾取 + 可砸生物」诉求相反。
        //   伤害取 kDispenserArrowDamage（本地常量 = EntityManager::kArrowDamage 同值 2；后者 private 不能跨层读，
        //   同 kDispenserSnowballDamage 本地常量模式）。玩家交互：arrowFromPlayer 箭出膛 0.2s（kArrowSelfArmDelay）
        //   后「武装」，下落砸中玩家也会扣血（t324 既有自伤例外）—— 机制等价 MC 1.0 发射器箭可伤任何生物
        //   含玩家；水平射出时首帧已飞出踩板玩家命中盒（0.5 排出口偏移）→ 正常不误伤触发者。
        m_entityManager->spawnArrowPlayer(origin, dir * kDispenserArrowSpeed, kDispenserArrowDamage);
    } else if (itemId == RecipeRegistry::SnowballId) {
        // t608 雪球 → 投掷物实体，**与玩家手抛同口径：damage=0（0 伤害 + 红闪 + 击退 + 减速）**。旧版对 mob 有
        //   1HP 实伤（kDispenserSnowballDamage，机关陷阱口径）—— 用户口径「和手持这些投掷物一样：没有伤害
        //   只有击退」→ 统一 0 伤。命中链：damageEntity(0) 被 amount<=0 守卫早退 → 命中分支手动设 hurtFlash
        //   红闪 + knockback 击退 + slowTimer 减速（t505 0 伤害反馈链，与玩家手抛完全同一代码路径，机制等价
        //   MC 1.0 发射器雪球与手掷雪球同为无伤击退投掷物）。thrower=-1 = 无实体发射者，不排除任何 mob。
        m_entityManager->spawnSnowball(origin, dir * kDispenserSnowballSpeed, 0);
    } else if (itemId == RecipeRegistry::EggId) {
        // t583 鸡蛋 → 投掷物实体（同玩家手抛口径：0 伤害 + 命中碎裂 + 1/8 概率孵小鸡；机制等价 MC 1.0 发射器
        //   弹鸡蛋可砸出小鸡 —— 机关「蛋孵化器」玩法）。t608 命中 mob 击退（与雪球同逻辑，entitymanager Egg
        //   分支处理）。速度复用发射器雪球速度（同为轻抛物弹丸）。
        m_entityManager->spawnEgg(origin, dir * kDispenserSnowballSpeed);
    } else {
        const ToolRegistry::ToolDef *td = ToolRegistry::tool(itemId);
        if (td && td->type == BlockRegistry::Sword) {
            // t580 剑 → 发射方向 kDispenserWeaponRange 格内命中活体 mob → ToolRegistry::attackDamage 一次
            //   + 沿发射方向击退（发射器弹射武器，机制等价 MC 发射器射武器伤害）；随后剑本体弹出掉落物。
            const int mobIdx = m_entityManager->findMobHit(origin, dir, kDispenserWeaponRange);
            if (mobIdx >= 0) {
                const int dmg = std::max(1, ToolRegistry::attackDamage(itemId));
                m_entityManager->damageEntity(mobIdx, dmg);
                const QVector3D mobPos = m_entityManager->posAt(mobIdx);
                m_entityManager->knockback(mobIdx, mobPos.x() - origin.x(), mobPos.z() - origin.z());
            }
        }
        // 其余物品（含剑本体弹出）→ 掉落物实体。t608 **统一排出口**：origin（发射器格中心 + 朝向外向 ×0.5）
        //   与箭 / 雪球 / 鸡蛋同一口出来（旧版 emit spawnItem 用发射器格中心 → 与投掷物两个口，用户「投掷出
        //   物品和雪球这些不是一个口出来的」）+ 沿朝向定向弹出初速 kDispenserPopSpeed（旧版哈希随机方向）。
        //   走 spawnItemAt（定点定向弹出，C++ 直调同 pickupScan 的 removeAt 模式；免 QML 信号往返）。
        //   t647：实例元数据随实体走（slotEnch / slotName / slotDur —— DispenserStore 槽现持元数据，
        //   弹出的附魔工具 / 附魔书 / 改名物品拾取回填保真；旧版丢元数据「发射器弹出附魔书变普通书」）。
        //   m_itemEntities 未注入 → 回退 spawnItem 信号路径（同 throwItemInLook 回退模式，防「扣了库存
        //   却无实体」静默吞物品——review L4）。
        if (m_itemEntities)
            m_itemEntities->spawnItemThrown(origin, itemId, 1, dir.x(), 0.0f, dir.z(), kDispenserPopSpeed, slotEnch, slotName, slotDur);
        else
            emit spawnItem(int(std::floor(origin.x())), int(std::floor(origin.y())),
                           int(std::floor(origin.z())), itemId, 1, slotEnch, slotName);
    }
    // 扣 1 库存（count-1；归 0 → setSlot 空栈归一清槽——t607 修：count 归 0 时 id 一并归 0，旧版存
    //   {id>0,count=0} 幽灵栈致「UI 图标残留 / 不再发射 / 拿出物品消失」）。分派表全覆盖（else 兜底）→ 恒扣。
    //   t647：扣库存走「count-1、元数据保留」的保真写回（count>1 时同槽余件仍带实例元数据；归 0 清槽）。
    if (count > 1)
        m_dispenserStore->setSlot(x, y, z, slot, itemId, count - 1, slotEnch, slotName, slotDur);
    else
        m_dispenserStore->setSlot(x, y, z, slot, 0, 0);
    // t619 progress 成就埋点：玩家库存发射器/投掷器成功弹出物品 → dispenserFired 语义事件（「发射!」）。
    //   仅此库存路径发（神殿陷阱 fallback 算 worldgen 机关非玩家成就）。
    emit dispenserFired();
    return true;
}

// t569 红石矿石置亮 / 熄（机制等价 MC 1.0 redstone ore 发光翻转；见 playercontroller.h 头注释）。
//   走 5 参数 setBlock（id 不变只 state 变 → 仅发 worldChanged；World setBlock 内 recomputeLightAround 用
//   状态感知 lightEmission 检出 0↔9 光变 → 增量重 flood 方块光，微弱阴沉红光泛出）。已是目标态 / 非红石矿 /
//   越界 → no-op（同 setFurnaceLit 模式，避免无谓 worldChanged 刷重建）。
void PlayerController::setRedstoneOreLit(int x, int y, int z, bool lit)
{
    if (!m_world) return;
    const quint8 cur = m_world->blockAt(x, y, z);
    if (!BlockRegistry::isRedstoneOre(cur)) return;  // 非红石矿（已被破 / 替换）→ no-op
    const quint8 oldState = m_world->stateAt(x, y, z);
    const quint8 newState = lit ? quint8(oldState | BlockRegistry::RedstoneOreStateLitFlag)
                                : quint8(oldState & quint8(~BlockRegistry::RedstoneOreStateLitFlag));
    if (newState == oldState) return;                // 已是目标态 → no-op
    m_world->setBlock(x, y, z, cur, newState);       // id 不变 → 仅 worldChanged + 光重 flood
}

// t569 红石矿石点亮触发（见 playercontroller.h 头注释）。扫玩家 footprint 格 ± 水平 4 邻 × 3 行
//   （feetY-1 / feetY / feetY+1 —— 覆盖走过旁格 / 相邻蹭到 / 站其上 / 头顶邻层），命中 RedstoneOre →
//   setRedstoneOreLit(true) + 点亮表续时 RedstoneOreLitSeconds（已亮条目续时 → 光不闪断）。
//   点亮表倒计时递减（dt），到期 setRedstoneOreLit(false) 自熄 + 移除（机制等价 MC 触发发光一次点亮窗口，
//   ~5s 后熄灭；玩家持续在旁则反复续时）。
void PlayerController::scanRedstoneOre(float dt)
{
    if (!m_world) return;
    // 点亮表倒计时：每 tick 递减 dt，到期熄灭并移除（先递减再扫描 → 玩家仍在旁时下方扫描会重新续时）。
    if (!m_redstoneLitCells.isEmpty()) {
        for (auto it = m_redstoneLitCells.begin(); it != m_redstoneLitCells.end(); ) {
            it.value() -= dt;
            if (it.value() <= 0.0f) {
                // 自熄：解包坐标（x = 低 21 位有符号偏移、z = 中 21 位、y = 高位；打包见下方 cellKey）。
                const quint64 key = it.key();
                const int lx = int(quint32(key & 0x1FFFFFu)) - 0x100000;
                const int lz = int(quint32((key >> 21) & 0x1FFFFFu)) - 0x100000;
                const int ly = int(quint32(key >> 42)) & 0x3FFu;
                setRedstoneOreLit(lx, ly, lz, false);
                it = m_redstoneLitCells.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (m_dead) return; // 死亡态不触发新点亮（同 pickupScan / scanTntTraps 门控；已亮的仍倒计时自熄）
    // t706 观察者门控（机制等价 MC spectator 不触发方块交互）：观察者穿行红石矿不应点亮（同压力板
    //   updatePressurePlates 的 m_mode != Spectator 采样门）。已亮的仍倒计时自熄（世界模拟连续）。
    if (m_mode == Spectator) return;
    // 玩家 footprint 格（脚位 cellY + AABB 覆盖的 X/Z 格）± 水平 4 邻 × 3 行（feetY-1..feetY+1）。
    //   3 行覆盖：走过红石矿旁（同层邻格）/ 站在红石矿上（feetY-1 是脚下矿）/ 头顶邻层矿。同
    //   scanTntTraps / scanDispenserTraps footprint 采样模式。
    const int feetY = int(std::floor(m_pos.y()));
    const int x0 = int(std::floor(m_pos.x() - kHalfW)) - 1;
    const int x1 = int(std::floor(m_pos.x() + kHalfW)) + 1;
    const int z0 = int(std::floor(m_pos.z() - kHalfW)) - 1;
    const int z1 = int(std::floor(m_pos.z() + kHalfW)) + 1;
    for (int by = feetY - 1; by <= feetY + 1; ++by) {
        for (int bx = x0; bx <= x1; ++bx) {
            for (int bz = z0; bz <= z1; ++bz) {
                if (!BlockRegistry::isRedstoneOre(m_world->blockAt(bx, by, bz))) continue;
                // 置亮 + 续时（已亮条目覆盖写 → 玩家在旁光不闪断；离开后倒计时自熄）。
                setRedstoneOreLit(bx, by, bz, true);
                // 打包坐标键：x/z 各 21 位有符号偏移（±1M 格余量）、y 10 位（0..1023；世界 H≤64 足够）。
                const quint64 key = (quint64(quint32(bx + 0x100000) & 0x1FFFFFu))
                                  | (quint64(quint32(bz + 0x100000) & 0x1FFFFFu) << 21)
                                  | (quint64(quint32(by) & 0x3FFu) << 42);
                m_redstoneLitCells.insert(key, BlockRegistry::RedstoneOreLitSeconds);
            }
        }
    }
}

// t146 放置校验：按「将放置方块的实际形状 sub-AABB」判是否与玩家 AABB 相交（3 轴严格重叠）。
//   id/state = 放置态（placeBlock 算好后传入）。air/torch（shape=ShapeNone）→ collisionAABBs 空 →
//   不相交 → 允许放入玩家格（机制等价 MC「非实体方块可放入玩家所在格」）。t51：AABB 高用 m_height。
bool PlayerController::overlapsPlayerAABB(int bx, int by, int bz, quint8 id, quint8 state) const
{
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float miny = m_pos.y(),           maxy = m_pos.y() + m_height;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    const float fx = float(bx), fy = float(by), fz = float(bz);
    for (const BlockRegistry::BlockAABB &a : BlockRegistry::collisionAABBs(id, state)) {
        if (minx < a.maxX + fx && maxx > a.minX + fx &&
            miny < a.maxY + fy && maxy > a.minY + fy &&
            minz < a.maxZ + fz && maxz > a.minZ + fz) return true;
    }
    return false;
}

// t146 玩家 AABB vs 世界碰撞 sub-AABB 重叠测试（3 轴严格重叠）。axis∈{0,1,2} 时记录沿该轴的相交
//   sub-AABB 表面（outMinSurf = 相交盒 min 表面；outMaxSurf = 相交盒 max 表面），供 moveAxis 贴面：
//   向 + 移动贴到 minSurf（玩家 max 边 = 最近进入面）、向 - 移动贴到 maxSurf（玩家 min 边 = 最近阻挡面）。
//   axis<0 仅判命中（aabbHitsSolid 用，不填表面）。取样范围与旧 aabbHitsSolid 同策略。
//   t161 修：maxSurfCap 仅把 bmax<=cap 的块顶计入 maxSurf（向下着地用：cap=pyBefore → 只累计「玩家移动前
//   脚底 ≥ 块顶」的可着陆面，忽略沙等 bury 块），outHasMax 报是否有合格块。默认 cap=+inf、outHasMax=nullptr
//   → 取全部块顶（旧行为），兼容 aabbHitsSolid / 水平轴贴面调用。
bool PlayerController::overlapSubAABBs(int axis, float *outMinSurf, float *outMaxSurf,
                                       bool *outHasMax, float maxSurfCap,
                                       bool *outHasMin, float minSurfFloor) const
{
    if (!m_world) return false;
    // t259：完整 AABB（cell 采样边界用——不漏采贴面格）。t51：高用 m_height（蹲下变矮）。
    const float fminx = m_pos.x() - kHalfW, fmaxx = m_pos.x() + kHalfW;
    const float fminy = m_pos.y(),           fmaxy = m_pos.y() + m_height;
    const float fminz = m_pos.z() - kHalfW, fmaxz = m_pos.z() + kHalfW;
    // t259 碰撞皮肤：逐块重叠判定用「内缩 skin」的有效 AABB —— 吸收落地 / 贴墙 snap 的 +eps（1e-4）
    //   等浮点漂移，使蹲下（AABB 1.5）能通过「精确 1.5 格」通道（上半砖天花板 / 整砖+下半砖组合），
    //   对齐 MC 1.0 蹲下通行（见 playercontroller.h kCollisionSkin 注释）。
    const float sk = kCollisionSkin;
    const float minx = fminx + sk, maxx = fmaxx - sk;
    const float miny = fminy + sk, maxy = fmaxy - sk; // t51：蹲下用 m_height（变矮）
    const float minz = fminz + sk, maxz = fmaxz - sk;
    // cell 采样仍走完整 AABB（ceil(max)-1 排除「仅贴面」的方块 → 防卡缝，且不漏采边界格）：
    const int x0 = int(std::floor(fminx)), x1 = int(std::ceil(fmaxx)) - 1;
    // t209 Y 取样向下扩 1 格（yFloor - 1）：栅栏等「高 AABB」（maxY > 1，探入上格）的方块其 sub-AABB 会从
    //   玩家脚位下一格延伸上来。玩家跳跃（脚位上升到 F+1.x）时，栅栏立柱（cell F，AABB [F, F+1.5]）仍在
    //   Y 区间重叠 [F+1.x, F+1.5]，但 floor(miny)=F+1 → 原 y0=F+1 漏掉 cell F → 玩家跨格横移即穿隧道越栅栏。
    //   扩 1 格后 y0=F 命中栅栏格、3 轴严格重叠测试仍过滤掉「 maxY<=cell 顶」的普通方块（其 AABB 上界 ≤
    //   yFloor ≤ miny → miny < b.maxY=false，无假阳性）。仅 +Y 向上凸出的形状（栅栏 / 未来 fence gate 等）获益。
    const int yFloor = int(std::floor(fminy));
    const int y0 = yFloor - 1, y1 = int(std::ceil(fmaxy)) - 1;
    const int z0 = int(std::floor(fminz)), z1 = int(std::ceil(fmaxz)) - 1;
    bool hit = false, haveMin = false, haveMax = false;
    float minSurf = 0.f, maxSurf = 0.f;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(x, y, z)) {
                    if (!(minx < b.maxX && maxx > b.minX &&
                          miny < b.maxY && maxy > b.minY &&
                          minz < b.maxZ && maxz > b.minZ)) continue; // 3 轴任一仅贴面 / 不重叠 → 跳过
                    hit = true;
                    if (axis < 0) continue; // 仅判命中（aabbHitsSolid）
                    const float bmin = (axis == 0) ? b.minX : (axis == 1) ? b.minY : b.minZ;
                    const float bmax = (axis == 0) ? b.maxX : (axis == 1) ? b.maxY : b.maxZ;
                    // t352 修（对称 maxSurfCap）：minSurf 只累计 bmin>=floor 的块（向上顶头时 floor=pyBefore+
                    //   m_height 过滤掉身体 / 脚位 partial 块，只留「玩家原头顶之上」的真天花板）。
                    if (outMinSurf && bmin >= minSurfFloor && (!haveMin || bmin < minSurf)) { minSurf = bmin; haveMin = true; }
                    // t161 修：maxSurf 只累计 bmax<=cap 的块（向下着地时 cap=pyBefore 过滤掉沙等 bury 块顶）
                    if (outMaxSurf && bmax <= maxSurfCap && (!haveMax || bmax > maxSurf)) { maxSurf = bmax; haveMax = true; }
                }
            }
    if (hit) {
        if (outMinSurf && haveMin) *outMinSurf = minSurf;
        if (outMaxSurf && haveMax) *outMaxSurf = maxSurf;
    }
    if (outHasMax) *outHasMax = haveMax; // 区分「有碰撞但无可着陆面=纯 bury」与「有可着陆面」
    if (outHasMin) *outHasMin = haveMin; // t352：区分「有碰撞但无真天花板=身体级 partial 重叠」与「有真天花板」
    return hit;
}

// t134 玩家水平朝向（4 向，据 yaw 推）：前向 = (-sin(yaw), -cos(yaw))（与 wishHoriz/lookDirection 同源）。
//   主轴（|fx| vs |fz|）+ 符号 → 0=+X 1=-X 2=+Z 3=-Z（与 stairs/door/trapdoor state 朝向编码一致）。
int PlayerController::horizontalFacing() const
{
    const float yr = m_yaw * kDeg;
    const float fx = -std::sin(yr), fz = -std::cos(yr);
    if (std::fabs(fx) >= std::fabs(fz)) return fx > 0.0f ? 0 : 1; // +X / -X
    return fz > 0.0f ? 2 : 3;                                     // +Z / -Z
}

QVector3D PlayerController::wishHoriz() const
{
    const float yr = m_yaw * kDeg;
    const float fx = -std::sin(yr), fz = -std::cos(yr); // 前
    const float rx = std::cos(yr), rz = -std::sin(yr);  // 右
    float dx = 0, dz = 0;
    if (m_keys.value(Qt::Key_W)) { dx += fx; dz += fz; }
    if (m_keys.value(Qt::Key_S)) { dx -= fx; dz -= fz; }
    if (m_keys.value(Qt::Key_D)) { dx += rx; dz += rz; }
    if (m_keys.value(Qt::Key_A)) { dx -= rx; dz -= rz; }
    QVector3D w(dx, 0, dz);
    if (w.lengthSquared() > 0) w.normalize();
    return w;
}

// 移动状态速率因子（t51）：Sprint×1.3 / Crouch×0.4 / Walk×1.0。
//   仅走路模式（Survival / Creative-未飞）的水平速度乘此值；飞 / Spectator 恒 Walk（speedMul=1）。
//   spec「疾跑移速 ×1.3、蹲下 ×0.4」。同时驱动 moveSpeed 报告 → walkPhase 推进频率 → 四肢摆频。
float PlayerController::speedMul() const
{
    switch (m_moveState) {
    case Sprint: return 1.3f;
    case Crouch: return 0.4f;
    default:     return 1.0f; // Walk
    }
}

// t159 当前有效飞行速度（blocks/sec）= clamp(kFly * flySpeedMul, kFlyMin, kFlyMax)。
//   spectator 与 creative-飞 共用（统一「飞行」单一旋钮）；step 各飞态分支读它算位移。
float PlayerController::flySpeed() const
{
    return std::clamp(kFly * m_flySpeedMul, kFlyMin, kFlyMax);
}

// t159 水下判定：眼位格 == Water。眼位 = position()（脚底 + eyeHeight，与相机同源）。只读 World::blockAt
//   （向下依赖，不改栅格；蹲下眼位降低随之变）。无世界 → false（保守不减速）。
bool PlayerController::eyeInWater() const
{
    if (!m_world) return false;
    const QVector3D eye = position();
    return m_world->blockAt(int(std::floor(eye.x())), int(std::floor(eye.y())), int(std::floor(eye.z())))
           == BlockRegistry::Water;
}

// t351 岩浆中判定（见 .h 头注释）：眼位格 == Lava（与 eyeInWater 平行；水/岩浆互斥）。只读 World::blockAt。
bool PlayerController::eyeInLava() const
{
    if (!m_world) return false;
    const QVector3D eye = position();
    return m_world->blockAt(int(std::floor(eye.x())), int(std::floor(eye.y())), int(std::floor(eye.z())))
           == BlockRegistry::Lava;
}

// t174 脚位水中判定：脚底格 == Water（m_pos 整数坐标 → 脚所处方块）。浮力/游泳物理用它（眼位高于水面
//   时仍能游；机制等价 MC「在水中游泳」= 脚或身在水中即可）。只读 World::blockAt；无世界 → false。
bool PlayerController::feetInWater() const
{
    if (!m_world) return false;
    return m_world->blockAt(int(std::floor(m_pos.x())), int(std::floor(m_pos.y())), int(std::floor(m_pos.z())))
           == BlockRegistry::Water;
}

// t468 脚下冰面判定：着地（m_onGround，上一 tick 解算结果）且脚底下方一格为冰族（isIce）。供 step() 冰滑行物理
//   分流：在冰上时水平速度向目标做指数接近（低摩擦 → 松键惯性滑行），非冰走瞬时设速（常规地面手感）。
//   脚底下方一格 = floor(m_pos.y) - 1（m_pos 存脚底中心）。无世界 → false。只读 World::blockAt + BlockRegistry::isIce。
bool PlayerController::onIce() const
{
    if (!m_world || !m_onGround) return false;
    const int bx = int(std::floor(m_pos.x()));
    const int by = int(std::floor(m_pos.y())) - 1; // 脚底下方一格（支撑面）
    const int bz = int(std::floor(m_pos.z()));
    return BlockRegistry::isIce(m_world->blockAt(bx, by, bz));
}

// t565 蛛网粘滞判定（见 .h 头注释；rv-low-batch2 补实现）：玩家 AABB footprint（±kHalfW）各列在脚位 +
//   身体（脚 +1）两行内任一格 == Cobweb 即真（取样策略同 onLadder / hasGroundBelowAt 的 footprint 全列严格
//   覆盖；Cobweb 无碰撞 ShapeNone → 玩家穿入网格占据该格，覆盖即粘滞）。step() 走路分支据此把目标水平速度
//   ×kCobwebSpeedMul（同 waterMul 乘入模式，机制等价 MC 1.0 cobweb 粘滞减速；矿井散布的蛛网从此真粘人）。
//   只读 World::blockAt（向下依赖，不改栅格）；无世界 → false。
bool PlayerController::inCobweb() const
{
    if (!m_world) return false;
    const int by0 = int(std::floor(m_pos.y()));           // 脚位行
    const int by1 = by0 + 1;                              // 身体行（玩家 1.8 高 AABB 上格）
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int zz = z0; zz <= z1; ++zz)
        for (int xx = x0; xx <= x1; ++xx) {
            if (m_world->blockAt(xx, by0, zz) == BlockRegistry::Cobweb) return true;
            if (m_world->blockAt(xx, by1, zz) == BlockRegistry::Cobweb) return true;
        }
    return false;
}

// t223 flowSoundLevel 属性 READ：返回 m_flowSoundLevel（tickImpl 节流扫描缓存值）。
float PlayerController::flowSoundLevel() const
{
    return m_flowSoundLevel;
}

// t223 近流水 proximity 扫描：在玩家眼位周围 kFlowSoundRadius 立方盒内查最近**流动水**格
//   （Water 且 state>0；静水水源 state=0 不算 —— MC 近大片静海无流水声、近瀑布 / 玩家倒水流才有，
//   spec「近流动水」）。返回 [0,1] 强度：1 = 贴脸流水格、0 = 范围外 / 无流水 / 无世界。
//   level = clamp(1 - minDist / kFlowSoundRadius, 0, 1)（线性衰减；minDist 用欧氏距离的三维近似 ——
//   取整数格中心 (cx+0.5,cy+0.5,cz+0.5) 到眼位的距离，近强远弱）。只读 World::blockAt/stateAt
//   （向下依赖，不改栅格）。扫描盒 (2R+1)³ 在 R=8 时 ~17³≈4900 格，但 Y 维玩家通常 ±8 已含全部水柱，
//   实际 blockAt/stateAt 是 O(1) 数组索引 → 整体亚毫秒级，每 0.25s 一次不影响帧率。
float PlayerController::scanFlowSoundLevel() const
{
    if (!m_world) return 0.0f;
    const QVector3D eye = position();  // 玩家眼位（脚底 + eyeHeight）
    const float ex = eye.x(), ey = eye.y(), ez = eye.z();
    const int cx = int(std::floor(ex)), cy = int(std::floor(ey)), cz = int(std::floor(ez));
    const int R = int(kFlowSoundRadius);
    float minDistSq = kFlowSoundRadius * kFlowSoundRadius + 1.0f;  // 初始化为超出范围（>R² → 无命中返 0）
    bool found = false;
    // 扫描盒 [cx-R, cx+R] × [cy-R, cy+R] × [cz-R, cz+R]；Y 维世界高度由 World::blockAt 越界返 0 兜底。
    for (int dx = -R; dx <= R; ++dx) {
        for (int dy = -R; dy <= R; ++dy) {
            for (int dz = -R; dz <= R; ++dz) {
                const int bx = cx + dx, by = cy + dy, bz = cz + dz;
                if (m_world->blockAt(bx, by, bz) != BlockRegistry::Water) continue;
                if (m_world->stateAt(bx, by, bz) == 0) continue;  // 静水水源不算（spec「流动水」）
                // 格中心到眼位的距离平方（欧氏）；用平方比较免开方。
                const float ddx = (float(bx) + 0.5f) - ex;
                const float ddy = (float(by) + 0.5f) - ey;
                const float ddz = (float(bz) + 0.5f) - ez;
                const float dsq = ddx * ddx + ddy * ddy + ddz * ddz;
                if (dsq < minDistSq) {
                    minDistSq = dsq;
                    found = true;
                }
            }
        }
    }
    if (!found) return 0.0f;
    const float dist = std::sqrt(minDistSq);
    float level = 1.0f - dist / kFlowSoundRadius;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    return level;
}

// t343 lavaSoundLevel 属性 READ：返回 m_lavaSoundLevel（tickImpl 节流扫描缓存值）。
float PlayerController::lavaSoundLevel() const
{
    return m_lavaSoundLevel;
}

// t343 近岩浆 proximity 扫描（机制同 scanFlowSoundLevel，但查 Lava 格——源 / 流皆算：岩浆湖多为源，
//   rumble 应近任何岩浆响起，区别于水流声仅算流态）。返回 [0,1] 强度：1=贴脸岩浆格、0=范围外 / 无岩浆 / 无世界。
float PlayerController::scanLavaSoundLevel() const
{
    if (!m_world) return 0.0f;
    const QVector3D eye = position();
    const float ex = eye.x(), ey = eye.y(), ez = eye.z();
    const int cx = int(std::floor(ex)), cy = int(std::floor(ey)), cz = int(std::floor(ez));
    const int R = int(kFlowSoundRadius);
    float minDistSq = kFlowSoundRadius * kFlowSoundRadius + 1.0f;
    bool found = false;
    for (int dx = -R; dx <= R; ++dx) {
        for (int dy = -R; dy <= R; ++dy) {
            for (int dz = -R; dz <= R; ++dz) {
                const int bx = cx + dx, by = cy + dy, bz = cz + dz;
                if (m_world->blockAt(bx, by, bz) != BlockRegistry::Lava) continue;
                const float ddx = (float(bx) + 0.5f) - ex;
                const float ddy = (float(by) + 0.5f) - ey;
                const float ddz = (float(bz) + 0.5f) - ez;
                const float dsq = ddx * ddx + ddy * ddy + ddz * ddz;
                if (dsq < minDistSq) { minDistSq = dsq; found = true; }
            }
        }
    }
    if (!found) return 0.0f;
    const float dist = std::sqrt(minDistSq);
    float level = 1.0f - dist / kFlowSoundRadius;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    return level;
}

// t159 上报实际水平速度（speed 属性）：= |水平位移| / dt。含撞墙归零、疾跑 / 飞 / 水下倍数 —— 反映
//   玩家**真实**水平移动速率（非意图速度）。值真变（> 0.05 阈值，免帧间噪声刷 QML）才发 moveSpeedChanged
//   （speed 复用此 NOTIFY）。dt<=0 → no-op（极端：首帧 dt 未取）。
void PlayerController::reportHorizSpeed(const QVector3D &posBefore, qreal dt)
{
    if (dt <= 1e-5) return;
    const float dx = m_pos.x() - posBefore.x();
    const float dz = m_pos.z() - posBefore.z();
    const float s = std::sqrt(dx * dx + dz * dz) / float(dt);
    if (std::fabs(s - m_horizSpeed) > 0.05f) {
        m_horizSpeed = s;
        emit moveSpeedChanged();
    }
    // progress 走过路程埋点：纯水平位移增量（√(dx²+dz²)，不含跳跃 dy），由呈现层 Connections 路由到
    //   progress.onMove。本函数是 step 各出口（船 / 飞 / 走 / 观察者）的唯一位移瓶颈 → 每帧每条移动路径
    //   只计一次（无重复计数）。delta>0 才发（静止站位不刷信号，免无谓 QML 调用）。progress 内部节流累积
    //   到 ~0.5s flush（onPlayTimeTick 闸门），非每帧 emit progressChanged 抖 QML 绑定。
    const float delta = std::sqrt(dx * dx + dz * dz);
    if (delta > 0.0f) emit moved(delta);
}

// 切移动态（t51）：同步更新 AABB 高 / 眼位（蹲下变矮、相机随之降低）。无变化静默。
//   蹲下：m_height=kCrouchHeight(1.5) / m_eyeHeight=kCrouchEye(1.35)；
//   站起（Walk/Sprint）：m_height=kHeight(1.8) / m_eyeHeight=kEyeHeight(1.62)。
//   相机 position 读 m_eyeHeight → 蹲下相机自动降低（无需 QML 额外处理）。
// t574/t575 集中站起闸门：从 Crouch 切往 Walk/Sprint（= 站起）时，若头顶无站起空间（canStandUp()==false，
//   1.5 格通道 / 低天花板），一律拒绝切换 —— 无论 shift 是否按住、无论触发源（松 shift / 开关背包 release /
//   飞行起飞 / 切模式前的态清理）。setMoveState 是所有「切出 Crouch」路径的单一瓶颈（setKey 松 shift、
//   release、双击空格起飞、setMode、respawn、loadSavedState）→ 在此设闸后任何路径都不会把 1.8 AABB
//   塞进 1.5 空间（不嵌入 → 头不卡方块、extrudeEmbedded 不推）。拒绝时标 m_autoCrouch：step 内每 tick
//   复探头顶，有空间即自动站起（走出低顶区 → 自动站）。例外：Spectator 是真 noclip（位移直加 m_pos 不走
//   moveAxis，无碰撞嵌入问题），respawn/loadSavedState 摆到新位置（旧位置头顶判定无意义）→ 这两路径用
//   force 参数绕过闸门。
//   review M6：旧版豁免还含「Creative + 飞行」（注释称 noclip —— 不实：创造飞行走 moveAxis 子步碰撞，
//   有嵌入问题）。蹲在 1.5 格通道双击空格起飞 → 闸门被豁免放行 → 1.8 AABB 塞进 1.5 空间嵌入天花板 →
//   飞行分支早 return（不走 extrudeEmbedded 自救）→ 全向位移被 moveAxis snap 锁死 = 永久卡死。收紧为
//   仅 Spectator：创造头顶不足时起飞被拒（保持蹲，m_autoCrouch 复探；开阔处起飞不受影响——头顶本就有
//   站起空间，canStandUp 恒过）。
void PlayerController::setMoveState(MoveState s, bool force)
{
    if (s == m_moveState) return;
    // t574/t575 站起闸门：当前蹲 + 目标站 + 头顶站不下（且非 noclip 模式 / 非强制重置）→ 拒绝站起，
    //   转为「自动蹲保持」（清计时语义同 t559：等 step 复探 canStandUp 再站）。
    if (m_moveState == Crouch && s != Crouch && !force
        && m_mode != Spectator
        && !canStandUp()) {
        m_autoCrouch = true;
        return;
    }
    m_moveState = s;
    if (s == Crouch) {
        m_height = kCrouchHeight;
        m_eyeHeight = kCrouchEye;
    } else {
        // t559：切出 Crouch（站起 / 切模式 / 飞行 / respawn / loadSavedState）→ 清自动蹲标记（不再每 tick 复探）。
        //   注意：若 s==Crouch 但 m_autoCrouch 仍 true（松 shift 自动蹲持续中），此处不清 —— 保持自动蹲态。
        m_autoCrouch = false;
        m_height = kHeight;
        m_eyeHeight = kEyeHeight;
    }
    emit moveStateChanged();
    emit positionChanged(); // 眼位随蹲 / 站变 → 相机 position 绑定刷新（蹲下相机降低）
}

// 蹲下「边缘安全」支撑判定（t51）：给定水平位置 (x,z)，当前脚位 m_pos.y() 下方 0.05 处那一格
//   在 AABB footprint（kHalfW 宽）内任一列实体即算「有支撑」。step() 据此判定蹲下时是否允许
//   水平移动（无支撑 → 不移动，防走下边缘）。仅读 World（isSolid），与碰撞同层。
//   注：脚底 y = 站立方块顶面（整数 + eps）；其下一格 = floor(y - 0.05)。仅着地时调用（见 step）。
bool PlayerController::hasGroundBelowAt(float x, float z) const
{
    if (!m_world) return false;
    const float checkY = m_pos.y() - 0.05f;          // 脚底略下 → 支撑方块所在格
    const int by = int(std::floor(checkY));
    const float minx = x - kHalfW, maxx = x + kHalfW;
    const float minz = z - kHalfW, maxz = z + kHalfW;
    // 严格覆盖（与 aabbHitsSolid 同取样策略）：AABB footprint 内所有可能列
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int zz = z0; zz <= z1; ++zz)
        for (int xx = x0; xx <= x1; ++xx)
            if (m_world->isCollidable(xx, by, zz)) return true; // t88：火把 non-solid 不挡玩家
    return false;
}

// t413 贴梯检测（spec「玩家走进梯格 + 按前 → 向上爬」）：扫玩家 AABB footprint 各列在脚位 + 身体（脚 +1）两行
//   的方块，命中 Ladder 即真。Ladder 无碰撞（ShapeNone）→ 玩家穿入梯格占据该格，故覆盖即贴梯。取样策略同
//   hasGroundBelowAt / overlapSubAABBs（footprint 全列严格覆盖）；Y 取脚位行（floor(m_pos.y)）+ 身体行（+1）覆盖
//   玩家 1.8 高 AABB 的两格。只读 World::blockAt（向下依赖）；无世界 → false。
bool PlayerController::onLadder() const
{
    if (!m_world) return false;
    const int by0 = int(std::floor(m_pos.y()));           // 脚位行
    const int by1 = by0 + 1;                              // 身体行（玩家 1.8 高 AABB 上格）
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int zz = z0; zz <= z1; ++zz)
        for (int xx = x0; xx <= x1; ++xx) {
            if (m_world->blockAt(xx, by0, zz) == BlockRegistry::Ladder) return true;
            if (m_world->blockAt(xx, by1, zz) == BlockRegistry::Ladder) return true;
        }
    return false;
}

// t539 面向梯子判定（spec「侧身经过不爬、直接穿过」）：复用 onLadder 的 footprint 扫描，对命中的每格梯子解码
//   其 state[1:0] 支撑墙方向（BlockRegistry::ladderSupportOffset）→ 单位墙向 (wdx,wdz)；判定玩家水平移动意图
//   wish 与墙向的点积是否超过 kLadderFaceDot。任一被覆盖的梯格满足「朝墙走」即真（多格覆盖时只要有一面梯子被
//   面向即可爬）。wish 由 wishHoriz() 算（含 W/S/A/D + 视角 yaw 合成的世界向），非零归一化 → 点积即 cos 夹角。
//   kLadderFaceDot ≈ 0.5（≈60° 锥）：wish 主分量须朝墙才爬；纯前后 / 侧身擦过（dot≤0.5）判为「不面向」→ 不爬、
//   直接穿过（仅 onLadder 的悬挂 / 缓降仍生效，托住不掉）。无世界 / 无被覆盖梯格 → false。
bool PlayerController::facingLadder() const
{
    if (!m_world) return false;
    const QVector3D wish = wishHoriz();                  // 玩家水平移动意图（世界向，归一化；全松键 = 0）
    // 全松键：无水平移动意图 → 既非「面向」也非「侧身走」，不构成爬升（爬升须有向上意图：按前 / 空格，
    //   按前已在 wish 中；空格由调用端 facingLadder() 外的 space 短路覆盖 → 此处 wish=0 即返回 false，不爬）。
    if (wish.lengthSquared() < 0.001f) return false;
    const int by0 = int(std::floor(m_pos.y()));           // 脚位行
    const int by1 = by0 + 1;                              // 身体行
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    const int x0 = int(std::floor(minx)), x1 = int(std::ceil(maxx)) - 1;
    const int z0 = int(std::floor(minz)), z1 = int(std::ceil(maxz)) - 1;
    for (int zz = z0; zz <= z1; ++zz)
        for (int xx = x0; xx <= x1; ++xx) {
            for (int yy = by0; yy <= by1; ++yy) {
                if (m_world->blockAt(xx, yy, zz) != BlockRegistry::Ladder) continue;
                int wdx = 0, wdz = 0;                     // 该梯格支撑墙所在水平方向（单位向）
                BlockRegistry::ladderSupportOffset(m_world->stateAt(xx, yy, zz), wdx, wdz);
                // wish·wallDir > kLadderFaceDot = 朝墙走（面向梯子）→ 可爬。
                if (wish.x() * float(wdx) + wish.z() * float(wdz) > kLadderFaceDot) return true;
            }
        }
    return false;
}

bool PlayerController::aabbHitsSolid() const
{
    // t146：委托 overlapSubAABBs（axis<0 仅判命中）。逐格逐 sub-AABB 测试 → 不完整方块精确碰撞
    //   （下半砖只在 y[0,0.5] 挡玩家，上半空气可穿过）。火把 shape=ShapeNone → 无 sub-AABB → 不挡。
    return overlapSubAABBs(-1, nullptr, nullptr);
}

// t559 站起可行性：以「站起 AABB（kHeight=1.8，脚底 m_pos.y 起）」在当前位置做实体重叠测试（碰撞皮肤内缩
//   同 overlapSubAABBs，与真正站起后 aabbHitsSolid 的结果一致）。松 shift 站起判定 + 自动蹲每 tick 复探用它：
//   头顶（1.8 高度内）有任一实体（含下半砖 / 低顶天花板）→ false（不能站，须保持蹲）；全空 → true（可站）。
//   只读 World（collisionAABBsAt，向下依赖）；无世界 → true（宽松可站）。
bool PlayerController::canStandUp() const
{
    if (!m_world) return true;
    const float sk = kCollisionSkin;
    const float fminx = m_pos.x() - kHalfW, fmaxx = m_pos.x() + kHalfW;
    const float fminy = m_pos.y(),           fmaxy = m_pos.y() + kHeight;
    const float fminz = m_pos.z() - kHalfW,  fmaxz = m_pos.z() + kHalfW;
    const float minx = fminx + sk, maxx = fmaxx - sk;
    const float miny = fminy + sk, maxy = fmaxy - sk;
    const float minz = fminz + sk, maxz = fmaxz - sk;
    const int x0 = int(std::floor(fminx)), x1 = int(std::ceil(fmaxx)) - 1;
    const int y0 = int(std::floor(fminy)), y1 = int(std::ceil(fmaxy)) - 1;
    const int z0 = int(std::floor(fminz)), z1 = int(std::ceil(fmaxz)) - 1;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(x, y, z))
                    if (minx < b.maxX && maxx > b.minX &&
                        miny < b.maxY && maxy > b.minY &&
                        minz < b.maxZ && maxz > b.minZ)
                        return false; // 3 轴严格重叠（同 overlapSubAABBs 判据）→ 站不下
    return true;
}

// t559 自动攀爬抬升量（auto-step lift）：扫描当前 footprint（±kHalfW）在 [脚底, 脚底+kAutoStepMax] 高度带内
//   的所有方块 sub-AABB，取「最高可迈表面顶」（b.maxY ∈ (脚底, 脚底+kAutoStepMax]，footprint 重叠）。
//   返回 = 该顶 − 脚底（>0 可自动攀爬）；无合格障碍 → 0（不自动爬，交玩家跳）。
//   与旧固定 0.55 抬升的区别：精确到障碍顶 → 半砖（顶 0.5）抬 0.5、蹲态（1.5 高）在 1.5 通道里恰好贴天花板
//   （抬 0.5 + 蹲 1.5 = 2.0 与天花板底 2.0 贴平，靠 kCollisionSkin 内缩吸收 eps → 可过；旧 0.55 顶头失败）。
//   整块（顶 1.0 > 0.6）不在扫描带内 → 返回 0（须跳，机制等价 MC auto-step 只对 ≤0.5 台阶生效）。
// t581 修（t559 回归根因）：footprint 重叠判定改为「外扩 kStepProbe 容差」。t559 版用严格重叠，但本函数的
//   调用前提恰是玩家已被 moveAxis 贴面 snap（X/Z 定位在障碍面外 eps=1e-4 缝上）→ footprint 与挡路障碍的
//   严格重叠**恒假** → autoStepLift 恒 0 → 睡莲 / 压力板 / 积雪层 / 下半砖 / 楼梯全部迈不上去（须跳；蹲态在
//   1.5 通道里跳又顶头 = 完全卡死）。外扩 kStepProbe=0.02（≫ snap eps，≪ kHalfW）把「正贴面被挡」的障碍
//   纳入候选；容差只放宽候选集，抬升后仍有 aabbHitsSolid（顶头还原）+ moveAxis 复走（高墙还原）两道校验，
//   不会误上 / 穿墙。侧向 2cm 内的邻近障碍即使被误纳入，bestTop 偏高也只是多抬一点，两道校验兜底。
float PlayerController::autoStepLift() const
{
    if (!m_world || !m_onGround) return 0.0f;
    const float baseY = m_pos.y();
    const float maxY = baseY + kAutoStepMax;
    const float minx = m_pos.x() - kHalfW, maxx = m_pos.x() + kHalfW;
    const float minz = m_pos.z() - kHalfW, maxz = m_pos.z() + kHalfW;
    // t581：cell 取样范围同步外扩 kStepProbe —— 下半砖 / 雪层 / 睡莲等满 footprint 薄板的 AABB 从格边界 C
    //   起，玩家被 snap 在 C-1e-4 → 原 ceil(max)-1=C-1 漏采障碍格 C（只扩重叠判定看不见盒）。两处容差同值。
    constexpr float kStepProbe = 0.02f;
    const int x0 = int(std::floor(minx - kStepProbe)), x1 = int(std::ceil(maxx + kStepProbe)) - 1;
    const int z0 = int(std::floor(minz - kStepProbe)), z1 = int(std::ceil(maxz + kStepProbe)) - 1;
    const int y0 = int(std::floor(baseY)), y1 = int(std::floor(maxY));
    float bestTop = 0.0f;
    bool found = false;
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(x, y, z)) {
                    const float top = b.maxY;
                    if (top <= baseY + 1e-3f || top > maxY) continue; // 只取「脚底之上、maxStep 内」的顶面
                    if (!(minx - kStepProbe < b.maxX && maxx + kStepProbe > b.minX &&
                          minz - kStepProbe < b.maxZ && maxz + kStepProbe > b.minZ))
                        continue; // footprint 外扩容差重叠（t581：含「正贴面被挡」的障碍；排除仅邻格远障碍）
                    if (!found || top > bestTop) { bestTop = top; found = true; }
                }
    return found ? (bestTop - baseY) : 0.0f;
}

// 沿单轴移动 amount；碰撞则贴面 + eps + 清该轴速度。无世界则自由移动。
// t51：AABB 高用 m_height（蹲下变矮 → 头顶碰撞 / 着地贴面随之变；顶头贴面按当前高度算）。
// t146：贴面到相交 sub-AABB 的实际表面（非整格边界）—— 落下半砖着地于 y=0.5，撞栅栏柱贴到柱面。
void PlayerController::moveAxis(int axis, float amount)
{
    if (amount == 0) return;
    switch (axis) {
    case 0: m_pos.setX(m_pos.x() + amount); break;
    case 1: m_pos.setY(m_pos.y() + amount); break;
    case 2: m_pos.setZ(m_pos.z() + amount); break;
    }
    if (!m_world) return;
    float minSurf = 0.f, maxSurf = 0.f;
    bool hasMax = false, hasMin = false;
    // t161 修：向下（axis==1, amount<0）着地面只取玩家移动前脚底 pyBefore ≥ 块顶 bmax 的块（地面顶），
    //   忽略沙等 pyBefore<bmax 的 bury 块（顶更高）→ maxSurfCap=pyBefore+eps 过滤。否则 overlapSubAABBs 取
    //   所有重叠块 MAX（=沙顶 11）会让 snap 无效、地面托举（顶 10）随之失效，每 tick 重力再下沉穿地坠虚空。
    //   其他方向（X/Z）cap=+inf = 取全部（旧行为）。
    // t352 修（对称 maxSurfCap）：向上（axis==1, amount>0）顶头只取块底 bmin ≥ 玩家移动前头顶 pyBefore+m_height
    //   的块（真天花板），忽略半砖阶 / 楼梯背墙 / 开活板门整高板 / 栅栏柱等「身体 / 脚位」partial 块（它们的 bmin
    //   在玩家头顶之下）→ minSurfFloor=pyBefore+m_height-eps 过滤。否则 overlapSubAABBs 取所有重叠块 MIN（=脚位
    //   partial 块底）会让 snap 把玩家猛拽到 minSurf-m_height（向下穿格）并清零 m_vel.y，吃掉合法跳跃冲量
    //   （t317 复发根因：跳跃上升期 footprint 偶发重叠身体级 partial 块 → 被当假天花板 → 速度清零 → 只跳半格卡死）。
    //   hasMin=false = 重叠均为身体级 partial（无真天花板）→ 不 snap / 不清零（保留跳跃上升），交后续 moveAxis(0/2)
    //   横向解算（贴墙 snap）。其他方向（X/Z、Y 向下）floor=-inf = 取全部（旧行为）。
    const float pyBefore = m_pos.y() - amount; // 移动前脚底 Y（Y 已按 amount 更新，减回 = 旧值）
    const float cap = (axis == 1 && amount < 0) ? (pyBefore + 1e-3f)
                                                : std::numeric_limits<float>::max();
    const float floor = (axis == 1 && amount > 0) ? (pyBefore + m_height - 1e-3f)
                                                  : -std::numeric_limits<float>::max();
    if (!overlapSubAABBs(axis, &minSurf, &maxSurf, &hasMax, cap, &hasMin, floor)) return; // 无碰撞 → 自由
    const float eps = 1e-4f;
    switch (axis) {
    case 0:
        // 向 +：玩家 max 边贴到最近 sub-AABB 的 min 面；向 -：玩家 min 边贴到最近 sub-AABB 的 max 面。
        if (amount > 0) m_pos.setX(minSurf - kHalfW - eps);
        else            m_pos.setX(maxSurf + kHalfW + eps);
        m_vel.setX(0); break;
    case 1:
        if (amount > 0) {
            // t352：仅当存在真天花板（hasMin：块底在玩家原头顶之上）才顶头 snap + 清零垂直速度。
            //   身体 / 脚位 partial 块重叠（hasMin=false）不是天花板 → 保留本次上升位移与 m_vel.y（跳跃不被吃）。
            if (hasMin) {
                m_pos.setY(minSurf - m_height - eps); // 顶头（按当前高度）
                m_vel.setY(0);
            }
        } else {
            // t161 修：maxSurf 已被 cap=pyBefore 限定为「玩家原本站其顶上」的可着陆最高面（地面顶 10，非沙顶 11）。
            //   hasMax=true → 贴顶面站住（地面托举正常生效，玩家不再逐 tick 下沉穿地）；hasMax=false = 所有重叠
            //   块顶都在 pyBefore 之上（纯 bury：沙落身上 / 侧面卡入且脚下无支撑面）→ 不 snap Y（留格内），交后续
            //   moveAxis(0/2)+extrudeEmbedded 横向推出（用户「向外挤 not 向上」）。被完全包裹（无水平出路）则由
            //   t160 窒息扣血兜底。原 t161「据 inflated maxSurf 全不 snap」会连地面托举一起失效 → 穿地坠虚空。
            if (hasMax) m_pos.setY(maxSurf + eps); // 站到可着陆最高面（地面顶，非沙顶）
            else m_pos.setY(pyBefore); // t258（Y 轴）：纯 bury（无 landable 面）→ 回退本次重力位移，防逐 tick 下沉穿地 / 坠出基岩外。水平锁定见 step() 入口 isLockedBuried（全包裹→velocity 清零→moveAxis 空转）/ extrudeEmbedded（有开放侧→挤出），只能挖出脱困（t160 窒息扣血兜底）
            m_vel.setY(0);
        }
        break;
    case 2:
        if (amount > 0) m_pos.setZ(minSurf - kHalfW - eps);
        else            m_pos.setZ(maxSurf + kHalfW + eps);
        m_vel.setZ(0); break;
    }
}

// t258 被埋锁定检测（spec「被埋→锁定不能动，只能挖出脱困」）：玩家被实体方块完全包围时 → 锁定。
//   判据与 extrudeEmbedded 的「全包裹 → 不挤」同源（见 .h 头注释）：
//   1) 中心列嵌入：玩家 XZ 中心列 (bx,bz) 在玩家全高 [y0,y1] 各格的 sub-AABB 与玩家 AABB 真重叠
//      （3 轴严格重叠）= 有方块在玩家身上 materialize（下落沙着地 / 侧面塞入）= burial。正常贴墙 /
//      站立时中心列为玩家占据的空气 → 非嵌入 → 不锁（不影响正常碰撞）。
//   2) 四向皆堵：4 向邻列在玩家全高全开放（无 collidable）= 可逃；任一开放 → 不锁（extrudeEmbedded
//      会推出去）。四向皆堵 → 锁定（无水平出路，moveAxis snap 会穿 → 必须整段跳过位移）。
//   两者皆真 = 锁定。只读 World（collisionAABBsAt / isCollidable 向下依赖）；无世界 → false。
bool PlayerController::isLockedBuried() const
{
    if (!m_world) return false;
    const float px = m_pos.x(), pz = m_pos.z();
    const int bx = int(std::floor(px));
    const int bz = int(std::floor(pz));
    const int y0 = int(std::floor(m_pos.y()));
    const int y1 = int(std::floor(m_pos.y() + m_height));
    const float minx = px - kHalfW, maxx = px + kHalfW;
    const float miny = m_pos.y(),        maxy = m_pos.y() + m_height;
    const float minz = pz - kHalfW, maxz = pz + kHalfW;
    // 1) 中心列有嵌入块（玩家 AABB 与该列某 Y 格 sub-AABB **显著**重叠）。
    //   t289 修：原 hairline 重叠（任何 ε 接触即算嵌入）致玩家 Y 贴方块边界时（站立 / 落地 snapping 的
    //   FP 残差）误判嵌入 → 恰在窄处（四向堵）触发 isLockedBuried → velocity 清零 → WASD / 空格失效
    //   （移动锁定，切观察者飞态不经此门控故可动 = 用户报告症状）。改：玩家 AABB 内缩 kEmbedTol 后再测
    //   → 仅「显著嵌入」（深度 >0.1）才算，排除边界 FP。真被埋（沙落身 / 卡进墙）仍显著嵌入 → 仍锁。
    constexpr float kEmbedTol = 0.1f;
    bool embedded = false;
    for (int y = y0; y <= y1 && !embedded; ++y) {
        for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(bx, y, bz)) {
            if (minx + kEmbedTol < b.maxX && maxx - kEmbedTol > b.minX &&
                miny + kEmbedTol < b.maxY && maxy - kEmbedTol > b.minY &&
                minz + kEmbedTol < b.maxZ && maxz - kEmbedTol > b.minZ) { embedded = true; break; }
        }
    }
    if (!embedded) return false; // 非嵌入（正常站立 / 贴墙）→ 不锁
    // 2) 四向邻列全高开放判定（与 extrudeEmbedded 的 columnClear 同源）：有任一侧开放 → 可挤出，不锁。
    const auto columnClear = [&](int cx, int cz) -> bool {
        for (int y = y0; y <= y1; ++y)
            if (m_world->isCollidable(cx, y, cz)) return false;
        return true;
    };
    if (columnClear(bx + 1, bz) || columnClear(bx - 1, bz) ||
        columnClear(bx, bz + 1) || columnClear(bx, bz - 1))
        return false; // 有开放侧 → 可挤出（extrudeEmbedded 推出去），不锁
    return true; // 嵌入 + 四向皆堵 = 锁定
}

// t161 嵌入挤出：见 .h 注释。补充实现要点——
//   1) 嵌入块定位：扫玩家 XZ 中心列 (bx,bz) 在玩家全高 [y0,y1] 各格的 sub-AABB，找到第一个与玩家 AABB
//      真重叠（3 轴严格重叠）的实体格 embY。中心列是玩家正常占据的「空气柱」，凡该柱出现重叠实体 = 有
//      方块在玩家身上 materialize（下落沙着地 / 侧面塞入）= burial；正常贴墙时墙在玩家前导边、中心列仍
//      空气 → embY<0 不触发。
//   2) 逃向选择：4 向邻列（bx±1 / bz±1）在玩家全高全开放（无 collidable）= 可逃；取玩家中心距嵌入块该
//      侧边界最近的一向（最少位移）。把玩家整体推到该侧嵌入块面之外（footprint 完全进入开放邻列）。
//   3) 全包裹（4 向皆堵）→ 无可逃向 → 不动，交 t160 窒息扣血兜底（spec「被完全包裹」语义）。
//   注：仅改 XZ、不动 Y / 速度 → 与 moveAxis(1) 不上抬（fab580e）正交协同：Y 留格内待此处横向清出。
void PlayerController::extrudeEmbedded()
{
    if (!m_world) return;
    const float px = m_pos.x(), pz = m_pos.z();
    const int bx = int(std::floor(px));
    const int bz = int(std::floor(pz));
    const int y0 = int(std::floor(m_pos.y()));
    const int y1 = int(std::floor(m_pos.y() + m_height));
    const float minx = px - kHalfW, maxx = px + kHalfW;
    const float miny = m_pos.y(),        maxy = m_pos.y() + m_height;
    const float minz = pz - kHalfW, maxz = pz + kHalfW;
    // t355 修：嵌入检测用与 isLockedBuried（t289）相同的 kEmbedTol 内缩。旧版用裸「任意 ε 接触即重叠」，
    //   当玩家 m_pos.y 贴方块整数边界（站立 / 落地 snapping 的 FP 残差，如 m_pos.y=64.9999 使 floor() 落到
    //   脚下方块格）会把「玩家站其上的地面方块」误判为中心列嵌入 → extrudeEmbedded 把玩家横向推到邻列 =
    //   站立 / 走动时偶发瞬移（同高邻列通常也是地面 → 推回 → 来回抖 = 用户报告的单机 rubberband）。内缩 0.1
    //   后仅「显著嵌入」（穿透 >0.1：下落沙着地 / 侧面塞入 / 卡进墙）才触发推出，与 isLockedBuried 一致；
    //   hairline / 边界 FP 不触发 → 无瞬移。真嵌入仍深 >0.1 → 仍被正常推出（本职不破）。
    constexpr float kEmbedTol = 0.1f; // 须与 isLockedBuried 同值（边界 FP 阈一致；改须两处同步）
    // 1) 找嵌入块（中心列上某 Y 格**显著**重叠，内缩 kEmbedTol 排除边界 FP）。
    int embY = -1;
    for (int y = y0; y <= y1 && embY < 0; ++y) {
        for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(bx, y, bz)) {
            if (minx + kEmbedTol < b.maxX && maxx - kEmbedTol > b.minX &&
                miny + kEmbedTol < b.maxY && maxy - kEmbedTol > b.minY &&
                minz + kEmbedTol < b.maxZ && maxz - kEmbedTol > b.minZ) { embY = y; break; }
        }
    }
    if (embY < 0) return; // 中心列为空气 / 仅 hairline 接触（玩家正常占据）→ 非嵌入，不干预
    // 2) 邻列全高开放判定（玩家能整身进入才算可逃，避免挤进半堵列又被卡）。
    const auto columnClear = [&](int cx, int cz) -> bool {
        for (int y = y0; y <= y1; ++y)
            if (m_world->isCollidable(cx, y, cz)) return false;
        return true;
    };
    // 3) 取最近开放侧（玩家中心距嵌入块该侧边界最近 = 位移最小）。
    const float toPX = (bx + 1) - px, toMX = px - bx; // 到嵌入块 ±X 面距离
    const float toPZ = (bz + 1) - pz, toMZ = pz - bz; // 到嵌入块 ±Z 面距离
    int bestAxis = -1, bestDir = 0; float bestDist = 1e9f;
    const auto consider = [&](int axis, int dir, float d) {
        if (d >= bestDist) return;
        const int cx = bx + (axis == 0 ? dir : 0);
        const int cz = bz + (axis == 2 ? dir : 0);
        if (!columnClear(cx, cz)) return; // 该侧堵 → 跳过
        bestAxis = axis; bestDir = dir; bestDist = d;
    };
    consider(0,  1, toPX); consider(0, -1, toMX);
    consider(2,  1, toPZ); consider(2, -1, toMZ);
    if (bestAxis < 0) return; // 全包裹 → 不挤（t160 窒息兜底）
    const float eps = 1e-3f;
    if (bestAxis == 0)
        m_pos.setX(bestDir > 0 ? (bx + 1) + kHalfW + eps : bx - kHalfW - eps); // 出 ±X 面，footprint 入开放邻列
    else
        m_pos.setZ(bestDir > 0 ? (bz + 1) + kHalfW + eps : bz - kHalfW - eps);
}

// t712 批「被击飞插沙」修复（见头文件注释）：击飞窗口内嵌入 → 向上排出。判据与 extrudeEmbedded 同源
//   （中心列 kEmbedTol 内缩真重叠）；嵌入块 = 重叠的那格，目标位 = 该格顶面（cellY+1）——上方须能容玩家
//   全高（AABB at 顶面 无碰撞）才抬（塞不进就不抬，保持既有挖出脱困路径）。抬升后 vy 清零（防残余下坠
//   速度立刻再嵌一格）。
void PlayerController::launchUnburyUpward()
{
    if (m_golemLaunchTimer <= 0.0f) return; // 仅击飞窗口内（t258 常规被埋语义不破坏）
    if (!m_world) return;
    const float px = m_pos.x(), pz = m_pos.z();
    const int bx = int(std::floor(px));
    const int bz = int(std::floor(pz));
    const int y0 = int(std::floor(m_pos.y()));
    const int y1 = int(std::floor(m_pos.y() + m_height));
    const float minx = px - kHalfW, maxx = px + kHalfW;
    const float miny = m_pos.y(),        maxy = m_pos.y() + m_height;
    const float minz = pz - kHalfW, maxz = pz + kHalfW;
    constexpr float kEmbedTol = 0.1f; // 与 extrudeEmbedded / isLockedBuried 同值（边界 FP 阈一致，改须三处同步）
    // 找中心列嵌入块（同 extrudeEmbedded 步骤 1）。
    int embY = -1;
    for (int y = y0; y <= y1 && embY < 0; ++y) {
        for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(bx, y, bz)) {
            if (minx + kEmbedTol < b.maxX && maxx - kEmbedTol > b.minX &&
                miny + kEmbedTol < b.maxY && maxy - kEmbedTol > b.minY &&
                minz + kEmbedTol < b.maxZ && maxz - kEmbedTol > b.minZ) { embY = y; break; }
        }
    }
    if (embY < 0) return; // 无显著嵌入 → 不干预
    // 嵌入块顶面（完整格顶 = embY+1；partial 块取其 sub-AABB 最高 maxY，落沙 / 沙块为整格）。
    float topY = float(embY) + 1.0f;
    for (const BlockRegistry::BlockAABB &b : m_world->collisionAABBsAt(bx, embY, bz))
        if (b.maxY > topY) topY = b.maxY;
    // 顶面上方须容得下玩家全高（抬过去不顶头 / 不再嵌）才抬。
    const float savedY = m_pos.y();
    m_pos.setY(topY + 1e-3f);
    if (aabbHitsSolid()) { m_pos.setY(savedY); return; } // 头顶容不下 → 不抬（保持既有水平挤出 / 挖出路径）
    m_vel.setY(0.0f); // 清残余下坠速度（防下一 tick 立刻再嵌）
    qInfo("player golem-launch unburied upward to y=%.2f (embedded cell y=%d)", topY, embY);
}

void PlayerController::step(qreal dt)
{
    const QVector3D posBefore = m_pos; // t159：出口算实际水平速度（speed 属性）的位移基准
    const QVector3D wish = wishHoriz();
    const bool space = m_keys.value(Qt::Key_Space), shift = m_keys.value(Qt::Key_Shift);
    const bool spaceEdge = space && !m_spacePrev; // 跳跃边沿：长按只跳一次（生存/创造-走路统一）
    m_spacePrev = space;
    const bool shiftEdge = shift && !m_shiftPrev; // t469 下船边沿：骑乘期 Shift 按下沿 → dismount（长按只一次）
    m_shiftPrev = shift;

    // t469 船骑乘分支（spec「骑乘时禁用玩家自身移动，由船位移带动玩家」；机制等价 MC 1.0 船骑乘）。
    //   优先于 Spectator / Creative-飞 / 走路分支 —— 骑乘期一律走船物理（无视走路 / 飞行物理，机制等价 MC 骑船）。
    //   Shift 按下沿 → 下船（dismount，玩家摆船侧安全位）；WASD（wish）驱动船（tickRiddenBoat：水上快移 + 冰面加速）；
    //   高速撞硬墙 → 撞毁（breakRiddenBoat 移除船 + emit boatBroken 掉船物品），玩家摆末位船位（不坠虚空）。
    //   玩家脚底 = 船中心（坐船舱），眼位 / 相机自动跟随 position()。骑乘期禁重力 / 跳跃 / 自身移动（船浮水 Y 钉水面）。
    if (m_boatManager && m_boatManager->ridingIndex() >= 0) {
        // t556「坐船不禁走路动画（划船时腿手还在动）」（用户报⑤）：坐船时**禁用走路动画** —— 旧版按 WASD
        //   （wish 非零）把 m_moveSpeed 设 kWalk + 推进 walkPhase → QML walkBlend=1 → 腿手持续摆（划船还摆动 =
        //   MC 船骑乘不该有的走步动画）。改：骑乘期强制 m_moveSpeed=0 + 不推进 walkPhase → walkBlend=0 → 四肢
        //   归中性位；坐姿（sitBlend=1）由 QML 独立驱动（大腿水平前伸 + 小腿竖直下垂 = 坐姿），与行走动画解耦。
        //   脚步音同理（walkPhase 不动 → 不触发），骑乘无走步声。
        if (m_moveSpeed != 0.0f) { m_moveSpeed = 0.0f; emit moveSpeedChanged(); }
        if (shiftEdge) {
            // 下船：dismount 清骑乘态 + 玩家摆船侧安全位。
            QVector3D feet;
            m_boatManager->dismount(m_world, feet);
            m_pos = feet;
            m_vel = QVector3D(0, 0, 0);
            if (m_onGround) { m_onGround = false; emit onGroundChanged(); }
            reportHorizSpeed(posBefore, dt);
            emit positionChanged();
            return;
        }
        // WASD 驱动船。
        QVector3D boatPos; bool crashed = false;
        m_boatManager->tickRiddenBoat(dt, m_world, wish.x(), wish.z(), boatPos, crashed);
        if (crashed) {
            // 高速撞硬墙 → 船损坏（breakRiddenBoat 移除船 + emit boatBroken → 呈层 spawnItem 掉船物品）。
            m_boatManager->breakRiddenBoat();
            m_pos = QVector3D(boatPos.x(), boatPos.y(), boatPos.z()); // 玩家留撞击点（下 tick 回正常重力物理）
            m_vel = QVector3D(0, 0, 0);
            if (m_onGround) { m_onGround = false; emit onGroundChanged(); }
            reportHorizSpeed(posBefore, dt);
            emit positionChanged();
            return;
        }
        // 玩家随船位移（脚底 = 船中心）。
        m_pos = QVector3D(boatPos.x(), boatPos.y(), boatPos.z());
        m_vel = QVector3D(0, 0, 0);
        // review L11：骑船期自动蹲复探（镜像走路分支末尾的同款复探）。低顶区（桥洞 / 悬崖下）蹲上船后松
        //   shift 被站起闸门拒（dismount 分支外的松 shift 走 setKey 集中闸门 → m_autoCrouch=true），旧版骑乘
        //   分支早 return 不复探 → 驶入开阔水面仍保持蹲（眼位 1.35）直到下船。骑乘期玩家位置 = 船位（已同步
        //   到 m_pos），canStandUp 以 m_pos 起算 1.8 AABB 头顶空间 —— 开阔水面恒可站 → 自动站起；桥洞下仍
        //   不可站 → 保持蹲（不把 1.8 AABB 塞进低顶）。setMoveState(Walk) 复位 AABB 高 / 眼位，同走路分支。
        if (m_autoCrouch && m_moveState == Crouch && canStandUp()) {
            m_autoCrouch = false;
            setMoveState(Walk);
        }
        reportHorizSpeed(posBefore, dt);
        emit positionChanged();
        return;
    }
    // t565 矿车骑乘分支（机制等价 MC 1.0 矿车骑乘；同船骑乘分支模式，优先于走 / 飞 / 观察者分支）。
    //   骑乘期禁用玩家自身移动（重力 / 跳跃 / 走步动画全停，坐姿由 QML sitBlend 驱动）；WASD 经
    //   tickRiddenCart 沿轨推进（W 前推 / S 后拉；轨拐角自动转弯；轨尽头停 + 可反向推回）；Shift 按下沿
    //   下车（dismount，玩家摆矿车侧安全位）。玩家脚底 = 矿车中心 - 车高差（坐车斗内）。
    if (m_minecartManager && m_minecartManager->ridingIndex() >= 0) {
        if (m_moveSpeed != 0.0f) { m_moveSpeed = 0.0f; emit moveSpeedChanged(); } // 禁走路动画（同船）
        if (shiftEdge) {
            QVector3D feet;
            m_minecartManager->dismount(m_world, feet);
            m_pos = feet;
            m_vel = QVector3D(0, 0, 0);
            if (m_onGround) { m_onGround = false; emit onGroundChanged(); }
            reportHorizSpeed(posBefore, dt);
            emit positionChanged();
            return;
        }
        // WASD 驱动矿车（wish 是据 yaw 的世界系意图向量；tickRiddenCart 在轨约束下投影到行进方向）。
        QVector3D cartPos;
        m_minecartManager->tickRiddenCart(dt, m_world, wish.x(), wish.z(), cartPos);
        // 玩家随矿车位移（脚底 = 矿车中心下移 0.3 —— 坐进车斗；眼位 / 相机自动跟随 position()）。
        m_pos = QVector3D(cartPos.x(), cartPos.y() - 0.3f, cartPos.z());
        m_vel = QVector3D(0, 0, 0);
        reportHorizSpeed(posBefore, dt);
        emit positionChanged();
        return;
    }
    // t708 ③④ 空矿车滑行 / 被推（骑乘分支之外全局推进）：每帧推进未被骑的空矿车滑行（pushEmptyCart 推动
    //   / 下坡顺坡溜 → 磨擦渐停 / 出轨死端停）；走路 / 飞行 / 观察者期间玩家脚底与静止空车重叠 + wish 沿
    //   轨轴 → 把车推走（车无碰撞盒，推走即让出，不阻断玩家行走）。骑乘分支已早 return（被骑的走
    //   tickRiddenCart，其它空车此刻暂不滑 —— t708 范围外：骑乘态空车停驻可接受）。
    if (m_minecartManager) {
        m_minecartManager->tickPushedCarts(dt, m_world);
        m_minecartManager->pushEmptyCart(m_world, m_pos, wish.x(), wish.z());
    }
    // t159 水下速度倍数：眼位在水格 → 水平（及飞垂直）速度 ×kUnderwaterSpeedMul（~0.4）。所有模式统一适用
    //   （走 / 飞 / 观察者进水都变慢，机制等价 MC 水中减速）。每 tick 查一次（blockAt 单查，廉价）。
    const float waterMul = eyeInWater() ? kUnderwaterSpeedMul : 1.0f;

    // 行走动画驱动（t45）：moveSpeed 仅走路模式（Survival / Creative-未飞）按住 WASD 时非零；
    //   Spectator / Creative-飞 → 0（飞行/幽灵态无走步动画，spec 未要求；为未来泳/飞姿留接口）。
    //   walkPhase 仅在走时累加（speed*dt*kStrideRate），2π 回绕；静止不累加 → QML 据此 sin*0=0 中性位。
    //   「按住 WASD 撞墙」时 wish 仍非零（玩家在「尝试」走）→ 腿仍摆，对齐 MC 行为。
    //   t51：moveSpeed 乘 speedMul（疾跑 ×1.3 / 蹲 ×0.4）→ walkPhase 推进速率随状态变（动画频率）。
    //   t159：moveSpeed 不乘 waterMul（动画驱动用「意图」速度；水下减速是位移层，与四肢摆频解耦）。
    //   分层：动画驱动数据由 Game/Physics tick 算出，QML 呈现层只读消费（同 swingArm 模式）。
    {
        const bool moving = wish.lengthSquared() > 0.001f; // wish 已 normalize：非零即有 WASD 输入
        const float walk = (moving && (m_mode == Survival || (m_mode == Creative && !m_flying)))
                         ? kWalk * speedMul() : 0.0f;
        if (walk != m_moveSpeed) { m_moveSpeed = walk; emit moveSpeedChanged(); }
        // 相位推进（仅走时；走时每 tick 发 walkPhaseChanged 供 QML 重算四肢欧拉角，~60Hz）。
        if (m_moveSpeed > 0.1f) {
            m_walkPhase += m_moveSpeed * float(dt) * kStrideRate;
            if (m_walkPhase >= 6.28318530718f) m_walkPhase -= 6.28318530718f; // 2π 回绕
            emit walkPhaseChanged();
        }
    }

    if (m_mode == Spectator) {
        // t159：飞态统一用 flySpeed()（滚轮可调）× waterMul；spectator 常驻飞亦适用（spec「创造/观察者」）。
        const float fs = flySpeed() * waterMul;
        QVector3D v = wish * fs;
        if (space) v.setY(fs);
        if (shift) v.setY(-fs);
        m_pos += v * dt; // noclip
        reportHorizSpeed(posBefore, dt); // t159：speed 属性上报
        emit positionChanged();
        return;
    }

    if (m_mode == Creative && m_flying) {
        // t159：飞态统一用 flySpeed()（滚轮可调）× waterMul（原水平基 kWalk 改 kFly 基，与 spectator 同旋钮；
        //   默认 mul=1.0 → kFly=8，比旧 kWalk=4.3 快但更贴近 MC 创造飞，且滚轮可在 4..20 调）。
        const float fs = flySpeed() * waterMul;
        QVector3D v = wish * fs;
        if (space) v.setY(fs);
        if (shift) v.setY(-fs);
        // t208 防穿隧道（薄板 / 门）：旧 creative-fly 无子步（unlike walk 路径的 ≤0.4/子步）→ 滚轮加速到 16+
        //   blocks/sec 叠卡顿（dt 钳到 0.05）时单次 moveAxis 位移可达 0.8+，一步跨过薄障碍（门 3/16 板穿隧道
        //   阈值仅 0.7875 = 板厚 0.1875 + 玩家宽 0.6）。门是游戏内最薄的可放置障碍 → 最先被穿（整立方墙阈值
        //   1.6，飞 20×0.05=1.0 也穿不过）。补子步（同 walk：任意轴单步 ≤0.4 格）→ 子步位移 < 玩家宽 0.6 →
        //   必与路径上任何障碍重叠被检出。lessons-learned「dt 钳制 + 子步防穿墙」同源。
        QVector3D delta = v * float(dt);
        const float md = std::max({std::fabs(delta.x()), std::fabs(delta.y()), std::fabs(delta.z())});
        const int sub = std::max(1, int(std::ceil(md / 0.4f)));
        delta /= float(sub);
        for (int i = 0; i < sub; ++i) {
            moveAxis(0, delta.x()); // 碰撞，无重力
            moveAxis(2, delta.z());
            moveAxis(1, delta.y());
        }
        // review M6（belt-and-braces）：飞行与走路同走 moveAxis（有碰撞）→ 也可能被嵌入（下落沙 /
        //   worldgen / 系统放置 materialize 在玩家身上）。旧版飞行分支无 extrudeEmbedded 自救（早
        //   return）→ 一旦嵌入（如低顶区起飞 / 飞行中被落沙掩埋）全向位移被 snap 锁死无法脱困。
        //   同走路分支在逐轴解算后调用（同一 helper、同一模式，正常飞行嵌入 <kEmbedTol 早退零开销）。
        extrudeEmbedded();
        // review D1-c 低顶区起飞后的自动蹲复探（镜像走路分支末尾 / L11 船骑乘分支的同款复探）：7cd3b6b 收紧
        //   站起闸门后，蹲在 1.5 格通道双击空格起飞被拒（保持蹲 + m_autoCrouch=true 等复探）。旧版飞行分支
        //   早 return 不复探 → 飞出通道到开阔区仍蹲着飞（蹲位眼高）直到落地走路才自动站。同走路分支：飞到
        //   头顶有站起空间处（canStandUp）即自动站起 —— 飞行走 moveAxis 有碰撞，站起不会嵌入（1.8 AABB 放进
        //   开阔空间安全）；仍在低顶（桥洞 / 洞穴飞行）→ 保持蹲（闸门语义不破）。setMoveState(Walk) 复位 AABB
        //   高 / 眼位（闸门内已 canStandUp 验证，不会二次拒）。
        if (m_autoCrouch && m_moveState == Crouch && canStandUp()) {
            m_autoCrouch = false;
            setMoveState(Walk);
        }
        // t540 创造飞行长按 shift 触地切步行（spec「飞行时长按 shift，应落地后立即切步行模式，得重新按两下
        //   空格才能再飞」）。机制等价 MC 1.0 创造飞：按住 shift 下降，触地瞬间自动退出飞态回步行。旧实现只在
        //   「shift 按下瞬间」处理（setKey 蹲分支 canCrouch 守卫挡了飞态），飞态 shift 永远是「下降」语义 → 玩家
        //   长按 shift 贴地滑行仍是飞态（m_onGround 还被本块强制 false），只有双击空格才下来，与 spec 不符。
        //   修：飞态 + 持续按 shift（下降意图）+ 脚底贴地（同走路地面复探：oy-0.05 与实体重叠）→ 退出飞态
        //   （m_flying=false + emit flyingChanged + 清 m_vel.y 防陈旧下坠）+ 标记 onGround（落地）。退飞后 moveState
        //   已是 Walk（起飞时 setKey 行 201 清过），故无需再 setMoveState。松 shift / 飞在空中 → 不触发（保持飞态）。
        //   分层（PLAN §2-D）：判据用 step() 帧内现采的 shift（m_keys 翻译态，单一输入路径）+ 物理层 aabbHitsSolid。
        if (shift && delta.y() < 0.0f) {
            const float oy = m_pos.y();
            m_pos.setY(oy - 0.05f);
            const bool landed = aabbHitsSolid(); // 脚底下方 0.05 有实体 = 着地（同走路地面复探）
            m_pos.setY(oy);
            if (landed) {
                m_flying = false;
                m_vel.setY(0); // 清下坠余速，防退飞后走路路径重力叠加穿地
                if (!m_onGround) { m_onGround = true; emit onGroundChanged(); }
                // rv-low-batch1 修「飞行落地 shift 仍按住却不进蹲」：退飞时 canCrouch 翻 true，但 shift 按下沿
                //   （setKey 蹲分支）在飞态已被 canCrouch 守卫拦下 → shift 持续按住不再有新按下沿 → moveState 停在
                //   Walk，玩家须松开重按 shift 才蹲。修：退飞帧等效补一次「shift 仍按住 → 进蹲」（直接设 moveState，
                //   不走 setKey 免重复写 m_keys；canStandUp 由后续松 shift 的正常路径处理）。同 setKey 蹲分支语义。
                if (shift && m_moveState == Walk) { m_autoCrouch = false; setMoveState(Crouch); }
                emit flyingChanged();
                reportHorizSpeed(posBefore, dt);
                emit positionChanged();
                return; // 退飞 → 后续不再走飞态 early return（本块已是飞态分支末尾），交下帧走路路径
            }
        }
        if (m_onGround) { m_onGround = false; emit onGroundChanged(); }
        reportHorizSpeed(posBefore, dt); // t159：speed 属性上报
        emit positionChanged();
        return;
    }

    // 走（生存 / 创造-未飞）：重力 + 跳跃 + 逐轴解算
    // t51：水平速度乘 speedMul（疾跑 ×1.3 / 蹲 ×0.4 / 走 ×1.0）；spec「疾跑移速 +、蹲下 -」。
    //   双击 W 疾跑（setKey 内 m_lastWms 双击窗 ≤250ms 进 Sprint）→ speedMul()=1.3 实际乘入此处水平速度，
    //   走速 4.3 → 疾跑 5.59 blocks/sec（同 MC 1.0 系数）。t159：再乘 waterMul（水下减速）。
    // t304 拉弓减速（spec「拉弓减速（叠 shift）」）：m_bowDrawing 时水平速度再 ×kBowSlowMul=0.5（与蹲下
    //   ×0.4 叠加 → 蹲拉弓 = 走速×0.4×0.5=0.2，机制等价 MC 1.0 拉弓大幅减速）。仅走路模式（飞态早 return）。
    const float bowMul = m_bowDrawing ? kBowSlowMul : 1.0f;
    // t565 蛛网粘滞（rv-low-batch2 补实现，见 inCobweb 头注释）：玩家 footprint 在蛛网内 → 目标水平速度再乘
    //   kCobwebSpeedMul(0.15)，同 waterMul 乘入模式（机制等价 MC 1.0 cobweb 粘滞：进网水平速度大减 → 贴网
    //   挣扎挪动；蛛网无碰撞 → 玩家低速穿过网格）。仅走路模式生效（飞 / 观察者分支已 early return）。
    const float webMul = inCobweb() ? kCobwebSpeedMul : 1.0f;
    // t715 缓慢效果减速（StatusEffect::EffectSlowness，机制等价 MC 1.0 slowness 按等级降移速；v1 等级 1
    //   ×kSlowSpeedMul=0.85）：m_slowTimer>0 时水平速度再乘此倍数（与蹲 / 水下 / 拉弓 / 蛛网同乘入模式叠加）。
    //   仅 Survival 有缓慢（applyStatusEffect 门控）；飞 / 观察者分支已 early return 不受影响。
    const float slowMul = (m_slowTimer > 0.0f) ? kSlowSpeedMul : 1.0f;
    // t468 冰上滑动（spec「冰面摩擦力极低→玩家移动加速滑；松键后惯性继续滑一段才停」）。机制等价 MC 1.0 冰滑行：
    //   非冰地面 → 瞬时设速（旧手感：松键即停）；冰面 → 水平速度向「目标速度」做指数接近（1 - exp(-rate*dt)），
    //   rate = iceSlipApproach（Ice 中等 / PackIce 更滑 / BlueIce 最滑）。松键时 wish=0 → 目标=0 → 速度按同 rate
    //   衰减 → 冰上明显惯性滑行（BlueIce 滑得最远）。帧率无关（exp(-rate*dt)）。仅走路模式（飞态已 early return）。
    //   水中（feetInWater）不走冰滑行（水中已减速 + 浮力，无冰面；waterMul 仍乘入目标速度）。
    const float targetVx = wish.x() * kWalk * speedMul() * waterMul * bowMul * webMul * slowMul;
    const float targetVz = wish.z() * kWalk * speedMul() * waterMul * bowMul * webMul * slowMul;
    if (onIce() && !feetInWater()) {
        const quint8 iceBlk = m_world->blockAt(int(std::floor(m_pos.x())),
                                                int(std::floor(m_pos.y())) - 1,
                                                int(std::floor(m_pos.z())));
        const float rate = BlockRegistry::iceSlipApproach(iceBlk); // 1/s（>0 即冰族；越小越滑）
        const float alpha = 1.0f - std::exp(-rate * float(dt));    // 本 tick 接近比例（帧率无关）
        m_vel.setX(m_vel.x() + (targetVx - m_vel.x()) * alpha);
        m_vel.setZ(m_vel.z() + (targetVz - m_vel.z()) * alpha);
    } else {
        m_vel.setX(targetVx); // 常规地面：瞬时设速（松键即停，旧手感）
        m_vel.setZ(targetVz);
    }
    // t174 水中浮力 / 游泳（spec「浮力/游泳」）：脚位在水格 → 缓沉（kWaterGravity << kGravity）+ 按住空格
    //   上浮（kSwimUp，连续非边沿）+ 钳最大下沉（防穿水底）。机制等价 MC 1.0 水中：减速 + 浮力 + 空格上浮。
    //   离水（脚位非水）走原重力 + 跳跃（spaceEdge && onGround）。waterMul 已乘水平速度（眼位在水中减速）。
    if (feetInWater()) {
        // t211 水流推动玩家（spec「创造非飞 + 生存，玩家在流水中被水流沿流动方向水平推动」）：
        //   脚位在流水格（state>0；水源 state=0 静止不推，spec）→ 沿「离源方向」叠入水平推力。流向据脚位
        //   4 向邻居 state 梯度推算：state 低于脚位的邻居 = 近源方向 → 推力朝远离它（离源 = 高 state 远源
        //   方向）。梯度加权（footState − ns）使陡降（近源 → 远源跨多级）推得更猛；归一化后 ×kWaterFlowPush
        //   叠入 m_vel.x/z（与玩家 wish 输入相加 → 逆流游净速 = 走速 − 推力，松手则被流走）。无梯度（四面无
        //   更近源邻居，如水源/对称流）→ 不推。仅走路模式生效（Spectator / Creative-飞 已 early return）。
        //   悬崖边落水额外向下带：脚位下方为空气 = 水柱下落（瀑布）→ 下沉上限抬到 kWaterfallSinkMax。
        //   t270 推力增强：kWaterFlowPush 由 2.5 提升到 4.0（见 .h 注释）—— 机制（梯度方向 + 每 tick 叠入 +
        //     仅流水格 state>0 + 仅走路模式）完全不变，仅幅值增强使水流「持续外推」可被明显感知。
        const int fx = int(std::floor(m_pos.x()));
        const int fy = int(std::floor(m_pos.y()));
        const int fz = int(std::floor(m_pos.z()));
        const quint8 footState = m_world->stateAt(fx, fy, fz);
        if (footState > 0) { // 流水格才推（水源 state=0 不推，spec）
            float gx = 0.0f, gz = 0.0f;
            constexpr int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto &d : dirs) {
                const int nx = fx + d[0], nz = fz + d[1];
                if (m_world->blockAt(nx, fy, nz) == BlockRegistry::Water) {
                    const quint8 ns = m_world->stateAt(nx, fy, nz);
                    if (ns < footState) { // 该邻居更近源 → 推力朝远离它（离源）
                        gx -= float(d[0]) * float(footState - ns);
                        gz -= float(d[1]) * float(footState - ns);
                    }
                }
            }
            const float glen = std::sqrt(gx * gx + gz * gz);
            if (glen > 1e-4f) {
                m_vel.setX(m_vel.x() + (gx / glen) * kWaterFlowPush);
                m_vel.setZ(m_vel.z() + (gz / glen) * kWaterFlowPush);
            }
        }
        // 瀑布（脚位下方空气 = 水柱下落）→ 下沉上限抬高（额外向下带）；普通水仍 kWaterSinkMax。
        const bool waterfall = footState > 0
            && m_world->blockAt(fx, fy - 1, fz) == BlockRegistry::Air;
        const float sinkMax = waterfall ? kWaterfallSinkMax : kWaterSinkMax;
        m_vel.setY(std::clamp(float(m_vel.y() - kWaterGravity * dt), -sinkMax, kSwimUp));
        if (space) m_vel.setY(kSwimUp); // 按住空格 = 游泳上浮（连续；离水后 spaceEdge 跳跃边沿仍由下方分支处理）
    } else if (onLadder()) {
        // t413 竖直爬梯物理（机制等价 MC 1.0 梯子）：梯无碰撞 → 玩家穿入梯格；此处覆写垂直速度实现攀爬 / 悬挂 /
        //   缓降（取代正常重力，否则玩家会穿梯坠落）。仅走路模式生效（飞 / 观察者 early return）。
        //   按前（wish 非零）/ 空格 → 向上爬（kLadderClimb；spec「入梯 + 按前 → 向上爬」）；按蹲（shift）→ 悬挂静止
        //   （机制等价 MC 梯子按蹲不下滑）；松手 → 缓降（kLadderGravity << kGravity，机制对标 MC 梯子缓慢下滑）。
        //   spaceEdge 跳跃边沿：贴梯 + 着地按空格仍给一次跳跃（从梯顶 / 梯旁地面起跳离梯）。
        //   t539 爬升方向门控（spec「侧身经过不爬、直接穿过」）：爬升分支额外要求 facingLadder() —— 玩家水平移动
        //   意图须朝向所贴墙（wish·wallDir>kLadderFaceDot，≈60° 锥）。侧身擦过（wish 与墙向近乎垂直 / 反向，如
        //   沿通道直走而梯在左右墙）→ facingLadder 假 → 不爬，落入松手缓降分支（贴梯仍托住，不坠）→ 玩家直接
        //   穿过梯格不被「升起来」。悬挂（shift）/ 缓降不受门控影响（贴梯即可托住，与面向无关，机制对齐 MC）。
        if (shift) {
            m_vel.setY(0.0f); // 蹲 = 悬挂静止（机制等价 MC 梯子按蹲不下滑）
        } else if (space || (wish.lengthSquared() > 0.001f && facingLadder())) {
            m_vel.setY(kLadderClimb); // 面向梯子 + 按前 / 空格 = 向上意图 → 爬升（spec 验收：入梯 + 按前 → 向上爬）
        } else {
            m_vel.setY(std::max(float(m_vel.y() - kLadderGravity * dt), -kLadderSinkMax)); // 松手缓降（贴梯不下坠）
        }
        if (spaceEdge && m_onGround) m_vel.setY(kJump); // 贴梯 + 着地按空格 = 起跳（离梯）
    } else {
        m_vel.setY(std::max(float(m_vel.y() - kGravity * dt), -kMaxFall));
        if (spaceEdge && m_onGround) m_vel.setY(kJump);
    }
    // t258 被埋锁定（spec「被埋→锁定不能动，只能挖出脱困」）：玩家被实体方块完全包围（嵌入 + 四向皆堵）→
    //   moveAxis 的 snap 会把玩家推穿相邻块（前后左右穿出 / 坠出基岩外，机制等价观察者 noclip；Y 轴已由
    //   moveAxis(1) 纯 bury 回退防坠，水平在此先验门控）。锁定 → 水平 velocity 清零 → delta.x/z=0 →
    //   moveAxis(0/2) amount==0 早退（无穿出机会）。脱困（挖出方块打开侧面 / 创造双击空格切飞）后自动解锁。
    //   挖掘（raycast→beginMining）不经位移 → 不受影响，玩家仍可挖出卡住的方块脱困（spec「只能挖出脱困」）。
    //   窒息（t160）/ 饥饿等后续照常推进（受困 → 扣血逼其挖出）。判据与 extrudeEmbedded「全包裹→不挤」同源，
    //   见 isLockedBuried。地面复探（下方 aabbHitsSolid）仍跑：被埋态 AABB 重叠 → onGround=true（被支撑），
    //   防误判坠落 / 摔伤。
    // t352（t317 复发根治）：被埋锁定只清零**水平** velocity（m_vel.x/z）+ 击退冲量，**保留 m_vel.y**。
    //   t317 旧法仅豁免「跳跃触发那一 tick」（jumpFired 重设 kJump），但跳跃**上升期**的后续 tick 仍被这里
    //   清零 m_vel.y → 速度被吃 → 只跳半格卡死（须切创造 / 观察者复位）。锁定本职是防 moveAxis X/Z snap 穿墙
    //   （水平），Y 轴穿墙 / 坠落已由 moveAxis(1)（顶头 snap + 纯 bury 回退）兜底，故无需在此清垂直速度：
    //   真被埋且头顶有方块的玩家上跳仍被 moveAxis(1,+) 顶头 snap 挡住（锁定语义不破）；头顶无方块时上跳脱困
    //   合理（t317 既定设计）。水平清零保留 → 防穿墙目的不破。
    if (isLockedBuried()) { m_vel.setX(0); m_vel.setZ(0); m_knockback = QVector3D(0, 0, 0); }
    // t296 受击击退冲量水平积分（仅走路模式；Spectator / Creative-飞 noclip 早 return 不至此）。m_knockback 只剩 XZ
    //   （垂直小跳 kHitKnockbackUp 已在 applyHitKnockback 直接写入 m_vel.y，由上方 gravity / 着地分支统一处理）。
    //   每帧水平指数衰减，衰减殆尽（|x|+|z| < 阈）→ 清零。叠入位移 delta = (m_vel + m_knockback) * dt；m_knockback.y 恒 0
    //   → 垂直位移纯由 m_vel.y 决定（单一重力）。
    //   【旧 bug 根因 / 用户「被怪打后跳不起来」】旧版把垂直小跳也放 m_knockback.y 并在此每 tick 施重力（kGravity），
    //   与 m_vel.y 自身的重力（行上方 gravity 分支）叠加 = 双重力（dv/dt=-56）。小跳着地后 m_knockback.y 被清零，但只要
    //   水平击退（XZ）还在衰减（~1s），本积分块仍每 tick 跑 → 再把已清零的 m_knockback.y 按 -kGravity*dt 拉成负值 →
    //   叠入 delta.y 把玩家向下拽。其后的跳跃（m_vel.y=kJump=8.4）/ 水中上浮（m_vel.y=kSwimUp=4.5）有效向上速度被这股
    //   陈旧负冲量 + 双重力吃掉，峰值腰斩（跳跃 1.25→~0.5 格、水中上浮几乎顶不住），~1s 后水平击退衰减完才恢复 = 用户实测
    //   「被怪打后只跳半格、水里跳不上一格、过一会儿恢复」。根治：垂直走 m_vel.y（单一重力），m_knockback 只管水平
    //   （无重力、只衰减）→ 跳跃 / 上浮不再被陈旧击退拽下。
    if (m_knockback.x() != 0.0f || m_knockback.z() != 0.0f) {
        const float decay = std::max(0.0f, 1.0f - kHitKnockbackDrag * float(dt)); // 水平指数衰减（钳 ≥0 防 dt 过大翻负）
        m_knockback.setX(m_knockback.x() * decay);
        m_knockback.setZ(m_knockback.z() * decay);
        const float km = std::fabs(m_knockback.x()) + std::fabs(m_knockback.z());
        if (km < 0.08f) m_knockback = QVector3D(0, 0, 0); // 衰减殆尽 → 清零（防浮点噪声永久残留）
    }
    // 防穿墙：每子步任意轴移动 ≤0.4 格
    QVector3D delta = (m_vel + m_knockback) * dt; // 锁定 → m_vel=m_knockback=0 → delta=0 → moveAxis 早退
    const float md = std::max({std::fabs(delta.x()), std::fabs(delta.y()), std::fabs(delta.z())});
    const int sub = std::max(1, int(std::ceil(md / 0.4f)));
    delta /= float(sub);

    // t58 蹲下「边缘安全」（逐轴后查回滚）：原 t51 实现「整帧目的位置（m_pos + delta*sub）预查 →
    //   通过则整帧放行水平移动」在多轴合成位移 / 子步内 wall snap 下会与 moveAxis 后的真实 m_pos
    //   错位 → 玩家可能落到无支撑格而坠下方块边缘。改为 moveAxis(0) → 立即查脚下（新位置 -Y）有无
    //   支撑方块 → 无则回滚 X 位移；moveAxis(2) → 同样查 → 回滚 Z。判定点 = moveAxis 后的真实 m_pos
    //   （含本子步 wall snap），与 aabbHitsSolid 同源 → 边缘安全与实际碰撞一致。仅蹲态 + 本子步着地
    //   （m_onGround）时启用：走 / 疾跑 / 滞空 / 起跳子步不限制（防走下边缘，不干预跳跃 / 坠落）。
    const bool wasGround = m_onGround;
    m_onGround = false;
    for (int i = 0; i < sub; ++i) {
        const float dy = delta.y();
        moveAxis(1, dy);
        if (dy < 0 && m_vel.y() == 0) m_onGround = true; // 下落被挡 = 着地
        const bool crouchSafe = (m_moveState == Crouch && m_onGround);
        const float prevX = m_pos.x();
        moveAxis(0, delta.x());
        if (crouchSafe && delta.x() != 0.0f && !hasGroundBelowAt(m_pos.x(), m_pos.z()))
            m_pos.setX(prevX); // 蹲下边缘安全：X 移动后脚下无支撑 → 回滚该轴位移
        // t163 auto-step X：被低障碍（≤0.5：下半砖 / 楼梯整步 / 活版门合态）挡住且在地面 → 试抬升走过去
        //   （机制等价 MC 自动上半砖 / 楼梯，无需跳）。t559：① 抬升量改 autoStepLift() 精确到障碍顶 ——
        //   蹲态（1.5 高）在「1.5 格通道 + 下半砖」组合下抬 0.5 恰好贴天花板可过（旧固定 0.55 顶头失败）；
        //   ② 取消 `m_moveState != Crouch` 门 —— 蹲态（含 shift 按住 / 自动蹲）也能自动上半砖楼梯（用户
        //   「半砖楼梯自动上去不用跳」）。抬升后顶头或仍走不通 → 还原 Y（不影响正常碰撞）。
        const float stepLiftX = (delta.x() != 0.0f && m_pos.x() == prevX && m_onGround)
                                ? autoStepLift() : 0.0f;
        if (stepLiftX > 0.0f) {
            const float baseY = m_pos.y();
            m_pos.setY(baseY + stepLiftX);
            if (!aabbHitsSolid()) {
                const float prevXs = m_pos.x();
                moveAxis(0, delta.x());
                if (m_pos.x() == prevXs) m_pos.setY(baseY); // 抬升仍走不通（高墙）→ 还原
            } else {
                m_pos.setY(baseY); // 抬升顶头（天花板）→ 还原
            }
        }
        const float prevZ = m_pos.z();
        moveAxis(2, delta.z());
        if (crouchSafe && delta.z() != 0.0f && !hasGroundBelowAt(m_pos.x(), m_pos.z()))
            m_pos.setZ(prevZ); // 蹲下边缘安全：Z 移动后脚下无支撑 → 回滚该轴位移
        // t163 auto-step Z（同 X）：低障碍 + 地面 → 抬升 autoStepLift() 走过去，失败还原（t559 同上）。
        const float stepLiftZ = (delta.z() != 0.0f && m_pos.z() == prevZ && m_onGround)
                                ? autoStepLift() : 0.0f;
        if (stepLiftZ > 0.0f) {
            const float baseY = m_pos.y();
            m_pos.setY(baseY + stepLiftZ);
            if (!aabbHitsSolid()) {
                const float prevZs = m_pos.z();
                moveAxis(2, delta.z());
                if (m_pos.z() == prevZs) m_pos.setY(baseY);
            } else {
                m_pos.setY(baseY);
            }
        }
    }
    // t161 嵌入挤出：逐轴解算后若玩家仍被包裹（下落沙 / 放置方块 materialize 在玩家身上），沿最近开放
    //   水平方向推出（向外 not 向上）。先于地面复探 / 窒息判定 → 挤出成功则该 tick 不误判着地 / 窒息。
    extrudeEmbedded();
    // t712 批「被击飞插沙」：击飞窗口内的嵌入走向上排出（水平挤出无出路 / 未触发的兜底；沙坑四壁围堵
    //   时垂直是唯一生路。仅窗口内启用，常规被埋语义不变）。见 launchUnburyUpward。
    launchUnburyUpward();
    // t559 自动蹲站起复探：松 shift 后自动保持蹲（m_autoCrouch）期间，每 tick 查头顶 —— 有站起空间
    //   （canStandUp）即自动站起（清自动蹲 + setMoveState(Walk) 复位 AABB 高 / 眼位）。走位 / 半砖楼梯
    //   抬升后头顶仍不足 → 保持蹲（不误站、不把 1.8 AABB 塞进低顶 → 不被挤出 / 穿墙）。
    if (m_autoCrouch && m_moveState == Crouch && canStandUp()) {
        m_autoCrouch = false;
        setMoveState(Walk);
    }
    // 稳健地面复探：脚底下方 0.05 有实体即算着地
    const float oy = m_pos.y();
    m_pos.setY(oy - 0.05f);
    if (aabbHitsSolid()) m_onGround = true;
    m_pos.setY(oy);
    if (m_onGround && m_vel.y() < 0) m_vel.setY(0);
    // t296 击退小跳着地：垂直小跳已走 m_vel.y（applyHitKnockback 写入），由上一行 m_vel.y 着地归零统一吸收。
    //   旧版 m_knockback.y 的着地清零已随「垂直走 m_vel.y」重构移除（m_knockback.y 恒 0，无需再清）。

    // 掉落伤害（t22，仅 Survival）：滞空期间记录最高点 m_peakY，着地瞬间按 MC 1.0 公式
    // floor(落差-3) 结算（fall>3 才伤，每整格 1 HP = 半心）。上 tick 已着地 → 重置基准；
    // 上升（跳跃）更新峰；本 tick 刚着地 → 结算并复位。
    if (wasGround) m_peakY = m_pos.y();
    else if (m_pos.y() > m_peakY) m_peakY = m_pos.y();
    if (!wasGround && m_onGround) {
        const float fall = m_peakY - m_pos.y();
        if (m_mode == Survival && fall > 3.0f) {
            // t200 水抵消摔落伤害（机制等价 MC：落入水中免除摔伤）。着地瞬间脚位格 == Water → 水缓冲冲击，
            //   不结算伤害。复用 feetInWater()（脚位 blockAt == Water；水非实体 → 落地必踩在水床底块上方，
            //   floor(m_pos.y) 取水格 → 正确判中；无世界 → false 保守不抵消）。
            if (!feetInWater()) {
                const int dmg = int(std::floor(fall - 3.0f));
                // t655 击飞摔死归属：铁傀儡上抛后 5s 窗口内（m_golemLaunchTimer>0，applyGolemLaunch 起算）
                //   的着地摔伤改发 GolemLaunchFall（死亡播报「被铁傀儡击飞摔死」区别于普通「从高处坠落」）。
                //   **t690：着地沿无条件清窗口** —— 一次上抛只归属第一次落地。旧版只在 dmg>0 分支清 →
                //   无伤落地（落水抵消 / ≤3 格落差 / dmg==0）窗口滞留 → 5s 内随后的普通坠落摔死被误播报
                //   「被铁傀儡击飞摔死」。清窗统一挪到本分支末尾（任何着地沿），伤害判定读清窗前的值。
                if (dmg > 0 && m_golemLaunchTimer > 0.0f) {
                    emit fallDamageTaken(dmg, PlayerState::GolemLaunchFall);
                } else if (dmg > 0) emit fallDamageTaken(dmg, PlayerState::Fall); // t311 死因=高处坠落
            }
        }
        // t639④ 踩踏耕地（机制等价 MC 1.0：跳跃 / 坠落落到耕地上 → 耕地变泥土 + 上方作物掉落）。
        //   触发条件 = 着地瞬间（本分支即"滞空→着地"沿）+ 下落距离 > 阈值（普通跳跃 ~1.26 / 跨 1 格平
        //   台下落 ~1.06 均触发；走路并入耕地 / 微步下台阶 < 1.0 不踩坏，机制对齐 MC「非跳跃踩踏不坏」。
        //   脚位格 = 地面复探同款取样（floor(m_pos.y - 0.05)），精确取落点被踩的耕地格（脚位 0.9375 /
        //   整块顶 1.0 均映射到支撑格）。踩坏 → setBlock(Dirt)（耕地湿润态一并清）；耕地上有作物 →
        //   失撑掉落（复用 dropCropDrops 按生长阶段掉产物 = 未成熟掉种子、成熟掉小麦/作物，同失撑级联）。
        //   仅玩家路径（mob 踩踏留 t642 mob AI 轮）；创造同样踩坏（世界交互非伤害，机制对齐 MC）。分层：
        //   本处属 Game/Physics（读落点 + 写 World），不改 setBlock 语义。
        if (fall > kFarmlandTrampleFall && m_world) {
            const int bx = int(std::floor(m_pos.x()));
            const int by = int(std::floor(m_pos.y() - 0.05f)); // 脚底下一格（同地面复探 oy-0.05 取样）
            const int bz = int(std::floor(m_pos.z()));
            if (m_world->blockAt(bx, by, bz) == BlockRegistry::Farmland) {
                // t669② 用 setBlockSilent 静默写回土：踩踏是物理事件（非玩家破/放），原走 setBlock 发
                //   blockPlaced(Dirt) → QML onBlockPlaced 的「生存放置消耗 1 件」把选中槽误扣（手持泥土踩踏 →
                //   泥土凭空消失 = 本 bug）。静默写保留 worldChanged 重建 mesh，不发 blockPlaced/broken
                //   （机制等价 MC 踩踏回土无放置反馈）。踩坏 → setBlock(Dirt)（耕地湿润态一并清）。
                m_world->setBlockSilent(bx, by, bz, BlockRegistry::Dirt, 0);
                const int cy = by + 1;
                if (cy < m_world->height()) {
                    const quint8 crop = m_world->blockAt(bx, cy, bz);
                    if (crop == BlockRegistry::WheatCrop || crop == BlockRegistry::CarrotCrop
                        || crop == BlockRegistry::PotatoCrop) {
                        const quint8 cstate = m_world->stateAt(bx, cy, bz);
                        // 清作物走既有静默写（失撑级联，非玩家破块 → 无 broken 粒子/音；机制等价 MC 踩踏
                        //   作物掉落无声效）。setWaterSilent 是通用静默 state 写入口（名字历史遗留 water-first）。
                        m_world->setWaterSilent(bx, cy, bz, BlockRegistry::Air, 0);
                        dropCropDrops(bx, cy, bz, crop, cstate); // 失撑掉落（成熟按 stage 出产物）
                    }
                }
            }
        }
        // t690：着地沿无条件清击飞归属窗口（含无伤落地：落水抵消 / ≤3 格 / dmg==0 / 非生存模式）。
        //   归属只认「上抛后的第一次落地」——无论那次落地是否造成伤害，窗口使命即终。
        m_golemLaunchTimer = 0.0f;
        m_peakY = m_pos.y();
    }

    // t160 窒息（仅 Survival）：眼位（头部）嵌进实体方块的碰撞体（被埋 / 头卡进方块，机制等价 MC 窒息）→
    //   每 kSuffocationInterval 秒扣 1HP（fallDamageTaken 同路径 → PlayerState.takeDamage）+ 发 suffocationPulse
    //   （呈现层红屏闪 + 视角晃动）。创造 / 观察者无伤。脱困（头部出方块）即停累积。蹲下眼位低随之判定点下移。
    // t575 判据收紧「眼位格 collidable」→「眼位点落入该格某 sub-AABB 内」：1.5 格通道的天花板（上半砖等
    //   partial 块）整格 collidable，但蹲态眼位（1.35）只在其下方空气区 —— 旧判据把这种「合法约束蹲姿」
    //   误判窒息扣血。点在 sub-AABB 内才真嵌（与碰撞同源，partial 块精确）。
    if (m_mode == Survival && m_world) {
        const float ex = m_pos.x(), ey = m_pos.y() + m_eyeHeight, ez = m_pos.z();
        bool embedded = false;
        for (const BlockRegistry::BlockAABB &b
             : m_world->collisionAABBsAt(int(std::floor(ex)), int(std::floor(ey)), int(std::floor(ez)))) {
            if (ex > b.minX && ex < b.maxX && ey > b.minY && ey < b.maxY && ez > b.minZ && ez < b.maxZ) {
                embedded = true;
                break;
            }
        }
        if (embedded) {
            m_suffocationTimer += float(dt);
            if (m_suffocationTimer >= kSuffocationInterval) {
                m_suffocationTimer -= kSuffocationInterval;
                // fallDamageTaken(1) 经既有链 takeDamage→damaged→onDamaged 已驱动 HP 扣减 + 红屏闪 + 视角晃动（t67）。
                emit fallDamageTaken(1, PlayerState::Suffocation); // t311 死因=窒息
            }
        } else {
            m_suffocationTimer = 0.0f;
        }
    }

    // t202 气泡 + 溺水（仅 Survival 耗气；Creative/Spectator 不溺水 → 恒满气）。复用 eyeInWater()（眼位
    //   blockAt == Water；同 t201 蓝滤镜判定）。眼位入水：m_airTimer 累加 → 每 kAirInterval 减 1 气泡
    //   （emit airUpdated 驱动 PlayerState.air + 气泡条）；气泡归零后 m_drownTimer 累加 → 每 kDrownInterval
    //   扣 1HP（emit fallDamageTaken(1) 复用 takeDamage→damaged 红闪 / 视角晃链，与窒息同路径）。出水：
    //   m_airRegenTimer 累加 → 每 kAirRegenInterval 回 1 气泡（spec「出水 → 气泡回满后消失」）。非 Survival
    //   强制满气（切回 Survival 从满起算，防陈旧低气值残留）。死亡态（!m_captured 早 return）不进 step →
    //   不耗气；respawn 复位 m_air + 三计时器。
    if (m_world) {
        if (m_mode != Survival) {
            if (m_air != kMaxAir) { m_air = kMaxAir; emit airUpdated(m_air); }
            m_airTimer = 0.0f;
            m_drownTimer = 0.0f;
            m_airRegenTimer = 0.0f;
        } else if (eyeInWater()) {
            m_airRegenTimer = 0.0f;
            if (m_air > 0) {
                m_airTimer += float(dt);
                if (m_airTimer >= kAirInterval) {
                    m_airTimer -= kAirInterval;
                    --m_air;
                    emit airUpdated(m_air);
                }
            } else {
                // 气泡归零 → 溺水扣血（机制等价 MC 1.0：air=0 后每秒 1HP；复用 fallDamageTaken→damaged 链）
                m_drownTimer += float(dt);
                if (m_drownTimer >= kDrownInterval) {
                    m_drownTimer -= kDrownInterval;
                    emit fallDamageTaken(1, PlayerState::Drowning); // t311 死因=溺水
                }
            }
        } else {
            // 出水：气泡逐格回满（满后气泡条消失；计时器同步清）
            m_airTimer = 0.0f;
            m_drownTimer = 0.0f;
            if (m_air < kMaxAir) {
                m_airRegenTimer += float(dt);
                if (m_airRegenTimer >= kAirRegenInterval) {
                    m_airRegenTimer -= kAirRegenInterval;
                    ++m_air;
                    emit airUpdated(m_air);
                }
            } else {
                m_airRegenTimer = 0.0f;
            }
        }
    }

    // t238 饥饿系统（仅 Survival 推进；Creative/Spectator 锁满 + 计时器归零）。时间源 = 物理 tick 的 dt
    //   （与窒息 / 溺水同：step(dt) 由 tickImpl 每帧调，dt 来自 QTimer 真实流逝 → 等价 WorldClock 时间权威）。
    //   spec「饥饿随时间/运动掉落（hunger depletion tick，WorldClock 驱动；到 0→开始扣血，复用 takeDamage 链）」。
    //   三态推进：
    //   (1) 消耗：m_hungerDepleteAccum 按 dt × rate 累加（idle / walk / sprint 三档率，疾跑 > 走 > 静）；
    //       每满 1.0 扣 1 饥饿（clamp ≥0）+ emit hungerUpdated。spec「随时间 / 运动掉落」—— 水平速度 >0.1
    //       即视为移动（疾跑用 sprint 率，否则 walk 率），不动用 idle 率。
    //   (2) 饥饿回血：m_hunger >= kRegenHungerThreshold（18，9 鼓腿）且未满血时 m_regenTimer 累加 → 每
    //       kHungerRegenInterval（4s）回 1HP（机制等价 MC 1.0 饱腹回血；让「食面包」能真回血）。饥饿不足 /
    //       已满血 → 计时器归零。
    //   (3) 饥饿伤害：m_hunger == 0 时 m_starveTimer 累加 → 每 kStarveInterval（4s）扣 1HP（emit
    //       fallDamageTaken(1) → takeDamage → damaged 红闪 / 视角晃，与窒息 / 溺水同链，spec「复用 takeDamage 链」）。
    //   分层（PLAN §2）：Game/Physics 层算时序 + 发语义事件（hungerUpdated / fallDamageTaken），呈现层只消费
    //   （PlayerState.setHunger / takeDamage，经 Connections 路由，同 air / suffocation 模式）。
    if (m_mode == Survival) {
        // (1) 消耗：按运动状态选率（疾跑 > 走 > 静）。m_horizSpeed 由 reportHorizSpeed 上一 tick 算（本 tick
        //     用上一帧值近似当前运动态 —— 饥饿率误差 1 帧可忽，无需每帧重算）。
        const float rate = (m_horizSpeed > 0.1f)
                           ? (m_moveState == Sprint ? kHungerSprintRate : kHungerWalkRate)
                           : kHungerIdleRate;
        m_hungerDepleteAccum += float(dt) * rate;
        while (m_hungerDepleteAccum >= 1.0f) {
            m_hungerDepleteAccum -= 1.0f;
            if (m_hunger > 0) { --m_hunger; emit hungerUpdated(m_hunger); }
            // 饥饿已 0 → 不再扣（clamp），accum 继续累但无副作用（下一 starving 分支接管扣血）。
        }
        // (2) 饥饿回血：饱腹（m_hunger >= kRegenHungerThreshold=18）+ 未满血 → 每 kHungerRegenInterval（4s）
        //     回 1HP（emit healed(1) → 呈现层 PlayerState.heal；机制等价 MC 1.0 hunger≥18 自动回血，让
        //     「食面包」真有生存收益）。饥饿不足 / 已满血 → 计时器归零。
        if (m_hunger >= kRegenHungerThreshold) {
            m_regenTimer += float(dt);
            if (m_regenTimer >= kHungerRegenInterval) {
                m_regenTimer -= kHungerRegenInterval;
                emit healed(1);
            }
        } else {
            m_regenTimer = 0.0f;
        }
        // (3) 饥饿伤害：m_hunger == 0 → 每 kStarveInterval（4s）扣 1HP（emit fallDamageTaken(1) → 呈现层
        //     PlayerState.takeDamage → damaged 红闪 / 视角晃，与窒息 / 溺水同链，spec「到 0→开始扣血，复用
        //     takeDamage 链」）。m_hunger > 0 → 计时器归零（不跨饿时段累积）。
        if (m_hunger <= 0) {
            m_starveTimer += float(dt);
            if (m_starveTimer >= kStarveInterval) {
                m_starveTimer -= kStarveInterval;
                emit fallDamageTaken(1, PlayerState::Starvation); // t311 死因=饥饿
            }
        } else {
            m_starveTimer = 0.0f;
        }
    } else {
        // 非 Survival（Creative/Spectator）：饥饿锁满 + 三计时器归零（防切回 Survival 时陈旧累积串入）。
        if (m_hunger != kMaxHunger) { m_hunger = kMaxHunger; emit hungerUpdated(m_hunger); }
        m_hungerDepleteAccum = 0.0f;
        m_starveTimer = 0.0f;
        m_regenTimer = 0.0f;
    }

    // t344 玩家火烧（岩浆 / 火点燃；仅 Survival 着火 + 火伤 + 熄灭；机制等价 MC 1.0 玩家触岩浆着火）。
    //   分两段（同 EntityManager mob 火烧逻辑，常量复用 EntityManager::kFire* 保一致手感）：
    //   (1) 岩浆接触点燃：脚位格 floor(m_pos.y) 或眼位格 floor(pos.y+eye) 任一 == Lava → 刷 fireTimer = kFireDuration
    //       （持续重燃 = 离开前不熄）。机制等价 MC 玩家进岩浆着火。t351 修「伤害时有时无」：旧版在岩浆内把
    //       m_fireDmgTimer 归零 → 每帧累 dt 又被归零 → 累积器永达不到 kFireDamageInterval → **泡在岩浆里反而不扣血**，
    //       只有离开后的余焰才扣（且被随机熄灭跳过 → 时有时无）。现只刷 fireTimer（保持续燃），不碰火伤累积器 →
    //       泡岩浆 / 余焰均按 kFireDamageInterval 稳定扣血（修 spec「伤害稳定扣血」）。
    //   (2) 火烧推进：fireTimer>0 → 离开岩浆递减；每 kFireDamageInterval 扣 1HP（fallDamageTaken(1, Fire) 复用
    //       takeDamage→damaged 红闪 / 视角晃链，同窒息 / 溺水）+ 掷随机提前熄灭（kFireExtinguishChance）。
    //       fireTimer 归零即熄（定时双保险）。m_burning = fireTimer>0，翻转才 emit burningChanged（驱动底部火焰叠层）。
    //   非 Survival（Creative/Spectator 无敌）→ 清火（不着火 / 不火伤），翻转才 emit。无世界 → 不进火段（m_burning 翻 false）。
    if (m_mode == Survival && m_world) {
        const int fx = int(std::floor(m_pos.x()));
        const int fz = int(std::floor(m_pos.z()));
        const int footY = int(std::floor(m_pos.y()));            // 脚位格
        const int eyeY = int(std::floor(m_pos.y() + m_eyeHeight)); // 眼位格（潜没时）
        bool touchingLava = false;
        if (footY >= 0 && m_world->blockAt(fx, footY, fz) == BlockRegistry::Lava) touchingLava = true;
        if (!touchingLava && eyeY >= 0 && m_world->blockAt(fx, eyeY, fz) == BlockRegistry::Lava)
            touchingLava = true;
        if (touchingLava) {
            m_fireTimer = EntityManager::kFireDuration; // 持续重燃（离开前 fireTimer 不衰减）；不动 m_fireDmgTimer（t351）
        }
        if (m_fireTimer > 0.0f) {
            if (!touchingLava) m_fireTimer -= float(dt);
            m_fireDmgTimer += float(dt);
            if (m_fireDmgTimer >= EntityManager::kFireDamageInterval) {
                m_fireDmgTimer -= EntityManager::kFireDamageInterval;
                // 先掷随机提前熄灭（机制等价 MC 火 random extinguish）；不熄才扣 1HP 火伤。
                if (QRandomGenerator::global()->generateDouble() < double(EntityManager::kFireExtinguishChance)) {
                    m_fireTimer = 0.0f;
                    m_fireDmgTimer = 0.0f;
                } else {
                    emit fallDamageTaken(1, PlayerState::Fire); // t311 死因=燃烧（复用 takeDamage→damaged 链）
                }
            }
            if (m_fireTimer <= 0.0f) { m_fireTimer = 0.0f; m_fireDmgTimer = 0.0f; } // 定时熄灭
        }
        // m_burning 翻转才 emit（避免每帧抖 QML 绑定，同 eyeInWater 模式）。
        if ((m_fireTimer > 0.0f) != m_burning) { m_burning = !m_burning; emit burningChanged(); }
    } else {
        // 非 Survival：无敌不着火 → 清火态（防切回 Survival 时陈旧 fireTimer 串入）；翻转才 emit。
        m_fireTimer = 0.0f;
        m_fireDmgTimer = 0.0f;
        if (m_burning) { m_burning = false; emit burningChanged(); }
    }

    // t669 毒马铃薯食物中毒推进（机制等价 MC 1.0 poison）。触发 = finishEating 食毒薯 60% 掷中 →
    //   m_poisonTimer=kPoisonDuration（8s）。t715 状态效果系统 v1 收编语义（对齐 MC 1.0 poison 规范）：
    //   (a) 伤害周期改 **1.25s 扣 1 HP**（kPoisonInterval=1.25，MC 1.0 poison 每 25 tick 扣 1；旧 t669 无状态
    //       系统时简化为每秒 1 次）；
    //   (b) **等级 1 不致死** —— 扣到剩 1 HP 停（不致死亡；下限在呈现层 onPoisonDamageTaken 路由处守，
    //       见 Main.qml t715 注释——Physics 层不持 PlayerState.health，经信号解耦同 t690 设计）；
    //   (c) 饥饿损耗保留（-1/周期；t669 既有行为，防回归）。
    //   仅 Survival（Creative/Spectator 无敌不清毒也无效——统一复位防切回陈旧串入，同火烧态）。
    //   poisonDamageTaken（t690 独立信号：毒不走 fallDamageTaken → 不吃护甲减伤 / 不磨护甲耐久，机制等价
    //   MC poison 绕过盔甲；死因归 Generic——等级 1 本就不致死，死因分支实际不可达，保留兜底）。
    //   中毒归零即解毒（时间耗尽）。
    if (m_mode == Survival) {
        if (m_poisonTimer > 0.0f) {
            m_poisonTimer -= float(dt);
            m_poisonDmgAccum += float(dt);
            if (m_poisonDmgAccum >= kPoisonInterval) {
                m_poisonDmgAccum -= kPoisonInterval;
                if (m_hunger > 0) { --m_hunger; emit hungerUpdated(m_hunger); } // 毒掉饥饿（clamp≥0）
                emit poisonDamageTaken(1); // 毒掉血（t690 独立链：绕护甲；不致死下限在呈现层路由守）
            }
            if (m_poisonTimer <= 0.0f) { m_poisonTimer = 0.0f; m_poisonDmgAccum = 0.0f; } // 定时解毒
        }
    } else {
        m_poisonTimer = 0.0f;      // 非 Survival：无敌不清毒（防切回 Survival 时陈旧 timer 串入）
        m_poisonDmgAccum = 0.0f;
    }

    // t394/t445 玩家仙人掌接触伤害 + t467 雪原浆果灌木丛穿越伤害（spec「contact damages entities that touch it」
    //   / 「踩过 stage>0 丛的实体受少量伤害」；机制等价 MC 1.0 仙人掌触碰即伤 + 甜浆果丛穿越即伤）。
    //   接触判定：扫描玩家 AABB footprint（±0.3 覆盖玩家宽 0.6 的 1~4 格）在脚位 / 眼位 Y 层，对每个 footprint 格查
    //   其自身 + 水平 4 邻，任一 == Cactus 即接触；再加脚下格（站仙人掌顶）。**t445 ③ 全方位**：旧版仅查 footprint 自身
    //   格 —— 但 Cactus 实体（碰撞停在邻格）→ 玩家自身格恒非 Cactus → 仅「脚下格」分支生效（只判站顶，撞侧面不伤）。
    //   改查 footprint 格 + 水平 4 邻 → 覆盖「撞其任意侧」（与 mob 版 EntityManager 同源 4 邻判定）。每 kCactusDamageInterval
    //   (0.5s) 扣 1HP（fallDamageTaken(1, Cactus) 复用 takeDamage→damaged 红闪 / 视角晃链，同窒息 / 溺水 / 火）。仅
    //   Survival（Creative/Spectator 无敌）；离开即重置累积器。只读 World::blockAt（向下依赖）。
    //   t467 浆果丛穿越：SweetBerryBush 是 ShapeNone 无碰撞 → 玩家**穿过**丛格（嵌入丛内）。stage>0（带果）丛有刺，
    //   穿越即伤（机制等价 MC 甜浆果丛穿越即伤 + 减速，本工程简化仅伤害不减速）。检测 = footprint 自身格（脚位 / 眼位）
    //   含 stage>0 SweetBerryBush 即接触（丛无碰撞 → 玩家自身格即丛格，不查 4 邻）。伤害复用同一 m_cactusDmgTimer +
    //   fallDamageTaken(1, Cactus)（同为「带刺植物接触伤害」语义；死因归 Cactus 接触植物，简化避免新增 DeathCause 枚举）。
    if (m_mode == Survival && m_world) {
        const int fx0 = int(std::floor(m_pos.x() - 0.3f)), fx1 = int(std::floor(m_pos.x() + 0.3f));
        const int fz0 = int(std::floor(m_pos.z() - 0.3f)), fz1 = int(std::floor(m_pos.z() + 0.3f));
        const int footY = int(std::floor(m_pos.y()));
        auto cactusAt = [&](int xx, int yy, int zz) -> bool {
            return yy >= 0 && yy < m_world->height()
                   && m_world->blockAt(xx, yy, zz) == BlockRegistry::Cactus;
        };
        // t467 浆果丛（stage>0 带果即有刺）：ShapeNone 无碰撞 → 玩家穿过丛格，自身格即丛格。stateAt 判 stage>0。
        auto thornyBushAt = [&](int xx, int yy, int zz) -> bool {
            return yy >= 0 && yy < m_world->height()
                   && m_world->blockAt(xx, yy, zz) == BlockRegistry::SweetBerryBush
                   && m_world->stateAt(xx, yy, zz) > 0;
        };
        // misc 二轮 仙人掌斜对角误伤修复：旧版用整数格 footprint + 水平 4 邻判接触 → footprint 跨格时 4 邻覆盖到
        //   斜对角格（如玩家在 (5,5)，footprint 含 (6,5)，其 +Z 邻 (6,6) 命中斜对角仙人掌 → 误扣血）。改**精确 AABB
        //   重叠**判定：仙人掌方块 AABB 内缩 0.1（kCactusInset，partialblockgeometry 同源）= [cx+0.1,cx+0.9]×
        //   [cy,cy+1]×[cz+0.1,cz+0.9]；玩家 AABB = m_pos.x()±0.3 / m_pos.z()±0.3 / [m_pos.y, m_pos.y+m_height]。
        //   仅当两 AABB 在三轴都重叠才算接触（机制等价 MC 仙人掌实体 AABB 真接触才伤）。遍历玩家 XZ 覆盖格 ×
        //   Y 覆盖层（footY..ceil(height)），每格 Cactus 做 AABB 测试。浆果丛无碰撞（穿入），保留格判定不动。
        // misc 三轮 仙人掌无伤害回归修复（二轮 AABB 改矫枉过正）：二轮只遍历玩家 footprint 内格，但玩家被
        //   仙人掌满格碰撞挡在格边界外 → footprint 不含仙人掌格 → 遍历不到 → 无伤害。修：
        //   (a) 遍历范围扩到含**正交 4 邻**（fx0-1..fx1+1）—— 玩家撞仙人掌侧面时被挡在邻格边界，须查邻格；
        //   (b) 仙人掌 AABB 用**满格** [c,c+1]（非内缩 0.1），且 overlap 用**含边界**（>=/<=，非严格 >/<）
        //       —— 玩家被碰撞挡在格边界（pMaxX ≈ 整数边界 = 仙人掌格 cMinX）时算接触（机制等价 MC 仙人掌触碰即伤）。
        //   (c) 斜对角仍被 AABB 过滤：玩家挡在 (p+1,0) 边界，其 AABB 不越入 (p+1,p+1) 对角格 → 无重叠 → 不误伤。
        const float pMinX = m_pos.x() - 0.3f, pMaxX = m_pos.x() + 0.3f;
        const float pMinZ = m_pos.z() - 0.3f, pMaxZ = m_pos.z() + 0.3f;
        const float pMinY = m_pos.y(),         pMaxY = m_pos.y() + m_height;
        auto cactusAabbOverlap = [&](int cx, int cy, int cz) -> bool {
            const float cMinX = float(cx), cMaxX = float(cx) + 1.0f;   // 满格 AABB（仙人掌实体 0.8 内缩不用于伤害判定；
            const float cMinZ = float(cz), cMaxZ = float(cz) + 1.0f;   //   触碰伤害用整格接触，避免边界相等漏判）
            const float cMinY = float(cy), cMaxY = float(cy) + 1.0f;
            return pMinX <= cMaxX && pMaxX >= cMinX   // 含边界（>=/<=）：玩家挡在格边界 = 接触
                   && pMinZ <= cMaxZ && pMaxZ >= cMinZ
                   && pMinY <  cMaxY && pMaxY >  cMinY;  // Y 严格（上下格不因边界相等误判）
        };
        bool touch = false;
        const int yLo = footY, yHi = int(std::floor(pMaxY));   // 玩家 AABB 覆盖的 Y 整数层（脚到头顶）
        // 遍历 XZ 范围扩到 ±1（含正交邻）：玩家撞仙人掌侧面被挡在邻格边界 → 须查邻格。斜对角靠 AABB 过滤。
        for (int yy = yLo; yy <= yHi && !touch; ++yy) {
            for (int cx = fx0 - 1; cx <= fx1 + 1 && !touch; ++cx)
                for (int cz = fz0 - 1; cz <= fz1 + 1 && !touch; ++cz) {
                    if (cactusAt(cx, yy, cz) && cactusAabbOverlap(cx, yy, cz)) { touch = true; break; }
                    // t467 浆果丛穿越：无碰撞 → 玩家自身格即丛格（不查邻、不做 AABB，丛占满格）。
                    if (thornyBushAt(cx, yy, cz)) { touch = true; break; }
                }
        }
        // t716 ① 站仙人掌顶伤害回归修复（t190 修过、后续三轮 AABB 改后回归）：主循环 / 下方复探的 Y 判定用
        //   **严格** `pMinY < cMaxY` —— 玩家站仙人掌顶（碰撞 snap 使 m_pos.y == 仙人掌格顶 cMaxY，脚位无
        //   穿入）时该式为 false → 站顶接触漏判 → **站上仙人掌不扣血**（近靠侧面仍扣，唯站顶失效）。修：脚下
        //   复探分支单独用**含边界** Y 判定（pMinY <= cMaxY）—— 该分支只查 footY-1 一层（玩家正站其上的
        //   支撑格），语义即「贴顶 = 接触」，无斜对角 / 上下层误伤面（XZ 仍受满格 AABB 含边界过滤，同主循环）。
        //   主循环保持严格 Y（侧撞判定不变，防头顶擦过下方格误伤）。
        if (!touch) { // 站在仙人掌顶（脚下格）—— 脚位贴其顶 = 接触（含边界 Y）
            for (int cx = fx0; cx <= fx1 && !touch; ++cx)
                for (int cz = fz0; cz <= fz1 && !touch; ++cz)
                    if (cactusAt(cx, footY - 1, cz)) {
                        const float cMinX = float(cx), cMaxX = float(cx) + 1.0f;
                        const float cMinZ = float(cz), cMaxZ = float(cz) + 1.0f;
                        const float cMaxY = float(footY - 1) + 1.0f; // 脚下仙人掌格顶 = footY
                        if (pMinX <= cMaxX && pMaxX >= cMinX
                            && pMinZ <= cMaxZ && pMaxZ >= cMinZ
                            && pMinY <= cMaxY) touch = true;
                    }
        }
        if (touch) {
            m_cactusDmgTimer += float(dt);
            if (m_cactusDmgTimer >= EntityManager::kCactusDamageInterval) {
                m_cactusDmgTimer = 0.0f;
                emit fallDamageTaken(1, PlayerState::Cactus); // t394 死因=仙人掌（复用 takeDamage→damaged 链）
            }
        } else {
            m_cactusDmgTimer = 0.0f; // 离开即重置（机制等价 MC：接触才扣，离开即停）
        }
    } else {
        m_cactusDmgTimer = 0.0f; // 非 Survival：无敌不累（防切回 Survival 时陈旧累积串入）
    }

    // t715 缓慢效果推进（StatusEffect::EffectSlowness 时序源；来源 = applyStatusEffect /effect 命令）。
    //   仅 Survival 生效（无敌模式无效果，同火 / 中毒门控）；归零解除 + 等级清位（防切回 Survival 陈旧串入）。
    //   减速的应用不在本段 —— step() 走路分支读 m_slowTimer 乘 kSlowSpeedMul（见该处注释）。
    if (m_mode == Survival) {
        if (m_slowTimer > 0.0f) {
            m_slowTimer -= float(dt);
            if (m_slowTimer <= 0.0f) { m_slowTimer = 0.0f; m_slowLevel = 0; } // 定时解除
        }
    } else {
        m_slowTimer = 0.0f;
        m_slowLevel = 0;
    }

    // t715 状态效果快照广播（效果框架 v1 收编口）：组装当前活跃效果（中毒 m_poisonTimer / 缓慢 m_slowTimer /
    //   着火 m_fireTimer），与上一帧缓存深比较（逐项 type/整秒/level）真变才 emit activeEffectsChanged →
    //   呈现层 Connections 路由 PlayerState.setActiveEffects（Game 层持显值）。seconds 用 ceil 整秒 →
    //   每 effect 每秒最多 1 次 emit，无每帧抖动（同 burningChanged 翻转才发纪律）。
    {
        const QVariantList snapshot = buildActiveEffects();
        const bool changed = (snapshot.size() != m_lastEffectSigCache.size())
            || [&]() {
                   for (int i = 0; i < snapshot.size(); ++i) {
                       const QVariantMap &a = snapshot.at(i).toMap();
                       const QVariantMap &b = m_lastEffectSigCache.at(i).toMap();
                       if (a.value(QStringLiteral("type")).toInt() != b.value(QStringLiteral("type")).toInt()
                           || a.value(QStringLiteral("seconds")).toInt() != b.value(QStringLiteral("seconds")).toInt()
                           || a.value(QStringLiteral("level")).toInt() != b.value(QStringLiteral("level")).toInt())
                           return true;
                   }
                   return false;
               }();
        if (changed) {
            m_lastEffectSigCache = snapshot;
            emit activeEffectsChanged(snapshot);
        }
    }

    if (wasGround != m_onGround) emit onGroundChanged();
    reportHorizSpeed(posBefore, dt); // t159：speed 属性上报（位移/dt；含撞墙归零 / 疾跑 / 水下倍数）
    emit positionChanged();
}
