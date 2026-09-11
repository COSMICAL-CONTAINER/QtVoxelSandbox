#ifndef BILLBOARDDROPINSTANCING_H
#define BILLBOARDDROPINSTANCING_H

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
                               //   TypeMustBeComplete）——blockdropinstancing.h 的 include 全量头先例照搬。

// t1041（R19.23）掉落物 instancing 批 4 收官 ②③：**billboard 图标族双池单 feeder 类**——
//   ⑤ 工具/材料图标族（isIconBillboardDrop：剪刀/打火石 ToolIcon sourceItem + 材料段〔含护甲〕
//      MaterialIcon sourceItem）与 ③ 异形方块图标族（isBlockIconBillboardDrop：partial/cross/床 非 3D，
//      per-id 图标 PNG iconSourceForBlock）各一个 QML 桶池（Main.qml itemIconInstHost /
//      blockIconInstHost），共用本 feeder 类：两池同构（BillboardQuad 几何 + per-id 贴图材质），
//      仅收纳谓词与贴图形态不同——由 **itemIconFamily** 属性切换（批 2 shapeFamily 模式开关先例）：
//      false（缺省）= 异形方块图标族；true = 工具/材料图标族。
//
// **per-id 贴图 → per-id 桶（选型登记，PLAN §2-A 合规）**：instanced Model 全实例共享同一材质——per-id
//   贴图差异的两条路：texture atlas + UV 进实例表需自定义 shader（RHI 囚笼违规禁区）✗；per-id host 桶
//   （每活跃 id 占一桶，桶内全实例共享同一 Texture/材质 = 同 id 合批 1 draw/桶）✓——与批 1/2 同构，
//   本批选型 = **per-id 桶池**，无自定义 shader。
//
// **朝相机旋转进实例表**：旧 billboard delegate 的世界朝向 = Ry(camYaw)·Rx(camPitch)（本 Model 是
//   entRoot 自转子节点，本地 yaw 减 rotY 抵消继承；QtQuick3D eulerRotation 按 fromEulerAngles
//   =Ry·Rx 当 roll=0——Main.qml 材料段注释同款推导）→ 本类 **camPitch / camYaw** Q_PROPERTY（QML 绑
//   cam.eulerRotation.x/.y）进实例表 rotation=(camPitch, camYaw, 0)；相机沿 setter markDirty（无时钟
//   语义）。billboard **不自转**（旧分支显式抵消 rotY）——与批 1/2/3 的纯 Y 自转不同，本表无 rotY 项。
//
// **动画下沉**（批 1/2/3 同公式源，billboard 只剩 bob）：
//   bob = 0.075 × (1 − cos(pi·s))，s = fmod(ph, 2)（旧 bobY 0↔0.15 两段 InOutSine 1s/leg）；
//   ph = t + slot × 0.37s（批 1/2 的「旧相位 = 创建时刻」等价视觉错开口径）。
//   scale 0.3（旧 billboard Model scale 0.3）；color 白（天光乘子 terrainLight(skyLight) 留在共享材质
//   baseColor——族内无 per-instance 色差，白 = 不调制，批 1 同口径）。
//
// **收纳过滤（单一权威）**：aliveAt(i) && itemId == familyId && 谓词（itemIconFamily 缺省 false =
//   ItemEntityManager::isBlockIconBillboardDrop / true = isIconBillboardDrop）。楼梯 16 在异形族、
//   火把 13 在 3D 族不进 billboard 池、剪刀/材料不进异形池——跨池互斥探针面。
//
// **空转门（t1032 同款）**：16ms QTimer 构造**不启钟**——familyId<=0 / 非本族 id / 桶内活体 0 不走钟；
//   活跃沿 setManager / setFamilyId / setItemIconFamily / manager entitiesChanged → start + markDirty
//   兜底；空转沿 → stop（探针 probeTickerActive 钉）。camPitch / camYaw 沿只 markDirty（朝向重取）。
//
// **分层（PLAN §2）**：Game 层呈现适配器——向下只读 ItemEntityManager（pos/itemId/alive）+ Qt Quick3D
//   公开类型（无自定义 shader——PLAN §2-A RHI 囚笼合规）。拾取 / despawn / 合并 / 掉落物理零改动。
class BillboardDropInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BillboardDropInstancing)
    // 数据源（只读）：pos / itemId / alive 逐槽读。切世界 clearAll 只释放槽位 → 实例表自然清空。
    Q_PROPERTY(ItemEntityManager *manager READ manager WRITE setManager NOTIFY managerChanged)
    // 本桶收纳的图标族 id（familyId）。<=0 = 未指派 → 恒空表（QML 桶池未用槽位）。
    Q_PROPERTY(int familyId READ familyId WRITE setFamilyId NOTIFY familyIdChanged)
    // t1041 收纳模式：false（缺省）= 收 isBlockIconBillboardDrop 异形方块图标族（blockIconInstHost 池，
    //   贴图 = iconSourceForBlock 图标 PNG）；true = 收 isIconBillboardDrop 工具/材料图标族
    //   （itemIconInstHost 池，贴图 = ToolIcon/MaterialIcon Canvas sourceItem）。
    Q_PROPERTY(bool itemIconFamily READ itemIconFamily WRITE setItemIconFamily NOTIFY itemIconFamilyChanged)
    // 相机俯仰/偏航（QML 绑 cam.eulerRotation.x/.y）：实例表 rotation=(camPitch, camYaw, 0)——
    //   旧 billboard delegate 世界朝向 = Ry(camYaw)·Rx(camPitch) 的逐位等效面。缺省 0（headless 确定态）。
    Q_PROPERTY(qreal camPitch READ camPitch WRITE setCamPitch NOTIFY camPitchChanged)
    Q_PROPERTY(qreal camYaw READ camYaw WRITE setCamYaw NOTIFY camYawChanged)

