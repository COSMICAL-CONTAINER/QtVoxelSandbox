#ifndef TOOLDROPINSTANCING_H
#define TOOLDROPINSTANCING_H

#include <QtQuick3D/QQuick3DInstancing>
#include <QtQml/qqml.h>

#include <QByteArray>
#include <QColor>
#include <QElapsedTimer>
#include <QQuaternion>
#include <QTimer>
#include <QVector>
#include <QVector3D>

#include "itementitymanager.h" // Q_PROPERTY(ItemEntityManager*) 的 metatype 完整性要求完整类型（Qt 6.11 moc
                               //   TypeMustBeComplete）——blockdropinstancing.h / glowshellinstancing.h 的
                               //   include 全量头先例照搬（前向声明模式靠 mocs_compilation include 顺序侥幸存活）。

// t1041（R19.23）掉落物 instancing 批 4 收官 ①：**工具 3D 族**（isTool3DDrop：五类几何镐/锄/斧/铲/剑 +
// 弓；tier 色映射逐字对齐 Main.qml 工具 delegate 五分支 t472/t557/t589）从「每实体一个 Repeater delegate
// 内联 3D 几何 + per-entity 材质」压成 **per-id 桶 × 单 Model 单实例表**（1 draw / 桶；Main.qml
// toolDropInstHost，批 1/2 blockDropInstHost / blockShapeInstHost 同构）。族内 per-id 差异只在
// 几何（toolType 决定 Pickaxe/Hoe/Axe/Shovel/Sword/Bow 几何——QML 桶 Model 按桶 id 的 toolType 切
// 单例几何）与 tier 色（同 id 同 tier → 常量色）；tier 色×天光走 **per-instance color**（t1039
// GlowShellInstancing color 实例表先例；材质本体白基色，实例色乘法 = 旧「材质 baseColor = tier 色 ×
// tintBySkyLight」的逐位等效面）。
//
// **弓弦（BowStringGeometry）双表**：旧 delegate 弓 Model 带子节点白弦 Model（t330 蜘蛛丝白 245，
// 不随 tier）——instancing 不承载 per-instance 子树（t1038 书-台教训定夺先例），弦独立成第二 Model +
// 本类 **stringPass=true** 第二实例表：变换口径与弓身表逐字同参（同 slot 集合同 pos/bob/rotY/scale 0.45），
// 仅色不同（stringPass → 245×k 蜘蛛丝白；缺省 → tier 色×k）。两 feeder 同帧构造（同一 Repeater delegate
// 实例化期），m_clock 起点偏移 ms 级 = 批 1/2/3 跨 feeder 相位脱锁已登记的接受口径（t1039 类头同款）。
//
// **动画下沉**（批 1/2/3 同公式源，逐字对齐旧 delegate entRoot 动画）：
//   rotY = fmod(ph / 3.0, 1) × 360（旧 `NumberAnimation on rotY { to:360; duration:3000 }`，工具 3D
//        分支继承 entRoot 自转——无 billboard 抵消）；
//   bob  = 0.075 × (1 − cos(pi·s))，s = fmod(ph, 2)（旧 bobY 0↔0.15 两段 InOutSine 1s/leg）；
//   ph = t + slot × 0.37s（批 1/2 的「旧相位 = 创建时刻」等价视觉错开口径）。
//   scale 0.45（旧工具 Model scale 0.45）。
//
// **收纳过滤（单一权威）**：ItemEntityManager::isTool3DDrop(familyId)——桶粒度 = itemId，谓词只依赖
//   familyId（桶内全实例同 id）→ getInstanceBuffer 循环外单次判定 + hasLiveMember 同参（review0910 #3
//   谓词一致性纪律）。剪刀/打火石（图标 billboard 族）、钓鱼竿 8（无掉落分支）、材料段、方块段恒不入。
//
// **空转门（t1032 同款）**：16ms QTimer 构造**不启钟**——familyId<=0 / 非本族 id / 桶内活体 0 不走钟；
//   活跃沿 setManager / setFamilyId / setStringPass / manager entitiesChanged → start + markDirty 兜底；
//   空转沿 → stop（探针 probeTickerActive 钉）。skyLight / minLight 沿只 markDirty（色调重取，无时钟语义）。
//
// **分层（PLAN §2）**：Game 层呈现适配器——向下只读 ItemEntityManager（pos/itemId/alive）+ ToolRegistry
//   纯表（tier）+ Qt Quick3D 公开类型（无自定义 shader——PLAN §2-A RHI 囚笼合规）。拾取 / despawn /
//   合并 / 掉落物理零改动（只读渲染态）。
class ToolDropInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ToolDropInstancing)
    // 数据源（只读）：pos / itemId / alive 逐槽读。切世界 clearAll 只释放槽位 → 实例表自然清空
    //（alive=false 槽不进表），无需本类参与重置（同 BlockDropInstancing / GlowShellInstancing）。
    Q_PROPERTY(ItemEntityManager *manager READ manager WRITE setManager NOTIFY managerChanged)
    // 本桶收纳的工具 id（familyId）。<=0 = 未指派 → 恒空表（QML 桶池未用槽位）。
    Q_PROPERTY(int familyId READ familyId WRITE setFamilyId NOTIFY familyIdChanged)
    // 天光乘子（QML 绑 worldClock.skyLight，[0,1]）；缺省 1.0 = 正午全亮（headless 探针确定态）。
    //   k = minLight + (1−minLight)×skyLight（Main.qml tintBySkyLight floor 公式逐字对齐）。
    Q_PROPERTY(qreal skyLight READ skyLight WRITE setSkyLight NOTIFY skyLightChanged)
    // 夜间 floor（QML 绑 window.minLight；Main.qml 缺省 0.4 同值）。
    Q_PROPERTY(qreal minLight READ minLight WRITE setMinLight NOTIFY minLightChanged)
    // 弓弦通道（Main.qml 弓桶第二 Model）：true = 实例表色为蜘蛛丝白 245×k（t330，不随 tier）；
    //   false（缺省）= tier 色×k。变换口径与弓身表逐字同参。
    Q_PROPERTY(bool stringPass READ stringPass WRITE setStringPass NOTIFY stringPassChanged)

