#ifndef BOW_H
#define BOW_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// 弓 3D 几何（t330 重绘）：弓臂为清晰的 C 形弧（XY 平面内、薄 Z 厚度）+ 握把缠绳。
//
// 旧版（t304）把弓臂弧放在 YZ 深度面（belly ±Z 仅 0.04→0.09 微凸）→ 从正面（相机沿 -Z 看）弧被
// 完全压缩成一根直棍，配深色细弦 → 观感「两根棍」（粗棍=弓身、细棍=弦）；且弦摆在凸侧 -Z（错侧）。
// t330 修正：弧改放 XY 面（正面可见的清晰 C 弯）+ 弦挪到凹侧（+X，连接两臂尖）+ 弦独立几何配白材质
// （蜘蛛丝白，与弓身 tier 木色分离）。机制对齐 MC 1.0 弓：垂直 C 形弓身 + 凹侧白弦 + 中央握把。
//
// 两个几何类（共享本文件，同层同风格，复制 addBox / addOrientedBox 模式）：
//   - BowGeometry       弓身（C 弧 + 握把），pos-only，QML 配 tier 木色 baseColor（与 Pickaxe/Sword 同路径）。
//   - BowStringGeometry 弓弦（凹侧竖直线），pos-only，QML 配白色 baseColor（独立于 tier）。作为 BowGeometry
//                       Model 的子节点消费（继承父变换）→ 一处摆位、两色分染。
//
// 顶点：弓身 10 盒 × 24 = 240；弓弦 1 盒 × 24 = 24。索引 36/盒。CCW 朝外。基准：握把中心 ≈ 原点
// （上臂尖 y≈+0.45、下臂尖 y≈-0.45、凹侧弦 x≈+0.06、凸侧握把 x≈-0.10）。消费点用 Model 的
// position/scale/eulerRotation 摆位定向（弓身平面 XY 正对相机 → 弧清晰可见）。
//
// 分层（PLAN §2）：本类属 Renderer（依赖 QtQuick3D），只产出几何。弓 tier / 名称在 ToolRegistry（Game 层）；
// 本类不读 tier、不持贴图——着色由 QML 呈现层据 hotbarVM.toolTier 设 baseColor（弓身）+ 硬编码白（弦）。
// 依赖只向下。与 sword.h / pickaxe.h 同层同风格。
class BowGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BowGeometry)

public:
    explicit BowGeometry(QQuick3DObject *parent = nullptr);
};

// 弓弦几何（t330）：凹侧（+X）竖直细线，连接上下臂尖。独立几何 → QML 配白色 PrincipledMaterial
// （蜘蛛丝白，与弓身 tier 木色分离；旧版整把同色致弦呈木色）。作为 BowGeometry Model 的子节点消费
// （继承父 position/scale/eulerRotation → 与弓身精确同位）。pos-only，同 BowGeometry 写入顺序。
class BowStringGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BowStringGeometry)

public:
    explicit BowStringGeometry(QQuick3DObject *parent = nullptr);
};

// 搭箭几何（t368）：拉弓时显于弓弦上的箭（nocks 贴弦中点）。箭尾（nocks）= 局部原点 (0,0,0)、
//   箭身沿 -X（朝准星 / 弓凹侧反向）：细木身 + 阔头镞（-X 端两段阶梯收尖）+ 尾羽（近 nocks 处 ±Y 加宽薄片）。
//   pos-only（同 BowGeometry / BowStringGeometry），单材质单色（QML 配 baseColor；身/镞/羽同色，靠几何造型辨识）。
//   作弓 Model 子节点消费（继承父变换）→ 一处摆位。机制等价 MC 1.0 拉弓搭箭（弦上横箭、镞朝前）。
//   顶点：4 盒 × 24 = 96；索引 36/盒。CCW 朝外。基准：nocks (0,0,0)、镞尖 (-0.27,0,0)。
class BowArrowGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BowArrowGeometry)

public:
    explicit BowArrowGeometry(QQuick3DObject *parent = nullptr);
};

#endif // BOW_H
