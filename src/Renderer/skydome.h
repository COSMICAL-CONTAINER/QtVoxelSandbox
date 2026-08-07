#ifndef SKYDOME_H
#define SKYDOME_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 天穹球几何（Renderer 层）：一张绕相机眼位的 UV 球（半径 1，QML 里 scale 到 ~600 格），
// 内表面铺星空贴图，作为「夜空星点」的承载几何。
//
// 用途（t389 月相 + 星空 + 天穹渐变）：夜间抬头应见散布星点。星空贴图（stars.png，透明底 + 白 / 蓝亮点）
// 铺到这张球的内表面，材质 opacity 随天光乘子淡入（夜显 / 昼隐）。球**居中于相机眼位**（QML 里 position
// 绑 player.position）→ 相机恒在球心 → 每条视线恰好交球一次（距眼 = radius）→ 既无近 / 远面互相遮挡，
// 也恒在地形（近）与 clipFar 之外、太阳 / 月（500 格）之后（dome 600 > 500 → 星空作为最远天幕，日 / 月
// 渲在其前可见）。机制对齐 MC 1.0 天穹（绕玩家的一层星点球壳）。
//
// winding：默认 backface 剔除下只有「外法线朝观察者」的面可见。相机在球**内**，故需令内表面 = 正面。
// 本类按「从球内看 CCW」生成三角（即外法线指向球心 / 内），使默认剔除即显内表面（无需 QML 设 NoCulling，
// 亦避免外表面无谓绘制）。UV 球：u 绕赤道一圈（0..1，左右对接）、v 自南极(0)→北极(1)；星空贴图避开了
// 两极 pinch 区（v∈[0.08,0.92] 布星）。
//
// 与 BillboardQuad 的区别：Quad 是单面平面（朝相机广告牌）；本类是闭合球壳（包住相机），用整张贴图
// 一次性铺满全天，而非逐星 N 个 Model（N=170 Model 的逐帧变换开销不可取）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。夜间淡入 / 颜色 / 位置在 QML 呈现层；
// 时间（天光乘子）来自 Game 层 WorldClock（只读）。本类不读相机、不持贴图。
class SkyDome : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(SkyDome)

public:
    explicit SkyDome(QQuick3DObject *parent = nullptr);
};

#endif // SKYDOME_H
