#ifndef BEDMODELGEOMETRY_H
#define BEDMODELGEOMETRY_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

#include "blockregistry.h" // BedRed 兜底 / kBedHeadboardTop 高度（bounds）

// t784 床完整低 3D 模型几何（双格：床尾半 + 床头半并排；Renderer 层）。
//
// 用途：资源浏览器（ResourceBrowser.qml）床条目 3D 预览——替代旧「满格 BlockCube 六面同贴床色被面瓦片」
//   的旋转立方（= 任务「仍是老模型」根因：床 isPartialBlock/isCrossBlock/isMaterial 均否 → 浏览器
//   selectedIsCube 路由把床当整立方）。现与游戏内一致显示低 3D 床（四角木柱腿 + 木板床架 + 彩色被面
//   床垫 + 床头板/床尾板 + 白色枕头）。
//
// **几何单一权威复用**（不重复造轮子）：盒布局不持有副本，直接调 PartialBlockGeometry::bedHalfBoxes
//   （src/World，游戏内 chunk mesh 床 case 的同一盒列表）——一次调两半（foot 本格 + head 沿 -front 邻格
//   平移一格）拼成完整双格床，facing 固定 0（head→foot = +X，游戏内玩家面 -X 放置形态；预览自转无朝向
//   语义，任一 facing 观感等价）。改床形只改 bedHalfBoxes 一处，浏览器 / 游戏内同步（两侧各自漂移的
//   旧病根除，同 t750 图鉴「几何差异下沉 MobModel 共享层」抉择）。贴图同源共享图集：材质 baseColorMap =
//   宿主注入 atlasSource（pack 开 → 合成图集床瓦片即时刷新，同 cubeView BlockCube 分支）。
//
// 16 色变体联动：blockId Q_PROPERTY 绑浏览器 selectedId——调色板 16 个床条目（既存 8 色 0x20..0x27 +
//   补齐 8 色 0x4E..0x55）各持独立 id → 选色即换被面瓦片（t751 变体联动同族；床色无共用模型控件，
//   不设 variantPanel）。非床 id 兜底 BedRed（保几何非空、瓦片合法）。
//
// 顶点：pos(3) + uv(2) = 5 float（同 MobModel；材质无顶点色需求 → 不设 ColorSemantic）。每盒 6 面 ×
//   4 角 = 24 顶点 / 36 索引，双半 11~12 盒累加（head 半多枕头）。局部原点 = 床整体中心（x∈[-1,1] 双格
//   对称 / y 以床高 kBedHeadboardTop 居中 / z [-0.5,0.5]），Model 摆位用「中心 = pos」自洽（同 BlockCube
//   ±0.5 居中约定，lessons t34「几何原点与摆位成对契约」）。
//
// 分层（PLAN §2）：本类属 Renderer，向下只读 World（bedHalfBoxes）+ Core（BlockRegistry 瓦片/常量），
//   不反向写。QML_NAMED_ELEMENT 经 import VoxelSandbox 解析（qt_add_qml_module 自动注册，同 BlockCube）。
class BedModelGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BedModelGeometry)
    Q_PROPERTY(int blockId READ blockId WRITE setBlockId NOTIFY blockIdChanged)

public:
    explicit BedModelGeometry(QQuick3DObject *parent = nullptr);

    int blockId() const { return m_blockId; }
    void setBlockId(int id);

signals:
    void blockIdChanged();

private:
    void rebuild(); // 按 m_blockId 取床色盒列表（双半）→ 建顶点/索引 → 整几何重传 GPU。

    int m_blockId = int(BlockRegistry::BedRed); // 默认红床（配方产物默认色；合法非空，防未设 blockId 时空几何）
};

#endif // BEDMODELGEOMETRY_H
