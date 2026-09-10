#ifndef BLOCKDROPINSTANCING_H
#define BLOCKDROPINSTANCING_H

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
                               //   TypeMustBeComplete）——xporbinstancing.h 的前向声明模式靠 mocs_compilation
                               //   include 顺序侥幸存活（moc_xporbmanager.cpp 恰排 moc_xporbinstancing.cpp 前），
                               //   本头按字母序排 moc_itementitymanager.cpp 之前 → 直接 include 全量头。

// t1027（R19.21）掉落物 instancing 治理首批：**方块整立方掉落族**（ItemEntityManager::isPlainCubeDrop，
// 挖掘产出主面：泥土 / 石头 / 圆石 / 木板 / 砂 / 羊毛 16 色 …）从「每实体一个 Repeater delegate 内联
// BlockCube geometry + 材质实例」压成 **per-id 桶 × 单 Model 单实例表**（1 draw / 桶 / 全族活跃 id 子集）。
//
// 族划分（本批登记，治理全量分步）：掉落物 delegate 按「同族同贴图 / 同几何」分桶——
//   ① 方块整立方族（本批）：BlockCube + 共享 voxelAtlas。族内 per-id 差异只在几何 UV（BlockCube 按
//      blockId 烘 per-face 图集瓦片），故桶粒度 = itemId：每个活跃整立方 id 占一个桶位，桶内全部实例
//      共享同一 BlockCube 几何 + 同一材质 → 同 id 掉落物合批。
//   ② 3D 形状族（isItem3DFamily 25 id，ItemShapeGeometry + voxelAtlas）——**t1032（R19.22）本批收编**：
//      同一批 1 模式 per-id 桶（Main.qml blockShapeInstHost），本 feeder 以 shapeFamily=true 切换
//      收纳过滤到 isItem3DFamily（两侧谓词同源纪律同批 1）；桶 Model 几何 = ItemShapeGeometry per-id
//      （单 Model 单几何单材质，桶内全实例经 instancing 共享 = 同 id 全实例同几何）。附魔台 94 台顶
//      悬浮小书不进实例表（instancing 无法承载 per-instance 子树）→ 保留 delegate 逐实体渲染
//      （Main.qml dropBookNode 移出 shape Model 成 entRoot 直接子节点，世界变换逐位不变）。
//   ③ 异形 billboard 族（partial/cross/bed 非 3D，BillboardQuad + per-id 图标 Texture）——登记分步。
//   ④ 工具 3D 族（五类几何 + 弓，tier 色 per-instance color 可承载）——登记分步。
//   ⑤ 工具 / 材料 billboard 族（ToolIcon / MaterialIcon sourceItem per-id）——登记分步。
//   ⑥ 光晕壳族（UnitCube 半透壳，全族横切；per-instance color + 呼吸 opacity）——登记分步。
//
// 机制（t858 XpOrbInstancing 同款公开 API 模式，无自定义 shader——PLAN §2-A RHI 囚笼合规）：
//   Model.instancing 指向本类实例，渲染 sync 经 getInstanceBuffer() 取实例表（每实例 80B）；本类 16ms
//   QTimer markDirty → 下帧 sync 重算全表（桶内活体 ≤ kCap=200 × 80B ≈ 16KB/帧 上界，常态远低）。
//
// **空转门（t1032，review0910 #3 收口；批 1/批 2 两池同款）**：16ms QTimer 构造**不启钟**——
//   familyId<=0（未指派桶）或桶内活体 0 时不走钟（旧恒走钟 = 空场景 ~N 桶 × 60 次/秒空唤醒 +
//   渲染 sync 每帧重取空表）。活跃沿由 refreshTicker 统一重估：setManager / setFamilyId /
//   setShapeFamily / manager entitiesChanged（有桶内活体）→ start + markDirty 兜底防丢帧；
//   空转沿 → stop（tick 不再 markDirty 的可观测面 = m_ticker.isActive()==false，探针
//   probeTickerActive 钉）。entitiesChanged 是活体集变更的完备沿（spawn / removeAt / setCountAt /
//   clearAll / despawn / 物理位移全经 notifyChanged 单点 emit）。t858 XpOrbInstancing 同模式先例
//   恒走钟维持现状（单 feeder 成本低，登记后续顺手收口，不在 t1032 范围）。
//
// **动画下沉**（旧 delegate 的 QML NumberAnimation/SequentialAnimation → C++ 解析式，公式逐字对齐）：
//   rotY = fmod(ph / 3.0, 1) × 360（旧 `NumberAnimation on rotY { to:360; duration:3000 }`）；
//   bob  = 0.075 × (1 − cos(pi·s))，s = fmod(ph, 2)（旧 0↔0.15 两段 InOutSine 1s/leg；InOutSine
//   out-and-back = 0.5·(1−cos(pi·s)) 三角波，同 t858 推导）；per-slot 错峰相位 = slot × 0.37s（旧
//   delegate 相位 = 各自创建时刻的等价视觉错开）。槽复用 = 新实体相位连续（同 xp 球取舍：无球时冻结
//   才会出生跳变，恒走钟无此问题）。
//
// **分层（PLAN §2）**：Game 层呈现适配器——向下只读 ItemEntityManager（pos/itemId/alive）+ Qt Quick3D
//   公开类型；家族谓词用 ItemEntityManager 静态权威（isPlainCubeDrop / isItem3DFamily），QML delegate
//   排除侧与 C++ feeder 收纳侧逐位同判（否则双渲 z-fight 或丢渲）。拾取 / despawn / 合并 / 掉落物理
//   全在 ItemEntityManager / PlayerController（C++），本类只读渲染态，零数据链改动。
//
// **桶指派**在 QML（Main.qml blockDropInstHost）：entitiesChanged → 普通 JS handler 重算活跃 id →
//   稳定指派进固定桶池（在用桶保持、新 id 按活体数降序补空位、消失 id 释放；t976/t977 AOT 教训——
//   instancing 表更新走信号 handler 不走静态绑定）。桶池满（本批 8）→ 未入桶 id 走 delegate 旧路径
//   （Main.qml plain-cube Model visible 追加 hasBucket 排除），优雅降级不丢渲。
class BlockDropInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BlockDropInstancing)
    // 数据源（只读）：pos / itemId / alive 逐槽读。切世界 clearAll 只释放槽位 → 实例表自然清空
    //（alive=false 槽不进表），无需本类参与重置（同 XpOrbInstancing）。
    Q_PROPERTY(ItemEntityManager *manager READ manager WRITE setManager NOTIFY managerChanged)
    // 本桶收纳的整立方方块 id（familyId）。<=0 = 未指派 → 恒空表（QML 桶池未用槽位）。
    Q_PROPERTY(int familyId READ familyId WRITE setFamilyId NOTIFY familyIdChanged)
    // t1032（R19.22）批 2 收纳模式：false（缺省，批 1 行为逐位不变）= 收 isPlainCubeDrop 整立方族
    //   （blockDropInstHost 池）；true = 收 isItem3DFamily 3D 形状族（blockShapeInstHost 池）。
    //   两模式过滤谓词同为 ItemEntityManager 静态单一权威（QML delegate 排除侧逐位同判）。
    Q_PROPERTY(bool shapeFamily READ shapeFamily WRITE setShapeFamily NOTIFY shapeFamilyChanged)

