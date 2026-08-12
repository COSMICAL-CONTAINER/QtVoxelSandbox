#ifndef PLAYERSTATE_H
#define PLAYERSTATE_H

#include <QObject>
#include <QtQml/qqml.h>

// 玩家状态视图模型（Game/Entities 层）：生命/饥饿/气泡的数值持有 + 受伤/恢复钩子。
//
// 暴露给 QML（呈现层只读）：
//   - health   当前生命 0..maxHealth（每心 2 HP；满 = 20 = 10 心）
//   - maxHealth 恒 20（10 心 × 2 HP）
//   - hunger   当前饥饿 0..maxHunger（每鼓腿 2 点；满 = 20 = 10 鼓腿）
//   - maxHunger 恒 20（10 鼓腿 × 2 点）
//   - air      当前气泡 0..maxAir（t202 溺水系统；满 = 10 气泡；眼位入水逐格减，归零后溺水扣血）
// 心/鼓腿的「每格 full/half/empty」由 QML delegate 直接据 health/hunger 算出（绑定 NOTIFY
// 自动刷新），无需把列表做成 Q_PROPERTY(QVariantList)（本工具链 moc 拒绝后者，见 lessons-learned）。
//
// 数值变更入口（Game/Physics 调用，呈现层绝不反向写）：
//   - takeDamage(amount)  受伤钩子（如掉落伤害，t22）：扣 HP，clamp 到 0；扣到 0 → 置 dead 态 + emit died
//   - heal(amount)        恢复（进食/治疗，Phase 1.1 用）：加 HP，clamp 到 maxHealth
//   - setHunger(value)    设饥饿（进食消耗，Phase 1.1 用；留接口）：clamp 到 [0, maxHunger]
//   - respawn()           t78 重生：清 dead 态 + 恢复满血满饥（玩家「立即重生」按钮调；定位由 PlayerController）
//
// 死亡态（t78）：health ≤ 0 时 dead=true 并 emit died + deadChanged；dead 期间 takeDamage 早退（不再继续扣，
// spec「且不再继续扣」）。respawn() 把 dead 翻回 false（emit deadChanged）+ 拉满血饥。呈现层据 dead 显死亡界面。
//
// 初值：满血满饥、未死。本轮（Phase 1.0）做**呈现 + 受伤钩子 + 死亡/重生**（掉落高处扣血到 0 → 死亡界面）。
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
    // t202 气泡（溺水系统）：当前气泡 0..maxAir（满 10）。眼位入水逐格减 → 归零后溺水扣血；出水回满。
    //   驱动 = PlayerController 经 airUpdated 信号 → Main.qml Connections 路由到 setAir（同 fallDamageTaken→
    //   takeDamage 模式：Physics 层算时序、Game 层持显值、呈现层路由）。maxAir 恒 10 = 10 气泡（MC 1.0 风格）。
    Q_PROPERTY(int air READ air NOTIFY airChanged)
    Q_PROPERTY(int maxAir READ maxAir CONSTANT)
    // 死亡态（t78）：health ≤ 0 → true（died 信号触发时刻置位）；respawn() 翻回 false。
    // NOTIFY=deadChanged（dead 翻 true/false 都发 → QML 绑定据 dead 显/隐死亡界面）。
    Q_PROPERTY(bool dead READ dead NOTIFY deadChanged)
    // t311 死亡原因（DeathCause）：记录致死那一击的来源，供 t312 聊天播报 / t313 死亡屏文案消费。
    //   deathCause = 致死来源枚举；deathCauseText = 该枚举对应的中文文案（通用描述词，§9 零 MC 专名：
    //   机制等价 MC 僵尸/骷髅/苦力怕 → 蹒跚者/骸骨/潜行者）。两属性 NOTIFY=deathCauseChanged（仅「致死时刻」
    //   置位 + respawn / 存档加载复位时翻动；非致死扣血不改死因）。
    Q_PROPERTY(int deathCause READ deathCause NOTIFY deathCauseChanged)
    Q_PROPERTY(QString deathCauseText READ deathCauseText NOTIFY deathCauseChanged)
    // t402 经验值（XP）累积：玩家吸收经验球（杀怪 / 冶炼产出）累加。机制等价 MC 1.0 经验
    //   （level/points 留 Phase 1.1+；本轮做裸数值累积 + 拾取钩子，呈现层只读）。NOTIFY=xpChanged
    //   驱动 F3 / HUD 经验条刷新。addXp 由呈现层 Connections 据经验球拾取语义事件（XpOrbManager::
    //   xpPickedUp）路由调用（同 fallDamageTaken→takeDamage 模式：Game 层持显值、呈现层路由）。
    Q_PROPERTY(int xp READ xp NOTIFY xpChanged)
    // t403 等级（level）：由总 xp 经 MC 1.0 风格递增曲线派生（单一权威在 Game 层，呈现层只读）。
    //   addXp 累积到「升至下一级所需」即 level++（条重置：分母跳到更大值，机制等价 MC 1.0 经验曲线）；
    //   每升一级所需 XP 单调递增（低级 2L+7、中级 5L−38、高级 9L−158，三段斜率）。升级为后续附魔台前置。
    //   NOTIFY=levelChanged 驱动 HUD 等级数刷新。**level 纯派生自 m_xp（总）→ 存档只存 xp、读档自动重建 level**，
    //   无需单独持久化 level（同 health/hunger 非派生项的持久化各自独立，level 跟随 xp 走）。
    Q_PROPERTY(int level READ level NOTIFY levelChanged)
    // t403 从当前 level 升到下一级所需 XP（曲线驱动；level 变才变）。驱动经验条填充分母。
    Q_PROPERTY(int xpToNextLevel READ xpToNextLevel NOTIFY levelChanged)
    // t403 经验条填充比 [0,1] = 当前级内 XP / xpToNextLevel。NOTIFY=xpChanged（条增长靠 addXp，而 addXp
    //   必发 xpChanged；level 仅在 addXp/setXp 内随 xp 同步变 → xpChanged 覆盖所有「条可能变」的时机，
    //   见 lessons-learned「Q_PROPERTY NOTIFY 只能挂一个信号」：挂主、补发副，此处无需补发）。
    Q_PROPERTY(qreal xpBarFraction READ xpBarFraction NOTIFY xpChanged)

