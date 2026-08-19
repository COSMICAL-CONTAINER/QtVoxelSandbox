#include "playerstate.h"

#include <QVariantMap>
#include <algorithm> // std::clamp/std::max/std::min

PlayerState::PlayerState(QObject *parent) : QObject(parent) {}

// 受伤钩子：扣 HP，clamp 到 0（Phase 1.0 仅掉落伤害走此路径；真实战斗属 Phase 1.1）。
// t51：实扣血时同步发 damaged(amount) → 呈现层启动红闪叠层动画（spec「受伤红色半透闪烁」）。
// t78：health 扣到 ≤0 → 置 dead 态 + emit died + deadChanged；此后 takeDamage 早退（不再继续扣，spec）。
//   即「致死那一击」照常发 damaged（最后一次红闪）+ healthChanged，紧接 died；之后免疫（dead=true）。
void PlayerState::takeDamage(int amount, int cause)
{
    if (amount <= 0) return;
    if (m_dead) return;              // 已死亡 → 不再继续扣（spec t78）
    const int nv = std::max(0, m_health - amount);
    if (nv == m_health) return;      // 无变化（理论上不达：m_health>0 且 amount>0）
    m_lastCause = cause;             // t311 记录最近受伤来源（致死那一击即死因）
    m_health = nv;
    emit healthChanged();
    emit damaged(amount);            // 触发呈现层红闪（即便未跨整心边界也要闪，spec）
    if (m_health <= 0) {
        m_dead = true;
        m_deathCause = m_lastCause;  // t311 致死来源 = 致死那一击的来源
        // t443 死亡清空 XP（spec「死亡清空 XP 条」）：xp / level / intoLevel 归零。Game 层持有此死亡规则
        //   （同 dead / deathCause 在致死分支置位），呈现层（Main.qml onDied）另在死亡点 spawn 一个少量
        //   经验球（约 1 只被动 mob 量）作可回收的部分 XP。level 真降才 emit levelChanged；xp 真变才 emit
        //   xpChanged（无变化不发信号，同 healthChanged 纪律）。intoLevel 随 xp 归零（xp=0 → intoLevel 恒 0）。
        const bool xpWas = (m_xp != 0);
        m_xp = 0;
        m_intoLevel = 0;
        if (m_level != 0) { m_level = 0; emit levelChanged(); }
        if (xpWas) emit xpChanged();
        emit deadChanged();          // 驱动 QML 死亡界面显（dead 绑定）
        emit deathCauseChanged();    // t311 驱动 deathCause / deathCauseText 绑定
        emit died();                 // 一次性事件：呈现层据此释放指针 / 关背包面板
    }
}

// t311 死因文案（通用描述词，§9 零 MC 专名：机制等价 MC 僵尸/骷髅/苦力怕 → 蹒跚者/骸骨/潜行者）。
//   供 t312 聊天播报 / t313 死亡屏文案消费（呈现层只读，单一权威在 Game 层）。未知 / 默认 → 不明原因。
QString PlayerState::deathCauseText() const
{
    switch (m_deathCause) {
    case Fall:         return QStringLiteral("从高处坠落");
    case Suffocation:  return QStringLiteral("在方块中窒息");
    case Drowning:     return QStringLiteral("溺水身亡");
    case Starvation:   return QStringLiteral("饥饿而亡");
    case Shambler:     return QStringLiteral("被蹒跚者击败");
    case Bones:        return QStringLiteral("被骷髅弓箭手击败"); // t594：骸骨→骷髅弓箭手（显示字串；MobBones 标识符不动）
    case Spider:       return QStringLiteral("被蜘蛛击败");
    case Stalker:      return QStringLiteral("被潜行者炸飞");
    case Tnt:          return QStringLiteral("被 TNT 炸死"); // t494：TNT 爆炸致死（独立死因，区别于潜行者自爆）
    case Fire:         return QStringLiteral("被烈火吞噬");
    case Cactus:       return QStringLiteral("被仙人掌扎死");
    case GolemLaunchFall: return QStringLiteral("被铁傀儡击飞摔死"); // t655 归属窗口内的摔落（普通坠落仍「从高处坠落」）
    case GolemSlain:      return QStringLiteral("被铁傀儡击杀"); // t712：近距重拳直接击杀（非摔落路径）
    case Generic:
    default:           return QStringLiteral("不明原因");
    }
}

// 恢复生命：加 HP，clamp 到 maxHealth（进食/治疗，Phase 1.1 用）。
void PlayerState::heal(int amount)
{
    if (amount <= 0) return;
    const int nv = std::min(kMaxHealth, m_health + amount);
    if (nv == m_health) return;
    m_health = nv;
    emit healthChanged();
}

// 设饥饿值：clamp 到 [0, maxHunger]。无变化不发信号（进食消耗属 Phase 1.1；本轮留接口）。
void PlayerState::setHunger(int value)
{
    const int nv = std::clamp(value, 0, kMaxHunger);
    if (nv == m_hunger) return;
    m_hunger = nv;
    emit hungerChanged();
}

