#include "playerstate.h"

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
    case Bones:        return QStringLiteral("被骸骨击败");
    case Spider:       return QStringLiteral("被蜘蛛击败");
    case Stalker:      return QStringLiteral("被潜行者炸飞");
    case Fire:         return QStringLiteral("被烈火吞噬");
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
    // t311 重生复位死因（死亡屏 / 聊天文案随之清空；m_lastCause 同步复位，防下次死亡前陈旧来源）。
    if (m_deathCause != Generic) { m_deathCause = Generic; emit deathCauseChanged(); }
    m_lastCause = Generic;
}
