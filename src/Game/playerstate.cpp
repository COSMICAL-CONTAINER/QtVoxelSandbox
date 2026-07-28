#include "playerstate.h"

#include <algorithm> // std::clamp/std::max/std::min

PlayerState::PlayerState(QObject *parent) : QObject(parent) {}

// 受伤钩子：扣 HP，clamp 到 0（Phase 1.0 仅掉落伤害走此路径；真实战斗属 Phase 1.1）。
// t51：实扣血时同步发 damaged(amount) → 呈现层启动红闪叠层动画（spec「受伤红色半透闪烁」）。
void PlayerState::takeDamage(int amount)
{
    if (amount <= 0) return;
    const int nv = std::max(0, m_health - amount);
    if (nv == m_health) return; // 已 0，再扣无变化 → 不发信号
    m_health = nv;
    emit healthChanged();
    emit damaged(amount); // 触发呈现层红闪（即便未跨整心边界也要闪，spec）
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
