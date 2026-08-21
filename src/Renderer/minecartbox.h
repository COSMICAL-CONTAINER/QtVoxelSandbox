#ifndef MINECARTBOX_H
#define MINECARTBOX_H

#include <QtQuick3D/QQuick3DGeometry>
#include <QtQml/qqml.h>

// t732 矿车斗贴图盒几何（Renderer 层；Main.qml cartHost delegate 消费）。
//
// 用途：t565 矿车斗各部件（车底板 / 左右纵长车帮 / 前后端帮）从 UnitCube 纯色改为带 UV 的 ±0.5
// 居中单位盒（同 UnitCube/PlayerSkinBox 基准 → Main.qml 既有 position/scale 直接沿用），六面按
// 「部件 × 面」像素矩形表采样 64×32 系矿车贴图（铁灰壁 + 铆钉 + 木底板）。骑乘 / 物理 / 槽位逻辑
// 零改动（纯呈现层换装）。
//
// 双布局（layout）：qrc 程序贴图与 demo 包贴图的像素分区**实测不同**（t730 鱿鱼是标准 box-UV 族可
// 单表通用；矿车是 1.0 自定义渲染器时代的条带布局，各包排版有出入）→ 双表由呈现层按 pack 命中态
// 切换（同 MobModel.packTextured 双 UV 方案先例）：
//   0 = qrc 程序 entity_minecart.png（tools/build_entities_pack.py 实测：侧帮带 (0,4)-(44,20)
//       （上 2px 亮卷边 + 铆钉行）、木底板带 (0,20)-(44,28)）；
//   1 = demo 包 entity/minecart.png（512×256 = base 64×32 的 8×，实测：侧壁带 y[10,28)（左壁
//       x[2,22) / 右壁 x[24,44)，上 2px 亮卷边）、框栏暗端面 (0,2)-(20,10)、浅纹内壁 (20,2)-(36,10)、
//       木底 (46,12)-(62,24)；8× HD 下 UV 分数与 base 等价）。非 demo 排版的包按 1 采样会有窗口
//       偏差（呈现层降级可接受，与 mob 贴图对非标准包的容错同语义）。
//
// piece 部件（面序同 kFace：0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z；左右以矿车本地系为准，车头 = -Z）：
//   0 底板（大面 = ±Y 采木底板带；侧薄边被车帮遮蔽，兜底木条）
//   1 左纵帮（外 = -X 采左壁窗含亮卷边；内 = +X 采内壁区）
//   2 右纵帮（外 = +X 采右壁窗；内 = -X 采内壁区）
//   3 端帮（前后共用：大面 ±Z 采端面区——同一几何既作前壁外/内又作后壁外/内，两窗口取同区）
//
// 顶点格式：pos(3)+uv(2)=5 float；24 顶点 / 36 索引；CCW 朝外单面（默认 backface 剔除）。
//
// 分层（PLAN §2）：属 Renderer，与 playerskinbox / armorlayerbox 同层同风格；不读 Game/Core
// （贴图选择在 QML 呈现层，pack 探测在 Core entitySource），依赖只向下。
class MinecartBox : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MinecartBox)
    // 部件（0 底板 / 1 左帮 / 2 右帮 / 3 端帮；见类注释分区表）。setter 触发 rebuild 重算六面 UV 子区。
    Q_PROPERTY(int piece READ piece WRITE setPiece NOTIFY pieceChanged)
    // 贴图布局（0 = qrc 程序 64×32 / 1 = demo 包 8× 实测排版；见类注释双布局说明）。
    Q_PROPERTY(int layout READ layout WRITE setLayout NOTIFY layoutChanged)

public:
    explicit MinecartBox(QQuick3DObject *parent = nullptr);

    int piece() const { return m_piece; }
    void setPiece(int piece);

    int layout() const { return m_layout; }
    void setLayout(int layout);

signals:
    void pieceChanged();
    void layoutChanged();

private:
    void rebuild(); // 按 m_layout/m_piece 查「部件 × 六面」像素矩形表，建 ±0.5 单位盒。

    int m_piece = 0;   // 默认底板（合法非空，防未设 piece 时空几何）
    int m_layout = 0;  // 默认 qrc 程序布局（无包时的常态）
};

#endif // MINECARTBOX_H