public:
    // t311 死亡原因枚举（机制等价 MC 1.0 各来源死因，§9 改名为通用词）。Q_ENUM 暴露给 QML：
    //   PlayerState.Fall 等（同 EntityManager.MobPig 模式）。避免命名 None（Linux CI 下 X11 头 None 宏冲突）。
    //   Generic=未归类（默认 / takeDamage 单参兜底）；Fall=高处坠落；Suffocation=嵌实体方块窒息；
    //   Drowning=气泡归零溺水；Starvation=饥饿归零饿死；Shambler/Bones/Spider/Stalker=被对应敌对生物击败；
    //   Fire=燃烧致死（t344：触碰岩浆 / 火点燃后火伤扣血到 0）；Tnt=TNT 爆炸致死（t494：detonateTntSphere 传
    //   MobTnt 哨兵 mobType → QML onMobAttackedPlayer 映射到本死因，文案「被 TNT 炸死」）。
    enum DeathCause { Generic = 0, Fall, Suffocation, Drowning, Starvation, Shambler, Bones, Spider, Stalker, Fire, Cactus, Tnt };
    Q_ENUM(DeathCause)

    explicit PlayerState(QObject *parent = nullptr);

    int health() const { return m_health; }
    int maxHealth() const { return kMaxHealth; }
    int hunger() const { return m_hunger; }
    int maxHunger() const { return kMaxHunger; }
    int air() const { return m_air; }       // t202 当前气泡（0..maxAir）
    int maxAir() const { return kMaxAir; }  // t202 恒 10（10 气泡）
    bool dead() const { return m_dead; }
    int deathCause() const { return m_deathCause; } // t311 致死来源枚举（仅 dead 时有意义）
    QString deathCauseText() const;                  // t311 死因中文文案（通用词，§9）
    int xp() const { return m_xp; }                  // t402 当前经验值累积（总）
    int level() const { return m_level; }             // t403 当前等级（由总 xp 经曲线派生）
    int xpToNextLevel() const;                         // t403 升下一级所需 XP（曲线）
    qreal xpBarFraction() const;                       // t403 经验条填充比 [0,1]

    // 受伤钩子（Game/Physics 调用）：扣 amount HP，clamp 到 0；扣到 0 → 置 dead + emit died（且此后不再继续扣，
    // spec t78）。amount<=0 忽略（无治疗语义）。dead 期间早退（不再扣血、不再发 damaged）。
    // t311 cause=致死来源（DeathCause 枚举），默认 Generic（单参调用兜底）；每次受伤记录最近来源，致死那一击
    //   （health 扣到 ≤0）的 cause 即 deathCause。分层（PLAN §2）：Game 层持值，呈现层只读。
    // t443 致死分支同步清空 XP（xp / level / intoLevel 归零 + emit xpChanged / levelChanged；spec「死亡清空 XP 条」）。
    //   死亡地点掉部分 XP（约 1 只被动 mob 量）由呈现层 onDied spawn 经验球（需死亡位置 + XpOrbManager）。
    Q_INVOKABLE void takeDamage(int amount, int cause = Generic);
    // 恢复生命：加 amount HP，clamp 到 maxHealth。amount<=0 忽略。
    Q_INVOKABLE void heal(int amount);
    // 设饥饿值（进食消耗属 Phase 1.1；留接口）：clamp 到 [0, maxHunger]。无变化不发信号。
    Q_INVOKABLE void setHunger(int value);
    // t176 存档加载：直接设 health（不走 takeDamage 的死亡判定路径）。clamp 到 [0, maxHealth]；同时按
    //   结果同步 dead 态（health>0 → false）—— 存档玩家不可能是死亡态，但防御性置位 + emit deadChanged
    //   以免陈旧 dead=true 残留显死亡界面。无变化（值 == 当前）不发信号。
    Q_INVOKABLE void setHealth(int value);
    // t202 设气泡值：由 PlayerController 经 airUpdated 信号驱动（Physics 层算时序、Game 层持显值）。
    //   clamp 到 [0, maxAir]；无变化（值 == 当前）不发信号。t176 存档 round-trip 亦走此入口（air 可能 < max）。
    Q_INVOKABLE void setAir(int value);
    // t78 重生：清 dead 态（emit deadChanged）+ 恢复满血满饥。仅复位 PlayerState 数值；
    //   玩家定位（传回出生点）由 PlayerController::respawn() 负责（分层：状态属 Game 层，定位属 Physics 层）。
    //   呈现层「立即重生」按钮同时调本方法 + PlayerController.respawn()。
    Q_INVOKABLE void respawn();
    // t402 累积经验值：加 amount XP（amount<=0 忽略）。仅累积数值 + emit xpChanged（呈现层 F3 /
    //   经验条刷新）；level/points 留 Phase 1.1+。由呈现层 Connections 据经验球拾取语义事件
    //   （XpOrbManager::xpPickedUp）路由调用（同 fallDamageTaken→takeDamage 模式）。
    Q_INVOKABLE void addXp(int amount);
    // t402 设经验值（存档加载用；与 setHealth/setHunger 同模式）：直接设（不走任何派生判定），
    //   clamp 到 >=0；无变化不发信号。供 Main.qml::applyPlayerState 在 playerState.xp 灌入存档值。
    Q_INVOKABLE void setXp(int value);
    // t474 消耗 XP 等级（附魔台每次附魔消耗 1/2/3 级；机制等价 MC 1.0 附魔消耗 player level）。
    //   若当前 level < amount → 返 false（余额不足，caller 不应推进附魔）；否则把总 xp 截到「升至
    //   (level-amount) 所需」即 m_xp = xpTotalForLevel(newLevel)，级内进度（m_intoLevel）随之归零
    //   （机制等价 MC 附魔后「等级降 N、级内经验条清空」）。重算后 emit xpChanged（条 / 总值刷新）
    //   + levelChanged（HUD 等级数降）。分层（PLAN §2）：Game 层持显值 + 派生，呈现层（EnchantingTableUI）
    //   只读消费（点附魔槽 → 调本方法 → 据 bool 决定是否扣 lapis / 提示）。
    Q_INVOKABLE bool spendLevels(int amount);

