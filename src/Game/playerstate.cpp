#include "playerstate.h"

#include <algorithm> // std::clamp/std::max/std::min

PlayerState::PlayerState(QObject *parent) : QObject(parent) {}

// 受伤钩子：扣 HP，clamp 到 0（Phase 1.0 仅掉落伤害走此路径；真实战斗属 Phase 1.1）。
// t51：实扣血时同步发 damaged(amount) → 呈现层启动红闪叠层动画（spec「受伤红色半透闪烁」）。
// t78：health 扣到 ≤0 → 置 dead 态 + emit died + deadChanged；此后 takeDamage 早退（不再继续扣，spec）。
//   即「致死那一击」照常发 damaged（最后一次红闪）+ healthChanged，紧接 died；之后免疫（dead=true）。
void PlayerState::takeDamage(int amount)
{
    if (amount <= 0) return;
    if (m_dead) return;              // 已死亡 → 不再继续扣（spec t78）
    const int nv = std::max(0, m_health - amount);
    if (nv == m_health) return;      // 无变化（理论上不达：m_health>0 且 amount>0）
    m_health = nv;
    emit healthChanged();
    emit damaged(amount);            // 触发呈现层红闪（即便未跨整心边界也要闪，spec）
    if (m_health <= 0) {
        m_dead = true;
        emit deadChanged();          // 驱动 QML 死亡界面显（dead 绑定）
        emit died();                 // 一次性事件：呈现层据此释放指针 / 关背包面板
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

// t78 重生：清 dead 态 + 恢复满血满饥（无变化静默；dead 翻回 false 必发 deadChanged → 死亡界面隐）。
//   仅复位本类数值；玩家传回出生点由 PlayerController::respawn() 负责（分层：状态 vs 定位）。
void PlayerState::respawn()
{
    if (m_dead) { m_dead = false; emit deadChanged(); } // 必发：驱动死亡界面 visible 绑定重算为 false
    if (m_health != kMaxHealth) { m_health = kMaxHealth; emit healthChanged(); }
    if (m_hunger != kMaxHunger) { m_hunger = kMaxHunger; emit hungerChanged(); }
}
