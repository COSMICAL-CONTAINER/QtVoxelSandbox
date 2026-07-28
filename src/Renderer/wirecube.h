#ifndef WIRECUBE_H
#define WIRECUBE_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 单位立方体线框（Renderer 视图）：12 条棱（8 角），LineList，跨度 ±0.5 = 恰好包住 1×1×1 方块。
//
// 用途（t52 选中高亮）：射线选体命中后，选中框从「命中面方框 (WireSquare)」改为「整个立方体框」，
// 包住命中方块 8 角 12 棱。Model 摆到命中方块中心（hitBlock + 0.5），scale 略放大防 z-fight；
// 几何本身对称、与朝向无关，故无需 eulerRotation（与 WireSquare 需按命中面法线旋转不同）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何，不读栅格、不做选体逻辑。
// 与 WireSquare 同层同模式（静态规范几何，每帧只改 Model 变换、不重建几何）。
class WireCube : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(WireCube)

public:
    explicit WireCube(QQuick3DObject *parent = nullptr);
};

#endif // WIRECUBE_H