signals:
    void healthChanged();
    void hungerChanged();
    void airChanged(); // t202 气泡值变（驱动 QML 气泡条刷新 / 显隐；值真变才发）
    void deadChanged(); // dead 翻转（true/false 都发；驱动 QML 死亡界面显隐，t78）
    // t78 死亡一次性事件：health 首次降到 ≤0 时发（与 deadChanged 同帧发；died 语义=「刚死」，供呈现层
    //   释放指针 / 关面板等一次性副作用；deadChanged 驱动持续可见性绑定）。
    void died();
    // t311 致死来源变更（致死时刻置位 / respawn + 存档加载复位时翻动；驱动 deathCause / deathCauseText 绑定）。
    void deathCauseChanged();
    // t402 经验值累积变更（addXp / setXp 真变时发；驱动 F3 / 经验条绑定刷新）。
    void xpChanged();
    // t403 等级变更（addXp 跨越曲线阈值时；setXp 读档重建时）。驱动 HUD 等级数 / 经验条分母刷新。
    void levelChanged();
    // 受伤闪烁触发（t51）：takeDamage 实扣 HP 时发；呈现层（Main.qml）Connections 据此启动
    // 红色半透全屏叠层的 alpha 0.4→0 淡出动画（~600ms）。amount = 本次请求扣血量（不计 clamp 截断）。
    // 与 healthChanged 分离：healthChanged 驱动心条数值刷新（每半心切态），damaged 驱动一次性的视觉闪烁
    // （即便扣血未跨整心边界也要闪）。分层（PLAN §2）：Game 层发语义事件，呈现层只消费（同 fallDamageTaken）。
    void damaged(int amount);