public:
    explicit ToolDropInstancing(QQuick3DObject *parent = nullptr);

    ItemEntityManager *manager() const { return m_manager; }
    void setManager(ItemEntityManager *m);

    int familyId() const { return m_familyId; }
    void setFamilyId(int id);

    qreal skyLight() const { return m_skyLight; }
    void setSkyLight(qreal v);

    qreal minLight() const { return m_minLight; }
    void setMinLight(qreal v);

    bool stringPass() const { return m_stringPass; }
    void setStringPass(bool stringPass);

    // 矩阵探针专用（t1027/t1032/t1039 同款）：当前实例表条数 / 第 k 条内容（越界 false）。headless 直调
    //   getInstanceBuffer 后解码 InstanceTableEntry——不依赖渲染 sync / QQuick3D 私有态。
    //   批 4 扩展：outColor 回带 per-instance color（tier 色/弓弦白 × 天光 k——批 4 核心断言面）。
    int probeInstanceCount();
    bool probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale, QQuaternion *outRot,
                         QColor *outColor);
    // 空转门探针（t1032 同款）：动画 QTimer 是否在走。空转（未指派 / 非本族 / 活体 0）必须 false。
    Q_INVOKABLE bool probeTickerActive() const { return m_ticker.isActive(); }

signals:
    void managerChanged();
    void familyIdChanged();
    void skyLightChanged();
    void minLightChanged();
    void stringPassChanged();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    void tick(); // QTimer 16ms：动画相位推进 → markDirty（下帧 sync 重取表）
    // 空转门（t1032 模式）：按「familyId>0 且为本族且桶内有活体」启/停钟。活跃沿 start + markDirty
    //   兜底防丢帧，空转沿 stop。沿源 = setManager / setFamilyId / setStringPass / manager
    //   entitiesChanged（活体集变更完备沿）。
    void refreshTicker();
    bool hasLiveMember() const;
    // manager 活体集变更沿（entitiesChanged 直连）：空转门重估 + markDirty 兜底。
    void onManagerEntitiesChanged();

    ItemEntityManager *m_manager = nullptr;
    int m_familyId = 0;
    qreal m_skyLight = 1.0;   // QML 绑 worldClock.skyLight；缺省正午（探针确定态）
    qreal m_minLight = 0.4;   // QML 绑 window.minLight；Main.qml 同缺省 0.4
    bool m_stringPass = false; // 弓弦通道（色 245 白，不随 tier；变换同弓身表）
    QTimer m_ticker;           // 动画驱动（16ms Precise；空转门——构造不启钟，t1032 同款）
    QElapsedTimer m_clock;     // 动画相位时钟（构造起点，秒）
    QByteArray m_buffer;       // 实例表字节缓冲（复用，零稳态分配）
    QVector<int> m_liveSlots;  // 本桶活体槽快照（填表用，复用）
};

#endif // TOOLDROPINSTANCING_H
