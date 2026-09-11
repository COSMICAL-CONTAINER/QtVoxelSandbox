#ifndef GLOWSHELLINSTANCING_H
#define GLOWSHELLINSTANCING_H

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
                               //   TypeMustBeComplete）——blockdropinstancing.h 的前向声明模式靠 mocs_compilation
                               //   include 顺序侥幸存活，本头直接 include 全量头（同 blockdropinstancing.h 批 1/2 先例）。

// t1039（R19.23）掉落物 instancing 批 3：**光晕壳族全族合批**（治理全量 ⑥ 号族，登记见
// blockdropinstancing.h 族表）。掉落物 delegate 的光晕壳（Main.qml entShell：UnitCube 半透壳，
// scale 0.45，附魔紫 / 普通灰 + t696 附魔呼吸 opacity）旧观感是**全族横切**——delegate 无 visible
// 条件，每个活体掉落实体恒带一壳（与本体族无关：整立方 / 3D 形状 / 工具 / 材料 / billboard 全带）。
// 本类把整族壳压成 **全族单 instanced Model 单实例表**（1 draw / 全族壳；本体仍走批 1/2 桶或
// delegate 旧路径，壳独立成池与本体族解耦——「第三池」设计自由度沿批 1/2）。
//
// 与批 1/2（BlockDropInstancing per-id 桶）的结构差异：壳族无 per-id 几何（UnitCube 全族共享）/
// 无 per-id 材质（纯色 + per-instance color），故**不需要桶池**——单 feeder 单 Model 单表。收纳
// 谓词 = 「活体即收」（槽序前 kShellCap=128 个活体；manager 上限 200，常态活体个位到几十
// ——t1007 高水位口径——128 保证一切现实场景全合批，同时让溢出降级腿真实可达）。
//
// **两侧谓词同源（同 id 壳不双渲）**：delegate 侧 entShell 追加 `visible:
// !glowShellInstHost.hasShellAt(index)`（薄委托本类 Q_INVOKABLE hasShellAt——与批 1 hasBucket /
// 批 2 hasShapeBucket 排除链同纪律，但更强：排除侧直调 C++ 同一函数，逐位同判无第二实现）。
// 收纳（getInstanceBuffer 取前 kShellCap 个活体槽）与排除（hasShellAt = 该槽活体且其前活体数
// < kShellCap）互为镜像，同一实体壳恰在一侧渲染：入池 → 实例壳出 + delegate 壳隐藏；溢出 /
// 无 manager / 空槽 → delegate 壳保底（降级不丢渲，不双渲）。壳排除链只挂在 entShell 节点上，
// 与本体 Model 的 hasBucket / hasShapeBucket 链正交（各管各的节点，互不掺杂）。
//
// **per-instance color 承载**（t858 二值绿同机制扩 alpha）：
//   - rgb = 附魔紫 (140,64,230) / 普通灰 (176,176,176) × 天光乘子 k = minLight +
//     (1−minLight)×skyLight（Main.qml tintBySkyLight / terrainLight 的 floor 公式逐字对齐；
//     skyLight / minLight 为 Q_PROPERTY，QML 绑 worldClock.skyLight / window.minLight——
//     t144「外壳随夜同步变暗」契约在实例化路径继续成立）。
//   - alpha = 普通灰静态 0.35 / 附魔呼吸 0.28↔0.45。呼吸解析式（旧 delegate SequentialAnimation
//     逐字对齐：`NumberAnimation { from:0.28; to:0.45; duration:800; easing.type: InOutSine }`
//     + 反向腿）：InOutSine out-and-back = 0.5×(1−cos(π·s)) 三角波（t858 推导同源），
//     alpha(s) = 0.28 + 0.17×0.5×(1−cos(π·s))，s = fmod(ph/0.8, 2)（1.6s 全循环 = 800ms×2 腿）。
//     实例表 alpha 的渲染生效 = Qt 官方 hasTransparency 属性（构造置 true：表含 alpha 须参与
//     渲染；材质本体保持不透明白——不透明白材质 × 表 alpha = 旧「材质 opacity 0.35」的逐位
//     等效面，半透混合走透明通道）。
//
// **动画下沉**（批 1/2 同公式源）：壳继承旧 entRoot 的自转 + 浮动——rotY = fmod(ph/3, 1)×360、
//   bob = 0.075×(1−cos(π·s2))（0↔0.15 三角波），ph = t + slot×0.37（批 1/2 的「旧相位 = 创建
//   时刻」等价视觉错开口径沿用）。
//
// **壳-体相位脱锁口径（t1032 书相位教训的如实评估，先断言再实现，登记）**：旧观感壳与本体同挂
//   entRoot（bobY/rotY 逐位同锁）。实例化后壳走本 feeder 时钟（与批 1/2 本体 feeder 各自构造的
//   m_clock），跨时钟恒定偏移不可避免。评估：①批 1/2 本体（压倒性常态路径）与本壳 feeder 同用
//   slot×0.37 口径，两钟同在 Main.qml 实例化期构造 → 偏移 ms 级 → bob 相对偏移远低于可察觉阈；
//   ②delegate 保底本体（桶满溢出，罕见）相位 = 创建时刻 → 最坏恒定 0.15 bob 相位差——壳是
//   0.45 半透 halo（本体 0.3，单面净空 0.075），瞬态包络差与 t1038 书-台（不透明实体嵌入 +
//   0-360° 旋转偏移 = 明确破损）不同级：halo 是独立视觉件，脱锁可接受；③呼吸相位旧世界本就
//   相对本 bob 无观测锁（视觉为慢速 alpha 摆动）。三者登记为 **接受** 的口径变化。
//
// **空转门（t1032 同款）**：16ms QTimer 构造**不启钟**——manager 空 / 活体 0 不走钟。壳族无
//   familyId（全族横切），活跃判定 = 「任一活体槽」（≥1 活体 ⇒ ≥1 壳入池 ⇒ 动画须走钟；灰壳
//   静态但壳仍随本体 bob/rotY 动画）。活跃沿 setManager / manager entitiesChanged → start +
//   markDirty 兜底；空转沿 → stop（探针 probeTickerActive 钉）。skyLight / minLight 沿只
//   markDirty（色调重取，无时钟语义）。
//
// **分层（PLAN §2）**：Game 层呈现适配器——向下只读 ItemEntityManager（pos/itemId/enchants/alive，
//   附魔判定经既有 enchantsAt 与 QML entHasEnch 同一数据源）+ Qt Quick3D 公开类型（无自定义
//   shader——PLAN §2-A RHI 囚笼合规）。拾取 / despawn / 合并 / 掉落物理零改动（只读渲染态）。
class GlowShellInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GlowShellInstancing)
    // 数据源（只读）：pos / enchants / alive 逐槽读。切世界 clearAll 只释放槽位 → 实例表自然清空
    //（alive=false 槽不进表），无需本类参与重置（同 BlockDropInstancing / XpOrbInstancing）。
    Q_PROPERTY(ItemEntityManager *manager READ manager WRITE setManager NOTIFY managerChanged)
    // 天光乘子（QML 绑 worldClock.skyLight，[0,1]）；缺省 1.0 = 正午全亮（headless 探针确定态）。
    Q_PROPERTY(qreal skyLight READ skyLight WRITE setSkyLight NOTIFY skyLightChanged)
    // 夜间 floor（QML 绑 window.minLight；Main.qml 缺省 0.4 同值）；k = minLight + (1−minLight)×skyLight。
    Q_PROPERTY(qreal minLight READ minLight WRITE setMinLight NOTIFY minLightChanged)

