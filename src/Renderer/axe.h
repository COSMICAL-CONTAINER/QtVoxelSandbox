#ifndef AXE_H
#define AXE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 斧 3D 几何（木柄 + 顶端单边厚重斧刃，体素化块组合，Triangles，pos-only）。
//
// 用途（t264 完整工具集）：与 PickaxeGeometry / HoeGeometry 对称的斧形 3D——手持（第一/第三人称）
// + 掉落实体三处统一消费。本类是**纯实心体素几何**（无贴图、无 alpha），配 PrincipledMaterial
// {NoLighting + baseColor}（同 PickaxeGeometry / HoeGeometry 已验证可见路径）→ 必为实色斧形、永不黑。
// 形状机制对齐 MC 1.0 斧：竖直木柄 + 顶部一侧的厚重块状斧刃（单边刃，区别于镐的「横梁 + 两端下勾」
// 与锄的「宽扁横刃」）。
//
// 三处消费点按工具 tier（hotbarVM.toolTier）给 baseColor 着色（木斧褐 / 石斧灰 / 铁斧银白），形状不变；
// QML 据 hotbarVM.toolType(selectedItem) 选各工具几何（Pickaxe / Hoe / Axe / Shovel / Sword）。
//
// 顶点 96（4 盒 × 每盒 24）+ 索引 144（4 盒 × 36）。CCW 朝外。基准：几何中心 ≈ 原点
// （木柄中段 y≈0、斧刃 y≈+0.4），消费点用 Model 的 position/scale/eulerRotation 摆位与定向。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。工具 tier / 名称在 ToolRegistry
// （Game 层）；本类不读 tier、不持贴图——着色由 QML 呈现层据 hotbarVM.toolTier 设 baseColor。
// 依赖只向下。与 pickaxe.h / hoe.h 同层同风格（复制 addBox 模式，仅改头部形状）。
class AxeGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AxeGeometry)

public:
    explicit AxeGeometry(QQuick3DObject *parent = nullptr);
};

#endif // AXE_H
