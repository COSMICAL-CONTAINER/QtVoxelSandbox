#ifndef CRACKBOX_H
#define CRACKBOX_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 裂纹叠层立方体几何（1×1×1，居中 ±0.5，Triangles，每面独立全幅 UV）。
//
// 用途（t34 挖掘系统）：在生存挖掘目标方块上叠一个 Model，按进度切 6 阶裂纹贴图。
// 与 UnitCube 的区别：UnitCube 仅 pos（玩家模型 / 手用纯色材，不采样贴图）；
// 本类带 TexCoord0 → baseColorMap（裂纹 PNG）能正确映射到 6 面。每面 UV 全 0..1，
// 故整张贴图完整铺满每面（裂纹覆盖可见面，机制对齐 MC 1.0 destroy_stage）。
//
// 顶点 24（每面 4 角，独立顶点便于 per-face UV），索引 36（12 三角形）；CCW 朝外
// （默认 backface 剔除下，外法线面可见 → 裂纹只显在玩家看得到的那几面，背向面被剔除）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。挖掘逻辑 / 进度态
// 在 PlayerController（Game/Physics 层）；本类不读进度、不持裂纹贴图——选哪张贴图由
// QML 呈现层据 player.miningStage 切 baseColorMap（同 hotbar/held 浮动图标切法）。
class CrackBox : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CrackBox)

public:
    explicit CrackBox(QQuick3DObject *parent = nullptr);
};

#endif // CRACKBOX_H
