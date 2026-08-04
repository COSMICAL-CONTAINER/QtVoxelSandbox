#ifndef BOW_H
#define BOW_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 弓 3D 几何（弓臂弧 + 弓弦 + 握把，体素化块组合，Triangles，pos-only）。
//
// 用途（t304 弓 + 箭）：与 PickaxeGeometry / SwordGeometry 对称的弓形 3D ——手持（第一 / 第三人称）+
// 掉落实体三处统一消费。本类是**纯实心体素几何**（无贴图、无 alpha），配 PrincipledMaterial{NoLighting +
// baseColor}（同 Pickaxe / Sword 已验证可见路径）→ 必为实色弓形、永不黑。形状机制对齐 MC 1.0 弓：垂直弓身
// （弓臂在 XY 平面、 belly 朝 +Z / 弦在 -Z）+ 中央握把 + 后侧细弦。QML 据 toolType===Bow（BlockRegistry::Bow=7）
// 选本几何；弓臂 / 弦共用 tier 色（与 ToolIcon 弓图标同色策略：整把 tier 着色，木弓褐）。
//
// 顶点 144（6 盒 × 每盒 24）+ 索引 216（6 盒 × 36）。CCW 朝外。基准：握把中心 ≈ 原点（上 limb tip y≈+0.46、
// 下 limb tip y≈-0.46、弦 z≈-0.04、belly z≈+0.12），消费点用 Model 的 position/scale/eulerRotation 摆位定向。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。弓 tier / 名称在 ToolRegistry（Game 层）；
// 本类不读 tier、不持贴图——着色由 QML 呈现层据 hotbarVM.toolTier 设 baseColor（与 sword 同模式）。
// 依赖只向下。与 sword.h / pickaxe.h 同层同风格（复制 addBox 模式，仅改整体形状）。
class BowGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BowGeometry)

public:
    explicit BowGeometry(QQuick3DObject *parent = nullptr);
};

#endif // BOW_H