// t176 存档加载：直接设 health（不走 takeDamage 死亡判定）。clamp + 同步 dead 态（存档玩家非死亡 → 置 false）。
void PlayerState::setHealth(int value)
{
    const int nv = std::clamp(value, 0, kMaxHealth);
    if (nv != m_health) { m_health = nv; emit healthChanged(); }
    if (nv > 0 && m_dead) { m_dead = false; emit deadChanged(); } // 防陈旧死亡态残留显死亡界面
    // t311：存档加载的玩家非死亡态 → 死因复位 Generic（防上一局死因残留到本局死亡屏 / 聊天播报）。
    if (nv > 0 && m_deathCause != Generic) { m_deathCause = Generic; emit deathCauseChanged(); }
    // t715：存档加载清状态效果容器（效果不持久化 —— PlayerController::loadSavedState 同步清 m_poisonTimer/
    //   m_fireTimer 时序源，同火烧瞬态语义；此处清容器防上一世界效果跨世界残留显陈旧图标）。
    if (!m_effects.isEmpty()) {
        m_effects.clear();
        ++m_effectRevision;
        emit effectsChanged();
    }
}

// t202 设气泡值：clamp 到 [0, maxAir]；无变化静默。由 PlayerController 经 airUpdated 信号驱动（眼位入水
//   逐格减 / 出水回满）；t176 存档 round-trip 亦走此入口（air 可能 < max，需保真）。
void PlayerState::setAir(int value)
{
    const int nv = std::clamp(value, 0, kMaxAir);
    if (nv != m_air) { m_air = nv; emit airChanged(); }
}

// t78 重生：清 dead 态 + 恢复满血满饥满气泡（无变化静默；dead 翻回 false 必发 deadChanged → 死亡界面隐）。
//   仅复位本类数值；玩家传回出生点由 PlayerController::respawn() 负责（分层：状态 vs 定位）。
//   t202：同步恢复 air 到 maxAir（重生回出生点必在地表 / 水外 → 满气泡起算）。
void PlayerState::respawn()
{
    if (m_dead) { m_dead = false; emit deadChanged(); } // 必发：驱动死亡界面 visible 绑定重算为 false
    if (m_health != kMaxHealth) { m_health = kMaxHealth; emit healthChanged(); }
    if (m_hunger != kMaxHunger) { m_hunger = kMaxHunger; emit hungerChanged(); }
    if (m_air != kMaxAir) { m_air = kMaxAir; emit airChanged(); } // t202 重生回满气泡
    // t715 重生清状态效果（机制等价 MC 1.0 死亡/重生清除全部状态；PlayerController::respawn 同步清
    //   m_poisonTimer/m_fireTimer 等时序源，本容器随之在下一 tick 收到空列表 —— 此处再显式清防「重生后
    //   最后一帧残留图标」，同火烧 m_burning 清态模式）。
    if (!m_effects.isEmpty()) {
        m_effects.clear();
        ++m_effectRevision;
        emit effectsChanged();
    }
    // t311 重生复位死因（死亡屏 / 聊天文案随之清空；m_lastCause 同步复位，防下次死亡前陈旧来源）。
    if (m_deathCause != Generic) { m_deathCause = Generic; emit deathCauseChanged(); }
    m_lastCause = Generic;
}

// t715 设活跃效果列表（PlayerController 经信号 → 呈现层路由调；见头注释）。深比较：逐项 type/seconds/level
//   全同（含长度）视为无变化静默 —— PlayerController 只在真变化（增删 / 剩余秒跨整秒）时发信号，本处比较是
//   防御性二次校验（同 setAir「无变化不发信号」纪律）。变化 → 覆盖 + ++m_effectRevision + emit effectsChanged。
void PlayerState::setActiveEffects(const QVariantList &list)
{
    const bool same = (list.size() == m_effects.size())
        && [&]() {
               for (int i = 0; i < list.size(); ++i) {
                   const QVariantMap &a = list.at(i).toMap();
                   const QVariantMap &b = m_effects.at(i).toMap();
                   if (a.value(QStringLiteral("type")).toInt() != b.value(QStringLiteral("type")).toInt()
                       || a.value(QStringLiteral("seconds")).toInt() != b.value(QStringLiteral("seconds")).toInt()
                       || a.value(QStringLiteral("level")).toInt() != b.value(QStringLiteral("level")).toInt())
                       return false;
               }
               return true;
           }();
    if (same) return;
    m_effects = list;
    ++m_effectRevision;
    emit effectsChanged();
}

// t715 当前活跃效果列表（直接返容器；QML Repeater model = { effectRevision 参与表达式 + effectList() } 模式）。
QVariantList PlayerState::effectList() const
{
    return m_effects;
}

// t402 累积经验值：加 amount（吸收经验球时调；amount<=0 忽略）。累积 m_xp（总）+ 派生 level（t403，
//   跨曲线阈值即升级）+ emit xpChanged（呈现层 F3 / 经验条刷新）。分层（PLAN §2）：Game 层持显值，
//   呈现层 Connections 据语义事件（XpOrbManager::xpPickedUp）路由调用。
void PlayerState::addXp(int amount)
{
    if (amount <= 0) return;
    m_xp += amount;
    recomputeLevel(); // t403 跨曲线阈值 → level++（条重置）；level 真变才 emit levelChanged
    emit xpChanged();
}

