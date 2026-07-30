#ifndef ENTITYMANAGER_H
#define ENTITYMANAGER_H

#include <QObject>
#include <QString>
#include <QVector3D>
#include <QtQml/qqml.h>

#include <vector>

// 统一实体管理器（t95；Entities 层）。
//
// 为后续 Mob/AI 系统铺垫的「统一实体基类」：每个实体 = {世界坐标 pos, 半径 radius, 可推动标志
// pushable, 渲染外观（kind/color）, 物理态（vy/resting）}。本轮持有一类测试生物（pushable=true），
// 玩家走碰可被推动（swept 碰撞解析玩家位移传给实体）。掉落物（src/Game 的 ItemEntityManager）机制
// 等价「pushable=false、被拾取」的实体变体——本轮不迁移既有的掉落系统（已深度集成 t35-t64：
// 拾取 / 丢弃 / 重力 / 数量 / 免拾取窗），仅在此确立统一基类形态，为后续把掉落物并入统一
// EntityManager 留形（spec「统一 EntityManager 设计」「为后续 Mob/AI 系统铺垫（统一实体基类）」）。
//
// 物理：
//   - 重力 + 地面静止（tick(dt, world)）：未 resting 的实体 vy -= g*dt（钳 -kMaxFall），按 dy 下移并
//     扫实体所在列首个实体方块 → 落到其顶面停下（resting=true，pos.y = solidCellY + 1 + kRestOffset）。
//     resting 实体复探支撑格，失支撑则续落（防挖空悬空）。机制与 ItemEntityManager::tick 同源（向下
//     只读 World::isSolid，PLAN §2 合规）。
//   - 玩家推动（resolvePlayerPush）：对每个 pushable 实体，用「玩家 AABB（XZ 矩形）vs 实体圆（XZ）」
//     求穿透，把实体沿「AABB 最近点 → 实体中心」方向推出穿透量（玩家位移传给实体）。仅在实体与玩家
//     AABB 垂直区间重叠时推动（防跨层误推——玩家从实体头顶跳过不应推开它）。推动后做世界碰撞钳制：
//     扫 mob AABB footprint 覆盖的所有格子（仿 player aabbHitsSolid；非旧版「只查中心格」），任一实体
//     方块 → 撤回该轴推动（防穿墙 + 消除斜推角落 jitter——旧版单格检查致 mob 入墙反复跳变）。
//
// 分层（PLAN §2）：本层属 Entities（位于 Game/Physics 之下、World 之上）。向下只读 World（isSolid），
// 不依赖 Renderer/Physics/QtQuick3D。tick / resolvePlayerPush 由 PlayerController（Game/Physics 层）每帧
// 调（C++ 直调，非 Q_INVOKABLE——避开 moc 对 World* 前向类型的 metatype 处理，同 ItemEntityManager
// 先例）。呈现层（Main.qml 的 Repeater）只读 count/posAt/colorAt，自发渲染，绝不反向写（PLAN §2 分层：
// 呈现层只消费 Entities 数据，同 blockBroken→粒子 / spawnItem→掉落物 模式）。
class World; // 前向声明（tick / resolvePlayerPush 只读 World::isSolid；完整定义在 .cpp include）
class EntityManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EntityManager)
    // count：当前实体数（Repeater 作 int model → 生成 0..count-1 delegate）。NOTIFY entitiesChanged
    // 驱动 spawn 后 Repeater 追加新 delegate（不重建已有 → 后续 Mob/AI 动画连续不被打断）。
    Q_PROPERTY(int count READ count NOTIFY entitiesChanged)
    // revision：实体集版本号（随 spawn / 推动位移 / 重力下落 自增）。供「触碰」绑定作 NOTIFY 触发器
    // （同 Hotbar.slotRevision / ItemEntityManager.revision 模式）——posAt/colorAt 是 Q_INVOKABLE 不被
    // NOTIFY 自动跟踪，需 { revision; posAt(i) } 显式建依赖，push/下落后绑定才重算。
    Q_PROPERTY(int revision READ revision NOTIFY entitiesChanged)

