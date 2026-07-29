#ifndef PICKAXE_H
#define PICKAXE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 木镐 3D 几何（镐头 + 木柄，体素化块组合，Triangles，pos-only）。
//
// 用途（t75）：替代工具段「CrackBox + ToolIcon 透明底贴图」的兜底渲染。后者根因是 ToolIcon 在
// 透明底（RGB 0,0,0 + alpha 0）上画镐，作 3D 纹理贴到 CrackBox 六面时无 alphaCutoff → 透明底被
// 当不透明黑渲染 → 6 面全黑立方体（「工具贴图黑」bug）。本类是**纯实心体素几何**（无贴图、无 alpha），
// 配 PrincipledMaterial{NoLighting + baseColor}（同 UnitCube / BlockCube 已验证可见路径）→ 必为
// 实色镐形、永不黑。形状机制对齐 MC 木镐：竖直木柄 + 顶部水平横梁 + 两端下勾（像素镐经典轮廓）。
//
// 三处统一消费（spec t75）：丢弃实体 + 第一人称手持 + 第三人称手持均用它（不再 CrackBox 兜底）。
// 各消费点按工具 tier（hotbarVM.toolTier）给 baseColor 着色（木镐褐 / 石镐灰 / 铁镐银白），形状不变。
//
// 顶点 96（4 盒 × 每盒 24 = 6 面 × 4 角，独立顶点便于 per-face 剔除），索引 144（4 盒 × 36）。
// CCW 朝外（默认 backface 剔除下，外法线面可见）。基准：几何中心 ≈ 原点（木柄中段 y≈0、镐头 y≈+0.4），
// 消费点用 Model 的 position/scale/eulerRotation 摆位与定向。
//
// 为何不用内置 #Cube 拼装：本工程实测**静态 source:"#Cube" Model 不渲染**（t31 诊断结论；粒子
// instanced #Cube 可见、静态 #Cube 不可见），故自定义 QQuick3DGeometry + NoLighting 是已验证
// 可见路径（同 UnitCube / BlockCube / CrackBox）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。工具 tier / 名称在 ToolRegistry
// （Game 层）；本类不读 tier、不持贴图——着色由 QML 呈现层据 hotbarVM.toolTier 设 baseColor。
// 依赖只向下。
class PickaxeGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PickaxeGeometry)

public:
    explicit PickaxeGeometry(QQuick3DObject *parent = nullptr);
};

#endif // PICKAXE_H