public:
    explicit GlowShellInstancing(QQuick3DObject *parent = nullptr);

    // 壳池容量（槽序前 N 个活体入池；>N 的活体壳走 delegate 保底）。manager 上限 200 > 128 →
    // 溢出态真实可达（降级腿探针面）。常量而非 Q_PROPERTY：容量是设计常量（同桶池 kBucketCount=8
    // 钉在 QML host 的口径），不运营期改动。
    static constexpr int kShellCap = 128;

    ItemEntityManager *manager() const { return m_manager; }
    void setManager(ItemEntityManager *m);

    qreal skyLight() const { return m_skyLight; }
    void setSkyLight(qreal v);

    qreal minLight() const { return m_minLight; }
    void setMinLight(qreal v);

    // **两侧谓词同源单一权威**：第 slot 槽的壳是否入池（QML delegate 排除侧薄委托本函数）。
    // 语义 = 该槽活体 **且** 槽序上先于它的活体数 < kShellCap（与 getInstanceBuffer 的
    // 「前 kShellCap 个活体槽」收纳互为镜像）。无 manager / 越界 / 空槽 / 溢出 → false =
    // delegate 壳保底（降级不丢渲）。
    Q_INVOKABLE bool hasShellAt(int slot) const;

    // 矩阵探针专用（t1027/t1032 同款）：当前实例表条数 / 第 k 条内容（越界 false）。headless 直调
    // getInstanceBuffer 后解码 InstanceTableEntry——不依赖渲染 sync / QQuick3D 私有态。
    // 批 3 扩展：outColor 回带 per-instance color（rgb 天光调 + alpha 呼吸/静态——批 3 核心断言面）。
    int probeInstanceCount();
    bool probeInstanceAt(int k, QVector3D *outPos, QVector3D *outScale, QQuaternion *outRot,
                         QColor *outColor);
    // 空转门探针（t1032 同款）：动画 QTimer 是否在走。空转（无 manager / 活体 0）必须 false。
    Q_INVOKABLE bool probeTickerActive() const { return m_ticker.isActive(); }

signals:
    void managerChanged();
    void skyLightChanged();
    void minLightChanged();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    void tick(); // QTimer 16ms：动画相位推进 → markDirty（下帧 sync 重取表）
    // 空转门（t1032 模式）：按「任一活体槽」启/停钟。活跃沿 start + markDirty 兜底防丢帧，
    //   空转沿 stop。沿源 = setManager / manager entitiesChanged（活体集变更完备沿）。
    void refreshTicker();
    bool hasLiveMember() const;
    // manager 活体集变更沿（entitiesChanged 直连）：空转门重估 + markDirty 兜底。
    void onManagerEntitiesChanged();

    ItemEntityManager *m_manager = nullptr;
    qreal m_skyLight = 1.0;   // QML 绑 worldClock.skyLight；缺省正午（探针确定态）
    qreal m_minLight = 0.4;   // QML 绑 window.minLight；Main.qml 同缺省 0.4
    QTimer m_ticker;          // 动画驱动（16ms Precise；空转门——构造不启钟，t1032 同款）
    QElapsedTimer m_clock;    // 动画相位时钟（构造起点，秒）
    QByteArray m_buffer;      // 实例表字节缓冲（复用，零稳态分配）
    QVector<int> m_liveSlots; // 入池活体槽快照（填表用，复用）
};

#endif // GLOWSHELLINSTANCING_H
