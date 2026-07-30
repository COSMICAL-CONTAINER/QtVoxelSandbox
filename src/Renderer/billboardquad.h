#ifndef BILLBOARDQUAD_H
#define BILLBOARDQUAD_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 广告牌四边形几何（单面，XY 平面内 1×1，居中 ±0.5，外法线 +Z，Triangles，UV 0..1）。
//
// 用途（t112 煤/木炭/铁原矿/木棒/铁锭/玻璃 掉落实体）：把「无等距立方体 PNG、靠 2D 图标自绘」
// 的材料段从 CrackBox（6 面立方）改为单面平图标。根因：CrackBox 把同一张 MaterialIcon Canvas
// 贴到立方体 6 面 → 斜视时 6 个半透面互相穿插、呈「未封闭超立方体」观感（图标边缘透明底露出
// 背后面的图标像素）。单面四边形 + 朝相机（QML 里令承载 Model 的世界欧拉 = 相机欧拉）→ 恒以
// 一张完整平图标正对玩家，机制对齐 MC 1.0 掉落物（平面 billboard）。
//
// 与 CrackBox 的区别：CrackBox 6 面 24 顶点（每面独立全幅 UV，供立方叠层用）；本类仅 1 面
// 4 顶点（单四边形 2 三角形），正面 +Z 朝外（CCW），默认 backface 剔除下从 +Z 侧可见——
// 故承载 Model 必须令 +Z 指回相机（见 Main.qml 材料段 eulerRotation 绑定）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。朝向逻辑 / 实体数据在
// QML 呈现层 / Game 层；本类不读相机、不持贴图——贴哪张 MaterialIcon 由 QML 据 itemId 切
// （同 CrackBox 旧材料段契约）。
class BillboardQuad : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BillboardQuad)

public:
    explicit BillboardQuad(QQuick3DObject *parent = nullptr);
};

#endif // BILLBOARDQUAD_H