// t402 设经验值（存档加载用；与 setHealth/setHunger 同模式）：clamp 到 >=0；无变化不发信号。
//   t403：改值后 recomputeLevel 重建 level（存档只存 xp 总量 → 读档 level 自动派生，无需单独持久化）。
void PlayerState::setXp(int value)
{
    const int nv = value < 0 ? 0 : value;
    if (nv != m_xp) {
        m_xp = nv;
        recomputeLevel();
        emit xpChanged();
    }
}

// t474 消耗 XP 等级（附魔台每次附魔扣 1/2/3 级；机制等价 MC 1.0 附魔消耗 player level）。
//   余额不足（m_level < amount）→ 返 false（caller 不应推进附魔）。否则把 m_xp 截到「升至 (level-amount)
//   所需 XP」即 xpTotalForLevel(newLevel) —— 由于 level 是 m_xp 的派生（recomputeLevel 据曲线倒推），
//   把 m_xp 设到「恰好升至 newLevel 所需」即等价于「保留前 newLevel 级、丢弃级内进度 + 后续 N 级」。
//   级内进度（m_intoLevel）随之归零（机制等价 MC 附魔后级内条清空）。重算后 emit xpChanged + levelChanged。
//   amount<=0 → 视作无消耗返 true（防御，caller 应保证 >=1）。
bool PlayerState::spendLevels(int amount)
{
    if (amount <= 0) return true;          // 防御：无消耗视为成功
    if (m_level < amount) return false;    // 余额不足 → 拒绝（caller 不扣 lapis / 不推进）
    const int newLevel = m_level - amount;
    m_xp = xpTotalForLevel(newLevel);      // 截到「升至 newLevel 所需」总量（级内进度归零）
    recomputeLevel();                       // 同步 m_level / m_intoLevel（恒等于 newLevel / 0）
    emit xpChanged();
    // levelChanged 由 recomputeLevel 内部 emit（level 真降必发）；此处不再重复 emit。
    return true;
}

// t695 加 N 级（/xp <N>L 命令用；见头注释）：直接把 m_xp 设到「恰好升至 (m_level+amount) 所需」总量
//   （级内进度清零 = 整级跨越语义，机制等价 MC /xp <N>L）。走 setXp 复用「无变化静默 + recomputeLevel +
//   emit xpChanged」全套纪律。
void PlayerState::addLevels(int amount)
{
    if (amount <= 0) return;
    setXp(xpTotalForLevel(m_level + amount));
}

// t403 MC 1.0 风格递增曲线（机制等价 MC，三段斜率；need 单调递增 → 每级比上一级要更多 XP）。
int PlayerState::xpNeedForLevel(int level)
{
    if (level < 0) return 0;   // 防御：负级无意义
    if (level < 15) return 2 * level + 7;
    if (level < 30) return 5 * level - 38;
    return 9 * level - 158;
}

// t474 升至 level 所需总 XP（= sum_{i=0..level-1} xpNeedForLevel(i)；机制等价 MC「level 0 起，每升一级累加
//   需要的 XP」）。用于 spendLevels 把 m_xp 截到「恰好升至 newLevel 所需」总量。level<=0 → 0（0 级无累计）。
//   纯函数于曲线（与 xpNeedForLevel 同源；改曲线须同步二者，单一权威在 xpNeedForLevel）。
int PlayerState::xpTotalForLevel(int level)
{
    if (level <= 0) return 0;
    int total = 0;
    for (int i = 0; i < level; ++i) total += xpNeedForLevel(i);
    return total;
}

// t403 升下一级所需 XP（曲线驱动；level 变才变）。
int PlayerState::xpToNextLevel() const
{
    return xpNeedForLevel(m_level);
}

// t403 经验条填充比 [0,1] = 当前级内 XP / xpToNextLevel。need<=0（防御）→ 0。
qreal PlayerState::xpBarFraction() const
{
    const int need = xpNeedForLevel(m_level);
    if (need <= 0) return 0.0;
    return qreal(m_intoLevel) / qreal(need);
}

// t403 据 m_xp（总）派生 m_level + m_intoLevel：从 level 0 起逐级减去「升下一级所需」直到余额不足
//   → m_level = 已升至的级、m_intoLevel = 余额（= 当前级内进度，驱动经验条）。单次 addXp 跨多级时循环
//   连升（大经验球一次跨数级）。level 真变才 emit levelChanged（驱动 HUD 等级数 / 条分母刷新）。
void PlayerState::recomputeLevel()
{
    int lvl = 0;
    int rem = m_xp;
    for (;;) {
        const int need = xpNeedForLevel(lvl);
        if (need <= 0 || rem < need) break; // 不足以升下一级（need 恒 >0，<=0 仅防御兜底）
        rem -= need;
        ++lvl;
    }
    m_intoLevel = rem < 0 ? 0 : rem;
    if (lvl != m_level) { m_level = lvl; emit levelChanged(); }
}
