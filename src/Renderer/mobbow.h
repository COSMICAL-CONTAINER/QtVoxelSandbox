#ifndef MOBBOW_H
#define MOBBOW_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 骸骨（Bones）持用弓几何（t331；Renderer 层）。独立于 MobModel —— 弓需**木褐色**材质（区别骸骨灰白体色），
// 单一 QQuick3DGeometry 单材质无法同几何双色 → 把弓从 MobModel mobType 5 抽出为独立几何 + QML 独立 Model
// 配木色 PrincipledMaterial（机制等价 MC 1.0 骷髅木弓 vs 骨色身；t331 修「弓误用骨白」）。垂直握把 + 上下肢
// 回弯 C 形 + 凹侧（+Z 射手侧）弓弦；弓平面 = Y-Z（X=0），握把中心 = 原点 → 消费点用 Model position 摆到右手。
//
// t331 拉弓动画（draw 动作）：drawAmount（0..1，由 EntityManager::drawAmountAt 驱动）把弓弦朝 +Z（射手方向）
// 后移 + 弓肢回弯角略增（蓄力前凸感）。drawAmount=0 = 松弦静态；=1 = 满拉（弦后移、肢前凸）。setDrawAmount
// 触发 rebuild（每 active 帧重算一次；非活动态早退不重算，同 MobModel.setWalkPhase 模式）。
//
// 顶点：pos(3) = 12 字节（pos-only，同 BowGeometry / SwordGeometry；颜色由 QML baseColor 给，不进顶点）。
// 4 盒 × 24 = 96 顶点 / 144 索引。CCW 朝外。分层（PLAN §2）：Renderer 只产出几何；drawAmount / 拉弓态在
// Entities 层（EntityManager aimTimer）；着色在呈现层（Main.qml 木色 baseColor）。依赖只向下。
// 与 bow.h / mobmodel.h 同层同风格。
class MobBowGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MobBowGeometry)
    // t331 拉弓进度 0..1（EntityManager::drawAmountAt 驱动）：setDrawAmount 触发 rebuild 把弦后拉 + 肢增弯。
    Q_PROPERTY(float drawAmount READ drawAmount WRITE setDrawAmount NOTIFY drawAmountChanged)

public:
    explicit MobBowGeometry(QQuick3DObject *parent = nullptr);

    float drawAmount() const { return m_drawAmount; }
    void setDrawAmount(float amount);

signals:
    void drawAmountChanged();

private:
    void rebuild(); // 据 m_drawAmount 建 4 盒弓几何（握把 + 上/下肢 + 弓弦），拉弓时弦后移 + 肢增弯。

    float m_drawAmount = 0.0f; // 拉弓进度 0..1（0=松弦、1=满拉）；setDrawAmount 触发 rebuild。
};

#endif // MOBBOW_H
