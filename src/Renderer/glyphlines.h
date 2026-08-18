#ifndef GLYPHLINES_H
#define GLYPHLINES_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// t697 符文字迹几何（附魔台悬浮书页面细节）：XZ 平面上一组细扁盒条（暗色「字行」），排布成两列
// 短横条的符文页观感 —— 压在暖白纸面上读作手写符文行（用户「书字太少 / 太白」；程序生成，§9 原创，
// 非任何 MC 资产）。
//
// 形态：基础平面 X∈[-0.5,0.5] × Z∈[-0.5,0.5]（同 UnitCube 基准，Model scale 控制实际大小），每条
// 是一个 X 长 0.16..0.30 / Z 宽 0.05 / Y 厚 0.02 的扁盒，两列（Z=-0.18 / +0.14 两组行位）共 8 条，
// 行位错落（X 偏移交替 ±0.06）避免整齐雷同（符文页的手写感）。Y 厚 0.02 让薄片在页面之上略凸，
// 无需 z-fight 偏移即可与页面上表面分离（caller 仍给 +0.013 Y 偏移保余量）。
//
// 顶点：8 盒 × 36 顶点（同 UnitCube 每盒 12 三角），pos(3 float)，CCW 朝外。
//
// 分层（PLAN §2）：属 Renderer（依赖 QtQuick3D），只产出几何，无状态无更新。
class GlyphLines : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(GlyphLines)

public:
    explicit GlyphLines(QQuick3DObject *parent = nullptr);
};

#endif // GLYPHLINES_H