public:
    explicit BillboardDropInstancing(QQuick3DObject *parent = nullptr);

    ItemEntityManager *manager() const { return m_manager; }
    void setManager(ItemEntityManager *m);

    int familyId() const { return m_familyId; }
    void setFamilyId(int id);

    bool itemIconFamily() const { return m_itemIconFamily; }
    void setItemIconFamily(bool itemIconFamily);

    qreal camPitch() const { return m_camPitch; }
    void setCamPitch(qreal v);

    qreal camYaw() const { return m_camYaw; }
    void setCamYaw(qreal v);

    // 矩阵探针专用（t1027/t1032/t1039 同款）：当前实例表条数 / 第 k 条内容（越界 false）。
    int probeInstanceCount();
    bool probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale, QQuaternion *outRot);
    // 空转门探针（t1032 同款）：动画 QTimer 是否在走。空转（未指派 / 非本族 / 活体 0）必须 false。
    Q_INVOKABLE bool probeTickerActive() const { return m_ticker.isActive(); }

signals:
    void managerChanged();
    void familyIdChanged();
    void itemIconFamilyChanged();
    void camPitchChanged();
    void camYawChanged();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    void tick(); // QTimer 16ms：bob 相位推进 → markDirty（下帧 sync 重取表）
    // 空转门（t1032 模式）：按「familyId>0 且为本族且桶内有活体」启/停钟。
    void refreshTicker();
    bool hasLiveMember() const;
    // manager 活体集变更沿（entitiesChanged 直连）：空转门重估 + markDirty 兜底。
    void onManagerEntitiesChanged();

    ItemEntityManager *m_manager = nullptr;
    int m_familyId = 0;
    bool m_itemIconFamily = false; // false = 异形方块图标族（③）/ true = 工具/材料图标族（⑤）
    qreal m_camPitch = 0.0;        // QML 绑 cam.eulerRotation.x
    qreal m_camYaw = 0.0;          // QML 绑 cam.eulerRotation.y
    QTimer m_ticker;               // 动画驱动（16ms Precise；空转门——构造不启钟，t1032 同款）
    QElapsedTimer m_clock;         // bob 相位时钟（构造起点，秒）
    QByteArray m_buffer;           // 实例表字节缓冲（复用，零稳态分配）
    QVector<int> m_liveSlots;      // 本桶活体槽快照（填表用，复用）
};

#endif // BILLBOARDDROPINSTANCING_H