public:
    explicit EntityManager(QObject *parent = nullptr);

    int count() const { return int(m_entities.size()); }
    int revision() const { return m_revision; }

    // 实体外观种类（Q_ENUM 供 QML 渲染分流；本轮仅 Mob，预留 Item 等后续扩展）。
    enum Kind { Mob, Item };
    Q_ENUM(Kind)

    // 在方块格 (x,y,z)（整数坐标）生成一个测试生物。位置存该格中心 (x+0.5, y+0.5, z+0.5)；从高处
    // 生成时由重力 tick 落到地表（无需 caller 查地表高度）。radius=0.5（1×1 方块半宽），pushable=true
    // （玩家可推动），color=#ff5555（醒目纯色，spec「纯色突出」）。达 kCap → 跳过 + 告警（防溢出）。
    Q_INVOKABLE void spawnMob(int x, int y, int z);

    // 第 i 个实体的渲染数据（呈现层 Repeater delegate 绑它摆位 + 配色）。越界返回安全默认。
    Q_INVOKABLE QVector3D posAt(int i) const;
    Q_INVOKABLE float radiusAt(int i) const;
    Q_INVOKABLE bool pushableAt(int i) const;
    Q_INVOKABLE int kindAt(int i) const;
    Q_INVOKABLE QString colorAt(int i) const;

    // 玩家推动解析（C++ 直调；PlayerController::tick 每帧调，captured 时）。
    //   playerFeet=玩家脚底中心，halfW=玩家 AABB 半宽，height=玩家 AABB 高，world=只读世界（钳制穿墙用）。
    //   对每个 pushable 实体：垂直区间与玩家 AABB 重叠时，按「AABB(XZ) vs 实体圆(XZ)」求穿透，把实体
    //   沿 (实体中心 − AABB 最近点) 方向推出穿透量；实体陷入实体方块时撤回该轴推动（防穿墙）。
    //   任一实体 pos 真变 → dirty=true，末尾统一 bump revision + emit（驱动 QML 位置绑定重算）。
    void resolvePlayerPush(const QVector3D &playerFeet, float halfW, float height, World *world);

    // 重力 + 地面静止（C++ 直调；PlayerController::tick 每帧调，独立于捕获态——菜单/暂停时实体仍模拟）。
    //   机制同 ItemEntityManager::tick（向下只读 World::isSolid）。world=null / 无实体 → 早 return。
    void tick(qreal dt, World *world);

signals:
    void entitiesChanged(); // spawn / 推动位移 / 重力下落 触发；驱动 count/revision + QML 绑定刷新

private:
    struct Entity {
        QVector3D pos;
        float radius = 0.5f;     // 碰撞半径（1×1 方块半宽）；XZ 圆碰撞 + 垂直区间用
        bool pushable = true;    // 玩家是否可推动（掉落物变体 pushable=false，统一基类预留）
        int kind = Mob;          // 渲染分流（Mob/Item；Q_ENUM）
        QString color = QStringLiteral("#ff5555"); // 渲染配色（醒目纯色）
        float vy = 0.0f;         // 垂直速度（blocks/s；向下为负）；落地后归 0
        bool resting = false;    // 是否已落在实体方块顶面（resting 跳过重力，仅复探支撑格）
    };
    std::vector<Entity> m_entities;
    int m_revision = 0;

    static constexpr int kCap = 64;            // 实体数上限（测试用，防溢出）
    static constexpr float kGravity = 28.0f;   // 重力加速度（blocks/s²；与玩家/掉落物同值，世界手感一致）
    static constexpr float kMaxFall = 78.4f;   // 终端下落速度（blocks/s；防无限加速）
    static constexpr float kRestOffset = 0.5f; // 落地后实体中心相对支撑方块顶面的偏移（1×1 方块半宽 → 底面贴顶面）
};

#endif // ENTITYMANAGER_H
