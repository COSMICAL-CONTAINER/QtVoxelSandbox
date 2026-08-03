#ifndef SWORD_H
#define SWORD_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 剑 3D 几何（纵向长刃 + 护手 + 柄，体素化块组合，Triangles，pos-only）。
//
// 用途（t264 完整工具集）：与 PickaxeGeometry / HoeGeometry / AxeGeometry / ShovelGeometry 对称的剑形 3D
// ——手持（第一/第三人称）+ 掉落实体三处统一消费。本类是**纯实心体素几何**（无贴图、无 alpha），配
// PrincipledMaterial{NoLighting + baseColor}（同 PickaxeGeometry 已验证可见路径）→ 必为实色剑形、永不黑。
// 形状机制对齐 MC 1.0 剑：纵向对称长刃（刃占上半，tier 金属色）+ 横向护手 + 短柄 + 柄首圆头
// （区别于工具的对角木柄 + 侧伸头）。
//
// 三处消费点按工具 tier（hotbarVM.toolTier）给 baseColor 着色（木剑褐 / 石剑灰 / 铁剑银白），形状不变；
// QML 据 hotbarVM.toolType(selectedItem) 选各工具几何（type=Sword → 本类）。
//
// 顶点 120（5 盒 × 每盒 24）+ 索引 180（5 盒 × 36）。CCW 朝外。基准：几何中心 ≈ 原点
// （刃中段 y≈0、刃尖 y≈+0.45、柄底 y≈-0.45），消费点用 Model 的 position/scale/eulerRotation 摆位与定向。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。工具 tier / 名称在 ToolRegistry
// （Game 层）；本类不读 tier、不持贴图——着色由 QML 呈现层据 hotbarVM.toolTier 设 baseColor（刃与柄
// 共用 tier 色，与 2D ToolIcon 剑刃 / 护手同色策略一致：整把 tier 着色，无木柄区分——剑的护手 / 柄
// 在 MC 中亦同材质，简化呈现）。
// 依赖只向下。与 pickaxe.h / hoe.h / axe.h / shovel.h 同层同风格（复制 addBox 模式，仅改整体形状）。
class SwordGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SwordGeometry)

public:
    explicit SwordGeometry(QQuick3DObject *parent = nullptr);
};

#endif // SWORD_H