public:
    explicit BlockDropInstancing(QQuick3DObject *parent = nullptr);

    ItemEntityManager *manager() const { return m_manager; }
    void setManager(ItemEntityManager *m);

    int familyId() const { return m_familyId; }
    void setFamilyId(int id);

    bool shapeFamily() const { return m_shapeFamily; }
    void setShapeFamily(bool shape);

    // 矩阵探针专用（t1027）：当前实例表条数 / 第 k 条内容（越界 false）。headless 直调
    // getInstanceBuffer 后解码 InstanceTableEntry——不依赖渲染 sync / QQuick3D 私有态（t858 同款）。
    int probeInstanceCount();
    bool probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale, QQuaternion *outRot);
    // t1032 空转门探针：动画 QTimer 是否在走。空转（familyId<=0 / 桶内活体 0）必须 false——
    //   「tick 不再 markDirty」的 headless 可观测面（钟停 = 无周期 markDirty 源）。
    Q_INVOKABLE bool probeTickerActive() const { return m_ticker.isActive(); }

signals:
    void managerChanged();
    void familyIdChanged();
    void shapeFamilyChanged();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    void tick(); // QTimer 16ms：动画相位推进 → markDirty（下帧 sync 重取表）
    // t1032 空转门：按「familyId>0 且桶内（按当前收纳谓词）有活体」启/停钟。活跃沿 start +
    //   markDirty 兜底防丢帧，空转沿 stop。沿源 = setManager / setFamilyId / setShapeFamily /
    //   manager entitiesChanged（活体集变更完备沿，见类头注释）。
    void refreshTicker();
    bool hasLiveMember() const;
    // manager 活体集变更沿（entitiesChanged 直连）：空转门重估 + markDirty 兜底。
    void onManagerEntitiesChanged();

    ItemEntityManager *m_manager = nullptr;
    int m_familyId = 0;
    bool m_shapeFamily = false; // t1032 批 2：false = 整立方族（批 1）/ true = 3D 形状族（批 2）
    QTimer m_ticker;                       // 动画驱动（16ms Precise；t1032 起带空转门——构造不启钟）
    QElapsedTimer m_clock;                 // 动画相位时钟（构造起点，秒）
    QByteArray m_buffer;                   // 实例表字节缓冲（复用，零稳态分配）
    QVector<int> m_liveSlots;              // 本桶活体槽快照（填表用，复用）
};

#endif // BLOCKDROPINSTANCING_H
