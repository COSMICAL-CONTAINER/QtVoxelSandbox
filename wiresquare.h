#ifndef WIRESQUARE_H
#define WIRESQUARE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 命中面线框（Renderer 视图）：XY 平面内 1×1 方框（LineStrip，法线 +Z）。
//
// 静态规范几何 —— 由 Model 的 position/eulerRotation 摆到命中面上（命中点 + 法线由
// PlayerController 的射线选体算好）。每帧只改 Model 变换，不重建几何（4 段线，开销可忽略）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何，不读栅格、不做选体逻辑。
class WireSquare : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WireSquare)

public:
    explicit WireSquare(QQuick3DObject *parent = nullptr);
};

#endif // WIRESQUARE_H
