#ifndef XPORBINSTANCING_H
#define XPORBINSTANCING_H

#include <QtQuick3D/QQuick3DInstancing>
#include <QtQml/qqml.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>

class XpOrbManager;

// t858（R19.14）经验球 instancing 试点：XpOrbManager 的掉落渲染从「每球一个 Repeater delegate
// （UnitCube Model + per-delegate QML 动画）」压成 **一个 Model + 一张实例表**（1 draw / 全族）。
//
// 机制（Quick3D 公开 API，无自定义 shader——PLAN §2-A RHI 囚笼合规）：Model.instancing 指向本类实例，
// 渲染 sync 时经 getInstanceBuffer() 取实例表（每实例 80B：3×4 变换行 + RGBA + customData）；本类每帧
// 重算表（QTimer 16ms → markDirty，Qt 官方 dynamic instancing 示例同款模式）——经验球数量上限 64，
// 全表重算 + 上传 ≈ 5KB/帧，远廉于 64 个 delegate 子树的场景图遍历 / 绘制 / 绑定开销。
//
// **动画下沉**：旧 delegate 的 bob（0↔0.12，700ms/leg InOutSine）/ pulse（0.85↔1.15，500ms/leg）是
// per-delegate QML SequentialAnimation；实例化后 per-instance 状态只能进实例表 → 在 C++ 用解析式复算
// （InOutSine 的 out-and-back 恰为 0.5*(1-cos(pi*s)) 三角波，s = 相位/半周期 mod 2），per-slot 相位取
// slot*0.37s 错峰（旧 delegate 相位 = 各自创建时刻，等价的视觉错开）。观感公式逐字对齐旧 QML 数字。
//
// **分层（PLAN §2）**：Game 层呈现适配器——向下依赖 Entities 层 XpOrbManager（只读 pos/amount/alive）
// + Qt Quick3D 公开类型（先例：Game 层 PlayerController 本就是 QQuickItem 派生）；不写实体数据、
// 不反向依赖 World/QML。拾取 / 磁吸判定纯 C++（XpOrbManager::tick）不经过本类，渲染层替换零影响。
//
// **试点范围取舍（钉死）**：只做经验球——它整族同几何（UnitCube）同材质语义（纯色、无贴图、无 alpha
// 契约、无附魔壳 / 命中框分支），是 instancing 的无争议适用面。掉落物各族（billboard 平图标 / 3D 形状 /
// 工具几何）**保持逐 Model**：per-item 贴图或 per-item 几何使单 Model 单材质的实例表无法覆盖，需按
// itemId 分桶动态建 Model + 贴图（复杂度与 delegate 生命周期风险高——t170/t256/t437/t492 的
// slot-reuse / hardReset 契约都在这条链上），留待独立任务（dev-plan R19.14 t858 记录取舍）。
class XpOrbInstancing : public QQuick3DInstancing
{
    Q_OBJECT
    QML_NAMED_ELEMENT(XpOrbInstancing)
    // 数据源（只读）：pos / amount / alive 逐槽读。切世界 clearAll 只释放槽位 → 实例表自然清空
    //（count 单调但 alive=false 的槽不进表），无需本类参与重置。
    Q_PROPERTY(XpOrbManager *manager READ manager WRITE setManager NOTIFY managerChanged)

public:
    explicit XpOrbInstancing(QQuick3DObject *parent = nullptr);

    XpOrbManager *manager() const { return m_manager; }
    void setManager(XpOrbManager *m);

    // 矩阵探针专用（t858）：返回当前实例表首条的世界位（无活体 → false）。headless 直调
    // getInstanceBuffer 后解码 InstanceTableEntry——不依赖渲染 sync / QQuick3D 私有态。
    //（纯 C++ 测试口，非 Q_INVOKABLE——QVector3D* 出参不经 QML。）
    bool probeFirstInstancePosition(QVector3D *outPos);

signals:
    void managerChanged();

protected:
    QByteArray getInstanceBuffer(int *instanceCount) override;

private:
    void tick(); // QTimer 16ms：动画相位推进 → markDirty（下帧 sync 重取表）

    XpOrbManager *m_manager = nullptr;
    QTimer m_ticker;                       // 动画驱动（16ms Precise；无活体时也跑，空表零成本）
    QElapsedTimer m_clock;                 // 动画相位时钟（构造起点，秒；连续不冻结）
    QByteArray m_buffer;                   // 实例表字节缓冲（复用，避免逐帧分配）
    QVector<int> m_liveSlots;              // 活体槽快照（填表用，复用）
};

#endif // XPORBINSTANCING_H