private:
    static constexpr int kMaxHealth = 20; // 10 心 × 2 HP
    static constexpr int kMaxHunger = 20; // 10 鼓腿 × 2 点
    static constexpr int kMaxAir = 10;    // t202 10 气泡（MC 1.0 风格溺水系统）

    int m_health = kMaxHealth; // 初值满血
    int m_hunger = kMaxHunger; // 初值满饥
    int m_air = kMaxAir;       // t202 初值满气泡（眼位入水逐格减；归零溺水扣血；出水回满）
    bool m_dead = false;       // t78 死亡态（health ≤ 0 → true；respawn 翻回 false）
    int m_lastCause = Generic; // t311 最近一次受伤来源（致死那一击写入 m_deathCause）
    int m_deathCause = Generic;// t311 致死来源（health 扣到 ≤0 时 = m_lastCause；respawn / 存档加载复位 Generic）
    int m_xp = 0;              // t402 经验值累积（吸收经验球累加；初值 0）
    int m_level = 0;           // t403 当前等级（由 m_xp 经曲线派生；初值 0）
    int m_intoLevel = 0;       // t403 当前级内已累积 XP（m_xp − xpTotalForLevel(m_level)；初值 0）

    // t403 MC 1.0 风格递增曲线（机制等价 MC，三段斜率；need 单调递增）：从 level 升到 level+1 所需 XP。
    //   低级（0..14）2L+7、中级（15..29）5L−38、高级（30+）9L−158。level<0 → 0（防御）。
    static int xpNeedForLevel(int level);
    // t474 升至 level 所需总 XP（= sum_{i=0..level-1} xpNeedForLevel(i)）。spendLevels 据此把 m_xp 截到
    //   「恰好升至 newLevel 所需」总量（级内进度归零，机制等价 MC 附魔后级内条清空）。
    static int xpTotalForLevel(int level);
    // 据 m_xp（总）重算 m_level + m_intoLevel；level 真变才 emit levelChanged（驱动 HUD 等级数 / 条分母）。
    // addXp / setXp 在改 m_xp 后调（xpChanged + levelChanged 同帧发，QML 绑定齐刷）。
    void recomputeLevel();
};

#endif // PLAYERSTATE_H
