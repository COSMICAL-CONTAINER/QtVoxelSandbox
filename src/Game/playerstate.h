#ifndef PLAYERSTATE_H
#define PLAYERSTATE_H

#include <QObject>
#include <QtQml/qqml.h>

// 玩家状态视图模型（Game/Entities 层）：生命/饥饿的数值持有 + 受伤/恢复钩子。
//
// 暴露给 QML（呈现层只读）：
//   - health   当前生命 0..maxHealth（每心 2 HP；满 = 20 = 10 心）
//   - maxHealth 恒 20（10 心 × 2 HP）
//   - hunger   当前饥饿 0..maxHunger（每鼓腿 2 点；满 = 20 = 10 鼓腿）
//   - maxHunger 恒 20（10 鼓腿 × 2 点）
// 心/鼓腿的「每格 full/half/empty」由 QML delegate 直接据 health/hunger 算出（绑定 NOTIFY
// 自动刷新），无需把列表做成 Q_PROPERTY(QVariantList)（本工具链 moc 拒绝后者，见 lessons-learned）。
//
// 数值变更入口（Game/Physics 调用，呈现层绝不反向写）：
//   - takeDamage(amount)  受伤钩子（如掉落伤害，t22）：扣 HP，clamp 到 0
//   - heal(amount)        恢复（进食/治疗，Phase 1.1 用）：加 HP，clamp 到 maxHealth
//   - setHunger(value)    设饥饿（进食消耗，Phase 1.1 用；留接口）：clamp 到 [0, maxHunger]
//
// 初值：满血满饥。本轮（Phase 1.0）做**呈现 + 受伤钩子**（掉落高处扣血，仅 Survival）；
// 真实战斗/饥饿消耗/死亡重生属 Phase 1.1，本类只暴露接口供后续接入。
//
// 分层（PLAN §2）：Game/Entities 层，只依赖 Core（Qt）。不依赖 Renderer/World/Physics。
// §4 法律 + §9：零 MC 名词；心/鼓腿图标在呈现层自绘原创（见 VitalIcon.qml，非 MC GUI PNG）。
class PlayerState : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PlayerState)
    Q_PROPERTY(int health READ health NOTIFY healthChanged)
    Q_PROPERTY(int maxHealth READ maxHealth CONSTANT)
    Q_PROPERTY(int hunger READ hunger NOTIFY hungerChanged)
    Q_PROPERTY(int maxHunger READ maxHunger CONSTANT)

public:
    explicit PlayerState(QObject *parent = nullptr);

    int health() const { return m_health; }
    int maxHealth() const { return kMaxHealth; }
    int hunger() const { return m_hunger; }
    int maxHunger() const { return kMaxHunger; }

    // 受伤钩子（Game/Physics 调用）：扣 amount HP，clamp 到 0。amount<=0 忽略（无治疗语义）。
    Q_INVOKABLE void takeDamage(int amount);
    // 恢复生命：加 amount HP，clamp 到 maxHealth。amount<=0 忽略。
    Q_INVOKABLE void heal(int amount);
    // 设饥饿值（进食消耗属 Phase 1.1；留接口）：clamp 到 [0, maxHunger]。无变化不发信号。
    Q_INVOKABLE void setHunger(int value);

signals:
    void healthChanged();
    void hungerChanged();

private:
    static constexpr int kMaxHealth = 20; // 10 心 × 2 HP
    static constexpr int kMaxHunger = 20; // 10 鼓腿 × 2 点

    int m_health = kMaxHealth; // 初值满血
    int m_hunger = kMaxHunger; // 初值满饥
};

#endif // PLAYERSTATE_H
