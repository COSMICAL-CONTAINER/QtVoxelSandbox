#ifndef UNITCUBE_H
#define UNITCUBE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 单位立方体几何（1×1×1，居中原点 ±0.5，Triangles）。
//
// 用途：玩家模型 / 第一人称手等「静态可见方块」的几何源，替代内置 `#Cube`。
// 原因（t31 诊断结论）：本工程实测**静态 `source:"#Cube"` Model 不渲染**（同 NoLighting 材质下，
// 粒子 instanced #Cube 可见、静态 #Cube 不可见；而自定义 QQuick3DGeometry——地形/线框——均可见）。
// 故模型/手改用本几何，走「自定义几何 + NoLighting PrincipledMaterial」这条**已验证可见**的渲染路径。
//
// 顶点 36（12 三角形，每面 2 三角），pos(3 float)，CCW 朝外（默认 backface 剔除下各面可见）。
// 基准 ±0.5 与 #Cube 同，Main.qml 里 Model 的 scale/position 取值沿用不变。
//
// 分层（PLAN §2）：属 Renderer（依赖 QtQuick3D），只产出几何。
class UnitCube : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(UnitCube)

public:
    explicit UnitCube(QQuick3DObject *parent = nullptr);
};

#endif // UNITCUBE_H
