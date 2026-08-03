#ifndef SHOVEL_H
#define SHOVEL_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 铲 3D 几何（木柄 + 顶端方形铲斗，体素化块组合，Triangles，pos-only）。
//
// 用途（t264 完整工具集）：与 PickaxeGeometry / HoeGeometry / AxeGeometry 对称的铲形 3D——手持
// （第一/第三人称）+ 掉落实体三处统一消费。本类是**纯实心体素几何**（无贴图、无 alpha），配
// PrincipledMaterial{NoLighting + baseColor}（同 PickaxeGeometry 已验证可见路径）→ 必为实色铲形、永不黑。
// 形状机制对齐 MC 1.0 铲：竖直木柄 + 顶部方形/梯形铲斗（掘土容器，前端平直刃口，区别于斧的弧形单边刃
// 与锄的宽扁横刃）。
//
// 三处消费点按工具 tier（hotbarVM.toolTier）给 baseColor 着色（木铲褐 / 石铲灰 / 铁铲银白），形状不变；
// QML 据 hotbarVM.toolType(selectedItem) 选各工具几何。
//
// 顶点 72（3 盒 × 每盒 24）+ 索引 108（3 盒 × 36）。CCW 朝外。基准：几何中心 ≈ 原点
// （木柄中段 y≈0、铲斗 y≈+0.4），消费点用 Model 的 position/scale/eulerRotation 摆位与定向。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。工具 tier / 名称在 ToolRegistry
// （Game 层）；本类不读 tier、不持贴图——着色由 QML 呈现层据 hotbarVM.toolTier 设 baseColor。
// 依赖只向下。与 pickaxe.h / hoe.h / axe.h 同层同风格（复制 addBox 模式，仅改头部形状）。
class ShovelGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ShovelGeometry)

public:
    explicit ShovelGeometry(QQuick3DObject *parent = nullptr);
};

#endif // SHOVEL_H
