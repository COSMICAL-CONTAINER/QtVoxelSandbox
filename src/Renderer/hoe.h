#ifndef HOE_H
#define HOE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 锄 3D 几何（木柄 + 顶端横向锄刃，体素化块组合，Triangles，pos-only）。
//
// 用途（t233）：与 PickaxeGeometry 对称的锄形 3D——手持（第一/第三人称）+ 掉落实体三处统一消费，
// 替代旧「CrackBox + ToolIcon 透明底贴图」兜底（后者无 alphaCutoff → 6 面黑，t75 已为镐修复）。
// 本类是**纯实心体素几何**（无贴图、无 alpha），配 PrincipledMaterial{NoLighting + baseColor}
// （同 UnitCube / BlockCube / PickaxeGeometry 已验证可见路径）→ 必为实色锄形、永不黑。
// 形状机制对齐 MC 1.0 锄：竖直木柄 + 顶部一片横向扁平锄刃（锄地刮土的工具头，区别于镐的「横梁 + 两端下勾」）。
//
// 三处消费点按工具 tier（hotbarVM.toolTier）给 baseColor 着色（木锄褐 / 石锄灰 / 铁锄银白），形状不变；
// QML 据 hotbarVM.toolType(selectedItem) 选 PickaxeGeometry（type=Pickaxe）vs 本类（type=Hoe）。
//
// 顶点 72（3 盒 × 每盒 24）+ 索引 108（3 盒 × 36）。CCW 朝外。基准：几何中心 ≈ 原点
// （木柄中段 y≈0、锄刃 y≈+0.4），消费点用 Model 的 position/scale/eulerRotation 摆位与定向。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。工具 tier / 名称在 ToolRegistry
// （Game 层）；本类不读 tier、不持贴图——着色由 QML 呈现层据 hotbarVM.toolTier 设 baseColor。
// 依赖只向下。与 pickaxe.h 同层同风格（复制其 addBox 模式，仅改头部形状）。
class HoeGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(HoeGeometry)

public:
    explicit HoeGeometry(QQuick3DObject *parent = nullptr);
};

#endif // HOE_H
